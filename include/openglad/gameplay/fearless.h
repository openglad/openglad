/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

class walker;

namespace og::sim {

// A walker that never flees: it wears KIT_FEARLESS, or (with the New
// Specials setting on) it stands within the rally_radius of a friendly live
// weapon on its floor (the war banner). Consulted by the flee command and
// by the fright binding. RNG-free.
[[nodiscard]] bool fearless(const walker& w);

}  // namespace og::sim
