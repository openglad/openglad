// Jonathan Dearborn 5/07/2013
// converts pixie files
// based on pixedit by Zardus

#include <stdio.h>
#include <SDL.h>
#include <SDL_image.h>
#include "savepng.h"

char ourcolors[] = {
                           0, 0, 0, 8, 8, 8,16,16,16,24,24,24,32,32,32,40,40,40,48,48,48,56,56,56,  // 8 colors per row
                           1, 1, 1, 9, 9, 9,17,17,17,25,25,25,33,33,33,41,41,41,49,49,49,57,57,57,
                           0, 0, 0,15,15,15,18,18,18,21,21,21,24,24,24,27,27,27,30,30,30,33,33,33,
                          36,36,36,39,39,39,42,42,42,45,45,45,48,48,48,51,51,51,54,54,54,57,57,57,
                          57,16,16,54,18,18,51,20,20,48,22,22,45,24,24,42,26,26,39,28,28,36,30,30,
                          57, 0, 0,52, 0, 0,47, 0, 0,42, 0, 0,37, 0, 0,32, 0, 0,27, 0, 0,22, 0, 0,
                          16,57,16,18,54,18,20,51,20,22,48,22,24,45,24,26,42,26,28,39,28,30,36,30,
                           0,57, 0, 0,52, 0, 0,47, 0, 0,42, 0, 0,37, 0, 0,32, 0, 0,27, 0, 0,22, 0,
                          16,16,57,18,18,54,20,20,51,22,22,48,24,24,45,26,26,42,28,28,39,30,30,36,
                           0, 0,57, 0, 0,52, 0, 0,47, 0, 0,42, 0, 0,37, 0, 0,32, 0, 0,27, 0, 0,22,
                          57,57,16,54,54,18,51,51,20,48,48,22,45,45,24,42,42,26,39,39,28,36,36,30,
                          57,57, 0,52,52, 0,47,47, 0,42,42, 0,37,37, 0,32,32, 0,27,27, 0,22,22, 0,
                          57,16,57,54,18,54,51,20,51,48,22,48,45,24,45,42,26,42,39,28,39,36,30,36,57,
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

static char no_cycling_colors = 0;

// From http://stackoverflow.com/questions/5309471/getting-file-extension-in-c
static const char *get_filename_ext(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if(!dot || dot == filename)
        return "";
    return dot + 1;
}


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
            // Don't match with cycling colors
            if(no_cycling_colors)
            {
                if(WATER_START <= i && i <= WATER_END)
                    continue;
                if(ORANGE_START <= i && i <= ORANGE_END)
                    continue;
            }
            
            if(is_in_range(ourcolors[i * 3], rr, slop) && is_in_range(ourcolors[i * 3 + 1], gg, slop) && is_in_range(ourcolors[i * 3 + 2], bb, slop))
                return i;
        }
        
        // Look again with a wider tolerance...
        slop++;
    }
    
    return 0;
}

// Raw data loader from any format
unsigned char* load_pix_data(const char* filename, unsigned char* numframes, unsigned char* width, unsigned char* height)
{
    // A pix file to load
    if(strcmp(get_filename_ext(filename), "pix") == 0)
    {
        printf("Reading pix file: %s\n",filename);
        
        FILE *file;
        unsigned char *data;
        
        if(!(file=fopen(filename,"rb")))
        {
            printf("Failed to open %s\n", filename);
            exit(1);
        }

        fread(numframes,1,1,file);
        fread(width,1,1,file);
        fread(height,1,1,file);
        
        int size = (*numframes)*(*width)*(*height);
        data = (unsigned char *)malloc(size);
        fread(data,1,size,file);
        
        fclose(file);
        return data;
    }
    
    // A standard image type to load
    printf("Reading image: %s\n", filename);
    
    // An image file to load
    SDL_Surface* surface = IMG_Load(filename);
    if(surface == NULL)
    {
        printf("Failed to load %s\n", filename);
        exit(1);
    }
    
    if(surface->w > 255)
    {
        printf("File %s has width that is too big for a pix (>255)\n", filename);
        exit(1);
    }
    if(surface->h > 255)
    {
        printf("File %s has height that is too big for a pix (>255)\n", filename);
        exit(1);
    }
    
    *numframes = 1;
    *width = surface->w;
    *height = surface->h;
    
    int size = (*numframes)*(*width)*(*height);
	unsigned char* data = (unsigned char *)malloc(size);
	int frame = 1;  // Only one
	
	// Fill with pixel data
	int x = *width;
	int y = *height;
	int i;
	int j;
    for (i = 0; i < *height; i++)
    {
        for (j = 0; j < *width; j++)
        {
            Uint8 r, g, b, a;
            Uint32 c;
            
            c = getPixel(surface, j, i);
            SDL_GetRGBA(c, surface->format, &r, &g, &b, &a);
            
            data[(frame - 1) * x * y + i * x + j] = get_color_index(r, g, b, a);
        }
    }
    
    SDL_FreeSurface(surface);
    return data;
}

void convert_to_pix(const char* filename)
{
    unsigned char numframes, x, y;
    unsigned char* data = load_pix_data(filename, &numframes, &x, &y);
    
    // Save it to pix
    char outname[200];
    snprintf(outname, 200, "%s.pix", filename);
    
    FILE* outfile;
    outfile = fopen(outname, "wb");
    fwrite(&numframes, 1, 1, outfile);
    fwrite(&x, 1, 1, outfile);
    fwrite(&y, 1, 1, outfile);
    fwrite(data, (numframes*x*y), 1, outfile);
    fclose(outfile);

    printf("File saved: %s\n", outname);
    
    free(data);
}


void convert_to_png(const char* filename)
{
    unsigned char numframes, x, y;
    unsigned char* data = load_pix_data(filename, &numframes, &x, &y);
    
	int i, j;
	int frame;

	printf("=================== %s ===================\n", filename);
	printf("num of frames: %d\nx: %d\ny: %d\n",numframes,x,y);
	
	SDL_Init(SDL_INIT_VIDEO);
	
	printf("Saving pix frames to png\n");
	for(frame = 1; frame <= numframes; frame++)
    {
        SDL_Surface* pixie = SDL_CreateRGBSurface(SDL_SWSURFACE, x, y, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
        
        // Draw sprite frame
        for (i = 0; i < y; i++)
        {
            for (j = 0; j < x; j++)
            {
                SDL_Rect rect;
                int r, g, b, c, d;

                d = data[(frame - 1) * x * y + i * x + j];
                r = ourcolors[d * 3] * 4;
                g = ourcolors[d * 3 + 1] * 4;
                b = ourcolors[d * 3 + 2] * 4;

                rect.x = j;
                rect.y = i;
                rect.w = 1;
                rect.h = 1;
                
                if(r > 0 || g > 0 || b > 0)
                {
                    c = SDL_MapRGB(pixie->format, r, g, b);

                    SDL_FillRect(pixie,&rect,c);
                }
            }
        }
        
        // Save result
        char buf[200];
        snprintf(buf, 200, "%s%d.png", filename, frame);
        SDL_SavePNG(pixie, buf);
        printf("Frame saved: %s\n", buf);
    }


	free(data);
}

void concatenate_pix(int numFiles, char** files)
{
    unsigned char total_frames = 0;
    unsigned char numframes, width, height, w, h;
    unsigned char* data;
    
    char outname[200];
    FILE* outfile;
    
    char first = 1;
    
    int i;
    for(i = 0; i < numFiles; i++)
    {
        char* filename = files[i];
        data = load_pix_data(filename, &numframes, &w, &h);
        
        if(first)
        {
            first = 0;
            
            total_frames += numframes;
            width = w;
            height = h;
            
            // Open new file for writing
            snprintf(outname, 200, "%s.pix", filename);
            outfile = fopen(outname, "wb");
            
            fwrite(&total_frames, 1, 1, outfile);  // This will be rewritten later
            fwrite(&width, 1, 1, outfile);
            fwrite(&height, 1, 1, outfile);
        }
        else
        {
            if(w != width || h != height)
            {
                // Mismatched dims error
                printf("File (%s) dimensions (%ux%u) do not match the output file (%ux%u).\n", filename, w, h, width, height);
                fclose(outfile);
                remove(outname);
                exit(1);
            }
            
            total_frames += numframes;
        }
        
        fwrite(data, (numframes*w*h), 1, outfile);
        free(data);
    }
    
    // Go back to the beginning to update the number of frames
    fseek(outfile, 0, SEEK_SET);
    fwrite(&total_frames, 1, 1, outfile);
    
    printf("File saved: %s\n", outname);
    fclose(outfile);
}

void parse_args(int argc, char **argv, int* mode, int* numFiles, char** files)
{
    int i = 1;
    if(argc < 2)
    {
        // Error
        *mode = 0;
        return;
    }
    
    // Normal conversion mode
    *mode = 1;
    
    
    // Grab the rest of the files list
    int j = 0;
    while(i < argc)
    {
        char* arg = argv[i];
        int len = strlen(arg);
        
        if(len > 1 && arg[0] == '-')
        {
            // It's a flag
            if(arg[1] == 'c' || strcmp(arg, "--cat") == 0 || strcmp(arg, "--concat") == 0 || strcmp(arg, "--concatenate") == 0)
            {
                // Concatenate files into one pix
                *mode = 2;
            }
            else if(arg[1] == 'n' || strcmp(arg, "--no-cycle") == 0 || strcmp(arg, "--no-cycling") == 0)
            {
                // Disable matching cycling colors in image -> pix conversions
                no_cycling_colors = 1;
            }
        }
        else
        {
            // It's a file
            files[j] = arg;
            j++;
        }
        
        i++;
    }
    
    *numFiles = j;
}

int main(int argc, char **argv)
{
    int mode;
    int numFiles;
    char* files[argc];
    parse_args(argc, argv, &mode, &numFiles, files);
    
	switch(mode)
    {
        case 0:
            printf("\nUSAGE\n"
                    "Convert image to pix:\n    pixconvert image.ext\n"
                    "Convert pix to png:\n    pixconvert image.pix\n"
                    "Concatenate pix into multiframe/animated pix:\n    pixconvert -c image1.ext image2.ext ...\n"
                    "\nOPTIONS\n"
                    " -c, --cat, --concat, --concatenate\n    Combines the listed files into one pix file as frames.  Every listed file must have the same width and height.\n\n"
                    " -n, --no-cycling\n    Disables matching cycling colors when converting an image file to pix.\n\n"
                    );
            return 1;
        case 1:
            {
                int i;
                for(i = 0; i < numFiles; i++)
                {
                    char* filename = files[i];
                    Uint8 usingPix = (strcmp(get_filename_ext(filename), "pix") == 0);
                    
                    if(usingPix)
                        convert_to_png(filename);
                    else
                        convert_to_pix(filename);
                }
            }
            break;
        case 2:
            {
                concatenate_pix(numFiles, files);
            }
            break;
	}
	
    
    SDL_Quit();
	return 0;
}
