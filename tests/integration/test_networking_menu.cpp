#include <openglad/core/test_trace.h>
#include <openglad/interface/button.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/platform/video_sdl.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#if !defined(__EMSCRIPTEN__)
#include <ixwebsocket/IXGetFreePort.h>
#endif

#include <cstdlib>
#include <atomic>
#include <format>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_interact.h"

void picker_main(Sint32 argc, char** argv);
void picker_cleanup_resources();
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

namespace {

void cleanup_picker_state()
{
    picker_cleanup_resources();
    og::runtime::current_session->localbuttons_ = nullptr;
}

class ScopedEnvVar
{
public:
    ScopedEnvVar(const char* name, const char* value)
        : name_(name)
    {
        const char* const current = std::getenv(name_);
        if (current != nullptr)
        {
            had_value_ = true;
            old_value_ = current;
        }

        if (value != nullptr)
        {
#ifdef _WIN32
            _putenv_s(name_, value);
#else
            setenv(name_, value, 1);
#endif
        }
        else
        {
#ifdef _WIN32
            _putenv_s(name_, "");
#else
            unsetenv(name_);
#endif
        }
    }

    ~ScopedEnvVar()
    {
        if (had_value_)
        {
#ifdef _WIN32
            _putenv_s(name_, old_value_.c_str());
#else
            setenv(name_, old_value_.c_str(), 1);
#endif
        }
        else
        {
#ifdef _WIN32
            _putenv_s(name_, "");
#else
            unsetenv(name_);
#endif
        }
    }

private:
    const char* name_;
    bool had_value_ = false;
    std::string old_value_;
};

class PlatformBridgeGuard
{
public:
    PlatformBridgeGuard()
        : saved_(platform_bridge())
    {
    }

    ~PlatformBridgeGuard()
    {
        set_platform_bridge(std::move(saved_));
    }

private:
    PlatformBridge saved_;
};

class ScriptedRoomListRequest final
    : public og::ui::IPickerRelayRoomListRequest
{
public:
    using Poll = std::function<
        std::optional<og::ui::PickerRelayRoomListResult>()>;
    using OnDestroy = std::function<void()>;

    explicit ScriptedRoomListRequest(Poll poll, OnDestroy on_destroy = {})
        : poll_(std::move(poll)), on_destroy_(std::move(on_destroy))
    {
    }

    ~ScriptedRoomListRequest() override
    {
        if (on_destroy_)
            on_destroy_();
    }

    std::optional<og::ui::PickerRelayRoomListResult> poll() override
    {
        return poll_();
    }

private:
    Poll poll_;
    OnDestroy on_destroy_;
};

bool interactable_label_contains(const std::string& id,
                                 const std::string& expected_substring)
{
    const auto interactables = get_interactables();
    for (const Interactable& item : interactables)
    {
        if (item.id == id && !item.hidden)
            return item.label.find(expected_substring) != std::string::npos;
    }
    return false;
}

bool wait_for_interactable_label_contains(const std::string& id,
                                          const std::string& expected_substring,
                                          int timeout_ms = 5000)
{
    int elapsed = 0;
    constexpr int poll_interval_ms = 50;
    while (elapsed < timeout_ms)
    {
        if (interactable_label_contains(id, expected_substring))
            return true;
        SDL_Delay(poll_interval_ms);
        elapsed += poll_interval_ms;
    }
    return interactable_label_contains(id, expected_substring);
}

bool wait_for_trace_contains(const char* category,
                             const char* substring,
                             int timeout_ms = 5000)
{
    int elapsed = 0;
    constexpr int poll_interval_ms = 50;
    while (elapsed < timeout_ms)
    {
        if (trace_contains(category, substring))
            return true;
        SDL_Delay(poll_interval_ms);
        elapsed += poll_interval_ms;
    }
    return trace_contains(category, substring);
}

bool wait_for_any_interactable(std::initializer_list<const char*> ids,
                               int timeout_ms = 5000)
{
    int elapsed = 0;
    constexpr int poll_interval_ms = 50;
    while (elapsed < timeout_ms)
    {
        for (const char* id : ids)
        {
            if (has_interactable(id))
                return true;
        }
        SDL_Delay(poll_interval_ms);
        elapsed += poll_interval_ms;
    }

    for (const char* id : ids)
    {
        if (has_interactable(id))
            return true;
    }
    return false;
}

bool enter_team_build_from_continue_game(int timeout_ms = 10000)
{
    if (!wait_for_interactable("continue_game", timeout_ms))
        return false;

    SDL_Delay(300);
    interact("continue_game");
    return wait_for_interactable("networking", timeout_ms);
}

bool interact_until_label_contains(const std::string& id,
                                   const std::string& expected_substring,
                                   int timeout_ms = 10000)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    while (SDL_GetTicks() < deadline)
    {
        if (interactable_label_contains(id, expected_substring))
            return true;

        if (!wait_for_interactable(id, 250))
        {
            SDL_Delay(100);
            continue;
        }

        interact(id);
        // Generous confirmation wait: each interact() on a text field consumes
        // one queued prompt. If the label-update lags past this (e.g. the slow,
        // instrumented coverage-CI runner — ~12x slower than local), the loop
        // re-interacts and eats the NEXT field's prompt, starving a later step
        // (this is what flaked NetworkingMenu.submenu_validation_errors: the port
        // edit re-interacted, consumed the blank-IP prompt, and set_valid_port
        // never reached "24567"). Wait long enough to confirm before re-interacting.
        if (wait_for_interactable_label_contains(
                id, expected_substring, 5000))
        {
            return true;
        }

        SDL_Delay(100);
    }

    return interactable_label_contains(id, expected_substring);
}

bool interact_until_trace_contains(const std::string& id,
                                   const char* category,
                                   const char* substring,
                                   int timeout_ms = 10000)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    while (SDL_GetTicks() < deadline)
    {
        if (trace_contains(category, substring))
            return true;

        if (!has_interactable(id))
        {
            SDL_Delay(100);
            continue;
        }

        interact(id);
        if (wait_for_trace_contains(category, substring, 5000))
            return true;

        SDL_Delay(100);
    }

    return trace_contains(category, substring);
}

bool interact_until_any_interactable(const std::string& id,
                                     std::initializer_list<const char*> ids,
                                     int timeout_ms = 10000)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_any_interactable(ids, 0))
            return true;

        if (!has_interactable(id))
        {
            SDL_Delay(100);
            continue;
        }

        interact(id);
        if (wait_for_any_interactable(ids, 5000))
            return true;

        SDL_Delay(100);
    }

    return wait_for_any_interactable(ids, 0);
}

// Under TESTING every fadeblack takes FadeBetween's test-mode branch, which
// traces exactly one "video" line per fade — so counting those lines counts
// fades.
int count_fade_between_traces()
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int fades = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == "video" &&
            entry.message.find("FadeBetween") != std::string::npos)
            ++fades;
    }
    return fades;
}

// #237: a fading screen's exit scope skips its fade-out when an Instant note
// is pending — the door being opened is instant. Traced by the runner; the
// NETWORKING door (Base Camp exits to open it) is the ONLY door that shape
// is for, and these flows hold it to exactly that one occurrence.
int count_instant_exit_skips()
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int skips = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == "video" &&
            entry.message.find("exit fade skipped: Instant note pending") !=
                std::string::npos)
            ++skips;
    }
    return skips;
}

struct NetworkingJoinState
{
    bool started = false;
    bool finished = false;
    bool saw_networking_menu = false;
    bool updated_ip = false;
    bool updated_port = false;
    bool enabled_room_code = false;
    bool updated_room_code = false;
    bool saw_join_relay_error_popup = false;
    bool stayed_in_submenu_after_join_error = false;
    bool returned_to_main_menu = false;
    // #237: NETWORKING is a Base Camp strip door — instant, both ways, like
    // its HIRE/TRAIN/DIFFICULTY siblings. Base Camp exits before the legacy
    // screen runs, so the depth rule would fade BOTH legs without the
    // Instant notes in SdlPickerClient::configure_networking.
    int fades_added_by_networking_door = -1;
    int fades_added_by_networking_back = -1;
};

int networking_join_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingJoinState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }

    // Generic settle before the door is measured (fades are instant under
    // TESTING, so nothing here is waiting on an animation).
    SDL_Delay(300);
    const int fades_before_door = count_fade_between_traces();
    interact("networking");

    if (!wait_for_interactable("network_ip", 10000))
    {
        state->finished = true;
        return 0;
    }

    state->saw_networking_menu = true;
    SDL_Delay(150);  // generic settle (fades are instant under TESTING)
    state->fades_added_by_networking_door =
        count_fade_between_traces() - fades_before_door;

    state->updated_ip = interact_until_label_contains(
        "network_ip", "10.24.8.16");

    SDL_Delay(100);
    state->updated_port = interact_until_label_contains(
        "network_port", "24567");

    // Prove that editing the prominent relay field selects relay mode even
    // after the player explicitly chose DIRECT (LAN).
    SDL_Delay(100);
    if (interactable_label_contains("network_room_toggle", "ON"))
        (void)interact_until_label_contains("network_room_toggle", "OFF");

    SDL_Delay(100);
    state->updated_room_code = interact_until_label_contains(
        "network_room_value", "glad-xkcd");
    state->enabled_room_code = wait_for_interactable_label_contains(
        "network_room_toggle", "ON");

    SDL_Delay(150);
    state->saw_join_relay_error_popup = interact_until_trace_contains(
        "network_join",
        "popup",
        "JOIN GAME: Relay base URL must use");
    state->stayed_in_submenu_after_join_error =
        has_interactable("network_join") && has_interactable("network_back");

    if (has_interactable("network_back"))
    {
        SDL_Delay(150);
        const int fades_before_back = count_fade_between_traces();
        interact("network_back");
        // BACK re-presents Base Camp: the "networking" row is its own.
        if (wait_for_interactable("networking", 10000))
        {
            SDL_Delay(750);
            state->fades_added_by_networking_back =
                count_fade_between_traces() - fades_before_back;
        }
    }

    const Uint64 deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_interactable("quit", 250))
        {
            state->returned_to_main_menu = true;
            SDL_Delay(100);
            interact("quit");
            break;
        }

        if (wait_for_interactable("network_back", 150))
        {
            interact("network_back");
            SDL_Delay(150);
            continue;
        }

        if (wait_for_interactable("back", 150))
        {
            interact("back");
            SDL_Delay(150);
            continue;
        }

        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}

struct NetworkingValidationState
{
    bool started = false;
    bool finished = false;
    bool saw_networking_menu = false;
    bool set_invalid_port = false;
    bool saw_host_invalid_port_popup = false;
    bool saw_join_invalid_port_popup = false;
    bool set_valid_port = false;
    bool cleared_ip = false;
    bool enabled_room_code = false;
    bool saw_blank_ip_popup = false;
    bool saw_host_relay_error_popup = false;
    bool stayed_after_host_invalid_port = false;
    bool stayed_after_join_invalid_port = false;
    bool stayed_after_blank_ip = false;
    bool stayed_after_host_relay_error = false;
    bool returned_to_main_menu = false;
};

int networking_validation_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingValidationState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }

    SDL_Delay(300);
    interact("networking");

    if (!wait_for_interactable("network_port", 10000))
    {
        state->finished = true;
        return 0;
    }

    state->saw_networking_menu = true;
    SDL_Delay(150);

    // Relay is the default; choose the explicit DIRECT (LAN) path before
    // exercising IP/port validation.
    if (interactable_label_contains("network_room_toggle", "ON"))
        (void)interact_until_label_contains("network_room_toggle", "OFF");

    state->set_invalid_port = interact_until_label_contains(
        "network_port", "70000");

    SDL_Delay(150);
    state->saw_host_invalid_port_popup = interact_until_trace_contains(
        "network_host",
        "popup",
        "HOST GAME: Please enter a port from 1 to 65535.");
    state->stayed_after_host_invalid_port =
        has_interactable("network_host") && has_interactable("network_back");

    SDL_Delay(150);
    state->saw_join_invalid_port_popup = interact_until_trace_contains(
        "network_join",
        "popup",
        "JOIN GAME: Please enter a port from 1 to 65535.");
    state->stayed_after_join_invalid_port =
        has_interactable("network_join") && has_interactable("network_back");

    SDL_Delay(150);
    state->set_valid_port = interact_until_label_contains(
        "network_port", "24567");

    SDL_Delay(150);
    state->cleared_ip = interact_until_label_contains(
        "network_ip", "(enter address)");

    if (interactable_label_contains("network_room_toggle", "ON"))
    {
        SDL_Delay(100);
        (void)interact_until_label_contains("network_room_toggle", "OFF");
    }

    SDL_Delay(150);
    state->saw_blank_ip_popup = interact_until_trace_contains(
        "network_join",
        "popup",
        "JOIN GAME: Please enter an IP address or hostname.");
    state->stayed_after_blank_ip =
        has_interactable("network_join") && has_interactable("network_back");

    if (!interactable_label_contains("network_room_toggle", "ON"))
    {
        SDL_Delay(100);
    }
    state->enabled_room_code = interact_until_label_contains(
        "network_room_toggle", "ON");

    SDL_Delay(150);
    state->saw_host_relay_error_popup = interact_until_trace_contains(
        "network_host",
        "popup",
        "HOST GAME: Relay base URL must use");
    state->stayed_after_host_relay_error =
        has_interactable("network_host") && has_interactable("network_back");

    if (has_interactable("network_back"))
    {
        SDL_Delay(150);
        interact("network_back");
    }

    const Uint64 deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_interactable("quit", 250))
        {
            state->returned_to_main_menu = true;
            SDL_Delay(100);
            interact("quit");
            break;
        }

        if (wait_for_interactable("network_back", 150))
        {
            interact("network_back");
            SDL_Delay(150);
            continue;
        }

        if (wait_for_interactable("back", 150))
        {
            interact("back");
            SDL_Delay(150);
            continue;
        }

        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}

struct NetworkingHostFactoryErrorState
{
    bool started = false;
    bool finished = false;
    bool saw_networking_menu = false;
    bool updated_port = false;
    bool stayed_in_submenu_after_host_error = false;
    bool returned_to_main_menu = false;
};

int networking_host_factory_error_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingHostFactoryErrorState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }

    SDL_Delay(300);
    interact("networking");

    if (!wait_for_interactable("network_port", 10000))
    {
        state->finished = true;
        return 0;
    }

    state->saw_networking_menu = true;
    SDL_Delay(150);

    state->updated_port = interact_until_label_contains(
        "network_port", "24567");

    SDL_Delay(150);
    (void)interact_until_trace_contains(
        "network_host", "popup", "simulated host failure");
    state->stayed_in_submenu_after_host_error =
        has_interactable("network_host") && has_interactable("network_back");

    if (has_interactable("network_back"))
    {
        SDL_Delay(150);
        interact("network_back");
    }

    const Uint64 deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_interactable("quit", 250))
        {
            state->returned_to_main_menu = true;
            SDL_Delay(100);
            interact("quit");
            break;
        }

        if (wait_for_interactable("network_back", 150))
        {
            interact("network_back");
            SDL_Delay(150);
            continue;
        }

        if (wait_for_interactable("back", 150))
        {
            interact("back");
            SDL_Delay(150);
            continue;
        }

        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}

struct NetworkingRoomListState
{
    bool started = false;
    bool finished = false;
    bool saw_networking_menu = false;
    bool enabled_room_code = false;
    bool saw_room_button = false;
    bool room_label_ok = false;
    bool joined_selected_room = false;
    bool stayed_after_room_join_error = false;
    bool cleared_room_code = false;
    bool saw_prefilled_join = false;
    bool returned_to_main_menu = false;
};

// Drives the ACTIVE GAMES list end to end: relay mode ON -> auto-refresh
// surfaces the stubbed rooms as tappable rows -> tapping a row joins that
// room (join factory stub echoes the code) -> a JOIN with a blank room code
// opens the room prompt preselecting the first live room.
int networking_room_list_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingRoomListState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }

    SDL_Delay(300);
    interact("networking");

    if (!wait_for_interactable("network_room_value", 10000))
    {
        state->finished = true;
        return 0;
    }

    state->saw_networking_menu = true;
    SDL_Delay(150);

    state->enabled_room_code = interact_until_label_contains(
        "network_room_toggle", "ON");

    // The throttled in-menu refresh (bridge stub) surfaces the rooms.
    state->saw_room_button = wait_for_interactable("network_room_0", 10000);
    state->room_label_ok = wait_for_interactable_label_contains(
        "network_room_0", "GLAD-AAAA");

    SDL_Delay(300);
    state->joined_selected_room = interact_until_trace_contains(
        "network_room_0",
        "popup",
        "JOIN GAME: simulated join to GLAD-AAAA");
    state->stayed_after_room_join_error =
        has_interactable("network_join") && has_interactable("network_back");

    // Clear the room code (the row click stored GLAD-AAAA in the field),
    // then JOIN: the blank-code path must open the room prompt preselecting
    // the first live room (queued prompt answer overrides it).
    SDL_Delay(150);
    state->cleared_room_code = interact_until_label_contains(
        "network_room_value", "(enter room code)");

    SDL_Delay(150);
    state->saw_prefilled_join = interact_until_trace_contains(
        "network_join",
        "popup",
        "JOIN GAME: simulated join to glad-bbbb");

    if (has_interactable("network_back"))
    {
        SDL_Delay(150);
        interact("network_back");
    }

    const Uint64 deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_interactable("quit", 250))
        {
            state->returned_to_main_menu = true;
            SDL_Delay(100);
            interact("quit");
            break;
        }

        if (wait_for_interactable("network_back", 150))
        {
            interact("network_back");
            SDL_Delay(150);
            continue;
        }

        if (wait_for_interactable("back", 150))
        {
            interact("back");
            SDL_Delay(150);
            continue;
        }

        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}

struct NetworkingEmptyRoomListState
{
    bool started = false;
    bool finished = false;
    bool saw_networking_menu = false;
    bool enabled_room_code = false;
    bool no_room_rows_appeared = false;
    bool returned_to_main_menu = false;
};

struct NetworkingRoomRefreshRaceState
{
    std::atomic<bool> second_refresh_started{false};
    std::atomic<bool> release_second_refresh{false};
    bool started = false;
    bool finished = false;
    bool saw_initial_room = false;
    bool queued_click_during_refresh = false;
    bool joined_visible_room = false;
    bool avoided_reordered_room = false;
    bool saw_new_snapshot_after_click = false;
    bool returned_to_main_menu = false;
};

struct NetworkingRoomReentryState
{
    std::atomic<bool> stale_request_polled{false};
    std::atomic<bool> stale_request_destroyed{false};
    std::atomic<bool> replacement_request_polled{false};
    std::atomic<bool> release_replacement{false};
    bool started = false;
    bool finished = false;
    bool saw_initial_room = false;
    bool reentered_networking = false;
    bool hid_stale_room = false;
    bool saw_replacement_room = false;
    bool returned_to_main_menu = false;
};

int networking_room_refresh_race_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingRoomRefreshRaceState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);
    interact("networking");
    state->saw_initial_room = wait_for_interactable_label_contains(
        "network_room_0", "GLAD-AAAA", 10000);

    const Uint64 refresh_deadline = SDL_GetTicks() + 10000;
    while (!state->second_refresh_started.load() &&
           SDL_GetTicks() < refresh_deadline)
    {
        SDL_Delay(10);
    }

    if (state->second_refresh_started.load())
    {
        // The second room-list request stays pending, but the menu must remain
        // responsive and join the still-drawn A row before B is released.
        interact("network_room_0");
        state->queued_click_during_refresh = true;
        state->joined_visible_room = wait_for_trace_contains(
            "popup", "simulated join to GLAD-AAAA", 5000);
    }

    state->avoided_reordered_room =
        !trace_contains("popup", "simulated join to GLAD-BBBB");
    state->release_second_refresh.store(true);
    state->saw_new_snapshot_after_click = wait_for_interactable_label_contains(
        "network_room_0", "GLAD-BBBB", 10000);

    if (has_interactable("network_back"))
        interact("network_back");
    const Uint64 exit_deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < exit_deadline)
    {
        if (wait_for_interactable("quit", 250))
        {
            state->returned_to_main_menu = true;
            interact("quit");
            break;
        }
        if (wait_for_interactable("network_back", 100))
            interact("network_back");
        else if (wait_for_interactable("back", 100))
            interact("back");
        else
            inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->release_second_refresh.store(true);
    state->finished = true;
    return 0;
}

int networking_room_reentry_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingRoomReentryState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);
    interact("networking");
    state->saw_initial_room = wait_for_interactable_label_contains(
        "network_room_0", "GLAD-AAAA", 10000);

    // Let the next refresh begin and remain pending. BACK must destroy this
    // obsolete generation immediately so re-entry can start request 3 rather
    // than waiting for request 2 to settle.
    const Uint64 stale_request_deadline = SDL_GetTicks() + 10000;
    while (!state->stale_request_polled.load() &&
           SDL_GetTicks() < stale_request_deadline)
    {
        SDL_Delay(10);
    }

    if (has_interactable("network_back"))
        interact("network_back");
    if (wait_for_interactable("networking", 5000))
    {
        SDL_Delay(300);
        interact("networking");
        state->reentered_networking =
            wait_for_interactable("network_room_value", 5000);
    }

    const Uint64 replacement_deadline = SDL_GetTicks() + 10000;
    while (!state->replacement_request_polled.load() &&
           SDL_GetTicks() < replacement_deadline)
    {
        SDL_Delay(10);
    }

    // Re-entry starts a new view generation. The old A row must disappear
    // immediately and remain hidden while the replacement request is pending.
    SDL_Delay(300);
    state->hid_stale_room =
        !interactable_label_contains("network_room_0", "GLAD-AAAA");
    state->release_replacement.store(true);
    state->saw_replacement_room = wait_for_interactable_label_contains(
        "network_room_0", "GLAD-BBBB", 10000);

    if (has_interactable("network_back"))
        interact("network_back");
    const Uint64 exit_deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < exit_deadline)
    {
        if (wait_for_interactable("quit", 250))
        {
            state->returned_to_main_menu = true;
            interact("quit");
            break;
        }
        if (wait_for_interactable("network_back", 100))
            interact("network_back");
        else if (wait_for_interactable("back", 100))
            interact("back");
        else
            inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->release_replacement.store(true);
    state->finished = true;
    return 0;
}

// Relay mode ON with a healthy relay that has no live rooms: the refresh
// succeeds, no ACTIVE GAMES rows appear, and the menu stays usable.
int networking_empty_room_list_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingEmptyRoomListState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }

    SDL_Delay(300);
    interact("networking");

    if (!wait_for_interactable("network_room_value", 10000))
    {
        state->finished = true;
        return 0;
    }

    state->saw_networking_menu = true;
    SDL_Delay(150);

    state->enabled_room_code = interact_until_label_contains(
        "network_room_toggle", "ON");

    // Give the throttled refresh time to run and draw a few frames with the
    // empty result ("No active games found.").
    SDL_Delay(1200);
    state->no_room_rows_appeared = !has_interactable("network_room_0");

    if (has_interactable("network_back"))
    {
        SDL_Delay(150);
        interact("network_back");
    }

    const Uint64 deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_interactable("quit", 250))
        {
            state->returned_to_main_menu = true;
            SDL_Delay(100);
            interact("quit");
            break;
        }

        if (wait_for_interactable("network_back", 150))
        {
            interact("network_back");
            SDL_Delay(150);
            continue;
        }

        if (wait_for_interactable("back", 150))
        {
            interact("back");
            SDL_Delay(150);
            continue;
        }

        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}

#if !defined(__EMSCRIPTEN__)
struct NetworkingHostState
{
    bool started = false;
    bool finished = false;
    bool saw_networking_menu = false;
    bool updated_port = false;
    bool entered_team_menu = false;
    bool returned_to_main_menu = false;
    int port = 0;
};

int networking_host_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingHostState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }

    SDL_Delay(300);
    interact("networking");

    if (!wait_for_interactable("network_port", 10000))
    {
        state->finished = true;
        return 0;
    }

    state->saw_networking_menu = true;
    SDL_Delay(150);

    if (interactable_label_contains("network_room_toggle", "ON"))
        (void)interact_until_label_contains("network_room_toggle", "OFF");

    state->updated_port = interact_until_label_contains(
        "network_port", std::to_string(state->port));

    SDL_Delay(150);
    state->entered_team_menu = interact_until_any_interactable(
        "network_host", {"hire_troops", "go", "back"}, 15000);

    if (state->entered_team_menu && wait_for_interactable("back", 5000))
    {
        SDL_Delay(150);
        interact("back");
    }

    const Uint64 deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_interactable("quit", 250))
        {
            state->returned_to_main_menu = true;
            SDL_Delay(100);
            interact("quit");
            break;
        }

        if (wait_for_interactable("back", 150))
        {
            interact("back");
            SDL_Delay(150);
            continue;
        }

        if (wait_for_interactable("network_back", 150))
        {
            interact("network_back");
            SDL_Delay(150);
            continue;
        }

        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}
#endif

} // namespace

TEST(NetworkingMenu, room_code_join_invalid_relay_url_stays_in_submenu)
{
    trace_clear();
    ScopedEnvVar relay_env("OPENGLAD_RELAY_BASE_URL", "ftp://relay.invalid");
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("10.24.8.16");
    level_editor_testing_prompt_queue_push("24567");
    level_editor_testing_prompt_queue_push("glad-xkcd");

    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));

    NetworkingJoinState state;
    SDL_Thread* thread =
        SDL_CreateThread(networking_join_injector, "networking_join_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create networking join injector";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_networking_menu);
    ASSERT_TRUE(state.updated_ip);
    ASSERT_TRUE(state.updated_port);
    ASSERT_TRUE(state.enabled_room_code);
    ASSERT_TRUE(state.updated_room_code);
    ASSERT_TRUE(state.saw_join_relay_error_popup);
    ASSERT_TRUE(state.stayed_in_submenu_after_join_error);
    ASSERT_TRUE(state.returned_to_main_menu);
    // #237: NETWORKING is a Base Camp strip door — symmetric-INSTANT, like
    // HIRE/TRAIN/DIFFICULTY. Master faded the way back only; that was one of
    // the reported asymmetries, and the fix was to make both legs match, not
    // to fade both. Base Camp exits before the legacy screen runs, so the
    // depth rule alone would fade them: the Instant notes in
    // SdlPickerClient::configure_networking are what these two pins hold.
    EXPECT_EQ(0, state.fades_added_by_networking_door)
        << "#237: the NETWORKING door must stay instant";
    EXPECT_EQ(0, state.fades_added_by_networking_back)
        << "#237 symmetry: and so must BACK to Base Camp";
    // The mechanism behind the 0 on the way in: Base Camp's exit scope
    // skipped its fade-out for the pending Instant note — once, on this
    // door, and nowhere else in the flow (CONTINUE in, BACK out, QUIT).
    EXPECT_EQ(1, count_instant_exit_skips())
        << "#237: the Instant-note exit skip happens exactly on the "
           "NETWORKING door";
}

TEST(NetworkingMenu, submenu_validation_errors_stay_in_place)
{
    trace_clear();
    ScopedEnvVar relay_env("OPENGLAD_RELAY_BASE_URL", "ftp://relay.invalid");
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("70000");
    level_editor_testing_prompt_queue_push("24567");
    level_editor_testing_prompt_queue_push("");

    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));

    NetworkingValidationState state;
    SDL_Thread* thread = SDL_CreateThread(
        networking_validation_injector, "networking_validation_test", &state);
    ASSERT_TRUE(thread != nullptr)
        << "failed to create networking validation injector";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_networking_menu);
    ASSERT_TRUE(state.set_invalid_port);
    ASSERT_TRUE(state.saw_host_invalid_port_popup);
    ASSERT_TRUE(state.saw_join_invalid_port_popup);
    ASSERT_TRUE(state.set_valid_port);
    ASSERT_TRUE(state.cleared_ip);
    ASSERT_TRUE(state.enabled_room_code);
    ASSERT_TRUE(state.saw_blank_ip_popup);
    ASSERT_TRUE(state.saw_host_relay_error_popup);
    ASSERT_TRUE(state.stayed_after_host_invalid_port);
    ASSERT_TRUE(state.stayed_after_join_invalid_port);
    ASSERT_TRUE(state.stayed_after_blank_ip);
    ASSERT_TRUE(state.stayed_after_host_relay_error);
    ASSERT_TRUE(state.returned_to_main_menu);
}

TEST(NetworkingMenu, room_list_rows_join_and_prefill_first_room)
{
    trace_clear();
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    bridge.begin_list_relay_rooms = {};
    bridge.list_relay_rooms =
        [](const std::string&, const std::string&)
            -> std::vector<og::ui::PickerRelayRoomInfo> {
        std::vector<og::ui::PickerRelayRoomInfo> rooms;
        og::ui::PickerRelayRoomInfo room;
        room.code = "GLAD-AAAA";
        room.host_name = "Alice";
        room.player_count = 2u;
        rooms.push_back(room);
        room.code = "GLAD-BBBB";
        room.host_name = "Bob";
        room.player_count = 1u;
        rooms.push_back(room);
        return rooms;
    };
    bridge.create_join_picker_lobby_client =
        [](const og::ui::PickerJoinGameOptions& options)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        throw std::runtime_error(
            std::format("simulated join to {}", options.room_code));
    };
    set_platform_bridge(std::move(bridge));

    level_editor_testing_prompt_queue_clear();
    // First prompt: clear the room-code field. Second: the blank-code JOIN
    // room prompt (overriding the GLAD-AAAA preselection).
    level_editor_testing_prompt_queue_push("");
    level_editor_testing_prompt_queue_push("glad-bbbb");

    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));

    NetworkingRoomListState state;
    SDL_Thread* thread = SDL_CreateThread(
        networking_room_list_injector, "networking_room_list_test", &state);
    ASSERT_TRUE(thread != nullptr)
        << "failed to create networking room list injector";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_networking_menu);
    ASSERT_TRUE(state.enabled_room_code);
    ASSERT_TRUE(state.saw_room_button);
    ASSERT_TRUE(state.room_label_ok);
    ASSERT_TRUE(state.joined_selected_room);
    ASSERT_TRUE(state.stayed_after_room_join_error);
    ASSERT_TRUE(state.cleared_room_code);
    ASSERT_TRUE(state.saw_prefilled_join);
    ASSERT_TRUE(state.returned_to_main_menu);
    ASSERT_TRUE(trace_contains("networking", "join prompt prefill GLAD-AAAA"))
        << "blank-code JOIN should preselect the first live room";
}

TEST(NetworkingMenu, room_click_during_refresh_joins_the_visible_snapshot)
{
    trace_clear();
    PlatformBridgeGuard bridge_guard;
    NetworkingRoomRefreshRaceState state;
    std::atomic<int> list_calls{0};

    PlatformBridge bridge = platform_bridge();
    bridge.begin_list_relay_rooms =
        [&state, &list_calls](const std::string&, const std::string&)
            -> std::unique_ptr<og::ui::IPickerRelayRoomListRequest> {
        const int call = list_calls.fetch_add(1) + 1;
        og::ui::PickerRelayRoomInfo room;
        room.code = call == 1 ? "GLAD-AAAA" : "GLAD-BBBB";
        room.host_name = call == 1 ? "Visible Host" : "New Host";
        room.player_count = 1u;
        return std::make_unique<ScriptedRoomListRequest>(
            [&state, call, room = std::move(room), delivered = false]() mutable
                -> std::optional<og::ui::PickerRelayRoomListResult> {
                if (delivered)
                    return std::nullopt;
                if (call >= 2)
                {
                    state.second_refresh_started.store(true);
                    if (!state.release_second_refresh.load())
                        return std::nullopt;
                }
                delivered = true;
                og::ui::PickerRelayRoomListResult result;
                result.rooms.push_back(std::move(room));
                return result;
            });
    };
    bridge.create_join_picker_lobby_client =
        [](const og::ui::PickerJoinGameOptions& options)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        throw std::runtime_error(
            std::format("simulated join to {}", options.room_code));
    };
    set_platform_bridge(std::move(bridge));

    level_editor_testing_prompt_queue_clear();
    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));

    SDL_Thread* thread = SDL_CreateThread(
        networking_room_refresh_race_injector,
        "networking_room_refresh_race_test",
        &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_initial_room);
    ASSERT_TRUE(state.second_refresh_started.load());
    ASSERT_TRUE(state.queued_click_during_refresh);
    ASSERT_TRUE(state.joined_visible_room)
        << "the queued click must join the A row that was visible";
    ASSERT_TRUE(state.avoided_reordered_room)
        << "the just-fetched B row must not steal the queued click";
    ASSERT_TRUE(state.saw_new_snapshot_after_click);
    ASSERT_TRUE(state.returned_to_main_menu);
}

TEST(NetworkingMenu, reentry_hides_stale_rooms_until_current_request_completes)
{
    trace_clear();
    PlatformBridgeGuard bridge_guard;
    NetworkingRoomReentryState state;
    std::atomic<int> list_calls{0};

    PlatformBridge bridge = platform_bridge();
    bridge.begin_list_relay_rooms =
        [&state, &list_calls](const std::string&, const std::string&)
            -> std::unique_ptr<og::ui::IPickerRelayRoomListRequest> {
        const int call = list_calls.fetch_add(1) + 1;
        og::ui::PickerRelayRoomInfo room;
        room.code = call == 1 ? "GLAD-AAAA" : "GLAD-BBBB";
        room.host_name = call == 1 ? "Old Host" : "Current Host";
        return std::make_unique<ScriptedRoomListRequest>(
            [&state, call, room = std::move(room), delivered = false]() mutable
                -> std::optional<og::ui::PickerRelayRoomListResult> {
                if (delivered)
                    return std::nullopt;
                if (call == 2)
                {
                    state.stale_request_polled.store(true);
                    return std::nullopt;
                }
                if (call >= 3)
                {
                    state.replacement_request_polled.store(true);
                    if (!state.release_replacement.load())
                        return std::nullopt;
                }
                delivered = true;
                og::ui::PickerRelayRoomListResult result;
                result.rooms.push_back(std::move(room));
                return result;
            },
            [&state, call] {
                if (call == 2)
                    state.stale_request_destroyed.store(true);
            });
    };
    set_platform_bridge(std::move(bridge));

    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));

    SDL_Thread* thread = SDL_CreateThread(
        networking_room_reentry_injector,
        "networking_room_reentry_test",
        &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_initial_room);
    ASSERT_TRUE(state.stale_request_polled.load());
    ASSERT_TRUE(state.stale_request_destroyed.load())
        << "BACK must cancel the obsolete pending discovery request";
    ASSERT_TRUE(state.reentered_networking);
    ASSERT_TRUE(state.replacement_request_polled.load());
    ASSERT_TRUE(state.hid_stale_room)
        << "a previous menu generation must never remain tappable on re-entry";
    ASSERT_TRUE(state.saw_replacement_room);
    ASSERT_TRUE(state.returned_to_main_menu);
}

TEST(NetworkingMenu, empty_room_list_shows_no_rows_and_stays_usable)
{
    trace_clear();
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    bridge.begin_list_relay_rooms = {};
    bridge.list_relay_rooms =
        [](const std::string&, const std::string&)
            -> std::vector<og::ui::PickerRelayRoomInfo> {
        return {};
    };
    set_platform_bridge(std::move(bridge));

    level_editor_testing_prompt_queue_clear();

    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));

    NetworkingEmptyRoomListState state;
    SDL_Thread* thread = SDL_CreateThread(
        networking_empty_room_list_injector,
        "networking_empty_room_list_test",
        &state);
    ASSERT_TRUE(thread != nullptr)
        << "failed to create networking empty room list injector";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_networking_menu);
    ASSERT_TRUE(state.enabled_room_code);
    ASSERT_TRUE(state.no_room_rows_appeared);
    ASSERT_TRUE(state.returned_to_main_menu);
}

TEST(NetworkingMenu, host_factory_error_stays_in_submenu)
{
    trace_clear();
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    bridge.begin_list_relay_rooms = {};
    bridge.list_relay_rooms =
        [](const std::string&, const std::string&)
            -> std::vector<og::ui::PickerRelayRoomInfo> {
        return {};
    };
    bridge.create_host_picker_lobby_client =
        [](const og::ui::PickerHostGameOptions&)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        throw std::runtime_error("simulated host failure");
    };
    set_platform_bridge(std::move(bridge));

    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("24567");

    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));

    NetworkingHostFactoryErrorState state;
    SDL_Thread* thread = SDL_CreateThread(
        networking_host_factory_error_injector,
        "networking_host_factory_error_test",
        &state);
    ASSERT_TRUE(thread != nullptr)
        << "failed to create networking host factory error injector";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_networking_menu);
    ASSERT_TRUE(state.updated_port);
    ASSERT_TRUE(state.stayed_in_submenu_after_host_error);
    ASSERT_TRUE(state.returned_to_main_menu);
    ASSERT_TRUE(trace_contains("popup", "simulated host failure"));
}

#if !defined(__EMSCRIPTEN__)
TEST(NetworkingMenu, host_flow_enters_team_build_and_returns_to_main_menu)
{
    trace_clear();
    level_editor_testing_prompt_queue_clear();

    NetworkingHostState state;
    state.port = ix::getFreePort();
    const std::string port_text = std::to_string(state.port);
    level_editor_testing_prompt_queue_push(port_text.c_str());

    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));

    SDL_Thread* thread =
        SDL_CreateThread(networking_host_injector, "networking_host_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create networking host injector";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.saw_networking_menu);
    ASSERT_TRUE(state.updated_port);
    ASSERT_TRUE(state.entered_team_menu);
    ASSERT_TRUE(state.returned_to_main_menu);
    ASSERT_EQ(0, trace_count("popup"))
        << "successful hosting should enter the lobby directly without an intermediate status popup";
    // #237: the door in is the one Instant-note exit skip; the hookup exit
    // is the legacy screen's OWN fade-out (fade_out_at_exit), so Base Camp's
    // fading re-entry finds a black window — the listener holds that no
    // entry in this flow found an unfaded one.
    EXPECT_EQ(1, count_instant_exit_skips())
        << "#237: the Instant-note exit skip happens exactly on the "
           "NETWORKING door";
    EXPECT_EQ(0, og::video_testing::g_fade_violations.load())
        << (og::video_testing::fade_violation_messages().empty()
                ? std::string()
                : og::video_testing::fade_violation_messages().front());
}
#endif

// ---------------------------------------------------------------------------
// LINEUP §6 session modes (Hosting / Joined), driven through the real
// NETWORKING screen with a stub lobby client (the test_menu_layout pattern):
// the five list rows become PLAYERS machine rows, DISCONNECT replaces
// HOST/JOIN, kicks confirm and land on the stub with the clicked row's
// machine id, and DISCONNECT swaps a fresh LOCAL client in. The live
// two-peer kick wire is pinned by test_picker_network_client.cpp
// (host_kicks_a_joiner_which_learns_it_was_kicked) — these flows pin the
// picker glue only.

namespace {

class FakeSessionLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return host_view.load();
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players()
        const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return players;
    }
    [[nodiscard]] std::vector<std::uint8_t> local_player_indices()
        const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return local_indices;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    // The real clients answer this from the server pointer (host) or from
    // "lobby state landed AND the link is alive" (joiner); the stub carries
    // the answer directly so a test can pose a pending, a dead and a live
    // session without a transport.
    [[nodiscard]] bool session_established() const noexcept override
    {
        return established.load();
    }
    [[nodiscard]] std::string session_room_code() const override
    {
        return "GLAD-7Q2F";
    }
    bool kick_machine(og::sim::LobbyMachineId machine_id) override
    {
        kicked_machine.store(machine_id);
        return true;
    }
    bool disconnect_session() override
    {
        disconnect_calls.fetch_add(1);
        return true;
    }

    std::atomic<bool> host_view{true};
    std::atomic<bool> established{true};
    std::atomic<og::sim::LobbyMachineId> kicked_machine{0};
    std::atomic<int> disconnect_calls{0};
    mutable std::mutex mutex;
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_indices;
};

og::sim::LobbyPlayer session_seat(std::uint8_t index,
                                  og::sim::LobbyMachineId machine_id,
                                  const char* name, const char* company,
                                  bool is_host)
{
    og::sim::LobbyPlayer player;
    player.player_index = index;
    player.seat_id = static_cast<og::sim::LobbySeatId>(index) + 1;
    player.machine_id = machine_id;
    player.name = name;
    player.company = company;
    player.is_host = is_host;
    return player;
}

void seed_session_flow_save()
{
    auto& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(save.save("save0"));
}

struct SessionHostKickState
{
    FakeSessionLobbyClient* lobby = nullptr;
    og::ui::IPickerLobbyClient* saved_client = nullptr;
    bool started = false;
    bool finished = false;
    bool entered_session_view = false;
    bool host_join_hidden = false;
    bool machine_rows_labeled = false;
    bool room_code_shown = false;
    bool kick_confirmed = false;
    bool returned_to_main_menu = false;
};

int session_host_kick_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<SessionHostKickState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }

    // Install the session stub between engine frames (the issue #257 pump);
    // the Base Camp screen is engine-hosted, so the pump is live here.
    run_on_main_thread([state] {
        state->saved_client = og::ui::active_picker_lobby_client();
        og::ui::install_active_picker_lobby_client(state->lobby);
    });
    SDL_Delay(200);
    interact("networking");
    state->entered_session_view =
        wait_for_interactable("network_disconnect", 10000);
    SDL_Delay(300);
    state->host_join_hidden = !has_interactable("network_host") &&
        !has_interactable("network_join");
    // §6: the COMPANY leads each row; the opaque transport name never
    // shows while a company name exists.
    state->machine_rows_labeled =
        wait_for_interactable_label_contains("network_room_0", "(HOST)") &&
        interactable_label_contains("network_room_0", "(YOU)") &&
        interactable_label_contains("network_room_0", "M1 IRON KETTLE BAND") &&
        wait_for_interactable_label_contains("network_room_1",
                                             "M2 RIVER BAND") &&
        !interactable_label_contains("network_room_1", "net-far");
    state->room_code_shown = wait_for_interactable_label_contains(
        "network_room_value", "GLAD-7Q2F");

    // Host kicks the foreign machine: confirm YES, stub records id 2.
    // Bounded retry (the click_until idiom): a swallowed click consumes no
    // queued answer it did not reach, so each attempt queues its own YES.
    for (int attempt = 0;
         attempt < 3 && !trace_contains("networking", "KICKED"); ++attempt)
    {
        picker_testing_yes_or_no_queue_push(true);
        SDL_Delay(150);
        interact("network_room_1");
        (void)wait_for_trace_contains("networking", "KICKED", 3000);
    }
    state->kick_confirmed = trace_contains("networking", "KICKED");

    SDL_Delay(300);
    interact("network_back");
    wait_for_interactable("networking", 10000);
    // Uninstall the stub before leaving (its storage is the test frame).
    run_on_main_thread([state] {
        og::ui::install_active_picker_lobby_client(state->saved_client);
    });
    SDL_Delay(300);
    interact("back");
    state->returned_to_main_menu = wait_for_interactable("quit", 10000);
    SDL_Delay(150);
    interact("quit");
    state->finished = true;
    return 0;
}

} // namespace

TEST(NetworkingMenu, session_host_view_lists_machines_and_kicks)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    seed_session_flow_save();

    FakeSessionLobbyClient lobby;
    lobby.players = {
        session_seat(0, 1, "net-self", "IRON KETTLE BAND", true),
        session_seat(1, 2, "net-far", "RIVER BAND", false),
    };
    lobby.local_indices = {0};

    SessionHostKickState state;
    state.lobby = &lobby;
    SDL_Thread* thread = SDL_CreateThread(
        session_host_kick_injector, "networking_session_kick", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    picker_testing_yes_or_no_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.entered_session_view)
        << "hosting view must show DISCONNECT";
    EXPECT_TRUE(state.host_join_hidden)
        << "HOST/JOIN must hide in a session";
    EXPECT_TRUE(state.machine_rows_labeled)
        << "PLAYERS rows must carry the machine labels";
    EXPECT_TRUE(state.room_code_shown)
        << "the session room code shows read-only";
    EXPECT_TRUE(state.kick_confirmed);
    EXPECT_TRUE(state.returned_to_main_menu);
    EXPECT_TRUE(trace_contains("confirm", "KICK"))
        << "the kick must go through the yes/no confirm";
    EXPECT_EQ(2u, lobby.kicked_machine.load())
        << "the clicked row's machine id reaches kick_machine";
    EXPECT_EQ(0, lobby.disconnect_calls.load());
}

namespace {

struct SessionJoinDisconnectState
{
    FakeSessionLobbyClient* lobby = nullptr;
    bool started = false;
    bool finished = false;
    bool entered_session_view = false;
    bool foreign_row_inert = false;
    bool disconnected = false;
    bool back_in_team_build_local = false;
    bool returned_to_main_menu = false;
};

int session_join_disconnect_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<SessionJoinDisconnectState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }

    run_on_main_thread([state] {
        og::ui::install_active_picker_lobby_client(state->lobby);
    });
    SDL_Delay(200);
    interact("networking");
    state->entered_session_view =
        wait_for_interactable("network_disconnect", 10000);
    SDL_Delay(300);

    // Every machine row is inert for a joiner: clicking the host's row
    // must not open the kick confirm or reach the stub.
    interact("network_room_0");
    SDL_Delay(400);
    state->foreign_row_inert = !trace_contains("confirm", "KICK") &&
        state->lobby->kicked_machine.load() == 0;

    // Bounded retry: each attempt queues its own YES; once the disconnect
    // lands the screen exits and the button disappears, so a retried click
    // is a no-op.
    for (int attempt = 0;
         attempt < 3 && !trace_contains("networking", "DISCONNECTED");
         ++attempt)
    {
        picker_testing_yes_or_no_queue_push(true);
        SDL_Delay(150);
        interact("network_disconnect");
        (void)wait_for_trace_contains("networking", "DISCONNECTED", 3000);
    }
    state->disconnected = trace_contains("networking", "DISCONNECTED");

    // The swap left a fresh LOCAL client active: Team Build shows GO again
    // (a networked joiner would show the READY twin instead).
    state->back_in_team_build_local = wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");
    state->returned_to_main_menu = wait_for_interactable("quit", 10000);
    SDL_Delay(150);
    interact("quit");
    state->finished = true;
    return 0;
}

} // namespace

TEST(NetworkingMenu, session_joined_rows_inert_and_disconnect_goes_local)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    seed_session_flow_save();

    FakeSessionLobbyClient lobby;
    lobby.host_view.store(false);
    lobby.players = {
        session_seat(0, 1, "net-host", "IRON KETTLE BAND", true),
        session_seat(1, 2, "net-self", "RIVER BAND", false),
    };
    lobby.local_indices = {1};

    SessionJoinDisconnectState state;
    state.lobby = &lobby;
    SDL_Thread* thread = SDL_CreateThread(
        session_join_disconnect_injector, "networking_session_leave", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    picker_testing_yes_or_no_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.entered_session_view);
    EXPECT_TRUE(state.foreign_row_inert)
        << "a joiner's machine rows never dispatch";
    EXPECT_TRUE(state.disconnected);
    EXPECT_TRUE(state.back_in_team_build_local)
        << "after DISCONNECT the local client is active (GO visible)";
    EXPECT_TRUE(state.returned_to_main_menu);
    EXPECT_EQ(1, lobby.disconnect_calls.load())
        << "DISCONNECT must reach disconnect_session exactly once";
    EXPECT_EQ(0u, lobby.kicked_machine.load());
    EXPECT_TRUE(trace_contains("popup", "DISCONNECTED"));
}

namespace {

struct ActiveLobbyClientGuard
{
    og::ui::IPickerLobbyClient* saved = nullptr;

    explicit ActiveLobbyClientGuard(og::ui::IPickerLobbyClient* client)
        : saved(og::ui::active_picker_lobby_client())
    {
        og::ui::install_active_picker_lobby_client(client);
    }

    ~ActiveLobbyClientGuard()
    {
        og::ui::install_active_picker_lobby_client(saved);
    }
};

} // namespace

// LINEUP §6: session mode needs an ESTABLISHED session, not merely a
// networked client. A joiner mid-handshake (or one whose connect failed) has
// received no lobby state, so it keeps the IDLE table — JOIN clickable in its
// own rect, DISCONNECT hidden — and a second JOIN replaces the pending client
// the way it always did. Getting this wrong put DISCONNECT (drawn in JOIN's
// exact rect) under the JOIN click.
TEST(NetworkingMenu, connecting_joiner_keeps_the_idle_table_until_lobby_state)
{
    button* const buttons = picker_networking_buttons();
    const int count = picker_networking_button_count();
    ASSERT_EQ(kNetworkingMenuButtonCount, count);

    FakeSessionLobbyClient lobby;
    lobby.host_view.store(false);  // a joiner, never the host
    lobby.established.store(false);  // ...still handshaking
    ActiveLobbyClientGuard guard(&lobby);

    // is_networked_session() is true, no session behind it yet: Idle.
    const NetworkingMenuModeState connecting =
        picker_current_networking_menu_mode();
    EXPECT_FALSE(connecting.networked)
        << "a joiner without lobby state is not an established session";
    std::vector<button> idle(buttons, buttons + count);
    picker_apply_networking_menu_mode(idle.data(), count, connecting);
    EXPECT_FALSE(idle[kNetworkingMenuJoinIndex].hidden)
        << "JOIN must stay clickable so a re-JOIN replaces the client";
    EXPECT_FALSE(idle[kNetworkingMenuHostIndex].hidden);
    EXPECT_TRUE(idle[kNetworkingMenuDisconnectIndex].hidden)
        << "DISCONNECT must not sit in JOIN's rect while idle";

    // A roster ALONE is not a session: the real join client keeps the last
    // state after the link dies, so the stale roster must not buy back the
    // session table (and with it JOIN's rect).
    {
        std::lock_guard<std::mutex> lock(lobby.mutex);
        lobby.players = {
            session_seat(0, 1, "net-host", "IRON KETTLE BAND", true),
            session_seat(1, 2, "net-self", "RIVER BAND", false),
        };
    }
    const NetworkingMenuModeState lost = picker_current_networking_menu_mode();
    EXPECT_FALSE(lost.networked)
        << "a dead link is Idle however full its retained roster is";
    std::vector<button> after_loss(buttons, buttons + count);
    picker_apply_networking_menu_mode(after_loss.data(), count, lost);
    EXPECT_FALSE(after_loss[kNetworkingMenuJoinIndex].hidden)
        << "a joiner whose session died keeps its JOIN retry";
    EXPECT_TRUE(after_loss[kNetworkingMenuDisconnectIndex].hidden);

    // The session is live: the joiner is in.
    lobby.established.store(true);
    const NetworkingMenuModeState joined =
        picker_current_networking_menu_mode();
    EXPECT_TRUE(joined.networked);
    EXPECT_FALSE(joined.host);
    std::vector<button> session(buttons, buttons + count);
    picker_apply_networking_menu_mode(session.data(), count, joined);
    EXPECT_TRUE(session[kNetworkingMenuJoinIndex].hidden);
    EXPECT_TRUE(session[kNetworkingMenuHostIndex].hidden);
    EXPECT_FALSE(session[kNetworkingMenuDisconnectIndex].hidden);

    // The hosting half never waits on a roster: this machine runs the lobby
    // from the moment host controls are visible.
    lobby.host_view.store(true);
    {
        std::lock_guard<std::mutex> lock(lobby.mutex);
        lobby.players.clear();
    }
    const NetworkingMenuModeState hosting =
        picker_current_networking_menu_mode();
    EXPECT_TRUE(hosting.networked);
    EXPECT_TRUE(hosting.host);
}

namespace {

// A joiner client the picker OWNS (installed through the join factory
// bridge), so the per-frame kicked check may swap it: was_kicked() flips
// when the test raises the shared flag — the picker glue under test is
// picker_revert_lobby_client_if_kicked, not the wire (wave 1 pinned that).
class FakeKickedJoinClient final : public og::ui::IPickerLobbyClient
{
public:
    explicit FakeKickedJoinClient(std::atomic<bool>* kicked_flag)
        : kicked_flag_(kicked_flag)
    {
    }
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    [[nodiscard]] bool was_kicked() const noexcept override
    {
        return kicked_flag_->load();
    }
    [[nodiscard]] std::optional<std::string> connection_alert() const override
    {
        if (kicked_flag_->load())
            return "KICKED BY HOST";
        return std::nullopt;
    }

private:
    std::atomic<bool>* kicked_flag_;
};

struct KickedJoinerRevertState
{
    std::atomic<bool>* kicked_flag = nullptr;
    bool started = false;
    bool finished = false;
    bool joined_session = false;
    bool reverted = false;
    bool popup_seen = false;
    bool go_visible_again = false;
    bool returned_to_main_menu = false;
};

int kicked_joiner_revert_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<KickedJoinerRevertState*>(data);
    state->started = true;

    if (!enter_team_build_from_continue_game())
    {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);
    interact("networking");
    if (!wait_for_interactable("network_join", 10000))
    {
        state->finished = true;
        return 0;
    }
    SDL_Delay(300);
    // JOIN with a blank code: the prompt consumes the queued room code and
    // the bridge factory returns the owned fake join client. Bounded retry:
    // a successful prompt persists the code, so a retried JOIN goes
    // straight to submit.
    state->joined_session = interact_until_any_interactable(
        "network_join", {"networking"}, 15000);
    SDL_Delay(300);

    // The kick lands while parked in Team Build: the per-frame check swaps
    // the owned client for a fresh local one and says why.
    state->kicked_flag->store(true);
    state->reverted = wait_for_trace_contains(
        "networking", "kicked by host", 5000);
    state->popup_seen =
        wait_for_trace_contains("popup", "KICKED BY HOST", 5000);
    state->go_visible_again = wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");
    state->returned_to_main_menu = wait_for_interactable("quit", 10000);
    SDL_Delay(150);
    interact("quit");
    state->finished = true;
    return 0;
}

} // namespace

// The owned-client slot SdlPickerClient installs, exposed for this test only
// (tests/coverage_internal/picker_sdl_client_factory.inc).
void picker_testing_set_lobby_client_owner(
    std::unique_ptr<og::ui::IPickerLobbyClient>* owner);

// picker.cpp's swap seam (external linkage; declared here with every
// parameter spelled out because the defaults live at its own declaration).
bool picker_replace_lobby_client(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client,
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client,
    const char* popup_title,
    bool show_success_popup,
    bool restore_previous_on_failure);

namespace {

// A client that records whether anyone re-ran its initialize_from_save —
// which, for a real join client, is a RECONNECT.
class ReinitCountingLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    // The counters live OUTSIDE the object: a swap that does not roll back
    // destroys the previous client, which is the whole point.
    ReinitCountingLobbyClient(int* init, int* shutdown)
        : init_calls(init), shutdown_calls(shutdown)
    {
    }
    void initialize_from_save() override { ++*init_calls; }
    void shutdown() override { ++*shutdown_calls; }
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }

    int* init_calls;
    int* shutdown_calls;
};

// The fresh client whose initialize_from_save fails.
class ThrowingLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override
    {
        throw std::runtime_error("could not load the local save");
    }
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
};

} // namespace

// LINEUP §6: DISCONNECT (and the kicked revert) tear the network session down
// FIRST and then swap in a fresh local client. If that fresh client's
// initialize_from_save fails, the generic rollback would restore the previous
// client and re-run ITS initialize_from_save — and a join client's
// initialize_from_save reconnects, so the rollback would dial straight back
// into the lobby this machine just left, or into the host that just kicked
// it. Those callers ask for no rollback: they stay local and report.
TEST(NetworkingMenu, disconnect_rollback_never_redials_the_dead_session)
{
    int previous_init = 0;
    int previous_shutdown = 0;
    std::unique_ptr<og::ui::IPickerLobbyClient> owner =
        std::make_unique<ReinitCountingLobbyClient>(&previous_init,
                                                    &previous_shutdown);
    ActiveLobbyClientGuard guard(owner.get());

    auto next = std::make_unique<ThrowingLobbyClient>();
    og::ui::IPickerLobbyClient* const next_raw = next.get();
    EXPECT_THROW(
        (void)picker_replace_lobby_client(owner, std::move(next), "NETWORKING",
                                          /*show_success_popup=*/false,
                                          /*restore_previous_on_failure=*/false),
        std::exception);
    EXPECT_EQ(1, previous_shutdown)
        << "the dead session is still torn down";
    EXPECT_EQ(0, previous_init)
        << "and NOT re-initialized: that is the reconnect";
    EXPECT_EQ(next_raw, owner.get())
        << "the machine stays on the fresh local client";
    EXPECT_EQ(next_raw, og::ui::active_picker_lobby_client());

    // The HOST/JOIN direction keeps its rollback: there the previous client
    // is a live session nobody asked to leave.
    int live_init = 0;
    int live_shutdown = 0;
    std::unique_ptr<og::ui::IPickerLobbyClient> live =
        std::make_unique<ReinitCountingLobbyClient>(&live_init, &live_shutdown);
    og::ui::IPickerLobbyClient* const kept = live.get();
    og::ui::install_active_picker_lobby_client(live.get());
    EXPECT_THROW(
        (void)picker_replace_lobby_client(
            live, std::make_unique<ThrowingLobbyClient>(), "NETWORKING",
            /*show_success_popup=*/false,
            /*restore_previous_on_failure=*/true),
        std::exception);
    EXPECT_EQ(kept, live.get()) << "the live session comes back";
    EXPECT_EQ(1, live_init) << "...re-initialized, which is the point";
}

// LINEUP §6: the kicked revert swaps the ACTIVE lobby client and opens a
// modal. Both are safe at the top of a frame and neither is safe from inside
// a held click: the PROGRESS spin-wait samples the click's coordinates before
// it starts spinning, then polls the lobby and runs the Team Build
// remote-start check on every turn of the loop — so a kick landing mid-hold
// used to swap the client under the pending dispatch and pop a modal that
// eats the very button-up the loop is waiting for. The revert is now deferred
// for the life of the held-click scope and lands on the next top-of-frame
// check.
TEST(NetworkingMenu, kick_revert_waits_for_a_held_click_to_finish)
{
    trace_clear();
    std::atomic<bool> kicked{true};
    std::unique_ptr<og::ui::IPickerLobbyClient> owned =
        std::make_unique<FakeKickedJoinClient>(&kicked);
    og::ui::IPickerLobbyClient* const raw = owned.get();
    ActiveLobbyClientGuard guard(raw);
    picker_testing_set_lobby_client_owner(&owned);

    EXPECT_FALSE(picker_kick_revert_suspended());
    {
        PickerHeldClickScope held_click;
        EXPECT_TRUE(picker_kick_revert_suspended());
        {
            // Nesting must not re-arm the outer scope on the inner's exit.
            PickerHeldClickScope nested;
            EXPECT_TRUE(picker_kick_revert_suspended());
        }
        EXPECT_TRUE(picker_kick_revert_suspended());

        EXPECT_FALSE(picker_revert_lobby_client_if_kicked())
            << "no swap while a click is being held/dispatched";
        EXPECT_EQ(raw, og::ui::active_picker_lobby_client())
            << "the pending click was sampled against THIS client";
        EXPECT_FALSE(trace_contains("popup", "KICKED BY HOST"))
            << "and no modal opened to eat the button-up";
    }

    EXPECT_FALSE(picker_kick_revert_suspended());
    EXPECT_TRUE(picker_revert_lobby_client_if_kicked())
        << "the latched kick lands on the next top-of-frame check";
    EXPECT_NE(raw, og::ui::active_picker_lobby_client());
    EXPECT_TRUE(trace_contains("networking", "kicked by host"));
    EXPECT_TRUE(trace_contains("popup", "KICKED BY HOST"));

    picker_testing_set_lobby_client_owner(nullptr);
}

TEST(NetworkingMenu, kicked_joiner_reverts_to_local_client_in_team_build)
{
    trace_clear();
    level_editor_testing_prompt_queue_clear();
    picker_testing_yes_or_no_queue_clear();
    seed_session_flow_save();

    std::atomic<bool> kicked_flag{false};
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
    bridge.begin_list_relay_rooms = {};
    bridge.list_relay_rooms =
        [](const std::string&, const std::string&)
            -> std::vector<og::ui::PickerRelayRoomInfo> {
        return {};
    };
    bridge.create_join_picker_lobby_client =
        [&kicked_flag](const og::ui::PickerJoinGameOptions&)
            -> std::unique_ptr<og::ui::IPickerLobbyClient> {
        return std::make_unique<FakeKickedJoinClient>(&kicked_flag);
    };
    set_platform_bridge(std::move(bridge));

    level_editor_testing_prompt_queue_push("glad-kick-test");

    KickedJoinerRevertState state;
    state.kicked_flag = &kicked_flag;
    SDL_Thread* thread = SDL_CreateThread(
        kicked_joiner_revert_injector, "networking_kicked_revert", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_TRUE(state.joined_session)
        << "the bridge join client must enter Team Build networked";
    EXPECT_TRUE(state.reverted)
        << "the per-frame Team Build check must notice was_kicked";
    EXPECT_TRUE(state.popup_seen)
        << "the revert says KICKED BY HOST";
    EXPECT_TRUE(state.go_visible_again)
        << "after the revert the local client is active (GO visible)";
    EXPECT_TRUE(state.returned_to_main_menu);
}
