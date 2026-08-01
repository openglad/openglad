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
#include <openglad/gameplay/dirty_field_bits.h>
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

    [[nodiscard]] short xpos() const noexcept { return xpos_; }
    void set_xpos(short value) noexcept
    {
        xpos_ = value;
        mark_dirty(og::dirty::BIT_XPOS);
    }

    [[nodiscard]] short ypos() const noexcept { return ypos_; }
    void set_ypos(short value) noexcept
    {
        ypos_ = value;
        mark_dirty(og::dirty::BIT_YPOS);
    }

    [[nodiscard]] short sizex() const noexcept { return sizex_; }
    void set_sizex(short value) noexcept
    {
        sizex_ = value;
        mark_dirty(og::dirty::BIT_SIZEX);
    }

    [[nodiscard]] short sizey() const noexcept { return sizey_; }
    void set_sizey(short value) noexcept
    {
        sizey_ = value;
        mark_dirty(og::dirty::BIT_SIZEY);
    }

    // --- Z-axis / multi-floor (default 0 == legacy flat single-floor) -------
    // worldz: authoritative sub-floor height in pixels (rendering subtracts it
    // from the screen y). Public like xpos/sizex (no snapped counterpart, no
    // setxy-style invariant to protect). floor: which stacked floor the entity
    // occupies. sizez: cylinder height; 0 is the "full/unbounded height"
    // sentinel so legacy entities always overlap in z (collision unchanged).
    [[nodiscard]] float worldz() const noexcept { return worldz_value_; }
    void set_worldz(float value) noexcept
    {
        worldz_value_ = value;
        mark_dirty(og::dirty::BIT_WORLDZ);
    }

    [[nodiscard]] short floor() const noexcept { return floor_; }
    void set_floor(short value) noexcept
    {
        floor_ = value;
        mark_dirty(og::dirty::BIT_FLOOR);
    }

    [[nodiscard]] short sizez() const noexcept { return sizez_; }
    void set_sizez(short value) noexcept
    {
        sizez_ = value;
        mark_dirty(og::dirty::BIT_SIZEZ);
    }

    [[nodiscard]] unsigned char team_num() const noexcept { return team_num_; }
    void set_team_num(unsigned char value) noexcept
    {
        team_num_ = value;
        mark_dirty(og::dirty::BIT_TEAM_NUM);
    }

    [[nodiscard]] unsigned char real_team_num() const noexcept
    {
        return real_team_num_;
    }
    void set_real_team_num(unsigned char value) noexcept
    {
        real_team_num_ = value;
        mark_dirty(og::dirty::BIT_REAL_TEAM_NUM);
    }

    [[nodiscard]] signed char user() const noexcept { return user_; }
    void set_user(signed char value) noexcept
    {
        user_ = value;
        mark_dirty(og::dirty::BIT_USER);
    }

    [[nodiscard]] short dead() const noexcept { return dead_; }
    void set_dead(short value) noexcept
    {
        dead_ = value;
        mark_dirty(og::dirty::BIT_DEAD);
    }

    [[nodiscard]] short death_called() const noexcept { return death_called_; }
    void set_death_called(short value) noexcept
    {
        death_called_ = value;
        mark_dirty(og::dirty::BIT_DEATH_CALLED);
    }

    [[nodiscard]] short invulnerable_left() const noexcept
    {
        return invulnerable_left_;
    }
    void set_invulnerable_left(short value) noexcept
    {
        invulnerable_left_ = value;
        mark_dirty(og::dirty::BIT_INVULNERABLE_LEFT);
    }

    [[nodiscard]] short invisibility_left() const noexcept
    {
        return invisibility_left_;
    }
    void set_invisibility_left(short value) noexcept
    {
        invisibility_left_ = value;
        mark_dirty(og::dirty::BIT_INVISIBILITY_LEFT);
    }

    [[nodiscard]] short flight_left() const noexcept { return flight_left_; }
    void set_flight_left(short value) noexcept
    {
        flight_left_ = value;
        mark_dirty(og::dirty::BIT_FLIGHT_LEFT);
    }

    [[nodiscard]] short bonus_rounds() const noexcept { return bonus_rounds_; }
    void set_bonus_rounds(short value) noexcept
    {
        bonus_rounds_ = value;
        mark_dirty(og::dirty::BIT_BONUS_ROUNDS);
    }

    [[nodiscard]] Order order() const noexcept { return order_; }
    void set_order(Order value) noexcept
    {
        order_ = value;
        mark_dirty(og::dirty::BIT_ORDER);
    }

    [[nodiscard]] char family() const noexcept { return family_; }
    void set_family(char value) noexcept
    {
        family_ = value;
        mark_dirty(og::dirty::BIT_FAMILY);
    }

    [[nodiscard]] std::uint32_t entity_id() const noexcept
    {
        return entity_id_value_;
    }
    void set_entity_id(std::uint32_t entity_id) noexcept
    {
        entity_id_value_ = entity_id;
        mark_dirty(og::dirty::BIT_ENTITY_ID);
    }
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
        set_entity_id(entity_id);
    }
    void set_snapshot_position(short xpos_value, short ypos_value,
                               float worldx_value, float worldy_value) noexcept
    {
        set_xpos(xpos_value);
        set_ypos(ypos_value);
        set_worldx(worldx_value);
        set_worldy(worldy_value);
    }

    // Animation frame state (sim-relevant; actual pixel data is in render component)
    [[nodiscard]] short frame() const noexcept { return frame_; }

protected:
    void set_frame_state(short value) noexcept
    {
        frame_ = value;
        mark_dirty(og::dirty::BIT_FRAME);
    }

    [[nodiscard]] float worldx() const noexcept { return worldx_value_; }
    void set_worldx(float value) noexcept
    {
        worldx_value_ = value;
        mark_dirty(og::dirty::BIT_WORLDX);
    }

    [[nodiscard]] float worldy() const noexcept { return worldy_value_; }
    void set_worldy(float value) noexcept
    {
        worldy_value_ = value;
        mark_dirty(og::dirty::BIT_WORLDY);
    }

    // Position: floating-point authoritative, xpos/ypos are display-snapped
    float worldx_value_ = -1.0f;
    float worldy_value_ = -1.0f;
    // Z height in pixels above the entity's current floor plane (0 == on floor).
    float worldz_value_ = 0.0f;

private:
    short xpos_ = 0;
    short ypos_ = 0;
    short sizex_ = 0;
    short sizey_ = 0;
    short floor_ = 0;   // stacked floor index (0 == ground / single-floor)
    short sizez_ = 0;   // cylinder height; 0 == full/unbounded sentinel
    // Zardus: FIX: lets make these unsigned so that real_team_num doesn't
    // wrap around from 255 to -1 :-)
    unsigned char team_num_ = 0;
    // to show nothing's changed
    unsigned char real_team_num_ = 255;
    signed char user_ = -1;
    short dead_ = 0;
    short death_called_ = 0;
    short invulnerable_left_ = 0;
    short invisibility_left_ = 0;
    short flight_left_ = 0;
    short bonus_rounds_ = 0;
    Order order_ = Order::Living;
    char family_ = 0;
    short frame_ = 0;
    std::uint32_t entity_id_value_ = 0;

protected:
    short frames = 0;
    GameWorld* owning_world_ = nullptr;
    std::uint64_t dirty_mask_[2] = {};

    friend class ::GameWorld;
};

} // namespace og::sim
