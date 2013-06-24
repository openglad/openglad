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
#ifndef __LOADER_H
#define __LOADER_H

// Definition of LOADER class

#include "base.h"

class loader
{
	public:
		loader();
		virtual ~loader(void);
		walker  *create_walker(char order, char family, screen  *screenp, bool cache_weapons = true);
		pixieN *create_pixieN(char order, char family);
		walker *set_walker(walker *ob, char order, char family);
		unsigned char **graphics;
		signed char  ***animations;
		Sint32  *stepsizes;
		Sint32  *lineofsight;
	protected:
		//         char  *hitpoints;
		short  hitpoints[200]; // hack for now
		char  *act_types;
		Sint32  *damage;
		signed char  *fire_frequency;
};

#endif
