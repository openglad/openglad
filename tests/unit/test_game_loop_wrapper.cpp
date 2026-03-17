#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>

#include <openglad/interface/screen.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>

#include <SDL.h>
#include <gtest/gtest.h>

std::string get_asset_path();

namespace {

void ensure_game_loop_wrapper_test_runtime()
{
    static bool initialized = false;
    if (initialized)
        return;

    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);

    if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0) {
        ASSERT_EQ(0, SDL_Init(SDL_INIT_VIDEO))
            << "SDL video init should succeed for wrapper test";
    }

    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"))
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
