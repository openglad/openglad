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

// Definition of WEAP class

#include "base.h"
#include "walker.h"

class weap : public walker
{
	public:
		using walker::setxy; // unhide overloads (Sint32/float/Uint32) from base
		weap(const PixieData& data);
		~weap() override;
		weap(const weap&) = delete;
		weap& operator=(const weap&) = delete;
		weap(weap&&) = delete;
		weap& operator=(weap&&) = delete;

		bool act() override;
		bool animate() override;
		bool death() override;
		bool setxy(short x, short y) override;
		Order query_order() const override
		{
			return Order::Weapon;
		}

		// Weapons-only related variables; use with care
		Sint32 do_bounce; // do we bounce?

};
