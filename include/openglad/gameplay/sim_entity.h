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

#include <cstddef>
#include <openglad/core/order.h>
#include <cstdint>

class GameWorld;

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

    // Identity
    Order order = Order::Living;
    char  family = 0;

    [[nodiscard]] std::uint32_t entity_id() const noexcept { return entity_id_; }
    void mark_dirty(std::uint8_t bit) noexcept
    {
        dirty_mask_[bit / 64] |= (1ULL << (bit % 64));
    }
    void mark_all_dirty() noexcept
    {
        dirty_mask_[0] = ~0ULL;
        dirty_mask_[1] = ~0ULL;
    }
    void clear_dirty() noexcept
    {
        dirty_mask_[0] = 0;
        dirty_mask_[1] = 0;
    }
    [[nodiscard]] bool is_dirty(std::uint8_t bit) const noexcept
    {
        return (dirty_mask_[bit / 64] & (1ULL << (bit % 64))) != 0;
    }
    [[nodiscard]] std::uint64_t dirty_mask_word(std::size_t index) const noexcept
    {
        return (index < 2) ? dirty_mask_[index] : 0;
    }
    void set_snapshot_entity_id(std::uint32_t entity_id) noexcept
    {
        entity_id_ = entity_id;
    }
    void set_snapshot_position(short xpos_value, short ypos_value,
                               float worldx_value, float worldy_value) noexcept
    {
        xpos = xpos_value;
        ypos = ypos_value;
        worldx_ = worldx_value;
        worldy_ = worldy_value;
    }

    // Animation frame state (sim-relevant; actual pixel data is in render component)
    short frame = 0;

protected:
    // Position: floating-point authoritative, xpos/ypos are display-snapped
    float worldx_ = -1.0f;
    float worldy_ = -1.0f;

    std::uint32_t entity_id_ = 0;
    short frames = 0;
    GameWorld* owning_world_ = nullptr;
    std::uint64_t dirty_mask_[2] = {};

    friend class ::GameWorld;
};

} // namespace og::sim
