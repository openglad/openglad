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

// Build the field once: three octaves of value noise (lattice cells 64/32/16
// px, amplitudes 128/64/32) over LCG lattices, smooth-interpolated with
// wraparound so every octave tiles.
void generate_cloud_noise()
{
	cloud_noise.fill(0);
	std::uint32_t state = kNoiseSeed;
	constexpr std::array<int, 3> kCell = {64, 32, 16};
	constexpr std::array<int, 3> kAmp = {128, 64, 32};
	for (std::size_t oct = 0; oct < kCell.size(); oct++)
	{
		const int cell = kCell[oct];
		const int lat_size = kNoiseSize / cell; // power of two: 4/8/16
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
				cloud_noise[idx] = static_cast<unsigned char>(
				    cloud_noise[idx] + ((v * kAmp[oct]) >> 8));
			}
		}
	}
	cloud_noise_ready = true;
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
	glow_kernel_ready = false;
	outdoor_memo_valid = false;
	render_store.clear();
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
#endif
