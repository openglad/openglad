#include <memory>
#include <array>
#include <openglad/resources/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/interface/ui/picker_common.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
Sint32 edit_player_keymap(Sint32 arg);
Sint32 toggle_player_control_mode(Sint32 arg);

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

static bool click_until_interactable(const std::string& click_id, const std::string& next_id, int timeout_ms)
{
    const Uint32 deadline = SDL_GetTicks() + static_cast<Uint32>(timeout_ms);
    while (SDL_GetTicks() < deadline) {
        if (has_interactable(next_id))
            return true;
        if (has_interactable(click_id)) {
            interact(click_id);
            SDL_Delay(250);
            continue;
        }
        SDL_Delay(50);
    }
    return has_interactable(next_id);
}

// Test: Open options menu, toggle some settings, then exit.
//
// Flow: Main Menu -> Options -> toggle some settings -> Back -> (main menu exits)
//
// Verifies:
//   1. Options menu opens
//   2. Can toggle visual effects
//   3. Returns cleanly to main menu

// The three FX subscreens: main-options opener id, unique BACK id, and every
// toggle each screen hosts with its cfg (category, key) pair. A cycle entry
// (the depth selector) stores a value string instead of an on/off flag and
// gets the five-step lap check instead of the flip check.
struct FxToggleSpec {
    const char* button_id;
    const char* category;
    const char* key;
    bool cycle = false;
};

struct FxScreenSpec {
    const char* opener_id;
    const char* back_id;
    const FxToggleSpec* toggles;
    int toggle_count;
};

static const FxToggleSpec kGameplayFxToggles[] = {
    {"toggle_hit_recoil", "effects", "hit_recoil"},
    {"toggle_attack_lunge", "effects", "attack_lunge"},
};

static const FxToggleSpec kUiFxToggles[] = {
    {"toggle_mini_hp_bar", "effects", "mini_hp_bar"},
    {"toggle_damage_numbers", "effects", "damage_numbers"},
    {"toggle_heal_numbers", "effects", "heal_numbers"},
};

static const FxToggleSpec kGraphicsFxToggles[] = {
    {"toggle_hit_flash", "effects", "hit_flash"},
    {"toggle_hit_sparks", "effects", "hit_anim"},
    {"toggle_gore", "effects", "gore"},
    {"toggle_shadows", "effects", "shadows"},
    {"toggle_reflections", "effects", "reflections"},
    {"toggle_weather", "effects", "weather"},
    {"toggle_dust", "effects", "dust"},
    {"depth_fx", "effects", "depth_fx", true},
    {"toggle_trails", "effects", "trails"},
    {"toggle_fire_glow", "effects", "fire_glow"},
    {"toggle_ripples", "effects", "ripples"},
    {"toggle_screen_shake", "effects", "screen_shake"},
};

inline constexpr int kFxScreenCount = 3;
inline constexpr int kFxMaxToggles = 12;

static const FxScreenSpec kFxScreens[kFxScreenCount] = {
    {"gameplay_fx", "gameplay_fx_back", kGameplayFxToggles, 2},
    {"ui_fx", "ui_fx_back", kUiFxToggles, 3},
    {"graphics_fx", "graphics_fx_back", kGraphicsFxToggles, 12},
};

struct OptionsState {
    bool started;
    bool finished;
    bool saw_options;
    bool entered_controls;
    bool exited_controls;
    bool entered_fx[kFxScreenCount];
    bool exited_fx[kFxScreenCount];
    bool toggled_fx[kFxScreenCount][kFxMaxToggles];
    bool used_options_back;
    // Label the RENDERING ENGINE button shows while cfg (graphics, render) is
    // the empty string (as in any process that never ran load_settings()).
    std::string render_label_when_unset;
};

// Click an FX-subscreen toggle and report whether its cfg key flipped.
// Under machine load a single click can be dropped (the press is still held
// when the handler samples the mouse), so poll for the flip and re-click
// until a deadline. Net flip parity doesn't matter: the flow ends with
// RESTORE DEFAULTS, which reloads cfg from disk and undoes every toggle.
static bool toggle_effect_and_check_flip(const char* button_id, const char* category,
                                         const char* cfg_key)
{
    const bool before = cfg.is_on(category, cfg_key);
    const Uint32 deadline = SDL_GetTicks() + 5000;
    interact(button_id);
    for (;;) {
        // 300ms per the menu-test discipline: a shorter gap can land the next
        // click while this one's press is still held, and it gets dropped.
        SDL_Delay(300);
        if (cfg.is_on(category, cfg_key) != before)
            return true;
        if (SDL_GetTicks() >= deadline)
            return false;
        interact(button_id);
    }
}

// One verified step of a cycle button: click and poll (re-clicking on the
// dropped-click pattern above) until the stored value string moves.
static bool click_cycle_step(const char* button_id, const char* category,
                             const char* cfg_key)
{
    const std::string before = cfg.get_setting(category, cfg_key);
    const Uint32 deadline = SDL_GetTicks() + 5000;
    interact(button_id);
    for (;;) {
        SDL_Delay(300);
        if (cfg.get_setting(category, cfg_key) != before)
            return true;
        if (SDL_GetTicks() >= deadline)
            return false;
        interact(button_id);
    }
}

// The depth selector is a five-way cycle: every verified click must land on
// a new effects/depth_fx value, and a full lap of five restores the setting
// (an out-of-set start normalizes to the default, so compare against five
// pure cycle_depth_fx steps rather than the raw start). A re-click racing a
// slow handler can double-step; finish the lap if so — deadline-bounded.
static bool cycle_effect_and_check_lap(const char* button_id, const char* category,
                                       const char* cfg_key)
{
    std::string expected = cfg.get_setting(category, cfg_key);
    for (int i = 0; i < 5; ++i)
        expected = og::ui::cycle_depth_fx(expected);

    for (int i = 0; i < 5; ++i)
        if (!click_cycle_step(button_id, category, cfg_key))
            return false;

    const Uint32 deadline = SDL_GetTicks() + 5000;
    while (cfg.get_setting(category, cfg_key) != expected) {
        if (SDL_GetTicks() >= deadline)
            return false;
        interact(button_id);
        SDL_Delay(300);
    }
    return true;
}

static int options_injector(void* data)
{
    og::runtime::ensure_thread_session();
    OptionsState* state = static_cast<OptionsState*>(data);
    state->started = true;

    // Wait for main menu
    if (!wait_for_interactable("options", 5000)) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking options\n");
    bool in_options = click_until_interactable("options", "gameplay_fx", 10000);

    // Options menu buttons
    SDL_Delay(150);
    if (in_options || wait_for_interactable("gameplay_fx", 10000)) {
        state->saw_options = true;
        SDL_Delay(150);

        // With cfg (graphics, render) empty, the per-frame label sync must
        // fall back to "normal" instead of drawing a blank button face.
        fprintf(stderr, "  [test] checking rendering-engine label fallback\n");
        {
            const std::string prev_render = cfg.get_setting("graphics", "render");
            cfg.apply_setting("graphics", "render", "");
            SDL_Delay(300);  // let main_options() run a few label-sync frames
            for (const Interactable& item : get_interactables()) {
                if (item.id == "toggle_rendering") {
                    state->render_label_when_unset = item.label;
                    break;
                }
            }
            cfg.apply_setting("graphics", "render",
                              prev_render.empty() ? "normal" : prev_render);
            SDL_Delay(150);
        }

        fprintf(stderr, "  [test] entering player controls\n");
        bool in_controls = click_until_interactable("player_controls", "player1_mode", 5000);
        SDL_Delay(150);
        if (in_controls || wait_for_interactable("player1_mode", 5000)) {
            state->entered_controls = true;
            interact("player1_mode");
            if (wait_for_interactable("controls_back", 5000)) {
                SDL_Delay(200);
                state->exited_controls = click_until_interactable("controls_back", "gameplay_fx", 5000);
            }
            wait_for_interactable("gameplay_fx", 10000);
            SDL_Delay(150);
        }

        fprintf(stderr, "  [test] toggling sound/render/fullscreen\n");
        interact("toggle_sound");
        SDL_Delay(80);
        interact("toggle_rendering");
        SDL_Delay(80);
        interact("toggle_fullscreen");
        SDL_Delay(80);

        fprintf(stderr, "  [test] adjusting overscan\n");
        interact("overscan_plus");
        SDL_Delay(80);
        interact("overscan_minus");
        SDL_Delay(80);

        // Enter each FX subscreen in turn, flip every toggle it hosts
        // (asserting the cfg key actually changed), and leave via the
        // screen's unique BACK id.
        for (int s = 0; s < kFxScreenCount; ++s) {
            const FxScreenSpec& screen = kFxScreens[s];
            fprintf(stderr, "  [test] entering %s subscreen\n", screen.opener_id);
            bool in_screen = click_until_interactable(
                screen.opener_id, screen.toggles[0].button_id, 5000);
            SDL_Delay(750);
            if (!in_screen &&
                !wait_for_interactable(screen.toggles[0].button_id, 5000)) {
                continue;
            }
            state->entered_fx[s] = true;
            SDL_Delay(300);

            fprintf(stderr, "  [test] toggling %s effects\n", screen.opener_id);
            for (int t = 0; t < screen.toggle_count; ++t) {
                const FxToggleSpec& toggle = screen.toggles[t];
                state->toggled_fx[s][t] = toggle.cycle
                    ? cycle_effect_and_check_lap(
                          toggle.button_id, toggle.category, toggle.key)
                    : toggle_effect_and_check_flip(
                          toggle.button_id, toggle.category, toggle.key);
            }

            fprintf(stderr, "  [test] leaving %s subscreen\n", screen.opener_id);
            state->exited_fx[s] =
                click_until_interactable(screen.back_id, screen.opener_id, 5000);
            wait_for_interactable(screen.opener_id, 10000);
            SDL_Delay(300);
        }

        // Restore defaults last: it reloads cfg from disk, undoing every
        // toggle above (including the FX subscreen flips).
        fprintf(stderr, "  [test] restoring defaults\n");
        interact("restore_defaults");
        SDL_Delay(80);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking options_back\n");
        if (wait_for_interactable("options_back", 5000)) {
            interact("options_back");
            state->used_options_back = true;
            SDL_Delay(500);
        }
    }

    // Ensure mainmenu() returns so picker_main() can complete. Coverage builds
    // can redraw the main menu slowly after leaving options, so use Escape to
    // unwind whichever menu is currently active.
    const Uint32 quit_deadline = SDL_GetTicks() + 3000;
    while (SDL_GetTicks() < quit_deadline) {
        if (has_interactable("quit")) {
            SDL_Delay(80);
            fprintf(stderr, "  [test] clicking quit\n");
            interact("quit");
            break;
        }

        inject_key_press(SDLK_ESCAPE, 20);
        SDL_Delay(150);
    }

    state->finished = true;
    return 0;
}

TEST(OptionsMenu, options_menu) {
    trace_clear();

    // Need save data for continue_game
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    OptionsState state = {};
    SDL_Thread* thread = SDL_CreateThread(options_injector, "options_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;  // options REDRAW is handled within same mainmenu() call

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.started) << "injector thread should have started";
    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_options) << "should have entered the options menu";
    ASSERT_TRUE(state.entered_controls) << "should have entered controls submenu";
    ASSERT_TRUE(state.exited_controls) << "should have exited via controls_back";
    for (int s = 0; s < kFxScreenCount; ++s) {
        const FxScreenSpec& screen = kFxScreens[s];
        ASSERT_TRUE(state.entered_fx[s])
            << "should have entered the " << screen.opener_id << " subscreen";
        for (int t = 0; t < screen.toggle_count; ++t) {
            const FxToggleSpec& toggle = screen.toggles[t];
            EXPECT_TRUE(state.toggled_fx[s][t])
                << toggle.button_id << " should flip " << toggle.category
                << "/" << toggle.key;
        }
        ASSERT_TRUE(state.exited_fx[s])
            << "should have returned to main options via " << screen.back_id;
    }
    ASSERT_TRUE(state.used_options_back) << "should have exited options via options_back";
    EXPECT_EQ("normal", state.render_label_when_unset)
        << "empty cfg (graphics, render) must fall back to 'normal' on the button face";
}


TEST(OptionsMenu, edit_player_keymap_exercises_four_and_eight_direction_prompts)
{
    constexpr Sint32 kMenuRedraw = 2;
    reset_default_player_controls();
    const int original_fire = og::runtime::current_session->player_keys_[0][KEY_FIRE];

    ASSERT_EQ(kMenuRedraw, edit_player_keymap(0))
        << "four-direction remap should complete without changing ESC-kept keys";
    ASSERT_EQ(original_fire, og::runtime::current_session->player_keys_[0][KEY_FIRE])
        << "fake ESC key should preserve the existing binding";

    ASSERT_EQ(kMenuRedraw, toggle_player_control_mode(0));
    ASSERT_EQ(kMenuRedraw, edit_player_keymap(0))
        << "eight-direction remap should also complete under TESTING";

    ASSERT_EQ(kMenuRedraw, edit_player_keymap(-1))
        << "invalid player index should be ignored safely";
    ASSERT_EQ(kMenuRedraw, edit_player_keymap(4))
        << "out-of-range player index should be ignored safely";
}

// ---------------------------------------------------------------------------
// FX-capture: menu tours for the visual review site (scripts/fx_review).
// Skipped unless OG_FX_CAPTURE_DIR is set. Frames are produced by the
// TESTING dump hook in screen::buffer_to_screen (OG_DUMP_DIR, every 3rd
// present); keyboard-highlight steps are driven through the TESTING nav
// hook in handle_menu_nav (g_test_menu_nav_key) because real key events
// can't be injected from another thread mid-press.
// Run standalone with OG_FX_CAPTURE_DIR=<dir>:
//   ./build/ci-test/og_test_menu_ui --gtest_filter='OptionsMenu.zz_capture_*'
// ---------------------------------------------------------------------------
#include <cstdlib>
#include <filesystem>

extern int g_test_menu_nav_key;

namespace menu_capture {

struct CaptureState {
    bool started;
    bool finished;
    bool saw_options;
    bool entered_controls;
    bool exited_controls;
    bool toured_fx;
    bool used_options_back;
    bool clicked_quit;
};

// Click a toggle, dwell so the color flip is on-camera for several frames.
void capture_toggle(const char* button_id)
{
    interact(button_id);
    SDL_Delay(380);
}

void capture_quit_main_menu(CaptureState* state)
{
    const Uint32 quit_deadline = SDL_GetTicks() + 5000;
    while (SDL_GetTicks() < quit_deadline) {
        if (has_interactable("quit")) {
            SDL_Delay(600); // dwell on the main menu before leaving
            interact("quit");
            state->clicked_quit = true;
            break;
        }
        inject_key_press(SDLK_ESCAPE, 20);
        SDL_Delay(150);
    }
}

// menu_tour: main menu (keyboard-nav highlight walk) -> SETTINGS -> CONTROLS
// (flip P1 mode twice: 4-DIRECTION <-> 8-DIRECTION) -> back -> back -> quit.
int menu_tour_injector(void* data)
{
    og::runtime::ensure_thread_session();
    CaptureState* state = static_cast<CaptureState*>(data);
    state->started = true;

    if (!wait_for_interactable("options", 10000)) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(1000);

    // Keyboard-nav highlight walk on the main menu.
    for (int i = 0; i < 3; i++) {
        g_test_menu_nav_key = KEY_DOWN;
        SDL_Delay(480);
    }
    g_test_menu_nav_key = KEY_UP;
    SDL_Delay(480);

    bool in_options = click_until_interactable("options", "player_controls", 10000);
    SDL_Delay(900);
    if (in_options || wait_for_interactable("player_controls", 10000)) {
        state->saw_options = true;

        // Two nav steps inside SETTINGS so the highlight is seen moving.
        g_test_menu_nav_key = KEY_DOWN;
        SDL_Delay(480);
        g_test_menu_nav_key = KEY_RIGHT;
        SDL_Delay(480);

        bool in_controls = click_until_interactable("player_controls", "player1_mode", 5000);
        SDL_Delay(900);
        if (in_controls || wait_for_interactable("player1_mode", 5000)) {
            state->entered_controls = true;
            g_test_menu_nav_key = KEY_DOWN;
            SDL_Delay(480);
            capture_toggle("player1_mode"); // 4-DIRECTION -> 8-DIRECTION
            capture_toggle("player1_mode"); // flip back: settings unchanged
            state->exited_controls =
                click_until_interactable("controls_back", "player_controls", 5000);
            wait_for_interactable("player_controls", 10000);
            SDL_Delay(700);
        }

        if (wait_for_interactable("options_back", 5000)) {
            SDL_Delay(300);
            interact("options_back");
            state->used_options_back = true;
            SDL_Delay(700);
        }
    }

    capture_quit_main_menu(state);
    state->finished = true;
    return 0;
}

// menu_effects: SETTINGS -> GAMEPLAY FX -> UI FX -> GRAPHICS FX, flipping
// each showcased toggle TWICE (visible red/green flip, settings unchanged),
// then back out and quit.
int menu_effects_injector(void* data)
{
    og::runtime::ensure_thread_session();
    CaptureState* state = static_cast<CaptureState*>(data);
    state->started = true;

    if (!wait_for_interactable("options", 10000)) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(1000);

    bool in_options = click_until_interactable("options", "gameplay_fx", 10000);
    SDL_Delay(900);
    if (in_options || wait_for_interactable("gameplay_fx", 10000)) {
        state->saw_options = true;
        bool all_screens = true;

        struct ScreenPlan {
            const char* opener;
            const char* back;
            const char* toggles[4];
            int toggle_count;
        };
        static const ScreenPlan kPlan[3] = {
            {"gameplay_fx", "gameplay_fx_back",
                {"toggle_hit_recoil", "toggle_attack_lunge"}, 2},
            {"ui_fx", "ui_fx_back",
                {"toggle_mini_hp_bar", "toggle_damage_numbers", "toggle_heal_numbers"}, 3},
            {"graphics_fx", "graphics_fx_back",
                {"toggle_weather", "toggle_shadows", "toggle_fire_glow", "toggle_screen_shake"}, 4},
        };

        for (const ScreenPlan& plan : kPlan) {
            bool in_screen = click_until_interactable(plan.opener, plan.toggles[0], 5000);
            SDL_Delay(750);
            if (!in_screen && !wait_for_interactable(plan.toggles[0], 5000)) {
                all_screens = false;
                continue;
            }
            SDL_Delay(300);
            for (int t = 0; t < plan.toggle_count; ++t) {
                capture_toggle(plan.toggles[t]); // flip (color changes)
                capture_toggle(plan.toggles[t]); // flip back (restored)
            }
            if (!click_until_interactable(plan.back, plan.opener, 5000))
                all_screens = false;
            wait_for_interactable(plan.opener, 10000);
            SDL_Delay(400);
        }
        state->toured_fx = all_screens;

        if (wait_for_interactable("options_back", 5000)) {
            SDL_Delay(300);
            interact("options_back");
            state->used_options_back = true;
            SDL_Delay(700);
        }
    }

    capture_quit_main_menu(state);
    state->finished = true;
    return 0;
}

// The cfg keys menu_effects flips; snapshot them around the flow to prove
// the flip-twice discipline left settings untouched.
const std::array<std::pair<const char*, const char*>, 9> kEffectsCfgKeys = {{
    {"effects", "hit_recoil"}, {"effects", "attack_lunge"},
    {"effects", "mini_hp_bar"}, {"effects", "damage_numbers"},
    {"effects", "heal_numbers"}, {"effects", "weather"},
    {"effects", "shadows"}, {"effects", "fire_glow"},
    {"effects", "screen_shake"},
}};

void run_capture_flow(const char* scene, int (*injector)(void*),
                      CaptureState& state)
{
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    char scene_dir[512];
    snprintf(scene_dir, sizeof(scene_dir), "%s/%s",
             getenv("OG_FX_CAPTURE_DIR"), scene);
    std::filesystem::create_directories(scene_dir);
    setenv("OG_DUMP_DIR", scene_dir, 1);

    SDL_Thread* thread = SDL_CreateThread(injector, "capture_injector", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2; // the SETTINGS click exits mainmenu() once

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);
    unsetenv("OG_DUMP_DIR");

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
}

// menu_difficulty: main menu -> DIFFICULTY door -> cycle every setting through
// a FULL loop (3-value cycles get 3 clicks, permadeath 2) so each row visibly
// changes and every setting ends exactly where it started, then back and quit.
int menu_difficulty_injector(void* data)
{
    og::runtime::ensure_thread_session();
    CaptureState* state = static_cast<CaptureState*>(data);
    state->started = true;

    if (!wait_for_interactable("difficulty", 10000)) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(1000);

    // Highlight walk down to the DIFFICULTY door before opening it.
    g_test_menu_nav_key = KEY_DOWN;
    SDL_Delay(480);
    g_test_menu_nav_key = KEY_DOWN;
    SDL_Delay(480);

    bool in_menu = click_until_interactable("difficulty", "difficulty_back", 10000);
    SDL_Delay(900);
    if (in_menu || wait_for_interactable("difficulty_back", 10000)) {
        state->saw_options = true;

        struct RowPlan { const char* id; int clicks; };
        static const RowPlan kRows[5] = {
            {"difficulty", 3},     // Battle -> Slaughter -> Skirmish -> Battle
            {"respawn_mode", 3},   // Off -> Heroes -> Everyone -> Off
            {"respawn_delay", 3},  // Normal -> Fast -> Slow -> Normal
            {"permadeath", 2},     // On -> Off -> On
            {"generator_rate", 3}, // Normal -> Calm -> Frenzy -> Normal
        };
        bool all_rows = true;
        for (const RowPlan& row : kRows) {
            if (!wait_for_interactable(row.id, 5000)) {
                all_rows = false;
                continue;
            }
            g_test_menu_nav_key = KEY_DOWN; // walk the highlight row to row
            SDL_Delay(420);
            for (int c = 0; c < row.clicks; ++c)
                capture_toggle(row.id);
        }
        state->toured_fx = all_rows;

        if (wait_for_interactable("difficulty_back", 5000)) {
            SDL_Delay(400);
            interact("difficulty_back");
            state->used_options_back = true;
            SDL_Delay(700);
        }
    }

    capture_quit_main_menu(state);
    state->finished = true;
    return 0;
}

} // namespace menu_capture

TEST(OptionsMenu, zz_capture_menu_difficulty)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    // Session difficulty + save-backed settings must end where they started
    // (full-cycle discipline).
    const int diff_before = og::runtime::current_session->current_difficulty_;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const short respawn_before = save.respawn_mode;
    const short delay_before = save.ctf_respawn_ticks;
    const short keep_before = save.keep_fallen_heroes;
    const short rate_before = save.generator_rate;

    menu_capture::CaptureState state = {};
    menu_capture::run_capture_flow("menu_difficulty",
                                   menu_capture::menu_difficulty_injector,
                                   state);

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_options) << "should have entered the difficulty menu";
    ASSERT_TRUE(state.toured_fx) << "should have cycled every settings row";
    ASSERT_TRUE(state.used_options_back) << "should have exited via difficulty_back";

    EXPECT_EQ(diff_before, og::runtime::current_session->current_difficulty_);
    SaveData& after = og::runtime::current_session->myscreen_->save_data;
    EXPECT_EQ(respawn_before, after.respawn_mode);
    EXPECT_EQ(delay_before, after.ctf_respawn_ticks);
    EXPECT_EQ(keep_before, after.keep_fallen_heroes);
    EXPECT_EQ(rate_before, after.generator_rate);
}

TEST(OptionsMenu, zz_capture_menu_tour)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    menu_capture::CaptureState state = {};
    menu_capture::run_capture_flow("menu_tour",
                                   menu_capture::menu_tour_injector, state);

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_options) << "should have entered the options menu";
    ASSERT_TRUE(state.entered_controls) << "should have entered controls";
    ASSERT_TRUE(state.exited_controls) << "should have returned from controls";
    ASSERT_TRUE(state.used_options_back) << "should have exited settings";
}

TEST(OptionsMenu, zz_capture_menu_effects)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";
    std::array<bool, menu_capture::kEffectsCfgKeys.size()> before{};
    for (size_t i = 0; i < menu_capture::kEffectsCfgKeys.size(); ++i)
        before[i] = cfg.is_on(menu_capture::kEffectsCfgKeys[i].first,
                              menu_capture::kEffectsCfgKeys[i].second);

    menu_capture::CaptureState state = {};
    menu_capture::run_capture_flow("menu_effects",
                                   menu_capture::menu_effects_injector, state);

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_options) << "should have entered the options menu";
    ASSERT_TRUE(state.toured_fx) << "should have toured all three FX subscreens";
    ASSERT_TRUE(state.used_options_back) << "should have exited settings";

    for (size_t i = 0; i < menu_capture::kEffectsCfgKeys.size(); ++i)
        EXPECT_EQ(before[i], cfg.is_on(menu_capture::kEffectsCfgKeys[i].first,
                                       menu_capture::kEffectsCfgKeys[i].second))
            << menu_capture::kEffectsCfgKeys[i].first << "/"
            << menu_capture::kEffectsCfgKeys[i].second
            << " must end unchanged (flip-twice discipline)";
}
