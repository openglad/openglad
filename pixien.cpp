#include "graph.h"
#include <fstream.h>
#include <stdlib.h>
// Z's script: #include <dos.h>

// ************************************************************
//  PixieN -- N-Frame pixie
//  It is identical to the PIXIE class except it handles
//  multiple frames and switch frames before a put.
// ************************************************************

pixieN::pixieN(unsigned char  *data, screen  *myscreen):
                                        pixie(data,data[1],data[2], myscreen)
{
  facings = data+3;
  bmp = (unsigned char *)(data+3);
  frames = data[0];
  frame = 0;
}

pixieN::~pixieN()
{
  bmp = NULL;
  facings = NULL;
  frames = NULL;
}

// Changes the frame number and poshorts the BMP data poshorter to
//  the correct frame's data
short pixieN::set_frame(short framenum)
{
  if (framenum < 0 || framenum >= frames)
  {
         //printf("setting frame less than 0.\n");
         return 0;
  }
  bmp = facings+framenum*size;
  frame = framenum;
  return 1;
}

short pixieN::next_frame()
{
  return set_frame(frame++ % frames);
}

short pixieN::query_frame()
{
  return frame;
}
