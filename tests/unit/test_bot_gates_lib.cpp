/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The bot gates' library (packs/core/lib/gates.lua) predicate by predicate.
//
// Every bot gate is built from these predicates, so each one is asked here
// on bodies placed by hand, with the answer worked out from the engine's own
// rules: the finders (game_world.cpp), the fired weapon's range
// (walker::configure_weapon_profile_base), facing() (walker_movement.cpp),
// the explosion's reach (effect_bomb.lua) and collide() (obmap.cpp).
//
// A probe chunk replaces the soldier's do_special with a Lua body that asks
// the library and writes each answer with og.log as "<key>\t<value>"; the
// probe also checks that the library drew nothing from the world's random
// stream, the rule every gate stands on.

#include <gtest/gtest.h>

#include "bot_gate_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

using og::test::bot_gates::GameplayFixesBots;
using Answers = std::map<std::string, std::string>;

// The probe's prelude: the library, og.C, and at(x, y), the living, effect
// or generator standing with its top-left corner at (x, y).
constexpr const char* kPrelude =
    "local G = og.use('core:gates')\n"
    "local C = og.C\n"
    "local function at(x, y)\n"
    "  local obs = og.oblist()\n"
    "  for i = 1, #obs do\n"
    "    local w = obs[i]\n"
    "    if w and w:xpos() == x and w:ypos() == y then\n"
    "      return w\n"
    "    end\n"
    "  end\n"
    "  return nil\n"
    "end\n"
    "local function where(w)\n"
    "  if w == nil then\n"
    "    return 'nil'\n"
    "  end\n"
    "  return w:xpos() .. ',' .. w:ypos()\n"
    "end\n";

class BotGatesLib : public GameplayFixesBots {
protected:
    // A level-1 soldier on team 1, 16 by 16, with its top-left at (x, y):
    // the `self` of every probe.
    living* caster(short x = 200, short y = 200, int level = 1)
    {
        return body(add(FAMILY_SOLDIER, 1, x, y, level), 16, 16);
    }

    living* fighter(unsigned char team, short x, short y)
    {
        return body(add(FAMILY_SOLDIER, team, x, y, 1), 16, 16);
    }

    walker* tent(unsigned char team, short x, short y)
    {
        walker* w = world().add_ob(Order::Generator, FAMILY_TENT);
        if (w != nullptr) {
            w->set_team_num(team);
            w->setxy(x, y);
        }
        return body(w, 16, 16);
    }

    // Runs `lua` as core:soldier's do_special for `self` and returns what
    // it logged after the probe's own marker, keyed by the first field.
    Answers probe(walker* self, const std::string& lua, bool draw_free = true)
    {
        og::script::register_pack_script(
            {"test.bot_gates", "bot_gates_probe.lua",
             std::string(kPrelude) +
                 "og.register_hooks('living', 'core:soldier', {\n"
                 "  do_special = function(self)\n"
                 "    og.log('<probe>')\n" +
                 lua +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
        const std::uint32_t before = world().rng_.state_;
        const auto handled = og::script::hooks::do_special(
            get_family_descriptor(FAMILY_SOLDIER), self);
        EXPECT_TRUE(handled.has_value()) << last_error();
        if (draw_free) {
            EXPECT_EQ(before, world().rng_.state_)
                << "the library draws nothing from the world's stream";
        }
        Answers out;
        const auto& log = world().scripts().host().log();
        std::size_t start = log.size();
        while (start > 0 && log[start - 1] != "<probe>")
            --start;
        for (std::size_t i = start; i < log.size(); ++i) {
            const std::string& line = log[i];
            const auto tab = line.find('\t');
            if (tab == std::string::npos)
                out[line] = "";
            else
                out[line.substr(0, tab)] = line.substr(tab + 1);
        }
        return out;
    }

    std::string last_error()
    {
        const auto& errors = world().scripts().host().errors();
        return errors.empty() ? std::string("(no script error recorded)")
                              : errors.back().message;
    }
};

// What walker::configure_weapon_profile_base and act_fire make of a weapon
// of table step / los fired by a caster of `level` (and a hero's strength
// and dexterity), per axis: walker.cpp adds strength / 23 + dexterity / 31
// for a hero, then level / 3; on a straight heading the range is scaled by
// 309 / 256 in integers and the step by 362 / 256 in floats; the first walk
// only turns the bolt, so range - 1 steps move.
int engine_reach(int step, int los, int level, bool straight, int strength = 0,
                 int dexterity = 0)
{
    int range = los + strength / 23 + dexterity / 31 + level / 3;
    float px = static_cast<float>(step);
    if (straight) {
        range = range * 309 / 256;
        px = (px * 362.0f) / 256.0f;
    }
    return static_cast<int>(static_cast<float>(range - 1) * px);
}

}  // namespace

// The finder counts living bodies and generators alike; the library's
// living_foes keeps the living ones, and on request only those on the
// caster's floor. The range is the finder's: Manhattan between top-left
// corners, inclusive.
//
// RED (living_foes without the order test): n80 is 3, not 2.
TEST_F(BotGatesLib, living_foes_skips_generators)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 240, 200));        // 40 px
    ASSERT_NE(nullptr, tent(0, 200, 240));           // 40 px, a generator
    living* upstairs = fighter(0, 260, 200);         // 60 px, floor 1
    ASSERT_NE(nullptr, upstairs);
    upstairs->set_floor(1);
    ASSERT_NE(nullptr, fighter(0, 300, 200));        // 100 px
    const Answers a = probe(self,
        "    local _, finder = og.find_foes_in_range('ob', 80, self)\n"
        "    og.log('finder80', finder)\n"
        "    og.log('n80', #G.living_foes(self, 80, false))\n"
        "    og.log('n80floor', #G.living_foes(self, 80, true))\n"
        "    og.log('n100', #G.living_foes(self, 100, false))\n"
        "    og.log('n99', #G.living_foes(self, 99, false))\n"
        "    og.log('living_fighter', G.living(at(240, 200)))\n"
        "    og.log('living_tent', G.living(at(200, 240)))\n");
    EXPECT_EQ("3", a.at("finder80")) << "the finder counts the tent";
    EXPECT_EQ("2", a.at("n80")) << "the tent is never a living foe";
    EXPECT_EQ("1", a.at("n80floor")) << "the foe upstairs is on floor 1";
    EXPECT_EQ("3", a.at("n100")) << "100 px is within 100 (inclusive)";
    EXPECT_EQ("2", a.at("n99"));
    EXPECT_EQ("true", a.at("living_fighter"));
    EXPECT_EQ("false", a.at("living_tent"));
}

// targets_within counts what the finder counts, generators included (fix
// K's casts strike a tent); foes_within counts living foes only.
//
// RED (targets_within through living_foes): t2_40 is false.
TEST_F(BotGatesLib, targets_within_counts_a_generator)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 240, 200));  // 40 px
    ASSERT_NE(nullptr, tent(0, 200, 240));     // 40 px
    const Answers a = probe(self,
        "    og.log('t2_40', G.targets_within(self, 2, 40))\n"
        "    og.log('t3_40', G.targets_within(self, 3, 40))\n"
        "    og.log('t2_39', G.targets_within(self, 2, 39))\n"
        "    og.log('f1_40', G.foes_within(self, 1, 40, false))\n"
        "    og.log('f2_40', G.foes_within(self, 2, 40, false))\n");
    EXPECT_EQ("true", a.at("t2_40"));
    EXPECT_EQ("false", a.at("t3_40"));
    EXPECT_EQ("false", a.at("t2_39"));
    EXPECT_EQ("true", a.at("f1_40"));
    EXPECT_EQ("false", a.at("f2_40")) << "the tent is not a living foe";
}

// The nearest living foe by the finder's distance; a tie goes to the one
// the list holds first; a nearer generator is passed over; and the caster's
// foe is left alone (acquiring is the classic gate's business).
//
// RED (`<=` in the compare): nearest is 200,240, the second of the tie.
TEST_F(BotGatesLib, nearest_living_foe_ties_to_list_order)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 240, 200));  // 40 px, first in the list
    ASSERT_NE(nullptr, fighter(0, 200, 240));  // 40 px, second
    ASSERT_NE(nullptr, tent(0, 210, 200));     // 10 px, a generator
    ASSERT_NE(nullptr, fighter(0, 260, 200));  // 60 px
    const Answers a = probe(self,
        "    og.log('nearest', where(G.nearest_living_foe(self, 100, false)))\n"
        "    og.log('none', where(G.nearest_living_foe(self, 39, false)))\n"
        "    og.log('foe', where(self:foe()))\n");
    EXPECT_EQ("240,200", a.at("nearest"));
    EXPECT_EQ("nil", a.at("none"));
    EXPECT_EQ("nil", a.at("foe")) << "the library never assigns a foe";
}

// gap(a, b): the edge-to-edge gap on each axis, negative where the boxes
// overlap. self is 16 x 16 at (200, 200), so its far edges are 216.
TEST_F(BotGatesLib, gap_is_negative_where_boxes_overlap)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 210, 205));
    ASSERT_NE(nullptr, fighter(0, 221, 300));
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 0, 150, 150, 1), 25, 29));
    const Answers a = probe(self,
        "    local x, y = G.gap(self, at(210, 205))\n"
        "    og.log('overlap', x .. ',' .. y)\n"
        "    x, y = G.gap(self, at(221, 300))\n"
        "    og.log('apart', x .. ',' .. y)\n"
        "    x, y = G.gap(self, at(150, 150))\n"
        "    og.log('tower', x .. ',' .. y)\n"
        "    x, y = G.gap(at(150, 150), self)\n"
        "    og.log('tower_rev', x .. ',' .. y)\n");
    // 210 - 216 = -6 and 205 - 216 = -11 (the other sides are farther in).
    EXPECT_EQ("-6,-11", a.at("overlap"));
    // 221 - 216 = 5; 300 - 216 = 84.
    EXPECT_EQ("5,84", a.at("apart"));
    // A 25 x 29 body at (150, 150) ends at 175 / 179: 200 - 175, 200 - 179.
    EXPECT_EQ("25,21", a.at("tower"));
    EXPECT_EQ("25,21", a.at("tower_rev")) << "the gap is symmetric";
}

// Within a blow: both gaps at most 5. foes_at_blows keeps the living foes
// at blows with the caster (a friend at blows is not one).
//
// RED (at_blows with `< BLOW_GAP`): gap5 is false.
TEST_F(BotGatesLib, at_blows_gap_five_in_six_out)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 400, 400));
    ASSERT_NE(nullptr, fighter(0, 421, 400));  // gap 5, -16
    ASSERT_NE(nullptr, fighter(0, 400, 500));
    ASSERT_NE(nullptr, fighter(0, 422, 500));  // gap 6
    ASSERT_NE(nullptr, fighter(0, 600, 600));
    ASSERT_NE(nullptr, fighter(0, 621, 621));  // gap 5, 5
    ASSERT_NE(nullptr, fighter(0, 700, 600));
    ASSERT_NE(nullptr, fighter(0, 721, 622));  // gap 5, 6
    ASSERT_NE(nullptr, fighter(0, 221, 200));  // a foe at blows with self
    ASSERT_NE(nullptr, fighter(0, 200, 222));  // a foe 6 px below self
    ASSERT_NE(nullptr, fighter(1, 179, 200));  // a friend at blows with self
    const Answers a = probe(self,
        "    og.log('gap5', G.at_blows(at(400, 400), at(421, 400)))\n"
        "    og.log('gap6', G.at_blows(at(400, 500), at(422, 500)))\n"
        "    og.log('diag5', G.at_blows(at(600, 600), at(621, 621)))\n"
        "    og.log('diag56', G.at_blows(at(700, 600), at(721, 622)))\n"
        "    local mine = G.foes_at_blows(self)\n"
        "    og.log('mine', #mine)\n"
        "    og.log('mine1', where(mine[1]))\n");
    EXPECT_EQ("true", a.at("gap5"));
    EXPECT_EQ("false", a.at("gap6"));
    EXPECT_EQ("true", a.at("diag5"));
    EXPECT_EQ("false", a.at("diag56"));
    EXPECT_EQ("1", a.at("mine"));
    EXPECT_EQ("221,200", a.at("mine1"));
}

// The allies' fight: foes at blows with an ally other than the caster,
// counted foe by foe (a foe at blows with two allies counts once), and
// whether such an ally is under a quarter of its health. A foe at blows
// with the caster, and a generator at blows with an ally, are not the
// allies' fight.
//
// RED (ally-major counting): engaged is 3, not 2.
TEST_F(BotGatesLib, allies_fight_counts_a_foe_once)
{
    living* self = caster(100, 100);
    living* a1 = fighter(1, 300, 300);
    ASSERT_NE(nullptr, a1);
    ASSERT_NE(nullptr, fighter(1, 342, 300));
    ASSERT_NE(nullptr, fighter(0, 321, 300));  // gap 5 to both allies
    ASSERT_NE(nullptr, fighter(0, 363, 300));  // gap 5 to the second ally
    ASSERT_NE(nullptr, fighter(0, 117, 100));  // at blows with the caster
    ASSERT_NE(nullptr, tent(0, 300, 321));     // at blows with the first ally
    const std::string lua =
        "    local engaged, hurt = G.allies_fight(self)\n"
        "    og.log('engaged', engaged)\n"
        "    og.log('hurt', hurt)\n";
    Answers a = probe(self, lua);
    EXPECT_EQ("2", a.at("engaged"));
    EXPECT_EQ("false", a.at("hurt"));

    a1->stats()->set_hitpoints(100.0f);  // 100 * 4 is not under 400
    a = probe(self, lua);
    EXPECT_EQ("false", a.at("hurt"));
    a1->stats()->set_hitpoints(99.0f);   // 396 < 400
    a = probe(self, lua);
    EXPECT_EQ("true", a.at("hurt"));
    EXPECT_EQ("2", a.at("engaged"));
}

// The fire heading is facing(lastx, lasty): UP at spawn (lastx = lasty = 0
// while curdir is DOWN), whatever curdir later says. lastx and lasty are
// floats the binding truncates toward zero.
//
// RED (fire_heading reading curdir): spawn is 4, not 0.
TEST_F(BotGatesLib, fire_heading_is_up_at_spawn_and_follows_lastx_lasty)
{
    living* self = caster();
    const std::string lua =
        "    og.log('curdir', self:curdir())\n"
        "    og.log('heading', G.fire_heading(self))\n";
    Answers a = probe(self, lua);
    EXPECT_EQ(std::to_string(FACE_DOWN), a.at("curdir"));
    EXPECT_EQ(std::to_string(FACE_UP), a.at("heading"));

    self->set_lastx(1.0f);
    a = probe(self, lua);
    EXPECT_EQ(std::to_string(FACE_RIGHT), a.at("heading"));

    self->set_curdir(FACE_LEFT);  // the SEARCH snap turns curdir only
    a = probe(self, lua);
    EXPECT_EQ(std::to_string(FACE_LEFT), a.at("curdir"));
    EXPECT_EQ(std::to_string(FACE_RIGHT), a.at("heading"));

    self->set_lastx(-0.5f);  // truncates to 0: facing(0, 0) is UP
    a = probe(self, lua);
    EXPECT_EQ(std::to_string(FACE_UP), a.at("heading"));

    self->set_lastx(3.0f);
    self->set_lasty(3.0f);
    a = probe(self, lua);
    EXPECT_EQ(std::to_string(FACE_DOWN_RIGHT), a.at("heading"));

    self->set_lastx(-3.0f);
    a = probe(self, lua);
    EXPECT_EQ(std::to_string(FACE_DOWN_LEFT), a.at("heading"));
}

// has_heading: a rush or a walk along (lastx, lasty) goes somewhere.
//
// RED (has_heading through the fire heading): spawn is true.
TEST_F(BotGatesLib, has_heading_is_false_at_spawn)
{
    living* self = caster();
    const std::string lua = "    og.log('has', G.has_heading(self))\n";
    EXPECT_EQ("false", probe(self, lua).at("has"));
    self->set_lasty(-1.0f);
    EXPECT_EQ("true", probe(self, lua).at("has"));
    self->set_lasty(0.0f);
    self->set_lastx(0.25f);
    EXPECT_EQ("true", probe(self, lua).at("has"))
        << "a fraction of a pixel is still a heading";
}

// flight_px against the engine for the rock (5, 8), the fire arrow (8, 12),
// the fireball (6, 7) and the meteor (7, 9) at levels 1, 7 and 13, on a
// straight and a diagonal heading. The literal columns were worked by
// hand; engine_reach() re-derives each from walker.cpp's formula.
//
// RED (the first, turning walk counted as a move): rock_s is 63, not 56.
TEST_F(BotGatesLib, flight_px_matches_the_engine)
{
    struct Weapon {
        const char* name;
        int step;
        int los;
        int straight[3];  // levels 1, 7, 13
        int diagonal[3];
    };
    // level 1, straight: rock 8 -> 9 range, 8 moves of 7.0703 = 56.
    const Weapon weapons[] = {
        {"rock", 5, 8, {56, 77, 91}, {35, 45, 55}},
        {"fire_arrow", 8, 12, {147, 169, 203}, {88, 104, 120}},
        {"fireball", 6, 7, {59, 76, 101}, {36, 48, 60}},
        {"meteor", 7, 9, {89, 118, 138}, {56, 70, 84}},
    };
    std::string lua;
    for (const Weapon& w : weapons) {
        const std::string args =
            std::to_string(w.step) + ", " + std::to_string(w.los);
        lua += std::string("    og.log('") + w.name + "_s', G.flight_px(self, " +
               args + ", C.FACE_RIGHT))\n";
        lua += std::string("    og.log('") + w.name + "_u', G.flight_px(self, " +
               args + ", C.FACE_UP))\n";
        lua += std::string("    og.log('") + w.name + "_d', G.flight_px(self, " +
               args + ", C.FACE_UP_RIGHT))\n";
        lua += std::string("    og.log('") + w.name + "_dl', G.flight_px(self, " +
               args + ", C.FACE_DOWN_LEFT))\n";
    }
    const int levels[] = {1, 7, 13};
    for (int li = 0; li < 3; ++li) {
        fresh_world();
        living* self = caster(200, 200, levels[li]);
        ASSERT_NE(nullptr, self);
        const Answers a = probe(self, lua);
        for (const Weapon& w : weapons) {
            const std::string n = w.name;
            SCOPED_TRACE(n + " at level " + std::to_string(levels[li]));
            EXPECT_EQ(w.straight[li],
                      engine_reach(w.step, w.los, levels[li], true));
            EXPECT_EQ(w.diagonal[li],
                      engine_reach(w.step, w.los, levels[li], false));
            EXPECT_EQ(std::to_string(w.straight[li]), a.at(n + "_s"));
            EXPECT_EQ(std::to_string(w.straight[li]), a.at(n + "_u"));
            EXPECT_EQ(std::to_string(w.diagonal[li]), a.at(n + "_d"));
            EXPECT_EQ(std::to_string(w.diagonal[li]), a.at(n + "_dl"));
        }
    }

    // A hero: strength 46 / 23 + dexterity 31 / 31 adds 3 to the rock's
    // range at level 1: 11 -> 13 straight, 12 moves of 7.0703 = 84; 11
    // diagonal, 10 moves of 5 = 50. Strength 45 and dexterity 30 add 1.
    fresh_world();
    living* hero = caster();
    ASSERT_NE(nullptr, hero);
    hero->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    hero->myguy->strength = 46;
    hero->myguy->dexterity = 31;
    const std::string rock =
        "    og.log('s', G.flight_px(self, 5, 8, C.FACE_RIGHT))\n"
        "    og.log('d', G.flight_px(self, 5, 8, C.FACE_UP_LEFT))\n";
    Answers a = probe(hero, rock);
    EXPECT_EQ(84, engine_reach(5, 8, 1, true, 46, 31));
    EXPECT_EQ(50, engine_reach(5, 8, 1, false, 46, 31));
    EXPECT_EQ("84", a.at("s"));
    EXPECT_EQ("50", a.at("d"));
    hero->myguy->strength = 45;
    hero->myguy->dexterity = 30;
    a = probe(hero, rock);
    // 9 * 309 / 256 = 10: 9 moves of 7.0703 = 63; diagonal 8 moves of 5.
    EXPECT_EQ(63, engine_reach(5, 8, 1, true, 45, 30));
    EXPECT_EQ("63", a.at("s"));
    EXPECT_EQ("40", a.at("d"));
}

// in_flight: the edge gap on the dominant axis against the reach along the
// heading to the target's centre. A level-1 rock goes 56 px straight and
// 35 px a side on a diagonal.
//
// RED (in_flight measuring corner to corner): right56 is false.
TEST_F(BotGatesLib, in_flight_measures_the_edge_gap)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 272, 200));  // gap 56, right
    ASSERT_NE(nullptr, fighter(0, 251, 251));  // gap 35, 35: down-right
    ASSERT_NE(nullptr, fighter(0, 148, 148));  // gap 36, 36: up-left
    const Answers a = probe(self,
        "    og.log('right56', G.in_flight(self, at(272, 200), 5, 8))\n"
        "    og.log('diag35', G.in_flight(self, at(251, 251), 5, 8))\n"
        "    og.log('diag36', G.in_flight(self, at(148, 148), 5, 8))\n");
    EXPECT_EQ("true", a.at("right56"));
    EXPECT_EQ("true", a.at("diag35"));
    EXPECT_EQ("false", a.at("diag36"));

    // The same foe one pixel farther on the straight heading.
    fresh_world();
    living* again = caster();
    ASSERT_NE(nullptr, fighter(0, 273, 200));  // gap 57
    const Answers b = probe(again,
        "    og.log('right57', G.in_flight(self, at(273, 200), 5, 8))\n");
    EXPECT_EQ("false", b.at("right57"));
}

// The elf's volleys multiply the fired rock's range by los_num / 2 after
// the straight scaling (living-01-elf.lua); the mage's WARP SPACE adds the
// spare pool's steps to a ring whose bolts move one pixel a tick.
//
// RED (the multiplier applied before the 309 / 256 scaling): elf_s is 91.
TEST_F(BotGatesLib, in_flight_takes_the_elf_multiplier_and_the_mage_add)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 300, 200));  // gap 84 right
    Answers a = probe(self,
        "    og.log('elf_s', G.flight_px(self, 5, 8, C.FACE_RIGHT, 3, 2))\n"
        "    og.log('elf_d', G.flight_px(self, 5, 8, C.FACE_UP_RIGHT, 3, 2))\n"
        "    og.log('elf84', G.in_flight(self, at(300, 200), 5, 8, 3, 2))\n"
        "    og.log('rock84', G.in_flight(self, at(300, 200), 5, 8))\n");
    // 9 straight steps * 3 // 2 = 13: 12 moves of 7.0703 = 84; diagonal
    // 8 * 3 // 2 = 12: 11 moves of 5 = 55.
    EXPECT_EQ("84", a.at("elf_s"));
    EXPECT_EQ("55", a.at("elf_d"));
    EXPECT_EQ("true", a.at("elf84"));
    EXPECT_EQ("false", a.at("rock84"));

    // Level 7: 7 + 2 = 9 -> 10 straight, plus 12 is 22, 21 moves of one
    // pixel; diagonal 9 + 12 = 21, 20 moves.
    fresh_world();
    living* mage = caster(200, 200, 7);
    ASSERT_NE(nullptr, fighter(0, 237, 200));  // gap 21 right
    ASSERT_NE(nullptr, fighter(0, 200, 239));  // gap 23 below
    a = probe(mage,
        "    og.log('ring_s', G.flight_px(self, 1, 7, C.FACE_RIGHT, nil, nil, 12))\n"
        "    og.log('ring_d', G.flight_px(self, 1, 7, C.FACE_DOWN_LEFT, nil, nil, 12))\n"
        "    og.log('ring21', G.in_flight(self, at(237, 200), 1, 7, nil, nil, 12))\n"
        "    og.log('ring23', G.in_flight(self, at(200, 239), 1, 7, nil, nil, 12))\n");
    EXPECT_EQ("21", a.at("ring_s"));
    EXPECT_EQ("20", a.at("ring_d"));
    EXPECT_EQ("true", a.at("ring21"));
    EXPECT_EQ("false", a.at("ring23"));
}

// A ring pays with two living foes in its flight, or one the caster does
// not face. The fire arrow (8, 12) at level 1: 147 px straight, 88 on a
// diagonal; the spawn heading is UP.
//
// RED (ring_pays without the unfaced arm): below is false.
TEST_F(BotGatesLib, ring_pays_two_in_or_one_unfaced)
{
    const std::string lua = "    og.log('pays', G.ring_pays(self, 8, 12))\n";

    living* self = caster(400, 400);
    ASSERT_NE(nullptr, fighter(0, 400, 324));  // gap 60 above: faced
    EXPECT_EQ("false", probe(self, lua).at("pays")) << "one faced foe";

    fresh_world();
    self = caster(400, 400);
    ASSERT_NE(nullptr, fighter(0, 400, 476));  // gap 60 below
    EXPECT_EQ("true", probe(self, lua).at("pays")) << "one foe behind";

    fresh_world();
    self = caster(400, 400);
    ASSERT_NE(nullptr, fighter(0, 400, 324));  // faced
    ASSERT_NE(nullptr, fighter(0, 324, 400));  // gap 60 left
    EXPECT_EQ("true", probe(self, lua).at("pays")) << "two in the ring";

    fresh_world();
    self = caster(400, 400);
    ASSERT_NE(nullptr, fighter(0, 400, 324));  // faced
    ASSERT_NE(nullptr, fighter(0, 516, 516));  // gap 100 down-right, past 88
    EXPECT_EQ("false", probe(self, lua).at("pays"))
        << "the diagonal foe is out of the ring";

    fresh_world();
    self = caster(400, 400);
    self->set_lasty(1.0f);                     // now facing down
    ASSERT_NE(nullptr, fighter(0, 400, 476));
    EXPECT_EQ("false", probe(self, lua).at("pays"))
        << "the foe below is faced now";

    // The point-blank ring (step 1, los 7, add 12 at level 7: 21 / 20).
    const std::string warp =
        "    og.log('pays', G.ring_pays(self, 1, 7, 12))\n";
    fresh_world();
    self = caster(400, 400, 7);
    ASSERT_NE(nullptr, fighter(0, 400, 431));  // gap 15 below
    EXPECT_EQ("true", probe(self, warp).at("pays"));
    fresh_world();
    self = caster(400, 400, 7);
    ASSERT_NE(nullptr, fighter(0, 400, 441));  // gap 25 below
    EXPECT_EQ("false", probe(self, warp).at("pays"));
}

// The explosion's reach: 15 + clamp(4 * level, 16, 96) (effect_bomb.lua).
TEST_F(BotGatesLib, blast_reach_is_the_explosions)
{
    living* self = caster();
    const Answers a = probe(self,
        "    og.log('l1', G.blast_reach(1))\n"
        "    og.log('l4', G.blast_reach(4))\n"
        "    og.log('l5', G.blast_reach(5))\n"
        "    og.log('l24', G.blast_reach(24))\n"
        "    og.log('l30', G.blast_reach(30))\n");
    EXPECT_EQ("31", a.at("l1"));
    EXPECT_EQ("31", a.at("l4"));
    EXPECT_EQ("35", a.at("l5"));
    EXPECT_EQ("111", a.at("l24"));
    EXPECT_EQ("111", a.at("l30"));
}

// blast_clear asks about the CASTER's allies around the blast's centre (a
// foe), never the foe's own friends; the caster itself counts only with
// check_self.
//
// RED (the friends finder asked about the centre): the foe's own friend
// refuses, clear is false.
TEST_F(BotGatesLib, blast_clear_sees_the_casters_allies_around_a_foe)
{
    const std::string lua =
        "    local foe = at(300, 100)\n"
        "    og.log('clear', G.blast_clear(self, foe, 31, false))\n"
        "    og.log('clear_self', G.blast_clear(self, foe, 31, true))\n"
        "    og.log('on_self', G.blast_clear(self, self, 31, true))\n";

    living* self = caster(100, 100);
    ASSERT_NE(nullptr, fighter(0, 300, 100));
    ASSERT_NE(nullptr, fighter(1, 320, 100));  // the caster's ally, 20 px off
    Answers a = probe(self, lua);
    EXPECT_EQ("false", a.at("clear")) << "an ally of the caster in the blast";

    fresh_world();
    self = caster(100, 100);
    ASSERT_NE(nullptr, fighter(0, 300, 100));
    ASSERT_NE(nullptr, fighter(0, 320, 100));  // the foe's friend
    a = probe(self, lua);
    EXPECT_EQ("true", a.at("clear")) << "the foe's own friend does not refuse";
    EXPECT_EQ("true", a.at("clear_self"));
    EXPECT_EQ("true", a.at("on_self"));

    fresh_world();
    self = caster(100, 100);
    ASSERT_NE(nullptr, fighter(0, 300, 100));
    ASSERT_NE(nullptr, fighter(1, 332, 100));  // 32 px from the foe
    a = probe(self, lua);
    EXPECT_EQ("true", a.at("clear")) << "32 px is outside a 31 px blast";

    fresh_world();
    self = caster(280, 100);                   // 20 px from the foe
    ASSERT_NE(nullptr, fighter(0, 300, 100));
    ASSERT_NE(nullptr, fighter(1, 280, 120));  // 20 px from the caster, 40 from the foe
    a = probe(self, lua);
    EXPECT_EQ("true", a.at("clear")) << "the caster is not asked about";
    EXPECT_EQ("false", a.at("clear_self")) << "the caster stands in the blast";
    EXPECT_EQ("false", a.at("on_self")) << "a blast on the caster reaches the ally";
}

// owned(order, family): the order is tested, because family numbers collide
// across orders (FX boomerang 7 is the faerie's living number); the weapon
// order scans the weapon list (the druid's circle).
//
// RED (owned matching the family alone): fx7 is 2.
TEST_F(BotGatesLib, owned_tests_the_order)
{
    living* self = caster();
    walker* blade = world().add_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, blade);
    blade->setxy(230, 200);
    blade->set_owner(self);
    walker* stray = world().add_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, stray);
    stray->setxy(240, 200);
    stray->set_owner(nullptr);
    walker* spent = world().add_ob(Order::FX, FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, spent);
    spent->setxy(250, 200);
    spent->set_owner(self);
    spent->set_dead(1);
    living* faerie = body(add(FAMILY_FAERIE, 1, 260, 200, 1), 16, 16);
    ASSERT_NE(nullptr, faerie);
    faerie->set_owner(self);
    walker* circle = world().add_weap_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_NE(nullptr, circle);
    circle->setxy(200, 230);
    circle->set_owner(self);
    const std::string boomerang = std::to_string(FAMILY_BOOMERANG);
    const std::string ring = std::to_string(FAMILY_CIRCLE_PROTECTION);
    const Answers a = probe(self,
        "    local n, first = G.owned(self, C.ORDER_FX, " + boomerang + ")\n"
        "    og.log('fx7', n)\n"
        "    og.log('fx7_first', where(first))\n"
        "    n, first = G.owned(self, C.ORDER_LIVING, " + boomerang + ")\n"
        "    og.log('living7', n)\n"
        "    og.log('living7_first', where(first))\n"
        "    n, first = G.owned(self, C.ORDER_WEAPON, " + ring + ")\n"
        "    og.log('circle', n)\n"
        "    og.log('circle_first', where(first))\n"
        "    n = G.owned(self, C.ORDER_FX, " + ring + ")\n"
        "    og.log('fx16', n)\n");
    EXPECT_EQ("1", a.at("fx7")) << "the live blade alone";
    EXPECT_EQ("230,200", a.at("fx7_first"));
    EXPECT_EQ("1", a.at("living7")) << "the faerie, and not the blade";
    EXPECT_EQ("260,200", a.at("living7_first"));
    EXPECT_EQ("1", a.at("circle"));
    EXPECT_EQ("200,230", a.at("circle_first"));
    EXPECT_EQ("0", a.at("fx16"));
}

// A phantom is a living the caster owns whose max_hp is 1 (SUMMON IMAGE).
TEST_F(BotGatesLib, owned_phantom_wants_max_hp_one)
{
    living* self = caster();
    living* summoned = fighter(1, 260, 200);
    ASSERT_NE(nullptr, summoned);
    summoned->set_owner(self);
    living* stranger = fighter(1, 280, 200);
    ASSERT_NE(nullptr, stranger);
    stranger->stats()->set_max_hitpoints(1.0f);
    const std::string lua = "    og.log('phantom', where(G.owned_phantom(self)))\n";
    EXPECT_EQ("nil", probe(self, lua).at("phantom")) << "max_hp 400 is no phantom";
    summoned->stats()->set_max_hitpoints(1.0f);
    EXPECT_EQ("260,200", probe(self, lua).at("phantom"));
}

// corpse_near: the stain find_nearest_blood returns, within a Manhattan
// range. The finder starts at 800 on the squared centre distance, so a
// stain 40 px off (1600) is never returned, whatever the range.
//
// RED (corpse_near scanning the stains itself): far is 240,200.
TEST_F(BotGatesLib, corpse_near_is_capped_by_the_finder)
{
    living* self = caster();
    walker* far = body(world().add_fx_ob(Order::Treasure, FAMILY_STAIN), 16, 16);
    ASSERT_NE(nullptr, far);
    far->setxy(240, 200);
    const std::string lua =
        "    og.log('near100', where(G.corpse_near(self, 100)))\n"
        "    og.log('near10', where(G.corpse_near(self, 10)))\n"
        "    og.log('near11', where(G.corpse_near(self, 11)))\n"
        "    og.log('under100', where(G.corpse_underfoot(self, 100)))\n"
        "    og.log('under99', where(G.corpse_underfoot(self, 99)))\n";
    Answers a = probe(self, lua);
    EXPECT_EQ("nil", a.at("near100")) << "40 px: beyond the finder";

    walker* close = body(world().add_fx_ob(Order::Treasure, FAMILY_STAIN), 16, 16);
    ASSERT_NE(nullptr, close);
    close->setxy(210, 200);  // 10 px: squared centre distance 100
    a = probe(self, lua);
    EXPECT_EQ("210,200", a.at("near100"));
    EXPECT_EQ("nil", a.at("near10")) << "the range is strict";
    EXPECT_EQ("210,200", a.at("near11"));
    EXPECT_EQ("210,200", a.at("under100")) << "the meal's compare is inclusive";
    EXPECT_EQ("nil", a.at("under99"));

    close->set_floor(1);
    a = probe(self, lua);
    EXPECT_EQ("nil", a.at("near100")) << "a stain upstairs";
    EXPECT_EQ("nil", a.at("under100"));
}

// ahead_blocked_by_foe: the caster's 16 x 16 box at (200, 200) moved 1 px
// along curdir (UP) is [200, 214] x [201, 215] once shrunk by collide()'s
// 1 px; a 16 x 16 foe at (x, y) shrinks to [y + 1, y + 15] x [x + 1, x + 15].
// They meet, inclusive, for y >= 185 and 186 <= x <= 214 (asked within
// 40 px: a foe aside at (214, 185) is 29 px off).
//
// RED (the caster's box compared unshrunk): the foe at (215, 185) is found.
TEST_F(BotGatesLib, ahead_blocked_by_foe_mirrors_collide)
{
    struct Case {
        unsigned char team;
        short x, y;
        const char* expect;
        const char* why;
    };
    const Case cases[] = {
        {0, 200, 185, "200,185", "2 px of overlap after the step"},
        {0, 200, 184, "nil", "1 px of overlap after the step"},
        {0, 200, 183, "nil", "a 1 px gap"},
        {0, 214, 185, "214,185", "the last pixel aside that meets"},
        {0, 215, 185, "nil", "one pixel farther aside"},
        {0, 186, 185, "186,185", "the last pixel on the other side"},
        {1, 200, 185, "nil", "a friend in front"},
    };
    const std::string lua =
        "    og.log('ahead', where(G.ahead_blocked_by_foe(self, 40)))\n"
        "    og.log('reach15', where(G.ahead_blocked_by_foe(self, 15)))\n"
        "    og.log('reach14', where(G.ahead_blocked_by_foe(self, 14)))\n";
    for (const Case& c : cases) {
        fresh_world();
        living* self = caster();
        self->set_curdir(FACE_UP);
        ASSERT_NE(nullptr, fighter(c.team, c.x, c.y));
        const Answers a = probe(self, lua);
        EXPECT_EQ(c.expect, a.at("ahead")) << c.why;
        EXPECT_EQ("nil", a.at("reach14")) << "nobody within 14 px";
    }

    // The reach is the finder's Manhattan range: the foe straight ahead at
    // (200, 185) is 15 px off.
    fresh_world();
    {
        living* self = caster();
        self->set_curdir(FACE_UP);
        ASSERT_NE(nullptr, fighter(0, 200, 185));
        const Answers a = probe(self, lua);
        EXPECT_EQ("200,185", a.at("reach15"));
    }

    fresh_world();
    living* self = caster();
    living* foe = fighter(0, 200, 185);
    ASSERT_NE(nullptr, foe);
    self->set_curdir(-1);  // FIRE ARROWS and WHIRLWIND leave curdir at -1
    EXPECT_EQ("nil", probe(self, lua).at("ahead"));
    self->set_curdir(FACE_UP);
    foe->set_floor(1);
    EXPECT_EQ("nil", probe(self, lua).at("ahead")) << "a foe upstairs";
}

// can_pay: the alternate's own price when shifted, the slot's price when
// the alternate has none (alternate_cost answers 0) or is not in play.
// The fire elemental's REKINDLE (slot 4) costs 40 and its SUPERNOVA 10 with
// New Specials on; the mage's TELEPORT MARKER costs TELEPORT's 15.
//
// RED (can_pay passing any pool when alternate_cost is 0): mage14 is true.
TEST_F(BotGatesLib, can_pay_falls_back_to_the_slot_price_when_the_alternate_has_none)
{
    living* self = caster();
    living* elemental = body(add(FAMILY_FIREELEMENTAL, 1, 300, 300, 10, 10.0f), 16, 16);
    ASSERT_NE(nullptr, elemental);
    living* mage = body(add(FAMILY_MAGE, 1, 400, 300, 10, 15.0f), 16, 16);
    ASSERT_NE(nullptr, mage);
    const std::string lua =
        "    og.log('nova', G.can_pay(at(300, 300), 4, true))\n"
        "    og.log('rekindle', G.can_pay(at(300, 300), 4, false))\n"
        "    og.log('mage', G.can_pay(at(400, 300), 1, true))\n";
    world().new_specials = 1;
    Answers a = probe(self, lua);
    EXPECT_EQ("true", a.at("nova")) << "SUPERNOVA's own 10";
    EXPECT_EQ("false", a.at("rekindle")) << "REKINDLE's 40";
    EXPECT_EQ("true", a.at("mage")) << "the marker costs TELEPORT's 15";

    elemental->stats()->set_magicpoints(9.0f);
    mage->stats()->set_magicpoints(14.0f);
    a = probe(self, lua);
    EXPECT_EQ("false", a.at("nova"));
    EXPECT_EQ("false", a.at("mage")) << "14 is short of 15";

    elemental->stats()->set_magicpoints(10.0f);
    world().new_specials = 0;  // SUPERNOVA is not in play: the slot's 40
    a = probe(self, lua);
    EXPECT_EQ("false", a.at("nova"));
}

// TELEPORT MARKER pays once (no marker of the caster's own stands) and only
// funded: half of what is left after the slot's price must still cover
// next_slot's price. A level-10 mage on slot 1 (15) keeping WARP SPACE (60):
// (135 - 15) / 2 = 60 pays, (134 - 15) / 2 = 59 does not. A hero needs the
// INT the cast asks.
//
// RED (marker_pays without the standing-marker test): stands is true.
TEST_F(BotGatesLib, marker_pays_once_and_only_when_funded)
{
    living* self = caster();
    living* mage = body(add(FAMILY_MAGE, 1, 400, 300, 10, 135.0f), 16, 16);
    ASSERT_NE(nullptr, mage);
    mage->set_current_special(1);
    const std::string lua =
        "    local marker = og.family_id('fx', 'core:marker')\n"
        "    og.log('pays', G.marker_pays(at(400, 300), marker, 2, 75))\n";
    EXPECT_EQ("true", probe(self, lua).at("pays"));

    mage->stats()->set_magicpoints(134.0f);
    EXPECT_EQ("false", probe(self, lua).at("pays")) << "59 is short of 60";
    mage->stats()->set_magicpoints(135.0f);

    walker* someone_elses = world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, someone_elses);
    someone_elses->setxy(500, 500);
    someone_elses->set_owner(self);
    EXPECT_EQ("true", probe(self, lua).at("pays")) << "another's marker";

    walker* own = world().add_ob(Order::FX, FAMILY_MARKER);
    ASSERT_NE(nullptr, own);
    own->setxy(520, 500);
    own->set_owner(mage);
    EXPECT_EQ("false", probe(self, lua).at("pays")) << "stands";
    own->set_dead(1);
    EXPECT_EQ("true", probe(self, lua).at("pays")) << "a spent marker";

    mage->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    mage->myguy->intelligence = 74;
    EXPECT_EQ("false", probe(self, lua).at("pays")) << "INT 74";
    mage->myguy->intelligence = 75;
    EXPECT_EQ("true", probe(self, lua).at("pays")) << "INT 75";
}

// resting: the rest clock is on the slot and running.
TEST_F(BotGatesLib, resting_reads_the_slot_and_the_clock)
{
    living* self = caster();
    const std::string lua =
        "    og.log('r2', G.resting(self, 2))\n"
        "    og.log('r1', G.resting(self, 1))\n";
    self->set_special_rest_slot(2);
    self->set_special_rest_left(5);
    Answers a = probe(self, lua);
    EXPECT_EQ("true", a.at("r2"));
    EXPECT_EQ("false", a.at("r1"));
    self->set_special_rest_left(0);
    EXPECT_EQ("false", probe(self, lua).at("r2"));
}

// The friendly side: allies (living friends, not the caster), a hurt ally,
// and an ally at blows with a living foe.
TEST_F(BotGatesLib, allies_hurt_ally_and_ally_at_blows)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(1, 240, 200));  // 40 px
    living* far = fighter(1, 300, 200);        // 100 px
    ASSERT_NE(nullptr, far);
    ASSERT_NE(nullptr, tent(1, 210, 200));     // a friendly generator
    ASSERT_NE(nullptr, fighter(0, 300, 221));  // a foe at blows with `far`
    const std::string lua =
        "    og.log('a40', #G.allies(self, 40))\n"
        "    og.log('w1_40', G.allies_within(self, 1, 40))\n"
        "    og.log('w2_40', G.allies_within(self, 2, 40))\n"
        "    og.log('w2_100', G.allies_within(self, 2, 100))\n"
        "    og.log('hurt', where(G.hurt_ally(self, 100)))\n"
        "    local ally, foe = G.ally_at_blows(self, 100)\n"
        "    og.log('ally', where(ally))\n"
        "    og.log('foe', where(foe))\n"
        "    og.log('ally99', where(G.ally_at_blows(self, 99)))\n";
    Answers a = probe(self, lua);
    EXPECT_EQ("1", a.at("a40"));
    EXPECT_EQ("true", a.at("w1_40"));
    EXPECT_EQ("false", a.at("w2_40"));
    EXPECT_EQ("true", a.at("w2_100"));
    EXPECT_EQ("nil", a.at("hurt")) << "everybody is whole";
    EXPECT_EQ("300,200", a.at("ally"));
    EXPECT_EQ("300,221", a.at("foe"));
    EXPECT_EQ("nil", a.at("ally99")) << "the fighting ally is 100 px off";
    far->stats()->set_hitpoints(399.0f);
    a = probe(self, lua);
    EXPECT_EQ("300,200", a.at("hurt"));
}

// hunters / hunted_by: the living foes in range whose foe is the caster.
TEST_F(BotGatesLib, hunters_are_the_foes_whose_foe_is_the_caster)
{
    living* self = caster();
    living* ally = fighter(1, 240, 240);
    ASSERT_NE(nullptr, ally);
    living* f1 = fighter(0, 230, 230);  // 60 px, hunting the caster
    living* f2 = fighter(0, 260, 200);  // 60 px, hunting the ally
    living* f3 = fighter(0, 200, 250);  // 50 px, hunting the caster
    ASSERT_TRUE(f1 && f2 && f3);
    f1->set_foe(self);
    f2->set_foe(ally);
    f3->set_foe(self);
    const std::string lua =
        "    og.log('n100', #G.hunters(self, 100))\n"
        "    og.log('first', where(G.hunted_by(self, 100)))\n"
        "    og.log('n55', #G.hunters(self, 55))\n"
        "    og.log('first49', where(G.hunted_by(self, 49)))\n";
    const Answers a = probe(self, lua);
    EXPECT_EQ("2", a.at("n100"));
    EXPECT_EQ("230,230", a.at("first")) << "list order";
    EXPECT_EQ("1", a.at("n55"));
    EXPECT_EQ("nil", a.at("first49"));
}

// hp_below compares hp * den with max_hp * num as C++ floats; centre and
// heading_to work from the body's centre.
TEST_F(BotGatesLib, hp_below_centre_and_heading_to)
{
    living* self = caster();
    ASSERT_NE(nullptr, body(add(FAMILY_SOLDIER, 0, 100, 100, 1), 25, 29));
    ASSERT_NE(nullptr, fighter(0, 300, 200));
    ASSERT_NE(nullptr, fighter(0, 200, 100));
    ASSERT_NE(nullptr, fighter(0, 300, 300));
    ASSERT_NE(nullptr, fighter(0, 100, 300));
    const std::string lua =
        "    og.log('quarter', G.hp_below(self, 1, 4))\n"
        "    local x, y = G.centre(self)\n"
        "    og.log('centre', x .. ',' .. y)\n"
        "    x, y = G.centre(at(100, 100))\n"
        "    og.log('tower', x .. ',' .. y)\n"
        "    og.log('right', G.heading_to(self, at(300, 200)))\n"
        "    og.log('up', G.heading_to(self, at(200, 100)))\n"
        "    og.log('downright', G.heading_to(self, at(300, 300)))\n"
        "    og.log('downleft', G.heading_to(self, at(100, 300)))\n";
    self->stats()->set_hitpoints(100.0f);
    Answers a = probe(self, lua);
    EXPECT_EQ("false", a.at("quarter")) << "100 * 4 is not under 400";
    EXPECT_EQ("208,208", a.at("centre"));
    EXPECT_EQ("112,114", a.at("tower")) << "25 / 2 and 29 / 2 in integers";
    EXPECT_EQ(std::to_string(FACE_RIGHT), a.at("right"));
    EXPECT_EQ(std::to_string(FACE_UP), a.at("up"));
    EXPECT_EQ(std::to_string(FACE_DOWN_RIGHT), a.at("downright"));
    EXPECT_EQ(std::to_string(FACE_DOWN_LEFT), a.at("downleft"));
    self->stats()->set_hitpoints(99.9f);
    EXPECT_EQ("true", probe(self, lua).at("quarter"));
}

// faces, aimed_at and fan_faces read the fire heading. aimed_at also wants
// a heading at all: at spawn the caster faces UP but would rush nowhere.
//
// RED (aimed_at without has_heading): spawn_aimed is true.
TEST_F(BotGatesLib, faces_aimed_at_and_fan_faces_read_the_fire_heading)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 200, 100));  // up
    ASSERT_NE(nullptr, fighter(0, 300, 100));  // up-right
    ASSERT_NE(nullptr, fighter(0, 100, 100));  // up-left
    ASSERT_NE(nullptr, fighter(0, 300, 200));  // right
    const std::string lua =
        "    og.log('faces_up', G.faces(self, at(200, 100)))\n"
        "    og.log('faces_right', G.faces(self, at(300, 200)))\n"
        "    og.log('aimed', G.aimed_at(self, at(200, 100)))\n"
        "    og.log('fan_ur', G.fan_faces(self, at(300, 100)))\n"
        "    og.log('fan_ul', G.fan_faces(self, at(100, 100)))\n"
        "    og.log('fan_up', G.fan_faces(self, at(200, 100)))\n"
        "    og.log('fan_right', G.fan_faces(self, at(300, 200)))\n";
    Answers a = probe(self, lua);
    EXPECT_EQ("true", a.at("faces_up")) << "the spawn heading is UP";
    EXPECT_EQ("false", a.at("faces_right"));
    EXPECT_EQ("false", a.at("aimed")) << "no heading: the rush goes nowhere";
    EXPECT_EQ("true", a.at("fan_ur"));
    EXPECT_EQ("true", a.at("fan_ul")) << "UP_LEFT is UP's other neighbour";
    EXPECT_EQ("true", a.at("fan_up"));
    EXPECT_EQ("false", a.at("fan_right"));

    self->set_lasty(-1.0f);
    a = probe(self, lua);
    EXPECT_EQ("true", a.at("aimed"));

    self->set_lastx(-1.0f);  // UP_LEFT: its neighbours are LEFT and UP
    a = probe(self, lua);
    EXPECT_EQ("true", a.at("fan_up")) << "(7 + 1) % 8 is UP";
    EXPECT_EQ("false", a.at("fan_ur"));
    EXPECT_EQ("false", a.at("faces_up"));
}

// fan_covers asks the facing the kit passes (the captain's knife fan and
// the skeleton's bone wall go along curdir first), and fan_faces is
// fan_covers on the fire heading. At spawn curdir is DOWN and the fire
// heading is UP, so the two answer differently for the same foe.
//
// RED (fan_covers reading the fire heading): down_on_down is false.
TEST_F(BotGatesLib, fan_covers_takes_the_facing_the_kit_passes)
{
    living* self = caster();
    ASSERT_NE(nullptr, fighter(0, 200, 300));  // down
    ASSERT_NE(nullptr, fighter(0, 100, 300));  // down-left
    ASSERT_NE(nullptr, fighter(0, 300, 300));  // down-right
    ASSERT_NE(nullptr, fighter(0, 300, 200));  // right
    ASSERT_NE(nullptr, fighter(0, 100, 100));  // up-left
    const std::string lua =
        "    local down = self:curdir()\n"
        "    og.log('down_on_down', G.fan_covers(self, down, at(200, 300)))\n"
        "    og.log('dl_on_down', G.fan_covers(self, down, at(100, 300)))\n"
        "    og.log('dr_on_down', G.fan_covers(self, down, at(300, 300)))\n"
        "    og.log('right_on_down', G.fan_covers(self, down, at(300, 200)))\n"
        "    og.log('fan_faces_down', G.fan_faces(self, at(200, 300)))\n"
        "    og.log('ul_on_up', G.fan_covers(self, C.FACE_UP, at(100, 100)))\n";
    Answers a = probe(self, lua);
    EXPECT_EQ("true", a.at("down_on_down"));
    EXPECT_EQ("true", a.at("dl_on_down"));
    EXPECT_EQ("true", a.at("dr_on_down"));
    EXPECT_EQ("false", a.at("right_on_down"))
        << "RIGHT is two steps from DOWN";
    EXPECT_EQ("false", a.at("fan_faces_down"))
        << "the fire heading is UP at spawn";
    EXPECT_EQ("true", a.at("ul_on_up")) << "(0 + 7) % 8 is UP_LEFT";
}

// with_classic: the classic gate runs first on both values of the setting
// (its draws and its foe assignment are the classic bot's); with the
// setting off its answer stands; with it on, `fixed` decides and is handed
// the classic answer.
//
// RED (with_classic skipping the classic call when the setting is on):
// on.seen1 is "fixed", and the stream does not move.
TEST_F(BotGatesLib, with_classic_calls_classic_first_on_both_values)
{
    living* self = caster();
    const std::string lua =
        "    local seen = {}\n"
        "    local function classic(s)\n"
        "      seen[#seen + 1] = 'classic' .. og.match_setting('gameplay_fixes')\n"
        "      og.rand(7)\n"
        "      return s == self\n"
        "    end\n"
        "    local function fixed(s, answer)\n"
        "      seen[#seen + 1] = 'fixed'\n"
        "      og.log('handed', answer)\n"
        "      return 'fixed answer'\n"
        "    end\n"
        "    og.log('answer', G.with_classic(classic, fixed)(self))\n"
        "    og.log('calls', #seen)\n"
        "    for i = 1, #seen do\n"
        "      og.log('seen' .. i, seen[i])\n"
        "    end\n";
    std::uint32_t after[2] = {0, 0};
    for (short fixes : {short{0}, short{1}}) {
        world().gameplay_fixes = fixes;
        world().rng_.state_ = 4242u;
        const Answers a = probe(self, lua, /*draw_free=*/false);
        after[fixes] = world().rng_.state_;
        EXPECT_NE(4242u, after[fixes]) << "the classic gate drew, setting " << fixes;
        EXPECT_EQ("classic" + std::to_string(fixes), a.at("seen1"));
        if (fixes == 0) {
            EXPECT_EQ("true", a.at("answer")) << "the classic answer stands";
            EXPECT_EQ("1", a.at("calls"));
            EXPECT_EQ(0u, a.count("handed"));
        } else {
            EXPECT_EQ("fixed answer", a.at("answer"));
            EXPECT_EQ("2", a.at("calls"));
            EXPECT_EQ("fixed", a.at("seen2"));
            EXPECT_EQ("true", a.at("handed")) << "fixed sees the classic answer";
        }
    }
    EXPECT_EQ(after[0], after[1]) << "the same draws on both values";
}
