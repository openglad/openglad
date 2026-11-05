/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch families-2: the rules the review found in living-03-mage.lua, living-05-cleric.lua, living-06-elemental.lua, living-08-slime.lua.
// Each test is named after the rule it asserts.
//
// Every cast goes through the real walker::special() (the price gate and the
// charge included) against the shipped core pack, in a hand-built open grass
// field. Classic arms were read against the old game's walker::special,
// walker::death, living::act and guy.cpp at 7ff1654a.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_family_lookup.h"
#include "../test_game_world_fixture.h"
#include "../test_sim_random_scope.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using Failure = walker::SpecialFailure;

// Answers every bounded draw with its midpoint, (n - 1) / 2. walker::fire()
// draws its waver as next(w + 1) - w / 2, so under this stream every fired
// weapon flies with zero waver: a cardinal bolt has exactly one zero
// component.
class MidpointRandom : public IRandom {
public:
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        return max_exclusive == 0 ? 0 : (max_exclusive - 1) / 2;
    }
};

// An open grass field, 1024 px a side, nobody on it, the player's team 0.
struct Field {
    std::unique_ptr<TestGameWorld> tw;
    og::test::ScopedHookFailureGuard guard;

    explicit Field(short fixes = 0)
    {
        og::test::mount_core_pack();
        tw = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = fixes;
        w.allied_mode = 0;
        w.enemy_freeze = 0;
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw->world(); }
    og::sim::SimEventLog& events() { return tw->events; }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // A bot (no seat, no character) with 100/100 hp and no mana.
    walker* living_at(int family, unsigned char team, int x, int y,
                      short level = 4)
    {
        walker* ob = world().add_ob(Order::Living, family);
        if (ob == nullptr)
            return nullptr;
        ob->set_team_num(team);
        ob->set_real_team_num(255);
        ob->setxy(static_cast<short>(x + 1), static_cast<short>(y));
        ob->setxy(static_cast<short>(x), static_cast<short>(y));
        ob->stats()->set_level(level);
        ob->stats()->set_max_hitpoints(100.0f);
        ob->stats()->set_hitpoints(100.0f);
        ob->stats()->set_max_magicpoints(0.0f);
        ob->stats()->set_magicpoints(0.0f);
        ob->set_busy(0);
        ob->set_user(-1);
        return ob;
    }

    // A caster on `slot`, unshifted, with `mp` of `mp` mana.
    walker* caster(int family, unsigned char team, int x, int y, short level,
                   float mp, int slot)
    {
        walker* ob = living_at(family, team, x, y, level);
        if (ob == nullptr)
            return nullptr;
        ob->stats()->set_max_magicpoints(mp);
        ob->stats()->set_magicpoints(mp);
        ob->set_current_special(static_cast<char>(slot));
        ob->set_shifter_down(0);
        return ob;
    }

    // Every live entity of an order and family, in all three lists.
    std::vector<walker*> live(Order order, int family)
    {
        std::vector<walker*> out;
        for (const GameWorld::EntityList* list :
             {&world().oblist, &world().fxlist, &world().weaplist}) {
            for (const auto& ob : *list) {
                if (ob != nullptr && ob->dead() == 0 &&
                    ob->query_order() == order &&
                    static_cast<int>(static_cast<unsigned char>(ob->family())) ==
                        family)
                    out.push_back(ob.get());
            }
        }
        return out;
    }

    int notifications(const char* needle)
    {
        int n = 0;
        for (const auto& ev : events().events())
            if (ev.kind == og::sim::EventKind::Notification &&
                ev.text.find(needle) != std::string::npos)
                n++;
        return n;
    }

    int all_notifications()
    {
        int n = 0;
        for (const auto& ev : events().events())
            if (ev.kind == og::sim::EventKind::Notification)
                n++;
        return n;
    }

    int events_of(og::sim::EventKind kind, std::uint32_t a)
    {
        int n = 0;
        for (const auto& ev : events().events())
            if (ev.kind == kind && ev.a == a)
                n++;
        return n;
    }
};

void give_character(walker* w, int family, short intelligence)
{
    auto g = std::make_unique<guy>(family);
    g->intelligence = intelligence;
    g->teamnum = w->team_num();
    g->exp = 0;
    g->total_shots = 0;
    g->scen_shots = 0;
    w->set_owned_myguy(std::move(g));
}

int sign(float v) { return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0); }

std::set<std::pair<int, int>> headings(const std::vector<walker*>& bolts)
{
    std::set<std::pair<int, int>> out;
    for (walker* b : bolts)
        out.insert({sign(b->lastx()), sign(b->lasty())});
    return out;
}

const std::set<std::pair<int, int>> kEightHeadings = {
    {-1, -1}, {-1, 0}, {-1, 1}, {0, -1}, {0, 1}, {1, -1}, {1, 0}, {1, 1}};

}  // namespace

// ---------------------------------------------------------------------------
// living-03-mage.lua
// ---------------------------------------------------------------------------

// :185 bot arm. A bot's marker holds level/4 + 1 uses (a hero's INT/33 is
// pinned in test_walker_specials). Two levels pin the formula, not a value.
TEST(SemMage, a_bot_marker_holds_a_quarter_of_the_level_plus_one_uses)
{
    for (const auto& [level, uses] : {std::pair<short, int>{9, 3},
                                      std::pair<short, int>{3, 1}}) {
        Field f;
        walker* mage = f.caster(FAMILY_MAGE, 1, 160, 160, level, 600.0f, 1);
        ASSERT_NE(nullptr, mage);
        mage->set_shifter_down(1);
        Failure why = Failure::None;
        std::string reason;
        ASSERT_TRUE(mage->special(&why, &reason)) << reason;
        const auto markers = f.live(Order::FX, FAMILY_MARKER);
        ASSERT_EQ(1u, markers.size()) << "level " << level;
        EXPECT_EQ(uses, markers[0]->lifetime())
            << "bot arm: a level-" << level << " bot's marker has level/4+1 uses";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :191 bot arm. A bot placing a marker says nothing: neither a foreign bot
// nor a player-team bot without a seat.
TEST(SemMage, a_bot_placing_a_marker_announces_nothing)
{
    for (unsigned char team : {static_cast<unsigned char>(1),
                               static_cast<unsigned char>(0)}) {
        Field f;
        walker* mage = f.caster(FAMILY_MAGE, team, 160, 160, 8, 600.0f, 1);
        ASSERT_NE(nullptr, mage);
        mage->set_shifter_down(1);
        ASSERT_TRUE(mage->special());
        ASSERT_EQ(1u, f.live(Order::FX, FAMILY_MARKER).size());
        EXPECT_EQ(0, f.notifications("Teleport Marker Placed"))
            << "bot arm (team " << int(team) << "): no placement notice";
        EXPECT_EQ(0, f.notifications("Uses)"))
            << "bot arm (team " << int(team) << "): no uses notice";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

namespace {

// The plain fireball a twin mage of the same level fires along each of the
// eight headings (zero waver): the base damage and line of sight a WARP SPACE
// bolt starts from. Cardinal bolts carry the 1.207 "circular range" scale, so
// the base is per heading.
std::map<std::pair<int, int>, std::pair<float, int>> base_fireballs(short level)
{
    MidpointRandom midpoint;
    ScopedSimRandom scoped(&midpoint);
    std::map<std::pair<int, int>, std::pair<float, int>> out;
    Field f;
    walker* twin = f.caster(FAMILY_MAGE, 0, 160, 160, level, 1000.0f, 2);
    if (twin == nullptr)
        return out;
    for (const auto& h : kEightHeadings) {
        twin->set_lastx(static_cast<float>(h.first));
        twin->set_lasty(static_cast<float>(h.second));
        walker* bolt = twin->fire();
        if (bolt == nullptr)
            continue;
        out[{sign(bolt->lastx()), sign(bolt->lasty())}] = {
            bolt->damage(), static_cast<int>(bolt->lineofsight())};
        bolt->set_dead(1);
    }
    return out;
}

}  // namespace

// :208. WARP SPACE spends spare magic: min(spare/15, 40) on every bolt's
// damage and a third of it on range, and the mage pays that bonus once
// more; with no spare the bolts are plain.
TEST(SemMage, warp_space_puts_the_spare_magic_into_every_bolt)
{
    const auto base = base_fireballs(5);
    ASSERT_EQ(8u, base.size()) << "control: a twin mage fires every heading";

    MidpointRandom midpoint;
    ScopedSimRandom scoped(&midpoint);  // zero waver: the heading keys the base
    {   // spare > 0: 360 MP, spare 300, bonus 20, range +6.
        Field f;
        walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 5, 360.0f, 2);
        ASSERT_NE(nullptr, mage);
        ASSERT_TRUE(mage->special());
        const auto bolts = f.live(Order::Weapon, FAMILY_FIREBALL);
        ASSERT_EQ(8u, bolts.size());
        for (walker* b : bolts) {
            const auto& [dmg, los] = base.at({sign(b->lastx()), sign(b->lasty())});
            EXPECT_FLOAT_EQ(dmg + 20.0f, b->damage())
                << "spare>0 arm: every bolt gains spare/15 damage";
            EXPECT_EQ(los + 6, static_cast<int>(b->lineofsight()))
                << "spare>0 arm: every bolt gains a third of it in range";
        }
        EXPECT_FLOAT_EQ(360.0f - 20.0f - 60.0f, mage->stats()->magicpoints())
            << "spare>0 arm: the mage pays the bonus once beside the price";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
    {   // spare == 0: exactly the price.
        Field f;
        walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 5, 60.0f, 2);
        ASSERT_NE(nullptr, mage);
        ASSERT_TRUE(mage->special());
        const auto bolts = f.live(Order::Weapon, FAMILY_FIREBALL);
        ASSERT_EQ(8u, bolts.size());
        for (walker* b : bolts) {
            const auto& [dmg, los] = base.at({sign(b->lastx()), sign(b->lasty())});
            EXPECT_FLOAT_EQ(dmg, b->damage())
                << "spare<=0 arm: the bolts are plain";
            EXPECT_EQ(los, static_cast<int>(b->lineofsight()))
                << "spare<=0 arm: the range is plain";
        }
        EXPECT_FLOAT_EQ(0.0f, mage->stats()->magicpoints())
            << "spare<=0 arm: the price only";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :220. One fireball along each of the eight headings, and the mage's own
// facing comes back afterwards, truncated toward zero as the C int temps did.
TEST(SemMage, warp_space_fires_all_eight_headings_and_restores_the_facing)
{
    MidpointRandom midpoint;
    ScopedSimRandom scoped(&midpoint);  // zero waver: the sign is the heading
    Field f;
    walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 5, 600.0f, 2);
    ASSERT_NE(nullptr, mage);
    mage->set_lastx(-3.7f);
    mage->set_lasty(1.2f);
    ASSERT_TRUE(mage->special());
    const auto bolts = f.live(Order::Weapon, FAMILY_FIREBALL);
    ASSERT_EQ(8u, bolts.size());
    EXPECT_EQ(kEightHeadings, headings(bolts))
        << "8 headings arm: one bolt per non-null heading";
    EXPECT_FLOAT_EQ(-3.0f, mage->lastx())
        << "8 headings arm: the facing x is restored (C truncation)";
    EXPECT_FLOAT_EQ(1.0f, mage->lasty())
        << "8 headings arm: the facing y is restored (C truncation)";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :226. A heading whose pad is a wall is simply missing; the cast stands.
TEST(SemMage, a_warp_space_bolt_with_a_walled_pad_is_missing_and_the_cast_stands)
{
    MidpointRandom midpoint;
    ScopedSimRandom scoped(&midpoint);  // zero waver: the sign is the heading
    Field f;
    walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 5, 600.0f, 2);
    ASSERT_NE(nullptr, mage);
    // The north bolt's pad sits in the tile row above the mage, centred.
    f.tile(10, 9, PIX_H_WALL1);
    Failure why = Failure::None;
    std::string reason;
    ASSERT_TRUE(mage->special(&why, &reason))
        << "pad blocked arm: the cast still succeeds (" << reason << ")";
    const auto bolts = f.live(Order::Weapon, FAMILY_FIREBALL);
    EXPECT_EQ(7u, bolts.size()) << "pad blocked arm: seven bolts";
    auto expected = kEightHeadings;
    expected.erase({0, -1});
    EXPECT_EQ(expected, headings(bolts))
        << "pad blocked arm: only the walled north heading is missing";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :231. Every non-zero velocity component of a WARP SPACE bolt is set to
// +-1; a zero component stays zero.
TEST(SemMage, warp_space_bolts_crawl_one_pixel_per_axis)
{
    MidpointRandom midpoint;
    ScopedSimRandom scoped(&midpoint);  // zero waver: cardinals keep a zero axis
    Field f;
    walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 5, 600.0f, 2);
    ASSERT_NE(nullptr, mage);
    ASSERT_TRUE(mage->special());
    const auto bolts = f.live(Order::Weapon, FAMILY_FIREBALL);
    ASSERT_EQ(8u, bolts.size());
    for (walker* b : bolts) {
        const int sx = sign(b->lastx());
        const int sy = sign(b->lasty());
        EXPECT_FLOAT_EQ(static_cast<float>(sx), b->lastx())
            << "x component: non-zero becomes +-1, zero stays 0";
        EXPECT_FLOAT_EQ(static_cast<float>(sy), b->lasty())
            << "y component: non-zero becomes +-1, zero stays 0";
    }
    EXPECT_EQ(kEightHeadings, headings(bolts))
        << "cardinals keep exactly one zero component, diagonals none";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :255 the caster-on-my-team arm, staged where the old game agrees (a team-0
// mage, my_team 0): the time stop is banked AND the freeze palette is
// requested, once.
TEST(SemMage, a_player_team_freeze_time_banks_the_stop_and_asks_for_the_blue_palette)
{
    Field f;
    walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 4, 600.0f, 3);
    ASSERT_NE(nullptr, mage);
    ASSERT_TRUE(mage->special());
    EXPECT_EQ(64, f.world().enemy_freeze) << "on-team arm: 20 + 11*4";
    EXPECT_EQ(1, f.events_of(og::sim::EventKind::SetPalette, 1))
        << "on-team arm: exactly one blue-palette request";
    EXPECT_EQ(0, f.notifications("TIME IS FROZEN"))
        << "on-team arm: no bonus-round notice";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :281. ENERGY WAVE rides a fired fireball: the wave takes the bolt's
// heading and the bolt is gone; with no bolt the cast fails.
TEST(SemMage, energy_wave_replaces_its_fireball_and_fails_when_none_fires)
{
    {   // fire ok
        MidpointRandom midpoint;
        ScopedSimRandom scoped(&midpoint);
        Field f;
        walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 6, 600.0f, 4);
        ASSERT_NE(nullptr, mage);
        mage->set_lastx(2);
        mage->set_lasty(0);
        // The heading a plain fireball takes from this facing.
        walker* twin = f.caster(FAMILY_MAGE, 0, 400, 400, 6, 100.0f, 4);
        ASSERT_NE(nullptr, twin);
        twin->set_lastx(2);
        twin->set_lasty(0);
        walker* probe = twin->fire();
        ASSERT_NE(nullptr, probe);
        const float bolt_x = probe->lastx();
        const float bolt_y = probe->lasty();
        probe->set_dead(1);
        ASSERT_NE(0.0f, bolt_x);

        ASSERT_TRUE(mage->special());
        const auto waves = f.live(Order::Weapon, FAMILY_WAVE);
        ASSERT_EQ(1u, waves.size());
        EXPECT_FLOAT_EQ(bolt_x, waves[0]->lastx()) << "fire ok arm: the bolt's x heading";
        EXPECT_FLOAT_EQ(bolt_y, waves[0]->lasty()) << "fire ok arm: the bolt's y heading";
        EXPECT_EQ(mage, waves[0]->owner()) << "fire ok arm: owned by the mage";
        EXPECT_EQ(6, waves[0]->stats()->level()) << "fire ok arm: at the mage's level";
        EXPECT_EQ(0u, f.live(Order::Weapon, FAMILY_FIREBALL).size())
            << "fire ok arm: the carrier fireball is dead";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
    {   // fire nil: facing north into a wall
        Field f;
        walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 6, 600.0f, 4);
        ASSERT_NE(nullptr, mage);
        mage->set_lastx(0);
        mage->set_lasty(-2);
        f.tile(10, 9, PIX_H_WALL1);
        Failure why = Failure::None;
        std::string reason;
        EXPECT_FALSE(mage->special(&why, &reason)) << "fire nil arm: refused";
        EXPECT_EQ(Failure::ScriptDeclined, why);
        EXPECT_EQ("COULD NOT FIRE", reason) << "fire nil arm: its reason";
        EXPECT_EQ(0u, f.live(Order::Weapon, FAMILY_WAVE).size())
            << "fire nil arm: no wave";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :304. HEARTBURST with no foe in its reach is refused for free.
TEST(SemMage, heartburst_with_no_foe_in_reach_is_refused_for_free)
{
    Field f;
    walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 5, 1500.0f, 5);
    ASSERT_NE(nullptr, mage);
    // Reach is 80 + 2*5 = 90 px; the only foe stands 200 px off.
    ASSERT_NE(nullptr, f.living_at(FAMILY_ORC, 1, 360, 160));
    Failure why = Failure::None;
    std::string reason;
    EXPECT_FALSE(mage->special(&why, &reason));
    EXPECT_EQ(Failure::ScriptDeclined, why);
    EXPECT_EQ("NO FOE IN RANGE", reason) << "no foe arm: its reason";
    EXPECT_FLOAT_EQ(1500.0f, mage->stats()->magicpoints()) << "no foe arm: free";
    EXPECT_FLOAT_EQ(0.0f, mage->busy()) << "no foe arm: no busy";
    EXPECT_EQ(0u, f.live(Order::FX, FAMILY_EXPLOSION).size())
        << "no foe arm: no explosion";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :310. A hero's HEARTBURST counts one shot per foe burst; a bot keeps no
// stats and still bursts.
TEST(SemMage, a_hero_heartburst_counts_one_shot_per_foe)
{
    for (bool hero : {true, false}) {
        Field f;
        walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 5, 1000.0f, 5);
        ASSERT_NE(nullptr, mage);
        if (hero)
            give_character(mage, FAMILY_MAGE, 100);
        ASSERT_NE(nullptr, f.living_at(FAMILY_ORC, 1, 200, 160));
        ASSERT_NE(nullptr, f.living_at(FAMILY_ORC, 1, 160, 200));
        ASSERT_NE(nullptr, f.living_at(FAMILY_ORC, 1, 120, 160));
        ASSERT_TRUE(mage->special());
        EXPECT_EQ(3u, f.live(Order::FX, FAMILY_EXPLOSION).size())
            << (hero ? "hero" : "bot") << " arm: three bursts";
        if (hero) {
            EXPECT_EQ(3u, mage->myguy->total_shots) << "hero arm: total_shots += 3";
            EXPECT_EQ(3u, mage->myguy->scen_shots) << "hero arm: scen_shots += 3";
        } else {
            EXPECT_EQ(nullptr, mage->myguy) << "bot arm: no character";
        }
        EXPECT_EQ(0u, f.guard.count()) << (hero ? "hero" : "bot") << " arm: "
                                       << f.guard.message();
    }
}

// :316. One magical explosion on each foe, each an equal share of half the
// spare pool, centred on its foe, sparing the caster; the mage pays each
// share, takes 5 busy, and each burst sounds once.
TEST(SemMage, heartburst_bursts_each_foe_for_an_equal_share_of_half_the_spare_pool)
{
    Field f;
    walker* mage = f.caster(FAMILY_MAGE, 0, 160, 160, 4, 1000.0f, 5);
    ASSERT_NE(nullptr, mage);
    walker* east = f.living_at(FAMILY_ORC, 1, 200, 160);
    walker* south = f.living_at(FAMILY_ORC, 1, 160, 204);
    ASSERT_TRUE(east && south);
    ASSERT_TRUE(mage->special());
    // spare = 1000 - 100 = 900, half 450 (under the 600 cap), 2 foes: 225.
    const auto bursts = f.live(Order::FX, FAMILY_EXPLOSION);
    ASSERT_EQ(2u, bursts.size());
    std::set<walker*> centred_on;
    for (walker* b : bursts) {
        EXPECT_FLOAT_EQ(225.0f, b->damage()) << "n foes arm: the equal share";
        EXPECT_TRUE(b->stats()->query_bit_flags(BIT_MAGICAL))
            << "n foes arm: magical";
        EXPECT_EQ(100, b->skip_exit()) << "n foes arm: spares the caster";
        for (walker* foe : {east, south}) {
            if (b->xpos() + b->sizex() / 2 == foe->xpos() + foe->sizex() / 2 &&
                b->ypos() + b->sizey() / 2 == foe->ypos() + foe->sizey() / 2)
                centred_on.insert(foe);
        }
    }
    EXPECT_EQ(2u, centred_on.size()) << "n foes arm: one burst centred on each foe";
    EXPECT_FLOAT_EQ(1000.0f - 2 * 225.0f - 100.0f, mage->stats()->magicpoints())
        << "n foes arm: each share and the price are paid";
    EXPECT_FLOAT_EQ(5.0f, mage->busy()) << "n foes arm: 5 busy";
    EXPECT_EQ(2, f.events_of(og::sim::EventKind::PlaySound, static_cast<std::uint32_t>(SOUND_EXPLODE)))
        << "n foes arm: one explosion sound per burst";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// ---------------------------------------------------------------------------
// living-05-cleric.lua
// ---------------------------------------------------------------------------

namespace {

// A bloodstain (Order::Treasure FAMILY_STAIN, filed in fxlist where
// find_nearest_blood looks) on the first passable spot east, south, west or
// north of the caster that is inside `reach`.
walker* stain_in_reach(Field& f, walker* caster, unsigned char team,
                       char old_family, std::int32_t reach, short level = 1)
{
    walker* stain = f.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (stain == nullptr)
        return nullptr;
    stain->set_team_num(team);
    stain->set_real_team_num(team);
    stain->stats()->set_old_family(old_family);
    stain->stats()->set_level(level);
    stain->set_dead(0);
    for (std::int32_t d = 1; d < reach; d++) {
        const std::int32_t ring[4][2] = {{d, 0}, {0, d}, {-d, 0}, {0, -d}};
        for (const auto& o : ring) {
            const float x = static_cast<float>(caster->xpos() + o[0]);
            const float y = static_cast<float>(caster->ypos() + o[1]);
            stain->setxy(x, y);
            if (caster->distance_to_ob(stain) < reach &&
                f.world().query_passable(x, y, stain))
                return stain;
        }
    }
    ADD_FAILURE() << "no passable stain spot within " << reach;
    return nullptr;
}

// A cleric whose first TURN UNDEAD roll beats a level-0 undead: the roll is
// next(range*40) > next(0), and next(0) answers 0 without a draw.
void pin_turn_roll_to_land(Field& f, std::uint32_t bound)
{
    f.world().rng_ = og::sim::SimRandom(state_where_draws_are({bound}, {1u}));
}

}  // namespace

// :29. TURN UNDEAD (Shift on slots 2 and 3) refuses SPECIAL BUSY while busy,
// for free; the same rig not busy turns the undead.
TEST(SemCleric, turn_undead_is_refused_for_free_while_busy)
{
    for (int slot : {2, 3}) {
        Field f;
        walker* cleric = f.caster(FAMILY_CLERIC, 0, 160, 160, 10, 500.0f, slot);
        ASSERT_NE(nullptr, cleric);
        give_character(cleric, FAMILY_CLERIC, 100);
        cleric->set_shifter_down(1);
        cleric->set_busy(3);
        walker* undead = f.living_at(FAMILY_SKELETON, 2, 172, 160, 0);
        ASSERT_NE(nullptr, undead);
        const std::uint32_t undead_id = undead->entity_id();
        pin_turn_roll_to_land(f, 40u * 40u);

        Failure why = Failure::None;
        std::string reason;
        EXPECT_FALSE(cleric->special(&why, &reason)) << "busy arm, slot " << slot;
        EXPECT_EQ(Failure::ScriptDeclined, why);
        EXPECT_EQ("SPECIAL BUSY", reason) << "busy arm, slot " << slot << ": its reason";
        EXPECT_FLOAT_EQ(500.0f, cleric->stats()->magicpoints())
            << "busy arm, slot " << slot << ": free";
        EXPECT_FLOAT_EQ(3.0f, cleric->busy()) << "busy arm: busy untouched";
        EXPECT_EQ(0, undead->dead()) << "busy arm, slot " << slot << ": undead untouched";
        EXPECT_EQ(0u, cleric->myguy->exp) << "busy arm: no experience";

        cleric->set_busy(0);
        ASSERT_TRUE(cleric->special(&why, &reason))
            << "control, slot " << slot << ": not busy, the turn lands (" << reason << ")";
        walker* after = f.world().find_by_id(undead_id);
        ASSERT_NE(nullptr, after);
        EXPECT_EQ(1, after->dead()) << "control: the undead is turned";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :47 the two arms the hero n>0 tests leave: a hero whose classic turn finds
// foes but no undead succeeds and earns nothing, says nothing; a bot that
// turns an undead earns nothing and says nothing.
TEST(SemCleric, only_a_hero_who_turned_an_undead_is_rewarded_and_announced)
{
    {   // hero, n == 0 (the classic success with nothing turned)
        Field f(0);
        walker* cleric = f.caster(FAMILY_CLERIC, 0, 160, 160, 10, 500.0f, 2);
        ASSERT_NE(nullptr, cleric);
        give_character(cleric, FAMILY_CLERIC, 100);
        cleric->myguy->name = "Tuck";
        cleric->set_shifter_down(1);
        walker* orc = f.living_at(FAMILY_ORC, 2, 180, 160, 1);
        ASSERT_NE(nullptr, orc);
        Failure why = Failure::None;
        std::string reason;
        ASSERT_TRUE(cleric->special(&why, &reason))
            << "hero n==0 arm: the classic turn succeeds (" << reason << ")";
        EXPECT_EQ(0u, cleric->myguy->exp) << "hero n==0 arm: no experience";
        EXPECT_EQ(0, f.notifications("turned")) << "hero n==0 arm: no notice";
        EXPECT_EQ(1, f.events_of(og::sim::EventKind::PlaySound,
                                 static_cast<std::uint32_t>(SOUND_HEAL)))
            << "hero n==0 arm: the cast still sounds";
        EXPECT_EQ(0, orc->dead()) << "hero n==0 arm: the living orc is not turned";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
    for (unsigned char team : {static_cast<unsigned char>(0),
                               static_cast<unsigned char>(1)}) {
        // bot: a player-team bot (the inner team test would pass) and a
        // foreign one
        Field f(0);
        walker* cleric = f.caster(FAMILY_CLERIC, team, 160, 160, 10, 500.0f, 2);
        ASSERT_NE(nullptr, cleric);
        cleric->set_shifter_down(1);
        walker* undead = f.living_at(FAMILY_SKELETON, 2, 172, 160, 0);
        ASSERT_NE(nullptr, undead);
        pin_turn_roll_to_land(f, 40u * 40u);
        Failure why = Failure::None;
        std::string reason;
        ASSERT_TRUE(cleric->special(&why, &reason))
            << "bot arm (team " << int(team) << "): the turn lands (" << reason << ")";
        EXPECT_EQ(1, undead->dead()) << "bot arm: the undead is turned";
        EXPECT_EQ(0, f.notifications("turned"))
            << "bot arm (team " << int(team) << "): no notice";
        EXPECT_EQ(nullptr, cleric->myguy) << "bot arm: no character to reward";
        EXPECT_EQ(0u, f.guard.count()) << "bot arm (team " << int(team) << "): "
                                       << f.guard.message();
    }
}

// :123 the silence half. A refused HEAL makes no sound and no notice, for
// both refusals: every ally already whole, and a heal too weak to land.
TEST(SemCleric, a_refused_heal_is_silent)
{
    {   // nobody needs it
        Field f;
        walker* cleric = f.caster(FAMILY_CLERIC, 0, 160, 160, 1, 3.0f, 1);
        ASSERT_NE(nullptr, cleric);
        walker* ally = f.living_at(FAMILY_SOLDIER, 0, 180, 160);
        ASSERT_NE(nullptr, ally);
        Failure why = Failure::None;
        std::string reason;
        ASSERT_FALSE(cleric->special(&why, &reason));
        EXPECT_EQ("NO ALLY NEEDS HEALING", reason);
        EXPECT_FLOAT_EQ(3.0f, cleric->stats()->magicpoints());
        EXPECT_EQ(0, f.events_of(og::sim::EventKind::PlaySound,
                                 static_cast<std::uint32_t>(SOUND_HEAL)))
            << "nobody healed (silence) arm: no heal sound";
        EXPECT_EQ(0, f.all_notifications())
            << "nobody healed (silence) arm: no notice";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
    {   // too weak: level 0, trunc(3)/4 == 0 and no level bonus
        Field f;
        walker* cleric = f.caster(FAMILY_CLERIC, 0, 160, 160, 0, 3.0f, 1);
        ASSERT_NE(nullptr, cleric);
        walker* ally = f.living_at(FAMILY_SOLDIER, 0, 180, 160);
        ASSERT_NE(nullptr, ally);
        ally->stats()->set_hitpoints(40.0f);
        Failure why = Failure::None;
        std::string reason;
        ASSERT_FALSE(cleric->special(&why, &reason));
        EXPECT_EQ("HEAL TOO WEAK", reason);
        EXPECT_FLOAT_EQ(40.0f, ally->stats()->hitpoints());
        EXPECT_EQ(0, f.events_of(og::sim::EventKind::PlaySound,
                                 static_cast<std::uint32_t>(SOUND_HEAL)))
            << "too weak (silence) arm: no heal sound";
        EXPECT_EQ(0, f.all_notifications()) << "too weak (silence) arm: no notice";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :127. The heal notice counts the men: "1 man!" for one, "N men!" for more.
TEST(SemCleric, the_heal_notice_counts_one_man_or_several_men)
{
    {
        Field f;
        walker* cleric = f.caster(FAMILY_CLERIC, 0, 160, 160, 4, 100.0f, 1);
        ASSERT_NE(nullptr, cleric);
        walker* a = f.living_at(FAMILY_SOLDIER, 0, 180, 160);
        walker* b = f.living_at(FAMILY_SOLDIER, 0, 160, 180);
        ASSERT_TRUE(a && b);
        a->stats()->set_hitpoints(20.0f);
        b->stats()->set_hitpoints(30.0f);
        ASSERT_TRUE(cleric->special());
        EXPECT_EQ(1, f.notifications("Cleric healed 2 men!")) << "N>1 arm";
        EXPECT_EQ(0, f.notifications("Cleric healed 1 man!")) << "N>1 arm: not the singular";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
    {
        Field f;
        walker* cleric = f.caster(FAMILY_CLERIC, 0, 160, 160, 4, 100.0f, 1);
        ASSERT_NE(nullptr, cleric);
        walker* a = f.living_at(FAMILY_SOLDIER, 0, 180, 160);
        walker* b = f.living_at(FAMILY_SOLDIER, 0, 160, 180);
        ASSERT_TRUE(a && b);
        a->stats()->set_hitpoints(20.0f);  // b is whole
        ASSERT_TRUE(cleric->special());
        EXPECT_EQ(1, f.notifications("Cleric healed 1 man!")) << "1 arm";
        EXPECT_EQ(0, f.notifications("men!")) << "1 arm: not the plural";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :132. An enemy bot cleric's heal lands in silence; the same heal from a
// player-team cleric is announced once. Both heal 50 -> 106 hp for 20 mana.
//
// RED (`og.fadd(ally.hp, amount)` -> `og.fadd(ally.hp, amount + 1)` at
// :104): Expected equality of these values: 106.0f Which is: 106
// healed_hp[1] Which is: 107 "enemy bot arm: the heal still lands".
TEST(SemCleric, an_enemy_bot_heal_lands_without_a_notice)
{
    float healed_hp[2] = {0.0f, 0.0f};
    for (unsigned char team : {static_cast<unsigned char>(1),
                               static_cast<unsigned char>(0)}) {
        Field f;
        walker* cleric = f.caster(FAMILY_CLERIC, team, 160, 160, 4, 100.0f, 1);
        ASSERT_NE(nullptr, cleric);
        walker* ally = f.living_at(FAMILY_SOLDIER, team, 180, 160);
        ASSERT_NE(nullptr, ally);
        ally->stats()->set_max_hitpoints(400.0f);
        ally->stats()->set_hitpoints(50.0f);
        ASSERT_TRUE(cleric->special());
        healed_hp[team] = ally->stats()->hitpoints();
        EXPECT_FLOAT_EQ(80.0f, cleric->stats()->magicpoints())
            << "the heal costs base / 2 = 18 plus the slot's 2 on either team";
        EXPECT_EQ(1, f.events_of(og::sim::EventKind::PlaySound,
                                 static_cast<std::uint32_t>(SOUND_HEAL)));
        if (team == 1)
            EXPECT_EQ(0, f.notifications("Cleric healed"))
                << "enemy bot arm: no notice";
        else
            EXPECT_EQ(1, f.notifications("Cleric healed 1 man!"))
                << "team 0 arm (contrast): one notice";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
    // 100 mana, level 4, the sim stream pinned at 12345: base = 25 + next(25)
    // = 25 + 11 = 36, the heal 36 + 4 * 5 = 56 on 50 hp.
    EXPECT_FLOAT_EQ(106.0f, healed_hp[1]) << "enemy bot arm: the heal still lands";
    EXPECT_FLOAT_EQ(healed_hp[0], healed_hp[1])
        << "enemy bot arm: the same heal as the announced one";
}

// :139 the reason and the missing mace. MYSTIC MACE refuses SPECIAL BUSY
// while busy, summons nothing and counts no shot; not busy it summons one.
TEST(SemCleric, mystic_mace_is_refused_while_busy_and_summons_nothing)
{
    Field f;
    walker* cleric = f.caster(FAMILY_CLERIC, 0, 160, 160, 8, 500.0f, 1);
    ASSERT_NE(nullptr, cleric);
    give_character(cleric, FAMILY_CLERIC, 120);
    cleric->set_shifter_down(1);
    cleric->set_busy(2);
    Failure why = Failure::None;
    std::string reason;
    EXPECT_FALSE(cleric->special(&why, &reason));
    EXPECT_EQ(Failure::ScriptDeclined, why);
    EXPECT_EQ("SPECIAL BUSY", reason) << "busy (reason) arm";
    EXPECT_FLOAT_EQ(500.0f, cleric->stats()->magicpoints());
    EXPECT_FLOAT_EQ(2.0f, cleric->busy());
    EXPECT_EQ(0u, f.live(Order::FX, FAMILY_MAGIC_SHIELD).size())
        << "busy (no fx) arm: no mace";
    EXPECT_EQ(0u, cleric->myguy->scen_shots) << "busy arm: no shot counted";

    cleric->set_busy(0);
    ASSERT_TRUE(cleric->special(&why, &reason)) << reason;
    EXPECT_EQ(1u, f.live(Order::FX, FAMILY_MAGIC_SHIELD).size())
        << "control: not busy, one mace";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :146. A hero's MYSTIC MACE counts one shot in total_shots and scen_shots.
TEST(SemCleric, a_hero_mystic_mace_counts_one_shot)
{
    Field f;
    walker* cleric = f.caster(FAMILY_CLERIC, 0, 160, 160, 8, 500.0f, 1);
    ASSERT_NE(nullptr, cleric);
    give_character(cleric, FAMILY_CLERIC, 120);
    cleric->myguy->total_shots = 7;
    cleric->myguy->scen_shots = 4;
    cleric->set_shifter_down(1);
    ASSERT_TRUE(cleric->special());
    EXPECT_EQ(8u, cleric->myguy->total_shots) << "hero (total_shots) arm";
    EXPECT_EQ(5u, cleric->myguy->scen_shots) << "hero (scen_shots) arm";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :179 the risen level, on both setting values: rand0(cleric level) + 1,
// so 1..L. The world stream is scripted so the one rand0(4) answers k.
TEST(SemCleric, a_raised_skeleton_rises_at_one_to_the_cleric_level)
{
    for (short fixes : {static_cast<short>(0), static_cast<short>(1)}) {
        for (const auto& [draw, level] : {std::pair<std::uint32_t, int>{0, 1},
                                          std::pair<std::uint32_t, int>{2, 3},
                                          std::pair<std::uint32_t, int>{3, 4}}) {
            FixedRandom scripted(draw);
            ScopedSimRandom scoped(&scripted);
            Field f(fixes);
            walker* cleric = f.caster(FAMILY_CLERIC, 0, 80, 80, 4, 200.0f, 2);
            ASSERT_NE(nullptr, cleric);
            walker* stain = stain_in_reach(f, cleric, 1, FAMILY_ORC, 60);
            ASSERT_NE(nullptr, stain);
            Failure why = Failure::None;
            std::string reason;
            ASSERT_TRUE(cleric->special(&why, &reason))
                << "fixes " << fixes << ": " << reason;
            const auto risen = f.live(Order::Living, FAMILY_SKELETON);
            ASSERT_EQ(1u, risen.size());
            EXPECT_EQ(level, risen[0]->stats()->level())
                << "corpse (risen level) arm, fixes " << fixes << ": rand0(4) = "
                << draw << " rises at level " << level;
            EXPECT_EQ(cleric, risen[0]->owner());
            EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
        }
    }
}

// :106 (#456), the classic arm (setting off): the raise's corpse finder
// answers only a stain on the cleric's floor. A cleric on a balcony (floor
// 1) right above a corpse on floor 0 refuses RAISE SKELETON as NO CORPSE
// NEARBY: nothing rises, nothing is paid. The same rig with the cleric on
// floor 0 raises it.
TEST(SemCleric, classic_a_raise_never_reaches_a_corpse_on_another_floor)
{
    for (const short cleric_floor : {short{1}, short{0}}) {
        Field f(0);
        f.world().set_floor_count(2);
        walker* cleric = f.caster(FAMILY_CLERIC, 0, 80, 80, 4, 200.0f, 2);
        ASSERT_NE(nullptr, cleric);
        walker* stain = stain_in_reach(f, cleric, 1, FAMILY_ORC, 60);
        ASSERT_NE(nullptr, stain);
        cleric->change_floor(cleric_floor);
        cleric->setxy(80, 80);
        ASSERT_EQ(0, static_cast<int>(stain->floor()));
        Failure why = Failure::None;
        std::string reason;
        const bool raised = cleric->special(&why, &reason);
        if (cleric_floor == 1) {
            EXPECT_FALSE(raised) << "the corpse below: refused";
            EXPECT_EQ("NO CORPSE NEARBY", reason) << "the corpse below";
            EXPECT_EQ(0u, f.live(Order::Living, FAMILY_SKELETON).size()) << "the corpse below: none risen";
            EXPECT_FLOAT_EQ(200.0f, cleric->stats()->magicpoints()) << "the corpse below: free";
            EXPECT_EQ(0, static_cast<int>(stain->dead())) << "the corpse below: the stain stays";
        } else {
            EXPECT_TRUE(raised) << "the corpse on the cleric's floor: " << reason;
            EXPECT_EQ(1u, f.live(Order::Living, FAMILY_SKELETON).size()) << "one risen";
        }
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :239 the hostile arm's lifetime: a foe's corpse rises as a 200-tick ghost.
TEST(SemCleric, a_foe_corpse_resurrects_as_a_two_hundred_tick_ghost)
{
    Field f;
    walker* cleric = f.caster(FAMILY_CLERIC, 0, 80, 80, 10, 300.0f, 4);
    ASSERT_NE(nullptr, cleric);
    walker* stain = stain_in_reach(f, cleric, 1, FAMILY_ORC, 30);
    ASSERT_NE(nullptr, stain);
    ASSERT_TRUE(cleric->special());
    const auto ghosts = f.live(Order::Living, FAMILY_GHOST);
    ASSERT_EQ(1u, ghosts.size());
    EXPECT_EQ(200, ghosts[0]->lifetime()) << "hostile stain (lifetime) arm";
    EXPECT_EQ(0, static_cast<int>(ghosts[0]->team_num()));
    EXPECT_EQ(cleric, ghosts[0]->owner());
    EXPECT_EQ(0u, f.live(Order::Living, FAMILY_ORC).size());
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :256 the wrapped penalty. The penalty is level^2 * 100 stored in a short
// and read back unsigned: a level-19 friend costs 36100 (the short is
// -29436; +65536 restores it), floored at zero, then the 90 reward.
TEST(SemCleric, a_resurrect_penalty_past_a_short_wraps_back_to_its_unsigned_value)
{
    for (const auto& [exp_before, exp_after] : {std::pair<unsigned, unsigned>{40000, 3990},
                                                std::pair<unsigned, unsigned>{1000, 90}}) {
        Field f;
        walker* cleric = f.caster(FAMILY_CLERIC, 0, 80, 80, 10, 300.0f, 4);
        ASSERT_NE(nullptr, cleric);
        give_character(cleric, FAMILY_CLERIC, 100);
        cleric->myguy->exp = exp_before;
        walker* stain = stain_in_reach(f, cleric, 0, FAMILY_SOLDIER, 30, 19);
        ASSERT_NE(nullptr, stain);
        stain->stats()->set_max_hitpoints(100.0f);
        ASSERT_TRUE(cleric->special());
        ASSERT_EQ(1u, f.live(Order::Living, FAMILY_SOLDIER).size())
            << "the friend is back";
        EXPECT_EQ(exp_after, cleric->myguy->exp)
            << "loss negative arm: exp " << exp_before << " pays 36100, floored at 0, +90";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// ---------------------------------------------------------------------------
// living-06-elemental.lua
// ---------------------------------------------------------------------------

// :15. STARBURST fires one meteor along each of the eight headings and the
// elemental's facing comes back afterwards (C truncation of the int temps).
TEST(SemElemental, starburst_fires_all_eight_headings_and_restores_the_facing)
{
    for (short new_specials : {static_cast<short>(0), static_cast<short>(1)}) {
        MidpointRandom midpoint;
        ScopedSimRandom scoped(&midpoint);  // zero waver: the sign is the heading
        Field f;
        f.world().new_specials = new_specials;
        walker* e = f.caster(FAMILY_FIREELEMENTAL, 0, 160, 160, 5, 100.0f, 1);
        ASSERT_NE(nullptr, e);
        e->set_lastx(-3.7f);
        e->set_lasty(1.2f);
        Failure why = Failure::None;
        std::string reason;
        ASSERT_TRUE(e->special(&why, &reason)) << reason;
        const auto meteors = f.live(Order::Weapon, FAMILY_METEOR);
        ASSERT_EQ(8u, meteors.size());
        EXPECT_EQ(kEightHeadings, headings(meteors))
            << "cast arm: one meteor per heading (new_specials " << new_specials << ")";
        EXPECT_FLOAT_EQ(-3.0f, e->lastx()) << "cast arm: the facing x is restored";
        EXPECT_FLOAT_EQ(1.0f, e->lasty()) << "cast arm: the facing y is restored";
        EXPECT_FLOAT_EQ(50.0f, e->stats()->magicpoints())
            << "cast arm: the weapon costs are pre-refunded; only the price is paid";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :36 the slot-1 arm. An elemental dying on slot 1 fires its free parting
// starburst and is dead again afterwards, still on slot 1 unshifted.
TEST(SemElemental, an_elemental_dying_on_slot_one_fires_a_free_starburst_and_stays_dead)
{
    for (short new_specials : {static_cast<short>(0), static_cast<short>(1)}) {
        Field f;
        f.world().new_specials = new_specials;
        walker* e = f.caster(FAMILY_FIREELEMENTAL, 0, 160, 160, 5, 100.0f, 1);
        ASSERT_NE(nullptr, e);
        e->stats()->set_hitpoints(-3.0f);
        e->set_dead(1);
        e->death();
        EXPECT_EQ(8u, f.live(Order::Weapon, FAMILY_METEOR).size())
            << "slot 1 arm (new_specials " << new_specials << "): the parting ring";
        EXPECT_FLOAT_EQ(100.0f, e->stats()->magicpoints())
            << "slot 1 arm: the parting shot is free";
        EXPECT_EQ(1, e->dead()) << "slot 1 arm: the body is re-killed";
        EXPECT_EQ(1, e->current_special()) << "slot 1 arm: still on slot 1";
        EXPECT_EQ(0, e->shifter_down()) << "slot 1 arm: still unshifted";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

namespace {

struct TollRig {
    Field f;
    walker* e = nullptr;
    walker* owner = nullptr;
    const FamilyDescriptor* fd = nullptr;

    TollRig()
    {
        fd = get_family_descriptor(FAMILY_FIREELEMENTAL);
        e = f.living_at(FAMILY_FIREELEMENTAL, 0, 160, 160);
        owner = f.living_at(FAMILY_MAGE, 0, 200, 160);
        if (e == nullptr || owner == nullptr)
            return;
        owner->stats()->set_max_hitpoints(90.0f);
        owner->stats()->set_hitpoints(90.0f);
        owner->stats()->set_magicpoints(10.0f);
        e->set_owner(owner);
        e->set_lifetime(10);
        e->stats()->set_hitpoints(50.0f);
    }
};

}  // namespace

// :65. Only a summoned (finite lifetime), hurt elemental with a living owner
// takes the toll; a natural one, a whole one, or one whose owner is dead or
// gone takes nothing and burns no lifetime.
TEST(SemElemental, only_a_hurt_summoned_elemental_with_a_living_owner_takes_the_toll)
{
    {   // lifetime 0
        TollRig r;
        ASSERT_TRUE(r.e && r.owner && r.fd);
        r.e->set_lifetime(0);
        og::test::on_act_living(*r.fd, dynamic_cast<living*>(r.e));
        EXPECT_FLOAT_EQ(90.0f, r.owner->stats()->hitpoints()) << "lifetime 0 arm";
        EXPECT_FLOAT_EQ(10.0f, r.owner->stats()->magicpoints()) << "lifetime 0 arm";
        EXPECT_FLOAT_EQ(50.0f, r.e->stats()->hitpoints()) << "lifetime 0 arm";
        EXPECT_EQ(0, r.e->lifetime()) << "lifetime 0 arm";
        EXPECT_EQ(0u, r.f.guard.count()) << r.f.guard.message();
    }
    {   // owner dead
        TollRig r;
        ASSERT_TRUE(r.e && r.owner && r.fd);
        r.owner->set_dead(1);
        og::test::on_act_living(*r.fd, dynamic_cast<living*>(r.e));
        EXPECT_FLOAT_EQ(90.0f, r.owner->stats()->hitpoints()) << "owner dead arm";
        EXPECT_FLOAT_EQ(10.0f, r.owner->stats()->magicpoints()) << "owner dead arm";
        EXPECT_FLOAT_EQ(50.0f, r.e->stats()->hitpoints()) << "owner dead arm";
        EXPECT_EQ(10, r.e->lifetime()) << "owner dead arm: no lifetime burned";
        EXPECT_EQ(0u, r.f.guard.count()) << r.f.guard.message();
    }
    {   // no owner
        TollRig r;
        ASSERT_TRUE(r.e && r.owner && r.fd);
        r.e->set_owner(nullptr);
        og::test::on_act_living(*r.fd, dynamic_cast<living*>(r.e));
        EXPECT_FLOAT_EQ(50.0f, r.e->stats()->hitpoints()) << "owner nil arm";
        EXPECT_EQ(10, r.e->lifetime()) << "owner nil arm: no lifetime burned";
        EXPECT_EQ(0u, r.f.guard.count()) << "owner nil arm: " << r.f.guard.message();
    }
    {   // full health
        TollRig r;
        ASSERT_TRUE(r.e && r.owner && r.fd);
        r.e->stats()->set_hitpoints(100.0f);
        og::test::on_act_living(*r.fd, dynamic_cast<living*>(r.e));
        EXPECT_FLOAT_EQ(90.0f, r.owner->stats()->hitpoints()) << "full hp arm";
        EXPECT_FLOAT_EQ(10.0f, r.owner->stats()->magicpoints()) << "full hp arm";
        EXPECT_FLOAT_EQ(100.0f, r.e->stats()->hitpoints()) << "full hp arm";
        EXPECT_EQ(10, r.e->lifetime()) << "full hp arm: no lifetime burned";
        EXPECT_EQ(0u, r.f.guard.count()) << r.f.guard.message();
    }
}

// :71 the two unpaid arms (classic, living.cpp:82-101 at 7ff1654a). An
// owner who can pay the HP but not the MP still loses the HP and the
// elemental burns a tick of lifetime instead of healing; an owner under a
// third of its health pays nothing and the elemental burns the tick.
TEST(SemElemental, classic_an_owner_who_cannot_pay_both_costs_the_elemental_a_tick)
{
    {   // owner hp >= max/3, mp < 3
        TollRig r;
        ASSERT_TRUE(r.e && r.owner && r.fd);
        r.owner->stats()->set_hitpoints(60.0f);
        r.owner->stats()->set_magicpoints(2.0f);
        og::test::on_act_living(*r.fd, dynamic_cast<living*>(r.e));
        EXPECT_FLOAT_EQ(59.0f, r.owner->stats()->hitpoints())
            << "hp-only arm: the owner loses the HP anyway";
        EXPECT_FLOAT_EQ(2.0f, r.owner->stats()->magicpoints()) << "hp-only arm";
        EXPECT_FLOAT_EQ(50.0f, r.e->stats()->hitpoints()) << "hp-only arm: no heal";
        EXPECT_EQ(9, r.e->lifetime()) << "hp-only arm: one tick burned";
        EXPECT_EQ(0u, r.f.guard.count()) << r.f.guard.message();
    }
    {   // owner hp < max/3
        TollRig r;
        ASSERT_TRUE(r.e && r.owner && r.fd);
        r.owner->stats()->set_hitpoints(20.0f);
        r.owner->stats()->set_magicpoints(10.0f);
        og::test::on_act_living(*r.fd, dynamic_cast<living*>(r.e));
        EXPECT_FLOAT_EQ(20.0f, r.owner->stats()->hitpoints()) << "weak owner arm: untouched";
        EXPECT_FLOAT_EQ(10.0f, r.owner->stats()->magicpoints()) << "weak owner arm: untouched";
        EXPECT_FLOAT_EQ(50.0f, r.e->stats()->hitpoints()) << "weak owner arm: no heal";
        EXPECT_EQ(9, r.e->lifetime()) << "weak owner arm: one tick burned";
        EXPECT_EQ(0u, r.f.guard.count()) << r.f.guard.message();
    }
}

// ---------------------------------------------------------------------------
// living-08-slime.lua
// ---------------------------------------------------------------------------

// :23. A veteran split re-levels both halves from half the experience on the
// guy.cpp ladder (step 6000L - 2000: level 2 at 10000, level 3 at 26000).
TEST(SemSlime, a_veteran_split_levels_both_halves_on_the_experience_ladder)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_SLIME);
    for (const auto& [exp_held, level] : {std::pair<unsigned, int>{2000, 1},
                                          std::pair<unsigned, int>{19998, 1},
                                          std::pair<unsigned, int>{20000, 2},
                                          std::pair<unsigned, int>{51998, 2},
                                          std::pair<unsigned, int>{52000, 3}}) {
        Field f;
        walker* slime = f.living_at(FAMILY_SLIME, 0, 160, 160, 1);  // threshold 1000
        ASSERT_NE(nullptr, slime);
        slime->set_ani_type(static_cast<char>(ANI_SLIME_SPLIT));
        auto character = std::make_unique<guy>(FAMILY_SLIME);
        character->exp = exp_held;
        character->teamnum = 0;
        slime->set_owned_myguy(std::move(character));

        const std::optional<bool> handled =
            og::script::hooks::on_ani_complete(&desc, slime);
        ASSERT_TRUE(handled.has_value());
        ASSERT_TRUE(*handled);
        walker* sibling = nullptr;
        for (walker* ob : f.live(Order::Living, FAMILY_SMALL_SLIME))
            if (ob != slime)
                sibling = ob;
        ASSERT_NE(nullptr, sibling);
        ASSERT_NE(nullptr, slime->myguy);
        ASSERT_NE(nullptr, sibling->myguy);
        const char* arm = level == 1 ? "below 10000 arm" :
                          level == 2 ? "[10000, 26000) arm" : "26000 arm";
        EXPECT_EQ(level, static_cast<int>(slime->myguy->level))
            << arm << ": " << exp_held / 2 << " experience";
        EXPECT_EQ(level, static_cast<int>(sibling->myguy->level))
            << arm << ": " << exp_held / 2 << " experience";
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :43 the full-world arm. A big slime bot splits only while the world's
// head-count leaves room for the extra body.
TEST(SemSlime, a_big_slime_never_splits_in_a_full_world)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_SLIME);
    Field f;
    walker* slime = f.caster(FAMILY_SLIME, 2, 160, 160, 3, 1000.0f, 1);
    ASSERT_NE(nullptr, slime);
    living* body = dynamic_cast<living*>(slime);
    ASSERT_NE(nullptr, body);
    f.world().living_count = MAXOBS;
    EXPECT_FALSE(og::test::check_special_ai(desc, body))
        << "living_count >= MAXOBS arm: no split";
    f.world().living_count = MAXOBS - 1;
    EXPECT_TRUE(og::test::check_special_ai(desc, body))
        << "control: one place left, the split is allowed";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}

// :60. The dying slime takes its offspring's name when it has one of its
// own (sic, the old game's strcpy direction). A fresh offspring is always
// unnamed (stats.cpp name[0] = 0 in the old game; nothing names an add_ob
// living in either game), so a NAMED dying slime ends blank and an unnamed
// one stays blank. Classic, pinned on purpose.
TEST(SemSlime, classic_a_dying_slime_ends_unnamed_named_or_not)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_SLIME);
    for (const char* name : {"", "BLOBBY"}) {
        Field f;
        walker* slime = f.living_at(FAMILY_SLIME, 2, 160, 160, 3);
        ASSERT_NE(nullptr, slime);
        slime->stats()->name = name;
        slime->stats()->set_hitpoints(1.0f);
        ASSERT_TRUE(og::test::on_death(desc, slime));
        const auto children = f.live(Order::Living, FAMILY_MEDIUM_SLIME);
        ASSERT_EQ(1u, children.size());
        EXPECT_EQ(std::string(), std::string(children[0]->stats()->name))
            << "the offspring is born unnamed";
        EXPECT_EQ(std::string(), std::string(slime->stats()->name))
            << (*name ? "parent named arm: the name is replaced by the offspring's blank"
                      : "parent unnamed arm: the dying slime stays unnamed");
        EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
    }
}

// :65 the no-guy arm. A slime without a character leaves an offspring
// without one.
TEST(SemSlime, a_slime_without_a_character_leaves_an_offspring_without_one)
{
    const FamilyDescriptor& desc = describe_family(FAMILY_MEDIUM_SLIME);
    Field f;
    walker* slime = f.living_at(FAMILY_MEDIUM_SLIME, 2, 160, 160, 6);
    ASSERT_NE(nullptr, slime);
    ASSERT_EQ(nullptr, slime->myguy);
    ASSERT_TRUE(og::test::on_death(desc, slime));
    const auto children = f.live(Order::Living, FAMILY_SMALL_SLIME);
    ASSERT_EQ(1u, children.size());
    EXPECT_EQ(nullptr, children[0]->myguy) << "no guy arm: the offspring has none";
    EXPECT_EQ(nullptr, slime->myguy) << "no guy arm: nor does the parent";
    EXPECT_EQ(0u, f.guard.count()) << f.guard.message();
}
