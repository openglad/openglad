/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch walker-1: the rules the review found in statistics.h, walker.h, effect.cpp, walker.cpp.
// Each test is named after the rule it asserts.
//
// Two kinds of subject. A loader-built body (world().add_ob) runs its own
// class's act (living::act, effect::act); walker::act itself runs only for a
// plain `walker` object, which in play means a generator, so the act-switch
// rules are staged on a generator from the loader or on a plain walker built
// here and pushed onto the oblist.
//
// Sounds. In the old game a shot or a clang sounds only when the walker is on
// screen. The current game emits every positional sound placed at its walker
// unless a visibility test is installed, and each machine plays it only where
// one of its own views shows that place (#468; SemPresentation pins the
// placing). These tests install a test that answers "on screen", so what they
// pin is which sound, and that an on-screen hit sounds.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

// Every row of the table is {0, 1, -1}: two frames, then the sentinel.
signed char** two_frame_table()
{
    static std::array<std::array<signed char, 4>, 256> seqs{};
    static std::array<signed char*, 256> rows{};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        seqs[i] = {0, 1, -1, -1};
        rows[i] = seqs[i].data();
    }
    return rows.data();
}

bool on_screen(const walker*, std::uint32_t, void*) { return true; }

class SemWalker1 : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    og::test::ScopedHookFailureGuard guard_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        fresh_world();
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        if (current_game != nullptr)
            current_game->positional_sound_visible = nullptr;
        tw_.reset();
    }

    // An open field of grass, 1024 px a side, with nobody on it; every
    // positional sound counts as on screen.
    void fresh_world()
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = 0;
        w.rng_.state_ = 12345u;
        current_game->positional_sound_visible = &on_screen;
    }

    GameWorld& world() { return tw_->world(); }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // Sizes first, then the position: the obmap registers a body in every
    // cell its box covers at the moment it is placed.
    static walker* place(walker* w, short x, short y, short sx, short sy)
    {
        if (w == nullptr)
            return nullptr;
        w->set_sizex(sx);
        w->set_sizey(sy);
        w->setxy(static_cast<short>(x + 1), y);  // force a real move
        w->setxy(x, y);
        return w;
    }

    walker* living_at(int family, unsigned char team, short x, short y,
                      short sx = 16, short sy = 16)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_act_type(ACT_RANDOM);
        w->stats()->set_level(4);
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(500.0f);
        w->stats()->set_magicpoints(500.0f);
        w->stats()->set_magic_per_round(0.0f);
        w->set_busy(0.0f);
        return place(w, x, y, sx, sy);
    }

    walker* generator_at(int family, unsigned char team, short x, short y,
                         int level = 4)
    {
        walker* g = world().add_ob(Order::Generator, family);
        if (g == nullptr)
            return nullptr;
        g->set_team_num(team);
        g->stats()->set_level(level);
        g->stats()->set_magicpoints(9999.0f);
        return place(g, x, y, g->sizex(), g->sizey());
    }

    // A plain walker (walker::act, not a subclass's) on the oblist.
    walker* plain_at(Order order, int family, unsigned char team, short x,
                     short y)
    {
        auto w = std::make_unique<walker>();
        w->set_order_family(order, static_cast<char>(family));
        w->set_sizex(16);
        w->set_sizey(16);
        w->set_stepsize(1.0f);
        w->set_lineofsight(6);
        w->setxy(x, y);
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_dead(0);
        w->ani = two_frame_table();
        w->ani_count = 0;
        walker* out = w.get();
        world().oblist.push_back(std::move(w));
        return out;
    }

    void face(walker* w, int facing, float dx, float dy)
    {
        w->set_curdir(static_cast<signed char>(facing));
        w->set_enddir(static_cast<char>(facing));
        w->set_lastx(dx);
        w->set_lasty(dy);
    }

    std::size_t live_weapons() const
    {
        std::size_t n = 0;
        for (const auto& w : tw_->world().weaplist)
            if (w && !w->dead())
                ++n;
        return n;
    }

    std::size_t count_sound(std::uint32_t id) const
    {
        const auto all = sounds();
        return static_cast<std::size_t>(std::count(all.begin(), all.end(), id));
    }

    std::vector<std::uint32_t> sounds() const
    {
        std::vector<std::uint32_t> out;
        for (const og::sim::Event& e : tw_->events.events())
            if (e.kind == og::sim::EventKind::PlaySound)
                out.push_back(e.a);
        return out;
    }

    // Lines logged by test hooks since the last call to mark_log().
    std::vector<std::string> log_since_mark()
    {
        const auto& log = world().scripts().host().log();
        std::vector<std::string> out;
        for (std::size_t i = log_mark_; i < log.size(); ++i)
            out.push_back(log[i]);
        return out;
    }
    void mark_log() { log_mark_ = world().scripts().host().log().size(); }

    void register_test_hooks(const std::string& source)
    {
        og::script::register_pack_script(
            {"test.sem_walker_1", "sem_walker_1.lua", source});
        const auto& errors = world().scripts().host().errors();
        ASSERT_TRUE(errors.empty()) << errors.front().message;
        mark_log();
    }

    std::size_t log_mark_ = 0;
};

std::vector<const walker*> snapshot_ptrs(const GameWorld::EntityList& list)
{
    std::vector<const walker*> out;
    for (const auto& w : list)
        out.push_back(w.get());
    return out;
}

std::vector<walker*> new_of(const GameWorld::EntityList& list,
                            const std::vector<const walker*>& before,
                            Order order, int family)
{
    std::vector<walker*> out;
    for (const auto& w : list) {
        if (std::find(before.begin(), before.end(), w.get()) != before.end())
            continue;
        if (w->query_order() == order && w->family() == family)
            out.push_back(w.get());
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// statistics.h:173 statistics::frozen_delay
// ---------------------------------------------------------------------------

// A walker in its thaw-immunity window (negative raw timer) counts as not
// frozen for every reader, the snapshot included: the wire carries 0 and
// capturing does not clear the immunity. A frozen walker's timer goes out as
// it is (the control).
// RED (planted): capture_entity_stats reading frozen_delay_raw().
TEST_F(SemWalker1, a_thawing_walker_reads_as_unfrozen_in_the_snapshot)
{
    walker* thawing = living_at(FAMILY_SOLDIER, 0, 320, 320);
    walker* frozen = living_at(FAMILY_SOLDIER, 0, 400, 320);
    ASSERT_NE(nullptr, thawing);
    ASSERT_NE(nullptr, frozen);
    thawing->stats()->set_frozen_delay(-12);
    frozen->stats()->set_frozen_delay(7);

    const og::sim::WorldSnapshot snap = og::sim::peek_keyframe_snapshot(world());
    const auto find = [&](const walker* w) -> const og::sim::EntitySnapshot* {
        for (const auto& e : snap.oblist)
            if (e.entity_id == w->entity_id())
                return &e;
        return nullptr;
    };
    const og::sim::EntitySnapshot* t = find(thawing);
    const og::sim::EntitySnapshot* f = find(frozen);
    ASSERT_NE(nullptr, t);
    ASSERT_NE(nullptr, f);
    EXPECT_EQ(0, t->frozen_delay) << "immunity phase: the wire reads 0";
    EXPECT_EQ(0, thawing->stats()->frozen_delay()) << "immunity phase: the getter reads 0";
    EXPECT_EQ(-12, thawing->stats()->frozen_delay_raw())
        << "immunity phase: capture leaves the raw timer alone";
    EXPECT_EQ(7, f->frozen_delay) << "frozen: the wire carries the timer";
}

// ---------------------------------------------------------------------------
// walker.h:500 walker::note_attacker
// ---------------------------------------------------------------------------

// A hit is credited to the topmost living on the attacker's owner chain: the
// thrower's id, team and the world tick. A weapon with no living on its chain
// stamps nothing (the hit still lands), and a null credit leaves an earlier
// stamp as it was.
// RED (planted): credit_head returning `top ? top : this` (the knife itself
// is credited); attack() stamping tick_count_ + 1.
TEST_F(SemWalker1, a_hit_is_credited_to_the_living_at_the_top_of_the_owner_chain)
{
    walker* thrower = living_at(FAMILY_SOLDIER, 0, 100, 100);
    walker* victim = living_at(FAMILY_ORC, 1, 300, 300);
    walker* victim2 = living_at(FAMILY_ORC, 1, 500, 300);
    ASSERT_NE(nullptr, thrower);
    ASSERT_NE(nullptr, victim);
    ASSERT_NE(nullptr, victim2);
    victim->stats()->set_armor(0.0f);
    victim2->stats()->set_armor(0.0f);
    world().tick_count_ = 5;

    walker* knife = world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->set_team_num(0);
    knife->set_owner(thrower);
    knife->set_damage(10.0f);
    ASSERT_TRUE(knife->attack(victim));
    EXPECT_EQ(thrower->entity_id(), victim->last_attacker_id()) << "living arm: the thrower's id";
    EXPECT_EQ(0, victim->last_attacker_team()) << "living arm: the thrower's team";
    EXPECT_EQ(5u, victim->last_attacker_tick()) << "living arm: the world tick";
    EXPECT_FLOAT_EQ(392.0f, victim->stats()->hitpoints()) << "the hit lands";

    walker* orphan = world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, orphan);
    orphan->set_team_num(0);
    orphan->set_owner(nullptr);
    orphan->set_damage(10.0f);
    ASSERT_TRUE(orphan->attack(victim2));
    EXPECT_FLOAT_EQ(392.0f, victim2->stats()->hitpoints())
        << "no living on the chain: the same hit still lands";
    EXPECT_EQ(0u, victim2->last_attacker_id()) << "no living on the chain: no id";
    EXPECT_EQ(-1, victim2->last_attacker_team()) << "no living on the chain: no team";
    EXPECT_EQ(0u, victim2->last_attacker_tick()) << "no living on the chain: no tick";

    victim->note_attacker(nullptr, 9);
    EXPECT_EQ(thrower->entity_id(), victim->last_attacker_id()) << "a null credit keeps the stamp";
    EXPECT_EQ(0, victim->last_attacker_team());
    EXPECT_EQ(5u, victim->last_attacker_tick());
}

// ---------------------------------------------------------------------------
// effect.cpp:57 effect::act (dead refs forgotten, live refs kept)
// ---------------------------------------------------------------------------

// An effect keeps a foe, leader and owner that are alive at the start of its
// tick; only the dead one is forgotten.
// RED (planted): each of the three guards reduced to `if (foe())` etc.
TEST_F(SemWalker1, an_effect_keeps_its_living_foe_leader_and_owner)
{
    walker* foe = living_at(FAMILY_ORC, 1, 100, 100);
    walker* leader = living_at(FAMILY_SOLDIER, 0, 200, 100);
    walker* owner = living_at(FAMILY_SOLDIER, 0, 300, 100);
    ASSERT_TRUE(foe && leader && owner);

    walker* fx = world().add_fx_ob(Order::FX, FAMILY_FLASH);
    ASSERT_NE(nullptr, fx);
    fx->ani = two_frame_table();
    fx->ani_count = 0;
    fx->set_ani_type(ANI_ATTACK);
    fx->set_cycle(0);
    fx->set_foe(foe);
    fx->set_leader(leader);
    fx->set_owner(owner);
    ASSERT_TRUE(fx->act());
    EXPECT_EQ(foe, fx->foe()) << "alive: the foe is kept";
    EXPECT_EQ(leader, fx->leader()) << "alive: the leader is kept";
    EXPECT_EQ(owner, fx->owner()) << "alive: the owner is kept";
    EXPECT_EQ(0, fx->dead());

    // Only the owner dies: it alone is forgotten.
    owner->set_dead(1);
    fx->set_ani_type(ANI_ATTACK);
    fx->set_cycle(0);
    ASSERT_TRUE(fx->act());
    EXPECT_EQ(nullptr, fx->owner()) << "dead: the owner is forgotten";
    EXPECT_EQ(foe, fx->foe()) << "the live foe beside it is kept";
    EXPECT_EQ(leader, fx->leader()) << "the live leader beside it is kept";
}

// ---------------------------------------------------------------------------
// effect.cpp:72 effect::act (on_act's answer)
// ---------------------------------------------------------------------------

// An effect whose on_act returns false hands the tick to the default: the
// playing row animates frame by frame, and the tick after it ends the effect
// dies. The same family answering true owns every tick: no frame moves and it
// never dies.
// RED (planted): `if (*hook_result) return 1;` -> `if (*hook_result || true)`.
TEST_F(SemWalker1, an_effect_whose_on_act_declines_animates_then_dies)
{
    register_test_hooks(
        "og.register_hooks('effect', 'core:flash', {\n"
        "  on_act = function(self) og.log('flash act'); return false end,\n"
        "})\n");
    walker* fx = world().add_fx_ob(Order::FX, FAMILY_FLASH);
    ASSERT_NE(nullptr, fx);
    fx->ani = two_frame_table();
    fx->ani_count = 0;
    fx->set_ani_type(ANI_ATTACK);
    fx->set_curdir(static_cast<signed char>(FACE_UP));
    fx->set_cycle(0);
    fx->set_frame(9);

    EXPECT_TRUE(fx->act()) << "declined, first frame: animate() answers";
    EXPECT_EQ(0, fx->frame()) << "the row's first frame is shown";
    EXPECT_EQ(1, static_cast<int>(fx->cycle()));
    EXPECT_EQ(ANI_ATTACK, static_cast<int>(fx->ani_type()));
    EXPECT_EQ(0, fx->dead());

    EXPECT_TRUE(fx->act()) << "declined, last frame";
    EXPECT_EQ(1, fx->frame());
    EXPECT_EQ(ANI_WALK, static_cast<int>(fx->ani_type())) << "the row ended";
    EXPECT_EQ(0, fx->dead());

    EXPECT_FALSE(fx->act()) << "declined on the walk row: the effect dies";
    EXPECT_EQ(1, fx->dead());
    EXPECT_EQ(3u, log_since_mark().size()) << "on_act ran on each of the three ticks";
}

TEST_F(SemWalker1, an_effect_whose_on_act_claims_the_tick_neither_animates_nor_dies)
{
    register_test_hooks(
        "og.register_hooks('effect', 'core:flash', {\n"
        "  on_act = function(self) return true end,\n"
        "})\n");
    walker* fx = world().add_fx_ob(Order::FX, FAMILY_FLASH);
    ASSERT_NE(nullptr, fx);
    fx->ani = two_frame_table();
    fx->ani_count = 0;
    fx->set_ani_type(ANI_ATTACK);
    fx->set_cycle(0);
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(fx->act());
    EXPECT_EQ(0, static_cast<int>(fx->cycle())) << "claimed: the row does not advance";
    EXPECT_EQ(ANI_ATTACK, static_cast<int>(fx->ani_type())) << "claimed: the row never ends";
    EXPECT_EQ(0, fx->dead()) << "claimed: alive";

    fx->set_ani_type(ANI_WALK);
    EXPECT_TRUE(fx->act());
    EXPECT_EQ(0, fx->dead()) << "claimed: alive even on the walk row";
}

// ---------------------------------------------------------------------------
// effect.cpp:80 effect::act (walk row: die, and the death hook runs)
// ---------------------------------------------------------------------------

// A default effect back on its walk row dies that tick, and act() runs its
// death: a bomb's on_death sets off exactly one explosion, answering to the
// bomb's owner.
// RED (planted): effect::act's `death();` removed.
TEST_F(SemWalker1, a_default_effect_on_its_walk_row_dies_and_runs_its_death_hook)
{
    walker* thief = living_at(FAMILY_THIEF, 0, 128, 128);
    ASSERT_NE(nullptr, thief);
    thief->stats()->set_level(3);
    walker* bomb = world().add_ob(Order::FX, FAMILY_BOMB);
    ASSERT_NE(nullptr, bomb);
    bomb->setxy(128, 128);
    bomb->set_owner(thief);
    bomb->set_damage(12.0f);
    bomb->set_ani_type(ANI_WALK);
    bomb->set_dead(0);

    const auto before = snapshot_ptrs(world().oblist);
    EXPECT_FALSE(bomb->act());
    EXPECT_EQ(1, bomb->dead());
    EXPECT_EQ(1, bomb->death_called()) << "act() ran death()";
    const auto blasts = new_of(world().oblist, before, Order::FX, FAMILY_EXPLOSION);
    ASSERT_EQ(1u, blasts.size()) << "the bomb's on_death set off one explosion";
    EXPECT_EQ(thief, blasts[0]->owner());
}

// ---------------------------------------------------------------------------
// effect.cpp:155 effect::death
// ---------------------------------------------------------------------------

// An effect's death consequence happens once however often death() is
// reached: two calls, one explosion.
// RED (planted): `if (death_called()) return 0;` -> `&& false`.
TEST_F(SemWalker1, an_effect_dies_only_once_however_often_death_runs)
{
    walker* thief = living_at(FAMILY_THIEF, 0, 128, 128);
    ASSERT_NE(nullptr, thief);
    thief->stats()->set_level(3);
    walker* bomb = world().add_ob(Order::FX, FAMILY_BOMB);
    ASSERT_NE(nullptr, bomb);
    bomb->setxy(128, 128);
    bomb->set_owner(thief);
    bomb->set_damage(12.0f);
    bomb->set_dead(1);

    const auto before = snapshot_ptrs(world().oblist);
    EXPECT_TRUE(bomb->death());
    EXPECT_FALSE(bomb->death());
    EXPECT_EQ(1u, new_of(world().oblist, before, Order::FX, FAMILY_EXPLOSION).size())
        << "again: no second blast";
}

// ---------------------------------------------------------------------------
// walker.cpp:76 query_difficulty_percent
// ---------------------------------------------------------------------------

// An unset or non-positive difficulty, or no world at all, is normal: 100
// percent. A generator's hp is 100 per level scaled by it.
// RED (planted): `difficulty > 0` -> `>= 0`; the no-world `return 100u` -> 50u.
TEST_F(SemWalker1, an_unset_difficulty_or_no_world_means_one_hundred_percent)
{
    walker* gen = generator_at(FAMILY_TENT, 1, 320, 320);
    ASSERT_NE(nullptr, gen);
    const auto hp_at = [&](short difficulty) {
        world().difficulty = difficulty;
        gen->set_difficulty(3);
        return gen->stats()->hitpoints();
    };
    EXPECT_FLOAT_EQ(300.0f, hp_at(100)) << "100 percent";
    EXPECT_FLOAT_EQ(600.0f, hp_at(200)) << "200 percent";
    EXPECT_FLOAT_EQ(300.0f, hp_at(0)) << "unset: 100 percent";
    EXPECT_FLOAT_EQ(300.0f, hp_at(static_cast<short>(-5))) << "negative: 100 percent";

    world().difficulty = 200;
    GameWorld* const saved = current_game->world;
    current_game->world = nullptr;
    gen->set_difficulty(3);
    current_game->world = saved;
    EXPECT_FLOAT_EQ(300.0f, gen->stats()->hitpoints()) << "no world: 100 percent";
}

// ---------------------------------------------------------------------------
// walker.cpp:96 query_generator_rate_percent
// ---------------------------------------------------------------------------

// The generator rate is clamped to 25..400 percent: a wild 5000 runs exactly
// like 400 and a 3 exactly like 25 (same wins, same stream). Each spawn win
// heals the post one hp, so the hp counts the wins.
// RED (planted): the clamp's bounds widened to (rate, 1, 5000).
TEST_F(SemWalker1, a_wild_generator_rate_runs_as_the_nearest_bound)
{
    struct Run
    {
        float wins = 0.0f;
        std::uint32_t rng = 0;
    };
    const auto run = [&](short rate) {
        fresh_world();
        walker* gen = plain_at(Order::Generator, FAMILY_TENT, 1, 320, 320);
        gen->ani = nullptr;  // init_fire's animate() answers 0: no spawn
        gen->stats()->set_level(20);
        gen->stats()->set_max_hitpoints(100000.0f);
        gen->stats()->set_hitpoints(0.0f);
        gen->set_act_type(ACT_GENERATE);
        world().generator_rate = rate;
        world().rng_.state_ = 777u;
        for (int t = 0; t < 600; ++t) {
            gen->set_ani_type(ANI_WALK);
            gen->set_busy(0.0f);
            gen->act();
        }
        return Run{gen->stats()->hitpoints(), world().rng_.state_};
    };
    const Run r5000 = run(5000);
    const Run r400 = run(400);
    const Run r300 = run(300);
    const Run r3 = run(3);
    const Run r25 = run(25);
    const Run r50 = run(50);
    EXPECT_FLOAT_EQ(r400.wins, r5000.wins) << "above 400: runs as 400";
    EXPECT_EQ(r400.rng, r5000.rng);
    EXPECT_FLOAT_EQ(r25.wins, r3.wins) << "below 25: runs as 25";
    EXPECT_EQ(r25.rng, r3.rng);
    // The controls: the rate is live at both ends, so the equalities above
    // are the clamp and not a rate that does nothing.
    EXPECT_NE(r400.rng, r300.rng) << "400 and 300 differ";
    EXPECT_NE(r25.rng, r50.rng) << "25 and 50 differ";
}

// ---------------------------------------------------------------------------
// walker.cpp:481 walker::init_fire (turn before firing)
// ---------------------------------------------------------------------------

// A living that faces away from the shot turns instead of shooting: a player
// does not fire that call, a bot turns one step. enddir is overwritten only
// when the shot's direction differs from curdir, so a walk turn still pending
// (enddir != curdir) blocks a shot in the direction the walker already faces,
// and the bot then turns toward the WALK direction, away from the shot. The
// old game's init_fire has the same shape (openglad-master src/walker.cpp:669).
// A generator does not turn. No call here launches a weapon or pays the delay.
// RED (planted): `if (facing(xdir, ydir) != curdir())` -> `if (true)` (the
// pending turn no longer blocks); `return turn(enddir());` -> `return 0;`.
TEST_F(SemWalker1, classic_a_living_facing_away_or_mid_turn_turns_instead_of_firing)
{
    struct Out
    {
        bool fired;
        int curdir;
        int enddir;
        std::size_t weapons;
        float busy;
        int ani;
    };
    const auto shoot = [&](short act, int curdir, int enddir, short dx, short dy) {
        fresh_world();
        walker* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
        s->set_act_type(act);
        s->set_curdir(static_cast<signed char>(curdir));
        s->set_enddir(static_cast<char>(enddir));
        s->set_fire_frequency(6.0f);
        const bool fired = s->init_fire(dx, dy);
        return Out{fired, s->curdir(), s->enddir(), live_weapons(), s->busy(),
                   s->ani_type()};
    };

    // Requested facing differs from curdir.
    Out o = shoot(ACT_CONTROL, FACE_UP, FACE_UP, 1, 0);
    EXPECT_FALSE(o.fired) << "player, facing away: no shot this call";
    EXPECT_EQ(FACE_UP, o.curdir) << "player, facing away: no turn";
    EXPECT_EQ(FACE_RIGHT, o.enddir) << "player, facing away: enddir = the shot";
    EXPECT_EQ(0u, o.weapons);
    EXPECT_FLOAT_EQ(0.0f, o.busy);
    o = shoot(ACT_RANDOM, FACE_UP, FACE_UP, 1, 0);
    EXPECT_EQ(FACE_UP_RIGHT, o.curdir) << "bot, facing away: one step toward the shot";
    EXPECT_EQ(FACE_RIGHT, o.enddir);
    EXPECT_EQ(0u, o.weapons) << "bot, facing away: no weapon";
    EXPECT_FLOAT_EQ(0.0f, o.busy) << "bot, facing away: the delay is not paid";
    EXPECT_EQ(ANI_WALK, o.ani);

    // Facing the shot already, a walk turn to the left still pending.
    o = shoot(ACT_CONTROL, FACE_UP, FACE_LEFT, 0, -1);
    EXPECT_FALSE(o.fired) << "player, turn pending: no shot";
    EXPECT_EQ(FACE_UP, o.curdir);
    EXPECT_EQ(FACE_LEFT, o.enddir) << "player, turn pending: enddir keeps the walk";
    EXPECT_EQ(0u, o.weapons);
    EXPECT_FLOAT_EQ(0.0f, o.busy);
    o = shoot(ACT_RANDOM, FACE_UP, FACE_LEFT, 0, -1);
    EXPECT_EQ(FACE_UP_LEFT, o.curdir)
        << "bot, turn pending: one step toward the walk, away from the shot";
    EXPECT_EQ(FACE_LEFT, o.enddir);
    EXPECT_EQ(0u, o.weapons) << "bot, turn pending: no weapon";
    EXPECT_FLOAT_EQ(0.0f, o.busy);
    EXPECT_EQ(ANI_WALK, o.ani);

    // A generator fires in any direction: no turn, straight to the swing.
    fresh_world();
    walker* tent = generator_at(FAMILY_TENT, 1, 320, 320);
    ASSERT_NE(nullptr, tent);
    tent->set_curdir(static_cast<signed char>(FACE_UP));
    tent->set_enddir(static_cast<char>(FACE_UP));
    tent->set_fire_frequency(6.0f);
    EXPECT_TRUE(tent->init_fire(1, 0));
    EXPECT_EQ(ANI_ATTACK, static_cast<int>(tent->ani_type())) << "generator: no turn";
    EXPECT_FLOAT_EQ(6.0f, tent->busy());
}

// ---------------------------------------------------------------------------
// walker.cpp:501 walker::init_fire (walk row swings, any other row fires now)
// ---------------------------------------------------------------------------

// Past the busy gate the full delay is paid first. A walker in another
// animation than walking fires at once: a weapon this call; a refused shot (no
// magic) still costs the delay and spends nothing.
// RED (planted): `if (ani_type() == ANI_WALK)` -> `if (true)` (every walker
// swings first, no weapon this call).
TEST_F(SemWalker1, a_walker_in_another_animation_fires_at_once_and_pays_the_delay_either_way)
{
    walker* elf = living_at(FAMILY_ELF, 1, 320, 320);
    ASSERT_NE(nullptr, elf);
    elf->set_current_weapon(FAMILY_ROCK);
    face(elf, FACE_RIGHT, 1.0f, 0.0f);
    elf->set_fire_frequency(7.0f);
    elf->set_ani_type(ANI_TELE_IN);
    elf->stats()->set_weapon_cost(20);
    elf->stats()->set_magicpoints(100.0f);
    EXPECT_TRUE(elf->init_fire(1, 0));
    EXPECT_EQ(1u, live_weapons()) << "not walking, shot paid for: a weapon this call";
    EXPECT_FLOAT_EQ(7.0f, elf->busy()) << "the delay is paid";
    EXPECT_FLOAT_EQ(80.0f, elf->stats()->magicpoints());

    fresh_world();
    elf = living_at(FAMILY_ELF, 1, 320, 320);
    elf->set_current_weapon(FAMILY_ROCK);
    face(elf, FACE_RIGHT, 1.0f, 0.0f);
    elf->set_fire_frequency(7.0f);
    elf->set_ani_type(ANI_ATTACK);
    elf->stats()->set_weapon_cost(20);
    elf->stats()->set_magicpoints(0.0f);
    EXPECT_FALSE(elf->init_fire(1, 0)) << "not walking, no magic: refused";
    EXPECT_EQ(0u, live_weapons()) << "refused: no weapon";
    EXPECT_FLOAT_EQ(7.0f, elf->busy()) << "refused: the delay is paid all the same";
    EXPECT_FLOAT_EQ(0.0f, elf->stats()->magicpoints());
}

// ---------------------------------------------------------------------------
// walker.cpp:543 walker::fire (heading per facing, with the waver)
// ---------------------------------------------------------------------------

// A shot flies along the shooter's facing at the weapon's stepsize with a
// random waver w = rng(h+1) - h/2, h = stepsize/2: cardinal shots waver on
// the cross axis, the diagonals by a fixed pattern per diagonal. The same
// switch in the old game's walker::fire.
// RED (planted): FACE_UP_LEFT's `-stepsize() - waver` -> `- stepsize() + waver`;
// FACE_DOWN's `set_lastx(waver)` -> `set_lastx(-waver)`.
TEST_F(SemWalker1, a_shot_flies_along_the_facing_with_its_waver_across_or_along_the_diagonal)
{
    const struct Facing
    {
        int face;
        float dx;
        float dy;
    } facings[] = {
        {FACE_UP, 0.0f, -1.0f},       {FACE_UP_RIGHT, 1.0f, -1.0f},
        {FACE_RIGHT, 1.0f, 0.0f},     {FACE_DOWN_RIGHT, 1.0f, 1.0f},
        {FACE_DOWN, 0.0f, 1.0f},      {FACE_DOWN_LEFT, -1.0f, 1.0f},
        {FACE_LEFT, -1.0f, 0.0f},     {FACE_UP_LEFT, -1.0f, -1.0f},
    };
    for (const Facing& f : facings) {
        SCOPED_TRACE(std::format("facing {}", f.face));
        fresh_world();
        walker* elf = living_at(FAMILY_ELF, 1, 320, 320);
        ASSERT_NE(nullptr, elf);
        elf->set_current_weapon(FAMILY_ROCK);
        face(elf, f.face, f.dx, f.dy);

        // A stream position whose waver is not zero, so every sign shows.
        walker* probe = elf->create_weapon();
        ASSERT_NE(nullptr, probe);
        const float step = probe->stepsize();
        probe->set_dead(1);
        const signed char h = static_cast<signed char>(std::clamp(step / 2, 0.0f, 127.0f));
        signed char waver = 0;
        std::uint32_t seed = 1;
        for (; seed < 1000; ++seed) {
            og::sim::SimRandom r(seed);
            waver = static_cast<signed char>(
                r.next(static_cast<std::uint32_t>(h + 1)) - static_cast<std::uint32_t>(h / 2));
            if (waver != 0)
                break;
        }
        ASSERT_NE(0, waver);
        world().rng_.state_ = seed;
        const float w = static_cast<float>(waver);

        walker* rock = elf->fire();
        ASSERT_NE(nullptr, rock);
        float hx = 0.0f;
        float hy = 0.0f;
        switch (f.face) {
        case FACE_RIGHT: hx = step; hy = w; break;
        case FACE_LEFT: hx = -step; hy = w; break;
        case FACE_UP: hx = w; hy = -step; break;
        case FACE_DOWN: hx = w; hy = step; break;
        case FACE_UP_RIGHT: hx = step + w; hy = -step + w; break;
        case FACE_UP_LEFT: hx = -step - w; hy = -step + w; break;
        case FACE_DOWN_RIGHT: hx = step - w; hy = step + w; break;
        case FACE_DOWN_LEFT: hx = -step + w; hy = step + w; break;
        default: FAIL() << "not a facing";
        }
        EXPECT_FLOAT_EQ(hx, rock->lastx()) << "x heading, waver " << static_cast<int>(waver);
        EXPECT_FLOAT_EQ(hy, rock->lasty()) << "y heading, waver " << static_cast<int>(waver);
    }
}

// ---------------------------------------------------------------------------
// walker.cpp:598 walker::fire (a dormant cell stops spawns, not shots)
// ---------------------------------------------------------------------------

// A living's shot flies over a delayed-spawn (dormant) walker that has not
// entered the world: the projectile launches, the dormant walker is left
// untouched.
// RED (planted): the discard's `query_order() == Order::Generator &&` dropped.
TEST_F(SemWalker1, a_livings_shot_flies_over_a_dormant_walker)
{
    walker* elf = living_at(FAMILY_ELF, 1, 320, 320);
    ASSERT_NE(nullptr, elf);
    elf->set_current_weapon(FAMILY_ROCK);
    face(elf, FACE_RIGHT, 1.0f, 0.0f);
    walker* lurker = living_at(FAMILY_ORC, 2, 336, 316, 24, 24);
    ASSERT_NE(nullptr, lurker);
    lurker->set_spawn_delay(60000);
    lurker->set_dormant(true);
    ASSERT_TRUE(world().dormant_occupies_spot(lurker) == false);

    walker* rock = elf->fire();
    ASSERT_NE(nullptr, rock) << "the shot launches over the dormant cell";
    EXPECT_TRUE(world().dormant_occupies_spot(rock)) << "premise: the pad is on the dormant walker";
    EXPECT_EQ(0, rock->dead());
    EXPECT_EQ(1u, live_weapons());
    EXPECT_TRUE(lurker->dormant());
    EXPECT_EQ(0, lurker->dead());
    EXPECT_FLOAT_EQ(400.0f, lurker->stats()->hitpoints());
}

// ---------------------------------------------------------------------------
// walker.cpp:611 walker::fire (a landed blow: clang, lunge, on_melee_hit)
// ---------------------------------------------------------------------------

// A landed melee blow clangs; a living attacker also lunges along its facing
// and its family's on_melee_hit runs on the victim. A generator's landed blow
// clangs but neither lunges nor runs the hook. A refused blow (an ally on the
// pad) is silent and still.
// RED (planted): `if (query_order() == Order::Living)` (the lunge) ->
// `if (true)`; `if (order() == Order::Living)` (the hook) -> `if (true)`.
TEST_F(SemWalker1, a_landed_blow_clangs_and_a_living_attacker_lunges_and_runs_its_hook)
{
    const std::string hooks =
        "og.register_hooks('living', 'core:soldier', {\n"
        "  on_melee_hit = function(self, target) og.log('melee hit') end,\n"
        "})\n";

    register_test_hooks(hooks);
    walker* soldier = living_at(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, soldier);
    face(soldier, FACE_RIGHT, 1.0f, 0.0f);
    walker* orc = living_at(FAMILY_ORC, 1, 337, 320);
    ASSERT_NE(nullptr, orc);
    mark_log();
    EXPECT_EQ(nullptr, soldier->fire());
    EXPECT_EQ(1u, count_sound(SOUND_CLANG)) << "living, landed: one clang";
    EXPECT_FLOAT_EQ(1.0f, soldier->attack_lunge()) << "living, landed: a full lunge";
    EXPECT_FLOAT_EQ(soldier->get_current_angle(), soldier->attack_lunge_angle())
        << "living, landed: along its facing";
    EXPECT_EQ(std::vector<std::string>{"melee hit"}, log_since_mark())
        << "living, landed: on_melee_hit ran once";

    // Refused: an invulnerable foe on the pad. (An ally never holds a
    // weapon's pad: the weapon passes over its own side.)
    fresh_world();
    register_test_hooks(hooks);
    soldier = living_at(FAMILY_SOLDIER, 0, 320, 320);
    face(soldier, FACE_RIGHT, 1.0f, 0.0f);
    walker* warded = living_at(FAMILY_ORC, 1, 337, 320);
    ASSERT_NE(nullptr, warded);
    warded->set_invulnerable_left(10);
    mark_log();
    EXPECT_EQ(nullptr, soldier->fire());
    EXPECT_FLOAT_EQ(400.0f, warded->stats()->hitpoints()) << "refused: no damage";
    EXPECT_TRUE(sounds().empty()) << "refused: no clang";
    EXPECT_FLOAT_EQ(0.0f, soldier->attack_lunge()) << "refused: no lunge";
    EXPECT_TRUE(log_since_mark().empty()) << "refused: no hook";

    // A generator whose pad a foe holds strikes it: a clang only. (A tent is
    // family 0, the soldier's number, so a hook dispatched for it would run
    // the soldier's on_melee_hit.)
    fresh_world();
    register_test_hooks(hooks);
    walker* tent = generator_at(FAMILY_TENT, 1, 320, 320);
    ASSERT_NE(nullptr, tent);
    face(tent, FACE_RIGHT, 1.0f, 0.0f);
    walker* foe = living_at(FAMILY_SOLDIER, 0,
                            static_cast<short>(320 + tent->sizex() + 1), 320);
    ASSERT_NE(nullptr, foe);
    tent->set_damage(5.0f);
    mark_log();
    EXPECT_EQ(nullptr, tent->fire());
    EXPECT_EQ(1u, count_sound(SOUND_CLANG)) << "generator, landed: one clang";
    EXPECT_FLOAT_EQ(0.0f, tent->attack_lunge()) << "generator: no lunge";
    EXPECT_TRUE(log_since_mark().empty()) << "generator: no on_melee_hit";
}

// ---------------------------------------------------------------------------
// walker.cpp:628 walker::fire (a player's swing counts as a shot)
// ---------------------------------------------------------------------------

// A player guy's swing at a live body counts as a shot, landed or refused. A
// hostile non-living body (a generator) that attack() accepts has already
// been un-counted inside attack(), so that swing nets 0; one attack() refuses
// before that (an invulnerable one) nets +1. (A friendly body or a treasure
// never holds a weapon's pad: ob_pass_check lets the weapon over them.) A walker with no guy records
// nothing and still lands the blow. The old game counts the same way.
// RED (planted): the melee arm's `if (myguy)` -> `if (myguy && false)`.
TEST_F(SemWalker1, a_players_swing_at_a_body_counts_as_a_shot_except_a_generator_it_hits)
{
    struct Case
    {
        const char* name;
        int expect;  // total_shots and scen_shots after the swing
    };
    const auto swing = [&](const std::string& kind) {
        fresh_world();
        walker* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
        face(s, FACE_RIGHT, 1.0f, 0.0f);
        s->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        if (kind == "enemy living") {
            living_at(FAMILY_ORC, 1, 337, 320);
        } else if (kind == "invulnerable enemy living") {
            living_at(FAMILY_ORC, 1, 337, 320)->set_invulnerable_left(10);
        } else if (kind == "enemy generator") {
            generator_at(FAMILY_TENT, 1, 337, 320);
        } else if (kind == "invulnerable enemy generator") {
            generator_at(FAMILY_TENT, 1, 337, 320)->set_invulnerable_left(10);
        } else if (kind == "wall") {
            tile(21, 20, PIX_H_WALL1);
        }
        EXPECT_EQ(nullptr, s->fire());
        return std::pair<int, int>{s->myguy->total_shots, s->myguy->scen_shots};
    };
    const Case cases[] = {
        {"enemy living", 1},
        {"invulnerable enemy living", 1},
        {"enemy generator", 0},
        {"invulnerable enemy generator", 1},
        {"wall", 0},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        const auto [total, scen] = swing(c.name);
        EXPECT_EQ(c.expect, total) << "total_shots";
        EXPECT_EQ(c.expect, scen) << "scen_shots";
    }

    // No guy: nothing to count, and the blow lands.
    fresh_world();
    walker* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    face(s, FACE_RIGHT, 1.0f, 0.0f);
    walker* orc = living_at(FAMILY_ORC, 1, 337, 320);
    EXPECT_EQ(nullptr, s->fire());
    EXPECT_EQ(nullptr, s->myguy) << "no guy: none is made";
    EXPECT_EQ(s->entity_id(), orc->last_attacker_id()) << "no guy: the blow lands";
}

// ---------------------------------------------------------------------------
// walker.cpp:637 walker::fire (melee-only families)
// ---------------------------------------------------------------------------

// A melee-only family (BIT_NO_RANGED) never launches: with a clear pad the
// weapon is discarded, the cost is already paid, and no shot is counted. The
// old game charges first too.
// RED (planted): `else if (stats_->query_bit_flags(BIT_NO_RANGED))` -> `&& false`.
TEST_F(SemWalker1, a_melee_only_walker_pays_for_a_shot_it_never_launches)
{
    walker* orc = living_at(FAMILY_ORC, 1, 320, 320);
    ASSERT_NE(nullptr, orc);
    ASSERT_TRUE(orc->stats()->query_bit_flags(BIT_NO_RANGED)) << "premise: the orc is melee-only";
    face(orc, FACE_RIGHT, 1.0f, 0.0f);
    orc->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
    orc->stats()->set_weapon_cost(5);
    orc->stats()->set_magicpoints(20.0f);
    EXPECT_EQ(nullptr, orc->fire());
    EXPECT_EQ(0u, live_weapons()) << "melee-only: the weapon is discarded";
    EXPECT_FLOAT_EQ(15.0f, orc->stats()->magicpoints()) << "melee-only: the cost is paid";
    EXPECT_EQ(0, orc->myguy->total_shots) << "melee-only: no shot counted";
    EXPECT_EQ(0, orc->myguy->scen_shots);

    orc->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    EXPECT_NE(nullptr, orc->fire()) << "flag clear: the shot launches";
    EXPECT_EQ(1u, live_weapons());
    EXPECT_FLOAT_EQ(10.0f, orc->stats()->magicpoints());
    EXPECT_EQ(1, orc->myguy->total_shots);
}

// ---------------------------------------------------------------------------
// walker.cpp walker::fire (a family hook's veto, #428)
// ---------------------------------------------------------------------------

// A family's on_fire_weapon answering false vetoes the shot: fire() answers
// nullptr and the weapon it built dies, as at every other refusal in fire()
// (#428). Nothing flies: no live weapon, no launch sound, no shot counted. The
// shipped soldier's veto kills its weapon itself; this hook does not, so the
// kill is fire()'s. The same elf under a hook answering true launches its rock.
// RED (planted): the veto arm's `weapon->set_dead(1);` deleted.
TEST_F(SemWalker1, a_shot_its_family_hook_vetoes_leaves_no_live_weapon)
{
    const auto elf_with_hook = [&](const char* answer) {
        fresh_world();
        register_test_hooks(std::format(
            "og.register_hooks('living', 'core:elf', {{\n"
            "  on_fire_weapon = function(self, weapon) return {} end,\n"
            "}})\n",
            answer));
        walker* elf = living_at(FAMILY_ELF, 0, 320, 320);
        elf->set_current_weapon(FAMILY_ROCK);
        face(elf, FACE_RIGHT, 1.0f, 0.0f);
        elf->set_owned_myguy(std::make_unique<guy>(FAMILY_ELF));
        return elf;
    };

    walker* elf = elf_with_hook("false");
    ASSERT_NE(nullptr, elf);
    EXPECT_EQ(nullptr, elf->fire()) << "vetoed: fire() answers no shot";
    ASSERT_EQ(1u, world().weaplist.size()) << "premise: fire() built one weapon before the hook";
    EXPECT_EQ(1, world().weaplist.front()->dead()) << "vetoed: the built weapon is dead";
    EXPECT_EQ(0u, live_weapons()) << "vetoed: no live weapon";
    EXPECT_EQ(0u, count_sound(SOUND_FWIP)) << "vetoed: no launch sound";
    EXPECT_EQ(0, elf->myguy->total_shots) << "vetoed: no shot counted";

    elf = elf_with_hook("true");
    ASSERT_NE(nullptr, elf);
    EXPECT_NE(nullptr, elf->fire()) << "allowed: the rock flies";
    EXPECT_EQ(1u, live_weapons()) << "allowed: one live weapon";
    EXPECT_EQ(1u, count_sound(SOUND_FWIP)) << "allowed: the rock's fwip";
    EXPECT_EQ(1, elf->myguy->total_shots) << "allowed: one shot counted";
}

// ---------------------------------------------------------------------------
// walker.cpp:655 walker::fire (a player's launch counts as a shot)
// ---------------------------------------------------------------------------

// Every ranged launch by a player guy counts once in total_shots and
// scen_shots; a bot with no guy launches the same and records nothing.
// RED (planted): the ranged arm's `if (myguy)` -> `if (myguy && false)`.
TEST_F(SemWalker1, every_launch_by_a_player_counts_as_a_shot)
{
    walker* elf = living_at(FAMILY_ELF, 0, 320, 320);
    ASSERT_NE(nullptr, elf);
    elf->set_current_weapon(FAMILY_ROCK);
    face(elf, FACE_RIGHT, 1.0f, 0.0f);
    elf->set_owned_myguy(std::make_unique<guy>(FAMILY_ELF));
    for (int i = 0; i < 3; ++i)
        ASSERT_NE(nullptr, elf->fire());
    EXPECT_EQ(3, elf->myguy->total_shots);
    EXPECT_EQ(3, elf->myguy->scen_shots);
    EXPECT_EQ(3u, live_weapons());

    walker* bot = living_at(FAMILY_ELF, 1, 320, 600);
    bot->set_current_weapon(FAMILY_ROCK);
    face(bot, FACE_RIGHT, 1.0f, 0.0f);
    ASSERT_NE(nullptr, bot->fire());
    EXPECT_EQ(nullptr, bot->myguy) << "no guy: nothing recorded";
    EXPECT_EQ(4u, live_weapons());
    EXPECT_EQ(3, elf->myguy->total_shots) << "another walker's shot is not ours";
}

// ---------------------------------------------------------------------------
// walker.cpp:665 walker::fire (the launch sound)
// ---------------------------------------------------------------------------

// A launch makes its weapon's sound, the old game's per-family table: an
// arrow the bow, a fireball the blast, anything else the fwip. A generator
// spawn runs the same line with the SPAWNED LIVING as the weapon, so its
// family number is looked up among the weapons: a tower's mage (3) sounds as
// the fireball (3), a tent's skeleton (4) as the tree (4, fwip), and a
// family number with no weapon at all fwips. The old game compares the
// spawn's family against the same weapon constants (openglad-master
// src/walker.cpp:835-853), so the collision is classic. Every launch here is
// on screen (see the file comment).
// RED (planted): `get_weapon_family_descriptor(weapon->family())` ->
// `(weapon->family() + 1)`.
TEST_F(SemWalker1, classic_a_launch_sounds_by_its_weapon_family_number_spawns_included)
{
    struct Shot
    {
        const char* name;
        int shooter;
        int weapon;
        std::uint32_t sound;
    };
    const Shot shots[] = {
        {"archer's arrow", FAMILY_ARCHER, FAMILY_ARROW, SOUND_BOW},
        {"mage's fireball", FAMILY_MAGE, FAMILY_FIREBALL, SOUND_BLAST},
        {"elf's rock", FAMILY_ELF, FAMILY_ROCK, SOUND_FWIP},
        {"soldier's knife", FAMILY_SOLDIER, FAMILY_KNIFE, SOUND_FWIP},
    };
    for (const Shot& s : shots) {
        SCOPED_TRACE(s.name);
        fresh_world();
        walker* w = living_at(s.shooter, 1, 320, 320);
        ASSERT_NE(nullptr, w);
        w->set_current_weapon(static_cast<unsigned short>(s.weapon));
        face(w, FACE_RIGHT, 1.0f, 0.0f);
        ASSERT_NE(nullptr, w->fire());
        EXPECT_EQ(1u, count_sound(s.sound)) << "weapon descriptor present: its sound";
        EXPECT_EQ(1u, sounds().size()) << "one launch, one sound";
    }

    struct Spawn
    {
        const char* name;
        int generator;
        std::uint32_t sound;
    };
    const Spawn spawns[] = {
        {"tower's mage = fireball slot", FAMILY_TOWER, SOUND_BLAST},
        {"tent's skeleton = tree slot", FAMILY_TENT, SOUND_FWIP},
    };
    for (const Spawn& s : spawns) {
        SCOPED_TRACE(s.name);
        fresh_world();
        walker* g = generator_at(s.generator, 1, 320, 320);
        ASSERT_NE(nullptr, g);
        face(g, FACE_RIGHT, 1.0f, 0.0f);
        ASSERT_NE(nullptr, g->fire());
        EXPECT_EQ(std::vector<std::uint32_t>{s.sound}, sounds())
            << "generator spawn: the weapon slot of the spawn's number";
    }

    // The core pack never reaches the null-descriptor fwip (every living
    // family number names a weapon slot); a pack living family at a free
    // slot does. A soldier's descriptor copied to a mod slot no weapon
    // family holds, and a tower set to spawn it: the spawn sounds
    // FWIP. The pack-store guard drops the slot again.
    for (int f = 0; f < NUM_FAMILIES; ++f) {
        if (get_family_descriptor(f) != nullptr) {
            EXPECT_NE(nullptr, get_weapon_family_descriptor(f)) << "living family " << f;
        }
    }
    {
        SCOPED_TRACE("pack living at a free slot: no weapon descriptor");
        // The first mod living slot no weapon family holds (the core weapon
        // span runs past the living one); below 128, as create_weapon
        // narrows the number through a char.
        int kModLiving = -1;
        for (int f = NUM_FAMILIES; f < 128 && kModLiving < 0; ++f)
            if (get_family_descriptor(f) == nullptr && get_weapon_family_descriptor(f) == nullptr)
                kModLiving = f;
        ASSERT_LE(NUM_FAMILIES, kModLiving) << "a free living slot with no weapon";
        FamilyDescriptor mod = *get_family_descriptor(FAMILY_SOLDIER);
        mod.family_id = kModLiving;
        ASSERT_TRUE(set_family_descriptor(kModLiving, mod));
        fresh_world();
        walker* g = generator_at(FAMILY_TOWER, 1, 320, 320);
        ASSERT_NE(nullptr, g);
        g->set_default_weapon(static_cast<unsigned short>(kModLiving));
        face(g, FACE_RIGHT, 1.0f, 0.0f);
        walker* spawn = g->fire();
        ASSERT_NE(nullptr, spawn);
        EXPECT_EQ(kModLiving, static_cast<int>(spawn->family())) << "the pack family spawned";
        EXPECT_EQ(std::vector<std::uint32_t>{SOUND_FWIP}, sounds())
            << "no weapon descriptor at the spawn's number: FWIP";
        reset_family_registry_mod_slots();
    }
}

// ---------------------------------------------------------------------------
// walker.cpp:672 / :674 walker::fire (entry animation and lifetime)
// ---------------------------------------------------------------------------

// A tent's and a treehouse's spawns enter as their family is made (a
// skeleton grows, an elf walks); a tower's teleport in. A tent's spawns are
// temporary, 800 + 11 * generator level ticks; tower and treehouse spawns
// are permanent (lifetime 0). The old game's switch does the same.
// RED (planted): the spawn_ani_type test made `if (true)` setting ANI_TELE_IN;
// `if (gfd->has_lifetime)` -> `if (true)`.
TEST_F(SemWalker1, a_tent_spawn_walks_in_for_a_while_and_tower_and_treehouse_spawns_stay)
{
    struct Case
    {
        const char* name;
        int generator;
        int ani;
        int lifetime;
    };
    const Case cases[] = {
        {"tent", FAMILY_TENT, ANI_SKEL_GROW, 800 + 4 * 11},
        {"tower", FAMILY_TOWER, ANI_TELE_IN, 0},
        {"treehouse", FAMILY_TREEHOUSE, ANI_WALK, 0},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world();
        walker* g = generator_at(c.generator, 3, 160, 128, 4);
        ASSERT_NE(nullptr, g);
        face(g, FACE_RIGHT, 1.0f, 0.0f);
        walker* spawn = g->fire();
        ASSERT_NE(nullptr, spawn);
        EXPECT_EQ(c.ani, static_cast<int>(spawn->ani_type())) << "entry animation";
        EXPECT_EQ(c.lifetime, spawn->lifetime()) << "lifetime";
    }
}

// ---------------------------------------------------------------------------
// walker.cpp:771 walker::compute_outline
// ---------------------------------------------------------------------------

// The outline steps from the one shown. An invulnerable walker with nothing
// else keeps the invulnerable outline. A named walker shows the name outline
// only to a viewer on another team, never with no viewer, and keeps it while
// nothing else is on. A flight outline whose flight ran out stays while only
// the name is left and the viewer is a teammate. Two statuses blink. The
// same chain in the old game's walker::draw.
// RED (planted): INVULNERABLE branch `if (flight_left())` -> `|| true`; NAMED
// branch `else if (flight_left())` -> `|| true`; FLYING branch `else if
// (invulnerable_left())` -> `|| true`; a `!viewer_control` NAMED arm added to
// the last branch.
TEST_F(SemWalker1, a_walkers_outline_steps_through_its_statuses_and_names_only_to_enemies)
{
    walker* subject = plain_at(Order::Living, FAMILY_SOLDIER, 2, 100, 100);
    walker* enemy = plain_at(Order::Living, FAMILY_SOLDIER, 1, 200, 100);
    walker* mate = plain_at(Order::Living, FAMILY_SOLDIER, 2, 300, 100);
    const auto reset = [&](unsigned char outline) {
        subject->set_outline(outline);
        subject->set_invulnerable_left(0);
        subject->set_flight_left(0);
        subject->set_invisibility_left(0);
        subject->stats()->set_bit_flags(BIT_NAMED, 0);
    };

    reset(OUTLINE_INVULNERABLE);
    subject->set_invulnerable_left(5);
    subject->compute_outline(enemy);
    EXPECT_EQ(OUTLINE_INVULNERABLE, subject->outline()) << "invulnerable alone stays invulnerable";

    reset(0);
    subject->stats()->set_bit_flags(BIT_NAMED, 1);
    subject->compute_outline(enemy);
    EXPECT_EQ(OUTLINE_NAMED, subject->outline()) << "from none, named, enemy viewer: named";
    subject->set_outline(0);
    subject->compute_outline(nullptr);
    EXPECT_EQ(0, subject->outline()) << "no viewer: never named";
    subject->set_outline(0);
    subject->compute_outline(mate);
    EXPECT_EQ(0, subject->outline()) << "teammate viewer: never named";

    reset(OUTLINE_NAMED);
    subject->stats()->set_bit_flags(BIT_NAMED, 1);
    subject->compute_outline(enemy);
    EXPECT_EQ(OUTLINE_NAMED, subject->outline()) << "named alone stays named (enemy viewer)";
    subject->compute_outline(mate);
    EXPECT_EQ(OUTLINE_NAMED, subject->outline()) << "named alone stays named (teammate viewer)";

    reset(OUTLINE_FLYING);
    subject->stats()->set_bit_flags(BIT_NAMED, 1);
    subject->compute_outline(mate);
    EXPECT_EQ(OUTLINE_FLYING, subject->outline())
        << "flight run out, only the name left, teammate viewer: stays flying";

    reset(0);
    subject->set_flight_left(9);
    subject->set_invulnerable_left(9);
    std::vector<int> blink;
    for (int i = 0; i < 4; ++i) {
        subject->compute_outline(enemy);
        blink.push_back(subject->outline());
    }
    EXPECT_EQ((std::vector<int>{OUTLINE_FLYING, OUTLINE_INVULNERABLE, OUTLINE_FLYING,
                                OUTLINE_INVULNERABLE}),
              blink)
        << "flying and invulnerable blink";
}

// ---------------------------------------------------------------------------
// walker.cpp:839 walker::compute_outline (the networked teammate marker)
// ---------------------------------------------------------------------------

// The team-colour marker for another human on your team, in networked play
// only, is never put on a bot or on an enemy human.
// RED (planted): the marker's `user() != -1` dropped; its
// `team_num() == viewer_control->team_num()` -> `!=`.
TEST_F(SemWalker1, the_networked_teammate_marker_skips_bots_and_enemies)
{
    walker* viewer = plain_at(Order::Living, FAMILY_SOLDIER, 0, 100, 100);
    viewer->set_user(0);
    walker* bot = plain_at(Order::Living, FAMILY_SOLDIER, 0, 200, 100);
    bot->set_user(-1);
    walker* foe = plain_at(Order::Living, FAMILY_SOLDIER, 1, 300, 100);
    foe->set_user(1);
    walker* mate = plain_at(Order::Living, FAMILY_SOLDIER, 0, 400, 100);
    mate->set_user(2);

    bot->compute_outline(viewer, true);
    EXPECT_EQ(0, bot->outline()) << "a bot teammate: no marker";
    foe->compute_outline(viewer, true);
    EXPECT_EQ(0, foe->outline()) << "an enemy human: no marker";
    mate->compute_outline(viewer, true);
    EXPECT_EQ(mate->query_team_color(), mate->outline()) << "control: a human teammate is marked";
}

// ---------------------------------------------------------------------------
// walker.cpp:853 walker::act (live links kept)
// ---------------------------------------------------------------------------

// A walker keeps a foe, leader and owner that are alive at the start of its
// tick, and forgets only the one that died.
// RED (planted): walker::act's three guards reduced to `if (foe())` etc.
TEST_F(SemWalker1, a_walker_keeps_its_living_foe_leader_and_owner)
{
    walker* gen = plain_at(Order::Generator, FAMILY_TENT, 1, 96, 96);
    walker* foe = plain_at(Order::Living, FAMILY_ORC, 0, 200, 96);
    walker* leader = plain_at(Order::Living, FAMILY_ORC, 1, 300, 96);
    walker* owner = plain_at(Order::Living, FAMILY_ORC, 1, 400, 96);
    gen->set_act_type(ACT_CONTROL);
    gen->set_foe(foe);
    gen->set_leader(leader);
    gen->set_owner(owner);
    ASSERT_TRUE(gen->act());
    EXPECT_EQ(foe, gen->foe()) << "alive: the foe is kept";
    EXPECT_EQ(leader, gen->leader()) << "alive: the leader is kept";
    EXPECT_EQ(owner, gen->owner()) << "alive: the owner is kept";

    leader->set_dead(1);
    ASSERT_TRUE(gen->act());
    EXPECT_EQ(nullptr, gen->leader()) << "dead: the leader is forgotten";
    EXPECT_EQ(foe, gen->foe()) << "the live foe beside it is kept";
    EXPECT_EQ(owner, gen->owner()) << "the live owner beside it is kept";
}

// ---------------------------------------------------------------------------
// walker.cpp:863 walker::act (an animation owns the tick)
// ---------------------------------------------------------------------------

// While a walker plays a non-walk animation the tick only advances it: the
// freeze timer, the fire delay and the order queue stay as they are. Back on
// the walk row the tick reaches the freeze check (one freeze tick burned).
// RED (planted): `if (ani_type() != ANI_WALK)` -> `if (false)`; and, apart,
// -> `if (ani_type() == ANI_WALK)`.
TEST_F(SemWalker1, an_animating_walker_only_advances_its_animation)
{
    walker* gen = plain_at(Order::Generator, FAMILY_TENT, 1, 96, 96);
    gen->set_act_type(ACT_CONTROL);
    gen->set_ani_type(ANI_ATTACK);
    gen->set_cycle(0);
    gen->set_busy(5.0f);
    gen->stats()->set_frozen_delay(3);
    gen->stats()->add_command(COMMAND_WALK, 4, 1, 0);
    EXPECT_TRUE(gen->act());
    EXPECT_EQ(1, static_cast<int>(gen->cycle())) << "animating: the row advanced";
    EXPECT_EQ(3, gen->stats()->frozen_delay()) << "animating: no freeze tick";
    EXPECT_FLOAT_EQ(5.0f, gen->busy()) << "animating: no busy drain";
    EXPECT_EQ(1u, gen->stats()->commands.size()) << "animating: the order waits";
    EXPECT_EQ(4, gen->stats()->commands.front().commandcount);

    gen->set_ani_type(ANI_WALK);
    gen->set_cycle(0);
    gen->stats()->set_frozen_delay(3);
    EXPECT_TRUE(gen->act());
    EXPECT_EQ(2, gen->stats()->frozen_delay()) << "walking: the freeze check runs";
    EXPECT_EQ(0, static_cast<int>(gen->cycle())) << "walking: no animation step";
    EXPECT_FLOAT_EQ(5.0f, gen->busy()) << "walking, frozen: no busy drain yet";
    EXPECT_EQ(4, gen->stats()->commands.front().commandcount);
}

// ---------------------------------------------------------------------------
// walker.cpp:873 walker::act (the fire delay drains, unclamped)
// ---------------------------------------------------------------------------

// The fire delay drains by one per free tick only while it is above zero: a
// zero stays zero, and a fractional negative is left exactly as it is (no
// clamp; every gate tests busy > 0, so it acts as zero). The old game drains
// its float busy the same way.
// RED (planted): `if (busy() > 0.0f)` -> `if (busy() > -1.0f)`.
TEST_F(SemWalker1, classic_the_fire_delay_drains_only_while_positive_and_is_not_clamped)
{
    walker* gen = plain_at(Order::Generator, FAMILY_TENT, 1, 96, 96);
    gen->set_act_type(ACT_CONTROL);
    gen->set_busy(3.0f);
    EXPECT_TRUE(gen->act());
    EXPECT_FLOAT_EQ(2.0f, gen->busy()) << "positive: one per tick";
    gen->set_busy(0.0f);
    EXPECT_TRUE(gen->act());
    EXPECT_FLOAT_EQ(0.0f, gen->busy()) << "zero stays zero";
    gen->set_busy(-0.5f);
    EXPECT_TRUE(gen->act());
    EXPECT_FLOAT_EQ(-0.5f, gen->busy()) << "a negative is neither drained nor clamped";
}

// ---------------------------------------------------------------------------
// walker.cpp:888 walker::act (an order that runs takes the tick)
// ---------------------------------------------------------------------------

// A queued order runs before the walker's own behaviour, and when it took the
// tick nothing else happens: the lunge and recoil do not decay and the act
// type does not run (an ACT_DIE walker stays alive). With no order the tick
// goes on to both (the control).
// RED (planted): `if (temp) return 1;` -> `if (temp && false)`.
TEST_F(SemWalker1, an_order_that_runs_takes_the_whole_tick)
{
    walker* gen = plain_at(Order::Generator, FAMILY_TENT, 1, 96, 96);
    gen->set_act_type(ACT_DIE);
    gen->set_attack_lunge(1.0f);
    gen->set_hit_recoil(1.0f);
    gen->stats()->force_command(COMMAND_WALK, 2, 1, 0);
    EXPECT_TRUE(gen->act());
    EXPECT_FLOAT_EQ(1.0f, gen->attack_lunge()) << "order ran: no lunge decay";
    EXPECT_FLOAT_EQ(1.0f, gen->hit_recoil()) << "order ran: no recoil decay";
    EXPECT_EQ(0, gen->dead()) << "order ran: the act type did not run";

    gen->stats()->clear_command();
    EXPECT_TRUE(gen->act());
    EXPECT_FLOAT_EQ(0.6f, gen->attack_lunge()) << "no order: the lunge decays";
    EXPECT_FLOAT_EQ(0.4f, gen->hit_recoil()) << "no order: the recoil decays";
    EXPECT_EQ(1, gen->dead()) << "no order: ACT_DIE runs";
}

// ---------------------------------------------------------------------------
// walker.cpp:911 walker::act (ACT_CONTROL and an unknown act type)
// ---------------------------------------------------------------------------

// A player-controlled walker does nothing on its own tick: no order, no move,
// no draw from the stream. An act type the switch does not know does nothing
// and reports it did nothing.
// RED (planted): ACT_CONTROL's `return 1;` -> `act_generate(); return 1;`;
// the default's `return 0;` -> `return 1;`.
TEST_F(SemWalker1, a_controlled_or_unknown_act_type_does_nothing_on_its_own)
{
    walker* gen = plain_at(Order::Generator, FAMILY_TENT, 1, 96, 96);
    gen->stats()->set_level(50);
    gen->stats()->set_hitpoints(10.0f);
    gen->stats()->set_max_hitpoints(20.0f);
    world().rng_.state_ = 2u;  // an ACT_GENERATE tick here would spawn

    gen->set_act_type(ACT_CONTROL);
    EXPECT_TRUE(gen->act());
    EXPECT_EQ(2u, world().rng_.state_) << "ACT_CONTROL: no draw";
    EXPECT_TRUE(gen->stats()->commands.empty()) << "ACT_CONTROL: no order";
    EXPECT_EQ(96, gen->xpos());
    EXPECT_EQ(96, gen->ypos());
    EXPECT_FLOAT_EQ(10.0f, gen->stats()->hitpoints());
    EXPECT_EQ(ANI_WALK, static_cast<int>(gen->ani_type()));

    gen->set_act_type(99);
    EXPECT_FALSE(gen->act()) << "unknown act type: nothing done";
    EXPECT_EQ(2u, world().rng_.state_) << "unknown: no draw";
    EXPECT_TRUE(gen->stats()->commands.empty());
    EXPECT_EQ(96, gen->xpos());
    EXPECT_EQ(96, gen->ypos());
    EXPECT_FLOAT_EQ(10.0f, gen->stats()->hitpoints());
    EXPECT_EQ(0, gen->dead());
}

// ---------------------------------------------------------------------------
// walker.cpp:946 walker::act (ACT_RANDOM: the special first)
// ---------------------------------------------------------------------------

// On the 1-in-80 tick a roaming walker tries its current special first; when
// it goes off, the price is paid and no wander is queued.
// RED (planted): `if (!special())` -> `if (!special() || true)`.
TEST_F(SemWalker1, a_roaming_walker_that_casts_on_its_wander_tick_does_not_wander)
{
    register_test_hooks(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  do_special = function(self) og.log('special'); return true end,\n"
        "})\n");
    walker* w = plain_at(Order::Living, FAMILY_SOLDIER, 1, 96, 96);
    w->set_act_type(ACT_RANDOM);
    w->set_current_special(1);
    w->stats()->set_special_cost(1, 7);
    w->stats()->set_max_magicpoints(600.0f);
    w->stats()->set_magicpoints(600.0f);
    const unsigned short price = og::sim::cast_cost(*w, 1, false);
    ASSERT_EQ(7, price) << "premise: slot 1 costs what it was stamped";
    world().rng_.state_ = 189u;  // next(4) == 0, next(20) == 0
    mark_log();
    EXPECT_TRUE(w->act());
    EXPECT_EQ(std::vector<std::string>{"special"}, log_since_mark()) << "the special went off";
    EXPECT_FLOAT_EQ(593.0f, w->stats()->magicpoints()) << "its price is paid";
    EXPECT_TRUE(w->stats()->commands.empty()) << "and no wander is queued";
}

// ---------------------------------------------------------------------------
// walker.cpp:959 walker::act (ACT_RANDOM: nobody to hunt)
// ---------------------------------------------------------------------------

// On the other three ticks in four a roaming walker with no foe looks for the
// nearest one; with nobody on the level it has none and queues nothing.
// RED (planted): `if (foe())` before the search -> `if (true)`.
TEST_F(SemWalker1, a_roaming_walker_with_nobody_to_hunt_idles)
{
    walker* tower = generator_at(FAMILY_TOWER, 1, 100, 100);
    ASSERT_NE(nullptr, tower);
    tower->set_act_type(ACT_RANDOM);
    tower->set_ani_type(ANI_WALK);
    tower->stats()->clear_command();
    world().rng_.state_ = 1u;  // next(4) != 0
    EXPECT_TRUE(tower->act());
    EXPECT_EQ(nullptr, tower->foe()) << "no foe on the level";
    EXPECT_TRUE(tower->stats()->commands.empty()) << "no foe: no search queued";
}

// ---------------------------------------------------------------------------
// walker.cpp:1097 walker::animate (teleport refused)
// ---------------------------------------------------------------------------

// When a teleport-out finishes and the family's teleport handler refuses
// (no spot), the walker stops where it is and walks.
// RED (planted): `handle_teleport(fd, this).value_or(false)` -> `.has_value()`.
TEST_F(SemWalker1, a_refused_teleport_leaves_the_walker_walking_where_it_stood)
{
    register_test_hooks(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  handle_teleport = function(self) og.log('teleport'); return false end,\n"
        "})\n");
    walker* w = plain_at(Order::Living, FAMILY_SOLDIER, 1, 96, 96);
    w->set_ani_type(ANI_TELE_OUT);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_cycle(5);  // past the end of the row
    mark_log();
    EXPECT_FALSE(w->animate());
    EXPECT_EQ(std::vector<std::string>{"teleport"}, log_since_mark()) << "the handler ran";
    EXPECT_EQ(ANI_WALK, static_cast<int>(w->ani_type())) << "refused: back to walking";
    EXPECT_EQ(0, static_cast<int>(w->cycle()));
    EXPECT_EQ(96, w->xpos()) << "refused: not moved";
    EXPECT_EQ(96, w->ypos());
}

// ---------------------------------------------------------------------------
// walker.cpp:1107 walker::animate (on_ani_complete declines)
// ---------------------------------------------------------------------------

// A finished animation offered to on_ani_complete that the hook does not
// claim returns the walker to walking.
// RED (planted): `on_ani_complete(fd2, this).value_or(false)` -> `.has_value()`.
TEST_F(SemWalker1, an_animation_the_hook_declines_returns_the_walker_to_walking)
{
    register_test_hooks(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  on_ani_complete = function(self) og.log('complete'); return false end,\n"
        "})\n");
    walker* w = plain_at(Order::Living, FAMILY_SOLDIER, 1, 96, 96);
    w->set_ani_type(ANI_SLIME_SPLIT);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_cycle(5);
    mark_log();
    EXPECT_TRUE(w->animate());
    EXPECT_EQ(std::vector<std::string>{"complete"}, log_since_mark()) << "the hook was offered it";
    EXPECT_EQ(ANI_WALK, static_cast<int>(w->ani_type())) << "declined: back to walking";
    EXPECT_EQ(0, static_cast<int>(w->cycle()));
    EXPECT_EQ(FAMILY_SOLDIER, w->family());
}

// ---------------------------------------------------------------------------
// walker.cpp:1122 walker::set_frame_from_current_walk_animation
// ---------------------------------------------------------------------------

namespace {
// Walk rows (0..7) three frames {1, 2, 3}; attack rows (8..15) two {5, 6};
// row 2 (FACE_RIGHT's walk row) absent in the holed variant.
signed char** walk_attack_table(bool hole_at_right)
{
    static std::array<std::array<signed char, 4>, 64> seqs{};
    static std::array<signed char*, 64> rows{};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (i < 8)
            seqs[i] = {1, 2, 3, -1};
        else
            seqs[i] = {5, 6, -1, -1};
        rows[i] = seqs[i].data();
    }
    if (hole_at_right)
        rows[FACE_RIGHT] = nullptr;
    return rows.data();
}
}  // namespace

// With no table, a facing outside the eight or no walk row for the facing,
// the helper answers false and leaves cycle and frame alone.
// RED (planted): `if (!ani) return false;` -> `return true;` (first guard);
// `dir_index >= NUM_FACINGS` -> `dir_index >= 64`; `if (!seq) return false;`
// -> `return true;`.
TEST_F(SemWalker1, the_walk_frame_helper_leaves_a_walker_without_a_walk_row_alone)
{
    walker* w = living_at(FAMILY_SOLDIER, 1, 96, 96);
    ASSERT_NE(nullptr, w);
    const auto arm = [&](const char* name, signed char** table, signed char curdir) {
        SCOPED_TRACE(name);
        w->ani = table;
        w->set_curdir(curdir);
        w->set_cycle(1);
        ASSERT_EQ(1, w->set_frame(4)) << "premise: frame 4 exists";
        EXPECT_FALSE(w->set_frame_from_current_walk_animation());
        EXPECT_EQ(1, static_cast<int>(w->cycle())) << "cycle untouched";
        EXPECT_EQ(4, w->frame()) << "frame untouched";
    };
    const auto own = w->ani;
    arm("no table", nullptr, static_cast<signed char>(FACE_RIGHT));
    arm("facing out of range", walk_attack_table(false), 9);
    arm("no walk row", walk_attack_table(true), static_cast<signed char>(FACE_RIGHT));
    w->ani = own;
}

// Walking during an attack swing advances and wraps the cycle against the
// WALK row of the facing, not the attack row: the frame is the walk row's.
// The old game indexes ani[curdir][cycle] the same way (openglad-master
// src/walker.cpp:466-468).
// RED (planted): the helper reading `ani[dir_index + ani_type() * NUM_FACINGS]`.
TEST_F(SemWalker1, classic_a_walk_step_during_an_attack_uses_the_walk_row)
{
    walker* w = living_at(FAMILY_SOLDIER, 1, 96, 96);
    ASSERT_NE(nullptr, w);
    const auto own = w->ani;
    w->ani = walk_attack_table(false);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_ani_type(ANI_ATTACK);
    w->set_cycle(2);  // past the attack row's two frames, inside the walk row's three
    EXPECT_TRUE(w->set_frame_from_current_walk_animation());
    EXPECT_EQ(2, static_cast<int>(w->cycle())) << "inside the walk row: kept";
    EXPECT_EQ(3, w->frame()) << "the walk row's third frame";
    w->set_cycle(3);
    EXPECT_TRUE(w->set_frame_from_current_walk_animation());
    EXPECT_EQ(0, static_cast<int>(w->cycle())) << "off the walk row's end: wraps";
    EXPECT_EQ(1, w->frame()) << "the walk row's first frame";
    w->ani = own;
}
