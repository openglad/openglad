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

//vbutton functions, vbutton is a button class that will be self controlled
vbutton::vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high,
                 Sint32 func(Sint32), Sint32 pass, char *msg, unsigned char hot )
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
	strcpy(label,msg);
	next = NULL;
	//  prev = NULL;
	had_focus = 0;
	do_outline = 0;
	depressed = 0;

	mypixie = NULL; // by default, no graphic picture

	hotkey = hot;

	vdisplay();
}

vbutton::vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high,
                 Sint32 func_code, Sint32 pass, char *msg, unsigned char hot )
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
	strcpy(label,msg);
	next = NULL;
	//  prev = NULL;
	had_focus = 0;
	do_outline = 0;
	depressed = 0;

	mypixie = NULL; // no graphic by default

	hotkey = hot;

	vdisplay();
}

vbutton::vbutton(Sint32 xpos, Sint32 ypos, Sint32 wide, Sint32 high,
                 Sint32 func_code, Sint32 pass, char *msg, char family,
                 unsigned char hot )
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
	strcpy(label,msg);
	next = NULL;
	//  prev = NULL;
	had_focus = 0;
	do_outline = 0;
	depressed = 0;

	mypixie = myscreen->myloader->create_pixieN(ORDER_BUTTON1, family);

	hotkey = hot;

	width = mypixie->sizex;
	height = mypixie->sizey;
	xend = xloc + width;
	yend = yloc + height;
	vdisplay();
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
	mypixie = myscreen->myloader->create_pixieN(ORDER_BUTTON1, family);
	width = mypixie->sizex;
	height= mypixie->sizey;
	xend = xloc + width;
	yend = yloc + height;
	vdisplay();
}

void vbutton::vdisplay()
{
	if (do_outline)
	{
		vdisplay(2);
		return;
	}
	if (mypixie) // then use the graphic
	{
		mypixie->draw(xloc, yloc, myscreen->viewob[0]);
		if (strlen(label))
			mytext->write_xy( (short) ( ((xloc+xend)/2) - (((strlen(label)-1)* (mytext->sizex+1) )/2)) ,
			                  (short) (yloc + (height-(mytext->sizey))/2), label, (unsigned char) DARK_BLUE, 1);
	}
	else
	{
		myscreen->draw_box(xloc,yloc,xend-1,yend-1,BUTTON_FACING,1,1); // front
		myscreen->draw_box(xloc,yloc,xend-2,yloc,BUTTON_TOP,1,1); // top edge
		myscreen->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_LEFT,1,1); // left
		myscreen->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_RIGHT,1,1); // right
		myscreen->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_BOTTOM,1,1); // bottom
		if (strlen(label))
			mytext->write_xy( (short) ( ((xloc+xend)/2) - (((strlen(label)-1)* (mytext->sizex+1) )/2)) ,
			                  (short) (yloc + (height-(mytext->sizey))/2), label, (unsigned char) DARK_BLUE, 1);
	}
	//release_mouse();
	//myscreen->buffer_to_screen(xloc-4,yloc-4,xend+4,yend+4);
	//grab_mouse();
}

void vbutton::vdisplay(Sint32 status)
{
	if (!status) // do normal
	{
		vdisplay();
		return;
	}
	if (mypixie) // then use the graphic
	{
		mypixie->draw(xloc, yloc, myscreen->viewob[0]);
		if (strlen(label))
			mytext->write_xy( (short) ( ((xloc+xend)/2) - (((strlen(label)-1)* (mytext->sizex+1) )/2)) ,
			                  (short) (yloc + (height-(mytext->sizey))/2), label, (unsigned char) DARK_BLUE, 1);
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
			if (strlen(label))
				mytext->write_xy( (short) ( ((xloc+xend)/2) - (((strlen(label)-1)* (mytext->sizex+1) )/2)) ,
				                  (short) (yloc + (height-(mytext->sizey))/2), label, (unsigned char) DARK_BLUE, 1);
			myscreen->buffer_to_screen(xloc,yloc,xend-xloc,yend-yloc);
		}
		else if (status == 2) // special (red) button..
		{
			myscreen->draw_box(xloc,yloc,xend-1,yend-1,BUTTON_FACING+32,1,1); // front
			myscreen->draw_box(xloc,yloc,xend-2,yloc,BUTTON_TOP+32,1,1); // top edge
			myscreen->draw_box(xloc,yloc+1,xloc,yend-2,BUTTON_LEFT+32,1,1); // left
			myscreen->draw_box(xend-1,yloc+1,xend-1,yend-2,BUTTON_RIGHT+32,1,1); // right
			myscreen->draw_box(xloc+1,yend-1,xend-1,yend-1,BUTTON_BOTTOM+32,1,1); // bottom
			if (strlen(label))
				mytext->write_xy( (short) ( ((xloc+xend)/2) - (((strlen(label)-1)* (mytext->sizex+1) )/2)) ,
				                  (short) (yloc + (height-(mytext->sizey))/2), label, (unsigned char) DARK_BLUE, 1);
		}
	}
	release_mouse();
	//buffers: myscreen->buffer_to_screen(0, 0, 320, 200);
	// Zardus: following isn't really needed and it messes up the fading
	//myscreen->buffer_to_screen(xloc,yloc,xend-xloc,yend-yloc);
	grab_mouse();
}

Sint32 vbutton::leftclick()
{
	Sint32 whichone=0;
	Sint32 retvalue=0;
	// First check hotkeys ...
	while (allbuttons[whichone])
	{
		retvalue = allbuttons[whichone]->leftclick(1);
		if (retvalue != -1)
			return retvalue;
		whichone++;
	}
	// Now normal click ..
	whichone = 0;
	while (allbuttons[whichone])
	{
		retvalue = allbuttons[whichone]->leftclick(2);
		if (retvalue != -1)
			return retvalue;
		whichone++;
	}
	return 0; // none worked
}

Sint32 vbutton::rightclick()
{
	Sint32 whichone=0;
	Sint32 retvalue=0;
	while (allbuttons[whichone])
	{
		retvalue = allbuttons[whichone]->rightclick(whichone);
		if (retvalue != -1)
			return retvalue;
		whichone++;
	}
	return 0; // none worked
}

Sint32 vbutton::leftclick(Sint32 whichbutton)
{
	Sint32 retvalue=0;
	Uint8* mousekeys = query_keyboard();

	if (whichbutton == 1) // hotkeys
	{
		if (mousekeys[hotkey])
		{
			myscreen->soundp->play_sound(SOUND_BOW);
			vdisplay(1);
			vdisplay();
			if (myfunc)
			{
				retvalue = do_call(myfunc, arg);
			}
			while (mousekeys[hotkey])
				get_input_events(WAIT);
			return retvalue;
		}
	}
	else
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
			//else
			//  retvalue = fun(arg);
			//while (mousekeys[hotkey])
			//	(void)1;
			return retvalue;
			//vdisplay();
		}
	}
	return -1; // wasn't focused on us
}

Sint32 vbutton::rightclick(Sint32 whichbutton)
{
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
	Sint32 mousex,mousey;
	mymouse = query_mouse();
	mousex = mymouse[MOUSE_X];
	mousey = mymouse[MOUSE_Y];

	if (mousex > xloc && mousex < xend && mousey > yloc && mousey < yend)
	{
		if (!had_focus) // just gained focus
		{
			vdisplay();
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
			vdisplay();
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

Sint32 ventermenu(vbutton * vbuttons)
{
	vbutton * here; // = new vbutton();
	if (vbuttons == NULL)
		return 0; //if no buttons, exit
	here = vbuttons;
	while(here)
	{
		here->vdisplay();
		here = here->next;
	}
	return 1;
}

Sint32 vexitmenu(vbutton * vbuttons)
{
	vbutton * here; // = new vbutton();
	vbutton * nextb; // = new vbutton();
	if (!vbuttons)
		return 0;
	here = vbuttons;
	while(here)
	{
		nextb = here->next;
		delete(here);
		here = nextb;
	}
	return 1;
}

vbutton * buttonmenu(button * buttons, Sint32 numbuttons)
{
	//buffers: return buttonmenu(buttons, numbuttons, 1); // default is redraw screen
	return buttonmenu(buttons,numbuttons,0);
}

vbutton * buttonmenu(button * buttons, Sint32 numbuttons, Sint32 redraw)
{
	Sint32 i;

	for (i=1; i < MAX_BUTTONS; i++) // skip # 0!
	{
		if (allbuttons[i])
			delete allbuttons[i];
		allbuttons[i] = NULL;
	}

	for (i=0; i < 5; i++)
		if (backdrops[i])
			backdrops[i]->draw(myscreen->viewob[0]);

	for (i=0; i < numbuttons; i++)
	{
		allbuttons[i] = new vbutton(buttons[i].x,buttons[i].y,
		                            buttons[i].sizex, buttons[i].sizey,
		                            buttons[i].myfun, buttons[i].arg1,
		                            buttons[i].label, buttons[i].hotkey);
		myscreen->draw_box(allbuttons[i]->xloc-1,
		                   allbuttons[i]->yloc-1,
		                   allbuttons[i]->xend,
		                   allbuttons[i]->yend, 0, 0, 1);
	}

	release_mouse();

	//if (redraw)
	//	myscreen->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();
	return allbuttons[0];

}

vbutton * buttonmenu_no_backdrop(button * buttons, Sint32 numbuttons, Sint32 redraw)
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
		myscreen->draw_box(allbuttons[i]->xloc-1,
		                   allbuttons[i]->yloc-1,
		                   allbuttons[i]->xend,
		                   allbuttons[i]->yend, 0, 0, 1);
	}

	release_mouse();

	//if (redraw)
	//	myscreen->buffer_to_screen(0, 0, 320, 200);
	grab_mouse();
	return allbuttons[0];

}

//after this point old code
void clearmenu(button *buttons, short numbuttons)
{
	short i;

	// First remove the mouse ..
	release_mouse();
	for (i=0; i < numbuttons; i++)
	{
		myscreen->fastbox(buttons[i].x-1,
		                  buttons[i].y-1,
		                  buttons[i].sizex+3,
		                  buttons[i].sizey+3, 0, 1);
	}
	// Clear the WHOLE screen
	//myscreen->clearbuffer();
	//commented out temporarily to see if ok

	// DARK_BLUEisplay the mouse
	grab_mouse();
}

/*short buttonmenu(button *buttons, short numbuttons)
{
  return buttonmenu(buttons, numbuttons, 0);
}
 
short buttonmenu(button *buttons, short numbuttons, short no_clear)
{
  short buttonnum = 0;
  short i;
  //char input, temp;
  //short color = 1;
  Sint32 longtemp = 0;
//  short DARK_BLUE=0, green=0, blue=0;
//  char rdelta, gdelta, bdelta;
  char *mykeyboard;
  short dumbcount=0;
  short cyclestage = 0;
  short focuschanged = 1;
 
  mykeyboard = query_keyboard();        // get keyboard state
 
  release_mouse();
 
  for (i=0; i < numbuttons; i++)
  {
         myscreen->draw_button(buttons[i].x,
                               buttons[i].y,
                               buttons[i].x+buttons[i].sizex,
                               buttons[i].y+buttons[i].sizey,2);
         mytext->write_xy(buttons[i].x +
                         (buttons[i].sizex - (strlen(buttons[i].label)-1)*7)/2,
                          buttons[i].y + (buttons[i].sizey-6)/2,
                          buttons[i].label,(unsigned char) DARK_BLUE, 1);
  }
 
  // Highlight the current button number
  myscreen->draw_box(buttons[buttonnum].x-1,
                          buttons[buttonnum].y-1,
                          buttons[buttonnum].x + buttons[buttonnum].sizex+1,
                          buttons[buttonnum].y + buttons[buttonnum].sizey+1, ORANGE_START, 0);
 
//  myscreen->refresh();
 
  grab_mouse();
 
//  rdelta = (random(3)+1);
//  gdelta = (random(3)+1);
//  bdelta = (random(3)+1);
 
  while(1)
  {
         ++longtemp %= 90;
         // First cycle the colors ..
         if (!random(80))
                myscreen->do_cycle(cyclestage++, 8);
 
         // Cheat key for more cash ..
         if (mykeyboard[KEYSTATE_CTRL])
         {
                if (mykeyboard[KEYSTATE_KP_PLUS])
                {
                  add_money((Sint32)2);
                  return 1;
                }
                if (mykeyboard[KEYSTATE_KP_MINUS])
                {
                  add_money((Sint32)-2);
                  return 1;
                }
                if (mykeyboard[KEYSTATE_LEFTBRACKET]) // lower scen num
                {
                  if (scen_level > 1)
                         scen_level--;
                  while (mykeyboard[KEYSTATE_LEFTBRACKET])
                         dumbcount++;
                  return 1;
                }
                if (mykeyboard[KEYSTATE_RIGHTBRACKET]) // raise scen num
                {
                  scen_level++;
                  while (mykeyboard[KEYSTATE_RIGHTBRACKET])
                         dumbcount++;
                  return 1;
                }
         }
 
         // Mouse input loop ..
         mymouse = query_mouse();       // get mouse status
         for (i=0; i < numbuttons; i++)
         {
                if (i == buttonnum && focuschanged)
                {
                  focuschanged = 0;
                  release_mouse();
                  myscreen->draw_box(buttons[i].x-1,
                                buttons[i].y-1,
                                buttons[i].x + buttons[i].sizex+1,
                                buttons[i].y + buttons[i].sizey+1, ORANGE_START, 0);
                  grab_mouse();
                  buttonnum = i;
                }  // changed selected button
                if (i != buttonnum && focuschanged)
                {
                  release_mouse();
                  myscreen->draw_box(buttons[i].x-1,
                                buttons[i].y-1,
                                buttons[i].x + buttons[i].sizex+1,
                                buttons[i].y + buttons[i].sizey+1, 0, 0);
                  grab_mouse();
                }
                if (has_mouse_focus(buttons[i]) )
                {
                  if (i != buttonnum)
                  {
                         release_mouse();
                         myscreen->draw_box(buttons[buttonnum].x-1,
                                buttons[buttonnum].y-1,
                                buttons[buttonnum].x + buttons[buttonnum].sizex+1,
                                buttons[buttonnum].y + buttons[buttonnum].sizey+1,0, 0);
                         grab_mouse();
                         focuschanged = 1;
                         buttonnum = i;
                  }
                  if (mymouse[MOUSE_LEFT])      // have focus, button click
                  {
                         //release_mouse();
                         //fastbox(buttons[buttonnum].x-1,
                         //     buttons[buttonnum].y-1,
                         //     buttons[buttonnum].sizex+1,
                         //     buttons[buttonnum].sizey+1,15);
                         //grab_mouse();
                         // Wait for mouse 'up'
                         while (mymouse[MOUSE_LEFT])
                                mymouse = query_mouse();
                         if (buttons[buttonnum].fun)
                         {
                                if (!no_clear)
                                  clearmenu(buttons,numbuttons);
                                buttons[buttonnum].fun(buttons[buttonnum].arg1);
                                grab_mouse();
                                return 1;
                         }
                         else
                         {
                                if (!no_clear)
                                  clearmenu(buttons,numbuttons);
                                grab_mouse();
                                return 0;
                         }
                  } // end of check for mouse click
                }  // end of check for mouse focus
 
                if (mykeyboard[buttons[i].hotkey])      // just do ti
                {
                  if (i != buttonnum)
                  {
                         release_mouse();
                  //     fastbox(buttons[buttonnum].x-1,
                  //            buttons[buttonnum].y-1,
                  //            buttons[buttonnum].x+buttons[buttonnum].sizex+1,
                  //            buttons[buttonnum].y+buttons[buttonnum].sizey+1,0);
                         grab_mouse();
                         buttonnum = i;
                  }  // changed selected button
                  release_mouse();
                 // fastbox(buttons[buttonnum].x-1,
                 //      buttons[buttonnum].y-1,
                 //      buttons[buttonnum].x+buttons[buttonnum].sizex+1,
                 //      buttons[buttonnum].y+buttons[buttonnum].sizey+1,15);
                  grab_mouse();
                  // Wait for release of key ..
                  while (mykeyboard[buttons[i].hotkey])
                         dumbcount++;
                  if (buttons[buttonnum].fun)
                  {
                         if (!no_clear)
                           clearmenu(buttons,numbuttons);
                         buttons[buttonnum].fun(buttons[buttonnum].arg1);
                         grab_mouse();
                         return 1;
                  }
                  else
                  {
                         if (!no_clear)
                                clearmenu(buttons,numbuttons);
                         grab_mouse();
                         return 0;
                  }
                } // end of check for hot-key press
         }  // end of button-check loop
 
//       input = 0;
//       while(temp = get_SDLK_code())
//              input = temp;
 
         if (mykeyboard[KEYSTATE_DOWN])
         {
                release_mouse();
                myscreen->draw_box(buttons[buttonnum].x-1,
                        buttons[buttonnum].y-1,
                        buttons[buttonnum].x + buttons[buttonnum].sizex+1,
                        buttons[buttonnum].y + buttons[buttonnum].sizey+1,0, 0);
                grab_mouse();
                buttonnum++;
                focuschanged = 1;
                while (mykeyboard[KEYSTATE_DOWN])
                  dumbcount++;
         }
         if (mykeyboard[KEYSTATE_UP])
         {
                release_mouse();
                myscreen->draw_box(buttons[buttonnum].x-1,
                        buttons[buttonnum].y-1,
                        buttons[buttonnum].x + buttons[buttonnum].sizex+1,
                        buttons[buttonnum].y + buttons[buttonnum].sizey+1,0, 0);
                grab_mouse();
                buttonnum--;
                focuschanged = 1;
                while (mykeyboard[KEYSTATE_UP])
                  dumbcount++;
         }
         if (buttonnum < 0) buttonnum = numbuttons-1;
         buttonnum = buttonnum % numbuttons;
         //fastbox(buttons[buttonnum].x-1,
         //                     buttons[buttonnum].y-1,
         //                     buttons[buttonnum].x+buttons[buttonnum].sizex+1,
         //                     buttons[buttonnum].y+buttons[buttonnum].sizey+1,14);
 
         if (mykeyboard[KEYSTATE_RETURN])
         {
                while (mykeyboard[KEYSTATE_RETURN])
                  dumbcount++;
                if (!no_clear)
                  clearmenu(buttons,numbuttons);
                if (buttons[buttonnum].fun)
                {
                  buttons[buttonnum].fun(buttons[buttonnum].arg1);
                  grab_mouse();
                  return 1;
                }
                else
                {
                  grab_mouse();
                  return 0;
                }
         }
  }
}
*/

short has_mouse_focus(button thisbutton)
{
	Sint32 *thismouse;

	thismouse = query_mouse();

	// Check the x and y boundaries of current button to determine
	// if it has the mouse focus
	if ( (thisbutton.x <= thismouse[MOUSE_X]) &&
	        (thismouse[MOUSE_X] <= (thisbutton.x + thisbutton.sizex) ) )
		if ( (thisbutton.y <= thismouse[MOUSE_Y]) &&
		        (thismouse[MOUSE_Y] <= (thisbutton.y + thisbutton.sizey) ) )
			return 1;

	return 0;
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
		case CREATE_EDIT_MENU:
			return create_edit_menu(arg);
		case CREATE_BUY_MENU:
			return create_buy_menu(arg);
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

