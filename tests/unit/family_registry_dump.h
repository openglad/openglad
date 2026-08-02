/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// A text rendering of every installed family descriptor, field by field.
//
// This exists to answer ONE question mechanically: did changing the FRONT
// END change the DATA? packs/core is being moved off classpack.yaml onto
// Lua `og.family` declarations, and the whole promise of that move is that
// the installed registry comes out identical — same wire ids, same numbers,
// same strings, same animation frames, same tuning. A promise nobody can
// diff is a hope, so this dumps the registry into stable text that a golden
// file can hold and a test can compare byte for byte.
//
// Rules the format follows, all so the text is a function of the DATA and
// nothing else:
//
//   * every field of every descriptor appears, on its own line, named —
//     "unset" is spelled, never omitted, so a field that stopped being
//     installed shows up as a changed line rather than a missing one;
//   * pointers are never printed as addresses. A `const char*` prints its
//     text (or `~` for nullptr, which is the descriptor's own "none"), and
//     a behavior callback prints only whether it is there;
//   * floats print with enough digits to round-trip, so a 0.1f that became
//     a double 0.1 is a visible difference;
//   * tuning pairs are SORTED by key. The two front ends disagree about
//     order and only about order: the YAML reader preserves stream order,
//     the Lua harvest sorts (a Lua table has no source order to preserve).
//     Reads are by key in both, so sorting here compares the content the
//     engine can actually observe.
//
// Test-only: it lives under tests/ and nothing in src/ knows it exists.

#include <string>

namespace og::testing {

// Every populated slot of all five registries, in order:
// living, weapon, effect, treasure, generator; ascending family id within
// each. Free slots (the getters answer nullptr) are skipped entirely — a
// slot that gained or lost a family shows as an added or removed block.
std::string dump_installed_families();

}  // namespace og::testing
