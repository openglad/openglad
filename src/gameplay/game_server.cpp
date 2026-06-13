#include <openglad/gameplay/game_server.h>

#include <openglad/core/constants.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstddef>
#include <format>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace
{

using EntityListKind = og::sim::SnapshotEntityListKind;
using PendingRemovedEntity = og::sim::PendingRemovedEntity;

constexpr std::size_t kMaxKeyframesPerTick = 2;

struct ServerPollResult {
    std::vector<og::sim::TypedReceivedMessage> messages;
    std::vector<og::sim::PeerId> malformed_peers;
};

bool contains_entity_id(const std::vector<std::uint32_t>& ids,
                        std::uint32_t entity_id)
{
    return std::find(ids.begin(), ids.end(), entity_id) != ids.end();
}

void erase_entity_id(std::vector<std::uint32_t>& ids, std::uint32_t entity_id)
{
    ids.erase(std::remove(ids.begin(), ids.end(), entity_id), ids.end());
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

bool typed_messages_include_peer(
    const std::vector<og::sim::TypedReceivedMessage>& messages,
    og::sim::PeerId peer_id)
{
    return std::any_of(messages.begin(), messages.end(),
                       [peer_id](const og::sim::TypedReceivedMessage& message) {
                           return message.peer_id == peer_id;
                       });
}

bool dirty_mask_has_bits(const og::sim::EntitySnapshotDirtyMask& mask) noexcept
{
    return mask[0] != 0 || mask[1] != 0;
}

bool is_removed_entity_sentinel(const og::sim::EntitySnapshot& entity) noexcept
{
    return entity.dirty_mask[0] == 0 && entity.dirty_mask[1] == 0;
}

bool contains_removed_entity(
    const std::vector<PendingRemovedEntity>& removed_entities,
    std::uint32_t entity_id)
{
    return std::find_if(
               removed_entities.begin(), removed_entities.end(),
               [entity_id](const PendingRemovedEntity& removed_entity) {
                   return removed_entity.entity_id == entity_id;
               }) != removed_entities.end();
}

void upsert_grid_dirty_tile(std::vector<og::sim::GridTileSnapshot>& tiles,
                            const og::sim::GridTileSnapshot& tile)
{
    const auto it = std::find_if(
        tiles.begin(), tiles.end(),
        [&tile](const og::sim::GridTileSnapshot& existing) {
            return existing.x == tile.x && existing.y == tile.y;
        });
    if (it == tiles.end())
    {
        tiles.push_back(tile);
    }
    else
    {
        *it = tile;
    }
}

void accumulate_grid_for_client(og::sim::PerClientState& client_state,
                                const og::sim::WorldSnapshot& snapshot)
{
    if (snapshot.grid_full_resend)
    {
        client_state.pending_grid_dirty = true;
        client_state.pending_grid_full_resend = true;
        client_state.pending_full_grid_data = snapshot.full_grid_data;
        client_state.pending_grid_dirty_tiles.clear();
    }

    if (snapshot.grid_dirty)
        client_state.pending_grid_dirty = true;

    for (const auto& tile : snapshot.grid_dirty_tiles)
        upsert_grid_dirty_tile(client_state.pending_grid_dirty_tiles, tile);
}

void collect_snapshot_entity_lists(
    const og::sim::WorldSnapshot& snapshot,
    std::unordered_map<std::uint32_t, EntityListKind>& entity_lists)
{
    entity_lists.clear();
    entity_lists.reserve(snapshot.oblist.size() + snapshot.fxlist.size() +
                         snapshot.weaplist.size());

    const auto collect =
        [&entity_lists](const auto& entities, EntityListKind list_kind) {
            for (const auto& entity : entities)
                entity_lists[entity.entity_id] = list_kind;
        };

    collect(snapshot.oblist, EntityListKind::Ob);
    collect(snapshot.fxlist, EntityListKind::Fx);
    collect(snapshot.weaplist, EntityListKind::Weap);
}

std::vector<og::sim::EntitySnapshot> select_delta_entities(
    const std::vector<og::sim::EntitySnapshot>& current_entities,
    const og::sim::PerClientState& client_state)
{
    std::vector<og::sim::EntitySnapshot> selected;
    selected.reserve(current_entities.size());

    for (const auto& entity : current_entities)
    {
        og::sim::EntitySnapshot delta_entity = entity;
        if (contains_entity_id(client_state.new_entity_ids, entity.entity_id))
        {
            delta_entity.dirty_mask[0] = ~0ULL;
            delta_entity.dirty_mask[1] = ~0ULL;
            selected.push_back(delta_entity);
            continue;
        }

        const auto mask_it = client_state.accumulated_dirty.find(entity.entity_id);
        if (mask_it != client_state.accumulated_dirty.end() &&
            dirty_mask_has_bits(mask_it->second))
        {
            delta_entity.dirty_mask[0] = mask_it->second[0];
            delta_entity.dirty_mask[1] = mask_it->second[1];
        }
        else
        {
            delta_entity.dirty_mask[0] = 1ULL << og::dirty::BIT_ENTITY_ID;
            delta_entity.dirty_mask[1] = 0;
        }
        selected.push_back(delta_entity);
    }

    return selected;
}

void append_removed_entity_sentinels(
    std::vector<og::sim::EntitySnapshot>& entities,
    const std::vector<PendingRemovedEntity>& removed_entities,
    EntityListKind list_kind)
{
    for (const PendingRemovedEntity& removed_entity : removed_entities)
    {
        if (removed_entity.list_kind != list_kind)
            continue;

        og::sim::EntitySnapshot sentinel;
        sentinel.entity_id = removed_entity.entity_id;
        sentinel.guy_id = og::sim::kNoGuyId;
        entities.push_back(sentinel);
    }
}

void collect_removed_entity_ids(
    const std::vector<og::sim::EntitySnapshot>& entities,
    std::vector<std::uint32_t>& removed_entity_ids)
{
    for (const auto& entity : entities)
    {
        if (is_removed_entity_sentinel(entity))
            removed_entity_ids.push_back(entity.entity_id);
    }
}

void remember_sent_entity_lists(og::sim::PerClientState& client_state,
                                const std::vector<og::sim::EntitySnapshot>& entities,
                                EntityListKind list_kind)
{
    for (const auto& entity : entities)
    {
        if (is_removed_entity_sentinel(entity))
            continue;
        client_state.known_entity_lists[entity.entity_id] = list_kind;
    }
}

void populate_special_names(
    std::string (&special_names)[NUM_FAMILIES][NUM_SPECIALS])
{
    for (int family = 0; family < NUM_FAMILIES; ++family)
    {
        const FamilyDescriptor* descriptor = get_family_descriptor(family);
        for (int special = 0; special < NUM_SPECIALS; ++special)
        {
            special_names[family][special] =
                descriptor ? descriptor->special_names[special] : "NONE";
        }
    }
}

bool player_input_has_activity(const PlayerInput& input)
{
    for (int key = 0; key < NUM_INPUT_KEYS; ++key)
    {
        if (input.held[key] || input.pressed[key])
            return true;
    }
    return false;
}

const PlayerInput& select_player_input(const InputState& input,
                                       std::size_t player_index)
{
    const std::size_t bounded_index = std::min(
        player_index, static_cast<std::size_t>(MAX_PLAYERS - 1));
    if (player_input_has_activity(input.players[bounded_index]))
        return input.players[bounded_index];

    const PlayerInput* only_active_input = nullptr;
    for (int index = 0; index < MAX_PLAYERS; ++index)
    {
        if (!player_input_has_activity(input.players[index]))
            continue;
        if (only_active_input != nullptr)
            return input.players[bounded_index];
        only_active_input = &input.players[index];
    }

    return only_active_input != nullptr ? *only_active_input
                                        : input.players[bounded_index];
}

bool contains_event_kind(const og::sim::SimEventBatch& batch,
                         og::sim::EventKind kind) noexcept
{
    return std::any_of(
        batch.events.begin(),
        batch.events.end(),
        [kind](const og::sim::Event& event) { return event.kind == kind; });
}

bool contains_endgame_event(const og::sim::SimEventBatch& batch) noexcept
{
    return contains_event_kind(batch, og::sim::EventKind::EndGame);
}

std::uint8_t palette_id_from_event_value(std::uint32_t value) noexcept
{
    return value == 0u ? 0u : 1u;
}

void apply_authoritative_event_state(GameWorld& world,
                                     const og::sim::SimEventBatch& batch,
                                     og::sim::WorldSnapshot& snapshot)
{
    for (const auto& event : batch.events)
    {
        switch (event.kind)
        {
        case og::sim::EventKind::SetPalette:
        {
            const std::uint8_t palette_id = palette_id_from_event_value(event.a);
            world.current_palette_id = palette_id;
            snapshot.current_palette_id = palette_id;
            break;
        }
        case og::sim::EventKind::DamageTile:
        {
            // Tile damage is a deterministic world-state mutation routed
            // through the sim event log (matching master, where the screen
            // event consumer calls damage_tile). The authoritative server
            // applies it here so the grid mutates server-side and the change
            // propagates to clients via the snapshot grid-sync. The snapshot
            // for this tick has already been captured, so fold the damaged
            // tile into it directly (mirroring the SetPalette pattern) instead
            // of stranding it for the next keyframe.
            const short xpix = static_cast<short>(event.a);
            const short ypix = static_cast<short>(event.b);
            if (world.damage_tile(xpix, ypix) && world.grid.valid())
            {
                const short tx = static_cast<short>(xpix / GRID_SIZE);
                const short ty = static_cast<short>(ypix / GRID_SIZE);
                if (tx >= 0 && ty >= 0 && tx < world.grid.w && ty < world.grid.h)
                {
                    const std::size_t gi =
                        static_cast<std::size_t>(ty) * world.grid.w + tx;
                    const std::uint8_t value = world.grid.data[gi];
                    bool present = false;
                    for (auto& tile : snapshot.grid_dirty_tiles)
                    {
                        if (tile.x == tx && tile.y == ty)
                        {
                            tile.value = value;
                            present = true;
                            break;
                        }
                    }
                    if (!present && !snapshot.grid_full_resend)
                    {
                        snapshot.grid_dirty = true;
                        snapshot.grid_dirty_tiles.push_back(
                            {tx, ty, value});
                    }
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

template <typename EntityList>
void consume_hurt_flash_in_entities(const EntityList& entities)
{
    for (const auto& entry : entities)
    {
        walker* const entity = entry.get();
        if (entity != nullptr && entity->hurt_flash())
            entity->set_hurt_flash(false);
    }
}

void consume_authoritative_visual_transients(GameWorld& world,
                                             og::sim::SnapshotCaptureMode mode)
{
    if (mode != og::sim::SnapshotCaptureMode::Consume)
        return;

    consume_hurt_flash_in_entities(world.oblist);
    consume_hurt_flash_in_entities(world.fxlist);
    consume_hurt_flash_in_entities(world.weaplist);
}

ServerPollResult poll_server_messages(
    og::sim::ITransport& transport)
{
    ServerPollResult result;
    if (transport.supports_typed_messages())
    {
        std::unordered_set<og::sim::PeerId> malformed_peers;
        for (og::sim::TypedReceivedMessage& message : transport.poll_typed())
        {
            if (message.kind == og::sim::TypedReceivedMessageKind::Malformed)
            {
                malformed_peers.insert(message.peer_id);
                continue;
            }
            if (malformed_peers.contains(message.peer_id))
                continue;

            result.messages.push_back(std::move(message));
        }
        result.malformed_peers.assign(malformed_peers.begin(), malformed_peers.end());
        std::sort(result.malformed_peers.begin(), result.malformed_peers.end());
        return result;
    }

    std::unordered_set<og::sim::PeerId> malformed_peers;
    for (const auto& message : transport.poll())
    {
        if (malformed_peers.contains(message.peer_id))
            continue;

        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
        {
            malformed_peers.insert(message.peer_id);
            continue;
        }

        og::sim::TypedReceivedMessage typed_message;
        typed_message.peer_id = message.peer_id;
        try
        {
            switch (envelope.message_type)
            {
            case og::sim::kLobbyMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_lobby_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::LobbyMessage;
                typed_message.lobby_message =
                    std::make_shared<og::sim::LobbyMessage>(*decoded);
                break;
            }

            case og::sim::kLobbyStateMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_lobby_state_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::LobbyState;
                typed_message.lobby_state =
                    std::make_shared<og::sim::LobbyState>(*decoded);
                break;
            }

            case og::sim::kInputMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_input_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }

                typed_message.kind = og::sim::TypedReceivedMessageKind::Input;
                typed_message.input = std::make_shared<InputState>(decoded->input);
                typed_message.tick = decoded->tick;
                break;
            }

            case og::sim::kHelloMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_hello_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind = og::sim::TypedReceivedMessageKind::Hello;
                typed_message.hello =
                    std::make_shared<og::sim::HelloMessage>(*decoded);
                break;
            }

            case og::sim::kClientReadyMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_client_ready_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::ClientReady;
                typed_message.client_ready =
                    std::make_shared<og::sim::ClientReadyMessage>(*decoded);
                break;
            }

            case og::sim::kKeyframeRequestMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_keyframe_request_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::KeyframeRequest;
                typed_message.keyframe_request =
                    std::make_shared<og::sim::KeyframeRequestMessage>(*decoded);
                break;
            }

            case og::sim::kHeartbeatMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_heartbeat_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::Heartbeat;
                typed_message.heartbeat =
                    std::make_shared<og::sim::HeartbeatMessage>(*decoded);
                break;
            }

            case og::sim::kExitPromptResponseMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_exit_prompt_response_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::ExitPromptResponse;
                typed_message.exit_prompt_response =
                    std::make_shared<og::sim::ExitPromptResponseMessage>(*decoded);
                break;
            }

            case og::sim::kPauseBroadcastMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_pause_broadcast_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::PauseBroadcast;
                typed_message.pause_broadcast =
                    std::make_shared<og::sim::PauseBroadcastMessage>(*decoded);
                break;
            }

            case og::sim::kPauseResponseMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_pause_response_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::PauseResponse;
                typed_message.pause_response =
                    std::make_shared<og::sim::PauseResponseMessage>(*decoded);
                break;
            }

            case og::sim::kSnapshotHashCheckMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_snapshot_hash_check_message(message.data);
                if (!decoded.has_value())
                {
                    malformed_peers.insert(message.peer_id);
                    continue;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::SnapshotHashCheck;
                typed_message.snapshot_hash_check =
                    std::make_shared<og::sim::SnapshotHashCheckMessage>(*decoded);
                break;
            }

            default:
                malformed_peers.insert(message.peer_id);
                continue;
            }
        }
        catch (const std::exception&)
        {
            malformed_peers.insert(message.peer_id);
            continue;
        }

        result.messages.push_back(std::move(typed_message));
    }

    result.malformed_peers.assign(malformed_peers.begin(), malformed_peers.end());
    std::sort(result.malformed_peers.begin(), result.malformed_peers.end());
    return result;
}

og::sim::WorldSnapshot capture_server_snapshot(GameWorld& world,
                                               og::sim::SnapshotCaptureMode mode)
{
    if (mode == og::sim::SnapshotCaptureMode::Peek)
        return og::sim::peek_snapshot(world);
    return og::sim::capture_snapshot(world);
}

og::sim::WorldSnapshot capture_server_keyframe(GameWorld& world,
                                               og::sim::SnapshotCaptureMode mode)
{
    if (mode == og::sim::SnapshotCaptureMode::Peek)
        return og::sim::peek_keyframe_snapshot(world);
    return og::sim::capture_keyframe_snapshot(world);
}

std::vector<og::sim::InitialSetupGuyData> collect_initial_setup_guys(
    const GameWorld& world)
{
    std::unordered_set<std::int32_t> seen_ids;
    std::vector<og::sim::InitialSetupGuyData> guys;

    const auto collect = [&seen_ids, &guys](const auto& entities) {
        for (const auto& entry : entities)
        {
            const walker* const entity = entry.get();
            if (entity == nullptr || entity->myguy == nullptr)
                continue;

            const guy& source = *entity->myguy;
            if (!seen_ids.insert(source.id).second)
                continue;

            og::sim::InitialSetupGuyData data;
            data.guy_id = source.id;
            data.name = source.name;
            data.family = source.family;
            data.strength = source.strength;
            data.dexterity = source.dexterity;
            data.constitution = source.constitution;
            data.intelligence = source.intelligence;
            data.armor = source.armor;
            data.exp = source.exp;
            data.kills = source.kills;
            data.level_kills = source.level_kills;
            data.total_damage = source.total_damage;
            data.total_hits = source.total_hits;
            data.total_shots = source.total_shots;
            data.teamnum = source.teamnum;
            data.scen_damage = source.scen_damage;
            data.scen_kills = source.scen_kills;
            data.scen_damage_taken = source.scen_damage_taken;
            data.scen_min_hp = source.scen_min_hp;
            data.scen_shots = source.scen_shots;
            data.scen_hits = source.scen_hits;
            data.level = source.level;
            guys.push_back(std::move(data));
        }
    };

    collect(world.oblist);
    collect(world.fxlist);
    collect(world.weaplist);
    return guys;
}

bool has_living_member_for_any_bound_team(
    const GameWorld& world,
    const std::unordered_map<og::sim::PeerId, og::sim::ConnectedClientState>& clients,
    const std::vector<og::sim::DisconnectedPlayer>& disconnected_players) noexcept
{
    std::array<bool, MAX_TEAM + 1> tracked_teams = {};
    auto track_team = [&tracked_teams](short team_num) {
        if (team_num >= 0 &&
            team_num < static_cast<short>(tracked_teams.size()))
        {
            tracked_teams[static_cast<std::size_t>(team_num)] = true;
        }
    };

    for (const auto& [peer_id, client] : clients)
    {
        (void)peer_id;
        if (client.has_player_binding)
            track_team(client.team_num);
    }

    for (const og::sim::DisconnectedPlayer& disconnected : disconnected_players)
        track_team(disconnected.team_num);

    for (const auto& uptr : world.oblist)
    {
        const walker* const entity = uptr.get();
        if (entity == nullptr || entity->dead() ||
            entity->query_order() != Order::Living)
        {
            continue;
        }

        const unsigned char team_num = entity->team_num();
        if (team_num < tracked_teams.size() && tracked_teams[team_num])
            return true;
    }

    return false;
}

std::string player_name_from_control(const walker* control,
                                     std::size_t player_index)
{
    if (control != nullptr)
    {
        if (control->myguy != nullptr && !control->myguy->name.empty())
            return control->myguy->name;
        if (control->stats() != nullptr && !control->stats()->name.empty())
            return control->stats()->name;
    }

    return std::format("Player {}", player_index + 1);
}

void clear_pressed(PlayerInput& input)
{
    for (bool& pressed : input.pressed)
        pressed = false;
}

bool session_tokens_equal(const og::sim::SessionToken& lhs,
                          const og::sim::SessionToken& rhs) noexcept
{
    std::uint8_t diff = 0;
    for (std::size_t index = 0; index < lhs.size(); ++index)
        diff |= static_cast<std::uint8_t>(lhs[index] ^ rhs[index]);
    return diff == 0;
}

} // namespace

namespace og::sim {

void reset_client_snapshot_state(PerClientState& client_state) noexcept
{
    client_state.last_sent_tick = 0;
    client_state.accumulated_dirty.clear();
    client_state.new_entity_ids.clear();
    client_state.removed_entities.clear();
    client_state.known_entity_lists.clear();
    client_state.pending_grid_dirty = false;
    client_state.pending_grid_full_resend = false;
    client_state.pending_full_grid_data.clear();
    client_state.pending_grid_dirty_tiles.clear();
}

void seed_client_snapshot_baseline(PerClientState& client_state,
                                   const WorldSnapshot& keyframe)
{
    reset_client_snapshot_state(client_state);
    client_state.last_sent_tick = keyframe.tick_count;
    collect_snapshot_entity_lists(keyframe, client_state.known_entity_lists);
}

void accumulate_snapshot_for_client(PerClientState& client_state,
                                    const WorldSnapshot& snapshot)
{
    accumulate_grid_for_client(client_state, snapshot);

    auto accumulate_entities = [&client_state](const auto& entities,
                                               EntityListKind list_kind) {
        for (const auto& entity : entities)
        {
            auto known_it = client_state.known_entity_lists.find(entity.entity_id);
            if (known_it == client_state.known_entity_lists.end())
            {
                if (!contains_entity_id(client_state.new_entity_ids,
                                        entity.entity_id))
                {
                    client_state.new_entity_ids.push_back(entity.entity_id);
                }
                continue;
            }

            known_it->second = list_kind;
            if (contains_entity_id(client_state.new_entity_ids, entity.entity_id))
                continue;

            if (entity.dirty_mask[0] == 0 && entity.dirty_mask[1] == 0)
                continue;

            EntitySnapshotDirtyMask& accumulated =
                client_state.accumulated_dirty[entity.entity_id];
            accumulated[0] |= entity.dirty_mask[0];
            accumulated[1] |= entity.dirty_mask[1];
        }
    };

    accumulate_entities(snapshot.oblist, EntityListKind::Ob);
    accumulate_entities(snapshot.fxlist, EntityListKind::Fx);
    accumulate_entities(snapshot.weaplist, EntityListKind::Weap);

    for (std::uint32_t removed_id : snapshot.removed_entity_ids)
    {
        client_state.accumulated_dirty.erase(removed_id);
        if (contains_entity_id(client_state.new_entity_ids, removed_id))
        {
            erase_entity_id(client_state.new_entity_ids, removed_id);
            continue;
        }

        const auto known_it = client_state.known_entity_lists.find(removed_id);
        if (known_it == client_state.known_entity_lists.end())
            continue;

        const PendingRemovedEntity removed_entity = {
            removed_id,
            known_it->second,
        };
        client_state.known_entity_lists.erase(known_it);

        if (!contains_removed_entity(client_state.removed_entities, removed_id))
            client_state.removed_entities.push_back(removed_entity);
    }
}

WorldSnapshot consume_delta_snapshot_for_client(PerClientState& client_state,
                                                const WorldSnapshot& snapshot)
{
    WorldSnapshot delta = snapshot;
    delta.grid_dirty = client_state.pending_grid_dirty;
    delta.grid_full_resend = client_state.pending_grid_full_resend;
    delta.full_grid_data = client_state.pending_grid_full_resend
        ? client_state.pending_full_grid_data
        : std::vector<std::uint8_t>{};
    delta.grid_dirty_tiles = client_state.pending_grid_dirty_tiles;
    delta.oblist = select_delta_entities(snapshot.oblist, client_state);
    delta.fxlist = select_delta_entities(snapshot.fxlist, client_state);
    delta.weaplist = select_delta_entities(snapshot.weaplist, client_state);
    append_removed_entity_sentinels(delta.oblist, client_state.removed_entities,
                                    EntityListKind::Ob);
    append_removed_entity_sentinels(delta.fxlist, client_state.removed_entities,
                                    EntityListKind::Fx);
    append_removed_entity_sentinels(delta.weaplist, client_state.removed_entities,
                                    EntityListKind::Weap);

    delta.removed_entity_ids.clear();
    delta.removed_entity_ids.reserve(client_state.removed_entities.size());
    collect_removed_entity_ids(delta.oblist, delta.removed_entity_ids);
    collect_removed_entity_ids(delta.fxlist, delta.removed_entity_ids);
    collect_removed_entity_ids(delta.weaplist, delta.removed_entity_ids);

    remember_sent_entity_lists(client_state, delta.oblist, EntityListKind::Ob);
    remember_sent_entity_lists(client_state, delta.fxlist, EntityListKind::Fx);
    remember_sent_entity_lists(client_state, delta.weaplist, EntityListKind::Weap);

    client_state.last_sent_tick = snapshot.tick_count;
    client_state.accumulated_dirty.clear();
    client_state.new_entity_ids.clear();
    client_state.removed_entities.clear();
    client_state.pending_grid_dirty = false;
    client_state.pending_grid_full_resend = false;
    client_state.pending_full_grid_data.clear();
    client_state.pending_grid_dirty_tiles.clear();
    return delta;
}

GameServer::GameServer(GameWorld& world, SimEventLog& events, ITransport& transport)
    : world_(world)
    , events_(events)
    , transport_(transport)
{
    populate_special_names(special_names_);
    wall_clock_ms_source_ = [] {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    };
}

void GameServer::set_wall_clock_ms_source(std::function<std::uint64_t()> source)
{
    wall_clock_ms_source_ = std::move(source);
}

std::uint64_t GameServer::now_ms() const
{
    return wall_clock_ms_source_ ? wall_clock_ms_source_() : 0;
}

SessionToken GameServer::allocate_session_token()
{
    SessionToken token = kZeroSessionToken;
    std::random_device random_device;
    while (is_zero_session_token(token))
    {
        for (std::size_t index = 0; index < token.size(); ++index)
        {
            token[index] =
                static_cast<std::uint8_t>(random_device() & 0xffu);
        }
    }
    return token;
}

void GameServer::connect_client(PeerId peer_id)
{
    erase_peer_id_sorted(pending_transport_disconnects_, peer_id);
    ConnectedClientState& client = clients_[peer_id];
    client.last_received_input_ms = now_ms();
    if (!host_peer_id_.has_value() && clients_.size() == 1u)
        host_peer_id_ = peer_id;
}

void GameServer::handle_transport_disconnect(
    PeerId peer_id,
    bool close_transport,
    std::optional<std::uint64_t> grace_started_at_ms)
{
    erase_peer_id_sorted(connected_transport_peers_, peer_id);
    erase_peer_id_sorted(pending_transport_disconnects_, peer_id);
    const bool was_host = host_peer_id_.has_value() && *host_peer_id_ == peer_id;
    if (was_host)
        host_peer_id_.reset();

    const auto it = clients_.find(peer_id);
    if (it == clients_.end())
    {
        if (close_transport)
            transport_.disconnect(peer_id);
        return;
    }

    const ConnectedClientState client = it->second;

    // Tell the remaining players that someone dropped — a transient on-field
    // banner like the other in-game event notifications. Only while the level is
    // live: during teardown every client disconnects and there is no display
    // left to read it (and a level-end is not a "disconnect").
    if (client.has_player_binding && world_.end == 0 && !world_.game_ended)
    {
        events_.push_notification(
            std::format("Player {} disconnected", client.player_index + 1), 40);
    }

    if (pending_pause_state_.has_value() &&
        pending_pause_state_->player_index == client.player_index)
    {
        clear_pause_state();
    }
    if (pending_exit_prompt_state_.has_value() &&
        pending_exit_prompt_state_->triggering_player_index == client.player_index)
    {
        clear_pending_exit_prompt();
    }

    if (client.has_player_binding && !is_zero_session_token(client.session_token))
    {
        const auto existing = std::find_if(
            disconnected_players_.begin(),
            disconnected_players_.end(),
            [&client](const DisconnectedPlayer& disconnected) {
                return disconnected.player_index == client.player_index ||
                    session_tokens_equal(disconnected.session_token,
                                         client.session_token);
            });

        DisconnectedPlayer replacement;
        replacement.session_token = client.session_token;
        replacement.player_index = client.player_index;
        replacement.team_num = client.team_num;
        replacement.control = client.control;
        replacement.repeated_input = client.last_known_input;
        std::optional<std::uint32_t> newest_pending_tick = std::nullopt;
        for (const auto& [tick, pending_input] : client.pending_inputs)
        {
            if (!newest_pending_tick.has_value() || tick > *newest_pending_tick)
            {
                newest_pending_tick = tick;
                replacement.repeated_input = pending_input;
            }
        }
        clear_pressed(replacement.repeated_input);
        replacement.grace_started_at_ms =
            grace_started_at_ms.value_or(now_ms());
        replacement.disconnected_at_ms = now_ms();
        replacement.ai_control_enabled = false;
        replacement.was_host = was_host;

        if (existing != disconnected_players_.end())
            *existing = replacement;
        else
            disconnected_players_.push_back(replacement);
    }
    else if (client.has_player_binding && client.control != nullptr &&
             client.control->user() == static_cast<int>(client.player_index))
    {
        client.control->set_user(-1);
        client.control->restore_act_type();
    }

    clients_.erase(it);
    if (close_transport)
        transport_.disconnect(peer_id);
}

void GameServer::disconnect_client(PeerId peer_id)
{
    const bool was_host = host_peer_id_.has_value() && *host_peer_id_ == peer_id;
    std::vector<PeerId> peers_to_disconnect;
    if (was_host)
    {
        peers_to_disconnect.reserve(clients_.size());
        for (const auto& [other_peer_id, client] : clients_)
        {
            (void)client;
            if (other_peer_id != peer_id)
                peers_to_disconnect.push_back(other_peer_id);
        }
        host_peer_id_.reset();
    }

    const auto disconnect_one = [this](PeerId disconnected_peer_id) {
        erase_peer_id_sorted(connected_transport_peers_, disconnected_peer_id);
        erase_peer_id_sorted(pending_transport_disconnects_, disconnected_peer_id);

        std::optional<std::size_t> disconnected_player_index = std::nullopt;
        const auto it = clients_.find(disconnected_peer_id);
        if (it != clients_.end())
        {
            const ConnectedClientState& client = it->second;
            if (client.has_player_binding)
                disconnected_player_index = client.player_index;
            if (client.has_player_binding && client.control != nullptr &&
                client.control->user() == static_cast<int>(client.player_index))
            {
                client.control->set_user(-1);
                client.control->restore_act_type();
            }

            if (pending_pause_state_.has_value() &&
                pending_pause_state_->player_index == client.player_index)
            {
                clear_pause_state();
            }
            if (pending_exit_prompt_state_.has_value() &&
                pending_exit_prompt_state_->triggering_player_index ==
                    client.player_index)
            {
                clear_pending_exit_prompt();
            }

            clients_.erase(it);
        }

        if (disconnected_player_index.has_value())
        {
            disconnected_players_.erase(
                std::remove_if(
                    disconnected_players_.begin(),
                    disconnected_players_.end(),
                    [player_index = *disconnected_player_index](
                        const DisconnectedPlayer& disconnected) {
                        return disconnected.player_index == player_index;
                    }),
                disconnected_players_.end());
        }

        transport_.disconnect(disconnected_peer_id);
    };

    disconnect_one(peer_id);

    if (was_host)
    {
        for (const PeerId other_peer_id : peers_to_disconnect)
            disconnect_one(other_peer_id);
    }
}

void GameServer::bind_player(PeerId peer_id,
                             std::size_t player_index,
                             short team_num,
                             walker* control)
{
    if (player_index >= static_cast<std::size_t>(MAX_PLAYERS))
    {
        throw std::out_of_range("GameServer player index exceeds MAX_PLAYERS");
    }

    ConnectedClientState& client = clients_[peer_id];
    client.player_index = player_index;
    client.team_num = team_num;
    if (control == nullptr)
    {
        // CTF: prefer the binding player's OWN hero. Several humans may share
        // a team in CTF lobbies, and the generic first-unclaimed scan would
        // hand the first binder a teammate's character. Classic and allied
        // worlds keep the original pool claim (allied deliberately treats the
        // combined roster as a shared pool in oblist order).
        if ((world_.type & GameWorld::TYPE_CTF) != 0)
        {
            for (const auto& uptr : world_.oblist)
            {
                walker* w = uptr.get();
                if (w == nullptr || w->dead() ||
                    w->query_order() != Order::Living)
                {
                    continue;
                }
                if (w->user() != -1 || w->team_num() != team_num)
                    continue;
                if (w->myguy == nullptr ||
                    w->myguy->owner_player_index !=
                        static_cast<std::uint8_t>(player_index))
                {
                    continue;
                }
                control = w;
                break;
            }
        }
        if (control == nullptr)
            control = sim_find_next_control(world_, team_num);
        if (control != nullptr && control->user() == -1)
        {
            control->set_user(static_cast<signed char>(player_index));
            control->set_act_type(ACT_CONTROL);
            if (control->stats() != nullptr)
                control->stats()->clear_command();
        }
    }
    client.control = control;
    client.has_player_binding = true;
    if (is_zero_session_token(client.session_token))
        client.session_token = allocate_session_token();
    player_controls_[player_index] = control;
    if (control != nullptr && control->stats() != nullptr)
        world_.control_hp = control->stats()->hitpoints();
}

void GameServer::set_player_control(std::size_t player_index,
                                    walker* control) noexcept
{
    if (player_index >= static_cast<std::size_t>(MAX_PLAYERS))
        return;

    player_controls_[player_index] = control;
    for (auto& [peer_id, client] : clients_)
    {
        (void)peer_id;
        if (client.has_player_binding && client.player_index == player_index)
            client.control = control;
    }
    maybe_send_control_change(player_index, control);
}

walker* GameServer::player_control(std::size_t player_index) const noexcept
{
    return player_index < static_cast<std::size_t>(MAX_PLAYERS)
        ? player_controls_[player_index]
        : nullptr;
}

InitialSetupMessage GameServer::build_initial_setup(PeerId peer_id) const
{
    const auto client_it = clients_.find(peer_id);
    if (client_it == clients_.end())
        throw std::runtime_error("GameServer missing client for initial setup");

    InitialSetupMessage message;
    message.level_id = world_.id;
    message.level_title = world_.title;
    message.level_type = static_cast<std::int8_t>(world_.type);
    message.par_value = world_.par_value;
    message.time_bonus_limit = world_.time_bonus_limit;
    message.difficulty = world_.difficulty;
    message.pixmaxx = world_.pixmaxx;
    message.pixmaxy = world_.pixmaxy;
    message.my_team = client_it->second.team_num;
    message.allied_mode = world_.allied_mode;
    message.current_scenario = world_.current_scenario;
    message.guys = collect_initial_setup_guys(world_);
    message.completed_levels.assign(world_.completed_levels.begin(),
                                    world_.completed_levels.end());
    for (std::size_t index = 0; index < message.controlled_entity_ids.size(); ++index)
    {
        message.controlled_entity_ids[index] =
            player_controls_[index] != nullptr
                ? player_controls_[index]->entity_id()
                : 0U;
    }

    return message;
}

void GameServer::send_initial_setup(PeerId peer_id)
{
    InitialSetupMessage message = build_initial_setup(peer_id);
    clients_[peer_id].initial_setup_sent = true;
    transport_.send_initial_setup(
        peer_id,
        std::make_shared<InitialSetupMessage>(std::move(message)));
}

void GameServer::handle_hello(PeerId peer_id, const HelloMessage& message)
{
    const auto client_it = clients_.find(peer_id);
    if (client_it == clients_.end())
        return;

    ConnectedClientState& client = client_it->second;
    client.last_received_input_ms = now_ms();

    HelloMessage response;
    response.protocol_version = kNetworkProtocolVersion;
    response.min_protocol_version = kNetworkProtocolVersion;

    if (client.has_player_binding)
    {
        if (!is_zero_session_token(client.session_token) &&
            !is_zero_session_token(message.session_token) &&
            !session_tokens_equal(message.session_token, client.session_token))
        {
            handle_transport_disconnect(peer_id, true);
            return;
        }

        if (is_zero_session_token(client.session_token))
            client.session_token = allocate_session_token();
        response.session_token = client.session_token;
        transport_.send_hello(peer_id, std::make_shared<HelloMessage>(response));
        return;
    }

    if (is_zero_session_token(message.session_token))
    {
        handle_transport_disconnect(peer_id, true);
        return;
    }

    const auto disconnected_it = std::find_if(
        disconnected_players_.begin(),
        disconnected_players_.end(),
        [&message](const DisconnectedPlayer& disconnected) {
            return session_tokens_equal(disconnected.session_token,
                                        message.session_token);
        });
    if (disconnected_it == disconnected_players_.end())
    {
        handle_transport_disconnect(peer_id, true);
        return;
    }

    client.session_token = disconnected_it->session_token;
    bind_player(peer_id,
                disconnected_it->player_index,
                disconnected_it->team_num,
                disconnected_it->control);

    ConnectedClientState& reconnected = clients_[peer_id];
    if (disconnected_it->was_host)
        host_peer_id_ = peer_id;
    reconnected.session_token = disconnected_it->session_token;
    reconnected.initial_setup_sent = false;
    reconnected.has_initial_snapshot = false;
    reconnected.client_ready = false;
    reconnected.allow_initial_keyframe_without_ready = true;
    reconnected.budget_pending_keyframe = true;
    reconnected.force_keyframe = true;
    reconnected.resume_in_dead_state =
        reconnected.control != nullptr && reconnected.control->dead();
    reconnected.pending_inputs.clear();
    reconnected.expected_snapshot_hashes.clear();
    reconnected.last_known_input = disconnected_it->repeated_input;
    clear_pressed(reconnected.last_known_input);
    reconnected.last_received_input_ms = now_ms();
    reset_client_snapshot_state(reconnected.snapshot_state);

    if (reconnected.resume_in_dead_state)
    {
        if (reconnected.control->user() ==
            static_cast<int>(reconnected.player_index))
        {
            reconnected.control->set_user(-1);
            reconnected.control->restore_act_type();
        }
    }
    else if (reconnected.control != nullptr &&
             reconnected.control->user() == -1)
    {
        reconnected.control->set_user(
            static_cast<signed char>(reconnected.player_index));
        reconnected.control->set_act_type(ACT_CONTROL);
        if (reconnected.control->stats() != nullptr)
            reconnected.control->stats()->clear_command();
    }

    player_controls_[reconnected.player_index] = reconnected.control;
    send_initial_setup(peer_id);
    maybe_send_control_change(reconnected.player_index, reconnected.control);

    response.session_token = reconnected.session_token;
    transport_.send_hello(peer_id, std::make_shared<HelloMessage>(response));
    disconnected_players_.erase(disconnected_it);
}

void GameServer::handle_heartbeat(PeerId peer_id)
{
    const auto client_it = clients_.find(peer_id);
    if (client_it == clients_.end())
        return;
    client_it->second.last_received_input_ms = now_ms();
}

void GameServer::update_disconnected_players(std::uint64_t now)
{
    for (auto it = disconnected_players_.begin();
         it != disconnected_players_.end();)
    {
        const bool expired =
            now >= it->disconnected_at_ms &&
            now - it->disconnected_at_ms >= PAUSE_TIMEOUT_MS;
        if (expired)
        {
            it = disconnected_players_.erase(it);
            continue;
        }

        const bool grace_expired =
            !it->ai_control_enabled &&
            now >= it->grace_started_at_ms &&
            now - it->grace_started_at_ms >= DISCONNECT_TIMEOUT_MS;
        if (grace_expired)
        {
            if (it->control != nullptr && !it->control->dead() &&
                it->control->user() == static_cast<int>(it->player_index))
            {
                it->control->set_user(-1);
                it->control->restore_act_type();
                if (it->control->stats() != nullptr)
                    it->control->stats()->clear_command();
            }
            it->ai_control_enabled = true;
        }

        ++it;
    }
}

bool GameServer::process_disconnected_players(std::uint32_t expected_tick)
{
    bool should_tick_world = true;
    (void)expected_tick;

    for (auto& disconnected : disconnected_players_)
    {
        // Same CTF-revive reclaim as apply_polled_inputs: a player who
        // disconnected while dead with no control gets its revived walker
        // rebound during the grace window so it is not a statue.
        if (!disconnected.ai_control_enabled &&
            disconnected.control == nullptr &&
            disconnected.player_index < static_cast<std::size_t>(MAX_PLAYERS))
        {
            if (walker* reclaimed =
                    find_ctf_reclaim_control(disconnected.player_index))
            {
                disconnected.control = reclaimed;
                player_controls_[disconnected.player_index] = reclaimed;
                maybe_send_control_change(disconnected.player_index, reclaimed);
                if (reclaimed->stats() != nullptr)
                    world_.control_hp = reclaimed->stats()->hitpoints();
            }
        }

        if (disconnected.ai_control_enabled || disconnected.control == nullptr ||
            disconnected.control->dead() ||
            disconnected.player_index >= static_cast<std::size_t>(MAX_PLAYERS))
        {
            continue;
        }

        walker* const previous_control = disconnected.control;
        const SimInputResult result = sim_process_player_input(
            disconnected.repeated_input,
            disconnected.control,
            world_,
            static_cast<short>(disconnected.player_index),
            disconnected.team_num,
            player_input_debounce_[disconnected.player_index],
            special_names_,
            &events_);

        player_controls_[disconnected.player_index] = disconnected.control;
        if (disconnected.control != previous_control)
            maybe_send_control_change(disconnected.player_index, disconnected.control);
        if (result.control_hp_changed)
            world_.control_hp = result.control_hp;
        if (result.endgame_requested &&
            !og::sim::ctf_suppress_team_wipe_endgame(world_))
        {
            if (has_living_member_for_any_bound_team(
                    world_, clients_, disconnected_players_))
            {
                continue;
            }

            world_.ending = result.endgame_type;
            emit_event(&events_, EventKind::EndGame,
                       static_cast<std::uint32_t>(result.endgame_type),
                       static_cast<std::uint32_t>(-1));
            should_tick_world = false;
        }
    }

    return should_tick_world;
}

void GameServer::synchronize_transport_peers()
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
        if (clients_.find(peer_id) == clients_.end())
            connect_client(peer_id);
    }

    connected_transport_peers_ = current_peers;

    for (const PeerId peer_id : removed_peers)
    {
        const bool has_pending_for_peer = std::any_of(
            pending_inbound_messages_.begin(),
            pending_inbound_messages_.end(),
            [peer_id](const og::sim::TypedReceivedMessage& message) {
                return message.peer_id == peer_id;
            });
        if (typed_messages_include_peer(last_polled_messages_, peer_id) ||
            has_pending_for_peer)
        {
            insert_peer_id_sorted(pending_transport_disconnects_, peer_id);
        }
        else
        {
            handle_transport_disconnect(peer_id, false);
        }
    }
}

void GameServer::apply_transport_disconnects()
{
    std::vector<PeerId> disconnected_peers = std::move(pending_transport_disconnects_);
    pending_transport_disconnects_.clear();
    for (const PeerId peer_id : disconnected_peers)
        handle_transport_disconnect(peer_id, false);
}

void GameServer::poll_incoming_messages()
{
    poll_incoming_messages(INT_MAX);
}

void GameServer::poll_incoming_messages(int max_messages)
{
    apply_transport_disconnects();
    ServerPollResult poll_result = poll_server_messages(transport_);

    for (auto& message : poll_result.messages)
        pending_inbound_messages_.push_back(std::move(message));
    poll_result.messages.clear();

    const std::size_t available = pending_inbound_messages_.size();
    const std::size_t budget =
        max_messages <= 0 ? 0u : static_cast<std::size_t>(max_messages);
    const std::size_t to_drain = std::min(available, budget);

    last_polled_messages_.clear();
    last_polled_messages_.reserve(to_drain);
    for (std::size_t i = 0; i < to_drain; ++i)
    {
        last_polled_messages_.push_back(
            std::move(pending_inbound_messages_.front()));
        pending_inbound_messages_.pop_front();
    }
    messages_drained_last_call_ = static_cast<int>(to_drain);

    for (const PeerId peer_id : poll_result.malformed_peers)
    {
        LogError("game_server_malformed_message peer={}\n", peer_id);
        handle_transport_disconnect(peer_id, true);
    }
    synchronize_transport_peers();
}

PlayerInput GameServer::select_effective_input(ConnectedClientState& client,
                                               std::uint32_t expected_tick)
{
    std::vector<std::uint32_t> pending_ticks;
    pending_ticks.reserve(client.pending_inputs.size());
    for (const auto& [tick, input] : client.pending_inputs)
    {
        (void)input;
        if (tick <= expected_tick)
            pending_ticks.push_back(tick);
    }
    std::sort(pending_ticks.begin(), pending_ticks.end());

    PlayerInput effective = client.last_known_input;
    clear_pressed(effective);
    std::array<bool, NUM_INPUT_KEYS> late_pressed = {};
    bool has_exact_input = false;

    for (const std::uint32_t tick : pending_ticks)
    {
        const auto input_it = client.pending_inputs.find(tick);
        if (input_it == client.pending_inputs.end())
            continue;

        const PlayerInput& received = input_it->second;
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
            client.last_known_input.held[key] = received.held[key];

        if (tick == expected_tick)
        {
            effective = received;
            has_exact_input = true;
        }
        else if (expected_tick - tick <= MAX_LATE_PRESS_TICKS)
        {
            for (int key = 0; key < NUM_INPUT_KEYS; ++key)
                late_pressed[key] = late_pressed[key] || received.pressed[key];
        }

        client.pending_inputs.erase(input_it);
    }

    if (!has_exact_input)
    {
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
            effective.held[key] = client.last_known_input.held[key];
    }

    for (int key = 0; key < NUM_INPUT_KEYS; ++key)
        effective.pressed[key] = effective.pressed[key] || late_pressed[key];

    return effective;
}

void GameServer::process_non_input_messages(std::uint32_t expected_tick)
{
    for (const auto& message : last_polled_messages_)
    {
        auto client_it = clients_.find(message.peer_id);
        if (client_it == clients_.end())
            continue;

        ConnectedClientState& client = client_it->second;
        switch (message.kind)
        {
        case TypedReceivedMessageKind::Input:
            if (!message.input || !client.has_player_binding)
                break;
            if (host_peer_id_.has_value() &&
                message.peer_id == *host_peer_id_ &&
                message.input->timer_wait_request != kNoTimerWaitRequest)
            {
                world_.timer_wait = static_cast<signed char>(std::clamp<int>(
                    message.input->timer_wait_request,
                    0,
                    20));
            }
            client.pending_inputs[message.tick] =
                select_player_input(*message.input, client.player_index);
            client.last_received_input_tick = message.tick;
            client.last_received_input_ms = now_ms();
            break;

        case TypedReceivedMessageKind::Hello:
            if (message.hello)
                handle_hello(message.peer_id, *message.hello);
            break;

        case TypedReceivedMessageKind::ClientReady:
            client.client_ready = true;
            client.allow_initial_keyframe_without_ready = false;
            client.budget_pending_keyframe = false;
            client.force_keyframe = true;
            break;

        case TypedReceivedMessageKind::KeyframeRequest:
            client.budget_pending_keyframe = true;
            client.force_keyframe = true;
            break;

        case TypedReceivedMessageKind::Heartbeat:
            handle_heartbeat(message.peer_id);
            break;

        case TypedReceivedMessageKind::ExitPromptResponse:
            if (message.exit_prompt_response)
            {
                if (message.exit_prompt_response->abort_request)
                    abort_level_for_all();
                else if (pending_exit_prompt_state_.has_value())
                    handle_exit_prompt_response(
                        message.exit_prompt_response->accepted);
            }
            break;

        case TypedReceivedMessageKind::PauseBroadcast:
            handle_pause_request(message.peer_id);
            break;

        case TypedReceivedMessageKind::PauseResponse:
            if (message.pause_response && message.pause_response->resume)
                handle_pause_response();
            break;

        case TypedReceivedMessageKind::SnapshotHashCheck:
            if (message.snapshot_hash_check)
            {
                auto hash_it =
                    client.expected_snapshot_hashes.find(message.snapshot_hash_check->tick);
                if (hash_it == client.expected_snapshot_hashes.end() ||
                    hash_it->second.empty())
                {
                    ++snapshot_hash_mismatch_count_;
                    client.budget_pending_keyframe = true;
                    client.force_keyframe = true;
                    LogError(
                        "snapshot_hash_mismatch peer={} tick={} server={} client={} entities={}\n",
                        message.peer_id,
                        message.snapshot_hash_check->tick,
                        0u,
                        message.snapshot_hash_check->snapshot_hash,
                        world_.oblist.size() + world_.fxlist.size() +
                            world_.weaplist.size());
                }
                else
                {
                    const std::uint32_t expected_hash = hash_it->second.front();
                    if (expected_hash != message.snapshot_hash_check->snapshot_hash)
                    {
                        ++snapshot_hash_mismatch_count_;
                        client.budget_pending_keyframe = true;
                        client.force_keyframe = true;
                        LogError(
                            "snapshot_hash_mismatch peer={} tick={} server={} client={} entities={}\n",
                            message.peer_id,
                            message.snapshot_hash_check->tick,
                            expected_hash,
                            message.snapshot_hash_check->snapshot_hash,
                            world_.oblist.size() + world_.fxlist.size() +
                                world_.weaplist.size());
                    }
                    else
                    {
                        hash_it->second.pop_front();
                        if (hash_it->second.empty())
                            client.expected_snapshot_hashes.erase(hash_it);
                    }
                }
            }
            break;

        case TypedReceivedMessageKind::Malformed:
            break;

        case TypedReceivedMessageKind::Snapshot:
        case TypedReceivedMessageKind::DeltaSnapshot:
        case TypedReceivedMessageKind::SimEventBatch:
        case TypedReceivedMessageKind::GameFlowEventBatch:
        case TypedReceivedMessageKind::LobbyMessage:
        case TypedReceivedMessageKind::LobbyState:
        case TypedReceivedMessageKind::InitialSetup:
        case TypedReceivedMessageKind::ExitPromptBroadcast:
        case TypedReceivedMessageKind::ControlChange:
            break;
        }
    }

    apply_transport_disconnects();
    (void)expected_tick;
    update_timeouts();
}

void GameServer::update_timeouts()
{
    const bool suspended_for_ui =
        pending_exit_prompt_state_.has_value() || pending_pause_state_.has_value();
    if (pending_exit_prompt_state_.has_value() &&
        pending_exit_prompt_state_->triggering_player_index <
            static_cast<std::size_t>(MAX_PLAYERS))
    {
        walker* const triggering_control =
            player_control(pending_exit_prompt_state_->triggering_player_index);
        if (triggering_control == nullptr || triggering_control->dead())
        {
            handle_exit_prompt_response(false);
            return;
        }
    }

    const std::uint64_t now = now_ms();
    if (pending_exit_prompt_state_.has_value() &&
        now >= pending_exit_prompt_state_->opened_at_ms &&
        now - pending_exit_prompt_state_->opened_at_ms >= EXIT_PROMPT_TIMEOUT_MS)
    {
        handle_exit_prompt_response(false);
    }

    if (pending_pause_state_.has_value() &&
        now >= pending_pause_state_->opened_at_ms &&
        now - pending_pause_state_->opened_at_ms >= PAUSE_TIMEOUT_MS)
    {
        clear_pause_state();
    }

    if (suspended_for_ui)
    {
        update_disconnected_players(now);
        return;
    }

    std::vector<std::pair<PeerId, std::uint64_t>> timed_out_peers;
    timed_out_peers.reserve(clients_.size());
    for (const auto& [peer_id, client] : clients_)
    {
        if (client.last_received_input_ms == 0 || now < client.last_received_input_ms)
            continue;

        if (now - client.last_received_input_ms >= DISCONNECT_TIMEOUT_MS)
            timed_out_peers.push_back({peer_id, client.last_received_input_ms});
    }

    for (const auto& [peer_id, grace_start_ms] : timed_out_peers)
        handle_transport_disconnect(peer_id, true, grace_start_ms);

    update_disconnected_players(now);
}

void GameServer::clear_pending_exit_prompt()
{
    world_.pending_exit_prompt = false;
    world_.withdraw_requested = false;
    world_.withdraw_level = -1;
    pending_exit_prompt_state_.reset();
}

void GameServer::clear_pause_state()
{
    world_.paused = false;
    world_.pause_player_index = kNoPausePlayerIndex;
    pending_pause_state_.reset();
}

void GameServer::handle_exit_prompt_response(bool accepted)
{
    if (!pending_exit_prompt_state_.has_value())
        return;

    const PendingExitPromptState prompt = *pending_exit_prompt_state_;
    clear_pending_exit_prompt();

    if (!accepted)
        return;

    bool handled = false;
    if (prompt.withdraw_prompt)
    {
        handled = on_withdraw_accepted
            ? on_withdraw_accepted(prompt.destination_level)
            : false;
    }
    else
    {
        handled = on_exit_accepted
            ? on_exit_accepted(prompt.destination_level)
            : false;
    }

    if (!handled)
        return;

    // Return-to-lobby: the accepted exit/withdraw only finalized the cursor (no
    // next-level load). Tell every display to end + show results so each peer
    // returns to the team-build menu, then stop — no in-session reload.
    if (return_to_lobby_mode_)
    {
        forward_level_end_to_clients(prompt.withdraw_prompt ? 1 : 0,
                                     prompt.destination_level);
        return;
    }

    prepare_clients_for_loaded_level();
}

void GameServer::abort_level_for_all()
{
    // A player chose "Quit this mission". Treat it as a deliberate party-wide
    // WITHDRAW to the current level: revert the roster to the level's start and
    // return every peer to the team-build menu (retry available) — rather than
    // the requesting client simply leaving and its character becoming AI. Any
    // player (host or client) may trigger this.
    clear_pending_exit_prompt();
    clear_pause_state();

    const int destination = static_cast<int>(world_.current_scenario);
    const bool handled =
        on_withdraw_accepted ? on_withdraw_accepted(destination) : false;
    if (!handled)
        return;

    if (return_to_lobby_mode_)
    {
        forward_level_end_to_clients(1, destination);
        return;
    }

    prepare_clients_for_loaded_level();
}

void GameServer::handle_level_transition(std::int16_t next_level)
{
    if (next_level < 0 || !on_level_transition)
        return;

    if (on_save_sync)
        on_save_sync();

    if (!on_level_transition(next_level))
        return;

    // Return-to-lobby: the hook finalized the cursor only (no next-level load).
    // The win's terminal EndGame was already forwarded for this game-ended tick,
    // so every display ends and returns to the menu — skip the in-session
    // client reload.
    if (return_to_lobby_mode_)
        return;

    prepare_clients_for_loaded_level();
}

void GameServer::forward_level_end_to_clients(int ending, int next_level)
{
    SimEventBatch batch;

    Event set_palette_event;
    set_palette_event.kind = EventKind::SetPalette;
    set_palette_event.a = world_.current_palette_id;
    set_palette_event.b = 0u;
    batch.events.push_back(std::move(set_palette_event));

    Event request_redraw_event;
    request_redraw_event.kind = EventKind::RequestRedraw;
    batch.events.push_back(std::move(request_redraw_event));

    Event endgame_event;
    endgame_event.kind = EventKind::EndGame;
    endgame_event.a =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(ending));
    endgame_event.b =
        static_cast<std::uint32_t>(static_cast<std::int32_t>(next_level));
    batch.events.push_back(std::move(endgame_event));
    og::sim::normalize_endgame_event_order(batch);

    forward_event_batch(batch);
}

void GameServer::prepare_clients_for_loaded_level()
{
    // A freshly loaded level must start live: drop any pause / exit-prompt that
    // was in effect when the transition was requested (e.g. a withdraw/exit
    // chosen from the pause menu). These server-side optionals gate step() but
    // are invisible to the level-load path, so without this the new level would
    // stay frozen — "in a level but no one can move".
    clear_pause_state();
    clear_pending_exit_prompt();

    events_.clear();
    world_.clear_removed_entity_ids();
    world_.clear_grid_dirty_tiles();
    next_sim_event_sequence_ = 1;
    next_game_flow_event_sequence_ = 1;
    disconnected_players_.clear();
    player_controls_.fill(nullptr);
    player_input_debounce_ = {};
    world_.control_hp = 0.0f;

    const std::uint64_t now = now_ms();
    for (auto& [peer_id, client] : clients_)
    {
        (void)peer_id;
        client.initial_setup_sent = false;
        client.has_initial_snapshot = false;
        client.client_ready = false;
        client.allow_initial_keyframe_without_ready = false;
        client.budget_pending_keyframe = false;
        client.force_keyframe = true;
        client.resume_in_dead_state = false;
        client.pending_inputs.clear();
        client.expected_snapshot_hashes.clear();
        client.last_known_input = {};
        client.last_received_input_tick = 0;
        client.last_received_input_ms = now;
        reset_client_snapshot_state(client.snapshot_state);
    }

    rebind_players_for_loaded_level();

    for (const auto& [peer_id, client] : clients_)
    {
        (void)client;
        send_initial_setup(peer_id);
    }

    poll_incoming_messages();
    last_polled_messages_.clear();
}

void GameServer::rebind_players_for_loaded_level()
{
    struct PlayerBinding {
        PeerId peer_id = 0;
        std::size_t player_index = 0;
        short team_num = 0;
    };

    std::vector<PlayerBinding> bindings;
    bindings.reserve(clients_.size());
    for (const auto& [peer_id, client] : clients_)
    {
        if (!client.has_player_binding)
            continue;

        bindings.push_back(PlayerBinding{
            .peer_id = peer_id,
            .player_index = client.player_index,
            .team_num = client.team_num,
        });
    }

    std::sort(bindings.begin(), bindings.end(),
              [](const PlayerBinding& lhs, const PlayerBinding& rhs) {
                  if (lhs.player_index != rhs.player_index)
                      return lhs.player_index < rhs.player_index;
                  return lhs.peer_id < rhs.peer_id;
              });

    for (const PlayerBinding& binding : bindings)
        bind_player(binding.peer_id, binding.player_index, binding.team_num, nullptr);
}

std::size_t GameServer::infer_exit_triggering_player_index() const noexcept
{
    constexpr std::size_t kNoPlayer = static_cast<std::size_t>(-1);
    std::size_t fallback = kNoPlayer;

    for (const auto& [peer_id, client] : clients_)
    {
        (void)peer_id;
        if (!client.has_player_binding || client.control == nullptr ||
            client.control->dead())
        {
            continue;
        }

        if (client.control->skip_exit() == 10)
            return client.player_index;

        if (fallback == kNoPlayer && client.control->skip_exit() > 1)
            fallback = client.player_index;
    }

    return fallback;
}

void GameServer::handle_pause_request(PeerId peer_id)
{
    if (pending_exit_prompt_state_.has_value() || pending_pause_state_.has_value())
        return;

    const auto client_it = clients_.find(peer_id);
    if (client_it == clients_.end() || !client_it->second.has_player_binding)
        return;

    ConnectedClientState& client = client_it->second;
    const std::uint64_t now = now_ms();
    if (client.last_pause_request_ms != 0 &&
        now >= client.last_pause_request_ms &&
        now - client.last_pause_request_ms < PAUSE_RATE_LIMIT_MS)
    {
        return;
    }

    client.last_pause_request_ms = now;
    world_.paused = true;
    world_.pause_player_index = static_cast<std::uint8_t>(client.player_index);

    PendingPauseState state;
    state.player_index = client.player_index;
    state.player_name = player_name_from_control(client.control, client.player_index);
    state.opened_at_ms = now;
    pending_pause_state_ = state;

    PauseBroadcastMessage message;
    message.player_index = static_cast<std::uint8_t>(client.player_index);
    message.player_name = state.player_name;
    for (const auto& [other_peer_id, other_client] : clients_)
    {
        (void)other_client;
        transport_.send_pause_broadcast(
            other_peer_id,
            std::make_shared<PauseBroadcastMessage>(message));
    }
}

void GameServer::handle_pause_response()
{
    clear_pause_state();
}

bool GameServer::apply_polled_inputs(std::uint32_t expected_tick)
{
    bool should_tick_world = true;
    for (auto& [peer_id, client] : clients_)
    {
        (void)peer_id;
        if (!client.has_player_binding)
            continue;

        const std::size_t player_index = client.player_index;
        walker* const previous_control = client.control;
        if (client.resume_in_dead_state && client.control != nullptr &&
            client.control->dead())
        {
            player_controls_[player_index] = client.control;
            continue;
        }

        client.resume_in_dead_state = false;
        if (client.control == nullptr)
        {
            // A CTF revive restored this player's walker in place with its
            // user tag intact; rebind it (the previous_control diff below
            // broadcasts the ControlChange to every mirror).
            if (walker* reclaimed = find_ctf_reclaim_control(player_index))
            {
                client.control = reclaimed;
                if (reclaimed->stats() != nullptr)
                    world_.control_hp = reclaimed->stats()->hitpoints();
            }
        }
        const PlayerInput input = select_effective_input(client, expected_tick);
        const SimInputResult result = sim_process_player_input(
            input,
            client.control,
            world_,
            static_cast<short>(player_index),
            client.team_num,
            player_input_debounce_[player_index],
            special_names_,
            &events_);

        player_controls_[player_index] = client.control;
        if (client.control != previous_control)
            maybe_send_control_change(player_index, client.control);
        if (result.control_hp_changed)
            world_.control_hp = result.control_hp;
        if (result.endgame_requested &&
            !og::sim::ctf_suppress_team_wipe_endgame(world_))
        {
            if (has_living_member_for_any_bound_team(
                    world_, clients_, disconnected_players_))
            {
                continue;
            }

            world_.ending = result.endgame_type;
            emit_event(&events_, EventKind::EndGame,
                       static_cast<std::uint32_t>(result.endgame_type),
                       static_cast<std::uint32_t>(-1));
            should_tick_world = false;
        }
    }

    return should_tick_world;
}

void GameServer::send_initial_snapshot(PeerId peer_id,
                                       SnapshotCaptureMode capture_mode)
{
    ConnectedClientState& client = clients_[peer_id];
    send_initial_setup(peer_id);
    WorldSnapshot keyframe = capture_server_keyframe(world_, capture_mode);
    seed_client_snapshot_baseline(client.snapshot_state, keyframe);
    client.has_initial_snapshot = true;
    client.client_ready = false;
    client.allow_initial_keyframe_without_ready = false;
    client.budget_pending_keyframe = false;
    client.force_keyframe = false;
    remember_snapshot_hash(client, keyframe);
    transport_.send_snapshot(peer_id,
                             std::make_shared<WorldSnapshot>(std::move(keyframe)));
}

void GameServer::send_initial_snapshots(SnapshotCaptureMode capture_mode)
{
    for (const auto& [peer_id, client] : clients_)
    {
        (void)client;
        send_initial_snapshot(peer_id, capture_mode);
    }
}

bool GameServer::should_send_to_client(const ConnectedClientState& client) const
{
    return client.has_initial_snapshot && client.client_ready;
}

bool GameServer::should_send_keyframe(const ConnectedClientState& client,
                                      const WorldSnapshot& snapshot) const
{
    const bool should_send =
        client.force_keyframe ||
        (snapshot.tick_count != 0 &&
         (snapshot.tick_count % KEYFRAME_INTERVAL_TICKS) == 0) ||
        pending_exit_prompt_state_.has_value() ||
        pending_pause_state_.has_value() ||
        snapshot.tick_count <= client.snapshot_state.last_sent_tick;

    og::runtime::RuntimeTraceRecord trace =
        og::runtime::make_runtime_trace_record(
            "game_server",
            should_send ? "keyframe_required" : "delta_snapshot_allowed");
    trace.tick = snapshot.tick_count;
    trace.snapshot_kind = should_send ? "keyframe" : "delta";
    og::runtime::emit_runtime_trace(std::move(trace));
    return should_send;
}

void GameServer::forward_event_batch(const SimEventBatch& batch)
{
    if (batch.events.empty())
        return;

    SimEventBatch sim_batch;
    SimEventBatch game_flow_batch;
    og::sim::split_event_batches(batch, sim_batch, game_flow_batch);
    if (!sim_batch.events.empty())
        sim_batch.sequence = next_sim_event_sequence_++;
    if (!game_flow_batch.events.empty())
        game_flow_batch.sequence = next_game_flow_event_sequence_++;

    for (const auto& [peer_id, client] : clients_)
    {
        if (!should_send_to_client(client))
            continue;

        if (!sim_batch.events.empty())
        {
            transport_.send_sim_event_batch(
                peer_id,
                std::make_shared<SimEventBatch>(sim_batch));
        }
        if (!game_flow_batch.events.empty())
        {
            transport_.send_game_flow_event_batch(
                peer_id,
                std::make_shared<SimEventBatch>(game_flow_batch));
        }
    }
}

void GameServer::remember_snapshot_hash(ConnectedClientState& client,
                                        const WorldSnapshot& snapshot,
                                        bool expect_client_hash_check)
{
    if (!expect_client_hash_check)
        return;

    client.expected_snapshot_hashes[snapshot.tick_count].push_back(
        snapshot.snapshot_hash);
    const std::uint32_t oldest_tick =
        snapshot.tick_count > (KEYFRAME_INTERVAL_TICKS * 2)
            ? snapshot.tick_count - (KEYFRAME_INTERVAL_TICKS * 2)
            : 0U;
    for (auto it = client.expected_snapshot_hashes.begin();
         it != client.expected_snapshot_hashes.end();)
    {
        if (it->first < oldest_tick)
            it = client.expected_snapshot_hashes.erase(it);
        else
            ++it;
    }
}

walker* GameServer::find_ctf_reclaim_control(std::size_t player_index) const
{
    // Gate strictly on an active CTF match so non-CTF behavior is untouched.
    if (!(world_.type & GameWorld::TYPE_CTF) || !world_.ctf.active)
        return nullptr;

    for (const auto& uptr : world_.oblist)
    {
        walker* const entity = uptr.get();
        if (entity != nullptr && !entity->dead() &&
            entity->query_order() == Order::Living &&
            entity->user() == static_cast<int>(player_index))
        {
            return entity;
        }
    }
    return nullptr;
}

void GameServer::maybe_send_control_change(std::size_t player_index, walker* control)
{
    if (player_index >= static_cast<std::size_t>(MAX_PLAYERS))
        return;

    ControlChangeMessage message;
    message.player_index = static_cast<std::uint8_t>(player_index);
    message.entity_id = control != nullptr ? control->entity_id() : 0U;

    for (const auto& [peer_id, client] : clients_)
    {
        (void)client;
        transport_.send_control_change(
            peer_id,
            std::make_shared<ControlChangeMessage>(message));
    }
}

void GameServer::maybe_resolve_world_events(SimEventBatch& batch,
                                            WorldSnapshot& snapshot)
{
    std::optional<Event> exit_request = std::nullopt;
    std::optional<Event> withdraw_request = std::nullopt;
    std::vector<Event> filtered;
    filtered.reserve(batch.events.size());

    for (const auto& event : batch.events)
    {
        switch (event.kind)
        {
        case EventKind::RequestExitConfirmation:
            if (!exit_request.has_value())
                exit_request = event;
            break;

        case EventKind::WithdrawToLevel:
            if (!withdraw_request.has_value())
                withdraw_request = event;
            break;

        default:
            filtered.push_back(event);
            break;
        }
    }

    batch.events = std::move(filtered);
    og::sim::normalize_endgame_event_order(batch);

    if (!exit_request.has_value() || pending_exit_prompt_state_.has_value())
        return;

    PendingExitPromptState prompt;
    prompt.destination_level = static_cast<std::int16_t>(
        static_cast<std::int32_t>(exit_request->a));
    prompt.withdraw_prompt = exit_request->b != 0;
    prompt.prompt_text = exit_request->text.empty()
        ? (prompt.withdraw_prompt
               ? std::format("Withdraw to Level {}?", prompt.destination_level)
               : std::format("Exit to Level {}?", prompt.destination_level))
        : exit_request->text;
    prompt.opened_at_ms = now_ms();
    prompt.triggering_player_index = infer_exit_triggering_player_index();
    pending_exit_prompt_state_ = prompt;

    world_.pending_exit_prompt = true;
    world_.withdraw_requested = prompt.withdraw_prompt;
    world_.withdraw_level = prompt.withdraw_prompt
        ? prompt.destination_level
        : -1;
    snapshot.pending_exit_prompt = true;
    snapshot.withdraw_requested = world_.withdraw_requested;
    snapshot.withdraw_level = world_.withdraw_level;

    ExitPromptBroadcastMessage message;
    message.destination_level = prompt.destination_level;
    message.withdraw_prompt = prompt.withdraw_prompt;
    message.prompt_text = prompt.prompt_text;

    // Prompt ONLY the player who triggered the exit/withdraw. Showing the
    // blocking yes/no on every display would (a) force the whole party to
    // confirm and (b) freeze the host's loop — and therefore the server — on its
    // own modal, so a client's "Yes" couldn't be processed until the host also
    // answered. The triggering player's confirmation drives the exit/withdraw
    // for everyone (handle_exit_prompt_response -> forward_level_end_to_clients).
    const auto send_prompt_to = [&](og::sim::PeerId peer_id) {
        transport_.send_exit_prompt_broadcast(
            peer_id, std::make_shared<ExitPromptBroadcastMessage>(message));
    };

    bool prompted_trigger = false;
    if (prompt.triggering_player_index != static_cast<std::size_t>(-1))
    {
        for (const auto& [peer_id, client] : clients_)
        {
            if (client.has_player_binding &&
                client.player_index == prompt.triggering_player_index)
            {
                send_prompt_to(peer_id);
                prompted_trigger = true;
                break;
            }
        }
    }

    if (!prompted_trigger)
    {
        // Couldn't identify the triggering player (e.g. a synthesized event):
        // fall back to prompting everyone — an extra confirmation beats a stuck
        // exit.
        for (const auto& [peer_id, client] : clients_)
        {
            (void)client;
            send_prompt_to(peer_id);
        }
    }

    (void)withdraw_request;
}

void GameServer::broadcast_current_state(SnapshotCaptureMode capture_mode,
                                         EventDeliveryMode event_mode)
{
    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "game_server", "broadcast_current_state_begin"));
    WorldSnapshot snapshot = capture_server_snapshot(world_, capture_mode);
    if (pending_exit_prompt_state_.has_value())
    {
        snapshot.pending_exit_prompt = true;
        snapshot.withdraw_requested = world_.withdraw_requested;
        snapshot.withdraw_level = world_.withdraw_level;
    }
    if (pending_pause_state_.has_value())
    {
        snapshot.paused = true;
        snapshot.pause_player_index = world_.pause_player_index;
    }

    std::optional<SimEventBatch> drained_batch = std::nullopt;
    if (event_mode == EventDeliveryMode::Drain)
    {
        drained_batch = drain_sim_events(events_);
        maybe_resolve_world_events(*drained_batch, snapshot);
        apply_authoritative_event_state(world_, *drained_batch, snapshot);
        if (snapshot.game_ended)
        {
            // Synthesize level-completion display events from authoritative
            // snapshot state. The deterministic sim (GameWorld::tick) no longer
            // emits these (matching master, which signals level-end
            // imperatively via screen.cpp/endgame()); the broadcast layer
            // re-derives them here so the client mirror still receives the
            // palette/redraw/end notifications it consumes.
            if (!contains_event_kind(*drained_batch, EventKind::SetPalette))
            {
                Event set_palette_event;
                set_palette_event.kind = EventKind::SetPalette;
                set_palette_event.a = snapshot.current_palette_id;
                set_palette_event.b = 0u;
                drained_batch->events.push_back(std::move(set_palette_event));
            }
            if (!contains_event_kind(*drained_batch, EventKind::RequestRedraw))
            {
                Event request_redraw_event;
                request_redraw_event.kind = EventKind::RequestRedraw;
                drained_batch->events.push_back(std::move(request_redraw_event));
            }
            // SetEnd tells the display to end its session (world.end=1). Only
            // send it for a terminal outcome (no next level). When the win
            // auto-advances to a next level the display must NOT end — it
            // follows the server into the next level via the transition
            // InitialSetup; sending SetEnd here would freeze the next level.
            if (snapshot.next_level < 0 &&
                !contains_event_kind(*drained_batch, EventKind::SetEnd))
            {
                Event set_end_event;
                set_end_event.kind = EventKind::SetEnd;
                drained_batch->events.push_back(std::move(set_end_event));
            }
            if (!contains_endgame_event(*drained_batch))
            {
                Event endgame_event;
                endgame_event.kind = EventKind::EndGame;
                endgame_event.a = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(snapshot.ending));
                endgame_event.b = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(snapshot.next_level));
                drained_batch->events.push_back(std::move(endgame_event));
                og::sim::normalize_endgame_event_order(*drained_batch);
            }
        }
    }

    std::optional<WorldSnapshot> keyframe = std::nullopt;
    const auto ensure_keyframe = [&]() -> const WorldSnapshot& {
        if (!keyframe.has_value())
        {
            keyframe = capture_server_keyframe(world_, SnapshotCaptureMode::Peek);
        }
        return *keyframe;
    };
    snapshot.snapshot_hash = ensure_keyframe().snapshot_hash;

    std::vector<PeerId> ordered_peers;
    ordered_peers.reserve(clients_.size());
    for (const auto& [peer_id, client] : clients_)
    {
        (void)client;
        ordered_peers.push_back(peer_id);
    }
    std::sort(ordered_peers.begin(), ordered_peers.end());

    bool can_broadcast_keyframe = !ordered_peers.empty();
    for (const PeerId peer_id : ordered_peers)
    {
        const ConnectedClientState& client = clients_.at(peer_id);
        if (!client.initial_setup_sent ||
            !client.has_initial_snapshot ||
            !should_send_to_client(client) ||
            !should_send_keyframe(client, snapshot) ||
            client.allow_initial_keyframe_without_ready ||
            client.budget_pending_keyframe)
        {
            can_broadcast_keyframe = false;
            break;
        }
    }

    bool broadcast_keyframe_sent = false;
    std::size_t keyframe_budget_remaining = kMaxKeyframesPerTick;
    for (const PeerId peer_id : ordered_peers)
    {
        ConnectedClientState& client = clients_.at(peer_id);
        if (!client.initial_setup_sent)
        {
            if (!client.has_player_binding)
                continue;
            send_initial_snapshot(peer_id, SnapshotCaptureMode::Peek);
            continue;
        }

        if (!client.has_initial_snapshot)
        {
            if (!client.client_ready && !client.allow_initial_keyframe_without_ready)
                continue;
            const bool budgeted_initial_keyframe =
                client.allow_initial_keyframe_without_ready ||
                client.budget_pending_keyframe;
            if (budgeted_initial_keyframe && keyframe_budget_remaining == 0)
            {
                client.force_keyframe = true;
                continue;
            }

            WorldSnapshot full = ensure_keyframe();
            seed_client_snapshot_baseline(client.snapshot_state, full);
            client.has_initial_snapshot = true;
            client.client_ready = false;
            client.allow_initial_keyframe_without_ready = false;
            client.budget_pending_keyframe = false;
            client.force_keyframe = false;
            remember_snapshot_hash(client, full);
            transport_.send_snapshot(
                peer_id,
                std::make_shared<WorldSnapshot>(std::move(full)));
            if (budgeted_initial_keyframe && keyframe_budget_remaining > 0)
                --keyframe_budget_remaining;
            continue;
        }

        if (!should_send_to_client(client))
            continue;

        const bool keyframe_required = should_send_keyframe(client, snapshot);
        const bool budgeted_keyframe =
            client.allow_initial_keyframe_without_ready ||
            (client.force_keyframe && client.budget_pending_keyframe);
        if (keyframe_required)
        {
            if (can_broadcast_keyframe)
            {
                const WorldSnapshot& full = ensure_keyframe();
                if (!broadcast_keyframe_sent)
                {
                    og::runtime::RuntimeTraceRecord trace =
                        og::runtime::make_runtime_trace_record(
                            "game_server", "broadcast_keyframe_shared");
                    trace.tick = full.tick_count;
                    trace.snapshot_kind = "keyframe";
                    og::runtime::emit_runtime_trace(std::move(trace));
                    const std::vector<std::uint8_t> bytes =
                        serialize_snapshot(full);
                    transport_.broadcast(bytes.data(), bytes.size());
                    broadcast_keyframe_sent = true;
                }
                seed_client_snapshot_baseline(client.snapshot_state, full);
                client.allow_initial_keyframe_without_ready = false;
                client.budget_pending_keyframe = false;
                client.force_keyframe = false;
                remember_snapshot_hash(client, full);
                continue;
            }

            if (budgeted_keyframe && keyframe_budget_remaining == 0)
            {
                client.force_keyframe = true;
                continue;
            }
            WorldSnapshot full = ensure_keyframe();
            og::runtime::RuntimeTraceRecord trace =
                og::runtime::make_runtime_trace_record(
                    "game_server", "broadcast_keyframe_direct");
            trace.tick = full.tick_count;
            trace.snapshot_kind = "keyframe";
            og::runtime::emit_runtime_trace(std::move(trace));
            seed_client_snapshot_baseline(client.snapshot_state, full);
            client.allow_initial_keyframe_without_ready = false;
            client.budget_pending_keyframe = false;
            client.force_keyframe = false;
            remember_snapshot_hash(client, full);
            transport_.send_snapshot(
                peer_id,
                std::make_shared<WorldSnapshot>(std::move(full)));
            if (budgeted_keyframe && keyframe_budget_remaining > 0)
                --keyframe_budget_remaining;
            continue;
        }

        accumulate_snapshot_for_client(client.snapshot_state, snapshot);
        WorldSnapshot delta =
            consume_delta_snapshot_for_client(client.snapshot_state, snapshot);
        client.budget_pending_keyframe = false;
        client.force_keyframe = false;
        og::runtime::RuntimeTraceRecord trace =
            og::runtime::make_runtime_trace_record(
                "game_server", "broadcast_delta_snapshot");
        trace.tick = delta.tick_count;
        trace.snapshot_kind = "delta";
        og::runtime::emit_runtime_trace(std::move(trace));
        // Clients only auto-send hash checks for full snapshots and periodic
        // delta ticks. Skipping non-periodic deltas avoids same-tick pause or
        // exit keyframes being compared against an earlier delta hash for the
        // same authoritative tick.
        remember_snapshot_hash(
            client,
            delta,
            delta.tick_count != 0 &&
                (delta.tick_count % KEYFRAME_INTERVAL_TICKS) == 0);
        transport_.send_delta_snapshot(
            peer_id,
            std::make_shared<WorldSnapshot>(std::move(delta)));
    }

    consume_authoritative_visual_transients(world_, capture_mode);

    if (drained_batch.has_value())
        forward_event_batch(*drained_batch);

    if (snapshot.game_ended && snapshot.next_level != -1)
        handle_level_transition(snapshot.next_level);

    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "game_server", "broadcast_current_state_end"));
}

void GameServer::step()
{
    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "game_server", "step_begin"));
    const std::uint32_t next_tick = world_.tick_count_ + 1;
    events_.current_tick_ = next_tick;
    poll_incoming_messages();
    process_non_input_messages(next_tick);

    bool should_tick_world =
        !pending_exit_prompt_state_.has_value() && !pending_pause_state_.has_value();
    if (should_tick_world)
        should_tick_world = apply_polled_inputs(next_tick);
    if (should_tick_world)
        should_tick_world = process_disconnected_players(next_tick);
    if (should_tick_world)
        world_.tick();

    broadcast_current_state(SnapshotCaptureMode::Consume,
                            EventDeliveryMode::Drain);
    og::runtime::RuntimeTraceRecord trace =
        og::runtime::make_runtime_trace_record(
            "game_server", "step_end");
    trace.tick = world_.tick_count_;
    og::runtime::emit_runtime_trace(std::move(trace));
}

} // namespace og::sim
