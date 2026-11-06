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
    // rule: A walker is fearless (never flees when hit, a fright binding skips it) if it carries
    //   the kit FEARLESS mark, or, with New Specials on, if a live friendly weapon-order rally
    //   object (war banner: a weapon family declaring rally_radius > 0) stands on its floor within
    //   rally_radius of its centre. With New Specials off a banner-like pack weapon never makes
    //   anyone fearless.
    // tests: KitSeamsSmall.fearless_bit_and_rally_radius_block_the_flee_walk
    // tests: KitCaptain.rallied_walker_does_not_flee_when_hit
    // tests: ScriptBindingPropsTest.kit_fearless_answers_the_kit_mark
    // tests: SemFearless.a_banner_is_not_made_fearless_by_its_own_aura
    // tests: SemFearless.a_weapon_without_a_rally_radius_rallies_nobody
    // tests: SemFearless.the_rally_radius_is_inclusive_at_exactly_r
    if (w.fearless_bit())
        return true;
    // With the setting off no banner exists, and a pack weapon that happens
    // to declare rally_radius must not change a classic game either.
    if (session_new_specials() == 0)
        return false;
    for (const auto& entry : current_game->world->weaplist) {
        const walker* rally = entry.get();
        // rule: A walker is fearless (never flees when hit, a fright binding skips it) if it
        //   carries the kit FEARLESS mark, or, with New Specials on, if a live friendly
        //   weapon-order rally object (war banner.
        // tests: KitSeamsSmall.fearless_bit_and_rally_radius_block_the_flee_walk
        // tests: KitCaptain.rallied_walker_does_not_flee_when_hit
        // tests: ScriptBindingPropsTest.kit_fearless_answers_the_kit_mark
        // tests: SemFearless.a_banner_is_not_made_fearless_by_its_own_aura
        // tests: SemFearless.a_weapon_without_a_rally_radius_rallies_nobody
        // tests: SemFearless.the_rally_radius_is_inclusive_at_exactly_r
        if (rally == nullptr || rally->dead() || rally == &w)
            continue;
        // rule: A walker is fearless (never flees when hit, a fright binding skips it) if it
        //   carries the kit FEARLESS mark, or, with New Specials on, if a live friendly
        //   weapon-order rally object (war banner.
        // tests: KitSeamsSmall.fearless_bit_and_rally_radius_block_the_flee_walk
        // tests: KitCaptain.rallied_walker_does_not_flee_when_hit
        // tests: ScriptBindingPropsTest.kit_fearless_answers_the_kit_mark
        // tests: SemFearless.a_banner_is_not_made_fearless_by_its_own_aura
        // tests: SemFearless.a_weapon_without_a_rally_radius_rallies_nobody
        // tests: SemFearless.the_rally_radius_is_inclusive_at_exactly_r
        if (rally->query_order() != Order::Weapon || rally->floor() != w.floor())
            continue;
        const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(
            static_cast<int>(static_cast<unsigned char>(rally->family())));
        // rule: A walker is fearless (never flees when hit, a fright binding skips it) if it
        //   carries the kit FEARLESS mark, or, with New Specials on, if a live friendly
        //   weapon-order rally object (war banner.
        // tests: KitSeamsSmall.fearless_bit_and_rally_radius_block_the_flee_walk
        // tests: KitCaptain.rallied_walker_does_not_flee_when_hit
        // tests: ScriptBindingPropsTest.kit_fearless_answers_the_kit_mark
        // tests: SemFearless.a_banner_is_not_made_fearless_by_its_own_aura
        // tests: SemFearless.a_weapon_without_a_rally_radius_rallies_nobody
        // tests: SemFearless.the_rally_radius_is_inclusive_at_exactly_r
        if (wfd == nullptr || wfd->rally_radius <= 0)
            continue;
        // rule: A walker is fearless (never flees when hit, a fright binding skips it) if it
        //   carries the kit FEARLESS mark, or, with New Specials on, if a live friendly
        //   weapon-order rally object (war banner.
        // tests: KitSeamsSmall.fearless_bit_and_rally_radius_block_the_flee_walk
        // tests: KitCaptain.rallied_walker_does_not_flee_when_hit
        // tests: ScriptBindingPropsTest.kit_fearless_answers_the_kit_mark
        // tests: SemFearless.a_banner_is_not_made_fearless_by_its_own_aura
        // tests: SemFearless.a_weapon_without_a_rally_radius_rallies_nobody
        // tests: SemFearless.the_rally_radius_is_inclusive_at_exactly_r
        if (!w.is_friendly(rally))
            continue;
        const std::int32_t r = wfd->rally_radius;
        // rule: A walker is fearless (never flees when hit, a fright binding skips it) if it
        //   carries the kit FEARLESS mark, or, with New Specials on, if a live friendly
        //   weapon-order rally object (war banner.
        // tests: KitSeamsSmall.fearless_bit_and_rally_radius_block_the_flee_walk
        // tests: KitCaptain.rallied_walker_does_not_flee_when_hit
        // tests: ScriptBindingPropsTest.kit_fearless_answers_the_kit_mark
        // tests: SemFearless.a_banner_is_not_made_fearless_by_its_own_aura
        // tests: SemFearless.a_weapon_without_a_rally_radius_rallies_nobody
        // tests: SemFearless.the_rally_radius_is_inclusive_at_exactly_r
        if (w.distance_to_ob_center(rally) <= r * r)
            return true;
    }
    return false;
}

}  // namespace og::sim
