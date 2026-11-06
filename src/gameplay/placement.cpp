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
    // rule: Only weapon-order scenery whose family declares blocks_placement (bone wall, war
    //   banner) blocks spawn/summon placement spots; livings and other weapons do not.
    // tests: SpecialsView.blocks_placement_reaches_the_predicate
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
    // rule: Two different walkers that are alive, awake and visible trade positions and floors
    //   (nothing underfoot is eaten, both stay indexed once); a walker cannot swap with itself or
    //   with a dead, dormant or hidden walker.
    // tests: ScriptBindingPropsTest.kit_swap_places_trades_spots_and_floors_and_eats_nothing
    // tests: SemPlacement.a_dead_or_dormant_walker_never_swaps_places
    if (&a == &b || !swappable(a) || !swappable(b))
        return false;
    // Out of the collision table first, so neither walker is ever indexed
    // on the other's cells or floor half-way through the exchange.
    // plumbing: No obmap in a bare world: skip removal.
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
