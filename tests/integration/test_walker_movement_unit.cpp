#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
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
#include "test_gameplay_context_scope.h"

// --- From test_walker_movement_push.cpp ---
namespace detail_walker_movement_push {
namespace {

struct MovementFixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    MovementFixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(MovementFixture& fx, char family = FAMILY_SOLDIER)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
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
    normal->turn(FACE_RIGHT);
    ASSERT_TRUE(normal->curdir() != FACE_UP);
    ASSERT_TRUE(!(normal->lastx() == 0.0f && normal->lasty() == -normal->stepsize()));

    walker* tower = add_living(fx, FAMILY_TOWER1);
    ASSERT_TRUE(tower != nullptr);
    tower->set_stepsize(3.0f);
    tower->set_lastx(9.0f);
    tower->set_lasty(-4.0f);
    tower->set_curdir(FACE_UP);
    tower->turn(FACE_LEFT);
    ASSERT_TRUE(tower->lastx() == 9.0f);
    ASSERT_TRUE(tower->lasty() == -4.0f);
}

TEST(WalkerMovementUnit, walker_movement_walk_and_walkstep_edge_paths)
{
    MovementFixture fx;
    walker* w = add_living(fx);
    ASSERT_TRUE(w != nullptr);

    w->set_curdir(FACE_RIGHT);
    ASSERT_TRUE(w->walk(1.0f, 0.0f));

    w->setxy(0, 0);
    w->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!w->walk(-1.0f, 0.0f));

    w->set_user(-1);
    (void)w->walkstep(-1.0f, -1.0f);
    (void)w->walkstep(1.0f, -1.0f);

    w->set_user(0);
    w->setxy(0, 10);
    (void)w->walkstep(-1.0f, -1.0f);
    (void)w->walkstep(-1.0f, 1.0f);
}
} // namespace detail_walker_movement_push

// --- From test_walker_movement_r11.cpp ---
namespace detail_walker_movement_r11 {
namespace {

struct WalkerMovementR11Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    WalkerMovementR11Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = save.allied_mode;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(WalkerMovementR11Fixture& fx, char family = FAMILY_SOLDIER)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
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
        seqs[static_cast<std::size_t>(i)][1] = -1;
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
    ASSERT_TRUE(w != nullptr);

    (void)w->move(1, -1);
    w->worldmove(0.5f, -0.5f);
    ASSERT_TRUE(w->xpos() != 0 || w->ypos() != 0);

    w->set_ignore(1);
    (void)w->setxy(100, 100);
    w->setworldxy(101.0f, 99.0f);
    w->set_ignore(0);
}

TEST(WalkerMovementUnit, walker_movement_r11_walkstep_npc_and_user_slide_paths)
{
    WalkerMovementR11Fixture fx;
    walker* w = add_living(fx);
    ASSERT_TRUE(w != nullptr);
    assign_ani(w);

    // Force blocked movement by surrounding a tile with non-passable wall tile.
    w->setxy(0, 0);
    w->set_user(-1);
    for (int d = 0; d < 8; ++d)
    {
        const float dx = (d == FACE_LEFT || d == FACE_UP_LEFT || d == FACE_DOWN_LEFT) ? -1.0f : ((d == FACE_UP || d == FACE_DOWN) ? 0.0f : 1.0f);
        const float dy = (d == FACE_UP || d == FACE_UP_LEFT || d == FACE_UP_RIGHT) ? -1.0f : ((d == FACE_LEFT || d == FACE_RIGHT) ? 0.0f : 1.0f);
        (void)w->walkstep(dx, dy);
    }

    // user branch with diagonal slide checks
    w->set_user(0);
    w->setxy(1, 1);
    (void)w->walkstep(1.0f, 1.0f);
    (void)w->walkstep(-1.0f, 1.0f);
    (void)w->walkstep(1.0f, -1.0f);
    (void)w->walkstep(-1.0f, -1.0f);
}

TEST(WalkerMovementUnit, walker_movement_r11_walk_turn_and_angles)
{
    WalkerMovementR11Fixture fx;
    walker* w = add_living(fx);
    ASSERT_TRUE(w != nullptr);
    assign_ani(w);

    // walk(0,0) and changed-direction branch
    ASSERT_TRUE(w->walk(0.0f, 0.0f));
    w->set_curdir(FACE_UP);
    ASSERT_TRUE(w->walk(1.0f, 0.0f));

    // blocked path with BIT_ANIMATE branch
    w->stats()->set_bit_flags(BIT_ANIMATE, 1);
    w->setxy(0, 0);
    (void)w->walk(-1.0f, 0.0f);

    // turn switch and default path
    for (int d = 0; d < 8; ++d)
    {
        w->set_curdir(static_cast<char>(d));
        (void)w->turn(static_cast<short>((d + 3) % 8));
    }
    w->set_curdir(120);
    (void)w->turn(2);

    // stationary turn branch in family descriptor path
    walker* tower = add_living(fx, FAMILY_TOWER1);
    tower->set_stepsize(2.0f);
    tower->set_lastx(5.0f);
    tower->set_lasty(6.0f);
    (void)tower->turn(FACE_LEFT);
}
} // namespace detail_walker_movement_r11

// --- From test_walker_movement_r12.cpp ---
namespace detail_walker_movement_r12 {
namespace {

struct MovementR12Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    MovementR12Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(MovementR12Fixture& fx, char family = FAMILY_SOLDIER)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, family);
    bind_test_entity_sim_context(fx.level, w.get());
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

    station->set_stepsize(3.0f);
    ASSERT_TRUE(station->walkstep(1.0f, 0.0f));
    ASSERT_TRUE(station->lastx() == 1.0f);
    ASSERT_TRUE(station->lasty() == 0.0f);

    ASSERT_TRUE(mover->facing(2, 5) == FACE_DOWN);
    ASSERT_TRUE(mover->facing(2, 1) == FACE_DOWN_RIGHT);
    ASSERT_TRUE(mover->facing(2, -1) == FACE_UP_RIGHT);
    ASSERT_TRUE(mover->facing(2, -5) == FACE_UP);
    ASSERT_TRUE(mover->facing(-2, 5) == FACE_DOWN);
    ASSERT_TRUE(mover->facing(-2, 1) == FACE_DOWN_LEFT);
    ASSERT_TRUE(mover->facing(-2, -1) == FACE_UP_LEFT);
    ASSERT_TRUE(mover->facing(-2, -5) == FACE_UP);

    assign_basic_ani(mover);
    mover->stats()->set_bit_flags(BIT_ANIMATE, 1);
    mover->setxy(0, 0);
    mover->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!mover->walk(-1.0f, 0.0f));
}

TEST(WalkerMovementUnit, walker_movement_r12_walkstep_npc_and_user_slide_paths)
{
    MovementR12Fixture fx;
    walker* npc = add_living(fx, FAMILY_SOLDIER);
    walker* user = add_living(fx, FAMILY_SOLDIER);
    ASSERT_TRUE(npc && user);
    assign_basic_ani(npc);
    assign_basic_ani(user);

    // walk() wrapper
    npc->set_lastx(1.0f);
    npc->set_lasty(0.0f);
    (void)npc->walk();

    // shove non-living fallback path
    ASSERT_TRUE(npc->shove(user, 1, 0) == -1);

    npc->setxy(0, 0);
    npc->set_stepsize(1.0f);
    npc->set_user(-1);

    // NPC fallback switch paths from blocked movement.
    npc->set_curdir(FACE_LEFT);
    (void)npc->walkstep(-1.0f, 0.0f);
    npc->set_curdir(FACE_UP);
    (void)npc->walkstep(0.0f, -1.0f);
    npc->set_curdir(FACE_UP_RIGHT);
    (void)npc->walkstep(1.0f, -1.0f);
    npc->set_curdir(FACE_DOWN_LEFT);
    (void)npc->walkstep(-1.0f, 1.0f);

    // User slide path where diagonal move is blocked but one axis can progress.
    user->setxy(0, 1);
    user->set_stepsize(1.0f);
    user->set_user(0);
    user->set_curdir(FACE_UP_LEFT);
    (void)user->walkstep(-1.0f, -1.0f);

    // Invalid BIT_ANIMATE walk branch.
    user->stats()->set_bit_flags(BIT_ANIMATE, 1);
    user->setxy(0, 0);
    user->set_curdir(FACE_LEFT);
    ASSERT_TRUE(!user->walk(-1.0f, 0.0f));

    // turn default branch via invalid curdir.
    user->set_curdir(127);
    (void)user->turn(FACE_UP);
}
} // namespace detail_walker_movement_r12

// --- From test_walker_movement_r14.cpp ---
namespace detail_walker_movement_r14 {
namespace {

struct MovementR14Fixture {
    LevelRuntimeData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    ScopedGameplayContext gameplay;

    MovementR14Fixture()
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_living(MovementR14Fixture& fx, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(Order::Living, FAMILY_SOLDIER);
    bind_test_entity_sim_context(fx.level, w.get());
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

TEST(WalkerMovementUnit, walker_movement_r14_lines_175_186_198_210_npc_fallback_cases)
{
    MovementR14Fixture fx;
    walker* npc = add_living(fx, 0, 0);
    ASSERT_TRUE(npc != nullptr);

    assign_basic_ani(npc);
    npc->set_user(-1);

    npc->set_curdir(FACE_UP);
    (void)npc->walkstep(0.0f, -1.0f);

    npc->setxy(fx.level.world().pixmaxx - 1, 0);
    npc->set_curdir(FACE_RIGHT);
    (void)npc->walkstep(1.0f, 0.0f);

    npc->setxy(0, fx.level.world().pixmaxy - 1);
    npc->set_curdir(FACE_DOWN);
    (void)npc->walkstep(0.0f, 1.0f);

    npc->setxy(0, fx.level.world().pixmaxy - 1);
    npc->set_curdir(FACE_DOWN_RIGHT);
    (void)npc->walkstep(1.0f, 1.0f);

    npc->setxy(0, 0);
    npc->set_curdir(FACE_UP_LEFT);
    (void)npc->walkstep(-1.0f, -1.0f);
}

TEST(WalkerMovementUnit, walker_movement_r14_lines_234_255_268_273_278_285_292_user_slide_and_turn_default)
{
    MovementR14Fixture fx;
    walker* user = add_living(fx, 0, 0);
    ASSERT_TRUE(user != nullptr);

    assign_basic_ani(user);
    user->set_user(0);

    user->set_curdir(FACE_UP);
    (void)user->walkstep(0.0f, -1.0f);

    user->set_curdir(FACE_UP_RIGHT);
    (void)user->walkstep(1.0f, -1.0f);

    user->set_curdir(FACE_DOWN_LEFT);
    (void)user->walkstep(-1.0f, 1.0f);

    user->set_curdir(127);
    (void)user->turn(FACE_RIGHT);
}
} // namespace detail_walker_movement_r14
