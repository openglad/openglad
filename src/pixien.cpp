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
//pixien.cpp

/* ChangeLog
	buffers: 7/31/02: *include cleanup
*/
#include "graph.h"

// ************************************************************
//  PixieN -- N-Frame pixie
//  It is identical to the PIXIE class except it handles
//  multiple frames and switch frames before a put.
// ************************************************************

pixieN::pixieN(unsigned char  *data, screen  *myscreen):
		pixie(data+3,data[1],data[2], myscreen)
{
	facings = data+3;
	//bmp = (data+3);
	frames = data[0];
	frame = 0;
}

pixieN::pixieN(unsigned char  *data, screen  *myscreen, int doaccel):
		pixie(data+3,data[1],data[2], myscreen)
{
	facings = data+3;
	//bmp = (data+3);
	frames = data[0];
	frame = 0;
	if(doaccel)
		init_sdl_surface();
}

pixieN::~pixieN()
{
	bmp = NULL;
	facings = NULL;
	frames = 0;
	frame = 0;
}

// Changes the frame number and poshorts the BMP data poshorter to
//  the correct frame's data
short pixieN::set_frame(short framenum)
{
	if (framenum < 0 || framenum >= frames)
	{
		//printf("setting frame less than 0.\n");
		return 0;
	}
	bmp = facings+framenum*size;
	frame = framenum;
	return 1;
}

short pixieN::next_frame()
{
	return set_frame(frame++ % frames);
}

short pixieN::query_frame()
{
	return frame;
}
