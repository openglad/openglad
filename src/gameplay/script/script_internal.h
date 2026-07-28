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

#include <cstdint>
#include <string>
#include <vector>

class walker;
class guy;

namespace og::script {

class WorldScripts;

inline constexpr const char* kWalkerMeta = "og.walker";
inline constexpr const char* kGuyMeta = "og.guy";

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
    WorldScripts* owner = nullptr;
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
    // og.use module system (quality plan Stage 1). lib_exports holds the
    // frozen export of every loaded module ("<pack>/<name>" keys);
    // lib_status tracks in-flight/broken loads ("loading" detects cycles,
    // "failed" makes later og.use calls error deterministically instead of
    // re-running a broken chunk).
    int lib_exports_ref = -1;     // table: "pack/name" → frozen export
    int lib_status_ref = -1;      // table: "pack/name" → "loading"|"failed"
    // The pack whose chunk (script or lib module) is CURRENTLY loading.
    // Empty outside pack load — og.use is pack-relative and load-time-only,
    // so this is both its resolution root and its availability gate.
    std::string current_pack;
    // og.tuning per-VM cache of frozen views, invalidated when the process
    // store's generation moves (pack remount reinstalls tuning).
    int tuning_cache_ref = -1;    // table: order*4096+family → frozen view
    std::uint64_t tuning_cache_gen = 0;
};

// Level-script hook kinds (bit positions in level_hook_kinds and the kind
// component of level_hooks_ref keys).
enum class LevelHook : int {
    Load = 0,
    Tick = 1,
    EntityDeath = 2,
    EntitySpawn = 3,
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
// + og.* API). Called once from the WorldScripts constructor.
void install_entity_bindings(lua_State* L, VmState* st);

// Adds the og.* world API + og.C constants; expects the og table on top of
// the stack (leaves it there).
void install_entity_bindings_into_og(lua_State* L);

}  // namespace og::script
