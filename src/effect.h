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
#ifndef __EFFECT_H
#define __EFFECT_H

// Definition of EFFECT class

#include "base.h"
#include "obmap.h"
#include "screen.h"
#include "stats.h"
#include "walker.h"
#include "guy.h"

class effect : public walker
{
	public:
		effect(char  *data, screen  *myscreen);
		~effect();
		short          act();
		short          animate();
		short          death(); // called on destruction
		char         query_order()
		{
			return ORDER_FX;
		}
};

#endif

