#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>

#include <openglad/core/frame_pacing.h>
#include <openglad/core/frame_rate_config.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "../test_save_state_guard.h"

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

// The screen-backed session needs a campaign mounted, and the mount is
// process-global: each test borrows it and hands it back (its caller holds
// an og::test::ScopedCampaignMountState), rather than the one-shot runtime
// setup above leaving it mounted for every later test in the binary.
void mount_game_loop_wrapper_campaign()
{
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"))
        << "default campaign should mount for screen-backed unit session";
}

// --- Remap / VIEW TEAM wait loops on production objects -------------------
//
// og_unit_sim links the non-TESTING game objects, so the blocking waits in
// input.cpp (assignKeyFromWaitEventPolling) and view.cpp (view_team) run
// their real loops here. Every scripted poll below carries a hard pass
// budget: it answers false once the budget is spent, so a wait whose event
// never arrives ends instead of hanging. SDL_EVENT_QUIT is never pushed
// (quit_if_quit_event calls the real quit() in these objects).

// A session with a screen, the shape the wrapper tests below use; by default
// without a display window. SDL_Init(VIDEO) brings up the events subsystem
// too.
std::unique_ptr<og::runtime::GameSession> make_wait_loop_session(
    bool create_display = false)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = true;
    session_cfg.create_display = create_display;
    session_cfg.install_legacy_globals = false;
    session_cfg.allocate_prefs = true;
    return std::make_unique<og::runtime::GameSession>(session_cfg);
}

void push_sdl_key(Uint32 type, SDL_Keycode key)
{
    SDL_Event event{};
    event.type = type;
    event.key.down = (type == SDL_EVENT_KEY_DOWN);
    event.key.key = key;
    event.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    ASSERT_TRUE(SDL_PushEvent(&event)) << SDL_GetError();
}

int count_queued_key_downs(SDL_Keycode key)
{
    int seen = 0;
    while (const void* native = og::input_native::poll_event())
    {
        const SDL_Event& event = *static_cast<const SDL_Event*>(native);
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == key)
            ++seen;
    }
    return seen;
}

// The remap binds into the player's active control-mode keymap; read it
// back through the same public accessor the options menu uses.
int bound_key(int player, int key_enum)
{
    return get_player_key_binding_for_mode(
        player, get_player_control_mode(player), key_enum);
}

// Restores one binding on scope exit so a remap test never leaks into the
// next test's controls.
struct ScopedKeyBinding
{
    ScopedKeyBinding(int player_index, int key)
        : player(player_index), key_enum(key), saved(bound_key(player_index, key))
    {
    }
    ~ScopedKeyBinding() { set_player_key_binding(player, key_enum, saved); }
    int player;
    int key_enum;
    int saved;
};

// Scripted remap owner. On call `queue_at` (the first by default) it queues
// the events the wait loop will see (in this order: a key release, a key
// press, a second press behind it); on call `cancel_at` it answers false,
// and it never answers true more than `budget` times.
struct RemapPollScript
{
    int calls = 0;
    int budget = 0;
    int cancel_at = 0; // 1-based call that answers false; 0 = never
    int queue_at = 1;  // 1-based call that queues the scripted events
    SDL_Keycode first_release = SDLK_UNKNOWN;
    SDL_Keycode first_press = SDLK_UNKNOWN;
    SDL_Keycode second_press = SDLK_UNKNOWN;
};
RemapPollScript g_remap_poll;

bool scripted_remap_poll()
{
    RemapPollScript& s = g_remap_poll;
    ++s.calls;
    if (s.calls == s.queue_at)
    {
        if (s.first_release != SDLK_UNKNOWN)
            push_sdl_key(SDL_EVENT_KEY_UP, s.first_release);
        if (s.first_press != SDLK_UNKNOWN)
            push_sdl_key(SDL_EVENT_KEY_DOWN, s.first_press);
        if (s.second_press != SDLK_UNKNOWN)
            push_sdl_key(SDL_EVENT_KEY_DOWN, s.second_press);
    }
    if (s.calls == s.cancel_at)
        return false;
    return s.calls <= s.budget;
}

int g_view_team_poll_calls = 0;
int g_view_team_poll_true_passes = 0;

bool counting_view_team_poll()
{
    ++g_view_team_poll_calls;
    // Hard stop regardless of the loop under test: never more than one
    // false-less stretch of `true_passes` answers.
    return g_view_team_poll_calls <= g_view_team_poll_true_passes;
}

} // namespace

// FIXTURE GATE (first in the file): everything below relies on SDL handing
// an SDL_PushEvent'ed key press back through og::input_native::poll_event()
// in this display-less session. If this fails, the remap tests would spin
// on their pass budgets and the owner-less remap below would never return.
TEST(RemapWait, pushed_key_press_reaches_poll_event_exactly_once)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();

    clear_events();
    push_sdl_key(SDL_EVENT_KEY_DOWN, SDLK_J);
    EXPECT_EQ(1, count_queued_key_downs(SDLK_J))
        << "the pushed press must come back through poll_event";
    EXPECT_EQ(0, count_queued_key_downs(SDLK_J))
        << "and only once: the queue is empty after it was read";
}

// Remap wait with a lobby poll owner: key releases are not answers, the
// first key press is, and the accepted key is bound before a 400 ms debounce
// that keeps polling the owner every 10 ms (one wait pass + 40 debounce
// passes).
TEST(RemapWait, skips_key_release_and_binds_first_key_press)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();
    ScopedKeyBinding restore(0, KEY_SPECIAL);
    ASSERT_NE(static_cast<int>(SDLK_J), restore.saved);

    clear_events();
    g_remap_poll = RemapPollScript{};
    g_remap_poll.budget = 100;
    g_remap_poll.first_release = SDLK_K;
    g_remap_poll.first_press = SDLK_J;
    EXPECT_TRUE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, &scripted_remap_poll))
        << "a key press must end the remap wait";
    EXPECT_EQ(static_cast<int>(SDLK_J), bound_key(0, KEY_SPECIAL))
        << "the pressed key (not the released one) is bound";
    EXPECT_EQ(41, g_remap_poll.calls)
        << "one wait pass, then 40 debounce passes of 10 ms";
}

// Escape answers the remap wait but keeps the existing binding (paired with
// the J press above, which rebinds).
TEST(RemapWait, escape_press_keeps_existing_binding)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();
    ScopedKeyBinding restore(0, KEY_SPECIAL);

    clear_events();
    g_remap_poll = RemapPollScript{};
    g_remap_poll.budget = 100;
    g_remap_poll.first_press = SDLK_ESCAPE;
    EXPECT_TRUE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, &scripted_remap_poll))
        << "Escape still answers the wait";
    EXPECT_EQ(restore.saved, bound_key(0, KEY_SPECIAL))
        << "Escape must not become the binding";
    EXPECT_EQ(41, g_remap_poll.calls);
}

// A joystick button press answers the remap too and binds through the
// seat's joystick layout (JoyData), leaving the keyboard map alone. The
// device is an SDL virtual joystick: SDL's own device and event path.
TEST(RemapWait, joystick_button_press_binds_through_joy_layout)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();
    ScopedKeyBinding restore(0, KEY_SPECIAL);

    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_JOYSTICK)) << SDL_GetError();
    struct JoystickFixture
    {
        JoyData saved_layout = player_joy[0];
        SDL_JoystickID id = 0;
        SDL_Joystick* pad = nullptr;
        ~JoystickFixture()
        {
            if (pad != nullptr)
                SDL_CloseJoystick(pad);
            if (id != 0)
                SDL_DetachVirtualJoystick(id);
            player_joy[0] = saved_layout;
            clear_events();
            SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        }
    } joy;

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = 4;
    desc.name = "remap virtual pad";
    joy.id = SDL_AttachVirtualJoystick(&desc);
    ASSERT_NE(0u, joy.id) << SDL_GetError();
    joy.pad = SDL_OpenJoystick(joy.id);
    ASSERT_NE(nullptr, joy.pad) << SDL_GetError();
    ASSERT_NE(JoyData::BUTTON, player_joy[0].key_type[KEY_SPECIAL]);

    clear_events(); // the device-added notice is not part of the answer
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joy.pad, 2, true)) << SDL_GetError();
    SDL_UpdateJoysticks(); // posts the button-down event

    g_remap_poll = RemapPollScript{};
    g_remap_poll.budget = 100;
    EXPECT_TRUE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, &scripted_remap_poll))
        << "a joystick button press must end the remap wait";
    EXPECT_EQ(JoyData::BUTTON, player_joy[0].key_type[KEY_SPECIAL]);
    EXPECT_EQ(2, player_joy[0].key_index[KEY_SPECIAL]);
    EXPECT_EQ(restore.saved, bound_key(0, KEY_SPECIAL))
        << "a joystick answer leaves the keyboard binding alone";
}

// An empty poll pass is not an answer: the wait sleeps and polls the owner
// again. The owner queues nothing on its first call and the J press on its
// second, so the remap binds J after exactly two wait passes plus the 40
// debounce passes.
TEST(RemapWait, empty_poll_pass_waits_and_polls_again)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();
    ScopedKeyBinding restore(0, KEY_SPECIAL);
    ASSERT_NE(static_cast<int>(SDLK_J), restore.saved);

    clear_events();
    g_remap_poll = RemapPollScript{};
    g_remap_poll.budget = 100;
    g_remap_poll.queue_at = 2;
    g_remap_poll.first_press = SDLK_J;
    EXPECT_TRUE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, &scripted_remap_poll))
        << "the press queued on the second pass must end the remap wait";
    EXPECT_EQ(static_cast<int>(SDLK_J), bound_key(0, KEY_SPECIAL));
    EXPECT_EQ(42, g_remap_poll.calls)
        << "two wait passes (the first found nothing), then 40 debounce passes";
}

// A stick nudged inside the dead zone (JOY_DEAD_ZONE = 8000, input.cpp) is
// no answer: the button press queued behind it binds. Control: the same
// axis pushed past the dead zone answers the remap as a positive axis.
TEST(RemapWait, joystick_axis_inside_dead_zone_is_ignored)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();
    ScopedKeyBinding restore(0, KEY_SPECIAL);

    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_JOYSTICK)) << SDL_GetError();
    struct JoystickFixture
    {
        JoyData saved_layout = player_joy[0];
        SDL_JoystickID id = 0;
        SDL_Joystick* pad = nullptr;
        ~JoystickFixture()
        {
            if (pad != nullptr)
                SDL_CloseJoystick(pad);
            if (id != 0)
                SDL_DetachVirtualJoystick(id);
            player_joy[0] = saved_layout;
            clear_events();
            SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
        }
    } joy;

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 1;
    desc.nbuttons = 4;
    desc.name = "remap dead-zone pad";
    joy.id = SDL_AttachVirtualJoystick(&desc);
    ASSERT_NE(0u, joy.id) << SDL_GetError();
    joy.pad = SDL_OpenJoystick(joy.id);
    ASSERT_NE(nullptr, joy.pad) << SDL_GetError();
    ASSERT_NE(JoyData::BUTTON, player_joy[0].key_type[KEY_SPECIAL]);

    clear_events(); // the device-added notice is not part of the answer
    ASSERT_TRUE(SDL_SetJoystickVirtualAxis(joy.pad, 0, 4000)) << SDL_GetError();
    SDL_UpdateJoysticks(); // posts the in-dead-zone axis motion
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joy.pad, 2, true)) << SDL_GetError();
    SDL_UpdateJoysticks(); // posts the button-down behind it

    g_remap_poll = RemapPollScript{};
    g_remap_poll.budget = 100;
    EXPECT_TRUE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, &scripted_remap_poll));
    EXPECT_EQ(JoyData::BUTTON, player_joy[0].key_type[KEY_SPECIAL])
        << "the in-dead-zone axis motion must not answer the remap";
    EXPECT_EQ(2, player_joy[0].key_index[KEY_SPECIAL]);

    // Control: release the button, then push the axis past the dead zone.
    ASSERT_TRUE(SDL_SetJoystickVirtualButton(joy.pad, 2, false)) << SDL_GetError();
    SDL_UpdateJoysticks();
    clear_events();
    player_joy[0] = joy.saved_layout;
    ASSERT_TRUE(SDL_SetJoystickVirtualAxis(joy.pad, 0, 9000)) << SDL_GetError();
    SDL_UpdateJoysticks();

    g_remap_poll = RemapPollScript{};
    g_remap_poll.budget = 100;
    EXPECT_TRUE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, &scripted_remap_poll));
    EXPECT_EQ(JoyData::POS_AXIS, player_joy[0].key_type[KEY_SPECIAL])
        << "an axis past the dead zone answers the remap";
    EXPECT_EQ(0, player_joy[0].key_index[KEY_SPECIAL]);
}

// The owner's poll saying stop before any press abandons the remap: nothing
// is bound and the press queued in that same frame is drained, not leaked
// into the next screen.
TEST(RemapWait, owner_cancel_before_a_press_abandons_and_drains)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();
    ScopedKeyBinding restore(0, KEY_SPECIAL);
    ASSERT_NE(static_cast<int>(SDLK_J), restore.saved);

    clear_events();
    g_remap_poll = RemapPollScript{};
    g_remap_poll.budget = 0;
    g_remap_poll.cancel_at = 1;
    g_remap_poll.first_press = SDLK_J;
    EXPECT_FALSE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, &scripted_remap_poll))
        << "an owner cancel ends the remap unanswered";
    EXPECT_EQ(restore.saved, bound_key(0, KEY_SPECIAL))
        << "the press queued with the cancel must not be bound";
    EXPECT_EQ(1, g_remap_poll.calls);
    EXPECT_EQ(0, count_queued_key_downs(SDLK_J))
        << "the queued press is drained";
}

// An owner cancel during the debounce abandons the remap (false) and drains
// the input still queued behind the accepted key.
TEST(RemapWait, owner_cancel_during_debounce_abandons_and_drains)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();
    ScopedKeyBinding restore(0, KEY_SPECIAL);

    clear_events();
    g_remap_poll = RemapPollScript{};
    g_remap_poll.budget = 100;
    g_remap_poll.first_press = SDLK_J;
    g_remap_poll.second_press = SDLK_K; // queued behind the accepted J
    g_remap_poll.cancel_at = 2;         // the first debounce pass
    EXPECT_FALSE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, &scripted_remap_poll))
        << "a cancel during the debounce reports the remap abandoned";
    EXPECT_EQ(2, g_remap_poll.calls) << "the debounce stops at the first false";
    EXPECT_EQ(0, count_queued_key_downs(SDLK_K))
        << "the press queued behind the accepted key is drained";
}

// A display-owning session SDL_Quit()s on teardown; bring video (and the
// event queue) back for the tests that run after it in this binary.
namespace {
struct RestoreSdlVideoAfterDisplay
{
    ~RestoreSdlVideoAfterDisplay()
    {
        if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
        {
            EXPECT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
        }
    }
};
} // namespace

// VIEW TEAM (pause menu) waits for Esc and polls its owner once per pass;
// the first false ends the wait. It draws the roster into the display
// before waiting, so this session owns a (dummy-driver) display.
TEST(ViewTeamWait, polls_owner_once_per_pass_and_ends_on_first_false)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    RestoreSdlVideoAfterDisplay restore_video;
    auto session = make_wait_loop_session(/*create_display=*/true);
    auto scope = session->activate();
    screen& s = *session->screen_ptr();
    ASSERT_NE(nullptr, s.viewob[0]);
    ASSERT_NE(nullptr, og::runtime::current_session->keystates_);
    ASSERT_FALSE(og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
        << "Esc held would skip the wait";

    clear_events();
    g_view_team_poll_calls = 0;
    g_view_team_poll_true_passes = 3;
    s.viewob[0]->view_team(&counting_view_team_poll);
    EXPECT_EQ(4, g_view_team_poll_calls)
        << "three true passes, then the first false ends the wait";
}

TEST(GameLoopWrapper, bool_wrapper_matches_typed_result)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();

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
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();

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

// LAST in the file: the remap wait without a poll owner has no exit but a
// delivered event (the gate test above proves delivery), so the press is
// queued before the wait starts. It binds the key and then debounces for a
// flat 400 ms, the product's own timer.
TEST(RemapWait, without_owner_binds_a_queued_press)
{
    og::test::ScopedCampaignMountState mount_restore;
    ensure_game_loop_wrapper_test_runtime();
    mount_game_loop_wrapper_campaign();
    auto session = make_wait_loop_session();
    auto scope = session->activate();
    ScopedKeyBinding restore(0, KEY_SPECIAL);
    ASSERT_NE(static_cast<int>(SDLK_J), restore.saved);

    clear_events();
    push_sdl_key(SDL_EVENT_KEY_DOWN, SDLK_J);
    EXPECT_TRUE(assignKeyFromWaitEventPolling(0, KEY_SPECIAL, nullptr));
    EXPECT_EQ(static_cast<int>(SDLK_J), bound_key(0, KEY_SPECIAL));
}
