/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch libs-1: the rules the review found in ai.lua, corpses.lua, effect_bomb.lua, effect_chain.lua, effect_cloud.lua, effect_common.lua, effect_door_open.lua, effect_ember.lua, effect_ghost_scare.lua.
// Each test is named after the rule it asserts.
//
// Every test runs headless on TestGameWorld (or the GameplayFixesBots field)
// and reaches the class-pack Lua through og::script::hooks, the door the sim
// uses; each one fails if a hook errored (ScopedHookFailureGuard), since an
// erroring hook is a silent no-op with no C++ behind it.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_sim_random_scope.h"
#include "bot_gate_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {

// --- shared rig ------------------------------------------------------------

// A walker of `order`/`family` at pixel (x, y) on `team`. Effects go on the
// object list (og.add_ob("fx", ...) is where the hooks put theirs too).
walker* put(GameWorld& w, Order order, int family, int x, int y,
            unsigned char team)
{
    walker* ob = w.add_ob(order, family);
    if (ob == nullptr)
        return nullptr;
    ob->setxy(static_cast<short>(x), static_cast<short>(y));
    ob->set_team_num(team);
    ob->set_real_team_num(255);
    return ob;
}

// A living with `hp` hit points, no armour, no queued command.
walker* put_living(GameWorld& w, int family, int x, int y, unsigned char team,
                   short level = 1, float hp = 500.0f)
{
    walker* ob = put(w, Order::Living, family, x, y, team);
    if (ob == nullptr)
        return nullptr;
    ob->stats()->set_level(level);
    ob->stats()->set_armor(0.0f);
    ob->stats()->set_max_hitpoints(hp);
    ob->stats()->set_hitpoints(hp);
    ob->stats()->clear_command();
    return ob;
}

// The live entities of `order`/`family` on all three lists.
std::vector<walker*> live_of(GameWorld& w, Order order, int family)
{
    std::vector<walker*> out;
    for (const GameWorld::EntityList* list : {&w.oblist, &w.fxlist, &w.weaplist}) {
        for (const auto& ob : *list) {
            if (ob != nullptr && ob->dead() == 0 && ob->query_order() == order &&
                static_cast<int>(static_cast<unsigned char>(ob->family())) == family)
                out.push_back(ob.get());
        }
    }
    return out;
}

int fx_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::FX, id);
}

float hp(const walker* w) { return w->stats()->hitpoints(); }

// The single command a shove leaves, or a note why there is not exactly one.
::testing::AssertionResult forced_walk(const walker* w, int count, int dx, int dy)
{
    const auto& q = w->stats()->commands;
    if (q.size() != 1)
        return ::testing::AssertionFailure() << q.size() << " commands queued, not 1";
    const command& c = q.front();
    if (c.commandtype != COMMAND_WALK || !c.forced || c.commandcount != count ||
        c.com1 != dx || c.com2 != dy)
        return ::testing::AssertionFailure()
               << "queued type " << c.commandtype << " forced " << c.forced
               << " count " << c.commandcount << " dir (" << c.com1 << ", "
               << c.com2 << "), wanted a forced WALK of " << count << " ("
               << dx << ", " << dy << ")";
    return ::testing::AssertionSuccess();
}

// Draws taken between two states of the sim stream (-1: more than 64).
int draws_between(std::uint32_t from, std::uint32_t to)
{
    og::sim::SimRandom r(from);
    for (int n = 0; n <= 64; ++n) {
        if (r.state_ == to)
            return n;
        (void)r.next(2);
    }
    return -1;
}

// A scripted sim stream that counts what it answered.
class CountingSequence : public IRandom {
public:
    explicit CountingSequence(std::initializer_list<std::uint32_t> vals) : vals_(vals) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        ++calls;
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = idx_ < vals_.size() ? vals_[idx_++] : vals_.back();
        return v % max_exclusive;
    }
    int calls = 0;

private:
    std::vector<std::uint32_t> vals_;
    std::size_t idx_ = 0;
};

const EffectFamilyDescriptor& efd_of(int family)
{
    return *get_effect_family_descriptor(family);
}

}  // namespace

// ===========================================================================
// ai.lua — M.foe_in_window, the soldier's classic CHARGE window (20, 75)
// ===========================================================================

class SemLibsAi : public og::test::bot_gates::GameplayFixesBots {};

// ai.lua:28 arm "no foe, a foe is findable". The old game's soldier ladder
// (living.cpp check_special, FAMILY_SOLDIER) assigns `foe =
// find_near_foe(this)` before measuring, so the find sticks.
TEST_F(SemLibsAi, a_classic_window_gate_keeps_the_nearest_foe_it_finds)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 1, 200.0f), 16, 16);
    living* far_foe = body(add(FAMILY_SOLDIER, 2, 300, 200, 1), 16, 16);
    ASSERT_TRUE(soldier && far_foe);
    ASSERT_EQ(nullptr, soldier->foe());
    ASSERT_EQ(100, soldier->distance_to_ob(far_foe));

    EXPECT_FALSE(gate(soldier, 1, 0)) << "100 px is outside the window";
    EXPECT_EQ(far_foe, soldier->foe())
        << "arm 'no foe, a foe is findable': the found foe is kept even when the window says no";
}

// ai.lua:28 arm "no foe and none findable".
TEST_F(SemLibsAi, a_classic_window_gate_with_nobody_to_find_stays_closed)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 1, 200.0f), 16, 16);
    ASSERT_NE(nullptr, soldier);
    EXPECT_FALSE(gate(soldier, 1, 0)) << "arm 'none findable': the gate is closed";
    EXPECT_EQ(nullptr, soldier->foe()) << "arm 'none findable': the foe stays nil";
}

// ai.lua:28 arm "walker has a foe": the held foe is measured, not the nearest.
TEST_F(SemLibsAi, a_classic_window_gate_answers_by_the_foe_it_already_holds)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 1, 200.0f), 16, 16);
    living* near_foe = body(add(FAMILY_SOLDIER, 2, 210, 200, 1), 16, 16);  // 10 px
    living* mid_foe = body(add(FAMILY_SOLDIER, 2, 250, 200, 1), 16, 16);   // 50 px
    living* far_foe = body(add(FAMILY_SOLDIER, 2, 300, 200, 1), 16, 16);   // 100 px
    ASSERT_TRUE(soldier && near_foe && mid_foe && far_foe);

    soldier->set_foe(far_foe);
    EXPECT_FALSE(gate(soldier, 1, 0))
        << "held foe at 100 px: closed, though a foe at 50 px sits in the window";
    EXPECT_EQ(far_foe, soldier->foe()) << "the held foe is kept";

    soldier->set_foe(mid_foe);
    EXPECT_TRUE(gate(soldier, 1, 0))
        << "held foe at 50 px: open, though the nearest foe (10 px) is too near";
    EXPECT_EQ(mid_foe, soldier->foe()) << "the held foe is kept";
}

// ai.lua:36: both edges of the window are open (strict compares).
TEST_F(SemLibsAi, the_classic_soldier_window_is_open_strictly_between_20_and_75_px)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 1, 200.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 220, 200, 1), 16, 16);
    ASSERT_TRUE(soldier && foe);
    soldier->set_foe(foe);
    const struct {
        short x;
        int distance;
        bool open;
    } cases[] = {{220, 20, false}, {221, 21, true}, {274, 74, true}, {275, 75, false}};
    for (const auto& c : cases) {
        foe->setxy(c.x, short{200});
        ASSERT_EQ(c.distance, soldier->distance_to_ob(foe));
        EXPECT_EQ(c.open, gate(soldier, 1, 0))
            << "distance exactly " << c.distance << (c.open ? " is inside" : " is shut");
    }
}

// ===========================================================================
// corpses.lua — body_for: RESURRECT on a foe's stain probes a ghost
// ===========================================================================

namespace {

void wall_cell(GameWorld& w, int col, int row)
{
    w.grid.data[static_cast<std::size_t>(col + w.grid.w * row)] = PIX_H_WALL1;
}

// A victim of `family`/`team` killed with its corner at (x, y): its stain.
walker* fallen(TestGameWorld& tw, int family, unsigned char team, int x, int y)
{
    walker* v = put(tw.world(), Order::Living, family, x, y, team);
    if (v == nullptr)
        return nullptr;
    v->stats()->set_level(1);
    v->stats()->set_hitpoints(0.0f);
    v->generate_bloodspot();
    walker* stain = nullptr;
    for (auto& u : tw.world().fxlist)
        if (u && u->query_order() == Order::Treasure && u->family() == FAMILY_STAIN &&
            !u->dead())
            stain = u.get();
    return stain;
}

std::vector<walker*> live_livings(GameWorld& w, int family, unsigned char team,
                                  const walker* except)
{
    std::vector<walker*> out;
    for (walker* ob : live_of(w, Order::Living, family))
        if (ob != except && ob->team_num() == team)
            out.push_back(ob);
    return out;
}

}  // namespace

// corpses.lua:26 arm "RESURRECT on a foe stain => probed family = core:ghost".
// The pocket of a_blocked_near_stain_does_not_hide_a_usable_far_one: grass
// row 12 between wall rows 11 and 13, wall column 15, the cleric (16 x 12)
// at (213, 194) in its mouth. A FOE soldier killed at (229, 194): a soldier
// (16 wide) would not fit in the 11 px between the cleric and the wall, an
// ethereal ghost does. With the setting on the cast measures the ghost it
// will raise and succeeds; the classic cast (the old game's walker.cpp
// RESURRECT: query_passable on the stain's own box) is refused.
TEST(SemLibsCorpses, resurrect_measures_a_foe_stain_for_the_ghost_it_will_raise)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(testing::Message() << "gameplay_fixes " << fixes);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.gameplay_fixes = fixes;
        for (const int col : {13, 14, 15}) {
            wall_cell(w, col, 11);
            wall_cell(w, col, 13);
        }
        wall_cell(w, 15, 12);
        walker* cleric = put_living(w, FAMILY_CLERIC, 213, 194, 0, 10, 100.0f);
        ASSERT_NE(nullptr, cleric);
        cleric->stats()->set_max_magicpoints(600.0f);
        cleric->stats()->set_magicpoints(600.0f);
        walker* blood = fallen(tw, FAMILY_SOLDIER, 1, 229, 194);
        ASSERT_NE(nullptr, blood);
        ASSERT_EQ(blood, w.find_nearest_blood(cleric));
        ASSERT_FALSE(cleric->is_friendly(blood)) << "a foe's stain";

        cleric->set_current_special(4);  // RESURRECT
        cleric->set_shifter_down(0);
        cleric->set_busy(0);
        walker::SpecialFailure why = walker::SpecialFailure::None;
        std::string reason;
        const bool ok = cleric->special(&why, &reason);
        const auto ghosts = live_livings(w, FAMILY_GHOST, 0, cleric);
        const auto soldiers = live_livings(w, FAMILY_SOLDIER, 0, cleric);
        EXPECT_EQ(0u, soldiers.size()) << "a foe's stain never rises as its own family";
        if (fixes == 0) {
            EXPECT_FALSE(ok) << "classic: the stain's box reaches into the wall";
            EXPECT_EQ("CORPSE IS BLOCKED", reason);
            EXPECT_EQ(0u, ghosts.size());
            EXPECT_EQ(0, blood->dead());
            continue;
        }
        EXPECT_TRUE(ok) << "arm 'foe stain': the ghost is measured and fits; refusal: " << reason;
        EXPECT_EQ(1, blood->dead());
        ASSERT_EQ(1u, ghosts.size()) << "one ghost of the cleric's team rises";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_bomb.lua — the blast's reach, its scorch, and who it touches
// ===========================================================================

namespace {

// One blast of an owner of `owner_level` at (200, 200), skip_exit as given,
// and one invulnerable foe orc `d` px due east (Manhattan, corner to
// corner, the finder's measure). True when the blast shoved it: the shove is
// a forced WALK of min(2 + level / 15, 8) steps east (checked exactly). An
// invulnerable witness keeps the shove (a wound would clear the queue).
struct Reach {
    bool reached = false;
    bool exact = false;
    std::string why;
};

Reach blast_reaches(short owner_level, short skip_exit, int d)
{
    Reach out;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* owner = put_living(w, FAMILY_SOLDIER, 600, 600, 0, owner_level);
    walker* foe = put_living(w, FAMILY_ORC, 200 + d, 200, 1);
    walker* blast = put(w, Order::FX, FAMILY_EXPLOSION, 200, 200, 0);
    if (!owner || !foe || !blast) {
        out.why = "rig";
        return out;
    }
    foe->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    if (blast->distance_to_ob(foe) != d) {
        out.why = "placement";
        return out;
    }
    blast->set_owner(owner);
    blast->set_skip_exit(skip_exit);
    blast->set_damage(40.0f);
    blast->set_dead(1);
    (void)og::test::on_death(efd_of(FAMILY_EXPLOSION), static_cast<effect*>(blast));
    out.reached = foe->stats()->has_commands();
    const int shove = std::min(2 + owner_level / 15, 8);
    const auto r = forced_walk(foe, shove, 1, 0);
    out.exact = !out.reached || static_cast<bool>(r);
    if (!out.exact)
        out.why = r.message();
    return out;
}

}  // namespace

// effect_bomb.lua:11, the old game's rule (effect.cpp FAMILY_EXPLOSION:
// generic = 4 * level capped at 96, skip_exit -> 16; the finder searches
// 15 + range, inclusive). Level 1 -> 4 (19 px), 2 -> 8 (23 px), 3 -> 12
// (27 px): no floor (#483); level 4 -> 16 (31 px), level 10 -> 40 (55 px),
// level 30 -> the cap 96 (111 px); a skip_exit blast at level 30 or 10 -> 16
// (31 px).
// Breaks: `range = og.min(range, 96)` -> `range = og.clamp(range, 16, 96)`
// (levels 1-3 red); `range = 16` -> `range = 0` with the min (skip_exit red).
TEST(SemLibsBomb, a_blast_reaches_four_px_a_level_capped_at_96_and_a_magical_one_16)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    const struct {
        short level;
        short skip_exit;
        int edge;  // the last reached distance
    } cases[] = {{1, 0, 19}, {2, 0, 23}, {3, 0, 27}, {4, 0, 31},
                 {10, 0, 55}, {30, 0, 111}, {30, 1, 31}, {10, 1, 31}};
    for (const auto& c : cases) {
        SCOPED_TRACE(testing::Message() << "owner level " << c.level << ", skip_exit "
                                        << c.skip_exit);
        const Reach in = blast_reaches(c.level, c.skip_exit, c.edge);
        const Reach out = blast_reaches(c.level, c.skip_exit, c.edge + 1);
        for (const Reach* r : {&in, &out}) {
            ASSERT_NE("rig", r->why);
            ASSERT_NE("placement", r->why);
        }
        EXPECT_TRUE(in.reached) << "a foe at " << c.edge << " px is inside 15 + range";
        EXPECT_TRUE(in.exact) << in.why;
        EXPECT_FALSE(out.reached) << "a foe at " << c.edge + 1 << " px is outside 15 + range";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_bomb.lua:57 arm "nobody within 15 + range": the tile under the
// centre is still scorched (one DamageTile event at the centre), nobody is
// touched, and the hook answers false. The old game: damage_tile, then
// `if (howmany < 1) return 0`.
TEST(SemLibsBomb, a_blast_with_nobody_in_reach_scorches_its_centre_and_answers_false)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* owner = put_living(w, FAMILY_SOLDIER, 500, 500, 0, 10);
    walker* blast = put(w, Order::FX, FAMILY_EXPLOSION, 200, 200, 0);
    ASSERT_TRUE(owner && blast);
    ASSERT_GT(owner->distance_to_ob(blast), 55) << "the owner is out of reach";
    blast->set_owner(owner);
    blast->set_skip_exit(0);
    blast->set_damage(40.0f);
    blast->set_dead(1);
    tw.events.clear();

    EXPECT_FALSE(og::test::on_death(efd_of(FAMILY_EXPLOSION), static_cast<effect*>(blast)))
        << "nobody in reach: not handled";
    int tiles = 0;
    for (const auto& ev : tw.events.events()) {
        if (ev.kind != og::sim::EventKind::DamageTile)
            continue;
        ++tiles;
        EXPECT_EQ(static_cast<std::uint32_t>(200 + blast->sizex() / 2), ev.a) << "centre x";
        EXPECT_EQ(static_cast<std::uint32_t>(200 + blast->sizey() / 2), ev.b) << "centre y";
    }
    EXPECT_EQ(1, tiles) << "the centre tile is scorched once";
    EXPECT_FLOAT_EQ(500.0f, hp(owner));
    EXPECT_FALSE(owner->stats()->has_commands());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_bomb.lua:66: the blast's loop skips walkers on another floor (no
// shove queued, no wound), treasure and effects (never blown about) and the
// dead. A live foe on the blast's floor is the control: shoved 2 + 15 / 15 =
// 3 steps east. The old game filters !dead, treasure and fx the same way
// (effect.cpp FAMILY_EXPLOSION); it has one floor.
TEST(SemLibsBomb, a_blast_leaves_other_floors_treasure_effects_and_the_dead_alone)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    walker* owner = put_living(w, FAMILY_THIEF, 600, 600, 0, 15);
    walker* control = put_living(w, FAMILY_ORC, 210, 200, 1);
    walker* upstairs = put_living(w, FAMILY_ORC, 210, 200, 1);
    walker* corpse = put_living(w, FAMILY_ORC, 210, 205, 1);
    walker* gold = put(w, Order::Treasure, FAMILY_GOLD_BAR, 205, 200, 1);
    walker* cloud = put(w, Order::FX, FAMILY_CLOUD, 205, 205, 1);
    walker* blast = put(w, Order::FX, FAMILY_EXPLOSION, 200, 200, 0);
    ASSERT_TRUE(owner && control && upstairs && corpse && gold && cloud && blast);
    control->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    upstairs->set_floor(1);
    corpse->set_dead(1);
    for (walker* ob : {gold, cloud})
        ob->stats()->clear_command();
    blast->set_owner(owner);
    blast->set_skip_exit(0);
    blast->set_damage(40.0f);
    blast->set_dead(1);

    ASSERT_TRUE(og::test::on_death(efd_of(FAMILY_EXPLOSION), static_cast<effect*>(blast)));
    EXPECT_TRUE(forced_walk(control, 3, 1, 0)) << "the control on the blast's floor is shoved";
    EXPECT_FALSE(upstairs->stats()->has_commands()) << "arm 'other floor': no shove queued";
    EXPECT_FLOAT_EQ(500.0f, hp(upstairs)) << "arm 'other floor': no wound";
    EXPECT_FALSE(gold->stats()->has_commands()) << "arm 'treasure': never blown about";
    EXPECT_EQ(205, gold->xpos());
    EXPECT_EQ(0, gold->dead());
    EXPECT_FALSE(cloud->stats()->has_commands()) << "arm 'fx': never blown about";
    EXPECT_EQ(205, cloud->xpos());
    EXPECT_FALSE(corpse->stats()->has_commands()) << "arm 'already dead': left alone";
    EXPECT_FLOAT_EQ(500.0f, hp(corpse)) << "arm 'already dead': not struck";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_chain.lua — the chain-lightning bolt
// ===========================================================================

namespace {

// A chain bolt of `team` at (x, y), owned by `owner`, homing on `leader`.
walker* bolt_at(GameWorld& w, int x, int y, unsigned char team, walker* owner,
                walker* leader, float damage, short level = 1)
{
    walker* b = put(w, Order::FX, FAMILY_CHAIN, x, y, team);
    if (b == nullptr)
        return nullptr;
    b->set_owner(owner);
    b->set_leader(leader);
    b->set_damage(damage);
    b->set_lineofsight(20);
    b->stats()->set_level(level);
    return b;
}

std::vector<walker*> forks(GameWorld& w)
{
    return live_of(w, Order::FX, FAMILY_CHAIN);
}

}  // namespace

// effect_chain.lua:20 arm "leader nil", isolated: an owner, life left, no
// leader. The old game: `if (!leader || lineofsight < 1 || !owner)` dies.
TEST(SemLibsChain, a_bolt_that_has_lost_its_target_fizzles_without_striking)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* owner = put_living(w, FAMILY_SOLDIER, 300, 300, 0, 3);
    walker* bolt = bolt_at(w, 40, 40, 0, owner, nullptr, 100.0f);
    ASSERT_TRUE(owner && bolt);
    bolt->set_lineofsight(5);

    ASSERT_TRUE(bolt->act());
    EXPECT_EQ(1, bolt->dead()) << "arm 'leader nil': the bolt dies";
    EXPECT_EQ(5, bolt->lineofsight()) << "it did not fly";
    EXPECT_EQ(40, bolt->xpos());
    EXPECT_EQ(40, bolt->ypos());
    EXPECT_EQ(0u, live_of(w, Order::FX, FAMILY_EXPLOSION).size()) << "no strike";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_chain.lua:31 arm "world full": the strike cannot make its
// explosion, so the bolt simply dies: no immunity on the target, no fork.
// The old game's failsafe (effect.cpp FAMILY_CHAIN) does the same. The world
// is made full the way test_kit_ghost does it: a factory that makes no FX.
TEST(SemLibsChain, a_bolt_striking_in_a_full_world_dies_without_a_blast_or_forks)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* owner = put_living(w, FAMILY_SOLDIER, 300, 300, 0, 3);
    walker* other = put_living(w, FAMILY_ORC, 130, 100, 1);
    walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
    walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f);
    ASSERT_TRUE(owner && other && leader && bolt);
    const auto factory = w.entity_factory;
    w.entity_factory = [factory](Order order, std::int32_t family) {
        if (order == Order::FX)
            return std::unique_ptr<walker>();
        return factory(order, family);
    };

    ASSERT_TRUE(bolt->act());
    w.entity_factory = factory;
    EXPECT_EQ(1, bolt->dead()) << "arm 'world full': the bolt dies";
    EXPECT_EQ(0, leader->skip_exit()) << "arm 'world full': no immunity on the target";
    EXPECT_EQ(0u, live_of(w, Order::FX, FAMILY_EXPLOSION).size());
    EXPECT_EQ(0u, forks(w).size()) << "arm 'world full': no fork";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_chain.lua:55: a hero's bolt forks up to 240 + Int / 2 px (Int 60 ->
// 270), a bot's up to 240 + 5 px per bolt level (level 2 -> 250). The old
// game: owner->myguy ? 240 + intelligence / 2 : 240 + level * 5. Owner level
// 1, so one fork; the far foe is listed before the leader (which spends a
// fork whenever it comes first).
TEST(SemLibsChain, a_heros_forks_reach_240_plus_half_int_a_bots_240_plus_5_a_level)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    guy hero(FAMILY_SOLDIER);
    hero.intelligence = 60;
    const struct {
        bool has_guy;
        int distance;
        std::size_t forks;
    } cases[] = {{true, 270, 1}, {true, 271, 0}, {false, 250, 1}, {false, 251, 0},
                 {false, 260, 0}};
    for (const auto& c : cases) {
        SCOPED_TRACE(testing::Message() << (c.has_guy ? "hero" : "bot") << " owner, foe at "
                                        << c.distance);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 1);
        walker* far_foe = put_living(w, FAMILY_ORC, 100 + c.distance, 100, 1);
        walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
        walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f, 2);
        ASSERT_TRUE(owner && far_foe && leader && bolt);
        if (c.has_guy)
            owner->myguy = &hero;
        ASSERT_EQ(c.distance, bolt->distance_to_ob(far_foe));

        ASSERT_TRUE(bolt->act());
        const auto made = forks(w);
        EXPECT_EQ(c.forks, made.size());
        if (made.size() == 1) {
            EXPECT_EQ(far_foe, made[0]->leader());
        }
        owner->myguy = nullptr;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_chain.lua:65: a strike forks only with a foe in reach and children
// stronger than 20 (half the bolt, truncated); only then is the fork budget
// rolled, ONE draw of rand0(owner level), and up to that + 1 children made.
// The old game: `if (temp && generic > 20) numfoes = random(level) + 1`.
TEST(SemLibsChain, a_strike_forks_only_with_a_foe_in_reach_and_children_over_20)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;

    {
        SCOPED_TRACE("forking: owner level 3, the budget roll 2 -> three forks");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 3);
        std::vector<walker*> foes;
        for (const int dx : {30, 40, 50, 60})
            foes.push_back(put_living(w, FAMILY_ORC, 100 + dx, 100, 1));
        walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
        walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f);
        ASSERT_TRUE(owner && leader && bolt);
        w.rng_.state_ = state_where_draws_are({3}, {2});
        og::sim::SimRandom expect(w.rng_.state_);
        (void)expect.next(3);

        ASSERT_TRUE(bolt->act());
        EXPECT_EQ(expect.state_, w.rng_.state_) << "arm 'fork budget draw': exactly one draw";
        const auto made = forks(w);
        ASSERT_EQ(3u, made.size()) << "rand0(3) = 2 -> 2 + 1 forks of four candidates";
        for (walker* f : made) {
            EXPECT_FLOAT_EQ(50.0f, f->damage()) << "half the bolt";
            EXPECT_NE(foes[3], f->leader()) << "the fourth foe is past the budget";
        }
    }
    {
        SCOPED_TRACE("no foe in reach: the struck leader is a friend");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 3);
        walker* leader = put_living(w, FAMILY_ORC, 100, 100, 0);
        walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f);
        ASSERT_TRUE(owner && leader && bolt);
        w.rng_.state_ = 12345u;

        ASSERT_TRUE(bolt->act());
        EXPECT_EQ(1u, live_of(w, Order::FX, FAMILY_EXPLOSION).size()) << "it did strike";
        EXPECT_EQ(12345u, w.rng_.state_) << "arm 'no foe in reach': no draw";
        EXPECT_EQ(0u, forks(w).size()) << "arm 'no foe in reach': no child";
    }
    for (const float damage : {41.0f, 42.0f}) {
        SCOPED_TRACE(testing::Message() << "bolt damage " << damage);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 1);
        walker* foe = put_living(w, FAMILY_ORC, 130, 100, 1);
        walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
        walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, damage);
        ASSERT_TRUE(owner && foe && leader && bolt);
        w.rng_.state_ = 12345u;

        ASSERT_TRUE(bolt->act());
        const auto made = forks(w);
        if (damage < 42.0f) {
            EXPECT_EQ(12345u, w.rng_.state_) << "arm 'child damage <= 20': no draw";
            EXPECT_EQ(0u, made.size()) << "arm 'child damage <= 20': no child";
            continue;
        }
        EXPECT_EQ(1, draws_between(12345u, w.rng_.state_)) << "one budget draw";
        ASSERT_EQ(1u, made.size());
        EXPECT_FLOAT_EQ(21.0f, made[0]->damage());
        EXPECT_EQ(foe, made[0]->leader());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_chain.lua:76: a foe on another floor gets no fork and does not use
// the budget. Owner level 1 (one fork); the other-floor foe is listed first.
TEST(SemLibsChain, chain_lightning_never_forks_to_another_floor_nor_spends_a_fork_there)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 1);
    walker* upstairs = put_living(w, FAMILY_ORC, 130, 100, 1);
    walker* same_floor = put_living(w, FAMILY_ORC, 160, 100, 1);
    walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
    walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f);
    ASSERT_TRUE(owner && upstairs && same_floor && leader && bolt);
    upstairs->set_floor(1);

    ASSERT_TRUE(bolt->act());
    const auto made = forks(w);
    ASSERT_EQ(1u, made.size()) << "arm 'other floor': skipped without spending the fork";
    EXPECT_EQ(same_floor, made[0]->leader());
    EXPECT_EQ(0, made[0]->floor());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_chain.lua:77: a fork never goes to the struck foe nor to one immune
// (skip_exit >= 1), yet each such foe still spends a fork; a child carries
// the bolt's level and floor. The old game: the loop's `e++, numfoes--`
// counts every foe, `if (w != leader && w->skip_exit < 1)` decides the bolt.
TEST(SemLibsChain, a_fork_skips_the_struck_and_the_immune_but_they_spend_the_budget)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;

    {
        SCOPED_TRACE("budget 3: [immune, fresh, leader] -> one fork, to the fresh foe");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.set_floor_count(2);
        walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 3);
        walker* immune = put_living(w, FAMILY_ORC, 120, 100, 1);
        walker* fresh = put_living(w, FAMILY_ORC, 130, 100, 1);
        walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
        walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f, 4);
        ASSERT_TRUE(owner && immune && fresh && leader && bolt);
        for (walker* ob : {owner, immune, fresh, leader, bolt})
            ob->set_floor(1);
        immune->set_skip_exit(2);
        w.rng_.state_ = state_where_draws_are({3}, {2});
        const short bx = bolt->xpos();
        const short by = bolt->ypos();

        ASSERT_TRUE(bolt->act());
        const auto made = forks(w);
        ASSERT_EQ(1u, made.size()) << "arm 'non-leader foe with skip_exit >= 1': no child";
        walker* f = made[0];
        EXPECT_EQ(fresh, f->leader());
        EXPECT_EQ(4, f->stats()->level()) << "arm '(level, floor)': the bolt's level";
        EXPECT_EQ(1, f->floor()) << "arm '(level, floor)': the bolt's floor";
        EXPECT_EQ(owner, f->owner());
        EXPECT_EQ(0, f->team_num());
        EXPECT_NE(0, f->stats()->query_bit_flags(BIT_MAGICAL)) << "a fork is magical";
        EXPECT_FLOAT_EQ(50.0f, f->damage());
        EXPECT_EQ(bx, f->xpos()) << "centred on the bolt (same box)";
        EXPECT_EQ(by, f->ypos());
    }
    {
        SCOPED_TRACE("budget 1: [immune, fresh, leader] -> the immune foe spent it");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 1);
        walker* immune = put_living(w, FAMILY_ORC, 120, 100, 1);
        walker* fresh = put_living(w, FAMILY_ORC, 130, 100, 1);
        walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
        walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f);
        ASSERT_TRUE(owner && immune && fresh && leader && bolt);
        immune->set_skip_exit(2);

        ASSERT_TRUE(bolt->act());
        EXPECT_EQ(0u, forks(w).size()) << "arm 'skipped foe': the immune foe spent the fork";
    }
    {
        SCOPED_TRACE("budget 1: [leader, fresh] -> the struck leader spent it");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 1);
        walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
        walker* fresh = put_living(w, FAMILY_ORC, 130, 100, 1);
        walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f);
        ASSERT_TRUE(owner && leader && fresh && bolt);

        ASSERT_TRUE(bolt->act());
        EXPECT_EQ(0u, forks(w).size()) << "arm 'skipped foe': the leader spent the fork";
    }
    {
        SCOPED_TRACE("budget 3: [immune, leader] -> nothing to fork to");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* owner = put_living(w, FAMILY_SOLDIER, 50, 600, 0, 3);
        walker* immune = put_living(w, FAMILY_ORC, 120, 100, 1);
        walker* leader = put_living(w, FAMILY_ORC, 100, 100, 1);
        walker* bolt = bolt_at(w, 100, 100, 0, owner, leader, 100.0f);
        ASSERT_TRUE(owner && immune && leader && bolt);
        immune->set_skip_exit(1);
        w.rng_.state_ = state_where_draws_are({3}, {2});

        ASSERT_TRUE(bolt->act());
        EXPECT_EQ(0u, forks(w).size())
            << "arm 'non-leader foe with skip_exit >= 1': immune at 1 already";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_chain.lua:106 arm "any": a homing step faces the way it flies and
// shows that facing's first frame (the old game: curdir = facing(xd, yd);
// set_frame(ani[curdir][0])). Also the small-gap left/up step closes the gap
// exactly (6 px left of a 10 px step).
TEST(SemLibsChain, a_homing_bolt_faces_the_way_it_flies)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    const struct {
        int lx, ly;
        int want_x, want_y;
        int facing;
    } cases[] = {
        {300, 100, 110, 100, FACE_RIGHT},
        {20, 20, 90, 90, FACE_UP_LEFT},
        {94, 50, 94, 90, FACE_UP_LEFT},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(testing::Message() << "leader at (" << c.lx << ", " << c.ly << ")");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* owner = put_living(w, FAMILY_MAGE, 50, 600, 1, 1);
        walker* leader = put_living(w, FAMILY_ORC, c.lx, c.ly, 2);
        walker* bolt = bolt_at(w, 100, 100, 1, owner, leader, 100.0f);
        ASSERT_TRUE(owner && leader && bolt);
        bolt->set_stepsize(10.0f);
        bolt->set_curdir(static_cast<signed char>(FACE_DOWN));
        ASSERT_NE(nullptr, bolt->ani) << "the bolt has its animation table";

        ASSERT_TRUE(bolt->act());
        EXPECT_EQ(0, bolt->dead());
        EXPECT_EQ(c.want_x, bolt->xpos());
        EXPECT_EQ(c.want_y, bolt->ypos());
        EXPECT_EQ(c.facing, static_cast<int>(bolt->curdir())) << "arm 'any': faces its flight";
        EXPECT_EQ(static_cast<int>(bolt->ani[c.facing][0]), static_cast<int>(bolt->frame()))
            << "arm 'any': that facing's first frame";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_cloud.lua — the poison cloud
// ===========================================================================

namespace {

// A cloud of team 0 at (x, y), owned by a team-0 druid parked far away.
walker* cloud_at(GameWorld& w, int x, int y, int lifetime)
{
    walker* owner = put_living(w, FAMILY_DRUID, 600, 900, 0);
    walker* c = put(w, Order::FX, FAMILY_CLOUD, x, y, 0);
    if (owner == nullptr || c == nullptr)
        return nullptr;
    c->set_owner(owner);
    c->set_lifetime(static_cast<short>(lifetime));
    c->set_damage(9.0f);
    c->stats()->clear_command();
    return c;
}

}  // namespace

// effect_cloud.lua:17 arm "lifetime <= 0 (no action)": the expiring act
// neither hurts nor drifts. The old game: `else { dead = 1; death(); return 1; }`.
TEST(SemLibsCloud, an_expired_cloud_vanishes_without_acting_that_tick)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* cloud = cloud_at(w, 160, 160, 0);
    walker* orc = put_living(w, FAMILY_ORC, 160, 160, 1);
    ASSERT_TRUE(cloud && orc);
    const std::uint32_t state = w.rng_.state_;

    ASSERT_TRUE(cloud->act());
    EXPECT_EQ(1, cloud->dead());
    EXPECT_EQ(0, cloud->lifetime());
    EXPECT_FLOAT_EQ(500.0f, hp(orc)) << "arm 'no action': no damage";
    EXPECT_FALSE(cloud->stats()->has_commands()) << "arm 'no action': no drift walk";
    EXPECT_EQ(state, w.rng_.state_) << "arm 'no action': no drift roll";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_cloud.lua:24: in its last 8 ticks a cloud gains 3 invisibility a
// tick and every tick spends 1 (net +2 a tick); before that a cloud's
// invisibility drains by 1 and stays at 0. The old game: `if (lifetime < 8)
// invisibility_left += 3; if (invisibility_left > 0) invisibility_left--;`.
TEST(SemLibsCloud, a_cloud_fades_out_over_its_last_eight_ticks)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        walker* cloud = cloud_at(tw.world(), 160, 160, 10);
        ASSERT_NE(nullptr, cloud);
        cloud->set_invisibility_left(0);
        const int want_life[] = {9, 8, 7, 6};
        const int want_invis[] = {0, 0, 2, 4};
        for (int i = 0; i < 4; ++i) {
            ASSERT_TRUE(cloud->act());
            EXPECT_EQ(want_life[i], cloud->lifetime()) << "act " << i + 1;
            EXPECT_EQ(want_invis[i], cloud->invisibility_left())
                << "act " << i + 1 << ": lifetime " << want_life[i]
                << (want_life[i] < 8 ? " < 8: net +2" : " >= 8 at 0: stays 0");
        }
    }
    {
        TestGameWorld tw;
        walker* cloud = cloud_at(tw.world(), 160, 160, 50);
        ASSERT_NE(nullptr, cloud);
        cloud->set_invisibility_left(5);
        ASSERT_TRUE(cloud->act());
        EXPECT_EQ(4, cloud->invisibility_left()) << "arm 'lifetime >= 8 and invisibility > 0': -1";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_cloud.lua:53 arm "moved on either axis or turned (RNG)": a cloud
// whose queued step moved it (on x alone, on y alone) or only turned it keeps
// its walk and draws nothing. The old game draws nothing on these ticks
// either (`if (stats->has_commands()) temp = stats->do_command();`). The
// other arm (a wedged cloud re-rolls its walk, #233) is NOT pinned: the old
// game has no re-roll, see the batch report.
TEST(SemLibsCloud, a_cloud_that_moved_or_turned_keeps_its_walk_and_draws_nothing)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    const struct {
        const char* what;
        int dx, dy;
        int start_facing;
        int want_dx, want_dy;  // pixels moved
    } cases[] = {
        {"x alone", 1, 0, FACE_RIGHT, 4, 0},
        {"y alone", 0, 1, FACE_DOWN, 0, 4},
        {"a pure turn", 1, 0, FACE_LEFT, 0, 0},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.what);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        walker* cloud = cloud_at(w, 200, 200, 30);
        ASSERT_NE(nullptr, cloud);
        ASSERT_FLOAT_EQ(4.0f, cloud->stepsize());
        cloud->set_curdir(static_cast<signed char>(c.start_facing));
        cloud->stats()->add_command(COMMAND_WALK, 10, static_cast<short>(c.dx),
                                    static_cast<short>(c.dy));
        w.rng_.state_ = 777u;

        ASSERT_TRUE(cloud->act());
        EXPECT_EQ(200 + c.want_dx, cloud->xpos());
        EXPECT_EQ(200 + c.want_dy, cloud->ypos());
        if (c.want_dx == 0 && c.want_dy == 0) {
            EXPECT_NE(c.start_facing, static_cast<int>(cloud->curdir())) << "it turned";
        }
        EXPECT_EQ(777u, w.rng_.state_) << "no draw";
        ASSERT_EQ(1u, cloud->stats()->commands.size()) << "the walk is kept";
        const command& q = cloud->stats()->commands.front();
        EXPECT_EQ(COMMAND_WALK, q.commandtype);
        EXPECT_EQ(c.dx, q.com1);
        EXPECT_EQ(c.dy, q.com2);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_cloud.lua:58 arm "both rolls 0 => redraw both": the scripted rolls
// 1, 1 (a zero step) are thrown away and 2, 0 kept: a WALK (+1, -1) of 4,
// five draws in all. The old game: `while (xd == 0 && yd == 0) { xd =
// random(3) - 1; yd = random(3) - 1; }` then random(20).
TEST(SemLibsCloud, a_cloud_never_drifts_by_the_zero_step)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* cloud = cloud_at(w, 200, 200, 30);
    ASSERT_NE(nullptr, cloud);
    CountingSequence seq({1, 1, 2, 0, 4});
    {
        ScopedSimRandom scoped(&seq);
        ASSERT_TRUE(cloud->act());
    }
    EXPECT_EQ(5, seq.calls) << "both rolls drawn twice, then the count";
    ASSERT_EQ(1u, cloud->stats()->commands.size());
    const command& q = cloud->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, q.commandtype);
    EXPECT_EQ(1, q.com1) << "the redrawn x roll";
    EXPECT_EQ(-1, q.com2) << "the redrawn y roll";
    EXPECT_EQ(4, q.commandcount);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_common.lua — M.hits, inclusive box edges
// ===========================================================================

// effect_common.lua:13 arm "edge-touch counts": a foe whose box only shares
// an edge with the cloud's, on each of the four sides, is struck exactly as
// hard as one the cloud covers; one pixel further out it is not. The old
// game's hits() compares with > and < the same way (effect.cpp).
TEST(SemLibsEffectCommon, a_box_that_only_shares_an_edge_still_touches)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;

    // The loss of a foe the cloud covers, as the reference.
    float covered = 0.0f;
    int csx = 0, csy = 0, osx = 0, osy = 0;
    {
        TestGameWorld tw;
        walker* cloud = cloud_at(tw.world(), 160, 160, 30);
        walker* orc = put_living(tw.world(), FAMILY_ORC, 160, 160, 1);
        ASSERT_TRUE(cloud && orc);
        csx = cloud->sizex();
        csy = cloud->sizey();
        osx = orc->sizex();
        osy = orc->sizey();
        ASSERT_TRUE(cloud->act());
        covered = 500.0f - hp(orc);
        ASSERT_GT(covered, 0.0f) << "premise: the cloud hurts the foe it covers";
    }
    // One pixel past the right or bottom edge is past the finder's radius
    // too (the cloud's own width, corner to corner), so only the left and
    // top sides have a placement that is clear of the box yet still found.
    const struct {
        const char* side;
        int x, y;    // the touching placement
        int ox, oy;  // one pixel further out
        bool clear_case;
    } cases[] = {
        {"left", 160 - osx, 160, 160 - osx - 1, 160, true},
        {"right", 160 + csx, 160, 0, 0, false},
        {"above", 160, 160 - osy, 160, 160 - osy - 1, true},
        {"below", 160, 160 + csy, 0, 0, false},
    };
    for (const auto& c : cases) {
        for (const bool touching : {true, false}) {
            if (!touching && !c.clear_case)
                continue;
            SCOPED_TRACE(testing::Message() << c.side << (touching ? ", touching" : ", one px out"));
            TestGameWorld tw;
            walker* cloud = cloud_at(tw.world(), 160, 160, 30);
            walker* orc = put_living(tw.world(), FAMILY_ORC, touching ? c.x : c.ox,
                                     touching ? c.y : c.oy, 1);
            ASSERT_TRUE(cloud && orc);
            ASSERT_LE(cloud->distance_to_ob(orc), cloud->sizex())
                << "premise: inside the finder's radius, so hits() decides";
            ASSERT_TRUE(cloud->act());
            if (touching)
                EXPECT_FLOAT_EQ(covered, 500.0f - hp(orc)) << "an edge touch is a touch";
            else
                EXPECT_FLOAT_EQ(500.0f, hp(orc)) << "one pixel clear is clear";
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_door_open.lua — the opening door
// ===========================================================================

// effect_door_open.lua:20 arm "still animating": the hook declines (the
// default animate path runs), the door stays and makes no copy; once the
// animation is over the copy appears. The old game: `if (ani_type !=
// ANI_WALK) return animate();`.
TEST(SemLibsDoorOpen, an_opening_door_plays_its_animation_before_it_settles)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* door = w.add_fx_ob(Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_NE(nullptr, door);
    door->setworldxy(200.0f, 120.0f);
    door->set_ani_type(static_cast<char>(ANI_DOOR_OPEN));

    EXPECT_FALSE(og::test::on_act(efd_of(FAMILY_DOOR_OPEN), static_cast<effect*>(door)))
        << "arm 'still animating': the default animate path";
    EXPECT_EQ(0, door->dead());
    EXPECT_EQ(1u, live_of(w, Order::FX, FAMILY_DOOR_OPEN).size()) << "no copy yet";

    door->set_ani_type(static_cast<char>(ANI_WALK));
    EXPECT_TRUE(og::test::on_act(efd_of(FAMILY_DOOR_OPEN), static_cast<effect*>(door)));
    EXPECT_EQ(1, door->dead());
    EXPECT_EQ(1u, live_of(w, Order::FX, FAMILY_DOOR_OPEN).size()) << "the settled copy";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_door_open.lua:27 arm "copy spawned (floor)": the settled copy stays
// on the door's floor (a door upstairs stays upstairs), with the spot,
// facing, ignore and walk animation of the original, which retires.
TEST(SemLibsDoorOpen, a_settled_door_stays_on_the_floor_it_opened_on)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    GameWorld& w = tw.world();
    w.set_floor_count(2);
    walker* door = w.add_fx_ob(Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_NE(nullptr, door);
    door->set_floor(1);
    door->setworldxy(200.0f, 120.0f);
    door->set_curdir(static_cast<signed char>(FACE_RIGHT));
    door->set_ani_type(static_cast<char>(ANI_WALK));
    door->set_ignore(0);

    ASSERT_TRUE(og::test::on_act(efd_of(FAMILY_DOOR_OPEN), static_cast<effect*>(door)));
    EXPECT_EQ(1, door->dead());
    const auto copies = live_of(w, Order::FX, FAMILY_DOOR_OPEN);
    ASSERT_EQ(1u, copies.size());
    walker* open = copies[0];
    EXPECT_EQ(1, open->floor()) << "arm 'copy spawned (floor)'";
    EXPECT_FLOAT_EQ(200.0f, open->worldx());
    EXPECT_FLOAT_EQ(120.0f, open->worldy());
    EXPECT_EQ(FACE_RIGHT, static_cast<int>(open->curdir()));
    EXPECT_EQ(1, static_cast<int>(open->ignore()));
    EXPECT_EQ(ANI_WALK, static_cast<int>(open->ani_type()));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_ember.lua — IMMOLATE's embers (New Specials)
// ===========================================================================

namespace {

int ember_family()
{
    int id = fx_family("core:ember");
    if (id < 0) {
        (void)og::resources::refresh_pack_scripts();
        id = fx_family("core:ember");
    }
    return id;
}

// An ember of team 0, damage 6, at (200, 200) with `lifetime` left.
walker* ember_at(GameWorld& w, int lifetime)
{
    walker* e = put(w, Order::FX, ember_family(), 200, 200, 0);
    if (e == nullptr)
        return nullptr;
    e->set_lifetime(static_cast<short>(lifetime));
    e->set_damage(6.0f);
    e->set_frame(0);
    return e;
}

// A body of `family`/`order` of team 1, centred on the ember, 100 hp.
walker* centred_on(GameWorld& w, Order order, int family, const walker* ember)
{
    walker* ob = put(w, order, family, 0, 0, 1);
    if (ob == nullptr)
        return nullptr;
    ob->setxy(static_cast<short>(ember->xpos() + ember->sizex() / 2 - ob->sizex() / 2),
              static_cast<short>(ember->ypos() + ember->sizey() / 2 - ob->sizey() / 2));
    ob->stats()->set_armor(0.0f);
    ob->stats()->set_max_hitpoints(100.0f);
    ob->stats()->set_hitpoints(100.0f);
    ob->stats()->set_magicpoints(0.0f);
    return ob;
}

}  // namespace

// effect_ember.lua:23: the burn touches living foes on its own floor only: a
// foe's generator and a foe upstairs, both squarely on the ember, are not
// burned while a foe on the ember's floor is (36 left: a pulse tick). The
// ember does not read the setting; both values of New Specials agree.
TEST(SemLibsEmber, an_ember_burns_only_living_foes_on_its_own_floor)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    ASSERT_GE(ember_family(), 0);
    for (const short ns : {short{0}, short{1}}) {
        SCOPED_TRACE(testing::Message() << "new_specials " << ns);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.new_specials = ns;
        w.set_floor_count(2);
        walker* ember = ember_at(w, 37);
        ASSERT_NE(nullptr, ember);
        walker* crosser = centred_on(w, Order::Living, FAMILY_SOLDIER, ember);
        walker* upstairs = centred_on(w, Order::Living, FAMILY_SOLDIER, ember);
        walker* tent = centred_on(w, Order::Generator, FAMILY_TENT, ember);
        ASSERT_TRUE(crosser && upstairs && tent);
        upstairs->set_floor(1);

        ASSERT_TRUE(ember->act());
        ASSERT_EQ(36, ember->lifetime());
        ASSERT_LT(hp(crosser), 100.0f) << "premise: a pulse tick burns the foe on the ember";
        EXPECT_FLOAT_EQ(100.0f, hp(tent)) << "arm 'generator foe': not burned";
        EXPECT_FLOAT_EQ(100.0f, hp(upstairs)) << "arm 'other floor': not burned";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_ember.lua:41 arm "lifetime reaches 0 (no burn)": the act that takes
// the count to 0 (a multiple of the pulse) ends the ember and burns nothing.
TEST(SemLibsEmber, an_ember_goes_out_on_its_last_act_without_burning)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    ASSERT_GE(ember_family(), 0);
    for (const short ns : {short{0}, short{1}}) {
        SCOPED_TRACE(testing::Message() << "new_specials " << ns);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.new_specials = ns;
        walker* ember = ember_at(w, 1);
        ASSERT_NE(nullptr, ember);
        walker* foe = centred_on(w, Order::Living, FAMILY_SOLDIER, ember);
        ASSERT_NE(nullptr, foe);

        ASSERT_TRUE(ember->act());
        EXPECT_EQ(1, ember->dead()) << "out at 0";
        EXPECT_FLOAT_EQ(100.0f, hp(foe)) << "arm 'no burn': the last act burns nothing";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_ember.lua:46: the dying frame shows only below 10 left: at 11 and
// 10 the frame stays 0, at 9 it turns 1.
TEST(SemLibsEmber, an_ember_shows_its_dying_frame_only_in_its_last_nine_ticks)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    ASSERT_GE(ember_family(), 0);
    for (const short ns : {short{0}, short{1}}) {
        SCOPED_TRACE(testing::Message() << "new_specials " << ns);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        w.new_specials = ns;
        walker* ember = ember_at(w, 12);
        ASSERT_NE(nullptr, ember);
        const int want[] = {0, 0, 1};
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(ember->act());
            EXPECT_EQ(11 - i, ember->lifetime());
            EXPECT_EQ(want[i], static_cast<int>(ember->frame()))
                << "at " << 11 - i << " left" << (want[i] ? ": dying" : ": arm 'otherwise'");
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_ghost_scare.lua — the ghost's scare cloud
// ===========================================================================

namespace {

// A level-5 ghost of team 0 at (100, 100) (scare radius 50 + 10 * 5 = 100)
// and its scare cloud.
struct ScareRig {
    walker* ghost = nullptr;
    walker* scare = nullptr;
};

ScareRig scare_rig(GameWorld& w)
{
    ScareRig r;
    r.ghost = put_living(w, FAMILY_GHOST, 100, 100, 0, 5);
    r.scare = put(w, Order::FX, FAMILY_GHOST_SCARE, 100, 100, 0);
    if (r.ghost != nullptr && r.scare != nullptr)
        r.scare->set_owner(r.ghost);
    return r;
}

}  // namespace

// effect_ghost_scare.lua:16 arm "no owner": a scare without its ghost (none,
// or a dead one: effect::act drops a dead owner first) stays where it is,
// then takes the default path (here: animation over, so it dies). The old
// game: `if (owner) center_on(owner);`, then the default.
TEST(SemLibsGhostScare, a_scare_without_its_ghost_stays_where_it_is)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    for (const bool dead_ghost : {false, true}) {
        SCOPED_TRACE(dead_ghost ? "ghost dead" : "no ghost");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        ScareRig r = scare_rig(w);
        ASSERT_TRUE(r.ghost && r.scare);
        r.ghost->setxy(160, 130);
        if (dead_ghost)
            r.ghost->set_dead(1);
        else
            r.scare->set_owner(nullptr);
        r.scare->setxy(100, 100);
        r.scare->set_ani_type(static_cast<char>(ANI_WALK));

        (void)r.scare->act();
        EXPECT_EQ(100, r.scare->xpos()) << "arm 'no owner': not re-centred";
        EXPECT_EQ(100, r.scare->ypos()) << "arm 'no owner': not re-centred";
        EXPECT_EQ(1, r.scare->dead()) << "the default path: animation over, it dies";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_ghost_scare.lua:28 arm "owner dead/missing": the expiring scare
// frightens nobody and answers false; with the ghost alive the foe at 40 px
// is put to flight (25 x 5 = 125 steps). The old game: `if (!owner ||
// owner->dead) return 0;`.
TEST(SemLibsGhostScare, a_scare_whose_ghost_is_gone_frightens_nobody)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    for (const int ghost_state : {0, 1, 2}) {  // alive, dead, missing
        SCOPED_TRACE(testing::Message() << "ghost state " << ghost_state);
        TestGameWorld tw;
        GameWorld& w = tw.world();
        ScareRig r = scare_rig(w);
        walker* foe = put_living(w, FAMILY_SOLDIER, 140, 100, 1);
        ASSERT_TRUE(r.ghost && r.scare && foe);
        if (ghost_state == 1)
            r.ghost->set_dead(1);
        if (ghost_state == 2)
            r.scare->set_owner(nullptr);
        r.scare->set_dead(1);

        const bool handled =
            og::test::on_death(efd_of(FAMILY_GHOST_SCARE), static_cast<effect*>(r.scare));
        if (ghost_state == 0) {
            EXPECT_TRUE(handled);
            EXPECT_TRUE(forced_walk(foe, 125, 1, 0)) << "the control: a live ghost's scare";
            continue;
        }
        EXPECT_FALSE(handled) << "arm 'owner dead/missing'";
        EXPECT_FALSE(foe->stats()->has_commands()) << "arm 'owner dead/missing': nobody flees";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_ghost_scare.lua:37 arm "no foe in radius": a level-5 ghost's scare
// reaches 100 px (Manhattan, from the ghost); a foe at 101 is not frightened,
// nothing is drawn and the hook answers false. At 100 the foe flees.
TEST(SemLibsGhostScare, a_scare_with_no_foe_in_its_radius_does_nothing_and_draws_nothing)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    for (const int d : {100, 101}) {
        SCOPED_TRACE(testing::Message() << "foe at " << d << " px");
        TestGameWorld tw;
        GameWorld& w = tw.world();
        ScareRig r = scare_rig(w);
        walker* foe = put_living(w, FAMILY_SOLDIER, 100 + d, 100, 1);
        ASSERT_TRUE(r.ghost && r.scare && foe);
        ASSERT_EQ(d, r.ghost->distance_to_ob(foe));
        r.scare->set_dead(1);
        w.rng_.state_ = 4242u;

        const bool handled =
            og::test::on_death(efd_of(FAMILY_GHOST_SCARE), static_cast<effect*>(r.scare));
        EXPECT_EQ(4242u, w.rng_.state_) << "no draw (the foe has no guy)";
        if (d == 100) {
            EXPECT_TRUE(handled);
            EXPECT_TRUE(forced_walk(foe, 125, 1, 0)) << "the edge of the radius frightens";
            continue;
        }
        EXPECT_FALSE(handled) << "arm 'no foe in radius'";
        EXPECT_FALSE(foe->stats()->has_commands()) << "arm 'no foe in radius': nobody flees";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
