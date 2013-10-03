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
#include "button.h"

extern short scen_level;
extern pixieN *backdrops[5];

vbutton * allbuttons[MAX_BUTTONS];
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

MenuNav::MenuNav(int up, int down, int left, int right)
    : up(up), down(down), left(left), right(right)
{}



//vbutton functions, vbutton is a button class that will be self controlled
vbutton::vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high,
                 Sint32 func(Sint32), Sint32 pass, const std::string& msg, int hot )
{
    arg = pass;
    fun = func;
    myfunc = 0;
    xloc = xpos;
    yloc = ypos;
    width = wide;
    height = high;
    xend = xloc + width;
    yend = yloc + height;
    label = msg;
    next = NULL;
    //  prev = NULL;
    had_focus = 0;
    do_outline = 0;
    depressed = 0;

    mypixie = NULL; // by default, no graphic picture

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
    fun = NULL; // don't use this!
    myfunc = func_code;
    xloc = xpos;
    yloc = ypos;
    width = wide;
    height = high;
    xend = xloc + width;
    yend = yloc + height;
    label = msg;
    next = NULL;
    //  prev = NULL;
    had_focus = 0;
    do_outline = 0;
    depressed = 0;

    mypixie = NULL; // no graphic by default

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
    fun = NULL; // don't use this!
    myfunc = func_code;
    xloc = xpos;
    yloc = ypos;
    width = wide;
    height = high;
    xend = xloc + width;
    yend = yloc + height;
    label = msg;
    next = NULL;
    //  prev = NULL;
    had_focus = 0;
    do_outline = 0;
    depressed = 0;

    mypixie = myscreen->level_data.myloader->create_pixieN(ORDER_BUTTON1, family);

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
    next = NULL;
    //  prev = NULL;
    had_focus = do_outline = depressed = 0;
    mypixie = NULL;
}

vbutton::~vbutton()
{
    //myscreen->draw_box(xloc-4,yloc-4,xend+4,yend+4,0,1,1);
    delete mypixie;

    /*
      release_mouse();
      myscreen->buffer_to_screen(xloc-4,yloc-4,xend + 4, yend+4);
      grab_mouse();

      if (next != NULL)
      {
        delete next;
        next = NULL;
      }
    */
}

void vbutton::set_graphic(char family)
{
    if (mypixie)
        delete mypixie;
    mypixie = myscreen->level_data.myloader->create_pixieN(ORDER_BUTTON1, family);
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
    if (mypixie) // then use the graphic
    {
        mypixie->draw(xloc, yloc, myscreen->viewob[0]);
        if (label.size())
            mytext->write_xy( (short) ( ((xloc+xend)/2) - (((label.size()-1)* (mytext->letters.w+1) )/2)) ,
                              (short) (yloc + (height-(mytext->letters.h))/2), label.c_str(), (unsigned char) DARK_BLUE, 1);
    }
    else
    {
        myscreen->draw_box(xloc,yloc,xend-1,yend-1,color,1,1); // front
        myscreen->draw_box(xloc,yloc,xend-2,yloc,BUTTON_TOP,1,1); // top edge
        myscreen->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_LEFT,1,1); // left
        myscreen->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_RIGHT,1,1); // right
        myscreen->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_BOTTOM,1,1); // bottom
        if (label.size())
            mytext->write_xy( (short) ( ((xloc+xend)/2) - (((label.size()-1)* (mytext->letters.w+1) )/2)) ,
                              (short) (yloc + (height-(mytext->letters.h))/2), label.c_str(), (unsigned char) DARK_BLUE, 1);
    }
    //release_mouse();
    //myscreen->buffer_to_screen(xloc-4,yloc-4,xend+4,yend+4);
    //grab_mouse();
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
    if (mypixie) // then use the graphic
    {
        mypixie->draw(xloc, yloc, myscreen->viewob[0]);
        if (label.size())
            mytext->write_xy( (short) ( ((xloc+xend)/2) - (((label.size()-1)* (mytext->letters.w+1) )/2)) ,
                              (short) (yloc + (height-(mytext->letters.h))/2), label.c_str(), (unsigned char) DARK_BLUE, 1);
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
                mytext->write_xy( (short) ( ((xloc+xend)/2) - (((label.size()-1)* (mytext->letters.w+1) )/2)) ,
                                  (short) (yloc + (height-(mytext->letters.h))/2), label.c_str(), (unsigned char) DARK_BLUE, 1);
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
                mytext->write_xy( (short) ( ((xloc+xend)/2) - (((label.size()-1)* (mytext->letters.w+1) )/2)) ,
                                  (short) (yloc + (height-(mytext->letters.h))/2), label.c_str(), (unsigned char) DARK_BLUE, 1);
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
        if(buttons == NULL || !buttons[whichone].hidden)
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
        if(buttons == NULL || !buttons[whichone].hidden)
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
        if(buttons == NULL || !buttons[whichone].hidden)
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
                get_input_events(WAIT);
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
    mousex = mymouse.x;
    mousey = mymouse.y;

    if (mousex > xloc && mousex < xend && mousey > yloc && mousey < yend)
    {
        if (!had_focus) // just gained focus
        {
            //vdisplay();
            // Zardus: FIX: comment release and grab mouse out cause there's no need for them here and they're causing choppyness on buttons
            // release_mouse();
            if (mypixie)
                myscreen->draw_box(xloc-1, yloc-1, xend, yend, 27, 0, 1);
            else
                myscreen->draw_box(xloc-1, yloc-1, xend, yend, 27, 0, 1);
            myscreen->buffer_to_screen(0, 0, 320, 200);
            // grab_mouse();
            had_focus = 1;
        }
        return 1;
    }
    else
    {
        if (had_focus)
        {
            //vdisplay();
            // release_mouse();
            if (mypixie)
                myscreen->draw_box(xloc-1, yloc-1, xend, yend, 0, 0, 1);
            else
                myscreen->draw_box(xloc-1, yloc-1, xend, yend, 0, 0, 1);
            myscreen->buffer_to_screen(0, 0, 320, 200);
            // grab_mouse();
            had_focus = 0;
        }
        return 0;
    }
}

vbutton * init_buttons(button * buttons, Sint32 numbuttons)
{
    Sint32 i;

    for (i=1; i < MAX_BUTTONS; i++) // skip # 0!
    {
        if (allbuttons[i])
            delete allbuttons[i];
        allbuttons[i] = NULL;
    }
    
    for (i=0; i < numbuttons; i++)
    {
        allbuttons[i] = new vbutton(buttons[i].x,buttons[i].y,
                                    buttons[i].sizex, buttons[i].sizey,
                                    buttons[i].myfun, buttons[i].arg1,
                                    buttons[i].label, buttons[i].hotkey);
        allbuttons[i]->hidden = buttons[i].hidden;
        allbuttons[i]->no_draw = buttons[i].no_draw;
    }

    return allbuttons[0];
}

void draw_backdrop()
{
    Sint32 i;
    for (i=0; i < 5; i++)
        if (backdrops[i])
            backdrops[i]->draw(myscreen->viewob[0]);
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

Sint32 vbutton::do_call(Sint32 whatfunc, Sint32 arg)
{
    switch (whatfunc)
    {
    case BEGINMENU:
        return beginmenu(arg);
    case CREATE_TEAM_MENU:
        return create_team_menu(arg);
    case SET_PLAYER_MODE:
        return set_player_mode(arg);
    case QUIT_MENU:
        quit(arg);
        return 1;
    case CREATE_VIEW_MENU:
        return create_view_menu(arg);
    case CREATE_TRAIN_MENU:
        return create_train_menu(arg);
    case CREATE_HIRE_MENU:
        return create_hire_menu(arg);
    case CREATE_LOAD_MENU:
        return create_load_menu(arg);
    case CREATE_SAVE_MENU:
        return create_save_menu(arg);
    case GO_MENU:
        return go_menu(arg);
    case RETURN_MENU:
        return arg;
    case CYCLE_TEAM_GUY:
        return cycle_team_guy(arg);
    case DECREASE_STAT:
        return decrease_stat(arg);
    case INCREASE_STAT:
        return increase_stat(arg);
    case EDIT_GUY:
        return edit_guy(arg);
    case CYCLE_GUY:
        return cycle_guy(arg);
    case ADD_GUY:
        return add_guy(arg);
    case DO_SAVE:
        return do_save(arg);
    case DO_LOAD:
        return do_load(arg);
    case NAME_GUY: // name some guy
        return name_guy(arg);
    case CREATE_DETAIL_MENU:
        return create_detail_menu(NULL);
    case DO_SET_SCEN_LEVEL:
        return do_set_scen_level(arg);
    case DO_PICK_CAMPAIGN:
        return do_pick_campaign(arg);
    case SET_DIFFICULTY:
        return set_difficulty();
    case CHANGE_TEAM:
        return change_teamnum(arg);
    case CHANGE_HIRE_TEAM:
        return change_hire_teamnum(arg);
    case ALLIED_MODE:
        return change_allied();
    case DO_LEVEL_EDIT:
        return level_editor();
    case YES_OR_NO:
        return yes_or_no(arg);
    default:
        return 4;
    }
}

// For right-button
Sint32 vbutton::do_call_right(Sint32 whatfunc, Sint32 arg)
{
    switch (whatfunc)
    {
    case DECREASE_STAT:
        return decrease_stat(arg, 5);
    case INCREASE_STAT:
        return increase_stat(arg, 5);
    default:
        return 4;
    }
}

