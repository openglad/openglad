#ifndef __PIXIEN_H
#define __PIXIEN_H

// Definition of PIXIEN class

#include "base.h"
#include "pixie.h"

class pixieN : public pixie
{
  public:
         pixieN(unsigned char  *data, screen  *myscreen);
	 pixieN(unsigned char  *data, screen  *myscreen, int doaccel);
         ~pixieN();
         short set_frame(short framenum);
         short query_frame();
         short next_frame();
  protected:
         short frames; // total frames
         short frame; // current frame
         unsigned char * facings;
};

#endif

