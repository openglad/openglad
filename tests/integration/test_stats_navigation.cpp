#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <gtest/gtest.h>

#include <array>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
std::unique_ptr<walker> make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(100, 100);
    return w;
}

// A fresh, wholly walkable board. Every oracle below reads the step a
// successful walk recorded, and living::walk only leaves lastx/lasty alone on
// the "continue direction" branch -- a blocked tile would send it through the
// npc/slide fallbacks and rewrite them.
void reset_open_grid()
{
    auto& lvl = og::runtime::current_session->myscreen_->level_runtime_data();
    lvl.create_new_grid();
    const int size = static_cast<int>(lvl.world().grid.w) * static_cast<int>(lvl.world().grid.h);
    for (int i = 0; i < size; i++)
        lvl.world().grid.data[static_cast<std::size_t>(i)] = PIX_GRASS1;
}

// Take the screen's whole view set aside so the test can build the one- and
// two-view layouts find_follow_leader() branches on, and put it back after.
struct ViewSetRestore
{
    screen* scr;
    short numviews;
    std::array<std::unique_ptr<viewscreen>, MAX_VIEWS> views;

    explicit ViewSetRestore(screen* screen_ptr)
        : scr(screen_ptr), numviews(screen_ptr->numviews)
    {
        for (int i = 0; i < MAX_VIEWS; ++i)
            views[static_cast<std::size_t>(i)] = std::move(scr->viewob[i]);
    }
    ~ViewSetRestore()
    {
        for (int i = 0; i < MAX_VIEWS; ++i)
            scr->viewob[i].reset();
        for (int i = 0; i < MAX_VIEWS; ++i)
            scr->viewob[i] = std::move(views[static_cast<std::size_t>(i)]);
        scr->numviews = numviews;
        scr->relayout_views();
    }
    ViewSetRestore(const ViewSetRestore&) = delete;
    ViewSetRestore& operator=(const ViewSetRestore&) = delete;
};

// Place a walker and refuse to continue if the world clamped it somewhere else
// -- every oracle below is a delta between two placements.
void place(walker* w, int x, int y)
{
    w->setxy(static_cast<float>(x), static_cast<float>(y));
    ASSERT_EQ(x, static_cast<int>(w->xpos())) << "placement must not be clamped";
    ASSERT_EQ(y, static_cast<int>(w->ypos())) << "placement must not be clamped";
}
} // namespace

// COMMAND_FOLLOW's leader choice is find_follow_leader()'s rule (screen.cpp:
// 80-92): with one view it is always viewob[0]->control; with two it is
// whichever view's controller has yo_delay set, view 0 first, and nobody if
// neither does. The follower then steps ONE normalized unit toward the leader
// (zeroing the minor axis past 3:1), releases the leader once inside 60, and
// releases it again on the command's last round.
//
// The old body checked `leader() == nullptr || leader() == viewob[0]->control`,
// which every possible outcome satisfies, and swept the blocked helpers with
// `(void)` casts (those now live in
// StatsRightWalkDirectWalk.blocked_probe_tables_resolve_each_facings_exact_cell).
//
// Reading the step back needs care: for a `living`, walk() on a direction
// CHANGE calls turn(), which rotates one 45-degree notch and overwrites
// lastx/lasty from the new facing. Each case below therefore starts the
// follower already facing the step it should take, so walk() takes the
// continue-direction branch and the recorded step survives -- and a follower
// that computed a DIFFERENT direction turns away, which the curdir oracle
// catches.
TEST(StatsNavigation, follow_picks_the_view_leader_and_steps_one_normalized_unit)
{
    reset_open_grid();

    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "follower walker created";

    screen* const scr = og::runtime::current_session->myscreen_;
    ViewSetRestore restore_views{scr};

    scr->numviews = 1;
    scr->initialize_views();
    ASSERT_NE(nullptr, scr->viewob[0]) << "one-view layout must have view 0";

    auto view0_control = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, view0_control) << "view0 control created";
    scr->viewob[0]->control = view0_control.get();

    const float step = w->stepsize();
    ASSERT_GT(step, 0.0f) << "a level-3 soldier walks somewhere";

    const auto follow_once = [&](int fx, int fy, walker* leader_ob, int lx, int ly,
                                 int rounds, int facing_dir) {
        place(w.get(), fx, fy);
        place(leader_ob, lx, ly);
        w->set_foe(nullptr);
        w->set_leader(nullptr);
        w->set_curdir(static_cast<signed char>(facing_dir));
        w->set_enddir(static_cast<char>(facing_dir));
        w->set_lastx(99.0f);
        w->set_lasty(99.0f);
        w->stats()->clear_command();
        w->stats()->force_command(COMMAND_FOLLOW, rounds, 0, 0);
        return static_cast<int>(w->stats()->do_command());
    };

    // A) One view: the leader IS viewob[0]->control, and a leader down-and-right
    //    at (+100,+100) -- neither axis 3x the other -- yields a diagonal unit
    //    step, recorded by walkstep as lastx/lasty = unit * stepsize.
    ASSERT_EQ(1, follow_once(100, 100, view0_control.get(), 200, 200, 2, FACE_DOWN_RIGHT))
        << "a reachable leader makes COMMAND_FOLLOW succeed";
    ASSERT_EQ(view0_control.get(), w->leader())
        << "with one view the follower adopts viewob[0]->control";
    EXPECT_EQ(FACE_DOWN_RIGHT, static_cast<int>(w->curdir()))
        << "a leader down-and-right is followed down-and-right";
    EXPECT_FLOAT_EQ(step, w->lastx()) << "diagonal follow steps +1 unit in x";
    EXPECT_FLOAT_EQ(step, w->lasty()) << "diagonal follow steps +1 unit in y";

    // B) A leader 4x further in x than in y: the y minor axis is zeroed, so the
    //    follower walks straight right instead of diagonally.
    ASSERT_EQ(1, follow_once(100, 100, view0_control.get(), 300, 150, 2, FACE_RIGHT))
        << "a reachable leader makes COMMAND_FOLLOW succeed";
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(w->curdir()))
        << "x more than 3x y walks straight right";
    EXPECT_FLOAT_EQ(step, w->lastx()) << "x-major follow keeps the x unit step";
    EXPECT_FLOAT_EQ(0.0f, w->lasty()) << "x more than 3x y zeroes the y step";

    // C) Mirrored, and upward: signs must survive the normalization, and a
    //    200-pixel gap must still be ONE step.
    ASSERT_EQ(1, follow_once(200, 300, view0_control.get(), 180, 100, 2, FACE_UP))
        << "a reachable leader makes COMMAND_FOLLOW succeed";
    EXPECT_EQ(FACE_UP, static_cast<int>(w->curdir()))
        << "y more than 3x x walks straight up";
    EXPECT_FLOAT_EQ(0.0f, w->lastx()) << "y more than 3x x zeroes the x step";
    EXPECT_FLOAT_EQ(-step, w->lasty()) << "a leader above is one step up, not 200";

    // D) Inside 60 (Manhattan): don't crowd the leader -- release it, report 1,
    //    and take no step at all. 99 is a value no step can produce.
    place(w.get(), 100, 100);
    place(view0_control.get(), 120, 120);
    w->set_foe(nullptr);
    w->set_leader(nullptr);
    w->set_curdir(static_cast<signed char>(FACE_DOWN_RIGHT));
    w->set_lastx(99.0f);
    w->set_lasty(99.0f);
    w->stats()->clear_command();
    w->stats()->force_command(COMMAND_FOLLOW, 2, 0, 0);
    ASSERT_EQ(1, (int)w->stats()->do_command()) << "the too-close arm still reports success";
    ASSERT_EQ(nullptr, w->leader()) << "a leader within 60 is released";
    EXPECT_FLOAT_EQ(99.0f, w->lastx()) << "the too-close arm breaks before walkstep";
    EXPECT_FLOAT_EQ(99.0f, w->lasty()) << "the too-close arm breaks before walkstep";

    // E) Last round of the command: the leader is released after the step.
    ASSERT_EQ(1, follow_once(100, 100, view0_control.get(), 200, 200, 1, FACE_DOWN_RIGHT))
        << "the last round still reports success";
    ASSERT_EQ(nullptr, w->leader()) << "commandcount < 2 releases the leader after stepping";
    EXPECT_FLOAT_EQ(step, w->lastx()) << "the last round still takes its step";

    // F) Two views, neither controller yelling (yo_delay 0): nobody is
    //    eligible, so the count is zeroed, the queue drains and the result is 0.
    scr->numviews = 2;
    scr->initialize_views();
    ASSERT_NE(nullptr, scr->viewob[1]) << "two-view layout must have view 1";
    auto view1_control = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, view1_control) << "view1 control created";
    scr->viewob[0]->control = view0_control.get();
    scr->viewob[1]->control = view1_control.get();
    view0_control->set_yo_delay(0);
    view1_control->set_yo_delay(0);
    place(view0_control.get(), 260, 260);

    ASSERT_EQ(0, follow_once(100, 100, view1_control.get(), 300, 300, 3, FACE_DOWN_RIGHT))
        << "two views with nobody yelling must report failure";
    ASSERT_EQ(nullptr, w->leader()) << "no leader is adopted";
    ASSERT_TRUE(w->stats()->commands.empty())
        << "the no-leader arm zeroes commandcount, draining the 3-round command";

    // G) Positive control for F: give view 1's controller a yo_delay and it
    //    becomes the leader. Without this row, F would pass for a
    //    find_follow_leader() that always returned nullptr.
    view1_control->set_yo_delay(5);
    ASSERT_EQ(1, follow_once(100, 100, view1_control.get(), 300, 300, 2, FACE_DOWN_RIGHT))
        << "a yelling controller is followable";
    ASSERT_EQ(view1_control.get(), w->leader())
        << "with two views the leader is the controller whose yo_delay is set";

    // H) ... and view 0 wins when BOTH are yelling.
    view0_control->set_yo_delay(5);
    ASSERT_EQ(1, follow_once(100, 100, view1_control.get(), 300, 300, 2, FACE_DOWN_RIGHT))
        << "a yelling controller is followable";
    ASSERT_EQ(view0_control.get(), w->leader())
        << "view 0 is consulted before view 1";

    scr->viewob[0]->control = nullptr;
    if (scr->viewob[1])
        scr->viewob[1]->control = nullptr;
    w->set_leader(nullptr);
    w->stats()->clear_command();
}
