/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// New Specials placement verbs (see placement.h).
#include <openglad/gameplay/placement.h>

#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/respawn/respawn_state.h>
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

namespace {

[[nodiscard]] bool swappable(const walker& w)
{
    return !w.dead() && !w.dormant() && !w.hidden();
}

}  // namespace

bool swap_places(GameWorld& world, walker& a, walker& b)
{
    if (&a == &b || !swappable(a) || !swappable(b))
        return false;
    // Out of the collision table first, so neither walker is ever indexed
    // on the other's cells or floor half-way through the exchange.
    if (world.myobmap != nullptr) {
        world.myobmap->remove(&a);
        world.myobmap->remove(&b);
    }
    const short ax = a.xpos();
    const short ay = a.ypos();
    const short afloor = a.floor();
    const short bx = b.xpos();
    const short by = b.ypos();
    const short bfloor = b.floor();
    a.set_floor(bfloor);
    b.set_floor(afloor);
    a.setxy(bx, by);
    b.setxy(ax, ay);
    // setxy leaves a walker unindexed when its coordinates did not change
    // (a swap across floors at one spot): register both explicitly.
    ensure_obmap_registration(world, &a);
    ensure_obmap_registration(world, &b);
    return true;
}

}  // namespace og::sim
