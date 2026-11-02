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
