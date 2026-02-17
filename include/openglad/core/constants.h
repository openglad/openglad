/* Game-wide constant definitions.
 *
 * Extracted from legacy/base.h so that core/ and entities/ can use these
 * without pulling in SDL, render, or runtime headers.
 */
#pragma once

#include <openglad/core/order.h>
#include <cstdint>

// Act types
inline constexpr int ACT_RANDOM = 0;
inline constexpr int ACT_FIRE = 1;
inline constexpr int ACT_CONTROL = 2;
inline constexpr int ACT_GUARD = 3;
inline constexpr int ACT_GENERATE = 4;
inline constexpr int ACT_DIE = 5;
inline constexpr int ACT_SIT = 6;

// Team types
inline constexpr int MAX_TEAM = 7;

// Animation Types : Livings
inline constexpr int ANI_WALK = 0;
inline constexpr int ANI_ATTACK = 1;
inline constexpr int ANI_TELE_OUT = 2;
inline constexpr int ANI_SKEL_GROW = 3;
inline constexpr int ANI_TELE_IN = 3;
inline constexpr int ANI_SLIME_SPLIT = 4;

// Animations types : weapons
inline constexpr int ANI_GROW = 1; // Trees have no attack animation
inline constexpr int ANI_GLOWGROW = 1; // Neither do sparkles
inline constexpr int ANI_GLOWPULSE = 2; // sparkles cycling

// These are for effect objects ..
inline constexpr int ANI_EXPAND_8 = 1; //1
inline constexpr int ANI_DOOR_OPEN = 1; // Door opening
inline constexpr int ANI_SCARE    = 1; // 2 ghost scare
inline constexpr int ANI_BOMB     = 1; // 3 thief's bomb
inline constexpr int ANI_EXPLODE  = 1; // 4
inline constexpr int ANI_SPIN     = 1; // for the marker

// Living families
inline constexpr int FAMILY_SOLDIER = 0;
inline constexpr int FAMILY_ELF = 1;
inline constexpr int FAMILY_ARCHER = 2;
inline constexpr int FAMILY_MAGE = 3;
inline constexpr int FAMILY_SKELETON = 4;
inline constexpr int FAMILY_CLERIC = 5;
inline constexpr int FAMILY_FIREELEMENTAL = 6;
inline constexpr int FAMILY_FAERIE = 7;
inline constexpr int FAMILY_SLIME = 8;
inline constexpr int FAMILY_SMALL_SLIME = 9;
inline constexpr int FAMILY_MEDIUM_SLIME = 10;
inline constexpr int FAMILY_THIEF = 11;
inline constexpr int FAMILY_GHOST = 12;
inline constexpr int FAMILY_DRUID = 13;
inline constexpr int FAMILY_ORC   = 14;
inline constexpr int FAMILY_BIG_ORC = 15;
inline constexpr int FAMILY_BARBARIAN = 16;
inline constexpr int FAMILY_ARCHMAGE = 17;
inline constexpr int FAMILY_GOLEM = 18;
inline constexpr int FAMILY_GIANT_SKELETON = 19;
inline constexpr int FAMILY_TOWER1 = 20;
inline constexpr int NUM_FAMILIES = 21;

constexpr int PIX(int a, int b) { return NUM_FAMILIES * a + b; }
constexpr int PIX(Order a, int b) { return NUM_FAMILIES * static_cast<int>(a) + b; }

//Weapon families
inline constexpr int FAMILY_KNIFE = 0;
inline constexpr int FAMILY_ROCK = 1;
inline constexpr int FAMILY_ARROW = 2;
inline constexpr int FAMILY_FIREBALL = 3;
inline constexpr int FAMILY_TREE = 4;
inline constexpr int FAMILY_METEOR = 5;
inline constexpr int FAMILY_SPRINKLE = 6;
inline constexpr int FAMILY_BONE = 7;
inline constexpr int FAMILY_BLOOD = 8;
inline constexpr int FAMILY_BLOB = 9;
inline constexpr int FAMILY_FIRE_ARROW = 10;
inline constexpr int FAMILY_LIGHTNING = 11;
inline constexpr int FAMILY_GLOW = 12;
inline constexpr int FAMILY_WAVE = 13;
inline constexpr int FAMILY_WAVE2 = 14;
inline constexpr int FAMILY_WAVE3 = 15;
inline constexpr int FAMILY_CIRCLE_PROTECTION = 16;
inline constexpr int FAMILY_HAMMER = 17;
inline constexpr int FAMILY_DOOR = 18;
inline constexpr int FAMILY_BOULDER = 19;

// Treasure families
inline constexpr int FAMILY_STAIN = 0;
inline constexpr int FAMILY_DRUMSTICK = 1;
inline constexpr int FAMILY_GOLD_BAR = 2;
inline constexpr int FAMILY_SILVER_BAR = 3;
inline constexpr int FAMILY_MAGIC_POTION = 4;
inline constexpr int FAMILY_INVIS_POTION = 5;
inline constexpr int FAMILY_INVULNERABLE_POTION = 6;
inline constexpr int FAMILY_FLIGHT_POTION = 7;
inline constexpr int FAMILY_EXIT = 8;
inline constexpr int FAMILY_TELEPORTER = 9;
inline constexpr int FAMILY_LIFE_GEM = 10; // generated upon death
inline constexpr int FAMILY_KEY = 11;
inline constexpr int FAMILY_SPEED_POTION = 12;
inline constexpr int MAX_TREASURE = 12;

// Generator families
inline constexpr int FAMILY_TENT = 0;  // skeletons
inline constexpr int FAMILY_TOWER = 1; // mages
inline constexpr int FAMILY_BONES = 2; // ghosts
inline constexpr int FAMILY_TREEHOUSE = 3; // elves :)

// FX families
inline constexpr int FAMILY_EXPAND = 0;
inline constexpr int FAMILY_GHOST_SCARE = 1;
inline constexpr int FAMILY_BOMB = 2;
inline constexpr int FAMILY_EXPLOSION = 3;
inline constexpr int FAMILY_FLASH = 4;
inline constexpr int FAMILY_MAGIC_SHIELD = 5;
inline constexpr int FAMILY_KNIFE_BACK  = 6;
inline constexpr int FAMILY_BOOMERANG  = 7;
inline constexpr int FAMILY_CLOUD = 8;
inline constexpr int FAMILY_MARKER = 9;
inline constexpr int FAMILY_CHAIN = 10;
inline constexpr int FAMILY_DOOR_OPEN = 11;
inline constexpr int FAMILY_HIT = 12;

// Special families
inline constexpr int FAMILY_RESERVED_TEAM = 0;

// Button graphic families
inline constexpr int FAMILY_NORMAL1 = 0;
inline constexpr int FAMILY_PLUS = 1;
inline constexpr int FAMILY_MINUS = 2;
inline constexpr int FAMILY_WRENCH = 3;

// Facings
inline constexpr int FACE_UP = 0;
inline constexpr int FACE_UP_RIGHT = 1;
inline constexpr int FACE_RIGHT = 2;
inline constexpr int FACE_DOWN_RIGHT = 3;
inline constexpr int FACE_DOWN = 4;
inline constexpr int FACE_DOWN_LEFT = 5;
inline constexpr int FACE_LEFT = 6;
inline constexpr int FACE_UP_LEFT = 7;
inline constexpr int NUM_FACINGS = 8;

// Stats / command defines
inline constexpr int COMMAND_WALK = 1;
inline constexpr int COMMAND_FIRE = 2;
inline constexpr int COMMAND_RANDOM_WALK = 3;
inline constexpr int COMMAND_DIE = 4;
inline constexpr int COMMAND_FOLLOW = 5;
inline constexpr int COMMAND_RUSH = 6;
inline constexpr int COMMAND_MULTIDO = 7;
inline constexpr int COMMAND_QUICK_FIRE = 8;
inline constexpr int COMMAND_SET_WEAPON = 9;
inline constexpr int COMMAND_RESET_WEAPON = 10;
inline constexpr int COMMAND_SEARCH = 11;
inline constexpr int COMMAND_ATTACK = 12;
inline constexpr int COMMAND_RIGHT_WALK = 13;
inline constexpr int COMMAND_UNCHARM = 14;
inline constexpr std::int32_t REGEN = 4000;

// Rendering/display mode constants
inline constexpr int NORMAL_MODE    = 0;
inline constexpr int INVISIBLE_MODE = 1;
inline constexpr int PHANTOM_MODE   = 2;
inline constexpr int OUTLINE_MODE   = 3;

inline constexpr int SHIFT_LIGHTER      = 0;
inline constexpr int SHIFT_DARKER       = 1;
inline constexpr int SHIFT_LEFT         = 2;
inline constexpr int SHIFT_RIGHT        = 3;
inline constexpr int SHIFT_RIGHT_RANDOM = 4;
inline constexpr int SHIFT_RANDOM       = 5;
inline constexpr int SHIFT_BLOCKY       = 6;

// Scenario type flags
inline constexpr char SCEN_TYPE_CAN_EXIT = 1;
inline constexpr char SCEN_TYPE_GEN_EXIT = 2;
inline constexpr char SCEN_TYPE_SAVE_ALL = 4;

// Outline colors
inline constexpr unsigned char OUTLINE_NAMED         = 7;
inline constexpr unsigned char OUTLINE_INVULNERABLE  = 224;
inline constexpr unsigned char OUTLINE_FLYING        = 208;

inline constexpr char ACTION_FOLLOW = 1;

// Grid/map
inline constexpr int GRID_SIZE = 16;

// Generators are limited by this number
inline constexpr int MAXOBS = 150;

// Difficulty settings
inline constexpr int DIFFICULTY_SETTINGS = 3;

// Palette color indices (used by entity logic for outlines, notifications)
inline constexpr unsigned char RED = 40;
