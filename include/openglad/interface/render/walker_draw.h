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
class PixieData;
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
// alpha<255 marks a non-camera (faded/ghosted) floor: the sprite is drawn ONLY
// (no flash/outline/mode/HP-bar/damage-number embellishments). With an active
// compositor layer it draws opaque and the layer applies the fade/scale; when a
// budget/allocation fallback prevents that redirect, it draws directly with
// `alpha` so a lower floor can never become full-brightness. alpha==255 is the
// camera-floor full path.
bool draw_walker(walker& w, viewscreen* view_buf, unsigned char alpha = 255,
                 bool layer_active = true);
// Multifloor FX pre-pass sprites (render-only, drawn before the normal entity
// loops so entities overdraw them). Both apply to alive Living/Weapon walkers
// that are neither phantom nor invisible, and return true when a blit was
// issued (callers count for the per-pass TRACE).
// - Shadow: squashed black silhouette on the GROUND plane (no worldz raise),
//   so an arcing projectile's height reads from the sprite/shadow gap.
// - Reflection: vertically flipped sprite mirrored below the feet, masked to
//   the reflective tiles of camera_grid (the camera floor's tile grid) per
//   reflective_tiles(): PIX_GLASS + pure water + lava/marsh.
bool draw_walker_shadow(walker& w, viewscreen* view_buf);
bool draw_walker_reflection(walker& w, viewscreen* view_buf,
                            const PixieData& camera_grid);
// Blob shadow cast onto the CAMERA floor by an entity `stories` floors above
// it (default ghosts-off multifloor look; the upper floor itself is not
// drawn). Same eligibility as draw_walker_shadow, but offset SE +2px per
// story (matching the NW sun of the overhang/cloud shadows) and rendered
// slightly smaller + fainter per story (size/alpha capped at 2 stories).
bool draw_walker_blob_shadow(walker& w, viewscreen* view_buf,
                             std::int32_t stories);
// Ground-plane screen anchor shared by the FX pre-pass (shadows, reflections,
// water ripples): draw_walker's screen position INCLUDING the lunge/recoil
// offsets but EXCLUDING the worldz raise. (Sint32 aliases std::int32_t.)
void ground_plane_anchor(walker& w, viewscreen* view_buf,
                         std::int32_t& xscreen, std::int32_t& yscreen);
bool draw_walker_tile(walker& w, viewscreen* view_buf);
void draw_walker_path(walker& w, viewscreen* view_buf);
void draw_small_health_bar(walker* w, viewscreen* view_buf);
#ifdef TESTING
std::size_t damage_number_render_state_count(const screen* screen_ctx);
#endif
