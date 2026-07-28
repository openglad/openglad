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
// reinstall classpack.yaml data. The one call sites use when mounts
// CHANGE (campaign mount/unmount — campaign zips may embed packs/ that
// merge into the virtual tree). World VMs rebuild lazily on their next
// dispatch via the generation counter.
int refresh_pack_scripts();

// Scan packs/ for each pack's classpack data (same deterministic order
// as the scripts) and install the family descriptor DATA into the five
// gameplay registries. Per pack, packs/<pack_id>/classpack.yaml is
// parsed first when present, then every packs/<pack_id>/families/*.yaml
// in sorted filename order into the same parsed pack — the split layout
// loads exactly like the concatenated monolith, and a pack may ship
// either layout or both (families/ entries append after classpack.yaml;
// a later entry pinning an already-declared WIRE slot overwrites just
// the data fields it declares, and — like any install — replaces that
// slot's tuning map whole). For each entry, the current descriptor is
// copied, only
// the fields the YAML declares are overwritten, and all C++ behavior
// callback pointers are preserved unchanged (behavior lives in pack
// Lua). wire_id pins the slot (core = legacy bytes); wire_id
// auto/absent assigns the first free id >= 21 in pack-id-lexicographic
// order — entries beyond a registry's capacity are skipped with a
// warning. Any file that fails to parse rejects that pack only.
// Returns the number of family entries installed.
//
// Lifetime: descriptor const char* fields installed from YAML point into
// a process-lifetime ClasspackStore that owns every parsed ClasspackData
// (deliberately never destroyed — registry descriptors may be read up to
// static teardown).
int install_classpacks();

// Installs one already-parsed classpack, moving it into the process-
// lifetime ClasspackStore first (same semantics as one iteration of
// install_classpacks, with a fresh auto-wire-id counter). Exposed for
// unit tests. Returns the number of family entries installed.
int install_classpack_data(og::data::ClasspackData&& data);

}  // namespace og::resources
