/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// classpack.yaml reader (docs/lua-classpacks-design.md §4). Parses a pack's
// family descriptor data into the shared interchange structs
// (openglad/gameplay/families/classpack_data.h), which own their strings:
// a ClasspackData survives on its own, independent of the YAML text buffer.
//
// This header is the YAML FRONT END only. The structs it fills — and the
// installer that consumes them — are shared with the Lua declaration pass;
// see classpack_data.h for why they live on the gameplay side.

#include <openglad/gameplay/families/classpack_data.h>

#include <string_view>

namespace og::data {

// Parses classpack.yaml text into out. Strict: malformed YAML, a bad
// number/bool, or a family entry without an id fails the whole pack
// (returns false after LogWarn; out is partially filled and must be
// discarded). Unknown keys and unknown families sections are skipped for
// forward compatibility. Never throws.
//
// The v2 blocks of a living entry hold two extra tiers, both about
// mistakes a modder can actually make:
//   * a member that stats: or combat: leaves out, a costs: without hire, a
//     special without id/name/mp_cost, an out-of-order slot, a duplicate
//     id, one of the four dead derived axes (mp/ranged_damage/range/
//     defense), or a retired schema-v1 key fails the pack with a message
//     saying what to write instead;
//   * an unrecognized key INSIDE one of those blocks warns and is dropped,
//     rather than vanishing the way an unknown entry-level key does.
bool parse_classpack_yaml(std::string_view text, ClasspackData& out,
                          const char* source_name);

} // namespace og::data
