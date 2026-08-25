#include <openglad/gameplay/net_transport.h>

#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/world_snapshot.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value)
{
    bytes.push_back(value);
}

void append_bool(std::vector<std::uint8_t>& bytes, bool value)
{
    append_u8(bytes, value ? 1U : 0U);
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8)
    {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_i16(std::vector<std::uint8_t>& bytes, std::int16_t value)
{
    const std::uint16_t raw = static_cast<std::uint16_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(raw & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 8) & 0xffu));
}

void append_i32(std::vector<std::uint8_t>& bytes, std::int32_t value)
{
    append_u32(bytes, static_cast<std::uint32_t>(value));
}

void append_f32(std::vector<std::uint8_t>& bytes, float value)
{
    // Portable, alias-safe byte extraction (std::bit_cast is unavailable on the
    // older Emscripten libc++ used by the ctest wasm build).
    std::array<std::uint8_t, sizeof(float)> raw{};
    std::memcpy(raw.data(), &value, sizeof(float));
    bytes.insert(bytes.end(), raw.begin(), raw.end());
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("transport string exceeds maximum size");
    }

    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void append_bytes(std::vector<std::uint8_t>& bytes,
                  const std::vector<std::uint8_t>& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("transport byte blob exceeds maximum size");
    }

    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> wrap_transport_message(
    std::uint8_t message_type,
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() > std::numeric_limits<std::uint16_t>::max())
    {
        throw std::runtime_error("transport payload exceeds maximum size");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(og::sim::kTransportHeaderSize + payload.size());
    og::sim::append_transport_header(
        bytes,
        message_type,
        static_cast<std::uint16_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

std::optional<std::span<const std::uint8_t>> payload_span(
    std::span<const std::uint8_t> bytes,
    std::uint8_t expected_type)
{
    og::sim::TransportEnvelope envelope;
    if (!og::sim::decode_transport_envelope(bytes, envelope) ||
        envelope.protocol_version != og::sim::kNetworkProtocolVersion ||
        envelope.message_type != expected_type ||
        bytes.size() != og::sim::kTransportHeaderSize + envelope.payload_length)
    {
        return std::nullopt;
    }

    return bytes.subspan(og::sim::kTransportHeaderSize, envelope.payload_length);
}

class PayloadReader
{
public:
    explicit PayloadReader(std::span<const std::uint8_t> payload)
        : payload_(payload)
    {
    }

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] bool finished() const noexcept
    {
        return ok_ && offset_ == payload_.size();
    }
    [[nodiscard]] std::size_t remaining_bytes() const noexcept
    {
        return offset_ <= payload_.size() ? payload_.size() - offset_ : 0;
    }
    void fail() noexcept { ok_ = false; }

    std::uint8_t read_u8()
    {
        if (offset_ + 1 > payload_.size())
        {
            ok_ = false;
            return 0;
        }
        return payload_[offset_++];
    }

    bool read_bool()
    {
        return read_u8() != 0;
    }

    std::uint32_t read_u32()
    {
        if (offset_ + 4 > payload_.size())
        {
            ok_ = false;
            return 0;
        }

        const std::uint32_t value =
            static_cast<std::uint32_t>(payload_[offset_]) |
            (static_cast<std::uint32_t>(payload_[offset_ + 1]) << 8) |
            (static_cast<std::uint32_t>(payload_[offset_ + 2]) << 16) |
            (static_cast<std::uint32_t>(payload_[offset_ + 3]) << 24);
        offset_ += 4;
        return value;
    }

    std::int16_t read_i16()
    {
        if (offset_ + 2 > payload_.size())
        {
            ok_ = false;
            return 0;
        }

        const std::uint16_t value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(payload_[offset_]) |
            (static_cast<std::uint16_t>(payload_[offset_ + 1]) << 8));
        offset_ += 2;
        return static_cast<std::int16_t>(value);
    }

    std::int32_t read_i32()
    {
        return static_cast<std::int32_t>(read_u32());
    }

    float read_f32()
    {
        if (offset_ + sizeof(float) > payload_.size())
        {
            ok_ = false;
            return 0.0f;
        }

        float value = 0.0f;
        std::memcpy(&value, payload_.data() + offset_, sizeof(value));
        offset_ += sizeof(value);
        return value;
    }

    std::uint64_t read_u64()
    {
        if (offset_ + 8 > payload_.size())
        {
            ok_ = false;
            return 0;
        }

        std::uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 8)
        {
            value |= static_cast<std::uint64_t>(payload_[offset_++]) << shift;
        }
        return value;
    }

    std::vector<std::uint8_t> read_bytes(std::size_t max_size)
    {
        const std::uint32_t size = read_u32();
        // Same overflow-safe subtractive bound as read_string below.
        if (!ok_ || size > payload_.size() - offset_ || size > max_size)
        {
            ok_ = false;
            return {};
        }

        std::vector<std::uint8_t> value(
            payload_.begin() + static_cast<std::ptrdiff_t>(offset_),
            payload_.begin() + static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += size;
        return value;
    }

    std::string read_string()
    {
        const std::uint32_t size = read_u32();
        // offset_ <= payload_.size() always holds (every read_* advances only
        // after its own bounds check), so the subtractive comparison avoids the
        // size_t overflow that `offset_ + size` suffers on 32-bit targets
        // (wasm32) when `size` is an attacker-controlled uint32 near SIZE_MAX.
        if (!ok_ || size > payload_.size() - offset_)
        {
            ok_ = false;
            return {};
        }

        std::string value(payload_.begin() + static_cast<std::ptrdiff_t>(offset_),
                          payload_.begin() +
                              static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += size;
        return value;
    }

private:
    std::span<const std::uint8_t> payload_;
    std::size_t offset_ = 0;
    bool ok_ = true;
};

og::sim::TypedReceivedMessage malformed_typed_message(og::sim::PeerId peer_id)
{
    og::sim::TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = og::sim::TypedReceivedMessageKind::Malformed;
    return typed_message;
}

template <typename GuyLike>
void append_serialized_guy(std::vector<std::uint8_t>& payload,
                           const GuyLike& guy)
{
    append_i32(payload, guy.guy_id);
    append_string(payload, guy.name);
    append_u8(payload, static_cast<std::uint8_t>(guy.family));
    append_i16(payload, guy.strength);
    append_i16(payload, guy.dexterity);
    append_i16(payload, guy.constitution);
    append_i16(payload, guy.intelligence);
    append_i16(payload, guy.armor);
    append_u32(payload, guy.exp);
    append_i16(payload, guy.kills);
    append_i32(payload, guy.level_kills);
    append_i32(payload, guy.total_damage);
    append_i32(payload, guy.total_hits);
    append_i32(payload, guy.total_shots);
    append_i16(payload, guy.teamnum);
    append_f32(payload, guy.scen_damage);
    append_i16(payload, guy.scen_kills);
    append_f32(payload, guy.scen_damage_taken);
    append_f32(payload, guy.scen_min_hp);
    append_i16(payload, guy.scen_shots);
    append_i16(payload, guy.scen_hits);
    append_i16(payload, guy.level);
}

constexpr std::size_t kMinSerializedGuyDataSize = 59;

template <typename GuyLike>
GuyLike read_serialized_guy(PayloadReader& reader)
{
    GuyLike guy;
    guy.guy_id = reader.read_i32();
    // Clamp to the 11-char disk width (SaveData's 12-byte name field) instead
    // of trusting the sender: unbounded names re-serialize into the merged
    // LobbyState and can push it past the u16 transport cap. Honest writers
    // pre-clamp (the builders run clamp_lobby_guy_name), so this only alters
    // crafted payloads.
    guy.name = og::sim::clamp_lobby_guy_name(reader.read_string());
    // The family byte is unsigned on the wire (0..255): widen it as-is so
    // pack families >= 128 keep their id instead of aliasing negative.
    guy.family = reader.read_u8();
    guy.strength = reader.read_i16();
    guy.dexterity = reader.read_i16();
    guy.constitution = reader.read_i16();
    guy.intelligence = reader.read_i16();
    guy.armor = reader.read_i16();
    guy.exp = reader.read_u32();
    guy.kills = reader.read_i16();
    guy.level_kills = reader.read_i32();
    guy.total_damage = reader.read_i32();
    guy.total_hits = reader.read_i32();
    guy.total_shots = reader.read_i32();
    guy.teamnum = reader.read_i16();
    guy.scen_damage = reader.read_f32();
    guy.scen_kills = reader.read_i16();
    guy.scen_damage_taken = reader.read_f32();
    guy.scen_min_hp = reader.read_f32();
    guy.scen_shots = reader.read_i16();
    guy.scen_hits = reader.read_i16();
    guy.level = reader.read_i16();
    return guy;
}

void append_initial_setup_guy(std::vector<std::uint8_t>& payload,
                              const og::sim::InitialSetupGuyData& guy)
{
    append_serialized_guy(payload, guy);
}

og::sim::InitialSetupGuyData read_initial_setup_guy(PayloadReader& reader)
{
    return read_serialized_guy<og::sim::InitialSetupGuyData>(reader);
}

void append_lobby_settings(std::vector<std::uint8_t>& payload,
                           const og::sim::LobbySettings& settings)
{
    append_string(payload, settings.campaign_id);
    append_i16(payload, settings.scenario_id);
    append_i16(payload, settings.difficulty);
    append_i16(payload, settings.allied_mode);
    append_i16(payload, settings.ctf_team_count);
    append_u8(payload, settings.ctf_authored_team_mask);
    append_i16(payload, settings.ctf_capture_limit);
    append_i16(payload, settings.ctf_respawn_ticks);
    append_i16(payload, settings.ctf_strip_scenario_troops);
    append_i16(payload, settings.respawn_mode);
    append_i16(payload, settings.generator_rate);
    append_i16(payload, settings.keep_fallen_heroes);
    append_i16(payload, settings.cross_control);
    append_i16(payload, settings.infinite_gold);
    append_i16(payload, settings.shared_teams);
    append_i16(payload, settings.time_limit);
    // Protocol v16: the eight per-team bot knobs, appended LAST so every
    // earlier lobby payload offset keeps its number.
    for (const std::int16_t squad : settings.bot_squad)
        append_i16(payload, squad);
    for (const std::int16_t level : settings.bot_level)
        append_i16(payload, level);
}

og::sim::LobbySettings read_lobby_settings(PayloadReader& reader)
{
    og::sim::LobbySettings settings;
    settings.campaign_id = reader.read_string();
    settings.scenario_id = reader.read_i16();
    settings.difficulty = reader.read_i16();
    settings.allied_mode = reader.read_i16();
    settings.ctf_team_count = reader.read_i16();
    settings.ctf_authored_team_mask = reader.read_u8();
    settings.ctf_capture_limit = reader.read_i16();
    settings.ctf_respawn_ticks = reader.read_i16();
    settings.ctf_strip_scenario_troops = reader.read_i16();
    settings.respawn_mode = reader.read_i16();
    settings.generator_rate = reader.read_i16();
    settings.keep_fallen_heroes = reader.read_i16();
    settings.cross_control = reader.read_i16();
    settings.infinite_gold = reader.read_i16();
    settings.shared_teams = reader.read_i16();
    settings.time_limit = reader.read_i16();
    for (std::int16_t& squad : settings.bot_squad)
        squad = reader.read_i16();
    for (std::int16_t& level : settings.bot_level)
        level = reader.read_i16();
    return settings;
}

// v9 wire minimums: a slot is slot_index + slot_flags + the guy payload; a
// player is index u8 + stable seat_id u32 + machine_id u32 + two empty strings
// (name, company: u32 lengths) + team i16 + ready/is_host bools + the u32 slot
// count.
constexpr std::size_t kMinSerializedLobbyCharacterSlotSize =
    2 + kMinSerializedGuyDataSize;
constexpr std::size_t kMinSerializedLobbyPlayerSize = 25;

// LobbyCharacterSlot slot_flags byte (v8): bit0 = deployed. The writer
// zeroes bits 1-7; the reader masks bit0 and ignores the rest so future
// flag bits stay wire-compatible.
constexpr std::uint8_t kLobbySlotFlagDeployed = 0x01;

void append_lobby_character_slot(std::vector<std::uint8_t>& payload,
                                 const og::sim::LobbyCharacterSlot& slot)
{
    append_u8(payload, slot.slot_index);
    append_u8(payload, slot.deployed ? kLobbySlotFlagDeployed : 0x00u);
    append_serialized_guy(payload, slot.character);
}

og::sim::LobbyCharacterSlot read_lobby_character_slot(PayloadReader& reader)
{
    og::sim::LobbyCharacterSlot slot;
    slot.slot_index = reader.read_u8();
    slot.deployed = (reader.read_u8() & kLobbySlotFlagDeployed) != 0;
    slot.character = read_serialized_guy<og::sim::LobbyCharacterData>(reader);
    return slot;
}

void append_lobby_player(std::vector<std::uint8_t>& payload,
                         const og::sim::LobbyPlayer& player)
{
    append_u8(payload, player.player_index);
    append_u32(payload, player.seat_id);
    append_u32(payload, player.machine_id);
    append_string(payload, player.name);
    append_string(payload, player.company);
    append_i16(payload, player.team);
    append_bool(payload, player.ready);
    append_bool(payload, player.is_host);
    append_u32(payload,
               static_cast<std::uint32_t>(player.character_slots.size()));
    for (const auto& slot : player.character_slots)
        append_lobby_character_slot(payload, slot);
}

og::sim::LobbyPlayer read_lobby_player(PayloadReader& reader)
{
    og::sim::LobbyPlayer player;
    player.player_index = reader.read_u8();
    player.seat_id = reader.read_u32();
    player.machine_id = reader.read_u32();
    player.name = og::sim::clamp_lobby_player_label(reader.read_string());
    player.company = og::sim::clamp_lobby_player_label(reader.read_string());
    player.team = reader.read_i16();
    player.ready = reader.read_bool();
    player.is_host = reader.read_bool();
    const std::uint32_t slot_count = reader.read_u32();
    if (!reader.ok() ||
        slot_count >
            reader.remaining_bytes() / kMinSerializedLobbyCharacterSlotSize)
    {
        reader.fail();
        return player;
    }

    player.character_slots.clear();
    player.character_slots.reserve(slot_count);
    for (std::uint32_t i = 0; i < slot_count && reader.ok(); ++i)
        player.character_slots.push_back(read_lobby_character_slot(reader));
    return player;
}

template <typename Message>
std::optional<Message> deserialize_message(
    std::span<const std::uint8_t> bytes,
    std::uint8_t expected_type,
    const std::function<void(PayloadReader&, Message&)>& decode)
{
    const std::optional<std::span<const std::uint8_t>> payload =
        payload_span(bytes, expected_type);
    if (!payload.has_value())
        return std::nullopt;

    PayloadReader reader(*payload);
    Message message;
    decode(reader, message);
    if (!reader.finished())
        return std::nullopt;
    return message;
}

} // namespace

namespace og::sim {

std::vector<std::uint8_t> serialize_initial_setup_message(
    const InitialSetupMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_i32(payload, message.level_id);
    append_string(payload, message.level_title);
    append_u8(payload, static_cast<std::uint8_t>(message.level_type));
    append_i16(payload, message.par_value);
    append_i16(payload, message.time_bonus_limit);
    append_i16(payload, message.difficulty);
    append_i32(payload, message.pixmaxx);
    append_i32(payload, message.pixmaxy);
    append_i16(payload, message.my_team);
    append_i16(payload, message.allied_mode);
    append_i16(payload, message.current_scenario);
    append_i16(payload, message.respawn_mode);
    append_i16(payload, message.generator_rate);
    // v13: the ready-resetting setup generation (see net_transport.h).
    append_u32(payload, message.setup_generation);
    append_u32(payload, static_cast<std::uint32_t>(message.guys.size()));
    for (const auto& guy : message.guys)
        append_initial_setup_guy(payload, guy);
    append_u32(payload, static_cast<std::uint32_t>(message.completed_levels.size()));
    for (const std::int32_t level_id : message.completed_levels)
        append_i32(payload, level_id);
    // v7: u8 count prefix (sender always writes the full kMaxGlobalPlayers).
    append_u8(payload,
              static_cast<std::uint8_t>(message.controlled_entity_ids.size()));
    for (const std::uint32_t entity_id : message.controlled_entity_ids)
        append_u32(payload, entity_id);
    return wrap_transport_message(kInitialSetupMessageType, payload);
}

std::optional<InitialSetupMessage> deserialize_initial_setup_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<InitialSetupMessage>(
        bytes, kInitialSetupMessageType,
        [](PayloadReader& reader, InitialSetupMessage& message) {
            message.level_id = reader.read_i32();
            message.level_title = reader.read_string();
            message.level_type = static_cast<std::int8_t>(reader.read_u8());
            message.par_value = reader.read_i16();
            message.time_bonus_limit = reader.read_i16();
            message.difficulty = reader.read_i16();
            message.pixmaxx = reader.read_i32();
            message.pixmaxy = reader.read_i32();
            message.my_team = reader.read_i16();
            message.allied_mode = reader.read_i16();
            message.current_scenario = reader.read_i16();
            message.respawn_mode = reader.read_i16();
            message.generator_rate = reader.read_i16();
            message.setup_generation = reader.read_u32();
            const std::uint32_t guy_count = reader.read_u32();
            if (!reader.ok() ||
                guy_count >
                    reader.remaining_bytes() / kMinSerializedGuyDataSize)
            {
                reader.fail();
                return;
            }
            message.guys.clear();
            message.guys.reserve(guy_count);
            for (std::uint32_t i = 0; i < guy_count && reader.ok(); ++i)
                message.guys.push_back(read_initial_setup_guy(reader));
            const std::uint32_t level_count = reader.read_u32();
            if (!reader.ok() ||
                level_count > reader.remaining_bytes() / sizeof(std::int32_t))
            {
                reader.fail();
                return;
            }
            message.completed_levels.clear();
            message.completed_levels.reserve(level_count);
            for (std::uint32_t i = 0; i < level_count && reader.ok(); ++i)
                message.completed_levels.push_back(reader.read_i32());
            // v7: count-prefixed controlled ids. Reject oversized counts and
            // zero-fill any tail a shorter (still valid) sender leaves.
            const std::uint8_t controlled_count = reader.read_u8();
            if (!reader.ok() ||
                static_cast<std::size_t>(controlled_count) >
                    message.controlled_entity_ids.size())
            {
                reader.fail();
                return;
            }
            message.controlled_entity_ids.fill(0u);
            for (std::uint8_t i = 0; i < controlled_count && reader.ok(); ++i)
                message.controlled_entity_ids[i] = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_lobby_message(const LobbyMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u8(payload, lobby_message_kind_value(message.kind()));
    std::visit(
        [&payload](const auto& lobby_message) {
            using Message = std::decay_t<decltype(lobby_message)>;
            if constexpr (std::is_same_v<Message, LobbyJoinMessage>)
            {
                // v9: one declaration/retry generation. Keeping the nonce
                // before the variable-size seat records makes malformed
                // payloads fail before any potentially large allocations.
                append_u32(payload, lobby_message.request_id);
                append_bool(payload, lobby_message.resume_after_level);
                append_lobby_player(payload, lobby_message.player);
                // v7: extra seats (1..N-1) after seat 0.
                append_u8(payload,
                          static_cast<std::uint8_t>(std::min<std::size_t>(
                              lobby_message.extra_players.size(), 0xffu)));
                for (const auto& extra : lobby_message.extra_players)
                    append_lobby_player(payload, extra);
            }
            else if constexpr (std::is_same_v<Message, LobbyLeaveMessage>)
            {
                append_u8(payload, lobby_message.player_index);
            }
            else if constexpr (std::is_same_v<Message, LobbyReadyMessage>)
            {
                append_u8(payload, lobby_message.player_index);
                append_bool(payload, lobby_message.ready);
            }
            else if constexpr (std::is_same_v<Message, LobbyTeamChangeMessage>)
            {
                append_u8(payload, lobby_message.player_index);
                append_u32(payload, lobby_message.seat_id);
                append_i16(payload, lobby_message.team);
            }
            else if constexpr (std::is_same_v<Message, LobbyRemoveSeatMessage>)
            {
                append_u8(payload, lobby_message.player_index);
                append_u32(payload, lobby_message.seat_id);
            }
            else if constexpr (std::is_same_v<Message, LobbyStartGameMessage>)
            {
                append_u8(payload, lobby_message.player_index);
                append_u32(payload, lobby_message.request_id);
            }
            else if constexpr (std::is_same_v<Message,
                                              LobbySettingsChangeMessage>)
            {
                append_u8(payload, lobby_message.player_index);
                append_lobby_settings(payload, lobby_message.settings);
            }
            else if constexpr (std::is_same_v<Message, LobbyKickMessage>)
            {
                append_u32(payload, lobby_message.machine_id);
            }
            else if constexpr (std::is_same_v<Message, LobbyKickedMessage>)
            {
                // No payload: the kind byte IS the whole message.
            }
        },
        message.payload);
    return wrap_transport_message(kLobbyMessageType, payload);
}

std::optional<LobbyMessage> deserialize_lobby_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<LobbyMessage>(
        bytes, kLobbyMessageType,
        [](PayloadReader& reader, LobbyMessage& message) {
            const LobbyMessageKind kind =
                static_cast<LobbyMessageKind>(reader.read_u8());
            switch (kind)
            {
            case LobbyMessageKind::Join:
            {
                LobbyJoinMessage join;
                join.request_id = reader.read_u32();
                join.resume_after_level = reader.read_bool();
                join.player = read_lobby_player(reader);
                // v7: u8 extra-seat count, bounds-checked against the minimum
                // serialized seat size so a crafted count cannot over-reserve.
                const std::uint8_t extra_count = reader.read_u8();
                if (!reader.ok() ||
                    static_cast<std::size_t>(extra_count) >
                        reader.remaining_bytes() / kMinSerializedLobbyPlayerSize)
                {
                    reader.fail();
                    return;
                }
                join.extra_players.reserve(extra_count);
                for (std::uint8_t i = 0; i < extra_count && reader.ok(); ++i)
                    join.extra_players.push_back(read_lobby_player(reader));
                message.payload = std::move(join);
                break;
            }

            case LobbyMessageKind::Leave:
            {
                LobbyLeaveMessage leave;
                leave.player_index = reader.read_u8();
                message.payload = leave;
                break;
            }

            case LobbyMessageKind::Ready:
            {
                LobbyReadyMessage ready;
                ready.player_index = reader.read_u8();
                ready.ready = reader.read_bool();
                message.payload = ready;
                break;
            }

            case LobbyMessageKind::TeamChange:
            {
                LobbyTeamChangeMessage team_change;
                team_change.player_index = reader.read_u8();
                team_change.seat_id = reader.read_u32();
                team_change.team = reader.read_i16();
                message.payload = team_change;
                break;
            }

            case LobbyMessageKind::RemoveSeat:
            {
                LobbyRemoveSeatMessage remove_seat;
                remove_seat.player_index = reader.read_u8();
                remove_seat.seat_id = reader.read_u32();
                message.payload = remove_seat;
                break;
            }

            case LobbyMessageKind::StartGame:
            {
                LobbyStartGameMessage start_game;
                start_game.player_index = reader.read_u8();
                start_game.request_id = reader.read_u32();
                message.payload = start_game;
                break;
            }

            case LobbyMessageKind::SettingsChange:
            {
                LobbySettingsChangeMessage settings_change;
                settings_change.player_index = reader.read_u8();
                settings_change.settings = read_lobby_settings(reader);
                message.payload = std::move(settings_change);
                break;
            }

            case LobbyMessageKind::Kick:
            {
                LobbyKickMessage kick;
                kick.machine_id = reader.read_u32();
                message.payload = kick;
                break;
            }

            case LobbyMessageKind::Kicked:
                message.payload = LobbyKickedMessage{};
                break;

            default:
                reader.fail();
                return;
            }
        });
}

std::vector<std::uint8_t> serialize_lobby_state_message(const LobbyState& state)
{
    std::vector<std::uint8_t> payload;
    append_lobby_settings(payload, state.settings);
    append_u8(payload, state.host_player_id);
    append_u8(payload, state.last_start_denial);
    append_u32(payload, state.last_start_request_id);
    append_u32(payload, static_cast<std::uint32_t>(state.players.size()));
    for (const auto& player : state.players)
        append_lobby_player(payload, player);
    append_u8(payload,
              static_cast<std::uint8_t>(std::min<std::size_t>(
                  state.local_seat_ids.size(), MAX_PLAYERS)));
    for (std::size_t index = 0;
         index < state.local_seat_ids.size() &&
         index < static_cast<std::size_t>(MAX_PLAYERS);
         ++index)
    {
        append_u32(payload, state.local_seat_ids[index]);
    }
    append_u32(payload, state.last_join_request_id);
    append_bool(payload, state.local_peer_is_host);
    return wrap_transport_message(kLobbyStateMessageType, payload);
}

std::optional<LobbyState> deserialize_lobby_state_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<LobbyState>(
        bytes, kLobbyStateMessageType,
        [](PayloadReader& reader, LobbyState& state) {
            state.settings = read_lobby_settings(reader);
            state.host_player_id = reader.read_u8();
            state.last_start_denial = reader.read_u8();
            state.last_start_request_id = reader.read_u32();
            const std::uint32_t player_count = reader.read_u32();
            if (!reader.ok() ||
                player_count >
                    reader.remaining_bytes() / kMinSerializedLobbyPlayerSize)
            {
                reader.fail();
                return;
            }

            state.players.clear();
            state.players.reserve(player_count);
            for (std::uint32_t i = 0; i < player_count && reader.ok(); ++i)
                state.players.push_back(read_lobby_player(reader));

            const std::uint8_t local_seat_count = reader.read_u8();
            const std::size_t remaining = reader.remaining_bytes();
            if (!reader.ok() ||
                remaining < sizeof(std::uint32_t) + sizeof(std::uint8_t) ||
                static_cast<std::size_t>(local_seat_count) >
                    (remaining - sizeof(std::uint32_t) -
                     sizeof(std::uint8_t)) /
                        sizeof(LobbySeatId) ||
                local_seat_count > MAX_PLAYERS)
            {
                reader.fail();
                return;
            }
            state.local_seat_ids.clear();
            state.local_seat_ids.reserve(local_seat_count);
            for (std::uint8_t i = 0; i < local_seat_count && reader.ok(); ++i)
                state.local_seat_ids.push_back(reader.read_u32());
            state.last_join_request_id = reader.read_u32();
            state.local_peer_is_host = reader.read_bool();
        });
}

std::vector<std::uint8_t> serialize_client_ready_message(
    const ClientReadyMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u32(payload, message.last_applied_tick);
    return wrap_transport_message(kClientReadyMessageType, payload);
}

std::optional<ClientReadyMessage> deserialize_client_ready_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<ClientReadyMessage>(
        bytes, kClientReadyMessageType,
        [](PayloadReader& reader, ClientReadyMessage& message) {
            message.last_applied_tick = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_heartbeat_message(
    const HeartbeatMessage& message)
{
    (void)message;
    return wrap_transport_message(kHeartbeatMessageType, {});
}

std::optional<HeartbeatMessage> deserialize_heartbeat_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<HeartbeatMessage>(
        bytes, kHeartbeatMessageType,
        [](PayloadReader& reader, HeartbeatMessage& message) {
            (void)message;
            if (reader.remaining_bytes() != 0)
                reader.fail();
        });
}

std::vector<std::uint8_t> serialize_keyframe_request_message(
    const KeyframeRequestMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u32(payload, message.last_seen_tick);
    return wrap_transport_message(kKeyframeRequestMessageType, payload);
}

std::optional<KeyframeRequestMessage> deserialize_keyframe_request_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<KeyframeRequestMessage>(
        bytes, kKeyframeRequestMessageType,
        [](PayloadReader& reader, KeyframeRequestMessage& message) {
            message.last_seen_tick = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_exit_prompt_broadcast_message(
    const ExitPromptBroadcastMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_i16(payload, message.destination_level);
    append_bool(payload, message.withdraw_prompt);
    append_string(payload, message.prompt_text);
    return wrap_transport_message(kExitPromptBroadcastMessageType, payload);
}

std::optional<ExitPromptBroadcastMessage>
deserialize_exit_prompt_broadcast_message(std::span<const std::uint8_t> bytes)
{
    return deserialize_message<ExitPromptBroadcastMessage>(
        bytes, kExitPromptBroadcastMessageType,
        [](PayloadReader& reader, ExitPromptBroadcastMessage& message) {
            message.destination_level = reader.read_i16();
            message.withdraw_prompt = reader.read_bool();
            message.prompt_text = reader.read_string();
        });
}

std::vector<std::uint8_t> serialize_exit_prompt_response_message(
    const ExitPromptResponseMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_bool(payload, message.accepted);
    append_bool(payload, message.abort_request);
    return wrap_transport_message(kExitPromptResponseMessageType, payload);
}

std::optional<ExitPromptResponseMessage>
deserialize_exit_prompt_response_message(std::span<const std::uint8_t> bytes)
{
    return deserialize_message<ExitPromptResponseMessage>(
        bytes, kExitPromptResponseMessageType,
        [](PayloadReader& reader, ExitPromptResponseMessage& message) {
            message.accepted = reader.read_bool();
            message.abort_request = reader.read_bool();
        });
}

std::vector<std::uint8_t> serialize_pause_broadcast_message(
    const PauseBroadcastMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u8(payload, message.player_index);
    append_string(payload, message.player_name);
    return wrap_transport_message(kPauseBroadcastMessageType, payload);
}

std::optional<PauseBroadcastMessage> deserialize_pause_broadcast_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<PauseBroadcastMessage>(
        bytes, kPauseBroadcastMessageType,
        [](PayloadReader& reader, PauseBroadcastMessage& message) {
            message.player_index = reader.read_u8();
            message.player_name = reader.read_string();
        });
}

std::vector<std::uint8_t> serialize_pause_response_message(
    const PauseResponseMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_bool(payload, message.resume);
    return wrap_transport_message(kPauseResponseMessageType, payload);
}

std::optional<PauseResponseMessage> deserialize_pause_response_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<PauseResponseMessage>(
        bytes, kPauseResponseMessageType,
        [](PayloadReader& reader, PauseResponseMessage& message) {
            message.resume = reader.read_bool();
        });
}

std::vector<std::uint8_t> serialize_control_change_message(
    const ControlChangeMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u8(payload, message.player_index);
    append_u32(payload, message.entity_id);
    return wrap_transport_message(kControlChangeMessageType, payload);
}

std::optional<ControlChangeMessage> deserialize_control_change_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<ControlChangeMessage>(
        bytes, kControlChangeMessageType,
        [](PayloadReader& reader, ControlChangeMessage& message) {
            message.player_index = reader.read_u8();
            message.entity_id = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_snapshot_hash_check_message(
    const SnapshotHashCheckMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u32(payload, message.tick);
    append_u32(payload, message.snapshot_hash);
    return wrap_transport_message(kSnapshotHashCheckMessageType, payload);
}

std::optional<SnapshotHashCheckMessage> deserialize_snapshot_hash_check_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<SnapshotHashCheckMessage>(
        bytes, kSnapshotHashCheckMessageType,
        [](PayloadReader& reader, SnapshotHashCheckMessage& message) {
            message.tick = reader.read_u32();
            message.snapshot_hash = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_pack_manifest_message(
    const PackManifestMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u8(payload, message.pack_index);
    append_u8(payload, message.pack_count);
    append_string(payload, message.pack_id);
    append_string(payload, message.version);
    append_u32(payload, static_cast<std::uint32_t>(message.files.size()));
    for (const PackManifestFileEntry& file : message.files)
    {
        append_string(payload, file.path);
        append_u32(payload, file.size_bytes);
        append_u64(payload, file.hash64);
    }
    return wrap_transport_message(kPackManifestMessageType, payload);
}

std::optional<PackManifestMessage> deserialize_pack_manifest_message(
    std::span<const std::uint8_t> bytes)
{
    // Minimum serialized file entry: u32 path length (empty) + u32 size +
    // u64 hash.
    constexpr std::size_t kMinSerializedManifestFileSize = 16;

    return deserialize_message<PackManifestMessage>(
        bytes, kPackManifestMessageType,
        [](PayloadReader& reader, PackManifestMessage& message) {
            message.pack_index = reader.read_u8();
            message.pack_count = reader.read_u8();
            message.pack_id = reader.read_string();
            message.version = reader.read_string();
            if (!reader.ok() ||
                message.pack_id.size() > kMaxPackIdLength ||
                message.version.size() > kMaxPackVersionLength)
            {
                reader.fail();
                return;
            }
            if (message.pack_count == 0)
            {
                // Explicit "no packs" announcement: nothing else may follow.
                if (message.pack_index != 0 || !message.pack_id.empty() ||
                    !message.version.empty() || reader.read_u32() != 0)
                {
                    reader.fail();
                }
                return;
            }
            if (message.pack_index >= message.pack_count ||
                message.pack_count > kMaxPacksPerSession ||
                message.pack_id.empty())
            {
                reader.fail();
                return;
            }
            const std::uint32_t file_count = reader.read_u32();
            if (!reader.ok() || file_count > kMaxPackManifestFiles ||
                file_count >
                    reader.remaining_bytes() / kMinSerializedManifestFileSize)
            {
                reader.fail();
                return;
            }
            message.files.reserve(file_count);
            for (std::uint32_t i = 0; i < file_count && reader.ok(); ++i)
            {
                PackManifestFileEntry file;
                file.path = reader.read_string();
                file.size_bytes = reader.read_u32();
                file.hash64 = reader.read_u64();
                if (file.path.empty() ||
                    file.path.size() > kMaxPackRelativePathLength)
                {
                    reader.fail();
                    return;
                }
                message.files.push_back(std::move(file));
            }
        });
}

std::vector<std::uint8_t> serialize_pack_request_message(
    const PackRequestMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_string(payload, message.pack_id);
    return wrap_transport_message(kPackRequestMessageType, payload);
}

std::optional<PackRequestMessage> deserialize_pack_request_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<PackRequestMessage>(
        bytes, kPackRequestMessageType,
        [](PayloadReader& reader, PackRequestMessage& message) {
            message.pack_id = reader.read_string();
            if (message.pack_id.empty() ||
                message.pack_id.size() > kMaxPackIdLength)
            {
                reader.fail();
            }
        });
}

std::vector<std::uint8_t> serialize_pack_file_chunk_message(
    const PackFileChunkMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_string(payload, message.pack_id);
    append_u32(payload, message.file_index);
    append_u32(payload, message.offset);
    append_bytes(payload, message.data);
    return wrap_transport_message(kPackFileChunkMessageType, payload);
}

std::optional<PackFileChunkMessage> deserialize_pack_file_chunk_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<PackFileChunkMessage>(
        bytes, kPackFileChunkMessageType,
        [](PayloadReader& reader, PackFileChunkMessage& message) {
            message.pack_id = reader.read_string();
            if (message.pack_id.empty() ||
                message.pack_id.size() > kMaxPackIdLength)
            {
                reader.fail();
                return;
            }
            message.file_index = reader.read_u32();
            message.offset = reader.read_u32();
            message.data = reader.read_bytes(kPackFileChunkMaxBytes);
        });
}

std::vector<std::uint8_t> serialize_pack_transfer_done_message(
    const PackTransferDoneMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_string(payload, message.pack_id);
    return wrap_transport_message(kPackTransferDoneMessageType, payload);
}

std::optional<PackTransferDoneMessage> deserialize_pack_transfer_done_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<PackTransferDoneMessage>(
        bytes, kPackTransferDoneMessageType,
        [](PayloadReader& reader, PackTransferDoneMessage& message) {
            message.pack_id = reader.read_string();
            if (message.pack_id.empty() ||
                message.pack_id.size() > kMaxPackIdLength)
            {
                reader.fail();
            }
        });
}

namespace {

// Shared v13 staged-wrapper writer: u32 generation + length-prefixed inner
// message bytes. Refuses (empty result, never a throw) when the inner
// message would push the wrapper payload past the u16 transport cap, so the
// owner's poll loop can never die on an oversize staged world — it reports
// StageStatus::Failed instead (#218).
std::vector<std::uint8_t> serialize_staged_wrapper(
    std::uint8_t message_type,
    std::uint32_t stage_generation,
    const std::vector<std::uint8_t>& inner_bytes)
{
    if (inner_bytes.empty() ||
        inner_bytes.size() > kMaxStagedInnerMessageBytes)
    {
        return {};
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(8 + inner_bytes.size());
    append_u32(payload, stage_generation);
    append_bytes(payload, inner_bytes);
    return wrap_transport_message(message_type, payload);
}

} // namespace

std::vector<std::uint8_t> serialize_staged_match_setup_message(
    const StagedMatchSetupMessage& message)
{
    return serialize_staged_wrapper(kStagedMatchSetupMessageType,
                                    message.stage_generation,
                                    message.setup_bytes);
}

std::optional<StagedMatchSetupMessage> deserialize_staged_match_setup_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<StagedMatchSetupMessage>(
        bytes, kStagedMatchSetupMessageType,
        [](PayloadReader& reader, StagedMatchSetupMessage& message) {
            message.stage_generation = reader.read_u32();
            message.setup_bytes =
                reader.read_bytes(kMaxStagedInnerMessageBytes);
            if (message.setup_bytes.empty())
                reader.fail();
        });
}

std::vector<std::uint8_t> serialize_staged_match_keyframe_message(
    const StagedMatchKeyframeMessage& message)
{
    return serialize_staged_wrapper(kStagedMatchKeyframeMessageType,
                                    message.stage_generation,
                                    message.snapshot_bytes);
}

std::optional<StagedMatchKeyframeMessage>
deserialize_staged_match_keyframe_message(std::span<const std::uint8_t> bytes)
{
    return deserialize_message<StagedMatchKeyframeMessage>(
        bytes, kStagedMatchKeyframeMessageType,
        [](PayloadReader& reader, StagedMatchKeyframeMessage& message) {
            message.stage_generation = reader.read_u32();
            message.snapshot_bytes =
                reader.read_bytes(kMaxStagedInnerMessageBytes);
            if (message.snapshot_bytes.empty())
                reader.fail();
        });
}

bool ITransport::supports_typed_messages() const noexcept
{
    return false;
}

void ITransport::broadcast(const std::uint8_t* data, std::size_t len)
{
    for (const PeerId peer_id : connected_peers())
        send(peer_id, data, len);
}

void ITransport::send_snapshot(PeerId peer_id,
                               std::shared_ptr<WorldSnapshot> snapshot)
{
    if (!snapshot)
        return;

    const std::vector<std::uint8_t> bytes = serialize_snapshot(*snapshot);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_delta_snapshot(PeerId peer_id,
                                     std::shared_ptr<WorldSnapshot> snapshot)
{
    if (!snapshot)
        return;

    const std::vector<std::uint8_t> bytes = serialize_delta(*snapshot);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_input(PeerId peer_id,
                            std::shared_ptr<InputState> input,
                            std::uint32_t tick)
{
    if (!input)
        return;

    const auto bytes = serialize_input(tick, *input);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_sim_event_batch(PeerId peer_id,
                                      std::shared_ptr<SimEventBatch> batch)
{
    if (!batch)
        return;

    const std::vector<std::uint8_t> bytes = serialize_sim_event_batch(*batch);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_game_flow_event_batch(
    PeerId peer_id,
    std::shared_ptr<SimEventBatch> batch)
{
    if (!batch)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_game_flow_event_batch(*batch);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_lobby_message(PeerId peer_id,
                                    std::shared_ptr<LobbyMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes = serialize_lobby_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_lobby_state(PeerId peer_id,
                                  std::shared_ptr<LobbyState> state)
{
    if (!state)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_lobby_state_message(*state);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_initial_setup(PeerId peer_id,
                                    std::shared_ptr<InitialSetupMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_initial_setup_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_hello(PeerId peer_id,
                            std::shared_ptr<HelloMessage> message)
{
    if (!message)
        return;

    const auto bytes = serialize_hello(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_client_ready(PeerId peer_id,
                                   std::shared_ptr<ClientReadyMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_client_ready_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_keyframe_request(
    PeerId peer_id,
    std::shared_ptr<KeyframeRequestMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_keyframe_request_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_heartbeat(
    PeerId peer_id,
    std::shared_ptr<HeartbeatMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_heartbeat_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_exit_prompt_broadcast(
    PeerId peer_id,
    std::shared_ptr<ExitPromptBroadcastMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_exit_prompt_broadcast_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_exit_prompt_response(
    PeerId peer_id,
    std::shared_ptr<ExitPromptResponseMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_exit_prompt_response_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_pause_broadcast(
    PeerId peer_id,
    std::shared_ptr<PauseBroadcastMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_pause_broadcast_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_pause_response(
    PeerId peer_id,
    std::shared_ptr<PauseResponseMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_pause_response_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_control_change(
    PeerId peer_id,
    std::shared_ptr<ControlChangeMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_control_change_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_snapshot_hash_check(
    PeerId peer_id,
    std::shared_ptr<SnapshotHashCheckMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_snapshot_hash_check_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_pack_manifest(PeerId peer_id,
                                    std::shared_ptr<PackManifestMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_pack_manifest_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_pack_request(PeerId peer_id,
                                   std::shared_ptr<PackRequestMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_pack_request_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_pack_file_chunk(
    PeerId peer_id,
    std::shared_ptr<PackFileChunkMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_pack_file_chunk_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_pack_transfer_done(
    PeerId peer_id,
    std::shared_ptr<PackTransferDoneMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_pack_transfer_done_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_staged_match_setup(
    PeerId peer_id,
    std::shared_ptr<StagedMatchSetupMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_staged_match_setup_message(*message);
    if (bytes.empty()) // oversize refusal — the owner gates this pre-send
        return;
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_staged_match_keyframe(
    PeerId peer_id,
    std::shared_ptr<StagedMatchKeyframeMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_staged_match_keyframe_message(*message);
    if (bytes.empty()) // oversize refusal — the owner gates this pre-send
        return;
    send(peer_id, bytes.data(), bytes.size());
}

TypedReceivedMessage decode_received_message(const ReceivedMessage& message)
{
    TransportEnvelope envelope;
    if (!decode_transport_envelope(message.data, envelope))
        return malformed_typed_message(message.peer_id);

    TypedReceivedMessage typed_message;
    typed_message.peer_id = message.peer_id;

    try
    {
        switch (envelope.message_type)
        {
        case kSnapshotMessageType:
            typed_message.kind = TypedReceivedMessageKind::Snapshot;
            typed_message.snapshot = std::make_shared<WorldSnapshot>(
                deserialize_snapshot(message.data));
            return typed_message;

        case kDeltaSnapshotMessageType:
            typed_message.kind = TypedReceivedMessageKind::DeltaSnapshot;
            typed_message.snapshot = std::make_shared<WorldSnapshot>(
                deserialize_delta(message.data));
            return typed_message;

        case kInputMessageType:
        {
            const std::optional<InputStateMessage> decoded =
                deserialize_input_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::Input;
            typed_message.input = std::make_shared<InputState>(decoded->input);
            typed_message.tick = decoded->tick;
            return typed_message;
        }

        case kSimEventBatchMessageType:
            typed_message.kind = TypedReceivedMessageKind::SimEventBatch;
            typed_message.event_batch = std::make_shared<SimEventBatch>(
                deserialize_sim_event_batch(message.data));
            return typed_message;

        case kGameFlowEventBatchMessageType:
            typed_message.kind = TypedReceivedMessageKind::GameFlowEventBatch;
            typed_message.event_batch = std::make_shared<SimEventBatch>(
                deserialize_game_flow_event_batch(message.data));
            return typed_message;

        case kLobbyMessageType:
        {
            const auto decoded = deserialize_lobby_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::LobbyMessage;
            typed_message.lobby_message =
                std::make_shared<LobbyMessage>(std::move(*decoded));
            return typed_message;
        }

        case kLobbyStateMessageType:
        {
            const auto decoded = deserialize_lobby_state_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::LobbyState;
            typed_message.lobby_state =
                std::make_shared<LobbyState>(std::move(*decoded));
            return typed_message;
        }

        case kInitialSetupMessageType:
        {
            const auto decoded = deserialize_initial_setup_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::InitialSetup;
            typed_message.initial_setup =
                std::make_shared<InitialSetupMessage>(std::move(*decoded));
            return typed_message;
        }

        case kHelloMessageType:
        {
            const auto decoded = deserialize_hello_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::Hello;
            typed_message.hello =
                std::make_shared<HelloMessage>(std::move(*decoded));
            return typed_message;
        }

        case kClientReadyMessageType:
        {
            const auto decoded = deserialize_client_ready_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::ClientReady;
            typed_message.client_ready =
                std::make_shared<ClientReadyMessage>(std::move(*decoded));
            return typed_message;
        }

        case kKeyframeRequestMessageType:
        {
            const auto decoded = deserialize_keyframe_request_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::KeyframeRequest;
            typed_message.keyframe_request =
                std::make_shared<KeyframeRequestMessage>(std::move(*decoded));
            return typed_message;
        }

        case kHeartbeatMessageType:
        {
            const auto decoded = deserialize_heartbeat_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::Heartbeat;
            typed_message.heartbeat =
                std::make_shared<HeartbeatMessage>(std::move(*decoded));
            return typed_message;
        }

        case kExitPromptBroadcastMessageType:
        {
            const auto decoded =
                deserialize_exit_prompt_broadcast_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::ExitPromptBroadcast;
            typed_message.exit_prompt_broadcast =
                std::make_shared<ExitPromptBroadcastMessage>(std::move(*decoded));
            return typed_message;
        }

        case kExitPromptResponseMessageType:
        {
            const auto decoded =
                deserialize_exit_prompt_response_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::ExitPromptResponse;
            typed_message.exit_prompt_response =
                std::make_shared<ExitPromptResponseMessage>(std::move(*decoded));
            return typed_message;
        }

        case kPauseBroadcastMessageType:
        {
            const auto decoded = deserialize_pause_broadcast_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::PauseBroadcast;
            typed_message.pause_broadcast =
                std::make_shared<PauseBroadcastMessage>(std::move(*decoded));
            return typed_message;
        }

        case kPauseResponseMessageType:
        {
            const auto decoded = deserialize_pause_response_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::PauseResponse;
            typed_message.pause_response =
                std::make_shared<PauseResponseMessage>(std::move(*decoded));
            return typed_message;
        }

        case kControlChangeMessageType:
        {
            const auto decoded = deserialize_control_change_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::ControlChange;
            typed_message.control_change =
                std::make_shared<ControlChangeMessage>(std::move(*decoded));
            return typed_message;
        }

        case kSnapshotHashCheckMessageType:
        {
            const auto decoded =
                deserialize_snapshot_hash_check_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::SnapshotHashCheck;
            typed_message.snapshot_hash_check =
                std::make_shared<SnapshotHashCheckMessage>(std::move(*decoded));
            return typed_message;
        }

        case kPackManifestMessageType:
        {
            const auto decoded = deserialize_pack_manifest_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::PackManifest;
            typed_message.pack_manifest =
                std::make_shared<PackManifestMessage>(std::move(*decoded));
            return typed_message;
        }

        case kPackRequestMessageType:
        {
            const auto decoded = deserialize_pack_request_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::PackRequest;
            typed_message.pack_request =
                std::make_shared<PackRequestMessage>(std::move(*decoded));
            return typed_message;
        }

        case kPackFileChunkMessageType:
        {
            const auto decoded =
                deserialize_pack_file_chunk_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::PackFileChunk;
            typed_message.pack_file_chunk =
                std::make_shared<PackFileChunkMessage>(std::move(*decoded));
            return typed_message;
        }

        case kPackTransferDoneMessageType:
        {
            const auto decoded =
                deserialize_pack_transfer_done_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::PackTransferDone;
            typed_message.pack_transfer_done =
                std::make_shared<PackTransferDoneMessage>(std::move(*decoded));
            return typed_message;
        }

        case kStagedMatchSetupMessageType:
        {
            const auto decoded =
                deserialize_staged_match_setup_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::StagedMatchSetup;
            typed_message.staged_match_setup =
                std::make_shared<StagedMatchSetupMessage>(std::move(*decoded));
            return typed_message;
        }

        case kStagedMatchKeyframeMessageType:
        {
            const auto decoded =
                deserialize_staged_match_keyframe_message(message.data);
            if (!decoded.has_value())
                return malformed_typed_message(message.peer_id);

            typed_message.kind = TypedReceivedMessageKind::StagedMatchKeyframe;
            typed_message.staged_match_keyframe =
                std::make_shared<StagedMatchKeyframeMessage>(
                    std::move(*decoded));
            return typed_message;
        }

        default:
            return malformed_typed_message(message.peer_id);
        }
    }
    catch (const std::exception&)
    {
        return malformed_typed_message(message.peer_id);
    }
}

std::vector<TypedReceivedMessage> ITransport::poll_typed()
{
    std::vector<TypedReceivedMessage> typed_messages;
    for (const auto& message : poll())
        typed_messages.push_back(decode_received_message(message));
    return typed_messages;
}

} // namespace og::sim
