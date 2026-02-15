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

// Definition of LIVING class

#include <openglad/legacy/base.h>
#include <openglad/entities/walker.h>
class living : public walker
{
	public:
		using walker::facing; // unhide overloads (Sint32/float) from base
		using walker::shove;  // unhide overloads (Sint32/float) from base
		living(const PixieData& data);
		~living() override;
		living(const living&) = delete;
		living& operator=(const living&) = delete;
		living(living&&) = delete;
		living& operator=(living&&) = delete;
		bool           act() override;
		bool           check_special() override;
		bool           collide(walker  *ob) override;
		bool           do_action(); // perform overriding action
		walker* do_summon(char whatfamily, Sint32 summon_lifetime) override;
		short          facing(short x, short y) override;
		void           set_difficulty(Uint32 whatlevel) override;
		short          shove(walker  *target, short x, short y) override;
		Order          query_order() const override
		{
			return Order::Living;
		}
		bool walk(float x, float y) override;
	protected:
		bool act_random() override;
};
