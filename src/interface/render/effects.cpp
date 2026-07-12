/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Render-only effects: the weather overlay (cloud banks + ground shadows,
// rain + lightning) for the top floor of multifloor levels, water ripple
// rings, projectile trails + falling dust (both fed by a per-entity visual
// position store), the fire glow, the screen-shake jitter, and the shared
// reflective-tile mask. The noise field is DELIBERATELY deterministic —
// fixed seed, small LCG, no libc rand(), no wall clock — and the wind
// drift, rain fall, lightning schedule, ripple/trail/dust phases, glow
// flicker and shake jolts come only from a render tick advanced once per
// frame, so tests can reset and replay every effect exactly.

#include <openglad/interface/render/effects.h>

#include <openglad/core/colors.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{

// 256x256 power-of-two field: samples wrap with & kNoiseMask, so the cloud
// banks tile seamlessly across the whole world plane.
inline constexpr int kNoiseSize = 256;
inline constexpr int kNoiseMask = kNoiseSize - 1;
inline constexpr std::uint32_t kNoiseSeed = 0xC10DB00Fu;

// Threshold + gain shape the field into patchy banks, not uniform haze:
// most samples fall below kCloudThreshold (clear sky), the rest ramp up
// steeply to the kCloudMaxAlpha cap.
inline constexpr int kCloudThreshold = 135;
inline constexpr int kCloudGain = 3;
inline constexpr int kCloudMaxAlpha = 100;
inline constexpr int kCloudMinAlpha = 8;

// Cloud ground shadows: the same banks sampled at a fixed world offset (sun
// from the NW) and blended black at kCloudShadowScale% of the cloud alpha
// curve, so the shadows land displaced from the banks floating overhead.
inline constexpr int kCloudShadowOffsetX = 40;
inline constexpr int kCloudShadowOffsetY = 24;
inline constexpr int kCloudShadowScale = 55;

// Overhang shadows (ghosts-off multifloor look): each solid upper-floor tile
// darkens its footprint on the camera floor, displaced SE +2px per story of
// height (the same NW sun as the cloud shadows above), one flat alpha-70
// black blend per upper floor with a 1px checkerboard-dithered rim.
inline constexpr Sint32 kOverhangShadowOffsetPerStory = 2;
inline constexpr unsigned char kOverhangShadowAlpha = 70;
// Extra darkening for the inner band along a footprint boundary: slab edges
// stay readable even when the floor above covers most of the viewport (a
// uniform 27% darkening reads as nothing without a visible edge).
inline constexpr unsigned char kOverhangShadowEdgeAlpha = 50;
inline constexpr Sint32 kOverhangShadowEdgeBand = 4;

// Rain: FULL-SCREEN vertical streaks falling kRainFallSpeed px per tick —
// a WeatherKind::Rain level rains over the whole open sky (the old
// wet-threshold coupling to the cloud field is gone along with the mixed
// clouds+rain sky). The streak is LONGER than the per-tick fall on
// purpose: a 10px streak moving 3px/frame overlaps its previous position
// by 7px, so the eye reads continuous falling motion. (The original 3px
// streak at 5px/tick never overlapped itself frame to frame and read as
// random twinkling pixels — "weird rain-like glitches" — not rain.) Each
// world column cycles every 64 px of streak space; a per-(column, cell)
// hash keeps 1/4 of the cells occupied, so the downpour stays sparse. The
// streak head is bright white, the tail fades down a static pale blue
// (palette 64).
inline constexpr std::uint32_t kRainFallSpeed = 3;
inline constexpr std::uint32_t kRainPeriodMask = 63;
inline constexpr std::uint32_t kRainStreakLen = 10;
inline constexpr std::uint32_t kRainCellStride = 0x9E3779B9u; // golden ratio
inline constexpr unsigned char kRainColorHead = PURE_WHITE;
inline constexpr unsigned char kRainColorTail = 64;
inline constexpr int kRainAlphaHead = 130;
inline constexpr int kRainAlphaStep = 9;

// Snow (WeatherKind::Snow): the gentle sibling of rain. Shorter streaks
// falling slowly — a 4px streak at 1px/tick overlaps its previous position
// by 3px (75%, above rain's 70%), so flakes read as drifting motes, not
// twinkle. Slant is HALF rain's (1px per 8px down) and ALTERNATES direction
// per 32px world-column band (hash-picked), so neighboring bands drift
// opposite ways like wind eddies. Occupancy 3/8 of cells (vs rain 2/8) but
// the short streak keeps lit density ~2.3% (vs rain ~3.9%): a soft fall,
// not a downpour. Colors sit OUTSIDE the cycled bands 208-231: head
// PURE_WHITE (15), tail palette 30 (54,54,54 pale grey, static). No
// lightning — the flash block stays rain-only.
// (kRainPeriodMask and kRainCellStride are shared.)
inline constexpr std::uint32_t kSnowFallSpeed = 1;
inline constexpr std::uint32_t kSnowStreakLen = 4;
inline constexpr unsigned char kSnowColorHead = PURE_WHITE;
inline constexpr unsigned char kSnowColorTail = 30;
inline constexpr int kSnowAlphaHead = 140;
inline constexpr int kSnowAlphaStep = 15;

// Lightning (part of WeatherKind::Rain): a 2-tick full-viewport white flash
// on a fixed schedule — full strike then a fading afterglow.
inline constexpr std::uint32_t kLightningPeriod = 628;
inline constexpr unsigned char kLightningAlphaStrike = 70;
inline constexpr unsigned char kLightningAlphaFade = 35;

std::array<unsigned char, static_cast<std::size_t>(kNoiseSize) * kNoiseSize>
    cloud_noise;
bool cloud_noise_ready = false;
std::uint32_t frame_tick = 0;

// Depth-fog patches (DepthFxMode::Fog): a SECOND value-noise field over the
// same 256x256 wraparound lattice machinery as the sky clouds, but with its
// own seed and a coarser octave mix (128/32 px cells), so the fog banks
// drifting across a below-floor composite never mirror the sky. Sampled in
// SCREEN space — fog hangs between the camera and the floor, so it must not
// be glued to the parallax-sliding floor beneath it.
// Alpha curve: the two-sample average narrows the field to roughly 106..181
// (mean ~139), so the threshold sits just above the mean — about half the
// area stays clear — and the gentle 3/2 gain grades a bank from its edge
// all the way to its core instead of plateauing at the cap (which read as
// flat blobs). Deeper floors cap higher (heavier fog).
inline constexpr std::uint32_t kDepthFogSeed = 0x5EAF0061u;
inline constexpr int kFogThreshold = 134;
inline constexpr int kFogGainNum = 3;
inline constexpr int kFogGainDen = 2;
inline constexpr int kFogMinAlpha = 8;
inline constexpr int kFogAlphaCap = 58;      // one story down
inline constexpr int kFogAlphaCapDeep = 84;  // two or more stories down
std::array<unsigned char, static_cast<std::size_t>(kNoiseSize) * kNoiseSize>
    depth_noise;
bool depth_noise_ready = false;

unsigned char lcg_byte(std::uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	return static_cast<unsigned char>(state >> 24);
}

// Smoothstep fade of an 8-bit fraction (u in [0,256)), integer math.
int fade8(int u)
{
	return (u * u * (3 * 256 - 2 * u)) >> 16;
}

int lerp8(int a, int b, int u)
{
	return a + (((b - a) * u) >> 8);
}

// Build a tiling value-noise field once: the given octaves (lattice cell
// sizes + amplitudes) over LCG lattices seeded from `seed`, smooth-
// interpolated with wraparound so every octave tiles.
template <std::size_t N>
void generate_value_noise(
    std::array<unsigned char, static_cast<std::size_t>(kNoiseSize) * kNoiseSize>& field,
    std::uint32_t seed,
    const std::array<int, N>& cells, const std::array<int, N>& amps)
{
	field.fill(0);
	std::uint32_t state = seed;
	for (std::size_t oct = 0; oct < cells.size(); oct++)
	{
		const int cell = cells[oct];
		const int lat_size = kNoiseSize / cell; // power of two: 2..16
		std::array<unsigned char, 256> lat{};   // 16x16 max lattice
		for (int i = 0; i < lat_size * lat_size; i++)
			lat[static_cast<std::size_t>(i)] = lcg_byte(state);
		for (int y = 0; y < kNoiseSize; y++)
		{
			const int yi = y / cell;
			const int yi1 = (yi + 1) & (lat_size - 1);
			const int yf = fade8((y % cell) * 256 / cell);
			for (int x = 0; x < kNoiseSize; x++)
			{
				const int xi = x / cell;
				const int xi1 = (xi + 1) & (lat_size - 1);
				const int xf = fade8((x % cell) * 256 / cell);
				const int a = lat[static_cast<std::size_t>(xi + lat_size * yi)];
				const int b = lat[static_cast<std::size_t>(xi1 + lat_size * yi)];
				const int c = lat[static_cast<std::size_t>(xi + lat_size * yi1)];
				const int d = lat[static_cast<std::size_t>(xi1 + lat_size * yi1)];
				const int v = lerp8(lerp8(a, b, xf), lerp8(c, d, xf), yf);
				const std::size_t idx = static_cast<std::size_t>(x + kNoiseSize * y);
				field[idx] = static_cast<unsigned char>(
				    field[idx] + ((v * amps[oct]) >> 8));
			}
		}
	}
}

// The sky field: three octaves, cells 64/32/16 px, amplitudes 128/64/32.
void generate_cloud_noise()
{
	generate_value_noise(cloud_noise, kNoiseSeed,
	                     std::array<int, 3>{64, 32, 16},
	                     std::array<int, 3>{128, 64, 32});
	cloud_noise_ready = true;
}

// The depth-fog field: two coarser octaves (128/32 px), its own seed.
void generate_depth_noise()
{
	generate_value_noise(depth_noise, kDepthFogSeed,
	                     std::array<int, 2>{128, 32},
	                     std::array<int, 2>{160, 96});
	depth_noise_ready = true;
}

// Two drift layers: layer 1 scrolls the field slowly, layer 2 samples at
// half spatial frequency and drifts faster, so the banks evolve instead of
// sliding past as one rigid sheet. Negative world coords wrap fine under
// two's-complement & kNoiseMask.
int cloud_sample(int wx, int wy, std::uint32_t tick)
{
	const int t = static_cast<int>(tick & 0x0FFFFFFFu);
	const int x1 = (wx + t / 2) & kNoiseMask;
	const int y1 = (wy + t / 4) & kNoiseMask;
	const int x2 = ((wx >> 1) + t) & kNoiseMask;
	const int y2 = ((wy >> 1) - t / 8) & kNoiseMask;
	const int s1 = cloud_noise[static_cast<std::size_t>(x1 + kNoiseSize * y1)];
	const int s2 = cloud_noise[static_cast<std::size_t>(x2 + kNoiseSize * y2)];
	return (s1 + s2) / 2;
}

// Depth-fog drift: like cloud_sample, two layers of the depth field evolve
// against each other, but slower and along different headings (E+N vs the
// clouds' W+S), so fog banks crawl rather than race and never track the sky.
int depth_fog_sample(int x, int y, std::uint32_t tick)
{
	const int t = static_cast<int>(tick & 0x0FFFFFFFu);
	const int x1 = (x + t / 3) & kNoiseMask;
	const int y1 = (y - t / 5) & kNoiseMask;
	const int x2 = ((x >> 1) - t / 2) & kNoiseMask;
	const int y2 = ((y >> 1) + t / 7) & kNoiseMask;
	const int s1 = depth_noise[static_cast<std::size_t>(x1 + kNoiseSize * y1)];
	const int s2 = depth_noise[static_cast<std::size_t>(x2 + kNoiseSize * y2)];
	return (s1 + s2) / 2;
}

// xorshift-multiply finalizer: decorrelates the rain streak cells from the
// raw column/cell integers (no libc rand anywhere in the effects path).
std::uint32_t hash_u32(std::uint32_t v)
{
	v ^= v >> 16;
	v *= 0x7FEB352Du;
	v ^= v >> 15;
	v *= 0x846CA68Bu;
	v ^= v >> 16;
	return v;
}

// The cloud alpha curve shared by the banks and their ground shadows
// (returns 0 for clear sky below kCloudMinAlpha).
int cloud_alpha(int sample)
{
	int a = (sample - kCloudThreshold) * kCloudGain;
	if (a < kCloudMinAlpha)
		return 0;
	if (a > kCloudMaxAlpha)
		a = kCloudMaxAlpha;
	return a;
}

// Ripple rings: ellipses (ry = rx/2) expanding rx 2..8 over a 32-tick period,
// two rings half a period apart, brightness fading as they widen.
inline constexpr int kRippleMinRx = 2;
inline constexpr int kRippleMaxRx = 8;
inline constexpr int kRippleRadii = kRippleMaxRx - kRippleMinRx + 1;
inline constexpr std::uint32_t kRipplePeriod = 32;
inline constexpr int kRippleAlphaMax = 70;
inline constexpr int kRippleAlphaMin = 15;
// Knuth multiplicative hash: de-syncs neighboring entities' ripple phases.
inline constexpr std::uint32_t kRipplePhaseHash = 2654435761u;

std::array<std::vector<std::pair<int, int>>, kRippleRadii> ripple_rings;
bool ripple_rings_ready = false;

// Rasterize each ring's point offsets once, sweeping from both axes so
// neither the flat top/bottom nor the steep sides leave gaps.
void generate_ripple_rings()
{
	for (int rx = kRippleMinRx; rx <= kRippleMaxRx; rx++)
	{
		auto& ring = ripple_rings[static_cast<std::size_t>(rx - kRippleMinRx)];
		ring.clear();
		const int ry = rx / 2;
		auto push_unique = [&ring](int dx, int dy)
		{
			for (const auto& p : ring)
				if (p.first == dx && p.second == dy)
					return;
			ring.emplace_back(dx, dy);
		};
		for (int dx = -rx; dx <= rx; dx++)
		{
			const int dy = static_cast<int>(std::lround(
			    std::sqrt(static_cast<double>(rx * rx - dx * dx)) * ry / rx));
			push_unique(dx, dy);
			push_unique(dx, -dy);
		}
		for (int dy = -ry; dy <= ry; dy++)
		{
			const int dx = static_cast<int>(std::lround(
			    std::sqrt(static_cast<double>(ry * ry - dy * dy)) * rx / ry));
			push_unique(dx, dy);
			push_unique(-dx, dy);
		}
	}
	ripple_rings_ready = true;
}

// Only alive Living units whose feet actually touch the surface make rings:
// phantoms and invisible units (their FX must not give them away) and
// airborne walkers (raised by worldz) do not.
bool makes_ripples(const walker& w)
{
	if (w.query_order() != Order::Living || w.dead())
		return false;
	if (w.stats()->query_bit_flags(BIT_PHANTOM))
		return false;
	if (w.invisibility_left() > 0)
		return false;
	if (w.worldz() > 0.0f)
		return false;
	return w.sizex() > 0 && w.sizey() > 0;
}

// ---- Per-entity render store (feeds trails + dust motion detection) ----
// For each tracked entity id: the last kStoreDepth VISUAL world positions
// (worldx, worldy - worldz), newest first, with the frame tick of the newest
// push. Pushes are idempotent per tick — draw runs once per viewport, up to
// four times a frame, and every viewport must see identical history.
// Entities untouched for kStoreMaxAge ticks (died, left every view) are
// pruned in effects_advance_frame(). RENDER-ONLY state: never feeds the sim.
inline constexpr std::size_t kStoreDepth = 6;
inline constexpr std::uint32_t kStoreMaxAge = 120;

struct RenderHistory
{
	std::array<std::pair<float, float>, kStoreDepth> pos{};
	std::size_t count = 0;
	std::uint32_t last_update_tick = 0;
};

std::unordered_map<std::uint32_t, RenderHistory> render_store;

RenderHistory& store_push(std::uint32_t id, float vx, float vy)
{
	RenderHistory& h = render_store[id];
	if (h.count > 0 && h.last_update_tick == frame_tick)
		return h; // already pushed this tick (another viewport drew first)
	for (std::size_t i = std::min(h.count, kStoreDepth - 1); i > 0; i--)
		h.pos[i] = h.pos[i - 1];
	h.pos[0] = {vx, vy};
	if (h.count < kStoreDepth)
		h.count++;
	h.last_update_tick = frame_tick;
	return h;
}

// The sprite's VISUAL world-plane position: interpolated draw position with
// the worldz raise folded into y (the point the eye tracks on screen).
std::pair<float, float> visual_world_pos(walker& w, const viewscreen* vs)
{
	const WalkerRenderPosition p =
	    resolve_walker_render_position(w, vs->interpolation_alpha);
	return {p.worldx, p.worldy - w.worldz()};
}

// Trails: 2x2 dots at the previous stored positions, newest brightest.
// Stationary segments (successor closer than 1px) draw nothing, so a
// resting weapon leaves no mark.
inline constexpr int kTrailAlphaNewest = 60;
inline constexpr int kTrailAlphaOldest = 12;
inline constexpr int kTrailAlphaStep =
    (kTrailAlphaNewest - kTrailAlphaOldest) /
    static_cast<int>(kStoreDepth - 2);

// Dust: staggered falling specks under a mover on the floor above. Each
// speck loops a kDustPeriod cycle and is visible while its phase is below
// kDustFallLen, descending 1px per tick and fading out; the thirds stagger
// keeps at least one speck visible whenever the walker moves.
inline constexpr std::uint32_t kDustSpecks = 3;
inline constexpr std::uint32_t kDustPeriod = 30;
inline constexpr std::uint32_t kDustFallLen = 12;
inline constexpr std::uint32_t kDustStagger = kDustPeriod / kDustSpecks;
inline constexpr std::uint32_t kDustJitter = 6; // +/- px around the center
inline constexpr int kDustAlphaTop = 100;
inline constexpr int kDustAlphaStep = 7; // 100 -> 23 across the fall
inline constexpr int kDustDropStart = 10; // specks appear this far above wy
inline constexpr unsigned char kDustColor = 22; // static mid-grey (120,120,120)
inline constexpr float kDustMoveSq = 0.25f; // moved > 0.5px since last tick

// ---- Falling cue (render-only air-fall transition) ----
// The sim resolves an air fall INSTANTLY (walker::apply_z_motion:
// change_floor + setxy in one tick), which on screen reads as a teleport.
// This cue is pure presentation on top of that: a per-frame floor tracker
// (its own store, beside the position history) notices a Living entity's
// floor DROPPING between consecutive render frames, and — unless the old
// spot was a Z-stair tile (a deliberate descent) or the entity landed far
// away (a cross-floor teleport, beyond the sim's 4-cell A5 landing nudge) —
// plays a short smear: a grey streak sliding down-screen from "above the
// hole" to the landing feet, finished by an expanding landing dust puff
// (the ripple ring tables + the dust grey, no new primitives). Entirely
// deterministic from the effects frame tick; the sim never changes.
inline constexpr std::uint32_t kFallCueFrames = 8;
inline constexpr Sint32 kFallCueDropPx = 24;      // smear starts this far up
inline constexpr Sint32 kFallCueStreakLen = 14;   // px of fading tail
inline constexpr int kFallCueAlphaHead = 150;
inline constexpr int kFallCueAlphaStep = 9;
inline constexpr std::uint32_t kFallPuffStartAge = 5; // puff on ages 5..7
inline constexpr int kFallPuffAlphaTop = 110;
inline constexpr int kFallPuffAlphaStep = 30;
// Landing farther than the A5 nudge radius (4 cells) + a cell of slack from
// the hole is no fall — walker::teleport can change floors too.
inline constexpr float kFallCueMaxNudgePx = 5.0f * GRID_SIZE;

struct FloorTrack
{
	short floor = 0;
	float x = 0.0f;
	float y = 0.0f;
	std::uint32_t tick = 0;
};

struct FallCue
{
	std::uint32_t start_tick = 0;
	short to_floor = 0;
	float hole_x = 0.0f;  // entity top-left when it fell through (world px)
	float hole_y = 0.0f;
	float land_x = 0.0f;  // entity top-left where it landed (world px)
	float land_y = 0.0f;
	Sint32 size_x = 0;
	Sint32 size_y = 0;
};

std::unordered_map<std::uint32_t, FloorTrack> floor_track_store;
std::unordered_map<std::uint32_t, FallCue> fall_cues;
bool fall_track_ran = false;
std::uint32_t fall_track_tick = 0;
// Floor-change classification store (floor glide): every fresh floor diff
// the tracker sees — both directions — lands here as Stairs/Fall/Teleport,
// queried by viewscreen::update_floor_glide the same frame. Pruned a few
// frames later in effects_advance_frame; purely additive beside the
// FallCue outputs, which stay byte-identical.
inline constexpr std::uint32_t kFloorChangeMaxAge = 4;
std::unordered_map<std::uint32_t, FloorChange> floor_change_store;

// Push the walker's visual position into the render store for this frame
// (idempotent per tick) and report whether it moved more than half a pixel
// since the previous frame. Shared motion gate for dust and marsh ripples.
bool push_and_query_motion(walker& w, const viewscreen* vs)
{
	const auto [vx, vy] = visual_world_pos(w, vs);
	const RenderHistory& h = store_push(w.entity_id(), vx, vy);
	if (h.count < 2)
		return false; // no motion history yet
	const float dx = h.pos[0].first - h.pos[1].first;
	const float dy = h.pos[0].second - h.pos[1].second;
	return dx * dx + dy * dy > kDustMoveSq;
}

// Fire glow: a radial kernel of kFireStableColor with quadratic falloff,
// center kGlowAlphaCenter fading to zero at the rim. The radius reaches
// well past the fire sprites (~16-18px wide) — the kernel's core lands ON
// the sprite where orange-on-orange is invisible, so the part players see
// is the ring beyond the sprite edge; it needs real alpha out there.
inline constexpr int kGlowRadius = 18;
inline constexpr int kGlowSize = kGlowRadius * 2 + 1;
inline constexpr int kGlowAlphaCenter = 100;
// Refined flicker: two triangle waves — a fast flame pulse (~0.7s at the
// 72fps target) over a slower swell (~1.8s), periods incommensurate enough
// that the sum repeats only every ~5s — plus a hash jitter LERPED across
// each fast cycle. The result is a clearly VISIBLE organic pulse (roughly
// [73%, 103%] of the kernel, ~30 points trough to peak) whose per-frame
// step stays near 1% — the old per-frame hash re-roll jumped up to 30% at
// frame rate (strobe), and a ±5%/1.8s single wave read as static.
inline constexpr std::uint32_t kGlowPulsePeriod = 48;  // fast pulse ticks
inline constexpr std::uint32_t kGlowSwellPeriod = 128; // slow swell ticks
inline constexpr int kGlowFlickerBase = 76;            // percent at trough
inline constexpr int kGlowPulseAmp = 7;                // fast: peak +2*amp
inline constexpr int kGlowSwellAmp = 5;                // slow: peak +2*amp
inline constexpr std::uint32_t kGlowJitterSpan = 7;    // per-cycle, -3..+3
// COLOR_FIRE (224) is ORANGE_START — the first index of the palette band
// do_cycle rotates in-game, so anything painted with it strobes at the
// cycle rate no matter how smooth the alpha is. The palette carries a
// STATIC copy of the same fire ramp right after the band (232..236);
// 234 = (230,109,0), a mid fire orange that never cycles. Used by the
// glow kernel and the fire-family trail dots.
inline constexpr unsigned char kFireStableColor = 234;

std::array<unsigned char,
           static_cast<std::size_t>(kGlowSize) * kGlowSize> glow_kernel;
bool glow_kernel_ready = false;

void generate_glow_kernel()
{
	// r2 one past the squared radius so the rim row lands exactly on zero.
	constexpr int r2 = kGlowRadius * kGlowRadius + 1;
	for (int dy = -kGlowRadius; dy <= kGlowRadius; dy++)
		for (int dx = -kGlowRadius; dx <= kGlowRadius; dx++)
		{
			const int d2 = dx * dx + dy * dy;
			const std::size_t idx = static_cast<std::size_t>(
			    (dy + kGlowRadius) * kGlowSize + (dx + kGlowRadius));
			glow_kernel[idx] = static_cast<unsigned char>(
			    d2 >= r2 ? 0 : kGlowAlphaCenter * (r2 - d2) / r2);
		}
	glow_kernel_ready = true;
}

// Fire-family (order, family) pairs — families are per-order namespaces.
bool glows(const walker& w)
{
	if (w.dead())
		return false;
	const int fam = w.family();
	switch (w.query_order())
	{
	case Order::Living:
		return fam == FAMILY_FIREELEMENTAL;
	case Order::Weapon:
		return fam == FAMILY_FIREBALL || fam == FAMILY_METEOR ||
		    fam == FAMILY_FIRE_ARROW;
	case Order::FX:
		return fam == FAMILY_EXPLOSION;
	default:
		return false;
	}
}

} // namespace

const std::array<bool, 256>& reflective_tiles()
{
	static const std::array<bool, 256> lut = []
	{
		std::array<bool, 256> mask{};
		mask[PIX_GLASS] = true;
		mask[PIX_WATER1] = true;
		mask[PIX_WATER2] = true;
		mask[PIX_WATER3] = true;
		// Westlands: molten lava and marsh pools mirror entities too.
		mask[PIX_LAVA1] = true;
		mask[PIX_LAVA2] = true;
		mask[PIX_MARSH1] = true;
		mask[PIX_MARSH2] = true;
		return mask;
	}();
	return lut;
}

bool draw_walker_ripples(walker& w, viewscreen* vs,
                         const PixieData& camera_grid)
{
	if (!vs || w.dormant() || !makes_ripples(w) || !camera_grid.valid())
		return false;

	Sint32 xscreen = 0, yscreen = 0;
	ground_plane_anchor(w, vs, xscreen, yscreen);
	const Sint32 cx = xscreen + w.sizex() / 2;
	const Sint32 cy = yscreen + w.sizey(); // ground line just below the feet

	// Rings only where the feet stand on PURE water of the camera floor (edge
	// tiles would put rings on their grass pixels).
	const Sint32 gx = (cx + vs->topx - vs->xloc) / GRID_SIZE;
	const Sint32 gy = (cy + vs->topy - vs->yloc) / GRID_SIZE;
	if (gx < 0 || gx >= camera_grid.w || gy < 0 || gy >= camera_grid.h)
		return false;
	const unsigned char tile = camera_grid.data[gx + camera_grid.w * gy];
	if (tile != PIX_WATER1 && tile != PIX_WATER2 && tile != PIX_WATER3 &&
	    tile != PIX_MARSH1 && tile != PIX_MARSH2)
		return false;

	// Marsh is thick bog, not open water: it only ripples around a walker
	// that is actually WADING (playtest bug #14 — a standing unit ringed
	// forever). Motion comes from the render store's per-frame visual
	// positions; open water keeps its constant lap.
	if ((tile == PIX_MARSH1 || tile == PIX_MARSH2) &&
	    !push_and_query_motion(w, vs))
		return false;

	if (!ripple_rings_ready)
		generate_ripple_rings();

	screen* dest = og::runtime::current_session->myscreen_;
	const std::uint32_t phase =
	    (frame_tick + w.entity_id() * kRipplePhaseHash) % kRipplePeriod;
	bool drew = false;
	for (std::uint32_t ring = 0; ring < 2; ring++)
	{
		const std::uint32_t p =
		    (phase + ring * (kRipplePeriod / 2)) % kRipplePeriod;
		const int rx = kRippleMinRx +
		    static_cast<int>(p * static_cast<std::uint32_t>(kRippleRadii) /
		                     kRipplePeriod);
		const unsigned char a = static_cast<unsigned char>(
		    kRippleAlphaMax -
		    (kRippleAlphaMax - kRippleAlphaMin) * (rx - kRippleMinRx) /
		        (kRippleMaxRx - kRippleMinRx));
		const auto& points =
		    ripple_rings[static_cast<std::size_t>(rx - kRippleMinRx)];
		for (const auto& [dx, dy] : points)
		{
			const Sint32 x = cx + dx;
			const Sint32 y = cy + dy;
			if (x < vs->xloc || x >= vs->endx || y < vs->yloc || y >= vs->endy)
				continue;
			dest->pointb(x, y, PURE_WHITE, a);
			drew = true;
		}
	}
	return drew;
}

bool draw_stair_overlays(viewscreen* vs, const PixieData& camera_grid)
{
	if (!vs || !camera_grid.valid())
		return false;

	// Subtle triangle-wave alpha pulse (period 64 render frames) so the
	// affordance breathes instead of blinking: 96..192 out of 255.
	const std::uint32_t p = frame_tick % 64u;
	const std::uint32_t tri = p < 32u ? p : 64u - p; // 0..32
	const unsigned char pulse_alpha =
	    static_cast<unsigned char>(96u + tri * 3u);
	const unsigned char shadow_alpha =
	    static_cast<unsigned char>(pulse_alpha / 2u);

	screen* dest = og::runtime::current_session->myscreen_;

	// Visible tile range of the camera floor (same derivation as the
	// redraw tile loop, clamped to the grid).
	Sint32 gx0 = vs->topx / GRID_SIZE - (vs->topx < 0 ? 1 : 0);
	Sint32 gy0 = vs->topy / GRID_SIZE - (vs->topy < 0 ? 1 : 0);
	Sint32 gx1 = (vs->topx + vs->xview) / GRID_SIZE + 1;
	Sint32 gy1 = (vs->topy + vs->yview) / GRID_SIZE + 1;
	gx0 = std::max<Sint32>(gx0, 0);
	gy0 = std::max<Sint32>(gy0, 0);
	gx1 = std::min<Sint32>(gx1, camera_grid.w - 1);
	gy1 = std::min<Sint32>(gy1, camera_grid.h - 1);

	bool drew = false;
	auto plot = [&](Sint32 x, Sint32 y, unsigned char color,
	                unsigned char alpha, bool count)
	{
		if (x < vs->xloc || x >= vs->endx || y < vs->yloc || y >= vs->endy)
			return;
		dest->pointb(x, y, color, alpha);
		drew = drew || count;
	};

	for (Sint32 gj = gy0; gj <= gy1; ++gj)
		for (Sint32 gi = gx0; gi <= gx1; ++gi)
		{
			const unsigned char tile =
			    camera_grid.data[gi + camera_grid.w * gj];
			if (tile != PIX_ZSTAIR_UP && tile != PIX_ZSTAIR_DOWN)
				continue;
			const bool up = (tile == PIX_ZSTAIR_UP);
			// Tile's top-left corner in screen space.
			const Sint32 tx = gi * GRID_SIZE - vs->topx + vs->xloc;
			const Sint32 ty = gj * GRID_SIZE - vs->topy + vs->yloc;
			// Double chevron, 2px-thick 45-degree strokes, centered in the
			// 16x16 tile: apexes at rows 3/8 (up) or 12/7 (down). Pass 0
			// lays every (+1,+1) drop-shadow pixel, pass 1 the white
			// strokes, so no shadow ever darkens a stroke pixel.
			for (const int pass : {0, 1})
				for (const Sint32 apex : {up ? 3 : 12, up ? 8 : 7})
					for (Sint32 dx = -4; dx <= 4; ++dx)
					{
						const Sint32 ax = tx + 8 + dx;
						const Sint32 ay = ty +
						    (up ? apex + (dx < 0 ? -dx : dx)
						        : apex - (dx < 0 ? -dx : dx));
						if (pass == 0)
						{
							plot(ax + 1, ay + 1, PURE_BLACK, shadow_alpha, false);
							plot(ax + 1, ay + 2, PURE_BLACK, shadow_alpha, false);
						}
						else
						{
							plot(ax, ay, PURE_WHITE, pulse_alpha, true);
							plot(ax, ay + 1, PURE_WHITE, pulse_alpha, true);
						}
					}
		}
	return drew;
}

bool draw_upper_floor_shadows(viewscreen* vs, GameWorld& world)
{
	if (!vs || world.floor_count() <= 1)
		return false; // single-floor: byte-identical short-circuit
	const Sint32 top_floor = static_cast<Sint32>(world.floor_count()) - 1;
	if (vs->current_floor_ >= top_floor)
		return false; // camera on the top floor: nothing overhead

	screen* dest = og::runtime::current_session->myscreen_;
	bool drew = false;
	for (Sint32 f = vs->current_floor_ + 1; f <= top_floor; ++f)
	{
		const Sint32 stories = f - vs->current_floor_;
		const Sint32 offset = stories * kOverhangShadowOffsetPerStory;
		const PixieData& grid = world.grid_for_floor(static_cast<int>(f));
		if (grid.valid())
		{
			const Sint32 gw = static_cast<Sint32>(grid.w);
			const Sint32 gh = static_cast<Sint32>(grid.h);
			// Is this camera-floor world pixel inside the SE-displaced
			// footprint of a solid tile of floor f? Testing coverage (not
			// blending per tile) merges adjacent tiles into ONE flat region,
			// so overlaps within a floor never double-darken.
			auto covered = [&](Sint32 wx, Sint32 wy)
			{
				const Sint32 sx = wx - offset;
				const Sint32 sy = wy - offset;
				if (sx < 0 || sy < 0)
					return false;
				const Sint32 gi = sx / GRID_SIZE;
				const Sint32 gj = sy / GRID_SIZE;
				if (gi >= gw || gj >= gh)
					return false;
				return grid.data[gi + gw * gj] !=
				    static_cast<unsigned char>(PIX_AIR);
			};
			bool floor_drew = false;
			for (Sint32 y = vs->yloc; y < vs->endy; y++)
			{
				const Sint32 wy = y - vs->yloc + vs->topy;
				for (Sint32 x = vs->xloc; x < vs->endx; x++)
				{
					const Sint32 wx = x - vs->xloc + vs->topx;
					if (!covered(wx, wy))
						continue;
					// Soft edge: the 1px rim keeps only its checkerboard
					// half (a dither, not an extra alpha level — no requant).
					const bool rim = !covered(wx - 1, wy) ||
					    !covered(wx + 1, wy) || !covered(wx, wy - 1) ||
					    !covered(wx, wy + 1);
					if (rim && (((x + y) & 1) != 0))
						continue;
					dest->pointb(x, y, PURE_BLACK, kOverhangShadowAlpha);
					// Inner emphasis band: pixels within kOverhangShadowEdgeBand
					// of the boundary darken again, so the slab edge pops even
					// under near-total coverage (interiors, ramparts).
					const bool band = !covered(wx - kOverhangShadowEdgeBand, wy) ||
					    !covered(wx + kOverhangShadowEdgeBand, wy) ||
					    !covered(wx, wy - kOverhangShadowEdgeBand) ||
					    !covered(wx, wy + kOverhangShadowEdgeBand);
					if (band)
						dest->pointb(x, y, PURE_BLACK, kOverhangShadowEdgeAlpha);
					floor_drew = true;
				}
			}
			if (floor_drew)
			{
				TRACE("render", "overhang_shadow floor=%d",
				      static_cast<int>(f));
				drew = true;
			}
		}
		// Entities living on (or flying across) floor f read as blob
		// shadows at their spot on the camera floor.
		int blobs = 0;
		auto blob_list = [&](auto& list)
		{
			for (auto& uptr : list)
			{
				walker* w = uptr.get();
				if (w && !w->dead() && static_cast<Sint32>(w->floor()) == f &&
				    draw_walker_blob_shadow(*w, vs, stories))
					++blobs;
			}
		};
		blob_list(world.oblist);
		blob_list(world.weaplist);
		if (blobs > 0)
		{
			TRACE("render", "blob_shadow floor=%d n=%d",
			      static_cast<int>(f), blobs);
			drew = true;
		}
	}
	return drew;
}

std::uint32_t effects_frame_tick()
{
	return frame_tick;
}

int depth_fog_alpha_at(int x, int y, std::uint32_t tick, int stories)
{
	if (!depth_noise_ready)
		generate_depth_noise();
	const int cap = stories >= 2 ? kFogAlphaCapDeep : kFogAlphaCap;
	int a = (depth_fog_sample(x, y, tick) - kFogThreshold) * kFogGainNum /
	    kFogGainDen;
	if (a < kFogMinAlpha)
		return 0;
	if (a > cap)
		a = cap;
	return a;
}

void effects_advance_frame()
{
	++frame_tick;
	// Drop history for entities no viewport has drawn in kStoreMaxAge ticks
	// (died or scrolled out of every view) so the store cannot grow without
	// bound across a long level.
	for (auto it = render_store.begin(); it != render_store.end();)
	{
		if (frame_tick - it->second.last_update_tick > kStoreMaxAge)
			it = render_store.erase(it);
		else
			++it;
	}
	// Same for the fall tracker (entities gone from the world), and expired
	// falling cues (their smear + puff have fully played out).
	for (auto it = floor_track_store.begin(); it != floor_track_store.end();)
	{
		if (frame_tick - it->second.tick > kStoreMaxAge)
			it = floor_track_store.erase(it);
		else
			++it;
	}
	for (auto it = fall_cues.begin(); it != fall_cues.end();)
	{
		if (frame_tick - it->second.start_tick >= kFallCueFrames)
			it = fall_cues.erase(it);
		else
			++it;
	}
	// And the floor-change classifications (the glide trigger reads them the
	// same frame; anything older is a stale record, never animated).
	for (auto it = floor_change_store.begin(); it != floor_change_store.end();)
	{
		if (frame_tick - it->second.tick > kFloorChangeMaxAge)
			it = floor_change_store.erase(it);
		else
			++it;
	}
}

bool draw_walker_trail(walker& w, viewscreen* vs)
{
	if (!vs || w.query_order() != Order::Weapon || w.dead() || w.dormant() ||
	    w.invisibility_left() > 0)
		return false;

	const auto [vx, vy] = visual_world_pos(w, vs);
	const RenderHistory& h = store_push(w.entity_id(), vx, vy);
	if (h.count < 2)
		return false; // no history yet: nothing to trail

	// Fire-family projectiles smoke in fire colors; everything else white.
	const int fam = w.family();
	const unsigned char color =
	    (fam == FAMILY_FIREBALL || fam == FAMILY_METEOR ||
	     fam == FAMILY_FIRE_ARROW)
	    ? kFireStableColor : PURE_WHITE;

	screen* dest = og::runtime::current_session->myscreen_;
	const float half_w = static_cast<float>(w.sizex()) / 2.0f;
	const float half_h = static_cast<float>(w.sizey()) / 2.0f;
	bool drew = false;
	// pos[1..] are the previous positions, newest first: the dot alpha ramps
	// down so the oldest dot is the faintest.
	for (std::size_t i = 1; i < h.count; i++)
	{
		const float dx = h.pos[i].first - h.pos[i - 1].first;
		const float dy = h.pos[i].second - h.pos[i - 1].second;
		if (dx * dx + dy * dy < 1.0f)
			continue; // closer than 1px to its successor: skip the dot
		const unsigned char a = static_cast<unsigned char>(
		    kTrailAlphaNewest - static_cast<int>(i - 1) * kTrailAlphaStep);
		const Sint32 sx = static_cast<Sint32>(
		    h.pos[i].first + half_w - static_cast<float>(vs->topx) +
		    static_cast<float>(vs->xloc));
		const Sint32 sy = static_cast<Sint32>(
		    h.pos[i].second + half_h - static_cast<float>(vs->topy) +
		    static_cast<float>(vs->yloc));
		for (Sint32 y = sy; y < sy + 2; y++)
			for (Sint32 x = sx; x < sx + 2; x++)
			{
				if (x < vs->xloc || x >= vs->endx ||
				    y < vs->yloc || y >= vs->endy)
					continue;
				dest->pointb(x, y, color, a);
				drew = true;
			}
	}
	return drew;
}

bool draw_walker_dust(walker& w, viewscreen* vs)
{
	if (!vs || w.query_order() != Order::Living || w.dead() || w.dormant())
		return false;

	const auto [vx, vy] = visual_world_pos(w, vs);
	const RenderHistory& h = store_push(w.entity_id(), vx, vy);
	if (h.count < 2)
		return false; // no motion history yet
	const float dx = h.pos[0].first - h.pos[1].first;
	const float dy = h.pos[0].second - h.pos[1].second;
	if (dx * dx + dy * dy <= kDustMoveSq)
		return false; // standing still: no dust shakes loose

	screen* dest = og::runtime::current_session->myscreen_;
	const std::uint32_t id = w.entity_id();
	const std::uint32_t seed = hash_u32(id);
	const Sint32 cx = static_cast<Sint32>(
	    vx + static_cast<float>(w.sizex()) / 2.0f -
	    static_cast<float>(vs->topx) + static_cast<float>(vs->xloc));
	const Sint32 top = static_cast<Sint32>(
	    vy - static_cast<float>(vs->topy) + static_cast<float>(vs->yloc)) -
	    kDustDropStart;
	bool drew = false;
	for (std::uint32_t i = 0; i < kDustSpecks; i++)
	{
		const std::uint32_t k =
		    (frame_tick + seed + i * kDustStagger) % kDustPeriod;
		if (k >= kDustFallLen)
			continue; // this speck is between falls
		// Per-(entity, speck) horizontal jitter; the phase drives the fall.
		const std::uint32_t jh = hash_u32(id * kDustSpecks + i);
		const Sint32 jx =
		    static_cast<Sint32>(jh % (2 * kDustJitter + 1)) -
		    static_cast<Sint32>(kDustJitter);
		const Sint32 sx = cx + jx;
		const Sint32 sy = top + static_cast<Sint32>(k);
		const unsigned char a = static_cast<unsigned char>(
		    kDustAlphaTop - static_cast<int>(k) * kDustAlphaStep);
		for (Sint32 y = sy; y < sy + 2; y++)
			for (Sint32 x = sx; x < sx + 2; x++)
			{
				if (x < vs->xloc || x >= vs->endx ||
				    y < vs->yloc || y >= vs->endy)
					continue;
				dest->pointb(x, y, kDustColor, a);
				drew = true;
			}
	}
	return drew;
}

void effects_track_air_falls(GameWorld& world)
{
	if (fall_track_ran && fall_track_tick == frame_tick)
		return; // once per frame: every viewport must see the same cues
	fall_track_ran = true;
	fall_track_tick = frame_tick;

	for (auto& uptr : world.oblist)
	{
		walker* const w = uptr.get();
		if (w == nullptr || w->dead() || w->dormant() ||
		    w->query_order() != Order::Living)
			continue;
		const std::uint32_t id = w->entity_id();
		if (id == 0)
			continue;
		const short f = w->floor();
		const float wx = static_cast<float>(w->xpos());
		const float wy = static_cast<float>(w->ypos());

		auto it = floor_track_store.find(id);
		if (it == floor_track_store.end())
		{
			floor_track_store.emplace(id, FloorTrack{f, wx, wy, frame_tick});
			continue; // first sighting: baseline only, never a cue
		}
		FloorTrack& prev = it->second;
		// A floor CHANGE between consecutive frames (a stale track means the
		// entity was untracked meanwhile — a level load, not a fall).
		if (f != prev.floor && frame_tick - prev.tick <= 2)
		{
			// Not a Z-stair step: the stair user relocates IN PLACE from a
			// stair tile, so its previous center cell smooths to TYPE_ZSTAIRS
			// — the identical departure-cell probe reads both descents and
			// ascents (the climber departed from the stair tile too).
			// (An air faller's previous cell is the hole or the walk tile
			// right before it — never a stair.) And not a teleport: air falls
			// land straight down or within the sim's 4-cell landing nudge.
			smoother& sm = world.smoother_for_floor(prev.floor);
			const std::int32_t cx =
			    (static_cast<std::int32_t>(prev.x) + w->sizex() / 2) /
			    GRID_SIZE;
			const std::int32_t cy =
			    (static_cast<std::int32_t>(prev.y) + w->sizey() / 2) /
			    GRID_SIZE;
			const bool from_stair = sm.query_genre_x_y(cx, cy) == TYPE_ZSTAIRS;
			const bool near_hole =
			    wx - prev.x <= kFallCueMaxNudgePx &&
			    prev.x - wx <= kFallCueMaxNudgePx &&
			    wy - prev.y <= kFallCueMaxNudgePx &&
			    prev.y - wy <= kFallCueMaxNudgePx;
			// Classify the change for the floor-glide camera (additive store;
			// the FallCue grey smear below is untouched): a one-story stair
			// step is Stairs, a no-stair drop landing near the hole is a Fall
			// (any magnitude — multi-story falls), everything else (rise
			// without a stair, drop beyond the nudge, multi-story stair jump)
			// is a Teleport.
			const int delta = static_cast<int>(f) - static_cast<int>(prev.floor);
			FloorChangeKind kind = FloorChangeKind::Teleport;
			if (delta == 1)
				kind = from_stair ? FloorChangeKind::Stairs
				                  : FloorChangeKind::Teleport;
			else if (delta == -1 && from_stair)
				kind = FloorChangeKind::Stairs;
			else if (delta < 0 && !from_stair && near_hole)
				kind = FloorChangeKind::Fall;
			floor_change_store[id] = FloorChange{frame_tick, prev.floor, f, kind};
			// Cue RECORDING keeps its pre-glide gate: before floor_glide, the
			// tracker only ever ran under the dust toggle (the draw_floor_effects
			// call site), so with dust off no cue state existed. The glide's own
			// call site would otherwise warm this store with dust off, and a
			// mid-session dust toggle could draw a smear the pre-feature tree
			// never would.
			if (f < prev.floor && !from_stair && near_hole &&
			    cfg.is_on("effects", "dust"))
			{
				FallCue& cue = fall_cues[id];
				cue.start_tick = frame_tick;
				cue.to_floor = f;
				cue.hole_x = prev.x;
				cue.hole_y = prev.y;
				cue.land_x = wx;
				cue.land_y = wy;
				cue.size_x = w->sizex();
				cue.size_y = w->sizey();
				TRACE("effects", "fall_cue start id=%u from=%d to=%d",
				      id, static_cast<int>(prev.floor), static_cast<int>(f));
			}
		}
		prev = {f, wx, wy, frame_tick};
	}
}

bool effects_last_floor_change(std::uint32_t entity_id, FloorChange* out)
{
	const auto it = floor_change_store.find(entity_id);
	if (it == floor_change_store.end())
		return false;
	if (out != nullptr)
		*out = it->second;
	return true;
}

bool draw_fall_cues(viewscreen* vs, int floor)
{
	if (vs == nullptr || fall_cues.empty())
		return false;
	if (!ripple_rings_ready)
		generate_ripple_rings();

	screen* dest = og::runtime::current_session->myscreen_;
	bool drew = false;
	for (const auto& [id, cue] : fall_cues)
	{
		(void)id;
		if (static_cast<int>(cue.to_floor) != floor)
			continue;
		const std::uint32_t age = frame_tick - cue.start_tick;
		if (age >= kFallCueFrames)
			continue; // expired; effects_advance_frame prunes it

		auto plot2 = [&](Sint32 sx, Sint32 sy, unsigned char alpha)
		{
			for (Sint32 y = sy; y < sy + 2; y++)
				for (Sint32 x = sx; x < sx + 2; x++)
				{
					if (x < vs->xloc || x >= vs->endx ||
					    y < vs->yloc || y >= vs->endy)
						continue;
					dest->pointb(x, y, kDustColor, alpha);
					drew = true;
				}
		};

		// Motion smear: the streak head slides from kFallCueDropPx above the
		// hole down to the landing feet across the cue, x lerped hole ->
		// landing (the A5 nudge can displace the landing sideways), with a
		// fading tail trailing UP toward where it fell from.
		const float hole_cx =
		    cue.hole_x + static_cast<float>(cue.size_x) / 2.0f;
		const float land_cx =
		    cue.land_x + static_cast<float>(cue.size_x) / 2.0f;
		const Sint32 feet_wy =
		    static_cast<Sint32>(cue.land_y) + cue.size_y;
		const Sint32 step = static_cast<Sint32>(age) + 1; // 1..kFallCueFrames
		const Sint32 head_wy = feet_wy - kFallCueDropPx +
		    step * kFallCueDropPx / static_cast<Sint32>(kFallCueFrames);
		const Sint32 head_wx = static_cast<Sint32>(
		    hole_cx + (land_cx - hole_cx) * static_cast<float>(step) /
		        static_cast<float>(kFallCueFrames));
		for (Sint32 t = 0; t < kFallCueStreakLen; t++)
		{
			const int a = kFallCueAlphaHead - static_cast<int>(t) *
			    kFallCueAlphaStep;
			if (a <= 0)
				break;
			plot2(head_wx - 1 - vs->topx + vs->xloc,
			      head_wy - t - vs->topy + vs->yloc,
			      static_cast<unsigned char>(a));
		}

		// Landing dust puff: an expanding grey ring (the ripple ellipse
		// tables, 2x2 blocks like the dust specks so it survives the dark
		// overhang-shadowed ground) around the feet over the last three
		// cue frames.
		if (age >= kFallPuffStartAge)
		{
			const int ridx = static_cast<int>(age - kFallPuffStartAge); // 0..2
			const int rx = 4 + 2 * ridx; // 4, 6, 8: inside the ring tables
			const int a = kFallPuffAlphaTop - ridx * kFallPuffAlphaStep;
			const Sint32 pcx = static_cast<Sint32>(land_cx) - vs->topx +
			    vs->xloc;
			const Sint32 pcy = feet_wy - vs->topy + vs->yloc;
			const auto& points =
			    ripple_rings[static_cast<std::size_t>(rx - kRippleMinRx)];
			for (const auto& [dx, dy] : points)
				plot2(pcx + dx, pcy + dy, static_cast<unsigned char>(a));
		}
	}
	return drew;
}

bool draw_walker_fire_glow(walker& w, viewscreen* vs)
{
	if (!vs || w.dormant() || !glows(w))
		return false;
	if (!glow_kernel_ready)
		generate_glow_kernel();

	// The glow follows the flame, not the ground: visual center including
	// the worldz raise (an arcing fireball lights its own height).
	Sint32 xs = 0, ys = 0;
	ground_plane_anchor(w, vs, xs, ys);
	ys -= static_cast<Sint32>(w.worldz());
	const Sint32 cx = xs + w.sizex() / 2;
	const Sint32 cy = ys + w.sizey() / 2;

	// Deterministic organic pulse: fast triangle + slow swell + jitter
	// lerped across the fast cycle, each phase-offset per entity so nearby
	// fires desync. Every triangle ramps 0..period/2..0 and is scaled so
	// its peak sits 2*amp above the base.
	const std::uint32_t seed = w.entity_id() * kRipplePhaseHash;
	const std::uint32_t beat = frame_tick + ((seed >> 8) % kGlowPulsePeriod);
	const std::uint32_t phase = beat % kGlowPulsePeriod;
	const int tri = static_cast<int>(
	    phase < kGlowPulsePeriod / 2 ? phase : kGlowPulsePeriod - phase);
	const std::uint32_t sbeat =
	    frame_tick + ((seed >> 16) % kGlowSwellPeriod);
	const std::uint32_t sphase = sbeat % kGlowSwellPeriod;
	const int stri = static_cast<int>(
	    sphase < kGlowSwellPeriod / 2 ? sphase : kGlowSwellPeriod - sphase);
	// Jitter is hashed once per fast cycle and lerped to the next cycle's
	// value, so the drift is continuous at cycle boundaries.
	const std::uint32_t cycle = beat / kGlowPulsePeriod;
	const int half_span = static_cast<int>(kGlowJitterSpan / 2);
	const int j0 = static_cast<int>(
	    hash_u32(seed ^ cycle) % kGlowJitterSpan) - half_span;
	const int j1 = static_cast<int>(
	    hash_u32(seed ^ (cycle + 1)) % kGlowJitterSpan) - half_span;
	const int jitter = j0 + ((j1 - j0) * static_cast<int>(phase)) /
	    static_cast<int>(kGlowPulsePeriod);
	const int flicker = kGlowFlickerBase +
	    (tri * 4 * kGlowPulseAmp) / static_cast<int>(kGlowPulsePeriod) +
	    (stri * 4 * kGlowSwellAmp) / static_cast<int>(kGlowSwellPeriod) +
	    jitter;

	screen* dest = og::runtime::current_session->myscreen_;
	const Sint32 x0 = std::max(cx - kGlowRadius, vs->xloc);
	const Sint32 x1 = std::min(cx + kGlowRadius, vs->endx - 1);
	const Sint32 y0 = std::max(cy - kGlowRadius, vs->yloc);
	const Sint32 y1 = std::min(cy + kGlowRadius, vs->endy - 1);
	bool drew = false;
	for (Sint32 y = y0; y <= y1; y++)
		for (Sint32 x = x0; x <= x1; x++)
		{
			const std::size_t idx = static_cast<std::size_t>(
			    (y - cy + kGlowRadius) * kGlowSize + (x - cx + kGlowRadius));
			const int a = glow_kernel[idx] * flicker / 100;
			if (a <= 0)
				continue;
			dest->pointb(x, y, kFireStableColor,
			             static_cast<unsigned char>(a));
			drew = true;
		}
	return drew;
}

void effects_screen_shake_offset(int strength, int& dx, int& dy)
{
	dx = 0;
	dy = 0;
	if (strength <= 0)
		return;
	// One hash per tick: the low byte drives x, the next byte y, each mapped
	// onto the odd span [-strength, +strength]. Salted with the golden-ratio
	// stride so the jolts decorrelate from the rain cells' tick hash.
	const std::uint32_t span = static_cast<std::uint32_t>(2 * strength + 1);
	const std::uint32_t h = hash_u32(frame_tick * kRainCellStride + 1u);
	dx = static_cast<int>(h % span) - strength;
	dy = static_cast<int>((h >> 8) % span) - strength;
}

// Single-floor levels carry no Z-context to say "open sky", so weather
// falls back to reading the terrain: outdoor when at least half the tiles
// are open-country genres (grass/water/trees/dirt). Cobble stays neutral
// (a courtyard and a dungeon floor share it); walls and carpet vote indoor
// by not counting. Memoized per frame tick — the scan reruns at most once
// per frame, so in-place grid edits (level editor painting, tests refilling
// a buffer) are picked up on the next tick.
static std::uint32_t outdoor_memo_tick = 0;
static bool outdoor_memo_valid = false;
static bool outdoor_memo = false;

static bool single_floor_reads_outdoor(GameWorld& world)
{
	if (outdoor_memo_valid && outdoor_memo_tick == frame_tick)
		return outdoor_memo;
	const PixieData& grid = world.grid_for_floor(0);
	if (!grid.valid())
		return false;
	smoother& sm = world.smoother_for_floor(0);
	int outdoor = 0;
	const int total = static_cast<int>(grid.w) * static_cast<int>(grid.h);
	for (int y = 0; y < static_cast<int>(grid.h); y++)
		for (int x = 0; x < static_cast<int>(grid.w); x++)
			switch (sm.query_genre_x_y(x, y))
			{
			case TYPE_GRASS:
			case TYPE_GRASS_DARK:
			case TYPE_GRASS_LIGHT:
			case TYPE_WATER:
			case TYPE_TREES:
			case TYPE_DIRT:
			case TYPE_DIRT_DARK:
			// Westlands open-country genres: snowfields, lava plains,
			// bogs, and ash wastes all read as open sky.
			case TYPE_SNOW:
			case TYPE_LAVA:
			case TYPE_MARSH:
			case TYPE_ASH:
				outdoor++;
				break;
			default:
				break;
			}
	outdoor_memo = total > 0 && outdoor * 2 >= total;
	outdoor_memo_tick = frame_tick;
	outdoor_memo_valid = true;
	TRACE("effects", "outdoor %d/%d verdict=%d", outdoor, total,
	      outdoor_memo ? 1 : 0);
	return outdoor_memo;
}

void draw_cloud_overlay(viewscreen* vs, GameWorld& world)
{
	if (!vs)
		return;
	// The kind is WORLD state (rolled by the authoritative side, synced via
	// snapshot); this is a render-only READ. cfg "effects" weather is the
	// CLIENT-side display opt-out on top of it.
	const WeatherKind kind = world.weather();
	if (kind == WeatherKind::None)
		return;
	if (!cfg.is_on("effects", "weather"))
		return;
	const int floor_count = world.floor_count();
	if (floor_count > 1)
	{
		if (vs->current_floor_ != floor_count - 1)
			return; // under a ceiling: only the TOP floor sees sky
	}
	else if (!single_floor_reads_outdoor(world))
		return; // single-floor: weather only over outdoor terrain
	const bool clouds_on = (kind == WeatherKind::Clouds);
	const bool rain_on = (kind == WeatherKind::Rain);
	const bool snow_on = (kind == WeatherKind::Snow);
	if (clouds_on)
	{
		if (!cloud_noise_ready)
			generate_cloud_noise();
		TRACE("effects", "clouds floor=%d", static_cast<int>(vs->current_floor_));
	}
	// Per-kind streak parameters, resolved once. With the rain values every
	// expression below computes exactly the pre-snow rain arithmetic —
	// bit-identical rain is a hard requirement (render pins).
	const std::uint32_t fall = rain_on ? kRainFallSpeed : kSnowFallSpeed;
	const std::uint32_t streak_len = rain_on ? kRainStreakLen : kSnowStreakLen;
	const unsigned char color_head = rain_on ? kRainColorHead : kSnowColorHead;
	const unsigned char color_tail = rain_on ? kRainColorTail : kSnowColorTail;
	const int alpha_head = rain_on ? kRainAlphaHead : kSnowAlphaHead;
	const int alpha_step = rain_on ? kRainAlphaStep : kSnowAlphaStep;
	const std::uint32_t occupancy = rain_on ? 2u : 3u;
	screen* dest = og::runtime::current_session->myscreen_;
	bool rained = false;
	// World-anchored sampling: a world pixel keeps its cloud regardless of
	// camera scroll; only the tick moves the banks and drops the rain.
	for (Sint32 y = vs->yloc; y < vs->endy; y++)
	{
		const int wy = static_cast<int>(y - vs->yloc + vs->topy);
		for (Sint32 x = vs->xloc; x < vs->endx; x++)
		{
			const int wx = static_cast<int>(x - vs->xloc + vs->topx);
			if (clouds_on)
			{
				// Ground shadow first, so a bank blends over its own shadow
				// where the two overlap: the shadow of the bank overhead
				// lands displaced by the fixed sun offset.
				const int cast_a = cloud_alpha(cloud_sample(
				    wx + kCloudShadowOffsetX, wy + kCloudShadowOffsetY,
				    frame_tick));
				if (cast_a > 0)
					dest->pointb(x, y, PURE_BLACK,
					             static_cast<unsigned char>(
					                 cast_a * kCloudShadowScale / 100));
				const int a = cloud_alpha(cloud_sample(wx, wy, frame_tick));
				if (a > 0)
					dest->pointb(x, y, PURE_WHITE,
					             static_cast<unsigned char>(a));
			}
			if (rain_on || snow_on)
			{
				// Full-screen fall: density comes from the cell-occupancy
				// hash alone. Rain streaks lean ~14 degrees (1px right per 4
				// down, wind from the left): q = 4*wx - wy is constant along
				// that slant, so hashing q's cell keys whole slanted rails,
				// and the falling phase below makes each segment slide
				// DOWN-ALONG its rail — angled fall for free. The per-rail
				// hash de-phases neighbors and the per-cell hash leaves most
				// cells dry. Snow rails are twice as steep (1px per 8 down)
				// and alternate lean direction per 32px world-column band.
				int q;
				if (rain_on)
					q = 4 * wx - wy;
				else
				{
					const int s =
					    (hash_u32(static_cast<std::uint32_t>(wx >> 5)) & 1u)
					        ? 1 : -1;
					q = 8 * wx - s * wy;
				}
				const std::uint32_t col = hash_u32(
				    static_cast<std::uint32_t>(rain_on ? (q >> 2) : (q >> 3)));
				const std::uint32_t v = static_cast<std::uint32_t>(wy) -
				    frame_tick * fall + col;
				const std::uint32_t pos = v & kRainPeriodMask;
				if (pos < streak_len &&
				    (hash_u32(col + (v >> 6) * kRainCellStride) & 7u) <
				        occupancy)
				{
					dest->pointb(x, y,
					             pos == 0 ? color_head : color_tail,
					             static_cast<unsigned char>(
					                 alpha_head -
					                 static_cast<int>(pos) * alpha_step));
					rained = true;
				}
			}
		}
	}
	if (rained)
	{
		if (rain_on)
			TRACE("effects", "rain floor=%d",
			      static_cast<int>(vs->current_floor_));
		else
			TRACE("effects", "snow floor=%d",
			      static_cast<int>(vs->current_floor_));
	}
	if (rain_on)
	{
		// Deterministic lightning schedule: one 2-tick flash per period,
		// full strike then a fading afterglow, blended over the whole
		// viewport (only while it rains).
		const std::uint32_t strike = frame_tick % kLightningPeriod;
		if (strike < 2)
		{
			TRACE("effects", "lightning floor=%d",
			      static_cast<int>(vs->current_floor_));
			dest->draw_rect_filled(vs->xloc, vs->yloc,
			                       static_cast<Uint32>(vs->xview),
			                       static_cast<Uint32>(vs->yview), PURE_WHITE,
			                       strike == 0 ? kLightningAlphaStrike
			                                   : kLightningAlphaFade);
		}
	}
}

#ifdef TESTING
void effects_reset_for_testing()
{
	frame_tick = 0;
	cloud_noise_ready = false;
	depth_noise_ready = false;
	glow_kernel_ready = false;
	outdoor_memo_valid = false;
	render_store.clear();
	floor_track_store.clear();
	fall_cues.clear();
	floor_change_store.clear();
	fall_track_ran = false;
	fall_track_tick = 0;
}

std::size_t effects_store_size()
{
	return render_store.size();
}

std::size_t effects_store_depth(std::uint32_t entity_id)
{
	const auto it = render_store.find(entity_id);
	return it == render_store.end() ? 0 : it->second.count;
}

std::uint32_t effects_fall_cue_frames_left(std::uint32_t entity_id)
{
	const auto it = fall_cues.find(entity_id);
	if (it == fall_cues.end())
		return 0;
	const std::uint32_t age = frame_tick - it->second.start_tick;
	return age >= kFallCueFrames ? 0 : kFallCueFrames - age;
}
#endif
