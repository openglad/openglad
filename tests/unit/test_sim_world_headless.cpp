#include <openglad/gameplay/event.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include "../test_game_world_fixture.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// --- EventKind values ---

TEST(SimWorldHeadless, event_kind_set_palette_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::SetPalette) == 11);
}

TEST(SimWorldHeadless, event_kind_request_redraw_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::RequestRedraw) == 12);
}

// --- SimEventLog handles new event types ---

TEST(SimWorldHeadless, sim_event_log_set_palette_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;
    log.push(og::sim::EventKind::SetPalette, 0, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(ev.a == 0);
    ASSERT_TRUE(ev.tick == 1);
}

TEST(SimWorldHeadless, sim_event_log_set_palette_blue)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 2;
    log.push(og::sim::EventKind::SetPalette, 1, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(ev.a == 1);
}

TEST(SimWorldHeadless, sim_event_log_request_redraw_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 3;
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::RequestRedraw);
    ASSERT_TRUE(ev.tick == 3);
}

// --- emit_event convenience helper ---

TEST(SimWorldHeadless, emit_event_set_palette)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 5;

    og::sim::emit_event(&log, og::sim::EventKind::SetPalette, 1);

    ASSERT_TRUE(log.size() == 1);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(log.events()[0].a == 1);
}

TEST(SimWorldHeadless, emit_event_request_redraw)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 7;

    og::sim::emit_event(&log, og::sim::EventKind::RequestRedraw);

    ASSERT_TRUE(log.size() == 1);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::RequestRedraw);
}

// --- Mixed event stream with new types ---

TEST(SimWorldHeadless, mixed_event_stream_with_new_types)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;

    log.push_sound(10);
    log.push_notification("freeze!");
    log.push(og::sim::EventKind::SetPalette, 1, 0);
    log.push(og::sim::EventKind::RequestRedraw, 0, 0);

    ASSERT_TRUE(log.size() == 4);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::PlaySound);
    ASSERT_TRUE(log.events()[1].kind == og::sim::EventKind::Notification);
    ASSERT_TRUE(log.events()[2].kind == og::sim::EventKind::SetPalette);
    ASSERT_TRUE(log.events()[3].kind == og::sim::EventKind::RequestRedraw);
}

// --- SimRandom deterministic RNG ---

TEST(SimWorldHeadless, sim_random_same_seed_same_sequence)
{
    og::sim::SimRandom rng1(42);
    og::sim::SimRandom rng2(42);

    // Same seed must produce identical sequences
    for (int i = 0; i < 100; i++)
    {
        ASSERT_TRUE(rng1.next(1000) == rng2.next(1000));
    }
}

TEST(SimWorldHeadless, sim_random_different_seeds_differ)
{
    og::sim::SimRandom rng1(42);
    og::sim::SimRandom rng2(99);

    // Different seeds should produce different first values
    ASSERT_TRUE(rng1.next(1000) != rng2.next(1000));
}

TEST(SimWorldHeadless, sim_random_reset_reproduces)
{
    og::sim::SimRandom rng(123);
    auto v1 = rng.next(1000);
    auto v2 = rng.next(1000);
    auto v3 = rng.next(1000);

    rng.state_ = 123;
    ASSERT_TRUE(rng.next(1000) == v1);
    ASSERT_TRUE(rng.next(1000) == v2);
    ASSERT_TRUE(rng.next(1000) == v3);
}

TEST(SimWorldHeadless, sim_random_zero_max_returns_zero)
{
    og::sim::SimRandom rng(42);
    ASSERT_TRUE(rng.next(0) == 0);
}

TEST(SimWorldHeadless, sim_world_owns_rng)
{
    GameWorld world1(42);
    GameWorld world2(42);

    // Both worlds should have the same RNG state
    ASSERT_TRUE(world1.rng_.state_ == world2.rng_.state_);

    // After advancing one, they should differ
    world1.rng_.next(100);
    ASSERT_TRUE(world1.rng_.state_ != world2.rng_.state_);
}

// --- EndGame and SetEnd-adjacent event kinds (Phase 2: G5, G6) ---

TEST(SimWorldHeadless, event_kind_endgame_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::EndGame) == 13);
}

TEST(SimWorldHeadless, event_kind_set_end_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::SetEnd) == 15);
}

TEST(SimWorldHeadless, event_kind_request_exit_confirmation_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::RequestExitConfirmation) == 16);
}

TEST(SimWorldHeadless, event_kind_withdraw_to_level_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::WithdrawToLevel) == 17);
}

TEST(SimWorldHeadless, event_kind_score_change_value)
{
    ASSERT_TRUE(static_cast<std::uint32_t>(og::sim::EventKind::ScoreChange) == 18);
}

TEST(SimWorldHeadless, emit_endgame_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 10;
    og::sim::emit_event(&log, og::sim::EventKind::EndGame, 0, 5);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::EndGame);
    ASSERT_TRUE(ev.a == 0);  // ending type: normal
    ASSERT_TRUE(ev.b == 5);  // next_level
    ASSERT_TRUE(ev.tick == 10);
}

TEST(SimWorldHeadless, emit_endgame_save_all_failure)
{
    // Simulates walker::death() emitting EndGame with SCEN_TYPE_SAVE_ALL
    og::sim::SimEventLog log;
    log.current_tick_ = 20;
    og::sim::emit_event(&log, og::sim::EventKind::EndGame,
                        4, static_cast<std::uint32_t>(-1));

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::EndGame);
    ASSERT_TRUE(ev.a == 4);  // SCEN_TYPE_SAVE_ALL
    ASSERT_TRUE(ev.b == static_cast<std::uint32_t>(-1));  // no next level
}

TEST(SimWorldHeadless, emit_set_end_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 25;
    og::sim::emit_event(&log, og::sim::EventKind::SetEnd);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::SetEnd);
    ASSERT_TRUE(ev.a == 0);
    ASSERT_TRUE(ev.b == 0);
}

TEST(SimWorldHeadless, emit_request_exit_confirmation_with_text)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 27;
    og::sim::emit_event_text(&log, og::sim::EventKind::RequestExitConfirmation,
                             "Exit to Level 3?", 3, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::RequestExitConfirmation);
    ASSERT_TRUE(ev.a == 3);
    ASSERT_TRUE(ev.b == 0);
    ASSERT_TRUE(ev.text == "Exit to Level 3?");
}

TEST(SimWorldHeadless, emit_withdraw_to_level_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 28;
    og::sim::emit_event(&log, og::sim::EventKind::WithdrawToLevel, 5, 0);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::WithdrawToLevel);
    ASSERT_TRUE(ev.a == 5);
    ASSERT_TRUE(ev.b == 0);
}

TEST(SimWorldHeadless, emit_score_change_event)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 29;
    og::sim::emit_event(&log, og::sim::EventKind::ScoreChange, 2, 125);

    ASSERT_TRUE(log.size() == 1);
    const auto& ev = log.events()[0];
    ASSERT_TRUE(ev.kind == og::sim::EventKind::ScoreChange);
    ASSERT_TRUE(ev.a == 2);
    ASSERT_TRUE(ev.b == 125);
}

TEST(SimWorldHeadless, mixed_stream_with_phase2_events)
{
    og::sim::SimEventLog log;
    log.current_tick_ = 1;

    log.push_sound(10);
    log.push_notification("level complete!");
    log.push(og::sim::EventKind::ScoreChange, 1, 250);
    log.push(og::sim::EventKind::SetEnd, 0, 0);
    log.push(og::sim::EventKind::EndGame, 0, 3);

    ASSERT_TRUE(log.size() == 5);
    ASSERT_TRUE(log.events()[0].kind == og::sim::EventKind::PlaySound);
    ASSERT_TRUE(log.events()[1].kind == og::sim::EventKind::Notification);
    ASSERT_TRUE(log.events()[2].kind == og::sim::EventKind::ScoreChange);
    ASSERT_TRUE(log.events()[2].a == 1);
    ASSERT_TRUE(log.events()[2].b == 250);
    ASSERT_TRUE(log.events()[3].kind == og::sim::EventKind::SetEnd);
    ASSERT_TRUE(log.events()[4].kind == og::sim::EventKind::EndGame);
    ASSERT_TRUE(log.events()[4].b == 3);
}

// --- emit_* with null log is a no-op ---

TEST(SimWorldHeadless, emit_sound_null_log_is_noop)
{
    // Should not crash
    og::sim::emit_sound(nullptr, 42);
}

TEST(SimWorldHeadless, emit_notification_null_log_is_noop)
{
    og::sim::emit_notification(nullptr, "test");
}

TEST(SimWorldHeadless, emit_event_null_log_is_noop)
{
    og::sim::emit_event(nullptr, og::sim::EventKind::PlaySound, 1);
}

// --- Grid passability ---

// Authored Specials (reserved-team markers, ACT_RANDOM) pathfind with no
// owner; the arrow-wall pass-through gamble dereferenced the null owner and
// crashed the sim (real data: Tryxian Chronicles scen108/scen115). Unowned
// non-living entities must treat arrow walls as solid instead.
TEST(SimWorldHeadless, arrow_wall_is_solid_for_unowned_special)
{
    TestGameWorld t;
    PixieData& grid = t.world().grid;
    const int gx = 2;
    const int gy = 2;
    grid.data[static_cast<std::size_t>(gx + grid.w * gy)] = PIX_WALL4;

    walker* special = t.world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
    ASSERT_NE(nullptr, special);
    ASSERT_EQ(nullptr, special->owner());

    EXPECT_FALSE(t.world().query_grid_passable(
        static_cast<float>(gx * GRID_SIZE),
        static_cast<float>(gy * GRID_SIZE), special));
}

// --- Generator spawn-rate multiplier (difficulty submenu) ---

// The generator cadence gate scales the Bernoulli COMPARISON by
// world.generator_rate percent (level_draw * rate > threshold_draw * 100);
// the draw bounds themselves never change. At the defaults (0 = unset and
// the explicit 100) both sides carry the same factor, so the world RNG
// stream must be byte-identical draw for draw. A non-default rate must
// diverge (different success odds -> extra direction draws on success).
TEST(SimWorldHeadless, generator_rate_default_keeps_rng_stream_identical)
{
    constexpr int kTicks = 400;
    const auto run = [](short rate) {
        TestGameWorld t;
        GameWorld& world = t.world();
        world.generator_rate = rate;

        walker* generator = world.add_ob(Order::Generator, FAMILY_TOWER);
        EXPECT_NE(nullptr, generator);
        generator->setxy(80, 80);
        generator->stats()->set_level(5);
        generator->set_act_type(ACT_GENERATE);

        world.rng_.state_ = 0xC0FFEE42u;
        std::vector<std::uint32_t> states;
        states.reserve(kTicks);
        for (int i = 0; i < kTicks; ++i)
        {
            generator->act();
            // Test-built walkers carry no animation table, so a successful
            // roll would otherwise wedge in ANI_ATTACK; reset symmetrically
            // in every run so the cadence gate rolls each tick.
            generator->set_ani_type(ANI_WALK);
            states.push_back(world.rng_.state_);
        }
        return states;
    };

    const std::vector<std::uint32_t> baseline = run(0);
    EXPECT_EQ(baseline, run(100))
        << "rate 100 must be an exact integer identity with the legacy bound";
    EXPECT_NE(baseline, run(200))
        << "a non-default rate must change the generator RNG stream";
}

// Calm (rate 50) must REDUCE a generator's cadence, never silence it: the
// rate scales the comparison, not the draw bound, so even a level-1
// generator (level_draw in {0,1,2}) can still win the roll. A scaled BOUND
// would collapse to next(1) == 0 at level 1 and never fire — and the
// sanitize floor (25) would reach the forbidden SimRandom::next(0). Success
// is observed via the +1 self-heal on every successful roll.
TEST(SimWorldHeadless, generator_rate_calm_keeps_level1_generators_live)
{
    constexpr int kTicks = 8000;
    const auto successes = [](short rate) {
        TestGameWorld t;
        GameWorld& world = t.world();
        world.generator_rate = rate;

        walker* generator = world.add_ob(Order::Generator, FAMILY_TOWER);
        EXPECT_NE(nullptr, generator);
        generator->setxy(80, 80);
        generator->stats()->set_level(1);
        generator->set_act_type(ACT_GENERATE);
        generator->stats()->set_max_hitpoints(30000);
        generator->stats()->set_hitpoints(100);

        world.rng_.state_ = 0xC0FFEE42u;
        for (int i = 0; i < kTicks; ++i)
        {
            generator->act();
            generator->set_ani_type(ANI_WALK); // keep the cadence gate rolling
        }
        return generator->stats()->hitpoints() - 100;
    };

    const auto baseline = successes(0);
    const auto calm = successes(50);
    const auto sanitize_floor = successes(25);
    ASSERT_GT(baseline, 0) << "a level-1 generator fires on the default rate";
    EXPECT_GT(calm, 0) << "Calm halves the odds; it must not go silent";
    EXPECT_LT(calm, baseline) << "Calm fires less often than the default";
    EXPECT_GT(sanitize_floor, 0)
        << "the lobby-sanitize floor (25) must keep level-1 generators live";
    EXPECT_LE(sanitize_floor, calm)
        << "a lower rate can never out-fire a higher one on the same stream";
}

// The exit pad's third branch: foes alive AND destination unearned used to be
// completely SILENT — the player walked over the exit with no acknowledgment
// (the tower playtest's "exit seems blocked" report). It now says why, on the
// existing skip_exit(10) cadence, without touching the two event-emitting
// branches (both parity-pinned: exit prompt + withdraw pair).
TEST(SimWorldHeadless, exit_with_foes_alive_says_foes_remain)
{
    TestGameWorld t;
    const auto notifications = [&](const char* needle) {
        int count = 0;
        for (const auto& ev : t.events.events())
            if (ev.kind == og::sim::EventKind::Notification &&
                ev.text.find(needle) != std::string::npos)
                count++;
        return count;
    };
    const auto exit_prompts = [&] {
        int count = 0;
        for (const auto& ev : t.events.events())
            if (ev.kind == og::sim::EventKind::RequestExitConfirmation)
                count++;
        return count;
    };

    walker* exit_pad = t.world().add_fx_ob(Order::Treasure, FAMILY_EXIT);
    ASSERT_NE(nullptr, exit_pad);
    exit_pad->setxy(120, 120);
    ASSERT_NE(nullptr, exit_pad->stats());
    exit_pad->stats()->set_level(2); // destination level id

    walker* hero = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, hero);
    hero->setxy(120, 120);
    hero->set_act_type(ACT_CONTROL);

    // Foes alive, destination unearned: the toast fires, no prompt.
    t.world().level_done = 0;
    ASSERT_TRUE(exit_pad->eat_me(hero));
    EXPECT_EQ(1, notifications("Foes remain"));
    EXPECT_EQ(0, exit_prompts());

    // The skip_exit throttle bounds the cadence while standing on the pad.
    ASSERT_TRUE(exit_pad->eat_me(hero));
    EXPECT_EQ(1, notifications("Foes remain"))
        << "an immediate re-eat must be swallowed by set_skip_exit(10)";

    // Withdraw shape (destination already completed): the withdraw pair
    // fires, never the toast.
    hero->set_skip_exit(0);
    t.world().completed_levels.insert(2);
    ASSERT_TRUE(exit_pad->eat_me(hero));
    EXPECT_EQ(1, notifications("Foes remain"));
    EXPECT_EQ(1, exit_prompts()) << "withdraw emits RequestExitConfirmation";
    t.world().completed_levels.erase(2);

    // Cleared level: the normal exit prompt, no new toast.
    hero->set_skip_exit(0);
    t.world().level_done = 2;
    t.world().withdraw_requested = false;
    ASSERT_TRUE(exit_pad->eat_me(hero));
    EXPECT_EQ(1, notifications("Foes remain"));
    EXPECT_EQ(2, exit_prompts());
}

// #207: withdraw stays impossible during a replay, by construction —
// can_withdraw needs the CURRENT scenario uncompleted, and a replayed level
// is completed (that is what made it replayable). Foes alive + completed
// destination + completed current: the "Foes remain" toast fires, never the
// withdraw prompt.
TEST(SimWorldHeadless, replay_of_a_completed_level_cannot_withdraw)
{
    TestGameWorld t;
    GameWorld& world = t.world();
    world.current_scenario = 1;              // the replayed level
    world.completed_levels.insert(1);        // ... which the save has cleared
    world.completed_levels.insert(2);        // the exit's destination too
    world.level_done = 0;                    // foes alive (a restored replay)

    walker* exit_pad = world.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    ASSERT_NE(nullptr, exit_pad);
    exit_pad->setxy(120, 120);
    ASSERT_NE(nullptr, exit_pad->stats());
    exit_pad->stats()->set_level(2);

    walker* hero = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, hero);
    hero->setxy(120, 120);
    hero->set_act_type(ACT_CONTROL);

    ASSERT_TRUE(exit_pad->eat_me(hero));
    int prompts = 0;
    int foes_toasts = 0;
    for (const auto& ev : t.events.events())
    {
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation)
            prompts++;
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find("Foes remain") != std::string::npos)
            foes_toasts++;
    }
    EXPECT_EQ(0, prompts)
        << "no withdraw prompt mid-replay: the current level is completed";
    EXPECT_EQ(1, foes_toasts) << "the exit answers with the foes toast";
    EXPECT_FALSE(world.withdraw_requested);
}

// --- #160 exit-pad re-trigger latch -----------------------------------------
//
// Exit pads are eaten from ob_pass_check on EVERY movement probe, so before
// the latch, held-direction walking on the pad re-ran the exit prompt (or the
// "Foes remain!" toast) each time the skip_exit cooldown expired (~10 ticks).
// These tests drive REAL movement probes (walkstep -> walk -> query_passable
// -> ob_pass_check), never direct eat_me calls.

namespace exit_latch {

int exit_prompts(const og::sim::SimEventLog& log)
{
    int count = 0;
    for (const auto& ev : log.events())
        if (ev.kind == og::sim::EventKind::RequestExitConfirmation)
            count++;
    return count;
}

int notifications(const og::sim::SimEventLog& log, const char* needle)
{
    int count = 0;
    for (const auto& ev : log.events())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
            count++;
    return count;
}

// Movement probes only happen on passable ground; make the whole default
// grid uniform grass so geometry is exact.
void all_grass(GameWorld& w)
{
    const int n = w.grid.w * w.grid.h;
    for (int i = 0; i < n; ++i)
        w.grid.data[static_cast<std::size_t>(i)] = PIX_GRASS1;
}

walker* make_exit_pad(TestGameWorld& t, short x, short y, short dest_level)
{
    walker* pad = t.world().add_fx_ob(Order::Treasure, FAMILY_EXIT);
    if (pad == nullptr)
        return nullptr;
    pad->setxy(x, y);
    pad->stats()->set_level(dest_level); // destination level id
    return pad;
}

// A player-controlled hero centered on the pad, pre-faced RIGHT so every
// walkstep(1, 0) is a genuine movement probe (a direction CHANGE turns in
// place without probing).
walker* make_hero_on_pad(TestGameWorld& t, const walker* pad)
{
    walker* hero = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    if (hero == nullptr)
        return nullptr;
    hero->setxy(pad->xpos() + (pad->sizex() - hero->sizex()) / 2,
                pad->ypos() + (pad->sizey() - hero->sizey()) / 2);
    hero->set_act_type(ACT_CONTROL);
    hero->set_curdir(static_cast<signed char>(FACE_RIGHT));
    hero->set_enddir(static_cast<char>(FACE_RIGHT));
    return hero;
}

// One control-walker sim tick. living::act only reaches the skip_exit
// decrement in the plain-walk shape: a mid-animation act returns early via
// animate() and a pending turn returns via turn(), so hold both flat.
void act_tick(walker* hero)
{
    hero->set_ani_type(ANI_WALK);
    hero->set_enddir(static_cast<char>(hero->curdir()));
    hero->act();
}

// One held-direction walk tick ON the pad: a real movement probe, then a
// snap back to (x, y) so the walker never leaves the pad footprint no
// matter how many ticks the test runs.
void probe_tick(walker* hero, short x, short y)
{
    hero->walkstep(1, 0);
    hero->setxy(x, y);
}

} // namespace exit_latch

// RED-BEFORE: a control walker walking within the pad footprint used to be
// re-prompted every time the 10-tick skip_exit cooldown expired (measured 5
// prompts in 40 ticks); one CONTACT must produce exactly one prompt.
TEST(SimWorldHeadless, exit_prompts_once_per_contact_not_per_cooldown)
{
    TestGameWorld t;
    exit_latch::all_grass(t.world());
    walker* pad = exit_latch::make_exit_pad(t, 120, 120, 2);
    ASSERT_NE(nullptr, pad);
    walker* hero = exit_latch::make_hero_on_pad(t, pad);
    ASSERT_NE(nullptr, hero);
    t.world().level_done = 2; // level cleared: the exit-prompt branch

    const short hx = hero->xpos();
    const short hy = hero->ypos();
    for (int tick = 0; tick < 40; ++tick)
    {
        exit_latch::probe_tick(hero, hx, hy); // walkstep probes eat the pad
        exit_latch::act_tick(hero);           // skip_exit expires twice in 40
    }
    EXPECT_EQ(1, exit_latch::exit_prompts(t.events))
        << "one contact with the pad must produce exactly one exit prompt";
}

// Fully leaving the pad clears the latch on the next act tick; stepping back
// on is a deliberate act and prompts again.
TEST(SimWorldHeadless, exit_reprompts_after_stepping_fully_off_and_back)
{
    TestGameWorld t;
    exit_latch::all_grass(t.world());
    walker* pad = exit_latch::make_exit_pad(t, 120, 120, 2);
    ASSERT_NE(nullptr, pad);
    walker* hero = exit_latch::make_hero_on_pad(t, pad);
    ASSERT_NE(nullptr, hero);
    t.world().level_done = 2;

    const short hx = hero->xpos();
    const short hy = hero->ypos();
    exit_latch::probe_tick(hero, hx, hy);
    ASSERT_EQ(1, exit_latch::exit_prompts(t.events));
    ASSERT_TRUE(hero->exit_latched());

    // Step fully off the pad; the next act tick clears the latch.
    hero->setxy(300, 120);
    exit_latch::act_tick(hero);
    EXPECT_FALSE(hero->exit_latched())
        << "the latch must clear once the bbox no longer overlaps the pad";

    // Walk back on (cooldown spent) -> a fresh contact, a fresh prompt.
    hero->set_skip_exit(0);
    hero->setxy(hx, hy);
    exit_latch::probe_tick(hero, hx, hy);
    EXPECT_EQ(2, exit_latch::exit_prompts(t.events))
        << "stepping off and back on must re-prompt exactly once";
}

// Standing still on the pad never re-prompts, and the latch holds — it must
// not decay with time, only with actually leaving the rect.
TEST(SimWorldHeadless, exit_latch_holds_while_standing_still)
{
    TestGameWorld t;
    exit_latch::all_grass(t.world());
    walker* pad = exit_latch::make_exit_pad(t, 120, 120, 2);
    ASSERT_NE(nullptr, pad);
    walker* hero = exit_latch::make_hero_on_pad(t, pad);
    ASSERT_NE(nullptr, hero);
    t.world().level_done = 2;

    exit_latch::probe_tick(hero, hero->xpos(), hero->ypos());
    ASSERT_EQ(1, exit_latch::exit_prompts(t.events));

    for (int tick = 0; tick < 60; ++tick)
        exit_latch::act_tick(hero); // no movement -> no probes
    EXPECT_EQ(1, exit_latch::exit_prompts(t.events));
    EXPECT_TRUE(hero->exit_latched())
        << "standing still on the pad must keep the latch armed";
}

// In-footprint jitter with the cooldown forced OFF every tick: the latch
// alone must suppress the re-trigger (pre-latch this fired on EVERY probe).
TEST(SimWorldHeadless, exit_latch_suppresses_infootprint_jitter_without_cooldown)
{
    TestGameWorld t;
    exit_latch::all_grass(t.world());
    walker* pad = exit_latch::make_exit_pad(t, 120, 120, 2);
    ASSERT_NE(nullptr, pad);
    walker* hero = exit_latch::make_hero_on_pad(t, pad);
    ASSERT_NE(nullptr, hero);
    t.world().level_done = 2;

    const short hx = hero->xpos();
    const short hy = hero->ypos();
    for (int tick = 0; tick < 30; ++tick)
    {
        hero->set_skip_exit(0); // defeat the cooldown: only the latch is left
        exit_latch::probe_tick(hero, hx, hy);
        exit_latch::act_tick(hero);
    }
    EXPECT_EQ(1, exit_latch::exit_prompts(t.events))
        << "small nudges whose bbox never leaves the pad must not re-prompt";
}

// RED-BEFORE: the "Foes remain!" toast (level_done == 0, destination
// unearned) used to spam on the same cooldown cadence; it must fire exactly
// once per contact.
TEST(SimWorldHeadless, foes_remain_toast_fires_once_per_contact)
{
    TestGameWorld t;
    exit_latch::all_grass(t.world());
    walker* pad = exit_latch::make_exit_pad(t, 120, 120, 2);
    ASSERT_NE(nullptr, pad);
    walker* hero = exit_latch::make_hero_on_pad(t, pad);
    ASSERT_NE(nullptr, hero);
    t.world().level_done = 0; // foes alive, destination unearned: the toast

    const short hx = hero->xpos();
    const short hy = hero->ypos();
    for (int tick = 0; tick < 40; ++tick)
    {
        exit_latch::probe_tick(hero, hx, hy);
        exit_latch::act_tick(hero);
    }
    EXPECT_EQ(1, exit_latch::notifications(t.events, "Foes remain"))
        << "one contact must produce exactly one Foes remain! toast";
    EXPECT_EQ(0, exit_latch::exit_prompts(t.events));

    // A fresh contact gets its own toast.
    hero->setxy(300, 120);
    exit_latch::act_tick(hero);
    ASSERT_FALSE(hero->exit_latched());
    hero->set_skip_exit(0);
    hero->setxy(hx, hy);
    exit_latch::probe_tick(hero, hx, hy);
    EXPECT_EQ(2, exit_latch::notifications(t.events, "Foes remain"))
        << "stepping off and back on is a new contact: a second toast";
}

// The latch is per-walker, not per-pad: A's latch must not suppress B's
// first trigger on the same pad.
TEST(SimWorldHeadless, two_control_walkers_latch_one_pad_independently)
{
    TestGameWorld t;
    exit_latch::all_grass(t.world());
    walker* pad = t.world().add_fx_ob(Order::Treasure, FAMILY_EXIT);
    ASSERT_NE(nullptr, pad);
    // Widen the pad (16x16 stock) into a 48x16 strip BEFORE placing it, so
    // the obmap registers the full span and two 16px heroes fit on it with
    // a clear gap between them (their probes must hit only the pad).
    pad->set_sizex(48);
    pad->setxy(120, 120);
    pad->stats()->set_level(2);
    t.world().level_done = 2;

    // A on the pad's left cell, B on its right cell: both overlap the pad,
    // neither overlaps the other (nor after a one-step probe right).
    walker* a = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, a);
    a->setxy(pad->xpos(), pad->ypos());
    a->set_act_type(ACT_CONTROL);
    a->set_curdir(static_cast<signed char>(FACE_RIGHT));
    a->set_enddir(static_cast<char>(FACE_RIGHT));

    walker* b = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, b);
    const short bx = static_cast<short>(pad->xpos() + pad->sizex() - b->sizex());
    b->setxy(bx, pad->ypos());
    b->set_act_type(ACT_CONTROL);
    b->set_curdir(static_cast<signed char>(FACE_RIGHT));
    b->set_enddir(static_cast<char>(FACE_RIGHT));

    exit_latch::probe_tick(a, a->xpos(), a->ypos());
    EXPECT_EQ(1, exit_latch::exit_prompts(t.events));
    EXPECT_TRUE(a->exit_latched());

    exit_latch::probe_tick(b, b->xpos(), b->ypos());
    EXPECT_EQ(2, exit_latch::exit_prompts(t.events))
        << "B's first contact must trigger even though A is latched";
    EXPECT_TRUE(b->exit_latched());
}

// Life-gem value vs permadeath: with keep_fallen_heroes set (permadeath off)
// the fallen hero returns with their growth intact, so the gem drops at HALF
// the legacy value — full salvage would double-dip. Default (0) is the
// byte-identical legacy math: heart_value * 0.75 / 2.
TEST(SimWorldHeadless, life_gem_halves_when_permadeath_is_off)
{
    const auto dropped_gem_value = [](short keep_fallen) -> float {
        TestGameWorld t;
        t.world().keep_fallen_heroes = keep_fallen;

        walker* hero = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
        if (hero == nullptr)
            return -1.0f;
        hero->setxy(120, 120);
        hero->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        hero->myguy->exp = 4000; // non-trivial heart value

        hero->set_dead(1);
        hero->death();

        for (const auto& uptr : t.world().oblist)
        {
            walker* w = uptr.get();
            if (w != nullptr && w->query_order() == Order::Treasure &&
                w->family() == FAMILY_LIFE_GEM)
                return w->stats()->hitpoints();
        }
        return -1.0f;
    };

    const float full = dropped_gem_value(0);
    const float halved = dropped_gem_value(1);
    ASSERT_GT(full, 0.0f) << "permadeath-on death must drop a valued gem";
    ASSERT_GT(halved, 0.0f);
    EXPECT_FLOAT_EQ(full * 0.5f, halved)
        << "permadeath-off gems carry exactly half the legacy value";
}

// --- Negative values must never reach IRandom::next -------------------------
//
// IRandom::next takes a std::uint32_t bound (include/openglad/core/irandom.h:12)
// and every implementation answers 0 for a bound of 0 (irandom.h:20, irandom.h:31,
// game_world.h:56). A negative int therefore does NOT clamp on the way in — it
// wraps to a bound just under 2^32, and SimRandom's `% max_exclusive` cannot
// reduce it either: the raw draw is `state_ >> 16`, at most 65535, always less
// than the wrapped bound, so the "tiny offset" comes back as an arbitrary
// 16-bit number. Three sim sites fed next() a value that sprite metadata,
// network snapshots or Lua can drive negative.
//
// These tests pin GameWorld::rng_ (og::sim::SimRandom is a plain LCG with a
// public state_) and assert OBSERVABLE outcomes: explosion positions, foe
// choices, and the RNG state left behind. They deliberately install no RNG
// spy. og::sim::set_sim_random_override is only consulted by callers that
// compiled SimRandom::next — it is inline in game_world.h — WITH -DTESTING,
// and og_unit_sim links og_game, whose walker.cpp / living.cpp /
// game_world.cpp live in og_gameplay and are built WITHOUT it (only
// og_game_test gets TESTING; cmake/OpenGladTests.cmake). A spy installed from
// this TU can therefore never observe a draw made by sim code, and every
// assertion about what it recorded would be vacuously true.
//
// Reading the state instead is strictly better here: SimRandom::next returns
// early for a bound of 0 WITHOUT advancing state_ (game_world.h:56), so
// "state_ is untouched" is a direct, spy-free proof that no absurd bound was
// ever requested — a wrapped ~4e9 bound always costs a step.

namespace rng_bound {

// SimRandom replayed (game_world.h:51..66) so the expected values below are
// derived arithmetic rather than recorded output.
constexpr std::uint32_t lcg_step(std::uint32_t state)
{
    return state * 1103515245u + 12345u;
}

constexpr std::uint32_t lcg_draw(std::uint32_t state, std::uint32_t bound)
{
    return (bound == 0u) ? 0u : ((lcg_step(state) >> 16) % bound);
}

// The pin used by most tests. Its first draw is (0x309cbe53 >> 16) == 12444,
// which is what makes every pre-clamp prediction below non-zero — i.e. plainly
// different from the clamped answer.
inline constexpr std::uint32_t kPin = 0xC0FFEE42u;
inline constexpr std::uint32_t kPinStepped = 0x309CBE53u;
static_assert(lcg_step(kPin) == kPinStepped);
static_assert((kPinStepped >> 16) == 12444u);

// A second pin whose 15-wide draw is 0 (41295 == 15 * 2753), used to pin the
// "the cloak roll succeeded" half of the legacy behavior.
inline constexpr std::uint32_t kZeroRollPin = 0xBEEF0002u;
inline constexpr std::uint32_t kZeroRollPinStepped = 0xA14FCD13u;
static_assert(lcg_step(kZeroRollPin) == kZeroRollPinStepped);
static_assert((kZeroRollPinStepped >> 16) == 41295u);
static_assert(lcg_draw(kZeroRollPin, 300 / 20) == 0u);

struct Explosion
{
    short x;
    short y;
};

// Kill a generator whose sprite is size_x by size_y and report where its four
// explosions landed, with the world RNG pinned to `pin_state` at the moment of
// death. The generator sits at (120, 120), so a zero-width scatter span puts
// every explosion at exactly (124, 124) — the corpse — no matter what the RNG
// would have said.
std::vector<Explosion> blow_up_generator(short size_x, short size_y,
                                         std::uint32_t pin_state,
                                         std::uint32_t* end_state = nullptr)
{
    TestGameWorld t;

    std::vector<Explosion> out;
    walker* gen = t.world().add_ob(Order::Generator, FAMILY_TOWER);
    EXPECT_NE(nullptr, gen);
    if (gen == nullptr)
        return out;
    gen->set_sizex(size_x);
    gen->set_sizey(size_y);
    gen->setxy(120, 120);
    gen->stats()->set_level(3);

    // Pin immediately before the action under test. (Walker construction draws
    // next(10) for path_check_counter, but from walker_rng() — which the
    // fixture points at its own FixedRandom via push_test_context ->
    // set_gameplay_rng_override — so it never touches world state. The world
    // stream below is only what the death scatter itself asks for.)
    t.world().rng_.state_ = pin_state;

    gen->set_dead(1);
    gen->death();

    for (const auto& uptr : t.world().oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && w->query_order() == Order::FX &&
            w->family() == FAMILY_EXPLOSION)
            out.push_back(Explosion{w->xpos(), w->ypos()});
    }
    if (end_state != nullptr)
        *end_state = t.world().rng_.state_;
    return out;
}

struct FoeTick
{
    bool kept_foe = false;
    std::uint32_t end_state = 0;
};

// One ACT_CONTROL tick of a hunter locked onto a cloaked prey, with the world
// RNG pinned. The foe-validity roll is the FIRST world draw living::act makes
// (living.cpp:101) — bonus_rounds, apply_z_motion (single-floor early return)
// and update_exit_latch draw nothing — and ACT_CONTROL returns before any
// later draw site, so the tick costs exactly one draw of bound
// invisibility/20, or none at all when that span is 0.
FoeTick foe_lock_tick(short invisibility, std::uint32_t pin_state)
{
    TestGameWorld t;
    exit_latch::all_grass(t.world());

    walker* hunter = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    EXPECT_NE(nullptr, hunter);
    walker* prey = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    EXPECT_NE(nullptr, prey);
    if (hunter == nullptr || prey == nullptr)
        return FoeTick{};

    hunter->setxy(120, 120);
    hunter->set_team_num(0);
    hunter->set_act_type(ACT_CONTROL);
    hunter->set_ani_type(ANI_WALK); // never return early via animate()

    prey->setxy(160, 120);
    prey->set_team_num(1);
    prey->set_invisibility_left(invisibility);

    hunter->set_foe(prey);
    t.world().rng_.state_ = pin_state; // pinned immediately before the tick
    hunter->act();

    FoeTick out;
    out.kept_foe = (hunter->foe() == prey);
    out.end_state = t.world().rng_.state_;
    return out;
}

struct FoeSearch
{
    bool far_found = false;
    bool near_found = false;
    std::uint32_t state_after_far = 0;
    std::uint32_t state_after_near = 0;
};

// Acquisition from a clean pin, once through find_far_foe and once through
// find_near_foe. The seeker is friendly to itself, and `is_friendly(w) == 0`
// short-circuits ahead of the roll, so the only walker that can reach next()
// is `hidden`: find_far_foe costs exactly one draw of bound invisibility/20.
FoeSearch search_for_foe(short invisibility, std::uint32_t pin_state)
{
    TestGameWorld t;
    exit_latch::all_grass(t.world());

    walker* seeker = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    EXPECT_NE(nullptr, seeker);
    walker* hidden = t.world().add_ob(Order::Living, FAMILY_SOLDIER);
    EXPECT_NE(nullptr, hidden);
    if (seeker == nullptr || hidden == nullptr)
        return FoeSearch{};

    seeker->setxy(120, 120);
    seeker->set_team_num(0);
    hidden->setxy(160, 120);
    hidden->set_team_num(1);
    hidden->set_invisibility_left(invisibility);

    FoeSearch out;
    t.world().rng_.state_ = pin_state;
    out.far_found = (t.world().find_far_foe(seeker) == hidden);
    out.state_after_far = t.world().rng_.state_;

    t.world().rng_.state_ = pin_state; // each search judged from the same pin
    out.near_found = (t.world().find_near_foe(seeker) == hidden);
    out.state_after_near = t.world().rng_.state_;
    return out;
}

} // namespace rng_bound

// (a) walker.cpp generator death scatter. sizex/sizey are sprite metadata, and
// snapshot application writes them straight onto the walker, so a sprite
// narrower than 8px makes sizex()-8 negative. The scatter span then wrapped to
// ~4e9 and the explosion FX landed at a truncated garbage coordinate instead of
// on the corpse.
//
// RED-BEFORE, exactly: from kPin the wrapped bound 0xFFFFFFFC leaves the raw
// 16-bit draws untouched by the modulo — 12444, 48108, 36898, 29482, 18326,
// 43937, 4229, 178 — so the four explosions land at (12568, 48232),
// (37022, 29606), (18450, 44061) and (4353, 302), i.e. at whatever those
// truncate to as a short, never on the corpse. Post-fix the span is
// max(4-8, 0) == 0, next(0) == 0, and the answer is a POINT, not a range:
// (120 + 0 + 4, 120 + 4 + 0) for every explosion and every RNG state.
TEST(SimWorldHeadless, generator_death_scatter_clamps_undersized_sprite_span)
{
    std::uint32_t tiny_end = 0;
    const std::vector<rng_bound::Explosion> tiny =
        rng_bound::blow_up_generator(4, 4, rng_bound::kPin, &tiny_end);
    ASSERT_EQ(4u, tiny.size()) << "a dying generator always emits 4 explosions";
    for (std::size_t i = 0; i < tiny.size(); ++i)
    {
        EXPECT_EQ(124, tiny[i].x) << "explosion " << i << " must land on the generator";
        EXPECT_EQ(124, tiny[i].y) << "explosion " << i << " must land on the generator";
    }

    // ...and it must cost the same RNG stream as the 8px sprite whose span is
    // already 0. Both runs start from the same pin, so identical end states
    // mean the 4px scatter asked the RNG for nothing extra. A wrapped bound
    // cannot do that: it steps the LCG twice per explosion.
    std::uint32_t edge_end = 0;
    const std::vector<rng_bound::Explosion> edge =
        rng_bound::blow_up_generator(8, 8, rng_bound::kPin, &edge_end);
    ASSERT_EQ(4u, edge.size());
    EXPECT_EQ(edge_end, tiny_end)
        << "a 4px sprite must not ask the RNG for a ~4-billion scatter span";
}

// Boundary: exactly 8px is already a zero-width span today (next(0) == 0 pins
// the offset AND leaves state_ alone), so the clamp has to reproduce that value
// for value.
TEST(SimWorldHeadless, generator_death_scatter_at_exactly_eight_pixels)
{
    const std::vector<rng_bound::Explosion> edge =
        rng_bound::blow_up_generator(8, 8, rng_bound::kPin);
    ASSERT_EQ(4u, edge.size());
    for (std::size_t i = 0; i < edge.size(); ++i)
    {
        EXPECT_EQ(124, edge[i].x) << "explosion " << i << ": span 8-8 == 0";
        EXPECT_EQ(124, edge[i].y) << "explosion " << i << ": span 8-8 == 0";
    }
}

// ...and the clamp must be invisible on every real sprite: a 40px generator
// still draws from the full legacy 32px span and still scatters.
//
// The world stream from kPin, per explosion, is the two setxy span draws
// next(32) then set_frame's next(3):
//   e0: 28, 12 | 2      e1:  2, 10 | 0
//   e2: 22,  1 | 2      e3:  5, 18 | 1
// Both coordinates are 120 + draw + 4, giving the pairs {152,136}, {126,134},
// {146,125} and {129,142} — a scatter that really scatters: 8 offsets, none of
// them the 0 that an over-clamped span would force. The two span draws are the
// arguments of a single setxy call, so which of a pair lands on x is
// unspecified evaluation order; the sorted pair is the exact assertion.
TEST(SimWorldHeadless, generator_death_scatter_unchanged_for_normal_sprites)
{
    static_assert(rng_bound::lcg_draw(rng_bound::kPin, 40 - 8) == 28u); // 12444 % 32
    static_assert(rng_bound::lcg_draw(rng_bound::kPinStepped, 40 - 8) == 12u); // 48108 % 32

    const std::vector<rng_bound::Explosion> normal =
        rng_bound::blow_up_generator(40, 40, rng_bound::kPin);
    ASSERT_EQ(4u, normal.size());

    const short expected_low[4] = {136, 126, 125, 129};
    const short expected_high[4] = {152, 134, 146, 142};
    for (std::size_t i = 0; i < normal.size(); ++i)
    {
        const short low = (normal[i].x < normal[i].y) ? normal[i].x : normal[i].y;
        const short high = (normal[i].x < normal[i].y) ? normal[i].y : normal[i].x;
        EXPECT_EQ(expected_low[i], low) << "explosion " << i;
        EXPECT_EQ(expected_high[i], high) << "explosion " << i;
    }
}

// (b) living.cpp foe-validity roll. invisibility_left is a short written raw
// from snapshots (world_snapshot.cpp) and from Lua (set_invisibility_left).
// Negative / 20 wrapped to a ~4e9 bound whose draw is > 0 for essentially every
// RNG state, so the hunter threw its lock away on every single tick.
//
// RED-BEFORE, exactly: -100 / 20 == -5 wraps to the bound 0xFFFFFFFB, and from
// kPin the draw is 12444 — greater than 0, so set_foe(nullptr). Post-fix the
// span is 0, next(0) returns 0 without touching state_, and the lock holds for
// every RNG state.
TEST(SimWorldHeadless, negative_invisibility_does_not_break_the_foe_lock)
{
    static_assert(static_cast<std::uint32_t>(-100 / 20) == 0xFFFFFFFBu);
    static_assert(rng_bound::lcg_draw(rng_bound::kPin, 0xFFFFFFFBu) == 12444u);

    const rng_bound::FoeTick tick = rng_bound::foe_lock_tick(-100, rng_bound::kPin);
    EXPECT_TRUE(tick.kept_foe)
        << "a negative cloak counter must read as 'not cloaked', never 'always cloaked'";
    EXPECT_EQ(rng_bound::kPin, tick.end_state)
        << "a clamped zero-width span costs no draw at all; a wrapped ~4-billion"
           " bound would have stepped the LCG";
}

// The same site, on the values the game actually produces. 0 and 19 both floor
// to a zero-width span (next(0) == 0, state untouched) so the lock holds; a
// real cloak rolls next(15) and drops the lock unless the draw is 0. Both
// outcomes are pinned, and each costs exactly one LCG step.
TEST(SimWorldHeadless, foe_lock_unchanged_for_non_negative_invisibility)
{
    const rng_bound::FoeTick uncloaked = rng_bound::foe_lock_tick(0, rng_bound::kPin);
    EXPECT_TRUE(uncloaked.kept_foe) << "an uncloaked foe is never dropped";
    EXPECT_EQ(rng_bound::kPin, uncloaked.end_state) << "0 / 20 == 0: no draw";

    const rng_bound::FoeTick sub_step = rng_bound::foe_lock_tick(19, rng_bound::kPin);
    EXPECT_TRUE(sub_step.kept_foe) << "below one full step the span is still 0";
    EXPECT_EQ(rng_bound::kPin, sub_step.end_state) << "19 / 20 == 0: no draw";

    // 300 / 20 == 15; from kPin the draw is 12444 % 15 == 9 > 0 -> lock broken.
    static_assert(rng_bound::lcg_draw(rng_bound::kPin, 300 / 20) == 9u);
    const rng_bound::FoeTick cloaked = rng_bound::foe_lock_tick(300, rng_bound::kPin);
    EXPECT_FALSE(cloaked.kept_foe) << "a real cloak must still break the lock";
    EXPECT_EQ(rng_bound::kPinStepped, cloaked.end_state)
        << "the cloak roll is exactly one draw";

    // Same cloak, a pin whose draw is 41295 % 15 == 0 -> the lock survives.
    const rng_bound::FoeTick lucky =
        rng_bound::foe_lock_tick(300, rng_bound::kZeroRollPin);
    EXPECT_TRUE(lucky.kept_foe)
        << "a cloak roll of 0 keeps the lock; that is the legacy 1-in-15";
    EXPECT_EQ(rng_bound::kZeroRollPinStepped, lucky.end_state);
}

// (c) game_world.cpp foe acquisition, both the obmap spiral (find_near_foe) and
// the full-list scan (find_far_foe). The same wrapped bound made the roll
// non-zero, so a foe carrying a negative cloak counter was permanently
// invisible to every searcher.
//
// RED-BEFORE, exactly: the bound wraps to 0xFFFFFFFB and the draw from kPin is
// 12444 != 0, so find_far_foe skips the only candidate and returns nullptr;
// find_near_foe's spiral rejects it in every cell it appears in and ends up in
// the same find_far_foe. Post-fix both searches see it, and neither touches
// state_ — the seeker is friendly to itself (short-circuit, no draw) and the
// clamped span short-circuits inside next().
TEST(SimWorldHeadless, negative_invisibility_does_not_hide_a_foe_from_search)
{
    const rng_bound::FoeSearch found = rng_bound::search_for_foe(-100, rng_bound::kPin);
    EXPECT_TRUE(found.far_found)
        << "find_far_foe must see a foe whose cloak counter is negative";
    EXPECT_EQ(rng_bound::kPin, found.state_after_far)
        << "no acquisition draw may run against a wrapped ~4-billion bound";
    EXPECT_TRUE(found.near_found)
        << "find_near_foe must see a foe whose cloak counter is negative";
    EXPECT_EQ(rng_bound::kPin, found.state_after_near)
        << "the spiral must not step the LCG for a clamped span either";
}

// Same acquisition site on good input: 0 and 19 stay visible for free
// (next(0) == 0, state untouched), and a real cloak still rolls next(15) —
// hiding its bearer on a non-zero draw, revealing them on a zero one. Exactly
// the pre-clamp outcome, draw for draw.
TEST(SimWorldHeadless, foe_search_unchanged_for_non_negative_invisibility)
{
    const rng_bound::FoeSearch uncloaked = rng_bound::search_for_foe(0, rng_bound::kPin);
    EXPECT_TRUE(uncloaked.far_found) << "an uncloaked foe is always acquirable";
    EXPECT_TRUE(uncloaked.near_found);
    EXPECT_EQ(rng_bound::kPin, uncloaked.state_after_far) << "0 / 20 == 0: no draw";
    EXPECT_EQ(rng_bound::kPin, uncloaked.state_after_near);

    const rng_bound::FoeSearch sub_step = rng_bound::search_for_foe(19, rng_bound::kPin);
    EXPECT_TRUE(sub_step.far_found) << "below one full step the span is still 0";
    EXPECT_TRUE(sub_step.near_found);
    EXPECT_EQ(rng_bound::kPin, sub_step.state_after_far) << "19 / 20 == 0: no draw";
    EXPECT_EQ(rng_bound::kPin, sub_step.state_after_near);

    // 300 / 20 == 15. Only find_far_foe has a pinnable draw count (one draw,
    // for the single non-friendly candidate); the spiral's is geometry-bound.
    const rng_bound::FoeSearch cloaked = rng_bound::search_for_foe(300, rng_bound::kPin);
    EXPECT_FALSE(cloaked.far_found) << "a real cloak must still hide its bearer";
    EXPECT_EQ(rng_bound::kPinStepped, cloaked.state_after_far)
        << "the cloak roll is exactly one draw";

    const rng_bound::FoeSearch lucky =
        rng_bound::search_for_foe(300, rng_bound::kZeroRollPin);
    EXPECT_TRUE(lucky.far_found)
        << "a cloak roll of 0 acquires the bearer; that is the legacy 1-in-15";
    EXPECT_EQ(rng_bound::kZeroRollPinStepped, lucky.state_after_far);
}
