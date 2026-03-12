#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <cstring>

// myscreen is now a macro defined in base.h (via game_session.h)

TEST(TextRendering, text_write_variants_cover_common_paths)
{
    text& t = og::runtime::current_session->myscreen_->text_normal;
    text& big = og::runtime::current_session->myscreen_->text_big;

    ASSERT_TRUE(t.query_width("ABC") > 0) << "query_width should return >0 for non-empty text";

    // Exercise common formatting / positioning paths.
    t.write_xy(10, 10, "hello", 250, 1);
    t.write_xy_center(160, 30, 240, "center %d", 1);
    t.write_xy_shadow(10, 50, 200, "shadow %s", "x");
    t.write_xy_center_shadow(160, 70, 200, "center shadow");
    t.write_char_xy_alpha(10, 90, 'A', 220, 128);

    // viewscreen-targeted writes
    viewscreen* v = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(v != nullptr) << "viewob[0] should exist";
    t.write_xy(10, 110, "to view", 230, v);

    // Big font path
    big.write_xy(10, 130, "BIG", 230, 1);
}

