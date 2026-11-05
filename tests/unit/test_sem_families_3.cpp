/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch families-3: the rules the review found in living-08-slime.lua, living-11-thief.lua, living-12-ghost.lua, living-13-druid.lua, living-14-orc.lua, living-16-barbarian.lua, living-17-archmage.lua.
// Each test is named after the rule it asserts.
//
// Every scene is an open grass field (the GameplayFixesBots fixture,
// bot_gate_fixture.h) with bodies placed by hand in pixels. A cast goes
// through walker::special, so the slot's price is charged exactly as in
// play; a bot gate is asked through og::test::check_special_ai; the slime's
// split through its on_ani_complete hook. Where the rule is a classic one
// (the setting off, or no setting at all) the old game's function was read
// at 7ff1654a and does the same.

#include <gtest/gtest.h>

#include "bot_gate_fixture.h"
#include "../test_family_hook_dispatch.h"
#include "../test_family_lookup.h"
#include "../test_sim_random_scope.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

// Every draw answers the same value (taken modulo the bound).
class ConstRandom : public IRandom
{
public:
    explicit ConstRandom(std::uint32_t v) : v_(v) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        return max_exclusive == 0 ? 0 : v_ % max_exclusive;
    }

private:
    std::uint32_t v_;
};

class SemFamilies3 : public og::test::bot_gates::GameplayFixesBots
{
protected:
    og::sim::SimEventLog& events() { return tw_->events; }

    int sounds(int id)
    {
        int n = 0;
        for (const og::sim::Event& e : events().events())
            if (e.kind == og::sim::EventKind::PlaySound && e.a == static_cast<std::uint32_t>(id))
                ++n;
        return n;
    }

    int notices(const char* needle)
    {
        int n = 0;
        for (const og::sim::Event& e : events().events())
            if (e.kind == og::sim::EventKind::Notification &&
                e.text.find(needle) != std::string::npos)
                ++n;
        return n;
    }

    int all_notices()
    {
        int n = 0;
        for (const og::sim::Event& e : events().events())
            if (e.kind == og::sim::EventKind::Notification)
                ++n;
        return n;
    }

    // The cast as play makes it: walker::special, the slot's price charged
    // on success.
    bool cast(walker* w, int slot, short fixes, std::string* reason, bool shifted = false)
    {
        world().gameplay_fixes = fixes;
        w->set_current_special(static_cast<char>(slot));
        w->set_shifter_down(shifted ? 1 : 0);
        walker::SpecialFailure why = walker::SpecialFailure::None;
        const bool ok = w->special(&why, reason);
        w->set_shifter_down(0);
        return ok;
    }

    // The live entities of `order`/`family` in all three lists.
    std::vector<walker*> live(Order order, int family)
    {
        std::vector<walker*> out;
        for (const GameWorld::EntityList* list : {&world().oblist, &world().fxlist, &world().weaplist})
            for (const auto& ob : *list)
                if (ob && ob->dead() == 0 && ob->query_order() == order &&
                    ob->family() == static_cast<char>(family))
                    out.push_back(ob.get());
        return out;
    }

    int count(Order order, int family) { return static_cast<int>(live(order, family).size()); }

    // Every entity of `order`/`family`, dead or alive, still in a list.
    std::vector<walker*> any(Order order, int family)
    {
        std::vector<walker*> out;
        for (const GameWorld::EntityList* list : {&world().oblist, &world().fxlist, &world().weaplist})
            for (const auto& ob : *list)
                if (ob && ob->query_order() == order && ob->family() == static_cast<char>(family))
                    out.push_back(ob.get());
        return out;
    }

    static void make_hero(walker* w, int family)
    {
        w->set_owned_myguy(std::make_unique<guy>(static_cast<char>(family)));
    }

    static void face(walker* w, float lx, float ly, int dir)
    {
        w->set_lastx(lx);
        w->set_lasty(ly);
        w->set_curdir(static_cast<signed char>(dir));
    }
};

class SemSlime : public SemFamilies3 {};
class SemThief : public SemFamilies3 {};
class SemGhost : public SemFamilies3 {};
class SemDruid : public SemFamilies3 {};
class SemOrc : public SemFamilies3 {};
class SemBarbarian : public SemFamilies3 {};

}  // namespace

// ===========================================================================
// The slime (packs/core/families/living-08-slime.lua)
// ===========================================================================

// living-08-slime.lua:106. A monster slime (no character) that splits takes
// neither character arm: its offspring is a plain copy, the level copied by
// transfer_stats and never recalculated, nobody gains or loses a character.
// The old game's walker::animate split (walker.cpp) does the same.
TEST_F(SemSlime, a_monster_slime_split_copies_its_level_and_makes_no_character)
{
    living* slime = add(FAMILY_SLIME, 3, 160, 160, 4, 20.0f);
    ASSERT_NE(nullptr, slime);
    slime->stats()->set_hitpoints(77.0f);
    slime->set_ani_type(static_cast<char>(ANI_SLIME_SPLIT));
    const std::optional<bool> handled =
        og::script::hooks::on_ani_complete(get_family_descriptor(FAMILY_SLIME), slime);
    ASSERT_TRUE(handled.has_value());
    EXPECT_TRUE(*handled);

    walker* sibling = nullptr;
    for (walker* ob : live(Order::Living, FAMILY_SMALL_SLIME))
        if (ob != slime)
            sibling = ob;
    ASSERT_NE(nullptr, sibling) << "the split makes a second small slime";
    EXPECT_EQ(nullptr, slime->myguy) << "monster arm: the parent has no character";
    EXPECT_EQ(nullptr, sibling->myguy) << "monster arm: nor does the offspring";
    EXPECT_EQ(4, static_cast<int>(sibling->stats()->level()))
        << "monster arm: the copied level stands (no calculate_level)";
    EXPECT_FLOAT_EQ(77.0f, sibling->stats()->hitpoints()) << "monster arm: the stats are copied";
    EXPECT_EQ(3, static_cast<int>(sibling->team_num())) << "monster arm: the offspring fights for the slime's side";
}

// living-08-slime.lua:106. The threshold is 1000 x level exactly: a level-4
// hero with 4000 experience takes the high arm (both halves keep a
// character with 2000 each, levelled to calculate_level(2000) = 1); with
// 3999 the offspring is stripped and levelled to calculate_level(1999) = 1.
TEST_F(SemSlime, a_slime_split_at_exactly_one_thousand_per_level_keeps_both_characters)
{
    for (const std::uint32_t exp : {4000u, 3999u})
    {
        fresh_world();
        living* slime = add(FAMILY_SLIME, 0, 160, 160, 4, 20.0f);
        ASSERT_NE(nullptr, slime);
        slime->set_ani_type(static_cast<char>(ANI_SLIME_SPLIT));
        auto character = std::make_unique<guy>(static_cast<char>(FAMILY_SLIME));
        character->exp = exp;
        character->teamnum = 0;
        slime->set_owned_myguy(std::move(character));
        const std::optional<bool> handled =
            og::script::hooks::on_ani_complete(get_family_descriptor(FAMILY_SLIME), slime);
        ASSERT_TRUE(handled.has_value());
        walker* sibling = nullptr;
        for (walker* ob : live(Order::Living, FAMILY_SMALL_SLIME))
            if (ob != slime)
                sibling = ob;
        ASSERT_NE(nullptr, sibling);
        ASSERT_NE(nullptr, slime->myguy);
        if (exp == 4000u)
        {
            ASSERT_NE(nullptr, sibling->myguy) << "exp == 1000 x level: the high arm";
            EXPECT_EQ(2000u, slime->myguy->exp);
            EXPECT_EQ(2000u, sibling->myguy->exp);
            EXPECT_EQ(1, static_cast<int>(slime->myguy->level));
            EXPECT_EQ(1, static_cast<int>(sibling->myguy->level));
        }
        else
        {
            EXPECT_EQ(nullptr, sibling->myguy) << "exp one short: the offspring is stripped";
            EXPECT_EQ(std::string("SLIME"), std::string(sibling->stats()->name));
            EXPECT_EQ(1, static_cast<int>(sibling->stats()->level()))
                << "the stripped offspring is levelled calculate_level(3999 / 2) = 1";
            EXPECT_EQ(3999u, slime->myguy->exp) << "the parent keeps its character untouched";
        }
    }
}

// ===========================================================================
// The thief (packs/core/families/living-11-thief.lua)
// ===========================================================================

// living-11-thief.lua:118. A hero's bomb counts as one shot in its total and
// scenario tallies; a monster thief keeps none (and the cast still lays its
// one bomb). A player-driven thief, so no flee draw is made.
TEST_F(SemThief, a_heros_bomb_counts_one_shot_and_a_monster_keeps_no_tally)
{
    for (const bool hero : {true, false})
    {
        fresh_world();
        world().new_specials = 0;
        living* thief = add(FAMILY_THIEF, 0, 160, 160, 1, 100.0f);
        ASSERT_NE(nullptr, thief);
        thief->set_user(0);
        thief->set_act_type(ACT_CONTROL);
        if (hero)
        {
            make_hero(thief, FAMILY_THIEF);
            thief->myguy->total_shots = 7;
            thief->myguy->scen_shots = 3;
        }
        std::string reason;
        EXPECT_TRUE(cast(thief, 1, 0, &reason)) << (hero ? "hero" : "monster") << ": " << reason;
        EXPECT_EQ(1, count(Order::FX, FAMILY_BOMB)) << (hero ? "hero" : "monster") << " arm: one bomb";
        EXPECT_FLOAT_EQ(65.0f, thief->stats()->magicpoints()) << "DROP BOMB's 35 MP";
        if (hero)
        {
            ASSERT_NE(nullptr, thief->myguy);
            EXPECT_EQ(8, thief->myguy->total_shots) << "hero arm: total shots +1";
            EXPECT_EQ(3 + 1, static_cast<int>(thief->myguy->scen_shots)) << "hero arm: scenario shots +1";
        }
        else
        {
            EXPECT_EQ(nullptr, thief->myguy) << "monster arm: no character to count on";
        }
    }
}

// living-11-thief.lua:130. An AI thief flees its own armed bomb: a forced
// WALK of 20 steps along (rand(3) - 1, rand(3) - 1), x drawn first. A
// player-driven thief is not moved and draws nothing. The old game's
// walker::special does the same (its "player" is the thief a view
// controls, which is the seat's user here).
TEST_F(SemThief, an_ai_thief_flees_its_own_bomb_and_a_player_thief_does_not)
{
    // AI arm: the draws come up 0 then 2: (-1, +1).
    {
        world().new_specials = 0;
        living* thief = add(FAMILY_THIEF, 0, 160, 160, 1, 100.0f);
        ASSERT_NE(nullptr, thief);
        ASSERT_EQ(-1, static_cast<int>(thief->user()));
        world().rng_.state_ = state_where_draws_are({3, 3}, {0, 2});
        std::string reason;
        ASSERT_TRUE(cast(thief, 1, 0, &reason)) << reason;
        EXPECT_EQ(1, count(Order::FX, FAMILY_BOMB));
        const auto& q = thief->stats()->commands;
        ASSERT_EQ(1u, q.size()) << "AI arm: one forced walk";
        EXPECT_EQ(COMMAND_WALK, q.front().commandtype);
        EXPECT_EQ(20, q.front().commandcount);
        EXPECT_EQ(-1, q.front().com1) << "AI arm: x from the first draw";
        EXPECT_EQ(1, q.front().com2) << "AI arm: y from the second draw";
    }
    // Still roll: both draws 1 (0, 0) becomes a step right.
    {
        fresh_world();
        world().new_specials = 0;
        living* thief = add(FAMILY_THIEF, 0, 160, 160, 1, 100.0f);
        ASSERT_NE(nullptr, thief);
        world().rng_.state_ = state_where_draws_are({3, 3}, {1, 1});
        std::string reason;
        ASSERT_TRUE(cast(thief, 1, 0, &reason)) << reason;
        const auto& q = thief->stats()->commands;
        ASSERT_EQ(1u, q.size()) << "still-roll arm: one forced walk";
        EXPECT_EQ(COMMAND_WALK, q.front().commandtype);
        EXPECT_EQ(20, q.front().commandcount);
        EXPECT_EQ(1, q.front().com1) << "still-roll arm: steps right";
        EXPECT_EQ(0, q.front().com2);
    }
    // Player arm: no command, no draw.
    {
        fresh_world();
        world().new_specials = 0;
        living* thief = add(FAMILY_THIEF, 0, 160, 160, 1, 100.0f);
        ASSERT_NE(nullptr, thief);
        thief->set_user(0);
        thief->set_act_type(ACT_CONTROL);
        world().rng_.state_ = 12345u;
        std::string reason;
        ASSERT_TRUE(cast(thief, 1, 0, &reason)) << reason;
        EXPECT_EQ(1, count(Order::FX, FAMILY_BOMB));
        EXPECT_TRUE(thief->stats()->commands.empty()) << "player arm: no command queued";
        EXPECT_EQ(12345u, world().rng_.state_) << "player arm: no flee draw";
    }
}

// living-11-thief.lua:169. A taunt takes each foe whose roll rand(foe level)
// does not beat the thief's rand(thief level). Every draw answers 9: the
// level-5 thief rolls 4; a level-1 foe rolls 0 and is taunted (follow count
// 10 + 9 % 5 = 14); a level-10 foe rolls 9 and is untouched.
TEST_F(SemThief, a_taunt_takes_only_the_foes_whose_roll_does_not_beat_the_thiefs)
{
    world().new_specials = 0;
    living* thief = add(FAMILY_THIEF, 0, 200, 200, 5, 200.0f);
    living* weak = add(FAMILY_SOLDIER, 1, 230, 200, 1);
    living* strong = add(FAMILY_SOLDIER, 1, 170, 200, 10);
    living* other = add(FAMILY_SOLDIER, 1, 600, 600, 1);
    ASSERT_TRUE(thief && weak && strong && other);
    thief->set_user(0);
    thief->set_act_type(ACT_CONTROL);
    strong->set_foe(other);
    strong->set_leader(other);
    ConstRandom nine(9);
    ScopedSimRandom scope(&nine);
    std::string reason;
    ASSERT_TRUE(cast(thief, 3, 0, &reason)) << reason;
    EXPECT_EQ(thief, weak->foe()) << "thief roll >= foe roll: taunted";
    EXPECT_EQ(thief, weak->leader());
    ASSERT_EQ(1u, weak->stats()->commands.size());
    EXPECT_EQ(COMMAND_FOLLOW, weak->stats()->commands.front().commandtype);
    EXPECT_EQ(14, weak->stats()->commands.front().commandcount);
    EXPECT_EQ(other, strong->foe()) << "thief roll < foe roll: the foe keeps its target";
    EXPECT_EQ(other, strong->leader()) << "thief roll < foe roll: and its leader";
    EXPECT_TRUE(strong->stats()->commands.empty()) << "thief roll < foe roll: and no command";
}

// living-11-thief.lua:173. A taunted player-controlled foe (ACT_CONTROL)
// only has its target and leader changed; a bot is also made to follow.
TEST_F(SemThief, a_taunted_player_foe_keeps_its_own_movement)
{
    world().new_specials = 0;
    living* thief = add(FAMILY_THIEF, 0, 200, 200, 1, 200.0f);
    living* player = add(FAMILY_SOLDIER, 1, 230, 200, 1);
    living* bot = add(FAMILY_SOLDIER, 1, 170, 200, 1);
    ASSERT_TRUE(thief && player && bot);
    thief->set_user(0);
    thief->set_act_type(ACT_CONTROL);
    player->set_act_type(ACT_CONTROL);
    std::string reason;
    ASSERT_TRUE(cast(thief, 3, 0, &reason)) << reason;
    EXPECT_EQ(thief, player->foe()) << "ACT_CONTROL arm: the target changes";
    EXPECT_EQ(thief, player->leader()) << "ACT_CONTROL arm: the leader changes";
    EXPECT_TRUE(player->stats()->commands.empty()) << "ACT_CONTROL arm: no command queued";
    EXPECT_EQ(thief, bot->foe());
    ASSERT_EQ(1u, bot->stats()->commands.size()) << "bot arm: one follow";
    EXPECT_EQ(COMMAND_FOLLOW, bot->stats()->commands.front().commandtype);
    EXPECT_EQ(10, bot->stats()->commands.front().commandcount) << "10 + rand(1)";
}

// living-11-thief.lua:216. An equal-level foe resists a charm when rand(20)
// comes up 0: it turns on the thief and keeps its team; any other draw
// charms it. The old game's charm is the same (!random(20)).
TEST_F(SemThief, an_equal_level_foe_resists_a_charm_one_time_in_twenty)
{
    for (const std::uint32_t draw : {0u, 1u})
    {
        fresh_world();
        world().new_specials = 0;
        living* thief = add(FAMILY_THIEF, 0, 200, 200, 3, 200.0f);
        living* foe = add(FAMILY_SOLDIER, 1, 216, 200, 3);
        ASSERT_TRUE(thief && foe);
        thief->set_user(0);
        thief->set_act_type(ACT_CONTROL);
        events().clear();
        ConstRandom r(draw);
        ScopedSimRandom scope(&r);
        std::string reason;
        ASSERT_TRUE(cast(thief, 3, 0, &reason, true)) << "draw " << draw << ": " << reason;
        if (draw == 0)
        {
            EXPECT_EQ(thief, foe->foe()) << "rand(20)==0 arm: the foe turns on the thief";
            EXPECT_EQ(1, static_cast<int>(foe->team_num())) << "rand(20)==0 arm: its team stands";
            EXPECT_EQ(255, static_cast<int>(foe->real_team_num())) << "rand(20)==0 arm: never charmed";
            EXPECT_EQ(0, static_cast<int>(foe->charm_left()));
            EXPECT_EQ(1, notices("failed to charm!")) << "rand(20)==0 arm: the resist message";
            EXPECT_EQ(0, notices("charmed an opponent!"));
        }
        else
        {
            EXPECT_EQ(0, static_cast<int>(foe->team_num())) << "charm arm: the foe joins the thief";
            EXPECT_EQ(1, static_cast<int>(foe->real_team_num()));
            EXPECT_EQ(75, static_cast<int>(foe->charm_left())) << "75 + 25 x 0";
            EXPECT_EQ(1, notices("charmed an opponent!"));
            EXPECT_EQ(0, notices("failed to charm!"));
        }
    }
}

// living-11-thief.lua:224. A charmed foe drops its quarry: the thief's own
// target gets no foe; any other charmed foe takes the thief's target.
TEST_F(SemThief, a_charmed_foe_takes_the_thiefs_target_unless_it_was_that_target)
{
    for (const bool was_target : {true, false})
    {
        fresh_world();
        world().new_specials = 0;
        living* thief = add(FAMILY_THIEF, 0, 200, 200, 6, 200.0f);
        living* foe = add(FAMILY_SOLDIER, 2, 216, 204, 1);
        living* far = add(FAMILY_ORC, 2, 400, 200, 1);
        ASSERT_TRUE(thief && foe && far);
        thief->set_user(0);
        thief->set_act_type(ACT_CONTROL);
        foe->set_foe(far);
        thief->set_foe(was_target ? static_cast<walker*>(foe) : static_cast<walker*>(far));
        ConstRandom one(1);
        ScopedSimRandom scope(&one);
        std::string reason;
        ASSERT_TRUE(cast(thief, 3, 0, &reason, true)) << reason;
        ASSERT_EQ(0, static_cast<int>(foe->team_num())) << "the foe is charmed";
        if (was_target)
            EXPECT_EQ(nullptr, foe->foe()) << "target-was-thief's-foe arm: no foe";
        else
            EXPECT_EQ(far, foe->foe()) << "thief-had-another-foe arm: the thief's target";
    }
}

// ===========================================================================
// The ghost (packs/core/families/living-12-ghost.lua)
// ===========================================================================

// living-12-ghost.lua:16. With the Gameplay Fixes setting on only a LIVING
// foe makes a scare worth casting: an enemy tent inside the scare radius
// (60 px at level 1) is not one, and the cast is refused before it spends
// anything; a living foe there lets it through.
TEST_F(SemGhost, a_generator_alone_in_the_scare_radius_does_not_let_a_scare_through)
{
    world().new_specials = 0;
    living* ghost = body(add(FAMILY_GHOST, 0, 200, 200, 1, 100.0f), 16, 16);
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_TRUE(ghost && tent);
    tent->set_team_num(1);
    tent->setxy(240, 200);
    tent->set_sizex(16);
    tent->set_sizey(16);
    ghost->set_user(0);
    ghost->set_act_type(ACT_CONTROL);

    std::string reason;
    EXPECT_FALSE(cast(ghost, 1, 1, &reason)) << "only a generator in reach";
    EXPECT_EQ(std::string("NO FOE IN RANGE"), reason) << "only-generators arm";
    EXPECT_FLOAT_EQ(100.0f, ghost->stats()->magicpoints()) << "only-generators arm: nothing spent";
    EXPECT_EQ(0u, any(Order::FX, FAMILY_GHOST_SCARE).size()) << "only-generators arm: no scare made";

    living* foe = body(add(FAMILY_SOLDIER, 1, 240, 220, 1), 16, 16);
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(cast(ghost, 1, 1, &reason)) << "a living foe in reach: " << reason;
    EXPECT_EQ(1u, any(Order::FX, FAMILY_GHOST_SCARE).size()) << "living-foe arm: one scare";
    EXPECT_FLOAT_EQ(70.0f, ghost->stats()->magicpoints()) << "living-foe arm: SCARE's 30 MP";
}

// living-12-ghost.lua:30. With the setting on a ghost bot's SCARE gate says
// yes with a living foe inside the scare radius, and casts it unshifted.
TEST_F(SemGhost, a_fixed_scare_gate_says_yes_and_casts_unshifted)
{
    world().new_specials = 0;
    living* ghost = body(add(FAMILY_GHOST, 1, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 260, 200, 1), 25, 29);  // 60 px
    ASSERT_TRUE(ghost && foe);
    ghost->set_shifter_down(1);
    EXPECT_TRUE(gate(ghost, 1, 1)) << "fixes on, living foe inside the scare radius: true";
    EXPECT_EQ(0, static_cast<int>(ghost->shifter_down())) << "fixes on, living foe inside: shifter 0";
}

// ===========================================================================
// The druid (packs/core/families/living-13-druid.lua)
// ===========================================================================

// living-13-druid.lua:13, :42, :78, :88. A busy druid casts none of its four
// specials: SPECIAL BUSY, nothing made, nothing paid, nothing revealed. The
// old game's four cases open with the same `if (busy > 0) return 0`.
TEST_F(SemDruid, a_busy_druid_casts_none_of_its_four_specials)
{
    for (const int slot : {1, 2, 3, 4})
    {
        fresh_world();
        living* druid = body(add(FAMILY_DRUID, 0, 200, 200, 5, 1000.0f), 16, 16);
        living* ally = body(add(FAMILY_SOLDIER, 0, 210, 216, 1), 16, 16);
        ASSERT_TRUE(druid && ally);
        druid->set_user(0);
        druid->set_act_type(ACT_CONTROL);
        face(druid, 1.0f, 0.0f, FACE_RIGHT);
        druid->set_busy(5.0f);
        const short view = druid->view_all();
        std::string reason;
        EXPECT_FALSE(cast(druid, slot, 0, &reason)) << "busy arm, slot " << slot;
        EXPECT_EQ(std::string("SPECIAL BUSY"), reason) << "busy arm, slot " << slot;
        EXPECT_FLOAT_EQ(1000.0f, druid->stats()->magicpoints()) << "busy arm: nothing paid, slot " << slot;
        EXPECT_EQ(0u, any(Order::Weapon, FAMILY_TREE).size()) << "busy arm: no tree, slot " << slot;
        EXPECT_EQ(0u, any(Order::Living, FAMILY_FAERIE).size()) << "busy arm: no faerie, slot " << slot;
        EXPECT_EQ(0u, any(Order::Weapon, FAMILY_CIRCLE_PROTECTION).size()) << "busy arm: no circle, slot " << slot;
        EXPECT_EQ(0u, any(Order::Weapon, FAMILY_LIGHTNING).size()) << "busy arm: no bolt fired, slot " << slot;
        EXPECT_EQ(view, druid->view_all()) << "busy arm: nothing revealed, slot " << slot;
        EXPECT_FLOAT_EQ(5.0f, druid->busy()) << "busy arm: the rest is untouched, slot " << slot;
    }
}

// living-13-druid.lua:16 and :45. With the setting off a bolt aimed into a
// wall is not refused up front: fire() is attempted (its waver is drawn, so
// the sim stream moves) and fails there, COULD NOT FIRE, with the refund
// and the swing cancelling out. As the old game's cases 1 and 2.
TEST_F(SemDruid, a_classic_cast_into_a_wall_attempts_the_bolt_and_fails_there)
{
    struct Case { int slot; float mp; };
    for (const Case c : {Case{1, 23.0f}, Case{2, 100.0f}})
    {
        fresh_world();
        living* druid = body(add(FAMILY_DRUID, 0, 100, 100, 5, c.mp), 16, 16);
        ASSERT_NE(nullptr, druid);
        druid->set_user(0);
        druid->set_act_type(ACT_CONTROL);
        face(druid, 1.0f, 0.0f, FACE_RIGHT);
        pad_wall(world(), (druid->xpos() + druid->sizex() + 1) / GRID_SIZE,
                 (druid->ypos() + druid->sizey() / 2) / GRID_SIZE);
        ASSERT_EQ(walker::PadAnswer::Terrain, druid->fire_pad_answer()) << "precondition: a wall on the pad";
        world().rng_.state_ = 4242u;
        std::string reason;
        EXPECT_FALSE(cast(druid, c.slot, 0, &reason)) << "fixes off, pad on terrain, slot " << c.slot;
        EXPECT_EQ(std::string("COULD NOT FIRE"), reason) << "slot " << c.slot;
        EXPECT_FLOAT_EQ(c.mp, druid->stats()->magicpoints()) << "refund and swing cancel, slot " << c.slot;
        // fire() draws exactly one number before the pad check: the waver in
        // set_weapon_heading (walker.cpp), one glibc LCG step from 4242.
        EXPECT_EQ(4242u * 1103515245u + 12345u, world().rng_.state_)
            << "fixes off: fire() was attempted (one waver draw), slot " << c.slot;
        EXPECT_EQ(0, count(Order::Weapon, FAMILY_TREE)) << "slot " << c.slot;
        EXPECT_EQ(0, count(Order::Living, FAMILY_FAERIE)) << "slot " << c.slot;
    }
}

// living-13-druid.lua:68. A faerie summoned where its body can stand lives:
// owned by the druid, on its team, with druid_faerie_lifetime(4) = 50 + 40 x
// 4 = 210 (the old game's 50 + level x 40), and the druid rests 3 x
// fire_frequency.
TEST_F(SemDruid, a_faerie_summoned_on_open_ground_lives_for_its_lifetime)
{
    living* druid = body(add(FAMILY_DRUID, 0, 200, 200, 4, 300.0f), 16, 16);
    ASSERT_NE(nullptr, druid);
    druid->set_user(0);
    druid->set_act_type(ACT_CONTROL);
    face(druid, 1.0f, 0.0f, FACE_RIGHT);
    const float f = druid->fire_frequency();
    std::string reason;
    ASSERT_TRUE(cast(druid, 2, 0, &reason)) << reason;
    const std::vector<walker*> faeries = live(Order::Living, FAMILY_FAERIE);
    ASSERT_EQ(1u, faeries.size()) << "spot-passable arm: one live faerie";
    walker* faerie = faeries[0];
    EXPECT_EQ(druid, faerie->owner());
    EXPECT_EQ(0, static_cast<int>(faerie->team_num()));
    EXPECT_EQ(210, faerie->lifetime());
    EXPECT_FLOAT_EQ(3.0f * f, druid->busy()) << "spot-passable arm: busy += 3 x fire_frequency";
    EXPECT_FLOAT_EQ(220.0f, druid->stats()->magicpoints()) << "SUMMON FAERIE's 80 MP";
}

// living-13-druid.lua:68. A faerie whose body lands on a body (an ally of
// the druid's: the bolt passes it, the faerie cannot) is cancelled: no live
// faerie, NO ROOM TO SUMMON, no rest and nothing paid.
TEST_F(SemDruid, a_faerie_summoned_onto_a_body_is_cancelled)
{
    // Where the faerie lands, read from an open-ground cast.
    short fx = 0, fy = 0;
    {
        living* druid = body(add(FAMILY_DRUID, 0, 200, 200, 4, 300.0f), 16, 16);
        ASSERT_NE(nullptr, druid);
        druid->set_user(0);
        druid->set_act_type(ACT_CONTROL);
        face(druid, 1.0f, 0.0f, FACE_RIGHT);
        std::string reason;
        ASSERT_TRUE(cast(druid, 2, 0, &reason)) << reason;
        const std::vector<walker*> faeries = live(Order::Living, FAMILY_FAERIE);
        ASSERT_EQ(1u, faeries.size());
        fx = faeries[0]->xpos();
        fy = faeries[0]->ypos();
    }
    fresh_world();
    living* druid = body(add(FAMILY_DRUID, 0, 200, 200, 4, 300.0f), 16, 16);
    living* ally = settle_body(body(add(FAMILY_SOLDIER, 0, fx, fy, 1), 16, 16));
    ASSERT_TRUE(druid && ally);
    druid->set_user(0);
    druid->set_act_type(ACT_CONTROL);
    face(druid, 1.0f, 0.0f, FACE_RIGHT);
    std::string reason;
    EXPECT_FALSE(cast(druid, 2, 0, &reason)) << "blocked arm";
    EXPECT_EQ(std::string("NO ROOM TO SUMMON"), reason) << "blocked arm";
    EXPECT_EQ(0, count(Order::Living, FAMILY_FAERIE)) << "blocked arm: no live faerie";
    EXPECT_FLOAT_EQ(0.0f, druid->busy()) << "blocked arm: no rest";
    EXPECT_FLOAT_EQ(300.0f, druid->stats()->magicpoints()) << "blocked arm: nothing paid";
}

// living-13-druid.lua:111. A friend's ring is known by owner AND family: a
// knife the friend owns, lying on it, is not a ring, so the cast mints the
// friend a new circle and leaves the knife alone.
TEST_F(SemDruid, a_friends_own_knife_is_not_mistaken_for_its_ring)
{
    living* druid = body(add(FAMILY_DRUID, 0, 160, 160, 5, 1000.0f), 16, 16);
    living* ally = body(add(FAMILY_SOLDIER, 0, 160, 176, 1), 16, 16);
    walker* knife = world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(druid && ally && knife);
    druid->set_user(0);
    druid->set_act_type(ACT_CONTROL);
    knife->set_owner(ally);
    knife->set_team_num(0);
    knife->setxy(160, 176);
    knife->stats()->set_hitpoints(9.0f);
    std::string reason;
    ASSERT_TRUE(cast(druid, 4, 0, &reason)) << reason;
    const std::vector<walker*> circles = live(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_EQ(1u, circles.size()) << "own-non-circle-weapons arm: a new circle";
    EXPECT_EQ(ally, circles[0]->owner()) << "own-non-circle-weapons arm: the ally's";
    EXPECT_FLOAT_EQ(9.0f, knife->stats()->hitpoints()) << "own-non-circle-weapons arm: the knife is not topped up";
}

// living-13-druid.lua:137. A hero druid earns its level (EXP_PROTECTION) in
// experience for every friend it protects, new ring or top-up alike; a
// monster druid earns nothing and the cast runs clean.
TEST_F(SemDruid, a_hero_druid_earns_experience_per_friend_protected)
{
    living* druid = body(add(FAMILY_DRUID, 0, 160, 160, 5, 2000.0f), 16, 16);
    living* a = body(add(FAMILY_SOLDIER, 0, 160, 190, 1), 16, 16);
    living* b = body(add(FAMILY_SOLDIER, 0, 190, 160, 1), 16, 16);
    ASSERT_TRUE(druid && a && b);
    druid->set_user(0);
    druid->set_act_type(ACT_CONTROL);
    make_hero(druid, FAMILY_DRUID);
    druid->myguy->exp = 100;
    std::string reason;
    ASSERT_TRUE(cast(druid, 4, 0, &reason)) << reason;
    ASSERT_EQ(2, count(Order::Weapon, FAMILY_CIRCLE_PROTECTION)) << "two new rings";
    EXPECT_EQ(110u, druid->myguy->exp) << "hero arm: + level 5 per friend, new rings";
    ASSERT_TRUE(cast(druid, 4, 0, &reason)) << reason;
    EXPECT_EQ(2, count(Order::Weapon, FAMILY_CIRCLE_PROTECTION)) << "the rings are topped up";
    EXPECT_EQ(120u, druid->myguy->exp) << "hero arm: + level 5 per friend, top-ups";

    fresh_world();
    living* monster = body(add(FAMILY_DRUID, 0, 160, 160, 5, 2000.0f), 16, 16);
    ASSERT_TRUE(monster && body(add(FAMILY_SOLDIER, 0, 160, 190, 1), 16, 16) &&
                body(add(FAMILY_SOLDIER, 0, 190, 160, 1), 16, 16));
    monster->set_user(0);
    monster->set_act_type(ACT_CONTROL);
    EXPECT_TRUE(cast(monster, 4, 0, &reason)) << "monster arm: " << reason;
    EXPECT_EQ(nullptr, monster->myguy) << "monster arm: no character";
    EXPECT_EQ(2, count(Order::Weapon, FAMILY_CIRCLE_PROTECTION));
    EXPECT_EQ(0u, guard_.count()) << "monster arm: the exp line never ran: " << guard_.message();
}

// living-13-druid.lua:143. The PROTECTION message counts the friends
// protected, singular for one.
TEST_F(SemDruid, the_protection_message_counts_the_friends_singular_for_one)
{
    for (const int allies : {1, 2})
    {
        fresh_world();
        living* druid = body(add(FAMILY_DRUID, 0, 160, 160, 5, 1000.0f), 16, 16);
        ASSERT_NE(nullptr, druid);
        druid->set_user(0);
        druid->set_act_type(ACT_CONTROL);
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 0, 160, 190, 1), 16, 16));
        if (allies == 2)
        {
            ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 0, 190, 160, 1), 16, 16));
        }
        events().clear();
        std::string reason;
        ASSERT_TRUE(cast(druid, 4, 0, &reason)) << reason;
        EXPECT_EQ(1, all_notices()) << allies << " allies: one notice";
        if (allies == 1)
        {
            EXPECT_EQ(1, notices("Druid protected 1 man!")) << "1-friend arm";
            EXPECT_EQ(0, notices("men!"));
        }
        else
        {
            EXPECT_EQ(1, notices("Druid protected 2 men!")) << "N>1 arm";
            EXPECT_EQ(0, notices("1 man!"));
        }
    }
}

// living-13-druid.lua:148. Only the player's side (team 0) or a hero druid
// announces PROTECTION; an enemy monster druid casts silently. The heal
// sound is emitted on every arm (the old game played it when the druid was
// on screen; the sim event is the client's to place).
TEST_F(SemDruid, only_the_home_team_or_a_hero_druid_announces_protection)
{
    struct Case { unsigned char team; bool hero; int notices; const char* arm; };
    for (const Case c : {Case{1, false, 0, "enemy monster druid"},
                         Case{1, true, 1, "enemy hero druid"},
                         Case{0, false, 1, "team-0 monster druid"}})
    {
        fresh_world();
        living* druid = body(add(FAMILY_DRUID, c.team, 160, 160, 5, 1000.0f), 16, 16);
        ASSERT_NE(nullptr, druid);
        ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, c.team, 160, 190, 1), 16, 16));
        if (c.hero)
        {
            make_hero(druid, FAMILY_DRUID);
        }
        events().clear();
        std::string reason;
        ASSERT_TRUE(cast(druid, 4, 0, &reason)) << c.arm << ": " << reason;
        EXPECT_EQ(c.notices, notices("Druid protected")) << c.arm;
        EXPECT_EQ(1, sounds(SOUND_HEAL)) << c.arm << ": the heal sound either way";
    }
}

// ===========================================================================
// The orc (packs/core/families/living-14-orc.lua)
// ===========================================================================

// living-14-orc.lua:22-28. The fixed gate answers only HOWL (slot 1) and EAT
// CORPSE (slot 2); any other slot falls through to the classic answer (a
// foe within 130 px) on both values. A stock orc has two slots; the gate is
// asked for slot 3 directly.
TEST_F(SemOrc, the_fixed_gate_leaves_any_other_slot_to_the_classic_answer)
{
    living* orc = body(add(FAMILY_ORC, 1, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 300, 200, 1), 16, 16);  // 100 px
    ASSERT_TRUE(orc && foe);
    orc->set_foe(nullptr);
    EXPECT_TRUE(gate(orc, 3, 0)) << "classic: a foe within 130";
    orc->set_foe(nullptr);
    EXPECT_TRUE(gate(orc, 3, 1)) << "other-slot arm, fixes on: the classic answer (true)";
    foe->setxy(400, 200);  // 200 px
    orc->set_foe(nullptr);
    EXPECT_FALSE(gate(orc, 3, 0)) << "classic: nobody within 130";
    orc->set_foe(nullptr);
    EXPECT_FALSE(gate(orc, 3, 1)) << "other-slot arm, fixes on: the classic answer (false)";
}

// orc_specials.lua:78 (#456). EAT CORPSE reaches only a stain on the orc's
// own floor: a hurt orc on a balcony (floor 1) right above a corpse on
// floor 0 refuses NO CORPSE NEARBY, for free, and the stain stays; with the
// stain on the balcony it eats it and heals the stain's level times 5.
TEST_F(SemOrc, an_orc_on_a_balcony_cannot_eat_the_corpse_below)
{
    for (const short stain_floor : {short{0}, short{1}})
    {
        fresh_world();
        world().set_floor_count(2);
        living* orc = body(add(FAMILY_ORC, 1, 200, 200, 4, 100.0f), 16, 16);
        walker* stain = world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
        ASSERT_TRUE(orc && stain);
        orc->change_floor(1);
        orc->setxy(200, 200);
        orc->stats()->set_hitpoints(100.0f);  // hurt: 100 of 400
        stain->set_sizex(16);
        stain->set_sizey(16);
        stain->set_floor(stain_floor);
        stain->stats()->set_level(3);
        stain->set_dead(0);
        stain->setxy(200, 200);
        ASSERT_EQ(0, orc->distance_to_ob_center(stain)) << "staging: right above or on it";
        std::string reason;
        const bool ate = cast(orc, 2, 0, &reason);
        if (stain_floor == 0)
        {
            EXPECT_FALSE(ate) << "the corpse below: refused";
            EXPECT_EQ("NO CORPSE NEARBY", reason) << "the corpse below";
            EXPECT_FLOAT_EQ(100.0f, orc->stats()->hitpoints()) << "the corpse below: no heal";
            EXPECT_FLOAT_EQ(100.0f, orc->stats()->magicpoints()) << "the corpse below: free";
            EXPECT_EQ(0, static_cast<int>(stain->dead())) << "the corpse below: not eaten";
        }
        else
        {
            EXPECT_TRUE(ate) << "the corpse on the balcony: " << reason;
            EXPECT_FLOAT_EQ(115.0f, orc->stats()->hitpoints()) << "level 3 * 5 healed";
            EXPECT_FLOAT_EQ(80.0f, orc->stats()->magicpoints()) << "the 20-MP price";
            EXPECT_EQ(1, static_cast<int>(stain->dead())) << "the corpse on the balcony: eaten";
        }
    }
}

// ===========================================================================
// The barbarian (packs/core/families/living-16-barbarian.lua)
// ===========================================================================

// living-16-barbarian.lua:16. A busy barbarian throws nothing: SPECIAL BUSY,
// no boulder, no hammer, nothing paid, nothing drawn.
TEST_F(SemBarbarian, a_busy_barbarian_throws_no_boulder)
{
    living* barb = body(add(FAMILY_BARBARIAN, 0, 160, 160, 5, 500.0f), 16, 16);
    ASSERT_NE(nullptr, barb);
    barb->set_user(0);
    barb->set_act_type(ACT_CONTROL);
    face(barb, 1.0f, 0.0f, FACE_RIGHT);
    barb->set_busy(5.0f);
    world().rng_.state_ = 777u;
    std::string reason;
    EXPECT_FALSE(cast(barb, 1, 0, &reason)) << "busy arm";
    EXPECT_EQ(std::string("SPECIAL BUSY"), reason) << "busy arm";
    EXPECT_EQ(0u, any(Order::Weapon, FAMILY_BOULDER).size()) << "busy arm: no boulder";
    EXPECT_EQ(0u, any(Order::Weapon, FAMILY_HAMMER).size()) << "busy arm: fire() never ran";
    EXPECT_FLOAT_EQ(500.0f, barb->stats()->magicpoints()) << "busy arm: nothing paid";
    EXPECT_EQ(777u, world().rng_.state_) << "busy arm: nothing drawn";
}

// living-16-barbarian.lua:21. The boulder rides on the hammer shot: a fired
// hammer becomes a boulder centred on it (the hammer marked dead); with a
// wall under the hammer's pad there is no shot and no boulder.
TEST_F(SemBarbarian, a_boulder_is_made_only_from_a_hammer_that_flew)
{
    {
        living* barb = body(add(FAMILY_BARBARIAN, 0, 160, 160, 5, 500.0f), 16, 16);
        ASSERT_NE(nullptr, barb);
        barb->set_user(0);
        barb->set_act_type(ACT_CONTROL);
        face(barb, 1.0f, 0.0f, FACE_RIGHT);
        std::string reason;
        ASSERT_TRUE(cast(barb, 1, 0, &reason)) << "shot arm: " << reason;
        const std::vector<walker*> boulders = live(Order::Weapon, FAMILY_BOULDER);
        ASSERT_EQ(1u, boulders.size()) << "shot arm: one boulder";
        const std::vector<walker*> hammers = any(Order::Weapon, FAMILY_HAMMER);
        ASSERT_EQ(1u, hammers.size()) << "shot arm: the one hammer fired";
        EXPECT_EQ(1, static_cast<int>(hammers[0]->dead())) << "shot arm: the hammer is spent";
        EXPECT_EQ(hammers[0]->xpos() + hammers[0]->sizex() / 2,
                  boulders[0]->xpos() + boulders[0]->sizex() / 2) << "shot arm: centred on the hammer (x)";
        EXPECT_EQ(hammers[0]->ypos() + hammers[0]->sizey() / 2,
                  boulders[0]->ypos() + boulders[0]->sizey() / 2) << "shot arm: centred on the hammer (y)";
        EXPECT_EQ(barb, boulders[0]->owner());
    }
    fresh_world();
    living* barb = body(add(FAMILY_BARBARIAN, 0, 160, 160, 5, 500.0f), 16, 16);
    ASSERT_NE(nullptr, barb);
    barb->set_user(0);
    barb->set_act_type(ACT_CONTROL);
    face(barb, 1.0f, 0.0f, FACE_RIGHT);
    pad_wall(world(), (barb->xpos() + barb->sizex() + 1) / GRID_SIZE,
             (barb->ypos() + barb->sizey() / 2) / GRID_SIZE);
    std::string reason;
    EXPECT_FALSE(cast(barb, 1, 0, &reason)) << "no-shot arm";
    EXPECT_EQ(std::string("COULD NOT FIRE"), reason) << "no-shot arm";
    EXPECT_EQ(0u, any(Order::Weapon, FAMILY_BOULDER).size()) << "no-shot arm: no boulder";
}

namespace {

// One HURL BOULDER's results: the boulder (nullptr when none) and the
// hammer it rode on (spent, still in the weapon list until the tick).
struct Throw
{
    walker* boulder = nullptr;
    walker* hammer = nullptr;
};

}  // namespace

class SemBoulder : public SemBarbarian
{
protected:
    // The boulder weapon's own damage, read off a freshly made one.
    float boulder_base_damage()
    {
        fresh_world();
        walker* b = world().add_ob(Order::Weapon, FAMILY_BOULDER);
        return b != nullptr ? b->damage() : -1.0f;
    }

    // One HURL BOULDER from a fresh world: the barbarian at (160, 160)
    // facing `dir`, of `level`, a hero of `strength` when strength > 0.
    Throw hurl(int level, short strength, float lx, float ly, int dir)
    {
        fresh_world();
        Throw t;
        living* barb = body(add(FAMILY_BARBARIAN, 0, 160, 160, level, 500.0f), 16, 16);
        if (barb == nullptr)
            return t;
        barb->set_user(0);
        barb->set_act_type(ACT_CONTROL);
        if (strength > 0)
        {
            make_hero(barb, FAMILY_BARBARIAN);
            barb->myguy->strength = strength;
        }
        face(barb, lx, ly, dir);
        std::string reason;
        if (!cast(barb, 1, 0, &reason))
        {
            ADD_FAILURE() << "the throw refused: " << reason;
            return t;
        }
        const std::vector<walker*> boulders = live(Order::Weapon, FAMILY_BOULDER);
        const std::vector<walker*> hammers = any(Order::Weapon, FAMILY_HAMMER);
        if (boulders.size() == 1)
            t.boulder = boulders[0];
        if (hammers.size() == 1)
            t.hammer = hammers[0];
        return t;
    }
};

// living-16-barbarian.lua:35-43. A hero's boulder rolls at 1 + strength / 7
// px a step and hits for strength / 5 extra (strength 21: 4 px, +4.2); a
// monster's rolls at 2 px per level and hits for its level extra (level 3:
// 6 px, +3). As the old game's walker::special.
TEST_F(SemBoulder, a_heros_boulder_goes_by_strength_and_a_monsters_by_level)
{
    const float base = boulder_base_damage();
    ASSERT_GE(base, 0.0f) << "a boulder can be made";
    const Throw hero = hurl(1, 21, 1.0f, 0.0f, FACE_RIGHT);
    ASSERT_NE(nullptr, hero.boulder);
    EXPECT_FLOAT_EQ(4.0f, hero.boulder->stepsize()) << "hero arm: 1 + 21 / 7";
    EXPECT_FLOAT_EQ(base + 21.0f / 5.0f, hero.boulder->damage()) << "hero arm: + strength / 5";
    const Throw monster = hurl(3, 0, 1.0f, 0.0f, FACE_RIGHT);
    ASSERT_NE(nullptr, monster.boulder);
    EXPECT_FLOAT_EQ(6.0f, monster.boulder->stepsize()) << "monster arm: 2 x level 3";
    EXPECT_FLOAT_EQ(base + 3.0f, monster.boulder->damage()) << "monster arm: + level";
}

// living-16-barbarian.lua:48-51. A boulder's speed is held to 1 .. 15: a
// level-10 monster's 20 is cut to 15; a level-3 monster's 6 stands. The
// heading's x is rebuilt from the held speed.
TEST_F(SemBoulder, a_boulders_speed_is_held_to_fifteen)
{
    const Throw fast = hurl(10, 0, 1.0f, 0.0f, FACE_RIGHT);
    ASSERT_NE(nullptr, fast.boulder);
    EXPECT_FLOAT_EQ(15.0f, fast.boulder->stepsize()) << "computed > 15 arm: 20 held to 15";
    EXPECT_FLOAT_EQ(15.0f, fast.boulder->lastx()) << "computed > 15 arm: x from the held speed";
    const Throw mid = hurl(3, 0, 1.0f, 0.0f, FACE_RIGHT);
    ASSERT_NE(nullptr, mid.boulder);
    EXPECT_FLOAT_EQ(6.0f, mid.boulder->stepsize()) << "in-range arm: 6 stands";
    EXPECT_FLOAT_EQ(6.0f, mid.boulder->lastx()) << "in-range arm: x from it";
}

// living-16-barbarian.lua:54-61. The boulder leaves along the hammer's
// heading at its own speed: a positive component becomes +stepsize, a
// negative one -stepsize, a zero one stays zero. A level-2 monster (4 px).
// The hammer's waver is the sim stream's draw: answered so the waver is 0
// (the cross component of a cardinal throw is then 0) or its most negative.
TEST_F(SemBoulder, a_boulder_keeps_the_sign_of_each_heading_component)
{
    // The waver is rand(w + 1) - w / 2 with w = hammer stepsize / 2.
    const Throw probe = hurl(2, 0, -1.0f, 0.0f, FACE_LEFT);
    ASSERT_NE(nullptr, probe.hammer);
    const auto w = static_cast<std::uint32_t>(probe.hammer->stepsize() / 2.0f);
    ASSERT_GE(w, 2u) << "precondition: the hammer can waver";
    {
        ConstRandom still(w / 2);
        ScopedSimRandom scope(&still);
        const Throw left = hurl(2, 0, -1.0f, 0.0f, FACE_LEFT);
        ASSERT_TRUE(left.boulder && left.hammer);
        ASSERT_FLOAT_EQ(0.0f, left.hammer->lasty()) << "precondition: no waver";
        EXPECT_FLOAT_EQ(-4.0f, left.boulder->lastx()) << "component < 0 arm";
        EXPECT_FLOAT_EQ(0.0f, left.boulder->lasty()) << "component 0 arm";
        const Throw up_left = hurl(2, 0, -1.0f, -1.0f, FACE_UP_LEFT);
        ASSERT_NE(nullptr, up_left.boulder);
        EXPECT_FLOAT_EQ(-4.0f, up_left.boulder->lastx()) << "component < 0 arm (x)";
        EXPECT_FLOAT_EQ(-4.0f, up_left.boulder->lasty()) << "component < 0 arm (y)";
    }
    {
        ConstRandom low(0);
        ScopedSimRandom scope(&low);
        const Throw right = hurl(2, 0, 1.0f, 0.0f, FACE_RIGHT);
        ASSERT_TRUE(right.boulder && right.hammer);
        ASSERT_LT(right.hammer->lasty(), 0.0f) << "precondition: the waver leans up";
        EXPECT_FLOAT_EQ(4.0f, right.boulder->lastx()) << "component > 0 arm";
        EXPECT_FLOAT_EQ(-4.0f, right.boulder->lasty()) << "component < 0 arm: a small lean is a full step";
    }
}

// living-16-barbarian.lua:79-81. With the setting on the HURL BOULDER gate
// measures the flight with the throw's own speed, clamped 1 .. 15, times 8
// steps: a hero of strength 14 reaches exactly 24 px, a monster of level 8
// (16 held to 15) and of level 10 (20 held to 15) exactly 120.
TEST_F(SemBarbarian, the_boulder_gate_measures_the_flight_with_the_throws_own_speed)
{
    // The tower is 25 x 29 above a 16 x 16 barbarian at (200, 200): a gap
    // of g px puts it at y = 200 - 29 - g.
    auto gap_at = [](int g) { return 171 - g; };
    living* barb = body(add(FAMILY_BARBARIAN, 1, 200, 200, 1, 100.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 195, gap_at(24), 1), 25, 29);
    ASSERT_TRUE(barb && foe);
    face(barb, 0.0f, -1.0f, FACE_UP);
    make_hero(barb, FAMILY_BARBARIAN);
    barb->myguy->strength = 14;
    EXPECT_TRUE(gate(barb, 1, 1)) << "hero strength 14 arm: a 24 px gap is inside";
    foe->setxy(195, gap_at(25));
    EXPECT_FALSE(gate(barb, 1, 1)) << "hero strength 14 arm: 25 px is past it";

    barb->clear_myguy();
    for (const int level : {8, 10})
    {
        barb->stats()->set_level(static_cast<short>(level));
        foe->setxy(195, gap_at(120));
        EXPECT_TRUE(gate(barb, 1, 1)) << "monster level " << level << " arm: 120 px is inside";
        foe->setxy(195, gap_at(121));
        EXPECT_FALSE(gate(barb, 1, 1)) << "monster level " << level << " arm: 121 px is past it";
    }
}

// living-16-barbarian.lua:117-119. With the setting on EXPLODING BOULDER
// without a boulder target refuses, whatever cluster there is: a level-4
// barbarian (64 px of flight) facing UP with a two-tower cluster behind it,
// or ahead past its flight. The classic gate says yes to both.
TEST_F(SemBarbarian, an_exploding_boulder_gate_with_no_boulder_target_refuses)
{
    living* barb = body(add(FAMILY_BARBARIAN, 1, 200, 200, 4, 200.0f), 16, 16);
    living* a = body(add(FAMILY_TOWER1, 2, 195, 256, 1), 25, 29);  // 40 px below
    living* b = body(add(FAMILY_TOWER1, 2, 215, 256, 1), 25, 29);
    ASSERT_TRUE(barb && a && b);
    face(barb, 0.0f, -1.0f, FACE_UP);
    EXPECT_TRUE(gate(barb, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(barb, 2, 1)) << "slot 2, no target arm: the cluster is behind";
    a->setxy(195, 101);  // 70 px above: past the 64 px flight
    b->setxy(215, 101);
    EXPECT_TRUE(gate(barb, 2, 0)) << "the classic answer";
    EXPECT_FALSE(gate(barb, 2, 1)) << "slot 2, no target arm: the cluster is past the flight";
    a->setxy(195, 131);  // 40 px above: inside it
    b->setxy(215, 131);
    EXPECT_TRUE(gate(barb, 2, 1)) << "control: the same cluster inside the flight";
}

// ===========================================================================
// The archmage (packs/core/families/living-17-archmage.lua)
// ===========================================================================

namespace {

class SemArchmage : public SemFamilies3
{
protected:
    // One shot east from a bot archmage with `mp` magic; the shot's damage
    // and the magic left, the draws pinned to one constant.
    struct Shot {
        bool fired = false;
        float damage = 0.0f;
        float mp = 0.0f;
    };

    Shot shoot(float mp)
    {
        fresh_world();
        ConstRandom rng(0);
        ScopedSimRandom scoped(&rng);
        Shot s;
        living* a = add(FAMILY_ARCHMAGE, 0, 200, 100, 4, mp);
        if (a == nullptr)
            return s;
        a->stats()->set_max_magicpoints(mp + 1.0f);
        a->stats()->set_magicpoints(mp);
        face(a, 1, 0, FACE_RIGHT);
        walker* shot = a->fire();
        if (shot == nullptr || shot->dead())
            return s;
        s.fired = true;
        s.damage = shot->damage();
        s.mp = a->stats()->magicpoints();
        return s;
    }

    float weapon_cost()
    {
        fresh_world();
        living* a = add(FAMILY_ARCHMAGE, 0, 200, 100, 4, 0.0f);
        return a == nullptr ? -1.0f : a->stats()->weapon_cost();
    }
};

}  // namespace

// living-17-archmage.lua:35 on_fire_weapon, the cap arm. Every shot drains a
// twentieth of the magic left after its cost into its damage, capped at
// SHOT_DRAIN_CAP (50): the cap binds above 1000 magic after the cost. This
// is the current game's documented rule (docs/runaway-effects-design.md
// section 2.12, "Archmage weapon drain/damage bonus | min(magicpoints / 20,
// 50)"), not a classic one: the old game drains magicpoints / 20 uncapped.
//
// RED (`og.min(extra, C.SHOT_DRAIN_CAP)` -> `og.min(extra, C.SHOT_DRAIN_CAP
// * 100)` at living-17-archmage.lua:35): Expected equality of these values:
// base.damage + 50.0f Which is: 120 capped.damage Which is: 121.
TEST_F(SemArchmage, an_archmage_shot_drains_at_most_fifty_magic_into_its_damage)
{
    const float cost = weapon_cost();
    ASSERT_GT(cost, 0.0f) << "staging: the archmage's shot has a price";
    const Shot base = shoot(cost);
    ASSERT_TRUE(base.fired) << "staging: the control shot launches";
    EXPECT_FLOAT_EQ(0.0f, base.mp) << "control: no spare magic, nothing drained";

    // Exactly 1000 after the cost: 1000 / 20 = 50, the cap and the drain agree.
    const Shot edge = shoot(cost + 1000.0f);
    ASSERT_TRUE(edge.fired);
    EXPECT_FLOAT_EQ(base.damage + 50.0f, edge.damage) << "1000 spare: drain 50";
    EXPECT_FLOAT_EQ(950.0f, edge.mp) << "1000 spare: drain 50";

    // 1020 after the cost: 1020 / 20 = 51, held to 50.
    const Shot capped = shoot(cost + 1020.0f);
    ASSERT_TRUE(capped.fired);
    EXPECT_FLOAT_EQ(base.damage + 50.0f, capped.damage) << "1020 spare: capped at 50, not 51";
    EXPECT_FLOAT_EQ(970.0f, capped.mp) << "1020 spare: capped at 50, not 51";

    // 3000 after the cost: 150 uncapped, held to 50.
    const Shot rich = shoot(cost + 3000.0f);
    ASSERT_TRUE(rich.fired);
    EXPECT_FLOAT_EQ(base.damage + 50.0f, rich.damage) << "3000 spare: capped at 50, not 150";
    EXPECT_FLOAT_EQ(2950.0f, rich.mp) << "3000 spare: capped at 50, not 150";
}
