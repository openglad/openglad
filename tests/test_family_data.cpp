#include <openglad/legacy/test_trace.h>
#include "test_framework.h"
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/entities/walker.h>
#include <openglad/core/stats.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>
#include <cstring>

extern screen* myscreen;
extern Sint32 costlist[NUM_FAMILIES];
extern Sint32 statlist[NUM_FAMILIES][6];
extern Sint32 statcosts[NUM_FAMILIES][6];
extern float derived_bonuses[NUM_FAMILIES][8];
const char* get_family_string(Sint32 family);

// Verify the registry base_stats match the existing statlist[] array
void test_family_data_base_stats_match_statlist()
{
    init_family_registry();
    // statlist only covers 0..FAMILY_ARCHMAGE (18 entries)
    for (int fam = 0; fam <= FAMILY_ARCHMAGE; fam++)
    {
        auto* d = get_family_descriptor(fam);
        TEST_ASSERT(d != nullptr, "descriptor should exist");
        for (int s = 0; s < 6; s++)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "family %d stat %d mismatch", fam, s);
            TEST_ASSERT_EQ(statlist[fam][s], d->base_stats[s], msg);
        }
    }
}
REGISTER_TEST(test_family_data_base_stats_match_statlist);

// Verify the registry hiring_cost matches costlist[]
void test_family_data_hiring_cost_match_costlist()
{
    init_family_registry();
    // costlist covers 0..FAMILY_ARCHMAGE (18 entries)
    for (int fam = 0; fam <= FAMILY_ARCHMAGE; fam++)
    {
        auto* d = get_family_descriptor(fam);
        char msg[128];
        std::snprintf(msg, sizeof(msg), "family %d cost mismatch", fam);
        TEST_ASSERT_EQ(costlist[fam], d->hiring_cost, msg);
    }
}
REGISTER_TEST(test_family_data_hiring_cost_match_costlist);

// Verify registry stat_costs match statcosts[]
void test_family_data_stat_costs_match_statcosts()
{
    init_family_registry();
    for (int fam = 0; fam <= FAMILY_ARCHMAGE; fam++)
    {
        auto* d = get_family_descriptor(fam);
        for (int s = 0; s < 6; s++)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "family %d statcost %d mismatch", fam, s);
            TEST_ASSERT_EQ(statcosts[fam][s], d->stat_costs[s], msg);
        }
    }
}
REGISTER_TEST(test_family_data_stat_costs_match_statcosts);

// Verify registry derived_bonuses match the global derived_bonuses[] array
void test_family_data_derived_bonuses_match()
{
    init_family_registry();
    for (int fam = 0; fam < NUM_FAMILIES; fam++)
    {
        auto* d = get_family_descriptor(fam);
        for (int b = 0; b < 8; b++)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "family %d bonus %d mismatch", fam, b);
            float diff = d->derived_bonuses[b] - derived_bonuses[fam][b];
            if (diff < 0) diff = -diff;
            TEST_ASSERT(diff < 0.01f, msg);
        }
    }
}
REGISTER_TEST(test_family_data_derived_bonuses_match);

// Verify guy constructor produces same stats as registry base_stats
void test_family_data_guy_constructor_matches_registry()
{
    init_family_registry();
    for (int fam = 0; fam <= FAMILY_ARCHMAGE; fam++)
    {
        auto* d = get_family_descriptor(fam);
        guy g(fam);
        char msg[128];

        std::snprintf(msg, sizeof(msg), "family %d STR mismatch", fam);
        TEST_ASSERT_EQ(d->base_stats[0], static_cast<Sint32>(g.strength), msg);
        std::snprintf(msg, sizeof(msg), "family %d DEX mismatch", fam);
        TEST_ASSERT_EQ(d->base_stats[1], static_cast<Sint32>(g.dexterity), msg);
        std::snprintf(msg, sizeof(msg), "family %d CON mismatch", fam);
        TEST_ASSERT_EQ(d->base_stats[2], static_cast<Sint32>(g.constitution), msg);
        std::snprintf(msg, sizeof(msg), "family %d INT mismatch", fam);
        TEST_ASSERT_EQ(d->base_stats[3], static_cast<Sint32>(g.intelligence), msg);
        std::snprintf(msg, sizeof(msg), "family %d ARMOR mismatch", fam);
        TEST_ASSERT_EQ(d->base_stats[4], static_cast<Sint32>(g.armor), msg);
        std::snprintf(msg, sizeof(msg), "family %d LVL mismatch", fam);
        TEST_ASSERT_EQ(d->base_stats[5], static_cast<Sint32>(g.level), msg);
    }
}
REGISTER_TEST(test_family_data_guy_constructor_matches_registry);

// Verify family names from get_family_string match registry names
void test_family_data_names_match_get_family_string()
{
    init_family_registry();
    // These families have explicit names in get_family_string
    struct { int fam; const char* expected; } checks[] = {
        {FAMILY_SOLDIER, "SOLDIER"},
        {FAMILY_ELF, "ELF"},
        {FAMILY_ARCHER, "ARCHER"},
        {FAMILY_MAGE, "MAGE"},
        {FAMILY_SKELETON, "SKELETON"},
        {FAMILY_CLERIC, "CLERIC"},
        {FAMILY_FIREELEMENTAL, "ELEMENTAL"},
        {FAMILY_FAERIE, "FAERIE"},
        {FAMILY_SLIME, "SLIME"},
        {FAMILY_SMALL_SLIME, "SLIME"},
        {FAMILY_MEDIUM_SLIME, "SLIME"},
        {FAMILY_THIEF, "THIEF"},
        {FAMILY_GHOST, "GHOST"},
        {FAMILY_DRUID, "DRUID"},
        {FAMILY_ORC, "ORC"},
        {FAMILY_BIG_ORC, "ORC CAPTAIN"},
        {FAMILY_BARBARIAN, "BARBARIAN"},
        {FAMILY_ARCHMAGE, "ARCHMAGE"},
    };
    for (auto& check : checks)
    {
        auto* d = get_family_descriptor(check.fam);
        const char* gfs = get_family_string(check.fam);
        char msg[128];
        std::snprintf(msg, sizeof(msg), "family %d name: registry='%s' vs get_family_string='%s'",
                      check.fam, d->name, gfs);
        TEST_ASSERT_STR_EQ(check.expected, d->name, msg);
        TEST_ASSERT_STR_EQ(check.expected, gfs, msg);
    }
}
REGISTER_TEST(test_family_data_names_match_get_family_string);

// Verify walker init matches registry for special_cost, weapon_cost, default_weapon
static void teardown_family_walker() {
    if (myscreen) myscreen->level_data.delete_objects();
}

void test_family_data_walker_init_matches_registry()
{
    init_family_registry();
    for (int fam = 0; fam < NUM_FAMILIES; fam++)
    {
        auto* d = get_family_descriptor(fam);
        guy g(fam);
        g.teamnum = 0;
        auto w = guy_create_walker_owned(g, myscreen);
        if (!w) continue;

        char msg[128];

        std::snprintf(msg, sizeof(msg), "family %d default_weapon mismatch", fam);
        TEST_ASSERT_EQ(d->default_weapon, static_cast<int>(w->default_weapon), msg);

        for (int s = 0; s < NUM_SPECIALS; s++)
        {
            std::snprintf(msg, sizeof(msg), "family %d special_cost[%d] mismatch", fam, s);
            TEST_ASSERT_EQ(d->special_cost[s], w->stats()->special_cost[s], msg);
        }

        std::snprintf(msg, sizeof(msg), "family %d weapon_cost mismatch", fam);
        TEST_ASSERT_EQ(d->weapon_cost, w->stats()->weapon_cost, msg);
    }
    if (myscreen) myscreen->level_data.delete_objects();
}
REGISTER_TEST_WITH_FIXTURE(test_family_data_walker_init_matches_registry, nullptr, teardown_family_walker);

// Verify special_name strings on screen match registry
void test_family_data_special_names_match_screen()
{
    init_family_registry();
    if (!myscreen) return;

    for (int fam = 0; fam < NUM_FAMILIES; fam++)
    {
        auto* d = get_family_descriptor(fam);
        for (int s = 0; s < NUM_SPECIALS; s++)
        {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "family %d special_name[%d] mismatch: '%s' vs '%s'",
                          fam, s, d->special_names[s], myscreen->special_name[fam][s].c_str());
            TEST_ASSERT_STR_EQ(d->special_names[s], myscreen->special_name[fam][s].c_str(), msg);

            std::snprintf(msg, sizeof(msg), "family %d alternate_name[%d] mismatch: '%s' vs '%s'",
                          fam, s, d->alternate_names[s], myscreen->alternate_name[fam][s].c_str());
            TEST_ASSERT_STR_EQ(d->alternate_names[s], myscreen->alternate_name[fam][s].c_str(), msg);
        }
    }
}
REGISTER_TEST(test_family_data_special_names_match_screen);

// Verify bit flags match for all families that set them during walker init
void test_family_data_bit_flags_match_walker_init()
{
    init_family_registry();

    struct FlagCheck { int fam; Sint32 flag; bool expected; };
    FlagCheck checks[] = {
        {FAMILY_ELF, BIT_FORESTWALK, true},
        {FAMILY_FAERIE, BIT_ANIMATE, true},
        {FAMILY_FAERIE, BIT_FLYING, true},
        {FAMILY_FIREELEMENTAL, BIT_ANIMATE, true},
        {FAMILY_GHOST, BIT_ANIMATE, true},
        {FAMILY_GHOST, BIT_FLYING, true},
        {FAMILY_GHOST, BIT_ETHEREAL, true},
        {FAMILY_GHOST, BIT_NO_RANGED, true},
        {FAMILY_SLIME, BIT_ANIMATE, true},
        {FAMILY_SMALL_SLIME, BIT_ANIMATE, true},
        {FAMILY_SMALL_SLIME, BIT_NO_RANGED, true},
        {FAMILY_MEDIUM_SLIME, BIT_ANIMATE, true},
        {FAMILY_ORC, BIT_NO_RANGED, true},
        {FAMILY_SOLDIER, BIT_FLYING, false},
        {FAMILY_SOLDIER, BIT_ANIMATE, false},
    };

    for (auto& c : checks)
    {
        auto* d = get_family_descriptor(c.fam);
        bool has_flag = (d->init_bit_flags & c.flag) != 0;
        char msg[128];
        std::snprintf(msg, sizeof(msg), "family %d flag 0x%x expected %s", c.fam, c.flag, c.expected ? "set" : "clear");
        TEST_ASSERT(has_flag == c.expected, msg);
    }
}
REGISTER_TEST(test_family_data_bit_flags_match_walker_init);
