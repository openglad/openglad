/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//#include "graph.h"
#include "loader.h"
#include "stats.h"
#include "walker.h"
#include "living.h"
#include "treasure.h"
#include "weap.h"
#include "effect.h"

// Switch to "friendly" blood and stain pix files.
//#define NO_BLOOD

#define SIZE_ORDERS 7 // see graph.h
#define SIZE_FAMILIES 21  // see also NUM_FAMILIES in graph.h
//#define PIX(a,b) (SIZE_FAMILIES*a+b)  //moved to graph.h

// These are for monsters and us
signed char bit1[] = {(char) 1,(char) 5,(char) 1,(char) 9,(signed char) -1};     // up
signed char bit2[] = {(char) 13,(char) 17,(char) 13,(char) 21,(signed char) -1}; // up-right
signed char bit3[] = {(char) 2,(char) 6,(char) 2,(char) 10,(signed char) -1};    // right
signed char bit4[] = {(char) 14,(char) 18,(char) 14,(char) 22,(signed char) -1}; // down-right
signed char bit5[] = {(char) 0,(char) 4,(char) 0,(char) 8,(signed char) -1};     // down
signed char bit6[] = {(char) 12,(char) 16,(char) 12,(char) 20,(signed char) -1}; // down-left
signed char bit7[] = {(char) 3,(char) 7,(char) 3,(char) 11,(signed char) -1};    // left
signed char bit8[] = {(char) 15,(char) 19,(char) 15,(char) 23,(signed char) -1}; // up-left

signed char att1[] = {1,5,1,-1};       // up
signed char att2[] = {13,17,13,-1};    // up-right
signed char att3[] = {2,6,2,-1};       // right
signed char att4[] = {14,18,14,-1};    // down-right
signed char att5[] = {0,4,0,-1};       // down
signed char att6[] = {12,16,12,-1};    // down-left
signed char att7[] = {3,7,3,-1};       // left
signed char att8[] = {15,19,15,-1};    // up-left

signed char bitm2[] = {21,25,21,29,-1};  // up-right
signed char bitm4[] = {22,26,22,30,-1};  // down-right
signed char bitm6[] = {20,24,20,28,-1};  // down-left
signed char bitm8[] = {23,27,23,31,-1};  // up-left

signed char mageatt1[] = {5,17,1,-1};    // up
signed char mageatt2[] = {25,33,21,-1};  // up-right
signed char mageatt3[] = {6,18,2,-1};    // right
signed char mageatt4[] = {26,34,22,-1};  // down-right
signed char mageatt5[] = {4,16,0,-1};    // down
signed char mageatt6[] = {24,32,20,-1};  // down-left
signed char mageatt7[] = {7,19,3,-1};    // left
signed char mageatt8[] = {27,35,23,-1};  // up-left


signed char tele_out1[] = {12,13,14,15,-1};
signed char tele_in1[] = {15,14,13,12,1,-1};  // up
signed char tele_in2[] = {15,14,13,12,2,-1};  // right
signed char tele_in3[] = {15,14,13,12,0,-1};  // down
signed char tele_in4[] = {15,14,13,12,3,-1};  // left

// Big skeleton, who is currently different ...
signed char gs_down[] = {0, 1, 2, 3, -1}; // true "down"
signed char gs_up[] = {3, 2, 1, 0, -1}; // faked up :)

// Skeleton growing
signed char skel_grow[] =   {27, 26, 25, 24, 0, -1};
signed char skel_shrink[] = {0, 24, 25, 26, 27, -1};

// For slime unidirectional movement
signed char slime_pulse[] = { 0, 0, 1, 1, 2, 2, 1, 1, -1 };

signed char slime_split[] = { 8, 8, 9, 9, 10, 10,
                              11,11,12,12, 13, 13, -1 };

signed char small_slime[] = { 0, 0, 1, 1, 2, 2, 3, 3,
                              4, 4, 5, 5, 6, 6, 7, 7,
                              6, 6, 5, 5, 4 ,4, 3, 3,
                              2, 2, 1, 1, -1 };

// These are for the 'effect' objects
signed char series_8[] = {0, 1, 2, 3, 4, 5, 6, 7, -1};
signed char  *aniexpand8[] = { series_8, series_8, series_8, series_8,
                               series_8, series_8, series_8, series_8,
                               series_8, series_8, series_8, series_8,
                               series_8, series_8, series_8, series_8 };

//signed char series_16[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1};
signed char series_16[] = {0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, -1};
signed char *ani16[] = {series_16, series_16, series_16, series_16,
                        series_16, series_16, series_16, series_16,
                        series_16, series_16, series_16, series_16,
                        series_16, series_16, series_16, series_16};

signed char bomb1[] = {0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3,
                       4, 5, 4, 5, 4, 5, 4, 5, 6, 7, 6, 7, 6, 7, 6, 7,
                       8, 9, 8, 9, 8, 9, 8, 9, 10, 11, 10, 11, 10, 11, 10, 11,
                       12, 12, -1};
signed char  *anibomb1[] = {bomb1, bomb1, bomb1, bomb1,
                            bomb1, bomb1, bomb1, bomb1,
                            bomb1, bomb1, bomb1, bomb1,
                            bomb1, bomb1, bomb1, bomb1 };

signed char explosion1[] = {0, 1, 2, -1};
signed char  *aniexplosion1[] = {explosion1, explosion1, explosion1, explosion1,
                                 explosion1, explosion1, explosion1, explosion1,
                                 explosion1, explosion1, explosion1, explosion1,
                                 explosion1, explosion1, explosion1, explosion1 };

signed char cloud_cycle[] = {0, 1, 2, 3, 2, 1, -1};
signed char *anicloud[] = {cloud_cycle, cloud_cycle, cloud_cycle, cloud_cycle,
                           cloud_cycle, cloud_cycle, cloud_cycle, cloud_cycle,
                           cloud_cycle, cloud_cycle, cloud_cycle, cloud_cycle,
                           cloud_cycle, cloud_cycle, cloud_cycle, cloud_cycle};

signed char marker_cycle[] = {0, 1, 2, 3, 4,      // mage TP marker
                              5, 6, 7, 8, 9,
                              10,11,12,13,14,
                              15,16,17,18,19,-1};
signed char *animarker[] = {marker_cycle, marker_cycle, marker_cycle, marker_cycle,
                            marker_cycle, marker_cycle, marker_cycle, marker_cycle,
                            marker_cycle, marker_cycle, marker_cycle, marker_cycle,
                            marker_cycle, marker_cycle, marker_cycle, marker_cycle };

// These are for livings now
signed char  *animan[] = {
                             bit1, bit2, bit3, bit4, bit5, bit6, bit7, bit8,
                             att1, att2, att3, att4, att5, att6, att7, att8,
                         };

signed char  *aniskel[] = {
                              bit1, bit2, bit3, bit4,
                              bit5, bit6, bit7, bit8,
                              att1, att2, att3, att4,
                              att5, att6, att7, att8,
                              skel_shrink, skel_shrink, skel_shrink, skel_shrink,  // == tele_out
                              skel_shrink, skel_shrink, skel_shrink, skel_shrink,
                              skel_grow, skel_grow, skel_grow, skel_grow, // grow from ground (tele-in)
                              skel_grow, skel_grow, skel_grow, skel_grow,

                          };

signed char  *animage[] = {
                              bit1, bitm2, bit3, bitm4,
                              bit5, bitm6, bit7, bitm8,
                              mageatt1, mageatt2, mageatt3, mageatt4,       // 8 == attack
                              mageatt5, mageatt6, mageatt7, mageatt8,
                              tele_out1, tele_out1, tele_out1, tele_out1,   // 16 == tele_out
                              tele_out1, tele_out1, tele_out1, tele_out1,
                              tele_in1, tele_in1, tele_in2, tele_in2,       // 24 == tele_in
                              tele_in3, tele_in3, tele_in4, tele_in4,
                          };

// giant skeleton ..
signed char *anigs[] = {
                           gs_down, gs_up, gs_down, gs_up,
                           gs_down, gs_up, gs_down, gs_up,
                           gs_down, gs_up, gs_down, gs_up,
                           gs_down, gs_up, gs_down, gs_up,
                       };

signed char  *anislime[] = {
                               slime_pulse, slime_pulse, slime_pulse, slime_pulse, // 0 == walk
                               slime_pulse, slime_pulse, slime_pulse, slime_pulse,
                               slime_pulse, slime_pulse, slime_pulse, slime_pulse, // 8 == attack
                               slime_pulse, slime_pulse, slime_pulse, slime_pulse,
                               slime_pulse, slime_pulse, slime_pulse, slime_pulse, // 16 == tele_out (ignored)
                               slime_pulse, slime_pulse, slime_pulse, slime_pulse,
                               NULL, NULL, NULL, NULL,                             // 24 == tele_in (ignored)
                               NULL, NULL, NULL, NULL,
                               slime_split, slime_split, slime_split, slime_split, // 32 == slime splits
                               slime_split, slime_split, slime_split, slime_split,
                           };

signed char  *ani_small_slime[] = {
                                      small_slime, small_slime, small_slime, small_slime,
                                      small_slime, small_slime, small_slime, small_slime,
                                      small_slime, small_slime, small_slime, small_slime,
                                      small_slime, small_slime, small_slime, small_slime,
                                  };


// These are for the knives
signed char kni1[] = {7,6,5,4,3,2,1,0,-1};  // clockwise?
signed char kni2[] = {0,1,2,3,4,5,6,7,-1};  // counter?
signed char  *anikni[] = { kni2, kni2, kni1, kni1,
                           kni1, kni1, kni2, kni2,
                           kni2, kni2, kni1, kni1,
                           kni1, kni1, kni2, kni2 };

// These are for the rocks
signed char rock1[] = {0, -1};
signed char  *anirock[] = { rock1, rock1, rock1, rock1,
                            rock1, rock1, rock1, rock1,
                            rock1, rock1, rock1, rock1,
                            rock1, rock1, rock1, rock1 };

signed char grow1[] = {4, 3, 2, 1, 0, -1};
signed char  *anitree[] = { rock1, rock1, rock1, rock1,
                            rock1, rock1, rock1, rock1,
                            grow1, grow1, grow1, grow1,
                            grow1, grow1, grow1, grow1 };

signed char door1[] = {0, -1};
signed char door2[] = {1, -1};
signed char *anidoor[] = { door1, door1, door2, door2,
                           door1, door1, door2, door2,
                           door1, door1, door2, door2,
                           door1, door1, door2, door2 };


signed char dooropen1[] = {0, 2, 3, 4, 1, -1};
signed char dooropen2[] = {1, 4, 3, 2, 0, -1};
signed char *anidooropen[] = { door2, door2, door1, door1,
                               door2, door2, door1, door1,
                               dooropen1, dooropen1, dooropen2, dooropen2,
                               dooropen1, dooropen1, dooropen2, dooropen2 };

signed char arrow1[] = {1, -1}; // up
signed char arrow2[] = {5, -1}; // up-right
signed char arrow3[] = {2, -1}; // right
signed char arrow4[] = {6, -1}; // down-right
signed char arrow5[] = {0, -1}; // down
signed char arrow6[] = {4, -1}; // down-left
signed char arrow7[] = {3, -1}; // left
signed char arrow8[] = {7, -1}; // up-left
signed char  *aniarrow[] = { arrow1, arrow2, arrow3, arrow4,
                             arrow5, arrow6, arrow7, arrow8,
                             arrow1, arrow2, arrow3, arrow4,
                             arrow5, arrow6, arrow7, arrow8 };

// These are for the slimes' blobs
signed char blob1[] = {0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0, -1};
signed char  *aniblob1[] = { blob1, blob1, blob1, blob1,
                             blob1, blob1, blob1, blob1,
                             blob1, blob1, blob1, blob1,
                             blob1, blob1, blob1, blob1 };

signed char none1[] = {0, -1};
signed char  *aninone[] = { none1, none1, none1, none1,
                            none1, none1, none1, none1,
                            none1, none1, none1, none1,
                            none1, none1, none1, none1 };

// for the tower generator
signed char towerglow1[] = { 1,1,1,2,2,0,-1 };
signed char *anitower[] = { none1, none1, none1, none1,
                            none1, none1, none1, none1,
                            towerglow1, towerglow1, towerglow1, towerglow1,
                            towerglow1, towerglow1, towerglow1, towerglow1 };

// for tent generator
signed char tent1[] = { 1,1,1,2,2,2,3,3,3,4,4,4,0,-1 };
signed char *anitent[] = { none1, none1, none1, none1,
                           none1, none1, none1, none1,
                           tent1, tent1, tent1, tent1,
                           tent1, tent1, tent1, tent1 };

signed char blood1[] = {3,2,1,0,-1};
signed char  *aniblood[] = { rock1, rock1, rock1, rock1,
                             rock1, rock1, rock1, rock1,
                             blood1, blood1, blood1, blood1,
                             blood1, blood1, blood1, blood1, };

// These are for the cleric's glow thing
signed char glowgrow[] = {0, 1, 2, 3, -1};
signed char glowpulse[] = {4, 5, 6, 7, 8, 9, 8, 7, 6, 5, -1};
signed char  *aniglowgrow[] = { rock1, rock1, rock1, rock1,
                                rock1, rock1, rock1, rock1,
                                glowgrow, glowgrow, glowgrow, glowgrow,
                                glowgrow, glowgrow, glowgrow, glowgrow,
                                glowpulse, glowpulse, glowpulse, glowpulse,
                                glowpulse, glowpulse, glowpulse, glowpulse, };

// Treasure animations

signed char food1[] = {0, -1};
signed char  *anifood[] = { food1, food1, food1, food1,
                            food1, food1, food1, food1,
                            food1, food1, food1, food1,
                            food1, food1, food1, food1 };

PixieData data_copy(const PixieData& d)
{
    PixieData result;
    
    if(!d.valid())
        return result;
    
    result.frames = d.frames;
    result.w = d.w;
    result.h = d.h;
    
    Sint32 len = d.w * d.h * d.frames;
    result.data = new unsigned char[len];
    memcpy(result.data, d.data, len);
    
    return result;
}


loader::loader()
    : graphics(NULL), animations(NULL), stepsizes(NULL), lineofsight(NULL), act_types(NULL), damage(NULL), fire_frequency(NULL)
{
	memset(hitpoints, 0, 200*sizeof(short));
    
	graphics = new PixieData[SIZE_ORDERS*SIZE_FAMILIES];
        
	//  hitpoints = new char[SIZE_ORDERS*SIZE_FAMILIES];
	act_types = new char[SIZE_ORDERS*SIZE_FAMILIES];
	memset(act_types, ACT_RANDOM, SIZE_ORDERS*SIZE_FAMILIES);
	animations = new signed char**[SIZE_ORDERS*SIZE_FAMILIES];
	memset(animations, 0, SIZE_ORDERS*SIZE_FAMILIES);
	stepsizes = new Sint32[SIZE_ORDERS*SIZE_FAMILIES];
	lineofsight = new Sint32[SIZE_ORDERS*SIZE_FAMILIES];
	damage = new Sint32[SIZE_ORDERS*SIZE_FAMILIES];
	fire_frequency = new signed char[SIZE_ORDERS*SIZE_FAMILIES];


	// Livings
	graphics[PIX(ORDER_LIVING, FAMILY_SOLDIER)] = read_pixie_file("footman.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_ELF)] = read_pixie_file("elf.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_ARCHER)] = read_pixie_file("archer.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_THIEF)] = read_pixie_file("thief.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_MAGE)] = read_pixie_file("mage.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_SKELETON)] = read_pixie_file("skeleton.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_CLERIC)] = read_pixie_file("cleric.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_FIREELEMENTAL)] = read_pixie_file("firelem.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_FAERIE)] = read_pixie_file("faerie.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_SLIME)] = read_pixie_file("amoeba3.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_SMALL_SLIME)] = read_pixie_file("s_slime.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_MEDIUM_SLIME)] = read_pixie_file("m_slime.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_GHOST)] = read_pixie_file("ghost.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_DRUID)] = read_pixie_file("druid.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_ORC)] = read_pixie_file("orc.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_BIG_ORC)] = read_pixie_file("orc2.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_BARBARIAN)] = read_pixie_file("barby.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_ARCHMAGE)] = read_pixie_file("archmage.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_GOLEM)] = read_pixie_file("golem1.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_GIANT_SKELETON)] = read_pixie_file("gs1.pix");
	graphics[PIX(ORDER_LIVING, FAMILY_TOWER1)] = read_pixie_file("towersm1.pix");

	hitpoints[PIX(ORDER_LIVING, FAMILY_SOLDIER)] = 90;
	hitpoints[PIX(ORDER_LIVING, FAMILY_ELF)] = 30;
	hitpoints[PIX(ORDER_LIVING, FAMILY_ARCHER)] = 60;
	hitpoints[PIX(ORDER_LIVING, FAMILY_THIEF)] = 45;
	hitpoints[PIX(ORDER_LIVING, FAMILY_MAGE)] = 60;
	hitpoints[PIX(ORDER_LIVING, FAMILY_SKELETON)] = 30;
	hitpoints[PIX(ORDER_LIVING, FAMILY_CLERIC)] = 90;
	hitpoints[PIX(ORDER_LIVING, FAMILY_FIREELEMENTAL)] = 150;
	hitpoints[PIX(ORDER_LIVING, FAMILY_FAERIE)] = 45;
	hitpoints[PIX(ORDER_LIVING, FAMILY_SLIME)] = 120;
	hitpoints[PIX(ORDER_LIVING, FAMILY_SMALL_SLIME)] = 30;
	hitpoints[PIX(ORDER_LIVING, FAMILY_MEDIUM_SLIME)] = 30;
	hitpoints[PIX(ORDER_LIVING, FAMILY_GHOST)] = 40;
	hitpoints[PIX(ORDER_LIVING, FAMILY_DRUID)] = 80;
	hitpoints[PIX(ORDER_LIVING, FAMILY_ORC)] = 110;
	hitpoints[PIX(ORDER_LIVING, FAMILY_BIG_ORC)] = 150;
	hitpoints[PIX(ORDER_LIVING, FAMILY_BARBARIAN)] = 120;
	hitpoints[PIX(ORDER_LIVING, FAMILY_ARCHMAGE)] = 120;
	hitpoints[PIX(ORDER_LIVING, FAMILY_GOLEM)] = 270;
	hitpoints[PIX(ORDER_LIVING, FAMILY_GIANT_SKELETON)] = 270;
	hitpoints[PIX(ORDER_LIVING, FAMILY_TOWER1)] = 100;


	act_types[PIX(ORDER_LIVING, FAMILY_SOLDIER)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_ELF)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_ARCHER)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_THIEF)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_MAGE)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_SKELETON)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_CLERIC)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_FIREELEMENTAL)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_FAERIE)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_SLIME)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_SMALL_SLIME)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_MEDIUM_SLIME)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_GHOST)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_DRUID)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_ORC)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_BIG_ORC)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_BARBARIAN)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_ARCHMAGE)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_GOLEM)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_GIANT_SKELETON)] = ACT_RANDOM;
	act_types[PIX(ORDER_LIVING, FAMILY_TOWER1)] = ACT_RANDOM;

	animations[PIX(ORDER_LIVING, FAMILY_SOLDIER)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_ELF)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_ARCHER)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_THIEF)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_MAGE)] = animage;
	animations[PIX(ORDER_LIVING, FAMILY_SKELETON)] = aniskel;
	animations[PIX(ORDER_LIVING, FAMILY_CLERIC)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_FIREELEMENTAL)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_FAERIE)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_SLIME)] = anislime;
	animations[PIX(ORDER_LIVING, FAMILY_SMALL_SLIME)] = ani_small_slime;
	animations[PIX(ORDER_LIVING, FAMILY_MEDIUM_SLIME)] = ani_small_slime;
	animations[PIX(ORDER_LIVING, FAMILY_GHOST)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_DRUID)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_ORC)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_BIG_ORC)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_BARBARIAN)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_ARCHMAGE)] = animage;
	animations[PIX(ORDER_LIVING, FAMILY_GOLEM)] = animan;
	animations[PIX(ORDER_LIVING, FAMILY_GIANT_SKELETON)] = anigs;
	animations[PIX(ORDER_LIVING, FAMILY_TOWER1)] = anifood;

	stepsizes[PIX(ORDER_LIVING, FAMILY_SOLDIER)] = 4;
	stepsizes[PIX(ORDER_LIVING, FAMILY_ELF)] = 2;
	stepsizes[PIX(ORDER_LIVING, FAMILY_ARCHER)] = 4;
	stepsizes[PIX(ORDER_LIVING, FAMILY_THIEF)] = 5;
	stepsizes[PIX(ORDER_LIVING, FAMILY_MAGE)] = 2;
	stepsizes[PIX(ORDER_LIVING, FAMILY_SKELETON)] = 6;
	stepsizes[PIX(ORDER_LIVING, FAMILY_CLERIC)] = 2;
	stepsizes[PIX(ORDER_LIVING, FAMILY_FIREELEMENTAL)] = 4;
	stepsizes[PIX(ORDER_LIVING, FAMILY_FAERIE)] = 4;
	stepsizes[PIX(ORDER_LIVING, FAMILY_SLIME)] = 3;
	stepsizes[PIX(ORDER_LIVING, FAMILY_SMALL_SLIME)] = 2;
	stepsizes[PIX(ORDER_LIVING, FAMILY_MEDIUM_SLIME)] = 2;
	stepsizes[PIX(ORDER_LIVING, FAMILY_GHOST)] = 4;
	stepsizes[PIX(ORDER_LIVING, FAMILY_DRUID)] = 3;
	stepsizes[PIX(ORDER_LIVING, FAMILY_ORC)] = 3;
	stepsizes[PIX(ORDER_LIVING, FAMILY_BIG_ORC)] = 3;
	stepsizes[PIX(ORDER_LIVING, FAMILY_BARBARIAN)] = 4;
	stepsizes[PIX(ORDER_LIVING, FAMILY_ARCHMAGE)] = 3;
	stepsizes[PIX(ORDER_LIVING, FAMILY_GOLEM)] = 8;
	stepsizes[PIX(ORDER_LIVING, FAMILY_GIANT_SKELETON)] = 8;
	stepsizes[PIX(ORDER_LIVING, FAMILY_TOWER1)] = 0;

	lineofsight[PIX(ORDER_LIVING, FAMILY_SOLDIER)] = 7;
	lineofsight[PIX(ORDER_LIVING, FAMILY_ELF)] = 8;
	lineofsight[PIX(ORDER_LIVING, FAMILY_ARCHER)] = 12;
	lineofsight[PIX(ORDER_LIVING, FAMILY_THIEF)] = 10;
	lineofsight[PIX(ORDER_LIVING, FAMILY_MAGE)] = 7;
	lineofsight[PIX(ORDER_LIVING, FAMILY_SKELETON)] = 7;
	lineofsight[PIX(ORDER_LIVING, FAMILY_CLERIC)] = 4;
	lineofsight[PIX(ORDER_LIVING, FAMILY_FIREELEMENTAL)] = 10;
	lineofsight[PIX(ORDER_LIVING, FAMILY_FAERIE)] = 8;
	lineofsight[PIX(ORDER_LIVING, FAMILY_SLIME)] = 4;
	lineofsight[PIX(ORDER_LIVING, FAMILY_SMALL_SLIME)] = 2;
	lineofsight[PIX(ORDER_LIVING, FAMILY_MEDIUM_SLIME)] = 3;
	lineofsight[PIX(ORDER_LIVING, FAMILY_GHOST)] = 12;
	lineofsight[PIX(ORDER_LIVING, FAMILY_DRUID)] = 10;
	lineofsight[PIX(ORDER_LIVING, FAMILY_ORC)] = 20;
	lineofsight[PIX(ORDER_LIVING, FAMILY_BIG_ORC)] = 25;
	lineofsight[PIX(ORDER_LIVING, FAMILY_BARBARIAN)] = 12;
	lineofsight[PIX(ORDER_LIVING, FAMILY_ARCHMAGE)] = 10;
	lineofsight[PIX(ORDER_LIVING, FAMILY_GOLEM)] = 20;
	lineofsight[PIX(ORDER_LIVING, FAMILY_GIANT_SKELETON)] = 20;
	lineofsight[PIX(ORDER_LIVING, FAMILY_TOWER1)] = 10;

	// Strength of melee attack
	damage[PIX(ORDER_LIVING, FAMILY_SOLDIER)] = 20;
	damage[PIX(ORDER_LIVING, FAMILY_ELF)] = 12;
	damage[PIX(ORDER_LIVING, FAMILY_ARCHER)] = 8;
	damage[PIX(ORDER_LIVING, FAMILY_THIEF)] = 12;
	damage[PIX(ORDER_LIVING, FAMILY_MAGE)] = 4;
	damage[PIX(ORDER_LIVING, FAMILY_SKELETON)] = 4;
	damage[PIX(ORDER_LIVING, FAMILY_CLERIC)] = 12;
	damage[PIX(ORDER_LIVING, FAMILY_FIREELEMENTAL)] = 12;
	damage[PIX(ORDER_LIVING, FAMILY_FAERIE)] = 3;
	damage[PIX(ORDER_LIVING, FAMILY_SLIME)] = 28;
	damage[PIX(ORDER_LIVING, FAMILY_SMALL_SLIME)] = 12;
	damage[PIX(ORDER_LIVING, FAMILY_MEDIUM_SLIME)] = 20;
	damage[PIX(ORDER_LIVING, FAMILY_GHOST)] = 6;
	damage[PIX(ORDER_LIVING, FAMILY_DRUID)] = 10;
	damage[PIX(ORDER_LIVING, FAMILY_ORC)] = 25;
	damage[PIX(ORDER_LIVING, FAMILY_BIG_ORC)] = 30;
	damage[PIX(ORDER_LIVING, FAMILY_BARBARIAN)] = 25;
	damage[PIX(ORDER_LIVING, FAMILY_ARCHMAGE)] = 8;
	damage[PIX(ORDER_LIVING, FAMILY_GOLEM)] = 60;
	damage[PIX(ORDER_LIVING, FAMILY_GIANT_SKELETON)] = 60;
	damage[PIX(ORDER_LIVING, FAMILY_TOWER1)] = 0;

	fire_frequency[PIX(ORDER_LIVING, FAMILY_SOLDIER)] = 6;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_ELF)] = 7;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_ARCHER)] = 5;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_THIEF)] = 5;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_MAGE)] = 4;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_SKELETON)] = 6;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_CLERIC)] = 9;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_FIREELEMENTAL)] = 5;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_FAERIE)] = 9;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_SLIME)] = 11;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_SMALL_SLIME)] = 12;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_MEDIUM_SLIME)] = 10;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_GHOST)] = 5;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_DRUID)] = 9;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_ORC)] = 7;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_BIG_ORC)] = 6;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_BARBARIAN)] = 5;
	fire_frequency[PIX(ORDER_LIVING, FAMILY_ARCHMAGE)] = 1;  // REALLY FAST
	fire_frequency[PIX(ORDER_LIVING, FAMILY_GOLEM)] = 9;  // REALLY SLOW
	fire_frequency[PIX(ORDER_LIVING, FAMILY_GIANT_SKELETON)] = 7;  // SLOW
	fire_frequency[PIX(ORDER_LIVING, FAMILY_TOWER1)] = 5;  // Fast

	// Weapons
	graphics[PIX(ORDER_WEAPON, FAMILY_KNIFE)] = read_pixie_file("knife.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_ROCK)] = read_pixie_file("rock.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_ARROW)] = read_pixie_file("arrow.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_FIRE_ARROW)] = read_pixie_file("farrow.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_FIREBALL)] = read_pixie_file("fire.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_TREE)] = read_pixie_file("tree.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_METEOR)] = read_pixie_file("meteor.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_SPRINKLE)] = read_pixie_file("sparkle.pix");
	#ifndef NO_BLOOD
	graphics[PIX(ORDER_WEAPON, FAMILY_BLOOD)] = read_pixie_file("blood.pix");
	#else
	graphics[PIX(ORDER_WEAPON, FAMILY_BLOOD)] = read_pixie_file("blood_friendly.pix");
	#endif
	graphics[PIX(ORDER_WEAPON, FAMILY_BONE)] = read_pixie_file("bone1.pix");
	#ifndef NO_BLOOD
	graphics[PIX(ORDER_TREASURE,FAMILY_STAIN)] = read_pixie_file("stain.pix");
	#else
	graphics[PIX(ORDER_TREASURE,FAMILY_STAIN)] = read_pixie_file("stain_friendly.pix");
	#endif
	graphics[PIX(ORDER_WEAPON, FAMILY_BLOB)] = read_pixie_file("sl_ball.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_LIGHTNING)] = read_pixie_file("lightnin.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_GLOW)] = read_pixie_file("clerglow.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_WAVE)] = read_pixie_file("wave.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_WAVE2)] = read_pixie_file("wave2.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = read_pixie_file("wave3.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_CIRCLE_PROTECTION)] = read_pixie_file("wave2.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_HAMMER)] = read_pixie_file("hammer.pix");
	
	graphics[PIX(ORDER_WEAPON, FAMILY_DOOR)] = read_pixie_file("door.pix");
	graphics[PIX(ORDER_WEAPON, FAMILY_BOULDER)] = read_pixie_file("boulder1.pix");

	hitpoints[PIX(ORDER_WEAPON, FAMILY_KNIFE)] = 6;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_BONE)] = 5;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_ROCK)] = 4;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_ARROW)] = 5;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_FIRE_ARROW)] = 7;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_FIREBALL)] = 8;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_TREE)] = 50;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_METEOR)] = 12;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_SPRINKLE)] = 1;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_BLOB)] = 1;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_LIGHTNING)] = 60;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_GLOW)] = 50;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_WAVE)] = 50;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_WAVE2)] = 50;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = 50;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_CIRCLE_PROTECTION)] = 50;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_HAMMER)] = 10;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_DOOR)] = 5000;
	hitpoints[PIX(ORDER_WEAPON, FAMILY_BOULDER)] = 50;

	act_types[PIX(ORDER_WEAPON, FAMILY_KNIFE)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_BONE)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_ROCK)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_ARROW)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_FIRE_ARROW)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_FIREBALL)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_TREE)] = ACT_SIT;
	act_types[PIX(ORDER_WEAPON, FAMILY_METEOR)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_SPRINKLE)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_BLOOD)] = ACT_DIE;
	act_types[PIX(ORDER_WEAPON, FAMILY_BLOB)] = ACT_FIRE;
	act_types[PIX(ORDER_TREASURE,     FAMILY_STAIN)] = ACT_CONTROL;
	act_types[PIX(ORDER_WEAPON, FAMILY_LIGHTNING)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_GLOW)] = ACT_SIT;
	act_types[PIX(ORDER_WEAPON, FAMILY_WAVE)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_CIRCLE_PROTECTION)] = ACT_SIT;
	act_types[PIX(ORDER_WEAPON, FAMILY_HAMMER)] = ACT_FIRE;
	act_types[PIX(ORDER_WEAPON, FAMILY_DOOR)] = ACT_SIT;
	act_types[PIX(ORDER_WEAPON, FAMILY_BOULDER)] = ACT_FIRE;

	animations[PIX(ORDER_WEAPON, FAMILY_KNIFE)] = anikni;
	animations[PIX(ORDER_WEAPON, FAMILY_BONE)] = anikni;
	animations[PIX(ORDER_WEAPON, FAMILY_ROCK)] = anirock;
	animations[PIX(ORDER_WEAPON, FAMILY_ARROW)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_FIRE_ARROW)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_FIREBALL)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_TREE)] = anitree;
	animations[PIX(ORDER_WEAPON, FAMILY_METEOR)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_SPRINKLE)] = anikni;
	animations[PIX(ORDER_WEAPON, FAMILY_BLOOD)] = aniblood;
	animations[PIX(ORDER_WEAPON, FAMILY_BLOB)] = aniblob1;
	animations[PIX(ORDER_TREASURE,     FAMILY_STAIN)] = aniblood;
	animations[PIX(ORDER_WEAPON, FAMILY_LIGHTNING)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_GLOW)] = aniglowgrow;
	animations[PIX(ORDER_WEAPON, FAMILY_WAVE)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_WAVE2)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_CIRCLE_PROTECTION)] = anifood;
	animations[PIX(ORDER_WEAPON, FAMILY_HAMMER)] = aniarrow;
	animations[PIX(ORDER_WEAPON, FAMILY_DOOR)] = anidoor;
	animations[PIX(ORDER_WEAPON, FAMILY_BOULDER)] = aninone;

	stepsizes[PIX(ORDER_WEAPON, FAMILY_KNIFE)] = 5;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_BONE)] = 6;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_ROCK)] = 5;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_ARROW)] = 8;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_FIRE_ARROW)] = 8;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_FIREBALL)] = 6;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_TREE)] = 0;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_METEOR)] = 7;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_SPRINKLE)] = 6;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_BLOOD)] = 0;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_BLOB)] = 2;
	stepsizes[PIX(ORDER_TREASURE,     FAMILY_STAIN)] = 0;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_LIGHTNING)] = 9;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_GLOW)] = 0;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_WAVE)] = 6;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_WAVE2)] = 4;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = 3;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_CIRCLE_PROTECTION)] = 1;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_HAMMER)] = 6;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_DOOR)] = 0;
	stepsizes[PIX(ORDER_WEAPON, FAMILY_BOULDER)] = 10;

	// Acts as weapon's range (pixel range == lineofsight * stepsize)
	lineofsight[PIX(ORDER_WEAPON, FAMILY_KNIFE)] = 7;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_BONE)] = 6;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_ROCK)] = 8;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_ARROW)] = 12;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_FIRE_ARROW)] = 12;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_FIREBALL)] = 7;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_TREE)] = 1;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_METEOR)] = 9;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_SPRINKLE)] = 10;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_BLOB)] = 11;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_BLOOD)] = 1;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_LIGHTNING)] = 13;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_GLOW)] = 1;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_WAVE)] = 3;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_WAVE2)] = 4;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = 6;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_CIRCLE_PROTECTION)] = 110;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_HAMMER)] = 5;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_DOOR)] = 1;
	lineofsight[PIX(ORDER_WEAPON, FAMILY_BOULDER)] = 5;

	// Strength of weapon
	damage[PIX(ORDER_WEAPON, FAMILY_KNIFE)] = 6;
	damage[PIX(ORDER_WEAPON, FAMILY_BONE)] = 5;
	damage[PIX(ORDER_WEAPON, FAMILY_ROCK)] = 4;
	damage[PIX(ORDER_WEAPON, FAMILY_ARROW)] = 5;
	damage[PIX(ORDER_WEAPON, FAMILY_FIRE_ARROW)] = 7;
	damage[PIX(ORDER_WEAPON, FAMILY_FIREBALL)] = 10;
	damage[PIX(ORDER_WEAPON, FAMILY_TREE)] = 0;
	damage[PIX(ORDER_WEAPON, FAMILY_METEOR)] = 12;
	damage[PIX(ORDER_WEAPON, FAMILY_SPRINKLE)] = 1;
	damage[PIX(ORDER_WEAPON, FAMILY_BLOB)] = 1;
	damage[PIX(ORDER_WEAPON, FAMILY_BLOOD)] = 0;
	damage[PIX(ORDER_WEAPON, FAMILY_LIGHTNING)] = 6;
	damage[PIX(ORDER_WEAPON, FAMILY_GLOW)] = 0;
	damage[PIX(ORDER_WEAPON, FAMILY_WAVE)] = 16;
	damage[PIX(ORDER_WEAPON, FAMILY_WAVE2)] = 12;
	damage[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = 10;
	damage[PIX(ORDER_WEAPON, FAMILY_CIRCLE_PROTECTION)] = 0;
	damage[PIX(ORDER_WEAPON, FAMILY_HAMMER)] = 9;
	damage[PIX(ORDER_WEAPON, FAMILY_DOOR)] = 0;
	damage[PIX(ORDER_WEAPON, FAMILY_BOULDER)] = 25;

	fire_frequency[PIX(ORDER_WEAPON, FAMILY_KNIFE)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_BONE)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_ROCK)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_ARROW)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_FIRE_ARROW)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_FIREBALL)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_TREE)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_METEOR)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_SPRINKLE)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_BLOB)] = 2;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_BLOOD)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_LIGHTNING)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_GLOW)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_WAVE)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_WAVE2)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_WAVE3)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_HAMMER)] = 0;
	fire_frequency[PIX(ORDER_WEAPON, FAMILY_BOULDER)] = 0;

	// Treasure items (food, etc.)
	graphics[PIX(ORDER_TREASURE, FAMILY_DRUMSTICK)] = read_pixie_file("food1.pix");
	graphics[PIX(ORDER_TREASURE, FAMILY_GOLD_BAR)] = read_pixie_file("bar1.pix");
	graphics[PIX(ORDER_TREASURE, FAMILY_SILVER_BAR)] = data_copy(graphics[PIX(ORDER_TREASURE, FAMILY_GOLD_BAR)]);
	graphics[PIX(ORDER_TREASURE, FAMILY_MAGIC_POTION)] = read_pixie_file("bottle.pix");
	graphics[PIX(ORDER_TREASURE, FAMILY_INVIS_POTION)] = data_copy(graphics[PIX(ORDER_TREASURE, FAMILY_MAGIC_POTION)]);
	graphics[PIX(ORDER_TREASURE, FAMILY_INVULNERABLE_POTION)] = data_copy(graphics[PIX(ORDER_TREASURE, FAMILY_MAGIC_POTION)]);
	graphics[PIX(ORDER_TREASURE, FAMILY_FLIGHT_POTION)] = data_copy(graphics[PIX(ORDER_TREASURE, FAMILY_MAGIC_POTION)]);
	graphics[PIX(ORDER_TREASURE, FAMILY_EXIT)] = read_pixie_file("16exit1.pix");
	graphics[PIX(ORDER_TREASURE, FAMILY_TELEPORTER)] = read_pixie_file("teleport.pix");
	graphics[PIX(ORDER_TREASURE, FAMILY_LIFE_GEM)] = read_pixie_file("lifegem.pix");
	graphics[PIX(ORDER_TREASURE, FAMILY_KEY)] = read_pixie_file("key.pix");
	graphics[PIX(ORDER_TREASURE, FAMILY_SPEED_POTION)] = data_copy(graphics[PIX(ORDER_TREASURE, FAMILY_MAGIC_POTION)]);

	hitpoints[PIX(ORDER_TREASURE, FAMILY_DRUMSTICK)] = 10;
	hitpoints[PIX(ORDER_TREASURE, FAMILY_GOLD_BAR)] = 1000;
	hitpoints[PIX(ORDER_TREASURE, FAMILY_SILVER_BAR)] = 100;

	act_types[PIX(ORDER_TREASURE, FAMILY_DRUMSTICK)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_GOLD_BAR)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_SILVER_BAR)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_MAGIC_POTION)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_INVIS_POTION)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_INVULNERABLE_POTION)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_FLIGHT_POTION)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_EXIT)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_TELEPORTER)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_LIFE_GEM)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_KEY)] = ACT_CONTROL;
	act_types[PIX(ORDER_TREASURE, FAMILY_SPEED_POTION)] = ACT_CONTROL;

	animations[PIX(ORDER_TREASURE, FAMILY_DRUMSTICK)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_GOLD_BAR)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_SILVER_BAR)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_MAGIC_POTION)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_INVIS_POTION)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_INVULNERABLE_POTION)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_FLIGHT_POTION)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_EXIT)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_TELEPORTER)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_LIFE_GEM)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_KEY)] = anifood;
	animations[PIX(ORDER_TREASURE, FAMILY_SPEED_POTION)] = anifood;

	stepsizes[PIX(ORDER_TREASURE, FAMILY_DRUMSTICK)] = 5;

	// Generator
	graphics[PIX(ORDER_GENERATOR, FAMILY_TENT)] = read_pixie_file("tent.pix");
	graphics[PIX(ORDER_GENERATOR, FAMILY_TOWER)] = read_pixie_file("tower4.pix");
	graphics[PIX(ORDER_GENERATOR, FAMILY_BONES)] = read_pixie_file("bonepile.pix");
	graphics[PIX(ORDER_GENERATOR, FAMILY_TREEHOUSE)] = read_pixie_file("bigtree.pix");
	hitpoints[PIX(ORDER_GENERATOR, FAMILY_TENT)] = 100;

	act_types[PIX(ORDER_GENERATOR, FAMILY_TENT)] = ACT_GENERATE;
	act_types[PIX(ORDER_GENERATOR, FAMILY_TOWER)] = ACT_GENERATE;
	act_types[PIX(ORDER_GENERATOR, FAMILY_BONES)] = ACT_GENERATE;
	act_types[PIX(ORDER_GENERATOR, FAMILY_TREEHOUSE)] = ACT_GENERATE;

	animations[PIX(ORDER_GENERATOR, FAMILY_TENT)] = anitent;
	animations[PIX(ORDER_GENERATOR, FAMILY_TOWER)] = anitower;
	animations[PIX(ORDER_GENERATOR, FAMILY_BONES)] = aninone;
	animations[PIX(ORDER_GENERATOR, FAMILY_TREEHOUSE)] = aninone;

	stepsizes[PIX(ORDER_GENERATOR, FAMILY_TENT)] = 0;
	stepsizes[PIX(ORDER_GENERATOR, FAMILY_TOWER)] = 0;
	stepsizes[PIX(ORDER_GENERATOR, FAMILY_BONES)] = 0;
	stepsizes[PIX(ORDER_GENERATOR, FAMILY_TREEHOUSE)] = 0;

	lineofsight[PIX(ORDER_GENERATOR, FAMILY_TENT)] = 0;
	lineofsight[PIX(ORDER_GENERATOR, FAMILY_TOWER)] = 0;
	lineofsight[PIX(ORDER_GENERATOR, FAMILY_BONES)] = 0;
	lineofsight[PIX(ORDER_GENERATOR, FAMILY_TREEHOUSE)] = 0;

	damage[PIX(ORDER_GENERATOR, FAMILY_TENT)] = 0;
	damage[PIX(ORDER_GENERATOR, FAMILY_TOWER)] = 0;
	damage[PIX(ORDER_GENERATOR, FAMILY_BONES)] = 2;
	damage[PIX(ORDER_GENERATOR, FAMILY_TREEHOUSE)] = 0;

	fire_frequency[PIX(ORDER_GENERATOR, FAMILY_TENT)] = 0;
	fire_frequency[PIX(ORDER_GENERATOR, FAMILY_TOWER)] = 0;
	fire_frequency[PIX(ORDER_GENERATOR, FAMILY_BONES)] = 0;
	fire_frequency[PIX(ORDER_GENERATOR, FAMILY_TREEHOUSE)] = 0;

	// Specials ..
	graphics[PIX(ORDER_SPECIAL, FAMILY_RESERVED_TEAM)] = read_pixie_file("team.pix");

	// Effects ..
	graphics[PIX(ORDER_FX, FAMILY_EXPAND)] = read_pixie_file("expand8.pix");
	graphics[PIX(ORDER_FX, FAMILY_GHOST_SCARE)]  = read_pixie_file("expand8.pix");
	graphics[PIX(ORDER_FX, FAMILY_BOMB)]  = read_pixie_file("bomb1.pix");
	graphics[PIX(ORDER_FX, FAMILY_EXPLOSION)]  = read_pixie_file("boom1.pix");
	graphics[PIX(ORDER_FX, FAMILY_FLASH)]  = read_pixie_file("telflash.pix");
	graphics[PIX(ORDER_FX, FAMILY_MAGIC_SHIELD)] = read_pixie_file("mshield.pix");
	graphics[PIX(ORDER_FX, FAMILY_KNIFE_BACK)] = read_pixie_file("knife.pix");
	graphics[PIX(ORDER_FX, FAMILY_CLOUD)] = read_pixie_file("cloud.pix");
	graphics[PIX(ORDER_FX, FAMILY_MARKER)] = read_pixie_file("marker.pix");
	graphics[PIX(ORDER_FX, FAMILY_BOOMERANG)] = read_pixie_file("boomer.pix");
	graphics[PIX(ORDER_FX, FAMILY_CHAIN)] = read_pixie_file("lightnin.pix");
	graphics[PIX(ORDER_FX, FAMILY_DOOR_OPEN)] = read_pixie_file("door.pix");

	animations[PIX(ORDER_FX, FAMILY_EXPAND)] = aniexpand8;
	animations[PIX(ORDER_FX, FAMILY_GHOST_SCARE)] = aniexpand8;
	animations[PIX(ORDER_FX, FAMILY_BOMB)] = anibomb1;
	animations[PIX(ORDER_FX, FAMILY_EXPLOSION)] = aniexplosion1;
	animations[PIX(ORDER_FX, FAMILY_FLASH)] = aniexpand8;
	animations[PIX(ORDER_FX, FAMILY_MAGIC_SHIELD)] = anikni;
	animations[PIX(ORDER_FX, FAMILY_KNIFE_BACK)] = anikni;
	animations[PIX(ORDER_FX, FAMILY_BOOMERANG)] = ani16;
	animations[PIX(ORDER_FX, FAMILY_CLOUD)] = anicloud;
	animations[PIX(ORDER_FX, FAMILY_MARKER)] = animarker;
	animations[PIX(ORDER_FX, FAMILY_CHAIN)] = aniarrow;
	animations[PIX(ORDER_FX, FAMILY_DOOR_OPEN)] = anidooropen;

	stepsizes[PIX(ORDER_FX, FAMILY_CLOUD)] = 4;
	stepsizes[PIX(ORDER_FX, FAMILY_CHAIN)] = 12;  // REALLY fast!

	lineofsight[PIX(ORDER_FX, FAMILY_CHAIN)] = 15;

	hitpoints[PIX(ORDER_FX, FAMILY_MAGIC_SHIELD)] = 100;
	hitpoints[PIX(ORDER_FX, FAMILY_BOOMERANG)] = 50;

	damage[PIX(ORDER_FX, FAMILY_MAGIC_SHIELD)] = 10;
	damage[PIX(ORDER_FX, FAMILY_BOOMERANG)] = 8;
	damage[PIX(ORDER_FX, FAMILY_CLOUD)] = 20;

	// These are button graphics ..
	graphics[PIX(ORDER_BUTTON1, FAMILY_NORMAL1)] = read_pixie_file("normal1.pix");
	graphics[PIX(ORDER_BUTTON1, FAMILY_PLUS)] = read_pixie_file("butplus.pix");
	graphics[PIX(ORDER_BUTTON1, FAMILY_MINUS)] = read_pixie_file("butminus.pix");

}

loader::~loader(void)
{
	int i;
	for(i=0;i<(SIZE_ORDERS*SIZE_FAMILIES);i++) {
	    graphics[i].free();
	}
	
	delete[] graphics;

	delete[] animations;
	delete[] act_types;
	delete[] stepsizes;
	delete[] lineofsight;
	delete[] damage;
	delete[] fire_frequency;
}

walker  *loader::create_walker(char order,
                               char family,
                               screen  *myscreen, bool cache_weapons)
{
	walker  *ob;

	//i = PIX(order, family);
	//Log("PIX(order, family) = %d\n", i);

	if (!graphics[PIX(order, family)].valid())
	{
		Log("Alert! No valid graphics for walker\n");
		return NULL;
	}

	if (order == ORDER_LIVING)
		ob = new living(graphics[PIX(order, family)], myscreen);
	else if (order == ORDER_WEAPON)
	{
		if (cache_weapons && myscreen != NULL && myscreen->weapfree) //there is one available
		{
			ob = myscreen->weapfree; //get the ob
			myscreen->weapfree = myscreen->weapfree->cachenext; //move up weapfree
			ob->reset();
			//      ob->death_called = 0;
			//      ob->cachenext = NULL;
			ob->transform_to(order,family);
			//      ob->dead = 0; //make it not dead anymore
			//      ob->stats->bit_flags = 0;
		}
		else
			ob = new weap(graphics[PIX(order, family)], myscreen);
	}
	else if (order == ORDER_TREASURE)
		ob = new treasure(graphics[PIX(order, family)], myscreen);
	else if (order == ORDER_FX)
		ob = new effect(graphics[PIX(order, family)], myscreen);
	else
		ob = new walker(graphics[PIX(order, family)], myscreen);
	if (!ob)
		return NULL;

	ob->stats->hitpoints = hitpoints[PIX(order, family)];
	ob->stats->max_hitpoints = hitpoints[PIX(order, family)];
	ob->stats->special_cost[0] = 0; // shouldn't be used
	ob->stats->weapon_cost = 1; // default value

	set_walker(ob, order, family);
	
	if(order == ORDER_LIVING)
        ob->set_frame(ob->ani[ob->curdir][0]);
	return ob;
}

walker  *loader::set_walker(walker *ob,
                            char order,
                            char family)
{
	short i;

	ob->set_order_family(order,family);
	ob->set_act_type(act_types[PIX(order, family)]);
	ob->ani = animations[PIX(order, family)];
	ob->stepsize = stepsizes[PIX(order, family)];
	ob->normal_stepsize = ob->stepsize;
	ob->lineofsight = lineofsight[PIX(order, family)];
	ob->damage = damage[PIX(order, family)];
	ob->fire_frequency = fire_frequency[PIX(order, family)];

	for (i=0; i < NUM_SPECIALS; i++)
		ob->stats->special_cost[i] = 5000;

	// For special settings
	switch (order)
	{
		case ORDER_LIVING:
			switch (family)
			{
				case FAMILY_SOLDIER:
					ob->stats->special_cost[1] = 25; // charge
					ob->stats->special_cost[2] = 100; // boomerang
					ob->stats->special_cost[3] = 120; // whirlwind
					ob->stats->special_cost[4] = 150; // disarm
					ob->stats->weapon_cost = 2;
					ob->default_weapon = FAMILY_KNIFE;
					break;
				case FAMILY_ELF:
					ob->stats->special_cost[1] = 20;
					ob->stats->special_cost[2] = 28;
					ob->stats->special_cost[3] = 36;
					ob->stats->special_cost[4] = 42;
					ob->stats->set_bit_flags(BIT_FORESTWALK, 1);
					ob->default_weapon = FAMILY_ROCK;
					break;
				case FAMILY_ARCHER:
					ob->stats->special_cost[1] = 20;  // fire arrows
					ob->stats->special_cost[2] = 60; // 3-arrows
					ob->stats->special_cost[3] = 70; // exploding bolt
					ob->default_weapon = FAMILY_ARROW;
					break;
				case FAMILY_THIEF:
					ob->stats->special_cost[1] = 35;  // bomb
					ob->stats->special_cost[2] = 125; // cloak, R
					ob->stats->special_cost[3] = 100; // taunt, R
					ob->stats->special_cost[4] = 150; // poison cloud, R
					ob->default_weapon = FAMILY_KNIFE;
					break;
				case FAMILY_CLERIC:
					ob->default_weapon = FAMILY_GLOW; //FAMILY_TREE;
					ob->stats->weapon_cost = 8;
					ob->stats->special_cost[1] = 2; // heal / mystic mace
					ob->stats->special_cost[2] = 20; // skeleton
					ob->stats->special_cost[3] = 50; // ghost
					ob->stats->special_cost[4] = 150; // raise dead
					break;
				case FAMILY_SKELETON:
					ob->default_weapon = FAMILY_BONE;
					ob->ani_type = ANI_SKEL_GROW;
					ob->stats->weapon_cost = 0; // free bones
					ob->stats->special_cost[1] = 10; // tunnel
					break;
				case FAMILY_FAERIE:
					ob->stats->set_bit_flags(BIT_ANIMATE, 1);
					ob->stats->set_bit_flags(BIT_FLYING, 1);
					ob->default_weapon = FAMILY_SPRINKLE;
					ob->stats->weapon_cost = 2;
					break;
				case FAMILY_MAGE:
					ob->stats->special_cost[1] = 15;  // cost to teleport
					ob->stats->special_cost[2] = 60; // warp space
					ob->stats->special_cost[3] = 500;  // freeze time
					ob->stats->special_cost[4] = 70; // energy wave
					ob->stats->special_cost[5] = 100; // heartburst
					ob->stats->weapon_cost = 5;
					ob->default_weapon = FAMILY_FIREBALL;
					break;
				case FAMILY_ARCHMAGE:
					ob->stats->special_cost[1] = 10;  // cost to teleport
					ob->stats->special_cost[2] = 80; // heartburst
					ob->stats->special_cost[3] = 500;  // summon elemental
					ob->stats->special_cost[4] = 150; // Mind-control enemies
					ob->stats->weapon_cost = 12;
					ob->default_weapon = FAMILY_FIREBALL;
					break;
				case FAMILY_FIREELEMENTAL:
					ob->stats->set_bit_flags(BIT_ANIMATE, 1);
					ob->stats->special_cost[1] = 50; // fireballs
					ob->stats->max_magicpoints = 150;
					ob->default_weapon = FAMILY_METEOR;
					break;
				case FAMILY_SLIME:
				case FAMILY_SMALL_SLIME:
				case FAMILY_MEDIUM_SLIME:
					ob->stats->set_bit_flags(BIT_ANIMATE, 1); // always wiggles
					ob->stats->special_cost[1] = 30;
					ob->stats->max_magicpoints = 50;
					//                         ob->stats->magicpoints = 0;
					if (order == FAMILY_SMALL_SLIME)
						ob->stats->set_bit_flags(BIT_NO_RANGED, 1); // no ranged attack
					ob->default_weapon = FAMILY_BLOB;
					ob->stats->weapon_cost = 0; // free slimeball
					break;
				case FAMILY_GHOST:
					ob->stats->set_bit_flags(BIT_ANIMATE, 1); // always move
					ob->stats->special_cost[1] = 30; // scare
					ob->stats->set_bit_flags(BIT_FLYING, 1);
					ob->stats->set_bit_flags(BIT_ETHEREAL, 1);
					ob->stats->set_bit_flags(BIT_NO_RANGED, 1);
					ob->default_weapon = FAMILY_KNIFE;
					ob->stats->weapon_cost = 0; // free melee
					break;
				case FAMILY_DRUID:
					ob->default_weapon = FAMILY_LIGHTNING;
					ob->stats->weapon_cost = 4;
					ob->stats->special_cost[1] = 15; // grow tree
					ob->stats->special_cost[2] = 80; // summon faerie
					ob->stats->special_cost[3] = 150; // reveal items
					ob->stats->special_cost[4] = 200; // protection shield
					break;
				case FAMILY_ORC:
					ob->stats->special_cost[1] = 25; // howl
					ob->stats->special_cost[2] = 20; // eat corpse
					ob->stats->weapon_cost = 2;
					ob->default_weapon = FAMILY_ROCK;
					ob->stats->set_bit_flags(BIT_NO_RANGED, 1);
					break;
				case FAMILY_BIG_ORC:
					ob->stats->weapon_cost = 2;
					ob->default_weapon = FAMILY_KNIFE;
					break;
				case FAMILY_BARBARIAN:
					ob->stats->weapon_cost = 2;
					ob->default_weapon = FAMILY_HAMMER;
					ob->stats->special_cost[1] = 20; // hurl boulder
					ob->stats->special_cost[2] = 30; // exploding boulder
					break;
				case FAMILY_GOLEM:
					ob->stats->weapon_cost = 2;
					//ob->stats->set_bit_flags(BIT_NO_RANGED, 1);
					ob->default_weapon = FAMILY_BOULDER; // default for now
					break;
				case FAMILY_GIANT_SKELETON:
					ob->stats->weapon_cost = 2;
					ob->default_weapon = FAMILY_BOULDER; // default for now
					break;
				case FAMILY_TOWER1: // not *really* a living ...
					ob->stats->weapon_cost = 2;
					ob->default_weapon = FAMILY_FIREBALL;
					//ob->stepsize = 0;
					//ob->normal_stepsize = 0;
					break;
				default:
					ob->transform_to(ORDER_LIVING, FAMILY_SOLDIER);
					return ob;
					break;
			}
			ob->current_weapon = ob->default_weapon;
			break; // end of livings
		case ORDER_WEAPON:
			switch (family)
			{
				case FAMILY_ROCK:
					ob->stats->set_bit_flags(BIT_FORESTWALK, 1);
					break;
				case FAMILY_FIREBALL:
					ob->stats->set_bit_flags(BIT_MAGICAL, 1);
					break;
				case FAMILY_METEOR:
					ob->stats->set_bit_flags(BIT_MAGICAL, 1);
					break;
				case FAMILY_SPRINKLE:
					ob->stats->set_bit_flags(BIT_FLYING, 1);
					break;
				case FAMILY_GLOW: // cleric's shield glad
					ob->lifetime = 350;
					break;
				case FAMILY_WAVE:
					ob->stats->set_bit_flags(BIT_IMMORTAL, 1);
					ob->stats->set_bit_flags(BIT_NO_COLLIDE, 1);
					ob->stats->set_bit_flags(BIT_PHANTOM, 1);
					ob->stats->set_bit_flags(BIT_FLYING, 1);
					ob->stats->set_bit_flags(BIT_MAGICAL, 1);
					break;
				case FAMILY_WAVE2:
					ob->stats->set_bit_flags(BIT_IMMORTAL, 1);
					ob->stats->set_bit_flags(BIT_NO_COLLIDE, 1);
					ob->stats->set_bit_flags(BIT_PHANTOM, 1);
					ob->stats->set_bit_flags(BIT_FLYING, 1);
					ob->stats->set_bit_flags(BIT_MAGICAL, 1);
					break;
				case FAMILY_WAVE3:
					ob->stats->set_bit_flags(BIT_IMMORTAL, 1);
					ob->stats->set_bit_flags(BIT_NO_COLLIDE, 1);
					ob->stats->set_bit_flags(BIT_PHANTOM, 1);
					ob->stats->set_bit_flags(BIT_FLYING, 1);
					ob->stats->set_bit_flags(BIT_MAGICAL, 1);
					break;
				case FAMILY_CIRCLE_PROTECTION:
					ob->stats->set_bit_flags(BIT_IMMORTAL, 1);
					ob->stats->set_bit_flags(BIT_NO_COLLIDE, 1);
					ob->stats->set_bit_flags(BIT_PHANTOM, 1);
					ob->stats->set_bit_flags(BIT_FLYING, 1);
					ob->ani_type = 5; // anything non-zero
					break;
				default:
					break;
			}  // end of weapons
		case ORDER_TREASURE:
			switch (family)
			{
				case FAMILY_STAIN:  // permanent bloodstains
					ob->ignore = 1;
					break;
				case FAMILY_GOLD_BAR:
					ob->set_direct_frame(0);
					break;
				case FAMILY_SILVER_BAR:
					ob->set_direct_frame(1);
					break;
				case FAMILY_MAGIC_POTION:
					ob->set_direct_frame(0);
					break;
				case FAMILY_INVIS_POTION:
					ob->set_direct_frame(1);
					break;
				case FAMILY_INVULNERABLE_POTION:
					ob->set_direct_frame(2);
					break;
				case FAMILY_FLIGHT_POTION:
					ob->set_direct_frame(11);
					break;
				case FAMILY_SPEED_POTION:
					ob->set_direct_frame(3);
					break;
				default :
					break;
			} // end of treasures
		case ORDER_GENERATOR:
			switch (family)
			{
				case FAMILY_TOWER:
					ob->stats->weapon_cost = 0;
					ob->default_weapon = FAMILY_MAGE;
					break;
				case FAMILY_BONES: // ghost bone pile
					ob->stats->weapon_cost = 0;
					ob->default_weapon = FAMILY_GHOST;
					break;
				case FAMILY_TREEHOUSE: // elf tree-house
					ob->stats->weapon_cost = 0;
					ob->default_weapon = FAMILY_ELF;
					break;
				default:
					ob->default_weapon = FAMILY_SKELETON;
					ob->stats->weapon_cost = 0;
					break;
			}
			break;
		case ORDER_FX:
			ob->ani_type = 0;
			switch (family)
			{
				case FAMILY_MAGIC_SHIELD:
					ob->stats->set_bit_flags(BIT_PHANTOM, 1);
					break;
				case FAMILY_CLOUD: // poison cloud
					ob->stats->set_bit_flags(BIT_NO_COLLIDE, 1);
					ob->stats->set_bit_flags(BIT_FLYING, 1);
					break;
				default:
					break;
			}

		default :
			break; // end of all orders
	}

	return ob;
}

// This is used for grabbing a pixieN directly, not through a walker
pixieN *loader::create_pixieN(char order, char family)
{
	pixieN *newpixie;

	if (!graphics[PIX(order, family)].valid())
	{
		Log("Alert! No valid graphics for pixieN\n");
		return NULL;
	}

	newpixie = new pixieN(graphics[PIX(order, family)], myscreen);

	return newpixie;
}


