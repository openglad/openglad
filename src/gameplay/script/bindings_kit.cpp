/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// The New Specials Lua bindings: the per-walker kit state accessors and the
// og.C.KIT_* constants, installed by one call from the entity-binding
// install site. New Specials verbs (og.possess and friends) are added to
// kOgKitFuncs below, so the entity bindings never need touching for them.
#include "script_internal.h"
#include "script_raise.h"

#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>

namespace og::script {

namespace {

walker* kit_self(lua_State* L)
{
    return resolve_walker(L, 1, /*required=*/true);
}

// walker:kit_state() -> integer (the KIT_* bits)
int m_kit_state(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(kit_self(L)->kit_state()));
    return 1;
}

// walker:set_kit_state(n) -- narrows to a byte like every byte setter. A
// change of the HIDDEN bit goes through set_hidden so the collision table
// follows it.
int m_set_kit_state(lua_State* L)
{
    walker* w = kit_self(L);
    const auto value = static_cast<std::uint8_t>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 2)));
    const bool hide = (value & KIT_HIDDEN) != 0;
    if (hide != w->hidden())
        w->set_hidden(hide);
    w->set_kit_state(value);
    return 0;
}

// walker:hidden() -> boolean
int m_hidden(lua_State* L)
{
    lua_pushboolean(L, kit_self(L)->hidden() ? 1 : 0);
    return 1;
}

// walker:set_hidden(0|1|boolean)
int m_set_hidden(lua_State* L)
{
    walker* w = kit_self(L);
    luaL_checkany(L, 2);
    const bool value = lua_isboolean(L, 2)
                           ? lua_toboolean(L, 2) != 0
                           : luaL_checkinteger(L, 2) != 0;
    w->set_hidden(value);
    return 0;
}

// walker:possess_link() -> integer (the partner's entity id, 0 = none)
int m_possess_link(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(kit_self(L)->possess_link()));
    return 1;
}

// walker:possess_ticks() -> integer (the host's countdown, 0 = permanent)
int m_possess_ticks(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(kit_self(L)->possess_ticks()));
    return 1;
}

const luaL_Reg kKitWalkerMethods[] = {
    {"kit_state", m_kit_state},
    {"set_kit_state", m_set_kit_state},
    {"hidden", m_hidden},
    {"set_hidden", m_set_hidden},
    {"possess_link", m_possess_link},
    {"possess_ticks", m_possess_ticks},
    {nullptr, nullptr},
};

// og.* New Specials world verbs. Every entry is fenced like the entity
// world API (no world access while a chunk loads).
const luaL_Reg kOgKitFuncs[] = {
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
