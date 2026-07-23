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
inline constexpr signed char PREF_JOY = 3;
  inline constexpr signed char PREF_NO_JOY = 0;
  inline constexpr signed char PREF_USE_JOY = 1;
inline constexpr signed char PREF_RADAR = 4;
  inline constexpr signed char PREF_RADAR_OFF   = 0;
  inline constexpr signed char PREF_RADAR_ON    = 1;
inline constexpr signed char PREF_FOES = 5;
  inline constexpr signed char PREF_FOES_OFF    = 0;
  inline constexpr signed char PREF_FOES_ON     = 1;
inline constexpr signed char PREF_GAMMA = 6;
inline constexpr signed char PREF_OVERLAY = 7;
  inline constexpr signed char PREF_OVERLAY_OFF = 0;
  inline constexpr signed char PREF_OVERLAY_ON  = 1;

inline constexpr int PREF_MAX = 8;  // == 1 + highest pref ..

inline constexpr int MAX_MESSAGES = 5;  // max of 5 lines, currently

struct InputState;
class GameWorld;
class viewscreen;
class walker;
class radar;

// Pure helper functions for HP/MP color thresholds
unsigned char compute_hp_color(float hp, float maxhp);
unsigned char compute_mp_color(float mp, float maxmp);
void reset_viewscreen_input_debounce();

// This is a child object of all viewscreens
//  It is used to save and load all prefs
//  because each player has their own
//  prefs.  WE ASSUME 4 PLAYERS ALWAYS
class options
{
	public:
		options();
		~options();
		short load(viewscreen *viewp);
		short save(viewscreen *viewp);
	protected:
		signed char prefs[4][10];
		char keys[4][16];
};

class viewscreen
{
	public:
		viewscreen(short x, short y, short length, short height, short whatnum);
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
		void update_floor_glide(GameWorld& vworld, walker* controlob);
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
		void view_team();
		void view_team(short left, short top, short right, short bottom);
		void options_menu();   // display the options menu
		Sint32 set_key_prefs(); // get player keyboard info
		void view_key_bindings(); // display current key bindings
		Sint32 change_speed(Sint32 whichway);
		Sint32 change_gamma(Sint32 whichway);
		walker* find_next_control();

		Sint32 gamma; // for gamma correction

		std::string textlist[MAX_MESSAGES];
		short textcycles[MAX_MESSAGES];  // duration in sim ticks
		std::uint32_t text_expire_ticks[MAX_MESSAGES];

		short mynum;     // # to id the viewscreen, 0, 1, 2 ...
		// Global simulation player mapped to this local view. Network joins can
		// map local view 0 to global player 6, so HUD ownership must never alias
		// this with mynum. -1 means spectator/no seat.
		short global_player_index_ = -1;
		short my_team;         // used for Player-v-Player mode
			int* mykeys;     // holds the keyboard mapping
			walker  *control;  // the user
			Sint32 xpos,ypos;
			Sint32 topx, topy;
			Sint32 xloc, yloc; // physical screen coords
			Sint32 endx, endy; // screen coords of lower right corner
			signed char prefs[10]; // User preferences ..
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
