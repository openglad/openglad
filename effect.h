#ifndef __EFFECT_H
#define __EFFECT_H

// Definition of EFFECT class

#include "base.h"
#include "obmap.h"
#include "screen.h"
#include "stats.h"
#include "walker.h"
#include "guy.h"

class effect : public walker
{
  public:
        effect(unsigned char  *data, screen  *myscreen);
         ~effect();
         short          act();
         short          animate();
         short          death(); // called on destruction
         char         query_order() { return ORDER_FX;}
};

#endif

