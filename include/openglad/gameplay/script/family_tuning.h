/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Per-family tuning data (quality plan Stage 1, adopted by Stage 4).
//
// A classpack.yaml family entry may carry a `tuning:` map of scalar
// constants. The resources installer parses it and stores it here, keyed by
// (order, wire id), exactly like the rest of the descriptor data: tuning is
// LOAD-TIME pack content — it rides multiplayer transfer as part of
// classpack.yaml, never as wire state, and no snapshot carries it. Scripts
// read it through `og.tuning(self)` as a frozen read-only table (key access
// only; writes error; iteration is neither needed nor provided, so the
// no-pairs rule never comes up).
//
// The store is process-global and rebuilt by every classpack install pass
// (install_classpacks clears it before reinstalling the mounted set, so an
// unmounted pack cannot leave tuning behind). The generation counter lets
// per-VM caches of the frozen views notice a reinstall.

#include <openglad/core/order.h>

#include <cstdint>
#include <string>
#include <vector>

namespace og::script {

// One scalar tuning value. The kind preserves the YAML author's spelling:
// a plain `5` arrives as Integer, `5.0` as Number (Lua float), `true` as
// Boolean, anything quoted (or an unparseable plain scalar) as String — so
// Lua-side arithmetic keeps the integer/float subtype the author wrote.
struct TuningValue {
    enum class Kind : std::uint8_t { Integer, Number, Boolean, String };
    Kind kind = Kind::Integer;
    std::int64_t integer = 0;  // Kind::Integer
    double number = 0.0;       // Kind::Number
    bool boolean = false;      // Kind::Boolean
    std::string string;        // Kind::String
};

struct TuningPair {
    std::string key;
    TuningValue value;
};

// YAML mapping order preserved (deterministic install; irrelevant to reads).
using TuningMap = std::vector<TuningPair>;

// Installs (or, with an empty map, erases) the tuning for one family slot.
// Called by the classpack installer for every installed entry, so a pack
// replacing a slot always replaces its tuning too.
void set_family_tuning(Order order, int family_id, TuningMap map);

// The installed tuning for a family, or nullptr when none was declared
// (og.tuning then serves an empty frozen table).
const TuningMap* family_tuning(Order order, int family_id);

// Drops every entry (the head of a full classpack reinstall).
void clear_all_family_tuning();

// Bumped by every mutation above; per-VM frozen-view caches key off it.
std::uint64_t family_tuning_generation();

}  // namespace og::script
