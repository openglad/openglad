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
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/util.h>

#include "script_internal.h"
#include "script_raise.h"

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
    {"on_kill", FamilyHook::OnKill},
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
    // rule: Pack hooks name their order as living, weapon, treasure, generator, fx or its alias
    //   effect; any other order name is a load error.
    // tests: ScriptHooksTest.order_alias_fx_and_effect_both_resolve
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    for (const auto& oi : kOrders)
        // rule: Pack hooks name their order as living, weapon, treasure, generator, fx or its alias
        //   effect; any other order name is a load error.
        // tests: ScriptHooksTest.order_alias_fx_and_effect_both_resolve
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
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
    // plumbing: registry lookup of the VM state pointer (get_vm_state)
    if (lua_islightuserdata(L, -1))
        st = static_cast<VmState*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return st;
}

// The VmState of a VM that install_vm_scaffolding built. 'og.vmstate' is
// written only there (its two callers: the WorldScripts ctor and the
// declaration VM in family_decl.cpp), and the sandbox has no debug library
// (script_host.cpp), so Lua cannot clear the key: for these VMs the state is
// never null, and callers that only ever see them take it by reference.
VmState& vm_state(lua_State* L)
{
    VmState* st = get_vm_state(L);
    assert(st != nullptr);
    return *st;
}

walker* resolve_walker(lua_State* L, int idx, bool required)
{
    auto* h = static_cast<WalkerHandle*>(
        luaL_checkudata(L, idx, kWalkerMeta));
    walker* w = nullptr;
    // rule: A Lua entity handle names a walker by its entity id for as long as that walker is in
    //   the world; a dying walker's handle (on_death and friends) stays usable only while the
    //   dispatch that produced it is still on the stack, including across nested dispatches it
    //   starts; any other stale handle is the script error 'stale or dead entity handle' (or
    //   nil/false for the non-required readers such as og.is_alive).
    // tests: ScriptBindingTest.world_query_rows_read_the_live_world
    // tests: ScriptBindingPropsTest.stale_handles_raise_the_same_error_text
    // tests: LevelScriptsTest.nested_dispatch_keeps_outer_handles_alive
    // tests: SemScript3.is_alive_answers_false_for_a_stale_handle_without_an_error
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
        // rule: A Lua entity handle names a walker by its entity id for as long as that walker is
        //   in the world.
        // tests: ScriptBindingTest.world_query_rows_read_the_live_world
        // tests: ScriptBindingPropsTest.stale_handles_raise_the_same_error_text
        // tests: LevelScriptsTest.nested_dispatch_keeps_outer_handles_alive
        // tests: SemScript3.is_alive_answers_false_for_a_stale_handle_without_an_error
        if (st != nullptr &&
            std::find(st->live_gens.begin(), st->live_gens.end(), h->gen) !=
                st->live_gens.end())
            w = h->raw;
    }
    if (w == nullptr && required)
        script_raise(L, "stale or dead entity handle");
    return w;
}

walker* resolve_walker_or_nil(lua_State* L, int idx)
{
    // plumbing: resolve_walker_or_nil: nil/none argument answers nullptr (optional handle
    //   parameters)
    if (lua_isnoneornil(L, idx))
        return nullptr;
    return resolve_walker(L, idx, /*required=*/true);
}

guy* resolve_guy(lua_State* L, int idx, bool required)
{
    auto* h = static_cast<GuyHandle*>(luaL_checkudata(L, idx, kGuyMeta));
    guy* g = nullptr;
    VmState* st = get_vm_state(L);
    // rule: A guy (character record) handle is valid only inside the dispatch that handed it out;
    //   using it later is the script error 'stale guy handle (guys are dispatch-scoped)'.
    // tests: ScriptHooksTest.level_up_guy_handle_is_dispatch_scoped
    if (st != nullptr &&
        std::find(st->live_gens.begin(), st->live_gens.end(), h->gen) !=
            st->live_gens.end())
        g = h->raw;
    if (g == nullptr && required)
        script_raise(L, "stale guy handle (guys are dispatch-scoped)");
    return g;
}

void push_walker_handle(lua_State* L, walker* w, std::uint64_t gen)
{
    // plumbing: a null walker reaches Lua as nil (push_walker_handle)
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
    // plumbing: current_dispatch_gen: null VM state answers generation 0
    return st != nullptr ? st->dispatch_gen : 0;
}

std::uint64_t push_dispatch_gen(lua_State* L)
{
    VmState* st = &vm_state(L);
    st->dispatch_gen++;
    st->live_gens.push_back(st->dispatch_gen);
    return st->dispatch_gen;
}

void pop_dispatch_gen(lua_State* L, std::uint64_t gen)
{
    VmState* st = &vm_state(L);
    // Erase this frame's generation wherever it sits: a Lua error unwinding
    // through pcall can skip an inner frame's pop, so do not assume LIFO.
    auto it = std::find(st->live_gens.begin(), st->live_gens.end(), gen);
    // plumbing: pop_dispatch_gen erases the frame's generation wherever it sits (pcall unwinding
    //   can skip an inner pop); the observable rule is above
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
    // plumbing: pack lib registry: re-registering pack_id+name replaces the module and bumps the
    //   generation
    for (PackLibModule& existing : lib_module_storage()) {
        // plumbing: pack lib registry: re-registering pack_id+name replaces the module and bumps
        //   the generation
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
    // plumbing: unregister with nothing removed leaves the generation alone
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
    // plumbing: family tuning store: an empty tuning map erases the entry instead of storing it
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
    // plumbing: family_tuning lookup miss answers nullptr
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
    script_raise(L, "attempt to modify a read-only table");
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
    // rule: A pack library's exports (and og.tuning tables) are read-only to every script: writing
    //   an existing or new key is the error 'attempt to modify a read-only table', # still works; a
    //   scalar or function export is passed through unchanged. The freeze is shallow: nested tables
    //   inside an export stay writable.
    // tests: PackLibTest.exports_are_frozen_shallow_views
    // tests: ScriptBindingPropsTest.tuning_views_reject_writes
    // tests: PackLibTest.non_table_exports_pass_through
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
                                     const std::string& name)
{
    // plumbing: lib registry scans (find_lib_module, pack_ships_lib_modules); their observable
    //   answers are og.use's errors below
    for (const PackLibModule& m : pack_lib_modules()) {
        // plumbing: lib registry scans (find_lib_module, pack_ships_lib_modules); their observable
        //   answers are og.use's errors below
        if (m.pack_id == pack_id && m.name == name)
            return &m;
    }
    return nullptr;
}

// Does any INSTALLED pack answer to this id with a lib/ directory? The
// qualified og.use form needs to tell "that pack ships no such module" from
// "there is no such pack", and a pack with no lib modules is indexed here
// exactly like one that was never installed — so the two answers this can
// give are the two the error messages distinguish.
bool pack_ships_lib_modules(const std::string& pack_id)
{
    // plumbing: lib registry scans (find_lib_module, pack_ships_lib_modules); their observable
    //   answers are og.use's errors below
    for (const PackLibModule& m : pack_lib_modules()) {
        // plumbing: lib registry scans (find_lib_module, pack_ships_lib_modules); their observable
        //   answers are og.use's errors below
        if (m.pack_id == pack_id)
            return true;
    }
    return false;
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
    // plumbing: lib status/export memo table reads and writes (Loading/Failed/None, memoized
    //   export); observable through og.use rules below
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
    // plumbing: lib status/export memo table reads and writes (Loading/Failed/None, memoized
    //   export); observable through og.use rules below
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
    // plumbing: lib status/export memo table reads and writes (Loading/Failed/None, memoized
    //   export); observable through og.use rules below
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
    // rule: A pack library module loads once: a syntax error, a runtime error, or a module that
    //   returns nothing is recorded as a pack error and latched, so every later og.use of it fails
    //   the same way without re-running it; a good module's export is memoized and shared by every
    //   user. Modules run in their own isolated environment. While a module loads, og.use inside it
    //   resolves against the module's own pack, not the caller's.
    // tests: PackLibTest.module_returning_nothing_is_an_error
    // tests: PackLibTest.failures_latch_and_do_not_rerun
    // tests: PackLibTest.modules_load_eagerly_in_order_before_scripts
    // tests: PackLibTest.qualified_use_shares_the_memoized_export
    // tests: PackLibTest.qualified_use_keeps_the_callee_packs_resolution_root
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
        // rule: A pack library module loads once.
        // tests: PackLibTest.module_returning_nothing_is_an_error
        // tests: PackLibTest.failures_latch_and_do_not_rerun
        // tests: PackLibTest.modules_load_eagerly_in_order_before_scripts
        // tests: PackLibTest.qualified_use_shares_the_memoized_export
        // tests: PackLibTest.qualified_use_keeps_the_callee_packs_resolution_root
        if (impl.protected_call(m.chunk_name.c_str(), 0, 1)) {
            // rule: A pack library module loads once.
            // tests: PackLibTest.module_returning_nothing_is_an_error
            // tests: PackLibTest.failures_latch_and_do_not_rerun
            // tests: PackLibTest.modules_load_eagerly_in_order_before_scripts
            // tests: PackLibTest.qualified_use_shares_the_memoized_export
            // tests: PackLibTest.qualified_use_keeps_the_callee_packs_resolution_root
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

// og.use("name") → the frozen export of packs/<current pack>/lib/<name>.lua,
// or og.use("<pack-id>:name") → the same module out of ANOTHER installed
// pack's lib/, for the shared libraries a campaign pack consumes rather than
// copies (docs/lineup-design.md C1: the match rules live in packs/core and
// the modes campaign uses that one implementation).
//
// Both forms are load-time-only: outside pack load there is no pack to
// resolve against (and a dispatch-time og.use would be hidden coupling the
// reader cannot see — bind modules to locals at chunk load). The qualified
// form changes only WHERE the name resolves; the memo key is the same
// "<pack>/<name>" either way, so a module two packs both use compiles once
// and both see the same frozen export, and a cross-pack cycle is caught by
// the same in-flight status the pack-relative form uses.
int og_use(lua_State* L)
{
    const char* spec = luaL_checkstring(L, 1);
    VmState* st = get_vm_state(L);
    // No null check: og.use is installed only by install_vm_scaffolding,
    // which registers 'og.vmstate' and sets host_impl first, so every VM
    // that can call this has both (the declaration VM included).
    // rule: og.use is load-time only and resolves a module in the caller's own pack, or in another
    //   installed pack with 'pack:module'; it reports a circular dependency, a previously failed
    //   module, a malformed qualified name, an unknown pack and a missing module each with its own
    //   error, and otherwise returns the memoized frozen export.
    // tests: PackLibTest.og_use_is_load_time_only
    // tests: PackLibTest.qualified_use_refuses_malformed_names
    // tests: PackLibTest.qualified_use_names_an_unknown_pack
    // tests: PackLibTest.dependency_cycles_are_detected, PackLibTest.resolution_is_pack_relative
    // tests: PackLibTest.failures_latch_and_do_not_rerun
    if (st->current_pack.empty())
        script_raise(L, "og.use: only callable while a pack chunk "
                        "loads (bind modules to locals at load time)");
    // A module name is a file stem, which never contains ':', so the first
    // colon splits the two halves unambiguously and a second one is a typo,
    // not a nested qualification.
    std::string pack = st->current_pack;
    std::string name = spec;
    if (const char* colon = std::strchr(spec, ':'); colon != nullptr) {
        pack.assign(spec, static_cast<std::size_t>(colon - spec));
        name.assign(colon + 1);
        // rule: og.use is load-time only and resolves a module in the caller's own pack, or in
        //   another installed pack with 'pack:module'.
        // tests: PackLibTest.og_use_is_load_time_only
        // tests: PackLibTest.qualified_use_refuses_malformed_names
        // tests: PackLibTest.qualified_use_names_an_unknown_pack
        // tests: PackLibTest.dependency_cycles_are_detected
        // tests: PackLibTest.resolution_is_pack_relative
        // tests: PackLibTest.failures_latch_and_do_not_rerun
        if (pack.empty() || name.empty() ||
            name.find(':') != std::string::npos)
            script_raise(L, "og.use: malformed qualified name '%s' "
                            "(expected \"<pack-id>:<module>\")", spec);
        // rule: og.use is load-time only and resolves a module in the caller's own pack, or in
        //   another installed pack with 'pack:module'.
        // tests: PackLibTest.og_use_is_load_time_only
        // tests: PackLibTest.qualified_use_refuses_malformed_names
        // tests: PackLibTest.qualified_use_names_an_unknown_pack
        // tests: PackLibTest.dependency_cycles_are_detected
        // tests: PackLibTest.resolution_is_pack_relative
        // tests: PackLibTest.failures_latch_and_do_not_rerun
        if (!pack_ships_lib_modules(pack))
            script_raise(L,
                         "og.use: no installed pack '%s' with lib "
                         "modules (expected packs/%s/lib/%s.lua)",
                         pack.c_str(), pack.c_str(), name.c_str());
    }
    const std::string key = pack + "/" + name;
    switch (lib_status(L, st, key)) {
        case LibStatus::Loading:
            script_raise(L, "og.use: circular dependency on module "
                            "'%s'", spec);
        case LibStatus::Failed:
            script_raise(L, "og.use: module '%s' failed to load", spec);
        case LibStatus::None:
            break;
    }
    if (push_lib_export(L, st, key))
        return 1;
    // Not loaded yet: a module (or script) may use a module the eager pass
    // has not reached — load it on demand, in the same deterministic way.
    const PackLibModule* m = find_lib_module(pack, name);
    if (m == nullptr)
        script_raise(L,
                     "og.use: no module '%s' in pack '%s' (expected "
                     "packs/%s/lib/%s.lua)",
                     name.c_str(), pack.c_str(), pack.c_str(),
                     name.c_str());
    if (!load_pack_lib_module(*st->host_impl, st, *m))
        script_raise(L, "og.use: module '%s' failed to load", spec);
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
    // plumbing: coverage recorder guard (coverage_declare_hook is a no-op unless the Lua coverage
    //   recorder is armed)
    if (!coverage::enabled()) return;  // callers guard too; belt and braces
    coverage::declare_function_closure(L, -1, label);
}

namespace {

int walker_eq(lua_State* L)
{
    auto* a = static_cast<WalkerHandle*>(luaL_checkudata(L, 1, kWalkerMeta));
    auto* b = static_cast<WalkerHandle*>(luaL_checkudata(L, 2, kWalkerMeta));
    bool eq;
    // rule: Two Lua handles are equal (==) when they name the same entity id, even if pushed by
    //   different dispatches; untracked entities compare by identity within one dispatch
    //   generation.
    // tests: ScriptBindingTest.world_query_rows_read_the_live_world
    // tests: SemScript3.handles_to_different_tracked_walkers_are_unequal
    // tests: SemScript3.untracked_handles_compare_by_identity_within_a_dispatch
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
    // plumbing: handle metatables created once per VM (luaL_newmetatable returns 0 when present)
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
    // plumbing: error-location string trimming for duplicate-registration reports ('chunk:line' or
    //   the registrar name when no debug info)
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
    // plumbing: null host_impl guard before recording the duplicate-hook error
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
    // rule: A specials-table key names a special by the id the family declares for it; an unknown
    //   key is a load error that lists the ids the family does declare, or says it declares none.
    // tests: SpecialsByIdTest.a_declared_id_keys_its_own_slot
    // tests: SpecialsByIdTest.misspelled_id_is_a_load_error_naming_the_real_ids
    // tests: SpecialsDispatchTest.an_id_key_on_a_family_that_declares_none_says_so
    if (fd == nullptr)
        return -1;
    for (int slot = 0; slot < FD_NUM_SPECIALS; slot++) {
        // rule: A specials-table key names a special by the id the family declares for it.
        // tests: SpecialsByIdTest.a_declared_id_keys_its_own_slot
        // tests: SpecialsByIdTest.misspelled_id_is_a_load_error_naming_the_real_ids
        // tests: SpecialsDispatchTest.an_id_key_on_a_family_that_declares_none_says_so
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
    // rule: A specials-table key names a special by the id the family declares for it.
    // tests: SpecialsByIdTest.a_declared_id_keys_its_own_slot
    // tests: SpecialsByIdTest.misspelled_id_is_a_load_error_naming_the_real_ids
    // tests: SpecialsDispatchTest.an_id_key_on_a_family_that_declares_none_says_so
    for (int slot = 0; fd != nullptr && slot < FD_NUM_SPECIALS; slot++) {
        // rule: A specials-table key names a special by the id the family declares for it.
        // tests: SpecialsByIdTest.a_declared_id_keys_its_own_slot
        // tests: SpecialsByIdTest.misspelled_id_is_a_load_error_naming_the_real_ids
        // tests: SpecialsDispatchTest.an_id_key_on_a_family_that_declares_none_says_so
        if (fd->special_ids[slot] == nullptr)
            continue;
        // rule: A specials-table key names a special by the id the family declares for it.
        // tests: SpecialsByIdTest.a_declared_id_keys_its_own_slot
        // tests: SpecialsByIdTest.misspelled_id_is_a_load_error_naming_the_real_ids
        // tests: SpecialsDispatchTest.an_id_key_on_a_family_that_declares_none_says_so
        if (!list.empty())
            list += ", ";
        list += fd->special_ids[slot];
    }
    // rule: A specials-table key names a special by the id the family declares for it.
    // tests: SpecialsByIdTest.a_declared_id_keys_its_own_slot
    // tests: SpecialsByIdTest.misspelled_id_is_a_load_error_naming_the_real_ids
    // tests: SpecialsDispatchTest.an_id_key_on_a_family_that_declares_none_says_so
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
    // rule: Campaign (menu) scripts may not rewrite sim hook tables: calling any hook registrar
    //   from inside a campaign hook is an error, checked before any argument error.
    // tests: CampaignHooksTest.fence_walk_everything_outside_the_allowlist_errors
    // tests: CampaignHooksTest.zone_dispatch_is_fenced_and_brackets_cleanly
    // tests: CampaignHooksTest.match_knobs_dispatch_is_fenced_and_brackets_cleanly
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    if (const VmState* fence_st = get_vm_state(L);
        fence_st != nullptr && fence_st->campaign_dispatch)
        script_raise(L, "og.register_hooks: hook registration is not "
                        "available during campaign hooks");
    const char* order_str = luaL_checkstring(L, 1);
    const char* family_str = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);

    const OrderInfo* oi = find_order(order_str);
    // rule: A pack's hook table may only use the hook names its order defines (plus 'specials' for
    //   livings): a misspelled hook name or a non-string key is a load error with a did-you-mean
    //   hint, never a silently missing behaviour.
    // tests: ScriptHooksTest.unknown_hook_name_is_a_load_error
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    // tests: ScriptHooksTest.order_alias_fx_and_effect_both_resolve
    // tests: SpecialsDispatchTest.table_selects_by_current_special
    if (oi == nullptr)
        script_raise(L, "og.register_hooks: unknown order '%s'",
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
        // rule: A pack's hook table may only use the hook names its order defines (plus 'specials'
        //   for livings).
        // tests: ScriptHooksTest.unknown_hook_name_is_a_load_error
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: ScriptHooksTest.order_alias_fx_and_effect_both_resolve
        // tests: SpecialsDispatchTest.table_selects_by_current_special
        for (size_t i = 0; i < oi->hook_count; i++)
            names.push_back(oi->hooks[i].name);
        names.push_back("specials");
        lua_pushnil(L);
        // rule: A pack's hook table may only use the hook names its order defines (plus 'specials'
        //   for livings).
        // tests: ScriptHooksTest.unknown_hook_name_is_a_load_error
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: ScriptHooksTest.order_alias_fx_and_effect_both_resolve
        // tests: SpecialsDispatchTest.table_selects_by_current_special
        while (lua_next(L, 3) != 0) {
            lua_pop(L, 1);  // value; the key stays for the next iteration
            // rule: A pack's hook table may only use the hook names its order defines (plus
            //   'specials' for livings).
            // tests: ScriptHooksTest.unknown_hook_name_is_a_load_error
            // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
            // tests: ScriptHooksTest.order_alias_fx_and_effect_both_resolve
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            if (lua_type(L, -1) != LUA_TSTRING) {
                script_raise(
                    L, "og.register_hooks: a hook table's keys are hook "
                  "names (got a %s key for %s '%s')",
                    luaL_typename(L, -1), order_str, family_str);
            }
            const std::string key = lua_tostring(L, -1);
            bool known = false;
            // rule: A pack's hook table may only use the hook names its order defines (plus
            //   'specials' for livings).
            // tests: ScriptHooksTest.unknown_hook_name_is_a_load_error
            // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
            // tests: ScriptHooksTest.order_alias_fx_and_effect_both_resolve
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            for (const char* n : names)
                known = known || key == n;
            // rule: A pack's hook table may only use the hook names its order defines (plus
            //   'specials' for livings).
            // tests: ScriptHooksTest.unknown_hook_name_is_a_load_error
            // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
            // tests: ScriptHooksTest.order_alias_fx_and_effect_both_resolve
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            if (!known) {
                const std::string hint = did_you_mean(key, names);
                script_raise(
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
    // rule: In the declaration pass og.register_hooks and og.register_level_hooks validate the call
    //   and return without resolving the family or installing anything, so a pack may register
    //   hooks for a family it declares in the same pass (no 'unknown family' error); nothing
    //   dispatches until the bind replay installs it.
    // tests: CampaignHooksTest.the_declaration_pass_bows_out_of_behaviour_registrars
    // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
    // tests: SemScript3.a_family_chunk_registers_hooks_for_its_own_family_in_the_bind_replay
    if (declare_st != nullptr && declare_st->mode == VmMode::Declare)
        return 0;

    const int family_id =
        og::families::resolve_family_string_id(oi->order, family_str);
    // rule: Hooks for a family that no loaded pack defines are a load error ('unknown <order>
    //   family').
    // tests: ScriptHooksTest.unknown_family_is_a_load_error
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    if (family_id < 0)
        script_raise(L, "og.register_hooks: unknown %s family '%s'",
                     order_str, family_str);

    VmState* st = get_vm_state(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, st->hooks_ref);  // hooks root
    lua_rawgeti(L, -1, hook_table_key(oi->order, family_id));
    // plumbing: create the per-family hook table on first registration
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
    // rule: Each named hook must be a function; when a later chunk registers a hook another chunk
    //   already registered, the later one wins and the collision is reported (trace, host error,
    //   warning) unless it is overriding an og.family declaration, which is the intended use.
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    // tests: SpecialsDispatchTest.cross_chunk_collision_reports_and_last_wins
    // tests: LuaFamilyBindTest.a_second_override_of_one_hook_is_still_reported
    // tests: LuaFamilyBindTest.register_hooks_overrides_a_declaration_quietly
    for (size_t i = 0; i < oi->hook_count; i++) {
        lua_getfield(L, 3, oi->hooks[i].name);
        // rule: Each named hook must be a function.
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: SpecialsDispatchTest.cross_chunk_collision_reports_and_last_wins
        // tests: LuaFamilyBindTest.a_second_override_of_one_hook_is_still_reported
        // tests: LuaFamilyBindTest.register_hooks_overrides_a_declaration_quietly
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        // rule: Each named hook must be a function.
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: SpecialsDispatchTest.cross_chunk_collision_reports_and_last_wins
        // tests: LuaFamilyBindTest.a_second_override_of_one_hook_is_still_reported
        // tests: LuaFamilyBindTest.register_hooks_overrides_a_declaration_quietly
        if (!lua_isfunction(L, -1))
            script_raise(L, "og.register_hooks: '%s' must be a function",
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
        // rule: Each named hook must be a function.
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: SpecialsDispatchTest.cross_chunk_collision_reports_and_last_wins
        // tests: LuaFamilyBindTest.a_second_override_of_one_hook_is_still_reported
        // tests: LuaFamilyBindTest.register_hooks_overrides_a_declaration_quietly
        if (occupied && !overriding_declaration)
            report_duplicate_hook(L, st, order_str, family_str,
                                  oi->hooks[i].name);
        // rule: Each named hook must be a function.
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: SpecialsDispatchTest.cross_chunk_collision_reports_and_last_wins
        // tests: LuaFamilyBindTest.a_second_override_of_one_hook_is_still_reported
        // tests: LuaFamilyBindTest.register_hooks_overrides_a_declaration_quietly
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
    // rule: The specials table (one handler per declared special, plus default) is a living-only
    //   form of do_special: on any other order, as a non-table, or alongside a plain do_special in
    //   the same call it is a load error.
    // tests: SpecialsDispatchTest.specials_on_a_non_living_order_is_a_load_error
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    // tests: SpecialsDispatchTest.both_forms_in_one_call_is_a_load_error
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
    } else {
        const int spec_idx = lua_gettop(L);
        const int family_idx = spec_idx - 1;  // family table pushed above
        // rule: The specials table (one handler per declared special, plus default) is a
        //   living-only form of do_special.
        // tests: SpecialsDispatchTest.specials_on_a_non_living_order_is_a_load_error
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: SpecialsDispatchTest.both_forms_in_one_call_is_a_load_error
        if (oi->order != Order::Living)
            script_raise(
                L, "og.register_hooks: 'specials' is a living-order key "
              "(order '%s' has no do_special hook)", order_str);
        // rule: The specials table (one handler per declared special, plus default) is a
        //   living-only form of do_special.
        // tests: SpecialsDispatchTest.specials_on_a_non_living_order_is_a_load_error
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: SpecialsDispatchTest.both_forms_in_one_call_is_a_load_error
        if (!lua_istable(L, spec_idx))
            script_raise(
                L, "og.register_hooks: 'specials' must be a table "
              "({ <special_id>=fn, ..., default=fn })");
        lua_getfield(L, 3, "do_special");
        const bool has_plain_do_special = !lua_isnil(L, -1);
        lua_pop(L, 1);
        // rule: The specials table (one handler per declared special, plus default) is a
        //   living-only form of do_special.
        // tests: SpecialsDispatchTest.specials_on_a_non_living_order_is_a_load_error
        // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
        // tests: SpecialsDispatchTest.both_forms_in_one_call_is_a_load_error
        if (has_plain_do_special)
            script_raise(
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
        // rule: Specials entries are keyed by declared special id or 'default' only (a slot number
        //   is refused so reordering a family's specials cannot silently re-point a handler), every
        //   entry is a function, and an empty table is a load error.
        // tests: SpecialsDispatchTest.a_slot_number_key_is_a_load_error
        // tests: SpecialsDispatchTest.bad_specials_key_is_a_load_error
        // tests: SpecialsDispatchTest.non_function_specials_entry_is_a_load_error
        // tests: SpecialsDispatchTest.empty_specials_table_is_a_load_error
        // tests: SpecialsByIdTest.a_slot_number_beside_its_id_is_still_a_load_error
        // tests: SemScript3.a_specials_table_with_only_a_default_registers
        while (lua_next(L, spec_idx) != 0) {
            // rule: Specials entries are keyed by declared special id or 'default' only (a slot
            //   number is refused so reordering a family's specials cannot silently re-point a
            //   handler), every entry is a function, and an empty table is a load error.
            // tests: SpecialsDispatchTest.a_slot_number_key_is_a_load_error
            // tests: SpecialsDispatchTest.bad_specials_key_is_a_load_error
            // tests: SpecialsDispatchTest.non_function_specials_entry_is_a_load_error
            // tests: SpecialsDispatchTest.empty_specials_table_is_a_load_error
            // tests: SpecialsByIdTest.a_slot_number_beside_its_id_is_still_a_load_error
            // tests: SemScript3.a_specials_table_with_only_a_default_registers
            if (lua_isinteger(L, -2)) {
                script_raise(
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
                // rule: Specials entries are keyed by declared special id or 'default' only (a slot
                //   number is refused so reordering a family's specials cannot silently re-point a
                //   handler), every entry is a function, and an empty table is a load error.
                // tests: SpecialsDispatchTest.a_slot_number_key_is_a_load_error
                // tests: SpecialsDispatchTest.bad_specials_key_is_a_load_error
                // tests: SpecialsDispatchTest.non_function_specials_entry_is_a_load_error
                // tests: SpecialsDispatchTest.empty_specials_table_is_a_load_error
                // tests: SpecialsByIdTest.a_slot_number_beside_its_id_is_still_a_load_error
                // tests: SemScript3.a_specials_table_with_only_a_default_registers
                if (key == "default") {
                    has_default = true;
                } else {
                    const int slot = special_slot_for_id(fd, key.c_str());
                    // rule: Specials entries are keyed by declared special id or 'default' only (a
                    //   slot number is refused so reordering a family's specials cannot silently
                    //   re-point a handler), every entry is a function, and an empty table is a
                    //   load error.
                    // tests: SpecialsDispatchTest.a_slot_number_key_is_a_load_error
                    // tests: SpecialsDispatchTest.bad_specials_key_is_a_load_error
                    // tests: SpecialsDispatchTest.non_function_specials_entry_is_a_load_error
                    // tests: SpecialsDispatchTest.empty_specials_table_is_a_load_error
                    // tests: SpecialsByIdTest.a_slot_number_beside_its_id_is_still_a_load_error
                    // tests: SemScript3.a_specials_table_with_only_a_default_registers
                    if (slot < 0)
                        script_raise(
                            L,
                            "og.register_hooks: 'specials' key '%s' names no "
                            "special of living family '%s' (declared ids: "
                            "%s)",
                            key.c_str(), family_str,
                            declared_special_ids(fd).c_str());
                    keys.push_back({slot, key});
                }
            } else {
                script_raise(
                    L, "og.register_hooks: 'specials' keys must be a "
                  "declared special id or 'default' (got a %s key)",
                    luaL_typename(L, -2));
            }
            // rule: Specials entries are keyed by declared special id or 'default' only (a slot
            //   number is refused so reordering a family's specials cannot silently re-point a
            //   handler), every entry is a function, and an empty table is a load error.
            // tests: SpecialsDispatchTest.a_slot_number_key_is_a_load_error
            // tests: SpecialsDispatchTest.bad_specials_key_is_a_load_error
            // tests: SpecialsDispatchTest.non_function_specials_entry_is_a_load_error
            // tests: SpecialsDispatchTest.empty_specials_table_is_a_load_error
            // tests: SpecialsByIdTest.a_slot_number_beside_its_id_is_still_a_load_error
            // tests: SemScript3.a_specials_table_with_only_a_default_registers
            if (!lua_isfunction(L, -1))
                script_raise(
                    L, "og.register_hooks: 'specials' entries must be "
                  "functions");
            lua_pop(L, 1);
            entry_count++;
        }
        if (entry_count == 0)
            script_raise(
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
        // rule: The registered specials are a private copy (mutating the caller's table later
        //   changes nothing); the current special's handler runs, a slot with no handler falls to
        //   'default', with neither the cast is a successful no-op; a specials table over an
        //   already registered do_special reports the collision and wins.
        // tests: SpecialsDispatchTest.table_selects_by_current_special
        // tests: SpecialsDispatchTest.missing_index_falls_to_default
        // tests: SpecialsDispatchTest.unmatched_index_without_default_is_a_noop_true
        // tests: SpecialsDispatchTest.registered_table_is_immune_to_later_mutation
        // tests: SpecialsDispatchTest.a_specials_table_over_a_taken_slot_reports_and_wins
        lua_createtable(L, static_cast<int>(keys.size()),
                        has_default ? 1 : 0);
        for (const SpecialsKey& k : keys) {
            lua_getfield(L, spec_idx, k.id.c_str());
            // rule: The registered specials are a private copy (mutating the caller's table later
            //   changes nothing).
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            // tests: SpecialsDispatchTest.missing_index_falls_to_default
            // tests: SpecialsDispatchTest.unmatched_index_without_default_is_a_noop_true
            // tests: SpecialsDispatchTest.registered_table_is_immune_to_later_mutation
            // tests: SpecialsDispatchTest.a_specials_table_over_a_taken_slot_reports_and_wins
            if (coverage::enabled())
                coverage_declare_hook(
                    L, std::string(order_str) + "/" + family_str +
                           "/do_special[" + std::to_string(k.slot) + "]");
            lua_rawseti(L, -2, k.slot);
        }
        if (has_default) {
            lua_getfield(L, spec_idx, "default");
            // rule: The registered specials are a private copy (mutating the caller's table later
            //   changes nothing).
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            // tests: SpecialsDispatchTest.missing_index_falls_to_default
            // tests: SpecialsDispatchTest.unmatched_index_without_default_is_a_noop_true
            // tests: SpecialsDispatchTest.registered_table_is_immune_to_later_mutation
            // tests: SpecialsDispatchTest.a_specials_table_over_a_taken_slot_reports_and_wins
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
    // rule: A registration call that installs no hook at all ({} or only unknown-free nils) is a
    //   load error ('no valid hooks ... (check names)').
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
    // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
    if (registered == 0)
        script_raise(
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
    // rule: og.rand(n) draws from the world's deterministic sim RNG in [0, n) for 1 <= n <=
    //   2147483647; a non-positive bound, an over-wide bound (which would wrap and desync) or no
    //   active world is a script error, never a clamp.
    // tests: ScriptBindingMathTest.rand_refuses_a_non_positive_bound
    // tests: ScriptBindingMathTest.rand_bounds_above_int32_max_are_refused
    // tests: ScriptBindingMathTest.rand_bounds_up_to_int32_max_draw_unchanged
    // tests: ScriptBindingMathTest.rand_requires_an_active_world
    if (n <= 0)
        script_raise(L, "og.rand: n must be positive");
    if (n > kMaxRandBound)
        script_raise(L, "og.rand: n out of range [1, 2147483647]");
    if (current_game == nullptr || current_game->world == nullptr)
        script_raise(L, "og.rand: no active world");
    lua_pushinteger(L, static_cast<lua_Integer>(
                           current_game->world->rng_.next(
                               static_cast<std::uint32_t>(n))));
    return 1;
}

// og.is_alive(handle) → entity still resolvable (and not flagged dead).
int og_is_alive(lua_State* L)
{
    walker* w = resolve_walker(L, 1, /*required=*/false);
    // rule: og.is_alive(handle) is true only for a resolvable entity not flagged dead; a stale
    //   handle or a dead walker answers false without an error.
    // tests: ScriptBindingTest.world_query_rows_read_the_live_world
    // tests: SemScript3.is_alive_answers_false_for_a_dead_walker_it_can_still_resolve
    // tests: SemScript3.is_alive_answers_false_for_a_stale_handle_without_an_error
    lua_pushboolean(L, (w != nullptr && w->dead() == 0) ? 1 : 0);
    return 1;
}

// og.entity_id(handle) → stable sim entity id (0 for untracked).
// rule: og.entity_id(handle) answers the sim entity id of the walker, 0 for an untracked
//   (loose/probe) walker.
// tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
// tests: LevelScriptsTest.generator_customize_spawn_dispatches
// tests: QuietProbe.the_lua_doors_answer_what_the_engine_answers
// tests: SemScript3.entity_id_is_zero_for_an_untracked_walker
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
    // rule: A level hook registered for level -1 runs on every level; one registered for level N
    //   runs only on level N, and a wildcard registration is not a level-N registration.
    // tests: LevelScriptsTest.wildcard_level_hooks_fire_for_any_level
    // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
    // tests: LevelScriptsTest.other_level_hooks_do_not_fire
    // tests: LevelScriptsTest.hooks_for_another_level_do_not_fire
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
    {"on_lineup_stage", LevelHook::LineupStage},
};

// og.register_level_hooks(level_id, { on_load=, on_tick=, on_entity_death=,
// on_entity_spawn= }). level_id -1 registers for every level.
int og_register_level_hooks(lua_State* L)
{
    // The same campaign fence as og.register_hooks (menu scripts may not
    // rewrite sim hook tables), before any argument check.
    // rule: Campaign (menu) scripts may not rewrite sim hook tables: calling any hook registrar
    //   from inside a campaign hook is an error, checked before any argument error.
    // tests: CampaignHooksTest.fence_walk_everything_outside_the_allowlist_errors
    // tests: CampaignHooksTest.zone_dispatch_is_fenced_and_brackets_cleanly
    // tests: CampaignHooksTest.match_knobs_dispatch_is_fenced_and_brackets_cleanly
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    if (const VmState* fence_st = get_vm_state(L);
        fence_st != nullptr && fence_st->campaign_dispatch)
        script_raise(L, "og.register_level_hooks: hook registration is "
                        "not available during campaign hooks");
    const int level_id = static_cast<int>(luaL_checkinteger(L, 1));
    luaL_checktype(L, 2, LUA_TTABLE);
    // Declaration pass: silent no-op, the og.register_hooks precedent — a
    // family chunk calling this neither rejects the pack nor registers; the
    // bind replay is where it lands.
    VmState* st = get_vm_state(L);
    // rule: In the declaration pass og.register_hooks and og.register_level_hooks validate the call
    //   and return without resolving the family or installing anything, so a pack may register
    //   hooks for a family it declares in the same pass (no 'unknown family' error).
    // tests: CampaignHooksTest.the_declaration_pass_bows_out_of_behaviour_registrars
    // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
    // tests: SemScript3.a_family_chunk_registers_hooks_for_its_own_family_in_the_bind_replay
    if (st != nullptr && st->mode == VmMode::Declare)
        return 0;

    lua_rawgeti(L, LUA_REGISTRYINDEX, st->level_hooks_ref);
    int registered = 0;
    // rule: Each level hook (on_load, on_tick, on_entity_death, on_entity_spawn, on_mode_init,
    //   on_mode_tick, on_damage, on_respawn, on_lineup_stage) must be a function; others in the
    //   table are not inspected here.
    // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
    // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
    for (const auto& h : kLevelHookNames) {
        lua_getfield(L, 2, h.name);
        // rule: Each level hook (on_load, on_tick, on_entity_death, on_entity_spawn, on_mode_init,
        //   on_mode_tick, on_damage, on_respawn, on_lineup_stage) must be a function.
        // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
        // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            continue;
        }
        // rule: Each level hook (on_load, on_tick, on_entity_death, on_entity_spawn, on_mode_init,
        //   on_mode_tick, on_damage, on_respawn, on_lineup_stage) must be a function.
        // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
        // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
        if (!lua_isfunction(L, -1))
            script_raise(L, "og.register_level_hooks: '%s' must be a "
                            "function", h.name);
        // rule: Each level hook (on_load, on_tick, on_entity_death, on_entity_spawn, on_mode_init,
        //   on_mode_tick, on_damage, on_respawn, on_lineup_stage) must be a function.
        // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
        // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
        if (coverage::enabled())
            coverage_declare_hook(L, "level/" + std::to_string(level_id) +
                                         "/" + h.name);
        lua_rawseti(L, -2, level_hook_key(h.kind, level_id));
        st->level_hook_kinds |= 1u << static_cast<unsigned>(h.kind);
        registered++;
    }
    lua_pop(L, 1);
    // rule: A registration call that installs no hook at all ({} or only unknown-free nils) is a
    //   load error ('no valid hooks ...
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
    // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
    if (registered == 0)
        script_raise(
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
    // rule: A level script may give one tracked entity its own on_death override (cleared when it
    //   fires); an untracked entity or a non-function on_death is a script error; an empty table
    //   still lets the level hook fire.
    // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.an_empty_entity_hook_table_still_lets_the_level_hook_fire
    if (h->entity_id == 0)
        script_raise(L, "og.set_entity_hooks: entity is untracked");
    VmState* st = &vm_state(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, st->entity_hooks_ref);
    lua_newtable(L);
    lua_getfield(L, 2, "on_death");
    // rule: A level script may give one tracked entity its own on_death override (cleared when it
    //   fires).
    // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.an_empty_entity_hook_table_still_lets_the_level_hook_fire
    if (!lua_isnil(L, -1) && !lua_isfunction(L, -1))
        script_raise(L, "og.set_entity_hooks: 'on_death' must be a "
                        "function");
    // rule: A level script may give one tracked entity its own on_death override (cleared when it
    //   fires).
    // tests: LevelScriptsTest.level_and_entity_hook_registration_rejections
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.an_empty_entity_hook_table_still_lets_the_level_hook_fire
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
                                              "base_camp", "match_knobs",
                                              "lineup"};

// Slots of the campaign_hooks_ref registry table.
constexpr lua_Integer kCampaignMenuSlot = 1;
constexpr lua_Integer kCampaignActionSlot = 2;
constexpr lua_Integer kCampaignZoneSlot = 3;
// `lineup` is a TABLE, not a function: only its `power` member lives in
// the registry table. The preset NAMES are plain data and are copied out
// to the VmState at registration, so the menus can read them without
// entering Lua at all (docs/lineup-design.md §3.3).
constexpr lua_Integer kCampaignLineupPowerSlot = 4;
// The match_knobs answer: which knobs the SETUP wizard shows for this game.
constexpr lua_Integer kCampaignMatchKnobsSlot = 5;

// "chunkname:line" of the caller, for the duplicate-registration record.
std::string campaign_caller_source(lua_State* L)
{
    luaL_where(L, 1);
    std::string where = lua_tostring(L, -1);
    lua_pop(L, 1);
    // plumbing: error-location string trimming for duplicate-registration reports ('chunk:line' or
    //   the registrar name when no debug info)
    while (!where.empty() && (where.back() == ' ' || where.back() == ':'))
        where.pop_back();
    if (where.empty()) where = "og.register_campaign_hooks";  // no debug info
    return where;
}

// og.register_campaign_hooks({ vars = {...}, picker_menu = fn,
// picker_action = fn, base_camp = fn, match_knobs = fn,
// lineup = { power = fn } }) — load-time only, one campaign
// book per VM (docs/campaign-scripting-design.md;
// docs/basecamp-zones-design.md for the base_camp zone hook).
int og_register_campaign_hooks(lua_State* L)
{
    VmState* st = get_vm_state(L);
    // Campaign and plan fences first (before any argument check): neither a
    // campaign hook nor a match plan may re-register hooks.
    // rule: Campaign (menu) scripts may not rewrite sim hook tables: calling any hook registrar
    //   from inside a campaign hook is an error, checked before any argument error.
    // tests: CampaignHooksTest.fence_walk_everything_outside_the_allowlist_errors
    // tests: CampaignHooksTest.zone_dispatch_is_fenced_and_brackets_cleanly
    // tests: CampaignHooksTest.match_knobs_dispatch_is_fenced_and_brackets_cleanly
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    if (st != nullptr && st->campaign_dispatch)
        script_raise(L, "og.register_campaign_hooks: hook registration "
                        "is not available during campaign hooks");
    luaL_checktype(L, 1, LUA_TTABLE);
    // The reads below use absolute stack indices 2/3/4 — drop any extra
    // arguments so a stray second argument cannot shadow them.
    lua_settop(L, 1);

    // Every validation below is a property of the call alone, so the
    // declaration pass runs it too — the pass that can still reject the pack
    // is the one that should catch the typo (the og.register_hooks
    // discipline).
    {
        std::vector<const char*> names(std::begin(kCampaignHookNames),
                                       std::end(kCampaignHookNames));
        names.push_back("vars");
        lua_pushnil(L);
        // rule: A campaign book's registration may only use its known keys; a typo or non-string
        //   key is a load error with a did-you-mean hint, even in the declaration pass.
        // tests: CampaignHooksTest.unknown_key_is_a_load_error_with_did_you_mean
        // tests: CampaignHooksTest.base_camp_key_typo_gets_did_you_mean
        // tests: CampaignHooksTest.declare_mode_still_rejects_a_bad_registration
        // tests: CampaignHooksTest.the_registrar_refuses_mis_shaped_books_by_name
        while (lua_next(L, 1) != 0) {
            lua_pop(L, 1);  // value; the key stays for the next iteration
            // rule: A campaign book's registration may only use its known keys; a typo or
            //   non-string key is a load error with a did-you-mean hint, even in the declaration
            //   pass.
            // tests: CampaignHooksTest.unknown_key_is_a_load_error_with_did_you_mean
            // tests: CampaignHooksTest.base_camp_key_typo_gets_did_you_mean
            // tests: CampaignHooksTest.declare_mode_still_rejects_a_bad_registration
            // tests: CampaignHooksTest.the_registrar_refuses_mis_shaped_books_by_name
            if (lua_type(L, -1) != LUA_TSTRING)
                script_raise(
                    L, "og.register_campaign_hooks: keys are 'vars', "
                  "'picker_menu', 'picker_action', 'base_camp', "
                  "'match_knobs' and 'lineup' (got a %s key)",
                    luaL_typename(L, -1));
            const std::string key = lua_tostring(L, -1);
            bool known = false;
            // rule: A campaign book's registration may only use its known keys; a typo or
            //   non-string key is a load error with a did-you-mean hint, even in the declaration
            //   pass.
            // tests: CampaignHooksTest.unknown_key_is_a_load_error_with_did_you_mean
            // tests: CampaignHooksTest.base_camp_key_typo_gets_did_you_mean
            // tests: CampaignHooksTest.declare_mode_still_rejects_a_bad_registration
            // tests: CampaignHooksTest.the_registrar_refuses_mis_shaped_books_by_name
            for (const char* n : names)
                known = known || key == n;
            // rule: A campaign book's registration may only use its known keys; a typo or
            //   non-string key is a load error with a did-you-mean hint, even in the declaration
            //   pass.
            // tests: CampaignHooksTest.unknown_key_is_a_load_error_with_did_you_mean
            // tests: CampaignHooksTest.base_camp_key_typo_gets_did_you_mean
            // tests: CampaignHooksTest.declare_mode_still_rejects_a_bad_registration
            // tests: CampaignHooksTest.the_registrar_refuses_mis_shaped_books_by_name
            if (!known) {
                const std::string hint = did_you_mean(key, names);
                script_raise(
                    L, "og.register_campaign_hooks: unknown key '%s'%s",
                    key.c_str(), hint.c_str());
            }
        }
    }
    lua_getfield(L, 1, "picker_menu");    // abs index 2
    const bool has_menu = !lua_isnil(L, 2);
    // rule: Each campaign hook given must be a function (lineup a table); a book with none of
    //   picker_menu/picker_action/base_camp/match_knobs/lineup is a load error; base_camp alone is
    //   a legal book.
    // tests: CampaignHooksTest.non_function_hook_is_a_load_error
    // tests: CampaignHooksTest.non_function_base_camp_is_a_load_error
    // tests: CampaignHooksTest.neither_hook_function_is_a_load_error
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    if (has_menu && !lua_isfunction(L, 2))
        script_raise(
            L, "og.register_campaign_hooks: 'picker_menu' must be a function");
    lua_getfield(L, 1, "picker_action");  // abs index 3
    const bool has_action = !lua_isnil(L, 3);
    if (has_action && !lua_isfunction(L, 3))
        script_raise(L, "og.register_campaign_hooks: 'picker_action' "
                        "must be a function");
    lua_getfield(L, 1, "base_camp");      // abs index 4
    const bool has_zone = !lua_isnil(L, 4);
    if (has_zone && !lua_isfunction(L, 4))
        script_raise(L, "og.register_campaign_hooks: 'base_camp' "
                        "must be a function");
    lua_getfield(L, 1, "match_knobs");    // abs index 5
    const bool has_knobs = !lua_isnil(L, 5);
    if (has_knobs && !lua_isfunction(L, 5))
        script_raise(L, "og.register_campaign_hooks: 'match_knobs' "
                        "must be a function");
    lua_getfield(L, 1, "lineup");         // abs index 6
    const bool has_lineup = !lua_isnil(L, 6);
    if (has_lineup && !lua_istable(L, 6))
        script_raise(L, "og.register_campaign_hooks: 'lineup' must "
                        "be a table of { power }");
    bool has_lineup_power = false;
    // rule: The lineup hook is a table whose only key is power (a function); the retired
    //   default_fill key and any typo are refused; a lineup table without power is a load error.
    // tests: CampaignHooksTest.lineup_registration_rejections
    // tests: CampaignHooksTest.lineup_presets_key_is_gone
    // tests: CampaignHooksTest.lineup_registers_and_prices_a_fighter
    if (has_lineup) {
        // Same "the typo is caught by the pass that can still reject the
        // pack" discipline as the outer key walk. `default_fill` was the
        // second key until amendment 4 (E2) retired the per-team default
        // resolver; a book that still names it is refused as unknown,
        // exactly like any other misspelling, rather than silently
        // registering a hook nothing calls.
        static const char* const kLineupKeys[] = {"power"};
        lua_pushnil(L);
        // rule: The lineup hook is a table whose only key is power (a function); the retired
        //   default_fill key and any typo are refused; a lineup table without power is a load
        //   error.
        // tests: CampaignHooksTest.lineup_registration_rejections
        // tests: CampaignHooksTest.lineup_presets_key_is_gone
        // tests: CampaignHooksTest.lineup_registers_and_prices_a_fighter
        while (lua_next(L, 6) != 0) {
            lua_pop(L, 1);  // value; the key stays for the next iteration
            // rule: The lineup hook is a table whose only key is power (a function); the retired
            //   default_fill key and any typo are refused; a lineup table without power is a load
            //   error.
            // tests: CampaignHooksTest.lineup_registration_rejections
            // tests: CampaignHooksTest.lineup_presets_key_is_gone
            // tests: CampaignHooksTest.lineup_registers_and_prices_a_fighter
            if (lua_type(L, -1) != LUA_TSTRING)
                script_raise(
                    L, "og.register_campaign_hooks: the only 'lineup' key "
                  "is 'power' (got a %s key)",
                    luaL_typename(L, -1));
            const std::string key = lua_tostring(L, -1);
            bool known = false;
            // rule: The lineup hook is a table whose only key is power (a function); the retired
            //   default_fill key and any typo are refused; a lineup table without power is a load
            //   error.
            // tests: CampaignHooksTest.lineup_registration_rejections
            // tests: CampaignHooksTest.lineup_presets_key_is_gone
            // tests: CampaignHooksTest.lineup_registers_and_prices_a_fighter
            for (const char* n : kLineupKeys)
                known = known || key == n;
            // rule: The lineup hook is a table whose only key is power (a function); the retired
            //   default_fill key and any typo are refused; a lineup table without power is a load
            //   error.
            // tests: CampaignHooksTest.lineup_registration_rejections
            // tests: CampaignHooksTest.lineup_presets_key_is_gone
            // tests: CampaignHooksTest.lineup_registers_and_prices_a_fighter
            if (!known) {
                const std::vector<const char*> names(std::begin(kLineupKeys),
                                                     std::end(kLineupKeys));
                const std::string hint = did_you_mean(key, names);
                script_raise(
                    L, "og.register_campaign_hooks: unknown 'lineup' key "
                  "'%s'%s",
                    key.c_str(), hint.c_str());
            }
        }
        lua_getfield(L, 6, "power");
        has_lineup_power = !lua_isnil(L, -1);
        // rule: The lineup hook is a table whose only key is power (a function); the retired
        //   default_fill key and any typo are refused; a lineup table without power is a load
        //   error.
        // tests: CampaignHooksTest.lineup_registration_rejections
        // tests: CampaignHooksTest.lineup_presets_key_is_gone
        // tests: CampaignHooksTest.lineup_registers_and_prices_a_fighter
        if (has_lineup_power && !lua_isfunction(L, -1))
            script_raise(L, "og.register_campaign_hooks: "
                            "'lineup.power' must be a function");
        lua_pop(L, 1);
        // rule: The lineup hook is a table whose only key is power (a function); the retired
        //   default_fill key and any typo are refused; a lineup table without power is a load
        //   error.
        // tests: CampaignHooksTest.lineup_registration_rejections
        // tests: CampaignHooksTest.lineup_presets_key_is_gone
        // tests: CampaignHooksTest.lineup_registers_and_prices_a_fighter
        if (!has_lineup_power)
            script_raise(L, "og.register_campaign_hooks: 'lineup' "
                            "carries no 'power'");
    }
    // rule: Each campaign hook given must be a function (lineup a table).
    // tests: CampaignHooksTest.non_function_hook_is_a_load_error
    // tests: CampaignHooksTest.non_function_base_camp_is_a_load_error
    // tests: CampaignHooksTest.neither_hook_function_is_a_load_error
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    if (!has_menu && !has_action && !has_zone && !has_knobs && !has_lineup)
        script_raise(L, "og.register_campaign_hooks: register at least "
                        "one of 'picker_menu' / 'picker_action' / "
                        "'base_camp' / 'match_knobs' / 'lineup'");

    std::vector<std::string> vars;
    lua_getfield(L, 1, "vars");
    // rule: A campaign book declares its persistent campaign variables as an array of at most 64
    //   lowercase names; a non-table, too many names, a non-string or badly spelled name is a load
    //   error.
    // tests: CampaignHooksTest.bad_vars_are_load_errors
    // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
    if (!lua_isnil(L, -1)) {
        // rule: A campaign book declares its persistent campaign variables as an array of at most
        //   64 lowercase names.
        // tests: CampaignHooksTest.bad_vars_are_load_errors
        // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
        if (!lua_istable(L, -1))
            script_raise(L, "og.register_campaign_hooks: 'vars' must "
                            "be an array of names");
        const lua_Integer count =
            static_cast<lua_Integer>(lua_rawlen(L, -1));
        // rule: A campaign book declares its persistent campaign variables as an array of at most
        //   64 lowercase names.
        // tests: CampaignHooksTest.bad_vars_are_load_errors
        // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
        if (count > hooks::kCampaignVarsMax)
            script_raise(
                L, "og.register_campaign_hooks: 'vars' names %d variables "
              "(max %d)",
                static_cast<int>(count), hooks::kCampaignVarsMax);
        // rule: A campaign book declares its persistent campaign variables as an array of at most
        //   64 lowercase names.
        // tests: CampaignHooksTest.bad_vars_are_load_errors
        // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
        for (lua_Integer i = 1; i <= count; i++) {
            lua_rawgeti(L, -1, i);
            // rule: A campaign book declares its persistent campaign variables as an array of at
            //   most 64 lowercase names.
            // tests: CampaignHooksTest.bad_vars_are_load_errors
            // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
            if (lua_type(L, -1) != LUA_TSTRING)
                script_raise(
                    L, "og.register_campaign_hooks: vars[%d] must be a "
                  "string",
                    static_cast<int>(i));
            std::size_t len = 0;
            const char* name = lua_tolstring(L, -1, &len);
            // rule: A campaign book's declared variable names must be 1-32 chars of [a-z0-9_]; a
            //   bad name rejects the whole registration at load time.
            // tests: CampaignHooksTest.bad_vars_are_load_errors
            // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
            if (!hooks::valid_campaign_var_name({name, len}))
                script_raise(
                    L, "og.register_campaign_hooks: vars[%d] '%s' must be "
                  "1-%d chars of [a-z0-9_]",
                    static_cast<int>(i), name, hooks::kCampaignVarNameMax);
            vars.emplace_back(name, len);
            lua_pop(L, 1);
        }
        // A hash-keyed table (vars = {watch_paid = 1}) has rawlen 0 and
        // would silently register nothing — reject any non-array key.
        lua_pushnil(L);
        // rule: The 'vars' list must be a pure array; any hash key (vars={watch_paid=1}) or
        //   non-integer key is a load error instead of silently registering nothing.
        // tests: CampaignHooksTest.bad_vars_are_load_errors
        // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
        while (lua_next(L, -2) != 0) {
            lua_pop(L, 1);  // value
            lua_Integer index = 0;
            // rule: The 'vars' list must be a pure array; any hash key (vars={watch_paid=1}) or
            //   non-integer key is a load error instead of silently registering nothing.
            // tests: CampaignHooksTest.bad_vars_are_load_errors
            // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
            if (lua_type(L, -1) == LUA_TNUMBER && lua_isinteger(L, -1))
                index = lua_tointeger(L, -1);
            // rule: The 'vars' list must be a pure array; any hash key (vars={watch_paid=1}) or
            //   non-integer key is a load error instead of silently registering nothing.
            // tests: CampaignHooksTest.bad_vars_are_load_errors
            // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
            if (index < 1 || index > count) {
                script_raise(
                    L, "og.register_campaign_hooks: 'vars' must be an "
                  "ARRAY of names (found a non-array key)");
            }
        }
    }
    lua_pop(L, 1);  // vars

    // In the family declaration pass the book lands in the throwaway
    // declaration VM's own state and goes with it: a family chunk calling
    // this neither rejects the pack nor registers anything that outlives
    // the pass.
    // rule: One campaign, one book: a second registration never raises, poisons the book (no
    //   scripted picker), and the first conflicting source is recorded once.
    // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
    // tests: CampaignHooksTest.duplicate_registration_conflicts_and_reports_once
    // tests: CampaignHooksTest.a_conflicted_book_cannot_poison_the_default
    // tests: SemScript3.a_third_campaign_registration_keeps_the_first_conflicting_source
    if (st->campaign_registered) {
        // One campaign, one book, or none: never raise (that would kill
        // this chunk's unrelated level hooks and make the winner an
        // accident of pack-id sort order). The query side answers "no
        // scripted picker" and reports the conflict once.
        // rule: One campaign, one book: a second registration never raises, poisons the book (no
        //   scripted picker), and the first conflicting source is recorded once.
        // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
        // tests: CampaignHooksTest.duplicate_registration_conflicts_and_reports_once
        // tests: CampaignHooksTest.a_conflicted_book_cannot_poison_the_default
        // tests: SemScript3.a_third_campaign_registration_keeps_the_first_conflicting_source
        if (st->campaign_conflict_source.empty())
            st->campaign_conflict_source = campaign_caller_source(L);
        return 0;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, st->campaign_hooks_ref);
    // rule: Each hook the campaign book supplies (picker_menu, picker_action, base_camp,
    //   match_knobs, lineup.power) is installed in its own slot; an absent key leaves its slot
    //   empty so that query answers the default.
    // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    // tests: CampaignHooksTest.zone_unregistered_without_base_camp_hook
    // tests: CampaignHooksTest.match_knobs_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_absent_answers_defaults_and_false
    // tests: CampaignHooksTest.no_lineup_hook_means_no_lineup
    // tests: CampaignHooksTest.match_knobs_and_lineup_coexist_at_their_slots
    // tests: CampaignHooksTest.state_set_bounds_rejection_raises_before_mutation
    if (has_menu) {
        lua_pushvalue(L, 2);
        // plumbing: coverage::enabled() instrumentation of Lua hook declarations for the coverage
        //   gate; no sim effect
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/picker_menu");
        lua_rawseti(L, -2, kCampaignMenuSlot);
    }
    if (has_action) {
        lua_pushvalue(L, 3);
        // plumbing: coverage::enabled() instrumentation of Lua hook declarations for the coverage
        //   gate; no sim effect
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/picker_action");
        lua_rawseti(L, -2, kCampaignActionSlot);
    }
    if (has_zone) {
        lua_pushvalue(L, 4);
        // plumbing: coverage::enabled() instrumentation of Lua hook declarations for the coverage
        //   gate; no sim effect
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/base_camp");
        lua_rawseti(L, -2, kCampaignZoneSlot);
    }
    if (has_knobs) {
        lua_pushvalue(L, 5);
        // plumbing: coverage::enabled() instrumentation of Lua hook declarations for the coverage
        //   gate; no sim effect
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/match_knobs");
        lua_rawseti(L, -2, kCampaignMatchKnobsSlot);
    }
    if (has_lineup_power) {
        lua_getfield(L, 6, "power");
        // plumbing: coverage::enabled() instrumentation of Lua hook declarations for the coverage
        //   gate; no sim effect
        if (coverage::enabled())
            coverage_declare_hook(L, "campaign/lineup_power");
        lua_rawseti(L, -2, kCampaignLineupPowerSlot);
    }
    lua_pop(L, 1);
    st->campaign_lineup_registered = has_lineup;
    st->campaign_vars = std::move(vars);
    st->campaign_registered = true;
    st->campaign_source = campaign_caller_source(L);
    return 0;
}

// og.register_default_lineup({ power = fn }) — the DEFAULT lineup pricing
// a shipped pack registers for every campaign that names none of its own
// (docs/lineup-design.md C5), load-time only.
//
// Why a registrar of its own rather than a fifth key on the campaign book:
// the book is one-campaign-one-book and a SECOND og.register_campaign_hooks
// poisons the whole registration ("no scripted picker will be served"), so
// a pack that registered its default through the book would kill the picker
// of every campaign that ships one. The default lives in its own per-VM
// slot instead, and the two can never reach each other.
//
// The TABLE it takes is the campaign book's `lineup` table, spelled
// identically — a pack's default and a campaign's override differ only in
// which registrar they hand it to. Duplicate registrations overwrite (the
// og.register_level_hooks wildcard precedent, and the reason this can
// never poison anything); a campaign's own `lineup.power` still wins over
// whatever the default slot holds.
int og_register_default_lineup(lua_State* L)
{
    VmState* st = get_vm_state(L);
    // The campaign fence first, before any argument check: a campaign hook
    // may not rewrite hook tables, this one included.
    // rule: Hook registration is closed while a campaign hook is running: a campaign hook calling
    //   og.register_default_lineup raises.
    // tests: CampaignHooksTest.fence_walk_everything_outside_the_allowlist_errors
    // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
    if (st != nullptr && st->campaign_dispatch)
        script_raise(L, "og.register_default_lineup: hook registration "
                        "is not available during campaign hooks");
    luaL_checktype(L, 1, LUA_TTABLE);
    // Absolute index 1 below — a stray second argument cannot shadow it.
    lua_settop(L, 1);

    // The typo pass runs BEFORE the declaration bow-out, the
    // og.register_campaign_hooks discipline: the pass that can still
    // reject the pack is the one that should catch the misspelling.
    {
        static const char* const kLineupKeys[] = {"power"};
        lua_pushnil(L);
        // rule: The default-lineup table's only key is 'power'; a non-string key or any other name
        //   is a load error (with did-you-mean), checked even in the declaration pass.
        // tests: CampaignHooksTest.default_lineup_registration_rejections
        // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
        // tests: SemScript3.a_misspelled_default_lineup_key_suggests_power
        while (lua_next(L, 1) != 0) {
            lua_pop(L, 1);  // value; the key stays for the next iteration
            // rule: The default-lineup table's only key is 'power'; a non-string key or any other
            //   name is a load error (with did-you-mean), checked even in the declaration pass.
            // tests: CampaignHooksTest.default_lineup_registration_rejections
            // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
            // tests: SemScript3.a_misspelled_default_lineup_key_suggests_power
            if (lua_type(L, -1) != LUA_TSTRING)
                script_raise(
                    L, "og.register_default_lineup: the only key is "
                  "'power' (got a %s key)",
                    luaL_typename(L, -1));
            const std::string key = lua_tostring(L, -1);
            bool known = false;
            // rule: The default-lineup table's only key is 'power'; a non-string key or any other
            //   name is a load error (with did-you-mean), checked even in the declaration pass.
            // tests: CampaignHooksTest.default_lineup_registration_rejections
            // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
            // tests: SemScript3.a_misspelled_default_lineup_key_suggests_power
            for (const char* n : kLineupKeys)
                known = known || key == n;
            // rule: The default-lineup table's only key is 'power'; a non-string key or any other
            //   name is a load error (with did-you-mean), checked even in the declaration pass.
            // tests: CampaignHooksTest.default_lineup_registration_rejections
            // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
            // tests: SemScript3.a_misspelled_default_lineup_key_suggests_power
            if (!known) {
                const std::vector<const char*> names(std::begin(kLineupKeys),
                                                     std::end(kLineupKeys));
                const std::string hint = did_you_mean(key, names);
                script_raise(
                    L, "og.register_default_lineup: unknown key '%s'%s",
                    key.c_str(), hint.c_str());
            }
        }
    }
    lua_getfield(L, 1, "power");
    // rule: A default lineup must carry a 'power' function.
    // tests: CampaignHooksTest.default_lineup_registration_rejections
    // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
    if (lua_isnil(L, -1))
        script_raise(L, "og.register_default_lineup: carries no "
                        "'power'");
    if (!lua_isfunction(L, -1))
        script_raise(L, "og.register_default_lineup: 'power' must be "
                        "a function");

    // Declaration pass: silent no-op, the og.register_hooks precedent.
    // rule: The declaration pass installs no default lineup; in the world pass the LAST
    //   registration wins (no conflict, unlike the campaign book).
    // tests: CampaignHooksTest.the_declaration_pass_bows_out_of_behaviour_registrars
    // tests: CampaignHooksTest.a_second_default_replaces_the_first
    // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
    if (st != nullptr && st->mode == VmMode::Declare)
        return 0;

    // Last registration wins (the og.register_level_hooks wildcard
    // precedent). The table carried a second `default_fill` member for as
    // long as the wheel had a DEFAULT code to resolve; amendment 4 (E2)
    // retired the resolver, so `power` is the whole registration again.
    // plumbing: coverage::enabled() hook declaration for the coverage gate
    if (coverage::enabled())
        coverage_declare_hook(L, "campaign/default_lineup_power");
    luaL_unref(L, LUA_REGISTRYINDEX, st->default_lineup_power_ref);
    st->default_lineup_power_ref = luaL_ref(L, LUA_REGISTRYINDEX);  // pops
    return 0;
}

// og.family_id(order, family_str) → wire byte (tests/diagnostics; also lets
// scripts compare walker:family() against named families).
int og_family_id(lua_State* L)
{
    const char* order_str = luaL_checkstring(L, 1);
    const char* family_str = luaL_checkstring(L, 2);
    const OrderInfo* oi = find_order(order_str);
    // rule: og.family_id with an unknown order name is a script error naming the order.
    // tests: ScriptBindingMathTest.family_id_refuses_an_unknown_order
    if (oi == nullptr)
        script_raise(L, "og.family_id: unknown order '%s'", order_str);
    // In the declaration pass the answer does not exist yet: the ids are
    // assigned by the install this very pass feeds, and a merged family file
    // asks about its own neighbours at chunk top level. A deferred token
    // keeps the shipped `assert(og.family_id(...))` idiom truthy and raises
    // on every actual use.
    const VmState* st = get_vm_state(L);
    // rule: During the declaration pass family ids do not exist yet: og.family_id returns a truthy
    //   deferred token that raises on any numeric use.
    // tests: LuaFamilyDeclTest.family_id_defers_during_a_declaration
    // tests: ScriptBindingMathTest.family_id_refuses_an_unknown_order
    if (st != nullptr && st->mode == VmMode::Declare) {
        push_family_ref(L);
        return 1;
    }
    const int id =
        og::families::resolve_family_string_id(oi->order, family_str);
    // rule: og.family_id answers the family's wire byte, or nil when the family string names no
    //   installed family.
    // tests: ScriptBindingMathTest.family_id_refuses_an_unknown_order
    // tests: SemScript3.family_id_is_nil_for_an_unknown_family
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
    // plumbing: iterate family slots / special slots; rule below
    for (int family_id = 0; family_id < NUM_FAMILY_SLOTS; family_id++) {
        const FamilyDescriptor* fd = get_family_descriptor(family_id);
        // plumbing: no FamilyDescriptor for that slot: nothing to check (the 1560 no-hook-table arm
        //   is a semantic arm of rule below)
        if (fd == nullptr)
            continue;
        lua_rawgeti(L, -1, hook_table_key(Order::Living, family_id));
        // rule: A castable special nothing handles still spends MP and does nothing; after load
        //   this is reported: a pack that DECLARED the slot gets a pack error, a slot installed
        //   without a declaration gets a warning. A plain do_special function, a 'default' entry, a
        //   'cast = false' entry, or a disabled ('none') slot each counts as handled.
        // tests: LuaFamilyBindTest.an_unhandled_castable_special_warns_unless_declared
        // tests: LuaFamilyBindTest.cast_false_is_a_handler_and_silences_the_rule
        // tests: LuaFamilyBindTest.a_default_cast_answers_every_slot
        // tests: SpecialsByIdTest.castable_slot_with_no_handler_warns_after_load
        // tests: SpecialsByIdTest.a_default_answers_for_every_castable_slot
        // tests: SemScript3.a_family_with_no_hook_table_is_never_reported_unhandled
        // tests: SemScript3.a_plain_do_special_function_counts_as_handling_every_slot
        // tests: SemScript3.a_disabled_none_slot_is_never_reported_unhandled
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
        // plumbing: label for the diagnostic message (declared id vs legacy name)
        const char* family_label =
            fd->declared_id != nullptr ? fd->declared_id : fd->name;
        // plumbing: iterate family slots / special slots; rule below
        for (int slot = 1; slot < FD_NUM_SPECIALS; slot++) {
            const char* name = fd->special_names[slot];
            // The name alone decides: a slot's name and cost travel as the
            // kSpecialNameNone / kSpecialCostDisabled pair (packs.cpp
            // install_specials), and a declared cost is always below it.
            // rule: A castable special nothing handles still spends MP and does nothing.
            // tests: LuaFamilyBindTest.an_unhandled_castable_special_warns_unless_declared
            // tests: LuaFamilyBindTest.cast_false_is_a_handler_and_silences_the_rule
            // tests: LuaFamilyBindTest.a_default_cast_answers_every_slot
            // tests: SpecialsByIdTest.castable_slot_with_no_handler_warns_after_load
            // tests: SpecialsByIdTest.a_default_answers_for_every_castable_slot
            // tests: SemScript3.a_family_with_no_hook_table_is_never_reported_unhandled
            // tests: SemScript3.a_plain_do_special_function_counts_as_handling_every_slot
            // tests: SemScript3.a_disabled_none_slot_is_never_reported_unhandled
            if (name == nullptr || std::strcmp(name, kSpecialNameNone) == 0)
                continue;
            lua_rawgeti(L, -1, slot);
            // A stored `false` is the declaration's explicit charged no-op,
            // and it is as much an answer as a function.
            const bool handled = lua_isfunction(L, -1) || lua_isboolean(L, -1);
            lua_pop(L, 1);
            // rule: A castable special nothing handles still spends MP and does nothing.
            // tests: LuaFamilyBindTest.an_unhandled_castable_special_warns_unless_declared
            // tests: LuaFamilyBindTest.cast_false_is_a_handler_and_silences_the_rule
            // tests: LuaFamilyBindTest.a_default_cast_answers_every_slot
            // tests: SpecialsByIdTest.castable_slot_with_no_handler_warns_after_load
            // tests: SpecialsByIdTest.a_default_answers_for_every_castable_slot
            // tests: SemScript3.a_family_with_no_hook_table_is_never_reported_unhandled
            // tests: SemScript3.a_plain_do_special_function_counts_as_handling_every_slot
            // tests: SemScript3.a_disabled_none_slot_is_never_reported_unhandled
            if (handled)
                continue;
            // rule: A castable special nothing handles still spends MP and does nothing.
            // tests: LuaFamilyBindTest.an_unhandled_castable_special_warns_unless_declared
            // tests: LuaFamilyBindTest.cast_false_is_a_handler_and_silences_the_rule
            // tests: LuaFamilyBindTest.a_default_cast_answers_every_slot
            // tests: SpecialsByIdTest.castable_slot_with_no_handler_warns_after_load
            // tests: SpecialsByIdTest.a_default_answers_for_every_castable_slot
            // tests: SemScript3.a_family_with_no_hook_table_is_never_reported_unhandled
            // tests: SemScript3.a_plain_do_special_function_counts_as_handling_every_slot
            // tests: SemScript3.a_disabled_none_slot_is_never_reported_unhandled
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
            // plumbing: host_impl null guard before recording the pack error
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
    lua_pushcfunction(L, og_register_default_lineup);
    lua_setfield(L, -2, "register_default_lineup");
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
    // plumbing: installs the load-time fence on rand/is_alive/entity_id/set_entity_hooks; no
    //   branch, fence semantics live in fence_world_entry (other chunk)
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
    // plumbing: g_test_world_instruction_budget override, written only by tests
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
    // rule: Pack lib modules load eagerly in registration order before any family or behaviour
    //   chunk; a module already pulled in on demand by an earlier module's og.use is not loaded
    //   twice.
    // tests: PackLibTest.modules_load_eagerly_in_order_before_scripts
    // tests: PackLibTest.on_demand_load_inside_the_eager_pass_settles_once
    // tests: SemScript3.a_module_that_failed_on_demand_is_not_loaded_a_second_time
    for (const PackLibModule& m : pack_lib_modules()) {
        const std::string key = m.pack_id + "/" + m.name;
        // rule: Pack lib modules load eagerly in registration order before any family or behaviour
        //   chunk.
        // tests: PackLibTest.modules_load_eagerly_in_order_before_scripts
        // tests: PackLibTest.on_demand_load_inside_the_eager_pass_settles_once
        // tests: SemScript3.a_module_that_failed_on_demand_is_not_loaded_a_second_time
        if (lib_status(L, &detail_->state, key) != LibStatus::None)
            continue;
        bool settled = push_lib_export(L, &detail_->state, key);
        // rule: Pack lib modules load eagerly in registration order before any family or behaviour
        //   chunk.
        // tests: PackLibTest.modules_load_eagerly_in_order_before_scripts
        // tests: PackLibTest.on_demand_load_inside_the_eager_pass_settles_once
        // tests: SemScript3.a_module_that_failed_on_demand_is_not_loaded_a_second_time
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
    // rule: A pack whose declaration failed has its family chunks skipped in every world VM (no
    //   second error); a family chunk that fails to bind is logged and registers nothing.
    // tests: LuaPackInstall.a_pack_whose_declaration_fails_installs_nothing
    // tests: LuaFamilyBindTest.a_default_cast_answers_every_slot
    // tests: LuaFamilyBindTest.the_world_api_is_closed_during_the_bind_replay
    // tests: SemScript3.a_family_chunk_that_fails_to_bind_is_logged_once_and_latched
    for (const PackScript& fc : pack_family_chunks()) {
        // A pack whose declaration failed installed nothing, so binding
        // against it would report a second, worse error in every VM the
        // process ever builds. The installer already named the casualty.
        // rule: A pack whose declaration failed has its family chunks skipped in every world VM (no
        //   second error); a family chunk that fails to bind is logged and registers nothing.
        // tests: LuaPackInstall.a_pack_whose_declaration_fails_installs_nothing
        // tests: LuaFamilyBindTest.a_default_cast_answers_every_slot
        // tests: LuaFamilyBindTest.the_world_api_is_closed_during_the_bind_replay
        // tests: SemScript3.a_family_chunk_that_fails_to_bind_is_logged_once_and_latched
        if (pack_declaration_failed(fc.pack_id))
            continue;
        detail_->state.current_pack = fc.pack_id;
        // rule: A pack whose declaration failed has its family chunks skipped in every world VM (no
        //   second error); a family chunk that fails to bind is logged and registers nothing.
        // tests: LuaPackInstall.a_pack_whose_declaration_fails_installs_nothing
        // tests: LuaFamilyBindTest.a_default_cast_answers_every_slot
        // tests: LuaFamilyBindTest.the_world_api_is_closed_during_the_bind_replay
        // tests: SemScript3.a_family_chunk_that_fails_to_bind_is_logged_once_and_latched
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
    // rule: A behaviour script chunk that fails to compile or errors at top level registers
    //   nothing; the failure is recorded and logged, other chunks still load.
    // tests: ScriptHostPackTest.a_chunk_that_fails_to_compile_is_recorded_not_silent
    // tests: SemScript3.a_script_chunk_that_fails_registers_nothing_and_is_logged
    for (const PackScript& ps : pack_scripts()) {
        detail_->state.current_pack = ps.pack_id;
        // rule: A behaviour script chunk that fails to compile or errors at top level registers
        //   nothing; the failure is recorded and logged, other chunks still load.
        // tests: ScriptHostPackTest.a_chunk_that_fails_to_compile_is_recorded_not_silent
        // tests: SemScript3.a_script_chunk_that_fails_registers_nothing_and_is_logged
        if (!host_->run_chunk(ps.chunk_name, ps.source, ps.pack_id)) {
            // A chunk that fails to compile (or errors at its top level)
            // registers nothing: every family it carries silently loses all
            // behavior while the pack still reports installed. run_chunk
            // already latched the record; this is the operator-facing half,
            // same channel as the duplicate-hook warning above.
            const auto& errs = host_->errors();
            // plumbing: the log line names the last error, or says there is none.
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
    // plumbing: order / family id bounds guard for the hook mask
    if (oi >= masks_.size() || family_id < 0 || family_id > 255)
        return false;
    return (masks_[oi][static_cast<size_t>(family_id)] &
            (1u << static_cast<unsigned>(hook))) != 0;
}

void WorldScripts::note_hook(Order order, int family_id, FamilyHook hook)
{
    const auto oi = static_cast<size_t>(order);
    // plumbing: order / family id bounds guard for the hook mask
    if (oi >= masks_.size() || family_id < 0 || family_id > 255)
        return;
    masks_[oi][static_cast<size_t>(family_id)] |=
        (1u << static_cast<unsigned>(hook));
}

WorldScripts& active_world_scripts()
{
    // rule: Hooks run in the live world's VM during a game; with no world (picker-side
    //   level_up/promotions) a shared VM serves them and is rebuilt whenever the pack registry
    //   changes.
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
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
    // plumbing: null where label replaced with '?'
    state.where = (where != nullptr) ? where : "?";
    state.message.clear();
    // record_error folds repeats, so the freshest text for this `where` is
    // the last matching stored record.
    // rule: Every failed hook dispatch is counted and the latest failure's where/message kept for
    //   tests; the operator log line prints once per distinct error.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: SemScript3.a_repeated_hook_error_logs_once_and_a_new_error_logs_again
    for (auto it = impl.errors.rbegin(); it != impl.errors.rend(); ++it) {
        // rule: Every failed hook dispatch is counted and the latest failure's where/message kept
        //   for tests; the operator log line prints once per distinct error.
        // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
        // tests: SemScript3.a_repeated_hook_error_logs_once_and_a_new_error_logs_again
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

    // Callers have already seen WorldScripts::has_hook(order, family_id,
    // hook), and the mask bit is set only by note_hook right after a
    // lua_rawseti of a FUNCTION into that family's hook table
    // (og.register_hooks, family_decl.cpp bind_one_hook), with nothing that
    // writes nil back. So the family table and the function are both there.
    void begin(Order order, int family_id, FamilyHook hook)
    {
        VmState* st = &vm_state(L_);
        gen_ = push_dispatch_gen(L_);
        lua_rawgeti(L_, LUA_REGISTRYINDEX, st->hooks_ref);
        lua_rawgeti(L_, -1, hook_table_key(order, family_id));
        lua_rawgeti(L_, -1, static_cast<lua_Integer>(hook));
        lua_remove(L_, -2);  // family table
        lua_remove(L_, -2);  // hooks root
        nargs_ = 0;
    }

    // begin() for the DoSpecial slot, which may hold either the classic
    // do_special function or a specials table (its declared-id keys were
    // resolved to slots at registration). For a table this selects [sp],
    // then "default". When the table holds neither, returns false with a
    // clean stack: the dispatch is consumed as a successful no-op, with
    // nothing called. Same has_hook precondition as begin(): the slot holds
    // the function or the specials table (both writers store one of the
    // two), so there is no other way to come back empty.
    bool begin_special(int family_id, lua_Integer sp)
    {
        VmState* st = &vm_state(L_);
        gen_ = push_dispatch_gen(L_);
        lua_rawgeti(L_, LUA_REGISTRYINDEX, st->hooks_ref);
        lua_rawgeti(L_, -1, hook_table_key(Order::Living, family_id));
        lua_rawgeti(L_, -1,
                    static_cast<lua_Integer>(FamilyHook::DoSpecial));
        // rule: A specials table dispatches the caster's current special slot; 'cast = false' is a
        //   deliberate charged no-op that beats 'default'; a missing slot falls to 'default'; with
        //   neither the cast is a successful no-op (MP still spent, nothing runs). A plain
        //   do_special function answers every slot.
        // tests: SpecialsDispatchTest.table_selects_by_current_special
        // tests: SpecialsDispatchTest.missing_index_falls_to_default
        // tests: SpecialsDispatchTest.unmatched_index_without_default_is_a_noop_true
        // tests: SpecialsDispatchTest.plain_do_special_function_form_still_works
        // tests: LuaFamilyBindTest.cast_false_beats_the_default_for_its_own_slot
        if (lua_istable(L_, -1)) {
            lua_rawgeti(L_, -1, sp);
            // `cast = false` — the declaration's explicit charged no-op. It
            // is a HANDLER, so it beats `default` rather than falling
            // through to it: the slot was written to do nothing, on purpose.
            // rule: A specials table dispatches the caster's current special slot.
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            // tests: SpecialsDispatchTest.missing_index_falls_to_default
            // tests: SpecialsDispatchTest.unmatched_index_without_default_is_a_noop_true
            // tests: SpecialsDispatchTest.plain_do_special_function_form_still_works
            // tests: LuaFamilyBindTest.cast_false_beats_the_default_for_its_own_slot
            if (lua_isboolean(L_, -1) && !lua_toboolean(L_, -1)) {
                lua_pop(L_, 4);  // entry + specials + family + hooks root
                return false;
            }
            // rule: A specials table dispatches the caster's current special slot.
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            // tests: SpecialsDispatchTest.missing_index_falls_to_default
            // tests: SpecialsDispatchTest.unmatched_index_without_default_is_a_noop_true
            // tests: SpecialsDispatchTest.plain_do_special_function_form_still_works
            // tests: LuaFamilyBindTest.cast_false_beats_the_default_for_its_own_slot
            if (!lua_isfunction(L_, -1)) {
                lua_pop(L_, 1);
                lua_pushstring(L_, "default");
                lua_rawget(L_, -2);
            }
            // rule: A specials table dispatches the caster's current special slot.
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            // tests: SpecialsDispatchTest.missing_index_falls_to_default
            // tests: SpecialsDispatchTest.unmatched_index_without_default_is_a_noop_true
            // tests: SpecialsDispatchTest.plain_do_special_function_form_still_works
            // tests: LuaFamilyBindTest.cast_false_beats_the_default_for_its_own_slot
            if (!lua_isfunction(L_, -1)) {
                // entry + specials table + family table + hooks root
                lua_pop(L_, 4);
                return false;
            }
            lua_remove(L_, -2);  // specials table
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

    // The only guy* hook argument is hooks::level_up's self, and its one
    // caller (guy::level_up, guy.cpp) passes `this`: never null.
    void arg(guy* g)
    {
        auto* h = static_cast<GuyHandle*>(
            lua_newuserdatauv(L_, sizeof(GuyHandle), 0));
        h->raw = g;
        h->gen = gen_;
        luaL_setmetatable(L_, kGuyMeta);
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
        // rule: A hook that errors counts as absent for that dispatch (nullopt) and is latched; a
        //   result hook's return is truthiness-coerced; a void hook that ran answers true.
        // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
        // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
        // tests: LivingActFlow.the_hit_response_draws_no_ordinary_act_after_a_gates_no
        // tests: LivingActFlow.a_roll_the_gate_refuses_takes_the_ordinary_act_with_the_setting_on
        // tests: ScriptHooksTest.on_melee_hit_reports_ran_and_latches_its_own_failures
        // tests: SemScript3.a_result_hook_answer_is_lua_truthiness
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

    // Cast results are stricter than the other boolean hooks: a refusal
    // must carry a usable HUD reason. Keep the result inside this frame so
    // self:special() can re-enter dispatch without replacing an outer reason.
    std::optional<SpecialResult> call_special(const char* where,
                                              std::string* error_reason)
    {
        // A malformed return is detected after the Lua call has unwound,
        // so keep the declaring location while the closure is still here.
        // Otherwise different broken casts would share one anonymous error.
        lua_Debug source{};
        lua_pushvalue(L_, lua_gettop(L_) - nargs_);
        lua_getinfo(L_, ">S", &source);
        // plumbing: null debug source name replaced with '?'
        const std::string source_name = source.source ? source.source : "?";
        const std::size_t errors_before = impl_.errors.size();
        // rule: A special cast must return true, or false plus a printable-ASCII visible reason of
        //   at most 24 bytes; an erroring cast or any other return shape is a script error whose
        //   HUD reason is 'SPECIAL SCRIPT ERROR' and which charges no MP; a valid refusal carries
        //   its own reason to the HUD.
        // tests: SpecialsDispatchTest.table_selects_by_current_special
        // tests: SpecialsDispatchTest.malformed_cast_results_are_errors_and_do_not_charge
        // tests: SpecialsDispatchTest.branch_falling_off_the_end_is_a_contract_error
        // tests: SpecialsDispatchTest.erroring_branch_latches_like_any_hook_error
        // tests: SpecialsDispatchTest.cast_reason_length_boundary_and_null_descriptor
        if (!impl_.protected_call(where, nargs_, 2)) {
            note_hook_failure(where, impl_,
                              impl_.errors.size() > errors_before);
            // plumbing: error_reason out-param null guard
            if (error_reason)
                *error_reason = "SPECIAL SCRIPT ERROR";
            return std::nullopt;
        }

        const char* contract_error = nullptr;
        std::string reason;
        bool succeeded = false;
        // rule: A special cast must return true, or false plus a printable-ASCII visible reason of
        //   at most 24 bytes.
        // tests: SpecialsDispatchTest.table_selects_by_current_special
        // tests: SpecialsDispatchTest.malformed_cast_results_are_errors_and_do_not_charge
        // tests: SpecialsDispatchTest.branch_falling_off_the_end_is_a_contract_error
        // tests: SpecialsDispatchTest.erroring_branch_latches_like_any_hook_error
        // tests: SpecialsDispatchTest.cast_reason_length_boundary_and_null_descriptor
        if (!lua_isboolean(L_, -2)) {
            contract_error =
                "special cast must return true or false, 'reason'";
        } else {
            succeeded = lua_toboolean(L_, -2) != 0;
            // rule: A special cast must return true, or false plus a printable-ASCII visible reason
            //   of at most 24 bytes.
            // tests: SpecialsDispatchTest.table_selects_by_current_special
            // tests: SpecialsDispatchTest.malformed_cast_results_are_errors_and_do_not_charge
            // tests: SpecialsDispatchTest.branch_falling_off_the_end_is_a_contract_error
            // tests: SpecialsDispatchTest.erroring_branch_latches_like_any_hook_error
            // tests: SpecialsDispatchTest.cast_reason_length_boundary_and_null_descriptor
            if (!succeeded) {
                // rule: A special cast must return true, or false plus a printable-ASCII visible
                //   reason of at most 24 bytes.
                // tests: SpecialsDispatchTest.table_selects_by_current_special
                // tests: SpecialsDispatchTest.malformed_cast_results_are_errors_and_do_not_charge
                // tests: SpecialsDispatchTest.branch_falling_off_the_end_is_a_contract_error
                // tests: SpecialsDispatchTest.erroring_branch_latches_like_any_hook_error
                // tests: SpecialsDispatchTest.cast_reason_length_boundary_and_null_descriptor
                if (lua_type(L_, -1) != LUA_TSTRING) {
                    contract_error =
                        "special cast returning false must supply a string reason";
                } else {
                    std::size_t length = 0;
                    const char* text = lua_tolstring(L_, -1, &length);
                    const std::string_view candidate(text, length);
                    // rule: A special cast must return true, or false plus a printable-ASCII
                    //   visible reason of at most 24 bytes.
                    // tests: SpecialsDispatchTest.table_selects_by_current_special
                    // tests: SpecialsDispatchTest.malformed_cast_results_are_errors_and_do_not_charge
                    // tests: SpecialsDispatchTest.branch_falling_off_the_end_is_a_contract_error
                    // tests: SpecialsDispatchTest.erroring_branch_latches_like_any_hook_error
                    // tests: SpecialsDispatchTest.cast_reason_length_boundary_and_null_descriptor
                    if (!SpecialResult::valid_reason(candidate)) {
                        contract_error =
                            "special failure reason must contain visible text, "
                            "use printable ASCII, and fit in 24 bytes";
                    } else {
                        reason.assign(candidate);
                    }
                }
            }
        }
        lua_pop(L_, 2);

        // rule: A special cast must return true, or false plus a printable-ASCII visible reason of
        //   at most 24 bytes.
        // tests: SpecialsDispatchTest.table_selects_by_current_special
        // tests: SpecialsDispatchTest.malformed_cast_results_are_errors_and_do_not_charge
        // tests: SpecialsDispatchTest.branch_falling_off_the_end_is_a_contract_error
        // tests: SpecialsDispatchTest.erroring_branch_latches_like_any_hook_error
        // tests: SpecialsDispatchTest.cast_reason_length_boundary_and_null_descriptor
        if (contract_error) {
            const std::string message = std::format(
                "{}:{}: {}", source_name, source.linedefined, contract_error);
            impl_.record_error(where, message.c_str());
            note_hook_failure(where, impl_,
                              impl_.errors.size() > errors_before);
            // plumbing: error_reason out-param null guard
            if (error_reason)
                *error_reason = "SPECIAL SCRIPT ERROR";
            return std::nullopt;
        }
        // rule: A special cast must return true, or false plus a printable-ASCII visible reason of
        //   at most 24 bytes.
        // tests: SpecialsDispatchTest.table_selects_by_current_special
        // tests: SpecialsDispatchTest.malformed_cast_results_are_errors_and_do_not_charge
        // tests: SpecialsDispatchTest.branch_falling_off_the_end_is_a_contract_error
        // tests: SpecialsDispatchTest.erroring_branch_latches_like_any_hook_error
        // tests: SpecialsDispatchTest.cast_reason_length_boundary_and_null_descriptor
        return succeeded ? SpecialResult::success()
                         : SpecialResult::failure(std::move(reason));
    }

private:
    ScriptHost::Impl& impl_;
    lua_State* L_;
    std::uint64_t gen_ = 0;
    int nargs_ = 0;
};

const char* hook_where(FamilyHook hook)
{
    // plumbing: hook_where: maps FamilyHook to the where-label string used in error
    //   records/hook_failures().where; no game rule
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
        case FamilyHook::OnKill: return "hook:on_kill";
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
    // rule: A family with no registered Lua hook of that kind runs nothing and costs nothing: the
    //   dispatcher answers 'no hook' so the C++ caller takes its own default path.
    // tests: SpecialsDispatchTest.table_selects_by_current_special
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ScriptHooksTest.no_scripts_means_cpp_dispatch_untouched
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    if (!ws.has_hook(order, family_id, hook))
        return std::nullopt;
    HookFrame f(ws);
    f.begin(order, family_id, hook);
    (f.arg(args), ...);
    return f.call(hook_where(hook), wants_result);
}

// DoSpecial dispatch, honoring both slot forms (stub generator reads this
// separate funnel and its call_special contract).
// Returns nullopt when no script hook ran, and the hook's validated cast
// result otherwise.
// The specials-table form implements the class-pack dispatch contract:
// self:current_special() selects, a missing index falls to `default`, and a
// table with neither is the ladder's fall-through — result true, no call.
std::optional<SpecialResult> try_script_do_special(int family_id, walker* self,
                                                  std::string* error_reason)
{
    WorldScripts& ws = active_world_scripts();
    // rule: Same rule as above for specials: no do_special hook -> not
    //   handled; a table with no entry for the slot and no default -> success with nothing called;
    //   a null caster selects 'default'.
    // tests: ScriptHooksTest.register_hooks_load_error_vocabulary
    // tests: SpecialsDispatchTest.unmatched_index_without_default_is_a_noop_true
    // tests: SpecialsDispatchTest.null_self_selects_the_default_entry
    // tests: SpecialsDispatchTest.table_selects_by_current_special
    if (!ws.has_hook(Order::Living, family_id, FamilyHook::DoSpecial))
        return std::nullopt;
    // Test dispatches may pass self == nullptr; slot 0 is unmappable
    // (declared specials occupy slots 1..5), so selection falls to
    // `default`.
    const lua_Integer sp =
        (self != nullptr) ? static_cast<lua_Integer>(self->current_special())
                          : 0;
    HookFrame f(ws);
    if (!f.begin_special(family_id, sp))
        return SpecialResult::success();
    f.arg(self);
    return f.call_special(hook_where(FamilyHook::DoSpecial), error_reason);
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

std::optional<SpecialResult> do_special(const FamilyDescriptor* fd, walker* self,
                                        std::string* error_reason)
{
    // plumbing: clears the caller's decline-reason buffer before dispatch
    if (error_reason)
        error_reason->clear();
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract: check_special_ai false vetoes a bot cast; on_death any answer suppresses the
    //   bloodspot default; on_fire_weapon false cancels the shot; handle_teleport/on_ani_complete
    //   true consume the event; effect_on_act true owns the effect's tick; treasure_on_eat's answer
    //   is the eat result; absent or errored hook -> nullopt -> the classic default.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_do_special(fd->family_id, self, error_reason))
        return r;
    return std::nullopt;
}

std::optional<bool> check_special_ai(const FamilyDescriptor* fd, living* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::CheckSpecialAi, true,
                                 static_cast<walker*>(self)))
        return r;
    return std::nullopt;
}

bool hit_response(const FamilyDescriptor* fd, statistics* stats, walker* who)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr || stats == nullptr)
        return false;
    // Lua signature: hit_response(self, foe) — self is the stats' owner.
    // rule: A Lua hit_response / set_difficulty / level_up hook that RUNS replaces the classic C++
    //   default (attack-the-attacker special roll, the 11*level^2 HP difficulty formula, default
    //   level-up gains); when no hook is registered or it ERRORS, the classic default runs.
    // tests: SemScript3.a_set_difficulty_hook_that_runs_replaces_the_default_scaling
    // tests: SemScript3.a_level_up_hook_that_runs_replaces_the_default_gains
    // tests: SemScript3.a_hit_response_hook_that_runs_replaces_attack_the_attacker
    // deferred: #463 hook-ran and no-hook arms asserted for all three callers; the errored arm
    //   (default runs on top of the partial hook) is bug #463 and is not pinned
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::HitResponse,
                        false, stats->controller(), who))
        return true;
    return false;
}

bool set_difficulty(const FamilyDescriptor* fd, living* self,
                    std::uint32_t level)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: A Lua hit_response / set_difficulty / level_up hook that RUNS replaces the classic C++
    //   default (attack-the-attacker special roll, the 11*level^2 HP difficulty formula, default
    //   level-up gains).
    // tests: SemScript3.a_set_difficulty_hook_that_runs_replaces_the_default_scaling
    // tests: SemScript3.a_level_up_hook_that_runs_replaces_the_default_gains
    // tests: SemScript3.a_hit_response_hook_that_runs_replaces_attack_the_attacker
    if (try_script_hook(Order::Living, fd->family_id,
                        FamilyHook::SetDifficulty, false,
                        static_cast<walker*>(self),
                        static_cast<lua_Integer>(level)))
        return true;
    return false;
}

bool level_up(const FamilyDescriptor* fd, guy* self, std::int32_t level_diff)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: A Lua hit_response / set_difficulty / level_up hook that RUNS replaces the classic C++
    //   default (attack-the-attacker special roll, the 11*level^2 HP difficulty formula, default
    //   level-up gains).
    // tests: SemScript3.a_set_difficulty_hook_that_runs_replaces_the_default_scaling
    // tests: SemScript3.a_level_up_hook_that_runs_replaces_the_default_gains
    // tests: SemScript3.a_hit_response_hook_that_runs_replaces_attack_the_attacker
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::LevelUp,
                        false, self, static_cast<lua_Integer>(level_diff)))
        return true;
    return false;
}

std::optional<bool> on_death(const FamilyDescriptor* fd, walker* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::OnDeath, true, self))
        return r;
    return std::nullopt;
}

bool on_act_living(const FamilyDescriptor* fd, living* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: Notification hooks (per-tick living act, shoved, created, weapon customised at fire,
    //   melee hit landed, kill, weapon hit target, generator customising a fresh spawn) run
    //   alongside the C++ behaviour; the bool says only whether the hook ran without error.
    // tests: ScriptHooksTest.on_melee_hit_reports_ran_and_latches_its_own_failures
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: LevelScriptsTest.generator_customize_spawn_dispatches
    // tests: SemScript3.a_notification_hook_that_runs_answers_true_with_its_side_effects
    // tests: SemScript3.a_notification_with_no_hook_of_its_kind_answers_false
    // tests: SemScript3.a_notification_hook_that_errors_answers_false_and_latches
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnActLiving,
                        false, static_cast<walker*>(self)))
        return true;
    return false;
}

bool on_act_override(const FamilyDescriptor* fd, living* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: A family's on_act_override hook returning true owns that walker's act for the tick
    //   (classic act skipped); returning false, no hook, or an error falls through to the classic
    //   act.
    // tests: ModeTick.act_override_true_owns_the_tick
    // tests: ModeTick.act_override_false_falls_through_to_default_act
    // tests: SemScript3.an_erroring_or_absent_act_override_falls_through_to_the_default_act
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::OnActOverride, true,
                                 static_cast<walker*>(self)))
        return *r;
    return false;
}

bool on_shoved(const FamilyDescriptor* fd, walker* target)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: Notification hooks (per-tick living act, shoved, created, weapon customised at fire,
    //   melee hit landed, kill, weapon hit target, generator customising a fresh spawn) run
    //   alongside the C++ behaviour.
    // tests: ScriptHooksTest.on_melee_hit_reports_ran_and_latches_its_own_failures
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: LevelScriptsTest.generator_customize_spawn_dispatches
    // tests: SemScript3.a_notification_hook_that_runs_answers_true_with_its_side_effects
    // tests: SemScript3.a_notification_with_no_hook_of_its_kind_answers_false
    // tests: SemScript3.a_notification_hook_that_errors_answers_false_and_latches
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnShoved,
                        false, target))
        return true;
    return false;
}

std::optional<bool> on_fire_weapon(const FamilyDescriptor* fd, walker* self,
                                   walker* weapon)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::OnFireWeapon, true, self,
                                 weapon))
        return r;
    return std::nullopt;
}

std::optional<bool> handle_teleport(const FamilyDescriptor* fd, walker* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::HandleTeleport, true, self))
        return r;
    return std::nullopt;
}

bool on_create(const FamilyDescriptor* fd, walker* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: Notification hooks (per-tick living act, shoved, created, weapon customised at fire,
    //   melee hit landed, kill, weapon hit target, generator customising a fresh spawn) run
    //   alongside the C++ behaviour.
    // tests: ScriptHooksTest.on_melee_hit_reports_ran_and_latches_its_own_failures
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: LevelScriptsTest.generator_customize_spawn_dispatches
    // tests: SemScript3.a_notification_hook_that_runs_answers_true_with_its_side_effects
    // tests: SemScript3.a_notification_with_no_hook_of_its_kind_answers_false
    // tests: SemScript3.a_notification_hook_that_errors_answers_false_and_latches
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnCreate,
                        false, self))
        return true;
    return false;
}

bool customize_weapon(const FamilyDescriptor* fd, walker* self, walker* weapon)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: Notification hooks (per-tick living act, shoved, created, weapon customised at fire,
    //   melee hit landed, kill, weapon hit target, generator customising a fresh spawn) run
    //   alongside the C++ behaviour.
    // tests: ScriptHooksTest.on_melee_hit_reports_ran_and_latches_its_own_failures
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: LevelScriptsTest.generator_customize_spawn_dispatches
    // tests: SemScript3.a_notification_hook_that_runs_answers_true_with_its_side_effects
    // tests: SemScript3.a_notification_with_no_hook_of_its_kind_answers_false
    // tests: SemScript3.a_notification_hook_that_errors_answers_false_and_latches
    if (try_script_hook(Order::Living, fd->family_id,
                        FamilyHook::CustomizeWeapon, false, self, weapon))
        return true;
    return false;
}

std::optional<bool> on_ani_complete(const FamilyDescriptor* fd, walker* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::Living, fd->family_id,
                                 FamilyHook::OnAniComplete, true, self))
        return r;
    return std::nullopt;
}

bool on_melee_hit(const FamilyDescriptor* fd, walker* self, walker* target)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: Notification hooks (per-tick living act, shoved, created, weapon customised at fire,
    //   melee hit landed, kill, weapon hit target, generator customising a fresh spawn) run
    //   alongside the C++ behaviour.
    // tests: ScriptHooksTest.on_melee_hit_reports_ran_and_latches_its_own_failures
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: LevelScriptsTest.generator_customize_spawn_dispatches
    // tests: SemScript3.a_notification_hook_that_runs_answers_true_with_its_side_effects
    // tests: SemScript3.a_notification_with_no_hook_of_its_kind_answers_false
    // tests: SemScript3.a_notification_hook_that_errors_answers_false_and_latches
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnMeleeHit,
                        false, self, target))
        return true;
    return false;
}

bool on_kill(const FamilyDescriptor* fd, walker* self, walker* victim)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (fd == nullptr)
        return false;
    // rule: Notification hooks (per-tick living act, shoved, created, weapon customised at fire,
    //   melee hit landed, kill, weapon hit target, generator customising a fresh spawn) run
    //   alongside the C++ behaviour.
    // tests: ScriptHooksTest.on_melee_hit_reports_ran_and_latches_its_own_failures
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: LevelScriptsTest.generator_customize_spawn_dispatches
    // tests: SemScript3.a_notification_hook_that_runs_answers_true_with_its_side_effects
    // tests: SemScript3.a_notification_with_no_hook_of_its_kind_answers_false
    // tests: SemScript3.a_notification_hook_that_errors_answers_false_and_latches
    if (try_script_hook(Order::Living, fd->family_id, FamilyHook::OnKill,
                        false, self, victim))
        return true;
    return false;
}

std::optional<bool> weapon_on_death(const WeaponFamilyDescriptor* wfd,
                                    weap* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (wfd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::Weapon, wfd->family_id,
                                 FamilyHook::WeaponOnDeath, true,
                                 static_cast<walker*>(self)))
        return r;
    return std::nullopt;
}

std::optional<bool> weapon_on_animate(const WeaponFamilyDescriptor* wfd,
                                      weap* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (wfd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::Weapon, wfd->family_id,
                                 FamilyHook::WeaponOnAnimate, true,
                                 static_cast<walker*>(self)))
        return r;
    return std::nullopt;
}

bool weapon_on_hit_target(const WeaponFamilyDescriptor* wfd, walker* weapon,
                          walker* target, walker* owner)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (wfd == nullptr)
        return false;
    // rule: Notification hooks (per-tick living act, shoved, created, weapon customised at fire,
    //   melee hit landed, kill, weapon hit target, generator customising a fresh spawn) run
    //   alongside the C++ behaviour.
    // tests: ScriptHooksTest.on_melee_hit_reports_ran_and_latches_its_own_failures
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: LevelScriptsTest.generator_customize_spawn_dispatches
    // tests: SemScript3.a_notification_hook_that_runs_answers_true_with_its_side_effects
    // tests: SemScript3.a_notification_with_no_hook_of_its_kind_answers_false
    // tests: SemScript3.a_notification_hook_that_errors_answers_false_and_latches
    if (try_script_hook(Order::Weapon, wfd->family_id,
                        FamilyHook::WeaponOnHitTarget, false, weapon, target,
                        owner))
        return true;
    return false;
}

std::optional<bool> effect_on_act(const EffectFamilyDescriptor* efd,
                                  effect* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (efd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::FX, efd->family_id,
                                 FamilyHook::EffectOnAct, true,
                                 static_cast<walker*>(self)))
        return r;
    return std::nullopt;
}

std::optional<bool> effect_on_death(const EffectFamilyDescriptor* efd,
                                    effect* self)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (efd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::FX, efd->family_id,
                                 FamilyHook::EffectOnDeath, true,
                                 static_cast<walker*>(self)))
        return r;
    return std::nullopt;
}

std::optional<bool> treasure_on_eat(const TreasureFamilyDescriptor* tfd,
                                    treasure* self, walker* eater)
{
    // rule: An entity whose family has no descriptor has no script behaviour: every family hook
    //   answers 'not handled' (nullopt/false) and nothing is latched.
    // tests: ScriptHooksTest.an_unclaimed_family_has_no_behavior
    // tests: SemScript3.a_null_descriptor_or_null_stats_has_no_script_behaviour
    // tests: SemScript3.every_hook_wrapper_reaches_the_hook_of_a_family_with_a_descriptor
    if (tfd == nullptr)
        return std::nullopt;
    // rule: Result-carrying hooks pass the Lua answer to the C++ caller, which reads it per its own
    //   contract.
    // tests: ScriptHooksTest.erroring_hook_is_latched_loudly
    // tests: ScriptHooksTest.healthy_hook_leaves_the_failure_latch_clear
    // tests: ExampleClassPack.its_hooks_run_when_the_engine_dispatches_them
    if (auto r = try_script_hook(Order::Treasure, tfd->family_id,
                                 FamilyHook::TreasureOnEat, true,
                                 static_cast<walker*>(self), eater))
        return r;
    return std::nullopt;
}

bool generator_customize_spawn(int generator_family, walker* generator,
                               walker* spawn)
{
    return try_script_hook(Order::Generator, generator_family,
                           FamilyHook::GeneratorCustomizeSpawn, false,
                           generator, spawn)
        .has_value();
}

// ---------------------------------------------------------------------------
// Level-script dispatch
// ---------------------------------------------------------------------------

namespace {

// Cheap gate shared by all level dispatchers: no pack Lua at all (no scripts/
// and no families/ chunk, pack_lua_absent) → no VM creation, no cost,
// byte-identical sims for script-less runs.
VmState* level_vm_state(std::uint32_t kind_bit)
{
    // rule: With no pack Lua at all, or no hook of this kind registered for any level, the level
    //   dispatchers do nothing and create no VM (script-less sims stay byte-identical).
    //   level_entity_death does not go through level_vm_state; its own pack_lua_absent() check at
    //   2528 is the same rule applied to deaths.
    // tests: LevelScriptsTest.no_packs_means_every_level_dispatcher_is_inert
    // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
    if (pack_lua_absent())
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
    // rule: A level's own hook takes precedence over the wildcard (-1) hook of the same kind; the
    //   wildcard fires for any level that has none; hooks for other levels never fire.
    // tests: LevelScriptsTest.wildcard_level_hooks_fire_for_any_level
    // tests: LevelScriptsTest.other_level_hooks_do_not_fire
    // tests: LevelScriptsTest.hooks_for_another_level_do_not_fire
    // tests: SemScript3.a_levels_own_hook_takes_precedence_over_the_wildcard
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
    // rule: Diagnostic: which hook kinds a level registers by its own id; wildcard registrations
    //   are deliberately not counted. The picker uses its on_mode_init bit to tell a refusing mode
    //   from a hook-less scripted level.
    // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
    // tests: LevelScriptsTest.no_packs_means_every_level_dispatcher_is_inert
    // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
    if (pack_lua_absent())
        return 0;
    WorldScripts& ws = active_world_scripts();
    lua_State* L = ws.host().impl().L;
    VmState* st = &vm_state(L);
    std::uint32_t kinds = 0;
    for (const auto& h : kLevelHookNames) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, st->level_hooks_ref);
        lua_rawgeti(L, -1, level_hook_key(h.kind, level_id));
        // rule: Diagnostic: which hook kinds a level registers by its own id; wildcard
        //   registrations are deliberately not counted.
        // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
        // tests: LevelScriptsTest.no_packs_means_every_level_dispatcher_is_inert
        // tests: LevelScriptsTest.a_family_chunk_registers_level_hooks_in_the_real_pass
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
    // rule: On level start the level's on_load(level_id) runs; every tick on_tick(level_id,
    //   level_tick_count) runs; a level without the hook runs nothing.
    // tests: LevelScriptsTest.load_and_tick_hooks_fire_in_order
    // tests: LevelScriptsTest.run_pending_level_on_load_dispatches_once_untensed
    // tests: LevelScriptsTest.other_level_hooks_do_not_fire
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
    // rule: On level start the level's on_load(level_id) runs; every tick on_tick(level_id,
    //   level_tick_count) runs; a level without the hook runs nothing.
    // tests: LevelScriptsTest.load_and_tick_hooks_fire_in_order
    // tests: LevelScriptsTest.run_pending_level_on_load_dispatches_once_untensed
    // tests: LevelScriptsTest.other_level_hooks_do_not_fire
    if (st == nullptr || world == nullptr)
        return;
    ScriptHost::Impl& impl = st->owner->host().impl();
    // rule: On level start the level's on_load(level_id) runs; every tick on_tick(level_id,
    //   level_tick_count) runs; a level without the hook runs nothing.
    // tests: LevelScriptsTest.load_and_tick_hooks_fire_in_order
    // tests: LevelScriptsTest.run_pending_level_on_load_dispatches_once_untensed
    // tests: LevelScriptsTest.other_level_hooks_do_not_fire
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
    // rule: With no pack Lua at all, or no hook of this kind registered for any level, the level
    //   dispatchers do nothing and create no VM (script-less sims stay byte-identical).
    // tests: LevelScriptsTest.no_packs_means_every_level_dispatcher_is_inert
    // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
    if (self == nullptr || pack_lua_absent())
        return;
    WorldScripts& ws = active_world_scripts();
    VmState* st = &vm_state(ws.host().impl().L);
    ScriptHost::Impl& impl = ws.host().impl();
    lua_State* L = impl.L;

    // plumbing: the 2536 current_game/world null ternary.
    GameWorld* world =
        (current_game != nullptr) ? current_game->world : nullptr;

    // Kill attribution (D3): resolve the combat stamp into (killer,
    // killer_team) for BOTH hook arms. The stamp must be fresh
    // (kKillAttributionTicks); a stale or absent stamp is an environment
    // death (nil, -1). The killer handle may be a corpse (mutual kills) —
    // only a swept id resolves to nil; the stamped TEAM survives either way.
    walker* killer = nullptr;
    lua_Integer killer_team = -1;
    // rule: A death is credited to its last attacker (owner-chain root, with that attacker's team)
    //   only if the hit was within 48 ticks; older or absent stamps are environment deaths (killer
    //   nil, team -1). A killer whose id was swept passes nil but keeps the stamped team.
    // tests: ModeTick.weapon_kill_attributes_to_owner_chain_root
    // tests: ModeTick.environment_death_passes_nil_killer
    // tests: ModeTick.stale_attacker_stamp_reads_as_environment
    // tests: ModeTick.per_entity_death_hook_receives_killer_args
    // tests: SemScript3.a_swept_killer_passes_nil_but_keeps_the_stamped_team
    // tests: SemScript3.a_stamp_exactly_at_the_attribution_window_still_credits_the_killer
    if (world != nullptr && self->last_attacker_id() != 0 &&
        world->tick_count_ - self->last_attacker_tick() <=
            og::sim::kKillAttributionTicks) {
        killer = world->find_by_id(self->last_attacker_id());
        killer_team = static_cast<lua_Integer>(self->last_attacker_team());
    }
    // Per-entity hook first (registered via og.set_entity_hooks); the entry
    // is consumed — a dead entity id never fires twice.
    // rule: An entity's own on_death hook (og.set_entity_hooks) fires first and exactly once (the
    //   entry is consumed); an empty per-entity table fires nothing but does not block the
    //   level-wide hook.
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.an_empty_entity_hook_table_still_lets_the_level_hook_fire
    if (st->has_entity_hooks && self->entity_id() != 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, st->entity_hooks_ref);
        lua_rawgeti(L, -1, static_cast<lua_Integer>(self->entity_id()));
        // rule: An entity's own on_death hook (og.set_entity_hooks) fires first and exactly once
        //   (the entry is consumed).
        // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
        // tests: LevelScriptsTest.an_empty_entity_hook_table_still_lets_the_level_hook_fire
        if (lua_istable(L, -1)) {
            lua_rawgeti(L, -1,
                        static_cast<lua_Integer>(LevelHook::EntityDeath));
            // rule: An entity's own on_death hook (og.set_entity_hooks) fires first and exactly
            //   once (the entry is consumed).
            // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
            // tests: LevelScriptsTest.an_empty_entity_hook_table_still_lets_the_level_hook_fire
            if (lua_isfunction(L, -1)) {
                lua_remove(L, -2);   // entity's hook table
                lua_pushnil(L);
                lua_rawseti(L, -3,
                            static_cast<lua_Integer>(self->entity_id()));
                lua_remove(L, -2);   // entity_hooks root
                const std::uint64_t gen = push_dispatch_gen(L);
                push_walker_handle(L, self, gen);
                // rule: A death is credited to its last attacker (owner-chain root, with that
                //   attacker's team) only if the hit was within 48 ticks.
                // tests: ModeTick.weapon_kill_attributes_to_owner_chain_root
                // tests: ModeTick.environment_death_passes_nil_killer
                // tests: ModeTick.stale_attacker_stamp_reads_as_environment
                // tests: ModeTick.per_entity_death_hook_receives_killer_args
                // tests: SemScript3.a_swept_killer_passes_nil_but_keeps_the_stamped_team
                // tests: SemScript3.a_stamp_exactly_at_the_attribution_window_still_credits_the_killer
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
    // rule: The level-wide on_entity_death(ent, killer, killer_team) fires for every
    //   living/generator death on a level that registers it, after the per-entity hook.
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.generator_deaths_dispatch_entity_death
    // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
    if ((st->level_hook_kinds &
         (1u << static_cast<unsigned>(LevelHook::EntityDeath))) == 0)
        return;
    // plumbing: no current world guard
    if (world == nullptr)
        return;
    // rule: The level-wide on_entity_death(ent, killer, killer_team) fires for every
    //   living/generator death on a level that registers it, after the per-entity hook.
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.generator_deaths_dispatch_entity_death
    // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
    if (!push_level_hook_fn(L, st, LevelHook::EntityDeath, world->id))
        return;
    const std::uint64_t gen = push_dispatch_gen(L);
    push_walker_handle(L, self, gen);
    // rule: A death is credited to its last attacker (owner-chain root, with that attacker's team)
    //   only if the hit was within 48 ticks.
    // tests: ModeTick.weapon_kill_attributes_to_owner_chain_root
    // tests: ModeTick.environment_death_passes_nil_killer
    // tests: ModeTick.stale_attacker_stamp_reads_as_environment
    // tests: ModeTick.per_entity_death_hook_receives_killer_args
    // tests: SemScript3.a_swept_killer_passes_nil_but_keeps_the_stamped_team
    // tests: SemScript3.a_stamp_exactly_at_the_attribution_window_still_credits_the_killer
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
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::EntitySpawn));
    // rule: Each living spawned into a level that registers on_entity_spawn is passed to it (fx
    //   spawns are not).
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.fx_spawns_do_not_fire_entity_spawn
    // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
    if (st == nullptr)
        return;
    // plumbing: current_game null -> world null
    GameWorld* world =
        (current_game != nullptr) ? current_game->world : nullptr;
    // rule: Each living spawned into a level that registers on_entity_spawn is passed to it (fx
    //   spawns are not).
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.fx_spawns_do_not_fire_entity_spawn
    // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
    if (world == nullptr)
        return;
    ScriptHost::Impl& impl = st->owner->host().impl();
    // rule: Each living spawned into a level that registers on_entity_spawn is passed to it (fx
    //   spawns are not).
    // tests: LevelScriptsTest.spawn_and_death_hooks_with_per_entity_override
    // tests: LevelScriptsTest.fx_spawns_do_not_fire_entity_spawn
    // tests: LevelScriptsTest.no_level_hooks_means_no_vm_activity
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
    // rule: A scripted mode activates only if its on_mode_init runs without error; no hook or an
    //   erroring init leaves the mode inactive and the level plays by classic rules.
    // tests: ModeTick.run_tick_refuses_failed_init_world_when_called_directly
    // tests: ModeTick.stage_init_failed_shape_defaults_and_stays_classic
    // tests: ModeTick.stage_init_activates_without_a_tick
    // tests: ModeTick.scripted_bit_plus_hooks_activates_once
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
    // rule: An active mode's on_mode_tick(level, tick) runs each tick on a level that registers it.
    // tests: ModeTick.scripted_bit_plus_hooks_activates_once
    // tests: ModeTick.stage_init_activates_without_a_tick
    // tests: SemScript3.an_active_mode_without_on_mode_tick_runs_no_mode_tick
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
    // rule: A revived walker is handed to the level's on_respawn so the mode can place it (e.g. at
    //   its team anchor); without the hook it stays where the engine revived it.
    // tests: ModesCtf.dead_hero_revives_at_team_anchor_with_cursor_rotation
    // tests: ModeTick.respawn_fires_in_place_and_dispatches_hook
    // tests: RespawnEngine.player_corpse_revives_in_place_with_control_preserved
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

bool level_lineup_stage(GameWorld* world)
{
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::LineupStage));
    // rule: The lineup stage hook (packs/core wildcard on_lineup_stage) runs for a staged level
    //   when registered; the answer means 'a hook ran', not success.
    // tests: LevelScriptsTest.no_packs_means_every_level_dispatcher_is_inert
    // tests: LevelScriptsTest.hooks_for_another_level_do_not_fire
    // tests: ClassicLineupTest.gladiator_map_units_off_strips_one_team
    if (st == nullptr || world == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    // rule: The lineup-stage step only reports 'a hook ran' when this level (exact id or wildcard)
    //   registered on_lineup_stage; it reports true even when the hook errors half way, because
    //   whatever it fielded before the error stays fielded and the tick must re-scan foes.
    // tests: LevelScriptsTest.no_packs_means_every_level_dispatcher_is_inert
    // tests: LevelScriptsTest.hooks_for_another_level_do_not_fire
    // tests: LineupStageLazyArm.refused_mode_world_runs_the_step_once
    // tests: SemScript3.a_lineup_stage_hook_that_raises_still_reports_it_ran
    if (!push_level_hook_fn(L, st, LevelHook::LineupStage, world->id))
        return false;
    const std::uint64_t gen = push_dispatch_gen(L);
    lua_pushinteger(L, world->id);
    // Reports "a hook RAN", not "it succeeded": the caller uses the answer to
    // decide whether this world may still have gained fighters after the
    // tick's foe scan, and a hook that errored half way through fielded
    // whatever it fielded. The error itself is recorded by protected_call.
    impl.protected_call("level:on_lineup_stage", 1, 0);
    pop_dispatch_gen(L, gen);
    return true;
}

short level_damage_gate(walker* target, walker* attacker, short amount)
{
    VmState* st =
        level_vm_state(1u << static_cast<unsigned>(LevelHook::Damage));
    // rule: A hit on a level with no on_damage hook (none registered at all, or only for another
    //   level id) lands the authored amount untouched - the classic damage path is byte-identical
    //   when no scripted mode gates damage. An exact-level registration shadows the wildcard one.
    // tests: ModeTick.damage_gate_classic_path_untouched_without_hook
    // tests: ModeTick.damage_gate_nil_keeps_authored_amount
    if (st == nullptr || target == nullptr)
        return amount;
    // plumbing: current_game / world null guard in level_damage_gate: no live world means no level
    //   to gate; returns the authored amount.
    GameWorld* world =
        (current_game != nullptr) ? current_game->world : nullptr;
    if (world == nullptr)
        return amount;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    // rule: A hit on a level with no on_damage hook (none registered at all, or only for another
    //   level id) lands the authored amount untouched - the classic damage path is byte-identical
    //   when no scripted mode gates damage.
    // tests: ModeTick.damage_gate_classic_path_untouched_without_hook
    // tests: ModeTick.damage_gate_nil_keeps_authored_amount
    if (!push_level_hook_fn(L, st, LevelHook::Damage, world->id))
        return amount;
    const std::uint64_t gen = push_dispatch_gen(L);
    push_walker_handle(L, target, gen);
    push_walker_handle(L, attacker, gen);  // nil for an unattributed hit
    lua_pushinteger(L, static_cast<lua_Integer>(amount));
    short result = amount;
    // A hook ERROR keeps the authored amount (R9: the hook counts as absent
    // for this dispatch; deterministic on every peer since only the
    // authoritative world dispatches).
    // rule: The level's on_damage answer decides the hit: false cancels it outright (no damage, no
    //   attribution stamp, no hit_response - and a 0-hp target is spared), true or nil or any
    //   non-number keeps the authored amount, a number replaces it clamped to 0..32767 with
    //   fractions truncated (NaN and negatives land on 0; a 0 replacement is a zero-damage hit
    //   whose death check still runs in walker::attack), and a hook error keeps the authored amount
    //   and records the error. Same gate is consulted by the lethal special path in
    //   walker_specials.cpp, where a replacement below the lethal amount is ordinary damage and
    //   never a kill.
    // tests: ModeTick.damage_gate_error_keeps_authored_amount
    // tests: ModeTick.damage_gate_false_cancels_the_hit
    // tests: ModeTick.damage_gate_false_spares_a_zero_hp_target
    // tests: ModeTick.damage_gate_true_keeps_authored_amount
    // tests: ModeTick.damage_gate_nil_keeps_authored_amount
    // tests: ModeTick.damage_gate_non_number_return_keeps_amount
    // tests: ModeTick.damage_gate_number_replaces_amount
    // tests: ModeTick.damage_gate_negative_replacement_clamps_to_zero
    // tests: ModeTick.damage_gate_nan_replacement_is_treated_as_zero
    // tests: ModeTick.damage_gate_huge_replacement_clamps_to_short_max
    // tests: ModeTick.damage_gate_zero_still_kills_a_zero_hp_target
    // tests: ModeTick.damage_gate_granting_a_ward_mid_hit_lands_no_damage
    if (impl.protected_call("level:on_damage", 3, 1)) {
        // rule: The level's on_damage answer decides the hit.
        // tests: ModeTick.damage_gate_error_keeps_authored_amount
        // tests: ModeTick.damage_gate_false_cancels_the_hit
        // tests: ModeTick.damage_gate_false_spares_a_zero_hp_target
        // tests: ModeTick.damage_gate_true_keeps_authored_amount
        // tests: ModeTick.damage_gate_nil_keeps_authored_amount
        // tests: ModeTick.damage_gate_non_number_return_keeps_amount
        // tests: ModeTick.damage_gate_number_replaces_amount
        // tests: ModeTick.damage_gate_negative_replacement_clamps_to_zero
        // tests: ModeTick.damage_gate_nan_replacement_is_treated_as_zero
        // tests: ModeTick.damage_gate_huge_replacement_clamps_to_short_max
        // tests: ModeTick.damage_gate_zero_still_kills_a_zero_hp_target
        // tests: ModeTick.damage_gate_granting_a_ward_mid_hit_lands_no_damage
        if (lua_isboolean(L, -1)) {
            // false = cancel the hit outright; true = keep the amount.
            // rule: The level's on_damage answer decides the hit.
            // tests: ModeTick.damage_gate_error_keeps_authored_amount
            // tests: ModeTick.damage_gate_false_cancels_the_hit
            // tests: ModeTick.damage_gate_false_spares_a_zero_hp_target
            // tests: ModeTick.damage_gate_true_keeps_authored_amount
            // tests: ModeTick.damage_gate_nil_keeps_authored_amount
            // tests: ModeTick.damage_gate_non_number_return_keeps_amount
            // tests: ModeTick.damage_gate_number_replaces_amount
            // tests: ModeTick.damage_gate_negative_replacement_clamps_to_zero
            // tests: ModeTick.damage_gate_nan_replacement_is_treated_as_zero
            // tests: ModeTick.damage_gate_huge_replacement_clamps_to_short_max
            // tests: ModeTick.damage_gate_zero_still_kills_a_zero_hp_target
            // tests: ModeTick.damage_gate_granting_a_ward_mid_hit_lands_no_damage
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
            // rule: The level's on_damage answer decides the hit.
            // tests: ModeTick.damage_gate_error_keeps_authored_amount
            // tests: ModeTick.damage_gate_false_cancels_the_hit
            // tests: ModeTick.damage_gate_false_spares_a_zero_hp_target
            // tests: ModeTick.damage_gate_true_keeps_authored_amount
            // tests: ModeTick.damage_gate_nil_keeps_authored_amount
            // tests: ModeTick.damage_gate_non_number_return_keeps_amount
            // tests: ModeTick.damage_gate_number_replaces_amount
            // tests: ModeTick.damage_gate_negative_replacement_clamps_to_zero
            // tests: ModeTick.damage_gate_nan_replacement_is_treated_as_zero
            // tests: ModeTick.damage_gate_huge_replacement_clamps_to_short_max
            // tests: ModeTick.damage_gate_zero_still_kills_a_zero_hp_target
            // tests: ModeTick.damage_gate_granting_a_ward_mid_hit_lands_no_damage
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
        // rule: Every campaign hook (picker page/action, base camp zone, match knobs, lineup power)
        //   runs inside the campaign fence - sim bindings such as the RNG and world mutators raise
        //   - and the fence is only raised at top-level entry; a campaign hook dispatched from
        //   inside another Lua entry is a caller bug and is refused (the dispatcher answers 'no
        //   scripted picker'). The fence drops when the dispatch returns.
        // tests: CampaignHooksTest.fence_walk_everything_outside_the_allowlist_errors
        // tests: CampaignHooksTest.lineup_is_fenced_like_every_other_campaign_hook
        // tests: CampaignHooksTest.zone_dispatch_is_fenced_and_brackets_cleanly
        // tests: CampaignHooksTest.match_knobs_dispatch_is_fenced_and_brackets_cleanly
        // unreachable: the refuse-when-nested arm sits behind an assert on the same condition
        //   (ci-test builds keep asserts), and no campaign hook is dispatched from inside another
        //   Lua entry
        if (impl.entry_depth == 0 && !st.campaign_dispatch) {
            st_.campaign_dispatch = true;
            armed_ = true;
        }
    }
    ~CampaignDispatchScope()
    {
        // rule: Every campaign hook (picker page/action, base camp zone, match knobs, lineup power)
        //   runs inside the campaign fence - sim bindings such as the RNG and world mutators raise
        //   - and the fence is only raised at top-level entry.
        // tests: CampaignHooksTest.fence_walk_everything_outside_the_allowlist_errors
        // tests: CampaignHooksTest.lineup_is_fenced_like_every_other_campaign_hook
        // tests: CampaignHooksTest.zone_dispatch_is_fenced_and_brackets_cleanly
        // tests: CampaignHooksTest.match_knobs_dispatch_is_fenced_and_brackets_cleanly
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

// Gate shared by every campaign query/dispatch: no pack Lua at all
// (pack_lua_absent) → nothing; a conflicted registration answers "no scripted picker" and records the
// conflict as a script error ONCE (the once-latch lives in the VmState, so
// a VM rebuild that replays the same conflict reports again — correctly,
// since the replay re-created it).
VmState* campaign_vm_state()
{
    // rule: One campaign, one book: with no pack Lua or no og.register_campaign_hooks there is no
    //   scripted picker; a second registration conflicts and then NO book is served (not the first,
    //   not the second), with the conflict recorded as a script error exactly once naming both
    //   source files.
    // tests: CampaignHooksTest.no_registration_means_no_picker
    // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
    // tests: CampaignHooksTest.duplicate_registration_conflicts_and_reports_once
    if (pack_lua_absent())
        return nullptr;
    WorldScripts& ws = active_world_scripts();
    ScriptHost::Impl& impl = ws.host().impl();
    VmState* st = get_vm_state(impl.L);
    if (st == nullptr || !st->campaign_registered)
        return nullptr;
    if (!st->campaign_conflict_source.empty()) {
        // rule: One campaign, one book: with no pack Lua or no og.register_campaign_hooks there is
        //   no scripted picker.
        // tests: CampaignHooksTest.no_registration_means_no_picker
        // tests: CampaignHooksTest.registration_happy_path_serves_picker_and_vars
        // tests: CampaignHooksTest.duplicate_registration_conflicts_and_reports_once
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

// The VM whose DEFAULT lineup slot answers when the active book carries no
// `lineup.power` (docs/lineup-design.md C5). Deliberately NOT
// campaign_vm_state: the default belongs to a shipped pack, not to a
// campaign, so it answers with no book registered at all — and with a
// CONFLICTED one, which is the half that makes the default un-poisonable.
// Null when no pack Lua is installed or nothing registered a default.
VmState* default_lineup_vm_state()
{
    // rule: The shipped pack's default lineup pricer answers whenever one is registered,
    //   independent of any campaign book - including when the book is absent or conflicted (the
    //   default cannot be poisoned); with no pack Lua or no default registered there is no metric
    //   at all.
    // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
    // tests: CampaignHooksTest.a_conflicted_book_cannot_poison_the_default
    // tests: CampaignHooksTest.no_book_and_no_default_is_no_metric
    // tests: LevelScriptsTest.no_packs_means_every_level_dispatcher_is_inert
    if (pack_lua_absent())
        return nullptr;
    WorldScripts& ws = active_world_scripts();
    VmState* st = get_vm_state(ws.host().impl().L);
    if (st == nullptr || st->owner == nullptr ||
        st->default_lineup_power_ref < 0)
        return nullptr;
    return st;
}

// Pushes the registered hook fn for `slot`, or returns false, stack clean.
bool push_campaign_hook_fn(lua_State* L, VmState* st, lua_Integer slot)
{
    lua_rawgeti(L, LUA_REGISTRYINDEX, st->campaign_hooks_ref);
    lua_rawgeti(L, -1, slot);
    // rule: A campaign book serves only the hooks it registered: an empty slot (no picker_menu, no
    //   picker_action, no base_camp, no match_knobs) answers 'not registered' and the engine shows
    //   its stock screen for that surface.
    // tests: CampaignHooksTest.zone_unregistered_without_base_camp_hook
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_absent_answers_defaults_and_false
    // tests: CampaignHooksTest.action_without_registration_answers_false
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
    // rule: Every optional field a campaign script returns follows one discipline: absent (nil)
    //   keeps the engine default, present with the wrong Lua type (a float where an integer is
    //   required, a number where a string is required) makes the whole answer malformed - the
    //   engine never coerces. Fields are read raw, so a metatable on the returned table can neither
    //   supply nor hide a field.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.page_parse_happy_path
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
    // rule: Every optional field a campaign script returns follows one discipline.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.page_parse_happy_path
    lua_rawget(L, idx > 0 ? idx : idx - 1);
    present = !lua_isnil(L, -1);
    // rule: Every optional field a campaign script returns follows one discipline.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.page_parse_happy_path
    if (!present) {
        lua_pop(L, 1);
        return true;
    }
    // rule: Every optional field a campaign script returns follows one discipline.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.page_parse_happy_path
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
    // rule: Every optional field a campaign script returns follows one discipline.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.page_parse_happy_path
    lua_rawget(L, idx > 0 ? idx : idx - 1);
    // rule: Every optional field a campaign script returns follows one discipline.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.page_parse_happy_path
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return true;
    }
    // rule: Every optional field a campaign script returns follows one discipline.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.page_parse_happy_path
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
    // rule: A picker row (and a zone action row) is exactly one of three kinds: a level to launch,
    //   a sub-page to open, or an action to run; any other word makes the page malformed.
    // tests: CampaignHooksTest.page_parse_happy_path
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
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
    // rule: A picker/zone row must carry a non-empty string id and a valid kind; label/note
    //   (strings), level/cost (integers), done/replay (booleans) are optional but must be typed.
    //   Whether the replay mark arms is the engine's check (level cleared), not the script's.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.page_parse_happy_path
    // tests: WestlandsFireCamp.provisions_stack_to_the_cap_and_retire
    // tests: SemScript3.a_page_entry_id_must_be_a_non_empty_string
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
    // rule: A picker page must be a table with a string title; anything else means no scripted
    //   picker this dispatch.
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    if (!lua_istable(L, -1))
        return malformed_page(impl, std::string("returned a ") +
                                        luaL_typename(L, -1) +
                                        ", not a page table");
    bool present = false;
    if (!read_string_field(L, -1, "title", out.title, present) || !present)
        return malformed_page(impl, "'title' is missing or not a string");

    lua_pushstring(L, "lines");
    lua_rawget(L, -2);
    // rule: A page's text lines are optional; when present they must be an array of strings, and
    //   the engine CLIPS (not rejects) past 6 lines.
    // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.page_parse_happy_path
    if (!lua_isnil(L, -1)) {
        // rule: A page's text lines are optional; when present they must be an array of strings,
        //   and the engine CLIPS (not rejects) past 6 lines.
        // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
        // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
        // tests: CampaignHooksTest.page_parse_happy_path
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return malformed_page(impl, "'lines' is not an array");
        }
        const lua_Integer count = std::min<lua_Integer>(
            static_cast<lua_Integer>(lua_rawlen(L, -1)),
            kCampaignPageMaxLines);
        // rule: A page's text lines are optional; when present they must be an array of strings,
        //   and the engine CLIPS (not rejects) past 6 lines.
        // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
        // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
        // tests: CampaignHooksTest.page_parse_happy_path
        for (lua_Integer i = 1; i <= count; i++) {
            lua_rawgeti(L, -1, i);
            // rule: A page's text lines are optional; when present they must be an array of
            //   strings, and the engine CLIPS (not rejects) past 6 lines.
            // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
            // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
            // tests: CampaignHooksTest.page_parse_happy_path
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
    // rule: A page's rows are optional; when present they must be an array of row tables, clipped
    //   at 24; one malformed row rejects the whole page.
    // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.page_parse_happy_path
    if (!lua_isnil(L, -1)) {
        // rule: A page's rows are optional; when present they must be an array of row tables,
        //   clipped at 24; one malformed row rejects the whole page.
        // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
        // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
        // tests: CampaignHooksTest.page_parse_happy_path
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return malformed_page(impl, "'entries' is not an array");
        }
        const lua_Integer count = std::min<lua_Integer>(
            static_cast<lua_Integer>(lua_rawlen(L, -1)),
            kCampaignPageMaxEntries);
        // rule: A page's rows are optional; when present they must be an array of row tables,
        //   clipped at 24; one malformed row rejects the whole page.
        // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
        // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
        // tests: CampaignHooksTest.page_parse_happy_path
        for (lua_Integer i = 1; i <= count; i++) {
            const std::string at = "entries[" + std::to_string(i) + "]";
            lua_rawgeti(L, -1, i);
            // rule: A page's rows are optional; when present they must be an array of row tables,
            //   clipped at 24; one malformed row rejects the whole page.
            // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
            // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
            // tests: CampaignHooksTest.page_parse_happy_path
            if (!lua_istable(L, -1)) {
                lua_pop(L, 2);
                return malformed_page(impl, at + " is not a table");
            }
            CampaignPageEntry entry;
            std::string bad;
            // rule: A page's rows are optional; when present they must be an array of row tables,
            //   clipped at 24; one malformed row rejects the whole page.
            // tests: CampaignHooksTest.page_parse_clips_entries_and_lines
            // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
            // tests: CampaignHooksTest.page_parse_happy_path
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
    // rule: A Base Camp zone widget is one of four kinds: the hero roster, a text block, an action
    //   list, or a readout; any other word rejects the zone.
    // tests: CampaignHooksTest.zone_parse_happy_path
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
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
    // rule: Roster locks name heroes by campaign tag 1..255 or the untagged state (unset = true) -
    //   exactly one of the two, never a guy id - at most 24 locks (one per roster slot), each with
    //   an optional string reason; any violation rejects the zone.
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.zone_parse_happy_path
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
        // rule: Roster locks name heroes by campaign tag 1..255 or the untagged state (unset =
        //   true) - exactly one of the two, never a guy id - at most 24 locks (one per roster
        //   slot), each with an optional string reason.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            bad = lock_at + " is not a table";
            return false;
        }
        CampaignRosterLock lock;
        bool tag_present = false;
        int tag = 0;
        // rule: Roster locks name heroes by campaign tag 1..255 or the untagged state (unset =
        //   true) - exactly one of the two, never a guy id - at most 24 locks (one per roster
        //   slot), each with an optional string reason.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
        if (!read_int_field_opt(L, -1, "tag", tag, tag_present)) {
            lua_pop(L, 2);
            bad = lock_at + ".tag is not an integer";
            return false;
        }
        // rule: Roster locks name heroes by campaign tag 1..255 or the untagged state (unset =
        //   true) - exactly one of the two, never a guy id - at most 24 locks (one per roster
        //   slot), each with an optional string reason.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
        if (!read_bool_field(L, -1, "unset", lock.unset)) {
            lua_pop(L, 2);
            bad = lock_at + ".unset is not a boolean";
            return false;
        }
        // rule: Roster locks name heroes by campaign tag 1..255 or the untagged state (unset =
        //   true) - exactly one of the two, never a guy id - at most 24 locks (one per roster
        //   slot), each with an optional string reason.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
        if (tag_present == lock.unset) {
            lua_pop(L, 2);
            bad = lock_at + " needs exactly one of tag / unset = true";
            return false;
        }
        // rule: Roster locks name heroes by campaign tag 1..255 or the untagged state (unset =
        //   true) - exactly one of the two, never a guy id - at most 24 locks (one per roster
        //   slot), each with an optional string reason.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
        if (tag_present) {
            // rule: Roster locks name heroes by campaign tag 1..255 or the untagged state (unset =
            //   true) - exactly one of the two, never a guy id - at most 24 locks (one per roster
            //   slot), each with an optional string reason.
            // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
            // tests: CampaignHooksTest.zone_parse_happy_path
            if (tag < 1 || tag > 255) {
                lua_pop(L, 2);
                bad = lock_at + ".tag must be 1..255 (0 is the unset state "
                                "— lock it with unset = true)";
                return false;
            }
            lock.tag = tag;
        }
        bool present = false;
        // rule: Roster locks name heroes by campaign tag 1..255 or the untagged state (unset =
        //   true) - exactly one of the two, never a guy id - at most 24 locks (one per roster
        //   slot), each with an optional string reason.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
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
    // rule: The roster's optional assign chip writes a campaign var whose key is a safe id
    //   ([a-z0-9_], 1-32), toggles between exactly two non-empty labels, and may carry a frozen
    //   reason; any violation rejects the zone.
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.zone_parse_happy_path
    // tests: SemScript3.a_non_string_assign_label_rejects_the_zone
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
        // rule: The roster's optional assign chip writes a campaign var whose key is a safe id
        //   ([a-z0-9_], 1-32), toggles between exactly two non-empty labels, and may carry a frozen
        //   reason.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
        // tests: SemScript3.a_non_string_assign_label_rejects_the_zone
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
    // rule: The roster widget's five capabilities (deploy, train, reorder, team, hire) are booleans
    //   the campaign switches on or off; a non-boolean rejects the zone; then locks and assign are
    //   parsed.
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.zone_parse_happy_path
    for (const auto& cap : caps) {
        // rule: The roster widget's five capabilities (deploy, train, reorder, team, hire) are
        //   booleans the campaign switches on or off.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
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
    // rule: A text widget holds at most 6 string lines; unlike a picker page, over-budget is a hard
    //   rejection (the default zone is shown), never a clip.
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignPickerSessionTest.terminal_camp_skips_empty_text_and_empty_actions
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
        // rule: A text widget holds at most 6 string lines; unlike a picker page, over-budget is a
        //   hard rejection (the default zone is shown), never a clip.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignPickerSessionTest.terminal_camp_skips_empty_text_and_empty_actions
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
    // rule: Action rows are capped at 16 across the WHOLE zone (both actions widgets together),
    //   because the ordinal band behind them only fits 16 rows + pagers; each row follows the
    //   picker-row vocabulary.
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignPickerSessionTest.terminal_camp_skips_empty_text_and_empty_actions
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
        // rule: Action rows are capped at 16 across the WHOLE zone (both actions widgets together),
        //   because the ordinal band behind them only fits 16 rows + pagers.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignPickerSessionTest.terminal_camp_skips_empty_text_and_empty_actions
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            bad = entry_at + " is not a table";
            return false;
        }
        CampaignPageEntry entry;
        // rule: Action rows are capped at 16 across the WHOLE zone (both actions widgets together),
        //   because the ordinal band behind them only fits 16 rows + pagers.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignPickerSessionTest.terminal_camp_skips_empty_text_and_empty_actions
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
    // rule: A readout shows at most 3 label/value string pairs; both strings are required per item;
    //   over-budget rejects the zone.
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: SemScript3.a_readout_without_items_is_accepted_and_shows_none
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
        // rule: A readout shows at most 3 label/value string pairs; both strings are required per
        //   item; over-budget rejects the zone.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: SemScript3.a_readout_without_items_is_accepted_and_shows_none
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            bad = item_at + " is not a table";
            return false;
        }
        CampaignZoneWidget::ReadoutItem item;
        bool present = false;
        // rule: A readout shows at most 3 label/value string pairs; both strings are required per
        //   item; over-budget rejects the zone.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: SemScript3.a_readout_without_items_is_accepted_and_shows_none
        if (!read_string_field(L, -1, "label", item.label, present) ||
            !present) {
            lua_pop(L, 2);
            bad = item_at + ".label is missing or not a string";
            return false;
        }
        // rule: A readout shows at most 3 label/value string pairs; both strings are required per
        //   item; over-budget rejects the zone.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: SemScript3.a_readout_without_items_is_accepted_and_shows_none
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
    // rule: Every widget declares a kind and an optional integer layout weight in 0..8 row units.
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
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
    // plumbing: switch on widget.kind dispatching to the per-kind parser (rows
    //   above/3262/3303/3349); the trailing return false is unreachable for a
    //   valid enum.
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
    // rule: A Base Camp zone is a table whose widgets array lists at most 6 widget tables; any
    //   malformed widget rejects the whole zone and the DEFAULT zone renders.
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.zone_parse_happy_path
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
        // rule: A Base Camp zone is a table whose widgets array lists at most 6 widget tables; any
        //   malformed widget rejects the whole zone and the DEFAULT zone renders.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2);
            return malformed_zone(impl, at + " is not a table");
        }
        CampaignZoneWidget widget;
        std::string bad;
        // rule: A Base Camp zone is a table whose widgets array lists at most 6 widget tables; any
        //   malformed widget rejects the whole zone and the DEFAULT zone renders.
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
        // tests: CampaignHooksTest.zone_parse_happy_path
        if (!parse_zone_widget(L, at, widget, total_entries, bad)) {
            lua_pop(L, 2);
            return malformed_zone(impl, bad);
        }
        // rule: A zone has exactly one roster, at most one readout, at most two action lists and at
        //   most two text blocks (singleton ordinals for deploy/chip/move-up and the readout row).
        // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
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
    // rule: A zone has exactly one roster, at most one readout, at most two action lists and at
    //   most two text blocks (singleton ordinals for deploy/chip/move-up and the readout row).
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    if (roster_widgets != 1)
        return malformed_zone(
            impl, "declares " + std::to_string(roster_widgets) +
                      " roster widgets (exactly one required)");
    // rule: A zone has exactly one roster, at most one readout, at most two action lists and at
    //   most two text blocks (singleton ordinals for deploy/chip/move-up and the readout row).
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    if (readout_widgets > kCampaignZoneMaxReadoutWidgets)
        return malformed_zone(
            impl, "declares " + std::to_string(readout_widgets) +
                      " readout widgets (max " +
                      std::to_string(kCampaignZoneMaxReadoutWidgets) + ")");
    // rule: A zone has exactly one roster, at most one readout, at most two action lists and at
    //   most two text blocks (singleton ordinals for deploy/chip/move-up and the readout row).
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    if (actions_widgets > kCampaignZoneMaxActionsWidgets)
        return malformed_zone(
            impl, "declares " + std::to_string(actions_widgets) +
                      " actions widgets (max " +
                      std::to_string(kCampaignZoneMaxActionsWidgets) + ")");
    // rule: A zone has exactly one roster, at most one readout, at most two action lists and at
    //   most two text blocks (singleton ordinals for deploy/chip/move-up and the readout row).
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    if (text_widgets > kCampaignZoneMaxTextWidgets)
        return malformed_zone(
            impl, "declares " + std::to_string(text_widgets) +
                      " text widgets (max " +
                      std::to_string(kCampaignZoneMaxTextWidgets) + ")");
    return true;
}

// ---------------------------------------------------------------------------
// match_knobs parse (docs/match-setup-design.md; the SETUP wizard's shape)
// ---------------------------------------------------------------------------

bool malformed_knobs(ScriptHost::Impl& impl, const std::string& what)
{
    const std::string message =
        "campaign match_knobs returned a malformed answer: " + what;
    LogError("class pack: {}\n", message);
    impl.record_error("campaign:match_knobs", message.c_str());
    return false;
}

// The FILL wheel spelled as the campaign spells it. One word, one code:
// the engine never invents a sixth rung.
bool parse_fill_word(const std::string& word, std::int16_t& out)
{
    // rule: The FILL wheel has exactly five rungs a campaign can name for its deal - none, weak,
    //   fair, strong, brutal - and the engine never invents another.
    // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    if (word == "none") {
        out = og::sim::kFillNone;
        return true;
    }
    if (word == "weak") {
        out = og::sim::kFillWeak;
        return true;
    }
    if (word == "fair") {
        out = og::sim::kFillFair;
        return true;
    }
    if (word == "strong") {
        out = og::sim::kFillStrong;
        return true;
    }
    if (word == "brutal") {
        out = og::sim::kFillBrutal;
        return true;
    }
    return false;
}

// `lines` is an ARRAY of short strings, with the `vars` discipline: a
// hash-keyed table has rawlen 0 and would silently answer no lines at all.
bool parse_knobs_lines(lua_State* L, ScriptHost::Impl& impl,
                       CampaignMatchKnobs& out)
{
    const lua_Integer count = static_cast<lua_Integer>(lua_rawlen(L, -1));
    // rule: The SETUP wizard's campaign note is at most 2 lines of 1..38 glyphs, given as a true
    //   array: a hash-keyed or holed table is refused rather than silently read as no lines.
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
    if (count > kCampaignMatchKnobsMaxLines) {
        return malformed_knobs(
            impl, "'lines' names " + std::to_string(count) + " lines (max " +
                      std::to_string(kCampaignMatchKnobsMaxLines) + ")");
    }
    for (lua_Integer i = 1; i <= count; i++) {
        lua_rawgeti(L, -1, i);
        // rule: The SETUP wizard's campaign note is at most 2 lines of 1..38 glyphs, given as a
        //   true array: a hash-keyed or holed table is refused rather than silently read as no
        //   lines.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        if (lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 1);
            return malformed_knobs(impl,
                                   "'lines' is not an array of strings");
        }
        std::size_t len = 0;
        const char* text = lua_tolstring(L, -1, &len);
        // rule: The SETUP wizard's campaign note is at most 2 lines of 1..38 glyphs, given as a
        //   true array: a hash-keyed or holed table is refused rather than silently read as no
        //   lines.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        if (len == 0 ||
            len > static_cast<std::size_t>(kCampaignMatchKnobsLineMax)) {
            lua_pop(L, 1);
            return malformed_knobs(
                impl, "lines[" + std::to_string(i) +
                          "] is empty or longer than " +
                          std::to_string(kCampaignMatchKnobsLineMax) +
                          " glyphs");
        }
        out.lines.emplace_back(text, len);
        lua_pop(L, 1);
    }
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        lua_pop(L, 1);  // value; the key stays for the next iteration
        lua_Integer index = 0;
        // rule: The SETUP wizard's campaign note is at most 2 lines of 1..38 glyphs, given as a
        //   true array: a hash-keyed or holed table is refused rather than silently read as no
        //   lines.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        if (lua_type(L, -1) == LUA_TNUMBER && lua_isinteger(L, -1))
            index = lua_tointeger(L, -1);
        // rule: The SETUP wizard's campaign note is at most 2 lines of 1..38 glyphs, given as a
        //   true array: a hash-keyed or holed table is refused rather than silently read as no
        //   lines.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        if (index < 1 || index > count) {
            lua_pop(L, 1);  // the key
            return malformed_knobs(impl,
                                   "'lines' is not an array of strings");
        }
    }
    return true;
}

// Parses the knobs table at the top of the stack into `out` (stack left
// unchanged). Like the zone parse, every bound is a HARD rejection: a
// clipped line or a snapped-to-FAIR deal would quietly re-shape a wizard
// the campaign author counted on, and the caller's fallback (the defaults)
// is a page that still works.
bool parse_campaign_match_knobs(lua_State* L, ScriptHost::Impl& impl,
                                CampaignMatchKnobs& out)
{
    // rule: match_knobs answers a table with a closed key vocabulary; a non-string key or an
    //   unknown key (with a did-you-mean hint) refuses the whole answer and the wizard falls back
    //   to its defaults.
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    if (!lua_istable(L, -1))
        return malformed_knobs(impl, std::string("returned a ") +
                                         luaL_typename(L, -1));
    static const char* const kKnobKeys[] = {"teams", "fill",  "score",
                                            "time",  "lines", "arena_page",
                                            "deal"};
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        lua_pop(L, 1);  // value; the key stays for the next iteration
        // rule: match_knobs answers a table with a closed key vocabulary.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        if (lua_type(L, -1) != LUA_TSTRING) {
            const std::string what =
                std::string("got a ") + luaL_typename(L, -1) + " key";
            lua_pop(L, 1);
            return malformed_knobs(impl, what);
        }
        const std::string key = lua_tostring(L, -1);
        bool known = false;
        // rule: match_knobs answers a table with a closed key vocabulary.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        for (const char* n : kKnobKeys)
            known = known || key == n;
        // rule: match_knobs answers a table with a closed key vocabulary.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        if (!known) {
            const std::vector<const char*> names(std::begin(kKnobKeys),
                                                 std::end(kKnobKeys));
            const std::string hint = did_you_mean(key, names);
            lua_pop(L, 1);
            return malformed_knobs(impl, "unknown key '" + key + "'" + hint);
        }
    }

    const struct {
        const char* key;
        bool* field;
    } flags[] = {{"teams", &out.teams},
                 {"score", &out.score},
                 {"time", &out.time}};
    // rule: The wizard's TEAMS, SCORE and TIME rows are each shown or hidden by an optional
    //   boolean; a non-boolean refuses the answer.
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
    // tests: MatchSetupSessionTest.match_knobs_shape_the_steps
    for (const auto& flag : flags) {
        lua_pushstring(L, flag.key);
        lua_rawget(L, -2);
        // rule: The wizard's TEAMS, SCORE and TIME rows are each shown or hidden by an optional
        //   boolean; a non-boolean refuses the answer.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
        // tests: MatchSetupSessionTest.match_knobs_shape_the_steps
        if (!lua_isnil(L, -1)) {
            // rule: The wizard's TEAMS, SCORE and TIME rows are each shown or hidden by an optional
            //   boolean; a non-boolean refuses the answer.
            // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
            // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
            // tests: MatchSetupSessionTest.match_knobs_shape_the_steps
            if (!lua_isboolean(L, -1)) {
                lua_pop(L, 1);
                return malformed_knobs(impl, std::string("'") + flag.key +
                                                 "' is not a boolean");
            }
            *flag.field = lua_toboolean(L, -1) != 0;
        }
        lua_pop(L, 1);
    }

    lua_pushstring(L, "fill");
    lua_rawget(L, -2);
    // rule: The FILL row is shown as the macro wheel, the band picker, or removed (fill = false);
    //   fill = true and any other word are refused.
    // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
    if (!lua_isnil(L, -1)) {
        bool understood = false;
        // rule: The FILL row is shown as the macro wheel, the band picker, or removed (fill =
        //   false); fill = true and any other word are refused.
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
        if (lua_type(L, -1) == LUA_TSTRING) {
            const std::string word = lua_tostring(L, -1);
            // rule: The FILL row is shown as the macro wheel, the band picker, or removed (fill =
            //   false); fill = true and any other word are refused.
            // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
            // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
            // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
            if (word == "macro") {
                out.fill = CampaignFillKnob::Macro;
                understood = true;
            } else if (word == "band") {
                out.fill = CampaignFillKnob::Band;
                understood = true;
            }
        } else if (lua_isboolean(L, -1) && lua_toboolean(L, -1) == 0) {
            // `fill = false` is the whole row gone; `fill = true` is not a
            // spelling of anything, so it is refused with the strings.
            out.fill = CampaignFillKnob::Off;
            understood = true;
        }
        // rule: The FILL row is shown as the macro wheel, the band picker, or removed (fill =
        //   false); fill = true and any other word are refused.
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
        if (!understood) {
            lua_pop(L, 1);
            return malformed_knobs(
                impl, "'fill' is not \"macro\", \"band\" or false");
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "lines");
    lua_rawget(L, -2);
    // rule: The knobs' lines key is optional; when present it must be a table and pass the lines
    //   rule (above).
    // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    // tests: CampaignHooksTest.match_knobs_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
    if (!lua_isnil(L, -1)) {
        // rule: The knobs' lines key is optional; when present it must be a table and pass the
        //   lines rule (above).
        // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_alone_is_a_legal_registration
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return malformed_knobs(impl,
                                   "'lines' is not an array of strings");
        }
        // rule: The knobs' lines key is optional; when present it must be a table and pass the
        //   lines rule (above).
        // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_alone_is_a_legal_registration
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        if (!parse_knobs_lines(L, impl, out)) {
            lua_pop(L, 1);
            return false;
        }
    }
    lua_pop(L, 1);

    lua_pushstring(L, "arena_page");
    lua_rawget(L, -2);
    // rule: The campaign may name the arena page the wizard opens on as a string ('' means none); a
    //   non-string refuses.
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
    if (!lua_isnil(L, -1)) {
        // rule: The campaign may name the arena page the wizard opens on as a string ('' means
        //   none); a non-string refuses.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        if (lua_type(L, -1) != LUA_TSTRING) {
            lua_pop(L, 1);
            return malformed_knobs(impl, "'arena_page' is not a string");
        }
        std::size_t len = 0;
        const char* text = lua_tolstring(L, -1, &len);
        out.arena_page.assign(text, len);
    }
    lua_pop(L, 1);

    lua_pushstring(L, "deal");
    lua_rawget(L, -2);
    // rule: The campaign may fix the bot deal to one wheel rung (see 3531); absent keeps FAIR.
    // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
    if (!lua_isnil(L, -1)) {
        std::int16_t code = og::sim::kFillFair;
        const bool word = lua_type(L, -1) == LUA_TSTRING &&
                          parse_fill_word(lua_tostring(L, -1), code);
        // rule: The campaign may fix the bot deal to one wheel rung (see 3531); absent keeps FAIR.
        // tests: CampaignHooksTest.match_knobs_accepts_every_wheel_word_and_fill_spelling
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        // tests: CampaignHooksTest.match_knobs_empty_table_answers_every_default
        if (!word) {
            lua_pop(L, 1);
            return malformed_knobs(impl, "'deal' is not a wheel word (none, "
                                         "weak, fair, strong, brutal)");
        }
        out.deal_fill = code;
    }
    lua_pop(L, 1);
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
    // plumbing: `if (st == nullptr) return false/{}` in each public campaign query: the 'no book /
    //   conflicted book' arm of above surfaced per entry point (asserted there and
    //   in action_without_registration_answers_false, no_registration_means_no_picker).
    if (st == nullptr)
        return {};
    return st->campaign_vars;
}

bool campaign_picker_page(const std::string& page_id, CampaignPage& out)
{
    VmState* st = campaign_vm_state();
    // plumbing: `if (st == nullptr) return false/{}` in each public campaign query: the 'no book /
    //   conflicted book' arm of above surfaced per entry point (asserted there and
    //   in action_without_registration_answers_false, no_registration_means_no_picker).
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    // rule: A campaign book serves only the hooks it registered.
    // tests: CampaignHooksTest.zone_unregistered_without_base_camp_hook
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_absent_answers_defaults_and_false
    // tests: CampaignHooksTest.action_without_registration_answers_false
    if (!push_campaign_hook_fn(L, st, kCampaignMenuSlot))
        return false;
    CampaignDispatchScope scope(*st, impl);
    // plumbing: Per-dispatcher `if (!scope.armed())` after a CampaignDispatchScope: the
    //   nested-dispatch refusal arm of above (pop the pushed fn, answer false).
    if (!scope.armed()) {
        lua_pop(L, 1);
        return false;
    }
    const std::uint64_t gen = push_dispatch_gen(L);
    lua_pushlstring(L, page_id.data(), page_id.size());
    bool ok = impl.protected_call("campaign:picker_menu", 1, 1);
    // rule: A picker page is served only when the hook ran without error AND the page parsed; an
    //   erroring hook means no scripted picker for that dispatch (the stock picker renders).
    // tests: CampaignHooksTest.malformed_pages_answer_no_scripted_picker
    // tests: CampaignHooksTest.page_parse_happy_path
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
    // plumbing: `if (st == nullptr) return false/{}` in each public campaign query: the 'no book /
    //   conflicted book' arm of above surfaced per entry point (asserted there and
    //   in action_without_registration_answers_false, no_registration_means_no_picker).
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    // rule: A campaign book serves only the hooks it registered.
    // tests: CampaignHooksTest.zone_unregistered_without_base_camp_hook
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_absent_answers_defaults_and_false
    // tests: CampaignHooksTest.action_without_registration_answers_false
    if (!push_campaign_hook_fn(L, st, kCampaignActionSlot))
        return false;
    CampaignDispatchScope scope(*st, impl);
    // plumbing: Per-dispatcher `if (!scope.armed())` after a CampaignDispatchScope: the
    //   nested-dispatch refusal arm of above (pop the pushed fn, answer false).
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
    // rule: A picker action always counts as dispatched (a spend it applied sticks, so the page
    //   refreshes): an error answers ok=false; a table result may carry a toast message and a
    //   scenario level in 0..32767; anything else carries no level (-1) and no toast.
    // tests: CampaignHooksTest.action_dispatch_and_message
    // tests: CampaignHooksTest.action_result_level_parse_and_bounds
    if (impl.protected_call("campaign:picker_action", 1, 1)) {
        out.ok = true;
        // nil (or any non-table) = no toast; a table's optional `message`
        // string is the toast. Raw access: untrusted result value.
        // rule: A picker action always counts as dispatched (a spend it applied sticks, so the page
        //   refreshes).
        // tests: CampaignHooksTest.action_dispatch_and_message
        // tests: CampaignHooksTest.action_result_level_parse_and_bounds
        if (lua_istable(L, -1)) {
            bool present = false;
            read_string_field(L, -1, "message", out.message, present);
            // Optional `level` (D3): the scenario the action answers with.
            // Read at 64-bit width so an oversized value cannot wrap into
            // the loader's id range; anything outside 0..32767 — or a
            // non-integer — stays the "no level carried" default (-1).
            lua_pushstring(L, "level");
            lua_rawget(L, -2);
            // rule: A picker action always counts as dispatched (a spend it applied sticks, so the
            //   page refreshes).
            // tests: CampaignHooksTest.action_dispatch_and_message
            // tests: CampaignHooksTest.action_result_level_parse_and_bounds
            if (lua_isinteger(L, -1)) {
                const lua_Integer level = lua_tointeger(L, -1);
                // rule: A picker action always counts as dispatched (a spend it applied sticks, so
                //   the page refreshes).
                // tests: CampaignHooksTest.action_dispatch_and_message
                // tests: CampaignHooksTest.action_result_level_parse_and_bounds
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
    // rule: The lineup band shows power prices when the active book registered lineup.power, or
    //   else when the shipped default pricer exists (C5).
    // tests: CampaignHooksTest.lineup_registers_and_prices_a_fighter
    // tests: CampaignHooksTest.no_lineup_hook_means_no_lineup
    // tests: CampaignHooksTest.default_lineup_prices_with_no_campaign_book
    if (st != nullptr && st->campaign_lineup_registered)
        return true;
    // C5: no book lineup — the shipped pack's default prices the bands.
    return default_lineup_vm_state() != nullptr;
}

bool campaign_fighter_power(const LineupPowerRow& row, long long& out)
{
    // The book first, the shipped default second (C5). A campaign that
    // registers `lineup.power` overrides the default; a campaign with no
    // lineup table — and a campaign whose book is conflicted, which is no
    // book at all — falls through to it.
    VmState* st = campaign_vm_state();
    const char* who = "campaign";
    bool pushed = false;
    // rule: A fighter's lineup price comes from the book's lineup.power first, and falls through to
    //   the shipped default when the book has no lineup or is conflicted; with neither there is no
    //   price and the caller's value stands.
    // tests: CampaignHooksTest.a_campaign_lineup_overrides_the_default
    // tests: CampaignHooksTest.a_book_with_no_lineup_falls_back_to_the_default
    // tests: CampaignHooksTest.no_book_and_no_default_is_no_metric
    if (st != nullptr) {
        lua_State* book_L = st->owner->host().impl().L;
        pushed = push_campaign_hook_fn(book_L, st, kCampaignLineupPowerSlot);
    }
    if (!pushed) {
        st = default_lineup_vm_state();
        // rule: A fighter's lineup price comes from the book's lineup.power first, and falls
        //   through to the shipped default when the book has no lineup or is conflicted.
        // tests: CampaignHooksTest.a_campaign_lineup_overrides_the_default
        // tests: CampaignHooksTest.a_book_with_no_lineup_falls_back_to_the_default
        // tests: CampaignHooksTest.no_book_and_no_default_is_no_metric
        if (st == nullptr)
            return false;
        lua_State* default_L = st->owner->host().impl().L;
        // Always a function: og.register_default_lineup refs the value only
        // after checking lua_isfunction, and nothing else writes the ref.
        lua_rawgeti(default_L, LUA_REGISTRYINDEX,
                    st->default_lineup_power_ref);
        who = "default";
    }
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    CampaignDispatchScope scope(*st, impl);
    // plumbing: Per-dispatcher `if (!scope.armed())` after a CampaignDispatchScope: the
    //   nested-dispatch refusal arm of above (pop the pushed fn, answer false).
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
    // "campaign:lineup_power" / "default:lineup_power" — an author reading
    // the error log has to be told WHICH pricer refused.
    const std::string label = std::string(who) + ":lineup_power";
    // rule: A lineup price is a number the pricer actually computed: an integer is taken as is, a
    //   finite float inside int64 range truncates toward zero, and a string (even '42'), NaN, an
    //   infinity or an out-of-range float is no price (band shows --) with an error naming which
    //   pricer (campaign or default) refused; a pricer error is also no price.
    // tests: CampaignHooksTest.lineup_power_non_integer_answers
    // tests: CampaignHooksTest.lineup_power_that_errors_answers_false
    // tests: CampaignHooksTest.lineup_power_that_returns_a_non_number_answers_false
    // tests: CampaignHooksTest.default_lineup_power_non_number_names_the_default
    // tests: CampaignHooksTest.default_lineup_power_that_errors_answers_false
    // tests: SemScript3.a_pricer_error_is_recorded_under_the_pricer_that_raised_it
    if (impl.protected_call(label.c_str(), 1, 1)) {
        // A number the engine can hold, or nothing: a price it cannot read
        // is no price at all, and the band shows `--` rather than an
        // invented figure. lua_isnumber would accept the STRING "42"; a
        // price must be a number the book actually computed. An integer
        // is taken as is; a float must be finite and inside the int64
        // range (then truncated toward zero) — a static_cast of NaN or an
        // infinity is undefined and used to render INT64_MIN on the band.
        std::string refusal;
        // rule: A lineup price is a number the pricer actually computed.
        // tests: CampaignHooksTest.lineup_power_non_integer_answers
        // tests: CampaignHooksTest.lineup_power_that_errors_answers_false
        // tests: CampaignHooksTest.lineup_power_that_returns_a_non_number_answers_false
        // tests: CampaignHooksTest.default_lineup_power_non_number_names_the_default
        // tests: CampaignHooksTest.default_lineup_power_that_errors_answers_false
        // tests: SemScript3.a_pricer_error_is_recorded_under_the_pricer_that_raised_it
        if (lua_type(L, -1) != LUA_TNUMBER) {
            refusal = std::string(who) + " lineup.power returned a " +
                      luaL_typename(L, -1) + ", not a number";
        } else if (lua_isinteger(L, -1)) {
            out = static_cast<long long>(lua_tointeger(L, -1));
            ok = true;
        } else {
            const lua_Number value = lua_tonumber(L, -1);
            // 2^63 as a double is exactly representable; the open bound on
            // the right keeps the truncation inside LLONG_MAX.
            constexpr lua_Number kInt64Span = 9223372036854775808.0;
            // rule: A lineup price is a number the pricer actually computed.
            // tests: CampaignHooksTest.lineup_power_non_integer_answers
            // tests: CampaignHooksTest.lineup_power_that_errors_answers_false
            // tests: CampaignHooksTest.lineup_power_that_returns_a_non_number_answers_false
            // tests: CampaignHooksTest.default_lineup_power_non_number_names_the_default
            // tests: CampaignHooksTest.default_lineup_power_that_errors_answers_false
            // tests: SemScript3.a_pricer_error_is_recorded_under_the_pricer_that_raised_it
            if (std::isfinite(value) && value >= -kInt64Span &&
                value < kInt64Span) {
                out = static_cast<long long>(std::trunc(value));
                ok = true;
            } else {
                refusal = std::format(
                    "{} lineup.power returned {}, not a finite integer",
                    who, value);
            }
        }
        // rule: A lineup price is a number the pricer actually computed.
        // tests: CampaignHooksTest.lineup_power_non_integer_answers
        // tests: CampaignHooksTest.lineup_power_that_errors_answers_false
        // tests: CampaignHooksTest.lineup_power_that_returns_a_non_number_answers_false
        // tests: CampaignHooksTest.default_lineup_power_non_number_names_the_default
        // tests: CampaignHooksTest.default_lineup_power_that_errors_answers_false
        // tests: SemScript3.a_pricer_error_is_recorded_under_the_pricer_that_raised_it
        if (!ok) {
            LogError("class pack: {}\n", refusal);
            impl.record_error(label.c_str(), refusal.c_str());
        }
        lua_pop(L, 1);
    }
    pop_dispatch_gen(L, gen);
    return ok;
}

bool campaign_zone_registered()
{
    VmState* st = campaign_vm_state();
    // plumbing: `if (st == nullptr) return false/{}` in each public campaign query: the 'no book /
    //   conflicted book' arm of above surfaced per entry point (asserted there and
    //   in action_without_registration_answers_false, no_registration_means_no_picker).
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    // rule: A campaign book serves only the hooks it registered.
    // tests: CampaignHooksTest.zone_unregistered_without_base_camp_hook
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_absent_answers_defaults_and_false
    // tests: CampaignHooksTest.action_without_registration_answers_false
    if (!push_campaign_hook_fn(L, st, kCampaignZoneSlot))
        return false;
    lua_pop(L, 1);
    return true;
}

bool campaign_zone(CampaignZone& out)
{
    VmState* st = campaign_vm_state();
    // plumbing: `if (st == nullptr) return false/{}` in each public campaign query: the 'no book /
    //   conflicted book' arm of above surfaced per entry point (asserted there and
    //   in action_without_registration_answers_false, no_registration_means_no_picker).
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    // rule: A campaign book serves only the hooks it registered.
    // tests: CampaignHooksTest.zone_unregistered_without_base_camp_hook
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_absent_answers_defaults_and_false
    // tests: CampaignHooksTest.action_without_registration_answers_false
    if (!push_campaign_hook_fn(L, st, kCampaignZoneSlot))
        return false;
    CampaignDispatchScope scope(*st, impl);
    // plumbing: Per-dispatcher `if (!scope.armed())` after a CampaignDispatchScope: the
    //   nested-dispatch refusal arm of above (pop the pushed fn, answer false).
    if (!scope.armed()) {
        lua_pop(L, 1);
        return false;
    }
    const std::uint64_t gen = push_dispatch_gen(L);
    bool ok = impl.protected_call("campaign:base_camp", 0, 1);
    // rule: The scripted Base Camp zone renders only when base_camp ran cleanly and parsed;
    //   otherwise the default zone renders.
    // tests: CampaignHooksTest.zone_parse_happy_path
    // tests: CampaignHooksTest.zone_bounds_and_malformed_shapes_fall_to_default
    // tests: CampaignHooksTest.zone_dispatch_is_fenced_and_brackets_cleanly
    if (ok) {
        out = CampaignZone{};
        ok = parse_campaign_zone(L, impl, out);
        lua_pop(L, 1);
    }
    pop_dispatch_gen(L, gen);
    return ok;
}

bool campaign_match_knobs_registered()
{
    VmState* st = campaign_vm_state();
    // plumbing: `if (st == nullptr) return false/{}` in each public campaign query: the 'no book /
    //   conflicted book' arm of above surfaced per entry point (asserted there and
    //   in action_without_registration_answers_false, no_registration_means_no_picker).
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    // rule: A campaign book serves only the hooks it registered.
    // tests: CampaignHooksTest.zone_unregistered_without_base_camp_hook
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_absent_answers_defaults_and_false
    // tests: CampaignHooksTest.action_without_registration_answers_false
    if (!push_campaign_hook_fn(L, st, kCampaignMatchKnobsSlot))
        return false;
    lua_pop(L, 1);
    return true;
}

bool campaign_match_knobs(CampaignMatchKnobs& out)
{
    // The defaults are the answer on every refusal below, so they land
    // BEFORE the first early return: a caller that keeps one struct across
    // navigations must never read the previous campaign's knobs back.
    out = CampaignMatchKnobs{};
    VmState* st = campaign_vm_state();
    // plumbing: `if (st == nullptr) return false/{}` in each public campaign query: the 'no book /
    //   conflicted book' arm of above surfaced per entry point (asserted there and
    //   in action_without_registration_answers_false, no_registration_means_no_picker).
    if (st == nullptr)
        return false;
    ScriptHost::Impl& impl = st->owner->host().impl();
    lua_State* L = impl.L;
    // rule: A campaign book serves only the hooks it registered.
    // tests: CampaignHooksTest.zone_unregistered_without_base_camp_hook
    // tests: CampaignHooksTest.base_camp_alone_is_a_legal_registration
    // tests: CampaignHooksTest.match_knobs_absent_answers_defaults_and_false
    // tests: CampaignHooksTest.action_without_registration_answers_false
    if (!push_campaign_hook_fn(L, st, kCampaignMatchKnobsSlot))
        return false;
    CampaignDispatchScope scope(*st, impl);
    // plumbing: Per-dispatcher `if (!scope.armed())` after a CampaignDispatchScope: the
    //   nested-dispatch refusal arm of above (pop the pushed fn, answer false).
    if (!scope.armed()) {
        lua_pop(L, 1);
        return false;
    }
    const std::uint64_t gen = push_dispatch_gen(L);
    bool ok = impl.protected_call("campaign:match_knobs", 0, 1);
    // rule: On every refusal (no book, no hook, nested, hook error, malformed answer) the knobs
    //   answer is the defaults, set before the first early return, so a caller never reads a
    //   previous campaign's knobs.
    // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
    if (ok) {
        ok = parse_campaign_match_knobs(L, impl, out);
        // rule: On every refusal (no book, no hook, nested, hook error, malformed answer) the knobs
        //   answer is the defaults, set before the first early return, so a caller never reads a
        //   previous campaign's knobs.
        // tests: CampaignHooksTest.match_knobs_refuses_mis_shaped_answers_by_name
        if (!ok)
            out = CampaignMatchKnobs{};
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
    // plumbing: og_function_names: lua_next over the sandbox og table collecting function-valued
    //   string keys, sorted; used by the fence-walk test (test_campaign_hooks.cpp
    //   fence_walk_everything_outside_the_allowlist_errors) to enumerate bindings.
    while (lua_next(L, -2) != 0) {
        // plumbing: og_function_names: lua_next over the sandbox og table collecting
        //   function-valued string keys, sorted; used by the fence-walk test
        //   (test_campaign_hooks.cpp fence_walk_everything_outside_the_allowlist_errors) to
        //   enumerate bindings.
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
