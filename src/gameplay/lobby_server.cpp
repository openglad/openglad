#include <openglad/gameplay/lobby_server.h>

#include <algorithm>
#include <format>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{

constexpr std::string_view kDefaultCampaignId = "org.openglad.gladiator";
constexpr std::int16_t kDefaultScenarioId = 1;
constexpr std::int16_t kDefaultDifficulty = 1;
constexpr std::int16_t kDefaultAlliedMode = 1;
constexpr std::int16_t kSharedAlliedGameplayTeam = 0;
constexpr std::size_t kMaxLobbyTeamSize = 24;

og::sim::LobbySettings make_default_lobby_settings()
{
    og::sim::LobbySettings settings;
    settings.campaign_id = std::string(kDefaultCampaignId);
    settings.scenario_id = kDefaultScenarioId;
    settings.difficulty = kDefaultDifficulty;
    settings.allied_mode = kDefaultAlliedMode;
    return settings;
}

og::sim::LobbySettings sanitize_settings(const og::sim::LobbySettings& requested,
                                         const og::sim::LobbySettings& fallback)
{
    og::sim::LobbySettings sanitized = requested;
    if (sanitized.campaign_id.empty())
        sanitized.campaign_id = fallback.campaign_id;
    if (sanitized.scenario_id <= 0)
        sanitized.scenario_id = fallback.scenario_id;
    if (sanitized.allied_mode != 0 && sanitized.allied_mode != 1)
        sanitized.allied_mode = fallback.allied_mode;
    if (sanitized.ctf_team_count > 0)
    {
        sanitized.ctf_team_count =
            std::clamp<std::int16_t>(sanitized.ctf_team_count, 2, 4);
    }
    else
    {
        sanitized.ctf_team_count = 0; // Auto: every team the map authors
    }
    sanitized.ctf_capture_limit =
        std::clamp<std::int16_t>(sanitized.ctf_capture_limit, 0, 50);
    if (sanitized.ctf_respawn_ticks != 0)
    {
        sanitized.ctf_respawn_ticks =
            std::clamp<std::int16_t>(sanitized.ctf_respawn_ticks, 12, 1200);
    }
    return sanitized;
}

std::vector<og::sim::LobbyCharacterSlot> sanitize_character_slots(
    const std::vector<og::sim::LobbyCharacterSlot>& slots,
    std::int16_t team)
{
    std::vector<og::sim::LobbyCharacterSlot> sanitized;
    sanitized.reserve(
        std::min<std::size_t>(slots.size(), kMaxLobbyTeamSize));

    std::unordered_set<std::uint8_t> seen_slot_indices;
    seen_slot_indices.reserve(
        std::min<std::size_t>(slots.size(), kMaxLobbyTeamSize));

    for (const auto& slot : slots)
    {
        if (sanitized.size() >= kMaxLobbyTeamSize)
            break;
        if (!seen_slot_indices.insert(slot.slot_index).second)
            continue;

        og::sim::LobbyCharacterSlot next = slot;
        next.character.teamnum = team;
        sanitized.push_back(std::move(next));
    }

    return sanitized;
}

std::int16_t gameplay_team_for_mode(std::int16_t allied_mode,
                                    std::int16_t team) noexcept
{
    return allied_mode != 0 ? kSharedAlliedGameplayTeam : team;
}

std::string default_player_name(std::size_t ordinal)
{
    return std::format("Player {}", ordinal);
}

void insert_peer_id_sorted(std::vector<og::sim::PeerId>& peers,
                           og::sim::PeerId peer_id)
{
    const auto it = std::lower_bound(peers.begin(), peers.end(), peer_id);
    if (it == peers.end() || *it != peer_id)
        peers.insert(it, peer_id);
}

void erase_peer_id_sorted(std::vector<og::sim::PeerId>& peers,
                          og::sim::PeerId peer_id)
{
    const auto it = std::lower_bound(peers.begin(), peers.end(), peer_id);
    if (it != peers.end() && *it == peer_id)
        peers.erase(it);
}

bool lobby_messages_include_peer(
    const std::vector<std::pair<og::sim::PeerId, og::sim::LobbyMessage>>& messages,
    og::sim::PeerId peer_id)
{
    return std::any_of(messages.begin(), messages.end(),
                       [peer_id](const auto& message) {
                           return message.first == peer_id;
                       });
}

std::vector<std::pair<og::sim::PeerId, og::sim::LobbyMessage>>
poll_lobby_messages(og::sim::ITransport& transport)
{
    std::vector<std::pair<og::sim::PeerId, og::sim::LobbyMessage>> messages;

    if (transport.supports_typed_messages())
    {
        for (const auto& typed_message : transport.poll_typed())
        {
            if (typed_message.kind == og::sim::TypedReceivedMessageKind::Malformed)
            {
                throw std::runtime_error(
                    "LobbyServer received malformed transport header");
            }
            if (typed_message.kind != og::sim::TypedReceivedMessageKind::LobbyMessage ||
                !typed_message.lobby_message)
            {
                continue;
            }

            messages.emplace_back(typed_message.peer_id,
                                  *typed_message.lobby_message);
        }
        return messages;
    }

    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
        {
            throw std::runtime_error(
                "LobbyServer received malformed transport header");
        }
        if (envelope.message_type != og::sim::kLobbyMessageType)
            continue;

        const auto decoded = og::sim::deserialize_lobby_message(message.data);
        if (!decoded.has_value())
            throw std::runtime_error("LobbyServer failed to deserialize lobby message");

        messages.emplace_back(message.peer_id, *decoded);
    }

    return messages;
}

struct OrderedLobbySlot {
    std::uint8_t slot_index = 0;
    std::size_t player_order = 0;
    std::size_t slot_order = 0;
    const og::sim::LobbyCharacterSlot* slot = nullptr;
};

} // namespace

namespace og::sim {

LobbyServer::LobbyServer(ITransport& transport)
    : transport_(transport)
{
    state_.settings = make_default_lobby_settings();
}

bool LobbyServer::consume_start_game_requested() noexcept
{
    const bool requested = start_game_requested_;
    start_game_requested_ = false;
    return requested;
}

void LobbyServer::connect_client(PeerId peer_id)
{
    erase_peer_id_sorted(pending_transport_disconnects_, peer_id);
    const auto [it, inserted] = peers_.emplace(
        peer_id, ConnectedPeerState{.connection_order = next_connection_order_++});
    if (!inserted)
        it->second.connection_order = it->second.connection_order == 0
            ? next_connection_order_++
            : it->second.connection_order;
    if (!host_peer_id_.has_value())
        host_peer_id_ = peer_id;

    send_state(peer_id);
}

void LobbyServer::disconnect_client(PeerId peer_id)
{
    erase_peer_id_sorted(connected_transport_peers_, peer_id);
    erase_peer_id_sorted(pending_transport_disconnects_, peer_id);
    const auto peer_it = peers_.find(peer_id);
    if (peer_it == peers_.end())
    {
        transport_.disconnect(peer_id);
        return;
    }

    const LobbyState previous_state = state_;
    const bool had_player = peer_it->second.player.has_value();
    const bool was_host = host_peer_id_.has_value() && *host_peer_id_ == peer_id;
    peers_.erase(peer_it);
    if (was_host)
        reassign_host_peer();

    if (had_player || was_host)
    {
        rebuild_state();
        if (state_ != previous_state)
            broadcast_state();
    }

    transport_.disconnect(peer_id);
}

bool LobbyServer::is_team_available(std::int16_t team,
                                    PeerId peer_id) const noexcept
{
    if (team < 0 || team >= MAX_PLAYERS)
        return false;

    for (const auto& [other_peer_id, peer] : peers_)
    {
        if (other_peer_id == peer_id || !peer.player.has_value())
            continue;
        if (peer.player->team == team)
            return false;
    }

    return true;
}

std::int16_t LobbyServer::resolve_team(
    PeerId peer_id,
    std::int16_t requested_team,
    std::optional<std::int16_t> current_team) const noexcept
{
    if (is_team_available(requested_team, peer_id))
        return requested_team;
    if (current_team.has_value() && is_team_available(*current_team, peer_id))
        return *current_team;

    for (std::int16_t candidate = 0; candidate < MAX_PLAYERS; ++candidate)
    {
        if (is_team_available(candidate, peer_id))
            return candidate;
    }

    return current_team.value_or(static_cast<std::int16_t>(-1));
}

std::size_t LobbyServer::remaining_team_capacity(PeerId peer_id) const noexcept
{
    std::size_t used_slots = 0;
    for (const auto& [other_peer_id, peer] : peers_)
    {
        if (other_peer_id == peer_id || !peer.player.has_value())
            continue;
        used_slots += peer.player->character_slots.size();
    }

    return used_slots >= kMaxLobbyTeamSize ? 0 : kMaxLobbyTeamSize - used_slots;
}

void LobbyServer::reassign_host_peer()
{
    host_peer_id_ = std::nullopt;
    for (const auto& [peer_id, peer] : peers_)
    {
        if (!host_peer_id_.has_value())
        {
            host_peer_id_ = peer_id;
            continue;
        }

        const auto current_host = peers_.find(*host_peer_id_);
        if (current_host != peers_.end() &&
            peer.connection_order < current_host->second.connection_order)
        {
            host_peer_id_ = peer_id;
        }
    }
}

void LobbyServer::rebuild_state()
{
    std::vector<std::pair<PeerId, ConnectedPeerState*>> ordered_peers;
    ordered_peers.reserve(peers_.size());
    for (auto& [peer_id, peer] : peers_)
    {
        if (peer.player.has_value())
            ordered_peers.emplace_back(peer_id, &peer);
    }

    std::sort(ordered_peers.begin(), ordered_peers.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.second->connection_order < rhs.second->connection_order;
              });

    state_.players.clear();
    state_.host_player_id = 0xff;
    state_.players.reserve(ordered_peers.size());
    for (std::size_t index = 0; index < ordered_peers.size(); ++index)
    {
        const PeerId peer_id = ordered_peers[index].first;
        LobbyPlayer& player = *ordered_peers[index].second->player;
        player.player_index = static_cast<std::uint8_t>(index);
        player.is_host = host_peer_id_.has_value() && *host_peer_id_ == peer_id;
        if (player.is_host)
            state_.host_player_id = player.player_index;
        if (player.name.empty())
            player.name = default_player_name(index + 1);
        state_.players.push_back(player);
    }
}

void LobbyServer::send_state(PeerId peer_id) const
{
    transport_.send_lobby_state(peer_id, std::make_shared<LobbyState>(state_));
}

void LobbyServer::broadcast_state() const
{
    const auto shared_state = std::make_shared<LobbyState>(state_);
    for (const auto& [peer_id, peer] : peers_)
    {
        (void)peer;
        transport_.send_lobby_state(peer_id, shared_state);
    }
}

void LobbyServer::broadcast_start_game(std::uint8_t player_index) const
{
    LobbyMessage message;
    message.payload = LobbyStartGameMessage{.player_index = player_index};
    const auto shared_message = std::make_shared<LobbyMessage>(std::move(message));
    for (const auto& [peer_id, peer] : peers_)
    {
        (void)peer;
        transport_.send_lobby_message(peer_id, shared_message);
    }
}

void LobbyServer::process_lobby_message(PeerId peer_id, const LobbyMessage& message)
{
    if (lobby_locked_)
        return;

    const auto peer_it = peers_.find(peer_id);
    if (peer_it == peers_.end())
        return;

    const LobbyState previous_state = state_;
    bool rebuild_needed = false;
    std::optional<std::uint8_t> accepted_start_player_index = std::nullopt;

    switch (message.kind())
    {
    case LobbyMessageKind::Join:
    {
        const auto& join = std::get<LobbyJoinMessage>(message.payload);
        if (!peer_it->second.player.has_value() &&
            state_.players.size() >= static_cast<std::size_t>(MAX_PLAYERS))
        {
            send_state(peer_id);
            return;
        }

        const std::optional<std::int16_t> current_team =
            peer_it->second.player.has_value()
                ? std::optional<std::int16_t>(peer_it->second.player->team)
                : std::nullopt;
        const std::int16_t team =
            resolve_team(peer_id, join.player.team, current_team);
        if (team < 0)
        {
            send_state(peer_id);
            return;
        }

        LobbyPlayer player = join.player;
        if (player.name.empty())
        {
            if (peer_it->second.player.has_value() &&
                !peer_it->second.player->name.empty())
            {
                player.name = peer_it->second.player->name;
            }
            else
            {
                player.name = default_player_name(state_.players.size() + 1);
            }
        }

        player.team = team;
        player.player_index = 0xff;
        player.is_host = false;
        player.character_slots =
            sanitize_character_slots(join.player.character_slots, team);
        const std::size_t capacity = remaining_team_capacity(peer_id);
        if (player.character_slots.size() > capacity)
            player.character_slots.resize(capacity);
        peer_it->second.player = std::move(player);
        rebuild_needed = true;
        break;
    }

    case LobbyMessageKind::Leave:
        if (peer_it->second.player.has_value())
        {
            peer_it->second.player.reset();
            rebuild_needed = true;
        }
        break;

    case LobbyMessageKind::Ready:
        if (peer_it->second.player.has_value())
        {
            const auto& ready = std::get<LobbyReadyMessage>(message.payload);
            peer_it->second.player->ready = ready.ready;
            rebuild_needed = true;
        }
        break;

    case LobbyMessageKind::TeamChange:
        if (peer_it->second.player.has_value())
        {
            const auto& team_change = std::get<LobbyTeamChangeMessage>(message.payload);
            const std::int16_t team = resolve_team(
                peer_id, team_change.team, peer_it->second.player->team);
            if (team >= 0)
            {
                peer_it->second.player->team = team;
                for (auto& slot : peer_it->second.player->character_slots)
                    slot.character.teamnum = team;
                rebuild_needed = true;
            }
        }
        break;

    case LobbyMessageKind::StartGame:
        if (host_peer_id_.has_value() && *host_peer_id_ == peer_id)
        {
            lobby_locked_ = true;
            start_game_requested_ = true;
            accepted_start_player_index = peer_it->second.player.has_value()
                ? std::optional<std::uint8_t>(peer_it->second.player->player_index)
                : std::optional<std::uint8_t>(0xffu);
        }
        break;

    case LobbyMessageKind::SettingsChange:
        if (host_peer_id_.has_value() && *host_peer_id_ == peer_id)
        {
            const auto& settings_change =
                std::get<LobbySettingsChangeMessage>(message.payload);
            state_.settings =
                sanitize_settings(settings_change.settings, state_.settings);
        }
        break;
    }

    if (rebuild_needed)
        rebuild_state();

    if (state_ != previous_state)
        broadcast_state();
    if (accepted_start_player_index.has_value())
        broadcast_start_game(*accepted_start_player_index);
}

void LobbyServer::poll_incoming_messages()
{
    apply_transport_disconnects();

    const auto messages = poll_lobby_messages(transport_);
    synchronize_transport_peers(messages);
    for (const auto& [peer_id, message] : messages)
        process_lobby_message(peer_id, message);
    apply_transport_disconnects();
}

void LobbyServer::synchronize_transport_peers(
    const std::vector<std::pair<PeerId, LobbyMessage>>& messages)
{
    const std::vector<PeerId> current_peers = transport_.connected_peers();

    std::vector<PeerId> added_peers;
    std::vector<PeerId> removed_peers;
    std::set_difference(current_peers.begin(), current_peers.end(),
                        connected_transport_peers_.begin(),
                        connected_transport_peers_.end(),
                        std::back_inserter(added_peers));
    std::set_difference(connected_transport_peers_.begin(),
                        connected_transport_peers_.end(),
                        current_peers.begin(), current_peers.end(),
                        std::back_inserter(removed_peers));

    for (const PeerId peer_id : added_peers)
    {
        if (peers_.find(peer_id) == peers_.end())
            connect_client(peer_id);
    }

    connected_transport_peers_ = current_peers;

    for (const PeerId peer_id : removed_peers)
    {
        if (lobby_messages_include_peer(messages, peer_id))
            insert_peer_id_sorted(pending_transport_disconnects_, peer_id);
        else
            disconnect_client(peer_id);
    }
}

void LobbyServer::apply_transport_disconnects()
{
    std::vector<PeerId> disconnected_peers = std::move(pending_transport_disconnects_);
    pending_transport_disconnects_.clear();
    for (const PeerId peer_id : disconnected_peers)
        disconnect_client(peer_id);
}

LobbySaveDataEquivalent LobbyServer::build_save_data_equivalent() const
{
    LobbySaveDataEquivalent equivalent;
    equivalent.numplayers = static_cast<unsigned char>(
        std::min<std::size_t>(state_.players.size(), static_cast<std::size_t>(MAX_PLAYERS)));
    equivalent.allied_mode = state_.settings.allied_mode;
    equivalent.ctf_team_count = state_.settings.ctf_team_count;
    equivalent.ctf_capture_limit = state_.settings.ctf_capture_limit;
    equivalent.ctf_respawn_ticks = state_.settings.ctf_respawn_ticks;
    equivalent.current_campaign = state_.settings.campaign_id.empty()
        ? std::string(kDefaultCampaignId)
        : state_.settings.campaign_id;
    equivalent.scen_num = state_.settings.scenario_id > 0
        ? state_.settings.scenario_id
        : kDefaultScenarioId;

    std::vector<OrderedLobbySlot> ordered_slots;
    for (std::size_t player_index = 0; player_index < state_.players.size();
         ++player_index)
    {
        const LobbyPlayer& player = state_.players[player_index];
        for (std::size_t slot_order = 0; slot_order < player.character_slots.size();
             ++slot_order)
        {
            ordered_slots.push_back(OrderedLobbySlot{
                .slot_index = player.character_slots[slot_order].slot_index,
                .player_order = player_index,
                .slot_order = slot_order,
                .slot = &player.character_slots[slot_order],
            });
        }
    }

    if (ordered_slots.size() > kMaxLobbyTeamSize)
    {
        throw std::runtime_error(
            "LobbyServer exceeded the SaveData-equivalent 24-slot team limit");
    }

    std::sort(ordered_slots.begin(), ordered_slots.end(),
              [](const OrderedLobbySlot& lhs, const OrderedLobbySlot& rhs) {
                  if (lhs.slot_index != rhs.slot_index)
                      return lhs.slot_index < rhs.slot_index;
                  if (lhs.player_order != rhs.player_order)
                      return lhs.player_order < rhs.player_order;
                  return lhs.slot_order < rhs.slot_order;
              });

    const bool slots_are_dense = std::all_of(
        ordered_slots.begin(), ordered_slots.end(),
        [&ordered_slots](const OrderedLobbySlot& slot) {
            return static_cast<std::size_t>(slot.slot_index) ==
                static_cast<std::size_t>(&slot - ordered_slots.data());
        });

    if (slots_are_dense)
    {
        for (const OrderedLobbySlot& slot : ordered_slots)
        {
            LobbyCharacterSlot gameplay_slot = *slot.slot;
            gameplay_slot.character.teamnum = gameplay_team_for_mode(
                equivalent.allied_mode,
                gameplay_slot.character.teamnum);
            gameplay_slot.owner_player_index =
                state_.players[slot.player_order].player_index;
            gameplay_slot.owner_save_slot = slot.slot_index;
            equivalent.team_list.push_back(std::move(gameplay_slot));
        }
        return equivalent;
    }

    for (std::size_t index = 0; index < ordered_slots.size(); ++index)
    {
        LobbyCharacterSlot compacted = *ordered_slots[index].slot;
        compacted.slot_index = static_cast<std::uint8_t>(index);
        compacted.character.teamnum = gameplay_team_for_mode(
            equivalent.allied_mode,
            compacted.character.teamnum);
        compacted.owner_player_index =
            state_.players[ordered_slots[index].player_order].player_index;
        compacted.owner_save_slot = ordered_slots[index].slot_index;
        equivalent.team_list.push_back(std::move(compacted));
    }

    return equivalent;
}

std::vector<LobbyPlayerBinding> LobbyServer::build_player_bindings() const
{
    std::vector<LobbyPlayerBinding> bindings;
    bindings.reserve(peers_.size());

    for (const auto& [peer_id, peer] : peers_)
    {
        if (!peer.player.has_value())
            continue;

        bindings.push_back(LobbyPlayerBinding{
            .peer_id = peer_id,
            .player_index = peer.player->player_index,
            .team = gameplay_team_for_mode(state_.settings.allied_mode,
                                           peer.player->team),
        });
    }

    std::sort(bindings.begin(), bindings.end(),
              [](const LobbyPlayerBinding& lhs, const LobbyPlayerBinding& rhs) {
                  if (lhs.player_index != rhs.player_index)
                      return lhs.player_index < rhs.player_index;
                  return lhs.peer_id < rhs.peer_id;
              });

    return bindings;
}

} // namespace og::sim
