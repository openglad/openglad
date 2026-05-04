/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//
// Palette.cpp file -- holds (what else?) palette routines
//
// Created: 02-05-95
//
/* ChangeLog
	buffers: 8/14/02: *added our_pal_lookup func so we don't need our.pal
	Zardus: 8/20/02: added return 0 for save_pallete so VC++ is happy
*/

#include <openglad/interface/render/pal32.h>
#include <openglad/resources/our_palette.h>
#include <openglad/interface/session_state.h>
#include <cstddef>
#include <cstdio>
#include <span>

//buffers: PORT: we need a palette to store the current palette
// Palette globals live in GameSession — access via current_session->curpal_/temppal_.

//
// load_and_set_palette
// Loads palette from file FILENAME,
// stores palette info in NEWPALETTE, and
// sets the current palette to this.
//
short load_and_set_palette(const char * /*filename*/, std::span<unsigned char> newpalette)
{
	std::size_t i;

	//buffers: don't need this file stuff since we use our_pal_lookup instead
	/*	if ( (infile = fopen(filename, "rb")) == nullptr ) // open for read
		{
			Log("Error in reading palette file %s\n", filename);
			return 0;
		}

		if (fread(og::runtime::current_session->temppal_, 1, 768, infile) != 768 || ferror(infile))
		{
			Log("Error: Corrupt palette file %s!\n", filename);
			return 0;
		}

		fclose(infile);
	*/
	// Copy back the palette info ..
	for (i = 0; i < 768; i++)
		//newpalette[i] = temppal[i];
		newpalette[i] = our_pal_lookup(static_cast<int>(i));

	//set_palette(temppal);
	set_palette(newpalette);

	return 1;
}

//
// save_palette
// save the dos palette so we can restore it when done
//
short save_palette(std::span<const unsigned char> /*whatpalette*/)
{
	//buffers: PORT: we don't have a palette to save :P
	return 0;
}


//
// load_palette
// Loads palette from file FILENAME into NEWPALETTE
//
short load_palette(const char * /*filename*/, std::span<unsigned char> newpalette)
{
	std::size_t i;

	/* buffers: we don't need this since we use our_pal_lookup() now
		if ( (infile = fopen(filename, "rb")) == nullptr ) // open for read
		{
			Log("Error in reading palette file %s\n", filename);
			return 0;
		}

		if (fread(og::runtime::current_session->temppal_, 1, 768, infile) != 768 || ferror(infile))
		{
			Log("Error: Corrupt palette file %s!\n", filename);
			return 0;
		}

		fclose(infile);
	*/
	// Copy back the palette info ..
	for (i = 0; i < 768; i++)
		//newpalette[i] = temppal[i];
		newpalette[i] = our_pal_lookup(static_cast<int>(i));

	return 1;
}


//
// set_palette
// Sets the current palette to NEWPALETTE.
//
short set_palette(std::span<const unsigned char> newpalette)
{
	std::size_t i;

	// Copy over the palette info ..
	for (i = 0; i < 768; i++)
		og::runtime::current_session->curpal_[i] = newpalette[i];

	return 1;
}

//
// adjust_palette
// Performs gamma correction (lightening/darkening)
//  on whichpal based on a positive or negative amount;
//  displays new palette, but does NOT affect whichpal
//
void adjust_palette(std::span<const unsigned char> whichpal, Sint32 amount)
{
	std::size_t i;
	short tempcol;
	short multiple = static_cast<short>(amount * 10);

	// Copy whichpal to temppal for setting ..
	for (i = 0; i < 768; i++)
	{
		tempcol = whichpal[i];

		// Now modify the current color bit based on 'amount'
		// Convert the 'amount' to = x*10% + x; ie, 2=(20% +2) increase
		tempcol = static_cast<short>( ( ( tempcol * (100+multiple) ) / 100) + amount);
		if (tempcol < 0)
			tempcol = 0;
		if (tempcol > 63)
			tempcol = 63;

		// Now set the current palette index to modified bit value
		og::runtime::current_session->curpal_[i] = static_cast<unsigned char>(tempcol);
	}
}

//
// cycle_palette
// Cycle and display newpalette
//
void cycle_palette(std::span<unsigned char> newpalette, short start, short end, short shift)
{
	std::size_t i;
	short length = static_cast<short>(end-start);
	short newval;
	short colorspot;

	// Copy over the palette info ..
	for (i = 0; i < 768; i += 3)
	{
		colorspot = static_cast<short>(i/3);
		if ( (colorspot>= start) && (colorspot <= end) )
		{
			newval = static_cast<short>(colorspot-shift);
			if (newval<start)
				newval += length;
			newval *= 3;
			const auto newval_u = static_cast<std::size_t>(newval);
			og::runtime::current_session->temppal_[i]   = newpalette[newval_u];
			og::runtime::current_session->temppal_[i+1] = newpalette[newval_u+1];
			og::runtime::current_session->temppal_[i+2] = newpalette[newval_u+2];
		}
		else
		{
			og::runtime::current_session->temppal_[i]   = newpalette[i];
			og::runtime::current_session->temppal_[i+1] = newpalette[i+1];
			og::runtime::current_session->temppal_[i+2] = newpalette[i+2];
		}
	}

	// Return the modified palette
	for (i = 0; i < 768; i++)
	{
		newpalette[i] = og::runtime::current_session->temppal_[i];
		//buffers: since this is supposed to load the pal too, we
		//buffers: copy it over to ourpal.
		og::runtime::current_session->curpal_[i] = og::runtime::current_session->temppal_[i];
	}
}

void query_palette_reg(unsigned char index, int *red, int *green, int *blue)
{
	int tred, tgreen, tblue;

	tred = static_cast<int>(og::runtime::current_session->curpal_[index*3]);
	tgreen = static_cast<int>(og::runtime::current_session->curpal_[index*3+1]);
	tblue = static_cast<int>(og::runtime::current_session->curpal_[index*3+2]);

	*red = tred;
	*green = tgreen;
	*blue = tblue;
}

void set_palette_reg(unsigned char index,int red,int green,int blue)
{
	// Palette values are expected to be in the classic 0..63 range.
	const auto clamp63 = [](int v) -> unsigned char {
		if (v < 0) return 0;
		if (v > 63) return 63;
		return static_cast<unsigned char>(v);
	};
	og::runtime::current_session->curpal_[index*3] = clamp63(red);
	og::runtime::current_session->curpal_[index*3+1] = clamp63(green);
	og::runtime::current_session->curpal_[index*3+2] = clamp63(blue);
}
