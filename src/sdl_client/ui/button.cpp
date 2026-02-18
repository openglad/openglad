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
#include <openglad/input/button.h>
#include <openglad/input/input.h>
#include <openglad/core/util.h>
#include <openglad/render/pixien.h>
#include <openglad/render/text.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/runtime/game_context.h>
#include <openglad/platform/io.h>
#include <array>
#include <utility>

extern short scen_level;
extern std::array<std::unique_ptr<pixieN>, 5> backdrops;

std::array<vbutton*, MAX_BUTTONS> allbuttons{};
namespace
{
std::array<std::unique_ptr<vbutton>, MAX_BUTTONS> owned_buttons;
}
short dumbcount;
void get_input_events(bool);




MenuNav MenuNav::Up(int up)
{
    return MenuNav(up, -1,-1,-1);
}
MenuNav MenuNav::Down(int down)
{
    return MenuNav(-1, down, -1, -1);
}
MenuNav MenuNav::Left(int left)
{
    return MenuNav(-1, -1, left, -1);
}
MenuNav MenuNav::Right(int right)
{
    return MenuNav(-1, -1, -1, right);
}
MenuNav MenuNav::UpDown(int up, int down)
{
    return MenuNav(up, down, -1, -1);
}
MenuNav MenuNav::UpLeft(int up, int left)
{
    return MenuNav(up, -1, left, -1);
}
MenuNav MenuNav::UpRight(int up, int right)
{
    return MenuNav(up, -1, -1, right);
}
MenuNav MenuNav::UpDownLeft(int up, int down, int left)
{
    return MenuNav(up, down, left, -1);
}
MenuNav MenuNav::UpDownRight(int up, int down, int right)
{
    return MenuNav(up, down, -1, right);
}
MenuNav MenuNav::UpLeftRight(int up, int left, int right)
{
    return MenuNav(up, -1, left, right);
}
MenuNav MenuNav::DownLeft(int down, int left)
{
    return MenuNav(-1, down, left, -1);
}
MenuNav MenuNav::DownRight(int down, int right)
{
    return MenuNav(-1, down, -1, right);
}
MenuNav MenuNav::DownLeftRight(int down, int left, int right)
{
    return MenuNav(-1, down, left, right);
}
MenuNav MenuNav::LeftRight(int left, int right)
{
    return MenuNav(-1, -1, left, right);
}
MenuNav MenuNav::UpDownLeftRight(int up, int down, int left, int right)
{
    return MenuNav(up, down, left, right);
}
MenuNav MenuNav::All(int up, int down, int left, int right)
{
    return MenuNav(up, down, left, right);
}
MenuNav MenuNav::None()
{
    return MenuNav();
}

MenuNav::MenuNav()
    : up(-1), down(-1), left(-1), right(-1)
{}

MenuNav::MenuNav(int up_idx, int down_idx, int left_idx, int right_idx)
    : up(up_idx), down(down_idx), left(left_idx), right(right_idx)
{}



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

    mypixie = myscreen->level_data.myloader->create_pixieN_owned(Order::Button1, family);

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
    had_focus = do_outline = depressed = 0;
    mypixie = nullptr;
}

vbutton::~vbutton()
{
    // No manual ownership; menu buttons are owned by `owned_buttons` in this TU.
}

void vbutton::set_graphic(char family)
{
    mypixie = myscreen->level_data.myloader->create_pixieN_owned(Order::Button1, family);
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
    
    text& mytext = myscreen->text_normal;
    if (mypixie) // then use the graphic
    {
        mypixie->draw(xloc, yloc, myscreen->viewob[0].get());
        if (label.size())
            mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - (((label.size()-1)* (mytext.letters->w+1) )/2)) ,
                              static_cast<short>(yloc + (height-(mytext.letters->h))/2), label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
    }
    else
    {
        myscreen->draw_box(xloc,yloc,xend-1,yend-1,color,1,1); // front
        myscreen->draw_box(xloc,yloc,xend-2,yloc,BUTTON_TOP,1,1); // top edge
        myscreen->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_LEFT,1,1); // left
        myscreen->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_RIGHT,1,1); // right
        myscreen->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_BOTTOM,1,1); // bottom
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
    
    text& mytext = myscreen->text_normal;
    if (mypixie) // then use the graphic
    {
        mypixie->draw(xloc, yloc, myscreen->viewob[0].get());
        if (label.size())
            mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - (((label.size()-1)* (mytext.letters->w+1) )/2)) ,
                              static_cast<short>(yloc + (height-(mytext.letters->h))/2), label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
    }
    else
    {
        if (status == 1)
        {
            myscreen->draw_box(xloc,yloc,xend-1,yend-1,BUTTON_FACING-3,1,1); // front
            myscreen->draw_box(xloc,yloc,xend-2,yloc,BUTTON_BOTTOM,1,1); // top edge
            myscreen->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_RIGHT,1,1); // left
            myscreen->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_LEFT,1,1); // right
            myscreen->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_TOP,1,1); // bottom
            if (label.size())
                mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - (((label.size()-1)* (mytext.letters->w+1) )/2)) ,
                                  static_cast<short>(yloc + (height-(mytext.letters->h))/2), label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
            myscreen->buffer_to_screen(xloc,yloc,xend-xloc,yend-yloc);
        }
        else if (status == 2) // special (red) button..
        {
            myscreen->draw_box(xloc,yloc,xend-1,yend-1,BUTTON_FACING+32,1,1); // front
            myscreen->draw_box(xloc,yloc,xend-2,yloc,BUTTON_TOP+32,1,1); // top edge
            myscreen->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_LEFT+32,1,1); // left
            myscreen->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_RIGHT+32,1,1); // right
            myscreen->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_BOTTOM+32,1,1); // bottom
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
    while (allbuttons[whichone])
    {
        if(buttons == nullptr || !buttons[whichone].hidden)
        {
            retvalue = allbuttons[whichone]->leftclick(1);
            if (retvalue != -1)
                return retvalue;
        }
        whichone++;
    }
    // Now normal click ..
    whichone = 0;
    while (allbuttons[whichone])
    {
        if(buttons == nullptr || !buttons[whichone].hidden)
        {
            retvalue = allbuttons[whichone]->leftclick(2);
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
    while (allbuttons[whichone])
    {
        if(buttons == nullptr || !buttons[whichone].hidden)
        {
            retvalue = allbuttons[whichone]->rightclick(whichone);
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
        if (keystates[hotkey])
        {
            myscreen->soundp->play_sound(SOUND_BOW);
            vdisplay(1);
            vdisplay();
            if (myfunc)
            {
                retvalue = do_call(myfunc, arg);
            }
            while (keystates[hotkey])
            {
                SDL_Delay(1);
                get_input_events(POLL);
            }
            return retvalue;
        }
    }
    else if(whichbutton == 2) // Normal click
    {
        if (mouse_on())
        {
            myscreen->soundp->play_sound(SOUND_BOW);
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
        myscreen->soundp->play_sound(SOUND_BOW);
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
        if (!had_focus) // just gained focus
        {
            //vdisplay();
            if (mypixie)
                myscreen->draw_box(xloc-1, yloc-1, xend, yend, 27, 0, 1);
            else
                myscreen->draw_box(xloc-1, yloc-1, xend, yend, 27, 0, 1);
            myscreen->buffer_to_screen(0, 0, 320, 200);
            
            had_focus = 1;
        }
        return 1;
    }
    else
    {
        if (had_focus)
        {
            //vdisplay();
            if (mypixie)
                myscreen->draw_box(xloc-1, yloc-1, xend, yend, 0, 0, 1);
            else
                myscreen->draw_box(xloc-1, yloc-1, xend, yend, 0, 0, 1);
            myscreen->buffer_to_screen(0, 0, 320, 200);
            had_focus = 0;
        }
        return 0;
    }
}

#ifdef TESTING
extern SDL_mutex* get_allbuttons_mutex();
namespace
{
struct SdlMutexLock final
{
    explicit SdlMutexLock(SDL_mutex* m) : m_(m) { SDL_LockMutex(m_); }
    ~SdlMutexLock() { SDL_UnlockMutex(m_); }
    SdlMutexLock(const SdlMutexLock&) = delete;
    SdlMutexLock& operator=(const SdlMutexLock&) = delete;
private:
    SDL_mutex* m_;
};
} // namespace
#endif

vbutton * init_buttons(button * buttons, Sint32 numbuttons)
{
    TRACE("menu", "init_buttons count=%d", numbuttons);

#ifdef TESTING
    SdlMutexLock lock(get_allbuttons_mutex());
#endif

    clear_allbuttons();

    for (Sint32 i = 0; i < numbuttons; i++)
    {
        auto owned_button = std::make_unique<vbutton>(buttons[i].x,buttons[i].y,
                                                      buttons[i].sizex, buttons[i].sizey,
                                                      buttons[i].myfun, buttons[i].arg1,
                                                      buttons[i].label, buttons[i].hotkey);
        allbuttons[i] = owned_button.get();
        owned_buttons[static_cast<size_t>(i)] = std::move(owned_button);
        allbuttons[i]->id = buttons[i].id;
        allbuttons[i]->hidden = buttons[i].hidden;
        allbuttons[i]->no_draw = buttons[i].no_draw;
    }

    return allbuttons[0];
}

void clear_allbuttons()
{
    for (size_t i = 0; i < owned_buttons.size(); i++)
    {
        owned_buttons[i].reset();
        allbuttons[i] = nullptr;
    }
}

void draw_backdrop()
{
    Sint32 i;
    for (i=0; i < 5; i++)
        if (backdrops[i])
            backdrops[i]->draw(myscreen->viewob[0].get());
}

void draw_buttons(button * buttons, Sint32 numbuttons)
{
    Sint32 i;
    for (i=0; i < numbuttons; i++)
    {
        if(buttons[i].hidden || buttons[i].no_draw)
            continue;
        
        allbuttons[i]->vdisplay();
        myscreen->draw_box(allbuttons[i]->xloc-1,
                           allbuttons[i]->yloc-1,
                           allbuttons[i]->xend,
                           allbuttons[i]->yend, 0, 0, 1);
    }
}

Sint32 yes_or_no(Sint32 arg)
{
    return arg;
}

static cfg_store& active_config()
{
    if (ctx().active_config())
        return *ctx().active_config();
    return cfg;
}

void toggle_effect(const std::string& category, const std::string& setting)
{
    cfg_store& config = active_config();
    if(config.is_on(category, setting))
        config.apply_setting(category, setting, "off");
    else
        config.apply_setting(category, setting, "on");
}

void toggle_rendering_engine()
{
    cfg_store& config = active_config();
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

	    switch (button_action_from_id(whatfunc))
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
    case ButtonAction::DoLevelEdit:
        return level_editor();
    case ButtonAction::YesOrNo:
        return yes_or_no(arg);
    case ButtonAction::MainOptions:
        return main_options();
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
        myscreen->set_fullscreen(active_config().is_on("graphics", "fullscreen"));
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
    case ButtonAction::RestoreDefaultSettings:
        restore_default_settings();
        active_config().load_settings();
        overscan_percentage = static_cast<float>(
            parse_int_strict(active_config().get_setting("graphics", "overscan_percentage")).value_or(0)) / 100.0f;
        update_overscan_setting();
        return REDRAW;
    default:
        return OK;
    }
}

// For right-button
Sint32 vbutton::do_call_right(Sint32 whatfunc, Sint32 call_arg)
{
    switch (button_action_from_id(whatfunc))
    {
    case ButtonAction::DecreaseStat:
        return decrease_stat(call_arg, 5);
    case ButtonAction::IncreaseStat:
        return increase_stat(call_arg, 5);
    default:
        return 4;
    }
}
