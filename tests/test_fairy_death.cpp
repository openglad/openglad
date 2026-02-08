#include "graph.h"
#include "guy.h"
#include "test_trace.h"
#include "test_framework.h"
#include "save_data.h"
#include "util.h"

extern screen* myscreen;

// Forward declarations
void glad_main(Sint32 playermode);
short remaining_team(screen *myscreen, char myteam);

// Test: hire a lone fairy, start level 1, stand there, and confirm we lose.
// Runs at max game speed so the fairy dies as fast as the CPU can manage.
void test_fairy_death() {
    trace_clear();

    // Save the original speed factor so we can restore it
    float original_speed = g_game_speed_factor;

    // Reset save data and set up a single fairy on the team
    myscreen->save_data.reset();
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";

    guy* fairy = new guy(FAMILY_FAERIE);
    strcpy(fairy->name, "TESTFAIRY");
    fairy->teamnum = 0;
    myscreen->save_data.team_list[0] = fairy;
    myscreen->save_data.team_size = 1;
    myscreen->save_data.save("save0");

    // Prepare the screen for battle
    myscreen->ready_for_battle(1);

    // Max speed — no frame delays
    set_game_speed(0.0f);

    // Run the game loop. glad_main blocks until the level ends.
    // The fairy should die to the enemies on level 1 since we never give input.
    glad_main(1);

    // Restore speed
    set_game_speed(original_speed);

    // The game ended — verify it's because our team is dead
    TEST_ASSERT(myscreen->end != 0, "game should have ended");
    TEST_ASSERT_EQ(0, remaining_team(myscreen, 0), "fairy should be dead (no team members left)");

    fprintf(stderr, "  [test] Fairy died after %d frames\n", (int)myscreen->framecount);
}
REGISTER_TEST(test_fairy_death);
