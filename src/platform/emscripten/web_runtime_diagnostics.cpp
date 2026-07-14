#include <openglad/platform/emscripten/web_runtime_diagnostics.h>

#ifdef __EMSCRIPTEN__

#include <openglad/core/constants.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/platform/game_session.h>

#include <SDL3/SDL.h>
#include <emscripten.h>
#ifndef TESTING
#include <emscripten/val.h>
#endif

#include <algorithm>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>

namespace og::platform::web {

#ifndef TESTING
namespace {

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

struct AppliedPreloadStartConfig
{
    std::int32_t scenario_id = 1;
    std::int32_t difficulty = 1;
    std::string campaign_id = "org.openglad.gladiator";
    std::int32_t numplayers = 1;
    std::int32_t allied_mode = 0;
    std::int32_t my_team = 0;
};

struct AppliedPostLoadRuntimeOverrides
{
    std::uint32_t control_entity_id = 0u;
    float control_worldx = 0.0f;
    float control_worldy = 0.0f;
    float camera_topx_float = 0.0f;
    float camera_topy_float = 0.0f;
    std::int32_t camera_topx = 0;
    std::int32_t camera_topy = 0;
    std::int32_t facing = 0;
};

struct AppliedInputHold
{
    std::int32_t player_index = 0;
    std::string logical_action = "move_right";
    std::string playwright_key = "d";
};

struct JitterCaptureProfileState
{
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

struct HorizontalRunCandidate
{
    std::int32_t start_x = 0;
    std::int32_t y = 0;
    std::int32_t length = 0;
};

JitterCaptureProfileState& jitter_capture_profile_state()
{
    static JitterCaptureProfileState state;
    return state;
}

screen* active_screen()
{
    return og::runtime::current_session != nullptr
        ? og::runtime::current_session->myscreen_
        : nullptr;
}

double js_number_from_u64(std::uint64_t value)
{
    return static_cast<double>(value);
}

std::string playwright_key_for_sdl_keycode(SDL_Keycode keycode)
{
    if (keycode >= SDLK_A && keycode <= SDLK_Z)
    {
        const char value = static_cast<char>(
            'a' + static_cast<int>(keycode - SDLK_A));
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
    const std::int32_t max_start_x =
        run->start_x + run->length - safety_margin;
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

bool runtime_trace_capture_requested_from_window()
{
    using emscripten::val;
    val requested = val::global("window")["__opengladEnableRuntimeTraceCapture"];
    return !(requested.isNull() || requested.isUndefined()) && requested.as<bool>();
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

} // namespace

bool should_skip_intro_for_tests()
{
    return should_skip_web_intro_js();
}

void seed_test_save_if_requested()
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

void prepare_runtime_diagnostics_for_boot()
{
    refresh_web_jitter_capture_request();
    og::runtime::set_runtime_trace_enabled(
        runtime_trace_capture_requested_from_window());
    reset_runtime_diagnostics_for_gameplay_start();
}

void reset_runtime_diagnostics_for_gameplay_start()
{
    og::runtime::reset_runtime_trace_capture_state();
    initialize_web_runtime_trace_bridge();
}

void maybe_apply_jitter_capture_start_config(
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

void finalize_jitter_capture_profile_after_load(screen& current_screen)
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
#else

bool should_skip_intro_for_tests()
{
    return false;
}

void seed_test_save_if_requested()
{
}

void prepare_runtime_diagnostics_for_boot()
{
    og::runtime::set_runtime_trace_enabled(false);
    reset_runtime_diagnostics_for_gameplay_start();
}

void reset_runtime_diagnostics_for_gameplay_start()
{
    og::runtime::reset_runtime_trace_capture_state();
    og::runtime::clear_runtime_trace_observer();
    og::runtime::clear_runtime_render_sample_observer();
}

void maybe_apply_jitter_capture_start_config(
    og::ui::PickerLobbyGameStartConfig&)
{
}

void finalize_jitter_capture_profile_after_load(screen&)
{
}
#endif

} // namespace og::platform::web

#endif
