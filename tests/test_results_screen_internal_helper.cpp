#include "ui/results_screen.h"
#include "test_framework.h"

void test_results_screen_internal_helper_exercises_core_paths()
{
    int score = results_screen_test_exercise_internal();
    TEST_ASSERT(score > 0, "internal helper should execute");
}
REGISTER_TEST(test_results_screen_internal_helper_exercises_core_paths);

