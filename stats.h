#ifndef __STATS_H
#define __STATS_H

// Definition of STATS class

#include "base.h"

//
// Include file for the stats object
//

// These are for the bit-flags
#define BIT_FLYING     (long int)     1  // fly over water, trees
#define BIT_SWIMMING   (long int)     2  // move over water
#define BIT_ANIMATE    (long int)     4  // animate even when not moving
#define BIT_INVINCIBLE (long int)     8  // can't be harmed
#define BIT_NO_RANGED  (long int)    16  // no ranged attack
#define BIT_IMMORTAL   (long int)    32  // for weapons that don't die when
                                         //   they hit
#define BIT_NO_COLLIDE (long int)    64  // fly through walkers
#define BIT_PHANTOM    (long int)   128  // use phantomputbuffer instead of
                                         //   walkerputbuffer
#define BIT_NAMED      (long int)   256  // has a name (will have outline)
#define BIT_FORESTWALK (long int)   512  // can walk through forests
#define BIT_MAGICAL    (long int)  1024  // generally for magical weapons
#define BIT_FIRE       (long int)  2048  // for any flame weapons
#define BIT_ETHEREAL   (long int)  4096  // fly "through" walls
#define BIT_LAST       (long int)  8192
// Other special effects, etc.
#define FAERIE_FREEZE_TIME    40


// Class statistics,
// for (guess what?) controlling stats, etc ..
//
class statistics
{
  public:
         statistics(walker  *);
         ~statistics();
         short  try_command(short, short, short, short);
         short  try_command(short, short);
         void set_command(short, short);
         void set_command(short, short, short, short);
         void add_command(short, short, short, short);
         void force_command(short, short, short, short);
         void clear_command();
         short do_command();
         void hit_response(walker * who);
         void yell_for_help(walker *foe);  // yell and run away
         short query_bit_flags(long myvalue);
         void set_bit_flags(long someflag, short newvalue); // sets a single flag
         short right_blocked(); // is our right blocked?
         short right_forward_blocked();
         short right_back_blocked();
         short forward_blocked(); // are we blocked in front?
         //short distance_to_foe(); //not in use???????
         short right_walk();      // walk using right-hand rule
         short direct_walk(); // walk in a line toward foe ..
         short walk_to_foe(); // try to walk intelligently towards foe

         char name[12]; // for NPC's, normally ..
         char old_order, old_family;
         long last_distance;
         long current_distance;  // Distances (to foe) are used for AI walking
         long bit_flags;         // holds (currently) 32 bit flags
         short delete_me;
         long hitpoints;
         long max_hitpoints;
         long magicpoints;
         long max_magicpoints;
         unsigned short level;
         short frozen_delay;              // use for paralyzing..
         unsigned short special_cost[NUM_SPECIALS];  // cost of our special ability
         short weapon_cost;                          // cost of our weapon
         long max_heal_delay;
         long current_heal_delay;
         long max_magic_delay;
         long current_magic_delay;
         long magic_per_round; //magic we regain each round
         long heal_per_round; //hp we regain each round
         long armor; // reduces damage against us
         walker  * controller;
         command *commandlist; // head of command list
         command *endlist;     // end of command list
  private:
//       short com1, com2;        // parameters to command
         long walkrounds; //number of rounds we've spent rightwalking

};

class command
{
  public:
         command();
         short commandtype;
         short commandcount;
         short com1;
         short com2;
         command *next;
};

#endif

