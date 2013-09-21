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
#ifndef __LIVING_H
#define __LIVING_H

// Definition of LIVING class

#include "base.h"
#include "walker.h"

class living : public walker
{
	public:
		living(const PixieData& data, screen  *myscreen);
		virtual ~living();
		short          act();
		short          check_special(); // determine if we should do special ..
		short          collide(walker  *ob);
		short          do_action(); // perform overriding action
		walker*        do_summon(char whatfamily, unsigned short lifetime);
		short          facing(short x, short y);
		void           set_difficulty(Uint32 whatlevel);
		short          shove(walker  *target, short x, short y);
		char           query_order()
		{
			return ORDER_LIVING;
		}
		virtual bool walk(float x, float y);
	protected:
		short act_random();
};

#endif

