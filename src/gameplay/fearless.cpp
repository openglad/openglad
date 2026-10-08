/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// New Specials fearlessness (see fearless.h): the walker's own mark, or,
// with the setting on, a friendly live weapon whose family declares a
// rally_radius (the war banner) standing near it on its floor.
#include <openglad/gameplay/fearless.h>

#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>

namespace og::sim {

bool fearless(const walker& w)
{
    if (w.fearless_bit())
        return true;
    // With the setting off no banner exists, and a pack weapon that happens
    // to declare rally_radius must not change a classic game either.
    if (session_new_specials() == 0)
        return false;
    for (const auto& entry : current_game->world->weaplist) {
        const walker* rally = entry.get();
        if (rally == nullptr || rally->dead() || rally == &w)
            continue;
        if (rally->query_order() != Order::Weapon || rally->floor() != w.floor())
            continue;
        const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(
            static_cast<int>(static_cast<unsigned char>(rally->family())));
        if (wfd == nullptr || wfd->rally_radius <= 0)
            continue;
        if (!w.is_friendly(rally))
            continue;
        const std::int32_t r = wfd->rally_radius;
        if (w.distance_to_ob_center(rally) <= r * r)
            return true;
    }
    return false;
}

}  // namespace og::sim
