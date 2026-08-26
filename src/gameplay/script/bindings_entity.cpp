/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// og.* entity/world bindings. Every function routes through the same C++
// member functions as native simulation code, preserving the documented
// mutation/query sequence (design doc §6).
//
// Type discipline (cookbook R2/R3): integer-typed fields push/pull
// lua_Integer with the setter narrowing exactly as the C++ member type
// does; float-typed fields push the exact double widening and setters cast
// lua_Number → float (og.f* results are doubles carrying exact floats).

#include "script_internal.h"

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/sound_ids.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/summon.h>
#include <openglad/gameplay/foe_query.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

// The widest bound the RNG bindings accept. Every draw lands in
// IRandom::next(std::uint32_t), so a Lua 64-bit n above this used to reach
// the generator as n mod 2^32: a silently different distribution, and
// exactly 2^32 arrived as next(0), which answers 0 WITHOUT advancing the
// stream. A wrapped bound is a desync in a deterministic sim — worse, one
// that can differ with the width of whatever the truncation lands in — so
// an over-wide n is a script error, never a clamp. Within [1, kMaxRandBound]
// the conversion is exact and the draw is byte-for-byte what it always was.
// (world_scripts.cpp keeps its own copy for og.rand; script_internal.h has
// no place for a value shared by exactly these two translation units.)
constexpr lua_Integer kMaxRandBound = 2147483647;  // INT32_MAX

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

// Defined with the statistics accessors below; the fused verbs above that
// section use it too.
statistics* stats_arg(lua_State* L);

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
// Read-only on purpose: walker::set_dormant maintains obmap membership, so
// waking or sleeping a walker is an engine decision, never a script's.
W_GET_BOOL(dormant)

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

// walker:heal_clamped(amount[, source]) — fused self-heal with fixed,
// parity-sensitive ordering. Operation sequence:
//   (1) s_set_hitpoints(og.fadd(s_hitpoints(), amount))   float add
//   (2) do_heal_effects(source, self, og.i16(amount))     source may be nil
//   (3) if s_hitpoints() > s_max_hitpoints():  clamp to max
// `amount` is an integer (callers pass level products):
// the heal-marker amount narrows through int16 exactly as og.i16 plus the
// short parameter did, while the add receives the full value widened
// number→float, matching the og.fadd operand path bit for bit.
// The clamp completes before the call returns; caller-side experience and
// notification work therefore cannot observe an intermediate hitpoint value.
int m_heal_clamped(lua_State* L)
{
    walker* w = self_arg(L);
    statistics* st = stats_arg(L);  // same guard text as every s_* accessor
    const lua_Integer amount = luaL_checkinteger(L, 2);
    walker* source = resolve_walker_or_nil(L, 3);
    world_arg(L);  // do_heal_effects stamps the world tick; fail loudly first
    st->set_hitpoints(st->hitpoints() +
                      static_cast<float>(static_cast<lua_Number>(amount)));
    w->do_heal_effects(source, w,
                       static_cast<short>(static_cast<std::uint64_t>(amount)));
    if (st->hitpoints() > st->max_hitpoints())
        st->set_hitpoints(st->max_hitpoints());
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

// walker:s_front_command() — the front queue entry's commandtype, 0 when the
// queue is empty. Mode directors read it to apply the preemption discipline
// (only preempt an idle/SEARCH/GOTO/FOLLOW front) and the refresh-in-place
// idiom the CTF director used.
int s_front_command(lua_State* L)
{
    statistics* stats = stats_arg(L);
    if (!stats->has_commands()) {
        lua_pushinteger(L, 0);
        return 1;
    }
    lua_pushinteger(L,
                    static_cast<lua_Integer>(stats->commands.front().commandtype));
    return 1;
}

// walker:s_refresh_front(iterations, com1, com2) — rewrite the FRONT queue
// entry's lease and payload in place, never touching its commandtype and
// never growing the queue. This is the C++ CTF director's refresh-in-place
// idiom (issue_front_command, ctf_ai.cpp): a mode director that re-issues
// its role order every cadence calls this when s_front_command() already
// answers the wanted type, and s_force_command otherwise. Errors on an
// empty queue — the caller must have read a nonzero s_front_command().
int s_refresh_front(lua_State* L)
{
    statistics* stats = stats_arg(L);
    if (!stats->has_commands())
        return luaL_error(L, "s_refresh_front: command queue is empty");
    command& front = stats->commands.front();
    front.commandcount = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    front.com1 = static_cast<std::int32_t>(luaL_checkinteger(L, 3));
    front.com2 = static_cast<std::int32_t>(luaL_checkinteger(L, 4));
    return 0;
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

// og.summon_configured(self, order, family, {ani_type=, lifetime=, hp_add=,
// max_hp_from_hp=, damage_add=}) → handle | nil — summon plus setters in
// fixed, parity-sensitive order. Each option uses the named binding's cast
// chain:
//   (1) ani_type    → set_ani_type(og.i8-style char narrowing)
//   (2) lifetime    → set_lifetime(int32)
//   (3) hp_add      → s_set_hitpoints(og.fadd(s_hitpoints(), hp_add))
//   (4) max_hp_from_hp (truthy) → s_set_max_hitpoints(s_hitpoints())
//   (5) damage_add  → set_damage(og.fadd(damage(), damage_add))
// Absent keys apply nothing. Every option is read and type-checked BEFORE
// summon_entity runs and an unknown key is an error (checked by comparing
// the table's entry count with the recognized keys — order-independent, so
// the message is identical on every peer): R9 wants the failure before the
// first sim mutation, and a silently ignored typo'd key would be a tuning
// bug nobody sees. A failed summon returns nil exactly like og.summon, with
// no options applied.
int og_summon_configured(lua_State* L)
{
    walker* summoner = resolve_walker(L, 1, /*required=*/true);
    const Order order = order_from_string(L, 2);
    const auto fam = static_cast<std::int32_t>(luaL_checkinteger(L, 3));
    luaL_checktype(L, 4, LUA_TTABLE);
    world_arg(L);  // summon_entity dereferences the world; fail loudly first

    bool has_ani_type = false;
    bool has_lifetime = false;
    bool has_hp_add = false;
    bool has_max_hp_from_hp = false;
    bool max_hp_from_hp = false;
    bool has_damage_add = false;
    lua_Integer ani_type = 0;
    lua_Integer lifetime = 0;
    lua_Number hp_add = 0.0;
    lua_Number damage_add = 0.0;
    int recognized = 0;
    int isnum = 0;

    if (lua_getfield(L, 4, "ani_type") != LUA_TNIL) {
        ani_type = lua_tointegerx(L, -1, &isnum);
        if (isnum == 0)
            return luaL_error(
                L, "og.summon_configured: 'ani_type' must be an integer");
        has_ani_type = true;
        recognized++;
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 4, "lifetime") != LUA_TNIL) {
        lifetime = lua_tointegerx(L, -1, &isnum);
        if (isnum == 0)
            return luaL_error(
                L, "og.summon_configured: 'lifetime' must be an integer");
        has_lifetime = true;
        recognized++;
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 4, "hp_add") != LUA_TNIL) {
        hp_add = lua_tonumberx(L, -1, &isnum);
        if (isnum == 0)
            return luaL_error(
                L, "og.summon_configured: 'hp_add' must be a number");
        has_hp_add = true;
        recognized++;
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 4, "max_hp_from_hp") != LUA_TNIL) {
        max_hp_from_hp = lua_toboolean(L, -1) != 0;
        has_max_hp_from_hp = true;
        recognized++;
    }
    lua_pop(L, 1);
    if (lua_getfield(L, 4, "damage_add") != LUA_TNIL) {
        damage_add = lua_tonumberx(L, -1, &isnum);
        if (isnum == 0)
            return luaL_error(
                L, "og.summon_configured: 'damage_add' must be a number");
        has_damage_add = true;
        recognized++;
    }
    lua_pop(L, 1);
    (void)has_max_hp_from_hp;

    int total = 0;
    lua_pushnil(L);
    while (lua_next(L, 4) != 0) {
        lua_pop(L, 1);  // value; the key stays for the next lua_next
        total++;
    }
    if (total != recognized)
        return luaL_error(L,
                          "og.summon_configured: unknown option key "
                          "(allowed: ani_type, lifetime, hp_add, "
                          "max_hp_from_hp, damage_add)");

    walker* w = summon_entity(summoner, order, fam);
    if (w == nullptr) {
        lua_pushnil(L);
        return 1;
    }
    if (has_ani_type)
        w->set_ani_type(
            static_cast<char>(static_cast<std::uint64_t>(ani_type)));
    if (has_lifetime)
        w->set_lifetime(static_cast<std::int32_t>(lifetime));
    statistics* st = w->stats();  // never null: walker constructs its stats
    if (has_hp_add)
        st->set_hitpoints(st->hitpoints() + static_cast<float>(hp_add));
    if (max_hp_from_hp)
        st->set_max_hitpoints(st->hitpoints());
    if (has_damage_add)
        w->set_damage(w->damage() + static_cast<float>(damage_add));
    push_walker_here(L, w);
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

// og.query_genre(tile_x, tile_y [, floor]) → int — the smoother's terrain
// genre for a TILE coordinate (the passable queries above all take pixels;
// this one does not, because smoother::query_genre_x_y does not). Compare
// against og.C.TYPE_*. The core door's orientation check is the only sim
// caller that reaches terrain by genre rather than by passability; see
// packs/core/lib/weapon_door.lua. query_genre_x_y is total —
// out-of-range tiles report TYPE_GRASS — so there is no nil case.
int og_query_genre(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const auto tx = static_cast<std::int32_t>(luaL_checkinteger(L, 1));
    const auto ty = static_cast<std::int32_t>(luaL_checkinteger(L, 2));
    smoother& sm =
        lua_isnoneornil(L, 3)
            ? world->mysmoother
            : world->smoother_for_floor(
                  static_cast<int>(luaL_checkinteger(L, 3)));
    lua_pushinteger(L, static_cast<lua_Integer>(sm.query_genre_x_y(tx, ty)));
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
    if (n > kMaxRandBound)
        return luaL_error(L,
                          "og.cosmetic_rand: n out of range [1, 2147483647]");
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

// og.emit_notification(text[, duration[, target]]) — target is either a walker
// (addressed to whoever controls it) or a global player index; nil/omitted or
// any index outside [0, kMaxGlobalPlayers) broadcasts, as it always did. The
// range check is the binding's job: a seat number leaves here as a signed
// int32 and is narrowed again by every display consumer, so 65536 must not be
// allowed to arrive as seat 0.
int og_emit_notification(lua_State* L)
{
    if (current_game == nullptr)
        return luaL_error(L, "no active context");
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    const auto duration =
        static_cast<std::uint32_t>(luaL_optinteger(L, 2, 0));
    std::int32_t target = -1;
    if (!lua_isnoneornil(L, 3))
    {
        if (lua_isuserdata(L, 3))
        {
            const walker* w = resolve_walker(L, 3, /*required=*/true);
            target = w != nullptr ? static_cast<std::int32_t>(w->user()) : -1;
        }
        else
        {
            target = static_cast<std::int32_t>(luaL_checkinteger(L, 3));
        }
        if (target < 0 ||
            target >= static_cast<std::int32_t>(og::sim::kMaxGlobalPlayers))
            target = -1;
    }
    og::sim::emit_notification(current_game->sim_events, std::string(s, len),
                               duration, target);
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
// og.tuning — per-family tuning constants
// ---------------------------------------------------------------------------

// og.tuning(self) → the `tuning` map self's family declared, as a frozen
// read-only table — key access only; writes
// raise; no iteration is provided (and none is needed, so the no-pairs rule
// never comes up). A family that declared nothing gets an EMPTY frozen
// table, so `og.tuning(self).some_key` is nil rather than an error and a
// script can carry defaults. Tuning is load-time pack content exactly like
// the rest of the descriptor: it rides multiplayer transfer inside the
// declaration and never appears in a snapshot. Views are cached per VM
// per (order, family) and rebuilt when a pack reinstall bumps the store's
// generation — a world VM built before a remount keeps serving the values
// its scripts were loaded against, matching how the scripts themselves
// behave.
int og_tuning(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/true);
    VmState* st = get_vm_state(L);
    if (st == nullptr)
        return luaL_error(L, "og.tuning: no world scripts active");
    if (st->tuning_cache_gen != family_tuning_generation()) {
        luaL_unref(L, LUA_REGISTRYINDEX, st->tuning_cache_ref);
        lua_newtable(L);
        st->tuning_cache_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        st->tuning_cache_gen = family_tuning_generation();
    }
    const lua_Integer key =
        static_cast<lua_Integer>(w->query_order()) * 4096 +
        static_cast<lua_Integer>(w->family());
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->tuning_cache_ref);
    lua_rawgeti(L, -1, key);
    if (lua_istable(L, -1)) {
        lua_remove(L, -2);
        return 1;
    }
    lua_pop(L, 1);
    lua_newtable(L);  // cache, data
    if (const TuningMap* map =
            family_tuning(w->query_order(), static_cast<int>(w->family()))) {
        for (const TuningPair& p : *map) {
            switch (p.value.kind) {
                case TuningValue::Kind::Integer:
                    lua_pushinteger(
                        L, static_cast<lua_Integer>(p.value.integer));
                    break;
                case TuningValue::Kind::Number:
                    lua_pushnumber(L,
                                   static_cast<lua_Number>(p.value.number));
                    break;
                case TuningValue::Kind::Boolean:
                    lua_pushboolean(L, p.value.boolean ? 1 : 0);
                    break;
                case TuningValue::Kind::String:
                    lua_pushlstring(L, p.value.string.data(),
                                    p.value.string.size());
                    break;
            }
            lua_setfield(L, -2, p.key.c_str());
        }
    }
    push_frozen_view_of_top(L);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, key);  // cache[key] = view
    lua_remove(L, -2);        // drop the cache table, leave the view
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
// The single-frame read `self->ani[row][index]` used by
// packs/core/lib/effect_chain.lua. nil means the animation table, row, or
// index does not exist, so the caller skips its set_frame. The
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
// (excluding) the -1 sentinel, for row-walking pack hooks such as
// packs/core/lib/weapon_animate.lua. nil covers every case where the
// engine-side lookup stops early: no table, row past ani_count, null row, or
// missing sentinel. An empty table is a present-but-zero-length sequence.
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

// og.award_score(team, points) — the treasure families' score credit: bump
// GameWorld::m_score and emit ScoreChange with the same payload. Teams
// outside the score table are silently ignored by the score-table guard.
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

// og.set_withdraw_request(level) — set both fields in the exit pad's
// withdraw latch together.
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

// og.campaign_var(name) → the campaign decision value copied into this
// world at level load (0 when absent — the var==0 rule is what keeps a
// dedicated server, whose SaveData carries no campaign state, on stock
// behavior). Read-only from the sim; menu-side writes go through
// og.campaign_state_set. World-fenced like every sim binding.
int og_campaign_var(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const char* name = luaL_checkstring(L, 1);
    for (const auto& [key, value] : world->campaign_vars) {
        if (key == name) {
            lua_pushinteger(L, static_cast<lua_Integer>(value));
            return 1;
        }
    }
    lua_pushinteger(L, 0);
    return 1;
}

// ---------------------------------------------------------------------------
// og.campaign_* — menu-time campaign bindings (campaign-dispatch only)
// ---------------------------------------------------------------------------
//
// The campaign-hook twin of world_arg: every og.campaign_* entry is legal
// ONLY while a campaign hook is on the stack (the picker has no world to
// fence against, so the dispatch flag is the gate), and resolves through
// the process-global providers the owning surface installed
// (campaign_hooks.h). No provider = a Lua error, not a silent default.

VmState* campaign_dispatch_arg(lua_State* L, const char* name)
{
    VmState* st = get_vm_state(L);
    if (st == nullptr || !st->campaign_dispatch)
        luaL_error(L, "og.%s: campaign bindings are campaign-hook only",
                   name);
    return st;
}

int og_campaign_state_get(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_state_get");
    const char* key = luaL_checkstring(L, 1);
    const auto& p = campaign_providers();
    if (!p.state_get)
        return luaL_error(
            L, "og.campaign_state_get: no campaign provider installed");
    lua_pushinteger(L, static_cast<lua_Integer>(p.state_get(key)));
    return 1;
}

// og.campaign_state_set(key, v) — the binding is the bounds-enforcement
// point: a bad key raises BEFORE the provider runs, and the provider's own
// check-then-write false answer raises before any mutation, so a script
// bug can never brick the save file.
int og_campaign_state_set(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_state_set");
    std::size_t len = 0;
    const char* key = luaL_checklstring(L, 1, &len);
    const lua_Integer value = luaL_checkinteger(L, 2);
    if (!hooks::valid_campaign_var_name({key, len}))
        return luaL_error(
            L, "og.campaign_state_set: bad key '%s' (1-%d chars of "
               "[a-z0-9_])",
            key, hooks::kCampaignVarNameMax);
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
        return luaL_error(L,
                          "og.campaign_state_set: value out of int32 range");
    const auto& p = campaign_providers();
    if (!p.state_set)
        return luaL_error(
            L, "og.campaign_state_set: no campaign provider installed");
    if (!p.state_set(key, static_cast<std::int32_t>(value)))
        return luaL_error(
            L, "og.campaign_state_set: the campaign store rejected '%s' "
               "(entry bounds)",
            key);
    return 0;
}

int og_campaign_gold(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_gold");
    const auto& p = campaign_providers();
    if (!p.gold_get)
        return luaL_error(L,
                          "og.campaign_gold: no campaign provider installed");
    lua_pushinteger(L, static_cast<lua_Integer>(p.gold_get()));
    return 1;
}

// og.campaign_spend_gold(amount) → true/false — the affordability-checked
// debit for variable-priced flows inside actions (fixed `cost` debits are
// owned by C++ before the action dispatches).
int og_campaign_spend_gold(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_spend_gold");
    const lua_Integer amount = luaL_checkinteger(L, 1);
    if (amount < 0)
        return luaL_error(L, "og.campaign_spend_gold: negative amount");
    const auto& p = campaign_providers();
    if (!p.gold_spend)
        return luaL_error(
            L, "og.campaign_spend_gold: no campaign provider installed");
    lua_pushboolean(L, p.gold_spend(amount) ? 1 : 0);
    return 1;
}

int og_campaign_grant_gold(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_grant_gold");
    const lua_Integer amount = luaL_checkinteger(L, 1);
    if (amount < 0)
        return luaL_error(L, "og.campaign_grant_gold: negative amount");
    const auto& p = campaign_providers();
    if (!p.gold_grant)
        return luaL_error(
            L, "og.campaign_grant_gold: no campaign provider installed");
    p.gold_grant(amount);
    return 0;
}

// og.campaign_team() → array of plain tables (values, not handles — the
// roster outlives any dispatch, so handle semantics would be a trap).
int og_campaign_team(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_team");
    const auto& p = campaign_providers();
    if (!p.team_snapshot)
        return luaL_error(L,
                          "og.campaign_team: no campaign provider installed");
    const std::vector<hooks::CampaignRosterEntry> team = p.team_snapshot();
    lua_createtable(L, static_cast<int>(team.size()), 0);
    for (std::size_t i = 0; i < team.size(); i++) {
        const hooks::CampaignRosterEntry& entry = team[i];
        lua_createtable(L, 0, 13);
        lua_pushlstring(L, entry.name.data(), entry.name.size());
        lua_setfield(L, -2, "name");
        lua_pushlstring(L, entry.family.data(), entry.family.size());
        lua_setfield(L, -2, "family");
        lua_pushinteger(L, entry.level);
        lua_setfield(L, -2, "level");
        lua_pushinteger(L, entry.exp);
        lua_setfield(L, -2, "exp");
        lua_pushinteger(L, entry.strength);
        lua_setfield(L, -2, "strength");
        lua_pushinteger(L, entry.dexterity);
        lua_setfield(L, -2, "dexterity");
        lua_pushinteger(L, entry.constitution);
        lua_setfield(L, -2, "constitution");
        lua_pushinteger(L, entry.intelligence);
        lua_setfield(L, -2, "intelligence");
        lua_pushinteger(L, entry.armor);
        lua_setfield(L, -2, "armor");
        lua_pushinteger(L, entry.team);
        lua_setfield(L, -2, "team");
        // Per-hero identity (GTL v16): the persisted campaign_tag byte and
        // the owning save slot — the address a base_camp composition's
        // locks/assign use (never a guy id; ids regenerate every mission).
        lua_pushinteger(L, entry.tag);
        lua_setfield(L, -2, "tag");
        lua_pushinteger(L, entry.save_slot);
        lua_setfield(L, -2, "save_slot");
        // Tonight's sortie, not the oath census: a march row that counts
        // swords has to know which of them are actually standing.
        lua_pushboolean(L, entry.deployed ? 1 : 0);
        lua_setfield(L, -2, "deployed");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i) + 1);
    }
    return 1;
}

// og.campaign_level_completed(id) — the menu-time twin of the sim's
// og.level_completed.
int og_campaign_level_completed(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_level_completed");
    const auto id = static_cast<int>(luaL_checkinteger(L, 1));
    const auto& p = campaign_providers();
    if (!p.level_completed)
        return luaL_error(
            L, "og.campaign_level_completed: no campaign provider installed");
    lua_pushboolean(L, p.level_completed(id) ? 1 : 0);
    return 1;
}

int og_campaign_current_level(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_current_level");
    const auto& p = campaign_providers();
    if (!p.current_level)
        return luaL_error(
            L, "og.campaign_current_level: no campaign provider installed");
    lua_pushinteger(L, static_cast<lua_Integer>(p.current_level()));
    return 1;
}

// og.campaign_scenario_title(id) → title string, "" when absent.
int og_campaign_scenario_title(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_scenario_title");
    const auto id = static_cast<int>(luaL_checkinteger(L, 1));
    const auto& p = campaign_providers();
    if (!p.scenario_title)
        return luaL_error(
            L, "og.campaign_scenario_title: no campaign provider installed");
    const std::string title = p.scenario_title(id);
    lua_pushlstring(L, title.data(), title.size());
    return 1;
}

// The og.campaign_match_* vocabulary check (#212). Get keeps the sim
// twin's unknown-name error; set answers false instead (the provider
// contract), so this helper serves only the erroring spelling.
bool known_match_setting(const char* name)
{
    for (const char* known : hooks::kCampaignMatchSettingNames) {
        if (std::strcmp(known, name) == 0)
            return true;
    }
    return false;
}

// og.campaign_match_get(name) → int32 — the menu-time twin of the sim's
// read-only og.match_setting, over the persisted match knobs
// ("score_limit", "respawn_ticks", "respawn_mode", "generator_rate",
// "time_limit", and the per-team "fill_1".."fill_4" /
// "map_units_1".."map_units_4"). Unknown names error, like the twin — and
// "team_count" (amendment A3) and "strip_troops" (amendment B5) are now two
// of them: both knobs are retired, and both sim-side reads survive, always
// answering 0.
int og_campaign_match_get(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_match_get");
    const char* name = luaL_checkstring(L, 1);
    if (!known_match_setting(name))
        return luaL_error(L, "og.campaign_match_get: unknown setting '%s'",
                          name);
    const auto& p = campaign_providers();
    if (!p.match_get)
        return luaL_error(
            L, "og.campaign_match_get: no campaign provider installed");
    lua_pushinteger(L, static_cast<lua_Integer>(p.match_get(name)));
    return 1;
}

// og.campaign_match_set(name, value) → true/false — write-through to the
// match knobs. Policy lives in the provider: it clamps like the lobby
// sanitizer and answers false for unknown names or when this machine is
// not the host (local play is always host).
int og_campaign_match_set(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_match_set");
    const char* name = luaL_checkstring(L, 1);
    const lua_Integer value = luaL_checkinteger(L, 2);
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max())
        return luaL_error(L,
                          "og.campaign_match_set: value out of int32 range");
    const auto& p = campaign_providers();
    if (!p.match_set)
        return luaL_error(
            L, "og.campaign_match_set: no campaign provider installed");
    lua_pushboolean(L, p.match_set(name, static_cast<std::int32_t>(value))
                           ? 1
                           : 0);
    return 1;
}

// og.campaign_is_host() → true/false — so a script can shape host-only
// pages (level rows and match presets) without tripping the refusal.
int og_campaign_is_host(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_is_host");
    const auto& p = campaign_providers();
    if (!p.is_host)
        return luaL_error(
            L, "og.campaign_is_host: no campaign provider installed");
    lua_pushboolean(L, p.is_host() ? 1 : 0);
    return 1;
}

// og.campaign_random(n) → integer in 1..n — the menu-time roll. Campaign
// dispatch only, resolved through the surface's random_pick provider (a
// process-lifetime menu-side generator by default — NEVER the sim stream,
// which og.rand keeps fenced during campaign hooks). Errors on n < 1, so a
// script cannot ask an empty question.
int og_campaign_random(lua_State* L)
{
    campaign_dispatch_arg(L, "campaign_random");
    const lua_Integer n = luaL_checkinteger(L, 1);
    if (n < 1)
        return luaL_error(L, "og.campaign_random: n must be >= 1");
    if (n > std::numeric_limits<std::int32_t>::max())
        return luaL_error(L, "og.campaign_random: n out of int32 range");
    const auto& p = campaign_providers();
    if (!p.random_pick)
        return luaL_error(
            L, "og.campaign_random: no campaign provider installed");
    lua_pushinteger(L,
                    static_cast<lua_Integer>(p.random_pick(static_cast<int>(n))));
    return 1;
}

// ---------------------------------------------------------------------------
// Deterministic arithmetic / branch helpers
// ---------------------------------------------------------------------------
//
// These calls give pack scripts one canonical implementation of guarded
// random draws, min/max, clamping, and sign extraction. Each documents the
// exact C++ semantic it reproduces, and the
// branchy micro-logic lives here, where gcov measures every arm (a Lua
// one-line guard is a single coverage point however many ways it can go).

// Shared argument validator: a real Lua number that is not NaN. Stricter
// than luaL_checknumber on purpose, twice over. No string coercion:
// og.max/og.min/og.clamp answer with ONE OF THEIR ARGUMENTS, so what goes
// in must already be the number that comes out. No NaN: every comparison
// below would silently answer "not less" and hand NaN onward — the sim
// never produces NaN, and this keeps that true on every build (a script
// error, not a debug-only assert), so a script that conjures one
// (math.huge - math.huge) fails loudly at the door instead of laundering
// it into a walker field.
void check_number_arg(lua_State* L, int idx)
{
    if (lua_type(L, idx) != LUA_TNUMBER)
        luaL_typeerror(L, idx, "number");
    if (!lua_isinteger(L, idx) && std::isnan(lua_tonumber(L, idx)))
        luaL_argerror(L, idx, "NaN");
}

// og.rand0(n) — the world RNG's `next(n)` with IRandom's real n <= 0
// contract instead of og.rand's error: next(0) answers 0 WITHOUT advancing
// the generator (SimRandom returns before the LCG step — core/irandom.h),
// which is exactly what the hand-written
//   local r = 0
//   if n > 0 then r = og.rand(n) end
// guard trios encode. Negative n behaves as 0 too, absorbing the C++
// pre-clamp at sites shaped `random(x < 1 ? 0 : x)`. For n > 0 this is
// og.rand verbatim — the identical int32 cast chain into the identical
// world RNG — so replacing a guarded og.rand with og.rand0 cannot move the
// stream. An active world is required on EVERY path (n <= 0 included): the
// answer is a property of the world's generator, and calling without one
// is a bug worth hearing about. n above kMaxRandBound is the one place the
// two spellings still differ from "verbatim" only in wording: both refuse
// it, because a bound that cannot survive the trip to
// IRandom::next(std::uint32_t) intact has no meaning to hand back.
int og_rand0(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer n = luaL_checkinteger(L, 1);
    if (n <= 0) {
        lua_pushinteger(L, 0);
        return 1;
    }
    if (n > kMaxRandBound)
        return luaL_error(L, "og.rand0: n out of range [1, 2147483647]");
    lua_pushinteger(L, static_cast<lua_Integer>(
                           world->rng_.next(static_cast<std::uint32_t>(n))));
    return 1;
}

// og.max(a, b) / og.min(a, b) — std::max / std::min EXACTLY: og.max answers
// b only when a < b, og.min answers b only when b < a, so every tie answers
// a (observable: math.type(og.max(5, 5.0)) is 'integer' while
// math.type(og.min(5.0, 5)) is 'float'). The comparison is Lua's own exact
// number ordering — mixed integer/float compares mathematically, never
// through a lossy int64→double round-trip — and the winning ARGUMENT is
// returned unchanged, so integer subtype survives into arithmetic
// downstream.
int og_max(lua_State* L)
{
    check_number_arg(L, 1);
    check_number_arg(L, 2);
    lua_pushvalue(L, lua_compare(L, 1, 2, LUA_OPLT) ? 2 : 1);
    return 1;
}

int og_min(lua_State* L)
{
    check_number_arg(L, 1);
    check_number_arg(L, 2);
    lua_pushvalue(L, lua_compare(L, 2, 1, LUA_OPLT) ? 2 : 1);
    return 1;
}

// og.clamp(v, lo, hi) — std::clamp: lo when v < lo, else hi when hi < v,
// else v itself (so ties answer v: math.type(og.clamp(5, 5.0, 6.0)) is
// 'integer'). std::clamp's precondition — hi < lo is UB — is hardened into
// a script error rather than inherited. Ordering and subtype rules are
// og.max's.
int og_clamp(lua_State* L)
{
    check_number_arg(L, 1);
    check_number_arg(L, 2);
    check_number_arg(L, 3);
    if (lua_compare(L, 3, 2, LUA_OPLT))
        return luaL_error(L, "og.clamp: hi < lo (empty range)");
    int answer = 1;
    if (lua_compare(L, 1, 2, LUA_OPLT))
        answer = 2;
    else if (lua_compare(L, 3, 1, LUA_OPLT))
        answer = 3;
    lua_pushvalue(L, answer);
    return 1;
}

// og.sign(x) — the sign-extraction idiom the movement code spells
// `v /= abs(v)` behind a v != 0 guard, as a total function: -1, 0 or 1 as
// an INTEGER for any number (og.sign(0), og.sign(0.0) and og.sign(-0.0)
// are all 0). The comparisons run on the double image of x, which
// preserves sign for every int64: magnitude may round, but the smallest
// nonzero magnitude, 1, rounds to 1.0 — never to 0 and never across it.
int og_sign(lua_State* L)
{
    check_number_arg(L, 1);
    const lua_Number v = lua_tonumber(L, 1);
    lua_Integer s = 0;
    if (v > 0)
        s = 1;
    if (v < 0)
        s = -1;
    lua_pushinteger(L, s);
    return 1;
}

// ---------------------------------------------------------------------------
// og.combat — bindings over the og::combat constexpr helpers
// (include/openglad/core/combat_math.h)
// ---------------------------------------------------------------------------
//
// One entry per combat helper exposed to pack scripts. The four flat
// spellings also used by the corpus (og.scare_duration, og.scare_radius,
// og.elemental_lifetime, og.image_lifetime) remain available; new
// combat_math.h surface belongs here.

// og.combat.yell_radius(level) — combat_math.h yell_radius: legacy
// 160 + 20*L px with a flat cap at kYellRadiusCap (420 = f(13)).
int og_combat_yell_radius(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(og::combat::yell_radius(
                           static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

// og.combat.stun_total(cur_raw, add) — combat_math.h stun_total, the orc
// yell stun accumulator over RAW frozen_delay: cur_raw < 0 (thaw-immunity
// phase) discards the add and answers cur_raw unchanged; otherwise a
// negative add counts as 0 and the sum caps monotonically at
// kFrozenStunStackCap (150) — an already-over-cap value is answered
// unchanged, never reduced.
int og_combat_stun_total(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(og::combat::stun_total(
                           static_cast<int>(luaL_checkinteger(L, 1)),
                           static_cast<int>(luaL_checkinteger(L, 2)))));
    return 1;
}

// og.combat.bomb_damage(level) — combat_math.h bomb_damage: legacy
// 15*(L+1) softened over kBombDamageKnee/kBombDamageCeiling (210/300).
int og_combat_bomb_damage(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(og::combat::bomb_damage(
                           static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

// og.combat.cloak_total(cur, gain) — combat_math.h cloak_total, the thief
// cloak accumulator over invisibility_left:
// max(cur, min(cur + gain, kInvisibilityCloakCap)) — monotonic, so a
// potion-granted value above the cap (450) is never reduced.
int og_combat_cloak_total(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(og::combat::cloak_total(
                           static_cast<int>(luaL_checkinteger(L, 1)),
                           static_cast<int>(luaL_checkinteger(L, 2)))));
    return 1;
}

// og.combat.glow_bonus(level) — combat_math.h glow_bonus: legacy 110*L
// with a FLAT cap at kGlowBonusCap (2200 — deliberately not a knee-13
// soften; the header records why).
int og_combat_glow_bonus(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(og::combat::glow_bonus(
                           static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

// og.combat.druid_faerie_lifetime(level) — combat_math.h
// druid_faerie_lifetime: soften(50 + 40*L, 570, 800).
int og_combat_druid_faerie_lifetime(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(og::combat::druid_faerie_lifetime(
                        static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

// og.combat.skeleton_lifetime(level) — combat_math.h skeleton_lifetime:
// soften(125 + 40*L, 645, 900).
int og_combat_skeleton_lifetime(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(og::combat::skeleton_lifetime(
                        static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

// og.combat.ghost_raise_lifetime(level) — combat_math.h
// ghost_raise_lifetime: soften(150 + 40*L, 670, 925).
int og_combat_ghost_raise_lifetime(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(og::combat::ghost_raise_lifetime(
                        static_cast<int>(luaL_checkinteger(L, 1)))));
    return 1;
}

// ob:add_frozen_stun(n) — the universal application pattern for
// stun_total, fused into one verb:
//   ob:s_set_frozen_delay(stun_total(ob:s_frozen_delay_raw(), n))
// in exactly that call order: RAW read (a negative thaw-immunity value is
// seen by the policy, not masked to 0 first), stun_total's discard/cap,
// then the short-narrowing setter — statistics::set_frozen_delay itself,
// dirty-bit semantics included. n narrows to C++ int first (stun_total's
// parameter type), and the write narrows to short through the same
// uint64 chain every s_set_* integer setter uses.
int m_add_frozen_stun(lua_State* L)
{
    statistics* st = stats_arg(L);
    const auto add = static_cast<int>(luaL_checkinteger(L, 2));
    st->set_frozen_delay(static_cast<short>(static_cast<std::uint64_t>(
        og::combat::stun_total(st->frozen_delay_raw(), add))));
    return 0;
}

// ---------------------------------------------------------------------------
// Walker property layer
// ---------------------------------------------------------------------------
//
// `self.hp` / `self.busy = x` route through the SAME lua_CFunctions the
// method table registers: kWalkerProperties below is the single source of
// truth, pairing each property name with the registered getter/setter
// pointers, so a property cannot drift from its method — narrowing included
// (writing self.team runs m_set_team_num's unsigned-char narrowing; writing
// self.ani_type runs m_set_ani_type's char narrowing) and handle validity
// included (a stale handle raises the identical "stale or dead entity
// handle" error, because it IS the same code). The accessor is invoked
// DIRECTLY rather than through lua_call so luaL_error's level-1 position
// names the accessing Lua line exactly as a method call would; an
// interposed C frame would erase it.
//
// READ RESOLUTION IS METHOD-FIRST, and that is a load-bearing choice, not a
// tidiness one. `self:busy()` is sugar for `self.busy(self)`: the lookup
// cannot see whether a call follows, so a name that is both a method and a
// property can serve only one of the two spellings — and the corpus (and
// any third-party pack) calls the method spelling, so the method MUST keep
// winning or every `self:busy()` becomes "attempt to call a number value".
// Concretely:
//   * names with no method of the same name (hp, max_hp, magicpoints,
//     max_magicpoints, level, team) read as values: `self.hp`;
//   * names shadowed by a method (busy, dead, weapons_left, lifetime,
//     damage, ani_type, current_special, foe, xpos, ypos) keep reading as
//     the method — `self.busy` is a function, exactly what `self:busy()`
//     resolves. These names therefore expose property writes but not property
//     reads while their methods exist.
// Reads of unknown names still answer nil, unchanged for every existing
// script.
//
// WRITES have no such conflict — nothing resolved methods through
// assignment before (any write to a handle raised) — so EVERY writable
// property works as a write, shadowed-read names included:
// `self.busy = 5` runs m_set_busy. An unknown-field write, a write to a
// read-only property, and a write to a method name each raise a distinct
// script error.
//
// The s_*/method spellings remain functional aliases; docs/lua-style.md S6
// defines which spelling new pack code uses.

struct WalkerProperty {
    const char* name;
    lua_CFunction getter;  // reads self at stack index 1
    lua_CFunction setter;  // self at 1, value at 2; nullptr = read-only
};

const WalkerProperty kWalkerProperties[] = {
    {"hp", s_hitpoints, s_set_hitpoints},
    {"max_hp", s_max_hitpoints, s_set_max_hitpoints},
    {"magicpoints", s_magicpoints, s_set_magicpoints},
    {"max_magicpoints", s_max_magicpoints, s_set_max_magicpoints},
    {"level", s_level, s_set_level},
    {"busy", m_busy, m_set_busy},
    {"team", m_team_num, m_set_team_num},
    {"dead", m_dead, m_set_dead},
    {"weapons_left", m_weapons_left, m_set_weapons_left},
    {"lifetime", m_lifetime, m_set_lifetime},
    {"damage", m_damage, m_set_damage},
    {"ani_type", m_ani_type, m_set_ani_type},
    {"current_special", m_current_special, m_set_current_special},
    {"foe", m_foe, m_set_foe},
    {"xpos", m_xpos, nullptr},
    {"ypos", m_ypos, nullptr},
};

// __index(handle, key): method table first (see the resolution contract
// above), then the property getters.
// Upvalues: 1 = method table, 2 = getter table.
int walker_index(lua_State* L)
{
    lua_pushvalue(L, 2);
    if (lua_rawget(L, lua_upvalueindex(1)) != LUA_TNIL)
        return 1;  // the method function, exactly as the plain table served
    lua_pop(L, 1);
    lua_pushvalue(L, 2);
    if (lua_rawget(L, lua_upvalueindex(2)) == LUA_TFUNCTION) {
        const lua_CFunction getter = lua_tocfunction(L, -1);
        lua_settop(L, 1);  // just the handle — the stack a method call sees
        return getter(L);
    }
    lua_pop(L, 1);
    lua_pushnil(L);  // unknown name: nil, like any table miss
    return 1;
}

// __newindex(handle, key, value): property setters only.
// Upvalues: 1 = setter table, 2 = getter table (read-only diagnosis),
// 3 = method table (reassignment diagnosis).
int walker_newindex(lua_State* L)
{
    lua_pushvalue(L, 2);
    if (lua_rawget(L, lua_upvalueindex(1)) == LUA_TFUNCTION) {
        const lua_CFunction setter = lua_tocfunction(L, -1);
        lua_pop(L, 1);
        lua_remove(L, 2);  // (handle, value) — the stack a set_* call sees
        return setter(L);
    }
    lua_pop(L, 1);
    const char* key = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2)
                                                    : luaL_typename(L, 2);
    lua_pushvalue(L, 2);
    const bool read_only =
        lua_rawget(L, lua_upvalueindex(2)) == LUA_TFUNCTION;
    lua_pop(L, 1);
    if (read_only)
        return luaL_error(L, "walker property '%s' is read-only", key);
    lua_pushvalue(L, 2);
    const bool is_method = lua_rawget(L, lua_upvalueindex(3)) != LUA_TNIL;
    lua_pop(L, 1);
    if (is_method)
        return luaL_error(L, "'%s' is a walker method, not a writable "
                             "property", key);
    return luaL_error(L, "cannot assign unknown walker field '%s'", key);
}

// ---------------------------------------------------------------------------
// Scripted-mode (TYPE_SCRIPTED) bindings: mode state, win channel, respawn
// surface, director support. Backends live in mode/mode_tick.cpp and the
// respawn engine (respawn/respawn_state.h).
// ---------------------------------------------------------------------------

// og.end_level(ending, next_level) — arm the scripted-mode win latch:
// ending 0 = win/advance, 1 = loss; next_level id+1 advance | id rematch |
// -1 terminal. Safe to call once — mode_run_tick re-asserts the world end
// fields from the latch every tick. Calling again overwrites (last write
// wins). First arming revives every pending respawn.
int og_end_level(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer ending = luaL_checkinteger(L, 1);
    if (ending != 0 && ending != 1)
        return luaL_error(L, "og.end_level: ending must be 0 (win) or 1 "
                             "(loss)");
    const lua_Integer next_level = luaL_checkinteger(L, 2);
    if (next_level < -1 || next_level > 32767)
        return luaL_error(L, "og.end_level: next_level out of range "
                             "[-1, 32767]");
    og::sim::mode_end_level(*world, static_cast<int>(ending),
                            static_cast<int>(next_level));
    return 0;
}

// og.declare_winner(team) — convenience win latch: records winner_team,
// computes winner_is_player (live myguy on the winning team, after the
// first-arming revive flush), and latches ending 0 with next_level id+1 on
// a player win, id (rematch) otherwise. Accepts a score team 0-3 or an FFA
// band byte 16-31. Emits no notification/sound — the mode's Lua owns its
// own announcements.
int og_declare_winner(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer team = luaL_checkinteger(L, 1);
    if (team < 0 || !og::sim::is_scoring_identity(static_cast<int>(team)))
        return luaL_error(L, "team %d not in [0, %d] or band [%d, %d]",
                          static_cast<int>(team), SCORE_TEAM_COUNT - 1,
                          kFfaTeamBase, kFfaTeamBase + kFfaTeamCount - 1);
    og::sim::mode_declare_winner(*world, static_cast<int>(team));
    return 0;
}

// og.mode_winner() — ModeState.winner_team; -1 while undecided.
int og_mode_winner(lua_State* L)
{
    lua_pushinteger(
        L, static_cast<lua_Integer>(world_arg(L)->mode.winner_team));
    return 1;
}

// og.mode_get(i) — read replicated mode var i (0-based, error outside
// [0, 63]). The ONLY durable home for Lua mode state (R6).
int og_mode_get(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer i = luaL_checkinteger(L, 1);
    if (i < 0 || i >= og::sim::kModeVarCount)
        return luaL_error(L, "mode var index %d out of range [0, %d]",
                          static_cast<int>(i), og::sim::kModeVarCount - 1);
    lua_pushinteger(L, static_cast<lua_Integer>(
                           world->mode.vars[static_cast<std::size_t>(i)]));
    return 1;
}

// og.mode_set(i, v) — write mode var i; v truncates to int32 (og.i32
// semantics; non-integer numbers error).
int og_mode_set(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer i = luaL_checkinteger(L, 1);
    if (i < 0 || i >= og::sim::kModeVarCount)
        return luaL_error(L, "mode var index %d out of range [0, %d]",
                          static_cast<int>(i), og::sim::kModeVarCount - 1);
    const lua_Integer v = luaL_checkinteger(L, 2);
    world->mode.vars[static_cast<std::size_t>(i)] =
        static_cast<std::int32_t>(v);
    return 0;
}

// og.set_mode_name(s) — the HUD/results mode label, clamped to 11 bytes.
int og_set_mode_name(lua_State* L)
{
    GameWorld* world = world_arg(L);
    size_t len = 0;
    const char* s = luaL_checklstring(L, 1, &len);
    auto& name = world->mode.name;
    name.fill('\0');
    const size_t n = std::min(len, static_cast<size_t>(
                                       og::sim::kModeNameBytes - 1));
    std::memcpy(name.data(), s, n);
    return 0;
}

// og.set_hud_line(slot, text [, team]) — write a generic mode HUD line:
// slot 0-3, text clamped to 25 bytes, team (0-3 or band 16-31) tints the
// line (nil = default HUD color). Replicated; each client renders it its
// own way.
int og_set_hud_line(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer slot = luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= og::sim::kModeHudLines)
        return luaL_error(L, "slot %d out of range [0, %d]",
                          static_cast<int>(slot), og::sim::kModeHudLines - 1);
    size_t len = 0;
    const char* s = luaL_checklstring(L, 2, &len);
    std::uint8_t team = 255;
    if (!lua_isnoneornil(L, 3)) {
        const lua_Integer t = luaL_checkinteger(L, 3);
        if (t < 0 || !og::sim::is_scoring_identity(static_cast<int>(t)))
            return luaL_error(L, "team %d not in [0, %d] or band [%d, %d]",
                              static_cast<int>(t), SCORE_TEAM_COUNT - 1,
                              kFfaTeamBase, kFfaTeamBase + kFfaTeamCount - 1);
        team = static_cast<std::uint8_t>(t);
    }
    og::sim::ModeHudLine& line =
        world->mode.hud[static_cast<std::size_t>(slot)];
    line.team = team;
    line.text.fill('\0');
    const size_t n = std::min(len, static_cast<size_t>(
                                       og::sim::kModeHudTextBytes - 1));
    std::memcpy(line.text.data(), s, n);
    return 0;
}

// og.clear_hud_line(slot) — reset a HUD line to empty/default.
int og_clear_hud_line(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer slot = luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= og::sim::kModeHudLines)
        return luaL_error(L, "slot %d out of range [0, %d]",
                          static_cast<int>(slot), og::sim::kModeHudLines - 1);
    world->mode.hud[static_cast<std::size_t>(slot)] = og::sim::ModeHudLine{};
    return 0;
}

// og.set_beacon(slot, entity_or_nil [, team]) — mark an entity for the
// off-screen/radar beacon channel (the Mutant marker, a flag carrier).
// nil clears the slot.
int og_set_beacon(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer slot = luaL_checkinteger(L, 1);
    if (slot < 0 || slot >= og::sim::kModeBeacons)
        return luaL_error(L, "slot %d out of range [0, %d]",
                          static_cast<int>(slot), og::sim::kModeBeacons - 1);
    og::sim::ModeBeacon& beacon =
        world->mode.beacons[static_cast<std::size_t>(slot)];
    walker* w = resolve_walker_or_nil(L, 2);
    if (w == nullptr) {
        beacon = og::sim::ModeBeacon{};
        return 0;
    }
    std::uint8_t team = 255;
    if (!lua_isnoneornil(L, 3)) {
        const lua_Integer t = luaL_checkinteger(L, 3);
        if (t < 0 || !og::sim::is_scoring_identity(static_cast<int>(t)))
            return luaL_error(L, "team %d not in [0, %d] or band [%d, %d]",
                              static_cast<int>(t), SCORE_TEAM_COUNT - 1,
                              kFfaTeamBase, kFfaTeamBase + kFfaTeamCount - 1);
        team = static_cast<std::uint8_t>(t);
    }
    beacon.entity_id = static_cast<std::int32_t>(w->entity_id());
    beacon.team = team;
    return 0;
}

// og.team_score(t) — read GameWorld::m_score[t] (og.award_score's counter);
// errors outside [0, 3].
int og_team_score(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer team = luaL_checkinteger(L, 1);
    if (team < 0 || team >= SCORE_TEAM_COUNT)
        return luaL_error(L, "team %d out of range [0, %d]",
                          static_cast<int>(team), SCORE_TEAM_COUNT - 1);
    lua_pushinteger(L, static_cast<lua_Integer>(
                           world->m_score[static_cast<std::size_t>(team)]));
    return 1;
}

// og.find_by_id(id) — entity id -> handle; nil for 0, absent, or
// dead-and-swept ids. The id -> handle resolver every replicated mode var
// holding an entity id needs.
int og_find_by_id(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer id = luaL_checkinteger(L, 1);
    if (id <= 0) {
        lua_pushnil(L);
        return 1;
    }
    push_walker_here(L, world->find_by_id(static_cast<std::uint32_t>(id)));
    return 1;
}

// og.fxlist() — the fx entity list in list order (flags, exit pads, balls
// live here; og.oblist covers livings/generators only).
int og_fxlist(lua_State* L)
{
    GameWorld* world = world_arg(L);
    lua_newtable(L);
    lua_Integer i = 1;
    for (const auto& uptr : world->fxlist) {
        if (uptr == nullptr)
            continue;
        push_walker_here(L, uptr.get());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// og.weaplist() — the weapon entity list in list order.
int og_weaplist(lua_State* L)
{
    GameWorld* world = world_arg(L);
    lua_newtable(L);
    lua_Integer i = 1;
    for (const auto& uptr : world->weaplist) {
        if (uptr == nullptr)
            continue;
        push_walker_here(L, uptr.get());
        lua_rawseti(L, -2, i++);
    }
    return 1;
}

// og.world_tick() — the absolute world tick counter (snapshotted, safe
// across a mid-level restore; og.level_tick is the per-level counter).
int og_world_tick(lua_State* L)
{
    lua_pushinteger(L,
                    static_cast<lua_Integer>(world_arg(L)->tick_count_));
    return 1;
}

// og.team_color_name(t) — "RED"/"GREEN"/"BLUE"/"YELLOW" for score teams,
// the 16 band names for FFA bytes 16-31, matching the rendered palette
// ramps; errors outside those two ranges.
int og_team_color_name(lua_State* L)
{
    const lua_Integer team = luaL_checkinteger(L, 1);
    if (team < 0 || !og::sim::is_scoring_identity(static_cast<int>(team)))
        return luaL_error(L, "team %d not in [0, %d] or band [%d, %d]",
                          static_cast<int>(team), SCORE_TEAM_COUNT - 1,
                          kFfaTeamBase, kFfaTeamBase + kFfaTeamCount - 1);
    lua_pushstring(L, og::sim::team_color_name(static_cast<int>(team)));
    return 1;
}

// og.match_setting(name) — the lobby/save match knobs, reinterpreted as
// generic match settings. Names: "team_count" (RETIRED by amendment A3 and
// always 0 — every team the map authors; kept readable so scripts written
// against it keep running),
// "score_limit", "respawn_ticks",
// "strip_troops" (RETIRED by amendment B5 — always 0; the per-team
// "map_units_N" box answers what it used to ask, and the name stays
// readable so an old script keeps running),
// "respawn_mode" (the difficulty submenu's classic respawn selector),
// "time_limit" (the match clock in SIM TICKS; 0 = the map's own value, so
// modes resolve it against their manifest row through match.resolve_limit),
// "fill_1".."fill_4" (the per-team FILL wheel: 0 = FAIR, 1 = NONE,
// 2 = WEAK, 3 = STRONG, 4 = BRUTAL — the multiplier each code means lives
// in the mode Lua, which is the only layer that solves a target),
// "map_units_1".."map_units_4" (the per-team MAP UNITS box: 0 = the map's
// own authored units are fielded, 1 = they are not),
// "difficulty" (the session difficulty percent, 100 = normal — the CTF
// bot-squad level formula reads it).
int og_match_setting(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const char* s = luaL_checkstring(L, 1);
    lua_Integer value = 0;
    if (std::strcmp(s, "team_count") == 0)
        value = world->ctf_requested_team_count;
    else if (std::strcmp(s, "score_limit") == 0)
        value = world->ctf_requested_capture_limit;
    else if (std::strcmp(s, "respawn_ticks") == 0)
        value = world->ctf_requested_respawn_ticks;
    else if (std::strcmp(s, "strip_troops") == 0)
        value = world->ctf_requested_strip_scenario_troops;
    else if (std::strcmp(s, "respawn_mode") == 0)
        value = world->respawn_mode;
    else if (std::strcmp(s, "time_limit") == 0)
        value = world->ctf_requested_time_limit;
    else if (std::strcmp(s, "fill_1") == 0)
        value = world->ctf_requested_fill[0];
    else if (std::strcmp(s, "fill_2") == 0)
        value = world->ctf_requested_fill[1];
    else if (std::strcmp(s, "fill_3") == 0)
        value = world->ctf_requested_fill[2];
    else if (std::strcmp(s, "fill_4") == 0)
        value = world->ctf_requested_fill[3];
    else if (std::strcmp(s, "map_units_1") == 0)
        value = world->ctf_requested_map_units[0];
    else if (std::strcmp(s, "map_units_2") == 0)
        value = world->ctf_requested_map_units[1];
    else if (std::strcmp(s, "map_units_3") == 0)
        value = world->ctf_requested_map_units[2];
    else if (std::strcmp(s, "map_units_4") == 0)
        value = world->ctf_requested_map_units[3];
    else if (std::strcmp(s, "difficulty") == 0)
        value = world->difficulty;
    else
        return luaL_error(L, "og.match_setting: unknown setting '%s'", s);
    lua_pushinteger(L, value);
    return 1;
}

// og.authored_team_mask() — bitmask of teams with authored start markers
// (dead ones included; the versus-map team domain).
int og_authored_team_mask(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(
                           og::sim::authored_team_mask(*world_arg(L))));
    return 1;
}

// og.effective_team_mask() — authored mask clamped by the requested team
// count (the ONE copy of the activation rule, shared with the lobby).
int og_effective_team_mask(lua_State* L)
{
    GameWorld* world = world_arg(L);
    lua_pushinteger(
        L, static_cast<lua_Integer>(og::sim::effective_team_mask(
               og::sim::authored_team_mask(*world),
               world->ctf_requested_team_count)));
    return 1;
}

// og.respawn_schedule(ent [, ticks]) — queue a dead Living; every stain —
// player and AI — survives until fire, while life gems scrub at schedule.
// Dedupe is by queued id and live duplicate; queue cap 64 evicts a bot.
// ticks overrides the resolved match delay (error when not positive).
// Returns true when a new entry was queued.
int og_respawn_schedule(lua_State* L)
{
    GameWorld* world = world_arg(L);
    walker* w = resolve_walker(L, 1, /*required=*/true);
    int ticks_override = -1;
    if (!lua_isnoneornil(L, 2)) {
        const lua_Integer ticks = luaL_checkinteger(L, 2);
        if (ticks <= 0 || ticks > 65535)
            return luaL_error(L, "og.respawn_schedule: ticks out of range "
                                 "[1, 65535]");
        ticks_override = static_cast<int>(ticks);
    }
    lua_pushboolean(
        L, og::sim::respawn_schedule_corpse(*world, w, ticks_override) ? 1
                                                                       : 0);
    return 1;
}

// og.respawn_pending(ent) — true while an engine respawn entry (player
// revive or AI replacement) is queued for this entity id.
int og_respawn_pending(lua_State* L)
{
    GameWorld* world = world_arg(L);
    walker* w = resolve_walker(L, 1, /*required=*/true);
    lua_pushboolean(L, og::sim::respawn_pending_for(*world, w) ? 1 : 0);
    return 1;
}

// og.respawn_pending_count(team) — queued entries for a team (error
// outside [0, 3]).
int og_respawn_pending_count(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer team = luaL_checkinteger(L, 1);
    if (team < 0 || team >= SCORE_TEAM_COUNT)
        return luaL_error(L, "team %d out of range [0, %d]",
                          static_cast<int>(team), SCORE_TEAM_COUNT - 1);
    lua_pushinteger(L,
                    static_cast<lua_Integer>(og::sim::respawn_pending_count(
                        *world, static_cast<int>(team))));
    return 1;
}

// og.respawn_anchor_count(team) — authored start-marker anchors recorded
// for a team (filled at CTF init / the first scripted tick).
int og_respawn_anchor_count(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer team = luaL_checkinteger(L, 1);
    if (team < 0 || team >= SCORE_TEAM_COUNT)
        return luaL_error(L, "team %d out of range [0, %d]",
                          static_cast<int>(team), SCORE_TEAM_COUNT - 1);
    lua_pushinteger(
        L, static_cast<lua_Integer>(
               world->respawn.anchor_count[static_cast<std::size_t>(team)]));
    return 1;
}

// og.respawn_anchor(team, i) — anchor i (0-based) for a team as x, y;
// errors when i is outside the recorded count.
int og_respawn_anchor(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const lua_Integer team = luaL_checkinteger(L, 1);
    if (team < 0 || team >= SCORE_TEAM_COUNT)
        return luaL_error(L, "team %d out of range [0, %d]",
                          static_cast<int>(team), SCORE_TEAM_COUNT - 1);
    const lua_Integer i = luaL_checkinteger(L, 2);
    const int count = static_cast<int>(
        world->respawn.anchor_count[static_cast<std::size_t>(team)]);
    if (i < 0 || i >= count)
        return luaL_error(L, "og.respawn_anchor: index %d out of range "
                             "[0, %d]",
                          static_cast<int>(i), count - 1);
    lua_pushinteger(L, static_cast<lua_Integer>(
                           world->respawn.anchor_x[team][i]));
    lua_pushinteger(L, static_cast<lua_Integer>(
                           world->respawn.anchor_y[team][i]));
    return 2;
}

// og.spawn_spot_clear(ent, x, y [, floor]) — the eat-free placement probe
// (NEVER og.query_passable: its obmap route dispatches eat_me, so a probe
// could eat a drumstick or pick up a flag). floor omitted probes the
// walker's own grid floor; floor given is the floor-explicit variant.
int og_spawn_spot_clear(lua_State* L)
{
    GameWorld* world = world_arg(L);
    walker* w = resolve_walker(L, 1, /*required=*/true);
    const short x = static_cast<short>(luaL_checkinteger(L, 2));
    const short y = static_cast<short>(luaL_checkinteger(L, 3));
    int floor = -1;
    if (!lua_isnoneornil(L, 4))
        floor = static_cast<int>(luaL_checkinteger(L, 4));
    lua_pushboolean(
        L, og::sim::respawn_spot_clear(*world, w, x, y, floor) ? 1 : 0);
    return 1;
}

// og.scrub_corpse_stain(x, y [, floor]) — preserve pending-respawn STAIN
// drops (player or AI); otherwise kill fresh STAIN/LIFE_GEM drops at a
// corpse position (its top-left) so a permanent body cannot be resurrected
// or farm gem score. floor omitted scrubs every floor.
int og_scrub_corpse_stain(lua_State* L)
{
    GameWorld* world = world_arg(L);
    const short x = static_cast<short>(luaL_checkinteger(L, 1));
    const short y = static_cast<short>(luaL_checkinteger(L, 2));
    int floor = -1;
    if (!lua_isnoneornil(L, 3))
        floor = static_cast<int>(luaL_checkinteger(L, 3));
    og::sim::respawn_scrub_stains_at(*world, x, y, floor);
    return 0;
}

// walker:set_act_type(n) — write the AI act type. ACT_CONTROL is refused
// (the level-loader rule: a script must not steal player control).
int m_set_act_type(lua_State* L)
{
    walker* w = self_arg(L);
    const lua_Integer n = luaL_checkinteger(L, 2);
    if (n == ACT_CONTROL)
        return luaL_error(L, "set_act_type: ACT_CONTROL is reserved for "
                             "player seats");
    w->set_act_type(static_cast<short>(n));
    return 0;
}

// walker:restore_act_type() — one-deep undo of set_act_type.
int m_restore_act_type(lua_State* L)
{
    self_arg(L)->restore_act_type();
    return 0;
}

// walker:last_self_teleport_tick() — the world tick this walker last began
// a SELF-teleport (blinks and marker beacons; map teleporter pads never
// stamp). Compare against og.world_tick() for the CTF drop rule.
int m_last_self_teleport_tick(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(
                           self_arg(L)->last_self_teleport_tick()));
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
    {"act_type", m_act_type}, {"set_act_type", m_set_act_type},
    {"restore_act_type", m_restore_act_type},
    {"last_self_teleport_tick", m_last_self_teleport_tick},
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
    {"heal_clamped", m_heal_clamped},
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
    {"dormant", m_dormant},
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
    {"add_frozen_stun", m_add_frozen_stun},
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
    {"s_front_command", s_front_command},
    {"s_refresh_front", s_refresh_front},
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
    {"summon_configured", og_summon_configured},
    {"find_near_foe", og_find_near_foe},
    {"find_nearest_blood", og_find_nearest_blood},
    {"find_foes_in_range", og_find_foes_in_range},
    {"find_friends_in_range", og_find_friends_in_range},
    {"find_in_range", og_find_in_range},
    {"find_foe_weapons_in_range", og_find_foe_weapons_in_range},
    {"foes_in_range", og_foes_in_range},
    {"oblist", og_oblist},
    {"fxlist", og_fxlist},
    {"weaplist", og_weaplist},
    {"find_by_id", og_find_by_id},
    {"query_passable", og_query_passable},
    {"query_grid_passable", og_query_grid_passable},
    {"query_object_passable", og_query_object_passable},
    {"query_genre", og_query_genre},
    {"cosmetic_rand", og_cosmetic_rand},
    {"level_id", og_level_id},
    {"level_tick", og_level_tick},
    {"world_tick", og_world_tick},
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
    {"campaign_var", og_campaign_var},
    // Scripted-mode (TYPE_SCRIPTED) surface.
    {"end_level", og_end_level},
    {"declare_winner", og_declare_winner},
    {"mode_winner", og_mode_winner},
    {"mode_get", og_mode_get},
    {"mode_set", og_mode_set},
    {"set_mode_name", og_set_mode_name},
    {"set_hud_line", og_set_hud_line},
    {"clear_hud_line", og_clear_hud_line},
    {"set_beacon", og_set_beacon},
    {"team_score", og_team_score},
    {"team_color_name", og_team_color_name},
    {"match_setting", og_match_setting},
    {"authored_team_mask", og_authored_team_mask},
    {"effective_team_mask", og_effective_team_mask},
    {"respawn_schedule", og_respawn_schedule},
    {"respawn_pending", og_respawn_pending},
    {"respawn_pending_count", og_respawn_pending_count},
    {"respawn_anchor_count", og_respawn_anchor_count},
    {"respawn_anchor", og_respawn_anchor},
    {"spawn_spot_clear", og_spawn_spot_clear},
    {"scrub_corpse_stain", og_scrub_corpse_stain},
    // Deterministic arithmetic/branch helpers; definitions and
    // exact-semantics contracts are above.
    {"rand0", og_rand0},
    {"max", og_max},
    {"min", og_min},
    {"clamp", og_clamp},
    {"sign", og_sign},
    // Per-family tuning constants.
    {"tuning", og_tuning},
    {nullptr, nullptr},
};

// og.combat.* — installed as a subtable of og by
// install_entity_bindings_into_og, one entry per bound og::combat helper.
const luaL_Reg kOgCombatFuncs[] = {
    {"yell_radius", og_combat_yell_radius},
    {"stun_total", og_combat_stun_total},
    {"bomb_damage", og_combat_bomb_damage},
    {"cloak_total", og_combat_cloak_total},
    {"glow_bonus", og_combat_glow_bonus},
    {"druid_faerie_lifetime", og_combat_druid_faerie_lifetime},
    {"skeleton_lifetime", og_combat_skeleton_lifetime},
    {"ghost_raise_lifetime", og_combat_ghost_raise_lifetime},
    {nullptr, nullptr},
};

// og.campaign_* — the menu-time campaign surface. Every entry self-fences
// on VmState::campaign_dispatch (campaign_dispatch_arg), so the table is
// installed WITHOUT the load fence: the load fence would close it during
// the one context it exists for. Parsed by scripts/modding/gen_api_stubs.py
// by literal name.
const luaL_Reg kOgCampaignFuncs[] = {
    {"campaign_state_get", og_campaign_state_get},
    {"campaign_state_set", og_campaign_state_set},
    {"campaign_gold", og_campaign_gold},
    {"campaign_spend_gold", og_campaign_spend_gold},
    {"campaign_grant_gold", og_campaign_grant_gold},
    {"campaign_team", og_campaign_team},
    {"campaign_level_completed", og_campaign_level_completed},
    {"campaign_current_level", og_campaign_current_level},
    {"campaign_scenario_title", og_campaign_scenario_title},
    {"campaign_match_get", og_campaign_match_get},
    {"campaign_match_set", og_campaign_match_set},
    {"campaign_is_host", og_campaign_is_host},
    {"campaign_random", og_campaign_random},
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
    // Team start markers (mode init consumes/strips them by team).
    {"ORDER_SPECIAL", static_cast<lua_Integer>(Order::Special)},
    {"FAMILY_RESERVED_TEAM", FAMILY_RESERVED_TEAM},
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
    {"COMMAND_GOTO", COMMAND_GOTO},
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
    // Facings (walker::curdir)
    {"FACE_UP", FACE_UP},
    {"FACE_UP_RIGHT", FACE_UP_RIGHT},
    {"FACE_RIGHT", FACE_RIGHT},
    {"FACE_DOWN_RIGHT", FACE_DOWN_RIGHT},
    {"FACE_DOWN", FACE_DOWN},
    {"FACE_DOWN_LEFT", FACE_DOWN_LEFT},
    {"FACE_LEFT", FACE_LEFT},
    {"FACE_UP_LEFT", FACE_UP_LEFT},
    {"NUM_FACINGS", NUM_FACINGS},
    // Terrain genres (og.query_genre)
    {"TYPE_GRASS", TYPE_GRASS},
    {"TYPE_WATER", TYPE_WATER},
    {"TYPE_TREES", TYPE_TREES},
    {"TYPE_DIRT", TYPE_DIRT},
    {"TYPE_COBBLE", TYPE_COBBLE},
    {"TYPE_GRASS_DARK", TYPE_GRASS_DARK},
    {"TYPE_DIRT_DARK", TYPE_DIRT_DARK},
    {"TYPE_WALL", TYPE_WALL},
    {"TYPE_CARPET", TYPE_CARPET},
    {"TYPE_GRASS_LIGHT", TYPE_GRASS_LIGHT},
    {"TYPE_AIR", TYPE_AIR},
    {"TYPE_GLASS", TYPE_GLASS},
    {"TYPE_DROP_BLOCK", TYPE_DROP_BLOCK},
    {"TYPE_ZSTAIRS", TYPE_ZSTAIRS},
    {"TYPE_SNOW", TYPE_SNOW},
    {"TYPE_LAVA", TYPE_LAVA},
    {"TYPE_MARSH", TYPE_MARSH},
    {"TYPE_ASH", TYPE_ASH},
    {"TYPE_UNKNOWN", TYPE_UNKNOWN},
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
    {"STARBURST_ADD_CAP", og::combat::kStarburstAddCap},
    {"MACE_LIFE_CAP", og::combat::kMaceLifeCap},
    {"SPRINKLE_REFRESH_OWNER_LEVEL", og::combat::kSprinkleRefreshOwnerLevel},
    {"SPRINKLE_REFRESH_FLOOR", og::combat::kSprinkleRefreshFloor},
    // Misc
    {"NUM_SPECIALS", NUM_SPECIALS},
    {"MAXOBS", MAXOBS},
    {"SCORE_TEAM_COUNT", SCORE_TEAM_COUNT},
    {"FFA_TEAM_BASE", kFfaTeamBase},
    {"FFA_TEAM_COUNT", kFfaTeamCount},
};

// ---------------------------------------------------------------------------
// The load-time fence
// ---------------------------------------------------------------------------
//
// The world API is DISPATCH-time only. A pack chunk's top level runs while
// the VM is being built — during the declaration pass, and again in every
// bind replay — and a world call there is never what the author meant: it
// asks the world a question at a moment the answer is either unavailable or,
// worse, available on ONE peer.
//
// og.rand is the case that proves it. It used to fail at chunk load by
// accident, because current_game is null while the boot-time VM is built;
// but GameWorld::scripts() rebuilds a VM at the first dispatch after a mount
// change, with a live world, and a top-level og.rand there would have
// SUCCEEDED and pulled the sim's stream out from under the other peers. The
// fence makes "no world at chunk-load time" a property of load time rather
// than of which VM happens to have no world.
//
// Deliberately NOT fenced: og.max/min/clamp/sign (pure branch helpers over
// their arguments — a pack may reasonably fold a constant with them at load
// time), og.div/mod/f*/i*/trunc/log (the sandbox arithmetic, which never
// reached the world), and og.combat.* (draw-free formulas). Everything else
// in the world table either needs a live world or a dispatch-scoped handle,
// so fencing it costs a correct pack nothing.
constexpr const char* kUnfencedWorldFuncs[] = {"max", "min", "clamp", "sign"};

bool is_unfenced(const char* name)
{
    for (const char* n : kUnfencedWorldFuncs) {
        if (std::strcmp(n, name) == 0)
            return true;
    }
    return false;
}

// upvalue 1: the real binding (a light C function), 2: the VmState,
// 3: its og.* name. Calling the inner function DIRECTLY (rather than through
// lua_call) keeps the Lua stack frame identical, so argument indices,
// luaL_where levels and error positions are exactly what they were before
// the fence existed.
int load_fenced_call(lua_State* L)
{
    const auto* st =
        static_cast<VmState*>(lua_touserdata(L, lua_upvalueindex(2)));
    // The campaign-dispatch fence shares this closure: a campaign hook runs
    // in the world VM (in the SDL client, WITH a live world and the
    // replicated sim RNG), so the world API erroring there is what keeps a
    // menu script from silently perturbing the sim
    // (docs/campaign-scripting-design.md).
    if (st != nullptr && st->campaign_dispatch) {
        return luaL_error(
            L, "og.%s: the world API is not available during campaign hooks",
            lua_tostring(L, lua_upvalueindex(3)));
    }
    if (st != nullptr && st->loading) {
        return luaL_error(
            L,
            "og.%s: the world API is dispatch-time only — a pack chunk's top "
            "level runs before there is a world to ask (bind hooks here and "
            "call it from them)",
            lua_tostring(L, lua_upvalueindex(3)));
    }
    return lua_tocfunction(L, lua_upvalueindex(1))(L);
}

}  // namespace

void fence_world_entry(lua_State* L, VmState* st, const char* name)
{
    lua_getfield(L, -1, name);
    lua_pushlightuserdata(L, st);
    lua_pushstring(L, name);
    lua_pushcclosure(L, load_fenced_call, 3);
    lua_setfield(L, -2, name);
}

void install_entity_bindings(lua_State* L, VmState* st)
{
    // Walker method table (metatable __index created by
    // ensure_handle_metatables; fill it here).
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->walker_methods_ref);
    luaL_setfuncs(L, kWalkerMethods, 0);
    lua_pop(L, 1);

    // Property layer: swap the metatable's plain-table __index for the
    // property-aware closures built from kWalkerProperties (the shared
    // registration table — see the property-layer comment above). The
    // method table is the __index closure's FIRST upvalue and is consulted
    // first, so every method keeps resolving exactly as the plain table
    // served it — method-first is what keeps `self:busy()` working when a
    // property shares the name.
    luaL_getmetatable(L, kWalkerMeta);  // mt
    lua_newtable(L);                    // mt getters
    lua_newtable(L);                    // mt getters setters
    for (const WalkerProperty& p : kWalkerProperties) {
        lua_pushcfunction(L, p.getter);
        lua_setfield(L, -3, p.name);
        if (p.setter != nullptr) {
            lua_pushcfunction(L, p.setter);
            lua_setfield(L, -2, p.name);
        }
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->walker_methods_ref);
    lua_pushvalue(L, -3);  // getters
    lua_pushcclosure(L, walker_index, 2);
    lua_setfield(L, -4, "__index");
    lua_pushvalue(L, -1);  // setters
    lua_pushvalue(L, -3);  // getters
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->walker_methods_ref);
    lua_pushcclosure(L, walker_newindex, 3);
    lua_setfield(L, -4, "__newindex");
    lua_pop(L, 3);  // setters, getters, mt

    // Guy handle methods.
    luaL_getmetatable(L, kGuyMeta);
    lua_newtable(L);
    luaL_setfuncs(L, kGuyMethods, 0);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

void install_entity_bindings_into_og(lua_State* L, VmState* st)
{
    // Expects the og table at the top of the stack.
    luaL_setfuncs(L, kOgWorldFuncs, 0);
    for (const luaL_Reg* r = kOgWorldFuncs; r->name != nullptr; r++) {
        if (is_unfenced(r->name))
            continue;
        fence_world_entry(L, st, r->name);
    }
    // Campaign bindings self-fence (campaign-hook only) — no load fence.
    luaL_setfuncs(L, kOgCampaignFuncs, 0);
    lua_newtable(L);
    luaL_setfuncs(L, kOgCombatFuncs, 0);
    lua_setfield(L, -2, "combat");
    lua_newtable(L);
    for (const NamedConst& c : kConstants) {
        lua_pushinteger(L, c.value);
        lua_setfield(L, -2, c.name);
    }
    lua_setfield(L, -2, "C");
}

}  // namespace og::script
