#include <string.h>
#include <stdio.h>
#include "graph.h"

#define HELPTEXT_LEFT 40 
#define HELPTEXT_TOP  40
#define DISPLAY_LINES 15
#define TEXT_DOWN(x) ((x*7)+HELPTEXT_TOP)

short end_of_file;                        // global flag ..
char helptext[HELP_WIDTH][MAX_LINES];
void get_input_events(bool);


// This function reads one text line from file infile,
// stopping at length (length), or when encountering an
// end-of-line character ..
char* read_one_line(FILE *infile, short length)
{
  char newline[HELP_WIDTH]; // = new char(length);
  char temp;
  short readvalue;
  short i;

  // Make sure this line is blank ..
  for (i=0; i < HELP_WIDTH; i++)
         newline[i] = 0;

  for (i=0; i < length; i++)
  {
        readvalue = (short) fread(&temp, 1, 1, infile);
        if (readvalue != 1)
        {
          end_of_file = 1;
          return &newline[0];
        }
        if (temp == '\n' || temp == '\r')
          return &newline[0];
        newline[i] = temp;
  }

  return &newline[0];
}

// Note: this code has been redone to work in 'SDLKlines,'
//       so that the text scrolls by pixels rather than lines.
short read_scenario(screen *myscreen)
{
  long screenlines = myscreen->scentextlines * 8;
  long  numlines, j;
  long linesdown;
  long changed;
  long templines;
  long text_delay = 1; // bigger = slower
  long key_presses = 0;
  char *mykeyboard = query_keyboard();
  text *mytext = new text(myscreen, TEXT_1);
  long start_time, now_time;
  long bottomrow = (screenlines - ((DISPLAY_LINES-1)*8) ); 
  
  clear_keyboard();
  linesdown = 0;
  changed = 1;
  start_time = query_timer();
  numlines = screenlines;
        
  // Do the loop until person hits escape
  // Make sure we're not pressing keys ..
  mykeyboard[SDLK_DOWN] = mykeyboard[SDLK_UP] = 0;
  mykeyboard[SDLK_PAGEDOWN] = mykeyboard[SDLK_PAGEUP] = 0;
  while (!mykeyboard[SDLK_ESCAPE])
  {
	  get_input_events(POLL);
    if (mykeyboard[SDLK_DOWN])    // scrolling down
    {
      now_time = query_timer();

      key_presses =  (now_time - start_time) % text_delay;
      if (!key_presses && (linesdown < bottomrow) )
      {
        linesdown++;
        changed = 1;
      }
    } // end of SDLK_DOWN

    if (mykeyboard[SDLK_PAGEDOWN])    // scrolling one page down
    {
      now_time = query_timer();
      key_presses = (now_time - start_time) % (10*text_delay);
      if (!key_presses && (linesdown < bottomrow) )
      {
        templines = linesdown + (DISPLAY_LINES * 7);
        if (templines > bottomrow)
          templines = bottomrow;
        if (linesdown != templines) // we actually moved down
        {
          linesdown = templines;
          changed = 1;
        }
      }
    } // end of PAGE DOWN

    if (mykeyboard[SDLK_UP])      // scrolling up
    {
      now_time = query_timer();
      key_presses = (now_time - start_time) % text_delay;
      if (!key_presses && linesdown) 
      {
        linesdown--;
        changed = 1;
      }
    } // end of SDLK_UP

    if (mykeyboard[SDLK_PAGEUP])    // scrolling one page up
    {
      now_time = query_timer();
      key_presses = (now_time - start_time) % (10*text_delay);
      if (!key_presses && linesdown)
      {
        linesdown -= (DISPLAY_LINES * 7);
        if (linesdown < 0)
          linesdown = 0;
        changed = 1;
      }
    }  // end of PAGE UP

    if (changed)  // did we scroll, etc.?
    {
      templines = linesdown/8; // which TEXT line are we at?
      myscreen->draw_button(HELPTEXT_LEFT-4, HELPTEXT_TOP-4-8,
                 HELPTEXT_LEFT+200, HELPTEXT_TOP+107, 3, 1);
      for (j=0; j < DISPLAY_LINES; j++)
        if (strlen(myscreen->scentext[j+templines]))
          mytext->write_xy(HELPTEXT_LEFT+2, (short) (TEXT_DOWN(j)-linesdown%8),
                             myscreen->scentext[j+templines], (unsigned char) DARK_BLUE, 1 ); // to buffer!
      // Draw a bounding box (top and bottom edges) ..
      myscreen->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP-8,
        HELPTEXT_LEFT+200-4, HELPTEXT_TOP-2);
      myscreen->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP+97,
        HELPTEXT_LEFT+200-4, HELPTEXT_TOP+103);
      mytext->write_xy(HELPTEXT_LEFT+40,
        HELPTEXT_TOP-7, "SCENARIO INFORMATION", (unsigned char) RED, 1);
      mytext->write_xy(HELPTEXT_LEFT+30,
        HELPTEXT_TOP+98, "PRESS 'ESC' TO CONTINUE", (unsigned char) RED, 1);
      myscreen->buffer_to_screen(0, 0, 320, 200);
      changed = 0;
    } // end of changed drawing loop

  }  // loop until ESC is pressed 

  while (mykeyboard[SDLK_ESCAPE])  // wait for key release
	  get_input_events(WAIT);
  mykeyboard[SDLK_ESCAPE] = 0;
  delete mytext;
  mytext = NULL;
  return (short) numlines;
}

short read_help(char *somefile,screen * myscreen)
{
        FILE *infile;
        long screenlines;
        long  numlines, j;
        long linesdown;
        long changed;
        long templines;
        long text_delay = 1; // bigger = slower
        long key_presses = 0;
        char *mykeyboard = query_keyboard();
        static text *mytext = new text(myscreen, TEXT_1);
        long start_time, now_time;
        long bottomrow;

        if ((infile = fopen(somefile, "rt"))
                 == NULL)
        {
                fprintf(stderr, "Cannot open help file.\n");
                //delete mytext;
                return 1;
        }

        clear_keyboard();
        end_of_file = 0;
        linesdown = 0;
        changed = 1;
        start_time = query_timer();

        /* seek to the beginning of the file */
        fseek(infile, SEEK_SET, 0);

        // Fill the helptext array with data ..
        numlines = (long) (fill_help_array(helptext, infile));
        screenlines = numlines*8;
        numlines = screenlines;
        bottomrow = (screenlines - ((DISPLAY_LINES-1)*8) ); 

        // Close the help file
        fclose(infile);

  // Do the loop until person hits escape
  // Make sure we're not pressing keys ..
  mykeyboard[SDLK_DOWN] = mykeyboard[SDLK_UP] = 0;
  mykeyboard[SDLK_PAGEDOWN] = mykeyboard[SDLK_PAGEUP] = 0;
  while (!mykeyboard[SDLK_ESCAPE])
  {
	  get_input_events(POLL);
    if (mykeyboard[SDLK_DOWN])    // scrolling down
    {
      now_time = query_timer();

      key_presses =  (now_time - start_time) % text_delay;
      if (!key_presses && (linesdown < bottomrow) )
      {
        linesdown++;
        changed = 1;
      }
    } // end of SDLK_DOWN

    if (mykeyboard[SDLK_PAGEDOWN])    // scrolling one page down
    {
      now_time = query_timer();
      key_presses = (now_time - start_time) % (10*text_delay);
      if (!key_presses && (linesdown < bottomrow) )
      {
        templines = linesdown + (DISPLAY_LINES * 7);
        if (templines > bottomrow)
          templines = bottomrow;
        if (linesdown != templines) // we actually moved down
        {
          linesdown = templines;
          changed = 1;
        }
      }
    } // end of PAGE DOWN

    if (mykeyboard[SDLK_UP])      // scrolling up
    {
      now_time = query_timer();
      key_presses = (now_time - start_time) % text_delay;
      if (!key_presses && linesdown) 
      {
        linesdown--;
        changed = 1;
      }
    } // end of SDLK_UP

    if (mykeyboard[SDLK_PAGEUP])    // scrolling one page up
    {
      now_time = query_timer();
      key_presses = (now_time - start_time) % (10*text_delay);
      if (!key_presses && linesdown)
      {
        linesdown -= (DISPLAY_LINES * 7);
        if (linesdown < 0)
          linesdown = 0;
        changed = 1;
      }
    }  // end of PAGE UP

    if (changed)  // did we scroll, etc.?
    {
      templines = linesdown/8; // which TEXT line are we at?
      myscreen->draw_button(HELPTEXT_LEFT-4, HELPTEXT_TOP-4-8,
                 HELPTEXT_LEFT+240, HELPTEXT_TOP+107, 3, 1);
      for (j=0; j < DISPLAY_LINES; j++)
        if (strlen(helptext[j+templines]))
          mytext->write_xy(HELPTEXT_LEFT+2, (short) (TEXT_DOWN(j)-linesdown%8),
                             helptext[j+templines], (unsigned char) DARK_BLUE, 1 ); // to buffer!
      // Draw a bounding box (top and bottom edges) ..
      myscreen->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP-8,
        HELPTEXT_LEFT+240-4, HELPTEXT_TOP-2);
      myscreen->draw_text_bar(HELPTEXT_LEFT, HELPTEXT_TOP+97,
        HELPTEXT_LEFT+240-4, HELPTEXT_TOP+103);
      mytext->write_xy(HELPTEXT_LEFT+90,
        HELPTEXT_TOP-7, "GLADIATOR", (unsigned char) RED, 1);
      mytext->write_xy(HELPTEXT_LEFT+52,
        HELPTEXT_TOP+98, "PRESS 'ESC' TO CONTINUE", (unsigned char) RED, 1);
      myscreen->buffer_to_screen(0, 0, 320, 200);
      changed = 0;
    } // end of changed drawing loop

  }  // loop until ESC is pressed 

        

        while (mykeyboard[SDLK_ESCAPE])  // wait for key release
		get_input_events(WAIT);
        //delete mytext; 
        return (short) (numlines);
}


// This function fills the array with the help file
// text ..
// It returns the # of lines successfully filled ..
short fill_help_array(char somearray[HELP_WIDTH][MAX_LINES], FILE *infile)
//short fill_help_array(char somearray[80][80], FILE *infile)
{
  short i;

  if (!infile) return 0;

  for (i=0; i < MAX_LINES; i++)
  {
         //somearray[i] = read_one_line(infile, HELP_WIDTH);
         strcpy(somearray[i], read_one_line(infile, HELP_WIDTH) );
         if (end_of_file)
                return i;
  }

  return MAX_LINES;
}



