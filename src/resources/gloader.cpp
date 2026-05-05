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
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/og_file.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/weapon_family_descriptor.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/effect_family_descriptor.h>
#include <openglad/gameplay/treasure_family_descriptor.h>
#include <openglad/gameplay/generator_family_descriptor.h>
#include <format>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/effect.h>
#include <algorithm>
#include <cstring>

#define SIZE_ORDERS 7 // see constants.h
#define SIZE_FAMILIES 21  // see also NUM_FAMILIES in constants.h

static inline Order sanitize_order(Order order)
{
    if (order > Order::Button1)
        return Order::Living;
    return order;
}

// These are for monsters and us
const signed char bit1[] = {static_cast<char>(1),static_cast<char>(5),static_cast<char>(1),static_cast<char>(9),static_cast<signed char>(-1)};     // up
const signed char bit2[] = {static_cast<char>(13),static_cast<char>(17),static_cast<char>(13),static_cast<char>(21),static_cast<signed char>(-1)}; // up-right
const signed char bit3[] = {static_cast<char>(2),static_cast<char>(6),static_cast<char>(2),static_cast<char>(10),static_cast<signed char>(-1)};    // right
const signed char bit4[] = {static_cast<char>(14),static_cast<char>(18),static_cast<char>(14),static_cast<char>(22),static_cast<signed char>(-1)}; // down-right
const signed char bit5[] = {static_cast<char>(0),static_cast<char>(4),static_cast<char>(0),static_cast<char>(8),static_cast<signed char>(-1)};     // down
const signed char bit6[] = {static_cast<char>(12),static_cast<char>(16),static_cast<char>(12),static_cast<char>(20),static_cast<signed char>(-1)}; // down-left
const signed char bit7[] = {static_cast<char>(3),static_cast<char>(7),static_cast<char>(3),static_cast<char>(11),static_cast<signed char>(-1)};    // left
const signed char bit8[] = {static_cast<char>(15),static_cast<char>(19),static_cast<char>(15),static_cast<char>(23),static_cast<signed char>(-1)}; // up-left

const signed char att1[] = {1,5,1,-1};       // up
const signed char att2[] = {13,17,13,-1};    // up-right
const signed char att3[] = {2,6,2,-1};       // right
const signed char att4[] = {14,18,14,-1};    // down-right
const signed char att5[] = {0,4,0,-1};       // down
const signed char att6[] = {12,16,12,-1};    // down-left
const signed char att7[] = {3,7,3,-1};       // left
const signed char att8[] = {15,19,15,-1};    // up-left

const signed char bitm2[] = {21,25,21,29,-1};  // up-right
const signed char bitm4[] = {22,26,22,30,-1};  // down-right
const signed char bitm6[] = {20,24,20,28,-1};  // down-left
const signed char bitm8[] = {23,27,23,31,-1};  // up-left

const signed char mageatt1[] = {5,17,1,-1};    // up
const signed char mageatt2[] = {25,33,21,-1};  // up-right
const signed char mageatt3[] = {6,18,2,-1};    // right
const signed char mageatt4[] = {26,34,22,-1};  // down-right
const signed char mageatt5[] = {4,16,0,-1};    // down
const signed char mageatt6[] = {24,32,20,-1};  // down-left
const signed char mageatt7[] = {7,19,3,-1};    // left
const signed char mageatt8[] = {27,35,23,-1};  // up-left


const signed char tele_out1[] = {12,13,14,15,-1};
const signed char tele_in1[] = {15,14,13,12,1,-1};  // up
const signed char tele_in2[] = {15,14,13,12,2,-1};  // right
const signed char tele_in3[] = {15,14,13,12,0,-1};  // down
const signed char tele_in4[] = {15,14,13,12,3,-1};  // left

// Big skeleton, who is currently different ...
const signed char gs_down[] = {0, 1, 2, 3, -1}; // true "down"
const signed char gs_up[] = {3, 2, 1, 0, -1}; // faked up :)

// Skeleton growing
const signed char skel_grow[] =   {27, 26, 25, 24, 0, -1};
const signed char skel_shrink[] = {0, 24, 25, 26, 27, -1};

// For slime unidirectional movement
const signed char slime_pulse[] = { 0, 0, 1, 1, 2, 2, 1, 1, -1 };

const signed char slime_split[] = { 8, 8, 9, 9, 10, 10,
                              11,11,12,12, 13, 13, -1 };

const signed char small_slime[] = { 0, 0, 1, 1, 2, 2, 3, 3,
                              4, 4, 5, 5, 6, 6, 7, 7,
                              6, 6, 5, 5, 4 ,4, 3, 3,
                              2, 2, 1, 1, -1 };

// These are for the 'effect' objects
const signed char series_8[] = {0, 1, 2, 3, 4, 5, 6, 7, -1};
const signed char * const aniexpand8[] = { series_8, series_8, series_8, series_8,
                               series_8, series_8, series_8, series_8,
                               series_8, series_8, series_8, series_8,
                               series_8, series_8, series_8, series_8 };

//signed char series_16[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1};
const signed char series_16[] = {0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, -1};
const signed char * const ani16[] = {series_16, series_16, series_16, series_16,
                        series_16, series_16, series_16, series_16,
                        series_16, series_16, series_16, series_16,
                        series_16, series_16, series_16, series_16};

const signed char bomb1[] = {0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3,
                       4, 5, 4, 5, 4, 5, 4, 5, 6, 7, 6, 7, 6, 7, 6, 7,
                       8, 9, 8, 9, 8, 9, 8, 9, 10, 11, 10, 11, 10, 11, 10, 11,
                       12, 12, -1};
const signed char * const anibomb1[] = {bomb1, bomb1, bomb1, bomb1,
                            bomb1, bomb1, bomb1, bomb1,
                            bomb1, bomb1, bomb1, bomb1,
                            bomb1, bomb1, bomb1, bomb1 };

const signed char explosion1[] = {0, 1, 2, -1};
const signed char * const aniexplosion1[] = {explosion1, explosion1, explosion1, explosion1,
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

const signed char hit1[] = {0, 1, -1};
const signed char hit2[] = {0, 2, -1};
const signed char hit3[] = {0, 3, -1};
const signed char * const anihit[] = {hit1, hit1, hit1, hit1,
                                 hit1, hit1, hit1, hit1,
                                 hit1, hit1, hit1, hit1,
                                 hit1, hit1, hit1, hit1,
                                 hit2, hit2, hit2, hit2,
                                 hit2, hit2, hit2, hit2,
                                 hit3, hit3, hit3, hit3,
                                 hit3, hit3, hit3, hit3 };

const signed char cloud_cycle[] = {0, 1, 2, 3, 2, 1, -1};
const signed char * const anicloud[] = {cloud_cycle, cloud_cycle, cloud_cycle, cloud_cycle,
                           cloud_cycle, cloud_cycle, cloud_cycle, cloud_cycle,
                           cloud_cycle, cloud_cycle, cloud_cycle, cloud_cycle,
                           cloud_cycle, cloud_cycle, cloud_cycle, cloud_cycle};

const signed char marker_cycle[] = {0, 1, 2, 3, 4,      // mage TP marker
                              5, 6, 7, 8, 9,
                              10,11,12,13,14,
                              15,16,17,18,19,-1};
const signed char * const animarker[] = {marker_cycle, marker_cycle, marker_cycle, marker_cycle,
                            marker_cycle, marker_cycle, marker_cycle, marker_cycle,
                            marker_cycle, marker_cycle, marker_cycle, marker_cycle,
                            marker_cycle, marker_cycle, marker_cycle, marker_cycle };

// These are for livings now
const signed char * const animan[] = {
                             bit1, bit2, bit3, bit4, bit5, bit6, bit7, bit8,
                             att1, att2, att3, att4, att5, att6, att7, att8,
                         };

const signed char * const aniskel[] = {
                              bit1, bit2, bit3, bit4,
                              bit5, bit6, bit7, bit8,
                              att1, att2, att3, att4,
                              att5, att6, att7, att8,
                              skel_shrink, skel_shrink, skel_shrink, skel_shrink,  // == tele_out
                              skel_shrink, skel_shrink, skel_shrink, skel_shrink,
                              skel_grow, skel_grow, skel_grow, skel_grow, // grow from ground (tele-in)
                              skel_grow, skel_grow, skel_grow, skel_grow,

                          };

const signed char * const animage[] = {
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
const signed char * const anigs[] = {
                           gs_down, gs_up, gs_down, gs_up,
                           gs_down, gs_up, gs_down, gs_up,
                           gs_down, gs_up, gs_down, gs_up,
                           gs_down, gs_up, gs_down, gs_up,
                       };

const signed char * const anislime[] = {
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

const signed char * const ani_small_slime[] = {
                                      small_slime, small_slime, small_slime, small_slime,
                                      small_slime, small_slime, small_slime, small_slime,
                                      small_slime, small_slime, small_slime, small_slime,
                                      small_slime, small_slime, small_slime, small_slime,
                                  };


// These are for the knives
const signed char kni1[] = {7,6,5,4,3,2,1,0,-1};  // clockwise?
const signed char kni2[] = {0,1,2,3,4,5,6,7,-1};  // counter?
const signed char * const anikni[] = { kni2, kni2, kni1, kni1,
                           kni1, kni1, kni2, kni2,
                           kni2, kni2, kni1, kni1,
                           kni1, kni1, kni2, kni2 };

// These are for the rocks
const signed char rock1[] = {0, -1};
const signed char * const anirock[] = { rock1, rock1, rock1, rock1,
                            rock1, rock1, rock1, rock1,
                            rock1, rock1, rock1, rock1,
                            rock1, rock1, rock1, rock1 };

const signed char grow1[] = {4, 3, 2, 1, 0, -1};
const signed char * const anitree[] = { rock1, rock1, rock1, rock1,
                            rock1, rock1, rock1, rock1,
                            grow1, grow1, grow1, grow1,
                            grow1, grow1, grow1, grow1 };

const signed char door1[] = {0, -1};
const signed char door2[] = {1, -1};
const signed char * const anidoor[] = { door1, door1, door2, door2,
                           door1, door1, door2, door2,
                           door1, door1, door2, door2,
                           door1, door1, door2, door2 };


const signed char dooropen1[] = {0, 2, 3, 4, 1, -1};
const signed char dooropen2[] = {1, 4, 3, 2, 0, -1};
const signed char * const anidooropen[] = { door2, door2, door1, door1,
                               door2, door2, door1, door1,
                               dooropen1, dooropen1, dooropen2, dooropen2,
                               dooropen1, dooropen1, dooropen2, dooropen2 };

const signed char arrow1[] = {1, -1}; // up
const signed char arrow2[] = {5, -1}; // up-right
const signed char arrow3[] = {2, -1}; // right
const signed char arrow4[] = {6, -1}; // down-right
const signed char arrow5[] = {0, -1}; // down
const signed char arrow6[] = {4, -1}; // down-left
const signed char arrow7[] = {3, -1}; // left
const signed char arrow8[] = {7, -1}; // up-left
const signed char * const aniarrow[] = { arrow1, arrow2, arrow3, arrow4,
                             arrow5, arrow6, arrow7, arrow8,
                             arrow1, arrow2, arrow3, arrow4,
                             arrow5, arrow6, arrow7, arrow8 };

// These are for the slimes' blobs
const signed char blob1[] = {0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0, -1};
const signed char * const aniblob1[] = { blob1, blob1, blob1, blob1,
                             blob1, blob1, blob1, blob1,
                             blob1, blob1, blob1, blob1,
                             blob1, blob1, blob1, blob1 };

const signed char none1[] = {0, -1};
const signed char * const aninone[] = { none1, none1, none1, none1,
                            none1, none1, none1, none1,
                            none1, none1, none1, none1,
                            none1, none1, none1, none1 };

// for the tower generator
const signed char towerglow1[] = { 1,1,1,2,2,0,-1 };
const signed char * const anitower[] = { none1, none1, none1, none1,
                            none1, none1, none1, none1,
                            towerglow1, towerglow1, towerglow1, towerglow1,
                            towerglow1, towerglow1, towerglow1, towerglow1 };

// for tent generator
const signed char tent1[] = { 1,1,1,2,2,2,3,3,3,4,4,4,0,-1 };
const signed char * const anitent[] = { none1, none1, none1, none1,
                           none1, none1, none1, none1,
                           tent1, tent1, tent1, tent1,
                           tent1, tent1, tent1, tent1 };

const signed char blood1[] = {3,2,1,0,-1};
const signed char * const aniblood[] = { rock1, rock1, rock1, rock1,
                             rock1, rock1, rock1, rock1,
                             blood1, blood1, blood1, blood1,
                             blood1, blood1, blood1, blood1, };

// These are for the cleric's glow thing
const signed char glowgrow[] = {0, 1, 2, 3, -1};
const signed char glowpulse[] = {4, 5, 6, 7, 8, 9, 8, 7, 6, 5, -1};
const signed char * const aniglowgrow[] = { rock1, rock1, rock1, rock1,
                                rock1, rock1, rock1, rock1,
                                glowgrow, glowgrow, glowgrow, glowgrow,
                                glowgrow, glowgrow, glowgrow, glowgrow,
                                glowpulse, glowpulse, glowpulse, glowpulse,
                                glowpulse, glowpulse, glowpulse, glowpulse, };

// Treasure animations

const signed char food1[] = {0, -1};
const signed char * const anifood[] = { food1, food1, food1, food1,
                            food1, food1, food1, food1,
                            food1, food1, food1, food1,
                            food1, food1, food1, food1 };

// Animation table lookup from FamilyAnimationType enum
static const signed char * const * animation_for_type(int anim_type)
{
    switch (anim_type)
    {
        case FAMILY_ANIM_STANDARD:        return animan;
        case FAMILY_ANIM_MAGE:            return animage;
        case FAMILY_ANIM_SKELETON:        return aniskel;
        case FAMILY_ANIM_GIANT_SKELETON:  return anigs;
        case FAMILY_ANIM_SLIME:           return anislime;
        case FAMILY_ANIM_SMALL_SLIME:     return ani_small_slime;
        case FAMILY_ANIM_STATIC:          return anifood;
        default:                          return animan;
    }
}

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


loader::loader(EntityFactory entity_factory)
    : graphics(SIZE_ORDERS*SIZE_FAMILIES),
      animations(SIZE_ORDERS*SIZE_FAMILIES, nullptr),
      stepsizes(SIZE_ORDERS*SIZE_FAMILIES, 0.0f),
      lineofsight(SIZE_ORDERS*SIZE_FAMILIES, 0),
      act_types(SIZE_ORDERS*SIZE_FAMILIES, static_cast<char>(ACT_RANDOM)),
      damage(SIZE_ORDERS*SIZE_FAMILIES, 0.0f),
      fire_frequency(SIZE_ORDERS*SIZE_FAMILIES, 0.0f),
      entity_factory_(std::move(entity_factory))
{
	if (!entity_factory_.report_error)
	{
		entity_factory_.report_error = [](const std::string& message) {
			LogError("{}\n", message);
		};
	}

	std::fill(std::begin(hitpoints), std::end(hitpoints), 0.0f);

	// Livings — all data driven from family descriptors
	for (int i = 0; i < NUM_FAMILIES; i++)
	{
		const auto* fd = get_family_descriptor(i);
		if (!fd || !fd->pix_filename)
			continue;
		graphics[PIX(Order::Living, i)] = read_pixie_file(fd->pix_filename);
		act_types[PIX(Order::Living, i)] = ACT_RANDOM;
		animations[PIX(Order::Living, i)] = animation_for_type(fd->animation_type);
		lineofsight[PIX(Order::Living, i)] = fd->ai_line_of_sight;
		hitpoints[PIX(Order::Living, i)] = fd->derived_bonuses[0];
		damage[PIX(Order::Living, i)] = fd->derived_bonuses[2];
		stepsizes[PIX(Order::Living, i)] = fd->derived_bonuses[6];
		fire_frequency[PIX(Order::Living, i)] = fd->derived_bonuses[7];
	}

	// Table-driven entity initialization for non-Living entities
	struct EntityDef {
		Order order; int family; const char* pix_file;
		float hp; int act_type; const signed char * const * anim;
		float stepsize; int los; float dmg; float fire_freq;
	};

	// clang-format off
	static const EntityDef defs[] = {
		// Weapon entities (BLOOD pix handled separately for gore toggle)
		//                                                       hp  act         anim           step los  dmg freq
		{Order::Weapon, FAMILY_KNIFE,             "knife.png",    6, ACT_FIRE, anikni,          5,  7,  6, 0},
		{Order::Weapon, FAMILY_ROCK,              "rock.png",     4, ACT_FIRE, anirock,         5,  8,  4, 0},
		{Order::Weapon, FAMILY_ARROW,             "arrow.png",    5, ACT_FIRE, aniarrow,        8, 12,  5, 0},
		{Order::Weapon, FAMILY_FIRE_ARROW,        "farrow.png",   7, ACT_FIRE, aniarrow,        8, 12,  7, 0},
		{Order::Weapon, FAMILY_FIREBALL,          "fire.png",     8, ACT_FIRE, aniarrow,        6,  7, 10, 0},
		{Order::Weapon, FAMILY_TREE,              "tree.png",    50, ACT_SIT,  anitree,         0,  1,  0, 0},
		{Order::Weapon, FAMILY_METEOR,            "meteor.png",  12, ACT_FIRE, aniarrow,        7,  9, 12, 0},
		{Order::Weapon, FAMILY_SPRINKLE,          "sparkle.png",  1, ACT_FIRE, anikni,          6, 10,  1, 0},
		{Order::Weapon, FAMILY_BLOOD,             nullptr,        0, ACT_DIE,  aniblood,        0,  1,  0, 0},
		{Order::Weapon, FAMILY_BONE,              "bone1.png",    5, ACT_FIRE, anikni,          6,  6,  5, 0},
		{Order::Weapon, FAMILY_BLOB,              "sl_ball.png",  1, ACT_FIRE, aniblob1,        2, 11,  1, 2},
		{Order::Weapon, FAMILY_LIGHTNING,         "lightnin.png",60, ACT_FIRE, aniarrow,        9, 13,  6, 0},
		{Order::Weapon, FAMILY_GLOW,              "clerglow.png",50, ACT_SIT,  aniglowgrow,    0,   1,  0, 0},
		{Order::Weapon, FAMILY_WAVE,              "wave.png",    50, ACT_FIRE, aniarrow,        6,  3, 16, 0},
		{Order::Weapon, FAMILY_WAVE2,             "wave2.png",   50, ACT_RANDOM, aniarrow,      4,  4, 12, 0},
		{Order::Weapon, FAMILY_WAVE3,             "wave3.png",   50, ACT_FIRE, aniarrow,        3,  6, 10, 0},
		{Order::Weapon, FAMILY_CIRCLE_PROTECTION, "wave2.png",   50, ACT_SIT,  anifood,         1,110,  0, 0},
		{Order::Weapon, FAMILY_HAMMER,            "hammer.png",  10, ACT_FIRE, aniarrow,        6,  4,  9, 0},
		{Order::Weapon, FAMILY_DOOR,              "door.png",  5000, ACT_SIT,  anidoor,         0,  1,  0, 0},
		{Order::Weapon, FAMILY_BOULDER,           "boulder1.png",50, ACT_FIRE, aninone,        10,  9, 25, 0},

		// Treasure entities (STAIN pix handled by gore toggle; data_copy entries have nullptr pix)
		{Order::Treasure, FAMILY_STAIN,                nullptr,       0, ACT_CONTROL, aniblood, 0, 0, 0, 0},
		{Order::Treasure, FAMILY_DRUMSTICK,            "food1.png",  10, ACT_CONTROL, anifood,  5, 0, 0, 0},
		{Order::Treasure, FAMILY_GOLD_BAR,             "bar1.png", 1000, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_SILVER_BAR,           nullptr,     100, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_MAGIC_POTION,         "bottle.png",  0, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_INVIS_POTION,         nullptr,       0, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_INVULNERABLE_POTION,  nullptr,       0, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_FLIGHT_POTION,        nullptr,       0, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_EXIT,                 "16exit1.png", 0, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_TELEPORTER,           "teleport.png",0, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_LIFE_GEM,             "lifegem.png", 0, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_KEY,                  "key.png",     0, ACT_CONTROL, anifood,  0, 0, 0, 0},
		{Order::Treasure, FAMILY_SPEED_POTION,         nullptr,       0, ACT_CONTROL, anifood,  0, 0, 0, 0},

		// Generator entities
		{Order::Generator, FAMILY_TENT,      "tent.png",    100, ACT_GENERATE, anitent,  0, 0, 0, 0},
		{Order::Generator, FAMILY_TOWER,     "tower4.png",    0, ACT_GENERATE, anitower, 0, 0, 0, 0},
		{Order::Generator, FAMILY_BONES,     "bonepile.png",  0, ACT_GENERATE, aninone,  0, 0, 2, 0},
		{Order::Generator, FAMILY_TREEHOUSE, "bigtree.png",   0, ACT_GENERATE, aninone,  0, 0, 0, 0},

		// Special
		{Order::Special, FAMILY_RESERVED_TEAM, "team.png", 0, ACT_RANDOM, nullptr, 0, 0, 0, 0},

		// FX entities
		{Order::FX, FAMILY_EXPAND,       "expand8.png",    0, ACT_RANDOM, aniexpand8,    0,  0,  0, 0},
		{Order::FX, FAMILY_GHOST_SCARE,  "expand8.png",    0, ACT_RANDOM, aniexpand8,    0,  0,  0, 0},
		{Order::FX, FAMILY_BOMB,         "bomb1.png",      0, ACT_RANDOM, anibomb1,      0,  0,  0, 0},
		{Order::FX, FAMILY_EXPLOSION,    "boom1.png",      0, ACT_RANDOM, aniexplosion1, 0,  0,  0, 0},
		{Order::FX, FAMILY_FLASH,        "telflash.png",   0, ACT_RANDOM, aniexpand8,    0,  0,  0, 0},
		{Order::FX, FAMILY_MAGIC_SHIELD, "mshield.png",  100, ACT_RANDOM, anikni,        0,  0, 10, 0},
		{Order::FX, FAMILY_KNIFE_BACK,   "knife.png",      0, ACT_RANDOM, anikni,        0,  0,  0, 0},
		{Order::FX, FAMILY_CLOUD,        "cloud.png",      0, ACT_RANDOM, anicloud,      4,  0, 20, 0},
		{Order::FX, FAMILY_MARKER,       "marker.png",     0, ACT_RANDOM, animarker,     0,  0,  0, 0},
		{Order::FX, FAMILY_BOOMERANG,    "boomer.png",    50, ACT_RANDOM, ani16,         0,  0,  8, 0},
		{Order::FX, FAMILY_CHAIN,        "lightnin.png",   0, ACT_RANDOM, aniarrow,     12, 15,  0, 0},
		{Order::FX, FAMILY_DOOR_OPEN,    "door.png",       0, ACT_RANDOM, anidooropen,   0,  0,  0, 0},
		{Order::FX, FAMILY_HIT,          "hit.png",        0, ACT_RANDOM, anihit,        0,  0,  0, 0},

		// Button graphics (no gameplay properties)
		{Order::Button1, FAMILY_NORMAL1, "normal1.png",  0, ACT_RANDOM, nullptr, 0, 0, 0, 0},
		{Order::Button1, FAMILY_PLUS,    "butplus.png",  0, ACT_RANDOM, nullptr, 0, 0, 0, 0},
		{Order::Button1, FAMILY_MINUS,   "butminus.png", 0, ACT_RANDOM, nullptr, 0, 0, 0, 0},
		{Order::Button1, FAMILY_WRENCH,  "wrench.png",   0, ACT_RANDOM, nullptr, 0, 0, 0, 0},
	};
	// clang-format on

	for (const auto& e : defs) {
		int idx = PIX(e.order, e.family);
		if (e.pix_file)
			graphics[idx] = read_pixie_file(e.pix_file);
		hitpoints[idx] = e.hp;
		act_types[idx] = static_cast<char>(e.act_type);
		if (e.anim)
			animations[idx] = e.anim;
		stepsizes[idx] = e.stepsize;
		lineofsight[idx] = e.los;
		damage[idx] = e.dmg;
		fire_frequency[idx] = e.fire_freq;
	}

	// Gore toggle: BLOOD and STAIN use alternate graphics when gore is off
	if (cfg.is_on("effects", "gore")) {
		graphics[PIX(Order::Weapon, FAMILY_BLOOD)] = read_pixie_file("blood.png");
		graphics[PIX(Order::Treasure, FAMILY_STAIN)] = read_pixie_file("stain.png");
	} else {
		graphics[PIX(Order::Weapon, FAMILY_BLOOD)] = read_pixie_file("blood_friendly.png");
		graphics[PIX(Order::Treasure, FAMILY_STAIN)] = read_pixie_file("stain_friendly.png");
	}

	// Treasure items that share graphics via data_copy
	graphics[PIX(Order::Treasure, FAMILY_SILVER_BAR)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_GOLD_BAR)]);
	graphics[PIX(Order::Treasure, FAMILY_INVIS_POTION)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)]);
	graphics[PIX(Order::Treasure, FAMILY_INVULNERABLE_POTION)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)]);
	graphics[PIX(Order::Treasure, FAMILY_FLIGHT_POTION)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)]);
	graphics[PIX(Order::Treasure, FAMILY_SPEED_POTION)] = data_copy(graphics[PIX(Order::Treasure, FAMILY_MAGIC_POTION)]);

}

loader::~loader(void)
{
	for(int i=0;i<(SIZE_ORDERS*SIZE_FAMILIES);i++) {
	    graphics[i].free();
	}
	// vectors clean up automatically
}

const PixieData* loader::graphics_for(Order order, std::int32_t family) const
{
    order = sanitize_order(order);
    if (family < 0 || family >= NUM_FAMILIES)
        family = 0;

    const PixieData& pix = graphics[PIX(order, family)];
    if (!pix.valid())
        return nullptr;
    return &pix;
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
		entity_factory_.report_error(buf);
		return nullptr;
	}

	// Render component wiring is callback-driven by the platform layer.
	const auto& pix = graphics[PIX(order, family)];

	if (order == Order::Living)
		ob = std::make_unique<living>();
	else if (order == Order::Weapon)
	    ob = std::make_unique<weap>();
	else if (order == Order::Treasure)
		ob = std::make_unique<treasure>();
	else if (order == Order::FX)
		ob = std::make_unique<effect>();
	else
		ob = std::make_unique<walker>();
	if (!ob)
		return nullptr;

	if (entity_factory_.attach_render)
		entity_factory_.attach_render(*ob, pix);

	// Keep sim size/frame metadata in sync even if render attachment is disabled.
	ob->set_data(pix);
	ob->frame = 0;

	ob->stats()->hitpoints = hitpoints[PIX(order, family)];
	ob->stats()->max_hitpoints = hitpoints[PIX(order, family)];
	ob->stats()->special_cost[0] = 0; // shouldn't be used
	ob->stats()->weapon_cost = 1; // default value

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
