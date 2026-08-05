#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>

#include <openglad/core/frame_pacing.h>
#include <openglad/core/frame_rate_config.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>

std::string get_asset_path();

namespace {

void ensure_game_loop_wrapper_test_runtime()
{
    static bool initialized = false;
    if (initialized)
        return;

    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
        ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO))
            << "SDL video init should succeed for wrapper test";
    }

    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"))
        << "default campaign should mount for screen-backed unit session";
    ASSERT_TRUE(og::resources::mount((get_asset_path() + "pix/").c_str(),
                                     "pix/",
                                     1));
    ASSERT_TRUE(og::resources::mount((get_asset_path() + "sound/").c_str(),
                                     "sound/",
                                     1));
    ASSERT_TRUE(og::resources::mount((get_asset_path() + "cfg/").c_str(),
                                     "cfg/",
                                     1));
    initialized = true;
}

} // namespace

TEST(GameLoopWrapper, bool_wrapper_matches_typed_result)
{
    ensure_game_loop_wrapper_test_runtime();

    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = true;
    session_cfg.create_display = false;
    session_cfg.install_legacy_globals = false;
    session_cfg.allocate_prefs = true;

    og::runtime::GameSession session(session_cfg);
    ASSERT_NE(nullptr, session.screen_ptr());

    auto scope = session.activate();
    screen& s = *session.screen_ptr();
    s.world().end = 1;

    GameLoopFrameState typed_state;
    GameLoopFrameState wrapped_state;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;

    const GameFrameResult typed = game_frame_with_result(s, typed_state, deps);
    const bool wrapped = game_frame(s, wrapped_state, deps);

    EXPECT_EQ(static_cast<int>(typed != GameFrameResult::Continue),
              static_cast<int>(wrapped));
    EXPECT_TRUE(typed_state.done);
    EXPECT_TRUE(wrapped_state.done);
}

TEST(GameLoopWrapper, browser_wrapper_emits_one_browser_frame_step_per_call)
{
    ensure_game_loop_wrapper_test_runtime();

    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = true;
    session_cfg.create_display = false;
    session_cfg.install_legacy_globals = false;
    session_cfg.allocate_prefs = true;

    og::runtime::GameSession session(session_cfg);
    ASSERT_NE(nullptr, session.screen_ptr());

    auto scope = session.activate();
    screen& s = *session.screen_ptr();
    s.world().end = 1;

    og::runtime::set_runtime_trace_enabled(true);
    og::runtime::clear_runtime_trace();

    GameLoopFrameState st;
    GameLoopDeps render_deps;
    render_deps.enable_render = false;
    render_deps.enable_event_poll = false;

    GameLoopDeps tick_deps = render_deps;

    const og::core::BrowserFramePacingResult pacing =
        og::core::step_browser_frame_pacing(0u, 0u, 0u);
    ASSERT_TRUE(pacing.should_run_frame);

    run_browser_wrapper_frame(s, st, 0u, pacing, render_deps, tick_deps);
    run_browser_wrapper_frame(s, st, 0u, pacing, render_deps, tick_deps);

    const auto traces = og::runtime::drain_runtime_trace();
    const auto step_count = std::count_if(
        traces.begin(),
        traces.end(),
        [](const og::runtime::RuntimeTraceRecord& record) {
            return record.category == "browser_pacing" &&
                   record.event == "browser_frame_step";
        });
    EXPECT_EQ(2, step_count);

    og::runtime::set_runtime_trace_enabled(false);
}
