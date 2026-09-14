#include <openglad/gameplay/guy.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w) return nullptr;
    w->setxy(50, 50);
    return w;
}

// ---------------------------------------------------------------------------
// facing tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_facing_right)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = w->facing(10, 0);
    ASSERT_EQ(FACE_RIGHT, (int)dir) << "facing right should be FACE_RIGHT";

}


TEST(WalkerExtended, walker_facing_left)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = w->facing(-10, 0);
    ASSERT_EQ(FACE_LEFT, (int)dir) << "facing left should be FACE_LEFT";

}


TEST(WalkerExtended, walker_facing_up)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = w->facing(0, -10);
    ASSERT_EQ(FACE_UP, (int)dir) << "facing up should be FACE_UP";

}


TEST(WalkerExtended, walker_facing_down)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = w->facing(0, 10);
    ASSERT_EQ(FACE_DOWN, (int)dir) << "facing down should be FACE_DOWN";

}


// ---------------------------------------------------------------------------
// turn tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_turn_basic)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->set_stepsize(2.0f);
    w->set_curdir(FACE_UP);
    ASSERT_TRUE(w->turn(FACE_RIGHT)) << "turn() always reports it rotated";

    // distance = curdir - target = FACE_UP - FACE_RIGHT = -2, which lands in
    // [-4, 0) -> exactly one 45-degree step clockwise. turn() must never snap
    // straight onto the target, and must never rotate the other way.
    ASSERT_EQ(FACE_UP_RIGHT, (int)w->curdir())
        << "turn(FACE_RIGHT) from FACE_UP rotates one step to FACE_UP_RIGHT";
    ASSERT_FLOAT_EQ(2.0f, w->lastx())
        << "the new FACE_UP_RIGHT heading sets lastx = +stepsize";
    ASSERT_FLOAT_EQ(-2.0f, w->lasty())
        << "the new FACE_UP_RIGHT heading sets lasty = -stepsize";
}


TEST(WalkerExtended, walker_turn_all_targets)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->set_stepsize(1.0f);

    // From FACE_UP (0) the turn distance is -target. Targets 1..4 sit in
    // [-4, 0) and rotate clockwise (+1 -> FACE_UP_RIGHT); every other target
    // (including "turn to where I already face") rotates counter-clockwise
    // (+7 mod 8 -> FACE_UP_LEFT).
    const int expected_dir[8] = { FACE_UP_LEFT, FACE_UP_RIGHT, FACE_UP_RIGHT, FACE_UP_RIGHT,
                                  FACE_UP_RIGHT, FACE_UP_LEFT, FACE_UP_LEFT, FACE_UP_LEFT };

    for (int dir = 0; dir < 8; dir++) {
        w->set_curdir(FACE_UP);
        w->set_lastx(0.0f);
        w->set_lasty(0.0f);
        ASSERT_TRUE(w->turn(static_cast<short>(dir))) << "turn() reports it rotated, target " << dir;
        ASSERT_EQ(expected_dir[dir], (int)w->curdir())
            << "turn from FACE_UP toward " << dir << " must land on one 45-degree step";
        // Both landing facings are "up-ish": lasty is always -stepsize, lastx
        // is +stepsize clockwise and -stepsize counter-clockwise.
        ASSERT_FLOAT_EQ(expected_dir[dir] == FACE_UP_RIGHT ? 1.0f : -1.0f, w->lastx())
            << "heading lastx for target " << dir;
        ASSERT_FLOAT_EQ(-1.0f, w->lasty())
            << "heading lasty for target " << dir;
    }
}


// ---------------------------------------------------------------------------
// distance_to_ob tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_distance_to_self)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    Sint32 d = w->distance_to_ob(w.get());
    ASSERT_EQ(0, (int)d) << "distance to self should be 0";

}


TEST(WalkerExtended, walker_distance_to_other)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_MAGE);
    ASSERT_TRUE(a != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(b != nullptr) << "create_walker should succeed";

    a->setxy(100, 100);

    // distance_to_ob is Manhattan: abs(dx) + abs(dy), never squared, never halved.
    b->setxy(110, 100);
    ASSERT_EQ(10, (int)a->distance_to_ob(b.get()))
        << "10 px due east is a Manhattan distance of 10";
    b->setxy(110, 105);
    ASSERT_EQ(15, (int)a->distance_to_ob(b.get()))
        << "dx 10 + dy 5 is 15, not the 11 a euclidean formula would give";
    b->setxy(90, 95);
    ASSERT_EQ(15, (int)a->distance_to_ob(b.get()))
        << "negative deltas are taken in absolute value";
}


TEST(WalkerExtended, walker_distance_to_ob_center)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_MAGE);
    ASSERT_TRUE(a != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(b != nullptr) << "create_walker should succeed";

    a->set_sizex(8);
    a->set_sizey(8);
    b->set_sizex(10);
    b->set_sizey(12);
    a->setxy(100, 100);
    b->setxy(110, 100);

    // xd = (tx - x) + (tsizex - sizex)/2 = 10 + 1 = 11
    // yd = (ty - y) + (tsizey - sizey)/2 =  0 + 2 =  2
    // result is the SQUARE of that offset: 121 + 4 = 125.
    ASSERT_EQ(125, (int)a->distance_to_ob_center(b.get()))
        << "center distance is squared and half-size corrected (11^2 + 2^2)";

    // Same footprint on both sides: the half-size correction drops out and the
    // result is the plain squared delta.
    b->set_sizex(8);
    b->set_sizey(8);
    b->setxy(103, 104);
    ASSERT_EQ(25, (int)a->distance_to_ob_center(b.get()))
        << "equal sizes leave 3^2 + 4^2 = 25";

    ASSERT_EQ(0, (int)a->distance_to_ob_center(a.get()))
        << "an object is at zero center distance from itself";
}


// ---------------------------------------------------------------------------
// query_team_color test
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_query_team_color)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->set_team_num(0);
    unsigned char c0 = w->query_team_color();
    ASSERT_EQ(40, (int)c0) << "team 0 color should be 40";

    w->set_team_num(1);
    unsigned char c1 = w->query_team_color();
    ASSERT_EQ(56, (int)c1) << "team 1 color should be 56";

    w->set_team_num(3);
    unsigned char c3 = w->query_team_color();
    ASSERT_EQ(88, (int)c3) << "team 3 color should be 88";

}


// ---------------------------------------------------------------------------
// get_current_angle test
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_get_current_angle)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    // Literal radian constants, so a mutation of the switch in
    // walker_movement.cpp cannot be mirrored by the expectation.
    const float expected[8] = {
        -1.57079633f,  // FACE_UP        -pi/2
        -0.78539816f,  // FACE_UP_RIGHT  -pi/4
         0.00000000f,  // FACE_RIGHT      0
         0.78539816f,  // FACE_DOWN_RIGHT pi/4
         1.57079633f,  // FACE_DOWN       pi/2
         2.35619449f,  // FACE_DOWN_LEFT  3pi/4
         3.14159265f,  // FACE_LEFT       pi
         3.92699082f   // FACE_UP_LEFT    5pi/4
    };

    for (int dir = 0; dir < 8; dir++) {
        w->set_curdir(static_cast<char>(dir));
        EXPECT_NEAR(expected[dir], w->get_current_angle(), 1e-5f)
            << "get_current_angle for facing " << dir;
    }

    w->set_curdir(static_cast<char>(42));
    EXPECT_NEAR(0.0f, w->get_current_angle(), 1e-6f)
        << "an out-of-range facing falls back to the 0.0 default";
}


// ---------------------------------------------------------------------------
// act_type tests
// ---------------------------------------------------------------------------

// walker::set_act_type is a ONE-DEEP UNDO STACK, not a setter: it banks the
// OUTGOING act type in old_act_type before overwriting act_type, and
// restore_act_type() puts the banked one back and reports it. Every seat
// hand-off depends on exactly that pairing -- dropping a player parks the
// seat on the AI with set_act_type(ACT_RANDOM) and the seat comes back with
// restore_act_type() (src/gameplay/game_server.cpp, sim_input_handler.cpp).
TEST(WalkerExtended, walker_set_act_type_banks_one_level_of_undo)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    // A known starting point, plus a sentinel in the bank that the first push
    // must overwrite (set_act_type_state writes act_type WITHOUT banking).
    w->set_act_type_state(ACT_GUARD);
    w->set_old_act_type(ACT_SIT);

    ASSERT_EQ(ACT_CONTROL, (int)w->set_act_type(ACT_CONTROL))
        << "set_act_type reports the act type it installed";
    ASSERT_EQ(ACT_CONTROL, (int)w->act_type())
        << "set_act_type installs the requested act type";
    ASSERT_EQ(ACT_GUARD, (int)w->old_act_type())
        << "set_act_type banks the OUTGOING act type, not the incoming one "
           "and not the sentinel that was in the bank";

    // Pushing again keeps only the previous level: the ACT_GUARD that was
    // banked first is gone for good.
    ASSERT_EQ(ACT_RANDOM, (int)w->set_act_type(ACT_RANDOM))
        << "set_act_type reports the act type it installed";
    ASSERT_EQ(ACT_CONTROL, (int)w->old_act_type())
        << "the undo bank is one deep: the second push banks ACT_CONTROL";

    ASSERT_EQ(ACT_CONTROL, (int)w->restore_act_type())
        << "restore_act_type reports the act type it restored";
    ASSERT_EQ(ACT_CONTROL, (int)w->act_type())
        << "restore_act_type installs the banked act type";
    ASSERT_EQ(ACT_CONTROL, (int)w->old_act_type())
        << "restore leaves the bank alone -- it is a restore, not a swap";
    ASSERT_EQ(ACT_CONTROL, (int)w->restore_act_type())
        << "so restoring twice is idempotent and never toggles back";
    ASSERT_EQ(ACT_CONTROL, (int)w->act_type())
        << "a second restore still leaves the banked act type installed";
}


// ---------------------------------------------------------------------------
// collide test
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_collide)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_MAGE);
    ASSERT_TRUE(a != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(b != nullptr) << "create_walker should succeed";

    ASSERT_EQ(nullptr, a->collide_ob()) << "a fresh walker has no collision partner";

    // living::collide always returns 1; the things it is FOR are recording the
    // partner and, for a hostile one, opening fire.
    ASSERT_TRUE(a->collide(b.get())) << "collide always reports handled";
    ASSERT_EQ(b.get(), a->collide_ob()) << "collide records the object we hit";
    ASSERT_EQ(nullptr, b->collide_ob()) << "collide records on the caller only, not the partner";

    ASSERT_TRUE(a->collide(nullptr)) << "collide(nullptr) still reports handled";
    ASSERT_EQ(nullptr, a->collide_ob()) << "collide(nullptr) clears the recorded partner";
    // (collide_ob_id stays 0 here: these walkers are loader-owned and never
    // joined a world, so entity_id() is 0 on both sides -- the pointer is the
    // only honest oracle in this fixture.)

    // Paired control on the "bumping a foe starts a swing" rule. Line the
    // attacker up so init_fire() has nothing to turn toward and is not busy:
    // the observable is the attack animation taking over from the walk one.
    auto arm = [](walker* w) {
        w->set_act_type(ACT_RANDOM);
        w->set_curdir(FACE_RIGHT);
        w->set_enddir(FACE_RIGHT);
        w->set_lastx(1.0f);
        w->set_lasty(0.0f);
        w->set_busy(0.0f);
        w->set_ani_type(ANI_WALK);
    };

    // Same team: bumping an ally must NOT start a swing.
    a->set_team_num(0);
    b->set_team_num(0);
    arm(a.get());
    ASSERT_TRUE(a->collide(b.get())) << "collide with an ally still reports handled";
    ASSERT_EQ(b.get(), a->collide_ob()) << "the ally is still recorded as the partner";
    ASSERT_EQ(ANI_WALK, (int)a->ani_type()) << "bumping an ally must not open fire";

    // Hostile team: the same bump does start a swing.
    b->set_team_num(1);
    arm(a.get());
    ASSERT_TRUE(a->collide(b.get())) << "collide with a foe reports handled";
    ASSERT_EQ(ANI_ATTACK, (int)a->ani_type()) << "bumping a live foe opens fire";
}


// ---------------------------------------------------------------------------
// walk / walkstep smoke tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_walk_moves_turns_and_refuses_off_map)
{
    // walk() consults the grid; this binary loads no map of its own.
    og::runtime::current_session->myscreen_->world().create_new_grid();

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    // The walker is a living, so living::walk(float,float) is the override
    // under test: aligned -> move, misaligned -> record enddir and turn ONE
    // 45-degree step (except for an uncommanded ACT_CONTROL walker).
    w->set_act_type(ACT_RANDOM);
    w->setxy(100, 100);

    // Aligned with the requested heading and standing on open grass: move.
    w->set_curdir(FACE_RIGHT);
    ASSERT_TRUE(w->walk(1, 0)) << "an aligned walk onto passable ground succeeds";
    ASSERT_EQ(101, (int)w->xpos()) << "the aligned walk advanced x by exactly 1";
    ASSERT_EQ(100, (int)w->ypos()) << "the aligned walk left y alone";

    w->set_curdir(FACE_DOWN);
    w->set_cycle(0);
    ASSERT_TRUE(w->walk(0, 1)) << "an aligned walk south succeeds";
    ASSERT_EQ(101, (int)w->xpos()) << "walking south left x alone";
    ASSERT_EQ(101, (int)w->ypos()) << "the aligned walk advanced y by exactly 1";
    ASSERT_EQ(1, (int)w->cycle()) << "a walk that actually moved advances the walk cycle by one";

    // curdir != facing(x,y): the misaligned branch records the goal facing in
    // enddir and rotates exactly one step toward it. It must not snap, and it
    // must not move the walker.
    w->set_curdir(FACE_UP);
    ASSERT_TRUE(w->walk(1, 0)) << "the changed-direction branch returns 1";
    ASSERT_EQ(FACE_RIGHT, (int)w->enddir())
        << "living::walk records facing(1,0) as the goal facing";
    ASSERT_EQ(FACE_UP_RIGHT, (int)w->curdir())
        << "living::walk turns one 45-degree step toward the goal, never straight onto it";
    ASSERT_EQ(101, (int)w->xpos()) << "turning must not move the walker";
    ASSERT_EQ(101, (int)w->ypos()) << "turning must not move the walker";

    // The ACT_CONTROL exemption: a player walker with no queued command does
    // NOT turn inside walk() (act() turns it), it only records enddir.
    w->set_act_type(ACT_CONTROL);
    ASSERT_TRUE(!w->stats()->has_commands()) << "fixture precondition: no queued commands";
    w->set_curdir(FACE_UP);
    ASSERT_TRUE(w->walk(0, 1)) << "the changed-direction branch returns 1 for control too";
    ASSERT_EQ(FACE_DOWN, (int)w->enddir()) << "an uncommanded control walker still records enddir";
    ASSERT_EQ(FACE_UP, (int)w->curdir())
        << "an uncommanded ACT_CONTROL walker must not be turned by walk()";
    w->set_act_type(ACT_RANDOM);

    // Aligned, but the destination is off the west edge of the grid.
    w->setxy(0, 50);
    w->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!w->walk(-1, 0)) << "walking off the west edge returns 0";
    ASSERT_EQ(0, (int)w->xpos()) << "the refused walk left the position untouched";
    ASSERT_EQ(50, (int)w->ypos()) << "the refused walk left the position untouched";
}


TEST(WalkerExtended, walker_walkstep_records_heading_and_steps_by_stepsize)
{
    // walkstep() consults the grid; this binary loads no map of its own.
    og::runtime::current_session->myscreen_->world().create_new_grid();

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    w->set_act_type(ACT_RANDOM);
    w->set_stepsize(2.0f);
    w->setxy(100, 100);

    // Aligned: walkstep records the heading and moves a full stepsize.
    w->set_curdir(FACE_RIGHT);
    ASSERT_TRUE(w->walkstep(1, 0)) << "aligned walkstep onto open grass succeeds";
    ASSERT_FLOAT_EQ(2.0f, w->lastx()) << "walkstep stores lastx = dx * stepsize";
    ASSERT_FLOAT_EQ(0.0f, w->lasty()) << "walkstep stores lasty = dy * stepsize";
    ASSERT_EQ(102, (int)w->xpos()) << "walkstep advances x by a whole stepsize";
    ASSERT_EQ(100, (int)w->ypos()) << "walkstep east leaves y alone";

    // Not yet aligned: living::walk turns one step toward the goal and reports
    // success, so this tick costs the step and nothing moves. Two ticks are
    // needed to swing FACE_RIGHT -> FACE_DOWN.
    ASSERT_TRUE(w->walkstep(0, 1)) << "a turning walkstep still reports success";
    ASSERT_EQ(FACE_DOWN, (int)w->enddir()) << "the requested facing is recorded as the goal";
    ASSERT_EQ(FACE_DOWN_RIGHT, (int)w->curdir())
        << "the first turning walkstep rotates FACE_RIGHT one step toward FACE_DOWN";
    ASSERT_EQ(102, (int)w->xpos()) << "the turning walkstep must not move x";
    ASSERT_EQ(100, (int)w->ypos()) << "the turning walkstep must not move y";

    ASSERT_TRUE(w->walkstep(0, 1)) << "the second turning walkstep also reports success";
    ASSERT_EQ(FACE_DOWN, (int)w->curdir()) << "the second step completes the swing to FACE_DOWN";
    ASSERT_EQ(100, (int)w->ypos()) << "still no movement while turning";

    // Now aligned south: the same request moves a whole stepsize.
    ASSERT_TRUE(w->walkstep(0, 1)) << "the aligned walkstep moves";
    ASSERT_FLOAT_EQ(0.0f, w->lastx()) << "heading lastx for a due-south step";
    ASSERT_FLOAT_EQ(2.0f, w->lasty()) << "heading lasty for a due-south step";
    ASSERT_EQ(102, (int)w->xpos()) << "walking south leaves x alone";
    ASSERT_EQ(102, (int)w->ypos()) << "walkstep advances y by a whole stepsize";

    // Diagonal, aligned: both axes move a full stepsize.
    w->set_curdir(FACE_UP_LEFT);
    ASSERT_TRUE(w->walkstep(-1, -1)) << "aligned diagonal walkstep succeeds";
    ASSERT_FLOAT_EQ(-2.0f, w->lastx()) << "diagonal heading lastx = -stepsize";
    ASSERT_FLOAT_EQ(-2.0f, w->lasty()) << "diagonal heading lasty = -stepsize";
    ASSERT_EQ(100, (int)w->xpos()) << "the diagonal step moved x back by a stepsize";
    ASSERT_EQ(100, (int)w->ypos()) << "the diagonal step moved y back by a stepsize";
}


// ---------------------------------------------------------------------------
// set_order_family test
// ---------------------------------------------------------------------------

// set_order_family re-stamps BOTH halves of an entity's identity in one call
// -- that is the whole point of it existing next to the two setters. The
// loader stamps a fresh walker with it (src/resources/gloader.cpp set_walker)
// and a snapshot re-stamps a recycled one with it
// (src/gameplay/world_snapshot.cpp, which then compares order() and family()
// against the wire to decide whether a slot was reused). walker::walkstep
// reads the pair together -- its stationary short-circuit fires only for an
// Order::Living walker whose FAMILY is a stationary one -- so one walkstep
// proves both halves landed.
TEST(WalkerExtended, walker_set_order_family_restamps_both_halves_of_the_identity)
{
    // walkstep() consults the grid; this binary loads no map of its own.
    og::runtime::current_session->myscreen_->world().create_new_grid();

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    ASSERT_EQ(Order::Living, w->order()) << "fixture precondition";
    ASSERT_EQ((int)FAMILY_SOLDIER, (int)w->family()) << "fixture precondition";

    ASSERT_TRUE(w->set_order_family(Order::Living, FAMILY_ARCHER))
        << "set_order_family reports it stamped the pair";
    ASSERT_EQ((int)FAMILY_ARCHER, (int)w->family()) << "the family half was re-stamped";
    ASSERT_EQ(Order::Living, w->order()) << "the order half was re-stamped as Living";

    // A stationary family + the Living order: walkstep short-circuits, records
    // the RAW delta (not delta * stepsize) and refuses to move.
    w->setxy(100, 100);
    w->set_stepsize(3.0f);
    w->set_curdir(FACE_RIGHT);
    ASSERT_TRUE(w->set_order_family(Order::Living, FAMILY_TOWER1))
        << "set_order_family reports it stamped the pair";
    ASSERT_EQ((int)FAMILY_TOWER1, (int)w->family()) << "the family half is now the tower";
    ASSERT_TRUE(w->walkstep(1, 0)) << "a stationary walkstep reports success";
    ASSERT_FLOAT_EQ(1.0f, w->lastx())
        << "the stationary arm the new FAMILY selected records the raw delta";
    ASSERT_EQ(100, (int)w->xpos()) << "a stationary family never moves";
    ASSERT_EQ(100, (int)w->ypos()) << "a stationary family never moves";

    // Same family, Weapon order: the short-circuit is gated on the ORDER
    // field, so re-stamping it alone puts the very same walker back on the
    // normal path -- stepsize-scaled heading and a real move.
    ASSERT_TRUE(w->set_order_family(Order::Weapon, FAMILY_TOWER1))
        << "set_order_family reports it stamped the pair";
    ASSERT_EQ(Order::Weapon, w->order()) << "the order half changed to Weapon";
    ASSERT_EQ((int)FAMILY_TOWER1, (int)w->family()) << "the family half is unchanged";
    ASSERT_TRUE(w->walkstep(1, 0)) << "the non-stationary walkstep reports success";
    ASSERT_FLOAT_EQ(3.0f, w->lastx())
        << "off the stationary arm, walkstep records delta * stepsize";
    ASSERT_EQ(103, (int)w->xpos()) << "and it steps a whole stepsize east";
    ASSERT_EQ(100, (int)w->ypos()) << "walking east leaves y alone";

    // The virtual query_order() is the C++ CLASS identity and is deliberately
    // NOT the stamped field: a living object answers Living whatever
    // set_order_family wrote, which is why snapshot reconciliation compares
    // order() and never query_order().
    ASSERT_EQ(Order::Living, w->query_order())
        << "living::query_order is the class identity, not the stamped order";
}


// ---------------------------------------------------------------------------
// is_friendly extended tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_is_friendly_same_team)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_ARCHER);
    ASSERT_TRUE(a != nullptr) << "create a should succeed";
    ASSERT_TRUE(b != nullptr) << "create b should succeed";

    a->set_team_num(0);
    b->set_team_num(0);
    ASSERT_TRUE(a->is_friendly(b.get())) << "same team should be friendly";

}


TEST(WalkerExtended, walker_is_friendly_null)
{
    auto a = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(a != nullptr) << "create a should succeed";

    ASSERT_TRUE(!a->is_friendly(nullptr)) << "null target should not be friendly";

}
