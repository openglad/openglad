//
// Palette.cpp file -- holds (what else?) palette routines
//
// Created: 02-05-95
//
#include "pal32.h"
#include <stdio.h>
#include "SDL/SDL_types.h"
// Z's script: #include <i86.h>
// Z's script: #include <conio.h>

#define PAL_MASK 0x03C6
#define PAL_REG_RD 0x03C7
#define PAL_REG_WR 0x03C8
#define PAL_DATA 0x03C9
#define PAL_BLUE (0x7)
#define PAL_GREEN (0x38)
#define PAL_RED (0x1C0)

//buffers: PORT: changed these arrays to type int
char temppal[768];    // for loading, setting, etc.
char gammapal[768];   // for gamma-correction

//buffers: PORT: we need a palette to store the current palette
char curpal[768];

//
// load_and_set_palette
// Loads palette from file FILENAME,
// stores palette info in NEWPALETTE, and
// sets the current palette to this.
//
short load_and_set_palette(char *filename, char *newpalette)
{
	FILE *infile;
	short i;

	if ( (infile = fopen(filename, "rb")) == NULL ) // open for read
	{
		printf("Error in reading palette file %s\n", filename);
		return 0;
	}

	if (fread(temppal, 1, 768, infile) != 768 || ferror(infile))
	{
		printf("Error: Corrupt palette file %s!\n", filename);
		return 0;
	}

	fclose(infile);

	// Copy back the palette info ..
	for (i=0; i < 768; i++)
		newpalette[i] = temppal[i];

	set_palette(temppal);

	return 1;
}

//
// save_palette
// save the dos palette so we can restore it when done
//
short save_palette(char * whatpalette)
{
	//buffers: PORT: we don't have a palette to save :P
}
  

//
// load_palette
// Loads palette from file FILENAME shorto NEWPALETTE
//
short load_palette(char *filename, char *newpalette)
{
	FILE *infile;
	short i;

	if ( (infile = fopen(filename, "rb")) == NULL ) // open for read
	{
		printf("Error in reading palette file %s\n", filename);
		return 0;
	}

	if (fread(temppal, 1, 768, infile) != 768 || ferror(infile))
	{
		printf("Error: Corrupt palette file %s!\n", filename);
		return 0;
	}

	fclose(infile);

	// Copy back the palette info ..
	for (i=0; i < 768; i++)
		newpalette[i] = temppal[i];

	return 1;
}


//
// set_palette
// Sets the current palette to NEWPALETTE.
//
short set_palette(char *newpalette)
{
	short i;

	// Copy over the palette info ..
	for (i=0; i < 768; i++)
		curpal[i] = newpalette[i];

	return 1;
}

//
// adjust_palette
// Performs gamma correction (lightening/darkening)
//  on whichpal based on a positive or negative amount;
//  displays new palette, but does NOT affect whichpal
//
void adjust_palette(char *whichpal, short amount)
{
	short i;
	short tempcol;
	short multiple = (short) (amount * 10);

	// Copy whichpal to temppal for setting ..
	for (i=0; i < 768; i++)
	{
		tempcol = whichpal[i];

		// Now modify the current color bit based on 'amount'
		// Convert the 'amount' to = x*10% + x; ie, 2=(20% +2) increase
		tempcol = (short) ( ( ( tempcol * (100+multiple) ) / 100) + amount);
		if (tempcol < 0) tempcol = 0;
		if (tempcol > 63) tempcol = 63;

		// Now set the current palette index to modified bit value
		curpal[i] = (char) tempcol;
	}
}

//
// cycle_palette
// Cycle and display newpalette
//
void cycle_palette(char *newpalette, short start, short end, short shift)
{
	short i;
	short length = (short) (end-start);
	short newval;
	short colorspot;
	short begin = (short) (start*3);

	// Copy over the palette info ..
	for (i=0; i < 768; i+=3)
	{
		colorspot = (short) (i/3);
		if ( (colorspot>= start) && (colorspot <= end) )
		{
			newval = (short) (colorspot-shift);
			if (newval<start)
				newval += length;
			newval *= 3;
			temppal[i]   = newpalette[newval];
			temppal[i+1] = newpalette[newval+1];
			temppal[i+2] = newpalette[newval+2];
		}
		else
		{
			temppal[i]   = newpalette[i];
			temppal[i+1] = newpalette[i+1];
			temppal[i+2] = newpalette[i+2];
		}	
	}

  	// Return the modified palette
	for (i=0; i < 768; i++) {
		newpalette[i] = temppal[i];
		//buffers: since this is supposed to load the pal too, we
		//buffers: copy it over to ourpal.
		curpal[i] = temppal[i];
	}
}

void query_palette_reg(unsigned char index, int *red, int *green, int *blue)
{
 	int tred, tgreen, tblue;

	tred = (int)curpal[index*3];
	tgreen = (int)curpal[index*3+1];
	tblue = (int)curpal[index*3+2];
 
 	*red = tred; *green = tgreen; *blue = tblue;
}

void set_palette_reg(unsigned char index,int red,int green,int blue)
{
	curpal[index*3] = red;
	curpal[index*3+1] = green;
	curpal[index*3+2] = blue;
}

