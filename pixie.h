#ifndef __PIXIE_H
#define __PIXIE_H

// Definition of PIXIE class

#include "base.h"

class pixie
{
  public:
         pixie(unsigned char *data,short xsize, short ysize,screen  *myscreen);
         ~pixie();
         short setxy(short x, short y);
         virtual short move (short x, short y);
         short draw (viewscreen  *view_buf);
         short draw (short x, short y, viewscreen  *view_buf);
         short drawMix (viewscreen *view_buf);
         short drawMix (short x, short y, viewscreen *view_buf);                
         short put_screen(short x, short y);
         short sizex, sizey;
         short xpos,ypos;
         short on_screen();                                                                // on ANY viewscreen?
         short on_screen(viewscreen  *viewp);  // on a specific viewscreen?
         screen  *screenp;
  protected:
         unsigned short size;
         unsigned char  *bmp,  *oldbmp;
};

#endif
