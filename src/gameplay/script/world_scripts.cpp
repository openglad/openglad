/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_coverage.h>
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
#include <openglad/core/util.h>

#include "script_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

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
        // index; the handle stays valid for as long as the dispatch that
        // produced it is still on the stack. "Still on the stack" is not the
        // same as "newest": a hook that re-enters dispatch (walker:special(),
        // a death hook that spawns) opens a newer generation while its own
        // frame is still running, and its handles must survive that.
        VmState* st = get_vm_state(L);
        if (st != nullptr &&
            std::find(st->live_gens.begin(), st->live_gens.end(), h->gen) !=
                st->live_gens.end())
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
    if (st != nullptr &&
        std::find(st->live_gens.begin(), st->live_gens.end(), h->gen) !=
            st->live_gens.end())
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

std::uint64_t push_dispatch_gen(lua_State* L)
{
    VmState* st = get_vm_state(L);
    if (st == nullptr)
        return 0;
    st->dispatch_gen++;
    st->live_gens.push_back(st->dispatch_gen);
    return st->dispatch_gen;
}

void pop_dispatch_gen(lua_State* L, std::uint64_t gen)
{
    VmState* st = get_vm_state(L);
    if (st == nullptr || gen == 0)
        return;
    // Erase this frame's generation wherever it sits: a Lua error unwinding
    // through pcall can skip an inner frame's pop, so do not assume LIFO.
    auto it = std::find(st->live_gens.begin(), st->live_gens.end(), gen);
    if (it != st->live_gens.end())
        st->live_gens.erase(it, st->live_gens.end());
}

// ---------------------------------------------------------------------------
// Pack lib module registry (og.use) — process-global, mirrors pack_scripts
// ---------------------------------------------------------------------------

namespace {

std::vector<PackLibModule>& lib_module_storage()
{
    static std::vector<PackLibModule> modules;
    return modules;
}

unsigned& lib_generation_storage()
{
    static unsigned generation = 0;
    return generation;
}

}  // namespace

void register_pack_lib_module(PackLibModule module)
{
    for (PackLibModule& existing : lib_module_storage()) {
        if (existing.pack_id == module.pack_id &&
            existing.name == module.name) {
            existing = std::move(module);
            lib_generation_storage()++;
            return;
        }
    }
    lib_module_storage().push_back(std::move(module));
    lib_generation_storage()++;
}

void unregister_pack_lib_modules(const std::string& pack_id)
{
    auto& modules = lib_module_storage();
    const auto removed = std::remove_if(
        modules.begin(), modules.end(),
        [&](const PackLibModule& m) { return m.pack_id == pack_id; });
    if (removed == modules.end())
        return;
    modules.erase(removed, modules.end());
    lib_generation_storage()++;
}

const std::vector<PackLibModule>& pack_lib_modules()
{
    return lib_module_storage();
}

void clear_pack_lib_modules()
{
    lib_module_storage().clear();
    lib_generation_storage()++;
}

unsigned pack_lib_generation()
{
    return lib_generation_storage();
}

unsigned pack_scripts_build_generation()
{
    return pack_scripts_generation() + pack_lib_generation();
}

// ---------------------------------------------------------------------------
// Family tuning store (og.tuning) — process-global, rebuilt per install pass
// ---------------------------------------------------------------------------

namespace {

int tuning_key(Order order, int family_id)
{
    return static_cast<int>(order) * 4096 + family_id;
}

std::map<int, TuningMap>& tuning_storage()
{
    static std::map<int, TuningMap> store;
    return store;
}

std::uint64_t& tuning_generation_storage()
{
    static std::uint64_t generation = 0;
    return generation;
}

}  // namespace

void set_family_tuning(Order order, int family_id, TuningMap map)
{
    if (map.empty())
        tuning_storage().erase(tuning_key(order, family_id));
    else
        tuning_storage()[tuning_key(order, family_id)] = std::move(map);
    tuning_generation_storage()++;
}

const TuningMap* family_tuning(Order order, int family_id)
{
    const auto& store = tuning_storage();
    const auto it = store.find(tuning_key(order, family_id));
    return it == store.end() ? nullptr : &it->second;
}

void clear_all_family_tuning()
{
    tuning_storage().clear();
    tuning_generation_storage()++;
}

std::uint64_t family_tuning_generation()
{
    return tuning_generation_storage();
}

// ---------------------------------------------------------------------------
// Read-only proxy views (og.use exports, og.tuning tables)
// ---------------------------------------------------------------------------

namespace {

int frozen_newindex(lua_State* L)
{
    return luaL_error(L, "attempt to modify a read-only table");
}

// __len forwarding, so an array-shaped module export keeps a working #.
int frozen_len(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(
                           lua_rawlen(L, lua_upvalueindex(1))));
    return 1;
}

}  // namespace

void push_frozen_view_of_top(lua_State* L)
{
    if (!lua_istable(L, -1))
        return;  // immutable scalar/function exports pass through unwrapped
    // Stack: data. The proxy is EMPTY, so every read misses into __index and
    // every write misses into __newindex — the one metatable shape in which
    // assigning an EXISTING key still errors (a metatable on the data table
    // itself would only fence absent keys).
    lua_newtable(L);            // data proxy
    lua_createtable(L, 0, 4);   // data proxy mt
    lua_pushvalue(L, -3);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, frozen_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushvalue(L, -3);
    lua_pushcclosure(L, frozen_len, 1);
    lua_setfield(L, -2, "__len");
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");  // fence, like every handle metatable
    lua_setmetatable(L, -2);    // data proxy
    lua_remove(L, -2);          // proxy
}

// ---------------------------------------------------------------------------
// og.use — module loading
// ---------------------------------------------------------------------------

namespace {

const PackLibModule* find_lib_module(const std::string& pack_id,
                                     const char* name)
{
    for (const PackLibModule& m : pack_lib_modules()) {
        if (m.pack_id == pack_id && m.name == name)
            return &m;
    }
    return nullptr;
}

// status_tbl[key] as a small enum. "loading" marks an in-flight load (a
// cycle when seen again), "failed" a load that already errored.
enum class LibStatus { None, Loading, Failed };

LibStatus lib_status(lua_State* L, VmState* st, const std::string& key)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->lib_status_ref);
    lua_pushlstring(L, key.data(), key.size());
    lua_rawget(L, -2);
    LibStatus status = LibStatus::None;
    if (lua_isstring(L, -1))
        status = (std::strcmp(lua_tostring(L, -1), "loading") == 0)
                     ? LibStatus::Loading
                     : LibStatus::Failed;
    lua_pop(L, 2);
    return status;
}

void set_lib_status(lua_State* L, VmState* st, const std::string& key,
                    const char* status_or_null)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->lib_status_ref);
    lua_pushlstring(L, key.data(), key.size());
    if (status_or_null == nullptr)
        lua_pushnil(L);
    else
        lua_pushstring(L, status_or_null);
    lua_rawset(L, -3);
    lua_pop(L, 1);
}

// Pushes the memoized export of key, or pushes nothing and returns false.
bool push_lib_export(lua_State* L, VmState* st, const std::string& key)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->lib_exports_ref);
    lua_pushlstring(L, key.data(), key.size());
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return false;
    }
    lua_remove(L, -2);
    return true;
}

// Loads one module chunk and memoizes its frozen export. Returns true on
// success. Failures (compile error, runtime error, no exports returned) are
// recorded on the host and latch status "failed" so every later og.use of
// the module errors deterministically instead of re-running a broken chunk.
//
// The compile is TEXT-ONLY and passes through coverage::bind_compiled_chunk
// exactly like the two ScriptHost compile sites — both invariants (bytecode
// refusal, generation binding/scrubbing) quantify over every compile in the
// process, and this is the third site.
bool load_pack_lib_module(ScriptHost::Impl& impl, VmState* st,
                          const PackLibModule& m)
{
    lua_State* L = impl.L;
    const std::string key = m.pack_id + "/" + m.name;
    set_lib_status(L, st, key, "loading");
    // Modules resolve their own og.use calls against their OWN pack.
    const std::string caller_pack = st->current_pack;
    st->current_pack = m.pack_id;

    bool ok = false;
    if (luaL_loadbufferx(L, m.source.data(), m.source.size(),
                         m.chunk_name.c_str(), "t") != LUA_OK) {
        impl.record_error(m.chunk_name.c_str(), lua_tostring(L, -1));
        lua_pop(L, 1);
    } else {
        coverage::bind_compiled_chunk(L, -1, m.chunk_name, m.source);
        // A fresh isolated environment per module: modules communicate
        // through their RETURNED exports only, never through shared
        // globals (unlike pack scripts, which share the pack environment).
        impl.push_new_environment();
        lua_setupvalue(L, -2, 1);
        if (impl.protected_call(m.chunk_name.c_str(), 0, 1)) {
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                impl.record_error(
                    m.chunk_name.c_str(),
                    "module returned no exports (a lib chunk must end with "
                    "`return <table of pure functions/constants>`)");
            } else {
                push_frozen_view_of_top(L);
                lua_rawgeti(L, LUA_REGISTRYINDEX, st->lib_exports_ref);
                lua_pushlstring(L, key.data(), key.size());
                lua_pushvalue(L, -3);
                lua_rawset(L, -3);
                lua_pop(L, 2);  // exports table, frozen export
                ok = true;
            }
        }
    }
    st->current_pack = caller_pack;
    set_lib_status(L, st, key, ok ? nullptr : "failed");
    return ok;
}

// og.use("name") → the frozen export of packs/<current pack>/lib/<name>.lua.
// Pack-relative and load-time-only: outside pack load there is no pack to
// resolve against (and a dispatch-time og.use would be hidden coupling the
// reader cannot see — bind modules to locals at chunk load).
int og_use(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    VmState* st = get_vm_state(L);
    if (st == nullptr || st->owner == nullptr)
        return luaL_error(L, "og.use: no world scripts active");
    if (st->current_pack.empty())
        return luaL_error(L, "og.use: only callable while a pack chunk "
                             "loads (bind modules to locals at load time)");
    const std::string key = st->current_pack + "/" + name;
    switch (lib_status(L, st, key)) {
        case LibStatus::Loading:
            return luaL_error(L, "og.use: circular dependency on module "
                                 "'%s'", name);
        case LibStatus::Failed:
            return luaL_error(L, "og.use: module '%s' failed to load", name);
        case LibStatus::None:
            break;
    }
    if (push_lib_export(L, st, key))
        return 1;
    // Not loaded yet: a module (or script) may use a module the eager pass
    // has not reached — load it on demand, in the same deterministic way.
    const PackLibModule* m = find_lib_module(st->current_pack, name);
    if (m == nullptr)
        return luaL_error(L,
                          "og.use: no module '%s' in pack '%s' (expected "
                          "packs/%s/lib/%s.lua)",
                          name, st->current_pack.c_str(),
                          st->current_pack.c_str(), name);
    if (!load_pack_lib_module(st->owner->host().impl(), st, *m))
        return luaL_error(L, "og.use: module '%s' failed to load", name);
    push_lib_export(L, st, key);
    return 1;
}

}  // namespace

namespace {

// ---------------------------------------------------------------------------
// Function-coverage LABELS
// ---------------------------------------------------------------------------
//
// Coverage itself is not measured here. The denominator is every function
// prototype in the file (og_lua_lines walks them statically) and the
// numerator is "a line of that prototype's body executed" (the line hook in
// script_host.cpp). Registration is neither: a registered hook that is never
// entered must read as a MISS, and a helper nobody registers still has to
// count.
//
// What registration contributes is a NAME. Identity is the Lua function's own
// (source chunk, linedefined, lastlinedefined) — the same span the static
// walk emits and the same one the lcov FN record is named after — so the
// report can print "living/core:soldier/do_special" instead of
// "soldier.lua:87". A no-op unless the recorder is armed, and it does not
// disturb the stack: the function stays exactly where it was.

// The hook function on top of the stack was REGISTERED by a pack. The
// recorder resolves the closure's prototype to the generation that DEFINED
// it, so the label lands on the right grid even when the chunk has since
// been re-declared with different bytes. Does not disturb the stack.
void coverage_declare_hook(lua_State* L, const std::string& label)
{
    if (!coverage::enabled()) return;  // callers guard too; belt and braces
    coverage::declare_function_closure(L, -1, label);
}

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

// Pack scripts load filename-lexicographically, so a chunk that registers a
// hook another chunk already claimed silently wins with nothing to show for
// it. Last-registration-wins stays the dispatch rule — this only makes the
// collision visible (trace + host error record + operator-facing warning).
void report_duplicate_hook(lua_State* L, VmState* st, const char* order_str,
                           const char* family_str, const char* hook_name)
{
    luaL_where(L, 1);  // "chunkname:line: ", or "" with no debug info
    std::string where = lua_tostring(L, -1);
    lua_pop(L, 1);
    while (!where.empty() && (where.back() == ' ' || where.back() == ':'))
        where.pop_back();
    if (where.empty()) where = "og.register_hooks";  // no debug info

    std::string message = "duplicate hook registration: ";
    message += order_str;
    message += " '";
    message += family_str;
    message += "' hook '";
    message += hook_name;
    message += "' was already registered by an earlier chunk in this pack "
               "load; the later registration wins";

    LogWarn("class pack: {}: {}\n", where, message);
    // Also traces (script_error) and folds repeats into one record.
    st->owner->host().impl().record_error(where.c_str(), message.c_str());
}

// --- specials table keys ------------------------------------------------
//
// A specials table may key an entry by slot integer or by the special's
// declared id ("charge"). The id form is resolved to its slot HERE, once,
// at registration: what gets stored and dispatched is the same int-keyed
// private table either way, so the cast path is untouched.

// One validated entry of a caller's specials table.
struct SpecialsKey {
    lua_Integer slot;
    bool by_id;        // spelled as the declared id rather than the slot
    std::string text;  // how the caller spelled it, for diagnostics
};

// The slot a declared special id names, or -1 when the family declares no
// such id.
int special_slot_for_id(const FamilyDescriptor* fd, const char* id)
{
    if (fd == nullptr)
        return -1;
    for (int slot = 0; slot < FD_NUM_SPECIALS; slot++) {
        if (fd->special_ids[slot] != nullptr &&
            std::strcmp(fd->special_ids[slot], id) == 0)
            return slot;
    }
    return -1;
}

// The ids the family does declare — the other half of an unknown-key
// error. A family whose descriptor names none (an unmigrated pack, say)
// says so, which is the answer to "why doesn't my key resolve".
std::string declared_special_ids(const FamilyDescriptor* fd)
{
    std::string list;
    for (int slot = 0; fd != nullptr && slot < FD_NUM_SPECIALS; slot++) {
        if (fd->special_ids[slot] == nullptr)
            continue;
        if (!list.empty())
            list += ", ";
        list += fd->special_ids[slot];
    }
    if (list.empty())
        return "none — this family declares no special ids";
    return list;
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
        // Stack: hooks_root, family_tbl, fn — family_tbl is at -2.
        lua_rawgeti(L, -2, static_cast<lua_Integer>(oi->hooks[i].hook));
        const bool occupied = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (occupied)
            report_duplicate_hook(L, st, order_str, family_str,
                                  oi->hooks[i].name);
        if (coverage::enabled())
            coverage_declare_hook(L, std::string(order_str) + "/" +
                                         family_str + "/" +
                                         oi->hooks[i].name);
        lua_rawseti(L, -2, static_cast<lua_Integer>(oi->hooks[i].hook));
        st->owner->note_hook(oi->order, family_id, oi->hooks[i].hook);
        registered++;
    }
    // -----------------------------------------------------------------
    // specials = { [1]=fn, [2]=fn, ..., default=fn } — table form of the
    // do_special hook (living orders only). self:current_special() selects
    // the entry, a missing index falls to `default`, and a table with neither
    // is the dispatch fall-through — a successful no-op
    // (result true) with no Lua call at all. The table shares the
    // do_special slot, so cross-chunk collisions are reported and last
    // registration wins, exactly like every named hook.
    // -----------------------------------------------------------------
    lua_getfield(L, 3, "specials");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
    } else {
        const int spec_idx = lua_gettop(L);
        const int family_idx = spec_idx - 1;  // family table pushed above
        if (oi->order != Order::Living)
            return luaL_error(
                L, "og.register_hooks: 'specials' is a living-order key "
                   "(order '%s' has no do_special hook)", order_str);
        if (!lua_istable(L, spec_idx))
            return luaL_error(
                L, "og.register_hooks: 'specials' must be a table "
                   "({ [1]=fn, ..., default=fn })");
        lua_getfield(L, 3, "do_special");
        const bool has_plain_do_special = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (has_plain_do_special)
            return luaL_error(
                L, "og.register_hooks: register 'do_special' or 'specials' "
                   "for a family, not both in one call");
        // Validate every entry before storing anything. Keys are integers
        // 1..255 (current_special is a char-wide slot index), a declared
        // special id, or the string "default"; every value is a function.
        // lua_next order cannot matter here: any invalid entry raises the
        // same load error, and the resolved keys are sorted before the
        // collision check so its message reads the same every run.
        const FamilyDescriptor* fd = get_family_descriptor(family_id);
        std::vector<SpecialsKey> keys;
        int entry_count = 0;
        bool has_default = false;
        lua_pushnil(L);
        while (lua_next(L, spec_idx) != 0) {
            if (lua_isinteger(L, -2)) {
                const lua_Integer k = lua_tointeger(L, -2);
                if (k < 1 || k > 255)
                    return luaL_error(
                        L, "og.register_hooks: 'specials' keys must be "
                           "integers 1..255, a declared special id, or "
                           "'default'");
                keys.push_back({k, false, std::to_string(k)});
            } else if (lua_type(L, -2) == LUA_TSTRING) {
                // Safe during traversal: the key already IS a string.
                const std::string key = lua_tostring(L, -2);
                if (key == "default") {
                    has_default = true;
                } else {
                    const int slot = special_slot_for_id(fd, key.c_str());
                    if (slot < 0)
                        return luaL_error(
                            L,
                            "og.register_hooks: 'specials' key '%s' names no "
                            "special of living family '%s' (declared ids: "
                            "%s)",
                            key.c_str(), family_str,
                            declared_special_ids(fd).c_str());
                    keys.push_back({slot, true, key});
                }
            } else {
                return luaL_error(
                    L, "og.register_hooks: 'specials' keys must be "
                       "integers 1..255, a declared special id, or "
                       "'default'");
            }
            if (!lua_isfunction(L, -1))
                return luaL_error(
                    L, "og.register_hooks: 'specials' entries must be "
                       "functions");
            lua_pop(L, 1);
            entry_count++;
        }
        if (entry_count == 0)
            return luaL_error(
                L, "og.register_hooks: 'specials' table is empty "
                   "(register at least one entry or a default)");
        // Two keys for one slot: the table says two different things about
        // one special and nothing here can pick between them.
        std::sort(keys.begin(), keys.end(),
                  [](const SpecialsKey& a, const SpecialsKey& b) {
                      return std::tie(a.slot, a.text) <
                             std::tie(b.slot, b.text);
                  });
        for (std::size_t i = 1; i < keys.size(); i++) {
            if (keys[i].slot != keys[i - 1].slot)
                continue;
            return luaL_error(
                L,
                "og.register_hooks: 'specials' keys '%s' and '%s' both name "
                "slot %d of '%s'",
                keys[i - 1].text.c_str(), keys[i].text.c_str(),
                static_cast<int>(keys[i].slot), family_str);
        }
        // Store a PRIVATE copy: mutating the caller's table after
        // registration must not be able to change dispatch — the
        // registered contract is as immutable as a registered function
        // value (R6 at the registration boundary).
        lua_createtable(L, static_cast<int>(keys.size()),
                        has_default ? 1 : 0);
        for (const SpecialsKey& k : keys) {
            if (k.by_id)
                lua_getfield(L, spec_idx, k.text.c_str());
            else
                lua_rawgeti(L, spec_idx, k.slot);
            if (coverage::enabled())
                coverage_declare_hook(
                    L, std::string(order_str) + "/" + family_str +
                           "/do_special[" + std::to_string(k.slot) + "]");
            lua_rawseti(L, -2, k.slot);
        }
        if (has_default) {
            lua_getfield(L, spec_idx, "default");
            if (coverage::enabled())
                coverage_declare_hook(L, std::string(order_str) + "/" +
                                             family_str +
                                             "/do_special[default]");
            lua_setfield(L, -2, "default");
        }
        lua_rawgeti(L, family_idx,
                    static_cast<lua_Integer>(FamilyHook::DoSpecial));
        const bool occupied = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (occupied)
            report_duplicate_hook(L, st, order_str, family_str, "specials");
        lua_rawseti(L, family_idx,
                    static_cast<lua_Integer>(FamilyHook::DoSpecial));
        st->owner->note_hook(oi->order, family_id, FamilyHook::DoSpecial);
        registered++;
        lua_pop(L, 1);  // the caller's specials table
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
        if (coverage::enabled())
            coverage_declare_hook(L, "level/" + std::to_string(level_id) +
                                         "/" + h.name);
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
    if (coverage::enabled() && lua_isfunction(L, -1))
        coverage_declare_hook(L, "entity/on_death");
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

// Says so when a family can cast a special that nothing will handle.
//
// A specials table with no entry for the cast slot and no `default` is a
// SUCCESSFUL no-op that still spends the MP (begin_special's
// no_special_handler path, then walker::special deducts). That contract
// stands — this only stops it being silent. Run once per VM, after every
// pack chunk has registered, so the table it reads is the final one.
void warn_unhandled_castable_specials(lua_State* L, const VmState& st)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, st.hooks_ref);
    for (int family_id = 0; family_id < NUM_FAMILY_SLOTS; family_id++) {
        const FamilyDescriptor* fd = get_family_descriptor(family_id);
        if (fd == nullptr)
            continue;
        lua_rawgeti(L, -1, hook_table_key(Order::Living, family_id));
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        lua_rawgeti(L, -1, static_cast<lua_Integer>(FamilyHook::DoSpecial));
        // Only the table form can fall through: a plain do_special function
        // is asked about every slot and answers for itself.
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            continue;
        }
        lua_pushstring(L, "default");
        lua_rawget(L, -2);
        const bool has_default = lua_isfunction(L, -1);
        lua_pop(L, 1);
        if (has_default) {
            lua_pop(L, 2);
            continue;  // a default answers for every slot
        }
        for (int slot = 1; slot < FD_NUM_SPECIALS; slot++) {
            const char* name = fd->special_names[slot];
            if (name == nullptr || std::strcmp(name, kSpecialNameNone) == 0)
                continue;
            if (fd->special_cost[slot] >= kSpecialCostDisabled)
                continue;
            lua_rawgeti(L, -1, slot);
            const bool handled = lua_isfunction(L, -1);
            lua_pop(L, 1);
            if (handled)
                continue;
            LogWarn("class pack: special '{}' (slot {}) of living family "
                    "'{}' has no handler and no default: casting it will "
                    "spend {} MP and do nothing\n",
                    name, slot,
                    fd->declared_id != nullptr ? fd->declared_id : fd->name,
                    fd->special_cost[slot]);
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
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
    lua_newtable(L);
    detail_->state.lib_exports_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    detail_->state.lib_status_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    detail_->state.tuning_cache_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    detail_->state.tuning_cache_gen = family_tuning_generation();

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
    lua_pushcfunction(L, og_use);
    lua_setfield(L, -2, "use");
    lua_pop(L, 2);

    // The registries' combined generation: a module-only mutation must
    // rebuild long-lived hosts exactly like a script mutation.
    built_generation_ = pack_scripts_build_generation();

    // Load lib modules first (registration order — pack-id-lexicographic,
    // then filename-lexicographic), so every pack chunk can og.use them at
    // its top level. A module another module already pulled in on demand is
    // settled and skipped; failures are recorded and latched, never fatal.
    ScriptHost::Impl& host_impl = host_->impl();
    for (const PackLibModule& m : pack_lib_modules()) {
        const std::string key = m.pack_id + "/" + m.name;
        if (lib_status(L, &detail_->state, key) != LibStatus::None)
            continue;
        bool settled = push_lib_export(L, &detail_->state, key);
        if (settled)
            lua_pop(L, 1);
        else
            (void)load_pack_lib_module(host_impl, &detail_->state, m);
    }

    // Replay pack scripts (registration order; env shared per pack).
    // current_pack scopes og.use to the pack whose chunk is loading.
    for (const PackScript& ps : pack_scripts()) {
        detail_->state.current_pack = ps.pack_id;
        if (!host_->run_chunk(ps.chunk_name, ps.source, ps.pack_id)) {
            // A chunk that fails to compile (or errors at its top level)
            // registers nothing: every family it carries silently loses all
            // behavior while the pack still reports installed. run_chunk
            // already latched the record; this is the operator-facing half,
            // same channel as the duplicate-hook warning above.
            const auto& errs = host_->errors();
            LogError("class pack '{}': chunk {} failed to load: {}\n",
                     ps.pack_id, ps.chunk_name,
                     errs.empty() ? "(no error record)"
                                  : errs.back().message.c_str());
        }
    }
    detail_->state.current_pack.clear();

    warn_unhandled_castable_specials(L, detail_->state);
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
    if (ui == nullptr ||
        ui->built_generation() != pack_scripts_build_generation())
        ui = std::make_unique<WorldScripts>();
    return *ui;
}

// ---------------------------------------------------------------------------
// Hook dispatch
// ---------------------------------------------------------------------------

namespace {

// Pack-installed descriptors carry no C++ callbacks, so an erroring hook
// means nothing happens at all. Count every failed dispatch, keep
// the most recent one for a test to read back, and shout the first sighting
// of each distinct error at operator level. Process-global bookkeeping, read
// by nothing in the sim: peers stay in step and nothing here can throw.
hooks::HookFailure& hook_failure_state()
{
    static hooks::HookFailure state;
    return state;
}

void note_hook_failure(const char* where, const ScriptHost::Impl& impl,
                       bool is_new_error)
{
    hooks::HookFailure& state = hook_failure_state();
    state.count++;
    state.where = (where != nullptr) ? where : "?";
    state.message.clear();
    // record_error folds repeats, so the freshest text for this `where` is
    // the last matching stored record.
    for (auto it = impl.errors.rbegin(); it != impl.errors.rend(); ++it) {
        if (it->where == state.where) {
            state.message = it->message;
            break;
        }
    }
    // record_error already traced ("script_error") on every occurrence; this
    // is the operator-facing half, once per distinct error so a hook failing
    // every tick cannot drown the log.
    if (is_new_error)
        LogError("class pack: family hook {} FAILED — no C++ fallback "
                 "exists, so nothing ran: {}\n",
                 state.where, state.message);
}

// RAII frame for one hook invocation: locates the hook function, pushes
// arguments, pcalls under the host budget, and converts the result.
class HookFrame {
public:
    explicit HookFrame(WorldScripts& ws)
        : ws_(ws), impl_(ws.host().impl()), L_(impl_.L)
    {
    }

    // Closes this frame's generation however it exits (normal return, early
    // bail, or a Lua error unwinding out of protected_call), so the live-gen
    // set never leaks entries.
    ~HookFrame() { pop_dispatch_gen(L_, gen_); }

    HookFrame(const HookFrame&) = delete;
    HookFrame& operator=(const HookFrame&) = delete;

    bool begin(Order order, int family_id, FamilyHook hook)
    {
        VmState* st = get_vm_state(L_);
        if (st == nullptr)
            return false;
        gen_ = push_dispatch_gen(L_);
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

    // begin() for the DoSpecial slot, which may hold either the classic
    // do_special function or a specials table ({ [1]=fn, ..., default=fn }).
    // For a table this selects [sp], then "default". When the table holds
    // neither, sets *no_special_handler and returns false with a clean stack:
    // the dispatch is consumed as a successful no-op, with nothing called.
    bool begin_special(int family_id, lua_Integer sp,
                       bool* no_special_handler)
    {
        VmState* st = get_vm_state(L_);
        if (st == nullptr)
            return false;
        gen_ = push_dispatch_gen(L_);
        lua_rawgeti(L_, LUA_REGISTRYINDEX, st->hooks_ref);
        lua_rawgeti(L_, -1, hook_table_key(Order::Living, family_id));
        if (!lua_istable(L_, -1)) {
            lua_pop(L_, 2);
            return false;
        }
        lua_rawgeti(L_, -1,
                    static_cast<lua_Integer>(FamilyHook::DoSpecial));
        if (lua_istable(L_, -1)) {
            lua_rawgeti(L_, -1, sp);
            if (!lua_isfunction(L_, -1)) {
                lua_pop(L_, 1);
                lua_pushstring(L_, "default");
                lua_rawget(L_, -2);
            }
            if (!lua_isfunction(L_, -1)) {
                // entry + specials table + family table + hooks root
                lua_pop(L_, 4);
                *no_special_handler = true;
                return false;
            }
            lua_remove(L_, -2);  // specials table
        }
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
        const std::size_t errors_before = impl_.errors.size();
        if (!impl_.protected_call(where, nargs_, wants_result ? 1 : 0)) {
            note_hook_failure(where, impl_,
                              impl_.errors.size() > errors_before);
            return std::nullopt;
        }
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

// DoSpecial dispatch, honoring both slot forms (stub generator: do_special
// is fun(self: og.Walker): boolean, wants_result=true — asserted below).
// Returns nullopt when no script hook ran (the caller may use an optional
// descriptor callback), and the hook's boolean-coerced result otherwise.
// The specials-table form implements the class-pack dispatch contract:
// self:current_special() selects, a missing index falls to `default`, and a
// table with neither is the ladder's fall-through — result true, no call.
std::optional<bool> try_script_do_special(int family_id, walker* self)
{
    WorldScripts& ws = active_world_scripts();
    if (!ws.has_hook(Order::Living, family_id, FamilyHook::DoSpecial))
        return std::nullopt;
    // Test dispatches may pass self == nullptr; slot 0 is unmappable
    // (specials keys are 1..255), so selection falls to `default`.
    const lua_Integer sp =
        (self != nullptr) ? static_cast<lua_Integer>(self->current_special())
                          : 0;
    HookFrame f(ws);
    bool no_special_handler = false;
    if (!f.begin_special(family_id, sp, &no_special_handler)) {
        if (no_special_handler)
            return true;
        return std::nullopt;
    }
    f.arg(self);
    return f.call(hook_where(FamilyHook::DoSpecial), true);
}

}  // namespace

namespace hooks {

const HookFailure& hook_failures()
{
    return hook_failure_state();
}

void reset_hook_failures()
{
    hook_failure_state() = HookFailure{};
}

std::optional<bool> do_special(const FamilyDescriptor* fd, walker* self)
{
    if (fd == nullptr)
        return std::nullopt;
    if (auto r = try_script_do_special(fd->family_id, self))
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
    const std::uint64_t gen = push_dispatch_gen(impl.L);
    lua_pushinteger(impl.L, world->id);
    impl.protected_call("level:on_load", 1, 0);
    pop_dispatch_gen(impl.L, gen);
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
    const std::uint64_t gen = push_dispatch_gen(impl.L);
    lua_pushinteger(impl.L, world->id);
    lua_pushinteger(impl.L,
                    static_cast<lua_Integer>(world->level_tick_count()));
    impl.protected_call("level:on_tick", 2, 0);
    pop_dispatch_gen(impl.L, gen);
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
                const std::uint64_t gen = push_dispatch_gen(L);
                push_walker_handle(L, self, gen);
                impl.protected_call("entity:on_death", 1, 0);
                pop_dispatch_gen(L, gen);
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
    const std::uint64_t gen = push_dispatch_gen(L);
    push_walker_handle(L, self, gen);
    impl.protected_call("level:on_entity_death", 1, 0);
    pop_dispatch_gen(L, gen);
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
    const std::uint64_t gen = push_dispatch_gen(impl.L);
    push_walker_handle(impl.L, spawned, gen);
    impl.protected_call("level:on_entity_spawn", 1, 0);
    pop_dispatch_gen(impl.L, gen);
}

}  // namespace hooks

}  // namespace og::script
