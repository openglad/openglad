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
#include <openglad/core/version.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/render/view.h>
//buffers:  using input.h instead #include "int32.h"
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/web_back_key.h>
#include <openglad/core/util.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/sound.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_ui_state.h>

#include <openglad/resources/gparser.h>
#include <openglad/interface/ui/campaign_picker.h>
#include <openglad/interface/ui/level_picker.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/screen_lifecycle.h>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <set>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstdint>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifdef TESTING
#include <atomic>
#endif
// Z's script: #include <process.h>
// Z's script: #include <i86.h> //_enable, _disable

// Stat cost exponent moved to og::ui::kStatCostExponent in picker_common.h

#include "picker_sdl_defs.h"

bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
void popup_dialog(const char* title, const char* message);
void timed_dialog(const char* message, float delay_seconds = 3.0f);

bool prompt_for_string(const std::string& message, std::string& result);
template <typename T, size_t N>
constexpr size_t array_size(const T (&)[N]) noexcept { return N; }

//int matherr (struct exception *);

void show_guy(Sint32 frames, Sint32 who, Sint32 centerx = 80, Sint32 centery = 45); // shows the current guy ..
Sint32 name_guy(Sint32 arg); // rename (or name) the current_guy

void glad_main(Sint32 playermode);
std::string get_saved_name(const char * filename);
Sint32 do_pick_campaign(Sint32 arg1);
Sint32 do_set_scen_level(Sint32 arg1);
bool picker_prepare_new_game_setup();
Sint32 show_general_help();

Sint32 leftmouse(button* buttons);
void draw_highlight_interior(const button& b);
void draw_highlight(const button& b);
bool handle_menu_nav(button* buttons, int& highlighted_button, Sint32& retvalue, bool use_global_vbuttons = true);
bool reset_buttons(vbutton*& local_btns, button* buttons, int num_buttons, Sint32& retvalue);
const char* family_name_copy(short family);

static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

// Flag to signal that game should start (for state machine)
bool g_start_game_requested = false;

bool picker_replace_lobby_client(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client,
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client,
    const char* popup_title,
    bool show_success_popup = true);

namespace {
void destroy_picker_pixie(pixieN* pixie)
{
    delete pixie;
}

PickerPixieHandle make_picker_pixie(const PixieData& data)
{
    PickerPixieHandle handle;
    auto up = std::make_unique<pixieN>(data);
    handle.reset(up.release(), destroy_picker_pixie);
    return handle;
}
} // namespace


#ifdef TESTING
// Test infrastructure for picker_mainmenu_loop
int g_picker_mainmenu_calls = 0;
int g_picker_max_mainmenu_calls = 0;  // 0 = unlimited
// Set true while glad_main is running inside go_menu, so tests can
// wait for the game to finish before clicking menu buttons.
std::atomic<bool> g_test_in_game{false};
// Monotonic counter incremented each time go_menu starts glad_main, so
// injector threads can't miss a fast start+finish transition.
std::atomic<int> g_test_game_epoch{0};
// Number of game frames executed during the current go_menu run.
std::atomic<int> g_test_game_frame_ticks{0};
#endif

void picker_request_start_game()
{
    (void)picker_lobby_request_start();
}

#ifdef TESTING
void picker_testing_mark_game_start()
{
    g_test_game_epoch.fetch_add(1, std::memory_order_release);
    g_test_game_frame_ticks.store(0, std::memory_order_release);
    g_test_in_game.store(true, std::memory_order_release);
}

void picker_testing_mark_frame_advance()
{
    g_test_game_frame_ticks.fetch_add(1, std::memory_order_release);
}

void picker_testing_mark_game_end()
{
    g_test_in_game.store(false, std::memory_order_release);
}
#endif

// allowable_guys moved to og::ui::kAllowableGuys in picker_common.h

enum class PickerInterceptScope
{
    None,
    MainMenu,
    TeamBuild,
};

static inline PickerInterceptScope get_intercept_scope() {
    return static_cast<PickerInterceptScope>(pks().intercept_scope);
}
static inline void set_intercept_scope(PickerInterceptScope s) {
    pks().intercept_scope = static_cast<int>(s);
}

// The mainmenu loop: keeps showing the main menu after submenus return.
// quit() calls exit(0) directly, so we only re-enter here from submenu BACK.
void picker_mainmenu_loop()
{
    while(true) {
        TRACE("picker", "mainmenu_loop_iteration");
        // Menus draw and present on the fixed 320x200 UI canvas. Re-assert it
        // each pass: on Emscripten the Playing->Picker state transition does
        // not unwind glad_main's world-canvas scope.
        if (og::runtime::current_session && og::runtime::current_session->myscreen_)
            og::runtime::current_session->myscreen_->set_active_canvas(CanvasTarget::UI);
        mainmenu(1);
#ifdef TESTING
        g_picker_mainmenu_calls++;
        if (g_picker_max_mainmenu_calls > 0 && g_picker_mainmenu_calls >= g_picker_max_mainmenu_calls)
            return;
#endif
    }
}

static void picker_initialize_shared_menu_state()
{
    clear_allbuttons();

    // Set backdrops to nullptr
    pks().backpics[0] = read_pixie_file("mainul.png");
    pks().backpics[1] = read_pixie_file("mainur.png");
    pks().backpics[2] = read_pixie_file("mainll.png");
    pks().backpics[3] = read_pixie_file("mainlr.png");

    pks().backdrops[0] = make_picker_pixie(pks().backpics[0]);
    pks().backdrops[0]->setxy(0, 0);
    pks().backdrops[1] = make_picker_pixie(pks().backpics[1]);
    pks().backdrops[1]->setxy(160, 0);
    pks().backdrops[2] = make_picker_pixie(pks().backpics[2]);
    pks().backdrops[2]->setxy(0, 100);
    pks().backdrops[3] = make_picker_pixie(pks().backpics[3]);
    pks().backdrops[3]->setxy(160, 100);

    og::runtime::current_session->myscreen_->viewob[0]->resize(PREF_VIEW_FULL);
    og::runtime::current_session->myscreen_->clearbuffer();

    pks().main_title_logo_data = read_pixie_file("title.png"); // marbled gladiator title
    pks().main_title_logo_pix = make_picker_pixie(pks().main_title_logo_data);

    pks().main_columns_data = read_pixie_file("columns.png");
    pks().main_columns_pix = make_picker_pixie(pks().main_columns_data);

    // Get the mouse, timer, & keyboard ..
    grab_mouse();
    grab_timer();
    clear_keyboard();
}

static void picker_load_default_save_if_present()
{
    // The active-company slot defaults to "save0" (§3.4), so legacy flows are
    // byte-identical; only an explicit company selection repoints this.
    const std::string slot_file = og::data::active_company_slot() + ".gtl";
    auto loadgame = og::io::og_open_read("save/", slot_file.c_str());
    if (loadgame)
        og::runtime::current_session->myscreen_->save_data.load(
            og::data::active_company_slot());
}

bool picker_try_intercept_button_action(Sint32 whatfunc, Sint32 call_arg, Sint32& retvalue)
{
    (void)call_arg;
    const ButtonAction action = static_cast<ButtonAction>(whatfunc);
    if (get_intercept_scope() == PickerInterceptScope::MainMenu) {
        const og::ui::PickerMenuItem* menu_item = nullptr;
        switch (action) {
        case ButtonAction::BeginMenu:
            menu_item = og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::BeginNewGame);
            break;
        case ButtonAction::CreateTeamMenu: {
            // §2.1 CONTINUE opens the most-recent company (WP2 startup
            // selection). A corrupt/failed most-recent keeps the loaded save
            // rather than silently switching (§3.5), surfaces a popup, and
            // routes through the LOAD door so the Company List presents the
            // CORRUPT row (restore/delete live there — §2.9 flow 2).
            SaveDataIoError continue_io = SaveDataIoError::None;
            const og::ui::ContinueResult continue_result =
                og::ui::open_most_recent_company(
                    og::runtime::current_session->myscreen_->save_data,
                    &continue_io);
            switch (continue_result) {
            case og::ui::ContinueResult::Corrupt:
                popup_dialog("CONTINUE", "COMPANY FILE DAMAGED");
                menu_item = og::ui::find_picker_menu_item(
                    og::ui::PickerMenuId::Main,
                    og::ui::PickerMenuCommand::LoadGame);
                break;
            case og::ui::ContinueResult::LoadFailed:
                popup_dialog("CONTINUE",
                             og::ui::save_error_string(continue_io));
                menu_item = og::ui::find_picker_menu_item(
                    og::ui::PickerMenuId::Main,
                    og::ui::PickerMenuCommand::LoadGame);
                break;
            case og::ui::ContinueResult::Opened:
                // §2.9 flow 2: the open replaced the in-memory save with the
                // most-recent company — re-seed the local lobby cache from it
                // (the BEGIN NEW GAME pattern). Without this the polled
                // apply_state_to_save keeps rebuilding roster/settings from
                // the BOOT company's cached lobby state and the next §3.8
                // autosave persists that stale state into the opened file.
                picker_lobby_initialize_from_save();
                menu_item = og::ui::find_picker_menu_item(
                    og::ui::PickerMenuId::Main,
                    og::ui::PickerMenuCommand::ContinueGame);
                break;
            case og::ui::ContinueResult::NoCompany:
                menu_item = og::ui::find_picker_menu_item(
                    og::ui::PickerMenuId::Main,
                    og::ui::PickerMenuCommand::ContinueGame);
                break;
            }
            break;
        }
        case ButtonAction::CreateLoadMenu:
            // §2.1 LOAD door: routes through the shared LoadGame command —
            // show_main_menu presents the §2.3 Company List engine screen
            // via show_company_list().
            menu_item = og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::LoadGame);
            break;
        case ButtonAction::MainOptions:
            menu_item = og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::Options);
            break;
        case ButtonAction::ShowHelp:
            menu_item = og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::Help);
            break;
        case ButtonAction::QuitMenu:
            menu_item = og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::Quit);
            break;
        default:
            return false;
        }
        if (!menu_item)
            return false;
        pks().selected_menu_item = menu_item;
        retvalue = MENU_EXIT;
        return true;
    }
    if (get_intercept_scope() == PickerInterceptScope::TeamBuild) {
        if (action == ButtonAction::GoMenu) {
            pks().selected_menu_item = og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::TeamBuild, og::ui::PickerMenuCommand::StartGame);
            retvalue = MENU_EXIT;
            return true;
        }
        if (action == ButtonAction::Networking) {
            pks().selected_menu_item = og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::TeamBuild, og::ui::PickerMenuCommand::Networking);
            retvalue = MENU_EXIT;
            return true;
        }
    }
    return false;
}

namespace {

std::string join_status_lines(const std::vector<std::string>& lines)
{
    std::string message;
    for (const std::string& line : lines) {
        if (line.empty())
            continue;
        if (!message.empty())
            message.append("\n");
        message.append(line);
    }
    return message;
}

constexpr bool picker_default_network_room_code_enabled()
{
    // Relay discovery is the primary path on every platform. Native players
    // can still choose DIRECT (LAN) below the room list by turning it off.
    return true;
}

struct PickerNetworkingSettings
{
    std::string ip_address = "127.0.0.1";
    std::string port_text = "12345";
    bool use_room_code = picker_default_network_room_code_enabled();
    // Empty by default: the field shows a real placeholder instead of a
    // fake GLAD-XXXX value, and JOIN on an empty code opens the active-room
    // prompt (preselecting the first live room).
    std::string room_code;
};

// Live ACTIVE GAMES list state for the NETWORKING subscreen. Network work is
// started asynchronously and polled by configure_networking's menu loop, so a
// slow relay never suspends input or rendering.
struct PickerRelayRoomListState
{
    // `rooms` is exactly the snapshot represented by the currently drawn
    // buttons. A refresh stages its result separately so a tap in the frame a
    // request completes still selects the code the player actually saw, not a
    // new room that happened to reuse its slot.
    std::vector<og::ui::PickerRelayRoomInfo> rooms;
    std::string error;
    bool fetched = false;
    std::uint64_t snapshot_generation = 0;
    std::string snapshot_campaign_tag;
    std::string snapshot_base_url;

    std::vector<og::ui::PickerRelayRoomInfo> pending_rooms;
    std::string pending_error;
    bool update_pending = false;
    std::uint64_t pending_generation = 0;
    std::string pending_campaign_tag;
    std::string pending_base_url;

    std::unique_ptr<og::ui::IPickerRelayRoomListRequest> request;
    std::uint64_t request_generation = 0;
    std::string request_campaign_tag;
    std::string request_base_url;

    // A new generation starts on every menu entry, campaign/base change, or
    // relay-mode toggle. Results from older generations are polled and
    // discarded, never published into the current view.
    std::uint64_t view_generation = 0;
    std::string view_campaign_tag;
    std::string view_base_url;
    std::string view_base_url_error;
    std::uint32_t next_refresh_ms = 0;
};

inline constexpr std::uint32_t kNetworkingRoomRefreshMs = 3000;
inline constexpr std::uint32_t kNetworkingRoomRefreshErrorBackoffMs = 15000;
// Let the first menu frame paint before starting discovery.
inline constexpr std::uint32_t kNetworkingRoomFirstRefreshDelayMs = 400;

#ifdef __EMSCRIPTEN__
EM_JS(void, publish_relay_room_snapshot_js,
      (int generation, int room_count, const char* first_code), {
    window.__opengladRelayRoomSnapshot = {
        generation,
        roomCount: room_count,
        firstRoomCode: UTF8ToString(first_code),
    };
});
#endif

std::string picker_networking_campaign_tag()
{
    if (og::runtime::current_session != nullptr &&
        og::runtime::current_session->myscreen_ != nullptr)
    {
        return og::runtime::current_session->myscreen_->save_data
            .current_campaign;
    }
    return {};
}

bool is_blank_text(std::string_view value)
{
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char c) { return std::isspace(c) != 0; });
}

std::optional<int> parse_network_port(std::string_view port_text)
{
    const std::optional<int> port = parse_int_strict(port_text);
    if (!port.has_value() || *port <= 0 || *port > 65535)
        return std::nullopt;
    return port;
}

bool picker_host_game(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client,
    const og::ui::PickerHostGameOptions& options,
    const char* popup_title)
{
    try
    {
        return picker_replace_lobby_client(
            current_client,
            og::ui::create_host_picker_lobby_client(options),
            popup_title,
            false);
    }
    catch (const std::exception& error)
    {
        popup_dialog(popup_title, error.what());
        return false;
    }
}

bool picker_join_game(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client,
    const og::ui::PickerJoinGameOptions& options,
    const char* popup_title)
{
    try
    {
        return picker_replace_lobby_client(
            current_client,
            og::ui::create_join_picker_lobby_client(options),
            popup_title,
            // Match the host: don't pop a blocking "connected" modal. The join
            // client's live status_lines() ("Status: connecting/connected",
            // "Lobby: N players") render on top of the same lobby menu instead.
            /*show_success_popup=*/false);
    }
    catch (const std::exception& error)
    {
        popup_dialog(popup_title, error.what());
        return false;
    }
}

} // namespace

bool picker_replace_lobby_client(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client,
    std::unique_ptr<og::ui::IPickerLobbyClient> next_client,
    const char* popup_title,
    bool show_success_popup)
{
    if (!next_client)
        return false;

    const bool entering_networked_session =
        next_client->is_networked_session() &&
        (current_client == nullptr ||
         !current_client->is_networked_session());
    if (entering_networked_session)
    {
        // Establish a durable private baseline before network initialization
        // applies shared campaign/team settings. The lobby keeps the roster
        // private in memory; its combined roster lives only in LobbyState and
        // the gameplay handoff. The write goes through the §3.8 choke point
        // (WindowEvent: stamp + atomic, no backup; the save is still fully
        // private here, so the plain default context is correct), and
        // [SAVE-R7] keeps the hard gate: a failed baseline write REFUSES to
        // enter the networked session.
        if (og::runtime::current_session == nullptr ||
            og::runtime::current_session->myscreen_ == nullptr ||
            og::data::company_autosave(
                og::runtime::current_session->myscreen_->save_data,
                og::data::CompanyAutosaveKind::WindowEvent) !=
                SaveDataIoError::None)
        {
            popup_dialog(popup_title,
                         "Could not preserve your local team save.");
            return false;
        }
    }

    std::unique_ptr<og::ui::IPickerLobbyClient> previous_client =
        std::move(current_client);
    const bool previous_was_active =
        og::ui::active_picker_lobby_client() == previous_client.get();
    if (previous_was_active)
        og::ui::install_active_picker_lobby_client(nullptr);

    if (previous_client)
        previous_client->shutdown();

    std::string message;
    try
    {
        next_client->initialize_from_save();
        if (show_success_popup)
            message = join_status_lines(next_client->status_lines());
    }
    catch (...)
    {
        current_client = std::move(previous_client);
        if (previous_was_active)
            og::ui::install_active_picker_lobby_client(current_client.get());
        if (current_client)
            current_client->initialize_from_save();
        throw;
    }

    current_client = std::move(next_client);
    og::ui::install_active_picker_lobby_client(current_client.get());
    if (show_success_popup && !message.empty())
        popup_dialog(popup_title, message.c_str());
    return true;
}

bool picker_join_game(
    std::unique_ptr<og::ui::IPickerLobbyClient>& current_client)
{
    og::ui::PickerJoinGameOptions options;
    const bool direct_connect = yes_or_no_prompt(
        "JOIN GAME",
        "Direct connect?\nChoose NO for a relay room code.",
        true);

    try
    {
        if (direct_connect)
        {
            std::string endpoint = "127.0.0.1:12345";
            if (!prompt_for_string("CONNECT TO IP:PORT", endpoint))
                return false;
            options.mode = og::ui::PickerJoinMode::Direct;
            options.direct_endpoint = endpoint;
        }
        else
        {
            const std::string relay_base_url =
                og::ui::default_relay_base_url();
            std::string campaign_tag;
            if (og::runtime::current_session != nullptr &&
                og::runtime::current_session->myscreen_ != nullptr)
            {
                campaign_tag =
                    og::runtime::current_session->myscreen_->save_data.current_campaign;
            }

            std::vector<og::ui::PickerRelayRoomInfo> active_rooms;
            try
            {
                active_rooms =
                    og::ui::list_relay_rooms(relay_base_url, campaign_tag);
            }
            catch (const std::exception&)
            {
                // Manual entry remains available when discovery is offline.
            }

            // Preselect the first live room so accepting the prompt joins
            // it directly; typing replaces the code as before.
            std::string room_code =
                active_rooms.empty() ? std::string() : active_rooms[0].code;
            // The bitmap prompt renderer is deliberately single-line. The
            // old room-list message contained newlines and overflowed the
            // 320px UI; the full list now lives in tappable NETWORKING rows.
            if (!prompt_for_string("JOIN ROOM CODE", room_code))
                return false;
            options.mode = og::ui::PickerJoinMode::Relay;
            options.room_code = room_code;
            options.relay_base_url = relay_base_url;
        }

        return picker_join_game(current_client, options, "JOIN GAME");
    }
    catch (const std::exception& error)
    {
        popup_dialog("JOIN GAME", error.what());
        return false;
    }
}

class SdlPickerClient final : public og::ui::IPickerClient
{
    // The definition lives in tests/coverage_internal and only exists in
    // TESTING builds.  Friendship exposes state-machine state without adding
    // a test branch to any shipped behavior.
    friend struct PickerSdlClientTestAccess;

public:
    SdlPickerClient()
        : lobby_client_(og::ui::create_local_picker_lobby_client())
    {
        og::ui::install_active_picker_lobby_client(lobby_client_.get());
    }

    ~SdlPickerClient() override
    {
        if (og::ui::active_picker_lobby_client() == lobby_client_.get())
            og::ui::install_active_picker_lobby_client(nullptr);
    }

    void poll_updates() override
    {
        picker_lobby_poll();
    }

    const og::ui::PickerMenuItem* present_menu(og::ui::PickerMenuId menu_id) override
    {
#ifdef TESTING
        if (menu_id == og::ui::PickerMenuId::Main
            && g_picker_max_mainmenu_calls > 0
            && g_picker_mainmenu_calls >= g_picker_max_mainmenu_calls) {
            return og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::Quit);
        }
#endif
        pks().selected_menu_item = nullptr;
        if (menu_id == og::ui::PickerMenuId::Main) {
            set_intercept_scope(PickerInterceptScope::MainMenu);
            mainmenu(1);
            set_intercept_scope(PickerInterceptScope::None);
#ifdef TESTING
            g_picker_mainmenu_calls++;
#endif
            if (pks().selected_menu_item)
                return pks().selected_menu_item;
            return og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::Quit);
        }

        if (menu_id == og::ui::PickerMenuId::Scenario) {
            // The SDL client never dispatches the shared state machine's
            // Scenario menu: the SCENARIO subscreen runs inside the blocking
            // create_team_menu loop (ButtonAction::CreateScenarioMenu), so
            // show_team_build never selects the Scenario item here. The
            // TeamBuild fall-through below would re-enter create_team_menu
            // and nest a second team-build screen — answer the inherited
            // show_submenu(Scenario) loop with a safe no-op Back instead.
            return og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Scenario,
                og::ui::PickerMenuCommand::Back);
        }

        set_intercept_scope(PickerInterceptScope::TeamBuild);
        create_team_menu(0);
        set_intercept_scope(PickerInterceptScope::None);
        if (pks().selected_menu_item)
            return pks().selected_menu_item;
        return og::ui::find_picker_menu_item(
            og::ui::PickerMenuId::TeamBuild, og::ui::PickerMenuCommand::Back);
    }

    bool prepare_new_game() override
    {
        if (!picker_prepare_new_game_setup())
            return false;
        return true;
    }

    bool configure_networking() override
    {
#ifdef __EMSCRIPTEN__
        constexpr std::string_view kRoomCodePlaceholder =
            "(tap to enter room code)";
#else
        constexpr std::string_view kRoomCodePlaceholder = "(enter room code)";
#endif
        text& mytext = og::runtime::current_session->myscreen_->text_normal;
        button* buttons = picker_networking_buttons();
        const auto instruction_lines =
            og::ui::networking_menu_instruction_lines();
        const int num_buttons = picker_networking_button_count();
        int highlighted_button = kNetworkingMenuRoomValueIndex;
        og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);
        clear_keyboard();
        pks().networking_clicked_room_slot = -1;
        begin_relay_room_list_view();

        auto visible_room_count = [&]() -> int {
            if (!networking_settings_.use_room_code ||
                !relay_room_snapshot_is_current())
            {
                return 0;
            }
            return static_cast<int>(std::min(
                relay_rooms_.rooms.size(),
                static_cast<std::size_t>(kNetworkingMenuRoomSlots)));
        };

        auto sync_button_labels = [&]() {
#ifndef __EMSCRIPTEN__
            buttons[kNetworkingMenuIpIndex].label =
                networking_settings_.ip_address.empty()
                    ? "(enter address)"
                    : networking_settings_.ip_address;
            buttons[kNetworkingMenuPortIndex].label =
                networking_settings_.port_text.empty()
                    ? "(enter port)"
                    : networking_settings_.port_text;
            buttons[kNetworkingMenuToggleIndex].label =
                networking_settings_.use_room_code
                    ? "ROOM CODE: ON"
                    : "ROOM CODE: OFF";
#endif
            buttons[kNetworkingMenuRoomValueIndex].label =
                networking_settings_.room_code.empty()
                    ? std::string(kRoomCodePlaceholder)
                    : networking_settings_.room_code;

            const int room_count = visible_room_count();
            for (int slot = 0; slot < kNetworkingMenuRoomSlots; ++slot)
            {
                button& row = buttons[kNetworkingMenuRoomFirstIndex + slot];
                row.hidden = slot >= room_count;
                row.label = slot < room_count
                    ? og::ui::format_relay_room_button_label(
                          relay_rooms_.rooms[static_cast<std::size_t>(slot)],
                          kNetworkingMenuRoomLabelChars)
                    : std::string();
            }
            picker_wire_networking_menu_nav(buttons, num_buttons, room_count);
            if (buttons[highlighted_button].hidden)
                highlighted_button = kNetworkingMenuRoomValueIndex;

            // Both label surfaces: the static rows above and the live
            // vbuttons (labels AND the room rows' hidden flags).
            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(num_buttons);
                 ++i)
            {
                if (og::runtime::current_session->allbuttons_[i] != nullptr)
                {
                    og::runtime::current_session->allbuttons_[i]->label =
                        buttons[i].label;
                    og::runtime::current_session->allbuttons_[i]->hidden =
                        buttons[i].hidden;
                }
            }
        };

        auto reinitialize_buttons = [&]() {
            og::runtime::current_session->localbuttons_ =
                init_buttons(buttons, num_buttons);
            clear_keyboard();
        };

        sync_button_labels();

        Sint32 retvalue = 0;
        while (!(retvalue & MENU_EXIT))
        {
            ensure_relay_room_list_view_is_current();
            // Only a snapshot that was already staged before this frame may
            // be committed after its input dispatch. A completion observed in
            // this frame remains staged until the following frame, preserving
            // the exact labels against which pointer input was sampled.
            const bool room_update_was_pending = relay_rooms_.update_pending;
            poll_relay_room_list_request();
            picker_lobby_poll();
            // Sample and dispatch input before starting any room-list request.
            const bool sampled_pointer_action = leftmouse(buttons) != 0;
            if (sampled_pointer_action)
                retvalue =
                    og::runtime::current_session->localbuttons_->leftclick(
                        buttons);

            handle_menu_nav(buttons, highlighted_button, retvalue);
            if (retvalue == MENU_EXIT)
                break;
            const bool had_menu_action = retvalue != 0;

            switch (static_cast<ButtonAction>(retvalue))
            {
            case ButtonAction::EditNetworkAddress:
                (void)prompt_for_string(
                    "JOIN IP / HOSTNAME",
                    networking_settings_.ip_address);
                reinitialize_buttons();
                break;
            case ButtonAction::EditNetworkPort:
                (void)prompt_for_string(
                    "NETWORK PORT",
                    networking_settings_.port_text);
                reinitialize_buttons();
                break;
            case ButtonAction::ToggleNetworkRoomCode:
                networking_settings_.use_room_code =
                    !networking_settings_.use_room_code;
                begin_relay_room_list_view();
                reinitialize_buttons();
                break;
            case ButtonAction::EditNetworkRoomCode:
            {
                const bool relay_was_enabled =
                    networking_settings_.use_room_code;
                if (prompt_for_string("JOIN ROOM CODE",
                                      networking_settings_.room_code))
                {
                    networking_settings_.use_room_code = true;
                }
                if (networking_settings_.use_room_code != relay_was_enabled)
                    begin_relay_room_list_view();
                reinitialize_buttons();
                break;
            }
            case ButtonAction::SubmitNetworkHost:
                if (submit_network_host())
                {
                    cancel_relay_room_list_request();
                    return true;
                }
                reinitialize_buttons();
                break;
            case ButtonAction::SubmitNetworkJoin:
                if (prompt_for_relay_room_code_if_blank() &&
                    submit_network_join())
                {
                    cancel_relay_room_list_request();
                    return true;
                }
                reinitialize_buttons();
                break;
            case ButtonAction::JoinRelayRoomListEntry:
            {
                const int slot = pks().networking_clicked_room_slot;
                pks().networking_clicked_room_slot = -1;
                if (slot >= 0 && slot < visible_room_count())
                {
                    networking_settings_.use_room_code = true;
                    networking_settings_.room_code =
                        relay_rooms_.rooms[static_cast<std::size_t>(slot)]
                            .code;
                    if (submit_network_join())
                    {
                        cancel_relay_room_list_request();
                        return true;
                    }
                }
                reinitialize_buttons();
                break;
            }
            default:
                break;
            }
            if (!had_menu_action && !sampled_pointer_action)
            {
                // Refresh only on an input-idle frame. Results remain staged
                // until after dispatch, preserving the labels represented by
                // the current button snapshot for the whole click frame.
                refresh_relay_room_list(false);
            }
            retvalue = 0;
            if (room_update_was_pending)
                commit_pending_relay_room_list();
            sync_button_labels();

            draw_backdrop();

            // One enclosing panel frame around the whole menu, sized to contain
            // the longest copy (the centered instruction lines) with margin.
            screen* const myscreen = og::runtime::current_session->myscreen_;
            myscreen->draw_button(
                PICKER_NETWORKING_FRAME_X1, PICKER_NETWORKING_FRAME_Y1,
                PICKER_NETWORKING_FRAME_X2, PICKER_NETWORKING_FRAME_Y2, 1, 1);
            myscreen->draw_button_inverted(
                PICKER_NETWORKING_FRAME_X1 + PICKER_NETWORKING_FRAME_BORDER,
                PICKER_NETWORKING_FRAME_Y1 + PICKER_NETWORKING_FRAME_BORDER,
                static_cast<Uint32>(PICKER_NETWORKING_FRAME_X2 -
                                    PICKER_NETWORKING_FRAME_X1 -
                                    2 * PICKER_NETWORKING_FRAME_BORDER),
                static_cast<Uint32>(PICKER_NETWORKING_FRAME_Y2 -
                                    PICKER_NETWORKING_FRAME_Y1 -
                                    2 * PICKER_NETWORKING_FRAME_BORDER));

            draw_buttons(buttons, num_buttons);

            mytext.write_xy_center(160, PICKER_NETWORKING_TITLE_Y, RED, "NETWORKING");

            const auto draw_field_label = [&](int field_index,
                                              std::string_view label) {
                const button& field = buttons[field_index];
                const Sint32 label_x =
                    field.x - mytext.query_width(label) -
                    PICKER_NETWORKING_LABEL_GAP;
                const Sint32 label_y =
                    field.y + (field.sizey - mytext.sizey) / 2;
                mytext.write_xy(label_x, label_y, label, DARK_BLUE);
            };
            draw_field_label(kNetworkingMenuRoomValueIndex, "ROOM CODE");
#ifndef __EMSCRIPTEN__
            draw_field_label(kNetworkingMenuIpIndex, "JOIN IP / HOST");
            draw_field_label(kNetworkingMenuPortIndex, "PORT");
            mytext.write_xy_center(160, PICKER_NETWORKING_DIRECT_HEADER_Y,
                                   DARK_BLUE, "DIRECT (LAN)");
#endif
            mytext.write_xy_center(160, PICKER_NETWORKING_ROOMS_HEADER_Y,
                                   DARK_BLUE, "ACTIVE GAMES");

            if (visible_room_count() == 0)
            {
                std::string_view rooms_status;
                if (!networking_settings_.use_room_code)
                    rooms_status = "Turn ROOM CODE ON to list relay games.";
                else if (!relay_rooms_.error.empty())
                    rooms_status = "Room list unavailable.";
                else if (relay_rooms_.fetched)
                    rooms_status = "No active games found.";
                else
                    rooms_status = "Looking for active games...";
                mytext.write_xy(
                    160 - mytext.query_width(rooms_status) / 2,
                    PICKER_NETWORKING_ROOM_Y + 2,
                    rooms_status,
                    DARK_BLUE);
            }

            const Sint32 instruction_pitch = mytext.sizey + 1;
            const Sint32 instruction_height =
                instruction_lines.empty()
                    ? 0
                    : mytext.sizey +
                        static_cast<Sint32>(instruction_lines.size() - 1) *
                            instruction_pitch;
            const Sint32 instruction_y =
                buttons[kNetworkingMenuJoinIndex].y -
                PICKER_NETWORKING_INSTRUCTION_GAP - instruction_height;
            for (std::size_t line_index = 0;
                 line_index < instruction_lines.size();
                 ++line_index)
            {
                const std::string_view line = instruction_lines[line_index];
                mytext.write_xy(
                    160 - mytext.query_width(line) / 2,
                    instruction_y +
                        static_cast<Sint32>(line_index) * instruction_pitch,
                    line,
                    DARK_BLUE);
            }

            draw_highlight(buttons[highlighted_button]);
            og::runtime::current_session->myscreen_->buffer_to_screen(
                0, 0, 320, 200);
            og::input_native::sleep_ms(10);
        }

        cancel_relay_room_list_request();
        return false;
    }

    bool host_game() override
    {
        std::string port_text = networking_settings_.port_text;
        if (!prompt_for_string("HOST PORT", port_text))
            return false;

        const std::optional<int> port = parse_network_port(port_text);
        if (!port.has_value())
        {
            popup_dialog("HOST GAME", "Please enter a port from 1 to 65535.");
            return false;
        }

        networking_settings_.port_text = port_text;
        try
        {
            og::ui::PickerHostGameOptions options;
            options.port = *port;
#ifdef __EMSCRIPTEN__
            const bool default_enable_relay = true;
#else
            const bool default_enable_relay = false;
#endif
            options.enable_relay = yes_or_no_prompt(
                "HOST GAME",
                "Create a relay room code too?\n"
                "Choose YES for NAT-friendly hosting.",
                default_enable_relay);
            networking_settings_.use_room_code = options.enable_relay;
            if (options.enable_relay)
                options.relay_base_url = og::ui::default_relay_base_url();
            return picker_host_game(lobby_client_, options, "HOST GAME");
        }
        catch (const std::exception& error)
        {
            popup_dialog("HOST GAME", error.what());
            return false;
        }
    }

    bool join_game() override
    {
        return picker_join_game(lobby_client_);
    }

    std::string show_campaign_select() override
    {
#ifdef TESTING
        // Keep legacy menu-injector tests stable: they script Begin New Game ->
        // Hire menu directly and do not interact with the campaign picker UI.
        return og::runtime::current_session->myscreen_->save_data.current_campaign;
#endif
        CampaignResult result = pick_campaign(&og::runtime::current_session->myscreen_->save_data);
        if (result.id.empty())
            return {};

        og::runtime::current_session->myscreen_->save_data.current_campaign = result.id;
        og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(
            load_campaign(result.id, og::runtime::current_session->myscreen_->save_data.current_levels, result.first_level));
        picker_lobby_sync_settings_from_save();
        return result.id;
    }

    void show_options() override
    {
        main_options();
    }

    void show_help() override
    {
        show_general_help();
    }

    void run_game() override
    {
        go_menu(0);
    }

    bool load_game() override
    {
        // §2.3: the slot menu is retired — loading IS the Company List.
        // (Reached only via run_picker's legacy LoadGame transition; the
        // main-menu LOAD door routes through show_company_list directly.)
        return og::ui::run_company_list_screen();
    }

    bool save_game() override
    {
        // §3.8: the manual save UI is retired — the company autosaves on
        // every base-camp mutation; a legacy SaveGame transition just runs
        // one autosave against the active slot.
        return og::ui::company_autosave_after_mutation(
                   og::runtime::current_session->myscreen_->save_data,
                   picker_lobby_is_networked()) == SaveDataIoError::None;
    }

    bool show_company_list() override
    {
        // §2.3: the LOAD door opens the Company List engine screen. True
        // (a company opened: active slot + save + mount switched) proceeds
        // to team build; false (BACK / list emptied) re-presents the main
        // menu, whose entry refresh re-derives the CONTINUE/LOAD gate.
        return og::ui::run_company_list_screen();
    }

    og::ui::PickerScreen screen_after_game() const override
    {
#ifdef __EMSCRIPTEN__
        if (g_start_game_requested || picker_lobby_start_request_pending())
            return og::ui::PickerScreen::Quit;
#endif
        return og::ui::PickerScreen::TeamBuild;
    }

private:
    bool submit_network_host()
    {
        try
        {
            const std::optional<int> port =
                parse_network_port(networking_settings_.port_text);
            if (!port.has_value())
            {
                popup_dialog("HOST GAME", "Please enter a port from 1 to 65535.");
                return false;
            }

            og::ui::PickerHostGameOptions options;
            options.port = *port;
            options.enable_relay = networking_settings_.use_room_code;
            if (options.enable_relay)
                options.relay_base_url = og::ui::default_relay_base_url();
            return picker_host_game(lobby_client_, options, "HOST GAME");
        }
        catch (const std::exception& error)
        {
            popup_dialog("HOST GAME", error.what());
            return false;
        }
    }

    bool submit_network_join()
    {
        try
        {
            og::ui::PickerJoinGameOptions options;
            if (networking_settings_.use_room_code)
            {
                options.mode = og::ui::PickerJoinMode::Relay;
                options.room_code = networking_settings_.room_code;
                options.relay_base_url = og::ui::default_relay_base_url();
                return picker_join_game(lobby_client_, options, "JOIN GAME");
            }

            const std::optional<int> port =
                parse_network_port(networking_settings_.port_text);
            if (!port.has_value())
            {
                popup_dialog("JOIN GAME", "Please enter a port from 1 to 65535.");
                return false;
            }
            if (is_blank_text(networking_settings_.ip_address))
            {
                popup_dialog("JOIN GAME", "Please enter an IP address or hostname.");
                return false;
            }

            options.mode = og::ui::PickerJoinMode::Direct;
            options.direct_endpoint = std::format(
                "{}:{}",
                networking_settings_.ip_address,
                *port);
            return picker_join_game(lobby_client_, options, "JOIN GAME");
        }
        catch (const std::exception& error)
        {
            popup_dialog("JOIN GAME", error.what());
            return false;
        }
    }

    void clear_pending_relay_room_list()
    {
        relay_rooms_.pending_rooms.clear();
        relay_rooms_.pending_error.clear();
        relay_rooms_.update_pending = false;
        relay_rooms_.pending_generation = 0;
        relay_rooms_.pending_campaign_tag.clear();
        relay_rooms_.pending_base_url.clear();
    }

    void cancel_relay_room_list_request()
    {
        // Destruction is the platform cancellation contract: Wasm aborts and
        // removes its JS registry entry; native drops the UI state while the
        // bounded worker owner safely finishes/reaps the HTTP operation.
        relay_rooms_.request.reset();
        relay_rooms_.request_generation = 0;
        relay_rooms_.request_campaign_tag.clear();
        relay_rooms_.request_base_url.clear();
    }

    void begin_relay_room_list_view()
    {
        // A view replacement must not be serialized behind an obsolete
        // request (menu re-entry, campaign/base change, or relay toggle).
        cancel_relay_room_list_request();
        ++relay_rooms_.view_generation;
        relay_rooms_.view_campaign_tag = picker_networking_campaign_tag();
        try
        {
            relay_rooms_.view_base_url = og::ui::default_relay_base_url();
            relay_rooms_.view_base_url_error.clear();
        }
        catch (const std::exception& error)
        {
            relay_rooms_.view_base_url.clear();
            relay_rooms_.view_base_url_error = error.what();
        }
        catch (...)
        {
            relay_rooms_.view_base_url.clear();
            relay_rooms_.view_base_url_error =
                "Relay base URL is invalid.";
        }

        relay_rooms_.rooms.clear();
        relay_rooms_.error.clear();
        relay_rooms_.fetched = false;
        relay_rooms_.snapshot_generation = 0;
        relay_rooms_.snapshot_campaign_tag.clear();
        relay_rooms_.snapshot_base_url.clear();
        clear_pending_relay_room_list();
#ifdef __EMSCRIPTEN__
        publish_relay_room_snapshot_js(
            static_cast<int>(relay_rooms_.view_generation), 0, "");
#endif
        relay_rooms_.next_refresh_ms = og::input_native::ticks_ms() +
            kNetworkingRoomFirstRefreshDelayMs;
        if (!relay_rooms_.view_base_url_error.empty())
        {
            stage_relay_room_list_error(
                relay_rooms_.view_base_url_error);
        }
    }

    void ensure_relay_room_list_view_is_current()
    {
        const std::string campaign_tag = picker_networking_campaign_tag();
        std::string base_url;
        std::string base_url_error;
        try
        {
            base_url = og::ui::default_relay_base_url();
        }
        catch (const std::exception& error)
        {
            base_url_error = error.what();
        }
        catch (...)
        {
            base_url_error = "Relay base URL is invalid.";
        }
        if (campaign_tag != relay_rooms_.view_campaign_tag ||
            base_url != relay_rooms_.view_base_url ||
            base_url_error != relay_rooms_.view_base_url_error)
        {
            begin_relay_room_list_view();
        }
    }

    bool relay_room_snapshot_is_current() const
    {
        return relay_rooms_.snapshot_generation ==
                   relay_rooms_.view_generation &&
            relay_rooms_.snapshot_campaign_tag ==
                relay_rooms_.view_campaign_tag &&
            relay_rooms_.snapshot_base_url == relay_rooms_.view_base_url;
    }

    void stage_relay_room_list_error(std::string message)
    {
        relay_rooms_.pending_rooms.clear();
        relay_rooms_.pending_error = std::move(message);
        relay_rooms_.pending_generation = relay_rooms_.view_generation;
        relay_rooms_.pending_campaign_tag = relay_rooms_.view_campaign_tag;
        relay_rooms_.pending_base_url = relay_rooms_.view_base_url;
        relay_rooms_.update_pending = true;
        relay_rooms_.next_refresh_ms = og::input_native::ticks_ms() +
            kNetworkingRoomRefreshErrorBackoffMs;
    }

    void poll_relay_room_list_request()
    {
        if (!relay_rooms_.request)
            return;

        std::optional<og::ui::PickerRelayRoomListResult> result;
        try
        {
            result = relay_rooms_.request->poll();
        }
        catch (const std::exception& error)
        {
            const bool request_is_current =
                relay_rooms_.request_generation ==
                    relay_rooms_.view_generation &&
                relay_rooms_.request_campaign_tag ==
                    relay_rooms_.view_campaign_tag &&
                relay_rooms_.request_base_url == relay_rooms_.view_base_url;
            cancel_relay_room_list_request();
            if (request_is_current)
                stage_relay_room_list_error(error.what());
            return;
        }
        catch (...)
        {
            const bool request_is_current =
                relay_rooms_.request_generation ==
                    relay_rooms_.view_generation &&
                relay_rooms_.request_campaign_tag ==
                    relay_rooms_.view_campaign_tag &&
                relay_rooms_.request_base_url == relay_rooms_.view_base_url;
            cancel_relay_room_list_request();
            if (request_is_current)
            {
                stage_relay_room_list_error(
                    "Relay room listing failed while polling.");
            }
            return;
        }
        if (!result.has_value())
            return;

        const std::uint64_t generation = relay_rooms_.request_generation;
        std::string campaign_tag =
            std::move(relay_rooms_.request_campaign_tag);
        std::string base_url = std::move(relay_rooms_.request_base_url);
        cancel_relay_room_list_request();

        // The request may have completed after leaving/re-entering this menu,
        // changing campaigns, or toggling relay mode. Never publish it into a
        // different view; the current view can start its own request below.
        if (!networking_settings_.use_room_code ||
            generation != relay_rooms_.view_generation ||
            campaign_tag != relay_rooms_.view_campaign_tag ||
            base_url != relay_rooms_.view_base_url)
        {
            relay_rooms_.next_refresh_ms = og::input_native::ticks_ms();
            return;
        }

        relay_rooms_.pending_rooms = std::move(result->rooms);
        relay_rooms_.pending_error = std::move(result->error);
        relay_rooms_.pending_generation = generation;
        relay_rooms_.pending_campaign_tag = std::move(campaign_tag);
        relay_rooms_.pending_base_url = std::move(base_url);
        relay_rooms_.update_pending = true;
        relay_rooms_.next_refresh_ms = og::input_native::ticks_ms() +
            (relay_rooms_.pending_error.empty()
                 ? kNetworkingRoomRefreshMs
                 : kNetworkingRoomRefreshErrorBackoffMs);
    }

    // Throttled ACTIVE GAMES refresh. Starting and polling are nonblocking;
    // errors back off harder so an unreachable relay is not hammered.
    void refresh_relay_room_list(bool force)
    {
        if (!networking_settings_.use_room_code || relay_rooms_.request)
            return;
        const std::uint32_t now = og::input_native::ticks_ms();
        if (!force &&
            static_cast<std::int32_t>(now - relay_rooms_.next_refresh_ms) < 0)
        {
            return;
        }
        if (!relay_rooms_.view_base_url_error.empty())
        {
            stage_relay_room_list_error(
                relay_rooms_.view_base_url_error);
            return;
        }
        try
        {
            relay_rooms_.request = og::ui::begin_list_relay_rooms(
                relay_rooms_.view_base_url,
                relay_rooms_.view_campaign_tag);
            if (!relay_rooms_.request)
            {
                stage_relay_room_list_error(
                    "Relay room listing could not be started.");
                return;
            }
            relay_rooms_.request_generation = relay_rooms_.view_generation;
            relay_rooms_.request_campaign_tag =
                relay_rooms_.view_campaign_tag;
            relay_rooms_.request_base_url = relay_rooms_.view_base_url;
        }
        catch (const std::exception& error)
        {
            stage_relay_room_list_error(error.what());
        }
        catch (...)
        {
            stage_relay_room_list_error("Relay room listing failed.");
        }
    }

    void commit_pending_relay_room_list()
    {
        if (!relay_rooms_.update_pending)
            return;
        if (!networking_settings_.use_room_code ||
            relay_rooms_.pending_generation !=
                relay_rooms_.view_generation ||
            relay_rooms_.pending_campaign_tag !=
                relay_rooms_.view_campaign_tag ||
            relay_rooms_.pending_base_url != relay_rooms_.view_base_url)
        {
            clear_pending_relay_room_list();
            return;
        }
        relay_rooms_.rooms = std::move(relay_rooms_.pending_rooms);
        relay_rooms_.error = std::move(relay_rooms_.pending_error);
        relay_rooms_.snapshot_generation =
            relay_rooms_.pending_generation;
        relay_rooms_.snapshot_campaign_tag =
            relay_rooms_.pending_campaign_tag;
        relay_rooms_.snapshot_base_url = relay_rooms_.pending_base_url;
        clear_pending_relay_room_list();
        relay_rooms_.fetched = true;
#ifdef __EMSCRIPTEN__
        publish_relay_room_snapshot_js(
            static_cast<int>(relay_rooms_.snapshot_generation),
            static_cast<int>(relay_rooms_.rooms.size()),
            relay_rooms_.rooms.empty()
                ? ""
                : relay_rooms_.rooms.front().code.c_str());
#endif
    }

    // JOIN with an empty room code in relay mode: open the active-room
    // prompt (same copy as the legacy JOIN flow), preselecting the first
    // live room so a bare "accept" joins it. Returns false to stay in the
    // menu (cancelled or still blank).
    bool prompt_for_relay_room_code_if_blank()
    {
        if (!networking_settings_.use_room_code)
            return true;
        if (!is_blank_text(networking_settings_.room_code))
            return true;

        // Discovery is never allowed to block this prompt. Start a request if
        // needed; a current committed snapshot may prefill the field, while a
        // pending request simply leaves it blank for manual entry.
        if (!relay_rooms_.fetched && !relay_rooms_.update_pending)
            refresh_relay_room_list(true);

        std::string room_code =
            !relay_room_snapshot_is_current() || relay_rooms_.rooms.empty()
            ? std::string()
            : relay_rooms_.rooms[0].code;
        TRACE("networking", "join prompt prefill %s", room_code.c_str());
        if (!prompt_for_string("JOIN ROOM CODE", room_code))
        {
            return false;
        }
        if (is_blank_text(room_code))
            return false;
        networking_settings_.room_code = room_code;
        return true;
    }

    bool replace_lobby_client(std::unique_ptr<og::ui::IPickerLobbyClient> client,
                              const char* popup_title)
    {
        return picker_replace_lobby_client(
            lobby_client_,
            std::move(client),
            popup_title);
    }

    PickerNetworkingSettings networking_settings_;
    PickerRelayRoomListState relay_rooms_;
    std::unique_ptr<og::ui::IPickerLobbyClient> lobby_client_;
};

#ifdef TESTING
#include "../../../tests/coverage_internal/picker_sdl_client_factory.inc"
#endif

void picker_main(Sint32 argc, char  **argv)
{
	(void)argc;
	(void)argv;
	// Get main dir ..
	//strcpy(main_dir, "");
    picker_initialize_shared_menu_state();
    // Load the current saved game, if it exists .. (save0.gtl)
    picker_load_default_save_if_present();
    SdlPickerClient client;
    picker_lobby_initialize_from_save();
    og::ui::run_picker(client);
}

// Centralized picker resource cleanup (no screen destruction).
// Tests and PickerSession use this instead of duplicating cleanup logic.
void picker_cleanup_resources()
{
    picker_lobby_shutdown();

	for (auto& backdrop : pks().backdrops)
    {
        backdrop.reset();
    }

    for (auto& backpic : pks().backpics)
    {
        backpic.free();
    }

    clear_allbuttons();

	pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
    pks().mainmenu_buttons.clear();
    pks().seat_settings_buttons.clear();
    pks().createmenu_buttons.clear();
    pks().main_options_buttons.clear();
    pks().control_options_buttons.clear();
    pks().display_settings_buttons.clear();
    pks().gameplay_fx_options_buttons.clear();
    pks().ui_fx_options_buttons.clear();
    pks().graphics_fx_options_buttons.clear();
    pks().details_buttons.clear();
    pks().trainmenu_buttons.clear();
    pks().hiremenu_buttons.clear();
    pks().networking_buttons.clear();
    pks().teamsmenu_buttons.clear();
    pks().viewscenario_buttons.clear();
    pks().progressmenu_buttons.clear();
    pks().scenariomenu_buttons.clear();
    pks().difficulty_menu_buttons.clear();
    pks().name_entry_buttons.clear();
    pks().company_list_buttons.clear();
    pks().company_backups_buttons.clear();
    pks().cloud_save_buttons.clear();
}

void picker_quit()
{
	picker_cleanup_resources();
    destroy_global_screen();
}

// MAIN MENU: engine-hosted — the four k_mainmenu_buttons build variants
// unified into the MP/no-MP spec pair (web/native = the build-gated
// enabled/disabled-QUIT fork), the USE_TOUCH_INPUT => DISABLE_MULTIPLAYER variant
// selection, the accessor shims, picker_mainmenu_options_index() (which
// retired both OPTIONS_BUTTON_INDEX #defines), and mainmenu() itself all
// live in menu_screen_specs.cpp (docs/menu-engine.md).

// MAIN OPTIONS: engine-hosted — spec, accessor shims, and the entry
// function (which keeps the family-wide cfg-persist epilogue) live in
// menu_screen_specs.cpp (docs/menu-engine.md).

// DISPLAY and CONTROLS subscreens: engine-hosted — specs, accessor shims,
// and the entry functions live in menu_screen_specs.cpp
// (docs/menu-engine.md).

// FX subscreens (GAMEPLAY FX / UI FX / GRAPHICS FX): engine-hosted — specs,
// accessor shims, and the entry functions live in menu_screen_specs.cpp
// (docs/menu-engine.md).

// TEAM BUILD -> BASE CAMP (create_team_menu, design §2.5): engine-hosted —
// spec, accessor shims, and the entry wrapper live in menu_screen_specs.cpp
// (docs/menu-engine.md). The VIEW TEAM screen and the SAVE/LOAD slot menus
// are retired (the roster IS the default view; saving is automatic).

static const button k_details_buttons[] =
    {
        button("back", "BACK", KEYSTATE_ESCAPE, 10, 170, 40, 20, button_action_id(ButtonAction::ReturnMenu) , MENU_EXIT, MenuNav{.up=1, .right=1}),
        button("promote", 160, 4, 315 - 160, 66 - 4, 0 , -1, MenuNav{.down=0, .left=0}, false, true) // PROMOTE
    };

// TRAIN / HIRE: engine-hosted — specs, accessor shims, and the per-frame
// hooks/entry wrappers live in menu_screen_specs.cpp / picker_team_build.cpp
// (docs/menu-engine.md).

// NETWORKING subscreen (relay-first — see the layout contract in
// picker_sdl_defs.h). Static nav encodes the zero-visible-rooms variant;
// configure_networking rewires the graph every frame via
// picker_wire_networking_menu_nav as ACTIVE GAMES rows appear/disappear.
// Action row (BACK | HOST | JOIN), centered inside the panel so every
// control lives within the frame. (BACK previously floated at 10,10,
// outside the panel.)
#ifdef __EMSCRIPTEN__
// Web: relay is the only join path — no direct JOIN IP / PORT rows and no
// ROOM CODE toggle (forced ON).
static const button k_networking_menu_buttons[] =
    {
        button("network_back", "BACK", KEYSTATE_ESCAPE, 41, PICKER_NETWORKING_ACTION_Y, PICKER_NETWORKING_ACTION_WIDTH, BUTTON_HEIGHT, button_action_id(ButtonAction::ReturnMenu), MENU_EXIT, MenuNav{.up=1, .down=1, .right=2}),
        button("network_room_value", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_CODE_X, PICKER_NETWORKING_ROOM_CODE_Y, PICKER_NETWORKING_ROOM_CODE_WIDTH, BUTTON_HEIGHT, button_action_id(ButtonAction::EditNetworkRoomCode), -1, MenuNav{.up=0, .down=2}),
        button("network_host", "HOST", KEYSTATE_UNKNOWN, 123, PICKER_NETWORKING_ACTION_Y, PICKER_NETWORKING_ACTION_WIDTH, BUTTON_HEIGHT, button_action_id(ButtonAction::SubmitNetworkHost), -1, MenuNav{.up=1, .left=0, .right=3}),
        button("network_join", "JOIN", KEYSTATE_UNKNOWN, 205, PICKER_NETWORKING_ACTION_Y, PICKER_NETWORKING_ACTION_WIDTH, BUTTON_HEIGHT, button_action_id(ButtonAction::SubmitNetworkJoin), -1, MenuNav{.up=1, .down=1, .left=2}),
        button("network_room_0", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 0 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 0, MenuNav{}, true),
        button("network_room_1", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 1 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 1, MenuNav{}, true),
        button("network_room_2", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 2 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 2, MenuNav{}, true),
        button("network_room_3", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 3 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 3, MenuNav{}, true),
        button("network_room_4", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 4 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 4, MenuNav{}, true),
    };
#else
// Native: ROOM CODE + ACTIVE GAMES lead; the direct JOIN IP / PORT rows keep
// their historical positional indices (1/2/3) but sit below the list under
// the DIRECT (LAN) header.
static const button k_networking_menu_buttons[] =
    {
        button("network_back", "BACK", KEYSTATE_ESCAPE, 41, PICKER_NETWORKING_ACTION_Y, PICKER_NETWORKING_ACTION_WIDTH, BUTTON_HEIGHT, button_action_id(ButtonAction::ReturnMenu), MENU_EXIT, MenuNav{.up=2, .down=4, .right=5}),
        button("network_ip", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_FIELD_X, PICKER_NETWORKING_DIRECT_IP_Y, PICKER_NETWORKING_FIELD_WIDTH, PICKER_NETWORKING_DIRECT_FIELD_H, button_action_id(ButtonAction::EditNetworkAddress), -1, MenuNav{.up=4, .down=2}),
        button("network_port", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_FIELD_X, PICKER_NETWORKING_DIRECT_ROW2_Y, PICKER_NETWORKING_PORT_WIDTH, PICKER_NETWORKING_DIRECT_FIELD_H, button_action_id(ButtonAction::EditNetworkPort), -1, MenuNav{.up=1, .down=0, .right=3}),
        button("network_room_toggle", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_TOGGLE_X, PICKER_NETWORKING_DIRECT_ROW2_Y, PICKER_NETWORKING_TOGGLE_WIDTH, PICKER_NETWORKING_DIRECT_FIELD_H, button_action_id(ButtonAction::ToggleNetworkRoomCode), -1, MenuNav{.up=1, .down=6, .left=2}),
        button("network_room_value", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_CODE_X, PICKER_NETWORKING_ROOM_CODE_Y, PICKER_NETWORKING_ROOM_CODE_WIDTH, BUTTON_HEIGHT, button_action_id(ButtonAction::EditNetworkRoomCode), -1, MenuNav{.up=0, .down=1}),
        button("network_host", "HOST", KEYSTATE_UNKNOWN, 123, PICKER_NETWORKING_ACTION_Y, PICKER_NETWORKING_ACTION_WIDTH, BUTTON_HEIGHT, button_action_id(ButtonAction::SubmitNetworkHost), -1, MenuNav{.up=2, .left=0, .right=6}),
        button("network_join", "JOIN", KEYSTATE_UNKNOWN, 205, PICKER_NETWORKING_ACTION_Y, PICKER_NETWORKING_ACTION_WIDTH, BUTTON_HEIGHT, button_action_id(ButtonAction::SubmitNetworkJoin), -1, MenuNav{.up=3, .left=5}),
        button("network_room_0", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 0 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 0, MenuNav{}, true),
        button("network_room_1", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 1 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 1, MenuNav{}, true),
        button("network_room_2", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 2 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 2, MenuNav{}, true),
        button("network_room_3", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 3 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 3, MenuNav{}, true),
        button("network_room_4", "", KEYSTATE_UNKNOWN, PICKER_NETWORKING_ROOM_X, PICKER_NETWORKING_ROOM_Y + 4 * PICKER_NETWORKING_ROOM_PITCH, PICKER_NETWORKING_ROOM_WIDTH, PICKER_NETWORKING_ROOM_H, button_action_id(ButtonAction::JoinRelayRoomListEntry), 4, MenuNav{}, true),
    };
#endif


// SAVE / LOAD slot menus: engine-hosted — specs, accessor shims, and
// create_save_menu / create_load_menu live in menu_screen_specs.cpp
// (docs/menu-engine.md).

// MATCHUP subscreen (create_teams_menu): engine-hosted — the spec, accessor
// shim, and entry wrapper live in menu_screen_specs.cpp; the per-frame
// machinery (compute/sync/draw hooks) lives in picker_team_build.cpp
// (docs/menu-engine.md).

// VIEW LEVEL (create_view_scenario_menu) and PROGRESS (create_progress_menu):
// engine-hosted — specs, accessor shims, and the per-frame hooks/entry
// wrappers live in menu_screen_specs.cpp / picker_team_build.cpp
// (docs/menu-engine.md).

// SCENARIO subscreen (create_scenario_menu): engine-hosted — the spec,
// accessor shim, title-strip content pass, and entry wrapper live in
// menu_screen_specs.cpp (docs/menu-engine.md).

// DIFFICULTY subscreen: engine-hosted — spec, accessor shim, and
// run_difficulty_menu live in menu_screen_specs.cpp (docs/menu-engine.md).

namespace
{
template <std::size_t N>
void reset_mutable_button_layout(std::vector<button>& out, const button (&defaults)[N])
{
    out.assign(defaults, defaults + N);
}
} // namespace

// picker_createmenu_buttons()/picker_createmenu_button_count():
// engine-hosted screen — the D3 materialization shims live in
// menu_screen_specs.cpp.

button* picker_details_buttons()
{
    reset_mutable_button_layout(pks().details_buttons, k_details_buttons);
    return pks().details_buttons.data();
}

int picker_details_button_count()
{
    return static_cast<int>(pks().details_buttons.size());
}

// picker_trainmenu_buttons()/picker_hiremenu_buttons() (+ counts):
// engine-hosted screens — the D3 materialization shims live in
// menu_screen_specs.cpp.

button* picker_networking_buttons()
{
    reset_mutable_button_layout(pks().networking_buttons, k_networking_menu_buttons);
    return pks().networking_buttons.data();
}

// NETWORKING subscreen nav graph for `visible_rooms` visible ACTIVE GAMES
// rows. Deterministic rewire (teams-menu pattern): nav never links to a
// hidden room row, and every visible button stays reachable from BACK.
void picker_wire_networking_menu_nav(button* buttons, int count,
                                     int visible_rooms)
{
    if (buttons == nullptr || count < kNetworkingMenuButtonCount)
        return;
    visible_rooms = std::clamp(visible_rooms, 0, kNetworkingMenuRoomSlots);

    for (int index = 0; index < kNetworkingMenuButtonCount; ++index)
        buttons[index].nav = MenuNav{};

    const int first_room =
        visible_rooms > 0 ? kNetworkingMenuRoomFirstIndex : -1;
    const int last_room = visible_rooms > 0
        ? kNetworkingMenuRoomFirstIndex + visible_rooms - 1
        : -1;
    for (int slot = 0; slot < visible_rooms; ++slot)
    {
        const int index = kNetworkingMenuRoomFirstIndex + slot;
        buttons[index].nav.up = slot == 0
            ? kNetworkingMenuRoomValueIndex
            : index - 1;
        buttons[index].nav.down = slot + 1 < visible_rooms
            ? index + 1
            : -1; // patched to the section below the list per build
    }

#ifdef __EMSCRIPTEN__
    // room_value -> rooms -> action row (BACK | HOST | JOIN) -> wrap.
    buttons[kNetworkingMenuRoomValueIndex].nav.up = kNetworkingMenuBackIndex;
    buttons[kNetworkingMenuRoomValueIndex].nav.down =
        first_room >= 0 ? first_room : kNetworkingMenuHostIndex;
    if (last_room >= 0)
        buttons[last_room].nav.down = kNetworkingMenuHostIndex;

    const int above_actions =
        last_room >= 0 ? last_room : kNetworkingMenuRoomValueIndex;
    buttons[kNetworkingMenuBackIndex].nav.up = above_actions;
    buttons[kNetworkingMenuBackIndex].nav.down = kNetworkingMenuRoomValueIndex;
    buttons[kNetworkingMenuBackIndex].nav.right = kNetworkingMenuHostIndex;
    buttons[kNetworkingMenuHostIndex].nav.up = above_actions;
    buttons[kNetworkingMenuHostIndex].nav.left = kNetworkingMenuBackIndex;
    buttons[kNetworkingMenuHostIndex].nav.right = kNetworkingMenuJoinIndex;
    buttons[kNetworkingMenuJoinIndex].nav.up = above_actions;
    buttons[kNetworkingMenuJoinIndex].nav.left = kNetworkingMenuHostIndex;
    buttons[kNetworkingMenuJoinIndex].nav.down = kNetworkingMenuRoomValueIndex;
#else
    // room_value -> rooms -> DIRECT (LAN) fields (ip, then port | toggle)
    // -> action row (BACK | HOST | JOIN) -> wrap.
    buttons[kNetworkingMenuRoomValueIndex].nav.up = kNetworkingMenuBackIndex;
    buttons[kNetworkingMenuRoomValueIndex].nav.down =
        first_room >= 0 ? first_room : kNetworkingMenuIpIndex;
    if (last_room >= 0)
        buttons[last_room].nav.down = kNetworkingMenuIpIndex;

    buttons[kNetworkingMenuIpIndex].nav.up =
        last_room >= 0 ? last_room : kNetworkingMenuRoomValueIndex;
    buttons[kNetworkingMenuIpIndex].nav.down = kNetworkingMenuPortIndex;
    buttons[kNetworkingMenuPortIndex].nav.up = kNetworkingMenuIpIndex;
    buttons[kNetworkingMenuPortIndex].nav.down = kNetworkingMenuBackIndex;
    buttons[kNetworkingMenuPortIndex].nav.right = kNetworkingMenuToggleIndex;
    buttons[kNetworkingMenuToggleIndex].nav.up = kNetworkingMenuIpIndex;
    buttons[kNetworkingMenuToggleIndex].nav.down = kNetworkingMenuJoinIndex;
    buttons[kNetworkingMenuToggleIndex].nav.left = kNetworkingMenuPortIndex;

    buttons[kNetworkingMenuBackIndex].nav.up = kNetworkingMenuPortIndex;
    buttons[kNetworkingMenuBackIndex].nav.down = kNetworkingMenuRoomValueIndex;
    buttons[kNetworkingMenuBackIndex].nav.right = kNetworkingMenuHostIndex;
    buttons[kNetworkingMenuHostIndex].nav.up = kNetworkingMenuPortIndex;
    buttons[kNetworkingMenuHostIndex].nav.left = kNetworkingMenuBackIndex;
    buttons[kNetworkingMenuHostIndex].nav.right = kNetworkingMenuJoinIndex;
    buttons[kNetworkingMenuJoinIndex].nav.up = kNetworkingMenuToggleIndex;
    buttons[kNetworkingMenuJoinIndex].nav.left = kNetworkingMenuHostIndex;
    buttons[kNetworkingMenuJoinIndex].nav.down = kNetworkingMenuRoomValueIndex;
#endif
}

int picker_networking_button_count()
{
    return static_cast<int>(pks().networking_buttons.size());
}

// picker_teamsmenu_buttons()/picker_teamsmenu_button_count(): engine-hosted
// screen — the D3 materialization shims live in menu_screen_specs.cpp.

// picker_viewscenario_buttons()/picker_progressmenu_buttons() (+ counts):
// engine-hosted screens — the D3 materialization shims live in
// menu_screen_specs.cpp.

// picker_scenariomenu_buttons()/picker_scenariomenu_button_count():
// engine-hosted screen — the D3 materialization shims live in
// menu_screen_specs.cpp.

// picker_difficulty_menu_buttons()/picker_difficulty_menu_button_count():
// engine-hosted screen — the D3 materialization shims live in
// menu_screen_specs.cpp.

void view_team(short left, short top, short right, short bottom)
{
	Sint32 text_down = static_cast<Sint32>(top) + 3;
	int i;
	std::string row_message;
	unsigned char namecolor;
	char numguys = 0;
	text& mytext = og::runtime::current_session->myscreen_->text_normal;

	og::runtime::current_session->myscreen_->redrawme = 1;
	og::runtime::current_session->myscreen_->draw_button(left, top, right, bottom, 2, 1);

	mytext.write_xy(left+5, text_down, "  Name  ", static_cast<unsigned char>(BLACK), 1);

	mytext.write_xy(left+80, text_down, "STR  DEX  CON  INT  ARM", static_cast<unsigned char>(BLACK), 1);

	mytext.write_xy(left+230, text_down, "Level", static_cast<unsigned char>(BLACK), 1);

	text_down+=6;

	for(i=0; i < MAX_TEAM_SIZE; i++)
	{
	    auto& ourteam = og::runtime::current_session->myscreen_->save_data.team_list;
		if (ourteam[i])
		{
			numguys++;

			// Pick a nice dark color based on family type
				namecolor = static_cast<unsigned char>(((ourteam[i]->family + 1) << 4) & 255);
				mytext.write_xy(left+5, text_down, ourteam[i]->name.c_str(), static_cast<unsigned char>(namecolor), 1);

				row_message = std::format("{:4d} {:4d} {:4d} {:4d} {:4d}",
				         ourteam[i]->strength, ourteam[i]->dexterity,
				         ourteam[i]->constitution, ourteam[i]->intelligence,
				         ourteam[i]->armor);
				mytext.write_xy(left+70, text_down, row_message.c_str(), static_cast<unsigned char>(BLACK), 1);

				row_message = std::format("{:2d}", ourteam[i]->level);
				mytext.write_xy(left+235, text_down, row_message.c_str(), static_cast<unsigned char>(BLACK), 1);

			mytext.write_xy(left+260, text_down, family_name_copy(ourteam[i]->family), static_cast<unsigned char>(namecolor), 1);

			text_down+=6;
		}
	}
	if (numguys == 0)
	{
		mytext.write_xy(left+80, 60, "*** YOU HAVE NO TEAM! ***", static_cast<unsigned char>(ORANGE_START), 1);
	}

	return;
}



void draw_version_number()
{
	text& mytext = og::runtime::current_session->myscreen_->text_normal;

	og::runtime::current_session->myscreen_->redrawme = 1;
	int w = static_cast<int>(std::string(OPENGLAD_VERSION_STRING).size())*6;
	int h = 8;
	int x = 320 - w - 80;
	int y = 200 - 12;
	og::runtime::current_session->myscreen_->fastbox(x, y, w, h, PURE_BLACK);
	mytext.write_xy(x, y, OPENGLAD_VERSION_STRING, static_cast<unsigned char>(DARK_BLUE), 1);
}



const char* get_family_string(Sint32 family)
{
	return og::ui::family_display_name(family);
}


const char* family_name_copy(short family)
{
	return og::ui::family_short_name(family);
}


void quit(Sint32 arg1)
{
#ifdef TESTING
	(void)arg1;
	TRACE("picker", "quit called (test mode - not exiting)");
#else
		og::runtime::current_session->myscreen_->refresh();

		picker_quit();  // deletes the screen objects
		Log("quit({})\n", arg1);
		exit(0);
#endif
}

// Shared face draw for the FX settings rows: green backing when the setting
// is active, red when it is off, label centered on the face.
static void draw_effect_button_state(button& b, bool active)
{
    if(b.hidden || b.no_draw)
        return;

    if(active)
        og::runtime::current_session->myscreen_->draw_button_colored(b.x-1, b.y-1, b.x + b.sizex, b.y + b.sizey, 1, LIGHT_GREEN);
    else
        og::runtime::current_session->myscreen_->draw_button_colored(b.x-1, b.y-1, b.x + b.sizex, b.y + b.sizey, 1, RED);

    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    mytext.write_xy_center(b.x + b.sizex/2, b.y + b.sizey/2 - 3, DARK_BLUE, "%s", b.label.c_str());
}

void draw_toggle_effect_button(button& b, const std::string& category, const std::string& setting)
{
    draw_effect_button_state(b, cfg.is_on(category, setting));
}

// Cycle-aware variant for the depth selector: the setting is a value string
// ("fog"/"haze"/"mist"/"tint"/"off"), green for everything but off.
void draw_cycle_effect_button(button& b, const std::string& category, const std::string& setting)
{
    draw_effect_button_state(b, og::ui::depth_fx_is_active(cfg.get_setting(category, setting)));
}

void draw_sprite_sheet_button(button& b)
{
    if (b.hidden || b.no_draw)
        return;
    if (cfg.get_setting("graphics", "sprite_sheet").empty())
        return;
    og::runtime::current_session->myscreen_->draw_button_colored(b.x-1, b.y-1, b.x + b.sizex, b.y + b.sizey, 1, LIGHT_GREEN);
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    mytext.write_xy_center(b.x + b.sizex/2, b.y + b.sizey/2 - 3, DARK_BLUE, "%s", b.label.c_str());
}

static std::string get_key_display_name_short(int keycode)
{
    const char* key_name = og::input_native::key_name(keycode);
    std::string sname = key_name ? key_name : "Unknown Key";

    if (sname == "`") return "`";
    if (sname == "Up") return std::string(1, '\x01');
    if (sname == "Down") return std::string(1, '\x02');
    if (sname == "Left") return std::string(1, '\x03');
    if (sname == "Right") return std::string(1, '\x04');
    if (sname == "Left Ctrl") return "LC";
    if (sname == "Right Ctrl") return "RC";
    if (sname == "Left Shift") return "LS";
    if (sname == "Right Shift") return "RS";
    if (sname == "Left Alt") return "LA";
    if (sname == "Right Alt") return "RA";
    if (sname == "Backspace") return "Bk";
    if (sname == "CapsLock") return "Cap";
    if (sname == "Unknown Key") return "--";
    if (sname.size() > 10)
        return sname.substr(0, 9);
    return sname;
}

std::string player_control_key_display_name(int player_index, int key_enum)
{
    if (player_index < 0 || player_index >= 4 ||
        key_enum < 0 || key_enum >= NUM_KEYS)
    {
        return "--";
    }
    const int keycode =
        og::runtime::current_session->player_keys_[player_index][key_enum];
    return keycode == KEYCODE_UNKNOWN
        ? "--"
        : get_key_display_name_short(keycode);
}

std::array<std::string, 2> build_player_control_summary_lines(int player_index, bool remap_mode)
{
    if (player_index < 0 || player_index >= 4)
        return {"", ""};

    const bool eight_dir =
        get_player_control_mode(player_index) == static_cast<int>(ControlDirectionMode::EightDirection);
    const std::string up_s = player_control_key_display_name(player_index, KEY_UP);
    const std::string left_s = player_control_key_display_name(player_index, KEY_LEFT);
    const std::string down_s = player_control_key_display_name(player_index, KEY_DOWN);
    const std::string right_s = player_control_key_display_name(player_index, KEY_RIGHT);
    const std::string yell_s = player_control_key_display_name(player_index, KEY_YELL);
    const std::string fire_s = player_control_key_display_name(player_index, KEY_FIRE);
    const std::string special_s = player_control_key_display_name(player_index, KEY_SPECIAL);
    const std::string special_switch_s =
        player_control_key_display_name(player_index, KEY_SPECIAL_SWITCH);
    const std::string switch_s =
        player_control_key_display_name(player_index, KEY_SWITCH);
    const std::string shifter_s =
        player_control_key_display_name(player_index, KEY_SHIFTER);
    // Unbound look-up (the P4 8-direction default, or user-cleared) shows
    // "--" instead of the empty string SDL names keycode 0 with.
    const std::string lookup_s =
        player_control_key_display_name(player_index, KEY_LOOKUP);
    const std::string action_line = std::format("Y:{} F:{} S:{} SS:{} SW:{} Sh:{} L:{}",
        yell_s, fire_s, special_s, special_switch_s, switch_s, shifter_s, lookup_s);

    if (!eight_dir)
    {
        if (remap_mode)
        {
            return {
                std::format("Dir:{}/{}/{}/{}", up_s, left_s, down_s, right_s),
                action_line
            };
        }
        return {
            std::format("D:{}{}{}{}", up_s, left_s, down_s, right_s),
            action_line
        };
    }

    const std::string up_right_s =
        player_control_key_display_name(player_index, KEY_UP_RIGHT);
    const std::string down_right_s =
        player_control_key_display_name(player_index, KEY_DOWN_RIGHT);
    const std::string down_left_s =
        player_control_key_display_name(player_index, KEY_DOWN_LEFT);
    const std::string up_left_s =
        player_control_key_display_name(player_index, KEY_UP_LEFT);

    if (remap_mode)
    {
        return {
            std::format("Dir:{}/{}/{}/{}/{}/{}/{}/{}",
                up_s, up_right_s, right_s, down_right_s, down_s, down_left_s, left_s, up_left_s),
            action_line
        };
    }

    return {
        std::format("D:{}{}{}{}{}{}{}{}",
            up_s, up_right_s, right_s, down_right_s, down_s, down_left_s, left_s, up_left_s),
        action_line
    };
}

std::string build_player_control_summary(int player_index)
{
    const auto lines = build_player_control_summary_lines(player_index, false);
    if (lines[0].empty() && lines[1].empty())
        return {};
    return std::format("{} {}", lines[0], lines[1]);
}

static void draw_remap_prompt(const std::string& prompt, int player_index)
{
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    og::runtime::current_session->myscreen_->clear_window();
    og::runtime::current_session->myscreen_->draw_button(0, 0, 320, 200, 0);
    og::runtime::current_session->myscreen_->draw_button_inverted(20, 30, 300, 170);
    mytext.write_xy_center(160, 45, RED, "REMAP CONTROLS");
    mytext.write_xy_center(160, 80, DARK_BLUE, "%s", prompt.c_str());
    mytext.write_xy_center(160, 105, DARK_BLUE,
                           og::input::kWebBackKeyMode
                               ? "Press BACKSPACE to keep current key"
                               : "Press ESC to keep current key");
    const auto summary_lines = build_player_control_summary_lines(player_index, true);
    mytext.write_xy_center(160, 150, DARK_BLUE, "%s", summary_lines[0].c_str());
    mytext.write_xy_center(160, 160, DARK_BLUE, "%s", summary_lines[1].c_str());
    og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
}

// Remapping is intentionally a blocking prompt, but a network guest must
// still receive an accepted host GO while waiting for the next key. Returning
// false cancels the prompt sequence; the enclosing engine screen then sees
// the same remote-start flag at its next loop-top check and unwinds normally.
static bool poll_lobby_during_control_remap()
{
    picker_lobby_poll();
    return !g_start_game_requested ||
        !picker_lobby_has_game_start_config();
}

static void remap_player_keys(int player_index)
{
    if (player_index < 0 || player_index >= 4)
        return;

    const bool eight_dir =
        get_player_control_mode(player_index) == static_cast<int>(ControlDirectionMode::EightDirection);
    struct KeyPrompt { int key; const char* label; };
    constexpr KeyPrompt prompts_four[] = {
        {KEY_UP, "UP"},
        {KEY_RIGHT, "RIGHT"},
        {KEY_DOWN, "DOWN"},
        {KEY_LEFT, "LEFT"},
        {KEY_FIRE, "FIRE"},
        {KEY_SPECIAL, "SPECIAL"},
        {KEY_SPECIAL_SWITCH, "SPECIAL SWITCH"},
        {KEY_YELL, "YELL"},
        {KEY_SWITCH, "SWITCH CHARACTER"},
        {KEY_SHIFTER, "SHIFTER"},
        {KEY_LOOKUP, "LOOK UP (HOLD)"},
    };
    constexpr KeyPrompt prompts_eight[] = {
        {KEY_UP, "UP"},
        {KEY_UP_RIGHT, "UP-RIGHT"},
        {KEY_RIGHT, "RIGHT"},
        {KEY_DOWN_RIGHT, "DOWN-RIGHT"},
        {KEY_DOWN, "DOWN"},
        {KEY_DOWN_LEFT, "DOWN-LEFT"},
        {KEY_LEFT, "LEFT"},
        {KEY_UP_LEFT, "UP-LEFT"},
        {KEY_FIRE, "FIRE"},
        {KEY_SPECIAL, "SPECIAL"},
        {KEY_SPECIAL_SWITCH, "SPECIAL SWITCH"},
        {KEY_YELL, "YELL"},
        {KEY_SWITCH, "SWITCH CHARACTER"},
        {KEY_SHIFTER, "SHIFTER"},
        {KEY_LOOKUP, "LOOK UP (HOLD)"},
    };

    const KeyPrompt* prompts = eight_dir ? prompts_eight : prompts_four;
    const size_t prompt_count = eight_dir ? array_size(prompts_eight) : array_size(prompts_four);

    for (size_t idx = 0; idx < prompt_count; ++idx)
    {
        const auto& prompt = prompts[idx];
        draw_remap_prompt(std::format("P{} {}", player_index + 1, prompt.label), player_index);
        if (!assignKeyFromWaitEventPolling(
                player_index, prompt.key, &poll_lobby_during_control_remap))
        {
            break;
        }
    }
}

Sint32 toggle_player_control_mode(Sint32 arg)
{
    const int player_index = static_cast<int>(arg);
    if (player_index < 0 || player_index >= 4)
        return MENU_REDRAW;

    const int current_mode = get_player_control_mode(player_index);
    const int next_mode = (current_mode == static_cast<int>(ControlDirectionMode::EightDirection))
        ? static_cast<int>(ControlDirectionMode::FourDirection)
        : static_cast<int>(ControlDirectionMode::EightDirection);
    set_player_control_mode(player_index, next_mode);
    return MENU_REDRAW;
}

Sint32 edit_player_keymap(Sint32 arg)
{
    remap_player_keys(static_cast<int>(arg));
    return MENU_REDRAW;
}

// main_controls_options(): engine-hosted (menu_screen_specs.cpp drives it
// through run_menu_screen; the legacy loop is gone — docs/menu-engine.md).

// gameplay_fx_options() / ui_fx_options() / graphics_fx_options(): engine-
// hosted (menu_screen_specs.cpp drives them through run_menu_screen; the
// shared legacy loop run_fx_options_screen is gone — docs/menu-engine.md).

// A remote (host) GO while this peer sits on the main menu or one of its
// blocking subscreens: mainmenu()'s caller (present_menu) acts on the
// selected item after the loop exits, so leaving with CONTINUE selected
// routes the shared state machine into team build, whose loop-top
// team_build_remote_start_requested check consumes the start config and
// launches the game.
bool picker_main_scope_remote_start_requested(int32_t& retvalue)
{
    if (!g_start_game_requested || !picker_lobby_has_game_start_config())
        return false;

    pks().selected_menu_item = og::ui::find_picker_menu_item(
        og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::ContinueGame);
    retvalue = MENU_EXIT;
    return true;
}

// run_difficulty_menu(): engine-hosted (menu_screen_specs.cpp drives it
// through run_menu_screen; the legacy loop is gone — docs/menu-engine.md).

// main_options(): engine-hosted (menu_screen_specs.cpp drives it through
// run_menu_screen and keeps the exit epilogue that persists cfg for the
// whole options family; the legacy loop is gone — docs/menu-engine.md).

Sint32 overscan_adjust(Sint32 arg)
{
    og::runtime::current_session->overscan_percentage_ -= static_cast<float>(arg) / 100.0f;
    update_overscan_setting();
    
    return MENU_REDRAW;
}

Sint32 set_player_mode(Sint32 howmany)
{
	Sint32 count = 0;
    g_start_game_requested = false;
    picker_lobby_set_player_mode(howmany);

	while (count < static_cast<Sint32>(og::runtime::current_session->allbuttons_.size())
	       && og::runtime::current_session->allbuttons_[count])
	{
		og::runtime::current_session->allbuttons_[count]->vdisplay();
		count++;
	}
	//buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

	return MENU_OK;
}


//new functions
Sint32 return_menu(Sint32 arg)
{
   return arg;
}

// Character ability descriptions for the detail menu, rendered data-driven.
namespace {

inline constexpr int DETAIL_LM = 11;
inline constexpr int DETAIL_MM = 164;
constexpr int detail_line_y(int x) { return 90 + x * 6; }

struct AbilityBlock {
    int level_req;
    bool right;       // false = left column (DETAIL_LM), true = right (DETAIL_MM)
    int start_line;
    const char* text[8]; // nullptr-terminated
};

static const AbilityBlock soldier_abilities[] = {
    { 1, false, 2, { " Charge", "  Charge causes you to ", "  run forward, damaging", "  anything in your way." } },
    { 4, false, 7, { " Boomerang", "  The boomerang flies  ", "  out in a spiral,     ", "  hurting nearby foes. " } },
    { 7, true, 0, { " Whirl    ", "  The fighter whirls in", "  a spiral, hurting or ", "  stunning melee foes. " } },
    { 10, true, 5, { " Disarm   ", "  Cause a melee foe to ", "  temporarily lose the ", "  strength of attacks. " } },
};

static const AbilityBlock barbarian_abilities[] = {
    { 1, false, 2, { " Hurl Boulder", "  Throw a massive stone", "  boulder at your      ", "  enemies.             " } },
    { 4, false, 7, { " Exploding Boulder", "  Hurl a boulder so hard ", "  that it explodes and   ", "  hits foes all around.  " } },
};

static const AbilityBlock elf_abilities[] = {
    { 1, false, 2, { " Rocks/Forestwalk", "  Rocks hurls a few rocks", "  at the enemy.  Forest- ", "  walk, dexterity-based, ", "  lets you move in trees." } },
    { 4, false, 7, { " More Rocks", "  Like #1, but these    ", "  rocks bounce off walls", "  and other barricades. " } },
    { 7, true, 0, { " Lots of Rocks", "  Like #2, but more     ", "  rocks, with a longer  ", "  thrown range.         " } },
    { 10, true, 5, { " MegaRocks", "  This giant handful of ", "  rocks bounces far away", "  and packs a big punch." } },
};

static const AbilityBlock archer_abilities[] = {
    { 1, false, 2, { " Fire Arrows     ", "  An archer can spin in a", "  circle, firing off a   ", "  ring of flaming bolts. " } },
    { 4, false, 7, { " Barrage   ", "  Rather than a single  ", "  bolt, the archer sends", "  3 deadly bolts ahead. " } },
    { 7, true, 0, { " Exploding Bolt", "  This fatal bolt will  ", "  explode on contact,   ", "  dealing death to all. " } },
};

static const AbilityBlock mage_abilities[] = {
    { 1, false, 2, { " Teleport/Marker ", "  Any mage can teleport  ", "  randomly away easily.  ", "  Leaving a marker for   ", "  anchor requires 75 int." } },
    { 4, false, 7, { " Warp Space", "  Twist the fabric of   ", "  space around you to   ", "  deal death to enemies." } },
    { 7, true, 0, { " Freeze Time   ", "  Freeze time for all   ", "  but your team and kill", "  enemies with ease.    " } },
    { 10, true, 4, { " Energy Wave", "  Send a growing ripple ", "  of energy through     ", "  walls and foes.       " } },
    { 13, true, 8, { " HeartBurst  ", "  Burst your enemies    ", "  into flame. More magic", "  means a bigger effect." } },
};

static const AbilityBlock archmage_abilities[] = {
    { 1, false, 2, { " Teleport/Marker ", "  Any mage can teleport  ", "  randomly away easily.  ", "  Leaving a marker for   ", "  anchor requires 75 int." } },
    { 4, false, 7, { " HeartBurst/Lightning", "  Burst your enemies    ", "  into flame around you.", "  ALT: Chain lightning  ", "  bounces through foes. " } },
    { 7, true, 0, { " Summon Image/Sum. Elem.", "  Summon an illusionary ", "  ally to fight for you.", "  ALT: Summon a daemon, ", "  who uses your stamina." } },
    { 10, true, 5, { " Mind Control", "  Convert nearby foes to", "  your team, for a time." } },
};

static const AbilityBlock cleric_abilities[] = {
    { 1, false, 2, { " Heal            ", "  Heal all teammates who ", "  are close to you, for  ", "  as much as you have SP." } },
    { 4, false, 7, { " Raise/Turn Undead", "  Raise the gore of any ", "  victim to a skeleton. ", "  Alternate (turning)   ", "  requires 65 Int.      " } },
    { 7, true, 0, { " Raise/Turn Ghost", "  A more powerful raise,", "  you can now get ghosts", "  to fly and wail.      " } },
    { 10, true, 5, { " Resurrection", "  The ultimate Healing, ", "  this restores dead    ", "  friends to life, or   ", "  enemies to undead.    ", "  Beware: this will use ", "  your own EXP to cast! " } },
};

static const AbilityBlock druid_abilities[] = {
    { 1, false, 2, { " Plant Tree      ", "  These magical trees    ", "  will resist the enemy, ", "  while allowing friends ", "  to pass.               " } },
    { 4, false, 7, { " Summon Faerie", "  This spell brings to  ", "  you a small flying    ", "  faerie to stun foes.  " } },
    { 7, true, 0, { " Circle of Protection", "  Calls the winds to aid", "  your nearby friends by", "  circling them with a  ", "  shield of moving air. " } },
    { 10, true, 5, { " Reveal   ", "  Gives you a magical   ", "  view to see treasure, ", "  potions, outposts, and", "  invisible enemies.    " } },
};

static const AbilityBlock thief_abilities[] = {
    { 1, false, 2, { " Drop Bomb       ", "  Leave a burning bomb to", "  explode and hurt the   ", "  unwary, friend or foe! " } },
    { 4, false, 7, { " Cloak of Darkness", "  Cloak yourself in the ", "  shadows, slipping past", "  your enemies.         " } },
    { 7, true, 0, { " Taunt Enemies       ", "  Beckon your enemies   ", "  to you with jeers, and", "  confuse their attack. " } },
    { 10, true, 5, { " Poison Cloud", "  Release a cloud of    ", "  poisonous gas to roam ", "  at will and sicken    ", "  your foes.            " } },
};

static const AbilityBlock orc_abilities[] = {
    { 1, false, 2, { " Howl            ", "  Howl in rage, stunning ", "  nearby enemies in their", "  tracks.                " } },
    { 4, false, 7, { " Devour Corpse    ", "  Regain health by      ", "  devouring the corpses ", "  of your foes.         " } },
};

struct FamilyDetail {
    const char* class_name;
    const AbilityBlock* abilities;
    int num_abilities;
};

const FamilyDetail* get_family_detail(int family_id) {
    struct Entry { int id; FamilyDetail detail; };
    static const Entry entries[] = {
        { FAMILY_SOLDIER,   { "soldier",   soldier_abilities,   static_cast<int>(std::size(soldier_abilities)) } },
        { FAMILY_BARBARIAN, { "barbarian", barbarian_abilities, static_cast<int>(std::size(barbarian_abilities)) } },
        { FAMILY_ELF,       { "elf",       elf_abilities,       static_cast<int>(std::size(elf_abilities)) } },
        { FAMILY_ARCHER,    { "archer",    archer_abilities,    static_cast<int>(std::size(archer_abilities)) } },
        { FAMILY_MAGE,      { "Mage",      mage_abilities,      static_cast<int>(std::size(mage_abilities)) } },
        { FAMILY_ARCHMAGE,  { "ArchMage",  archmage_abilities,  static_cast<int>(std::size(archmage_abilities)) } },
        { FAMILY_CLERIC,    { "Cleric",    cleric_abilities,    static_cast<int>(std::size(cleric_abilities)) } },
        { FAMILY_DRUID,     { "Druid",     druid_abilities,     static_cast<int>(std::size(druid_abilities)) } },
        { FAMILY_THIEF,     { "Thief",     thief_abilities,     static_cast<int>(std::size(thief_abilities)) } },
        { FAMILY_ORC,       { "Orc",       orc_abilities,       static_cast<int>(std::size(orc_abilities)) } },
    };
    for (const auto& e : entries) {
        if (e.id == family_id) return &e.detail;
    }
    return nullptr;
}

void render_family_abilities(text& mytext, const guy* g) {
    const FamilyDetail* detail = get_family_detail(g->family);
    if (!detail) return;

    std::string title = std::format("Level {} {} has:", g->level, detail->class_name);
    mytext.write_xy(DETAIL_LM+1, detail_line_y(0)+1, title.c_str(), 10, 1);
    mytext.write_xy(DETAIL_LM, detail_line_y(0), title.c_str(), DARK_BLUE, 1);

    for (int i = 0; i < detail->num_abilities; i++) {
        const auto& ab = detail->abilities[i];
        if (g->level < ab.level_req) continue;
        int x = ab.right ? DETAIL_MM : DETAIL_LM;
        for (int j = 0; j < 8 && ab.text[j]; j++) {
            unsigned char color = (ab.text[j][1] != ' ')
                ? static_cast<unsigned char>(RED) : static_cast<unsigned char>(DARK_BLUE);
            mytext.write_xy(x, detail_line_y(ab.start_line + j), ab.text[j], color, 1);
        }
    }

    // Promotion dialog boxes (mage -> archmage, orc -> orc captain)
    if (g->family == FAMILY_MAGE && g->level >= 6) {
        std::string promo_msg = std::format("Level {} Archmage. This", (g->level-6)/2+1);
        og::runtime::current_session->myscreen_->draw_dialog(158, 4, 315, 66, "Become ArchMage");
        mytext.write_xy(DETAIL_MM, detail_line_y(-10), "Your Mage is now of high", RED, 1);
        mytext.write_xy(DETAIL_MM, detail_line_y(-9), "enough level to become a", RED, 1);
        mytext.write_xy(DETAIL_MM, detail_line_y(-8), promo_msg.c_str(), RED, 1);
        mytext.write_xy(DETAIL_MM, detail_line_y(-7), "change CANNOT be undone!", RED, 1);
        mytext.write_xy(DETAIL_MM, detail_line_y(-6), " Click here to change.  ", RED, 1);
    }
    if (g->family == FAMILY_ORC && g->level >= 6) {
        og::runtime::current_session->myscreen_->draw_dialog(158, 4, 315, 66, "Become Orc Captain");
        mytext.write_xy(DETAIL_MM, detail_line_y(-10), "Your Orc is now of high ", RED, 1);
        mytext.write_xy(DETAIL_MM, detail_line_y(-9), "enough level to become a", RED, 1);
        mytext.write_xy(DETAIL_MM, detail_line_y(-8), "Level 1 Orc Captain. You", RED, 1);
        mytext.write_xy(DETAIL_MM, detail_line_y(-7), "CANNOT undo this action!", RED, 1);
        mytext.write_xy(DETAIL_MM, detail_line_y(-6), " Click here to change.  ", RED, 1);
    }
}

} // namespace

Sint32 create_detail_menu(guy *arg1)
{
   Sint32 retvalue = 0;
   Sint32 start_time = query_timer();

   release_mouse();

	   // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
	button* buttons = picker_details_buttons();
	int num_buttons = picker_details_button_count();
	int highlighted_button = 0;
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

   //leftmouse(buttons);
   //localbuttons->leftclick(buttons);

   while ( !(retvalue & MENU_EXIT) )
   {
       picker_lobby_poll();
       guy* thisguy = arg1;
       if (!thisguy)
       {
           auto& tl = og::runtime::current_session->myscreen_->save_data.team_list;
           const int slot = og::runtime::current_session->editguy_;
           if (slot < 0 || slot >= static_cast<int>(tl.size()))
               return MENU_REDRAW;
           thisguy = tl[slot].get();
       }
       if (!thisguy)
           return MENU_REDRAW;

       const auto* fd = get_family_descriptor(thisguy->family);
       buttons[1].hidden = !(fd && fd->promotes_to >= 0 && thisguy->level >= fd->promotion_level_req);

       show_guy(query_timer()-start_time, 1); // 1 means ourteam[editguy]
    
       bool pressed = handle_menu_nav(buttons, highlighted_button, retvalue);
       
       MouseState& detailmouse = query_mouse();
       bool do_click = false;
       if(leftmouse(buttons))
       {
           do_click = true;
       }
       
       bool do_promote = !buttons[1].hidden && ((do_click && detailmouse.x >= 160 &&
                   detailmouse.x <= 315 &&
                   detailmouse.y >= 4   &&
                   detailmouse.y <= 66) || (pressed && highlighted_button == 1));
       if(do_promote)
       {
           if (fd && fd->promotes_to >= 0 && thisguy->level >= fd->promotion_level_req)
           {
               short new_level = fd->promotion_new_level ? fd->promotion_new_level(thisguy->level) : 1;
               thisguy->upgrade_to_level(new_level);
               thisguy->family = static_cast<unsigned char>(fd->promotes_to);
               // The promotion mutated the REAL team member in place: run
               // the full §3.8 mutation tail — the lobby roster push (every
               // picker menu loop starts with picker_lobby_poll(), which
               // rewrites save.team_list from the lobby's cached roster;
               // without this push the very next poll reverts the promotion,
               // issue #133), the optimistic local ready drop (§4.3 — a
               // networked content change re-readies this machine), and the
               // company autosave (with SAVE retired, a bare sync would lose
               // a promote-then-quit).
               picker_base_camp_after_roster_mutation();
               og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_EXPLODE);
               og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_EXPLODE);
               og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_EXPLODE);
               return MENU_REDRAW;
           }
       }
        
        if(do_click)
            retvalue=og::runtime::current_session->localbuttons_->leftclick(buttons);
       
       
    
        draw_backdrop();

       og::runtime::current_session->myscreen_->draw_button(34,  8, 126, 24, 1, 1);  // name box
       og::runtime::current_session->myscreen_->draw_text_bar(36, 10, 124, 22);
       
       text& mytext = og::runtime::current_session->myscreen_->text_normal;
       if (og::runtime::current_session->current_guy_)
           mytext.write_xy(80 - mytext.query_width(og::runtime::current_session->current_guy_->name.c_str())/2, 14,
                        og::runtime::current_session->current_guy_->name.c_str(),static_cast<unsigned char>(DARK_BLUE), 1);
       og::runtime::current_session->myscreen_->draw_dialog(5, 68, 315, 167, "Character Special Abilities");
       og::runtime::current_session->myscreen_->draw_text_bar(160, 90, 162, 160);

       render_family_abilities(mytext, thisguy);

       show_guy(0, 1);
       
       
       draw_buttons(buttons, num_buttons);
       draw_highlight_interior(buttons[highlighted_button]);
       og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
   }
   return MENU_REDRAW;  // back to edit menu
}





int get_scen_num_from_filename(const char* name)
{
   if(!name)
    return -1;
    
   const char* n = name;
   while(std::isalpha(static_cast<unsigned char>(*n)))
   {
       n++;
   }
   if(*n == '\0')
    return -1;
   else
   {
       const auto parsed = parse_int_prefix(n);
       if (!parsed)
           return -1;
       return *parsed;
   }
}


static void spritesheet_wait_for_mouse_release()
{
    MouseState& ms = query_mouse();
    while (ms.left) {
        get_input_events(true);
        og::input_native::sleep_ms(10);
    }
}


static std::string pick_spritesheet()
{
    std::string extra_dir = get_user_path() + "extra_pix/";
    std::vector<std::string> packs;
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(extra_dir, ec))
            if (entry.is_directory())
                packs.push_back(entry.path().filename().string());
    }
    std::sort(packs.begin(), packs.end());

    std::string selection = cfg.get_setting("graphics", "sprite_sheet");

    constexpr int LIST_X      = 60;
    constexpr int LIST_W      = 200;
    constexpr int ROW_H       = 16;
    constexpr int TITLE_Y     = 12;
    constexpr int LIST_Y      = 32;
    constexpr int BACK_Y      = 178;
    constexpr int VISIBLE_ROWS = (BACK_Y - LIST_Y) / ROW_H;  // 9
    constexpr int SCROLL_H    = VISIBLE_ROWS * ROW_H;
    constexpr int SCROLL_X    = LIST_X + LIST_W + 2;
    constexpr int SCROLL_W    = 8;

    int scroll_top = 0;

    button back_btn[] = {
        button("ss_back", "BACK", KEYSTATE_ESCAPE, 10, BACK_Y, 50, 15,
               button_action_id(ButtonAction::ReturnMenu), MENU_EXIT, MenuNav{})
    };
    int highlighted = 0;
    og::runtime::current_session->localbuttons_ = init_buttons(back_btn, 1);

    while (true) {
        Sint32 ret = 0;
        if (leftmouse(back_btn)) {
            if (og::runtime::current_session->localbuttons_->leftclick() & MENU_EXIT)
                break;
        }
        handle_menu_nav(back_btn, highlighted, ret);
        if (ret & MENU_EXIT)
            break;

        const int total_items = 1 + static_cast<int>(packs.size());
        const bool need_scroll = total_items > VISIBLE_ROWS;

        // Mouse wheel
        short scroll_delta = get_and_reset_scroll_amount();
        if (scroll_delta < 0 && scroll_top + VISIBLE_ROWS < total_items)
            ++scroll_top;
        if (scroll_delta > 0 && scroll_top > 0)
            --scroll_top;

        MouseState& ms = query_mouse();
        if (ms.left) {
            int mx = static_cast<int>(ms.x);
            int my = static_cast<int>(ms.y);

            if (need_scroll && mx >= SCROLL_X && mx < SCROLL_X + SCROLL_W
                    && my >= LIST_Y && my < LIST_Y + SCROLL_H) {
                int thumb_h = std::max(ROW_H, SCROLL_H * VISIBLE_ROWS / total_items);
                int thumb_range = SCROLL_H - thumb_h;
                int thumb_y = LIST_Y + scroll_top * thumb_range / (total_items - VISIBLE_ROWS);
                if (my < thumb_y)
                    scroll_top = std::max(0, scroll_top - 1);
                else if (my >= thumb_y + thumb_h)
                    scroll_top = std::min(total_items - VISIBLE_ROWS, scroll_top + 1);
                spritesheet_wait_for_mouse_release();
            } else if (mx >= LIST_X && mx < LIST_X + LIST_W
                    && my >= LIST_Y && my < LIST_Y + SCROLL_H) {
                int item = scroll_top + (my - LIST_Y) / ROW_H;
                if (item == 0) {
                    selection = "";
                } else if (item - 1 < static_cast<int>(packs.size())) {
                    const std::string& pack = packs[item - 1];
                    selection = (selection == pack) ? "" : pack;
                }
                spritesheet_wait_for_mouse_release();
            }
        }

        screen* scr = og::runtime::current_session->myscreen_;
        text& mytext = scr->text_normal;
        scr->clear_window();
        scr->draw_button(0, 0, 320, 200, 0);
        scr->draw_button_inverted(4, 4, 312, 192);
        mytext.write_xy(LIST_X, TITLE_Y, DARK_BLUE, "Select Sprite Sheet");

        auto draw_row = [&](int x, int y, int w, int h,
                            const std::string& label, bool selected) {
            if (selected)
                scr->draw_button_colored(x-1, y-1, x+w, y+h, 1, LIGHT_GREEN);
            else
                scr->draw_button(x-1, y-1, x+w, y+h, 1);
            mytext.write_xy_center(x + w/2, y + 3, DARK_BLUE, "%s", label.c_str());
        };

        for (int row = 0; row < VISIBLE_ROWS; ++row) {
            int item = scroll_top + row;
            if (item >= total_items) break;
            int ry = LIST_Y + row * ROW_H;
            if (item == 0)
                draw_row(LIST_X, ry, LIST_W, ROW_H, "Standard", selection.empty());
            else
                draw_row(LIST_X, ry, LIST_W, ROW_H, packs[item - 1], selection == packs[item - 1]);
        }
        if (packs.empty())
            mytext.write_xy(LIST_X + 4, LIST_Y + ROW_H + 3, GREY, "No packs in extra_pix/");

        if (need_scroll) {
            int thumb_h = std::max(ROW_H, SCROLL_H * VISIBLE_ROWS / total_items);
            int thumb_range = SCROLL_H - thumb_h;
            int thumb_y = LIST_Y + scroll_top * thumb_range / (total_items - VISIBLE_ROWS);
            scr->draw_button(SCROLL_X - 1, LIST_Y - 1, SCROLL_X + SCROLL_W, LIST_Y + SCROLL_H, 0);
            scr->draw_button_colored(SCROLL_X, thumb_y, SCROLL_X + SCROLL_W - 1, thumb_y + thumb_h, 1, GREY);
        }

        draw_buttons(back_btn, 1);
        scr->buffer_to_screen(0, 0, 320, 200);
        og::input_native::sleep_ms(10);
    }

    return selection;
}

Sint32 do_pick_spritesheet(Sint32)
{
    const std::string old_selection = cfg.get_setting("graphics", "sprite_sheet");
    const std::string new_selection = pick_spritesheet();
    cfg.apply_setting("graphics", "sprite_sheet", new_selection);
    if (!apply_sprite_sheet_setting()) {
        cfg.apply_setting("graphics", "sprite_sheet", old_selection);
        if (apply_sprite_sheet_setting())
            sdl_entity_loader()->reload_graphics();
        popup_dialog("Sprite Sheet", "Could not load\nselected sprite sheet.");
        return MENU_REDRAW;
    }
    sdl_entity_loader()->reload_graphics();
    return MENU_REDRAW;
}

Sint32 do_pick_campaign(Sint32 arg1)
{
	(void)arg1;
   CampaignResult result = pick_campaign(&og::runtime::current_session->myscreen_->save_data);
   if(result.id.size() > 0)
   {
        // Load new campaign
        og::runtime::current_session->myscreen_->save_data.current_campaign = result.id;
        og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(load_campaign(result.id, og::runtime::current_session->myscreen_->save_data.current_levels, result.first_level));
        picker_lobby_sync_settings_from_save();
   }
   return MENU_REDRAW;
}

Sint32 do_set_scen_level(Sint32 arg1)
{
	(void)arg1;
   Sint32 templevel = og::runtime::current_session->myscreen_->save_data.scen_num;
   
   templevel = pick_level(og::runtime::current_session->myscreen_, og::runtime::current_session->myscreen_->world().id);
   
   // Have some feedback if the level changed
   if(templevel != og::runtime::current_session->myscreen_->world().id)
   {
       int old_id = og::runtime::current_session->myscreen_->world().id;
       og::runtime::current_session->myscreen_->world().id = templevel;
       if (templevel < 0 || !og::runtime::current_session->myscreen_->load_level())
       {
            og::runtime::current_session->myscreen_->clearbuffer();
            popup_dialog("Load Failed", "Invalid level file.");
            
           og::runtime::current_session->myscreen_->world().id = old_id;
           if(!og::runtime::current_session->myscreen_->load_level())
           {
                og::runtime::current_session->myscreen_->clearbuffer();
                popup_dialog("Big problem", "Also failed to reload current level...");
           }
       }
       else  // We're good
       {
           og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(templevel);
           picker_lobby_sync_settings_from_save();
           Log("Set level to {}\n", templevel);
       }
   }

   return MENU_REDRAW;
}

/*
int matherr(struct exception *problem)
{
 // Do nothing
 return 0;
}
*/

// §3.8 settings tail (hook inventory: "difficulty/CTF setting callbacks —
// picker_lobby_sync_settings_from_save() + company_autosave(BaseCampMutation)"):
// persisted match settings autosave like any other base-camp mutation, so a
// toggled setting survives QUIT (§0.3 "saving is automatic"). Gated on the
// active company FILE existing: BEGIN NEW GAME's creation write is the only
// company creator (§2.2), so a settings tweak from the pre-company main menu
// never conjures an empty company. Networked lobbies take the [SAVE-F1]
// owner-preserving merge write (session settings stay session-scoped there
// by contract). Failures log inside company_autosave; settings cycling never
// blocks on a failed autosave (§3.8 — callers surface but don't crash).
static void picker_settings_autosave()
{
   if (!user_file_exists("save/" + og::data::active_company_slot() + ".gtl"))
       return;
   (void)og::ui::company_autosave_after_mutation(
       og::runtime::current_session->myscreen_->save_data,
       picker_lobby_is_networked());
}

// Refresh a DIFFICULTY-subscreen settings button's label in both the live
// vbutton array and the mutable descriptor row that backs later redraws.
static void refresh_difficulty_menu_button_label(int button_index,
                                                 const std::string& label)
{
   if (og::runtime::current_session->allbuttons_[button_index] != nullptr)
       og::runtime::current_session->allbuttons_[button_index]->label = label;
   if (static_cast<int>(pks().difficulty_menu_buttons.size()) > button_index)
       pks().difficulty_menu_buttons[button_index].label = label;
}

Sint32 set_difficulty()
{
   og::runtime::current_session->current_difficulty_ = og::ui::cycle_difficulty(og::runtime::current_session->current_difficulty_);
   const auto percent =
       static_cast<short>(og::ui::difficulty_percent(og::runtime::current_session->current_difficulty_));
   if (og::runtime::current_session->game_.world != nullptr)
       og::runtime::current_session->game_.world->difficulty = percent;
   if (og::runtime::current_session->myscreen_ != nullptr)
       og::runtime::current_session->myscreen_->world().difficulty = percent;

   // The cycling row lives in the DIFFICULTY subscreen now (the main-menu
   // DIFFICULTY button is a static-label door into it).
   refresh_difficulty_menu_button_label(
       kDifficultyMenuDifficultyIndex,
       og::ui::format_difficulty_label(og::runtime::current_session->current_difficulty_));

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   return MENU_OK;
}

Sint32 change_teamnum(Sint32 arg)
{
   // Change the team number of the current guy. TRAIN and Base Camp share the
   // saved roster member as their source of truth: the Base Camp TEAM chip
   // already cycles that member directly, so TRAIN must not leave its visible
   // "Playing on Team" choice stranded in the session's temporary stat copy.
   Sint32 current_team;

   // What is our current team number?
   if (!og::runtime::current_session->current_guy_)
       return 0;

   bool roster_changed = false;
   if (pks().train_session && !pks().train_session->empty()) {
       const int slot = pks().train_session->current_slot();
       if (!picker_lobby_save_slot_editable(slot))
           return 0;
       SaveData& save = og::runtime::current_session->myscreen_->save_data;
       const short old_team =
           save.team_list[static_cast<std::size_t>(slot)]->teamnum;
       const short cycled = og::ui::cycle_guy_team(save, slot, arg);
       if (cycled < 0)
           return 0;
       current_team = cycled;
       roster_changed = cycled != old_team;
       pks().train_session->set_team(current_team);
       TRACE("train", "team slot=%d team=%d", slot, current_team);
   } else {
       if (!picker_lobby_save_slot_editable(
               og::runtime::current_session->editguy_))
           return 0;
       current_team = og::runtime::current_session->current_guy_->teamnum;

       // We can be from team 0 (default) to team 3 .. make sure
       // we don't exceed this range.
       current_team += arg;
       current_team = (current_team % 4 + 4) % 4;
   }

   og::runtime::current_session->current_team_num_ = static_cast<short>(current_team);

   // Set our team number ..
   og::runtime::current_session->current_guy_->teamnum = static_cast<short>(current_team);

   // Update our button display
   if (og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex] != nullptr)
       og::runtime::current_session->allbuttons_[kTrainMenuChangeTeamIndex]->label = std::format("Playing on Team {}", current_team + 1);
   if (roster_changed)
       picker_base_camp_after_roster_mutation();
   //allbuttons[18]->do_outline = 1;
   //allbuttons[18]->vdisplay();
   //myscreen->buffer_to_screen(0, 0, 320, 200);

   return MENU_OK;
}

Sint32 change_hire_teamnum(Sint32 arg)
{
   // Change the team number of the hiring menu ..
   int next_team = og::runtime::current_session->current_team_num_ + static_cast<int>(arg);
   next_team = (next_team % 4 + 4) % 4;
   og::runtime::current_session->current_team_num_ = static_cast<short>(next_team);

   // Change our guy, if he exists ..
   if (og::runtime::current_session->current_guy_)
   {
       og::runtime::current_session->current_guy_->teamnum = static_cast<short>(og::runtime::current_session->current_team_num_);
   }

   // Update our button display
   if (og::runtime::current_session->allbuttons_[2] != nullptr)
       og::runtime::current_session->allbuttons_[2]->label = std::format("Hiring for Team {}", og::runtime::current_session->current_team_num_ + 1);

   return MENU_OK;
}

Sint32 change_allied()
{
   // Change our allied mode (on or off)
   //
   // Heritage compatibility: current menus no longer expose this global
   // switch; Base Camp assigns every player seat explicitly. Old saves and
   // replay/wire records still carry allied_mode, so the retired action stays
   // callable for compatibility tests and older integrations.
   og::ui::toggle_allied_mode(og::runtime::current_session->myscreen_->save_data);

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   //buffers: allbuttons[7]->vdisplay();
   //buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

   return MENU_OK;
}

// Refresh a MATCHUP settings button's label in both the live vbutton
// array and the mutable descriptor row that backs later redraws.
static void refresh_teamsmenu_button_label(int button_index,
                                           const std::string& label)
{
   if (og::runtime::current_session->allbuttons_[button_index] != nullptr)
       og::runtime::current_session->allbuttons_[button_index]->label = label;
   if (static_cast<int>(pks().teamsmenu_buttons.size()) > button_index)
       pks().teamsmenu_buttons[button_index].label = label;
}

Sint32 change_ctf_teams()
{
   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   og::ui::cycle_ctf_team_count(save);

   refresh_teamsmenu_button_label(kTeamsMenuCtfTeamsIndex,
                                  og::ui::format_ctf_teams_label(save));

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   return MENU_OK;
}

Sint32 change_ctf_caps()
{
   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   og::ui::cycle_ctf_capture_limit(save);

   refresh_teamsmenu_button_label(kTeamsMenuCtfCapsIndex,
                                  og::ui::format_ctf_caps_label(save));

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   return MENU_OK;
}

Sint32 change_ctf_troops()
{
   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   og::ui::toggle_ctf_scenario_troops(save);

   refresh_teamsmenu_button_label(kTeamsMenuCtfTroopsIndex,
                                  og::ui::format_ctf_troops_label(save));

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   return MENU_OK;
}

// DIFFICULTY-subscreen match-rule callbacks: pure helper, both label
// surfaces, lobby broadcast — the change_ctf_* recipe.
Sint32 change_respawn_mode()
{
   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   og::ui::cycle_respawn_mode(save);

   refresh_difficulty_menu_button_label(kDifficultyMenuRespawnModeIndex,
                                        og::ui::format_respawn_mode_label(save));

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   return MENU_OK;
}

Sint32 change_respawn_delay()
{
   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   og::ui::cycle_respawn_delay(save);

   refresh_difficulty_menu_button_label(kDifficultyMenuRespawnDelayIndex,
                                        og::ui::format_respawn_delay_label(save));

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   return MENU_OK;
}

Sint32 change_permadeath()
{
   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   og::ui::toggle_permadeath(save);

   refresh_difficulty_menu_button_label(kDifficultyMenuPermadeathIndex,
                                        og::ui::format_permadeath_label(save));

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   return MENU_OK;
}

Sint32 change_generator_rate()
{
   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   og::ui::cycle_generator_rate(save);

   refresh_difficulty_menu_button_label(kDifficultyMenuGeneratorRateIndex,
                                        og::ui::format_generator_rate_label(save));

   picker_lobby_sync_settings_from_save();
   picker_settings_autosave();

   return MENU_OK;
}

// Infinite gold is the one DIFFICULTY row that is SESSION-ONLY: the wallet is
// never inflated and infinite_gold is never serialized, so the settings
// autosave tail is deliberately absent (the cross_control precedent). Writing
// the wallet — or persisting the flag — would bake the cheat into the
// player's company file and outlive the toggle.
Sint32 change_infinite_gold()
{
   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   og::ui::toggle_infinite_gold(save);

   refresh_difficulty_menu_button_label(kDifficultyMenuInfiniteGoldIndex,
                                        og::ui::format_infinite_gold_label(save));

   picker_lobby_sync_settings_from_save();

   return MENU_OK;
}

// GRAPHICS FX depth selector: step cfg effects/depth_fx one value. Pure
// cfg, no save/lobby state — main_options() persists cfg on exit like every
// FX toggle. The row's label re-derives from cfg on both surfaces every
// frame via the engine spec's LabelBinding (menu_screen_specs.cpp), so this
// callback no longer writes it (G8).
Sint32 change_depth_fx()
{
   const std::string next =
       og::ui::cycle_depth_fx(cfg.get_setting("effects", "depth_fx"));
   cfg.apply_setting("effects", "depth_fx", next);

   return MENU_OK;
}

// DISPLAY zoom selector: step graphics/zoom through the resource-safe range,
// apply its aspect-relative world canvas (menus use the fixed UI canvas),
// and relayout views when the logical dimensions change. main_options()
// persists cfg on exit. The face re-derives from cfg on both surfaces every
// frame via the engine spec's LabelBinding (which is why the platform's
// reflect-back of an allocation-rejected request shows correctly), so this
// callback no longer writes labels (G8).
Sint32 change_zoom()
{
   screen* scr = og::runtime::current_session->myscreen_;
   const int minimum_steps = scr->minimum_world_zoom_steps();
   const std::string next = og::ui::cycle_zoom(
       cfg.get_setting("graphics", "zoom"), minimum_steps);
   cfg.apply_setting("graphics", "zoom", next);

   const int old_w = scr->world_canvas_w();
   const int old_h = scr->world_canvas_h();
   scr->reapply_world_scale();
   if (scr->world_canvas_w() != old_w || scr->world_canvas_h() != old_h)
       scr->relayout_views();

   return MENU_OK;
}

Sint32 change_smoothing()
{
   const std::string current = og::ui::effective_smoothing_setting(
       cfg.get_setting("graphics", "smoothing"),
       cfg.get_setting("graphics", "render"));
   const std::string next = og::ui::cycle_smoothing(current);
   cfg.apply_setting("graphics", "smoothing", next);

   screen* scr = og::runtime::current_session->myscreen_;
   scr->reapply_world_scale(); // engine-only change: canvas dims are stable

   return MENU_OK;
}

Sint32 change_display_mode()
{
   const og::ui::DisplayMode previous = og::ui::parse_display_mode(
       cfg.get_setting("graphics", "fullscreen"));
	og::ui::DisplayMode requested = og::ui::next_display_mode(previous);
	screen* scr = og::runtime::current_session->myscreen_;
	// Borderless stores the remembered logical Windowed size, but Exclusive
	// accepts only a real enumerated physical mode. Prefer the desktop when it
	// is in that list; otherwise use the largest real mode instead of treating
	// (say) a remembered 640x400 window as a fullscreen request.
	if (previous == og::ui::DisplayMode::Borderless &&
	    requested == og::ui::DisplayMode::Exclusive)
	{
		const auto resolution = og::ui::preferred_exclusive_resolution(
			scr->display_resolutions(), scr->desktop_resolution());
		if (resolution.first >= 640 && resolution.second >= 400)
		{
			cfg.apply_setting("graphics", "width", std::to_string(resolution.first));
			cfg.apply_setting("graphics", "height", std::to_string(resolution.second));
		}
		else
		{
			// A backend with no real fullscreen modes cannot enter Exclusive.
			// Skip it before issuing a window request; this is unambiguous and
			// avoids guessing whether a timed-out real request was rejected.
			requested = og::ui::DisplayMode::Windowed;
		}
	}
	cfg.apply_setting("graphics", "fullscreen",
	                  og::ui::display_mode_cfg_value(requested));

	const int old_w = scr->world_canvas_w();
   const int old_h = scr->world_canvas_h();
   scr->apply_display_settings_from_cfg();
   if (scr->world_canvas_w() != old_w || scr->world_canvas_h() != old_h)
       scr->relayout_views();

	// SDL may reject exclusive fullscreen and leave the window in borderless
   // (or windowed) mode. The platform apply normalizes cfg to that real
   // state, and the engine spec's LabelBinding reads cfg back every frame,
	// so the button never displays the request (label writes removed — G8).
	return MENU_OK;
}

// The resolution lap uses desktop-derived window sizes only outside exclusive
// fullscreen. Exclusive must never advertise a synthetic fraction, cfg size,
// or desktop size that SDL did not enumerate as a real video mode.
static std::vector<std::pair<int, int>> resolution_choices()
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

// The face ("Res: WxH" / the borderless desktop mode) re-derives from cfg
// on both surfaces every frame via the engine spec's LabelBinding
// (active_resolution_label in menu_screen_specs.cpp), so this callback no
// longer writes labels (G8).
Sint32 change_resolution()
{
   if (og::ui::parse_display_mode(cfg.get_setting("graphics", "fullscreen")) ==
       og::ui::DisplayMode::Borderless)
   {
       // There is no alternate resolution to apply in borderless desktop
       // mode. Leave the remembered window size alone and keep the face
       // truthful; users can select a mode after choosing Windowed or
       // Fullscreen.
       return MENU_OK;
   }

   const std::pair<int, int> next = og::ui::next_resolution(
       resolution_choices(),
       cfg.get_setting("graphics", "width"),
       cfg.get_setting("graphics", "height"));
   cfg.apply_setting("graphics", "width", std::to_string(next.first));
   cfg.apply_setting("graphics", "height", std::to_string(next.second));

   screen* scr = og::runtime::current_session->myscreen_;
   const int old_w = scr->world_canvas_w();
   const int old_h = scr->world_canvas_h();
   scr->apply_display_settings_from_cfg();
   if (scr->world_canvas_w() != old_w || scr->world_canvas_h() != old_h)
       scr->relayout_views();

#ifdef TESTING
   const std::pair<int, int> normalized = og::ui::parse_resolution(
       cfg.get_setting("graphics", "width"),
       cfg.get_setting("graphics", "height"));
   const int actual_w = static_cast<int>(
       og::runtime::current_session->window_w_);
   const int actual_h = static_cast<int>(
       og::runtime::current_session->window_h_);
   TRACE("display_resolution_apply",
         "requested=%dx%d normalized=%dx%d actual=%dx%d min_zoom=%d match=%d",
         next.first, next.second, normalized.first, normalized.second,
         actual_w, actual_h, scr->minimum_world_zoom_steps(),
         next == normalized && actual_w == normalized.first &&
             actual_h == normalized.second);
#endif

   return MENU_OK;
}

// display_settings_options(): engine-hosted (menu_screen_specs.cpp drives
// it through run_menu_screen; the legacy loop and its platform hide/rewire
// block are gone — the spec's Rewire program carries the platform fork.
// docs/menu-engine.md).

Sint32 teams_join_team(Sint32 team)
{
   if (team < 0 || team >= MAX_PLAYERS)
       return MENU_OK;

   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   if (!picker_lobby_is_networked() &&
       !og::ui::team_has_members(save, static_cast<short>(team)))
   {
       popup_dialog("MATCHUP", "NO HEROES ON\nTHIS TEAM");
       return MENU_OK;
   }

   if (!picker_lobby_request_team_change(static_cast<short>(team)))
       popup_dialog("MATCHUP", "CHANGE DENIED");

   return MENU_OK;
}

// Advance the retired MATCHUP roster cursor to the next occupied
// slot in `whichway` (+1/-1), wrapping. Returns the new slot, or -1 when the
// roster is empty.
static int advance_teams_menu_guy_slot(const SaveData& save, int whichway)
{
   if (save.team_size <= 0)
       return -1;

   const int slot_count = static_cast<int>(save.team_list.size());
   int slot = std::clamp(pks().teams_menu_guy_slot, 0, slot_count - 1);
   for (int step = 0; step < slot_count; ++step)
   {
       slot = ((slot + whichway) % slot_count + slot_count) % slot_count;
       if (save.team_list[slot])
           return slot;
   }
   return -1;
}

// The selected slot, normalized onto an occupied roster slot (-1 when empty).
int teams_menu_selected_guy_slot()
{
   const SaveData& save = og::runtime::current_session->myscreen_->save_data;
   if (save.team_size <= 0)
       return -1;

   const int slot_count = static_cast<int>(save.team_list.size());
   const int slot = std::clamp(pks().teams_menu_guy_slot, 0, slot_count - 1);
   if (save.team_list[slot])
       return slot;
   return advance_teams_menu_guy_slot(save, 1);
}

Sint32 teams_cycle_guy(Sint32 whichway)
{
   const SaveData& save = og::runtime::current_session->myscreen_->save_data;
   const int slot = advance_teams_menu_guy_slot(save, whichway >= 0 ? 1 : -1);
   if (slot >= 0)
       pks().teams_menu_guy_slot = slot;
   return MENU_OK;
}

Sint32 teams_cycle_guy_team(Sint32 whichway)
{
   // Per-character moves are a local-session surface; the button stays hidden
   // in networked lobbies, where roster ownership is server-authoritative.
   if (picker_lobby_is_networked())
       return MENU_OK;

   SaveData& save = og::runtime::current_session->myscreen_->save_data;
   const int slot = teams_menu_selected_guy_slot();
   if (slot < 0)
       return MENU_OK;
   if (!picker_lobby_save_slot_editable(slot))
   {
       popup_dialog("TEAM", "LOCKED");
       return MENU_OK;
   }

   pks().teams_menu_guy_slot = slot;
   if (og::ui::cycle_guy_team(save, slot, static_cast<int>(whichway)) >= 0)
   {
       // §3.8 hook inventory row "team cycle": the cycler mutates the
       // persisted roster (teamnum), so it takes the full mutation tail —
       // solo-only surface (the networked early-return above), so the ready
       // drop inside is a no-op and the autosave is a plain company write.
       picker_base_camp_after_roster_mutation();
   }

   return MENU_OK;
}

// Shared by the retired MATCHUP mirror (origin_button_index < 0) and the
// base-camp READY twin (origin = kCreateMenuReadyIndex, §2.6): both drive the
// same lobby flag. The compatibility mirror refreshes its label by index for
// the same-frame flip; the base-camp twin is re-derived by the engine
// label/color pass the same frame (a cross-screen index write here would
// stamp a roster row instead).
Sint32 teams_toggle_ready(Sint32 origin_button_index)
{
   const bool ready = !picker_lobby_local_ready();
   if (ready)
   {
       // §2.6 client ready gate: cross-control OFF + brought characters +
       // none deployed => popup instead of readying. An active seat with an
       // empty roster bypasses this local deploy check [NET-R9]. A true
       // zero-seat client has no READY action and never reaches this handler.
       const og::ui::ReadyGoPresentation presentation =
           picker_compute_ready_go_presentation();
       if (presentation.state == og::ui::ReadyGoState::ClientUnready &&
           !presentation.caption.empty())
       {
           TRACE("basecamp", "ready_gated");
           popup_dialog(presentation.caption.c_str(),
                        "Deploy at least\none character\nbefore readying");
           return MENU_OK;
       }
   }
   (void)picker_lobby_set_ready(ready);
   if (origin_button_index < 0)
   {
       refresh_teamsmenu_button_label(kTeamsMenuReadyIndex,
                                      ready ? "UNREADY" : "READY");
   }
   return MENU_OK;
}

// MATCHUP per-team detail pager: advance the team's detail slice. The frame
// loop (compute_teams_menu_state) wraps the raw counter onto the current
// page count, so a shrinking roster can never strand the page out of range.
Sint32 teams_page_flip(Sint32 team)
{
   if (team < 0 || team >= 4)
       return MENU_OK;
   pks().teams_menu_team_page[static_cast<std::size_t>(team)] += 1;
   return MENU_OK;
}

#ifdef __EMSCRIPTEN__
// ============================================================================
// Emscripten State Machine Functions
// These functions allow the picker to work with a non-blocking main loop
// ============================================================================

static SdlPickerClient& emscripten_picker_client()
{
    static std::unique_ptr<SdlPickerClient> client;
    if (!client)
        client = std::make_unique<SdlPickerClient>();
    return *client;
}

static void run_picker_state_machine_until_game_requested(
    og::ui::PickerScreen initial_screen = og::ui::PickerScreen::MainMenu)
{
    og::ui::run_picker(emscripten_picker_client(), initial_screen);
}

// Check if game start was requested (called from main after picker_init)
bool picker_check_start_requested()
{
    picker_lobby_poll();
    Log("picker_check_start_requested: g_start_game_requested={}\n", g_start_game_requested);
    if (g_start_game_requested && og::runtime::current_session->myscreen_)
        og::runtime::current_session->myscreen_->save_data.save(
            og::data::active_company_slot());
    return g_start_game_requested;
}

// Initialize the picker (called once at startup from main)
void picker_init()
{
    Log("picker_init: Initializing picker\n");

    picker_initialize_shared_menu_state();
    // Load the current saved game, if it exists
    picker_load_default_save_if_present();

    g_start_game_requested = false;
    (void)emscripten_picker_client();
    picker_lobby_initialize_from_save();
    run_picker_state_machine_until_game_requested();
    Log("picker_init: picker returned, g_start_game_requested={}\n", g_start_game_requested);
}

// Run one frame of the picker - returns true when game should start
bool picker_frame()
{
    picker_lobby_poll();

    // Check if game start was requested
    if (g_start_game_requested) {
        Log("picker_frame: Game start requested\n");
        if (og::runtime::current_session->myscreen_)
            og::runtime::current_session->myscreen_->save_data.save(
                og::data::active_company_slot());
        g_start_game_requested = false;
        return true;  // Signal to transition to PLAYING state
    }

    return false;
}

// Prepare for transitioning to game (cleanup only, initialization done by state machine)
void picker_cleanup_for_game()
{
    Log("picker_cleanup_for_game: Preparing for game\n");
    // Game initialization is now done in the GAME_STATE_PLAYING handler
}

// Reinitialize picker after game ends
void picker_reinit_after_game()
{
    Log("picker_reinit_after_game: Reinitializing picker\n");

    // Fade out from game
    screen* const current_screen = og::runtime::current_session->myscreen_;
    current_screen->set_active_canvas(current_screen->last_presented_canvas());
    current_screen->fadeblack(0);
    current_screen->clearbuffer();
    current_screen->set_active_canvas(CanvasTarget::UI);

    grab_mouse();

    og::runtime::current_session->myscreen_->reset(1);
    og::runtime::current_session->myscreen_->viewob[0]->resize(PREF_VIEW_FULL);

    // Reload save data
    picker_load_default_save_if_present();

    picker_reinitialize_lobby_after_game();

    // The native go_menu loop returns here after glad_main and redraws the
    // team-build/Continue screen. The browser unwinds gameplay through its
    // outer rAF state machine, so explicitly resume at the same destination
    // instead of constructing a fresh picker at MainMenu.
    // The result dialog's dismissing touch may still be held (or its release
    // may be queued), and its coordinates overlap the SCENARIO button here.
    // Establish a fresh pointer baseline so that touch cannot click through
    // into the newly-created menu.
    reset_mouse_click_tracking();
    run_picker_state_machine_until_game_requested(
        og::ui::PickerScreen::TeamBuild);
    Log("picker_reinit_after_game: picker returned, g_start_game_requested={}\n", g_start_game_requested);
}
#endif
