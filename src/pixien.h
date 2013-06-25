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
#ifndef __PIXIEN_H
#define __PIXIEN_H

// Definition of PIXIEN class

#include "base.h"
#include "pixie.h"

class pixieN : public pixie
{
	public:
		pixieN(const PixieData& data, screen  *myscreen);
		pixieN(const PixieData& data, screen  *myscreen, int doaccel);
		virtual ~pixieN();
		short set_frame(short framenum);
		short query_frame();
		short next_frame();
	protected:
		short frames; // total frames
		short frame; // current frame
		unsigned char * facings;
};

#endif

