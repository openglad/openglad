/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Process-global registry of pack behavior scripts. The resources layer (or
// tests) pushes script sources here after mounting packs; every new
// per-world ScriptHost replays them in registration order, which is required
// to be deterministic: callers register packs in pack-id-lexicographic
// order, scripts within a pack in path-lexicographic order.

#include <string>
#include <vector>

namespace og::script {

struct PackScript {
    std::string pack_id;     // e.g. "org.openglad.core"
    std::string chunk_name;  // e.g. "core/scripts/soldier.lua" (diagnostics)
    std::string source;      // Lua source text
};

// Append a script. Duplicate (pack_id, chunk_name) pairs replace the prior
// entry in place (same position), so a re-mounted pack keeps its load order.
void register_pack_script(PackScript script);

// Remove every script belonging to pack_id (pack unmounted).
void unregister_pack_scripts(const std::string& pack_id);

// All registered scripts in replay order.
const std::vector<PackScript>& pack_scripts();

// Drop everything (tests; full remount).
void clear_pack_scripts();

// Monotonic counter bumped by every mutation above. Long-lived hosts (the
// picker-side UI host) compare it to decide when to rebuild.
unsigned pack_scripts_generation();

// ---------------------------------------------------------------------------
// Pack lib modules (og.use)
// ---------------------------------------------------------------------------
//
// A pack may ship shared helpers as packs/<id>/lib/<name>.lua. The resources
// layer enumerates them at pack install (pack-id-lexicographic, then
// filename-lexicographic — the same deterministic order scripts use) and
// registers them here; every new per-world VM loads each module ONCE, in
// registration order, BEFORE any pack script runs, and memoizes the frozen
// export `og.use("<name>")` hands back. Modules run as text-only chunks in a
// fresh isolated environment under the normal instruction/memory budgets and
// must `return` their exports. og.use is pack-relative (a chunk of pack P
// resolves P's modules only) and available only while a pack chunk loads —
// bind modules to locals at load time. Lib modules must be PURE: one table
// of functions and constants, no chunk-level mutable state (cookbook R6 and
// docs/lua-style.md S4). The shallow export freeze cannot detect mutable
// closure upvalues.

struct PackLibModule {
    std::string pack_id;     // e.g. "org.openglad.core"
    std::string name;        // og.use key: the file stem, e.g. "living_common"
    std::string chunk_name;  // e.g. "packs/core/lib/living_common.lua"
    std::string source;      // Lua source text
};

// Append a module. A duplicate (pack_id, name) replaces the prior entry in
// place, mirroring register_pack_script.
void register_pack_lib_module(PackLibModule module);

// Remove every module belonging to pack_id (pack unmounted).
void unregister_pack_lib_modules(const std::string& pack_id);

// All registered modules in load order.
const std::vector<PackLibModule>& pack_lib_modules();

// Drop everything (tests; full remount).
void clear_pack_lib_modules();

// Monotonic counter over module mutations. WorldScripts folds it into its
// build generation so a module-only change still rebuilds long-lived hosts.
unsigned pack_lib_generation();

// pack_scripts_generation() + pack_lib_generation() — the ONE value
// WorldScripts::built_generation() stores and every staleness check must
// compare against (both addends are monotonic, so the sum is too). A
// consumer comparing built_generation() to pack_scripts_generation() alone
// would see a permanent mismatch once any lib mutation happened and rebuild
// its VM on every dispatch, wiping logs and hook state each time.
unsigned pack_scripts_build_generation();

}  // namespace og::script
