#ifndef MOM_SCREEN
#define MOM_SCREEN

#include "SDL.h"

typedef enum 
{
	NoZoom = 0x01,
	SAI = 0x02,
	EAGLE = 0x03,
	DOUBLE = 0x04
} RenderEngine;

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
        
		Screen( RenderEngine engine, int width, int height, int fullscreen );

		void Quit();

		~Screen();

		void SaveBMP(SDL_Surface* screen, char* filename);

        void clear();
        void clear(int x, int y, int w, int h);
		void swap(int x, int y, int w, int h);
		
		void clear_window();

};

extern Screen *E_Screen;



#endif
