/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <SDL3/SDL.h>
#include <openglad/platform/sai2x.h>
#include <openglad/platform/video_sdl.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/util.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <array>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
//#include "os_depend.h"

#ifdef __EMSCRIPTEN__
EM_JS(void, publish_canvas_diagnostics,
      (int zoom_steps, int width, int height, int scale_mode,
       int smart_used, int smart_suppressed,
       int gameplay_ui_width, int gameplay_ui_height), {
  const smoothing = scale_mode === 2 ? 'sai' :
                    scale_mode === 3 ? 'eagle' : 'off';
  window.__opengladCanvasDiagnostics = {
    zoom_steps,
    zoom: zoom_steps === 10 ? '1.0' : '0.' + zoom_steps,
    world_width: width,
    world_height: height,
    smoothing,
    smart_used: Boolean(smart_used),
    smart_suppressed: Boolean(smart_suppressed),
    gameplay_ui_width,
    gameplay_ui_height,
  };
});
#else
static void publish_canvas_diagnostics(int, int, int, int, int, int,
                                       int, int) {}
#endif

// Private var for SAI2x
static Uint32 colorMask = 0xF7DEF7DE;
static Uint32 lowPixelMask = 0x08210821;
static Uint32 qcolorMask = 0xE79CE79C;
static Uint32 qlowpixelMask = 0x18631863;
[[maybe_unused]] static Uint32 redblueMask = 0xF81F;
[[maybe_unused]] static Uint32 greenMask = 0x7E0;
[[maybe_unused]] static int PixelsPerMask = 2;
static int xsai_depth = 0;

static std::array<unsigned char*, 4> src_line{};
static std::array<unsigned char*, 2> dst_line{};

// SDL window/event coordinates stay logical on HiDPI displays, while the
// renderer backbuffer and exclusive mode are measured in physical pixels.
// Convert the existing logical overscan rectangle at present time so a
// density-2 fullscreen mode fills all 4K pixels without changing pointer
// mapping or the byte-identical density-1 path.
static SDL_FRect renderer_output_rect(SDL_Renderer* renderer,
                                      float x, float y, float w, float h)
{
	if (renderer == nullptr || og::runtime::current_session == nullptr)
		return {x, y, w, h};
	const float logical_w = og::runtime::current_session->window_w_;
	const float logical_h = og::runtime::current_session->window_h_;
	int output_w = 0;
	int output_h = 0;
	if (logical_w <= 0.0f || logical_h <= 0.0f ||
	    !SDL_GetRenderOutputSize(renderer, &output_w, &output_h) ||
	    output_w <= 0 || output_h <= 0)
	{
		return {x, y, w, h};
	}
	const float scale_x = static_cast<float>(output_w) / logical_w;
	const float scale_y = static_cast<float>(output_h) / logical_h;
	return {x * scale_x, y * scale_y, w * scale_x, h * scale_y};
}

#define GET_RESULT(A, B, C, D) ((A != C || A != D) - (B != C || B != D))

#define INTERPOLATE(A, B) (((A & colorMask) >> 1) + ((B & colorMask) >> 1) + (A & B & lowPixelMask))

#define Q_INTERPOLATE(A, B, C, D) ((A & qcolorMask) >> 2) + ((B & qcolorMask) >> 2) + ((C & qcolorMask) >> 2) + ((D & qcolorMask) >> 2) \
	+ ((((A & qlowpixelMask) + (B & qlowpixelMask) + (C & qlowpixelMask) + (D & qlowpixelMask)) >> 2) & qlowpixelMask)


// works only for bpp 32 !
int Init_2xSaI()
{
	redblueMask = 0xFF00FF00;
	greenMask = 0x00FF0000;
	PixelsPerMask = 1;
/*
		Color Mask:       0xFEFEFE
		Low Pixel Mask:   0x10101
		QColor Mask:      0xFCFCFC
		QLow Pixel Mask:  0x30303

*/
	colorMask=0xFEFEFE;
	lowPixelMask=0x10101;
	qcolorMask=0xFCFCFC;
	qlowpixelMask=0x30303;
	xsai_depth = 32;
	return 0;
}






void Super2xSaI_ex2(
	unsigned char* src, 
	int srcx, int srcy, 
	int srcw, int srch, 
	int src_pitch,
	int src_height,
	unsigned char* dst,
	int dstx,
	int dsty,
	int dst_pitch )
{
	unsigned char* srcPtr = src + (4*srcx +   srcy*src_pitch );
	unsigned char* dstPtr = dst + (4*dstx +   dsty*dst_pitch );

	src_pitch=src_pitch/4;
	dst_pitch=dst_pitch/4;
	
	if (srcx + srcw >= src_pitch)
	{
		srcw = src_pitch - srcx;
	}

	int ybeforelast1 = src_height - 1 - srcy;
    int ybeforelast2 = src_height - 2 - srcy;
    int xbeforelast1 = src_pitch - 1 - srcx;
    int xbeforelast2 = src_pitch - 2 - srcx;

	for (int y = 0; y < srch; y++) 
	{
		Uint32* bP = reinterpret_cast<Uint32*>(srcPtr);
		Uint32* dP = reinterpret_cast<Uint32*>(dstPtr);
	
		for (int x = 0; x < srcw; x++) 
		{
           Uint32 color4, color5, color6;
           Uint32 color1, color2, color3;
           Uint32 colorA0, colorA1, colorA2, colorA3,
						colorB0, colorB1, colorB2, colorB3,
						colorS1, colorS2;
           Uint32 product1a, product1b,
 					  product2a, product2b;
 
			int add1, add2;
			int sub1;
			int nextl1, nextl2;
			int prevl1;

			if (x==0)
				sub1=0;
			else
				sub1=0;

			if (x >= xbeforelast2)
				add2 = 0;
			else add2 = 1;

			if (x >= xbeforelast1)
				add1 = 0;
			else add1 = 1;

			if (y == 0)
				prevl1 = 0;
			else
				prevl1 = src_pitch;

			if (y >= ybeforelast2)
				nextl2 = 0;
			else 
				nextl2 = src_pitch;

			if (y >= ybeforelast1)
				nextl1 = 0;
			else 
				nextl1 = src_pitch;

            colorB0 = *(bP- prevl1 - sub1);
            colorB1 = *(bP- prevl1);
            colorB2 = *(bP- prevl1 + add1);
            colorB3 = *(bP- prevl1 + add1 + add2);

            color4 = *(bP - sub1);
            color5 = *(bP);
            color6 = *(bP + add1);
            colorS2 = *(bP + add1 + add2);

            color1 = *(bP + nextl1 - sub1);
            color2 = *(bP + nextl1);
            color3 = *(bP + nextl1 + add1);
            colorS1 = *(bP + nextl1 + add1 + add2);

            colorA0 = *(bP + nextl1 + nextl2 - sub1);
            colorA1 = *(bP + nextl1 + nextl2);
            colorA2 = *(bP + nextl1 + nextl2 + add1);
            colorA3 = *(bP + nextl1 + nextl2 + add1 + add2);

//---------------------------------------  B0 B1 B2 B3    0  1  2  3
//                                         4  5* 6  S2 -> 4  5* 6  7
//                                         1  2  3  S1    8  9 10 11
//                                         A0 A1 A2 A3   12 13 14 15
//--------------------------------------
			if (color2 == color6 && color5 != color3)
			    product2b = product1b = color2;
			else if (color5 == color3 && color2 != color6)
				product2b = product1b = color5;
			else if (color5 == color3 && color2 == color6)
			{
			   	int r = 0;

               	r += GET_RESULT (color6, color5, color1, colorA1);
               	r += GET_RESULT (color6, color5, color4, colorB1);
               	r += GET_RESULT (color6, color5, colorA2, colorS1);
               	r += GET_RESULT (color6, color5, colorB2, colorS2);

			   	if (r > 0)
				 	product2b = product1b = color6;
			   	else if (r < 0)
					product2b = product1b = color5;
			   	else
				  	product2b = product1b = INTERPOLATE (color5, color6);
			}
			else
			{

			   if (color6 == color3 && color3 == colorA1 && color2 != colorA2 && color3 != colorA0)
				  	product2b = Q_INTERPOLATE (color3, color3, color3, color2);
			   else if (color5 == color2 && color2 == colorA2 && colorA1 != color3 && color2 != colorA3)
				  	product2b = Q_INTERPOLATE (color2, color2, color2, color3);
			   else
				  	product2b = INTERPOLATE (color2, color3);


			   if (color6 == color3 && color6 == colorB1 && color5 != colorB2 && color6 != colorB0)
				  	product1b = Q_INTERPOLATE (color6, color6, color6, color5);
			   else if (color5 == color2 && color5 == colorB2 && colorB1 != color6 && color5 != colorB3)
				  	product1b = Q_INTERPOLATE (color6, color5, color5, color5);
			   else
				  	product1b = INTERPOLATE (color5, color6);
			}

			if (color5 == color3 && color2 != color6 && color4 == color5 && color5 != colorA2)
			   	product2a = INTERPOLATE (color2, color5);
			else if (color5 == color1 && color6 == color5 && color4 != color2 && color5 != colorA0)
			   	product2a = INTERPOLATE(color2, color5);
			else
				product2a = color2;

			if (color2 == color6 && color5 != color3 && color1 == color2 && color2 != colorB2)
			   	product1a = INTERPOLATE (color2, color5);
			else if (color4 == color2 && color3 == color2 && color1 != color5 && color2 != colorB0)
			   	product1a = INTERPOLATE(color2, color5);
			else
				product1a = color5;

			*dP = product1a;
			*(dP+1) = product1b;
			*(dP+dst_pitch) = product2a;
			*(dP+dst_pitch+1) = product2b;


			bP += 1;
			dP += 2;

		}
		srcPtr +=   src_pitch*4; 
		dstPtr += 2*dst_pitch*4; 
	}
}


void Scale_SuperEagle(
	unsigned char* src, 
	int srcx, int srcy, 
	int srcw, int srch, 
	int src_pitch,
	int src_height,
	unsigned char* dst,
	int dstx,
	int dsty,
	int dst_pitch )
{

	// Need to ensure that the update is alligned to 4 pixels - Colourless
	// The idea was to prevent artifacts from appearing, but it doesn't seem
	// to help
	/*
	{
		int sx = ((srcx-4)/4)*4;
		int ex = ((srcx+srcw+7)/4)*4;
		int sy = ((srcy-4)/4)*4;
		int ey = ((srcy+srch+7)/4)*4;

		if (sx < 0) sx = 0;
		if (sy < 0) sy = 0;
		if (ex > sline_pixels) ex = sline_pixels;
		if (ey > sheight) ey = sheight;

		srcx = sx;
		srcy = sy;
		srcw = ex - sx;
		srch = ey - sy;
	}
	*/

	unsigned char *srcPtr = src + (4*srcx + srcy*src_pitch);
	unsigned char *dstPtr = dst + (4*dstx + dsty*dst_pitch);

	src_pitch=src_pitch/4;
	dst_pitch=dst_pitch/4;
	
	if (srcx + srcw >= src_pitch)
	{
		srcw = src_pitch - srcx;
	}

    int ybeforelast1 = src_height - 1 - srcy;
    int ybeforelast2 = src_height - 2 - srcy;
    int xbeforelast1 = src_pitch - 1 - srcx;
    int xbeforelast2 = src_pitch - 2 - srcx;

    for (int y = 0; y < srch; y++)
	{
		Uint32 *bP = reinterpret_cast<Uint32*>(srcPtr);
		Uint32 *dP = reinterpret_cast<Uint32*>(dstPtr);

		for (int x = 0; x < srcw; x++)
		{
			Uint32	color4, color5, color6;
			Uint32	color1, color2, color3;
			//Uint32	colorA0, colorA3, colorB0, colorB3;
			Uint32	colorA1, colorA2,
							colorB1, colorB2,
							colorS1, colorS2;
			Uint32	product1a, product1b,
							product2a, product2b;

			//---------------------------------------  B0 B1 B2 B3
			//                                         4  5  6  S2
			//                                         1  2  3  S1
			//                                         A0 A1 A2 A3
			//--------------------------------------
			int add1, add2;
			int sub1;
			int nextl1, nextl2;
			int prevl1;

			if (x == 0)
				sub1 = 0;
			else
				sub1 = 1;

			if (x >= xbeforelast2)
				add2 = 0;
			else add2 = 1;

			if (x >= xbeforelast1)
				add1 = 0;
			else add1 = 1;

			if (y == 0)
				prevl1 = 0;
			else
				prevl1 = src_pitch;

			if (y >= ybeforelast2)
				nextl2 = 0;
			else 
				nextl2 = src_pitch;

			if (y >= ybeforelast1)
				nextl1 = 0;
			else 
				nextl1 = src_pitch;


            //colorB0 = *(bP- prevl1 - sub1);
            colorB1 = *(bP- prevl1);
            colorB2 = *(bP- prevl1 + add1);
            //colorB3 = *(bP- prevl1 + add1 + add2);

            color4 = *(bP - sub1);
            color5 = *(bP);
            color6 = *(bP + add1);
            colorS2 = *(bP + add1 + add2);

            color1 = *(bP + nextl1 - sub1);
            color2 = *(bP + nextl1);
            color3 = *(bP + nextl1 + add1);
            colorS1 = *(bP + nextl1 + add1 + add2);

            //colorA0 = *(bP + nextl1 + nextl2 - sub1);
            colorA1 = *(bP + nextl1 + nextl2);
            colorA2 = *(bP + nextl1 + nextl2 + add1);
            //colorA3 = *(bP + nextl1 + nextl2 + add1 + add2);


			if (color2 == color6 && color5 != color3)
			{
			   product1b = product2a = color2;
			   /*manip.copy(product2a, color2);
			   product1b = product2a;*/


			   if ((color1 == color2) || (color6 == colorB2))
			   {
				   product1a = INTERPOLATE (color2, color5);
				   product1a = INTERPOLATE (color2, product1a);
				   //product1a = QInterpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color2, color2, color2, color5, manip);

			   }
			   else
			   {
				   product1a = INTERPOLATE (color5, color6);
				   //product1a = Interpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color6, color5, manip);
			   }

			   if ((color6 == colorS2) || (color2 == colorA1))
               {
                   product2b = INTERPOLATE (color2, color3);
                   product2b = INTERPOLATE (color2, product2b);
				   //product2b = QInterpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color2, color2, color2, color3, manip);

               }
               else
               {
                   product2b = INTERPOLATE (color2, color3);
				   //product2b = Interpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color2, color3, manip);
               }
            }
            else
            if (color5 == color3 && color2 != color6)
            {
               product2b = product1a = color5;
   			   //manip.copy(product1a, color5);
			   //product2b = product1a;

 
               if ((colorB1 == color5) ||  (color3 == colorS1))
               {
                   product1b = INTERPOLATE (color5, color6);
				   product1b = INTERPOLATE (color5, product1b);
				   //product1b = QInterpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color5, color5, color5, color6, manip);
               }
               else
               {
                  product1b = INTERPOLATE (color5, color6);
				  //product1b = Interpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color5, color6, manip);
               }

			   if ((color3 == colorA2) || (color4 == color5))
               {
                   product2a = INTERPOLATE (color5, color2);
                   product2a = INTERPOLATE (color5, product2a);
				   //product2a = QInterpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color2, color5, color5, color5, manip);
               }
               else
               {
                  product2a = INTERPOLATE (color2, color3);
				  //product2a = Interpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color3, color2, manip);
               }

            }
            else
            if (color5 == color3 && color2 == color6)
            {
               int r = 0;

               r += GET_RESULT (color6, color5, color1, colorA1);
               r += GET_RESULT (color6, color5, color4, colorB1);
               r += GET_RESULT (color6, color5, colorA2, colorS1);
               r += GET_RESULT (color6, color5, colorB2, colorS2);
			   //r += GetResult1 <Source_pixel>(color5, color6, color4, colorB1);
			   //r += GetResult2 <Source_pixel>(color6, color5, colorA2, colorS1);
			   //r += GetResult2 <Source_pixel>(color6, color5, color1, colorA1);
 			   //r += GetResult1 <Source_pixel>(color5, color6, colorB2, colorS2);

               if (r > 0)
               {
				   product1b = product2a = color2;
  				   //manip.copy(product2a, color2);
				   //product1b = product2a;
                  product1a = product2b = INTERPOLATE (color5, color6);
				  //product1a = product2b = Interpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color5, color6, manip);
               }
               else
               if (r < 0)
               {
                  product2b = product1a = color5;
				  // manip.copy(product1a, color5);
				  // product2b = product1a;
                  product1b = product2a = INTERPOLATE (color5, color6);
				  //product1b = product2a = Interpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color5, color6, manip);
               }
               else
               {
                  product2b = product1a = color5;
				  // manip.copy(product1a, color5);
				   //product2b = product1a;
                  product1b = product2a = color2;
				  // manip.copy(product2a, color2);
  				   //product1b = product2a;

               }
            }
            else
            {
                  product2b = product1a = INTERPOLATE (color2, color6);
                  product2b = Q_INTERPOLATE (color3, color3, color3, product2b);
                  product1a = Q_INTERPOLATE (color5, color5, color5, product1a);
				  //product2b = OInterpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color3, color2, color6, manip);
				  //product1a = OInterpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color5, color6, color2, manip);

                  product2a = product1b = INTERPOLATE (color5, color3);
                  product2a = Q_INTERPOLATE (color2, color2, color2, product2a);
                  product1b = Q_INTERPOLATE (color6, color6, color6, product1b);
				  //product2a = OInterpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color2, color5, color3, manip);
				  //product1b = OInterpolate_2xSaI< Source_pixel,  Dest_pixel,  Manip_pixels>(color6, color5, color3, manip);
			}

			*dP = product1a;
			*(dP+1) = product1b;
			*(dP+dst_pitch) = product2a;
			*(dP+dst_pitch+1) = product2b;

			bP += 1;
			dP += 2;

		}
		srcPtr +=   src_pitch*4;
		dstPtr += 2*dst_pitch*4;
	}
}



void Super2xSaI_ex(unsigned char *src, Uint32 src_pitch, unsigned char *unused, unsigned char *dest, Uint32 dest_pitch, Uint32 width, Uint32 height) 
{


	(void)unused;
	//int j;
	unsigned int x, y;
	std::array<Uint32, 16> color{};

	/* Point to the first 3 lines. */
	src_line[0] = src;
	src_line[1] = src;
	src_line[2] = src + src_pitch;
	src_line[3] = src + src_pitch * 2;
	
	dst_line[0] = reinterpret_cast<unsigned char*>(dest);
	dst_line[1] = reinterpret_cast<unsigned char*>(dest) + dest_pitch;
	
	x = 0, y = 0;
	Uint32 *lbp;
	lbp = reinterpret_cast<Uint32*>(src_line[0]);
	color[0] = *lbp;       color[1] = color[0];   color[2] = color[0];    color[3] = color[0];
	color[4] = color[0];   color[5] = color[0];   color[6] = *(lbp + 1);  color[7] = *(lbp + 2);
	lbp = reinterpret_cast<Uint32*>(src_line[2]);
	color[8] = *lbp;     color[9] = color[8];     color[10] = *(lbp + 1); color[11] = *(lbp + 2);
	lbp = reinterpret_cast<Uint32*>(src_line[3]);
	color[12] = *lbp;    color[13] = color[12];   color[14] = *(lbp + 1); color[15] = *(lbp + 2);

	for (y = 0; y < height; y++) {
	
		/* Todo: x = width - 2, x = width - 1 */
		
		for (x = 0; x < width; x++) {
			Uint32 product1a, product1b, product2a, product2b;

//---------------------------------------  B0 B1 B2 B3    0  1  2  3
//                                         4  5* 6  S2 -> 4  5* 6  7
//                                         1  2  3  S1    8  9 10 11
//                                         A0 A1 A2 A3   12 13 14 15
//--------------------------------------
			if (color[9] == color[6] && color[5] != color[10]) {
				product2b = color[9];
				product1b = product2b;
			}
			else if (color[5] == color[10] && color[9] != color[6]) {
				product2b = color[5];
				product1b = product2b;
			}

			else if (color[5] == color[10] && color[9] == color[6]) {
				int r = 0;

				r += GET_RESULT(color[6], color[5], color[8], color[13]);
				r += GET_RESULT(color[6], color[5], color[4], color[1]);
				r += GET_RESULT(color[6], color[5], color[14], color[11]);
				r += GET_RESULT(color[6], color[5], color[2], color[7]);

				if (r > 0)
					product1b = color[6];
				else if (r < 0)
					product1b = color[5];
				else
					product1b = INTERPOLATE(color[5], color[6]);
					
				product2b = product1b;

			}
			else {
				if (color[6] == color[10] && color[10] == color[13] && color[9] != color[14] && color[10] != color[12])
					product2b = Q_INTERPOLATE(color[10], color[10], color[10], color[9]);
				else if (color[5] == color[9] && color[9] == color[14] && color[13] != color[10] && color[9] != color[15])
					product2b = Q_INTERPOLATE(color[9], color[9], color[9], color[10]);
				else
					product2b = INTERPOLATE(color[9], color[10]);

				if (color[6] == color[10] && color[6] == color[1] && color[5] != color[2] && color[6] != color[0])
					product1b = Q_INTERPOLATE(color[6], color[6], color[6], color[5]);
				else if (color[5] == color[9] && color[5] == color[2] && color[1] != color[6] && color[5] != color[3])
					product1b = Q_INTERPOLATE(color[6], color[5], color[5], color[5]);
				else
					product1b = INTERPOLATE(color[5], color[6]);
			}

			if (color[5] == color[10] && color[9] != color[6] && color[4] == color[5] && color[5] != color[14])
				product2a = INTERPOLATE(color[9], color[5]);
			else if (color[5] == color[8] && color[6] == color[5] && color[4] != color[9] && color[5] != color[12])
				product2a = INTERPOLATE(color[9], color[5]);
			else
				product2a = color[9];

			if (color[9] == color[6] && color[5] != color[10] && color[8] == color[9] && color[9] != color[2])
				product1a = INTERPOLATE(color[9], color[5]);
			else if (color[4] == color[9] && color[10] == color[9] && color[8] != color[5] && color[9] != color[0])
				product1a = INTERPOLATE(color[9], color[5]);
			else
				product1a = color[5];
	
			*reinterpret_cast<Uint32*>(&dst_line[0][x * 8]) = product1a;
			*reinterpret_cast<Uint32*>(&dst_line[0][x * 8 + 4]) = product1b;
			*reinterpret_cast<Uint32*>(&dst_line[1][x * 8]) = product2a;
			*reinterpret_cast<Uint32*>(&dst_line[1][x * 8 + 4]) = product2b;
			
			/* Move color matrix forward */
			color[0] = color[1]; color[4] = color[5]; color[8] = color[9];   color[12] = color[13];
			color[1] = color[2]; color[5] = color[6]; color[9] = color[10];  color[13] = color[14];
			color[2] = color[3]; color[6] = color[7]; color[10] = color[11]; color[14] = color[15];
			
			if (x < width - 3) {
				x += 3;
				color[3] = *((reinterpret_cast<Uint32*>(src_line[0])) + x);
				color[7] = *((reinterpret_cast<Uint32*>(src_line[1])) + x);
				color[11] = *((reinterpret_cast<Uint32*>(src_line[2])) + x);
				color[15] = *((reinterpret_cast<Uint32*>(src_line[3])) + x);
				x -= 3;
			}
		}

		/* We're done with one line, so we shift the source lines up */
		src_line[0] = src_line[1];
		src_line[1] = src_line[2];
		src_line[2] = src_line[3];		

		/* Read next line */
		if (y + 3 >= height)
			src_line[3] = src_line[2];
			else
				src_line[3] = src_line[2] + src_pitch;
				
			/* Then shift the color matrix up */
			lbp = reinterpret_cast<Uint32*>(src_line[0]);
			color[0] = *lbp; color[1] = color[0]; color[2] = *(lbp + 1); color[3] = *(lbp + 2);
		lbp = reinterpret_cast<Uint32*>(src_line[1]);
		color[4] = *lbp; color[5] = color[4]; color[6] = *(lbp + 1); color[7] = *(lbp + 2);
		lbp = reinterpret_cast<Uint32*>(src_line[2]);
		color[8] = *lbp; color[9] = color[9]; color[10] = *(lbp + 1); color[11] = *(lbp + 2);
		lbp = reinterpret_cast<Uint32*>(src_line[3]);
		color[12] = *lbp; color[13] = color[12]; color[14] = *(lbp + 1); color[15] = *(lbp + 2);
		
		if (y < height - 1) {
			dst_line[0] = reinterpret_cast<unsigned char*>(dest) + dest_pitch*(y*2+2);
			dst_line[1] = reinterpret_cast<unsigned char*>(dest) + dest_pitch*(y*2+3);
		}
	}
}

void Super2xSaI(SDL_Surface *src, SDL_Surface *dest, int s_x, int s_y, int d_x, int d_y, int w, int h)
{
	int sbpp, dbpp;

	if (!src || !dest)
	{
		LogError("In Super2xSaI, src or dest are nullptr\n");
		return;
	}

	sbpp = SDL_BITSPERPIXEL(src->format);
	dbpp = SDL_BITSPERPIXEL(dest->format);

	if ((sbpp != xsai_depth) || (sbpp != dbpp))	/* Must be same color depth */
	{
		LogError("In Super2xSaI, sbpp or dbpp are not equal to xsai_depth\n");
		return;
	}

	sbpp = SDL_BYTESPERPIXEL(src->format);
	dbpp = SDL_BYTESPERPIXEL(dest->format);

	if (w < 4 || h < 4)
	{
		/* Image is too small to be 2xSaI'ed. */
		LogWarn("Surface to copy is too small for 2xSaI\n");
		return;
	}	
	
	Super2xSaI_ex(
			reinterpret_cast<unsigned char*>(src->pixels) + src->pitch*s_y + s_x*sbpp, 
			static_cast<Uint32>(src->pitch),
			nullptr,
			reinterpret_cast<unsigned char*>(dest->pixels) + dest->pitch*d_y +d_x*dbpp,
			static_cast<Uint32>(dest->pitch),
			static_cast<Uint32>(w), static_cast<Uint32>(h));
	return;
}



/////////////////////////////////
//
Screen::Screen( RenderEngine engine, int width, int height, int fullscreen)
{
	Engine = engine;
	switch(Engine)
	{
	case RenderEngine::SAI:
		Init_2xSaI();
		break;
	case RenderEngine::Eagle:
		Init_2xSaI();
		break;
	default:
		break;
	}
    
    int w, h;
    #ifdef ANDROID
    w = 0;
    h = 0;
    fullscreen = true;
    #else
    w = width;
    h = height;
    #endif

    // SDL3: windows are shown by default; SDL_WINDOW_FULLSCREEN with no
    // exclusive mode set is borderless fullscreen-desktop (the SDL2
    // SDL_WINDOW_FULLSCREEN_DESKTOP behavior).
    SDL_WindowFlags window_flags = 0;
    if(fullscreen)
        window_flags |= SDL_WINDOW_FULLSCREEN;

    // This immutable create flag gives Retina/HiDPI fullscreen modes a real
    // physical-pixel backbuffer. renderer_output_rect() then expands the
    // logical viewport across that output; without it the compositor could
    // upscale a lower-resolution backing store after OpenGlad presents.
    window_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

	#ifdef __EMSCRIPTEN__
	// The page owns the CSS box. Let browser resize/fullscreen changes update
	// SDL's logical size while HIGH_PIXEL_DENSITY keeps the WebGL drawing buffer
	// at device-pixel resolution.
	window_flags |= SDL_WINDOW_RESIZABLE;
	#endif

    #ifdef __IPHONEOS__
    window_flags |= SDL_WINDOW_BORDERLESS;
    #endif

    window = SDL_CreateWindow("Gladiator", w, h, window_flags);
    if(window == nullptr)
        throw std::runtime_error(std::string("Fatal: SDL_CreateWindow failed: ") + SDL_GetError());
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // SDL3 defaults SDL_GL_ALPHA_SIZE to 8 (SDL2: 0). On Emscripten that
    // creates a translucent WebGL canvas (alpha:true, premultiplied), and the
    // XRGB canvas pixels carry alpha=0, so the browser composites the whole
    // game away to black. Request the SDL2-default opaque context everywhere.
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);

    #ifdef TESTING
    renderer = SDL_CreateRenderer(window, nullptr);
    #else
    renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);
    #endif

	SDL_GetWindowSize(window, &w, &h);
	og::runtime::current_session->window_w_ = static_cast<float>(w);
	og::runtime::current_session->window_h_ = static_cast<float>(h);

	update_overscan_setting();
	zoom_window_w_ = w;
	zoom_window_h_ = h;

    // The UI canvas is pinned at kUiCanvasW x kUiCanvasH; the world canvas
    // starts shared with it (aliased) at the same 320x200 dims, keeping the
    // renderer byte-identical to the historical single-canvas setup.
    ui_surf_ = SDL_CreateSurface(kUiCanvasW, kUiCanvasH, SDL_PIXELFORMAT_XRGB8888);
	ui_tex_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kUiCanvasW, kUiCanvasH);
    // SDL3 defaults alpha-format textures to SDL_BLENDMODE_BLEND (the canvas
    // pixels have alpha=0, so blending would draw nothing). The canvas
    // present is an opaque full-frame copy: force NONE on every canvas
    // texture. SDL3 also defaults texture filtering to LINEAR where SDL2's
    // render-scale-quality default was nearest — the canvas upscale must
    // stay chunky-crisp, not smoothed.
    SDL_SetTextureBlendMode(ui_tex_, SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(ui_tex_, SDL_SCALEMODE_NEAREST);
    world_surf_ = ui_surf_;
    world_tex_ = ui_tex_;
    render = ui_surf_;      // boot in UI mode (intro/menus draw first)
    render_tex = ui_tex_;
    render2 = nullptr;  // To be initialized when we actually need it
    render2_tex = nullptr;
    // set_world_zoom() replaces this with the requested world-only filter.
    // The initial nearest engine keeps boot/UI presentation byte-identical.
    world_engine_ = engine;
}

Screen::~Screen()
{
	destroy_render2();
	destroy_gameplay_ui_overlay();
	if (world_tex_ != ui_tex_)
		SDL_DestroyTexture(world_tex_);
	if (world_surf_ != ui_surf_)
		SDL_DestroySurface(world_surf_);
	SDL_DestroyTexture(ui_tex_);
	SDL_DestroySurface(ui_surf_);

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
}

int Screen::canvas_w() const
{
	if (active_ == CanvasTarget::UI)
		return kUiCanvasW;
	if (active_ == CanvasTarget::GameplayUI && gameplay_ui_frame_active_ &&
	    gameplay_ui_surf_ != nullptr && gameplay_ui_tex_ != nullptr)
	{
		return gameplay_ui_w();
	}
	return world_w_;
}

int Screen::canvas_h() const
{
	if (active_ == CanvasTarget::UI)
		return kUiCanvasH;
	if (active_ == CanvasTarget::GameplayUI && gameplay_ui_frame_active_ &&
	    gameplay_ui_surf_ != nullptr && gameplay_ui_tex_ != nullptr)
	{
		return gameplay_ui_h();
	}
	return world_h_;
}

void Screen::set_active_canvas(CanvasTarget target)
{
	const bool use_gameplay_ui =
		target == CanvasTarget::GameplayUI && gameplay_ui_frame_active_ &&
		gameplay_ui_surf_ != nullptr && gameplay_ui_tex_ != nullptr;
	const int target_w = target == CanvasTarget::UI
		? kUiCanvasW : (use_gameplay_ui ? gameplay_ui_w() : world_w_);
	const int target_h = target == CanvasTarget::UI
		? kUiCanvasH : (use_gameplay_ui ? gameplay_ui_h() : world_h_);
	// GameplayUI is a render-only target. HUD/effect scopes may switch into and
	// out of it several times per frame, so leave the cached gameplay pointer in
	// World coordinates instead of repeatedly quantizing it through the much
	// smaller fixed overlay. Real interactive UI transitions still remap.
	const bool remap_pointer = target != CanvasTarget::GameplayUI &&
		active_ != CanvasTarget::GameplayUI &&
		og::runtime::current_session != nullptr &&
		og::runtime::current_session->myscreen_ != nullptr &&
		(canvas_w() != target_w || canvas_h() != target_h);
	std::pair<float, float> pointer_window{};
	if (remap_pointer)
	{
		pointer_window = active_canvas_to_window(
			static_cast<float>(mouse_state.x),
			static_cast<float>(mouse_state.y));
	}
	active_ = target;
	if (active_ == CanvasTarget::UI)
	{
		render = ui_surf_;
		render_tex = ui_tex_;
	}
	else if (active_ == CanvasTarget::GameplayUI &&
	         gameplay_ui_frame_active_ && gameplay_ui_surf_ != nullptr &&
	         gameplay_ui_tex_ != nullptr)
	{
		render = gameplay_ui_surf_;
		render_tex = gameplay_ui_tex_;
	}
	else
	{
		render = world_surf_;
		render_tex = world_tex_;
	}
	if (remap_pointer)
	{
		const auto [x, y] = window_to_active_canvas(
			pointer_window.first, pointer_window.second);
		mouse_state.x = static_cast<float>(static_cast<int>(x));
		mouse_state.y = static_cast<float>(static_cast<int>(y));
	}
}

void Screen::begin_gameplay_frame()
{
	gameplay_ui_frame_active_ = false;
	gameplay_ui_capture_valid_ = false;
	const bool smart_smoothing =
		world_scale_.mode == og::WorldScaleMode::Sai ||
		world_scale_.mode == og::WorldScaleMode::Eagle;
	const bool independent_gameplay_ui =
		world_w_ != gameplay_ui_w() || world_h_ != gameplay_ui_h();
	const bool overlay_needed = independent_gameplay_ui || smart_smoothing;
	const bool smart_resource_ready =
		!smart_smoothing || ensure_render2_for_source(world_w_, world_h_);
	const bool overlay_ready =
		!overlay_needed || ensure_gameplay_ui_overlay();
	if (overlay_needed && overlay_ready)
	{
		SDL_FillSurfaceRect(gameplay_ui_surf_, nullptr, 0x00000000u);
		gameplay_ui_frame_active_ = true;
	}
	// Smart smoothing must never process HUD pixels. If either its scratch or
	// the independent overlay is unavailable, keep the complete fallback frame
	// nearest. Failed-size latches avoid an allocation storm.
	smart_present_suppressed_ =
		smart_smoothing && (!smart_resource_ready || !overlay_ready);
	// Refresh aliases if a caller happened to begin while GameplayUI was the
	// remembered target. Normal gameplay begins and ends on World.
	set_active_canvas(active_);
}

void Screen::discard_gameplay_ui_frame()
{
	gameplay_ui_frame_active_ = false;
	gameplay_ui_capture_valid_ = false;
	set_active_canvas(active_);
}

void Screen::prepare_ui_canvas_from_world()
{
	// The visible zoomed/filtered frame consists of scenery plus the nearest
	// gameplay-UI overlay. Seed modals from that complete frame so HUD, radar,
	// messages and pane chrome do not disappear behind the dialog.
	// When the last World present completed through SAI/Eagle, render2 is the
	// scenery the player actually saw. Downsampling the raw logical World here
	// would make the entire backdrop snap back to nearest as a dialog opened.
	SDL_Surface* scenery = world_surf_;
	if (last_world_present_used_render2_ && render2 != nullptr &&
	    render2->w == world_w_ * 2 && render2->h == world_h_ * 2)
	{
		scenery = render2;
	}
	SDL_Surface* composed = compose_gameplay_ui_for_capture(scenery);
	SDL_Surface* source = composed != nullptr ? composed : scenery;
	if (source == ui_surf_)
		return;

	const og::CanvasViewport crop = og::crop_canvas_to_aspect(
		source->w, source->h, ui_surf_->w, ui_surf_->h);
	const SDL_Rect source_rect{crop.x, crop.y, crop.w, crop.h};
	if (!SDL_BlitSurfaceScaled(source, &source_rect, ui_surf_, nullptr,
	                           SDL_SCALEMODE_NEAREST))
	{
		LogError("prepare_ui_canvas_from_world failed: {}\n", SDL_GetError());
		SDL_FillSurfaceRect(ui_surf_, nullptr, 0x000000);
	}
	SDL_DestroySurface(composed);
}

bool Screen::set_world_canvas_size(int w, int h)
{
	if (w <= 0 || h <= 0)
		return false;
	if (w == world_w_ && h == world_h_)
		return true;

	const bool remap_pointer = og::runtime::current_session != nullptr &&
		og::runtime::current_session->myscreen_ != nullptr &&
		active_ != CanvasTarget::UI;
	std::pair<float, float> pointer_window{};
	if (remap_pointer)
	{
		pointer_window = active_canvas_to_window(
			static_cast<float>(mouse_state.x),
			static_cast<float>(mouse_state.y));
	}

	SDL_Surface* next_surface = ui_surf_;
	SDL_Texture* next_texture = ui_tex_;

	if (w == kUiCanvasW && h == kUiCanvasH)
	{
		// Shared 320x200 dims: re-use the UI pair (the byte-identity mode).
	}
	else
	{
		// SDL2 rejected any surface whose byte size overflowed int before
		// allocating; SDL3's guard is size_t-wide, so a hostile canvas size
		// would reach the real allocator (hundreds of GB, an abort under
		// ASan) before failing. Keep the SDL2-era 2 GiB bound here.
		const int max_texture_dimension = renderer_max_texture_dimension();
		const bool texture_size_ok = max_texture_dimension <= 0 ||
			(w <= max_texture_dimension && h <= max_texture_dimension);
		if (!texture_size_ok)
		{
			LogError("set_world_canvas_size({}x{}) exceeds renderer texture "
			         "limit {}\n", w, h, max_texture_dimension);
			return false;
		}
		const bool size_ok = static_cast<Sint64>(w) * h <=
			std::min<Sint64>(og::kWorldCanvasAbsolutePixelBudget,
			                  static_cast<Sint64>(SDL_MAX_SINT32) / 4);
		#ifdef TESTING
		const bool injected_failure = fail_next_world_canvas_allocation_;
		fail_next_world_canvas_allocation_ = false;
		#else
		constexpr bool injected_failure = false;
		#endif
		next_surface = size_ok && !injected_failure
			? SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888)
			: nullptr;
		next_texture = next_surface != nullptr
			? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
			                    SDL_TEXTUREACCESS_STREAMING, w, h)
			: nullptr;
		if (next_surface == nullptr || next_texture == nullptr)
		{
			LogError("set_world_canvas_size({}x{}) allocation failed: {}\n", w, h, SDL_GetError());
			SDL_DestroyTexture(next_texture);
			SDL_DestroySurface(next_surface);
			return false;
		}

		SDL_SetTextureBlendMode(next_texture, SDL_BLENDMODE_NONE);
		// Integer-factor presents must stay unsmoothed even if a global
		// render-scale-quality hint asks for linear filtering. Only the split
		// world texture is touched; the shared UI texture keeps its own setup.
		SDL_SetTextureScaleMode(next_texture, SDL_SCALEMODE_NEAREST);
	}

	// Commit only after the complete replacement pair exists. In particular,
	// a failed texture allocation cannot discard a valid split world canvas or
	// leave render/render_tex pointing at destroyed storage.
	SDL_Surface* previous_surface = world_surf_;
	SDL_Texture* previous_texture = world_tex_;
	destroy_render2();
	destroy_gameplay_ui_overlay();
	world_surf_ = next_surface;
	world_tex_ = next_texture;
	world_w_ = w;
	world_h_ = h;
	// Refresh the active-target aliases.
	set_active_canvas(active_);
	if (remap_pointer)
	{
		const auto [x, y] = window_to_active_canvas(
			pointer_window.first, pointer_window.second);
		mouse_state.x = static_cast<float>(static_cast<int>(x));
		mouse_state.y = static_cast<float>(static_cast<int>(y));
	}
	if (previous_texture != ui_tex_)
		SDL_DestroyTexture(previous_texture);
	if (previous_surface != ui_surf_)
		SDL_DestroySurface(previous_surface);
	return true;
}

int Screen::renderer_max_texture_dimension() const
{
	const SDL_PropertiesID properties =
		renderer != nullptr ? SDL_GetRendererProperties(renderer) : 0;
	const Sint64 maximum = properties != 0
		? SDL_GetNumberProperty(properties,
		                        SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0)
		: 0;
	return maximum > 0 && maximum <= std::numeric_limits<int>::max()
		? static_cast<int>(maximum) : 0;
}

int Screen::minimum_world_zoom_steps() const
{
	return og::minimum_zoom_steps_for_window(
		zoom_window_w_, zoom_window_h_, renderer_max_texture_dimension());
}

og::WorldCanvasDims Screen::effective_zoom_canvas_dims(int zoom_steps) const
{
	og::WorldCanvasDims dims = og::compute_zoom_canvas_dims(
		zoom_window_w_, zoom_window_h_, zoom_steps);
	if (std::clamp(zoom_steps, 1, og::kZoomStepsMax) ==
	    og::kZoomStepsMax)
	{
		dims = og::constrain_world_canvas_dims(
			dims, renderer_max_texture_dimension());
	}
	return dims;
}

bool Screen::world_smoothing_supported() const
{
	if (world_w_ <= 0 || world_h_ <= 0 ||
	    world_w_ > std::numeric_limits<int>::max() / 2 ||
	    world_h_ > std::numeric_limits<int>::max() / 2)
	{
		return false;
	}
	const int target_w = world_w_ * 2;
	const int target_h = world_h_ * 2;
	const int max_texture_dimension = renderer_max_texture_dimension();
	return static_cast<Sint64>(target_w) * target_h <=
			kSmartScaleScratchPixelBudget &&
		(max_texture_dimension <= 0 ||
		 (target_w <= max_texture_dimension &&
		  target_h <= max_texture_dimension));
}

void Screen::set_world_zoom(int zoom_steps, og::WorldScaleMode smoothing,
                            int window_w, int window_h)
{
	if (window_w > 0 && window_h > 0)
	{
		zoom_window_w_ = window_w;
		zoom_window_h_ = window_h;
	}
	const int minimum_steps = minimum_world_zoom_steps();
	const int requested_zoom_steps = std::max(
		std::clamp(zoom_steps, 1, og::kZoomStepsMax), minimum_steps);
	const og::WorldCanvasDims dims =
		effective_zoom_canvas_dims(requested_zoom_steps);
	const og::WorldCanvasDims gameplay_ui_dims =
		og::compute_gameplay_ui_canvas_dims(
			effective_zoom_canvas_dims(og::kZoomStepsMax).w,
			effective_zoom_canvas_dims(og::kZoomStepsMax).h);
	// Preserve the shared legacy pair when the aspect-expanded result is
	// exactly classic-sized. Other aspects own a split canvas so the world can
	// reveal extra space instead of stretching master's default geometry.
	og::WorldScaleSetting requested_setting;
	if (dims.w == kUiCanvasW && dims.h == kUiCanvasH &&
	    smoothing == og::WorldScaleMode::Integer)
		requested_setting.mode = og::WorldScaleMode::Legacy;
	else
		requested_setting.mode = smoothing;

	RenderEngine requested_engine = Engine;
	switch (requested_setting.mode)
	{
		case og::WorldScaleMode::Integer:
			requested_engine = RenderEngine::NoZoom; // GPU nearest stretch
			break;
		case og::WorldScaleMode::Sai:
			requested_engine = RenderEngine::SAI;
			break;
		case og::WorldScaleMode::Eagle:
			requested_engine = RenderEngine::Eagle;
			break;
		case og::WorldScaleMode::Legacy:
		default:
			break;
	}

	if (!world_pinned_classic_)
	{
		if (!set_world_canvas_size(dims.w, dims.h))
		{
			LogWarn("Unable to apply world zoom {}; keeping {}x{} canvas and "
			        "its current zoom/smoothing state.\n",
			        requested_zoom_steps, world_w_, world_h_);
			return;
		}
	}

	if (gameplay_ui_w_ != gameplay_ui_dims.w ||
	    gameplay_ui_h_ != gameplay_ui_dims.h)
	{
		destroy_gameplay_ui_overlay();
		gameplay_ui_w_ = gameplay_ui_dims.w;
		gameplay_ui_h_ = gameplay_ui_dims.h;
		set_active_canvas(active_);
	}

	// The resource and public setting state advance only after the canvas
	// replacement succeeds. That keeps world_w/world_h, zoom, engine and live
	// render aliases describing the same effective configuration on failure.
	const int previous_zoom_steps = zoom_steps_;
	const og::WorldScaleMode previous_mode = world_scale_.mode;
	zoom_steps_ = requested_zoom_steps;
	world_scale_ = requested_setting;
	world_engine_ = requested_engine;
	if (requested_setting.mode != og::WorldScaleMode::Sai &&
	    requested_setting.mode != og::WorldScaleMode::Eagle)
	{
		// Turning smoothing off must release the scratch immediately. At 0.1
		// zoom the old pair could otherwise retain hundreds of MB indefinitely.
		destroy_render2();
	}
	if (previous_zoom_steps != zoom_steps_ ||
	    previous_mode != requested_setting.mode)
	{
		// Allow an explicit setting change to retry a prior SDL allocation and
		// report a budget/texture-limit fallback for the new state once. This
		// includes smart -> nearest: the fixed HUD allocation is still required
		// whenever zoom separates it from World.
		render2_failed_w_ = 0;
		render2_failed_h_ = 0;
		gameplay_ui_failed_w_ = 0;
		gameplay_ui_failed_h_ = 0;
		smart_scale_fallback_reported_ = false;
	}
	if (requested_setting.mode == og::WorldScaleMode::Sai ||
	    requested_setting.mode == og::WorldScaleMode::Eagle)
	{
		// The Screen now boots with the unsmoothed engine, so the constructor
		// no longer initializes the scaler's 32-bit interpolation masks.
		// Smoothing can be enabled live from DISPLAY; initialize at that seam.
		Init_2xSaI();
	}
	publish_canvas_diagnostics(
		zoom_steps_, world_w_, world_h_, static_cast<int>(world_scale_.mode),
		false, smart_present_suppressed_, gameplay_ui_w(), gameplay_ui_h());
}


void Screen::set_world_canvas_pinned_classic(bool pinned)
{
	bool resized = false;
	if (pinned)
	{
		resized = set_world_canvas_size(kUiCanvasW, kUiCanvasH);
	}
	else
	{
		// Restore the aspect-relative zoom canvas the pin displaced.
		const og::WorldCanvasDims dims =
			effective_zoom_canvas_dims(zoom_steps_);
		resized = set_world_canvas_size(dims.w, dims.h);
	}
	if (resized)
		world_pinned_classic_ = pinned;
	else
	{
		if (!pinned)
		{
			// A failed restore must not strand the renderer in a permanent pin:
			// leave the working classic canvas in place, but allow the next config
			// application or resize to retry the requested world allocation.
			world_pinned_classic_ = false;
		}
		LogWarn("Unable to {} the classic world-canvas pin; keeping the "
		        "current canvas state.\n", pinned ? "apply" : "release");
	}
}

// Set from the browser 'webglcontextrestored' notification (an ASYNCIFY
// re-entrant callback context) and consumed at the top of Screen::swap(),
// the single production present site.
static std::atomic<bool> g_render_backend_recreate_pending{false};

void request_render_backend_recreate()
{
	g_render_backend_recreate_pending.store(true, std::memory_order_release);
}

bool render_backend_recreate_pending()
{
	return g_render_backend_recreate_pending.load(std::memory_order_acquire);
}

void Screen::destroy_render2()
{
	SDL_DestroyTexture(render2_tex);
	SDL_DestroySurface(render2);
	render2 = nullptr;
	render2_tex = nullptr;
	render2_failed_w_ = 0;
	render2_failed_h_ = 0;
	smart_scale_fallback_reported_ = false;
	// A completed smart frame may have left both its filtered scenery and HUD
	// capture marked as the last visible pair. An engine-only setting change can
	// destroy the scenery scratch without resizing either canvas; invalidate the
	// paired HUD too so screenshots/modals cannot replay it over a newer frame.
	last_world_present_used_render2_ = false;
	gameplay_ui_capture_valid_ = false;
}

void Screen::destroy_gameplay_ui_overlay()
{
	gameplay_ui_frame_active_ = false;
	gameplay_ui_capture_valid_ = false;
	// Never leave the public draw aliases pointing into storage we are about
	// to release. GameplayUI degrades to the World target when inactive.
	if (active_ == CanvasTarget::GameplayUI)
	{
		render = world_surf_;
		render_tex = world_tex_;
	}
	SDL_DestroyTexture(gameplay_ui_tex_);
	SDL_DestroySurface(gameplay_ui_surf_);
	gameplay_ui_tex_ = nullptr;
	gameplay_ui_surf_ = nullptr;
	gameplay_ui_failed_w_ = 0;
	gameplay_ui_failed_h_ = 0;
	smart_present_suppressed_ = false;
	last_world_present_used_render2_ = false;
}

bool Screen::recreate_render_backend()
{
	// Free every GPU-side object owned by this Screen. The render2 scratch
	// and gameplay-UI overlay are lazy caches that self-recreate on demand.
	destroy_render2();
	destroy_gameplay_ui_overlay();
	if (world_tex_ != ui_tex_)
		SDL_DestroyTexture(world_tex_);
	world_tex_ = nullptr;
	SDL_DestroyTexture(ui_tex_);
	ui_tex_ = nullptr;
	SDL_DestroyRenderer(renderer);
	renderer = nullptr;

	// Constructor parity for the renderer (see Screen::Screen).
	renderer = SDL_CreateRenderer(window, nullptr);
	if (renderer == nullptr)
	{
		LogError("recreate_render_backend: SDL_CreateRenderer failed: {}\n",
		         SDL_GetError());
		set_active_canvas(active_);
		return false;
	}
	#ifndef TESTING
	SDL_SetRenderVSync(renderer, 1);
	#endif

	// Constructor parity for the fixed 320x200 UI texture: opaque present,
	// chunky-crisp nearest upscale (see the notes in Screen::Screen).
	ui_tex_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
	                            SDL_TEXTUREACCESS_STREAMING,
	                            kUiCanvasW, kUiCanvasH);
	if (ui_tex_ == nullptr)
	{
		LogError("recreate_render_backend: UI texture creation failed: {}\n",
		         SDL_GetError());
		set_active_canvas(active_);
		return false;
	}
	SDL_SetTextureBlendMode(ui_tex_, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(ui_tex_, SDL_SCALEMODE_NEAREST);

	if (world_surf_ == ui_surf_)
	{
		// Shared classic pair: keep the byte-identity aliasing.
		world_tex_ = ui_tex_;
	}
	else
	{
		// Split world canvas: rebuild its texture at the live dimensions
		// with set_world_canvas_size() parity. The world surface (and its
		// pixels) survives — only the GPU object was lost.
		world_tex_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		                               SDL_TEXTUREACCESS_STREAMING,
		                               world_w_, world_h_);
		if (world_tex_ == nullptr)
		{
			// Degrade to the shared classic pair rather than presenting
			// through a dead handle. The caller's apply_world_scale_from_cfg
			// pass retries the split allocation transactionally.
			LogError("recreate_render_backend: world texture creation failed "
			         "for {}x{}: {}\n", world_w_, world_h_, SDL_GetError());
			SDL_DestroySurface(world_surf_);
			world_surf_ = ui_surf_;
			world_tex_ = ui_tex_;
			world_w_ = kUiCanvasW;
			world_h_ = kUiCanvasH;
		}
		else
		{
			SDL_SetTextureBlendMode(world_tex_, SDL_BLENDMODE_NONE);
			SDL_SetTextureScaleMode(world_tex_, SDL_SCALEMODE_NEAREST);
		}
	}

	// Repair the public render/render_tex aliases for the active target.
	set_active_canvas(active_);
	return true;
}

bool Screen::ensure_gameplay_ui_overlay()
{
	const int target_w = gameplay_ui_w();
	const int target_h = gameplay_ui_h();
	if (gameplay_ui_surf_ != nullptr && gameplay_ui_tex_ != nullptr &&
	    gameplay_ui_surf_->w == target_w &&
	    gameplay_ui_surf_->h == target_h &&
	    gameplay_ui_tex_->w == target_w &&
	    gameplay_ui_tex_->h == target_h)
	{
		return true;
	}
	if (gameplay_ui_failed_w_ == target_w &&
	    gameplay_ui_failed_h_ == target_h)
	{
		return false;
	}
#ifdef TESTING
	if (fail_next_gameplay_ui_allocation_)
	{
		fail_next_gameplay_ui_allocation_ = false;
		gameplay_ui_failed_w_ = target_w;
		gameplay_ui_failed_h_ = target_h;
		return false;
	}
#endif

	SDL_Surface* next_surface =
		SDL_CreateSurface(target_w, target_h, SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture* next_texture = next_surface != nullptr
		? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		                    SDL_TEXTUREACCESS_STREAMING,
		                    target_w, target_h)
		: nullptr;
	if (next_surface == nullptr || next_texture == nullptr)
	{
		LogError("Gameplay UI overlay allocation failed for {}x{}: {}\n",
		         target_w, target_h, SDL_GetError());
		SDL_DestroyTexture(next_texture);
		SDL_DestroySurface(next_surface);
		gameplay_ui_failed_w_ = target_w;
		gameplay_ui_failed_h_ = target_h;
		return false;
	}

	// Surface blits (screenshots) and renderer compositing both use straight
	// alpha. Nearest sampling keeps the HUD/radar/message pixels crisp.
	SDL_SetSurfaceBlendMode(next_surface, SDL_BLENDMODE_BLEND);
	SDL_SetTextureBlendMode(next_texture, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(next_texture, SDL_SCALEMODE_NEAREST);
	SDL_DestroyTexture(gameplay_ui_tex_);
	SDL_DestroySurface(gameplay_ui_surf_);
	gameplay_ui_surf_ = next_surface;
	gameplay_ui_tex_ = next_texture;
	gameplay_ui_failed_w_ = 0;
	gameplay_ui_failed_h_ = 0;
	return true;
}

void Screen::fail_next_gameplay_ui_allocation_for_testing()
{
	destroy_gameplay_ui_overlay();
	fail_next_gameplay_ui_allocation_ = true;
}

SDL_Surface* Screen::compose_gameplay_ui_for_capture(SDL_Surface* scenery) const
{
	if (scenery == nullptr ||
	    (!gameplay_ui_frame_active_ && !gameplay_ui_capture_valid_) ||
	    gameplay_ui_surf_ == nullptr)
	{
		return nullptr;
	}
	SDL_Surface* composed = SDL_CreateSurface(
		scenery->w, scenery->h, SDL_PIXELFORMAT_XRGB8888);
	if (composed == nullptr ||
	    !SDL_BlitSurface(scenery, nullptr, composed, nullptr) ||
	    !SDL_BlitSurfaceScaled(gameplay_ui_surf_, nullptr, composed, nullptr,
	                           SDL_SCALEMODE_NEAREST))
	{
		LogError("Failed to composite gameplay capture: {}\n", SDL_GetError());
		SDL_DestroySurface(composed);
		return nullptr;
	}
	return composed;
}


bool Screen::ensure_render2_for_source(int source_w, int source_h)
{
	// Validate before multiplying: Screen's public canvas-size hook is also
	// exercised by hostile/error-path tests, and signed overflow here would be
	// undefined before SDL got a chance to reject the request.
	if (source_w <= 0 || source_h <= 0 ||
	    source_w > std::numeric_limits<int>::max() / 2 ||
	    source_h > std::numeric_limits<int>::max() / 2)
	{
		if (!smart_scale_fallback_reported_)
		{
			LogWarn("Smart smoothing skipped for invalid {}x{} source canvas; "
			        "presenting nearest.\n", source_w, source_h);
			smart_scale_fallback_reported_ = true;
		}
		return false;
	}

	const int need_w = source_w * 2;
	const int need_h = source_h * 2;
	const Sint64 need_pixels =
		static_cast<Sint64>(need_w) * static_cast<Sint64>(need_h);
	const SDL_PropertiesID renderer_properties =
		renderer != nullptr ? SDL_GetRendererProperties(renderer) : 0;
	const Sint64 renderer_max_texture = renderer_properties != 0
		? SDL_GetNumberProperty(renderer_properties,
		                        SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0)
		: 0;
	const bool exceeds_renderer_limit =
		renderer_max_texture > 0 &&
		(static_cast<Sint64>(need_w) > renderer_max_texture ||
		 static_cast<Sint64>(need_h) > renderer_max_texture);
	const bool exceeds_work_budget =
		need_pixels > kSmartScaleScratchPixelBudget;
	if (exceeds_renderer_limit || exceeds_work_budget)
	{
		if (!smart_scale_fallback_reported_)
		{
			LogWarn("Smart smoothing skipped for {}x{} canvas (2x target "
			        "{}x{}, {} pixels; budget {}, renderer max {}); "
			        "presenting nearest while preserving the selected setting.\n",
			        source_w, source_h, need_w, need_h, need_pixels,
			        kSmartScaleScratchPixelBudget, renderer_max_texture);
			smart_scale_fallback_reported_ = true;
		}
		return false;
	}

	if (render2 != nullptr && render2_tex != nullptr &&
	    render2->w == need_w && render2->h == need_h &&
	    render2_tex->w == need_w && render2_tex->h == need_h)
	{
		return true;
	}
	if (render2_failed_w_ == need_w && render2_failed_h_ == need_h)
		return false;

	// Build the replacement pair completely before touching the live pair.
	// A one-sided SDL failure therefore cannot strand a null texture, leak on
	// every retry, or discard a valid differently-sized scratch.
	SDL_Surface* next_surface =
		SDL_CreateSurface(need_w, need_h, SDL_PIXELFORMAT_XRGB8888);
	SDL_Texture* next_texture = next_surface != nullptr
		? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		                    SDL_TEXTUREACCESS_STREAMING, need_w, need_h)
		: nullptr;
	if (next_surface == nullptr || next_texture == nullptr)
	{
		LogError("Smart-scaler scratch allocation failed for {}x{}: {}\n",
		         need_w, need_h, SDL_GetError());
		SDL_DestroyTexture(next_texture);
		SDL_DestroySurface(next_surface);
		render2_failed_w_ = need_w;
		render2_failed_h_ = need_h;
		return false;
	}

	SDL_SetTextureBlendMode(next_texture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(next_texture, SDL_SCALEMODE_NEAREST);
	SDL_DestroyTexture(render2_tex);
	SDL_DestroySurface(render2);
	render2 = next_surface;
	render2_tex = next_texture;
	render2_failed_w_ = 0;
	render2_failed_h_ = 0;
	smart_scale_fallback_reported_ = false;
	return true;
}


void Screen::SaveBMP(SDL_Surface* screen, char* filename)
{
	SDL_SaveBMP(screen, filename);
}

void Screen::clear()
{
	SDL_FillSurfaceRect(render, nullptr, 0x000000);
}

void Screen::clear(int x, int y, int w, int h)
{
    SDL_Rect r = {x, y, w, h};
	SDL_FillSurfaceRect(render, &r, 0x000000);
}

void Screen::swap(int x, int y, int w, int h)
{
    // When suppress_present is set, rendering still goes to the surface
    // (E_Screen->render) but we skip presentation to the physical display.
    // This is used by multi-session demos that composite multiple session
    // surfaces before presenting once.
    if (suppress_present) return;

	// A lost rendering device (web WebGL context loss) is repaired here, at
	// the single production present site — never inside the browser event
	// callback that observed it: under ASYNCIFY those callbacks can run
	// while the C stack is suspended inside a blocking menu loop.
	if (g_render_backend_recreate_pending.load(std::memory_order_acquire))
	{
		if (!recreate_render_backend())
		{
			// The device may still be gone (Safari can restore late, or the
			// browser may fire restore before the context is usable). Keep
			// the request pending and retry on the next present; the page
			// watchdog covers the never-restored case with a reload.
			return;
		}
		g_render_backend_recreate_pending.store(false,
		                                        std::memory_order_release);
		if (E_Screen.get() == this)
		{
			// Re-derive the zoom/split world canvas state for the new
			// renderer (this also retries a degraded shared-pair fallback)
			// and force a full repaint. Per-present uploads restore the
			// pixels either way.
			apply_world_scale_from_cfg();
			if (og::runtime::current_session != nullptr &&
			    og::runtime::current_session->myscreen_ != nullptr)
			{
				og::runtime::current_session->myscreen_->redrawme = 1;
			}
		}
		TRACE("video", "render backend recreated");
	}

    // A World present consumes exactly one prepared gameplay overlay. Keep
    // its pixels capture-valid afterwards for an immediate screenshot, but
    // never replay it into a later fade/transition/partial World swap.
	const bool composite_gameplay_ui =
		active_ == CanvasTarget::World && gameplay_ui_frame_active_ &&
		gameplay_ui_surf_ != nullptr && gameplay_ui_tex_ != nullptr;
	if (active_ == CanvasTarget::World && !composite_gameplay_ui)
		gameplay_ui_capture_valid_ = false;
	if (active_ == CanvasTarget::World)
		last_world_present_used_render2_ = false;

    SDL_Surface* source_surface = render;
    SDL_Texture* dest_texture = render_tex;

    // Present engine per canvas: a non-Legacy WORLD canvas uses the filter
    // selected by graphics/smoothing. The UI canvas and shared 320x200
    // world path use `Engine`, which display creation pins to nearest. The
    // retained slot still supports legacy direct callers and tests.
    const RenderEngine present_engine =
        (active_ == CanvasTarget::World &&
	     !smart_present_suppressed_ &&
         world_scale_.mode != og::WorldScaleMode::Legacy)
            ? world_engine_
            : Engine;

	switch(present_engine) {
		case RenderEngine::SAI:
                // Doubling scratch sized 2x the ACTIVE canvas (640x400 for a
                // 320x200 source canvas).
				if(!ensure_render2_for_source(render->w, render->h))
                    break; // allocation failed: present the unscaled canvas
				if (!SDL_LockSurface(render2))
				{
					LogError("Unable to lock SAI scratch surface: {}\n", SDL_GetError());
					break; // present the unscaled canvas
				}
                Super2xSaI_ex2(
                        reinterpret_cast<unsigned char*>(render->pixels), x, y, w, h, render->pitch, render->h,
                        reinterpret_cast<unsigned char*>(render2->pixels), 2*x, 2*y, render2->pitch);
                SDL_UnlockSurface( render2 );

				source_surface = render2;
				dest_texture = render2_tex;
				last_world_present_used_render2_ = true;
            break;
		case RenderEngine::Eagle:
				if(!ensure_render2_for_source(render->w, render->h))
                    break; // allocation failed: present the unscaled canvas
				if (!SDL_LockSurface(render2))
				{
					LogError("Unable to lock Eagle scratch surface: {}\n", SDL_GetError());
					break; // present the unscaled canvas
				}
                Scale_SuperEagle(reinterpret_cast<unsigned char*>(render->pixels), x, y, w, h, render->pitch, render->h,
                                 reinterpret_cast<unsigned char*>(render2->pixels), 2*x, 2*y, render2->pitch);
                SDL_UnlockSurface( render2 );

				source_surface = render2;
				dest_texture = render2_tex;
				last_world_present_used_render2_ = true;
			break;
        default:
            break;
	}

	// Present through the same aspect-fitted logical rectangle used by pointer
	// mapping. An aspect-matched world canvas fills it; the fixed 320x200 UI is
	// pillar/letterboxed instead of being distorted on widescreen displays.

	SDL_UpdateTexture(dest_texture, nullptr, source_surface->pixels, source_surface->pitch);

	// SDL3 SDL_RenderTexture takes float rects; keep the SDL2 int-rect
	// truncation so the presented viewport stays pixel-identical.
	const og::CanvasViewport logical_dest = active_canvas_viewport();
	const SDL_FRect dest = renderer_output_rect(
		renderer,
		static_cast<float>(logical_dest.x),
		static_cast<float>(logical_dest.y),
		static_cast<float>(logical_dest.w),
		static_cast<float>(logical_dest.h));

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderTexture(renderer, dest_texture, nullptr, &dest);

	// Gameplay chrome is deliberately absent from source_surface/render2:
	// SAI/Eagle process only map/tiles/sprites/effects. Composite the
	// transparent HUD/radar/message layer afterwards with nearest sampling.
	// Aspect-fit it independently from World: fractional zoom dimensions round
	// to scaler-safe integers and can otherwise squeeze the fixed HUD through a
	// slightly different destination. This remains one frame transaction and
	// one SDL_RenderPresent call.
	if (composite_gameplay_ui)
	{
		const og::CanvasViewport logical_gameplay_ui_dest =
			gameplay_ui_canvas_viewport();
		const SDL_FRect gameplay_ui_dest = renderer_output_rect(
			renderer,
			static_cast<float>(logical_gameplay_ui_dest.x),
			static_cast<float>(logical_gameplay_ui_dest.y),
			static_cast<float>(logical_gameplay_ui_dest.w),
			static_cast<float>(logical_gameplay_ui_dest.h));
		if (!SDL_UpdateTexture(gameplay_ui_tex_, nullptr,
		                       gameplay_ui_surf_->pixels,
		                       gameplay_ui_surf_->pitch))
		{
			LogError("Unable to upload gameplay UI overlay: {}\n", SDL_GetError());
		}
		else if (!SDL_RenderTexture(renderer, gameplay_ui_tex_, nullptr,
		                            &gameplay_ui_dest))
		{
			LogError("Unable to composite gameplay UI overlay: {}\n", SDL_GetError());
		}
    }
    SDL_RenderPresent(renderer);
	last_presented_ = active_ == CanvasTarget::GameplayUI
		? CanvasTarget::World
		: active_;
	if (active_ == CanvasTarget::World)
	{
		publish_canvas_diagnostics(
			zoom_steps_, world_w_, world_h_, static_cast<int>(world_scale_.mode),
			last_world_present_used_render2_, smart_present_suppressed_,
			gameplay_ui_w(), gameplay_ui_h());
		gameplay_ui_capture_valid_ = composite_gameplay_ui;
		// One completed World transaction, announced. This is the instant
		// the filtered scenery scratch and the crisp gameplay-UI capture
		// both become readable, and it is what a test driving a private
		// loop (the level editor) has to wait for. Publishing it is what
		// lets such a test wait for the EVENT instead of guessing a
		// duration — see stop_editor_after_render in
		// tests/test_canvas_scale.cpp. Compiled out of production builds.
		TRACE("world_present", "smart=%d ui=%d",
		      last_world_present_used_render2_ ? 1 : 0,
		      composite_gameplay_ui ? 1 : 0);
		gameplay_ui_frame_active_ = false;
		set_active_canvas(active_);
	}
}

void Screen::clear_window()
{
    SDL_Surface* source_surface = render;
    SDL_Texture* dest_texture = render_tex;
    
    SDL_FillSurfaceRect(source_surface, nullptr, 0x000000);
    
    SDL_UpdateTexture(dest_texture, nullptr, source_surface->pixels, source_surface->pitch);
    
	const SDL_FRect dest = renderer_output_rect(
		renderer, 0.0f, 0.0f,
		float(int(og::runtime::current_session->window_w_)),
		float(int(og::runtime::current_session->window_h_)));

    SDL_RenderTexture(renderer, dest_texture, nullptr, &dest);
}

#undef GET_RESULT
#undef INTERPOLATE
#undef Q_INTERPOLATE
