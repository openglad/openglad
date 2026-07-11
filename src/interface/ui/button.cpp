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
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/core/util.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/render/text.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/sound.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <array>
#include <mutex>
#include <utility>

// Per-session picker state accessor.
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

// allbuttons is now a macro → current_session->allbuttons_
static inline auto& owned_buttons() { return og::runtime::current_session->owned_buttons_; }

void og::runtime::VButtonDeleter::operator()(::vbutton* button) const
{
    delete button;
}

void get_input_events(bool);







//vbutton functions, vbutton is a button class that will be self controlled
vbutton::vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high,
                 std::function<Sint32(Sint32)> func, Sint32 pass, const std::string& msg, int hot )
{
    arg = pass;
    fun = std::move(func);
    myfunc = 0;
    xloc = xpos;
    yloc = ypos;
    width = wide;
    height = high;
    xend = xloc + width;
    yend = yloc + height;
    label = msg;
    had_focus = 0;
    do_outline = 0;
    depressed = 0;

    mypixie = nullptr; // by default, no graphic picture

    hotkey = hot;

    //vdisplay();
    color = BUTTON_FACING;
    hidden = false;
    no_draw = false;
}

vbutton::vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high,
                 Sint32 func_code, Sint32 pass, const std::string& msg, int hot )
{
    arg = pass;
    fun = {}; // don't use this!
    myfunc = func_code;
    xloc = xpos;
    yloc = ypos;
    width = wide;
    height = high;
    xend = xloc + width;
    yend = yloc + height;
    label = msg;
    had_focus = 0;
    do_outline = 0;
    depressed = 0;

    mypixie = nullptr; // no graphic by default

    hotkey = hot;

    //vdisplay();
    color = BUTTON_FACING;
    hidden = false;
    no_draw = false;
}

vbutton::vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high,
                 Sint32 func_code, Sint32 pass, const std::string& msg, char family,
                 int hot )
{
    arg = pass;
    fun = {}; // don't use this!
    myfunc = func_code;
    xloc = xpos;
    yloc = ypos;
    width = wide;
    height = high;
    xend = xloc + width;
    yend = yloc + height;
    label = msg;
    had_focus = 0;
    do_outline = 0;
    depressed = 0;

    mypixie = og::runtime::current_session->myscreen_->myloader->create_pixieN_owned(Order::Button1, family);

    hotkey = hot;

    width = mypixie->sizex;
    height = mypixie->sizey;
    xend = xloc + width;
    yend = yloc + height;
    //vdisplay();
    color = BUTTON_FACING;
    hidden = false;
    no_draw = false;
}

vbutton::vbutton() //for pointers
{
    // Zero-initialize every integral member so a default-constructed vbutton
    // (intended only as a pointer placeholder) never carries indeterminate
    // values into mouse_on()/vdisplay()/leftclick() if accidentally used.
    xloc = 0;
    yloc = 0;
    width = 0;
    height = 0;
    xend = 0;
    yend = 0;
    myfunc = 0;
    arg = 0;
    hotkey = 0;
    color = 0;
    hidden = false;
    no_draw = false;
    had_focus = do_outline = depressed = 0;
    mypixie = nullptr;
}

vbutton::~vbutton() = default;

void vbutton::set_graphic(char family)
{
    mypixie = og::runtime::current_session->myscreen_->myloader->create_pixieN_owned(Order::Button1, family);
    width = mypixie->sizex;
    height= mypixie->sizey;
    xend = xloc + width;
    yend = yloc + height;
    //vdisplay();
}

void vbutton::vdisplay()
{
    if(hidden || no_draw)
        return;
    if (do_outline)
    {
        vdisplay(2);
        return;
    }
    
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    if (mypixie) // then use the graphic
    {
        mypixie->draw(xloc, yloc, og::runtime::current_session->myscreen_->viewob[0].get());
        if (label.size())
            mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - (((label.size()-1)* (mytext.letters->w+1) )/2)) ,
                              static_cast<short>(yloc + (height-(mytext.letters->h))/2), label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
    }
    else
    {
        og::runtime::current_session->myscreen_->draw_box(xloc,yloc,xend-1,yend-1,color,1,1); // front
        og::runtime::current_session->myscreen_->draw_box(xloc,yloc,xend-2,yloc,BUTTON_TOP,1,1); // top edge
        og::runtime::current_session->myscreen_->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_LEFT,1,1); // left
        og::runtime::current_session->myscreen_->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_RIGHT,1,1); // right
        og::runtime::current_session->myscreen_->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_BOTTOM,1,1); // bottom
        if (label.size())
            mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - (((label.size()-1)* (mytext.letters->w+1) )/2)) ,
                              static_cast<short>(yloc + (height-(mytext.letters->h))/2), label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
    }
}

void vbutton::vdisplay(Sint32 status)
{
    if(hidden || no_draw)
        return;
    if (!status) // do normal
    {
        vdisplay();
        return;
    }
    
    text& mytext = og::runtime::current_session->myscreen_->text_normal;
    if (mypixie) // then use the graphic
    {
        mypixie->draw(xloc, yloc, og::runtime::current_session->myscreen_->viewob[0].get());
        if (label.size())
            mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - (((label.size()-1)* (mytext.letters->w+1) )/2)) ,
                              static_cast<short>(yloc + (height-(mytext.letters->h))/2), label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
    }
    else
    {
        if (status == 1)
        {
            og::runtime::current_session->myscreen_->draw_box(xloc,yloc,xend-1,yend-1,BUTTON_FACING-3,1,1); // front
            og::runtime::current_session->myscreen_->draw_box(xloc,yloc,xend-2,yloc,BUTTON_BOTTOM,1,1); // top edge
            og::runtime::current_session->myscreen_->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_RIGHT,1,1); // left
            og::runtime::current_session->myscreen_->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_LEFT,1,1); // right
            og::runtime::current_session->myscreen_->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_TOP,1,1); // bottom
            if (label.size())
                mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - (((label.size()-1)* (mytext.letters->w+1) )/2)) ,
                                  static_cast<short>(yloc + (height-(mytext.letters->h))/2), label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
            og::runtime::current_session->myscreen_->buffer_to_screen(xloc,yloc,xend-xloc,yend-yloc);
        }
        else if (status == 2) // special (red) button..
        {
            og::runtime::current_session->myscreen_->draw_box(xloc,yloc,xend-1,yend-1,BUTTON_FACING+32,1,1); // front
            og::runtime::current_session->myscreen_->draw_box(xloc,yloc,xend-2,yloc,BUTTON_TOP+32,1,1); // top edge
            og::runtime::current_session->myscreen_->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_LEFT+32,1,1); // left
            og::runtime::current_session->myscreen_->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_RIGHT+32,1,1); // right
            og::runtime::current_session->myscreen_->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_BOTTOM+32,1,1); // bottom
            if (label.size())
                mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - (((label.size()-1)* (mytext.letters->w+1) )/2)) ,
                                  static_cast<short>(yloc + (height-(mytext.letters->h))/2), label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
        }
    }
    release_mouse();
    //buffers: myscreen->buffer_to_screen(0, 0, 320, 200);
    // Zardus: following isn't really needed and it messes up the fading
    //myscreen->buffer_to_screen(xloc,yloc,xend-xloc,yend-yloc);
    grab_mouse();
}

Sint32 vbutton::leftclick(button* buttons)
{
    Sint32 whichone=0;
    Sint32 retvalue=0;
    // First check hotkeys ...
    while (whichone < static_cast<Sint32>(og::runtime::current_session->allbuttons_.size())
           && og::runtime::current_session->allbuttons_[whichone])
    {
        if(buttons == nullptr || !buttons[whichone].hidden)
        {
            retvalue = og::runtime::current_session->allbuttons_[whichone]->leftclick(1);
            if (retvalue != -1)
                return retvalue;
        }
        whichone++;
    }
    // Now normal click ..
    whichone = 0;
    while (whichone < static_cast<Sint32>(og::runtime::current_session->allbuttons_.size())
           && og::runtime::current_session->allbuttons_[whichone])
    {
        if(buttons == nullptr || !buttons[whichone].hidden)
        {
            retvalue = og::runtime::current_session->allbuttons_[whichone]->leftclick(2);
            if (retvalue != -1)
                return retvalue;
        }
        whichone++;
    }
    return 0; // none worked
}

Sint32 vbutton::rightclick(button* buttons)
{
    Sint32 whichone=0;
    Sint32 retvalue=0;
    while (whichone < static_cast<Sint32>(og::runtime::current_session->allbuttons_.size())
           && og::runtime::current_session->allbuttons_[whichone])
    {
        if(buttons == nullptr || !buttons[whichone].hidden)
        {
            retvalue = og::runtime::current_session->allbuttons_[whichone]->rightclick(whichone);
            if (retvalue != -1)
                return retvalue;
        }
        whichone++;
    }
    return 0; // none worked
}

Sint32 vbutton::leftclick(Sint32 whichbutton)
{
    if(hidden)
        return -1;
    Sint32 retvalue=0;

    if (whichbutton == 1) // hotkeys
    {
        if (og::runtime::current_session->keystates_[hotkey])
        {
            og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
            vdisplay(1);
            vdisplay();
            if (myfunc)
            {
                retvalue = do_call(myfunc, arg);
            }
            while (og::runtime::current_session->keystates_[hotkey])
            {
                og::input_native::sleep_ms(1);
                get_input_events(POLL);
            }
            return retvalue;
        }
    }
    else if(whichbutton == 2) // Normal click
    {
        if (mouse_on())
        {
            og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
            vdisplay(1);
            vdisplay();
            if (myfunc)
            {
                retvalue = do_call(myfunc, arg);
            }
            return retvalue;
        }
    }
    return -1; // wasn't focused on us
}

Sint32 vbutton::rightclick(Sint32 whichbutton)
{
    if(hidden)
        return -1;
    Sint32 retvalue=0;

    if (whichbutton)
        whichbutton = 1;
    if (mouse_on())
    {
        og::runtime::current_session->myscreen_->soundp->play_sound(SOUND_BOW);
        vdisplay(1);
        vdisplay();
        if (myfunc)
        {
            retvalue = do_call_right(myfunc, arg);
        }
        return retvalue;
    }

    return -1; // wasn't focused on us
}

	Sint32 vbutton::mouse_on()
	{
	    if(hidden)
	        return 0;
	    Sint32 mousex,mousey;
	    MouseState& mymouse = query_mouse();
	    mousex = static_cast<Sint32>(mymouse.x);
	    mousey = static_cast<Sint32>(mymouse.y);

    if (mousex > xloc && mousex < xend && mousey > yloc && mousey < yend)
    {
        had_focus = 1;
        return 1;
    }
    else
    {
        if (had_focus)
        {
            had_focus = 0;
        }
        return 0;
    }
}

#ifdef TESTING
extern std::mutex& get_allbuttons_mutex();
namespace
{
struct AllButtonsLock final
{
    explicit AllButtonsLock(std::mutex& mutex) : lock_(mutex) {}
    AllButtonsLock(const AllButtonsLock&) = delete;
    AllButtonsLock& operator=(const AllButtonsLock&) = delete;
private:
    std::lock_guard<std::mutex> lock_;
};
} // namespace
#endif

vbutton * init_buttons(button * buttons, Sint32 numbuttons)
{
    TRACE("menu", "init_buttons count=%d", numbuttons);

#ifdef TESTING
    AllButtonsLock lock(get_allbuttons_mutex());
#endif

    clear_allbuttons();

    // allbuttons_/owned_buttons_ are fixed-capacity arrays. Every current caller
    // passes a small literal count, but clamp to the real capacity so a future
    // or garbage count can never drive an out-of-bounds store.
    const Sint32 cap = static_cast<Sint32>(og::runtime::current_session->allbuttons_.size());
    const Sint32 limit = (numbuttons < 0) ? 0 : (numbuttons > cap ? cap : numbuttons);
    for (Sint32 i = 0; i < limit; i++)
    {
        std::unique_ptr<vbutton, og::runtime::VButtonDeleter> owned_button(
            new vbutton(buttons[i].x,buttons[i].y,
                        buttons[i].sizex, buttons[i].sizey,
                        buttons[i].myfun, buttons[i].arg1,
                        buttons[i].label, buttons[i].hotkey));
        og::runtime::current_session->allbuttons_[i] = owned_button.get();
        owned_buttons()[static_cast<size_t>(i)] = std::move(owned_button);
        og::runtime::current_session->allbuttons_[i]->id = buttons[i].id;
        og::runtime::current_session->allbuttons_[i]->hidden = buttons[i].hidden;
        og::runtime::current_session->allbuttons_[i]->no_draw = buttons[i].no_draw;
    }

    return og::runtime::current_session->allbuttons_[0];
}

void clear_allbuttons()
{
    for (size_t i = 0; i < owned_buttons().size(); i++)
    {
        owned_buttons()[i].reset();
        og::runtime::current_session->allbuttons_[i] = nullptr;
    }
}

void draw_backdrop()
{
    Sint32 i;
    for (i=0; i < 5; i++)
        if (pks().backdrops[i])
            pks().backdrops[i]->draw(og::runtime::current_session->myscreen_->viewob[0].get());
}

void draw_buttons(button * buttons, Sint32 numbuttons)
{
    Sint32 i;
    for (i=0; i < numbuttons; i++)
    {
        if (!og::runtime::current_session->allbuttons_[i])
            continue;
        if(buttons[i].hidden || buttons[i].no_draw)
            continue;
        
        og::runtime::current_session->allbuttons_[i]->vdisplay();
        if (og::runtime::current_session->allbuttons_[i]->had_focus)
        {
            // Draw hover highlight after button draw so it survives
            // per-frame clearbuffer/redraw.
            og::runtime::current_session->myscreen_->draw_box(og::runtime::current_session->allbuttons_[i]->xloc - 1,
                               og::runtime::current_session->allbuttons_[i]->yloc - 1,
                               og::runtime::current_session->allbuttons_[i]->xend,
                               og::runtime::current_session->allbuttons_[i]->yend,
                               YELLOW,
                               0,
                               1);
        }
    }
}

Sint32 yes_or_no(Sint32 arg)
{
    return arg;
}

void toggle_effect(const std::string& category, const std::string& setting)
{
    cfg_store& config = cfg;
    if(config.is_on(category, setting))
        config.apply_setting(category, setting, "off");
    else
        config.apply_setting(category, setting, "on");
}

void toggle_rendering_engine()
{
    cfg_store& config = cfg;
    std::string engine = config.get_setting("graphics", "render");
    if(engine == "sai")
        engine = "eagle";
    else if(engine == "eagle")
        engine = "normal";
    else
        engine = "sai";
    
    config.apply_setting("graphics", "render", engine);
}

#define REDRAW 2 //we just exited a menu, so redraw your buttons
#define OK 4 //this function was successful, continue normal operation

bool picker_try_intercept_button_action(Sint32 whatfunc, Sint32 call_arg, Sint32& retvalue);

	Sint32 vbutton::do_call(Sint32 whatfunc, Sint32 call_arg)
	{
        Sint32 intercepted_retvalue = 0;
        if (picker_try_intercept_button_action(whatfunc, call_arg, intercepted_retvalue)) {
            return intercepted_retvalue;
        }

	    switch (static_cast<ButtonAction>(whatfunc))
	    {
	    case ButtonAction::BeginMenu:
	        return beginmenu(call_arg);
	    case ButtonAction::CreateTeamMenu:
	        return create_team_menu(call_arg);
	    case ButtonAction::SetPlayerMode:
	        return set_player_mode(call_arg);
	    case ButtonAction::QuitMenu:
	        quit(call_arg);
	        return 1;
	    case ButtonAction::CreateViewMenu:
	        return create_view_menu(call_arg);
	    case ButtonAction::CreateTrainMenu:
	        return create_train_menu(call_arg);
	    case ButtonAction::CreateHireMenu:
	        return create_hire_menu(call_arg);
	    case ButtonAction::CreateLoadMenu:
	        return create_load_menu(call_arg);
	    case ButtonAction::CreateSaveMenu:
	        return create_save_menu(call_arg);
	    case ButtonAction::CreateProgressMenu:
	        return create_progress_menu(call_arg);
	    case ButtonAction::GoMenu:
	        return go_menu(call_arg);
	    case ButtonAction::ReturnMenu:
	        return call_arg;
	    case ButtonAction::CycleTeamGuy:
	        return cycle_team_guy(call_arg);
	    case ButtonAction::DecreaseStat:
	        return decrease_stat(call_arg);
	    case ButtonAction::IncreaseStat:
	        return increase_stat(call_arg);
    case ButtonAction::EditGuy:
        return edit_guy(arg);
    case ButtonAction::CycleGuy:
        return cycle_guy(arg);
    case ButtonAction::AddGuy:
        return add_guy(arg);
    case ButtonAction::DoSave:
        return do_save(arg);
    case ButtonAction::DoLoad:
        return do_load(arg);
    case ButtonAction::NameGuy: // name some guy
        return name_guy(arg);
    case ButtonAction::CreateDetailMenu:
        return create_detail_menu(nullptr);
    case ButtonAction::DoSetScenLevel:
        return do_set_scen_level(arg);
    case ButtonAction::DoPickCampaign:
        return do_pick_campaign(arg);
    case ButtonAction::SetDifficulty:
        return set_difficulty();
    case ButtonAction::ChangeTeam:
        return change_teamnum(arg);
    case ButtonAction::ChangeHireTeam:
        return change_hire_teamnum(arg);
    case ButtonAction::AlliedMode:
        return change_allied();
    case ButtonAction::CycleCtfTeamCount:
        return change_ctf_teams();
    case ButtonAction::CycleCtfCaptureLimit:
        return change_ctf_caps();
    case ButtonAction::CycleCtfScenarioTroops:
        return change_ctf_troops();
    case ButtonAction::CreateTeamsMenu:
        return create_teams_menu(call_arg);
    case ButtonAction::ViewScenario:
        return create_view_scenario_menu(call_arg);
    case ButtonAction::ViewScenarioPageFlip:
        return view_scenario_page_flip(call_arg);
    case ButtonAction::CreateScenarioMenu:
        return create_scenario_menu(call_arg);
    case ButtonAction::TeamsPageFlip:
        return teams_page_flip(call_arg);
    case ButtonAction::JoinTeam:
        return teams_join_team(call_arg);
    case ButtonAction::TeamsCycleGuy:
        return teams_cycle_guy(call_arg);
    case ButtonAction::TeamsCycleGuyTeam:
        return teams_cycle_guy_team(call_arg);
    case ButtonAction::ToggleLobbyReady:
        return teams_toggle_ready();
    case ButtonAction::DoLevelEdit:
        return level_editor();
    case ButtonAction::YesOrNo:
        return yes_or_no(arg);
    case ButtonAction::MainOptions:
        return main_options();
    case ButtonAction::OpenControlSettings:
        return main_controls_options();
    case ButtonAction::OpenGameplayFxSettings:
        return gameplay_fx_options();
    case ButtonAction::OpenUiFxSettings:
        return ui_fx_options();
    case ButtonAction::OpenGraphicsFxSettings:
        return graphics_fx_options();
    case ButtonAction::OpenDifficultyMenu:
        return run_difficulty_menu();
    case ButtonAction::CycleRespawnMode:
        return change_respawn_mode();
    case ButtonAction::CycleRespawnDelay:
        return change_respawn_delay();
    case ButtonAction::TogglePermadeath:
        return change_permadeath();
    case ButtonAction::CycleGeneratorRate:
        return change_generator_rate();
    case ButtonAction::ToggleControlMode:
        return toggle_player_control_mode(arg);
    case ButtonAction::EditPlayerKeymap:
        return edit_player_keymap(arg);
    case ButtonAction::HostGame:
    case ButtonAction::JoinGame:
    case ButtonAction::Networking:
    case ButtonAction::EditNetworkAddress:
    case ButtonAction::EditNetworkPort:
    case ButtonAction::ToggleNetworkRoomCode:
    case ButtonAction::EditNetworkRoomCode:
    case ButtonAction::SubmitNetworkHost:
    case ButtonAction::SubmitNetworkJoin:
        return whatfunc;
    case ButtonAction::ShowHelp:
        show_general_help();
        return REDRAW;
    case ButtonAction::ToggleSound:
        toggle_effect("sound", "sound");
        return REDRAW;
    case ButtonAction::ToggleRenderingEngine:
        toggle_rendering_engine();
        return REDRAW;
    case ButtonAction::ToggleFullscreen:
        toggle_effect("graphics", "fullscreen");
        og::runtime::current_session->myscreen_->set_fullscreen(cfg.is_on("graphics", "fullscreen"));
        return REDRAW;
    case ButtonAction::OverscanAdjust:
        return overscan_adjust(arg);
    case ButtonAction::ToggleMiniHpBar:
        toggle_effect("effects", "mini_hp_bar");
        return REDRAW;
    case ButtonAction::ToggleHitFlash:
        toggle_effect("effects", "hit_flash");
        return REDRAW;
    case ButtonAction::ToggleHitRecoil:
        toggle_effect("effects", "hit_recoil");
        return REDRAW;
    case ButtonAction::ToggleAttackLunge:
        toggle_effect("effects", "attack_lunge");
        return REDRAW;
    case ButtonAction::ToggleHitAnim:
        toggle_effect("effects", "hit_anim");
        return REDRAW;
    case ButtonAction::ToggleDamageNumbers:
        toggle_effect("effects", "damage_numbers");
        return REDRAW;
    case ButtonAction::ToggleHealNumbers:
        toggle_effect("effects", "heal_numbers");
        return REDRAW;
    case ButtonAction::ToggleGore:
        toggle_effect("effects", "gore");
        return REDRAW;
    case ButtonAction::ToggleFloorGhosting:
        toggle_effect("graphics", "floor_ghost");
        return REDRAW;
    case ButtonAction::ToggleShadows:
        toggle_effect("effects", "shadows");
        return REDRAW;
    case ButtonAction::ToggleReflections:
        toggle_effect("effects", "reflections");
        return REDRAW;
    case ButtonAction::ToggleWeather:
        toggle_effect("effects", "weather");
        return REDRAW;
    case ButtonAction::ToggleDust:
        toggle_effect("effects", "dust");
        return REDRAW;
    case ButtonAction::ToggleDepthTint:
        toggle_effect("effects", "depth_tint");
        return REDRAW;
    case ButtonAction::ToggleTrails:
        toggle_effect("effects", "trails");
        return REDRAW;
    case ButtonAction::ToggleFireGlow:
        toggle_effect("effects", "fire_glow");
        return REDRAW;
    case ButtonAction::ToggleRipples:
        toggle_effect("effects", "ripples");
        return REDRAW;
    case ButtonAction::ToggleScreenShake:
        toggle_effect("effects", "screen_shake");
        return REDRAW;
    case ButtonAction::PickSpriteSheet:
        return do_pick_spritesheet(arg);
    case ButtonAction::RestoreDefaultSettings:
        restore_default_settings();
        cfg.load_settings();
        load_player_control_settings_from_cfg(cfg);
        og::runtime::current_session->overscan_percentage_ = static_cast<float>(
            parse_int_strict(cfg.get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
        update_overscan_setting();
        return REDRAW;
    case ButtonAction::RestoreDefaultControls:
        reset_default_player_controls();
        return REDRAW;
    default:
        return OK;
    }
}

// For right-button
Sint32 vbutton::do_call_right(Sint32 whatfunc, Sint32 call_arg)
{
    switch (static_cast<ButtonAction>(whatfunc))
    {
    case ButtonAction::DecreaseStat:
        return decrease_stat(call_arg, 5);
    case ButtonAction::IncreaseStat:
        return increase_stat(call_arg, 5);
    case ButtonAction::SetPlayerMode:
        // Right-click on the currently-selected player count deselects (spectator mode)
        if (og::runtime::current_session->myscreen_ && og::runtime::current_session->myscreen_->save_data.numplayers == static_cast<unsigned char>(call_arg))
            return set_player_mode(0);
        return 4; // not the selected button — no-op
    default:
        return 4;
    }
}
