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
// util.cpp
//
// random helper functions
//
#include "util.h"
#include "config.h"
#include <stdio.h>
#include <time.h>
#include <string.h> //buffers: for strlen
#include <string>
#include <sys/stat.h>

unsigned long start_time=0;
unsigned long reset_value=0;

void change_time(unsigned long new_count)
{}

void grab_timer()
{}

void release_timer()
{}

void reset_timer()
{
	reset_value = SDL_GetTicks();
}

long query_timer()
{
	// Zardus: why 13.6? With DOS timing, you had to divide 1,193,180 by the desired frequency and
	// that would return ticks / second. Gladiator used to use a frequency of 65536/4 ticks per hour,
	// or 1193180/16383 = 72.3 ticks per second. This translates into 13.6 milliseconds / tick
	return (long) ((SDL_GetTicks() - reset_value) / 13.6);
}

long query_timer_control()
{
	return (long) (SDL_GetTicks() / 13.6);
}

void time_delay(long delay)
{
	if (delay < 0) return;
	SDL_Delay((unsigned long) (delay * 13.6));
}

void lowercase(char * str)
{
	int i;
	for (i = 0; i < strlen(str);i++)
		str[i] = tolower(str[i]);
}

//buffers: add: another extra routine.
void uppercase(char *str)
{
	int i;
	for(i=0;i<strlen(str);i++)
		str[i] = toupper(str[i]);
}

// kari: yet two extra
void lowercase(std::string &str)
{
	for(std::string::iterator iter = str.begin(); iter!=str.end(); ++iter)
		*iter = tolower(*iter);
}

void uppercase(std::string &str)
{
	for(std::string::iterator iter = str.begin(); iter!=str.end(); ++iter)
		*iter = toupper(*iter);
}

FILE * open_misc_file(char * file, char * pos_dir, char * attr)
{
	FILE * infile;
	string filepath(file);

	filepath = getenv("HOME");
	filepath += "/.openglad/";
	filepath += pos_dir;
	filepath += file;

	if ((infile = fopen(filepath.c_str(), attr)))
		return infile;

	filepath = DATADIR;
	filepath += pos_dir;
	filepath += file;

	if ((infile = fopen(filepath.c_str(), attr)))
		return infile;

	// if it got here, it didn't find the file
	return NULL;
}

FILE * open_misc_file(char * file, char * pos_dir)
{
	return open_misc_file(file, pos_dir, "rb");
}

FILE * open_misc_file(char * file)
{
	return open_misc_file(file, "", "rb");
}

void create_dataopenglad()
{       
	string path(getenv("HOME"));
	path += "/.openglad/";
	mkdir(path.c_str(), 0755);
	path.reserve(path.size()+10);
	string::iterator subdirpos = path.end();
	path += "pix/";
	mkdir(path.c_str(), 0755);
	path.replace(subdirpos, path.end(), "scen/", 4);
	mkdir(path.c_str(), 0755);
	path.replace(subdirpos, path.end(), "save/", 5);
	mkdir(path.c_str(), 0755);
	path.replace(subdirpos, path.end(), "sound/", 5);
	mkdir(path.c_str(), 0755);
}

char * get_file_path(char * file, char * pos_dir, char * attr)
{
	FILE * infile;
	string filepath(file);

	filepath = getenv("HOME");
	filepath += "/.openglad/";
	filepath += pos_dir;
	filepath += file;

	if ((infile = fopen(filepath.c_str(), attr)))
	{
		fclose(infile);
		return (char *)filepath.c_str();
	}

	filepath = DATADIR;
	filepath += pos_dir;
	filepath += file;

	if ((infile = fopen(filepath.c_str(), attr)))
	{
		fclose(infile);
		return (char *)filepath.c_str();
	}

	// if it got here, it didn't find the file
	return NULL;
}

char * get_file_path(char * file, char * pos_dir)
{
	return get_file_path(file, pos_dir, "rb");
}

char * get_file_path(char * file)
{
	return get_file_path(file, "", "rb");
}
