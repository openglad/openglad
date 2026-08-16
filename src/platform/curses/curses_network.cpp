/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Host + join networking bring-up for the ncurses client. The lobby handshake
 * (LobbyServer + lobby messages) is lifted from the SDL-free parts of the SDL
 * lobby clients (src/platform/sdl/picker_lobby_network_client.cpp); the message
 * construction is plain data, replicated here because those helpers live in an
 * SDL translation unit the curses target does not link.
 *
 * The networked game sessions mirror LocalCursesSession in curses_game_runtime
 * (an in-process GameServer ticking an authoritative world + a co-located
 * GameClient mirror). The HOST session keeps the full server+client pair but the
 * server transport is the lobby's combined transport and the host's own client
 * connects over a loopback client transport; the JOIN session is only the client
 * half over the remote transport (no server). Both swap `current_game` around the
 * server.step() vs client.poll_messages() boundary so each world's obmap stays
 * separate — the same pattern LocalCursesSession relies on.
 */
#include <openglad/platform/curses/curses_network.h>

#include <openglad/platform/curses/clock.h>
#include <openglad/platform/curses/curses_input.h>
#include <openglad/platform/curses/curses_renderer.h>
#include <openglad/platform/curses/terminal.h>

#include <openglad/core/constants.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/net_transport_multiplex.h>
#include <openglad/gameplay/pack_transfer.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/platform/net_transport_relay_ws.h>
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/platform/net_transport_websocket_server.h>
#include <openglad/platform/curses/curses_game_runtime.h>
#include <openglad/resources/campaign_metadata.h>

#include <cstring>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h> // cfg
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/pack_transfer_io.h>
#include <openglad/resources/progression.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/win_shares.h>
#include <openglad/server/headless_server_runtime.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace og::curses {

namespace {

constexpr int kDefaultPort = 12345;
constexpr std::string_view kDefaultCampaignId = "gladiator";

// A unique-per-instance network player name for readable diagnostics. The
// server-issued LobbySeatId is the only ownership identity; names are
// client-controlled display metadata and may collide. Mirrors
// make_network_player_name() in the SDL lobby clients.
std::string make_network_player_name()
{
    static std::atomic<std::uint64_t> counter{0};
    const std::uint64_t now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t seq = counter.fetch_add(1, std::memory_order_relaxed);
    return std::format("curses-{:016x}-{:x}", now, seq);
}

// Pull human-readable notification text out of an event batch into `out`.
void collect_notifications(const og::sim::SimEventBatch& batch,
                           std::vector<std::string>& out)
{
    for (const og::sim::Event& ev : batch.events) {
        if (ev.kind == og::sim::EventKind::Notification && !ev.text.empty())
            out.push_back(ev.text);
    }
}

// Latched level-end state (see curses_game_runtime.cpp for the rationale: the
// authoritative end arrives as an EndGame/SetEnd event and must NOT be stored in
// the mirror world, since the next delta snapshot would clobber it).
struct PendingEnd {
    bool ended = false;
    short ending = 0;
    short next_level = -1;
};

// Apply terminal game-flow events: latch any level end and collect notifications.
void apply_game_flow_batch(const og::sim::SimEventBatch& batch, PendingEnd& end,
                           std::vector<std::string>& messages)
{
    for (const og::sim::Event& ev : batch.events) {
        switch (ev.kind) {
        case og::sim::EventKind::EndGame:
            end.ended = true;
            end.ending = static_cast<short>(static_cast<std::int32_t>(ev.a));
            end.next_level = static_cast<short>(static_cast<std::int32_t>(ev.b));
            break;
        case og::sim::EventKind::SetEnd:
            end.ended = true;
            break;
        case og::sim::EventKind::Notification:
            if (!ev.text.empty())
                messages.push_back(ev.text);
            break;
        default:
            break;
        }
    }
}

// --- lobby message construction (replicated from the SDL lobby helpers) ------

og::sim::LobbyCharacterData make_lobby_character_data(const guy& source)
{
    og::sim::LobbyCharacterData character;
    character.guy_id = source.id;
    character.name = source.name;
    character.family = static_cast<std::int8_t>(source.family);
    character.strength = source.strength;
    character.dexterity = source.dexterity;
    character.constitution = source.constitution;
    character.intelligence = source.intelligence;
    character.armor = source.armor;
    character.exp = source.exp;
    character.kills = source.kills;
    character.level_kills = source.level_kills;
    character.total_damage = source.total_damage;
    character.total_hits = source.total_hits;
    character.total_shots = source.total_shots;
    character.teamnum = source.teamnum;
    character.scen_damage = source.scen_damage;
    character.scen_kills = source.scen_kills;
    character.scen_damage_taken = source.scen_damage_taken;
    character.scen_min_hp = source.scen_min_hp;
    character.scen_shots = source.scen_shots;
    character.scen_hits = source.scen_hits;
    character.level = source.level;
    return character;
}

short resolve_initial_local_team(const SaveData& save)
{
    const auto has_team = [&save](short team) {
        return std::any_of(
            save.team_list.begin(), save.team_list.end(),
            [team](const auto& member) {
                return member != nullptr && member->teamnum == team;
            });
    };

    if (save.my_team >= 0 && save.my_team < MAX_PLAYERS && has_team(save.my_team))
        return save.my_team;

    for (const auto& member : save.team_list) {
        if (member != nullptr && member->teamnum >= 0 &&
            member->teamnum < MAX_PLAYERS)
            return member->teamnum;
    }
    return 0;
}

// Build this peer's complete roster. The player's seat team chooses the view;
// each character's own teamnum remains its combat allegiance.
og::sim::LobbyPlayer build_local_lobby_player(const SaveData& save,
                                              std::string_view player_name,
                                              short local_team)
{
    og::sim::LobbyPlayer player;
    player.name = std::string(player_name);
    // v8: advertise the active company's display name (SaveData::save_name).
    player.company = save.save_name;
    player.team = local_team;
    player.ready = false;
    player.is_host = false;

    for (std::size_t slot_index = 0; slot_index < save.team_list.size(); ++slot_index) {
        const auto& member = save.team_list[slot_index];
        if (member == nullptr)
            continue;
        player.character_slots.push_back(og::sim::LobbyCharacterSlot{
            .slot_index = static_cast<std::uint8_t>(slot_index),
            .character = make_lobby_character_data(*member),
            // v8: stamped from the save guy's v14 deploy flag.
            .deployed = member->deployed,
        });
    }
    return player;
}

og::sim::LobbyMessage make_join_message(const SaveData& save,
                                        std::string_view player_name,
                                        short local_team)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyJoinMessage{
        .player = build_local_lobby_player(save, player_name, local_team),
    };
    return message;
}

std::uint8_t ctf_authored_team_mask_for_save(const SaveData& save)
{
    if (!og::ui::is_versus_campaign(save) ||
        get_mounted_campaign() != save.current_campaign)
    {
        return 0;
    }

    LevelRuntimeData scenario(save.scen_num, false,
                              &headless_level_data_hooks());
    if (!scenario.load())
        return 0;
    return og::ui::ctf_authored_team_mask_for_loaded_level(
        save, scenario.world(), get_mounted_campaign());
}

og::sim::LobbyMessage make_settings_message(const SaveData& save, int difficulty)
{
    og::sim::LobbySettings settings;
    settings.campaign_id = save.current_campaign.empty()
        ? std::string(kDefaultCampaignId)
        : save.current_campaign;
    settings.scenario_id = save.scen_num;
    settings.difficulty = static_cast<std::int16_t>(difficulty);
    settings.allied_mode = save.allied_mode;
    settings.ctf_team_count = save.ctf_team_count;
    settings.ctf_authored_team_mask =
        ctf_authored_team_mask_for_save(save);
    settings.ctf_capture_limit = save.ctf_capture_limit;
    settings.ctf_respawn_ticks = save.ctf_respawn_ticks;
    settings.ctf_strip_scenario_troops = save.ctf_strip_scenario_troops;
    settings.respawn_mode = save.respawn_mode;
    settings.generator_rate = save.generator_rate;
    settings.keep_fallen_heroes = save.keep_fallen_heroes;
    settings.cross_control = save.cross_control;
    settings.infinite_gold = save.infinite_gold;
    // Protocol v12: shared-teams rule rides the wire (matchup: versus).
    settings.shared_teams = og::ui::is_versus_campaign(save) ? 1 : 0;

    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbySettingsChangeMessage{
        .player_index = 0xffu,
        .settings = std::move(settings),
    };
    return message;
}

void send_lobby_message(og::sim::ITransport& transport, og::sim::PeerId peer_id,
                        og::sim::LobbyMessage message)
{
    transport.send_lobby_message(
        peer_id, std::make_shared<og::sim::LobbyMessage>(std::move(message)));
}

const og::sim::LobbyPlayer* find_player_by_seat_id(
    const og::sim::LobbyState& state,
    og::sim::LobbySeatId seat_id) noexcept
{
    if (seat_id == og::sim::kInvalidLobbySeatId)
        return nullptr;
    const auto it = std::find_if(
        state.players.begin(), state.players.end(),
        [seat_id](const og::sim::LobbyPlayer& player) {
            return player.seat_id == seat_id;
        });
    return it != state.players.end() ? &*it : nullptr;
}

std::vector<const og::sim::LobbyPlayer*> find_local_seats(
    const og::sim::LobbyState& state)
{
    std::vector<const og::sim::LobbyPlayer*> seats;
    seats.reserve(state.local_seat_ids.size());
    for (const og::sim::LobbySeatId seat_id : state.local_seat_ids)
    {
        if (const og::sim::LobbyPlayer* const player =
                find_player_by_seat_id(state, seat_id))
        {
            seats.push_back(player);
        }
    }
    return seats;
}

const og::sim::LobbyPlayer* find_local_player(
    const og::sim::LobbyState& state) noexcept
{
    const std::vector<const og::sim::LobbyPlayer*> seats =
        find_local_seats(state);
    return seats.empty() ? nullptr : seats.front();
}

struct OrderedLobbyGameplaySlot {
    std::uint8_t private_slot_index = 0;
    std::size_t player_order = 0;
    std::size_t slot_order = 0;
    const og::sim::LobbyCharacterSlot* slot = nullptr;
};

// Rebuild the joiner's game-start seed from the authoritative lobby echo.
// Keep this byte-for-byte equivalent in shape to
// LobbyServer::build_save_data_equivalent(): both sides sort colliding private
// save slots by slot/player/source order, compact only when that combined order
// is sparse, preserve ownership metadata, and canonicalize private guy-id
// collisions before either side creates a world.
og::sim::LobbySaveDataEquivalent build_join_save_equivalent_from_state(
    const og::sim::LobbyState& state)
{
    og::sim::LobbySaveDataEquivalent equivalent;
    equivalent.current_campaign = state.settings.campaign_id.empty()
        ? std::string(kDefaultCampaignId)
        : state.settings.campaign_id;
    equivalent.scen_num =
        state.settings.scenario_id > 0 ? state.settings.scenario_id : 1;
    equivalent.numplayers = static_cast<unsigned char>(
        std::min<std::size_t>(state.players.size(), MAX_PLAYERS));
    equivalent.allied_mode = state.settings.allied_mode;
    equivalent.ctf_team_count = state.settings.ctf_team_count;
    equivalent.ctf_capture_limit = state.settings.ctf_capture_limit;
    equivalent.ctf_respawn_ticks = state.settings.ctf_respawn_ticks;
    equivalent.ctf_strip_scenario_troops =
        state.settings.ctf_strip_scenario_troops;
    equivalent.respawn_mode = state.settings.respawn_mode;
    equivalent.generator_rate = state.settings.generator_rate;
    equivalent.keep_fallen_heroes = state.settings.keep_fallen_heroes;
    equivalent.cross_control = state.settings.cross_control;
    equivalent.infinite_gold = state.settings.infinite_gold;

    std::vector<OrderedLobbyGameplaySlot> ordered_slots;
    for (std::size_t player_order = 0; player_order < state.players.size();
         ++player_order)
    {
        const og::sim::LobbyPlayer& player = state.players[player_order];
        for (std::size_t slot_order = 0;
             slot_order < player.character_slots.size();
             ++slot_order)
        {
            const og::sim::LobbyCharacterSlot& slot =
                player.character_slots[slot_order];
            if (!slot.deployed)
                continue;
            ordered_slots.push_back(OrderedLobbyGameplaySlot{
                .private_slot_index = slot.slot_index,
                .player_order = player_order,
                .slot_order = slot_order,
                .slot = &slot,
            });
        }
    }

    if (ordered_slots.size() > MAX_TEAM_SIZE)
    {
        throw std::runtime_error(
            "Curses lobby exceeded the SaveData-equivalent 24-slot team limit");
    }

    std::sort(
        ordered_slots.begin(),
        ordered_slots.end(),
        [](const OrderedLobbyGameplaySlot& lhs,
           const OrderedLobbyGameplaySlot& rhs) {
            if (lhs.private_slot_index != rhs.private_slot_index)
                return lhs.private_slot_index < rhs.private_slot_index;
            if (lhs.player_order != rhs.player_order)
                return lhs.player_order < rhs.player_order;
            return lhs.slot_order < rhs.slot_order;
        });

    const bool slots_are_dense = std::all_of(
        ordered_slots.begin(),
        ordered_slots.end(),
        [&ordered_slots](const OrderedLobbyGameplaySlot& slot) {
            return static_cast<std::size_t>(slot.private_slot_index) ==
                static_cast<std::size_t>(&slot - ordered_slots.data());
        });

    equivalent.team_list.reserve(ordered_slots.size());
    for (std::size_t index = 0; index < ordered_slots.size(); ++index)
    {
        const OrderedLobbyGameplaySlot& ordered = ordered_slots[index];
        og::sim::LobbyCharacterSlot gameplay_slot = *ordered.slot;
        if (!slots_are_dense)
            gameplay_slot.slot_index = static_cast<std::uint8_t>(index);
        gameplay_slot.owner_player_index =
            state.players[ordered.player_order].player_index;
        gameplay_slot.owner_save_slot = ordered.private_slot_index;
        equivalent.team_list.push_back(std::move(gameplay_slot));
    }

    og::sim::canonicalize_lobby_gameplay_guy_ids(equivalent.team_list);
    return equivalent;
}

// Resolve the entity this peer's view follows. The client's controlled-entity
// map is GLOBAL (indexed by player index, identical on every peer), so the local
// avatar is the slot for THIS peer's own player index. Falls back to any living
// entity this player controls. Mirrors select_control_for_view() in the SDL
// local transport shadow.
std::uint32_t resolve_followed_entity_id(const og::sim::GameClient& client,
                                         const GameWorld& mirror,
                                         std::size_t local_player_index)
{
    const auto& ids = client.controlled_entity_ids();
    if (local_player_index < ids.size() && ids[local_player_index] != 0) {
        if (const walker* w = mirror.find_by_id(ids[local_player_index]);
            w != nullptr && !w->dead())
            return ids[local_player_index];
    }
    // Fallback: the first living entity this player controls (user == index).
    for (const auto& up : mirror.oblist) {
        const walker* w = up.get();
        if (w && !w->dead() &&
            w->user() == static_cast<int>(local_player_index))
            return w->entity_id();
    }
    return 0;
}

// §4.5 follow camera for a networked curses peer — the curses parity of the
// SDL DisplayFollowState. Engaged while the local seat has no controllable
// walker (0-deploy, all-dead, spectator); the seat's SwitchChar binding
// cycles the watched target (Shift = reverse) through the shared
// og::sim follow-target selectors, so a curses peer picks the same targets
// an SDL peer would. Camera-only: never stamps user tags.
struct CursesFollowState {
    bool engaged = false;
    std::uint32_t target_entity_id = 0;
};

// Per-frame maintenance, run after the mirror poll: engage when the legacy
// resolution has nothing (no live mapped walker, no user-tagged fallback)
// and no respawn-pending corpse holds the "(spectating)" countdown view;
// disengage the moment the seat resolves again; auto-advance a
// dead/unresolved target.
void maintain_curses_follow(CursesFollowState& follow,
                            const og::sim::GameClient& client,
                            GameWorld& mirror,
                            std::size_t local_player_index)
{
    if (resolve_followed_entity_id(client, mirror, local_player_index) != 0) {
        follow = {};
        return;
    }

    // A dead own corpse with a pending revive entry keeps today's
    // "(spectating)" respawn-countdown shape instead of engaging.
    if (og::sim::mode_scripted_active(mirror) ||
        og::sim::classic_respawn_active(mirror)) {
        for (const auto& up : mirror.oblist) {
            const walker* w = up.get();
            if (w && w->dead() && w->myguy != nullptr &&
                w->user() == static_cast<int>(local_player_index) &&
                og::sim::respawn_pending_player(mirror.respawn,
                                                    w->entity_id())) {
                follow = {};
                return;
            }
        }
    }

    if (!follow.engaged) {
        follow.engaged = true;
        follow.target_entity_id = og::sim::default_follow_target_id(
            mirror, client.controlled_entity_ids());
        return;
    }

    walker* const target = follow.target_entity_id != 0
        ? mirror.find_by_id(follow.target_entity_id)
        : nullptr;
    if (target == nullptr) {
        follow.target_entity_id = og::sim::default_follow_target_id(
            mirror, client.controlled_entity_ids());
    } else if (target->dead()) {
        follow.target_entity_id =
            og::sim::next_follow_target_id(mirror, target, false);
    }
}

// SwitchChar press-edge cycles the watched target. The same input still
// rides to the server, which ignores it for a null seat (harmless dual
// consumption, matching the SDL client).
void handle_curses_follow_input(CursesFollowState& follow,
                                const InputState& input, GameWorld& mirror)
{
    if (!follow.engaged ||
        !input.players[0].was_pressed(InputAction::SwitchChar))
        return;
    walker* const current = follow.target_entity_id != 0
        ? mirror.find_by_id(follow.target_entity_id)
        : nullptr;
    follow.target_entity_id = og::sim::next_follow_target_id(
        mirror, current, input.players[0].is_held(InputAction::Shift));
}

// followed_entity_id() resolution while engaged: the watched target when it
// is still live, else the legacy resolution (maintenance re-targets on the
// next advance).
std::uint32_t follow_or_legacy_entity_id(const CursesFollowState& follow,
                                         const og::sim::GameClient& client,
                                         const GameWorld& mirror,
                                         std::size_t local_player_index)
{
    if (follow.engaged && follow.target_entity_id != 0) {
        if (const walker* w = mirror.find_by_id(follow.target_entity_id);
            w != nullptr && !w->dead())
            return follow.target_entity_id;
    }
    return resolve_followed_entity_id(client, mirror, local_player_index);
}

// Decode lobby traffic regardless of whether the transport speaks typed messages
// (in-process) or raw envelopes (WebSocket / relay).
std::vector<og::sim::TypedReceivedMessage> poll_lobby_transport_messages(
    og::sim::ITransport& transport)
{
    if (transport.supports_typed_messages())
        return transport.poll_typed();

    std::vector<og::sim::TypedReceivedMessage> typed_messages;
    for (const auto& message : transport.poll()) {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
            continue;

        og::sim::TypedReceivedMessage typed_message;
        typed_message.peer_id = message.peer_id;
        switch (envelope.message_type) {
        case og::sim::kLobbyMessageType: {
            const auto decoded = og::sim::deserialize_lobby_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind = og::sim::TypedReceivedMessageKind::LobbyMessage;
            typed_message.lobby_message =
                std::make_shared<og::sim::LobbyMessage>(*decoded);
            break;
        }
        case og::sim::kLobbyStateMessageType: {
            const auto decoded = og::sim::deserialize_lobby_state_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind = og::sim::TypedReceivedMessageKind::LobbyState;
            typed_message.lobby_state =
                std::make_shared<og::sim::LobbyState>(*decoded);
            break;
        }
        // Class-pack transfer (protocol v10): joiner-bound stream.
        case og::sim::kPackManifestMessageType: {
            const auto decoded =
                og::sim::deserialize_pack_manifest_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind = og::sim::TypedReceivedMessageKind::PackManifest;
            typed_message.pack_manifest =
                std::make_shared<og::sim::PackManifestMessage>(*decoded);
            break;
        }
        case og::sim::kPackFileChunkMessageType: {
            const auto decoded =
                og::sim::deserialize_pack_file_chunk_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind = og::sim::TypedReceivedMessageKind::PackFileChunk;
            typed_message.pack_file_chunk =
                std::make_shared<og::sim::PackFileChunkMessage>(*decoded);
            break;
        }
        case og::sim::kPackTransferDoneMessageType: {
            const auto decoded =
                og::sim::deserialize_pack_transfer_done_message(message.data);
            if (!decoded.has_value())
                continue;
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::PackTransferDone;
            typed_message.pack_transfer_done =
                std::make_shared<og::sim::PackTransferDoneMessage>(*decoded);
            break;
        }
        default:
            continue;
        }
        typed_messages.push_back(std::move(typed_message));
    }
    return typed_messages;
}

// =====================================================================
// Networked sessions
// =====================================================================

// §4.6: a curses networked win persists this machine's deploy-ratio SHARE of
// the pot into its own company save (active_company_slot), while `session_save`
// (the in-memory authoritative/mirror save) keeps the full combined fold. The
// fold's applied deltas + the pre-fold deploy roster feed the shared SDL-free
// win-share core, so a curses peer conserves with an SDL peer in the same
// match. Only a genuine win (ended, ending == 0, not a CTF rematch) persists.
void persist_curses_networked_win(SaveData& session_save, const GameWorld& world,
                                  std::size_t player_index, int next_level)
{
    if (is_mode_rematch_end(world, /*ending=*/0, next_level))
        return;

    og::server::sync_headless_server_save_data_from_world(session_save, world);

    og::progression::WinFoldContext fold_ctx;
    for (std::size_t team = 0; team < fold_ctx.time_bonus.size(); ++team)
        fold_ctx.time_bonus[team] = og::progression::calculate_win_time_bonus(
            world, session_save, static_cast<int>(team));
    fold_ctx.rematch_shape = og::progression::mode_rematch_shape(
        world, session_save, static_cast<short>(next_level));
    fold_ctx.finished_level = session_save.scen_num;
    fold_ctx.outcome.ending = 0;
    fold_ctx.outcome.next_level = static_cast<short>(
        next_level >= 0 ? next_level : (session_save.scen_num + 1));
    fold_ctx.outcome.networked = true;

    og::progression::NetWinFoldCapture capture;
    capture.deployed = og::progression::collect_deployed_contributors(world);

    const int finished_level = fold_ctx.finished_level;
    // The in-memory session save keeps the full combined fold.
    og::progression::apply_win_fold(session_save, world, fold_ctx);
    capture.cash_delta = fold_ctx.applied_cash_delta;
    capture.score_delta = fold_ctx.applied_score_delta;

    const std::array<std::uint8_t, 1> owners = {
        static_cast<std::uint8_t>(player_index)};
    std::optional<std::size_t> primary_team;
    if (session_save.my_team >= 0 && session_save.my_team < MAX_PLAYERS)
        primary_team = static_cast<std::size_t>(session_save.my_team);

    // The disk company save gets baseline + this machine's share.
    (void)og::progression::persist_networked_win(
        og::data::active_company_slot(), session_save, world,
        std::span<const std::uint8_t>(owners), primary_team, capture,
        finished_level);
}

// HOST: authoritative GameServer over the lobby's shared transport + a
// co-located mirror GameClient connected over a loopback client transport. Almost
// identical to LocalCursesSession, but the transport, save and player bindings
// come from the lobby instead of being built locally.
class HostCursesSession final : public CursesGameSession
{
public:
    static std::unique_ptr<HostCursesSession> create(
        const og::sim::LobbySaveDataEquivalent& lobby_save,
        const std::vector<og::sim::LobbyPlayerBinding>& bindings,
        int difficulty,
        std::shared_ptr<og::sim::ITransport> server_transport,
        std::shared_ptr<og::sim::InProcessTransport> host_client_transport,
        std::uint8_t host_player_index,
        std::string* error);

    ~HostCursesSession() override { current_game = saved_game_; }

    void send_input(const InputState& input) override
    {
        handle_curses_follow_input(follow_, input, client_level_->world());
        pending_input_ = input;
        have_input_ = true;
    }

    void advance() override
    {
        GameWorld& sw = server_world();
        current_game = &server_ctx_;
        if (have_input_) {
            client_->send_input(pending_input_, sw.tick_count_ + 1);
            have_input_ = false;
        }
        server_->step();
        current_game = &client_ctx_;
        client_->poll_messages();
        maintain_curses_follow(follow_, *client_, client_level_->world(),
                               local_player_index_);
    }

    GameWorld& mirror_world() override { return client_level_->world(); }

    std::uint32_t followed_entity_id() const override
    {
        return follow_or_legacy_entity_id(follow_, *client_,
                                          client_level_->world(),
                                          local_player_index_);
    }

    bool follow_engaged() const override { return follow_.engaged; }

    std::uint32_t next_input_tick() const override
    {
        return server_world().tick_count_ + 1;
    }

    bool ended() const override
    {
        return pending_end_.ended || client_level_->world().game_ended;
    }
    int ending() const override
    {
        return pending_end_.ended ? pending_end_.ending : client_level_->world().ending;
    }
    int next_level() const override
    {
        return pending_end_.ended ? pending_end_.next_level
                                  : client_level_->world().next_level;
    }

    void request_abort() override { client_->request_level_abort(); }

    std::vector<std::string> drain_messages() override
    {
        return std::exchange(messages_, {});
    }

    // §4.6: on a networked win the host banks its deploy-ratio share into its
    // own company save (the authoritative server_save_ keeps the full fold).
    void commit_result_to_save() override
    {
        if (!ended() || ending() != 0)
            return;
        persist_curses_networked_win(server_save_, server_world(),
                                     local_player_index_, next_level());
    }

    og::sim::GameServer& server() { return *server_; }
    og::sim::GameClient& client() { return *client_; }
    GameWorld& server_world_ref() { return server_world(); }

#ifdef TESTING
    // Turn the loaded classic level into a CTF map on the authoritative server
    // world: armed ModeState + respawn anchors for teams 0/1. Runs
    // under the server context so the obmap writes land in the server's grid;
    // the host's own mirror gets the (authored, non-replicated) type bit too.
    bool inject_mode_scenario_for_testing(short requested_respawn_ticks)
    {
        GameplayContext* const saved = current_game;
        current_game = &server_ctx_;
        GameWorld& world = server_world();
        world.type |= GameWorld::TYPE_SCRIPTED;
        // Hand-arm the mode (a mounted pack's on_mode_init would do this on
        // the first scripted tick) with a name and a scoreboard HUD line so
        // the mirror renderers have replicated text to show.
        world.mode.active = true;
        world.mode.init_attempted = true;
        std::strncpy(world.mode.name.data(), "CTF", world.mode.name.size() - 1);
        world.mode.hud[0].team = 0;
        std::strncpy(world.mode.hud[0].text.data(), "Caps 0:0",
                     world.mode.hud[0].text.size() - 1);
        if (requested_respawn_ticks > 0) {
            world.ctf_requested_respawn_ticks = requested_respawn_ticks;
            world.respawn.respawn_ticks =
                static_cast<std::uint16_t>(requested_respawn_ticks);
        }

        bool ok = true;
        const auto spawn_anchor = [&world, &ok](int team, int x, int y) {
            walker* const marker = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
            if (marker == nullptr) {
                ok = false;
                return;
            }
            marker->setxy(static_cast<short>(x), static_cast<short>(y));
            marker->set_team_num(static_cast<unsigned char>(team));
            // The level bootstrap consumes start markers (the anchor scan
            // reads dead markers by design); a live marker acts.
            marker->set_dead(1);
        };

        const int far_x = std::max(160, static_cast<int>(world.pixmaxx) - 48);
        const int far_y = std::max(160, static_cast<int>(world.pixmaxy) - 48);
        spawn_anchor(0, 80, 48);
        spawn_anchor(0, 48, 80);
        spawn_anchor(1, far_x - 32, far_y);
        spawn_anchor(1, far_x, far_y - 32);
        og::sim::respawn_scan_anchors(world);

        client_level_->world().type |= GameWorld::TYPE_SCRIPTED;
        current_game = saved;
        return ok;
    }

    // Force the deterministic WIN shape on the AUTHORITATIVE world, mirroring
    // the SDL e2e recipe (g_test_remove_exits + clear-all-foes): pin a nonzero
    // team-0 payout, kill the level exits, and slay every living hostile to
    // the host team. The next server steps then declare the win
    // (level_done == 2) and forward the terminal EndGame to every peer.
    // Returns the number of foes slain.
    int force_server_win_for_testing(std::uint32_t pinned_team0_score)
    {
        GameplayContext* const saved = current_game;
        current_game = &server_ctx_;
        GameWorld& world = server_world();
        world.m_score[0] = pinned_team0_score;
        for (auto& uptr : world.fxlist) {
            walker* const w = uptr.get();
            if (w != nullptr && w->query_order() == Order::Treasure &&
                w->family() == FAMILY_EXIT)
                w->set_dead(1);
        }
        const unsigned char host_team =
            static_cast<unsigned char>(world.my_team);
        int slain = 0;
        for (auto& uptr : world.oblist) {
            walker* const w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->is_friendly_to_team(host_team) == 0) {
                w->set_dead(1);
                ++slain;
            }
        }
        current_game = saved;
        return slain;
    }

    // Kill every living walker on `team` in the AUTHORITATIVE server world
    // (§4.5 follow tests). With the whole team gone the seat's
    // death-reacquire finds nothing claimable; while another bound team
    // stays alive the wipe is suppressed, the seat goes null, and the
    // mirror's follow camera engages. Returns the number of walkers slain.
    int clear_server_team_for_testing(short team)
    {
        GameplayContext* const saved = current_game;
        current_game = &server_ctx_;
        GameWorld& world = server_world();
        int slain = 0;
        for (auto& uptr : world.oblist) {
            walker* const w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->query_order() == Order::Living &&
                static_cast<short>(w->team_num()) == team) {
                w->set_dead(1);
                ++slain;
            }
        }
        current_game = saved;
        return slain;
    }
#endif

private:
    HostCursesSession() = default;

    GameWorld& server_world() { return server_level_->world(); }
    const GameWorld& server_world() const { return server_level_->world(); }

    // Server (authoritative) side.
    SaveData server_save_;
    std::unique_ptr<LevelRuntimeData> server_level_;
    og::sim::SimEventLog server_events_;
    GameplayContext server_ctx_;
    IRandom* server_rng_ptr_ = nullptr;
    bool server_active_ = true;
    // The shared transport is owned by the lobby; the session only borrows it.
    std::shared_ptr<og::sim::ITransport> server_transport_;
    std::shared_ptr<og::sim::InProcessTransport> host_client_transport_;
    std::unique_ptr<og::sim::GameServer> server_;

    // Client (mirror) side.
    SaveData client_save_;
    std::unique_ptr<LevelRuntimeData> client_level_;
    og::sim::SimEventLog client_events_;
    GameplayContext client_ctx_;
    IRandom* client_rng_ptr_ = nullptr;
    bool client_active_ = true;
    og::sim::PeerId host_peer_id_ = 0;
    std::unique_ptr<og::sim::GameClient> client_;
    std::size_t local_player_index_ = 0;
    CursesFollowState follow_;

    GameplayContext* saved_game_ = nullptr;
    PendingEnd pending_end_;
    std::vector<std::string> messages_;
    InputState pending_input_;
    bool have_input_ = false;
};

std::unique_ptr<HostCursesSession> HostCursesSession::create(
    const og::sim::LobbySaveDataEquivalent& lobby_save,
    const std::vector<og::sim::LobbyPlayerBinding>& bindings,
    int difficulty,
    std::shared_ptr<og::sim::ITransport> server_transport,
    std::shared_ptr<og::sim::InProcessTransport> host_client_transport,
    std::uint8_t host_player_index,
    std::string* error)
{
    auto set_error = [&](const char* msg) -> std::unique_ptr<HostCursesSession> {
        if (error)
            *error = msg;
        return nullptr;
    };

    if (!server_transport || !host_client_transport)
        return set_error("host session: missing transport");

    // factory: private ctor — make_unique not applicable
    std::unique_ptr<HostCursesSession> s(new HostCursesSession());
    s->server_transport_ = std::move(server_transport);
    s->host_client_transport_ = std::move(host_client_transport);

    // Build the authoritative save from the negotiated lobby roster/settings.
    og::server::apply_headless_lobby_game_start_config(s->server_save_, lobby_save);
    og::server::copy_headless_server_save_data(s->client_save_, s->server_save_);

    const short level = s->server_save_.scen_num > 0 ? s->server_save_.scen_num : 1;

    // --- Server world: load level + spawn team (authoritative) ---
    s->server_level_ = std::make_unique<LevelRuntimeData>(
        level, true, &headless_level_data_hooks());
    GameWorld& sw = s->server_level_->world();
    s->server_rng_ptr_ = &sw.rng_;
    s->server_level_->set_sim_context(&s->server_save_, &sw.enemy_freeze,
                                      &s->server_events_, s->server_rng_ptr_, &cfg);
    s->server_ctx_.world = &sw;
    s->server_ctx_.save = &s->server_save_;
    s->server_ctx_.sim_events = &s->server_events_;
    s->server_ctx_.config = &cfg;
    s->server_ctx_.session_rng_ref = &s->server_rng_ptr_;
    s->server_ctx_.gameplay_active_ref = &s->server_active_;

    s->saved_game_ = current_game;
    current_game = &s->server_ctx_;
    if (!og::server::load_headless_level_from_save(*s->server_level_, s->server_save_,
                                                   difficulty, s->server_events_,
                                                   /*authoritative=*/true)) {
        current_game = s->saved_game_;
        return set_error("failed to load level for host game");
    }

    // §4.4 control-policy install: derive owner-locked from the negotiated
    // lobby config (session-only cross_control rides the equivalent, never a
    // disk round-trip) and stamp the machine map BEFORE bind_player scans run
    // below; snapshot v9 replicates both scalars to every mirror, including
    // the host's own.
    og::sim::install_control_policy(sw,
                                    /*networked=*/true,
                                    s->server_save_.cross_control != 0,
                                    bindings,
                                    s->server_save_.team_list);

    // --- Client mirror world: load the same level (grid + smoother) ---
    s->client_level_ = std::make_unique<LevelRuntimeData>(
        level, true, &headless_level_data_hooks());
    GameWorld& cw = s->client_level_->world();
    s->client_rng_ptr_ = &cw.rng_;
    s->client_level_->set_sim_context(&s->client_save_, &cw.enemy_freeze,
                                      &s->client_events_, s->client_rng_ptr_, &cfg);
    s->client_ctx_.world = &cw;
    s->client_ctx_.save = &s->client_save_;
    s->client_ctx_.sim_events = &s->client_events_;
    s->client_ctx_.config = &cfg;
    s->client_ctx_.session_rng_ref = &s->client_rng_ptr_;
    s->client_ctx_.gameplay_active_ref = &s->client_active_;
    current_game = &s->client_ctx_;
    if (!og::server::load_headless_level_from_save(*s->client_level_, s->client_save_,
                                                   difficulty, s->client_events_,
                                                   /*authoritative=*/false)) {
        current_game = s->saved_game_;
        return set_error("failed to load mirror level for host game");
    }

    // --- Server over the shared (lobby) transport ---
    s->server_transport_->accept_connections();
    s->server_ = std::make_unique<og::sim::GameServer>(sw, s->server_events_,
                                                       *s->server_transport_);
    s->server_->set_return_to_lobby_mode(true);
    s->server_->on_save_sync = [s_raw = s.get()] {
        og::server::sync_headless_server_save_data_from_world(
            s_raw->server_save_, s_raw->server_world());
    };
    // Confirm withdraw/exit/transition requests so the server forwards the
    // terminal EndGame to every peer (host + remote joiners) and they all return
    // to the team-build menu, instead of the request being silently dropped.
    s->server_->on_withdraw_accepted = [](int) { return true; };
    s->server_->on_exit_accepted = [](int) { return true; };
    s->server_->on_level_transition = [](int) { return true; };

    // Connect every peer the lobby left attached to the shared transport (remote
    // joiners + the host's own loopback), then apply the lobby's player bindings.
    for (const og::sim::PeerId peer_id : s->server_transport_->connected_peers())
    {
        const bool owns_seat = std::any_of(
            bindings.begin(),
            bindings.end(),
            [peer_id](const og::sim::LobbyPlayerBinding& binding) {
                return binding.peer_id == peer_id;
            });
        if (owns_seat)
            s->server_->connect_client(peer_id);
        else
            s->server_->connect_spectator(peer_id);
    }
    for (const og::sim::LobbyPlayerBinding& binding : bindings) {
        s->server_->bind_player(binding.peer_id, binding.player_index,
                                static_cast<short>(binding.team), nullptr);
    }

    // --- Host's own client (mirror display) over the loopback transport ---
    s->host_peer_id_ = s->host_client_transport_->local_peer_id();
    s->client_ = std::make_unique<og::sim::GameClient>(*s->host_client_transport_,
                                                       s->host_peer_id_, &cw);
    auto* raw = s.get();
    s->client_->set_sim_event_batch_callback(
        [raw](const og::sim::SimEventBatch& b) { collect_notifications(b, raw->messages_); });
    s->client_->set_game_flow_event_batch_callback(
        [raw](const og::sim::SimEventBatch& b) {
            apply_game_flow_batch(b, raw->pending_end_, raw->messages_);
        });
    s->client_->set_exit_prompt_callback(
        [raw](const og::sim::ExitPromptBroadcastMessage&) {
            raw->client_->send_exit_prompt_response(true);
        });
    s->client_->send_client_ready();
    // The host's view follows its own player index (from the lobby binding); the
    // controlled-entity map the client receives is keyed by global player index.
    s->local_player_index_ = host_player_index;

    // Exchange the initial keyframe so the mirror world is populated before the
    // first render, swapping contexts so each side touches its own obmap.
    for (int i = 0; i < 6; ++i) {
        current_game = &s->server_ctx_;
        s->server_->step();
        current_game = &s->client_ctx_;
        s->client_->poll_messages();
    }

    return s;
}

// JOIN: only the client half. A GameClient over the remote transport bound to a
// mirror world loaded from the negotiated save (for grid/smoother). No server;
// advance() polls the client, send_input forwards to it.
class JoinCursesSession final : public CursesGameSession
{
public:
    static std::unique_ptr<JoinCursesSession> create(
        const og::sim::LobbySaveDataEquivalent& lobby_save,
        int difficulty,
        std::shared_ptr<og::sim::ITransport> transport,
        og::sim::PeerId server_peer_id,
        std::size_t local_player_index,
        std::string* error);

    ~JoinCursesSession() override { current_game = saved_game_; }

    void send_input(const InputState& input) override
    {
        current_game = &client_ctx_;
        handle_curses_follow_input(follow_, input, client_level_->world());
        client_->send_input(input, client_level_->world().tick_count_ + 1);
    }

    void advance() override
    {
        current_game = &client_ctx_;
        client_->poll_messages();
        maintain_curses_follow(follow_, *client_, client_level_->world(),
                               local_player_index_);
    }

    GameWorld& mirror_world() override { return client_level_->world(); }

    std::uint32_t followed_entity_id() const override
    {
        return follow_or_legacy_entity_id(follow_, *client_,
                                          client_level_->world(),
                                          local_player_index_);
    }

    bool follow_engaged() const override { return follow_.engaged; }

    std::uint32_t next_input_tick() const override
    {
        return client_level_->world().tick_count_ + 1;
    }

    bool ended() const override
    {
        return pending_end_.ended || client_level_->world().game_ended;
    }
    int ending() const override
    {
        return pending_end_.ended ? pending_end_.ending : client_level_->world().ending;
    }
    int next_level() const override
    {
        return pending_end_.ended ? pending_end_.next_level
                                  : client_level_->world().next_level;
    }

    void request_abort() override { client_->request_level_abort(); }

    std::vector<std::string> drain_messages() override
    {
        return std::exchange(messages_, {});
    }

    // §4.6: on a networked win the joiner banks its deploy-ratio share into its
    // own company save; the mirror client_save_ keeps the full fold.
    void commit_result_to_save() override
    {
        if (!ended() || ending() != 0)
            return;
        persist_curses_networked_win(client_save_, client_level_->world(),
                                     local_player_index_, next_level());
    }

    og::sim::GameClient& client() { return *client_; }

private:
    JoinCursesSession() = default;

    SaveData client_save_;
    std::unique_ptr<LevelRuntimeData> client_level_;
    og::sim::SimEventLog client_events_;
    GameplayContext client_ctx_;
    IRandom* client_rng_ptr_ = nullptr;
    bool client_active_ = true;
    std::shared_ptr<og::sim::ITransport> transport_;
    std::unique_ptr<og::sim::GameClient> client_;
    std::size_t local_player_index_ = 0;
    CursesFollowState follow_;

    GameplayContext* saved_game_ = nullptr;
    PendingEnd pending_end_;
    std::vector<std::string> messages_;
};

std::unique_ptr<JoinCursesSession> JoinCursesSession::create(
    const og::sim::LobbySaveDataEquivalent& lobby_save,
    int difficulty,
    std::shared_ptr<og::sim::ITransport> transport,
    og::sim::PeerId server_peer_id,
    std::size_t local_player_index,
    std::string* error)
{
    auto set_error = [&](const char* msg) -> std::unique_ptr<JoinCursesSession> {
        if (error)
            *error = msg;
        return nullptr;
    };

    if (!transport)
        return set_error("join session: missing transport");

    // factory: private ctor — make_unique not applicable
    std::unique_ptr<JoinCursesSession> s(new JoinCursesSession());
    s->transport_ = std::move(transport);
    s->local_player_index_ = local_player_index;

    og::server::apply_headless_lobby_game_start_config(s->client_save_, lobby_save);
    const short level = s->client_save_.scen_num > 0 ? s->client_save_.scen_num : 1;

    // Mirror world: load the level so the renderer has a grid/smoother. Entities
    // are populated by the authoritative keyframe from the host.
    s->client_level_ = std::make_unique<LevelRuntimeData>(
        level, true, &headless_level_data_hooks());
    GameWorld& cw = s->client_level_->world();
    s->client_rng_ptr_ = &cw.rng_;
    s->client_level_->set_sim_context(&s->client_save_, &cw.enemy_freeze,
                                      &s->client_events_, s->client_rng_ptr_, &cfg);
    s->client_ctx_.world = &cw;
    s->client_ctx_.save = &s->client_save_;
    s->client_ctx_.sim_events = &s->client_events_;
    s->client_ctx_.config = &cfg;
    s->client_ctx_.session_rng_ref = &s->client_rng_ptr_;
    s->client_ctx_.gameplay_active_ref = &s->client_active_;

    s->saved_game_ = current_game;
    current_game = &s->client_ctx_;
    if (!og::server::load_headless_level_from_save(*s->client_level_, s->client_save_,
                                                   difficulty, s->client_events_,
                                                   /*authoritative=*/false)) {
        current_game = s->saved_game_;
        return set_error("failed to load mirror level for join game");
    }

    s->transport_->accept_connections();
    s->client_ = std::make_unique<og::sim::GameClient>(*s->transport_,
                                                       server_peer_id, &cw);
    auto* raw = s.get();
    s->client_->set_sim_event_batch_callback(
        [raw](const og::sim::SimEventBatch& b) { collect_notifications(b, raw->messages_); });
    s->client_->set_game_flow_event_batch_callback(
        [raw](const og::sim::SimEventBatch& b) {
            apply_game_flow_batch(b, raw->pending_end_, raw->messages_);
        });
    s->client_->set_exit_prompt_callback(
        [raw](const og::sim::ExitPromptBroadcastMessage&) {
            raw->client_->send_exit_prompt_response(true);
        });
    s->client_->send_client_ready();

    return s;
}

// =====================================================================
// Lobby (drives the LobbyServer handshake and yields a session)
// =====================================================================

enum class LobbyRole { Host, Join };

class CursesLobbyImpl final : public CursesLobby
{
public:
    CursesLobbyImpl(LobbyRole role, SaveData& save, int difficulty)
        : role_(role), save_(save), difficulty_(difficulty)
    {
        local_team_ = resolve_initial_local_team(save);
        player_name_ = make_network_player_name();
    }

    ~CursesLobbyImpl() override { teardown(); }

    // --- HOST setup: in-process loopback + WebSocket server (+ relay) ---------
    bool init_host(const HostOptions& opt, std::string* error)
    {
        difficulty_ = opt.difficulty;

        loopback_server_ = og::sim::InProcessTransport::create_server();
        loopback_server_->accept_connections();
        host_client_transport_ = loopback_server_->create_client_transport();

        std::vector<std::shared_ptr<og::sim::ITransport>> transports;
        transports.push_back(loopback_server_);

        std::string direct_error;
        try {
            const int port = opt.port > 0 ? opt.port : kDefaultPort;
            ws_server_ = std::make_shared<og::sim::WebSocketServerTransport>(port);
            transports.push_back(ws_server_);
        } catch (const std::exception& ex) {
            ws_server_.reset();
            direct_error = ex.what();
        }

        std::string relay_error;
        if (!opt.relay_url.empty()) {
            try {
                relay_ = std::make_shared<og::sim::RelayWebSocketTransport>(opt.relay_url);
                relay_->accept_connections();
                transports.push_back(relay_);
            } catch (const std::exception& ex) {
                relay_.reset();
                relay_error = ex.what();
            }
        }

        if (!ws_server_ && !relay_) {
            teardown();
            if (error) {
                *error = "Unable to host a network lobby.";
                if (!direct_error.empty())
                    *error = "Direct: " + direct_error;
                if (!relay_error.empty())
                    *error += (direct_error.empty() ? "Relay: " : "  Relay: ") + relay_error;
            }
            return false;
        }

        combined_transport_ = std::make_shared<og::sim::MultiplexTransport>(
            std::move(transports));
        combined_transport_->accept_connections();
        server_ = std::make_unique<og::sim::LobbyServer>(*combined_transport_);
        // Offer this host's mounted non-core class packs (protocol v10).
        server_->set_hosted_packs(og::resources::build_transferable_packs());

        // Seed the host's own settings + join over the loopback client transport.
        send_lobby_message(*host_client_transport_,
                           host_client_transport_->local_peer_id(),
                           make_settings_message(save_, difficulty_));
        send_lobby_message(*host_client_transport_,
                           host_client_transport_->local_peer_id(),
                           make_join_message(save_, player_name_, local_team_));

        pump_once();
        return true;
    }

    // --- JOIN setup: WebSocket / relay client transport, send a join ----------
    bool init_join(const JoinOptions& opt, std::string* error)
    {
        try {
            if (opt.via_relay)
                transport_ = std::make_shared<og::sim::RelayWebSocketTransport>(opt.url);
            else
                transport_ = std::make_shared<og::sim::WebSocketClientTransport>(opt.url);
        } catch (const std::exception& ex) {
            transport_.reset();
            if (error)
                *error = ex.what();
            return false;
        }

        transport_->accept_connections();
        server_peer_id_ = 1;
        join_sent_ = false;
        make_pack_client();
        pump_once();
        return true;
    }

    // Class-pack transfer (protocol v10): joiners pull missing packs before
    // ready-up. Progress and failures surface as Log lines (text-mode UI).
    void make_pack_client()
    {
        og::sim::PackTransferClient::Callbacks callbacks =
            og::resources::make_pack_transfer_client_callbacks();
        callbacks.log_status = [](const std::string& text) {
            Log("{}\n", text);
        };
        pack_client_ = std::make_unique<og::sim::PackTransferClient>(
            std::move(callbacks));
    }

    // --- TEST hook: drive the whole flow over an injected in-process server ----
    void init_host_over_transport(
        std::shared_ptr<og::sim::ITransport> combined_transport,
        std::shared_ptr<og::sim::InProcessTransport> host_client_transport)
    {
        combined_transport_ = std::move(combined_transport);
        host_client_transport_ = std::move(host_client_transport);
        combined_transport_->accept_connections();
        server_ = std::make_unique<og::sim::LobbyServer>(*combined_transport_);
        server_->set_hosted_packs(og::resources::build_transferable_packs());

        send_lobby_message(*host_client_transport_,
                           host_client_transport_->local_peer_id(),
                           make_settings_message(save_, difficulty_));
        send_lobby_message(*host_client_transport_,
                           host_client_transport_->local_peer_id(),
                           make_join_message(save_, player_name_, local_team_));
        pump_once();
    }

    void init_join_over_transport(std::shared_ptr<og::sim::ITransport> transport,
                                  og::sim::PeerId server_peer_id)
    {
        transport_ = std::move(transport);
        server_peer_id_ = server_peer_id;
        join_sent_ = false;
        make_pack_client();
        pump_once();
    }

    // --- CursesLobby contract -------------------------------------------------

    bool poll(ITerminal& term, IClock& clock) override
    {
        pump_once();
        render(term);

        // Non-blocking key handling.
        for (;;) {
            const Key key = term.poll_key(false);
            if (key.is_none())
                break;
            if (key.is_release())
                continue; // act on presses/repeats only; ignore key-up + focus
            if (key.code == KeyCode::Escape || key.is_char(U'q')) {
                cancel();
                return false;
            }
            if (role_ == LobbyRole::Host &&
                (key.is_enter() || key.is_char(U's') || key.is_char(U'S'))) {
                request_start();
            }
            if (key.is_char(U'r') || key.is_char(U'R')) {
                // §2.6 client ready gate (parity with the SDL
                // teams_toggle_ready click): setting ready with brought
                // characters, none deployed, and cross-control OFF is
                // denied — surface the caption instead of sending.
                // An empty-roster machine has no deploy minimum [NET-R9].
                // This client always owns one active seat; SDL's true
                // zero-seat shape has no READY action. Host machines never
                // gate (the state table returns a host state with no
                // ClientUnready caption).
                const bool next_ready = !local_ready();
                const og::ui::ReadyGoPresentation ready_presentation =
                    og::ui::format_ready_go_button(
                        /*networked=*/true,
                        /*is_host=*/role_ == LobbyRole::Host,
                        /*my_ready=*/!next_ready,
                        /*all_other_machines_ready=*/true,
                        /*global_deployed=*/1,
                        /*own_deployed=*/
                        og::ui::count_deployed_members(save_),
                        /*cross_control=*/save_.cross_control != 0,
                        /*spectator=*/save_.numplayers <= 0 ||
                            save_.team_size <= 0);
                if (next_ready &&
                    ready_presentation.state ==
                        og::ui::ReadyGoState::ClientUnready &&
                    !ready_presentation.caption.empty()) {
                    team_status_ = ready_presentation.caption;
                } else {
                    (void)set_ready(next_ready);
                }
            }
            if (key.is_char(U'c') || key.is_char(U'C')) {
                // §2.7 cross-control (curses parity surface = the lobby):
                // host-only actionable; a toggle is a SETTINGS change, so
                // the server clears every non-host machine's ready (§4.5)
                // and the echoed settings drive the status line below.
                // Sanitize on toggle ({0,1}; junk counts as ON, lands 0).
                if (role_ == LobbyRole::Host &&
                    host_client_transport_ != nullptr) {
                    save_.cross_control = static_cast<std::int16_t>(
                        save_.cross_control != 0 ? 0 : 1);
                    send_lobby_message(
                        *host_client_transport_,
                        host_client_transport_->local_peer_id(),
                        make_settings_message(save_, difficulty_));
                    pump_once();
                } else {
                    team_status_ = "Host controls cross-control";
                }
            }
            if (key.code == KeyCode::Left || key.is_char(U'<') ||
                key.is_char(U'[')) {
                select_local_seat(-1);
                continue;
            }
            if (key.code == KeyCode::Right || key.is_char(U'>') ||
                key.is_char(U']')) {
                select_local_seat(1);
                continue;
            }
            if (key.is_char(U't') || key.is_char(U'T')) {
                const og::sim::LobbyPlayer* const selected =
                    selected_local_player();
                if (selected == nullptr)
                    continue;

                // Cycle from the last REQUESTED target, not a potentially
                // stale replicated echo. The shared domain helper skips
                // inactive authored CTF teams and obeys an explicit team
                // count; classic/fallback lobbies retain all four colors.
                const bool request_pending_for_selected =
                    last_team_request_ >= 0 &&
                    last_team_request_seat_id_ == selected->seat_id;
                const short base = request_pending_for_selected
                    ? last_team_request_
                    : selected->team;
                const short target = state_.has_value()
                    ? og::sim::lobby_next_selectable_team(
                          state_->settings, base)
                    : static_cast<short>((base + 1) % MAX_PLAYERS);
                if (target < 0)
                    continue;

                last_team_request_ = target;
                last_team_request_seat_id_ = selected->seat_id;
                team_status_ = "Requested P" +
                    std::to_string(selected->player_index + 1) + " -> " +
                    og::sim::team_color_name(target);
                if (request_seat_team_change(
                        selected->player_index, selected->seat_id, target)) {
                    last_team_request_ = -1;
                    last_team_request_seat_id_ =
                        og::sim::kInvalidLobbySeatId;
                    team_status_.clear();
                }
            }
        }
        (void)clock;

        if (start_negotiated_)
            build_session_if_needed();
        // Return true on a build failure too, so run_curses_lobby's take_session()
        // yields null and the caller surfaces the error rather than spinning.
        return start_negotiated_ && (session_ != nullptr || session_built_failed_);
    }

    bool is_host() const override { return role_ == LobbyRole::Host; }

    void request_start() override
    {
        if (role_ != LobbyRole::Host || server_ == nullptr ||
            host_client_transport_ == nullptr || !state_.has_value())
            return;
        if (!local_player_is_host())
            return;

        const og::sim::LobbyPlayer* const local = find_local_player(*state_);
        if (local == nullptr)
            return;

        og::sim::LobbyMessage message;
        pending_start_request_id_ = next_start_request_id_++;
        if (next_start_request_id_ == 0)
            next_start_request_id_ = 1;
        message.payload = og::sim::LobbyStartGameMessage{
            .player_index = local->player_index,
            .request_id = pending_start_request_id_,
        };
        send_lobby_message(*host_client_transport_,
                           host_client_transport_->local_peer_id(),
                           std::move(message));
        pump_once();
    }

    std::unique_ptr<CursesGameSession> take_session() override
    {
        build_session_if_needed();
        session_taken_ = (session_ != nullptr);
        return std::move(session_);
    }

    std::vector<std::string> status_lines() const override
    {
        std::vector<std::string> lines;
        lines.push_back(role_ == LobbyRole::Host ? "Mode: HOST" : "Mode: JOIN");
        if (state_.has_value()) {
            // Display titles only; settings.campaign_id itself stays the raw
            // wire id. Scenario titles are read off the LOCAL mount, so the
            // titled form is only trustworthy when the mount matches the
            // lobby's campaign — a joiner with a different campaign mounted
            // would otherwise see that campaign's title for the host's level
            // number (every campaign has a level 1).
            const std::string campaign_id = state_->settings.campaign_id.empty()
                ? std::string(kDefaultCampaignId)
                : state_->settings.campaign_id;
            lines.push_back("Campaign: " +
                            og::data::campaign_display_title(campaign_id));
            lines.push_back("Level: " +
                            (get_mounted_campaign() == campaign_id
                                 ? og::data::scenario_display_name(
                                       state_->settings.scenario_id)
                                 : std::to_string(state_->settings.scenario_id)));
            int lobby_deployed = 0;
            int lobby_slots = 0;
            const bool has_multiple_local_seats =
                state_->local_seat_ids.size() > 1;
            for (const og::sim::LobbyPlayer& player : state_->players) {
                const bool is_me =
                    std::find(state_->local_seat_ids.begin(),
                              state_->local_seat_ids.end(),
                              player.seat_id) != state_->local_seat_ids.end();
                const bool is_selected =
                    is_me && player.seat_id == selected_local_seat_id_;
                std::string line =
                    "  Player " + std::to_string(player.player_index + 1) +
                    " (" + og::sim::team_color_name(player.team) + ")" +
                    (player.is_host ? " [host]" : "") +
                    (player.ready ? " [ready]" : "") +
                    (is_me ? " [you]" : "") +
                    (is_selected && has_multiple_local_seats
                         ? " [selected]"
                         : "");
                // §2.5 curses parity: the origin/company column + per-seat
                // deploy counts (clipped to the SDL COMPANY budget).
                if (!player.company.empty()) {
                    std::string company = player.company;
                    if (company.size() > 16)
                        company.resize(16);
                    line += " <" + company + ">";
                }
                int seat_deployed = 0;
                for (const og::sim::LobbyCharacterSlot& slot :
                     player.character_slots)
                {
                    ++lobby_slots;
                    if (slot.deployed) {
                        ++seat_deployed;
                        ++lobby_deployed;
                    }
                }
                if (!player.character_slots.empty()) {
                    line += " DEP " + std::to_string(seat_deployed) + "/" +
                        std::to_string(player.character_slots.size());
                }
                lines.push_back(std::move(line));
            }
            lines.push_back("Players: " + std::to_string(state_->players.size()));
            lines.push_back("Deployed: " + std::to_string(lobby_deployed) +
                            "/" + std::to_string(lobby_slots));
            // §9.12 (G5) census parity: the same session-status line the
            // SDL base camp header shows — role + machine/player census +
            // the host machine's company for joiners (the curses lobby has
            // no relay room code, so the room half stays empty).
            // The terminal status list has no HIRE command beside it, so it
            // takes the wide band budget — the same shapes the SDL header
            // shows whenever a composition hides HIRE.
            lines.push_back(og::ui::format_base_camp_session_status(
                role_ == LobbyRole::Host, {}, state_->players,
                og::ui::kBaseCampLineBCharsHireHidden));
            // §2.7: every peer sees the mode that changes its own rights
            // (the SDL MATCHUP row's shared label formatter).
            lines.push_back(
                "Control: " + og::ui::format_cross_control_label(
                                  state_->settings.cross_control != 0));
        } else {
            lines.push_back(role_ == LobbyRole::Host ? "Waiting for players..."
                                                     : "Connecting...");
        }
        if (!team_status_.empty())
            lines.push_back(team_status_);
        if (start_negotiated_)
            lines.push_back("Starting game...");
        return lines;
    }

    void cancel() override
    {
        cancelled_ = true;
        teardown();
    }
    bool cancelled() const override { return cancelled_; }

    bool request_team_change(short team) override
    {
        return request_seat_team_change(local_player_index(), team);
    }

    bool request_seat_team_change(std::uint8_t player_index,
                                  short team) override
    {
        if (!state_.has_value())
            return false;
        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            find_local_seats(*state_);
        const auto target_it = std::find_if(
            local_seats.begin(), local_seats.end(),
            [player_index](const og::sim::LobbyPlayer* const seat) {
                return seat->player_index == player_index;
            });
        if (target_it == local_seats.end())
            return false;
        return request_seat_team_change(
            player_index, (*target_it)->seat_id, team);
    }

    bool request_seat_team_change(std::uint8_t player_index,
                                  og::sim::LobbySeatId seat_id,
                                  short team) override
    {
        og::sim::ITransport* const client_link = role_ == LobbyRole::Host
            ? host_client_transport_.get()
            : transport_.get();
        if (client_link == nullptr || !state_.has_value())
            return false;

        const std::vector<const og::sim::LobbyPlayer*> local_seats =
            find_local_seats(*state_);
        const auto target_it = std::find_if(
            local_seats.begin(), local_seats.end(),
            [player_index, seat_id](const og::sim::LobbyPlayer* const seat) {
                return seat->player_index == player_index &&
                    seat->seat_id == seat_id;
            });
        if (target_it == local_seats.end())
            return false;
        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyTeamChangeMessage{
            .player_index = player_index,
            .seat_id = seat_id,
            .team = team,
        };
        send_lobby_message(*client_link,
                           role_ == LobbyRole::Host
                               ? host_client_transport_->local_peer_id()
                               : server_peer_id_,
                           std::move(message));
        pump_once();
        if (!state_.has_value())
            return false;
        const og::sim::LobbyPlayer* const echoed =
            find_player_by_seat_id(*state_, seat_id);
        return echoed != nullptr && echoed->team == team;
    }

    std::vector<std::uint8_t> local_player_indices() const override
    {
        if (!state_.has_value())
            return {};
        std::vector<std::uint8_t> indices;
        for (const og::sim::LobbyPlayer* const seat :
             find_local_seats(*state_))
        {
            indices.push_back(seat->player_index);
        }
        return indices;
    }

    bool set_ready(bool ready) override
    {
        og::sim::ITransport* const client_link = role_ == LobbyRole::Host
            ? host_client_transport_.get()
            : transport_.get();
        if (client_link == nullptr)
            return false;

        if (ready && pack_client_) {
            // Packs mount before play: give in-flight chunks one pump, then
            // refuse ready while a transfer is incomplete or failed. The
            // caller simply retries once the Log shows the pack installed.
            pump_once();
            if (pack_client_->busy() || pack_client_->failed()) {
                team_status_ = pack_client_->failed()
                    ? pack_client_->status_text()
                    : "Waiting for pack transfer";
                return false;
            }
        }

        og::sim::LobbyMessage message;
        message.payload = og::sim::LobbyReadyMessage{
            .player_index = local_player_index(),
            .ready = ready,
        };
        send_lobby_message(*client_link,
                           role_ == LobbyRole::Host
                               ? host_client_transport_->local_peer_id()
                               : server_peer_id_,
                           std::move(message));
        pump_once();
        return local_ready() == ready;
    }

    bool local_ready() const override
    {
        if (!state_.has_value())
            return false;
        const og::sim::LobbyPlayer* const local = find_local_player(*state_);
        return local != nullptr && local->ready;
    }

    std::vector<og::sim::LobbyPlayer> players() const override
    {
        if (!state_.has_value())
            return {};
        return state_->players;
    }

    // --- test accessors -------------------------------------------------------
    const std::optional<og::sim::LobbyState>& state() const { return state_; }
    std::size_t player_count() const
    {
        return state_.has_value() ? state_->players.size() : 0u;
    }
    bool start_negotiated() const { return start_negotiated_; }

private:
    void render(ITerminal& term)
    {
        term.clear();
        int row = 0;
        term.put_str(row++, 0, role_ == LobbyRole::Host ? "Hosting Game" : "Joining Game",
                     Color::White, Color::Default, true);
        ++row;
        for (const std::string& line : status_lines()) {
            if (row >= term.rows() - 1)
                break;
            term.put_str(row++, 0, line, Color::Default, Color::Default, false);
        }
        const char* hint = role_ == LobbyRole::Host
            ? "[s] start  [</>] seat  [t] team  [r] ready  [c] control  [q] cancel"
            : "[</>] seat  [t] team  [r] ready  [q] cancel";
        if (term.rows() > 0)
            term.put_str(term.rows() - 1, 0, hint, Color::Cyan, Color::Default, false);
        term.present();
    }

    bool local_player_is_host() const
    {
        if (!state_.has_value())
            return false;
        const og::sim::LobbyPlayer* const local = find_local_player(*state_);
        return local != nullptr &&
               (local->is_host || local->player_index == state_->host_player_id);
    }

    std::uint8_t local_player_index() const
    {
        // Informational only: the LobbyServer keys on the sending peer.
        if (!state_.has_value())
            return 0xffu;
        const og::sim::LobbyPlayer* const local = find_local_player(*state_);
        return local != nullptr ? local->player_index : 0xffu;
    }

    const og::sim::LobbyPlayer* selected_local_player() const
    {
        if (!state_.has_value())
            return nullptr;
        if (const og::sim::LobbyPlayer* const selected =
                find_player_by_seat_id(
                    *state_, selected_local_seat_id_);
            selected != nullptr &&
            std::find(state_->local_seat_ids.begin(),
                      state_->local_seat_ids.end(),
                      selected->seat_id) != state_->local_seat_ids.end())
        {
            return selected;
        }
        return find_local_player(*state_);
    }

    void ensure_selected_local_seat()
    {
        const og::sim::LobbyPlayer* const selected =
            selected_local_player();
        selected_local_seat_id_ = selected != nullptr
            ? selected->seat_id
            : og::sim::kInvalidLobbySeatId;
    }

    void select_local_seat(int direction)
    {
        if (!state_.has_value())
            return;
        const std::vector<const og::sim::LobbyPlayer*> seats =
            find_local_seats(*state_);
        if (seats.empty())
            return;

        auto selected_it = std::find_if(
            seats.begin(), seats.end(),
            [this](const og::sim::LobbyPlayer* const seat) {
                return seat->seat_id == selected_local_seat_id_;
            });
        std::size_t index = selected_it == seats.end()
            ? 0u
            : static_cast<std::size_t>(selected_it - seats.begin());
        if (direction < 0)
            index = (index + seats.size() - 1u) % seats.size();
        else if (direction > 0)
            index = (index + 1u) % seats.size();

        selected_local_seat_id_ = seats[index]->seat_id;
        last_team_request_ = -1;
        last_team_request_seat_id_ = og::sim::kInvalidLobbySeatId;
        team_status_ =
            "Selected P" +
            std::to_string(seats[index]->player_index + 1);
    }

    void handle_typed_message(const og::sim::TypedReceivedMessage& message)
    {
        if (pack_client_ && transport_ &&
            pack_client_->handle_message(*transport_, server_peer_id_,
                                         message)) {
            return;
        }
        switch (message.kind) {
        case og::sim::TypedReceivedMessageKind::LobbyState:
            if (message.lobby_state) {
                state_ = *message.lobby_state;
                ensure_selected_local_seat();
                if (const og::sim::LobbyPlayer* const local =
                        find_local_player(*state_)) {
                    local_team_ = local->team;
                }
                // A late accept (joiner echo) lands here. Resolve it by the
                // stable seat token so another owned seat's update cannot
                // masquerade as this request's result after P# reindexing.
                if (const og::sim::LobbyPlayer* const requested =
                        find_player_by_seat_id(
                            *state_, last_team_request_seat_id_);
                    requested != nullptr &&
                    requested->team == last_team_request_) {
                    last_team_request_ = -1;
                    last_team_request_seat_id_ =
                        og::sim::kInvalidLobbySeatId;
                    team_status_.clear();
                }
                if (pending_start_request_id_ != 0 &&
                    state_->last_start_request_id ==
                        pending_start_request_id_ &&
                    state_->last_start_denial !=
                        og::sim::start_denial_reason_value(
                            og::sim::StartDenialReason::None))
                {
                    switch (static_cast<og::sim::StartDenialReason>(
                        state_->last_start_denial))
                    {
                    case og::sim::StartDenialReason::NotHost:
                        team_status_ = "Only the host can start";
                        break;
                    case og::sim::StartDenialReason::MachinesNotReady:
                        team_status_ = "Waiting for other machines";
                        break;
                    case og::sim::StartDenialReason::NoDeployedCharacters:
                        team_status_ = "No one is deployed";
                        break;
                    default:
                        break;
                    }
                    pending_start_request_id_ = 0;
                }
            }
            break;
        case og::sim::TypedReceivedMessageKind::LobbyMessage:
            if (message.lobby_message &&
                message.lobby_message->kind() == og::sim::LobbyMessageKind::StartGame) {
                const auto& start =
                    std::get<og::sim::LobbyStartGameMessage>(
                        message.lobby_message->payload);
                if (pending_start_request_id_ != 0 &&
                    start.request_id != pending_start_request_id_)
                {
                    break;
                }
                pending_start_request_id_ = 0;
                start_negotiated_ = true;
            }
            break;
        default:
            break;
        }
    }

    void pump_once()
    {
        if (server_ != nullptr)
            server_->poll_incoming_messages();

        // The host listens on its loopback client transport; the joiner on its
        // remote transport. Send the joiner's join once it has a peer.
        og::sim::ITransport* const client_link =
            role_ == LobbyRole::Host ? host_client_transport_.get() : transport_.get();
        if (client_link == nullptr)
            return;

        if (role_ == LobbyRole::Join && !join_sent_ &&
            !client_link->connected_peers().empty()) {
            // The settings message is honored by the LobbyServer only if THIS
            // peer is the host (e.g. after relay host-migration); a regular
            // joiner's settings are dropped, so it inherits the host's campaign/
            // level/difficulty from the broadcast LobbyState. The join carries
            // this peer's own roster.
            send_lobby_message(*client_link, server_peer_id_,
                               make_settings_message(save_, difficulty_));
            send_lobby_message(*client_link, server_peer_id_,
                               make_join_message(save_, player_name_, local_team_));
            join_sent_ = true;
        }

        for (const og::sim::TypedReceivedMessage& message :
             poll_lobby_transport_messages(*client_link)) {
            handle_typed_message(message);
        }

        // The host's LobbyServer reports the start request locally too.
        if (role_ == LobbyRole::Host && server_ != nullptr &&
            server_->start_game_requested()) {
            start_negotiated_ = true;
        }
    }

    void build_session_if_needed()
    {
        if (!start_negotiated_ || session_ != nullptr || session_built_failed_ ||
            session_taken_)
            return;

        std::string err;
        if (role_ == LobbyRole::Host) {
            if (server_ == nullptr || host_client_transport_ == nullptr) {
                session_built_failed_ = true;
                return;
            }
            const og::sim::LobbySaveDataEquivalent lobby_save =
                server_->build_save_data_equivalent();
            const std::vector<og::sim::LobbyPlayerBinding> bindings =
                server_->build_player_bindings();
            std::uint8_t host_index = 0;
            if (const og::sim::LobbyPlayer* const local =
                    state_.has_value() ? find_local_player(*state_) : nullptr) {
                host_index = local->player_index;
            }
            session_ = HostCursesSession::create(lobby_save, bindings, difficulty_,
                                                 combined_transport_, host_client_transport_,
                                                 host_index, &err);
        } else {
            if (!state_.has_value() || transport_ == nullptr) {
                session_built_failed_ = true;
                return;
            }
            // Build the equivalent the joiner will spawn from the negotiated state.
            og::sim::LobbySaveDataEquivalent lobby_save = build_join_save_equivalent();
            std::size_t join_index = 0;
            if (const og::sim::LobbyPlayer* const local =
                    find_local_player(*state_)) {
                join_index = local->player_index;
            }
            session_ = JoinCursesSession::create(lobby_save, difficulty_,
                                                 transport_, server_peer_id_,
                                                 join_index, &err);
        }
        if (session_ == nullptr)
            session_built_failed_ = true;
    }

    // The joiner spawns the same world the host negotiated. Reconstruct the
    // lobby-equivalent from the last LobbyState (campaign/scenario + full roster).
    og::sim::LobbySaveDataEquivalent build_join_save_equivalent() const
    {
        if (!state_.has_value())
            return {};
        return build_join_save_equivalent_from_state(*state_);
    }

    void teardown()
    {
        session_.reset();
        server_.reset();
        state_.reset();
        combined_transport_.reset();
        ws_server_.reset();
        relay_.reset();
        host_client_transport_.reset();
        loopback_server_.reset();
        pack_client_.reset();
        transport_.reset();
        start_negotiated_ = false;
        session_built_failed_ = false;
        session_taken_ = false;
        join_sent_ = false;
        pending_start_request_id_ = 0;
    }

    LobbyRole role_;
    SaveData& save_;
    int difficulty_ = 1;
    short local_team_ = 0;
    // 't'-cycle bookkeeping: selected/pending seats use server-issued tokens
    // so dense P# changes cannot redirect an in-flight team choice.
    og::sim::LobbySeatId selected_local_seat_id_ =
        og::sim::kInvalidLobbySeatId;
    og::sim::LobbySeatId last_team_request_seat_id_ =
        og::sim::kInvalidLobbySeatId;
    short last_team_request_ = -1;
    std::string team_status_;
    std::string player_name_;
    std::uint32_t next_start_request_id_ = 1;
    std::uint32_t pending_start_request_id_ = 0;

    // Host transports.
    std::shared_ptr<og::sim::InProcessTransport> loopback_server_;
    std::shared_ptr<og::sim::InProcessTransport> host_client_transport_;
    std::shared_ptr<og::sim::ITransport> combined_transport_;
    std::shared_ptr<og::sim::WebSocketServerTransport> ws_server_;
    std::shared_ptr<og::sim::RelayWebSocketTransport> relay_;
    std::unique_ptr<og::sim::LobbyServer> server_;

    // Join transport.
    std::shared_ptr<og::sim::ITransport> transport_;
    og::sim::PeerId server_peer_id_ = 1;
    bool join_sent_ = false;
    // Class-pack transfer collector (protocol v10), joiner role only.
    std::unique_ptr<og::sim::PackTransferClient> pack_client_;

    std::optional<og::sim::LobbyState> state_;
    bool start_negotiated_ = false;
    bool cancelled_ = false;
    bool session_built_failed_ = false;
    bool session_taken_ = false;
    std::unique_ptr<CursesGameSession> session_;
};

} // namespace

#ifdef TESTING
#include "../../../tests/curses/curses_network_internal.inc"
#endif

std::unique_ptr<CursesLobby> make_host_lobby(SaveData& save, const HostOptions& opt,
                                             std::string* error)
{
    if (error)
        error->clear();
    auto lobby = std::make_unique<CursesLobbyImpl>(LobbyRole::Host, save, opt.difficulty);
    if (!lobby->init_host(opt, error))
        return nullptr;
    return lobby;
}

std::unique_ptr<CursesLobby> make_join_lobby(SaveData& save, const JoinOptions& opt,
                                             std::string* error)
{
    if (error)
        error->clear();
    auto lobby = std::make_unique<CursesLobbyImpl>(LobbyRole::Join, save, /*difficulty=*/1);
    if (!lobby->init_join(opt, error))
        return nullptr;
    return lobby;
}

GameRunResult run_curses_lobby(CursesLobby& lobby, ITerminal& term, IClock& clock)
{
    for (;;) {
        if (lobby.poll(term, clock))
            break; // a game start was negotiated
        if (lobby.cancelled())
            return GameRunResult{}; // the user backed out of the lobby
        clock.sleep_ms(30);
    }

    std::unique_ptr<CursesGameSession> session = lobby.take_session();
    if (session == nullptr)
        return GameRunResult{};

    CursesInput input;
    CursesRenderer renderer;
    return run_level_loop(*session, term, clock, input, renderer, LevelLoopOptions{});
}

// =====================================================================
// Test-only hooks (declared extern in tests/curses/test_curses_network.cpp).
// These let a single-process test drive the full host+join handshake and the
// networked sessions over an InProcessTransport, with no real sockets.
// =====================================================================

std::unique_ptr<CursesLobby> make_host_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> combined_transport,
    std::shared_ptr<og::sim::InProcessTransport> host_client_transport)
{
    auto lobby = std::make_unique<CursesLobbyImpl>(LobbyRole::Host, save, difficulty);
    lobby->init_host_over_transport(std::move(combined_transport),
                                    std::move(host_client_transport));
    return lobby;
}

std::unique_ptr<CursesLobby> make_join_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> transport,
    og::sim::PeerId server_peer_id)
{
    auto lobby = std::make_unique<CursesLobbyImpl>(LobbyRole::Join, save, difficulty);
    lobby->init_join_over_transport(std::move(transport), server_peer_id);
    return lobby;
}

#ifdef TESTING
og::sim::LobbySaveDataEquivalent
curses_network_testing_build_join_save_equivalent(
    const og::sim::LobbyState& state)
{
    return build_join_save_equivalent_from_state(state);
}

bool curses_network_testing_inject_mode(CursesGameSession& session,
                                        short requested_respawn_ticks)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    if (host == nullptr)
        return false;
    return host->inject_mode_scenario_for_testing(requested_respawn_ticks);
}

int curses_network_testing_force_server_win(CursesGameSession& session,
                                            std::uint32_t pinned_team0_score)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    if (host == nullptr)
        return -1;
    return host->force_server_win_for_testing(pinned_team0_score);
}

int curses_network_testing_clear_server_team(CursesGameSession& session,
                                             short team)
{
    auto* const host = dynamic_cast<HostCursesSession*>(&session);
    if (host == nullptr)
        return -1;
    return host->clear_server_team_for_testing(team);
}
#endif

} // namespace og::curses
