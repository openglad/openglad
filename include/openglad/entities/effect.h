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

// Definition of EFFECT class

#include <openglad/entities/walker.h>
// Pure helper functions extracted from effect logic
void orbit_offset(int drawcycle, float &xd, float &yd);
std::int32_t compute_explosion_range(std::int32_t level, short skip_exit);

class effect : public walker
{
	public:
		effect(const PixieData& data);
		effect();  // Headless constructor (no rendering data)
		~effect() override;
		effect(const effect&) = delete;
		effect& operator=(const effect&) = delete;
		effect(effect&&) = delete;
		effect& operator=(effect&&) = delete;
		bool act() override;
		bool animate() override;
		bool death() override;
		Order query_order() const override
		{
			return Order::FX;
		}
};

