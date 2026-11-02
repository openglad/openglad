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
// screen; the current game emits every positional sound unless a visibility
// test is installed (#468). These tests install one that answers "on screen",
// so what they pin (which sound, and that an on-screen hit sounds) is the
// old game's rule, not the #468 difference.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/event.h>
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
