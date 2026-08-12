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
		short query_list(walker  *ob, short x, short y);
		short remove(walker  *ob);  // This goes in walker's destructor
		short add(walker  *ob, short x, short y);  // This goes in walker's constructor
		short move(walker  *ob, short x, short y);  // This goes in walker's setxy
		std::list<walker*>& obmap_get_list(short x, short y, int floor = 0); //Returns the list at x,y for fnf
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
