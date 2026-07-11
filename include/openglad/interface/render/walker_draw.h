/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

class GameWorld;
class screen;
class walker;
class viewscreen;

struct WalkerRenderPosition {
    float worldx = 0.0f;
    float worldy = 0.0f;
    float xpos = 0.0f;
    float ypos = 0.0f;
};

struct DamageNumberRenderSnapshot
{
    float x = 0.0f;
    float y = 0.0f;
    float t = 0.0f;
    float value = 0.0f;
    std::uint32_t created_tick = 0u;
    unsigned char color = 0;
};

class DamageNumberRenderContext
{
public:
    struct Entry
    {
        DamageNumberRenderSnapshot snapshot{};
        std::uint32_t last_advance_tick =
            std::numeric_limits<std::uint32_t>::max();
    };

    Entry& prepare_state(std::uint32_t owner_entity_id,
                         std::size_t index,
                         const DamageNumberRenderSnapshot& snapshot);
    void trim_owner(std::uint32_t owner_entity_id, std::size_t live_count);
    void erase_index(std::uint32_t owner_entity_id, std::size_t index);
    void prune_dead_owners(const GameWorld& world);

#ifdef TESTING
    [[nodiscard]] std::size_t state_count() const noexcept;
#endif

private:
    std::unordered_map<std::uint32_t, std::vector<Entry>> state_by_owner_;
};

// Rendering functions for walker entities.
// These live in the render layer, not the entity layer, because they
// depend on the screen/video system for drawing.
float query_render_interpolation_alpha();
WalkerRenderPosition resolve_walker_render_position(const walker& w,
                                                    float alpha);
// alpha<255 draws a faded/ghosted sprite only (for non-camera floors): no
// flash/outline/mode/HP-bar/damage-number embellishments.
bool draw_walker(walker& w, viewscreen* view_buf, unsigned char alpha = 255);
bool draw_walker_tile(walker& w, viewscreen* view_buf);
void draw_walker_path(walker& w, viewscreen* view_buf);
void draw_small_health_bar(walker* w, viewscreen* view_buf);
#ifdef TESTING
std::size_t damage_number_render_state_count(const screen* screen_ctx);
#endif
