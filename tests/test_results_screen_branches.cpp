#include "graph.h"
#include "ui/results_screen.h"
#include "test_framework.h"

#include <map>

extern screen* myscreen;

void test_results_screen_ending_branches_smoke()
{
    // Defeat generic.
    (void)results_screen(1, -1);
    // Defeat retreat.
    (void)results_screen(1, 2);

    // Victory, completed vs not completed.
    myscreen->save_data.scen_num = 1;
    // Make sure completion state is deterministic: clear and set once.
    myscreen->save_data.current_levels.clear();
    (void)results_screen(0, 2); // not completed -> "Victory!"

    myscreen->save_data.current_levels[myscreen->save_data.current_campaign] = myscreen->save_data.scen_num;
    (void)results_screen(0, 2); // completed -> "Traveling on..."

    // Special defeat type.
    (void)results_screen(SCEN_TYPE_SAVE_ALL, -1);
}
REGISTER_TEST(test_results_screen_ending_branches_smoke);

void test_results_screen_overload_calls_smoke()
{
    std::map<int, guy*> before;
    std::map<int, walker*> after;
    (void)results_screen(0, 2, before, after);
}
REGISTER_TEST(test_results_screen_overload_calls_smoke);
