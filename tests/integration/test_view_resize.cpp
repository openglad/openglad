#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// viewscreen::resize(char whatmode) - multi-player modes
// Tests the big switch statement for 1/2/3/4 player resize configurations
// ---------------------------------------------------------------------------

class ViewResize : public testing::Test
{
protected:
    screen* game_ = nullptr;
    viewscreen* view_ = nullptr;
    short saved_numviews_ = 0;
    short saved_mynum_ = 0;

    void SetUp() override
    {
        game_ = og::runtime::current_session->myscreen_;
        view_ = game_->viewob[0].get();
        ASSERT_NE(nullptr, view_);
        saved_numviews_ = game_->numviews;
        saved_mynum_ = view_->mynum;
        game_->set_world_canvas_pinned_classic(true);
        game_->relayout_views();
    }

    void TearDown() override
    {
        game_->numviews = saved_numviews_;
        view_->mynum = saved_mynum_;
        game_->set_world_canvas_pinned_classic(false);
        game_->relayout_views();
    }
};

// resize(whatmode) is only done when all four edges landed: xloc/yloc and the
// width/height it projects, plus the endx/endy it derives (view.cpp
// resize(x,y,w,h)). A pane checked on x alone hides every y-axis regression,
// which is exactly what the split/column layouts encode.
static void expect_pane(viewscreen* vs, signed char mode, int x, int y, int w,
                        int h,
                        const std::string& what)
{
    vs->resize(mode);
    EXPECT_EQ(x, (int)vs->xloc) << what << " xloc";
    EXPECT_EQ(y, (int)vs->yloc) << what << " yloc";
    EXPECT_EQ(w, (int)vs->xview) << what << " xview";
    EXPECT_EQ(h, (int)vs->yview) << what << " yview";
    EXPECT_EQ(x + w, (int)vs->endx) << what << " endx";
    EXPECT_EQ(y + h, (int)vs->endy) << what << " endy";
}

TEST_F(ViewResize, 1p_panels)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_PANELS);
    ASSERT_TRUE(vs->xloc == 44) << "1p panels xloc";
    ASSERT_TRUE(vs->xview == 232) << "1p panels xview";

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->resize(PREF_VIEW_FULL);
}


TEST_F(ViewResize, 1p_view1)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_1);
    ASSERT_TRUE(vs->xview == 192) << "1p view1 xview";

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->resize(PREF_VIEW_FULL);
}


TEST_F(ViewResize, 1p_view2)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_2);
    ASSERT_TRUE(vs->xview == 148) << "1p view2 xview";

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->resize(PREF_VIEW_FULL);
}


TEST_F(ViewResize, 1p_view3)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    og::runtime::current_session->myscreen_->numviews = 1;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_3);
    ASSERT_TRUE(vs->xview == 108) << "1p view3 xview";

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->resize(PREF_VIEW_FULL);
}


// --- 2-player mode ---

TEST_F(ViewResize, 2p_player0_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 2;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    ASSERT_TRUE(vs->xloc == 4) << "2p p0 panels xloc";
    ASSERT_TRUE(vs->xview == 152) << "2p p0 panels xview";

    vs->resize(PREF_VIEW_1);
    ASSERT_TRUE(vs->xview == 152) << "2p p0 view1 xview";

    vs->resize(PREF_VIEW_2);
    ASSERT_TRUE(vs->xview == 152) << "2p p0 view2 xview";

    vs->resize(PREF_VIEW_3);
    ASSERT_TRUE(vs->xview == 152) << "2p p0 view3 xview";

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


// 2p inset modes, right pane: x = right_x+3 = 164 and w = half_w-7 = 152 are
// mode-invariant, so the Y projection is the only thing that separates the
// four modes — y = 16*mode, h = 200-2*y (view_layout.h split_vertical_inset).
// Every mode is pinned on all four values.
TEST_F(ViewResize, 2p_player1_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 should exist";
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 2;
    vs->mynum = 1;

    vs->resize(PREF_VIEW_FULL);
    expect_pane(vs, PREF_VIEW_PANELS, 164, 16, 152, 168, "2p p1 PANELS");
    expect_pane(vs, PREF_VIEW_1, 164, 32, 152, 136, "2p p1 VIEW_1");
    expect_pane(vs, PREF_VIEW_2, 164, 48, 152, 104, "2p p1 VIEW_2");
    expect_pane(vs, PREF_VIEW_3, 164, 64, 152, 72, "2p p1 VIEW_3");

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


// --- 3-player mode ---

// 3p inset modes: three fixed-margin columns of width (320-20)/3 = 100.
// mynum 0 is the LEFT column at x=4; y/h come from the same
// split_vertical_inset ladder as 2p (view_layout.h).
TEST_F(ViewResize, 3p_player0_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 should exist";
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 3;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_FULL);
    expect_pane(vs, PREF_VIEW_PANELS, 4, 16, 100, 168, "3p p0 PANELS");
    expect_pane(vs, PREF_VIEW_1, 4, 32, 100, 136, "3p p0 VIEW_1");
    expect_pane(vs, PREF_VIEW_2, 4, 48, 100, 104, "3p p0 VIEW_2");
    expect_pane(vs, PREF_VIEW_3, 4, 64, 100, 72, "3p p0 VIEW_3");

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


// 3p mynum 1 is the RIGHT column: x = 4 + 2*100 + 12 = 216 (view_layout.h
// col_x). Width and the whole y ladder are pinned too.
TEST_F(ViewResize, 3p_player1_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 should exist";
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 3;
    vs->mynum = 1;

    vs->resize(PREF_VIEW_FULL);
    expect_pane(vs, PREF_VIEW_PANELS, 216, 16, 100, 168, "3p p1 PANELS");
    expect_pane(vs, PREF_VIEW_1, 216, 32, 100, 136, "3p p1 VIEW_1");
    expect_pane(vs, PREF_VIEW_2, 216, 48, 100, 104, "3p p1 VIEW_2");
    expect_pane(vs, PREF_VIEW_3, 216, 64, 100, 72, "3p p1 VIEW_3");

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


// 3p mynum 2 is the MIDDLE column: x = 4 + 100 + 8 = 112 (view_layout.h
// col_x). Width and the whole y ladder are pinned too.
TEST_F(ViewResize, 3p_player2_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 should exist";
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 3;
    vs->mynum = 2;

    vs->resize(PREF_VIEW_FULL);
    expect_pane(vs, PREF_VIEW_PANELS, 112, 16, 100, 168, "3p p2 PANELS");
    expect_pane(vs, PREF_VIEW_1, 112, 32, 100, 136, "3p p2 VIEW_1");
    expect_pane(vs, PREF_VIEW_2, 112, 48, 100, 104, "3p p2 VIEW_2");
    expect_pane(vs, PREF_VIEW_3, 112, 64, 100, 72, "3p p2 VIEW_3");

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


// --- 4-player mode ---

// 4p is quadrants in EVERY mode (compute_view_layout's default arm). At
// 320x200: half_w=159, half_h=99, right_x=161, bottom_y=101, so the seats sit
// at (0,0) (161,0) (0,101) (161,101), each 159x99. Pinned per seat AND per
// mode, because the default arm must ignore the mode entirely.
TEST_F(ViewResize, 4p_all_players)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 should exist";
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 4;

    const signed char modes[5] = {PREF_VIEW_FULL, PREF_VIEW_PANELS, PREF_VIEW_1,
                           PREF_VIEW_2, PREF_VIEW_3};
    for (int p = 0; p < 4; p++) {
        vs->mynum = static_cast<short>(p);
        const int want_x = (p == 1 || p == 3) ? 161 : 0;
        const int want_y = (p >= 2) ? 101 : 0;
        for (int m = 0; m < 5; ++m) {
            const std::string what =
                "4p seat " + std::to_string(p) + " mode " + std::to_string(m);
            expect_pane(vs, modes[m], want_x, want_y, 159, 99, what);
        }
    }

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}
