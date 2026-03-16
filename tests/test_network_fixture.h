#pragma once

#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/platform/game_context.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "test_gameplay_context_scope.h"

namespace og::sim::test {

struct NetworkTestConfig {
    std::size_t player_count = 1;
    int level_id = 1;
    std::uint32_t tick_count = 0;
    bool validate_serialization =
        kInProcessTransportValidateSerializationDefault;
    std::function<InputState(std::size_t client_index, std::uint32_t tick)>
        input_sequence;
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

        server_transport_ = InProcessTransport::create_server(
            {.validate_serialization = config_.validate_serialization});
        server_transport_->accept_connections();

        clients_.reserve(config_.player_count);
        for (std::size_t index = 0; index < config_.player_count; ++index)
        {
            auto client = std::make_unique<ClientState>(config_.level_id);
            client->transport = server_transport_->create_client_transport();
            clients_.push_back(std::move(client));
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

        level_loaded_ = true;
    }

    void initial_sync()
    {
        ensure_level_loaded();
        ASSERT_FALSE(initial_sync_complete_);

        const WorldSnapshot keyframe = server_world_.with_context([&] {
            return capture_keyframe_snapshot(server_world_.world());
        });

        for (auto& client : clients_)
        {
            seed_client_snapshot_baseline(client->server_state, keyframe);
            server_transport_->send_snapshot(
                client->peer_id(),
                std::make_shared<WorldSnapshot>(keyframe));
            pump_client_messages(*client);
        }

        initial_sync_complete_ = true;
    }

    void step_ticks(std::uint32_t tick_count)
    {
        ensure_initial_sync();

        for (std::uint32_t tick_index = 0; tick_index < tick_count; ++tick_index)
        {
            const std::uint32_t next_tick =
                server_world_.world().tick_count_ + 1;
            send_client_inputs(next_tick);
            server_inbox_ = server_transport_->poll_typed();

            server_world_.with_context([&] {
                server_world_.world().tick();
            });
            const WorldSnapshot snapshot = server_world_.with_context([&] {
                return capture_snapshot(server_world_.world());
            });
            const SimEventBatch batch = drain_sim_events(server_world_.events);

            for (auto& client : clients_)
            {
                accumulate_snapshot_for_client(client->server_state, snapshot);
                const WorldSnapshot delta =
                    consume_delta_snapshot_for_client(client->server_state,
                                                      snapshot);
                server_transport_->send_delta_snapshot(
                    client->peer_id(),
                    std::make_shared<WorldSnapshot>(delta));
                server_transport_->send_sim_event_batch(
                    client->peer_id(),
                    std::make_shared<SimEventBatch>(batch));
                pump_client_messages(*client);
            }
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
        ensure_initial_sync();

        const WorldSnapshot server_snapshot = server_world_.with_context([&] {
            return capture_keyframe_snapshot(server_world_.world());
        });

        for (std::size_t index = 0; index < clients_.size(); ++index)
        {
            ClientState& client = *clients_[index];
            const WorldSnapshot client_snapshot = client.world.with_context([&] {
                return capture_keyframe_snapshot(client.world.world());
            });
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

    GameWorld& server_world() { return server_world_.world(); }
    GameWorld& client_world(std::size_t client_index)
    {
        return clients_.at(client_index)->world.world();
    }

    InProcessTransport& server_transport() { return *server_transport_; }
    InProcessTransport& client_transport(std::size_t client_index)
    {
        return *clients_.at(client_index)->transport;
    }

    const std::vector<TypedReceivedMessage>& server_inbox() const noexcept
    {
        return server_inbox_;
    }

private:
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
            return transport ? transport->local_peer_id() : 0;
        }

        FixtureWorld world;
        std::shared_ptr<InProcessTransport> transport;
        PerClientState server_state;
        WorldSnapshot baseline;
        std::vector<SimEventBatch> sim_event_batches;
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

    void send_client_inputs(std::uint32_t tick)
    {
        for (std::size_t index = 0; index < clients_.size(); ++index)
        {
            if (!config_.input_sequence)
                continue;

            auto input = std::make_shared<InputState>(
                config_.input_sequence(index, tick));
            clients_[index]->transport->send_input(clients_[index]->peer_id(),
                                                   std::move(input),
                                                   tick);
        }
    }

    void pump_client_messages(ClientState& client)
    {
        const std::vector<TypedReceivedMessage> messages =
            client.transport->poll_typed();
        for (const auto& message : messages)
        {
            switch (message.kind)
            {
            case TypedReceivedMessageKind::Snapshot:
                ASSERT_NE(nullptr, message.snapshot);
                client.baseline = *message.snapshot;
                client.world.with_context([&] {
                    apply_snapshot(client.world.world(), client.baseline);
                });
                break;

            case TypedReceivedMessageKind::DeltaSnapshot:
                ASSERT_NE(nullptr, message.snapshot);
                apply_delta(client.baseline, *message.snapshot);
                client.world.with_context([&] {
                    apply_snapshot(client.world.world(), client.baseline);
                });
                break;

            case TypedReceivedMessageKind::SimEventBatch:
            case TypedReceivedMessageKind::GameFlowEventBatch:
                ASSERT_NE(nullptr, message.event_batch);
                client.sim_event_batches.push_back(*message.event_batch);
                break;

            case TypedReceivedMessageKind::Input:
                FAIL() << "clients should not receive typed input messages";
                break;
            }
        }
    }

    NetworkTestConfig config_;
    FixtureWorld server_world_;
    std::shared_ptr<InProcessTransport> server_transport_;
    std::vector<std::unique_ptr<ClientState>> clients_;
    std::vector<TypedReceivedMessage> server_inbox_;
    bool level_loaded_ = false;
    bool initial_sync_complete_ = false;
};

} // namespace og::sim::test
