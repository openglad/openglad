#ifndef __VIDEO_H
#define __VIDEO_H

// The definition of the VIDEO class

#include "base.h"

class video
{
  public:
         video();
         ~video();
         void clearscreen();
         void clearbuffer();

         unsigned char * getbuffer();
         void putblack(long startx, long starty, long xsize, long ysize);
         void fastbox(long startx, long starty, long xsize, long ysize, unsigned char color);
         void fastbox(long startx, long starty, long xsize, long ysize, unsigned char color, unsigned char flag);
         void point(long x, long y, unsigned char color);
         //buffers: PORT: added below prototype
	 void pointb(long x, long y, unsigned char color);
	 void hor_line(long x, long y, long length, unsigned char color);
         void ver_line(long x, long y, long length, unsigned char color);
         void hor_line(long x, long y, long length, unsigned char color, long tobuffer);
         void ver_line(long x, long y, long length, unsigned char color, long tobuffer);
         void do_cycle(long curmode, long maxmode);
         void putdata(long startx, long starty, long xsize, long ysize, 
                      unsigned char  *sourcedata);
         void putdata(long startx, long starty, long xsize, long ysize, 
                      unsigned char  *sourcedata, unsigned char color);

         void putbuffer(long tilestartx, long tilestarty,
                        long tilewidth, long tileheight,
                        long portstartx, long portstarty,
                        long portendx, long portendy,
                        unsigned char * sourceptr);
	void putbuffer(long tilestartx, long tilestarty,
			long tilewidth, long tileheight,
			long portstartx, long portstarty,
			long portendx, long portendy,
			SDL_Surface *sourceptr);
	 void walkputbuffer(long walkerstartx, long walkerstarty,
                            long walkerwidth, long walkerheight,
                            long portstartx, long portstarty,
                            long portendx, long portendy,
                            unsigned char  *sourceptr, unsigned char teamcolor);
         void walkputbuffer(long walkerstartx, long walkerstarty,
                            long walkerwidth, long walkerheight,
                            long portstartx, long portstarty,
                            long portendx, long portendy,
                            unsigned char  *sourceptr, unsigned char teamcolor,
                            unsigned char mode, long invisibility,
                            unsigned char outline, unsigned char shifttype);
         void buffer_to_screen(long viewstartx,long viewstarty,
                               long viewwidth, long viewheight);

         void draw_box(long x1, long y1, long x2, long y2, unsigned char color, long filled);
         void draw_box(long x1, long y1, long x2, long y2, unsigned char color, long filled, long tobuffer);
         void draw_button(long x1, long y1, long x2, long y2, long border);
         void draw_button(long x1, long y1, long x2, long y2, long border, long tobuffer);
         long draw_dialog(long x1, long y1, long x2, long y2, char *header);
         void draw_text_bar(long x1, long y1, long x2, long y2);

	void swap(void);

         void clear_ints();
         void restore_ints();

	char ourpalette[768]; // our standard glad palette
	char redpalette[768]; // for 'faded' backgrounds during menus
	char bluepalette[768]; // for special effects like time-freeze
	char dospalette[768]; // store the dos palette so we can restore it later

         unsigned char videobuffer[64000]; //our new unified video buffer
         short cyclemode; //color cycling on or off


	//buffers: screen vars
	SDL_Surface *screen;
	int screen_width,screen_height;
	int pdouble, mult;
};

#endif
