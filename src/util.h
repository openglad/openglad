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
// util.h
//
// misc helper functions, and timer
//

#include <SDL.h>
#include <ctype.h>
#include <string>


void change_time(unsigned long new_count);

void grab_timer();
void release_timer();
void reset_timer();
long query_timer();
long query_timer_control();

// Zardus: add: lowercase func
void lowercase(char *);
// kari: lowercase for std::strings
void lowercase(std::string &);
//buffers: add: uppercase func
void uppercase(char *);
// kari: uppercase for std::strings
void uppercase(std::string &);
// Zardus: add: set_mult func
void set_mult(int);
FILE * get_misc_file(char *, char *);
FILE * get_misc_file(char *);
