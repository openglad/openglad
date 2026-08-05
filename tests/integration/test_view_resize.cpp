#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

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


TEST_F(ViewResize, 2p_player1_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 2;
    vs->mynum = 1;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    ASSERT_TRUE(vs->xloc == 164) << "2p p1 panels xloc";
    ASSERT_TRUE(vs->xview == 152) << "2p p1 panels xview";

    vs->resize(PREF_VIEW_1);
    vs->resize(PREF_VIEW_2);
    vs->resize(PREF_VIEW_3);

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


// --- 3-player mode ---

TEST_F(ViewResize, 3p_player0_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 3;
    vs->mynum = 0;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    ASSERT_TRUE(vs->xloc == 4) << "3p p0 panels xloc";
    ASSERT_TRUE(vs->xview == 100) << "3p p0 panels xview";

    vs->resize(PREF_VIEW_1);
    vs->resize(PREF_VIEW_2);
    vs->resize(PREF_VIEW_3);

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


TEST_F(ViewResize, 3p_player1_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 3;
    vs->mynum = 1;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    ASSERT_TRUE(vs->xloc == 216) << "3p p1 panels xloc";

    vs->resize(PREF_VIEW_1);
    vs->resize(PREF_VIEW_2);
    vs->resize(PREF_VIEW_3);

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


TEST_F(ViewResize, 3p_player2_all)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 3;
    vs->mynum = 2;

    vs->resize(PREF_VIEW_FULL);
    vs->resize(PREF_VIEW_PANELS);
    ASSERT_TRUE(vs->xloc == 112) << "3p p2 panels xloc";

    vs->resize(PREF_VIEW_1);
    vs->resize(PREF_VIEW_2);
    vs->resize(PREF_VIEW_3);

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}


// --- 4-player mode ---

TEST_F(ViewResize, 4p_all_players)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    short old_numviews = og::runtime::current_session->myscreen_->numviews;
    short old_mynum = vs->mynum;
    og::runtime::current_session->myscreen_->numviews = 4;

    for (int p = 0; p < 4; p++) {
        vs->mynum = static_cast<short>(p);
        vs->resize(PREF_VIEW_FULL);
    }

    og::runtime::current_session->myscreen_->numviews = old_numviews;
    vs->mynum = old_mynum;
    vs->resize(PREF_VIEW_FULL);
}
