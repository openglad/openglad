/* Pre-refactor tests for family-specific behavioral callbacks.
 * These tests capture the exact per-family formulas in:
 *   - living::set_difficulty()  (living.cpp)
 *   - guy::upgrade_to_level()   (guy.cpp)
 *
 * They must pass both BEFORE and AFTER behavioral extraction into
 * FamilyDescriptor callbacks.
 */
#include <openglad/entities/guy.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <cmath>

extern screen* myscreen;

static std::unique_ptr<walker> make_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family, myscreen);
    if (!w) return nullptr;
    w->setxy(100, 100);
    return w;
}

// Helper: assert float is within tolerance of expected
#define TEST_ASSERT_FLOAT(expected, actual, msg) \
    do { \
        float _exp = (expected); \
        float _act = (actual); \
        if (std::fabs(_exp - _act) > 0.5f) { \
            fprintf(stderr, "  FAIL: %s (expected %.1f, got %.1f) (%s:%d)\n", \
                    msg, _exp, _act, __FILE__, __LINE__); \
            g_tests_failed++; \
            g_tests_run++; \
            return; \
        } \
    } while(0)

// ===========================================================================
// set_difficulty tests — verify exact per-family stat increments at level 2
// levmult = 2*2 = 4, level_f = 2
// ===========================================================================

struct DifficultyExpected {
    int family;
    const char* name;
    float hp_delta;    // max_hitpoints increment
    float mp_delta;    // max_magicpoints increment
    float dmg_delta;   // damage increment
    float armor_delta; // armor increment
};

// Expected increments for level 2 (levmult=4, level_f=2)
static const DifficultyExpected difficulty_cases[] = {
    { FAMILY_SOLDIER,  "soldier",  13*4, 8*4,  5*2, 2*4  },
    { FAMILY_ARCHER,   "archer",   11*4, 12*4, 4*2, 1*4  },
    { FAMILY_MAGE,     "mage",     7*4,  14*4, 3*2, 4/2  },
    { FAMILY_CLERIC,   "cleric",   9*4,  12*4, 4*2, 4/2  },
    { FAMILY_DRUID,    "druid",    9*4,  12*4, 4*2, 4/2  },
    { FAMILY_ORC,      "orc",      14*4, 7*4,  6*2, 3*4  },
    { FAMILY_GOLEM,    "golem",    18*4, 5*4,  7*2, 4*4  },
    // Default formula families:
    { FAMILY_ELF,          "elf",          11*4, 11*4, 4*2, 2*4 },
    { FAMILY_SKELETON,     "skeleton",     11*4, 11*4, 4*2, 2*4 },
    { FAMILY_FIREELEMENTAL,"fire_elem",    11*4, 11*4, 4*2, 2*4 },
    { FAMILY_FAERIE,       "faerie",       11*4, 11*4, 4*2, 2*4 },
    { FAMILY_SLIME,        "slime",        11*4, 11*4, 4*2, 2*4 },
    { FAMILY_SMALL_SLIME,  "small_slime",  11*4, 11*4, 4*2, 2*4 },
    { FAMILY_MEDIUM_SLIME, "medium_slime", 11*4, 11*4, 4*2, 2*4 },
    { FAMILY_THIEF,        "thief",        11*4, 11*4, 4*2, 2*4 },
    { FAMILY_GHOST,        "ghost",        11*4, 11*4, 4*2, 2*4 },
    { FAMILY_BIG_ORC,      "big_orc",      11*4, 11*4, 4*2, 2*4 },
    { FAMILY_BARBARIAN,    "barbarian",    11*4, 11*4, 4*2, 2*4 },
    { FAMILY_ARCHMAGE,     "archmage",     11*4, 11*4, 4*2, 2*4 },
    { FAMILY_GIANT_SKELETON,"giant_skel",  11*4, 11*4, 4*2, 2*4 },
    { FAMILY_TOWER1,       "tower1",       11*4, 11*4, 4*2, 2*4 },
};

void test_set_difficulty_per_family_exact()
{
    for (const auto& tc : difficulty_cases)
    {
        auto w = make_living(static_cast<char>(tc.family));
        TEST_ASSERT(w != nullptr, "make_living should succeed");

        // Record initial stats
        float hp0 = w->stats()->max_hitpoints;
        float mp0 = w->stats()->max_magicpoints;
        float dmg0 = w->damage;
        float armor0 = w->stats()->armor;

        // team_num=0 means player team, no difficulty scaling applied
        w->team_num = 0;
        static_cast<living*>(w.get())->set_difficulty(2);

        float hp_delta = w->stats()->max_hitpoints - hp0;
        float mp_delta = w->stats()->max_magicpoints - mp0;
        float dmg_delta = w->damage - dmg0;
        float armor_delta = w->stats()->armor - armor0;

        char buf[128];
        snprintf(buf, sizeof(buf), "%s HP delta", tc.name);
        TEST_ASSERT_FLOAT(tc.hp_delta, hp_delta, buf);
        snprintf(buf, sizeof(buf), "%s MP delta", tc.name);
        TEST_ASSERT_FLOAT(tc.mp_delta, mp_delta, buf);
        snprintf(buf, sizeof(buf), "%s dmg delta", tc.name);
        TEST_ASSERT_FLOAT(tc.dmg_delta, dmg_delta, buf);
        snprintf(buf, sizeof(buf), "%s armor delta", tc.name);
        TEST_ASSERT_FLOAT(tc.armor_delta, armor_delta, buf);
    }
}
REGISTER_TEST(test_set_difficulty_per_family_exact);

// Soldier: weapons_left = (level+1)/2
void test_set_difficulty_soldier_weapons_left()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "make_living should succeed");
    w->team_num = 0;

    static_cast<living*>(w.get())->set_difficulty(5);
    short wl = static_cast<living*>(w.get())->weapons_left;
    TEST_ASSERT_EQ(3, (int)wl, "soldier weapons_left at level 5 should be (5+1)/2=3");
}
REGISTER_TEST(test_set_difficulty_soldier_weapons_left);

// Non-player teams get difficulty scaling applied
void test_set_difficulty_enemy_scaling()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "make_living should succeed");

    auto w2 = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w2 != nullptr, "make_living should succeed");

    w->team_num = 0;  // player team
    w2->team_num = 1; // enemy team

    static_cast<living*>(w.get())->set_difficulty(3);
    static_cast<living*>(w2.get())->set_difficulty(3);

    // Player and enemy get different final values due to difficulty scaling
    // (exact ratio depends on difficulty_level setting, but they should differ)
    float player_hp = w->stats()->max_hitpoints;
    float enemy_hp = w2->stats()->max_hitpoints;
    // They should be different (unless difficulty is exactly 100)
    // We just verify both are positive
    TEST_ASSERT(player_hp > 0, "player HP positive");
    TEST_ASSERT(enemy_hp > 0, "enemy HP positive");
}
REGISTER_TEST(test_set_difficulty_enemy_scaling);

// After set_difficulty, hitpoints = max_hitpoints (healed to full)
void test_set_difficulty_heals_to_full()
{
    auto w = make_living(FAMILY_ARCHER);
    TEST_ASSERT(w != nullptr, "make_living should succeed");
    w->team_num = 0;

    static_cast<living*>(w.get())->set_difficulty(3);
    TEST_ASSERT_FLOAT(w->stats()->max_hitpoints, w->stats()->hitpoints,
                      "set_difficulty should heal to max HP");
    TEST_ASSERT_FLOAT(w->stats()->max_magicpoints, w->stats()->magicpoints,
                      "set_difficulty should heal to max MP");
}
REGISTER_TEST(test_set_difficulty_heals_to_full);

// ===========================================================================
// upgrade_to_level tests — verify exact per-family stat modifiers
// From level 1 to level 5 (level_diff = 4)
// Base deltas: s=32, d=24, c=32, it=32, a=4
// ===========================================================================

struct LevelUpExpected {
    int family;
    const char* name;
    Sint32 str_delta;
    Sint32 dex_delta;
    Sint32 con_delta;
    Sint32 int_delta;
    Sint32 armor_delta;
};

// Expected stat deltas for level 1→5 (level_diff=4):
// base: s=32, d=24, c=32, it=32, a=4
static const LevelUpExpected levelup_cases[] = {
    // Soldier: no mods
    { FAMILY_SOLDIER,  "soldier",  32, 24, 32, 32, 4 },
    // Elf: s*3/4=24, d*3/2=36, c*3/4=24
    { FAMILY_ELF,      "elf",      24, 36, 24, 32, 4 },
    // Archer: s/2=16, d*3/2=36
    { FAMILY_ARCHER,   "archer",   16, 36, 32, 32, 4 },
    // Mage: s/2=16, c/2=16, it*2=64
    { FAMILY_MAGE,     "mage",     16, 24, 16, 64, 4 },
    // Archmage: s/2=16, c/2=16, it*2=64
    { FAMILY_ARCHMAGE, "archmage", 16, 24, 16, 64, 4 },
    // Skeleton: d*2=48, c/2=16, it/2=16
    { FAMILY_SKELETON, "skeleton", 32, 48, 16, 16, 4 },
    // Cleric: no mods
    { FAMILY_CLERIC,   "cleric",   32, 24, 32, 32, 4 },
    // Fire Elemental: s*3/2=48, c/2=16
    { FAMILY_FIREELEMENTAL, "fire_elem", 48, 24, 16, 32, 4 },
    // Faerie: s/2=16, d*2=48, c/2=16
    { FAMILY_FAERIE,   "faerie",   16, 48, 16, 32, 4 },
    // Slime variants: no mods
    { FAMILY_SLIME,       "slime",       32, 24, 32, 32, 4 },
    { FAMILY_SMALL_SLIME, "small_slime", 32, 24, 32, 32, 4 },
    { FAMILY_MEDIUM_SLIME,"medium_slime",32, 24, 32, 32, 4 },
    // Thief: s/2=16, d*2=48, c/2=16
    { FAMILY_THIEF,    "thief",    16, 48, 16, 32, 4 },
    // Ghost: no mods
    { FAMILY_GHOST,    "ghost",    32, 24, 32, 32, 4 },
    // Druid: d/2=12, it*3/2=48
    { FAMILY_DRUID,    "druid",    32, 12, 32, 48, 4 },
    // Orc: s*3/2=48, d/2=12, c*3/2=48, it/2=16
    { FAMILY_ORC,      "orc",      48, 12, 48, 16, 4 },
    // Big Orc: same as Orc
    { FAMILY_BIG_ORC,  "big_orc",  48, 12, 48, 16, 4 },
    // Barbarian: same as Orc
    { FAMILY_BARBARIAN,"barbarian",48, 12, 48, 16, 4 },
    // Golem: default (no case) — no mods
    { FAMILY_GOLEM,    "golem",    32, 24, 32, 32, 4 },
    // Giant Skeleton: default — no mods
    { FAMILY_GIANT_SKELETON, "giant_skel", 32, 24, 32, 32, 4 },
    // Tower1: default — no mods
    { FAMILY_TOWER1,   "tower1",   32, 24, 32, 32, 4 },
};

void test_upgrade_to_level_per_family_exact()
{
    for (const auto& tc : levelup_cases)
    {
        guy g(tc.family);
        // Record initial stats at level 1
        Sint32 str0 = g.strength;
        Sint32 dex0 = g.dexterity;
        Sint32 con0 = g.constitution;
        Sint32 int0 = g.intelligence;
        Sint32 armor0 = g.armor;

        g.upgrade_to_level(5);

        Sint32 str_delta = g.strength - str0;
        Sint32 dex_delta = g.dexterity - dex0;
        Sint32 con_delta = g.constitution - con0;
        Sint32 int_delta = g.intelligence - int0;
        Sint32 armor_delta = g.armor - armor0;

        char buf[128];
        snprintf(buf, sizeof(buf), "%s str delta", tc.name);
        TEST_ASSERT_EQ(tc.str_delta, (int)str_delta, buf);
        snprintf(buf, sizeof(buf), "%s dex delta", tc.name);
        TEST_ASSERT_EQ(tc.dex_delta, (int)dex_delta, buf);
        snprintf(buf, sizeof(buf), "%s con delta", tc.name);
        TEST_ASSERT_EQ(tc.con_delta, (int)con_delta, buf);
        snprintf(buf, sizeof(buf), "%s int delta", tc.name);
        TEST_ASSERT_EQ(tc.int_delta, (int)int_delta, buf);
        snprintf(buf, sizeof(buf), "%s armor delta", tc.name);
        TEST_ASSERT_EQ(tc.armor_delta, (int)armor_delta, buf);
    }
}
REGISTER_TEST(test_upgrade_to_level_per_family_exact);

// Verify level and xp are set correctly
void test_upgrade_to_level_sets_level_and_xp()
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(5, true);
    TEST_ASSERT_EQ(5, (int)g.level, "level should be 5");
    TEST_ASSERT(g.exp > 0, "exp should be set when set_xp=true");

    guy g2(FAMILY_MAGE);
    g2.upgrade_to_level(5, false);
    TEST_ASSERT_EQ(5, (int)g2.level, "level should be 5");
    TEST_ASSERT_EQ(0, (int)g2.exp, "exp should be 0 when set_xp=false");
}
REGISTER_TEST(test_upgrade_to_level_sets_level_and_xp);

// Verify upgrade from level 1 to 10 (level_diff=9)
// base deltas: s=72, d=54, c=72, it=72, a=9
void test_upgrade_to_level_large_diff()
{
    guy g(FAMILY_MAGE);
    Sint32 str0 = g.strength;
    Sint32 int0 = g.intelligence;

    g.upgrade_to_level(10);

    // Mage: s/2, it*2
    Sint32 str_delta = g.strength - str0;
    Sint32 int_delta = g.intelligence - int0;
    TEST_ASSERT_EQ(36, (int)str_delta, "mage str delta l1→10: 72/2=36");
    TEST_ASSERT_EQ(144, (int)int_delta, "mage int delta l1→10: 72*2=144");
}
REGISTER_TEST(test_upgrade_to_level_large_diff);
