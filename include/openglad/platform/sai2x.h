#pragma once

#include <SDL3/SDL.h>
#include <openglad/interface/render/video.h> // CanvasTarget, kUiCanvasW/H
#include <openglad/platform/scale_mode.h>    // WorldScaleSetting (cfg graphics/scale)
#include <memory>

enum class RenderEngine
{
	NoZoom = 0x01,
	SAI = 0x02,
	Eagle = 0x03,
	Double = 0x04
};

// Screen owns TWO logical render targets (see CanvasTarget in video.h):
//  * the WORLD canvas — gameplay/editor map rendering, variable dims
//    (default kUiCanvasW x kUiCanvasH),
//  * the UI canvas — menus/picker/help/intro chrome, PINNED at
//    kUiCanvasW x kUiCanvasH.
//
// Design choice (byte-identity): while the world canvas is at the default
// 320x200 the two targets SHARE one surface/texture pair, so swap() presents
// exactly the same texture as the historical single-canvas renderer and every
// cross-mode pixel flow (fadeblack reading the current frame, in-game dialogs
// drawn over gameplay pixels, the 320x200-cell demo compositor) is untouched.
// Only set_world_canvas_size() with non-default dims splits them into
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
		RenderEngine Engine;  // how to render the physical screen

		SDL_Window* window;
		SDL_Renderer* renderer;

		// The ACTIVE draw target: aliases the world or UI canvas surface
		// per set_active_canvas() (all draw primitives write through this;
		// the multi-floor layer compositor temporarily redirects it).
		SDL_Surface* render;

		// The texture presented for the ACTIVE canvas (paired with 'render')
		SDL_Texture* render_tex;

		// A buffer for doubling filters (i.e. Sai or Eagle), sized
		// 2x the active canvas on demand
        SDL_Surface* render2;
        // A larger texture for the doubled result
        SDL_Texture* render2_tex;

		// When true, swap() renders to the surface but skips SDL_RenderPresent.
		// Used by multi-session demos to composite multiple sessions before presenting.
		bool suppress_present = false;

		Screen(RenderEngine engine, int width, int height, int fullscreen);
		~Screen();

		// --- Canvas dimensions & routing -------------------------------
		// Active canvas dims: every offset = x + y*canvas_w() plot
		// conversion and full-frame present rect derives from these
		// (320x200 by default — byte-identical to the legacy constants).
		int canvas_w() const { return active_ == CanvasTarget::World ? world_w_ : kUiCanvasW; }
		int canvas_h() const { return active_ == CanvasTarget::World ? world_h_ : kUiCanvasH; }
		int world_w() const { return world_w_; }
		int world_h() const { return world_h_; }
		int ui_w() const { return kUiCanvasW; }
		int ui_h() const { return kUiCanvasH; }
		CanvasTarget active_canvas() const { return active_; }
		// Repoints render/render_tex at the chosen canvas. Precondition:
		// never call during a floor-layer redirect (floor_layer_begin/end
		// bracket their redirect within one viewport draw).
		void set_active_canvas(CanvasTarget target);
		// Non-default dims allocate a separate world surface/texture;
		// default dims re-share the UI surface. Falls back to shared on
		// allocation failure.
		void set_world_canvas_size(int w, int h);

		// --- World canvas scale (cfg graphics/scale) --------------------
		// Legacy (the default) keeps the classic shared 320x200 canvas and
		// presents the world through the legacy `Engine`; every other mode
		// derives the world canvas from the window dims and overrides the
		// WORLD present engine only (the UI canvas always presents via
		// `Engine`). See scale_mode.h for the cfg-key contract.
		og::WorldScaleSetting world_scale() const { return world_scale_; }
		// The scale-derived world present engine — only consulted by swap()
		// under a non-Legacy scale mode (under Legacy both canvases follow
		// the runtime `Engine`, exactly as the single-engine renderer did).
		RenderEngine world_engine() const { return world_engine_; }
		// Applies a parsed graphics/scale setting: picks the world present
		// engine and sizes the world canvas from (win_w, win_h).
		void set_world_scale(og::WorldScaleSetting setting, int win_w, int win_h);
		// Window-resize hook: re-derives the world canvas dims for the new
		// window size. No-op under Legacy (the classic canvas never
		// resizes) and while the editor pin is held.
		void apply_world_scale_for_window(int win_w, int win_h);
		// Level-editor pin: forces the classic 320x200 world canvas while
		// held (the editor chrome assumes classic coordinates); releasing
		// re-derives the scale-mode dims from the current window.
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
		// Ensures render2/render2_tex exist at need_w x need_h (recreating
		// on a size change, e.g. after set_world_canvas_size).
		bool ensure_render2(int need_w, int need_h);

		CanvasTarget active_ = CanvasTarget::UI;
		// cfg graphics/scale state (Legacy => classic behavior; world_engine_
		// tracks Engine under Legacy so swap() is byte-identical).
		og::WorldScaleSetting world_scale_{};
		RenderEngine world_engine_ = RenderEngine::NoZoom;
		bool world_pinned_classic_ = false;
		int world_w_ = kUiCanvasW;
		int world_h_ = kUiCanvasH;
		// Owned canvas storage. world_surf_/world_tex_ ALIAS the UI pair
		// while the world canvas is at the default (shared) dims.
		SDL_Surface* ui_surf_ = nullptr;
		SDL_Texture* ui_tex_ = nullptr;
		SDL_Surface* world_surf_ = nullptr;
		SDL_Texture* world_tex_ = nullptr;
};

extern std::unique_ptr<Screen> E_Screen;
