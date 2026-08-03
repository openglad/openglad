/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Class-pack discovery: finds behavior scripts under the mounted virtual
// path packs/<pack_id>/scripts/*.lua and registers them with the gameplay
// script registry in deterministic order (pack ids lexicographic, then
// script filenames lexicographic — PhysFS enumeration is sorted). Every
// peer that mounts the same packs replays the same scripts in the same
// order, which the deterministic sim requires.

namespace og::data {
struct ClasspackData;
}

namespace og::resources {

// Scan packs/ and (re)register all pack scripts. Returns the number of
// scripts registered. Safe to call again after mounts change.
int register_mounted_pack_scripts();

// Drop every registered pack script, rescan the mounted set, and
// reinstall class-pack data. The one call sites use when mounts
// CHANGE (campaign mount/unmount — campaign zips may embed packs/ that
// merge into the virtual tree). World VMs rebuild lazily on their next
// dispatch via the generation counter.
int refresh_pack_scripts();

// Scan packs/ for each pack's family declarations (same deterministic
// order as the scripts) and install the family descriptor DATA into the
// five gameplay registries. Per pack, every packs/<pack_id>/families/
// *.lua is evaluated in sorted filename order by the declaration pass
// (og::script::declare_pack_families) into one harvested set — so a
// later file re-declaring an already-declared WIRE slot overwrites just
// the data fields it spells, and — like any install — replaces that
// slot's tuning map whole. For each entry, the current descriptor is
// copied, only the fields the declaration spells are overwritten, and
// all C++ behavior callback pointers are preserved unchanged (behavior
// lives in pack Lua). wire_id pins the slot (core = legacy bytes);
// wire_id auto/absent assigns the first free id >= 21 in
// pack-id-lexicographic order — entries beyond a registry's capacity are
// skipped with a warning. Any chunk that fails to declare rejects that
// pack only. Returns the number of family entries installed.
//
// Lifetime: installed descriptor const char* fields point into a
// process-lifetime ClasspackStore that owns every harvested
// ClasspackData (deliberately never destroyed — registry descriptors may
// be read up to static teardown).
int install_classpacks();

// Installs one already-harvested classpack, moving it into the process-
// lifetime ClasspackStore first (same semantics as one iteration of
// install_classpacks, with a fresh auto-wire-id counter). Exposed for
// unit tests and for tools that build a ClasspackData in C++
// (tools/concept_mapgen). Returns the number of family entries installed.
int install_classpack_data(og::data::ClasspackData&& data);

// How much work the pack path has actually done this process.
//
// These exist to be ASSERTED ON. Pack install is on the mount path, and
// mounts happen in bursts nobody counts by eye — one campaign switch is four
// (unmount old, mount new, unmount new, mount old). A regression there is
// invisible to every gate the repo has: it is not a wrong value, not a
// changed byte, and not a Lua instruction, just a slower startup, and the
// only thing that ever noticed was a wall-clock deadline in an unrelated
// renderer test. Counts are the part of that which is deterministic, so
// counts are what tests pin: `pack_parses` must not grow when the mounted
// bytes have not changed, and `installs` must not grow behind a mount.
struct PackInstallStats {
    unsigned refreshes = 0;  // refresh_pack_scripts() calls
    unsigned installs = 0;   // install_classpacks() passes
    // Packs whose declarations were evaluated, and packs served from the
    // memo instead. A repeat install of an unchanged tree must add only to
    // the latter.
    unsigned pack_parses = 0;
    unsigned pack_parse_reuses = 0;
    // How many objects the process-lifetime pack store holds right now: the
    // harvested packs plus every array derived from them that a descriptor
    // borrows (name pools, animation rows and row-pointer tables). Nothing
    // in that store is ever freed — the gameplay registries keep the borrows
    // through static teardown — so it must not grow when the mounted bytes
    // have not changed, or a session that switches campaigns grows without
    // bound. Absolute, not a delta: reset_pack_install_stats() does not zero
    // it, tests compare it across the operation they are pinning.
    unsigned store_objects = 0;
};

PackInstallStats pack_install_stats();
void reset_pack_install_stats();

}  // namespace og::resources
