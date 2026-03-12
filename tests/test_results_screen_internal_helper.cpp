#include <openglad/interface/ui/results_screen.h>
#include "test_framework.h"

TEST(ResultsScreenInternalHelper, exercises_core_paths)
{
    int score = results_screen_test_exercise_internal();
    ASSERT_TRUE(score > 0) << "internal helper should execute";
}


