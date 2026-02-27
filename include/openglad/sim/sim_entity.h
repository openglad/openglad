/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// SimEntity: SDL-free base class for all game entities.
//
// walker inherits from SimEntity for position, size, identity, state, and
// animation frame tracking. Rendering data (pixel buffers, SDL_Surface)
// lives in an optional pixieN render component attached at runtime.
//
// This class can be instantiated headlessly (no SDL, no PixieData) for
// deterministic testing and replay.

#include <openglad/core/order.h>
#include <cstdint>

struct SaveData;
class cfg_store;

namespace og::sim {
class SimEntity
{
public:
    SimEntity();
    virtual ~SimEntity();

    SimEntity(const SimEntity&) = delete;
    SimEntity& operator=(const SimEntity&) = delete;
    SimEntity(SimEntity&&) = delete;
    SimEntity& operator=(SimEntity&&) = delete;

    // Position (pixel coordinates in the game world)
    short xpos = 0;
    short ypos = 0;
    short sizex = 0;
    short sizey = 0;

    // Team/identity (public to match walker API)
    unsigned char team_num = 0;
    unsigned char real_team_num = 255;
    signed char user = -1;              // Controlling player (-1 = AI)

    // State flags
    short dead = 0;
    short death_called = 0;
    short invulnerable_left = 0;
    short invisibility_left = 0;
    short flight_left = 0;
    short bonus_rounds = 0;

    // Sim context (non-owning pointers set by the runtime layer)
    SaveData*     sim_save = nullptr;
    cfg_store*    sim_config = nullptr;

    // Identity
    Order order = Order::Living;
    char  family = 0;

    // Animation frame state (sim-relevant; actual pixel data is in render component)
    short frame = 0;

protected:
    // Position: floating-point authoritative, xpos/ypos are display-snapped
    float worldx_ = -1.0f;
    float worldy_ = -1.0f;

    short frames = 0;
};

} // namespace og::sim
