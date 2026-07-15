#pragma once

#include <SDL3/SDL.h>
#include <openglad/interface/render/video.h> // CanvasTarget, kUiCanvasW/H
#include <openglad/core/scale_mode.h> // zoom/smoothing canvas settings
#include <memory>

enum class RenderEngine
{
	NoZoom = 0x01,
	SAI = 0x02,
	Eagle = 0x03,
	Double = 0x04
};

// Screen owns the logical render targets described by CanvasTarget:
//  * the WORLD canvas — gameplay/editor scenery rendering, with dimensions
//    derived from the logical window and zoom,
//  * the UI canvas — menus/picker/help/intro chrome, PINNED at
//    kUiCanvasW x kUiCanvasH.
//  * a transparent GAMEPLAY UI overlay — world-sized, allocated only for a
//    usable SAI/Eagle frame and composited nearest after the filtered scenery.
//
// Design choice (byte-identity): while the world canvas is 320x200 the two
// targets SHARE one surface/texture pair, so swap() presents
// exactly the same texture as the historical single-canvas renderer and every
// cross-mode pixel flow (fadeblack reading the current frame, in-game dialogs
// drawn over gameplay pixels, the 320x200-cell demo compositor) is untouched.
// Only set_world_canvas_size() with other dimensions splits them into
// separate surfaces; each is then presented with its own stretch at swap
// time (the GPU viewport stretch already handles arbitrary source dims).
//
// The level editor renders on the WORLD canvas: it draws the map through the
// standard viewscreen machinery. Its panel chrome and mouse mapping still use
// absolute 320x200-era coordinates (left-anchored, e.g. its local S_RIGHT=245
// panel origin in level_editor.cpp), so the editor PINS the classic canvas
// for its whole session via set_world_canvas_pinned_classic (follow-up:
// right-anchor the chrome from canvas_w and drop the pin).
class Screen
{
	public:
		// Retained engine slot for legacy callers. Display creation initializes
		// it to nearest; world smoothing is selected separately below.
		RenderEngine Engine;

		SDL_Window* window;
		SDL_Renderer* renderer;

		// The ACTIVE draw target: aliases the world or UI canvas surface
		// per set_active_canvas() (all draw primitives write through this;
		// the multi-floor layer compositor temporarily redirects it).
		SDL_Surface* render;

		// The texture presented for the ACTIVE canvas (paired with 'render')
		SDL_Texture* render_tex;

		// A buffer for doubling filters (i.e. Sai or Eagle), sized
		// 2x the active canvas on demand. Smart scaling is deliberately
		// bounded to 16 classic 640x400 output frames (about 16.4 MB per
		// surface/texture) so deep world zoom cannot create a 6400x4000
		// scratch pair and upload more than 100 MB on every present.
		SDL_Surface* render2;
        // A larger texture for the doubled result
        SDL_Texture* render2_tex;

		// When true, swap() renders to the surface but skips SDL_RenderPresent.
		// Used by multi-session demos to composite multiple sessions before presenting.
		bool suppress_present = false;

		Screen(RenderEngine engine, int width, int height, int fullscreen);
		~Screen();

		// Maximum number of 32-bit output pixels the CPU SAI/Eagle pass may
		// process per present. This is 16x the classic 640x400 smart-scaled
		// frame: at most 16.4 MB in each scratch resource and upload. A
		// larger canvas keeps the requested setting but presents nearest.
		static constexpr Sint64 kSmartScaleScratchPixelBudget =
			static_cast<Sint64>(kUiCanvasW * 2) *
			static_cast<Sint64>(kUiCanvasH * 2) * 16;

		// --- Canvas dimensions & routing -------------------------------
		// Active canvas dims: every offset = x + y*canvas_w() plot
		// conversion and full-frame present rect derives from these. The UI
		// and classic-pinned world remain byte-identical 320x200 spaces.
		int canvas_w() const { return active_ == CanvasTarget::UI ? kUiCanvasW : world_w_; }
		int canvas_h() const { return active_ == CanvasTarget::UI ? kUiCanvasH : world_h_; }
		int world_w() const { return world_w_; }
		int world_h() const { return world_h_; }
		int ui_w() const { return kUiCanvasW; }
		int ui_h() const { return kUiCanvasH; }
		CanvasTarget active_canvas() const { return active_; }
		CanvasTarget last_presented_canvas() const { return last_presented_; }
		// Repoints render/render_tex at the chosen canvas. Precondition:
		// never call during a floor-layer redirect (floor_layer_begin/end
		// bracket their redirect within one viewport draw).
		void set_active_canvas(CanvasTarget target);
		// Prepare the optional transparent HUD/radar/message overlay for one
		// smart-smoothed gameplay frame. If either smart-scaler or overlay
		// allocation is unavailable, GameplayUI aliases World for this frame.
		void begin_gameplay_frame();
		bool gameplay_ui_overlay_active() const { return gameplay_ui_frame_active_; }
		SDL_Surface* gameplay_ui_overlay_surface() const
		{
			return (gameplay_ui_frame_active_ || gameplay_ui_capture_valid_)
				? gameplay_ui_surf_ : nullptr;
		}
		SDL_Texture* gameplay_ui_overlay_texture() const
		{
			return (gameplay_ui_frame_active_ || gameplay_ui_capture_valid_)
				? gameplay_ui_tex_ : nullptr;
		}
		// Build the same scenery + nearest gameplay-UI composition used for
		// presentation into a newly owned surface (screenshots/tests).
		SDL_Surface* compose_gameplay_ui_for_capture(SDL_Surface* scenery) const;
		// Cancel a prepared overlay without presenting it (fades/transitions).
		// The owned resources stay cached for the next gameplay frame.
		void discard_gameplay_ui_frame();
		bool smart_present_suppressed() const
		{
			return smart_present_suppressed_;
		}
		bool last_world_present_used_smart_surface() const
		{
			return last_world_present_used_render2_;
		}
		void fail_next_gameplay_ui_allocation_for_testing();
		void fail_next_world_canvas_allocation_for_testing()
		{
			fail_next_world_canvas_allocation_ = true;
		}
		// Downsample the current split world frame into the fixed UI canvas
		// with nearest filtering before drawing an in-game modal overlay.
		void prepare_ui_canvas_from_world();
		// Dimensions other than 320x200 allocate a separate world surface and
		// texture; 320x200 re-shares the UI pair. The replacement is atomic:
		// invalid dimensions or an allocation failure leave the live pair and
		// dimensions unchanged and return false.
		bool set_world_canvas_size(int w, int h);

		// --- World canvas zoom and smoothing -----------------------------
		// Zoom derives the world canvas from the logical window. Smoothing
		// selects its present engine. The UI canvas remains 320x200 and
		// presents nearest through `Engine`.
		og::WorldScaleSetting world_scale() const { return world_scale_; }
		int world_zoom_steps() const { return zoom_steps_; }
		int minimum_world_zoom_steps() const;
		bool world_smoothing_supported() const;
		// The world-only present engine. Legacy mode is the shared 320x200,
		// smoothing-off case and follows the nearest `Engine` path.
		RenderEngine world_engine() const { return world_engine_; }
		// Zoom 1.0 restores master's window-sized scale=1 canvas; lower
		// values divide that baseline and are clamped to a safe resource
		// budget. Completed logical dimensions refresh the resize base.
		void set_world_zoom(int zoom_steps, og::WorldScaleMode smoothing,
		                    int window_w = 0, int window_h = 0);
		// Level-editor pin: forces the classic 320x200 world canvas while
		// held (the editor chrome assumes classic coordinates); releasing
		// restores the zoom-derived dimensions.
		void set_world_canvas_pinned_classic(bool pinned);
		bool world_canvas_pinned_classic() const { return world_pinned_classic_; }

		// Screen owns SDL handles (window/renderer/surfaces/textures) freed in
		// the destructor; a shallow copy or move would double-free. It is owned
		// via std::unique_ptr<Screen>, so make non-copyable/non-movable explicit.
		Screen(const Screen&) = delete;
		Screen& operator=(const Screen&) = delete;
		Screen(Screen&&) = delete;
		Screen& operator=(Screen&&) = delete;

		void SaveBMP(SDL_Surface* screen, char* filename);

        void clear();
        void clear(int x, int y, int w, int h);
		void swap(int x, int y, int w, int h);

		void clear_window();

	private:
		// Ensures an atomic surface/texture pair exists at 2x source size.
		// Rejects overflow, the renderer's maximum texture dimension and the
		// bounded CPU/pixel budget before allocating either resource.
		bool ensure_render2_for_source(int source_w, int source_h);
		int renderer_max_texture_dimension() const;
		og::WorldCanvasDims effective_zoom_canvas_dims(int zoom_steps) const;
		void destroy_render2();
		bool ensure_gameplay_ui_overlay();
		void destroy_gameplay_ui_overlay();

		CanvasTarget active_ = CanvasTarget::UI;
		CanvasTarget last_presented_ = CanvasTarget::UI;
		// Parsed zoom/smoothing state. Legacy is the byte-identical shared
		// 320x200 canvas with smoothing off.
		og::WorldScaleSetting world_scale_{};
		RenderEngine world_engine_ = RenderEngine::NoZoom;
		int zoom_steps_ = og::kZoomStepsMax; // 10 = window-sized 1.0 zoom
		int zoom_window_w_ = kUiCanvasW;
		int zoom_window_h_ = kUiCanvasH;
		bool world_pinned_classic_ = false;
		// A failed SDL allocation is not retried every frame for the same
		// target dimensions. A canvas/config change clears the latch.
		int render2_failed_w_ = 0;
		int render2_failed_h_ = 0;
		int gameplay_ui_failed_w_ = 0;
		int gameplay_ui_failed_h_ = 0;
		bool smart_scale_fallback_reported_ = false;
		int world_w_ = kUiCanvasW;
		int world_h_ = kUiCanvasH;
		// Owned canvas storage. world_surf_/world_tex_ ALIAS the UI pair
		// while the world canvas has the shared 320x200 dimensions.
		SDL_Surface* ui_surf_ = nullptr;
		SDL_Texture* ui_tex_ = nullptr;
		SDL_Surface* world_surf_ = nullptr;
		SDL_Texture* world_tex_ = nullptr;
		// Transparent, world-sized gameplay chrome. It exists only for a
		// frame whose SAI/Eagle scratch is usable, and is rendered nearest
		// over the filtered world immediately before SDL_RenderPresent.
		SDL_Surface* gameplay_ui_surf_ = nullptr;
		SDL_Texture* gameplay_ui_tex_ = nullptr;
		bool gameplay_ui_frame_active_ = false;
		// Retain the just-presented pixels for an immediate screenshot while
		// consuming frame_active_ so later World swaps cannot replay stale HUD.
		bool gameplay_ui_capture_valid_ = false;
		// A gameplay frame whose overlay could not be prepared keeps its HUD
		// on World and must therefore skip SAI/Eagle as a complete raw frame.
		// The suppression persists until a later begin succeeds/config resets.
		bool smart_present_suppressed_ = false;
		bool last_world_present_used_render2_ = false;
		bool fail_next_gameplay_ui_allocation_ = false;
		bool fail_next_world_canvas_allocation_ = false;
};

extern std::unique_ptr<Screen> E_Screen;
