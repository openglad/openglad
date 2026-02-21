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
#include <openglad/data/gloader.h>
#include <openglad/data/gparser.h>
#include <openglad/io/og_file.h>
#include <openglad/core/util.h>
#include <openglad/runtime/game_context.h>
#include <openglad/core/stats.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <openglad/entities/weapon_family_descriptor.h>
#include <openglad/entities/weapon_family_registry.h>
#include <openglad/entities/effect_family_descriptor.h>
#include <openglad/entities/effect_family_registry.h>
#include <openglad/entities/treasure_family_descriptor.h>
#include <openglad/entities/treasure_family_registry.h>
#include <openglad/entities/generator_family_descriptor.h>
#include <openglad/entities/generator_family_registry.h>
#include <format>
#include <openglad/entities/living.h>
#include <openglad/entities/treasure.h>
#include <openglad/entities/weap.h>
#include <openglad/entities/effect.h>
#include <algorithm>
#include <cstring>

static inline cfg_store& active_config()
{
    if(ctx().config != nullptr)
        return *ctx().config;
    return cfg;
}

void popup_dialog(const char* title, const char* message);

#define SIZE_ORDERS 7 // see graph.h
#define SIZE_FAMILIES 21  // see also NUM_FAMILIES in graph.h
//#define PIX(a,b) (SIZE_FAMILIES*a+b)  //moved to graph.h

static inline Order sanitize_order(Order order)
{
    if (order > Order::Button1)
        return Order::Living;
    return order;
}

extern float derived_bonuses[NUM_FAMILIES][8];

// These are for monsters and us
signed char bit1[] = {static_cast<char>(1),static_cast<char>(5),static_cast<char>(1),static_cast<char>(9),static_cast<signed char>(-1)};     // up
signed char bit2[] = {static_cast<char>(13),static_cast<char>(17),static_cast<char>(13),static_cast<char>(21),static_cast<signed char>(-1)}; // up-right
signed char bit3[] = {static_cast<char>(2),static_cast<char>(6),static_cast<char>(2),static_cast<char>(10),static_cast<signed char>(-1)};    // right
signed char bit4[] = {static_cast<char>(14),static_cast<char>(18),static_cast<char>(14),static_cast<char>(22),static_cast<signed char>(-1)}; // down-right
signed char bit5[] = {static_cast<char>(0),static_cast<char>(4),static_cast<char>(0),static_cast<char>(8),static_cast<signed char>(-1)};     // down
signed char bit6[] = {static_cast<char>(12),static_cast<char>(16),static_cast<char>(12),static_cast<char>(20),static_cast<signed char>(-1)}; // down-left
signed char bit7[] = {static_cast<char>(3),static_cast<char>(7),static_cast<char>(3),static_cast<char>(11),static_cast<signed char>(-1)};    // left
signed char bit8[] = {static_cast<char>(15),static_cast<char>(19),static_cast<char>(15),static_cast<char>(23),static_cast<signed char>(-1)}; // up-left

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

/*
How do animations work?
animate() sets the walker graphic to: ani[curdir+ani_type*NUM_FACINGS][cycle]
ani_type of 0 causes an effect object to last only one frame.
So any lasting animation usually has ani_type of 1, which means 'ani' needs to store at least 16 elements (NUM_FACINGS == 8).
The animation can be directional due to the use of curdir.
The signed char[] are the actual frame indices for the animation.  -1 means to end the animation.
*/

signed char hit1[] = {0, 1, -1};
signed char hit2[] = {0, 2, -1};
signed char hit3[] = {0, 3, -1};
signed char  *anihit[] = {hit1, hit1, hit1, hit1,
                                 hit1, hit1, hit1, hit1,
                                 hit1, hit1, hit1, hit1,
                                 hit1, hit1, hit1, hit1,
                                 hit2, hit2, hit2, hit2,
                                 hit2, hit2, hit2, hit2,
                                 hit3, hit3, hit3, hit3,
                                 hit3, hit3, hit3, hit3 };

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
                               nullptr, nullptr, nullptr, nullptr,                             // 24 == tele_in (ignored)
                               nullptr, nullptr, nullptr, nullptr,
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

    size_t len = d.w * d.h * d.frames;
    result.data = std::make_unique<unsigned char[]>(len);
    std::copy_n(d.data.get(), len, result.data.get());

    return result;
}


loader::loader()
    : graphics(SIZE_ORDERS*SIZE_FAMILIES),
      animations(SIZE_ORDERS*SIZE_FAMILIES, nullptr),
      stepsizes(SIZE_ORDERS*SIZE_FAMILIES, 0.0f),
      lineofsight(SIZE_ORDERS*SIZE_FAMILIES, 0),
      act_types(SIZE_ORDERS*SIZE_FAMILIES, static_cast<char>(ACT_RANDOM)),
      damage(SIZE_ORDERS*SIZE_FAMILIES, 0.0f),
      fire_frequency(SIZE_ORDERS*SIZE_FAMILIES, 0.0f)
{
	std::fill(std::begin(hitpoints), std::end(hitpoints), 0.0f);

	// Livings
	graphics[PIX(Order::Living, FAMILY_SOLDIER)] = read_pixie_file("footman.pix");
	graphics[PIX(Order::Living, FAMILY_ELF)] = read_pixie_file("elf.pix");
	graphics[PIX(Order::Living, FAMILY_ARCHER)] = read_pixie_file("archer.pix");
	graphics[PIX(Order::Living, FAMILY_THIEF)] = read_pixie_file("thief.pix");
	graphics[PIX(Order::Living, FAMILY_MAGE)] = read_pixie_file("mage.pix");
	graphics[PIX(Order::Living, FAMILY_SKELETON)] = read_pixie_file("skeleton.pix");
	graphics[PIX(Order::Living, FAMILY_CLERIC)] = read_pixie_file("cleric.pix");
	graphics[PIX(Order::Living, FAMILY_FIREELEMENTAL)] = read_pixie_file("firelem.pix");
	graphics[PIX(Order::Living, FAMILY_FAERIE)] = read_pixie_file("faerie.pix");
	graphics[PIX(Order::Living, FAMILY_SLIME)] = read_pixie_file("amoeba3.pix");
	graphics[PIX(Order::Living, FAMILY_SMALL_SLIME)] = read_pixie_file("s_slime.pix");
	graphics[PIX(Order::Living, FAMILY_MEDIUM_SLIME)] = read_pixie_file("m_slime.pix");
	graphics[PIX(Order::Living, FAMILY_GHOST)] = read_pixie_file("ghost.pix");
	graphics[PIX(Order::Living, FAMILY_DRUID)] = read_pixie_file("druid.pix");
	graphics[PIX(Order::Living, FAMILY_ORC)] = read_pixie_file("orc.pix");
	graphics[PIX(Order::Living, FAMILY_BIG_ORC)] = read_pixie_file("orc2.pix");
	graphics[PIX(Order::Living, FAMILY_BARBARIAN)] = read_pixie_file("barby.pix");
	graphics[PIX(Order::Living, FAMILY_ARCHMAGE)] = read_pixie_file("archmage.pix");
	graphics[PIX(Order::Living, FAMILY_GOLEM)] = read_pixie_file("golem1.pix");
	graphics[PIX(Order::Living, FAMILY_GIANT_SKELETON)] = read_pixie_file("gs1.pix");
	graphics[PIX(Order::Living, FAMILY_TOWER1)] = read_pixie_file("towersm1.pix");

    for(int i = 0; i < NUM_FAMILIES; i++)
    {
        hitpoints[PIX(Order::Living, i)] = derived_bonuses[i][0];
        damage[PIX(Order::Living, i)] = derived_bonuses[i][2];
        stepsizes[PIX(Order::Living, i)] = derived_bonuses[i][6];
        fire_frequency[PIX(Order::Living, i)] = derived_bonuses[i][7];
    }


	act_types[PIX(Order::Living, FAMILY_SOLDIER)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_ELF)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_ARCHER)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_THIEF)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_MAGE)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_SKELETON)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_CLERIC)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_FIREELEMENTAL)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_FAERIE)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_SLIME)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_SMALL_SLIME)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_MEDIUM_SLIME)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_GHOST)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_DRUID)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_ORC)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_BIG_ORC)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_BARBARIAN)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_ARCHMAGE)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_GOLEM)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_GIANT_SKELETON)] = ACT_RANDOM;
	act_types[PIX(Order::Living, FAMILY_TOWER1)] = ACT_RANDOM;

	animations[PIX(Order::Living, FAMILY_SOLDIER)] = animan;
	animations[PIX(Order::Living, FAMILY_ELF)] = animan;
	animations[PIX(Order::Living, FAMILY_ARCHER)] = animan;
	animations[PIX(Order::Living, FAMILY_THIEF)] = animan;
	animations[PIX(Order::Living, FAMILY_MAGE)] = animage;
	animations[PIX(Order::Living, FAMILY_SKELETON)] = aniskel;
	animations[PIX(Order::Living, FAMILY_CLERIC)] = animan;
	animations[PIX(Order::Living, FAMILY_FIREELEMENTAL)] = animan;
	animations[PIX(Order::Living, FAMILY_FAERIE)] = animan;
	animations[PIX(Order::Living, FAMILY_SLIME)] = anislime;
	animations[PIX(Order::Living, FAMILY_SMALL_SLIME)] = ani_small_slime;
	animations[PIX(Order::Living, FAMILY_MEDIUM_SLIME)] = ani_small_slime;
	animations[PIX(Order::Living, FAMILY_GHOST)] = animan;
	animations[PIX(Order::Living, FAMILY_DRUID)] = animan;
	animations[PIX(Order::Living, FAMILY_ORC)] = animan;
	animations[PIX(Order::Living, FAMILY_BIG_ORC)] = animan;
	animations[PIX(Order::Living, FAMILY_BARBARIAN)] = animan;
	animations[PIX(Order::Living, FAMILY_ARCHMAGE)] = animage;
	animations[PIX(Order::Living, FAMILY_GOLEM)] = animan;
	animations[PIX(Order::Living, FAMILY_GIANT_SKELETON)] = anigs;
	animations[PIX(Order::Living, FAMILY_TOWER1)] = anifood;

    // AI's understanding of how much range its ranged attack has so it will try to shoot.
	lineofsight[PIX(Order::Living, FAMILY_SOLDIER)] = 7;
	lineofsight[PIX(Order::Living, FAMILY_ELF)] = 8;
	lineofsight[PIX(Order::Living, FAMILY_ARCHER)] = 12;
	lineofsight[PIX(Order::Living, FAMILY_THIEF)] = 10;
	lineofsight[PIX(Order::Living, FAMILY_MAGE)] = 7;
	lineofsight[PIX(Order::Living, FAMILY_SKELETON)] = 7;
	lineofsight[PIX(Order::Living, FAMILY_CLERIC)] = 4;
	lineofsight[PIX(Order::Living, FAMILY_FIREELEMENTAL)] = 10;
	lineofsight[PIX(Order::Living, FAMILY_FAERIE)] = 8;
	lineofsight[PIX(Order::Living, FAMILY_SLIME)] = 4;
	lineofsight[PIX(Order::Living, FAMILY_SMALL_SLIME)] = 2;
	lineofsight[PIX(Order::Living, FAMILY_MEDIUM_SLIME)] = 3;
	lineofsight[PIX(Order::Living, FAMILY_GHOST)] = 12;
	lineofsight[PIX(Order::Living, FAMILY_DRUID)] = 10;
	lineofsight[PIX(Order::Living, FAMILY_ORC)] = 20;
	lineofsight[PIX(Order::Living, FAMILY_BIG_ORC)] = 25;
	lineofsight[PIX(Order::Living, FAMILY_BARBARIAN)] = 12;
	lineofsight[PIX(Order::Living, FAMILY_ARCHMAGE)] = 10;
	lineofsight[PIX(Order::Living, FAMILY_GOLEM)] = 20;
	lineofsight[PIX(Order::Living, FAMILY_GIANT_SKELETON)] = 20;
	lineofsight[PIX(Order::Living, FAMILY_TOWER1)] = 10;

	// Weapons
	graphics[PIX(Order::Weapon, FAMILY_KNIFE)] = read_pixie_file("knife.pix");
	graphics[PIX(Order::Weapon, FAMILY_ROCK)] = read_pixie_file("rock.pix");
	graphics[PIX(Order::Weapon, FAMILY_ARROW)] = read_pixie_file("arrow.pix");
	graphics[PIX(Order::Weapon, FAMILY_FIRE_ARROW)] = read_pixie_file("farrow.pix");
	graphics[PIX(Order::Weapon, FAMILY_FIREBALL)] = read_pixie_file("fire.pix");
	graphics[PIX(Order::Weapon, FAMILY_TREE)] = read_pixie_file("tree.pix");
	graphics[PIX(Order::Weapon, FAMILY_METEOR)] = read_pixie_file("meteor.pix");
	graphics[PIX(Order::Weapon, FAMILY_SPRINKLE)] = read_pixie_file("sparkle.pix");
	
	if(active_config().is_on("effects", "gore"))
    {
        graphics[PIX(Order::Weapon, FAMILY_BLOOD)] = read_pixie_file("blood.pix");
        graphics[PIX(Order::Treasure,FAMILY_STAIN)] = read_pixie_file("stain.pix");
    }
	else
    {
        graphics[PIX(Order::Weapon, FAMILY_BLOOD)] = read_pixie_file("blood_friendly.pix");
        graphics[PIX(Order::Treasure,FAMILY_STAIN)] = read_pixie_file("stain_friendly.pix");
    }
        
	graphics[PIX(Order::Weapon, FAMILY_BONE)] = read_pixie_file("bone1.pix");
	graphics[PIX(Order::Weapon, FAMILY_BLOB)] = read_pixie_file("sl_ball.pix");
	graphics[PIX(Order::Weapon, FAMILY_LIGHTNING)] = read_pixie_file("lightnin.pix");
	graphics[PIX(Order::Weapon, FAMILY_GLOW)] = read_pixie_file("clerglow.pix");
	graphics[PIX(Order::Weapon, FAMILY_WAVE)] = read_pixie_file("wave.pix");
	graphics[PIX(Order::Weapon, FAMILY_WAVE2)] = read_pixie_file("wave2.pix");
	graphics[PIX(Order::Weapon, FAMILY_WAVE3)] = read_pixie_file("wave3.pix");
	graphics[PIX(Order::Weapon, FAMILY_CIRCLE_PROTECTION)] = read_pixie_file("wave2.pix");
	graphics[PIX(Order::Weapon, FAMILY_HAMMER)] = read_pixie_file("hammer.pix");
	
	graphics[PIX(Order::Weapon, FAMILY_DOOR)] = read_pixie_file("door.pix");
	graphics[PIX(Order::Weapon, FAMILY_BOULDER)] = read_pixie_file("boulder1.pix");

	hitpoints[PIX(Order::Weapon, FAMILY_KNIFE)] = 6;
	hitpoints[PIX(Order::Weapon, FAMILY_BONE)] = 5;
	hitpoints[PIX(Order::Weapon, FAMILY_ROCK)] = 4;
	hitpoints[PIX(Order::Weapon, FAMILY_ARROW)] = 5;
	hitpoints[PIX(Order::Weapon, FAMILY_FIRE_ARROW)] = 7;
	hitpoints[PIX(Order::Weapon, FAMILY_FIREBALL)] = 8;
	hitpoints[PIX(Order::Weapon, FAMILY_TREE)] = 50;
	hitpoints[PIX(Order::Weapon, FAMILY_METEOR)] = 12;
	hitpoints[PIX(Order::Weapon, FAMILY_SPRINKLE)] = 1;
	hitpoints[PIX(Order::Weapon, FAMILY_BLOB)] = 1;
	hitpoints[PIX(Order::Weapon, FAMILY_LIGHTNING)] = 60;
	hitpoints[PIX(Order::Weapon, FAMILY_GLOW)] = 50;
	hitpoints[PIX(Order::Weapon, FAMILY_WAVE)] = 50;
	hitpoints[PIX(Order::Weapon, FAMILY_WAVE2)] = 50;
	hitpoints[PIX(Order::Weapon, FAMILY_WAVE3)] = 50;
	hitpoints[PIX(Order::Weapon, FAMILY_CIRCLE_PROTECTION)] = 50;
	hitpoints[PIX(Order::Weapon, FAMILY_HAMMER)] = 10;
	hitpoints[PIX(Order::Weapon, FAMILY_DOOR)] = 5000;
	hitpoints[PIX(Order::Weapon, FAMILY_BOULDER)] = 50;

	act_types[PIX(Order::Weapon, FAMILY_KNIFE)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_BONE)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_ROCK)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_ARROW)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_FIRE_ARROW)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_FIREBALL)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_TREE)] = ACT_SIT;
	act_types[PIX(Order::Weapon, FAMILY_METEOR)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_SPRINKLE)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_BLOOD)] = ACT_DIE;
	act_types[PIX(Order::Weapon, FAMILY_BLOB)] = ACT_FIRE;
	act_types[PIX(Order::Treasure,     FAMILY_STAIN)] = ACT_CONTROL;
	act_types[PIX(Order::Weapon, FAMILY_LIGHTNING)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_GLOW)] = ACT_SIT;
	act_types[PIX(Order::Weapon, FAMILY_WAVE)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_WAVE3)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_WAVE3)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_CIRCLE_PROTECTION)] = ACT_SIT;
	act_types[PIX(Order::Weapon, FAMILY_HAMMER)] = ACT_FIRE;
	act_types[PIX(Order::Weapon, FAMILY_DOOR)] = ACT_SIT;
	act_types[PIX(Order::Weapon, FAMILY_BOULDER)] = ACT_FIRE;

	animations[PIX(Order::Weapon, FAMILY_KNIFE)] = anikni;
	animations[PIX(Order::Weapon, FAMILY_BONE)] = anikni;
	animations[PIX(Order::Weapon, FAMILY_ROCK)] = anirock;
	animations[PIX(Order::Weapon, FAMILY_ARROW)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_FIRE_ARROW)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_FIREBALL)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_TREE)] = anitree;
	animations[PIX(Order::Weapon, FAMILY_METEOR)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_SPRINKLE)] = anikni;
	animations[PIX(Order::Weapon, FAMILY_BLOOD)] = aniblood;
	animations[PIX(Order::Weapon, FAMILY_BLOB)] = aniblob1;
	animations[PIX(Order::Treasure,     FAMILY_STAIN)] = aniblood;
	animations[PIX(Order::Weapon, FAMILY_LIGHTNING)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_GLOW)] = aniglowgrow;
	animations[PIX(Order::Weapon, FAMILY_WAVE)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_WAVE2)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_WAVE3)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_CIRCLE_PROTECTION)] = anifood;
	animations[PIX(Order::Weapon, FAMILY_HAMMER)] = aniarrow;
	animations[PIX(Order::Weapon, FAMILY_DOOR)] = anidoor;
	animations[PIX(Order::Weapon, FAMILY_BOULDER)] = aninone;

	stepsizes[PIX(Order::Weapon, FAMILY_KNIFE)] = 5;
	stepsizes[PIX(Order::Weapon, FAMILY_BONE)] = 6;
	stepsizes[PIX(Order::Weapon, FAMILY_ROCK)] = 5;
	stepsizes[PIX(Order::Weapon, FAMILY_ARROW)] = 8;
	stepsizes[PIX(Order::Weapon, FAMILY_FIRE_ARROW)] = 8;
	stepsizes[PIX(Order::Weapon, FAMILY_FIREBALL)] = 6;
	stepsizes[PIX(Order::Weapon, FAMILY_TREE)] = 0;
	stepsizes[PIX(Order::Weapon, FAMILY_METEOR)] = 7;
	stepsizes[PIX(Order::Weapon, FAMILY_SPRINKLE)] = 6;
	stepsizes[PIX(Order::Weapon, FAMILY_BLOOD)] = 0;
	stepsizes[PIX(Order::Weapon, FAMILY_BLOB)] = 2;
	stepsizes[PIX(Order::Treasure,     FAMILY_STAIN)] = 0;
	stepsizes[PIX(Order::Weapon, FAMILY_LIGHTNING)] = 9;
	stepsizes[PIX(Order::Weapon, FAMILY_GLOW)] = 0;
	stepsizes[PIX(Order::Weapon, FAMILY_WAVE)] = 6;
	stepsizes[PIX(Order::Weapon, FAMILY_WAVE2)] = 4;
	stepsizes[PIX(Order::Weapon, FAMILY_WAVE3)] = 3;
	stepsizes[PIX(Order::Weapon, FAMILY_CIRCLE_PROTECTION)] = 1;
	stepsizes[PIX(Order::Weapon, FAMILY_HAMMER)] = 6;
	stepsizes[PIX(Order::Weapon, FAMILY_DOOR)] = 0;
	stepsizes[PIX(Order::Weapon, FAMILY_BOULDER)] = 10;

	// Acts as weapon's range (pixel range == lineofsight * stepsize)
	lineofsight[PIX(Order::Weapon, FAMILY_KNIFE)] = 7;
	lineofsight[PIX(Order::Weapon, FAMILY_BONE)] = 6;
	lineofsight[PIX(Order::Weapon, FAMILY_ROCK)] = 8;
	lineofsight[PIX(Order::Weapon, FAMILY_ARROW)] = 12;
	lineofsight[PIX(Order::Weapon, FAMILY_FIRE_ARROW)] = 12;
	lineofsight[PIX(Order::Weapon, FAMILY_FIREBALL)] = 7;
	lineofsight[PIX(Order::Weapon, FAMILY_TREE)] = 1;
	lineofsight[PIX(Order::Weapon, FAMILY_METEOR)] = 9;
	lineofsight[PIX(Order::Weapon, FAMILY_SPRINKLE)] = 10;
	lineofsight[PIX(Order::Weapon, FAMILY_BLOB)] = 11;
	lineofsight[PIX(Order::Weapon, FAMILY_BLOOD)] = 1;
	lineofsight[PIX(Order::Weapon, FAMILY_LIGHTNING)] = 13;
	lineofsight[PIX(Order::Weapon, FAMILY_GLOW)] = 1;
	lineofsight[PIX(Order::Weapon, FAMILY_WAVE)] = 3;
	lineofsight[PIX(Order::Weapon, FAMILY_WAVE2)] = 4;
	lineofsight[PIX(Order::Weapon, FAMILY_WAVE3)] = 6;
	lineofsight[PIX(Order::Weapon, FAMILY_CIRCLE_PROTECTION)] = 110;
	lineofsight[PIX(Order::Weapon, FAMILY_HAMMER)] = 4;
	lineofsight[PIX(Order::Weapon, FAMILY_DOOR)] = 1;
	lineofsight[PIX(Order::Weapon, FAMILY_BOULDER)] = 9;

	// Strength of weapon
	damage[PIX(Order::Weapon, FAMILY_KNIFE)] = 6;
	damage[PIX(Order::Weapon, FAMILY_BONE)] = 5;
	damage[PIX(Order::Weapon, FAMILY_ROCK)] = 4;
	damage[PIX(Order::Weapon, FAMILY_ARROW)] = 5;
	damage[PIX(Order::Weapon, FAMILY_FIRE_ARROW)] = 7;
	damage[PIX(Order::Weapon, FAMILY_FIREBALL)] = 10;
	damage[PIX(Order::Weapon, FAMILY_TREE)] = 0;
	damage[PIX(Order::Weapon, FAMILY_METEOR)] = 12;
	damage[PIX(Order::Weapon, FAMILY_SPRINKLE)] = 1;
	damage[PIX(Order::Weapon, FAMILY_BLOB)] = 1;
	damage[PIX(Order::Weapon, FAMILY_BLOOD)] = 0;
	damage[PIX(Order::Weapon, FAMILY_LIGHTNING)] = 6;
	damage[PIX(Order::Weapon, FAMILY_GLOW)] = 0;
	damage[PIX(Order::Weapon, FAMILY_WAVE)] = 16;
	damage[PIX(Order::Weapon, FAMILY_WAVE2)] = 12;
	damage[PIX(Order::Weapon, FAMILY_WAVE3)] = 10;
	damage[PIX(Order::Weapon, FAMILY_CIRCLE_PROTECTION)] = 0;
	damage[PIX(Order::Weapon, FAMILY_HAMMER)] = 9;
	damage[PIX(Order::Weapon, FAMILY_DOOR)] = 0;
	damage[PIX(Order::Weapon, FAMILY_BOULDER)] = 25;

	fire_frequency[PIX(Order::Weapon, FAMILY_KNIFE)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_BONE)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_ROCK)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_ARROW)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_FIRE_ARROW)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_FIREBALL)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_TREE)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_METEOR)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_SPRINKLE)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_BLOB)] = 2;
	fire_frequency[PIX(Order::Weapon, FAMILY_BLOOD)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_LIGHTNING)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_GLOW)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_WAVE)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_WAVE2)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_WAVE3)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_HAMMER)] = 0;
	fire_frequency[PIX(Order::Weapon, FAMILY_BOULDER)] = 0;

	// Treasure items (food, etc.)
	graphics[PIX(Order::Treasure, FAMILY_DRUMSTICK)] = read_pixie_file("food1.pix");
	graphics[PIX(Order::Treasure, FAMILY_GOLD_BAR)] = read_pixie_file("bar1.pix");
	graphics[PIX(Order::Treasure, FAMILY_SILVER_BAR)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_GOLD_BAR)]);
	graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)] = read_pixie_file("bottle.pix");
	graphics[PIX(Order::Treasure, FAMILY_INVIS_POTION)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)]);
	graphics[PIX(Order::Treasure, FAMILY_INVULNERABLE_POTION)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)]);
	graphics[PIX(Order::Treasure, FAMILY_FLIGHT_POTION)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)]);
	graphics[PIX(Order::Treasure, FAMILY_EXIT)] = read_pixie_file("16exit1.pix");
	graphics[PIX(Order::Treasure, FAMILY_TELEPORTER)] = read_pixie_file("teleport.pix");
	graphics[PIX(Order::Treasure, FAMILY_LIFE_GEM)] = read_pixie_file("lifegem.pix");
	graphics[PIX(Order::Treasure, FAMILY_KEY)] = read_pixie_file("key.pix");
	graphics[PIX(Order::Treasure, FAMILY_SPEED_POTION)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)]);

	hitpoints[PIX(Order::Treasure, FAMILY_DRUMSTICK)] = 10;
	hitpoints[PIX(Order::Treasure, FAMILY_GOLD_BAR)] = 1000;
	hitpoints[PIX(Order::Treasure, FAMILY_SILVER_BAR)] = 100;

	act_types[PIX(Order::Treasure, FAMILY_DRUMSTICK)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_GOLD_BAR)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_SILVER_BAR)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_MAGIC_POTION)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_INVIS_POTION)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_INVULNERABLE_POTION)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_FLIGHT_POTION)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_EXIT)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_TELEPORTER)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_LIFE_GEM)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_KEY)] = ACT_CONTROL;
	act_types[PIX(Order::Treasure, FAMILY_SPEED_POTION)] = ACT_CONTROL;

	animations[PIX(Order::Treasure, FAMILY_DRUMSTICK)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_GOLD_BAR)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_SILVER_BAR)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_MAGIC_POTION)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_INVIS_POTION)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_INVULNERABLE_POTION)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_FLIGHT_POTION)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_EXIT)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_TELEPORTER)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_LIFE_GEM)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_KEY)] = anifood;
	animations[PIX(Order::Treasure, FAMILY_SPEED_POTION)] = anifood;

	stepsizes[PIX(Order::Treasure, FAMILY_DRUMSTICK)] = 5;

	// Generator
	graphics[PIX(Order::Generator, FAMILY_TENT)] = read_pixie_file("tent.pix");
	graphics[PIX(Order::Generator, FAMILY_TOWER)] = read_pixie_file("tower4.pix");
	graphics[PIX(Order::Generator, FAMILY_BONES)] = read_pixie_file("bonepile.pix");
	graphics[PIX(Order::Generator, FAMILY_TREEHOUSE)] = read_pixie_file("bigtree.pix");
	hitpoints[PIX(Order::Generator, FAMILY_TENT)] = 100;

	act_types[PIX(Order::Generator, FAMILY_TENT)] = ACT_GENERATE;
	act_types[PIX(Order::Generator, FAMILY_TOWER)] = ACT_GENERATE;
	act_types[PIX(Order::Generator, FAMILY_BONES)] = ACT_GENERATE;
	act_types[PIX(Order::Generator, FAMILY_TREEHOUSE)] = ACT_GENERATE;

	animations[PIX(Order::Generator, FAMILY_TENT)] = anitent;
	animations[PIX(Order::Generator, FAMILY_TOWER)] = anitower;
	animations[PIX(Order::Generator, FAMILY_BONES)] = aninone;
	animations[PIX(Order::Generator, FAMILY_TREEHOUSE)] = aninone;

	stepsizes[PIX(Order::Generator, FAMILY_TENT)] = 0;
	stepsizes[PIX(Order::Generator, FAMILY_TOWER)] = 0;
	stepsizes[PIX(Order::Generator, FAMILY_BONES)] = 0;
	stepsizes[PIX(Order::Generator, FAMILY_TREEHOUSE)] = 0;

	lineofsight[PIX(Order::Generator, FAMILY_TENT)] = 0;
	lineofsight[PIX(Order::Generator, FAMILY_TOWER)] = 0;
	lineofsight[PIX(Order::Generator, FAMILY_BONES)] = 0;
	lineofsight[PIX(Order::Generator, FAMILY_TREEHOUSE)] = 0;

	damage[PIX(Order::Generator, FAMILY_TENT)] = 0;
	damage[PIX(Order::Generator, FAMILY_TOWER)] = 0;
	damage[PIX(Order::Generator, FAMILY_BONES)] = 2;
	damage[PIX(Order::Generator, FAMILY_TREEHOUSE)] = 0;

	fire_frequency[PIX(Order::Generator, FAMILY_TENT)] = 0;
	fire_frequency[PIX(Order::Generator, FAMILY_TOWER)] = 0;
	fire_frequency[PIX(Order::Generator, FAMILY_BONES)] = 0;
	fire_frequency[PIX(Order::Generator, FAMILY_TREEHOUSE)] = 0;

	// Specials ..
	graphics[PIX(Order::Special, FAMILY_RESERVED_TEAM)] = read_pixie_file("team.pix");

	// Effects ..
	graphics[PIX(Order::FX, FAMILY_EXPAND)] = read_pixie_file("expand8.pix");
	graphics[PIX(Order::FX, FAMILY_GHOST_SCARE)]  = read_pixie_file("expand8.pix");
	graphics[PIX(Order::FX, FAMILY_BOMB)]  = read_pixie_file("bomb1.pix");
	graphics[PIX(Order::FX, FAMILY_EXPLOSION)]  = read_pixie_file("boom1.pix");
	graphics[PIX(Order::FX, FAMILY_FLASH)]  = read_pixie_file("telflash.pix");
	graphics[PIX(Order::FX, FAMILY_MAGIC_SHIELD)] = read_pixie_file("mshield.pix");
	graphics[PIX(Order::FX, FAMILY_KNIFE_BACK)] = read_pixie_file("knife.pix");
	graphics[PIX(Order::FX, FAMILY_CLOUD)] = read_pixie_file("cloud.pix");
	graphics[PIX(Order::FX, FAMILY_MARKER)] = read_pixie_file("marker.pix");
	graphics[PIX(Order::FX, FAMILY_BOOMERANG)] = read_pixie_file("boomer.pix");
	graphics[PIX(Order::FX, FAMILY_CHAIN)] = read_pixie_file("lightnin.pix");
	graphics[PIX(Order::FX, FAMILY_DOOR_OPEN)] = read_pixie_file("door.pix");
	graphics[PIX(Order::FX, FAMILY_HIT)] = read_pixie_file("hit.pix");

	animations[PIX(Order::FX, FAMILY_EXPAND)] = aniexpand8;
	animations[PIX(Order::FX, FAMILY_GHOST_SCARE)] = aniexpand8;
	animations[PIX(Order::FX, FAMILY_BOMB)] = anibomb1;
	animations[PIX(Order::FX, FAMILY_EXPLOSION)] = aniexplosion1;
	animations[PIX(Order::FX, FAMILY_FLASH)] = aniexpand8;
	animations[PIX(Order::FX, FAMILY_MAGIC_SHIELD)] = anikni;
	animations[PIX(Order::FX, FAMILY_KNIFE_BACK)] = anikni;
	animations[PIX(Order::FX, FAMILY_BOOMERANG)] = ani16;
	animations[PIX(Order::FX, FAMILY_CLOUD)] = anicloud;
	animations[PIX(Order::FX, FAMILY_MARKER)] = animarker;
	animations[PIX(Order::FX, FAMILY_CHAIN)] = aniarrow;
	animations[PIX(Order::FX, FAMILY_DOOR_OPEN)] = anidooropen;
	animations[PIX(Order::FX, FAMILY_HIT)] = anihit;

	stepsizes[PIX(Order::FX, FAMILY_CLOUD)] = 4;
	stepsizes[PIX(Order::FX, FAMILY_CHAIN)] = 12;  // REALLY fast!

	lineofsight[PIX(Order::FX, FAMILY_CHAIN)] = 15;

	hitpoints[PIX(Order::FX, FAMILY_MAGIC_SHIELD)] = 100;
	hitpoints[PIX(Order::FX, FAMILY_BOOMERANG)] = 50;

	damage[PIX(Order::FX, FAMILY_MAGIC_SHIELD)] = 10;
	damage[PIX(Order::FX, FAMILY_BOOMERANG)] = 8;
	damage[PIX(Order::FX, FAMILY_CLOUD)] = 20;

	// These are button graphics ..
	graphics[PIX(Order::Button1, FAMILY_NORMAL1)] = read_pixie_file("normal1.pix");
	graphics[PIX(Order::Button1, FAMILY_PLUS)] = read_pixie_file("butplus.pix");
	graphics[PIX(Order::Button1, FAMILY_MINUS)] = read_pixie_file("butminus.pix");
	graphics[PIX(Order::Button1, FAMILY_WRENCH)] = read_pixie_file("wrench.pix");

}

loader::loader(LevelData* owner)
    : loader()
{
    owner_level = owner;
}

loader::~loader(void)
{
	for(int i=0;i<(SIZE_ORDERS*SIZE_FAMILIES);i++) {
	    graphics[i].free();
	}
	// vectors clean up automatically
}

void loader::set_derived_stats(walker* w, Order order, std::int32_t family)
{
    order = sanitize_order(order);
	if(family < 0 || family >= NUM_FAMILIES)
		family = 0;

	w->stepsize = stepsizes[PIX(order, family)];
	w->normal_stepsize = w->stepsize;
	w->lineofsight = lineofsight[PIX(order, family)];
	w->damage = damage[PIX(order, family)];
	w->fire_frequency = fire_frequency[PIX(order, family)];
}

std::unique_ptr<walker> loader::create_walker_owned(Order order,
                                                    std::int32_t family)
{
	std::unique_ptr<walker> ob;
    order = sanitize_order(order);

	if(family < 0 || family >= NUM_FAMILIES)
	{
		// Keep the legacy "bad living family" fallback to soldier; others clamp to 0.
		family = (order == Order::Living) ? FAMILY_SOLDIER : 0;
	}

	if (!graphics[PIX(order, family)].valid())
	{
	    std::string buf = std::format("No valid graphics for walker!\nOrder: {}, Family {}\nPlease report this to the developer!", static_cast<int>(order), static_cast<int>(family));
		popup_dialog("ERROR", buf.c_str());
		return nullptr;
	}

	// Pass PixieData to constructors. In SDL builds, the constructor calls
	// attach_render() which creates a PixieNWalkerRender. In headless builds,
	// attach_render() is a no-op so no render component is created.
	const auto& pix = graphics[PIX(order, family)];

	if (order == Order::Living)
		ob = std::make_unique<living>(pix);
	else if (order == Order::Weapon)
	    ob = std::make_unique<weap>(pix);
	else if (order == Order::Treasure)
		ob = std::make_unique<treasure>(pix);
	else if (order == Order::FX)
		ob = std::make_unique<effect>(pix);
	else
		ob = std::make_unique<walker>(pix);
	if (!ob)
		return nullptr;

	// Always set size from PixieData (needed for collision in both render and headless)
	ob->sizex = pix.w;
	ob->sizey = pix.h;

	ob->stats()->hitpoints = hitpoints[PIX(order, family)];
	ob->stats()->max_hitpoints = hitpoints[PIX(order, family)];
	ob->stats()->special_cost[0] = 0; // shouldn't be used
	ob->stats()->weapon_cost = 1; // default value

	// Wire sim context from owning LevelData if available.
	if (owner_level)
		owner_level->wire_entity(ob.get());

	set_walker(ob.get(), order, family);

	if(order == Order::Living && ob->ani)
        ob->set_frame(ob->ani[ob->curdir][0]);
	return ob;
}

walker  *loader::set_walker(walker *ob,
                            Order order,
                            std::int32_t family)
{
	short i;
    order = sanitize_order(order);

	if(family < 0 || family >= NUM_FAMILIES)
		family = 0;

	ob->set_order_family(order, static_cast<char>(family));
	ob->set_act_type(act_types[PIX(order, family)]);
	ob->ani = animations[PIX(order, family)];
	
	set_derived_stats(ob, order, family);

	for (i=0; i < NUM_SPECIALS; i++)
		ob->stats()->special_cost[i] = 5000;

	// For special settings
	switch (order)
	{
		case Order::Living:
		{
			auto* fd = get_family_descriptor(family);
			if (fd)
			{
				for (i = 0; i < NUM_SPECIALS; i++)
					ob->stats()->special_cost[i] = fd->special_cost[i];
				ob->stats()->weapon_cost = fd->weapon_cost;
				ob->default_weapon = static_cast<unsigned short>(fd->default_weapon);
				if (fd->init_ani_type != 0)
					ob->ani_type = fd->init_ani_type;
				if (fd->init_max_magicpoints > 0)
					ob->stats()->max_magicpoints = fd->init_max_magicpoints;
				// Set bit flags from descriptor
				if (fd->init_bit_flags & BIT_ANIMATE)
					ob->stats()->set_bit_flags(BIT_ANIMATE, 1);
				if (fd->init_bit_flags & BIT_FLYING)
					ob->stats()->set_bit_flags(BIT_FLYING, 1);
				if (fd->init_bit_flags & BIT_FORESTWALK)
					ob->stats()->set_bit_flags(BIT_FORESTWALK, 1);
				if (fd->init_bit_flags & BIT_ETHEREAL)
					ob->stats()->set_bit_flags(BIT_ETHEREAL, 1);
				if (fd->init_bit_flags & BIT_NO_RANGED)
					ob->stats()->set_bit_flags(BIT_NO_RANGED, 1);
			}
			else
			{
				ob->transform_to(Order::Living, FAMILY_SOLDIER);
				return ob;
			}
			ob->current_weapon = ob->default_weapon;
			break; // end of livings
		}
			case Order::Weapon:
			{
				const auto* wfd = get_weapon_family_descriptor(family);
				if (wfd)
				{
					if (wfd->init_bit_flags & BIT_FORESTWALK)
						ob->stats()->set_bit_flags(BIT_FORESTWALK, 1);
					if (wfd->init_bit_flags & BIT_MAGICAL)
						ob->stats()->set_bit_flags(BIT_MAGICAL, 1);
					if (wfd->init_bit_flags & BIT_FLYING)
						ob->stats()->set_bit_flags(BIT_FLYING, 1);
					if (wfd->init_bit_flags & BIT_IMMORTAL)
						ob->stats()->set_bit_flags(BIT_IMMORTAL, 1);
					if (wfd->init_bit_flags & BIT_NO_COLLIDE)
						ob->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
					if (wfd->init_bit_flags & BIT_PHANTOM)
						ob->stats()->set_bit_flags(BIT_PHANTOM, 1);
					if (wfd->init_lifetime != 0)
						ob->lifetime = wfd->init_lifetime;
					if (wfd->init_ani_type != 0)
						ob->ani_type = wfd->init_ani_type;
				}
			}  // end of weapons
			break;
			case Order::Treasure:
			{
				const auto* tfd = get_treasure_family_descriptor(family);
				if (tfd)
				{
					if (tfd->init_ignore)
						ob->ignore = 1;
					if (tfd->init_frame >= 0)
						ob->set_direct_frame(tfd->init_frame);
				}
			}
			break;
			case Order::Generator:
			{
				const auto* gfd = get_generator_family_descriptor(family);
				ob->stats()->weapon_cost = 0;
				ob->default_weapon = static_cast<unsigned short>(gfd ? gfd->default_weapon : FAMILY_SKELETON);
			}
			break;
		case Order::FX:
			ob->ani_type = 0;
			{
				const auto* efd = get_effect_family_descriptor(family);
				if (efd && efd->init_bit_flags)
				{
					if (efd->init_bit_flags & BIT_PHANTOM)
						ob->stats()->set_bit_flags(BIT_PHANTOM, 1);
					if (efd->init_bit_flags & BIT_NO_COLLIDE)
						ob->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
					if (efd->init_bit_flags & BIT_FLYING)
						ob->stats()->set_bit_flags(BIT_FLYING, 1);
				}
			}

		default :
			break; // end of all orders
	}

	return ob;
}
