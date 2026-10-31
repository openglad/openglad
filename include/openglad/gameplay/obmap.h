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
#pragma once

// Definition of OBMAP class

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <list>

class walker;

class obmap
{
	public:
		obmap();
		~obmap();
		// cell_index_ holds pointers into pos_to_walker nodes; a copied obmap
		// would alias the source's piles.
		obmap(const obmap&) = delete;
		obmap& operator=(const obmap&) = delete;
		// The pile half of query_passable. quiet = true asks the same question
		// without changing anything: no draw (an enemy weapon in flight is
		// passed, where the loud probe passes it 6 times in 10), no treasure
		// eaten or exit latched (a treasure never blocks), no door opened and
		// no "Key N needed!" notice (the loud answer is kept: a door blocks
		// this round), and no collide() or collide_ob change. Loud by default.
		short query_list(walker  *ob, short x, short y, bool quiet = false);
		short remove(walker  *ob);  // This goes in walker's destructor
		short add(walker  *ob, short x, short y);  // This goes in walker's constructor
		short move(walker  *ob, short x, short y);  // This goes in walker's setxy
		std::list<walker*>& obmap_get_list(short x, short y, int floor = 0); //Returns the list at x,y for fnf
		// The pile at (x, y) on `floor`, or nullptr when no walker is filed
		// there: a read that makes no node (#417; obmap_get_list stays for the
		// one caller that erases through it, GameWorld::find_near_foe).
		const std::list<walker*>* find_list(short x, short y, int floor = 0);
		short obmapres;
		size_t size() const;

		std::map<std::pair<short, short>, std::list<walker*> > pos_to_walker;
		std::unordered_map<walker*, std::list<std::pair<short, short> > > walker_to_pos;

		static short hash(short y);
		static short unhash(short y);

	private:
		// O(1) shadow index over pos_to_walker, keyed by packed (numx, numy).
		// std::map nodes are pointer-stable, so an entry stays valid until its
		// node is erased; every erase inside obmap.cpp forgets its key, and a
		// wholesale external pos_to_walker.clear() (level teardown) is caught
		// by sync_cell_index() before any cached pointer is dereferenced.
		// External code may insert piles into pos_to_walker directly (tests
		// do; the index tolerates unknown keys) but must never erase
		// individual piles behind obmap's back.
		std::unordered_map<std::uint32_t, std::list<walker*>*> cell_index_;
		void sync_cell_index();
};

// The obmap's box test (obmap.cpp): two boxes meet when they overlap by more
// than the one-pixel shrink the passability probe applies.
short collide(short x, short y, short xsize, short ysize, short x2, short y2, short xsize2, short ysize2);

namespace og::sim {
// Does `mover`'s box, moved by (dx, dy) from where it stands NOW, meet
// `body`'s box, by the obmap's own collide() rule? The three boxes
// living::walk probes are tried (the exact step, its ceil and its floor),
// narrowed to short as obmap::query_list narrows them, so the box tested is
// the box the probe tested, by collide() alone (the probe's z skip and its
// separating-move pass are not asked: a body touching the mover may still
// be shoved). Pure integer arithmetic: no draw, nothing
// written. Gameplay Fixes X and Z: a walk shoves and a rush strikes only a
// body the step itself ran into, never the stale collide_ob a teleporter, a
// grid-stopped step or a side probe left behind.
bool step_meets(const walker& mover, float dx, float dy, const walker& body);
}
