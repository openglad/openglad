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
// toggle each screen hosts with its cfg (category, key) pair. Floor ghost
// lives in GRAPHICS FX now (still cfg graphics/floor_ghost).
struct FxToggleSpec {
    const char* button_id;
    const char* category;
    const char* key;
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
    {"toggle_depth_tint", "effects", "depth_tint"},
    {"toggle_trails", "effects", "trails"},
    {"toggle_fire_glow", "effects", "fire_glow"},
    {"toggle_ripples", "effects", "ripples"},
    {"toggle_screen_shake", "effects", "screen_shake"},
    {"toggle_floor_ghost", "graphics", "floor_ghost"},
};

inline constexpr int kFxScreenCount = 3;
inline constexpr int kFxMaxToggles = 13;

static const FxScreenSpec kFxScreens[kFxScreenCount] = {
    {"gameplay_fx", "gameplay_fx_back", kGameplayFxToggles, 2},
    {"ui_fx", "ui_fx_back", kUiFxToggles, 3},
    {"graphics_fx", "graphics_fx_back", kGraphicsFxToggles, 13},
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
static bool toggle_effect_and_check_flip(const char* button_id, const char* category,
                                         const char* cfg_key)
{
    const bool before = cfg.is_on(category, cfg_key);
    interact(button_id);
    // 300ms per the menu-test discipline: a shorter gap can land the next
    // click while this one's press is still held, and it gets dropped.
    SDL_Delay(300);
    return cfg.is_on(category, cfg_key) != before;
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
                state->toggled_fx[s][t] = toggle_effect_and_check_flip(
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
