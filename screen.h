#ifndef __SCREEN_H
#define __SCREEN_H

// Definition of SCREEN class

#include "base.h"
#include "video.h"
#include "loader.h"

// Goddamned function doesn't belong in screen, much less as a C style extern!
//   I'll fix this eventually, if I ever get hold of that part of the code.
short load_scenario(char * filename, screen * master);

class screen : public video
{
  public:
         screen();  // called with '1' for numviews
         screen(short howmany);

         void reset(short howmany); 
         ~screen();
         void clear();
         video * get_video_ob();
         short query_passable(short x,short y,walker  *ob);
         short query_object_passable(short x, short y, walker  *ob);
         inline short query_grid_passable(short x, short y, walker  *ob);
         short redraw();
         void refresh();
         walker  * first_of(unsigned char whatorder, unsigned char whatfamily,
                            int team_num = -1);
         short input(char input);
         short act();
         walker  *add_ob(char order, char family);
         walker  *add_ob(char order, char family, short atstart); // to insert
         walker  *add_ob(walker  *newob); // add an existing walker
         walker  *add_fx_ob(char order, char family);
         walker  *add_fx_ob(walker  *newob); // add an existing walker
         walker  *add_weap_ob(char order, char family);
         walker  *add_weap_ob(walker  *newob); // add an existing weapon
         short remove_ob(walker  *ob, short no_delete); // don't delete us?

         short endgame(short ending);
         short endgame(short ending, short nextlevel); // what level next?
         walker *find_near_foe(walker *ob);
         walker *find_far_foe(walker *ob);
         walker  *get_new_control();
         void draw_panels(short howmany);
         walker  * find_nearest_blood(walker  *who);
         walker* find_nearest_player(walker *ob);
         oblink* find_in_range(oblink *somelist, long range, short *howmany, walker  *ob);
         oblink* find_foes_in_range(oblink *somelist, long range, short *howmany, walker  *ob);
         oblink* find_friends_in_range(oblink *somelist, long range, short *howmany, walker  *ob);
         oblink* find_foe_weapons_in_range(oblink *somelist, long range, short *howmany, walker  *ob);
         char damage_tile(short xloc, short yloc); // damage the specified tile
         void do_notify(char *message, walker  *who);  // printing text
         void report_mem();
         inline walker *set_walker(walker *ob, char order, char family)
            { return myloader->set_walker(ob, order, family); }
         char* get_scen_title(char *filename, screen *master);

         char scenario_type; // 0 is default
          char special_name[NUM_FAMILIES][NUM_SPECIALS][20];
          char alternate_name[NUM_FAMILIES][NUM_SPECIALS][20];
         unsigned short enemy_freeze; // stops enemies from acting
         soundob *soundp;       
         short redrawme;
         char levelstatus[MAX_LEVELS];
         char scentext[80][80];                         // Array to hold scenario information
         char scentextlines;                    // How many lines of text in scenario info
         char newpalette[768];
         short palmode;
         short my_team;
         guy  *first_guy;
         unsigned char  *grid;
         long maxx,maxy;
         long pixmaxx,pixmaxy;
         long topx, topy;
         short control_hp; // last turn's hitpoints
         oblink  *oblist;
         oblink  *fxlist;  // fx--explosions, etc.
         oblink  *weaplist;  // weapons
         loader * myloader;
         char end;
         pixieN  *back[PIX_MAX];
         long numobs;
         obmap  *myobmap;
         signed char timer_wait;
         short scen_num;
         unsigned long score;
         unsigned long m_score[4];
         unsigned long totalcash;
         unsigned long m_totalcash[4];
         unsigned long totalscore;
         unsigned long m_totalscore[4];
         viewscreen  * viewob[5];
         short numviews;
         unsigned long timerstart;
         unsigned long framecount;
         walker * weapfree; //free weapons for re-allocation
         char scenario_title[30]; // used in scenarios v. 6+
         short allied_mode;
         short par_value;
         short level_done; // set true when all our foes are dead
  protected:
         unsigned char  *pixdata[PIX_MAX];
};

#endif

