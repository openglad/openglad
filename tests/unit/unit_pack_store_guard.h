/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/resources/packs.h>

namespace og::test {

// Puts the process pack stores — scripts, lib modules, family chunks — and,
// through install_classpacks(), the five family registries and the tuning
// store back the way the mounted packs/ tree describes them.
//
// A test that clears or rewrites any of those to isolate itself owns the
// repair: unit_main.cpp's census fingerprints all of them around every test
// and names whoever handed the next test a world it never set up. The heal
// in the same listener runs AFTER the census, and only when a store is
// completely empty, so it is a backstop for teardown accidents, not a
// licence to leave the stores broken.
//
// Declare it as the test's (or fixture's) first member/statement, so it
// outlives every clear the body performs. Campaign mounts are a different
// rule with a different guard: og::test::ScopedCampaignMountState
// (tests/test_save_state_guard.h).
class ScopedPackStoreState
{
public:
    ScopedPackStoreState() = default;

    ~ScopedPackStoreState() { (void)og::resources::refresh_pack_scripts(); }

    ScopedPackStoreState(const ScopedPackStoreState&) = delete;
    ScopedPackStoreState& operator=(const ScopedPackStoreState&) = delete;
};

}  // namespace og::test
