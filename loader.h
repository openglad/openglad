#ifndef __LOADER_H
#define __LOADER_H

// Definition of LOADER class

#include "base.h"

class loader
{
  public:
         loader();
         walker  *create_walker(char order, char family, screen  *screenp);
         pixieN *create_pixieN(char order, char family);
         walker *set_walker(walker *ob, char order, char family);
         unsigned char  **graphics;
         signed char  ***animations;
         long  *stepsizes;
         long  *lineofsight;
  protected:
//         char  *hitpoints;
         short  hitpoints[200]; // hack for now
         char  *act_types;
         long  *damage;
         signed char  *fire_frequency;
};

#endif
