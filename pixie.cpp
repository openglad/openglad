#include "graph.h"
#include <fstream.h>
#include <stdlib.h>
// Z's script: #include <dos.h>

// ************************************************************
//  Pixie -- Base graphic object. It holds pixel by pixel data
//  of what should appear on screen. When told to, it handles
//  its own placing and movement on the background screen
//  buffer, though it requires info from the screen object.
// ************************************************************
/*
  pixie()                                                       - Does nothing (DON'T USE)
  pixie(char,short,short,screen)    - initializes the pixie data (pix = char)
  short setxy(short, short)                   - set x,y coords (without drawing)
  short move(short,short)                             - move pixie x,y
  short draw(short,short)                             - put pixie x,y
  short draw()
  short on_screen()
*/


// Pixie -- this initializes the graphics data for the pixie,
// as well as its graphics x and y size.  In addition, it informs
// the pixie of the screen object it is linked to.
pixie::pixie(unsigned char *data, short x, short y, screen  *myscreen)
{
  screenp = myscreen;
  bmp = data;
  sizex = x;
  sizey = y;
  size = (unsigned short) (sizex*sizey);
//  oldbmp = (unsigned char *)new char[size];
}

// Destruct the pixie and its variables
pixie::~pixie()
{
//  delete oldbmp;
}

// Set the pixie's x and y positon without drawing.
short pixie::setxy(short x, short y)
{
  xpos = x;
  ypos = y;
  return 1;
}

// Allows the pixie to be moved using pixel coord data
short pixie::move(short x, short y)
{
         return setxy((short)(xpos+x),(short)(ypos+y));
}

// Allows the pixie to be placed using pixel coord data
short pixie::draw(short x, short y, viewscreen  * view_buf)
{
  setxy(x, y);
  return draw(view_buf);
}

short pixie::draw(viewscreen * view_buf)
{
  long xscreen, yscreen;

//  if (!on_screen(view_buf))
//         return 0;
//we actually don't need to waste time on the above since the clipper
//will handle it
         
  xscreen = (long) (xpos - view_buf->topx + view_buf->xloc);
  yscreen = (long) (ypos - view_buf->topy + view_buf->yloc);

  view_buf->screenp->putbuffer(xscreen, yscreen, sizex, sizey,
                              view_buf->xloc, view_buf->yloc,
                              view_buf->endx, view_buf->endy,
                              bmp);

  return 1;
}

// Allows the pixie to be placed using pixel coord data
short pixie::drawMix(short x, short y, viewscreen  * view_buf)
{
  setxy(x, y);
  return drawMix(view_buf);
}

short pixie::drawMix(viewscreen * view_buf)
{
  long xscreen, yscreen;

//  if (!on_screen(view_buf))
//         return 0;
//we actually don't need to waste time on the above since the clipper
//will handle it
         
  xscreen = (long) (xpos - view_buf->topx + view_buf->xloc);
  yscreen = (long) (ypos - view_buf->topy + view_buf->yloc);

  view_buf->screenp->walkputbuffer(xscreen, yscreen, sizex, sizey,
                              view_buf->xloc, view_buf->yloc,
                              view_buf->endx, view_buf->endy,
                              bmp, RED);

  return 1;
}


short pixie::put_screen(short x, short y)
{
  screenp->putdata(x, y, sizex, sizey, bmp);
  return 1;
}

short pixie::on_screen()
{
  short i;
  for (i=0; i < screenp->numviews; i++)
  {
         if (on_screen(screenp->viewob[i]))
                return 1;
  }
  return 0;
}

short pixie::on_screen(viewscreen  *viewp)
{
  short topx = viewp->topx;
  short topy = viewp->topy;
  short xview = viewp->xview;
  short yview = viewp->yview;

  // Return 0 if off viewscreen.
  // These measurements are grid coords, not pixels.
  if ( (xpos+sizex) < topx) return 0;     // we are to the left of the view
  else if ( xpos > (topx + xview) ) return 0;  // we are to the right of the view
  else if ( (ypos+sizey) < topy) return 0;  // we are above the view
  else if ( ypos > (topy + yview) ) return 0; //we are below the view
  else 
    return 1;
}
