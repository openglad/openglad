#include "input.h"

#include "graph.h"
#include "colors.h"
#include <fstream.h>
#include <stdlib.h>
// Z's script: #include <dos.h>
// Z's script: #include <i86.h>

//these are for chad's team info page
#define VIEW_TEAM_TOP    2
#define VIEW_TEAM_LEFT   20
#define VIEW_TEAM_BOTTOM 198
#define VIEW_TEAM_RIGHT  280


//joystick tolerance defines
#define TOLERANCE 50

#define COUNT_TIME 3500
//note these are kind of backwards, they are screen oriented

// These are keyboard defines .. high-level
#define KEY_UP                  0
#define KEY_UP_RIGHT            1
#define KEY_RIGHT               2
#define KEY_DOWN_RIGHT          3
#define KEY_DOWN                4
#define KEY_DOWN_LEFT           5
#define KEY_LEFT                6
#define KEY_UP_LEFT             7
#define KEY_FIRE                8
#define KEY_SPECIAL             9
#define KEY_SWITCH              10
#define KEY_SPECIAL_SWITCH      11
#define KEY_YELL                12
#define KEY_SHIFTER             13
#define KEY_PREFS               14
#define KEY_CHEAT               15
// Now define the arrays with their default values
static char key1[] = {
  SDLK_UP, SDLK_PAGEUP, SDLK_RIGHT, SDLK_PAGEDOWN,  // movements
  SDLK_DOWN, SDLK_END, SDLK_LEFT, SDLK_HOME,
  SDLK_INSERT, SDLK_RETURN,                    // fire & special
  SDLK_KP_PLUS,                          // switch guys
  SDLK_KP_MINUS,                         // change special
  SDLK_KP5,                                // Yell
  SDLK_DELETE,                                // Shifter
  SDLK_KP_MULTIPLY,                         // Options menu
  SDLK_F8,                                    // Cheat key
};

static char key2[] = {
  SDLK_w, SDLK_e, SDLK_d, SDLK_c,  // movements
  SDLK_x, SDLK_z, SDLK_a, SDLK_q,
  SDLK_LCTRL, SDLK_LALT,                    // fire & special
  SDLK_TAB,                               // switch guys
  SDLK_1,                                 // change special
  SDLK_s,                                 // Yell
  SDLK_LSHIFT,                        // Shifter
  SDLK_2,                                 // Options menu
  SDLK_F5,                                 // Cheat key
};

static char key3[] = {
  SDLK_i, SDLK_o, SDLK_l, SDLK_PERIOD,  // movements
  SDLK_COMMA, SDLK_m, SDLK_j, SDLK_u,
  SDLK_SPACE, SDLK_SEMICOLON,                    // fire & special
  SDLK_BACKSPACE,                               // switch guys
  SDLK_7,                                 // change special
  SDLK_k,                                 // Yell
  SDLK_RSHIFT,                        // Shifter
  SDLK_8,                                 // Options menu
  SDLK_F7,                                 // Cheat key
};

static char key4[] = {
  SDLK_t, SDLK_y, SDLK_h, SDLK_n,  // movements
  SDLK_b, SDLK_v, SDLK_f, SDLK_r,
  SDLK_5, SDLK_6,                    // fire & special
  SDLK_EQUALS,                               // switch guys
  SDLK_3,                                 // change special
  SDLK_g,                                 // Yell
  SDLK_MINUS,                        // Shifter
  SDLK_4,                                 // Options menu
  SDLK_F6,                                 // Cheat key
};

// This is for saving/loading the key preferences
long save_key_prefs();
long load_key_prefs();
unsigned char get_keypress();
#define KEY_FILE "keyprefs.dat"

// This only exists so we can use the array constructor
//   for our prefs object (grumble grumble)
static char *normalkeys[] = {key1,key2,key3,key4};
static char keys[4][16];

// ** OUR prefs object! **
static options theprefs;

//#define viewscreen_X 60  // These are the dimensions of the viewscreen
//#define viewscreen_Y 44  // viewport

// ************************************************************
//  VIEWSCREEN -- It's nothing like viewscreen, it just looks like it
// ************************************************************
/*
  viewscreen(char,short,short,screen)    - initializes the viewscreen data (pix = char)
  short draw()
*/

// viewscreen -- this initializes the graphics data for the viewscreen,
// as well as its graphics x and y size.  In addition, it informs
// the viewscreen of the screen object it is linked to.
viewscreen::viewscreen(short x, short y, short width,
                       short height, short whatnum, screen  *myscreen)
{
  long i;

  screenp = myscreen;
  xview = width;
  yview = height;
  topx = topy = 0;
  xloc = x;  // where to display on the physical screen
  yloc = y;
  endx = xloc+width;
  endy = yloc+height;
  //buffer = (char  *)new char[xview*yview];
  control = NULL;
  gamma = 0;
  prefsob = &theprefs;

  // Key entries ..
  mynum = whatnum;              // what viewscreen am I?
  mykeys = keys[mynum]; // assign keyboard mappings

  // Set preferences to default values
/*  
  prefs[PREF_LIFE]  = PREF_LIFE_BOTH; // display hp/sp bars and numbers
  prefs[PREF_SCORE] = PREF_SCORE_ON;  // display score/exp info
  prefs[PREF_VIEW]  = PREF_VIEW_FULL; // start at full screen
  prefs[PREF_JOY]   = PREF_NO_JOY; //default to no joystick
  prefs[PREF_RADAR] = PREF_RADAR_ON;
  prefs[PREF_FOES]  = PREF_FOES_ON;
  prefs[PREF_GAMMA] = 0;
*/
  //load_key_prefs(); // load key prefs, if present
  prefsob->load(this);
  
  myradar = new radar(this, screenp, mynum);
  radarstart = 0; //the radar has not yet been started

  screentext = new text(myscreen);
  for (i=0; i < MAX_MESSAGES; i++)
  {
    textcycles[i] = 0;
    textlist[i][0] = 0; // null message
  }

/*
  joyaligned = 0; //by default not aligned
  joyright = 608; joydown = 498; //make sure these will be small
  joyleft = 15; joyup = 12; //make sure these start large
  joycenterx = 332; joycentery = 290; //start these at reasonable values
*/  
  resize(prefs[PREF_VIEW]); // Properly resize the viewscreen
}

// Destruct the viewscreen and its variables
viewscreen::~viewscreen()
{
  if (myradar)
    delete myradar;
  myradar = NULL;
  if (screentext)
    delete screentext;
  screentext = NULL;
  //delete buffer;
}

void viewscreen::clear()
{
  unsigned short i;

  for (i=0;i<64000;i++)
  {
         screenp->videobuffer[i] = 0;
  }
}

short viewscreen::redraw()
{
  short i,j;
  short xneg = 0;
  short yneg = 0;
  walker  *controlob = control;
  pixieN  **backp = screenp->back;
  unsigned char  * gridp = screenp->grid;
  unsigned short maxx = screenp->maxx;
  unsigned short maxy = screenp->maxy;

  // check if we are partially shorto a grid square and require
  //   extra row
  if (controlob)
  {
    topx = controlob->xpos - (xview - controlob->sizex)/2;
    topy = controlob->ypos - (yview - controlob->sizey)/2;
  }
  else // no control object now ..
  {
    topx = screenp->topx;
    topy = screenp->topy;
  }


  if (topx < 0)
    xneg = 1;
  if (topy < 0)
    yneg = 1;

  //note  >> 4 is equivalent to /16 but faster, since it doesn't divide
  //likewise <<4 is equivalent to *16, but faster

  for (j=(topy/GRID_SIZE)-yneg;j < ((topy+(yview))/GRID_SIZE) +1; j++)
    for (i=(topx/GRID_SIZE)-xneg;i < ((topx+(xview))/GRID_SIZE) +1; i++)
    {
      // NOTE: back is a PIXIEN.
      // background graphic [grid(x,y)] -> put in buffer
      if (i<0 || j<0 || i>=maxx || j>=maxy)
      {
        if (j == -1 && i>-1 && i<maxx)  // show side of wall
          backp[PIX_WALLSIDE1]->draw(i*GRID_SIZE,j*GRID_SIZE, this);
        else if (j == -2 && i>-1 && i<maxx)  // show top side of wall
          backp[PIX_H_WALL1]->draw(i*GRID_SIZE,j*GRID_SIZE, this);
        else                                                                  // show only top of wall
          backp[PIX_WALLTOP_H]->draw(i*GRID_SIZE,j*GRID_SIZE, this);
      }
      else
        backp[gridp[i + maxx * j]]->draw(i*GRID_SIZE,j*GRID_SIZE, this);
    }

  draw_obs(); //moved here to put the radar on top of obs
  if (prefs[PREF_RADAR] == PREF_RADAR_ON)
    myradar->draw();
  display_text();
  return 1;

}

void viewscreen::display_text()
{
  long i;

  for (i=0; i < MAX_MESSAGES; i++)
  {
    if (textcycles[i] > 0)  // Display text if there's any there ..
    {
      textcycles[i]--;
      screentext->write_xy( (xview-strlen(textlist[i])*6)/2,
        30+i*6, textlist[i], YELLOW, this );
    }
  }

  // Clean up any empty slots
  for (i=0; i < MAX_MESSAGES; i++)
    if (textcycles[i] < 1 && strlen(textlist[i]) )
      shift_text(i); // shift text up, starting at position i
}

void viewscreen::shift_text(long row)
{
  long i;

  for (i=row; i < (MAX_MESSAGES-1) ; i++)
  {
    strcpy(textlist[i], textlist[i+1]);
    textcycles[i] = textcycles[i+1];
  }
  textlist[MAX_MESSAGES-1][0] = 0;
  textcycles[MAX_MESSAGES-1] = 0;
}

short viewscreen::refresh()
{
  // The first two values are screwy... I don't know why
  screenp->buffer_to_screen(xloc, yloc, xview, yview);
  return 1;
}

short viewscreen::input(char inputthing)
{
  static text mytext(screenp);
  static char somemessage[80];
  long *joystick;
  oblink *templink;
  char sillytext[80];
  int  counter;

  static short prefcount[6] = {0, 0, 0, 0, 0, 0};
  short i;
  oblink  *here, *tempobj, *helpme;
  //short step;
  char *inputkeyboard;
  long dumbcount=0;
  static short changedchar[6] = {0, 0, 0, 0, 0, 0};   // for switching guys
  static short changedchar2[6]= {0, 0, 0, 0, 0, 0};  // for switching TYPE of guy
  static short changedspec[6]= {0, 0, 0, 0, 0, 0};  // for switching special
  static short changedteam[6] = {0, 0, 0, 0, 0, 0};  // for switching team
  //buffers: PORT: this doesn't compile: union REGS inregs,outregs;
  short newfam; //oldfam?
  unsigned long totaltime, totalframes, framespersec;
  walker  * blood;
  walker *newob; // for general-purpose use
  walker  * oldcontrol = control; // So we know if we changed guys
  char view_team_on = 0;

  inputkeyboard = query_keyboard();
  if (inputthing) dumbcount++;

//  joystick = query_joy();
//  if (prefs[PREF_JOY] == PREF_USE_JOY)
//  {
//    joystick = query_joy();
//    sprintf(somemessage, "X:%ld Y:%ld B1:%ld B2:%ld B3:%ld B4:%ld", 
//                         joystick[JOY_X], joystick[JOY_Y],
//                         joystick[JOY_B1], joystick[JOY_B2],
//                         joystick[JOY_B3],joystick[JOY_B4]);
//    set_display_text(somemessage, 1);
//  }

  if ( (prefs[PREF_JOY] == PREF_USE_JOY) && !joyaligned)
    align_joy();

  if ( (prefs[PREF_JOY] == PREF_USE_JOY) && joyaligned)
    input_joy();

  if ( (prefs[PREF_JOY] == PREF_NO_JOY) && joyaligned )
    remove_joy();
    
  if (control && control->user == -1)
  {
         control->set_act_type(ACT_CONTROL);
         control->user = (char) mynum;
         control->stats->clear_command();
  }

  if (!control || control->dead)
  {
    // First look for a player character, not already controlled
    here = screenp->oblist;
    counter = 0;
    while(counter < 2)
    {
      if (here->ob &&
          !here->ob->dead &&
          here->ob->query_order() == ORDER_LIVING &&
          here->ob->user == -1 && // mean's we're not player-controlled
          here->ob->myguy &&
          here->ob->team_num == my_team) // makes a difference for PvP
        break;
      here = here->next;
      if (!here)
      {
        counter++;
        if (counter < 2)
          here = screenp->oblist;
      }
    }
    if (!here)
    {
      // Second, look for anyone on our team, NPC or not
      here = screenp->oblist;
      counter = 0;
      while(counter < 2)
      {
        if (here->ob &&
            !here->ob->dead &&
            here->ob->query_order() == ORDER_LIVING &&
            here->ob->user == -1 && // mean's we're not player-controlled
            here->ob->team_num == my_team) // makes a difference for PvP
          break;
        here = here->next;
        if (!here)
        {
          counter++;
          if (counter < 2)
            here = screenp->oblist;
        }
      }
    }  // done with second search

    if (!here)
    {
      // Now try for ANYONE who's left alive ..
      here = screenp->oblist;
      counter = 0;
      while(counter < 2)
      {
        if (here->ob &&
            !here->ob->dead &&
            here->ob->query_order() == ORDER_LIVING &&
            here->ob->myguy != NULL)
          break;
        here = here->next;
        if (!here)
        {
          counter++;
          if (counter < 2)
            here = screenp->oblist;
        }
      }
    }  // done with all searches
    
    if (!here)  // then there's nobody left!
      return screenp->endgame(1);
    control = here->ob;
    if (control->user == -1)
      control->user = mynum; // show that we're controlled now
    control->set_act_type(ACT_CONTROL);
    screenp->control_hp = control->stats->hitpoints;
  }

  if (control && control->bonus_rounds) // do we have extra rounds?
  {
    control->bonus_rounds--;
    i = input('0');
    if (control->lastx || control->lasty)
      control->walk();
  }

  //step = control->stepsize;
  if (inputkeyboard[SDLK_F3] && !inputkeyboard[mykeys[KEY_CHEAT]])
  {
    totaltime = (query_timer_control() - screenp->timerstart)/72;
    totalframes = (screenp->framecount);
    framespersec = totalframes / totaltime;
    sprintf(somemessage, "%d FRAMES PER SEC", framespersec);
    screenp->viewob[0]->set_display_text(somemessage, STANDARD_TEXT_TIME);
  }

  if (inputkeyboard[SDLK_F4] && !inputkeyboard[mykeys[KEY_CHEAT]]) // Memory report
    screenp->report_mem();

  if (inputkeyboard[mykeys[KEY_PREFS]] && !inputkeyboard[mykeys[KEY_CHEAT]])
    options_menu();


  // TAB (ALONE) WILL SWITCH CONTROL TO THE NEXT GUY ON MY TEAM
  if (!inputkeyboard[mykeys[KEY_SWITCH]])
    changedchar[mynum] = 0;

  if (inputkeyboard[mykeys[KEY_SWITCH]] && !inputkeyboard[mykeys[KEY_SHIFTER]] 
      && !changedchar[mynum] && !inputkeyboard[mykeys[KEY_CHEAT]])
  {
    changedchar[mynum] = 1;
    if (control->user == mynum)
    {
      control->restore_act_type();
      control->user = -1;
    }
    here = screenp->oblist;
    while(here)
    {
      if (here->ob == control) break;
      if (here->ob && here->ob->user == mynum)
        here->ob->user = -1;
      here = here->next;
    }
    if (!here->next)
      here = screenp->oblist;
    else
      here = here->next;
    counter = 0;
    while(1)
    {
      if (here->ob->query_order() == ORDER_LIVING &&
          here->ob->team_num == my_team &&
          here->ob->real_team_num == 255)
        break;
      here = here->next;
      if (!here)
      {
        here = screenp->oblist;
        counter++;
      }
      if (counter >= 3)
      {
        return 0;
      }
    }
    control = here->ob;
    screenp->control_hp = control->stats->hitpoints;
    //control->set_act_type(ACT_CONTROL);
  }  // end of switch guys

  // LSHIFT-TAB WILL SWITCH TO NEXT FAMILY GROUP ON MY TEAM
  if (!(inputkeyboard[mykeys[KEY_SWITCH]] && inputkeyboard[mykeys[KEY_SHIFTER]]))
    changedchar2[mynum] = 0;

  if (inputkeyboard[mykeys[KEY_SWITCH]] && inputkeyboard[mykeys[KEY_SHIFTER]] 
      && !changedchar2[mynum] && !inputkeyboard[mykeys[KEY_CHEAT]])
  {
    changedchar2[mynum] = 1;
    newfam = control->query_family();
    newfam++;
    newfam %= NUM_FAMILIES;

    if (control->user == mynum)
    {
      control->restore_act_type();
      control->user = -1;
    }
    here = screenp->oblist;
    counter = 0;
    while(1)
    {
      if (here->ob->query_order() == ORDER_LIVING &&
          //   here->ob->query_act_type() != ACT_CONTROL &&
          here->ob->team_num == my_team &&
          here->ob->query_family() == newfam)
        break;
      here = here->next;
      if (!here)
      {
        here = screenp->oblist;
        newfam++;
        newfam %= NUM_FAMILIES;
        counter++;
      }
      if (counter >= NUM_FAMILIES)
        return 0;
    }
    control = here->ob;
    screenp->control_hp = control->stats->hitpoints;
    //  control->set_act_type(ACT_CONTROL);
  } // end of switch type of guy


  // Redisplay the scenario text ..
  if (inputkeyboard[SDLK_SLASH] && !inputkeyboard[mykeys[KEY_CHEAT]]) // actually "?"
  {
         read_scenario(screenp);
         screenp->redrawme = 1;
         clear_keyboard();
  }

  // Help system
  if (inputkeyboard[SDLK_F1] && !inputkeyboard[mykeys[KEY_CHEAT]] )
  {
    strcpy(somemessage, "GLADIATOR V. ");
    strcat(somemessage, GLAD_VER); //append the version num
    #ifdef REGISTERED
      strcat(somemessage, " REGISTERED");
    #endif
    set_display_text(somemessage, STANDARD_TEXT_TIME);
    
    while (inputkeyboard[SDLK_F1])
      dumbcount++;
    read_help("glad.hlp",screenp);
    inputkeyboard = query_keyboard();
    clear_keyboard();
    screenp->redrawme = 1;
  }

  // Change our currently selected special
  if (!(inputkeyboard[mykeys[KEY_SPECIAL_SWITCH]]))
    changedspec[mynum] = 0;

  if (inputkeyboard[mykeys[KEY_SPECIAL_SWITCH]] && !changedspec[mynum])
  {
    changedspec[mynum] = 1;
    dumbcount = 0;
    control->current_special++;
    if (control->current_special > (NUM_SPECIALS-1)
        || !(strcmp(screenp->special_name[control->query_family()][control->current_special],"NONE"))
        || (((control->current_special-1)*3+1) > control->stats->level) )
      control->current_special = 1;
    while (inputkeyboard[mykeys[KEY_SPECIAL_SWITCH]] && (dumbcount < 64000) )
      dumbcount++;
  } //end of switch our special

  // Make sure we haven't yelled recently
  if (control->yo_delay > 0)
         control->yo_delay--;

  if (inputkeyboard[mykeys[KEY_YELL]] && !control->yo_delay 
      && !inputkeyboard[mykeys[KEY_SHIFTER]] 
      && !inputkeyboard[mykeys[KEY_CHEAT]] ) // yell for help
  {
    helpme = screenp->oblist;
    while (helpme)
    {
      if (helpme->ob && (helpme->ob->query_order() == ORDER_LIVING) &&
         (helpme->ob->query_act_type() != ACT_CONTROL) &&
         (helpme->ob->team_num == control->team_num) &&
         (!helpme->ob->leader) )
      {
        // Remove any current foe ..
        helpme->ob->leader = control;
        helpme->ob->foe = NULL;
        helpme->ob->stats->force_command(COMMAND_FOLLOW, 100, 0, 0);
        //helpme->ob->action = ACTION_FOLLOW;
      }
      helpme = helpme->next;
    }
    control->yo_delay = 50;
    control->screenp->soundp->play_sound(SOUND_YO);
    control->screenp->do_notify("Yo!", control);
  } //end of yo for friends

  //summon team defense
  if (inputkeyboard[mykeys[KEY_SHIFTER]] && inputkeyboard[mykeys[KEY_YELL]]
      && !inputkeyboard[mykeys[KEY_CHEAT]] ) // change guys' behavior
  {
    switch (control->action)
    {
      case 0:   // not set ..
        helpme = screenp->oblist;
        while (helpme)
        {
          if (helpme->ob && 
             (helpme->ob->team_num == control->team_num)
             )
            {
              // Remove any current foe ..
              helpme->ob->leader = control;
              helpme->ob->foe = NULL;
              helpme->ob->action = ACTION_FOLLOW;
            }
          helpme = helpme->next;
        }
        control->screenp->do_notify("SUMMONING DEFENSE!", control);
        break;
      case ACTION_FOLLOW:  // turn back to normal mode..
        helpme = screenp->oblist;
        while (helpme)
        {
          if (helpme->ob && (helpme->ob->query_order() == ORDER_LIVING) &&
             (helpme->ob->query_act_type() != ACT_CONTROL) &&
             (helpme->ob->team_num == control->team_num)
             )
            {
              // Set to normal operation
              helpme->ob->action = 0;
            }
          helpme = helpme->next;
        }
        control->action = 0; // for our reference
        control->screenp->do_notify("RELEASING MEN!", control);
        break;
      default:
        control->action = 0;
        break;
    } // end of switch for action mode
    while (inputkeyboard[mykeys[KEY_YELL]])
      dumbcount++;
  } // end of summon team defense



  // Before here, all keys should check for !KEY_CHEAT

  // Cheat keys .. using control
  if (inputkeyboard[mykeys[KEY_CHEAT]] && CHEAT_MODE)
  {
     // Change our team :)
     if (changedteam[mynum] && !inputkeyboard[mykeys[KEY_SWITCH]])
       changedteam[mynum] = 0;
     if (inputkeyboard[mykeys[KEY_SWITCH]] && !changedteam[mynum] )
     {
       changedteam[mynum] = 1;  // to debounce keys
       screenp->my_team++;
       screenp->my_team %= MAX_TEAM;
       tempobj = screenp->oblist;
       //              control = NULL;
       control->user = -1;
       control->set_act_type(ACT_RANDOM); // hope this works

       while(1)
       {
         if ( (tempobj->ob->team_num == screenp->my_team) &&
              (tempobj->ob->query_order() == ORDER_LIVING)
            )
           break;  // out of while(1) loop; we found someone
         tempobj = tempobj->next;
         if (!tempobj)
         {
           tempobj = screenp->oblist;
           screenp->my_team++;
           screenp->my_team %= MAX_TEAM;
         }
       }
       // By here we know that tempobj->ob is valid
       control = tempobj->ob;
       control->user = mynum;
       control->set_act_type(ACT_CONTROL);
    } // end of change team
    
    
    // Testing bonus rounds .. take this out, please
    if (inputkeyboard[SDLK_F11] && CHEAT_MODE) // give bonus rounds ..
      control->bonus_rounds = 5;

    // Testing effect object ..
    if (inputkeyboard[SDLK_F12] && CHEAT_MODE) // kill living bad guys
    {
      templink = screenp->oblist;
      while (templink)
      {
        if (templink->ob)
          if (templink->ob->query_order() == ORDER_LIVING &&
              !control->is_friendly(templink->ob) )
              //templink->ob->team_num != control->team_num)
          {
            templink->ob->stats->hitpoints = -1;
            control->attack(templink->ob);
            templink->ob->death();
            //templink->ob->dead = 1;
          }
        templink = templink->next;
      }
    } //end of testing effect object
    
    
    if (inputkeyboard[SDLK_RIGHTBRACKET]) // up level
    {
      control->stats->level++;
      while (inputkeyboard[SDLK_RIGHTBRACKET])
        dumbcount++;
    }//end up level

    if (inputkeyboard[SDLK_LEFTBRACKET]) // down level
    {
      if (control->stats->level > 1)
        control->stats->level--;
      while (inputkeyboard[SDLK_LEFTBRACKET])
        dumbcount++;
    }//end down level
         
    if (inputkeyboard[SDLK_F1]) // freeze time
    {
      screenp->enemy_freeze += 50;
      set_palette(screenp->bluepalette);
      while (inputkeyboard[SDLK_F1])
        dumbcount++;
    }//end freeze time

    if (inputkeyboard[SDLK_F2]) // generate magic shield
    {
      newob = screenp->add_ob(ORDER_FX, FAMILY_MAGIC_SHIELD);
      newob->owner = control;
      newob->team_num = control->team_num;
      newob->ani_type = 1; // dummy, non-zero value
      newob->lifetime = 200;
      while (inputkeyboard[SDLK_F2])
        dumbcount++;
    }//end generate magic shield
           
    if (inputkeyboard[SDLK_f])  // ability to fly
    { 
      if (control->stats->query_bit_flags(BIT_FLYING))
        control->stats->set_bit_flags(BIT_FLYING,0);
      else
        control->stats->set_bit_flags(BIT_FLYING,1);
      while (inputkeyboard[SDLK_f]) 
        dumbcount++;
    } //end flying

    if (inputkeyboard[SDLK_h]) // give controller lots of hitpoints
    {
      control->stats->hitpoints += 100;
      screenp->control_hp += 100;
    } //end hitpoints
         
    if (inputkeyboard[SDLK_i])  // give invincibility
    {
      if (control->stats->query_bit_flags(BIT_INVINCIBLE))
        control->stats->set_bit_flags(BIT_INVINCIBLE,0);
      else
        control->stats->set_bit_flags(BIT_INVINCIBLE,1);
      while (inputkeyboard[SDLK_i]) 
        dumbcount++;
    } // end invincibility

    if (inputkeyboard[SDLK_m]) // give controller lots of magicpoints
    {
      control->stats->magicpoints += 150;
    } // end magic points

    if (inputkeyboard[SDLK_s]) // give us faster speed ..
    {
      control->speed_bonus_left += 20;
      control->speed_bonus = control->normal_stepsize;
    }

    if (inputkeyboard[SDLK_t]) // transform to new shape
    {
      dumbcount = (control->query_family()+1)% NUM_FAMILIES;
      control->transform_to(control->query_order(), (char) dumbcount);
      while (inputkeyboard[SDLK_t]) 
        dumbcount++;
    } //end transform

    if (inputkeyboard[SDLK_v]) // invisibility
    {
      if (control->invisibility_left < 3000)
        control->invisibility_left += 100;
    }
         
  } //end of cheat keys

  
  // Make sure we're not in use by another player
  if (control->user != mynum)
         return 1;


  if (control->ani_type != ANI_WALK)
  {
         control->animate();
         inputthing = 0;
  }

  // if we changed control characters
  if (control != oldcontrol)
         control->stats->clear_command();

  // If we're frozen ..
  if (control->stats->frozen_delay)
  {
         control->stats->frozen_delay--;
         return 1;
  }

  // Movement, etc.
  // Make sure we're not performing some queued action ..
  if (!control->stats->commandlist)
  {

         if (inputkeyboard[mykeys[KEY_SHIFTER]])
           control->shifter_down = 1;
         else
           control->shifter_down = 0;

// Testing ..
if (inputkeyboard[SDLK_r])
{
  control->stats->right_walk();
}

         if (inputkeyboard[mykeys[KEY_SPECIAL]])
         {
           control->special();
         }
         if (inputkeyboard[mykeys[KEY_UP]] && inputkeyboard[mykeys[KEY_RIGHT]])
                control->walkstep(1,-1);
         else if (inputkeyboard[mykeys[KEY_UP]] && inputkeyboard[mykeys[KEY_LEFT]])
                control->walkstep(-1,-1);
         else if (inputkeyboard[mykeys[KEY_DOWN]] && inputkeyboard[mykeys[KEY_RIGHT]])
                control->walkstep(1,1);
         else if (inputkeyboard[mykeys[KEY_DOWN]] && inputkeyboard[mykeys[KEY_LEFT]])
                control->walkstep(-1,1);
         else if (inputkeyboard[mykeys[KEY_UP_LEFT]]) control->walkstep(-1, -1);
         else if (inputkeyboard[mykeys[KEY_UP]]) control->walkstep(0, -1);
         else if (inputkeyboard[mykeys[KEY_UP_RIGHT]]) control->walkstep(1, -1);
         else if (inputkeyboard[mykeys[KEY_DOWN_LEFT]]) control->walkstep(-1, 1);
         else if (inputkeyboard[mykeys[KEY_DOWN]]) control->walkstep(0,1);
         else if (inputkeyboard[mykeys[KEY_DOWN_RIGHT]]) control->walkstep(1, 1);
         else if (inputkeyboard[mykeys[KEY_LEFT]]) control->walkstep(-1,0);
         else if (inputkeyboard[mykeys[KEY_RIGHT]]) control->walkstep(1,0);
         else if (control->stats->query_bit_flags(BIT_ANIMATE) )  // animate regardless..
         {
                control->cycle++;
                if (control->ani[control->curdir][control->cycle] == -1)
                  control->cycle = 0;
                control->set_frame(control->ani[control->curdir][control->cycle]);
         }

         // Standard fire
         if (inputkeyboard[mykeys[KEY_FIRE]])
         {
                control->init_fire();
         }
  } // end of check for queued actions...

  // Were we hurt?
/*
  if (control && (screenp->control_hp > control->stats->hitpoints) ) // we were hurt
  {
         screenp->control_hp = control->stats->hitpoints;
//       draw_box(S_LEFT, S_UP, S_RIGHT-1, S_DOWN-1, 44, 1);  // red flash
         // Make temporary stain:
         blood = screenp->add_ob(ORDER_WEAPON, FAMILY_BLOOD);
         blood->team_num = control->team_num;
         blood->ani_type = ANI_GROW;
         blood->setxy(control->xpos,control->ypos);
         blood->owner = control;
         //blood->draw(this);
//       redraw();
         //refresh();
         //screenp->remove_ob(blood);

  }
*/
  return 1;
}

void viewscreen::set_display_text(char *newtext, short numcycles)
{
  long i;

  if (!newtext) return;

  i = 0;
  while (strlen(textlist[i]) && i < MAX_MESSAGES)
    i++;
  if (i >= MAX_MESSAGES) // no room, need to scroll messages
  {
    shift_text(0); // shift up, starting at 0
    i = MAX_MESSAGES - 1;
  }
  //strcpy(infotext, newtext);
  strcpy(textlist[i], newtext);

  if (numcycles > 0)
    textcycles[i] = numcycles;
  else
    textcycles[i] = 0;
}

// Blanks the screen text
void viewscreen::clear_text()
{
    long i;
    for (i=0; i < MAX_MESSAGES; i++)
      textlist[i][0] = 0;
}

short viewscreen::draw_obs()
{
  oblink  *here;

  // First draw the special effects
  here = screenp->fxlist;
  while(here)
  {
    if (here->ob && !here->ob->dead)
    {
      here->ob->draw(this);
    }
    here = here->next;
  }

  // Now do real objects
  here = screenp->oblist;
  while(here)
  {
         if (here->ob && !here->ob->dead)
         {
                  here->ob->draw(this);
         }
         here = here->next;
  }

  // Finally draw the weapons
  here = screenp->weaplist;
  while(here)
  {
    if (here->ob && !here->ob->dead)
      here->ob->draw(this);
    here = here->next;
  }

  return 1;
}


void viewscreen::align_joy()
{
  long * joystick;
  static char somemessage[80];
  long deltax, deltay;
  long count, countx, county;

  if (prefs[PREF_JOY]!=PREF_USE_JOY) return; //this shouldn't happen, should be checked sooner

  joystick = query_joy();

  // First we need to get the center values ..
//  sprintf(somemessage,"CENTER JOYSTICK, THEN PRESS BUTTON 1");
//  screentext->write_xy( (xview-strlen(somemessage)*6)/2,
//                         50, somemessage, YELLOW, this );
  clear_text();
  redraw();
  set_display_text("CENTER JOYSTICK", 1);
  set_display_text("PRESS BUTTON 1", 1);
  display_text();
  display_text();
  screenp->buffer_to_screen(0,0,320,200);
  count = 0;
  while (!joystick[JOY_B1] || count < COUNT_TIME )
  {
    joycenterx +=joystick[JOY_X];
    joycentery +=joystick[JOY_Y];
    count++;
    //printf("CX: %ld,  CY:  %ld\n", joycenterx/count, joycentery/count);
    joystick = query_joy();
  }
  joycenterx /= (long) count;
  joycentery /= (long) count;

//  sprintf(somemessage, "MOVE JOYSTICK TO UPPER LEFT, THEN PRESS BUTTON 2");
//  screentext->write_xy( (xview-strlen(somemessage)*6)/2,
//                         60, somemessage, YELLOW, this );
  clear_text();
  redraw();
  set_display_text("MOVE JOYSTICK TO", 1);
  set_display_text("UPPER LEFT", 1);
  set_display_text("PRESS BUTTON 2", 1);
  display_text();
  display_text();
  display_text();
  screenp->buffer_to_screen(0,0,320,200);
  count = countx = county = 0;
  while (!joystick[JOY_B2] || (countx+county) < COUNT_TIME)
  {
    if (joystick[JOY_X] < joycenterx && joystick[JOY_X] > 5)
    {
      joyleft += joystick[JOY_X];
      countx++;
    }
    if (joystick[JOY_Y] < joycentery && joystick[JOY_Y] > 5)   
    {
      joyup += joystick[JOY_Y];
      county++;
    }
    count++;
    joystick = query_joy();
  }
  joyleft /= countx;
  joyup   /= county;

//  sprintf(somemessage, "MOVE JOYSTICK TO LOWER RIGHT, THEN PRESS BUTTON 1");
//  screentext->write_xy( (xview-strlen(somemessage)*6)/2,
//                         90, somemessage, YELLOW, this );
  clear_text();
  redraw();
  set_display_text("MOVE JOYSTICK TO", 1);
  set_display_text("LOWER RIGHT", 1);
  set_display_text("PRESS BUTTON 1", 1);
  display_text();
  display_text();
  display_text();        

  screenp->buffer_to_screen(0,0,320,200);
  count = countx = county = 0;
  while (!joystick[JOY_B1] || (countx+county) < COUNT_TIME)
  {
    if (joystick[JOY_X] > joycenterx)
    {
      joyright += joystick[JOY_X];
      countx++;
    }
    if (joystick[JOY_Y] > joycentery)
    {
      joydown += joystick[JOY_Y];
      county++;
    }
    count++;
    //printf("X: %ld, Y: %ld\n", joyright/count, joydown/count);
    joystick = query_joy();
  }
  joyright /= countx;
  joydown /= county;


  // Calculate left & top offsets ..
  deltax = joycenterx - joyleft;
  deltay = joycentery - joyup;
  minoffleft = joycenterx - (deltax * TOLERANCE)/100;
  minoffup   = joycentery - (deltay * TOLERANCE)/100;

  // Right and down offsets..
  deltax = joyright - joycenterx;
  deltay = joydown - joycentery;
  minoffright = joycenterx + (deltax * TOLERANCE)/100;
  minoffdown = joycentery + (deltay * TOLERANCE)/100;

  // Debugging only:
  //printf("L: %ld R: %ld U: %ld D:%ld",joyleft,joyright,joyup,joydown);
  //printf("ML: %ld MR: %ld MU: %ld MD: %ld",minoffleft,minoffright,minoffup,minoffdown);
  //printf("CX: %ld CY: %ld",joycenterx,joycentery);
  
  keys[mynum][KEY_UP] = JOY_KEYBOARD_UP;
  keys[mynum][KEY_DOWN] = JOY_KEYBOARD_DOWN;
  keys[mynum][KEY_LEFT] = JOY_KEYBOARD_LEFT;
  keys[mynum][KEY_RIGHT] = JOY_KEYBOARD_RIGHT;
  keys[mynum][KEY_FIRE] = JOY_KEYBOARD_B1;
  keys[mynum][KEY_SPECIAL] = JOY_KEYBOARD_B2;
//  keys[mynum][KEY_SPECIAL_SWITCH] = JOY_KEYBOARD_B3;
//  keys[mynum][KEY_SWITCH] = JOY_KEYBOARD_B4;
  joyaligned = 1;
  prefsob->save(this); // We need this so the changes are saved  
}


void viewscreen::input_joy()
{
  long resx,resy,diffx,diffy;
  long currentx, currenty;
  long * localjoystick;
  char * localkeyboard;
  localjoystick = query_joy();
  localkeyboard = query_keyboard();

  //first the buttons
  if (localjoystick[JOY_B1])
    localkeyboard[JOY_KEYBOARD_B1] = (char) 1;
  else
    localkeyboard[JOY_KEYBOARD_B1] = (char) 0;
  
  if (localjoystick[JOY_B2])
    localkeyboard[JOY_KEYBOARD_B2] = (char) 1;
  else
    localkeyboard[JOY_KEYBOARD_B2] = (char) 0;
  
  if (localjoystick[JOY_B3])
    localkeyboard[JOY_KEYBOARD_B3] = (char) 1;
  else
    localkeyboard[JOY_KEYBOARD_B3] = (char) 0;
  
  if (localjoystick[JOY_B4])
    localkeyboard[JOY_KEYBOARD_B4] = (char) 1;
  else
    localkeyboard[JOY_KEYBOARD_B4] = (char) 0;


  currentx = localjoystick[JOY_X]; 
  currenty = localjoystick[JOY_Y];
  //diffx = joycenterx - resx;
  //diffy = joycentery - resy;
  /*
  if (abs(diffx) < abs(joycenterx/4) &&      //make sure x is close to center
      abs(diffy) > abs(joycentery/12) &&     //make sure y is not close to center
      resy > minoffup)                       //make sure we are not trying to go up
  {
    resy += minoffdown; //values straight down are weird
    resy *= 2;
    diffy = joycentery - resy;
  }
  */

  if ( currentx < minoffleft )
    localkeyboard[JOY_KEYBOARD_LEFT] = (char) 1;
  else 
    localkeyboard[JOY_KEYBOARD_LEFT] = (char) 0;
  if ( currentx > minoffright )
    localkeyboard[JOY_KEYBOARD_RIGHT] = (char) 1;
  else
    localkeyboard[JOY_KEYBOARD_RIGHT] = (char) 0;
  
  if ( currenty < minoffup )
    localkeyboard[JOY_KEYBOARD_UP] = (char) 1;
  else
    localkeyboard[JOY_KEYBOARD_UP] = (char) 0;
  if ( currenty > minoffdown )
    localkeyboard[JOY_KEYBOARD_DOWN] = (char) 1;
  else
    localkeyboard[JOY_KEYBOARD_DOWN] = (char) 0;
}

void viewscreen::remove_joy()
{
  memcpy(mykeys, normalkeys[mynum], 16);
  joyaligned = 0;
}

void viewscreen::resize(short x, short y, short length, short height)
{

  xloc = x;
  yloc = y;
    
  xview = length;
  yview = height;

  endx = xloc+length;
  endy = yloc+height;

  if (myradar->bmp)
    myradar->start();
  screenp->redrawme = 1;
}

void viewscreen::resize(char whatmode)
{
  switch (screenp->numviews)
  {
    case 1: //  one-player mode
      switch (whatmode)
      {
        case PREF_VIEW_PANELS:
          resize(44, 12, 232, 176); // room for score panel ..
          break;
        case PREF_VIEW_1:
          resize(64, 28, 192, 144);
          break;
        case PREF_VIEW_2:
          resize(86, 44, 148, 112);
          break;
        case PREF_VIEW_3:
          resize(106, 60, 108, 80);
          break;
        case PREF_VIEW_FULL:
        default:
          resize(0, 0, 320, 200);
          break;
      }
      break;
    case 2: // two-player mode
      switch (mynum)  // left or right view?
      {
        case 0: 
          switch (whatmode) // left or right view?
          {
            case PREF_VIEW_PANELS:
              resize(164, 16, 152, 168); // room for score panel ..
              break;
            case PREF_VIEW_1:
              resize(164, 32, 152, 136);
              break;
            case PREF_VIEW_2:
              resize(164, 48, 152, 104);
              break;
            case PREF_VIEW_3:
              resize(164, 64, 152, 72);
              break;
            case PREF_VIEW_FULL:
            default:
              resize(164, 0, 156, 200);
              break;
          }
          break;
        case 1:
          switch (whatmode) // left or right view?
          {
            case PREF_VIEW_PANELS:
              resize(4, 16, 152, 168); // room for score panel ..
              break;
            case PREF_VIEW_1:
              resize(4, 32, 152, 136);
              break;
            case PREF_VIEW_2:
              resize(4, 48, 152, 104);
              break;
            case PREF_VIEW_3:
              resize(4, 64, 152, 72);
              break;
            case PREF_VIEW_FULL:
            default:
              resize(4, 0, 156, 200);
              break;
          }
          break;
      } // end of mynum switch
    break;
    case 3: // 3-player mode
      switch (mynum)  // left or right view?
      {
        case 0: 
          switch (whatmode) // left or right view?
          {
            case PREF_VIEW_PANELS:
              resize(216, 16, 100, 168); // room for score panel ..
              break;
            case PREF_VIEW_1:
              resize(216, 32, 100, 136);
              break;
            case PREF_VIEW_2:
              resize(216, 48, 100, 104);
              break;
            case PREF_VIEW_3:
              resize(216, 64, 100, 72);
              break;
            case PREF_VIEW_FULL:
            default:
              resize(216, 0, 104, 200);
              break;
          }
          break;
        case 1:
          switch (whatmode) // left or right view?
          {
            case PREF_VIEW_PANELS:
              resize(4, 16, 100, 168); // room for score panel ..
              break;
            case PREF_VIEW_1:
              resize(4, 32, 100, 136);
              break;
            case PREF_VIEW_2:
              resize(4, 48, 100, 104);
              break;
            case PREF_VIEW_3:
              resize(4, 64, 100, 72);
              break;
            case PREF_VIEW_FULL:
            default:
              resize(0, 0, 104, 200);
              break;
          }
          break;
        case 2:  // 3rd player
          switch (whatmode) // left or right view?
          {
            case PREF_VIEW_PANELS:
              resize(112, 16, 100, 168); // room for score panel ..
              break;
            case PREF_VIEW_1:
              resize(112, 32, 100, 136);
              break;
            case PREF_VIEW_2:
              resize(112, 48, 100, 104);
              break;
            case PREF_VIEW_3:
              resize(112, 64, 100, 72);
              break;
            case PREF_VIEW_FULL:
            default:
              resize(108, 0, 104, 200);
              break;
          }
          break;
      } // end of mynum switch
      break;
    case 4: // 4-player mode
    default:
      switch (mynum)  // left or right view?
      {
        case 0: 
          resize(164, 0, 152, 96);
          break;
        case 1:
          resize(0, 0, 152, 96);
          break;
        case 2:
          resize(164, 104, 152, 96);
          break;
        case 3:
        default:
          resize(0, 104, 152, 96);
          break;
      } // end of mynum switch
    break;
  } // end of numviews switch
  
} // end of resize(whatmode)

void viewscreen::view_team()
{
  view_team(VIEW_TEAM_LEFT, VIEW_TEAM_TOP, 
            VIEW_TEAM_RIGHT, VIEW_TEAM_BOTTOM);
}

void viewscreen::view_team(short left, short top, short right, short bottom)
{
  char teamnum = my_team;
  char text_down = top+3;
  oblink *here = screenp->oblist;
  oblink *dude, *list, *temp;
  char message[30], done = 0, hpcolor, mpcolor, namecolor, numguys = 0;
  short hp, mp, maxhp, maxmp;
  text mytext(screenp);
  list = new oblink;
  list->ob = NULL;
  list->next = NULL;
  temp = new oblink;
  char *teamkeys;
  long dumbcount, currentcycle = 0, cycletime = 30000;

  screenp->redrawme = 1;
  screenp->draw_button(left, top, right, bottom, 2); 
  
  strcpy(message, "  Name  "); 
  mytext.write_xy(left+5, text_down, message, (unsigned char) BLACK); 
     
  strcpy (message, "Health");
  mytext.write_xy(left+80, text_down, message, (unsigned char) BLACK); 
  
  sprintf (message, "Power");
  mytext.write_xy(left+140, text_down, message, (unsigned char) BLACK); 

  sprintf (message, "Level");
  mytext.write_xy(left+190, text_down, message, (unsigned char) BLACK);
      
  text_down+=6;
  
  while(here)
  {
    if (here->ob && !here->ob->dead 
        && here->ob->query_order() == ORDER_LIVING 
        && here->ob->team_num == teamnum
        && (here->ob->stats->name || here->ob->myguy)) //&& here->ob->owner == NULL)
    {
      dude = new oblink;
      dude->next = NULL;
      dude->ob = here->ob;
      dude->next = list;
      list = dude;
      
      while (dude->next && dude->next->ob && 
             dude->ob->stats->hitpoints <= dude->next->ob->stats->hitpoints)
             
             //(dude->ob->stats->hitpoints*10)/dude->ob->stats->max_hitpoints <=
             //(dude->next->ob->stats->hitpoints*10)/dude->next->ob->stats->max_hitpoints)
      {  
        temp->ob = dude->ob;
        dude->ob = dude->next->ob;
        dude->next->ob = temp->ob;
        dude = dude->next;
      }
    }  
    here = here->next;
  }

  dude = list;
  while (dude) 
  {
    if (dude->ob)
    {
      if (numguys++ > 30) break;
      hp = dude->ob->stats->hitpoints;
      mp = dude->ob->stats->magicpoints;
      maxhp = dude->ob->stats->max_hitpoints;
      maxmp = dude->ob->stats->max_magicpoints;

      if ( (hp * 3) < maxhp)
        hpcolor = LOW_HP_COLOR;
      else if ( (hp * 3 / 2) < maxhp)
        hpcolor = MID_HP_COLOR -3;
      else if (hp < maxhp)
        hpcolor = MAX_HP_COLOR+4;
      else if (hp == maxhp)
        hpcolor = HIGH_HP_COLOR+2;
      else
        hpcolor = ORANGE_START;
      
      if ( (mp * 3) < maxmp)
        mpcolor = LOW_MP_COLOR;
      else if ( (mp * 3 / 2) < maxmp)
        mpcolor = MID_MP_COLOR;
      else if (mp < maxmp)
        mpcolor = MAX_MP_COLOR;
      else if (mp == maxmp)
        mpcolor = HIGH_MP_COLOR+3;
      else
        mpcolor = WATER_START;

      if (dude->ob == control) namecolor = RED;
        else namecolor = BLACK;
      
      if (dude->ob->myguy)
        strcpy (message, dude->ob->myguy->name);
      else strcpy(message, dude->ob->stats->name); 
      mytext.write_xy(left+5, text_down, message, (unsigned char) namecolor); 
      
      sprintf (message, "%4d/%d", hp, maxhp);
      mytext.write_xy(left+70, text_down, message, (unsigned char) hpcolor); 
      
      sprintf (message, "%4d/%d", mp, maxmp);
      mytext.write_xy(left+130, text_down, message, (unsigned char) mpcolor); 

      sprintf (message, "%2d", dude->ob->stats->level);
      mytext.write_xy(left+195, text_down, message, (unsigned char) BLACK);
      
      text_down+=6;
    }
    if (!dude->next) break;
    dude = dude->next;
  }

  while (list)
  {
    temp = list;
    list = list->next;
    delete temp;
    temp = NULL;
  }
  delete temp;
  temp = NULL;
  delete dude;
  dude = NULL;

  teamkeys = query_keyboard();
  while (!teamkeys[SDLK_ESCAPE])
    screenp->do_cycle(currentcycle++, cycletime);
  while (teamkeys[SDLK_ESCAPE])
    dumbcount++;

  return;
}

void viewscreen::options_menu()
{
  static text optiontext(screenp);
  static char *opkeys;
  long dumbcount;
  long gamespeed, viewsize;
  static char message[80], tempstr[80];
  signed char gamma = prefs[PREF_GAMMA];

#define LEFT_OPS 49
#define TOP_OPS 44
#define TEXT_HEIGHT 5
#define OPLINES(y) (TOP_OPS + y*(TEXT_HEIGHT+3))
#define PANEL_COLOR 13
  if (!control)
   return;  // safety check; shouldn't happen

  opkeys = query_keyboard();
  clear_keyboard();

  // Draw the menu button
  screenp->draw_button(40, 40, 280, 160, 2, 1);
  screenp->draw_text_bar(40+4, 40+4, 280-4, 40+12);
  optiontext.write_xy(160-6*6, OPLINES(0)+2, "Options Menu", (unsigned char) RED, 1);

  
  gamespeed = change_speed(0);
  sprintf(message, "Change Game Speed (+/-): %2ld  ", gamespeed);
  optiontext.write_xy(LEFT_OPS, OPLINES(2), message, (unsigned char) BLACK, 1);
  switch (prefs[PREF_VIEW])
  {
    case PREF_VIEW_FULL:  strcpy(tempstr, "Full Screen"); break;
    case PREF_VIEW_PANELS: strcpy(tempstr, "Large"); break;
    case PREF_VIEW_1: strcpy(tempstr, "Medium"); break;
    case PREF_VIEW_2: strcpy(tempstr, "Small"); break;
    case PREF_VIEW_3: strcpy(tempstr, "Tiny"); break;
    default: strcpy(tempstr, "Weird"); break;
  }
  sprintf(message, "Change View Size ([,]) : %s ", tempstr);
  screenp->draw_box(LEFT_OPS, OPLINES(3), LEFT_OPS+strlen(message)*6, OPLINES(3)+6, PANEL_COLOR, 1, 1);
  optiontext.write_xy(LEFT_OPS, OPLINES(3), message, (unsigned char) BLACK, 1);
  
  gamma = change_gamma(0);
  sprintf(message, "Change Brightness (<,>): %ld ", gamma);
  screenp->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
  optiontext.write_xy(LEFT_OPS, OPLINES(4), message, (unsigned char) BLACK, 1);
  
  if (prefs[PREF_RADAR])
    sprintf(message, "Radar Display (R)      : ON ");
  else
    sprintf(message, "Radar Display (R)      : OFF ");
  screenp->draw_box(45, OPLINES(5), 275, OPLINES(5)+6, PANEL_COLOR, 1, 1);
  optiontext.write_xy(LEFT_OPS, OPLINES(5), message, (unsigned char) BLACK, 1);
  
  switch (prefs[PREF_LIFE])
  {
    case PREF_LIFE_TEXT:  strcpy(tempstr, "Text Only"); break;
    case PREF_LIFE_BARS:  strcpy(tempstr, "Bars Only"); break;
    case PREF_LIFE_BOTH:  strcpy(tempstr, "Bars and Text"); break;
    case PREF_LIFE_OFF:   strcpy(tempstr, "Off"); break;
    default:
    case PREF_LIFE_SMALL: strcpy(tempstr, "On"); break;
  }
  sprintf(message, "Hitpoint Display (H)   : %s", tempstr);
  screenp->draw_box(45, OPLINES(6), 275, OPLINES(6)+6, PANEL_COLOR, 1, 1);
  optiontext.write_xy(LEFT_OPS, OPLINES(6), message, (unsigned char) BLACK, 1);

  if (prefs[PREF_FOES])
    sprintf(message, "Foes Display (F)       : ON ");
  else
    sprintf(message, "Foes Display (F)       : OFF ");
  screenp->draw_box(45, OPLINES(7), 275, OPLINES(7)+6, PANEL_COLOR, 1, 1);
  optiontext.write_xy(LEFT_OPS, OPLINES(7), message, (unsigned char) BLACK, 1);
  
  if (prefs[PREF_SCORE])
    sprintf(message, "Score Display (S)      : ON ");
  else
    sprintf(message, "Score Display (S)      : OFF ");
  screenp->draw_box(45, OPLINES(8), 275, OPLINES(8)+6, PANEL_COLOR, 1, 1);
  optiontext.write_xy(LEFT_OPS, OPLINES(8), message, (unsigned char) BLACK, 1);
  
  optiontext.write_xy(LEFT_OPS, OPLINES(9), "VIEW TEAM INFO (T)", (unsigned char) BLACK, 1);
  
  if (screenp->cyclemode)
    sprintf(message,"Color Cycling (C)      : ON ");
  else
    sprintf(message,"Color Cycling (C)      : OFF ");
  screenp->draw_box(45,OPLINES(10),275,OPLINES(10)+6,PANEL_COLOR,1,1);
  optiontext.write_xy(LEFT_OPS,OPLINES(10),message,(unsigned char) BLACK,1);

  if (prefs[PREF_JOY] == PREF_NO_JOY)
    sprintf(message, "Joystick Mode (J)      : OFF ");
  else
    sprintf(message, "Joystick Mode (J)      : ON ");
  screenp->draw_box(45,OPLINES(11),275,OPLINES(11)+6,PANEL_COLOR,1,1);
  optiontext.write_xy(LEFT_OPS,OPLINES(11),message,(unsigned char) BLACK,1);
  
  optiontext.write_xy(LEFT_OPS, OPLINES(12), "EDIT KEY PREFS (K)", (unsigned char) BLACK, 1);

  if (prefs[PREF_OVERLAY])
    sprintf(message, "Text-button Display (B): ON ");
  else
    sprintf(message, "Text-button Display (B): OFF");
  optiontext.write_xy(LEFT_OPS, OPLINES(13), message, BLACK, 1);

  // Draw the current screen
  screenp->buffer_to_screen(0, 0, 320, 200);

  // Wait for esc for now
  while (!opkeys[SDLK_ESCAPE])
  {
    if (opkeys[SDLK_KP_PLUS]) // faster game speed
    {
      gamespeed = change_speed(1);
      sprintf(message, "Change Game Speed (+/-): %2ld  ", gamespeed);
      screenp->draw_box(LEFT_OPS, OPLINES(2), LEFT_OPS+strlen(message)*6, OPLINES(2)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(2), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_KP_PLUS])
        dumbcount++;
    }
    if (opkeys[SDLK_KP_MINUS]) // slower game speed
    {
      gamespeed = change_speed(-1);
      sprintf(message, "Change Game Speed (+/-): %2ld  ", gamespeed);
      screenp->draw_box(LEFT_OPS, OPLINES(2), LEFT_OPS+strlen(message)*6, OPLINES(2)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(2), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_KP_MINUS])
        dumbcount++;
    }
    if (opkeys[SDLK_LEFTBRACKET]) // smaller view size
    {
      prefs[PREF_VIEW] = prefs[PREF_VIEW]+1;
      if (prefs[PREF_VIEW] > 4)
        prefs[PREF_VIEW] = 4;
      resize(prefs[PREF_VIEW]);

      switch (prefs[PREF_VIEW])
      {
        case PREF_VIEW_FULL:  strcpy(tempstr, "Full Screen"); break;
        case PREF_VIEW_PANELS: strcpy(tempstr, "Large"); break;
        case PREF_VIEW_1: strcpy(tempstr, "Medium"); break;
        case PREF_VIEW_2: strcpy(tempstr, "Small"); break;
        case PREF_VIEW_3: strcpy(tempstr, "Tiny"); break;
        default: strcpy(tempstr, "Weird"); break;
      }
      sprintf(message, "Change View Size ([,]) : %s       ", tempstr);
      screenp->draw_box(45, OPLINES(3), 275, OPLINES(3)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(3), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_LEFTBRACKET])
        dumbcount++;
    }
    if (opkeys[SDLK_RIGHTBRACKET]) // larger view size
    {
      prefs[PREF_VIEW] = prefs[PREF_VIEW]-1;
      if (prefs[PREF_VIEW] < 0)
        prefs[PREF_VIEW] = 0;
      resize(prefs[PREF_VIEW]);

      switch (prefs[PREF_VIEW])
      {
        case PREF_VIEW_FULL:  strcpy(tempstr, "Full Screen"); break;
        case PREF_VIEW_PANELS: strcpy(tempstr, "Large"); break;
        case PREF_VIEW_1: strcpy(tempstr, "Medium"); break;
        case PREF_VIEW_2: strcpy(tempstr, "Small"); break;
        case PREF_VIEW_3: strcpy(tempstr, "Tiny"); break;
        default: strcpy(tempstr, "Weird"); break;
      }
      sprintf(message, "Change View Size ([,]) : %s  ", tempstr);
      screenp->draw_box(45, OPLINES(3), 275, OPLINES(3)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(3), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_RIGHTBRACKET])
        dumbcount++;
    }
    if (opkeys[SDLK_COMMA]) // darken screen
    {
      prefs[PREF_GAMMA] = gamma = change_gamma(-2);
      sprintf(message, "Change Brightness (<,>): %ld ", gamma);
      screenp->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(4), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_COMMA])
        dumbcount++;
    }
    if (opkeys[SDLK_PERIOD]) // lighten screen
    {
      prefs[PREF_GAMMA] = gamma = change_gamma(+2);
      sprintf(message, "Change Brightness (<,>): %ld ", gamma);
      screenp->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(4), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_PERIOD])
        dumbcount++;
    }
    if (opkeys[SDLK_r]) // toggle radar display      
    {
      prefs[PREF_RADAR] = (prefs[PREF_RADAR]+1)%2;
      if (prefs[PREF_RADAR])
        sprintf(message, "Radar Display (R)      : ON ");
      else
        sprintf(message, "Radar Display (R)      : OFF ");
      screenp->draw_box(45, OPLINES(5), 275, OPLINES(5)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(5), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_r])
        dumbcount++;
    }
    if (opkeys[SDLK_h]) // toggle HP display      
    {
      prefs[PREF_LIFE] = (prefs[PREF_LIFE]+1) %5;
      switch (prefs[PREF_LIFE])
      {
        case PREF_LIFE_TEXT:  strcpy(tempstr, "Text Only"); break;
        case PREF_LIFE_BARS:  strcpy(tempstr, "Bars Only"); break;
        case PREF_LIFE_BOTH:  strcpy(tempstr, "Bars and Text"); break;
        case PREF_LIFE_OFF:   strcpy(tempstr, "Off"); break;
        default:
        case PREF_LIFE_SMALL: strcpy(tempstr, "On"); break;
      }
      sprintf(message, "Hitpoint Display (H)   : %s", tempstr);
      screenp->draw_box(45, OPLINES(6), 275, OPLINES(6)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(6), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_h])
        dumbcount++;
    }
    if (opkeys[SDLK_f]) // toggle foes display      
    {
      prefs[PREF_FOES] = (prefs[PREF_FOES]+1)%2;
      if (prefs[PREF_FOES])
        sprintf(message, "Foes Display (F)       : ON ");
      else
        sprintf(message, "Foes Display (F)       : OFF ");
      screenp->draw_box(45, OPLINES(7), 275, OPLINES(7)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(7), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_f])
        dumbcount++;
    }
    if (opkeys[SDLK_s]) // toggle score display      
    {
      prefs[PREF_SCORE] = (prefs[PREF_SCORE]+1)%2;
      if (prefs[PREF_SCORE])
        sprintf(message, "Score Display (S)      : ON ");
      else
        sprintf(message, "Score Display (S)      : OFF ");
      screenp->draw_box(45, OPLINES(8), 275, OPLINES(8)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(8), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_s])
        dumbcount++;
    }

    if (opkeys[SDLK_t])      // View the teamlist
    {  
      view_team();
      screenp->redraw();
      options_menu();
      return;
    }     
    
    if (opkeys[SDLK_c])
    {
      screenp->cyclemode= (short) ((screenp->cyclemode+1) %2);
      while (opkeys[SDLK_c])
        dumbcount++;
      if (screenp->cyclemode)
        sprintf(message,"Color Cycling (C)      : ON ");
      else
        sprintf(message,"Color Cycling (C)      : OFF ");
      screenp->draw_box(45,OPLINES(10),275,OPLINES(10)+6,PANEL_COLOR,1,1);
      optiontext.write_xy(LEFT_OPS,OPLINES(10),message,(unsigned char) BLACK,1);
      screenp->buffer_to_screen(0, 0, 320, 200);

    }

    if (opkeys[SDLK_j]) // toggle joystick display      
    {
      prefs[PREF_JOY] = (prefs[PREF_JOY]+1)%2;
      if (prefs[PREF_JOY] == PREF_USE_JOY)
      {
        sprintf(message, "Joystick Mode (J)      : ON ");
      }
      else
      {
        sprintf(message, "Joystick Mode (J)      : OFF ");
        if (prefs[PREF_JOY] != PREF_USE_JOY)
        {
            memcpy(mykeys, normalkeys[mynum], 16);
        }
      }
      screenp->draw_box(45, OPLINES(11), 275, OPLINES(11)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(11), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_j])
        dumbcount++;
    }
    
    if (opkeys[SDLK_k])      // Edit the keyboard mappings
    {  
      if (set_key_prefs())
      {
        set_display_text("NEW KEYBOARD STATE SAVED", 30);
        set_display_text("DELETE KEYPREFS.DAT FOR DEFAULTS", 30);
      }
      screenp->redraw();
      options_menu();
      return;
    }     
    if (opkeys[SDLK_b]) // toggle button display
    {
      prefs[PREF_OVERLAY] = (prefs[PREF_OVERLAY]+1)%2;
      if (prefs[PREF_OVERLAY])
        sprintf(message, "Text-button Display (B): ON ");
      else
        sprintf(message, "Text-button Display (B): OFF ");
      screenp->draw_box(45, OPLINES(13), 275, OPLINES(13)+6, PANEL_COLOR, 1, 1);
      optiontext.write_xy(LEFT_OPS, OPLINES(13), message, (unsigned char) BLACK, 1);
      screenp->buffer_to_screen(0, 0, 320, 200);
      while (opkeys[SDLK_b])
        dumbcount++;
    }

  }  // end of wait for ESC press

  while (opkeys[SDLK_ESCAPE])
    dumbcount++;
  screenp->redrawme = 1;
  prefsob->save(this);
}


long viewscreen::change_speed(long whichway)
{
  if (whichway > 0)
  {
    screenp->timer_wait -= 2;
    if (screenp->timer_wait < 0)
      screenp->timer_wait = 0;
  }
  else if (whichway < 0)
  {
    screenp->timer_wait += 2;
    if (screenp->timer_wait > 20)
      screenp->timer_wait = 20;
  }
  return (long) ((20-screenp->timer_wait)/2+1);
}

long viewscreen::change_gamma(long whichway)
{
  if (whichway > 1)  // lighter
  {
    load_palette("our.pal", screenp->newpalette);
    adjust_palette(screenp->newpalette, ++gamma);
  }
  if (whichway < -1)  // darker
  {
    load_palette("our.pal", screenp->newpalette);
    adjust_palette(screenp->newpalette, --gamma);
  }
  if (whichway == -1) // set to default
  {
    gamma = 0;
    load_palette("our.pal", screenp->newpalette);
  }
  // So 0 just means report  
  return (long) gamma;
}

// **************************************************
// Options object
// **************************************************

options::options()
{
  int i;
  char *datap;
  FILE *infile;
  
  memcpy(*keys, *normalkeys, 64); // Allocate our normal keys
  // Set up preference defaults
  for(i=0; i<4; i++)
  {
    prefs[i][PREF_LIFE]  = PREF_LIFE_BOTH; // display hp/sp bars and numbers
    prefs[i][PREF_SCORE] = PREF_SCORE_ON;  // display score/exp info
    prefs[i][PREF_VIEW]  = PREF_VIEW_FULL; // start at full screen
    prefs[i][PREF_JOY]   = PREF_NO_JOY; //default to no joystick
    prefs[i][PREF_RADAR] = PREF_RADAR_ON;
    prefs[i][PREF_FOES]  = PREF_FOES_ON;
    prefs[i][PREF_GAMMA] = 0;
    prefs[i][PREF_OVERLAY] = PREF_OVERLAY_OFF; // no button behind text
    joys[i].joyaligned = 0;
  }
  
  infile = fopen(KEY_FILE, "rb");

  if (!infile) // failed to read
    return;

  // Read the blobs of data ..
  for (i=0; i < 4; i++)
  {
    fread(keys[i], 16, 1, infile);
    fread(prefs[i], 10, 1, infile);
    fread(&joys[i], sizeof(joyvalues), 1, infile);
  }

  fclose(infile);
  return; 
}

// It DOESN'T actually LOAD (tee hee), it only queries
//  the prefs object... but the stupid view objects
//  don't know that... don't tell them!
short options::load(viewscreen *viewp)
{
    short prefnum = viewp->mynum;
    // Yes, we are ACTUALLY COPYING the data
    memcpy(viewp->prefs, prefs[prefnum], 10);
    memcpy(viewp->mykeys, keys[prefnum], 16);
    viewp->joyaligned = joys[prefnum].joyaligned;
    viewp->joyleft = joys[prefnum].joyleft;
    viewp->joyright = joys[prefnum].joyright;
    viewp->joyup = joys[prefnum].joyup;
    viewp->joydown = joys[prefnum].joydown;
    viewp->minoffleft = joys[prefnum].minoffleft;
    viewp->minoffright = joys[prefnum].minoffright;
    viewp->minoffup = joys[prefnum].minoffup;
    viewp->minoffdown = joys[prefnum].minoffdown;
    viewp->joycenterx = joys[prefnum].joycenterx;
    viewp->joycentery = joys[prefnum].joycentery;
    viewp->deltax = joys[prefnum].deltax;
    viewp->deltay = joys[prefnum].deltay;
    return 1;
}


// This time, we actually DO access the file since the
//   bloke playing the game might decide to quit or
//   turn off the computer at any time and then
//   wonder later, "Where'd my prefs go! Bly'me!"
short options::save(viewscreen *viewp)
{
  short prefnum = viewp->mynum;
  long i;
  char *datap;
  FILE *outfile;

  // Yes, we are ACTUALLY COPYING the data
  memcpy(prefs[prefnum], viewp->prefs, 10);
  memcpy(keys[prefnum], viewp->mykeys, 16);
  joys[prefnum].joyaligned = viewp->joyaligned;
  joys[prefnum].joyleft = viewp->joyleft;
  joys[prefnum].joyright = viewp->joyright;
  joys[prefnum].joyup = viewp->joyup;
  joys[prefnum].joydown = viewp->joydown;
  joys[prefnum].minoffleft = viewp->minoffleft;
  joys[prefnum].minoffright = viewp->minoffright;
  joys[prefnum].minoffup = viewp->minoffup;
  joys[prefnum].minoffdown = viewp->minoffdown;
  joys[prefnum].joycenterx = viewp->joycenterx;
  joys[prefnum].joycentery = viewp->joycentery;
  joys[prefnum].deltax = viewp->deltax;
  joys[prefnum].deltay = viewp->deltay;
  
  outfile = fopen(KEY_FILE, "wb");

  if (!outfile) // failed to write
    return 0;

  // Write the blobs of data ..
  for (i=0; i < 4; i++)
  {
    fwrite(keys[i], 16, 1, outfile);
    fwrite(prefs[i], 10, 1, outfile);
    fwrite(&joys[i], sizeof(joyvalues), 1, outfile);    
  }

  fclose(outfile);

  return 1; 
}

options::~options()
{
}

/*

// save_key_prefs saves the state of all the player key preferences
// to the binary file KEY_FILE (currently keyprefs.dat)
// Returns success or failure
long save_key_prefs()
{
  long i;
  char *keypointer;
  FILE *outfile;

  outfile = fopen(KEY_FILE, "wb");

  if (!outfile) // failed to write
    return 0;

  // Write the blobs of data ..
  for (i=0; i < 4; i++)
  {
    keypointer = keys[i];
    fwrite(keypointer, 16, 1, outfile);
  }

  fclose(outfile);

  return 1; 

}

// load_key_prefs loads the state of all the player key preferences
// from the binary file KEY_FILE (currently keyprefs.dat)
// Returns success or failure
long load_key_prefs()
{
  long i;
  char *keypointer;
  FILE *infile;

  infile = fopen(KEY_FILE, "rb");

  if (!infile) // failed to read
    return 0;

  // Read the blobs of data ..
  for (i=0; i < 4; i++)
  {
    keypointer = keys[i];
    fread(keypointer, 16, 1, infile);
  }

  fclose(infile);
  return 1; 
}

*/

// set_key_prefs queries the user for key preferences, and
// places them into the proper key-press array.
// It returns success or failure.
long viewscreen::set_key_prefs()
{
  static text keytext(screenp);
  long dumbcount;
  static char message[80], tempstr[80];

  clear_keyboard();

  // Draw the menu button
  screenp->draw_button(40, 40, 280, 160, 2, 1); // same as options menu
  keytext.write_xy(160-6*6, OPLINES(0), "Keyboard Menu", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);

  keytext.write_xy(LEFT_OPS, OPLINES(2), "Press a key for 'UP':", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_UP] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(3), "Press a key for 'UP-RIGHT':", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_UP_RIGHT] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(4), "Press a key for 'RIGHT':", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_RIGHT] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(5), "Press a key for 'DOWN-RIGHT':", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_DOWN_RIGHT] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(6), "Press a key for 'DOWN':", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_DOWN] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(7), "Press a key for 'DOWN-LEFT':", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_DOWN_LEFT] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(8), "Press a key for 'LEFT':", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_LEFT] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(9), "Press a key for 'UP-LEFT':", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_UP_LEFT] = get_keypress();

  // Draw the menu button; back to the top for us!
  screenp->draw_button(40, 40, 280, 160, 2, 1); // same as options menu
  keytext.write_xy(160-6*6, OPLINES(0), "Keyboard Menu", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);

  keytext.write_xy(LEFT_OPS, OPLINES(2), "Press your 'FIRE' key:", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_FIRE] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(3), "Press your 'SPECIAL' key:", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_SPECIAL] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(4), "Press your 'SWITCHING' key:", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_SWITCH] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(5), "Press your 'SPECIAL SWITCH' key:", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_SPECIAL_SWITCH] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(6), "Press your 'YELL' key:", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_YELL] = get_keypress();

  keytext.write_xy(LEFT_OPS, OPLINES(7), "Press your 'SHIFTER' key:", (unsigned char) RED, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);
  keys[mynum][KEY_SHIFTER] = get_keypress();

//  keytext.write_xy(LEFT_OPS, OPLINES(8), "Press your 'MENU (PREFS)' key:", (unsigned char) RED, 1);
//  keys[mynum][KEY_PREFS] = get_keypress();

  if (CHEAT_MODE) // are cheats enabled?
  {
    keytext.write_xy(LEFT_OPS, OPLINES(9), "Press your 'CHEATS' key:", (unsigned char) RED, 1);
    screenp->buffer_to_screen(0, 0, 320, 200);
    keys[mynum][KEY_CHEAT] = get_keypress();
  }

  screenp->redrawme = 1;

//  return save_key_prefs();
  return 1;
}

// Waits for a key to be pressed and then released ..
// returns this key.
unsigned char get_keypress()
{
  long dumbcount;

  clear_key_press_event(); // clear any previous key
  while (!query_key_press_event())
    dumbcount++; // do nothing
  return query_key();
}

