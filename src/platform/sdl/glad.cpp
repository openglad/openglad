/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <openglad/core/runtime_trace.h>
#include <openglad/core/version.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/frame_pacing.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/screen_lifecycle.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#ifndef TESTING
#include <emscripten/val.h>
#endif
#endif

#include <openglad/legacy/colors.h>
#include <ctime>
#include <openglad/resources/gparser.h>
#include <string>
#include <cstring>
#include <format>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <openglad/core/util.h>
#include <openglad/interface/input.h>
#include <openglad/resources/io.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/results_screen.h>
#include <openglad/platform/game_context.h>
// theprefs is now a macro defined in view.h (via game_session.h)

namespace
{
inline screen* active_screen()
{
    return og::runtime::current_session->myscreen_;
}

inline options* active_prefs()
{
    return og::runtime::current_session->theprefs_;
}

} // namespace

#ifdef __EMSCRIPTEN__
namespace {
std::unique_ptr<og::runtime::GameSession> g_web_session;
bool g_web_boot_started = false;
std::optional<og::ui::PickerLobbyGameStartConfig> g_web_game_start_config;
char g_web_arg0[] = "/play.html";
char* g_web_argv[] = {g_web_arg0, nullptr};
} // namespace
#endif

static inline Uint32 rng(Uint32 max_exclusive) {
    return ctx().rng->next(max_exclusive);
}

#ifdef OUYA
#include <openglad/legacy/OuyaController.h>
#endif


#ifdef __EMSCRIPTEN__
// Game state machine for Emscripten - allows single main loop to handle all states
enum class GameState {
    Intro,
    Picker,
    Playing,
    Quit
};
static GameState g_game_state = GameState::Intro;
static bool g_state_initialized = false;

#ifndef TESTING
namespace {

EM_JS(void, publish_web_game_state_js, (int state), {
    window.__opengladGameState = state;
});

EM_JS(void, publish_web_numviews_js, (int numviews), {
    window.__opengladNumViews = numviews;
});

EM_JS(int, should_seed_web_test_save_js, (), {
    const hasPlayerCount = Number.isFinite(window.__opengladSeedPlayerCount);
    return (window.__opengladSeedSinglePlayerTeam || hasPlayerCount) ? 1 : 0;
});

EM_JS(int, web_test_seed_player_count_js, (), {
    return Number.isFinite(window.__opengladSeedPlayerCount)
        ? Number(window.__opengladSeedPlayerCount)
        : 1;
});

EM_JS(int, should_skip_web_intro_js, (), {
    return window.__opengladSkipIntroForTests ? 1 : 0;
});

struct AppliedPreloadStartConfig {
    std::int32_t scenario_id = 1;
    std::int32_t difficulty = 1;
    std::string campaign_id = "org.openglad.gladiator";
    std::int32_t numplayers = 1;
    std::int32_t allied_mode = 0;
    std::int32_t my_team = 0;
};

struct AppliedPostLoadRuntimeOverrides {
    std::uint32_t control_entity_id = 0u;
    float control_worldx = 0.0f;
    float control_worldy = 0.0f;
    float camera_topx_float = 0.0f;
    float camera_topy_float = 0.0f;
    std::int32_t camera_topx = 0;
    std::int32_t camera_topy = 0;
    std::int32_t facing = 0;
};

struct AppliedInputHold {
    std::int32_t player_index = 0;
    std::string logical_action = "move_right";
    std::string playwright_key = "d";
};

struct JitterCaptureProfileState {
    bool requested = false;
    bool active = false;
    std::string profile_id;
    AppliedPreloadStartConfig preload_start_config;
    AppliedPostLoadRuntimeOverrides post_load_runtime_overrides;
    std::uint64_t preload_applied_at_ms = 0u;
    std::uint64_t post_load_applied_at_ms = 0u;
    AppliedInputHold input_hold;
};

constexpr std::uint64_t kSinglePlayerRightRunPreloadAppliedAtMs = 6512u;
constexpr std::uint64_t kSinglePlayerRightRunPostLoadAppliedAtMs = 6977u;

struct HorizontalRunCandidate {
    std::int32_t start_x = 0;
    std::int32_t y = 0;
    std::int32_t length = 0;
};

JitterCaptureProfileState& jitter_capture_profile_state()
{
    static JitterCaptureProfileState state;
    return state;
}

double js_number_from_u64(std::uint64_t value)
{
    return static_cast<double>(value);
}

std::string playwright_key_for_sdl_keycode(SDL_Keycode keycode)
{
    if (keycode >= SDLK_a && keycode <= SDLK_z)
    {
        const char value = static_cast<char>(
            'a' + static_cast<int>(keycode - SDLK_a));
        return std::string(1, value);
    }
    if (keycode >= SDLK_0 && keycode <= SDLK_9)
    {
        const char value = static_cast<char>(
            '0' + static_cast<int>(keycode - SDLK_0));
        return std::string(1, value);
    }

    switch (keycode)
    {
    case SDLK_LEFT:
        return "ArrowLeft";
    case SDLK_RIGHT:
        return "ArrowRight";
    case SDLK_UP:
        return "ArrowUp";
    case SDLK_DOWN:
        return "ArrowDown";
    case SDLK_SPACE:
        return "Space";
    case SDLK_RETURN:
        return "Enter";
    case SDLK_ESCAPE:
        return "Escape";
    case SDLK_TAB:
        return "Tab";
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        return "Shift";
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        return "Control";
    case SDLK_LALT:
    case SDLK_RALT:
        return "Alt";
    default:
        break;
    }

    return std::string(SDL_GetKeyName(keycode));
}

std::int32_t absolute_distance(std::int32_t lhs, std::int32_t rhs)
{
    return lhs >= rhs ? lhs - rhs : rhs - lhs;
}

bool is_better_horizontal_run(
    const HorizontalRunCandidate& candidate,
    const HorizontalRunCandidate& best,
    std::int32_t target_y,
    std::int32_t desired_length)
{
    if (candidate.length <= 0)
        return false;
    if (best.length <= 0)
        return true;

    const bool candidate_meets_target = candidate.length >= desired_length;
    const bool best_meets_target = best.length >= desired_length;
    if (candidate_meets_target != best_meets_target)
        return candidate_meets_target;

    const std::int32_t candidate_y_distance =
        absolute_distance(candidate.y, target_y);
    const std::int32_t best_y_distance = absolute_distance(best.y, target_y);
    if (candidate_meets_target && candidate_y_distance != best_y_distance)
        return candidate_y_distance < best_y_distance;

    if (candidate.length != best.length)
        return candidate.length > best.length;

    return candidate_y_distance < best_y_distance;
}

std::optional<HorizontalRunCandidate> find_single_player_right_run(
    screen& current_screen,
    walker& control)
{
    constexpr std::int32_t kDesiredRunWorldUnits = 1280;
    const std::int32_t world_width =
        current_screen.world().grid.w * GRID_SIZE;
    const std::int32_t world_height =
        current_screen.world().grid.h * GRID_SIZE;
    if (world_width <= GRID_SIZE * 2 || world_height <= GRID_SIZE * 2)
        return std::nullopt;

    HorizontalRunCandidate best;
    const std::int32_t target_y =
        static_cast<std::int32_t>(control.worldy());
    const std::int32_t min_x = GRID_SIZE;
    const std::int32_t max_x =
        std::max<std::int32_t>(min_x, world_width - control.sizex() - GRID_SIZE);
    const std::int32_t min_y = GRID_SIZE;
    const std::int32_t max_y =
        std::max<std::int32_t>(min_y, world_height - control.sizey() - GRID_SIZE);

    for (std::int32_t y = min_y; y <= max_y; y += GRID_SIZE)
    {
        std::int32_t run_start = -1;
        for (std::int32_t x = min_x; x <= max_x + GRID_SIZE; x += GRID_SIZE)
        {
            const bool in_bounds = x <= max_x;
            const bool passable =
                in_bounds &&
                current_screen.level_runtime_data().query_passable(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    &control);
            if (passable)
            {
                if (run_start < 0)
                    run_start = x;
                continue;
            }
            if (run_start < 0)
                continue;

            HorizontalRunCandidate candidate;
            candidate.start_x = run_start;
            candidate.y = y;
            candidate.length = x - run_start;
            if (is_better_horizontal_run(
                    candidate, best, target_y, kDesiredRunWorldUnits))
            {
                best = candidate;
            }
            run_start = -1;
        }
    }

    if (best.length <= 0)
        return std::nullopt;
    return best;
}

bool apply_single_player_right_run_overrides(
    screen& current_screen,
    viewscreen& primary_view,
    walker& control,
    AppliedPostLoadRuntimeOverrides& overrides)
{
    const std::optional<HorizontalRunCandidate> run =
        find_single_player_right_run(current_screen, control);
    if (!run.has_value())
        return false;

    const std::int32_t safety_margin = GRID_SIZE * 2;
    const std::int32_t max_start_x = run->start_x + run->length - safety_margin;
    const std::int32_t chosen_x =
        max_start_x > run->start_x + safety_margin
            ? run->start_x + safety_margin
            : run->start_x;
    const std::int32_t chosen_y = run->y;

    if (!control.setxy(chosen_x, chosen_y))
        return false;

    control.set_curdir(FACE_RIGHT);
    const float camera_topx_float =
        control.worldx() -
        static_cast<float>(primary_view.xview - control.sizex()) / 2.0f;
    const float camera_topy_float =
        control.worldy() -
        static_cast<float>(primary_view.yview - control.sizey()) / 2.0f;
    primary_view.topx = static_cast<Sint32>(camera_topx_float);
    primary_view.topy = static_cast<Sint32>(camera_topy_float);
    current_screen.level_visuals_.topx = primary_view.topx;
    current_screen.level_visuals_.topy = primary_view.topy;

    overrides.control_entity_id = control.entity_id();
    overrides.control_worldx = control.worldx();
    overrides.control_worldy = control.worldy();
    overrides.camera_topx_float = camera_topx_float;
    overrides.camera_topy_float = camera_topy_float;
    overrides.camera_topx = primary_view.topx;
    overrides.camera_topy = primary_view.topy;
    overrides.facing = control.curdir();
    return true;
}

void initialize_web_jitter_capture_globals()
{
    using emscripten::val;
    val window = val::global("window");
    window.set("__opengladRuntimeTraceEvents", val::array());
    window.set("__opengladLatestRenderSample", val::null());
    window.set("__opengladAppliedJitterCaptureProfile", val::null());
}

void publish_web_runtime_trace_event(
    const og::runtime::RuntimeTraceRecord& record)
{
    using emscripten::val;
    val window = val::global("window");
    val event = val::object();
    event.set("trace_seq", val(js_number_from_u64(record.trace_seq)));
    event.set("engine_time_ms", val(js_number_from_u64(record.engine_time_ms)));
    event.set("browser_time_ms", val::global("performance").call<double>("now"));
    event.set("category", val(record.category));
    event.set("event", val(record.event));
    event.set("tick", val(record.tick));
    event.set("snapshot_kind", val(record.snapshot_kind));
    event.set("interpolation_alpha", val(record.interpolation_alpha));
    event.set("control_worldx", val(record.control_worldx));
    event.set("control_worldy", val(record.control_worldy));
    event.set("control_render_x", val(record.control_render_x));
    event.set("control_render_y", val(record.control_render_y));
    event.set("camera_topx", val(record.camera_topx));
    event.set("camera_topy", val(record.camera_topy));
    event.set("camera_topx_float", val(record.camera_topx_float));
    event.set("camera_topy_float", val(record.camera_topy_float));
    window["__opengladRuntimeTraceEvents"].call<void>("push", event);
}

void publish_web_render_sample(
    const og::runtime::RuntimeRenderSample& sample)
{
    using emscripten::val;
    val window = val::global("window");
    val object = val::object();
    object.set("contract_version", val(sample.contract_version));
    object.set("render_sample_seq",
               val(js_number_from_u64(sample.render_sample_seq)));
    object.set("engine_time_ms", val(js_number_from_u64(sample.engine_time_ms)));
    object.set("view_index", val(sample.view_index));
    object.set("tick", val(sample.tick));
    object.set("timer_wait", val(sample.timer_wait));
    object.set("speed_factor", val(sample.speed_factor));
    object.set("interpolation_alpha", val(sample.interpolation_alpha));
    object.set("control_worldx", val(sample.control_worldx));
    object.set("control_worldy", val(sample.control_worldy));
    object.set("control_render_x", val(sample.control_render_x));
    object.set("control_render_y", val(sample.control_render_y));
    object.set("camera_topx", val(sample.camera_topx));
    object.set("camera_topy", val(sample.camera_topy));
    object.set("camera_topx_float", val(sample.camera_topx_float));
    object.set("camera_topy_float", val(sample.camera_topy_float));
    window.set("__opengladLatestRenderSample", object);
}

void publish_applied_jitter_capture_profile()
{
    using emscripten::val;
    const JitterCaptureProfileState& state = jitter_capture_profile_state();
    if (!state.active)
        return;

    val window = val::global("window");
    val object = val::object();
    object.set("contract_version", val(og::runtime::kRuntimeTraceContractVersion));
    object.set("profile_id", val(state.profile_id));

    val preload = val::object();
    preload.set("scenario_id", val(state.preload_start_config.scenario_id));
    preload.set("difficulty", val(state.preload_start_config.difficulty));
    preload.set("campaign_id", val(state.preload_start_config.campaign_id));
    preload.set("numplayers", val(state.preload_start_config.numplayers));
    preload.set("allied_mode", val(state.preload_start_config.allied_mode));
    preload.set("my_team", val(state.preload_start_config.my_team));
    object.set("preload_start_config", preload);

    val overrides = val::object();
    overrides.set("control_entity_id",
                  val(state.post_load_runtime_overrides.control_entity_id));
    overrides.set("control_worldx",
                  val(state.post_load_runtime_overrides.control_worldx));
    overrides.set("control_worldy",
                  val(state.post_load_runtime_overrides.control_worldy));
    overrides.set("camera_topx",
                  val(state.post_load_runtime_overrides.camera_topx));
    overrides.set("camera_topy",
                  val(state.post_load_runtime_overrides.camera_topy));
    overrides.set("camera_topx_float",
                  val(state.post_load_runtime_overrides.camera_topx_float));
    overrides.set("camera_topy_float",
                  val(state.post_load_runtime_overrides.camera_topy_float));
    overrides.set("facing", val(state.post_load_runtime_overrides.facing));
    object.set("post_load_runtime_overrides", overrides);

    object.set("preload_applied_at_ms",
               val(js_number_from_u64(state.preload_applied_at_ms)));
    object.set("post_load_applied_at_ms",
               val(js_number_from_u64(state.post_load_applied_at_ms)));

    val input_hold = val::object();
    input_hold.set("player_index", val(state.input_hold.player_index));
    input_hold.set("logical_action", val(state.input_hold.logical_action));
    input_hold.set("playwright_key", val(state.input_hold.playwright_key));
    object.set("input_hold", input_hold);

    window.set("__opengladAppliedJitterCaptureProfile", object);
}

void refresh_web_jitter_capture_request()
{
    using emscripten::val;
    JitterCaptureProfileState& state = jitter_capture_profile_state();
    state.requested = false;
    state.active = false;
    state.profile_id.clear();
    state.preload_applied_at_ms = 0u;
    state.post_load_applied_at_ms = 0u;

    val window = val::global("window");
    val requested = window["__opengladJitterCaptureProfile"];
    if (requested.isNull() || requested.isUndefined())
        return;

    const val profile_id = requested["profile_id"];
    if (profile_id.isUndefined() || profile_id.isNull())
        return;

    state.profile_id = profile_id.as<std::string>();
    state.requested = state.profile_id == "single-player-right-run";
}

bool web_runtime_trace_capture_requested()
{
    using emscripten::val;
    val requested = val::global("window")["__opengladEnableRuntimeTraceCapture"];
    return !(requested.isNull() || requested.isUndefined()) && requested.as<bool>();
}

void publish_web_game_state()
{
    publish_web_game_state_js(static_cast<int>(g_game_state));
    const screen* const current_screen = active_screen();
    publish_web_numviews_js(current_screen != nullptr ? current_screen->numviews : 0);
}

void initialize_web_runtime_trace_bridge()
{
    initialize_web_jitter_capture_globals();
    og::runtime::set_runtime_render_sample_observer(
        [](const og::runtime::RuntimeRenderSample& sample) {
            publish_web_render_sample(sample);
        });

    if (og::runtime::runtime_trace_enabled())
    {
        og::runtime::set_runtime_trace_observer(
            [](const og::runtime::RuntimeTraceRecord& record) {
                publish_web_runtime_trace_event(record);
            });
    }
    else
    {
        og::runtime::clear_runtime_trace_observer();
    }
}

void maybe_apply_web_jitter_capture_start_config(
    og::ui::PickerLobbyGameStartConfig& config)
{
    JitterCaptureProfileState& state = jitter_capture_profile_state();
    if (!state.requested || state.profile_id != "single-player-right-run")
        return;

    state.active = true;
    state.preload_start_config.scenario_id = 11;
    state.preload_start_config.difficulty = 1;
    state.preload_start_config.campaign_id = "org.openglad.gladiator";
    state.preload_start_config.numplayers = 1;
    state.preload_start_config.allied_mode = 0;
    state.preload_start_config.my_team = 0;

    config.save_data.current_campaign = state.preload_start_config.campaign_id;
    config.save_data.scen_num =
        static_cast<std::int16_t>(state.preload_start_config.scenario_id);
    config.save_data.numplayers =
        static_cast<std::uint8_t>(state.preload_start_config.numplayers);
    config.save_data.allied_mode =
        static_cast<std::uint8_t>(state.preload_start_config.allied_mode);
    config.difficulty =
        static_cast<std::int16_t>(state.preload_start_config.difficulty);
    config.my_team = static_cast<std::int16_t>(state.preload_start_config.my_team);
    state.preload_applied_at_ms = kSinglePlayerRightRunPreloadAppliedAtMs;
}

void seed_web_test_save_if_requested()
{
    if (!should_seed_web_test_save_js())
        return;

    screen* const current_screen = active_screen();
    if (current_screen == nullptr)
        return;

    SaveData& save = current_screen->save_data;
    const int requested_player_count =
        std::clamp(web_test_seed_player_count_js(), 1, MAX_PLAYERS);
    save.reset();
    save.numplayers = static_cast<unsigned char>(requested_player_count);
    save.current_campaign = "org.openglad.gladiator";
    save.current_levels[save.current_campaign] = 1;
    save.scen_num = 1;

    for (auto& member : save.team_list)
        member.reset();

    for (std::size_t index = 0;
         index < static_cast<std::size_t>(requested_player_count);
         ++index)
    {
        auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
        soldier->name = requested_player_count == 1
            ? "Web Soldier"
            : std::format("Web Soldier {}", index + 1);
        soldier->teamnum = static_cast<short>(index);
        soldier->strength = 200;
        soldier->dexterity = 200;
        soldier->constitution = 200;
        soldier->intelligence = 200;
        soldier->armor = 200;
        save.team_list[index] = std::move(soldier);
    }
    save.team_size = static_cast<unsigned char>(requested_player_count);
    save.save("save0");
}

} // namespace
#endif
#endif


#ifdef TESTING
// Defined in src/runtime/glad_gameplay.cpp (linked into test binaries via og_game_test).
extern bool g_test_remove_exits;
#endif

// Z's script: #include <process.h>

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);

void picker_main(Sint32 argc, char **argv);
void intro_main(Sint32 argc, char **argv);

short remaining_foes(screen *scr, walker* myguy);
short remaining_team(screen *scr, char myteam);
short score_panel(screen *scr);
short score_panel(screen *scr, short do_it);
short new_score_panel(screen *s, short /*do_it*/);
void draw_value_bar(short left, short top, walker * control, short mode, screen * scr);
void new_draw_value_bar(Sint32 left, Sint32 top,
                        walker  * control, short mode, screen * scr);
void draw_percentage_bar(Sint32 left, Sint32 top, unsigned char somecolor,
                         short somelength, screen * scr);
void init_input();

void draw_radar_gems(screen  *scr);
void draw_gem(short x, short y, short color, screen * scr);

void glad_main(screen *scr, Sint32 playermode);

// Zardus: FIX: from view.cpp. We need this here so that it doesn't
// try to create it before main and go nuts trying to load it
// theprefs is now a macro defined in view.h (via game_session.h)

// Frame state lives in GameSession::frame_state_
static inline GameLoopFrameState& g_frame_state() {
    return og::runtime::current_session->frame_state_;
}

short gameplay_numviews_for_start(
    const screen& current_screen,
    const og::ui::PickerLobbyGameStartConfig* lobby_config)
{
    const std::uint8_t player_count = lobby_config != nullptr
        ? lobby_config->save_data.numplayers
        : current_screen.save_data.numplayers;
    return static_cast<short>(player_count == 0 ? 1 : player_count);
}

void ready_screen_for_game_start(
    screen& current_screen,
    const og::ui::PickerLobbyGameStartConfig* lobby_config)
{
    current_screen.ready_for_battle(
        gameplay_numviews_for_start(current_screen, lobby_config));
}

// Forward declarations
void glad_init(bool preserve_frame_timing = false);
void glad_init(bool preserve_frame_timing,
               const og::ui::PickerLobbyGameStartConfig* lobby_config);

#ifndef TESTING
namespace {
og::runtime::GameSession::Config default_session_config()
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.numviews = 1;
    session_cfg.allocate_screen = true;
    session_cfg.allocate_prefs = true;
    session_cfg.install_legacy_globals = true;
    return session_cfg;
}

void initialize_runtime_config(int argc, char* argv[])
{
    init_logging();  // Set up logging output (uses JS console on web)
    io_init(argc, argv);

    cfg.load_settings();
    cfg.commandline(argc, argv);
}

void bootstrap_runtime(int argc, char* argv[])
{
#ifdef OUYA
    OuyaControllerManager::init();
#endif

    //buffers: setting the seed
    srand(static_cast<unsigned int>(time(nullptr)));

    init_input();
    load_player_control_settings_from_cfg(cfg);
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();

    // Sync overscan from config (must be after session creation since
    // overscan_percentage is now a macro backed by current_session).
    og::runtime::current_session->overscan_percentage_ = static_cast<float>(
        parse_int_strict(cfg.get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
    update_overscan_setting();
    cfg.apply_setting("graphics", "overscan_percentage",
        std::format("{:.0f}", 100 * og::runtime::current_session->overscan_percentage_));
#if defined(__EMSCRIPTEN__) && !defined(TESTING)
    if (should_skip_web_intro_js())
        return;
#endif
    intro_main(argc, argv);
}
} // namespace

#ifdef __EMSCRIPTEN__
void openglad_web_finalize_jitter_capture_profile_after_load(screen& current_screen)
{
    JitterCaptureProfileState& state = jitter_capture_profile_state();
    if (!state.active || state.profile_id != "single-player-right-run")
        return;

    viewscreen* const primary_view =
        current_screen.viewob[0] != nullptr ? current_screen.viewob[0].get() : nullptr;
    walker* const control = primary_view != nullptr ? primary_view->control : nullptr;

    if (primary_view != nullptr && control != nullptr)
    {
        if (!apply_single_player_right_run_overrides(
                current_screen,
                *primary_view,
                *control,
                state.post_load_runtime_overrides))
        {
            const float camera_topx_float =
                control->worldx() -
                static_cast<float>(primary_view->xview - control->sizex()) /
                    2.0f;
            const float camera_topy_float =
                control->worldy() -
                static_cast<float>(primary_view->yview - control->sizey()) /
                    2.0f;
            primary_view->topx = static_cast<Sint32>(camera_topx_float);
            primary_view->topy = static_cast<Sint32>(camera_topy_float);
            current_screen.level_visuals_.topx = primary_view->topx;
            current_screen.level_visuals_.topy = primary_view->topy;

            state.post_load_runtime_overrides.control_entity_id =
                control->entity_id();
            state.post_load_runtime_overrides.control_worldx = control->worldx();
            state.post_load_runtime_overrides.control_worldy = control->worldy();
            state.post_load_runtime_overrides.camera_topx_float =
                camera_topx_float;
            state.post_load_runtime_overrides.camera_topy_float =
                camera_topy_float;
            state.post_load_runtime_overrides.camera_topx = primary_view->topx;
            state.post_load_runtime_overrides.camera_topy = primary_view->topy;
            state.post_load_runtime_overrides.facing = control->curdir();
        }
    }

    state.input_hold.playwright_key = playwright_key_for_sdl_keycode(
        og::runtime::current_session->player_keys_[0][KEY_RIGHT]);
    state.post_load_applied_at_ms = kSinglePlayerRightRunPostLoadAppliedAtMs;
    publish_applied_jitter_capture_profile();
}
#endif
#endif

#ifdef __EMSCRIPTEN__
// Forward declarations for state handlers
void picker_init();
bool picker_frame();  // Returns true when should transition to game
bool picker_check_start_requested();
void picker_cleanup_for_game();
void picker_reinit_after_game();

// Emscripten frame wrapper with timing control
// The browser calls this at ~60 FPS via requestAnimationFrame
// The wrapper keeps gameplay on the browser cadence helper while letting
// game_frame() own the normal gameplay render path.
static void emscripten_frame_wrapper() {
	screen* current_screen = active_screen();
	switch (g_game_state) {
		case GameState::Picker:
			og::runtime::current_session->gameplay_active_ = false;
			if (!g_state_initialized) {
				picker_reinit_after_game();
				g_state_initialized = true;
			}
			if (picker_frame()) {
				// Transition to playing state
				Log("Transitioning from PICKER to PLAYING\n");
				g_web_game_start_config = picker_lobby_consume_game_start_config();
                    if (g_web_game_start_config.has_value())
                        maybe_apply_web_jitter_capture_start_config(
                            *g_web_game_start_config);
				picker_cleanup_for_game();
				g_game_state = GameState::Playing;
				g_state_initialized = false;
			}
			break;

		case GameState::Playing:
			if (!g_state_initialized) {
				// Initialize game state
				Log("GameState::Playing: Initializing game\n");
				release_mouse();
				if(current_screen == nullptr)
				{
					LogError("game_state_init_failed state=playing reason=missing_screen\n");
					og::runtime::current_session->gameplay_active_ = false;
					g_game_state = GameState::Quit;
					break;
				}
                    og::runtime::reset_runtime_trace_capture_state();
                    initialize_web_jitter_capture_globals();
					{
                    if (g_web_game_start_config.has_value())
                        maybe_apply_web_jitter_capture_start_config(
                            *g_web_game_start_config);
					ready_screen_for_game_start(
					    *current_screen,
					    g_web_game_start_config.has_value()
					        ? &*g_web_game_start_config
					        : nullptr);
					}
				og::runtime::current_session->gameplay_active_ = true;
				glad_init(
				    true,
				    g_web_game_start_config.has_value()
				        ? &*g_web_game_start_config
				        : nullptr);
				g_web_game_start_config.reset();
				g_frame_state().done = false;
				g_frame_state().initialized = false;
				g_frame_state().currentcycle = 0;
				g_frame_state().cycletime = 3;
				g_frame_state().last_frame_time = 0;
				g_frame_state().accumulated_time = 0;
				g_state_initialized = true;
			}
			if (current_screen == nullptr || g_frame_state().done)
				break;
			{
				const Uint32 current_time = SDL_GetTicks();
				const short timer_wait = current_screen->world().timer_wait;
				const float speed_factor =
					og::runtime::current_session->g_game_speed_factor_;
				og::runtime::BrowserFramePacingResult pacing;
				if (!g_frame_state().initialized) {
					pacing.target_interval_ms =
						og::runtime::browser_frame_target_interval_ms(
							timer_wait, speed_factor);
					pacing.should_run_frame = true;
				} else {
					const Uint32 delta =
						current_time - g_frame_state().last_frame_time;
					pacing = og::runtime::step_browser_frame_pacing(
						g_frame_state().accumulated_time,
						delta,
						timer_wait,
						speed_factor);
				}

				og::runtime::emit_runtime_trace(
					og::runtime::make_runtime_trace_record(
						"browser_pacing",
						pacing.should_run_frame ? "wrapper_frame_ready"
						                        : "wrapper_frame_skip"));

				{
					GameLoopFrameState render_state = g_frame_state();
					render_state.initialized = true;
					render_state.last_frame_time = current_time;
					render_state.accumulated_time = 0;

					GameLoopDeps render_deps;
					render_deps.enable_render = true;
					game_frame(*current_screen, render_state, render_deps);
					g_frame_state().done = render_state.done;
					g_frame_state().has_pending_input =
						render_state.has_pending_input;
					g_frame_state().pending_input =
						render_state.pending_input;
				}

				if (!g_frame_state().done && pacing.should_run_frame) {
					GameLoopDeps tick_deps;
					tick_deps.enable_render = false;
					tick_deps.enable_event_poll = false;
					tick_deps.enable_frame_timing = false;
					game_frame(*current_screen, g_frame_state(), tick_deps);
				}

				g_frame_state().initialized = true;
				g_frame_state().last_frame_time = current_time;
				g_frame_state().accumulated_time =
					pacing.accumulated_after_step_ms;
			}
			if (g_frame_state().done) {
				Log("Game done, transitioning back to PICKER\n");
				og::runtime::current_session->gameplay_active_ = false;
				if (og::runtime::current_game_session != nullptr)
					og::runtime::clear_local_transport_shadow(
					    *og::runtime::current_game_session);
				clear_keyboard();
				current_screen->world().delete_objects();
				g_web_game_start_config.reset();
				g_game_state = GameState::Picker;
				g_state_initialized = false;
			}
			break;

		case GameState::Quit:
			og::runtime::current_session->gameplay_active_ = false;
			emscripten_cancel_main_loop();
			break;

		default:
			break;
	}
#ifndef TESTING
	publish_web_game_state();
#endif
}
#endif

#ifndef TESTING
#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void openglad_web_boot()
{
    if (g_web_boot_started) {
        return;
    }

    g_web_boot_started = true;

    try
    {
        refresh_web_jitter_capture_request();
        og::runtime::set_runtime_trace_enabled(
            web_runtime_trace_capture_requested());
        og::runtime::reset_runtime_trace_capture_state();
        initialize_web_runtime_trace_bridge();

        initialize_runtime_config(1, g_web_argv);
        g_web_session = std::make_unique<og::runtime::GameSession>(default_session_config());
        bootstrap_runtime(1, g_web_argv);
        seed_web_test_save_if_requested();

        // For Emscripten, initialize picker and start the unified main loop
        picker_init();
        Log("openglad_web_boot: After picker_init, about to check if game should start\n");

        // Check if picker_init resulted in a game start request
        if (picker_check_start_requested()) {
            Log("openglad_web_boot: Game start was requested during picker_init, starting in PLAYING state\n");
            g_web_game_start_config = picker_lobby_consume_game_start_config();
            if (g_web_game_start_config.has_value())
                maybe_apply_web_jitter_capture_start_config(
                    *g_web_game_start_config);
            g_game_state = GameState::Playing;
            g_state_initialized = false;  // Will trigger glad_init on first frame
        } else {
            Log("openglad_web_boot: No game start requested, starting in PICKER state\n");
            g_web_game_start_config.reset();
            g_game_state = GameState::Picker;
            g_state_initialized = true;
        }

        // Initialize timing
        g_frame_state().last_frame_time = SDL_GetTicks();
        g_frame_state().accumulated_time = 0;
        publish_web_game_state();

        // Browser startup uses an explicit JS-triggered bootstrap, so let the
        // boot call return after scheduling the frame loop.
        emscripten_set_main_loop(emscripten_frame_wrapper, 0, 0);
    }
    catch (const std::runtime_error& e)
    {
        g_web_boot_started = false;
        g_web_session.reset();
        publish_web_game_state();
        LogError("Unrecoverable error: {}\n", e.what());
    }
}
#endif

int main(int argc, char *argv[])
{
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
    openglad_web_boot();
    return 0;
#else
	try
	{
		initialize_runtime_config(argc, argv);

		// GameSession is the RAII root for all runtime state.
		// It owns the screen, prefs, and installs legacy global shims.
		// Must be created before any macro-backed globals are accessed
		// (overscan_percentage, player_keys, keystates, etc.).
		og::runtime::GameSession session(default_session_config());
		bootstrap_runtime(argc, argv);

		// Native build: use traditional blocking calls
		picker_main(argc, argv);
		text_shutdown();
		io_exit();

	// Session destructor restores previous globals and frees owned resources.
	}
	catch (const std::runtime_error& e)
	{
		LogError("Unrecoverable error: {}\n", e.what());
		return 1;
	}

	return 0;
#endif
}
#endif // TESTING

void draw_radar_gems(screen* s)
{
	short upper_left_x = 246;
	short upper_left_y = 140;
	short upper_right_x = upper_left_x + 65;
	short upper_right_y = upper_left_y;
	short lower_left_x = upper_left_x;
	short lower_left_y = upper_left_y + 49;
	short lower_right_x = upper_right_x;
	short lower_right_y = lower_left_y;

	short team_light;

	static short old_team_num = -1;
	if (old_team_num == s->viewob[0]->control->team_num())
		return;
	old_team_num = s->viewob[0]->control->team_num();

	team_light = s->viewob[0]->control->query_team_color();

	draw_gem(upper_left_x, upper_left_y, team_light, s);
	draw_gem(upper_right_x, upper_right_y, team_light, s);
	draw_gem(lower_left_x, lower_left_y, team_light, s);
	draw_gem(lower_right_x, lower_right_y, team_light, s);
}

void draw_gem(short x, short y, short color, screen* s)
{
	const unsigned char light = static_cast<unsigned char>(color);
	const unsigned char med = static_cast<unsigned char>(light + 2);
	const unsigned char darker = static_cast<unsigned char>(med + 2);
	const unsigned char darkest = static_cast<unsigned char>(darker + 2);

	s->point(x, y, light);
	s->point(x-1, y+1, light);
	s->point(x, y+1, med);
	s->point(x+1, y+1, darker);
	s->point(x-2, y+2, light);
	s->hor_line(x-1, y+2, 3, med);
	s->point(x+2, y+2, darkest);
	s->point(x-1, y+3, darker);
	s->point(x, y+3, med);
	s->point(x+1, y+3, darkest);
	s->point(x, y+4, darkest);
}

bool float_eq(float a, float b);

void draw_value_bar(short left, short top,
                    walker  * control, short mode, screen * s)
{
	float points;
	Sint32 bar_length = 0;
	Sint32 bar_remainder = 60 - bar_length;
	Sint32 i = 0;
	Sint32 j = 0;
	unsigned char whatcolor = 0;

	if (mode == 0) // hitpoint bar
	{
		points = control->stats()->hitpoints();

		if (float_eq(points, control->stats()->max_hitpoints()))
			whatcolor = MAX_HP_COLOR;
		else if ( (points * 3) < control->stats()->max_hitpoints())
			whatcolor = LOW_HP_COLOR;
		else if ( (points * 3 / 2) < control->stats()->max_hitpoints())
			whatcolor = MID_HP_COLOR;
		else if (points < control->stats()->max_hitpoints())
			whatcolor = HIGH_HP_COLOR;
			else 
				whatcolor = ORANGE_START;

			if (points > control->stats()->max_hitpoints())
				bar_length = 60;
			else
				bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_hitpoints()));
			bar_remainder = 60 - bar_length;

			s->draw_box(left, top, left+61, top+6, BOX_COLOR, 0);
			//myscreen->fastbox(left, top, 61, 6, BOX_COLOR, 1);
			if ( points > control->stats()->max_hitpoints())
				for (i=0;i<bar_length/2;i++)
					for (j=0; j<3; j++)
					{
						const unsigned char col = static_cast<unsigned char>(whatcolor + static_cast<unsigned char>((i + j) % 16));
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2-j), 1, col);
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2+j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2-j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2+j), 1, col);
					}
			else
				s->fastbox(left+1, top+1, bar_length, 5, whatcolor);
			s->fastbox(left+1+bar_length, top+1, bar_remainder, 5, BAR_BACK_COLOR);

		//This part rounds the corners (via 4 masks)

			for (i=0;i<4;i++)
			{
				//upper left
				s->ver_line(left+i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top, 2-i, 27);
				//upper right
				s->ver_line(left+61-i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top, 2-i, 27);
				//lower left
				s->ver_line(left+i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top+5+i, 2-i, 27);
				//lower right
				s->ver_line(left+61-i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top+5+i, 2-i, 27);
			}
	}  // end of doing hp stuff..
	else if (mode == 1) // sp stuff ..
	{
		points = control->stats()->magicpoints();

		if (float_eq(points, control->stats()->max_magicpoints()))
			whatcolor = MAX_MP_COLOR;
		else if ( (points * 3) < control->stats()->max_magicpoints())
			whatcolor = LOW_MP_COLOR;
		else if ( (points * 3 / 2) < control->stats()->max_magicpoints())
			whatcolor = MID_MP_COLOR;
		else if (points < control->stats()->max_magicpoints())
			whatcolor = HIGH_MP_COLOR;
			else 
				whatcolor = WATER_START;

			if (points > control->stats()->max_magicpoints())
				bar_length = 60;
			else
				bar_length = static_cast<Sint32>(ceilf(points * 60.0f / control->stats()->max_magicpoints()));
			bar_remainder = 60 - bar_length;

			s->draw_box(left, top, left+61, top+6, BOX_COLOR, 0);
			if ( points > control->stats()->max_magicpoints())
				for (i=0;i<bar_length/2;i++)
					for (j=0; j<3; j++)
					{
						const unsigned char col = static_cast<unsigned char>(whatcolor + static_cast<unsigned char>((i + j) % 16));
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2-j), 1, col);
						s->ver_line(left+1+(bar_length/2)-i-1, top+1+(2+j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2-j), 1, col);
						s->ver_line(left+1+i+(bar_length/2), top+1+(2+j), 1, col);
					}
			else
				s->fastbox(left+1, top+1, bar_length, 5, whatcolor);
			s->fastbox(left+1+bar_length, top+1, bar_remainder, 5, BAR_BACK_COLOR);

		//This part rounds the corners (via 4 masks)

			for (i=0;i<4;i++)
			{
				//upper left
				s->ver_line(left+i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top, 2-i, 27);
				//upper right
				s->ver_line(left+61-i, top, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top, 2-i, 27);
				//lower left
				s->ver_line(left+i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+i, top+5+i, 2-i, 27);
				//lower right
				s->ver_line(left+61-i, top+4+i, 3-i, 0);
				if ((2-i)>0)
					s->ver_line(left+61-i, top+5+i, 2-i, 27);
			}
	} // end of sp stuff
} // end of drawing routine ..

// Note: new_draw_value_bar/new_score_panel/draw_percentage_bar moved to
// src/runtime/score_panel.cpp so score_panel() can be linked without main().
