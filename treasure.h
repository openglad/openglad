#ifndef __TREASURE_H
#define __TREASURE_H

// Definition of TREASURE class

#include "base.h"
#include "walker.h"

class treasure : public walker
{
  public:
        treasure(unsigned char  *data, screen  *myscreen);
         ~treasure();
         short          act();
         //short                    death(); // called upon destruction
         short          eat_me(walker  * eater);
         walker  * find_teleport_target();
         void         set_direct_frame(short whatframe);
         char         query_order() { return ORDER_TREASURE;}
};

#endif

