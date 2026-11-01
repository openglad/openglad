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
#ifndef __OBMAP_H
#define __OBMAP_H

// Definition of OBMAP class

#include "base.h"
#include <map>
#include <list>

class obmap
{
	public:
		obmap();
		~obmap();
		// The pile half of query_passable. quiet = true asks the same question
		// without changing anything: no draw (an enemy weapon in flight is
		// passed, where the loud probe passes it 6 times in 10), no treasure
		// eaten, no door opened and no "Key N needed!" notice (the loud answer
		// is kept: a door blocks this round), and no collide() or collide_ob
		// change. Loud by default. (port of the current game's 15053f4a)
		short query_list(walker  *ob, short x, short y, bool quiet = false);
		short remove(walker  *ob);  // This goes in walker's destructor
		short add(walker  *ob, short x, short y);  // This goes in walker's constructor
		short move(walker  *ob, short x, short y);  // This goes in walker's setxy
		std::list<walker*>& obmap_get_list(short x, short y); //Returns the list at x,y for fnf
		// The pile at x,y, or NULL when none was ever made there: a read that
		// never creates a node (the body probe's sweep; port of the current
		// game's 15053f4a)
		const std::list<walker*>* find_list(short x, short y);
		short obmapres;
		size_t size() const;
		void draw();
		
		std::map<std::pair<short, short>, std::list<walker*> > pos_to_walker;
		std::map<walker*, std::list<std::pair<short, short> > > walker_to_pos;
		
	private:
		short hash(short y);
		short unhash(short y);
};

// The obmap's box test (obmap.cpp): two boxes meet when they overlap by more
// than the one-pixel shrink the passability probe applies.
short collide(short x, short y, short xsize, short ysize, short x2, short y2, short xsize2, short ysize2);

// Does `mover`'s box, moved by (dx, dy) from where it stands NOW, meet
// `body`'s box, by the obmap's own collide() rule? The three boxes
// living::walk probes are tried (the exact step, its ceil and its floor),
// narrowed to short as obmap::query_list narrows them. Pure integer
// arithmetic: no draw, nothing written. Gameplay Fixes X and Z: a walk
// shoves and a rush strikes only a body the step itself ran into, never the
// stale collide_ob a grid-stopped step or a side probe left behind. (port of
// the current game's 94a4ae95)
bool step_meets(walker *mover, float dx, float dy, walker *body);

#endif
