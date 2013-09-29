#ifndef MOM_SCREEN
#define MOM_SCREEN

#include "SDL.h"

//#include "global.h"

// SDL 2 compat
#if SDL_VERSION_ATLEAST(2,0,0)
    #define USE_SDL2
#endif

//extern SDL_Surface *screen;

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
        #ifdef USE_SDL2
		SDL_Window* window;
		SDL_Renderer* renderer;
		#endif
		
		RenderEngine	Engine;			// how to render the physical screen
		
		SDL_Surface* render;
		SDL_Texture* render_tex;
		
        SDL_Surface* render2;
        SDL_Texture* render2_tex;
        
		Screen( RenderEngine engine, int width, int height, int fullscreen );

		void Quit();

		~Screen();

		void SaveBMP(SDL_Surface* screen, char* filename);

        void clear();
        void clear(int x, int y, int w, int h);
		void swap(int x, int y, int w, int h);

};

extern Screen *E_Screen;



#endif
