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

// The current render-only effects frame tick, for callers that thread it
// into stateless per-pixel effects (the depth-fog drift in DepthFxParams).
// Never read by the sim.
std::uint32_t effects_frame_tick();

// Depth-fog patch alpha (0..255; 0 = clear) at screen pixel (x, y) for the
// given effects frame tick, `stories` floors below the camera. Samples a
// dedicated fixed-seed value-noise field (its own seed and a coarser octave
// mix than the sky clouds) drifting slowly with the tick; deeper floors fog
// up to a higher alpha cap. Deterministic and replayable after
// effects_reset_for_testing(). Backs DepthFxMode::Fog in floor_layer_end.
int depth_fog_alpha_at(int x, int y, std::uint32_t tick, int stories);

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

// Upper-floor shadow pass (the DEFAULT multifloor look): unless the player
// holds the look-up key, floors above the camera are not rendered — their
// presence reads through shadows cast down onto the camera floor instead.
// For each floor above the camera, every solid (non-PIX_AIR) tile darkens
// its footprint displaced SE by +2px per story of height (the same NW sun
// as the cloud/unit shadows) in ONE flat black blend per floor (overlapping
// tiles of one floor never double-darken; stacked floors darken additively
// and clamp in the blend). The footprint's 1px rim is dithered (checkerboard
// skip) for a soft edge. Entities on those floors cast blob shadows
// (draw_walker_blob_shadow). Called by viewscreen::redraw after the camera
// floor's entities, before weather/HUD/radar; the caller gates it to the
// ghosts-off path outside the editor's floor-override draw, and a
// floor_count<=1 world short-circuits here (single-floor byte-identity).
// Returns true when any shadow pixel was blended.
bool draw_upper_floor_shadows(viewscreen* vs, GameWorld& world);

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

// Falling-cue tracker: once per frame tick (idempotent across viewports),
// compare every alive Living entity's floor against the previous frame's and
// start a falling cue where it DROPPED via an air fall — i.e. not a Z-stair
// descent (previous cell smooths to TYPE_ZSTAIRS) and not a cross-floor
// teleport (landed beyond the sim's 4-cell landing nudge). RENDER-ONLY state
// beside the position store; the sim is never touched. Shares the "dust"
// effects key with draw_walker_dust (the caller gates both), so the OFF path
// stays byte-identical. Also records EVERY observed floor change (both
// directions) into the classification store behind
// effects_last_floor_change below, feeding the floor-glide camera.
void effects_track_air_falls(GameWorld& world);

// Classification of an entity's last observed floor change, recorded by
// effects_track_air_falls: a deliberate Z-stair step (departure cell smooths
// to TYPE_ZSTAIRS, |delta| == 1), an air fall (drop, no stair, landed within
// the sim's landing nudge — any magnitude), or a teleport (everything else).
// Render-only, like the fall cues; drives the floor-glide camera's
// per-cause easing (Teleport ⇒ snap).
enum class FloorChangeKind : std::int8_t { Stairs, Fall, Teleport };
struct FloorChange
{
	std::uint32_t tick; // effects frame tick when the change was seen
	short from;
	short to;
	FloorChangeKind kind;
};

// Last observed floor change for entity_id, classified Stairs/Fall/Teleport.
// Returns false if none recorded. Records are pruned after a few frames.
bool effects_last_floor_change(std::uint32_t entity_id, FloorChange* out);

// Draw the active falling cues that landed on `floor` (the camera floor):
// for ~8 frames after the fall, a grey 2px motion smear slides down-screen
// from above the hole to the landing feet, and the last three frames add an
// expanding landing dust puff (the ripple ellipse tables in the dust grey).
// A pure function of (cue, effects frame tick): deterministic and replayable
// after effects_reset_for_testing(). Returns true when at least one pixel
// was blended.
bool draw_fall_cues(viewscreen* vs, int floor);

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

// Last draw_upper_floor_shadows coverage-mask work counters.  One
// `replaced_coverage_queries` entry is a call the former per-pixel `covered`
// lambda would have made (including its coordinate division and bounds
// checks); the optimized path replaces it with a byte-mask lookup.  The mask
// itself samples `source_tile_probes` grid cells once while rasterizing the
// expanded viewport.  Reset at the start of every shadow-pass call.
struct UpperFloorShadowMaskStats
{
	std::size_t expanded_mask_pixels = 0;
	std::size_t source_tile_probes = 0;
	std::size_t replaced_coverage_queries = 0;
	std::size_t shadowed_pixels = 0;
};
UpperFloorShadowMaskStats upper_floor_shadow_mask_stats_for_testing();

// Per-entity render store introspection: tracked entity count and the
// number of stored positions for one entity id.
std::size_t effects_store_size();
std::size_t effects_store_depth(std::uint32_t entity_id);
// Falling-cue introspection: frames of the entity's cue still to play
// (0 = no active cue).
std::uint32_t effects_fall_cue_frames_left(std::uint32_t entity_id);
#endif
