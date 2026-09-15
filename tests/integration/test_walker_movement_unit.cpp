#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#include <openglad/legacy/base.h>
#include <memory>
#include <gtest/gtest.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <array>
#include <cmath>
#include "test_gameplay_context_scope.h"

// --- From test_walker_movement_push.cpp ---
namespace detail_walker_movement_push {
namespace {

struct MovementFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    MovementFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &events, &cfg);
    }
};

walker* add_living(MovementFixture& fx, char family = FAMILY_SOLDIER)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(64, 64);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(WalkerMovementUnit, walker_movement_facing_thresholds)
{
    MovementFixture fx;
    walker* w = add_living(fx);
    ASSERT_TRUE(w != nullptr);

    ASSERT_TRUE(w->facing(0, 1) == FACE_DOWN);
    ASSERT_TRUE(w->facing(0, -1) == FACE_UP);
    ASSERT_TRUE(w->facing(1, 0) == FACE_RIGHT);
    ASSERT_TRUE(w->facing(-1, 0) == FACE_LEFT);
    ASSERT_TRUE(w->facing(1, 1) == FACE_DOWN_RIGHT);
    ASSERT_TRUE(w->facing(1, -1) == FACE_UP_RIGHT);
    ASSERT_TRUE(w->facing(-1, 1) == FACE_DOWN_LEFT);
    ASSERT_TRUE(w->facing(-1, -1) == FACE_UP_LEFT);
}

TEST(WalkerMovementUnit, walker_movement_turn_stationary_and_normal)
{
    MovementFixture fx;
    walker* normal = add_living(fx, FAMILY_SOLDIER);
    ASSERT_TRUE(normal != nullptr);
    normal->set_stepsize(2.0f);
    normal->set_curdir(FACE_UP);
    normal->set_lastx(0.0f);
    normal->set_lasty(-normal->stepsize());
    ASSERT_TRUE(normal->turn(FACE_RIGHT));
    // distance = FACE_UP - FACE_RIGHT = -2, inside [-4,0): one CLOCKWISE step.
    ASSERT_EQ(FACE_UP_RIGHT, static_cast<int>(normal->curdir()))
        << "turn must rotate exactly one 45-degree step, clockwise for a -2 distance";
    ASSERT_FLOAT_EQ(2.0f, normal->lastx())
        << "the UP_RIGHT arm writes lastx = +stepsize";
    ASSERT_FLOAT_EQ(-2.0f, normal->lasty())
        << "the UP_RIGHT arm writes lasty = -stepsize";

    walker* tower = add_living(fx, FAMILY_TOWER1);
    ASSERT_TRUE(tower != nullptr);
    tower->set_stepsize(3.0f);
    tower->set_lastx(9.0f);
    tower->set_lasty(-4.0f);
    tower->set_curdir(FACE_UP);
    ASSERT_TRUE(tower->turn(FACE_LEFT));
    // distance = 0 - 6 = -6: outside [-4,0) and below 4, so COUNTER-clockwise.
    ASSERT_EQ(FACE_UP_LEFT, static_cast<int>(tower->curdir()))
        << "a stationary family still rotates its facing";
    ASSERT_FLOAT_EQ(9.0f, tower->lastx())
        << "a stationary family never has its heading rewritten by turn";
    ASSERT_FLOAT_EQ(-4.0f, tower->lasty())
        << "a stationary family never has its heading rewritten by turn";
}

TEST(WalkerMovementUnit, walker_movement_walk_and_walkstep_edge_paths)
{
    MovementFixture fx;
    walker* w = add_living(fx);
    ASSERT_NE(nullptr, w);

    // Aligned with the requested heading and standing on open grass: walk()
    // moves by exactly the delta it was handed and advances the walk cycle.
    w->set_curdir(FACE_RIGHT);
    w->set_cycle(0);
    ASSERT_TRUE(w->walk(1.0f, 0.0f)) << "an aligned walk onto grass succeeds";
    ASSERT_EQ(65, w->xpos()) << "the aligned walk advanced x by exactly the delta";
    ASSERT_EQ(64, w->ypos()) << "the aligned walk left y alone";
    ASSERT_EQ(1, static_cast<int>(w->cycle()))
        << "a walk that actually moved advances the walk cycle by one";

    // Aligned, but the destination is off the west edge: refused, and nobody
    // moves. The off-map guard returns before the passability probe, so a
    // walker without BIT_ANIMATE does not cycle either.
    w->setxy(0, 0);
    w->set_curdir(FACE_LEFT);
    w->set_cycle(0);
    ASSERT_FALSE(w->walk(-1.0f, 0.0f)) << "walking off the west edge is refused";
    ASSERT_EQ(0, w->xpos()) << "the refused walk moved nothing";
    ASSERT_EQ(0, w->ypos()) << "the refused walk moved nothing";
    ASSERT_EQ(0, static_cast<int>(w->cycle())) << "the refused walk did not cycle";

    // walkstep() records the requested heading scaled by stepsize FIRST, then
    // delegates to walk(). While curdir still disagrees with the request,
    // walk() takes its changed-direction branch: it adopts the facing, reports
    // success and moves nobody -- so a misaligned walkstep never reaches the
    // blocked-move fallbacks, whatever user() says. (Those fallbacks are
    // pinned by walker_movement_r11_walkstep_npc_fallback_and_user_slide.)
    w->set_user(-1);
    ASSERT_TRUE(w->walkstep(-1.0f, -1.0f)) << "a misaligned NPC walkstep reports success";
    ASSERT_FLOAT_EQ(-1.0f, w->lastx()) << "walkstep records lastx = dx * stepsize";
    ASSERT_FLOAT_EQ(-1.0f, w->lasty()) << "walkstep records lasty = dy * stepsize";
    ASSERT_EQ(FACE_UP_LEFT, static_cast<int>(w->curdir()))
        << "the base walker snaps straight onto the requested facing";
    ASSERT_EQ(0, w->xpos()) << "a turning walkstep moves nothing";
    ASSERT_EQ(0, w->ypos()) << "a turning walkstep moves nothing";

    // NOW curdir agrees, so the very same request reaches the NPC fallback:
    // it tries UP, then LEFT, both of which are off-map in the corner, and
    // restores the entry facing before reporting failure.
    ASSERT_FALSE(w->walkstep(-1.0f, -1.0f)) << "a cornered NPC walkstep fails";
    ASSERT_EQ(0, w->xpos()) << "the failed fallback moved nothing";
    ASSERT_EQ(0, w->ypos()) << "the failed fallback moved nothing";
    ASSERT_EQ(FACE_UP_LEFT, static_cast<int>(w->curdir()))
        << "walkstep restores the entry facing before returning";

    // A fresh request is misaligned again: turn, do not step.
    ASSERT_TRUE(w->walkstep(1.0f, -1.0f)) << "a misaligned NPC walkstep reports success";
    ASSERT_FLOAT_EQ(1.0f, w->lastx()) << "walkstep records lastx = dx * stepsize";
    ASSERT_FLOAT_EQ(-1.0f, w->lasty()) << "walkstep records lasty = dy * stepsize";
    ASSERT_EQ(FACE_UP_RIGHT, static_cast<int>(w->curdir())) << "it adopted the new facing";
    ASSERT_EQ(0, w->xpos()) << "a turning walkstep moves nothing";
    ASSERT_EQ(0, w->ypos()) << "a turning walkstep moves nothing";

    // A user walker takes the same turn-first path.
    w->set_user(0);
    w->setxy(0, 10);
    ASSERT_TRUE(w->walkstep(-1.0f, -1.0f)) << "a misaligned user walkstep reports success";
    ASSERT_EQ(FACE_UP_LEFT, static_cast<int>(w->curdir())) << "it adopted the new facing";
    ASSERT_EQ(0, w->xpos()) << "a turning walkstep moves nothing";
    ASSERT_EQ(10, w->ypos()) << "a turning walkstep moves nothing";

    ASSERT_TRUE(w->walkstep(-1.0f, 1.0f)) << "a misaligned user walkstep reports success";
    ASSERT_EQ(FACE_DOWN_LEFT, static_cast<int>(w->curdir())) << "it adopted the new facing";
    ASSERT_EQ(0, w->xpos()) << "a turning walkstep moves nothing";
    ASSERT_EQ(10, w->ypos()) << "a turning walkstep moves nothing";

    // Aligned DOWN_LEFT against the west edge: the user slide gives up on the
    // diagonal, slides one pixel south on the free axis, and STILL reports
    // failure -- the slide's whole point is that it moves you on a step the
    // return value calls refused.
    ASSERT_FALSE(w->walkstep(-1.0f, 1.0f))
        << "the user slide always reports the requested diagonal failed";
    ASSERT_EQ(0, w->xpos()) << "x stayed: west of 0 is off-map";
    ASSERT_EQ(11, w->ypos()) << "y slid exactly one pixel south";
    ASSERT_EQ(FACE_DOWN_LEFT, static_cast<int>(w->curdir()))
        << "walkstep restores the entry facing before returning";
}
} // namespace detail_walker_movement_push

// --- From test_walker_movement_r11.cpp ---
namespace detail_walker_movement_r11 {
namespace {

struct WalkerMovementR11Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    WalkerMovementR11Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &events, &cfg);
    }
};

walker* add_living(WalkerMovementR11Fixture& fx, char family = FAMILY_SOLDIER)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(96, 96);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void assign_ani(walker* w)
{
    static std::array<std::array<signed char, 3>, 8> seqs{};
    static std::array<signed char*, 8> rows{};
    for (int i = 0; i < 8; ++i)
    {
        seqs[static_cast<std::size_t>(i)][0] = 0;
        seqs[static_cast<std::size_t>(i)][1] = 1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(WalkerMovementUnit, walker_movement_r11_move_worldmove_setxy_and_setworldxy)
{
    WalkerMovementR11Fixture fx;
    walker* w = add_living(fx);
    ASSERT_NE(nullptr, w);
    obmap* map = fx.level.world().myobmap.get();
    ASSERT_NE(nullptr, map);

    // move(x,y) == setxy(xpos+x, ypos+y) from the spawn cell (96,96).
    ASSERT_TRUE(w->move(1, -1));
    ASSERT_EQ(97, w->xpos()) << "move() must offset from the current x";
    ASSERT_EQ(95, w->ypos()) << "move() must offset from the current y";
    ASSERT_FLOAT_EQ(97.0f, w->worldx()) << "setxy mirrors into the float position";
    ASSERT_FLOAT_EQ(95.0f, w->worldy()) << "setxy mirrors into the float position";

    // worldmove(x,y) == setworldxy(worldx+x, worldy+y): the float position keeps
    // the fraction, the short position truncates it.
    w->worldmove(0.5f, -0.5f);
    ASSERT_FLOAT_EQ(97.5f, w->worldx()) << "worldmove must keep the sub-pixel x";
    ASSERT_FLOAT_EQ(94.5f, w->worldy()) << "worldmove must keep the sub-pixel y";
    ASSERT_EQ(97, w->xpos()) << "xpos is the truncated worldx";
    ASSERT_EQ(94, w->ypos()) << "ypos is the truncated worldy";

    // A normal walker is registered in the obmap by every reposition.
    ASSERT_EQ(1u, map->walker_to_pos.count(w))
        << "a non-ignored walker must stay obmap-registered";
    const std::size_t registered = map->size();

    // ignore() flips BOTH setters from map->move to map->remove.
    w->set_ignore(1);
    ASSERT_TRUE(w->setxy(100, 100));
    ASSERT_EQ(100, w->xpos());
    ASSERT_EQ(100, w->ypos());
    ASSERT_EQ(0u, map->walker_to_pos.count(w))
        << "setxy on an ignore() walker must REMOVE it from the obmap, not move it";
    ASSERT_EQ(registered - 1u, map->size());

    w->setworldxy(101.0f, 99.0f);
    ASSERT_EQ(101, w->xpos());
    ASSERT_EQ(99, w->ypos());
    ASSERT_EQ(0u, map->walker_to_pos.count(w))
        << "setworldxy on an ignore() walker must keep it out of the obmap";
    w->set_ignore(0);
}

TEST(WalkerMovementUnit, walker_movement_r11_walkstep_npc_fallback_and_user_slide)
{
    WalkerMovementR11Fixture fx;
    walker* npc = add_living(fx);
    ASSERT_NE(nullptr, npc);
    assign_ani(npc);
    npc->set_user(-1);

    // The NPC fallback switch only runs when curdir already equals the step's
    // facing (otherwise walk() just turns in place and reports success).

    // FACE_LEFT into the west edge -> the switch tries DOWN, which is clear.
    npc->setxy(0, 0);
    npc->set_curdir(FACE_LEFT);
    ASSERT_TRUE(npc->walkstep(-1.0f, 0.0f))
        << "blocked LEFT must fall back to DOWN and report the move";
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(1, npc->ypos()) << "the DOWN fallback moves exactly one pixel south";
    ASSERT_EQ(FACE_LEFT, static_cast<int>(npc->curdir()))
        << "walkstep restores the entry facing before returning";

    // FACE_UP in the corner -> the LEFT fallback is off-map too.
    npc->setxy(0, 0);
    npc->set_curdir(FACE_UP);
    ASSERT_FALSE(npc->walkstep(0.0f, -1.0f))
        << "UP and its LEFT fallback are both off-map in the corner";
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(0, npc->ypos());

    // FACE_UP_RIGHT -> the UP component fails, the RIGHT component carries it.
    npc->setxy(0, 0);
    npc->set_curdir(FACE_UP_RIGHT);
    ASSERT_TRUE(npc->walkstep(1.0f, -1.0f))
        << "a diagonal fallback succeeds when either component walks";
    ASSERT_EQ(1, npc->xpos()) << "the RIGHT component moved one pixel east";
    ASSERT_EQ(0, npc->ypos()) << "the UP component was off-map";

    // FACE_UP_LEFT in the corner -> both components fail.
    npc->setxy(0, 0);
    npc->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(npc->walkstep(-1.0f, -1.0f));
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(0, npc->ypos());

    npc->setxy(300, 300); // out of the user's way

    // User slide: the diagonal is blocked by the west edge, the y axis is free.
    walker* user = add_living(fx);
    ASSERT_NE(nullptr, user);
    assign_ani(user);
    user->set_user(0);
    user->setxy(0, 1);
    user->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(user->walkstep(-1.0f, -1.0f))
        << "the user slide always reports the requested diagonal failed";
    ASSERT_EQ(0, user->xpos()) << "x stayed: west of 0 is off-map";
    ASSERT_EQ(0, user->ypos()) << "y slid one pixel north";
    ASSERT_EQ(FACE_UP_LEFT, static_cast<int>(user->curdir()))
        << "walkstep restores the entry facing before returning";
}

TEST(WalkerMovementUnit, walker_movement_r11_walk_turn_and_angles)
{
    WalkerMovementR11Fixture fx;
    walker* w = add_living(fx);
    ASSERT_TRUE(w != nullptr);
    assign_ani(w);

    // walk(0,0) is the documented non-fatal no-op: reports success, moves
    // nothing, turns nothing.
    w->setxy(96, 96);
    w->set_curdir(FACE_UP);
    ASSERT_TRUE(w->walk(0.0f, 0.0f));
    ASSERT_EQ(96, w->xpos());
    ASSERT_EQ(96, w->ypos());
    ASSERT_EQ(FACE_UP, static_cast<int>(w->curdir()));

    // Changed direction: walk() turns in place, zeroes the cycle and does NOT
    // advance the walker.
    w->set_cycle(1);
    ASSERT_TRUE(w->walk(1.0f, 0.0f));
    ASSERT_EQ(FACE_RIGHT, static_cast<int>(w->curdir()))
        << "walk() adopts the requested facing when it differs from curdir";
    ASSERT_EQ(0, static_cast<int>(w->cycle()))
        << "a direction change restarts the walk animation at frame 0";
    ASSERT_EQ(96, w->xpos()) << "a turn-in-place walk must not move";
    ASSERT_EQ(96, w->ypos()) << "a turn-in-place walk must not move";

    // Continue-direction into a cell the 16px footprint cannot occupy (the
    // east map edge): no move, but BIT_ANIMATE still advances the walk frame.
    w->stats()->set_bit_flags(BIT_ANIMATE, 1);
    const short east = static_cast<short>(fx.level.world().pixmaxx - 10);
    w->setxy(east, static_cast<short>(96));
    w->set_cycle(0);
    ASSERT_FALSE(w->walk(1.0f, 0.0f))
        << "the footprint hangs off the east edge, so the step is refused";
    ASSERT_EQ(east, w->xpos()) << "a refused step must not move the walker";
    ASSERT_EQ(1, static_cast<int>(w->cycle()))
        << "BIT_ANIMATE cycles the walk frame even on a blocked step";
    w->stats()->set_bit_flags(BIT_ANIMATE, 0);

    // turn(): a target three steps clockwise is a distance of -3 (or +5), both
    // of which rotate exactly one step CLOCKWISE, from every facing.
    for (int d = 0; d < 8; ++d)
    {
        w->set_curdir(static_cast<char>(d));
        ASSERT_TRUE(w->turn(static_cast<short>((d + 3) % 8))) << "d=" << d;
        ASSERT_EQ((d + 1) % 8, static_cast<int>(w->curdir()))
            << "turn from facing " << d << " must land on " << ((d + 1) % 8);
    }

    // An out-of-range facing keeps the classic modulo result: distance
    // 120 - 2 = 118 >= 4 -> (120 + 1) % 8 == FACE_UP_RIGHT.
    w->set_curdir(120);
    ASSERT_TRUE(w->turn(2));
    ASSERT_EQ(FACE_UP_RIGHT, static_cast<int>(w->curdir()));
    ASSERT_FLOAT_EQ(1.0f, w->lastx());
    ASSERT_FLOAT_EQ(-1.0f, w->lasty());

    // get_current_angle is a fixed facing -> radians table.
    const float expected_angle[8] = {
        -static_cast<float>(M_PI_2),      // FACE_UP
        -static_cast<float>(M_PI_4),      // FACE_UP_RIGHT
        0.0f,                             // FACE_RIGHT
        static_cast<float>(M_PI_4),       // FACE_DOWN_RIGHT
        static_cast<float>(M_PI_2),       // FACE_DOWN
        static_cast<float>(3 * M_PI_4),   // FACE_DOWN_LEFT
        static_cast<float>(M_PI),         // FACE_LEFT
        static_cast<float>(5 * M_PI_4),   // FACE_UP_LEFT
    };
    for (int d = 0; d < 8; ++d)
    {
        w->set_curdir(static_cast<char>(d));
        EXPECT_FLOAT_EQ(expected_angle[d], w->get_current_angle())
            << "facing " << d << " must map to its own angle";
    }
    w->set_curdir(120);
    EXPECT_FLOAT_EQ(0.0f, w->get_current_angle())
        << "an out-of-range facing uses the default angle";

    // A stationary family turns its facing but keeps its heading vector.
    walker* tower = add_living(fx, FAMILY_TOWER1);
    ASSERT_NE(nullptr, tower);
    tower->set_stepsize(2.0f);
    tower->set_lastx(5.0f);
    tower->set_lasty(6.0f);
    tower->set_curdir(FACE_UP);
    ASSERT_TRUE(tower->turn(FACE_LEFT));
    ASSERT_EQ(FACE_UP_LEFT, static_cast<int>(tower->curdir()));
    ASSERT_FLOAT_EQ(5.0f, tower->lastx())
        << "the stationary guard must skip the lastx/lasty switch";
    ASSERT_FLOAT_EQ(6.0f, tower->lasty())
        << "the stationary guard must skip the lastx/lasty switch";
}
} // namespace detail_walker_movement_r11

// --- From test_walker_movement_r12.cpp ---
namespace detail_walker_movement_r12 {
namespace {

struct MovementR12Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    MovementR12Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &events, &cfg);
    }
};

walker* add_living(MovementR12Fixture& fx, char family = FAMILY_SOLDIER)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(64, 64);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void assign_basic_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[static_cast<std::size_t>(i)][0] = 0;
        seqs[static_cast<std::size_t>(i)][1] = 1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        seqs[static_cast<std::size_t>(i)][3] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(WalkerMovementUnit, walker_movement_r12_stationary_slope_and_animate_paths)
{
    MovementR12Fixture fx;
    walker* station = add_living(fx, FAMILY_TOWER1);
    walker* mover = add_living(fx, FAMILY_SOLDIER);
    ASSERT_TRUE(station && mover);

    // A stationary family short-circuits walkstep: it takes the requested
    // facing, records the RAW delta (never delta * stepsize, which is what
    // every other walker records) and never moves.
    station->set_stepsize(3.0f);
    station->set_curdir(FACE_UP);
    ASSERT_TRUE(station->walkstep(1.0f, 0.0f)) << "a stationary walkstep reports success";
    ASSERT_FLOAT_EQ(1.0f, station->lastx())
        << "a stationary family records the raw dx, not dx * stepsize";
    ASSERT_FLOAT_EQ(0.0f, station->lasty())
        << "a stationary family records the raw dy, not dy * stepsize";
    ASSERT_EQ(FACE_RIGHT, static_cast<int>(station->curdir()))
        << "the stationary arm still turns to face the request";
    ASSERT_EQ(FACE_RIGHT, static_cast<int>(station->enddir()))
        << "and copies that facing into enddir";
    ASSERT_EQ(64, station->xpos()) << "a stationary family never moves";
    ASSERT_EQ(64, station->ypos()) << "a stationary family never moves";

    ASSERT_EQ(FACE_DOWN, static_cast<int>(mover->facing(2, 5)));
    ASSERT_EQ(FACE_DOWN_RIGHT, static_cast<int>(mover->facing(2, 1)));
    ASSERT_EQ(FACE_UP_RIGHT, static_cast<int>(mover->facing(2, -1)));
    ASSERT_EQ(FACE_UP, static_cast<int>(mover->facing(2, -5)));
    ASSERT_EQ(FACE_DOWN, static_cast<int>(mover->facing(-2, 5)));
    ASSERT_EQ(FACE_DOWN_LEFT, static_cast<int>(mover->facing(-2, 1)));
    ASSERT_EQ(FACE_UP_LEFT, static_cast<int>(mover->facing(-2, -1)));
    ASSERT_EQ(FACE_UP, static_cast<int>(mover->facing(-2, -5)));

    // walk()'s "invalid move" arm -- the destination is ON the map but
    // impassable. This is the only place BIT_ANIMATE changes walk(): a
    // BIT_ANIMATE walker keeps cycling its animation while it is stuck, a
    // plain one freezes. Neither of them moves and neither reports success.
    // (Walking off the edge returns EARLIER than this arm, so it can never
    // reach it -- which is why the wall below is needed at all.)
    assign_basic_ani(mover);
    station->setxy(300, 300); // out of the mover's way
    auto& world = fx.level.world();
    ASSERT_TRUE(world.grid.valid()) << "fixture precondition: the grid exists";
    ASSERT_EQ(40, static_cast<int>(world.grid.w)) << "fixture precondition: grid width";
    // Grid cell (5,4) is the one a 16x16 walker at (64,64) steps into when it
    // walks one pixel east; make it a wall.
    world.grid.data[5 + 40 * 4] = PIX_WALL2;

    mover->setxy(64, 64);
    mover->set_curdir(FACE_RIGHT);
    mover->set_cycle(0);
    mover->stats()->set_bit_flags(BIT_ANIMATE, 0);
    ASSERT_FALSE(mover->walk(1.0f, 0.0f)) << "a walk into a wall is refused";
    ASSERT_EQ(64, mover->xpos()) << "the refused walk moved nothing";
    ASSERT_EQ(64, mover->ypos()) << "the refused walk moved nothing";
    ASSERT_EQ(0, static_cast<int>(mover->cycle()))
        << "without BIT_ANIMATE a blocked walk must not advance the cycle";

    mover->stats()->set_bit_flags(BIT_ANIMATE, 1);
    ASSERT_FALSE(mover->walk(1.0f, 0.0f))
        << "BIT_ANIMATE does not make a blocked walk succeed";
    ASSERT_EQ(64, mover->xpos()) << "the refused walk still moved nothing";
    ASSERT_EQ(64, mover->ypos()) << "the refused walk still moved nothing";
    ASSERT_EQ(1, static_cast<int>(mover->cycle()))
        << "a BIT_ANIMATE walker animates in place, advancing the cycle by one";
}

TEST(WalkerMovementUnit, walker_movement_r12_walkstep_npc_and_user_slide_paths)
{
    MovementR12Fixture fx;
    walker* npc = add_living(fx, FAMILY_SOLDIER);
    walker* user = add_living(fx, FAMILY_SOLDIER);
    ASSERT_TRUE(npc && user);
    assign_basic_ani(npc);
    assign_basic_ani(user);

    // walk() with no arguments repeats lastx/lasty; a facing change turns in
    // place without moving.
    npc->setxy(64, 64);
    npc->set_curdir(FACE_UP);
    npc->set_lastx(1.0f);
    npc->set_lasty(0.0f);
    ASSERT_TRUE(npc->walk());
    ASSERT_EQ(FACE_RIGHT, static_cast<int>(npc->curdir()))
        << "walk() must re-read lastx/lasty as the requested heading";
    ASSERT_EQ(64, npc->xpos());
    ASSERT_EQ(64, npc->ypos());

    // walker::shove is the non-living stub: it never shoves, it always
    // reports -1 and leaves the target where it was.
    const short target_x = user->xpos();
    ASSERT_EQ(-1, npc->shove(user, 1, 0));
    ASSERT_EQ(target_x, user->xpos()) << "the base shove must not move anyone";

    user->setxy(300, 300); // keep the pair from colliding in the corner below
    npc->setxy(0, 0);
    npc->set_stepsize(1.0f);
    npc->set_user(-1);

    // NPC fallback switch: curdir is aligned with each step, so a blocked
    // step really does reach the switch.
    npc->set_curdir(FACE_LEFT);
    ASSERT_TRUE(npc->walkstep(-1.0f, 0.0f))
        << "blocked LEFT falls back to DOWN";
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(1, npc->ypos());

    npc->set_curdir(FACE_UP);
    ASSERT_TRUE(npc->walkstep(0.0f, -1.0f))
        << "north is clear from (0,1): the direct walk succeeds, no fallback";
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(0, npc->ypos());

    npc->set_curdir(FACE_UP_RIGHT);
    ASSERT_TRUE(npc->walkstep(1.0f, -1.0f))
        << "the UP component is off-map, the RIGHT component carries the step";
    ASSERT_EQ(1, npc->xpos());
    ASSERT_EQ(0, npc->ypos());

    npc->set_curdir(FACE_DOWN_LEFT);
    ASSERT_TRUE(npc->walkstep(-1.0f, 1.0f))
        << "the diagonal itself is clear from (1,0)";
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(1, npc->ypos());

    npc->setxy(300, 340); // out of the user's way

    // User slide: the diagonal is blocked by the west edge, y is free.
    user->setxy(0, 1);
    user->set_stepsize(1.0f);
    user->set_user(0);
    user->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(user->walkstep(-1.0f, -1.0f))
        << "a slide still reports the requested diagonal failed";
    ASSERT_EQ(0, user->xpos()) << "x stayed: west of 0 is off-map";
    ASSERT_EQ(0, user->ypos()) << "y slid one pixel north";

    // BIT_ANIMATE: a blocked continue-step advances the walk frame anyway.
    user->stats()->set_bit_flags(BIT_ANIMATE, 1);
    const short east = static_cast<short>(fx.level.world().pixmaxx - 10);
    user->setxy(east, static_cast<short>(64));
    user->set_curdir(FACE_RIGHT);
    user->set_cycle(0);
    ASSERT_FALSE(user->walk(1.0f, 0.0f))
        << "the 16px footprint cannot occupy the east edge cell";
    ASSERT_EQ(east, user->xpos());
    ASSERT_EQ(1, static_cast<int>(user->cycle()))
        << "BIT_ANIMATE cycles the walk frame on a blocked step";
    user->stats()->set_bit_flags(BIT_ANIMATE, 0);

    // turn from an out-of-range facing: distance 127 - 0 = 127 >= 4, so
    // (127 + 1) % 8 == 0 == FACE_UP, and the FACE_UP arm writes the heading.
    user->set_curdir(127);
    ASSERT_TRUE(user->turn(FACE_UP));
    ASSERT_EQ(FACE_UP, static_cast<int>(user->curdir()));
    ASSERT_FLOAT_EQ(0.0f, user->lastx());
    ASSERT_FLOAT_EQ(-1.0f, user->lasty());
}
} // namespace detail_walker_movement_r12

// --- From test_walker_movement_r14.cpp ---
namespace detail_walker_movement_r14 {
namespace {

struct MovementR14Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    MovementR14Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &events, &cfg);
    }
};

walker* add_living(MovementR14Fixture& fx, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    w->set_sizex(16);
    w->set_sizey(16);
    w->set_stepsize(1.0f);
    w->setxy(x, y);
    walker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

void assign_basic_ani(walker* w)
{
    static std::array<std::array<signed char, 4>, 16> seqs{};
    static std::array<signed char*, 16> rows{};
    for (int i = 0; i < 16; ++i)
    {
        seqs[static_cast<std::size_t>(i)][0] = 0;
        seqs[static_cast<std::size_t>(i)][1] = 1;
        seqs[static_cast<std::size_t>(i)][2] = -1;
        seqs[static_cast<std::size_t>(i)][3] = -1;
        rows[static_cast<std::size_t>(i)] = seqs[static_cast<std::size_t>(i)].data();
    }
    w->ani = rows.data();
}

} // namespace

TEST(WalkerMovementUnit, walker_movement_r14_npc_fallback_denials_and_one_success)
{
    MovementR14Fixture fx;
    walker* npc = add_living(fx, 0, 0);
    ASSERT_TRUE(npc != nullptr);

    assign_basic_ani(npc);
    npc->set_user(-1);

    const short maxx = static_cast<short>(fx.level.world().pixmaxx);
    const short maxy = static_cast<short>(fx.level.world().pixmaxy);

    // UP in the NW corner: the LEFT fallback is off-map too.
    npc->set_curdir(FACE_UP);
    ASSERT_FALSE(npc->walkstep(0.0f, -1.0f));
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(0, npc->ypos());

    // RIGHT at the east edge: the UP fallback is off-map.
    npc->setxy(static_cast<short>(maxx - 1), static_cast<short>(0));
    npc->set_curdir(FACE_RIGHT);
    ASSERT_FALSE(npc->walkstep(1.0f, 0.0f));
    ASSERT_EQ(maxx - 1, npc->xpos());
    ASSERT_EQ(0, npc->ypos());

    // DOWN at the south edge: the RIGHT fallback is on-map but the 16px
    // footprint still hangs off the bottom, so query_grid_passable denies it.
    npc->setxy(static_cast<short>(0), static_cast<short>(maxy - 1));
    npc->set_curdir(FACE_DOWN);
    ASSERT_FALSE(npc->walkstep(0.0f, 1.0f));
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(maxy - 1, npc->ypos());

    // DOWN_RIGHT there: both components fail.
    npc->setxy(static_cast<short>(0), static_cast<short>(maxy - 1));
    npc->set_curdir(FACE_DOWN_RIGHT);
    ASSERT_FALSE(npc->walkstep(1.0f, 1.0f));
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(maxy - 1, npc->ypos());

    // UP_LEFT in the NW corner: both components fail.
    npc->setxy(0, 0);
    npc->set_curdir(FACE_UP_LEFT);
    ASSERT_FALSE(npc->walkstep(-1.0f, -1.0f));
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(0, npc->ypos());

    // Positive control: away from the corner the LEFT -> DOWN fallback works,
    // and curdir is restored to the entry facing on the way out.
    npc->setxy(0, 32);
    npc->set_curdir(FACE_LEFT);
    ASSERT_TRUE(npc->walkstep(-1.0f, 0.0f))
        << "the DOWN fallback is clear here; deleting the switch loses this";
    ASSERT_EQ(0, npc->xpos());
    ASSERT_EQ(33, npc->ypos());
    ASSERT_EQ(FACE_LEFT, static_cast<int>(npc->curdir()))
        << "the fallback facing is scratch state, restored before returning";
}

TEST(WalkerMovementUnit, walker_movement_r14_user_slide_axes_and_turn_default)
{
    MovementR14Fixture fx;
    walker* user = add_living(fx, 0, 0);
    ASSERT_TRUE(user != nullptr);

    assign_basic_ani(user);
    user->set_user(0);

    // A cardinal direction has no slide: the switch breaks with dx == dy == 0.
    user->set_curdir(FACE_UP);
    ASSERT_FALSE(user->walkstep(0.0f, -1.0f));
    ASSERT_EQ(0, user->xpos());
    ASSERT_EQ(0, user->ypos()) << "a cardinal user step never slides";

    // UP_RIGHT in the NW corner: y is off-map, x slides east. The slide still
    // reports failure (ret1/ret2 stay 0) even though the walker moved.
    user->set_curdir(FACE_UP_RIGHT);
    ASSERT_FALSE(user->walkstep(1.0f, -1.0f));
    ASSERT_EQ(1, user->xpos()) << "the x axis slid one pixel east";
    ASSERT_EQ(0, user->ypos()) << "the y axis was off-map";

    // DOWN_LEFT from the west edge: x is off-map, y slides south.
    user->setxy(0, 0);
    user->set_curdir(FACE_DOWN_LEFT);
    ASSERT_FALSE(user->walkstep(-1.0f, 1.0f));
    ASSERT_EQ(0, user->xpos()) << "the x axis was off-map";
    ASSERT_EQ(1, user->ypos()) << "the y axis slid one pixel south";

    // turn from 127 toward FACE_RIGHT: distance 125 >= 4 -> (127+1)%8 == 0,
    // so this lands on the FACE_UP arm, not the default one.
    user->set_curdir(127);
    ASSERT_TRUE(user->turn(FACE_RIGHT));
    ASSERT_EQ(FACE_UP, static_cast<int>(user->curdir()));
    ASSERT_FLOAT_EQ(0.0f, user->lastx());
    ASSERT_FLOAT_EQ(-1.0f, user->lasty());

    // The real default arm needs a facing whose modulo result is negative:
    // (-120 + 7) % 8 == -1, which no case label matches.
    user->set_stepsize(2.0f);
    user->set_curdir(static_cast<char>(-120));
    ASSERT_TRUE(user->turn(FACE_RIGHT));
    ASSERT_EQ(-1, static_cast<int>(user->curdir()))
        << "the classic turn keeps the negative modulo result";
    ASSERT_FLOAT_EQ(0.0f, user->lastx())
        << "the default arm writes the FACE_UP heading";
    ASSERT_FLOAT_EQ(-2.0f, user->lasty())
        << "the default arm writes lasty = -stepsize";
}
} // namespace detail_walker_movement_r14
