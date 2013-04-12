#include <SDL.h>
#include "sai2x.h"
//#include "os_depend.h"

#define Log(x) fprintf(stderr,x)

// Private var for SAI2x
static Uint32 colorMask = 0xF7DEF7DE;
static Uint32 lowPixelMask = 0x08210821;
static Uint32 qcolorMask = 0xE79CE79C;
static Uint32 qlowpixelMask = 0x18631863;
static Uint32 redblueMask = 0xF81F;
static Uint32 greenMask = 0x7E0;
static int PixelsPerMask = 2;
static int xsai_depth = 0;

static unsigned char *src_line[4];
static unsigned char *dst_line[2];

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
		Uint32* bP = (Uint32*) srcPtr;
		Uint32* dP = (Uint32*) dstPtr;
	
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
			   	register int r = 0;

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
		Uint32 *bP = (Uint32*) srcPtr;
		Uint32 *dP = (Uint32*) dstPtr;

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
               register int r = 0;

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


	unused=NULL;	//for avoid warning
	//int j;
	unsigned int x, y;
	Uint32 color[16];

	/* Point to the first 3 lines. */
	src_line[0] = src;
	src_line[1] = src;
	src_line[2] = src + src_pitch;
	src_line[3] = src + src_pitch * 2;
	
	dst_line[0] = (unsigned char*) dest;
	dst_line[1] = (unsigned char*) dest + dest_pitch;
	
	x = 0, y = 0;
	Uint32 *lbp;
	lbp = (Uint32*)src_line[0];
	color[0] = *lbp;       color[1] = color[0];   color[2] = color[0];    color[3] = color[0];
	color[4] = color[0];   color[5] = color[0];   color[6] = *(lbp + 1);  color[7] = *(lbp + 2);
	lbp = (Uint32*)src_line[2];
	color[8] = *lbp;     color[9] = color[8];     color[10] = *(lbp + 1); color[11] = *(lbp + 2);
	lbp = (Uint32*)src_line[3];
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
	
			*((Uint32 *) (&dst_line[0][x * 8])) = product1a;
			*((Uint32 *) (&dst_line[0][x * 8 + 4])) = product1b;
			*((Uint32 *) (&dst_line[1][x * 8])) = product2a;
			*((Uint32 *) (&dst_line[1][x * 8 + 4])) = product2b;
			
			/* Move color matrix forward */
			color[0] = color[1]; color[4] = color[5]; color[8] = color[9];   color[12] = color[13];
			color[1] = color[2]; color[5] = color[6]; color[9] = color[10];  color[13] = color[14];
			color[2] = color[3]; color[6] = color[7]; color[10] = color[11]; color[14] = color[15];
			
			if (x < width - 3) {
				x += 3;
				color[3] = *(((Uint32*)src_line[0]) + x);
				color[7] = *(((Uint32*)src_line[1]) + x);
				color[11] = *(((Uint32*)src_line[2]) + x);
				color[15] = *(((Uint32*)src_line[3]) + x);
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
		Uint32 *lbp;
		lbp = (Uint32*)src_line[0];
		color[0] = *lbp; color[1] = color[0]; color[2] = *(lbp + 1); color[3] = *(lbp + 2);
		lbp = (Uint32*)src_line[1];
		color[4] = *lbp; color[5] = color[4]; color[6] = *(lbp + 1); color[7] = *(lbp + 2);
		lbp = (Uint32*)src_line[2];
		color[8] = *lbp; color[9] = color[9]; color[10] = *(lbp + 1); color[11] = *(lbp + 2);
		lbp = (Uint32*)src_line[3];
		color[12] = *lbp; color[13] = color[12]; color[14] = *(lbp + 1); color[15] = *(lbp + 2);
		
		if (y < height - 1) {
			dst_line[0] = (unsigned char*) dest + dest_pitch*(y*2+2);
			dst_line[1] = (unsigned char*) dest + dest_pitch*(y*2+3);
		}
	}
}

void Super2xSaI(SDL_Surface *src, SDL_Surface *dest, int s_x, int s_y, int d_x, int d_y, int w, int h)
{
	int sbpp, dbpp;

	if (!src || !dest)
	{
		Log("Error: In Super2xSaI, src or dest are NULL\n");
		return;
	}

	sbpp = src->format->BitsPerPixel;
	dbpp = dest->format->BitsPerPixel;

	if ((sbpp != xsai_depth) || (sbpp != dbpp))	/* Must be same color depth */
	{
		Log("Error: In Super2xSaI, sbpp or dbpp are not equal to xsai_depth\n");
		return;
	}

	sbpp = src->format->BytesPerPixel;
	dbpp = dest->format->BytesPerPixel;

	if (w < 4 || h < 4)
	{
		/* Image is too small to be 2xSaI'ed. */
		Log("Error: surface to copy is to small, TODO here\n");
		return;
	}	
	
	Super2xSaI_ex(
			(unsigned char*) src->pixels + src->pitch*s_y + s_x*sbpp, 
			src->pitch, 
			NULL, 
			(unsigned char*) dest->pixels + dest->pitch*d_y +d_x*dbpp, 
			dest->pitch, 
			w, h);
	return;
}



/////////////////////////////////
//
Screen::Screen( RenderEngine engine, int fullscreen)
{
	int tx,ty;
	Engine=engine;
	switch(Engine)
	{
	case SAI:
		Init_2xSaI();
		tx=640;
		ty=400;
		break;
	case EAGLE:
		Init_2xSaI();
		tx=640;
		ty=400;
		break;
	case DOUBLE:
		tx=640;
		ty=400;
		break;
	case NoZoom:
	default:
		tx=320;
		ty=200;
		break;
	}
	#ifndef USE_SDL2
	if(!fullscreen)
		render=SDL_SetVideoMode(tx, ty, 32, SDL_SWSURFACE|SDL_DOUBLEBUF);
	else
		render=SDL_SetVideoMode(tx, ty, 32, SDL_SWSURFACE|SDL_DOUBLEBUF|SDL_FULLSCREEN);
    #else
    window = SDL_CreateWindow("Gladiator",
                        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                        tx, ty,
                        SDL_WINDOW_SHOWN);
    if(window == NULL)
        exit(1);
    
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    
    render = SDL_CreateRGBSurface(SDL_SWSURFACE, tx, ty, 32, 0, 0, 0, 0);
    renderer = SDL_CreateRenderer(window, -1, 0);
    
    if(fullscreen)
    {
        SDL_SetWindowFullscreen(window, 1);
    }
    #endif
	if(Engine != DOUBLE)
		screen=SDL_CreateRGBSurface(SDL_SWSURFACE, 320, 200, 32, 0, 0, 0, 0);
	else
		screen=SDL_CreateRGBSurface(SDL_SWSURFACE,640,400,32,0,0,0,0);
	SDL_FillRect( screen, 0, SDL_MapRGB(screen->format, 0x00, 0x00, 0x00) );
}

Screen::~Screen()
{
	if (screen)
		Log("Error while trying to destroy Screen, Quit wasn't called!\n");
}


void Screen::Quit()
{
	if(screen)
		SDL_FreeSurface( screen );
	
	//buffers:if(tempo)
	//buffers:	SDL_FreeSurface( tempo );
	screen=NULL;
	//buffers: tempo=NULL;
}

void Screen::SaveBMP( char *filename )
{
	SDL_SaveBMP( render, filename );
}

void Screen::Render(Sint16 x, Sint16 y, Uint16 w, Uint16 h)
{
	#ifndef USE_SDL2
	SDL_UpdateRect(render, x,y,w,h);
	#else
	SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, render);
	SDL_Rect dest = {0, 0, 0, 0};
    SDL_GetWindowSize(window, &dest.w, &dest.h); // Fill up the whole window
    
	SDL_RenderCopy(renderer, tex, NULL, &dest);
	SDL_DestroyTexture(tex);
	SDL_RenderPresent(renderer);
	#endif
	
}

SDL_Surface *Screen::RenderAndReturn( int x, int y, int w, int h )
{
        switch( Engine )
        {
        case DOUBLE:
        case NoZoom:
                SDL_BlitSurface(screen,NULL,render,NULL);
                break;
        case SAI:
                SDL_LockSurface( render );
                Super2xSaI_ex2(
                        (unsigned char*) screen->pixels, x, y, w, h, screen->pitch, screen->h,
                        (unsigned char*) render->pixels, 2*x, 2*y, render->pitch);
                SDL_UnlockSurface( render );
                break;
        case EAGLE:
                SDL_LockSurface( render );
                Scale_SuperEagle((unsigned char*) screen->pixels, x, y, w, h, screen->pitch, screen->h,(unsigned char*) render->pixels, 2*x, 2*y, render->pitch);
                SDL_UnlockSurface( render );
                break;
        default:
                printf("error, default reached\n");
                //SDL_BlitSurface(screen,NULL,render,NULL);
        }

	return render;
}

void Screen::Swap(int x, int y, int w, int h)
{
	switch(Engine) {
		case DOUBLE:
			Render(x*2,y*2,w*2,h*2);
			break;
		case NoZoom:
			Render(x,y,w,h);
			break;
		case SAI:
		case EAGLE:
			Render(x*2,y*2,w*2,h*2);
			break;
        //default:
			//Render(x,y,w,h);
	}
}

#undef GET_RESULT
#undef INTERPOLATE
#undef Q_INTERPOLATE
