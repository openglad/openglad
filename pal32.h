//
// H file for palette.cpp
//

//#include <stdio.h>
//the above is included in palette.cpp now

#include "SDL/SDL_types.h"

short load_and_set_palette(char *filename,char  *newpalette); // load/set palette from disk
short load_palette(char *filename,char *newpalette); // load palette from disk
short set_palette(char *newpalette); // set palette
void adjust_palette(char *whichpal, short amount); //gamma correction??
void cycle_palette(char *newpalette, short start, short end, short shift); //color cycling

void query_palette_reg(unsigned char index, int *red, int *green, int *blue);
void set_palette_reg(unsigned char index,int red,int green,int blue);
short save_palette(char * whatpalette);

