#include "graph.h"

text::text(screen * myscreen)
{
  letters = (unsigned char *) read_pixie_file(TEXT_1);
  sizex = letters[1];
  sizey = letters[2];
  letters = letters+3;
  screenp = myscreen;
}

text::text(screen * myscreen, char * filename)
{
  if (!filename || strlen(filename) < 2) filename = "text.pix";
  letters = (unsigned char *) read_pixie_file(filename);
  sizex = letters[1];
  sizey = letters[2];
  letters = letters+3;
  screenp = myscreen;
}

text::~text()
{
  letters -= 3;
  delete letters;
  letters = NULL;
}
short text::query_width(char *string) // returns width, in pixels
{
  unsigned short i=0;
  short over = 0;
  
  if (sizex < 9) // small, monospaced font
    return (sizex+1)*strlen(string);

  while (string[i]) 
  {
    if (string[i] >= 65 && string[i] <= 93) // uppercase
      over += sizex;
    else
      over += sizex-1;
    i++;
  }

  return over;
}

short text::write_xy(short x, short y, char *string, unsigned char color)
{
  unsigned short i = 0;
  while(string[i])
  {
         write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color);
         i++;
  }
 return 1;
}

short text::write_xy(short x, short y, char *string)
{
  unsigned short i = 0;
  while(string[i])
  {
         write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR);
         i++;
  }
 return 1;
}

short text::write_xy(short x, short y, char *string, unsigned char color,
                     short to_buffer)
{
  unsigned short i = 0;
  unsigned short width;
  short over = 0;

  if (sizex < 9) // small, monospaced font
    while(string[i])
    {
      if (!to_buffer)
        write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color);
      else
        write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color, (short) 1);
      i++;
      over += sizex+1;
    }
  else // larger font, help out the lowercase ..
    while(string[i])
    {
      write_char_xy((short) (x+over), y, string[i], (unsigned char) color, (short) 1);
      if (string[i] >=65 && string[i] <= 92) // uppercase
        over += sizex;
      else // lowercase, other things
        over += sizex-1;
      i++;
    }

  if (to_buffer)
  {
    width = (unsigned short) ((sizex+1)*strlen(string));
    width -= width%4;
    width +=4;
    //screenp->buffer_to_screen(x, y, width, sizey);
  }
      
 return over;
}

short text::write_xy(short x, short y, char *string, short to_buffer)
{
  unsigned short i = 0;
  unsigned short width;
  while(string[i])
  {
        if (!to_buffer)
         write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR);
        else
         write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR, (short) 1);
        i++;
  }
  if (to_buffer)
  {
    width = (unsigned short) ((sizex+1)*strlen(string));
    width -= width%4;
    width +=4;
    //screenp->buffer_to_screen(x, y, width, sizey);
  }
      
 return 1;
}

short text::write_xy(short x, short y, char *string, unsigned char color,
                     viewscreen *whereto)
{
  unsigned short i = 0;
  while(string[i])
  {
         if (!whereto)
                write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color);
         else
                write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) color, whereto);
         i++;
  }
 return 1;
}

short text::write_xy(short x, short y, char *string, viewscreen *whereto)
{
  unsigned short i = 0;
  while(string[i])
  {
         if (!whereto)
                write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR);
         else
                write_char_xy((short) (x+i*(sizex+1)), y, string[i], (unsigned char) DEFAULT_TEXT_COLOR, whereto);
         i++;
  }
 return 1;
}

short text::write_y(short y, char *string, unsigned char color)
{
  unsigned short len = 0;
  unsigned short xstart;
  len = (unsigned short) strlen(string);
  xstart = (unsigned short) ((320 - len * (sizex+1))/2);
  return write_xy(xstart, y, string, (unsigned char) color);
}

short text::write_y(short y, char *string)
{
  unsigned short len = 0;
  unsigned short xstart;
  len = (unsigned short) strlen(string);
  xstart = (unsigned short) ((320 - len * (sizex+1))/2);
  return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR);
}

short text::write_y(short y, char *string, unsigned char color,
                    short to_buffer)
{
  unsigned short len = 0;
  unsigned short xstart;
  len = (unsigned short) strlen(string);
  xstart = (unsigned short) ((320 - len * (sizex+1))/2);
  if (!to_buffer)
        return write_xy(xstart, y, string, (unsigned char) color);
  else
        return write_xy(xstart, y, string, (unsigned char) color, to_buffer);
}

short text::write_y(short y, char *string, short to_buffer)
{
  unsigned short len = 0;
  unsigned short xstart;
  len = (unsigned short) strlen(string);
  xstart = (unsigned short) ((320 - len * (sizex+1))/2);
  if (!to_buffer)
        return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR);
  else
        return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR, to_buffer);
}

short text::write_y(short y, char *string, unsigned char color,
                    viewscreen *whereto)
{
  unsigned short len = 0;
  unsigned short xstart;
  len = (unsigned short) strlen(string);
  xstart = (unsigned short) ((320 - len * (sizex+1))/2);
  if (!whereto)
         return write_xy(xstart, y, string, (unsigned char) color);
  else
         return write_xy(xstart, y, string, (unsigned char) color, whereto);
}

short text::write_y(short y, char *string, viewscreen *whereto)
{
  unsigned short len = 0;
  unsigned short xstart;
  len = (unsigned short) strlen(string);
  xstart = (unsigned short) ((320 - len * (sizex+1))/2);
  if (!whereto)
         return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR);
  else
         return write_xy(xstart, y, string, (unsigned char) DEFAULT_TEXT_COLOR, whereto);
}

// This version writes to the buffer and then writes the
// buffer to the screen .. (double buffered to eliminate flashing)
short text::write_char_xy(short x, short y, char letter, unsigned char color,
                          short to_buffer)
{
  if (!to_buffer)
    return write_char_xy(x, y, letter, (unsigned char) color);

  screenp->walkputbuffer(x, y, sizex, sizey, 0, 0, 319,199, (unsigned char*) &letters[letter * sizex * sizey], (unsigned char) color);
  //screenp->buffer_to_screen(x, y, sizex + 4 - (sizex%4), sizey + 4 - (sizey%4) );
  return 1;
}

short text::write_char_xy(short x, short y, char letter, short to_buffer)
{
  if (!to_buffer)
    return write_char_xy(x, y, letter, (unsigned char) DEFAULT_TEXT_COLOR);

  screenp->walkputbuffer(x, y, sizex, sizey, 0, 0, 319,199, (unsigned char*) &letters[letter * sizex * sizey], (unsigned char) DEFAULT_TEXT_COLOR);
  //screenp->buffer_to_screen(x, y, sizex + 4 - (sizex%4), sizey + 4 - (sizey%4) );
  return 1;
}
  
short text::write_char_xy(short x, short y, char letter, unsigned char color)
{
  screenp->putdata(x, y, sizex, sizey, (unsigned char *) &letters[letter *sizex*sizey], (unsigned char) color);
  return 1;
}

short text::write_char_xy(short x, short y, char letter)
{
  screenp->putdata(x, y, sizex, sizey, (unsigned char *) &letters[letter *sizex*sizey]);
  return 1;
}

short text::write_char_xy(short x, short y, char letter, unsigned char color,
                          viewscreen *whereto)
{
  if (!whereto)
         screenp->putdata(x, y, sizex, sizey, (unsigned char *)&letters[letter *sizex*sizey], (unsigned char) color);
  else
         screenp->walkputbuffer(x+whereto->xloc, y+whereto->yloc, sizex, sizey,
                                whereto->xloc,whereto->yloc,whereto->endx, whereto->endy,
                                (unsigned char *)&letters[letter *sizex*sizey], (unsigned char) color);
//         screenp->buffer_to_screen(x+whereto->xloc, y+whereto->yloc,
//           (sizex + 4 - (sizex%4)), (sizey + 4 - (sizey%4)) );
  return 1;
}

short text::write_char_xy(short x, short y, char letter, viewscreen *whereto)
{
  if (!whereto)
         screenp->putdata(x, y, sizex, sizey, (unsigned char *)&letters[letter *sizex*sizey]);
  else
         screenp->walkputbuffer(x+whereto->xloc, y+whereto->yloc, sizex, sizey,
                                whereto->xloc,whereto->yloc,whereto->endx, whereto->endy,
                                (unsigned char *)&letters[letter *sizex*sizey], (unsigned char) DEFAULT_TEXT_COLOR);
//         screenp->buffer_to_screen(x+whereto->xloc, y+whereto->yloc,
//           (sizex + 4 - (sizex%4)), (sizey + 4 - (sizey%4)) );
  return 1;
}

// This version passes DARK_BLUE and a grey color as defaults ..
char * text::input_string(short x, short y, short maxlength, char *begin)
{
  return input_string(x, y, maxlength, begin, DARK_BLUE, 13);
}

// Input_string reads a string from the keyboard, displaying it
// at screen position x,y.  The maximum length of the string
// is maxlength, and any string in 'begin' will automatically be
// entered at the start.  Fore- and backcolor are used for the
// text foreground and background color
char * text::input_string(short x, short y, short maxlength, char *begin,
  unsigned char forecolor, unsigned char backcolor)
{
  short current_length, i;
  short string_done = 0;
  static char editstring[100], firststring[100];
  //char *somekeyboard = grab_keyboard();
  short dumbcount; // used for delays, etc.
  unsigned char tempchar;
  short has_typed = 0; // hasn't typed yet

  for (i=0; i < 100; i++)
    editstring[i] = 0; // clear the string ...

  if (begin)
  {
    strcpy(editstring, begin);
  }
  strcpy(firststring, begin); // default case
  current_length = (short) strlen(editstring);
  screenp->draw_box(x, y, x+maxlength*(sizex+1), y+sizey, backcolor, 1, 1);
  if (strlen(begin))
    screenp->draw_box(x, y, x+query_width(begin), y+sizey-2, forecolor, 1, 1);
  write_xy(x, y, editstring, WHITE, 1);
  screenp->buffer_to_screen(0, 0, 320, 200);

  clear_keyboard();
  clear_key_press_event();

  while ( !string_done )
  {
    // Wait for a key to be pressed ..
    while (!query_key_press_event())
      dumbcount++;
    tempchar = (unsigned char) query_key();
    clear_key_press_event();
    if (tempchar == SCAN_ENTER)
      string_done = 1;
    else if (tempchar == SCAN_ESC)
    {
      strcpy(editstring, firststring);
      string_done = 1;
    }
    else if (tempchar == SCAN_BACKSPACE && current_length)
      editstring[current_length-1] = 0;
    else if ( (convert_to_ascii(tempchar) != 255) &&
              (current_length < maxlength) )
    {
      if (!has_typed) // first char, so replace text
      {
        current_length = 0;
        for (i=0; i < 100; i++)
          editstring[i] = 0; // clear the string ...
        screenp->draw_box(x, y, x+maxlength*(sizex+1),
                          y+sizey+1, backcolor, 1, 1);
      }
      editstring[current_length] = convert_to_ascii(tempchar);
    }
    has_typed = 1;
    current_length = (short) strlen(editstring);
    screenp->draw_box(x, y, x+maxlength*(sizex+1), y+sizey+1, backcolor, 1, 1);
    write_xy(x, y, editstring, forecolor, 1);
    screenp->buffer_to_screen(0, 0, 320, 200);
  }
  
  clear_keyboard();
  return editstring;

}

// Convert from scancode to ascii, ie, SCAN_A to 'A'
unsigned char text::convert_to_ascii(unsigned char scancode)
{
  switch (scancode)
  {
    case SCAN_A: return 'A';
    case SCAN_B: return 'B';
    case SCAN_C: return 'C';
    case SCAN_D: return 'D';
    case SCAN_E: return 'E';
    case SCAN_F: return 'F';
    case SCAN_G: return 'G';
    case SCAN_H: return 'H';
    case SCAN_I: return 'I';
    case SCAN_J: return 'J';
    case SCAN_K: return 'K';
    case SCAN_L: return 'L';
    case SCAN_M: return 'M';
    case SCAN_N: return 'N';
    case SCAN_O: return 'O';
    case SCAN_P: return 'P';
    case SCAN_Q: return 'Q';
    case SCAN_R: return 'R';
    case SCAN_S: return 'S';
    case SCAN_T: return 'T';
    case SCAN_U: return 'U';
    case SCAN_V: return 'V';
    case SCAN_W: return 'W';
    case SCAN_X: return 'X';
    case SCAN_Y: return 'Y';
    case SCAN_Z: return 'Z';

    case SCAN_1: return '1';
    case SCAN_2: return '2';
    case SCAN_3: return '3';
    case SCAN_4: return '4';
    case SCAN_5: return '5';
    case SCAN_6: return '6';
    case SCAN_7: return '7';
    case SCAN_8: return '8';
    case SCAN_9: return '9';
    case SCAN_0: return '0';

    case SCAN_SPACE: return 32;
//    case SCAN_BACKSPACE: return 8;
    case SCAN_ENTER: return 13;
    case SCAN_ESC: return 27;
    case SCAN_PERIOD:  return '.';
    case SCAN_COMMA: return ',';
// Zardus: PORT: not defined in scankeys.h:    case SCAN_CLOSE_QUOTE: return ''';
    case SCAN_OPEN_QUOTE: return '`';

    default: return 255;
  }
}

