#ifndef __RADAR_H
#define __RADAR_H

// Definition of RADAR class

#include "base.h"

class radar
{
  public:
         radar(viewscreen * myview, screen * myscreen, short whatnum);
         ~radar();
         short draw ();
         short sizex, sizey;
         short xpos,ypos;
         short xloc, yloc;        // where on the screen to display
         short on_screen();
         short on_screen(short whatx, short whaty, short hor, short ver);
         short refresh();
         void update(); // slow function to update radar/map info
         void start();

         screen * screenp;
         viewscreen * viewscreenp;
         unsigned char  *bmp,  *oldbmp;
  protected:
         short mynum; // what is my viewscreen-related number?
//         char  *buffer;
         short size;
         short xview;
         short yview;
};

#endif

