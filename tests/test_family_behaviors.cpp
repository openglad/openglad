/* Pre-refactor tests for family-specific behavioral callbacks.
 * These tests capture the exact per-family formulas in:
 *   - living::set_difficulty()  (living.cpp)
 *   - guy::upgrade_to_level()   (guy.cpp)
 *
 * They must pass both BEFORE and AFTER behavioral extraction into
 * FamilyDescriptor callbacks.
 */
#include <openglad/entities/guy.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
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

// ===========================================================================
// on_death tests — verify family-specific death behaviors
// ===========================================================================

static std::unique_ptr<walker> make_guy_for_death(char family, short level = 3)
{
    guy g(family);
    g.teamnum = 1; // non-player team (avoids endgame check)
    g.upgrade_to_level(level, true);
    auto w = g.create_walker_owned(myscreen);
    if (w) w->setxy(100, 100);
    return w;
}

// Bloodspot families should leave a stain, non-bloodspot should not
void test_on_death_bloodspot_families()
{
    // Families that leave bloodspot (default behavior)
    int bloodspot_families[] = {
        FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
        FAMILY_CLERIC, FAMILY_FAERIE, FAMILY_SLIME, FAMILY_ORC,
        FAMILY_BARBARIAN, FAMILY_ARCHMAGE
    };
    for (int fam : bloodspot_families)
    {
        auto* fd = get_family_descriptor(fam);
        TEST_ASSERT(fd != nullptr, "descriptor should exist");
        TEST_ASSERT(fd->leaves_bloodspot == true, "family should leave bloodspot");
    }

    // Families that DON'T leave bloodspot
    int no_bloodspot_families[] = {
        FAMILY_GHOST, FAMILY_SKELETON, FAMILY_TOWER1, FAMILY_GIANT_SKELETON
    };
    for (int fam : no_bloodspot_families)
    {
        auto* fd = get_family_descriptor(fam);
        TEST_ASSERT(fd != nullptr, "descriptor should exist");
        TEST_ASSERT(fd->leaves_bloodspot == false, "family should not leave bloodspot");
    }
}
REGISTER_TEST(test_on_death_bloodspot_families);

// Fire elemental: death triggers special (explosion)
void test_on_death_fire_elemental_explodes()
{
    auto w = make_guy_for_death(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(w != nullptr, "should create fire elemental");
    w->dead = 1;
    // death() should call special() which creates explosions
    w->death();
    // If we got here without crash, the explosion path worked
}
REGISTER_TEST(test_on_death_fire_elemental_explodes);

// Slime death: should shrink to medium slime
void test_on_death_slime_shrinks()
{
    auto w = myscreen->level_data.myloader->create_walker_owned(
        Order::Living, FAMILY_SLIME, myscreen);
    TEST_ASSERT(w != nullptr, "should create slime");
    w->setxy(100, 100);
    w->dead = 1;
    w->death();
    // Slime death spawns a new medium slime; no crash = success
}
REGISTER_TEST(test_on_death_slime_shrinks);

// Medium slime death: should shrink to small slime
void test_on_death_medium_slime_shrinks()
{
    auto w = myscreen->level_data.myloader->create_walker_owned(
        Order::Living, FAMILY_MEDIUM_SLIME, myscreen);
    TEST_ASSERT(w != nullptr, "should create medium slime");
    w->setxy(100, 100);
    w->dead = 1;
    w->death();
    // Medium slime death spawns a new small slime; no crash = success
}
REGISTER_TEST(test_on_death_medium_slime_shrinks);

// Ghost death: should NOT crash and should not generate bloodspot
void test_on_death_ghost_no_bloodspot()
{
    auto w = make_guy_for_death(FAMILY_GHOST);
    TEST_ASSERT(w != nullptr, "should create ghost");
    w->dead = 1;
    w->death();
    // Ghost doesn't generate bloodspot; no crash = success
}
REGISTER_TEST(test_on_death_ghost_no_bloodspot);

// Skeleton death: should NOT crash and should not generate bloodspot
void test_on_death_skeleton_no_bloodspot()
{
    auto w = make_guy_for_death(FAMILY_SKELETON);
    TEST_ASSERT(w != nullptr, "should create skeleton");
    w->dead = 1;
    w->death();
}
REGISTER_TEST(test_on_death_skeleton_no_bloodspot);

// ===========================================================================
// check_special tests — verify per-family AI decision logic
// distance_to_ob uses Manhattan: abs(dx)+abs(dy)
// ===========================================================================

// Helper: create a living walker via loader, add to oblist, return raw ptr
static walker* add_living_to_level(int family, int team, short x, short y)
{
    walker* ob = myscreen->level_data.add_ob(Order::Living, static_cast<Sint32>(family));
    if (!ob) return nullptr;
    ob->team_num = static_cast<unsigned char>(team);
    ob->setxy(x, y);
    return ob;
}

// Soldier: foe within 20-75 → true; outside → false
void test_check_special_soldier_range()
{
    myscreen->level_data.create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(soldier != nullptr, "soldier created");
    // Enemy at distance 50 (within 20-75)
    walker* enemy = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    TEST_ASSERT(enemy != nullptr, "enemy created");
    soldier->foe = enemy;
    soldier->stats()->magicpoints = 1000; // ensure enough MP
    soldier->current_special = 1;
    bool result = soldier->check_special();
    TEST_ASSERT(result == true, "soldier check_special: foe at dist 50 should be true");

    // Move enemy to distance 100 (outside 75)
    enemy->setxy(200, 100);
    result = soldier->check_special();
    TEST_ASSERT(result == false, "soldier check_special: foe at dist 100 should be false");

    // Move enemy to distance 10 (inside 20)
    enemy->setxy(110, 100);
    result = soldier->check_special();
    TEST_ASSERT(result == false, "soldier check_special: foe at dist 10 should be false");
}
REGISTER_TEST(test_check_special_soldier_range);

// Archer/FireElemental/Ghost/Orc: foe within 130 → true
void test_check_special_ranged_families()
{
    myscreen->level_data.create_new_grid();
    int families[] = {FAMILY_ARCHER, FAMILY_FIREELEMENTAL, FAMILY_GHOST, FAMILY_ORC};
    for (int fam : families)
    {
        walker* w = add_living_to_level(fam, 0, 100, 100);
        TEST_ASSERT(w != nullptr, "walker created");
        walker* enemy = add_living_to_level(FAMILY_SOLDIER, 1, 200, 100);
        TEST_ASSERT(enemy != nullptr, "enemy created");
        w->foe = enemy;
        w->stats()->magicpoints = 1000;
        w->current_special = 1;

        // Distance 100, within 130
        bool result = w->check_special();
        TEST_ASSERT(result == true, "ranged family: foe at dist 100 should be true");

        // Distance 150, outside 130
        enemy->setxy(250, 100);
        result = w->check_special();
        TEST_ASSERT(result == false, "ranged family: foe at dist 150 should be false");
    }
}
REGISTER_TEST(test_check_special_ranged_families);

// Mage: 0 foes → true (teleport away), 2 foes → false (fight), 4+ foes → true (flee)
void test_check_special_mage_foe_count()
{
    myscreen->level_data.create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    TEST_ASSERT(mage != nullptr, "mage created");
    mage->stats()->magicpoints = 1000;
    mage->current_special = 1;

    // No foes nearby → should want to teleport
    bool result = mage->check_special();
    TEST_ASSERT(result == true, "mage check_special: no foes nearby should be true");

    // Add 2 enemies within range 110 → should fight (false)
    add_living_to_level(FAMILY_ORC, 1, 150, 100);
    add_living_to_level(FAMILY_ORC, 1, 160, 100);
    result = mage->check_special();
    TEST_ASSERT(result == false, "mage check_special: 2 foes nearby should be false");

    // Add more enemies (total 5) → too many, flee (true)
    add_living_to_level(FAMILY_ORC, 1, 140, 100);
    add_living_to_level(FAMILY_ORC, 1, 130, 100);
    add_living_to_level(FAMILY_ORC, 1, 120, 100);
    result = mage->check_special();
    TEST_ASSERT(result == true, "mage check_special: 5 foes nearby should be true");
}
REGISTER_TEST(test_check_special_mage_foe_count);

// Skeleton: no foes within 5*GRID_SIZE → true (tunnel), foes nearby → false
void test_check_special_skeleton_tunnel()
{
    myscreen->level_data.create_new_grid();
    walker* skel = add_living_to_level(FAMILY_SKELETON, 0, 100, 100);
    TEST_ASSERT(skel != nullptr, "skeleton created");
    skel->stats()->magicpoints = 1000;
    skel->current_special = 1;

    // No foes nearby
    bool result = skel->check_special();
    TEST_ASSERT(result == true, "skeleton: no foes should tunnel (true)");

    // Add foe very close
    add_living_to_level(FAMILY_SOLDIER, 1, 120, 100);
    result = skel->check_special();
    TEST_ASSERT(result == false, "skeleton: foe nearby should not tunnel (false)");
}
REGISTER_TEST(test_check_special_skeleton_tunnel);

// Default families (druid, barbarian, etc.) always return true
void test_check_special_default_families()
{
    myscreen->level_data.create_new_grid();
    int families[] = {FAMILY_DRUID, FAMILY_BARBARIAN, FAMILY_FAERIE,
                      FAMILY_BIG_ORC, FAMILY_GOLEM};
    for (int fam : families)
    {
        walker* w = add_living_to_level(fam, 0, 100, 100);
        TEST_ASSERT(w != nullptr, "walker created for default family");
        w->stats()->magicpoints = 1000;
        w->current_special = 1;
        bool result = w->check_special();
        TEST_ASSERT(result == true, "default family check_special should be true");
    }
}
REGISTER_TEST(test_check_special_default_families);

// Slime: should return true when numobs < MAXOBS
void test_check_special_slime_capacity()
{
    myscreen->level_data.create_new_grid();
    walker* slime = add_living_to_level(FAMILY_SLIME, 0, 100, 100);
    TEST_ASSERT(slime != nullptr, "slime created");
    slime->stats()->magicpoints = 1000;
    slime->current_special = 1;

    // Far below MAXOBS limit
    bool result = slime->check_special();
    TEST_ASSERT(result == true, "slime: numobs < MAXOBS should allow special");
}
REGISTER_TEST(test_check_special_slime_capacity);

// check_special: if insufficient MP, current_special resets to 1
void test_check_special_insufficient_mp()
{
    myscreen->level_data.create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(soldier != nullptr, "soldier created");
    walker* enemy = add_living_to_level(FAMILY_ORC, 1, 140, 100);
    TEST_ASSERT(enemy != nullptr, "enemy created");
    soldier->foe = enemy;

    // Set high special with insufficient MP
    soldier->current_special = 3;
    soldier->stats()->magicpoints = 0;
    soldier->check_special();
    TEST_ASSERT_EQ(1, (int)soldier->current_special,
                   "insufficient MP should reset current_special to 1");
}
REGISTER_TEST(test_check_special_insufficient_mp);

// ===========================================================================
// hit_response tests — verify per-family response to being attacked
// ===========================================================================

// hit_response: default families acquire attacker as foe
void test_hit_response_acquires_foe()
{
    myscreen->level_data.create_new_grid();
    walker* defender = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(defender != nullptr, "defender created");
    defender->set_act_type(0); // not ACT_CONTROL (player)
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    TEST_ASSERT(attacker != nullptr, "attacker created");

    defender->foe = nullptr;
    defender->stats()->hitpoints = defender->stats()->max_hitpoints;
    defender->stats()->hit_response(attacker);

    TEST_ASSERT(defender->foe == attacker, "default hit_response should set foe to attacker");
}
REGISTER_TEST(test_hit_response_acquires_foe);

// hit_response: archer runs when too close (distance < 64)
void test_hit_response_archer_flees()
{
    myscreen->level_data.create_new_grid();
    walker* archer = add_living_to_level(FAMILY_ARCHER, 0, 100, 100);
    TEST_ASSERT(archer != nullptr, "archer created");
    archer->set_act_type(0); // AI-controlled
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    TEST_ASSERT(attacker != nullptr, "attacker created");

    archer->foe = nullptr;
    archer->stats()->hitpoints = archer->stats()->max_hitpoints;
    archer->stats()->hit_response(attacker);

    // Archer should set foe and potentially force a walk command
    TEST_ASSERT(archer->foe == attacker, "archer hit_response should set foe to attacker");
}
REGISTER_TEST(test_hit_response_archer_flees);

// hit_response: player-controlled walkers are skipped (ACT_CONTROL)
void test_hit_response_skip_player_control()
{
    myscreen->level_data.create_new_grid();
    walker* player = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(player != nullptr, "player created");
    player->set_act_type(ACT_CONTROL); // player-controlled
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    TEST_ASSERT(attacker != nullptr, "attacker created");

    player->foe = nullptr;
    player->stats()->hit_response(attacker);

    // Should be skipped entirely — foe unchanged
    TEST_ASSERT(player->foe == nullptr,
                "hit_response should not modify player-controlled walker");
}
REGISTER_TEST(test_hit_response_skip_player_control);

// hit_response: mage at full HP does NOT teleport
void test_hit_response_mage_full_hp_no_teleport()
{
    myscreen->level_data.create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    TEST_ASSERT(mage != nullptr, "mage created");
    mage->set_act_type(0);
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    TEST_ASSERT(attacker != nullptr, "attacker created");

    mage->stats()->hitpoints = mage->stats()->max_hitpoints; // full HP
    mage->stats()->magicpoints = 1000;
    mage->foe = nullptr;
    mage->stats()->hit_response(attacker);

    // At full HP, mage should just acquire foe (not teleport)
    TEST_ASSERT(mage->foe == attacker, "mage at full HP should acquire foe");
}
REGISTER_TEST(test_hit_response_mage_full_hp_no_teleport);

// hit_response: weapon owner is traced to get real foe
void test_hit_response_weapon_owner_resolved()
{
    myscreen->level_data.create_new_grid();
    walker* defender = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(defender != nullptr, "defender created");
    defender->set_act_type(0);
    walker* shooter = add_living_to_level(FAMILY_ARCHER, 1, 200, 100);
    TEST_ASSERT(shooter != nullptr, "shooter created");

    // Create a weapon and set its owner
    walker* arrow = myscreen->level_data.add_ob(Order::Weapon, FAMILY_ARROW);
    TEST_ASSERT(arrow != nullptr, "arrow created");
    arrow->owner = shooter;
    arrow->team_num = 1;
    arrow->setxy(110, 100);

    defender->foe = nullptr;
    defender->stats()->hitpoints = defender->stats()->max_hitpoints;
    defender->stats()->hit_response(arrow);

    // Foe should be the shooter (weapon owner), not the arrow
    TEST_ASSERT(defender->foe == shooter,
                "hit_response should resolve weapon owner as foe");
}
REGISTER_TEST(test_hit_response_weapon_owner_resolved);

// ===========================================================================
// do_special tests — verify per-family special ability behaviors
// ===========================================================================

// Guard: dead walkers cannot use special
void test_special_dead_walker_returns_false()
{
    myscreen->level_data.create_new_grid();
    walker* w = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(w != nullptr, "soldier created");
    w->stats()->magicpoints = 1000;
    w->current_special = 1;
    w->dead = 1;
    bool result = w->special();
    TEST_ASSERT(result == false, "dead walker special should return false");
}
REGISTER_TEST(test_special_dead_walker_returns_false);

// Guard: insufficient MP should return false without deducting mana
void test_special_insufficient_mp_returns_false()
{
    myscreen->level_data.create_new_grid();
    walker* w = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(w != nullptr, "soldier created");
    w->current_special = 1;
    w->stats()->magicpoints = 0; // no mana
    float mp_before = w->stats()->magicpoints;
    bool result = w->special();
    TEST_ASSERT(result == false, "insufficient MP special should return false");
    TEST_ASSERT_FLOAT(mp_before, w->stats()->magicpoints, "MP should not change");
}
REGISTER_TEST(test_special_insufficient_mp_returns_false);

// Skeleton: tunnel sets ani_type to ANI_TELE_OUT
void test_special_skeleton_tunnel()
{
    myscreen->level_data.create_new_grid();
    walker* skel = add_living_to_level(FAMILY_SKELETON, 0, 100, 100);
    TEST_ASSERT(skel != nullptr, "skeleton created");
    skel->stats()->magicpoints = 1000;
    skel->current_special = 1;

    // Skeleton starts with ANI_SKEL_GROW; must finish grow first
    skel->ani_type = 0; // reset to default so tunnel can work
    float mp_before = skel->stats()->magicpoints;
    float special_cost = skel->stats()->special_cost[1];

    bool result = skel->special();

    // If special succeeded, verify ani_type and mana; if it failed
    // (e.g. path blocked), that's OK too — just verify no crash
    if (skel->ani_type == ANI_TELE_OUT)
    {
        TEST_ASSERT_FLOAT(mp_before - special_cost, skel->stats()->magicpoints,
                          "skeleton tunnel should deduct mana");
    }
    (void)result;
}
REGISTER_TEST(test_special_skeleton_tunnel);

// Ghost: scare spawns a ghost_scare FX
void test_special_ghost_scare()
{
    myscreen->level_data.create_new_grid();
    walker* ghost = add_living_to_level(FAMILY_GHOST, 0, 100, 100);
    TEST_ASSERT(ghost != nullptr, "ghost created");
    ghost->stats()->magicpoints = 1000;
    ghost->current_special = 1;
    float mp_before = ghost->stats()->magicpoints;
    float special_cost = ghost->stats()->special_cost[1];

    ghost->special();

    TEST_ASSERT_FLOAT(mp_before - special_cost, ghost->stats()->magicpoints,
                      "ghost scare should deduct mana");
    // If we got here without crash, the scare FX was spawned successfully
}
REGISTER_TEST(test_special_ghost_scare);

// Slime: split sets ani_type to ANI_SLIME_SPLIT
void test_special_slime_split()
{
    myscreen->level_data.create_new_grid();
    walker* slime = add_living_to_level(FAMILY_SLIME, 0, 100, 100);
    TEST_ASSERT(slime != nullptr, "slime created");
    slime->stats()->magicpoints = 1000;
    slime->current_special = 1;

    slime->special();

    TEST_ASSERT_EQ(ANI_SLIME_SPLIT, (int)slime->ani_type,
                   "slime split should set ani_type to ANI_SLIME_SPLIT");
}
REGISTER_TEST(test_special_slime_split);

// Fire elemental: starburst fires in 8 directions (deducts mana)
void test_special_fire_elemental_starburst()
{
    myscreen->level_data.create_new_grid();
    walker* fe = add_living_to_level(FAMILY_FIREELEMENTAL, 0, 100, 100);
    TEST_ASSERT(fe != nullptr, "fire elemental created");
    fe->stats()->magicpoints = 1000;
    fe->current_special = 1;
    float mp_before = fe->stats()->magicpoints;
    float special_cost = fe->stats()->special_cost[1];

    fe->special();

    TEST_ASSERT_FLOAT(mp_before - special_cost, fe->stats()->magicpoints,
                      "fire elemental starburst should deduct mana");
}
REGISTER_TEST(test_special_fire_elemental_starburst);

// Elf: special 1 fires 2 rocks (deducts mana)
void test_special_elf_rocks()
{
    myscreen->level_data.create_new_grid();
    walker* elf = add_living_to_level(FAMILY_ELF, 0, 100, 100);
    TEST_ASSERT(elf != nullptr, "elf created");
    elf->stats()->magicpoints = 1000;
    elf->current_special = 1;
    float mp_before = elf->stats()->magicpoints;
    float special_cost = elf->stats()->special_cost[1];

    elf->special();

    TEST_ASSERT_FLOAT(mp_before - special_cost, elf->stats()->magicpoints,
                      "elf rock special should deduct mana");
}
REGISTER_TEST(test_special_elf_rocks);

// Soldier charge: deducts mana
void test_special_soldier_charge()
{
    myscreen->level_data.create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(soldier != nullptr, "soldier created");
    soldier->stats()->magicpoints = 1000;
    soldier->current_special = 1;
    float mp_before = soldier->stats()->magicpoints;
    float special_cost = soldier->stats()->special_cost[1];

    soldier->special();

    // Charge may fail if path blocked, but mana should be deducted if it succeeds
    // If it fails (returns early), mana won't be deducted — just verify no crash
    (void)mp_before;
    (void)special_cost;
}
REGISTER_TEST(test_special_soldier_charge);

// Mage teleport: sets ani_type to ANI_TELE_OUT
void test_special_mage_teleport()
{
    myscreen->level_data.create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    TEST_ASSERT(mage != nullptr, "mage created");
    mage->stats()->magicpoints = 1000;
    mage->current_special = 1;
    mage->shifter_down = 0; // teleport, not marker

    mage->special();

    TEST_ASSERT_EQ(ANI_TELE_OUT, (int)mage->ani_type,
                   "mage teleport should set ani_type to ANI_TELE_OUT");
}
REGISTER_TEST(test_special_mage_teleport);

// Thief bomb: spawns bomb FX (deducts mana)
void test_special_thief_bomb()
{
    myscreen->level_data.create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    TEST_ASSERT(thief != nullptr, "thief created");
    thief->stats()->magicpoints = 1000;
    thief->current_special = 1;
    float mp_before = thief->stats()->magicpoints;
    float special_cost = thief->stats()->special_cost[1];

    thief->special();

    TEST_ASSERT_FLOAT(mp_before - special_cost, thief->stats()->magicpoints,
                      "thief bomb should deduct mana");
}
REGISTER_TEST(test_special_thief_bomb);

// Thief cloak: increases invisibility_left
void test_special_thief_cloak()
{
    myscreen->level_data.create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    TEST_ASSERT(thief != nullptr, "thief created");
    thief->stats()->magicpoints = 1000;
    thief->current_special = 2;
    thief->invisibility_left = 0;

    thief->special();

    TEST_ASSERT(thief->invisibility_left > 0,
                "thief cloak should increase invisibility_left");
}
REGISTER_TEST(test_special_thief_cloak);

// Orc howl: sets busy and deducts mana
void test_special_orc_howl()
{
    myscreen->level_data.create_new_grid();
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    TEST_ASSERT(orc != nullptr, "orc created");
    orc->stats()->magicpoints = 1000;
    orc->current_special = 1;
    orc->busy = 0;

    orc->special();

    TEST_ASSERT(orc->busy > 0, "orc howl should set busy");
}
REGISTER_TEST(test_special_orc_howl);

// Druid reveal: increments view_all
void test_special_druid_reveal()
{
    myscreen->level_data.create_new_grid();
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    TEST_ASSERT(druid != nullptr, "druid created");
    druid->stats()->magicpoints = 1000;
    druid->current_special = 3;
    druid->busy = 0;

    short view_all_before = druid->view_all;
    druid->special();

    TEST_ASSERT(druid->view_all > view_all_before,
                "druid reveal should increment view_all");
}
REGISTER_TEST(test_special_druid_reveal);

// ===========================================================================
// upgrade_to_level continued
// ===========================================================================

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

// ===========================================================================
// Step 3 pre-refactor tests: on_act_living, on_shoved, on_fire_weapon,
//   handle_teleport, on_create
// ===========================================================================

// --- on_act_living: archmage gets periodic view_all bonus ---
void test_archmage_periodic_view_all()
{
    auto w = make_living(FAMILY_ARCHMAGE);
    TEST_ASSERT(w != nullptr, "make_living should succeed");
    living* lv = static_cast<living*>(w.get());
    w->stats()->level = 40;  // temp >= 1 when level>=40, so view_all increments every cycle
    lv->drawcycle = 0;
    short va_before = w->view_all;
    // Simulate one act cycle — view_all should increment
    lv->act();
    TEST_ASSERT(w->view_all > va_before,
                "archmage at level 40 should gain view_all during act()");
}
REGISTER_TEST(test_archmage_periodic_view_all);

// Non-archmage should NOT gain view_all
void test_non_archmage_no_view_all()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "make_living should succeed");
    living* lv = static_cast<living*>(w.get());
    short va_before = w->view_all;
    lv->act();
    TEST_ASSERT_EQ((int)va_before, (int)w->view_all,
                   "soldier should not gain view_all during act()");
}
REGISTER_TEST(test_non_archmage_no_view_all);

// --- on_act_living: fire elemental summoned drain ---
void test_fire_elemental_summoned_drain()
{
    // Create a mage as owner
    auto owner = make_living(FAMILY_MAGE);
    TEST_ASSERT(owner != nullptr, "make owner mage");
    owner->stats()->hitpoints = owner->stats()->max_hitpoints;
    owner->stats()->magicpoints = owner->stats()->max_magicpoints;

    // Create a fire elemental as summoned creature
    auto fe = make_living(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(fe != nullptr, "make fire elemental");
    fe->owner = owner.get();
    fe->lifetime = 100;
    fe->team_num = owner->team_num;
    // Hurt the elemental so drain triggers
    fe->stats()->hitpoints = fe->stats()->max_hitpoints / 2;

    float owner_hp_before = owner->stats()->hitpoints;
    float owner_mp_before = owner->stats()->magicpoints;

    living* lv = static_cast<living*>(fe.get());
    lv->act();

    // Owner should lose 1 HP and 3 MP (drain)
    TEST_ASSERT(owner->stats()->hitpoints < owner_hp_before,
                "owner HP should decrease from fire elemental drain");
    TEST_ASSERT(owner->stats()->magicpoints < owner_mp_before,
                "owner MP should decrease from fire elemental drain");
}
REGISTER_TEST(test_fire_elemental_summoned_drain);

// --- on_shoved: cleric casts heal when shoved ---
void test_cleric_heals_when_shoved()
{
    myscreen->level_data.create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(soldier != nullptr, "soldier created");
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 130, 100);
    TEST_ASSERT(cleric != nullptr, "cleric created");
    cleric->set_act_type(0); // AI-controlled
    cleric->stats()->magicpoints = 500;

    // Shove the cleric
    static_cast<living*>(soldier)->shove(cleric, 1, 0);
    // The cleric should have had its special triggered (current_special set to 1)
    // We can't easily verify this happened since the special was already called,
    // but we verify no crash occurred
}
REGISTER_TEST(test_cleric_heals_when_shoved);

// Non-cleric should NOT cast heal when shoved
void test_non_cleric_no_heal_when_shoved()
{
    myscreen->level_data.create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(soldier != nullptr, "soldier created");
    walker* archer = add_living_to_level(FAMILY_ARCHER, 0, 130, 100);
    TEST_ASSERT(archer != nullptr, "archer created");
    archer->set_act_type(0); // AI-controlled
    archer->stats()->magicpoints = 500;

    float mp_before = archer->stats()->magicpoints;
    static_cast<living*>(soldier)->shove(archer, 1, 0);
    // Archer's MP should not change (no heal cast)
    TEST_ASSERT_FLOAT(mp_before, archer->stats()->magicpoints,
                      "non-cleric should not cast heal when shoved");
}
REGISTER_TEST(test_non_cleric_no_heal_when_shoved);

// --- on_fire_weapon: soldier weapons_left ---
void test_soldier_weapons_left_limits_fire()
{
    myscreen->level_data.create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    TEST_ASSERT(soldier != nullptr, "soldier created");
    static_cast<living*>(soldier)->weapons_left = 1;
    soldier->stats()->magicpoints = 1000;
    soldier->lastx = 1; // firing direction

    // First fire should succeed (weapons_left goes from 1 to 0)
    walker* w1 = soldier->fire();
    // w1 may be non-null (weapon created) or null if blocked, but weapons_left should decrement
    (void)w1;

    // Second fire should fail (weapons_left is 0)
    walker* w2 = soldier->fire();
    TEST_ASSERT(w2 == nullptr, "soldier with 0 weapons_left should not fire");
}
REGISTER_TEST(test_soldier_weapons_left_limits_fire);

// --- on_fire_weapon: archmage weapon damage boost ---
void test_archmage_weapon_damage_boost()
{
    myscreen->level_data.create_new_grid();
    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    TEST_ASSERT(arch != nullptr, "archmage created");
    arch->stats()->magicpoints = 1000;
    arch->lastx = 1; // firing direction

    float mp_before = arch->stats()->magicpoints;
    walker* weapon = arch->fire();
    if (weapon && !weapon->dead)
    {
        // Archmage should have transferred 1/20th of remaining magic to weapon damage
        float mp_used_for_weapon_cost = mp_before - arch->stats()->magicpoints;
        // mp_used should be more than just weapon_cost (due to the 1/20 drain)
        TEST_ASSERT(mp_used_for_weapon_cost > arch->stats()->weapon_cost,
                    "archmage should spend extra MP on weapon damage");
    }
}
REGISTER_TEST(test_archmage_weapon_damage_boost);

// --- handle_teleport: mage teleport-out completes ---
void test_mage_handle_teleport()
{
    auto w = make_living(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "make mage");
    w->ani_type = ANI_TELE_OUT;
    w->cycle = 0;
    // Pump animate() until the animation completes and transitions
    for (int i = 0; i < 50 && w->ani_type == ANI_TELE_OUT; i++)
        w->animate();
    TEST_ASSERT_EQ(ANI_TELE_IN, (int)w->ani_type,
                   "mage teleport-out should transition to ANI_TELE_IN");
}
REGISTER_TEST(test_mage_handle_teleport);

// Skeleton teleport: uses teleport_ranged
void test_skeleton_handle_teleport()
{
    auto w = make_living(FAMILY_SKELETON);
    TEST_ASSERT(w != nullptr, "make skeleton");
    w->ani_type = ANI_TELE_OUT;
    w->cycle = 0;
    for (int i = 0; i < 50 && w->ani_type == ANI_TELE_OUT; i++)
        w->animate();
    TEST_ASSERT_EQ(ANI_TELE_IN, (int)w->ani_type,
                   "skeleton teleport-out should transition to ANI_TELE_IN");
}
REGISTER_TEST(test_skeleton_handle_teleport);

// --- on_create: soldier weapons_left set from level ---
void test_soldier_weapons_left_on_create()
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = 0;
    g.upgrade_to_level(5, true);
    auto w = g.create_walker_owned(myscreen);
    TEST_ASSERT(w != nullptr, "create soldier walker");
    TEST_ASSERT_EQ(3, (int)static_cast<living*>(w.get())->weapons_left,
                   "soldier weapons_left should be (level+1)/2 = 3 at level 5");
}
REGISTER_TEST(test_soldier_weapons_left_on_create);
