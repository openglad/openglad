#ifndef __WALKER_H
#define __WALKER_H

// Definition of WALKER class

#include "base.h"
#include "pixien.h"

class walker : public pixieN
{
  public:
         friend class statistics;
         friend class command;
         walker(unsigned char  *data, screen  *myscreen);
         ~walker();
         short reset(void);
         short move(short x, short y);
         virtual short setxy(short x, short y);
         short walk();
         short walkstep(short x, short y);
         virtual short walk(short x, short y);
         short draw(viewscreen  *view_buf);
         short init_fire();
         short init_fire(short xdir, short ydir);
         void set_weapon_heading(walker *weapon);
         walker  * fire();
         virtual short act();
         short set_act_type(short num);
         short restore_act_type();
         short query_act_type();
         short set_old_act_type(short num);
         short query_old_act_type();
         virtual short collide(walker  *ob);
         short attack(walker  *target);
         virtual short animate();
         short set_order_family(char order, char family);
         virtual char query_order() { return order;}
         char query_family() { return family;}
         walker  *create_weapon();
         short fire_check(short xdelta, short ydelta);
         short query_next_to();
         short special();
         short teleport();
         short teleport_ranged(long range);
         long  turn_undead(long range, long power);
         virtual short shove(walker  *target, short x, short y);
         virtual short eat_me(walker  *eater);
         virtual void set_direct_frame(short whichframe);
         short turn(short targetdir);
         short spaces_clear(); // how many (of 8) spaces around us are clear
         void transfer_stats(walker  *newob); // transfer values to new walker
         void transform_to(char whatorder, char whatfamily); // change picture, etc.
         virtual short death(); // called when death/destruction occurs ..
         void generate_bloodspot(); // make a permanent stain ..
         virtual walker  *do_summon(char whatfamily, unsigned short lifetime);
         virtual short check_special();
         void center_on(walker  *target);  // center us on target
         virtual void set_difficulty(unsigned long whatlevel);
         long distance_to_ob(walker * target);
         long distance_to_ob_center(walker * target);
         virtual short facing(short x, short y);
         unsigned char query_team_color();
         long is_friendly(walker *target);
         inline short query_type(char oval, char fval) { if (oval == order && fval == family) return 1; else return 0; };

         
         unsigned long keys; // used to open doors
         short view_all;     // used for seeing treasures, etc. on radar
         short shifter_down; // is our shifter/alternate key pressed?
         short bonus_rounds; // used if an object has extra rounds this cycle
         short death_called; // if death has already been called
         short weapons_left;   // for fighter's blades
         short yo_delay;
         short lastx, lasty;
         signed char curdir;  // Current direction facing
         signed char cycle;
         signed char  **ani;
         char action;
         char team_num;
         char real_team_num; // for 'Charm', etc.
         char ani_type;
         long stepsize;
         long normal_stepsize; // used for elven forestwalk
         long lineofsight;
         long damage;
         signed char fire_frequency;
         char busy;
         statistics *stats;
         walker  *collide_ob;
         walker * foe;
         walker * leader;
         walker * owner;                // for weapons
         walker * myself;
         guy  *myguy;                   // our special stats..
         short dead;                    // safety check
         char ignore;                   // for non-colliding objects
         unsigned short default_weapon;
         unsigned short current_weapon;
         signed char user;              // are we being used by anyone?
         short flight_left;             // for bonus flight ..
         short invulnerable_left;
         short invisibility_left;
         short charm_left;              // If we're still being charmed
         unsigned char drawcycle;
         char current_special;
         long lifetime; // how much life summoned guys have ..
         short skip_exit; // cycles after failed exit choice
         unsigned char outline;
         walker * cachenext;
         short speed_bonus;             // These two are used for
         short speed_bonus_left;        // speed potions, etc.

  protected:
         short act_generate();
         short act_fire();
         short act_guard();
         virtual short act_random();
         char act_type,old_act_type;
         char enddir;  // Proposed direction facing
         char order, family;


};

#endif
