/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Shared internals of the scripting layer, private to src/gameplay/script/.

#include "script_host_impl.h"

#include <openglad/core/order.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>

#include <cstdint>
#include <string>
#include <vector>

class walker;
class guy;
struct FamilyDescriptor;

namespace og::data {
struct ClasspackData;
}

namespace og::script {

class WorldScripts;

inline constexpr const char* kWalkerMeta = "og.walker";
inline constexpr const char* kGuyMeta = "og.guy";
// The deferred family reference og.family_id answers during the declaration
// pass — see push_family_ref below.
inline constexpr const char* kFamilyRefMeta = "og.familyref";

// What a VM is FOR. The same pack chunk runs in both, and og.family is the
// one binding whose meaning depends on which.
enum class VmMode : std::uint8_t {
    Bind,     // an ordinary world/UI VM: hooks and casts bind, data is checked
    Declare,  // the throwaway install-time VM: data is harvested, nothing binds
};

// Which directory a chunk came from. og.family is legal only in families/,
// which is the installer's execution scope: a declaration in scripts/ would
// bind behavior per VM while its data half never installed — a split-brain
// family, and the one failure mode this whole two-context design exists to
// make impossible.
enum class ChunkKind : std::uint8_t { None, Lib, Family, Script };

struct WalkerHandle {
    walker* raw;
    std::uint32_t entity_id;  // 0 = untracked (dispatch-scoped validity)
    std::uint64_t gen;        // dispatch generation for untracked handles
};

struct GuyHandle {
    guy* raw;
    std::uint64_t gen;  // always dispatch-scoped
};

// Per-VM state reachable from lua callbacks via the "og.vmstate" registry
// lightuserdata. Owned by WorldScripts (stable address).
struct VmState {
    // The world scripts that own this VM, or null in the declaration VM —
    // which has no hooks to note and no world to serve, and is exactly why
    // the field is nullable.
    WorldScripts* owner = nullptr;
    // The host this VM runs in. Set for BOTH kinds: og.use and the error
    // records need a host, and the declaration VM has one without having a
    // WorldScripts.
    ScriptHost::Impl* host_impl = nullptr;
    std::uint64_t dispatch_gen = 0;
    // Generations of every dispatch currently ON THE STACK. A hook may
    // re-enter dispatch (walker:special() runs do_special, a death hook
    // spawns something that dispatches, ...), which mints a newer
    // generation; the outer frame's handles must stay usable while its
    // Lua code is still running, so validity is "any live generation",
    // not "the newest one". Pushed by HookFrame::begin and the level
    // dispatchers, popped when the frame unwinds.
    std::vector<std::uint64_t> live_gens;
    int hooks_ref = -1;           // table: order/family key → hook table
    int walker_methods_ref = -1;  // __index table for walker handles
    int level_hooks_ref = -1;     // table: kind*65536 + (level+1) → fn
    int entity_hooks_ref = -1;    // table: entity_id → { kind → fn }
    std::uint32_t level_hook_kinds = 0;  // bitmask of registered kinds
    bool has_entity_hooks = false;
    // og.use module system. lib_exports holds the frozen export of every
    // loaded module ("<pack>/<name>" keys);
    // lib_status tracks in-flight/broken loads ("loading" detects cycles,
    // "failed" makes later og.use calls error deterministically instead of
    // re-running a broken chunk).
    int lib_exports_ref = -1;     // table: "pack/name" → frozen export
    int lib_status_ref = -1;      // table: "pack/name" → "loading"|"failed"
    // The pack whose chunk (script or lib module) is CURRENTLY loading.
    // Empty outside pack load — og.use is pack-relative and load-time-only,
    // so this is both its resolution root and its availability gate.
    std::string current_pack;
    // What this VM is for, and what is executing in it right now.
    VmMode mode = VmMode::Bind;
    ChunkKind current_chunk = ChunkKind::None;
    // TRUE for the whole top-level execution of any pack chunk, in EVERY
    // VM. While it is set, world-facing og.* bindings error.
    //
    // This closes a real hole rather than adding ceremony: og.rand used to
    // fail at chunk load only by accident, because current_game is null
    // while the boot-time VM is built — but GameWorld::scripts() rebuilds a
    // VM at the first dispatch after a mount change, with a live world, and
    // there a top-level og.rand would have SUCCEEDED and pulled the sim's
    // stream out from under the other peers. "No world at declaration time"
    // becomes a property of chunk-load time everywhere instead of a
    // property of which VM happens to have no world.
    bool loading = false;
    // Declaration pass only: where og.family/og.anims/og.pack harvest to.
    og::data::ClasspackData* harvest = nullptr;
    // og.tuning per-VM cache of frozen views, invalidated when the process
    // store's generation moves (pack remount reinstalls tuning).
    int tuning_cache_ref = -1;    // table: order*4096+family → frozen view
    std::uint64_t tuning_cache_gen = 0;
    // og.register_campaign_hooks storage — per VM, load-time, one campaign
    // picker per VM. A SECOND registration never raises (that would kill
    // the chunk's unrelated level hooks); it records the conflict here and
    // the query side answers "no scripted picker", reporting once.
    int campaign_hooks_ref = -1;  // table: 1 = picker_menu, 2 = picker_action
    std::vector<std::string> campaign_vars;  // sim-visible names, in order
    bool campaign_registered = false;
    std::string campaign_source;            // registering chunk (diagnostics)
    std::string campaign_conflict_source;   // second registrant; ""=no conflict
    bool campaign_conflict_reported = false;  // once-latch for the record
    // TRUE while a campaign hook dispatch is on the stack. The world API,
    // og.rand and the three registrars error while it is set; only the
    // og.campaign_* bindings and the pure helpers stay legal
    // (docs/campaign-scripting-design.md, "The campaign-dispatch fence").
    bool campaign_dispatch = false;
};

// Level-script hook kinds (bit positions in level_hook_kinds and the kind
// component of level_hooks_ref keys).
enum class LevelHook : int {
    Load = 0,
    Tick = 1,
    EntityDeath = 2,
    EntitySpawn = 3,
    // Scripted-mode (TYPE_SCRIPTED) hooks:
    ModeInit = 4,     // on_mode_init(level) — once, at activation
    ModeTick = 5,     // on_mode_tick(level, tick) — post-act, every tick
    Damage = 6,       // on_damage(target, attacker, amount) -> number|false|nil
    Respawn = 7,      // on_respawn(ent) — engine revive -> Lua placement
};

VmState* get_vm_state(lua_State* L);

// Resolve a walker handle at stack idx. Prefers the entity-id index of the
// current world; falls back to the raw pointer when the handle was minted
// during the current dispatch (dying entities). Raises when required and
// unresolvable.
walker* resolve_walker(lua_State* L, int idx, bool required);

// nil-tolerant variant: nil → nullptr, otherwise resolve (required).
walker* resolve_walker_or_nil(lua_State* L, int idx);

// Resolve a guy handle (dispatch-scoped only).
guy* resolve_guy(lua_State* L, int idx, bool required);

void push_walker_handle(lua_State* L, walker* w, std::uint64_t gen);

// Current dispatch generation (for minting handles outside HookFrame).
std::uint64_t current_dispatch_gen(lua_State* L);

// Opens a dispatch generation (returns it) and closes it again. Every site
// that mints walker handles for a Lua call must bracket the call with these
// so nested dispatches cannot invalidate an outer frame's handles.
std::uint64_t push_dispatch_gen(lua_State* L);
void pop_dispatch_gen(lua_State* L, std::uint64_t gen);

// Replaces the TABLE at the top of the stack with a read-only proxy view:
// an empty table whose metatable __index reaches the real data, whose
// __newindex raises, whose __len forwards (so array-shaped exports keep #),
// and whose metatable is fenced. Reads are live, writes error — the freeze
// og.use exports and og.tuning views share. Shallow by design: nested
// mutable state inside an export is an R6 violation the lib lint owns, not
// something the proxy can absorb. Non-table values are left untouched
// (they are immutable in Lua already). Known limit, documented rather than
// defended: rawset(proxy, ...) can still shadow entries — the sandbox keeps
// rawset — which only lets a pack sabotage its own view.
void push_frozen_view_of_top(lua_State* L);

// Installs entity/world/constants bindings into the VM (walker method table
// + og.* API). Called once per VM by install_vm_scaffolding.
void install_entity_bindings(lua_State* L, VmState* st);

// Adds the og.* world API + og.C constants; expects the og table on top of
// the stack (leaves it there). Every world-facing entry goes in behind the
// load-time fence, which reads `st` straight out of a closure upvalue — a
// pointer dereference per call, because these sit on the hook hot path and
// a registry lookup per binding call would not.
void install_entity_bindings_into_og(lua_State* L, VmState* st);

// Wraps og[name] — a plain C function already on the og table at the top of
// the stack — in that same load-time fence. Exported so the entry points
// registered outside the kOgWorldFuncs table (og.rand) can share the one
// mechanism instead of open-coding a second check.
void fence_world_entry(lua_State* L, VmState* st, const char* name);

// Creates the walker/guy handle metatables in this VM (idempotent).
void ensure_handle_metatables(lua_State* L, VmState* st);

// Everything a VM needs before a pack chunk may run in it: the registry
// tables VmState refers to by ref, the handle metatables, the entity
// bindings and the whole og.* surface. BOTH VM kinds go through it, so the
// declaration pass and an ordinary world VM differ only in VmState::mode —
// which is the property the two-context design rests on.
void install_vm_scaffolding(ScriptHost::Impl& impl, VmState& st);

// ---------------------------------------------------------------------------
// Hook vocabulary + registration internals, shared with family_decl.cpp
// ---------------------------------------------------------------------------
//
// The tables themselves stay in world_scripts.cpp: they are the single
// source of truth scripts/modding/gen_api_stubs.py parses, and the stub
// generator finds them by name in that file.

struct HookName {
    const char* name;
    FamilyHook hook;
};

struct OrderInfo {
    const char* name;
    Order order;
    const HookName* hooks;
    size_t hook_count;
};

// The order named by an og.* argument ("living", "fx", "effect", ...), or
// nullptr.
const OrderInfo* find_order(const char* name);

// Key of one family's hook table inside the VM's hooks root.
lua_Integer hook_table_key(Order order, int family_id);

// The slot a declared special id names on an INSTALLED descriptor, or -1.
// The id join is what keeps a cast pointing at the special it was written
// for when a pack reorders or re-costs the list.
int special_slot_for_id(const FamilyDescriptor* fd, const char* id);

// The ids the family does declare — the other half of an unknown-id error.
std::string declared_special_ids(const FamilyDescriptor* fd);

// Last-registration-wins stays the rule; this makes the collision visible
// (operator warning + host error record + trace).
void report_duplicate_hook(lua_State* L, VmState* st, const char* order_str,
                           const char* family_str, const char* hook_name);

// The process-global campaign providers (campaign_hooks.cpp). Read by the
// og.campaign_* bindings only, under the campaign-dispatch fence.
const hooks::CampaignProviders& campaign_providers();

// Names the registered function on top of the stack for the coverage
// report. No-op with the recorder off; never disturbs the stack.
void coverage_declare_hook(lua_State* L, const std::string& label);

// ---------------------------------------------------------------------------
// og.family / og.anims / og.pack (family_decl.cpp)
// ---------------------------------------------------------------------------

// The deferred family reference og.family_id answers in the declaration
// pass. It cannot resolve there — a merged family file binds its neighbours
// at chunk top level (`assert(og.family_id("fx", "core:boomerang"))`), and
// the pass that PRODUCES the install cannot also read it back — so the
// token is truthy, satisfying the shipped assert idiom, and every other use
// raises. Cast bodies never run in that VM, so a correct pack never touches
// one; a top-level computation over a family byte is exactly the
// wire-derived data value a declaration must not contain, and it fails
// loudly.
void push_family_ref(lua_State* L);

// og.NIL — the PRESENT-null. A sparse declaration patches only the fields it
// spells, so leaving a key out means "keep whatever the slot had"; og.NIL is
// how a pack says "explicitly none". One process-wide light-userdata
// sentinel: identity comparison, no allocation, same value in every VM.
void push_og_nil(lua_State* L);
bool is_og_nil(lua_State* L, int idx);

// " (did you mean 'name'?)" for the nearest known spelling of `bad`, or the
// empty string when nothing is close. Half of a strict-key rule is refusing
// the typo; the other half is naming what the author meant (format spec V5).
// Shared with og.register_hooks, whose unknown hook names are the same kind
// of mistake.
std::string did_you_mean(const std::string& bad,
                         const std::vector<const char*>& known);

// Was `hook` of the family hook table at `family_idx` bound by an og.family
// declaration that has not been overridden yet? Consumes the mark, so the
// FIRST og.register_hooks over a declaration is the supported override seam
// and a second one is two scripts racing (which still reports). Returns
// false for a table with no marks at all, i.e. every pre-v3 registration.
bool take_declared_hook(lua_State* L, int family_idx, FamilyHook hook);

// og.family(order, declaration) — harvests in the declaration pass, binds
// hooks and specials casts in a bind VM. og.anims(name, set) and
// og.pack(header) harvest and no-op respectively. All three raise on any
// validation failure, which rejects the whole pack.
int og_family(lua_State* L);
int og_anims(lua_State* L);
int og_pack(lua_State* L);

}  // namespace og::script
