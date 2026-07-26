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

namespace og::resources {

// Scan packs/ and (re)register all pack scripts. Returns the number of
// scripts registered. Safe to call again after mounts change.
int register_mounted_pack_scripts();

}  // namespace og::resources
