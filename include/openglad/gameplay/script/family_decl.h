/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The DECLARATION pass: `og.family` / `og.anims` / `og.pack` evaluated for
// their DATA, in a throwaway VM, once per content change.
//
// One families/*.lua chunk serves two contexts, and this header owns the
// first of them:
//
//   DECLARE — a fresh, tight VM evaluates one pack's families/ chunks (and,
//     through og.use, that pack's lib/ modules). og.family harvests into the
//     interchange structs the installer consumes; the hook and
//     cast functions it carries are type-checked and thrown away with the
//     VM. Nothing binds, nothing installs: the caller hands the harvest to
//     the ordinary installer. World-facing og.* bindings all error, so a
//     declaration cannot read the world or draw from the RNG.
//
//   BIND — every ordinary WorldScripts replays the same chunks, where
//     og.family binds hooks and specials casts against the ALREADY INSTALLED
//     descriptors (joined by declared special id) and touches no registry.
//
// Install-once for data, per-VM for behavior. See
// docs/lua-classpacks-design.md.

#include <openglad/core/order.h>

#include <string>

namespace og::data {
struct ClasspackData;
}

namespace og::script {

// og.api.version — the declaration vocabulary's revision. A pack that must
// straddle engine releases gates a key on this rather than hoping an
// unknown key is skipped (v3 made unknown keys load errors; see V5 in the
// format spec).
inline constexpr int kPackApiVersion = 3;

struct DeclareResult {
    bool ok = false;
    // First recorded load error, for the caller's rejection message. Empty
    // when ok.
    std::string error;
};

// Runs the declaration pass over one pack's registered families/*.lua
// chunks, appending everything they declare to `out` in chunk order. Any
// error — compile failure, runtime error, validation error, blown budget —
// answers {false, message} and REJECTS the whole pack, exactly as one
// unusable family file always has.
//
// A pack with no family chunks — a scripts-only or art-only pack — answers
// {true, ""} without building a VM, so it never pays for this.
DeclareResult declare_pack_families(const std::string& pack_id,
                                    og::data::ClasspackData& out);

// ---------------------------------------------------------------------------
// Packs whose declaration pass FAILED
// ---------------------------------------------------------------------------
//
// A rejected pack installs nothing, but its family chunks are still in the
// chunk registry (registration precedes install), so the bind pass would
// replay declarations for families that do not exist and report a second,
// less useful error in every VM it ever builds. The installer names the
// casualties instead; the bind replay skips them.
void clear_failed_pack_declarations();
void note_failed_pack_declaration(const std::string& pack_id);
bool pack_declaration_failed(const std::string& pack_id);

// ---------------------------------------------------------------------------
// Which registry slots a Lua declaration owns
// ---------------------------------------------------------------------------
//
// Written by the installer at the end of every install pass, read by the
// bind pass. Two rules key off it:
//
//   * a castable special that no cast, do_special or default handles is a
//     WARNING for a slot that arrived through install_classpack_data
//     rather than from a declaration, and a pack LOAD ERROR for a slot an
//     og.family declared — in one file there is no excuse
//     (format spec V3). The check runs after the whole bind replay, so a
//     cross-file og.register_hooks override still counts as a handler.
//   * diagnostics name the pack that declared the slot.
//
// Empty until a pack actually declares families in Lua, so a process whose
// registries were filled some other way never carries an entry.
void clear_lua_declared_families();
void note_lua_declared_family(Order order, int family_id,
                              const std::string& pack_id);
// The declaring pack's id, or nullptr when no Lua declaration owns the slot.
const std::string* lua_declared_family_pack(Order order, int family_id);

}  // namespace og::script
