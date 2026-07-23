#pragma once

#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/net_transport_websocket_server.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXWebSocket.h>
#endif

#include "test_gameplay_context_scope.h"

namespace og::sim::test {

enum class NetworkTransportBackend : std::uint8_t {
    InProcess,
    WebSocketLoopback,
};

template <typename Predicate>
bool wait_until(Predicate&& predicate,
                std::chrono::milliseconds timeout,
                std::chrono::milliseconds poll_interval =
                    std::chrono::milliseconds(5))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
            return true;
        std::this_thread::sleep_for(poll_interval);
    }
    return predicate();
}

#if !defined(__EMSCRIPTEN__)
class WebSocketLoopbackClientTransport final : public ITransport
{
public:
    explicit WebSocketLoopbackClientTransport(std::string url)
        : url_(std::move(url))
    {
        websocket_.disableAutomaticReconnection();
        websocket_.setUrl(url_);
        websocket_.setOnMessageCallback(
            [this](const ix::WebSocketMessagePtr& message) {
                handle_message(message);
            });
        websocket_.start();
        started_ = true;
    }

    ~WebSocketLoopbackClientTransport() override
    {
        stop();
    }

    void set_remote_peer_id(PeerId peer_id)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        remote_peer_id_ = peer_id;
    }

    bool wait_until_open(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(state_mutex_);
        return state_condition_.wait_for(lock, timeout, [this] {
            return open_ || !error_.empty();
        }) && open_;
    }

    std::string error() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return error_;
    }

    // Production websocket clients auto-reconnect when a connection errors
    // out, and GameClient reacts to the transport's disconnected->connected
    // edge by resending its hello (session token), which makes the server
    // re-bind the player and re-keyframe. This minimal loopback wrapper
    // disables IX auto-reconnection for deterministic teardown, so a
    // connection that WEDGES while still claiming Open — observed rarely on
    // loaded runners: the socket stops delivering frames (rx froze at the
    // baseline burst while siblings kept streaming) but never errors — would
    // starve its client forever. drop()+redial() force the production
    // recovery path. They are separate calls (not one kick()) so the caller
    // can let GameClient OBSERVE the disconnected state in between: a
    // stop+start pair that completes within one poll interval races the
    // GameClient's edge detection, and a missed edge means the hello is never
    // resent and the fresh server-side peer never gets bound or keyframed.
    void drop()
    {
        stop();
    }
    void redial()
    {
        websocket_.start();
        started_ = true;
    }

    void send(PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!open_)
            {
                // Production parity (WebSocketClientTransport): a send while
                // disconnected/reconnecting is a no-op, not a game-thread
                // throw. Matters during a kick(): GameClient::send_input sends
                // unconditionally and must not abort the test mid-recovery.
                return;
            }
            if (remote_peer_id_ != 0 && peer_id != remote_peer_id_)
            {
                throw std::runtime_error(std::format(
                    "WebSocketLoopbackClientTransport peer mismatch: expected {}, got {}",
                    remote_peer_id_,
                    peer_id));
            }
        }

        if (data == nullptr && len != 0)
            throw std::runtime_error("WebSocketLoopbackClientTransport send buffer is null");

        const char* raw = reinterpret_cast<const char*>(data);
        const std::string payload =
            (raw == nullptr || len == 0)
                ? std::string()
                : std::string(raw, raw + len);
        const ix::WebSocketSendInfo info = websocket_.sendBinary(payload);
        if (!info.success)
        {
            throw std::runtime_error(
                "WebSocketLoopbackClientTransport failed to send binary payload");
        }
    }

    [[nodiscard]] std::vector<ReceivedMessage> poll() override
    {
        std::deque<ReceivedMessage> queued_messages;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_, std::try_to_lock);
            if (!lock.owns_lock() || queue_.empty())
                return {};
            queued_messages.swap(queue_);
        }

        return {std::make_move_iterator(queued_messages.begin()),
                std::make_move_iterator(queued_messages.end())};
    }

    void accept_connections() override {}

    void disconnect(PeerId peer_id) override
    {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (remote_peer_id_ != 0 && peer_id != remote_peer_id_)
                return;
        }
        stop();
    }

    [[nodiscard]] std::vector<PeerId> connected_peers() const override
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!open_ || remote_peer_id_ == 0)
            return {};
        return {remote_peer_id_};
    }

private:
    void stop()
    {
        if (!started_)
            return;

        websocket_.stop();
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            open_ = false;
        }
        started_ = false;
        state_condition_.notify_all();
    }

    void handle_message(const ix::WebSocketMessagePtr& message)
    {
        if (!message)
            return;

        switch (message->type)
        {
        case ix::WebSocketMessageType::Open:
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            open_ = true;
            error_.clear();
            state_condition_.notify_all();
            break;
        }

        case ix::WebSocketMessageType::Message:
            if (message->binary)
            {
                PeerId remote_peer_id = 0;
                {
                    std::lock_guard<std::mutex> lock(state_mutex_);
                    remote_peer_id = remote_peer_id_;
                }
                std::lock_guard<std::mutex> queue_lock(queue_mutex_);
                queue_.push_back(
                    ReceivedMessage{
                        .peer_id = remote_peer_id,
                        .data = std::vector<std::uint8_t>(message->str.begin(),
                                                          message->str.end()),
                    });
            }
            break;

        case ix::WebSocketMessageType::Close:
        case ix::WebSocketMessageType::Error:
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            open_ = false;
            if (message->type == ix::WebSocketMessageType::Error && error_.empty())
                error_ = message->errorInfo.reason;
            state_condition_.notify_all();
            break;
        }

        default:
            break;
        }
    }

    std::string url_;
    ix::WebSocket websocket_;
    bool started_ = false;

    mutable std::mutex state_mutex_;
    std::condition_variable state_condition_;
    PeerId remote_peer_id_ = 0;
    bool open_ = false;
    std::string error_;

    mutable std::mutex queue_mutex_;
    std::deque<ReceivedMessage> queue_;
};
#endif

struct NetworkTestConfig {
    std::size_t player_count = 1;
    int level_id = 1;
    std::uint32_t tick_count = 0;
    bool validate_serialization =
        kInProcessTransportValidateSerializationDefault;
    std::vector<short> player_teams = {};
    std::function<InputState(std::size_t client_index, std::uint32_t tick)>
        input_sequence = {};
    NetworkTransportBackend transport_backend =
        NetworkTransportBackend::InProcess;
    std::chrono::milliseconds network_timeout = std::chrono::seconds(5);
};

class NetworkTestFixture
{
public:
    explicit NetworkTestFixture(NetworkTestConfig config = {})
        : config_(std::move(config))
        , server_world_(config_.level_id)
    {
        prepare_default_campaign();

        if (config_.player_count == 0)
            config_.player_count = 1;
        if (config_.player_count > static_cast<std::size_t>(MAX_PLAYERS))
        {
            throw std::invalid_argument(
                "NetworkTestFixture supports up to MAX_PLAYERS clients");
        }

        server_transport_ = create_server_transport();
        server_transport_->accept_connections();
        server_ = std::make_unique<GameServer>(
            server_world_.world(),
            server_world_.events,
            *server_transport_);

        if (uses_websocket_transport())
        {
            create_websocket_clients();
        }
        else
        {
            create_inprocess_clients();
        }
    }

    void load_level()
    {
        ASSERT_FALSE(level_loaded_);

        ASSERT_TRUE(server_world_.load_level())
            << "server level load should succeed";
        for (auto& client : clients_)
        {
            ASSERT_TRUE(client->world.load_level())
                << "client level load should succeed";
        }

        bind_players();

        level_loaded_ = true;
    }

    void initial_sync()
    {
        ensure_level_loaded();
        ASSERT_FALSE(initial_sync_complete_);

        with_server_context([&] {
            server_->send_initial_snapshots(SnapshotCaptureMode::Peek);
        });
        if (uses_websocket_transport())
        {
            ASSERT_TRUE(wait_until(
                [&] {
                    poll_all_clients();
                    return std::all_of(
                        clients_.begin(), clients_.end(),
                        [](const std::unique_ptr<ClientState>& client) {
                            return client->game_client != nullptr &&
                                client->game_client->baseline().has_value() &&
                                client->game_client->client_ready_count() > 0;
                        });
                },
                config_.network_timeout))
                << "websocket clients should receive the initial snapshot and send ready";
        }
        else
        {
            poll_all_clients();
        }

        initial_sync_complete_ = true;
    }

    void apply_server_snapshot(const WorldSnapshot& snapshot)
    {
        ensure_level_loaded();
        ASSERT_FALSE(initial_sync_complete_);

        with_server_context([&] {
            apply_snapshot(server_world_.world(), snapshot);
            server_world_.world().clear_removed_entity_ids();
            server_world_.world().clear_grid_dirty_tiles();
            server_world_.events.clear();
        });
        bind_players();
        with_server_context([&] {
            server_world_.world().control_hp = snapshot.control_hp;
        });
    }

    void step_tick(const InputState& input)
    {
        ensure_initial_sync();

        const std::uint32_t next_tick =
            server_world_.world().tick_count_ + 1;
        send_explicit_input(input, next_tick);
        step_server_once();
        poll_clients_for_server_tick();
    }

    void step_ticks(std::uint32_t tick_count)
    {
        ensure_initial_sync();

        for (std::uint32_t tick_index = 0; tick_index < tick_count; ++tick_index)
        {
            const std::uint32_t next_tick =
                server_world_.world().tick_count_ + 1;
            send_client_inputs(next_tick);
            step_server_once();
            poll_clients_for_server_tick();
            // A failed catch-up wait already burned the full network timeout;
            // repeating it for every remaining tick would turn one failure
            // into an hour-long run. Stop at the first fatal failure.
            if (::testing::Test::HasFatalFailure())
                return;
        }
    }

    void run()
    {
        load_level();
        initial_sync();
        step_ticks(config_.tick_count);
    }

    void expect_clients_match_server()
    {
        expect_clients_match_server_impl(false);
    }

    void expect_clients_match_server_ignoring_visual_transients()
    {
        // hurt_flash is delivered for rendering, then consumed on the server
        // after broadcast. A client may therefore retain the just-delivered
        // value until the following delta without any simulation divergence.
        expect_clients_match_server_impl(true);
    }

    GameWorld& server_world() { return server_world_.world(); }
    SimEventLog& server_events() { return server_world_.events; }
    GameWorld& client_world(std::size_t client_index)
    {
        return clients_.at(client_index)->world.world();
    }

    InProcessTransport& server_transport()
    {
        if (!server_transport_inprocess_)
            throw std::logic_error("NetworkTestFixture server transport is not in-process");
        return *server_transport_inprocess_;
    }
    InProcessTransport& client_transport(std::size_t client_index)
    {
        if (!clients_.at(client_index)->inprocess_transport)
        {
            throw std::logic_error(
                "NetworkTestFixture client transport is not in-process");
        }
        return *clients_.at(client_index)->inprocess_transport;
    }
    walker* server_control(std::size_t player_index) const noexcept
    {
        return server_ ? server_->player_control(player_index) : nullptr;
    }
    // Clears every control claim and re-runs the initial player binding.
    // Lets tests stage walker state (e.g. myguy owner tags) after load_level
    // and observe how bind_player resolves it.
    void rebind_players()
    {
        bind_players();
    }
    GameServer& server() { return *server_; }
    GameClient& client(std::size_t client_index)
    {
        return *clients_.at(client_index)->game_client;
    }

    const std::vector<TypedReceivedMessage>& server_inbox() const noexcept
    {
        return server_->last_polled_messages();
    }

    template <typename Fn>
    auto with_server_context(Fn&& fn)
        -> std::invoke_result_t<Fn&>
    {
        return server_world_.with_context(std::forward<Fn>(fn));
    }

    template <typename Fn>
    auto with_client_context(std::size_t client_index, Fn&& fn)
        -> std::invoke_result_t<Fn&>
    {
        return clients_.at(client_index)->world.with_context(std::forward<Fn>(fn));
    }

    void poll_client_messages(std::size_t client_index)
    {
        with_client_context(client_index, [&] {
            clients_.at(client_index)->game_client->poll_messages();
        });
    }

private:
    void expect_clients_match_server_impl(bool ignore_visual_transients)
    {
        ensure_initial_sync();

        WorldSnapshot server_snapshot = server_world_.with_context([&] {
            return capture_keyframe_snapshot(server_world_.world());
        });
        if (ignore_visual_transients)
            clear_visual_transients(server_snapshot);

        for (std::size_t index = 0; index < clients_.size(); ++index)
        {
            ClientState& client = *clients_[index];
            WorldSnapshot client_snapshot = client.world.with_context([&] {
                return capture_keyframe_snapshot(client.world.world());
            });
            if (ignore_visual_transients)
                clear_visual_transients(client_snapshot);
            const auto failure = find_first_snapshot_difference(
                server_snapshot.tick_count, server_snapshot, client_snapshot);
            ASSERT_FALSE(failure.has_value())
                << "client " << index << " diverged at field "
                << (failure ? failure->field : std::string{})
                << " expected "
                << (failure ? failure->expected_value : std::string{})
                << " actual "
                << (failure ? failure->actual_value : std::string{});
        }
    }

    static void clear_visual_transients(WorldSnapshot& snapshot)
    {
        const auto clear_hurt_flash = [](std::vector<EntitySnapshot>& entities) {
            for (EntitySnapshot& entity : entities)
                entity.hurt_flash = 0;
        };
        clear_hurt_flash(snapshot.oblist);
        clear_hurt_flash(snapshot.fxlist);
        clear_hurt_flash(snapshot.weaplist);
        snapshot.snapshot_hash = compute_snapshot_hash(snapshot);
    }

    struct FixtureWorld {
        explicit FixtureWorld(int level_id)
            : level(level_id, true, &sdl_level_data_hooks())
        {
            level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                                  &level.world().rng_, &cfg);
            gc.rng = &level.world().rng_;
        }

        template <typename Fn>
        std::invoke_result_t<Fn&> with_context(Fn&& fn)
        {
            ScopedGameplayContext gameplay(level, save, events, cfg);
            push_test_context(&gc);
            struct PopGuard {
                ~PopGuard() { pop_test_context(); }
            } pop_guard;
            using Result = std::invoke_result_t<Fn&>;
            if constexpr (std::is_void_v<Result>)
            {
                std::forward<Fn>(fn)();
            }
            else
            {
                return std::forward<Fn>(fn)();
            }
        }

        bool load_level()
        {
            return with_context([&] { return level.load(); });
        }

        GameWorld& world() { return level.world(); }

        LevelRuntimeData level;
        SaveData save;
        SimEventLog events;
        GameContext gc;
    };

    struct ClientState {
        explicit ClientState(int level_id)
            : world(level_id)
        {
        }

        PeerId peer_id() const
        {
            return peer_id_value;
        }

        FixtureWorld world;
        PeerId peer_id_value = 0;
        std::shared_ptr<ITransport> transport;
        std::shared_ptr<InProcessTransport> inprocess_transport;
#if !defined(__EMSCRIPTEN__)
        std::shared_ptr<WebSocketLoopbackClientTransport> websocket_transport;
#endif
        std::unique_ptr<GameClient> game_client;
    };

    static void prepare_default_campaign()
    {
        restore_default_campaigns();
        restore_default_settings();
        const CampaignPackageIoError error =
            mount_campaign_package_with_error("org.openglad.gladiator");
        if (error != CampaignPackageIoError::None)
        {
            throw std::runtime_error(
                "NetworkTestFixture failed to mount default campaign");
        }
    }

    void ensure_level_loaded()
    {
        if (!level_loaded_)
            load_level();
    }

    void ensure_initial_sync()
    {
        if (!initial_sync_complete_)
        {
            ensure_level_loaded();
            initial_sync();
        }
    }

    void bind_players()
    {
        with_server_context([&] {
            clear_control_claims(server_world_.world());
            for (std::size_t index = 0; index < clients_.size(); ++index)
            {
                server_->bind_player(clients_[index]->peer_id(),
                                     index,
                                     player_team(index));
            }
        });
    }

    void poll_all_clients()
    {
        for (auto& client : clients_)
        {
            client->world.with_context([&] {
                client->game_client->poll_messages();
            });
        }
    }

    void poll_clients_for_server_tick()
    {
        const std::uint32_t target_tick = server_world_.world().tick_count_;
        if (!uses_websocket_transport())
        {
            poll_all_clients();
            return;
        }

        const auto all_clients_caught_up = [&] {
            return std::all_of(
                clients_.begin(), clients_.end(),
                [target_tick](const std::unique_ptr<ClientState>& client) {
                    return client->game_client != nullptr &&
                        client->game_client->last_seen_server_tick() >= target_tick;
                });
        };

        // Lockstep catch-up loop. The production server pumps its transport every
        // frame and re-keyframes lagging clients; this harness must do the same
        // while it waits, otherwise a client that (under load/scheduler jitter)
        // missed the one-shot per-tick broadcast has no way to recover and stalls
        // for the whole timeout. So each iteration we (a) service the server +
        // client transports and (b) periodically re-deliver the current
        // authoritative state. broadcast_current_state(Peek, Skip) is
        // non-destructive: Peek does not consume snapshot delta state and Skip
        // does not drain pending events, so re-sending is safe and idempotent;
        // a far-behind client receives a fresh keyframe and catches up.
        const auto deadline =
            std::chrono::steady_clock::now() + config_.network_timeout;
        auto next_resync = std::chrono::steady_clock::now();
        // Watchdog for silently wedged websocket connections: a client whose
        // socket hangs while still claiming Open (rare under runner load)
        // receives nothing — no re-broadcast can heal it and no error ever
        // fires (IX auto-reconnection is disabled in the loopback wrapper).
        // If a client makes no progress for kSilentClientKick while its
        // siblings advance, kick() its socket: the reconnect + hello resend +
        // server re-keyframe path recovers it within a few ticks.
        constexpr std::chrono::seconds kSilentClientKick{3};
        std::vector<std::uint32_t> last_ticks(clients_.size(), 0u);
        std::vector<std::chrono::steady_clock::time_point> last_progress(
            clients_.size(), std::chrono::steady_clock::now());
        while (std::chrono::steady_clock::now() < deadline)
        {
            (void)server_transport_->poll();
            with_server_context([&] { server_->poll_incoming_messages(); });
            poll_all_clients();
            if (all_clients_caught_up())
                return;

            const auto now = std::chrono::steady_clock::now();
            if (now >= next_resync)
            {
                with_server_context([&] {
                    server_->broadcast_current_state(SnapshotCaptureMode::Peek,
                                                     EventDeliveryMode::Skip);
                });
                next_resync = now + std::chrono::milliseconds(100);
            }
            for (std::size_t ci = 0; ci < clients_.size(); ++ci)
            {
                const auto& c = clients_[ci];
                if (!c->game_client)
                    continue;
                const std::uint32_t seen =
                    c->game_client->last_seen_server_tick();
                if (seen != last_ticks[ci])
                {
                    last_ticks[ci] = seen;
                    last_progress[ci] = now;
                }
                else if (seen < target_tick && c->websocket_transport &&
                         now - last_progress[ci] >= kSilentClientKick)
                {
                    std::fprintf(stderr,
                                 "[network_fixture] client %zu silent at tick "
                                 "%u (target %u) — kicking wedged websocket\n",
                                 ci, seen, target_tick);
                    kick_wedged_websocket_client(ci);
                    last_progress[ci] = now;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        poll_all_clients();
        ASSERT_TRUE(all_clients_caught_up())
            << "websocket clients should catch up to authoritative tick "
            << target_tick;
    }

    void send_client_inputs(std::uint32_t tick)
    {
        for (std::size_t index = 0; index < clients_.size(); ++index)
        {
            if (!config_.input_sequence)
                continue;

            clients_[index]->game_client->send_input(
                config_.input_sequence(index, tick),
                tick);
        }
    }

    void send_explicit_input(const InputState& input, std::uint32_t tick)
    {
        for (std::size_t index = 0; index < clients_.size(); ++index)
        {
            clients_[index]->game_client->send_input(
                input_for_client(input, index),
                tick);
        }
    }

    static InputState input_for_client(const InputState& input,
                                       std::size_t client_index)
    {
        InputState client_input;
        client_input.quit_requested = input.quit_requested;
        client_input.timer_wait_request = input.timer_wait_request;
        if (client_index < static_cast<std::size_t>(MAX_PLAYERS))
        {
            client_input.players[client_index] =
                input.players[client_index];
        }
        return client_input;
    }

    short player_team(std::size_t player_index)
    {
        if (player_index < config_.player_teams.size())
            return config_.player_teams[player_index];
        return server_world_.world().my_team;
    }

    static void clear_control_claims(GameWorld& world)
    {
        for (auto& uptr : world.oblist)
        {
            walker* const entity = uptr.get();
            if (entity == nullptr || entity->user() == -1)
                continue;

            entity->set_user(-1);
            entity->restore_act_type();
        }
        world.control_hp = 0.0f;
    }

    [[nodiscard]] bool uses_websocket_transport() const noexcept
    {
        return config_.transport_backend ==
            NetworkTransportBackend::WebSocketLoopback;
    }

    std::shared_ptr<ITransport> create_server_transport()
    {
        if (!uses_websocket_transport())
        {
            server_transport_inprocess_ = InProcessTransport::create_server(
                {.validate_serialization = config_.validate_serialization});
            return server_transport_inprocess_;
        }

#if defined(__EMSCRIPTEN__)
        throw std::runtime_error(
            "WebSocket loopback transport is unavailable under Emscripten");
#else
        const int port = ix::getFreePort();
        WebSocketServerTransport::Options options;
        options.host = "127.0.0.1";
        websocket_server_url_ = std::format("ws://127.0.0.1:{}", port);
        server_transport_websocket_ =
            std::make_shared<WebSocketServerTransport>(port, options);
        return server_transport_websocket_;
#endif
    }

    void create_inprocess_clients()
    {
        clients_.reserve(config_.player_count);
        for (std::size_t index = 0; index < config_.player_count; ++index)
        {
            auto client = std::make_unique<ClientState>(config_.level_id);
            client->inprocess_transport = server_transport_inprocess_->create_client_transport();
            client->transport = client->inprocess_transport;
            client->peer_id_value = client->inprocess_transport->local_peer_id();
            server_->connect_client(client->peer_id());
            client->game_client = std::make_unique<GameClient>(
                *client->transport,
                client->peer_id(),
                &client->world.world());
            clients_.push_back(std::move(client));
        }
    }

    void create_websocket_clients()
    {
#if defined(__EMSCRIPTEN__)
        throw std::runtime_error(
            "WebSocket loopback transport is unavailable under Emscripten");
#else
        clients_.reserve(config_.player_count);
        connected_server_peers_.clear();
        for (std::size_t index = 0; index < config_.player_count; ++index)
        {
            auto client = std::make_unique<ClientState>(config_.level_id);
            client->websocket_transport =
                std::make_shared<WebSocketLoopbackClientTransport>(
                    websocket_server_url_);
            client->transport = client->websocket_transport;
            if (!client->websocket_transport->wait_until_open(config_.network_timeout))
            {
                throw std::runtime_error(std::format(
                    "NetworkTestFixture websocket client {} failed to connect: {}",
                    index,
                    client->websocket_transport->error()));
            }

            client->peer_id_value = wait_for_next_websocket_peer();
            with_server_context([&] {
                server_->poll_incoming_messages();
            });
            client->websocket_transport->set_remote_peer_id(client->peer_id());
            client->game_client = std::make_unique<GameClient>(
                *client->transport,
                client->peer_id(),
                &client->world.world());
            clients_.push_back(std::move(client));
        }
#endif
    }

    void step_server_once()
    {
        with_server_context([&] {
            server_->step();
        });
    }

#if !defined(__EMSCRIPTEN__)
    std::vector<PeerId> poll_websocket_server_peers()
    {
        if (!uses_websocket_transport())
            return {};

        (void)server_transport_->poll();

        std::vector<PeerId> current_peers = server_transport_->connected_peers();
        std::vector<PeerId> added_peers;
        std::set_difference(current_peers.begin(), current_peers.end(),
                            connected_server_peers_.begin(), connected_server_peers_.end(),
                            std::back_inserter(added_peers));
        connected_server_peers_ = std::move(current_peers);
        return added_peers;
    }

    // Recovery for a silently wedged loopback websocket connection (it stops
    // delivering frames while still claiming Open; no error ever fires, so
    // neither IX auto-reconnection nor the server's re-broadcasts can heal
    // it). Force the full production-equivalent rejoin, using the fixture's
    // server-side authority instead of the hello/session-token dance (this
    // harness binds players directly and never runs that handshake):
    //   1. drop + re-dial the client socket (fresh connection),
    //   2. discover the fresh connection's new server-side peer id,
    //   3. re-bind the SAME player slot + control walker to the new peer, and
    //   4. recreate the GameClient against the new peer id (it is baked into
    //      the client at construction).
    // The catch-up loop's periodic broadcast_current_state then sends the new
    // peer its initial setup + keyframe, the client acks ready, and per-tick
    // snapshots resume.
    void kick_wedged_websocket_client(std::size_t client_index)
    {
        ClientState& c = *clients_.at(client_index);
        if (!c.websocket_transport)
            return;

        c.websocket_transport->drop();
        c.websocket_transport->redial();
        if (!c.websocket_transport->wait_until_open(std::chrono::seconds(2)))
        {
            std::fprintf(stderr,
                         "[network_fixture] client %zu re-dial failed: %s\n",
                         client_index,
                         c.websocket_transport->error().c_str());
            return;
        }

        PeerId new_peer_id = 0;
        wait_until(
            [&] {
                const std::vector<PeerId> added =
                    poll_websocket_server_peers();
                if (!added.empty())
                    new_peer_id = added.back();
                return new_peer_id != 0;
            },
            std::chrono::seconds(2));
        if (new_peer_id == 0)
        {
            std::fprintf(stderr,
                         "[network_fixture] client %zu reconnected but no new "
                         "server peer appeared\n",
                         client_index);
            return;
        }

        walker* const control = server_->player_control(client_index);
        with_server_context([&] {
            server_->poll_incoming_messages();
            // bind_player refreshes world_.control_hp from the control's
            // current hitpoints — a mid-stream write to snapshot-hashed world
            // state that would break every in-flight client's hash check.
            // This heal must be side-effect-free on the sim: restore it.
            GameWorld& world = server_world();
            const float prior_control_hp = world.control_hp;
            server_->bind_player(new_peer_id, client_index,
                                 player_team(client_index), control);
            world.control_hp = prior_control_hp;
        });
        c.peer_id_value = new_peer_id;
        c.websocket_transport->set_remote_peer_id(new_peer_id);
        c.game_client = std::make_unique<GameClient>(*c.transport, new_peer_id,
                                                     &c.world.world());
    }

    PeerId wait_for_next_websocket_peer()
    {
        std::vector<PeerId> added_peers;
        const bool connected = wait_until(
            [&] {
                added_peers = poll_websocket_server_peers();
                return !added_peers.empty();
            },
            config_.network_timeout);
        if (!connected)
        {
            throw std::runtime_error(std::format(
                "NetworkTestFixture timed out waiting for next websocket peer"));
        }

        if (added_peers.size() != 1u)
            throw std::runtime_error("NetworkTestFixture failed to identify websocket peer");
        return added_peers.front();
    }
#else
    std::vector<PeerId> poll_websocket_server_peers()
    {
        return {};
    }
#endif

    NetworkTestConfig config_;
    FixtureWorld server_world_;
    std::shared_ptr<ITransport> server_transport_;
    std::shared_ptr<InProcessTransport> server_transport_inprocess_;
    std::shared_ptr<WebSocketServerTransport> server_transport_websocket_;
    std::unique_ptr<GameServer> server_;
    std::vector<std::unique_ptr<ClientState>> clients_;
    std::vector<PeerId> connected_server_peers_;
    std::string websocket_server_url_;
    bool level_loaded_ = false;
    bool initial_sync_complete_ = false;
};

} // namespace og::sim::test
