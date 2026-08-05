#include <openglad/core/frame_pacing.h>
#include <openglad/core/frame_rate_config.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/base.h>
#include <openglad/interface/game_loop_state.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

short load_saved_game(const char* filename, screen* scr);

namespace {

og::runtime::GameSession* jitter_active_game_session()
{
    if (og::runtime::current_game_session != nullptr)
        return og::runtime::current_game_session;
    return dynamic_cast<og::runtime::GameSession*>(og::runtime::current_session);
}

// Wires a ClientOnly local-transport shadow without loading a real scenario.
// In ClientOnly mode local_transport_shadow_finish_tick skips the
// authoritative server step entirely, so 600 sim ticks can run on the
// gameplay screen with no possibility of an in-flight EndGame event ending
// the level mid-test (the jitter measurement is independent of gameplay).
struct InProcessShadowFixture
{
    std::shared_ptr<og::sim::InProcessTransport> server_transport;
    std::shared_ptr<og::sim::InProcessTransport> client_transport;
    og::sim::PeerId client_peer_id = 0u;
};

InProcessShadowFixture install_clientonly_shadow_fixture(
    og::runtime::GameSession& session,
    screen& gameplay_screen)
{
    InProcessShadowFixture fixture;
    fixture.server_transport = og::sim::InProcessTransport::create_server();
    fixture.server_transport->accept_connections();
    fixture.client_transport =
        fixture.server_transport->create_client_transport();
    fixture.client_peer_id = fixture.client_transport->local_peer_id();

    og::runtime::reset_network_client_transport_shadow(
        session,
        gameplay_screen,
        std::static_pointer_cast<og::sim::ITransport>(fixture.client_transport),
        fixture.client_peer_id,
        0u);
    return fixture;
}

struct TargetFpsGuard
{
    int old_fps;
    explicit TargetFpsGuard(int new_fps)
        : old_fps(og::runtime::current_session->target_fps_)
    {
        og::runtime::current_session->target_fps_ = new_fps;
    }
    ~TargetFpsGuard()
    {
        og::runtime::current_session->target_fps_ = old_fps;
    }
};

struct SpeedFactorGuard
{
    float old_speed;
    explicit SpeedFactorGuard(float new_speed)
        : old_speed(og::runtime::current_session->g_game_speed_factor_)
    {
        set_game_speed(new_speed);
    }
    ~SpeedFactorGuard()
    {
        set_game_speed(old_speed);
    }
};

void run_jitter_drive_at_fps(int fps)
{
    og::runtime::GameSession* const game_session = jitter_active_game_session();
    ASSERT_NE(nullptr, game_session);
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    SpeedFactorGuard speed(1.0f);
    TargetFpsGuard fps_guard(fps);

    // Empty the gameplay world so the 600-frame sim cannot trigger gameplay
    // events (e.g. an EndGame event from real combat) that would end the
    // run mid-test. The pacer/jitter measurement is independent of any
    // gameplay state.
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().timer_wait = og::sim::DEFAULT_TIMER_WAIT;

    const InProcessShadowFixture fixture =
        install_clientonly_shadow_fixture(*game_session, *game_screen);
    (void)fixture;
    ASSERT_TRUE(og::runtime::local_transport_active(*game_session));

    // Two-pacer model:
    // - sim_pacer fires at the master cadence derived from world.timer_wait
    //   (DEFAULT_TIMER_WAIT * TIMER_WAIT_TO_MS ≈ 82 ms), independent of fps.
    // - render_pacer fires at the configured target fps (~14 ms at 72 fps,
    //   ~17 ms at 60 fps).
    // The drive clock advances at the render cadence so the render pacer
    // observes a tight, deterministic 1-ms-jitter sequence, while sim ticks
    // fire every sim_interval/render_interval drive calls.
    const std::uint32_t render_interval =
        og::core::target_frame_interval_ms(fps);
    ASSERT_GE(render_interval, 1u);
    const std::uint32_t sim_interval =
        static_cast<std::uint32_t>(std::lround(
            og::sim::DEFAULT_TIMER_WAIT * og::sim::TIMER_WAIT_TO_MS));
    ASSERT_EQ(82u, sim_interval);

    og::runtime::set_runtime_trace_enabled(true);
    og::runtime::clear_runtime_trace();

    constexpr int kFrames = 600;
    std::uint32_t fake_now = 100000u;
    int tick_count = 0;
    int render_count = 0;
    std::vector<std::uint32_t> sleep_calls;
    std::vector<std::uint32_t> tick_observations;
    std::vector<std::uint32_t> render_observations;

    GameLoopFrameState st;
    // Pre-configure both pacers at fake_now so the loop's interval-mismatch
    // check is a no-op and no startup sleep is emitted.
    st.sim_pacer.configure(sim_interval, fake_now);
    st.render_pacer.configure(render_interval, fake_now);

    GameLoopDeps deps;
    deps.enable_render = true;
    deps.enable_event_poll = false;
    // Leave deps.fixed_tick_ms == 0 so compute_tick_schedule() reads from
    // world.timer_wait (master sim cadence) — proving target_fps drives
    // render only, not sim.
    deps.now_ms = [&fake_now]() { return fake_now; };
    deps.sleep_ms = [&sleep_calls](std::uint32_t ms) {
        sleep_calls.push_back(ms);
    };
    deps.after_act = [&tick_count, &tick_observations, &fake_now](screen&) {
        ++tick_count;
        tick_observations.push_back(fake_now);
    };
    deps.on_render = [&render_count, &render_observations, &fake_now](screen&) {
        ++render_count;
        render_observations.push_back(fake_now);
    };

    for (int i = 0; i < kFrames; ++i)
    {
        fake_now += render_interval;
        const GameFrameResult result =
            game_frame_with_result(*game_screen, st, deps);
        ASSERT_EQ(GameFrameResult::Continue, result)
            << "scenario should keep running across all jitter frames";
    }

    // Sim ticks fire roughly kFrames * render_interval / sim_interval times
    // (≈ 102 at 72 fps, ≈ 124 at 60 fps). Allow ±2 to absorb phase between
    // the two deadlines at startup.
    const int expected_sim_ticks = static_cast<int>(
        (static_cast<std::uint64_t>(kFrames) * render_interval) / sim_interval);
    EXPECT_NEAR(expected_sim_ticks, tick_count, 2)
        << "sim cadence must track world.timer_wait (master), not target_fps,"
           " at " << fps << " fps";

    // One render fires per call (drive clock advances by exactly
    // render_interval).
    EXPECT_NEAR(kFrames, render_count, 1)
        << "render cadence must track target_fps at " << fps << " fps";
    ASSERT_GE(render_observations.size(), 2u);
    ASSERT_GE(tick_observations.size(), 2u);

    // Sim observations are sampled at the drive (render) clock, so
    // inter-sim-tick deltas snap to multiples of render_interval. The
    // structural cadence guarantee is total-elapsed / total-ticks ≈
    // sim_interval — i.e. the long-run average matches master cadence —
    // not that every individual delta equals sim_interval.
    const std::uint64_t total_elapsed_ms =
        tick_observations.back() - tick_observations.front();
    const std::uint64_t observed_intervals =
        tick_observations.size() - 1;
    // Average inter-tick interval must be within one render-frame quantum
    // of sim_interval. A tighter bound would be unsound because the pacer
    // applies drift correction in interval-sized increments, so individual
    // deltas can be off by up to one render frame in either direction.
    const double mean_sim_delta_ms =
        static_cast<double>(total_elapsed_ms) /
        static_cast<double>(observed_intervals);
    EXPECT_NEAR(static_cast<double>(sim_interval), mean_sim_delta_ms,
                static_cast<double>(render_interval))
        << "average sim cadence must track master (~" << sim_interval
        << " ms / tick) at " << fps << " fps";

    std::vector<std::uint32_t> render_deltas;
    render_deltas.reserve(render_observations.size());
    for (std::size_t i = 1; i < render_observations.size(); ++i)
        render_deltas.push_back(
            render_observations[i] - render_observations[i - 1]);
    const std::uint32_t min_render_delta =
        *std::min_element(render_deltas.begin(), render_deltas.end());
    const std::uint32_t max_render_delta =
        *std::max_element(render_deltas.begin(), render_deltas.end());
    EXPECT_EQ(render_interval, min_render_delta)
        << "min observed inter-render delta should equal the configured "
           "render interval at "
        << fps << " fps";
    EXPECT_EQ(render_interval, max_render_delta)
        << "max observed inter-render delta should equal the configured "
           "render interval at "
        << fps << " fps";
    EXPECT_LE(max_render_delta - min_render_delta, 1u)
        << "render-pacer structural jitter must be bounded by integer "
           "rounding (<=1ms) at "
        << fps << " fps";

    // With both pacers pre-configured and the clock advancing exactly one
    // render quantum each call, every call has at least render_decision
    // firing — so the loop should never sleep nor emit a desktop_loop_sleep_ms
    // trace.
    EXPECT_TRUE(sleep_calls.empty())
        << "no sleeps expected when fake_now exactly tracks the render "
           "deadline";
    const auto traces = og::runtime::copy_runtime_trace();
    const int sleep_traces = static_cast<int>(std::count_if(
        traces.begin(), traces.end(),
        [](const og::runtime::RuntimeTraceRecord& r) {
            return r.category == "game_loop" &&
                r.event == "desktop_loop_sleep_ms";
        }));
    EXPECT_EQ(0, sleep_traces);

    og::runtime::clear_local_transport_shadow(*game_session);
    game_screen->world().delete_objects();
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

} // namespace

TEST(FramePacingJitter, deterministic_72_fps_render_with_master_sim_cadence)
{
    run_jitter_drive_at_fps(72);
}

TEST(FramePacingJitter, deterministic_60_fps_render_with_master_sim_cadence)
{
    run_jitter_drive_at_fps(60);
}

TEST(FramePacingJitter, shadow_finish_tick_bounded_by_message_budget_under_storm)
{
    og::runtime::GameSession* const session = jitter_active_game_session();
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
    for (int i = 0; i < kStormCount; ++i)
    {
        server_transport->send_snapshot(
            client_peer_id,
            std::make_shared<og::sim::WorldSnapshot>(
                make_storm_snapshot(static_cast<std::uint32_t>(i + 1))));
    }

    og::runtime::set_runtime_trace_enabled(true);
    og::runtime::clear_runtime_trace();

    // Drive a single shadow tick under an injected logical clock. The
    // deterministic bound on per-tick work is enforced by the
    // MAX_INBOUND_MESSAGES_PER_TICK budget on the inbound drain — wall-clock
    // duration is therefore proportional to the budget, not to the queue
    // length, regardless of CI runner speed. We measure the bound via the
    // GameClient's drained-this-call counter (which is what the budget caps),
    // making the assertion clock-independent.
    og::runtime::local_transport_shadow_finish_tick(*session);

    const og::sim::GameClient* const display_client =
        session->myscreen_->render_interpolation_client();
    ASSERT_NE(nullptr, display_client);

    const int drained = display_client->messages_drained_last_call();
    EXPECT_LE(drained, og::sim::MAX_INBOUND_MESSAGES_PER_TICK)
        << "shadow_finish_tick drain must be bounded by the per-tick message "
           "budget, not by the queue length";
    EXPECT_GT(drained, 0)
        << "the storm should have produced at least one drained message";

    const std::size_t pending = display_client->pending_inbound_message_count();
    EXPECT_GE(pending, static_cast<std::size_t>(
                           kStormCount - og::sim::MAX_INBOUND_MESSAGES_PER_TICK))
        << "un-drained queue must persist beyond the per-tick budget";
    EXPECT_LT(pending, static_cast<std::size_t>(kStormCount))
        << "at least one message must have been drained";

    const auto records = og::runtime::copy_runtime_trace();
    const int overflow_count = static_cast<int>(std::count_if(
        records.begin(), records.end(),
        [](const og::runtime::RuntimeTraceRecord& r) {
            return r.category == "local_transport_shadow" &&
                r.event == "shadow_inbound_overflow";
        }));
    EXPECT_EQ(1, overflow_count)
        << "the cap must fire exactly once on the first storm tick";

    og::runtime::clear_local_transport_shadow(*session);
}
