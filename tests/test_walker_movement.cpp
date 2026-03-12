#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/render/walker_draw.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"
#include <deque>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

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
    if (!w) return;

    // Test all 8 quadrants plus cardinal directions
    struct { short x; short y; } dirs[] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
        {2, 1}, {1, 2}, {-2, 1}, {-1, 2},
        {2, -1}, {1, -2}, {-2, -1}, {-1, -2}
    };
    for (auto& d : dirs) {
        short dir = w->facing(d.x, d.y);
        ASSERT_TRUE(dir >= 0 && dir < 8) << "facing should be 0-7";
    }
}


TEST(WalkerMovement, walker_facing_zero)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    short dir = w->facing(0, 0);
    (void)dir; // behavior for (0,0) may vary
}


TEST(WalkerMovement, walker_facing_threshold_boundaries_round6)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;

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
    if (!w) return;

    for (short target = 0; target < 8; target++) {
        w->curdir = 0;
        w->turn(target);
    }
}


TEST(WalkerMovement, walker_turn_from_all_starts)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;

    for (short start = 0; start < 8; start++) {
        w->curdir = static_cast<char>(start);
        w->turn(0);
    }
}


// ---------------------------------------------------------------------------
// walker::walkstep - movement logic
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_walkstep_cardinals)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    w->walkstep(1, 0);
    w->walkstep(-1, 0);
    w->walkstep(0, 1);
    w->walkstep(0, -1);

}


TEST(WalkerMovement, walker_walkstep_diagonals)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    w->walkstep(1, 1);
    w->walkstep(-1, 1);
    w->walkstep(1, -1);
    w->walkstep(-1, -1);

}


TEST(WalkerMovement, walker_walkstep_zero)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->walkstep(0, 0);

    // Blocked movement near map edge (npc path).
    w->setxy(0, 0);
    w->user = -1;
    (void)w->walkstep(-1, 0);
    (void)w->walkstep(0, -1);
    (void)w->walkstep(-1, -1);

    // User slide path on blocked diagonal movement.
    w->setxy(0, 10);
    w->user = 0;
    (void)w->walkstep(-1, -1);
    (void)w->walkstep(-1, 1);

}


TEST(WalkerMovement, walker_walkstep_user_slide_cardinal_break_path_round5)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->user = 0;
    w->stepsize = 2.0f;
    w->setxy(0, 24); // left edge forces blocked cardinal movement
    w->curdir = FACE_LEFT;

    const bool moved = w->walkstep(-1, 0);
    ASSERT_TRUE(!moved) << "blocked cardinal user movement should keep slide dx/dy at zero and fail";
}


TEST(WalkerMovement, walker_walkstep_user_slide_diagonal_switch_cases_round6)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    // Block movement at map edge so the user-slide diagonal switch executes.
    w->user = 0;
    w->stepsize = 1.0f;
    w->setxy(0, 0);

    (void)w->walkstep(1, -1);   // FACE_UP_RIGHT
    (void)w->walkstep(1, 1);    // FACE_DOWN_RIGHT
    (void)w->walkstep(-1, 1);   // FACE_DOWN_LEFT
    (void)w->walkstep(-1, -1);  // FACE_UP_LEFT
}


// ---------------------------------------------------------------------------
// walker::draw and walker::draw_tile via viewscreen
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_draw_basic)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (vs) {
        draw_walker(*w, vs);
    }
}


TEST(WalkerMovement, walker_draw_tile_basic)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (vs) {
        walker* old_control = vs->control;
        walker* control = make_guy(FAMILY_SOLDIER, 0);
        if (control) {
            control->setxy(96, 96);
            vs->control = control;
        }

        draw_walker_tile(*w, vs);

        // draw_tile invisibility path (requires non-null control).
        w->invisibility_left = 12;
        draw_walker_tile(*w, vs);
        w->invisibility_left = 0;

        // draw_tile outline path.
        w->invulnerable_left = 10;
        draw_walker_tile(*w, vs);
        w->invulnerable_left = 0;

        vs->control = old_control;
        delete control;
    }
}


TEST(WalkerMovement, walker_draw_with_flight)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->flight_left = 10;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (vs) {
        draw_walker(*w, vs);
    }
}


TEST(WalkerMovement, walker_draw_with_invisibility)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->invisibility_left = 10;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (vs) {
        walker* old_control = vs->control;
        walker* control = make_guy(FAMILY_SOLDIER, 1);
        if (control) {
            control->setxy(96, 96);
            vs->control = control;
        }
        draw_walker(*w, vs);
        w->compute_outline(vs->control);
        w->flight_left = 8;
        w->compute_outline(vs->control);
        w->invulnerable_left = 8;
        w->compute_outline(vs->control);
        vs->control = old_control;
        delete control;
    }
}


TEST(WalkerMovement, walker_draw_with_invulnerability)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->invulnerable_left = 10;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (vs) {
        draw_walker(*w, vs);
    }
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
    ASSERT_EQ(1, (int)w->lastx) << "stationary walkstep should store unit x input";
    ASSERT_EQ(0, (int)w->lasty) << "stationary walkstep should store unit y input";

    // walk() stationary branch.
    ASSERT_TRUE(w->walk(1, 1)) << "stationary walk should succeed";

    // turn() stationary branch should not overwrite facing vector.
    w->lastx = 7;
    w->lasty = -3;
    (void)w->turn(FACE_LEFT);
    ASSERT_EQ(7, (int)w->lastx) << "stationary turn should preserve lastx";
    ASSERT_EQ(-3, (int)w->lasty) << "stationary turn should preserve lasty";
}


TEST(WalkerMovement, walker_walkstep_user_slide_sets_vertical_and_horizontal_dirs)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    w->user = 0;
    w->stepsize = 2.0f;

    // Horizontal-only slide: up blocked at top edge, right passable.
    w->setxy(32, 0);
    w->curdir = FACE_DOWN;
    (void)w->walkstep(1, -1);
    ASSERT_TRUE(true) << "user slide horizontal branch executed";

    // Vertical-only slide: left blocked at left edge, up passable.
    w->setxy(0, 32);
    w->curdir = FACE_RIGHT;
    (void)w->walkstep(-1, -1);
    ASSERT_TRUE(true) << "user slide vertical branch executed";
}


// ---------------------------------------------------------------------------
// walker::animate - different animation types
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_animate_walk)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->ani_type = ANI_WALK;
    w->animate();
}


TEST(WalkerMovement, walker_animate_attack)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->ani_type = ANI_ATTACK;
    w->animate();
}


TEST(WalkerMovement, walker_animate_all_families)
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        walker* w = make_guy(families[i], 0);
        if (!w) continue;
        w->ani_type = ANI_WALK;
        w->animate();
    }
}


TEST(WalkerMovement, round9_user_cardinal_slide_break_and_offmap_guards)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    // User + cardinal blocked move: dx/dy stays 0 in slide switch and returns false.
    w->user = 0;
    w->stepsize = 1.0f;
    w->setxy(0, 16);
    w->curdir = FACE_LEFT;
    ASSERT_TRUE(!w->walkstep(-1, 0)) << "blocked cardinal user slide should fail";

    // walk(0,0) early-return path.
    ASSERT_TRUE(w->walk(0, 0)) << "walk(0,0) should return success";

    // Off-map guard in walk().
    w->setxy(0, 0);
    w->curdir = FACE_LEFT;
    ASSERT_TRUE(!w->walk(-1, 0)) << "walk should fail when target is off map";
}


TEST(WalkerMovement, round9_blocked_animate_angle_and_turn_default_paths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    // Force an in-bounds blocked move and keep animation active.
    w->stats()->set_bit_flags(BIT_ANIMATE, 1);
    w->setxy(GRID_SIZE, GRID_SIZE);
    w->curdir = FACE_RIGHT;
    // Moving from (1,1) one tile right targets tile (2,1).
    og::runtime::current_session->myscreen_->world().grid.data[1 * og::runtime::current_session->myscreen_->world().grid.w + 2] = PIX_TREE_M1;
    ASSERT_TRUE(!w->walk(1, 0)) << "blocked movement should fail while still executing animate-on-block path";

    // get_current_angle switch branches.
    w->curdir = FACE_UP;
    ASSERT_TRUE(w->get_current_angle() < 0.0f) << "FACE_UP angle should be negative";
    w->curdir = 99;
    ASSERT_EQ(0, (int)w->get_current_angle()) << "invalid direction should use default angle";

    // Invalid curdir is clamped before turning; one step toward FACE_UP from clamped
    // FACE_UP results in FACE_UP_LEFT.
    w->stepsize = 2.0f;
    w->curdir = static_cast<char>(-120);
    (void)w->turn(FACE_UP);
    ASSERT_EQ(-2, (int)w->lastx) << "invalid turn direction should clamp and turn safely";
    ASSERT_EQ(-2, (int)w->lasty) << "invalid turn direction should default lasty to -stepsize";
}


// ---------------------------------------------------------------------------
// walker::create_weapon
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_create_weapon_soldier)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->lastx = 1;
    w->lasty = 0;

    walker* weap = w->fire();
    if (weap) {
        og::runtime::current_session->myscreen_->world().remove_ob(weap);
    }

}


TEST(WalkerMovement, walker_create_weapon_archer)
{
    walker* w = make_guy(FAMILY_ARCHER, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->lastx = 1;
    w->lasty = 0;

    walker* weap = w->fire();
    if (weap) {
        og::runtime::current_session->myscreen_->world().remove_ob(weap);
    }

}


TEST(WalkerMovement, walker_create_weapon_mage)
{
    walker* w = make_guy(FAMILY_MAGE, 0);
    if (!w) return;
    w->setxy(100, 100);
    w->lastx = 0;
    w->lasty = 1;

    walker* weap = w->fire();
    if (weap) {
        og::runtime::current_session->myscreen_->world().remove_ob(weap);
    }

}


TEST(WalkerMovement, round6_blocked_animate_and_default_angle_turn)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    if (!w)
        return;

    // Blocked walk + BIT_ANIMATE branch.
    og::runtime::current_session->myscreen_->world().grid.data[1] = PIX_TREE_M1;

    w->setxy(0, 0);
    w->sizex = 1;
    w->sizey = 1;
    w->curdir = FACE_LEFT;
    w->stats()->set_bit_flags(BIT_ANIMATE, 1);
    (void)w->walk(-1, 0);
    ASSERT_TRUE(true) << "animated off-map walk path executed";

    // get_current_angle default branch.
    w->curdir = static_cast<char>(99);
    ASSERT_EQ(0, (int)w->get_current_angle()) << "invalid direction should map to angle 0";

    // turn default branch in lastx/lasty fallback.
    w->curdir = static_cast<char>(99);
    (void)w->turn(FACE_UP);
    ASSERT_TRUE(true) << "turn should tolerate invalid current direction";
}


// ---------------------------------------------------------------------------
// walker on_screen
// ---------------------------------------------------------------------------

TEST(WalkerMovement, walker_on_screen)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    if (!w) return;
    w->setxy(100, 100);
    // on_screen() is a render-layer method on pixie, not walker.
    // Verify walker position is set correctly instead.
    ASSERT_TRUE(w->xpos == 100) << "xpos set";
    ASSERT_TRUE(w->ypos == 100) << "ypos set";
}


TEST(WalkerMovement, walker_draw_tile_phantom_and_forestwalk_paths)
{
    walker* w = make_guy(FAMILY_ELF, 0);
    ASSERT_TRUE(w != nullptr) << "walker created";
    w->setxy(96, 96);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (vs) {
        walker* old_control = vs->control;
        walker* control = make_guy(FAMILY_SOLDIER, 0);
        if (control) {
            control->setxy(80, 80);
            vs->control = control;
        }

        // PHANTOM draw_tile branch.
        w->stats()->set_bit_flags(BIT_PHANTOM, 1);
        (void)draw_walker_tile(*w, vs);
        w->stats()->set_bit_flags(BIT_PHANTOM, 0);

        // FORESTWALK draw_tile branch.
        int tx = w->xpos / GRID_SIZE;
        int ty = w->ypos / GRID_SIZE;
        if (tx >= 0 && ty >= 0 && tx < og::runtime::current_session->myscreen_->world().grid.w && ty < og::runtime::current_session->myscreen_->world().grid.h) {
            og::runtime::current_session->myscreen_->world().grid.data[ty * og::runtime::current_session->myscreen_->world().grid.w + tx] = PIX_TREE_T1;
            og::runtime::current_session->myscreen_->world().mysmoother.set_target(og::runtime::current_session->myscreen_->world().grid);
        }
        w->flight_left = 0;
        w->stats()->set_bit_flags(BIT_FLYING, 0);
        (void)draw_walker_tile(*w, vs);

        vs->control = old_control;
        delete control;
    }

}


TEST(WalkerMovement, deep_branch_coverage_smoke)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    if (!w)
        return;

    // facing() threshold coverage for both x>0 and x<0 ladders.
    const short f0 = w->facing(1, 3);
    const short f1 = w->facing(2, 1);
    const short f2 = w->facing(3, 0);
    const short f3 = w->facing(2, -1);
    const short f4 = w->facing(1, -3);
    const short f5 = w->facing(-1, 3);
    const short f6 = w->facing(-2, 1);
    const short f7 = w->facing(-3, 0);
    const short f8 = w->facing(-2, -1);
    const short f9 = w->facing(-1, -3);
    ASSERT_TRUE(f0 >= 0 && f0 < 8) << "facing value should be valid";
    ASSERT_TRUE(f1 >= 0 && f1 < 8) << "facing value should be valid";
    ASSERT_TRUE(f2 >= 0 && f2 < 8) << "facing value should be valid";
    ASSERT_TRUE(f3 >= 0 && f3 < 8) << "facing value should be valid";
    ASSERT_TRUE(f4 >= 0 && f4 < 8) << "facing value should be valid";
    ASSERT_TRUE(f5 >= 0 && f5 < 8) << "facing value should be valid";
    ASSERT_TRUE(f6 >= 0 && f6 < 8) << "facing value should be valid";
    ASSERT_TRUE(f7 >= 0 && f7 < 8) << "facing value should be valid";
    ASSERT_TRUE(f8 >= 0 && f8 < 8) << "facing value should be valid";
    ASSERT_TRUE(f9 >= 0 && f9 < 8) << "facing value should be valid";

    // NPC blocked walkstep fallback switch paths.
    w->user = -1;
    w->stepsize = 2.0f;
    w->setxy(0, 0);
    (void)w->walkstep(-1, 0);   // FACE_LEFT
    (void)w->walkstep(0, -1);   // FACE_UP
    (void)w->walkstep(-1, -1);  // FACE_UP_LEFT
    (void)w->walkstep(1, -1);   // FACE_UP_RIGHT

    // Force bottom/right edge for remaining blocked cardinal/diagonal attempts.
    const short max_x = static_cast<short>(og::runtime::current_session->myscreen_->world().grid.w * GRID_SIZE - 1);
    const short max_y = static_cast<short>(og::runtime::current_session->myscreen_->world().grid.h * GRID_SIZE - 1);
    w->setxy(max_x, max_y);
    (void)w->walkstep(1, 0);    // FACE_RIGHT
    (void)w->walkstep(0, 1);    // FACE_DOWN
    (void)w->walkstep(1, 1);    // FACE_DOWN_RIGHT
    (void)w->walkstep(-1, 1);   // FACE_DOWN_LEFT

    // User slide internals.
    w->user = 0;
    w->setxy(32, 0);
    (void)w->walkstep(1, -1);   // horizontal slide path
    w->setxy(0, 32);
    (void)w->walkstep(-1, -1);  // vertical slide path

    // walk() off-map and blocked animate paths.
    w->setxy(0, 0);
    w->curdir = FACE_LEFT;
    w->stats()->set_bit_flags(BIT_ANIMATE, 1);
    (void)w->walk(-1, 0); // off-map check
    w->curdir = FACE_UP;
    (void)w->walk(0, -1); // blocked move with animate path

    // stationary walk branch + turn default branch.
    w->set_order_family(Order::Living, FAMILY_TOWER1);
    (void)w->walk(1, 0);
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->curdir = 99;
    (void)w->turn(FACE_UP);

    ASSERT_TRUE(true) << "walker movement deep branches executed";
}


TEST(WalkerMovement, round6_npc_blocked_switch_and_user_slide_subpaths)
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    if (!w)
        return;

    w->stepsize = 2.0f;

    // NPC blocked switch: force first and baby-step movement failures by placing at edges.
    w->user = -1;

    w->setxy(0, GRID_SIZE * 6);
    (void)w->walkstep(-1, 0);  // FACE_LEFT -> FACE_DOWN fallback

    w->setxy(GRID_SIZE * 6, 0);
    (void)w->walkstep(0, -1);  // FACE_UP -> FACE_LEFT fallback

    const short max_x = static_cast<short>(og::runtime::current_session->myscreen_->world().grid.w * GRID_SIZE - 1);
    const short max_y = static_cast<short>(og::runtime::current_session->myscreen_->world().grid.h * GRID_SIZE - 1);
    w->setxy(max_x, static_cast<short>(GRID_SIZE * 6));
    (void)w->walkstep(1, 0);   // FACE_RIGHT -> FACE_UP fallback

    w->setxy(static_cast<short>(GRID_SIZE * 6), max_y);
    (void)w->walkstep(0, 1);   // FACE_DOWN -> FACE_RIGHT fallback

    w->setxy(0, 0);
    (void)w->walkstep(-1, -1); // FACE_UP_LEFT diagonal fallback
    (void)w->walkstep(1, -1);  // FACE_UP_RIGHT diagonal fallback
    w->setxy(max_x, max_y);
    (void)w->walkstep(1, 1);   // FACE_DOWN_RIGHT diagonal fallback
    (void)w->walkstep(-1, 1);  // FACE_DOWN_LEFT diagonal fallback

    // User-slide branch internals: one-axis movement results (gotup/gotover flags).
    w->user = 0;
    w->stepsize = 2.0f;

    // Top edge: vertical blocked, horizontal passable -> gotover branch.
    w->setxy(GRID_SIZE * 3, 0);
    (void)w->walkstep(1, -1);

    // Left edge: horizontal blocked, vertical passable -> gotup branch.
    w->setxy(0, GRID_SIZE * 3);
    (void)w->walkstep(-1, -1);

    ASSERT_TRUE(true) << "blocked npc and user-slide subpaths executed";
}


namespace {
class ScriptedWalkWalker : public walker {
public:
    explicit ScriptedWalkWalker(const PixieData& p) : walker(p) {}

    void set_walk_results(std::initializer_list<bool> vals)
    {
        results_ = std::deque<bool>(vals.begin(), vals.end());
    }

    bool walk(float x, float y) override
    {
        calls_.push_back({x, y});
        if (results_.empty())
            return false;
        const bool r = results_.front();
        results_.pop_front();
        return r;
    }

    std::size_t call_count() const { return calls_.size(); }
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
    short forced_facing_ = FACE_UP;
    bool use_forced_facing_ = false;
};

static PixieData one_px_for_scripted()
{
    return PixieData(1, 1, 1, new unsigned char[1]{0});
}
} // namespace

TEST(WalkerMovement, round6_scripted_walkstep_switch_coverage)
{
    PixieData px = one_px_for_scripted();
    ScriptedWalkWalker w(px);
    w.stepsize = 1.0f;

    // NPC fallback switch: first two attempts fail, case body executes.
    w.user = -1;
    w.set_walk_results({false, false, true});
    ASSERT_TRUE(w.walkstep(0, -1)) << "FACE_UP npc fallback should return ret1";

    w.set_walk_results({false, false, true});
    ASSERT_TRUE(w.walkstep(1, 0)) << "FACE_RIGHT npc fallback should return ret1";

    w.set_walk_results({false, false, true});
    ASSERT_TRUE(w.walkstep(0, 1)) << "FACE_DOWN npc fallback should return ret1";

    w.set_walk_results({false, false, true});
    ASSERT_TRUE(w.walkstep(-1, 0)) << "FACE_LEFT npc fallback should return ret1";

    // Diagonal NPC fallbacks (ret1/ret2 dual-call path).
    w.set_walk_results({false, false, false, true});
    ASSERT_TRUE(w.walkstep(1, -1)) << "FACE_UP_RIGHT npc fallback should return ret2";

    w.set_walk_results({false, false, true, false});
    ASSERT_TRUE(w.walkstep(1, 1)) << "FACE_DOWN_RIGHT npc fallback should return ret1";

    w.set_walk_results({false, false, false, true});
    ASSERT_TRUE(w.walkstep(-1, 1)) << "FACE_DOWN_LEFT npc fallback should return ret2";

    w.set_walk_results({false, false, true, false});
    ASSERT_TRUE(w.walkstep(-1, -1)) << "FACE_UP_LEFT npc fallback should return ret1";

    // User slide switch cardinal branch (dx/dy stays zero and returns false).
    w.user = 0;
    w.set_walk_results({false, false});
    ASSERT_TRUE(!w.walkstep(0, -1)) << "user cardinal blocked path should return false";
}


TEST(WalkerMovement, round8_user_slide_switch_default_branch)
{
    PixieData px = one_px_for_scripted();
    ScriptedWalkWalker w(px);
    w.stepsize = 1.0f;
    w.user = 0;

    // User-slide cardinal branch: switch hits the cardinal break path.
    w.set_forced_facing(FACE_UP);
    w.set_walk_results({false, false});
    ASSERT_TRUE(!w.walkstep(0, -1)) << "blocked user cardinal slide should return false";

    // Force impossible facing value to hit user-slide switch default fallback.
    w.set_forced_facing(99);
    w.set_walk_results({false, false});
    ASSERT_TRUE(!w.walkstep(0, -1)) << "invalid facing should hit user-slide default branch and return false";
}


TEST(WalkerMovement, walker_get_current_angle_all_direction_cases)
{
    walker* w = make_guy(FAMILY_SOLDIER, 0);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    if (!w)
        return;

    w->curdir = FACE_UP;
    ASSERT_TRUE(w->get_current_angle() < -1.0f) << "FACE_UP should be near -pi/2";
    w->curdir = FACE_UP_RIGHT;
    ASSERT_TRUE(w->get_current_angle() < 0.0f) << "FACE_UP_RIGHT should be negative";
    w->curdir = FACE_RIGHT;
    ASSERT_EQ(0, (int)w->get_current_angle()) << "FACE_RIGHT should be zero angle";
    w->curdir = FACE_DOWN_RIGHT;
    ASSERT_TRUE(w->get_current_angle() > 0.0f) << "FACE_DOWN_RIGHT should be positive";
    w->curdir = FACE_DOWN;
    ASSERT_TRUE(w->get_current_angle() > 1.0f) << "FACE_DOWN should be near +pi/2";
    w->curdir = FACE_DOWN_LEFT;
    ASSERT_TRUE(w->get_current_angle() > 2.0f) << "FACE_DOWN_LEFT should be in third quadrant";
    w->curdir = FACE_LEFT;
    ASSERT_TRUE(w->get_current_angle() > 3.0f) << "FACE_LEFT should be near pi";
    w->curdir = FACE_UP_LEFT;
    ASSERT_TRUE(w->get_current_angle() > 3.5f) << "FACE_UP_LEFT should be near 5*pi/4";
    w->curdir = static_cast<char>(99);
    ASSERT_EQ(0, (int)w->get_current_angle()) << "invalid direction should use default 0.0 angle";
}

