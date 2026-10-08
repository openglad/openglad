/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// The New Specials Lua bindings: the per-walker kit state accessors, the
// og.C.KIT_* constants and the kit verbs (og.possess and friends), installed
// by one call from the entity-binding install site.
#include "script_internal.h"
#include "script_raise.h"

#include <openglad/gameplay/fearless.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/placement.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <limits>

namespace og::script {

namespace {

walker* kit_self_arg(lua_State* L)
{
    return resolve_walker(L, 1, /*required=*/true);
}

// walker:kit_state() -> integer (the KIT_* bits)
int m_kit_state(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(kit_self_arg(L)->kit_state()));
    return 1;
}

// walker:set_kit_state(n) -- narrows to a byte like every byte setter. A
// change of the HIDDEN bit goes through set_hidden so the collision table
// follows it.
int m_set_kit_state(lua_State* L)
{
    walker* w = kit_self_arg(L);
    const auto value = static_cast<std::uint8_t>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 2)));
    const bool hide = (value & KIT_HIDDEN) != 0;
    if (hide != w->hidden())
        w->set_hidden(hide);
    // set_hidden may turn the hide away (a possessed body never hides), so
    // the HIDDEN bit lands as set_hidden left it; the other bits as asked.
    const std::uint8_t landed = static_cast<std::uint8_t>(
        (value & ~KIT_HIDDEN) | (w->hidden() ? KIT_HIDDEN : 0));
    w->set_kit_state(landed);
    return 0;
}

// walker:hidden() -> boolean
int m_hidden(lua_State* L)
{
    lua_pushboolean(L, kit_self_arg(L)->hidden() ? 1 : 0);
    return 1;
}

// walker:set_hidden(0|1|boolean)
int m_set_hidden(lua_State* L)
{
    walker* w = kit_self_arg(L);
    luaL_checkany(L, 2);
    bool value = false;
    if (lua_isboolean(L, 2)) {
        value = lua_toboolean(L, 2) != 0;
    } else {
        int is_integer = 0;
        const lua_Integer n = lua_tointegerx(L, 2, &is_integer);
        if (is_integer == 0)
            script_raise(L, "walker:set_hidden: expected a boolean or 0|1");
        value = n != 0;
    }
    w->set_hidden(value);
    return 0;
}

// walker:possess_link() -> integer (the partner's entity id, 0 = none)
int m_possess_link(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(kit_self_arg(L)->possess_link()));
    return 1;
}

// walker:possess_ticks() -> integer (the host's countdown, 0 = permanent)
int m_possess_ticks(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(kit_self_arg(L)->possess_ticks()));
    return 1;
}

// walker:last_attacker_id() -> integer (the entity id of whoever last
// damaged this walker, 0 = nobody yet; resolve it with og.find_by_id)
int m_last_attacker_id(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(kit_self_arg(L)->last_attacker_id()));
    return 1;
}

// walker:init_fire() -> boolean. The engine's own attack start: turns
// toward the facing first, refuses while busy, otherwise starts the attack
// row so the weapon leaves at its end (fire() for a walker with no row).
int m_init_fire(lua_State* L)
{
    lua_pushboolean(L, kit_self_arg(L)->init_fire() ? 1 : 0);
    return 1;
}

GameWorld* kit_world(lua_State* L)
{
    if (current_game == nullptr || current_game->world == nullptr)
        script_raise(L, "no active world");
    return current_game->world;
}

// og.possess(rider, host, ticks) -> true | false, reason. The rider (a
// ghost) enters the host for `ticks` ticks (0 = for good); the reason is a
// short HUD refusal when it cannot.
int og_possess(lua_State* L)
{
    GameWorld* world = kit_world(L);
    walker* rider = resolve_walker(L, 1, /*required=*/true);
    walker* host = resolve_walker(L, 2, /*required=*/true);
    const lua_Integer ticks = luaL_checkinteger(L, 3);
    if (ticks < 0 || ticks > std::numeric_limits<std::int16_t>::max())
        script_raise(L, "og.possess: ticks out of range [0, 32767]");
    const og::sim::PossessResult r = og::sim::possess(
        *world, *rider, *host, static_cast<std::int16_t>(ticks));
    lua_pushboolean(L, r.ok ? 1 : 0);
    if (r.ok)
        return 1;
    lua_pushstring(L, r.reason != nullptr ? r.reason : "");
    return 2;
}

// og.release_possession(ent) -> the rider, or nil when `ent` (either side)
// was not in a possession.
int og_release_possession(lua_State* L)
{
    GameWorld* world = kit_world(L);
    walker* w = resolve_walker(L, 1, /*required=*/true);
    push_walker_handle(L, og::sim::release_possession(*world, *w),
                       current_dispatch_gen(L));
    return 1;
}

// og.fearless(ent) -> boolean (og::sim::fearless: the kit mark, or a
// friendly banner's rally aura with the setting on)
int og_fearless(lua_State* L)
{
    kit_world(L);
    walker* w = resolve_walker(L, 1, /*required=*/true);
    lua_pushboolean(L, og::sim::fearless(*w) ? 1 : 0);
    return 1;
}

// og.map_size() -> pixmaxx, pixmaxy, floor_count (every floor shares the
// pixel footprint)
int og_map_size(lua_State* L)
{
    GameWorld* world = kit_world(L);
    lua_pushinteger(L, static_cast<lua_Integer>(world->pixmaxx));
    lua_pushinteger(L, static_cast<lua_Integer>(world->pixmaxy));
    lua_pushinteger(L, static_cast<lua_Integer>(world->floor_count()));
    return 3;
}

// og.line_clear(a, b) -> boolean: no wall cell between the two walkers'
// centres (false across floors). Reads the grid only; no RNG.
int og_line_clear(lua_State* L)
{
    GameWorld* world = kit_world(L);
    walker* first = resolve_walker(L, 1, /*required=*/true);
    walker* second = resolve_walker(L, 2, /*required=*/true);
    lua_pushboolean(L, world->clear_sight_line(first, second) ? 1 : 0);
    return 1;
}

// og.swap_places(a, b) -> boolean: the two walkers trade positions and
// floors (false, nothing moved, for a dead, dormant or hidden walker or the
// same walker twice). Probes nothing, so nothing underfoot is eaten.
int og_swap_places(lua_State* L)
{
    GameWorld* world = kit_world(L);
    walker* first = resolve_walker(L, 1, /*required=*/true);
    walker* second = resolve_walker(L, 2, /*required=*/true);
    lua_pushboolean(L, og::sim::swap_places(*world, *first, *second) ? 1 : 0);
    return 1;
}

// og.rejoin_obmap(ent): put a walker that a kit took out of the collision
// table back in at its current spot (never a dead, dormant, hidden or
// ignored one).
int og_rejoin_obmap(lua_State* L)
{
    GameWorld* world = kit_world(L);
    walker* w = resolve_walker(L, 1, /*required=*/true);
    og::sim::ensure_obmap_registration(*world, w);
    return 0;
}

const luaL_Reg kKitWalkerMethods[] = {
    {"kit_state", m_kit_state},
    {"set_kit_state", m_set_kit_state},
    {"hidden", m_hidden},
    {"set_hidden", m_set_hidden},
    {"possess_link", m_possess_link},
    {"possess_ticks", m_possess_ticks},
    {"last_attacker_id", m_last_attacker_id},
    {"init_fire", m_init_fire},
    {nullptr, nullptr},
};

// og.* New Specials world verbs. Every entry is fenced like the entity
// world API (no world access while a chunk loads).
const luaL_Reg kOgKitFuncs[] = {
    {"possess", og_possess},
    {"release_possession", og_release_possession},
    {"fearless", og_fearless},
    {"map_size", og_map_size},
    {"line_clear", og_line_clear},
    {"swap_places", og_swap_places},
    {"rejoin_obmap", og_rejoin_obmap},
    {nullptr, nullptr},
};

struct KitConst {
    const char* name;
    lua_Integer value;
};

const KitConst kKitConstants[] = {
    {"KIT_HIDDEN", KIT_HIDDEN},
    {"KIT_FEARLESS", KIT_FEARLESS},
    {"KIT_WARD", KIT_WARD},
    {"KIT_CHANNEL", KIT_CHANNEL},
};

}  // namespace

void register_kit_bindings(lua_State* L, VmState* st)
{
    // Walker methods join the shared method table.
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->walker_methods_ref);
    luaL_setfuncs(L, kKitWalkerMethods, 0);
    lua_pop(L, 1);

    // og.* verbs, behind the load-time fence (og table on top).
    luaL_setfuncs(L, kOgKitFuncs, 0);
    for (const luaL_Reg* r = kOgKitFuncs; r->name != nullptr; r++)
        fence_world_entry(L, st, r->name);

    // og.C.KIT_*: og.C exists by now (install_entity_bindings_into_og).
    lua_getfield(L, -1, "C");
    for (const KitConst& c : kKitConstants) {
        lua_pushinteger(L, c.value);
        lua_setfield(L, -2, c.name);
    }
    lua_pop(L, 1);
}

}  // namespace og::script
