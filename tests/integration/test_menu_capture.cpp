// ---------------------------------------------------------------------------
// The three menu SCENES: Base Camp -> DIFFICULTY, the main-menu tour through
// GAME SETTINGS into DISPLAY, and the three FX subscreens.
//
// They are two things at once, and the split is the point.
//
//   ALWAYS-RUN (every ctest lane, no env var): each scene walks the real
//   picker with the real keyboard-nav program and proves, at every step,
//   that (a) the nav key LANDED on the button the live nav graph names,
//   (b) the presenter froze a real composed frame with ink on it, (c) the
//   screen behind the door published exactly the row ids its spec table
//   carries, and (d) the row's live FACE agrees with the value the session
//   stores. Those four are what nothing else in the suite pins:
//   Difficulty.submenu_door_flow and OptionsMenu.options_menu already own
//   the cyclers, the FX round-trips and RESTORE SETTINGS, so these scenes
//   deliberately click no cycler at all in this mode.
//
//   FILM (only with OG_FX_CAPTURE_DIR set, from scripts/fx_review/
//   generate.sh): the same walk, plus the six difficulty laps and the
//   flip-twice FX passes at camera pace, with the TESTING dump hook armed
//   (OG_DUMP_DIR, every 3rd present in screen::buffer_to_screen) so the
//   review site gets its frames. Nothing under this branch runs in CI.
//
// These three used to GTEST_SKIP unless OG_FX_CAPTURE_DIR was set, which
// meant zero teeth on every lane -- and hid a month-old hang: when the
// DIFFICULTY door moved to Base Camp (9dac53d6), the main-menu-only escape
// tail here stopped being able to leave, and the one caller wedged on the
// first scene. Every give-up now routes through the shared tail in
// tests/test_escape_tail.h with a NUMBERED leg, and
// MenuCapture.a_leg_that_gives_up_frees_the_main_thread is the regression
// for that shape.
//
// Run the film standalone with OG_FX_CAPTURE_DIR=<dir>:
//   ./build/ci-test/og_test_menu_ui --gtest_filter='MenuCapture.zz_capture_*'
// ---------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>

#include "test_click_ladder.h"
#include "test_company_cleanup.h"
#include "test_escape_tail.h"
#include "test_frame_capture.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

// The FX-capture keyboard-nav hook (src/interface/ui/picker_input.cpp,
// TESTING only): one KEY_* direction per pulse, self-clearing to -1 when the
// menu loop consumes it. Real key events cannot drive these screens from an
// injector thread -- the hold-and-release loops eat them mid-press.
extern int g_test_menu_nav_key;

static inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

// Every injector file in this suite carries one of these (pre-existing
// duplication; not this package's to fix).
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

namespace {

// --- ceilings, all cancellation deadlines and never budgets ----------------
//
// Do not raise them to buy time on an instrumented lane: a lane that needs
// more than these has stopped pumping, and the escape tail is what turns
// that into a named red instead of a wedged binary.
constexpr int kMainMenuWaitMs = 5000;
// CONTINUE loads the company and its campaign level on the menu thread.
constexpr int kBaseCampDoorWaitMs = 10000;
constexpr int kDoorWaitMs = 5000;
// The landing deadline for ONE nav step, and its poll tick.
constexpr int kNavLandingCeilingMs = 5000;
constexpr Uint32 kNavPollMs = 20;
// The sabotaged leg waits for an id nothing ever publishes. Short on
// purpose: nothing is coming, and the point of that run is the tail.
constexpr int kSabotageWaitMs = 500;

// A camera hold is a COUNT of presented frames, never a sleep: at the ~75
// presents/s these screens run, 28 frames is ~380 ms on camera and ~9 dumped
// frames (the hook writes every 3rd). A frame count is what the film
// actually needs and is what survives ASan stretch; the flat 380 ms sleep it
// replaces was both a lie about what it waited for and a tier-2 gate failure
// (scripts/check_injector_settles.sh).
constexpr int kCameraHoldFrames = 28;

const char* fx_capture_dir()
{
    const char* const dir = std::getenv("OG_FX_CAPTURE_DIR");
    return (dir != nullptr && dir[0] != '\0') ? dir : nullptr;
}

// Everything the injector observes, recorded here and asserted on the MAIN
// thread after the join: a gtest failure raised on an injector that then
// dies mid-flow takes its own message with it.
struct CaptureState {
    // Set by the MAIN thread after picker_main returns, never by the
    // injector -- it is what tells the escape tail the menus are gone.
    std::atomic<bool> test_finished{false};
    // Point one leg at an id the flow never publishes, so the give-up path
    // itself is testable.
    int sabotage_leg = 0;
    bool filming = false;
    const char* scene = "";
    std::string scene_dir;
    std::string stills_dir;

    int nav_steps_landed = 0;
    int captures = 0;
    std::vector<std::string> nav_misses;
    // One published-id set per capture point, in capture order.
    std::vector<std::set<std::string>> published;

    // menu_difficulty: the DIFFICULTY row's live face, read off the same
    // frame that was captured.
    std::string difficulty_label;

    // menu_tour: the BRIGHTNESS pair's two surfaces.
    int brightness_before = -99;
    int brightness_after_plus = -99;
    int applied_after_plus = -99;
    std::string brightness_label_after_plus;
    int brightness_after_minus = -99;
};

std::string join_misses(const std::vector<std::string>& misses)
{
    std::string joined;
    for (const std::string& miss : misses) {
        if (!joined.empty())
            joined += "; ";
        joined += miss;
    }
    return joined;
}

// The ids the live screen publishes as VISIBLE rows. Holes and gate-hidden
// rows (the difficulty screen's networked-only CTRL row) are not published.
std::set<std::string> published_ids()
{
    std::set<std::string> ids;
    for (const Interactable& item : get_interactables()) {
        if (!item.hidden)
            ids.insert(item.id);
    }
    return ids;
}

std::string set_difference_text(const std::set<std::string>& expected,
                                const std::set<std::string>& actual)
{
    std::string text;
    for (const std::string& id : expected) {
        if (actual.count(id) == 0)
            text += " missing:" + id;
    }
    for (const std::string& id : actual) {
        if (expected.count(id) == 0)
            text += " extra:" + id;
    }
    return text.empty() ? std::string(" (identical)") : text;
}

// --- one keyboard-nav step, proven to LAND -------------------------------
//
// The hook is a plain int the menu loop reads, so the write goes through the
// main-thread pump (it drains at frame top, BEFORE the event poll) and the
// race disappears instead of being slept over. Then wait for THAT frame to
// complete and read two things in one menu-thread task: the hook must have
// self-cleared to -1 (the step was consumed) and the live highlight mirror
// must index the button the nav graph names (the step LANDED). A hook that
// self-clears proves only consumption -- a nav program that moved the
// highlight to the wrong row clears it exactly the same way.
//
// NEVER re-posted: a re-posted nav key is a SECOND step, and the highlight
// would walk one row past the row asked for (the toggle-safety rule the
// click ladder states, applied to the keyboard).
bool nav_step(int key, const char* expected_id, CaptureState* state)
{
    // Taken BEFORE the post, so "one completed frame" cannot be satisfied by
    // a frame that finished before the key was even queued.
    const std::uint64_t target =
        og::ui::menu_screen_testing_completed_frames() + 1;
    if (!run_on_main_thread([key] { g_test_menu_nav_key = key; },
                            kAckPostCeilingMs)) {
        state->nav_misses.push_back(std::string(expected_id) +
                                    ": the menu loop never took the nav key");
        return false;
    }

    const Uint64 deadline =
        SDL_GetTicks() + static_cast<Uint64>(kNavLandingCeilingMs);
    std::string landed = "<never read>";
    int taken = key;
    for (;;) {
        std::string id = "<unread>";
        int hook = key;
        if (!run_on_main_thread(
                [&id, &hook] {
                    hook = g_test_menu_nav_key;
                    AllButtonsLock lock;
                    const int index =
                        og::ui::menu_screen_testing_highlighted_button();
                    id = "<no button at index " + std::to_string(index) + ">";
                    if (index >= 0 && index < MAX_BUTTONS) {
                        const vbutton* const row =
                            og::runtime::current_session
                                ->allbuttons_[static_cast<std::size_t>(index)];
                        if (row != nullptr)
                            id = row->id;
                    }
                },
                kAckPostCeilingMs)) {
            state->nav_misses.push_back(
                std::string(expected_id) +
                ": the menu loop never read the highlight mirror");
            return false;
        }
        landed = id;
        taken = hook;
        if (hook == -1 && id == expected_id &&
            og::ui::menu_screen_testing_completed_frames() >= target) {
            ++state->nav_steps_landed;
            return true;
        }
        if (SDL_GetTicks() >= deadline)
            break;
        SDL_Delay(kNavPollMs);
    }

    state->nav_misses.push_back(std::string(expected_id) + " got " + landed +
                                (taken == -1 ? "" : " (key never consumed)"));
    fprintf(stderr, "  [test] nav miss: wanted '%s', highlight is '%s'\n",
            expected_id, landed.c_str());
    return false;
}

// A camera hold, film only.
void hold_camera(CaptureState* state)
{
    if (state->filming)
        (void)wait_for_menu_frames(kCameraHoldFrames);
}

// Open a door and settle on the screen BEHIND it.
//
// The shared ladder, never the file-local blind re-clicker the capture block
// used to carry: click_until_interactable re-pressed every 250 ms with no
// idea whether the press had landed, and the Base Camp DOOR and the
// submenu's first CYCLER are both spelled `difficulty` -- so one slow
// publish turned a door re-click into a cycler click and moved a setting the
// scene never meant to touch. click_until_edge re-checks the edge
// immediately before every re-press and cancels exactly that.
bool open_door(const std::string& opener, const std::string& edge_id,
               int wait_ms = kDoorWaitMs)
{
    if (!click_until_edge(opener,
                          [&edge_id](int ms) {
                              return wait_for_interactable(edge_id, ms);
                          },
                          /*landed_trace=*/nullptr, /*attempts=*/3, wait_ms))
        return false;
    return wait_for_menu_frames(2);
}

// Freeze the composed frame and record what the screen published on it.
void capture_scene_frame(const char* name, CaptureState* state)
{
    capture_presented_frame(
        name, state->filming ? state->stills_dir.c_str() : nullptr);
    ++state->captures;
    state->published.push_back(published_ids());
}

int escape(CaptureState* state, int leg, const char* why)
{
    return escape_to_the_main_thread(state->test_finished, leg, why);
}

// A leg's own edge id: the sabotage test points ONE leg at a row nothing
// publishes, so the give-up path is exercised by a committed test rather
// than by a defect.
const char* leg_edge(const CaptureState* state, int leg, const char* id)
{
    return state->sabotage_leg == leg ? "never_published_row" : id;
}

int leg_wait(const CaptureState* state, int leg, int wait_ms)
{
    return state->sabotage_leg == leg ? kSabotageWaitMs : wait_ms;
}

// --- the dump hook's arming, scoped --------------------------------------
//
// Armed only while filming, and disarmed by the destructor, so a fatal
// assertion mid-flow can no longer leave the process-global hook writing
// every third frame of every later test into a stale directory.
struct ScopedDumpDir {
    bool armed = false;

    explicit ScopedDumpDir(const std::string& dir)
    {
        std::error_code error;
        std::filesystem::create_directories(dir, error);
        if (error) {
            ADD_FAILURE() << "could not create the film directory " << dir
                          << ": " << error.message();
            return;
        }
        setenv("OG_DUMP_DIR", dir.c_str(), 1);
        armed = true;
    }

    ScopedDumpDir(const ScopedDumpDir&) = delete;
    ScopedDumpDir& operator=(const ScopedDumpDir&) = delete;

    ~ScopedDumpDir()
    {
        if (armed)
            unsetenv("OG_DUMP_DIR");
    }
};

// --- the rows each scene knows about --------------------------------------

struct RowPlan {
    const char* id;
    int clicks;  // one full lap, film only
};

// menu_difficulty, in table order (menu_screen_specs.cpp kDifficultyRows).
constexpr RowPlan kDifficultyRows[] = {
    {"difficulty", 3},      // Battle -> Slaughter -> Skirmish -> Battle
    {"respawn_mode", 4},    // Off -> Heroes -> Everyone -> Team 1 -> Off
    {"respawn_delay", 3},   // Normal -> Fast -> Slow -> Normal
    {"permadeath", 2},      // On -> Off -> On
    {"generator_rate", 3},  // Normal -> Calm -> Frenzy -> Normal
    {"infinite_gold", 2},   // Off -> On -> Off
};

// The value each row STORES, read on the menu thread by the shared value
// ladder. Same readers as Difficulty.submenu_door_flow.
int read_difficulty_row(int row)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    switch (row) {
    case 0:
        return static_cast<int>(og::runtime::current_session->current_difficulty_);
    case 1:
        return static_cast<int>(save.respawn_mode);
    case 2:
        return static_cast<int>(save.ctf_respawn_ticks);
    case 3:
        return static_cast<int>(save.keep_fallen_heroes);
    case 4:
        return static_cast<int>(save.generator_rate);
    default:
        return static_cast<int>(save.infinite_gold);
    }
}

// The published row ids of the DIFFICULTY subscreen in a LOCAL session: the
// networked-only CTRL row is gate-hidden, the other seven are host rows.
const std::set<std::string> kDifficultyScreenIds = {
    "difficulty_back", "difficulty",      "respawn_mode",  "respawn_delay",
    "permadeath",      "generator_rate",  "infinite_gold",
};

// kDisplaySettingsRows, whole. Mode and resolution are hidden only on the
// TV/mobile/web fork (display_settings_platform_rewire); a desktop build
// publishes all nine.
const std::set<std::string> kDisplayScreenIds = {
    "display_back",      "display_mode",   "display_resolution",
    "overscan_minus",    "overscan_plus",  "display_zoom",
    "display_smoothing", "brightness_minus", "brightness_plus",
};

// A film toggle and the cfg flag it writes -- the value the click ladder
// reads to prove the press was CONSUMED (the row's colour is not readable
// from an injector).
struct FxFilmToggle {
    const char* id;
    const char* key;
};

struct FxScreenPlan {
    const char* opener;
    const char* back;
    // The DOWN chain from BACK: the screens' vertical nav walks the LEFT
    // column, not every toggle (kGraphicsFxRows is a 3-wide grid).
    std::vector<const char*> nav_chain;
    // The toggles the film flips twice.
    std::vector<FxFilmToggle> film_toggles;
    std::set<std::string> published;
};

const std::vector<FxScreenPlan>& fx_screen_plans()
{
    static const std::vector<FxScreenPlan> plans = {
        {"gameplay_fx",
         "gameplay_fx_back",
         {"toggle_hit_recoil", "toggle_attack_lunge"},
         {{"toggle_hit_recoil", "hit_recoil"},
          {"toggle_attack_lunge", "attack_lunge"}},
         {"gameplay_fx_back", "toggle_hit_recoil", "toggle_attack_lunge"}},
        {"ui_fx",
         "ui_fx_back",
         {"toggle_mini_hp_bar", "toggle_damage_numbers", "toggle_heal_numbers"},
         {{"toggle_mini_hp_bar", "mini_hp_bar"},
          {"toggle_damage_numbers", "damage_numbers"},
          {"toggle_heal_numbers", "heal_numbers"}},
         {"ui_fx_back", "toggle_mini_hp_bar", "toggle_damage_numbers",
          "toggle_heal_numbers"}},
        {"graphics_fx",
         "graphics_fx_back",
         {"toggle_hit_flash", "toggle_shadows", "toggle_dust",
          "toggle_fire_glow", "toggle_floor_glide"},
         {{"toggle_weather", "weather"},
          {"toggle_shadows", "shadows"},
          {"toggle_fire_glow", "fire_glow"},
          {"toggle_screen_shake", "screen_shake"}},
         {"graphics_fx_back", "toggle_hit_flash", "toggle_hit_sparks",
          "toggle_gore", "toggle_shadows", "toggle_reflections",
          "toggle_weather", "toggle_dust", "depth_fx", "toggle_trails",
          "toggle_fire_glow", "toggle_ripples", "toggle_screen_shake",
          "toggle_floor_glide", "toggle_color_cycling"}},
    };
    return plans;
}

// The cfg keys menu_effects flips on film; the flip-twice discipline must
// leave every one of them exactly as it found it.
constexpr std::array<std::pair<const char*, const char*>, 9> kEffectsCfgKeys = {{
    {"effects", "hit_recoil"},
    {"effects", "attack_lunge"},
    {"effects", "mini_hp_bar"},
    {"effects", "damage_numbers"},
    {"effects", "heal_numbers"},
    {"effects", "weather"},
    {"effects", "shadows"},
    {"effects", "fire_glow"},
    {"effects", "screen_shake"},
}};

// --- the three injectors ---------------------------------------------------

// menu_difficulty: main menu -> CONTINUE -> Base Camp -> the keyboard route
// down the roster and the seat rail onto the DIFFICULTY door -> the
// subscreen, one nav step per row -> back out through the tail.
int menu_difficulty_injector(void* data)
{
    og::runtime::ensure_thread_session();
    CaptureState* const state = static_cast<CaptureState*>(data);

    // -- Leg 1: the main menu is up --
    if (!wait_for_interactable(leg_edge(state, 1, "continue_game"),
                               leg_wait(state, 1, kMainMenuWaitMs)) ||
        !wait_for_menu_frames(2))
        return escape(state, 1, "the main menu never published continue_game");

    // -- Leg 2: CONTINUE opens a Base Camp carrying the DIFFICULTY door --
    if (!open_door("continue_game", leg_edge(state, 2, "difficulty"),
                   leg_wait(state, 2, kBaseCampDoorWaitMs)))
        return escape(state, 2,
                      "CONTINUE never opened a Base Camp with the DIFFICULTY "
                      "door on its strip");

    // The keyboard route to the door, exactly as the Base Camp rewire wires
    // it (menu_screen_specs.cpp): the roster body column drops onto the
    // rail's leftmost live slot (`body_down_exit`/`rail_first`, always
    // seat_card_0), and each rail slot drops onto its own strip door
    // (`card_down[0]` is DIFFICULTY). The company this flow seeds carries
    // exactly one unit, so the roster is one row deep and the route is two
    // DOWN steps whatever else is in the binary.
    (void)nav_step(KEY_DOWN, "seat_card_0", state);
    (void)nav_step(KEY_DOWN, "difficulty", state);

    // -- Leg 3: the door opens the subscreen --
    if (!open_door("difficulty", leg_edge(state, 3, "difficulty_back")))
        return escape(state, 3, "the DIFFICULTY door never opened its screen");

    capture_scene_frame("difficulty_submenu", state);
    (void)run_on_main_thread([state] {
        state->difficulty_label = interactable_label("difficulty");
    });
    hold_camera(state);

    for (int row = 0; row < static_cast<int>(std::size(kDifficultyRows));
         ++row) {
        (void)nav_step(KEY_DOWN, kDifficultyRows[row].id, state);
        if (!state->filming)
            continue;
        click_until_value_moves(kDifficultyRows[row].id,
                                kDifficultyRows[row].clicks,
                                [row] { return read_difficulty_row(row); });
        hold_camera(state);
    }

    // -- Leg 4: BACK lands on a live Base Camp --
    if (!open_door("difficulty_back", "go"))
        return escape(state, 4,
                      "the subscreen's BACK did not return to a live Base "
                      "Camp");
    hold_camera(state);
    return escape(state, 0, "");
}

// menu_tour: the main-menu highlight walk -> GAME SETTINGS -> DISPLAY, where
// BRIGHTNESS is stepped up and back.
int menu_tour_injector(void* data)
{
    og::runtime::ensure_thread_session();
    CaptureState* const state = static_cast<CaptureState*>(data);

    // -- Leg 1: the main menu is up --
    if (!wait_for_interactable(leg_edge(state, 1, "continue_game"),
                               leg_wait(state, 1, kMainMenuWaitMs)) ||
        !wait_for_menu_frames(2))
        return escape(state, 1, "the main menu never published continue_game");

    // The main menu's entry highlight is row 1, CONTINUE
    // (menu_screen_specs.cpp: spec.default_highlight = 1). DOWN walks the
    // centre stack and UP comes back one row.
    (void)nav_step(KEY_DOWN, "level_edit", state);
    (void)nav_step(KEY_DOWN, "options", state);
    (void)nav_step(KEY_DOWN, "cloud", state);
    (void)nav_step(KEY_UP, "options", state);
    hold_camera(state);

    // -- Leg 2: GAME SETTINGS --
    if (!open_door("options", leg_edge(state, 2, "display_settings")))
        return escape(state, 2, "GAME SETTINGS never opened");

    (void)nav_step(KEY_DOWN, "toggle_sound", state);
    (void)nav_step(KEY_RIGHT, "pick_sprite_sheet", state);
    hold_camera(state);

    // -- Leg 3: DISPLAY --
    if (!open_door("display_settings", leg_edge(state, 3, "brightness_plus")))
        return escape(state, 3, "the DISPLAY door never opened its screen");

    (void)nav_step(KEY_DOWN, "display_mode", state);

    (void)run_on_main_thread([state] {
        state->brightness_before =
            og::ui::parse_brightness_steps(cfg.get_setting("graphics", "brightness"));
    });
    click_until_value_moves("brightness_plus", 1, [] {
        return og::ui::parse_brightness_steps(
            cfg.get_setting("graphics", "brightness"));
    });
    (void)run_on_main_thread([state] {
        state->brightness_after_plus =
            og::ui::parse_brightness_steps(cfg.get_setting("graphics", "brightness"));
        // The APPLIED gamma, which is what the DISPLAY content pass draws
        // beside the -/+ pair -- the second surface of the same value.
        state->applied_after_plus = static_cast<int>(display_brightness_steps());
        state->brightness_label_after_plus = og::ui::format_brightness_label(
            static_cast<int>(display_brightness_steps()));
    });

    capture_scene_frame("display_after_plus", state);
    hold_camera(state);

    click_until_value_moves("brightness_minus", 1, [] {
        return og::ui::parse_brightness_steps(
            cfg.get_setting("graphics", "brightness"));
    });
    (void)run_on_main_thread([state] {
        state->brightness_after_minus =
            og::ui::parse_brightness_steps(cfg.get_setting("graphics", "brightness"));
    });
    hold_camera(state);

    // -- Leg 4: back out to a live main menu --
    if (!open_door("display_back", "display_settings"))
        return escape(state, 4, "DISPLAY's BACK did not reach GAME SETTINGS");
    if (!open_door("options_back", "continue_game"))
        return escape(state, 5,
                      "GAME SETTINGS' BACK did not reach the main menu");
    return escape(state, 0, "");
}

// menu_effects: GAME SETTINGS -> the three FX subscreens in turn.
int menu_effects_injector(void* data)
{
    og::runtime::ensure_thread_session();
    CaptureState* const state = static_cast<CaptureState*>(data);

    // -- Leg 1: the main menu is up --
    if (!wait_for_interactable(leg_edge(state, 1, "continue_game"),
                               leg_wait(state, 1, kMainMenuWaitMs)) ||
        !wait_for_menu_frames(2))
        return escape(state, 1, "the main menu never published continue_game");

    // -- Leg 2: GAME SETTINGS --
    if (!open_door("options", leg_edge(state, 2, "gameplay_fx")))
        return escape(state, 2, "GAME SETTINGS never opened");

    int leg = 3;
    for (const FxScreenPlan& plan : fx_screen_plans()) {
        if (!open_door(plan.opener,
                       leg_edge(state, leg, plan.nav_chain.front())))
            return escape(state, leg, "an FX subscreen never opened");
        ++leg;

        for (const char* row : plan.nav_chain)
            (void)nav_step(KEY_DOWN, row, state);

        capture_scene_frame(plan.opener, state);
        hold_camera(state);

        if (state->filming) {
            for (const FxFilmToggle& toggle : plan.film_toggles) {
                // Flip and flip back: the colour change is on camera and the
                // player's settings end where they started. Each click is
                // proven consumed by the cfg flag the row writes, so an even
                // number of LOST clicks can no longer read as a round trip.
                const char* const key = toggle.key;
                for (int pass = 0; pass < 2; ++pass) {
                    click_until_value_moves(toggle.id, 1, [key] {
                        return cfg.is_on("effects", key) ? 1 : 0;
                    });
                    hold_camera(state);
                }
            }
        }

        if (!open_door(plan.back, plan.opener))
            return escape(state, leg, "an FX subscreen's BACK never returned");
    }

    if (!open_door("options_back", "continue_game"))
        return escape(state, 6,
                      "GAME SETTINGS' BACK did not reach the main menu");
    return escape(state, 0, "");
}

// --- the shared flow ------------------------------------------------------

// The company every scene runs on. CONTINUE opens the MOST RECENT company on
// disk, not the one a test happened to write, so seed through the autosave
// choke point that stamps last_played_unix_s -- and pin the roster at ONE
// unit, because the Base Camp keyboard route this scene walks is derived
// from the roster's depth.
void seed_scene_company()
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "SCENE";
    save.team_list[0]->teamnum = 0;
    save.team_size = 1;
    save.my_team = 0;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";
}

// mainmenu_cap: how many capped main-menu passes the flow needs. A complete
// scene spends two -- its own door click is the first, the main menu it
// returns to is the second, and the escape tail's CONTINUE -> BACK then
// meets the cap so present_menu answers Quit. A flow that gives up before
// its second door only needs one, and paying for the second costs the tail
// a whole extra company load.
void run_capture_flow(const char* scene, int (*injector)(void*),
                      CaptureState& state, int& injector_result,
                      int mainmenu_cap = 2)
{
    trace_clear();
    (void)take_captured_frames();  // no other flow's captures in this ledger

    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";
    ASSERT_NO_FATAL_FAILURE(seed_scene_company());

    state.scene = scene;
    const char* const capture_dir = fx_capture_dir();
    state.filming = capture_dir != nullptr;
    if (state.filming) {
        state.scene_dir = std::string(capture_dir) + "/" + scene;
        state.stills_dir = std::string(capture_dir) + "/" + scene + "_stills";
    }

    {
        // Armed only while filming; the dump hook is a process global and
        // must never survive this scope.
        std::unique_ptr<ScopedDumpDir> film;
        if (state.filming)
            film = std::make_unique<ScopedDumpDir>(state.scene_dir);

        SDL_Thread* const thread =
            SDL_CreateThread(injector, "capture_injector", &state);
        ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

        g_picker_mainmenu_calls = 0;
        g_picker_max_mainmenu_calls = mainmenu_cap;

        picker_main(0, nullptr);

        // The tail runs until the MAIN thread says the menus are gone.
        state.test_finished.store(true);
        injector_result = -1;
        SDL_WaitThread(thread, &injector_result);
        escape_tail_join_hygiene();
    }

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(std::getenv("OG_DUMP_DIR") == nullptr)
        << scene << ": the film hook must be disarmed when the flow returns";
    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the flow must have run on the company this test seeded";
}

// The shared always-run oracles: no leg gave up, every nav step landed, and
// every capture point froze a real, inked frame.
void expect_scene_completed(const CaptureState& state, int injector_result,
                            int expected_nav_steps, std::size_t expected_captures)
{
    EXPECT_EQ(0, injector_result)
        << state.scene << ": the injector gave up at leg " << injector_result;
    EXPECT_TRUE(state.nav_misses.empty())
        << state.scene << ": keyboard nav did not land: "
        << join_misses(state.nav_misses);
    EXPECT_EQ(expected_nav_steps, state.nav_steps_landed)
        << state.scene << ": the scene must walk every nav step it names";
    EXPECT_EQ(expected_captures, static_cast<std::size_t>(state.captures))
        << state.scene << ": the flow did not reach every capture point";
}

void expect_published(const CaptureState& state, std::size_t capture_index,
                      const char* what, const std::set<std::string>& expected)
{
    ASSERT_GT(state.published.size(), capture_index)
        << state.scene << ": no published-id snapshot for " << what;
    const std::set<std::string>& actual = state.published[capture_index];
    EXPECT_EQ(expected, actual)
        << state.scene << ": " << what
        << " published a different row set:"
        << set_difference_text(expected, actual);
}

}  // namespace

TEST(MenuCapture, zz_capture_menu_difficulty)
{
    // A NON-default session difficulty, so a row face bound to a constant
    // cannot pass the label check below.
    og::runtime::current_session->current_difficulty_ = 2;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.respawn_mode = 0;
    save.ctf_respawn_ticks = 0;
    save.keep_fallen_heroes = 0;
    save.generator_rate = 0;
    save.infinite_gold = 0;

    CaptureState state;
    int injector_result = -1;
    ASSERT_NO_FATAL_FAILURE(run_capture_flow("menu_difficulty",
                                             menu_difficulty_injector, state,
                                             injector_result));

    // Two Base Camp steps (roster body -> seat rail -> DIFFICULTY) and one
    // per settings row.
    expect_scene_completed(state, injector_result, 8, 1);
    verify_captured_frames("menu_difficulty", 1);
    expect_published(state, 0, "the DIFFICULTY subscreen", kDifficultyScreenIds);
    EXPECT_EQ(og::ui::format_difficulty_label(2), state.difficulty_label)
        << "the DIFFICULTY row's live face must re-derive from the session "
           "difficulty the test set, not from its spec-table default";

    SaveData& after = og::runtime::current_session->myscreen_->save_data;
    if (state.filming) {
        // A full lap on every row: the film flips each setting all the way
        // round and must leave the player's company untouched.
        EXPECT_EQ(2, og::runtime::current_session->current_difficulty_);
        EXPECT_EQ(0, after.respawn_mode);
        EXPECT_EQ(0, after.ctf_respawn_ticks);
        EXPECT_EQ(0, after.keep_fallen_heroes);
        EXPECT_EQ(0, after.generator_rate);
        EXPECT_EQ(0, after.infinite_gold);
    } else {
        // Nothing was clicked: the always-run legs walk and observe only.
        EXPECT_EQ(2, og::runtime::current_session->current_difficulty_)
            << "the always-run scene must not cycle a settings row";
    }
}

TEST(MenuCapture, zz_capture_menu_tour)
{
    // Park BRIGHTNESS in the middle of its clamped range so one step up and
    // one step down are both real travel.
    // No menu loop is running yet, so this write races nothing.
    cfg.apply_setting("graphics", "brightness", "0");
    set_display_brightness_steps(0);

    CaptureState state;
    int injector_result = -1;
    ASSERT_NO_FATAL_FAILURE(run_capture_flow("menu_tour", menu_tour_injector,
                                             state, injector_result));

    // Four on the main menu, two in GAME SETTINGS, one in DISPLAY.
    expect_scene_completed(state, injector_result, 7, 1);
    verify_captured_frames("menu_tour", 1);
    expect_published(state, 0, "the DISPLAY subscreen", kDisplayScreenIds);

    EXPECT_EQ(0, state.brightness_before)
        << "the flow must have read the brightness this test parked";
    EXPECT_EQ(state.brightness_before + 1, state.brightness_after_plus)
        << "brightness_plus must move cfg graphics/brightness exactly one "
           "step";
    EXPECT_EQ(state.brightness_before + 1, state.applied_after_plus)
        << "the APPLIED gamma must follow the stored step -- the DISPLAY "
           "content pass draws this one, not the cfg string";
    EXPECT_EQ(og::ui::format_brightness_label(state.brightness_before + 1),
              state.brightness_label_after_plus)
        << "the live BRIGHTNESS readout must re-derive from the applied step";
    EXPECT_EQ(state.brightness_before, state.brightness_after_minus)
        << "brightness_minus must put the step back where it started";
}

TEST(MenuCapture, zz_capture_menu_effects)
{
    std::array<bool, kEffectsCfgKeys.size()> before{};
    for (std::size_t i = 0; i < kEffectsCfgKeys.size(); ++i)
        before[i] = cfg.is_on(kEffectsCfgKeys[i].first, kEffectsCfgKeys[i].second);

    CaptureState state;
    int injector_result = -1;
    ASSERT_NO_FATAL_FAILURE(run_capture_flow("menu_effects",
                                             menu_effects_injector, state,
                                             injector_result));

    // 2 + 3 + 5 nav steps down the three screens' left columns.
    expect_scene_completed(state, injector_result, 10, 3);
    verify_captured_frames("menu_effects", 3);
    const std::vector<FxScreenPlan>& plans = fx_screen_plans();
    for (std::size_t i = 0; i < plans.size(); ++i)
        expect_published(state, i, plans[i].opener, plans[i].published);

    if (state.filming) {
        for (std::size_t i = 0; i < kEffectsCfgKeys.size(); ++i)
            EXPECT_EQ(before[i], cfg.is_on(kEffectsCfgKeys[i].first,
                                           kEffectsCfgKeys[i].second))
                << kEffectsCfgKeys[i].first << "/" << kEffectsCfgKeys[i].second
                << " must end unchanged (flip-twice discipline)";
    }
}

// The regression for the wedge itself, and the reason every give-up above is
// spelled `return escape(N, ...)`.
//
// Leg 2 is the worst arm in the file: the main thread is already INSIDE Base
// Camp when the leg gives up, so nothing on the main menu can free it. The
// escape tail has to walk BACK -> main menu -> CONTINUE -> BACK until the
// iteration cap makes present_menu answer Quit; only then does picker_main
// return and SDL_WaitThread join.
//
// Replace that `return escape(2, ...)` with `state->finished = true;
// return 0;` -- the shape the capture block carried before PR #292 -- and
// this test hangs to the 420 s CTest ceiling instead of naming the leg.
TEST(MenuCapture, a_leg_that_gives_up_frees_the_main_thread)
{
    CaptureState state;
    state.sabotage_leg = 2;
    int injector_result = -1;
    // One capped pass: leg 2 gives up with the main thread inside Base Camp,
    // so the tail's single BACK is all it takes to reach the cap.
    ASSERT_NO_FATAL_FAILURE(run_capture_flow("menu_difficulty_sabotage",
                                             menu_difficulty_injector, state,
                                             injector_result,
                                             /*mainmenu_cap=*/1));

    EXPECT_EQ(2, injector_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_EQ(0, state.nav_steps_landed)
        << "leg 2 gave up before the Base Camp nav walk began";
    EXPECT_EQ(0, state.captures)
        << "a flow that gave up at leg 2 never reached a capture point";
    EXPECT_TRUE(state.nav_misses.empty())
        << "the sabotaged flow must not have attempted a nav step: "
        << join_misses(state.nav_misses);
}
