#ifndef __GUY_H
#define __GUY_H

// Definition of GUY class

#include "base.h"

class guy               // for the picker, loading team info
{
  public:
         guy ();
         guy (char whatfamily);
         ~guy();
         long query_heart_value(); // how much are we worth?

         char name[12];
         char family;  // our family
         short strength;
         short dexterity;
         short constitution;
         short intelligence;
         short level;
         short armor;
         unsigned long exp;
         short kills;       // version 3+
         long level_kills;  // version 3+
         long total_damage; // version 4+
         long total_hits;   // version 4+
         long total_shots;  // version 4+
         short teamnum;     // version 5+
         guy  *next;

};

#endif

