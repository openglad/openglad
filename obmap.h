#ifndef __OBMAP_H
#define __OBMAP_H

// Definition of OBMAP class

#include "base.h"

class obmap
{
  public:
         obmap(short maxx, short maxy);
         ~obmap();
         short query_list(walker  *ob, short x, short y);
         short remove(walker  *ob);  // This goes in walker's destructor
         short add(walker  *ob, short x, short y);  // This goes in walker's constructor
         short move(walker  *ob, short x, short y);  // This goes in walker's setxy
         oblink * obmap_get_list(short x, short y); //Returns the list at x,y for fnf
         short totalobs;
         short obmapres;
  private:
         short hash(short y);
         oblink  *list[200][200];
};

#endif
