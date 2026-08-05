#include <openglad/core/runtime_trace.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

og::runtime::GameSession* shadow_active_game_session()
{
    if (og::runtime::current_game_session != nullptr)
        return og::runtime::current_game_session;
    return dynamic_cast<og::runtime::GameSession*>(og::runtime::current_session);
}

og::sim::WorldSnapshot make_storm_snapshot(std::uint32_t tick)
{
    og::sim::WorldSnapshot snapshot;
    snapshot.tick_count = tick;
    snapshot.rng_state = tick * 37u + 1u;
    snapshot.level_tick_count = tick;
    snapshot.current_palette_id = 0;
    snapshot.my_team = 0;
    snapshot.allied_mode = 0;
    snapshot.difficulty = 1;
    snapshot.grid_width = 0;
    snapshot.grid_height = 0;
    snapshot.grid_dirty = false;
    snapshot.grid_full_resend = false;
    return snapshot;
}

void push_storm_snapshots(og::sim::ITransport& server_transport,
                          og::sim::PeerId client_peer_id,
                          int count)
{
    for (int i = 0; i < count; ++i)
    {
        server_transport.send_snapshot(
            client_peer_id,
            std::make_shared<og::sim::WorldSnapshot>(
                make_storm_snapshot(static_cast<std::uint32_t>(i + 1))));
    }
}

int count_shadow_overflow_traces(
    const std::vector<og::runtime::RuntimeTraceRecord>& records)
{
    return static_cast<int>(std::count_if(
        records.begin(), records.end(),
        [](const og::runtime::RuntimeTraceRecord& record) {
            return record.category == "local_transport_shadow" &&
                record.event == "shadow_inbound_overflow";
        }));
}

} // namespace

TEST(LocalTransportShadowBudget,
     inactive_and_client_only_public_guards_preserve_state)
{
    og::runtime::GameSession::Config config;
    config.create_display = false;
    config.install_legacy_globals = false;
    og::runtime::GameSession session(config);
    auto session_scope = session.activate();
    screen* const display = session.screen_ptr();
    ASSERT_NE(nullptr, display);

    display->world().end = 0;
    display->redrawme = 0;
    const std::array<std::pair<std::uint8_t, std::string>, 1> companies = {
        std::pair<std::uint8_t, std::string>{0u, "Guard Company"}};
    EXPECT_FALSE(og::runtime::display_follow_cycle_target(session, 0, false));
    og::runtime::local_transport_shadow_set_player_companies(
        session, companies);
    EXPECT_EQ(nullptr,
              og::runtime::local_transport_shadow_testing_server_screen(
                  session));
    EXPECT_FALSE(
        og::runtime::local_transport_shadow_testing_server_pending_exit_prompt(
            session));
    EXPECT_TRUE(og::runtime::local_transport_shadow_abort_level(session));
    EXPECT_FALSE(og::runtime::local_transport_active(session));
    EXPECT_EQ(0, static_cast<int>(display->world().end));
    EXPECT_EQ(0, static_cast<int>(display->redrawme));

    auto server_transport = og::sim::InProcessTransport::create_server();
    server_transport->accept_connections();
    auto client_transport = server_transport->create_client_transport();
    session.networked_session_ = true;
    og::runtime::reset_network_client_transport_shadow(
        session,
        *display,
        client_transport,
        client_transport->local_peer_id(),
        0u);
    ASSERT_TRUE(og::runtime::local_transport_active(session));
    ASSERT_EQ(1u, og::runtime::local_transport_client_count(session));

    EXPECT_FALSE(og::runtime::display_follow_cycle_target(session, -1, false));
    EXPECT_FALSE(og::runtime::display_follow_cycle_target(session, 0, true));
    EXPECT_EQ(0, static_cast<int>(display->redrawme));
    EXPECT_FALSE(og::runtime::local_transport_shadow_abort_level(session));
    EXPECT_EQ(0, static_cast<int>(display->world().end));

    session.myscreen_ = nullptr;
    og::runtime::local_transport_shadow_finish_tick(session);
    EXPECT_EQ(1u, og::runtime::local_transport_client_count(session));
    session.myscreen_ = display;

    og::runtime::clear_local_transport_shadow(session);
    EXPECT_FALSE(og::runtime::local_transport_active(session));
    EXPECT_FALSE(session.networked_session_);
}

TEST(LocalTransportShadowBudget, client_drains_at_most_budget_per_call)
{
    const auto pair = og::sim::InProcessTransport::create_linked_pair();
    ASSERT_NE(nullptr, pair.server);
    ASSERT_NE(nullptr, pair.client);

    og::sim::GameClient client(*pair.client, pair.peer_id, nullptr);

    constexpr int kStormCount = 1000;
    push_storm_snapshots(*pair.server, pair.peer_id, kStormCount);

    client.poll_messages(0.0f, og::sim::MAX_INBOUND_MESSAGES_PER_TICK);

    EXPECT_EQ(og::sim::MAX_INBOUND_MESSAGES_PER_TICK,
              client.messages_drained_last_call());
    EXPECT_EQ(static_cast<std::size_t>(og::sim::MAX_INBOUND_MESSAGES_PER_TICK),
              client.last_polled_messages().size());
    EXPECT_GT(client.pending_inbound_message_count(), 0u)
        << "the un-drained queue must persist between bounded poll calls";
    EXPECT_EQ(static_cast<std::size_t>(
                  kStormCount - og::sim::MAX_INBOUND_MESSAGES_PER_TICK),
              client.pending_inbound_message_count());
}

TEST(LocalTransportShadowBudget, client_repeated_drain_until_empty)
{
    const auto pair = og::sim::InProcessTransport::create_linked_pair();
    og::sim::GameClient client(*pair.client, pair.peer_id, nullptr);

    constexpr int kStormCount = 1000;
    push_storm_snapshots(*pair.server, pair.peer_id, kStormCount);

    int total_drained = 0;
    int max_drained_in_one_call = 0;
    int call_count = 0;
    constexpr int kMaxCalls = (kStormCount + og::sim::MAX_INBOUND_MESSAGES_PER_TICK)
                                  * 2;
    while (call_count < kMaxCalls)
    {
        client.poll_messages(0.0f, og::sim::MAX_INBOUND_MESSAGES_PER_TICK);
        const int drained = client.messages_drained_last_call();
        max_drained_in_one_call = std::max(max_drained_in_one_call, drained);
        total_drained += drained;
        ++call_count;
        if (drained == 0 && client.pending_inbound_message_count() == 0u)
            break;
    }

    EXPECT_EQ(0u, client.pending_inbound_message_count());
    EXPECT_EQ(kStormCount, total_drained);
    EXPECT_LE(max_drained_in_one_call, og::sim::MAX_INBOUND_MESSAGES_PER_TICK);
}

TEST(LocalTransportShadowBudget, server_drains_at_most_budget_per_call)
{
    const auto pair = og::sim::InProcessTransport::create_linked_pair();

    constexpr int kStormCount = 1000;
    for (int i = 0; i < kStormCount; ++i)
    {
        og::sim::HeartbeatMessage heartbeat;
        pair.client->send_heartbeat(
            pair.peer_id,
            std::make_shared<og::sim::HeartbeatMessage>(heartbeat));
    }

    GameWorld throwaway_world;
    og::sim::SimEventLog throwaway_events;
    og::sim::GameServer server(throwaway_world, throwaway_events, *pair.server);

    server.poll_incoming_messages(og::sim::MAX_INBOUND_MESSAGES_PER_TICK);
    EXPECT_EQ(og::sim::MAX_INBOUND_MESSAGES_PER_TICK,
              server.messages_drained_last_call());
    EXPECT_EQ(static_cast<std::size_t>(
                  kStormCount - og::sim::MAX_INBOUND_MESSAGES_PER_TICK),
              server.pending_inbound_message_count());

    int total_drained = og::sim::MAX_INBOUND_MESSAGES_PER_TICK;
    int max_drained_in_one_call = og::sim::MAX_INBOUND_MESSAGES_PER_TICK;
    constexpr int kMaxCalls =
        (kStormCount + og::sim::MAX_INBOUND_MESSAGES_PER_TICK) * 2;
    int call_count = 1;
    while (call_count < kMaxCalls)
    {
        server.poll_incoming_messages(og::sim::MAX_INBOUND_MESSAGES_PER_TICK);
        const int drained = server.messages_drained_last_call();
        max_drained_in_one_call = std::max(max_drained_in_one_call, drained);
        total_drained += drained;
        ++call_count;
        if (drained == 0 && server.pending_inbound_message_count() == 0u)
            break;
    }

    EXPECT_EQ(0u, server.pending_inbound_message_count());
    EXPECT_EQ(kStormCount, total_drained);
    EXPECT_LE(max_drained_in_one_call, og::sim::MAX_INBOUND_MESSAGES_PER_TICK);
}

TEST(LocalTransportShadowBudget, shadow_emits_overflow_trace_on_storm)
{
    og::runtime::GameSession* const session = shadow_active_game_session();
    ASSERT_NE(nullptr, session);
    ASSERT_NE(nullptr, session->myscreen_);

    auto server_transport = og::sim::InProcessTransport::create_server();
    server_transport->accept_connections();
    auto client_transport = server_transport->create_client_transport();
    const og::sim::PeerId client_peer_id = client_transport->local_peer_id();

    og::runtime::reset_network_client_transport_shadow(
        *session,
        *session->myscreen_,
        client_transport,
        client_peer_id,
        0u);
    ASSERT_TRUE(og::runtime::local_transport_active(*session));

    constexpr int kStormCount = 1000;
    push_storm_snapshots(*server_transport, client_peer_id, kStormCount);

    og::runtime::set_runtime_trace_enabled(true);
    og::runtime::clear_runtime_trace();

    og::runtime::local_transport_shadow_finish_tick(*session);
    {
        const auto records = og::runtime::copy_runtime_trace();
        EXPECT_EQ(1, count_shadow_overflow_traces(records))
            << "first storm tick must emit exactly one shadow_inbound_overflow";
    }

    int total_overflow_traces = count_shadow_overflow_traces(
        og::runtime::copy_runtime_trace());
    constexpr int kMaxTicks =
        (kStormCount + og::sim::MAX_INBOUND_MESSAGES_PER_TICK) * 2;
    int storm_ticks = 1;
    int consecutive_quiet_ticks = 0;
    while (storm_ticks < kMaxTicks && consecutive_quiet_ticks < 2)
    {
        og::runtime::clear_runtime_trace();
        og::runtime::local_transport_shadow_finish_tick(*session);
        const auto records = og::runtime::copy_runtime_trace();
        const int overflow_this_tick = count_shadow_overflow_traces(records);
        total_overflow_traces += overflow_this_tick;
        consecutive_quiet_ticks =
            overflow_this_tick == 0 ? consecutive_quiet_ticks + 1 : 0;
        ++storm_ticks;
        if (session->myscreen_->world().end != 0)
            break;
    }

    EXPECT_GE(total_overflow_traces, 1)
        << "at least one shadow_inbound_overflow trace must fire across the storm";

    og::runtime::clear_local_transport_shadow(*session);
}
