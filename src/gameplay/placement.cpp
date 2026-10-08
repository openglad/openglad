/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// New Specials placement verbs (see placement.h). swap_places is a
// placeholder that keeps every caller compiling: nothing swaps yet.
#include <openglad/gameplay/placement.h>

#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/walker.h>

namespace og::sim {

bool declares_blocks_placement(const walker& w)
{
    if (w.query_order() != Order::Weapon)
        return false;
    const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(
        static_cast<int>(static_cast<unsigned char>(w.family())));
    return wfd != nullptr && wfd->blocks_placement;
}

bool swap_places(GameWorld& /*world*/, walker& /*a*/, walker& /*b*/)
{
    return false;
}

}  // namespace og::sim
