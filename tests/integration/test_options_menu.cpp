#include <memory>
#include <array>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <vector>
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
// GAME SETTINGS SPEED handler (button.cpp), driven directly for its
// live-apply and relay-warning halves.
Sint32 change_game_speed();
// Level-start settings apply (glad_gameplay.cpp), driven directly: glad_init
// calls exactly this, right before it arms the replay recorder.
void apply_level_start_global_settings(screen& game_screen);

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[static_cast<std::size_t>(i)].reset();
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
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
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

// Test: Open GAME SETTINGS, toggle presentation settings across it and its
// subscreens, and exit.
//
// Flow: Main Menu -> Game Settings -> DISPLAY -> Back -> the three FX
// subscreens -> RESTORE SETTINGS -> Back -> (main menu exits)
//
// Verifies:
//   1. Options menu opens
//   2. Can toggle visual effects
//   3. RESTORE SETTINGS leaves player controls alone
//   4. Returns cleanly to main menu

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
    {"toggle_floor_glide", "effects", "floor_glide"},
    // Migrated from the retired in-game options menu. Its identity state is
    // ON, so this flip turns the classic palette rotation OFF.
    {"toggle_color_cycling", "effects", "color_cycling"},
};

inline constexpr int kFxScreenCount = 3;
inline constexpr int kFxMaxToggles = 14;

static const FxScreenSpec kFxScreens[kFxScreenCount] = {
    {"gameplay_fx", "gameplay_fx_back", kGameplayFxToggles, 2},
    {"ui_fx", "ui_fx_back", kUiFxToggles, 3},
    {"graphics_fx", "graphics_fx_back", kGraphicsFxToggles, 14},
};

struct OptionsState {
    bool started;
    bool finished;
    bool saw_options;
    bool changed_control_mode;
    bool settings_restore_preserved_controls;
    int initial_control_mode;
    bool entered_fx[kFxScreenCount];
    bool exited_fx[kFxScreenCount];
    bool toggled_fx[kFxScreenCount][kFxMaxToggles];
    bool gore_sprite_followed_toggle;
    bool gore_sprite_matches_cfg;
    bool cycled_zoom;
    bool cycled_smoothing;
    bool entered_display;
    bool cycled_display_mode;
    bool cycled_resolution;
    bool resolution_applied_to_window;
    int zoom_window_w;
    int zoom_window_h;
    int minimum_zoom_steps;
    bool exited_display;
    bool used_options_back;
    // SPEED (GAME SETTINGS) and BRIGHTNESS (DISPLAY): the two migrated rows
    // that must reach the live game, not just cfg.
    bool cycled_game_speed;
    int cfg_timer_wait_after_speed_click;
    int world_timer_wait_after_speed_click;
    bool stepped_brightness_up;
    bool stepped_brightness_down;
    int cfg_brightness_after_two_plus;
    int applied_brightness_after_two_plus;
    int cfg_brightness_after_return;
    // Labels the ZOOM / SMOOTH rows show while their cfg keys are the empty
    // string (as in any process that never ran load_settings()).
    std::string zoom_label_when_unset;
    std::string smoothing_label_when_unset;
};

// blood.png is solid palette index 40 (the red); blood_friendly.png is drawn
// from the grey/white indices instead. Whether the loader's live BLOOD sprite
// carries index 40 is therefore an exact read of which variant is installed —
// the check that fails whenever the gore setting is only applied at startup.
static bool live_blood_is_gory()
{
    loader* game_loader = og::runtime::current_session->myscreen_->myloader;
    if (game_loader == nullptr)
        return false;
    const PixieData& blood = game_loader->graphics[PIX(Order::Weapon, FAMILY_BLOOD)];
    if (!blood.valid())
        return false;
    const std::size_t len =
        static_cast<std::size_t>(blood.frames) * blood.w * blood.h;
    return std::find(blood.data.get(), blood.data.get() + len,
                     static_cast<unsigned char>(40)) != blood.data.get() + len;
}

// Click an FX-subscreen toggle and report whether its cfg key flipped.
// Under machine load a single click can be dropped (the press is still held
// when the handler samples the mouse), so poll for the flip and re-click
// until a deadline. Net flip parity doesn't matter: the flow ends with
// RESTORE SETTINGS, which reloads non-control cfg defaults and undoes every
// presentation/effect toggle.
static bool toggle_effect_and_check_flip(const char* button_id, const char* category,
                                         const char* cfg_key)
{
    const bool before = cfg.is_on(category, cfg_key);
    const Uint64 deadline = SDL_GetTicks() + 5000;
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
    const Uint64 deadline = SDL_GetTicks() + 5000;
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

    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (cfg.get_setting(category, cfg_key) != expected) {
        if (SDL_GetTicks() >= deadline)
            return false;
        interact(button_id);
        SDL_Delay(300);
    }
    return true;
}

// The SPEED cycler's eleven-step lap over cfg gameplay/timer_wait. The
// first verified click also has to land on the live world (the row replaces
// viewscreen::change_speed, which wrote GameWorld::timer_wait directly), so
// the caller records both sides right after it.
static bool cycle_game_speed_and_check_lap(const char* button_id,
                                           OptionsState* state)
{
    std::string expected = cfg.get_setting("gameplay", "timer_wait");
    for (int i = 0; i < 11; ++i)
        expected = og::ui::cycle_game_speed(expected);

    for (int i = 0; i < 11; ++i) {
        if (!click_cycle_step(button_id, "gameplay", "timer_wait"))
            return false;
        if (i == 0) {
            state->cfg_timer_wait_after_speed_click =
                og::ui::parse_timer_wait(cfg.get_setting("gameplay", "timer_wait"));
            state->world_timer_wait_after_speed_click = static_cast<int>(
                og::runtime::current_session->myscreen_->world().timer_wait);
        }
    }

    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (cfg.get_setting("gameplay", "timer_wait") != expected) {
        if (SDL_GetTicks() >= deadline)
            return false;
        interact(button_id);
        SDL_Delay(300);
    }
    return true;
}

// The zoom selector cycles over the resource-safe range for the current
// window, each click live-applied to the renderer's world canvas. The absent
// key normalizes to 1.0; compute the same dynamic minimum as the live button
// and finish the lap deadline-bounded if a re-click double-steps.
static bool cycle_zoom_and_check_lap(const char* button_id,
                                     int minimum_steps)
{
    const int lap_steps = og::kZoomStepsMax - minimum_steps + 1;
    std::string expected = cfg.get_setting("graphics", "zoom");
    for (int i = 0; i < lap_steps; ++i)
        expected = og::ui::cycle_zoom(expected, minimum_steps);

    for (int i = 0; i < lap_steps; ++i)
        if (!click_cycle_step(button_id, "graphics", "zoom"))
            return false;

    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (cfg.get_setting("graphics", "zoom") != expected) {
        if (SDL_GetTicks() >= deadline)
            return false;
        interact(button_id);
        SDL_Delay(300);
    }
    return true;
}

// The smoothing selector's three-way lap (off -> sai -> eagle -> off) over
// cfg graphics/smoothing, each click live-applied to the world present
// engine (the canvas dims are zoom-owned and stay put).
static bool cycle_smoothing_and_check_lap(const char* button_id)
{
    std::string expected = og::ui::effective_smoothing_setting(
        cfg.get_setting("graphics", "smoothing"),
        cfg.get_setting("graphics", "render"));
    for (int i = 0; i < 3; ++i)
        expected = og::ui::cycle_smoothing(expected);

    for (int i = 0; i < 3; ++i)
        if (!click_cycle_step(button_id, "graphics", "smoothing"))
            return false;

    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (cfg.get_setting("graphics", "smoothing") != expected) {
        if (SDL_GetTicks() >= deadline)
            return false;
        interact(button_id);
        SDL_Delay(300);
    }
    return true;
}

// Click the display-mode selector around one observable lap. Drivers without
// exclusive fullscreen deliberately skip that unavailable state, so the lap
// has two steps there and three where all modes are supported.
static bool cycle_display_mode_and_check_lap(const char* button_id)
{
    const og::ui::DisplayMode start =
        og::ui::parse_display_mode(cfg.get_setting("graphics", "fullscreen"));
    for (int i = 0; i < 3; ++i) {
        if (!click_cycle_step(button_id, "graphics", "fullscreen"))
            return false;
        if (og::ui::parse_display_mode(cfg.get_setting("graphics", "fullscreen")) == start)
            return true;
    }
    return false;
}

static bool select_windowed_mode(const char* button_id)
{
    for (int i = 0; i < 3; ++i) {
        if (og::ui::parse_display_mode(
                cfg.get_setting("graphics", "fullscreen")) ==
            og::ui::DisplayMode::Windowed)
            return true;
        if (!click_cycle_step(button_id, "graphics", "fullscreen"))
            return false;
    }
    return false;
}

static std::string latest_trace_message(const char* category)
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    for (auto it = g_trace_buffer.rbegin(); it != g_trace_buffer.rend(); ++it)
        if (it->category == category)
            return it->message;
    return {};
}

// Click the resolution selector around one full lap and verify cfg
// graphics/width follows the pure next_resolution orbit. Mirrors the
// picker's resolution_choices(): only real platform modes in Exclusive;
// otherwise the logical desktop-derived fallback plus the current cfg size.
static std::vector<std::pair<int, int>> expected_resolution_choices()
{
    screen* scr = og::runtime::current_session->myscreen_;
	const og::ui::DisplayMode mode = og::ui::parse_display_mode(
		cfg.get_setting("graphics", "fullscreen"));
    const std::pair<int, int> current = og::ui::parse_resolution(
        cfg.get_setting("graphics", "width"), cfg.get_setting("graphics", "height"));
	const std::vector<std::pair<int, int>> display_modes =
		mode == og::ui::DisplayMode::Exclusive
			? scr->display_resolutions()
			: std::vector<std::pair<int, int>>{};
	const std::pair<int, int> desktop =
		mode == og::ui::DisplayMode::Exclusive
			? scr->desktop_resolution()
			: scr->windowed_desktop_resolution();
    return og::ui::build_resolution_choices(
		display_modes, desktop, current, mode);
}

static bool cycle_resolution_and_check_lap(const char* button_id)
{
    // First click: leaves the (off-list) boot default for its neighbor on
    // the current-inclusive lap.
    const std::vector<std::pair<int, int>> start_list = expected_resolution_choices();
    const std::pair<int, int> first = og::ui::next_resolution(
        start_list, cfg.get_setting("graphics", "width"),
        cfg.get_setting("graphics", "height"));
    if (start_list.size() <= 1)
        return first == og::ui::parse_resolution(
            cfg.get_setting("graphics", "width"), cfg.get_setting("graphics", "height"));
    if (!click_cycle_step(button_id, "graphics", "width"))
        return false;
    {
        const Uint64 deadline = SDL_GetTicks() + 5000;
        while (cfg.get_setting("graphics", "width") != std::to_string(first.first)) {
            if (SDL_GetTicks() >= deadline)
                return false;
            SDL_Delay(100);
        }
    }

    // From an in-list entry the lap is stationary: one full orbit of the
    // base list must return exactly here.
    const std::vector<std::pair<int, int>> list = expected_resolution_choices();
    const std::string lap_start = cfg.get_setting("graphics", "width");
    for (std::size_t i = 0; i < list.size(); ++i)
        if (!click_cycle_step(button_id, "graphics", "width"))
            return false;
    const Uint64 deadline = SDL_GetTicks() + 5000;
    while (cfg.get_setting("graphics", "width") != lap_start) {
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

    // Wait for the main menu, then enter the shared settings family.
    if (!wait_for_interactable("options", 5000)) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking options\n");
    bool in_options =
        click_until_interactable("options", "gameplay_fx", 10000);

    // Options menu buttons
    SDL_Delay(150);
    if (in_options || wait_for_interactable("gameplay_fx", 10000)) {
        state->saw_options = true;
        SDL_Delay(150);

        // Player controls belong to the per-seat player screens, which are
        // not reachable from here — but RESTORE SETTINGS below shares this
        // screen with them through the same cfg file, and must not touch
        // them. Change P1's direction mode exactly as a player screen does
        // (the row calls this function), then check it survives the restore.
        fprintf(stderr, "  [test] changing the P1 direction mode\n");
        toggle_player_control_mode(0);
        state->changed_control_mode =
            get_player_control_mode(0) != state->initial_control_mode;

        fprintf(stderr, "  [test] toggling sound\n");
        interact("toggle_sound");
        SDL_Delay(80);

        // SPEED: a full eleven-step lap of cfg gameplay/timer_wait, with the
        // first click checked against the live world field too.
        fprintf(stderr, "  [test] cycling game speed through a full lap\n");
        state->cycled_game_speed =
            cycle_game_speed_and_check_lap("game_speed", state);
        SDL_Delay(300);

        // Everything window/presentation related lives in the DISPLAY
        // subscreen: enter it, exercise every row, and leave via its BACK.
        fprintf(stderr, "  [test] entering display settings\n");
        if (click_until_interactable("display_settings", "display_mode", 5000)) {
            state->entered_display = true;
            SDL_Delay(300);

            // With cfg (graphics, zoom) and (graphics, smoothing) empty,
            // the per-frame label sync must fall back to the classic
            // defaults, never a blank face.
            {
                const std::string prev_zoom = cfg.get_setting("graphics", "zoom");
                const std::string prev_smoothing =
                    cfg.get_setting("graphics", "smoothing");
                const std::string prev_render =
                    cfg.get_setting("graphics", "render");
                cfg.apply_setting("graphics", "zoom", "");
                cfg.apply_setting("graphics", "smoothing", "");
                cfg.apply_setting("graphics", "render", "normal");
                SDL_Delay(300);  // let the display loop run label-sync frames
                for (const Interactable& item : get_interactables()) {
                    if (item.id == "display_zoom")
                        state->zoom_label_when_unset = item.label;
                    if (item.id == "display_smoothing")
                        state->smoothing_label_when_unset = item.label;
                }
                cfg.apply_setting("graphics", "zoom", prev_zoom);
                cfg.apply_setting("graphics", "smoothing", prev_smoothing);
                cfg.apply_setting("graphics", "render", prev_render);
                SDL_Delay(150);
            }
            fprintf(stderr, "  [test] cycling display mode\n");
            state->cycled_display_mode = cycle_display_mode_and_check_lap("display_mode");
            SDL_Delay(300);
            fprintf(stderr, "  [test] cycling resolution\n");
            trace_clear();
            state->cycled_resolution = select_windowed_mode("display_mode") &&
                cycle_resolution_and_check_lap("display_resolution");
            SDL_Delay(300);
            // The menu-thread trace records the normalized cfg and the REAL
            // session size after every resolution callback. Reading those
            // fields directly from this injector would race SDL window events.
            state->resolution_applied_to_window =
                trace_count("display_resolution_apply") > 0 &&
                !trace_contains("display_resolution_apply", "match=0");
            const std::string applied =
                latest_trace_message("display_resolution_apply");
            int requested_w = 0;
            int requested_h = 0;
            int normalized_w = 0;
            int normalized_h = 0;
            int match = 0;
            if (std::sscanf(
                    applied.c_str(),
                    "requested=%dx%d normalized=%dx%d actual=%dx%d "
                    "min_zoom=%d match=%d",
                    &requested_w, &requested_h, &normalized_w, &normalized_h,
                    &state->zoom_window_w, &state->zoom_window_h,
                    &state->minimum_zoom_steps, &match) != 8 ||
                match != 1) {
                state->resolution_applied_to_window = false;
            }

            // Menu.button_misc_paths intentionally leaves the retired legacy
            // graphics/render value at "sai". Pin this selector to explicit
            // Off so the zoom traces are order-independent and exercise the
            // nearest path before the separate smoothing lap below.
            cfg.apply_setting("graphics", "smoothing", "off");
            // Resolution application may have emitted canvas traces using a
            // legacy order-dependent smoothing value. From here onward only
            // the explicit zoom/smoothing laps may satisfy renderer checks.
            trace_clear();

            fprintf(stderr, "  [test] adjusting overscan\n");
            interact("overscan_plus");
            SDL_Delay(80);
            interact("overscan_minus");
            SDL_Delay(80);

            // BRIGHTNESS: two steps up and back. Each click must move cfg
            // AND the applied gamma the palette path re-reads.
            fprintf(stderr, "  [test] stepping brightness\n");
            state->stepped_brightness_up =
                click_cycle_step("brightness_plus", "graphics", "brightness") &&
                click_cycle_step("brightness_plus", "graphics", "brightness");
            SDL_Delay(300);
            state->cfg_brightness_after_two_plus =
                og::ui::parse_brightness_steps(
                    cfg.get_setting("graphics", "brightness"));
            state->applied_brightness_after_two_plus =
                static_cast<int>(display_brightness_steps());
            state->stepped_brightness_down =
                click_cycle_step("brightness_minus", "graphics", "brightness") &&
                click_cycle_step("brightness_minus", "graphics", "brightness");
            SDL_Delay(300);
            state->cfg_brightness_after_return =
                og::ui::parse_brightness_steps(
                    cfg.get_setting("graphics", "brightness"));

            // A full verified lap of the zoom cycle: every click steps cfg
            // graphics/zoom AND live-applies it to the world canvas (the
            // menu keeps presenting the 320x200 UI canvas).
            fprintf(stderr, "  [test] cycling zoom through a full lap\n");
            state->cycled_zoom = cycle_zoom_and_check_lap(
                "display_zoom", state->minimum_zoom_steps);
            SDL_Delay(300);
            fprintf(stderr, "  [test] cycling smoothing through a full lap\n");
            state->cycled_smoothing =
                cycle_smoothing_and_check_lap("display_smoothing");
            SDL_Delay(300);

            fprintf(stderr, "  [test] leaving display settings\n");
            state->exited_display =
                click_until_interactable("display_back", "display_settings", 5000);
            SDL_Delay(300);
        }

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
                const bool is_gore =
                    std::strcmp(toggle.button_id, "toggle_gore") == 0;
                bool blood_was_gory = false;
                if (is_gore) {
                    // Bring the loader in line with cfg first, so the click's
                    // direction is known whatever earlier tests left behind.
                    loader* game_loader =
                        og::runtime::current_session->myscreen_->myloader;
                    if (game_loader != nullptr)
                        game_loader->sync_gore_graphics();
                    blood_was_gory = live_blood_is_gory();
                }
                state->toggled_fx[s][t] = toggle.cycle
                    ? cycle_effect_and_check_lap(
                          toggle.button_id, toggle.category, toggle.key)
                    : toggle_effect_and_check_flip(
                          toggle.button_id, toggle.category, toggle.key);
                if (is_gore && state->toggled_fx[s][t]) {
                    // The click must repaint the live sprite, not just cfg.
                    const Uint64 deadline = SDL_GetTicks() + 3000;
                    while (live_blood_is_gory() == blood_was_gory &&
                           SDL_GetTicks() < deadline) {
                        SDL_Delay(50);
                    }
                    state->gore_sprite_followed_toggle =
                        live_blood_is_gory() != blood_was_gory;
                    state->gore_sprite_matches_cfg =
                        live_blood_is_gory() == cfg.is_on("effects", "gore");
                }
            }

            fprintf(stderr, "  [test] leaving %s subscreen\n", screen.opener_id);
            state->exited_fx[s] =
                click_until_interactable(screen.back_id, screen.opener_id, 5000);
            wait_for_interactable(screen.opener_id, 10000);
            SDL_Delay(300);
        }

        // Restore game settings last, undoing the presentation and
        // FX flips without touching the player-control mode changed above.
        fprintf(stderr, "  [test] restoring settings\n");
        interact("restore_defaults");
        SDL_Delay(300);
        state->settings_restore_preserved_controls =
            get_player_control_mode(0) != state->initial_control_mode;

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
    const Uint64 quit_deadline = SDL_GetTicks() + 3000;
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

// Direct coverage of the display-settings apply: all three mode branches and
// the mode enumeration run headlessly (fullscreen requests fail gracefully
// under the offscreen/dummy drivers; the cfg -> window -> session bookkeeping
// still executes).
// Level start applies both migrated global settings to the world the mission
// runs with (glad_gameplay.cpp calls this one line before it arms the replay
// recorder, so the header records the seeded speed). Before the move,
// timer_wait only ever changed live and never persisted, and cyclemode was
// hardcoded on in the screen constructor.
TEST(OptionsMenu, level_start_applies_speed_and_color_cycling_from_cfg) {
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    const std::string saved_speed = cfg.get_setting("gameplay", "timer_wait");
    const std::string saved_cycling = cfg.get_setting("effects", "color_cycling");
    const signed char saved_wait = game_screen->world().timer_wait;
    const short saved_mode = game_screen->cyclemode;

    cfg.apply_setting("gameplay", "timer_wait", "12");  // SPEED 5
    cfg.apply_setting("effects", "color_cycling", "off");
    apply_level_start_global_settings(*game_screen);
    EXPECT_EQ(12, static_cast<int>(game_screen->world().timer_wait))
        << "the mission must start at the stored speed";
    EXPECT_EQ(0, game_screen->cyclemode)
        << "color cycling off must reach the live flag at level start";

    // The shipped defaults restore the classic pair.
    cfg.apply_setting("gameplay", "timer_wait", "6");
    cfg.apply_setting("effects", "color_cycling", "on");
    apply_level_start_global_settings(*game_screen);
    EXPECT_EQ(og::sim::DEFAULT_TIMER_WAIT, game_screen->world().timer_wait);
    EXPECT_EQ(1, game_screen->cyclemode);

    // A config that never mentioned the keys still starts a normal mission.
    cfg.apply_setting("gameplay", "timer_wait", "");
    cfg.apply_setting("effects", "color_cycling", "");
    apply_level_start_global_settings(*game_screen);
    EXPECT_EQ(og::sim::DEFAULT_TIMER_WAIT, game_screen->world().timer_wait)
        << "an absent speed key must fall back to the shipped cadence";
    EXPECT_EQ(1, game_screen->cyclemode)
        << "an absent color-cycling key must still rotate: unlike every "
           "other effects key, ON is this one's identity state";

    cfg.apply_setting("gameplay", "timer_wait",
                      saved_speed.empty() ? "6" : saved_speed);
    cfg.apply_setting("effects", "color_cycling",
                      saved_cycling.empty() ? "on" : saved_cycling);
    game_screen->world().timer_wait = saved_wait;
    game_screen->cyclemode = saved_mode;
}

// SPEED's live-apply half, driven directly (the injector flow above covers
// the click plumbing). The row carries over what the retired
// viewscreen::change_speed did: the world field, the host-authoritative
// pending request, and the relay warning that fires ONCE per descent into
// the relay-expensive speeds and re-arms when the speed comes back down.
TEST(OptionsMenu, game_speed_applies_to_the_world_and_warns_once_on_relay) {
    constexpr Sint32 kMenuRedraw = 2;  // do_call's REDRAW
    og::runtime::SessionState* const session = og::runtime::current_session;
    screen* const scr = session->myscreen_;
    const std::string saved_cfg = cfg.get_setting("gameplay", "timer_wait");
    const signed char saved_wait = scr->world().timer_wait;
    const bool saved_relay = session->relay_transport_active_;
    const bool saved_warned = session->relay_speed_warning_shown_;
    const std::int8_t saved_request = session->pending_timer_wait_request_;

    session->relay_transport_active_ = false;
    session->relay_speed_warning_shown_ = false;
    cfg.apply_setting("gameplay", "timer_wait", "6");  // SPEED 8

    ASSERT_EQ(kMenuRedraw, change_game_speed());
    EXPECT_EQ("4", cfg.get_setting("gameplay", "timer_wait"))
        << "one click is one SPEED step faster";
    EXPECT_EQ(4, static_cast<int>(scr->world().timer_wait))
        << "the click must reach the live sim cadence";
    EXPECT_EQ(4, static_cast<int>(session->pending_timer_wait_request_))
        << "the host stamps the request the server applies for its own peer";
    EXPECT_FALSE(session->relay_speed_warning_shown_)
        << "no relay, no warning";

    // On a relay, the first step below the relay-friendly wait warns and
    // latches; further steps in the same descent stay quiet.
    session->relay_transport_active_ = true;
    ASSERT_EQ(kMenuRedraw, change_game_speed());  // wait 4 -> 2
    EXPECT_EQ(2, static_cast<int>(scr->world().timer_wait));
    EXPECT_TRUE(session->relay_speed_warning_shown_)
        << "dropping below wait 4 on a relay must warn";
    ASSERT_EQ(kMenuRedraw, change_game_speed());  // wait 2 -> 0
    EXPECT_EQ(0, static_cast<int>(scr->world().timer_wait));
    EXPECT_TRUE(session->relay_speed_warning_shown_)
        << "the warning latch must not re-fire while still fast";

    // Wrapping back to the slowest speed re-arms it.
    ASSERT_EQ(kMenuRedraw, change_game_speed());  // wraps to wait 20
    EXPECT_EQ(20, static_cast<int>(scr->world().timer_wait));
    EXPECT_FALSE(session->relay_speed_warning_shown_)
        << "a relay-friendly speed must re-arm the warning";

    session->relay_transport_active_ = saved_relay;
    session->relay_speed_warning_shown_ = saved_warned;
    session->pending_timer_wait_request_ = saved_request;
    scr->world().timer_wait = saved_wait;
    cfg.apply_setting("gameplay", "timer_wait",
                      saved_cfg.empty() ? "6" : saved_cfg);
}

TEST(OptionsMenu, apply_display_settings_covers_all_modes) {
    const std::string prev_mode = cfg.get_setting("graphics", "fullscreen");
    const std::string prev_w = cfg.get_setting("graphics", "width");
    const std::string prev_h = cfg.get_setting("graphics", "height");
    const std::string prev_windowed_w =
        cfg.get_setting("graphics", "windowed_width");
    const std::string prev_windowed_h =
        cfg.get_setting("graphics", "windowed_height");

    screen* scr = og::runtime::current_session->myscreen_;

    cfg.apply_setting("graphics", "width", "960");
    cfg.apply_setting("graphics", "height", "600");
    for (const char* mode : {"off", "borderless", "exclusive"}) {
        cfg.apply_setting("graphics", "fullscreen", mode);
        scr->apply_display_settings_from_cfg();
        // The session must always track the REAL window afterwards.
        EXPECT_GT(og::runtime::current_session->window_w_, 0.0f) << mode;
        EXPECT_GT(og::runtime::current_session->window_h_, 0.0f) << mode;
    }

    // An unsupported exclusive request must not survive in cfg as though it
    // had been applied. SDL either chooses a real closest mode (whose exact
    // dimensions we store) or settles on borderless/windowed (whose real
    // window dimensions and mode we store).
    cfg.apply_setting("graphics", "width", "731");
    cfg.apply_setting("graphics", "height", "457");
    cfg.apply_setting("graphics", "fullscreen", "exclusive");
    scr->apply_display_settings_from_cfg();
    const bool actual_fullscreen =
        (SDL_GetWindowFlags(E_Screen->window) & SDL_WINDOW_FULLSCREEN) != 0;
    const SDL_DisplayMode* actual_mode = actual_fullscreen
        ? SDL_GetWindowFullscreenMode(E_Screen->window)
        : nullptr;
    if (actual_mode != nullptr) {
		const auto actual_pixels =
			og::platform::display_mode_pixel_size(*actual_mode);
        EXPECT_EQ("exclusive", cfg.get_setting("graphics", "fullscreen"));
		EXPECT_EQ(std::to_string(actual_pixels.first),
		          cfg.get_setting("graphics", "width"));
		EXPECT_EQ(std::to_string(actual_pixels.second),
		          cfg.get_setting("graphics", "height"));
    } else {
        EXPECT_EQ(actual_fullscreen ? "borderless" : "off",
                  cfg.get_setting("graphics", "fullscreen"));
		if (actual_fullscreen) {
			// Borderless keeps the remembered logical Windowed size; its
			// physical monitor size is supplied separately for the label.
			EXPECT_EQ("960", cfg.get_setting("graphics", "width"));
			EXPECT_EQ("600", cfg.get_setting("graphics", "height"));
		} else {
			EXPECT_EQ(std::to_string(static_cast<int>(og::runtime::current_session->window_w_)),
			          cfg.get_setting("graphics", "width"));
			EXPECT_EQ(std::to_string(static_cast<int>(og::runtime::current_session->window_h_)),
			          cfg.get_setting("graphics", "height"));
		}

        // Starting from Borderless, the mode selector requests Exclusive.
        // If SDL cannot provide it, the callback must skip to Windowed
        // instead of becoming permanently trapped on Borderless.
        cfg.apply_setting("graphics", "width", "731");
        cfg.apply_setting("graphics", "height", "457");
        cfg.apply_setting("graphics", "fullscreen", "borderless");
        scr->apply_display_settings_from_cfg();
        change_display_mode();
        EXPECT_NE(og::ui::DisplayMode::Borderless,
                  og::ui::parse_display_mode(
                      cfg.get_setting("graphics", "fullscreen")));
        EXPECT_EQ("731", cfg.get_setting("graphics", "width"));
        EXPECT_EQ("457", cfg.get_setting("graphics", "height"));
    }
    // Windowed sizing does reach the real window headlessly.
    cfg.apply_setting("graphics", "width", "960");
    cfg.apply_setting("graphics", "height", "600");
    cfg.apply_setting("graphics", "fullscreen", "off");
    scr->apply_display_settings_from_cfg();
    EXPECT_EQ(960, static_cast<int>(og::runtime::current_session->window_w_));
    EXPECT_EQ(600, static_cast<int>(og::runtime::current_session->window_h_));
    EXPECT_EQ("960", cfg.get_setting("graphics", "windowed_width"));
    EXPECT_EQ("600", cfg.get_setting("graphics", "windowed_height"));

    // SDL may report completion after the initiating apply has returned. Feed
    // only causes consistent with the real state: ENTER/LEAVE themselves are
    // authoritative, while RESIZED and HiDPI PIXEL_SIZE_CHANGED confirm a
    // same-fullscreen mode change without supplying logical dimensions.
    const bool event_actual_fullscreen =
        (SDL_GetWindowFlags(E_Screen->window) & SDL_WINDOW_FULLSCREEN) != 0;
    const std::string event_actual_mode = !event_actual_fullscreen
        ? "off"
        : (SDL_GetWindowFullscreenMode(E_Screen->window) != nullptr
               ? "exclusive"
               : "borderless");
    const std::array<Uint32, 3> completion_events = event_actual_fullscreen
        ? std::array<Uint32, 3>{SDL_EVENT_WINDOW_ENTER_FULLSCREEN,
                                SDL_EVENT_WINDOW_RESIZED,
                                SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED}
        : std::array<Uint32, 3>{SDL_EVENT_WINDOW_LEAVE_FULLSCREEN,
                                SDL_EVENT_WINDOW_RESIZED,
                                SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED};
    for (const Uint32 event_type : completion_events) {
        cfg.apply_setting("graphics", "fullscreen",
                          event_actual_mode == "off" ? "borderless" : "off");
        SDL_Event event{};
        event.type = event_type;
        if (event_type == SDL_EVENT_WINDOW_RESIZED) {
            SDL_GetWindowSize(E_Screen->window,
                              &event.window.data1, &event.window.data2);
        }
        handle_window_event(event);
        EXPECT_EQ(event_actual_mode, cfg.get_setting("graphics", "fullscreen"))
            << "event type " << event_type;
    }

    // Headless drivers may enumerate no fullscreen modes. The Exclusive
    // selector stays put instead of inventing a desktop-sized video mode.
    const std::vector<std::pair<int, int>> modes = scr->display_resolutions();
    for (const auto& m : modes) {
        EXPECT_GE(m.first, 640);
        EXPECT_GE(m.second, 400);
    }

    // Restore boot state (640x400 windowed) through the same live path.
    cfg.apply_setting("graphics", "fullscreen", prev_mode);
    cfg.apply_setting("graphics", "width", prev_w.empty() ? "640" : prev_w);
    cfg.apply_setting("graphics", "height", prev_h.empty() ? "400" : prev_h);
    cfg.apply_setting("graphics", "windowed_width",
                      prev_windowed_w.empty() ? "640" : prev_windowed_w);
    cfg.apply_setting("graphics", "windowed_height",
                      prev_windowed_h.empty() ? "400" : prev_windowed_h);
    scr->apply_display_settings_from_cfg();
    if (prev_w.empty()) {
        cfg.apply_setting("graphics", "width", "");
        cfg.apply_setting("graphics", "height", "");
    }
    if (prev_mode.empty())
        cfg.apply_setting("graphics", "fullscreen", "");
    if (prev_windowed_w.empty())
        cfg.apply_setting("graphics", "windowed_width", "");
    if (prev_windowed_h.empty())
        cfg.apply_setting("graphics", "windowed_height", "");
}

TEST(OptionsMenu, options_menu) {
    trace_clear();

    reset_default_player_controls();
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();

    // Need save data for continue_game
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    OptionsState state = {};
    state.initial_control_mode = get_player_control_mode(0);
    SDL_Thread* thread = SDL_CreateThread(options_injector, "options_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;  // options REDRAW is handled within same mainmenu() call

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    // Leave this integration process with the same default controls it began
    // with; the assertions below use the injector's captured results.
    reset_default_player_controls();
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();

    ASSERT_TRUE(state.started) << "injector thread should have started";
    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_options) << "should have entered the options menu";
    ASSERT_TRUE(state.changed_control_mode)
        << "the player-screen mode toggle should change the P1 direction mode";
    ASSERT_TRUE(state.settings_restore_preserved_controls)
        << "GAME SETTINGS restore must not reset player controls";
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
    // Issue #158: the gore row used to change nothing but a cfg key, so the
    // blood sprite kept whatever variant the loader picked at startup until the
    // next launch. Both halves have to move together.
    EXPECT_TRUE(state.gore_sprite_followed_toggle)
        << "clicking Gore must swap the live blood sprite, not just cfg "
           "effects/gore (the toggle must not need a restart)";
    EXPECT_TRUE(state.gore_sprite_matches_cfg)
        << "after the click the installed blood sprite must match cfg "
           "effects/gore";
    ASSERT_TRUE(state.used_options_back) << "should have exited options via options_back";
    EXPECT_EQ("Zoom: 1.0x", state.zoom_label_when_unset)
        << "empty cfg (graphics, zoom) must fall back to 'Zoom: 1.0x' on the button face";
    EXPECT_EQ("Smooth: Off", state.smoothing_label_when_unset)
        << "empty cfg (graphics, smoothing) must fall back to 'Smooth: Off' on the button face";
    ASSERT_TRUE(state.entered_display) << "should have entered the DISPLAY subscreen";
    ASSERT_TRUE(state.cycled_display_mode)
        << "display_mode must step cfg graphics/fullscreen around windowed/borderless/exclusive";
    ASSERT_TRUE(state.cycled_resolution)
        << "display_resolution must step cfg graphics/width around the preset lap";
    EXPECT_TRUE(state.resolution_applied_to_window)
        << "each resolution click must live-apply to the real window "
           "(session window_w_/h_ re-read from SDL after the apply)";
    ASSERT_TRUE(state.cycled_zoom)
        << "display_zoom must step cfg graphics/zoom around its safe lap";
    ASSERT_TRUE(state.cycled_smoothing)
        << "display_smoothing must step cfg graphics/smoothing around off/sai/eagle";
    ASSERT_TRUE(state.exited_display) << "should have returned via display_back";
    // SPEED: cfg is the persisted value AND the live world's sim cadence.
    ASSERT_TRUE(state.cycled_game_speed)
        << "game_speed must step cfg gameplay/timer_wait around its 11-step lap";
    EXPECT_EQ(state.cfg_timer_wait_after_speed_click,
              state.world_timer_wait_after_speed_click)
        << "a SPEED click must reach GameWorld::timer_wait, not just cfg "
           "(the row replaced viewscreen::change_speed)";
    // BRIGHTNESS: both halves of the pair move, and the applied gamma
    // follows the stored value on every click.
    ASSERT_TRUE(state.stepped_brightness_up)
        << "brightness_plus must step cfg graphics/brightness";
    EXPECT_EQ(2, state.cfg_brightness_after_two_plus);
    EXPECT_EQ(2, state.applied_brightness_after_two_plus)
        << "the click must apply the gamma the palette path re-reads, not "
           "only store it";
    ASSERT_TRUE(state.stepped_brightness_down)
        << "brightness_minus must step cfg graphics/brightness back";
    EXPECT_EQ(0, state.cfg_brightness_after_return);
    // Each click live-applies the value: the zoom steps and both smart
    // scalers must all have reached the renderer
    // (apply_world_scale_from_cfg traces the parsed values).
    EXPECT_TRUE(trace_contains("canvas", "zoom steps=9"))
        << "the first zoom-out step must re-derive the world canvas";
    ASSERT_GT(state.zoom_window_w, 0);
    ASSERT_GT(state.zoom_window_h, 0);
    ASSERT_GT(state.minimum_zoom_steps, 0);
    const og::WorldCanvasDims half_zoom = og::compute_zoom_canvas_dims(
        state.zoom_window_w, state.zoom_window_h, 5);
    const std::string half_zoom_trace =
        "zoom steps=5 smoothing=1 canvas=" + std::to_string(half_zoom.w) +
        "x" + std::to_string(half_zoom.h);
    EXPECT_TRUE(trace_contains("canvas", half_zoom_trace.c_str()))
        << "the 0.5 step must double the classic-density canvas";
    const og::WorldCanvasDims deepest = og::compute_zoom_canvas_dims(
        state.zoom_window_w, state.zoom_window_h, state.minimum_zoom_steps);
    const std::string deepest_trace =
        "zoom steps=" + std::to_string(state.minimum_zoom_steps) +
        " smoothing=1 canvas=" + std::to_string(deepest.w) + "x" +
        std::to_string(deepest.h);
    EXPECT_TRUE(trace_contains("canvas", deepest_trace.c_str()))
        << "the deepest resource-safe step must reach its derived canvas";
    const og::WorldCanvasDims classic_zoom = og::compute_zoom_canvas_dims(
        state.zoom_window_w, state.zoom_window_h, 10);
    const std::string classic_zoom_trace =
        "zoom steps=10 smoothing=1 canvas=" +
        std::to_string(classic_zoom.w) + "x" +
        std::to_string(classic_zoom.h);
    EXPECT_TRUE(trace_contains("canvas", classic_zoom_trace.c_str()))
        << "the closing wrap to 1.0 must restore classic density";
    EXPECT_TRUE(trace_contains("canvas", "smoothing=2"))
        << "the sai smoothing step must reach the renderer";
    EXPECT_TRUE(trace_contains("canvas", "smoothing=3"))
        << "the eagle smoothing step must reach the renderer";
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
    bool entered_display;
    bool exited_display;
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
    const Uint64 quit_deadline = SDL_GetTicks() + 5000;
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

// menu_tour: main menu highlight walk -> GAME SETTINGS -> DISPLAY (step
// brightness up and back) -> back -> back -> quit.
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

    bool in_options = click_until_interactable(
        "options", "display_settings", 10000);
    SDL_Delay(900);
    if (in_options || wait_for_interactable("display_settings", 10000)) {
        state->saw_options = true;

        // Two nav steps inside GAME SETTINGS so the highlight is seen moving.
        g_test_menu_nav_key = KEY_DOWN;
        SDL_Delay(480);
        g_test_menu_nav_key = KEY_RIGHT;
        SDL_Delay(480);

        bool in_display = click_until_interactable(
            "display_settings", "brightness_plus", 5000);
        SDL_Delay(900);
        if (in_display || wait_for_interactable("brightness_plus", 5000)) {
            state->entered_display = true;
            g_test_menu_nav_key = KEY_DOWN;
            SDL_Delay(480);
            capture_toggle("brightness_plus"); // the palette visibly lifts
            capture_toggle("brightness_minus"); // back: settings unchanged
            state->exited_display =
                click_until_interactable(
                    "display_back", "display_settings", 5000);
            wait_for_interactable("display_settings", 10000);
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
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
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

// menu_difficulty: main menu -> CONTINUE -> Base Camp -> the DIFFICULTY door
// on the command strip -> cycle every setting through a FULL loop (3-value
// cycles get 3 clicks, permadeath 2) so each row visibly changes and every
// setting ends exactly where it started, then back and quit.
int menu_difficulty_injector(void* data)
{
    og::runtime::ensure_thread_session();
    CaptureState* state = static_cast<CaptureState*>(data);
    state->started = true;

    if (!wait_for_interactable("continue_game", 10000)) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(1000);
    interact("continue_game");
    SDL_Delay(500);
    if (!wait_for_interactable("difficulty", 10000)) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(1000);

    // Highlight walk along the Base Camp strip before opening the door: the
    // roster is the default highlight, so DOWN reaches the strip and RIGHT
    // steps BACK -> DIFFICULTY.
    g_test_menu_nav_key = KEY_DOWN;
    SDL_Delay(480);
    g_test_menu_nav_key = KEY_RIGHT;
    SDL_Delay(480);

    bool in_menu = click_until_interactable("difficulty", "difficulty_back", 10000);
    SDL_Delay(900);
    if (in_menu || wait_for_interactable("difficulty_back", 10000)) {
        state->saw_options = true;

        struct RowPlan { const char* id; int clicks; };
        static const RowPlan kRows[6] = {
            {"difficulty", 3},     // Battle -> Slaughter -> Skirmish -> Battle
            {"respawn_mode", 4},   // Off -> Heroes -> Everyone -> Team 1 -> Off
            {"respawn_delay", 3},  // Normal -> Fast -> Slow -> Normal
            {"permadeath", 2},     // On -> Off -> On
            {"generator_rate", 3}, // Normal -> Calm -> Frenzy -> Normal
            {"infinite_gold", 2},  // Off -> On -> Off
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
            // Dwell on the Base Camp the nested BACK returns to.
            wait_for_interactable("go", 5000);
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
    const short gold_before = save.infinite_gold;

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
    EXPECT_EQ(gold_before, after.infinite_gold);
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
    ASSERT_TRUE(state.saw_options) << "should have entered Game Settings";
    ASSERT_TRUE(state.entered_display) << "should have entered DISPLAY";
    ASSERT_TRUE(state.exited_display) << "should have returned from DISPLAY";
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
