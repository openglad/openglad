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
#pragma once

// Definition of VIEWSCREEN class

#include <openglad/interface/base.h>
#include <openglad/interface/level_runtime_data.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// Blocking-wait poll hook (canonical definition in openglad/interface/input.h;
// identical alias redeclaration keeps this header off input.h's include cost).
using KeyWaitPollCallback = bool (*)();

// Viewscreen-related defines
inline constexpr signed char PREF_LIFE = 0;
  inline constexpr signed char PREF_LIFE_TEXT  = 0;
  inline constexpr signed char PREF_LIFE_BARS  = 1;
  inline constexpr signed char PREF_LIFE_BOTH  = 2;
  inline constexpr signed char PREF_LIFE_SMALL = 3;
  inline constexpr signed char PREF_LIFE_OFF   = 4;
inline constexpr signed char PREF_SCORE = 1;
  inline constexpr signed char PREF_SCORE_OFF = 0;
  inline constexpr signed char PREF_SCORE_ON  = 1;
inline constexpr signed char PREF_VIEW = 2;
  inline constexpr signed char PREF_VIEW_FULL   = 0;
  inline constexpr signed char PREF_VIEW_PANELS = 1;
  inline constexpr signed char PREF_VIEW_1      = 2;
  inline constexpr signed char PREF_VIEW_2      = 3;
  inline constexpr signed char PREF_VIEW_3      = 4;
// Dead slot: keyprefs.dat's 10-byte prefs block has fixed offsets, so the
// index must stay even though nothing reads the value.
inline constexpr signed char PREF_JOY = 3;
  inline constexpr signed char PREF_NO_JOY = 0;
  inline constexpr signed char PREF_USE_JOY = 1;
inline constexpr signed char PREF_RADAR = 4;
  inline constexpr signed char PREF_RADAR_OFF   = 0;
  inline constexpr signed char PREF_RADAR_ON    = 1;
inline constexpr signed char PREF_FOES = 5;
  inline constexpr signed char PREF_FOES_OFF    = 0;
  inline constexpr signed char PREF_FOES_ON     = 1;
// Dead slot (same reason as PREF_JOY): brightness is cfg graphics/brightness
// now, applied in the palette path rather than per view.
inline constexpr signed char PREF_GAMMA = 6;
inline constexpr signed char PREF_OVERLAY = 7;
  inline constexpr signed char PREF_OVERLAY_OFF = 0;
  inline constexpr signed char PREF_OVERLAY_ON  = 1;

inline constexpr int PREF_MAX = 8;  // == 1 + highest pref ..

inline constexpr int MAX_MESSAGES = 5;  // max of 5 lines, currently

struct InputState;
class GameWorld;
class screen;
class viewscreen;
class walker;
class radar;

// Pure helper functions for HP/MP color thresholds
unsigned char compute_hp_color(float hp, float maxhp);
unsigned char compute_mp_color(float mp, float maxmp);
void reset_viewscreen_input_debounce();

// This is a child object of all viewscreens
//  It is used to load all prefs
//  because each player has their own
//  prefs.  WE ASSUME 4 PLAYERS ALWAYS
// Read-only: keyprefs.dat is a legacy seed for the HUD preferences (cfg owns
// them once apply_hud_settings_from_cfg has migrated a player), and nothing
// writes the file any more.
class options
{
	public:
		options();
		~options();
		short load(viewscreen *viewp);
	protected:
		signed char prefs[4][10];
};

class viewscreen
{
	public:
		viewscreen(short x, short y, short length, short height, short whatnum);
		// Camera-pane factory (docs/camera-views-design.md §4): builds a
		// viewscreen with the camera identity — mynum -1 (never a seat, so
		// no players[]/prefs[] role can index with it), global_player_index_
		// -1 (the documented "no seat" sentinel: HUD/radar ownership gates
		// fail closed), following_ false, prefs seeded directly. The public
		// constructor above is seat-flavored (keyprefs load, cfg overlay,
		// seat-mode resize); the factory touches no player cfg key.
		static std::unique_ptr<viewscreen> make_camera(screen* screenp);
		~viewscreen();
		void clear();
		bool draw ();
		bool redraw();
		bool redraw(LevelRuntimeData* data, bool draw_radar = true);
		bool refresh();
		short input(const void* native_event);
		template <typename EventT>
		short input(const EventT& event)
		{
			return input(static_cast<const void*>(&event));
		}
		short continuous_input();
		void process_input(const InputState& input_state);
		void set_display_text(std::string_view newtext, short numcycles);
		void refresh_display_text(std::string_view newtext, short numcycles);
		// Retire an exact line the caller previously wrote. Unlike
		// refresh_display_text this NEVER appends: a line that is not on
		// the feed stays off it (#246 — the overlay off-switch must not
		// resurrect the message it is trying to clear).
		void expire_display_text(std::string_view oldtext);
		void display_text(); // put the text to the buffer, if there
		void shift_text(Sint32 row); // cycle text upward
		void clear_text(void); // clear all text in buffer
		bool draw_obs(); //moved here to fix radar
		bool draw_obs(LevelRuntimeData* data);
		// Multi-floor rendering: draw stacked floors bottom-up with per-floor
		// opacity (camera floor opaque, floors below fade with depth, floors
		// above are faint ghosts), interleaving each floor's tiles + entities so
		// the camera floor occludes lower floors except through air holes.
		// Single-floor levels collapse to one opaque pass (byte-identical).
		void draw_floor_entities(LevelRuntimeData* data, int floor,
		                         unsigned char alpha, bool layer_active);
		// Floor-glide trigger + suppression ladder: the ONE place (both redraw
		// overloads call it) that assigns current_floor_ from the control
		// walker / editor override, starting or advancing the render-only
		// camera dolly on a classified floor change (Stairs/Fall animate,
		// Teleport and every suppression rung snap — today's behavior).
		void update_floor_glide(GameWorld& vworld, walker* controlob,
		                       Sint32 camera_floor_override = -1);
		// Per-floor presentation for one pass of the redraw floor loop.
		struct FloorPassParams {
			unsigned char falpha; float fscale; float pf;
			bool shift;      // apply the parallax topx/topy shift this pass
			bool skip;       // alpha==0: render nothing for this floor
			bool entities;   // false => terrain+decor only
		};
		// While a glide is inactive this returns the pre-glide integer math
		// verbatim (floor_render_alpha + the fixed parallax step), so OFF /
		// idle / single-floor frames are identical arithmetic. Mid-glide it
		// evaluates the same depth grammar continuously from
		// dz = f - glide_camera_z_.
		[[nodiscard]] FloorPassParams compute_floor_pass(Sint32 f, const GameWorld& vworld,
		                                                 bool ghosts_on) const;
		// Effects pre-pass for one floor: water ripples, reflections, ground
		// shadows, projectile trails and falling dust, drawn BEFORE the
		// entity lists so the sprites overdraw them. Each is gated on its
		// cfg key ("effects" ripples/reflections/shadows/trails/dust) — all
		// off means zero extra work, so disabled effects render
		// byte-identically.
		void draw_floor_effects(LevelRuntimeData* data, int floor);
		// Effects post-pass for one floor: the fire glow, blended AFTER the
		// entity lists so it reads as light over the sprites. Camera floor
		// only; gated on cfg "effects" fire_glow (off costs nothing).
		void draw_floor_effects_post(LevelRuntimeData* data, int floor);
		[[nodiscard]] unsigned char floor_render_alpha(int f) const;
		static constexpr unsigned char kFloorBelowAlphaStep = 70;
		static constexpr unsigned char kFloorBelowAlphaMin = 90;
		static constexpr unsigned char kFloorGhostAlpha = 48;
		void resize(short x, short y, short length, short height);
		void resize(char whatmode); // set according to preferences ..
		// Stable zoom-1.0 pane geometry used by HUD, radar and text. The World
		// pane is a projection of this rectangle at reduced zoom.
		[[nodiscard]] std::pair<Sint32, Sint32>
		project_world_point_to_gameplay_ui(float x, float y) const;

		// ---- Per-view zoom-out (§7.1). Cycle position 0 = GAME (no override
		// — follow graphics/zoom), 1..5 = 0.9x..0.5x. Runtime carrier beside
		// prefs[]; persisted as cfg controls/playerN_view_zoom. The step
		// changes GEOMETRY only (canvas composition + window size in
		// resize(whatmode)); the render loop has no zoom code — gameplay
		// pixels are resampled exactly once, by the presentation path.
		Sint32 view_zoom_step_ = 0;
		static constexpr Sint32 kViewZoomStepCount = 6;
		// The step's scale numerator in tenths (10 = GAME .. 5 = 0.5x), the
		// per-view factor composed into og::compose_zoom_pct.
		[[nodiscard]] static constexpr Sint32 view_scale_num_for_step(
			Sint32 step)
		{
			const Sint32 clamped = step < 0 ? 0
				: (step >= kViewZoomStepCount ? kViewZoomStepCount - 1 : step);
			return 10 - clamped;
		}
		[[nodiscard]] Sint32 view_scale_num() const
		{
			return view_scale_num_for_step(view_zoom_step_);
		}
		// ---- Canvas SLOT (§7.1): this view's proportional share of the
		// world canvas (the baseline layout projected onto it — exactly the
		// pre-per-view-zoom rect). The 1:1 render WINDOW (xloc/yloc/xview/
		// yview) equals the slot scaled by n_min/n_view and anchored at the
		// slot's top-left; window == slot whenever this view sits at the
		// minimum effective zoom (in particular when every view is at GAME).
		// relayout publishes {window -> slot} presentation slices whenever
		// the two differ.
		Sint32 slot_x_ = 0;
		Sint32 slot_y_ = 0;
		Sint32 slot_w_ = 0;
		Sint32 slot_h_ = 0;
		// §7.1 persistence: overlay the cfg-carried HUD/zoom values onto the
		// keyprefs-loaded prefs (called once from the constructor). When this
		// player's cfg keys were never written, seeds them from the legacy
		// keyprefs.dat prefs instead (one-shot; apply_setting only).
		void apply_hud_settings_from_cfg();
		// Blocking team roster. The optional poll callback runs once per
		// wait-loop pass (the pause menu pumps its transport + pause
		// keep-alive through it); returning false ends the wait. Under
		// TESTING the blocking wait is compiled out (returns immediately);
		// view_team_testing_set_poll_passes drives the poll contract.
		void view_team(KeyWaitPollCallback poll = nullptr);
		void view_team(short left, short top, short right, short bottom,
		               KeyWaitPollCallback poll = nullptr);
		walker* find_next_control();

		std::string textlist[MAX_MESSAGES];
		short textcycles[MAX_MESSAGES];  // duration in sim ticks
		std::uint32_t text_expire_ticks[MAX_MESSAGES];
		// Tick the slot was stamped at. The world clock restarts at 0 on
		// every level (re)launch, which used to leave an in-flight line with
		// an expiry far in the future — it then outlived the reset by its
		// whole duration. A slot whose stamp is in the future relative to
		// the current tick is treated as expired (#246).
		std::uint32_t text_stamp_ticks[MAX_MESSAGES];

		short mynum;     // # to id the viewscreen, 0, 1, 2 ...
		// Global simulation player mapped to this local view. Network joins can
		// map local view 0 to global player 6, so HUD ownership must never alias
		// this with mynum. -1 means spectator/no seat.
		short global_player_index_ = -1;
		short my_team;         // used for Player-v-Player mode
			walker  *control;  // the user
			Sint32 xpos,ypos;
			Sint32 topx, topy;
			Sint32 xloc, yloc; // physical screen coords
			Sint32 endx, endy; // screen coords of lower right corner
			signed char prefs[10] = {}; // User preferences ..
			std::unique_ptr<radar> myradar;
			short radarstart; //has the radar been started yet?
			Sint32 xview;
			Sint32 yview;
			float interpolation_alpha = 1.0f;
			// Floor the camera-followed walker is on; the background draws floors
			// 0..current_floor_ bottom-up (air reveals lower floors) and draw_obs
			// layers entities the same way. 0 for single-floor levels.
			Sint32 current_floor_ = 0;
		// When >= 0, forces the rendered floor. Set by the level editor (which
		// has no control walker) and by capture/spectator cameras. -1 in
		// gameplay so the control walker's floor is used.
		Sint32 editor_floor_override_ = -1;
		// True only for the level editor's authoring view: suppresses the
		// ghosts-off upper-floor shadow pass so the floor being edited renders
		// clean. Spectator/capture cameras leave this false — they use
		// editor_floor_override_ for the floor cut but must render the real
		// gameplay presentation (shadows included).
		bool editor_authoring_view_ = false;
		// Look-up hold (KEY_LOOKUP): true while this viewport's player holds
		// the key, ADDING the floors above the camera to this frame as faint
		// alpha ghosts. This hold is the ONLY way to see floors above — there
		// is no cfg setting for it. It gates nothing else: floors BELOW the
		// camera always render depth-faded (+ depth-tintable) whether or not
		// the key is held (see floor_render_alpha). Recomputed at the top of
		// every redraw; render-only, never fed into the sim.
		bool ghost_hold_override_ = false;

		// §4.5 networked follow camera (render-only; stamped by the
		// platform local transport shadow on every control re-sync). True
		// while this view watches a target it does not control: gates the
		// SwitchChar follow-cycle branch in process_input and the §2.8
		// "FOLLOWING <name>" caption strip in score_panel. The watched
		// walker rides view->control WITHOUT a user-tag stamp, so the HUD
		// (gated on user() != -1) and the radar stay quiet for AI targets.
		bool following_ = false;
		// Company display name of the watched target's owning machine for
		// the caption ("" = unknown / AI target -> name-only caption).
		std::string follow_company_;

		// True only for camera panes (set by make_camera, never by a seat
		// constructor): both redraw overloads skip their GameplayUI
		// chrome-scope block (radar/text) when set — the camera must not
		// depend on the coincidence that compute_view_layout's default arm
		// clamps mynum -1 onto quadrant 3 (design-review ruling). Also the
		// greppable handle for TESTING asserts.
		bool camera_view_ = false;

		// ---- Floor-glide transition (render-only; per-viewport => mirror-safe and
		// split-screen-independent, exactly like current_floor_). Inactive whenever
		// glide_frames_left_ == 0; the inactive render path is the pre-glide integer
		// code, so cfg-off / idle / single-floor frames are byte-identical.
		enum class FloorGlideCause : std::int8_t { None, Stairs, Fall };
		float           glide_camera_z_      = 0.0f;  // valid while active
		float           glide_from_z_        = 0.0f;
		Sint32          glide_to_floor_      = 0;
		Sint32          glide_frames_left_   = 0;
		Sint32          glide_total_         = 0;
		FloorGlideCause glide_cause_         = FloorGlideCause::None;
		// Trigger baseline (previous redraw's view of the world)
		std::uint32_t   glide_prev_control_id_ = 0;
		std::uint32_t   glide_last_seen_frame_ = 0;   // effects_frame_tick() at last update
		const void*     glide_world_key_       = nullptr;  // &GameWorld identity
		std::uint32_t   glide_world_tick_      = 0;   // world tick monotonicity check

		// Glide introspection (unconditional, for tests and instrumentation).
		[[nodiscard]] Sint32 floor_glide_frames_left() const { return glide_frames_left_; }
		[[nodiscard]] float  floor_glide_camera_z()   const
		{
			return glide_frames_left_ > 0 ? glide_camera_z_
			                              : static_cast<float>(current_floor_);
		}
		[[nodiscard]] Sint32 floor_glide_cause()      const { return static_cast<Sint32>(glide_cause_); }

	private:
		// make_camera's constructor, tag-dispatched so the seat-flavored
		// public constructor keeps its exact historical shape.
		struct CameraViewTag {};
		viewscreen(CameraViewTag, screen* screenp);

	protected:
		options *prefsob;

		short size = 0;
};

class video;

// Temporarily expose a viewscreen's zoom-1.0 rectangle while drawing fixed
// gameplay chrome. If the backend had to fall back to drawing HUD on World,
// this leaves the live World geometry untouched.
class ScopedGameplayUiViewLayout final
{
	public:
		ScopedGameplayUiViewLayout(viewscreen& view, const video& output);
		~ScopedGameplayUiViewLayout();

		ScopedGameplayUiViewLayout(const ScopedGameplayUiViewLayout&) = delete;
		ScopedGameplayUiViewLayout& operator=(const ScopedGameplayUiViewLayout&) = delete;
		ScopedGameplayUiViewLayout(ScopedGameplayUiViewLayout&&) = delete;
		ScopedGameplayUiViewLayout& operator=(ScopedGameplayUiViewLayout&&) = delete;

	private:
		viewscreen& view_;
		Sint32 xloc_ = 0;
		Sint32 yloc_ = 0;
		Sint32 xview_ = 0;
		Sint32 yview_ = 0;
		Sint32 endx_ = 0;
		Sint32 endy_ = 0;
		bool applied_ = false;
};

// World-pane -> gameplay-UI-pane projection, captured as a value.
// Construct while the GameplayUI overlay is the ACTIVE canvas (inside
// ScopedGameplayUiCanvas) and BEFORE any ScopedGameplayUiViewLayout swaps the
// view onto the UI pane — inside that scope the live xloc/xview already hold
// UI values and the capture degenerates to the identity (the issue-#220 bug).
// project() maps world-canvas screen coordinates (world_px - topx +
// world_xloc()) to UI-pane coordinates; it is bit-identical to
// viewscreen::project_world_point_to_gameplay_ui, which delegates here.
// scale_w()/scale_h() scale a world-canvas length by the pane ratio with
// integer floor division (int64 numerator): exact identity at zoom 1.0, on
// the overlay-allocation fallback, and on the classic-pinned canvas.
class GameplayUiProjector final
{
	public:
		explicit GameplayUiProjector(const viewscreen& view);

		std::pair<Sint32, Sint32> project(float x, float y) const;
		Sint32 scale_w(Sint32 len, Sint32 min_len) const;
		Sint32 scale_h(Sint32 len, Sint32 min_len) const;
		Sint32 world_xloc() const { return world_xloc_; }
		Sint32 world_yloc() const { return world_yloc_; }

	private:
		bool applies_ = false;
		Sint32 world_xloc_ = 0;
		Sint32 world_yloc_ = 0;
		Sint32 world_xview_ = 0;
		Sint32 world_yview_ = 0;
		Sint32 ui_x_ = 0;
		Sint32 ui_y_ = 0;
		Sint32 ui_w_ = 0;
		Sint32 ui_h_ = 0;
};
