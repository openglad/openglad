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
// parsecfg.h
//

//#include <stdio.h>
//#include <string.h>
//#include <cstring.h>
//includes moved to parsecfg.cpp
#include <stdio.h> //needed here for a file* function

FILE* open_cfg_file(char *filename);   // returns a poshorter to open                                                                                                                          // .cfg file
void close_cfg_file(FILE *cfgfile);     // close a .cfg file
void dump_cfg_file(FILE *cfgfile);
char* read_one_line(FILE *cfgfile);    // read until \n
char* query_cfg_value(FILE *cfgfile, char *section, char *pattern);
