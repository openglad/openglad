/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// String identity for family registries (docs/lua-classpacks-design.md §5)
// plus the classpack.yaml vocabulary tables shared by the pack exporter
// (tools/pack_export) and the pack reader (resources/classpack_yaml).
// Keeping forward and inverse mappings in one place guarantees that an
// exported id always resolves back to the same family byte.

#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_descriptor.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace og::families {

// Canonical string id for a registered family: the fully-qualified id the
// declaring pack gave it (FamilyDescriptor::declared_id, e.g.
// "mypack:warlock"), or — for a family no pack has declared — the legacy
// "core:<name>", where <name> is the registry display name lowercased with
// spaces mapped to underscores. When that candidate id is shared with
// another populated slot (two core families answering to BEAST or SLIME,
// or two packs pinning the same declared id) every member of the collision
// group takes the positional escape "<namespace>:#<id>" instead, so the id
// this returns always resolves back to exactly this family. Returns an
// empty string for unknown ids.
std::string family_string_id(Order order, int family_id);

// Resolves a family id string to the family byte for the order, or -1.
// Three forms, tried in this order:
//   1. "<ns>:#<id>" / "#<id>" — positional escape: the literal byte, valid
//      only when that slot is populated. The namespace is ignored.
//   2. "<ns>:<name>" — exact match against a family's declared pack id
//      (FamilyDescriptor::declared_id). The namespace IS a scope here: two
//      packs may each ship a family named WARLOCK and stay addressable as
//      "alpha:warlock" and "beta:warlock".
//   3. bare-name fallback — the part after the first ':' (or the whole
//      string) matched against the registry display `name`, namespace
//      ignored. This is the back-compat path: it resolves "soldier", and it
//      resolves "core:soldier" against a C++ core pin that no pack has
//      declared yet. First (lowest) matching byte wins, so a display name
//      shared by two mounted families is only addressable through form 2.
// Matching is case-insensitive and treats ' ' and '_' as the same character.
int resolve_family_string_id(Order order, const char* family_str);

// FamilyAnimationType ↔ classpack.yaml `animation:` name
// (standard|mage|skeleton|giant_skeleton|slime|small_slime|static).
const char* animation_type_name(FamilyAnimationType type);
bool animation_type_from_name(std::string_view name, FamilyAnimationType& out);

// BIT_* stat flag ↔ classpack.yaml `init_bit_flags:` entry name
// ("FLYING", "SWIMMING", ...). bit_flag_from_name returns 0 for unknown
// names. bit_flag_names decomposes a mask in ascending bit order; bits
// with no name are returned in *unknown_bits (0 when fully decomposed).
std::int32_t bit_flag_from_name(std::string_view name);
std::vector<std::string> bit_flag_names(std::int32_t flags,
                                        std::int32_t* unknown_bits = nullptr);

}  // namespace og::families
