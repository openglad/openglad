#include <gtest/gtest.h>

#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/ctf/ctf_state.h>

// Curses-client CTF coverage (glyphs, HUD line, networked round) lands with
// the UI phase. This anchors the suite entry on the SDL-free binary.
TEST(CursesCtf, ctf_constants_visible_without_sdl)
{
    ASSERT_EQ(13, og::FAMILY_FLAG);
    ASSERT_EQ(14, og::FAMILY_CTF_POINT);
    ASSERT_EQ(3, og::sim::kCtfDefaultCaptureLimit);
}
