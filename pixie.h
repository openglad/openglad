#ifndef __PIXIE_H
#define __PIXIE_H

// Definition of PIXIE class

#include "base.h"

class pixie
{
  public:
         pixie(unsigned char *data,short xsize, short ysize,screen  *myscreen);
         pixie(unsigned char *data,short x,short y,screen *myscreen,int doaccel);
	 ~pixie();
         short setxy(short x, short y);
         virtual short move (short x, short y);
         short draw (viewscreen  *view_buf);
         short draw (short x, short y, viewscreen  *view_buf);
         short drawMix (viewscreen *view_buf);
         short drawMix (short x, short y, viewscreen *view_buf);                
         short put_screen(short x, short y);
	 void init_sdl_surface(void);
	 void set_accel(int a);
         short sizex, sizey;
         short xpos,ypos;
	 //buffers: is SDL_Surface acceleration on/off, 1/0
	 int accel;
         short on_screen();                                                                // on ANY viewscreen?
         short on_screen(viewscreen  *viewp);  // on a specific viewscreen?
         screen  *screenp;
  protected:
         unsigned short size;
         unsigned char  *bmp,  *oldbmp;
	 //buffers: same data as bmp but in a convient SDL_Surface
	 SDL_Surface *bmp_surface;
};

#endif
