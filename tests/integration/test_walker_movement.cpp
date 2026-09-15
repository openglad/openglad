#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/gparser.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <list>
#include <string>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

static auto& movement_world()
{
    return og::runtime::current_session->myscreen_->world();
}

// The integration harness carries world().grid (and pixmaxx/pixmaxy) across
// tests, so a sibling that painted a tree decides whether a step is passable
// here. Every movement test that asserts a position starts from open grass.
static void fresh_grass_map()
{
    movement_world().create_new_grid();
}

// draw_walker_tile's mini-HP-bar gate is a cfg switch, and the test harness
// config carries no effects block (cfg.is_on then answers false). The bar is the
// only walker-visible difference between draw_walker_tile's concealing arms
// (phantom / forestwalk / invisible: no bar) and its ordinary arms, so the tests
// that read the hp_bar trace switch it on through an in-memory override that is
// never persisted, and put it back afterwards.
class ScopedMiniHpBar
{
public:
    ScopedMiniHpBar()
        : previous_(cfg.get_setting("effects", "mini_hp_bar"))
    {
        cfg.apply_override("effects", "mini_hp_bar", "on");
    }
    ~ScopedMiniHpBar()
    {
        cfg.apply_override("effects", "mini_hp_bar",
                           previous_.empty() ? std::string("off") : previous_);
    }
    ScopedMiniHpBar(const ScopedMiniHpBar&) = delete;
    ScopedMiniHpBar& operator=(const ScopedMiniHpBar&) = delete;

private:
    std::string previous_;
};

static walker* make_guy(char family, unsigned char team = 0)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) w->setxy(100, 100);
    return w.release();
}

// ---------------------------------------------------------------------------
// walker::facing - comprehensive direction testing
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_facing_all_16_vectors)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";

    // walker::facing buckets slope = y*1000/x at +-414 / +-2414, per sign of x.
    // Every one of these 16 vectors names exactly one of the eight facings.
    struct { short x; short y; int expected; } dirs[] = {
        {1, 0, FACE_RIGHT},       {-1, 0, FACE_LEFT},
        {0, 1, FACE_DOWN},        {0, -1, FACE_UP},
        {1, 1, FACE_DOWN_RIGHT},  {1, -1, FACE_UP_RIGHT},
        {-1, 1, FACE_DOWN_LEFT},  {-1, -1, FACE_UP_LEFT},
        {2, 1, FACE_DOWN_RIGHT},  {1, 2, FACE_DOWN_RIGHT},
        {-2, 1, FACE_DOWN_LEFT},  {-1, 2, FACE_DOWN_LEFT},
        {2, -1, FACE_UP_RIGHT},   {1, -2, FACE_UP_RIGHT},
        {-2, -1, FACE_UP_LEFT},   {-1, -2, FACE_UP_LEFT}
    };
    for (auto& d : dirs) {
        ASSERT_EQ(d.expected, (int)w->facing(d.x, d.y))
            << "facing(" << d.x << "," << d.y << ") slope bucket";
    }
}


TEST(WalkerMovement, walker_facing_threshold_boundaries_round6)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";

    // x == 0 branch
    ASSERT_EQ(FACE_DOWN, (int)w->facing(0, 1)) << "facing(0,+) should be FACE_DOWN";
    ASSERT_EQ(FACE_UP, (int)w->facing(0, 0)) << "facing(0,0) should be FACE_UP";

    // x > 0 slope buckets
    ASSERT_EQ(FACE_DOWN, (int)w->facing(1, 3)) << "positive x with steep positive slope";
    ASSERT_EQ(FACE_DOWN_RIGHT, (int)w->facing(1, 1)) << "positive x with medium positive slope";
    ASSERT_EQ(FACE_RIGHT, (int)w->facing(1, 0)) << "positive x with flat slope";
    ASSERT_EQ(FACE_UP_RIGHT, (int)w->facing(1, -1)) << "positive x with medium negative slope";
    ASSERT_EQ(FACE_UP, (int)w->facing(1, -3)) << "positive x with steep negative slope";

    // x < 0 slope buckets
    ASSERT_EQ(FACE_UP, (int)w->facing(-1, -3)) << "negative x with steep positive slope";
    ASSERT_EQ(FACE_UP_LEFT, (int)w->facing(-1, -1)) << "negative x with medium positive slope";
    ASSERT_EQ(FACE_LEFT, (int)w->facing(-1, 0)) << "negative x with flat slope";
    ASSERT_EQ(FACE_DOWN_LEFT, (int)w->facing(-1, 1)) << "negative x with medium negative slope";
    ASSERT_EQ(FACE_DOWN, (int)w->facing(-1, 3)) << "negative x with steep negative slope";
}


// ---------------------------------------------------------------------------
// walker::turn - exercises the turning logic
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_turn_to_all_targets)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->set_stepsize(2.0f);

    // turn() rotates ONE 45-degree step per call: distance = curdir - target,
    // clockwise (+1) when distance is in [-4,0) or >= 4, counter-clockwise
    // (+7 mod 8) otherwise. From FACE_UP that is 7 for targets 0 and 5..7.
    static const int expected[8] = {7, 1, 1, 1, 1, 7, 7, 7};
    for (short target = 0; target < 8; target++) {
        w->set_curdir(0);
        ASSERT_TRUE(w->turn(target)) << "turn(" << target << ") reports done";
        ASSERT_EQ(expected[target], (int)w->curdir())
            << "one step from FACE_UP toward " << target;
        // lastx/lasty are rewritten from the NEW facing, +-stepsize.
        if (expected[target] == FACE_UP_RIGHT) {
            ASSERT_FLOAT_EQ(2.0f, w->lastx()) << "FACE_UP_RIGHT lastx = +stepsize";
            ASSERT_FLOAT_EQ(-2.0f, w->lasty()) << "FACE_UP_RIGHT lasty = -stepsize";
        } else {
            ASSERT_FLOAT_EQ(-2.0f, w->lastx()) << "FACE_UP_LEFT lastx = -stepsize";
            ASSERT_FLOAT_EQ(-2.0f, w->lasty()) << "FACE_UP_LEFT lasty = -stepsize";
        }
    }
}


TEST(WalkerMovement, walker_turn_from_all_starts)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";

    // Turning toward FACE_UP (0): distance == start, so starts 1..3 rotate
    // counter-clockwise and starts 4..7 (distance >= 4) rotate clockwise.
    static const int expected[8] = {7, 0, 1, 2, 5, 6, 7, 0};
    for (short start = 0; start < 8; start++) {
        w->set_curdir(static_cast<char>(start));
        ASSERT_TRUE(w->turn(0)) << "turn(FACE_UP) from " << start;
        ASSERT_EQ(expected[start], (int)w->curdir())
            << "shortest-way rotation from curdir " << start << " toward FACE_UP";
    }
}


// ---------------------------------------------------------------------------
// walker::walkstep - movement logic
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_walkstep_cardinals)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->set_stepsize(2.0f);

    struct { short dx; short dy; } steps[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    for (auto& s : steps) {
        w->setxy(200, 200);
        // walk() only MOVES when curdir already equals facing(x,y); otherwise it
        // turns in place and reports success without moving. Keep them aligned
        // so this really exercises the worldmove arm.
        w->set_curdir(static_cast<signed char>(w->facing(s.dx, s.dy)));
        ASSERT_TRUE(w->walkstep(s.dx, s.dy))
            << "open-ground step " << s.dx << "," << s.dy;
        ASSERT_EQ(200 + 2 * s.dx, w->xpos())
            << "step " << s.dx << "," << s.dy << " moves x by stepsize";
        ASSERT_EQ(200 + 2 * s.dy, w->ypos())
            << "step " << s.dx << "," << s.dy << " moves y by stepsize";
        ASSERT_FLOAT_EQ(2.0f * s.dx, w->lastx())
            << "walkstep stores x*stepsize in lastx";
        ASSERT_FLOAT_EQ(2.0f * s.dy, w->lasty())
            << "walkstep stores y*stepsize in lasty";
    }
}


TEST(WalkerMovement, walker_walkstep_diagonals)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->set_stepsize(2.0f);

    struct { short dx; short dy; } steps[] = { {1, 1}, {-1, 1}, {1, -1}, {-1, -1} };
    for (auto& s : steps) {
        w->setxy(200, 200);
        w->set_curdir(static_cast<signed char>(w->facing(s.dx, s.dy)));
        ASSERT_TRUE(w->walkstep(s.dx, s.dy))
            << "open-ground diagonal " << s.dx << "," << s.dy;
        ASSERT_EQ(200 + 2 * s.dx, w->xpos())
            << "diagonal " << s.dx << "," << s.dy << " advances x by stepsize";
        ASSERT_EQ(200 + 2 * s.dy, w->ypos())
            << "diagonal " << s.dx << "," << s.dy << " advances y by stepsize";
        ASSERT_FLOAT_EQ(2.0f * s.dx, w->lastx())
            << "walkstep stores x*stepsize in lastx";
        ASSERT_FLOAT_EQ(2.0f * s.dy, w->lasty())
            << "walkstep stores y*stepsize in lasty";
    }
}


TEST(WalkerMovement, walker_walkstep_zero)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->set_stepsize(2.0f);

    // walkstep(0,0): facing(0,0) is FACE_UP, so with curdir already FACE_UP
    // living::walk takes its continue-direction arm and moves nowhere.
    w->setxy(200, 200);
    w->set_curdir(FACE_UP);
    ASSERT_TRUE(w->walkstep(0, 0)) << "walkstep(0,0) succeeds";
    ASSERT_EQ(200, w->xpos()) << "walkstep(0,0) does not move x";
    ASSERT_EQ(200, w->ypos()) << "walkstep(0,0) does not move y";
    ASSERT_FLOAT_EQ(0.0f, w->lastx()) << "walkstep(0,0) zeroes lastx";
    ASSERT_FLOAT_EQ(0.0f, w->lasty()) << "walkstep(0,0) zeroes lasty";

    // NPC blocked walking LEFT off the map: the FACE_LEFT fallback arm turns
    // south, walks one full step, and restores the original facing.
    w->set_user(-1);
    w->setxy(0, 0);
    w->set_curdir(FACE_LEFT);
    ASSERT_TRUE(w->walkstep(-1, 0)) << "npc FACE_LEFT fallback finds a way south";
    ASSERT_EQ(0, w->xpos()) << "the blocked axis does not move";
    ASSERT_EQ(2, w->ypos()) << "the FACE_LEFT fallback walks +stepsize south";
    ASSERT_EQ(FACE_LEFT, (int)w->curdir()) << "walkstep restores oldcurdir";

    // NPC blocked walking UP in the corner: the fallback is FACE_LEFT, also off
    // the map, so nothing moves and walkstep fails.
    w->setxy(0, 0);
    w->set_curdir(FACE_UP);
    ASSERT_FALSE(w->walkstep(0, -1)) << "npc FACE_UP fallback is also blocked";
    ASSERT_EQ(0, w->xpos()) << "no movement on a doubly blocked npc step";
    ASSERT_EQ(0, w->ypos()) << "no movement on a doubly blocked npc step";

    // A user blocked on a diagonal SLIDES along the open axis, yet walkstep
    // still returns FALSE (ret1/ret2 are never set on the slide path).
    w->set_user(0);
    w->setxy(0, 10);
    w->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(w->walkstep(-1, -1)) << "the user slide path reports failure";
    ASSERT_EQ(0, w->xpos()) << "x stays pinned at the left edge";
    ASSERT_EQ(8, w->ypos()) << "y slides up one pixel per stepsize unit";
    ASSERT_EQ(FACE_UP_LEFT, (int)w->curdir()) << "the slide restores oldcurdir";

    // Same slide, mirrored: FACE_DOWN_LEFT against the left edge goes south.
    w->setxy(0, 10);
    w->set_curdir(FACE_DOWN_LEFT);
    ASSERT_FALSE(w->walkstep(-1, 1)) << "the user slide path reports failure";
    ASSERT_EQ(0, w->xpos()) << "x stays pinned at the left edge";
    ASSERT_EQ(12, w->ypos()) << "y slides down one pixel per stepsize unit";
}


// The user slide's four cardinal arms are `break` with dx == dy == 0: a player
// who walks straight into a wall does NOT slide along it. The distinguishing
// observable is the animation cycle - the slide loop advances it once per
// stepsize unit it moves, so a cardinal break must leave it exactly where it
// was, with the walker unmoved and oldcurdir restored.
TEST(WalkerMovement, walker_walkstep_user_slide_cardinal_break_path_round5)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "walker created";

    w->set_user(0);
    w->set_stepsize(2.0f);
    w->setxy(0, 24); // left edge forces blocked cardinal movement
    w->set_curdir(FACE_LEFT);
    w->set_cycle(0);

    ASSERT_FALSE(w->walkstep(-1, 0))
        << "blocked cardinal user movement keeps slide dx/dy at zero and fails";
    ASSERT_EQ(0, w->xpos()) << "a cardinal break never slides horizontally";
    ASSERT_EQ(24, w->ypos()) << "a cardinal break never slides vertically";
    ASSERT_EQ(FACE_LEFT, (int)w->curdir()) << "walkstep restores oldcurdir";
    ASSERT_EQ(0, (int)w->cycle())
        << "the cardinal arm never enters the slide loop, so no frame is cycled";
    ASSERT_FLOAT_EQ(-2.0f, w->lastx()) << "lastx = x*stepsize, stored before any walk";
    ASSERT_FLOAT_EQ(0.0f, w->lasty()) << "lasty = y*stepsize, stored before any walk";
}


TEST(WalkerMovement, walker_walkstep_user_slide_diagonal_switch_cases_round6)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    // Block movement at map edge so the user-slide diagonal switch executes.
    // It is only reached when curdir already equals the step's facing (else
    // walk() just turns in place) AND both walk attempts fail.
    w->set_user(0);
    w->set_stepsize(1.0f);

    const short right_edge =
        static_cast<short>(movement_world().pixmaxx - w->sizex() - 1);
    const short bottom_edge =
        static_cast<short>(movement_world().pixmaxy - w->sizey() - 1);

    // FACE_UP_RIGHT in the top-left corner: up is off-map, right is open, so
    // the slide moves one pixel east and still returns FALSE.
    w->setxy(0, 0);
    w->set_curdir(FACE_UP_RIGHT);
    ASSERT_FALSE(w->walkstep(1, -1)) << "the slide path returns ret1||ret2 == 0";
    ASSERT_EQ(1, w->xpos()) << "FACE_UP_RIGHT slides east along the top edge";
    ASSERT_EQ(0, w->ypos()) << "the blocked vertical axis does not move";
    ASSERT_EQ(FACE_UP_RIGHT, (int)w->curdir()) << "curdir is restored after the slide";

    // FACE_UP_LEFT in the same corner: both axes blocked -> nothing moves.
    w->setxy(0, 0);
    w->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(w->walkstep(-1, -1)) << "both axes blocked -> failure";
    ASSERT_EQ(0, w->xpos()) << "no horizontal slide in the corner";
    ASSERT_EQ(0, w->ypos()) << "no vertical slide in the corner";

    // FACE_DOWN_RIGHT against the right edge: east blocked, south open.
    w->setxy(right_edge, static_cast<short>(bottom_edge - 4));
    w->set_curdir(FACE_DOWN_RIGHT);
    ASSERT_FALSE(w->walkstep(1, 1)) << "the slide path returns ret1||ret2 == 0";
    ASSERT_EQ(right_edge, w->xpos()) << "the blocked horizontal axis holds";
    ASSERT_EQ(bottom_edge - 3, w->ypos()) << "FACE_DOWN_RIGHT slides south";

    // FACE_DOWN_LEFT against the left edge: west blocked, south open.
    w->setxy(static_cast<short>(0), static_cast<short>(bottom_edge - 4));
    w->set_curdir(FACE_DOWN_LEFT);
    ASSERT_FALSE(w->walkstep(-1, 1)) << "the slide path returns ret1||ret2 == 0";
    ASSERT_EQ(0, w->xpos()) << "the blocked horizontal axis holds";
    ASSERT_EQ(bottom_edge - 3, w->ypos()) << "FACE_DOWN_LEFT slides south";
}


// ---------------------------------------------------------------------------
// walker::draw and walker::draw_tile via viewscreen
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_draw_basic)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->setxy(100, 100);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 exists";
    ASSERT_TRUE(draw_walker(*w, vs)) << "a live walker composites";

    // A dormant (delayed-spawn) walker has not entered the world; outside the
    // editor (editor_floor_override_ < 0) draw_walker refuses it.
    ASSERT_LT(vs->editor_floor_override_, 0) << "not an editor view";
    w->set_dormant(true);
    ASSERT_FALSE(draw_walker(*w, vs)) << "a dormant walker is not drawn in play";
    w->set_dormant(false);

    // And it refuses a corpse.
    w->set_dead(1);
    ASSERT_FALSE(draw_walker(*w, vs)) << "a dead walker is not drawn";
    w->set_dead(0);
    ASSERT_TRUE(draw_walker(*w, vs)) << "live again, drawn again";
}


TEST(WalkerMovement, walker_draw_tile_basic)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->setxy(100, 100);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 exists";
    walker* old_control = vs->control;
    walker* control = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, control) << "control walker created";
    control->setxy(48, 48);
    vs->control = control;

    const ScopedMiniHpBar mini_hp_bar;

    // Damaged, so the mini HP bar is eligible: only draw_walker_tile's outline
    // and plain arms draw it, which is how the arm taken becomes observable.
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(50.0f);
    w->set_last_hitpoints(50.0f);

    // No status effect: compute_outline clears the outline and the plain arm runs.
    w->set_outline(0);
    trace_clear();
    ASSERT_TRUE(draw_walker_tile(*w, vs)) << "a live walker tile-draws";
    ASSERT_EQ(0, (int)w->outline()) << "no status -> outline 0";
    ASSERT_TRUE(trace_contains("hp_bar", "draw")) << "the plain arm draws the HP bar";

    // Invisible, seen by a SAME-team control: outline becomes the team colour
    // (40 for team 0) and the invisible arm suppresses the HP bar.
    w->set_invisibility_left(12);
    trace_clear();
    ASSERT_TRUE(draw_walker_tile(*w, vs)) << "an invisible walker still tile-draws";
    ASSERT_EQ(40, (int)w->outline()) << "invisibility paints the team-colour outline";
    ASSERT_EQ(40, (int)w->query_team_color()) << "team 0 ramp base";
    ASSERT_FALSE(trace_contains("hp_bar", "draw")) << "the invisible arm hides the HP bar";
    w->set_invisibility_left(0);

    // Invulnerable: the team-colour arm promotes the outline to
    // OUTLINE_INVULNERABLE and the outline arm draws the HP bar again.
    w->set_invulnerable_left(10);
    trace_clear();
    ASSERT_TRUE(draw_walker_tile(*w, vs)) << "an invulnerable walker tile-draws";
    ASSERT_EQ((int)OUTLINE_INVULNERABLE, (int)w->outline())
        << "invulnerability promotes the outline";
    ASSERT_TRUE(trace_contains("hp_bar", "draw")) << "the outline arm draws the HP bar";
    w->set_invulnerable_left(0);

    vs->control = old_control;
    delete control;
}


TEST(WalkerMovement, walker_draw_with_flight)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->setxy(100, 100);
    w->set_outline(0);
    w->set_flight_left(10);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 exists";
    ASSERT_TRUE(draw_walker(*w, vs)) << "a flying walker composites";
    ASSERT_EQ((int)OUTLINE_FLYING, (int)w->outline())
        << "flight_left alone paints the flying outline";
}


TEST(WalkerMovement, walker_draw_with_invisibility)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->setxy(100, 100);
    w->set_invisibility_left(10);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 exists";
    walker* old_control = vs->control;
    walker* control = make_guy(FAMILY_SOLDIER, 1);
    ASSERT_NE(nullptr, control) << "enemy-team control walker created";
    control->setxy(48, 48);
    vs->control = control;

    // compute_outline is a state machine over the CURRENT outline. This walker
    // is not BIT_NAMED, so the OUTLINE_NAMED arms stay out of the way.
    ASSERT_FALSE(w->stats()->query_bit_flags(BIT_NAMED)) << "not a named NPC";
    w->set_outline(0);
    ASSERT_TRUE(draw_walker(*w, vs)) << "an invisible walker composites";
    ASSERT_EQ(40, (int)w->outline()) << "from 0: invisibility -> team colour";

    w->compute_outline(vs->control);
    ASSERT_EQ(40, (int)w->outline())
        << "team-colour arm with no invuln/flight holds the team colour";

    w->set_flight_left(8);
    w->compute_outline(vs->control);
    ASSERT_EQ((int)OUTLINE_FLYING, (int)w->outline())
        << "team-colour arm promotes to flying";

    w->set_invulnerable_left(8);
    w->compute_outline(vs->control);
    ASSERT_EQ(40, (int)w->outline())
        << "flying arm drops back to the team colour while still invisible";

    w->set_invisibility_left(0);
    w->compute_outline(vs->control);
    ASSERT_EQ((int)OUTLINE_INVULNERABLE, (int)w->outline())
        << "once visible, the team-colour arm promotes to invulnerable";

    vs->control = old_control;
    delete control;
}


TEST(WalkerMovement, walker_draw_with_invulnerability)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->setxy(100, 100);
    w->set_outline(0);
    w->set_invulnerable_left(10);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen 0 exists";
    ASSERT_TRUE(draw_walker(*w, vs)) << "an invulnerable walker composites";
    ASSERT_EQ((int)OUTLINE_INVULNERABLE, (int)w->outline())
        << "invulnerable_left alone paints the invulnerable outline";
}


TEST(WalkerMovement, stationary_family_walk_and_turn_branches)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->setxy(120, 120);
    w->set_order_family(Order::Living, FAMILY_TOWER1);

    // walkstep stationary short-circuit branch.
    ASSERT_TRUE(w->walkstep(1, 0)) << "stationary walkstep should succeed without moving";
    ASSERT_EQ(1, (int)w->lastx()) << "stationary walkstep should store unit x input";
    ASSERT_EQ(0, (int)w->lasty()) << "stationary walkstep should store unit y input";

    // walk() stationary branch.
    ASSERT_TRUE(w->walk(1, 1)) << "stationary walk should succeed";

    // turn() stationary branch should not overwrite facing vector.
    w->set_lastx(7);
    w->set_lasty(-3);
    (void)w->turn(FACE_LEFT);
    ASSERT_EQ(7, (int)w->lastx()) << "stationary turn should preserve lastx";
    ASSERT_EQ(-3, (int)w->lasty()) << "stationary turn should preserve lasty";
}


TEST(WalkerMovement, walker_walkstep_user_slide_sets_vertical_and_horizontal_dirs)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->set_user(0);
    w->set_stepsize(2.0f);

    // Horizontal-only slide: up blocked at the top edge, right passable. The
    // slide arm is reached only with curdir == facing(x,y), and it walks one
    // pixel per stepsize unit while reporting failure.
    w->setxy(32, 0);
    w->set_curdir(FACE_UP_RIGHT);
    ASSERT_FALSE(w->walkstep(1, -1)) << "the user slide path reports failure";
    ASSERT_EQ(34, w->xpos()) << "two pixels of horizontal slide (stepsize 2)";
    ASSERT_EQ(0, w->ypos()) << "the blocked vertical axis holds";
    ASSERT_EQ(FACE_UP_RIGHT, (int)w->curdir()) << "the slide restores oldcurdir";

    // Vertical-only slide: left blocked at the left edge, up passable.
    w->setxy(0, 32);
    w->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(w->walkstep(-1, -1)) << "the user slide path reports failure";
    ASSERT_EQ(0, w->xpos()) << "the blocked horizontal axis holds";
    ASSERT_EQ(30, w->ypos()) << "two pixels of vertical slide (stepsize 2)";
    ASSERT_EQ(FACE_UP_LEFT, (int)w->curdir()) << "the slide restores oldcurdir";

    // Control: with curdir NOT equal to the step's facing, living::walk takes
    // its changed-direction branch instead — it records the wanted facing in
    // enddir, rotates ONE step toward it, moves nothing and reports success.
    // That is the shape this test used to assert by accident.
    w->setxy(64, 64);
    w->set_curdir(FACE_DOWN);
    ASSERT_TRUE(w->walkstep(1, -1)) << "a turn-in-place step succeeds";
    ASSERT_EQ(64, w->xpos()) << "turning in place moves nothing";
    ASSERT_EQ(64, w->ypos()) << "turning in place moves nothing";
    ASSERT_EQ(FACE_UP_RIGHT, (int)w->enddir()) << "the wanted facing lands in enddir";
    ASSERT_EQ(FACE_DOWN_RIGHT, (int)w->curdir())
        << "one 45-degree step from FACE_DOWN toward FACE_UP_RIGHT";
}


// ---------------------------------------------------------------------------
// walker::animate - different animation types
// ---------------------------------------------------------------------------

// Length of a sentinel(-1)-terminated animation row, as animate() computes it.
static int ani_row_length(const signed char* seq)
{
    int len = 0;
    while (len < 128 && seq[len] != -1)
        len++;
    return len;
}


TEST(WalkerMovement, walker_animate_walk)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->set_curdir(FACE_RIGHT);
    w->set_ani_type(ANI_WALK);
    w->set_cycle(0);

    const int row = FACE_RIGHT + ANI_WALK * NUM_FACINGS;
    ASSERT_GT(w->ani_count, row) << "the soldier table holds a FACE_RIGHT walk row";
    const signed char* seq = w->ani[row];
    ASSERT_NE(nullptr, seq) << "the walk row is populated";
    const int len = ani_row_length(seq);
    ASSERT_GT(len, 1) << "the walk row has frames";

    // animate() shows seq[cycle], then advances cycle, wrapping to 0 at the end.
    for (int c = 0; c < len; c++) {
        ASSERT_TRUE(w->animate()) << "walk step " << c << " animates";
        ASSERT_EQ((int)seq[c], (int)w->frame())
            << "walk step " << c << " shows the row's frame";
        ASSERT_EQ((c + 1 == len) ? 0 : c + 1, (int)w->cycle())
            << "walk step " << c << " advances (and wraps) cycle";
        ASSERT_EQ(ANI_WALK, (int)w->ani_type()) << "walking stays walking";
    }
}


TEST(WalkerMovement, walker_animate_attack)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->set_curdir(FACE_RIGHT);
    w->set_ani_type(ANI_ATTACK);
    w->set_cycle(0);
    // The attack sequence ends in fire(); starve the magic so that release is a
    // no-op and this test stays about the animation bookkeeping.
    w->stats()->set_magicpoints(0.0f);
    w->stats()->set_weapon_cost(1);

    const int row = FACE_RIGHT + ANI_ATTACK * NUM_FACINGS;
    ASSERT_GT(w->ani_count, row) << "the soldier table holds a FACE_RIGHT attack row";
    const signed char* seq = w->ani[row];
    ASSERT_NE(nullptr, seq) << "the attack row is populated";
    const int len = ani_row_length(seq);
    ASSERT_GT(len, 1) << "the attack row has frames";

    for (int c = 0; c < len; c++) {
        ASSERT_TRUE(w->animate()) << "attack step " << c << " animates";
        ASSERT_EQ((int)seq[c], (int)w->frame())
            << "attack step " << c << " shows the attack row's frame";
        if (c + 1 < len) {
            ASSERT_EQ(c + 1, (int)w->cycle()) << "attack step " << c << " advances cycle";
            ASSERT_EQ(ANI_ATTACK, (int)w->ani_type()) << "still swinging";
        }
    }
    // End of the attack sequence: fire(), then back to walking from cycle 0.
    ASSERT_EQ(ANI_WALK, (int)w->ani_type()) << "a finished attack returns to ANI_WALK";
    ASSERT_EQ(0, (int)w->cycle()) << "a finished attack resets cycle";
}


TEST(WalkerMovement, walker_animate_all_families)
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    int animated = 0;
    for (int i = 0; i < 14; i++) {
        walker* w = make_guy(families[i], 0);
        ASSERT_NE(nullptr, w) << "family " << (int)families[i] << " built a walker";
        ASSERT_GT(w->ani_count, 0) << "family " << (int)families[i]
                                   << " carries an animation table length";
        const signed char* seq = w->ani[FACE_UP + ANI_WALK * NUM_FACINGS];
        ASSERT_NE(nullptr, seq) << "family " << (int)families[i]
                                << " has a FACE_UP walk row";
        w->set_curdir(FACE_UP);
        w->set_ani_type(ANI_WALK);
        w->set_cycle(0);
        ASSERT_TRUE(w->animate()) << "family " << (int)families[i] << " animates";
        ASSERT_EQ((int)seq[0], (int)w->frame())
            << "family " << (int)families[i] << " shows walk frame 0";
        ++animated;
    }
    ASSERT_EQ(14, animated) << "every listed family was exercised";
}


TEST(WalkerMovement, round9_user_cardinal_slide_break_and_offmap_guards)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "walker created";

    // User + cardinal blocked move: dx/dy stays 0 in the slide switch, so the
    // walker holds its exact tile and its facing.
    w->set_user(0);
    w->set_stepsize(1.0f);
    w->setxy(0, 16);
    w->set_curdir(FACE_LEFT);
    w->set_cycle(0);
    ASSERT_FALSE(w->walkstep(-1, 0)) << "a blocked cardinal user slide fails";
    ASSERT_EQ(0, w->xpos()) << "the blocked axis holds";
    ASSERT_EQ(16, w->ypos()) << "the cardinal arm never slides";
    ASSERT_EQ(FACE_LEFT, (int)w->curdir()) << "walkstep restores oldcurdir";
    ASSERT_EQ(0, (int)w->cycle()) << "no slide means no cycled frame";

    // walk(0, 0) for a LIVING: living::walk has no zero-step guard, so
    // facing(0,0) == FACE_UP makes a standstill a turn order. It reports
    // success, moves nothing, and rotates exactly one 45-degree step toward
    // FACE_UP (FACE_LEFT + 1 == FACE_UP_LEFT, the shorter way round).
    ASSERT_TRUE(w->walk(0, 0)) << "walk(0,0) reports success";
    ASSERT_EQ(0, w->xpos()) << "walk(0,0) moves nothing";
    ASSERT_EQ(16, w->ypos()) << "walk(0,0) moves nothing";
    ASSERT_EQ(FACE_UP, (int)w->enddir())
        << "facing(0,0) is FACE_UP, so a standstill aims the walker north";
    ASSERT_EQ(FACE_UP_LEFT, (int)w->curdir())
        << "turn() rotates one step per call, FACE_LEFT -> FACE_UP_LEFT";
    ASSERT_EQ(0, (int)w->cycle()) << "a turn does not advance the walk cycle";

    // The base walker::walk DOES carry that zero-step guard, and only a
    // non-living reaches it: it returns success with the facing untouched.
    {
        PixieData px(1, 1, 1, new unsigned char[1]{0});
        walker nonliving(px);
        nonliving.setxy(32, 32);
        nonliving.set_curdir(FACE_LEFT);
        nonliving.set_cycle(0);
        ASSERT_TRUE(nonliving.walk(0, 0)) << "walker::walk(0,0) reports success";
        ASSERT_EQ(FACE_LEFT, (int)nonliving.curdir())
            << "the zero-step guard returns before any re-facing";
        ASSERT_EQ(0, (int)nonliving.cycle())
            << "the zero-step guard returns before any animation";
    }

    w->set_curdir(FACE_LEFT);

    // Off-map guard in walk(): the step is refused BEFORE the passability
    // check, so nothing moves and nothing animates.
    w->setxy(0, 0);
    w->set_curdir(FACE_LEFT);
    ASSERT_FALSE(w->walk(-1, 0)) << "walk fails when the target is off map";
    ASSERT_EQ(0, w->xpos()) << "an off-map step moves nothing";
    ASSERT_EQ(0, w->ypos()) << "an off-map step moves nothing";
    ASSERT_EQ(0, (int)w->cycle()) << "an off-map step cycles no frame";
}


// walk()'s blocked arm is gated on BIT_ANIMATE: a blocked walker that animates
// regardless still cycles one frame, and one that does not holds its cycle.
// Both arms move nothing - the cycle is the whole observable difference.
TEST(WalkerMovement, round9_blocked_animate_angle_and_turn_default_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "walker created";

    // Force an in-bounds blocked move and keep animation active.
    w->stats()->set_bit_flags(BIT_ANIMATE, 1);
    w->setxy(GRID_SIZE, GRID_SIZE);
    w->set_curdir(FACE_RIGHT);
    // Moving from (1,1) one tile right targets tile (2,1).
    og::runtime::current_session->myscreen_->world().grid.data[1 * og::runtime::current_session->myscreen_->world().grid.w + 2] = PIX_TREE_M1;
    w->set_cycle(0);
    ASSERT_FALSE(w->walk(1, 0)) << "the blocked in-bounds step fails";
    ASSERT_EQ(GRID_SIZE, w->xpos()) << "a blocked step moves nothing";
    ASSERT_EQ(GRID_SIZE, w->ypos()) << "a blocked step moves nothing";
    ASSERT_EQ(1, (int)w->cycle())
        << "BIT_ANIMATE cycles exactly one frame on the blocked step";

    // Negative control: the same blocked step without BIT_ANIMATE holds the
    // cycle where it was.
    w->stats()->set_bit_flags(BIT_ANIMATE, 0);
    w->set_cycle(0);
    ASSERT_FALSE(w->walk(1, 0)) << "the blocked in-bounds step still fails";
    ASSERT_EQ(0, (int)w->cycle())
        << "without BIT_ANIMATE a blocked step cycles no frame";
    w->stats()->set_bit_flags(BIT_ANIMATE, 1);

    // get_current_angle's out-of-range default arm. (The exact per-facing
    // ladder is pinned by WalkerMovement.walker_get_current_angle_all_direction_cases.)
    w->set_curdir(99);
    ASSERT_FLOAT_EQ(0.0f, w->get_current_angle())
        << "an out-of-range facing uses the default angle";

    // Classic invalid curdir handling keeps the modulo result and falls through
    // to the default last-vector branch instead of clamping to a valid facing.
    w->set_stepsize(2.0f);
    w->set_curdir(static_cast<char>(-120));
    (void)w->turn(FACE_UP);
    ASSERT_EQ(0, (int)w->lastx()) << "invalid turn direction should use default lastx";
    ASSERT_EQ(-2, (int)w->lasty()) << "invalid turn direction should default lasty to -stepsize";
}


// ---------------------------------------------------------------------------
// walker::create_weapon
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_create_weapon_soldier)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->setxy(200, 200);
    // fire() reads the heading off lastx/lasty, NOT curdir.
    w->set_lastx(1);
    w->set_lasty(0);
    w->stats()->set_magicpoints(100.0f);
    const short cost = w->stats()->weapon_cost();
    // The fighter's on_fire_weapon hook refunds and kills the blade when it has
    // no weapons left, so the throw only happens with one in hand.
    ASSERT_GT((int)w->weapons_left(), 0) << "the fighter still holds a blade";
    const short blades_before = w->weapons_left();

    walker* weap = w->fire();
    ASSERT_NE(nullptr, weap) << "firing east on open ground releases a weapon";
    ASSERT_EQ((int)Order::Weapon, (int)weap->query_order()) << "it is an Order::Weapon";
    ASSERT_EQ((int)w->current_weapon(), (int)weap->family())
        << "it is the walker's current weapon family";
    ASSERT_EQ(w, weap->owner()) << "the thrower owns it";
    ASSERT_EQ((int)w->team_num(), (int)weap->team_num()) << "it inherits the team";
    // FACE_RIGHT spawn geometry: just past our right edge, vertically centred.
    ASSERT_EQ(w->xpos() + w->sizex() + 1, weap->xpos()) << "spawned east of us";
    ASSERT_EQ(w->ypos() + (w->sizey() - weap->sizey()) / 2, weap->ypos())
        << "spawned vertically centred";
    ASSERT_FLOAT_EQ(weap->stepsize(), weap->lastx()) << "it flies east at stepsize";
    ASSERT_FLOAT_EQ(100.0f - (float)cost, w->stats()->magicpoints())
        << "firing costs weapon_cost magic points";
    ASSERT_EQ(blades_before - 1, (int)w->weapons_left())
        << "a ranged release consumes one blade";

    og::runtime::current_session->myscreen_->world().remove_ob(weap);
}


TEST(WalkerMovement, walker_create_weapon_archer)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_ARCHER, 0);
    ASSERT_NE(nullptr, w) << "archer walker created";
    w->setxy(200, 200);
    w->set_lastx(1);
    w->set_lasty(0);
    w->stats()->set_magicpoints(100.0f);
    const short cost = w->stats()->weapon_cost();

    walker* weap = w->fire();
    ASSERT_NE(nullptr, weap) << "the archer looses an arrow";
    ASSERT_EQ((int)Order::Weapon, (int)weap->query_order()) << "it is an Order::Weapon";
    ASSERT_EQ((int)FAMILY_ARROW, (int)weap->family()) << "the archer's weapon is an arrow";
    ASSERT_EQ((int)w->current_weapon(), (int)weap->family())
        << "and it is the archer's current weapon";
    ASSERT_EQ(w->xpos() + w->sizex() + 1, weap->xpos()) << "spawned east of us";
    ASSERT_FLOAT_EQ(weap->stepsize(), weap->lastx()) << "it flies east at stepsize";
    ASSERT_FLOAT_EQ(100.0f - (float)cost, w->stats()->magicpoints())
        << "firing costs weapon_cost magic points";

    og::runtime::current_session->myscreen_->world().remove_ob(weap);
}


TEST(WalkerMovement, walker_create_weapon_mage)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_MAGE, 0);
    ASSERT_NE(nullptr, w) << "mage walker created";
    w->setxy(200, 200);
    w->set_lastx(0);
    w->set_lasty(1);
    w->stats()->set_magicpoints(100.0f);
    const short cost = w->stats()->weapon_cost();

    walker* weap = w->fire();
    ASSERT_NE(nullptr, weap) << "the mage casts south";
    ASSERT_EQ((int)Order::Weapon, (int)weap->query_order()) << "it is an Order::Weapon";
    ASSERT_EQ((int)FAMILY_FIREBALL, (int)weap->family()) << "the mage's weapon is a fireball";
    // FACE_DOWN spawn geometry: just past our bottom edge, horizontally centred.
    ASSERT_EQ(w->ypos() + w->sizey() + 1, weap->ypos()) << "spawned south of us";
    ASSERT_EQ(w->xpos() + (w->sizex() - weap->sizex()) / 2, weap->xpos())
        << "spawned horizontally centred";
    ASSERT_FLOAT_EQ(weap->stepsize(), weap->lasty()) << "it flies south at stepsize";
    ASSERT_FLOAT_EQ(100.0f - (float)cost, w->stats()->magicpoints())
        << "casting costs weapon_cost magic points";

    og::runtime::current_session->myscreen_->world().remove_ob(weap);
}


// round6_blocked_animate_and_default_angle_turn lived here. Every row it had
// is pinned, exactly, by a sibling: its "blocked walk + BIT_ANIMATE" row never
// reached the animate arm at all (walking west from x == 0 returns on walk()'s
// off-map guard, which is pinned by round9_user_cardinal_slide_break_and_offmap_guards,
// and the real BIT_ANIMATE gate is pinned by
// round9_blocked_animate_angle_and_turn_default_paths); the angle default is
// pinned by walker_get_current_angle_all_direction_cases; and the (99 + 1) % 8
// turn is pinned by facing_buckets_and_npc_fallback_component_walks.


// ---------------------------------------------------------------------------
// walker on_screen
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_setxy_moves_obmap_registration)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->setxy(100, 100);

    obmap* map = movement_world().myobmap.get();
    ASSERT_NE(nullptr, map) << "the world carries an obmap";

    // setxy's real work is the spatial index: it re-buckets the walker so
    // collision queries can find it at its new cell.
    std::list<walker*>& first = map->obmap_get_list(100, 100);
    ASSERT_NE(first.end(), std::find(first.begin(), first.end(), w))
        << "setxy registers the walker in the cell it moved to";
    ASSERT_EQ(1u, map->size()) << "only this walker is registered";

    w->setxy(300, 300);
    ASSERT_EQ(300, w->xpos()) << "xpos follows";
    ASSERT_EQ(300, w->ypos()) << "ypos follows";
    ASSERT_FLOAT_EQ(300.0f, w->worldx()) << "worldx follows";
    ASSERT_FLOAT_EQ(300.0f, w->worldy()) << "worldy follows";
    std::list<walker*>& stale = map->obmap_get_list(100, 100);
    ASSERT_EQ(stale.end(), std::find(stale.begin(), stale.end(), w))
        << "the old cell must not keep a stale pointer";
    std::list<walker*>& moved = map->obmap_get_list(300, 300);
    ASSERT_NE(moved.end(), std::find(moved.begin(), moved.end(), w))
        << "the new cell holds the walker";
    ASSERT_EQ(1u, map->size()) << "a move does not duplicate the registration";

    // A non-colliding (ignore) walker is REMOVED from the index instead, while
    // its coordinates still update.
    w->set_ignore(1);
    w->setxy(320, 320);
    ASSERT_EQ(0u, map->size()) << "an ignore() walker is dropped from the obmap";
    ASSERT_EQ(320, w->xpos()) << "the position still updates";
    ASSERT_EQ(320, w->ypos()) << "the position still updates";
    w->set_ignore(0);
}


TEST(WalkerMovement, walker_draw_tile_phantom_and_forestwalk_paths)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_ELF, 0);
    ASSERT_NE(nullptr, w) << "elf walker created";
    w->setxy(96, 96);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs) << "viewscreen exists";
    walker* old_control = vs->control;
    walker* control = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, control) << "control walker created";
    control->setxy(48, 48);
    vs->control = control;

    const ScopedMiniHpBar mini_hp_bar;

    // Damage the elf so the mini HP bar is eligible. Only draw_walker_tile's
    // ORDINARY arms (outline / plain) draw it; the PHANTOM and concealed
    // FORESTWALK arms deliberately do not, so the hp_bar trace tells us which
    // arm ran — deleting either arm falls through to the plain blit and the bar
    // reappears.
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(50.0f);
    w->set_last_hitpoints(50.0f);

    // Control: no phantom, no trees -> plain arm, HP bar drawn, outline cleared.
    w->set_outline(0);
    trace_clear();
    ASSERT_TRUE(draw_walker_tile(*w, vs)) << "a live walker tile-draws";
    ASSERT_EQ(0, (int)w->outline()) << "no status effect -> outline 0";
    ASSERT_TRUE(trace_contains("hp_bar", "draw"))
        << "the ordinary arm draws the mini HP bar";

    // PHANTOM arm.
    w->stats()->set_bit_flags(BIT_PHANTOM, 1);
    w->set_outline(0);
    trace_clear();
    ASSERT_TRUE(draw_walker_tile(*w, vs)) << "a phantom tile-draws";
    ASSERT_EQ(0, (int)w->outline()) << "compute_outline still runs on the phantom arm";
    ASSERT_FALSE(trace_contains("hp_bar", "draw"))
        << "the phantom arm hides the mini HP bar";
    w->stats()->set_bit_flags(BIT_PHANTOM, 0);

    // FORESTWALK arm: an elf standing on trees, not flying, is concealed.
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_FORESTWALK))
        << "the elf family carries BIT_FORESTWALK";
    const int tx = w->xpos() / GRID_SIZE;
    const int ty = w->ypos() / GRID_SIZE;
    ASSERT_LT(tx, movement_world().grid.w) << "tile x in range";
    ASSERT_LT(ty, movement_world().grid.h) << "tile y in range";
    movement_world().grid.data[static_cast<std::size_t>(ty * movement_world().grid.w + tx)] =
        PIX_TREE_T1;
    movement_world().mysmoother.set_target(movement_world().grid);
    ASSERT_EQ(TYPE_TREES, movement_world().mysmoother.query_genre_x_y(tx, ty))
        << "the cell under the elf reads as trees";
    w->set_flight_left(0);
    w->stats()->set_bit_flags(BIT_FLYING, 0);
    w->set_outline(0);
    trace_clear();
    ASSERT_TRUE(draw_walker_tile(*w, vs)) << "a concealed forestwalker tile-draws";
    ASSERT_FALSE(trace_contains("hp_bar", "draw"))
        << "a forestwalker hiding in trees shows no mini HP bar";

    // Flying over the same trees is NOT concealed: the plain arm returns.
    w->set_flight_left(20);
    w->set_outline(0);
    trace_clear();
    ASSERT_TRUE(draw_walker_tile(*w, vs)) << "a flying forestwalker tile-draws";
    ASSERT_TRUE(trace_contains("hp_bar", "draw"))
        << "flight lifts the forestwalk concealment";
    w->set_flight_left(0);

    vs->control = old_control;
    delete control;
}


// walker::facing's slope ladder, plus the NPC blocked-step fallback arms that
// a bare "0 <= facing < 8" range check cannot tell from a no-op: each arm turns
// to a named facing, walks ONE full stepsize that way, reports that walk's
// result and restores the original curdir.
//
// The slope ladder exists TWICE in src/: living::facing (src/gameplay/living.cpp)
// is a byte-for-byte copy of walker::facing (src/gameplay/walker_movement.cpp),
// and a living never reaches the base one. Weapons, effects and every other
// non-living walker do, so the table below runs through both.
TEST(WalkerMovement, facing_buckets_and_npc_fallback_component_walks)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";

    // slope = y*1000/x, cut at +-2414 and +-414, with a separate ladder per
    // sign of x. Ten vectors, one per rung.
    struct { short x; short y; int expected; } vectors[] = {
        {1, 3, FACE_DOWN},        {2, 1, FACE_DOWN_RIGHT},
        {3, 0, FACE_RIGHT},       {2, -1, FACE_UP_RIGHT},
        {1, -3, FACE_UP},         {-1, 3, FACE_DOWN},
        {-2, 1, FACE_DOWN_LEFT},  {-3, 0, FACE_LEFT},
        {-2, -1, FACE_UP_LEFT},   {-1, -3, FACE_UP},
    };
    for (auto& v : vectors) {
        ASSERT_EQ(v.expected, (int)w->facing(v.x, v.y))
            << "living::facing(" << v.x << "," << v.y << ") slope bucket";
    }

    // The same ten rungs through the base rule, which only a NON-living walker
    // reaches: make_guy always yields a living, whose override shadows it.
    {
        PixieData px(1, 1, 1, new unsigned char[1]{0});
        walker nonliving(px);
        for (auto& v : vectors) {
            ASSERT_EQ(v.expected, (int)nonliving.facing(v.x, v.y))
                << "walker::facing(" << v.x << "," << v.y
                << ") slope bucket (the non-living twin of the rule above)";
        }
    }

    // An npc's blocked step takes the fallback switch. The arm is reached only
    // when curdir already equals facing(x,y) (otherwise walk() turns in place
    // and reports success) AND both the full step and the baby step fail.
    w->set_user(-1);
    w->set_stepsize(2.0f);

    // FACE_LEFT off the west edge -> FACE_DOWN, a full step south.
    w->setxy(0, 0);
    w->set_curdir(FACE_LEFT);
    ASSERT_TRUE(w->walkstep(-1, 0)) << "the FACE_LEFT fallback finds a way south";
    ASSERT_EQ(0, w->xpos()) << "the blocked axis does not move";
    ASSERT_EQ(2, w->ypos()) << "the FACE_LEFT fallback walks +stepsize south";
    ASSERT_EQ(FACE_LEFT, (int)w->curdir()) << "walkstep restores oldcurdir";

    // FACE_UP in the north-west corner: its fallback (FACE_LEFT) is off-map too.
    w->setxy(0, 0);
    w->set_curdir(FACE_UP);
    ASSERT_FALSE(w->walkstep(0, -1)) << "step and fallback are both off-map";
    ASSERT_EQ(0, w->xpos()) << "a doubly blocked npc step moves nothing";
    ASSERT_EQ(0, w->ypos()) << "a doubly blocked npc step moves nothing";

    // The diagonal arms walk the two components separately and return
    // ret1||ret2. FACE_UP_LEFT against the west edge: only FACE_UP carries.
    w->setxy(0, 4);
    w->set_curdir(FACE_UP_LEFT);
    ASSERT_TRUE(w->walkstep(-1, -1))
        << "the FACE_UP component of the diagonal arm walks";
    ASSERT_EQ(0, w->xpos()) << "the FACE_LEFT component stays blocked";
    ASSERT_EQ(2, w->ypos()) << "the vertical component walks a full stepsize";
    ASSERT_EQ(FACE_UP_LEFT, (int)w->curdir()) << "walkstep restores oldcurdir";

    // The far corner. A body whose right/bottom edge already touches
    // pixmaxx/pixmaxy can never step further east or south (query_grid_passable
    // refuses the overhang), so both of those cardinals reach their fallback.
    const short right_edge =
        static_cast<short>(movement_world().pixmaxx - w->sizex() - 1);
    const short bottom_edge =
        static_cast<short>(movement_world().pixmaxy - w->sizey() - 1);

    w->setxy(right_edge, bottom_edge);
    w->set_curdir(FACE_RIGHT);
    ASSERT_TRUE(w->walkstep(1, 0)) << "the FACE_RIGHT fallback finds a way north";
    ASSERT_EQ(right_edge, w->xpos()) << "the blocked axis does not move";
    ASSERT_EQ(bottom_edge - 2, w->ypos())
        << "the FACE_RIGHT fallback walks -stepsize north";
    ASSERT_EQ(FACE_RIGHT, (int)w->curdir()) << "walkstep restores oldcurdir";

    w->setxy(right_edge, bottom_edge);
    w->set_curdir(FACE_DOWN);
    ASSERT_FALSE(w->walkstep(0, 1))
        << "the FACE_DOWN fallback walks east, which is blocked here too";
    ASSERT_EQ(right_edge, w->xpos()) << "nothing moves when the fallback fails";
    ASSERT_EQ(bottom_edge, w->ypos()) << "nothing moves when the fallback fails";

    // turn() from an out-of-range curdir keeps the classic modulo result:
    // distance 99 - 0 >= 4 turns clockwise, (99 + 1) % 8 == FACE_DOWN.
    w->set_curdir(99);
    ASSERT_TRUE(w->turn(FACE_UP)) << "turn reports done";
    ASSERT_EQ(FACE_DOWN, (int)w->curdir()) << "(99 + 1) % 8 == FACE_DOWN";
}


// The eight NPC fallback arms of walker::walkstep, and the user path that never
// reaches them. Every call here is set up with curdir == facing(dx,dy) so the
// blocked-step arms really run: with the two out of step, living::walk turns in
// place and reports success, which is what this test used to assert by accident.
TEST(WalkerMovement, npc_blocked_step_fallback_arms_and_the_user_slide)
{
    fresh_grass_map();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";
    w->set_user(-1);
    w->set_stepsize(2.0f);

    const short right_edge =
        static_cast<short>(movement_world().pixmaxx - w->sizex() - 1);
    const short bottom_edge =
        static_cast<short>(movement_world().pixmaxy - w->sizey() - 1);
    const short mid = static_cast<short>(GRID_SIZE * 6);

    // The four cardinal arms, each on the edge that blocks the wanted
    // direction: LEFT->DOWN, UP->LEFT, RIGHT->UP, DOWN->RIGHT. Each returns
    // the fallback walk's own result and leaves the walker one stepsize along
    // the fallback direction.
    struct Case {
        const char* name;
        short x, y, dx, dy;
        short want_x, want_y;
    } cases[] = {
        {"FACE_LEFT falls back to FACE_DOWN",
         0, mid, -1, 0, 0, static_cast<short>(mid + 2)},
        {"FACE_UP falls back to FACE_LEFT",
         mid, 0, 0, -1, static_cast<short>(mid - 2), 0},
        {"FACE_RIGHT falls back to FACE_UP",
         right_edge, mid, 1, 0, right_edge, static_cast<short>(mid - 2)},
        {"FACE_DOWN falls back to FACE_RIGHT",
         mid, bottom_edge, 0, 1, static_cast<short>(mid + 2), bottom_edge},
    };
    for (auto& c : cases) {
        w->setxy(c.x, c.y);
        const short dir = w->facing(c.dx, c.dy);
        w->set_curdir(static_cast<signed char>(dir));
        ASSERT_TRUE(w->walkstep(c.dx, c.dy)) << c.name << ": the fallback walks";
        ASSERT_EQ(c.want_x, w->xpos()) << c.name << ": x after the fallback";
        ASSERT_EQ(c.want_y, w->ypos()) << c.name << ": y after the fallback";
        ASSERT_EQ((int)dir, (int)w->curdir()) << c.name << ": oldcurdir restored";
        ASSERT_FLOAT_EQ(c.dx * 2.0f, w->lastx()) << c.name << ": lastx = x*stepsize";
        ASSERT_FLOAT_EQ(c.dy * 2.0f, w->lasty()) << c.name << ": lasty = y*stepsize";
    }

    // The diagonal arms walk both components and return ret1||ret2.
    w->setxy(0, 0);
    w->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(w->walkstep(-1, -1))
        << "both components of FACE_UP_LEFT are off-map in the corner";
    ASSERT_EQ(0, w->xpos()) << "nothing moves";
    ASSERT_EQ(0, w->ypos()) << "nothing moves";
    ASSERT_EQ(FACE_UP_LEFT, (int)w->curdir()) << "oldcurdir restored";

    w->setxy(0, 0);
    w->set_curdir(FACE_UP_RIGHT);
    ASSERT_TRUE(w->walkstep(1, -1))
        << "the FACE_RIGHT component carries the FACE_UP_RIGHT diagonal";
    ASSERT_EQ(2, w->xpos()) << "the horizontal component walks a full stepsize";
    ASSERT_EQ(0, w->ypos()) << "the FACE_UP component is off-map";
    ASSERT_EQ(FACE_UP_RIGHT, (int)w->curdir()) << "oldcurdir restored";

    // A user never reaches that switch: a blocked diagonal slides along the
    // free axis one pixel per stepsize unit and STILL returns false, because
    // the slide arm never sets ret1/ret2.
    w->set_user(0);
    w->setxy(48, 0);
    w->set_curdir(FACE_UP_RIGHT);
    ASSERT_FALSE(w->walkstep(1, -1)) << "the user slide reports failure";
    ASSERT_EQ(50, w->xpos()) << "two pixels of eastward slide (stepsize 2)";
    ASSERT_EQ(0, w->ypos()) << "the blocked vertical axis holds";
    ASSERT_EQ(FACE_UP_RIGHT, (int)w->curdir()) << "the slide restores oldcurdir";

    w->setxy(0, 48);
    w->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(w->walkstep(-1, -1)) << "the user slide reports failure";
    ASSERT_EQ(0, w->xpos()) << "the blocked horizontal axis holds";
    ASSERT_EQ(46, w->ypos()) << "two pixels of northward slide (stepsize 2)";
    ASSERT_EQ(FACE_UP_LEFT, (int)w->curdir()) << "the slide restores oldcurdir";
}


namespace {
class ScriptedWalkWalker : public walker {
public:
    explicit ScriptedWalkWalker(const PixieData& p) : walker(p) {}

    void set_walk_results(std::initializer_list<bool> vals)
    {
        results_ = std::deque<bool>(vals.begin(), vals.end());
    }
    void set_walk_result_sequence(const std::vector<bool>& vals)
    {
        results_ = std::deque<bool>(vals.begin(), vals.end());
    }

    bool walk(float x, float y) override
    {
        calls_.push_back({x, y});
        call_dirs_.push_back(static_cast<short>(curdir()));
        if (results_.empty())
            return false;
        const bool r = results_.front();
        results_.pop_front();
        return r;
    }

    std::size_t call_count() const { return calls_.size(); }
    const std::vector<std::pair<float, float>>& calls() const { return calls_; }
    // The facing walkstep had set when each walk() attempt was made: the NPC
    // fallback arms turn before they walk, and that turn is invisible in the
    // deltas alone.
    const std::vector<short>& call_dirs() const { return call_dirs_; }
    void clear_calls() { calls_.clear(); call_dirs_.clear(); }
    void set_forced_facing(short dir)
    {
        forced_facing_ = dir;
        use_forced_facing_ = true;
    }
    void clear_forced_facing() { use_forced_facing_ = false; }

    short facing(short x, short y) override
    {
        if (use_forced_facing_)
            return forced_facing_;
        return walker::facing(x, y);
    }

private:
    std::deque<bool> results_;
    std::vector<std::pair<float, float>> calls_;
    std::vector<short> call_dirs_;
    short forced_facing_ = FACE_UP;
    bool use_forced_facing_ = false;
};

static PixieData one_px_for_scripted()
{
    return PixieData(1, 1, 1, new unsigned char[1]{0});
}
} // namespace

// Every arm of walkstep's NPC fallback switch, pinned by the exact walk()
// attempts it makes: the full step, the one-unit baby step, and then the
// fallback(s) - each with the facing the arm turned to first. A cardinal arm
// turns 90 degrees clockwise-of-blocked (UP->LEFT, RIGHT->UP, DOWN->RIGHT,
// LEFT->DOWN) and walks one stepsize that way; a diagonal arm walks its two
// components separately, vertical first, and returns ret1 || ret2.
TEST(WalkerMovement, round6_scripted_walkstep_switch_coverage)
{
    PixieData px = one_px_for_scripted();
    ScriptedWalkWalker w(px);
    w.set_stepsize(1.0f);
    w.set_user(-1);

    // A facing no arm below ever turns to, so every recorded facing that is
    // not the sentinel was written by the arm under test.
    const short kSentinel = FACE_DOWN_RIGHT;

    struct Attempt { float x; float y; int dir; };
    struct Row {
        const char* name;
        float dx, dy;
        std::vector<bool> results;
        bool expected_return;
        std::vector<Attempt> attempts;
    };
    const std::vector<Row> rows = {
        {"FACE_UP falls back to a full step FACE_LEFT",
         0, -1, {false, false, true}, true,
         {{0, -1, kSentinel}, {0, -1, kSentinel}, {-1, 0, FACE_LEFT}}},
        {"FACE_RIGHT falls back to a full step FACE_UP",
         1, 0, {false, false, true}, true,
         {{1, 0, kSentinel}, {1, 0, kSentinel}, {0, -1, FACE_UP}}},
        {"FACE_DOWN falls back to a full step FACE_RIGHT",
         0, 1, {false, false, true}, true,
         {{0, 1, kSentinel}, {0, 1, kSentinel}, {1, 0, FACE_RIGHT}}},
        {"FACE_LEFT falls back to a full step FACE_DOWN",
         -1, 0, {false, false, true}, true,
         {{-1, 0, kSentinel}, {-1, 0, kSentinel}, {0, 1, FACE_DOWN}}},
        {"FACE_UP_RIGHT walks UP then RIGHT and returns ret2",
         1, -1, {false, false, false, true}, true,
         {{1, -1, kSentinel}, {1, -1, kSentinel},
          {0, -1, FACE_UP}, {1, 0, FACE_RIGHT}}},
        {"FACE_DOWN_RIGHT walks DOWN then RIGHT and returns ret1",
         1, 1, {false, false, true, false}, true,
         {{1, 1, kSentinel}, {1, 1, kSentinel},
          {0, 1, FACE_DOWN}, {1, 0, FACE_RIGHT}}},
        {"FACE_DOWN_LEFT walks DOWN then LEFT and returns ret2",
         -1, 1, {false, false, false, true}, true,
         {{-1, 1, kSentinel}, {-1, 1, kSentinel},
          {0, 1, FACE_DOWN}, {-1, 0, FACE_LEFT}}},
        {"FACE_UP_LEFT walks UP then LEFT and returns ret1",
         -1, -1, {false, false, true, false}, true,
         {{-1, -1, kSentinel}, {-1, -1, kSentinel},
          {0, -1, FACE_UP}, {-1, 0, FACE_LEFT}}},
        {"a diagonal arm whose two components both fail returns false",
         1, -1, {false, false, false, false}, false,
         {{1, -1, kSentinel}, {1, -1, kSentinel},
          {0, -1, FACE_UP}, {1, 0, FACE_RIGHT}}},
    };

    for (const auto& row : rows)
    {
        w.set_curdir(static_cast<signed char>(kSentinel));
        w.set_walk_result_sequence(row.results);
        w.clear_calls();
        ASSERT_EQ(row.expected_return, w.walkstep(row.dx, row.dy))
            << row.name << ": walkstep returns ret1 || ret2";
        ASSERT_EQ(row.attempts.size(), w.calls().size())
            << row.name << ": exact number of walk attempts";
        for (std::size_t i = 0; i < row.attempts.size(); ++i)
        {
            EXPECT_FLOAT_EQ(row.attempts[i].x, w.calls()[i].first)
                << row.name << ": attempt " << i << " dx";
            EXPECT_FLOAT_EQ(row.attempts[i].y, w.calls()[i].second)
                << row.name << ": attempt " << i << " dy";
            EXPECT_EQ(row.attempts[i].dir, (int)w.call_dirs()[i])
                << row.name << ": attempt " << i << " was walked facing";
        }
        ASSERT_EQ(kSentinel, (int)w.curdir())
            << row.name << ": walkstep restores oldcurdir";
    }

    // A user never reaches that switch: the cardinal slide arm breaks with
    // dx == dy == 0, so only the two ordinary attempts are made.
    w.set_user(0);
    w.set_curdir(static_cast<signed char>(kSentinel));
    w.set_walk_results({false, false});
    w.clear_calls();
    ASSERT_FALSE(w.walkstep(0, -1)) << "a user's blocked cardinal step fails";
    ASSERT_EQ(2u, w.calls().size())
        << "the user cardinal arm makes no fallback walk at all";
    ASSERT_EQ(kSentinel, (int)w.call_dirs()[0]) << "the full step keeps our facing";
    ASSERT_EQ(kSentinel, (int)w.call_dirs()[1]) << "the baby step keeps our facing";
}


// walkstep's pre-switch contract, which "returns false" alone cannot see:
// lastx/lasty are stored as x*stepsize BEFORE anything walks, then the full
// step AND the one-unit baby step are both attempted, and only then does the
// user-slide switch run. A cardinal facing breaks out of that switch and an
// out-of-range facing takes its default arm; both leave dx/dy at zero, so the
// walker does not slide and walkstep returns false with oldcurdir restored.
TEST(WalkerMovement, user_blocked_step_tries_full_then_baby_step_before_sliding)
{
    PixieData px = one_px_for_scripted();
    ScriptedWalkWalker w(px);
    w.set_stepsize(3.0f);
    w.set_user(0);
    w.setxy(120, 120);

    // Cardinal facing: the switch breaks with dx == dy == 0.
    w.set_forced_facing(FACE_UP);
    w.set_curdir(FACE_DOWN);
    w.set_walk_results({false, false});
    w.clear_calls();
    ASSERT_FALSE(w.walkstep(0, -1)) << "a blocked user cardinal step fails";
    ASSERT_EQ(2u, w.calls().size())
        << "exactly two walk attempts: the full step and the baby step";
    ASSERT_FLOAT_EQ(0.0f, w.calls()[0].first) << "the full step keeps x at 0";
    ASSERT_FLOAT_EQ(-3.0f, w.calls()[0].second) << "the full step is y*stepsize";
    ASSERT_FLOAT_EQ(0.0f, w.calls()[1].first) << "the baby step keeps x at 0";
    ASSERT_FLOAT_EQ(-1.0f, w.calls()[1].second)
        << "the baby step is one unit, not one stepsize";
    ASSERT_FLOAT_EQ(0.0f, w.lastx()) << "lastx = x*stepsize, stored up front";
    ASSERT_FLOAT_EQ(-3.0f, w.lasty()) << "lasty = y*stepsize, stored up front";
    ASSERT_EQ(FACE_DOWN, (int)w.curdir()) << "walkstep restores oldcurdir";
    ASSERT_EQ(120, w.xpos()) << "a cardinal slide moves nothing";
    ASSERT_EQ(120, w.ypos()) << "a cardinal slide moves nothing";

    // Out-of-range facing: the default arm re-zeroes ret1/ret2 and dx/dy stay 0.
    w.set_forced_facing(99);
    w.set_curdir(FACE_LEFT);
    w.set_walk_results({false, false});
    w.clear_calls();
    ASSERT_FALSE(w.walkstep(0, -1)) << "an out-of-range facing takes the default arm";
    ASSERT_EQ(2u, w.calls().size()) << "the two walk attempts still ran";
    ASSERT_FLOAT_EQ(-3.0f, w.calls()[0].second) << "the full step is y*stepsize";
    ASSERT_FLOAT_EQ(-1.0f, w.calls()[1].second) << "then the baby step";
    ASSERT_EQ(FACE_LEFT, (int)w.curdir()) << "walkstep restores oldcurdir";
    ASSERT_EQ(120, w.xpos()) << "the default arm never slides";
    ASSERT_EQ(120, w.ypos()) << "the default arm never slides";
}


TEST(WalkerMovement, walker_get_current_angle_all_direction_cases)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_NE(nullptr, w) << "soldier walker created";

    // get_current_angle maps each facing to one exact radian value. The ladder
    // runs clockwise from FACE_RIGHT == 0 and deliberately does NOT wrap at pi:
    // FACE_UP_LEFT is 5*pi/4, not -3*pi/4.
    struct { int dir; float expected; const char* name; } cases[] = {
        {FACE_UP,         -static_cast<float>(M_PI_2),     "FACE_UP"},
        {FACE_UP_RIGHT,   -static_cast<float>(M_PI_4),     "FACE_UP_RIGHT"},
        {FACE_RIGHT,       0.0f,                           "FACE_RIGHT"},
        {FACE_DOWN_RIGHT,  static_cast<float>(M_PI_4),     "FACE_DOWN_RIGHT"},
        {FACE_DOWN,        static_cast<float>(M_PI_2),     "FACE_DOWN"},
        {FACE_DOWN_LEFT,   static_cast<float>(3 * M_PI_4), "FACE_DOWN_LEFT"},
        {FACE_LEFT,        static_cast<float>(M_PI),       "FACE_LEFT"},
        {FACE_UP_LEFT,     static_cast<float>(5 * M_PI_4), "FACE_UP_LEFT"},
    };
    for (auto& c : cases) {
        w->set_curdir(static_cast<signed char>(c.dir));
        EXPECT_NEAR(c.expected, w->get_current_angle(), 1e-4f)
            << c.name << " has exactly one angle";
    }

    // Out-of-range facings take the default arm.
    w->set_curdir(static_cast<char>(99));
    EXPECT_FLOAT_EQ(0.0f, w->get_current_angle())
        << "an out-of-range facing angles 0";
    w->set_curdir(static_cast<char>(-1));
    EXPECT_FLOAT_EQ(0.0f, w->get_current_angle())
        << "a negative facing angles 0";
}
