//
// Smooth.h
//
#ifndef SMOOTH_H
#define SMOOTH_H

#include "\wfiles\glad\graph.h"

// Used for deciding cases
#define TO_UP 1
#define TO_RIGHT 2
#define TO_DOWN 4
#define TO_LEFT 8
#define TO_AROUND 15

// These are the 'genre' defines ..
#define TYPE_GRASS 1
#define TYPE_WATER 2
#define TYPE_TREES 3
#define TYPE_DIRT  4
#define TYPE_COBBLE 5
#define TYPE_GRASS_DARK 6
#define TYPE_DIRT_DARK 7
#define TYPE_WALL 8
#define TYPE_CARPET 9
#define TYPE_GRASS_LIGHT 10
#define TYPE_UNKNOWN 50


class smoother
{
  public:
   smoother();
   ~smoother();

   void set_target(screen  *target);     // set our target grid to smooth ..
   long query_x_y(long x, long y);       // return target type, ie PIX_GRASS1
   long query_genre_x_y(long x, long y); // returns target genre, ie TYPE_GRASS
   long surrounds(long x, long y, long whatgenre); // returns 0-15 of 4 surroundings
   long smooth(long x, long y);          // smooth at x, y; returns changed or not
   long smooth();                        // smooths entire target grid
   void set_x_y(long x, long y, long whatvalue);  // sets grid location to whatvalue
   
   unsigned char  *mygrid; // our grid to change
   long maxx, maxy;   // dimensions of our grid ..
   char  *buffer; // start of the data in the target grid
};

#endif
