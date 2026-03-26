#include <openglad/core/test_trace.h>
#include <openglad/interface/button.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_ui_state.h>

#include <gtest/gtest.h>
#include <SDL.h>

#if !defined(__EMSCRIPTEN__)
#include <ixwebsocket/IXGetFreePort.h>
#endif

#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "test_interact.h"

void picker_main(Sint32 argc, char** argv);
void picker_cleanup_resources();
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);

static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

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

bool interact_until_label_contains(const std::string& id,
                                   const std::string& expected_substring,
                                   int timeout_ms = 10000)
{
    const Uint32 deadline = SDL_GetTicks() + static_cast<Uint32>(timeout_ms);
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
        if (wait_for_interactable_label_contains(
                id, expected_substring, 1000))
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
    const Uint32 deadline = SDL_GetTicks() + static_cast<Uint32>(timeout_ms);
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
        if (wait_for_trace_contains(category, substring, 1500))
            return true;

        SDL_Delay(100);
    }

    return trace_contains(category, substring);
}

bool interact_until_any_interactable(const std::string& id,
                                     std::initializer_list<const char*> ids,
                                     int timeout_ms = 10000)
{
    const Uint32 deadline = SDL_GetTicks() + static_cast<Uint32>(timeout_ms);
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
        if (wait_for_any_interactable(ids, 1500))
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
    bool stayed_in_submenu_after_join_error = false;
    bool returned_to_main_menu = false;
};

int networking_join_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetworkingJoinState*>(data);
    state->started = true;

    if (!wait_for_interactable("networking", 5000))
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

    SDL_Delay(100);
    state->enabled_room_code = interact_until_label_contains(
        "network_room_toggle", "ON");

    SDL_Delay(100);
    state->updated_room_code = interact_until_label_contains(
        "network_room_value", "glad-xkcd");

    SDL_Delay(150);
    interact("network_join");
    SDL_Delay(250);
    state->stayed_in_submenu_after_join_error =
        has_interactable("network_join") && has_interactable("network_back");

    if (has_interactable("network_back"))
    {
        SDL_Delay(150);
        interact("network_back");
    }

    const Uint32 deadline = SDL_GetTicks() + 10000;
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
    bool set_valid_port = false;
    bool cleared_ip = false;
    bool enabled_room_code = false;
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

    if (!wait_for_interactable("networking", 5000))
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

    state->set_invalid_port = interact_until_label_contains(
        "network_port", "70000");

    SDL_Delay(150);
    interact("network_host");
    SDL_Delay(250);
    state->stayed_after_host_invalid_port =
        has_interactable("network_host") && has_interactable("network_back");

    SDL_Delay(150);
    interact("network_join");
    SDL_Delay(250);
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
    interact("network_join");
    SDL_Delay(250);
    state->stayed_after_blank_ip =
        has_interactable("network_join") && has_interactable("network_back");

    if (!interactable_label_contains("network_room_toggle", "ON"))
    {
        SDL_Delay(100);
    }
    state->enabled_room_code = interact_until_label_contains(
        "network_room_toggle", "ON");

    SDL_Delay(150);
    interact("network_host");
    SDL_Delay(250);
    state->stayed_after_host_relay_error =
        has_interactable("network_host") && has_interactable("network_back");

    if (has_interactable("network_back"))
    {
        SDL_Delay(150);
        interact("network_back");
    }

    const Uint32 deadline = SDL_GetTicks() + 10000;
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

    if (!wait_for_interactable("networking", 5000))
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

    const Uint32 deadline = SDL_GetTicks() + 10000;
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

    if (!wait_for_interactable("networking", 5000))
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
        "network_port", std::to_string(state->port));

    if (interactable_label_contains("network_room_toggle", "ON"))
    {
        SDL_Delay(100);
        (void)interact_until_label_contains("network_room_toggle", "OFF");
    }

    SDL_Delay(150);
    state->entered_team_menu = interact_until_any_interactable(
        "network_host", {"view_team", "go", "back"}, 15000);

    if (state->entered_team_menu && wait_for_interactable("back", 5000))
    {
        SDL_Delay(150);
        interact("back");
    }

    const Uint32 deadline = SDL_GetTicks() + 10000;
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
    save.current_campaign = "org.openglad.gladiator";
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
    ASSERT_TRUE(state.stayed_in_submenu_after_join_error);
    ASSERT_TRUE(state.returned_to_main_menu);
    ASSERT_TRUE(trace_contains("popup", "Relay base URL must use"))
        << "room-code join errors should be surfaced as a popup instead of unwinding the menu";
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
    save.current_campaign = "org.openglad.gladiator";
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
    ASSERT_TRUE(state.set_valid_port);
    ASSERT_TRUE(state.cleared_ip);
    ASSERT_TRUE(state.enabled_room_code);
    ASSERT_TRUE(state.stayed_after_host_invalid_port);
    ASSERT_TRUE(state.stayed_after_join_invalid_port);
    ASSERT_TRUE(state.stayed_after_blank_ip);
    ASSERT_TRUE(state.stayed_after_host_relay_error);
    ASSERT_TRUE(state.returned_to_main_menu);
    ASSERT_TRUE(trace_contains("popup", "Please enter a port from 1 to 65535."));
    ASSERT_TRUE(trace_contains("popup", "Please enter an IP address or hostname."));
    ASSERT_TRUE(trace_contains("popup", "Relay base URL must use"));
}

TEST(NetworkingMenu, host_factory_error_stays_in_submenu)
{
    trace_clear();
    PlatformBridgeGuard bridge_guard;
    PlatformBridge bridge = platform_bridge();
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
    save.current_campaign = "org.openglad.gladiator";
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
    save.current_campaign = "org.openglad.gladiator";
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
