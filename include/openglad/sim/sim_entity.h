/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// SimEntity: a lightweight, SDL-free description of an entity's simulation state.
//
// This does NOT replace walker's inheritance from pixieN. Instead, it provides
// a pure-data view of the simulation-relevant fields that can be:
//   - Instantiated headlessly (no SDL, no PixieData)
//   - Used for deterministic replay/snapshot
//   - Tested without graphics initialization
//
// walker owns a SimEntity and exposes its fields. Sim-layer code that only
// needs position, team, order, and state flags can operate on SimEntity
// without pulling in SDL headers.
//
// Future work (G4 full extraction): refactor walker to inherit from SimEntity
// instead of pixieN for sim fields, making pixieN a render-only component.

#include <cstdint>

// Forward declarations to avoid pulling in heavy headers.
class LevelData;
struct SaveData;

namespace og::sim {
class SimEventLog;

struct SimEntity
{
    // Position (pixel coordinates in the game world)
    float worldx = -1.0f;
    float worldy = -1.0f;
    short xpos = 0;
    short ypos = 0;
    short sizex = 0;
    short sizey = 0;

    // Identity
    std::uint8_t order = 0;      // Order enum value
    std::int8_t  family = 0;
    std::uint8_t team_num = 0;
    std::uint8_t real_team_num = 255;
    std::int8_t  user = -1;       // Controlling player (-1 = AI)

    // State flags
    short dead = 0;
    short death_called = 0;
    short invulnerable_left = 0;
    short invisibility_left = 0;
    short flight_left = 0;
    short bonus_rounds = 0;

    // Sim context (non-owning pointers set by the runtime layer)
    LevelData*    sim_level = nullptr;
    SaveData*     sim_save = nullptr;
    std::int32_t* sim_enemy_freeze = nullptr;
    SimEventLog*  sim_events = nullptr;
};

} // namespace og::sim
