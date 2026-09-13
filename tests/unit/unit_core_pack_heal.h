/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdio>
#include <string>

#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/packs.h>

std::string get_asset_path();

namespace og::test {

// Family behavior lives in the core class pack. Its descriptors carry no
// C++ behavior callbacks, so a headless unit
// binary that skips io_init's asset mounts would run a sim whose specials,
// potions and effects all silently do nothing. Mount the shipped packs/ tree
// the same way io_init does. Idempotent, and re-asserted after every test:
// a test that tears PhysFS down (or remounts a campaign, which rescans
// packs/) must not leave later tests with no family behavior at all.
//
// Family data and behavior both live in the core class pack: the five
// registries start empty and are filled by install_classpacks() from the
// mounted packs/ tree. A headless unit binary
// that skips io_init's asset mounts would otherwise run against registries
// where every get_*_family_descriptor answers nullptr — no soldier, no
// knife, no specials. Mount the shipped packs/ tree the same way io_init
// does. Idempotent, and re-asserted after every test: a test that tears
// PhysFS down (or remounts a campaign, which rescans packs/) must not leave
// later tests with no families at all.
//
// (Both mains' own words, kept verbatim: this is one definition for both
// of them, because they had drifted and the heal the SDL-free one gained
// in af0c5e35 never reached the og_add_unit_group binaries.)
inline void mount_core_pack()
{
    const bool mounted = og::resources::mount(
        (get_asset_path() + "packs/").c_str(), "packs/", 1);
    if (!mounted)
    {
        std::fprintf(stderr,
                     "error: core class pack not mounted (%s) — the family "
                     "registries will be empty and family behavior absent\n",
                     og::resources::filesystem_last_error().c_str());
        return;
    }
    // Family chunks carry the shipped families' declarations AND their
    // hooks, so a test that isolated itself by clearing them needs the same
    // heal a cleared script registry gets.
    if (og::script::pack_scripts().empty() ||
        og::script::pack_family_chunks().empty())
        og::resources::refresh_pack_scripts();
}

}  // namespace og::test
