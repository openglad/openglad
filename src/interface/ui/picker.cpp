#include <openglad/platform/soundob_sdl.h>
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
#include <openglad/core/stats.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/interface/input/button.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/render/view.h>
//buffers:  using input.h instead #include "int32.h"
#include <openglad/interface/input/input.h>
#include <openglad/core/util.h>
#include <openglad/platform/io.h>
#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/picker_ui_state.h>

#include "SDL.h"
#include <openglad/resources/gparser.h>
#include <openglad/interface/ui/campaign_picker.h>
#include <openglad/interface/ui/level_picker.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_state.h>
#include <openglad/platform/screen_lifecycle.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <cctype>
#include <format>
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <algorithm>
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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Flag to signal that game should start (for state machine)
bool g_start_game_requested = false;
#endif


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
#endif

#ifdef __EMSCRIPTEN__
void picker_request_start_game()
{
    g_start_game_requested = true;
}
#endif

#ifdef TESTING
void picker_testing_mark_game_start()
{
    g_test_game_epoch.fetch_add(1, std::memory_order_release);
    g_test_in_game.store(true, std::memory_order_release);
}

void picker_testing_mark_game_end()
{
    g_test_in_game.store(false, std::memory_order_release);
}
#endif

// allowable_guys moved to og::ui::kAllowableGuys in picker_common.h

// difficulty_level[] now shared via picker_common.cpp
// difficulty_level[] now defined in picker_common.cpp (shared between SDL and headless)
extern const std::int32_t difficulty_level[DIFFICULTY_SETTINGS];

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

static cfg_store& active_config()
{
    return cfg;
}

// The mainmenu loop: keeps showing the main menu after submenus return.
// quit() calls exit(0) directly, so we only re-enter here from submenu BACK.
void picker_mainmenu_loop()
{
    while(true) {
        TRACE("picker", "mainmenu_loop_iteration");
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
    pks().backpics[0] = read_pixie_file("mainul.pix");
    pks().backpics[1] = read_pixie_file("mainur.pix");
    pks().backpics[2] = read_pixie_file("mainll.pix");
    pks().backpics[3] = read_pixie_file("mainlr.pix");

    pks().backdrops[0] = std::make_unique<pixieN>(pks().backpics[0]);
    pks().backdrops[0]->setxy(0, 0);
    pks().backdrops[1] = std::make_unique<pixieN>(pks().backpics[1]);
    pks().backdrops[1]->setxy(160, 0);
    pks().backdrops[2] = std::make_unique<pixieN>(pks().backpics[2]);
    pks().backdrops[2]->setxy(0, 100);
    pks().backdrops[3] = std::make_unique<pixieN>(pks().backpics[3]);
    pks().backdrops[3]->setxy(160, 100);

    og::runtime::current_session->myscreen_->viewob[0]->resize(PREF_VIEW_FULL);
    og::runtime::current_session->myscreen_->clearbuffer();

    //main_title_logo_data = read_pixie_file("glad.pix");
    pks().main_title_logo_data = read_pixie_file("title.pix"); // marbled gladiator title
    pks().main_title_logo_pix = std::make_unique<pixieN>(pks().main_title_logo_data);

    //main_columns_data = read_pixie_file("mage.pix");
    pks().main_columns_data = read_pixie_file("columns.pix");
    pks().main_columns_pix = std::make_unique<pixieN>(pks().main_columns_data);

    // Get the mouse, timer, & keyboard ..
    grab_mouse();
    grab_timer();
    clear_keyboard();
}

static void picker_load_default_save_if_present()
{
    RwopsPtr loadgame(open_read_file("save/", "save0.gtl"));
    if (loadgame)
        og::runtime::current_session->myscreen_->save_data.load("save0");
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
        case ButtonAction::CreateTeamMenu:
            menu_item = og::ui::find_picker_menu_item(
                og::ui::PickerMenuId::Main, og::ui::PickerMenuCommand::ContinueGame);
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
    }
    return false;
}

class SdlPickerClient final : public og::ui::IPickerClient
{
public:
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

        set_intercept_scope(PickerInterceptScope::TeamBuild);
        create_team_menu(start_team_build_in_hire_menu_ ? 1 : 0);
        set_intercept_scope(PickerInterceptScope::None);
        start_team_build_in_hire_menu_ = false;
        if (pks().selected_menu_item)
            return pks().selected_menu_item;
        return og::ui::find_picker_menu_item(
            og::ui::PickerMenuId::TeamBuild, og::ui::PickerMenuCommand::Back);
    }

    bool prepare_new_game() override
    {
        if (!picker_prepare_new_game_setup())
            return false;
        start_team_build_in_hire_menu_ = true;
        return true;
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
        create_load_menu(0);
        return true;
    }

    bool save_game() override
    {
        create_save_menu(0);
        return true;
    }

    og::ui::PickerScreen screen_after_game() const override
    {
#ifdef __EMSCRIPTEN__
        if (g_start_game_requested)
            return og::ui::PickerScreen::Quit;
#endif
        return og::ui::PickerScreen::TeamBuild;
    }

private:
    bool start_team_build_in_hire_menu_ = false;
};

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
    og::ui::run_picker(client);
}

// Centralized picker resource cleanup (no screen destruction).
// Tests and PickerSession use this instead of duplicating cleanup logic.
void picker_cleanup_resources()
{
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
}

void picker_quit()
{
	picker_cleanup_resources();
    destroy_global_screen();
}

#ifdef USE_TOUCH_INPUT
#define DISABLE_MULTIPLAYER
#endif

// mainmenu

#ifndef DISABLE_MULTIPLAYER

#ifdef __EMSCRIPTEN__
// Web build: Replace QUIT with HELP (QUIT doesn't make sense in browser)
button mainmenu_buttons[] =
    {
        button("begin_new_game", "", KEYSTATE_UNKNOWN, 80, 50, 140, 20, button_action_id(ButtonAction::BeginMenu), 1 , MenuNav{.down=1}, false), // BEGIN NEW GAME
        button("continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 75, 140, 20, button_action_id(ButtonAction::CreateTeamMenu), -1 , MenuNav{.up=0, .down=5}),

        button("4_player", "4 PLAYER", KEYSTATE_4, 152,125,68,20, button_action_id(ButtonAction::SetPlayerMode), 4 , MenuNav{.up=4, .down=6, .left=3}),
        button("3_player", "3 PLAYER", KEYSTATE_3, 80,125,68,20, button_action_id(ButtonAction::SetPlayerMode),3 , MenuNav{.up=5, .down=6, .right=2}),
        button("2_player", "2 PLAYER", KEYSTATE_2, 152,100,68,20, button_action_id(ButtonAction::SetPlayerMode),2 , MenuNav{.up=1, .down=2, .left=5}),
        button("1_player", "1 PLAYER", KEYSTATE_1, 80,100,68,20, button_action_id(ButtonAction::SetPlayerMode),1 , MenuNav{.up=1, .down=3, .right=4}),

        button("difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 148, 140, 10, button_action_id(ButtonAction::SetDifficulty), -1, MenuNav{.up=3, .down=7}),

        button("pvp_allied", "PVP: Allied", KEYSTATE_UNKNOWN, 80, 160, 68, 10, button_action_id(ButtonAction::AlliedMode), -1, MenuNav{.up=6, .down=9, .right=8}),
        button("level_edit", "Level Edit", KEYSTATE_UNKNOWN, 152, 160, 68, 10, button_action_id(ButtonAction::DoLevelEdit), -1, MenuNav{.up=6, .down=9, .left=7}),

        button("help", "HELP", KEYSTATE_UNKNOWN, 120, 175, 60, 20, button_action_id(ButtonAction::ShowHelp), -1, MenuNav{.up=7, .left=10}),
        button("options", "", KEYSTATE_UNKNOWN, 90, 175, 20, 20, button_action_id(ButtonAction::MainOptions), -1, MenuNav{.up=7, .right=9})
    };
#define OPTIONS_BUTTON_INDEX 10

#else // Native build
button mainmenu_buttons[] =
    {
        button("begin_new_game", "", KEYSTATE_UNKNOWN, 80, 50, 140, 20, button_action_id(ButtonAction::BeginMenu), 1 , MenuNav{.down=1}, false), // BEGIN NEW GAME
        button("continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 75, 140, 20, button_action_id(ButtonAction::CreateTeamMenu), -1 , MenuNav{.up=0, .down=5}),

        button("4_player", "4 PLAYER", KEYSTATE_4, 152,125,68,20, button_action_id(ButtonAction::SetPlayerMode), 4 , MenuNav{.up=4, .down=6, .left=3}),
        button("3_player", "3 PLAYER", KEYSTATE_3, 80,125,68,20, button_action_id(ButtonAction::SetPlayerMode),3 , MenuNav{.up=5, .down=6, .right=2}),
        button("2_player", "2 PLAYER", KEYSTATE_2, 152,100,68,20, button_action_id(ButtonAction::SetPlayerMode),2 , MenuNav{.up=1, .down=2, .left=5}),
        button("1_player", "1 PLAYER", KEYSTATE_1, 80,100,68,20, button_action_id(ButtonAction::SetPlayerMode),1 , MenuNav{.up=1, .down=3, .right=4}),

        button("difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 148, 140, 10, button_action_id(ButtonAction::SetDifficulty), -1, MenuNav{.up=3, .down=7}),

        button("pvp_allied", "PVP: Allied", KEYSTATE_UNKNOWN, 80, 160, 68, 10, button_action_id(ButtonAction::AlliedMode), -1, MenuNav{.up=6, .down=9, .right=8}),
        button("level_edit", "Level Edit", KEYSTATE_UNKNOWN, 152, 160, 68, 10, button_action_id(ButtonAction::DoLevelEdit), -1, MenuNav{.up=6, .down=9, .left=7}),

        button("quit", "QUIT ", KEYSTATE_ESCAPE, 120, 175, 60, 20, button_action_id(ButtonAction::QuitMenu), 0 , MenuNav{.up=7, .left=10}),
        button("options", "", KEYSTATE_UNKNOWN, 90, 175, 20, 20, button_action_id(ButtonAction::MainOptions), -1, MenuNav{.up=7, .right=9})
    };
#define OPTIONS_BUTTON_INDEX 10
#endif // __EMSCRIPTEN__

#else // DISABLE_MULTIPLAYER

#ifdef __EMSCRIPTEN__
// Web build without multiplayer: Replace QUIT with HELP
button mainmenu_buttons[] =
    {
        button("begin_new_game", "", KEYSTATE_UNKNOWN, 80, 70, 140, 20, button_action_id(ButtonAction::BeginMenu), 1 , MenuNav{.down=1}, false), // BEGIN NEW GAME
        button("continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 95, 140, 20, button_action_id(ButtonAction::CreateTeamMenu), -1 , MenuNav{.up=0, .down=2}),

        button("difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 120, 140, 15, button_action_id(ButtonAction::SetDifficulty), -1, MenuNav{.up=1, .down=3}),
        button("level_edit", "Level Edit", KEYSTATE_UNKNOWN, 80, 137, 140, 15, button_action_id(ButtonAction::DoLevelEdit), -1, MenuNav{.up=2, .down=4}),
        button("help", "HELP", KEYSTATE_UNKNOWN, 120, 154, 60, 20, button_action_id(ButtonAction::ShowHelp), -1, MenuNav{.up=3, .left=5}),
        button("options", "", KEYSTATE_UNKNOWN, 90, 154, 20, 20, button_action_id(ButtonAction::MainOptions), -1, MenuNav{.up=3, .right=4})
    };
#define OPTIONS_BUTTON_INDEX 5

#else // Native build without multiplayer
button mainmenu_buttons[] =
    {
        button("begin_new_game", "", KEYSTATE_UNKNOWN, 80, 70, 140, 20, button_action_id(ButtonAction::BeginMenu), 1 , MenuNav{.down=1}, false), // BEGIN NEW GAME
        button("continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 95, 140, 20, button_action_id(ButtonAction::CreateTeamMenu), -1 , MenuNav{.up=0, .down=2}),

        button("difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 120, 140, 15, button_action_id(ButtonAction::SetDifficulty), -1, MenuNav{.up=1, .down=3}),
        button("level_edit", "Level Edit", KEYSTATE_UNKNOWN, 80, 137, 140, 15, button_action_id(ButtonAction::DoLevelEdit), -1, MenuNav{.up=2, .down=4}),
        button("quit", "QUIT ", KEYSTATE_ESCAPE, 120, 154, 60, 20, button_action_id(ButtonAction::QuitMenu), 0, MenuNav{.up=3, .left=5}),
        button("options", "", KEYSTATE_UNKNOWN, 90, 154, 20, 20, button_action_id(ButtonAction::MainOptions), -1, MenuNav{.up=3, .right=4})
    };
#define OPTIONS_BUTTON_INDEX 5
#endif // __EMSCRIPTEN__

#endif // DISABLE_MULTIPLAYER


inline constexpr Sint32 BUTTON_PADDING = 8;
inline constexpr Sint32 BUTTON_PITCH = BUTTON_HEIGHT + BUTTON_PADDING;

button main_options_buttons[] =
{
    button("options_back", "BACK", KEYSTATE_ESCAPE, 10, 10, 50, 15, button_action_id(ButtonAction::ReturnMenu), MENU_EXIT, MenuNav{.up=12, .down=1, .right=15}),
    button("toggle_sound", "Sound", KEYSTATE_UNKNOWN, 135, 10 + BUTTON_PITCH, 50, 15, button_action_id(ButtonAction::ToggleSound), -1, MenuNav{.up=0, .down=2}),
    button("toggle_rendering", "NORMAL", KEYSTATE_UNKNOWN, 130, 10 + 2*BUTTON_PITCH, 60, 15, button_action_id(ButtonAction::ToggleRenderingEngine), -1, MenuNav{.up=1, .down=4, .right=3}),
    button("toggle_fullscreen", "Fullscreen", KEYSTATE_UNKNOWN, 210, 10 + 2*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleFullscreen), -1, MenuNav{.up=1, .down=5, .left=2}),
    button("overscan_minus", "- ", KEYSTATE_UNKNOWN, 130, 10 + 3*BUTTON_PITCH, 30, 15, button_action_id(ButtonAction::OverscanAdjust), -1, MenuNav{.up=2, .down=6, .right=5}),
    button("overscan_plus", "+ ", KEYSTATE_UNKNOWN, 170, 10 + 3*BUTTON_PITCH, 30, 15, button_action_id(ButtonAction::OverscanAdjust), 1, MenuNav{.up=3, .down=7, .left=4}),
    button("toggle_mini_hp_bar", "Mini HP bar", KEYSTATE_UNKNOWN, 80, 10 + 4*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleMiniHpBar), -1, MenuNav{.up=4, .down=8, .right=7}),
    button("toggle_hit_flash", "Hit flash", KEYSTATE_UNKNOWN, 210, 10 + 4*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleHitFlash), -1, MenuNav{.up=5, .down=9, .left=6}),
    button("toggle_hit_recoil", "Hit recoil", KEYSTATE_UNKNOWN, 80, 10 + 5*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleHitRecoil), -1, MenuNav{.up=6, .down=10, .right=9}),
    button("toggle_attack_lunge", "Attack lunge", KEYSTATE_UNKNOWN, 210, 10 + 5*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleAttackLunge), -1, MenuNav{.up=7, .down=11, .left=8}),
    button("toggle_hit_sparks", "Hit sparks", KEYSTATE_UNKNOWN, 80, 10 + 6*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleHitAnim), -1, MenuNav{.up=8, .down=12, .right=11}),
    button("toggle_damage_numbers", "Damage numbers", KEYSTATE_UNKNOWN, 210, 10 + 6*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleDamageNumbers), -1, MenuNav{.up=9, .down=13, .left=10}),
    button("toggle_heal_numbers", "Healing numbers", KEYSTATE_UNKNOWN, 80, 10 + 7*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleHealNumbers), -1, MenuNav{.up=10, .down=0, .right=13}),
    button("toggle_gore", "Gore", KEYSTATE_UNKNOWN, 210, 10 + 7*BUTTON_PITCH, 90, 15, button_action_id(ButtonAction::ToggleGore), -1, MenuNav{.up=11, .down=14, .left=12}),
    button("restore_defaults", "RESTORE DEFAULTS", KEYSTATE_UNKNOWN, 210, 10, 100, 15, button_action_id(ButtonAction::RestoreDefaultSettings), -1, MenuNav{.up=13, .down=1, .left=15}),
    button("player_controls", "CONTROLS", KEYSTATE_UNKNOWN, 100, 10, 80, 15,
        button_action_id(ButtonAction::OpenControlSettings), -1, MenuNav{.up=13, .down=1, .left=0, .right=14}),
};

// Control options: 4 player sections at 28px pitch, each with mode + remap buttons.
// Text labels ("Px", key info) drawn in main_controls_options() below each section's buttons.
#define CTRL_PLAYER_PITCH 28
#define CTRL_PLAYER_Y(i) (40 + (i) * CTRL_PLAYER_PITCH)

button control_options_buttons[] =
{
    button("controls_back", "BACK", KEYSTATE_ESCAPE, 10, 8, 50, 15, button_action_id(ButtonAction::ReturnMenu), MENU_EXIT, MenuNav{.down=1}),
    button("player1_mode", "4-DIRECTION", KEYSTATE_UNKNOWN, 30, CTRL_PLAYER_Y(0), 100, 15,
        button_action_id(ButtonAction::ToggleControlMode), 0, MenuNav{.up=0, .down=3, .right=2}),
    button("player1_remap", "REMAP P1", KEYSTATE_UNKNOWN, 170, CTRL_PLAYER_Y(0), 100, 15,
        button_action_id(ButtonAction::EditPlayerKeymap), 0, MenuNav{.up=0, .down=4, .left=1}),
    button("player2_mode", "4-DIRECTION", KEYSTATE_UNKNOWN, 30, CTRL_PLAYER_Y(1), 100, 15,
        button_action_id(ButtonAction::ToggleControlMode), 1, MenuNav{.up=1, .down=5, .right=4}),
    button("player2_remap", "REMAP P2", KEYSTATE_UNKNOWN, 170, CTRL_PLAYER_Y(1), 100, 15,
        button_action_id(ButtonAction::EditPlayerKeymap), 1, MenuNav{.up=2, .down=6, .left=3}),
    button("player3_mode", "4-DIRECTION", KEYSTATE_UNKNOWN, 30, CTRL_PLAYER_Y(2), 100, 15,
        button_action_id(ButtonAction::ToggleControlMode), 2, MenuNav{.up=3, .down=7, .right=6}),
    button("player3_remap", "REMAP P3", KEYSTATE_UNKNOWN, 170, CTRL_PLAYER_Y(2), 100, 15,
        button_action_id(ButtonAction::EditPlayerKeymap), 2, MenuNav{.up=4, .down=8, .left=5}),
    button("player4_mode", "4-DIRECTION", KEYSTATE_UNKNOWN, 30, CTRL_PLAYER_Y(3), 100, 15,
        button_action_id(ButtonAction::ToggleControlMode), 3, MenuNav{.up=5, .down=9, .right=8}),
    button("player4_remap", "REMAP P4", KEYSTATE_UNKNOWN, 170, CTRL_PLAYER_Y(3), 100, 15,
        button_action_id(ButtonAction::EditPlayerKeymap), 3, MenuNav{.up=6, .down=9, .left=7}),
    button("controls_restore_defaults", "RESET DEFAULTS", KEYSTATE_UNKNOWN, 80, 170, 160, 15,
        button_action_id(ButtonAction::RestoreDefaultControls), -1, MenuNav{.up=7}),
};

// beginmenu (first menu of new game), create_team_menu
button createmenu_buttons[] =
    {
        button("view_team", "VIEW TEAM", KEYSTATE_UNKNOWN, 30, 70, 80, 15, button_action_id(ButtonAction::CreateViewMenu), -1, MenuNav{.down=3, .right=1}),
        button("train_team", "TRAIN TEAM", KEYSTATE_UNKNOWN, 120, 70, 80, 15, button_action_id(ButtonAction::CreateTrainMenu), -1, MenuNav{.down=4, .left=0, .right=2}),
        button("hire_troops", "HIRE TROOPS",  KEYSTATE_UNKNOWN, 210, 70, 80, 15, button_action_id(ButtonAction::CreateHireMenu), -1, MenuNav{.down=5, .left=1}),
        button("load_team", "LOAD TEAM", KEYSTATE_UNKNOWN, 30, 100, 80, 15, button_action_id(ButtonAction::CreateLoadMenu), -1, MenuNav{.up=0, .down=6, .right=4}),
        button("save_team", "SAVE TEAM", KEYSTATE_UNKNOWN, 120, 100, 80, 15, button_action_id(ButtonAction::CreateSaveMenu), -1, MenuNav{.up=1, .left=3, .right=5}),
        button("go", "GO", KEYSTATE_UNKNOWN,        210, 100, 80, 15, button_action_id(ButtonAction::GoMenu), -1, MenuNav{.up=2, .down=8, .left=4}),

        button("back", "BACK", KEYSTATE_ESCAPE, 30, 140, 60, 30, button_action_id(ButtonAction::ReturnMenu), MENU_EXIT, MenuNav{.up=3, .right=7}),
        button("progress", "PROGRESS", KEYSTATE_UNKNOWN, 120, 140, 80, 20, button_action_id(ButtonAction::CreateProgressMenu), -1, MenuNav{.up=4, .left=6, .right=8}),
        button("set_level", "SET LEVEL", KEYSTATE_UNKNOWN, 210, 140, 80, 20, button_action_id(ButtonAction::DoSetScenLevel), MENU_EXIT, MenuNav{.up=5, .down=9, .left=7}),
        button("set_campaign", "SET CAMPAIGN", KEYSTATE_UNKNOWN, 210, 170, 80, 20, button_action_id(ButtonAction::DoPickCampaign), MENU_EXIT, MenuNav{.up=8, .left=7}),

    };

button viewteam_buttons[] =
    {
        //  button("TRAIN", KEYSTATE_e, 85, 170, 60, 20, button_action_id(ButtonAction::CreateTrainMenu), -1},
        //  button("HIRE",  KEYSTATE_b, 190, 170, 60, 20, button_action_id(ButtonAction::CreateHireMenu), -1},
        button("go", "GO", KEYSTATE_UNKNOWN,        270, 170, 40, 20, button_action_id(ButtonAction::GoMenu), -1, MenuNav{.left=1}),
        button("back", "BACK", KEYSTATE_ESCAPE,    10, 170, 44, 20, button_action_id(ButtonAction::ReturnMenu) , MENU_REDRAW, MenuNav{.right=0}),

    };

button details_buttons[] =
    {
        button("back", "BACK", KEYSTATE_ESCAPE, 10, 170, 40, 20, button_action_id(ButtonAction::ReturnMenu) , MENU_EXIT, MenuNav{.up=1, .right=1}),
        button("promote", 160, 4, 315 - 160, 66 - 4, 0 , -1, MenuNav{.down=0, .left=0}, false, true) // PROMOTE
    };

button trainmenu_buttons[] =
    {
        button("prev", "PREV", KEYSTATE_UNKNOWN,  10, 40, 40, 20, button_action_id(ButtonAction::CycleTeamGuy), -1, MenuNav{.down=2, .right=1}),
        button("next", "NEXT", KEYSTATE_UNKNOWN,  110, 40, 40, 20, button_action_id(ButtonAction::CycleTeamGuy), 1, MenuNav{.down=3, .left=0, .right=16}),
        button("dec_str", "", KEYSTATE_UNKNOWN,  16, 70, 16, 10, button_action_id(ButtonAction::DecreaseStat), BUT_STR, MenuNav{.up=0, .down=4, .right=3}),
        button("inc_str", "", KEYSTATE_UNKNOWN,  126, 70, 16, 12, button_action_id(ButtonAction::IncreaseStat), BUT_STR, MenuNav{.up=1, .down=5, .left=2}),
        button("dec_dex", "", KEYSTATE_UNKNOWN,  16, 85, 16, 10, button_action_id(ButtonAction::DecreaseStat), BUT_DEX, MenuNav{.up=2, .down=6, .right=5}),
        button("inc_dex", "", KEYSTATE_UNKNOWN,  126, 85, 16, 12, button_action_id(ButtonAction::IncreaseStat), BUT_DEX, MenuNav{.up=3, .down=7, .left=4}),
        button("dec_con", "", KEYSTATE_UNKNOWN,  16, 100, 16, 10, button_action_id(ButtonAction::DecreaseStat), BUT_CON, MenuNav{.up=4, .down=8, .right=7}),
        button("inc_con", "", KEYSTATE_UNKNOWN,  126,100, 16, 12, button_action_id(ButtonAction::IncreaseStat), BUT_CON, MenuNav{.up=5, .down=9, .left=6}),
        button("dec_int", "", KEYSTATE_UNKNOWN,  16, 115, 16, 10, button_action_id(ButtonAction::DecreaseStat), BUT_INT, MenuNav{.up=6, .down=10, .right=9}),
        button("inc_int", "", KEYSTATE_UNKNOWN,  126, 115, 16, 12, button_action_id(ButtonAction::IncreaseStat), BUT_INT, MenuNav{.up=7, .down=11, .left=8}),
        button("dec_armor", "", KEYSTATE_UNKNOWN,  16, 130, 16, 10, button_action_id(ButtonAction::DecreaseStat), BUT_ARMOR, MenuNav{.up=8, .down=12, .right=11}),
        button("inc_armor", "", KEYSTATE_UNKNOWN,  126, 130, 16, 12, button_action_id(ButtonAction::IncreaseStat), BUT_ARMOR, MenuNav{.up=9, .down=13, .left=10}),
        button("dec_level", "", KEYSTATE_UNKNOWN,  16, 145, 16, 10, button_action_id(ButtonAction::DecreaseStat), BUT_LEVEL, MenuNav{.up=10, .down=19, .right=13}),
        button("inc_level", "", KEYSTATE_UNKNOWN,  126, 145, 16, 12, button_action_id(ButtonAction::IncreaseStat), BUT_LEVEL, MenuNav{.up=11, .down=15, .left=12, .right=18}),
        button("view_team", "VIEW TEAM", KEYSTATE_UNKNOWN,  190, 170, 90, 20, button_action_id(ButtonAction::CreateViewMenu), -1, MenuNav{.up=18, .left=15}),
        button("accept", "ACCEPT", KEYSTATE_UNKNOWN,  80, 170, 80, 20, button_action_id(ButtonAction::EditGuy), -1, MenuNav{.up=13, .left=19, .right=14}),
        button("rename", "RENAME", KEYSTATE_UNKNOWN, 174,  8, 64, 22, button_action_id(ButtonAction::NameGuy), 1, MenuNav{.down=18, .left=1, .right=17}),
        button("details", "DETAILS..", KEYSTATE_UNKNOWN, 240, 8, 64, 22, button_action_id(ButtonAction::CreateDetailMenu), 0, MenuNav{.down=18, .left=16}),
        button("change_team", "Playing on Team X", KEYSTATE_UNKNOWN, 174, 138, 133, 22, button_action_id(ButtonAction::ChangeTeam), 1, MenuNav{.up=17, .down=14, .left=13}),
        button("back", "BACK", KEYSTATE_ESCAPE,10, 170, 40, 20, button_action_id(ButtonAction::ReturnMenu) , MENU_EXIT, MenuNav{.up=12, .right=15}),

    };

button hiremenu_buttons[] =
    {
        button("prev", "PREV", KEYSTATE_UNKNOWN,  10, 40, 40, 20, button_action_id(ButtonAction::CycleGuy), -1, MenuNav{.down=4, .right=1}),
        button("next", "NEXT", KEYSTATE_UNKNOWN,  110, 40, 40, 20, button_action_id(ButtonAction::CycleGuy), 1, MenuNav{.down=3, .left=0, .right=3}),
        button("change_hire_team", "hiring for team X", KEYSTATE_UNKNOWN, 190, 170, 110, 20, button_action_id(ButtonAction::ChangeHireTeam), 1, MenuNav{.up=1, .left=3}),
        button("hire_me", "HIRE ME", KEYSTATE_UNKNOWN,  82, 166, 88, 28, button_action_id(ButtonAction::AddGuy), -1, MenuNav{.up=1, .left=4, .right=2}),
        button("back", "BACK", KEYSTATE_ESCAPE,10, 170, 40, 20, button_action_id(ButtonAction::ReturnMenu) , MENU_EXIT, MenuNav{.up=0, .right=3}),

    };


button saveteam_buttons[] =
    {
        button("save_slot_1", "SLOT ONE", KEYSTATE_UNKNOWN,  25, 25, 220, 10, button_action_id(ButtonAction::DoSave), 1, MenuNav{.up=10, .down=1}),
        button("save_slot_2", "SLOT TWO", KEYSTATE_UNKNOWN,  25, 40, 220, 10, button_action_id(ButtonAction::DoSave), 2, MenuNav{.up=0, .down=2}),
        button("save_slot_3", "SLOT THREE", KEYSTATE_UNKNOWN,25, 55, 220, 10, button_action_id(ButtonAction::DoSave), 3, MenuNav{.up=1, .down=3}),
        button("save_slot_4", "SLOT FOUR", KEYSTATE_UNKNOWN, 25, 70, 220, 10, button_action_id(ButtonAction::DoSave), 4, MenuNav{.up=2, .down=4}),
        button("save_slot_5", "SLOT FIVE", KEYSTATE_UNKNOWN, 25, 85, 220, 10, button_action_id(ButtonAction::DoSave), 5, MenuNav{.up=3, .down=5}),
        button("save_slot_6", "SLOT Six", KEYSTATE_UNKNOWN, 25, 100, 220, 10, button_action_id(ButtonAction::DoSave),  6, MenuNav{.up=4, .down=6}),
        button("save_slot_7", "SLOT Seven", KEYSTATE_UNKNOWN, 25, 115, 220, 10, button_action_id(ButtonAction::DoSave), 7, MenuNav{.up=5, .down=7}),
        button("save_slot_8", "SLOT Eight", KEYSTATE_UNKNOWN, 25, 130, 220, 10, button_action_id(ButtonAction::DoSave), 8, MenuNav{.up=6, .down=8}),
        button("save_slot_9", "SLOT Nine", KEYSTATE_UNKNOWN, 25, 145, 220, 10, button_action_id(ButtonAction::DoSave), 9, MenuNav{.up=7, .down=9}),
        button("save_slot_10", "SLOT Ten", KEYSTATE_UNKNOWN, 25, 160, 220, 10, button_action_id(ButtonAction::DoSave), 10, MenuNav{.up=8, .down=10}),
        button("back", "BACK", KEYSTATE_ESCAPE,25, 175, 40, 20, button_action_id(ButtonAction::ReturnMenu) , MENU_EXIT, MenuNav{.up=9, .down=0}),

    };

button loadteam_buttons[] =
    {
        button("load_slot_1", "SLOT ONE", KEYSTATE_UNKNOWN,  25, 25, 220, 10, button_action_id(ButtonAction::DoLoad), 1, MenuNav{.up=10, .down=1}),
        button("load_slot_2", "SLOT TWO", KEYSTATE_UNKNOWN,  25, 40, 220, 10, button_action_id(ButtonAction::DoLoad), 2, MenuNav{.up=0, .down=2}),
        button("load_slot_3", "SLOT THREE", KEYSTATE_UNKNOWN,25, 55, 220, 10, button_action_id(ButtonAction::DoLoad), 3, MenuNav{.up=1, .down=3}),
        button("load_slot_4", "SLOT FOUR", KEYSTATE_UNKNOWN, 25, 70, 220, 10, button_action_id(ButtonAction::DoLoad), 4, MenuNav{.up=2, .down=4}),
        button("load_slot_5", "SLOT FIVE", KEYSTATE_UNKNOWN, 25, 85, 220, 10, button_action_id(ButtonAction::DoLoad), 5, MenuNav{.up=3, .down=5}),
        button("load_slot_6", "SLOT Six", KEYSTATE_UNKNOWN, 25, 100, 220, 10, button_action_id(ButtonAction::DoLoad),  6, MenuNav{.up=4, .down=6}),
        button("load_slot_7", "SLOT Seven", KEYSTATE_UNKNOWN, 25, 115, 220, 10, button_action_id(ButtonAction::DoLoad), 7, MenuNav{.up=5, .down=7}),
        button("load_slot_8", "SLOT Eight", KEYSTATE_UNKNOWN, 25, 130, 220, 10, button_action_id(ButtonAction::DoLoad), 8, MenuNav{.up=6, .down=8}),
        button("load_slot_9", "SLOT Nine", KEYSTATE_UNKNOWN, 25, 145, 220, 10, button_action_id(ButtonAction::DoLoad), 9, MenuNav{.up=7, .down=9}),
        button("load_slot_10", "SLOT Ten", KEYSTATE_UNKNOWN, 25, 160, 220, 10, button_action_id(ButtonAction::DoLoad), 10, MenuNav{.up=8, .down=10}),
        button("back", "BACK", KEYSTATE_ESCAPE,25, 175, 40, 20, button_action_id(ButtonAction::ReturnMenu) , MENU_EXIT, MenuNav{.up=9, .down=0}),

    };


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

void draw_toggle_effect_button(button& b, const std::string& category, const std::string& setting)
{
    if(b.hidden || b.no_draw)
        return;
    
    if(active_config().is_on(category, setting))
        og::runtime::current_session->myscreen_->draw_button_colored(b.x-1, b.y-1, b.x + b.sizex, b.y + b.sizey, 1, LIGHT_GREEN);
    else
        og::runtime::current_session->myscreen_->draw_button_colored(b.x-1, b.y-1, b.x + b.sizex, b.y + b.sizey, 1, RED);
    
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    mytext.write_xy_center(b.x + b.sizex/2, b.y + b.sizey/2 - 3, DARK_BLUE, "%s", b.label.c_str());
}

static std::string get_key_display_name_short(int keycode)
{
    std::string sname = SDL_GetKeyName(keycode);

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

std::array<std::string, 2> build_player_control_summary_lines(int player_index, bool remap_mode)
{
    if (player_index < 0 || player_index >= 4)
        return {"", ""};

    const bool eight_dir =
        get_player_control_mode(player_index) == static_cast<int>(ControlDirectionMode::EightDirection);
    const std::string up_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_UP]);
    const std::string left_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_LEFT]);
    const std::string down_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_DOWN]);
    const std::string right_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_RIGHT]);
    const std::string yell_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_YELL]);
    const std::string fire_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_FIRE]);
    const std::string special_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_SPECIAL]);
    const std::string special_switch_s =
        get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_SPECIAL_SWITCH]);
    const std::string switch_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_SWITCH]);
    const std::string shifter_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_SHIFTER]);
    const std::string action_line = std::format("Y:{} F:{} S:{} SS:{} SW:{} Sh:{}",
        yell_s, fire_s, special_s, special_switch_s, switch_s, shifter_s);

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

    const std::string up_right_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_UP_RIGHT]);
    const std::string down_right_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_DOWN_RIGHT]);
    const std::string down_left_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_DOWN_LEFT]);
    const std::string up_left_s = get_key_display_name_short(og::runtime::current_session->player_keys_[player_index][KEY_UP_LEFT]);

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
    mytext.write_xy_center(160, 105, DARK_BLUE, "Press ESC to keep current key");
    const auto summary_lines = build_player_control_summary_lines(player_index, true);
    mytext.write_xy_center(160, 150, DARK_BLUE, "%s", summary_lines[0].c_str());
    mytext.write_xy_center(160, 160, DARK_BLUE, "%s", summary_lines[1].c_str());
    og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
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
    };

    const KeyPrompt* prompts = eight_dir ? prompts_eight : prompts_four;
    const size_t prompt_count = eight_dir ? array_size(prompts_eight) : array_size(prompts_four);

    for (size_t idx = 0; idx < prompt_count; ++idx)
    {
        const auto& prompt = prompts[idx];
        draw_remap_prompt(std::format("P{} {}", player_index + 1, prompt.label), player_index);
        assignKeyFromWaitEvent(player_index, prompt.key);
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

Sint32 main_controls_options()
{
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    button* buttons = control_options_buttons;
    const int num_buttons = array_size(control_options_buttons);
    int highlighted_button = 0;
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);
    clear_keyboard();

    Sint32 retvalue = 0;
	while(!(retvalue & MENU_EXIT))
	{
        if(leftmouse(buttons))
        {
            const Sint32 click_result = og::runtime::current_session->localbuttons_->leftclick();
            if(click_result == MENU_EXIT)
                break;
            if(click_result != 0)
                retvalue = click_result;
        }

        handle_menu_nav(buttons, highlighted_button, retvalue);
        if(retvalue == MENU_EXIT)
            break;

        reset_buttons(og::runtime::current_session->localbuttons_, buttons, num_buttons, retvalue);

        for (int i = 0; i < 4; ++i)
        {
            const bool eight_dir = get_player_control_mode(i) == static_cast<int>(ControlDirectionMode::EightDirection);
            const int mode_index = 1 + i * 2;
            buttons[mode_index].label = eight_dir ? "8-DIRECTION" : "4-DIRECTION";
            og::runtime::current_session->allbuttons_[mode_index]->label = buttons[mode_index].label;
        }

        og::runtime::current_session->myscreen_->clear_window();
        og::runtime::current_session->myscreen_->draw_button(0, 0, 320, 200, 0);
        og::runtime::current_session->myscreen_->draw_button_inverted(4, 4, 312, 192);
        draw_buttons(buttons, num_buttons);

        mytext.write_xy(10, 24, DARK_BLUE, "Player control modes and key remapping");

        for (int i = 0; i < 4; ++i)
        {
            const int btn_y = CTRL_PLAYER_Y(i);
            mytext.write_xy(10, btn_y + 3, DARK_BLUE, "P%d", i + 1);
            const std::string summary = build_player_control_summary(i);
            mytext.write_xy(30, btn_y + 17, DARK_BLUE, "%s", summary.c_str());
        }

        mytext.write_xy(10, 155, DARK_BLUE, "4-dir = cardinal only. 8-dir adds diagonals.");

        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0, 0, 320, 200);
        SDL_Delay(10);
    }

    return MENU_REDRAW;
}

Sint32 main_options()
{
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    
		// init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
    #if defined(OUYA) || defined(ANDROID)
    main_options_buttons[3].hidden = main_options_buttons[3].no_draw = true;
    main_options_buttons[2].nav.right = -1;
    main_options_buttons[5].nav.up = 2;
    #endif
    
	button* buttons = main_options_buttons;
	int num_buttons = array_size(main_options_buttons);
	int highlighted_button = 0;
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

	clear_keyboard();
    
    Sint32 retvalue = 0;
	while(!(retvalue & MENU_EXIT))
	{
	    // Input
		if(leftmouse(buttons))
        {
            const Sint32 click_result = og::runtime::current_session->localbuttons_->leftclick();
			if(click_result == MENU_EXIT)
                break;
            if(click_result != 0)
                retvalue = click_result;
        }
        
        handle_menu_nav(buttons, highlighted_button, retvalue);
        if(retvalue == MENU_EXIT)
            break;
        
        // Reset buttons
        reset_buttons(og::runtime::current_session->localbuttons_, buttons, num_buttons, retvalue);
        buttons[2].label = active_config().get_setting("graphics", "render");
        og::runtime::current_session->allbuttons_[2]->label = buttons[2].label;
		
		// Draw
		og::runtime::current_session->myscreen_->clear_window();  // Clearing entire window because the overscan may have been adjusted.
		
		og::runtime::current_session->myscreen_->draw_button(0, 0, 320, 200, 0);
		og::runtime::current_session->myscreen_->draw_button_inverted(4, 4, 312, 192);
		
        
        draw_buttons(buttons, num_buttons);
        
		draw_toggle_effect_button(buttons[1], "sound", "sound");
		og::runtime::current_session->myscreen_->hor_line(60, buttons[2].y - BUTTON_PADDING/2, 200, PURE_WHITE);
		
		mytext.write_xy(20, buttons[2].y + 3, DARK_BLUE, "Rendering engine:");
		mytext.write_xy(20, buttons[2].y + 3 + 10, DARK_BLUE, " (needs restart)");
		draw_toggle_effect_button(buttons[3], "graphics", "fullscreen");
		mytext.write_xy(20, buttons[4].y + 3, DARK_BLUE, "Overscan adjust:");
		og::runtime::current_session->myscreen_->hor_line(60, buttons[6].y - BUTTON_PADDING/2, 200, PURE_WHITE);
		
		mytext.write_xy(20, buttons[6].y + 3, DARK_BLUE, "Effects:");
		draw_toggle_effect_button(buttons[6], "effects", "mini_hp_bar");
		draw_toggle_effect_button(buttons[7], "effects", "hit_flash");
		draw_toggle_effect_button(buttons[8], "effects", "hit_recoil");
		draw_toggle_effect_button(buttons[9], "effects", "attack_lunge");
		draw_toggle_effect_button(buttons[10], "effects", "hit_anim");
		draw_toggle_effect_button(buttons[11], "effects", "damage_numbers");
		draw_toggle_effect_button(buttons[12], "effects", "heal_numbers");
		draw_toggle_effect_button(buttons[13], "effects", "gore");
        
        draw_highlight(buttons[highlighted_button]);
        og::runtime::current_session->myscreen_->buffer_to_screen(0,0,320,200);
        SDL_Delay(10);
	}
	
	og::runtime::current_session->myscreen_->soundp->set_sound(!active_config().is_on("sound", "sound"));
	// Sync overscan to config before saving (data/ can't depend on input/)
	active_config().apply_setting("graphics", "overscan_percentage",
	    std::format("{:.0f}", 100 * og::runtime::current_session->overscan_percentage_));
    save_player_control_settings_to_cfg(active_config());
	active_config().save_settings();
    
    return MENU_REDRAW;
}

Sint32 overscan_adjust(Sint32 arg)
{
    og::runtime::current_session->overscan_percentage_ -= static_cast<float>(arg) / 100.0f;
    update_overscan_setting();
    
    return MENU_REDRAW;
}

Sint32 set_player_mode(Sint32 howmany)
{
	Sint32 count = 0;
	og::ui::set_player_count(og::runtime::current_session->myscreen_->save_data, howmany);

	while (og::runtime::current_session->allbuttons_[count])
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
	(void)arg1;

   Sint32 retvalue = 0;
   guy *thisguy;
   Sint32 start_time = query_timer();

   if (arg1)
       thisguy = arg1;
   else
       thisguy = og::runtime::current_session->myscreen_->save_data.team_list[og::runtime::current_session->editguy_].get();

   release_mouse();

	   // init_buttons owns allbuttons[]; localbuttons is a non-owning alias.
    
	button* buttons = details_buttons;
	int num_buttons = 2;
	int highlighted_button = 0;
	
	{
	    const auto* fd = get_family_descriptor(thisguy->family);
	    buttons[1].hidden = !(fd && fd->promotes_to >= 0 && thisguy->level >= fd->promotion_level_req);
	}
	og::runtime::current_session->localbuttons_ = init_buttons(buttons, num_buttons);

   //leftmouse(buttons);
   //localbuttons->leftclick(buttons);

   while ( !(retvalue & MENU_EXIT) )
   {
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
           const auto* fd = get_family_descriptor(thisguy->family);
           if (fd && fd->promotes_to >= 0 && thisguy->level >= fd->promotion_level_req)
           {
               short new_level = fd->promotion_new_level ? fd->promotion_new_level(thisguy->level) : 1;
               thisguy->upgrade_to_level(new_level);
               thisguy->family = static_cast<unsigned char>(fd->promotes_to);
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


Sint32 do_pick_campaign(Sint32 arg1)
{
	(void)arg1;
   CampaignResult result = pick_campaign(&og::runtime::current_session->myscreen_->save_data);
   if(result.id.size() > 0)
   {
        // Load new campaign
        og::runtime::current_session->myscreen_->save_data.current_campaign = result.id;
        og::runtime::current_session->myscreen_->save_data.scen_num = static_cast<short>(load_campaign(result.id, og::runtime::current_session->myscreen_->save_data.current_levels, result.first_level));
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

Sint32 set_difficulty()
{
   og::runtime::current_session->current_difficulty_ = og::ui::cycle_difficulty(og::runtime::current_session->current_difficulty_);
   std::string msg = og::ui::format_difficulty_label(og::runtime::current_session->current_difficulty_);
   #ifndef DISABLE_MULTIPLAYER
   og::runtime::current_session->allbuttons_[6]->label = msg;
   #else
   og::runtime::current_session->allbuttons_[2]->label = msg;
   #endif

   //allbuttons[6]->vdisplay();
   //myscreen->buffer_to_screen(0, 0, 320, 200);

   return MENU_OK;
}

Sint32 change_teamnum(Sint32 arg)
{
   // Change the team number of the current guy
   Sint32 current_team;

   // What is our current team number?
   if (!og::runtime::current_session->current_guy_)
       return 0;
   current_team = og::runtime::current_session->current_guy_->teamnum;

   // We can be from team 0 (default) to team 3 .. make sure
   // we don't exceed this range.
   current_team += arg;
   current_team = (current_team % 4 + 4) % 4;

   // Set our team number ..
   og::runtime::current_session->current_guy_->teamnum = static_cast<short>(current_team);

   // Update our button display
   og::runtime::current_session->allbuttons_[18]->label = std::format("Playing on Team {}", current_team + 1);
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
   og::runtime::current_session->allbuttons_[2]->label = std::format("Hiring for Team {}", og::runtime::current_session->current_team_num_ + 1);

   return MENU_OK;
}

Sint32 change_allied()
{
   og::ui::toggle_allied_mode(og::runtime::current_session->myscreen_->save_data);

   og::runtime::current_session->allbuttons_[7]->label = og::ui::format_allied_mode_label(og::runtime::current_session->myscreen_->save_data);

   //buffers: allbuttons[7]->vdisplay();
   //buffers: myscreen->buffer_to_screen(0, 0, 320, 200);

   return MENU_OK;
}

#ifdef __EMSCRIPTEN__
// ============================================================================
// Emscripten State Machine Functions
// These functions allow the picker to work with a non-blocking main loop
// ============================================================================

static void run_picker_state_machine_until_game_requested()
{
    SdlPickerClient client;
    og::ui::run_picker(client);
}

// Check if game start was requested (called from main after picker_init)
bool picker_check_start_requested()
{
    Log("picker_check_start_requested: g_start_game_requested={}\n", g_start_game_requested);
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
    run_picker_state_machine_until_game_requested();
    Log("picker_init: picker returned, g_start_game_requested={}\n", g_start_game_requested);
}

// Run one frame of the picker - returns true when game should start
bool picker_frame()
{
    // Check if game start was requested
    if (g_start_game_requested) {
        Log("picker_frame: Game start requested\n");
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
    og::runtime::current_session->myscreen_->fadeblack(0);
    og::runtime::current_session->myscreen_->clearbuffer();

    grab_mouse();

    og::runtime::current_session->myscreen_->reset(1);
    og::runtime::current_session->myscreen_->viewob[0]->resize(PREF_VIEW_FULL);

    // Reload save data
    picker_load_default_save_if_present();

    g_start_game_requested = false;

    run_picker_state_machine_until_game_requested();
    Log("picker_reinit_after_game: picker returned, g_start_game_requested={}\n", g_start_game_requested);
}
#endif
