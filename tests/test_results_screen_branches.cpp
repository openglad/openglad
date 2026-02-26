#include <openglad/interface/ui/results_screen.h>
#include <openglad/platform/game_session.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <map>

// myscreen is now a macro defined in base.h (via game_session.h)

void test_results_screen_ending_branches_smoke()
{
    // Defeat generic.
    (void)results_screen(1, -1);
    // Defeat retreat.
    (void)results_screen(1, 2);

    // Victory, completed vs not completed.
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    // Make sure completion state is deterministic: clear and set once.
    og::runtime::current_session->myscreen_->save_data.current_levels.clear();
    (void)results_screen(0, 2); // not completed -> "Victory!"

    og::runtime::current_session->myscreen_->save_data.current_levels[og::runtime::current_session->myscreen_->save_data.current_campaign] = og::runtime::current_session->myscreen_->save_data.scen_num;
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
