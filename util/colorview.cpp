// Jonathan Dearborn 10/24/2013
// Shows which colors you're dealing with

#include <cstdio>
#include "SDL.h"
#include <vector>
#include <list>
#include <string>
#include <algorithm>
using namespace std;

char ourcolors[] = {
                          0,0,0,8,8,8,16,16,16,24,24,24,32,32,32,40,40,40,48,48,48,56,56,56,1,
                          1,1,9,9,9,17,17,17,25,25,25,33,33,33,41,41,41,49,49,49,57,57,57,0,
                          0,0,15,15,15,18,18,18,21,21,21,24,24,24,27,27,27,30,30,30,33,33,33,36,
                          36,36,39,39,39,42,42,42,45,45,45,48,48,48,51,51,51,54,54,54,57,57,57,57,
                          16,16,54,18,18,51,20,20,48,22,22,45,24,24,42,26,26,39,28,28,36,30,30,57,
                          0,0,52,0,0,47,0,0,42,0,0,37,0,0,32,0,0,27,0,0,22,0,0,16,
                          57,16,18,54,18,20,51,20,22,48,22,24,45,24,26,42,26,28,39,28,30,36,30,0,
                          57,0,0,52,0,0,47,0,0,42,0,0,37,0,0,32,0,0,27,0,0,22,0,16,
                          16,57,18,18,54,20,20,51,22,22,48,24,24,45,26,26,42,28,28,39,30,30,36,0,
                          0,57,0,0,52,0,0,47,0,0,42,0,0,37,0,0,32,0,0,27,0,0,22,57,
                          57,16,54,54,18,51,51,20,48,48,22,45,45,24,42,42,26,39,39,28,36,36,30,57,
                          57,0,52,52,0,47,47,0,42,42,0,37,37,0,32,32,0,27,27,0,22,22,0,57,
                          16,57,54,18,54,51,20,51,48,22,48,45,24,45,42,26,42,39,28,39,36,30,36,57,
                          0,57,52,0,52,47,0,47,42,0,42,37,0,37,32,0,32,27,0,27,22,0,22,16,
                          57,57,18,54,54,20,51,51,22,48,48,24,45,45,26,42,42,28,39,39,30,36,36,0,
                          57,57,0,52,52,0,47,47,0,42,42,0,37,37,0,32,32,0,27,27,0,22,22,57,
                          41,25,52,36,20,47,31,15,42,26,10,37,21,5,32,16,0,27,11,0,22,6,0,50,
                          40,30,45,35,25,40,30,20,35,25,15,30,20,10,25,15,5,20,10,0,15,5,0,57,
                          25,41,52,20,36,47,15,31,42,10,26,37,5,21,32,0,16,27,0,11,22,0,6,50,
                          30,40,45,25,35,40,20,30,35,15,25,30,10,20,25,5,15,20,0,10,15,0,5,0,
                          18,6,0,16,6,0,13,5,0,11,5,0,8,3,0,6,2,0,3,1,0,2,0,17,
                          17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
                          17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,
                          17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,41,
                          25,57,36,20,52,31,15,47,26,10,42,21,5,37,16,0,32,11,0,27,6,0,22,40,
                          30,50,35,25,45,30,20,40,25,15,35,20,10,30,15,5,25,10,0,20,5,0,15,25,
                          41,57,23,39,55,21,37,53,19,35,51,17,33,49,15,31,47,13,29,45,11,27,43,9,
                          25,41,7,23,39,5,21,37,3,19,35,1,17,33,0,15,31,0,13,29,0,11,27,57,
                          15,0,57,21,0,57,27,0,57,33,0,57,39,0,57,45,0,57,51,0,57,57,0,57,
                          15,0,57,21,0,57,27,0,57,33,0,57,39,0,57,45,0,57,51,0,57,57,0,57,
                          37,31,51,33,27,47,28,24,43,24,20,56,35,23,52,32,24,48,30,22,44,27,19,28,
                          18,18,30,20,20,32,22,22,34,24,24,36,26,26,38,28,28,40,30,30,42,32,32
		};

// Color cycling colors indices
#define WATER_START  208
#define WATER_END    223
#define ORANGE_START 224
#define ORANGE_END   231


static inline Uint32 getPixel(SDL_Surface *Surface, int x, int y)
{
    Uint8* bits;
    Uint32 bpp;

    if(x < 0 || x >= Surface->w)
        return 0;  // Best I could do for errors

    bpp = Surface->format->BytesPerPixel;
    bits = ((Uint8*)Surface->pixels) + y*Surface->pitch + x*bpp;

    switch (bpp)
    {
    case 1:
        return *((Uint8*)Surface->pixels + y * Surface->pitch + x);
        break;
    case 2:
        return *((Uint16*)Surface->pixels + y * Surface->pitch/2 + x);
        break;
    case 3:
        // Endian-correct, but slower
    {
        Uint8 r, g, b;
        r = *((bits)+Surface->format->Rshift/8);
        g = *((bits)+Surface->format->Gshift/8);
        b = *((bits)+Surface->format->Bshift/8);
        return SDL_MapRGB(Surface->format, r, g, b);
    }
    break;
    case 4:
        return *((Uint32*)Surface->pixels + y * Surface->pitch/4 + x);
        break;
    }

    return 0;  // FIXME: Handle errors better
}

Uint8 is_in_range(int value, int target, int range)
{
    return (value >= target - range && value <= target + range);
}

int get_color_index(Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    if(a == 0)
        return 0;
    
    int rr = r/4;
    int gg = g/4;
    int bb = b/4;
    
    int numcolors = sizeof(ourcolors)/sizeof(char);
    
    int slop = 0;
    // Look for the nearest color
    while(1)
    {
        int i;
        
        if(is_in_range(0, rr, slop) && is_in_range(0, gg, slop) && is_in_range(0, bb, slop)) // Skip matching transparent black (0,0,0)
            return 8; // (1,1,1)
        for(i = 0; i < numcolors; i++)
        {
            if(is_in_range(ourcolors[i * 3], rr, slop) && is_in_range(ourcolors[i * 3 + 1], gg, slop) && is_in_range(ourcolors[i * 3 + 2], bb, slop))
                return i;
        }
        
        // Look again with a wider tolerance...
        slop++;
    }
    
    return 0;
}


int resolution = 39;
int columns = 20;

int get_index(int x, int y)
{
    return columns * (y/resolution) + (x/resolution);
}

void draw_palette_to_surface(SDL_Surface* surface)
{
    int x = 0, y = 0;
    
    int index = 0;
    while(index < 256)
    {
        Uint8 r, g, b;
        
        r = ourcolors[index * 3] * 4;
        g = ourcolors[index * 3 + 1] * 4;
        b = ourcolors[index * 3 + 2] * 4;

        SDL_Rect rect = {x, y, resolution, resolution};
        
        if(r > 0 || g > 0 || b > 0)
            SDL_FillRect(surface, &rect, SDL_MapRGB(surface->format, r, g, b));
        
        index++;
        x += resolution;
        if(x >= columns*resolution)
        {
            x = 0;
            y += resolution;
        }
    }
}

int main(int argc, char* argv[])
{
    if(SDL_Init(SDL_INIT_VIDEO) < 0)
        return 1;
    
    SDL_Window* window;
    SDL_Renderer* renderer;
    if(SDL_CreateWindowAndRenderer(800, 600, SDL_WINDOW_SHOWN, &window, &renderer) < 0)
        return 2;
    
    SDL_Surface* surface = SDL_CreateRGBSurface(SDL_SWSURFACE, 800, 600, 32, 0, 0, 0, 0);
    if(surface == NULL)
        return 3;
    
    draw_palette_to_surface(surface);
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if(texture == NULL)
        return 4;
    
    SDL_Event event;
    bool done = false;
    while(!done)
    {
        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_QUIT)
                done = true;
            else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                done = true;
            else if(event.type == SDL_MOUSEBUTTONDOWN)
            {
                int index = get_index(event.button.x, event.button.y);
                if(index >= 0 && index < 256)
                {
                    printf("Color: %u", index);
                    if(WATER_START <= index && index <= WATER_END)
                        printf(" (water cycle)");
                    if(ORANGE_START <= index && index <= ORANGE_END)
                        printf(" (orange cycle)");
                    printf("\n");
                }
            }
        }
        
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        
        SDL_Delay(10);
    }
    
    SDL_Quit();
	return 0;
}
