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
