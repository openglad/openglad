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

#define GLAD_HEADER             "GladPack"
#define GLAD_HEADER_SIZE        8
#define FILENAME_SIZE           13

class packfileinfo
{
	public:
		long filepos;
		char name[FILENAME_SIZE];
};

class packfile
{
	private:

		FILE *datafile;

		short numfiles;
		short last_subfile;
		packfileinfo *fileinfo;
		long filesize;

	public:

		packfile()
		{
			numfiles = 0;
		}
		~packfile()
		{
			close();
		}

		int open(char *filename);
		int close();

		FILE *get_subfile(char *subfilename);
		long get_subfilesize();

};

int packfile::open(char *filename)
{
	long i;
	char temp[GLAD_HEADER_SIZE+1];

	if ( (datafile=fopen(filename, "rb")) == NULL)
		return -1;

	fread(temp, GLAD_HEADER_SIZE, 1, datafile);
	temp[GLAD_HEADER_SIZE] = 0;
	if ( strcmp(temp, GLAD_HEADER) != 0)
		return -2;

	fread(&numfiles, sizeof(short), 1, datafile);

	fileinfo = new packfileinfo[numfiles];
	for (i=0; i < numfiles; i++)
		fread(&(fileinfo[i]), FILENAME_SIZE + sizeof(long), 1, datafile);
	fread(&filesize, sizeof(long), 1, datafile);

	return 1;
}

int packfile::close()
{
	if (numfiles)
	{
		fclose(datafile);
		numfiles = 0;
		delete[] fileinfo;
	}
	return 1;
}

FILE *packfile::get_subfile(char *subfilename)
{
	short i;

	//buffers: PORT: implicit delcaration: strupr(subfilename);

	for (i=numfiles; i--; )
		if ( strcmp(subfilename, fileinfo[i].name) == 0 )
		{
			fseek(datafile, fileinfo[i].filepos, SEEK_SET);
			last_subfile = i;
			return datafile;
		}

	return NULL;
}

long packfile::get_subfilesize()
{
	if (last_subfile + 1 == numfiles)
		return filesize - fileinfo[last_subfile].filepos;

	return fileinfo[last_subfile+1].filepos -
	       fileinfo[last_subfile].filepos;
}



