#include <openglad/legacy/graph.h>
#include "test_framework.h"

#include <cstring>

extern screen* myscreen;

void test_text_write_variants_cover_common_paths()
{
    text& t = myscreen->text_normal;
    text& big = myscreen->text_big;

    TEST_ASSERT(t.query_width("ABC") > 0, "query_width should return >0 for non-empty text");

    // Exercise common formatting / positioning paths.
    t.write_xy(10, 10, "hello", 250, 1);
    t.write_xy_center(160, 30, 240, "center %d", 1);
    t.write_xy_shadow(10, 50, 200, "shadow %s", "x");
    t.write_xy_center_shadow(160, 70, 200, "center shadow");
    t.write_char_xy_alpha(10, 90, 'A', 220, 128);

    // viewscreen-targeted writes
    viewscreen* v = myscreen->viewob[0].get();
    TEST_ASSERT(v != nullptr, "viewob[0] should exist");
    t.write_xy(10, 110, "to view", 230, v);

    // Big font path
    big.write_xy(10, 130, "BIG", 230, 1);
}
REGISTER_TEST(test_text_write_variants_cover_common_paths);
