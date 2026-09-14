#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{

screen* game()
{
    return og::runtime::current_session->myscreen_;
}

// redraw() rasterizes into the ACTIVE canvas and refuses anything but World,
// so every test here has to select it — and put the binary's shared screen
// back the way it found it.
class ActiveCanvasGuard
{
public:
    ActiveCanvasGuard() : saved_(game()->active_canvas()) {}
    ~ActiveCanvasGuard() { game()->set_active_canvas(saved_); }
    ActiveCanvasGuard(const ActiveCanvasGuard&) = delete;
    ActiveCanvasGuard& operator=(const ActiveCanvasGuard&) = delete;

private:
    CanvasTarget saved_;
};

void prepare_world()
{
    GameWorld& world = game()->world();
    world.create_new_grid();
    world.delete_objects();
    world.mysmoother.set_target(world.grid);
}

// Every palette index the pane currently shows, sampled on a fixed lattice.
// Used as a positive/negative pair: the same camera with and without the
// control walker must not produce the same pane.
std::vector<int> sample_pane(const viewscreen& vs)
{
    std::vector<int> samples;
    for (int y = 2; y < vs.yview - 2; y += 3)
        for (int x = 2; x < vs.xview - 2; x += 3)
        {
            int index = 0;
            game()->get_pixel(vs.xloc + x, vs.yloc + y, &index);
            samples.push_back(index);
        }
    return samples;
}

std::unique_ptr<walker> make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, game());
    if (w) w->setxy(100, 100);
    return w;
}

} // namespace

// ---------------------------------------------------------------------------
// viewscreen::redraw() — canvas rule + what it actually paints
// ---------------------------------------------------------------------------

// view.cpp:716: a live world is never rasterized into the fixed UI canvas —
// redraw() refuses and returns false. On the World canvas it composes the
// pane and returns true, which is only true if the terrain reaches pixels.
TEST(ViewDraw, redraw_refuses_the_ui_canvas_and_paints_on_the_world_canvas)
{
    viewscreen* vs = game()->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 must exist";
    ActiveCanvasGuard canvas_guard;

    game()->set_active_canvas(CanvasTarget::UI);
    EXPECT_FALSE(vs->redraw())
        << "redraw() must refuse to rasterize the world into the UI canvas";

    game()->set_active_canvas(CanvasTarget::World);
    prepare_world();
    walker* const saved_control = vs->control;
    vs->control = nullptr;
    game()->clearbuffer();

    int cleared = -1;
    game()->get_pixel(vs->xloc + 1, vs->yloc + 1, &cleared);
    ASSERT_EQ(0, cleared) << "clearbuffer must leave the pane black first, or "
                             "the painted-pixel check below proves nothing";

    ASSERT_TRUE(vs->redraw()) << "redraw() on the World canvas must compose";
    int painted = 0;
    game()->get_pixel(vs->xloc + 1, vs->yloc + 1, &painted);
    EXPECT_NE(0, painted) << "the composed pane must carry terrain pixels";

    vs->control = saved_control;
}

// ---------------------------------------------------------------------------
// viewscreen::control — orphan drop and camera follow
// ---------------------------------------------------------------------------

// view.cpp:168 sanitize_control_pointer runs first: a control that is not
// live in the level (oblist/fxlist/weaplist/dead_list) is cleared to nullptr
// and the camera falls back to level_visuals_.topx/topy instead of chasing a
// dangling pointer.
TEST(ViewDraw, redraw_drops_a_control_that_is_not_in_the_level)
{
    viewscreen* vs = game()->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 must exist";
    ActiveCanvasGuard canvas_guard;
    game()->set_active_canvas(CanvasTarget::World);
    prepare_world();

    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "the soldier fixture must build";
    walker* const wp = w.get();
    ASSERT_EQ(100.0f, wp->worldx()) << "fixture placement";

    game()->level_visuals().topx = 0;
    game()->level_visuals().topy = 0;
    vs->topx = 999;
    vs->topy = 999;
    vs->control = wp; // never pushed into any level list: an orphan

    ASSERT_TRUE(vs->redraw());
    EXPECT_EQ(nullptr, vs->control)
        << "a control that is not live in the level must be dropped";
    EXPECT_EQ(0, vs->topx)
        << "the dropped control leaves the camera on level_visuals_.topx";
    EXPECT_EQ(0, vs->topy)
        << "the dropped control leaves the camera on level_visuals_.topy";
}

// view.cpp:742 — with a LIVE control the camera centres the pane on it:
// topx = control x - (xview - sizex)/2, topy likewise. The pane also has to
// look different from the identical camera with no walker in it.
TEST(ViewDraw, redraw_centres_the_camera_on_a_live_control_and_draws_it)
{
    viewscreen* vs = game()->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 must exist";
    ActiveCanvasGuard canvas_guard;
    game()->set_active_canvas(CanvasTarget::World);
    prepare_world();

    auto w = make_walker(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w) << "the soldier fixture must build";
    walker* const wp = w.get();
    wp->setxy(100, 100);
    ASSERT_EQ(100.0f, wp->worldx()) << "fixture placement";
    ASSERT_EQ(100.0f, wp->worldy()) << "fixture placement";

    const Sint32 expected_topx = static_cast<Sint32>(
        100.0f - static_cast<float>(vs->xview - wp->sizex()) / 2.0f);
    const Sint32 expected_topy = static_cast<Sint32>(
        100.0f - static_cast<float>(vs->yview - wp->sizey()) / 2.0f);

    // Negative control: the same camera, same terrain, no walker.
    walker* const saved_control = vs->control;
    vs->control = nullptr;
    game()->level_visuals().topx = expected_topx;
    game()->level_visuals().topy = expected_topy;
    game()->clearbuffer();
    ASSERT_TRUE(vs->redraw());
    ASSERT_EQ(expected_topx, vs->topx) << "camera fallback placement";
    const std::vector<int> empty_pane = sample_pane(*vs);

    // Positive: the walker is now live in the level and is the control.
    game()->world().oblist.push_back(std::move(w));
    vs->control = wp;
    game()->clearbuffer();
    ASSERT_TRUE(vs->redraw());

    EXPECT_EQ(wp, vs->control) << "a live control must survive redraw()";
    EXPECT_EQ(expected_topx, vs->topx)
        << "the camera centres horizontally on the control";
    EXPECT_EQ(expected_topy, vs->topy)
        << "the camera centres vertically on the control";

    const std::vector<int> walker_pane = sample_pane(*vs);
    ASSERT_EQ(empty_pane.size(), walker_pane.size()) << "same pane geometry";
    EXPECT_NE(empty_pane, walker_pane)
        << "the control walker must be drawn inside its own pane";

    vs->control = saved_control;
    game()->world().oblist.pop_back();
}
