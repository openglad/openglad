/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch libs-4: the rules the review found in lineup.lua, living_common.lua, orc_specials.lua, treasure_consumables.lua, treasure_navigation.lua, treasure_valuables.lua, weapon_animate.lua.
// Each test is named after the rule it asserts.
//
// Every test was proven once against a planted break of its rule in the
// shipped Lua (packs/core/lib/..., restaged with --target
// stage_runtime_assets, then reverted); each test names its break in a
// comment, and the batch report records the line it failed on.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/treasure.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------- helpers

// A 64x64-tile grass field (1024 x 1024 px), nobody on it.
void open_field(TestGameWorld& tw)
{
    GameWorld& world = tw.world();
    world.resize_grid(64, 64);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.mysmoother.set_target(world.grid);
    world.my_team = 0;
    world.gameplay_fixes = 0;
    world.new_specials = 0;
}

// Sizes first, then the position: the obmap files a body under every cell
// its box covers at the moment it is placed.
walker* place(walker* w, short x, short y, short sx = 16, short sy = 16)
{
    if (w == nullptr)
        return nullptr;
    w->set_sizex(sx);
    w->set_sizey(sy);
    w->setxy(static_cast<short>(x + 1), y);
    w->setxy(x, y);
    return w;
}

// A bot living of `family` on `team` at (x, y): 200 hp, idle, 16 x 16.
walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y, short level = 1)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->set_act_type(ACT_RANDOM);
    w->stats()->set_level(level);
    w->stats()->set_max_hitpoints(200.0f);
    w->stats()->set_hitpoints(200.0f);
    w->stats()->set_max_magicpoints(200.0f);
    w->stats()->set_magicpoints(200.0f);
    w->stats()->set_magic_per_round(0.0f);
    w->set_busy(0.0f);
    return place(w, x, y);
}

walker* add_treasure(TestGameWorld& tw, int family, short x, short y,
                     short level, unsigned char team = 0)
{
    walker* w = tw.world().add_fx_ob(Order::Treasure, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->stats()->set_level(level);
    return place(w, x, y);
}

// Makes `w` a hero (it carries a guy).
void make_hero(walker* w, short con = 0, const char* name = "Gorik")
{
    w->set_owned_myguy(std::make_unique<guy>(static_cast<int>(w->family())));
    w->myguy->constitution = con;
    w->myguy->name = name;
}

int count_events(const TestGameWorld& tw, og::sim::EventKind kind)
{
    int n = 0;
    for (const auto& e : tw.events.events())
        if (e.kind == kind)
            ++n;
    return n;
}

int count_sound(const TestGameWorld& tw, int sound)
{
    int n = 0;
    for (const auto& e : tw.events.events())
        if (e.kind == og::sim::EventKind::PlaySound &&
            e.a == static_cast<std::uint32_t>(sound))
            ++n;
    return n;
}

std::vector<og::sim::Event> events_of(const TestGameWorld& tw,
                                      og::sim::EventKind kind)
{
    std::vector<og::sim::Event> out;
    for (const auto& e : tw.events.events())
        if (e.kind == kind)
            out.push_back(e);
    return out;
}

int count_live(const std::list<std::unique_ptr<walker>>& list, Order order,
               int family)
{
    int n = 0;
    for (const auto& uptr : list)
        if (uptr && !uptr->dead() && uptr->query_order() == order &&
            uptr->family() == family)
            ++n;
    return n;
}

// The world-RNG state after exactly the draws `bounds` from `state`.
std::uint32_t state_after(std::uint32_t state,
                          const std::vector<std::uint32_t>& bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

// The first state from which the draws satisfy `want`.
std::uint32_t state_where(const std::function<bool(og::sim::SimRandom&)>& want)
{
    for (std::uint32_t seed = 1; seed < 2000000u; ++seed) {
        og::sim::SimRandom probe(seed);
        if (want(probe))
            return seed;
    }
    return 0;
}

const TreasureFamilyDescriptor& treasure_fd(int family)
{
    og::test::mount_core_pack();
    return *get_treasure_family_descriptor(family);
}

bool eat(walker* pad, walker* eater)
{
    return og::test::on_eat(treasure_fd(pad->family()),
                            static_cast<treasure*>(pad), eater);
}

}  // namespace

// ===========================================================================
// lineup.lua — the lineup library's census, placement and banking. The
// matched-teams lineup is a current-game mode feature with no counterpart in
// the old game, so these rows have no classic oracle; each test calls the
// library itself from a probe script that stands in for a soldier's special.
// ===========================================================================

namespace {

constexpr int kProbeTeam = 7;  // the probe's own body: wildlife, so no census sees it
constexpr int kCursorSlot = 20;

class SemLineup : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    std::unique_ptr<TestGameWorld> tw_;
    og::test::ScopedHookFailureGuard guard_;
    walker* self_ = nullptr;

    void SetUp() override
    {
        og::test::mount_core_pack();
        tw_ = std::make_unique<TestGameWorld>();
        open_field(*tw_);
        world().rng_.state_ = 777u;
        self_ = add_living(*tw_, FAMILY_SOLDIER, kProbeTeam, 900, 900);
        ASSERT_NE(nullptr, self_);
    }

    void TearDown() override { tw_.reset(); }

    GameWorld& world() { return tw_->world(); }

    void anchor(int team, short x, short y)
    {
        auto& r = world().respawn;
        const int i = r.anchor_count[team];
        r.anchor_x[team][i] = x;
        r.anchor_y[team][i] = y;
        r.anchor_count[team] = static_cast<std::uint8_t>(i + 1);
    }

    void wall(int tx, int ty)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] =
            static_cast<std::uint8_t>(PIX_H_WALL1);
    }

    // Runs `lua` (with `lineup`, `C`, `self` and `at(x, y)` in scope) as
    // core:soldier's do_special for the probe body and returns its og.log
    // lines after the probe's marker, keyed by their first field.
    std::map<std::string, std::string> probe(const std::string& lua)
    {
        og::script::register_pack_script(
            {"test.sem_libs_4", "sem_libs_4_lineup_probe.lua",
             "local lineup = og.use('core:lineup')\n"
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
             "og.register_hooks('living', 'core:soldier', {\n"
             "  do_special = function(self)\n"
             "    og.log('<probe>')\n" +
                 lua +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
        const auto handled = og::script::hooks::do_special(
            get_family_descriptor(FAMILY_SOLDIER), self_);
        EXPECT_TRUE(handled.has_value()) << last_error();
        std::map<std::string, std::string> out;
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

}  // namespace

// lineup.lua:299. The human census sums f over the live roster characters
// of the score teams only: a dead hero, a hero on a wildlife team and a
// guyless living add nothing, though each of them has a power of its own
// (the same L1 soldier figure as the hero that counts).
// Breaks: `if w:dead() == 0 then` -> `if true then` (team 1 sums the dead
// hero); `if team < C.SCORE_TEAM_COUNT then` -> `if team < 8 then` (the
// wildlife hero indexes sums[6], an arithmetic error on nil).
TEST_F(SemLineup, the_power_census_counts_only_live_heroes_on_score_teams)
{
    walker* hero = add_living(*tw_, FAMILY_SOLDIER, 0, 96, 96);
    walker* dead_hero = add_living(*tw_, FAMILY_SOLDIER, 1, 160, 96);
    walker* wild_hero = add_living(*tw_, FAMILY_SOLDIER, 5, 224, 96);
    walker* monster = add_living(*tw_, FAMILY_SOLDIER, 2, 288, 96);
    ASSERT_TRUE(hero && dead_hero && wild_hero && monster);
    make_hero(hero);
    make_hero(dead_hero);
    make_hero(wild_hero);
    dead_hero->set_dead(1);

    auto out = probe(
        "    og.log('power', lineup.walker_power(at(96, 96)),\n"
        "           lineup.walker_power(at(160, 96)),\n"
        "           lineup.walker_power(at(224, 96)))\n"
        "    local ref, sums = lineup.census_power(og.oblist())\n"
        "    og.log('census', ref, sums[1], sums[2], sums[3], sums[4],\n"
        "           tostring(sums[6]))\n");
    ASSERT_EQ(1u, out.count("power")) << last_error();
    ASSERT_EQ(1u, out.count("census")) << last_error();
    // Every one of the three heroes has the same L1 soldier figure.
    const std::string p = out["power"].substr(0, out["power"].find('\t'));
    ASSERT_EQ(p + "\t" + p + "\t" + p, out["power"]);
    ASSERT_NE("0", p);
    EXPECT_EQ(p + "\t" + p + "\t0\t0\t0\tnil", out["census"])
        << "reference = team 0's sum; the dead hero (team 1), the guyless "
           "living (team 2) and the wildlife hero (team 5) add nothing";
    EXPECT_EQ(0u, guard_.count()) << guard_.message();
}

// lineup.lua:412. Banking a team's fill code it already holds changes
// nothing; a new code replaces that team's two digits and leaves the
// announce latch (the ones digit) and the other teams' digits alone.
// Break: the early `return` -> `og.mode_set(MATCHED.ANNOUNCED,
// og.mode_get(MATCHED.ANNOUNCED) + 1) return` (a re-bank that touches the
// slot).
TEST_F(SemLineup, re_banking_the_same_fill_code_leaves_the_announced_slot_alone)
{
    // Latch 1, team 0 code 3, team 1 code 2: 1 + 3*10 + 2*1000.
    auto out = probe(
        "    local A = lineup.MATCHED.ANNOUNCED\n"
        "    og.mode_set(A, 2031)\n"
        "    lineup.bank_lineup_facts(1, 2)\n"
        "    og.log('same', og.mode_get(A))\n"
        "    lineup.bank_lineup_facts(1, 4)\n"
        "    og.log('new', og.mode_get(A))\n"
        "    lineup.bank_lineup_facts(1, 4)\n"
        "    og.log('again', og.mode_get(A))\n");
    ASSERT_EQ(1u, out.count("same")) << last_error();
    EXPECT_EQ("2031", out["same"]) << "same code: the slot is unchanged";
    EXPECT_EQ("4031", out["new"]) << "new code: team 1's digits replaced, latch and team 0 kept";
    EXPECT_EQ("4031", out["again"]) << "same code again: unchanged";
    EXPECT_EQ(0u, guard_.count()) << guard_.message();
}

// lineup.lua:517. The staging census counts a live hero as roster with its
// power, a guyless living as an npc and a generator as a generator, each on
// its own team row; the same three kinds on a wildlife team (5) touch no
// row at all.
// Breaks: `trow.roster = trow.roster + 1` -> `+ 0` (hero roster red);
// `trow.power = trow.power + walker_power(w)` -> `trow.power` (hero power
// red); `if team < C.SCORE_TEAM_COUNT then` -> `if team < 8 then` (the
// wildlife walkers index teams[6], an error on nil).
TEST_F(SemLineup, the_staging_census_counts_heroes_npcs_and_generators_on_score_teams_only)
{
    walker* hero = add_living(*tw_, FAMILY_SOLDIER, 0, 96, 96);
    walker* npc = add_living(*tw_, FAMILY_ORC, 1, 160, 96);
    walker* tent = place(world().add_ob(Order::Generator, FAMILY_TENT), 224, 96);
    walker* wild_hero = add_living(*tw_, FAMILY_SOLDIER, 5, 96, 160);
    walker* wild_npc = add_living(*tw_, FAMILY_ORC, 5, 160, 160);
    walker* wild_tent = place(world().add_ob(Order::Generator, FAMILY_TENT), 224, 160);
    ASSERT_TRUE(hero && npc && tent && wild_hero && wild_npc && wild_tent);
    tent->set_team_num(2);
    wild_tent->set_team_num(5);
    make_hero(hero);
    make_hero(wild_hero);

    auto out = probe(
        "    og.log('power', lineup.walker_power(at(96, 96)))\n"
        "    local inputs = lineup.census_inputs()\n"
        "    for t = 1, 4 do\n"
        "      local r = inputs.teams[t]\n"
        "      og.log('team' .. (t - 1), r.roster, r.npcs, r.generators, r.power)\n"
        "    end\n"
        "    og.log('rows', #inputs.teams)\n");
    ASSERT_EQ(1u, out.count("power")) << last_error();
    const std::string p = out["power"];
    ASSERT_NE("0", p);
    EXPECT_EQ("1\t0\t0\t" + p, out["team0"]) << "hero: roster 1, power f(hero)";
    EXPECT_EQ("0\t1\t0\t0", out["team1"]) << "guyless living: npcs 1";
    EXPECT_EQ("0\t0\t1\t0", out["team2"]) << "generator: generators 1";
    EXPECT_EQ("0\t0\t0\t0", out["team3"]) << "nobody on team 3";
    EXPECT_EQ("4", out["rows"]) << "team >= 4: no row, nothing counted";
    EXPECT_EQ(0u, guard_.count()) << guard_.message();
}

// lineup.lua:544. Ring cell i of tile radius r walks the L1 ring clockwise
// from due north: north, east, south, west edges in turn.
// Breaks: the edge-1 return `r - k, k` -> `k, r - k`; the edge-2 return
// `-k, r - k` -> `k, r - k`.
TEST_F(SemLineup, a_ring_walks_clockwise_from_due_north)
{
    auto out = probe(
        "    for r = 1, 2 do\n"
        "      local cells = {}\n"
        "      for i = 0, 4 * r - 1 do\n"
        "        local dx, dy = lineup.ring_offset(i, r)\n"
        "        cells[#cells + 1] = '(' .. dx .. ',' .. dy .. ')'\n"
        "      end\n"
        "      og.log('r' .. r, table.concat(cells, ' '))\n"
        "    end\n");
    EXPECT_EQ("(0,-1) (1,0) (0,1) (-1,0)", out["r1"]) << last_error();
    EXPECT_EQ("(0,-2) (1,-1) (2,0) (1,1) (0,2) (-1,1) (-2,0) (-1,-1)",
              out["r2"]);
    EXPECT_EQ(0u, guard_.count()) << guard_.message();
}

// lineup.lua:563. A walker placed for a team outside the score range (5,
// or -1) gets no anchor: without teleport the call fails and nothing moves,
// the cursor included; with teleport it is exactly the walker's own random
// teleport (the same landing and the same draws as calling w:teleport()).
// Break: `if team < C.SCORE_TEAM_COUNT then` -> `if team < 8 then` (team 5
// reads respawn_anchor_count(5), a range error).
TEST_F(SemLineup, a_team_off_the_score_range_gets_no_anchor)
{
    anchor(0, 96, 96);
    anchor(0, 160, 96);  // two team-0 anchors, the only ones authored
    walker* orc = add_living(*tw_, FAMILY_ORC, 5, 400, 400);
    ASSERT_NE(nullptr, orc);
    world().rng_.state_ = 4242u;
    auto out = probe(
        "    local w = at(400, 400)\n"
        "    og.log('five', tostring(lineup.place_at_anchor(w, 5, 20, false)),\n"
        "           w:xpos(), w:ypos(), og.mode_get(20))\n"
        "    og.log('minus', tostring(lineup.place_at_anchor(w, -1, 20, false)),\n"
        "           w:xpos(), w:ypos(), og.mode_get(20))\n"
        "    local ok = lineup.place_at_anchor(w, 5, 20, true)\n"
        "    og.log('tele', tostring(ok), w:xpos(), w:ypos(), og.mode_get(20))\n");
    ASSERT_EQ(1u, out.count("tele")) << last_error();
    EXPECT_EQ("false\t400\t400\t0", out["five"])
        << "team 5, no teleport: false, not moved, cursor untouched";
    EXPECT_EQ("false\t400\t400\t0", out["minus"])
        << "team -1, no teleport: false, not moved, cursor untouched";
    walker* placed = orc;
    const std::uint32_t after_state = world().rng_.state_;
    const short px = placed->xpos();
    const short py = placed->ypos();

    // The twin: the same body, the same stream, the engine's own teleport.
    placed->setxy(400, 400);
    world().rng_.state_ = 4242u;
    const bool twin_ok = placed->teleport();
    EXPECT_EQ(std::string(twin_ok ? "true" : "false") + "\t" +
                  std::to_string(placed->xpos()) + "\t" +
                  std::to_string(placed->ypos()) + "\t0",
              out["tele"])
        << "team 5 with teleport: the teleport's own answer and landing";
    EXPECT_EQ(world().rng_.state_, after_state) << "the same draws as w:teleport()";
    EXPECT_NE(4242u, after_state) << "the teleport drew";
    EXPECT_EQ(px, placed->xpos());
    EXPECT_EQ(py, placed->ypos());
    EXPECT_EQ(0u, guard_.count()) << guard_.message();
}

// lineup.lua:606. With the anchor and every ring tile within three tiles
// walled in, a respawn placement (no teleport) reports failure, leaves the
// walker where it stood and draws nothing; an init placement falls back to
// exactly the walker's own random teleport. Either way the cursor advanced
// once per anchor probed.
// Breaks: `return w:teleport()` -> `return false` (teleport arm red);
// the final `return false` -> `return w:teleport()` (respawn arm red).
TEST_F(SemLineup, a_walled_in_anchor_fails_a_respawn_and_teleports_an_init_placement)
{
    anchor(1, 96, 96);
    for (int tx = 2; tx <= 10; ++tx)
        for (int ty = 2; ty <= 10; ++ty)
            wall(tx, ty);
    walker* orc = add_living(*tw_, FAMILY_ORC, 1, 400, 400);
    ASSERT_NE(nullptr, orc);
    world().rng_.state_ = 4242u;
    auto out = probe(
        "    local w = at(400, 400)\n"
        "    og.log('respawn', tostring(lineup.place_at_anchor(w, 1, 20, false)),\n"
        "           w:xpos(), w:ypos(), og.mode_get(20))\n");
    ASSERT_EQ(1u, out.count("respawn")) << last_error();
    EXPECT_EQ("false\t400\t400\t1", out["respawn"])
        << "respawn: false, not moved, the one anchor probed";
    EXPECT_EQ(4242u, world().rng_.state_) << "respawn: nothing drawn";

    out = probe(
        "    local w = at(400, 400)\n"
        "    local ok = lineup.place_at_anchor(w, 1, 20, true)\n"
        "    og.log('init', tostring(ok), w:xpos(), w:ypos(), og.mode_get(20))\n");
    ASSERT_EQ(1u, out.count("init")) << last_error();
    const std::uint32_t after_state = world().rng_.state_;
    const std::string init = out["init"];

    orc->setxy(400, 400);
    world().rng_.state_ = 4242u;
    const bool twin_ok = orc->teleport();
    EXPECT_EQ(std::string(twin_ok ? "true" : "false") + "\t" +
                  std::to_string(orc->xpos()) + "\t" +
                  std::to_string(orc->ypos()) + "\t2",
              init)
        << "init: the teleport's own answer and landing";
    EXPECT_EQ(world().rng_.state_, after_state) << "init: the same draws as w:teleport()";
    EXPECT_NE(4242u, after_state) << "init: the teleport drew";
    EXPECT_EQ(0u, guard_.count()) << guard_.message();
}

// lineup.lua:696. A mode with no placer of its own places a squad on the
// shared anchor rotation: member k on anchor k, the cursor advanced once
// per member.
// Break: the nil-placer `place_at_anchor(w, team, cursor_slot, true)` ->
// deleted (the members stay where add_ob put them).
TEST_F(SemLineup, a_squad_with_no_placer_stands_on_the_anchors_in_member_order)
{
    anchor(1, 96, 96);
    anchor(1, 192, 96);
    anchor(1, 288, 96);
    world().ctf_requested_fill[1] = 2;  // FAIR
    auto out = probe(
        "    lineup.spawn_bots(1, { 'core:soldier', 'core:archer', 'core:mage' },\n"
        "                      20, nil, nil, nil)\n"
        "    for _, xy in ipairs({ { 96, 96 }, { 192, 96 }, { 288, 96 } }) do\n"
        "      local w = at(xy[1], xy[2])\n"
        "      if w then\n"
        "        og.log('at' .. xy[1], w:family(), w:team_num())\n"
        "      else\n"
        "        og.log('at' .. xy[1], 'nobody')\n"
        "      end\n"
        "    end\n"
        "    og.log('cursor', og.mode_get(20))\n");
    ASSERT_EQ(1u, out.count("cursor")) << last_error();
    EXPECT_EQ(std::to_string(FAMILY_SOLDIER) + "\t1", out["at96"]) << "member 1 on anchor 0";
    EXPECT_EQ(std::to_string(FAMILY_ARCHER) + "\t1", out["at192"]) << "member 2 on anchor 1";
    EXPECT_EQ(std::to_string(FAMILY_MAGE) + "\t1", out["at288"]) << "member 3 on anchor 2";
    EXPECT_EQ("3", out["cursor"]) << "one cursor step per member";
    EXPECT_EQ(0u, guard_.count()) << guard_.message();
}

// ===========================================================================
// living_common.lua — the teleport guard (classic: walker.cpp FAMILY_SKELETON
// case 1 in the old game, `if (ani_type == ANI_TELE_OUT || ani_type ==
// ANI_TELE_IN) return 0;`)
// ===========================================================================

namespace {

struct Cast {
    bool ok = false;
    std::string reason;
};

Cast cast(walker* w, int slot)
{
    w->set_current_special(static_cast<char>(slot));
    w->set_shifter_down(0);
    Cast out;
    out.ok = w->special(nullptr, &out.reason);
    return out;
}

}  // namespace

// living_common.lua:22. A skeleton already going OUT of a teleport (its
// tunnel's first half) cannot tunnel again: SPECIAL BUSY, nothing spent;
// the same skeleton walking tunnels (the control).
// Break: `return ani == C.ANI_TELE_OUT or ani == C.ANI_TELE_IN` ->
// `return ani == C.ANI_TELE_IN`.
TEST(SemTeleportGuard, a_walker_going_out_of_a_teleport_is_mid_teleport)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    TestGameWorld tw;
    open_field(tw);
    walker* skel = add_living(tw, FAMILY_SKELETON, 0, 96, 96, 4);
    ASSERT_NE(nullptr, skel);
    skel->set_act_type(ACT_CONTROL);
    skel->stats()->set_max_magicpoints(50.0f);
    skel->stats()->set_magicpoints(50.0f);
    skel->set_ani_type(ANI_TELE_OUT);
    skel->set_cycle(0);

    const Cast busy = cast(skel, 1);
    EXPECT_FALSE(busy.ok) << "TELE_OUT: the tunnel is refused";
    EXPECT_EQ("SPECIAL BUSY", busy.reason) << "TELE_OUT: mid-teleport";
    EXPECT_FLOAT_EQ(50.0f, skel->stats()->magicpoints()) << "TELE_OUT: nothing spent";

    skel->set_ani_type(ANI_WALK);
    const Cast free_cast = cast(skel, 1);
    EXPECT_TRUE(free_cast.ok) << "walking: the tunnel goes";
    EXPECT_EQ(ANI_TELE_OUT, skel->ani_type());
    EXPECT_FLOAT_EQ(40.0f, skel->stats()->magicpoints()) << "walking: TUNNEL costs 10";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// orc_specials.lua — HOWL (classic: walker.cpp FAMILY_ORC case 1 in the old
// game: the gameplay_fixes no-foe refusal, then `busy += 2`, and per foe
// `tempx = myguy ? constitution : hitpoints / 30`, `tempy = 10 +
// random(level*10) - random(tempx*10)`, clamped at 0, added to frozen_delay)
// ===========================================================================

namespace {

// A level-0 orc on team 1 at (96,96): its level roll is rand0(0), no draw.
walker* add_howler(TestGameWorld& tw)
{
    walker* orc = add_living(tw, FAMILY_ORC, 1, 96, 96, 0);
    if (orc != nullptr)
        orc->set_act_type(ACT_CONTROL);
    return orc;
}

}  // namespace

// orc_specials.lua:20. With a foe in reach HOWL goes off under either value
// of Gameplay Fixes: the foe is stunned (a con-0 hero, level-0 orc: 10
// ticks), the orc is busy 2 and roars once.
// Break: `if gf.on() and #foes == 0 then` -> `if gf.on() then` (the
// fixes-on leg is refused).
TEST(SemOrcHowl, a_howl_with_a_foe_in_reach_goes_off_with_fixes_on_or_off)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("gameplay_fixes " + std::to_string(fixes));
        TestGameWorld tw;
        open_field(tw);
        tw.world().gameplay_fixes = fixes;
        tw.world().rng_.state_ = 99u;
        walker* orc = add_howler(tw);
        walker* foe = add_living(tw, FAMILY_SOLDIER, 0, 112, 96);
        ASSERT_TRUE(orc && foe);
        make_hero(foe, 0);
        foe->stats()->set_frozen_delay(0);

        const Cast c = cast(orc, 1);
        EXPECT_TRUE(c.ok) << "foe in range: the howl goes off";
        EXPECT_EQ("", c.reason);
        EXPECT_EQ(10, foe->stats()->frozen_delay()) << "foe in range: stunned 10";
        EXPECT_FLOAT_EQ(2.0f, orc->busy()) << "foe in range: busy 2";
        EXPECT_EQ(1, count_sound(tw, SOUND_ROAR)) << "foe in range: one roar";
        EXPECT_EQ(99u, tw.world().rng_.state_)
            << "level 0, con 0: both rolls are rand0(0), nothing drawn";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// orc_specials.lua:31. A guyless foe resists by trunc(hp / 30): at 89 hp
// that is constitution 2, one rand0(20) draw off the 10-tick stun.
// Break: the guyless `con = og.trunc(og.fdiv(foe.hp, 30.0))` -> `con = 0`
// (no draw, stun 10).
TEST(SemOrcHowl, a_guyless_foe_resists_a_howl_by_a_thirtieth_of_its_hitpoints)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    TestGameWorld tw;
    open_field(tw);
    walker* orc = add_howler(tw);
    walker* foe = add_living(tw, FAMILY_SOLDIER, 0, 112, 96);
    ASSERT_TRUE(orc && foe);
    foe->stats()->set_hitpoints(89.0f);  // 89 / 30 = 2.97 -> 2 (not 3)
    foe->stats()->set_frozen_delay(0);
    const std::uint32_t pin =
        state_where([](og::sim::SimRandom& r) { return r.next(20) == 7; });
    ASSERT_NE(0u, pin);
    ASSERT_NE(7u, og::sim::SimRandom(pin).next(30)) << "the pin tells trunc from round";
    tw.world().rng_.state_ = pin;

    ASSERT_TRUE(cast(orc, 1).ok);
    EXPECT_EQ(10 - 7, foe->stats()->frozen_delay())
        << "guyless foe: con = trunc(89 / 30) = 2, stun 10 - rand0(20)";
    EXPECT_EQ(state_after(pin, {20}), tw.world().rng_.state_)
        << "guyless foe: exactly one rand0(20) draw";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// treasure_consumables.lua — the drumstick and the potions (classic:
// treasure.cpp treasure::eat_me FAMILY_DRUMSTICK / FAMILY_MAGIC_POTION in the
// old game, the same branches)
// ===========================================================================

// treasure_consumables.lua:9. A bot eats a potion silently (no notification
// at all) and the potion is gone; a seated eater gets exactly one
// "Potion of Mana(1)!" line, addressed to its own seat.
// Break: `if eater:user() ~= -1 then` -> `if true then`.
TEST(SemTreasureConsumables, a_bot_drinks_a_potion_silently_a_seated_eater_is_told)
{
    og::test::ScopedHookFailureGuard guard;
    for (int seat : {-1, 1}) {
        SCOPED_TRACE("seat " + std::to_string(seat));
        TestGameWorld tw;
        open_field(tw);
        walker* eater = add_living(tw, FAMILY_MAGE, 0, 96, 96);
        walker* potion = add_treasure(tw, FAMILY_MAGIC_POTION, 96, 96, 1);
        ASSERT_TRUE(eater && potion);
        eater->set_user(static_cast<signed char>(seat));
        EXPECT_TRUE(eat(potion, eater));
        EXPECT_EQ(1, potion->dead()) << "the potion is consumed either way";
        const auto notes = events_of(tw, og::sim::EventKind::Notification);
        if (seat < 0) {
            EXPECT_EQ(0u, notes.size()) << "bot eater: no notification";
        } else {
            ASSERT_EQ(1u, notes.size()) << "seated eater: one notification";
            EXPECT_EQ("Potion of Mana(1)!", notes[0].text);
            EXPECT_EQ(1, notes[0].target_player) << "addressed to the eater's seat";
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// treasure_consumables.lua:27. A drumstick heals no higher than max hp: a
// level-10 drumstick (100..199) on a 99/100 eater leaves it at exactly 100.
// Break: `eater.hp = eater.max_hp` (the clamp) -> deleted.
TEST(SemTreasureConsumables, a_drumstick_heals_no_higher_than_max_hp)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    walker* eater = add_living(tw, FAMILY_SOLDIER, 0, 96, 96);
    walker* drumstick = add_treasure(tw, FAMILY_DRUMSTICK, 96, 96, 10);
    ASSERT_TRUE(eater && drumstick);
    eater->stats()->set_max_hitpoints(100.0f);
    eater->stats()->set_hitpoints(99.0f);
    tw.world().rng_.state_ = 31u;
    EXPECT_TRUE(eat(drumstick, eater));
    EXPECT_FLOAT_EQ(100.0f, eater->stats()->hitpoints()) << "heal exceeds the wound: hp == max";
    EXPECT_EQ(1, drumstick->dead()) << "the drumstick is eaten";
    EXPECT_EQ(1, count_sound(tw, SOUND_EAT));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// treasure_consumables.lua:38. A mana potion tops a low pool up to max
// before its 50-per-level overfill (10/100 -> 150, not 60), and never
// lowers an already overfilled pool (600/50 -> 650, not 100).
// Breaks: the top-up `eater.magicpoints = eater.max_magicpoints` ->
// deleted (below-max arm red); `<` -> `~=` (above-max arm red).
TEST(SemTreasureConsumables, a_mana_potion_tops_up_then_overfills_and_never_lowers_a_pool)
{
    og::test::ScopedHookFailureGuard guard;
    struct Case { float mp; float max; float want; const char* arm; };
    for (const Case c : {Case{10.0f, 100.0f, 150.0f, "below max"},
                         Case{600.0f, 50.0f, 650.0f, "already above max"}}) {
        SCOPED_TRACE(c.arm);
        TestGameWorld tw;
        open_field(tw);
        walker* eater = add_living(tw, FAMILY_MAGE, 0, 96, 96);
        walker* potion = add_treasure(tw, FAMILY_MAGIC_POTION, 96, 96, 1);
        ASSERT_TRUE(eater && potion);
        eater->stats()->set_max_magicpoints(c.max);
        eater->stats()->set_magicpoints(c.mp);
        EXPECT_TRUE(eat(potion, eater));
        EXPECT_FLOAT_EQ(c.want, eater->stats()->magicpoints()) << c.arm;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// treasure_navigation.lua — the exit pad and the warp pad (classic:
// treasure.cpp treasure::eat_me FAMILY_EXIT / FAMILY_TELEPORTER in the old
// game)
// ===========================================================================

namespace {

// A seated player-controlled soldier standing on an exit pad to level
// `dest`; the world says whether foes remain.
struct ExitScene {
    TestGameWorld tw;
    walker* hero = nullptr;
    walker* pad = nullptr;

    ExitScene(short dest, bool foes_left)
    {
        og::test::mount_core_pack();
        open_field(tw);
        tw.world().level_done = foes_left ? 0 : 2;
        tw.world().current_scenario = 5;
        hero = add_living(tw, FAMILY_SOLDIER, 0, 96, 96);
        pad = add_treasure(tw, FAMILY_EXIT, 96, 96, dest);
        if (hero != nullptr) {
            hero->set_act_type(ACT_CONTROL);
            hero->set_skip_exit(0);
        }
    }

    void title(const char* scen, const char* name)
    {
        const std::string want = scen;
        const std::string answer = name;
        tw.world().scenario_title_provider = [want, answer](const char* n) {
            return want == n ? answer : std::string("none");
        };
    }

    std::vector<og::sim::Event> prompts() const
    {
        return events_of(tw, og::sim::EventKind::RequestExitConfirmation);
    }
};

}  // namespace

// treasure_navigation.lua:23. The pad's prompt reads "Withdraw to <name>?"
// (b = 1) on the withdraw path — foes stand, the destination is already
// earned and this level is not — and "Exit to <name>?" (b = 0) on the
// exit path.
// Break: `if is_withdraw then` -> `if not is_withdraw then`.
TEST(SemExitPad, the_prompt_reads_withdraw_on_the_withdraw_path_and_exit_otherwise)
{
    og::test::ScopedHookFailureGuard guard;
    {
        ExitScene s(2, /*foes_left=*/true);
        ASSERT_TRUE(s.hero && s.pad);
        s.title("scen2", "Keep");
        s.tw.world().completed_levels.insert(2);
        EXPECT_TRUE(eat(s.pad, s.hero));
        const auto p = s.prompts();
        ASSERT_EQ(1u, p.size()) << "withdraw: one prompt";
        EXPECT_EQ("Withdraw to Keep?", p[0].text);
        EXPECT_EQ(2u, p[0].a);
        EXPECT_EQ(1u, p[0].b) << "withdraw: b = 1";
        EXPECT_EQ(1, count_events(s.tw, og::sim::EventKind::WithdrawToLevel));
    }
    {
        ExitScene s(2, /*foes_left=*/false);
        ASSERT_TRUE(s.hero && s.pad);
        s.title("scen2", "Keep");
        EXPECT_TRUE(eat(s.pad, s.hero));
        const auto p = s.prompts();
        ASSERT_EQ(1u, p.size()) << "exit: one prompt";
        EXPECT_EQ("Exit to Keep?", p[0].text);
        EXPECT_EQ(2u, p[0].a);
        EXPECT_EQ(0u, p[0].b) << "exit: b = 0";
        EXPECT_EQ(0, count_events(s.tw, og::sim::EventKind::WithdrawToLevel));
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// treasure_navigation.lua:51. An exit whose scenario has no readable title
// is named "Level <N>"; one with a title uses it.
// Breaks: `if exit_name == "none" then` -> `if exit_name == "nope" then`
// (no-title arm red); -> `if true then` (title arm red).
TEST(SemExitPad, an_exit_with_no_title_is_named_by_its_level_number)
{
    og::test::ScopedHookFailureGuard guard;
    {
        ExitScene s(77, /*foes_left=*/false);  // no title reader at all
        ASSERT_TRUE(s.hero && s.pad);
        EXPECT_TRUE(eat(s.pad, s.hero));
        const auto p = s.prompts();
        ASSERT_EQ(1u, p.size());
        EXPECT_EQ("Exit to Level 77?", p[0].text) << "no title";
    }
    {
        ExitScene s(77, /*foes_left=*/false);
        ASSERT_TRUE(s.hero && s.pad);
        s.title("scen77", "The Keep");
        EXPECT_TRUE(eat(s.pad, s.hero));
        const auto p = s.prompts();
        ASSERT_EQ(1u, p.size());
        EXPECT_EQ("Exit to The Keep?", p[0].text) << "title";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// treasure_navigation.lua:58. A scenario flagged CAN_EXIT_WHENEVER lets the
// player exit with foes standing, through the normal Exit prompt (b = 0, no
// withdraw); without the flag the same pad offers no prompt. The
// destination is unearned here, so the old game agrees on both arms (it
// tests the withdraw path first, which this scene never takes).
// Breaks: `foes_left == 0 or og.world_can_exit_whenever()` -> `foes_left ==
// 0` (flag-set arm red); -> `foes_left == 0 or true` (flag-clear arm red).
TEST(SemExitPad, a_can_exit_whenever_level_offers_the_exit_with_foes_standing)
{
    og::test::ScopedHookFailureGuard guard;
    {
        ExitScene s(2, /*foes_left=*/true);
        ASSERT_TRUE(s.hero && s.pad);
        s.title("scen2", "Keep");
        s.tw.world().type = GameWorld::TYPE_CAN_EXIT_WHENEVER;
        EXPECT_TRUE(eat(s.pad, s.hero));
        const auto p = s.prompts();
        ASSERT_EQ(1u, p.size()) << "flag set, foes alive: one prompt";
        EXPECT_EQ("Exit to Keep?", p[0].text);
        EXPECT_EQ(0u, p[0].b) << "flag set: the exit prompt, not withdraw";
        EXPECT_EQ(0, count_events(s.tw, og::sim::EventKind::WithdrawToLevel));
        EXPECT_FALSE(s.tw.world().withdraw_requested);
    }
    {
        ExitScene s(2, /*foes_left=*/true);
        ASSERT_TRUE(s.hero && s.pad);
        s.title("scen2", "Keep");
        s.tw.world().type = 0;
        EXPECT_TRUE(eat(s.pad, s.hero));
        EXPECT_EQ(0u, s.prompts().size()) << "flag clear, foes alive: no exit prompt";
        EXPECT_EQ(0, count_events(s.tw, og::sim::EventKind::WithdrawToLevel));
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// treasure_navigation.lua:80-90, Gameplay Fixes AJ (#481). On a
// CAN_EXIT_WHENEVER level with foes standing and the destination already
// won, the old game asks "Withdraw to <name>?" first (treasure.cpp
// FAMILY_EXIT, the withdraw test before the exit test; a declined withdraw
// then asks Exit, which the server's follow-up prompt does here). With the
// setting on the pad asks "Exit to <name>?" only (#98). With no foe left
// both settings exit: the old game's withdraw needs foes.
// Breaks: `can_exit_now = can_exit_now and not withdraw_first` ->
// `can_exit_now = can_exit_now` (off arm red); `withdraw_first =
// withdraw_first and not gf.on()` -> `withdraw_first = withdraw_first` (on
// arm red); `foes_left ~= 0` -> `foes_left ~= 2` (no-foe arm red).
TEST(SemExitPad, a_can_exit_whenever_level_withdraws_first_to_a_won_level_with_the_fixes_off_and_exits_with_them_on)
{
    og::test::ScopedHookFailureGuard guard;
    {
        SCOPED_TRACE("setting off, foes standing");
        ExitScene s(2, /*foes_left=*/true);
        ASSERT_TRUE(s.hero && s.pad);
        s.title("scen2", "Keep");
        s.tw.world().type = GameWorld::TYPE_CAN_EXIT_WHENEVER;
        s.tw.world().completed_levels.insert(2);
        s.tw.world().gameplay_fixes = 0;
        EXPECT_TRUE(eat(s.pad, s.hero));
        const auto p = s.prompts();
        ASSERT_EQ(1u, p.size()) << "off: one prompt";
        EXPECT_EQ("Withdraw to Keep?", p[0].text);
        EXPECT_EQ(2u, p[0].a);
        EXPECT_EQ(1u, p[0].b) << "off: the withdraw prompt, b = 1";
        EXPECT_EQ(1, count_events(s.tw, og::sim::EventKind::WithdrawToLevel));
        EXPECT_TRUE(s.tw.world().withdraw_requested);
        EXPECT_EQ(2, s.tw.world().withdraw_level);
    }
    {
        SCOPED_TRACE("setting on, foes standing");
        ExitScene s(2, /*foes_left=*/true);
        ASSERT_TRUE(s.hero && s.pad);
        s.title("scen2", "Keep");
        s.tw.world().type = GameWorld::TYPE_CAN_EXIT_WHENEVER;
        s.tw.world().completed_levels.insert(2);
        s.tw.world().gameplay_fixes = 1;
        EXPECT_TRUE(eat(s.pad, s.hero));
        const auto p = s.prompts();
        ASSERT_EQ(1u, p.size()) << "on: one prompt";
        EXPECT_EQ("Exit to Keep?", p[0].text);
        EXPECT_EQ(2u, p[0].a);
        EXPECT_EQ(0u, p[0].b) << "on: the exit prompt, b = 0";
        EXPECT_EQ(0, count_events(s.tw, og::sim::EventKind::WithdrawToLevel));
        EXPECT_FALSE(s.tw.world().withdraw_requested);
    }
    {
        SCOPED_TRACE("setting off, no foe left");
        ExitScene s(2, /*foes_left=*/false);
        ASSERT_TRUE(s.hero && s.pad);
        s.title("scen2", "Keep");
        s.tw.world().type = GameWorld::TYPE_CAN_EXIT_WHENEVER;
        s.tw.world().completed_levels.insert(2);
        s.tw.world().gameplay_fixes = 0;
        EXPECT_TRUE(eat(s.pad, s.hero));
        const auto p = s.prompts();
        ASSERT_EQ(1u, p.size()) << "no foe: one prompt";
        EXPECT_EQ("Exit to Keep?", p[0].text);
        EXPECT_EQ(0u, p[0].b) << "no foe: the exit prompt, the withdraw needs foes";
        EXPECT_EQ(0, count_events(s.tw, og::sim::EventKind::WithdrawToLevel));
        EXPECT_FALSE(s.tw.world().withdraw_requested);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

int flashes(const TestGameWorld& tw)
{
    // og.add_ob("fx", ...) files an effect on the object list.
    return count_live(tw.world().oblist, Order::FX, FAMILY_FLASH) +
           count_live(tw.world().fxlist, Order::FX, FAMILY_FLASH);
}

void stand_on(walker* who, const walker* pad)
{
    who->setxy(pad->xpos(), pad->ypos());
    who->set_skip_exit(0);
}

}  // namespace

// treasure_navigation.lua:106. A warp pad keeps the partner it remembered:
// handed a remembered partner other than the one its search finds, it warps
// there. Classic quirk (the old game's `leader` cache is the same): the
// remembered partner is never re-validated, so a partner that died this
// tick is still the warp target. A lone pad with no partner only bumps the
// eater's skip_exit by 20: no move, no flash, no partner remembered.
// Breaks: `if not self:leader() then` -> `if true then` (cached arm red);
// `if not target then` -> `if false then` (no-partner arm: center_on(nil)
// raises).
TEST(SemWarpPad, a_warp_pad_keeps_its_remembered_partner_classic_and_a_lone_pad_only_throttles)
{
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        open_field(tw);
        walker* a = add_treasure(tw, FAMILY_TELEPORTER, 100, 100, 5);
        walker* b = add_treasure(tw, FAMILY_TELEPORTER, 300, 300, 5);
        walker* c = add_treasure(tw, FAMILY_TELEPORTER, 500, 500, 5);
        walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 700, 700);
        ASSERT_TRUE(a && b && c && hero);

        stand_on(hero, a);
        EXPECT_TRUE(eat(a, hero));
        walker* found = a->leader();
        ASSERT_TRUE(found == b || found == c) << "first use: the search found a partner";
        walker* other = (found == b) ? c : b;
        EXPECT_EQ(found->xpos(), hero->xpos());

        a->set_leader(other);
        stand_on(hero, a);
        EXPECT_TRUE(eat(a, hero));
        EXPECT_EQ(other, a->leader()) << "cached: the remembered partner kept";
        EXPECT_EQ(other->xpos(), hero->xpos()) << "cached: warped to the remembered partner";
        EXPECT_EQ(other->ypos(), hero->ypos());

        other->set_dead(1);
        stand_on(hero, a);
        EXPECT_TRUE(eat(a, hero));
        EXPECT_EQ(other->xpos(), hero->xpos())
            << "classic: a dead remembered partner is still the target";
        EXPECT_EQ(other->ypos(), hero->ypos());
    }
    {
        TestGameWorld tw;
        open_field(tw);
        walker* lone = add_treasure(tw, FAMILY_TELEPORTER, 200, 200, 9);
        walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 200, 200);
        ASSERT_TRUE(lone && hero);
        stand_on(hero, lone);
        EXPECT_TRUE(eat(lone, hero));
        EXPECT_EQ(200, hero->xpos()) << "no partner: not moved";
        EXPECT_EQ(200, hero->ypos());
        EXPECT_EQ(20, hero->skip_exit()) << "no partner: skip_exit bumped by 20";
        EXPECT_EQ(0, flashes(tw)) << "no partner: no flash";
        EXPECT_EQ(nullptr, lone->leader()) << "no partner: nothing remembered";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// treasure_navigation.lua:116. A warp whose destination is blocked (an orc
// standing on the partner pad) bounces the eater back onto the departure
// pad with no flash; the same warp once the partner is clear lands on it
// and flashes once at the departure pad.
// Break: `if not og.query_passable(...) then` -> `if false then`.
TEST(SemWarpPad, a_blocked_warp_bounces_back_onto_the_departure_pad)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    walker* a = add_treasure(tw, FAMILY_TELEPORTER, 100, 100, 4);
    walker* b = add_treasure(tw, FAMILY_TELEPORTER, 300, 300, 4);
    walker* camper = add_living(tw, FAMILY_ORC, 1, 300, 300);
    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 700, 700);
    ASSERT_TRUE(a && b && camper && hero);

    stand_on(hero, a);
    EXPECT_TRUE(eat(a, hero));
    EXPECT_EQ(b, a->leader());
    EXPECT_EQ(100, hero->xpos()) << "blocked: back on the departure pad";
    EXPECT_EQ(100, hero->ypos());
    EXPECT_EQ(0, flashes(tw)) << "blocked: no flash";

    camper->setxy(800, 800);
    stand_on(hero, a);
    EXPECT_TRUE(eat(a, hero));
    EXPECT_EQ(300, hero->xpos()) << "clear: on the partner";
    EXPECT_EQ(300, hero->ypos());
    EXPECT_EQ(1, flashes(tw)) << "clear: one flash";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// treasure_valuables.lua — the bars and the key (classic: treasure.cpp
// treasure::eat_me FAMILY_GOLD_BAR / FAMILY_SILVER_BAR / FAMILY_KEY in the
// old game: `team_num == 0 || myguy`, 200 and 50 per level, and the key line
// `myguy ? myguy->name : stats->name`)
// ===========================================================================

namespace {

struct BarResult {
    std::uint32_t score1 = 0;
    short dead = 0;
    int money = 0;
};

BarResult eat_bar(int family, bool hero)
{
    TestGameWorld tw;
    open_field(tw);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 96, 96);
    walker* bar = add_treasure(tw, family, 96, 96, 3);
    BarResult r;
    if (orc == nullptr || bar == nullptr)
        return r;
    if (hero)
        make_hero(orc);
    tw.world().m_score[1] = 1000;
    (void)eat(bar, orc);
    r.score1 = tw.world().m_score[1];
    r.dead = bar->dead();
    r.money = count_sound(tw, SOUND_MONEY);
    return r;
}

}  // namespace

// treasure_valuables.lua:16. A hero on a team other than 0 cashes a gold
// bar: 200 per level to its own team, the bar consumed with the money
// sound; the guyless twin leaves it.
// Break: `eater.team == 0 or eater:has_guy()` -> `eater.team == 0`.
TEST(SemValuables, a_hero_of_any_team_cashes_a_gold_bar)
{
    og::test::ScopedHookFailureGuard guard;
    const BarResult hero = eat_bar(FAMILY_GOLD_BAR, true);
    EXPECT_EQ(1000u + 600u, hero.score1) << "hero on team 1: +200 * 3";
    EXPECT_EQ(1, hero.dead) << "hero: the bar is taken";
    EXPECT_EQ(1, hero.money) << "hero: one money sound";
    const BarResult bot = eat_bar(FAMILY_GOLD_BAR, false);
    EXPECT_EQ(1000u, bot.score1) << "guyless team-1 eater: no score";
    EXPECT_EQ(0, bot.dead) << "guyless team-1 eater: the bar stays";
    EXPECT_EQ(0, bot.money);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// treasure_valuables.lua:27. The silver bar, same rule at 50 per level: a
// team-1 hero banks 150 and takes it; a guyless team-1 eater leaves it, no
// score, no sound.
// Breaks: `eater.team == 0 or eater:has_guy()` -> `eater.team == 0` (hero
// arm red); -> `true` (bot arm red).
TEST(SemValuables, a_hero_cashes_a_silver_bar_a_guyless_monster_leaves_it)
{
    og::test::ScopedHookFailureGuard guard;
    const BarResult hero = eat_bar(FAMILY_SILVER_BAR, true);
    EXPECT_EQ(1000u + 150u, hero.score1) << "hero on team 1: +50 * 3";
    EXPECT_EQ(1, hero.dead) << "hero: the bar is taken";
    EXPECT_EQ(1, hero.money) << "hero: one money sound";
    const BarResult bot = eat_bar(FAMILY_SILVER_BAR, false);
    EXPECT_EQ(1000u, bot.score1) << "team-1 bot: no score";
    EXPECT_EQ(0, bot.dead) << "team-1 bot: the bar stays";
    EXPECT_EQ(0, bot.money) << "team-1 bot: no sound";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// treasure_valuables.lua:67. The key line names a hero by its character
// name and anyone else by its stats name.
// Breaks: `if eater:has_guy() then` -> `if false then` (hero arm red);
// -> `if true then` (guyless arm red: g_name on a guyless eater).
TEST(SemValuables, the_key_line_names_a_hero_by_its_guy_and_others_by_their_stats_name)
{
    og::test::ScopedHookFailureGuard guard;
    for (bool hero : {true, false}) {
        SCOPED_TRACE(hero ? "hero" : "guyless");
        TestGameWorld tw;
        open_field(tw);
        walker* eater = add_living(tw, FAMILY_SOLDIER, 0, 96, 96);
        walker* key = add_treasure(tw, FAMILY_KEY, 96, 96, 3);
        ASSERT_TRUE(eater && key);
        eater->stats()->name = "Grunt";
        eater->set_keys(0);
        if (hero)
            make_hero(eater, 0, "Gorik");
        EXPECT_TRUE(eat(key, eater));
        const auto notes = events_of(tw, og::sim::EventKind::Notification);
        ASSERT_EQ(1u, notes.size());
        EXPECT_EQ(hero ? "Gorik picks up key 3" : "Grunt picks up key 3",
                  notes[0].text);
        EXPECT_EQ(8, eater->keys());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// weapon_animate.lua — tree/blood, circle of protection, glow, sprinkle
// (classic: weap.cpp weap::animate FAMILY_TREE/BLOOD/CIRCLE_PROTECTION/GLOW
// and walker.cpp's FAMILY_SPRINKLE hit in the old game)
// ===========================================================================

namespace {

// Probe animation rows, 32 of them (facing + type * 8): type 0 rows
// {3, -1}, type 1 rows {5, 6, -1}, type 2 rows {1, 2, -1}.
signed char g_type0_row[] = {3, -1};
signed char g_type1_row[] = {5, 6, -1};
signed char g_type2_row[] = {1, 2, -1};
const signed char* g_probe_rows[32] = {};

const signed char* const* probe_rows()
{
    for (int i = 0; i < 32; ++i)
        g_probe_rows[i] = i < 8 ? g_type0_row : (i < 16 ? g_type1_row : g_type2_row);
    return g_probe_rows;
}

// Trees and blood splats declare no sprite, so the loader gives them no
// frames and set_frame() would refuse every frame; a ten-frame stand-in
// lets the frame the hook picks be read back.
const PixieData& ten_frames()
{
    static const PixieData data(10, 16, 16, new unsigned char[10 * 16 * 16]());
    return data;
}

walker* add_weapon(TestGameWorld& tw, int family)
{
    walker* w = tw.world().add_ob(Order::Weapon, family);
    if (w == nullptr)
        return nullptr;
    if (family == FAMILY_TREE || family == FAMILY_BLOOD)
        w->set_data(ten_frames());
    w->ani = probe_rows();
    w->ani_count = 32;
    w->set_curdir(0);
    w->set_cycle(0);
    return w;
}

}  // namespace

// weapon_animate.lua:63. A tree or blood splat on its sway/splash row
// (type 1) plays it once frame by frame — frame 5 with the type kept, then
// frame 6 — and returns to the still row at cycle 0 when the row ends.
// Break: the row-end `self:set_ani_type(0)` -> deleted (type stays 1).
TEST(SemWeaponAnimate, a_tree_or_blood_plays_its_sway_once_then_stands_still)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    for (int family : {FAMILY_TREE, FAMILY_BLOOD}) {
        SCOPED_TRACE("family " + std::to_string(family));
        TestGameWorld tw;
        open_field(tw);
        walker* w = add_weapon(tw, family);
        ASSERT_NE(nullptr, w);
        w->set_ani_type(1);
        ASSERT_TRUE(w->animate());
        EXPECT_EQ(5, w->frame()) << "mid-row: frame row[type 1][0]";
        EXPECT_EQ(1, w->cycle()) << "mid-row: cycle advanced";
        EXPECT_EQ(1, w->ani_type()) << "mid-row: type kept";
        ASSERT_TRUE(w->animate());
        EXPECT_EQ(6, w->frame()) << "the row's last frame";
        EXPECT_EQ(0, w->ani_type()) << "row end: back to still";
        EXPECT_EQ(0, w->cycle()) << "row end: cycle 0";
        EXPECT_EQ(0, w->dead());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_animate.lua:77. A protection ring vanishes the moment its owner is
// dead or its own hitpoints are spent (the caller runs its death); with a
// live owner and hitpoints left it stays and is re-centred on the owner.
// Breaks: `or owner:dead() ~= 0` -> deleted (owner-dead arm red);
// `or self.hp <= 0` -> deleted (hp arm red).
TEST(SemWeaponAnimate, a_protection_ring_dies_with_its_owner_or_its_hitpoints)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    struct Case { bool owner_dead; float hp; short want_dead; const char* arm; };
    for (const Case c : {Case{true, 5.0f, 1, "owner dead"},
                         Case{false, 0.0f, 1, "ring hp 0"},
                         Case{false, 5.0f, 0, "live owner, hp left"}}) {
        SCOPED_TRACE(c.arm);
        TestGameWorld tw;
        open_field(tw);
        walker* owner = add_living(tw, FAMILY_SOLDIER, 0, 180, 188);
        walker* ring = add_weapon(tw, FAMILY_CIRCLE_PROTECTION);
        ASSERT_TRUE(owner && ring);
        ring->set_owner(owner);
        ring->stats()->set_hitpoints(c.hp);
        ring->setxy(40, 40);
        if (c.owner_dead)
            owner->set_dead(1);
        (void)ring->animate();
        EXPECT_EQ(c.want_dead, ring->dead()) << c.arm;
        EXPECT_EQ(c.want_dead, ring->death_called()) << c.arm << ": the caller ran death()";
        if (c.want_dead == 0) {
            EXPECT_EQ(owner->xpos() + owner->sizex() / 2,
                      ring->xpos() + ring->sizex() / 2) << "re-centred on the owner";
            EXPECT_EQ(owner->ypos() + owner->sizey() / 2,
                      ring->ypos() + ring->sizey() / 2);
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_animate.lua:90. A glow on its pulse row (type 2) advances frame by
// frame with the type kept, and the row end restarts the pulse at cycle 0.
// Break: weapon_animate.lua:93 `if weapon_animate_step(self) then` ->
// `if weapon_animate_step(self) or true then` (every step restarts the pulse:
// cycle 0 where the mid-row step reads 1).
TEST(SemWeaponAnimate, a_glow_pulses_frame_by_frame_and_restarts_at_the_row_end)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    TestGameWorld tw;
    open_field(tw);
    walker* glow = add_weapon(tw, FAMILY_GLOW);
    ASSERT_NE(nullptr, glow);
    glow->set_ani_type(2);
    glow->set_lifetime(100);
    ASSERT_TRUE(glow->animate());
    EXPECT_EQ(1, glow->frame()) << "mid-row: row[type 2][0]";
    EXPECT_EQ(1, glow->cycle()) << "mid-row: cycle advanced";
    EXPECT_EQ(2, glow->ani_type()) << "mid-row: type kept";
    ASSERT_TRUE(glow->animate());
    EXPECT_EQ(2, glow->frame()) << "the pulse's last frame";
    EXPECT_EQ(0, glow->cycle()) << "row end: the pulse restarts";
    EXPECT_EQ(2, glow->ani_type());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_animate.lua:99. A glow burns one lifetime tick per animate and
// dies on the animate that finds it already below 1: lifetime 1 survives
// one animate (lifetime 0, alive), then dies with its death run.
// Breaks: `if lifetime < 1 then` -> `if lifetime < 2 then` (survive arm
// red); the hook's own `self:death()` -> deleted (death_called stays 0).
TEST(SemWeaponAnimate, a_glow_with_lifetime_n_shows_for_n_plus_one_animates)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    TestGameWorld tw;
    open_field(tw);
    walker* glow = add_weapon(tw, FAMILY_GLOW);
    ASSERT_NE(nullptr, glow);
    glow->set_ani_type(2);
    glow->set_lifetime(1);
    ASSERT_TRUE(glow->animate());
    EXPECT_EQ(0, glow->lifetime()) << "lifetime 1: one tick burned";
    EXPECT_EQ(0, glow->dead()) << "lifetime 1: still alive";
    EXPECT_EQ(0, glow->death_called());
    ASSERT_TRUE(glow->animate());
    EXPECT_EQ(1, glow->dead()) << "lifetime 0: dies";
    EXPECT_EQ(1, glow->death_called()) << "lifetime 0: its death ran";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

struct SprinkleScene {
    TestGameWorld tw;
    walker* faerie = nullptr;
    walker* sprinkle = nullptr;
    walker* victim = nullptr;

    explicit SprinkleScene(short owner_level)
    {
        og::test::mount_core_pack();
        open_field(tw);
        faerie = add_living(tw, FAMILY_FAERIE, 1, 96, 96, owner_level);
        victim = add_living(tw, FAMILY_SOLDIER, 0, 128, 96);
        sprinkle = tw.world().add_ob(Order::Weapon, FAMILY_SPRINKLE);
        if (sprinkle != nullptr)
            sprinkle->set_owner(faerie);
    }

    void hit()
    {
        (void)og::test::on_hit_target(
            *get_weapon_family_descriptor(FAMILY_SPRINKLE), sprinkle, victim,
            faerie);
    }
};

}  // namespace

// weapon_animate.lua:115. A hero's constitution shortens a sprinkle freeze:
// from state 1 the first draw is 16838; an L6 faerie's bound is 40 + 12 =
// 52 for a guyless victim (16838 % 52 = 42) and 52 - 210/21 = 42 for a
// constitution-210 hero (16838 % 42 = 38). The old game's
// `random(FAERIE_FREEZE_TIME + level*2 - constitution/21)` is the same.
// Break: `if target:has_guy() then` -> `if false then` (the hero rolls 42).
TEST(SemWeaponAnimate, a_heros_constitution_shortens_a_sprinkle_freeze)
{
    og::test::ScopedHookFailureGuard guard;
    for (bool hero : {false, true}) {
        SCOPED_TRACE(hero ? "hero" : "guyless");
        SprinkleScene s(6);
        ASSERT_TRUE(s.faerie && s.sprinkle && s.victim);
        if (hero)
            make_hero(s.victim, 210);
        s.victim->stats()->set_frozen_delay(0);
        s.tw.world().rng_.state_ = 1u;
        s.hit();
        EXPECT_EQ(hero ? 38 : 42, s.victim->stats()->frozen_delay());
        EXPECT_EQ(state_after(1u, {hero ? 42u : 52u}), s.tw.world().rng_.state_)
            << "one draw of the bound";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// weapon_animate.lua:128. A level-21+ faerie's sprinkle re-sets a freeze at
// or below the 10-tick floor (10 -> the roll) and keeps one above it (11
// stays 11); the roll is drawn either way. The old game always re-sets, so
// the floor case is where the two agree.
// Break: `target:s_frozen_delay() > SPRINKLE_REFRESH_FLOOR` -> `>=`.
TEST(SemWeaponAnimate, a_high_level_sprinkle_refreshes_a_freeze_at_the_ten_tick_floor)
{
    og::test::ScopedHookFailureGuard guard;
    // L21, con 0: bound 40 + 42 = 82; a roll of 60 is below the 79 knee.
    const std::uint32_t pin =
        state_where([](og::sim::SimRandom& r) { return r.next(82) == 60; });
    ASSERT_NE(0u, pin);
    struct Case { short before; short want; const char* arm; };
    for (const Case c : {Case{10, 60, "frozen 10: refreshed"},
                         Case{11, 11, "frozen 11: kept"}}) {
        SCOPED_TRACE(c.arm);
        SprinkleScene s(21);
        ASSERT_TRUE(s.faerie && s.sprinkle && s.victim);
        s.victim->stats()->set_frozen_delay(c.before);
        s.tw.world().rng_.state_ = pin;
        s.hit();
        EXPECT_EQ(c.want, s.victim->stats()->frozen_delay()) << c.arm;
        EXPECT_EQ(state_after(pin, {82}), s.tw.world().rng_.state_)
            << "one draw either way";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
