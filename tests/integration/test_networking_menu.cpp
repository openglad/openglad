#include <openglad/core/test_trace.h>
#include <openglad/interface/button.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_ui_state.h>

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
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_interact.h"

void picker_main(Sint32 argc, char** argv);
void picker_cleanup_resources();
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);

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

    SDL_Delay(300);
    interact("networking");

    if (!wait_for_interactable("network_ip", 10000))
    {
        state->finished = true;
        return 0;
    }

    state->saw_networking_menu = true;
    SDL_Delay(150);

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
}
#endif
