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
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <openglad/core/irandom.h>
#include <openglad/core/combat_math.h>
#include <gtest/gtest.h>
#include <cmath>
#include <algorithm>
#include "test_family_hook_dispatch.h"

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
        float hp0 = w->stats()->max_hitpoints();
        float mp0 = w->stats()->max_magicpoints();
        float dmg0 = w->damage();
        float armor0 = w->stats()->armor();

        // team_num=0 means player team, no difficulty scaling applied
        w->set_team_num(0);
        static_cast<living*>(w.get())->set_difficulty(2);

        float hp_delta = w->stats()->max_hitpoints() - hp0;
        float mp_delta = w->stats()->max_magicpoints() - mp0;
        float dmg_delta = w->damage() - dmg0;
        float armor_delta = w->stats()->armor() - armor0;

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
    w->set_team_num(0);

    static_cast<living*>(w.get())->set_difficulty(5);
    short wl = static_cast<living*>(w.get())->weapons_left();
    ASSERT_EQ(3, (int)wl) << "soldier weapons_left at level 5 should be (5+1)/2=3";
}


// living::set_difficulty(): after the per-family formula a walker on a
// hostile team has max_hitpoints/max_magicpoints/damage multiplied by the
// world's difficulty percent; a walker actually carrying a player guy is
// exempt (living.cpp, the team_num() != 0 arm and the myguy == nullptr
// clause below it).
TEST(FamilyBehaviors, set_difficulty_scales_hostiles_and_exempts_player_guys)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    struct DifficultyRestore {
        GameWorld& w;
        short saved;
        ~DifficultyRestore() { w.difficulty = saved; }
    } restore{world, world.difficulty};

    // A — hostile at 100 %: the unscaled reference.
    auto a = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(a != nullptr) << "make_living should succeed";
    a->set_team_num(1);
    world.difficulty = 100;
    static_cast<living*>(a.get())->set_difficulty(3);

    // B — hostile at 200 %, carrying a guy. The team-0 NPC arm below the
    // legacy gate requires myguy == nullptr, so only the team_num() != 0 arm
    // can possibly scale B: this pins THAT arm, not its neighbour.
    auto b = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(b != nullptr) << "make_living should succeed";
    b->set_team_num(1);
    b->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    world.difficulty = 200;
    static_cast<living*>(b.get())->set_difficulty(3);

    // C — team 0 carrying a player guy at 200 %: exempt.
    auto c = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(c != nullptr) << "make_living should succeed";
    c->set_team_num(0);
    c->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    static_cast<living*>(c.get())->set_difficulty(3);

    ASSERT_NEAR(2.0f * a->stats()->max_hitpoints(), b->stats()->max_hitpoints(), 0.5f)
        << "difficulty 200 must double a hostile's max HP";
    ASSERT_NEAR(2.0f * a->stats()->max_magicpoints(), b->stats()->max_magicpoints(), 0.5f)
        << "difficulty 200 must double a hostile's max MP";
    ASSERT_NEAR(2.0f * a->damage(), b->damage(), 0.5f)
        << "difficulty 200 must double a hostile's damage";

    ASSERT_NEAR(a->stats()->max_hitpoints(), c->stats()->max_hitpoints(), 0.5f)
        << "a team-0 walker carrying a player guy must not scale with difficulty";
    ASSERT_NEAR(a->stats()->max_magicpoints(), c->stats()->max_magicpoints(), 0.5f)
        << "a team-0 walker carrying a player guy must not scale with difficulty";
    ASSERT_NEAR(a->damage(), c->damage(), 0.5f)
        << "a team-0 walker carrying a player guy must not scale with difficulty";
}


// After set_difficulty, hitpoints = max_hitpoints (healed to full)
TEST(FamilyBehaviors, set_difficulty_heals_to_full)
{
    auto w = make_living(FAMILY_ARCHER);
    ASSERT_TRUE(w != nullptr) << "make_living should succeed";
    w->set_team_num(0);

    static_cast<living*>(w.get())->set_difficulty(3);
    ASSERT_TRUE(std::fabs((w->stats()->max_hitpoints()) - (w->stats()->hitpoints())) <= 0.5f) << "set_difficulty should heal to max HP" << " expected: " << (w->stats()->max_hitpoints()) << ", actual: " << (w->stats()->hitpoints());
    ASSERT_TRUE(std::fabs((w->stats()->max_magicpoints()) - (w->stats()->magicpoints())) <= 0.5f) << "set_difficulty should heal to max MP" << " expected: " << (w->stats()->max_magicpoints()) << ", actual: " << (w->stats()->magicpoints());
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
    // The elemental must live in the world, not be a loose owned walker: its
    // on_death hook re-enters dispatch through self:special(), and script
    // handles for an entity outside the world's id index only stay valid for
    // the one dispatch that produced them.
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    walker* w = world.add_ob(Order::Living, FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(w != nullptr) << "should create fire elemental";
    w->setxy(100, 100);
    w->set_team_num(1);  // non-player team (avoids endgame check)
    w->stats()->set_level(3);

    const std::size_t weapons_before = world.weaplist.size();
    og::script::hooks::reset_hook_failures();

    w->set_dead(1);
    // death() runs the on_death hook, which fires the parting starburst.
    w->death();

    EXPECT_EQ(0u, og::script::hooks::hook_failures().count)
        << og::script::hooks::hook_failures().message;
    EXPECT_GT(world.weaplist.size(), weapons_before)
        << "the parting starburst must actually fire";
}


template <typename List>
static int count_family_in(const List& list, int family)
{
    int found = 0;
    for (auto& uptr : list)
        if (uptr && uptr->family() == family)
            ++found;
    return found;
}

// generate_bloodspot() files the stain through add_fx_ob, so the counter has
// to look in all three lists to be able to say "no stain anywhere".
static int count_stains()
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    return count_family_in(world.oblist, FAMILY_STAIN) +
           count_family_in(world.fxlist, FAMILY_STAIN) +
           count_family_in(world.weaplist, FAMILY_STAIN);
}

// Slime death: the dying blob is replaced by exactly ONE next-size-down
// offspring which inherits its team and floor, and the corpse is topped back
// up to max hp (packs/core/families/living-08-slime.lua split_on_death,
// reached from slime_on_death and medium_slime_on_death).
// The parent must live IN the world: script handles for an entity outside the
// world's id index only stay valid for the dispatch that produced them.
TEST(FamilyBehaviors, on_death_slime_splits_into_one_next_size_down)
{
    struct SplitCase { int parent; int child; const char* name; };
    const SplitCase split_cases[] = {
        { FAMILY_SLIME,        FAMILY_MEDIUM_SLIME, "slime -> medium slime" },
        { FAMILY_MEDIUM_SLIME, FAMILY_SMALL_SLIME,  "medium slime -> small slime" },
    };

    GameWorld& world = og::runtime::current_session->myscreen_->world();
    for (const auto& tc : split_cases)
    {
        world.delete_objects();
        world.create_new_grid();

        walker* parent = world.add_ob(Order::Living, tc.parent);
        ASSERT_TRUE(parent != nullptr) << tc.name << ": parent created";
        parent->setxy(100, 100);
        parent->set_team_num(1);  // non-player team (avoids the endgame check)
        parent->stats()->set_level(3);

        const std::size_t obs_before = world.oblist.size();
        og::script::hooks::reset_hook_failures();

        parent->set_dead(1);
        parent->death();

        EXPECT_EQ(0u, og::script::hooks::hook_failures().count)
            << tc.name << ": " << og::script::hooks::hook_failures().message;
        ASSERT_EQ(obs_before + 1u, world.oblist.size())
            << tc.name << ": death must add exactly one offspring";

        int children = 0;
        walker* child = nullptr;
        for (auto& uptr : world.oblist)
        {
            walker* w = uptr.get();
            if (w && w != parent && w->query_order() == Order::Living &&
                w->family() == tc.child)
            {
                ++children;
                child = w;
            }
        }
        ASSERT_EQ(1, children)
            << tc.name << ": exactly one offspring of the expected family";
        ASSERT_EQ(static_cast<int>(parent->team_num()),
                  static_cast<int>(child->team_num()))
            << tc.name << ": offspring inherits the parent's team";
        ASSERT_EQ(static_cast<int>(parent->floor()),
                  static_cast<int>(child->floor()))
            << tc.name << ": offspring stays on the parent's floor";
        ASSERT_EQ(3, static_cast<int>(child->stats()->level()))
            << tc.name << ": offspring inherits the parent's level";
        ASSERT_NEAR(parent->stats()->max_hitpoints(),
                    parent->stats()->hitpoints(), 0.5f)
            << tc.name << ": the corpse's hp is reset to max";
    }
}


// walker::death(): with no on_death hook handling the corpse, the family
// descriptor's leaves_bloodspot flag alone decides whether
// generate_bloodspot() mints a FAMILY_STAIN. Ghost and skeleton say false
// (living-12-ghost.lua / living-04-skeleton.lua); the soldier row is the
// positive control that proves the counter is live.
TEST(FamilyBehaviors, on_death_bloodspot_flag_decides_stain)
{
    struct StainCase { int family; int delta; const char* name; };
    const StainCase stain_cases[] = {
        { FAMILY_GHOST,    0, "ghost leaves no stain" },
        { FAMILY_SKELETON, 0, "skeleton leaves no stain" },
        { FAMILY_SOLDIER,  1, "soldier leaves exactly one stain" },
    };

    GameWorld& world = og::runtime::current_session->myscreen_->world();
    for (const auto& tc : stain_cases)
    {
        world.delete_objects();
        world.create_new_grid();

        const auto* fd = get_family_descriptor(tc.family);
        ASSERT_TRUE(fd != nullptr) << tc.name << ": descriptor exists";
        ASSERT_EQ(tc.delta == 1, fd->leaves_bloodspot)
            << tc.name << ": leaves_bloodspot must match the expected stain";

        auto w = make_guy_for_death(static_cast<char>(tc.family));
        ASSERT_TRUE(w != nullptr) << tc.name << ": walker created";

        const int stains_before = count_stains();
        w->set_dead(1);
        w->death();

        ASSERT_EQ(stains_before + tc.delta, count_stains())
            << tc.name << ": death must mint exactly that many FAMILY_STAINs";
    }
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
    ob->set_team_num(static_cast<unsigned char>(team));
    ob->setxy(x, y);
    return ob;
}

// Live walkers of `family` in the world's weapon / object lists.
static int count_family_in_weaplist(int family)
{
    int n = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().weaplist)
        if (uptr && !uptr->dead() && uptr->family() == static_cast<char>(family))
            n++;
    return n;
}

static int count_family_in_fxlist(int family)
{
    int n = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist)
        if (uptr && !uptr->dead() && uptr->family() == static_cast<char>(family))
            n++;
    return n;
}

static int count_family_in_oblist(int family)
{
    int n = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
        if (uptr && !uptr->dead() && uptr->family() == static_cast<char>(family))
            n++;
    return n;
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

// og.rand draws from current_game->world->rng_, NOT from GameContext::rng, so
// a scripted IRandom only steers a family callback through this seam.
class ScopedSimRandom
{
public:
    explicit ScopedSimRandom(IRandom* rng) : rng_(rng)
    {
        og::sim::set_sim_random_override(&rng_);
    }
    ~ScopedSimRandom() { og::sim::set_sim_random_override(nullptr); }
    ScopedSimRandom(const ScopedSimRandom&) = delete;
    ScopedSimRandom& operator=(const ScopedSimRandom&) = delete;
private:
    IRandom* rng_;
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
    soldier->stats()->set_magicpoints(1000); // ensure enough MP
    soldier->set_current_special(1);
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
        w->stats()->set_magicpoints(1000);
        w->set_current_special(1);

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
    mage->stats()->set_magicpoints(1000);
    mage->set_current_special(1);

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
    skel->stats()->set_magicpoints(1000);
    skel->set_current_special(1);

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
        w->stats()->set_magicpoints(1000);
        w->set_current_special(1);
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
    slime->stats()->set_magicpoints(1000);
    slime->set_current_special(1);

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
    soldier->set_current_special(3);
    soldier->stats()->set_magicpoints(0);
    soldier->check_special();
    ASSERT_EQ(1, (int)soldier->current_special()) << "insufficient MP should reset current_special to 1";
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
    defender->stats()->set_hitpoints(defender->stats()->max_hitpoints());
    defender->stats()->hit_response(attacker);

    ASSERT_TRUE(defender->foe() == attacker) << "default hit_response should set foe to attacker";
}


// hit_response: the archer acquires the foe AND is forced to backpedal when
// the attacker is inside melee_backpedal_range (64 px in
// packs/core/families/living-02-archer.lua); the attacker below is 20 px away.
TEST(FamilyBehaviors, hit_response_archer_flees)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* archer = add_living_to_level(FAMILY_ARCHER, 0, 100, 100);
    ASSERT_TRUE(archer != nullptr) << "archer created";
    archer->set_act_type(0); // AI-controlled
    walker* attacker = add_living_to_level(FAMILY_ORC, 1, 120, 100);
    ASSERT_TRUE(attacker != nullptr) << "attacker created";

    archer->set_foe(nullptr);
    archer->stats()->set_hitpoints(archer->stats()->max_hitpoints());
    archer->stats()->clear_command();
    ASSERT_FALSE(archer->stats()->has_commands())
        << "the queue must start empty so the retreat walk is observable";
    archer->stats()->hit_response(attacker);

    ASSERT_TRUE(archer->foe() == attacker) << "archer hit_response should set foe to attacker";
    ASSERT_TRUE(archer->stats()->has_commands())
        << "a close-range hit must queue the archer's forced retreat walk";
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

    mage->stats()->set_hitpoints(mage->stats()->max_hitpoints()); // full HP
    mage->stats()->set_magicpoints(1000);
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
    arrow->set_team_num(1);
    arrow->setxy(110, 100);

    defender->set_foe(nullptr);
    defender->stats()->set_hitpoints(defender->stats()->max_hitpoints());
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
    w->stats()->set_magicpoints(1000);
    w->set_current_special(1);
    w->set_dead(1);
    bool result = w->special();
    ASSERT_TRUE(result == false) << "dead walker special should return false";
}


// Guard: insufficient MP should return false without deducting mana
TEST(FamilyBehaviors, special_insufficient_mp_returns_false)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(w != nullptr) << "soldier created";
    w->set_current_special(1);
    w->stats()->set_magicpoints(0); // no mana
    float mp_before = w->stats()->magicpoints();
    bool result = w->special();
    ASSERT_TRUE(result == false) << "insufficient MP special should return false";
    ASSERT_TRUE(std::fabs((mp_before) - (w->stats()->magicpoints())) <= 0.5f) << "MP should not change" << " expected: " << (mp_before) << ", actual: " << (w->stats()->magicpoints());
}


// Skeleton: tunnel starts the teleport-out animation from cycle 0, reports
// success, and walker::special() then charges special_cost(1)
// (packs/core/families/living-04-skeleton.lua do_special).
TEST(FamilyBehaviors, special_skeleton_tunnel)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* skel = add_living_to_level(FAMILY_SKELETON, 0, 100, 100);
    ASSERT_TRUE(skel != nullptr) << "skeleton created";
    skel->stats()->set_magicpoints(1000);
    skel->set_current_special(1);

    // Skeleton starts with ANI_SKEL_GROW; must finish grow first
    skel->set_ani_type(0); // reset to default so tunnel can work
    skel->set_cycle(5);    // non-zero, so the hook's cycle reset is observable
    float mp_before = skel->stats()->magicpoints();
    float special_cost = skel->stats()->special_cost(1);

    ASSERT_TRUE(skel->special())
        << "tunnel must succeed with mana in hand and no teleport in progress";
    ASSERT_EQ(static_cast<int>(ANI_TELE_OUT), static_cast<int>(skel->ani_type()))
        << "tunnel must start the teleport-out animation";
    ASSERT_EQ(0, static_cast<int>(skel->cycle()))
        << "tunnel must restart the animation cycle";
    ASSERT_NEAR(mp_before - special_cost, skel->stats()->magicpoints(), 0.5f)
        << "skeleton tunnel must deduct exactly special_cost(1)";
}


// Ghost: scare spawns a ghost_scare FX
TEST(FamilyBehaviors, special_ghost_scare)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* ghost = add_living_to_level(FAMILY_GHOST, 0, 100, 100);
    ASSERT_TRUE(ghost != nullptr) << "ghost created";
    ghost->stats()->set_magicpoints(1000);
    ghost->set_current_special(1);
    float mp_before = ghost->stats()->magicpoints();
    float special_cost = ghost->stats()->special_cost(1);

    ghost->special();

    ASSERT_TRUE(std::fabs((mp_before - special_cost) - (ghost->stats()->magicpoints())) <= 0.5f) << "ghost scare should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (ghost->stats()->magicpoints());
    // If we got here without crash, the scare FX was spawned successfully
}


// Slime: split sets ani_type to ANI_SLIME_SPLIT
TEST(FamilyBehaviors, special_slime_split)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* slime = add_living_to_level(FAMILY_SLIME, 0, 100, 100);
    ASSERT_TRUE(slime != nullptr) << "slime created";
    slime->stats()->set_magicpoints(1000);
    slime->set_current_special(1);

    slime->special();

    ASSERT_EQ(ANI_SLIME_SPLIT, (int)slime->ani_type()) << "slime split should set ani_type to ANI_SLIME_SPLIT";
}


// Fire elemental: starburst fires in 8 directions (deducts mana)
TEST(FamilyBehaviors, special_fire_elemental_starburst)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* fe = add_living_to_level(FAMILY_FIREELEMENTAL, 0, 100, 100);
    ASSERT_TRUE(fe != nullptr) << "fire elemental created";
    fe->stats()->set_magicpoints(1000);
    fe->set_current_special(1);
    float mp_before = fe->stats()->magicpoints();
    float special_cost = fe->stats()->special_cost(1);

    fe->special();

    ASSERT_TRUE(std::fabs((mp_before - special_cost) - (fe->stats()->magicpoints())) <= 0.5f) << "fire elemental starburst should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (fe->stats()->magicpoints());
}


// Elf: special 1 fires 2 rocks (deducts mana)
TEST(FamilyBehaviors, special_elf_rocks)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* elf = add_living_to_level(FAMILY_ELF, 0, 100, 100);
    ASSERT_TRUE(elf != nullptr) << "elf created";
    elf->stats()->set_magicpoints(1000);
    elf->set_current_special(1);
    float mp_before = elf->stats()->magicpoints();
    float special_cost = elf->stats()->special_cost(1);

    elf->special();

    ASSERT_TRUE(std::fabs((mp_before - special_cost) - (elf->stats()->magicpoints())) <= 0.5f) << "elf rock special should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (elf->stats()->magicpoints());
}


// Soldier charge: with the forward tile clear the special queues the rush
// command, reports success and walker::special() charges special_cost(1)
// (packs/core/families/living-00-soldier.lua charge). The blocked-forward
// negative lives in soldier_batch3_special_ai_and_fire_callback_paths.
TEST(FamilyBehaviors, special_soldier_charge)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    soldier->stats()->set_magicpoints(1000);
    soldier->set_current_special(1);
    // forward_blocked() reads curdir; the rush deltas read lastx/lasty. Keep
    // the two consistent and pointed at open grass.
    soldier->set_lastx(1);
    soldier->set_lasty(0);
    soldier->set_curdir(FACE_RIGHT);
    soldier->stats()->clear_command();
    float mp_before = soldier->stats()->magicpoints();
    float special_cost = soldier->stats()->special_cost(1);

    ASSERT_TRUE(soldier->special())
        << "charge must succeed when the tile ahead is clear";
    ASSERT_NEAR(mp_before - special_cost, soldier->stats()->magicpoints(), 0.5f)
        << "a successful charge must deduct exactly special_cost(1)";
    ASSERT_TRUE(soldier->stats()->has_commands())
        << "charge queues COMMAND_RUSH; it is not executed until act()";
}


// Mage teleport: sets ani_type to ANI_TELE_OUT
TEST(FamilyBehaviors, special_mage_teleport)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    mage->stats()->set_magicpoints(1000);
    mage->set_current_special(1);
    mage->set_shifter_down(0); // teleport, not marker

    mage->special();

    ASSERT_EQ(ANI_TELE_OUT, (int)mage->ani_type()) << "mage teleport should set ani_type to ANI_TELE_OUT";
}


// Thief bomb: spawns bomb FX (deducts mana)
TEST(FamilyBehaviors, special_thief_bomb)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    thief->stats()->set_magicpoints(1000);
    thief->set_current_special(1);
    float mp_before = thief->stats()->magicpoints();
    float special_cost = thief->stats()->special_cost(1);

    thief->special();

    ASSERT_TRUE(std::fabs((mp_before - special_cost) - (thief->stats()->magicpoints())) <= 0.5f) << "thief bomb should deduct mana" << " expected: " << (mp_before - special_cost) << ", actual: " << (thief->stats()->magicpoints());
}


// Thief cloak: increases invisibility_left
TEST(FamilyBehaviors, special_thief_cloak)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    thief->stats()->set_magicpoints(1000);
    thief->set_current_special(2);
    thief->set_invisibility_left(0);

    thief->special();

    ASSERT_TRUE(thief->invisibility_left() > 0) << "thief cloak should increase invisibility_left";
}


// Orc howl: sets busy and deducts mana
TEST(FamilyBehaviors, special_orc_howl)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    orc->stats()->set_magicpoints(1000);
    orc->set_current_special(1);
    orc->set_busy(0);

    orc->special();

    ASSERT_TRUE(orc->busy() > 0) << "orc howl should set busy";
}


// Druid reveal: increments view_all
TEST(FamilyBehaviors, special_druid_reveal)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    ASSERT_TRUE(druid != nullptr) << "druid created";
    druid->stats()->set_magicpoints(1000);
    druid->set_current_special(3);
    druid->set_busy(0);

    short view_all_before = druid->view_all();
    druid->special();

    ASSERT_TRUE(druid->view_all() > view_all_before) << "druid reveal should increment view_all";
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
    w->stats()->set_level(40);  // temp >= 1 when level>=40, so view_all increments every cycle
    lv->set_drawcycle(0);
    short va_before = w->view_all();
    // Simulate one act cycle — view_all should increment
    lv->act();
    ASSERT_TRUE(w->view_all() > va_before) << "archmage at level 40 should gain view_all during act()";
}


// Non-archmage should NOT gain view_all
TEST(FamilyBehaviors, non_archmage_no_view_all)
{
    auto w = make_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "make_living should succeed";
    living* lv = static_cast<living*>(w.get());
    short va_before = w->view_all();
    lv->act();
    ASSERT_EQ((int)va_before, (int)w->view_all()) << "soldier should not gain view_all during act()";
}


// --- on_act_living: fire elemental summoned drain ---
TEST(FamilyBehaviors, fire_elemental_summoned_drain)
{
    // Create a mage as owner
    auto owner = make_living(FAMILY_MAGE);
    ASSERT_TRUE(owner != nullptr) << "make owner mage";
    owner->stats()->set_hitpoints(owner->stats()->max_hitpoints());
    owner->stats()->set_magicpoints(owner->stats()->max_magicpoints());

    // Create a fire elemental as summoned creature
    auto fe = make_living(FAMILY_FIREELEMENTAL);
    ASSERT_TRUE(fe != nullptr) << "make fire elemental";
    fe->set_owner(owner.get());
    fe->set_lifetime(100);
    fe->set_team_num(owner->team_num());
    // Hurt the elemental so drain triggers
    fe->stats()->set_hitpoints(fe->stats()->max_hitpoints() / 2);

    float owner_hp_before = owner->stats()->hitpoints();
    float owner_mp_before = owner->stats()->magicpoints();

    living* lv = static_cast<living*>(fe.get());
    lv->act();

    // Owner should lose 1 HP and 3 MP (drain)
    ASSERT_TRUE(owner->stats()->hitpoints() < owner_hp_before) << "owner HP should decrease from fire elemental drain";
    ASSERT_TRUE(owner->stats()->magicpoints() < owner_mp_before) << "owner MP should decrease from fire elemental drain";
}


// --- on_shoved: cleric casts heal when shoved ---
// packs/core/families/living-05-cleric.lua on_shoved forces current_special
// onto slot 1 (heal) and casts it. The shove itself must land first:
// living::shove draws rng_.next(3) unconditionally and does nothing on a zero
// roll, so the shove is retried on a bounded loop.
TEST(FamilyBehaviors, cleric_heals_when_shoved)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 130, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    cleric->set_act_type(0); // AI-controlled
    cleric->stats()->set_magicpoints(500);
    // NOT the heal slot: the hook has to move it.
    cleric->set_current_special(4);
    // The heal declines when everybody is healthy, so wound the ally.
    soldier->stats()->set_hitpoints(soldier->stats()->max_hitpoints() / 2.0f);

    const float soldier_hp_before = soldier->stats()->hitpoints();
    const float cleric_mp_before = cleric->stats()->magicpoints();

    short shoved = 0;
    for (int i = 0; i < 12 && shoved == 0; ++i)
        shoved = static_cast<living*>(soldier)->shove(cleric, 1, 0);
    ASSERT_EQ(1, static_cast<int>(shoved))
        << "the shove must land, or on_shoved never runs";

    ASSERT_EQ(1, static_cast<int>(cleric->current_special()))
        << "on_shoved must force the cleric onto the heal special";
    ASSERT_GT(soldier->stats()->hitpoints(), soldier_hp_before)
        << "the forced heal must actually heal the wounded ally";
    ASSERT_LT(cleric->stats()->magicpoints(), cleric_mp_before)
        << "the forced heal must spend the cleric's mana";
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
    archer->stats()->set_magicpoints(500);

    float mp_before = archer->stats()->magicpoints();
    static_cast<living*>(soldier)->shove(archer, 1, 0);
    // Archer's MP should not change (no heal cast)
    ASSERT_TRUE(std::fabs((mp_before) - (archer->stats()->magicpoints())) <= 0.5f) << "non-cleric should not cast heal when shoved" << " expected: " << (mp_before) << ", actual: " << (archer->stats()->magicpoints());
}


// --- on_fire_weapon: soldier weapons_left ---
// packs/core/families/living-00-soldier.lua on_fire_weapon: above zero the
// ranged release goes out and weapons_left decrements; at zero the weapon is
// killed, its weapon_cost is refunded and walker::fire() answers nullptr.
TEST(FamilyBehaviors, soldier_weapons_left_limits_fire)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    static_cast<living*>(soldier)->set_weapons_left(1);
    soldier->stats()->set_magicpoints(1000);
    soldier->set_lastx(1); // firing direction
    soldier->set_lasty(0);

    walker* w1 = soldier->fire();
    ASSERT_TRUE(w1 != nullptr)
        << "the first throw must launch while weapons_left > 0";
    ASSERT_EQ(0, static_cast<int>(static_cast<living*>(soldier)->weapons_left()))
        << "a ranged release consumes one throwable";
    // Get the launched blade off the launch pad, or the second attempt would
    // take fire()'s melee branch instead of the on_fire_weapon refusal.
    w1->setxy(400, 400);
    const float mp_after_first = soldier->stats()->magicpoints();

    walker* w2 = soldier->fire();
    ASSERT_TRUE(w2 == nullptr) << "soldier with 0 weapons_left should not fire";
    ASSERT_NEAR(mp_after_first, soldier->stats()->magicpoints(), 0.5f)
        << "the refused throw must refund its weapon_cost";
}


// --- on_fire_weapon: archmage weapon damage boost ---
// packs/core/families/living-17-archmage.lua on_fire_weapon moves
// extra = min(magicpoints/20, SHOT_DRAIN_CAP) out of the caster's pool and
// into the shot's damage. walker::fire() deducts weapon_cost FIRST, so extra
// is a twentieth of what is left. The control archmage carries exactly
// weapon_cost, so its extra is 0 and its shot shows the unboosted base damage.
TEST(FamilyBehaviors, archmage_weapon_damage_boost)
{
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    world.delete_objects();
    world.create_new_grid();

    walker* control = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    ASSERT_TRUE(control != nullptr) << "control archmage created";
    control->set_lastx(1);
    control->set_lasty(0);
    const float weapon_cost = control->stats()->weapon_cost();
    control->stats()->set_magicpoints(weapon_cost);
    walker* base_shot = control->fire();
    ASSERT_TRUE(base_shot != nullptr) << "the control shot must launch";
    ASSERT_FALSE(base_shot->dead()) << "the control shot must be a ranged release";
    const float base_damage = base_shot->damage();
    ASSERT_NEAR(0.0f, control->stats()->magicpoints(), 0.05f)
        << "with no spare mana the archmage has nothing extra to drain";

    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 200, 100);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    arch->stats()->set_magicpoints(1000.0f);
    arch->set_lastx(1); // firing direction
    arch->set_lasty(0);
    const float expected_extra =
        std::min((1000.0f - weapon_cost) / 20.0f,
                 static_cast<float>(og::combat::kShotDrainCap));

    walker* weapon = arch->fire();
    ASSERT_TRUE(weapon != nullptr) << "the boosted shot must launch";
    ASSERT_FALSE(weapon->dead()) << "the boosted shot must be a ranged release";
    ASSERT_NEAR(1000.0f - weapon_cost - expected_extra,
                arch->stats()->magicpoints(), 0.05f)
        << "the shot must cost weapon_cost plus the 1/20 drain";
    ASSERT_NEAR(base_damage + expected_extra, weapon->damage(), 0.05f)
        << "the drained mana must land on the shot's damage";
}


TEST(FamilyBehaviors, archmage_on_act_low_level_periodic_gate)
{
    auto w = make_living(FAMILY_ARCHMAGE);
    ASSERT_TRUE(w != nullptr) << "make archmage";
    auto* fd = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_TRUE(fd && og::test::has_on_act_living(*fd)) << "archmage on_act_living callback exists";

    living* lv = static_cast<living*>(w.get());
    lv->stats()->set_level(20); // temp = 40-level = 20

    lv->set_drawcycle(1);
    short before = lv->view_all();
    og::test::on_act_living(*fd, lv);
    ASSERT_EQ(static_cast<int>(before), static_cast<int>(lv->view_all())) << "drawcycle not divisible by temp should not increment view_all";

    lv->set_drawcycle(20);
    og::test::on_act_living(*fd, lv);
    ASSERT_TRUE(lv->view_all() > before) << "drawcycle divisible by temp should increment view_all";
}


TEST(FamilyBehaviors, archmage_handle_teleport_and_special_guards)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    const auto* fd = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_TRUE(fd && og::test::has_handle_teleport(*fd) && og::test::has_do_special(*fd)) << "archmage callbacks exist";

    arch->set_ani_type(ANI_WALK);
    arch->set_cycle(5);
    ASSERT_TRUE(og::test::handle_teleport(*fd, arch)) << "handle_teleport should return true";
    ASSERT_EQ(ANI_TELE_IN, static_cast<int>(arch->ani_type())) << "handle_teleport should set tele-in";
    ASSERT_EQ(0, static_cast<int>(arch->cycle())) << "handle_teleport should reset cycle";

    // case 1 guard: already teleporting
    arch->set_current_special(1);
    arch->set_shifter_down(0);
    arch->set_ani_type(ANI_TELE_OUT);
    ASSERT_TRUE(!og::test::do_special(*fd, arch)) << "teleport special should fail while already teleporting";

    // case 1 guard: marker path but busy
    arch->set_ani_type(ANI_WALK);
    arch->set_shifter_down(1);
    arch->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*fd, arch)) << "marker path should fail when busy";

    // case 1 guard: low intelligence for marker
    arch->set_busy(0);
    auto low_int = std::make_unique<guy>(FAMILY_ARCHMAGE);
    low_int->intelligence = 30;
    arch->set_owned_myguy(std::move(low_int));
    arch->set_user(0);
    ASSERT_TRUE(!og::test::do_special(*fd, arch)) << "marker path should fail when int<75";
}


TEST(FamilyBehaviors, archmage_special_case2_case3_case4_guard_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    ASSERT_TRUE(arch != nullptr) << "archmage created";
    const auto* fd = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "archmage do_special callback exists";

    arch->stats()->set_magicpoints(5000);
    arch->stats()->set_special_cost(2, 0);
    arch->stats()->set_special_cost(3, 0);
    arch->stats()->set_special_cost(4, 0);

    // case 2 guard: busy
    arch->set_current_special(2);
    arch->set_busy(1);
    arch->set_shifter_down(0);
    ASSERT_TRUE(!og::test::do_special(*fd, arch)) << "heartburst should fail when busy";

    // case 2 guard: no foes in range
    arch->set_busy(0);
    ASSERT_TRUE(!og::test::do_special(*fd, arch)) << "heartburst should fail with zero foes";

    // case 3 guard: summon elemental needs int >= 150
    arch->set_current_special(3);
    arch->set_shifter_down(1);
    auto low_int = std::make_unique<guy>(FAMILY_ARCHMAGE);
    low_int->intelligence = 120;
    arch->set_owned_myguy(std::move(low_int));
    arch->set_user(0);
    arch->set_busy(0);
    ASSERT_TRUE(!og::test::do_special(*fd, arch)) << "true summon should fail when int<150";

    // case 4 guard: no charm candidates nearby
    arch->set_current_special(4);
    arch->set_shifter_down(0);
    arch->set_busy(0);
    ASSERT_TRUE(!og::test::do_special(*fd, arch)) << "mind control should fail with no nearby foes";
}


TEST(FamilyBehaviors, archmage_hit_response_threshold_and_retarget_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* arch = add_living_to_level(FAMILY_ARCHMAGE, 0, 100, 100);
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 132, 100);
    ASSERT_TRUE(arch != nullptr && foe != nullptr) << "archmage and foe should be created";
    const auto* fd = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_TRUE(fd && og::test::has_hit_response(*fd)) << "archmage hit_response callback exists";

    arch->stats()->set_special_cost(1, 0);
    arch->stats()->set_magicpoints(500);
    arch->stats()->set_level(9);
    arch->stats()->set_max_hitpoints(100);
    arch->stats()->set_hitpoints(10);
    arch->set_foe(nullptr);
    arch->set_busy(10);
    arch->set_shifter_down(1);
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1;

    og::test::hit_response(*fd, arch->stats(), foe);
    ASSERT_EQ(1, (int)arch->current_special()) << "low HP archmage should choose special 1";
    ASSERT_EQ(0, (int)arch->shifter_down()) << "low HP branch should clear shifter flag";

    // Exercise the retargeting/foe-assignment branch in the non-threshold path.
    arch->stats()->set_hitpoints(90);
    arch->set_foe(nullptr);
    foe->set_foe(nullptr);
    arch->stats()->set_last_distance(1);
    arch->stats()->set_current_distance(2);
    og::test::hit_response(*fd, arch->stats(), foe);

    ASSERT_TRUE(arch->foe() == foe) << "non-threshold branch should retarget controller to attacker";
    ASSERT_TRUE(foe->foe() == arch) << "non-threshold branch should set attacker foe back to controller";
    ASSERT_EQ(15000, (int)arch->stats()->last_distance()) << "retarget should reset last_distance";
    ASSERT_EQ(15000, (int)arch->stats()->current_distance()) << "retarget should reset current_distance";
}


TEST(FamilyBehaviors, cleric_check_special_ai_branch_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_check_special_ai(*fd)) << "cleric check_special_ai callback exists";

    living* lv = static_cast<living*>(cleric);
    lv->set_current_special(2);
    ASSERT_TRUE(og::test::check_special_ai(*fd, lv)) << "non-heal special should return true";

    lv->set_current_special(1);
    lv->stats()->set_magicpoints(0.0f);
    lv->stats()->set_max_magicpoints(100.0f);
    ASSERT_TRUE(!og::test::check_special_ai(*fd, lv)) << "heal special should return false with no friends and low magic";

    lv->stats()->set_magicpoints(60.0f);
    ASSERT_TRUE(og::test::check_special_ai(*fd, lv)) << "heal special should return true for mace mode when magic >= half";
    ASSERT_EQ(1, (int)lv->shifter_down()) << "mace mode should set shifter_down";

    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 108, 100);
    ASSERT_TRUE(ally != nullptr) << "ally created";
    lv->stats()->set_magicpoints(1.0f);
    ASSERT_TRUE(og::test::check_special_ai(*fd, lv)) << "heal special should return true when multiple allies nearby";
    ASSERT_EQ(0, (int)lv->shifter_down()) << "heal mode should clear shifter_down";

    og::runtime::current_session->myscreen_->world().delete_objects();
}


// --- handle_teleport: mage teleport-out completes ---
TEST(FamilyBehaviors, mage_handle_teleport)
{
    auto w = make_living(FAMILY_MAGE);
    ASSERT_TRUE(w != nullptr) << "make mage";
    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(0);
    // Pump animate() until the animation completes and transitions
    for (int i = 0; i < 50 && w->ani_type() == ANI_TELE_OUT; i++)
        w->animate();
    ASSERT_EQ(ANI_TELE_IN, (int)w->ani_type()) << "mage teleport-out should transition to ANI_TELE_IN";
}


// Skeleton teleport: uses teleport_ranged
TEST(FamilyBehaviors, skeleton_handle_teleport)
{
    auto w = make_living(FAMILY_SKELETON);
    ASSERT_TRUE(w != nullptr) << "make skeleton";
    w->set_ani_type(ANI_TELE_OUT);
    w->set_cycle(0);
    for (int i = 0; i < 50 && w->ani_type() == ANI_TELE_OUT; i++)
        w->animate();
    ASSERT_EQ(ANI_TELE_IN, (int)w->ani_type()) << "skeleton teleport-out should transition to ANI_TELE_IN";
}


// --- on_create: soldier weapons_left set from level ---
TEST(FamilyBehaviors, soldier_weapons_left_on_create)
{
    guy g(FAMILY_SOLDIER);
    g.teamnum = 0;
    g.upgrade_to_level(5, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    ASSERT_TRUE(w != nullptr) << "create soldier walker";
    ASSERT_EQ(3, (int)static_cast<living*>(w.get())->weapons_left()) << "soldier weapons_left should be (level+1)/2 = 3 at level 5";
}


static walker* add_stain_to_fxlist(int team, short x, short y)
{
    walker* ob = og::runtime::current_session->myscreen_->world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (!ob) return nullptr;
    ob->set_ignore(1);
    ob->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
    ob->set_team_num(static_cast<unsigned char>(team));
    ob->setxy(x, y);
    return ob;
}

TEST(FamilyBehaviors, cleric_check_special_ai_direct_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_check_special_ai(*fd)) << "cleric check_special_ai present";

    cleric->set_current_special(1);
    cleric->stats()->set_max_magicpoints(100);
    cleric->stats()->set_magicpoints(0);
    bool ok = og::test::check_special_ai(*fd, static_cast<living*>(cleric));
    ASSERT_TRUE(!ok) << "special=1 without allies and low MP should fail";

    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 120, 100);
    ASSERT_TRUE(ally != nullptr) << "ally created";
    ok = og::test::check_special_ai(*fd, static_cast<living*>(cleric));
    ASSERT_TRUE(ok) << "special=1 with an ally nearby should pass";
    ASSERT_EQ(0, (int)cleric->shifter_down()) << "heal mode should set shifter_down=0";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric recreated";
    cleric->set_current_special(1);
    cleric->stats()->set_max_magicpoints(100);
    cleric->stats()->set_magicpoints(50);
    ok = og::test::check_special_ai(*fd, static_cast<living*>(cleric));
    ASSERT_TRUE(ok) << "special=1 with no allies but MP>=half should pass";
    ASSERT_EQ(1, (int)cleric->shifter_down()) << "mace mode should set shifter_down=1";

    cleric->set_current_special(2);
    ok = og::test::check_special_ai(*fd, static_cast<living*>(cleric));
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
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    cleric->set_current_special(1);
    cleric->set_shifter_down(0);
    cleric->stats()->set_level(5);
    cleric->stats()->set_magicpoints(200);
    ally->stats()->set_max_hitpoints(100);
    ally->stats()->set_hitpoints(10);

    float hp_before = ally->stats()->hitpoints();
    float mp_before = cleric->stats()->magicpoints();
    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "heal special should succeed with an injured ally";
    ASSERT_TRUE(ally->stats()->hitpoints() > hp_before) << "ally HP should increase";
    ASSERT_TRUE(cleric->stats()->magicpoints() < mp_before) << "cleric MP should decrease";

    ally->stats()->set_hitpoints(ally->stats()->max_hitpoints());
    ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(!ok) << "heal special should fail when nobody is healable";
}


TEST(FamilyBehaviors, cleric_mystic_mace_gates)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    cleric->set_current_special(1);
    cleric->set_shifter_down(1);
    cleric->set_busy(1);
    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(!ok) << "mystic mace should fail while busy";

    cleric->set_busy(0);
    auto low_int = std::make_unique<guy>(FAMILY_CLERIC);
    low_int->intelligence = 40;
    cleric->set_owned_myguy(std::move(low_int));
    cleric->set_user(0);
    ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(!ok) << "mystic mace should fail when int<50";
}


TEST(FamilyBehaviors, cleric_turn_undead_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    auto low_int = std::make_unique<guy>(FAMILY_CLERIC);
    low_int->intelligence = 50;
    cleric->set_owned_myguy(std::move(low_int));
    cleric->set_busy(0);
    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(!ok) << "turn undead should fail at int<60";
    ASSERT_TRUE(cleric->busy() >= 5) << "failed int gate should add busy delay";

    auto good_int = std::make_unique<guy>(FAMILY_CLERIC);
    good_int->intelligence = 80;
    cleric->set_owned_myguy(std::move(good_int));
    cleric->set_busy(0);
    ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(!ok) << "turn undead should fail when no undead foes are in range";

}


TEST(FamilyBehaviors, cleric_mystic_mace_success_path_direct)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    cleric->set_current_special(1);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    cleric->stats()->set_magicpoints(200);
    cleric->stats()->set_special_cost(1, 0);
    cleric->stats()->set_level(6);
    auto smart = std::make_unique<guy>(FAMILY_CLERIC);
    smart->intelligence = 120;
    cleric->set_owned_myguy(std::move(smart));

    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "mystic mace should succeed with enough INT and not busy";
    ASSERT_TRUE(cleric->busy() > 0) << "mystic mace success should add busy delay";

    bool found_shield = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w->query_order() == Order::FX && w->family() == FAMILY_MAGIC_SHIELD &&
            w->owner() == cleric)
        {
            found_shield = true;
            break;
        }
    }
    for (auto& uptr : og::runtime::current_session->myscreen_->world().fxlist)
    {
        walker* w = uptr.get();
        if (w && w->family() == FAMILY_MAGIC_SHIELD && w->owner() == cleric)
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
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    cleric->set_team_num(1);
    cleric->stats()->set_level(6);
    // turn_undead() uses world RNG directly; seed it so the kill check is
    // deterministic under shuffled execution as well.
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1;

    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "turn undead branch should execute when an undead foe is nearby";
    ASSERT_TRUE(skeleton->dead() || skeleton->stats()->hitpoints() <= 0) << "turn undead should remove or kill nearby undead target";
}


// packs/core/families/living-05-cleric.lua do_turn_undead: with the shifter
// down, specials 2 and 3 both route to turn undead, and every undead the cast
// destroys is worth exp_from_action(..., "turn_undead", N) == N * 3
// (src/core/combat_math.cpp, ExpAction::TurnUndead).
TEST(FamilyBehaviors, cleric_turn_undead_special2_and_3_grant_three_exp_per_kill)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    // Special 2 / shifter_down path with myguy should pass generic>0 branch.
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    walker* skeleton = add_living_to_level(FAMILY_SKELETON, 2, 108, 100);
    ASSERT_TRUE(cleric && skeleton) << "cleric+skeleton created";

    auto c2 = std::make_unique<guy>(FAMILY_CLERIC);
    c2->intelligence = 80;
    c2->name = "Turner2";
    c2->exp = 0;
    cleric->set_owned_myguy(std::move(c2));
    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    cleric->stats()->set_level(6);
    // turn_undead() rolls the world RNG directly; seed it AFTER
    // create_new_grid (which consumes the stream) so the one kill is certain,
    // exactly as cleric_turn_undead_success_with_undead_targets does.
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1;
    const std::uint32_t exp_before_2 = cleric->myguy ? cleric->myguy->exp : 0u;
    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "turn undead special2 shifter path should succeed";
    ASSERT_TRUE(skeleton->dead() || skeleton->stats()->hitpoints() <= 0.0f)
        << "turn undead special2 must destroy the one undead in range";
    ASSERT_TRUE(cleric->myguy != nullptr) << "cleric still carries its guy";
    ASSERT_EQ(exp_before_2 + 3u, cleric->myguy->exp)
        << "one turned undead is worth exactly 3 exp";

    // Special 3 / shifter_down path with myguy should also pass generic>0 branch.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    walker* skeleton2 = add_living_to_level(FAMILY_SKELETON, 2, 108, 100);
    ASSERT_TRUE(cleric && skeleton2) << "cleric+skeleton recreated";

    auto c3 = std::make_unique<guy>(FAMILY_CLERIC);
    c3->intelligence = 80;
    c3->name = "Turner3";
    c3->exp = 0;
    cleric->set_owned_myguy(std::move(c3));
    cleric->set_current_special(3);
    cleric->set_shifter_down(1);
    cleric->set_busy(0);
    cleric->stats()->set_level(6);
    og::runtime::current_session->myscreen_->world().rng_.state_ = 1;
    const std::uint32_t exp_before_3 = cleric->myguy ? cleric->myguy->exp : 0u;
    ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "turn undead special3 shifter path should succeed";
    ASSERT_TRUE(skeleton2->dead() || skeleton2->stats()->hitpoints() <= 0.0f)
        << "turn undead special3 must destroy the one undead in range";
    ASSERT_TRUE(cleric->myguy != nullptr) << "cleric still carries its guy";
    ASSERT_EQ(exp_before_3 + 3u, cleric->myguy->exp)
        << "one turned undead is worth exactly 3 exp";
}


TEST(FamilyBehaviors, cleric_resurrect_penalty_underflow_clamps_to_zero)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    auto hero = std::make_unique<guy>(FAMILY_CLERIC);
    hero->exp = 0;
    cleric->set_owned_myguy(std::move(hero));
    cleric->set_current_special(4);

    walker* blood_friend = add_stain_to_fxlist(0, 110, 100);
    ASSERT_TRUE(blood_friend != nullptr) << "friendly blood created";
    blood_friend->stats()->set_old_family(FAMILY_SOLDIER);

    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "resurrect should succeed for nearby friendly blood";
    ASSERT_EQ(90u, cleric->myguy->exp) << "resurrect should clamp the penalty at zero before awarding the fixed resurrect XP";
}


TEST(FamilyBehaviors, cleric_raise_skeleton_and_ghost_from_blood)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    walker* blood = add_stain_to_fxlist(1, 110, 100);
    ASSERT_TRUE(blood != nullptr) << "blood created";
    cleric->set_current_special(2);
    cleric->set_shifter_down(0);
    cleric->stats()->set_level(6);
    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "raise skeleton should succeed when blood is nearby and passable";
    ASSERT_TRUE(blood->dead()) << "blood should be consumed by raise skeleton";

    bool found_skeleton = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w != cleric && w->query_order() == Order::Living &&
            w->family() == FAMILY_SKELETON && w->owner() == cleric)
        {
            found_skeleton = true;
            break;
        }
    }
    ASSERT_TRUE(found_skeleton) << "raise skeleton should spawn a summoned skeleton";

    blood = add_stain_to_fxlist(1, 115, 100);
    ASSERT_TRUE(blood != nullptr) << "second blood created";
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "raise ghost should succeed when blood is close (<30)";
    ASSERT_TRUE(blood->dead()) << "blood should be consumed by raise ghost";

    bool found_ghost = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w != cleric && w->query_order() == Order::Living &&
            w->family() == FAMILY_GHOST && w->owner() == cleric)
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
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    walker* blood_friend = add_stain_to_fxlist(0, 110, 100);
    ASSERT_TRUE(blood_friend != nullptr) << "friendly blood created";
    blood_friend->stats()->set_old_family(FAMILY_SOLDIER);
    cleric->set_current_special(4);
    bool ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "resurrect should succeed for friendly blood";
    ASSERT_TRUE(blood_friend->dead()) << "friendly blood should be consumed";

    bool found_resurrected_friend = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w != cleric && w->query_order() == Order::Living &&
            w->family() == FAMILY_SOLDIER && w->team_num() == 0)
        {
            found_resurrected_friend = true;
            break;
        }
    }
    ASSERT_TRUE(found_resurrected_friend) << "friendly blood should resurrect old_family";

    walker* blood_enemy = add_stain_to_fxlist(1, 112, 100);
    ASSERT_TRUE(blood_enemy != nullptr) << "enemy blood created";
    blood_enemy->stats()->set_old_family(FAMILY_ORC);
    ok = og::test::do_special(*fd, cleric);
    ASSERT_TRUE(ok) << "resurrect should also succeed for enemy blood";
    ASSERT_TRUE(blood_enemy->dead()) << "enemy blood should be consumed";

    bool found_enemy_ghost = false;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().oblist)
    {
        walker* w = uptr.get();
        if (w && w != cleric && w->query_order() == Order::Living &&
            w->family() == FAMILY_GHOST && w->team_num() == cleric->team_num() &&
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
    ASSERT_TRUE(fd && og::test::has_check_special_ai(*fd)) << "thief check_special_ai present";

    // special 1 with foe at 35<distance<130 should fail.
    thief->set_current_special(1);
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    thief->set_foe(foe);
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "drop bomb AI should fail at medium range";

    // special 1 with close foe should pass.
    foe->setxy(120, 100);
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "drop bomb AI should pass when foe is close";

    // special 1 without foe needs >=3 nearby foes.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief recreated for foe-count branch";
    thief->set_current_special(1);
    thief->set_foe(nullptr);
    walker* e1 = add_living_to_level(FAMILY_ORC, 1, 130, 100);
    walker* e2 = add_living_to_level(FAMILY_ORC, 1, 140, 100);
    ASSERT_TRUE(e1 && e2) << "two nearby foes created";
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "drop bomb AI should fail with fewer than 3 foes";
    walker* e3 = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(e3 != nullptr) << "third nearby foe created";
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "drop bomb AI should pass with 3+ foes";

    // special 3 uses two different ranges depending on shifter_down.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief recreated";
    thief->set_current_special(3);
    thief->stats()->set_level(1);
    thief->set_shifter_down(0);
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "taunt/charm AI should fail without foes";
    foe = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "foe for special 3 created";
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "taunt/charm AI should pass with foe in normal range";

    thief->set_shifter_down(1); // short charm range: 16 + 4*level = 20
    foe->setxy(130, 100);
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "charm AI should fail outside short range";
    foe->setxy(115, 100);
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "charm AI should pass inside short range";

    thief->set_current_special(2);
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(thief))) << "non-1/non-3 thief specials should pass AI check";
}


TEST(FamilyBehaviors, thief_batch3_special_taunt_charm_and_poison_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    const auto* fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "thief do_special present";

    thief->stats()->set_magicpoints(1000);

    // special 3 taunt busy guard.
    thief->set_current_special(3);
    thief->set_shifter_down(0);
    thief->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*fd, thief)) << "taunt should fail when busy";

    // taunt success and myguy-name message path.
    thief->set_busy(0);
    auto thief_guy = std::make_unique<guy>(FAMILY_THIEF);
    thief_guy->name = "Sneak";
    thief->set_owned_myguy(std::move(thief_guy));
    ASSERT_TRUE(og::test::do_special(*fd, thief)) << "taunt should succeed when not busy";
    ASSERT_TRUE(thief->busy() >= 2) << "taunt should add busy time";

    // charm busy guard.
    thief->set_shifter_down(1);
    thief->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*fd, thief)) << "charm should fail when busy";

    // charm no-foe guard.
    thief->set_busy(0);
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief recreated";
    thief->stats()->set_magicpoints(1000);
    thief->set_current_special(3);
    thief->set_shifter_down(1);
    thief->stats()->name = "Sneak";
    ASSERT_TRUE(!og::test::do_special(*fd, thief)) << "charm should fail with no targets";

    // deterministic failed charm branch: thief level lower than target.
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 112, 100);
    ASSERT_TRUE(foe != nullptr) << "foe created";
    thief->stats()->set_level(1);
    foe->stats()->set_level(10);
    thief->set_busy(0);
    ASSERT_TRUE(og::test::do_special(*fd, thief)) << "charm should run when target is in range";
    ASSERT_TRUE(thief->busy() >= 10) << "charm should add busy time";
    ASSERT_TRUE(foe->foe() == thief) << "failed charm path should make foe attack thief";

    // poison cloud guards and success path.
    thief->set_current_special(4);
    thief->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*fd, thief)) << "poison cloud should fail when busy";
    thief->set_busy(0);
    ASSERT_TRUE(og::test::do_special(*fd, thief)) << "poison cloud should succeed when not busy";
}


TEST(FamilyBehaviors, druid_batch3_special_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    ASSERT_TRUE(druid != nullptr) << "druid created";
    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "druid do_special present";

    druid->stats()->set_magicpoints(1000);
    druid->stats()->set_level(5);

    // Busy guards for cases 1/2/3/4.
    druid->set_busy(1);
    druid->set_current_special(1);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "plant tree should fail when busy";
    druid->set_current_special(2);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "summon faerie should fail when busy";
    druid->set_current_special(3);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "reveal should fail when busy";
    druid->set_current_special(4);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "protection should fail when busy";

    // Reveal success path.
    druid->set_busy(0);
    druid->set_current_special(3);
    const short view_before = druid->view_all();
    ASSERT_TRUE(og::test::do_special(*fd, druid)) << "reveal should succeed when not busy";
    // reveal_items (living-13-druid.lua): view_all += level * 10, and this
    // druid was set to level 5. A ">" pin passed on a +1 dribble.
    ASSERT_EQ(static_cast<int>(view_before) + 50, static_cast<int>(druid->view_all()))
        << "REVEAL grants exactly level * 10 ticks of item sight";

    // Protection fails when only self is present.
    druid->set_current_special(4);
    druid->set_busy(0);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "protection should fail with no allies in range";

    // Protection success: create ally and then refresh existing circle.
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 110, 100);
    ASSERT_TRUE(ally != nullptr) << "ally created";
    ASSERT_TRUE(og::test::do_special(*fd, druid)) << "protection should succeed with ally in range";

    walker* existing_circle = nullptr;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().weaplist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() && w->family() == FAMILY_CIRCLE_PROTECTION &&
            w->owner() == ally)
        {
            existing_circle = w;
            break;
        }
    }
    ASSERT_TRUE(existing_circle != nullptr) << "protection should spawn the ally's circle weapon";
    float hp_before = existing_circle->stats()->hitpoints();
    ASSERT_TRUE(og::test::do_special(*fd, druid)) << "second protection cast should refresh existing circle";
    ASSERT_GT(existing_circle->stats()->hitpoints(), hp_before)
        << "a recast must pour a fresh circle's charge into the existing ring";
}


TEST(FamilyBehaviors, orc_batch3_special_and_ai_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    const auto* fd = get_family_descriptor(FAMILY_ORC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd) && og::test::has_check_special_ai(*fd)) << "orc callbacks present";

    orc->stats()->set_magicpoints(1000);
    orc->stats()->set_level(4);

    // Howl busy guard.
    orc->set_current_special(1);
    orc->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*fd, orc)) << "howl should fail when busy";

    // Howl success with foes both with/without myguy branch.
    orc->set_busy(0);
    walker* foe_named = add_living_to_level(FAMILY_SOLDIER, 1, 120, 100);
    walker* foe_plain = add_living_to_level(FAMILY_SOLDIER, 1, 130, 100);
    ASSERT_TRUE(foe_named && foe_plain) << "foes for howl created";
    // packs/core/families/living-14-orc.lua yell:
    //   stun = max(0, yell_stun_base + rand0(level*10) - rand0(con*10)).
    // Both rolls are forced to zero so the stun is EXACTLY yell_stun_base:
    // og.rand0(0) answers 0 without touching the generator, so a level-0 orc
    // kills the level roll, a zero-constitution guy kills the con roll on the
    // has_guy branch, and hp < 30 makes trunc(hp/30) == 0 on the other.
    auto foe_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    foe_guy->constitution = 0;
    foe_named->set_owned_myguy(std::move(foe_guy));
    foe_plain->stats()->set_hitpoints(20.0f);
    orc->stats()->set_level(0);
    ASSERT_EQ(0, (int)foe_named->stats()->frozen_delay()) << "named foe starts unfrozen";
    ASSERT_EQ(0, (int)foe_plain->stats()->frozen_delay()) << "plain foe starts unfrozen";
    ASSERT_TRUE(og::test::do_special(*fd, orc)) << "howl should succeed when not busy";
    ASSERT_EQ(10, (int)foe_named->stats()->frozen_delay())
        << "howl must stun a foe that carries a guy by yell_stun_base (10)";
    ASSERT_EQ(10, (int)foe_plain->stats()->frozen_delay())
        << "howl must stun a guy-less foe by yell_stun_base (10)";
    orc->stats()->set_level(4);

    // Eat-corpse guards and success path.
    orc->set_current_special(2);
    orc->stats()->set_hitpoints(orc->stats()->max_hitpoints());
    ASSERT_TRUE(!og::test::do_special(*fd, orc)) << "eat corpse should fail at full HP";
    orc->stats()->set_hitpoints(orc->stats()->max_hitpoints() - 20.0f);
    ASSERT_TRUE(!og::test::do_special(*fd, orc)) << "eat corpse should fail without blood";

    walker* far_blood = add_stain_to_fxlist(1, 200, 100);
    ASSERT_TRUE(far_blood != nullptr) << "far blood created";
    far_blood->stats()->set_level(3);
    ASSERT_TRUE(!og::test::do_special(*fd, orc)) << "eat corpse should fail when blood is too far";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc recreated";
    orc->set_current_special(2);
    orc->stats()->set_hitpoints(orc->stats()->max_hitpoints() - 20.0f);
    auto orc_guy = std::make_unique<guy>(FAMILY_ORC);
    orc_guy->name = "Gruk";
    orc->set_owned_myguy(std::move(orc_guy));
    walker* near_blood = add_stain_to_fxlist(1, 101, 100);
    ASSERT_TRUE(near_blood != nullptr) << "near blood created";
    near_blood->stats()->set_level(4);
    ASSERT_TRUE(og::test::do_special(*fd, orc)) << "eat corpse should succeed when blood is close";
    ASSERT_TRUE(near_blood->dead()) << "eaten blood object should be marked dead";

    // check_special_ai with preset foe in/out of range.
    walker* foe = add_living_to_level(FAMILY_SOLDIER, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "foe for AI checks created";
    orc->set_foe(foe);
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(orc))) << "orc AI should pass when foe is in range";
    foe->setxy(260, 100);
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(orc))) << "orc AI should fail when foe is out of range";

    // check_special_ai with no foe should query nearest foe.
    orc->set_foe(nullptr);
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc recreated for nearest-foe branch";
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(orc))) << "orc AI should fail when no nearby foe exists";
    foe = add_living_to_level(FAMILY_SOLDIER, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "near foe created";
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(orc))) << "orc AI should pass after finding nearby foe";
}


TEST(FamilyBehaviors, soldier_batch3_special_ai_and_fire_callback_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    const auto* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd) && og::test::has_check_special_ai(*fd) && og::test::has_on_fire_weapon(*fd)) << "soldier callbacks present";

    soldier->stats()->set_magicpoints(1000);
    soldier->stats()->set_level(6);

    // Charge blocked path.
    soldier->set_current_special(1);
    soldier->set_curdir(FACE_LEFT);
    soldier->setxy(0, 0);
    ASSERT_TRUE(!og::test::do_special(*fd, soldier)) << "charge should fail when forward is blocked";

    // Whirlwind busy guard.
    soldier->set_current_special(3);
    soldier->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*fd, soldier)) << "whirlwind should fail when busy";

    // Disarm guards.
    soldier->set_current_special(4);
    soldier->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*fd, soldier)) << "disarm should fail when busy";
    soldier->set_busy(0);
    soldier->setxy(100, 100);
    soldier->set_curdir(FACE_RIGHT);
    ASSERT_TRUE(!og::test::do_special(*fd, soldier)) << "disarm should fail when forward is not blocked";

    // Make forward blocked and no foes in range -> fail.
    soldier->setxy(0, 100);
    soldier->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!og::test::do_special(*fd, soldier)) << "disarm should fail when blocked but no foes are in range";

    // Add a nearby foe so disarm succeeds.
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 8, 100);
    ASSERT_TRUE(foe != nullptr) << "foe for disarm created";
    soldier->set_busy(0);
    ASSERT_TRUE(og::test::do_special(*fd, soldier)) << "disarm should succeed when foe is nearby and blocked";

    // check_special_ai direct branches.
    soldier->set_foe(foe);
    foe->setxy(40, 100);
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(soldier))) << "soldier AI should pass in 20-75 range";
    foe->setxy(200, 100);
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(soldier))) << "soldier AI should fail when foe too far";
    soldier->set_foe(nullptr);
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier recreated for nearest-foe branch";
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(soldier))) << "soldier AI should fail without nearby foe";
    foe = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(foe != nullptr) << "near foe for AI created";
    ASSERT_TRUE(og::test::check_special_ai(*fd, static_cast<living*>(soldier))) << "soldier AI should pass after finding nearby foe";

    // on_fire_weapon callback: no weapons left path and decrement path.
    walker* weapon = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weapon != nullptr) << "weapon created";
    static_cast<living*>(soldier)->set_weapons_left(0);
    float mp_before = soldier->stats()->magicpoints();
    ASSERT_TRUE(!og::test::on_fire_weapon(*fd, soldier, weapon)) << "on_fire_weapon should fail when weapons_left<=0";
    ASSERT_TRUE(weapon->dead()) << "weapon should be marked dead when out of throws";
    ASSERT_TRUE(soldier->stats()->magicpoints() > mp_before) << "failed throw should refund weapon cost";

    weapon = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(weapon != nullptr) << "second weapon created";
    static_cast<living*>(soldier)->set_weapons_left(2);
    ASSERT_TRUE(og::test::on_fire_weapon(*fd, soldier, weapon)) << "on_fire_weapon should succeed when throws remain";
    ASSERT_EQ(1, (int)static_cast<living*>(soldier)->weapons_left()) << "successful throw should decrement weapons_left";
}


TEST(FamilyBehaviors, family_batch4_druid_refresh_oblist_and_failure_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "druid callback present";

    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    walker* ally1 = add_living_to_level(FAMILY_SOLDIER, 0, 108, 100);
    walker* ally2 = add_living_to_level(FAMILY_ARCHER, 0, 112, 100);
    ASSERT_TRUE(druid && ally1 && ally2) << "druid and allies created";

    druid->stats()->set_magicpoints(1000);
    druid->stats()->set_level(6);
    druid->set_owned_myguy(std::make_unique<guy>(FAMILY_DRUID));

    // Cases 1 and 2 both REFUND the weapon cost into the pool before calling
    // fire(), which then charges it again -- so even a 9999-point cost is
    // always affordable and the shot goes out. The two calls used to be
    // `(void)`-cast, which hid both the return and that net-zero MP ledger.
    druid->stats()->set_weapon_cost(9999);
    druid->set_current_special(1);
    druid->set_busy(0);
    const float mp_before_tree = druid->stats()->magicpoints();
    EXPECT_TRUE(og::test::do_special(*fd, druid))
        << "plant tree: the pre-refund keeps fire() affordable at any weapon cost";
    EXPECT_FLOAT_EQ(mp_before_tree, druid->stats()->magicpoints())
        << "plant tree refunds the weapon cost, then fire() charges it: net zero MP";
    EXPECT_FLOAT_EQ(18.0f, druid->busy())
        << "plant tree spends fire_frequency * 2 = 18 ticks of busy";
    EXPECT_EQ(1, count_family_in_weaplist(FAMILY_TREE))
        << "plant tree leaves exactly one grown tree behind";

    // ... and with that busy timer still running, the faerie arm refuses.
    druid->set_current_special(2);
    const float mp_before_faerie = druid->stats()->magicpoints();
    EXPECT_FALSE(og::test::do_special(*fd, druid))
        << "summon faerie refuses while the plant-tree busy timer is running";
    EXPECT_FLOAT_EQ(mp_before_faerie, druid->stats()->magicpoints())
        << "a refused faerie summon charges nothing";
    EXPECT_EQ(0, count_family_in_oblist(FAMILY_FAERIE))
        << "a refused faerie summon spawns nothing";
    druid->stats()->set_weapon_cost(0);

    // Summon faerie passability failure path.
    druid->set_current_special(2);
    druid->setxy(-200, -200);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "summon faerie should fail when spawn tile is impassable";
    druid->setxy(100, 100);

    // Protection refresh branch requires existing circle in oblist.
    walker* existing = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(existing != nullptr) << "existing protection object created";
    if (existing) {
        existing->set_owner(ally1);
        // See druid_round6_...: the refresh scan is owner-filtered but still
        // range-bounded, so the ring has to sit on its owner to be found.
        existing->center_on(ally1);
        existing->stats()->set_hitpoints(5);
    }
    druid->set_current_special(4);
    druid->set_busy(0);
    ASSERT_TRUE(og::test::do_special(*fd, druid)) << "protection should succeed with multiple allies";
    ASSERT_GT(existing->stats()->hitpoints(), 5.0f)
        << "the ally's existing protection ring must be topped up, not ignored";
}


TEST(FamilyBehaviors, family_batch4_soldier_orc_thief_edge_callbacks)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* sold_fd = get_family_descriptor(FAMILY_SOLDIER);
    const auto* orc_fd = get_family_descriptor(FAMILY_ORC);
    const auto* thief_fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_TRUE(sold_fd && orc_fd && thief_fd) << "family descriptors available";

    // Soldier default-special branch.
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    if (soldier) {
        soldier->set_current_special(99);
        ASSERT_TRUE(og::test::do_special(*sold_fd, soldier)) << "unknown soldier special should fall through and succeed";
        soldier->set_foe(nullptr);
        ASSERT_TRUE(!og::test::check_special_ai(*sold_fd, static_cast<living*>(soldier))) << "soldier AI should fail with no nearby foe";
    }

    // Orc AI no-foe branch and default corpse message path.
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    if (orc) {
        orc->set_foe(nullptr);
        ASSERT_TRUE(!og::test::check_special_ai(*orc_fd, static_cast<living*>(orc))) << "orc AI should fail with no nearby foe";

        orc->set_current_special(2);
        orc->stats()->set_hitpoints(orc->stats()->max_hitpoints() - 10.0f);
        orc->stats()->name.clear();
        orc->clear_myguy();
        walker* blood = add_stain_to_fxlist(1, 101, 100);
        ASSERT_TRUE(blood != nullptr) << "blood stain for eat-corpse created";
        if (blood)
            blood->stats()->set_level(2);
        ASSERT_TRUE(og::test::do_special(*orc_fd, orc)) << "orc should eat corpse via default message branch";
        ASSERT_EQ(1, (int)orc_fd->promotion_new_level(42)) << "orc promotion callback should return level 1";
    }

    // Thief: drop bomb AI run-away and charm success branch.
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    walker* foe = add_living_to_level(FAMILY_SOLDIER, 1, 110, 100);
    ASSERT_TRUE(thief && foe) << "thief and foe created";
    thief->stats()->set_magicpoints(1000);

    thief->set_current_special(1);
    thief->set_user(-1);
    ASSERT_TRUE(og::test::do_special(*thief_fd, thief)) << "drop bomb should succeed and schedule run-away for AI";

    thief->set_current_special(3);
    thief->set_shifter_down(1);
    thief->set_busy(0);
    thief->stats()->set_level(9);
    foe->stats()->set_level(1);
    thief->set_foe(foe);
    const unsigned char foe_team_before = foe->team_num();
    const float busy_before_charm = thief->busy();
    {
        // og.rand(20) == 0 is the RESIST roll; a constant 1 takes the charm
        // arm. This generator used to be constructed and never installed, so
        // the "favorable deterministic RNG" in the message below was a claim
        // about the world's own stream. Install it on the seam that actually
        // feeds og.rand.
        ConstRandomFamily rng_nonzero(1);
        ScopedSimRandom steer(&rng_nonzero);
        ASSERT_TRUE(og::test::do_special(*thief_fd, thief))
            << "charm should succeed with favorable deterministic RNG";
    }
    ASSERT_EQ(static_cast<int>(thief->team_num()), static_cast<int>(foe->team_num()))
        << "successful charm should switch foe team";
    ASSERT_EQ(static_cast<int>(foe_team_before), static_cast<int>(foe->real_team_num()))
        << "the charmed foe remembers the team it came from";
    // charm_duration_base 75 + level_diff 8 * 25 = 275, below soften()'s 375 knee.
    ASSERT_EQ(275, static_cast<int>(foe->charm_left()))
        << "charm lasts charm_duration_base + level_diff * charm_duration_per_diff";
    ASSERT_FLOAT_EQ(busy_before_charm + 10.0f, thief->busy())
        << "a landed charm costs the thief 10 ticks of busy";
}


TEST(FamilyBehaviors, family_batch5_cleric_on_shoved_and_elf_fire_fail_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* cleric_fd = get_family_descriptor(FAMILY_CLERIC);
    const auto* elf_fd = get_family_descriptor(FAMILY_ELF);
    ASSERT_TRUE(cleric_fd && og::test::has_on_shoved(*cleric_fd) && elf_fd && og::test::has_do_special(*elf_fd)) << "cleric/elf callbacks present";

    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    if (cleric)
    {
        cleric->set_current_special(4);
        og::test::on_shoved(*cleric_fd, cleric);
        ASSERT_EQ(1, (int)cleric->current_special()) << "cleric on_shoved should force heal special";
    }

    walker* elf = add_living_to_level(FAMILY_ELF, 0, 100, 100);
    ASSERT_TRUE(elf != nullptr) << "elf created";
    if (elf)
    {
        // Keep MP deeply negative so each special's pre-bonus still leaves
        // fire() below weapon_cost and returns null deterministically.
        elf->stats()->set_magicpoints(-1000);

        elf->set_current_special(1);
        ASSERT_TRUE(!og::test::do_special(*elf_fd, elf)) << "elf special 1 should fail when fire() fails";
        elf->set_current_special(2);
        ASSERT_TRUE(!og::test::do_special(*elf_fd, elf)) << "elf special 2 should fail when fire() fails";
        elf->set_current_special(3);
        ASSERT_TRUE(!og::test::do_special(*elf_fd, elf)) << "elf special 3 should fail when fire() fails";
        elf->set_current_special(4);
        ASSERT_TRUE(!og::test::do_special(*elf_fd, elf)) << "elf special 4 should fail when fire() fails";
    }
}


TEST(FamilyBehaviors, druid_batch5_fire_fail_and_existing_protection_refresh_branch)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "druid callback present";

    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 112, 100);
    ASSERT_TRUE(druid && ally) << "druid and ally created";

    // Force fire() failure in specials 1/2: MP remains below weapon_cost even
    // after do_special's pre-fire MP adjustment.
    druid->stats()->set_weapon_cost(10);
    druid->stats()->set_magicpoints(-1000);
    druid->set_busy(0);

    druid->set_current_special(1);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "druid special 1 should fail when fire() returns null";

    druid->set_current_special(2);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "druid special 2 should fail when fire() returns null";

    (void)ally;
}


TEST(FamilyBehaviors, mage_batch3_special_and_promotion_branches)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* mage = add_living_to_level(FAMILY_MAGE, 1, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    const auto* fd = get_family_descriptor(FAMILY_MAGE);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd) && og::test::has_check_special_ai(*fd) && fd->promotion_new_level) << "mage callbacks present";

    // check_special_ai false branch (1-3 foes in range).
    mage->set_current_special(1);
    add_living_to_level(FAMILY_ORC, 0, 150, 100);
    add_living_to_level(FAMILY_ORC, 0, 160, 100);
    ASSERT_TRUE(!og::test::check_special_ai(*fd, static_cast<living*>(mage))) << "mage AI should be false with 2 nearby foes";

    // Teleport marker path without myguy (lifetime from level).
    mage->stats()->set_level(8);
    mage->stats()->set_magicpoints(500);
    mage->set_current_special(1);
    mage->set_shifter_down(1);
    mage->set_busy(0);
    mage->set_user(-1);
    ASSERT_TRUE(og::test::do_special(*fd, mage)) << "mage marker placement should succeed";

    // Starburst low-mana branch (generic <= 0 path).
    mage->set_current_special(2);
    mage->stats()->set_special_cost(2, 1000);
    mage->stats()->set_magicpoints(1);
    ASSERT_TRUE(og::test::do_special(*fd, mage)) << "mage starburst should still execute with low mana";

    // A company-owned Mage on another color must use its own team's
    // bonus-round path, never the active team's global enemy-freeze bank.
    mage->set_current_special(3);
    mage->set_team_num(1);
    mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    og::runtime::current_session->myscreen_->world().my_team = 0;
    og::runtime::current_session->myscreen_->world().enemy_freeze = 0;
    walker* ally = add_living_to_level(FAMILY_ORC, 1, 110, 100);
    ASSERT_TRUE(ally != nullptr) << "ally for freeze-time created";
    short bonus_before = ally->bonus_rounds();
    ASSERT_TRUE(og::test::do_special(*fd, mage)) << "enemy freeze-time should succeed";
    ASSERT_TRUE(ally->bonus_rounds() >= bonus_before) << "enemy freeze-time should add ally bonus rounds";
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->world().enemy_freeze)
        << "the foreign-color caster must not freeze its own side";

    // Energy wave guard: fire() returns null when weapon_cost > magicpoints.
    mage->set_current_special(4);
    mage->stats()->set_magicpoints(0);
    ASSERT_TRUE(!og::test::do_special(*fd, mage)) << "energy wave should fail when fire() cannot create projectile";

    // Heartburst guard: no foes in range. (The slot used to be selected on the
    // OLD mage, before the world was rebuilt, so the recreated mage cast its
    // default slot 1 instead and the `(void)`-cast return hid that entirely.)
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    mage = add_living_to_level(FAMILY_MAGE, 1, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage recreated for heartburst guard";
    mage->set_current_special(5);
    mage->stats()->set_magicpoints(500);
    const float mp_before_burst = mage->stats()->magicpoints();
    const float busy_before_burst = mage->busy();
    EXPECT_FALSE(og::test::do_special(*fd, mage))
        << "heartburst refuses outright when no foe is in range";
    EXPECT_FLOAT_EQ(mp_before_burst, mage->stats()->magicpoints())
        << "a refused heartburst spends no magic";
    EXPECT_FLOAT_EQ(busy_before_burst, mage->busy())
        << "a refused heartburst costs no busy time";
    EXPECT_EQ(0, count_family_in_fxlist(FAMILY_EXPLOSION))
        << "a refused heartburst summons no bursts";

    ASSERT_EQ(3, (int)fd->promotion_new_level(10)) << "mage promotion level formula should match legacy behavior";
}


TEST(FamilyBehaviors, family_batch6_soldier_orc_mage_callback_edge_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* soldier_fd = get_family_descriptor(FAMILY_SOLDIER);
    const auto* orc_fd = get_family_descriptor(FAMILY_ORC);
    const auto* mage_fd = get_family_descriptor(FAMILY_MAGE);
    ASSERT_TRUE(soldier_fd && og::test::has_do_special(*soldier_fd) && og::test::has_check_special_ai(*soldier_fd)) << "soldier callbacks present";
    ASSERT_TRUE(orc_fd && og::test::has_do_special(*orc_fd)) << "orc callbacks present";
    ASSERT_TRUE(mage_fd && og::test::has_check_special_ai(*mage_fd)) << "mage callbacks present";

    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, -300, -300);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    if (soldier)
    {
        // charge blocked branch
        soldier->set_current_special(1);
        soldier->set_curdir(FACE_RIGHT);
        ASSERT_TRUE(!og::test::do_special(*soldier_fd, soldier)) << "charge should fail when forward is blocked";

        // check_special_ai no-foe + no-near-foe path
        soldier->set_foe(nullptr);
        ASSERT_TRUE(!og::test::check_special_ai(*soldier_fd, static_cast<living*>(soldier))) << "soldier AI should fail when no foe can be found";
    }

    walker* orc = add_living_to_level(FAMILY_ORC, 0, 120, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    if (orc)
    {
        orc->set_current_special(1);
        orc->set_busy(2);
        ASSERT_TRUE(!og::test::do_special(*orc_fd, orc)) << "orc howl should fail while busy";
    }

    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    if (mage)
    {
        // check_special_ai with exactly 1-3 foes in range should return false.
        add_living_to_level(FAMILY_ORC, 1, 120, 100);
        add_living_to_level(FAMILY_ORC, 1, 130, 100);
        ASSERT_TRUE(!og::test::check_special_ai(*mage_fd, static_cast<living*>(mage))) << "mage AI should be false with 2 nearby foes";
    }
}


TEST(FamilyBehaviors, cleric_raise_and_resurrect_distance_and_busy_guards)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";

    // Turn-undead busy guard.
    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    cleric->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*fd, cleric)) << "turn undead should fail while busy";
    cleric->set_busy(0);

    // Raise skeleton distance guard.
    walker* blood_far = add_stain_to_fxlist(1, 250, 100);
    ASSERT_TRUE(blood_far != nullptr) << "far blood created";
    cleric->set_current_special(2);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(!og::test::do_special(*fd, cleric)) << "raise skeleton should fail when blood is out of range";
    if (blood_far)
        blood_far->set_dead(1);

    // Raise ghost distance guard.
    walker* blood_far2 = add_stain_to_fxlist(1, 180, 100);
    ASSERT_TRUE(blood_far2 != nullptr) << "second far blood created";
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(!og::test::do_special(*fd, cleric)) << "raise ghost should fail when blood is out of range";
    if (blood_far2)
        blood_far2->set_dead(1);

    // Resurrect path distance guard.
    walker* blood_far3 = add_stain_to_fxlist(0, 250, 100);
    ASSERT_TRUE(blood_far3 != nullptr) << "third far blood created";
    cleric->set_current_special(4);
    ASSERT_TRUE(!og::test::do_special(*fd, cleric)) << "resurrect should fail when blood is out of range";
}


// Every oracle here is a REFUSAL, and an erroring Lua hook also refuses
// (do_special returns nullopt, which og::test::do_special maps to false), so
// the hook-failure guard and the positive control are what make these
// negatives mean anything.
TEST(FamilyBehaviors, druid_special_busy_and_friend_count_guards)
{
    og::test::ScopedHookFailureGuard guard;
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "druid do_special present";

    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    ASSERT_TRUE(druid != nullptr) << "druid created";
    druid->stats()->set_level(5);

    druid->set_busy(1);
    druid->set_current_special(1);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "druid tree special should fail while busy";
    druid->set_current_special(2);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "druid summon special should fail while busy";
    druid->set_current_special(3);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "druid reveal special should fail while busy";
    druid->set_current_special(4);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "druid protection special should fail while busy";

    druid->set_busy(0);
    druid->set_current_special(4);
    ASSERT_TRUE(!og::test::do_special(*fd, druid)) << "druid protection should fail with no nearby allies";

    // Positive control: the same ladder, not busy, on a special with no
    // friend requirement — reveal_items grants level * 10 view_all.
    druid->set_current_special(3);
    druid->set_busy(0);
    const short view_before = druid->view_all();
    ASSERT_TRUE(og::test::do_special(*fd, druid)) << "reveal should succeed when not busy";
    ASSERT_EQ(view_before + 50, (int)druid->view_all())
        << "reveal grants exactly level * 10 view_all at level 5";

    ASSERT_EQ(0u, guard.count()) << guard.message();
}


TEST(FamilyBehaviors, family_round6_mage_thief_soldier_guard_branches)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* mage_fd = get_family_descriptor(FAMILY_MAGE);
    const auto* thief_fd = get_family_descriptor(FAMILY_THIEF);
    const auto* soldier_fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(mage_fd && thief_fd && soldier_fd) << "family descriptors present";

    // Mage AI check branches: <1 foes => true, 1-3 foes => false, >3 foes => true.
    walker* mage = add_living_to_level(FAMILY_MAGE, 1, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    mage->set_current_special(1);
    ASSERT_TRUE(og::test::check_special_ai(*mage_fd, static_cast<living*>(mage))) << "mage AI should allow special when no foes are in range";
    add_living_to_level(FAMILY_SOLDIER, 0, 120, 100);
    add_living_to_level(FAMILY_ORC, 0, 130, 100);
    ASSERT_TRUE(!og::test::check_special_ai(*mage_fd, static_cast<living*>(mage))) << "mage AI should reject special when 1-3 foes are in range";
    add_living_to_level(FAMILY_SOLDIER, 0, 140, 100);
    add_living_to_level(FAMILY_ORC, 0, 150, 100);
    ASSERT_TRUE(og::test::check_special_ai(*mage_fd, static_cast<living*>(mage))) << "mage AI should allow special when many foes are in range";

    // Mage teleport guards.
    mage->stats()->set_magicpoints(1000);
    mage->set_current_special(1);
    mage->set_ani_type(ANI_TELE_OUT);
    ASSERT_TRUE(!og::test::do_special(*mage_fd, mage)) << "mage teleport should fail while already teleporting";
    mage->set_ani_type(ANI_WALK);
    mage->set_shifter_down(1);
    mage->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*mage_fd, mage)) << "mage marker path should fail while busy";
    mage->set_busy(0);
    mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    if (mage->myguy)
        mage->myguy->intelligence = 50;
    mage->set_user(0);
    ASSERT_TRUE(!og::test::do_special(*mage_fd, mage)) << "mage marker path should fail for low-intelligence player characters";

    // Thief AI and do_special guards.
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    ASSERT_TRUE(thief != nullptr) << "thief created";
    thief->set_current_special(1);
    thief->set_foe(add_living_to_level(FAMILY_SOLDIER, 1, 200, 100));
    ASSERT_TRUE(thief->foe() != nullptr) << "thief foe created";
    if (thief->foe())
    {
        ASSERT_TRUE(!og::test::check_special_ai(*thief_fd, static_cast<living*>(thief))) << "thief bomb AI should reject when foe distance is in drop-bomb window";
    }
    thief->set_foe(nullptr);
    ASSERT_TRUE(!og::test::check_special_ai(*thief_fd, static_cast<living*>(thief))) << "thief bomb AI should reject when too few foes are nearby";
    thief->set_current_special(5);
    ASSERT_TRUE(og::test::check_special_ai(*thief_fd, static_cast<living*>(thief))) << "thief AI default branch should allow special";

    thief->set_current_special(3);
    thief->set_shifter_down(0);
    thief->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*thief_fd, thief)) << "thief taunt should fail while busy";
    thief->set_shifter_down(1);
    thief->set_busy(0);
    thief->setxy(300, 100); // Keep charm range clear of foes created above.
    ASSERT_TRUE(!og::test::do_special(*thief_fd, thief)) << "thief charm should fail when no targets are in range";
    thief->set_current_special(4);
    thief->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*thief_fd, thief)) << "thief poison cloud should fail while busy";

    // Soldier special guards.
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 0, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    soldier->stats()->set_magicpoints(1000);
    soldier->set_current_special(1);
    soldier->set_curdir(FACE_LEFT); // blocked by map edge
    ASSERT_TRUE(!og::test::do_special(*soldier_fd, soldier)) << "soldier charge should fail when forward is blocked";
    soldier->set_current_special(3);
    soldier->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*soldier_fd, soldier)) << "soldier whirlwind should fail while busy";
}


TEST(FamilyBehaviors, cleric_round6_heal_low_magic_and_undead_raise_no_target_guards)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* fd = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "cleric do_special present";

    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 108, 100);
    ASSERT_TRUE(cleric && ally) << "cleric and ally created";

    // Heal special with low MP should take the low-magic adjustment branch.
    cleric->set_current_special(1);
    cleric->set_shifter_down(0);
    cleric->stats()->set_level(12);
    cleric->stats()->set_magicpoints(1);
    ally->stats()->set_hitpoints(ally->stats()->max_hitpoints() - 20.0f);
    const float ally_hp_before = ally->stats()->hitpoints();
    // compute_heal_amount(1, 12) banks base = 1/4 + rand(0) = 0, so cost = 0
    // and heal_or_mace breaks out of its loop before touching anyone: nobody
    // is healed, so the cast refuses and charges nothing.
    EXPECT_FALSE(og::test::do_special(*fd, cleric))
        << "a heal whose computed cost is 0 heals nobody and refuses";
    EXPECT_FLOAT_EQ(ally_hp_before, ally->stats()->hitpoints())
        << "the refused heal must not move the ally's hitpoints";
    EXPECT_FLOAT_EQ(1.0f, cleric->stats()->magicpoints())
        << "the refused heal must not spend the cleric's last magic point";

    // Full-health ally path should produce didheal==0 and return false.
    cleric->stats()->set_magicpoints(200);
    ally->stats()->set_hitpoints(ally->stats()->max_hitpoints());
    ASSERT_TRUE(!og::test::do_special(*fd, cleric)) << "heal special should fail when nobody needs healing";

    // Raise/ghost specials with no blood target should fail via nearest-blood null branches.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric recreated";
    cleric->set_current_special(2);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(!og::test::do_special(*fd, cleric)) << "raise skeleton should fail with no blood target";
    cleric->set_current_special(3);
    cleric->set_shifter_down(0);
    ASSERT_TRUE(!og::test::do_special(*fd, cleric)) << "raise ghost should fail with no blood target";
}


TEST(FamilyBehaviors, druid_round6_protection_existing_circle_and_blocked_faerie_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(fd && og::test::has_do_special(*fd)) << "druid do_special present";

    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 112, 100);
    ASSERT_TRUE(druid && ally) << "druid and ally created";

    // Pre-existing protection circle on ally should hit refresh/merge branch.
    walker* existing = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(existing != nullptr) << "existing protection circle created";
    existing->set_owner(ally);
    existing->set_team_num(ally->team_num());
    // The refresh scan is og.find_in_range("weap", 100, friend) with an
    // owner filter: a circle parked at add_ob's default (0,0) is 200+ px away
    // and the cast silently mints a SECOND ring instead. Park it on its owner,
    // where circle_protection_on_animate keeps it every tick.
    existing->center_on(ally);
    existing->stats()->set_hitpoints(10.0f);

    druid->set_current_special(4);
    druid->set_busy(0);
    druid->stats()->set_magicpoints(300);
    ASSERT_TRUE(og::test::do_special(*fd, druid)) << "protection with existing circle should succeed";
    // packs/core/families/living-13-druid.lua protection_circle: the ally
    // already owns a circle, so a fresh one is minted only to pour its charge
    // into the existing ring and then dies unused — the ally never ends up
    // with two live circles.
    ASSERT_GT(existing->stats()->hitpoints(), 10.0f)
        << "the existing circle must be topped up by a fresh circle's charge";
    int live_circles_on_ally = 0;
    for (auto& uptr : og::runtime::current_session->myscreen_->world().weaplist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() && w->family() == FAMILY_CIRCLE_PROTECTION &&
            w->owner() == ally)
            ++live_circles_on_ally;
    }
    ASSERT_EQ(1, live_circles_on_ally)
        << "a recast must refresh the ally's ring, never mint a second one";

    // Blocked summon destination path for special 2: off-map is impassable, so
    // summon_faerie must refuse (same geometry family_batch4 relies on).
    druid->set_current_special(2);
    druid->set_busy(0);
    druid->stats()->set_magicpoints(300);
    druid->setxy(-200, -200);
    ASSERT_TRUE(!og::test::do_special(*fd, druid))
        << "summon faerie must fail when the spawn tile is impassable";
}


TEST(FamilyBehaviors, family_round8_mage_thief_soldier_callback_edge_paths)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* mage_fd = get_family_descriptor(FAMILY_MAGE);
    const auto* thief_fd = get_family_descriptor(FAMILY_THIEF);
    const auto* soldier_fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_TRUE(mage_fd && og::test::has_check_special_ai(*mage_fd) && og::test::has_do_special(*mage_fd)) << "mage callbacks exist";
    ASSERT_TRUE(thief_fd && og::test::has_check_special_ai(*thief_fd)) << "thief callback exists";
    ASSERT_TRUE(soldier_fd && og::test::has_on_fire_weapon(*soldier_fd)) << "soldier callback exists";

    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";

    // Mage AI returns false when 1-3 foes are in range.
    add_living_to_level(FAMILY_ORC, 1, 120, 100);
    add_living_to_level(FAMILY_ORC, 1, 130, 100);
    ASSERT_TRUE(!og::test::check_special_ai(*mage_fd, static_cast<living*>(mage))) << "mage special AI should be false with 1-3 nearby foes";

    // Teleport special hard guard while already in teleport animation.
    mage->set_current_special(1);
    mage->set_ani_type(ANI_TELE_OUT);
    ASSERT_TRUE(!og::test::do_special(*mage_fd, mage)) << "mage teleport special should fail while already teleporting";

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 100, 100);
    walker* foe = add_living_to_level(FAMILY_ORC, 1, 150, 100);
    ASSERT_TRUE(thief && foe) << "thief and foe created";
    if (thief && foe)
    {
        thief->set_current_special(1);
        thief->set_foe(foe);
        ASSERT_TRUE(!og::test::check_special_ai(*thief_fd, static_cast<living*>(thief))) << "thief bomb AI should reject medium-range foe distance";
    }

    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 100, 100);
    walker* weapon = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(soldier && weapon) << "soldier and weapon created";
    if (soldier && weapon)
    {
        living* lv = static_cast<living*>(soldier);
        const float mp_before = soldier->stats()->magicpoints();
        lv->set_weapons_left(0);
        ASSERT_TRUE(!og::test::on_fire_weapon(*soldier_fd, soldier, weapon)) << "soldier on_fire_weapon should fail and consume weapon when no weapons_left";
        ASSERT_TRUE(weapon->dead() == 1) << "soldier fallback should mark weapon dead";
        ASSERT_TRUE(soldier->stats()->magicpoints() >= mp_before) << "soldier fallback should refund weapon cost to magicpoints";

        weapon->set_dead(0);
        lv->set_weapons_left(2);
        ASSERT_TRUE(og::test::on_fire_weapon(*soldier_fd, soldier, weapon)) << "soldier on_fire_weapon should succeed when weapons_left > 0";
        ASSERT_EQ(1, (int)lv->weapons_left()) << "soldier on_fire_weapon should decrement weapons_left";
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

    // ORC special: full-hp eat-corpse guard should fail.
    walker* orc = add_living_to_level(FAMILY_ORC, 0, 100, 100);
    ASSERT_TRUE(orc != nullptr) << "orc created";
    orc->set_current_special(2);
    orc->stats()->set_hitpoints(orc->stats()->max_hitpoints());
    ASSERT_TRUE(!og::test::do_special(*orc_fd, orc)) << "orc eat-corpse special should fail at full hp";

    // GHOST AI: no foe in range should fail, nearby foe should pass.
    walker* ghost = add_living_to_level(FAMILY_GHOST, 0, 120, 100);
    ASSERT_TRUE(ghost != nullptr) << "ghost created";
    ghost->set_foe(nullptr);
    ASSERT_TRUE(!og::test::check_special_ai(*ghost_fd, static_cast<living*>(ghost))) << "ghost check_special_ai should fail without nearby foes";
    walker* ghost_foe = add_living_to_level(FAMILY_ORC, 1, 130, 100);
    ASSERT_TRUE(ghost_foe != nullptr) << "ghost foe created";
    if (ghost_foe)
    {
        ghost->set_foe(ghost_foe);
        ASSERT_TRUE(og::test::check_special_ai(*ghost_fd, static_cast<living*>(ghost))) << "ghost check_special_ai should pass with close foe";
    }

    // ARCHER hit_response: close-range foe should force a walk command away.
    walker* archer = add_living_to_level(FAMILY_ARCHER, 0, 100, 100);
    walker* archer_foe = add_living_to_level(FAMILY_ORC, 1, 110, 100);
    ASSERT_TRUE(archer && archer_foe) << "archer and foe created";
    if (archer && archer_foe)
    {
        archer->stats()->clear_command();
        og::test::hit_response(*archer_fd, archer->stats(), archer_foe);
        ASSERT_TRUE(archer->foe() == archer_foe) << "archer hit_response should assign foe";
        ASSERT_TRUE(archer->stats()->has_commands()) << "archer hit_response should enqueue retreat command at close range";
    }

    // SLIME AI: MAXOBS guard branch.
    const int saved_numobs = og::runtime::current_session->myscreen_->world().living_count;
    og::runtime::current_session->myscreen_->world().living_count = MAXOBS;
    ASSERT_TRUE(!og::test::check_special_ai(*slime_fd, static_cast<living*>(orc))) << "slime check_special_ai should fail when numobs reaches MAXOBS";
    og::runtime::current_session->myscreen_->world().living_count = 0;
    ASSERT_TRUE(og::test::check_special_ai(*slime_fd, static_cast<living*>(orc))) << "slime check_special_ai should pass when numobs is below MAXOBS";
    og::runtime::current_session->myscreen_->world().living_count = saved_numobs;

    // ELF special basic branch should execute for case 1 when fire is available.
    walker* elf = add_living_to_level(FAMILY_ELF, 0, 100, 100);
    ASSERT_TRUE(elf != nullptr) << "elf created";
    if (elf)
    {
        elf->set_current_special(1);
        elf->stats()->set_magicpoints(200);
        ASSERT_TRUE(og::test::do_special(*elf_fd, elf)) << "elf special case 1 should succeed in normal conditions";
    }
}


TEST(FamilyBehaviors, family_round11_mage_and_druid_targeted_special_clusters)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* mage_fd = get_family_descriptor(FAMILY_MAGE);
    const auto* druid_fd = get_family_descriptor(FAMILY_DRUID);
    ASSERT_TRUE(mage_fd && og::test::has_do_special(*mage_fd) && druid_fd && og::test::has_do_special(*druid_fd)) << "mage/druid callbacks present";

    // Mage marker guard: reject a low-intelligence caster.
    walker* mage = add_living_to_level(FAMILY_MAGE, 0, 100, 100);
    ASSERT_TRUE(mage != nullptr) << "mage created";
    mage->stats()->set_magicpoints(400);
    mage->set_current_special(1);
    mage->set_ani_type(ANI_WALK);
    mage->set_shifter_down(1);
    mage->set_busy(0);
    mage->set_user(0);
    mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    if (mage->myguy)
        mage->myguy->intelligence = 50;
    ASSERT_TRUE(!og::test::do_special(*mage_fd, mage)) << "mage marker should fail with int < 75";

    // Mage heartburst success path with nearby foes.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    mage = add_living_to_level(FAMILY_MAGE, 1, 100, 100);
    walker* foe1 = add_living_to_level(FAMILY_ORC, 0, 118, 100);
    walker* foe2 = add_living_to_level(FAMILY_ORC, 0, 100, 118);
    ASSERT_TRUE(mage && foe1 && foe2) << "mage and foes created";
    mage->set_current_special(5);
    mage->stats()->set_magicpoints(500);
    const float mp_before = mage->stats()->magicpoints();
    ASSERT_TRUE(og::test::do_special(*mage_fd, mage)) << "mage heartburst should succeed with nearby foes";
    ASSERT_TRUE(mage->busy() >= 5) << "heartburst should add busy delay";
    ASSERT_TRUE(mage->stats()->magicpoints() < mp_before) << "heartburst should consume magic";

    // Druid protection cast with an existing circle.
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 100, 100);
    walker* ally = add_living_to_level(FAMILY_SOLDIER, 0, 110, 100);
    ASSERT_TRUE(druid && ally) << "druid and ally created";
    walker* circle = og::runtime::current_session->myscreen_->world().add_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_TRUE(circle != nullptr) << "existing protection circle created";
    circle->set_owner(ally);
    circle->stats()->set_hitpoints(10.0f);
    druid->set_current_special(4);
    druid->set_busy(0);
    druid->stats()->set_magicpoints(500);
    ASSERT_TRUE(og::test::do_special(*druid_fd, druid)) << "druid protection should succeed with nearby ally";
}


TEST(FamilyBehaviors, family_round12_cleric_druid_soldier_thief_guard_and_ai_edges)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().create_new_grid();

    const auto* cleric_fd = get_family_descriptor(FAMILY_CLERIC);
    const auto* druid_fd = get_family_descriptor(FAMILY_DRUID);
    const auto* soldier_fd = get_family_descriptor(FAMILY_SOLDIER);
    const auto* thief_fd = get_family_descriptor(FAMILY_THIEF);
    ASSERT_TRUE(cleric_fd && og::test::has_check_special_ai(*cleric_fd) && og::test::has_do_special(*cleric_fd)) << "cleric callbacks exist";
    ASSERT_TRUE(druid_fd && og::test::has_do_special(*druid_fd)) << "druid callback exists";
    ASSERT_TRUE(soldier_fd && og::test::has_do_special(*soldier_fd) && og::test::has_check_special_ai(*soldier_fd)) << "soldier callbacks exist";
    ASSERT_TRUE(thief_fd && og::test::has_do_special(*thief_fd) && og::test::has_check_special_ai(*thief_fd)) << "thief callbacks exist";

    // Cleric AI special-1 low-friend/low-magic false and non-special-1 true.
    walker* cleric = add_living_to_level(FAMILY_CLERIC, 0, 100, 100);
    ASSERT_TRUE(cleric != nullptr) << "cleric created";
    cleric->set_current_special(1);
    cleric->stats()->set_magicpoints(0.0f);
    ASSERT_TRUE(!og::test::check_special_ai(*cleric_fd, static_cast<living*>(cleric))) << "cleric check_special_ai should fail for heal with no targets and low mp";
    cleric->set_current_special(3);
    ASSERT_TRUE(og::test::check_special_ai(*cleric_fd, static_cast<living*>(cleric))) << "cleric check_special_ai should default true for non-heal specials";

    // Cleric turn-undead guard branch: busy rejects immediately.
    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    cleric->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*cleric_fd, cleric)) << "cleric turn-undead should fail while busy";
    cleric->set_busy(0);

    // Druid busy and fire-fail guards.
    walker* druid = add_living_to_level(FAMILY_DRUID, 0, 120, 100);
    ASSERT_TRUE(druid != nullptr) << "druid created";
    druid->set_current_special(1);
    druid->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*druid_fd, druid)) << "druid tree special should fail when busy";
    druid->set_busy(0);
    druid->set_current_special(2);
    druid->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    ASSERT_TRUE(!og::test::do_special(*druid_fd, druid)) << "druid faerie summon should fail when fire() fails";
    druid->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    druid->set_current_special(4);
    cleric->set_team_num(1); // ensure there are no nearby same-team allies for this guard check
    // No nearby allies except self => howmany <= 1 => false.
    ASSERT_TRUE(!og::test::do_special(*druid_fd, druid)) << "druid protection should fail with no nearby allies";

    // Soldier special guards.
    walker* soldier = add_living_to_level(FAMILY_SOLDIER, 0, 140, 100);
    ASSERT_TRUE(soldier != nullptr) << "soldier created";
    soldier->set_current_special(1);
    soldier->set_lastx(1);
    soldier->set_lasty(0);
    soldier->set_curdir(FACE_RIGHT);
    walker* block_front = add_living_to_level(FAMILY_ORC, 1, static_cast<short>(soldier->xpos() + 1), soldier->ypos());
    ASSERT_TRUE(block_front != nullptr) << "soldier blocker created";
    ASSERT_TRUE(!og::test::do_special(*soldier_fd, soldier)) << "soldier charge should fail when forward is blocked";
    soldier->set_current_special(3);
    soldier->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*soldier_fd, soldier)) << "soldier whirlwind should fail when busy";
    soldier->set_current_special(4);
    ASSERT_TRUE(!og::test::do_special(*soldier_fd, soldier)) << "soldier disarm should fail when busy";
    soldier->set_busy(0);
    soldier->set_foe(add_living_to_level(FAMILY_ORC, 1, static_cast<short>(soldier->xpos() + 10), soldier->ypos()));
    ASSERT_TRUE(!og::test::check_special_ai(*soldier_fd, static_cast<living*>(soldier))) << "soldier special ai should fail for too-close foe distance";

    // Thief AI/special guards.
    walker* thief = add_living_to_level(FAMILY_THIEF, 0, 180, 100);
    walker* thief_foe = add_living_to_level(FAMILY_ORC, 1, 240, 100);
    ASSERT_TRUE(thief && thief_foe) << "thief fixtures created";
    thief->set_current_special(1);
    thief->set_foe(thief_foe);
    ASSERT_TRUE(!og::test::check_special_ai(*thief_fd, static_cast<living*>(thief))) << "thief bomb ai should reject medium-range foe distances";
    thief->set_current_special(3);
    thief->set_shifter_down(0);
    thief->set_busy(1);
    ASSERT_TRUE(!og::test::do_special(*thief_fd, thief)) << "thief taunt should fail when busy";
    thief->set_shifter_down(1);
    ASSERT_TRUE(!og::test::do_special(*thief_fd, thief)) << "thief charm should fail when busy";
    thief->set_current_special(4);
    ASSERT_TRUE(!og::test::do_special(*thief_fd, thief)) << "thief poison cloud should fail when busy";
}
