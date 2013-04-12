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
/* gladpack.cpp
 *
 * Interface to Glad Pack Files
 *
 * Created 8/7/95 Doug Ricket
 *
*/

// Z's script: #include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gladpack.h"
#include "util.h"
#include <string>
using namespace std;

#define GLAD_HEADER             "GladPack"
#define GLAD_HEADER_SIZE        8
#define FILENAME_SIZE           13

class packfileinfo
{
	public:
		Sint32 filepos;
		char name[FILENAME_SIZE];
};

int packfile::open(const char *filename)
{
	Sint32 i;
	char temp[GLAD_HEADER_SIZE+1];

	// Zardus: first try in the current directory
	if ( (datafile=open_data_file((char *)filename)) == NULL)
	{
		return -1;
	}
	
	SDL_RWread(datafile, temp, GLAD_HEADER_SIZE, 1);
	temp[GLAD_HEADER_SIZE] = 0;
	if ( strcmp(temp, GLAD_HEADER) != 0)
		return -2;

	SDL_RWread(datafile, &numfiles, sizeof(short), 1);

	fileinfo = new packfileinfo[numfiles];
	for (i=0; i < numfiles; i++)
		SDL_RWread(datafile, &(fileinfo[i]), FILENAME_SIZE + sizeof(Sint32), 1);
	SDL_RWread(datafile, &filesize, sizeof(Sint32), 1);

	return 1;
}

int packfile::close()
{
	if (numfiles)
	{
	    SDL_RWclose(datafile);
		numfiles = 0;
		delete[] fileinfo;
	}
	return 1;
}

SDL_RWops *packfile::get_subfile(const char *subfilename)
{
	short i;

	for (i=numfiles; i--; )
		if ( strcmp(subfilename, fileinfo[i].name) == 0 )
		{
			SDL_RWseek(datafile, fileinfo[i].filepos, SEEK_SET);
			last_subfile = i;
			return datafile;
		}

	return NULL;
}

Sint32 packfile::get_subfilesize()
{
	if (last_subfile + 1 == numfiles)
		return filesize - fileinfo[last_subfile].filepos;

	return fileinfo[last_subfile+1].filepos -
	       fileinfo[last_subfile].filepos;
}




