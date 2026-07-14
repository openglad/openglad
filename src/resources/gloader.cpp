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
#include <openglad/resources/gloader_ctf.h>
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
#include <array>
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
static constexpr std::array<signed char, 5> bit1 = {static_cast<char>(1),static_cast<char>(5),static_cast<char>(1),static_cast<char>(9),static_cast<signed char>(-1)};     // up
static constexpr std::array<signed char, 5> bit2 = {static_cast<char>(13),static_cast<char>(17),static_cast<char>(13),static_cast<char>(21),static_cast<signed char>(-1)}; // up-right
static constexpr std::array<signed char, 5> bit3 = {static_cast<char>(2),static_cast<char>(6),static_cast<char>(2),static_cast<char>(10),static_cast<signed char>(-1)};    // right
static constexpr std::array<signed char, 5> bit4 = {static_cast<char>(14),static_cast<char>(18),static_cast<char>(14),static_cast<char>(22),static_cast<signed char>(-1)}; // down-right
static constexpr std::array<signed char, 5> bit5 = {static_cast<char>(0),static_cast<char>(4),static_cast<char>(0),static_cast<char>(8),static_cast<signed char>(-1)};     // down
static constexpr std::array<signed char, 5> bit6 = {static_cast<char>(12),static_cast<char>(16),static_cast<char>(12),static_cast<char>(20),static_cast<signed char>(-1)}; // down-left
static constexpr std::array<signed char, 5> bit7 = {static_cast<char>(3),static_cast<char>(7),static_cast<char>(3),static_cast<char>(11),static_cast<signed char>(-1)};    // left
static constexpr std::array<signed char, 5> bit8 = {static_cast<char>(15),static_cast<char>(19),static_cast<char>(15),static_cast<char>(23),static_cast<signed char>(-1)}; // up-left

static constexpr std::array<signed char, 4> att1 = {1,5,1,-1};       // up
static constexpr std::array<signed char, 4> att2 = {13,17,13,-1};    // up-right
static constexpr std::array<signed char, 4> att3 = {2,6,2,-1};       // right
static constexpr std::array<signed char, 4> att4 = {14,18,14,-1};    // down-right
static constexpr std::array<signed char, 4> att5 = {0,4,0,-1};       // down
static constexpr std::array<signed char, 4> att6 = {12,16,12,-1};    // down-left
static constexpr std::array<signed char, 4> att7 = {3,7,3,-1};       // left
static constexpr std::array<signed char, 4> att8 = {15,19,15,-1};    // up-left

static constexpr std::array<signed char, 5> bitm2 = {21,25,21,29,-1};  // up-right
static constexpr std::array<signed char, 5> bitm4 = {22,26,22,30,-1};  // down-right
static constexpr std::array<signed char, 5> bitm6 = {20,24,20,28,-1};  // down-left
static constexpr std::array<signed char, 5> bitm8 = {23,27,23,31,-1};  // up-left

static constexpr std::array<signed char, 4> mageatt1 = {5,17,1,-1};    // up
static constexpr std::array<signed char, 4> mageatt2 = {25,33,21,-1};  // up-right
static constexpr std::array<signed char, 4> mageatt3 = {6,18,2,-1};    // right
static constexpr std::array<signed char, 4> mageatt4 = {26,34,22,-1};  // down-right
static constexpr std::array<signed char, 4> mageatt5 = {4,16,0,-1};    // down
static constexpr std::array<signed char, 4> mageatt6 = {24,32,20,-1};  // down-left
static constexpr std::array<signed char, 4> mageatt7 = {7,19,3,-1};    // left
static constexpr std::array<signed char, 4> mageatt8 = {27,35,23,-1};  // up-left


static constexpr std::array<signed char, 5> tele_out1 = {12,13,14,15,-1};
static constexpr std::array<signed char, 6> tele_in1 = {15,14,13,12,1,-1};  // up
static constexpr std::array<signed char, 6> tele_in2 = {15,14,13,12,2,-1};  // right
static constexpr std::array<signed char, 6> tele_in3 = {15,14,13,12,0,-1};  // down
static constexpr std::array<signed char, 6> tele_in4 = {15,14,13,12,3,-1};  // left

// Big skeleton, who is currently different ...
static constexpr std::array<signed char, 5> gs_down = {0, 1, 2, 3, -1}; // true "down"
static constexpr std::array<signed char, 5> gs_up = {3, 2, 1, 0, -1}; // faked up :)

// Skeleton growing
static constexpr std::array<signed char, 6> skel_grow =   {27, 26, 25, 24, 0, -1};
static constexpr std::array<signed char, 6> skel_shrink = {0, 24, 25, 26, 27, -1};

// For slime unidirectional movement
static constexpr std::array<signed char, 9> slime_pulse = { 0, 0, 1, 1, 2, 2, 1, 1, -1 };

static constexpr std::array<signed char, 13> slime_split = { 8, 8, 9, 9, 10, 10,
                              11,11,12,12, 13, 13, -1 };

static constexpr std::array<signed char, 29> small_slime = { 0, 0, 1, 1, 2, 2, 3, 3,
                              4, 4, 5, 5, 6, 6, 7, 7,
                              6, 6, 5, 5, 4 ,4, 3, 3,
                              2, 2, 1, 1, -1 };

// These are for the 'effect' objects
static constexpr std::array<signed char, 9> series_8 = {0, 1, 2, 3, 4, 5, 6, 7, -1};
static constexpr std::array<const signed char*, 16> aniexpand8 = { series_8.data(), series_8.data(), series_8.data(), series_8.data(),
                               series_8.data(), series_8.data(), series_8.data(), series_8.data(),
                               series_8.data(), series_8.data(), series_8.data(), series_8.data(),
                               series_8.data(), series_8.data(), series_8.data(), series_8.data() };

//signed char series_16[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1};
static constexpr std::array<signed char, 17> series_16 = {0, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, -1};
static constexpr std::array<const signed char*, 16> ani16 = {series_16.data(), series_16.data(), series_16.data(), series_16.data(),
                        series_16.data(), series_16.data(), series_16.data(), series_16.data(),
                        series_16.data(), series_16.data(), series_16.data(), series_16.data(),
                        series_16.data(), series_16.data(), series_16.data(), series_16.data()};

static constexpr std::array<signed char, 51> bomb1 = {0, 1, 0, 1, 0, 1, 0, 1, 2, 3, 2, 3, 2, 3, 2, 3,
                       4, 5, 4, 5, 4, 5, 4, 5, 6, 7, 6, 7, 6, 7, 6, 7,
                       8, 9, 8, 9, 8, 9, 8, 9, 10, 11, 10, 11, 10, 11, 10, 11,
                       12, 12, -1};
static constexpr std::array<const signed char*, 16> anibomb1 = {bomb1.data(), bomb1.data(), bomb1.data(), bomb1.data(),
                            bomb1.data(), bomb1.data(), bomb1.data(), bomb1.data(),
                            bomb1.data(), bomb1.data(), bomb1.data(), bomb1.data(),
                            bomb1.data(), bomb1.data(), bomb1.data(), bomb1.data() };

static constexpr std::array<signed char, 4> explosion1 = {0, 1, 2, -1};
static constexpr std::array<const signed char*, 16> aniexplosion1 = {explosion1.data(), explosion1.data(), explosion1.data(), explosion1.data(),
                                 explosion1.data(), explosion1.data(), explosion1.data(), explosion1.data(),
                                 explosion1.data(), explosion1.data(), explosion1.data(), explosion1.data(),
                                 explosion1.data(), explosion1.data(), explosion1.data(), explosion1.data() };

/*
How do animations work?
animate() sets the walker graphic to: ani[curdir+ani_type*NUM_FACINGS][cycle]
ani_type of 0 causes an effect object to last only one frame.
So any lasting animation usually has ani_type of 1, which means 'ani' needs to store at least 16 elements (NUM_FACINGS == 8).
The animation can be directional due to the use of curdir.
The signed char[] are the actual frame indices for the animation.  -1 means to end the animation.
*/

static constexpr std::array<signed char, 3> hit1 = {0, 1, -1};
static constexpr std::array<signed char, 3> hit2 = {0, 2, -1};
static constexpr std::array<signed char, 3> hit3 = {0, 3, -1};
static constexpr std::array<const signed char*, 32> anihit = {hit1.data(), hit1.data(), hit1.data(), hit1.data(),
                                 hit1.data(), hit1.data(), hit1.data(), hit1.data(),
                                 hit1.data(), hit1.data(), hit1.data(), hit1.data(),
                                 hit1.data(), hit1.data(), hit1.data(), hit1.data(),
                                 hit2.data(), hit2.data(), hit2.data(), hit2.data(),
                                 hit2.data(), hit2.data(), hit2.data(), hit2.data(),
                                 hit3.data(), hit3.data(), hit3.data(), hit3.data(),
                                 hit3.data(), hit3.data(), hit3.data(), hit3.data() };

static constexpr std::array<signed char, 7> cloud_cycle = {0, 1, 2, 3, 2, 1, -1};
static constexpr std::array<const signed char*, 16> anicloud = {cloud_cycle.data(), cloud_cycle.data(), cloud_cycle.data(), cloud_cycle.data(),
                           cloud_cycle.data(), cloud_cycle.data(), cloud_cycle.data(), cloud_cycle.data(),
                           cloud_cycle.data(), cloud_cycle.data(), cloud_cycle.data(), cloud_cycle.data(),
                           cloud_cycle.data(), cloud_cycle.data(), cloud_cycle.data(), cloud_cycle.data()};

static constexpr std::array<signed char, 21> marker_cycle = {0, 1, 2, 3, 4,      // mage TP marker
                              5, 6, 7, 8, 9,
                              10,11,12,13,14,
                              15,16,17,18,19,-1};
static constexpr std::array<const signed char*, 16> animarker = {marker_cycle.data(), marker_cycle.data(), marker_cycle.data(), marker_cycle.data(),
                            marker_cycle.data(), marker_cycle.data(), marker_cycle.data(), marker_cycle.data(),
                            marker_cycle.data(), marker_cycle.data(), marker_cycle.data(), marker_cycle.data(),
                            marker_cycle.data(), marker_cycle.data(), marker_cycle.data(), marker_cycle.data() };

// These are for livings now
static constexpr std::array<const signed char*, 16> animan = {
                             bit1.data(), bit2.data(), bit3.data(), bit4.data(), bit5.data(), bit6.data(), bit7.data(), bit8.data(),
                             att1.data(), att2.data(), att3.data(), att4.data(), att5.data(), att6.data(), att7.data(), att8.data(),
                         };

static constexpr std::array<const signed char*, 32> aniskel = {
                              bit1.data(), bit2.data(), bit3.data(), bit4.data(),
                              bit5.data(), bit6.data(), bit7.data(), bit8.data(),
                              att1.data(), att2.data(), att3.data(), att4.data(),
                              att5.data(), att6.data(), att7.data(), att8.data(),
                              skel_shrink.data(), skel_shrink.data(), skel_shrink.data(), skel_shrink.data(),  // == tele_out
                              skel_shrink.data(), skel_shrink.data(), skel_shrink.data(), skel_shrink.data(),
                              skel_grow.data(), skel_grow.data(), skel_grow.data(), skel_grow.data(), // grow from ground (tele-in)
                              skel_grow.data(), skel_grow.data(), skel_grow.data(), skel_grow.data(),

                          };

static constexpr std::array<const signed char*, 32> animage = {
                              bit1.data(), bitm2.data(), bit3.data(), bitm4.data(),
                              bit5.data(), bitm6.data(), bit7.data(), bitm8.data(),
                              mageatt1.data(), mageatt2.data(), mageatt3.data(), mageatt4.data(),       // 8 == attack
                              mageatt5.data(), mageatt6.data(), mageatt7.data(), mageatt8.data(),
                              tele_out1.data(), tele_out1.data(), tele_out1.data(), tele_out1.data(),   // 16 == tele_out
                              tele_out1.data(), tele_out1.data(), tele_out1.data(), tele_out1.data(),
                              tele_in1.data(), tele_in1.data(), tele_in2.data(), tele_in2.data(),       // 24 == tele_in
                              tele_in3.data(), tele_in3.data(), tele_in4.data(), tele_in4.data(),
                          };

// giant skeleton ..
static constexpr std::array<const signed char*, 16> anigs = {
                           gs_down.data(), gs_up.data(), gs_down.data(), gs_up.data(),
                           gs_down.data(), gs_up.data(), gs_down.data(), gs_up.data(),
                           gs_down.data(), gs_up.data(), gs_down.data(), gs_up.data(),
                           gs_down.data(), gs_up.data(), gs_down.data(), gs_up.data(),
                       };

static constexpr std::array<const signed char*, 40> anislime = {
                               slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), // 0 == walk
                               slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), slime_pulse.data(),
                               slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), // 8 == attack
                               slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), slime_pulse.data(),
                               slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), // 16 == tele_out (ignored)
                               slime_pulse.data(), slime_pulse.data(), slime_pulse.data(), slime_pulse.data(),
                               nullptr, nullptr, nullptr, nullptr,                             // 24 == tele_in (ignored)
                               nullptr, nullptr, nullptr, nullptr,
                               slime_split.data(), slime_split.data(), slime_split.data(), slime_split.data(), // 32 == slime splits
                               slime_split.data(), slime_split.data(), slime_split.data(), slime_split.data(),
                           };

static constexpr std::array<const signed char*, 16> ani_small_slime = {
                                      small_slime.data(), small_slime.data(), small_slime.data(), small_slime.data(),
                                      small_slime.data(), small_slime.data(), small_slime.data(), small_slime.data(),
                                      small_slime.data(), small_slime.data(), small_slime.data(), small_slime.data(),
                                      small_slime.data(), small_slime.data(), small_slime.data(), small_slime.data(),
                                  };


// These are for the knives
static constexpr std::array<signed char, 9> kni1 = {7,6,5,4,3,2,1,0,-1};  // clockwise?
static constexpr std::array<signed char, 9> kni2 = {0,1,2,3,4,5,6,7,-1};  // counter?
static constexpr std::array<const signed char*, 16> anikni = { kni2.data(), kni2.data(), kni1.data(), kni1.data(),
                           kni1.data(), kni1.data(), kni2.data(), kni2.data(),
                           kni2.data(), kni2.data(), kni1.data(), kni1.data(),
                           kni1.data(), kni1.data(), kni2.data(), kni2.data() };

// These are for the rocks
static constexpr std::array<signed char, 2> rock1 = {0, -1};
static constexpr std::array<const signed char*, 16> anirock = { rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                            rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                            rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                            rock1.data(), rock1.data(), rock1.data(), rock1.data() };

static constexpr std::array<signed char, 6> grow1 = {4, 3, 2, 1, 0, -1};
static constexpr std::array<const signed char*, 16> anitree = { rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                            rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                            grow1.data(), grow1.data(), grow1.data(), grow1.data(),
                            grow1.data(), grow1.data(), grow1.data(), grow1.data() };

static constexpr std::array<signed char, 2> door1 = {0, -1};
static constexpr std::array<signed char, 2> door2 = {1, -1};
static constexpr std::array<const signed char*, 16> anidoor = { door1.data(), door1.data(), door2.data(), door2.data(),
                           door1.data(), door1.data(), door2.data(), door2.data(),
                           door1.data(), door1.data(), door2.data(), door2.data(),
                           door1.data(), door1.data(), door2.data(), door2.data() };


static constexpr std::array<signed char, 6> dooropen1 = {0, 2, 3, 4, 1, -1};
static constexpr std::array<signed char, 6> dooropen2 = {1, 4, 3, 2, 0, -1};
static constexpr std::array<const signed char*, 16> anidooropen = { door2.data(), door2.data(), door1.data(), door1.data(),
                               door2.data(), door2.data(), door1.data(), door1.data(),
                               dooropen1.data(), dooropen1.data(), dooropen2.data(), dooropen2.data(),
                               dooropen1.data(), dooropen1.data(), dooropen2.data(), dooropen2.data() };

static constexpr std::array<signed char, 2> arrow1 = {1, -1}; // up
static constexpr std::array<signed char, 2> arrow2 = {5, -1}; // up-right
static constexpr std::array<signed char, 2> arrow3 = {2, -1}; // right
static constexpr std::array<signed char, 2> arrow4 = {6, -1}; // down-right
static constexpr std::array<signed char, 2> arrow5 = {0, -1}; // down
static constexpr std::array<signed char, 2> arrow6 = {4, -1}; // down-left
static constexpr std::array<signed char, 2> arrow7 = {3, -1}; // left
static constexpr std::array<signed char, 2> arrow8 = {7, -1}; // up-left
static constexpr std::array<const signed char*, 16> aniarrow = { arrow1.data(), arrow2.data(), arrow3.data(), arrow4.data(),
                             arrow5.data(), arrow6.data(), arrow7.data(), arrow8.data(),
                             arrow1.data(), arrow2.data(), arrow3.data(), arrow4.data(),
                             arrow5.data(), arrow6.data(), arrow7.data(), arrow8.data() };

// These are for the slimes' blobs
static constexpr std::array<signed char, 14> blob1 = {0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1, 0, -1};
static constexpr std::array<const signed char*, 16> aniblob1 = { blob1.data(), blob1.data(), blob1.data(), blob1.data(),
                             blob1.data(), blob1.data(), blob1.data(), blob1.data(),
                             blob1.data(), blob1.data(), blob1.data(), blob1.data(),
                             blob1.data(), blob1.data(), blob1.data(), blob1.data() };

static constexpr std::array<signed char, 2> none1 = {0, -1};
static constexpr std::array<const signed char*, 16> aninone = { none1.data(), none1.data(), none1.data(), none1.data(),
                            none1.data(), none1.data(), none1.data(), none1.data(),
                            none1.data(), none1.data(), none1.data(), none1.data(),
                            none1.data(), none1.data(), none1.data(), none1.data() };

// for the tower generator
static constexpr std::array<signed char, 7> towerglow1 = { 1,1,1,2,2,0,-1 };
static constexpr std::array<const signed char*, 16> anitower = { none1.data(), none1.data(), none1.data(), none1.data(),
                            none1.data(), none1.data(), none1.data(), none1.data(),
                            towerglow1.data(), towerglow1.data(), towerglow1.data(), towerglow1.data(),
                            towerglow1.data(), towerglow1.data(), towerglow1.data(), towerglow1.data() };

// for tent generator
static constexpr std::array<signed char, 14> tent1 = { 1,1,1,2,2,2,3,3,3,4,4,4,0,-1 };
static constexpr std::array<const signed char*, 16> anitent = { none1.data(), none1.data(), none1.data(), none1.data(),
                           none1.data(), none1.data(), none1.data(), none1.data(),
                           tent1.data(), tent1.data(), tent1.data(), tent1.data(),
                           tent1.data(), tent1.data(), tent1.data(), tent1.data() };

static constexpr std::array<signed char, 5> blood1 = {3,2,1,0,-1};
static constexpr std::array<const signed char*, 16> aniblood = { rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                             rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                             blood1.data(), blood1.data(), blood1.data(), blood1.data(),
                             blood1.data(), blood1.data(), blood1.data(), blood1.data(), };

// These are for the cleric's glow thing
static constexpr std::array<signed char, 5> glowgrow = {0, 1, 2, 3, -1};
static constexpr std::array<signed char, 11> glowpulse = {4, 5, 6, 7, 8, 9, 8, 7, 6, 5, -1};
static constexpr std::array<const signed char*, 24> aniglowgrow = { rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                                rock1.data(), rock1.data(), rock1.data(), rock1.data(),
                                glowgrow.data(), glowgrow.data(), glowgrow.data(), glowgrow.data(),
                                glowgrow.data(), glowgrow.data(), glowgrow.data(), glowgrow.data(),
                                glowpulse.data(), glowpulse.data(), glowpulse.data(), glowpulse.data(),
                                glowpulse.data(), glowpulse.data(), glowpulse.data(), glowpulse.data(), };

// Treasure animations

static constexpr std::array<signed char, 2> food1 = {0, -1};
static constexpr std::array<const signed char*, 16> anifood = { food1.data(), food1.data(), food1.data(), food1.data(),
                            food1.data(), food1.data(), food1.data(), food1.data(),
                            food1.data(), food1.data(), food1.data(), food1.data(),
                            food1.data(), food1.data(), food1.data(), food1.data() };

// Animation table lookup from FamilyAnimationType enum
static const signed char * const * animation_for_type(FamilyAnimationType anim_type)
{
    switch (anim_type)
    {
        case FamilyAnimationType::FAMILY_ANIM_STANDARD:        return animan.data();
        case FamilyAnimationType::FAMILY_ANIM_MAGE:            return animage.data();
        case FamilyAnimationType::FAMILY_ANIM_SKELETON:        return aniskel.data();
        case FamilyAnimationType::FAMILY_ANIM_GIANT_SKELETON:  return anigs.data();
        case FamilyAnimationType::FAMILY_ANIM_SLIME:           return anislime.data();
        case FamilyAnimationType::FAMILY_ANIM_SMALL_SLIME:     return ani_small_slime.data();
        case FamilyAnimationType::FAMILY_ANIM_STATIC:          return anifood.data();
        default:                                               return animan.data();
    }
}

// Returns the number of (facing x ani_type) pointer entries in an animation
// table, or 0 for nullptr / an unrecognized table. Animation tables vary in
// length (most are 16 = 2 ani_types x 8 facings; a few are 24/32; only
// anislime is the full 40). animate() uses this to clamp index math so a
// snapshot/save-controlled ani_type/curdir cannot read past a short table.
static int anim_table_count(const signed char * const * t)
{
    if (!t)
        return 0;
    struct Entry { const signed char * const * table; int count; };
    static const Entry kRegistry[] = {
        { anihit.data(),         static_cast<int>(anihit.size()) },
        { anicloud.data(),       static_cast<int>(anicloud.size()) },
        { animarker.data(),      static_cast<int>(animarker.size()) },
        { animan.data(),         static_cast<int>(animan.size()) },
        { aniskel.data(),        static_cast<int>(aniskel.size()) },
        { animage.data(),        static_cast<int>(animage.size()) },
        { anigs.data(),          static_cast<int>(anigs.size()) },
        { anislime.data(),       static_cast<int>(anislime.size()) },
        { ani_small_slime.data(),static_cast<int>(ani_small_slime.size()) },
        { anikni.data(),         static_cast<int>(anikni.size()) },
        { anirock.data(),        static_cast<int>(anirock.size()) },
        { anitree.data(),        static_cast<int>(anitree.size()) },
        { anidoor.data(),        static_cast<int>(anidoor.size()) },
        { anidooropen.data(),    static_cast<int>(anidooropen.size()) },
        { aniarrow.data(),       static_cast<int>(aniarrow.size()) },
        { aniblob1.data(),       static_cast<int>(aniblob1.size()) },
        { aninone.data(),        static_cast<int>(aninone.size()) },
        { anitower.data(),       static_cast<int>(anitower.size()) },
        { anitent.data(),        static_cast<int>(anitent.size()) },
        { aniblood.data(),       static_cast<int>(aniblood.size()) },
        { aniglowgrow.data(),    static_cast<int>(aniglowgrow.size()) },
        { anifood.data(),        static_cast<int>(anifood.size()) },
        { aniexpand8.data(),     static_cast<int>(aniexpand8.size()) },
        { ani16.data(),          static_cast<int>(ani16.size()) },
        { anibomb1.data(),       static_cast<int>(anibomb1.size()) },
        { aniexplosion1.data(),  static_cast<int>(aniexplosion1.size()) },
    };
    for (const auto& e : kRegistry)
        if (e.table == t)
            return e.count;
    return 0;
}

PixieData data_copy(const PixieData& d)
{
    PixieData result;

    if(!d.valid())
        return result;

    result.frames = d.frames;
    result.w = d.w;
    result.h = d.h;

    size_t len = static_cast<std::size_t>(d.w) * d.h * d.frames;
    result.data = std::make_unique<unsigned char[]>(len);
    std::copy_n(d.data.get(), len, result.data.get());

    return result;
}


loader::loader(EntityFactory entity_factory)
    : graphics(SIZE_ORDERS*SIZE_FAMILIES),
      animations(SIZE_ORDERS*SIZE_FAMILIES, nullptr),
      animation_counts(SIZE_ORDERS*SIZE_FAMILIES, 0),
      stepsizes(SIZE_ORDERS*SIZE_FAMILIES, 0.0f),
      lineofsight(SIZE_ORDERS*SIZE_FAMILIES, 0),
      hitpoints(SIZE_ORDERS*SIZE_FAMILIES, 0.0f),
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

	reload_graphics();
}

void loader::reload_graphics()
{
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
		{Order::Weapon, FAMILY_KNIFE,             "knife.png",    6, ACT_FIRE, anikni.data(),          5,  7,  6, 0},
		{Order::Weapon, FAMILY_ROCK,              "rock.png",     4, ACT_FIRE, anirock.data(),         5,  8,  4, 0},
		{Order::Weapon, FAMILY_ARROW,             "arrow.png",    5, ACT_FIRE, aniarrow.data(),        8, 12,  5, 0},
		{Order::Weapon, FAMILY_FIRE_ARROW,        "farrow.png",   7, ACT_FIRE, aniarrow.data(),        8, 12,  7, 0},
		{Order::Weapon, FAMILY_FIREBALL,          "fire.png",     8, ACT_FIRE, aniarrow.data(),        6,  7, 10, 0},
		{Order::Weapon, FAMILY_TREE,              "tree.png",    50, ACT_SIT,  anitree.data(),         0,  1,  0, 0},
		{Order::Weapon, FAMILY_METEOR,            "meteor.png",  12, ACT_FIRE, aniarrow.data(),        7,  9, 12, 0},
		{Order::Weapon, FAMILY_SPRINKLE,          "sparkle.png",  1, ACT_FIRE, anikni.data(),          6, 10,  1, 0},
		{Order::Weapon, FAMILY_BLOOD,             nullptr,        0, ACT_DIE,  aniblood.data(),        0,  1,  0, 0},
		{Order::Weapon, FAMILY_BONE,              "bone1.png",    5, ACT_FIRE, anikni.data(),          6,  6,  5, 0},
		{Order::Weapon, FAMILY_BLOB,              "sl_ball.png",  1, ACT_FIRE, aniblob1.data(),        2, 11,  1, 2},
		{Order::Weapon, FAMILY_LIGHTNING,         "lightnin.png",60, ACT_FIRE, aniarrow.data(),        9, 13,  6, 0},
		{Order::Weapon, FAMILY_GLOW,              "clerglow.png",50, ACT_SIT,  aniglowgrow.data(),    0,   1,  0, 0},
		{Order::Weapon, FAMILY_WAVE,              "wave.png",    50, ACT_FIRE, aniarrow.data(),        6,  3, 16, 0},
		{Order::Weapon, FAMILY_WAVE2,             "wave2.png",   50, ACT_RANDOM, aniarrow.data(),      4,  4, 12, 0},
		{Order::Weapon, FAMILY_WAVE3,             "wave3.png",   50, ACT_FIRE, aniarrow.data(),        3,  6, 10, 0},
		{Order::Weapon, FAMILY_CIRCLE_PROTECTION, "wave2.png",   50, ACT_SIT,  anifood.data(),         1,110,  0, 0},
		{Order::Weapon, FAMILY_HAMMER,            "hammer.png",  10, ACT_FIRE, aniarrow.data(),        6,  4,  9, 0},
		{Order::Weapon, FAMILY_DOOR,              "door.png",  5000, ACT_SIT,  anidoor.data(),         0,  1,  0, 0},
		{Order::Weapon, FAMILY_BOULDER,           "boulder1.png",50, ACT_FIRE, aninone.data(),        10,  9, 25, 0},

		// Treasure entities (STAIN pix handled by gore toggle; data_copy entries have nullptr pix)
		{Order::Treasure, FAMILY_STAIN,                nullptr,       0, ACT_CONTROL, aniblood.data(), 0, 0, 0, 0},
		{Order::Treasure, FAMILY_DRUMSTICK,            "food1.png",  10, ACT_CONTROL, anifood.data(),  5, 0, 0, 0},
		{Order::Treasure, FAMILY_GOLD_BAR,             "bar1.png", 1000, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_SILVER_BAR,           nullptr,     100, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_MAGIC_POTION,         "bottle.png",  0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_INVIS_POTION,         nullptr,       0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_INVULNERABLE_POTION,  nullptr,       0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_FLIGHT_POTION,        nullptr,       0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_EXIT,                 "16exit1.png", 0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_TELEPORTER,           "teleport.png",0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_LIFE_GEM,             "lifegem.png", 0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_KEY,                  "key.png",     0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},
		{Order::Treasure, FAMILY_SPEED_POTION,         nullptr,       0, ACT_CONTROL, anifood.data(),  0, 0, 0, 0},

		// Generator entities
		{Order::Generator, FAMILY_TENT,      "tent.png",    100, ACT_GENERATE, anitent.data(),  0, 0, 0, 0},
		{Order::Generator, FAMILY_TOWER,     "tower4.png",    0, ACT_GENERATE, anitower.data(), 0, 0, 0, 0},
		{Order::Generator, FAMILY_BONES,     "bonepile.png",  0, ACT_GENERATE, aninone.data(),  0, 0, 2, 0},
		{Order::Generator, FAMILY_TREEHOUSE, "bigtree.png",   0, ACT_GENERATE, aninone.data(),  0, 0, 0, 0},

		// Special
		{Order::Special, FAMILY_RESERVED_TEAM, "team.png", 0, ACT_RANDOM, nullptr, 0, 0, 0, 0},

		// FX entities
		{Order::FX, FAMILY_EXPAND,       "expand8.png",    0, ACT_RANDOM, aniexpand8.data(),    0,  0,  0, 0},
		{Order::FX, FAMILY_GHOST_SCARE,  "expand8.png",    0, ACT_RANDOM, aniexpand8.data(),    0,  0,  0, 0},
		{Order::FX, FAMILY_BOMB,         "bomb1.png",      0, ACT_RANDOM, anibomb1.data(),      0,  0,  0, 0},
		{Order::FX, FAMILY_EXPLOSION,    "boom1.png",      0, ACT_RANDOM, aniexplosion1.data(), 0,  0,  0, 0},
		{Order::FX, FAMILY_FLASH,        "telflash.png",   0, ACT_RANDOM, aniexpand8.data(),    0,  0,  0, 0},
		{Order::FX, FAMILY_MAGIC_SHIELD, "mshield.png",  100, ACT_RANDOM, anikni.data(),        0,  0, 10, 0},
		{Order::FX, FAMILY_KNIFE_BACK,   "knife.png",      0, ACT_RANDOM, anikni.data(),        0,  0,  0, 0},
		{Order::FX, FAMILY_CLOUD,        "cloud.png",      0, ACT_RANDOM, anicloud.data(),      4,  0, 20, 0},
		{Order::FX, FAMILY_MARKER,       "marker.png",     0, ACT_RANDOM, animarker.data(),     0,  0,  0, 0},
		{Order::FX, FAMILY_BOOMERANG,    "boomer.png",    50, ACT_RANDOM, ani16.data(),         0,  0,  8, 0},
		{Order::FX, FAMILY_CHAIN,        "lightnin.png",   0, ACT_RANDOM, aniarrow.data(),     12, 15,  0, 0},
		{Order::FX, FAMILY_DOOR_OPEN,    "door.png",       0, ACT_RANDOM, anidooropen.data(),   0,  0,  0, 0},
		{Order::FX, FAMILY_HIT,          "hit.png",        0, ACT_RANDOM, anihit.data(),        0,  0,  0, 0},

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

	register_ctf_loader_entries(*this);

	// Record each animation table's length (parallel to `animations`) so animate()
	// can bound index math against short per-family tables.
	for (std::size_t i = 0; i < animations.size(); i++)
		animation_counts[i] = anim_table_count(animations[i]);
}

loader::~loader(void)
{
	// Derive the bound from the live container instead of re-stating the
	// SIZE_ORDERS*SIZE_FAMILIES constant (which lives far from the vector's
	// allocation at the top of the ctor); a range-for can never desync from
	// graphics.size(), removing the over-/under-index hazard if that count
	// ever changes. Same elements, same forward order — behavior-identical.
	for (auto& g : graphics) {
	    g.free();
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

	w->set_stepsize(stepsizes[PIX(order, family)]);
	w->set_normal_stepsize(w->stepsize());
	w->set_lineofsight(lineofsight[PIX(order, family)]);
	w->set_damage(damage[PIX(order, family)]);
	w->set_fire_frequency(fire_frequency[PIX(order, family)]);
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
	ob->set_direct_frame(0);

	ob->stats()->set_hitpoints(hitpoints[PIX(order, family)]);
	ob->stats()->set_max_hitpoints(hitpoints[PIX(order, family)]);
	ob->stats()->set_special_cost(0, 0); // shouldn't be used
	ob->stats()->set_weapon_cost(1); // default value

	set_walker(ob.get(), order, family);

	if(order == Order::Living && ob->ani)
        ob->set_frame(ob->ani[ob->curdir()][0]);
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
	ob->ani_count = animation_counts[PIX(order, family)];

	set_derived_stats(ob, order, family);

	for (i=0; i < NUM_SPECIALS; i++)
		ob->stats()->set_special_cost(i, 5000);

	// For special settings
	switch (order)
	{
		case Order::Living:
		{
			auto* fd = get_family_descriptor(family);
			if (fd)
			{
				for (i = 0; i < NUM_SPECIALS; i++)
					ob->stats()->set_special_cost(i, fd->special_cost[i]);
				ob->stats()->set_weapon_cost(fd->weapon_cost);
					ob->set_default_weapon(static_cast<unsigned short>(fd->default_weapon));
				if (fd->init_ani_type != 0)
					ob->set_ani_type(fd->init_ani_type);
				if (fd->init_max_magicpoints > 0)
					ob->stats()->set_max_magicpoints(fd->init_max_magicpoints);
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
			ob->set_current_weapon(ob->default_weapon());
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
						ob->set_lifetime(wfd->init_lifetime);
					if (wfd->init_ani_type != 0)
						ob->set_ani_type(wfd->init_ani_type);
					// Z-axis projectile physics (no-op for flat weapons).
					if (wfd->init_vz != 0.0f)
						ob->set_vz(wfd->init_vz);
					if (wfd->init_sizez != 0)
						ob->set_sizez(wfd->init_sizez);
				}
			}  // end of weapons
			break;
			case Order::Treasure:
			{
				const auto* tfd = get_treasure_family_descriptor(family);
				if (tfd)
				{
					if (tfd->init_ignore)
						ob->set_ignore(1);
					if (tfd->init_frame >= 0)
						ob->set_direct_frame(tfd->init_frame);
				}
			}
			break;
			case Order::Generator:
			{
				const auto* gfd = get_generator_family_descriptor(family);
				ob->stats()->set_weapon_cost(0);
					ob->set_default_weapon(static_cast<unsigned short>(gfd ? gfd->default_weapon : FAMILY_SKELETON));
			}
			break;
		case Order::FX:
			ob->set_ani_type(0);
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
