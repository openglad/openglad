#include <openglad/gameplay/game_server.h>

#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/sim_event_log.h>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace
{

using EntityListKind = og::sim::SnapshotEntityListKind;
using PendingRemovedEntity = og::sim::PendingRemovedEntity;

bool contains_entity_id(const std::vector<std::uint32_t>& ids,
                        std::uint32_t entity_id)
{
    return std::find(ids.begin(), ids.end(), entity_id) != ids.end();
}

void erase_entity_id(std::vector<std::uint32_t>& ids, std::uint32_t entity_id)
{
    ids.erase(std::remove(ids.begin(), ids.end(), entity_id), ids.end());
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
        if (mask_it == client_state.accumulated_dirty.end() ||
            !dirty_mask_has_bits(mask_it->second))
        {
            continue;
        }

        delta_entity.dirty_mask[0] = mask_it->second[0];
        delta_entity.dirty_mask[1] = mask_it->second[1];
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

bool is_game_flow_event(og::sim::EventKind kind) noexcept
{
    switch (kind)
    {
    case og::sim::EventKind::EndGame:
    case og::sim::EventKind::SetEnd:
    case og::sim::EventKind::RequestExitConfirmation:
    case og::sim::EventKind::WithdrawToLevel:
        return true;

    case og::sim::EventKind::None:
    case og::sim::EventKind::PlaySound:
    case og::sim::EventKind::Notification:
    case og::sim::EventKind::SetPalette:
    case og::sim::EventKind::RequestRedraw:
    case og::sim::EventKind::ScoreChange:
        return false;
    }

    return false;
}

void split_event_batches(const og::sim::SimEventBatch& source,
                         og::sim::SimEventBatch& sim_batch,
                         og::sim::SimEventBatch& game_flow_batch)
{
    sim_batch.sequence = source.sequence;
    game_flow_batch.sequence = source.sequence;
    for (const auto& event : source.events)
    {
        if (is_game_flow_event(event.kind))
            game_flow_batch.events.push_back(event);
        else
            sim_batch.events.push_back(event);
    }
}

std::vector<og::sim::TypedReceivedMessage> poll_server_messages(
    og::sim::ITransport& transport)
{
    if (transport.supports_typed_messages())
        return transport.poll_typed();

    std::vector<og::sim::TypedReceivedMessage> typed_messages;
    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
        {
            throw std::runtime_error(
                "GameServer received malformed transport header");
        }
        if (envelope.message_type != og::sim::kInputMessageType)
            continue;

        const std::optional<og::sim::InputStateMessage> decoded =
            og::sim::deserialize_input_message(message.data);
        if (!decoded.has_value())
        {
            throw std::runtime_error("GameServer failed to deserialize input");
        }

        og::sim::TypedReceivedMessage typed_message;
        typed_message.peer_id = message.peer_id;
        typed_message.kind = og::sim::TypedReceivedMessageKind::Input;
        typed_message.input = std::make_shared<InputState>(decoded->input);
        typed_message.tick = decoded->tick;
        typed_messages.push_back(std::move(typed_message));
    }

    return typed_messages;
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
}

void GameServer::connect_client(PeerId peer_id)
{
    clients_.try_emplace(peer_id);
}

void GameServer::disconnect_client(PeerId peer_id)
{
    clients_.erase(peer_id);
    transport_.disconnect(peer_id);
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
    client.control = control;
    client.has_player_binding = true;
    player_controls_[player_index] = control;
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
}

walker* GameServer::player_control(std::size_t player_index) const noexcept
{
    return player_index < static_cast<std::size_t>(MAX_PLAYERS)
        ? player_controls_[player_index]
        : nullptr;
}

void GameServer::poll_incoming_messages()
{
    last_polled_messages_ = poll_server_messages(transport_);
}

void GameServer::apply_polled_inputs(std::uint32_t expected_tick)
{
    for (const auto& message : last_polled_messages_)
    {
        if (message.kind != TypedReceivedMessageKind::Input || !message.input)
            continue;

        if (expected_tick != 0 && message.tick != expected_tick)
        {
            throw std::runtime_error(std::format(
                "GameServer expected input tick {} but received {} from peer {}",
                expected_tick,
                message.tick,
                message.peer_id));
        }

        auto client_it = clients_.find(message.peer_id);
        if (client_it == clients_.end() || !client_it->second.has_player_binding)
            continue;

        ConnectedClientState& client = client_it->second;
        const std::size_t player_index = client.player_index;
        const SimInputResult result = sim_process_player_input(
            select_player_input(*message.input, player_index),
            client.control,
            world_,
            static_cast<short>(player_index),
            client.team_num,
            player_input_debounce_[player_index],
            special_names_,
            &events_);

        player_controls_[player_index] = client.control;
        if (result.control_hp_changed)
            world_.control_hp = result.control_hp;
        if (result.endgame_requested)
        {
            world_.ending = result.endgame_type;
            world_.end = 1;
        }
    }
}

void GameServer::send_initial_snapshot(PeerId peer_id,
                                       SnapshotCaptureMode capture_mode)
{
    ConnectedClientState& client = clients_[peer_id];
    WorldSnapshot keyframe = capture_server_keyframe(world_, capture_mode);
    seed_client_snapshot_baseline(client.snapshot_state, keyframe);
    client.has_initial_snapshot = true;
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

void GameServer::broadcast_current_state(SnapshotCaptureMode capture_mode,
                                         EventDeliveryMode event_mode)
{
    WorldSnapshot snapshot = capture_server_snapshot(world_, capture_mode);
    std::optional<WorldSnapshot> initial_keyframe = std::nullopt;
    const bool needs_initial_keyframe = std::any_of(
        clients_.begin(), clients_.end(),
        [](const auto& entry) { return !entry.second.has_initial_snapshot; });
    if (needs_initial_keyframe)
        initial_keyframe = capture_server_keyframe(world_, capture_mode);

    SimEventBatch sim_batch;
    SimEventBatch game_flow_batch;
    if (event_mode == EventDeliveryMode::Drain)
    {
        const SimEventBatch drained = drain_sim_events(events_);
        split_event_batches(drained, sim_batch, game_flow_batch);
    }

    for (auto& [peer_id, client] : clients_)
    {
        if (!client.has_initial_snapshot)
        {
            WorldSnapshot keyframe = *initial_keyframe;
            seed_client_snapshot_baseline(client.snapshot_state, keyframe);
            client.has_initial_snapshot = true;
            transport_.send_snapshot(
                peer_id,
                std::make_shared<WorldSnapshot>(std::move(keyframe)));
        }
        else
        {
            accumulate_snapshot_for_client(client.snapshot_state, snapshot);
            WorldSnapshot delta =
                consume_delta_snapshot_for_client(client.snapshot_state, snapshot);
            transport_.send_delta_snapshot(
                peer_id,
                std::make_shared<WorldSnapshot>(std::move(delta)));
        }

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

void GameServer::step()
{
    const std::uint32_t next_tick = world_.tick_count_ + 1;
    events_.current_tick_ = next_tick;
    poll_incoming_messages();
    apply_polled_inputs(next_tick);
    world_.tick();
    broadcast_current_state(SnapshotCaptureMode::Consume,
                            EventDeliveryMode::Drain);
}

} // namespace og::sim
