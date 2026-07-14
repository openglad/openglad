/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <array>

// DECOR plane ids. A level cell is BASE tile (core/pixdefs.h byte in the
// grid plane) + optional DECOR (byte in the per-floor "{grid}_dN.png" plane,
// .fss v11+). These ids are persisted bytes in shipped decor PNGs:
// append-only, NEVER renumber. 0 means "no decor".
//
// This header is the SIM CONTRACT only (passability + concealment).
// Display mappings live per layer, NOT here: art table in graphlib.cpp,
// radar colors in radar.cpp, curses glyphs in glyph_map.cpp.
inline constexpr unsigned char DECOR_NONE          = 0;
inline constexpr unsigned char DECOR_TORCH1        = 1;  // art cut from 16torch1
inline constexpr unsigned char DECOR_TORCH2        = 2;
inline constexpr unsigned char DECOR_TORCH3        = 3;
inline constexpr unsigned char DECOR_BRAZIER       = 4;  // from 16braz1
inline constexpr unsigned char DECOR_BOULDER_1     = 5;  // from 16stone1..4
inline constexpr unsigned char DECOR_BOULDER_2     = 6;
inline constexpr unsigned char DECOR_BOULDER_3     = 7;
inline constexpr unsigned char DECOR_BOULDER_4     = 8;
inline constexpr unsigned char DECOR_PEBBLES       = 9;  // from 16grassr
inline constexpr unsigned char DECOR_COLUMN_BOTTOM = 10; // reuses 16colm0
inline constexpr unsigned char DECOR_COLUMN_TOP    = 11; // reuses 16colm1
inline constexpr unsigned char DECOR_SHRUB         = 12; // NEW art (procedural)
inline constexpr unsigned char DECOR_BONES         = 13; // NEW art (procedural)
inline constexpr unsigned char DECOR_MAX           = 14; // append-only, never renumber

// How a decor id composes with the base tile in query_grid_passable. Decor
// only RESTRICTS; it never grants passage (composition = base AND decor).
// BlocksGround reproduces the legacy water/torch/boulder arm exactly:
// Order::Weapon passes, BIT_FLYING || flight_left() passes, else blocked.
// BlocksAll is reserved (no v1 user).
enum class DecorPassability : unsigned char {
    None,
    BlocksGround,
    BlocksAll,
};

struct DecorDef
{
    DecorPassability pass;
    // Concealment like TYPE_TREES but walkable (forestwalk hide + weapon
    // lineofsight decay). No MIGRATED decor conceals — stock levels are
    // byte-identical; SHRUB is new-content only.
    bool conceals;
};

// Sim registry, indexed by decor id. Consumers MUST bounds-gate the index
// (id < DECOR_MAX) before subscripting: plane bytes are file-supplied data
// (the loader clamps, but hostile in-memory planes must not read OOB).
inline constexpr std::array<DecorDef, DECOR_MAX> kDecorRegistry = {{
    {DecorPassability::None,         false}, // DECOR_NONE
    {DecorPassability::BlocksGround, false}, // DECOR_TORCH1
    {DecorPassability::BlocksGround, false}, // DECOR_TORCH2
    {DecorPassability::BlocksGround, false}, // DECOR_TORCH3
    {DecorPassability::BlocksGround, false}, // DECOR_BRAZIER
    {DecorPassability::BlocksGround, false}, // DECOR_BOULDER_1
    {DecorPassability::BlocksGround, false}, // DECOR_BOULDER_2
    {DecorPassability::BlocksGround, false}, // DECOR_BOULDER_3
    {DecorPassability::BlocksGround, false}, // DECOR_BOULDER_4
    {DecorPassability::None,         false}, // DECOR_PEBBLES
    {DecorPassability::BlocksGround, false}, // DECOR_COLUMN_BOTTOM
    {DecorPassability::BlocksGround, false}, // DECOR_COLUMN_TOP
    {DecorPassability::None,         true},  // DECOR_SHRUB
    {DecorPassability::None,         false}, // DECOR_BONES
}};

[[nodiscard]] constexpr const std::array<DecorDef, DECOR_MAX>&
decor_registry() noexcept
{
    return kDecorRegistry;
}
