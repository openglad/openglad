#ifndef __TEXT_H
#define __TEXT_H

// Definition of TEXT class

#include "base.h"

class text
{
  public:
         friend class vbutton;
         text(screen * myscreen);
         text(screen * myscreen, char * filename);
         short query_width(char *string); // returns width, in pixels
         short write_xy(short x, short y, char  *string);
         short write_xy(short x, short y, char  *string, unsigned char color);
         short write_xy(short x, short y, char  *string, short to_buffer);
         short write_xy(short x, short y, char  *string, unsigned char color, short to_buffer);
         short write_xy(short x, short y, char  *string, viewscreen *whereto);
         short write_xy(short x, short y, char  *string, unsigned char color, viewscreen *whereto);
         short write_y(short y, char  *string);
         short write_y(short y, char  *string, unsigned char color);
         short write_y(short y, char  *string, short to_buffer);
         short write_y(short y, char  *string, unsigned char color, short to_buffer);
         short write_y(short y, char  *string, viewscreen *whereto);
         short write_y(short y, char  *string, unsigned char color, viewscreen *whereto);
         short write_char_xy(short x, short y, char letter);
         short write_char_xy(short x, short y, char letter, unsigned char color);
         short write_char_xy(short x, short y, char letter, short to_buffer);
         short write_char_xy(short x, short y, char letter, unsigned char color, short to_buffer);
         short write_char_xy(short x, short y, char letter, viewscreen *whereto);
         short write_char_xy(short x, short y, char letter, unsigned char color, viewscreen *whereto);
         char *input_string(short x, short y, short maxlength, char *begin);
         char *input_string(short x, short y, short maxlength, char *begin, 
           unsigned char forecolor, unsigned char backcolor);
         unsigned char convert_to_ascii(unsigned char scancode);
         ~text();
         screen * screenp;

  protected:
         unsigned char  *letters;
         short sizex;
         short sizey;
};

#endif

