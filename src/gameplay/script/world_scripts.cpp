/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/test_trace.h>

#include "script_internal.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace og::script {

namespace {

// ---------------------------------------------------------------------------
// Hook name tables (per order)
// ---------------------------------------------------------------------------

struct HookName {
    const char* name;
    FamilyHook hook;
};

constexpr HookName kLivingHooks[] = {
    {"do_special", FamilyHook::DoSpecial},
    {"check_special_ai", FamilyHook::CheckSpecialAi},
    {"hit_response", FamilyHook::HitResponse},
    {"set_difficulty", FamilyHook::SetDifficulty},
    {"level_up", FamilyHook::LevelUp},
    {"on_death", FamilyHook::OnDeath},
    {"on_act_living", FamilyHook::OnActLiving},
    {"on_shoved", FamilyHook::OnShoved},
    {"on_fire_weapon", FamilyHook::OnFireWeapon},
    {"handle_teleport", FamilyHook::HandleTeleport},
    {"on_create", FamilyHook::OnCreate},
    {"customize_weapon", FamilyHook::CustomizeWeapon},
    {"on_ani_complete", FamilyHook::OnAniComplete},
    {"on_melee_hit", FamilyHook::OnMeleeHit},
};

constexpr HookName kWeaponHooks[] = {
    {"on_death", FamilyHook::WeaponOnDeath},
    {"on_animate", FamilyHook::WeaponOnAnimate},
    {"on_hit_target", FamilyHook::WeaponOnHitTarget},
};

constexpr HookName kEffectHooks[] = {
    {"on_act", FamilyHook::EffectOnAct},
    {"on_death", FamilyHook::EffectOnDeath},
};

constexpr HookName kTreasureHooks[] = {
    {"on_eat", FamilyHook::TreasureOnEat},
};

constexpr HookName kGeneratorHooks[] = {
    {"customize_spawn", FamilyHook::GeneratorCustomizeSpawn},
};

struct OrderInfo {
    const char* name;
    Order order;
    const HookName* hooks;
    size_t hook_count;
};

constexpr OrderInfo kOrders[] = {
    {"living", Order::Living, kLivingHooks, std::size(kLivingHooks)},
    {"weapon", Order::Weapon, kWeaponHooks, std::size(kWeaponHooks)},
    {"treasure", Order::Treasure, kTreasureHooks, std::size(kTreasureHooks)},
    {"generator", Order::Generator, kGeneratorHooks,
     std::size(kGeneratorHooks)},
    {"fx", Order::FX, kEffectHooks, std::size(kEffectHooks)},
    {"effect", Order::FX, kEffectHooks, std::size(kEffectHooks)},  // alias
};

const OrderInfo* find_order(const char* name)
{
    for (const auto& oi : kOrders)
        if (std::strcmp(oi.name, name) == 0)
            return &oi;
    return nullptr;
}

// Family string-id resolution ("core:soldier" → family byte) lives in
// og::families::resolve_family_string_id (families/family_string_ids.h),
// shared with the classpack.yaml exporter and reader.

}  // namespace

// ---------------------------------------------------------------------------
// Entity handles (shared with bindings via script_internal.h)
// ---------------------------------------------------------------------------

struct WorldScriptsDetail {
    VmState state;
};

VmState* get_vm_state(lua_State* L)
{
    lua_pushstring(L, "og.vmstate");
    lua_rawget(L, LUA_REGISTRYINDEX);
    VmState* st = nullptr;
    if (lua_islightuserdata(L, -1))
        st = static_cast<VmState*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return st;
}

walker* resolve_walker(lua_State* L, int idx, bool required)
{
    auto* h = static_cast<WalkerHandle*>(
        luaL_checkudata(L, idx, kWalkerMeta));
    walker* w = nullptr;
    if (h->entity_id != 0 && current_game != nullptr &&
        current_game->world != nullptr) {
        w = current_game->world->find_by_id(h->entity_id);
    }
    if (w == nullptr && h->raw != nullptr) {
        // Dying entities (on_death and friends) may already be out of the id
        // index; the handle stays valid for the dispatch that produced it.
        VmState* st = get_vm_state(L);
        if (st != nullptr && h->gen == st->dispatch_gen)
            w = h->raw;
    }
    if (w == nullptr && required)
        luaL_error(L, "stale or dead entity handle");
    return w;
}

walker* resolve_walker_or_nil(lua_State* L, int idx)
{
    if (lua_isnoneornil(L, idx))
        return nullptr;
    return resolve_walker(L, idx, /*required=*/true);
}

guy* resolve_guy(lua_State* L, int idx, bool required)
{
    auto* h = static_cast<GuyHandle*>(luaL_checkudata(L, idx, kGuyMeta));
    guy* g = nullptr;
    VmState* st = get_vm_state(L);
    if (st != nullptr && h->gen == st->dispatch_gen)
        g = h->raw;
    if (g == nullptr && required)
        luaL_error(L, "stale guy handle (guys are dispatch-scoped)");
    return g;
}

void push_walker_handle(lua_State* L, walker* w, std::uint64_t gen)
{
    if (w == nullptr) {
        lua_pushnil(L);
        return;
    }
    auto* h = static_cast<WalkerHandle*>(
        lua_newuserdatauv(L, sizeof(WalkerHandle), 0));
    h->raw = w;
    h->entity_id = w->entity_id();
    h->gen = gen;
    luaL_setmetatable(L, kWalkerMeta);
}

std::uint64_t current_dispatch_gen(lua_State* L)
{
    VmState* st = get_vm_state(L);
    return st != nullptr ? st->dispatch_gen : 0;
}

namespace {

int walker_eq(lua_State* L)
{
    auto* a = static_cast<WalkerHandle*>(luaL_checkudata(L, 1, kWalkerMeta));
    auto* b = static_cast<WalkerHandle*>(luaL_checkudata(L, 2, kWalkerMeta));
    bool eq;
    if (a->entity_id != 0 || b->entity_id != 0)
        eq = (a->entity_id == b->entity_id);
    else
        eq = (a->raw == b->raw && a->gen == b->gen);
    lua_pushboolean(L, eq ? 1 : 0);
    return 1;
}

int walker_tostring(lua_State* L)
{
    auto* h = static_cast<WalkerHandle*>(luaL_checkudata(L, 1, kWalkerMeta));
    lua_pushfstring(L, "entity#%d", static_cast<int>(h->entity_id));
    return 1;
}

void ensure_handle_metatables(lua_State* L, VmState* st)
{
    if (luaL_newmetatable(L, kWalkerMeta)) {
        lua_pushcfunction(L, walker_eq);
        lua_setfield(L, -2, "__eq");
        lua_pushcfunction(L, walker_tostring);
        lua_setfield(L, -2, "__tostring");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");
        // __index: the walker method table (populated by the binding layer).
        lua_newtable(L);
        lua_pushvalue(L, -1);
        st->walker_methods_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        lua_setfield(L, -2, "__index");
    }
    lua_pop(L, 1);
    if (luaL_newmetatable(L, kGuyMeta)) {
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
}

// ---------------------------------------------------------------------------
// og.register_hooks
// ---------------------------------------------------------------------------

lua_Integer hook_table_key(Order order, int family_id)
{
    return static_cast<lua_Integer>(order) * 4096 + family_id;
}

int og_register_hooks(lua_State* L)
{
    const char* order_str = luaL_checkstring(L, 1);
    const char* family_str = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);

    const OrderInfo* oi = find_order(order_str);
    if (oi == nullptr)
        return luaL_error(L, "og.register_hooks: unknown order '%s'",
                          order_str);
    const int family_id =
        og::families::resolve_family_string_id(oi->order, family_str);
    if (family_id < 0)
        return luaL_error(L, "og.register_hooks: unknown %s family '%s'",
                          order_str, family_str);

    VmState* st = get_vm_state(L);
    if (st == nullptr || st->owner == nullptr)
        return luaL_error(L, "og.register_hooks: no world scripts active");

    lua_rawgeti(L, LUA_REGISTRYINDEX, st->hooks_ref);  // hooks root
    lua_rawgeti(L, -1, hook_table_key(oi->order, family_id));
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, hook_table_key(oi->order, family_id));
    }
    // Stack: hooks_root, family_tbl. Walk the provided table with the known
    // hook-name list (no pairs(): iteration order must not matter and the
    // sandbox has no pairs anyway).
    int registered = 0;
    for (size_t i = 0; i < oi->hook_count; i++) {
        lua_getfield(L, 3, oi->hooks[i].name);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        if (!lua_isfunction(L, -1))
            return luaL_error(L, "og.register_hooks: '%s' must be a function",
                              oi->hooks[i].name);
        lua_rawseti(L, -2, static_cast<lua_Integer>(oi->hooks[i].hook));
        st->owner->note_hook(oi->order, family_id, oi->hooks[i].hook);
        registered++;
    }
    lua_pop(L, 2);
    if (registered == 0)
        return luaL_error(
            L, "og.register_hooks: no valid hooks for %s '%s' (check names)",
            order_str, family_str);
    return 0;
}

// og.rand(n) → deterministic sim RNG (world-owned). n must be > 0.
int og_rand(lua_State* L)
{
    const lua_Integer n = luaL_checkinteger(L, 1);
    if (n <= 0)
        return luaL_error(L, "og.rand: n must be positive");
    if (current_game == nullptr || current_game->world == nullptr)
        return luaL_error(L, "og.rand: no active world");
    lua_pushinteger(L, static_cast<lua_Integer>(
                           current_game->world->rng_.next(
                               static_cast<std::int32_t>(n))));
    return 1;
}

// og.is_alive(handle) → entity still resolvable (and not flagged dead).
int og_is_alive(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/false);
    lua_pushboolean(L, (w != nullptr && w->dead() == 0) ? 1 : 0);
    return 1;
}

// og.entity_id(handle) → stable sim entity id (0 for untracked).
int og_entity_id(lua_State* L)
{
    auto* h = static_cast<WalkerHandle*>(
        luaL_checkudata(L, 1, kWalkerMeta));
    lua_pushinteger(L, static_cast<lua_Integer>(h->entity_id));
    return 1;
}

// ---------------------------------------------------------------------------
// Level-script hooks
// ---------------------------------------------------------------------------

constexpr lua_Integer kLevelKeyStride = 65536;

lua_Integer level_hook_key(LevelHook kind, int level_id)
{
    // level_id -1 = wildcard (all levels), stored in the +0 slot; concrete
    // levels at +(id+1).
    const lua_Integer level_part =
        level_id < 0 ? 0 : static_cast<lua_Integer>(level_id) + 1;
    return static_cast<lua_Integer>(kind) * kLevelKeyStride + level_part;
}

struct LevelHookName {
    const char* name;
    LevelHook kind;
};

constexpr LevelHookName kLevelHookNames[] = {
    {"on_load", LevelHook::Load},
    {"on_tick", LevelHook::Tick},
    {"on_entity_death", LevelHook::EntityDeath},
    {"on_entity_spawn", LevelHook::EntitySpawn},
};

// og.register_level_hooks(level_id, { on_load=, on_tick=, on_entity_death=,
// on_entity_spawn= }). level_id -1 registers for every level.
int og_register_level_hooks(lua_State* L)
{
    const int level_id = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);
    VmState* st = get_vm_state(L);
    if (st == nullptr || st->owner == nullptr)
        return luaL_error(L, "og.register_level_hooks: no world scripts");

    lua_rawgeti(L, LUA_REGISTRYINDEX, st->level_hooks_ref);
    int registered = 0;
    for (const auto& h : kLevelHookNames) {
        lua_getfield(L, 2, h.name);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        if (!lua_isfunction(L, -1))
            return luaL_error(L, "og.register_level_hooks: '%s' must be a "
                                 "function", h.name);
        lua_rawseti(L, -2, level_hook_key(h.kind, level_id));
        st->level_hook_kinds |= 1u << static_cast<unsigned>(h.kind);
        registered++;
    }
    lua_pop(L, 1);
    if (registered == 0)
        return luaL_error(
            L, "og.register_level_hooks: no valid hooks (check names)");
    return 0;
}

// og.set_entity_hooks(handle, { on_death = fn }) — per-entity overrides,
// registered from a level script (typically in on_load after finding the
// entity). Cleared automatically when the entity's death hook fires.
int og_set_entity_hooks(lua_State* L)
{
    auto* h = static_cast<WalkerHandle*>(
        luaL_checkudata(L, 1, kWalkerMeta));
    luaL_checktype(L, 2, LUA_TTABLE);
    if (h->entity_id == 0)
        return luaL_error(L, "og.set_entity_hooks: entity is untracked");
    VmState* st = get_vm_state(L);
    if (st == nullptr)
        return luaL_error(L, "og.set_entity_hooks: no world scripts");

    lua_rawgeti(L, LUA_REGISTRYINDEX, st->entity_hooks_ref);
    lua_newtable(L);
    lua_getfield(L, 2, "on_death");
    if (!lua_isnil(L, -1) && !lua_isfunction(L, -1))
        return luaL_error(L, "og.set_entity_hooks: 'on_death' must be a "
                             "function");
    lua_rawseti(L, -2, static_cast<lua_Integer>(LevelHook::EntityDeath));
    lua_rawseti(L, -2, static_cast<lua_Integer>(h->entity_id));
    lua_pop(L, 1);
    st->has_entity_hooks = true;
    return 0;
}

// og.family_id(order, family_str) → wire byte (tests/diagnostics; also lets
// scripts compare walker:family() against named families).
int og_family_id(lua_State* L)
{
    const char* order_str = luaL_checkstring(L, 1);
    const char* family_str = luaL_checkstring(L, 2);
    const OrderInfo* oi = find_order(order_str);
    if (oi == nullptr)
        return luaL_error(L, "og.family_id: unknown order '%s'", order_str);
    const int id =
        og::families::resolve_family_string_id(oi->order, family_str);
    if (id < 0)
        lua_pushnil(L);
    else
        lua_pushinteger(L, id);
    return 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// WorldScripts
// ---------------------------------------------------------------------------

WorldScripts::WorldScripts() : host_(std::make_unique<ScriptHost>())
{
    ScriptHost::Impl& impl = host_->impl();
    lua_State* L = impl.L;

    detail_ = std::make_unique<WorldScriptsDetail>();
    detail_->state.owner = this;
    lua_pushstring(L, "og.vmstate");
    lua_pushlightuserdata(L, &detail_->state);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_newtable(L);
    detail_->state.hooks_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    detail_->state.level_hooks_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    detail_->state.entity_hooks_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    ensure_handle_metatables(L, &detail_->state);
    install_entity_bindings(L, &detail_->state);

    // Extend the sandbox og table with world-facing entry points.
    impl.push_sandbox_root();
    lua_getfield(L, -1, "og");
    install_entity_bindings_into_og(L);
    lua_pushcfunction(L, og_register_hooks);
    lua_setfield(L, -2, "register_hooks");
    lua_pushcfunction(L, og_register_level_hooks);
    lua_setfield(L, -2, "register_level_hooks");
    lua_pushcfunction(L, og_set_entity_hooks);
    lua_setfield(L, -2, "set_entity_hooks");
    lua_pushcfunction(L, og_rand);
    lua_setfield(L, -2, "rand");
    lua_pushcfunction(L, og_family_id);
    lua_setfield(L, -2, "family_id");
    lua_pushcfunction(L, og_is_alive);
    lua_setfield(L, -2, "is_alive");
    lua_pushcfunction(L, og_entity_id);
    lua_setfield(L, -2, "entity_id");
    lua_pop(L, 2);

    // Replay pack scripts (registration order; env shared per pack).
    built_generation_ = pack_scripts_generation();
    for (const PackScript& ps : pack_scripts())
        host_->run_chunk(ps.chunk_name, ps.source, ps.pack_id);
}

WorldScripts::~WorldScripts() = default;

ScriptHost& WorldScripts::host()
{
    return *host_;
}

const ScriptHost& WorldScripts::host() const
{
    return *host_;
}

bool WorldScripts::has_hook(Order order, int family_id, FamilyHook hook) const
{
    const auto oi = static_cast<size_t>(order);
    if (oi >= masks_.size() || family_id < 0 || family_id > 255)
        return false;
    return (masks_[oi][static_cast<size_t>(family_id)] &
            (1u << static_cast<unsigned>(hook))) != 0;
}

void WorldScripts::note_hook(Order order, int family_id, FamilyHook hook)
{
    const auto oi = static_cast<size_t>(order);
    if (oi >= masks_.size() || family_id < 0 || family_id > 255)
        return;
    masks_[oi][static_cast<size_t>(family_id)] |=
        (1u << static_cast<unsigned>(hook));
}

WorldScripts& active_world_scripts()
{
    if (current_game != nullptr && current_game->world != nullptr)
        return current_game->world->scripts();
    // Picker-side hooks (level_up/promotions) run with no world; a shared
    // instance rebuilt on pack changes serves them.
    static std::unique_ptr<WorldScripts> ui;
    if (ui == nullptr || ui->built_generation() != pack_scripts_generation())
        ui = std::make_unique<WorldScripts>();
    return *ui;
}

// ---------------------------------------------------------------------------
// Hook dispatch
// ---------------------------------------------------------------------------

namespace {

// RAII frame for one hook invocation: locates the hook function, pushes
// arguments, pcalls under the host budget, and converts the result.
class HookFrame {
public:
    explicit HookFrame(WorldScripts& ws)
        : ws_(ws), impl_(ws.host().impl()), L_(impl_.L)
    {
    }

    bool begin(Order order, int family_id, FamilyHook hook)
    {
        VmState* st = get_vm_state(L_);
        if (st == nullptr)
            return false;
        st->dispatch_gen++;
        gen_ = st->dispatch_gen;
        lua_rawgeti(L_, LUA_REGISTRYINDEX, st->hooks_ref);
        lua_rawgeti(L_, -1, hook_table_key(order, family_id));
        if (!lua_istable(L_, -1)) {
            lua_pop(L_, 2);
            return false;
        }
        lua_rawgeti(L_, -1, static_cast<lua_Integer>(hook));
        if (!lua_isfunction(L_, -1)) {
            lua_pop(L_, 3);
            return false;
        }
        lua_remove(L_, -2);  // family table
        lua_remove(L_, -2);  // hooks root
        nargs_ = 0;
        return true;
    }

    void arg(walker* w)
    {
        push_walker_handle(L_, w, gen_);
        nargs_++;
    }

    void arg(guy* g)
    {
        if (g == nullptr) {
            lua_pushnil(L_);
        } else {
            auto* h = static_cast<GuyHandle*>(
                lua_newuserdatauv(L_, sizeof(GuyHandle), 0));
            h->raw = g;
            h->gen = gen_;
            luaL_setmetatable(L_, kGuyMeta);
        }
        nargs_++;
    }

    void arg(lua_Integer v)
    {
        lua_pushinteger(L_, v);
        nargs_++;
    }

    // Runs the hook. Returns nullopt on script error (R9: treat as absent),
    // otherwise the boolean-coerced result (void hooks: true).
    std::optional<bool> call(const char* where, bool wants_result)
    {
        if (!impl_.protected_call(where, nargs_, wants_result ? 1 : 0))
            return std::nullopt;
        bool result = true;
        if (wants_result) {
            result = lua_toboolean(L_, -1) != 0;
            lua_pop(L_, 1);
        }
        return result;
    }

private:
    WorldScripts& ws_;
    ScriptHost::Impl& impl_;
    lua_State* L_;
    std::uint64_t gen_ = 0;
    int nargs_ = 0;
};

const char* hook_where(FamilyHook hook)
{
    switch (hook) {
        case FamilyHook::DoSpecial: return "hook:do_special";
        case FamilyHook::CheckSpecialAi: return "hook:check_special_ai";
        case FamilyHook::HitResponse: return "hook:hit_response";
        case FamilyHook::SetDifficulty: return "hook:set_difficulty";
        case FamilyHook::LevelUp: return "hook:level_up";
        case FamilyHook::OnDeath: return "hook:on_death";
        case FamilyHook::OnActLiving: return "hook:on_act_living";
        case FamilyHook::OnShoved: return "hook:on_shoved";
        case FamilyHook::OnFireWeapon: return "hook:on_fire_weapon";
        case FamilyHook::HandleTeleport: return "hook:handle_teleport";
        case FamilyHook::OnCreate: return "hook:on_create";
        case FamilyHook::CustomizeWeapon: return "hook:customize_weapon";
        case FamilyHook::OnAniComplete: return "hook:on_ani_complete";
        case FamilyHook::OnMeleeHit: return "hook:on_melee_hit";
        case FamilyHook::WeaponOnDeath: return "hook:weapon.on_death";
        case FamilyHook::WeaponOnAnimate: return "hook:weapon.on_animate";
        case FamilyHook::WeaponOnHitTarget: return "hook:weapon.on_hit_target";
        case FamilyHook::EffectOnAct: return "hook:effect.on_act";
        case FamilyHook::EffectOnDeath: return "hook:effect.on_death";
        case FamilyHook::TreasureOnEat: return "hook:treasure.on_eat";
        case FamilyHook::GeneratorCustomizeSpawn:
            return "hook:generator.customize_spawn";
        default: return "hook:?";
    }
}

// Shared shape: try script hook with walker args, no C++ fallback here.
template <typename... Args>
std::optional<bool> try_script_hook(Order order, int family_id,
                                    FamilyHook hook, bool wants_result,
                                    Args... args)
{
    WorldScripts& ws = active_world_scripts();
    if (!ws.has_hook(order, family_id, hook))
        return std::nullopt;
    HookFrame f(ws);
    if (!f.begin(order, family_id, hook))
        return std::nullopt;
    (f.arg(args), ...);
    return f.call(hook_where(hook), wants_result);
}

}  // namespace

namespace hooks {

std::optional<bool> do_special(const FamilyDescriptor* fd, walker* self)
{
    if (fd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::DoSpecial, true, self))
        return r;
    if (fd->do_special != nullptr)
        return fd->do_special(self);
    return std::nullopt;
}

std::optional<bool> check_special_ai(const FamilyDescriptor* fd, living* self)
{
    if (fd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::CheckSpecialAi, true,
                                 static_cast<walker*>(self)))
        return r;
    if (fd->check_special_ai != nullptr)
        return fd->check_special_ai(self);
    return std::nullopt;
}

bool hit_response(const FamilyDescriptor* fd, statistics* stats, walker* who)
{
    if (fd == nullptr || stats == nullptr)
        return false;
    // Lua signature: hit_response(self, foe) — self is the stats' owner.
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::HitResponse,
                        false, stats->controller(), who))
        return true;
    if (fd->hit_response != nullptr) {
        fd->hit_response(stats, who);
        return true;
    }
    return false;
}

bool set_difficulty(const FamilyDescriptor* fd, living* self,
                    std::uint32_t level)
{
    if (fd == nullptr)
        return false;
    if (try_script_hook(Order::Living, fd->family_id,
                        FamilyHook::SetDifficulty, false,
                        static_cast<walker*>(self),
                        static_cast<lua_Integer>(level)))
        return true;
    if (fd->set_difficulty != nullptr) {
        fd->set_difficulty(self, level);
        return true;
    }
    return false;
}

bool level_up(const FamilyDescriptor* fd, guy* self, std::int32_t level_diff)
{
    if (fd == nullptr)
        return false;
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::LevelUp,
                        false, self, static_cast<lua_Integer>(level_diff)))
        return true;
    if (fd->level_up != nullptr) {
        fd->level_up(self, level_diff);
        return true;
    }
    return false;
}

std::optional<bool> on_death(const FamilyDescriptor* fd, walker* self)
{
    if (fd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::OnDeath, true, self))
        return r;
    if (fd->on_death != nullptr)
        return fd->on_death(self);
    return std::nullopt;
}

bool on_act_living(const FamilyDescriptor* fd, living* self)
{
    if (fd == nullptr)
        return false;
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnActLiving,
                        false, static_cast<walker*>(self)))
        return true;
    if (fd->on_act_living != nullptr) {
        fd->on_act_living(self);
        return true;
    }
    return false;
}

bool on_shoved(const FamilyDescriptor* fd, walker* target)
{
    if (fd == nullptr)
        return false;
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnShoved,
                        false, target))
        return true;
    if (fd->on_shoved != nullptr) {
        fd->on_shoved(target);
        return true;
    }
    return false;
}

std::optional<bool> on_fire_weapon(const FamilyDescriptor* fd, walker* self,
                                   walker* weapon)
{
    if (fd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::OnFireWeapon, true, self,
                                 weapon))
        return r;
    if (fd->on_fire_weapon != nullptr)
        return fd->on_fire_weapon(self, weapon);
    return std::nullopt;
}

std::optional<bool> handle_teleport(const FamilyDescriptor* fd, walker* self)
{
    if (fd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::HandleTeleport, true, self))
        return r;
    if (fd->handle_teleport != nullptr)
        return fd->handle_teleport(self);
    return std::nullopt;
}

bool on_create(const FamilyDescriptor* fd, walker* self)
{
    if (fd == nullptr)
        return false;
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnCreate,
                        false, self))
        return true;
    if (fd->on_create != nullptr) {
        fd->on_create(self);
        return true;
    }
    return false;
}

bool customize_weapon(const FamilyDescriptor* fd, walker* self, walker* weapon)
{
    if (fd == nullptr)
        return false;
    if (try_script_hook(Order::Living, fd->family_id,
                        FamilyHook::CustomizeWeapon, false, self, weapon))
        return true;
    if (fd->customize_weapon != nullptr) {
        fd->customize_weapon(self, weapon);
        return true;
    }
    return false;
}

std::optional<bool> on_ani_complete(const FamilyDescriptor* fd, walker* self)
{
    if (fd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::OnAniComplete, true, self))
        return r;
    if (fd->on_ani_complete != nullptr)
        return fd->on_ani_complete(self);
    return std::nullopt;
}

bool on_melee_hit(const FamilyDescriptor* fd, walker* self, walker* target)
{
    if (fd == nullptr)
        return false;
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnMeleeHit,
                        false, self, target))
        return true;
    if (fd->on_melee_hit != nullptr) {
        fd->on_melee_hit(self, target);
        return true;
    }
    return false;
}

std::optional<bool> weapon_on_death(const WeaponFamilyDescriptor* wfd,
                                    weap* self)
{
    if (wfd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Weapon, wfd->family_id,
                                 FamilyHook::WeaponOnDeath, true,
                                 static_cast<walker*>(self)))
        return r;
    if (wfd->on_death != nullptr)
        return wfd->on_death(self);
    return std::nullopt;
}

std::optional<bool> weapon_on_animate(const WeaponFamilyDescriptor* wfd,
                                      weap* self)
{
    if (wfd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Weapon, wfd->family_id,
                                 FamilyHook::WeaponOnAnimate, true,
                                 static_cast<walker*>(self)))
        return r;
    if (wfd->on_animate != nullptr)
        return wfd->on_animate(self);
    return std::nullopt;
}

bool weapon_on_hit_target(const WeaponFamilyDescriptor* wfd, walker* weapon,
                          walker* target, walker* owner)
{
    if (wfd == nullptr)
        return false;
    if (try_script_hook(Order::Weapon, wfd->family_id,
                        FamilyHook::WeaponOnHitTarget, false, weapon, target,
                        owner))
        return true;
    if (wfd->on_hit_target != nullptr) {
        wfd->on_hit_target(weapon, target, owner);
        return true;
    }
    return false;
}

std::optional<bool> effect_on_act(const EffectFamilyDescriptor* efd,
                                  effect* self)
{
    if (efd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::FX, efd->family_id,
                                 FamilyHook::EffectOnAct, true,
                                 static_cast<walker*>(self)))
        return r;
    if (efd->on_act != nullptr)
        return efd->on_act(self);
    return std::nullopt;
}

std::optional<bool> effect_on_death(const EffectFamilyDescriptor* efd,
                                    effect* self)
{
    if (efd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::FX, efd->family_id,
                                 FamilyHook::EffectOnDeath, true,
                                 static_cast<walker*>(self)))
        return r;
    if (efd->on_death != nullptr)
        return efd->on_death(self);
    return std::nullopt;
}

std::optional<bool> treasure_on_eat(const TreasureFamilyDescriptor* tfd,
                                    treasure* self, walker* eater)
{
    if (tfd == nullptr)
        return std::nullopt;
    if (auto r = try_script_hook(Order::Treasure, tfd->family_id,
                                 FamilyHook::TreasureOnEat, true,
                                 static_cast<walker*>(self), eater))
        return r;
    if (tfd->on_eat != nullptr)
        return tfd->on_eat(self, eater);
    return std::nullopt;
}

bool generator_customize_spawn(int generator_family, walker* generator,
                               walker* spawn)
{
    if (generator == nullptr || spawn == nullptr)
        return false;
    return try_script_hook(Order::Generator, generator_family,
                           FamilyHook::GeneratorCustomizeSpawn, false,
                           generator, spawn)
        .has_value();
}

// ---------------------------------------------------------------------------
// Level-script dispatch
// ---------------------------------------------------------------------------

namespace {

// Cheap gate shared by all level dispatchers: no packs → no VM creation, no
// cost, byte-identical sims for script-less runs.
VmState* level_vm_state(std::uint32_t kind_bit)
{
    if (pack_scripts().empty())
        return nullptr;
    WorldScripts& ws = active_world_scripts();
    VmState* st = get_vm_state(ws.host().impl().L);
    if (st == nullptr || (st->level_hook_kinds & kind_bit) == 0)
        return nullptr;
    return st;
}

// Pushes the registered fn for (kind, level) — exact slot first, then the
// wildcard — or returns false with the stack clean.
bool push_level_hook_fn(lua_State* L, VmState* st, LevelHook kind,
                        int level_id)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->level_hooks_ref);
    lua_rawgeti(L, -1, level_hook_key(kind, level_id));
    if (lua_isfunction(L, -1)) {
        lua_remove(L, -2);
        return true;
    }
    lua_pop(L, 1);
    lua_rawgeti(L, -1, level_hook_key(kind, -1));
    if (lua_isfunction(L, -1)) {
        lua_remove(L, -2);
        return true;
    }
    lua_pop(L, 2);
    return false;
}

}  // namespace

void level_load(GameWorld* world)
{
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::Load));
    if (st == nullptr || world == nullptr)
        return;
    ScriptHost::Impl& impl = st->owner->host().impl();
    if (!push_level_hook_fn(impl.L, st, LevelHook::Load, world->id))
        return;
    st->dispatch_gen++;
    lua_pushinteger(impl.L, world->id);
    impl.protected_call("level:on_load", 1, 0);
}

void level_tick(GameWorld* world)
{
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::Tick));
    if (st == nullptr || world == nullptr)
        return;
    ScriptHost::Impl& impl = st->owner->host().impl();
    if (!push_level_hook_fn(impl.L, st, LevelHook::Tick, world->id))
        return;
    st->dispatch_gen++;
    lua_pushinteger(impl.L, world->id);
    lua_pushinteger(impl.L,
                    static_cast<lua_Integer>(world->level_tick_count()));
    impl.protected_call("level:on_tick", 2, 0);
}

void level_entity_death(walker* self)
{
    if (self == nullptr || pack_scripts().empty())
        return;
    WorldScripts& ws = active_world_scripts();
    VmState* st = get_vm_state(ws.host().impl().L);
    if (st == nullptr)
        return;
    ScriptHost::Impl& impl = ws.host().impl();
    lua_State* L = impl.L;

    // Per-entity hook first (registered via og.set_entity_hooks); the entry
    // is consumed — a dead entity id never fires twice.
    if (st->has_entity_hooks && self->entity_id() != 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, st->entity_hooks_ref);
        lua_rawgeti(L, -1, static_cast<lua_Integer>(self->entity_id()));
        if (lua_istable(L, -1)) {
            lua_rawgeti(L, -1,
                        static_cast<lua_Integer>(LevelHook::EntityDeath));
            if (lua_isfunction(L, -1)) {
                lua_remove(L, -2);   // entity's hook table
                lua_pushnil(L);
                lua_rawseti(L, -3,
                            static_cast<lua_Integer>(self->entity_id()));
                lua_remove(L, -2);   // entity_hooks root
                st->dispatch_gen++;
                push_walker_handle(L, self, st->dispatch_gen);
                impl.protected_call("entity:on_death", 1, 0);
            } else {
                lua_pop(L, 3);
            }
        } else {
            lua_pop(L, 2);
        }
    }

    // Level-wide hook.
    if ((st->level_hook_kinds &
         (1u << static_cast<unsigned>(LevelHook::EntityDeath))) == 0)
        return;
    GameWorld* world =
        (current_game != nullptr) ? current_game->world : nullptr;
    if (world == nullptr)
        return;
    if (!push_level_hook_fn(L, st, LevelHook::EntityDeath, world->id))
        return;
    st->dispatch_gen++;
    push_walker_handle(L, self, st->dispatch_gen);
    impl.protected_call("level:on_entity_death", 1, 0);
}

void level_entity_spawn(walker* spawned)
{
    if (spawned == nullptr)
        return;
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::EntitySpawn));
    if (st == nullptr)
        return;
    GameWorld* world =
        (current_game != nullptr) ? current_game->world : nullptr;
    if (world == nullptr)
        return;
    ScriptHost::Impl& impl = st->owner->host().impl();
    if (!push_level_hook_fn(impl.L, st, LevelHook::EntitySpawn, world->id))
        return;
    st->dispatch_gen++;
    push_walker_handle(impl.L, spawned, st->dispatch_gen);
    impl.protected_call("level:on_entity_spawn", 1, 0);
}

}  // namespace hooks

}  // namespace og::script
