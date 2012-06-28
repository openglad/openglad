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
//
// Scen.h
//
#ifndef SCEN_H
#define SCEN_H

//#include "\wfiles\glad\graph.h"
//buffers: took out absolute locations in the includes of base.h and smooth.h
#include "base.h"
#include "smooth.h"

/* Zardus: why these values?
#define S_LEFT 12
#define S_RIGHT 245
#define S_UP 12
#define S_DOWN 188

*/

#define S_LEFT 1
#define S_RIGHT 245
#define S_UP 1
#define S_DOWN 188

#define VERSION_NUM (char) 8 // save scenario type info
#define SCROLLSIZE 8

#define OBJECT_MODE 0
#define MAP_MODE 1

#define NUM_BACKGROUNDS PIX_MAX

#define PIX_LEFT   (S_RIGHT+18)
#define PIX_TOP    (S_UP+79)
#define PIX_OVER   4
//#define PIX_DOWN   ((PIX_MAX/PIX_OVER)+1)
#define PIX_DOWN   4
#define PIX_RIGHT  (PIX_LEFT+(PIX_OVER*GRID_SIZE))
#define PIX_BOTTOM (PIX_TOP+(PIX_DOWN*GRID_SIZE))

#define L_D(x) ((S_UP+7)+8*x)
#define L_W(x) (x*8 + 9)
#define L_H(x) (x*8)

#define NORMAL_KEYBOARD(x)  clear_keyboard(); release_keyboard(); x grab_keyboard();


void set_screen_pos(screen *myscreen, int x, int y);
int save_scenario(char * filename, screen * master, char *gridname);
int save_map_file(char  * filename, screen *master);
int load_new_grid(screen *master);
int new_scenario_name();
int new_grid_name();
void do_help(screen * myscreen);
char some_pix(int whatback);
int check_collide(int x,  int y,  int xsize,  int ysize,
                   int x2, int y2, int xsize2, int ysize2);
walker * some_hit(int x, int y, walker  *ob, screen * screenp);

char  * query_my_map_name();

void remove_all_objects(screen *master);
void remove_first_ob(screen *master);


#endif
