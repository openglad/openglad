//
// Scen.h
//
#ifndef SCEN_H
#define SCEN_H

//#include "\wfiles\glad\graph.h"
#include "\wfiles\glad\base.h"

#include "\wfiles\glad\smooth.h"

#define S_LEFT 12
#define S_RIGHT 245
#define S_UP 12
#define S_DOWN 188

#define VERSION_NUM (char) 8 // save scenario type info
#define SCROLLSIZE 8

#define OBJECT_MODE 0
#define MAP_MODE 1

#define NUM_BACKGROUNDS PIX_MAX

#define PIX_LEFT   (S_RIGHT+18)
#define PIX_TOP    (S_UP+68)
#define PIX_OVER   4
//#define PIX_DOWN   ((PIX_MAX/PIX_OVER)+1)
#define PIX_DOWN   4
#define PIX_RIGHT  (PIX_LEFT+(PIX_OVER*GRID_SIZE))
#define PIX_BOTTOM (PIX_TOP+(PIX_DOWN*GRID_SIZE))

#define L_D(x) ((S_UP-4)+8*x)
#define L_W(x) (x*8)
#define L_H(x) (x*8)

#define NORMAL_KEYBOARD(x)  clear_keyboard(); release_keyboard(); x grab_keyboard();


void set_screen_pos(screen *myscreen, long x, long y);
long save_scenario(char * filename, screen * master, char *gridname);
long save_map_file(char  * filename, screen *master);
long load_new_grid(screen *master);
long new_scenario_name();
long new_grid_name();
void do_help(screen * myscreen);
char some_pix(long whatback);
long check_collide(long x,  long y,  long xsize,  long ysize,
            long x2, long y2, long xsize2, long ysize2);
walker * some_hit(long x, long y, walker  *ob, screen * screenp);

char  * query_my_map_name();

void uppercase(char *somestring);
void lowercase(char *somestring);

void remove_all_objects(screen *master);
void remove_first_ob(screen *master);


#endif
