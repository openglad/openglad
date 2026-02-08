#include "graph.h"
#include "guy.h"
#include "test_trace.h"
#include "test_framework.h"
#include "save_data.h"
#include "util.h"

extern screen* myscreen;

// Forward declarations
void glad_main(Sint32 playermode);

#ifdef TESTING
extern bool g_test_remove_exits;
#endif

// Hire one of each character type (budget permitting), crank stats to absurd
// levels, run level 1 at max speed, and confirm that we win.
void test_overpowered_team() {
    trace_clear();

    float original_speed = g_game_speed_factor;

    // All hireable families, cheapest first so we can fit more on the team
    struct { int family; int cost; const char* name; } recruits[] = {
        { FAMILY_ELF,            150, "MEGAELF" },
        { FAMILY_SOLDIER,        250, "MEGASOLD" },
        { FAMILY_ORC,            300, "MEGAORC" },
        { FAMILY_SKELETON,       300, "MEGASKEL" },
        { FAMILY_BARBARIAN,      350, "MEGABARB" },
        { FAMILY_ARCHER,         350, "MEGAARCH" },
        { FAMILY_DRUID,          350, "MEGADRU" },
        { FAMILY_THIEF,          400, "MEGATHF" },
        { FAMILY_CLERIC,         400, "MEGACLR" },
        { FAMILY_FAERIE,         450, "MEGAFAE" },
        { FAMILY_MAGE,           450, "MEGAMAGE" },
        { FAMILY_FIREELEMENTAL,  600, "MEGAFIRE" },
        { FAMILY_GHOST,          600, "MEGAGHOST" },
        { FAMILY_SMALL_SLIME,    700, "MEGASLIM" },
    };
    int num_recruits = sizeof(recruits) / sizeof(recruits[0]);

    // Reset save data — fresh start
    myscreen->save_data.reset();
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";

    // Hire one of each, spending from the 5000 starting gold
    int gold = myscreen->save_data.m_totalcash[0];
    int hired = 0;
    for (int i = 0; i < num_recruits && hired < MAX_TEAM_SIZE; i++) {
        if (gold < recruits[i].cost)
            continue;

        guy* g = new guy(recruits[i].family);
        strncpy(g->name, recruits[i].name, 11);
        g->name[11] = '\0';
        g->teamnum = 0;

        // Crank every stat to ludicrous levels
        g->strength = 200;
        g->dexterity = 200;
        g->constitution = 200;
        g->intelligence = 200;
        g->armor = 200;

        myscreen->save_data.team_list[hired] = g;
        gold -= recruits[i].cost;
        hired++;
    }
    myscreen->save_data.team_size = hired;
    TEST_ASSERT(hired >= 5, "should have hired at least 5 characters");

    myscreen->save_data.save("save0");
    myscreen->ready_for_battle(1);

    // Remove exits so level auto-completes when all enemies die
    g_test_remove_exits = true;
    set_game_speed(0.0f);

    glad_main(1);

    // Restore state
    set_game_speed(original_speed);
    g_test_remove_exits = false;

    // endgame(0) marks the level completed and saves
    TEST_ASSERT(myscreen->end != 0, "game should have ended");
    TEST_ASSERT(myscreen->save_data.is_level_completed(1),
                "level 1 should be marked completed (team should have won)");

    fprintf(stderr, "  [test] Team of %d won after %d frames\n",
            hired, (int)myscreen->framecount);
}
REGISTER_TEST(test_overpowered_team);
