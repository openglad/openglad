/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>

namespace og::dirty {

// Bit indices for SimEntity::dirty_mask_[2]. Shared between entity setters
// and the snapshot field table.

// SimEntity fields (0-18)
inline constexpr std::uint8_t BIT_ENTITY_ID = 0;
inline constexpr std::uint8_t BIT_XPOS = 1;
inline constexpr std::uint8_t BIT_YPOS = 2;
inline constexpr std::uint8_t BIT_SIZEX = 3;
inline constexpr std::uint8_t BIT_SIZEY = 4;
inline constexpr std::uint8_t BIT_TEAM_NUM = 5;
inline constexpr std::uint8_t BIT_REAL_TEAM_NUM = 6;
inline constexpr std::uint8_t BIT_USER = 7;
inline constexpr std::uint8_t BIT_DEAD = 8;
inline constexpr std::uint8_t BIT_DEATH_CALLED = 9;
inline constexpr std::uint8_t BIT_INVULNERABLE_LEFT = 10;
inline constexpr std::uint8_t BIT_INVISIBILITY_LEFT = 11;
inline constexpr std::uint8_t BIT_FLIGHT_LEFT = 12;
inline constexpr std::uint8_t BIT_BONUS_ROUNDS = 13;
inline constexpr std::uint8_t BIT_ORDER = 14;
inline constexpr std::uint8_t BIT_FAMILY = 15;
inline constexpr std::uint8_t BIT_FRAME = 16;
inline constexpr std::uint8_t BIT_WORLDX = 17;
inline constexpr std::uint8_t BIT_WORLDY = 18;

// walker fields (19-62)
inline constexpr std::uint8_t BIT_LASTX = 19;
inline constexpr std::uint8_t BIT_LASTY = 20;
inline constexpr std::uint8_t BIT_STEPSIZE = 21;
inline constexpr std::uint8_t BIT_NORMAL_STEPSIZE = 22;
inline constexpr std::uint8_t BIT_CURDIR = 23;
inline constexpr std::uint8_t BIT_ENDDIR = 24;
inline constexpr std::uint8_t BIT_DAMAGE = 25;
inline constexpr std::uint8_t BIT_FIRE_FREQUENCY = 26;
inline constexpr std::uint8_t BIT_BUSY = 27;
inline constexpr std::uint8_t BIT_CURRENT_WEAPON = 28;
inline constexpr std::uint8_t BIT_DEFAULT_WEAPON = 29;
inline constexpr std::uint8_t BIT_ATTACK_LUNGE = 30;
inline constexpr std::uint8_t BIT_ATTACK_LUNGE_ANGLE = 31;
inline constexpr std::uint8_t BIT_HIT_RECOIL = 32;
inline constexpr std::uint8_t BIT_HIT_RECOIL_ANGLE = 33;
inline constexpr std::uint8_t BIT_LAST_HITPOINTS = 34;
inline constexpr std::uint8_t BIT_ACTION = 35;
inline constexpr std::uint8_t BIT_ACT_TYPE = 36;
inline constexpr std::uint8_t BIT_OLD_ACT_TYPE = 37;
inline constexpr std::uint8_t BIT_ANI_TYPE = 38;
inline constexpr std::uint8_t BIT_CYCLE = 39;
inline constexpr std::uint8_t BIT_DRAWCYCLE = 40;
inline constexpr std::uint8_t BIT_CURRENT_SPECIAL = 41;
inline constexpr std::uint8_t BIT_IGNORE = 42;
inline constexpr std::uint8_t BIT_IN_ACT = 43;
inline constexpr std::uint8_t BIT_SHIFTER_DOWN = 44;
inline constexpr std::uint8_t BIT_YO_DELAY = 45;
inline constexpr std::uint8_t BIT_SKIP_EXIT = 46;
inline constexpr std::uint8_t BIT_OUTLINE = 47;
inline constexpr std::uint8_t BIT_HURT_FLASH = 48;
inline constexpr std::uint8_t BIT_LIFETIME = 49;
inline constexpr std::uint8_t BIT_SPEED_BONUS = 50;
inline constexpr std::uint8_t BIT_SPEED_BONUS_LEFT = 51;
inline constexpr std::uint8_t BIT_CHARM_LEFT = 52;
inline constexpr std::uint8_t BIT_WEAPONS_LEFT = 53;
inline constexpr std::uint8_t BIT_KEYS = 54;
inline constexpr std::uint8_t BIT_VIEW_ALL = 55;
inline constexpr std::uint8_t BIT_LINEOFSIGHT = 56;
inline constexpr std::uint8_t BIT_PATH_CHECK_COUNTER = 57;
inline constexpr std::uint8_t BIT_REGEN_DELAY = 58;
inline constexpr std::uint8_t BIT_FOE_ID = 59;
inline constexpr std::uint8_t BIT_LEADER_ID = 60;
inline constexpr std::uint8_t BIT_OWNER_ID = 61;
inline constexpr std::uint8_t BIT_COLLIDE_OB_ID = 62;

// statistics fields (63-84)
inline constexpr std::uint8_t BIT_HITPOINTS = 63;
inline constexpr std::uint8_t BIT_MAX_HITPOINTS = 64;
inline constexpr std::uint8_t BIT_MAGICPOINTS = 65;
inline constexpr std::uint8_t BIT_MAX_MAGICPOINTS = 66;
inline constexpr std::uint8_t BIT_MAX_HEAL_DELAY = 67;
inline constexpr std::uint8_t BIT_CURRENT_HEAL_DELAY = 68;
inline constexpr std::uint8_t BIT_MAX_MAGIC_DELAY = 69;
inline constexpr std::uint8_t BIT_CURRENT_MAGIC_DELAY = 70;
inline constexpr std::uint8_t BIT_MAGIC_PER_ROUND = 71;
inline constexpr std::uint8_t BIT_HEAL_PER_ROUND = 72;
inline constexpr std::uint8_t BIT_ARMOR = 73;
inline constexpr std::uint8_t BIT_LEVEL = 74;
inline constexpr std::uint8_t BIT_BIT_FLAGS = 75;
inline constexpr std::uint8_t BIT_DELETE_ME = 76;
inline constexpr std::uint8_t BIT_FROZEN_DELAY = 77;
inline constexpr std::uint8_t BIT_WEAPON_COST = 78;
inline constexpr std::uint8_t BIT_SPECIAL_COST = 79;
inline constexpr std::uint8_t BIT_OLD_ORDER = 80;
inline constexpr std::uint8_t BIT_OLD_FAMILY = 81;
inline constexpr std::uint8_t BIT_LAST_DISTANCE = 82;
inline constexpr std::uint8_t BIT_CURRENT_DISTANCE = 83;
inline constexpr std::uint8_t BIT_CONTROLLER_ID = 84;

// weap fields (85)
inline constexpr std::uint8_t BIT_DO_BOUNCE = 85;

inline constexpr std::uint8_t FIELD_COUNT = 86;

static_assert(BIT_DO_BOUNCE + 1 == FIELD_COUNT,
              "Dirty field bit count drift -- update dirty_field_bits.h");

} // namespace og::dirty
