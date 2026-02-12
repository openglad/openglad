#include "graph.h"
#include "entities/guy.h"
#include "test_framework.h"

extern screen* myscreen;

// ---------------------------------------------------------------------------
// viewscreen::resize(char whatmode) - multi-player modes
// Tests the big switch statement for 1/2/3/4 player resize configurations
// ---------------------------------------------------------------------------

void test_view_resize_1p_panels()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    myscreen->numviews = 1;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_PANELS);
    TEST_ASSERT(vs->xloc == 44, "1p panels xloc");
    TEST_ASSERT(vs->xview == 232, "1p panels xview");

    myscreen->numviews = old_numviews;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_1p_panels);

void test_view_resize_1p_view1()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    myscreen->numviews = 1;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_1);
    TEST_ASSERT(vs->xview == 192, "1p view1 xview");

    myscreen->numviews = old_numviews;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_1p_view1);

void test_view_resize_1p_view2()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    myscreen->numviews = 1;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_2);
    TEST_ASSERT(vs->xview == 148, "1p view2 xview");

    myscreen->numviews = old_numviews;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_1p_view2);

void test_view_resize_1p_view3()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    myscreen->numviews = 1;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_3);
    TEST_ASSERT(vs->xview == 108, "1p view3 xview");

    myscreen->numviews = old_numviews;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_1p_view3);

// --- 2-player mode ---

void test_view_resize_2p_player0_all()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    short old_mynum = vs->mynum;
    myscreen->numviews = 2;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    TEST_ASSERT(vs->xloc == 4, "2p p0 panels xloc");
    TEST_ASSERT(vs->xview == 152, "2p p0 panels xview");

    vs->resize(PREF_VIEW_1);
    TEST_ASSERT(vs->xview == 152, "2p p0 view1 xview");

    vs->resize(PREF_VIEW_2);
    TEST_ASSERT(vs->xview == 152, "2p p0 view2 xview");

    vs->resize(PREF_VIEW_3);
    TEST_ASSERT(vs->xview == 152, "2p p0 view3 xview");

    myscreen->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_2p_player0_all);

void test_view_resize_2p_player1_all()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    short old_mynum = vs->mynum;
    myscreen->numviews = 2;
    vs->mynum = 1;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    TEST_ASSERT(vs->xloc == 164, "2p p1 panels xloc");
    TEST_ASSERT(vs->xview == 152, "2p p1 panels xview");

    vs->resize(PREF_VIEW_1);
    vs->resize(PREF_VIEW_2);
    vs->resize(PREF_VIEW_3);

    myscreen->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_2p_player1_all);

// --- 3-player mode ---

void test_view_resize_3p_player0_all()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    short old_mynum = vs->mynum;
    myscreen->numviews = 3;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    TEST_ASSERT(vs->xloc == 4, "3p p0 panels xloc");
    TEST_ASSERT(vs->xview == 100, "3p p0 panels xview");

    vs->resize(PREF_VIEW_1);
    vs->resize(PREF_VIEW_2);
    vs->resize(PREF_VIEW_3);

    myscreen->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_3p_player0_all);

void test_view_resize_3p_player1_all()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    short old_mynum = vs->mynum;
    myscreen->numviews = 3;
    vs->mynum = 1;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    TEST_ASSERT(vs->xloc == 216, "3p p1 panels xloc");

    vs->resize(PREF_VIEW_1);
    vs->resize(PREF_VIEW_2);
    vs->resize(PREF_VIEW_3);

    myscreen->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_3p_player1_all);

void test_view_resize_3p_player2_all()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    short old_mynum = vs->mynum;
    myscreen->numviews = 3;
    vs->mynum = 2;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    TEST_ASSERT(vs->xloc == 112, "3p p2 panels xloc");

    vs->resize(PREF_VIEW_1);
    vs->resize(PREF_VIEW_2);
    vs->resize(PREF_VIEW_3);

    myscreen->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_3p_player2_all);

// --- 4-player mode ---

void test_view_resize_4p_all_players()
{
    viewscreen* vs = myscreen->viewob[0].get();
    if (!vs) return;
    short old_numviews = myscreen->numviews;
    short old_mynum = vs->mynum;
    myscreen->numviews = 4;

    for (int p = 0; p < 4; p++) {
        vs->mynum = static_cast<short>(p);
        vs->resize(PREF_VIEW_FULL);
    }

    myscreen->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}
REGISTER_TEST(test_view_resize_4p_all_players);
