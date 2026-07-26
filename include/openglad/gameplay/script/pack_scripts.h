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

}  // namespace og::script
