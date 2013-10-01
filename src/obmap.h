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
		short query_list(walker  *ob, short x, short y);
		short remove(walker  *ob);  // This goes in walker's destructor
		short add(walker  *ob, short x, short y);  // This goes in walker's constructor
		short move(walker  *ob, short x, short y);  // This goes in walker's setxy
		std::list<walker*>& obmap_get_list(short x, short y); //Returns the list at x,y for fnf
		short obmapres;
		size_t size() const;
		void draw();
		
		std::map<std::pair<short, short>, std::list<walker*> > pos_to_walker;
		std::map<walker*, std::list<std::pair<short, short> > > walker_to_pos;
		
	private:
		short hash(short y);
		short unhash(short y);
};

// Zardus: ADD: and a little util for oblink chains
void delete_list(oblink *);

#endif
