#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/ctf/ctf_state.h>

// CTF AI director coverage lands with the dedicated AI phase. This anchors
// the binary so the director's cadence constants stay visible from day one.
TEST(CtfAi, director_cadence_constant)
{
    ASSERT_EQ(15, og::sim::kCtfAiCadenceTicks);
    ASSERT_EQ(15, COMMAND_GOTO);
}
