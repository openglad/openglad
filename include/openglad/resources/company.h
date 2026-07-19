/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#pragma once

#include <cstdint>
#include <optional>

// Company layer (docs/company-basecamp-design.md §3). A save file IS a
// company; this header collects the company-level helpers that sit above the
// raw GTL serialization in SaveData.

namespace og::data {

// Wall-clock seam (§3.2) for the GTL v14 `last_played_unix_s` field.
//
// This is the ONLY place company code reads wall time. It is parity-safe by
// construction: og_gameplay cannot include og_resources (dependency direction
// og_resources -> og_gameplay), the parity harness performs zero save IO, and
// SaveData::save()/load() merely serialize the field — they never call this.
// Nothing in the deterministic sim can ever observe the clock.
//
// Returns the current wall-clock time as unix seconds, or the fixed test
// value when one is installed.
std::int64_t company_clock_now_s();

// Installs (or, with std::nullopt, removes) a fixed wall-clock value so tests
// can pin `last_played_unix_s` deterministically.
void set_company_clock_for_tests(std::optional<std::int64_t> fixed_now_s);

} // namespace og::data
