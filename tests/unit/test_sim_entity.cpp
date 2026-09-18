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

// The context is the CHANNEL sim code pushes through: every sound, banner and
// redraw request in gameplay is `current_game->sim_events->push(...)`, and the
// runtime drains the log it bound. So the pins read the BOUND log directly —
// a push that came back out of current_game->sim_events would be satisfied by
// a context bound to any log at all, including one nobody drains.
TEST(SimEntity, event_log_binding_routes_pushes_to_the_bound_log)
{
    GameWorld world(7);
    GameplayContext game_ctx;
    game_ctx.world = &world;

    og::sim::SimEventLog log;
    game_ctx.sim_events = &log;
    log.current_tick_ = 9;

    GameplayContext* const prev = current_game;
    current_game = &game_ctx;

    ASSERT_NE(nullptr, current_game) << "the context must be installed";
    current_game->sim_events->push(og::sim::EventKind::PlaySound, 42, 7);
    current_game->sim_events->push_notification("ARENA", 60, 2);
    current_game = prev;

    ASSERT_EQ(2u, log.size())
        << "both pushes must land in the log the context bound, not elsewhere";
    EXPECT_EQ(og::sim::EventKind::PlaySound, log.events()[0].kind);
    EXPECT_EQ(42u, log.events()[0].a) << "the sound id rides in a";
    EXPECT_EQ(7u, log.events()[0].b);
    EXPECT_EQ(9u, log.events()[0].tick)
        << "push stamps the log's current tick, so the drain knows the frame";
    EXPECT_EQ(-1, log.events()[0].target_player)
        << "an unaddressed event broadcasts to every view";
    EXPECT_EQ(og::sim::EventKind::Notification, log.events()[1].kind);
    EXPECT_EQ("ARENA", log.events()[1].text) << "the banner text rides along";
    EXPECT_EQ(60u, log.events()[1].a) << "the duration override rides in a";
    EXPECT_EQ(2, log.events()[1].target_player)
        << "a seat-addressed banner keeps its addressee";
}

// ---------------------------------------------------------------------------
// Headless walker creation tests (G4)
// Verify walker can be created without SDL, without pixieN rendering data.
// ---------------------------------------------------------------------------

// The headless contract, and the identity sentinels the sim reads off a
// brand-new entity: a walker built with no PixieData carries NO render
// component at all (that is what lets the dedicated server and every unit
// group link gameplay without SDL), and it comes up as an UNOWNED NPC —
// user() == -1 is the "npc" test in walker_movement.cpp and the
// "seat may claim this body" test in sim_input_handler/sim_control_policy,
// while real_team_num() == 255 is the "not a charmed foe" sentinel
// (walker.cpp). A fresh entity defaulting to user 0 / team 0 would hand
// player 0 control of every object the level spawns.
TEST(SimEntity, walker_headless_construction_is_an_unowned_npc_with_no_render)
{
    walker w;  // No PixieData — headless mode

    EXPECT_FALSE(w.has_render())
        << "a walker built without PixieData must stay render-free";
    EXPECT_EQ(nullptr, w.bmp_data());
    EXPECT_EQ(nullptr, w.render_component());

    EXPECT_EQ(0, w.xpos());
    EXPECT_EQ(0, w.ypos());
    EXPECT_EQ(0, w.dead());
    EXPECT_EQ(0u, w.entity_id()) << "ids are handed out by the world, not the ctor";
    EXPECT_EQ(-1, w.user()) << "a fresh body is an NPC until a seat claims it";
    EXPECT_EQ(0, w.team_num());
    EXPECT_EQ(255, w.real_team_num())
        << "255 is the 'no charm/no original team' sentinel";
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

// Every statistics setter routes its dirty mark to the OWNING walker
// (statistics::mark_dirty, stats.cpp): the delta writer reads the walker's
// mask, so a stat whose mark never reaches the owner is a hitpoint change no
// networked mirror ever sees (the HUD bar freezes on the joiner's screen).
TEST(SimEntity, walker_stats_route_their_dirty_marks_to_the_owning_walker)
{
    walker w;
    statistics* const st = w.stats();
    ASSERT_NE(nullptr, st) << "a headless walker still owns its statistics";

    w.clear_dirty();
    st->set_hitpoints(50.0f);
    EXPECT_FLOAT_EQ(50.0f, st->hitpoints());
    EXPECT_TRUE(w.is_dirty(og::dirty::BIT_HITPOINTS))
        << "set_hitpoints must mark HITPOINTS on the owning walker";
    EXPECT_FALSE(w.is_dirty(og::dirty::BIT_MAX_HITPOINTS))
        << "stat marks are per field — set_hitpoints must not mark MAX";

    st->set_max_hitpoints(100.0f);
    EXPECT_FLOAT_EQ(100.0f, st->max_hitpoints());
    EXPECT_TRUE(w.is_dirty(og::dirty::BIT_MAX_HITPOINTS))
        << "set_max_hitpoints must mark MAX_HITPOINTS on the owner";

    // adjust_* is set(current + delta), so it carries the mark too.
    w.clear_dirty();
    EXPECT_EQ(0u, w.dirty_mask_word(0));
    EXPECT_EQ(0u, w.dirty_mask_word(1));
    st->adjust_hitpoints(-20.0f);
    EXPECT_FLOAT_EQ(30.0f, st->hitpoints())
        << "adjust_hitpoints adds the delta to the stored value";
    EXPECT_TRUE(w.is_dirty(og::dirty::BIT_HITPOINTS))
        << "a damage adjustment must ship in the next delta";
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
