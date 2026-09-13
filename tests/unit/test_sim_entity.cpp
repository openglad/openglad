// Headless tests for SimEntity and walker (G4).
// These verify that SimEntity/walker can be created and manipulated without SDL.

#include <openglad/gameplay/sim_entity.h>
#include <openglad/gameplay/dirty_field_bits.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/statistics.h>
#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"

#include <algorithm>
#include <cstdint>

namespace {

// The LCG GameWorld::rng_ runs (game_world.h SimRandom): glibc constants,
// draw taken from the high half. Spelled out here so the expectations below
// are COMPUTED from the contract, not transcribed from a run.
constexpr std::uint32_t lcg_step(std::uint32_t state) noexcept
{
    return state * 1103515245u + 12345u;
}

constexpr std::uint32_t lcg_draw(std::uint32_t state,
                                 std::uint32_t max_exclusive) noexcept
{
    return (lcg_step(state) >> 16) % max_exclusive;
}

// True when `cell` (an obmap pile) holds `w`.
bool pile_holds(const std::list<walker*>& cell, const walker* w)
{
    return std::find(cell.begin(), cell.end(), w) != cell.end();
}

} // namespace

TEST(SimEntity, default_construction)
{
    og::sim::SimEntity e;
    ASSERT_TRUE(e.xpos() == 0);
    ASSERT_TRUE(e.ypos() == 0);
    ASSERT_TRUE(e.entity_id() == 0);
    ASSERT_TRUE(e.dead() == 0);
    ASSERT_TRUE(e.user() == -1);
    ASSERT_TRUE(e.team_num() == 0);
    ASSERT_TRUE(e.real_team_num() == 255);
}

// Every position/size setter stores the value AND marks its dirty bit: the
// delta writer ships only marked fields (world_snapshot.cpp copies
// dirty_mask_word() into the entity snapshot), so a setter that forgets its
// mark_dirty silently stops crossing the wire to networked mirrors.
TEST(SimEntity, set_position_stores_and_marks_its_dirty_bit)
{
    og::sim::SimEntity e;
    e.clear_dirty();
    e.set_xpos(100);
    e.set_ypos(200);
    e.set_sizex(16);
    e.set_sizey(16);

    EXPECT_EQ(100, e.xpos());
    EXPECT_EQ(200, e.ypos());
    EXPECT_EQ(16, e.sizex());
    EXPECT_EQ(16, e.sizey());

    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_XPOS)) << "set_xpos must mark XPOS";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_YPOS)) << "set_ypos must mark YPOS";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_SIZEX))
        << "set_sizex must mark SIZEX";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_SIZEY))
        << "set_sizey must mark SIZEY";
    EXPECT_NE(0u, e.dirty_mask_word(0))
        << "the word the delta writer copies must carry the marks";

    // The marks are per-field, and clear_dirty really clears them.
    e.clear_dirty();
    EXPECT_EQ(0u, e.dirty_mask_word(0));
    EXPECT_EQ(0u, e.dirty_mask_word(1));
    EXPECT_FALSE(e.is_dirty(og::dirty::BIT_XPOS));
    e.set_xpos(101);
    EXPECT_EQ(101, e.xpos());
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_XPOS));
    EXPECT_FALSE(e.is_dirty(og::dirty::BIT_YPOS))
        << "dirty bits are per field — set_xpos must not mark YPOS";
    EXPECT_FALSE(e.is_dirty(og::dirty::BIT_SIZEX))
        << "dirty bits are per field — set_xpos must not mark SIZEX";
}

// Team/user identity rides the same store-and-mark contract: an unmarked
// team or user change is a control claim a joining mirror never sees.
TEST(SimEntity, team_and_identity_store_and_mark_their_dirty_bits)
{
    og::sim::SimEntity e;
    e.clear_dirty();
    e.set_team_num(1);
    e.set_real_team_num(255);
    e.set_user(0);      // Player 0

    EXPECT_EQ(1, e.team_num());
    EXPECT_EQ(255, e.real_team_num());
    EXPECT_EQ(0, e.user());

    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_TEAM_NUM))
        << "set_team_num must mark TEAM_NUM";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_REAL_TEAM_NUM))
        << "set_real_team_num must mark REAL_TEAM_NUM";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_USER))
        << "set_user must mark USER";

    e.clear_dirty();
    e.set_user(2);
    EXPECT_EQ(2, e.user());
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_USER));
    EXPECT_FALSE(e.is_dirty(og::dirty::BIT_TEAM_NUM))
        << "dirty bits are per field — set_user must not mark TEAM_NUM";
}

// The transient state flags, same contract: a death (or a spent
// invulnerability) that never marks its bit leaves a mirror drawing a live
// walker forever.
TEST(SimEntity, state_flags_store_and_mark_their_dirty_bits)
{
    og::sim::SimEntity e;
    e.clear_dirty();
    e.set_invulnerable_left(30);
    e.set_flight_left(15);
    e.set_invisibility_left(0);
    e.set_bonus_rounds(2);
    e.set_dead(0);

    EXPECT_EQ(30, e.invulnerable_left());
    EXPECT_EQ(15, e.flight_left());
    EXPECT_EQ(0, e.invisibility_left());
    EXPECT_EQ(2, e.bonus_rounds());
    EXPECT_EQ(0, e.dead());

    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_INVULNERABLE_LEFT))
        << "set_invulnerable_left must mark INVULNERABLE_LEFT";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_FLIGHT_LEFT))
        << "set_flight_left must mark FLIGHT_LEFT";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_INVISIBILITY_LEFT))
        << "set_invisibility_left must mark INVISIBILITY_LEFT even for 0";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_BONUS_ROUNDS))
        << "set_bonus_rounds must mark BONUS_ROUNDS";
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_DEAD))
        << "set_dead must mark DEAD";

    e.clear_dirty();
    e.set_dead(1);
    EXPECT_EQ(1, e.dead());
    EXPECT_TRUE(e.is_dirty(og::dirty::BIT_DEAD))
        << "a death must ship in the next delta";
    EXPECT_FALSE(e.is_dirty(og::dirty::BIT_FLIGHT_LEFT))
        << "dirty bits are per field — set_dead must not mark FLIGHT_LEFT";
}

TEST(SimEntity, event_log_binding)
{
    GameWorld world(7);
    GameplayContext game_ctx;
    game_ctx.world = &world;

    og::sim::SimEventLog log;
    game_ctx.sim_events = &log;

    GameplayContext* prev = current_game;
    current_game = &game_ctx;

    ASSERT_TRUE(current_game != nullptr);
    ASSERT_TRUE(current_game->world == &world);
    ASSERT_TRUE(current_game->sim_events == &log);

    current_game->sim_events->push(og::sim::EventKind::PlaySound, 42);
    ASSERT_TRUE(current_game->sim_events->size() == 1);
    ASSERT_TRUE(current_game->sim_events->events()[0].a == 42);

    current_game = prev;
}

// ---------------------------------------------------------------------------
// Headless walker creation tests (G4)
// Verify walker can be created without SDL, without pixieN rendering data.
// ---------------------------------------------------------------------------

TEST(SimEntity, walker_headless_construction)
{
    walker w;  // No PixieData — headless mode

    ASSERT_TRUE(w.xpos() == 0);
    ASSERT_TRUE(w.ypos() == 0);
    ASSERT_TRUE(w.dead() == 0);
    ASSERT_TRUE(w.user() == -1);
    ASSERT_TRUE(!w.has_render());
    ASSERT_TRUE(w.bmp_data() == nullptr);
    ASSERT_TRUE(w.render_component() == nullptr);
}

// walker::setxy is the position AUTHORITY (walker_movement.cpp): it writes
// the float worldx/worldy the sim integrates FIRST, then the snapped
// xpos/ypos. Both halves, or the sim moves from stale float coordinates.
TEST(SimEntity, walker_headless_setxy_writes_world_and_snapped_position)
{
    walker w;
    w.setxy(100, 200);
    EXPECT_EQ(100, w.xpos());
    EXPECT_EQ(200, w.ypos());
    EXPECT_FLOAT_EQ(100.0f, w.worldx())
        << "setxy must write the float authority, not only the snapped copy";
    EXPECT_FLOAT_EQ(200.0f, w.worldy());

    w.setxy(50, 75);
    EXPECT_EQ(50, w.xpos());
    EXPECT_EQ(75, w.ypos());
    EXPECT_FLOAT_EQ(50.0f, w.worldx());
    EXPECT_FLOAT_EQ(75.0f, w.worldy());
}

// The obmap arm of the same authority: a live walker is MOVED between
// collision piles, walker::move steps relative to the current spot, and a
// dormant (delayed-spawn) walker is REMOVED instead of re-registered.
TEST(SimEntity, walker_movement_keeps_the_obmap_in_step)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();
    ASSERT_NE(nullptr, world.myobmap) << "the fixture world owns an obmap";
    obmap& map = *world.myobmap;

    walker* const w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(96, 96);
    EXPECT_EQ(1u, map.walker_to_pos.count(w))
        << "a live walker must be registered where setxy put it";
    EXPECT_TRUE(pile_holds(map.obmap_get_list(96, 96), w))
        << "the (96, 96) pile must hold the walker";

    w->setxy(160, 160);
    EXPECT_EQ(160, w->xpos());
    EXPECT_FALSE(pile_holds(map.obmap_get_list(96, 96), w))
        << "setxy must MOVE the walker, not leave a ghost in the old pile";
    EXPECT_TRUE(pile_holds(map.obmap_get_list(160, 160), w));

    // walker::move is relative: +16 px in x off the current position.
    EXPECT_TRUE(w->move(16, 0));
    EXPECT_EQ(176, w->xpos());
    EXPECT_EQ(160, w->ypos());
    EXPECT_FLOAT_EQ(176.0f, w->worldx());
    EXPECT_TRUE(pile_holds(map.obmap_get_list(176, 160), w));

    // Dormant walkers hold the "never in the obmap" invariant.
    w->set_dormant(true);
    w->setxy(200, 200);
    EXPECT_EQ(0u, map.walker_to_pos.count(w))
        << "dormant walkers are removed, never re-registered";
    EXPECT_EQ(200, w->xpos()) << "the position still moves while dormant";
    EXPECT_FLOAT_EQ(200.0f, w->worldx());
}

// GameWorld::rng_ IS the determinism contract every snapshot, replay and
// parity golden rests on: the glibc LCG stepped from the world seed, with
// the draw taken from the high half of the state. No walker involved.
TEST(SimEntity, sim_random_lcg_arithmetic_is_pinned)
{
    GameWorld world(42);
    EXPECT_EQ(42u, world.rng_.state_) << "the world seeds the LCG state";

    EXPECT_EQ(lcg_draw(42u, 100u), world.rng_.next(100))
        << "next(max) must be (state * 1103515245 + 12345 >> 16) % max";
    EXPECT_EQ(49u, lcg_draw(42u, 100u)) << "the arithmetic itself";
    EXPECT_EQ(lcg_step(42u), world.rng_.state_)
        << "every draw must advance the state exactly one LCG step";
    EXPECT_EQ(3397979675u, lcg_step(42u)) << "the arithmetic itself";

    const std::uint32_t after_one = world.rng_.state_;
    EXPECT_EQ(0u, world.rng_.next(0)) << "next(0) is defined as 0";
    EXPECT_EQ(after_one, world.rng_.state_)
        << "next(0) must draw nothing — it may not perturb the stream";

    EXPECT_EQ(lcg_draw(after_one, 8u), world.rng_.next(8));
    EXPECT_EQ(lcg_step(after_one), world.rng_.state_);
}

TEST(SimEntity, walker_headless_stats)
{
    walker w;
    statistics* st = w.stats();
    ASSERT_TRUE(st != nullptr);
    st->set_hitpoints(50);
    st->set_max_hitpoints(100);
    ASSERT_TRUE(st->hitpoints() == 50);
    ASSERT_TRUE(st->max_hitpoints() == 100);
}

TEST(SimEntity, walker_headless_frame_tracking)
{
    walker w;
    ASSERT_TRUE(w.frame() == 0);

    // set_frame validates against frames count; headless walker has 0 frames
    short result = w.set_frame(2);
    ASSERT_TRUE(result == 0);           // Should fail — no frames allocated
    ASSERT_TRUE(w.frame() == 0);  // Frame unchanged
}
