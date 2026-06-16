#pragma once

#include "SDL.h"
#include <memory>

enum class RenderEngine
{
	NoZoom = 0x01,
	SAI = 0x02,
	Eagle = 0x03,
	Double = 0x04
};

class Screen
{
	public:
		RenderEngine Engine;  // how to render the physical screen
		
		SDL_Window* window;
		SDL_Renderer* renderer;
		
		// The target for all rendering
		SDL_Surface* render;
		
		// A texture updated by 'render' for normal rendering
		SDL_Texture* render_tex;
		
		// A buffer for doubling filters (i.e. Sai or Eagle)
        SDL_Surface* render2;
        // A larger texture for the doubled result
        SDL_Texture* render2_tex;
        
		// When true, swap() renders to the surface but skips SDL_RenderPresent.
		// Used by multi-session demos to composite multiple sessions before presenting.
		bool suppress_present = false;

		Screen(RenderEngine engine, int width, int height, int fullscreen);
		~Screen();

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

};

extern std::unique_ptr<Screen> E_Screen;
