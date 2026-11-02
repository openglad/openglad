/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch families-1: the rules the review found in living-00-soldier.lua, living-01-elf.lua, living-02-archer.lua, living-03-mage.lua.
// Each test is named after the rule it asserts.
//
// Every scene is an open grass field (the GameplayFixesBots fixture,
// bot_gate_fixture.h) with bodies placed by hand in pixels. A cast goes
// through walker::special, the door player input and the AI use, so the
// slot's price is charged exactly as in play; a bot gate is asked through
// og::test::check_special_ai; a struck body through statistics::hit_response.
// Where the rule is a classic one (the setting off, or no setting at all)
// the old game's function was read at 7ff1654a and does the same.

#include <gtest/gtest.h>

#include "bot_gate_fixture.h"
#include "../test_family_hook_dispatch.h"
#include "../test_sim_random_scope.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// Draws the listed values in turn (each taken modulo the bound), then
// repeats the last one.
class SequenceRandom : public IRandom
{
public:
    explicit SequenceRandom(std::vector<std::uint32_t> values) : values_(std::move(values)) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        const std::uint32_t v = values_[at_ < values_.size() ? at_ : values_.size() - 1];
        ++at_;
        return v % max_exclusive;
    }

private:
    std::vector<std::uint32_t> values_;
    std::size_t at_ = 0;
};

struct Cmd
{
    std::int32_t type;
    std::int32_t count;
    std::int32_t com1;
    std::int32_t com2;
    bool forced;
};

std::vector<Cmd> commands_of(walker* w)
{
    std::vector<Cmd> out;
    for (const command& c : w->stats()->commands)
        out.push_back({c.commandtype, c.commandcount, c.com1, c.com2, c.forced});
    return out;
}

class SemFamilies1 : public og::test::bot_gates::GameplayFixesBots
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

    int all_sounds()
    {
        int n = 0;
        for (const og::sim::Event& e : events().events())
            if (e.kind == og::sim::EventKind::PlaySound)
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

    // The cast as play makes it: walker::special, the slot's price charged
    // on success.
    bool cast(walker* w, int slot, short fixes, std::string* reason)
    {
        world().gameplay_fixes = fixes;
        w->set_current_special(static_cast<char>(slot));
        walker::SpecialFailure why = walker::SpecialFailure::None;
        return w->special(&why, reason);
    }

    // The live entities of `family` in the world's weapon list.
    std::vector<walker*> weapons_of(int family)
    {
        std::vector<walker*> out;
        for (const auto& ob : world().weaplist)
            if (ob && ob->dead() == 0 && ob->query_order() == Order::Weapon &&
                ob->family() == static_cast<char>(family))
                out.push_back(ob.get());
        return out;
    }

    // Live markers, and those of them `owner` owns.
    int live_markers(walker* owner = nullptr)
    {
        int n = 0;
        for (const GameWorld::EntityList* list : {&world().oblist, &world().fxlist, &world().weaplist})
            for (const auto& ob : *list)
                if (ob && ob->dead() == 0 && ob->query_order() == Order::FX &&
                    ob->family() == static_cast<char>(FAMILY_MARKER) &&
                    (owner == nullptr || ob->owner() == owner))
                    ++n;
        return n;
    }

    static void make_hero(walker* w, int family, short intelligence = 0)
    {
        w->set_owned_myguy(std::make_unique<guy>(static_cast<char>(family)));
        if (intelligence != 0)
            w->myguy->intelligence = intelligence;
    }
};

class SemSoldier : public SemFamilies1 {};
class SemElf : public SemFamilies1 {};
class SemArcher : public SemFamilies1 {};
class SemMage : public SemFamilies1 {};

}  // namespace

// ===========================================================================
// The soldier (packs/core/families/living-00-soldier.lua)
// ===========================================================================

// living-00-soldier.lua:11. A charge with nothing in front queues one rush
// and plays SOUND_CHARGE, once, on both values of the setting.
TEST_F(SemSoldier, a_clear_charge_queues_its_rush_and_plays_the_charge_sound)
{
    for (const short fixes : {short{0}, short{1}})
    {
        fresh_world();
        living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 500.0f), 16, 16);
        ASSERT_NE(nullptr, soldier);
        soldier->set_stepsize(4.0f);
        soldier->set_lastx(4.0f);
        soldier->set_lasty(0.0f);
        soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
        events().clear();

        std::string reason;
        EXPECT_TRUE(cast(soldier, 1, fixes, &reason)) << "clear arm, fixes=" << fixes << ": " << reason;
        EXPECT_FLOAT_EQ(475.0f, soldier->stats()->magicpoints()) << "clear arm: CHARGE's 25 MP, fixes=" << fixes;
        EXPECT_EQ(1, sounds(SOUND_CHARGE)) << "clear arm: SOUND_CHARGE once, fixes=" << fixes;
        EXPECT_EQ(1, all_sounds()) << "clear arm: and no other sound, fixes=" << fixes;
        const std::vector<Cmd> q = commands_of(soldier);
        ASSERT_EQ(1u, q.size()) << "clear arm: one rush queued, fixes=" << fixes;
        EXPECT_EQ(COMMAND_RUSH, q[0].type);
        EXPECT_EQ(3, q[0].count);
        EXPECT_EQ(1, q[0].com1) << "trunc(lastx / stepsize)";
        EXPECT_EQ(0, q[0].com2) << "trunc(lasty / stepsize)";
    }
}

// living-00-soldier.lua:43. A soldier still busy (busy > 0) is refused
// WHIRLWIND, for free, on both values: the reason, the pool, the queue and
// the foe in the ring are all left as they were.
TEST_F(SemSoldier, a_busy_soldier_is_refused_whirlwind_for_free)
{
    for (const short fixes : {short{0}, short{1}})
    {
        fresh_world();
        living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 500.0f), 16, 16);
        living* foe = body(add(FAMILY_TOWER1, 2, 230, 200, 1), 25, 29);  // inside the 40 px ring
        ASSERT_TRUE(soldier && foe);
        soldier->set_busy(2.0f);
        events().clear();

        std::string reason;
        EXPECT_FALSE(cast(soldier, 3, fixes, &reason)) << "busy > 0 arm, fixes=" << fixes;
        EXPECT_EQ("SPECIAL BUSY", reason) << "busy > 0 arm, fixes=" << fixes;
        EXPECT_FLOAT_EQ(500.0f, soldier->stats()->magicpoints()) << "busy > 0: nothing spent, fixes=" << fixes;
        EXPECT_FLOAT_EQ(2.0f, soldier->busy()) << "busy > 0: no spin busy added, fixes=" << fixes;
        EXPECT_EQ(0u, commands_of(soldier).size()) << "busy > 0: no WALK queued, fixes=" << fixes;
        EXPECT_FLOAT_EQ(400.0f, foe->stats()->hitpoints()) << "busy > 0: the foe is not struck, fixes=" << fixes;
        EXPECT_EQ(0u, commands_of(foe).size()) << "busy > 0: the foe is not shoved, fixes=" << fixes;
    }
}

// living-00-soldier.lua:43. With the setting on a busy resting below zero
// (a hero's fractional fire delay) is not busy: the spin is cast, adds its 8
// busy to the rest and queues its 8 walk orders.
TEST_F(SemSoldier, a_whirlwind_after_a_shot_spins_with_the_setting_on)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 500.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 230, 200, 1), 25, 29);
    ASSERT_TRUE(soldier && foe);
    soldier->set_busy(-0.25f);

    FixedRandom draws(0);
    ScopedSimRandom scoped(&draws);
    std::string reason;
    EXPECT_TRUE(cast(soldier, 3, 1, &reason)) << "busy < 0, setting on: " << reason;
    EXPECT_FLOAT_EQ(7.75f, soldier->busy()) << "busy < 0, setting on: the rest plus 8";
    EXPECT_FLOAT_EQ(380.0f, soldier->stats()->magicpoints()) << "WHIRLWIND's 120 MP";
    const std::vector<Cmd> q = commands_of(soldier);
    ASSERT_EQ(8u, q.size()) << "busy < 0, setting on: the 8 walk orders";
    for (const Cmd& c : q)
        EXPECT_EQ(COMMAND_WALK, c.type) << "busy < 0, setting on: every order is a WALK";
}

// living-00-soldier.lua:49. The classic WHIRLWIND with nobody in its ring
// still spins: the soldier loses its heading (curdir -1, lastx = lasty = 0)
// and walks one step to each compass point, clockwise from north.
TEST_F(SemSoldier, a_classic_whirlwind_with_no_foe_spins_through_the_eight_compass_points)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 500.0f), 16, 16);
    ASSERT_NE(nullptr, soldier);
    soldier->set_lastx(4.0f);
    soldier->set_lasty(0.0f);
    soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));

    std::string reason;
    ASSERT_TRUE(cast(soldier, 3, 0, &reason)) << "setting off, no foe: the classic spin: " << reason;
    EXPECT_EQ(-1, soldier->curdir()) << "spin shape: no heading";
    EXPECT_FLOAT_EQ(0.0f, soldier->lastx()) << "spin shape: lastx reset";
    EXPECT_FLOAT_EQ(0.0f, soldier->lasty()) << "spin shape: lasty reset";

    const std::vector<std::pair<int, int>> compass = {
        {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
    const std::vector<Cmd> q = commands_of(soldier);
    ASSERT_EQ(compass.size(), q.size()) << "spin shape: eight orders";
    for (std::size_t i = 0; i < q.size(); ++i)
    {
        EXPECT_EQ(COMMAND_WALK, q[i].type) << "order " << i;
        EXPECT_EQ(1, q[i].count) << "order " << i;
        EXPECT_EQ(compass[i].first, q[i].com1) << "spin shape: order " << i << " dx";
        EXPECT_EQ(compass[i].second, q[i].com2) << "spin shape: order " << i << " dy";
        EXPECT_FALSE(q[i].forced) << "order " << i;
    }
}

// living-00-soldier.lua:67. Every foe inside the ring (32 + 2 px a level, 40
// at level 4) takes exactly one blow, the one attack() deals, and is then
// forced to walk 8 steps straight away from the soldier; a foe outside the
// ring is neither struck nor shoved.
TEST_F(SemSoldier, whirlwind_strikes_and_shoves_every_foe_in_its_ring_and_nobody_outside)
{
    FixedRandom draws(0);
    ScopedSimRandom scoped(&draws);

    // One attack()'s blow on a tower, measured on its own.
    float blow = 0.0f;
    {
        living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 500.0f), 16, 16);
        living* tower = body(add(FAMILY_TOWER1, 2, 230, 200, 1), 25, 29);
        ASSERT_TRUE(soldier && tower);
        soldier->attack(tower);
        blow = 400.0f - tower->stats()->hitpoints();
        ASSERT_NE(0.0f, blow) << "precondition: attack() hurts a tower";
    }

    fresh_world();
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 500.0f), 16, 16);
    living* east = body(add(FAMILY_TOWER1, 2, 230, 200, 1), 25, 29);   // 30 px east
    living* north = body(add(FAMILY_TOWER1, 2, 200, 170, 1), 25, 29);  // 30 px north
    living* far = body(add(FAMILY_TOWER1, 2, 260, 200, 1), 25, 29);    // 60 px: outside
    ASSERT_TRUE(soldier && east && north && far);

    std::string reason;
    ASSERT_TRUE(cast(soldier, 3, 0, &reason)) << reason;

    EXPECT_FLOAT_EQ(400.0f - blow, east->stats()->hitpoints()) << "two foes: the east one struck once";
    EXPECT_FLOAT_EQ(400.0f - blow, north->stats()->hitpoints()) << "two foes: the north one struck once";
    EXPECT_FLOAT_EQ(400.0f, far->stats()->hitpoints()) << "foe outside the ring: not struck";
    EXPECT_EQ(0u, commands_of(far).size()) << "foe outside the ring: not shoved";

    const std::vector<Cmd> e = commands_of(east);
    ASSERT_EQ(1u, e.size()) << "two foes: the east one shoved once";
    EXPECT_EQ(COMMAND_WALK, e[0].type);
    EXPECT_EQ(8, e[0].count);
    EXPECT_EQ(1, e[0].com1) << "away from the soldier: east";
    EXPECT_EQ(0, e[0].com2);
    EXPECT_TRUE(e[0].forced);

    const std::vector<Cmd> n = commands_of(north);
    ASSERT_EQ(1u, n.size()) << "two foes: the north one shoved once";
    EXPECT_EQ(COMMAND_WALK, n[0].type);
    EXPECT_EQ(8, n[0].count);
    EXPECT_EQ(0, n[0].com1);
    EXPECT_EQ(-1, n[0].com2) << "away from the soldier: north";
    EXPECT_TRUE(n[0].forced);
}

// living-00-soldier.lua:79. A busy soldier is refused DISARM for free on
// both values: reason, pool, the foe's busy and its own busy unchanged.
TEST_F(SemSoldier, a_busy_soldier_is_refused_disarm_for_free)
{
    for (const short fixes : {short{0}, short{1}})
    {
        for (const float busy : {3.0f, 0.0f})
        {
            fresh_world();
            living* soldier = settle_body(body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 500.0f), 16, 16));
            living* foe = settle_body(body(add(FAMILY_TOWER1, 2, 212, 200, 1), 25, 29));
            ASSERT_TRUE(soldier && foe);
            soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
            soldier->set_lastx(4.0f);
            soldier->set_busy(busy);
            ASSERT_TRUE(soldier->stats()->forward_blocked(true)) << "precondition: the foe is in front";

            SequenceRandom draws({0});
            ScopedSimRandom scoped(&draws);
            std::string reason;
            const bool ok = cast(soldier, 4, fixes, &reason);
            if (busy > 0.0f)
            {
                EXPECT_FALSE(ok) << "busy > 0 arm, fixes=" << fixes;
                EXPECT_EQ("SPECIAL BUSY", reason) << "busy > 0 arm, fixes=" << fixes;
                EXPECT_FLOAT_EQ(500.0f, soldier->stats()->magicpoints()) << "busy > 0: free, fixes=" << fixes;
                EXPECT_FLOAT_EQ(0.0f, foe->busy()) << "busy > 0: the foe is not staggered, fixes=" << fixes;
                EXPECT_FLOAT_EQ(3.0f, soldier->busy()) << "busy > 0: no recovery added, fixes=" << fixes;
            }
            else
            {
                // The control: the same scene not busy disarms (6 * (4 - 1 + 1)).
                EXPECT_TRUE(ok) << "not busy, fixes=" << fixes << ": " << reason;
                EXPECT_FLOAT_EQ(24.0f, foe->busy()) << "not busy, fixes=" << fixes;
            }
        }
    }
}

// living-00-soldier.lua:117 (the classic arm). With the setting off a DISARM
// whose every foe resisted still succeeds: it charges the full 150 MP, adds
// the soldier's 5 busy and plays SOUND_CHARGE.
TEST_F(SemSoldier, a_resisted_classic_disarm_still_pays_its_recovery_and_plays_the_charge_sound)
{
    living* soldier = settle_body(body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 500.0f), 16, 16));
    living* foe = settle_body(body(add(FAMILY_TOWER1, 2, 212, 200, 4), 25, 29));
    ASSERT_TRUE(soldier && foe);
    soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
    soldier->set_lastx(4.0f);
    events().clear();

    // og.rand(4) draws 0 for the soldier and 3 for the foe: the foe resists.
    SequenceRandom draws({0, 3});
    ScopedSimRandom scoped(&draws);
    std::string reason;
    EXPECT_TRUE(cast(soldier, 4, 0, &reason)) << "all resisted, setting off: " << reason;
    EXPECT_FLOAT_EQ(0.0f, foe->busy()) << "the foe resisted";
    EXPECT_FLOAT_EQ(350.0f, soldier->stats()->magicpoints()) << "the full 150 MP";
    EXPECT_FLOAT_EQ(5.0f, soldier->busy()) << "all resisted, setting off: the 5 busy recovery";
    EXPECT_EQ(1, sounds(SOUND_CHARGE)) << "all resisted, setting off: SOUND_CHARGE";
    EXPECT_EQ(1, all_sounds()) << "and only that sound";
}

// living-00-soldier.lua:127. "Fighter Disarmed Enemy!" is for the player's
// team or a hero: an enemy bot's disarm lands as silently as it is cast.
TEST_F(SemSoldier, only_the_player_team_or_a_hero_is_told_of_a_disarm)
{
    for (const short fixes : {short{0}, short{1}})
    {
        for (const unsigned char team : {static_cast<unsigned char>(1), static_cast<unsigned char>(0)})
        {
            fresh_world();
            living* soldier = settle_body(body(add(FAMILY_SOLDIER, team, 200, 200, 4, 500.0f), 16, 16));
            living* foe = settle_body(body(add(FAMILY_TOWER1, 2, 212, 200, 1), 25, 29));
            ASSERT_TRUE(soldier && foe);
            soldier->set_curdir(static_cast<signed char>(FACE_RIGHT));
            soldier->set_lastx(4.0f);
            events().clear();

            SequenceRandom draws({0});
            ScopedSimRandom scoped(&draws);
            std::string reason;
            EXPECT_TRUE(cast(soldier, 4, fixes, &reason)) << "team " << int(team) << " fixes=" << fixes << ": " << reason;
            EXPECT_FLOAT_EQ(24.0f, foe->busy()) << "the disarm lands either way";
            EXPECT_FLOAT_EQ(5.0f, soldier->busy()) << "and costs the usual recovery";
            if (team == 1)
                EXPECT_EQ(0, notices("Fighter Disarmed Enemy!")) << "enemy bot arm: no notice, fixes=" << fixes;
            else
                EXPECT_EQ(1, notices("Fighter Disarmed Enemy!")) << "team 0 arm: one notice, fixes=" << fixes;
        }
    }
}

// living-00-soldier.lua:136. The thrown-blade limit is a living soldier's:
// the hook lets any other body of the family fire, its weapons_left
// untouched, the weapon alive and the pool unrefunded. (walker::fire asks
// the hook for living bodies only, so this arm is reached by dispatching the
// hook directly, as the sim would for any caller.)
TEST_F(SemSoldier, the_blade_limit_binds_only_a_living_soldier)
{
    const FamilyDescriptor& desc = *get_family_descriptor(FAMILY_SOLDIER);

    walker* fx = world().add_ob(Order::FX, FAMILY_SOLDIER);
    walker* knife = world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(fx && knife);
    ASSERT_NE(Order::Living, fx->query_order());
    fx->set_weapons_left(0);
    fx->stats()->set_magicpoints(100.0f);
    EXPECT_TRUE(og::test::on_fire_weapon(desc, fx, knife)) << "non-living arm: fire allowed";
    EXPECT_EQ(0, fx->weapons_left()) << "non-living arm: weapons_left untouched";
    EXPECT_EQ(0, knife->dead()) << "non-living arm: the weapon is not taken back";
    EXPECT_FLOAT_EQ(100.0f, fx->stats()->magicpoints()) << "non-living arm: nothing refunded";

    // The control: a living soldier with no blade left is refused.
    living* soldier = add(FAMILY_SOLDIER, 1, 200, 200, 4, 100.0f);
    walker* knife2 = world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_TRUE(soldier && knife2);
    soldier->set_weapons_left(0);
    EXPECT_FALSE(og::test::on_fire_weapon(desc, soldier, knife2)) << "living arm: the limit binds";
    EXPECT_EQ(1, knife2->dead());
}

// living-00-soldier.lua:154. A living soldier is created with (level+1)//2
// blades; any other body of the family keeps the weapons_left it had.
TEST_F(SemSoldier, on_create_arms_only_a_living_soldier_with_blades)
{
    const FamilyDescriptor& desc = *get_family_descriptor(FAMILY_SOLDIER);
    for (const auto& [level, blades] : std::vector<std::pair<int, int>>{{1, 1}, {2, 1}, {5, 3}, {6, 3}})
    {
        living* soldier = add(FAMILY_SOLDIER, 1, 200, 200, level);
        ASSERT_NE(nullptr, soldier);
        soldier->set_weapons_left(7);
        og::test::on_create(desc, soldier);
        EXPECT_EQ(blades, soldier->weapons_left()) << "living arm: level " << level;
    }

    walker* fx = world().add_ob(Order::FX, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fx);
    fx->stats()->set_level(5);
    fx->set_weapons_left(7);
    og::test::on_create(desc, fx);
    EXPECT_EQ(7, fx->weapons_left()) << "other order arm: weapons_left untouched";
}

// living-00-soldier.lua:259. The classic window's foe search runs first on
// every slot whatever the setting: a bot with no foe acquires the one 50 px
// away, though with the setting on the slot's own gate then says no (the
// level-4 BOOMERANG orbit is 41 px).
TEST_F(SemSoldier, the_soldier_bot_gate_acquires_its_foe_on_both_values)
{
    living* soldier = body(add(FAMILY_SOLDIER, 1, 200, 200, 4, 300.0f), 16, 16);
    living* foe = body(add(FAMILY_TOWER1, 2, 250, 200, 1), 25, 29);
    ASSERT_TRUE(soldier && foe);

    ASSERT_EQ(nullptr, soldier->foe());
    EXPECT_FALSE(gate(soldier, 2, 1)) << "setting on: 50 px is outside the orbit";
    EXPECT_EQ(foe, soldier->foe()) << "setting on: foe acquisition still ran";

    soldier->set_foe(nullptr);
    EXPECT_TRUE(gate(soldier, 2, 0)) << "setting off: inside the classic window";
    EXPECT_EQ(foe, soldier->foe()) << "setting off: the window acquired the foe";
}

// ===========================================================================
// The elf (packs/core/families/living-01-elf.lua)
// ===========================================================================

// living-01-elf.lua:19 and :54. With the setting on a volley whose pad is on
// a wall is refused before the refund and the swing: the elf does not even
// take the swing's recovery.
TEST_F(SemElf, a_volley_at_a_wall_leaves_the_elf_rested_with_the_setting_on)
{
    for (const int slot : {1, 2, 3, 4})
    {
        fresh_world();
        living* elf = settle_body(body(add(FAMILY_ELF, 1, 192, 192, 4, 600.0f), 16, 16));
        ASSERT_NE(nullptr, elf);
        elf->set_lastx(1.0f);
        elf->set_lasty(0.0f);
        elf->set_busy(0.0f);
        pad_wall(world(), 13, 12);
        ASSERT_EQ(walker::PadAnswer::Terrain, elf->fire_pad_answer()) << "precondition: the pad is on the wall";

        std::string reason;
        EXPECT_FALSE(cast(elf, slot, 1, &reason)) << "slot " << slot;
        EXPECT_EQ("COULD NOT FIRE", reason) << "slot " << slot;
        EXPECT_FLOAT_EQ(0.0f, elf->busy()) << "pad on a wall, setting on: busy unchanged, slot " << slot;
        EXPECT_FLOAT_EQ(600.0f, elf->stats()->magicpoints()) << "slot " << slot;
        EXPECT_EQ(0u, weapons_of(FAMILY_ROCK).size()) << "slot " << slot;
    }
}

// living-01-elf.lua:65. Every rock of a bouncing volley bounces and sees
// los_num / 2 times as far as a plain rock: x1.5, x2 and x2.5 for slots 2,
// 3 and 4.
TEST_F(SemElf, bouncing_volleys_throw_bouncing_rocks_with_the_slots_longer_sight)
{
    std::int32_t plain_sight = 0;
    {
        living* elf = body(add(FAMILY_ELF, 1, 200, 200, 4, 2000.0f), 16, 16);
        ASSERT_NE(nullptr, elf);
        elf->set_lastx(4.0f);
        std::string reason;
        ASSERT_TRUE(cast(elf, 1, 0, &reason)) << reason;
        const std::vector<walker*> rocks = weapons_of(FAMILY_ROCK);
        ASSERT_EQ(2u, rocks.size());
        plain_sight = rocks[0]->lineofsight();
        EXPECT_EQ(plain_sight, rocks[1]->lineofsight());
        EXPECT_EQ(0, dynamic_cast<weap*>(rocks[0])->do_bounce()) << "a plain rock does not bounce";
        ASSERT_NE(0, plain_sight);
    }

    struct Volley { int slot; std::size_t count; std::int32_t los_num; };
    for (const Volley v : {Volley{2, 2, 3}, Volley{3, 3, 4}, Volley{4, 4, 5}})
    {
        fresh_world();
        living* elf = body(add(FAMILY_ELF, 1, 200, 200, 4, 2000.0f), 16, 16);
        ASSERT_NE(nullptr, elf);
        elf->set_lastx(4.0f);
        std::string reason;
        ASSERT_TRUE(cast(elf, v.slot, 0, &reason)) << "slot " << v.slot << ": " << reason;
        const std::vector<walker*> rocks = weapons_of(FAMILY_ROCK);
        ASSERT_EQ(v.count, rocks.size()) << "slot " << v.slot;
        for (walker* rock : rocks)
        {
            weap* w = dynamic_cast<weap*>(rock);
            ASSERT_NE(nullptr, w);
            EXPECT_EQ(1, w->do_bounce()) << "rock shape: bounces, slot " << v.slot;
            EXPECT_EQ(plain_sight * v.los_num / 2, rock->lineofsight())
                << "rock shape: sight x" << v.los_num << "/2, slot " << v.slot;
        }
    }
}

// living-01-elf.lua:67. A volley that runs dry mid-flight (the i-th rock
// fails fire()'s own mana check) keeps the earlier rocks flying; with the
// setting on it takes back only the count - i + 1 rocks that never flew.
// The cast is dispatched directly (the slot's price would refuse it first in
// play), from a pool below zero so the refund pays for the first rocks only.
TEST_F(SemElf, a_volley_that_runs_dry_takes_back_only_the_rocks_that_never_flew)
{
    const FamilyDescriptor& desc = *get_family_descriptor(FAMILY_ELF);
    struct Case { int slot; short fixes; float pool_wc; std::size_t flew; float end_wc; };
    // slot 3 on: refund 3, rock 1 flies, rock 2 fails: take back 2.
    // slot 4 on: refund 4, rock 1 flies, rock 2 fails: take back 3.
    // slot 3 off: refund 4, rocks 1 and 2 fly, rock 3 fails: nothing taken back.
    for (const Case c : {Case{3, 1, -2.0f, 1, -2.0f}, Case{4, 1, -3.0f, 1, -3.0f}, Case{3, 0, -2.0f, 2, 0.0f}})
    {
        fresh_world();
        living* elf = body(add(FAMILY_ELF, 1, 200, 200, 4, 0.0f), 16, 16);
        ASSERT_NE(nullptr, elf);
        elf->set_lastx(4.0f);
        const float wc = static_cast<float>(elf->stats()->weapon_cost());
        ASSERT_NE(0.0f, wc);
        elf->stats()->set_magicpoints(c.pool_wc * wc);
        elf->set_current_special(static_cast<char>(c.slot));
        world().gameplay_fixes = c.fixes;

        const auto r = og::script::hooks::do_special(&desc, elf);
        ASSERT_TRUE(r.has_value());
        EXPECT_FALSE(r->succeeded()) << "slot " << c.slot << " fixes=" << c.fixes;
        EXPECT_EQ("COULD NOT FIRE", r->reason());
        EXPECT_EQ(c.flew, weapons_of(FAMILY_ROCK).size()) << "the earlier rocks fly, slot " << c.slot << " fixes=" << c.fixes;
        EXPECT_FLOAT_EQ(c.end_wc * wc, elf->stats()->magicpoints())
            << "later fire nil: take back count - i + 1, slot " << c.slot << " fixes=" << c.fixes;
    }
}

// living-01-elf.lua:97. With the setting on a bot elf with no living foe in
// reach never throws, whatever the slot: a dead foe and an ally in the
// rocks' flight are not foes.
TEST_F(SemElf, the_elf_bot_gate_with_no_living_foe_never_throws)
{
    living* elf = body(add(FAMILY_ELF, 1, 200, 200, 4, 600.0f), 16, 16);
    ASSERT_NE(nullptr, elf);
    elf->set_lastx(1.0f);
    elf->set_lasty(0.0f);
    for (const int slot : {1, 2, 3, 4})
        EXPECT_FALSE(gate(elf, slot, 1)) << "no living foe, alone: slot " << slot;

    living* ally = body(add(FAMILY_TOWER1, 1, 246, 200, 1), 25, 29);
    living* dead = body(add(FAMILY_TOWER1, 2, 246, 230, 1), 25, 29);
    ASSERT_TRUE(ally && dead);
    dead->set_dead(1);
    for (const int slot : {1, 2, 3, 4})
        EXPECT_FALSE(gate(elf, slot, 1)) << "no living foe, an ally and a corpse in flight: slot " << slot;

    // The control: one living foe faced in flight and ROCKS throws.
    living* foe = body(add(FAMILY_TOWER1, 2, 246, 170, 1), 25, 29);
    ASSERT_NE(nullptr, foe);
    foe->setxy(246, 200);
    ally->setxy(400, 400);
    EXPECT_TRUE(gate(elf, 1, 1)) << "a living foe faced in flight";
}

// living-01-elf.lua:122. A slot outside 1..4 asks for MEGA ROCKS' shape (two
// foes, the x2.5 flight), as the cast's default_cast throws MEGA ROCKS.
TEST_F(SemElf, an_unmapped_elf_slot_aims_like_mega_rocks)
{
    living* elf = body(add(FAMILY_ELF, 1, 200, 200, 1, 100.0f), 16, 16);
    living* near_foe = body(add(FAMILY_TOWER1, 2, 246, 200, 1), 25, 29);  // a 30 px gap east
    living* behind = body(add(FAMILY_TOWER1, 2, 27, 200, 1), 25, 29);     // a 148 px gap west
    ASSERT_TRUE(elf && near_foe && behind);
    elf->set_lastx(1.0f);

    EXPECT_FALSE(gate(elf, 3, 1)) << "LOTS OF ROCKS' x2 flight misses the foe behind";
    EXPECT_TRUE(gate(elf, 4, 1)) << "MEGA ROCKS' x2.5 flight reaches it";
    for (const int slot : {0, 5})
        EXPECT_TRUE(gate(elf, slot, 1)) << "slot outside 1..4: MEGA ROCKS' shape, slot " << slot;
    behind->setxy(26, 200);
    for (const int slot : {0, 5})
        EXPECT_FALSE(gate(elf, slot, 1)) << "slot outside 1..4: a pixel past MEGA's flight, slot " << slot;
}

// ===========================================================================
// The archer (packs/core/families/living-02-archer.lua)
// ===========================================================================

// living-02-archer.lua:29. BARRAGE refunds three weapon costs and fires three
// arrows at a weapon cost each: it costs exactly the slot's 60 MP, from rest
// (busy 0) on both values and, with the setting on, from a busy resting
// below zero, adding 2 x fire_frequency busy.
TEST_F(SemArcher, a_barrage_costs_exactly_the_slots_price)
{
    struct Case { short fixes; float busy; };
    for (const Case c : {Case{0, 0.0f}, Case{1, 0.0f}, Case{1, -0.25f}})
    {
        fresh_world();
        living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 4, 500.0f), 16, 16);
        ASSERT_NE(nullptr, archer);
        archer->set_lasty(-1.0f);
        archer->set_busy(c.busy);
        const float ff = archer->fire_frequency();

        std::string reason;
        EXPECT_TRUE(cast(archer, 2, c.fixes, &reason)) << "fixes=" << c.fixes << " busy " << c.busy << ": " << reason;
        EXPECT_FLOAT_EQ(440.0f, archer->stats()->magicpoints())
            << "exact MP: -cost(2) + 3wc - three shots, fixes=" << c.fixes << " busy " << c.busy;
        EXPECT_EQ(3u, weapons_of(FAMILY_ARROW).size()) << "fixes=" << c.fixes << " busy " << c.busy;
        EXPECT_FLOAT_EQ(c.busy + ff * 2.0f, archer->busy()) << "fixes=" << c.fixes << " busy " << c.busy;
    }
}

// living-02-archer.lua:51. EXPLODING BOLT is one fire arrow marked to explode
// (skip_exit 5000) with 500 hp and twice a plain fire arrow's damage; the
// archer gets its own bow back whether or not the bolt flew.
TEST_F(SemArcher, an_exploding_bolt_is_a_buffed_fire_arrow_and_the_archer_keeps_its_bow)
{
    float plain_damage = 0.0f;
    {
        living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 4, 500.0f), 16, 16);
        ASSERT_NE(nullptr, archer);
        archer->set_lasty(-1.0f);
        archer->set_current_weapon(static_cast<unsigned short>(FAMILY_FIRE_ARROW));
        walker* plain = archer->fire();
        ASSERT_NE(nullptr, plain);
        plain_damage = plain->damage();
        ASSERT_NE(0.0f, plain_damage);
    }

    for (const short fixes : {short{0}, short{1}})
    {
        fresh_world();
        living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 4, 500.0f), 16, 16);
        ASSERT_NE(nullptr, archer);
        archer->set_lasty(-1.0f);
        const unsigned short bow = archer->current_weapon();
        ASSERT_EQ(static_cast<unsigned short>(FAMILY_ARROW), bow);

        std::string reason;
        ASSERT_TRUE(cast(archer, 3, fixes, &reason)) << "fixes=" << fixes << ": " << reason;
        const std::vector<walker*> bolts = weapons_of(FAMILY_FIRE_ARROW);
        ASSERT_EQ(1u, bolts.size()) << "fixes=" << fixes;
        EXPECT_EQ(5000, bolts[0]->skip_exit()) << "fire ok: marked to explode";
        EXPECT_FLOAT_EQ(500.0f, bolts[0]->stats()->hitpoints()) << "fire ok: arrow buffs: hp 500, fixes=" << fixes;
        EXPECT_FLOAT_EQ(2.0f * plain_damage, bolts[0]->damage()) << "fire ok: arrow buffs: damage x2, fixes=" << fixes;
        EXPECT_EQ(bow, archer->current_weapon()) << "fire ok: weapon restored, fixes=" << fixes;
    }

    // The bolt cannot fly: a wall under the fire arrow's wider pad.
    for (const short fixes : {short{0}, short{1}})
    {
        fresh_world();
        living* archer = settle_body(body(add(FAMILY_ARCHER, 1, 192, 184, 7, 300.0f), 16, 16));
        ASSERT_NE(nullptr, archer);
        pad_wall(world(), 12, 10);
        ASSERT_EQ(walker::PadAnswer::Terrain, archer->fire_pad_answer(FAMILY_FIRE_ARROW))
            << "precondition: the fire arrow's pad is on the wall";
        const unsigned short bow = archer->current_weapon();

        std::string reason;
        EXPECT_FALSE(cast(archer, 3, fixes, &reason)) << "fire nil, fixes=" << fixes;
        EXPECT_EQ("COULD NOT FIRE", reason) << "fixes=" << fixes;
        EXPECT_EQ(0u, weapons_of(FAMILY_FIRE_ARROW).size()) << "fixes=" << fixes;
        EXPECT_EQ(bow, archer->current_weapon()) << "fire nil: weapon restored, fixes=" << fixes;
    }
}

// living-02-archer.lua:67. A new attacker makes the struck archer drop its
// orders and its pursuit distances; a repeat hit from its current foe beyond
// the backpedal range changes nothing.
TEST_F(SemArcher, a_new_attacker_clears_the_archers_orders_and_a_repeat_hit_keeps_them)
{
    living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 4), 16, 16);
    living* orc = body(add(FAMILY_ORC, 2, 300, 200, 1), 16, 16);  // 100 px: past the 64 px backpedal
    ASSERT_TRUE(archer && orc);
    archer->stats()->add_command(COMMAND_WALK, 1, 1, 0);
    archer->stats()->set_current_distance(40);
    archer->stats()->set_last_distance(50);

    archer->stats()->hit_response(orc);
    EXPECT_EQ(orc, archer->foe()) << "new attacker: retargeted";
    EXPECT_EQ(0u, commands_of(archer).size()) << "new attacker: queue cleared";
    EXPECT_EQ(15000, archer->stats()->current_distance());
    EXPECT_EQ(15000u, archer->stats()->last_distance());

    archer->stats()->add_command(COMMAND_WALK, 1, 0, 1);
    archer->stats()->set_current_distance(40);
    archer->stats()->set_last_distance(50);
    archer->stats()->hit_response(orc);
    EXPECT_EQ(orc, archer->foe()) << "same foe: still the foe";
    const std::vector<Cmd> q = commands_of(archer);
    ASSERT_EQ(1u, q.size()) << "same foe: commands kept";
    EXPECT_EQ(COMMAND_WALK, q[0].type);
    EXPECT_EQ(0, q[0].com1);
    EXPECT_EQ(1, q[0].com2);
    EXPECT_EQ(40, archer->stats()->current_distance()) << "same foe: distances kept";
    EXPECT_EQ(50u, archer->stats()->last_distance()) << "same foe: distances kept";
}

// living-02-archer.lua:101. With the setting on FIRE ARROWS' ring with no
// living foe anywhere never fires; neither does the classic gate.
TEST_F(SemArcher, the_archer_ring_gate_with_no_foe_never_fires)
{
    living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 1, 100.0f), 16, 16);
    ASSERT_NE(nullptr, archer);
    EXPECT_FALSE(gate(archer, 1, 1)) << "none arm: setting on";
    EXPECT_FALSE(gate(archer, 1, 0)) << "none: the classic answer";
    EXPECT_EQ(nullptr, archer->foe());

    // The control: one foe behind the archer and the ring pays.
    living* behind = body(add(FAMILY_TOWER1, 2, 195, 276, 1), 25, 29);
    ASSERT_NE(nullptr, behind);
    EXPECT_TRUE(gate(archer, 1, 1)) << "one foe behind the archer";
}

// living-02-archer.lua:109. With the setting on BARRAGE and EXPLODING BOLT
// want the archer's own foe alive: with no foe, or a dead one, they hold.
TEST_F(SemArcher, the_archer_line_gates_want_a_living_current_foe)
{
    living* archer = body(add(FAMILY_ARCHER, 1, 200, 200, 4, 200.0f), 16, 16);
    living* foe = body(add(FAMILY_SOLDIER, 2, 200, 84, 1), 16, 16);  // a 100 px gap above
    ASSERT_TRUE(archer && foe);
    archer->set_lasty(-1.0f);
    archer->set_foe(foe);
    EXPECT_TRUE(gate(archer, 2, 1)) << "the control: faced, in flight";

    foe->set_dead(1);
    archer->set_foe(foe);
    EXPECT_FALSE(gate(archer, 2, 1)) << "dead foe arm: BARRAGE";
    EXPECT_FALSE(gate(archer, 3, 1)) << "dead foe arm: EXPLODING BOLT";

    archer->set_foe(nullptr);
    EXPECT_FALSE(gate(archer, 2, 1)) << "no foe arm: BARRAGE";
    EXPECT_EQ(nullptr, archer->foe()) << "nobody else to acquire";
    EXPECT_FALSE(gate(archer, 3, 1)) << "no foe arm: EXPLODING BOLT";
}

// ===========================================================================
// The mage (packs/core/families/living-03-mage.lua)
// ===========================================================================

// living-03-mage.lua:94. A struck bot mage holds its ground down to 3/8 of
// its hp ("enemy mages are braver"); a hero teleports below 3/5.
TEST_F(SemMage, a_bot_mage_flees_only_below_three_eighths_and_a_hero_below_three_fifths)
{
    struct Case { bool hero; float hp; bool flees; };
    for (const Case c : {Case{false, 200.0f, false}, Case{false, 140.0f, true}, Case{true, 200.0f, true},
                         Case{true, 250.0f, false}})
    {
        fresh_world();
        living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 3, 100.0f), 16, 16);
        living* orc = body(add(FAMILY_ORC, 0, 217, 200, 1), 16, 16);
        ASSERT_TRUE(mage && orc);
        if (c.hero)
            make_hero(mage, FAMILY_MAGE);
        mage->stats()->set_hitpoints(c.hp);

        mage->stats()->hit_response(orc);
        const char* arm = c.hero ? "hero" : "bot";
        if (c.flees)
        {
            EXPECT_EQ(ANI_TELE_OUT, mage->ani_type()) << arm << " at " << c.hp << "/400: teleport arm";
            EXPECT_FLOAT_EQ(85.0f, mage->stats()->magicpoints()) << arm << ": TELEPORT's 15 MP";
            EXPECT_EQ(nullptr, mage->foe()) << arm << ": no retarget";
        }
        else
        {
            EXPECT_NE(ANI_TELE_OUT, mage->ani_type()) << arm << " at " << c.hp << "/400: no teleport";
            EXPECT_FLOAT_EQ(100.0f, mage->stats()->magicpoints()) << arm << ": nothing spent";
            EXPECT_EQ(orc, mage->foe()) << arm << ": retarget arm";
            EXPECT_EQ(mage, orc->foe()) << arm << ": the attacker targets the mage";
        }
    }
}

// living-03-mage.lua:105. A struck mage that cannot pay TELEPORT's 15 MP does
// not escape even far below its threshold: it retargets instead.
TEST_F(SemMage, a_struck_mage_that_cannot_pay_for_teleport_retargets_instead)
{
    for (const auto& [mp, flees] : std::vector<std::pair<float, bool>>{{10.0f, false}, {15.0f, true}})
    {
        fresh_world();
        living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 3, mp), 16, 16);
        living* orc = body(add(FAMILY_ORC, 0, 217, 200, 1), 16, 16);
        ASSERT_TRUE(mage && orc);
        ASSERT_EQ(15, mage->stats()->special_cost(1));
        mage->stats()->set_hitpoints(100.0f);  // 1/4: under the bot's 3/8

        mage->stats()->hit_response(orc);
        if (!flees)
        {
            EXPECT_NE(ANI_TELE_OUT, mage->ani_type()) << "MP < 15 arm: no teleport";
            EXPECT_FLOAT_EQ(10.0f, mage->stats()->magicpoints()) << "MP < 15 arm: nothing spent";
            EXPECT_EQ(orc, mage->foe()) << "MP < 15 arm: retarget";
            EXPECT_EQ(mage, orc->foe()) << "MP < 15 arm: the attacker targets the mage";
        }
        else
        {
            EXPECT_EQ(ANI_TELE_OUT, mage->ani_type()) << "MP 15: the teleport is affordable";
            EXPECT_FLOAT_EQ(0.0f, mage->stats()->magicpoints());
        }
    }
}

// living-03-mage.lua:112 and :121. Only a NEW attacker retargets a struck
// mage: a repeat hit from its current foe leaves the foe links and the
// pursuit distances alone.
TEST_F(SemMage, a_repeat_hit_from_the_same_foe_leaves_the_mages_pursuit_alone)
{
    living* mage = body(add(FAMILY_MAGE, 1, 140, 100, 3, 100.0f), 16, 16);
    living* orc = body(add(FAMILY_ORC, 0, 160, 100, 1), 16, 16);
    ASSERT_TRUE(mage && orc);
    make_hero(mage, FAMILY_MAGE);  // full hp: far above the threshold

    mage->stats()->hit_response(orc);
    ASSERT_EQ(orc, mage->foe()) << "new attacker: retargeted";
    ASSERT_EQ(mage, orc->foe());
    ASSERT_EQ(15000, mage->stats()->current_distance());

    mage->stats()->set_current_distance(40);
    mage->stats()->set_last_distance(50);
    orc->set_foe(nullptr);
    mage->stats()->hit_response(orc);
    EXPECT_EQ(orc, mage->foe()) << "same foe: the foe stays";
    EXPECT_EQ(nullptr, orc->foe()) << "same foe: the attacker is not retargeted";
    EXPECT_EQ(40, mage->stats()->current_distance()) << "same foe: no change";
    EXPECT_EQ(50u, mage->stats()->last_distance()) << "same foe: no change";
    EXPECT_NE(ANI_TELE_OUT, mage->ani_type());
    EXPECT_FLOAT_EQ(100.0f, mage->stats()->magicpoints());
}

// living-03-mage.lua:139. A mage mid-teleport (fading out or in) is refused
// TELEPORT and TELEPORT MARKER alike, for free and with no marker made.
TEST_F(SemMage, a_mage_mid_teleport_cannot_teleport_again)
{
    for (const char ani : {static_cast<char>(ANI_TELE_OUT), static_cast<char>(ANI_TELE_IN)})
    {
        for (const short shifter : {short{0}, short{1}})
        {
            fresh_world();
            living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 4, 100.0f), 16, 16);
            ASSERT_NE(nullptr, mage);
            mage->set_shifter_down(shifter);
            mage->set_ani_type(ani);
            events().clear();

            std::string reason;
            EXPECT_FALSE(cast(mage, 1, 0, &reason)) << "mid-teleport arm: ani " << int(ani) << " shifter " << shifter;
            EXPECT_EQ("SPECIAL BUSY", reason) << "ani " << int(ani) << " shifter " << shifter;
            EXPECT_FLOAT_EQ(100.0f, mage->stats()->magicpoints()) << "mid-teleport: MP unchanged";
            EXPECT_EQ(ani, mage->ani_type()) << "mid-teleport: the animation runs on";
            EXPECT_EQ(0, live_markers()) << "mid-teleport: no marker";
            EXPECT_EQ(0, all_sounds()) << "mid-teleport: no sound";
        }
    }
}

// living-03-mage.lua:142. An unshifted TELEPORT plays SOUND_TELEPORT and
// starts the fade-out from its first frame.
TEST_F(SemMage, an_unshifted_teleport_plays_the_teleport_sound_and_starts_the_fade_out)
{
    living* mage = body(add(FAMILY_MAGE, 1, 200, 200, 4, 100.0f), 16, 16);
    ASSERT_NE(nullptr, mage);
    mage->set_shifter_down(0);
    mage->set_cycle(3);
    events().clear();

    std::string reason;
    ASSERT_TRUE(cast(mage, 1, 0, &reason)) << reason;
    EXPECT_EQ(1, sounds(SOUND_TELEPORT)) << "unshifted arm: SOUND_TELEPORT";
    EXPECT_EQ(1, all_sounds()) << "unshifted arm: one sound";
    EXPECT_EQ(ANI_TELE_OUT, mage->ani_type()) << "unshifted arm: the fade-out";
    EXPECT_EQ(0, mage->cycle()) << "unshifted arm: from its first frame";
    EXPECT_EQ(0, live_markers()) << "unshifted: no marker";
}

// living-03-mage.lua:149. A busy mage cannot lay a TELEPORT MARKER: refused
// for free, no marker, its busy left as it was.
TEST_F(SemMage, a_busy_mage_cannot_lay_a_marker)
{
    living* mage = body(add(FAMILY_MAGE, 0, 200, 200, 4, 600.0f), 16, 16);
    ASSERT_NE(nullptr, mage);
    make_hero(mage, FAMILY_MAGE, 120);
    mage->set_shifter_down(1);
    mage->set_busy(3.0f);

    std::string reason;
    EXPECT_FALSE(cast(mage, 1, 0, &reason)) << "busy > 0 arm";
    EXPECT_EQ("SPECIAL BUSY", reason);
    EXPECT_EQ(0, live_markers()) << "busy > 0: no marker";
    EXPECT_FLOAT_EQ(600.0f, mage->stats()->magicpoints()) << "busy > 0: free";
    EXPECT_FLOAT_EQ(3.0f, mage->busy()) << "busy > 0: busy unchanged";

    mage->set_busy(0.0f);
    EXPECT_TRUE(cast(mage, 1, 0, &reason)) << "the control: not busy, the marker is laid: " << reason;
    EXPECT_EQ(1, live_markers(mage));
}

// living-03-mage.lua:158. Laying a marker retires only the caster's own live
// marker: with none of its own the cast adds its plain 8 busy, and another
// mage's marker, the caster's dead marker and its other effects are left
// alone.
TEST_F(SemMage, laying_a_first_marker_leaves_every_other_mark_alone)
{
    living* mage = body(add(FAMILY_MAGE, 0, 200, 200, 4, 600.0f), 16, 16);
    living* other = body(add(FAMILY_MAGE, 0, 400, 400, 4, 600.0f), 16, 16);
    ASSERT_TRUE(mage && other);
    make_hero(mage, FAMILY_MAGE, 120);
    mage->set_shifter_down(1);

    walker* others_marker = world().add_ob(Order::FX, FAMILY_MARKER);
    walker* dead_own = world().add_ob(Order::FX, FAMILY_MARKER);
    walker* own_boomerang = world().add_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_TRUE(others_marker && dead_own && own_boomerang);
    others_marker->set_owner(other);
    others_marker->setxy(300, 300);
    dead_own->set_owner(mage);
    dead_own->set_dead(1);
    own_boomerang->set_owner(mage);
    own_boomerang->setxy(220, 200);

    std::string reason;
    ASSERT_TRUE(cast(mage, 1, 0, &reason)) << reason;
    EXPECT_FLOAT_EQ(8.0f, mage->busy()) << "no own marker: busy +8, no removal surcharge";
    EXPECT_EQ(0, others_marker->dead()) << "another mage's marker is untouched";
    EXPECT_EQ(1, dead_own->dead()) << "the dead marker stays as it was";
    EXPECT_EQ(0, own_boomerang->dead()) << "a non-marker effect is untouched";
    EXPECT_EQ(1, live_markers(mage)) << "exactly one live marker of the caster's";
    EXPECT_EQ(2, live_markers()) << "and the other mage's";
    EXPECT_EQ(0, notices("Old Marker Removed")) << "nothing was removed";
}

// living-03-mage.lua:167. "(Old Marker Removed)" is told only to a seated
// caster of the player's team or a hero: an unseated bot (or an unseated
// body of team 0) retires its old marker in silence.
TEST_F(SemMage, an_unseated_mage_retires_its_marker_without_a_notice)
{
    for (const unsigned char team : {static_cast<unsigned char>(1), static_cast<unsigned char>(0)})
    {
        fresh_world();
        living* mage = body(add(FAMILY_MAGE, team, 200, 200, 8, 9000.0f), 16, 16);
        ASSERT_NE(nullptr, mage);
        ASSERT_EQ(-1, mage->user());
        mage->set_shifter_down(1);

        std::string reason;
        ASSERT_TRUE(cast(mage, 1, 0, &reason)) << reason;
        ASSERT_EQ(1, live_markers(mage));
        walker* first = nullptr;
        for (const auto& ob : world().oblist)
            if (ob && ob->dead() == 0 && ob->family() == static_cast<char>(FAMILY_MARKER) &&
                ob->query_order() == Order::FX)
                first = ob.get();
        ASSERT_NE(nullptr, first);
        mage->set_busy(0.0f);
        events().clear();

        ASSERT_TRUE(cast(mage, 1, 0, &reason)) << reason;
        EXPECT_EQ(1, first->dead()) << "the old marker is retired, team " << int(team);
        EXPECT_EQ(1, live_markers(mage));
        EXPECT_FLOAT_EQ(16.0f, mage->busy()) << "removal and placement, team " << int(team);
        EXPECT_EQ(0, notices("Old Marker Removed")) << "unseated arm: no notice, team " << int(team);
    }
}
