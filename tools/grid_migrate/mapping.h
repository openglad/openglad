/* grid_migrate — the BASE + DECOR migration mapping table.
 *
 * Source of truth for how legacy combined tiles (object baked into the
 * ground art, blocking baked into the byte) decompose into a BASE ground
 * tile plus a DECOR plane id (core/decordefs.h). Kept self-contained (core
 * headers only) so the migration unit tests can include the same table the
 * tool applies; see docs/z-axis-design.md and the tile-layering design.
 *
 * Everything NOT in this table copies through unchanged: legacy combined
 * tiles remain valid base tiles forever, so partial migration is always
 * safe (per-cell fallback re-uses that guarantee).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>

#include <array>
#include <cstddef>

namespace gridmig {

// One migration row. `contextual` marks the boulders, whose base is picked
// by a deterministic neighbor-majority ground scan (boulders sit on whatever
// ground surrounds them: grass in gladiator, snow in tryxian/westlands);
// `base` is then the art-default fallback used on a tie or when no ground
// neighbor votes. Fixed-base rows reproduce the combined art's own
// background: the torch art is a wall torch on brick wallside, the brazier
// a fire bowl on wood planks, the rubble pebbles on dark grass.
struct MappedTile
{
    unsigned char legacy;      // the combined tile byte being replaced
    unsigned char base;        // fixed base byte / contextual fallback
    unsigned char decor;       // decor plane id (core/decordefs.h)
    bool contextual;           // base picked by the neighbor ground scan
};

// Passability invariant (why the table is safe by construction): every
// legacy byte here sits in query_grid_passable's "weapons and flyers pass,
// ground walkers blocked" arm EXCEPT PIX_GRASS_RUBBLE (plain walkable), and
// composes to the identical verdict: TORCH*/BRAZIER1/BOULDER_* get
// DecorPassability::BlocksGround (the same arm) over a base that is either
// walkable (grass/FLOOR1) or itself in that arm (WALLSIDE_C), and
// GRASS_RUBBLE becomes walkable GRASS_DARK_1 + DECOR_PEBBLES (None).
// The tool proves this per cell anyway; the table comment is the intent.
inline constexpr std::array<MappedTile, 9> kMigrationTable = {{
    {PIX_TORCH1, PIX_WALLSIDE_C, DECOR_TORCH1, false},
    {PIX_TORCH2, PIX_WALLSIDE_C, DECOR_TORCH2, false},
    {PIX_TORCH3, PIX_WALLSIDE_C, DECOR_TORCH3, false},
    {PIX_BRAZIER1, PIX_FLOOR1, DECOR_BRAZIER, false},
    // Art-default grass per boulder sprite (fidelity-measured: boulder2 cut
    // over GRASS3 leaves 0 residual pixels; 1/3/4 sit best on GRASS2).
    {PIX_BOULDER_1, PIX_GRASS2, DECOR_BOULDER_1, true},
    {PIX_BOULDER_2, PIX_GRASS3, DECOR_BOULDER_2, true},
    {PIX_BOULDER_3, PIX_GRASS2, DECOR_BOULDER_3, true},
    {PIX_BOULDER_4, PIX_GRASS2, DECOR_BOULDER_4, true},
    // Same genre before and after (PIX_GRASS_RUBBLE already smooths as
    // TYPE_GRASS_DARK), so autotiling around the cell is unchanged.
    {PIX_GRASS_RUBBLE, PIX_GRASS_DARK_1, DECOR_PEBBLES, false},
}};

// Table lookup; nullptr = byte copies through unchanged.
[[nodiscard]] inline constexpr const MappedTile*
map_legacy_tile(unsigned char pix) noexcept
{
    for (const MappedTile& row : kMigrationTable)
    {
        if (row.legacy == pix)
            return &row;
    }
    return nullptr;
}

// Canonical ground tiles the boulder contextual scan may select, keyed by
// the neighbor's terrain genre (TYPE_* from core/terrain_types.h via the
// smoother). GRASS itself resolves to the boulder row's art-default `base`.
// These are exactly the "walkable plain ground" genres: nothing here can
// change a cell's passability class.
inline constexpr unsigned char kCanonicalGrassDark = PIX_GRASS_DARK_1;
inline constexpr unsigned char kCanonicalGrassLight = PIX_GRASS_LIGHT_1;
inline constexpr unsigned char kCanonicalDirt = PIX_DIRT_1;
inline constexpr unsigned char kCanonicalDirtDark = PIX_DIRT_DARK_1;
inline constexpr unsigned char kCanonicalSnow = PIX_SNOW1;
inline constexpr unsigned char kCanonicalAsh = PIX_ASH1;

// The complete set of base bytes a contextual (boulder) cell may end up
// with — the audit whitelist for "every migrated cell is either an exact
// copy or a table product".
[[nodiscard]] inline constexpr bool
is_contextual_base_candidate(unsigned char pix) noexcept
{
    return pix == PIX_GRASS2 || pix == PIX_GRASS3 ||
           pix == kCanonicalGrassDark || pix == kCanonicalGrassLight ||
           pix == kCanonicalDirt || pix == kCanonicalDirtDark ||
           pix == kCanonicalSnow || pix == kCanonicalAsh;
}

} // namespace gridmig
