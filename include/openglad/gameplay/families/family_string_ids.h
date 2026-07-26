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

// Canonical string id for a registered family: "core:<name>" where <name>
// is the registry display name lowercased with spaces mapped to
// underscores, or the positional escape "core:#<id>" when the display name
// is shared by more than one family in the order (the BEAST and SLIME
// groups). Returns an empty string for unknown ids.
std::string family_string_id(Order order, int family_id);

// Resolves "core:<name>", a bare "<name>", or the positional escape
// "core:#<id>" to the family byte for the order. Returns -1 when unknown.
// Namespace prefixes are not validated: resolution is name-based within
// the order's registry, so installed mod families resolve the same way.
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
