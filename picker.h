// Class statistics,
// for (guess what?) controlling stats, etc ..
//

#ifndef __PICKER_H
#define __PICKER_H

class walker;

class statistics
{
  public:
         statistics();
         ~statistics();
         short  try_command(short, short, short, short);
         void set_command(short, short, short, short);
         void do_command(walker  *owner);

         short hitpoints;
         short max_hitpoints;
         short magicpoints;
         short max_magicpoints;
         short commandcount;      // # times to execute command
         short command;       // command to execute
  private:
         short com1, com2;                // parameters to command


};

#endif
