#include <openglad/gameplay/world_snapshot.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/core/zlib_api.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace
{

using EntityStorage = GameWorld::EntityList::Storage;
using EntitySnapshotLookup =
    std::unordered_map<std::uint32_t, const og::sim::EntitySnapshot*>;
using GuyStorage = std::unordered_map<std::int32_t, std::unique_ptr<guy>>;
using GuyLookup = std::unordered_map<std::int32_t, guy*>;
using EntityDirtyMask = og::sim::EntitySnapshotDirtyMask;

constexpr std::size_t kMessageHeaderSize = og::sim::kTransportHeaderSize;
constexpr std::size_t kDeltaWirePayloadHeaderSize =
    og::sim::kDeltaPayloadHeaderSize;
constexpr std::size_t kZlibChunkSize = 4096;
constexpr std::size_t kMaxDecompressedPayloadBytes = 4U * 1024U * 1024U;
constexpr std::uint32_t kMaxSnapshotStringBytes = 1024;
constexpr std::uint32_t kMaxGridCellCount = 255U * 255U;
constexpr std::uint32_t kMaxGridDirtyTileCount = kMaxGridCellCount;
constexpr std::uint32_t kMaxGuySnapshotCount = 4096;
constexpr std::uint32_t kMaxEntitySnapshotCount = 16384;
constexpr std::uint32_t kMaxRemovedEntityCount = 16384;
constexpr std::uint8_t kDeltaPayloadSupportedFlags =
    og::sim::kDeltaPayloadUncompressedFlag;
constexpr std::uint8_t kDeltaEntityHasDirtyWord1Flag = 0x01;
constexpr EntityDirtyMask kAllEntityFieldsDirty = {~0ULL, ~0ULL};

bool entity_delta_is_removal(const og::sim::EntitySnapshot& snapshot) noexcept
{
    return snapshot.dirty_mask[0] == 0 && snapshot.dirty_mask[1] == 0;
}

class ByteReader
{
public:
    ByteReader(const std::uint8_t* data,
               std::size_t size,
               const char* source_name)
        : cursor_(data)
        , end_(data + size)
        , source_name_(source_name)
    {
    }

    std::size_t remaining() const noexcept
    {
        return static_cast<std::size_t>(end_ - cursor_);
    }

    std::uint8_t read_u8(const char* field_name)
    {
        require(1, field_name);
        return *cursor_++;
    }

    bool read_bool(const char* field_name)
    {
        return read_u8(field_name) != 0;
    }

    std::uint16_t read_u16(const char* field_name)
    {
        require(2, field_name);
        const std::uint16_t value =
            static_cast<std::uint16_t>(cursor_[0]) |
            (static_cast<std::uint16_t>(cursor_[1]) << 8);
        cursor_ += 2;
        return value;
    }

    std::int16_t read_i16(const char* field_name)
    {
        return static_cast<std::int16_t>(read_u16(field_name));
    }

    std::uint32_t read_u32(const char* field_name)
    {
        require(4, field_name);
        const std::uint32_t value =
            static_cast<std::uint32_t>(cursor_[0]) |
            (static_cast<std::uint32_t>(cursor_[1]) << 8) |
            (static_cast<std::uint32_t>(cursor_[2]) << 16) |
            (static_cast<std::uint32_t>(cursor_[3]) << 24);
        cursor_ += 4;
        return value;
    }

    std::int32_t read_i32(const char* field_name)
    {
        return static_cast<std::int32_t>(read_u32(field_name));
    }

    std::uint64_t read_u64(const char* field_name)
    {
        require(8, field_name);
        const std::uint64_t value =
            static_cast<std::uint64_t>(cursor_[0]) |
            (static_cast<std::uint64_t>(cursor_[1]) << 8) |
            (static_cast<std::uint64_t>(cursor_[2]) << 16) |
            (static_cast<std::uint64_t>(cursor_[3]) << 24) |
            (static_cast<std::uint64_t>(cursor_[4]) << 32) |
            (static_cast<std::uint64_t>(cursor_[5]) << 40) |
            (static_cast<std::uint64_t>(cursor_[6]) << 48) |
            (static_cast<std::uint64_t>(cursor_[7]) << 56);
        cursor_ += 8;
        return value;
    }

    float read_f32(const char* field_name)
    {
        const std::uint32_t bits = read_u32(field_name);
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::string read_string(const char* field_name)
    {
        const std::uint32_t length = read_u32(field_name);
        if (length > kMaxSnapshotStringBytes)
        {
            throw std::runtime_error(std::string(source_name_) +
                                     ": " + field_name +
                                     " exceeds maximum string length");
        }
        require(length, field_name);
        std::string value;
        value.resize(length);
        if (length > 0)
        {
            std::memcpy(value.data(), cursor_, length);
            cursor_ += length;
        }
        return value;
    }

    std::vector<std::uint8_t> read_bytes(std::size_t count, const char* field_name)
    {
        require(count, field_name);
        std::vector<std::uint8_t> value(count);
        if (count > 0)
        {
            std::memcpy(value.data(), cursor_, count);
            cursor_ += count;
        }
        return value;
    }

    void finish(const char* context) const
    {
        if (cursor_ != end_)
        {
            throw std::runtime_error(std::string(context) +
                                     ": trailing bytes remain in " +
                                     source_name_);
        }
    }

private:
    void require(std::size_t count, const char* field_name)
    {
        if (remaining() < count)
        {
            throw std::runtime_error(std::string(source_name_) +
                                     ": truncated while reading " +
                                     field_name);
        }
    }

    const std::uint8_t* cursor_ = nullptr;
    const std::uint8_t* end_ = nullptr;
    const char* source_name_ = nullptr;
};

std::uint32_t read_bounded_count(ByteReader& reader,
                                 const char* field_name,
                                 std::uint32_t max_count,
                                 const char* context)
{
    const std::uint32_t count = reader.read_u32(field_name);
    if (count > max_count)
    {
        throw std::runtime_error(std::string(context) + " exceeds maximum count");
    }
    return count;
}

std::size_t grid_cell_count(std::uint8_t width, std::uint8_t height) noexcept
{
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

bool has_materialized_grid(const og::sim::WorldSnapshot& snapshot) noexcept
{
    if (snapshot.grid_width == 0 || snapshot.grid_height == 0)
        return false;

    return snapshot.full_grid_data.size() ==
           grid_cell_count(snapshot.grid_width, snapshot.grid_height);
}

void normalize_materialized_grid(og::sim::WorldSnapshot& snapshot)
{
    if (!has_materialized_grid(snapshot))
    {
        snapshot.grid_dirty = false;
        snapshot.grid_full_resend = false;
        snapshot.full_grid_data.clear();
        snapshot.grid_dirty_tiles.clear();
        return;
    }

    snapshot.grid_dirty = true;
    snapshot.grid_full_resend = true;
    snapshot.grid_dirty_tiles.clear();
}

void append_u8(std::vector<std::uint8_t>& buffer, std::uint8_t value)
{
    buffer.push_back(value);
}

void append_bool(std::vector<std::uint8_t>& buffer, bool value)
{
    append_u8(buffer, value ? 1U : 0U);
}

void append_u16(std::vector<std::uint8_t>& buffer, std::uint16_t value)
{
    buffer.push_back(static_cast<std::uint8_t>(value & 0xffu));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void append_i16(std::vector<std::uint8_t>& buffer, std::int16_t value)
{
    append_u16(buffer, static_cast<std::uint16_t>(value));
}

void append_u32(std::vector<std::uint8_t>& buffer, std::uint32_t value)
{
    buffer.push_back(static_cast<std::uint8_t>(value & 0xffu));
    buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    buffer.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    buffer.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void append_i32(std::vector<std::uint8_t>& buffer, std::int32_t value)
{
    append_u32(buffer, static_cast<std::uint32_t>(value));
}

void append_u64(std::vector<std::uint8_t>& buffer, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        buffer.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu));
}

void append_f32(std::vector<std::uint8_t>& buffer, float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(buffer, bits);
}

void append_bytes(std::vector<std::uint8_t>& buffer,
                  const std::uint8_t* data,
                  std::size_t size)
{
    buffer.insert(buffer.end(), data, data + size);
}

void append_string(std::vector<std::uint8_t>& buffer, const std::string& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("snapshot serialization: string field exceeds 32-bit length");
    }

    append_u32(buffer, static_cast<std::uint32_t>(value.size()));
    append_bytes(buffer,
                 reinterpret_cast<const std::uint8_t*>(value.data()),
                 value.size());
}

bool mask_has_bit(const std::uint64_t* mask, std::uint8_t bit_index) noexcept
{
    return (mask[bit_index / 64] & (1ULL << (bit_index % 64))) != 0;
}

const og::sim::EntitySnapshotFieldDesc* find_field_desc(std::uint8_t bit_index)
{
    const auto it = std::find_if(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [bit_index](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.bit_index == bit_index;
        });
    return it == std::end(og::sim::kEntitySnapshotFields) ? nullptr : &*it;
}

void append_raw_trivial_field(std::vector<std::uint8_t>& buffer,
                              const void* src,
                              std::size_t size)
{
    switch (size)
    {
    case 1:
        append_u8(buffer, *static_cast<const std::uint8_t*>(src));
        return;
    case 2:
    {
        std::uint16_t value = 0;
        std::memcpy(&value, src, sizeof(value));
        append_u16(buffer, value);
        return;
    }
    case 4:
    {
        std::uint32_t value = 0;
        std::memcpy(&value, src, sizeof(value));
        append_u32(buffer, value);
        return;
    }
    case 8:
    {
        std::uint64_t value = 0;
        std::memcpy(&value, src, sizeof(value));
        append_u64(buffer, value);
        return;
    }
    default:
        throw std::runtime_error("snapshot serialization: unsupported field size");
    }
}

void read_raw_trivial_field(ByteReader& reader,
                            void* dst,
                            std::size_t size,
                            const char* field_name)
{
    switch (size)
    {
    case 1:
    {
        const std::uint8_t value = reader.read_u8(field_name);
        std::memcpy(dst, &value, sizeof(value));
        return;
    }
    case 2:
    {
        const std::uint16_t value = reader.read_u16(field_name);
        std::memcpy(dst, &value, sizeof(value));
        return;
    }
    case 4:
    {
        const std::uint32_t value = reader.read_u32(field_name);
        std::memcpy(dst, &value, sizeof(value));
        return;
    }
    case 8:
    {
        const std::uint64_t value = reader.read_u64(field_name);
        std::memcpy(dst, &value, sizeof(value));
        return;
    }
    default:
        throw std::runtime_error("snapshot deserialization: unsupported field size");
    }
}

void serialize_entity_field(std::vector<std::uint8_t>& buffer,
                            const og::sim::EntitySnapshot& snapshot,
                            std::uint8_t bit_index)
{
    switch (bit_index)
    {
    case og::dirty::BIT_ENTITY_ID:
        return;
    case og::dirty::BIT_REGEN_DELAY:
        append_i32(buffer, snapshot.regen_delay);
        return;
    case og::dirty::BIT_SPECIAL_COST:
        for (int i = 0; i < NUM_SPECIALS; ++i)
            append_u16(buffer, snapshot.special_cost[i]);
        return;
    case og::dirty::BIT_DO_BOUNCE:
        append_i32(buffer, snapshot.do_bounce);
        return;
    default:
        break;
    }

    const og::sim::EntitySnapshotFieldDesc* const field = find_field_desc(bit_index);
    if (field == nullptr)
    {
        throw std::runtime_error("snapshot serialization: unknown entity field bit");
    }

    const std::uint8_t* const src =
        reinterpret_cast<const std::uint8_t*>(&snapshot) + field->snap_offset;
    append_raw_trivial_field(buffer, src, field->size);
}

void deserialize_entity_field(ByteReader& reader,
                              og::sim::EntitySnapshot& snapshot,
                              std::uint8_t bit_index)
{
    switch (bit_index)
    {
    case og::dirty::BIT_ENTITY_ID:
        return;
    case og::dirty::BIT_REGEN_DELAY:
        snapshot.regen_delay = reader.read_i32("entity.regen_delay");
        return;
    case og::dirty::BIT_SPECIAL_COST:
        for (int i = 0; i < NUM_SPECIALS; ++i)
            snapshot.special_cost[i] = reader.read_u16("entity.special_cost");
        return;
    case og::dirty::BIT_DO_BOUNCE:
        snapshot.do_bounce = reader.read_i32("entity.do_bounce");
        return;
    default:
        break;
    }

    const og::sim::EntitySnapshotFieldDesc* const field = find_field_desc(bit_index);
    if (field == nullptr)
    {
        throw std::runtime_error("snapshot deserialization: unknown entity field bit");
    }

    std::uint8_t* const dst =
        reinterpret_cast<std::uint8_t*>(&snapshot) + field->snap_offset;
    read_raw_trivial_field(reader, dst, field->size, "entity.field");
}

void serialize_entity_fields(std::vector<std::uint8_t>& buffer,
                             const og::sim::EntitySnapshot& snapshot,
                             const std::uint64_t* dirty_mask)
{
    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit)
    {
        if (!mask_has_bit(dirty_mask, bit))
            continue;
        serialize_entity_field(buffer, snapshot, bit);
    }
}

void deserialize_entity_fields(ByteReader& reader,
                               og::sim::EntitySnapshot& snapshot,
                               const std::uint64_t* dirty_mask)
{
    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit)
    {
        if (!mask_has_bit(dirty_mask, bit))
            continue;
        deserialize_entity_field(reader, snapshot, bit);
    }
}

void copy_entity_field(og::sim::EntitySnapshot& dst,
                       const og::sim::EntitySnapshot& src,
                       std::uint8_t bit_index)
{
    switch (bit_index)
    {
    case og::dirty::BIT_ENTITY_ID:
        dst.entity_id = src.entity_id;
        return;
    case og::dirty::BIT_REGEN_DELAY:
        dst.regen_delay = src.regen_delay;
        return;
    case og::dirty::BIT_DO_BOUNCE:
        dst.do_bounce = src.do_bounce;
        return;
    default:
        break;
    }

    const og::sim::EntitySnapshotFieldDesc* const field = find_field_desc(bit_index);
    if (field == nullptr)
    {
        throw std::runtime_error("apply_delta: unknown entity field bit");
    }

    std::memcpy(reinterpret_cast<std::uint8_t*>(&dst) + field->snap_offset,
                reinterpret_cast<const std::uint8_t*>(&src) + field->snap_offset,
                field->size);
}

void apply_entity_delta_fields(og::sim::EntitySnapshot& baseline,
                               const og::sim::EntitySnapshot& delta)
{
    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit)
    {
        if (!mask_has_bit(delta.dirty_mask, bit))
            continue;
        copy_entity_field(baseline, delta, bit);
    }
}

void serialize_guy_snapshot(std::vector<std::uint8_t>& buffer,
                            const og::sim::GuySnapshot& snapshot)
{
    append_i32(buffer, snapshot.guy_id);
    append_string(buffer, snapshot.name);
    append_u8(buffer, static_cast<std::uint8_t>(snapshot.family));
    append_i16(buffer, snapshot.strength);
    append_i16(buffer, snapshot.dexterity);
    append_i16(buffer, snapshot.constitution);
    append_i16(buffer, snapshot.intelligence);
    append_i16(buffer, snapshot.armor);
    append_u32(buffer, snapshot.exp);
    append_i16(buffer, snapshot.kills);
    append_i32(buffer, snapshot.level_kills);
    append_i32(buffer, snapshot.total_damage);
    append_i32(buffer, snapshot.total_hits);
    append_i32(buffer, snapshot.total_shots);
    append_i16(buffer, snapshot.teamnum);
    append_f32(buffer, snapshot.scen_damage);
    append_i16(buffer, snapshot.scen_kills);
    append_f32(buffer, snapshot.scen_damage_taken);
    append_f32(buffer, snapshot.scen_min_hp);
    append_i16(buffer, snapshot.scen_shots);
    append_i16(buffer, snapshot.scen_hits);
    append_i16(buffer, snapshot.level);
    append_u8(buffer, snapshot.owner_player_index);
    append_u8(buffer, snapshot.owner_save_slot);
}

og::sim::GuySnapshot deserialize_guy_snapshot(ByteReader& reader)
{
    og::sim::GuySnapshot snapshot;
    snapshot.guy_id = reader.read_i32("guy.guy_id");
    snapshot.name = reader.read_string("guy.name");
    snapshot.family = static_cast<std::int8_t>(reader.read_u8("guy.family"));
    snapshot.strength = reader.read_i16("guy.strength");
    snapshot.dexterity = reader.read_i16("guy.dexterity");
    snapshot.constitution = reader.read_i16("guy.constitution");
    snapshot.intelligence = reader.read_i16("guy.intelligence");
    snapshot.armor = reader.read_i16("guy.armor");
    snapshot.exp = reader.read_u32("guy.exp");
    snapshot.kills = reader.read_i16("guy.kills");
    snapshot.level_kills = reader.read_i32("guy.level_kills");
    snapshot.total_damage = reader.read_i32("guy.total_damage");
    snapshot.total_hits = reader.read_i32("guy.total_hits");
    snapshot.total_shots = reader.read_i32("guy.total_shots");
    snapshot.teamnum = reader.read_i16("guy.teamnum");
    snapshot.scen_damage = reader.read_f32("guy.scen_damage");
    snapshot.scen_kills = reader.read_i16("guy.scen_kills");
    snapshot.scen_damage_taken = reader.read_f32("guy.scen_damage_taken");
    snapshot.scen_min_hp = reader.read_f32("guy.scen_min_hp");
    snapshot.scen_shots = reader.read_i16("guy.scen_shots");
    snapshot.scen_hits = reader.read_i16("guy.scen_hits");
    snapshot.level = reader.read_i16("guy.level");
    snapshot.owner_player_index = reader.read_u8("guy.owner_player_index");
    snapshot.owner_save_slot = reader.read_u8("guy.owner_save_slot");
    return snapshot;
}

void serialize_ctf_state(std::vector<std::uint8_t>& buffer,
                         const og::sim::WorldSnapshot& snapshot)
{
    append_bool(buffer, snapshot.ctf_active);
    append_bool(buffer, snapshot.ctf_init_attempted);
    append_u8(buffer, snapshot.ctf_team_count);
    append_u8(buffer, snapshot.ctf_capture_limit);
    append_u16(buffer, snapshot.ctf_respawn_ticks);
    append_u16(buffer, snapshot.ctf_flag_return_ticks);
    append_u32(buffer, snapshot.ctf_time_limit_ticks);
    append_u8(buffer, static_cast<std::uint8_t>(snapshot.ctf_winner_team));
    append_bool(buffer, snapshot.ctf_winner_is_player);
    for (std::uint16_t captures : snapshot.ctf_captures)
        append_u16(buffer, captures);
    append_u16(buffer, snapshot.ctf_respawn_serial);
    for (bool team_active : snapshot.ctf_team_active)
        append_bool(buffer, team_active);

    for (const og::sim::CtfFlag& flag : snapshot.ctf_flags)
    {
        append_u8(buffer, static_cast<std::uint8_t>(flag.state));
        append_u32(buffer, flag.carrier_entity_id);
        append_i16(buffer, flag.x);
        append_i16(buffer, flag.y);
        append_i16(buffer, flag.home_x);
        append_i16(buffer, flag.home_y);
        append_u16(buffer, flag.return_ticks);
        append_u32(buffer, flag.flag_entity_id);
        append_bool(buffer, flag.present);
    }

    if (snapshot.ctf_cp_count > og::sim::kCtfMaxControlPoints)
    {
        throw std::runtime_error(
            "snapshot serialization: ctf control point count exceeds maximum");
    }
    append_u8(buffer, snapshot.ctf_cp_count);
    for (int i = 0; i < snapshot.ctf_cp_count; ++i)
    {
        const og::sim::CtfControlPoint& cp = snapshot.ctf_cps[i];
        append_u8(buffer, static_cast<std::uint8_t>(cp.owner));
        append_i16(buffer, cp.progress);
        append_u8(buffer, static_cast<std::uint8_t>(cp.progress_team));
        append_i16(buffer, cp.x);
        append_i16(buffer, cp.y);
        append_u8(buffer, cp.radius_tiles);
        append_u32(buffer, cp.entity_id);
        append_u32(buffer, cp.next_pulse_tick);
    }

    for (int team = 0; team < 4; ++team)
    {
        if (snapshot.ctf_anchor_count[team] > og::sim::kCtfMaxAnchorsPerTeam)
        {
            throw std::runtime_error(
                "snapshot serialization: ctf anchor count exceeds maximum");
        }
        append_u8(buffer, snapshot.ctf_anchor_count[team]);
        for (int i = 0; i < snapshot.ctf_anchor_count[team]; ++i)
        {
            append_i16(buffer, snapshot.ctf_anchor_x[team][i]);
            append_i16(buffer, snapshot.ctf_anchor_y[team][i]);
        }
    }

    if (snapshot.ctf_respawn_queue.size() >
        static_cast<std::size_t>(og::sim::kCtfMaxRespawnEntries))
    {
        throw std::runtime_error(
            "snapshot serialization: ctf respawn queue exceeds maximum");
    }
    append_u8(buffer, static_cast<std::uint8_t>(snapshot.ctf_respawn_queue.size()));
    for (const og::sim::CtfRespawnEntry& entry : snapshot.ctf_respawn_queue)
    {
        append_u8(buffer, entry.kind);
        append_u8(buffer, entry.team);
        append_u8(buffer, entry.family);
        append_u8(buffer, entry.level);
        append_u16(buffer, entry.ticks_left);
        append_u32(buffer, entry.walker_entity_id);
    }

    append_i16(buffer, snapshot.ctf_requested_team_count);
    append_i16(buffer, snapshot.ctf_requested_capture_limit);
    append_i16(buffer, snapshot.ctf_requested_respawn_ticks);
    append_i16(buffer, snapshot.ctf_requested_strip_scenario_troops);
}

void deserialize_ctf_state(ByteReader& reader, og::sim::WorldSnapshot& snapshot)
{
    snapshot.ctf_active = reader.read_bool("world.ctf_active");
    snapshot.ctf_init_attempted = reader.read_bool("world.ctf_init_attempted");
    snapshot.ctf_team_count = reader.read_u8("world.ctf_team_count");
    snapshot.ctf_capture_limit = reader.read_u8("world.ctf_capture_limit");
    snapshot.ctf_respawn_ticks = reader.read_u16("world.ctf_respawn_ticks");
    snapshot.ctf_flag_return_ticks = reader.read_u16("world.ctf_flag_return_ticks");
    snapshot.ctf_time_limit_ticks = reader.read_u32("world.ctf_time_limit_ticks");
    snapshot.ctf_winner_team =
        static_cast<std::int8_t>(reader.read_u8("world.ctf_winner_team"));
    snapshot.ctf_winner_is_player = reader.read_bool("world.ctf_winner_is_player");
    for (std::uint16_t& captures : snapshot.ctf_captures)
        captures = reader.read_u16("world.ctf_captures");
    snapshot.ctf_respawn_serial = reader.read_u16("world.ctf_respawn_serial");
    for (bool& team_active : snapshot.ctf_team_active)
        team_active = reader.read_bool("world.ctf_team_active");

    for (og::sim::CtfFlag& flag : snapshot.ctf_flags)
    {
        flag.state =
            static_cast<og::sim::CtfFlagState>(reader.read_u8("ctf_flag.state"));
        flag.carrier_entity_id = reader.read_u32("ctf_flag.carrier_entity_id");
        flag.x = reader.read_i16("ctf_flag.x");
        flag.y = reader.read_i16("ctf_flag.y");
        flag.home_x = reader.read_i16("ctf_flag.home_x");
        flag.home_y = reader.read_i16("ctf_flag.home_y");
        flag.return_ticks = reader.read_u16("ctf_flag.return_ticks");
        flag.flag_entity_id = reader.read_u32("ctf_flag.flag_entity_id");
        flag.present = reader.read_bool("ctf_flag.present");
    }

    snapshot.ctf_cp_count = reader.read_u8("world.ctf_cp_count");
    if (snapshot.ctf_cp_count > og::sim::kCtfMaxControlPoints)
    {
        throw std::runtime_error("ctf control point count exceeds maximum count");
    }
    for (int i = 0; i < snapshot.ctf_cp_count; ++i)
    {
        og::sim::CtfControlPoint& cp = snapshot.ctf_cps[i];
        cp.owner = static_cast<std::int8_t>(reader.read_u8("ctf_cp.owner"));
        cp.progress = reader.read_i16("ctf_cp.progress");
        cp.progress_team =
            static_cast<std::int8_t>(reader.read_u8("ctf_cp.progress_team"));
        cp.x = reader.read_i16("ctf_cp.x");
        cp.y = reader.read_i16("ctf_cp.y");
        cp.radius_tiles = reader.read_u8("ctf_cp.radius_tiles");
        cp.entity_id = reader.read_u32("ctf_cp.entity_id");
        cp.next_pulse_tick = reader.read_u32("ctf_cp.next_pulse_tick");
    }

    for (int team = 0; team < 4; ++team)
    {
        snapshot.ctf_anchor_count[team] = reader.read_u8("world.ctf_anchor_count");
        if (snapshot.ctf_anchor_count[team] > og::sim::kCtfMaxAnchorsPerTeam)
        {
            throw std::runtime_error("ctf anchor count exceeds maximum count");
        }
        for (int i = 0; i < snapshot.ctf_anchor_count[team]; ++i)
        {
            snapshot.ctf_anchor_x[team][i] = reader.read_i16("ctf_anchor.x");
            snapshot.ctf_anchor_y[team][i] = reader.read_i16("ctf_anchor.y");
        }
    }

    const std::uint8_t queue_size = reader.read_u8("world.ctf_respawn_queue_size");
    if (queue_size > og::sim::kCtfMaxRespawnEntries)
    {
        throw std::runtime_error("ctf respawn queue exceeds maximum count");
    }
    snapshot.ctf_respawn_queue.clear();
    snapshot.ctf_respawn_queue.reserve(queue_size);
    for (std::uint8_t i = 0; i < queue_size; ++i)
    {
        og::sim::CtfRespawnEntry entry;
        entry.kind = reader.read_u8("ctf_respawn.kind");
        entry.team = reader.read_u8("ctf_respawn.team");
        entry.family = reader.read_u8("ctf_respawn.family");
        entry.level = reader.read_u8("ctf_respawn.level");
        entry.ticks_left = reader.read_u16("ctf_respawn.ticks_left");
        entry.walker_entity_id = reader.read_u32("ctf_respawn.walker_entity_id");
        snapshot.ctf_respawn_queue.push_back(entry);
    }

    snapshot.ctf_requested_team_count =
        reader.read_i16("world.ctf_requested_team_count");
    snapshot.ctf_requested_capture_limit =
        reader.read_i16("world.ctf_requested_capture_limit");
    snapshot.ctf_requested_respawn_ticks =
        reader.read_i16("world.ctf_requested_respawn_ticks");
    snapshot.ctf_requested_strip_scenario_troops =
        reader.read_i16("world.ctf_requested_strip_scenario_troops");
}

void serialize_world_state(std::vector<std::uint8_t>& buffer,
                           const og::sim::WorldSnapshot& snapshot)
{
    append_u32(buffer, snapshot.tick_count);
    append_u32(buffer, snapshot.rng_state);
    append_u32(buffer, snapshot.level_tick_count);
    append_i16(buffer, snapshot.level_done);
    append_bool(buffer, snapshot.game_ended);
    append_u8(buffer, static_cast<std::uint8_t>(snapshot.end));
    append_bool(buffer, snapshot.retry);
    append_i16(buffer, snapshot.next_level);
    append_i16(buffer, snapshot.ending);
    append_i32(buffer, snapshot.enemy_freeze);
    append_u8(buffer, static_cast<std::uint8_t>(snapshot.timer_wait));
    append_i32(buffer, snapshot.living_count);
    append_f32(buffer, snapshot.control_hp);
    append_i16(buffer, snapshot.my_team);
    append_i16(buffer, snapshot.allied_mode);
    append_i16(buffer, snapshot.difficulty);
    append_bool(buffer, snapshot.withdraw_requested);
    append_i16(buffer, snapshot.withdraw_level);
    append_i32(buffer, snapshot.guy_id_counter);
    for (std::uint32_t score : snapshot.m_score)
        append_u32(buffer, score);
    append_u8(buffer, snapshot.current_palette_id);
    append_bool(buffer, snapshot.pending_exit_prompt);
    append_bool(buffer, snapshot.paused);
    append_u8(buffer, snapshot.pause_player_index);
    append_u32(buffer, snapshot.snapshot_hash);
    serialize_ctf_state(buffer, snapshot);
}

void deserialize_world_state(ByteReader& reader, og::sim::WorldSnapshot& snapshot)
{
    snapshot.tick_count = reader.read_u32("world.tick_count");
    snapshot.rng_state = reader.read_u32("world.rng_state");
    snapshot.level_tick_count = reader.read_u32("world.level_tick_count");
    snapshot.level_done = reader.read_i16("world.level_done");
    snapshot.game_ended = reader.read_bool("world.game_ended");
    snapshot.end = static_cast<std::int8_t>(reader.read_u8("world.end"));
    snapshot.retry = reader.read_bool("world.retry");
    snapshot.next_level = reader.read_i16("world.next_level");
    snapshot.ending = reader.read_i16("world.ending");
    snapshot.enemy_freeze = reader.read_i32("world.enemy_freeze");
    snapshot.timer_wait = static_cast<std::int8_t>(reader.read_u8("world.timer_wait"));
    snapshot.living_count = reader.read_i32("world.living_count");
    snapshot.control_hp = reader.read_f32("world.control_hp");
    snapshot.my_team = reader.read_i16("world.my_team");
    snapshot.allied_mode = reader.read_i16("world.allied_mode");
    snapshot.difficulty = reader.read_i16("world.difficulty");
    snapshot.withdraw_requested = reader.read_bool("world.withdraw_requested");
    snapshot.withdraw_level = reader.read_i16("world.withdraw_level");
    snapshot.guy_id_counter = reader.read_i32("world.guy_id_counter");
    for (std::uint32_t& score : snapshot.m_score)
        score = reader.read_u32("world.m_score");
    snapshot.current_palette_id = reader.read_u8("world.current_palette_id");
    snapshot.pending_exit_prompt = reader.read_bool("world.pending_exit_prompt");
    snapshot.paused = reader.read_bool("world.paused");
    snapshot.pause_player_index = reader.read_u8("world.pause_player_index");
    snapshot.snapshot_hash = reader.read_u32("world.snapshot_hash");
    deserialize_ctf_state(reader, snapshot);
}

void serialize_grid_state(std::vector<std::uint8_t>& buffer,
                          const og::sim::WorldSnapshot& snapshot)
{
    const std::size_t expected_grid_size =
        grid_cell_count(snapshot.grid_width, snapshot.grid_height);
    if (snapshot.grid_full_resend)
    {
        if (expected_grid_size == 0)
        {
            throw std::runtime_error(
                "snapshot serialization: full grid resend requires non-zero dimensions");
        }
        if (snapshot.full_grid_data.size() != expected_grid_size)
        {
            throw std::runtime_error(
                "snapshot serialization: full grid payload size does not match dimensions");
        }
    }
    else if (!snapshot.full_grid_data.empty())
    {
        throw std::runtime_error(
            "snapshot serialization: incremental grid update cannot carry full grid payload");
    }

    if (snapshot.grid_full_resend && !snapshot.grid_dirty)
    {
        throw std::runtime_error(
            "snapshot serialization: full grid resend requires grid_dirty");
    }
    if (!snapshot.grid_dirty && !snapshot.grid_dirty_tiles.empty())
    {
        throw std::runtime_error(
            "snapshot serialization: clean grid snapshot cannot carry dirty tiles");
    }

    append_u8(buffer, snapshot.grid_width);
    append_u8(buffer, snapshot.grid_height);
    append_bool(buffer, snapshot.grid_dirty);
    append_bool(buffer, snapshot.grid_full_resend);

    if (snapshot.full_grid_data.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("snapshot serialization: grid payload exceeds 32-bit length");
    }
    append_u32(buffer, static_cast<std::uint32_t>(snapshot.full_grid_data.size()));
    append_bytes(buffer, snapshot.full_grid_data.data(), snapshot.full_grid_data.size());

    if (snapshot.grid_dirty_tiles.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("snapshot serialization: grid dirty tile count exceeds 32-bit length");
    }
    append_u32(buffer, static_cast<std::uint32_t>(snapshot.grid_dirty_tiles.size()));
    for (const auto& tile : snapshot.grid_dirty_tiles)
    {
        if (tile.x < 0 || tile.y < 0 ||
            tile.x >= snapshot.grid_width || tile.y >= snapshot.grid_height)
        {
            throw std::runtime_error(
                "snapshot serialization: grid dirty tile is outside snapshot dimensions");
        }
        append_i16(buffer, tile.x);
        append_i16(buffer, tile.y);
        append_u8(buffer, tile.value);
    }
}

void deserialize_grid_state(ByteReader& reader, og::sim::WorldSnapshot& snapshot)
{
    snapshot.grid_width = reader.read_u8("grid.grid_width");
    snapshot.grid_height = reader.read_u8("grid.grid_height");
    snapshot.grid_dirty = reader.read_bool("grid.grid_dirty");
    snapshot.grid_full_resend = reader.read_bool("grid.grid_full_resend");
    const std::uint32_t full_grid_size =
        read_bounded_count(reader, "grid.full_grid_size",
                           kMaxGridCellCount, "grid full payload");
    snapshot.full_grid_data =
        reader.read_bytes(full_grid_size, "grid.full_grid_data");

    const std::uint32_t dirty_tile_count =
        read_bounded_count(reader, "grid.dirty_tile_count",
                           kMaxGridDirtyTileCount, "grid dirty tile count");

    const std::size_t expected_grid_size =
        grid_cell_count(snapshot.grid_width, snapshot.grid_height);
    if (snapshot.grid_full_resend)
    {
        if (expected_grid_size == 0)
        {
            throw std::runtime_error(
                "grid full resend requires non-zero dimensions");
        }
        if (full_grid_size != expected_grid_size)
        {
            throw std::runtime_error(
                "grid full payload size does not match grid dimensions");
        }
    }
    else if (full_grid_size != 0)
    {
        throw std::runtime_error(
            "incremental grid update cannot include full grid payload");
    }

    if (snapshot.grid_full_resend && !snapshot.grid_dirty)
    {
        throw std::runtime_error(
            "grid full resend requires grid_dirty flag");
    }
    if (!snapshot.grid_dirty && dirty_tile_count != 0)
    {
        throw std::runtime_error(
            "clean grid snapshot cannot include dirty tiles");
    }

    snapshot.grid_dirty_tiles.clear();
    snapshot.grid_dirty_tiles.reserve(dirty_tile_count);
    for (std::uint32_t i = 0; i < dirty_tile_count; ++i)
    {
        og::sim::GridTileSnapshot tile;
        tile.x = reader.read_i16("grid.tile.x");
        tile.y = reader.read_i16("grid.tile.y");
        tile.value = reader.read_u8("grid.tile.value");
        if (tile.x < 0 || tile.y < 0 ||
            tile.x >= snapshot.grid_width || tile.y >= snapshot.grid_height)
        {
            throw std::runtime_error(
                "grid dirty tile is outside snapshot dimensions");
        }
        snapshot.grid_dirty_tiles.push_back(tile);
    }
}

void serialize_guy_snapshots(std::vector<std::uint8_t>& buffer,
                             const std::vector<og::sim::GuySnapshot>& snapshots)
{
    if (snapshots.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("snapshot serialization: too many guy snapshots");
    }

    append_u32(buffer, static_cast<std::uint32_t>(snapshots.size()));
    for (const auto& snapshot : snapshots)
        serialize_guy_snapshot(buffer, snapshot);
}

void deserialize_guy_snapshots(ByteReader& reader,
                               std::vector<og::sim::GuySnapshot>& snapshots)
{
    const std::uint32_t count =
        read_bounded_count(reader, "guy_snapshot_count",
                           kMaxGuySnapshotCount, "guy snapshot count");
    snapshots.clear();
    snapshots.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        snapshots.push_back(deserialize_guy_snapshot(reader));
}

void serialize_keyframe_entity(std::vector<std::uint8_t>& buffer,
                               const og::sim::EntitySnapshot& snapshot)
{
    append_i32(buffer, snapshot.guy_id);
    append_u32(buffer, snapshot.entity_id);
    serialize_entity_fields(buffer, snapshot, kAllEntityFieldsDirty.data());
}

og::sim::EntitySnapshot deserialize_keyframe_entity(ByteReader& reader)
{
    og::sim::EntitySnapshot snapshot;
    snapshot.guy_id = reader.read_i32("entity.guy_id");
    snapshot.entity_id = reader.read_u32("entity.entity_id");
    snapshot.dirty_mask[0] = kAllEntityFieldsDirty[0];
    snapshot.dirty_mask[1] = kAllEntityFieldsDirty[1];
    deserialize_entity_fields(reader, snapshot, snapshot.dirty_mask);
    return snapshot;
}

void serialize_delta_entity(std::vector<std::uint8_t>& buffer,
                            const og::sim::EntitySnapshot& snapshot)
{
    append_i32(buffer, snapshot.guy_id);
    append_u32(buffer, snapshot.entity_id);

    std::uint8_t flags = 0;
    if (snapshot.dirty_mask[1] != 0)
        flags |= kDeltaEntityHasDirtyWord1Flag;
    append_u8(buffer, flags);
    append_u64(buffer, snapshot.dirty_mask[0]);
    if ((flags & kDeltaEntityHasDirtyWord1Flag) != 0)
        append_u64(buffer, snapshot.dirty_mask[1]);

    serialize_entity_fields(buffer, snapshot, snapshot.dirty_mask);
}

og::sim::EntitySnapshot deserialize_delta_entity(ByteReader& reader)
{
    og::sim::EntitySnapshot snapshot;
    snapshot.guy_id = reader.read_i32("entity.guy_id");
    snapshot.entity_id = reader.read_u32("entity.entity_id");
    const std::uint8_t flags = reader.read_u8("entity.flags");
    if ((flags & ~kDeltaEntityHasDirtyWord1Flag) != 0)
    {
        throw std::runtime_error("delta payload: unknown entity flags");
    }

    snapshot.dirty_mask[0] = reader.read_u64("entity.dirty_mask0");
    snapshot.dirty_mask[1] =
        ((flags & kDeltaEntityHasDirtyWord1Flag) != 0)
            ? reader.read_u64("entity.dirty_mask1")
            : 0ULL;
    deserialize_entity_fields(reader, snapshot, snapshot.dirty_mask);
    return snapshot;
}

void serialize_keyframe_entity_list(std::vector<std::uint8_t>& buffer,
                                    const std::vector<og::sim::EntitySnapshot>& entities)
{
    if (entities.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("snapshot serialization: too many entities");
    }

    append_u32(buffer, static_cast<std::uint32_t>(entities.size()));
    for (const auto& entity : entities)
        serialize_keyframe_entity(buffer, entity);
}

void deserialize_keyframe_entity_list(ByteReader& reader,
                                      std::vector<og::sim::EntitySnapshot>& entities)
{
    const std::uint32_t count =
        read_bounded_count(reader, "entity_count",
                           kMaxEntitySnapshotCount, "entity count");
    entities.clear();
    entities.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
        entities.push_back(deserialize_keyframe_entity(reader));
}

void serialize_delta_entity_list(std::vector<std::uint8_t>& buffer,
                                 const std::vector<og::sim::EntitySnapshot>& entities)
{
    if (entities.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("delta serialization: too many entities");
    }

    append_u32(buffer, static_cast<std::uint32_t>(entities.size()));
    for (const auto& entity : entities)
        serialize_delta_entity(buffer, entity);
}

void deserialize_delta_entity_list(ByteReader& reader,
                                   std::vector<og::sim::EntitySnapshot>& entities,
                                   std::vector<std::uint32_t>* removed_entity_ids = nullptr)
{
    const std::uint32_t count =
        read_bounded_count(reader, "delta_entity_count",
                           kMaxEntitySnapshotCount, "delta entity count");
    entities.clear();
    entities.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        og::sim::EntitySnapshot entity = deserialize_delta_entity(reader);
        if (removed_entity_ids != nullptr && entity_delta_is_removal(entity))
            removed_entity_ids->push_back(entity.entity_id);
        entities.push_back(std::move(entity));
    }
}

std::vector<std::uint8_t> compress_zlib_payload(const std::vector<std::uint8_t>& payload)
{
    if (payload.size() > std::numeric_limits<uLong>::max())
    {
        throw std::runtime_error("snapshot compression: payload exceeds zlib input size");
    }

    std::vector<std::uint8_t> compressed(compressBound(static_cast<uLong>(payload.size())));
    uLongf compressed_size = static_cast<uLongf>(compressed.size());
    const int rc = compress2(compressed.data(),
                             &compressed_size,
                             payload.data(),
                             static_cast<uLong>(payload.size()),
                             Z_DEFAULT_COMPRESSION);
    if (rc != Z_OK)
    {
        throw std::runtime_error("snapshot compression failed");
    }

    compressed.resize(static_cast<std::size_t>(compressed_size));
    return compressed;
}

std::vector<std::uint8_t> decompress_zlib_payload(const std::uint8_t* data,
                                                  std::size_t size)
{
    if (size > std::numeric_limits<uInt>::max())
    {
        throw std::runtime_error("snapshot decompression: payload exceeds zlib input size");
    }

    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
    stream.avail_in = static_cast<uInt>(size);

    const int init_rc = inflateInit(&stream);
    if (init_rc != Z_OK)
    {
        throw std::runtime_error("snapshot decompression setup failed");
    }

    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, kZlibChunkSize> chunk{};
    int rc = Z_OK;
    do
    {
        stream.next_out = chunk.data();
        stream.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END)
        {
            inflateEnd(&stream);
            throw std::runtime_error("snapshot decompression failed");
        }

        const std::size_t produced = chunk.size() - stream.avail_out;
        if (output.size() + produced > kMaxDecompressedPayloadBytes)
        {
            inflateEnd(&stream);
            throw std::runtime_error("snapshot decompression: payload exceeds maximum size");
        }
        output.insert(output.end(), chunk.begin(), chunk.begin() + produced);
    } while (rc != Z_STREAM_END);

    inflateEnd(&stream);
    return output;
}

std::vector<std::uint8_t> serialize_snapshot_payload(const og::sim::WorldSnapshot& snapshot)
{
    std::vector<std::uint8_t> payload;
    append_u8(payload, og::sim::kSnapshotFormatVersion);
    serialize_world_state(payload, snapshot);
    serialize_grid_state(payload, snapshot);
    serialize_guy_snapshots(payload, snapshot.guy_snapshots);
    serialize_keyframe_entity_list(payload, snapshot.oblist);
    serialize_keyframe_entity_list(payload, snapshot.fxlist);
    serialize_keyframe_entity_list(payload, snapshot.weaplist);

    if (snapshot.removed_entity_ids.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("snapshot serialization: too many removed entities");
    }
    append_u32(payload, static_cast<std::uint32_t>(snapshot.removed_entity_ids.size()));
    for (std::uint32_t entity_id : snapshot.removed_entity_ids)
        append_u32(payload, entity_id);

    return payload;
}

std::vector<std::uint8_t> serialize_snapshot_payload_for_hash(
    const og::sim::WorldSnapshot& snapshot,
    const GameWorld* world = nullptr)
{
    og::sim::WorldSnapshot normalized = snapshot;
    normalized.snapshot_hash = 0;
    normalized.removed_entity_ids.clear();
    if (!has_materialized_grid(normalized) && world != nullptr && world->grid.valid())
    {
        normalized.grid_width = world->grid.w;
        normalized.grid_height = world->grid.h;
        const std::size_t grid_size =
            static_cast<std::size_t>(world->grid.w) * world->grid.h;
        normalized.full_grid_data.assign(world->grid.data.get(),
                                         world->grid.data.get() + grid_size);
    }
    normalize_materialized_grid(normalized);
    return serialize_snapshot_payload(normalized);
}

std::uint32_t compute_snapshot_hash_impl(const og::sim::WorldSnapshot& snapshot,
                                         const GameWorld* world = nullptr)
{
    const std::vector<std::uint8_t> payload =
        serialize_snapshot_payload_for_hash(snapshot, world);
    if (payload.size() > std::numeric_limits<uInt>::max())
    {
        throw std::runtime_error("snapshot hash payload exceeds zlib input size");
    }

    return static_cast<std::uint32_t>(
        crc32(0UL,
              reinterpret_cast<const Bytef*>(payload.data()),
              static_cast<uInt>(payload.size())));
}

og::sim::WorldSnapshot deserialize_snapshot_payload(const std::vector<std::uint8_t>& payload)
{
    ByteReader reader(payload.data(), payload.size(), "snapshot payload");
    const std::uint8_t format_version = reader.read_u8("snapshot.format_version");
    if (format_version != og::sim::kSnapshotFormatVersion)
    {
        throw std::runtime_error(
            "server snapshot format v" + std::to_string(format_version) +
            ", client supports v" +
            std::to_string(og::sim::kSnapshotFormatVersion) +
            " -- please update");
    }

    og::sim::WorldSnapshot snapshot;
    deserialize_world_state(reader, snapshot);
    deserialize_grid_state(reader, snapshot);
    deserialize_guy_snapshots(reader, snapshot.guy_snapshots);
    deserialize_keyframe_entity_list(reader, snapshot.oblist);
    deserialize_keyframe_entity_list(reader, snapshot.fxlist);
    deserialize_keyframe_entity_list(reader, snapshot.weaplist);

    const std::uint32_t removed_count =
        read_bounded_count(reader, "snapshot.removed_entity_count",
                           kMaxRemovedEntityCount, "removed entity count");
    snapshot.removed_entity_ids.clear();
    snapshot.removed_entity_ids.reserve(removed_count);
    for (std::uint32_t i = 0; i < removed_count; ++i)
        snapshot.removed_entity_ids.push_back(reader.read_u32("snapshot.removed_entity_id"));

    reader.finish("snapshot payload");
    return snapshot;
}

std::vector<std::uint8_t> serialize_delta_payload(const og::sim::WorldSnapshot& delta)
{
    std::vector<std::uint8_t> payload;
    append_u8(payload, og::sim::kSnapshotFormatVersion);
    serialize_world_state(payload, delta);
    serialize_grid_state(payload, delta);
    serialize_guy_snapshots(payload, delta.guy_snapshots);
    serialize_delta_entity_list(payload, delta.oblist);
    serialize_delta_entity_list(payload, delta.fxlist);
    serialize_delta_entity_list(payload, delta.weaplist);
    return payload;
}

og::sim::WorldSnapshot deserialize_delta_payload(const std::vector<std::uint8_t>& payload)
{
    ByteReader reader(payload.data(), payload.size(), "delta payload");
    const std::uint8_t format_version = reader.read_u8("delta.format_version");
    if (format_version != og::sim::kSnapshotFormatVersion)
    {
        throw std::runtime_error(
            "server snapshot format v" + std::to_string(format_version) +
            ", client supports v" +
            std::to_string(og::sim::kSnapshotFormatVersion) +
            " -- please update");
    }

    og::sim::WorldSnapshot delta;
    deserialize_world_state(reader, delta);
    deserialize_grid_state(reader, delta);
    deserialize_guy_snapshots(reader, delta.guy_snapshots);
    delta.removed_entity_ids.clear();
    deserialize_delta_entity_list(reader, delta.oblist, &delta.removed_entity_ids);
    deserialize_delta_entity_list(reader, delta.fxlist, &delta.removed_entity_ids);
    deserialize_delta_entity_list(reader, delta.weaplist, &delta.removed_entity_ids);
    reader.finish("delta payload");
    return delta;
}

bool erase_entity_snapshot(std::vector<og::sim::EntitySnapshot>& entities,
                           std::uint32_t entity_id,
                           og::sim::EntitySnapshot* extracted = nullptr)
{
    const auto it = std::find_if(
        entities.begin(), entities.end(),
        [entity_id](const og::sim::EntitySnapshot& snapshot) {
            return snapshot.entity_id == entity_id;
        });
    if (it == entities.end())
        return false;

    if (extracted != nullptr)
        *extracted = *it;
    entities.erase(it);
    return true;
}

og::sim::EntitySnapshot* find_entity_snapshot_mutable(
    std::vector<og::sim::EntitySnapshot>& entities,
    std::uint32_t entity_id)
{
    const auto it = std::find_if(
        entities.begin(), entities.end(),
        [entity_id](const og::sim::EntitySnapshot& snapshot) {
            return snapshot.entity_id == entity_id;
        });
    return it == entities.end() ? nullptr : &*it;
}

void apply_delta_entity_list(std::vector<og::sim::EntitySnapshot>& target,
                             std::vector<og::sim::EntitySnapshot>& list_a,
                             std::vector<og::sim::EntitySnapshot>& list_b,
                             const std::vector<og::sim::EntitySnapshot>& delta_entities,
                             std::vector<std::uint32_t>& removed_entity_ids)
{
    for (const auto& delta_entity : delta_entities)
    {
        if (entity_delta_is_removal(delta_entity))
        {
            (void)erase_entity_snapshot(target, delta_entity.entity_id);
            (void)erase_entity_snapshot(list_a, delta_entity.entity_id);
            (void)erase_entity_snapshot(list_b, delta_entity.entity_id);
            if (std::find(removed_entity_ids.begin(), removed_entity_ids.end(),
                          delta_entity.entity_id) == removed_entity_ids.end())
            {
                removed_entity_ids.push_back(delta_entity.entity_id);
            }
            continue;
        }

        og::sim::EntitySnapshot* baseline_entity =
            find_entity_snapshot_mutable(target, delta_entity.entity_id);
        if (baseline_entity == nullptr)
        {
            og::sim::EntitySnapshot extracted;
            if (erase_entity_snapshot(list_a, delta_entity.entity_id, &extracted) ||
                erase_entity_snapshot(list_b, delta_entity.entity_id, &extracted))
            {
                target.push_back(extracted);
            }
            else
            {
                og::sim::EntitySnapshot created;
                created.entity_id = delta_entity.entity_id;
                target.push_back(created);
            }

            baseline_entity = &target.back();
        }

        baseline_entity->guy_id = delta_entity.guy_id;
        apply_entity_delta_fields(*baseline_entity, delta_entity);
        baseline_entity->dirty_mask[0] = 0;
        baseline_entity->dirty_mask[1] = 0;
    }
}

void reorder_entity_snapshots(
    std::vector<og::sim::EntitySnapshot>& entities,
    const std::vector<og::sim::EntitySnapshot>& ordered_entities)
{
    if (entities.size() <= 1)
        return;

    std::vector<og::sim::EntitySnapshot> reordered;
    reordered.reserve(entities.size());

    for (const auto& ordered : ordered_entities)
    {
        if (entity_delta_is_removal(ordered))
            continue;

        og::sim::EntitySnapshot extracted;
        if (erase_entity_snapshot(entities, ordered.entity_id, &extracted))
            reordered.push_back(std::move(extracted));
    }

    for (auto& entity : entities)
        reordered.push_back(std::move(entity));
    entities = std::move(reordered);
}

class ApplyingSnapshotGuard
{
public:
    explicit ApplyingSnapshotGuard(GameWorld& world)
        : world_(world)
        , prev_(world.applying_snapshot_)
    {
        world_.applying_snapshot_ = true;
    }

    ~ApplyingSnapshotGuard()
    {
        world_.applying_snapshot_ = prev_;
    }

    ApplyingSnapshotGuard(const ApplyingSnapshotGuard&) = delete;
    ApplyingSnapshotGuard& operator=(const ApplyingSnapshotGuard&) = delete;

private:
    GameWorld& world_;
    bool prev_ = false;
};

og::sim::GuySnapshot capture_guy_snapshot(const guy& source)
{
    og::sim::GuySnapshot snapshot;
    snapshot.guy_id = source.id;
    snapshot.name = source.name;
    snapshot.family = source.family;
    snapshot.strength = source.strength;
    snapshot.dexterity = source.dexterity;
    snapshot.constitution = source.constitution;
    snapshot.intelligence = source.intelligence;
    snapshot.armor = source.armor;
    snapshot.exp = source.exp;
    snapshot.kills = source.kills;
    snapshot.level_kills = source.level_kills;
    snapshot.total_damage = source.total_damage;
    snapshot.total_hits = source.total_hits;
    snapshot.total_shots = source.total_shots;
    snapshot.teamnum = source.teamnum;
    snapshot.scen_damage = source.scen_damage;
    snapshot.scen_kills = source.scen_kills;
    snapshot.scen_damage_taken = source.scen_damage_taken;
    snapshot.scen_min_hp = source.scen_min_hp;
    snapshot.scen_shots = source.scen_shots;
    snapshot.scen_hits = source.scen_hits;
    snapshot.level = source.level;
    snapshot.owner_player_index = source.owner_player_index;
    snapshot.owner_save_slot = source.owner_save_slot;
    return snapshot;
}

void apply_guy_snapshot(guy& target, const og::sim::GuySnapshot& snapshot)
{
    target.id = snapshot.guy_id;
    target.name = snapshot.name;
    target.family = snapshot.family;
    target.strength = snapshot.strength;
    target.dexterity = snapshot.dexterity;
    target.constitution = snapshot.constitution;
    target.intelligence = snapshot.intelligence;
    target.armor = snapshot.armor;
    target.exp = snapshot.exp;
    target.kills = snapshot.kills;
    target.level_kills = snapshot.level_kills;
    target.total_damage = snapshot.total_damage;
    target.total_hits = snapshot.total_hits;
    target.total_shots = snapshot.total_shots;
    target.teamnum = snapshot.teamnum;
    target.scen_damage = snapshot.scen_damage;
    target.scen_kills = snapshot.scen_kills;
    target.scen_damage_taken = snapshot.scen_damage_taken;
    target.scen_min_hp = snapshot.scen_min_hp;
    target.scen_shots = snapshot.scen_shots;
    target.scen_hits = snapshot.scen_hits;
    target.level = snapshot.level;
    target.owner_player_index = snapshot.owner_player_index;
    target.owner_save_slot = snapshot.owner_save_slot;
}

void build_entity_snapshot_lookup(
    const std::vector<og::sim::EntitySnapshot>& snapshots,
    EntitySnapshotLookup& lookup)
{
    lookup.clear();
    lookup.reserve(snapshots.size());
    for (const auto& snapshot : snapshots)
        lookup[snapshot.entity_id] = &snapshot;
}

template <typename EntityList>
void clear_entity_guy_links(EntityList& entities)
{
    for (const auto& entry : entities)
    {
        if (entry != nullptr)
            entry->clear_myguy();
    }
}

template <typename EntityList>
void remove_missing_entities(GameWorld& world,
                             EntityList& entities,
                             const EntitySnapshotLookup& snapshots)
{
    for (auto it = entities.begin(); it != entities.end();)
    {
        walker* const entity = it->get();
        if (entity == nullptr)
        {
            it = entities.erase(it);
            continue;
        }

        if (snapshots.find(entity->entity_id()) != snapshots.end())
        {
            ++it;
            continue;
        }

        if (world.myobmap != nullptr)
            world.myobmap->remove(entity);
        it = entities.erase(it);
    }
}

void apply_entity_snapshot_fields(GameWorld& world,
                                  walker& entity,
                                  const og::sim::EntitySnapshot& snapshot,
                                  bool reconfigure)
{
    if (world.myobmap != nullptr)
        world.myobmap->remove(&entity);

    if (reconfigure)
    {
        if (world.entity_configurator != nullptr)
        {
            const PixieData* data = world.configure_existing_entity(
                entity, snapshot.order, snapshot.family);
            if (data == nullptr)
            {
                LogError("apply_snapshot: failed to configure entity {} ({}, {})\n",
                         snapshot.entity_id, static_cast<int>(snapshot.order),
                         static_cast<int>(snapshot.family));
            }
        }
        world.set_entity_derived_stats(&entity, snapshot.order, snapshot.family);
    }

    entity.clear_myguy();
    entity.set_foe(nullptr);
    entity.set_leader(nullptr);
    entity.set_owner(nullptr);
    entity.set_collide_ob(nullptr);
    entity.path_to_foe.clear();
    entity.damage_numbers.clear();

    // Clamp the wire-supplied family to the valid range before it becomes the
    // entity's family(): family() indexes per-family tables (descriptors, special
    // names, graphics) across the sim, and a malicious peer can send any value.
    // Mirrors loader::set_walker's family clamp so family() stays consistent with
    // the entity's configured graphics/animation.
    int safe_family = static_cast<int>(snapshot.family);
    if (safe_family < 0 || safe_family >= NUM_FAMILIES)
        safe_family = 0;
    entity.set_order_family(snapshot.order, static_cast<char>(safe_family));
    entity.set_snapshot_position(snapshot.xpos, snapshot.ypos,
                                 snapshot.worldx, snapshot.worldy);
    entity.set_sizex(snapshot.sizex);
    entity.set_sizey(snapshot.sizey);
    entity.set_team_num(snapshot.team_num);
    entity.set_real_team_num(snapshot.real_team_num);
    entity.set_user(snapshot.user);
    entity.set_dead(snapshot.dead);
    entity.set_death_called(snapshot.death_called);
    entity.set_invulnerable_left(snapshot.invulnerable_left);
    entity.set_invisibility_left(snapshot.invisibility_left);
    entity.set_flight_left(snapshot.flight_left);
    entity.set_bonus_rounds(snapshot.bonus_rounds);
    entity.set_lastx(snapshot.lastx);
    entity.set_lasty(snapshot.lasty);
    entity.set_stepsize(snapshot.stepsize);
    entity.set_normal_stepsize(snapshot.normal_stepsize);
    entity.set_curdir(snapshot.curdir);
    entity.set_enddir(snapshot.enddir);
    entity.set_damage(snapshot.damage);
    entity.set_fire_frequency(snapshot.fire_frequency);
    entity.set_busy(snapshot.busy);
    entity.set_current_weapon(snapshot.current_weapon);
    entity.set_default_weapon(snapshot.default_weapon);
    entity.set_attack_lunge(snapshot.attack_lunge);
    entity.set_attack_lunge_angle(snapshot.attack_lunge_angle);
    entity.set_hit_recoil(snapshot.hit_recoil);
    entity.set_hit_recoil_angle(snapshot.hit_recoil_angle);
    entity.set_last_hitpoints(snapshot.last_hitpoints);
    entity.set_action(snapshot.action);
    entity.set_act_type_state(snapshot.act_type);
    entity.set_old_act_type(snapshot.old_act_type);
    entity.set_ani_type(snapshot.ani_type);
    entity.set_cycle(snapshot.cycle);
    entity.set_drawcycle(snapshot.drawcycle);
    // Clamp wire-supplied current_special: it indexes per-special arrays
    // (special names/costs, NUM_SPECIALS wide) at several use sites.
    int safe_current_special = static_cast<int>(snapshot.current_special);
    if (safe_current_special < 0 || safe_current_special >= NUM_SPECIALS)
        safe_current_special = 0;
    entity.set_current_special(static_cast<std::int8_t>(safe_current_special));
    entity.set_ignore(snapshot.ignore);
    entity.set_in_act(snapshot.in_act != 0);
    entity.set_shifter_down(snapshot.shifter_down);
    entity.set_yo_delay(snapshot.yo_delay);
    entity.set_skip_exit(snapshot.skip_exit);
    entity.set_outline(snapshot.outline);
    entity.set_hurt_flash(snapshot.hurt_flash != 0);
    entity.set_lifetime(snapshot.lifetime);
    entity.set_speed_bonus(snapshot.speed_bonus);
    entity.set_speed_bonus_left(snapshot.speed_bonus_left);
    entity.set_charm_left(snapshot.charm_left);
    entity.set_weapons_left(snapshot.weapons_left);
    entity.set_keys(snapshot.keys);
    entity.set_view_all(snapshot.view_all);
    entity.set_lineofsight(snapshot.lineofsight);
    entity.set_path_check_counter(snapshot.path_check_counter);
    entity.set_regen_delay(snapshot.regen_delay);
    entity.set_foe_id(snapshot.foe_id);
    entity.set_leader_id(snapshot.leader_id);
    entity.set_owner_id(snapshot.owner_id);
    entity.set_collide_ob_id(snapshot.collide_ob_id);

    if (statistics* const stats = entity.stats(); stats != nullptr)
    {
        stats->set_controller(nullptr);
        stats->set_hitpoints(snapshot.hitpoints);
        stats->set_max_hitpoints(snapshot.max_hitpoints);
        stats->set_magicpoints(snapshot.magicpoints);
        stats->set_max_magicpoints(snapshot.max_magicpoints);
        stats->set_max_heal_delay(snapshot.max_heal_delay);
        stats->set_current_heal_delay(snapshot.current_heal_delay);
        stats->set_max_magic_delay(snapshot.max_magic_delay);
        stats->set_current_magic_delay(snapshot.current_magic_delay);
        stats->set_magic_per_round(snapshot.magic_per_round);
        stats->set_heal_per_round(snapshot.heal_per_round);
        stats->set_armor(snapshot.armor);
        stats->set_level(snapshot.level);
        stats->set_bit_flags(snapshot.bit_flags);
        stats->set_delete_me(snapshot.delete_me);
        stats->set_frozen_delay(snapshot.frozen_delay);
        stats->set_weapon_cost(snapshot.weapon_cost);
        for (int i = 0; i < NUM_SPECIALS; ++i)
            stats->set_special_cost(i, snapshot.special_cost[i]);
        stats->set_old_order(snapshot.old_order);
        stats->set_old_family(snapshot.old_family);
        stats->set_last_distance(snapshot.last_distance);
        stats->set_current_distance(snapshot.current_distance);
        stats->set_controller_id(snapshot.controller_id);
        stats->commands.clear();
    }

    if (auto* weapon = dynamic_cast<weap*>(&entity); weapon != nullptr)
        weapon->set_do_bounce(snapshot.do_bounce);

    entity.set_direct_frame(snapshot.frame);
}

template <typename EntityList>
void update_existing_entities(GameWorld& world,
                              EntityList& entities,
                              const EntitySnapshotLookup& snapshots)
{
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity == nullptr)
            continue;

        const auto snapshot_it = snapshots.find(entity->entity_id());
        if (snapshot_it == snapshots.end() || snapshot_it->second == nullptr)
            continue;

        const og::sim::EntitySnapshot& snapshot = *snapshot_it->second;
        const bool reconfigure =
            entity->order() != snapshot.order || entity->family() != snapshot.family;
        apply_entity_snapshot_fields(world, *entity, snapshot, reconfigure);
    }
}

template <typename EntityList>
void create_missing_entities(GameWorld& world,
                             EntityList& preferred_list,
                             const std::vector<og::sim::EntitySnapshot>& snapshots)
{
    for (const auto& snapshot : snapshots)
    {
        if (world.find_by_id(snapshot.entity_id) != nullptr)
            continue;

        if (!world.entity_factory)
        {
            LogError("apply_snapshot: entity_factory unavailable for entity {}\n",
                     snapshot.entity_id);
            return;
        }

        auto created = world.entity_factory(snapshot.order, snapshot.family);
        if (!created)
        {
            LogError("apply_snapshot: failed to create entity {} ({}, {})\n",
                     snapshot.entity_id, static_cast<int>(snapshot.order),
                     static_cast<int>(snapshot.family));
            continue;
        }

        walker* const raw = created.get();
        raw->set_snapshot_entity_id(snapshot.entity_id);
        if (world.entity_configurator != nullptr)
            (void)world.configure_existing_entity(*raw, snapshot.order, snapshot.family);
        world.set_entity_derived_stats(raw, snapshot.order, snapshot.family);
        apply_entity_snapshot_fields(world, *raw, snapshot, false);
        preferred_list.push_back(std::move(created));
    }
}

template <typename EntityList>
void append_entities_to_index(EntityList& entities,
                              std::unordered_map<std::uint32_t, walker*>& index)
{
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity == nullptr || entity->entity_id() == 0)
            continue;
        index[entity->entity_id()] = entity;
    }
}

void resolve_cross_references(GameWorld& world,
                              const std::unordered_map<std::uint32_t, walker*>& index)
{
    auto resolve_entity = [&index](walker& entity) {
        const auto lookup = [&index](std::uint32_t entity_id) -> walker* {
            const auto it = index.find(entity_id);
            return (it == index.end()) ? nullptr : it->second;
        };

        entity.set_foe(lookup(entity.foe_id()));
        entity.set_leader(lookup(entity.leader_id()));
        entity.set_owner(lookup(entity.owner_id()));
        entity.set_collide_ob(lookup(entity.collide_ob_id()));

        if (statistics* const stats = entity.stats(); stats != nullptr)
            stats->set_controller(lookup(stats->controller_id()));
    };

    for (const auto& entry : world.oblist)
        if (entry != nullptr)
            resolve_entity(*entry);
    for (const auto& entry : world.fxlist)
        if (entry != nullptr)
            resolve_entity(*entry);
    for (const auto& entry : world.weaplist)
        if (entry != nullptr)
            resolve_entity(*entry);
}

void rebind_guys(GameWorld& world,
                 const EntitySnapshotLookup& ob_snapshots,
                 const EntitySnapshotLookup& fx_snapshots,
                 const EntitySnapshotLookup& weap_snapshots,
                 GuyStorage& guy_storage,
                 const GuyLookup& guy_lookup)
{
    std::unordered_set<std::int32_t> claimed_ids;

    auto bind_list = [&](auto& entities, const EntitySnapshotLookup& snapshots) {
        for (const auto& entry : entities)
        {
            walker* const entity = entry.get();
            if (entity == nullptr)
                continue;

            const auto snapshot_it = snapshots.find(entity->entity_id());
            if (snapshot_it == snapshots.end() || snapshot_it->second == nullptr)
                continue;

            const std::int32_t guy_id = snapshot_it->second->guy_id;
            if (guy_id == og::sim::kNoGuyId)
                continue;

            const auto guy_it = guy_lookup.find(guy_id);
            if (guy_it == guy_lookup.end())
            {
                LogError("apply_snapshot: missing guy {} for entity {}\n",
                         guy_id, entity->entity_id());
                continue;
            }

            if (claimed_ids.insert(guy_id).second)
            {
                auto owned_it = guy_storage.find(guy_id);
                if (owned_it != guy_storage.end() && owned_it->second != nullptr)
                    entity->set_owned_myguy(std::move(owned_it->second));
                else
                    entity->set_myguy_view(guy_it->second);
            }
            else
            {
                entity->set_myguy_view(guy_it->second);
            }
        }
    };

    bind_list(world.oblist, ob_snapshots);
    bind_list(world.fxlist, fx_snapshots);
    bind_list(world.weaplist, weap_snapshots);
}

void rebuild_obmap(GameWorld& world)
{
    if (world.myobmap == nullptr)
        return;

    auto readd_list = [&world](const auto& entities) {
        for (const auto& entry : entities)
        {
            if (entry == nullptr)
                continue;
            if (!entry->ignore() && !entry->dead())
                world.myobmap->add(entry.get(), entry->xpos(), entry->ypos());
            else
                world.myobmap->remove(entry.get());
        }
    };

    readd_list(world.oblist);
    readd_list(world.fxlist);
    readd_list(world.weaplist);
}

void apply_grid_snapshot(GameWorld& world, const og::sim::WorldSnapshot& snapshot)
{
    if (!world.grid.valid())
        return;

    const std::size_t grid_size =
        static_cast<std::size_t>(world.grid.w) * world.grid.h;

    if (snapshot.grid_full_resend)
    {
        if (snapshot.full_grid_data.size() != grid_size)
        {
            LogError("apply_snapshot: full grid size mismatch (got {}, expected {})\n",
                     snapshot.full_grid_data.size(), grid_size);
        }
        else
        {
            std::copy(snapshot.full_grid_data.begin(),
                      snapshot.full_grid_data.end(),
                      world.grid.data.get());
        }
    }

    for (const auto& tile : snapshot.grid_dirty_tiles)
    {
        if (tile.x < 0 || tile.y < 0 ||
            tile.x >= world.grid.w || tile.y >= world.grid.h)
        {
            continue;
        }

        const std::size_t grid_index =
            static_cast<std::size_t>(tile.y) * world.grid.w + tile.x;
        world.grid.data[grid_index] = tile.value;
    }
}

void apply_delta_grid(og::sim::WorldSnapshot& baseline,
                      const og::sim::WorldSnapshot& delta)
{
    baseline.grid_width = delta.grid_width;
    baseline.grid_height = delta.grid_height;

    if (baseline.grid_width == 0 || baseline.grid_height == 0)
    {
        baseline.grid_dirty = false;
        baseline.grid_full_resend = false;
        baseline.full_grid_data.clear();
        baseline.grid_dirty_tiles.clear();
        return;
    }

    if (delta.grid_full_resend)
        baseline.full_grid_data = delta.full_grid_data;

    if (!has_materialized_grid(baseline))
    {
        baseline.grid_dirty = delta.grid_dirty;
        baseline.grid_full_resend = delta.grid_full_resend;
        baseline.full_grid_data = delta.full_grid_data;
        baseline.grid_dirty_tiles = delta.grid_dirty_tiles;
        return;
    }

    for (const auto& tile : delta.grid_dirty_tiles)
    {
        if (tile.x < 0 || tile.y < 0 ||
            tile.x >= baseline.grid_width || tile.y >= baseline.grid_height)
        {
            continue;
        }

        const std::size_t grid_index =
            static_cast<std::size_t>(tile.y) * baseline.grid_width + tile.x;
        baseline.full_grid_data[grid_index] = tile.value;
    }

    normalize_materialized_grid(baseline);
}

template <typename EntityList>
void reorder_entity_list(EntityList& entities,
                         const std::vector<og::sim::EntitySnapshot>& snapshots)
{
    EntityStorage detached;
    entities.splice_into(detached);

    if (detached.empty())
        return;

    std::unordered_map<std::uint32_t, EntityStorage::iterator> by_id;
    by_id.reserve(detached.size());
    for (auto it = detached.begin(); it != detached.end(); ++it)
    {
        if (*it != nullptr)
            by_id[(*it)->entity_id()] = it;
    }

    EntityStorage ordered;
    for (const auto& snapshot : snapshots)
    {
        const auto found = by_id.find(snapshot.entity_id);
        if (found == by_id.end())
            continue;
        ordered.splice(ordered.end(), detached, found->second);
    }

    ordered.splice(ordered.end(), detached);
    entities.splice(entities.end(), ordered);
}

void clear_entity_dirty_masks(GameWorld& world)
{
    auto clear_list = [](const auto& entities) {
        for (const auto& entry : entities)
        {
            if (entry != nullptr)
                entry->clear_dirty();
        }
    };

    clear_list(world.oblist);
    clear_list(world.fxlist);
    clear_list(world.weaplist);
}

std::uint8_t capture_bool_byte(const bool& value)
{
    return value ? 1U : 0U;
}

void capture_world_grid(const GameWorld& world,
                        og::sim::WorldSnapshot& snapshot,
                        std::vector<std::pair<short, short>> dirty_tiles,
                        bool keyframe)
{
    snapshot.grid_width = world.grid.w;
    snapshot.grid_height = world.grid.h;

    if (!world.grid.valid())
        return;

    const bool overflowed = dirty_tiles.size() > og::sim::MAX_GRID_DIRTY_TILES;
    snapshot.grid_dirty = keyframe || !dirty_tiles.empty();
    snapshot.grid_full_resend = keyframe || overflowed;

    if (snapshot.grid_full_resend)
    {
        const std::size_t grid_size =
            static_cast<std::size_t>(world.grid.w) * world.grid.h;
        snapshot.full_grid_data.assign(world.grid.data.get(),
                                       world.grid.data.get() + grid_size);
    }

    if (overflowed)
        dirty_tiles.clear();

    snapshot.grid_dirty_tiles.reserve(dirty_tiles.size());
    for (const auto& [x, y] : dirty_tiles)
    {
        if (x < 0 || y < 0 || x >= world.grid.w || y >= world.grid.h)
            continue;

        const std::size_t grid_index =
            static_cast<std::size_t>(y) * world.grid.w + x;
        snapshot.grid_dirty_tiles.push_back(
            {x, y, world.grid.data[grid_index]});
    }
}

void capture_entity_stats(const statistics* entity_stats,
                          og::sim::EntitySnapshot& snapshot)
{
    if (entity_stats == nullptr)
        return;

    snapshot.hitpoints = entity_stats->hitpoints();
    snapshot.max_hitpoints = entity_stats->max_hitpoints();
    snapshot.magicpoints = entity_stats->magicpoints();
    snapshot.max_magicpoints = entity_stats->max_magicpoints();
    snapshot.max_heal_delay = entity_stats->max_heal_delay();
    snapshot.current_heal_delay = entity_stats->current_heal_delay();
    snapshot.max_magic_delay = entity_stats->max_magic_delay();
    snapshot.current_magic_delay = entity_stats->current_magic_delay();
    snapshot.magic_per_round = entity_stats->magic_per_round();
    snapshot.heal_per_round = entity_stats->heal_per_round();
    snapshot.armor = entity_stats->armor();
    snapshot.level = entity_stats->level();
    snapshot.bit_flags = entity_stats->bit_flags();
    snapshot.delete_me = entity_stats->delete_me();
    snapshot.frozen_delay = entity_stats->frozen_delay();
    snapshot.weapon_cost = entity_stats->weapon_cost();
    for (int i = 0; i < NUM_SPECIALS; ++i)
        snapshot.special_cost[i] = entity_stats->special_cost(i);
    snapshot.old_order = entity_stats->old_order();
    snapshot.old_family = entity_stats->old_family();
    snapshot.last_distance = entity_stats->last_distance();
    snapshot.current_distance = entity_stats->current_distance();
    snapshot.controller_id = entity_stats->controller_id();
}

og::sim::EntitySnapshot capture_entity_snapshot(walker& entity,
                                                bool keyframe,
                                                bool consume_dirty_state)
{
    entity.sync_ids_from_pointers();

    og::sim::EntitySnapshot snapshot;
    if (keyframe)
    {
        std::fill(std::begin(snapshot.dirty_mask),
                  std::end(snapshot.dirty_mask),
                  ~0ULL);
    }
    else
    {
        for (std::size_t i = 0; i < og::sim::kEntitySnapshotDirtyMaskWords; ++i)
            snapshot.dirty_mask[i] = entity.dirty_mask_word(i);
    }

    snapshot.guy_id = (entity.myguy != nullptr) ? entity.myguy->id
                                                : og::sim::kNoGuyId;
    snapshot.entity_id = entity.entity_id();
    snapshot.xpos = entity.xpos();
    snapshot.ypos = entity.ypos();
    snapshot.sizex = entity.sizex();
    snapshot.sizey = entity.sizey();
    snapshot.team_num = entity.team_num();
    snapshot.real_team_num = entity.real_team_num();
    snapshot.user = entity.user();
    snapshot.dead = entity.dead();
    snapshot.death_called = entity.death_called();
    snapshot.invulnerable_left = entity.invulnerable_left();
    snapshot.invisibility_left = entity.invisibility_left();
    snapshot.flight_left = entity.flight_left();
    snapshot.bonus_rounds = entity.bonus_rounds();
    snapshot.order = entity.order();
    snapshot.family = entity.family();
    snapshot.frame = entity.frame();
    snapshot.worldx = entity.worldx();
    snapshot.worldy = entity.worldy();

    snapshot.lastx = entity.lastx();
    snapshot.lasty = entity.lasty();
    snapshot.stepsize = entity.stepsize();
    snapshot.normal_stepsize = entity.normal_stepsize();
    snapshot.curdir = entity.curdir();
    snapshot.enddir = entity.enddir();
    snapshot.damage = entity.damage();
    snapshot.fire_frequency = entity.fire_frequency();
    snapshot.busy = entity.busy();
    snapshot.current_weapon = entity.current_weapon();
    snapshot.default_weapon = entity.default_weapon();
    snapshot.attack_lunge = entity.attack_lunge();
    snapshot.attack_lunge_angle = entity.attack_lunge_angle();
    snapshot.hit_recoil = entity.hit_recoil();
    snapshot.hit_recoil_angle = entity.hit_recoil_angle();
    snapshot.last_hitpoints = entity.last_hitpoints();
    snapshot.action = entity.action();
    snapshot.act_type = entity.act_type();
    snapshot.old_act_type = entity.old_act_type();
    snapshot.ani_type = entity.ani_type();
    snapshot.cycle = entity.cycle();
    snapshot.drawcycle = entity.drawcycle();
    snapshot.current_special = entity.current_special();
    snapshot.ignore = entity.ignore();
    snapshot.in_act = capture_bool_byte(entity.in_act());
    snapshot.shifter_down = entity.shifter_down();
    snapshot.yo_delay = entity.yo_delay();
    snapshot.skip_exit = entity.skip_exit();
    snapshot.outline = entity.outline();
    snapshot.hurt_flash = capture_bool_byte(entity.hurt_flash());
    snapshot.lifetime = entity.lifetime();
    snapshot.speed_bonus = entity.speed_bonus();
    snapshot.speed_bonus_left = entity.speed_bonus_left();
    snapshot.charm_left = entity.charm_left();
    snapshot.weapons_left = entity.weapons_left();
    snapshot.keys = entity.keys();
    snapshot.view_all = entity.view_all();
    snapshot.lineofsight = entity.lineofsight();
    snapshot.path_check_counter = entity.path_check_counter();
    snapshot.regen_delay = entity.regen_delay();
    snapshot.foe_id = entity.foe_id();
    snapshot.leader_id = entity.leader_id();
    snapshot.owner_id = entity.owner_id();
    snapshot.collide_ob_id = entity.collide_ob_id();

    capture_entity_stats(entity.stats(), snapshot);

    if (const auto* weapon = dynamic_cast<const weap*>(&entity);
        weapon != nullptr)
    {
        snapshot.do_bounce = weapon->do_bounce();
    }
    else
    {
        snapshot.do_bounce = 0;
    }

    if (consume_dirty_state)
        entity.clear_dirty();
    return snapshot;
}

template <typename EntityList>
void capture_entity_list(EntityList& entities,
                         std::vector<og::sim::EntitySnapshot>& entity_snapshots,
                         std::vector<og::sim::GuySnapshot>& guy_snapshots,
                         std::unordered_set<int>& seen_guy_ids,
                         bool keyframe,
                         bool consume_dirty_state)
{
    entity_snapshots.reserve(entities.size());
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity == nullptr)
            continue;

        if (entity->myguy != nullptr &&
            seen_guy_ids.insert(entity->myguy->id).second)
        {
            guy_snapshots.push_back(capture_guy_snapshot(*entity->myguy));
        }

        entity_snapshots.push_back(
            capture_entity_snapshot(*entity, keyframe, consume_dirty_state));
    }
}

std::vector<std::pair<short, short>> copy_grid_dirty_tiles(const GameWorld& world)
{
    return std::vector<std::pair<short, short>>(world.grid_dirty_tiles().begin(),
                                                world.grid_dirty_tiles().end());
}

// Copies the live CtfState into the snapshot, normalized: only the counted
// control points, anchors, and queue entries are carried, so a snapshot of a
// world holding stale data past a count (inactive-team strip leaves anchor
// coordinates behind) still compares equal to its wire round trip.
void capture_ctf_state(const GameWorld& world, og::sim::WorldSnapshot& snapshot)
{
    const og::sim::CtfState& ctf = world.ctf;
    snapshot.ctf_active = ctf.active;
    snapshot.ctf_init_attempted = ctf.init_attempted;
    snapshot.ctf_team_count = ctf.team_count;
    snapshot.ctf_capture_limit = ctf.capture_limit;
    snapshot.ctf_respawn_ticks = ctf.respawn_ticks;
    snapshot.ctf_flag_return_ticks = ctf.flag_return_ticks;
    snapshot.ctf_time_limit_ticks = ctf.time_limit_ticks;
    snapshot.ctf_winner_team = ctf.winner_team;
    snapshot.ctf_winner_is_player = ctf.winner_is_player;
    std::copy(std::begin(ctf.captures), std::end(ctf.captures),
              std::begin(snapshot.ctf_captures));
    snapshot.ctf_respawn_serial = ctf.respawn_serial;
    std::copy(std::begin(ctf.team_active), std::end(ctf.team_active),
              std::begin(snapshot.ctf_team_active));
    std::copy(std::begin(ctf.flags), std::end(ctf.flags),
              std::begin(snapshot.ctf_flags));

    snapshot.ctf_cp_count = std::min<std::uint8_t>(
        ctf.cp_count, og::sim::kCtfMaxControlPoints);
    for (int i = 0; i < snapshot.ctf_cp_count; ++i)
        snapshot.ctf_cps[i] = ctf.cps[i];

    for (int team = 0; team < 4; ++team)
    {
        snapshot.ctf_anchor_count[team] = std::min<std::uint8_t>(
            ctf.anchor_count[team], og::sim::kCtfMaxAnchorsPerTeam);
        for (int i = 0; i < snapshot.ctf_anchor_count[team]; ++i)
        {
            snapshot.ctf_anchor_x[team][i] = ctf.anchor_x[team][i];
            snapshot.ctf_anchor_y[team][i] = ctf.anchor_y[team][i];
        }
    }

    snapshot.ctf_respawn_queue.assign(
        ctf.respawn_queue.begin(),
        ctf.respawn_queue.begin() +
            static_cast<std::ptrdiff_t>(std::min<std::size_t>(
                ctf.respawn_queue.size(), og::sim::kCtfMaxRespawnEntries)));

    snapshot.ctf_requested_team_count = world.ctf_requested_team_count;
    snapshot.ctf_requested_capture_limit = world.ctf_requested_capture_limit;
    snapshot.ctf_requested_respawn_ticks = world.ctf_requested_respawn_ticks;
    snapshot.ctf_requested_strip_scenario_troops =
        world.ctf_requested_strip_scenario_troops;
}

// Rebuilds the world's CtfState from the snapshot. Constructing a fresh state
// zeroes everything past the carried counts, mirroring capture normalization,
// and the counts are clamped so a hand-crafted snapshot cannot overflow.
void apply_ctf_state(GameWorld& world, const og::sim::WorldSnapshot& snapshot)
{
    og::sim::CtfState ctf;
    ctf.active = snapshot.ctf_active;
    ctf.init_attempted = snapshot.ctf_init_attempted;
    ctf.team_count = snapshot.ctf_team_count;
    ctf.capture_limit = snapshot.ctf_capture_limit;
    ctf.respawn_ticks = snapshot.ctf_respawn_ticks;
    ctf.flag_return_ticks = snapshot.ctf_flag_return_ticks;
    ctf.time_limit_ticks = snapshot.ctf_time_limit_ticks;
    ctf.winner_team = snapshot.ctf_winner_team;
    ctf.winner_is_player = snapshot.ctf_winner_is_player;
    std::copy(std::begin(snapshot.ctf_captures), std::end(snapshot.ctf_captures),
              std::begin(ctf.captures));
    ctf.respawn_serial = snapshot.ctf_respawn_serial;
    std::copy(std::begin(snapshot.ctf_team_active),
              std::end(snapshot.ctf_team_active),
              std::begin(ctf.team_active));
    std::copy(std::begin(snapshot.ctf_flags), std::end(snapshot.ctf_flags),
              std::begin(ctf.flags));

    ctf.cp_count = std::min<std::uint8_t>(snapshot.ctf_cp_count,
                                          og::sim::kCtfMaxControlPoints);
    for (int i = 0; i < ctf.cp_count; ++i)
        ctf.cps[i] = snapshot.ctf_cps[i];

    for (int team = 0; team < 4; ++team)
    {
        ctf.anchor_count[team] = std::min<std::uint8_t>(
            snapshot.ctf_anchor_count[team], og::sim::kCtfMaxAnchorsPerTeam);
        for (int i = 0; i < ctf.anchor_count[team]; ++i)
        {
            ctf.anchor_x[team][i] = snapshot.ctf_anchor_x[team][i];
            ctf.anchor_y[team][i] = snapshot.ctf_anchor_y[team][i];
        }
    }

    ctf.respawn_queue.assign(
        snapshot.ctf_respawn_queue.begin(),
        snapshot.ctf_respawn_queue.begin() +
            static_cast<std::ptrdiff_t>(std::min<std::size_t>(
                snapshot.ctf_respawn_queue.size(),
                og::sim::kCtfMaxRespawnEntries)));

    world.ctf = std::move(ctf);
    world.ctf_requested_team_count = snapshot.ctf_requested_team_count;
    world.ctf_requested_capture_limit = snapshot.ctf_requested_capture_limit;
    world.ctf_requested_respawn_ticks = snapshot.ctf_requested_respawn_ticks;
    world.ctf_requested_strip_scenario_troops =
        snapshot.ctf_requested_strip_scenario_troops;
}

og::sim::WorldSnapshot capture_snapshot_impl(GameWorld& world,
                                             bool keyframe,
                                             bool consume_transients)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = world.tick_count_;
    snapshot.rng_state = world.rng_.state_;
    snapshot.level_tick_count = world.level_tick_count();

    snapshot.level_done = world.level_done;
    snapshot.game_ended = world.game_ended;
    snapshot.end = world.end;
    snapshot.retry = world.retry;
    snapshot.next_level = world.next_level;
    snapshot.ending = world.ending;

    snapshot.enemy_freeze = world.enemy_freeze;
    snapshot.timer_wait = world.timer_wait;
    snapshot.living_count = world.living_count;
    snapshot.control_hp = world.control_hp;
    snapshot.my_team = world.my_team;
    snapshot.allied_mode = world.allied_mode;
    snapshot.difficulty = world.difficulty;
    snapshot.withdraw_requested = world.withdraw_requested;
    snapshot.withdraw_level = world.withdraw_level;
    snapshot.guy_id_counter = world.guy_id_counter;
    std::copy(std::begin(world.m_score), std::end(world.m_score),
              std::begin(snapshot.m_score));

    snapshot.current_palette_id = world.current_palette_id;
    snapshot.pending_exit_prompt = world.pending_exit_prompt;
    snapshot.paused = world.paused;
    snapshot.pause_player_index = world.pause_player_index;
    capture_ctf_state(world, snapshot);

    std::unordered_set<int> seen_guy_ids;
    capture_entity_list(world.oblist, snapshot.oblist, snapshot.guy_snapshots,
                        seen_guy_ids, keyframe, consume_transients);
    capture_entity_list(world.fxlist, snapshot.fxlist, snapshot.guy_snapshots,
                        seen_guy_ids, keyframe, consume_transients);
    capture_entity_list(world.weaplist, snapshot.weaplist, snapshot.guy_snapshots,
                        seen_guy_ids, keyframe, consume_transients);

    snapshot.removed_entity_ids = consume_transients
        ? world.take_removed_entity_ids()
        : std::vector<std::uint32_t>(
              world.removed_entity_ids().begin(), world.removed_entity_ids().end());
    capture_world_grid(world,
                       snapshot,
                       consume_transients ? world.take_grid_dirty_tiles()
                                          : copy_grid_dirty_tiles(world),
                       keyframe);
    snapshot.snapshot_hash = compute_snapshot_hash_impl(snapshot, &world);

    return snapshot;
}

} // namespace

namespace og::sim {

WorldSnapshot capture_snapshot(GameWorld& world)
{
    return capture_snapshot_impl(world, false, true);
}

WorldSnapshot capture_keyframe_snapshot(GameWorld& world)
{
    return capture_snapshot_impl(world, true, true);
}

WorldSnapshot peek_snapshot(GameWorld& world)
{
    return capture_snapshot_impl(world, false, false);
}

WorldSnapshot peek_keyframe_snapshot(GameWorld& world)
{
    return capture_snapshot_impl(world, true, false);
}

namespace {

void serialize_event_payload(std::vector<std::uint8_t>& buffer,
                             const og::sim::Event& event)
{
    append_u32(buffer, event.tick);
    append_u32(buffer, static_cast<std::uint32_t>(event.kind));
    append_u32(buffer, event.a);
    append_u32(buffer, event.b);
    append_string(buffer, event.text);
}

og::sim::Event deserialize_event_payload(ByteReader& reader,
                                         std::size_t index,
                                         const char* batch_name)
{
    const std::string prefix =
        std::string(batch_name) + ".events[" + std::to_string(index) + "]";

    og::sim::Event event;
    event.tick = reader.read_u32((prefix + ".tick").c_str());
    event.kind = static_cast<og::sim::EventKind>(
        reader.read_u32((prefix + ".kind").c_str()));
    event.a = reader.read_u32((prefix + ".a").c_str());
    event.b = reader.read_u32((prefix + ".b").c_str());
    event.text = reader.read_string((prefix + ".text").c_str());
    return event;
}

std::vector<std::uint8_t> serialize_event_batch_message(
    const og::sim::SimEventBatch& batch,
    std::uint8_t message_type,
    const char* batch_name)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(16 + (batch.events.size() * 24));
    append_u32(payload, batch.sequence);
    if (batch.events.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error(std::string(batch_name) +
                                 ": event count exceeds 32-bit wire length");
    }
    append_u32(payload, static_cast<std::uint32_t>(batch.events.size()));
    for (const auto& event : batch.events)
        serialize_event_payload(payload, event);

    if (payload.size() > std::numeric_limits<std::uint16_t>::max())
    {
        throw std::runtime_error(std::string(batch_name) +
                                 ": payload exceeds 16-bit wire length");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kMessageHeaderSize + payload.size());
    append_transport_header(bytes, message_type,
                            static_cast<std::uint16_t>(payload.size()));
    append_bytes(bytes, payload.data(), payload.size());
    return bytes;
}

og::sim::SimEventBatch deserialize_event_batch_message(
    std::span<const std::uint8_t> data,
    std::uint8_t message_type,
    const char* batch_name)
{
    if (data.data() == nullptr || data.size() < kMessageHeaderSize)
    {
        throw std::runtime_error(std::string(batch_name) +
                                 ": truncated header");
    }

    og::sim::TransportEnvelope envelope;
    if (!decode_transport_envelope(data, envelope))
    {
        throw std::runtime_error(std::string(batch_name) +
                                 ": unsupported protocol version " +
                                 std::to_string(data[0]));
    }
    if (envelope.message_type != message_type)
    {
        throw std::runtime_error(std::string(batch_name) +
                                 ": unexpected message type " +
                                 std::to_string(envelope.message_type));
    }
    if (data.size() != kMessageHeaderSize + envelope.payload_length)
    {
        throw std::runtime_error(std::string(batch_name) +
                                 ": payload length mismatch");
    }

    ByteReader reader(data.data() + kMessageHeaderSize, envelope.payload_length,
                      batch_name);
    og::sim::SimEventBatch batch;
    batch.sequence = reader.read_u32("event_batch.sequence");
    const std::uint32_t event_count = read_bounded_count(
        reader, "event_batch.count", 4096, batch_name);
    batch.events.reserve(event_count);
    for (std::uint32_t i = 0; i < event_count; ++i)
        batch.events.push_back(
            deserialize_event_payload(reader, i, "event_batch"));
    reader.finish(batch_name);
    return batch;
}

} // namespace

std::vector<std::uint8_t> serialize_snapshot(const WorldSnapshot& snapshot)
{
    const std::vector<std::uint8_t> payload = serialize_snapshot_payload(snapshot);
    const std::vector<std::uint8_t> compressed = compress_zlib_payload(payload);

    if (compressed.size() > std::numeric_limits<std::uint16_t>::max())
    {
        throw std::runtime_error("snapshot message: payload exceeds 16-bit wire length");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kMessageHeaderSize + compressed.size());
    append_transport_header(bytes, kSnapshotMessageType,
                            static_cast<std::uint16_t>(compressed.size()));
    append_bytes(bytes, compressed.data(), compressed.size());
    return bytes;
}

WorldSnapshot deserialize_snapshot(std::span<const std::uint8_t> data)
{
    if (data.data() == nullptr || data.size() < kMessageHeaderSize)
    {
        throw std::runtime_error("snapshot message: truncated header");
    }
    og::sim::TransportEnvelope envelope;
    if (!decode_transport_envelope(data, envelope))
    {
        throw std::runtime_error(
            "snapshot message: unsupported protocol version " +
            std::to_string(data[0]));
    }
    if (envelope.message_type != kSnapshotMessageType)
    {
        throw std::runtime_error(
            "snapshot message: unexpected message type " +
            std::to_string(envelope.message_type));
    }
    if (data.size() != kMessageHeaderSize + envelope.payload_length)
    {
        throw std::runtime_error("snapshot message: payload length mismatch");
    }

    const std::vector<std::uint8_t> payload =
        decompress_zlib_payload(data.data() + kMessageHeaderSize, envelope.payload_length);
    return deserialize_snapshot_payload(payload);
}

std::vector<std::uint8_t> serialize_delta(const WorldSnapshot& delta)
{
    const std::vector<std::uint8_t> payload = serialize_delta_payload(delta);
    const std::vector<std::uint8_t> compressed = compress_zlib_payload(payload);
    const bool use_uncompressed = compressed.size() >= payload.size();
    const std::vector<std::uint8_t>& wire_payload =
        use_uncompressed ? payload : compressed;
    const std::size_t framed_payload_size =
        kDeltaWirePayloadHeaderSize + wire_payload.size();

    if (framed_payload_size > std::numeric_limits<std::uint16_t>::max())
    {
        throw std::runtime_error("delta message: payload exceeds 16-bit wire length");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kMessageHeaderSize + framed_payload_size);
    append_transport_header(bytes, kDeltaSnapshotMessageType,
                            static_cast<std::uint16_t>(framed_payload_size));
    bytes.push_back(use_uncompressed ? kDeltaPayloadUncompressedFlag : 0U);
    append_bytes(bytes, wire_payload.data(), wire_payload.size());
    return bytes;
}

WorldSnapshot deserialize_delta(std::span<const std::uint8_t> data)
{
    if (data.data() == nullptr || data.size() < kMessageHeaderSize)
    {
        throw std::runtime_error("delta message: truncated header");
    }
    og::sim::TransportEnvelope envelope;
    if (!decode_transport_envelope(data, envelope))
    {
        throw std::runtime_error(
            "delta message: unsupported protocol version " +
            std::to_string(data[0]));
    }

    if (envelope.message_type != kDeltaSnapshotMessageType)
    {
        throw std::runtime_error(
            "delta message: unexpected message type " +
            std::to_string(envelope.message_type));
    }
    if (data.size() != kMessageHeaderSize + envelope.payload_length)
    {
        throw std::runtime_error("delta message: payload length mismatch");
    }
    if (envelope.payload_length < kDeltaWirePayloadHeaderSize)
    {
        throw std::runtime_error("delta message: truncated payload flags");
    }

    const std::uint8_t payload_flags = data[kMessageHeaderSize];
    if ((payload_flags & ~kDeltaPayloadSupportedFlags) != 0)
    {
        throw std::runtime_error(
            "delta message: unsupported payload flags " +
            std::to_string(payload_flags));
    }
    const bool payload_is_uncompressed =
        (payload_flags & kDeltaPayloadUncompressedFlag) != 0;
    const std::size_t wire_payload_length =
        static_cast<std::size_t>(envelope.payload_length) -
        kDeltaWirePayloadHeaderSize;
    const std::uint8_t* const wire_payload =
        data.data() + kMessageHeaderSize + kDeltaWirePayloadHeaderSize;

    std::vector<std::uint8_t> payload;
    if (payload_is_uncompressed)
    {
        payload.assign(wire_payload, wire_payload + wire_payload_length);
    }
    else
    {
        payload = decompress_zlib_payload(wire_payload, wire_payload_length);
    }

    return deserialize_delta_payload(payload);
}

std::vector<std::uint8_t> serialize_sim_event_batch(const SimEventBatch& batch)
{
    return serialize_event_batch_message(batch, kSimEventBatchMessageType,
                                         "sim event batch");
}

SimEventBatch deserialize_sim_event_batch(std::span<const std::uint8_t> data)
{
    return deserialize_event_batch_message(data, kSimEventBatchMessageType,
                                           "sim event batch");
}

std::vector<std::uint8_t> serialize_game_flow_event_batch(
    const SimEventBatch& batch)
{
    return serialize_event_batch_message(batch, kGameFlowEventBatchMessageType,
                                         "game flow event batch");
}

SimEventBatch deserialize_game_flow_event_batch(std::span<const std::uint8_t> data)
{
    return deserialize_event_batch_message(
        data, kGameFlowEventBatchMessageType, "game flow event batch");
}

bool is_game_flow_event(EventKind kind) noexcept
{
    switch (kind)
    {
    case EventKind::EndGame:
    case EventKind::SetEnd:
    case EventKind::RequestExitConfirmation:
    case EventKind::WithdrawToLevel:
    case EventKind::ScoreChange:
    case EventKind::DamageTile:
        return true;

    case EventKind::None:
    case EventKind::PlaySound:
    case EventKind::Notification:
    case EventKind::SetPalette:
    case EventKind::RequestRedraw:
        return false;
    }

    return false;
}

void normalize_endgame_event_order(GameFlowEventBatch& batch)
{
    std::stable_sort(
        batch.events.begin(), batch.events.end(),
        [](const Event& lhs, const Event& rhs) {
            return lhs.kind != EventKind::EndGame &&
                   rhs.kind == EventKind::EndGame;
        });
}

void split_event_batches(const SimEventBatch& source,
                         SimEventBatch& sim_batch,
                         GameFlowEventBatch& game_flow_batch)
{
    sim_batch.sequence = source.sequence;
    sim_batch.events.clear();
    sim_batch.events.reserve(source.events.size());

    game_flow_batch.sequence = source.sequence;
    game_flow_batch.events.clear();
    game_flow_batch.events.reserve(source.events.size());

    for (const auto& event : source.events)
    {
        if (is_game_flow_event(event.kind))
            game_flow_batch.events.push_back(event);
        else
            sim_batch.events.push_back(event);
    }

    normalize_endgame_event_order(game_flow_batch);
}

void apply_snapshot(GameWorld& world, const WorldSnapshot& snapshot)
{
    ApplyingSnapshotGuard applying_guard(world);

    GameplayContext gameplay_context;
    const bool has_context = world.populate_gameplay_context(gameplay_context);
    assert(has_context && gameplay_context.world == &world &&
           "apply_snapshot requires a bound gameplay context for the target world");
    if (!has_context || gameplay_context.world != &world)
    {
        LogError("apply_snapshot: missing gameplay context bindings for world {}\n",
                 world.id);
        return;
    }

    GameplayContext* installed_context = &gameplay_context;
    if (current_game != nullptr &&
        current_game->world == &world &&
        current_game->save == gameplay_context.save &&
        current_game->sim_events == gameplay_context.sim_events &&
        current_game->config == gameplay_context.config)
    {
        installed_context = current_game;
    }

    // Snapshot application may target a world whose bound event sink differs
    // from the ambient session context (for example, benchmark/test harnesses
    // that swap in a dedicated SimEventLog). Install the world-bound context
    // temporarily instead of assuming the ambient pointer is already exact.
    struct ScopedSnapshotGameplayContext final
    {
        explicit ScopedSnapshotGameplayContext(GameplayContext* context)
            : previous_(current_game)
        {
            current_game = context;
        }

        ~ScopedSnapshotGameplayContext()
        {
            current_game = previous_;
        }

        ScopedSnapshotGameplayContext(const ScopedSnapshotGameplayContext&) = delete;
        ScopedSnapshotGameplayContext& operator=(const ScopedSnapshotGameplayContext&) = delete;

        GameplayContext* previous_ = nullptr;
    } gameplay_guard(installed_context);

    SimEventLogSuppressGuard event_guard(*gameplay_context.sim_events);

    world.tick_count_ = snapshot.tick_count;
    world.set_level_tick_count(snapshot.level_tick_count);

    world.level_done = snapshot.level_done;
    world.game_ended = snapshot.game_ended;
    world.end = snapshot.end;
    world.retry = snapshot.retry;
    world.next_level = snapshot.next_level;
    world.ending = snapshot.ending;
    world.enemy_freeze = snapshot.enemy_freeze;
    world.timer_wait = snapshot.timer_wait;
    world.living_count = snapshot.living_count;
    world.control_hp = snapshot.control_hp;
    world.my_team = snapshot.my_team;
    world.allied_mode = snapshot.allied_mode;
    world.difficulty = snapshot.difficulty;
    world.withdraw_requested = snapshot.withdraw_requested;
    world.withdraw_level = snapshot.withdraw_level;
    world.guy_id_counter = snapshot.guy_id_counter;
    std::copy(std::begin(snapshot.m_score), std::end(snapshot.m_score),
              std::begin(world.m_score));
    world.current_palette_id = snapshot.current_palette_id;
    world.pending_exit_prompt = snapshot.pending_exit_prompt;
    world.paused = snapshot.paused;
    world.pause_player_index = snapshot.pause_player_index;
    apply_ctf_state(world, snapshot);

    GuyStorage guy_storage;
    GuyLookup guy_lookup;
    guy_storage.reserve(snapshot.guy_snapshots.size());
    guy_lookup.reserve(snapshot.guy_snapshots.size());
    for (const auto& guy_snapshot : snapshot.guy_snapshots)
    {
        auto applied_guy = std::make_unique<guy>(guy_snapshot.family);
        apply_guy_snapshot(*applied_guy, guy_snapshot);
        guy_lookup[guy_snapshot.guy_id] = applied_guy.get();
        guy_storage[guy_snapshot.guy_id] = std::move(applied_guy);
    }
    world.guy_id_counter = snapshot.guy_id_counter;

    clear_entity_guy_links(world.oblist);
    clear_entity_guy_links(world.fxlist);
    clear_entity_guy_links(world.weaplist);

    EntitySnapshotLookup ob_snapshots;
    EntitySnapshotLookup fx_snapshots;
    EntitySnapshotLookup weap_snapshots;
    build_entity_snapshot_lookup(snapshot.oblist, ob_snapshots);
    build_entity_snapshot_lookup(snapshot.fxlist, fx_snapshots);
    build_entity_snapshot_lookup(snapshot.weaplist, weap_snapshots);

    remove_missing_entities(world, world.oblist, ob_snapshots);
    remove_missing_entities(world, world.fxlist, fx_snapshots);
    remove_missing_entities(world, world.weaplist, weap_snapshots);

    update_existing_entities(world, world.oblist, ob_snapshots);
    update_existing_entities(world, world.fxlist, fx_snapshots);
    update_existing_entities(world, world.weaplist, weap_snapshots);

    create_missing_entities(world, world.oblist, snapshot.oblist);
    create_missing_entities(world, world.fxlist, snapshot.fxlist);
    create_missing_entities(world, world.weaplist, snapshot.weaplist);

    std::unordered_map<std::uint32_t, walker*> live_entities;
    live_entities.reserve(
        world.oblist.size() + world.fxlist.size() + world.weaplist.size());
    append_entities_to_index(world.oblist, live_entities);
    append_entities_to_index(world.fxlist, live_entities);
    append_entities_to_index(world.weaplist, live_entities);

    resolve_cross_references(world, live_entities);
    rebind_guys(world, ob_snapshots, fx_snapshots, weap_snapshots,
                guy_storage, guy_lookup);
    rebuild_obmap(world);
    apply_grid_snapshot(world, snapshot);
    reorder_entity_list(world.oblist, snapshot.oblist);
    reorder_entity_list(world.fxlist, snapshot.fxlist);
    reorder_entity_list(world.weaplist, snapshot.weaplist);
    world.dead_list.clear();
    world.clear_removed_entity_ids();
    world.clear_grid_dirty_tiles();
    clear_entity_dirty_masks(world);

    // walker construction rolls path_check_counter from the world RNG, so
    // restore the authoritative snapshot state after all apply-side effects.
    world.rng_.state_ = snapshot.rng_state;
}

void apply_delta(WorldSnapshot& baseline, const WorldSnapshot& delta)
{
    baseline.tick_count = delta.tick_count;
    baseline.rng_state = delta.rng_state;
    baseline.level_tick_count = delta.level_tick_count;
    baseline.level_done = delta.level_done;
    baseline.game_ended = delta.game_ended;
    baseline.end = delta.end;
    baseline.retry = delta.retry;
    baseline.next_level = delta.next_level;
    baseline.ending = delta.ending;
    baseline.enemy_freeze = delta.enemy_freeze;
    baseline.timer_wait = delta.timer_wait;
    baseline.living_count = delta.living_count;
    baseline.control_hp = delta.control_hp;
    baseline.my_team = delta.my_team;
    baseline.allied_mode = delta.allied_mode;
    baseline.difficulty = delta.difficulty;
    baseline.withdraw_requested = delta.withdraw_requested;
    baseline.withdraw_level = delta.withdraw_level;
    baseline.guy_id_counter = delta.guy_id_counter;
    std::copy(std::begin(delta.m_score), std::end(delta.m_score),
              std::begin(baseline.m_score));
    baseline.current_palette_id = delta.current_palette_id;
    baseline.pending_exit_prompt = delta.pending_exit_prompt;
    baseline.paused = delta.paused;
    baseline.pause_player_index = delta.pause_player_index;
    baseline.snapshot_hash = delta.snapshot_hash;

    baseline.ctf_active = delta.ctf_active;
    baseline.ctf_init_attempted = delta.ctf_init_attempted;
    baseline.ctf_team_count = delta.ctf_team_count;
    baseline.ctf_capture_limit = delta.ctf_capture_limit;
    baseline.ctf_respawn_ticks = delta.ctf_respawn_ticks;
    baseline.ctf_flag_return_ticks = delta.ctf_flag_return_ticks;
    baseline.ctf_time_limit_ticks = delta.ctf_time_limit_ticks;
    baseline.ctf_winner_team = delta.ctf_winner_team;
    baseline.ctf_winner_is_player = delta.ctf_winner_is_player;
    std::copy(std::begin(delta.ctf_captures), std::end(delta.ctf_captures),
              std::begin(baseline.ctf_captures));
    baseline.ctf_respawn_serial = delta.ctf_respawn_serial;
    std::copy(std::begin(delta.ctf_team_active), std::end(delta.ctf_team_active),
              std::begin(baseline.ctf_team_active));
    std::copy(std::begin(delta.ctf_flags), std::end(delta.ctf_flags),
              std::begin(baseline.ctf_flags));
    baseline.ctf_cp_count = delta.ctf_cp_count;
    std::copy(std::begin(delta.ctf_cps), std::end(delta.ctf_cps),
              std::begin(baseline.ctf_cps));
    std::copy(std::begin(delta.ctf_anchor_count), std::end(delta.ctf_anchor_count),
              std::begin(baseline.ctf_anchor_count));
    for (int team = 0; team < 4; ++team)
    {
        std::copy(std::begin(delta.ctf_anchor_x[team]),
                  std::end(delta.ctf_anchor_x[team]),
                  std::begin(baseline.ctf_anchor_x[team]));
        std::copy(std::begin(delta.ctf_anchor_y[team]),
                  std::end(delta.ctf_anchor_y[team]),
                  std::begin(baseline.ctf_anchor_y[team]));
    }
    baseline.ctf_respawn_queue = delta.ctf_respawn_queue;
    baseline.ctf_requested_team_count = delta.ctf_requested_team_count;
    baseline.ctf_requested_capture_limit = delta.ctf_requested_capture_limit;
    baseline.ctf_requested_respawn_ticks = delta.ctf_requested_respawn_ticks;
    baseline.ctf_requested_strip_scenario_troops =
        delta.ctf_requested_strip_scenario_troops;

    apply_delta_grid(baseline, delta);
    baseline.guy_snapshots = delta.guy_snapshots;
    std::vector<std::uint32_t> removed_entity_ids;
    removed_entity_ids.reserve(delta.removed_entity_ids.size());

    apply_delta_entity_list(baseline.oblist, baseline.fxlist, baseline.weaplist,
                            delta.oblist, removed_entity_ids);
    apply_delta_entity_list(baseline.fxlist, baseline.oblist, baseline.weaplist,
                            delta.fxlist, removed_entity_ids);
    apply_delta_entity_list(baseline.weaplist, baseline.oblist, baseline.fxlist,
                            delta.weaplist, removed_entity_ids);
    reorder_entity_snapshots(baseline.oblist, delta.oblist);
    reorder_entity_snapshots(baseline.fxlist, delta.fxlist);
    reorder_entity_snapshots(baseline.weaplist, delta.weaplist);
    baseline.removed_entity_ids.clear();
}

SimEventBatch drain_sim_events(SimEventLog& log)
{
    SimEventBatch batch;
    batch.sequence = log.current_tick_;
    batch.events = log.drain();
    return batch;
}

std::uint32_t compute_snapshot_hash(const WorldSnapshot& snapshot)
{
    return compute_snapshot_hash_impl(snapshot);
}

} // namespace og::sim
