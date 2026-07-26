/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// og.* entity/world bindings. Every function here routes through the SAME
// C++ member functions the native family code called, so a transliterated
// hook produces the identical mutation/query sequence (design doc §6).
//
// Type discipline (cookbook R2/R3): integer-typed fields push/pull
// lua_Integer with the setter narrowing exactly as the C++ member type
// does; float-typed fields push the exact double widening and setters cast
// lua_Number → float (the transliteration feeds them og.f* results, which
// are doubles carrying exact floats).

#include "script_internal.h"

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/summon.h>
#include <openglad/gameplay/foe_query.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <cstring>
#include <list>
#include <string>

// Walker-level XP wrapper (defined in walker_combat.cpp; no public header).
short exp_from_action(ExpAction action, walker* w, walker* target,
                      short value);

namespace og::script {

namespace {

// ---------------------------------------------------------------------------
// Common argument helpers
// ---------------------------------------------------------------------------

walker* self_arg(lua_State* L)
{
    return resolve_walker(L, 1, /*required=*/true);
}

GameWorld* world_arg(lua_State* L)
{
    if (current_game == nullptr || current_game->world == nullptr)
        luaL_error(L, "no active world");
    return current_game->world;
}

living* living_arg(lua_State* L, int idx = 1)
{
    walker* w = resolve_walker(L, idx, /*required=*/true);
    living* lv = dynamic_cast<living*>(w);
    if (lv == nullptr)
        luaL_error(L, "entity is not a living");
    return lv;
}

guy* guy_arg(lua_State* L, int idx)
{
    // Accepts a guy handle (level_up hook) or a walker handle (w.myguy).
    if (luaL_testudata(L, idx, kGuyMeta) != nullptr)
        return resolve_guy(L, idx, /*required=*/true);
    walker* w = resolve_walker(L, idx, /*required=*/true);
    if (w->myguy == nullptr)
        luaL_error(L, "walker has no guy record");
    return w->myguy;
}

void push_walker_here(lua_State* L, walker* w)
{
    push_walker_handle(L, w, current_dispatch_gen(L));
}

// Push a std::list<walker*> as a Lua array, preserving list order (the
// deterministic oblist scan order the C++ iteration saw).
void push_walker_list(lua_State* L, const std::list<walker*>& list)
{
    lua_createtable(L, static_cast<int>(list.size()), 0);
    lua_Integer i = 1;
    for (walker* w : list) {
        push_walker_here(L, w);
        lua_rawseti(L, -2, i++);
    }
}

Order order_from_string(lua_State* L, int idx)
{
    const char* s = luaL_checkstring(L, idx);
    if (std::strcmp(s, "living") == 0) return Order::Living;
    if (std::strcmp(s, "weapon") == 0) return Order::Weapon;
    if (std::strcmp(s, "treasure") == 0) return Order::Treasure;
    if (std::strcmp(s, "generator") == 0) return Order::Generator;
    if (std::strcmp(s, "fx") == 0 || std::strcmp(s, "effect") == 0)
        return Order::FX;
    luaL_error(L, "unknown order '%s'", s);
    return Order::Living;  // unreachable
}

const std::list<std::unique_ptr<walker>>& list_from_selector(lua_State* L,
                                                             int idx)
{
    GameWorld* world = world_arg(L);
    const char* s = luaL_checkstring(L, idx);
    if (std::strcmp(s, "ob") == 0) return world->oblist;
    if (std::strcmp(s, "weap") == 0) return world->weaplist;
    if (std::strcmp(s, "fx") == 0) return world->fxlist;
    luaL_error(L, "unknown list selector '%s' (ob|weap|fx)", s);
    return world->oblist;  // unreachable
}

// ---------------------------------------------------------------------------
// Walker field accessors (macro-generated)
// ---------------------------------------------------------------------------

// Integer-family getter/setter: CALL is the accessor name; setters narrow
// through the exact C++ parameter type PTYPE.
#define W_GET_INT(NAME)                                        \
    int m_##NAME(lua_State* L)                                 \
    {                                                          \
        lua_pushinteger(L, static_cast<lua_Integer>(           \
                               self_arg(L)->NAME()));          \
        return 1;                                              \
    }
#define W_SET_INT(NAME, PTYPE)                                 \
    int m_set_##NAME(lua_State* L)                             \
    {                                                          \
        walker* w = self_arg(L);                               \
        w->set_##NAME(static_cast<PTYPE>(                      \
            static_cast<std::uint64_t>(luaL_checkinteger(L, 2)))); \
        return 0;                                              \
    }
#define W_GET_FLT(NAME)                                        \
    int m_##NAME(lua_State* L)                                 \
    {                                                          \
        lua_pushnumber(L, static_cast<lua_Number>(             \
                              self_arg(L)->NAME()));           \
        return 1;                                              \
    }
#define W_SET_FLT(NAME)                                        \
    int m_set_##NAME(lua_State* L)                             \
    {                                                          \
        walker* w = self_arg(L);                               \
        w->set_##NAME(static_cast<float>(luaL_checknumber(L, 2))); \
        return 0;                                              \
    }
#define W_GET_BOOL(NAME)                                       \
    int m_##NAME(lua_State* L)                                 \
    {                                                          \
        lua_pushboolean(L, self_arg(L)->NAME() ? 1 : 0);       \
        return 1;                                              \
    }
#define W_SET_BOOL(NAME)                                       \
    int m_set_##NAME(lua_State* L)                             \
    {                                                          \
        walker* w = self_arg(L);                               \
        luaL_checkany(L, 2);                                   \
        w->set_##NAME(lua_toboolean(L, 2) != 0);               \
        return 0;                                              \
    }
#define W_GET_REF(NAME)                                        \
    int m_##NAME(lua_State* L)                                 \
    {                                                          \
        push_walker_here(L, self_arg(L)->NAME());              \
        return 1;                                              \
    }
#define W_SET_REF(NAME)                                        \
    int m_set_##NAME(lua_State* L)                             \
    {                                                          \
        walker* w = self_arg(L);                               \
        w->set_##NAME(resolve_walker_or_nil(L, 2));            \
        return 0;                                              \
    }

W_GET_INT(xpos)
W_GET_INT(ypos)
W_GET_INT(sizex)
W_GET_INT(sizey)
W_GET_INT(sizez)
W_GET_INT(floor)
W_SET_INT(floor, short)
W_GET_FLT(worldx)
W_GET_FLT(worldy)
W_GET_FLT(worldz)
W_GET_INT(team_num)
W_SET_INT(team_num, unsigned char)
W_GET_INT(real_team_num)
W_SET_INT(real_team_num, unsigned char)
W_GET_INT(user)
W_GET_INT(dead)
W_SET_INT(dead, short)
W_GET_INT(death_called)
W_SET_INT(death_called, short)
W_GET_INT(invulnerable_left)
W_SET_INT(invulnerable_left, short)
W_GET_INT(invisibility_left)
W_SET_INT(invisibility_left, short)
W_GET_INT(flight_left)
W_SET_INT(flight_left, short)
W_GET_INT(bonus_rounds)
W_SET_INT(bonus_rounds, short)
W_GET_INT(family)
W_GET_FLT(lastx)
W_SET_FLT(lastx)
W_GET_FLT(lasty)
W_SET_FLT(lasty)
W_GET_FLT(stepsize)
W_SET_FLT(stepsize)
W_GET_INT(curdir)
W_SET_INT(curdir, signed char)
W_GET_FLT(damage)
W_SET_FLT(damage)
W_GET_FLT(fire_frequency)
W_SET_FLT(fire_frequency)
W_GET_FLT(busy)
W_SET_FLT(busy)
W_GET_INT(current_weapon)
W_SET_INT(current_weapon, unsigned short)
W_GET_INT(default_weapon)
W_SET_INT(default_weapon, unsigned short)
W_GET_INT(act_type)
W_GET_INT(ani_type)
W_SET_INT(ani_type, char)
W_GET_INT(cycle)
W_SET_INT(cycle, signed char)
W_GET_INT(drawcycle)
W_GET_INT(current_special)
W_SET_INT(current_special, char)
W_SET_INT(ignore, char)
W_GET_BOOL(in_act)
W_GET_INT(shifter_down)
W_SET_INT(shifter_down, short)
W_GET_INT(skip_exit)
W_SET_INT(skip_exit, short)
W_GET_INT(lifetime)
W_SET_INT(lifetime, std::int32_t)
W_GET_FLT(speed_bonus)
W_SET_FLT(speed_bonus)
W_GET_INT(speed_bonus_left)
W_SET_INT(speed_bonus_left, std::int32_t)
W_GET_INT(charm_left)
W_SET_INT(charm_left, short)
W_GET_INT(weapons_left)
W_SET_INT(weapons_left, short)
W_GET_INT(keys)
W_SET_INT(keys, std::uint32_t)
W_GET_INT(view_all)
W_SET_INT(view_all, short)
W_GET_INT(lineofsight)
W_SET_INT(lineofsight, std::int32_t)
W_GET_BOOL(summoned)
W_SET_BOOL(summoned)
W_GET_BOOL(save_all_protected)
W_SET_BOOL(save_all_protected)

// do_bounce lives on weap.
int m_do_bounce(lua_State* L)
{
    weap* wp = dynamic_cast<weap*>(self_arg(L));
    if (wp == nullptr)
        return luaL_error(L, "do_bounce: entity is not a weapon");
    lua_pushinteger(L, static_cast<lua_Integer>(wp->do_bounce()));
    return 1;
}

int m_set_do_bounce(lua_State* L)
{
    weap* wp = dynamic_cast<weap*>(self_arg(L));
    if (wp == nullptr)
        return luaL_error(L, "set_do_bounce: entity is not a weapon");
    wp->set_do_bounce(static_cast<std::int32_t>(luaL_checkinteger(L, 2)));
    return 0;
}

W_GET_REF(foe)
W_SET_REF(foe)
W_GET_REF(leader)
W_SET_REF(leader)
W_GET_REF(owner)
W_SET_REF(owner)
W_GET_REF(collide_ob)

// ---------------------------------------------------------------------------
// Walker action methods
// ---------------------------------------------------------------------------

int m_order(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(self_arg(L)->query_order()));
    return 1;
}

int m_setxy(lua_State* L)
{
    walker* w = self_arg(L);
    const float x = static_cast<float>(luaL_checknumber(L, 2));
    const float y = static_cast<float>(luaL_checknumber(L, 3));
    lua_pushboolean(L, w->setxy(x, y) ? 1 : 0);
    return 1;
}

int m_setworldxy(lua_State* L)
{
    walker* w = self_arg(L);
    w->setworldxy(static_cast<float>(luaL_checknumber(L, 2)),
                  static_cast<float>(luaL_checknumber(L, 3)));
    return 0;
}

int m_center_on(lua_State* L)
{
    walker* w = self_arg(L);
    walker* other = resolve_walker(L, 2, /*required=*/true);
    w->center_on(other);
    return 0;
}

int m_distance_to_ob(lua_State* L)
{
    walker* w = self_arg(L);
    walker* other = resolve_walker(L, 2, /*required=*/true);
    lua_pushinteger(L,
                    static_cast<lua_Integer>(w->distance_to_ob(other)));
    return 1;
}

int m_distance_to_ob_center(lua_State* L)
{
    walker* w = self_arg(L);
    walker* other = resolve_walker(L, 2, /*required=*/true);
    lua_pushinteger(
        L, static_cast<lua_Integer>(w->distance_to_ob_center(other)));
    return 1;
}

int m_attack(lua_State* L)
{
    walker* w = self_arg(L);
    walker* target = resolve_walker(L, 2, /*required=*/true);
    lua_pushboolean(L, w->attack(target) ? 1 : 0);
    return 1;
}

int m_fire(lua_State* L)
{
    push_walker_here(L, self_arg(L)->fire());
    return 1;
}

int m_special(lua_State* L)
{
    lua_pushboolean(L, self_arg(L)->special() ? 1 : 0);
    return 1;
}

int m_death(lua_State* L)
{
    lua_pushboolean(L, self_arg(L)->death() ? 1 : 0);
    return 1;
}

int m_teleport(lua_State* L)
{
    lua_pushboolean(L, self_arg(L)->teleport() ? 1 : 0);
    return 1;
}

int m_teleport_ranged(lua_State* L)
{
    walker* w = self_arg(L);
    lua_pushboolean(
        L,
        w->teleport_ranged(
            static_cast<std::int32_t>(luaL_checkinteger(L, 2)))
            ? 1
            : 0);
    return 1;
}

int m_find_teleport_target(lua_State* L)
{
    treasure* t = dynamic_cast<treasure*>(self_arg(L));
    if (t == nullptr)
        return luaL_error(L, "find_teleport_target: entity is not a treasure");
    push_walker_here(L, t->find_teleport_target());
    return 1;
}

int m_turn_undead(lua_State* L)
{
    walker* w = self_arg(L);
    const auto range = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    const auto power = static_cast<std::int32_t>(luaL_checkinteger(L, 3));
    lua_pushinteger(L,
                    static_cast<lua_Integer>(w->turn_undead(range, power)));
    return 1;
}

int m_do_summon(lua_State* L)
{
    walker* w = self_arg(L);
    const auto fam = static_cast<char>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 2)));
    const auto life = static_cast<std::int32_t>(luaL_checkinteger(L, 3));
    push_walker_here(L, w->do_summon(fam, life));
    return 1;
}

int m_do_heal_effects(lua_State* L)
{
    walker* w = self_arg(L);
    walker* healer = resolve_walker_or_nil(L, 2);
    walker* target = resolve_walker(L, 3, /*required=*/true);
    const auto amount = static_cast<short>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 4)));
    w->do_heal_effects(healer, target, amount);
    return 0;
}

int m_transform_to(lua_State* L)
{
    walker* w = self_arg(L);
    const Order order = order_from_string(L, 2);
    const auto fam = static_cast<std::int32_t>(luaL_checkinteger(L, 3));
    w->transform_to(order, fam);
    return 0;
}

int m_transfer_stats(lua_State* L)
{
    walker* w = self_arg(L);
    walker* target = resolve_walker(L, 2, /*required=*/true);
    w->transfer_stats(target);
    return 0;
}

int m_spaces_clear(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(self_arg(L)->spaces_clear()));
    return 1;
}

int m_is_friendly(lua_State* L)
{
    walker* w = self_arg(L);
    walker* other = resolve_walker(L, 2, /*required=*/true);
    lua_pushboolean(L, w->is_friendly(other) ? 1 : 0);
    return 1;
}

int m_set_frame(lua_State* L)
{
    walker* w = self_arg(L);
    lua_pushinteger(L, static_cast<lua_Integer>(w->set_frame(static_cast<short>(
                           static_cast<std::uint64_t>(luaL_checkinteger(L, 2))))));
    return 1;
}

int m_animate(lua_State* L)
{
    lua_pushboolean(L, self_arg(L)->animate() ? 1 : 0);
    return 1;
}

int m_collide_check(lua_State* L)
{
    // walker::collide_ob() getter is bound as m_collide_ob; this is
    // walker::collide(ob) — invoke collision handler.
    walker* w = self_arg(L);
    walker* other = resolve_walker(L, 2, /*required=*/true);
    lua_pushboolean(L, w->collide(other) ? 1 : 0);
    return 1;
}

int m_clear_myguy(lua_State* L)
{
    self_arg(L)->clear_myguy();
    return 0;
}

int m_move_myguy_to(lua_State* L)
{
    walker* w = self_arg(L);
    walker* target = resolve_walker(L, 2, /*required=*/true);
    w->move_myguy_to(target);
    return 0;
}

int m_has_guy(lua_State* L)
{
    lua_pushboolean(L, self_arg(L)->myguy != nullptr ? 1 : 0);
    return 1;
}

int m_set_difficulty(lua_State* L)
{
    walker* w = self_arg(L);
    w->set_difficulty(static_cast<std::uint32_t>(luaL_checkinteger(L, 2)));
    return 0;
}

int m_facing(lua_State* L)
{
    walker* w = self_arg(L);
    const auto x = static_cast<short>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 2)));
    const auto y = static_cast<short>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 3)));
    lua_pushinteger(L, static_cast<lua_Integer>(w->facing(x, y)));
    return 1;
}

// ---------------------------------------------------------------------------
// Statistics accessors (flattened onto the walker as s_*)
// ---------------------------------------------------------------------------

statistics* stats_arg(lua_State* L)
{
    statistics* st = self_arg(L)->stats();
    if (st == nullptr)
        luaL_error(L, "entity has no stats");
    return st;
}

#define S_GET_FLT(NAME)                                        \
    int s_##NAME(lua_State* L)                                 \
    {                                                          \
        lua_pushnumber(L, static_cast<lua_Number>(             \
                              stats_arg(L)->NAME()));          \
        return 1;                                              \
    }
#define S_SET_FLT(NAME)                                        \
    int s_set_##NAME(lua_State* L)                             \
    {                                                          \
        stats_arg(L)->set_##NAME(                              \
            static_cast<float>(luaL_checknumber(L, 2)));       \
        return 0;                                              \
    }
#define S_GET_INT(NAME)                                        \
    int s_##NAME(lua_State* L)                                 \
    {                                                          \
        lua_pushinteger(L, static_cast<lua_Integer>(           \
                               stats_arg(L)->NAME()));         \
        return 1;                                              \
    }
#define S_SET_INT(NAME, PTYPE)                                 \
    int s_set_##NAME(lua_State* L)                             \
    {                                                          \
        stats_arg(L)->set_##NAME(static_cast<PTYPE>(           \
            static_cast<std::uint64_t>(luaL_checkinteger(L, 2)))); \
        return 0;                                              \
    }

S_GET_FLT(hitpoints)
S_SET_FLT(hitpoints)
S_GET_FLT(max_hitpoints)
S_SET_FLT(max_hitpoints)
S_GET_FLT(magicpoints)
S_SET_FLT(magicpoints)
S_GET_FLT(max_magicpoints)
S_SET_FLT(max_magicpoints)
S_GET_FLT(armor)
S_SET_FLT(armor)
S_GET_FLT(magic_per_round)
S_SET_FLT(magic_per_round)
S_GET_FLT(heal_per_round)
S_SET_FLT(heal_per_round)
S_GET_INT(level)
S_SET_INT(level, std::int32_t)
S_GET_INT(weapon_cost)
S_SET_INT(weapon_cost, short)
S_GET_INT(frozen_delay)
S_SET_INT(frozen_delay, short)
S_GET_INT(current_distance)
S_SET_INT(current_distance, std::int32_t)
S_GET_INT(last_distance)
S_SET_INT(last_distance, std::uint32_t)
S_GET_INT(max_heal_delay)
S_SET_INT(max_heal_delay, std::int32_t)
S_GET_INT(current_heal_delay)
S_SET_INT(current_heal_delay, std::int32_t)
S_GET_INT(max_magic_delay)
S_SET_INT(max_magic_delay, std::int32_t)
S_GET_INT(current_magic_delay)
S_SET_INT(current_magic_delay, std::int32_t)

// Raw (unmasked) frozen_delay: negatives are the thaw-immunity phase, which
// the masked s_frozen_delay getter hides (orc howl needs the raw value).
int s_frozen_delay_raw(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(
                           stats_arg(L)->frozen_delay_raw()));
    return 1;
}

// Pre-transform family (cleric resurrect restores the corpse's old family).
int s_old_family(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(stats_arg(L)->old_family()));
    return 1;
}

int s_special_cost(lua_State* L)
{
    const int idx = static_cast<int>(luaL_checkinteger(L, 2));
    if (idx < 0 || idx >= NUM_SPECIALS)
        return luaL_error(L, "special_cost index out of range");
    lua_pushinteger(L, static_cast<lua_Integer>(
                           stats_arg(L)->special_cost(idx)));
    return 1;
}

int s_set_special_cost(lua_State* L)
{
    const int idx = static_cast<int>(luaL_checkinteger(L, 2));
    if (idx < 0 || idx >= NUM_SPECIALS)
        return luaL_error(L, "special_cost index out of range");
    stats_arg(L)->set_special_cost(
        idx, static_cast<unsigned short>(
                 static_cast<std::uint64_t>(luaL_checkinteger(L, 3))));
    return 0;
}

int s_query_bit_flags(lua_State* L)
{
    lua_pushboolean(L, stats_arg(L)->query_bit_flags(static_cast<std::int32_t>(
                           luaL_checkinteger(L, 2))) != 0
                           ? 1
                           : 0);
    return 1;
}

int s_set_bit_flags(lua_State* L)
{
    stats_arg(L)->set_bit_flags(
        static_cast<std::int32_t>(luaL_checkinteger(L, 2)),
        static_cast<short>(
            static_cast<std::uint64_t>(luaL_checkinteger(L, 3))));
    return 0;
}

int s_add_command(lua_State* L)
{
    stats_arg(L)->add_command(
        static_cast<std::int32_t>(luaL_checkinteger(L, 2)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 3)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 4)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 5)));
    return 0;
}

int s_force_command(lua_State* L)
{
    stats_arg(L)->force_command(
        static_cast<std::int32_t>(luaL_checkinteger(L, 2)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 3)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 4)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 5)));
    return 0;
}

int s_set_command(lua_State* L)
{
    stats_arg(L)->set_command(
        static_cast<std::int32_t>(luaL_checkinteger(L, 2)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 3)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 4)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 5)));
    return 0;
}

int s_clear_command(lua_State* L)
{
    stats_arg(L)->clear_command();
    return 0;
}

int s_has_commands(lua_State* L)
{
    lua_pushboolean(L, stats_arg(L)->has_commands() ? 1 : 0);
    return 1;
}

int s_forward_blocked(lua_State* L)
{
    lua_pushboolean(L, stats_arg(L)->forward_blocked() ? 1 : 0);
    return 1;
}

int s_name(lua_State* L)
{
    const std::string& n = stats_arg(L)->name;
    lua_pushlstring(L, n.data(), n.size());
    return 1;
}

int s_set_name(lua_State* L)
{
    size_t len = 0;
    const char* s = luaL_checklstring(L, 2, &len);
    stats_arg(L)->name.assign(s, len);
    return 0;
}

int s_controller(lua_State* L)
{
    push_walker_here(L, stats_arg(L)->controller());
    return 1;
}

// walker:s_force_fright(iterations, info1, info2) — statistics::force_fright,
// the ghost-scare fright injection (stats.cpp). NOT interchangeable with
// s_force_command: it MERGES into an existing forced COMMAND_WALK at the
// queue front (runaway-specials §3.2) instead of prepending, so overlapping
// scares cannot stack end-to-end.
int s_force_fright(lua_State* L)
{
    stats_arg(L)->force_fright(
        static_cast<std::int32_t>(luaL_checkinteger(L, 2)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 3)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 4)));
    return 0;
}

// walker:s_do_command() → short — runs the queued command one step
// (statistics::do_command). The s_*_command family only manipulates the
// queue; this is the only binding that executes it.
int s_do_command(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(stats_arg(L)->do_command()));
    return 1;
}

// ---------------------------------------------------------------------------
// Guy accessors (guy handle or walker's myguy; g_*)
// ---------------------------------------------------------------------------

#define G_GET_INT(NAME)                                        \
    int g_##NAME(lua_State* L)                                 \
    {                                                          \
        lua_pushinteger(L, static_cast<lua_Integer>(           \
                               guy_arg(L, 1)->NAME));          \
        return 1;                                              \
    }
#define G_SET_INT(NAME, PTYPE)                                 \
    int g_set_##NAME(lua_State* L)                             \
    {                                                          \
        guy_arg(L, 1)->NAME = static_cast<PTYPE>(              \
            static_cast<std::uint64_t>(luaL_checkinteger(L, 2))); \
        return 0;                                              \
    }

G_GET_INT(strength)
G_SET_INT(strength, short)
G_GET_INT(dexterity)
G_SET_INT(dexterity, short)
G_GET_INT(constitution)
G_SET_INT(constitution, short)
G_GET_INT(intelligence)
G_SET_INT(intelligence, short)
G_GET_INT(armor)
G_SET_INT(armor, short)
G_GET_INT(level)
G_GET_INT(exp)
G_SET_INT(exp, std::uint32_t)
G_GET_INT(total_shots)
G_SET_INT(total_shots, std::int32_t)
G_GET_INT(scen_shots)
G_SET_INT(scen_shots, short)
G_GET_INT(total_hits)
G_SET_INT(total_hits, std::int32_t)
G_GET_INT(scen_hits)
G_SET_INT(scen_hits, short)

int g_name(lua_State* L)
{
    const std::string& n = guy_arg(L, 1)->name;
    lua_pushlstring(L, n.data(), n.size());
    return 1;
}

int g_update_derived_stats(lua_State* L)
{
    guy* g = guy_arg(L, 1);
    walker* w = resolve_walker(L, 2, /*required=*/true);
    g->update_derived_stats(w);
    return 0;
}

// ---------------------------------------------------------------------------
// og.* world API
// ---------------------------------------------------------------------------

int og_add_ob(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const Order order = order_from_string(L, 1);
    const auto fam = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    const bool atstart = lua_toboolean(L, 3) != 0;
    push_walker_here(L, world->add_ob(order, fam, atstart));
    return 1;
}

int og_add_fx_ob(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const Order order = order_from_string(L, 1);
    const auto fam = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    push_walker_here(L, world->add_fx_ob(order, fam));
    return 1;
}

int og_add_weap_ob(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const Order order = order_from_string(L, 1);
    const auto fam = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    push_walker_here(L, world->add_weap_ob(order, fam));
    return 1;
}

int og_summon(lua_State* L)
{
    walker* summoner = resolve_walker(L, 1, /*required=*/true);
    const Order order = order_from_string(L, 2);
    const auto fam = static_cast<std::int32_t>(luaL_checkinteger(L, 3));
    push_walker_here(L, summon_entity(summoner, order, fam));
    return 1;
}

int og_find_near_foe(lua_State* L)
{
    GameWorld* world = world_arg(L);
    walker* w = resolve_walker(L, 1, /*required=*/true);
    push_walker_here(L, world->find_near_foe(w));
    return 1;
}

int og_find_nearest_blood(lua_State* L)
{
    GameWorld* world = world_arg(L);
    walker* w = resolve_walker(L, 1, /*required=*/true);
    push_walker_here(L, world->find_nearest_blood(w));
    return 1;
}

// og.find_foes_in_range(list_sel, range, self) → array, count
int og_find_foes_in_range(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const auto& list = list_from_selector(L, 1);
    const auto range = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    walker* w = resolve_walker(L, 3, /*required=*/true);
    std::int32_t howmany = 0;
    push_walker_list(L, world->find_foes_in_range(list, range, &howmany, w));
    lua_pushinteger(L, howmany);
    return 2;
}

int og_find_friends_in_range(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const auto& list = list_from_selector(L, 1);
    const auto range = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    walker* w = resolve_walker(L, 3, /*required=*/true);
    std::int32_t howmany = 0;
    push_walker_list(L,
                     world->find_friends_in_range(list, range, &howmany, w));
    lua_pushinteger(L, howmany);
    return 2;
}

int og_find_in_range(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const auto& list = list_from_selector(L, 1);
    const auto range = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    walker* w = resolve_walker(L, 3, /*required=*/true);
    std::int32_t howmany = 0;
    push_walker_list(L, world->find_in_range(list, range, &howmany, w));
    lua_pushinteger(L, howmany);
    return 2;
}

int og_find_foe_weapons_in_range(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const auto& list = list_from_selector(L, 1);
    const auto range = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    walker* w = resolve_walker(L, 3, /*required=*/true);
    std::int32_t howmany = 0;
    push_walker_list(
        L, world->find_foe_weapons_in_range(list, range, &howmany, w));
    lua_pushinteger(L, howmany);
    return 2;
}

// og.foes_in_range(self, range) → array (for_each_foe_in_range order)
int og_foes_in_range(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/true);
    const auto range = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    std::list<walker*> found;
    for_each_foe_in_range(w, range, [&](walker* foe) { found.push_back(foe); });
    push_walker_list(L, found);
    return 1;
}

int og_oblist(lua_State* L)
{
    GameWorld* world = world_arg(L);
    lua_newtable(L);
    lua_Integer i = 1;
    for (const auto& uptr : world->oblist) {
        if (uptr == nullptr)
            continue;
        push_walker_here(L, uptr.get());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

int og_query_passable(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    walker* w = resolve_walker(L, 3, /*required=*/true);
    bool ok;
    if (lua_isnoneornil(L, 4))
        ok = world->query_passable(x, y, w);
    else
        ok = world->query_passable(x, y, w,
                                   static_cast<int>(luaL_checkinteger(L, 4)));
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int og_query_grid_passable(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    walker* w = resolve_walker(L, 3, /*required=*/true);
    bool ok;
    if (lua_isnoneornil(L, 4))
        ok = world->query_grid_passable(x, y, w);
    else
        ok = world->query_grid_passable(
            x, y, w, static_cast<int>(luaL_checkinteger(L, 4)));
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

int og_query_object_passable(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    walker* w = resolve_walker(L, 3, /*required=*/true);
    bool ok;
    if (lua_isnoneornil(L, 4))
        ok = world->query_object_passable(x, y, w);
    else
        ok = world->query_object_passable(
            x, y, w, static_cast<int>(luaL_checkinteger(L, 4)));
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// og.cosmetic_rand(n): draw from the parity harness's cosmetic libc-rand
// override when installed (so captured dumps match master's dual-RNG-stream
// behavior without observing the sim rng_state), else the sim RNG — exactly
// the C++ cosmetic_rng_override() pattern (see walker.cpp
// next_path_check_delay / elf spread).
int og_cosmetic_rand(lua_State* L)
{
    const lua_Integer n = luaL_checkinteger(L, 1);
    if (n <= 0)
        return luaL_error(L, "og.cosmetic_rand: n must be positive");
    if (IRandom* cos = cosmetic_rng_override()) {
        lua_pushinteger(L, static_cast<lua_Integer>(
                               cos->next(static_cast<std::uint32_t>(n))));
        return 1;
    }
    GameWorld* world = world_arg(L);
    lua_pushinteger(L, static_cast<lua_Integer>(
                           world->rng_.next(static_cast<std::uint32_t>(n))));
    return 1;
}

int og_level_id(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(world_arg(L)->id));
    return 1;
}

int og_level_tick(lua_State* L)
{
    lua_pushinteger(
        L, static_cast<lua_Integer>(world_arg(L)->level_tick_count()));
    return 1;
}

int og_level_done(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(world_arg(L)->level_done));
    return 1;
}

int og_game_ended(lua_State* L)
{
    lua_pushboolean(L, world_arg(L)->game_ended ? 1 : 0);
    return 1;
}

int og_remaining_foes(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/true);
    lua_pushinteger(L, static_cast<lua_Integer>(
                           world_arg(L)->remaining_foes(w)));
    return 1;
}

int og_enemy_freeze(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(world_arg(L)->enemy_freeze));
    return 1;
}

int og_set_enemy_freeze(lua_State* L)
{
    world_arg(L)->enemy_freeze =
        static_cast<std::int32_t>(luaL_checkinteger(L, 1));
    return 0;
}

// ---------------------------------------------------------------------------
// Sim events
// ---------------------------------------------------------------------------

int og_emit_sound(lua_State* L)
{
    if (current_game == nullptr)
        return luaL_error(L, "no active context");
    og::sim::emit_sound(current_game->sim_events,
                        static_cast<std::uint32_t>(luaL_checkinteger(L, 1)));
    return 0;
}

int og_emit_positional_sound(lua_State* L)
{
    if (current_game == nullptr)
        return luaL_error(L, "no active context");
    walker* w = resolve_walker(L, 1, /*required=*/true);
    og::sim::emit_positional_sound(
        current_game->sim_events, w,
        static_cast<std::uint32_t>(luaL_checkinteger(L, 2)));
    return 0;
}

int og_emit_notification(lua_State* L)
{
    if (current_game == nullptr)
        return luaL_error(L, "no active context");
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    const auto duration =
        static_cast<std::uint32_t>(luaL_optinteger(L, 2, 0));
    og::sim::emit_notification(current_game->sim_events, std::string(s, len),
                               duration);
    return 0;
}

// og.emit_event(kind, a, b) — raw sim event (kinds in og.C.EVENT_*).
int og_emit_event(lua_State* L)
{
    if (current_game == nullptr)
        return luaL_error(L, "no active context");
    og::sim::emit_event(
        current_game->sim_events,
        static_cast<og::sim::EventKind>(luaL_checkinteger(L, 1)),
        static_cast<std::uint32_t>(luaL_optinteger(L, 2, 0)),
        static_cast<std::uint32_t>(luaL_optinteger(L, 3, 0)));
    return 0;
}

int og_my_team(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(world_arg(L)->my_team));
    return 1;
}

// The world's living head-count bookkeeping (NOT derivable from an oblist
// scan: the counter can legitimately drift, e.g. editor map-resize erases
// livings without decrementing — scripts must read the same field the C++
// read).
// og.family_flag("living", family_byte, flag_name) → descriptor boolean.
// Read-only view of living-descriptor flags for scripts that branch on
// another entity's family traits (knife return gate, undead checks).
int og_family_flag(lua_State* L)
{
    const Order order = order_from_string(L, 1);
    const int fam = static_cast<int>(luaL_checkinteger(L, 2));
    const char* flag = luaL_checkstring(L, 3);
    if (order != Order::Living)
        return luaL_error(L, "og.family_flag: only 'living' supported");
    const FamilyDescriptor* fd = get_family_descriptor(fam);
    if (fd == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    bool value;
    if (std::strcmp(flag, "has_returning_weapon") == 0)
        value = fd->has_returning_weapon;
    else if (std::strcmp(flag, "is_undead") == 0)
        value = fd->is_undead;
    else if (std::strcmp(flag, "leaves_bloodspot") == 0)
        value = fd->leaves_bloodspot;
    else if (std::strcmp(flag, "is_stationary") == 0)
        value = fd->is_stationary;
    else
        return luaL_error(L, "og.family_flag: unknown flag '%s'", flag);
    lua_pushboolean(L, value ? 1 : 0);
    return 1;
}

int og_living_count(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(world_arg(L)->living_count));
    return 1;
}

int g_upgrade_to_level(lua_State* L)
{
    guy* g = guy_arg(L, 1);
    const auto new_level = static_cast<short>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 2)));
    const bool set_xp =
        lua_isnoneornil(L, 3) ? true : (lua_toboolean(L, 3) != 0);
    // Re-dispatches the guy's family level_up hook internally (nested VM
    // entry is fine: budgets arm on the outermost call only).
    g->upgrade_to_level(new_level, set_xp);
    return 0;
}

int og_set_palette(lua_State* L)
{
    world_arg(L)->current_palette_id =
        static_cast<std::uint8_t>(luaL_checkinteger(L, 1));
    return 0;
}

// ---------------------------------------------------------------------------
// Combat math + shared family helpers
// ---------------------------------------------------------------------------

int og_soften(lua_State* L)
{
    lua_pushinteger(
        L, static_cast<lua_Integer>(og::combat::soften(
               static_cast<int>(luaL_checkinteger(L, 1)),
               static_cast<int>(luaL_checkinteger(L, 2)),
               static_cast<int>(luaL_checkinteger(L, 3)))));
    return 1;
}

int og_charm_duration(lua_State* L)
{
    GameWorld* world = world_arg(L);
    lua_pushinteger(L, static_cast<lua_Integer>(compute_charm_duration(
                           static_cast<std::int32_t>(luaL_checkinteger(L, 1)),
                           world->rng_)));
    return 1;
}

int og_freeze_duration(lua_State* L)
{
    GameWorld* world = world_arg(L);
    lua_pushinteger(L, static_cast<lua_Integer>(compute_freeze_duration(
                           static_cast<std::int32_t>(luaL_checkinteger(L, 1)),
                           static_cast<std::int32_t>(luaL_checkinteger(L, 2)),
                           world->rng_)));
    return 1;
}

int og_heal_amount(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const HealResult r = compute_heal_amount(
        static_cast<std::int32_t>(luaL_checkinteger(L, 1)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 2)), world->rng_);
    lua_pushinteger(L, static_cast<lua_Integer>(r.amount));
    lua_pushinteger(L, static_cast<lua_Integer>(r.cost));
    return 2;
}

int og_scare_duration(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(og::combat::scare_duration(
                           static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

int og_elemental_lifetime(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(og::combat::elemental_lifetime(
                        static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

int og_image_lifetime(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(og::combat::image_lifetime(
                           static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

int og_entity_display_name(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/true);
    size_t len = 0;
    const char* fallback = luaL_optlstring(L, 2, "", &len);
    const std::string_view name =
        entity_display_name(w, std::string_view(fallback, len));
    lua_pushlstring(L, name.data(), name.size());
    return 1;
}

// og.exp_from_action(self, target_or_nil, action_name, value) — the
// walker-level exp_from_action wrapper the family code calls.
int og_exp_from_action(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/true);
    walker* target = resolve_walker_or_nil(L, 2);
    const char* action_name = luaL_checkstring(L, 3);
    const auto value = static_cast<short>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 4)));
    ExpAction action;
    if (std::strcmp(action_name, "attack") == 0) action = ExpAction::Attack;
    else if (std::strcmp(action_name, "kill") == 0) action = ExpAction::Kill;
    else if (std::strcmp(action_name, "heal") == 0) action = ExpAction::Heal;
    else if (std::strcmp(action_name, "turn_undead") == 0) action = ExpAction::TurnUndead;
    else if (std::strcmp(action_name, "raise_skeleton") == 0) action = ExpAction::RaiseSkeleton;
    else if (std::strcmp(action_name, "raise_ghost") == 0) action = ExpAction::RaiseGhost;
    else if (std::strcmp(action_name, "resurrect") == 0) action = ExpAction::Resurrect;
    else if (std::strcmp(action_name, "resurrect_penalty") == 0) action = ExpAction::ResurrectPenalty;
    else if (std::strcmp(action_name, "protection") == 0) action = ExpAction::Protection;
    else if (std::strcmp(action_name, "eat_corpse") == 0) action = ExpAction::EatCorpse;
    else return luaL_error(L, "og.exp_from_action: unknown action '%s'", action_name);
    lua_pushinteger(L, static_cast<lua_Integer>(
                           exp_from_action(action, w, target, value)));
    return 1;
}

int og_apply_level_up(lua_State* L)
{
    guy* g = guy_arg(L, 1);
    const auto diff = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    const LevelUpGains gains{
        static_cast<std::int32_t>(luaL_checkinteger(L, 3)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 4)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 5)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 6)),
        static_cast<std::int32_t>(luaL_checkinteger(L, 7)),
    };
    apply_level_up(g, diff, gains);
    return 0;
}

int og_apply_difficulty_scaling(lua_State* L)
{
    living* lv = living_arg(L, 1);
    const auto level = static_cast<std::uint32_t>(luaL_checkinteger(L, 2));
    const DifficultyScaling s{
        static_cast<float>(luaL_checknumber(L, 3)),
        static_cast<float>(luaL_checknumber(L, 4)),
        static_cast<float>(luaL_checknumber(L, 5)),
        static_cast<float>(luaL_checknumber(L, 6)),
    };
    apply_difficulty_scaling(lv, level, s);
    return 0;
}

int og_check_special_ai_distance(lua_State* L)
{
    living* lv = living_arg(L, 1);
    const auto threshold =
        static_cast<std::uint32_t>(luaL_checkinteger(L, 2));
    lua_pushboolean(L, check_special_ai_distance(lv, threshold) ? 1 : 0);
    return 1;
}

int og_scare_radius(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(og::combat::scare_radius(
                           static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

// ---------------------------------------------------------------------------
// Animation tables (walker::ani)
// ---------------------------------------------------------------------------

// Sentinel scan bound, identical to walker::animate / weapon_animate_step: a
// row with no -1 terminator inside this many entries is malformed and is
// reported as "no such sequence" rather than read further.
inline constexpr int kAniScanCap = 128;

// ani[row] with the walker ani_count invariant applied: when the loader
// recorded this family's real table length (ani_count > 0) rows past it do
// not exist, so a snapshot/save-driven index cannot walk off the end;
// ani_count == 0 marks a test-built walker that assigned `ani` directly and
// keeps the legacy direct-index behavior. nullptr = no such row.
const signed char* ani_row_ptr(walker* w, lua_Integer row)
{
    if (w->ani == nullptr || row < 0)
        return nullptr;
    if (w->ani_count > 0 && row >= static_cast<lua_Integer>(w->ani_count))
        return nullptr;
    return w->ani[static_cast<std::size_t>(row)];
}

// Frame count before the -1 sentinel; -1 when the row is malformed.
int ani_row_len(const signed char* seq)
{
    int len = 0;
    while (len < kAniScanCap && seq[len] != -1)
        len++;
    return len >= kAniScanCap ? -1 : len;
}

// og.ani_frame(entity, row, index) → frame | nil.
// The single-frame read `self->ani[row][index]` (effect_family_chain.cpp
// does `if (self->ani) self->set_frame(self->ani[curdir()][0])`). nil means
// the C++ `if (self->ani)` guard would have failed — or the row/index does
// not exist — so the transliteration simply skips its set_frame. The
// sentinel slot itself is addressable (index == length) so a legitimately
// empty row reads back the same -1 the C++ would have handed set_frame.
int og_ani_frame(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/true);
    const signed char* seq = ani_row_ptr(w, luaL_checkinteger(L, 2));
    const int len = (seq != nullptr) ? ani_row_len(seq) : -1;
    const lua_Integer index = luaL_checkinteger(L, 3);
    if (len < 0 || index < 0 || index > static_cast<lua_Integer>(len)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(seq[index]));
    return 1;
}

// og.ani_row(entity, row) → { frame, ... } | nil — the whole sequence up to
// (excluding) the -1 sentinel, for the row-walking form
// (weapon_family_animate.cpp's `const signed char* seq = self->ani[i];` then
// iterate to -1). nil covers every case where the C++ bails out early: no
// table, row past ani_count, null row, missing sentinel. An empty table is a
// present-but-zero-length sequence (the C++ `seq_len <= 0` stop).
int og_ani_row(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/true);
    const signed char* seq = ani_row_ptr(w, luaL_checkinteger(L, 2));
    const int len = (seq != nullptr) ? ani_row_len(seq) : -1;
    if (len < 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, len, 0);
    for (int i = 0; i < len; ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(seq[i]));
        lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Score / campaign progression / exit flow
// ---------------------------------------------------------------------------

// og.award_score(team, points) — the treasure families' score credit
// (the file-local award_score in treasure_family_valuables.cpp): bump
// GameWorld::m_score and emit ScoreChange with the same payload. Teams
// outside the score table are silently ignored, exactly like the C++
// is_valid_score_team() guard.
int og_award_score(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const auto team = static_cast<unsigned char>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 1)));
    const auto points = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 2)));
    if (team >= SCORE_TEAM_COUNT)
        return 0;
    world->m_score[team] += points;
    og::sim::emit_event(current_game->sim_events,
                        og::sim::EventKind::ScoreChange,
                        static_cast<std::uint32_t>(team), points);
    return 0;
}

int og_world_can_exit_whenever(lua_State* L)
{
    lua_pushboolean(
        L, (world_arg(L)->type & GameWorld::TYPE_CAN_EXIT_WHENEVER) != 0 ? 1
                                                                        : 0);
    return 1;
}

int og_level_completed(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const auto level = static_cast<int>(luaL_checkinteger(L, 1));
    lua_pushboolean(L, world->completed_levels.count(level) > 0 ? 1 : 0);
    return 1;
}

int og_current_scenario(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(world_arg(L)->current_scenario));
    return 1;
}

// og.set_withdraw_request(level) — the exit pad's withdraw latch
// (treasure_family_navigation.cpp sets both fields together).
int og_set_withdraw_request(lua_State* L)
{
    GameWorld* world = world_arg(L);
    world->withdraw_requested = true;
    world->withdraw_level = static_cast<short>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 1)));
    return 0;
}

// og.emit_exit_confirmation(prompt, dest_level, is_withdraw) —
// EventKind::RequestExitConfirmation with the exit pad's payload
// (a = dest_level, b = 1 for the withdraw prompt).
int og_emit_exit_confirmation(lua_State* L)
{
    if (current_game == nullptr)
        return luaL_error(L, "no active context");
    size_t len = 0;
    const char* prompt = luaL_checklstring(L, 1, &len);
    const auto dest = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(luaL_checkinteger(L, 2)));
    const auto is_withdraw = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(luaL_optinteger(L, 3, 0)));
    og::sim::emit_event_text(current_game->sim_events,
                             og::sim::EventKind::RequestExitConfirmation,
                             std::string(prompt, len), dest, is_withdraw);
    return 0;
}

// og.emit_withdraw_to_level(level) — EventKind::WithdrawToLevel (a = level).
int og_emit_withdraw_to_level(lua_State* L)
{
    if (current_game == nullptr)
        return luaL_error(L, "no active context");
    og::sim::emit_event(
        current_game->sim_events, og::sim::EventKind::WithdrawToLevel,
        static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(luaL_checkinteger(L, 1))),
        0);
    return 0;
}

// og.scenario_title(name) → title string ("none" when unreadable).
// The reader lives in the resources component (og::data::load_scenario_title)
// and gameplay may not depend on it, so it arrives through the
// GameWorld::scenario_title_provider seam the owning layer installs when it
// wires the world's entity services. No provider ⇒ "none", i.e. the same
// answer a failed read gives, so callers keep their existing fallback.
int og_scenario_title(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const char* name = luaL_checkstring(L, 1);
    if (!world->scenario_title_provider) {
        lua_pushliteral(L, "none");
        return 1;
    }
    const std::string title = world->scenario_title_provider(name);
    lua_pushlstring(L, title.data(), title.size());
    return 1;
}

// og.ctf_on_flag_touch(flag, eater) → bool — the whole CTF flag-touch
// operation (og::sim::ctf_on_flag_touch, src/gameplay/ctf/ctf.cpp), wrapped
// as one call exactly like the C++ treasure hook delegates to it. CTF rules
// stay in the CTF engine.
int og_ctf_on_flag_touch(lua_State* L)
{
    walker* flag = resolve_walker(L, 1, /*required=*/true);
    walker* eater = resolve_walker(L, 2, /*required=*/true);
    lua_pushboolean(L, og::sim::ctf_on_flag_touch(flag, eater) ? 1 : 0);
    return 1;
}

// ---------------------------------------------------------------------------
// Registration tables
// ---------------------------------------------------------------------------

const luaL_Reg kWalkerMethods[] = {
    {"xpos", m_xpos}, {"ypos", m_ypos},
    {"sizex", m_sizex}, {"sizey", m_sizey}, {"sizez", m_sizez},
    {"floor", m_floor}, {"set_floor", m_set_floor},
    {"worldx", m_worldx}, {"worldy", m_worldy}, {"worldz", m_worldz},
    {"team_num", m_team_num}, {"set_team_num", m_set_team_num},
    {"real_team_num", m_real_team_num},
    {"set_real_team_num", m_set_real_team_num},
    {"user", m_user},
    {"dead", m_dead}, {"set_dead", m_set_dead},
    {"death_called", m_death_called},
    {"set_death_called", m_set_death_called},
    {"invulnerable_left", m_invulnerable_left},
    {"set_invulnerable_left", m_set_invulnerable_left},
    {"invisibility_left", m_invisibility_left},
    {"set_invisibility_left", m_set_invisibility_left},
    {"flight_left", m_flight_left}, {"set_flight_left", m_set_flight_left},
    {"bonus_rounds", m_bonus_rounds},
    {"set_bonus_rounds", m_set_bonus_rounds},
    {"family", m_family}, {"order", m_order},
    {"lastx", m_lastx}, {"set_lastx", m_set_lastx},
    {"lasty", m_lasty}, {"set_lasty", m_set_lasty},
    {"stepsize", m_stepsize}, {"set_stepsize", m_set_stepsize},
    {"curdir", m_curdir}, {"set_curdir", m_set_curdir},
    {"damage", m_damage}, {"set_damage", m_set_damage},
    {"fire_frequency", m_fire_frequency},
    {"set_fire_frequency", m_set_fire_frequency},
    {"busy", m_busy}, {"set_busy", m_set_busy},
    {"current_weapon", m_current_weapon},
    {"set_current_weapon", m_set_current_weapon},
    {"default_weapon", m_default_weapon},
    {"set_default_weapon", m_set_default_weapon},
    {"act_type", m_act_type},
    {"ani_type", m_ani_type}, {"set_ani_type", m_set_ani_type},
    {"cycle", m_cycle}, {"set_cycle", m_set_cycle},
    {"drawcycle", m_drawcycle},
    {"current_special", m_current_special},
    {"set_current_special", m_set_current_special},
    {"set_ignore", m_set_ignore},
    {"in_act", m_in_act},
    {"shifter_down", m_shifter_down},
    {"set_shifter_down", m_set_shifter_down},
    {"skip_exit", m_skip_exit}, {"set_skip_exit", m_set_skip_exit},
    {"lifetime", m_lifetime}, {"set_lifetime", m_set_lifetime},
    {"speed_bonus", m_speed_bonus}, {"set_speed_bonus", m_set_speed_bonus},
    {"speed_bonus_left", m_speed_bonus_left},
    {"set_speed_bonus_left", m_set_speed_bonus_left},
    {"charm_left", m_charm_left}, {"set_charm_left", m_set_charm_left},
    {"weapons_left", m_weapons_left},
    {"set_weapons_left", m_set_weapons_left},
    {"keys", m_keys}, {"set_keys", m_set_keys},
    {"view_all", m_view_all}, {"set_view_all", m_set_view_all},
    {"lineofsight", m_lineofsight},
    {"set_lineofsight", m_set_lineofsight},
    {"summoned", m_summoned}, {"set_summoned", m_set_summoned},
    {"save_all_protected", m_save_all_protected},
    {"set_save_all_protected", m_set_save_all_protected},
    {"do_bounce", m_do_bounce}, {"set_do_bounce", m_set_do_bounce},
    {"foe", m_foe}, {"set_foe", m_set_foe},
    {"leader", m_leader}, {"set_leader", m_set_leader},
    {"owner", m_owner}, {"set_owner", m_set_owner},
    {"collide_ob", m_collide_ob},
    {"setxy", m_setxy}, {"setworldxy", m_setworldxy},
    {"center_on", m_center_on},
    {"distance_to_ob", m_distance_to_ob},
    {"distance_to_ob_center", m_distance_to_ob_center},
    {"attack", m_attack}, {"fire", m_fire}, {"special", m_special},
    {"death", m_death},
    {"teleport", m_teleport}, {"teleport_ranged", m_teleport_ranged},
    {"find_teleport_target", m_find_teleport_target},
    {"turn_undead", m_turn_undead},
    {"do_summon", m_do_summon},
    {"do_heal_effects", m_do_heal_effects},
    {"transform_to", m_transform_to},
    {"transfer_stats", m_transfer_stats},
    {"spaces_clear", m_spaces_clear},
    {"is_friendly", m_is_friendly},
    {"set_frame", m_set_frame},
    {"animate", m_animate},
    {"collide", m_collide_check},
    {"clear_myguy", m_clear_myguy},
    {"move_myguy_to", m_move_myguy_to},
    {"has_guy", m_has_guy},
    {"set_difficulty", m_set_difficulty},
    {"facing", m_facing},
    // statistics (flattened, s_ prefix)
    {"s_hitpoints", s_hitpoints}, {"s_set_hitpoints", s_set_hitpoints},
    {"s_max_hitpoints", s_max_hitpoints},
    {"s_set_max_hitpoints", s_set_max_hitpoints},
    {"s_magicpoints", s_magicpoints},
    {"s_set_magicpoints", s_set_magicpoints},
    {"s_max_magicpoints", s_max_magicpoints},
    {"s_set_max_magicpoints", s_set_max_magicpoints},
    {"s_armor", s_armor}, {"s_set_armor", s_set_armor},
    {"s_magic_per_round", s_magic_per_round},
    {"s_set_magic_per_round", s_set_magic_per_round},
    {"s_heal_per_round", s_heal_per_round},
    {"s_set_heal_per_round", s_set_heal_per_round},
    {"s_level", s_level}, {"s_set_level", s_set_level},
    {"s_weapon_cost", s_weapon_cost},
    {"s_set_weapon_cost", s_set_weapon_cost},
    {"s_frozen_delay", s_frozen_delay},
    {"s_set_frozen_delay", s_set_frozen_delay},
    {"s_frozen_delay_raw", s_frozen_delay_raw},
    {"s_old_family", s_old_family},
    {"s_current_distance", s_current_distance},
    {"s_set_current_distance", s_set_current_distance},
    {"s_last_distance", s_last_distance},
    {"s_set_last_distance", s_set_last_distance},
    {"s_max_heal_delay", s_max_heal_delay},
    {"s_set_max_heal_delay", s_set_max_heal_delay},
    {"s_current_heal_delay", s_current_heal_delay},
    {"s_set_current_heal_delay", s_set_current_heal_delay},
    {"s_max_magic_delay", s_max_magic_delay},
    {"s_set_max_magic_delay", s_set_max_magic_delay},
    {"s_current_magic_delay", s_current_magic_delay},
    {"s_set_current_magic_delay", s_set_current_magic_delay},
    {"s_special_cost", s_special_cost},
    {"s_set_special_cost", s_set_special_cost},
    {"s_query_bit_flags", s_query_bit_flags},
    {"s_set_bit_flags", s_set_bit_flags},
    {"s_add_command", s_add_command},
    {"s_force_command", s_force_command},
    {"s_set_command", s_set_command},
    {"s_clear_command", s_clear_command},
    {"s_has_commands", s_has_commands},
    {"s_forward_blocked", s_forward_blocked},
    {"s_force_fright", s_force_fright},
    {"s_do_command", s_do_command},
    {"s_name", s_name}, {"s_set_name", s_set_name},
    {"s_controller", s_controller},
    // guy (g_ prefix; walker must have myguy)
    {"g_strength", g_strength}, {"g_set_strength", g_set_strength},
    {"g_dexterity", g_dexterity}, {"g_set_dexterity", g_set_dexterity},
    {"g_constitution", g_constitution},
    {"g_set_constitution", g_set_constitution},
    {"g_intelligence", g_intelligence},
    {"g_set_intelligence", g_set_intelligence},
    {"g_armor", g_armor}, {"g_set_armor", g_set_armor},
    {"g_level", g_level}, {"g_exp", g_exp}, {"g_set_exp", g_set_exp},
    {"g_total_shots", g_total_shots},
    {"g_set_total_shots", g_set_total_shots},
    {"g_scen_shots", g_scen_shots},
    {"g_set_scen_shots", g_set_scen_shots},
    {"g_total_hits", g_total_hits},
    {"g_set_total_hits", g_set_total_hits},
    {"g_scen_hits", g_scen_hits},
    {"g_set_scen_hits", g_set_scen_hits},
    {"g_name", g_name},
    {"g_update_derived_stats", g_update_derived_stats},
    {"g_upgrade_to_level", g_upgrade_to_level},
    {nullptr, nullptr},
};

const luaL_Reg kGuyMethods[] = {
    {"g_strength", g_strength}, {"g_set_strength", g_set_strength},
    {"g_dexterity", g_dexterity}, {"g_set_dexterity", g_set_dexterity},
    {"g_constitution", g_constitution},
    {"g_set_constitution", g_set_constitution},
    {"g_intelligence", g_intelligence},
    {"g_set_intelligence", g_set_intelligence},
    {"g_armor", g_armor}, {"g_set_armor", g_set_armor},
    {"g_level", g_level}, {"g_exp", g_exp},
    {"g_name", g_name},
    {nullptr, nullptr},
};

const luaL_Reg kOgWorldFuncs[] = {
    {"add_ob", og_add_ob},
    {"add_fx_ob", og_add_fx_ob},
    {"add_weap_ob", og_add_weap_ob},
    {"summon", og_summon},
    {"find_near_foe", og_find_near_foe},
    {"find_nearest_blood", og_find_nearest_blood},
    {"find_foes_in_range", og_find_foes_in_range},
    {"find_friends_in_range", og_find_friends_in_range},
    {"find_in_range", og_find_in_range},
    {"find_foe_weapons_in_range", og_find_foe_weapons_in_range},
    {"foes_in_range", og_foes_in_range},
    {"oblist", og_oblist},
    {"query_passable", og_query_passable},
    {"query_grid_passable", og_query_grid_passable},
    {"query_object_passable", og_query_object_passable},
    {"cosmetic_rand", og_cosmetic_rand},
    {"level_id", og_level_id},
    {"level_tick", og_level_tick},
    {"level_done", og_level_done},
    {"game_ended", og_game_ended},
    {"remaining_foes", og_remaining_foes},
    {"enemy_freeze", og_enemy_freeze},
    {"set_enemy_freeze", og_set_enemy_freeze},
    {"emit_sound", og_emit_sound},
    {"emit_positional_sound", og_emit_positional_sound},
    {"emit_notification", og_emit_notification},
    {"emit_event", og_emit_event},
    {"my_team", og_my_team},
    {"set_palette", og_set_palette},
    {"living_count", og_living_count},
    {"family_flag", og_family_flag},
    {"soften", og_soften},
    {"charm_duration", og_charm_duration},
    {"freeze_duration", og_freeze_duration},
    {"heal_amount", og_heal_amount},
    {"scare_duration", og_scare_duration},
    {"scare_radius", og_scare_radius},
    {"elemental_lifetime", og_elemental_lifetime},
    {"image_lifetime", og_image_lifetime},
    {"entity_display_name", og_entity_display_name},
    {"exp_from_action", og_exp_from_action},
    {"apply_level_up", og_apply_level_up},
    {"apply_difficulty_scaling", og_apply_difficulty_scaling},
    {"check_special_ai_distance", og_check_special_ai_distance},
    {"ani_frame", og_ani_frame},
    {"ani_row", og_ani_row},
    {"award_score", og_award_score},
    {"world_can_exit_whenever", og_world_can_exit_whenever},
    {"level_completed", og_level_completed},
    {"current_scenario", og_current_scenario},
    {"set_withdraw_request", og_set_withdraw_request},
    {"emit_exit_confirmation", og_emit_exit_confirmation},
    {"emit_withdraw_to_level", og_emit_withdraw_to_level},
    {"scenario_title", og_scenario_title},
    // Both spellings resolve to the same wrapper: ctf_on_flag_touch matches
    // the C++ entry point it delegates to, ctf_flag_touch the og.* verb
    // shape used elsewhere in this table.
    {"ctf_on_flag_touch", og_ctf_on_flag_touch},
    {"ctf_flag_touch", og_ctf_on_flag_touch},
    {nullptr, nullptr},
};

// Engine constants exposed as og.C.* — built from the real headers so the
// compiler keeps them in sync.
struct NamedConst {
    const char* name;
    lua_Integer value;
};

const NamedConst kConstants[] = {
    // Orders (for walker:order() comparisons)
    {"ORDER_LIVING", static_cast<lua_Integer>(Order::Living)},
    {"ORDER_WEAPON", static_cast<lua_Integer>(Order::Weapon)},
    {"ORDER_TREASURE", static_cast<lua_Integer>(Order::Treasure)},
    {"ORDER_GENERATOR", static_cast<lua_Integer>(Order::Generator)},
    {"ORDER_FX", static_cast<lua_Integer>(Order::FX)},
    // Commands
    {"COMMAND_WALK", COMMAND_WALK},
    {"COMMAND_FIRE", COMMAND_FIRE},
    {"COMMAND_RANDOM_WALK", COMMAND_RANDOM_WALK},
    {"COMMAND_DIE", COMMAND_DIE},
    {"COMMAND_FOLLOW", COMMAND_FOLLOW},
    {"COMMAND_RUSH", COMMAND_RUSH},
    {"COMMAND_MULTIDO", COMMAND_MULTIDO},
    {"COMMAND_QUICK_FIRE", COMMAND_QUICK_FIRE},
    {"COMMAND_SET_WEAPON", COMMAND_SET_WEAPON},
    {"COMMAND_RESET_WEAPON", COMMAND_RESET_WEAPON},
    {"COMMAND_SEARCH", COMMAND_SEARCH},
    {"COMMAND_ATTACK", COMMAND_ATTACK},
    {"COMMAND_RIGHT_WALK", COMMAND_RIGHT_WALK},
    {"COMMAND_UNCHARM", COMMAND_UNCHARM},
    // Bit flags
    {"BIT_FLYING", BIT_FLYING},
    {"BIT_SWIMMING", BIT_SWIMMING},
    {"BIT_ANIMATE", BIT_ANIMATE},
    {"BIT_INVINCIBLE", BIT_INVINCIBLE},
    {"BIT_NO_RANGED", BIT_NO_RANGED},
    {"BIT_IMMORTAL", BIT_IMMORTAL},
    {"BIT_NO_COLLIDE", BIT_NO_COLLIDE},
    {"BIT_PHANTOM", BIT_PHANTOM},
    {"BIT_NAMED", BIT_NAMED},
    {"BIT_FORESTWALK", BIT_FORESTWALK},
    {"BIT_MAGICAL", BIT_MAGICAL},
    {"BIT_FIRE", BIT_FIRE},
    {"BIT_ETHEREAL", BIT_ETHEREAL},
    // Animation types
    {"ANI_WALK", ANI_WALK},
    {"ANI_ATTACK", ANI_ATTACK},
    {"ANI_TELE_OUT", ANI_TELE_OUT},
    {"ANI_TELE_IN", ANI_TELE_IN},
    {"ANI_SKEL_GROW", ANI_SKEL_GROW},
    {"ANI_SLIME_SPLIT", ANI_SLIME_SPLIT},
    {"ANI_EXPLODE", ANI_EXPLODE},
    {"ANI_GROW", ANI_GROW},
    {"ANI_GLOWGROW", ANI_GLOWGROW},
    {"ANI_GLOWPULSE", ANI_GLOWPULSE},
    {"ANI_EXPAND_8", ANI_EXPAND_8},
    {"ANI_DOOR_OPEN", ANI_DOOR_OPEN},
    {"ANI_SCARE", ANI_SCARE},
    {"ANI_BOMB", ANI_BOMB},
    {"ANI_SPIN", ANI_SPIN},
    {"GRID_SIZE", GRID_SIZE},
    // Act types
    {"ACT_RANDOM", ACT_RANDOM},
    {"ACT_FIRE", ACT_FIRE},
    {"ACT_CONTROL", ACT_CONTROL},
    {"ACT_GUARD", ACT_GUARD},
    {"ACT_GENERATE", ACT_GENERATE},
    {"ACT_DIE", ACT_DIE},
    {"ACT_SIT", ACT_SIT},
    // Sounds
    {"SOUND_BOW", SOUND_BOW},
    {"SOUND_CLANG", SOUND_CLANG},
    {"SOUND_DIE1", SOUND_DIE1},
    {"SOUND_BLAST", SOUND_BLAST},
    {"SOUND_SPARKLE", SOUND_SPARKLE},
    {"SOUND_TELEPORT", SOUND_TELEPORT},
    {"SOUND_YO", SOUND_YO},
    {"SOUND_BOLT", SOUND_BOLT},
    {"SOUND_HEAL", SOUND_HEAL},
    {"SOUND_CHARGE", SOUND_CHARGE},
    {"SOUND_FWIP", SOUND_FWIP},
    {"SOUND_EXPLODE", SOUND_EXPLODE},
    {"SOUND_DIE2", SOUND_DIE2},
    {"SOUND_ROAR", SOUND_ROAR},
    {"SOUND_MONEY", SOUND_MONEY},
    {"SOUND_EAT", SOUND_EAT},
    // Sim event kinds (og.emit_event)
    {"EVENT_PLAY_SOUND",
     static_cast<lua_Integer>(og::sim::EventKind::PlaySound)},
    {"EVENT_NOTIFICATION",
     static_cast<lua_Integer>(og::sim::EventKind::Notification)},
    {"EVENT_SET_PALETTE",
     static_cast<lua_Integer>(og::sim::EventKind::SetPalette)},
    {"EVENT_REQUEST_REDRAW",
     static_cast<lua_Integer>(og::sim::EventKind::RequestRedraw)},
    {"EVENT_DAMAGE_TILE",
     static_cast<lua_Integer>(og::sim::EventKind::DamageTile)},
    // Combat caps used by family code
    {"SHOT_DRAIN_CAP", og::combat::kShotDrainCap},
    {"MP_POOL_DAMAGE_CAP", og::combat::kMpPoolDamageCap},
    {"ENEMY_FREEZE_BANK_CAP", og::combat::kEnemyFreezeBankCap},
    // Misc
    {"NUM_SPECIALS", NUM_SPECIALS},
    {"MAXOBS", MAXOBS},
};

}  // namespace

void install_entity_bindings(lua_State* L, VmState* st)
{
    // Walker method table (metatable __index created by
    // ensure_handle_metatables; fill it here).
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->walker_methods_ref);
    luaL_setfuncs(L, kWalkerMethods, 0);
    lua_pop(L, 1);

    // Guy handle methods.
    luaL_getmetatable(L, kGuyMeta);
    lua_newtable(L);
    luaL_setfuncs(L, kGuyMethods, 0);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

void install_entity_bindings_into_og(lua_State* L)
{
    // Expects the og table at the top of the stack.
    luaL_setfuncs(L, kOgWorldFuncs, 0);
    lua_newtable(L);
    for (const NamedConst& c : kConstants) {
        lua_pushinteger(L, c.value);
        lua_setfield(L, -2, c.name);
    }
    lua_setfield(L, -2, "C");
}

}  // namespace og::script
