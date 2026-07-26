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
    int hooks_ref = -1;           // table: order/family key → hook table
    int walker_methods_ref = -1;  // __index table for walker handles
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

// Installs entity/world/constants bindings into the VM (walker method table
// + og.* API). Called once from the WorldScripts constructor.
void install_entity_bindings(lua_State* L, VmState* st);

// Adds the og.* world API + og.C constants; expects the og table on top of
// the stack (leaves it there).
void install_entity_bindings_into_og(lua_State* L);

}  // namespace og::script
