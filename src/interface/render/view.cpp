/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//view.cpp

/* ChangeLog
	buffers: 7/31/02: *include cleanup
*/

#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/interface/input.h>
#include <openglad/interface/cheat_handler.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/core/colors.h>
#include <openglad/core/constants.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/version.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/base.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/gparser.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/interface/render/effects.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/sound.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <string>
#include <format>
#include <openglad/interface/view_sizes.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <openglad/core/test_trace.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
  #ifdef __ASYNCIFY__
    #define YIELD_SLEEP(ms) emscripten_sleep(ms)
  #else
    #warning "ASYNCIFY is not enabled; YIELD_SLEEP will be a no-op."
    #define YIELD_SLEEP(ms) ((void)(ms))
  #endif
#else
  #define YIELD_SLEEP(ms) ((void)(ms))
#endif

// these are for chad's team info page
inline constexpr int VIEW_TEAM_TOP = 2;
inline constexpr int VIEW_TEAM_LEFT = 20;
inline constexpr int VIEW_TEAM_BOTTOM = 198;
inline constexpr int VIEW_TEAM_RIGHT = 280;

// Zardus: these were originally static chars but are now ints
// Now define the arrays with their default values

static constexpr std::array<int, 16> key1 = {
                 KEYCODE_w, KEYCODE_e, KEYCODE_d, KEYCODE_c,  // movements
                 KEYCODE_x, KEYCODE_z, KEYCODE_a, KEYCODE_q,
                 KEYCODE_LCTRL, KEYCODE_LALT,                    // fire & special
                 KEYCODE_TAB,                               // switch guys
                 KEYCODE_1,                                 // change special
                 KEYCODE_s,                                 // Yell
                 KEYCODE_LSHIFT,                        // Shifter
                 KEYCODE_2,                                 // Options menu
                 KEYCODE_F5,                                 // Cheat key
};

void timed_dialog(const char* message, float delay_seconds = 3.0f);

static constexpr std::array<int, 16> key2 = {
                 KEYCODE_KP_8, KEYCODE_KP_9, KEYCODE_KP_6, KEYCODE_KP_3,  // movements
                 KEYCODE_KP_2, KEYCODE_KP_1, KEYCODE_KP_4, KEYCODE_KP_7,
                 KEYCODE_KP_0, KEYCODE_KP_ENTER,                    // fire & special
                 KEYCODE_KP_PLUS,                          // switch guys
                 KEYCODE_KP_MINUS,                         // change special
                 KEYCODE_KP_5,                                // Yell
                 KEYCODE_KP_PERIOD,                                // Shifter
                 KEYCODE_KP_MULTIPLY,                         // Options menu
                 KEYCODE_F8,                                    // Cheat key
             };

static constexpr std::array<int, 16> key3 = {
                 KEYCODE_i, KEYCODE_o, KEYCODE_l, KEYCODE_PERIOD,  // movements
                 KEYCODE_COMMA, KEYCODE_m, KEYCODE_j, KEYCODE_u,
                 KEYCODE_SPACE, KEYCODE_SEMICOLON,                    // fire & special
                 KEYCODE_BACKSPACE,                               // switch guys
                 KEYCODE_7,                                 // change special
                 KEYCODE_k,                                 // Yell
                 KEYCODE_RSHIFT,                        // Shifter
                 KEYCODE_8,                                 // Options menu
                 KEYCODE_F7,                                 // Cheat key
             };

static constexpr std::array<int, 16> key4 = {
                 KEYCODE_t, KEYCODE_y, KEYCODE_h, KEYCODE_n,  // movements
                 KEYCODE_b, KEYCODE_v, KEYCODE_f, KEYCODE_r,
                 KEYCODE_5, KEYCODE_6,                    // fire & special
                 KEYCODE_EQUALS,                               // switch guys
                 KEYCODE_3,                                 // change special
                 KEYCODE_g,                                 // Yell
                 KEYCODE_MINUS,                        // Shifter
                 KEYCODE_4,                                 // Options menu
                 KEYCODE_F6,                                 // Cheat key
             };

static SimInputDebounce g_viewscreen_debounce[6] = {};

// This is for saving/loading the key preferences
Sint32 save_key_prefs();
Sint32 load_key_prefs();
// Zardus: no longer unsigned
int get_keypress();
inline constexpr const char* KEY_FILE = "keyprefs.dat";

// This only exists so we can use the array constructor
//   for our prefs object (grumble grumble)
// Zardus: these used to be static chars too
static constexpr std::array<const int*, 4> normalkeys = {
    key1.data(), key2.data(), key3.data(), key4.data()};
// Zardus: keys is a sys var (apparently) so we'll use allkeys
// allkeys now lives in GameSession::allkeys_ (Phase 5 migration).
// File-local accessor for convenience.
static inline auto& allkeys() { return og::runtime::current_session->allkeys_; }

// theprefs is now a macro defined in view.h (dereferences current_session).
// myscreen is now a macro defined in base.h (dereferences current_session).

namespace
{
inline screen* active_screen()
{
    return og::runtime::current_session->myscreen_;
}

// Glass tiles draw faint so the floor below still shows through, but the tile is
// visibly present — distinguishing glass from an empty air hole (which draws
// nothing). The PIX_GLASS bitmap is a synthesized glass pane (cyan-blue body +
// bright frame + diagonal sheen, see graphlib make_glass_tile); at this alpha
// (~39%) its tint/frame/glint read as glass while ~61% of the floor below still
// shows through. Capped at the floor's own per-floor alpha so ghosted/faded
// floors don't get brighter glass than their surroundings.
inline constexpr unsigned char kGlassAlpha = 100;

// Vertical parallax: a non-camera floor scrolls at (1 + floor_delta*kParallax)
// of the camera floor's scroll rate, so it slides relative to the camera floor
// as the player moves (a fake-3D depth cue — floors below lag, floors above
// lead, as if the camera hung high on the Z axis). Subtle by design; gated
// multifloor so single-floor rendering is byte-identical.
inline constexpr float kParallaxScroll = 0.05f;

// Per-floor scale step for vertical parallax: a floor `d` levels from the camera
// renders at scale (1 + d*kParallaxScale) about the viewport centre — floors
// below the camera shrink (d<0), floors above enlarge (d>0), as if seen from a
// high camera looking down. The faded floor is composited as ONE bitmap via an
// off-screen layer (video::floor_layer_begin/end), smoothly (bilinear) scaled,
// so there are no inter-tile seams — a more pronounced step now reads cleanly.
inline constexpr float kParallaxScale = 0.10f;

inline options* active_prefs()
{
    return og::runtime::current_session->theprefs_;
}

template <typename WalkerList>
bool contains_walker_ptr(const WalkerList& list, const walker* candidate)
{
    return std::any_of(list.begin(), list.end(),
                       [candidate](const auto& entry) {
                           return entry.get() == candidate;
                       });
}

bool control_pointer_is_live(LevelRuntimeData& level, const walker* candidate)
{
    if (candidate == nullptr)
        return false;

    return contains_walker_ptr(level.world().oblist, candidate)
        || contains_walker_ptr(level.world().fxlist, candidate)
        || contains_walker_ptr(level.world().weaplist, candidate)
        || contains_walker_ptr(level.world().dead_list, candidate);
}

walker* sanitize_control_pointer(viewscreen& view, LevelRuntimeData& level)
{
    walker* candidate = view.control;
    if (candidate != nullptr && !control_pointer_is_live(level, candidate))
        view.control = nullptr;
    return view.control;
}

void publish_primary_render_sample(const viewscreen& view,
                                   const walker* control,
                                   float interpolation_alpha,
                                   float control_worldx,
                                   float control_worldy,
                                   float control_render_x,
                                   float control_render_y,
                                   float camera_topx_float,
                                   float camera_topy_float)
{
    if (og::runtime::current_session == nullptr ||
        !og::runtime::current_session->gameplay_active_ || view.mynum != 0)
    {
        return;
    }

    screen* const game_screen = active_screen();
    if (game_screen == nullptr)
        return;

    og::runtime::RuntimeRenderSample sample;
    sample.view_index = view.mynum;
    sample.tick = game_screen->world().tick_count_;
    sample.timer_wait = game_screen->world().timer_wait;
    sample.speed_factor = game_screen->render_interpolation_speed_factor();
    sample.interpolation_alpha = interpolation_alpha;
    sample.control_worldx = control != nullptr ? control_worldx : 0.0f;
    sample.control_worldy = control != nullptr ? control_worldy : 0.0f;
    sample.control_render_x = control != nullptr ? control_render_x : 0.0f;
    sample.control_render_y = control != nullptr ? control_render_y : 0.0f;
    sample.camera_topx = view.topx;
    sample.camera_topy = view.topy;
    sample.camera_topx_float = camera_topx_float;
    sample.camera_topy_float = camera_topy_float;
    og::runtime::publish_runtime_render_sample(std::move(sample));

    og::runtime::RuntimeTraceRecord trace =
        og::runtime::make_runtime_trace_record("render", "viewscreen_redraw");
    trace.tick = game_screen->world().tick_count_;
    trace.interpolation_alpha = interpolation_alpha;
    trace.control_worldx = sample.control_worldx;
    trace.control_worldy = sample.control_worldy;
    trace.control_render_x = sample.control_render_x;
    trace.control_render_y = sample.control_render_y;
    trace.camera_topx = sample.camera_topx;
    trace.camera_topy = sample.camera_topy;
    trace.camera_topx_float = sample.camera_topx_float;
    trace.camera_topy_float = sample.camera_topy_float;
    og::runtime::emit_runtime_trace(std::move(trace));
}

// Screen shake: count the alive detonation FX (explosion / bomb families) on
// the camera floor inside this viewport, then jolt topx/topy by a
// deterministic hash-of-tick jitter of strength min(2, count). Render-only,
// gated on cfg "effects" screen_shake, and never in the level editor's
// floor-override path. The caller saves topx/topy first and restores them
// after the shaken draw.
void apply_screen_shake(viewscreen& view, GameWorld& world,
                        const walker* controlob)
{
	if (view.editor_floor_override_ >= 0 ||
	    !cfg.is_on("effects", "screen_shake"))
		return;
	const Sint32 camera_floor =
	    controlob ? static_cast<Sint32>(controlob->floor()) : 0;
	int booms = 0;
	// Detonations spawn via add_ob (-> oblist); scan fxlist too for FX
	// added through the dedicated fx path.
	auto count_list = [&](auto& list)
	{
		for (auto& uptr : list)
		{
			walker* fx = uptr.get();
			if (!fx || fx->dead() || fx->query_order() != Order::FX ||
			    (fx->family() != FAMILY_EXPLOSION && fx->family() != FAMILY_BOMB))
				continue;
			if (static_cast<Sint32>(fx->floor()) != camera_floor)
				continue;
			if (fx->xpos() + fx->sizex() <= view.topx ||
			    fx->xpos() >= view.topx + view.xview ||
			    fx->ypos() + fx->sizey() <= view.topy ||
			    fx->ypos() >= view.topy + view.yview)
				continue; // outside this viewport: too far away to feel
			++booms;
		}
	};
	count_list(world.oblist);
	count_list(world.fxlist);
	if (booms == 0)
		return;
	const int strength = booms < 2 ? booms : 2;
	int shake_dx = 0;
	int shake_dy = 0;
	effects_screen_shake_offset(strength, shake_dx, shake_dy);
	view.topx += shake_dx;
	view.topy += shake_dy;
	TRACE("effects", "screen_shake s=%d", strength);
}
} // namespace

// ************************************************************
//  VIEWSCREEN -- It's nothing like viewscreen, it just looks like it
// ************************************************************
/*
  viewscreen(char,short,short,screen)    - initializes the viewscreen data (pix = char)
  short draw()
*/

// viewscreen -- this initializes the graphics data for the viewscreen,
// as well as its graphics x and y size.  In addition, it informs
// the viewscreen of the screen object it is linked to.
viewscreen::viewscreen(short x, short y, short width,
                       short height, short whatnum)
{
	Sint32 i;

	xview = width;
	yview = height;
	topx = topy = 0;
	xloc = x;  // where to display on the physical screen
	yloc = y;
	endx = xloc+width;
	endy = yloc+height;
	control = nullptr;
	gamma = 0;
	prefsob = active_prefs();

	// Key entries ..
	mynum = whatnum;              // what viewscreen am I?
	my_team = 0;
	mykeys = allkeys()[mynum]; // assign keyboard mappings

	// Set preferences to default values
	/*
	  prefs[PREF_LIFE]  = PREF_LIFE_BOTH; // display hp/sp bars and numbers
	  prefs[PREF_SCORE] = PREF_SCORE_ON;  // display score/exp info
	  prefs[PREF_VIEW]  = PREF_VIEW_FULL; // start at full screen
	  prefs[PREF_JOY]   = PREF_NO_JOY; //default to no joystick
	  prefs[PREF_RADAR] = PREF_RADAR_ON;
	  prefs[PREF_FOES]  = PREF_FOES_ON;
	  prefs[PREF_GAMMA] = 0;
	*/
	//load_key_prefs(); // load key prefs, if present
	prefsob->load(this);

	myradar = std::make_unique<radar>(this, active_screen(), mynum);
	radarstart = 0; //the radar has not yet been started

	for (i=0; i < MAX_MESSAGES; i++)
	{
		textcycles[i] = 0;
		text_expire_ticks[i] = 0;
		textlist[i].clear(); // null message
	}

	resize(prefs[PREF_VIEW]); // Properly resize the viewscreen
}

// Destruct the viewscreen and its variables
viewscreen::~viewscreen() = default;

void viewscreen::clear()
{
	active_screen()->videobuffer.fill(0);
}

bool viewscreen::redraw()
{
	Sint32 i,j;
	Sint32 xneg = 0;
	Sint32 yneg = 0;
    float control_worldx = 0.0f;
    float control_worldy = 0.0f;
    float control_render_x = 0.0f;
    float control_render_y = 0.0f;
    float camera_topx_float = static_cast<float>(topx);
    float camera_topy_float = static_cast<float>(topy);
    LevelRuntimeData& level = active_screen()->level_runtime_data();
	walker  *controlob = sanitize_control_pointer(*this, level);
	auto* renderer = active_screen()->level_visuals_.renderer_.get();
	if (!renderer) return false;
	GameWorld& vworld = active_screen()->world();
    interpolation_alpha = query_render_interpolation_alpha();

	// check if we are partially into a grid square and require
	//   extra row
	if (controlob)
	{
        const WalkerRenderPosition control_pos =
            resolve_walker_render_position(*controlob, interpolation_alpha);
        control_worldx = control_pos.worldx;
        control_worldy = control_pos.worldy;
        control_render_x = control_pos.xpos;
        control_render_y = control_pos.ypos;
        camera_topx_float =
            control_pos.xpos -
            static_cast<float>(xview - controlob->sizex()) / 2.0f;
        camera_topy_float =
            control_pos.ypos -
            static_cast<float>(yview - controlob->sizey()) / 2.0f;
		topx = static_cast<Sint32>(camera_topx_float);
		topy = static_cast<Sint32>(camera_topy_float);
	}
	else // no control object now ..
	{
		topx = active_screen()->level_visuals_.topx;
		topy = active_screen()->level_visuals_.topy;
        camera_topx_float = static_cast<float>(topx);
        camera_topy_float = static_cast<float>(topy);
	}

	// Screen shake: a detonation (explosion / bomb FX) on the camera floor
	// inside this viewport jolts the whole draw by a deterministic
	// hash-of-tick jitter on topx/topy (render-only; the level editor's
	// floor-override path never shakes). Restored after the floor loop +
	// weather draw, so the radar and publish_primary_render_sample report
	// the UNSHAKEN camera (camera_topx_float above is captured pre-shake).
	const Sint32 unshaken_topx = topx, unshaken_topy = topy;
	apply_screen_shake(*this, vworld, controlob);

	if (topx < 0)
		xneg = 1;
	if (topy < 0)
		yneg = 1;

	//note  >> 4 is equivalent to /16 but faster, since it doesn't divide
	//likewise <<4 is equivalent to *16, but faster

	current_floor_ = (editor_floor_override_ >= 0)
	    ? editor_floor_override_
	    : (controlob ? static_cast<Sint32>(controlob->floor()) : 0);
	// Multi-floor: draw stacked floors bottom-up, interleaving each floor's tiles
	// + entities at a per-floor opacity (floors below fade with depth, the camera
	// floor is opaque, floors above are faint ghosts). The opaque camera floor
	// occludes lower floors except through "air" holes (empty graphics). Single-
	// floor levels draw exactly one opaque pass (byte-identical to pre-Z).
	{
		// The render surface persists across frames and is never cleared per
		// frame; the engine relies on opaque tiles overwriting every pixel. On an
		// upper floor the out-of-bounds border (drawn only for floor 0) and the
		// camera floor's air-hole cells are covered ONLY by sub-255 alpha blends,
		// which read-modify-write and so compound against the previous frame ->
		// flashing. Clear this view's viewport to a stable black base first.
		// Gated floor_count>1 so single-floor stays byte-identical (opaque path).
		if (vworld.floor_count() > 1)
			active_screen()->clearbuffer(xloc, yloc, xview, yview);
		// With ghosting disabled, draw only floors 0..camera (no above-ghosts).
		const bool ghosts_on = cfg.is_on("graphics", "floor_ghost");
		// Depth tint: floors below the camera composite through a cold
		// blue-grey color mod (deeper = colder); the camera floor and the
		// ghosts above stay untinted. Strength 0 is bit-identical.
		const bool tint_on = cfg.is_on("effects", "depth_tint");
		const Sint32 floor_top = (vworld.floor_count() > 1 && ghosts_on)
		    ? static_cast<Sint32>(vworld.floor_count() - 1) : current_floor_;
		for (Sint32 f = 0; f <= floor_top; ++f)
		{
			const unsigned char falpha = floor_render_alpha(static_cast<int>(f));
			const bool base_floor = (f == 0);
			// Vertical parallax: non-camera floors scroll at a slightly different
			// rate (shift, via topx/topy) AND, when faded, composite at a per-floor
			// scale about the viewport centre (shrink below / zoom above) through the
			// off-screen layer below, so they slide + recede as the player moves.
			// topx/topy restored after this floor draws; fscale/fcx/fcy -> layer_end.
			const Sint32 par_topx = topx, par_topy = topy;
			float fscale = 1.0f;
			const Sint32 fcx = xloc + xview / 2;
			const Sint32 fcy = yloc + yview / 2;
			if (vworld.floor_count() > 1 && f != current_floor_)
			{
				const float pf = static_cast<float>(f - current_floor_) * kParallaxScroll;
				topx = par_topx + static_cast<Sint32>(static_cast<float>(par_topx) * pf);
				topy = par_topy + static_cast<Sint32>(static_cast<float>(par_topy) * pf);
				fscale = 1.0f + static_cast<float>(f - current_floor_) * kParallaxScale;
			}
			// A faded/ghosted non-camera floor (alpha<255) renders 1:1 onto an
			// off-screen layer, then composites back smoothly scaled about the
			// viewport centre + faded (seam-free). The camera floor and opaque
			// (ghosting-off) floors draw straight to the screen, byte-identical.
			const bool use_layer = (vworld.floor_count() > 1 && falpha < 255);
			const unsigned char tile_alpha = use_layer ? 255 : falpha;
			if (use_layer)
				active_screen()->floor_layer_begin(xloc, yloc, xview, yview);
			PixieData& gridp = vworld.grid_for_floor(static_cast<int>(f));
			if (gridp.valid())
			{
				const unsigned short maxx = gridp.w;
				const unsigned short maxy = gridp.h;
				for (j=(topy/GRID_SIZE)-yneg;j < ((topy+(yview))/GRID_SIZE) +1; j++)
					for (i=(topx/GRID_SIZE)-xneg;i < ((topx+(xview))/GRID_SIZE) +1; i++)
					{
						if (i<0 || j<0 || i>=maxx || j>=maxy)
						{
							if (!base_floor) continue; // upper floors: transparent border
							if (j == -1 && i>-1 && i<maxx)
								renderer->draw_tile(PIX_WALLSIDE1, i*GRID_SIZE, j*GRID_SIZE, this, tile_alpha);
							else if (j == -2 && i>-1 && i<maxx)
								renderer->draw_tile(PIX_H_WALL1, i*GRID_SIZE, j*GRID_SIZE, this, tile_alpha);
							else
								renderer->draw_tile(PIX_WALLTOP_H, i*GRID_SIZE, j*GRID_SIZE, this, tile_alpha);
						}
						else
						{
						const int tile = static_cast<int>(gridp.data[i + maxx * j]);
						// On a layer the composite fades the whole floor, so tiles draw
						// opaque (full coverage); glass stays faint only on the directly
						// drawn camera floor (so the floor below shows through it).
						const unsigned char talpha = use_layer ? 255
						    : ((tile == PIX_GLASS && falpha > kGlassAlpha) ? kGlassAlpha : falpha);
						renderer->draw_tile(tile, i*GRID_SIZE, j*GRID_SIZE, this, talpha);
					}
					}
			}
			draw_floor_entities(&level, static_cast<int>(f), falpha); //radar drawn after
			if (use_layer)
			{
				// A floor d levels below the camera gets a cold blue-grey
				// mod; 255s (tint off / ghosts above) composite bit-identically.
				unsigned char tint_strength = 0;
				if (tint_on && f < current_floor_)
				{
					const int d = static_cast<int>(current_floor_) - static_cast<int>(f);
					tint_strength = static_cast<unsigned char>(d >= 2 ? 96 : 52);
					TRACE("effects", "depth_tint floor=%d", static_cast<int>(f));
				}
				active_screen()->floor_layer_end(xloc, yloc, xview, yview,
				                                 fscale, fcx, fcy, falpha,
				                                 tint_strength);
			}
			topx = par_topx; topy = par_topy; // undo the parallax shift
		}
	}
	// Weather (per the world's synced WeatherKind: cloud banks + ground
	// shadows, or full-screen rain + lightning) wherever the camera sits
	// under open sky: a multifloor TOP floor, or a single-floor level whose
	// terrain reads outdoor (render-only; gated inside on the kind and cfg
	// "effects" weather — off touches zero pixels).
	draw_cloud_overlay(this, vworld);
	topx = unshaken_topx; topy = unshaken_topy; // undo the shake
	if (controlob && !controlob->dead() && controlob->user() == mynum && prefs[PREF_RADAR] == PREF_RADAR_ON)
		myradar->draw();
	display_text();
    publish_primary_render_sample(*this,
                                  controlob,
                                  interpolation_alpha,
                                  control_worldx,
                                  control_worldy,
                                  control_render_x,
                                  control_render_y,
                                  camera_topx_float,
                                  camera_topy_float);
	return 1;

}

bool viewscreen::redraw(LevelRuntimeData* data, bool draw_radar)
{
    if (!data) return false;
	Sint32 i,j;
	Sint32 xneg = 0;
	Sint32 yneg = 0;
    float control_worldx = 0.0f;
    float control_worldy = 0.0f;
    float control_render_x = 0.0f;
    float control_render_y = 0.0f;
    float camera_topx_float = static_cast<float>(topx);
    float camera_topy_float = static_cast<float>(topy);
	walker  *controlob = sanitize_control_pointer(*this, *data);
	auto* renderer = data->level_visuals().renderer_.get();
	if (!renderer) return false;
	GameWorld& vworld = data->world();
    interpolation_alpha = query_render_interpolation_alpha();

	// check if we are partially into a grid square and require
	//   extra row
	if (controlob)
	{
        const WalkerRenderPosition control_pos =
            resolve_walker_render_position(*controlob, interpolation_alpha);
        control_worldx = control_pos.worldx;
        control_worldy = control_pos.worldy;
        control_render_x = control_pos.xpos;
        control_render_y = control_pos.ypos;
        camera_topx_float =
            control_pos.xpos -
            static_cast<float>(xview - controlob->sizex()) / 2.0f;
        camera_topy_float =
            control_pos.ypos -
            static_cast<float>(yview - controlob->sizey()) / 2.0f;
		topx = static_cast<Sint32>(camera_topx_float);
		topy = static_cast<Sint32>(camera_topy_float);
	}
	else // no control object now ..
	{
		topx = data->level_visuals().topx;
		topy = data->level_visuals().topy;
        camera_topx_float = static_cast<float>(topx);
        camera_topy_float = static_cast<float>(topy);
	}

	// See the no-arg redraw(): shake the whole draw on nearby detonations,
	// restored after the weather draw so the radar and the published render
	// sample report the UNSHAKEN camera (the floats above are pre-shake).
	const Sint32 unshaken_topx = topx, unshaken_topy = topy;
	apply_screen_shake(*this, vworld, controlob);

	if (topx < 0)
		xneg = 1;
	if (topy < 0)
		yneg = 1;

	//note  >> 4 is equivalent to /16 but faster, since it doesn't divide
	//likewise <<4 is equivalent to *16, but faster

	current_floor_ = (editor_floor_override_ >= 0)
	    ? editor_floor_override_
	    : (controlob ? static_cast<Sint32>(controlob->floor()) : 0);
	// Multi-floor: draw stacked floors bottom-up with per-floor opacity and
	// interleaved entities (see the no-arg redraw() for the rationale). Single-
	// floor draws one opaque pass (byte-identical).
	{
		// See the no-arg redraw(): clear this view's viewport to black before the
		// stacked-floor alpha blends so multi-floor composites against a stable
		// base (else the OOB border / air holes shimmer). Gated floor_count>1.
		if (vworld.floor_count() > 1)
			active_screen()->clearbuffer(xloc, yloc, xview, yview);
		// With ghosting disabled, draw only floors 0..camera (no above-ghosts).
		const bool ghosts_on = cfg.is_on("graphics", "floor_ghost");
		// Depth tint: floors below the camera composite through a cold
		// blue-grey color mod (deeper = colder); the camera floor and the
		// ghosts above stay untinted. Strength 0 is bit-identical.
		const bool tint_on = cfg.is_on("effects", "depth_tint");
		const Sint32 floor_top = (vworld.floor_count() > 1 && ghosts_on)
		    ? static_cast<Sint32>(vworld.floor_count() - 1) : current_floor_;
		for (Sint32 f = 0; f <= floor_top; ++f)
		{
			const unsigned char falpha = floor_render_alpha(static_cast<int>(f));
			const bool base_floor = (f == 0);
			// Vertical parallax: non-camera floors scroll at a slightly different
			// rate (shift, via topx/topy) AND, when faded, composite at a per-floor
			// scale about the viewport centre (shrink below / zoom above) through the
			// off-screen layer below, so they slide + recede as the player moves.
			// topx/topy restored after this floor draws; fscale/fcx/fcy -> layer_end.
			const Sint32 par_topx = topx, par_topy = topy;
			float fscale = 1.0f;
			const Sint32 fcx = xloc + xview / 2;
			const Sint32 fcy = yloc + yview / 2;
			if (vworld.floor_count() > 1 && f != current_floor_)
			{
				const float pf = static_cast<float>(f - current_floor_) * kParallaxScroll;
				topx = par_topx + static_cast<Sint32>(static_cast<float>(par_topx) * pf);
				topy = par_topy + static_cast<Sint32>(static_cast<float>(par_topy) * pf);
				fscale = 1.0f + static_cast<float>(f - current_floor_) * kParallaxScale;
			}
			// A faded/ghosted non-camera floor (alpha<255) renders 1:1 onto an
			// off-screen layer, then composites back smoothly scaled about the
			// viewport centre + faded (seam-free). The camera floor and opaque
			// (ghosting-off) floors draw straight to the screen, byte-identical.
			const bool use_layer = (vworld.floor_count() > 1 && falpha < 255);
			const unsigned char tile_alpha = use_layer ? 255 : falpha;
			if (use_layer)
				active_screen()->floor_layer_begin(xloc, yloc, xview, yview);
			PixieData& gridp = vworld.grid_for_floor(static_cast<int>(f));
			if (gridp.valid())
			{
				const unsigned short maxx = gridp.w;
				const unsigned short maxy = gridp.h;
				for (j=(topy/GRID_SIZE)-yneg;j < ((topy+(yview))/GRID_SIZE) +1; j++)
					for (i=(topx/GRID_SIZE)-xneg;i < ((topx+(xview))/GRID_SIZE) +1; i++)
					{
						if (i<0 || j<0 || i>=maxx || j>=maxy)
						{
							if (!base_floor) continue; // upper floors: transparent border
							if (j == -1 && i>-1 && i<maxx)
								renderer->draw_tile(PIX_WALLSIDE1, i*GRID_SIZE, j*GRID_SIZE, this, tile_alpha);
							else if (j == -2 && i>-1 && i<maxx)
								renderer->draw_tile(PIX_H_WALL1, i*GRID_SIZE, j*GRID_SIZE, this, tile_alpha);
							else
								renderer->draw_tile(PIX_WALLTOP_H, i*GRID_SIZE, j*GRID_SIZE, this, tile_alpha);
						}
						else
						{
						const int tile = static_cast<int>(gridp.data[i + maxx * j]);
						// On a layer the composite fades the whole floor, so tiles draw
						// opaque (full coverage); glass stays faint only on the directly
						// drawn camera floor (so the floor below shows through it).
						const unsigned char talpha = use_layer ? 255
						    : ((tile == PIX_GLASS && falpha > kGlassAlpha) ? kGlassAlpha : falpha);
						renderer->draw_tile(tile, i*GRID_SIZE, j*GRID_SIZE, this, talpha);
					}
					}
			}
			draw_floor_entities(data, static_cast<int>(f), falpha);
			if (use_layer)
			{
				// A floor d levels below the camera gets a cold blue-grey
				// mod; 255s (tint off / ghosts above) composite bit-identically.
				unsigned char tint_strength = 0;
				if (tint_on && f < current_floor_)
				{
					const int d = static_cast<int>(current_floor_) - static_cast<int>(f);
					tint_strength = static_cast<unsigned char>(d >= 2 ? 96 : 52);
					TRACE("effects", "depth_tint floor=%d", static_cast<int>(f));
				}
				active_screen()->floor_layer_end(xloc, yloc, xview, yview,
				                                 fscale, fcx, fcy, falpha,
				                                 tint_strength);
			}
			topx = par_topx; topy = par_topy; // undo the parallax shift
		}
	}
	// See the no-arg redraw(): open-sky weather overlay, before radar/text.
	draw_cloud_overlay(this, vworld);
	topx = unshaken_topx; topy = unshaken_topy; // undo the shake
	if (draw_radar && controlob && !controlob->dead() && controlob->user() == mynum && prefs[PREF_RADAR] == PREF_RADAR_ON)
		myradar->draw(data);
	display_text();
    publish_primary_render_sample(*this,
                                  controlob,
                                  interpolation_alpha,
                                  control_worldx,
                                  control_worldy,
                                  control_render_x,
                                  control_render_y,
                                  camera_topx_float,
                                  camera_topy_float);
	return 1;

}

void viewscreen::display_text()
{
	const std::uint32_t current_tick = active_screen()->world().tick_count_;
	Sint32 i;

	for (i=0; i < MAX_MESSAGES; i++)
	{
		if (textcycles[i] > 0 &&
		    !textlist[i].empty() &&
		    current_tick <= text_expire_ticks[i])
		{
			active_screen()->text_normal.write_xy( (xview-static_cast<int>(textlist[i].size())*6)/2,
			                      30+i*6, textlist[i].c_str(), YELLOW, this );
		}
	}

	// Clean up any empty slots
	for (i=0; i < MAX_MESSAGES; i++)
		if (!textlist[i].empty() &&
		    (textcycles[i] < 1 || current_tick > text_expire_ticks[i]))
			shift_text(i); // shift text up, starting at position i
}

void viewscreen::shift_text(Sint32 row)
{
	Sint32 i;

	for (i=row; i < (MAX_MESSAGES-1) ; i++)
	{
		textlist[i] = textlist[i+1];
		textcycles[i] = textcycles[i+1];
		text_expire_ticks[i] = text_expire_ticks[i+1];
	}
	textlist[MAX_MESSAGES-1].clear();
	textcycles[MAX_MESSAGES-1] = 0;
	text_expire_ticks[MAX_MESSAGES-1] = 0;
}

bool viewscreen::refresh()
{
	// The first two values are screwy... I don't know why
	active_screen()->buffer_to_screen(xloc, yloc, xview, yview);
	return 1;
}

walker* viewscreen::find_next_control()
{
    return sim_find_next_control(active_screen()->world(), my_team);
}

short viewscreen::input(const void* native_event)
{
	if (native_event == nullptr)
		return 1;

	// Gameplay input (movement, fire, special, switch, yell, etc.) is now
	// handled by process_input() via the SDL-independent InputState snapshot.
	// This method only handles raw SDL events that cannot go through InputState:
	// debug/cheat keys that use specific SDL keycodes (F-keys, letter keys, etc.).

	Uint32 totaltime, totalframes, framespersec;
    LevelRuntimeData& level = active_screen()->level_runtime_data();
    walker* controlob = sanitize_control_pointer(*this, level);

	if (!controlob || controlob->dead())
		return 1;

	const PlayerInput& pi = ctx().input.players[mynum];

	// --- Debug keys (require raw SDL keycode checks) ---
	if (!pi.is_held(InputAction::Cheat))
	{
		if (query_key_event(KEYCODE_F3, native_event))
		{
			totaltime = (query_timer_control() - active_screen()->timerstart)/72;
			totalframes = (active_screen()->framecount);
			framespersec = totalframes / totaltime;
			std::string somemessage = std::format("{} FRAMES PER SEC", framespersec);
			active_screen()->viewob[0]->set_display_text(somemessage.c_str(), STANDARD_TEXT_TIME);
		}

		if (query_key_event(KEYCODE_F4, native_event))
			active_screen()->report_mem();
	}

	// Redisplay scenario text (Shift+/ = "?") — needs raw SDL for KEYCODE_SLASH
	if (query_key_event(KEYCODE_SLASH, native_event) && pi.is_held(InputAction::Shift) && !pi.is_held(InputAction::Cheat))
	{
		read_scenario(active_screen());
		active_screen()->redrawme = 1;
		clear_keyboard();
	}

	// --- Cheat keys (sim mutations handled in runtime layer) ---
	handle_cheat_keys(controlob, mynum, native_event, pi, active_screen());

	return 1;
}

short viewscreen::continuous_input()
{
	// Movement, fire, special, shift, and other gameplay input is now
	// handled by process_input(const InputState&), which reads from the
	// SDL-independent InputState snapshot.  This method is retained only
	// as a legacy entry-point; it is a no-op because process_input()
	// runs first in the game loop.
	return 1;
}

void reset_viewscreen_input_debounce()
{
	for (auto& debounce : g_viewscreen_debounce)
		debounce = {};
}

void viewscreen::process_input(const InputState& input_state)
{
	const PlayerInput& pi = input_state.players[mynum];

	// --- Spectator mode: only allow switching the camera target ---
	if (og::ui::is_spectator_mode(active_screen()->save_data))
	{
		// SwitchChar cycles the camera target (no ACT_CONTROL claim)
		if (!pi.was_pressed(InputAction::SwitchChar))
			g_viewscreen_debounce[mynum].changedchar = 0;
		else if (!g_viewscreen_debounce[mynum].changedchar)
		{
			g_viewscreen_debounce[mynum].changedchar = 1;
			walker* oldcontrol = control;
			if (!oldcontrol)
			{
				control = find_next_control();
				return;
			}

			bool reverse = pi.is_held(InputAction::Shift);
			short team = my_team;
			auto filter = [team](const walker* w) {
				return !w->dead() && w->query_order() == Order::Living
				       && w->team_num() == team;
			};
			walker* found = sim_cycle_next_character(
				active_screen()->world().oblist, oldcontrol, reverse, filter);
			if (found)
				control = found;
			if (control && control->dead())
				control = find_next_control();
		}
		// If the current control died or was never set, re-acquire
		if (!control || control->dead())
			control = find_next_control();
		return; // No further input processing in spectator mode
	}

	// --- Prefs key (render-layer concern: opens a UI menu) ---
	if (!pi.is_held(InputAction::Cheat))
	{
		if (pi.was_pressed(InputAction::OpenPrefs))
		{
			options_menu();
			return;
		}
	}

	if (og::runtime::current_session != nullptr &&
	    og::runtime::current_session->gameplay_active_ &&
	    og::runtime::local_transport_active(*og::runtime::current_session))
	{
		return;
	}

	// Delegate all entity-driving logic to the sim layer
	SimInputResult result = sim_process_player_input(
		pi, control, active_screen()->world(),
		mynum, my_team, g_viewscreen_debounce[mynum],
		active_screen()->special_name,
		ctx().sim_events.get());

	// Handle render-layer effects from the sim result
	if (result.endgame_requested &&
	    !og::sim::respawn_suppress_team_wipe_endgame(active_screen()->world()))
	{
		active_screen()->endgame(result.endgame_type);
		return;
	}

	if (result.control_hp_changed)
		active_screen()->world().control_hp = result.control_hp;

	if (!result.notify_text.empty())
	{
		if (result.play_sound >= 0)
			active_screen()->soundp->play_sound(static_cast<short>(result.play_sound));
		active_screen()->do_notify(result.notify_text.c_str(), result.notify_source);
	}
}

void viewscreen::set_display_text(std::string_view newtext, short numcycles)
{
	const std::uint32_t current_tick = active_screen()->world().tick_count_;
	Sint32 i;

	i = 0;
	while (i < MAX_MESSAGES && !textlist[i].empty())
		i++;
	if (i >= MAX_MESSAGES) // no room, need to scroll messages
	{
		shift_text(0); // shift up, starting at 0
		i = MAX_MESSAGES - 1;
	}
	//strcpy(infotext, newtext);
	textlist[i] = newtext;

	if (numcycles > 0)
	{
		textcycles[i] = numcycles;
		text_expire_ticks[i] =
		    current_tick + static_cast<std::uint32_t>(numcycles - 1);
	}
	else
	{
		textcycles[i] = 0;
		text_expire_ticks[i] = current_tick;
	}
}

void viewscreen::refresh_display_text(std::string_view newtext, short numcycles)
{
	for (Sint32 slot = 0; slot < MAX_MESSAGES; ++slot)
	{
		if (textlist[slot] != newtext)
			continue;

		const std::uint32_t current_tick =
		    active_screen()->world().tick_count_;
		if (numcycles > 0)
		{
			textcycles[slot] = numcycles;
			text_expire_ticks[slot] =
			    current_tick + static_cast<std::uint32_t>(numcycles - 1);
		}
		else
		{
			textcycles[slot] = 0;
			text_expire_ticks[slot] = current_tick;
		}
		return;
	}

	set_display_text(newtext, numcycles);
}

// Blanks the screen text
void viewscreen::clear_text()
{
	Sint32 i;
	for (i=0; i < MAX_MESSAGES; i++)
	{
		textlist[i].clear();
		textcycles[i] = 0;
		text_expire_ticks[i] = 0;
	}
}

unsigned char viewscreen::floor_render_alpha(int f) const
{
	if (f == current_floor_ || !cfg.is_on("graphics", "floor_ghost"))
		return 255; // camera floor (or ghosting disabled): opaque
	if (f < current_floor_)
	{
		// Floors below the camera fade with depth.
		const int a = 255 - (current_floor_ - f) * static_cast<int>(kFloorBelowAlphaStep);
		return static_cast<unsigned char>(a < static_cast<int>(kFloorBelowAlphaMin)
		                                      ? kFloorBelowAlphaMin : a);
	}
	return kFloorGhostAlpha; // floors above the camera: faint ghost
}

void viewscreen::draw_floor_effects(LevelRuntimeData* data, int floor)
{
	const bool multifloor = data->world().floor_count() > 1;
	const bool shadows_on = cfg.is_on("effects", "shadows");
	// Reflections, ripples, trails and dust belong to the camera floor, and
	// only on the direct-draw pass (the camera floor never renders through
	// the off-screen floor layer, so floor == current_floor_ implies alpha
	// 255).
	const bool camera_pass = floor == current_floor_;
	const bool reflections_on = cfg.is_on("effects", "reflections") &&
	    camera_pass;
	const bool ripples_on = cfg.is_on("effects", "ripples") && camera_pass;
	const bool trails_on = cfg.is_on("effects", "trails") && camera_pass;
	// Dust falls only when some floor exists above the camera.
	const bool dust_on = cfg.is_on("effects", "dust") && camera_pass &&
	    multifloor &&
	    current_floor_ < static_cast<Sint32>(data->world().floor_count() - 1);
	if (!shadows_on && !reflections_on && !ripples_on && !trails_on &&
	    !dust_on)
		return;
	// Ripples sit on the water surface, so they draw first: reflections and
	// the entity sprites overdraw them.
	if (ripples_on)
	{
		const PixieData& camera_grid = data->world().grid_for_floor(floor);
		int n = 0;
		for (auto& uptr : data->world().oblist)
		{
			walker* w = uptr.get();
			if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor) &&
			    draw_walker_ripples(*w, this, camera_grid))
				++n;
		}
		if (n > 0)
			TRACE("effects", "ripples floor=%d n=%d", floor, n);
	}
	if (reflections_on)
	{
		const PixieData& camera_grid = data->world().grid_for_floor(floor);
		int n = 0;
		for (auto& uptr : data->world().oblist)
		{
			walker* w = uptr.get();
			if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor) &&
			    draw_walker_reflection(*w, this, camera_grid))
				++n;
		}
		for (auto& uptr : data->world().weaplist)
		{
			walker* w = uptr.get();
			if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor) &&
			    draw_walker_reflection(*w, this, camera_grid))
				++n;
		}
		if (n > 0)
			TRACE("effects", "reflections floor=%d n=%d", floor, n);
	}
	if (shadows_on)
	{
		int n = 0;
		for (auto& uptr : data->world().oblist)
		{
			walker* w = uptr.get();
			if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor) &&
			    draw_walker_shadow(*w, this))
				++n;
		}
		for (auto& uptr : data->world().weaplist)
		{
			walker* w = uptr.get();
			if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor) &&
			    draw_walker_shadow(*w, this))
				++n;
		}
		if (n > 0)
			TRACE("effects", "shadows floor=%d n=%d", floor, n);
	}
	// Trails sit above the ground effects but below the sprites; the drawer
	// also pushes each weapon's visual position into the per-entity store.
	if (trails_on)
	{
		int n = 0;
		for (auto& uptr : data->world().weaplist)
		{
			walker* w = uptr.get();
			if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor) &&
			    draw_walker_trail(*w, this))
				++n;
		}
		if (n > 0)
			TRACE("effects", "trails floor=%d n=%d", floor, n);
	}
	// Dust shaken loose by movers on the floor DIRECTLY ABOVE the camera
	// falls in this floor's space, under the sprites.
	if (dust_on)
	{
		int n = 0;
		for (auto& uptr : data->world().oblist)
		{
			walker* w = uptr.get();
			if (w && !w->dead() && static_cast<int>(w->floor()) == floor + 1 &&
			    draw_walker_dust(*w, this))
				++n;
		}
		if (n > 0)
			TRACE("effects", "dust floor=%d n=%d", floor, n);
	}
}

void viewscreen::draw_floor_effects_post(LevelRuntimeData* data, int floor)
{
	// Fire glow reads as light, so it blends OVER the sprites — camera floor
	// only (a glow composited into a faded floor layer would dim with it).
	// The cfg gate makes "off" cost nothing.
	if (floor != current_floor_ || !cfg.is_on("effects", "fire_glow"))
		return;
	const bool multifloor = data->world().floor_count() > 1;
	int n = 0;
	auto glow_list = [&](auto& list)
	{
		for (auto& uptr : list)
		{
			walker* w = uptr.get();
			if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor) &&
			    draw_walker_fire_glow(*w, this))
				++n;
		}
	};
	glow_list(data->world().fxlist);
	glow_list(data->world().oblist);
	glow_list(data->world().weaplist);
	if (n > 0)
		TRACE("effects", "fire_glow floor=%d n=%d", floor, n);
}

void viewscreen::draw_floor_entities(LevelRuntimeData* data, int floor, unsigned char alpha)
{
	const bool multifloor = data->world().floor_count() > 1;
	draw_floor_effects(data, floor);
	for (auto& uptr : data->world().fxlist)
	{
		walker* w = uptr.get();
		if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor))
			draw_walker(*w, this, alpha);
	}
	for (auto& uptr : data->world().oblist)
	{
		walker* w = uptr.get();
		if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor))
			draw_walker(*w, this, alpha);
	}
	for (auto& uptr : data->world().weaplist)
	{
		walker* w = uptr.get();
		if (w && !w->dead() && (!multifloor || static_cast<int>(w->floor()) == floor))
			draw_walker(*w, this, alpha);
	}
	draw_floor_effects_post(data, floor);
}

bool viewscreen::draw_obs()
{
    return draw_obs(&active_screen()->level_runtime_data());
}

bool viewscreen::draw_obs(LevelRuntimeData* data)
{
	const bool multifloor = data->world().floor_count() > 1;
	const Sint32 cf = current_floor_;
	// Layer entities bottom-up to the camera floor so lower-floor entities show
	// through air holes and the camera floor draws on top. Single-floor levels
	// (multifloor==false) draw every entity in one pass, exactly as before.
	for (Sint32 f = 0; f <= cf; ++f)
	{
		draw_floor_effects(data, static_cast<int>(f));
		// First draw the special effects
		for (auto& uptr : data->world().fxlist)
		{
		    walker* w = uptr.get();
			if(w && !w->dead() && (!multifloor || w->floor() == f))
				draw_walker(*w, this);
		}

		// Now do real objects
		for (auto& uptr : data->world().oblist)
		{
		    walker* w = uptr.get();
			if(w && !w->dead() && (!multifloor || w->floor() == f))
				draw_walker(*w, this);
		}

		// Finally draw the weapons
		for (auto& uptr : data->world().weaplist)
		{
		    walker* w = uptr.get();
			if(w && !w->dead() && (!multifloor || w->floor() == f))
				draw_walker(*w, this);
		}

		draw_floor_effects_post(data, static_cast<int>(f));
	}

	return 1;
}

void viewscreen::resize(short x, short y, short length, short height)
{
	xloc = x;
	yloc = y;

	xview = length;
	yview = height;

	endx = xloc+length;
	endy = yloc+height;

	if (!myradar->bmp.empty())
		myradar->start();
	active_screen()->redrawme = 1;
}

void viewscreen::resize(char whatmode)
{
	switch (active_screen()->numviews)
	{
		case 1: //  one-player mode
			switch (whatmode)
			{
				case PREF_VIEW_PANELS:
					resize(44, 12, 232, 176); // room for score panel ..
					break;
				case PREF_VIEW_1:
					resize(64, 28, 192, 144);
					break;
				case PREF_VIEW_2:
					resize(86, 44, 148, 112);
					break;
				case PREF_VIEW_3:
					resize(106, 60, 108, 80);
					break;
				case PREF_VIEW_FULL:
				default:
					resize(T_LEFT_ONE, T_UP_ONE, T_WIDTH, T_HEIGHT);
					break;
			}
			break;
		case 2: // two-player mode
			switch (mynum)  // left or right view?
			{
				case 0:
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(4, 16, 152, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(4, 32, 152, 136);
							break;
						case PREF_VIEW_2:
							resize(4, 48, 152, 104);
							break;
						case PREF_VIEW_3:
							resize(4, 64, 152, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT);
							break;
					}
					break;
				case 1:
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(164, 16, 152, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(164, 32, 152, 136);
							break;
						case PREF_VIEW_2:
							resize(164, 48, 152, 104);
							break;
						case PREF_VIEW_3:
							resize(164, 64, 152, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HEIGHT);
							break;
					}
					break;
			} // end of mynum switch
			break;
		case 3: // 3-player mode
			switch (mynum)  // left or right view?
			{
				case 0:
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(4, 16, 100, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(4, 32, 100, 136);
							break;
						case PREF_VIEW_2:
							resize(4, 48, 100, 104);
							break;
						case PREF_VIEW_3:
							resize(4, 64, 100, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HEIGHT);
							break;
					}
					break;
				case 1:
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(216, 16, 100, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(216, 32, 100, 136);
							break;
						case PREF_VIEW_2:
							resize(216, 48, 100, 104);
							break;
						case PREF_VIEW_3:
							resize(216, 64, 100, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT);
							break;
					}
					break;
				case 2:  // 3rd player
					switch (whatmode)
					{
						case PREF_VIEW_PANELS:
							resize(112, 16, 100, 168); // room for score panel ..
							break;
						case PREF_VIEW_1:
							resize(112, 32, 100, 136);
							break;
						case PREF_VIEW_2:
							resize(112, 48, 100, 104);
							break;
						case PREF_VIEW_3:
							resize(112, 64, 100, 72);
							break;
						case PREF_VIEW_FULL:
						default:
							resize(T_LEFT_THREE, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT);
							break;
					}
					break;
			} // end of mynum switch
			break;
		case 4: // 4-player mode
		default:
			switch (mynum)  // left or right view?
			{
				case 0:
                    resize(T_LEFT_ONE, T_UP_ONE, T_HALF_WIDTH, T_HALF_HEIGHT);
					break;
				case 1:
                    resize(T_LEFT_TWO, T_UP_TWO, T_HALF_WIDTH, T_HALF_HEIGHT);
					break;
				case 2:
                    resize(T_LEFT_THREE_FOUR, T_UP_THREE, T_HALF_WIDTH, T_HALF_HEIGHT);
					break;
				case 3:
				default:
                    resize(T_LEFT_FOUR, T_UP_FOUR, T_HALF_WIDTH, T_HALF_HEIGHT);
					break;
			} // end of mynum switch
			break;
	} // end of numviews switch

} // end of resize(whatmode)

unsigned char compute_hp_color(float hp, float maxhp)
{
    if ( (hp * 3) < maxhp)
        return LOW_HP_COLOR;
    else if ( (hp * 3 / 2) < maxhp)
        return MID_HP_COLOR -3;
    else if (hp < maxhp)
        return MAX_HP_COLOR+4;
    else if (hp == maxhp)
        return HIGH_HP_COLOR+2;
    else
        return ORANGE_START;
}

unsigned char compute_mp_color(float mp, float maxmp)
{
    if ( (mp * 3) < maxmp)
        return LOW_MP_COLOR;
    else if ( (mp * 3 / 2) < maxmp)
        return MID_MP_COLOR;
    else if (mp < maxmp)
        return MAX_MP_COLOR;
    else if (mp == maxmp)
        return HIGH_MP_COLOR+3;
    else
        return WATER_START;
}

void viewscreen::view_team()
{
	view_team(VIEW_TEAM_LEFT, VIEW_TEAM_TOP,
	          VIEW_TEAM_RIGHT, VIEW_TEAM_BOTTOM);
}

void viewscreen::view_team(short left, short top, short right, short bottom)
{
	short teamnum = my_team;
	short text_down = static_cast<short>(top + 3);
	std::string message;
	unsigned char hpcolor, mpcolor, namecolor, numguys = 0;
	float hp, mp, maxhp, maxmp;
	text& mytext = active_screen()->text_normal;
	
	active_screen()->redrawme = 1;
	active_screen()->draw_button(left, top, right, bottom, 2);

	mytext.write_xy(left+5, text_down, "  Name  ", static_cast<unsigned char>(BLACK));

	mytext.write_xy(left+80, text_down, "Health", static_cast<unsigned char>(BLACK));

	mytext.write_xy(left+140, text_down, "Power", static_cast<unsigned char>(BLACK));

	mytext.write_xy(left+190, text_down, "Level", static_cast<unsigned char>(BLACK));

	text_down+=6;
    
    // Build the list of characters
    std::list<walker*> ls;
	for(auto& uptr : active_screen()->world().oblist)
	{
	    walker* w = uptr.get();
		if (w && !w->dead()
		        && w->query_order() == Order::Living
		        && w->team_num() == teamnum
		        && (!w->stats()->name.empty() || w->myguy)) //&& w->owner() == nullptr)
		{
		    ls.push_back(w);
		}
	}
	
	// NOTE: The old code sorted the list by hitpoints.  I would do that again, but I'll probably just be removing this function anyway.
    
    // Go through the list and draw the entries
    for(auto* w : ls)
	{
		if (w)
		{
			if (numguys++ > 30)
				break;
			hp = w->stats()->hitpoints();
			mp = w->stats()->magicpoints();
			maxhp = w->stats()->max_hitpoints();
			maxmp = w->stats()->max_magicpoints();

			hpcolor = compute_hp_color(hp, maxhp);

			mpcolor = compute_mp_color(mp, maxmp);

			if (w == control)
				namecolor = RED;
			else
				namecolor = BLACK;

			if (w->myguy)
				message = w->myguy->name;
			else
				message = w->stats()->name;
			mytext.write_xy(left+5, text_down, message.c_str(), static_cast<unsigned char>(namecolor));

			message = std::format("{:4.0f}/{:.0f}", ceilf(hp), maxhp);
			mytext.write_xy(left+70, text_down, message.c_str(), static_cast<unsigned char>(hpcolor));

			message = std::format("{:4.0f}/{:.0f}", ceilf(mp), maxmp);
			mytext.write_xy(left+130, text_down, message.c_str(), static_cast<unsigned char>(mpcolor));

			message = std::format("{:2d}", w->stats()->level());
			mytext.write_xy(left+195, text_down, message.c_str(), static_cast<unsigned char>(BLACK));

			text_down+=6;
		}
	}

	active_screen()->swap();

#ifndef TESTING
	Sint32 currentcycle = 0;
	Sint32 cycletime = 30000;
	while (!og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(10);  // Yield to browser event loop
		active_screen()->do_cycle(currentcycle++, cycletime);
		get_input_events(POLL);
	}
	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}
#endif

	return;
}

void viewscreen::options_menu()
{
	text& optiontext = active_screen()->text_normal;
	Sint32 gamespeed;
	std::string message, tempstr;
	Sint32 gamma_val = prefs[PREF_GAMMA];

#define LEFT_OPS 49
#define TOP_OPS 44
#define TEXT_HEIGHT 5
#define OPLINES(y) (TOP_OPS + y*(TEXT_HEIGHT+3))
#define PANEL_COLOR 13

	if (!control)
	{
	    LogError("view_options_menu_failed reason=missing_control view={}\n", mynum);
		return;  // safety check; shouldn't happen
	}

	clear_keyboard();

	// Draw the menu button
	active_screen()->draw_button(40, 40, 280, 160, 2, 1);
	active_screen()->draw_text_bar(40+4, 40+4, 280-4, 40+12);
	std::string title = std::format("Options Menu ({})", mynum+1);
	optiontext.write_xy(160-6*6, OPLINES(0)+2, title.c_str(), static_cast<unsigned char>(RED), 1);

	gamespeed = change_speed(0);
	message = std::format("Change Game Speed (+/-): {:2d}  ", gamespeed);
	optiontext.write_xy(LEFT_OPS, OPLINES(2), message.c_str(), static_cast<unsigned char>(BLACK), 1);
	switch (prefs[PREF_VIEW])
	{
		case PREF_VIEW_FULL:
			tempstr = "Full Screen";
			break;
		case PREF_VIEW_PANELS:
			tempstr = "Large";
			break;
		case PREF_VIEW_1:
			tempstr = "Medium";
			break;
		case PREF_VIEW_2:
			tempstr = "Small";
			break;
		case PREF_VIEW_3:
			tempstr = "Tiny";
			break;
		default:
			tempstr = "Weird";
			break;
	}
	message = std::format("Change View Size ([,]) : {} ", tempstr);
	active_screen()->draw_box(LEFT_OPS, OPLINES(3), LEFT_OPS+static_cast<int>(message.size())*6, OPLINES(3)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(3), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	gamma_val = change_gamma(0);
	message = std::format("Change Brightness (<,>): {} ", gamma_val);
	active_screen()->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(4), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	if (prefs[PREF_RADAR])
		message = "Radar Display (R)      : ON ";
	else
		message = "Radar Display (R)      : OFF ";
	active_screen()->draw_box(45, OPLINES(5), 275, OPLINES(5)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(5), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	switch (prefs[PREF_LIFE])
	{
		case PREF_LIFE_TEXT:
			tempstr = "Text Only";
			break;
		case PREF_LIFE_BARS:
			tempstr = "Bars Only";
			break;
		case PREF_LIFE_BOTH:
			tempstr = "Bars and Text";
			break;
		case PREF_LIFE_OFF:
			tempstr = "Off";
			break;
		default:
		case PREF_LIFE_SMALL:
			tempstr = "On";
			break;
	}
	message = std::format("Hitpoint Display (H)   : {}", tempstr);
	active_screen()->draw_box(45, OPLINES(6), 275, OPLINES(6)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(6), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	if (prefs[PREF_FOES])
		message = "Foes Display (F)       : ON ";
	else
		message = "Foes Display (F)       : OFF ";
	active_screen()->draw_box(45, OPLINES(7), 275, OPLINES(7)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(7), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	if (prefs[PREF_SCORE])
		message = "Score Display (S)      : ON ";
	else
		message = "Score Display (S)      : OFF ";
	active_screen()->draw_box(45, OPLINES(8), 275, OPLINES(8)+6, PANEL_COLOR, 1, 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(8), message.c_str(), static_cast<unsigned char>(BLACK), 1);

	optiontext.write_xy(LEFT_OPS, OPLINES(9), "VIEW TEAM INFO (T)", static_cast<unsigned char>(BLACK), 1);

	if (active_screen()->cyclemode)
		message = "Color Cycling (C)      : ON ";
	else
		message = "Color Cycling (C)      : OFF ";
	active_screen()->draw_box(45,OPLINES(10),275,OPLINES(10)+6,PANEL_COLOR,1,1);
	optiontext.write_xy(LEFT_OPS,OPLINES(10),message.c_str(),static_cast<unsigned char>(BLACK),1);

	//if (prefs[PREF_JOY] == PREF_NO_JOY)
	if(!playerHasJoystick(mynum))
		message = "Joystick Mode (J)      : OFF ";
	else
		message = "Joystick Mode (J)      : ON ";
	active_screen()->draw_box(45,OPLINES(11),275,OPLINES(11)+6,PANEL_COLOR,1,1);
	optiontext.write_xy(LEFT_OPS,OPLINES(11),message.c_str(),static_cast<unsigned char>(BLACK),1);

	optiontext.write_xy(LEFT_OPS, OPLINES(12), "Configure controls from main menu", static_cast<unsigned char>(BLACK), 1);
	optiontext.write_xy(LEFT_OPS, OPLINES(13), "  Options -> Player Controls", static_cast<unsigned char>(BLACK), 1);

	// Draw the current screen
	active_screen()->buffer_to_screen(0, 0, 320, 200);

	// Wait for esc for now
	while (!og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(10);  // Yield to browser event loop
		get_input_events(POLL);
		if (og::runtime::current_session->keystates_[KEYSTATE_KP_PLUS]) // faster game speed
		{
			gamespeed = change_speed(1);
			message = std::format("Change Game Speed (+/-): {:2d}  ", gamespeed);
			active_screen()->draw_box(LEFT_OPS, OPLINES(2), LEFT_OPS+static_cast<int>(message.size())*6, OPLINES(2)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(2), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_KP_PLUS])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
		if (og::runtime::current_session->keystates_[KEYSTATE_KP_MINUS]) // slower game speed
		{
			gamespeed = change_speed(-1);
			message = std::format("Change Game Speed (+/-): {:2d}  ", gamespeed);
			active_screen()->draw_box(LEFT_OPS, OPLINES(2), LEFT_OPS+static_cast<int>(message.size())*6, OPLINES(2)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(2), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_KP_MINUS])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
		if (og::runtime::current_session->keystates_[KEYSTATE_LEFTBRACKET]) // smaller view size
		{
			prefs[PREF_VIEW] = prefs[PREF_VIEW]+1;
			if (prefs[PREF_VIEW] > 4)
				prefs[PREF_VIEW] = 4;
			resize(prefs[PREF_VIEW]);

			switch (prefs[PREF_VIEW])
			{
				case PREF_VIEW_FULL:
					tempstr = "Full Screen";
					break;
				case PREF_VIEW_PANELS:
					tempstr = "Large";
					break;
				case PREF_VIEW_1:
					tempstr = "Medium";
					break;
				case PREF_VIEW_2:
					tempstr = "Small";
					break;
				case PREF_VIEW_3:
					tempstr = "Tiny";
					break;
				default:
					tempstr = "Weird";
					break;
			}
			message = std::format("Change View Size ([,]) : {}       ", tempstr);
			active_screen()->draw_box(45, OPLINES(3), 275, OPLINES(3)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(3), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_LEFTBRACKET])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
		if (og::runtime::current_session->keystates_[KEYSTATE_RIGHTBRACKET]) // larger view size
		{
			prefs[PREF_VIEW] = prefs[PREF_VIEW]-1;
			if (prefs[PREF_VIEW] < 0)
				prefs[PREF_VIEW] = 0;
			resize(prefs[PREF_VIEW]);

			switch (prefs[PREF_VIEW])
			{
				case PREF_VIEW_FULL:
					tempstr = "Full Screen";
					break;
				case PREF_VIEW_PANELS:
					tempstr = "Large";
					break;
				case PREF_VIEW_1:
					tempstr = "Medium";
					break;
				case PREF_VIEW_2:
					tempstr = "Small";
					break;
				case PREF_VIEW_3:
					tempstr = "Tiny";
					break;
				default:
					tempstr = "Weird";
					break;
			}
			message = std::format("Change View Size ([,]) : {}  ", tempstr);
			active_screen()->draw_box(45, OPLINES(3), 275, OPLINES(3)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(3), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_RIGHTBRACKET])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_COMMA]) // darken screen
			{
				gamma_val = change_gamma(-2);
				prefs[PREF_GAMMA] = static_cast<signed char>(gamma_val);
				message = std::format("Change Brightness (<,>): {} ", gamma_val);
			active_screen()->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(4), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_COMMA])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_PERIOD]) // lighten screen
			{
				gamma_val = change_gamma(+2);
				prefs[PREF_GAMMA] = static_cast<signed char>(gamma_val);
				message = std::format("Change Brightness (<,>): {} ", gamma_val);
			active_screen()->draw_box(45, OPLINES(4), 275, OPLINES(4)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(4), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_PERIOD])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_r]) // toggle radar display
			{
				prefs[PREF_RADAR] = static_cast<signed char>((prefs[PREF_RADAR] + 1) % 2);
			if (prefs[PREF_RADAR])
				message = "Radar Display (R)      : ON ";
			else
				message = "Radar Display (R)      : OFF ";
			active_screen()->draw_box(45, OPLINES(5), 275, OPLINES(5)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(5), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_r])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_h]) // toggle HP display
			{
				prefs[PREF_LIFE] = static_cast<signed char>((prefs[PREF_LIFE] + 1) % 5);
			switch (prefs[PREF_LIFE])
			{
				case PREF_LIFE_TEXT:
					tempstr = "Text Only";
					break;
				case PREF_LIFE_BARS:
					tempstr = "Bars Only";
					break;
				case PREF_LIFE_BOTH:
					tempstr = "Bars and Text";
					break;
				case PREF_LIFE_OFF:
					tempstr = "Off";
					break;
				default:
				case PREF_LIFE_SMALL:
					tempstr = "On";
					break;
			}
			message = std::format("Hitpoint Display (H)   : {}", tempstr);
			active_screen()->draw_box(45, OPLINES(6), 275, OPLINES(6)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(6), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_h])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_f]) // toggle foes display
			{
				prefs[PREF_FOES] = static_cast<signed char>((prefs[PREF_FOES] + 1) % 2);
			if (prefs[PREF_FOES])
				message = "Foes Display (F)       : ON ";
			else
				message = "Foes Display (F)       : OFF ";
			active_screen()->draw_box(45, OPLINES(7), 275, OPLINES(7)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(7), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_f])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}
			if (og::runtime::current_session->keystates_[KEYSTATE_s]) // toggle score display
			{
				prefs[PREF_SCORE] = static_cast<signed char>((prefs[PREF_SCORE] + 1) % 2);
			if (prefs[PREF_SCORE])
				message = "Score Display (S)      : ON ";
			else
				message = "Score Display (S)      : OFF ";
			active_screen()->draw_box(45, OPLINES(8), 275, OPLINES(8)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(8), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_s])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}

		if (og::runtime::current_session->keystates_[KEYSTATE_t])      // View the teamlist
		{
			view_team();
			active_screen()->redraw();
			options_menu();
			return;
		}

		if (og::runtime::current_session->keystates_[KEYSTATE_c])
		{
			active_screen()->cyclemode= static_cast<short>((active_screen()->cyclemode+1) %2);
			while (og::runtime::current_session->keystates_[KEYSTATE_c])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
			if (active_screen()->cyclemode)
				message = "Color Cycling (C)      : ON ";
			else
				message = "Color Cycling (C)      : OFF ";
			active_screen()->draw_box(45,OPLINES(10),275,OPLINES(10)+6,PANEL_COLOR,1,1);
			optiontext.write_xy(LEFT_OPS,OPLINES(10),message.c_str(),static_cast<unsigned char>(BLACK),1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);

		}

		if (og::runtime::current_session->keystates_[KEYSTATE_j]) // toggle joystick display
		{
		    if(playerHasJoystick(mynum))
                disablePlayerJoystick(mynum);
		    else
                resetJoystick(mynum);
		    
		    // Update joystick display message
            if(!playerHasJoystick(mynum))
                message = "Joystick Mode (J)      : OFF ";
            else
                message = "Joystick Mode (J)      : ON ";
            active_screen()->draw_box(45,OPLINES(11),275,OPLINES(11)+6,PANEL_COLOR,1,1);
            optiontext.write_xy(LEFT_OPS,OPLINES(11),message.c_str(),static_cast<unsigned char>(BLACK),1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
            
            YIELD_SLEEP(500);
            clear_events();
		}

			if (og::runtime::current_session->keystates_[KEYSTATE_b]) // toggle button display
			{
				prefs[PREF_OVERLAY] = static_cast<signed char>((prefs[PREF_OVERLAY] + 1) % 2);
			if (prefs[PREF_OVERLAY])
				message = "Text-button Display (B): ON ";
			else
				message = "Text-button Display (B): OFF ";
			active_screen()->draw_box(45, OPLINES(13), 275, OPLINES(13)+6, PANEL_COLOR, 1, 1);
			optiontext.write_xy(LEFT_OPS, OPLINES(13), message.c_str(), static_cast<unsigned char>(BLACK), 1);
			active_screen()->buffer_to_screen(0, 0, 320, 200);
			while (og::runtime::current_session->keystates_[KEYSTATE_b])
			{
				YIELD_SLEEP(1);
				get_input_events(POLL);
			}
		}

	}  // end of wait for ESC press

	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}
	active_screen()->redrawme = 1;
	prefsob->save(this);
}


Sint32 viewscreen::change_speed(Sint32 whichway)
{
	if (whichway > 0)
	{
		active_screen()->world().timer_wait -= 2;
		if (active_screen()->world().timer_wait < 0)
			active_screen()->world().timer_wait = 0;
	}
	else if (whichway < 0)
	{
		active_screen()->world().timer_wait += 2;
		if (active_screen()->world().timer_wait > 20)
			active_screen()->world().timer_wait = 20;
	}
    if (og::runtime::current_session != nullptr)
    {
        og::runtime::current_session->pending_timer_wait_request_ =
            active_screen()->world().timer_wait;
        if (og::runtime::current_session->relay_transport_active_ &&
            active_screen()->world().timer_wait < 4 &&
            !og::runtime::current_session->relay_speed_warning_shown_)
        {
            timed_dialog("High game speed increases relay usage.", 2.5f);
            og::runtime::current_session->relay_speed_warning_shown_ = true;
        }
        else if (active_screen()->world().timer_wait >= 4)
        {
            og::runtime::current_session->relay_speed_warning_shown_ = false;
        }
    }
	return static_cast<Sint32>((20-active_screen()->world().timer_wait)/2+1);
}

Sint32 viewscreen::change_gamma(Sint32 whichway)
{
	if (whichway > 1)  // lighter
	{
		load_palette("our.pal", active_screen()->newpalette);
		adjust_palette(active_screen()->newpalette, ++gamma);
	}
	if (whichway < -1)  // darker
	{
		load_palette("our.pal", active_screen()->newpalette);
		adjust_palette(active_screen()->newpalette, --gamma);
	}
	if (whichway == -1) // set to default
	{
		gamma = 0;
		load_palette("our.pal", active_screen()->newpalette);
	}
	// So 0 just means report
	return static_cast<Sint32>(gamma);
}

// **************************************************
// Options object
// **************************************************

// Initialize allkeys from defaults, then override from keyprefs.dat if available.
// Called from GameSession constructor with direct pointer to session's allkeys_.
// File format: [allkeys[0](64B), prefs[0](10B), allkeys[1](64B), prefs[1](10B), ...]
void init_allkeys(int allkeys[][16])
{
	for (int i = 0; i < 4; i++)
		std::copy_n(normalkeys[static_cast<std::size_t>(i)], 16, allkeys[i]);

	og::io::OgFilePtr infile = og::io::og_open_read(KEY_FILE);
	if (!infile)
		return;

	for (int i = 0; i < 4; i++)
	{
		infile->read(allkeys[i], sizeof(int), 16);
		infile->seek(10, SEEK_CUR);  // skip prefs block
	}
}

options::options()
{
	int i;
	og::io::OgFilePtr infile;

	// Set up preference defaults
	for(i=0; i<4; i++)
	{
		prefs[i][PREF_LIFE]  = PREF_LIFE_BOTH; // display hp/sp bars and numbers
		prefs[i][PREF_SCORE] = PREF_SCORE_ON;  // display score/exp info
		prefs[i][PREF_VIEW]  = PREF_VIEW_FULL; // start at full screen
		prefs[i][PREF_JOY]   = PREF_NO_JOY; //default to no joystick
		prefs[i][PREF_RADAR] = PREF_RADAR_ON;
		prefs[i][PREF_FOES]  = PREF_FOES_ON;
		prefs[i][PREF_GAMMA] = 0;
		prefs[i][PREF_OVERLAY] = PREF_OVERLAY_OFF; // no button behind text
	}

	infile = og::io::og_open_read(KEY_FILE);

	if (!infile) // failed to read
		return;

	// Read the blobs of data ..
	for (i=0; i < 4; i++)
	{
		// Skip allkeys data — now loaded separately by init_allkeys()
		infile->seek(16 * sizeof(int), SEEK_CUR);
		infile->read(prefs[i], 10, 1);
	}
	return;
}

// It DOESN'T actually LOAD (tee hee), it only queries
//  the prefs object... but the stupid view objects
//  don't know that... don't tell them!
short options::load(viewscreen *viewp)
{
	short prefnum = viewp->mynum;
	if (prefnum < 0 || prefnum >= 4) return 0;  // prefs/allkeys are sized for 4 views
	// Yes, we are ACTUALLY COPYING the data
	std::copy_n(prefs[prefnum], 10, viewp->prefs);
	std::copy_n(allkeys()[prefnum], 16, viewp->mykeys);
	return 1;
}


// This time, we actually DO access the file since the
//   bloke playing the game might decide to quit or
//   turn off the computer at any time and then
//   wonder later, "Where'd my prefs go! Bly'me!"
short options::save(viewscreen *viewp)
{
	short prefnum = viewp->mynum;
	Sint32 i;
	og::io::OgFilePtr outfile;

	// Yes, we are ACTUALLY COPYING the data
	std::copy_n(viewp->prefs, 10, prefs[prefnum]);
	std::copy_n(viewp->mykeys, 16, allkeys()[prefnum]);

	outfile = og::io::og_open_write(KEY_FILE);

	if (!outfile) // failed to write
		return 0;

	// Write the blobs of data ..
	for (i=0; i < 4; i++)
	{
		outfile->write(allkeys()[i], 16 * sizeof(int), 1);
		outfile->write(prefs[i], 10, 1);
	}

	return 1;
}

options::~options()
{}

/*
 
// save_key_prefs saves the state of all the player key preferences
// to the binary file KEY_FILE (currently keyprefs.dat)
// Returns success or failure
Sint32 save_key_prefs()
{
  Sint32 i;
  char *keypointer;
  FILE *outfile;
 
  outfile = open_misc_file(KEY_FILE, "", "wb");
 
  if (!outfile) // failed to write
    return 0;
 
  // Write the blobs of data ..
  for (i=0; i < 4; i++)
  {
    keypointer = keys[i];
    fwrite(keypointer, 16 * sizeof(int), 1, outfile);
  }
 
  fclose(outfile);
 
  return 1; 
 
}
 
// load_key_prefs loads the state of all the player key preferences
// from the binary file KEY_FILE (currently keyprefs.dat)
// Returns success or failure
Sint32 load_key_prefs()
{
  Sint32 i;
  char *keypointer;
  FILE *infile;
 
  infile = open_misc_file(KEY_FILE);
 
  if (!infile) // failed to read
    return 0;
 
  // Read the blobs of data ..
  for (i=0; i < 4; i++)
  {
    keypointer = keys[i];
    fread(keypointer, 16 * sizeof(int), 1, infile);
  }
 
  fclose(infile);
  return 1; 
}
 
*/



// set_key_prefs queries the user for key preferences, and
// places them into the proper key-press array.
// It returns success or failure.
Sint32 viewscreen::set_key_prefs()
{
	text& keytext = active_screen()->text_normal;

	clear_keyboard();

	// Draw the menu button
	active_screen()->draw_button(40, 40, 280, 160, 2, 1); // same as options menu
	keytext.write_xy(160-6*6, OPLINES(0), "Keyboard Menu", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);

	keytext.write_xy(LEFT_OPS, OPLINES(2), "Press a key for 'UP':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_UP);

	keytext.write_xy(LEFT_OPS, OPLINES(3), "Press a key for 'UP-RIGHT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_UP_RIGHT);

	keytext.write_xy(LEFT_OPS, OPLINES(4), "Press a key for 'RIGHT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_RIGHT);

	keytext.write_xy(LEFT_OPS, OPLINES(5), "Press a key for 'DOWN-RIGHT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_DOWN_RIGHT);

	keytext.write_xy(LEFT_OPS, OPLINES(6), "Press a key for 'DOWN':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_DOWN);

	keytext.write_xy(LEFT_OPS, OPLINES(7), "Press a key for 'DOWN-LEFT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_DOWN_LEFT);

	keytext.write_xy(LEFT_OPS, OPLINES(8), "Press a key for 'LEFT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_LEFT);

	keytext.write_xy(LEFT_OPS, OPLINES(9), "Press a key for 'UP-LEFT':", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_UP_LEFT);

	// Draw the menu button; back to the top for us!
	active_screen()->draw_button(40, 40, 280, 160, 2, 1); // same as options menu
	keytext.write_xy(160-6*6, OPLINES(0), "Keyboard Menu", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);

	keytext.write_xy(LEFT_OPS, OPLINES(2), "Press your 'FIRE' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_FIRE);

	keytext.write_xy(LEFT_OPS, OPLINES(3), "Press your 'SPECIAL' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_SPECIAL);

	keytext.write_xy(LEFT_OPS, OPLINES(4), "Press your 'SPECIAL SWITCH' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_SPECIAL_SWITCH);

	keytext.write_xy(LEFT_OPS, OPLINES(5), "Press your 'YELL' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_YELL);

	keytext.write_xy(LEFT_OPS, OPLINES(6), "Press your 'SWITCHING' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_SWITCH);

	keytext.write_xy(LEFT_OPS, OPLINES(7), "Press your 'SHIFTER' key:", static_cast<unsigned char>(RED), 1);
	active_screen()->buffer_to_screen(0, 0, 320, 200);
	assignKeyFromWaitEvent(mynum, KEY_SHIFTER);

	//  keytext.write_xy(LEFT_OPS, OPLINES(8), "Press your 'MENU (PREFS)' key:", static_cast<unsigned char>(RED), 1);
	//  allkeys()[mynum][KEY_PREFS] = get_keypress();

	if (CHEAT_MODE) // are cheats enabled?
	{
		keytext.write_xy(LEFT_OPS, OPLINES(9), "Press your 'CHEATS' key:", static_cast<unsigned char>(RED), 1);
		active_screen()->buffer_to_screen(0, 0, 320, 200);
        assignKeyFromWaitEvent(mynum, KEY_CHEAT);
	}

	active_screen()->redrawme = 1;

	//  return save_key_prefs();
	return 1;
}

// Helper function to convert SDL key names to displayable text
// Only shortens long modifier names to fit in the display
// NOTE: Uses a static buffer. The returned pointer is only valid until the next call
// to this function. Callers must use the result immediately before calling again.
static const char* get_key_display_name(int keycode)
{
	static std::string buffer;
	std::string sname = query_key_name(keycode);

	// Map arrow keys to bitmap font arrow glyphs (indices 1-4)
	if (sname == "Up") { buffer = std::string(1, '\x01'); return buffer.c_str(); }
	if (sname == "Down") { buffer = std::string(1, '\x02'); return buffer.c_str(); }
	if (sname == "Left") { buffer = std::string(1, '\x03'); return buffer.c_str(); }
	if (sname == "Right") { buffer = std::string(1, '\x04'); return buffer.c_str(); }

	// Shorten long modifier key names to fit
	if (sname == "Left Ctrl") return "LCtrl";
	if (sname == "Right Ctrl") return "RCtrl";
	if (sname == "Left Shift") return "LShift";
	if (sname == "Right Shift") return "RShift";
	if (sname == "Left Alt") return "LAlt";
	if (sname == "Right Alt") return "RAlt";
	if (sname == "Backspace") return "BkSpc";
	if (sname == "CapsLock") return "Caps";

	// Truncate if too long for display
	if (sname.size() > 10) {
		buffer = sname.substr(0, 9);
		return buffer.c_str();
	}

	return query_key_name(keycode);
}

// view_key_bindings displays the current key bindings for this player
void viewscreen::view_key_bindings()
{
	text& keytext = active_screen()->text_normal;

	clear_keyboard();

	// Draw the menu box
	active_screen()->draw_button(20, 20, 300, 180, 2, 1);
	keytext.write_xy(95, 28, "Current Key Bindings", static_cast<unsigned char>(RED), 1);

	// Movement keys label
	keytext.write_xy(55, 42, "-- Movement --", static_cast<unsigned char>(COLOR_BLUE), 1);

	// Visual 3x3 grid for directional keys
	// Row 1: UP-LEFT, UP, UP-RIGHT
	keytext.write_xy(40, 54, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_UP_LEFT]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(75, 54, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_UP]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(100, 54, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_UP_RIGHT]), static_cast<unsigned char>(BLACK), 1);

	// Row 2: LEFT, [center], RIGHT
	keytext.write_xy(40, 66, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_LEFT]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(70, 66, "---", static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(100, 66, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_RIGHT]), static_cast<unsigned char>(BLACK), 1);

	// Row 3: DOWN-LEFT, DOWN, DOWN-RIGHT
	keytext.write_xy(40, 78, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_DOWN_LEFT]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(75, 78, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_DOWN]), static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(100, 78, get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_DOWN_RIGHT]), static_cast<unsigned char>(BLACK), 1);

	// Action keys label
	keytext.write_xy(180, 42, "-- Actions --", static_cast<unsigned char>(COLOR_BLUE), 1);

	// Action keys in right column
	std::string msg;
	msg = std::format("Fire: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_FIRE]));
	keytext.write_xy(165, 54, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	msg = std::format("Special: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_SPECIAL]));
	keytext.write_xy(165, 66, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	msg = std::format("Yell: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_YELL]));
	keytext.write_xy(165, 78, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	msg = std::format("Shifter: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_SHIFTER]));
	keytext.write_xy(165, 90, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	// Switching keys
	keytext.write_xy(55, 105, "-- Switching --", static_cast<unsigned char>(COLOR_BLUE), 1);

	msg = std::format("Switch Char: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_SWITCH]));
	keytext.write_xy(40, 117, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	msg = std::format("Switch Special: {}", get_key_display_name(og::runtime::current_session->player_keys_[mynum][KEY_SPECIAL_SWITCH]));
	keytext.write_xy(40, 129, msg.c_str(), static_cast<unsigned char>(BLACK), 1);

	// Menu key info
	keytext.write_xy(165, 117, "Options: 1", static_cast<unsigned char>(BLACK), 1);
	keytext.write_xy(165, 129, "Help: Shift+/", static_cast<unsigned char>(BLACK), 1);

	keytext.write_xy(95, 160, "Press ESC to return", static_cast<unsigned char>(RED), 1);

	active_screen()->buffer_to_screen(0, 0, 320, 200);

	// Wait for ESC
	while (!og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(10);
		get_input_events(POLL);
	}
	while (og::runtime::current_session->keystates_[KEYSTATE_ESCAPE])
	{
		YIELD_SLEEP(1);
		get_input_events(POLL);
	}

	active_screen()->redrawme = 1;
}

// Waits for a key to be pressed and then released ..
// returns this key.
int get_keypress()
{
	clear_key_press_event(); // clear any previous key
	while (!query_key_press_event())
		get_input_events(WAIT);
	return query_key();
}
