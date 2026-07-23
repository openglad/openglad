#include <openglad/gameplay/lobby_server.h>

#include <openglad/core/ctf_constants.h>
#include <openglad/core/tower_constants.h>

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

using og::kDefaultCampaignId;
constexpr std::int16_t kDefaultScenarioId = 1;
constexpr std::int16_t kDefaultDifficulty = 1;
constexpr std::int16_t kDefaultAlliedMode = 1;
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
                                         const og::sim::LobbySettings& fallback,
                                         bool local_session)
{
    og::sim::LobbySettings sanitized = requested;
    if (sanitized.campaign_id.empty())
        sanitized.campaign_id = fallback.campaign_id;
    if (!local_session && sanitized.campaign_id == og::kTowerCampaignId)
    {
        // The Endless Tower is local-only (tower-triple §5.9): its run state
        // lives in one player's save0 and its generated floors in one
        // player's user dir. Honest clients never offer it on a networked
        // shelf and the picker vetoes it at GO; this is the crafted-client
        // backstop. Rejecting to the fallback PAIR keeps the scenario id
        // coherent with the restored campaign, and the existing
        // settings-refresh flow reports the result to every peer.
        //
        // Local sessions are exempt: the solo picker round-trips its OWN
        // settings through this same server (LocalPickerLobbyClient), and an
        // unconditional rejection silently reverted a just-picked tower back
        // to the fallback pair while the tower stayed mounted — a split-brain
        // ghost session (display on the Gate, authority on the fallback
        // level, every keyframe rejected).
        sanitized.campaign_id = fallback.campaign_id;
        sanitized.scenario_id = fallback.scenario_id;
    }
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
    sanitized.ctf_authored_team_mask = static_cast<std::uint8_t>(
        sanitized.ctf_authored_team_mask & og::sim::kAllLobbyTeamMask);
    sanitized.ctf_capture_limit =
        std::clamp<std::int16_t>(sanitized.ctf_capture_limit, 0, 50);
    if (sanitized.ctf_respawn_ticks != 0)
    {
        sanitized.ctf_respawn_ticks =
            std::clamp<std::int16_t>(sanitized.ctf_respawn_ticks, 12, 1200);
    }
    if (sanitized.ctf_strip_scenario_troops != 0 &&
        sanitized.ctf_strip_scenario_troops != 1)
    {
        sanitized.ctf_strip_scenario_troops = fallback.ctf_strip_scenario_troops;
    }
    if (sanitized.respawn_mode < 0 || sanitized.respawn_mode > 2)
        sanitized.respawn_mode = fallback.respawn_mode;
    if (sanitized.generator_rate != 0)
    {
        sanitized.generator_rate =
            std::clamp<std::int16_t>(sanitized.generator_rate, 25, 400);
    }
    if (sanitized.keep_fallen_heroes != 0 && sanitized.keep_fallen_heroes != 1)
        sanitized.keep_fallen_heroes = fallback.keep_fallen_heroes;
    if (sanitized.cross_control != 0 && sanitized.cross_control != 1)
        sanitized.cross_control = fallback.cross_control;
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
        if (next.character.teamnum < 0 || next.character.teamnum >= MAX_PLAYERS)
            next.character.teamnum = team;
        sanitized.push_back(std::move(next));
    }

    return sanitized;
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

struct LobbyPollResult {
    std::vector<std::pair<og::sim::PeerId, og::sim::LobbyMessage>> messages;
    std::vector<og::sim::PeerId> malformed_peer_ids;
};

LobbyPollResult
poll_lobby_messages(og::sim::ITransport& transport)
{
    LobbyPollResult result;

    if (transport.supports_typed_messages())
    {
        for (const auto& typed_message : transport.poll_typed())
        {
            if (typed_message.kind == og::sim::TypedReceivedMessageKind::Malformed)
            {
                if (typed_message.peer_id != 0)
                    result.malformed_peer_ids.push_back(typed_message.peer_id);
                continue;
            }
            if (typed_message.kind != og::sim::TypedReceivedMessageKind::LobbyMessage ||
                !typed_message.lobby_message)
            {
                continue;
            }

            result.messages.emplace_back(typed_message.peer_id,
                                         *typed_message.lobby_message);
        }
        return result;
    }

    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
        {
            if (message.peer_id != 0)
                result.malformed_peer_ids.push_back(message.peer_id);
            continue;
        }
        if (envelope.message_type != og::sim::kLobbyMessageType)
            continue;

        const auto decoded = og::sim::deserialize_lobby_message(message.data);
        if (!decoded.has_value())
        {
            if (message.peer_id != 0)
                result.malformed_peer_ids.push_back(message.peer_id);
            continue;
        }

        result.messages.emplace_back(message.peer_id, *decoded);
    }

    std::sort(result.malformed_peer_ids.begin(),
              result.malformed_peer_ids.end());
    result.malformed_peer_ids.erase(
        std::unique(result.malformed_peer_ids.begin(),
                    result.malformed_peer_ids.end()),
        result.malformed_peer_ids.end());
    return result;
}

struct OrderedLobbySlot {
    std::uint8_t slot_index = 0;
    std::size_t player_order = 0;
    std::size_t slot_order = 0;
    const og::sim::LobbyCharacterSlot* slot = nullptr;
};

// A re-sent join whose seats match the peer's stored seats content-for-content
// preserves ready (kills the go_menu re-send deadlock). Compares seat count,
// per-seat team, and per-seat character-slot content. Two slots match on
// slot_index, deploy flag, and character data, but NOT on the assembly-only
// owner_player_index/owner_save_slot: the in-process loopback transport shares
// LobbyMessage objects with no serialize/normalize pass, so "wire-defaulted
// 0xff on both sides" is not a safe assumption on the host path. Player
// name/company are excluded (a rename never un-readies anyone);
// ready/is_host/player_index and server-issued seat_id/machine_id are
// non-content. The deploy flag IS content — a changed deploy selection clears
// ready (that machine's roster really changed, §4.2).
bool lobby_seats_content_identical(
    const std::vector<og::sim::LobbyPlayer>& previous,
    const std::vector<og::sim::LobbyPlayer>& next) noexcept
{
    return og::sim::lobby_join_seats_content_identical(previous, next);
}

} // namespace

namespace og::sim {

std::int16_t shared_allied_gameplay_team(const LobbyState& state) noexcept
{
    // Prefer the first seat that can actually control one of its deployed
    // fighters. This keeps an empty/fully-benched host a spectator instead of
    // forcing every Together seat onto the host's empty color.
    for (const LobbyPlayer& player : state.players)
    {
        if (player.team < 0 || player.team >= MAX_PLAYERS)
            continue;
        if (std::any_of(
                player.character_slots.begin(), player.character_slots.end(),
                [&player](const LobbyCharacterSlot& slot) {
                    return slot.deployed &&
                        slot.character.teamnum == player.team;
                }))
        {
            return player.team;
        }
    }

    // A stale seat preference may not match any of its roster colors. A
    // deployed fighter is still a better shared control team than an empty one.
    for (const LobbyPlayer& player : state.players)
    {
        for (const LobbyCharacterSlot& slot : player.character_slots)
        {
            if (slot.deployed && slot.character.teamnum >= 0 &&
                slot.character.teamnum < MAX_PLAYERS)
            {
                return slot.character.teamnum;
            }
        }
    }

    if (!state.players.empty() && state.players.front().team >= 0 &&
        state.players.front().team < MAX_PLAYERS)
    {
        return state.players.front().team;
    }
    return 0;
}

LobbyServer::LobbyServer(ITransport& transport, bool local_session)
    : transport_(transport)
    , local_session_(local_session)
{
    state_.settings = make_default_lobby_settings();
}

bool LobbyServer::consume_start_game_requested() noexcept
{
    const bool requested = start_game_requested_;
    start_game_requested_ = false;
    return requested;
}

void LobbyServer::unlock_for_new_round() noexcept
{
    lobby_locked_ = false;
    start_game_requested_ = false;
    // §4.3: every machine re-readies each round. Clear the stored seats AND the
    // published state so the next content-identical join preserves the zeroed
    // ready instead of re-arming a stale one.
    for (auto& [peer_id, peer] : peers_)
    {
        (void)peer_id;
        for (LobbyPlayer& seat : peer.seats)
            seat.ready = false;
    }
    for (LobbyPlayer& player : state_.players)
        player.ready = false;

    // Ready is authoritative state, not a local host-side cache. In
    // particular, a joiner that was ready for the previous level must see the
    // new-round false before its first READY click can produce a real change.
    broadcast_state();

    // A joiner may leave gameplay and re-declare its advanced roster before
    // the host has returned far enough to unlock this server. The lock still
    // prevents that declaration from affecting the running level; apply the
    // latest declaration now that the next-round lobby is open.
    std::vector<std::pair<PeerId, LobbyMessage>> pending_joins =
        std::move(pending_locked_joins_);
    pending_locked_joins_.clear();
    for (const auto& [peer_id, message] : pending_joins)
        process_lobby_message(peer_id, message);
}

bool LobbyServer::start_allowed(PeerId requester,
                                StartDenialReason& reason) const noexcept
{
    reason = StartDenialReason::None;

    // Rule 1: local/solo/split-screen lobbies GO exactly as today — the
    // in-process LobbyServer that echoes the solo picker's own settings is
    // never ready-gated.
    if (local_session_)
        return true;

    // Rule 2: the requester must be the elected host peer.
    if (!host_peer_id_.has_value() || *host_peer_id_ != requester)
    {
        reason = StartDenialReason::NotHost;
        return false;
    }

    // Rules 3 + 4 share one pass: count deployed slots across ALL machines
    // (rule 4) while requiring every NON-host joined peer to be ready (rule 3).
    std::size_t deployed_total = 0;
    for (const auto& [peer_id, peer] : peers_)
    {
        for (const LobbyPlayer& seat : peer.seats)
        {
            for (const LobbyCharacterSlot& slot : seat.character_slots)
            {
                if (slot.deployed)
                    ++deployed_total;
            }
        }

        if (peer_id == *host_peer_id_)
            continue;
        if (peer.seats.empty())
            continue; // Connected but not joined: nothing to ready.
        for (const LobbyPlayer& seat : peer.seats)
        {
            if (!seat.ready)
            {
                // Rule 3 outranks rule 4 (§4.3 order): report unready first.
                reason = StartDenialReason::MachinesNotReady;
                return false;
            }
        }
    }

    // Rule 4: at least one deployed character across all machines (global
    // minimum regardless of cross-control; per-machine minimums are gone).
    if (deployed_total == 0)
    {
        reason = StartDenialReason::NoDeployedCharacters;
        return false;
    }

    return true;
}

void LobbyServer::connect_client(PeerId peer_id)
{
    erase_peer_id_sorted(pending_transport_disconnects_, peer_id);
    const auto [it, inserted] = peers_.try_emplace(peer_id);
    if (!inserted)
    {
        it->second.connection_order = it->second.connection_order == 0
            ? next_connection_order_++
            : it->second.connection_order;
        if (it->second.machine_id == kInvalidLobbyMachineId)
            it->second.machine_id = allocate_machine_id();
    }
    else
    {
        it->second.connection_order = next_connection_order_++;
        it->second.machine_id = allocate_machine_id();
    }
    if (!host_peer_id_.has_value())
        host_peer_id_ = peer_id;

    send_state(peer_id);
}

void LobbyServer::disconnect_client(PeerId peer_id)
{
    erase_peer_id_sorted(connected_transport_peers_, peer_id);
    erase_peer_id_sorted(pending_transport_disconnects_, peer_id);
    std::erase_if(pending_locked_joins_,
                  [peer_id](const auto& pending) {
                      return pending.first == peer_id;
                  });
    const auto peer_it = peers_.find(peer_id);
    if (peer_it == peers_.end())
    {
        transport_.disconnect(peer_id);
        return;
    }

    const LobbyState previous_state = state_;
    const bool had_player = !peer_it->second.seats.empty();
    const bool was_host = host_peer_id_.has_value() && *host_peer_id_ == peer_id;
    peers_.erase(peer_it);
    if (was_host)
        reassign_host_peer();

    if (had_player || was_host)
    {
        rebuild_state();
        // Host authority is recipient-specific and can change even when both
        // the departing and promoted peers have zero active seats, leaving
        // the canonical player list byte-for-byte identical.
        if (was_host || state_ != previous_state)
            broadcast_state();
    }

    transport_.disconnect(peer_id);
}

std::uint8_t LobbyServer::effective_team_mask() const noexcept
{
    return lobby_effective_team_mask(state_.settings);
}

bool LobbyServer::is_team_available(std::int16_t team,
                                    PeerId peer_id) const noexcept
{
    if (!lobby_team_is_selectable(state_.settings, team))
        return false;

    // Explicit assignments are shareable in every mode: any in-range team is
    // open to every seat, including another seat of this peer. Keep the
    // compatibility query here so older allied/CTF-oriented callers retain
    // the same public helper without controlling the new assignment rule.
    if (lobby_teams_shareable(state_.settings))
        return true;

    for (const auto& [other_peer_id, peer] : peers_)
    {
        if (other_peer_id == peer_id)
            continue;
        for (const LobbyPlayer& seat : peer.seats)
        {
            if (seat.team == team)
                return false;
        }
    }

    return true;
}

std::int16_t LobbyServer::resolve_team(
    PeerId peer_id,
    std::int16_t requested_team,
    std::optional<std::int16_t> current_team) const noexcept
{
    return resolve_seat_team(peer_id, requested_team, current_team, {});
}

std::int16_t LobbyServer::resolve_seat_team(
    PeerId peer_id,
    std::int16_t requested_team,
    std::optional<std::int16_t> current_team,
    const std::vector<std::int16_t>& sibling_teams) const noexcept
{
    // The argument remains in the private helper signature to keep the
    // lowered-CTF-count and legacy call sites straightforward. Sibling teams
    // are no longer exclusions: explicit assignments deliberately permit
    // multiple seats of one machine to share a team.
    (void)sibling_teams;
    const auto seat_available = [&](std::int16_t team) noexcept {
        return is_team_available(team, peer_id);
    };

    if (seat_available(requested_team))
        return requested_team;
    if (current_team.has_value() && seat_available(*current_team))
        return *current_team;

    for (std::int16_t candidate = 0; candidate < SCORE_TEAM_COUNT; ++candidate)
    {
        if (seat_available(candidate))
            return candidate;
    }

    return current_team.value_or(static_cast<std::int16_t>(-1));
}

std::size_t LobbyServer::remaining_team_capacity(PeerId peer_id) const noexcept
{
    // §4.2: only DEPLOYED slots consume the combined 24-slot match capacity —
    // benched slots always replicate for display and never crowd anyone out.
    std::size_t used_slots = 0;
    for (const auto& [other_peer_id, peer] : peers_)
    {
        if (other_peer_id == peer_id)
            continue;
        for (const LobbyPlayer& seat : peer.seats)
        {
            for (const LobbyCharacterSlot& slot : seat.character_slots)
            {
                if (slot.deployed)
                    ++used_slots;
            }
        }
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
        if (!peer.seats.empty())
            ordered_peers.emplace_back(peer_id, &peer);
    }

    std::sort(ordered_peers.begin(), ordered_peers.end(),
              [](const auto& lhs, const auto& rhs) {
                  return lhs.second->connection_order < rhs.second->connection_order;
              });

    // Flatten (connection_order, seat order): global player indices are
    // sequential across the flattened seat list.
    state_.players.clear();
    state_.host_player_id = 0xff;
    std::size_t index = 0;
    for (auto& [peer_id, peer] : ordered_peers)
    {
        for (std::size_t seat_order = 0; seat_order < peer->seats.size();
             ++seat_order)
        {
            LobbyPlayer& player = peer->seats[seat_order];
            player.player_index = static_cast<std::uint8_t>(index);
            player.is_host = seat_order == 0 &&
                host_peer_id_.has_value() && *host_peer_id_ == peer_id;
            if (player.is_host)
                state_.host_player_id = player.player_index;
            if (player.name.empty())
                player.name = default_player_name(index + 1);
            state_.players.push_back(player);
            ++index;
        }
    }
}

LobbySeatId LobbyServer::allocate_seat_id()
{
    if (next_seat_id_ == kInvalidLobbySeatId)
        throw std::overflow_error("lobby seat id space exhausted");
    return next_seat_id_++;
}

LobbyMachineId LobbyServer::allocate_machine_id()
{
    if (next_machine_id_ == kInvalidLobbyMachineId)
        throw std::overflow_error("lobby machine id space exhausted");
    return next_machine_id_++;
}

void LobbyServer::send_state(PeerId peer_id) const
{
    auto peer_state = std::make_shared<LobbyState>(state_);
    const auto peer_it = peers_.find(peer_id);
    if (peer_it != peers_.end())
    {
        peer_state->local_seat_ids.reserve(peer_it->second.seats.size());
        for (const LobbyPlayer& seat : peer_it->second.seats)
            peer_state->local_seat_ids.push_back(seat.seat_id);
        peer_state->last_join_request_id =
            peer_it->second.last_join_request_id;
        peer_state->local_peer_is_host =
            host_peer_id_.has_value() && *host_peer_id_ == peer_id;
    }
    transport_.send_lobby_state(peer_id, std::move(peer_state));
}

void LobbyServer::broadcast_state() const
{
    for (const auto& [peer_id, peer] : peers_)
    {
        auto peer_state = std::make_shared<LobbyState>(state_);
        peer_state->local_seat_ids.reserve(peer.seats.size());
        for (const LobbyPlayer& seat : peer.seats)
            peer_state->local_seat_ids.push_back(seat.seat_id);
        peer_state->last_join_request_id = peer.last_join_request_id;
        peer_state->local_peer_is_host =
            host_peer_id_.has_value() && *host_peer_id_ == peer_id;
        transport_.send_lobby_state(peer_id, std::move(peer_state));
    }
}

void LobbyServer::broadcast_start_game(std::uint8_t player_index,
                                       std::uint32_t request_id) const
{
    LobbyMessage message;
    message.payload = LobbyStartGameMessage{
        .player_index = player_index,
        .request_id = request_id,
    };
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
    {
        if (message.kind() == LobbyMessageKind::Join &&
            peers_.contains(peer_id) &&
            std::get<LobbyJoinMessage>(message.payload).resume_after_level)
        {
            const auto pending_it = std::find_if(
                pending_locked_joins_.begin(),
                pending_locked_joins_.end(),
                [peer_id](const auto& pending) {
                    return pending.first == peer_id;
                });
            if (pending_it == pending_locked_joins_.end())
                pending_locked_joins_.emplace_back(peer_id, message);
            else
                pending_it->second = message;
        }
        return;
    }

    const auto peer_it = peers_.find(peer_id);
    if (peer_it == peers_.end())
        return;

    const LobbyState previous_state = state_;
    bool rebuild_needed = false;
    bool authoritative_echo_required = false;
    bool requester_echo_required = false;
    std::optional<std::uint8_t> accepted_start_player_index = std::nullopt;
    std::uint32_t accepted_start_request_id = 0;

    switch (message.kind())
    {
    case LobbyMessageKind::Join:
    {
        const auto& join = std::get<LobbyJoinMessage>(message.payload);
        // This is the recipient-specific proof that the declaration reached
        // the reopened LobbyServer rather than being drained by the prior
        // round's GameServer. Record it before validation so every processed
        // Join outcome (including authoritative truncation/rejection) receives
        // a correlated echo. Retries deliberately reuse the same request id.
        peer_it->second.last_join_request_id = join.request_id;
        // A Join is also the resume handshake for a surviving connection.
        // Even an identical declaration needs a fresh recipient-specific
        // state echo: the peer may have missed the new-round ready=false
        // broadcast while its GameClient still owned the shared transport.
        requester_echo_required = true;

        // Requested seat list: seat 0 + extras, per-machine capped.
        std::vector<const LobbyPlayer*> requested;
        requested.push_back(&join.player);
        for (const auto& extra : join.extra_players)
        {
            if (requested.size() >= static_cast<std::size_t>(MAX_PLAYERS))
                break;
            requested.push_back(&extra);
        }

        // Global seat capacity: every OTHER peer's seats count against
        // kMaxGlobalPlayers; a re-join replaces this peer's own seats.
        std::size_t other_seats = 0;
        for (const auto& [other_peer_id, peer] : peers_)
        {
            if (other_peer_id != peer_id)
                other_seats += peer.seats.size();
        }
        const std::size_t seat_capacity = other_seats >= kMaxGlobalPlayers
            ? 0
            : kMaxGlobalPlayers - other_seats;
        if (seat_capacity == 0)
        {
            send_state(peer_id);
            return;
        }
        if (requested.size() > seat_capacity)
            requested.resize(seat_capacity);

        const std::vector<LobbyPlayer> previous_seats =
            std::move(peer_it->second.seats);
        std::vector<LobbyPlayer> new_seats;
        new_seats.reserve(requested.size());
        std::vector<std::int16_t> sibling_teams;
        std::size_t slot_capacity = remaining_team_capacity(peer_id);
        for (std::size_t seat_order = 0; seat_order < requested.size();
             ++seat_order)
        {
            const LobbyPlayer& requested_seat = *requested[seat_order];
            const std::optional<std::int16_t> current_team =
                seat_order < previous_seats.size()
                    ? std::optional<std::int16_t>(
                          previous_seats[seat_order].team)
                    : std::nullopt;
            const std::int16_t team = resolve_seat_team(
                peer_id, requested_seat.team, current_team, sibling_teams);
            if (team < 0)
            {
                // No valid in-range team exists. Seat 0: reject the whole
                // join (matching the historic full-lobby echo). Later seats:
                // truncate — the client adopts the echoed authoritative seat
                // count. Duplicate assignments never reach this path.
                if (seat_order == 0)
                {
                    peer_it->second.seats = previous_seats;
                    send_state(peer_id);
                    return;
                }
                break;
            }

            LobbyPlayer seat = requested_seat;
            if (seat.name.empty())
            {
                if (seat_order < previous_seats.size() &&
                    !previous_seats[seat_order].name.empty())
                {
                    seat.name = previous_seats[seat_order].name;
                }
                else if (seat_order > 0 && !new_seats.front().name.empty())
                {
                    seat.name = std::format("{}#{}", new_seats.front().name,
                                            seat_order);
                }
                else
                {
                    seat.name = default_player_name(state_.players.size() + 1);
                }
            }

            seat.team = team;
            seat.player_index = 0xff;
            // A re-join replaces the peer's declaration, but the seat at local
            // ordinal k remains the same command target. New ordinals receive
            // fresh tokens; never trust a client-authored token.
            if (seat_order < previous_seats.size())
            {
                seat.seat_id = previous_seats[seat_order].seat_id;
            }
            else if (seat_order == 0 && previous_seats.empty() &&
                     peer_it->second.dormant_seat_id !=
                         kInvalidLobbySeatId)
            {
                seat.seat_id = peer_it->second.dormant_seat_id;
            }
            else
            {
                seat.seat_id = allocate_seat_id();
            }
            // Machine identity is scoped to the connected peer. All of its
            // seats share the server-issued value; never trust the client's.
            seat.machine_id = peer_it->second.machine_id;
            seat.is_host = false;
            seat.character_slots = sanitize_character_slots(
                requested_seat.character_slots, team);
            // §4.2 [NET-F2] overflow convergence: the FULL roster replicates
            // for display, but only DEPLOYED slots consume the combined
            // 24-slot capacity. Deployed slots beyond the remaining budget
            // are force-BENCHED in seat/slot order in the STORED seats and
            // ride the state echo back for client reconciliation. This is
            // idempotent: a client re-sending the same optimistic flags gets
            // force-cleared to the same stored seats, so the re-send is
            // content-identical and its ready survives (no perpetual
            // ready-clearing while the client converges).
            for (LobbyCharacterSlot& slot : seat.character_slots)
            {
                if (!slot.deployed)
                    continue;
                if (slot_capacity == 0)
                    slot.deployed = false;
                else
                    --slot_capacity;
            }
            new_seats.push_back(std::move(seat));
        }

        // §4.3 ready authority: the ready bit a client sends inside a Join is
        // IGNORED. A re-sent join whose seats are content-identical to the
        // stored seats PRESERVES the machine's ready (the go_menu re-send no
        // longer deadlocks a ready lobby); any real roster/team/deploy change
        // clears it.
        const bool preserve_ready =
            lobby_seats_content_identical(previous_seats, new_seats);
        for (std::size_t seat_order = 0; seat_order < new_seats.size();
             ++seat_order)
        {
            new_seats[seat_order].ready = preserve_ready
                ? previous_seats[seat_order].ready
                : false;
        }

        peer_it->second.seats = std::move(new_seats);
        if (!peer_it->second.seats.empty())
            peer_it->second.dormant_seat_id = kInvalidLobbySeatId;
        rebuild_needed = true;
        break;
    }

    case LobbyMessageKind::Leave:
        if (!peer_it->second.seats.empty())
        {
            peer_it->second.dormant_seat_id =
                peer_it->second.seats.front().seat_id;
            peer_it->second.seats.clear();
            rebuild_needed = true;
        }
        break;

    case LobbyMessageKind::Ready:
        if (!peer_it->second.seats.empty())
        {
            const auto& ready = std::get<LobbyReadyMessage>(message.payload);
            // Ready applies to ALL seats of the sending peer (one machine
            // readies as a unit).
            for (LobbyPlayer& seat : peer_it->second.seats)
                seat.ready = ready.ready;
            rebuild_needed = true;
        }
        break;

    case LobbyMessageKind::TeamChange:
        if (!peer_it->second.seats.empty())
        {
            const auto& team_change = std::get<LobbyTeamChangeMessage>(message.payload);
            std::vector<LobbyPlayer>& seats = peer_it->second.seats;
            // Retarget only the sender-owned seat whose stable server token
            // matches. player_index is deliberately NOT used here: it is the
            // dense P#/gameplay ordinal and can change when another peer
            // leaves. A stale/foreign token receives a denial echo instead of
            // aliasing a different sibling seat after that re-index.
            std::optional<std::size_t> seat_order;
            for (std::size_t k = 0; k < seats.size(); ++k)
            {
                if (team_change.seat_id != kInvalidLobbySeatId &&
                    seats[k].seat_id == team_change.seat_id)
                {
                    seat_order = k;
                    break;
                }
            }
            if (!seat_order.has_value())
            {
                send_state(peer_id);
                break;
            }
            std::vector<std::int16_t> sibling_teams;
            sibling_teams.reserve(seats.size());
            for (std::size_t k = 0; k < seats.size(); ++k)
            {
                if (k != *seat_order)
                    sibling_teams.push_back(seats[k].team);
            }
            LobbyPlayer& seat = seats[*seat_order];
            const std::int16_t current = seat.team;
            const std::int16_t team = resolve_seat_team(
                peer_id, team_change.team, current, sibling_teams);
            if (team >= 0 && team != current)
            {
                seat.team = team;
                // §4.3: a team change is a roster change for that machine —
                // clear its ready (all seats share one machine's ready).
                for (LobbyPlayer& machine_seat : seats)
                    machine_seat.ready = false;
                rebuild_needed = true;
            }
            else
            {
                // Denial echo: the resolve fell back to the requester's
                // current team, so the state will not change and no
                // broadcast fires. Echo the authoritative state to the
                // requester so its bounce is detectable without a timeout.
                send_state(peer_id);
            }
        }
        break;

    case LobbyMessageKind::RemoveSeat:
        if (!peer_it->second.seats.empty())
        {
            const auto& remove_seat =
                std::get<LobbyRemoveSeatMessage>(message.payload);
            std::vector<LobbyPlayer>& seats = peer_it->second.seats;
            const auto target = std::find_if(
                seats.begin(), seats.end(),
                [&remove_seat](const LobbyPlayer& seat) {
                    return remove_seat.seat_id != kInvalidLobbySeatId &&
                        seat.seat_id == remove_seat.seat_id;
                });
            if (target == seats.end())
            {
                // A stale P#/token pair must not remove whichever sibling
                // happens to occupy that dense index now.
                requester_echo_required = true;
                break;
            }

            if (seats.size() == 1u)
                peer_it->second.dormant_seat_id = target->seat_id;
            seats.erase(target);
            // Removing a seat changes this machine's declaration. Its
            // remaining seats must reconfirm readiness as one unit.
            for (LobbyPlayer& machine_seat : seats)
                machine_seat.ready = false;
            rebuild_needed = true;
        }
        else
        {
            requester_echo_required = true;
        }
        break;

    case LobbyMessageKind::StartGame:
        if (host_peer_id_.has_value() && *host_peer_id_ == peer_id)
        {
            const auto& start_game =
                std::get<LobbyStartGameMessage>(message.payload);
            // [NET-R4] a host StartGame request clears the prior denial echo
            // FIRST (queued non-StartGame messages between denial and read must
            // not wipe it — only the next StartGame does).
            state_.last_start_denial =
                start_denial_reason_value(StartDenialReason::None);
            state_.last_start_request_id = 0;

            StartDenialReason reason = StartDenialReason::None;
            if (start_allowed(peer_id, reason))
            {
                lobby_locked_ = true;
                start_game_requested_ = true;
                accepted_start_player_index = !peer_it->second.seats.empty()
                    ? std::optional<std::uint8_t>(
                          peer_it->second.seats.front().player_index)
                    : std::optional<std::uint8_t>(0xffu);
                accepted_start_request_id = start_game.request_id;
            }
            else
            {
                // §4.3 denial: record the reason and let the trailing
                // broadcast echo it to every peer. The lobby lock is NEVER
                // engaged, so the "locked lobby eats messages" hazard never
                // triggers and the host stays free to fix the blocker and
                // retry.
                state_.last_start_denial = start_denial_reason_value(reason);
                state_.last_start_request_id = start_game.request_id;
                // A repeated denial can restore exactly the same reason that
                // previous_state already carried (the request clears then
                // re-sets it). The requester still needs a fresh echo to
                // resolve this specific asynchronous StartGame attempt.
                authoritative_echo_required = true;
            }
        }
        // A non-host StartGame stays silently ignored (no denial echo),
        // preserving the historic drop-on-non-host behavior.
        break;

    case LobbyMessageKind::SettingsChange:
        if (host_peer_id_.has_value() && *host_peer_id_ == peer_id)
        {
            const auto& settings_change =
                std::get<LobbySettingsChangeMessage>(message.payload);
            state_.settings =
                sanitize_settings(settings_change.settings, state_.settings,
                                  local_session_);

            // A level or CTF-count change can strand joined players outside
            // the selected map's authored domain; re-resolve them so nobody
            // starts on a team the next match will not activate.
            const std::uint8_t team_mask = effective_team_mask();
            const std::int16_t first_team =
                lobby_first_selectable_team(state_.settings);
            for (auto& [other_peer_id, peer] : peers_)
            {
                for (std::size_t seat_order = 0;
                     seat_order < peer.seats.size(); ++seat_order)
                {
                    LobbyPlayer& seat = peer.seats[seat_order];
                    const auto seat_bit = seat.team >= 0 &&
                            seat.team < SCORE_TEAM_COUNT
                        ? static_cast<std::uint8_t>(
                              1u << static_cast<unsigned>(seat.team))
                        : static_cast<std::uint8_t>(0);
                    if ((team_mask & seat_bit) != 0)
                        continue;
                    std::vector<std::int16_t> sibling_teams;
                    sibling_teams.reserve(peer.seats.size());
                    for (std::size_t k = 0; k < peer.seats.size(); ++k)
                    {
                        if (k != seat_order)
                            sibling_teams.push_back(peer.seats[k].team);
                    }
                    const std::int16_t reteamed = resolve_seat_team(
                        other_peer_id,
                        first_team,
                        std::nullopt,
                        sibling_teams);
                    if (reteamed < 0)
                        continue;
                    seat.team = reteamed;
                    rebuild_needed = true;
                }
            }

            // Shared->exclusive transition (CTF/allied -> classic): seats
            // legitimately sharing a team across peers must be de-shared, or
            // a classic match starts with two humans on one team. Iterate the
            // flattened (connection order, seat order) list; the earliest
            // seat keeps the team.
            if (!lobby_teams_shareable(state_.settings))
            {
                struct OrderedSeat {
                    PeerId peer_id = 0;
                    ConnectedPeerState* peer = nullptr;
                    std::size_t seat_order = 0;
                };
                std::vector<std::pair<PeerId, ConnectedPeerState*>> ordered;
                ordered.reserve(peers_.size());
                for (auto& [other_peer_id, peer] : peers_)
                {
                    if (!peer.seats.empty())
                        ordered.emplace_back(other_peer_id, &peer);
                }
                std::sort(ordered.begin(), ordered.end(),
                          [](const auto& lhs, const auto& rhs) {
                              return lhs.second->connection_order <
                                     rhs.second->connection_order;
                          });
                std::vector<OrderedSeat> ordered_seats;
                for (auto& [other_peer_id, peer] : ordered)
                {
                    for (std::size_t seat_order = 0;
                         seat_order < peer->seats.size(); ++seat_order)
                    {
                        ordered_seats.push_back(
                            OrderedSeat{other_peer_id, peer, seat_order});
                    }
                }
                for (std::size_t i = 0; i < ordered_seats.size(); ++i)
                {
                    LobbyPlayer& seat = ordered_seats[i]
                        .peer->seats[ordered_seats[i].seat_order];
                    bool collides = false;
                    for (std::size_t j = 0; j < i; ++j)
                    {
                        const LobbyPlayer& earlier = ordered_seats[j]
                            .peer->seats[ordered_seats[j].seat_order];
                        if (earlier.team == seat.team)
                        {
                            collides = true;
                            break;
                        }
                    }
                    if (!collides)
                        continue;
                    std::vector<std::int16_t> sibling_teams;
                    const auto& seats = ordered_seats[i].peer->seats;
                    sibling_teams.reserve(seats.size());
                    for (std::size_t k = 0; k < seats.size(); ++k)
                    {
                        if (k != ordered_seats[i].seat_order)
                            sibling_teams.push_back(seats[k].team);
                    }
                    const std::int16_t reteamed = resolve_seat_team(
                        ordered_seats[i].peer_id, seat.team, std::nullopt,
                        sibling_teams);
                    if (reteamed < 0 || reteamed == seat.team)
                        continue;
                    seat.team = reteamed;
                    rebuild_needed = true;
                }
            }

            // §4.3: a host settings change that actually differs after
            // sanitize clears every NON-host machine's ready (each machine
            // re-confirms against the new match settings; the host uses GO,
            // not ready). An echo of identical settings is a no-op (go_menu
            // re-sends settings on every start attempt).
            if (state_.settings != previous_state.settings)
            {
                for (auto& [other_peer_id, peer] : peers_)
                {
                    if (host_peer_id_.has_value() &&
                        other_peer_id == *host_peer_id_)
                        continue;
                    for (LobbyPlayer& seat : peer.seats)
                    {
                        if (seat.ready)
                        {
                            seat.ready = false;
                            rebuild_needed = true;
                        }
                    }
                }
            }
        }
        break;
    }

    if (rebuild_needed)
        rebuild_state();

    if (state_ != previous_state || authoritative_echo_required)
        broadcast_state();
    else if (requester_echo_required)
        send_state(peer_id);
    if (accepted_start_player_index.has_value())
    {
        broadcast_start_game(*accepted_start_player_index,
                             accepted_start_request_id);
    }
}

void LobbyServer::poll_incoming_messages()
{
    apply_transport_disconnects();

    const LobbyPollResult poll_result = poll_lobby_messages(transport_);
    synchronize_transport_peers(poll_result.messages);
    for (const PeerId peer_id : poll_result.malformed_peer_ids)
        disconnect_client(peer_id);
    for (const auto& [peer_id, message] : poll_result.messages)
    {
        if (std::binary_search(poll_result.malformed_peer_ids.begin(),
                               poll_result.malformed_peer_ids.end(),
                               peer_id))
        {
            continue;
        }
        if (message.kind() == LobbyMessageKind::StartGame)
        {
            // A transport snapshot can report a disconnected peer together
            // with that peer's final queued message. Preserve those preceding
            // messages, but remove every deferred disconnect before freezing
            // and broadcasting the game-start roster. Otherwise clients build
            // from the ghost-inclusive StartGame state while the host builds
            // its config after the trailing disconnect pass.
            apply_transport_disconnects();
        }
        process_lobby_message(peer_id, message);
    }
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
    equivalent.ctf_strip_scenario_troops = state_.settings.ctf_strip_scenario_troops;
    equivalent.respawn_mode = state_.settings.respawn_mode;
    equivalent.generator_rate = state_.settings.generator_rate;
    equivalent.keep_fallen_heroes = state_.settings.keep_fallen_heroes;
    equivalent.cross_control = state_.settings.cross_control;
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
            // §4.2: NETWORKED roster assembly materializes only DEPLOYED
            // slots — benched characters never enter the netsession seed,
            // InitialSetup, or GuySnapshots (filtered BEFORE densification,
            // so compaction indices count deployed slots only). Local
            // sessions deliberately keep the full roster: the local start
            // path seeds the player's ACTIVE COMPANY slot from this
            // equivalent, so filtering here would drop benched members from
            // the real save file. The local assembly filter lives at spawn
            // time instead (spawn_team_from_save skips benched members).
            if (!local_session_ && !player.character_slots[slot_order].deployed)
                continue;
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
            gameplay_slot.owner_player_index =
                state_.players[slot.player_order].player_index;
            gameplay_slot.owner_save_slot = slot.slot_index;
            equivalent.team_list.push_back(std::move(gameplay_slot));
        }
        canonicalize_lobby_gameplay_guy_ids(equivalent.team_list);
        return equivalent;
    }

    for (std::size_t index = 0; index < ordered_slots.size(); ++index)
    {
        LobbyCharacterSlot compacted = *ordered_slots[index].slot;
        compacted.slot_index = static_cast<std::uint8_t>(index);
        compacted.owner_player_index =
            state_.players[ordered_slots[index].player_order].player_index;
        compacted.owner_save_slot = ordered_slots[index].slot_index;
        equivalent.team_list.push_back(std::move(compacted));
    }

    canonicalize_lobby_gameplay_guy_ids(equivalent.team_list);
    return equivalent;
}

std::vector<LobbyPlayerBinding> LobbyServer::build_player_bindings() const
{
    std::vector<LobbyPlayerBinding> bindings;
    bindings.reserve(peers_.size());

    for (const auto& [peer_id, peer] : peers_)
    {
        for (std::size_t seat_order = 0; seat_order < peer.seats.size();
             ++seat_order)
        {
            const LobbyPlayer& seat = peer.seats[seat_order];
            bindings.push_back(LobbyPlayerBinding{
                .peer_id = peer_id,
                .local_slot = static_cast<std::uint8_t>(seat_order),
                .player_index = seat.player_index,
                // allied_mode remains serialized for legacy saves/replays,
                // but explicit lobby assignments are already the gameplay
                // teams and must not collapse onto a shared color.
                .team = seat.team,
            });
        }
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
