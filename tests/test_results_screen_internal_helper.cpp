#include <openglad/interface/ui/results_screen.h>
#include <gtest/gtest.h>

TEST(ResultsScreenInternalHelper, exercises_core_paths)
{
    ASSERT_EQ(0, results_screen_test_exercise_internal())
        << "internal helper should report the first failed check as a negative index";
}
