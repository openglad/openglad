//
// H file for palette.cpp
//

//#include <stdio.h>
//the above is included in palette.cpp now

#include "SDL/SDL_types.h"

short load_and_set_palette(char *filename,Uint8  *newpalette); // load/set palette from disk
short load_palette(char *filename,Uint8 *newpalette); // load palette from disk
short set_palette(Uint8 *newpalette); // set palette
void adjust_palette(Uint8 *whichpal, short amount); //gamma correction??
void cycle_palette(Uint8 *newpalette, short start, short end, short shift); //color cycling

void query_palette_reg(unsigned char index, int *red, int *green, int *blue);
void set_palette_reg(unsigned char index,unsigned char red,unsigned char green,unsigned char blue);
short save_palette(Uint8 * whatpalette);

