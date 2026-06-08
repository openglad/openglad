#include <openglad/interface/ui/results_screen.h>
#include <gtest/gtest.h>

TEST(ResultsScreenInternalHelper, exercises_core_paths)
{
    constexpr int kExpectedInternalHelperChecks = 14;
    ASSERT_EQ(kExpectedInternalHelperChecks,
              results_screen_test_exercise_internal())
        << "internal helper should run every check";
}
