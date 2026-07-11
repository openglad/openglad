/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Render-only special effects: the weather overlay (drifting cloud banks +
// their ground shadows, rain streaks + scheduled lightning) shown when the
// camera sits on the TOP floor of a multifloor level, expanding ripple rings
// under units standing on water, projectile trails and falling dust fed by a
// per-entity position store, a fire glow around burning entities, the
// screen-shake camera jitter, and the shared reflective-tile mask.

#include <array>
#include <cstddef>
#include <cstdint>

class GameWorld;
class PixieData;
class viewscreen;
class walker;

// Advance all render-only effect state — the frame tick driving weather
// drift, ripple/trail/dust phases and the fire-glow flicker, plus pruning of
// the per-entity position store. Called exactly once per rendered frame
// (screen::redraw); never read by the sim.
void effects_advance_frame();

// Per-tile-id mask of the tiles that mirror entities: PIX_GLASS plus the
// pure water tiles PIX_WATER1/2/3, plus the Westlands lava (PIX_LAVA1/2)
// and marsh (PIX_MARSH1/2) tiles. Edge tiles (watergrass/grasswater) stay
// out — the mask is per-tile, so they would put reflections on their grass
// pixels. Shared by draw_walker_reflection's pre-check and the
// walkputbuffer_reflect blit.
const std::array<bool, 256>& reflective_tiles();

// Expanding ripple rings under an alive Living walker whose feet stand on a
// pure water tile of camera_grid (the camera floor's tile grid). Ring phase
// is a pure function of (render tick, entity id): deterministic and
// replayable after effects_reset_for_testing(). Returns true when at
// least one ring pixel was blended (callers count for the per-pass TRACE).
bool draw_walker_ripples(walker& w, viewscreen* vs,
                         const PixieData& camera_grid);

// Stair direction affordance: a soft alpha-pulsed double chevron blended
// over every visible PIX_ZSTAIR_UP (pointing up) / PIX_ZSTAIR_DOWN
// (pointing down) tile of camera_grid (the camera floor's tile grid).
// Core usability, NOT an "effects" cfg toggle: it is always on in play.
// The level editor never draws it (its floor-override path is excluded by
// the caller), and levels without stair tiles render byte-identically.
// The pulse phase is a pure function of the render tick. Returns true
// when at least one chevron pixel was blended.
bool draw_stair_overlays(viewscreen* vs, const PixieData& camera_grid);

// Weather overlay over vs's viewport, keyed on the WORLD's per-level
// WeatherKind (rolled once per level by the authoritative side and synced
// through WorldSnapshot — a render-only READ here): None draws nothing,
// Clouds draws drifting banks + their ground shadows, Rain draws a
// FULL-SCREEN sparse streak downpour + the scheduled full-viewport
// lightning flash. cfg "effects" weather is the client-side display
// opt-out on top of the kind. Also gated on the camera being under OPEN
// SKY: the top floor of a multifloor level, or a single-floor level whose
// terrain reads as outdoor (at least half its tiles are
// grass/water/trees/dirt genres — a dungeon of walls, cobble and carpet
// stays dry). Any gate failing touches zero pixels (byte-identical
// render). The world comes from the caller (the two redraw variants
// render different LevelRuntimeData worlds).
void draw_cloud_overlay(viewscreen* vs, GameWorld& world);

// Fading dot trail behind an alive, visible Order::Weapon walker: pushes the
// weapon's visual position into the per-entity store (once per frame tick,
// so repeated viewport draws see identical history) and blends 2x2 dots at
// the previous stored positions — PURE_WHITE, or fire colors for the
// fireball/meteor/fire-arrow families. Stationary segments (< 1px apart)
// draw nothing. Returns true when at least one dot pixel was blended.
bool draw_walker_trail(walker& w, viewscreen* vs);

// Falling grey dust specks under an alive Living walker on the floor ABOVE
// the camera: pushes its visual position into the store and, when it moved
// more than half a pixel since the previous stored tick, drops staggered
// specks in the camera floor's space at the walker's world x/y. Returns
// true when at least one speck pixel was blended.
bool draw_walker_dust(walker& w, viewscreen* vs);

// Radial fire glow blended OVER the sprite of an alive fire-family entity
// (Living fire elemental, Weapon fireball/meteor/fire arrow, FX explosion):
// a precomputed 25x25 quadratic-falloff kernel of COLOR_FIRE, scaled by a
// deterministic per-(tick, entity) flicker in [80%, 110%], centered on the
// sprite's VISUAL center (the glow follows an arcing fireball, not the
// ground). Returns true when at least one glow pixel was blended.
bool draw_walker_fire_glow(walker& w, viewscreen* vs);

// Screen-shake camera jitter for the current render tick: dx/dy land in
// [-strength, +strength], a pure integer-hash function of the frame tick (no
// rand(), no clock), so a reset + replay reproduces the same jolts. strength
// <= 0 yields (0, 0). The caller (viewscreen::redraw) offsets topx/topy for
// one frame and restores them before publishing the render sample.
void effects_screen_shake_offset(int strength, int& dx, int& dy);

#ifdef TESTING
// Reset the frame tick, force noise/kernel regeneration and clear the
// per-entity store so tests replay every effect deterministically.
void effects_reset_for_testing();
// Per-entity render store introspection: tracked entity count and the
// number of stored positions for one entity id.
std::size_t effects_store_size();
std::size_t effects_store_depth(std::uint32_t entity_id);
#endif
