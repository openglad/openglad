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
/* gladpack.h
 *
 * Header for gladpack.cpp
 *
 * Created 8/7/95 Doug Ricket
 *
*/

#ifndef GLADPACK_H
#define GLADPACK_H

#include <stdio.h>

#define GLAD_HEADER             "GladPack"
#define GLAD_HEADER_SIZE        8
#define FILENAME_SIZE           13

class packfileinfo;     /* Used internally by packfile */

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
		bool opened() {return numfiles > 0;}
		int close();

		FILE *get_subfile(char *subfilename);
		long get_subfilesize();

};

#endif
