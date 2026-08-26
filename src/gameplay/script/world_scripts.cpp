/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_coverage.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
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
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace og::script {

namespace {

// ---------------------------------------------------------------------------
// Hook name tables (per order)
// ---------------------------------------------------------------------------

// HookName / OrderInfo are declared in script_internal.h (family_decl.cpp
// walks the same vocabulary); the TABLES stay here, where
// scripts/modding/gen_api_stubs.py reads them as the API's source of truth.

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
    {"on_act_override", FamilyHook::OnActOverride},
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

constexpr OrderInfo kOrders[] = {
    {"living", Order::Living, kLivingHooks, std::size(kLivingHooks)},
    {"weapon", Order::Weapon, kWeaponHooks, std::size(kWeaponHooks)},
    {"treasure", Order::Treasure, kTreasureHooks, std::size(kTreasureHooks)},
    {"generator", Order::Generator, kGeneratorHooks,
     std::size(kGeneratorHooks)},
    {"fx", Order::FX, kEffectHooks, std::size(kEffectHooks)},
    {"effect", Order::FX, kEffectHooks, std::size(kEffectHooks)},  // alias
};

// Family string-id resolution ("core:soldier" → family byte) lives in
// og::families::resolve_family_string_id (families/family_string_ids.h),
// shared with the installer and the declaration pass.

}  // namespace

const OrderInfo* find_order(const char* name)
{
    for (const auto& oi : kOrders)
        if (std::strcmp(oi.name, name) == 0)
            return &oi;
    return nullptr;
}

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
    detail::declare_registered_chunk(module.chunk_name, module.source,
                                     module.origin);
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
    return pack_scripts_generation() + pack_lib_generation() +
           pack_family_generation();
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
    const ChunkKind caller_chunk = st->current_chunk;
    st->current_pack = m.pack_id;
    st->current_chunk = ChunkKind::Lib;

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
    st->current_chunk = caller_chunk;
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
    // host_impl, not owner: a lib module is pure by contract, so og.use is
    // one of the few entry points the declaration VM — which has a host but
    // no WorldScripts — is allowed to serve.
    if (st == nullptr || st->host_impl == nullptr)
        return luaL_error(L, "og.use: no script host active");
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
    if (!load_pack_lib_module(*st->host_impl, st, *m))
        return luaL_error(L, "og.use: module '%s' failed to load", name);
    push_lib_export(L, st, key);
    return 1;
}

}  // namespace

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

}  // namespace

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
    if (st->host_impl != nullptr)
        st->host_impl->record_error(where.c_str(), message.c_str());
}

// --- specials table keys ------------------------------------------------
//
// A specials table keys each entry by the special's declared id ("charge").
// The id is resolved to its slot HERE, once, at registration: what gets
// stored and dispatched is an int-keyed private table, so the cast path is
// untouched. A bare slot number is refused — see the walk below.

// One validated entry of a caller's specials table.
namespace {
struct SpecialsKey {
    lua_Integer slot;
    std::string id;  // the declared id the caller spelled
};
}  // namespace

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
// error. A family whose descriptor names none (an entry with no
// `specials:` list, say) says so, which is the answer to "why doesn't my
// key resolve".
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

namespace {

int og_register_hooks(lua_State* L)
{
    // Deliberately outside the load fence, but NOT outside the campaign
    // fence: a menu script may not rewrite sim hook tables. Checked before
    // any argument check so the fence-walk test sees this error, not an
    // argument error.
    if (const VmState* fence_st = get_vm_state(L);
        fence_st != nullptr && fence_st->campaign_dispatch)
        return luaL_error(L, "og.register_hooks: hook registration is not "
                             "available during campaign hooks");
    const char* order_str = luaL_checkstring(L, 1);
    const char* family_str = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);

    const OrderInfo* oi = find_order(order_str);
    if (oi == nullptr)
        return luaL_error(L, "og.register_hooks: unknown order '%s'",
                          order_str);

    // Every key must name a hook this order has (format spec V4). Walking
    // the known names and taking whatever is there would let a misspelled
    // `on_deatch` register nothing and say nothing: the family would simply
    // have no death behavior, discovered in play.
    // `specials` is the one extra key, the table form of do_special.
    //
    // Checked BEFORE the declaration pass bows out below: the names are a
    // property of the order alone, so the pass that can still reject the
    // pack is the one that should catch the typo.
    {
        std::vector<const char*> names;
        for (size_t i = 0; i < oi->hook_count; i++)
            names.push_back(oi->hooks[i].name);
        names.push_back("specials");
        lua_pushnil(L);
        while (lua_next(L, 3) != 0) {
            lua_pop(L, 1);  // value; the key stays for the next iteration
            if (lua_type(L, -1) != LUA_TSTRING) {
                return luaL_error(
                    L, "og.register_hooks: a hook table's keys are hook "
                       "names (got a %s key for %s '%s')",
                    luaL_typename(L, -1), order_str, family_str);
            }
            const std::string key = lua_tostring(L, -1);
            bool known = false;
            for (const char* n : names)
                known = known || key == n;
            if (!known) {
                const std::string hint = did_you_mean(key, names);
                return luaL_error(
                    L, "og.register_hooks: %s '%s' has no hook '%s'%s",
                    order_str, family_str, key.c_str(), hint.c_str());
            }
        }
    }

    // Behavior only, and the declaration pass installs no behavior: the
    // families the call names may not exist yet (this pass is what creates
    // them), so resolving the id here would fail on a correct pack. The
    // bind replay is where these land.
    const VmState* declare_st = get_vm_state(L);
    if (declare_st != nullptr && declare_st->mode == VmMode::Declare)
        return 0;

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
    const int family_tbl_idx = lua_gettop(L);
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
        // Overriding an og.family declaration is what this call is FOR
        // (format spec V4/V7) — only two registrations racing each other
        // are the accident the warning exists for.
        const bool overriding_declaration =
            take_declared_hook(L, family_tbl_idx, oi->hooks[i].hook);
        if (occupied && !overriding_declaration)
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
    // specials = { charge=fn, boomerang=fn, ..., default=fn } — table form
    // of the do_special hook (living orders only). self:current_special()
    // selects the entry, a missing slot falls to `default`, and a table with
    // neither is the dispatch fall-through — a successful no-op
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
                   "({ <special_id>=fn, ..., default=fn })");
        lua_getfield(L, 3, "do_special");
        const bool has_plain_do_special = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (has_plain_do_special)
            return luaL_error(
                L, "og.register_hooks: register 'do_special' or 'specials' "
                   "for a family, not both in one call");
        // Validate every entry before storing anything. Keys are a declared
        // special id or the string "default"; every value is a function.
        // lua_next order cannot matter here: any invalid entry raises the
        // same load error, and the resolved keys are sorted before they are
        // stored so the table is built the same way every run.
        //
        // A slot number is refused. It was the original spelling and it
        // bound a handler to a position, so reordering a family's
        // `specials:` list silently re-pointed the handler; the declared id
        // names what it handles and a typo on either side is this error.
        const FamilyDescriptor* fd = get_family_descriptor(family_id);
        std::vector<SpecialsKey> keys;
        int entry_count = 0;
        bool has_default = false;
        lua_pushnil(L);
        while (lua_next(L, spec_idx) != 0) {
            if (lua_isinteger(L, -2)) {
                return luaL_error(
                    L,
                    "og.register_hooks: 'specials' key [%s] is a slot "
                    "number; specials keys are the family's declared special "
                    "ids, or 'default' (living family '%s', declared ids: "
                    "%s)",
                    std::to_string(lua_tointeger(L, -2)).c_str(), family_str,
                    declared_special_ids(fd).c_str());
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
                    keys.push_back({slot, key});
                }
            } else {
                return luaL_error(
                    L, "og.register_hooks: 'specials' keys must be a "
                       "declared special id or 'default' (got a %s key)",
                    luaL_typename(L, -2));
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
        // Slot order, so the stored table and its coverage labels are built
        // identically whatever order lua_next walked the keys in. Two keys
        // cannot land on one slot: a family's declared ids are unique, so
        // distinct ids name distinct slots.
        std::sort(keys.begin(), keys.end(),
                  [](const SpecialsKey& a, const SpecialsKey& b) {
                      return a.slot < b.slot;
                  });
        // Store a PRIVATE copy: mutating the caller's table after
        // registration must not be able to change dispatch — the
        // registered contract is as immutable as a registered function
        // value (R6 at the registration boundary).
        lua_createtable(L, static_cast<int>(keys.size()),
                        has_default ? 1 : 0);
        for (const SpecialsKey& k : keys) {
            lua_getfield(L, spec_idx, k.id.c_str());
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
        const bool overriding_declaration =
            take_declared_hook(L, family_idx, FamilyHook::DoSpecial);
        if (occupied && !overriding_declaration)
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

// The widest bound og.rand accepts; bindings_entity.cpp holds the matching
// copy for og.rand0/og.cosmetic_rand (the value is shared by exactly those
// two translation units, so neither gets to own a header for it). Every
// draw lands in IRandom::next(std::uint32_t): a Lua 64-bit n above this
// used to reach the generator as n mod 2^32 — a silently different
// distribution, and exactly 2^32 arrived as next(0), which answers 0
// WITHOUT advancing the stream. A wrapped bound is a desync in a
// deterministic sim, so an over-wide n is a script error, never a clamp.
constexpr lua_Integer kMaxRandBound = 2147483647;  // INT32_MAX

// og.rand(n) → deterministic sim RNG (world-owned). n must lie in
// [1, kMaxRandBound]; within that range the conversion to next()'s
// std::uint32_t is exact, so every bound a script actually uses draws
// exactly what it always drew.
int og_rand(lua_State* L)
{
    const lua_Integer n = luaL_checkinteger(L, 1);
    if (n <= 0)
        return luaL_error(L, "og.rand: n must be positive");
    if (n > kMaxRandBound)
        return luaL_error(L, "og.rand: n out of range [1, 2147483647]");
    if (current_game == nullptr || current_game->world == nullptr)
        return luaL_error(L, "og.rand: no active world");
    lua_pushinteger(L, static_cast<lua_Integer>(
                           current_game->world->rng_.next(
                               static_cast<std::uint32_t>(n))));
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
    {"on_mode_init", LevelHook::ModeInit},
    {"on_mode_tick", LevelHook::ModeTick},
    {"on_damage", LevelHook::Damage},
    {"on_respawn", LevelHook::Respawn},
};

// og.register_level_hooks(level_id, { on_load=, on_tick=, on_entity_death=,
// on_entity_spawn= }). level_id -1 registers for every level.
int og_register_level_hooks(lua_State* L)
{
    // The same campaign fence as og.register_hooks (menu scripts may not
    // rewrite sim hook tables), before any argument check.
    if (const VmState* fence_st = get_vm_state(L);
        fence_st != nullptr && fence_st->campaign_dispatch)
        return luaL_error(L, "og.register_level_hooks: hook registration is "
                             "not available during campaign hooks");
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

// ---------------------------------------------------------------------------
// Campaign hooks (og.register_campaign_hooks)
// ---------------------------------------------------------------------------

// The hook-function keys og.register_campaign_hooks accepts ('vars' is the
// one non-hook key). Parsed by scripts/modding/gen_api_stubs.py — keep the
// spelling and location.
constexpr const char* kCampaignHookNames[] = {"picker_menu", "picker_action",
                                              "base_camp", "lineup"};

// Slots of the campaign_hooks_ref registry table.
constexpr lua_Integer kCampaignMenuSlot = 1;
constexpr lua_Integer kCampaignActionSlot = 2;
constexpr lua_Integer kCampaignZoneSlot = 3;
// `lineup` is a TABLE, not a function: only its `power` member lives in
// the registry table. The preset NAMES are plain data and are copied out
// to the VmState at registration, so the menus can read them without
// entering Lua at all (docs/lineup-design.md §3.3).
constexpr lua_Integer kCampaignLineupPowerSlot = 4;

// "chunkname:line" of the caller, for the duplicate-registration record.
std::string campaign_caller_source(lua_State* L)
{
    luaL_where(L, 1);
    std::string where = lua_tostring(L, -1);
    lua_pop(L, 1);
    while (!where.empty() && (where.back() == ' ' || where.back() == ':'))
        where.pop_back();
    if (where.empty()) where = "og.register_campaign_hooks";  // no debug info
    return where;
}

// og.register_campaign_hooks({ vars = {...}, picker_menu = fn,
// picker_action = fn, base_camp = fn }) — load-time only, one campaign
// book per VM (docs/campaign-scripting-design.md;
// docs/basecamp-zones-design.md for the base_camp zone hook).
int og_register_campaign_hooks(lua_State* L)
{
    VmState* st = get_vm_state(L);
    // Campaign and plan fences first (before any argument check): neither a
    // campaign hook nor a match plan may re-register hooks.
    if (st != nullptr && st->campaign_dispatch)
        return luaL_error(L, "og.register_campaign_hooks: hook registration "
                             "is not available during campaign hooks");
    luaL_checktype(L, 1, LUA_TTABLE);
    // The reads below use absolute stack indices 2/3/4 — drop any extra
    // arguments so a stray second argument cannot shadow them.
    lua_settop(L, 1);

    // Every validation below is a property of the call alone, so it runs
    // BEFORE the declaration pass bows out — the pass that can still reject
    // the pack is the one that should catch the typo (the og.register_hooks
    // discipline).
    {
        std::vector<const char*> names(std::begin(kCampaignHookNames),
                                       std::end(kCampaignHookNames));
        names.push_back("vars");
        lua_pushnil(L);
        while (lua_next(L, 1) != 0) {
            lua_pop(L, 1);  // value; the key stays for the next iteration
            if (lua_type(L, -1) != LUA_TSTRING)
                return luaL_error(
                    L, "og.register_campaign_hooks: keys are 'vars', "
                       "'picker_menu', 'picker_action', 'base_camp' and "
                       "'lineup' (got a %s key)",
                    luaL_typename(L, -1));
            const std::string key = lua_tostring(L, -1);
            bool known = false;
            for (const char* n : names)
                known = known || key == n;
            if (!known) {
                const std::string hint = did_you_mean(key, names);
                return luaL_error(
                    L, "og.register_campaign_hooks: unknown key '%s'%s",
                    key.c_str(), hint.c_str());
            }
        }
    }
    lua_getfield(L, 1, "picker_menu");    // abs index 2
    const bool has_menu = !lua_isnil(L, 2);
    if (has_menu && !lua_isfunction(L, 2))
        return luaL_error(
            L, "og.register_campaign_hooks: 'picker_menu' must be a function");
    lua_getfield(L, 1, "picker_action");  // abs index 3
    const bool has_action = !lua_isnil(L, 3);
    if (has_action && !lua_isfunction(L, 3))
        return luaL_error(L, "og.register_campaign_hooks: 'picker_action' "
                             "must be a function");
    lua_getfield(L, 1, "base_camp");      // abs index 4
    const bool has_zone = !lua_isnil(L, 4);
    if (has_zone && !lua_isfunction(L, 4))
        return luaL_error(L, "og.register_campaign_hooks: 'base_camp' "
                             "must be a function");
    lua_getfield(L, 1, "lineup");         // abs index 5
    const bool has_lineup = !lua_isnil(L, 5);
    if (has_lineup && !lua_istable(L, 5))
        return luaL_error(L, "og.register_campaign_hooks: 'lineup' must "
                             "be a table of { presets, power }");
    std::vector<std::string> lineup_presets;
    bool has_lineup_power = false;
    if (has_lineup) {
        // Same "the typo is caught by the pass that can still reject the
        // pack" discipline as the outer key walk.
        static const char* const kLineupKeys[] = {"presets", "power"};
        lua_pushnil(L);
        while (lua_next(L, 5) != 0) {
            lua_pop(L, 1);  // value; the key stays for the next iteration
            if (lua_type(L, -1) != LUA_TSTRING)
                return luaL_error(
                    L, "og.register_campaign_hooks: 'lineup' keys are "
                       "'presets' and 'power' (got a %s key)",
                    luaL_typename(L, -1));
            const std::string key = lua_tostring(L, -1);
            bool known = false;
            for (const char* n : kLineupKeys)
                known = known || key == n;
            if (!known) {
                const std::vector<const char*> names(std::begin(kLineupKeys),
                                                     std::end(kLineupKeys));
                const std::string hint = did_you_mean(key, names);
                return luaL_error(
                    L, "og.register_campaign_hooks: unknown 'lineup' key "
                       "'%s'%s",
                    key.c_str(), hint.c_str());
            }
        }
        lua_getfield(L, 5, "power");
        has_lineup_power = !lua_isnil(L, -1);
        if (has_lineup_power && !lua_isfunction(L, -1))
            return luaL_error(L, "og.register_campaign_hooks: "
                                 "'lineup.power' must be a function");
        lua_pop(L, 1);
        lua_getfield(L, 5, "presets");
        if (!lua_isnil(L, -1)) {
            if (!lua_istable(L, -1))
                return luaL_error(L, "og.register_campaign_hooks: "
                                     "'lineup.presets' must be an array of "
                                     "names");
            const lua_Integer count =
                static_cast<lua_Integer>(lua_rawlen(L, -1));
            // A sequence, or nothing: lua_rawlen reads 0 over a keyed
            // table and stops at the first hole, so either shape would
            // register fewer names than the book wrote — silently, the
            // one failure a registrar must never allow. Every key must
            // be one of 1..count.
            lua_Integer entries = 0;
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                lua_pop(L, 1);  // value; the key stays for the next step
                ++entries;
            }
            if (entries != count)
                return luaL_error(
                    L, "og.register_campaign_hooks: 'lineup.presets' must "
                       "be an array of names (a sequence of %d, but the "
                       "table holds %d entries)",
                    static_cast<int>(count), static_cast<int>(entries));
            if (count > hooks::kMaxBotPresets)
                return luaL_error(
                    L, "og.register_campaign_hooks: 'lineup.presets' names "
                       "%d squads (max %d)",
                    static_cast<int>(count), hooks::kMaxBotPresets);
            for (lua_Integer i = 1; i <= count; i++) {
                lua_rawgeti(L, -1, i);
                if (lua_type(L, -1) != LUA_TSTRING)
                    return luaL_error(
                        L, "og.register_campaign_hooks: lineup.presets[%d] "
                           "must be a string",
                        static_cast<int>(i));
                std::size_t len = 0;
                const char* name = lua_tolstring(L, -1, &len);
                std::string clipped(name, len);
                if (clipped.empty())
                    return luaL_error(
                        L, "og.register_campaign_hooks: lineup.presets[%d] "
                           "is empty",
                        static_cast<int>(i));
                // Clip, never refuse: the name is a 6-char cycler face and
                // a cosmetic overrun must not cost the campaign its book.
                if (clipped.size() > hooks::kLineupPresetNameMax)
                    clipped.resize(hooks::kLineupPresetNameMax);
                for (char& ch : clipped) {
                    ch = static_cast<char>(std::toupper(
                        static_cast<unsigned char>(ch)));
                }
                lineup_presets.push_back(std::move(clipped));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);  // presets
        if (!has_lineup_power && lineup_presets.empty())
            return luaL_error(L, "og.register_campaign_hooks: 'lineup' "
                                 "carries neither 'presets' nor 'power'");
    }
    if (!has_menu && !has_action && !has_zone && !has_lineup)
        return luaL_error(L, "og.register_campaign_hooks: register at least "
                             "one of 'picker_menu' / 'picker_action' / "
                             "'base_camp' / 'lineup'");

    std::vector<std::string> vars;
    lua_getfield(L, 1, "vars");
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1))
            return luaL_error(L, "og.register_campaign_hooks: 'vars' must "
                                 "be an array of names");
        const lua_Integer count =
            static_cast<lua_Integer>(lua_rawlen(L, -1));
        if (count > hooks::kCampaignVarsMax)
            return luaL_error(
                L, "og.register_campaign_hooks: 'vars' names %d variables "
                   "(max %d)",
                static_cast<int>(count), hooks::kCampaignVarsMax);
        for (lua_Integer i = 1; i <= count; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) != LUA_TSTRING)
                return luaL_error(
                    L, "og.register_campaign_hooks: vars[%d] must be a "
                       "string",
                    static_cast<int>(i));
            std::size_t len = 0;
            const char* name = lua_tolstring(L, -1, &len);
            if (!hooks::valid_campaign_var_name({name, len}))
                return luaL_error(
                    L, "og.register_campaign_hooks: vars[%d] '%s' must be "
                       "1-%d chars of [a-z0-9_]",
                    static_cast<int>(i), name, hooks::kCampaignVarNameMax);
            vars.emplace_back(name, len);
            lua_pop(L, 1);
        }
        // A hash-keyed table (vars = {watch_paid = 1}) has rawlen 0 and
        // would silently register nothing — reject any non-array key.
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            lua_pop(L, 1);  // value
            lua_Integer index = 0;
            if (lua_type(L, -1) == LUA_TNUMBER && lua_isinteger(L, -1))
                index = lua_tointeger(L, -1);
            if (index < 1 || index > count) {
                return luaL_error(
                    L, "og.register_campaign_hooks: 'vars' must be an "
                       "ARRAY of names (found a non-array key)");
            }
        }
    }
    lua_pop(L, 1);  // vars

    // Declaration pass: silent no-op, the og.register_hooks precedent — a
    // family chunk calling this neither rejects the pack nor registers.
    if (st != nullptr && st->mode == VmMode::Declare)
        return 0;
    if (st == nullptr || st->owner == nullptr)
        return luaL_error(L, "og.register_campaign_hooks: no world scripts");

    if (st->campaign_registered) {
        // One campaign, one book, or none: never raise (that would kill
        // this chunk's unrelated level hooks and make the winner an
        // accident of pack-id sort order). The query side answers "no
        // scripted picker" and reports the conflict once.
        if (st->campaign_conflict_source.empty())
            st->campaign_conflict_source = campaign_caller_source(L);
        return 0;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, st->campaign_hooks_ref);
    if (has_menu) {
        lua_pushvalue(L, 2);
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/picker_menu");
        lua_rawseti(L, -2, kCampaignMenuSlot);
    }
    if (has_action) {
        lua_pushvalue(L, 3);
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/picker_action");
        lua_rawseti(L, -2, kCampaignActionSlot);
    }
    if (has_zone) {
        lua_pushvalue(L, 4);
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/base_camp");
        lua_rawseti(L, -2, kCampaignZoneSlot);
    }
    if (has_lineup_power) {
        lua_getfield(L, 5, "power");
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/lineup_power");
        lua_rawseti(L, -2, kCampaignLineupPowerSlot);
    }
    lua_pop(L, 1);
    st->campaign_lineup_registered = has_lineup;
    st->campaign_lineup_presets = std::move(lineup_presets);
    st->campaign_vars = std::move(vars);
    st->campaign_registered = true;
    st->campaign_source = campaign_caller_source(L);
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
    // In the declaration pass the answer does not exist yet: the ids are
    // assigned by the install this very pass feeds, and a merged family file
    // asks about its own neighbours at chunk top level. A deferred token
    // keeps the shipped `assert(og.family_id(...))` idiom truthy and raises
    // on every actual use.
    const VmState* st = get_vm_state(L);
    if (st != nullptr && st->mode == VmMode::Declare) {
        push_family_ref(L);
        return 1;
    }
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
// pack chunk has registered, so the table it reads is the final one, which
// is also what lets a cross-file og.register_hooks count as the handler.
//
// Two severities, and the difference is what the pack could have known
// (format spec V3). A slot that arrived through install_classpack_data
// rather than from a declaration may legitimately have no Lua handler — the
// descriptor's own C++ do_special may serve it — so that stays a warning. A
// slot an og.family DECLARED is a slot whose costs, name and cast were
// written in one table by one author: leaving it castable with nothing to
// run is a mistake with no other reading, and it is a pack error.
// `cast = false` is how that author says "nothing, on purpose", and it
// counts as handled.
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
        const std::string* declaring_pack =
            lua_declared_family_pack(Order::Living, family_id);
        const char* family_label =
            fd->declared_id != nullptr ? fd->declared_id : fd->name;
        for (int slot = 1; slot < FD_NUM_SPECIALS; slot++) {
            const char* name = fd->special_names[slot];
            if (name == nullptr || std::strcmp(name, kSpecialNameNone) == 0)
                continue;
            if (fd->special_cost[slot] >= kSpecialCostDisabled)
                continue;
            lua_rawgeti(L, -1, slot);
            // A stored `false` is the declaration's explicit charged no-op,
            // and it is as much an answer as a function.
            const bool handled = lua_isfunction(L, -1) || lua_isboolean(L, -1);
            lua_pop(L, 1);
            if (handled)
                continue;
            if (declaring_pack == nullptr) {
                LogWarn("class pack: special '{}' (slot {}) of living family "
                        "'{}' has no handler and no default: casting it will "
                        "spend {} MP and do nothing\n",
                        name, slot, family_label, fd->special_cost[slot]);
                continue;
            }
            const std::string message =
                "class pack '" + *declaring_pack + "': special '" + name +
                "' (slot " + std::to_string(slot) + ") of living family '" +
                family_label + "' is castable for " +
                std::to_string(fd->special_cost[slot]) +
                " MP and nothing handles it — give the special a cast, give "
                "the list a default_cast, or say `cast = false` if the no-op "
                "is deliberate";
            LogError("{}\n", message);
            if (st.host_impl != nullptr)
                st.host_impl->record_error(declaring_pack->c_str(),
                                           message.c_str());
        }
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
}

}  // namespace

// ---------------------------------------------------------------------------
// VM scaffolding
// ---------------------------------------------------------------------------
//
// Everything a VM needs before a pack chunk may run in it. BOTH kinds go
// through here — the ordinary world VM and the throwaway declaration VM —
// so the two contexts differ in VmState::mode and nothing else. Any surface
// that appeared in one and not the other would be a second dialect of pack
// Lua, and a family chunk has to mean the same thing in both.
void install_vm_scaffolding(ScriptHost::Impl& impl, VmState& st)
{
    lua_State* L = impl.L;
    st.host_impl = &impl;
    lua_pushstring(L, "og.vmstate");
    lua_pushlightuserdata(L, &st);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_newtable(L);
    st.hooks_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    st.level_hooks_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    st.entity_hooks_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    st.campaign_hooks_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    st.lib_exports_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    st.lib_status_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_newtable(L);
    st.tuning_cache_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    st.tuning_cache_gen = family_tuning_generation();

    ensure_handle_metatables(L, &st);
    install_entity_bindings(L, &st);

    // Extend the sandbox og table with world-facing entry points.
    impl.push_sandbox_root();
    lua_getfield(L, -1, "og");
    install_entity_bindings_into_og(L, &st);
    lua_pushcfunction(L, og_register_hooks);
    lua_setfield(L, -2, "register_hooks");
    lua_pushcfunction(L, og_register_level_hooks);
    lua_setfield(L, -2, "register_level_hooks");
    lua_pushcfunction(L, og_register_campaign_hooks);
    lua_setfield(L, -2, "register_campaign_hooks");
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
    // The declaration vocabulary. Present in both VMs because one family
    // chunk is read by both: harvesting in the declaration pass, binding in
    // an ordinary one.
    lua_pushcfunction(L, og_family);
    lua_setfield(L, -2, "family");
    lua_pushcfunction(L, og_anims);
    lua_setfield(L, -2, "anims");
    lua_pushcfunction(L, og_pack);
    lua_setfield(L, -2, "pack");
    push_og_nil(L);
    lua_setfield(L, -2, "NIL");
    // og.api.version — feature detection for a pack straddling engine
    // releases, now that an unknown key is a load error rather than a
    // silently skipped one.
    lua_newtable(L);
    lua_pushinteger(L, kPackApiVersion);
    lua_setfield(L, -2, "version");
    push_frozen_view_of_top(L);
    lua_setfield(L, -2, "api");
    // The world entry points registered by hand above, behind the same
    // load-time fence the table-driven ones got. og.use and the three
    // register_* seams are load-time BY DESIGN and stay open (each
    // registrar carries its own campaign-dispatch error instead);
    // og.family_id answers a deferred token in the declaration pass, so it
    // handles its own case.
    for (const char* name : {"rand", "is_alive", "entity_id",
                             "set_entity_hooks"})
        fence_world_entry(L, &st, name);
    lua_pop(L, 2);
}

// ---------------------------------------------------------------------------
// WorldScripts
// ---------------------------------------------------------------------------

// Test-only budget override: a world VM built while this is positive uses it
// as the per-entry instruction budget instead of ScriptLimits' 5M default.
// The og_unit_modes headroom proof runs a full scripted-mode tick under a
// 10x-reduced budget; set it BEFORE the world's first dispatch (the VM is
// built lazily) and restore 0 afterwards. Nothing in production writes it
// (unconditional rather than TESTING-gated because the unit binaries link
// the production og_gameplay archive).
std::int64_t g_test_world_instruction_budget = 0;

namespace {

ScriptLimits world_script_limits()
{
    ScriptLimits limits;
    if (g_test_world_instruction_budget > 0)
        limits.instructions_per_call = g_test_world_instruction_budget;
    return limits;
}

}  // namespace

WorldScripts::WorldScripts()
    : host_(std::make_unique<ScriptHost>(world_script_limits()))
{
    ScriptHost::Impl& impl = host_->impl();
    lua_State* L = impl.L;

    detail_ = std::make_unique<WorldScriptsDetail>();
    detail_->state.owner = this;
    install_vm_scaffolding(impl, detail_->state);

    // The registries' combined generation: a module-only mutation must
    // rebuild long-lived hosts exactly like a script mutation.
    built_generation_ = pack_scripts_build_generation();

    // Load lib modules first (registration order — pack-id-lexicographic,
    // then filename-lexicographic), so every pack chunk can og.use them at
    // its top level. A module another module already pulled in on demand is
    // settled and skipped; failures are recorded and latched, never fatal.
    ScriptHost::Impl& host_impl = host_->impl();
    // Chunk top level, start to finish: the world API is closed for the
    // whole replay (VmState::loading). Hooks registered here run later, at
    // dispatch, with the fence down.
    detail_->state.loading = true;
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

    // Family chunks next, before any behavior script: the BIND half of the
    // two-context design. Their og.family calls hang hooks and specials
    // casts on the descriptors the declaration pass already installed, so
    // this is where a family's behavior enters the VM — and doing it
    // BEFORE scripts/ is what makes og.register_hooks an override rather
    // than a race (format spec V4/V7).
    detail_->state.current_chunk = ChunkKind::Family;
    for (const PackScript& fc : pack_family_chunks()) {
        // A pack whose declaration failed installed nothing, so binding
        // against it would report a second, worse error in every VM the
        // process ever builds. The installer already named the casualty.
        if (pack_declaration_failed(fc.pack_id))
            continue;
        detail_->state.current_pack = fc.pack_id;
        if (host_->run_chunk(fc.chunk_name, fc.source, fc.pack_id))
            continue;
        const auto& errs = host_->errors();
        LogError("class pack '{}': family chunk {} failed to bind: {}\n",
                 fc.pack_id, fc.chunk_name,
                 errs.empty() ? "(no error record)"
                              : errs.back().message.c_str());
    }

    // Replay pack scripts (registration order; env shared per pack).
    // current_pack scopes og.use to the pack whose chunk is loading.
    detail_->state.current_chunk = ChunkKind::Script;
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
    detail_->state.current_chunk = ChunkKind::None;
    detail_->state.loading = false;

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
        : impl_(ws.host().impl()), L_(impl_.L)
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
    // do_special function or a specials table (its declared-id keys were
    // resolved to slots at registration). For a table this selects [sp],
    // then "default". When the table holds neither, sets
    // *no_special_handler and returns false with a clean stack: the
    // dispatch is consumed as a successful no-op, with nothing called.
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
            // `cast = false` — the declaration's explicit charged no-op. It
            // is a HANDLER, so it beats `default` rather than falling
            // through to it: the slot was written to do nothing, on purpose.
            if (lua_isboolean(L_, -1) && !lua_toboolean(L_, -1)) {
                lua_pop(L_, 4);  // entry + specials + family + hooks root
                *no_special_handler = true;
                return false;
            }
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
        case FamilyHook::OnActOverride: return "hook:on_act_override";
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
    // (declared specials occupy slots 1..5), so selection falls to
    // `default`.
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

bool on_act_override(const FamilyDescriptor* fd, living* self)
{
    if (fd == nullptr)
        return false;
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::OnActOverride, true,
                                 static_cast<walker*>(self)))
        return *r;
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

std::uint32_t level_hook_kinds_for(int level_id)
{
    if (pack_scripts().empty())
        return 0;
    WorldScripts& ws = active_world_scripts();
    lua_State* L = ws.host().impl().L;
    VmState* st = get_vm_state(L);
    if (st == nullptr)
        return 0;
    std::uint32_t kinds = 0;
    for (const auto& h : kLevelHookNames) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, st->level_hooks_ref);
        lua_rawgeti(L, -1, level_hook_key(h.kind, level_id));
        if (lua_isfunction(L, -1))
            kinds |= 1u << static_cast<unsigned>(h.kind);
        lua_pop(L, 2);
    }
    return kinds;
}

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

    GameWorld* world =
        (current_game != nullptr) ? current_game->world : nullptr;

    // Kill attribution (D3): resolve the combat stamp into (killer,
    // killer_team) for BOTH hook arms. The stamp must be fresh
    // (kKillAttributionTicks); a stale or absent stamp is an environment
    // death (nil, -1). The killer handle may be a corpse (mutual kills) —
    // only a swept id resolves to nil; the stamped TEAM survives either way.
    walker* killer = nullptr;
    lua_Integer killer_team = -1;
    if (world != nullptr && self->last_attacker_id() != 0 &&
        world->tick_count_ - self->last_attacker_tick() <=
            og::sim::kKillAttributionTicks) {
        killer = world->find_by_id(self->last_attacker_id());
        killer_team = static_cast<lua_Integer>(self->last_attacker_team());
    }
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
                if (killer != nullptr)
                    push_walker_handle(L, killer, gen);
                else
                    lua_pushnil(L);
                lua_pushinteger(L, killer_team);
                impl.protected_call("entity:on_death", 3, 0);
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
    if (world == nullptr)
        return;
    if (!push_level_hook_fn(L, st, LevelHook::EntityDeath, world->id))
        return;
    const std::uint64_t gen = push_dispatch_gen(L);
    push_walker_handle(L, self, gen);
    if (killer != nullptr)
        push_walker_handle(L, killer, gen);
    else
        lua_pushnil(L);
    lua_pushinteger(L, killer_team);
    impl.protected_call("level:on_entity_death", 3, 0);
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

bool level_mode_init(GameWorld* world)
{
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::ModeInit));
    if (st == nullptr || world == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    if (!push_level_hook_fn(L, st, LevelHook::ModeInit, world->id))
        return false;
    const std::uint64_t gen = push_dispatch_gen(L);
    lua_pushinteger(L, world->id);
    // A failing init reports false so the mode stays inactive and the
    // level falls to classic rules (the CTF fewer-than-two-flag-teams
    // shape). The error itself is recorded by protected_call (script host
    // error store). The hook builds its own census over the live world
    // (mode_match.lua census_inputs) — the plan phase and its marshaling
    // layer are retired (#218 staged lobby).
    const bool ok = impl.protected_call("level:on_mode_init", 1, 0);
    pop_dispatch_gen(L, gen);
    return ok;
}

void level_mode_tick(GameWorld* world)
{
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::ModeTick));
    if (st == nullptr || world == nullptr)
        return;
    ScriptHost::Impl& impl = st->owner->host().impl();
    if (!push_level_hook_fn(impl.L, st, LevelHook::ModeTick, world->id))
        return;
    const std::uint64_t gen = push_dispatch_gen(impl.L);
    lua_pushinteger(impl.L, world->id);
    lua_pushinteger(impl.L,
                    static_cast<lua_Integer>(world->level_tick_count()));
    impl.protected_call("level:on_mode_tick", 2, 0);
    pop_dispatch_gen(impl.L, gen);
}

void level_respawn(GameWorld* world, walker* revived)
{
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::Respawn));
    if (st == nullptr || world == nullptr || revived == nullptr)
        return;
    ScriptHost::Impl& impl = st->owner->host().impl();
    if (!push_level_hook_fn(impl.L, st, LevelHook::Respawn, world->id))
        return;
    const std::uint64_t gen = push_dispatch_gen(impl.L);
    push_walker_handle(impl.L, revived, gen);
    impl.protected_call("level:on_respawn", 1, 0);
    pop_dispatch_gen(impl.L, gen);
}

short level_damage_gate(walker* target, walker* attacker, short amount)
{
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::Damage));
    if (st == nullptr || target == nullptr)
        return amount;
    GameWorld* world =
        (current_game != nullptr) ? current_game->world : nullptr;
    if (world == nullptr)
        return amount;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    if (!push_level_hook_fn(L, st, LevelHook::Damage, world->id))
        return amount;
    const std::uint64_t gen = push_dispatch_gen(L);
    push_walker_handle(L, target, gen);
    if (attacker != nullptr)
        push_walker_handle(L, attacker, gen);
    else
        lua_pushnil(L);
    lua_pushinteger(L, static_cast<lua_Integer>(amount));
    short result = amount;
    // A hook ERROR keeps the authored amount (R9: the hook counts as absent
    // for this dispatch; deterministic on every peer since only the
    // authoritative world dispatches).
    if (impl.protected_call("level:on_damage", 3, 1)) {
        if (lua_isboolean(L, -1)) {
            // false = cancel the hit outright; true = keep the amount.
            if (!lua_toboolean(L, -1))
                result = -1;
        } else if (lua_isnumber(L, -1)) {
            // Replacement amount, clamped to [0, 32767]; fractions truncate
            // toward zero (deterministic on every peer).
            lua_Number v = lua_tonumber(L, -1);
            // Negated tests on purpose: NaN compares false against everything,
            // so `v < 0` would let it through to a cast that is undefined
            // behavior (UBSan flags it as float-cast-overflow). Written this
            // way NaN takes the first branch and lands on 0. Every ordinary
            // value behaves exactly as before.
            if (!(v >= 0))
                v = 0;
            else if (!(v <= 32767))
                v = 32767;
            result = static_cast<short>(v);
        }
        // nil (or any other value) keeps the amount.
        lua_pop(L, 1);
    }
    pop_dispatch_gen(L, gen);
    return result;
}

// ---------------------------------------------------------------------------
// Campaign-hook dispatch (docs/campaign-scripting-design.md)
// ---------------------------------------------------------------------------

namespace {

// RAII arm of the campaign-dispatch fence around a protected call. Never
// arms at nested entry depth: a campaign hook dispatched from inside
// another Lua entry would share that frame's budget and un-fence on the
// inner scope's exit, so nesting is a caller bug — assert, refuse to arm,
// and the dispatcher answers "no scripted picker".
class CampaignDispatchScope {
public:
    CampaignDispatchScope(VmState& st, ScriptHost::Impl& impl) : st_(st)
    {
        assert(impl.entry_depth == 0 && !st.campaign_dispatch);
        if (impl.entry_depth == 0 && !st.campaign_dispatch) {
            st_.campaign_dispatch = true;
            armed_ = true;
        }
    }
    ~CampaignDispatchScope()
    {
        if (armed_)
            st_.campaign_dispatch = false;
    }
    CampaignDispatchScope(const CampaignDispatchScope&) = delete;
    CampaignDispatchScope& operator=(const CampaignDispatchScope&) = delete;
    bool armed() const { return armed_; }

private:
    VmState& st_;
    bool armed_ = false;
};

// Gate shared by every campaign query/dispatch: no packs → nothing; a
// conflicted registration answers "no scripted picker" and records the
// conflict as a script error ONCE (the once-latch lives in the VmState, so
// a VM rebuild that replays the same conflict reports again — correctly,
// since the replay re-created it).
VmState* campaign_vm_state()
{
    if (pack_scripts().empty())
        return nullptr;
    WorldScripts& ws = active_world_scripts();
    ScriptHost::Impl& impl = ws.host().impl();
    VmState* st = get_vm_state(impl.L);
    if (st == nullptr || !st->campaign_registered)
        return nullptr;
    if (!st->campaign_conflict_source.empty()) {
        if (!st->campaign_conflict_reported) {
            st->campaign_conflict_reported = true;
            const std::string message =
                "duplicate og.register_campaign_hooks (first: " +
                st->campaign_source + ", second: " +
                st->campaign_conflict_source +
                ") — one campaign, one book: no scripted picker will be "
                "served";
            LogError("class pack: {}\n", message);
            impl.record_error("og.register_campaign_hooks", message.c_str());
        }
        return nullptr;
    }
    return st;
}

// Pushes the registered hook fn for `slot`, or returns false, stack clean.
bool push_campaign_hook_fn(lua_State* L, VmState* st, lua_Integer slot)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->campaign_hooks_ref);
    lua_rawgeti(L, -1, slot);
    if (lua_isfunction(L, -1)) {
        lua_remove(L, -2);
        return true;
    }
    lua_pop(L, 2);
    return false;
}

bool malformed_page(ScriptHost::Impl& impl, const std::string& what)
{
    const std::string message =
        "campaign picker_menu returned a malformed page: " + what;
    LogError("class pack: {}\n", message);
    impl.record_error("campaign:picker_menu", message.c_str());
    return false;
}

// Raw string field read: false = present but not a string (nil answers
// true with `out` untouched). Raw accessors throughout the parse — the
// page came back from an untrusted chunk, and a metamethod firing inside
// an unprotected lua_getfield would raise with no handler.
bool read_string_field(lua_State* L, int idx, const char* key,
                       std::string& out, bool& present)
{
    lua_pushstring(L, key);
    lua_rawget(L, idx > 0 ? idx : idx - 1);
    present = !lua_isnil(L, -1);
    if (!present) {
        lua_pop(L, 1);
        return true;
    }
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        return false;
    }
    std::size_t len = 0;
    const char* s = lua_tolstring(L, -1, &len);
    out.assign(s, len);
    lua_pop(L, 1);
    return true;
}

// Raw integer field read (integer subtype required); nil answers true with
// `present` false and `out` untouched.
bool read_int_field_opt(lua_State* L, int idx, const char* key, int& out,
                        bool& present)
{
    lua_pushstring(L, key);
    lua_rawget(L, idx > 0 ? idx : idx - 1);
    present = !lua_isnil(L, -1);
    if (!present) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_isinteger(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    out = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return true;
}

// Raw integer field read (integer subtype required); nil answers true and
// leaves `out` at its default.
bool read_int_field(lua_State* L, int idx, const char* key, int& out)
{
    bool present = false;
    return read_int_field_opt(L, idx, key, out, present);
}

// Raw boolean field read; nil answers true and leaves `out` at its default.
bool read_bool_field(lua_State* L, int idx, const char* key, bool& out)
{
    lua_pushstring(L, key);
    lua_rawget(L, idx > 0 ? idx : idx - 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_isboolean(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return true;
}

bool parse_entry_kind(const std::string& s, CampaignPageEntry::Kind& out)
{
    if (s == "level") {
        out = CampaignPageEntry::Kind::Level;
        return true;
    }
    if (s == "page") {
        out = CampaignPageEntry::Kind::Page;
        return true;
    }
    if (s == "action") {
        out = CampaignPageEntry::Kind::Action;
        return true;
    }
    return false;
}

// Reads one page-entry table (stack top) into `entry` — the field
// vocabulary shared by picker pages and zone actions widgets. False sets
// `bad` to the malformed-message tail; the caller owns the stack cleanup.
bool read_page_entry(lua_State* L, const std::string& at,
                     CampaignPageEntry& entry, std::string& bad)
{
    bool present = false;
    if (!read_string_field(L, -1, "id", entry.id, present) || !present ||
        entry.id.empty()) {
        bad = at + ".id is missing or not a string";
        return false;
    }
    std::string kind;
    if (!read_string_field(L, -1, "kind", kind, present) || !present ||
        !parse_entry_kind(kind, entry.kind)) {
        bad = at + ".kind must be \"level\", \"page\" or \"action\"";
        return false;
    }
    if (!read_string_field(L, -1, "label", entry.label, present)) {
        bad = at + ".label is not a string";
        return false;
    }
    if (!read_string_field(L, -1, "note", entry.note, present)) {
        bad = at + ".note is not a string";
        return false;
    }
    if (!read_int_field(L, -1, "level", entry.level)) {
        bad = at + ".level is not an integer";
        return false;
    }
    if (!read_int_field(L, -1, "cost", entry.cost)) {
        bad = at + ".cost is not an integer";
        return false;
    }
    if (!read_bool_field(L, -1, "done", entry.done)) {
        bad = at + ".done is not a boolean";
        return false;
    }
    // #207: the replay mark (level rows; arm-only-when-cleared is the
    // engine's check, not the script's).
    if (!read_bool_field(L, -1, "replay", entry.replay)) {
        bad = at + ".replay is not a boolean";
        return false;
    }
    return true;
}

// Parses the page table at the top of the stack into `out` (stack left
// unchanged). C++ owns the bounds: entries clip at kCampaignPageMaxEntries,
// lines at kCampaignPageMaxLines; a missing/mistyped required field is
// malformed → false, recorded with the field's name.
bool parse_campaign_page(lua_State* L, ScriptHost::Impl& impl,
                         CampaignPage& out)
{
    if (!lua_istable(L, -1))
        return malformed_page(impl, std::string("returned a ") +
                                        luaL_typename(L, -1) +
                                        ", not a page table");
    bool present = false;
    if (!read_string_field(L, -1, "title", out.title, present) || !present)
        return malformed_page(impl, "'title' is missing or not a string");

    lua_pushstring(L, "lines");
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return malformed_page(impl, "'lines' is not an array");
        }
        const lua_Integer count = std::min<lua_Integer>(
            static_cast<lua_Integer>(lua_rawlen(L, -1)),
            kCampaignPageMaxLines);
        for (lua_Integer i = 1; i <= count; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) != LUA_TSTRING) {
                lua_pop(L, 2);
                return malformed_page(
                    impl, "lines[" + std::to_string(i) + "] is not a string");
            }
            std::size_t len = 0;
            const char* s = lua_tolstring(L, -1, &len);
            out.lines.emplace_back(s, len);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "entries");
    lua_rawget(L, -2);
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return malformed_page(impl, "'entries' is not an array");
        }
        const lua_Integer count = std::min<lua_Integer>(
            static_cast<lua_Integer>(lua_rawlen(L, -1)),
            kCampaignPageMaxEntries);
        for (lua_Integer i = 1; i <= count; i++) {
            const std::string at = "entries[" + std::to_string(i) + "]";
            lua_rawgeti(L, -1, i);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 2);
                return malformed_page(impl, at + " is not a table");
            }
            CampaignPageEntry entry;
            std::string bad;
            if (!read_page_entry(L, at, entry, bad)) {
                lua_pop(L, 2);
                return malformed_page(impl, bad);
            }
            out.entries.push_back(std::move(entry));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return true;
}

// ---------------------------------------------------------------------------
// Base Camp zone parse (docs/basecamp-zones-design.md "The widget contract")
// ---------------------------------------------------------------------------

bool malformed_zone(ScriptHost::Impl& impl, const std::string& what)
{
    const std::string message =
        "campaign base_camp returned a malformed zone: " + what;
    LogError("class pack: {}\n", message);
    impl.record_error("campaign:base_camp", message.c_str());
    return false;
}

bool parse_zone_kind(const std::string& s, CampaignZoneWidget::Kind& out)
{
    if (s == "roster") {
        out = CampaignZoneWidget::Kind::Roster;
        return true;
    }
    if (s == "text") {
        out = CampaignZoneWidget::Kind::Text;
        return true;
    }
    if (s == "actions") {
        out = CampaignZoneWidget::Kind::Actions;
        return true;
    }
    if (s == "readout") {
        out = CampaignZoneWidget::Kind::Readout;
        return true;
    }
    return false;
}

// The roster widget's `locks` array (widget table at stack top). Each lock
// addresses heroes by campaign_tag (1..255) or unset = true — exactly one
// of the two — never by guy id (ids regenerate every mission).
bool parse_zone_locks(lua_State* L, const std::string& at,
                      CampaignZoneWidget& widget, std::string& bad)
{
    lua_pushstring(L, "locks");
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        bad = at + ".locks is not an array";
        return false;
    }
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, -1));
    if (count > kCampaignZoneMaxRosterLocks) {
        lua_pop(L, 1);
        bad = at + ".locks lists " + std::to_string(count) + " locks (max " +
              std::to_string(kCampaignZoneMaxRosterLocks) + ")";
        return false;
    }
    for (lua_Integer j = 1; j <= count; j++) {
        const std::string lock_at = at + ".locks[" + std::to_string(j) + "]";
        lua_rawgeti(L, -1, j);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            bad = lock_at + " is not a table";
            return false;
        }
        CampaignRosterLock lock;
        bool tag_present = false;
        int tag = 0;
        if (!read_int_field_opt(L, -1, "tag", tag, tag_present)) {
            lua_pop(L, 2);
            bad = lock_at + ".tag is not an integer";
            return false;
        }
        if (!read_bool_field(L, -1, "unset", lock.unset)) {
            lua_pop(L, 2);
            bad = lock_at + ".unset is not a boolean";
            return false;
        }
        if (tag_present == lock.unset) {
            lua_pop(L, 2);
            bad = lock_at + " needs exactly one of tag / unset = true";
            return false;
        }
        if (tag_present) {
            if (tag < 1 || tag > 255) {
                lua_pop(L, 2);
                bad = lock_at + ".tag must be 1..255 (0 is the unset state "
                                "— lock it with unset = true)";
                return false;
            }
            lock.tag = tag;
        }
        bool present = false;
        if (!read_string_field(L, -1, "reason", lock.reason, present)) {
            lua_pop(L, 2);
            bad = lock_at + ".reason is not a string";
            return false;
        }
        widget.locks.push_back(std::move(lock));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// The roster widget's optional `assign` chip spec (widget table at stack
// top): key (safe-id charset), exactly two non-empty labels, optional
// frozen reason.
bool parse_zone_assign(lua_State* L, const std::string& at,
                       CampaignZoneWidget& widget, std::string& bad)
{
    lua_pushstring(L, "assign");
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        bad = at + ".assign is not a table";
        return false;
    }
    bool present = false;
    if (!read_string_field(L, -1, "key", widget.assign.key, present) ||
        !present || !valid_campaign_var_name(widget.assign.key)) {
        lua_pop(L, 1);
        bad = at + ".assign.key must be 1-" +
              std::to_string(kCampaignVarNameMax) + " chars of [a-z0-9_]";
        return false;
    }
    lua_pushstring(L, "labels");
    lua_rawget(L, -2);
    if (!lua_istable(L, -1) ||
        static_cast<lua_Integer>(lua_rawlen(L, -1)) != 2) {
        lua_pop(L, 2);
        bad = at + ".assign.labels must list exactly 2 labels";
        return false;
    }
    for (lua_Integer k = 1; k <= 2; k++) {
        lua_rawgeti(L, -1, k);
        std::size_t len = 0;
        const char* s = lua_type(L, -1) == LUA_TSTRING
                            ? lua_tolstring(L, -1, &len)
                            : nullptr;
        if (s == nullptr || len == 0) {
            lua_pop(L, 3);
            bad = at + ".assign.labels[" + std::to_string(k) +
                  "] must be a non-empty string";
            return false;
        }
        widget.assign.labels.emplace_back(s, len);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  // labels
    if (!read_string_field(L, -1, "frozen", widget.assign.frozen, present)) {
        lua_pop(L, 1);
        bad = at + ".assign.frozen is not a string";
        return false;
    }
    widget.assign.active = true;
    lua_pop(L, 1);  // assign
    return true;
}

bool parse_zone_roster(lua_State* L, const std::string& at,
                       CampaignZoneWidget& widget, std::string& bad)
{
    const struct {
        const char* key;
        bool CampaignZoneWidget::* member;
    } caps[] = {
        {"can_deploy", &CampaignZoneWidget::can_deploy},
        {"can_train", &CampaignZoneWidget::can_train},
        {"can_reorder", &CampaignZoneWidget::can_reorder},
        {"can_team", &CampaignZoneWidget::can_team},
        {"can_hire", &CampaignZoneWidget::can_hire},
    };
    for (const auto& cap : caps) {
        if (!read_bool_field(L, -1, cap.key, widget.*(cap.member))) {
            bad = at + "." + cap.key + " is not a boolean";
            return false;
        }
    }
    if (!parse_zone_locks(L, at, widget, bad))
        return false;
    return parse_zone_assign(L, at, widget, bad);
}

bool parse_zone_text(lua_State* L, const std::string& at,
                     CampaignZoneWidget& widget, std::string& bad)
{
    lua_pushstring(L, "lines");
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        bad = at + ".lines is not an array";
        return false;
    }
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, -1));
    if (count > kCampaignZoneMaxTextLines) {
        lua_pop(L, 1);
        bad = at + " lists " + std::to_string(count) + " lines (max " +
              std::to_string(kCampaignZoneMaxTextLines) + ")";
        return false;
    }
    for (lua_Integer j = 1; j <= count; j++) {
        lua_rawgeti(L, -1, j);
        if (lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 2);
            bad = at + ".lines[" + std::to_string(j) + "] is not a string";
            return false;
        }
        std::size_t len = 0;
        const char* s = lua_tolstring(L, -1, &len);
        widget.lines.emplace_back(s, len);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// `total_entries` accumulates the ZONE-wide action-row count: the 16-row
// cap is what keeps the appended ordinal band 49..71 sufficient (16 rows +
// 4 pagers + 3 spare), so it binds across widgets, not per widget.
bool parse_zone_actions(lua_State* L, const std::string& at,
                        CampaignZoneWidget& widget, int& total_entries,
                        std::string& bad)
{
    lua_pushstring(L, "entries");
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        bad = at + ".entries is not an array";
        return false;
    }
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, -1));
    if (count > kCampaignZoneMaxActionEntries ||
        static_cast<int>(count) + total_entries >
            kCampaignZoneMaxActionEntries) {
        lua_pop(L, 1);
        bad = "the zone lists more than " +
              std::to_string(kCampaignZoneMaxActionEntries) +
              " action rows in total";
        return false;
    }
    total_entries += static_cast<int>(count);
    for (lua_Integer j = 1; j <= count; j++) {
        const std::string entry_at =
            at + ".entries[" + std::to_string(j) + "]";
        lua_rawgeti(L, -1, j);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            bad = entry_at + " is not a table";
            return false;
        }
        CampaignPageEntry entry;
        if (!read_page_entry(L, entry_at, entry, bad)) {
            lua_pop(L, 2);
            return false;
        }
        widget.entries.push_back(std::move(entry));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

bool parse_zone_readout(lua_State* L, const std::string& at,
                        CampaignZoneWidget& widget, std::string& bad)
{
    lua_pushstring(L, "items");
    lua_rawget(L, -2);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        bad = at + ".items is not an array";
        return false;
    }
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, -1));
    if (count > kCampaignZoneMaxReadoutItems) {
        lua_pop(L, 1);
        bad = at + " lists " + std::to_string(count) +
              " readout items (max " +
              std::to_string(kCampaignZoneMaxReadoutItems) + ")";
        return false;
    }
    for (lua_Integer j = 1; j <= count; j++) {
        const std::string item_at =
            at + ".items[" + std::to_string(j) + "]";
        lua_rawgeti(L, -1, j);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            bad = item_at + " is not a table";
            return false;
        }
        CampaignZoneWidget::ReadoutItem item;
        bool present = false;
        if (!read_string_field(L, -1, "label", item.label, present) ||
            !present) {
            lua_pop(L, 2);
            bad = item_at + ".label is missing or not a string";
            return false;
        }
        if (!read_string_field(L, -1, "value", item.value, present) ||
            !present) {
            lua_pop(L, 2);
            bad = item_at + ".value is missing or not a string";
            return false;
        }
        widget.items.push_back(std::move(item));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return true;
}

// Parses widgets[i] (stack top) into `widget`. Only the declared kind's
// fields are read — the rest keep their defaults, matching the page
// parser's known-keys-only discipline. False sets `bad`.
bool parse_zone_widget(lua_State* L, const std::string& at,
                       CampaignZoneWidget& widget, int& total_entries,
                       std::string& bad)
{
    bool present = false;
    std::string kind;
    if (!read_string_field(L, -1, "kind", kind, present) || !present ||
        !parse_zone_kind(kind, widget.kind)) {
        bad = at + ".kind must be \"roster\", \"text\", \"actions\" or "
                   "\"readout\"";
        return false;
    }
    if (!read_int_field(L, -1, "weight", widget.weight)) {
        bad = at + ".weight is not an integer";
        return false;
    }
    if (widget.weight < 0) {
        bad = at + ".weight is negative";
        return false;
    }
    if (widget.weight > kCampaignZoneMaxWeight) {
        bad = at + ".weight is " + std::to_string(widget.weight) +
              " row units (max " + std::to_string(kCampaignZoneMaxWeight) +
              ")";
        return false;
    }
    switch (widget.kind) {
    case CampaignZoneWidget::Kind::Roster:
        return parse_zone_roster(L, at, widget, bad);
    case CampaignZoneWidget::Kind::Text:
        return parse_zone_text(L, at, widget, bad);
    case CampaignZoneWidget::Kind::Actions:
        return parse_zone_actions(L, at, widget, total_entries, bad);
    case CampaignZoneWidget::Kind::Readout:
        return parse_zone_readout(L, at, widget, bad);
    }
    return false;
}

// Parses the zone table at the top of the stack into `out` (stack left
// unchanged). Unlike parse_campaign_page, every bound is a hard rejection
// — a clip would silently re-shape a composition the author counted on —
// so an over-budget zone is malformed and the caller renders the DEFAULT
// zone (docs/basecamp-zones-design.md "Bounds arithmetic").
bool parse_campaign_zone(lua_State* L, ScriptHost::Impl& impl,
                         CampaignZone& out)
{
    if (!lua_istable(L, -1))
        return malformed_zone(impl, std::string("returned a ") +
                                        luaL_typename(L, -1) +
                                        ", not a zone table");
    lua_pushstring(L, "widgets");
    lua_rawget(L, -2);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return malformed_zone(impl, "'widgets' is missing or not an array");
    }
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, -1));
    if (count > kCampaignZoneMaxWidgets) {
        lua_pop(L, 1);
        return malformed_zone(
            impl, "lists " + std::to_string(count) + " widgets (max " +
                      std::to_string(kCampaignZoneMaxWidgets) + ")");
    }
    int roster_widgets = 0;
    int readout_widgets = 0;
    int actions_widgets = 0;
    int text_widgets = 0;
    int total_entries = 0;
    for (lua_Integer i = 1; i <= count; i++) {
        const std::string at = "widgets[" + std::to_string(i) + "]";
        lua_rawgeti(L, -1, i);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            return malformed_zone(impl, at + " is not a table");
        }
        CampaignZoneWidget widget;
        std::string bad;
        if (!parse_zone_widget(L, at, widget, total_entries, bad)) {
            lua_pop(L, 2);
            return malformed_zone(impl, bad);
        }
        switch (widget.kind) {
        case CampaignZoneWidget::Kind::Roster:
            roster_widgets++;
            break;
        case CampaignZoneWidget::Kind::Readout:
            readout_widgets++;
            break;
        case CampaignZoneWidget::Kind::Actions:
            actions_widgets++;
            break;
        case CampaignZoneWidget::Kind::Text:
            text_widgets++;
            break;
        }
        out.widgets.push_back(std::move(widget));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  // widgets
    // Per-kind caps: the roster/readout are singleton hardware (one set of
    // deploy/chip/move-up ordinals, one readout row); actions/text split
    // the remaining band at most two ways each.
    if (roster_widgets != 1)
        return malformed_zone(
            impl, "declares " + std::to_string(roster_widgets) +
                      " roster widgets (exactly one required)");
    if (readout_widgets > kCampaignZoneMaxReadoutWidgets)
        return malformed_zone(
            impl, "declares " + std::to_string(readout_widgets) +
                      " readout widgets (max " +
                      std::to_string(kCampaignZoneMaxReadoutWidgets) + ")");
    if (actions_widgets > kCampaignZoneMaxActionsWidgets)
        return malformed_zone(
            impl, "declares " + std::to_string(actions_widgets) +
                      " actions widgets (max " +
                      std::to_string(kCampaignZoneMaxActionsWidgets) + ")");
    if (text_widgets > kCampaignZoneMaxTextWidgets)
        return malformed_zone(
            impl, "declares " + std::to_string(text_widgets) +
                      " text widgets (max " +
                      std::to_string(kCampaignZoneMaxTextWidgets) + ")");
    return true;
}

}  // namespace

bool campaign_picker_registered()
{
    return campaign_vm_state() != nullptr;
}

std::vector<std::string> campaign_registered_vars()
{
    VmState* st = campaign_vm_state();
    if (st == nullptr)
        return {};
    return st->campaign_vars;
}

bool campaign_picker_page(const std::string& page_id, CampaignPage& out)
{
    VmState* st = campaign_vm_state();
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    if (!push_campaign_hook_fn(L, st, kCampaignMenuSlot))
        return false;
    CampaignDispatchScope scope(*st, impl);
    if (!scope.armed()) {
        lua_pop(L, 1);
        return false;
    }
    const std::uint64_t gen = push_dispatch_gen(L);
    lua_pushlstring(L, page_id.data(), page_id.size());
    bool ok = impl.protected_call("campaign:picker_menu", 1, 1);
    if (ok) {
        out = CampaignPage{};
        ok = parse_campaign_page(L, impl, out);
        lua_pop(L, 1);
    }
    pop_dispatch_gen(L, gen);
    return ok;
}

bool campaign_picker_action(const std::string& entry_id,
                            CampaignActionResult& out)
{
    VmState* st = campaign_vm_state();
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    if (!push_campaign_hook_fn(L, st, kCampaignActionSlot))
        return false;
    CampaignDispatchScope scope(*st, impl);
    if (!scope.armed()) {
        lua_pop(L, 1);
        return false;
    }
    out = CampaignActionResult{};
    const std::uint64_t gen = push_dispatch_gen(L);
    lua_pushlstring(L, entry_id.data(), entry_id.size());
    // A hook ERROR answers ok=false but still counts as dispatched: a spend
    // already applied sticks, so the caller must refresh the page either
    // way rather than fall back to the stock UI.
    if (impl.protected_call("campaign:picker_action", 1, 1)) {
        out.ok = true;
        // nil (or any non-table) = no toast; a table's optional `message`
        // string is the toast. Raw access: untrusted result value.
        if (lua_istable(L, -1)) {
            bool present = false;
            read_string_field(L, -1, "message", out.message, present);
            // Optional `level` (D3): the scenario the action answers with.
            // Read at 64-bit width so an oversized value cannot wrap into
            // the loader's id range; anything outside 0..32767 — or a
            // non-integer — stays the "no level carried" default (-1).
            lua_pushstring(L, "level");
            lua_rawget(L, -2);
            if (lua_isinteger(L, -1)) {
                const lua_Integer level = lua_tointeger(L, -1);
                if (level >= 0 && level <= 32767)
                    out.level = static_cast<int>(level);
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    pop_dispatch_gen(L, gen);
    return true;
}

bool campaign_lineup_registered()
{
    const VmState* st = campaign_vm_state();
    return st != nullptr && st->campaign_lineup_registered;
}

bool campaign_lineup_presets(std::vector<std::string>& out)
{
    const VmState* st = campaign_vm_state();
    if (st == nullptr || !st->campaign_lineup_registered)
        return false;
    out = st->campaign_lineup_presets;
    return true;
}

bool campaign_fighter_power(const LineupPowerRow& row, long long& out)
{
    VmState* st = campaign_vm_state();
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    if (!push_campaign_hook_fn(L, st, kCampaignLineupPowerSlot))
        return false;
    CampaignDispatchScope scope(*st, impl);
    if (!scope.armed()) {
        lua_pop(L, 1);
        return false;
    }
    const std::uint64_t gen = push_dispatch_gen(L);
    lua_createtable(L, 0, 8);
    const auto set_string = [L](const char* key, const std::string& value) {
        lua_pushstring(L, key);
        lua_pushlstring(L, value.data(), value.size());
        lua_rawset(L, -3);
    };
    const auto set_int = [L](const char* key, int value) {
        lua_pushstring(L, key);
        lua_pushinteger(L, value);
        lua_rawset(L, -3);
    };
    set_string("family", row.family);
    set_int("level", row.level);
    set_int("hp", row.hp);
    set_int("mp", row.mp);
    set_int("armor", row.armor);
    set_int("damage", row.damage);
    set_int("stepsize", row.stepsize);
    set_int("fire_frequency", row.fire_frequency);
    bool ok = false;
    if (impl.protected_call("campaign:lineup_power", 1, 1)) {
        // A number the engine can hold, or nothing: a price it cannot read
        // is no price at all, and the band shows `--` rather than an
        // invented figure. lua_isnumber would accept the STRING "42"; a
        // price must be a number the book actually computed. An integer
        // is taken as is; a float must be finite and inside the int64
        // range (then truncated toward zero) — a static_cast of NaN or an
        // infinity is undefined and used to render INT64_MIN on the band.
        std::string refusal;
        if (lua_type(L, -1) != LUA_TNUMBER) {
            refusal = std::string("campaign lineup.power returned a ") +
                      luaL_typename(L, -1) + ", not a number";
        } else if (lua_isinteger(L, -1)) {
            out = static_cast<long long>(lua_tointeger(L, -1));
            ok = true;
        } else {
            const lua_Number value = lua_tonumber(L, -1);
            // 2^63 as a double is exactly representable; the open bound on
            // the right keeps the truncation inside LLONG_MAX.
            constexpr lua_Number kInt64Span = 9223372036854775808.0;
            if (std::isfinite(value) && value >= -kInt64Span &&
                value < kInt64Span) {
                out = static_cast<long long>(std::trunc(value));
                ok = true;
            } else {
                refusal = std::format(
                    "campaign lineup.power returned {}, not a finite "
                    "integer",
                    value);
            }
        }
        if (!ok) {
            LogError("class pack: {}\n", refusal);
            impl.record_error("campaign:lineup_power", refusal.c_str());
        }
        lua_pop(L, 1);
    }
    pop_dispatch_gen(L, gen);
    return ok;
}

bool campaign_zone_registered()
{
    VmState* st = campaign_vm_state();
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    if (!push_campaign_hook_fn(L, st, kCampaignZoneSlot))
        return false;
    lua_pop(L, 1);
    return true;
}

bool campaign_zone(CampaignZone& out)
{
    VmState* st = campaign_vm_state();
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    if (!push_campaign_hook_fn(L, st, kCampaignZoneSlot))
        return false;
    CampaignDispatchScope scope(*st, impl);
    if (!scope.armed()) {
        lua_pop(L, 1);
        return false;
    }
    const std::uint64_t gen = push_dispatch_gen(L);
    bool ok = impl.protected_call("campaign:base_camp", 0, 1);
    if (ok) {
        out = CampaignZone{};
        ok = parse_campaign_zone(L, impl, out);
        lua_pop(L, 1);
    }
    pop_dispatch_gen(L, gen);
    return ok;
}

std::vector<std::string> og_function_names()
{
    WorldScripts& ws = active_world_scripts();
    ScriptHost::Impl& impl = ws.host().impl();
    lua_State* L = impl.L;
    impl.push_sandbox_root();
    lua_getfield(L, -1, "og");
    std::vector<std::string> names;
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        if (lua_isfunction(L, -1) && lua_type(L, -2) == LUA_TSTRING)
            names.emplace_back(lua_tostring(L, -2));
        lua_pop(L, 1);
    }
    lua_pop(L, 2);
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace hooks

}  // namespace og::script
