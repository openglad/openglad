#pragma once

#include <SDL3/SDL.h>
#include <openglad/interface/render/video.h> // CanvasTarget, kUiCanvasW/H
#include <openglad/core/scale_mode.h> // zoom/smoothing canvas settings
#include <memory>
#include <span>
#include <string>
#include <vector>

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
//  * a transparent GAMEPLAY UI overlay — pinned to zoom-1.0 dimensions and
//    composited nearest after the zoomed or filtered scenery.
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
		int canvas_w() const;
		int canvas_h() const;
		int world_w() const { return world_w_; }
		int world_h() const { return world_h_; }
		int gameplay_ui_w() const
		{
			return world_pinned_classic_ ? world_w_ : gameplay_ui_w_;
		}
		int gameplay_ui_h() const
		{
			return world_pinned_classic_ ? world_h_ : gameplay_ui_h_;
		}
		bool gameplay_ui_canvas_available() const
		{
			return (world_w_ == gameplay_ui_w() &&
			        world_h_ == gameplay_ui_h()) ||
			       (gameplay_ui_surf_ != nullptr && gameplay_ui_tex_ != nullptr);
		}
		int ui_w() const { return kUiCanvasW; }
		int ui_h() const { return kUiCanvasH; }
		CanvasTarget active_canvas() const { return active_; }
		CanvasTarget last_presented_canvas() const { return last_presented_; }
		// --- Window state (fade ownership, docs/menu-engine.md) ---------
		// True while the physical window shows black: from creation (a
		// window that has never presented shows nothing) and after a
		// completed fade-out, until the next present. swap() — the single
		// present site — clears it; sdl_video::fadeblack(false) sets it and
		// is a no-op while it holds, so a screen that already faded out can
		// never be faded out a second time, black-to-black.
		bool window_is_black() const { return window_is_black_; }
		void set_window_black(bool black) { window_is_black_ = black; }
#ifdef TESTING
		// The fade-out precondition: the active render surface holds
		// exactly the pixels the window last showed of it (nothing drew or
		// cleared it since its last present). False for a surface that was
		// never presented. `detail` (optional) receives where it differs —
		// the bounding box of the changed pixels, or "never presented".
		// "Showed" means the rect each present DECLARED: a partial-rect
		// swap() records only that rect, so a draw outside it stays
		// unpresented until a present covers it.
		bool testing_render_matches_presented(std::string* detail = nullptr) const;
		// Test boundary: the window is BLACK — the state a screen that faded
		// itself out at its exit leaves, and the state a process starts in —
		// and every canvas counts as presented as it stands, so a direct-call
		// test never inherits the previous test's window and its first
		// fading entry finds exactly what the ownership rule promises.
		void testing_reset_window_state();
#endif
		// Repoints render/render_tex at the chosen canvas. Precondition:
		// never call during a floor-layer redirect (floor_layer_begin/end
		// bracket their redirect within one viewport draw).
		void set_active_canvas(CanvasTarget target);
		// Prepare the transparent fixed-size HUD/radar/message overlay whenever
		// zoom or smart smoothing separates it from the World canvas. If its
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
		// UI-menu counterpart: copies `base` and composites any queued or
		// just-presented native world planes whose destinations use UI coords.
		// Returns nullptr when no such plane exists.
		SDL_Surface* compose_native_world_views_for_capture(
			SDL_Surface* base, CanvasTarget base_canvas) const;
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
		// Zoom derives the world canvas from a classic-density aspect
		// baseline. Smoothing selects its present engine. The UI canvas
		// remains 320x200 and presents nearest through `Engine`.
		og::WorldScaleSetting world_scale() const { return world_scale_; }
		int world_zoom_steps() const { return zoom_steps_; }
		int minimum_world_zoom_steps() const;
		bool world_smoothing_supported() const;
		// The world-only present engine. Legacy mode is the shared 320x200,
		// smoothing-off case and follows the nearest `Engine` path.
		RenderEngine world_engine() const { return world_engine_; }
		// Zoom 1.0 restores master's shipped/default world density without
		// aspect distortion; lower values divide that baseline and are
		// clamped to a safe resource budget. Completed logical dimensions
		// refresh the aspect base.
		void set_world_zoom(int zoom_steps, og::WorldScaleMode smoothing,
		                    int window_w = 0, int window_h = 0);
		// Level-editor pin: forces the classic 320x200 world canvas while
		// held (the editor chrome assumes classic coordinates); releasing
		// restores the zoom-derived dimensions.
		void set_world_canvas_pinned_classic(bool pinned);
		bool world_canvas_pinned_classic() const { return world_pinned_classic_; }

		// --- §7.1 per-view zoom composition + partitioned presentation ----
		// The deepest per-view zoom scale (tenths, 10 = none .. 5 = 0.5x)
		// composed into the canvas derivation: canvas percent =
		// zoom_steps * num. 10 is the untouched global-only path. The
		// replacement is transactional like set_world_zoom: an allocation
		// failure keeps the previous composition so windows and canvas
		// never desynchronize.
		void set_world_view_scale_num(int num);
		int world_view_scale_num() const { return world_view_scale_num_; }
		// Whether the composed canvas at the CURRENT global zoom fits the
		// split-canvas budget and texture limits (the per-view cycler gate).
		bool world_view_scale_fits(int num) const;
		// Present-time partition: each slice presents a view's 1:1 canvas
		// WINDOW onto its proportional canvas SLOT with one nearest GPU
		// blit, after (over) the ordinary whole-canvas present. Empty =
		// the byte-identical single-blit path. Canvas replacement clears
		// the list (relayout re-publishes against the new dimensions).
		void set_world_present_slices(
			std::span<const WorldPresentSlice> slices);
		const std::vector<WorldPresentSlice>& world_present_slices() const
		{
			return world_present_slices_;
		}

		// Live-world overlays (camera insets and menu scenario previews) own a
		// source raster independent of the fixed UI canvases. The raster survives
		// until swap(), which maps its logical destination rectangles directly to
		// physical renderer pixels after the base canvas and HUD are composed.
		NativeWorldViewSource begin_native_world_view(
			std::span<const NativeWorldViewDestination> destinations);
		bool end_native_world_view();
		void cancel_native_world_view();
		bool native_world_view_active() const
		{
			return native_world_view_active_index_ >= 0;
		}
#ifdef TESTING
		std::size_t native_world_view_ready_count_for_testing() const
		{
			return native_world_view_ready_count_;
		}
		SDL_Surface* native_world_view_surface_for_testing(
			std::size_t index = 0) const;
		std::span<const NativeWorldViewDestination>
		native_world_view_destinations_for_testing(
			std::size_t index = 0) const
		{
			return index < native_world_view_planes_.size()
				? std::span<const NativeWorldViewDestination>(
					native_world_view_planes_[index].destinations)
				: std::span<const NativeWorldViewDestination>{};
		}
		void discard_native_world_views_for_testing()
		{
			discard_native_world_views();
		}
		void fail_next_native_world_view_allocation_for_testing()
		{
			fail_next_native_world_view_allocation_ = true;
		}
#endif

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

		// Rebuild the renderer and every GPU-side texture after the rendering
		// device was lost (the web 'webglcontextrestored' path; see
		// request_render_backend_recreate in video_sdl.h). The CPU canvases
		// keep their pixels: swap() re-uploads surface content on every
		// present, so the next frame self-heals without repainting callers.
		// Returns false — and stays safe to retry — while the device is
		// still gone.
		bool recreate_render_backend();

		void clear_window();

		// Vsync for the presenting renderer. The choice is remembered and
		// replayed at every renderer creation (construction and
		// recreate_render_backend), so a lost device cannot restore the
		// display-rate cap that an uncapped frame rate turned off.
		void set_vsync(bool on);
		[[nodiscard]] bool vsync_enabled() const { return vsync_enabled_; }

	private:
		// Ensures an atomic surface/texture pair exists at 2x source size.
		// Rejects overflow, the renderer's maximum texture dimension and the
		// bounded CPU/pixel budget before allocating either resource.
		bool ensure_render2_for_source(int source_w, int source_h);
		int renderer_max_texture_dimension() const;
		// Global-only canvas dims (the pre-per-view math, byte-identical).
		og::WorldCanvasDims global_zoom_canvas_dims(int zoom_steps) const;
		// Canvas dims with the per-view composition folded in; equals the
		// global-only result while world_view_scale_num_ == 10.
		og::WorldCanvasDims effective_zoom_canvas_dims(int zoom_steps) const;
		// The cfg-requested smoothing for re-derivations (Legacy is the
		// derived shared-canvas state, not a requestable setting).
		og::WorldScaleMode requested_smoothing_mode() const
		{
			return world_scale_.mode == og::WorldScaleMode::Legacy
				? og::WorldScaleMode::Integer
				: world_scale_.mode;
		}
		void destroy_render2();
		bool ensure_gameplay_ui_overlay();
		void destroy_gameplay_ui_overlay();
		struct NativeWorldViewPlane
		{
			SDL_Surface* surface = nullptr;
			SDL_Texture* texture = nullptr;
			int failed_w = 0;
			int failed_h = 0;
			std::vector<NativeWorldViewDestination> destinations;
		};
		bool ensure_native_world_view_plane(std::size_t index, int source_w,
		                                    int source_h);
		void destroy_native_world_view_planes();
		void discard_native_world_views();

		CanvasTarget active_ = CanvasTarget::UI;
		CanvasTarget last_presented_ = CanvasTarget::UI;
		bool window_is_black_ = true;
#ifdef TESTING
		// Per canvas surface, a copy of its pixels as of its last present
		// (keyed by the surface, so the shared classic pair — one surface
		// under two canvas names — has one snapshot). Production builds
		// carry none of this.
		struct PresentedSnapshot {
			SDL_Surface* surface = nullptr;
			SDL_Surface* pixels = nullptr;
		};
		std::vector<PresentedSnapshot> presented_snapshots_;
		// Records the rect a present declared (clipped to the surface). A
		// surface's first snapshot starts black outside that rect: those
		// pixels were never shown.
		void testing_snapshot_presented(SDL_Surface* surface, int x, int y,
		                                int w, int h);
		void testing_forget_presented(SDL_Surface* surface);
#endif
		// Parsed zoom/smoothing state. Legacy is the byte-identical shared
		// 320x200 canvas with smoothing off.
		og::WorldScaleSetting world_scale_{};
		RenderEngine world_engine_ = RenderEngine::NoZoom;
		int zoom_steps_ = og::kZoomStepsMax; // 10 = master-density 1.0 zoom
		int zoom_window_w_ = kUiCanvasW;
		int zoom_window_h_ = kUiCanvasH;
		bool world_pinned_classic_ = false;
		// §7.1: deepest per-view zoom scale composed into the canvas (10 =
		// global-only) + the presentation partition (empty = single blit).
		int world_view_scale_num_ = og::kViewScaleNumMax;
		std::vector<WorldPresentSlice> world_present_slices_;
		std::vector<NativeWorldViewPlane> native_world_view_planes_;
		std::size_t native_world_view_ready_count_ = 0;
		std::size_t native_world_view_capture_count_ = 0;
		int native_world_view_active_index_ = -1;
		CanvasTarget native_world_view_saved_canvas_ = CanvasTarget::UI;
		SDL_Surface* native_world_view_saved_render_ = nullptr;
		SDL_Texture* native_world_view_saved_texture_ = nullptr;
		// A failed SDL allocation is not retried every frame for the same
		// target dimensions. A canvas/config change clears the latch.
		int render2_failed_w_ = 0;
		int render2_failed_h_ = 0;
		int gameplay_ui_failed_w_ = 0;
		int gameplay_ui_failed_h_ = 0;
		bool smart_scale_fallback_reported_ = false;
		int world_w_ = kUiCanvasW;
		int world_h_ = kUiCanvasH;
		int gameplay_ui_w_ = kUiCanvasW;
		int gameplay_ui_h_ = kUiCanvasH;
		// Owned canvas storage. world_surf_/world_tex_ ALIAS the UI pair
		// while the world canvas has the shared 320x200 dimensions.
		SDL_Surface* ui_surf_ = nullptr;
		SDL_Texture* ui_tex_ = nullptr;
		SDL_Surface* world_surf_ = nullptr;
		SDL_Texture* world_tex_ = nullptr;
		// Transparent, zoom-1.0-sized gameplay chrome, rendered nearest over
		// the zoomed/filtered world immediately before SDL_RenderPresent.
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
		bool fail_next_native_world_view_allocation_ = false;
		bool vsync_enabled_ = true;
};

extern std::unique_ptr<Screen> E_Screen;
