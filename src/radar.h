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
#ifndef __RADAR_H
#define __RADAR_H

// Definition of RADAR class

#include "base.h"
#include "level_data.h"

class radar
{
	public:
		radar(viewscreen * myview, screen * myscreen, short whatnum);
		~radar();
		short draw();
		short draw(LevelData* data);
		short sizex, sizey;
		short xpos,ypos;
		short xloc, yloc;        // where on the screen to display
		// Zardus: radarx and radary are now class members (instead of temp vars) so that scen can use them for scroll
		short radarx, radary;	  // what actual portion of the map is on the radar (top-left coord)
		short on_screen();
		short on_screen(short whatx, short whaty, short hor, short ver);
		short refresh();
		void update(); // slow function to update radar/map info
		void update(LevelData* data);
		void start();
		void start(LevelData* data);

		screen * screenp;
		viewscreen * viewscreenp;
		unsigned char  *bmp,  *oldbmp;
		bool force_lower_position;
		short xview;
		short yview;
	protected:
		short mynum; // what is my viewscreen-related number?
		//         char  *buffer;
		unsigned short size;
};

#endif

