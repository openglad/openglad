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

#include "version.h"

#include <stdio.h>
#include <time.h>
#include <string.h> //buffers: for strlen
#include <string>
#include <sys/stat.h>
#include "base.h"

#ifdef WINDOWS
#include "windows.h"
#include <shlobj.h>
#include <time.h>
#include <direct.h>

#ifndef mkdir
#define mkdir(path, perms) _mkdir(path)
#endif

#endif

using namespace std;

Uint32 start_time=0;
Uint32 reset_value=0;


void Log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
#ifdef USE_SDL2
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_ERROR, format, args);
#else
    char buf[300];
    snprintf(buf, 300, "%s\n", format);  // Add a newline
    vfprintf(stderr, buf, args);
#endif
    va_end(args);
}

void change_time(Uint32 new_count)
{}

void grab_timer()
{}

void release_timer()
{}

void reset_timer()
{
    reset_value = SDL_GetTicks();
}

Sint32 query_timer()
{
    // Zardus: why 13.6? With DOS timing, you had to divide 1,193,180 by the desired frequency and
    // that would return ticks / second. Gladiator used to use a frequency of 65536/4 ticks per hour,
    // or 1193180/16383 = 72.3 ticks per second. This translates into 13.6 milliseconds / tick
    return (Sint32) ((SDL_GetTicks() - reset_value) / 13.6);
}

Sint32 query_timer_control()
{
    return (Sint32) (SDL_GetTicks() / 13.6);
}

void time_delay(Sint32 delay)
{
    if (delay < 0) return;
    SDL_Delay((Uint32) (delay * 13.6));
}

void lowercase(char * str)
{
    unsigned int i;
    for (i = 0; i < strlen(str); i++)
        str[i] = tolower(str[i]);
}

//buffers: add: another extra routine.
void uppercase(char *str)
{
    unsigned int i;
    for(i=0; i<strlen(str); i++)
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
