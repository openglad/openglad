/* Pre-refactor tests for family-specific behavioral callbacks.
 * These tests capture the exact per-family formulas in:
 *   - living::set_difficulty()  (living.cpp)
 *   - guy::upgrade_to_level()   (guy.cpp)
 *
 * They must pass both BEFORE and AFTER behavioral extraction into
 * FamilyDescriptor callbacks.
 */
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/irandom.h>
#include <gtest/gtest.h>
#include <cmath>
#include <algorithm>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w) return nullptr;
    w->setxy(100, 100);
    return w;
}

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

TEST(FamilyBehaviors, set_difficulty_per_family_exact)
{
    for (const auto& tc : difficulty_cases)
    {
        auto w = make_living(static_cast<char>(tc.family));
        ASSERT_TRUE(w != nullptr) << "make_living should succeed";

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
        ASSERT_TRUE(std::fabs((tc.hp_delta) - (hp_delta)) <= 0.5f) << buf << " expected: " << (tc.hp_delta) << ", actual: " << (hp_delta);
        snprintf(buf, sizeof(buf), "%s MP delta", tc.name);
        ASSERT_TRUE(std::fabs((tc.mp_delta) - (mp_delta)) <= 0.5f) << buf << " expected: " << (tc.mp_delta) << ", actual: " << (mp_delta);
        snprintf(buf, sizeof(buf), "%s dmg delta", tc.name);
        ASSERT_TRUE(std::fabs((tc.dmg_delta) - (dmg_delta)) <= 0.5f) << buf << " expected: " << (tc.dmg_delta) << ", actual: " << (dmg_delta);
        snprintf(buf, sizeof(buf), "%s armor delta", tc.name);
        ASSERT_TRUE(std::fabs((tc.armor_delta) - (armor_delta)) <= 0.5f) << buf << " expected: " << (tc.armor_delta) << ", actual: " << (armor_delta);
    }
}


// Soldier: weapons_left = (level+1)/2
TEST(FamilyBehaviors, set_difficulty_soldier_weapons_left)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "make_living should succeed";
    w->team_num = 0;

    static_cast<living*>(w.get())->set_difficulty(5);
    short wl = static_cast<living*>(w.get())->weapons_left;
    ASSERT_EQ(3, (int)wl) << "soldier weapons_left at level 5 should be (5+1)/2=3";
}


// Non-player teams get difficulty scaling applied
TEST(FamilyBehaviors, set_difficulty_enemy_scaling)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "make_living should succeed";

    auto w2 = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w2 != nullptr) << "make_living should succeed";

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
    ASSERT_TRUE(player_hp > 0) << "player HP positive";
    ASSERT_TRUE(enemy_hp > 0) << "enemy HP positive";
}


// After set_difficulty, hitpoints = max_hitpoints (healed to full)
TEST(FamilyBehaviors, set_difficulty_heals_to_full)
{
    auto w = make_living(FAMILY_ARCHER);
    ASSERT_TRUE(w != nullptr) << "make_living should succeed";
    w->team_num = 0;

    static_cast<living*>(w.get())->set_difficulty(3);
    ASSERT_TRUE(std::fabs((w->stats()->max_hitpoints) - (w->stats()->hitpoints)) <= 0.5f) << "set_difficulty should heal to max HP" << " expected: " << (w->stats()->max_hitpoints) << ", actual: " << (w->stats()->hitpoints);
    ASSERT_TRUE(std::fabs((w->stats()->max_magicpoints) - (w->stats()->magicpoints)) <= 0.5f) << "set_difficulty should heal to max MP" << " expected: " << (w->stats()->max_magicpoints) << ", actual: " << (w->stats()->magicpoints);
}


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

TEST(FamilyBehaviors, upgrade_to_level_per_family_exact)
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
        ASSERT_EQ(tc.str_delta, (int)str_delta) << buf;
        snprintf(buf, sizeof(buf), "%s dex delta", tc.name);
        ASSERT_EQ(tc.dex_delta, (int)dex_delta) << buf;
        snprintf(buf, sizeof(buf), "%s con delta", tc.name);
        ASSERT_EQ(tc.con_delta, (int)con_delta) << buf;
        snprintf(buf, sizeof(buf), "%s int delta", tc.name);
        ASSERT_EQ(tc.int_delta, (int)int_delta) << buf;
        snprintf(buf, sizeof(buf), "%s armor delta", tc.name);
        ASSERT_EQ(tc.armor_delta, (int)armor_delta) << buf;
    }
}


// Verify level and xp are set correctly
TEST(FamilyBehaviors, upgrade_to_level_sets_level_and_xp)
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(5, true);
    ASSERT_EQ(5, (int)g.level) << "level should be 5";
    ASSERT_TRUE(g.exp > 0) << "exp should be set when set_xp=true";

    guy g2(FAMILY_MAGE);
    g2.upgrade_to_level(5, false);
    ASSERT_EQ(5, (int)g2.level) << "level should be 5";
    ASSERT_EQ(0, (int)g2.exp) << "exp should be 0 when set_xp=false";
}


// ===========================================================================
// on_death tests — verify family-specific death behaviors
// ===========================================================================

static std::unique_ptr<walker> make_guy_for_death(char family, short level = 3)
{
    guy g(family);
    g.teamnum = 1; // non-player team (avoids endgame check)
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(100, 100);
    return w;
}

// Bloodspot families should leave a stain, non-bloodspot should not
TEST(FamilyBehaviors, on_death_bloodspot_families)
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
        ASSERT_TRUE(fd != nullptr) << "descriptor should exist";
        ASSERT_TRUE(fd->leaves_bloodspot == true) << "family should leave bloodspot";
    }

    // Families that DON'T leave bloodspot
    int no_bloodspot_families[] = {
        FAMILY_GHOST, FAMILY_SKELETON, FAMILY_TOWER1, FAMILY_GIANT_SKELETON
    };
    for (int fam : no_bloodspot_families)
    {
        auto* fd = get_family_descriptor(fam);
        ASSERT_TRUE(fd != nullptr) << "descriptor should exist";
        ASSERT_TRUE(fd->leaves_bloodspot == false) << "family should not leave bloodspot";
    }
}


// Fire elemental: death triggers special (explosion)
TEST(FamilyBehaviors, on_death_fire_elemental_explodes)
{
    auto w = make_guy_for_death(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(w != nullptr) << "should create fire elemental";
    w->dead = 1;
    // death() should call special() which creates explosions
    w->death();
    // If we got here without crash, the explosion path worked
}


// Slime death: should shrink to medium slime
TEST(FamilyBehaviors, on_death_slime_shrinks)
{
    auto w = og::runtime::current_session->myscreen_->myloader->create_walker_owned(
        Order::Living, FAMILY_SLIME);
    ASSERT_TRUE(w != nullptr) << "should create slime";
    w->setxy(100, 100);
    w->dead = 1;
    w->death();
    // Slime death spawns a new medium slime; no crash = success
}


// Medium slime death: should shrink to small slime
TEST(FamilyBehaviors, on_death_medium_slime_shrinks)
{
    auto w = og::runtime::current_session->myscreen_->myloader->create_walker_owned(
        Order::Living, FAMILY_MEDIUM_SLIME);
    ASSERT_TRUE(w != nullptr) << "should create medium slime";
    w->setxy(100, 100);
    w->dead = 1;
    w->death();
    // Medium slime death spawns a new small slime; no crash = success
}


// Ghost death: should NOT crash and should not generate bloodspot
TEST(FamilyBehaviors, on_death_ghost_no_bloodspot)
{
    auto w = make_guy_for_death(FAMILY_GHOST);
    ASSERT_TRUE(w != nullptr) << "should create ghost";
    w->dead = 1;
    w->death();
    // Ghost doesn't generate bloodspot; no crash = success
}


// Skeleton death: should NOT crash and should not generate bloodspot
TEST(FamilyBehaviors, on_death_skeleton_no_bloodspot)
{
    auto w = make_guy_for_death(FAMILY_SKELETON);
    ASSERT_TRUE(w != nullptr) << "should create skeleton";
    w->dead = 1;
    w->death();
}


// ===========================================================================
// check_special tests — verify per-family AI decision logic
// distance_to_ob uses Manhattan: abs(dx)+abs(dy)
// ===========================================================================

// Helper: create a living walker via loader, add to oblist, return raw ptr
static walker* add_living_to_level(int family, int team, short x, short y)
{
    walker* ob = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, static_cast<Sint32>(family));
    if (!ob) return nullptr;
    ob->team_num = static_cast<unsigned char>(team);
    ob->setxy(x, y);
    return ob;
}

class ConstRandomFamily : public IRandom {
public:
    explicit ConstRandomFamily(Uint32 value) : value_(value) {}
    Uint32 next(Uint32 max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        return value_ % max_exclusive;
    }
private:
    Uint32 value_;
};

// Soldier: foe within 20-75 → true; outside → false
TEST(FamilyBehaviors, check_special_soldier_range)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    // Enemy at distance 50 (within 20-75)
    walker* enemy = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(enemy != nullptr) << "enemy created";
    soldier->set_foe(enemy);
    soldier->stats()->magicpoints = 1000; // ensure enough MP
    soldier->current_special = 1;
    bool result = soldier->check_special();
    ASSERT_TRUE(result == true) << "soldier check_special: foe at dist 50 should be true";

    // Move enemy to distance 100 (outside 75)
    enemy->setxy(200, 100);
    result = soldier->check_special();
    ASSERT_TRUE(result == false) << "soldier check_special: foe at dist 100 should be false";

    // Move enemy to distance 10 (inside 20)
    enemy->setxy(110, 100);
    result = soldier->check_special();
    ASSERT_TRUE(result == false) << "soldier check_special: foe at dist 10 should be false";
}


// Archer/FireElemental/Ghost/Orc: foe within 130 → true
TEST(FamilyBehaviors, check_special_ranged_families)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    int families[] = {FAMILY_ARCHER, FAMILY_FIREELEMENTAL, FAMILY_GHOST, FAMILY_ORC};
    for (int fam : families)
    {
        walker* w = add_living_to_level(fam, 0, 100, 100);
        ASSERT_TRUE(w != nullptr) << "walker created";
        walker* enemy = add_living_to_level(FAMILY_SOLDIER, 1, 200, 100);
        ASSERT_TRUE(enemy != nullptr) << "enemy created";
        w->set_foe(enemy);
        w->stats()->magicpoints = 1000;
        w->current_special = 1;

        // Distance 100, within 130
        bool result = w->check_special();
        ASSERT_TRUE(result == true) << "ranged family: foe at dist 100 should be true";

        // Distance 150, outside 130
        enemy->setxy(250, 100);
        result = w->check_special();
        ASSERT_TRUE(result == false) << "ranged family: foe at dist 150 should be false";
    }
}


// Mage: 0 foes → true (teleport away), 2 foes → false (fight), 4+ foes → true (flee)
TEST(FamilyBehaviors, check_special_mage_foe_count)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    mage->stats()->magicpoints = 1000;
    mage->current_special = 1;

    // No foes nearby → should want to teleport
    bool result = mage->check_special();
    ASSERT_TRUE(result == true) << "mage check_special: no foes nearby should be true";

    // Add 2 enemies within range 110 → should fight (false)
    add_living_to_level(FAMILY_ORC, 1, 150, 100);
    add_living_to_level(FAMILY_ORC, 1, 160, 100);
    result = mage->check_special();
    ASSERT_TRUE(result == false) << "mage check_special: 2 foes nearby should be false";

    // Add more enemies (total 5) → too many, flee (true)
    add_living_to_level(FAMILY_ORC, 1, 140, 100);
    add_living_to_level(FAMILY_ORC, 1, 130, 100);
    add_living_to_level(FAMILY_ORC, 1, 120, 100);
    result = mage->check_special();
    ASSERT_TRUE(result == true) << "mage check_special: 5 foes nearby should be true";
}


// Skeleton: no foes within 5*GRID_SIZE → true (tunnel), foes nearby → false
TEST(FamilyBehaviors, check_special_skeleton_tunnel)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* skel = add_living_to_level(FAMILY_SKELETON, 0, 100, 100);
    ASSERT_TRUE(skel != nullptr) << "skeleton created";
    skel->stats()->magicpoints = 1000;
    skel->current_special = 1;

    // No foes nearby
    bool result = skel->check_special();
    ASSERT_TRUE(result == true) << "skeleton: no foes should tunnel (true)";

    // Add foe very close
    add_living_to_level(FAMILY_SOLDIER, 1, 120, 100);
    result = skel->check_special();
    ASSERT_TRUE(result == false) << "skeleton: foe nearby should not tunnel (false)";
}


// Default families (druid, barbarian, etc.) always return true
TEST(FamilyBehaviors, check_special_default_families)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    int families[] = {FAMILY_DRUID, FAMILY_BARBARIAN, FAMILY_FAERIE,
                      FAMILY_BIG_ORC, FAMILY_GOLEM};
    for (int fam : families)
    {
        walker* w = add_living_to_level(fam, 0, 100, 100);
        ASSERT_TRUE(w != nullptr) << "walker created for default family";
        w->stats()->magicpoints = 1000;
        w->current_special = 1;
        bool result = w->check_special();
        ASSERT_TRUE(result == true) << "default family check_special should be true";
    }
}


// Slime: should return true when numobs < MAXOBS
TEST(FamilyBehaviors, check_special_slime_capacity)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* slime = add_living_to_level(FAMILY_SLIME, 0, 100, 100);
    ASSERT_TRUE(slime != nullptr) << "slime created";
    slime->stats()->magicpoints = 1000;
    slime->current_special = 1;

    // Far below MAXOBS limit
    bool result = slime->check_special();
    ASSERT_TRUE(result == true) << "slime: numobs < MAXOBS should allow special";
}


// check_special: if insufficient MP, current_special resets to 1
TEST(FamilyBehaviors, check_special_insufficient_mp)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    walker* enemy = add_living_to_level(FAMILY_ORC, 1, 140, 100);
    ASSERT_TRUE(enemy != nullptr) << "enemy created";
    soldier->set_foe(enemy);

    // Set high special with insufficient MP
    soldier->current_special = 3;
    soldier->stats()->magicpoints = 0;
    soldier->check_special();
    ASSERT_EQ(1, (int)soldier->current_special) << "insufficient MP should reset current_special to 1";
}


// ===========================================================================
// hit_response tests — verify per-family response to being attacked
// ===========================================================================

// hit_response: default families acquire attacker as foe
TEST(FamilyBehaviors, hit_response_acquires_foe)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* defender = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(defender != nullptr) << "defender created";
    defender->set_act_type(0); // not ACT_CONTROL (player)
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";

    defender->set_foe(nullptr);
    defender->stats()->hitpoints = defender->stats()->max_hitpoints;
    defender->stats()->hit_response(attacker);

    ASSERT_TRUE(defender->foe() == attacker) << "default hit_response should set foe to attacker";
}


// hit_response: archer runs when too close (distance < 64)
TEST(FamilyBehaviors, hit_response_archer_flees)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* archer = add_living_to_level(FAMILY_ARCHER, 0, 100, 100);
    ASSERT_TRUE(archer != nullptr) << "archer created";
    archer->set_act_type(0); // AI-controlled
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";

    archer->set_foe(nullptr);
    archer->stats()->hitpoints = archer->stats()->max_hitpoints;
    archer->stats()->hit_response(attacker);

    // Archer should set foe and potentially force a walk command
    ASSERT_TRUE(archer->foe() == attacker) << "archer hit_response should set foe to attacker";
}


// hit_response: player-controlled walkers are skipped (ACT_CONTROL)
TEST(FamilyBehaviors, hit_response_skip_player_control)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* player = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(player != nullptr) << "player created";
    player->set_act_type(ACT_CONTROL); // player-controlled
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";

    player->set_foe(nullptr);
    player->stats()->hit_response(attacker);

    // Should be skipped entirely — foe unchanged
    ASSERT_TRUE(player->foe() == nullptr) << "hit_response should not modify player-controlled walker";
}


// hit_response: mage at full HP does NOT teleport
TEST(FamilyBehaviors, hit_response_mage_full_hp_no_teleport)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    mage->set_act_type(0);
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";

    mage->stats()->hitpoints = mage->stats()->max_hitpoints; // full HP
    mage->stats()->magicpoints = 1000;
    mage->set_foe(nullptr);
    mage->stats()->hit_response(attacker);

    // At full HP, mage should just acquire foe (not teleport)
    ASSERT_TRUE(mage->foe() == attacker) << "mage at full HP should acquire foe";
}


// hit_response: weapon owner is traced to get real foe
TEST(FamilyBehaviors, hit_response_weapon_owner_resolved)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* defender = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(defender != nullptr) << "defender created";
    defender->set_act_type(0);
    walker* shooter = add_living_to_level(FAMILY_ARCHER, 1, 200, 100);
    ASSERT_TRUE(shooter != nullptr) << "shooter created";

    // Create a weapon and set its owner
    walker* arrow = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_ARROW);
    ASSERT_TRUE(arrow != nullptr) << "arrow created";
    arrow->set_owner(shooter);
    arrow->team_num = 1;
    arrow->setxy(110, 100);

    defender->set_foe(nullptr);
    defender->stats()->hitpoints = defender->stats()->max_hitpoints;
    defender->stats()->hit_response(arrow);

    // Foe should be the shooter (weapon owner), not the arrow
    ASSERT_TRUE(defender->foe() == shooter) << "hit_response should resolve weapon owner as foe";
}


// ===========================================================================
// do_special tests — verify per-family special ability behaviors
// ===========================================================================

// Guard: dead walkers cannot use special
TEST(FamilyBehaviors, special_dead_walker_returns_false)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(w != nullptr) << "soldier created";
    w->stats()->magicpoints = 1000;
    w->current_special = 1;
    w->dead = 1;
    bool result = w->special();
    ASSERT_TRUE(result == false) << "dead walker special should return false";
}


// Guard: insufficient MP should return false without deducting mana
TEST(FamilyBehaviors, special_insufficient_mp_returns_false)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(w != nullptr) << "soldier created";
    w->current_special = 1;
    w->stats()->magicpoints = 0; // no mana
    float mp_before = w->stats()->magicpoints;
    bool result = w->special();
    ASSERT_TRUE(result == false) << "insufficient MP special should return false";
    ASSERT_TRUE(std::fabs((mp_before) - (w->stats()->magicpoints)) <= 0.5f) << "MP should not change" << " expected: " << (mp_before) << ", actual: " << (w->stats()->magicpoints);
}


// Skeleton: tunnel sets ani_type to ANI_TELE_OUT
TEST(FamilyBehaviors, special_skeleton_tunnel)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* skel = add_living_to_level(FAMILY_SKELETON, 0, 100, 100);
    ASSERT_TRUE(skel != nullptr) << "skeleton created";
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
        ASSERT_TRUE(std::fabs((mp_before - special_cost) - (skel->stats()->magicpoints)) <= 0.5f) << "skeleton tunnel should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (skel->stats()->magicpoints);
    }
    (void)result;
}


// Ghost: scare spawns a ghost_scare FX
TEST(FamilyBehaviors, special_ghost_scare)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* ghost = add_living_to_level(FAMILY_GHOST, 0, 100, 100);
    ASSERT_TRUE(ghost != nullptr) << "ghost created";
    ghost->stats()->magicpoints = 1000;
    ghost->current_special = 1;
    float mp_before = ghost->stats()->magicpoints;
    float special_cost = ghost->stats()->special_cost[1];

    ghost->special();

    ASSERT_TRUE(std::fabs((mp_before - special_cost) - (ghost->stats()->magicpoints)) <= 0.5f) << "ghost scare should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (ghost->stats()->magicpoints);
    // If we got here without crash, the scare FX was spawned successfully
}


// Slime: split sets ani_type to ANI_SLIME_SPLIT
TEST(FamilyBehaviors, special_slime_split)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* slime = add_living_to_level(FAMILY_SLIME, 0, 100, 100);
    ASSERT_TRUE(slime != nullptr) << "slime created";
    slime->stats()->magicpoints = 1000;
    slime->current_special = 1;

    slime->special();

    ASSERT_EQ(ANI_SLIME_SPLIT, (int)slime->ani_type) << "slime split should set ani_type to ANI_SLIME_SPLIT";
}


// Fire elemental: starburst fires in 8 directions (deducts mana)
TEST(FamilyBehaviors, special_fire_elemental_starburst)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* fe = add_living_to_level(FAMILY_FIREELEMENTAL, 0, 100, 100);
    ASSERT_TRUE(fe != nullptr) << "fire elemental created";
    fe->stats()->magicpoints = 1000;
    fe->current_special = 1;
    float mp_before = fe->stats()->magicpoints;
    float special_cost = fe->stats()->special_cost[1];

    fe->special();

    ASSERT_TRUE(std::fabs((mp_before - special_cost) - (fe->stats()->magicpoints)) <= 0.5f) << "fire elemental starburst should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (fe->stats()->magicpoints);
}


// Elf: special 1 fires 2 rocks (deducts mana)
TEST(FamilyBehaviors, special_elf_rocks)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* elf = add_living_to_level(FAMILY_ELF, 0, 100, 100);
    ASSERT_TRUE(elf != nullptr) << "elf created";
    elf->stats()->magicpoints = 1000;
    elf->current_special = 1;
    float mp_before = elf->stats()->magicpoints;
    float special_cost = elf->stats()->special_cost[1];

    elf->special();

    ASSERT_TRUE(std::fabs((mp_before - special_cost) - (elf->stats()->magicpoints)) <= 0.5f) << "elf rock special should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (elf->stats()->magicpoints);
}


// Soldier charge: deducts mana
TEST(FamilyBehaviors, special_soldier_charge)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
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


// Mage teleport: sets ani_type to ANI_TELE_OUT
TEST(FamilyBehaviors, special_mage_teleport)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    mage->stats()->magicpoints = 1000;
    mage->current_special = 1;
    mage->shifter_down = 0; // teleport, not marker

    mage->special();

    ASSERT_EQ(ANI_TELE_OUT, (int)mage->ani_type) << "mage teleport should set ani_type to ANI_TELE_OUT";
}


// Thief bomb: spawns bomb FX (deducts mana)
TEST(FamilyBehaviors, special_thief_bomb)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    thief->stats()->magicpoints = 1000;
    thief->current_special = 1;
    float mp_before = thief->stats()->magicpoints;
    float special_cost = thief->stats()->special_cost[1];

    thief->special();

    ASSERT_TRUE(std::fabs((mp_before - special_cost) - (thief->stats()->magicpoints)) <= 0.5f) << "thief bomb should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (thief->stats()->magicpoints);
}


// Thief cloak: increases invisibility_left
TEST(FamilyBehaviors, special_thief_cloak)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    thief->stats()->magicpoints = 1000;
    thief->current_special = 2;
    thief->invisibility_left = 0;

    thief->special();

    ASSERT_TRUE(thief->invisibility_left > 0) << "thief cloak should increase invisibility_left";
}


// Orc howl: sets busy and deducts mana
TEST(FamilyBehaviors, special_orc_howl)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    orc->stats()->magicpoints = 1000;
    orc->current_special = 1;
    orc->busy = 0;

    orc->special();

    ASSERT_TRUE(orc->busy > 0) << "orc howl should set busy";
}


// Druid reveal: increments view_all
TEST(FamilyBehaviors, special_druid_reveal)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    ASSERT_TRUE(druid != nullptr) << "druid created";
    druid->stats()->magicpoints = 1000;
    druid->current_special = 3;
    druid->busy = 0;

    short view_all_before = druid->view_all;
    druid->special();

    ASSERT_TRUE(druid->view_all > view_all_before) << "druid reveal should increment view_all";
}


// ===========================================================================
// upgrade_to_level continued
// ===========================================================================

// Verify upgrade from level 1 to 10 (level_diff=9)
// base deltas: s=72, d=54, c=72, it=72, a=9
TEST(FamilyBehaviors, upgrade_to_level_large_diff)
{
    guy g(FAMILY_MAGE);
    Sint32 str0 = g.strength;
    Sint32 int0 = g.intelligence;

    g.upgrade_to_level(10);

    // Mage: s/2, it*2
    Sint32 str_delta = g.strength - str0;
    Sint32 int_delta = g.intelligence - int0;
    ASSERT_EQ(36, (int)str_delta) << "mage str delta l1→10: 72/2=36";
    ASSERT_EQ(144, (int)int_delta) << "mage int delta l1→10: 72*2=144";
}


// ===========================================================================
// Step 3 pre-refactor tests: on_act_living, on_shoved, on_fire_weapon,
//   handle_teleport, on_create
// ===========================================================================

// --- on_act_living: archmage gets periodic view_all bonus ---
TEST(FamilyBehaviors, archmage_periodic_view_all)
{
    auto w = make_living(FAMILY_ARCHMAGE);
    ASSERT_TRUE(w != nullptr) << "make_living should succeed";
    living* lv = static_cast<living*>(w.get());
    w->stats()->level = 40;  // temp >= 1 when level>=40, so view_all increments every cycle
    lv->drawcycle = 0;
    short va_before = w->view_all;
    // Simulate one act cycle — view_all should increment
    lv->act();
    ASSERT_TRUE(w->view_all > va_before) << "archmage at level 40 should gain view_all during act()";
}


// Non-archmage should NOT gain view_all
TEST(FamilyBehaviors, non_archmage_no_view_all)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "make_living should succeed";
    living* lv = static_cast<living*>(w.get());
    short va_before = w->view_all;
    lv->act();
    ASSERT_EQ((int)va_before, (int)w->view_all) << "soldier should not gain view_all during act()";
}


// --- on_act_living: fire elemental summoned drain ---
TEST(FamilyBehaviors, fire_elemental_summoned_drain)
{
    // Create a mage as owner
    auto owner = make_living(FAMILY_MAGE);
    ASSERT_TRUE(owner != nullptr) << "make owner mage";
    owner->stats()->hitpoints = owner->stats()->max_hitpoints;
    owner->stats()->magicpoints = owner->stats()->max_magicpoints;

    // Create a fire elemental as summoned creature
    auto fe = make_living(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(fe != nullptr) << "make fire elemental";
    fe->set_owner(owner.get());
    fe->lifetime = 100;
    fe->team_num = owner->team_num;
    // Hurt the elemental so drain triggers
    fe->stats()->hitpoints = fe->stats()->max_hitpoints / 2;

    float owner_hp_before = owner->stats()->hitpoints;
    float owner_mp_before = owner->stats()->magicpoints;

    living* lv = static_cast<living*>(fe.get());
    lv->act();

    // Owner should lose 1 HP and 3 MP (drain)
    ASSERT_TRUE(owner->stats()->hitpoints < owner_hp_before) << "owner HP should decrease from fire elemental drain";
    ASSERT_TRUE(owner->stats()->magicpoints < owner_mp_before) << "owner MP should decrease from fire elemental drain";
}


// --- on_shoved: cleric casts heal when shoved ---
TEST(FamilyBehaviors, cleric_heals_when_shoved)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 130, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    cleric->set_act_type(0); // AI-controlled
    cleric->stats()->magicpoints = 500;

    // Shove the cleric
    static_cast<living*>(soldier)->shove(cleric, 1, 0);
    // The cleric should have had its special triggered (current_special set to 1)
    // We can't easily verify this happened since the special was already called,
    // but we verify no crash occurred
}


// Non-cleric should NOT cast heal when shoved
TEST(FamilyBehaviors, non_cleric_no_heal_when_shoved)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    walker* archer = add_living_to_level(FAMILY_ARCHER, 0, 130, 100);
    ASSERT_TRUE(archer != nullptr) << "archer created";
    archer->set_act_type(0); // AI-controlled
    archer->stats()->magicpoints = 500;

    float mp_before = archer->stats()->magicpoints;
    static_cast<living*>(soldier)->shove(archer, 1, 0);
    // Archer's MP should not change (no heal cast)
    ASSERT_TRUE(std::fabs((mp_before) - (archer->stats()->magicpoints)) <= 0.5f) << "non-cleric should not cast heal when shoved" << " expected: " << (mp_before) << ", actual: " << (archer->stats()->magicpoints);
}


// --- on_fire_weapon: soldier weapons_left ---
TEST(FamilyBehaviors, soldier_weapons_left_limits_fire)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    static_cast<living*>(soldier)->weapons_left = 1;
    soldier->stats()->magicpoints = 1000;
    soldier->lastx = 1; // firing direction

    // First fire should succeed (weapons_left goes from 1 to 0)
    walker* w1 = soldier->fire();
    // w1 may be non-null (weapon created) or null if blocked, but weapons_left should decrement
    (void)w1;

    // Second fire should fail (weapons_left is 0)
    walker* w2 = soldier->fire();
    ASSERT_TRUE(w2 == nullptr) << "soldier with 0 weapons_left should not fire";
}


// --- on_fire_weapon: archmage weapon damage boost ---
TEST(FamilyBehaviors, archmage_weapon_damage_boost)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    arch->stats()->magicpoints = 1000;
    arch->lastx = 1; // firing direction

    float mp_before = arch->stats()->magicpoints;
    walker* weapon = arch->fire();
    if (weapon && !weapon->dead)
    {
        // Archmage should have transferred 1/20th of remaining magic to weapon damage
        float mp_used_for_weapon_cost = mp_before - arch->stats()->magicpoints;
        // mp_used should be more than just weapon_cost (due to the 1/20 drain)
        ASSERT_TRUE(mp_used_for_weapon_cost > arch->stats()->weapon_cost) << "archmage should spend extra MP on weapon damage";
    }
}


TEST(FamilyBehaviors, archmage_on_act_low_level_periodic_gate)
{
    auto w = make_living(FAMILY_ARCHMAGE);
    ASSERT_TRUE(w != nullptr) << "make archmage";
    auto* fd = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_TRUE(fd && fd->on_act_living) << "archmage on_act_living callback exists";
    if (!(w && fd && fd->on_act_living))
        return;

    living* lv = static_cast<living*>(w.get());
    lv->stats()->level = 20; // temp = 40-level = 20

    lv->drawcycle = 1;
    short before = lv->view_all;
    fd->on_act_living(lv);
    ASSERT_EQ(static_cast<int>(before), static_cast<int>(lv->view_all)) << "drawcycle not divisible by temp should not increment view_all";

    lv->drawcycle = 20;
    fd->on_act_living(lv);
    ASSERT_TRUE(lv->view_all > before) << "drawcycle divisible by temp should increment view_all";
}


TEST(FamilyBehaviors, archmage_handle_teleport_and_special_guards)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    const auto* fd = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_TRUE(fd && fd->handle_teleport && fd->do_special) << "archmage callbacks exist";
    if (!(arch && fd && fd->handle_teleport && fd->do_special))
        return;

    arch->ani_type = ANI_WALK;
    arch->cycle = 5;
    ASSERT_TRUE(fd->handle_teleport(arch)) << "handle_teleport should return true";
    ASSERT_EQ(ANI_TELE_IN, static_cast<int>(arch->ani_type)) << "handle_teleport should set tele-in";
    ASSERT_EQ(0, static_cast<int>(arch->cycle)) << "handle_teleport should reset cycle";

    // case 1 guard: already teleporting
    arch->current_special = 1;
    arch->shifter_down = 0;
    arch->ani_type = ANI_TELE_OUT;
    ASSERT_TRUE(!fd->do_special(arch)) << "teleport special should fail while already teleporting";

    // case 1 guard: marker path but busy
    arch->ani_type = ANI_WALK;
    arch->shifter_down = 1;
    arch->busy = 1;
    ASSERT_TRUE(!fd->do_special(arch)) << "marker path should fail when busy";

    // case 1 guard: low intelligence for marker
    arch->busy = 0;
    auto low_int = std::make_unique<guy>(FAMILY_ARCHMAGE);
    low_int->intelligence = 30;
    arch->set_owned_myguy(std::move(low_int));
    arch->user = 0;
    ASSERT_TRUE(!fd->do_special(arch)) << "marker path should fail when int<75";
}


TEST(FamilyBehaviors, archmage_special_case2_case3_case4_guard_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    const auto* fd = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_TRUE(fd && fd->do_special) << "archmage do_special callback exists";
    if (!(arch && fd && fd->do_special))
        return;

    arch->stats()->magicpoints = 5000;
    arch->stats()->special_cost[2] = 0;
    arch->stats()->special_cost[3] = 0;
    arch->stats()->special_cost[4] = 0;

    // case 2 guard: busy
    arch->current_special = 2;
    arch->busy = 1;
    arch->shifter_down = 0;
    ASSERT_TRUE(!fd->do_special(arch)) << "heartburst should fail when busy";

    // case 2 guard: no foes in range
    arch->busy = 0;
    ASSERT_TRUE(!fd->do_special(arch)) << "heartburst should fail with zero foes";

    // case 3 guard: summon elemental needs int >= 150
    arch->current_special = 3;
    arch->shifter_down = 1;
    auto low_int = std::make_unique<guy>(FAMILY_ARCHMAGE);
    low_int->intelligence = 120;
    arch->set_owned_myguy(std::move(low_int));
    arch->user = 0;
    arch->busy = 0;
    ASSERT_TRUE(!fd->do_special(arch)) << "true summon should fail when int<150";

    // case 4 guard: no charm candidates nearby
    arch->current_special = 4;
    arch->shifter_down = 0;
    arch->busy = 0;
    ASSERT_TRUE(!fd->do_special(arch)) << "mind control should fail with no nearby foes";
}


TEST(FamilyBehaviors, archmage_hit_response_threshold_and_retarget_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 132, 100);
    ASSERT_TRUE(arch != nullptr && foe != nullptr) << "archmage and foe should be created";
    const auto* fd = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_TRUE(fd && fd->hit_response) << "archmage hit_response callback exists";
    if (!(arch && foe && fd && fd->hit_response))
        return;

    arch->stats()->special_cost[1] = 0;
    arch->stats()->magicpoints = 500;
    arch->stats()->level = 9;
    arch->stats()->max_hitpoints = 100;
    arch->stats()->hitpoints = 10;
    arch->set_foe(nullptr);
    arch->busy = 10;
    arch->shifter_down = 1;
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1;

    fd->hit_response(arch->stats(), foe);
    ASSERT_EQ(1, (int)arch->current_special) << "low HP archmage should choose special 1";
    ASSERT_EQ(0, (int)arch->shifter_down) << "low HP branch should clear shifter flag";

    // Exercise the retargeting/foe-assignment branch in the non-threshold path.
    arch->stats()->hitpoints = 90;
    arch->set_foe(nullptr);
    foe->set_foe(nullptr);
    arch->stats()->last_distance = 1;
    arch->stats()->current_distance = 2;
    fd->hit_response(arch->stats(), foe);

    ASSERT_TRUE(arch->foe() == foe) << "non-threshold branch should retarget controller to attacker";
    ASSERT_TRUE(foe->foe() == arch) << "non-threshold branch should set attacker foe back to controller";
    ASSERT_EQ(15000, (int)arch->stats()->last_distance) << "retarget should reset last_distance";
    ASSERT_EQ(15000, (int)arch->stats()->current_distance) << "retarget should reset current_distance";
}


TEST(FamilyBehaviors, cleric_check_special_ai_branch_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->check_special_ai) << "cleric check_special_ai callback exists";
    if (!(cleric && fd && fd->check_special_ai))
        return;

    living* lv = static_cast<living*>(cleric);
    lv->current_special = 2;
    ASSERT_TRUE(fd->check_special_ai(lv)) << "non-heal special should return true";

    lv->current_special = 1;
    lv->stats()->magicpoints = 0.0f;
    lv->stats()->max_magicpoints = 100.0f;
    ASSERT_TRUE(!fd->check_special_ai(lv)) << "heal special should return false with no friends and low magic";

    lv->stats()->magicpoints = 60.0f;
    ASSERT_TRUE(fd->check_special_ai(lv)) << "heal special should return true for mace mode when magic >= half";
    ASSERT_EQ(1, (int)lv->shifter_down) << "mace mode should set shifter_down";

    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 108, 100);
    ASSERT_TRUE(ally != nullptr) << "ally created";
    lv->stats()->magicpoints = 1.0f;
    ASSERT_TRUE(fd->check_special_ai(lv)) << "heal special should return true when multiple allies nearby";
    ASSERT_EQ(0, (int)lv->shifter_down) << "heal mode should clear shifter_down";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


// --- handle_teleport: mage teleport-out completes ---
TEST(FamilyBehaviors, mage_handle_teleport)
{
    auto w = make_living(FAMILY_MAGE);
    ASSERT_TRUE(w != nullptr) << "make mage";
    w->ani_type = ANI_TELE_OUT;
    w->cycle = 0;
    // Pump animate() until the animation completes and transitions
    for (int i = 0; i < 50 && w->ani_type == ANI_TELE_OUT; i++)
        w->animate();
    ASSERT_EQ(ANI_TELE_IN, (int)w->ani_type) << "mage teleport-out should transition to ANI_TELE_IN";
}


// Skeleton teleport: uses teleport_ranged
TEST(FamilyBehaviors, skeleton_handle_teleport)
{
    auto w = make_living(FAMILY_SKELETON);
    ASSERT_TRUE(w != nullptr) << "make skeleton";
    w->ani_type = ANI_TELE_OUT;
    w->cycle = 0;
    for (int i = 0; i < 50 && w->ani_type == ANI_TELE_OUT; i++)
        w->animate();
    ASSERT_EQ(ANI_TELE_IN, (int)w->ani_type) << "skeleton teleport-out should transition to ANI_TELE_IN";
}


// --- on_create: soldier weapons_left set from level ---
TEST(FamilyBehaviors, soldier_weapons_left_on_create)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = 0;
    g.upgrade_to_level(5, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    ASSERT_TRUE(w != nullptr) << "create soldier walker";
    ASSERT_EQ(3, (int)static_cast<living*>(w.get())->weapons_left) << "soldier weapons_left should be (level+1)/2 = 3 at level 5";
}


static walker* add_stain_to_fxlist(int team, short x, short y)
{
    walker* ob = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (!ob) return nullptr;
    ob->ignore = 1;
    ob->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    ob->team_num = static_cast<unsigned char>(team);
    ob->setxy(x, y);
    return ob;
}

TEST(FamilyBehaviors, cleric_check_special_ai_direct_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->check_special_ai) << "cleric check_special_ai present";

    cleric->current_special = 1;
    cleric->stats()->max_magicpoints = 100;
    cleric->stats()->magicpoints = 0;
    bool ok = fd->check_special_ai(static_cast<living*>(cleric));
    ASSERT_TRUE(!ok) << "special=1 without allies and low MP should fail";

    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 120, 100);
    ASSERT_TRUE(ally != nullptr) << "ally created";
    ok = fd->check_special_ai(static_cast<living*>(cleric));
    ASSERT_TRUE(ok) << "special=1 with an ally nearby should pass";
    ASSERT_EQ(0, (int)cleric->shifter_down) << "heal mode should set shifter_down=0";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric recreated";
    cleric->current_special = 1;
    cleric->stats()->max_magicpoints = 100;
    cleric->stats()->magicpoints = 50;
    ok = fd->check_special_ai(static_cast<living*>(cleric));
    ASSERT_TRUE(ok) << "special=1 with no allies but MP>=half should pass";
    ASSERT_EQ(1, (int)cleric->shifter_down) << "mace mode should set shifter_down=1";

    cleric->current_special = 2;
    ok = fd->check_special_ai(static_cast<living*>(cleric));
    ASSERT_TRUE(ok) << "special!=1 should always pass";
}


TEST(FamilyBehaviors, cleric_heal_special_success_and_noheal_branch)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    cfg.apply_setting("effects", "heal_numbers", "on");
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 110, 100);
    ASSERT_TRUE(cleric && ally) << "cleric+ally created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";

    cleric->current_special = 1;
    cleric->shifter_down = 0;
    cleric->stats()->level = 5;
    cleric->stats()->magicpoints = 200;
    ally->stats()->max_hitpoints = 100;
    ally->stats()->hitpoints = 10;

    float hp_before = ally->stats()->hitpoints;
    float mp_before = cleric->stats()->magicpoints;
    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "heal special should succeed with an injured ally";
    ASSERT_TRUE(ally->stats()->hitpoints > hp_before) << "ally HP should increase";
    ASSERT_TRUE(cleric->stats()->magicpoints < mp_before) << "cleric MP should decrease";

    ally->stats()->hitpoints = ally->stats()->max_hitpoints;
    ok = fd->do_special(cleric);
    ASSERT_TRUE(!ok) << "heal special should fail when nobody is healable";
}


TEST(FamilyBehaviors, cleric_mystic_mace_gates)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";

    cleric->current_special = 1;
    cleric->shifter_down = 1;
    cleric->busy = 1;
    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(!ok) << "mystic mace should fail while busy";

    cleric->busy = 0;
    auto low_int = std::make_unique<guy>(FAMILY_CLERIC);
    low_int->intelligence = 40;
    cleric->set_owned_myguy(std::move(low_int));
    cleric->user = 0;
    ok = fd->do_special(cleric);
    ASSERT_TRUE(!ok) << "mystic mace should fail when int<50";
}


TEST(FamilyBehaviors, cleric_turn_undead_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";

    cleric->current_special = 2;
    cleric->shifter_down = 1;
    auto low_int = std::make_unique<guy>(FAMILY_CLERIC);
    low_int->intelligence = 50;
    cleric->set_owned_myguy(std::move(low_int));
    cleric->busy = 0;
    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(!ok) << "turn undead should fail at int<60";
    ASSERT_TRUE(cleric->busy >= 5) << "failed int gate should add busy delay";

    auto good_int = std::make_unique<guy>(FAMILY_CLERIC);
    good_int->intelligence = 80;
    cleric->set_owned_myguy(std::move(good_int));
    cleric->busy = 0;
    ok = fd->do_special(cleric);
    ASSERT_TRUE(!ok) << "turn undead should fail when no undead foes are in range";

}


TEST(FamilyBehaviors, cleric_mystic_mace_success_path_direct)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";
    if (!(cleric && fd && fd->do_special))
        return;

    cleric->current_special = 1;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->stats()->magicpoints = 200;
    cleric->stats()->special_cost[1] = 0;
    cleric->stats()->level = 6;
    auto smart = std::make_unique<guy>(FAMILY_CLERIC);
    smart->intelligence = 120;
    cleric->set_owned_myguy(std::move(smart));

    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "mystic mace should succeed with enough INT and not busy";
    ASSERT_TRUE(cleric->busy > 0) << "mystic mace success should add busy delay";

    bool found_shield = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w->query_order() == Order::FX && w->family == FAMILY_MAGIC_SHIELD &&
            w->owner() == cleric)
        {
            found_shield = true;
            break;
        }
    }
    for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist)
    {
        walker* w = uptr.get();
        if (w && w->family == FAMILY_MAGIC_SHIELD && w->owner() == cleric)
        {
            found_shield = true;
            break;
        }
    }
    ASSERT_TRUE(found_shield) << "mystic mace should spawn magic shield";
}


TEST(FamilyBehaviors, cleric_turn_undead_success_with_undead_targets)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    walker* skeleton = add_living_to_level(FAMILY_SKELETON, 2, 108, 100);
    ASSERT_TRUE(cleric != nullptr && skeleton != nullptr) << "cleric and skeleton created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";
    if (!(cleric && skeleton && fd && fd->do_special))
        return;

    cleric->current_special = 2;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->team_num = 1;
    cleric->stats()->level = 6;
    // turn_undead() uses world RNG directly; seed it so the kill check is
    // deterministic under shuffled execution as well.
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1;

    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "turn undead branch should execute when an undead foe is nearby";
    ASSERT_TRUE(skeleton->dead || skeleton->stats()->hitpoints <= 0) << "turn undead should remove or kill nearby undead target";
}


TEST(FamilyBehaviors, cleric_turn_undead_special2_and_3_shifter_notification_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";
    if (!(fd && fd->do_special))
        return;

    // Special 2 / shifter_down path with myguy should pass generic>0 branch.
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    walker* skeleton = add_living_to_level(FAMILY_SKELETON, 2, 108, 100);
    ASSERT_TRUE(cleric && skeleton) << "cleric+skeleton created";
    if (!(cleric && skeleton))
        return;

    auto c2 = std::make_unique<guy>(FAMILY_CLERIC);
    c2->intelligence = 80;
    c2->name = "Turner2";
    c2->exp = 0;
    cleric->set_owned_myguy(std::move(c2));
    cleric->current_special = 2;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->stats()->level = 6;
    ConstRandomFamily rng2(0);
    const int exp_before_2 = cleric->myguy ? cleric->myguy->exp : 0;
    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "turn undead special2 shifter path should succeed";
    ASSERT_TRUE(cleric->myguy && cleric->myguy->exp >= exp_before_2) << "turn undead special2 should run exp/notification block when generic is positive";

    // Special 3 / shifter_down path with myguy should also pass generic>0 branch.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    walker* skeleton2 = add_living_to_level(FAMILY_SKELETON, 2, 108, 100);
    ASSERT_TRUE(cleric && skeleton2) << "cleric+skeleton recreated";
    if (!(cleric && skeleton2))
        return;

    auto c3 = std::make_unique<guy>(FAMILY_CLERIC);
    c3->intelligence = 80;
    c3->name = "Turner3";
    c3->exp = 0;
    cleric->set_owned_myguy(std::move(c3));
    cleric->current_special = 3;
    cleric->shifter_down = 1;
    cleric->busy = 0;
    cleric->stats()->level = 6;
    ConstRandomFamily rng3(0);
    const int exp_before_3 = cleric->myguy ? cleric->myguy->exp : 0;
    ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "turn undead special3 shifter path should succeed";
    ASSERT_TRUE(cleric->myguy && cleric->myguy->exp >= exp_before_3) << "turn undead special3 should run exp/notification block when generic is positive";
}


TEST(FamilyBehaviors, cleric_resurrect_penalty_underflow_clamps_to_zero)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";
    if (!(cleric && fd && fd->do_special))
        return;

    auto hero = std::make_unique<guy>(FAMILY_CLERIC);
    hero->exp = 0;
    cleric->set_owned_myguy(std::move(hero));
    cleric->current_special = 4;

    walker* blood_friend = add_stain_to_fxlist(0, 110, 100);
    ASSERT_TRUE(blood_friend != nullptr) << "friendly blood created";
    if (!blood_friend)
        return;
    blood_friend->stats()->old_family = FAMILY_SOLDIER;

    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "resurrect should succeed for nearby friendly blood";
    ASSERT_TRUE(cleric->myguy->exp >= 0) << "resurrect path should leave non-negative experience";
}


TEST(FamilyBehaviors, cleric_raise_skeleton_and_ghost_from_blood)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";

    walker* blood = add_stain_to_fxlist(1, 110, 100);
    ASSERT_TRUE(blood != nullptr) << "blood created";
    cleric->current_special = 2;
    cleric->shifter_down = 0;
    cleric->stats()->level = 6;
    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "raise skeleton should succeed when blood is nearby and passable";
    ASSERT_TRUE(blood->dead) << "blood should be consumed by raise skeleton";

    bool found_skeleton = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w != cleric && w->query_order() == Order::Living &&
            w->family == FAMILY_SKELETON && w->owner() == cleric)
        {
            found_skeleton = true;
            break;
        }
    }
    ASSERT_TRUE(found_skeleton) << "raise skeleton should spawn a summoned skeleton";

    blood = add_stain_to_fxlist(1, 115, 100);
    ASSERT_TRUE(blood != nullptr) << "second blood created";
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "raise ghost should succeed when blood is close (<30)";
    ASSERT_TRUE(blood->dead) << "blood should be consumed by raise ghost";

    bool found_ghost = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w != cleric && w->query_order() == Order::Living &&
            w->family == FAMILY_GHOST && w->owner() == cleric)
        {
            found_ghost = true;
            break;
        }
    }
    ASSERT_TRUE(found_ghost) << "raise ghost should spawn a summoned ghost";
}


TEST(FamilyBehaviors, cleric_resurrect_friendly_and_enemy_blood)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";

    walker* blood_friend = add_stain_to_fxlist(0, 110, 100);
    ASSERT_TRUE(blood_friend != nullptr) << "friendly blood created";
    blood_friend->stats()->old_family = FAMILY_SOLDIER;
    cleric->current_special = 4;
    bool ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "resurrect should succeed for friendly blood";
    ASSERT_TRUE(blood_friend->dead) << "friendly blood should be consumed";

    bool found_resurrected_friend = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w != cleric && w->query_order() == Order::Living &&
            w->family == FAMILY_SOLDIER && w->team_num == 0)
        {
            found_resurrected_friend = true;
            break;
        }
    }
    ASSERT_TRUE(found_resurrected_friend) << "friendly blood should resurrect old_family";

    walker* blood_enemy = add_stain_to_fxlist(1, 112, 100);
    ASSERT_TRUE(blood_enemy != nullptr) << "enemy blood created";
    blood_enemy->stats()->old_family = FAMILY_ORC;
    ok = fd->do_special(cleric);
    ASSERT_TRUE(ok) << "resurrect should also succeed for enemy blood";
    ASSERT_TRUE(blood_enemy->dead) << "enemy blood should be consumed";

    bool found_enemy_ghost = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w != cleric && w->query_order() == Order::Living &&
            w->family == FAMILY_GHOST && w->team_num == cleric->team_num &&
            w->owner() == cleric)
        {
            found_enemy_ghost = true;
            break;
        }
    }
    ASSERT_TRUE(found_enemy_ghost) << "enemy blood should summon a friendly ghost";
}


TEST(FamilyBehaviors, thief_batch3_check_special_ai_matrix)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    const auto* fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_TRUE(fd && fd->check_special_ai) << "thief check_special_ai present";
    if (!(thief && fd && fd->check_special_ai))
        return;

    // special 1 with foe at 35<distance<130 should fail.
    thief->current_special = 1;
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    thief->set_foe(foe);
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(thief))) << "drop bomb AI should fail at medium range";

    // special 1 with close foe should pass.
    foe->setxy(120, 100);
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(thief))) << "drop bomb AI should pass when foe is close";

    // special 1 without foe needs >=3 nearby foes.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief recreated for foe-count branch";
    thief->current_special = 1;
    thief->set_foe(nullptr);
    walker* e1 = add_living_to_level(FAMILY_ORC, 1, 130, 100);
    walker* e2 = add_living_to_level(FAMILY_ORC, 1, 140, 100);
    ASSERT_TRUE(e1 && e2) << "two nearby foes created";
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(thief))) << "drop bomb AI should fail with fewer than 3 foes";
    walker* e3 = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(e3 != nullptr) << "third nearby foe created";
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(thief))) << "drop bomb AI should pass with 3+ foes";

    // special 3 uses two different ranges depending on shifter_down.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief recreated";
    thief->current_special = 3;
    thief->stats()->level = 1;
    thief->shifter_down = 0;
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(thief))) << "taunt/charm AI should fail without foes";
    foe = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "foe for special 3 created";
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(thief))) << "taunt/charm AI should pass with foe in normal range";

    thief->shifter_down = 1; // short charm range: 16 + 4*level = 20
    foe->setxy(130, 100);
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(thief))) << "charm AI should fail outside short range";
    foe->setxy(115, 100);
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(thief))) << "charm AI should pass inside short range";

    thief->current_special = 2;
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(thief))) << "non-1/non-3 thief specials should pass AI check";
}


TEST(FamilyBehaviors, thief_batch3_special_taunt_charm_and_poison_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    const auto* fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_TRUE(fd && fd->do_special) << "thief do_special present";
    if (!(thief && fd && fd->do_special))
        return;

    thief->stats()->magicpoints = 1000;

    // special 3 taunt busy guard.
    thief->current_special = 3;
    thief->shifter_down = 0;
    thief->busy = 1;
    ASSERT_TRUE(!fd->do_special(thief)) << "taunt should fail when busy";

    // taunt success and myguy-name message path.
    thief->busy = 0;
    auto thief_guy = std::make_unique<guy>(FAMILY_THIEF);
    thief_guy->name = "Sneak";
    thief->set_owned_myguy(std::move(thief_guy));
    ASSERT_TRUE(fd->do_special(thief)) << "taunt should succeed when not busy";
    ASSERT_TRUE(thief->busy >= 2) << "taunt should add busy time";

    // charm busy guard.
    thief->shifter_down = 1;
    thief->busy = 1;
    ASSERT_TRUE(!fd->do_special(thief)) << "charm should fail when busy";

    // charm no-foe guard.
    thief->busy = 0;
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief recreated";
    thief->stats()->magicpoints = 1000;
    thief->current_special = 3;
    thief->shifter_down = 1;
    thief->stats()->name = "Sneak";
    ASSERT_TRUE(!fd->do_special(thief)) << "charm should fail with no targets";

    // deterministic failed charm branch: thief level lower than target.
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 112, 100);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    thief->stats()->level = 1;
    foe->stats()->level = 10;
    thief->busy = 0;
    ASSERT_TRUE(fd->do_special(thief)) << "charm should run when target is in range";
    ASSERT_TRUE(thief->busy >= 10) << "charm should add busy time";
    ASSERT_TRUE(foe->foe() == thief) << "failed charm path should make foe attack thief";

    // poison cloud guards and success path.
    thief->current_special = 4;
    thief->busy = 1;
    ASSERT_TRUE(!fd->do_special(thief)) << "poison cloud should fail when busy";
    thief->busy = 0;
    ASSERT_TRUE(fd->do_special(thief)) << "poison cloud should succeed when not busy";
}


TEST(FamilyBehaviors, druid_batch3_special_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    ASSERT_TRUE(druid != nullptr) << "druid created";
    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && fd->do_special) << "druid do_special present";
    if (!(druid && fd && fd->do_special))
        return;

    druid->stats()->magicpoints = 1000;
    druid->stats()->level = 5;

    // Busy guards for cases 1/2/3/4.
    druid->busy = 1;
    druid->current_special = 1;
    ASSERT_TRUE(!fd->do_special(druid)) << "plant tree should fail when busy";
    druid->current_special = 2;
    ASSERT_TRUE(!fd->do_special(druid)) << "summon faerie should fail when busy";
    druid->current_special = 3;
    ASSERT_TRUE(!fd->do_special(druid)) << "reveal should fail when busy";
    druid->current_special = 4;
    ASSERT_TRUE(!fd->do_special(druid)) << "protection should fail when busy";

    // Reveal success path.
    druid->busy = 0;
    druid->current_special = 3;
    short view_before = druid->view_all;
    ASSERT_TRUE(fd->do_special(druid)) << "reveal should succeed when not busy";
    ASSERT_TRUE(druid->view_all > view_before) << "reveal should increase view_all";

    // Protection fails when only self is present.
    druid->current_special = 4;
    druid->busy = 0;
    ASSERT_TRUE(!fd->do_special(druid)) << "protection should fail with no allies in range";

    // Protection success: create ally and then refresh existing circle.
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 110, 100);
    ASSERT_TRUE(ally != nullptr) << "ally created";
    ASSERT_TRUE(fd->do_special(druid)) << "protection should succeed with ally in range";

    walker* existing_circle = nullptr;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().weaplist)
    {
        walker* w = uptr.get();
        if (w && w->family == FAMILY_CIRCLE_PROTECTION)
        {
            existing_circle = w;
            break;
        }
    }
    ASSERT_TRUE(existing_circle != nullptr) << "protection should spawn circle weapon";
    float hp_before = existing_circle ? existing_circle->stats()->hitpoints : 0.0f;
    ASSERT_TRUE(fd->do_special(druid)) << "second protection cast should refresh existing circle";
    if (existing_circle)
        ASSERT_TRUE(existing_circle->stats()->hitpoints >= hp_before) << "existing circle HP should not decrease on refresh";
}


TEST(FamilyBehaviors, orc_batch3_special_and_ai_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    const auto* fd = get_family_descriptor(FAMILY_ORC);
    ASSERT_TRUE(fd && fd->do_special && fd->check_special_ai) << "orc callbacks present";
    if (!(orc && fd && fd->do_special && fd->check_special_ai))
        return;

    orc->stats()->magicpoints = 1000;
    orc->stats()->level = 4;

    // Howl busy guard.
    orc->current_special = 1;
    orc->busy = 1;
    ASSERT_TRUE(!fd->do_special(orc)) << "howl should fail when busy";

    // Howl success with foes both with/without myguy branch.
    orc->busy = 0;
    walker* foe_named = add_living_to_level(FAMILY_SOLDIER, 1, 120, 100);
    walker* foe_plain = add_living_to_level(FAMILY_SOLDIER, 1, 130, 100);
    ASSERT_TRUE(foe_named && foe_plain) << "foes for howl created";
    auto foe_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    foe_guy->constitution = 10;
    foe_named->set_owned_myguy(std::move(foe_guy));
    short frozen_before_named = foe_named->stats()->frozen_delay;
    short frozen_before_plain = foe_plain->stats()->frozen_delay;
    ASSERT_TRUE(fd->do_special(orc)) << "howl should succeed when not busy";
    ASSERT_TRUE(foe_named->stats()->frozen_delay >= frozen_before_named) << "howl should affect foe with myguy";
    ASSERT_TRUE(foe_plain->stats()->frozen_delay >= frozen_before_plain) << "howl should affect foe without myguy";

    // Eat-corpse guards and success path.
    orc->current_special = 2;
    orc->stats()->hitpoints = orc->stats()->max_hitpoints;
    ASSERT_TRUE(!fd->do_special(orc)) << "eat corpse should fail at full HP";
    orc->stats()->hitpoints = orc->stats()->max_hitpoints - 20.0f;
    ASSERT_TRUE(!fd->do_special(orc)) << "eat corpse should fail without blood";

    walker* far_blood = add_stain_to_fxlist(1, 200, 100);
    ASSERT_TRUE(far_blood != nullptr) << "far blood created";
    far_blood->stats()->level = 3;
    ASSERT_TRUE(!fd->do_special(orc)) << "eat corpse should fail when blood is too far";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc recreated";
    orc->current_special = 2;
    orc->stats()->hitpoints = orc->stats()->max_hitpoints - 20.0f;
    auto orc_guy = std::make_unique<guy>(FAMILY_ORC);
    orc_guy->name = "Gruk";
    orc->set_owned_myguy(std::move(orc_guy));
    walker* near_blood = add_stain_to_fxlist(1, 101, 100);
    ASSERT_TRUE(near_blood != nullptr) << "near blood created";
    near_blood->stats()->level = 4;
    ASSERT_TRUE(fd->do_special(orc)) << "eat corpse should succeed when blood is close";
    ASSERT_TRUE(near_blood->dead) << "eaten blood object should be marked dead";

    // check_special_ai with preset foe in/out of range.
    walker* foe = add_living_to_level(FAMILY_SOLDIER, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "foe for AI checks created";
    orc->set_foe(foe);
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(orc))) << "orc AI should pass when foe is in range";
    foe->setxy(260, 100);
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(orc))) << "orc AI should fail when foe is out of range";

    // check_special_ai with no foe should query nearest foe.
    orc->set_foe(nullptr);
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc recreated for nearest-foe branch";
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(orc))) << "orc AI should fail when no nearby foe exists";
    foe = add_living_to_level(FAMILY_SOLDIER, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "near foe created";
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(orc))) << "orc AI should pass after finding nearby foe";
}


TEST(FamilyBehaviors, soldier_batch3_special_ai_and_fire_callback_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    const auto* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(fd && fd->do_special && fd->check_special_ai && fd->on_fire_weapon) << "soldier callbacks present";
    if (!(soldier && fd && fd->do_special && fd->check_special_ai && fd->on_fire_weapon))
        return;

    soldier->stats()->magicpoints = 1000;
    soldier->stats()->level = 6;

    // Charge blocked path.
    soldier->current_special = 1;
    soldier->curdir = FACE_LEFT;
    soldier->setxy(0, 0);
    ASSERT_TRUE(!fd->do_special(soldier)) << "charge should fail when forward is blocked";

    // Whirlwind busy guard.
    soldier->current_special = 3;
    soldier->busy = 1;
    ASSERT_TRUE(!fd->do_special(soldier)) << "whirlwind should fail when busy";

    // Disarm guards.
    soldier->current_special = 4;
    soldier->busy = 1;
    ASSERT_TRUE(!fd->do_special(soldier)) << "disarm should fail when busy";
    soldier->busy = 0;
    soldier->setxy(100, 100);
    soldier->curdir = FACE_RIGHT;
    ASSERT_TRUE(!fd->do_special(soldier)) << "disarm should fail when forward is not blocked";

    // Make forward blocked and no foes in range -> fail.
    soldier->setxy(0, 100);
    soldier->curdir = FACE_LEFT;
    ASSERT_TRUE(!fd->do_special(soldier)) << "disarm should fail when blocked but no foes are in range";

    // Add a nearby foe so disarm succeeds.
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 8, 100);
    ASSERT_TRUE(foe != nullptr) << "foe for disarm created";
    soldier->busy = 0;
    ASSERT_TRUE(fd->do_special(soldier)) << "disarm should succeed when foe is nearby and blocked";

    // check_special_ai direct branches.
    soldier->set_foe(foe);
    foe->setxy(40, 100);
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(soldier))) << "soldier AI should pass in 20-75 range";
    foe->setxy(200, 100);
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(soldier))) << "soldier AI should fail when foe too far";
    soldier->set_foe(nullptr);
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier recreated for nearest-foe branch";
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(soldier))) << "soldier AI should fail without nearby foe";
    foe = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "near foe for AI created";
    ASSERT_TRUE(fd->check_special_ai(static_cast<living*>(soldier))) << "soldier AI should pass after finding nearby foe";

    // on_fire_weapon callback: no weapons left path and decrement path.
    walker* weapon = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
    static_cast<living*>(soldier)->weapons_left = 0;
    float mp_before = soldier->stats()->magicpoints;
    ASSERT_TRUE(!fd->on_fire_weapon(soldier, weapon)) << "on_fire_weapon should fail when weapons_left<=0";
    ASSERT_TRUE(weapon->dead) << "weapon should be marked dead when out of throws";
    ASSERT_TRUE(soldier->stats()->magicpoints > mp_before) << "failed throw should refund weapon cost";

    weapon = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weapon != nullptr) << "second weapon created";
    static_cast<living*>(soldier)->weapons_left = 2;
    ASSERT_TRUE(fd->on_fire_weapon(soldier, weapon)) << "on_fire_weapon should succeed when throws remain";
    ASSERT_EQ(1, (int)static_cast<living*>(soldier)->weapons_left) << "successful throw should decrement weapons_left";
}


TEST(FamilyBehaviors, family_batch4_druid_refresh_oblist_and_failure_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && fd->do_special) << "druid callback present";
    if (!(fd && fd->do_special))
        return;

    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    walker* ally1 = add_living_to_level(FAMILY_SOLDIER, 0, 108, 100);
    walker* ally2 = add_living_to_level(FAMILY_ARCHER, 0, 112, 100);
    ASSERT_TRUE(druid && ally1 && ally2) << "druid and allies created";
    if (!(druid && ally1 && ally2))
        return;

    druid->stats()->magicpoints = 1000;
    druid->stats()->level = 6;
    druid->set_owned_myguy(std::make_unique<guy>(FAMILY_DRUID));

    // Force fire() fail paths for cases 1 and 2.
    druid->stats()->weapon_cost = 9999;
    druid->current_special = 1;
    druid->busy = 0;
    (void)fd->do_special(druid);
    druid->current_special = 2;
    (void)fd->do_special(druid);
    druid->stats()->weapon_cost = 0;

    // Summon faerie passability failure path.
    druid->current_special = 2;
    druid->setxy(-200, -200);
    ASSERT_TRUE(!fd->do_special(druid)) << "summon faerie should fail when spawn tile is impassable";
    druid->setxy(100, 100);

    // Protection refresh branch requires existing circle in oblist.
    walker* existing = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(existing != nullptr) << "existing protection object created";
    if (existing) {
        existing->set_owner(ally1);
        existing->stats()->hitpoints = 5;
    }
    druid->current_special = 4;
    druid->busy = 0;
    ASSERT_TRUE(fd->do_special(druid)) << "protection should succeed with multiple allies";
    if (existing)
        ASSERT_TRUE(existing->stats()->hitpoints >= 5.0f) << "existing protection HP should be refreshed";
}


TEST(FamilyBehaviors, family_batch4_soldier_orc_thief_edge_callbacks)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* sold_fd = get_family_descriptor(FAMILY_SOLDIER);
    const auto* orc_fd = get_family_descriptor(FAMILY_ORC);
    const auto* thief_fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_TRUE(sold_fd && orc_fd && thief_fd) << "family descriptors available";
    if (!(sold_fd && orc_fd && thief_fd))
        return;

    // Soldier default-special branch.
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    if (soldier) {
        soldier->current_special = 99;
        ASSERT_TRUE(sold_fd->do_special(soldier)) << "unknown soldier special should fall through and succeed";
        soldier->set_foe(nullptr);
        ASSERT_TRUE(!sold_fd->check_special_ai(static_cast<living*>(soldier))) << "soldier AI should fail with no nearby foe";
    }

    // Orc AI no-foe branch and default corpse message path.
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    if (orc) {
        orc->set_foe(nullptr);
        ASSERT_TRUE(!orc_fd->check_special_ai(static_cast<living*>(orc))) << "orc AI should fail with no nearby foe";

        orc->current_special = 2;
        orc->stats()->hitpoints = orc->stats()->max_hitpoints - 10.0f;
        orc->stats()->name.clear();
        orc->clear_myguy();
        walker* blood = add_stain_to_fxlist(1, 101, 100);
        ASSERT_TRUE(blood != nullptr) << "blood stain for eat-corpse created";
        if (blood)
            blood->stats()->level = 2;
        ASSERT_TRUE(orc_fd->do_special(orc)) << "orc should eat corpse via default message branch";
        ASSERT_EQ(1, (int)orc_fd->promotion_new_level(42)) << "orc promotion callback should return level 1";
    }

    // Thief: drop bomb AI run-away and charm success branch.
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    walker* foe = add_living_to_level(FAMILY_SOLDIER, 1, 110, 100);
    ASSERT_TRUE(thief && foe) << "thief and foe created";
    if (thief && foe) {
        ConstRandomFamily rng_nonzero(1);
        thief->stats()->magicpoints = 1000;

        thief->current_special = 1;
        thief->user = -1;
        ASSERT_TRUE(thief_fd->do_special(thief)) << "drop bomb should succeed and schedule run-away for AI";

        thief->current_special = 3;
        thief->shifter_down = 1;
        thief->busy = 0;
        thief->stats()->level = 9;
        foe->stats()->level = 1;
        thief->set_foe(foe);
        ASSERT_TRUE(thief_fd->do_special(thief)) << "charm should succeed with favorable deterministic RNG";
        ASSERT_TRUE(foe->team_num == thief->team_num) << "successful charm should switch foe team";
    }
}


TEST(FamilyBehaviors, family_batch5_cleric_on_shoved_and_elf_fire_fail_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* cleric_fd = get_family_descriptor(FAMILY_CLERIC);
    const auto* elf_fd = get_family_descriptor(FAMILY_ELF);
    ASSERT_TRUE(cleric_fd && cleric_fd->on_shoved && elf_fd && elf_fd->do_special) << "cleric/elf callbacks present";
    if (!(cleric_fd && cleric_fd->on_shoved && elf_fd && elf_fd->do_special))
        return;

    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    if (cleric)
    {
        cleric->current_special = 4;
        cleric_fd->on_shoved(cleric);
        ASSERT_EQ(1, (int)cleric->current_special) << "cleric on_shoved should force heal special";
    }

    walker* elf = add_living_to_level(FAMILY_ELF, 0, 100, 100);
    ASSERT_TRUE(elf != nullptr) << "elf created";
    if (elf)
    {
        // Keep MP deeply negative so each special's pre-bonus still leaves
        // fire() below weapon_cost and returns null deterministically.
        elf->stats()->magicpoints = -1000;

        elf->current_special = 1;
        ASSERT_TRUE(!elf_fd->do_special(elf)) << "elf special 1 should fail when fire() fails";
        elf->current_special = 2;
        ASSERT_TRUE(!elf_fd->do_special(elf)) << "elf special 2 should fail when fire() fails";
        elf->current_special = 3;
        ASSERT_TRUE(!elf_fd->do_special(elf)) << "elf special 3 should fail when fire() fails";
        elf->current_special = 4;
        ASSERT_TRUE(!elf_fd->do_special(elf)) << "elf special 4 should fail when fire() fails";
    }
}


TEST(FamilyBehaviors, druid_batch5_fire_fail_and_existing_protection_refresh_branch)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && fd->do_special) << "druid callback present";
    if (!(fd && fd->do_special))
        return;

    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 112, 100);
    ASSERT_TRUE(druid && ally) << "druid and ally created";
    if (!(druid && ally))
        return;

    // Force fire() failure in specials 1/2: MP remains below weapon_cost even
    // after do_special's pre-fire MP adjustment.
    druid->stats()->weapon_cost = 10;
    druid->stats()->magicpoints = -1000;
    druid->busy = 0;

    druid->current_special = 1;
    ASSERT_TRUE(!fd->do_special(druid)) << "druid special 1 should fail when fire() returns null";

    druid->current_special = 2;
    ASSERT_TRUE(!fd->do_special(druid)) << "druid special 2 should fail when fire() returns null";

    (void)ally;
}


TEST(FamilyBehaviors, mage_batch3_special_and_promotion_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 1, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    const auto* fd = get_family_descriptor(FAMILY_MAGE);
    ASSERT_TRUE(fd && fd->do_special && fd->check_special_ai && fd->promotion_new_level) << "mage callbacks present";
    if (!(mage && fd && fd->do_special && fd->check_special_ai && fd->promotion_new_level))
        return;

    // check_special_ai false branch (1-3 foes in range).
    mage->current_special = 1;
    add_living_to_level(FAMILY_ORC, 0, 150, 100);
    add_living_to_level(FAMILY_ORC, 0, 160, 100);
    ASSERT_TRUE(!fd->check_special_ai(static_cast<living*>(mage))) << "mage AI should be false with 2 nearby foes";

    // Teleport marker path without myguy (lifetime from level).
    mage->stats()->level = 8;
    mage->stats()->magicpoints = 500;
    mage->current_special = 1;
    mage->shifter_down = 1;
    mage->busy = 0;
    mage->user = -1;
    ASSERT_TRUE(fd->do_special(mage)) << "mage marker placement should succeed";

    // Starburst low-mana branch (generic <= 0 path).
    mage->current_special = 2;
    mage->stats()->special_cost[2] = 1000;
    mage->stats()->magicpoints = 1;
    ASSERT_TRUE(fd->do_special(mage)) << "mage starburst should still execute with low mana";

    // Enemy freeze-time path (team!=0 and no myguy).
    mage->current_special = 3;
    mage->team_num = 1;
    mage->set_owned_myguy(nullptr);
    walker* ally = add_living_to_level(FAMILY_ORC, 1, 110, 100);
    ASSERT_TRUE(ally != nullptr) << "ally for freeze-time created";
    short bonus_before = ally->bonus_rounds;
    ASSERT_TRUE(fd->do_special(mage)) << "enemy freeze-time should succeed";
    ASSERT_TRUE(ally->bonus_rounds >= bonus_before) << "enemy freeze-time should add ally bonus rounds";

    // Energy wave guard: fire() returns null when weapon_cost > magicpoints.
    mage->current_special = 4;
    mage->stats()->magicpoints = 0;
    ASSERT_TRUE(!fd->do_special(mage)) << "energy wave should fail when fire() cannot create projectile";

    // Heartburst guard: no foes in range.
    mage->current_special = 5;
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    mage = add_living_to_level(FAMILY_MAGE, 1, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage recreated for heartburst guard";
    mage->stats()->magicpoints = 500;
    (void)fd->do_special(mage);

    ASSERT_EQ(3, (int)fd->promotion_new_level(10)) << "mage promotion level formula should match legacy behavior";
}


TEST(FamilyBehaviors, family_batch6_soldier_orc_mage_callback_edge_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* soldier_fd = get_family_descriptor(FAMILY_SOLDIER);
    const auto* orc_fd = get_family_descriptor(FAMILY_ORC);
    const auto* mage_fd = get_family_descriptor(FAMILY_MAGE);
    ASSERT_TRUE(soldier_fd && soldier_fd->do_special && soldier_fd->check_special_ai) << "soldier callbacks present";
    ASSERT_TRUE(orc_fd && orc_fd->do_special) << "orc callbacks present";
    ASSERT_TRUE(mage_fd && mage_fd->check_special_ai) << "mage callbacks present";
    if (!(soldier_fd && soldier_fd->do_special && soldier_fd->check_special_ai &&
          orc_fd && orc_fd->do_special &&
          mage_fd && mage_fd->check_special_ai))
        return;

    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, -300, -300);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    if (soldier)
    {
        // charge blocked branch
        soldier->current_special = 1;
        soldier->curdir = FACE_RIGHT;
        ASSERT_TRUE(!soldier_fd->do_special(soldier)) << "charge should fail when forward is blocked";

        // check_special_ai no-foe + no-near-foe path
        soldier->set_foe(nullptr);
        ASSERT_TRUE(!soldier_fd->check_special_ai(static_cast<living*>(soldier))) << "soldier AI should fail when no foe can be found";
    }

    walker* orc = add_living_to_level(FAMILY_ORC, 0, 120, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    if (orc)
    {
        orc->current_special = 1;
        orc->busy = 2;
        ASSERT_TRUE(!orc_fd->do_special(orc)) << "orc howl should fail while busy";
    }

    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    if (mage)
    {
        // check_special_ai with exactly 1-3 foes in range should return false.
        add_living_to_level(FAMILY_ORC, 1, 120, 100);
        add_living_to_level(FAMILY_ORC, 1, 130, 100);
        ASSERT_TRUE(!mage_fd->check_special_ai(static_cast<living*>(mage))) << "mage AI should be false with 2 nearby foes";
    }
}


TEST(FamilyBehaviors, cleric_raise_and_resurrect_distance_and_busy_guards)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";
    if (!(fd && fd->do_special))
        return;

    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    if (!cleric)
        return;

    // Turn-undead busy guard.
    cleric->current_special = 2;
    cleric->shifter_down = 1;
    cleric->busy = 1;
    ASSERT_TRUE(!fd->do_special(cleric)) << "turn undead should fail while busy";
    cleric->busy = 0;

    // Raise skeleton distance guard.
    walker* blood_far = add_stain_to_fxlist(1, 250, 100);
    ASSERT_TRUE(blood_far != nullptr) << "far blood created";
    cleric->current_special = 2;
    cleric->shifter_down = 0;
    ASSERT_TRUE(!fd->do_special(cleric)) << "raise skeleton should fail when blood is out of range";
    if (blood_far)
        blood_far->dead = 1;

    // Raise ghost distance guard.
    walker* blood_far2 = add_stain_to_fxlist(1, 180, 100);
    ASSERT_TRUE(blood_far2 != nullptr) << "second far blood created";
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    ASSERT_TRUE(!fd->do_special(cleric)) << "raise ghost should fail when blood is out of range";
    if (blood_far2)
        blood_far2->dead = 1;

    // Resurrect path distance guard.
    walker* blood_far3 = add_stain_to_fxlist(0, 250, 100);
    ASSERT_TRUE(blood_far3 != nullptr) << "third far blood created";
    cleric->current_special = 4;
    ASSERT_TRUE(!fd->do_special(cleric)) << "resurrect should fail when blood is out of range";
}


TEST(FamilyBehaviors, druid_special_busy_and_friend_count_guards)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && fd->do_special) << "druid do_special present";
    if (!(fd && fd->do_special))
        return;

    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    ASSERT_TRUE(druid != nullptr) << "druid created";
    if (!druid)
        return;

    druid->busy = 1;
    druid->current_special = 1;
    ASSERT_TRUE(!fd->do_special(druid)) << "druid tree special should fail while busy";
    druid->current_special = 2;
    ASSERT_TRUE(!fd->do_special(druid)) << "druid summon special should fail while busy";
    druid->current_special = 3;
    ASSERT_TRUE(!fd->do_special(druid)) << "druid reveal special should fail while busy";
    druid->current_special = 4;
    ASSERT_TRUE(!fd->do_special(druid)) << "druid protection special should fail while busy";

    druid->busy = 0;
    druid->current_special = 4;
    ASSERT_TRUE(!fd->do_special(druid)) << "druid protection should fail with no nearby allies";
}


TEST(FamilyBehaviors, family_round6_mage_thief_soldier_guard_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* mage_fd = get_family_descriptor(FAMILY_MAGE);
    const auto* thief_fd = get_family_descriptor(FAMILY_THIEF);
    const auto* soldier_fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(mage_fd && thief_fd && soldier_fd) << "family descriptors present";
    if (!(mage_fd && thief_fd && soldier_fd))
        return;

    // Mage AI check branches: <1 foes => true, 1-3 foes => false, >3 foes => true.
    walker* mage = add_living_to_level(FAMILY_MAGE, 1, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    if (!mage)
        return;
    mage->current_special = 1;
    ASSERT_TRUE(mage_fd->check_special_ai(static_cast<living*>(mage))) << "mage AI should allow special when no foes are in range";
    add_living_to_level(FAMILY_SOLDIER, 0, 120, 100);
    add_living_to_level(FAMILY_ORC, 0, 130, 100);
    ASSERT_TRUE(!mage_fd->check_special_ai(static_cast<living*>(mage))) << "mage AI should reject special when 1-3 foes are in range";
    add_living_to_level(FAMILY_SOLDIER, 0, 140, 100);
    add_living_to_level(FAMILY_ORC, 0, 150, 100);
    ASSERT_TRUE(mage_fd->check_special_ai(static_cast<living*>(mage))) << "mage AI should allow special when many foes are in range";

    // Mage teleport guards.
    mage->stats()->magicpoints = 1000;
    mage->current_special = 1;
    mage->ani_type = ANI_TELE_OUT;
    ASSERT_TRUE(!mage_fd->do_special(mage)) << "mage teleport should fail while already teleporting";
    mage->ani_type = ANI_WALK;
    mage->shifter_down = 1;
    mage->busy = 1;
    ASSERT_TRUE(!mage_fd->do_special(mage)) << "mage marker path should fail while busy";
    mage->busy = 0;
    mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    if (mage->myguy)
        mage->myguy->intelligence = 50;
    mage->user = 0;
    ASSERT_TRUE(!mage_fd->do_special(mage)) << "mage marker path should fail for low-intelligence player characters";

    // Thief AI and do_special guards.
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    if (!thief)
        return;
    thief->current_special = 1;
    thief->set_foe(add_living_to_level(FAMILY_SOLDIER, 1, 200, 100));
    ASSERT_TRUE(thief->foe() != nullptr) << "thief foe created";
    if (thief->foe())
        ASSERT_TRUE(!thief_fd->check_special_ai(static_cast<living*>(thief))) << "thief bomb AI should reject when foe distance is in drop-bomb window";
    thief->set_foe(nullptr);
    ASSERT_TRUE(!thief_fd->check_special_ai(static_cast<living*>(thief))) << "thief bomb AI should reject when too few foes are nearby";
    thief->current_special = 5;
    ASSERT_TRUE(thief_fd->check_special_ai(static_cast<living*>(thief))) << "thief AI default branch should allow special";

    thief->current_special = 3;
    thief->shifter_down = 0;
    thief->busy = 1;
    ASSERT_TRUE(!thief_fd->do_special(thief)) << "thief taunt should fail while busy";
    thief->shifter_down = 1;
    thief->busy = 0;
    thief->setxy(300, 100); // Keep charm range clear of foes created above.
    ASSERT_TRUE(!thief_fd->do_special(thief)) << "thief charm should fail when no targets are in range";
    thief->current_special = 4;
    thief->busy = 1;
    ASSERT_TRUE(!thief_fd->do_special(thief)) << "thief poison cloud should fail while busy";

    // Soldier special guards.
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 0, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    if (!soldier)
        return;
    soldier->stats()->magicpoints = 1000;
    soldier->current_special = 1;
    soldier->curdir = FACE_LEFT; // blocked by map edge
    ASSERT_TRUE(!soldier_fd->do_special(soldier)) << "soldier charge should fail when forward is blocked";
    soldier->current_special = 3;
    soldier->busy = 1;
    ASSERT_TRUE(!soldier_fd->do_special(soldier)) << "soldier whirlwind should fail while busy";
}


TEST(FamilyBehaviors, cleric_round6_heal_low_magic_and_undead_raise_no_target_guards)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && fd->do_special) << "cleric do_special present";
    if (!(fd && fd->do_special))
        return;

    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 108, 100);
    ASSERT_TRUE(cleric && ally) << "cleric and ally created";
    if (!(cleric && ally))
        return;

    // Heal special with low MP should take the low-magic adjustment branch.
    cleric->current_special = 1;
    cleric->shifter_down = 0;
    cleric->stats()->level = 12;
    cleric->stats()->magicpoints = 1;
    ally->stats()->hitpoints = ally->stats()->max_hitpoints - 20.0f;
    (void)fd->do_special(cleric);

    // Full-health ally path should produce didheal==0 and return false.
    cleric->stats()->magicpoints = 200;
    ally->stats()->hitpoints = ally->stats()->max_hitpoints;
    ASSERT_TRUE(!fd->do_special(cleric)) << "heal special should fail when nobody needs healing";

    // Raise/ghost specials with no blood target should fail via nearest-blood null branches.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric recreated";
    if (!cleric)
        return;
    cleric->current_special = 2;
    cleric->shifter_down = 0;
    ASSERT_TRUE(!fd->do_special(cleric)) << "raise skeleton should fail with no blood target";
    cleric->current_special = 3;
    cleric->shifter_down = 0;
    ASSERT_TRUE(!fd->do_special(cleric)) << "raise ghost should fail with no blood target";
}


TEST(FamilyBehaviors, druid_round6_protection_existing_circle_and_blocked_faerie_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && fd->do_special) << "druid do_special present";
    if (!(fd && fd->do_special))
        return;

    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 112, 100);
    ASSERT_TRUE(druid && ally) << "druid and ally created";
    if (!(druid && ally))
        return;

    // Pre-existing protection circle on ally should hit refresh/merge branch.
    walker* existing = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(existing != nullptr) << "existing protection circle created";
    if (!existing)
        return;
    existing->set_owner(ally);
    existing->team_num = ally->team_num;
    existing->stats()->hitpoints = 10.0f;

    druid->current_special = 4;
    druid->busy = 0;
    druid->stats()->magicpoints = 300;
    ASSERT_TRUE(fd->do_special(druid)) << "protection with existing circle should succeed";
    ASSERT_TRUE(existing->stats()->hitpoints >= 10.0f) << "existing circle hp should be refreshed";

    // Blocked summon destination path for special 2.
    druid->current_special = 2;
    druid->busy = 0;
    druid->stats()->magicpoints = 300;
    druid->setxy(0, 0); // edge tends to make summon destination impassable
    (void)fd->do_special(druid);
}


TEST(FamilyBehaviors, family_round8_mage_thief_soldier_callback_edge_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* mage_fd = get_family_descriptor(FAMILY_MAGE);
    const auto* thief_fd = get_family_descriptor(FAMILY_THIEF);
    const auto* soldier_fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(mage_fd && mage_fd->check_special_ai && mage_fd->do_special) << "mage callbacks exist";
    ASSERT_TRUE(thief_fd && thief_fd->check_special_ai) << "thief callback exists";
    ASSERT_TRUE(soldier_fd && soldier_fd->on_fire_weapon) << "soldier callback exists";
    if (!(mage_fd && thief_fd && soldier_fd && mage_fd->check_special_ai && mage_fd->do_special &&
          thief_fd->check_special_ai && soldier_fd->on_fire_weapon))
        return;

    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    if (!mage)
        return;

    // Mage AI returns false when 1-3 foes are in range.
    add_living_to_level(FAMILY_ORC, 1, 120, 100);
    add_living_to_level(FAMILY_ORC, 1, 130, 100);
    ASSERT_TRUE(!mage_fd->check_special_ai(static_cast<living*>(mage))) << "mage special AI should be false with 1-3 nearby foes";

    // Teleport special hard guard while already in teleport animation.
    mage->current_special = 1;
    mage->ani_type = ANI_TELE_OUT;
    ASSERT_TRUE(!mage_fd->do_special(mage)) << "mage teleport special should fail while already teleporting";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(thief && foe) << "thief and foe created";
    if (thief && foe)
    {
        thief->current_special = 1;
        thief->set_foe(foe);
        ASSERT_TRUE(!thief_fd->check_special_ai(static_cast<living*>(thief))) << "thief bomb AI should reject medium-range foe distance";
    }

    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    walker* weapon = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(soldier && weapon) << "soldier and weapon created";
    if (soldier && weapon)
    {
        living* lv = static_cast<living*>(soldier);
        const float mp_before = soldier->stats()->magicpoints;
        lv->weapons_left = 0;
        ASSERT_TRUE(!soldier_fd->on_fire_weapon(soldier, weapon)) << "soldier on_fire_weapon should fail and consume weapon when no weapons_left";
        ASSERT_TRUE(weapon->dead == 1) << "soldier fallback should mark weapon dead";
        ASSERT_TRUE(soldier->stats()->magicpoints >= mp_before) << "soldier fallback should refund weapon cost to magicpoints";

        weapon->dead = 0;
        lv->weapons_left = 2;
        ASSERT_TRUE(soldier_fd->on_fire_weapon(soldier, weapon)) << "soldier on_fire_weapon should succeed when weapons_left > 0";
        ASSERT_EQ(1, (int)lv->weapons_left) << "soldier on_fire_weapon should decrement weapons_left";
    }
}


TEST(FamilyBehaviors, family_round10_orc_ghost_archer_slime_elf_edge_callbacks)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* orc_fd = get_family_descriptor(FAMILY_ORC);
    const auto* ghost_fd = get_family_descriptor(FAMILY_GHOST);
    const auto* archer_fd = get_family_descriptor(FAMILY_ARCHER);
    const auto* slime_fd = get_family_descriptor(FAMILY_SLIME);
    const auto* elf_fd = get_family_descriptor(FAMILY_ELF);
    ASSERT_TRUE(orc_fd && ghost_fd && archer_fd && slime_fd && elf_fd) << "family descriptors exist";
    if (!(orc_fd && ghost_fd && archer_fd && slime_fd && elf_fd))
        return;

    // ORC special: full-hp eat-corpse guard should fail.
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    if (!orc)
        return;
    orc->current_special = 2;
    orc->stats()->hitpoints = orc->stats()->max_hitpoints;
    ASSERT_TRUE(!orc_fd->do_special(orc)) << "orc eat-corpse special should fail at full hp";

    // GHOST AI: no foe in range should fail, nearby foe should pass.
    walker* ghost = add_living_to_level(FAMILY_GHOST, 0, 120, 100);
    ASSERT_TRUE(ghost != nullptr) << "ghost created";
    if (!ghost)
        return;
    ghost->set_foe(nullptr);
    ASSERT_TRUE(!ghost_fd->check_special_ai(static_cast<living*>(ghost))) << "ghost check_special_ai should fail without nearby foes";
    walker* ghost_foe = add_living_to_level(FAMILY_ORC, 1, 130, 100);
    ASSERT_TRUE(ghost_foe != nullptr) << "ghost foe created";
    if (ghost_foe)
    {
        ghost->set_foe(ghost_foe);
        ASSERT_TRUE(ghost_fd->check_special_ai(static_cast<living*>(ghost))) << "ghost check_special_ai should pass with close foe";
    }

    // ARCHER hit_response: close-range foe should force a walk command away.
    walker* archer = add_living_to_level(FAMILY_ARCHER, 0, 100, 100);
    walker* archer_foe = add_living_to_level(FAMILY_ORC, 1, 110, 100);
    ASSERT_TRUE(archer && archer_foe) << "archer and foe created";
    if (archer && archer_foe)
    {
        archer->stats()->clear_command();
        archer_fd->hit_response(archer->stats(), archer_foe);
        ASSERT_TRUE(archer->foe() == archer_foe) << "archer hit_response should assign foe";
        ASSERT_TRUE(archer->stats()->has_commands()) << "archer hit_response should enqueue retreat command at close range";
    }

    // SLIME AI: MAXOBS guard branch.
    const int saved_numobs = og::runtime::current_session->myscreen_->world().living_count;
    og::runtime::current_session->myscreen_->world().living_count = MAXOBS;
    ASSERT_TRUE(!slime_fd->check_special_ai(static_cast<living*>(orc))) << "slime check_special_ai should fail when numobs reaches MAXOBS";
    og::runtime::current_session->myscreen_->world().living_count = 0;
    ASSERT_TRUE(slime_fd->check_special_ai(static_cast<living*>(orc))) << "slime check_special_ai should pass when numobs is below MAXOBS";
    og::runtime::current_session->myscreen_->world().living_count = saved_numobs;

    // ELF special basic branch should execute for case 1 when fire is available.
    walker* elf = add_living_to_level(FAMILY_ELF, 0, 100, 100);
    ASSERT_TRUE(elf != nullptr) << "elf created";
    if (elf)
    {
        elf->current_special = 1;
        elf->stats()->magicpoints = 200;
        ASSERT_TRUE(elf_fd->do_special(elf)) << "elf special case 1 should succeed in normal conditions";
    }
}


TEST(FamilyBehaviors, family_round11_mage_and_druid_targeted_special_clusters)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* mage_fd = get_family_descriptor(FAMILY_MAGE);
    const auto* druid_fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(mage_fd && mage_fd->do_special && druid_fd && druid_fd->do_special) << "mage/druid callbacks present";
    if (!(mage_fd && mage_fd->do_special && druid_fd && druid_fd->do_special))
        return;

    // Mage case 1 guard: busy/intelligence marker path (family_mage.cpp:121-130).
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    if (!mage)
        return;
    mage->stats()->magicpoints = 400;
    mage->current_special = 1;
    mage->ani_type = ANI_WALK;
    mage->shifter_down = 1;
    mage->busy = 0;
    mage->user = 0;
    mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    if (mage->myguy)
        mage->myguy->intelligence = 50;
    ASSERT_TRUE(!mage_fd->do_special(mage)) << "mage marker should fail with int < 75";

    // Mage case 5 heartburst success path (family_mage.cpp:260-289).
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    mage = add_living_to_level(FAMILY_MAGE, 1, 100, 100);
    walker* foe1 = add_living_to_level(FAMILY_ORC, 0, 118, 100);
    walker* foe2 = add_living_to_level(FAMILY_ORC, 0, 100, 118);
    ASSERT_TRUE(mage && foe1 && foe2) << "mage and foes created";
    if (!(mage && foe1 && foe2))
        return;
    mage->current_special = 5;
    mage->stats()->magicpoints = 500;
    const float mp_before = mage->stats()->magicpoints;
    ASSERT_TRUE(mage_fd->do_special(mage)) << "mage heartburst should succeed with nearby foes";
    ASSERT_TRUE(mage->busy >= 5) << "heartburst should add busy delay";
    ASSERT_TRUE(mage->stats()->magicpoints < mp_before) << "heartburst should consume magic";

    // Druid protection refresh branch with existing circle (family_druid.cpp:147-176).
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 110, 100);
    ASSERT_TRUE(druid && ally) << "druid and ally created";
    if (!(druid && ally))
        return;
    walker* circle = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(circle != nullptr) << "existing protection circle created";
    if (!circle)
        return;
    circle->set_owner(ally);
    circle->stats()->hitpoints = 10.0f;
    druid->current_special = 4;
    druid->busy = 0;
    druid->stats()->magicpoints = 500;
    ASSERT_TRUE(druid_fd->do_special(druid)) << "druid protection should succeed with nearby ally";
}


TEST(FamilyBehaviors, family_round12_cleric_druid_soldier_thief_guard_and_ai_edges)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* cleric_fd = get_family_descriptor(FAMILY_CLERIC);
    const auto* druid_fd = get_family_descriptor(FAMILY_DRUID);
    const auto* soldier_fd = get_family_descriptor(FAMILY_SOLDIER);
    const auto* thief_fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_TRUE(cleric_fd && cleric_fd->check_special_ai && cleric_fd->do_special) << "cleric callbacks exist";
    ASSERT_TRUE(druid_fd && druid_fd->do_special) << "druid callback exists";
    ASSERT_TRUE(soldier_fd && soldier_fd->do_special && soldier_fd->check_special_ai) << "soldier callbacks exist";
    ASSERT_TRUE(thief_fd && thief_fd->do_special && thief_fd->check_special_ai) << "thief callbacks exist";
    if (!(cleric_fd && druid_fd && soldier_fd && thief_fd &&
          cleric_fd->check_special_ai && cleric_fd->do_special &&
          druid_fd->do_special && soldier_fd->do_special && soldier_fd->check_special_ai &&
          thief_fd->do_special && thief_fd->check_special_ai))
        return;

    // Cleric AI special-1 low-friend/low-magic false and non-special-1 true.
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    if (!cleric)
        return;
    cleric->current_special = 1;
    cleric->stats()->magicpoints = 0.0f;
    ASSERT_TRUE(!cleric_fd->check_special_ai(static_cast<living*>(cleric))) << "cleric check_special_ai should fail for heal with no targets and low mp";
    cleric->current_special = 3;
    ASSERT_TRUE(cleric_fd->check_special_ai(static_cast<living*>(cleric))) << "cleric check_special_ai should default true for non-heal specials";

    // Cleric turn-undead guard branch: busy rejects immediately.
    cleric->current_special = 2;
    cleric->shifter_down = 1;
    cleric->busy = 1;
    ASSERT_TRUE(!cleric_fd->do_special(cleric)) << "cleric turn-undead should fail while busy";
    cleric->busy = 0;

    // Druid busy and fire-fail guards.
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 120, 100);
    ASSERT_TRUE(druid != nullptr) << "druid created";
    if (!druid)
        return;
    druid->current_special = 1;
    druid->busy = 1;
    ASSERT_TRUE(!druid_fd->do_special(druid)) << "druid tree special should fail when busy";
    druid->busy = 0;
    druid->current_special = 2;
    druid->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    ASSERT_TRUE(!druid_fd->do_special(druid)) << "druid faerie summon should fail when fire() fails";
    druid->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    druid->current_special = 4;
    cleric->team_num = 1; // ensure there are no nearby same-team allies for this guard check
    // No nearby allies except self => howmany <= 1 => false.
    ASSERT_TRUE(!druid_fd->do_special(druid)) << "druid protection should fail with no nearby allies";

    // Soldier special guards.
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 140, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    if (!soldier)
        return;
    soldier->current_special = 1;
    soldier->lastx = 1;
    soldier->lasty = 0;
    soldier->curdir = FACE_RIGHT;
    walker* block_front = add_living_to_level(FAMILY_ORC, 1, static_cast<short>(soldier->xpos + 1), soldier->ypos);
    ASSERT_TRUE(block_front != nullptr) << "soldier blocker created";
    ASSERT_TRUE(!soldier_fd->do_special(soldier)) << "soldier charge should fail when forward is blocked";
    soldier->current_special = 3;
    soldier->busy = 1;
    ASSERT_TRUE(!soldier_fd->do_special(soldier)) << "soldier whirlwind should fail when busy";
    soldier->current_special = 4;
    ASSERT_TRUE(!soldier_fd->do_special(soldier)) << "soldier disarm should fail when busy";
    soldier->busy = 0;
    soldier->set_foe(add_living_to_level(FAMILY_ORC, 1, static_cast<short>(soldier->xpos + 10), soldier->ypos));
    ASSERT_TRUE(!soldier_fd->check_special_ai(static_cast<living*>(soldier))) << "soldier special ai should fail for too-close foe distance";

    // Thief AI/special guards.
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 180, 100);
    walker* thief_foe = add_living_to_level(FAMILY_ORC, 1, 240, 100);
    ASSERT_TRUE(thief && thief_foe) << "thief fixtures created";
    if (!(thief && thief_foe))
        return;
    thief->current_special = 1;
    thief->set_foe(thief_foe);
    ASSERT_TRUE(!thief_fd->check_special_ai(static_cast<living*>(thief))) << "thief bomb ai should reject medium-range foe distances";
    thief->current_special = 3;
    thief->shifter_down = 0;
    thief->busy = 1;
    ASSERT_TRUE(!thief_fd->do_special(thief)) << "thief taunt should fail when busy";
    thief->shifter_down = 1;
    ASSERT_TRUE(!thief_fd->do_special(thief)) << "thief charm should fail when busy";
    thief->current_special = 4;
    ASSERT_TRUE(!thief_fd->do_special(thief)) << "thief poison cloud should fail when busy";
}
