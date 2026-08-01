/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Family-id resolution (docs/lua-classpacks-design.md §5) and
// classpack.yaml vocabulary parsing used by resources/classpack_yaml.

#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_descriptor.h>

#include <cstdint>
#include <string_view>

namespace og::families {

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
//      ignored. This convenience path resolves "soldier". First (lowest)
//      matching byte wins, so a display name shared by two mounted families
//      is only unambiguously addressable through form 2.
// Matching is case-insensitive and treats ' ' and '_' as the same character.
int resolve_family_string_id(Order order, const char* family_str);

// classpack.yaml `animation:` name to FamilyAnimationType
// (standard|mage|skeleton|giant_skeleton|slime|small_slime|static).
bool animation_type_from_name(std::string_view name, FamilyAnimationType& out);

// classpack.yaml `init_bit_flags:` entry name ("FLYING", "SWIMMING", ...)
// to a BIT_* stat flag. Returns 0 for unknown names.
std::int32_t bit_flag_from_name(std::string_view name);

}  // namespace og::families
