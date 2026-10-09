/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials, orc captain: HOWL, EAT CORPSE, HOOK BLADE / KNIFE FAN,
// WAR BANNER / WARBAND (packs/core/lib/kit_captain.lua,
// lib/orc_specials.lua, lib/effect_hook_blade.lua, lib/weapon_banner.lua).
//
// Every cast goes through the REAL walker::special() (the engine's price
// gate and charge included); every bot gate through the family's
// check_special_ai dispatch. Each special has a twin with the setting off:
// the captain then has no specials at all (every slot is new-kit), so the
// cast is refused for mana and no kit entity appears.
//
// Perturbation proofs (one per special, each run once against the staged
// copy under build/ci-test/packs/core and then restored; the failing line
// is quoted where it was seen):
//   HOWL, EAT CORPSE   lib/orc_specials.lua `foe:add_frozen_stun(stun)` ->
//                      `foe:add_frozen_stun(0)` (the moved orc pin):
//                      captain_howl_stuns_like_the_orc fails on the stun.
//                      `corpse_heal_per_level = 5` -> 0 in the captain's
//                      tuning: captain_eats_a_corpse_when_hurt fails on hp.
//                      The WAR BANNER row's `mp_cost = 80` -> 81:
//                      promoted_orc_keeps_howl_and_eat_corpse fails on the
//                      promoted body's cast_cost (81).
//   HOOK BLADE         each hook test names its own (the spiral, the reel,
//                      the late snag, the wall, the ring).
//   KNIFE FAN          `for spread = -1, 1` -> `for spread = 0, 0`:
//                      knife_fan_throws_three counts one knife. Restoring
//                      the aim through og.trunc: the same test reads a
//                      0.75 aim back as 0.
//   WAR BANNER         `banner_regen = 1` -> 0 (core:war_banner tuning):
//                      banner_regens_allies_... fails on the ally's hp.
//   WARBAND            `warband_base = 2` -> 0: warband_arrives_... counts
//                      no grunt. `local clear = room and not
//                      on_the_captain(self, sx, sy)` -> `local clear = room`:
//                      the same test finds the east grunt at 480, inside the
//                      captain (448 wanted), and a grunt on top of him.
// (The exact output lines are in the change's report.)

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/family_presentation.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/fearless.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr unsigned char kUs = 0;
constexpr unsigned char kThem = 1;

// The captain's tuning, restated where a test checks an exact consequence.
constexpr int kHookTicks = 48;
constexpr int kHookHp = 30;
constexpr int kHookStart = 12;
constexpr int kHookGrowth = 2;
constexpr int kHookReach = 60;
constexpr int kHookReelStep = 8;
constexpr int kHookReelMax = 8;
constexpr int kHookStun = 12;
constexpr int kBannerHpBase = 120;
constexpr int kBannerTicks = 900;
constexpr int kWarbandLifetime = 400;

// lib/effect_hook_blade.lua's own copy of the 16-step orbit circle.
constexpr std::array<int, 16> kOrbitX = {0,  -9, -17, -22, -24, -22, -17, -9,
                                         0,  9,  17,  22,  24,  22,  17,  9};
constexpr std::array<int, 16> kOrbitY = {-24, -22, -17, -9, 0,  9,  17, 22,
                                         24,  22,  17,  9,  0,  -9, -17, -22};

// The spiral's radius on the blade's tick-th act (lib/effect_hook_blade.lua):
// out from 12 px, 2 px a tick, to 60 px at tick 24, then back in.
int hook_radius_at(int tick)
{
    const int turn = (kHookReach - kHookStart) / kHookGrowth;
    if (tick <= turn)
        return kHookStart + kHookGrowth * tick;
    return kHookReach - kHookGrowth * (tick - turn);
}

// The blade's offset from its centred spot on its tick-th act: one step of
// the 16-step circle a tick, scaled to the radius (the lib's float math).
std::pair<float, float> hook_offset_at(int tick)
{
    const std::size_t idx = static_cast<std::size_t>(tick % 16);
    const float r = static_cast<float>(hook_radius_at(tick));
    return {static_cast<float>(kOrbitX[idx]) * r / 24.0f,
            static_cast<float>(kOrbitY[idx]) * r / 24.0f};
}

int fx_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::FX, id);
}

int weapon_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::Weapon, id);
}

// A 32x32-tile grass field (512 x 512 px), nothing else.
void open_field(TestGameWorld& tw)
{
    GameWorld& world = tw.world();
    world.resize_grid(32, 32);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.mysmoother.set_target(world.grid);
    world.my_team = 0;
}

void wall_tile(TestGameWorld& tw, int tx, int ty)
{
    GameWorld& world = tw.world();
    world.grid.data[static_cast<std::size_t>(tx + ty * world.grid.w)] =
        static_cast<std::uint8_t>(PIX_H_WALL1);
}

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y, short level = 1)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    w->stats()->set_level(level);
    w->stats()->set_max_hitpoints(200.0f);
    w->stats()->set_hitpoints(200.0f);
    w->set_busy(0);
    return w;
}

// A level-10 captain with 500 MP on our team, idle.
walker* add_captain(TestGameWorld& tw, short x, short y, short level = 10)
{
    walker* c = add_living(tw, FAMILY_BIG_ORC, kUs, x, y, level);
    if (c == nullptr)
        return nullptr;
    c->stats()->set_max_magicpoints(500.0f);
    c->stats()->set_magicpoints(500.0f);
    return c;
}

walker* add_stain(TestGameWorld& tw, walker* under, short level = 3)
{
    walker* stain = tw.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (stain == nullptr)
        return nullptr;
    stain->set_team_num(kThem);
    stain->set_dead(0);
    stain->set_floor(under->floor());
    stain->center_on(under);
    stain->stats()->set_old_family(FAMILY_SOLDIER);
    stain->stats()->set_level(level);
    return stain;
}

struct Cast {
    bool ok = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    float mp_spent = 0.0f;
};

Cast cast(walker* w, int slot, bool shifted)
{
    w->set_current_special(static_cast<char>(slot));
    w->set_shifter_down(shifted ? 1 : 0);
    Cast out;
    const float before = w->stats()->magicpoints();
    out.ok = w->special(&out.why, &out.reason);
    out.mp_spent = before - w->stats()->magicpoints();
    w->set_shifter_down(0);
    return out;
}

std::vector<walker*> live_of(const std::list<std::unique_ptr<walker>>& list,
                             Order order, int family)
{
    std::vector<walker*> out;
    for (const auto& uptr : list)
        if (uptr && !uptr->dead() && uptr->query_order() == order &&
            uptr->family() == family)
            out.push_back(uptr.get());
    return out;
}

std::vector<walker*> blades(TestGameWorld& tw)
{
    return live_of(tw.world().oblist, Order::FX, fx_family("core:hook_blade"));
}

std::vector<walker*> banners(TestGameWorld& tw)
{
    return live_of(tw.world().weaplist, Order::Weapon,
                   weapon_family("core:war_banner"));
}

std::vector<walker*> grunts_of(TestGameWorld& tw, walker* captain)
{
    std::vector<walker*> out;
    for (walker* w : live_of(tw.world().oblist, Order::Living, FAMILY_ORC))
        if (w->owner() == captain)
            out.push_back(w);
    return out;
}

int count_sound(const TestGameWorld& tw, std::uint32_t sound)
{
    int n = 0;
    for (const auto& e : tw.events.events())
        if (e.kind == og::sim::EventKind::PlaySound && e.a == sound)
            ++n;
    return n;
}

bool notified(const TestGameWorld& tw, const std::string& needle)
{
    for (const auto& e : tw.events.events())
        if (e.kind == og::sim::EventKind::Notification &&
            e.text.find(needle) != std::string::npos)
            return true;
    return false;
}

std::vector<short> xs_of(const std::vector<walker*>& ws)
{
    std::vector<short> out;
    for (const walker* w : ws)
        out.push_back(w->xpos());
    return out;
}

int frozen(const walker* w)
{
    return static_cast<int>(w->stats()->frozen_delay());
}

const command* front_command(const walker* w)
{
    if (w->stats()->commands.empty())
        return nullptr;
    return &w->stats()->commands.front();
}

// Livings on the eight spots hugging `anchor` (kit_captain.ring_spot's
// geometry for a 16x16 mover), so nothing of that size can land beside it.
void fence_in(TestGameWorld& tw, walker* anchor, unsigned char team)
{
    const short ax = anchor->xpos();
    const short ay = anchor->ypos();
    const short near = static_cast<short>(anchor->sizex() + 2);
    const short far = 18;
    const std::array<short, 3> xs = {static_cast<short>(ax - far), ax,
                                     static_cast<short>(ax + near)};
    const std::array<short, 3> ys = {static_cast<short>(ay - far), ay,
                                     static_cast<short>(ay + near)};
    for (short x : xs)
        for (short y : ys)
            if (x != ax || y != ay)
                add_living(tw, FAMILY_SOLDIER, team, x, y);
}

// One world: our team-0 caster of `family` howls at a level-1 soldier with
// a constitution-0 guy (its con roll is og.rand0(0): no draw, so the stun
// is 10 + the level roll, never 0), from a pinned RNG state. Answers the soldier's
// frozen stun and the RNG state after the cast.
std::pair<int, std::uint32_t> howl_stun(int family, short new_specials)
{
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = new_specials;
    walker* caster = add_living(tw, family, kUs, 100, 100, 6);
    walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 140, 100);
    EXPECT_NE(nullptr, caster);
    EXPECT_NE(nullptr, foe);
    if (caster == nullptr || foe == nullptr)
        return {-1, 0};
    caster->stats()->set_max_magicpoints(100.0f);
    caster->stats()->set_magicpoints(100.0f);
    foe->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    foe->myguy->constitution = 0;
    tw.world().rng_.state_ = 12345u;
    cast(caster, 1, false);
    return {frozen(foe), tw.world().rng_.state_};
}

}  // namespace

// ---------------------------------------------------------------- the setting

// With the setting off the captain has no specials: every slot and every
// alternate answers 5000 through the view, the cast is refused for mana and
// nothing is spent, made or heard.
TEST(KitCaptain, captain_has_no_specials_when_off)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 0;
    walker* captain = add_captain(tw, 100, 100);
    ASSERT_NE(nullptr, captain);
    add_living(tw, FAMILY_ORC, kUs, 118, 100);
    add_living(tw, FAMILY_SOLDIER, kThem, 140, 100);
    add_stain(tw, captain);
    captain->stats()->set_hitpoints(50.0f);

    for (int slot = 1; slot <= 4; ++slot) {
        for (bool shifted : {false, true}) {
            EXPECT_EQ(5000, og::sim::cast_cost(*captain, slot, shifted))
                << "slot " << slot << " shifted " << shifted;
            const Cast c = cast(captain, slot, shifted);
            EXPECT_FALSE(c.ok) << "slot " << slot << " shifted " << shifted;
            EXPECT_EQ(walker::SpecialFailure::NoMP, c.why);
            EXPECT_FLOAT_EQ(0.0f, c.mp_spent);
        }
    }
    EXPECT_TRUE(blades(tw).empty());
    EXPECT_TRUE(banners(tw).empty());
    EXPECT_TRUE(grunts_of(tw, captain).empty());
    EXPECT_EQ(0, count_sound(tw, SOUND_ROAR));

    // ... and with it on, the same captain sees its four slots: HOWL, EAT
    // CORPSE (each on its own, so the shift changes neither price), HOOK
    // BLADE / KNIFE FAN, WAR BANNER / WARBAND.
    tw.world().new_specials = 1;
    EXPECT_EQ(25, og::sim::cast_cost(*captain, 1, false));
    EXPECT_EQ(25, og::sim::cast_cost(*captain, 1, true))
        << "EAT CORPSE is no longer HOWL's shift";
    EXPECT_EQ(20, og::sim::cast_cost(*captain, 2, false));
    EXPECT_EQ(20, og::sim::cast_cost(*captain, 2, true));
    EXPECT_EQ(40, og::sim::cast_cost(*captain, 3, false));
    EXPECT_EQ(30, og::sim::cast_cost(*captain, 3, true));
    EXPECT_EQ(80, og::sim::cast_cost(*captain, 4, false));
    EXPECT_EQ(80, og::sim::cast_cost(*captain, 4, true));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// With the setting off every captain gate answers true WITHOUT doing
// anything: no RNG draw, no shift written, no foe acquired. (A false would
// skip the engine's following rng(3) draw and move the recordings.)
TEST(KitCaptain, ai_off_answers_the_classic_gate)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 0;
    walker* captain = add_captain(tw, 100, 100);
    ASSERT_NE(nullptr, captain);
    add_living(tw, FAMILY_SOLDIER, kThem, 120, 100);
    add_living(tw, FAMILY_SOLDIER, kThem, 100, 120);
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_BIG_ORC);
    ASSERT_NE(nullptr, fd);
    ASSERT_TRUE(og::test::has_check_special_ai(*fd));
    auto* bot = dynamic_cast<living*>(captain);
    ASSERT_NE(nullptr, bot);
    for (int slot = 1; slot <= 4; ++slot) {
        bot->set_current_special(static_cast<char>(slot));
        bot->set_shifter_down(1);
        bot->set_foe(nullptr);
        tw.world().rng_.state_ = 777u;
        EXPECT_TRUE(og::test::check_special_ai(*fd, bot)) << "slot " << slot;
        EXPECT_EQ(777u, tw.world().rng_.state_) << "slot " << slot;
        EXPECT_EQ(1, bot->shifter_down()) << "slot " << slot;
        EXPECT_EQ(nullptr, bot->foe()) << "slot " << slot;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------- HOWL, EAT CORPSE

// The captain's HOWL is the orc's own yell (one body in lib/orc_specials.lua):
// the same pinned stream gives the same stun on the same foe and leaves the
// stream in the same place. Off, the captain draws nothing and stuns nobody.
TEST(KitCaptain, captain_howl_stuns_like_the_orc)
{
    og::test::ScopedHookFailureGuard guard;
    const auto orc = howl_stun(FAMILY_ORC, 1);
    const auto captain = howl_stun(FAMILY_BIG_ORC, 1);
    const auto captain_off = howl_stun(FAMILY_BIG_ORC, 0);
    EXPECT_GT(orc.first, 0) << "the orc's howl freezes the soldier";
    EXPECT_EQ(orc.first, captain.first)
        << "the captain's howl is the orc's: same stun from the same stream";
    EXPECT_EQ(orc.second, captain.second) << "and the same draws";
    EXPECT_EQ(0, captain_off.first) << "setting off: no howl";
    EXPECT_EQ(12345u, captain_off.second) << "setting off: no draw";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A busy captain cannot howl, and the refusal costs nothing.
TEST(KitCaptain, captain_howl_refuses_while_busy)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 100, 100);
    ASSERT_NE(nullptr, captain);
    captain->set_busy(3.0f);
    const Cast c = cast(captain, 1, false);
    EXPECT_FALSE(c.ok);
    EXPECT_EQ("SPECIAL BUSY", c.reason);
    EXPECT_FLOAT_EQ(0.0f, c.mp_spent);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 2: the orc's corpse meal at its own price (20). Its refusals carry
// over too: full health, no corpse, a corpse out of reach.
TEST(KitCaptain, captain_eats_a_corpse_when_hurt)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 100, 100);
    ASSERT_NE(nullptr, captain);
    captain->set_owned_myguy(std::make_unique<guy>(FAMILY_BIG_ORC));
    captain->myguy->exp = 0;

    Cast c = cast(captain, 2, false);
    EXPECT_EQ("ALREADY AT FULL HEALTH", c.reason);

    captain->stats()->set_hitpoints(50.0f);
    c = cast(captain, 2, false);
    EXPECT_EQ("NO CORPSE NEARBY", c.reason);

    walker* stain = add_stain(tw, captain, 3);
    ASSERT_NE(nullptr, stain);
    stain->setxy(stain->xpos() + 4, stain->ypos() + 4);  // 32 > 24 squared px
    c = cast(captain, 2, false);
    EXPECT_EQ("NO CORPSE IN RANGE", c.reason);
    EXPECT_FLOAT_EQ(0.0f, c.mp_spent);

    stain->center_on(captain);
    c = cast(captain, 2, false);
    EXPECT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(20.0f, c.mp_spent) << "EAT CORPSE's own price";
    EXPECT_FLOAT_EQ(65.0f, captain->stats()->hitpoints())
        << "50 + corpse level 3 * corpse_heal_per_level 5";
    EXPECT_EQ(1, stain->dead()) << "the corpse is eaten";
    EXPECT_GT(captain->myguy->exp, 0u) << "a hero is paid for the meal";
    EXPECT_TRUE(notified(tw, "ate a corpse."));

    // A meal bigger than the wound tops the captain up to full, no further.
    captain->stats()->set_hitpoints(195.0f);
    walker* second = add_stain(tw, captain, 3);
    ASSERT_NE(nullptr, second);
    c = cast(captain, 2, false);
    EXPECT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(200.0f, captain->stats()->hitpoints()) << "clamped to max";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The bug the kit fixes: an orc promoted to captain kept no specials. Now
// the promoted body carries the captain's whole price table, howls, and
// eats a corpse at EAT CORPSE's own price (setting on). The orc's own table
// reads 25 and 20 on its first two slots as well, so the proof that the
// body took the captain's table rests on slots 3 and 4 (40 and 80; an orc
// has nothing there and reads 5000).
TEST(KitCaptain, promoted_orc_keeps_howl_and_eat_corpse)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* orc = add_living(tw, FAMILY_ORC, kUs, 100, 100, 5);
    walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 140, 100);
    ASSERT_NE(nullptr, orc);
    ASSERT_NE(nullptr, foe);
    foe->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    foe->myguy->constitution = 0;  // stun = 10 + level roll: never 0
    orc->transform_to(Order::Living, FAMILY_BIG_ORC);
    ASSERT_EQ(FAMILY_BIG_ORC, orc->family());
    EXPECT_EQ(25, og::sim::cast_cost(*orc, 1, false));
    EXPECT_EQ(20, og::sim::cast_cost(*orc, 2, false));
    EXPECT_EQ(40, og::sim::cast_cost(*orc, 3, false));
    EXPECT_EQ(80, og::sim::cast_cost(*orc, 4, false));

    orc->stats()->set_max_magicpoints(100.0f);
    orc->stats()->set_magicpoints(100.0f);
    Cast c = cast(orc, 1, false);
    EXPECT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(25.0f, c.mp_spent) << "HOWL's price";
    EXPECT_GT(frozen(foe), 0);

    orc->set_busy(0);
    orc->stats()->set_max_hitpoints(200.0f);
    orc->stats()->set_hitpoints(50.0f);
    walker* stain = add_stain(tw, orc, 3);
    ASSERT_NE(nullptr, stain);
    c = cast(orc, 2, false);
    EXPECT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(20.0f, c.mp_spent) << "EAT CORPSE's own price";
    EXPECT_FLOAT_EQ(65.0f, orc->stats()->hitpoints())
        << "50 + corpse level 3 * corpse_heal_per_level 5";
    EXPECT_EQ(1, stain->dead()) << "the corpse is eaten";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ------------------------------------------------- HOOK BLADE / KNIFE FAN

// The blade spirals like the boomerang: one step of the 16-step circle a
// tick, the radius growing from 12 px by 2 px a tick to 60 px at tick 24
// and shrinking back to 12 at tick 48, re-centred on the captain each tick;
// one blade at a time; it wears out after hook_ticks (48).
//
// Proof it can fail: `hook_growth = 2` -> 1 in the staged
// living-15-orc_captain.lua printed, from tick 1 on,
//   Expected equality of these values: base_x + dx Which is: 199.75
//   blade->worldx() Which is: 200.125
TEST(KitCaptain, hook_spirals_out_and_back)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);

    Cast c = cast(captain, 3, false);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(40.0f, c.mp_spent);
    EXPECT_EQ(1, count_sound(tw, SOUND_FWIP));
    ASSERT_EQ(1u, blades(tw).size());
    walker* blade = blades(tw).front();
    EXPECT_EQ(captain, blade->owner());
    EXPECT_FLOAT_EQ(static_cast<float>(kHookHp), blade->stats()->hitpoints());
    EXPECT_EQ(0, blade->lineofsight()) << "not on the hook";

    c = cast(captain, 3, false);
    EXPECT_FALSE(c.ok);
    EXPECT_EQ("BLADE ALREADY OUT", c.reason);
    EXPECT_FLOAT_EQ(0.0f, c.mp_spent);

    const float base_x = static_cast<float>(
        captain->xpos() + captain->sizex() / 2 - blade->sizex() / 2);
    const float base_y = static_cast<float>(
        captain->ypos() + captain->sizey() / 2 - blade->sizey() / 2);
    float widest = 0.0f;
    for (int tick = 1; tick <= kHookTicks; ++tick) {
        blade->act();
        ASSERT_FALSE(blade->dead()) << "tick " << tick;
        const auto [dx, dy] = hook_offset_at(tick);
        EXPECT_FLOAT_EQ(base_x + dx, blade->worldx()) << "tick " << tick;
        EXPECT_FLOAT_EQ(base_y + dy, blade->worldy()) << "tick " << tick;
        widest = std::max(widest, std::max(std::abs(blade->worldx() - base_x),
                                            std::abs(blade->worldy() - base_y)));
    }
    EXPECT_FLOAT_EQ(60.0f, widest) << "out to 60 px (tick 24, straight down)";

    // It wears out: hook_ticks of life in all, gone on the next act.
    blade->act();
    EXPECT_TRUE(blade->dead()) << "48 ticks of spiral, gone on the 49th";

    // The captain may throw a new one once it is gone.
    c = cast(captain, 3, false);
    EXPECT_TRUE(c.ok) << c.reason;
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A blade whose captain dies falls with him.
TEST(KitCaptain, hook_blade_dies_with_its_captain)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);
    ASSERT_TRUE(cast(captain, 3, false).ok);
    walker* blade = blades(tw).front();
    captain->set_dead(1);
    blade->act();
    EXPECT_TRUE(blade->dead());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

// Act `blade` until `foe` is frozen (the snag) or the blade is gone; the
// act count, or -1 when it never snagged.
int act_until_snagged(walker* blade, walker* foe, int limit = 60)
{
    for (int tick = 1; tick <= limit; ++tick) {
        blade->act();
        if (frozen(foe) > 0)
            return tick;
        if (blade->dead())
            return -1;
    }
    return -1;
}

}  // namespace

// The first foe the spiral touches is cut, frozen for the whole reel and
// the stun after it (8 + 12 = 20), set on the captain, and reeled in 8 px a
// tick, the blade riding it, until it stands on the clear spot beside the
// captain (2 px from him); then the blade is spent. A soldier 56 px east of
// the captain (edge to edge 40) is passed on the way out and touched on the
// way back, on tick 28.
//
// Proof it can fail: `hook_reel_step = 8` -> 0 in the staged
// living-15-orc_captain.lua printed
//   Expected equality of these values:
//   (std::vector<short>{248, 240, 232, 224, 218}) Which is: { 248, 240, 232, 224, 218 }
//   path Which is: { 256, 256, 256, 256, 256, 256, 256, 256, 256 }
TEST(KitCaptain, hook_reels_the_foe_to_the_captains_feet)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 256, 200);
    ASSERT_NE(nullptr, captain);
    ASSERT_NE(nullptr, foe);
    ASSERT_TRUE(cast(captain, 3, false).ok);
    walker* blade = blades(tw).front();

    const float hp_before = foe->stats()->hitpoints();
    const int snag = act_until_snagged(blade, foe);
    EXPECT_EQ(28, snag) << "touched on the way back";
    ASSERT_GT(snag, 0);
    EXPECT_LT(foe->stats()->hitpoints(), hp_before) << "cut";
    EXPECT_EQ(kHookReelMax + kHookStun, frozen(foe)) << "frozen for the reel and the stun";
    EXPECT_EQ(captain, foe->foe());
    EXPECT_EQ(1, count_sound(tw, SOUND_CLANG));
    EXPECT_EQ(256, foe->xpos()) << "the snag itself moves nothing";
    ASSERT_FALSE(blade->dead()) << "the blade holds the foe";
    EXPECT_EQ(foe, blade->leader());
    EXPECT_EQ(1, blade->lineofsight()) << "on the hook";

    // 8 px a tick: 248, 240, 232, 224; the next step would reach the
    // captain, so it takes the spot beside him (218) and the blade goes.
    std::vector<short> path;
    while (!blade->dead() && path.size() < 20) {
        blade->act();
        path.push_back(foe->xpos());
        EXPECT_EQ(200, foe->ypos());
        if (!blade->dead()) {
            EXPECT_EQ(foe->xpos() + foe->sizex() / 2,
                      blade->xpos() + blade->sizex() / 2)
                << "the blade rides the foe in";
        }
    }
    EXPECT_EQ((std::vector<short>{248, 240, 232, 224, 218}), path);
    EXPECT_EQ(218, foe->xpos()) << "at the captain's feet, 2 px from him";
    EXPECT_EQ(kHookReelMax + kHookStun, frozen(foe))
        << "still frozen: only its own acts thaw it";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A foe snagged late on the way back is still reeled all the way, even
// while the captain walks off with it: at the snag the blade's lifetime
// becomes the reel's budget (8 ticks), so the spiral's clock, nearly spent,
// cannot cut the reel short. The foe turns up after tick 40, beside the
// captain's path, and the captain backs away 7 px a tick for six ticks.
//
// Proof it can fail: `blade.lifetime = t.hook_reel_max` deleted from the
// staged kit_captain.lua printed
//   Expected equality of these values: kHookReelMax Which is: 8
//   blade->lifetime() Which is: 5
//   ... captain->xpos() + captain->sizex() + 2 Which is: 176
//   foe->xpos() Which is: 186
//   ... 7 reel_acts Which is: 6
// (the spiral's clock ran out mid-reel and left the foe short of him).
TEST(KitCaptain, a_late_snag_still_reels_home)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);
    ASSERT_TRUE(cast(captain, 3, false).ok);
    walker* blade = blades(tw).front();
    for (int tick = 1; tick <= 40; ++tick)
        blade->act();
    ASSERT_FALSE(blade->dead());
    walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 226, 200);
    ASSERT_NE(nullptr, foe);
    const int snag = act_until_snagged(blade, foe, 8);
    ASSERT_GT(snag, 0);
    EXPECT_EQ(3, snag) << "touched on tick 43";
    EXPECT_EQ(kHookReelMax, blade->lifetime()) << "the reel's budget";

    int reel_acts = 0;
    while (!blade->dead() && reel_acts < 20) {
        if (reel_acts < 6)
            captain->setxy(static_cast<short>(captain->xpos() - 7), short{200});
        blade->act();
        ++reel_acts;
    }
    EXPECT_EQ(captain->xpos() + captain->sizex() + 2, foe->xpos())
        << "reeled to the captain's feet";
    EXPECT_EQ(7, reel_acts) << "more reel ticks than the spiral had left (5)";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A foe the blade's cut kills is not reeled: its body stays where it fell
// and the blade is spent.
TEST(KitCaptain, hook_kills_a_dying_foe_without_reeling_it)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 222, 222);
    ASSERT_NE(nullptr, captain);
    ASSERT_NE(nullptr, foe);
    foe->stats()->set_hitpoints(1.0f);
    ASSERT_TRUE(cast(captain, 3, false).ok);
    walker* blade = blades(tw).front();
    int acts = 0;
    while (!blade->dead() && acts < kHookTicks) {
        blade->act();
        ++acts;
    }
    ASSERT_TRUE(blade->dead());
    EXPECT_LT(acts, kHookTicks) << "spent on the cut, not worn out";
    EXPECT_TRUE(foe->dead()) << "the cut kills";
    EXPECT_EQ(222, foe->xpos()) << "not reeled";
    EXPECT_EQ(0, frozen(foe)) << "not stunned";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The reel never drags a foe through a wall: the first step that is not
// clear ends it where the foe stands, still frozen. A wall tile (x 224..239)
// stands between the captain and a soldier 56 px east of him.
//
// Proof it can fail: `if not og.spawn_spot_clear(foe, nx, ny) then` ->
// `if false then` in the staged effect_hook_blade.lua printed
//   Expected equality of these values: 240 foe->xpos() Which is: 224
// (dragged into the wall).
TEST(KitCaptain, reel_stops_at_a_wall)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    wall_tile(tw, 14, 12);
    wall_tile(tw, 14, 13);
    tw.world().mysmoother.set_target(tw.world().grid);
    walker* captain = add_captain(tw, 200, 200);
    walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 256, 200);
    ASSERT_NE(nullptr, captain);
    ASSERT_NE(nullptr, foe);
    ASSERT_TRUE(cast(captain, 3, false).ok);
    walker* blade = blades(tw).front();
    ASSERT_GT(act_until_snagged(blade, foe), 0);
    int acts = 0;
    while (!blade->dead() && acts < 20) {
        blade->act();
        ++acts;
    }
    EXPECT_TRUE(blade->dead());
    EXPECT_EQ(240, foe->xpos()) << "stopped at the wall's edge";
    EXPECT_EQ(kHookReelMax + kHookStun, frozen(foe)) << "frozen all the same";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The last step lands only on a clear spot beside the captain no more than
// one reel step away: with every spot beside him taken the foe stays where
// the reel left it, and with only the far side open it is never hauled past
// him to get there.
//
// Proof it can fail: `if og.max(dx, dy) <= step then` -> `if true then` in
// the staged effect_hook_blade.lua printed
//   Expected equality of these values: 220 foe->xpos() Which is: 182
TEST(KitCaptain, reel_never_hauls_a_foe_past_the_captain)
{
    for (const bool far_side_open : {false, true}) {
        og::test::ScopedHookFailureGuard guard;
        TestGameWorld tw;
        open_field(tw);
        tw.world().new_specials = 1;
        walker* captain = add_captain(tw, 200, 200);
        ASSERT_NE(nullptr, captain);
        // The eight spots beside the captain taken, or all but the west
        // three when the far side is open.
        if (!far_side_open) {
            fence_in(tw, captain, kUs);
        } else {
            for (const short x : {short{200}, short{218}}) {
                for (const short y : {short{182}, short{200}, short{218}}) {
                    if (x == 200 && y == 200)
                        continue;
                    ASSERT_NE(nullptr, add_living(tw, FAMILY_SOLDIER, kUs, x, y));
                }
            }
        }
        walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 220, 200);
        ASSERT_NE(nullptr, foe);
        ASSERT_TRUE(cast(captain, 3, false).ok);
        walker* blade = blades(tw).front();
        ASSERT_GT(act_until_snagged(blade, foe), 0) << "far side " << far_side_open;
        blade->act();
        EXPECT_TRUE(blade->dead()) << "far side " << far_side_open;
        EXPECT_EQ(220, foe->xpos())
            << "far side " << far_side_open << ": left where it was";
        EXPECT_EQ(0u, guard.count()) << guard.message();
    }
}

// The reel ends, blade and all, when the hooked foe dies on the way in or
// the captain leaves its floor.
//
// Proof it can fail: `if not foe or foe:floor() ~= owner:floor() then` ->
// `if not foe then` in the staged effect_hook_blade.lua printed (captain
// gone upstairs)
//   Value of: blade->dead() Actual: false Expected: true
//   Expected equality of these values: 248 foe->xpos() Which is: 240
// and -> `if foe:floor() ~= owner:floor() then` (no nil test) printed (foe
// died) the same blade->dead() line and a script error for the guard.
TEST(KitCaptain, reel_ends_when_the_foe_falls_or_the_captain_leaves_the_floor)
{
    for (const bool foe_dies : {true, false}) {
        og::test::ScopedHookFailureGuard guard;
        TestGameWorld tw;
        open_field(tw);
        tw.world().set_floor_count(2);
        tw.world().new_specials = 1;
        walker* captain = add_captain(tw, 200, 200);
        walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 256, 200);
        ASSERT_NE(nullptr, captain);
        ASSERT_NE(nullptr, foe);
        ASSERT_TRUE(cast(captain, 3, false).ok);
        walker* blade = blades(tw).front();
        ASSERT_GT(act_until_snagged(blade, foe), 0);
        blade->act();
        ASSERT_EQ(248, foe->xpos()) << "one reel step";
        if (foe_dies)
            foe->set_dead(1);
        else
            captain->set_floor(1);
        blade->act();
        EXPECT_TRUE(blade->dead()) << (foe_dies ? "foe died" : "captain left");
        EXPECT_EQ(248, foe->xpos()) << "no further";
        EXPECT_EQ(0u, guard.count()) << guard.message();
    }
}

// While it spins the blade is a shield: a foe's shot that closes in is cut
// down and costs the blade its damage in hitpoints; a shot heavier than the
// blade's hitpoints breaks it.
TEST(KitCaptain, hook_eats_incoming_projectiles)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);
    ASSERT_TRUE(cast(captain, 3, false).ok);
    walker* blade = blades(tw).front();
    blade->act();  // tick 1

    // Where the blade will be on tick 2: up and left, 16 px out.
    const short base_x = static_cast<short>(
        captain->xpos() + captain->sizex() / 2 - blade->sizex() / 2);
    const short base_y = static_cast<short>(
        captain->ypos() + captain->sizey() / 2 - blade->sizey() / 2);
    const auto at2 = hook_offset_at(2);
    walker* knife = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->set_team_num(kThem);
    knife->setxy(static_cast<short>(base_x + std::lround(at2.first)),
                 static_cast<short>(base_y + std::lround(at2.second)));
    knife->set_damage(5.0f);
    blade->act();  // tick 2
    EXPECT_TRUE(knife->dead()) << "the shot is cut down";
    EXPECT_FLOAT_EQ(static_cast<float>(kHookHp - 5),
                    blade->stats()->hitpoints());
    EXPECT_FALSE(blade->dead());

    walker* boulder = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, boulder);
    boulder->set_team_num(kThem);
    boulder->set_damage(100.0f);
    // Where the blade will be on tick 3.
    const auto at3 = hook_offset_at(3);
    boulder->setxy(static_cast<short>(base_x + std::lround(at3.first)),
                   static_cast<short>(base_y + std::lround(at3.second)));
    blade->act();
    EXPECT_TRUE(boulder->dead());
    EXPECT_TRUE(blade->dead()) << "a shot heavier than the blade breaks it";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The blade's guard cuts down shots, not scenery: an enemy war banner and an
// enemy bone wall inside the guard's reach are passed by (no hitpoints
// lost on either side), while a shot beside them is still cut down. Foes
// must chop scenery down. The scenery stands right where the blade will be
// on tick 2, so only the blocks_placement test can spare it.
//
// RED without it (the guard as the shield's): "scenery is passed by"
// fails for both (dead on tick 2) and the blade reads 7 hitpoints
// (30 - 9 - 9 - 5), not 25.
TEST(KitCaptain, hook_guard_passes_scenery_by)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);
    ASSERT_TRUE(cast(captain, 3, false).ok);
    walker* blade = blades(tw).front();
    blade->act();  // tick 1

    const short base_x = static_cast<short>(
        captain->xpos() + captain->sizex() / 2 - blade->sizex() / 2);
    const short base_y = static_cast<short>(
        captain->ypos() + captain->sizey() / 2 - blade->sizey() / 2);
    const auto at2 = hook_offset_at(2);
    const short spot_x = static_cast<short>(base_x + std::lround(at2.first));
    const short spot_y = static_cast<short>(base_y + std::lround(at2.second));
    std::vector<walker*> scenery;
    for (const char* id : {"core:war_banner", "core:bone_wall"}) {
        walker* piece = tw.world().add_weap_ob(Order::Weapon, weapon_family(id));
        ASSERT_NE(nullptr, piece) << id;
        const WeaponFamilyDescriptor* wfd =
            get_weapon_family_descriptor(piece->family());
        ASSERT_NE(nullptr, wfd);
        ASSERT_TRUE(wfd->blocks_placement) << id << " is solid scenery";
        piece->set_team_num(kThem);
        piece->setxy(spot_x, spot_y);
        piece->set_damage(9.0f);
        piece->stats()->set_max_hitpoints(120.0f);
        piece->stats()->set_hitpoints(120.0f);
        piece->set_act_type(ACT_SIT);
        scenery.push_back(piece);
    }
    walker* knife = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->set_team_num(kThem);
    knife->setxy(spot_x, spot_y);
    knife->set_damage(5.0f);

    blade->act();  // tick 2: the blade sits on all three
    EXPECT_TRUE(knife->dead()) << "the shot is still cut down";
    for (walker* piece : scenery) {
        EXPECT_FALSE(piece->dead()) << "family " << int(piece->family())
                                    << ": scenery is passed by";
        EXPECT_FLOAT_EQ(120.0f, piece->stats()->hitpoints());
    }
    EXPECT_FLOAT_EQ(static_cast<float>(kHookHp - 5),
                    blade->stats()->hitpoints())
        << "only the shot cost the blade";
    EXPECT_FALSE(blade->dead());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Shift + slot 3: three knives, at the facing and one point either side,
// paid by the special (fire()'s per-knife charge is pre-paid), the aim
// restored. A captain parked on the curdir -1 sentinel fans around its aim;
// a captain with no aim at all fans downward. Busy refuses.
TEST(KitCaptain, knife_fan_throws_three)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);

    const auto knives_now = [&tw]() {
        return live_of(tw.world().weaplist, Order::Weapon, FAMILY_KNIFE);
    };
    // The three new knives' flight vectors (lastx, lasty), ordered by the
    // side they lean to. Each fire() adds its own small waver, so the test
    // reads the spread, not exact unit vectors.
    const auto fan = [&](int curdir, float aim_x, float aim_y) {
        const std::size_t before = knives_now().size();
        captain->set_curdir(static_cast<char>(curdir));
        captain->set_lastx(aim_x);
        captain->set_lasty(aim_y);
        captain->set_busy(0);
        const Cast c = cast(captain, 3, true);
        EXPECT_TRUE(c.ok) << c.reason;
        EXPECT_FLOAT_EQ(30.0f, c.mp_spent) << "KNIFE FAN's price, no more";
        EXPECT_FLOAT_EQ(aim_x, captain->lastx()) << "aim restored";
        EXPECT_FLOAT_EQ(aim_y, captain->lasty());
        const auto now = knives_now();
        EXPECT_EQ(before + 3, now.size()) << "three knives";
        std::vector<std::pair<float, float>> v;
        for (std::size_t i = before; i < now.size(); ++i)
            v.push_back({now[i]->lastx(), now[i]->lasty()});
        return v;
    };
    // Facing right: all three fly east; one climbs, one falls, one between.
    auto v = fan(FACE_RIGHT, 1, 0);
    ASSERT_EQ(3u, v.size());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });
    for (const auto& k : v)
        EXPECT_GT(k.first, 0.0f) << "east";
    EXPECT_LT(v[0].second, -0.5f * v[0].first) << "up and east";
    EXPECT_LT(std::abs(v[1].second), 0.5f * v[1].first) << "straight east";
    EXPECT_GT(v[2].second, 0.5f * v[2].first) << "down and east";
    // The -1 sentinel reads the aim: west.
    v = fan(-1, -1, 0);
    ASSERT_EQ(3u, v.size());
    for (const auto& k : v)
        EXPECT_LT(k.first, 0.0f) << "west";
    // A diagonal walk leaves a fractional aim; the fan puts it back whole
    // (a truncating restore would read 0, 0 here).
    v = fan(FACE_DOWN_RIGHT, 0.75f, 0.75f);
    ASSERT_EQ(3u, v.size());
    // No aim at all: down.
    v = fan(-1, 0, 0);
    ASSERT_EQ(3u, v.size());
    std::sort(v.begin(), v.end());
    for (const auto& k : v)
        EXPECT_GT(k.second, 0.0f) << "south";
    EXPECT_LT(v[0].first, -0.5f * v[0].second) << "down and west";
    EXPECT_LT(std::abs(v[1].first), 0.5f * v[1].second) << "straight down";
    EXPECT_GT(v[2].first, 0.5f * v[2].second) << "down and east";

    captain->set_busy(4.0f);
    const Cast busy = cast(captain, 3, true);
    EXPECT_EQ("SPECIAL BUSY", busy.reason);
    EXPECT_FLOAT_EQ(0.0f, busy.mp_spent);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------- WAR BANNER / WARBAND

// The banner goes up only on a corpse the captain stands on (its floor,
// within the plant reach) and uses the corpse up.
TEST(KitCaptain, banner_needs_a_corpse_and_consumes_it)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().set_floor_count(2);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);

    Cast c = cast(captain, 4, false);
    EXPECT_EQ("NO CORPSE TO PLANT ON", c.reason);
    walker* stain = add_stain(tw, captain);
    ASSERT_NE(nullptr, stain);
    stain->set_floor(1);
    c = cast(captain, 4, false);
    EXPECT_EQ("NO CORPSE TO PLANT ON", c.reason) << "a corpse on another floor";
    stain->set_floor(0);
    stain->setxy(stain->xpos() + 20, stain->ypos() + 20);
    c = cast(captain, 4, false);
    EXPECT_EQ("NO CORPSE TO PLANT ON", c.reason) << "out of reach";
    EXPECT_FLOAT_EQ(0.0f, c.mp_spent);
    EXPECT_TRUE(banners(tw).empty());

    stain->center_on(captain);
    c = cast(captain, 4, false);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(80.0f, c.mp_spent);
    EXPECT_EQ(1, stain->dead()) << "the fallen one's head goes on the pole";
    ASSERT_EQ(1u, banners(tw).size());
    walker* banner = banners(tw).front();
    EXPECT_EQ(captain, banner->owner());
    EXPECT_EQ(kUs, banner->team_num());
    EXPECT_FLOAT_EQ(static_cast<float>(kBannerHpBase + 10 * 10),
                    banner->stats()->hitpoints());
    EXPECT_EQ(kBannerTicks, banner->lifetime());
    EXPECT_EQ(ACT_SIT, banner->act_type());
    EXPECT_TRUE(notified(tw, "raises a war banner!"));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

// A planted banner for the aura tests: captain at (200,200), banner on the
// corpse at its feet.
walker* plant_banner(TestGameWorld& tw, walker*& captain)
{
    captain = add_captain(tw, 200, 200);
    if (captain == nullptr)
        return nullptr;
    add_stain(tw, captain);
    const Cast c = cast(captain, 4, false);
    EXPECT_TRUE(c.ok) << c.reason;
    const auto b = banners(tw);
    return b.empty() ? nullptr : b.front();
}

}  // namespace

// Every banner_pulse ticks: every ally in banner_radius (any family) heals
// banner_regen; every LOWER-level living foe in it is frightened away; a
// foe of the banner's level or above stands its ground. Between pulses
// nothing happens. The banner counts its lifetime down every tick.
TEST(KitCaptain, banner_regens_allies_and_frights_lower_foes)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = nullptr;
    walker* banner = plant_banner(tw, captain);
    ASSERT_NE(nullptr, banner);
    walker* ally = add_living(tw, FAMILY_ELF, kUs, 240, 200);
    walker* weak = add_living(tw, FAMILY_SOLDIER, kThem, 260, 200, 3);
    walker* strong = add_living(tw, FAMILY_SOLDIER, kThem, 200, 260, 12);
    walker* far_ally = add_living(tw, FAMILY_ELF, kUs, 400, 400);
    ASSERT_TRUE(ally && weak && strong && far_ally);
    ally->stats()->set_hitpoints(100.0f);
    far_ally->stats()->set_hitpoints(100.0f);

    tw.world().tick_count_ = 801;  // not a pulse tick
    banner->act();
    EXPECT_FLOAT_EQ(100.0f, ally->stats()->hitpoints());
    EXPECT_EQ(nullptr, front_command(weak));
    EXPECT_EQ(kBannerTicks - 1, banner->lifetime());

    tw.world().tick_count_ = 808;  // 808 % 8 == 0
    banner->act();
    EXPECT_FLOAT_EQ(101.0f, ally->stats()->hitpoints()) << "regenerates";
    EXPECT_FLOAT_EQ(100.0f, far_ally->stats()->hitpoints()) << "out of reach";
    const command* flee = front_command(weak);
    ASSERT_NE(nullptr, flee) << "the weaker foe is frightened";
    EXPECT_EQ(1, flee->com1) << "away from the banner (east)";
    EXPECT_EQ(nullptr, front_command(strong)) << "a stronger foe holds";
    EXPECT_EQ(kBannerTicks - 2, banner->lifetime());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A banner wears out: at the end of its lifetime it falls by itself.
TEST(KitCaptain, banner_falls_when_its_time_is_up)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = nullptr;
    walker* banner = plant_banner(tw, captain);
    ASSERT_NE(nullptr, banner);
    banner->set_lifetime(1);
    tw.world().tick_count_ = 1;
    banner->act();
    EXPECT_FALSE(banner->dead());
    banner->act();
    EXPECT_TRUE(banner->dead());
    EXPECT_TRUE(notified(tw, "The banner falls."));
    EXPECT_EQ(1, count_sound(tw, SOUND_EXPLODE));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A second banner strikes the first: the captain plants on one corpse,
// walks to another 40 px off and plants again. The old banner comes down
// quietly (a flash where it stood, no boom, no "The banner falls."), so one
// banner stands, on the second corpse, and the raise notice covers it.
//
// Proof it can fail: `old:death()` deleted from the staged kit_captain.lua
// printed
//   Value of: first->dead() Actual: false Expected: true
//   Expected equality of these values: 1u standing.size() Which is: 2
// and the `if self:leader() then return true end` deleted from the staged
// weapon_banner.lua printed
//   Expected equality of these values: 0 count_sound(tw, SOUND_EXPLODE) Which is: 1
//   Value of: notified(tw, "The banner falls.") Actual: true Expected: false
TEST(KitCaptain, second_banner_strikes_the_first_quietly)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = nullptr;
    walker* first = plant_banner(tw, captain);
    ASSERT_NE(nullptr, first);
    const short first_cx = static_cast<short>(first->xpos() + first->sizex() / 2);
    const int flashes_before =
        static_cast<int>(live_of(tw.world().oblist, Order::FX, FAMILY_FLASH).size());

    captain->setxy(240, 200);
    walker* corpse = add_stain(tw, captain);
    ASSERT_NE(nullptr, corpse);
    const Cast c = cast(captain, 4, false);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_TRUE(first->dead()) << "the old banner is struck";
    const auto standing = banners(tw);
    ASSERT_EQ(1u, standing.size()) << "one banner stands";
    EXPECT_EQ(corpse->xpos() + corpse->sizex() / 2,
              standing.front()->xpos() + standing.front()->sizex() / 2)
        << "on the second corpse";
    EXPECT_EQ(1, corpse->dead());
    EXPECT_EQ(0, count_sound(tw, SOUND_EXPLODE)) << "no boom";
    EXPECT_FALSE(notified(tw, "The banner falls.")) << "no fall notice";
    const auto flashes = live_of(tw.world().oblist, Order::FX, FAMILY_FLASH);
    ASSERT_EQ(flashes_before + 1, static_cast<int>(flashes.size()))
        << "one flash, for the old banner";
    EXPECT_EQ(first_cx, flashes.back()->xpos() + flashes.back()->sizex() / 2)
        << "where the old banner stood";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The corpse comes first: a second press with no corpse underfoot is
// refused, spends nothing, and the standing banner stays up.
//
// Proof it can fail: the old-banner strike put above the corpse check in
// the staged kit_captain.lua printed
//   Value of: first->dead() Actual: true Expected: false
//   Expected equality of these values: 1u banners(tw).size() Which is: 0
TEST(KitCaptain, refused_second_plant_keeps_the_first)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = nullptr;
    walker* first = plant_banner(tw, captain);
    ASSERT_NE(nullptr, first);
    captain->setxy(300, 300);
    const Cast c = cast(captain, 4, false);
    EXPECT_FALSE(c.ok);
    EXPECT_EQ("NO CORPSE TO PLANT ON", c.reason);
    EXPECT_FLOAT_EQ(0.0f, c.mp_spent);
    EXPECT_FALSE(first->dead()) << "the first banner stands";
    EXPECT_EQ(1u, banners(tw).size());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Grunts marching to the old banner turn to the new one: the captain moved
// his rally point.
//
// Proof it can fail: the grunt re-point loop deleted from the staged
// kit_captain.lua printed
//   Expected equality of these values: new_cx Which is: 268
//   go->com1 Which is: 208
TEST(KitCaptain, grunts_turn_to_the_new_banner)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = nullptr;
    walker* first = plant_banner(tw, captain);
    ASSERT_NE(nullptr, first);
    ASSERT_TRUE(cast(captain, 4, true).ok);
    const auto grunts = grunts_of(tw, captain);
    ASSERT_EQ(2u, grunts.size());
    for (walker* g : grunts) {
        const command* go = front_command(g);
        ASSERT_NE(nullptr, go);
        ASSERT_EQ(first->xpos() + first->sizex() / 2, go->com1)
            << "marching to the first banner";
    }

    captain->setxy(260, 260);
    walker* corpse = add_stain(tw, captain);
    ASSERT_NE(nullptr, corpse);
    ASSERT_TRUE(cast(captain, 4, false).ok);
    const auto standing = banners(tw);
    ASSERT_EQ(1u, standing.size());
    const int new_cx = standing.front()->xpos() + standing.front()->sizex() / 2;
    const int new_cy = standing.front()->ypos() + standing.front()->sizey() / 2;
    ASSERT_NE(first->xpos() + first->sizex() / 2, new_cx);
    for (walker* g : grunts) {
        EXPECT_EQ(1u, g->stats()->commands.size()) << "the old order is gone";
        const command* go = front_command(g);
        ASSERT_NE(nullptr, go);
        EXPECT_EQ(COMMAND_GOTO, go->commandtype);
        EXPECT_EQ(new_cx, go->com1) << "to the new banner";
        EXPECT_EQ(new_cy, go->com2);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// An ally in the banner's radius never runs: the yell for help rallies its
// friends but queues no flee walk, and a fright binding skips it. Out of
// the radius, or with the setting off, it runs as before.
TEST(KitCaptain, rallied_walker_does_not_flee_when_hit)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = nullptr;
    walker* banner = plant_banner(tw, captain);
    ASSERT_NE(nullptr, banner);
    walker* ally = add_living(tw, FAMILY_ELF, kUs, 250, 200);
    walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 280, 200);
    ASSERT_TRUE(ally && foe);

    const auto runs = [&]() {
        ally->stats()->commands.clear();
        ally->stats()->yell_for_help(foe);
        const command* walk = front_command(ally);
        const bool ran = walk != nullptr && walk->commandtype == COMMAND_WALK;
        ally->stats()->commands.clear();
        return ran;
    };
    EXPECT_TRUE(og::sim::fearless(*ally));
    EXPECT_FALSE(runs()) << "rallied: holds";
    EXPECT_FALSE(og::sim::fearless(*foe)) << "the banner rallies its own side";

    tw.world().new_specials = 0;
    EXPECT_FALSE(og::sim::fearless(*ally)) << "setting off: no aura";
    EXPECT_TRUE(runs());
    tw.world().new_specials = 1;

    ally->setxy(450, 450);
    EXPECT_FALSE(og::sim::fearless(*ally)) << "out of the radius";
    EXPECT_TRUE(runs());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Foes chop the banner (it is auto-attackable and has hitpoints); it is
// solid scenery for placement and for a foe's body, and the captain's own
// side walks through it.
TEST(KitCaptain, banner_is_chopped_by_foes_and_passes_allies)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = nullptr;
    walker* banner = plant_banner(tw, captain);
    ASSERT_NE(nullptr, banner);
    const WeaponFamilyDescriptor* wfd =
        get_weapon_family_descriptor(banner->family());
    ASSERT_NE(nullptr, wfd);
    EXPECT_TRUE(wfd->is_auto_attackable);
    EXPECT_TRUE(wfd->blocks_placement);
    EXPECT_EQ(96, wfd->rally_radius);

    captain->setxy(400, 400);
    walker* ally = add_living(tw, FAMILY_SOLDIER, kUs, 300, 300);
    walker* foe = add_living(tw, FAMILY_SOLDIER, kThem, 330, 300);
    ASSERT_TRUE(ally && foe);
    const float bx = static_cast<float>(banner->xpos());
    const float by = static_cast<float>(banner->ypos());
    EXPECT_TRUE(tw.world().query_object_passable(bx, by, ally))
        << "allies pass";
    EXPECT_FALSE(tw.world().query_object_passable(bx, by, foe))
        << "a foe's body is stopped";

    banner->stats()->set_hitpoints(1.0f);
    foe->setxy(static_cast<short>(banner->xpos() + 14), banner->ypos());
    int swings = 0;
    while (!banner->dead() && swings < 50) {
        foe->set_busy(0);
        foe->attack(banner);
        ++swings;
    }
    EXPECT_TRUE(banner->dead()) << "chopped down";
    EXPECT_TRUE(notified(tw, "The banner falls."));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// WARBAND: two grunts at level 10 (one more every three levels above it)
// come in on the map edge nearest the captain, fearless, owned by him and
// timed like a summoned elemental, and run to the centre of his standing
// banner (or to him). Each edge is tried. The cap refuses a seventh; a
// walled edge refuses with nothing spent.
// RED with the goal at the banner's top-left (the first shape): com1 and
// com2 read 6 and 11 short of the centre of the 12x22 banner.
TEST(KitCaptain, warband_arrives_from_the_nearest_edge_fearless_and_runs_to_the_banner)
{
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        open_field(tw);
        tw.world().new_specials = 1;
        walker* captain = nullptr;
        walker* banner = plant_banner(tw, captain);
        ASSERT_NE(nullptr, banner);
        captain->setxy(40, 200);  // nearest edge: the west
        const Cast c = cast(captain, 4, true);
        ASSERT_TRUE(c.ok) << c.reason;
        EXPECT_FLOAT_EQ(80.0f, c.mp_spent);
        const auto grunts = grunts_of(tw, captain);
        ASSERT_EQ(2u, grunts.size());
        for (std::size_t i = 0; i < grunts.size(); ++i) {
            walker* g = grunts[i];
            EXPECT_EQ(static_cast<int>(i) * GRID_SIZE, g->xpos())
                << "on the west edge, scanning inward";
            EXPECT_EQ(200, g->ypos());
            EXPECT_EQ(kUs, g->team_num());
            EXPECT_EQ(5, g->stats()->level()) << "half the captain's level";
            EXPECT_EQ(kWarbandLifetime, g->lifetime());
            EXPECT_NE(0, g->kit_state() & KIT_FEARLESS);
            EXPECT_TRUE(og::sim::fearless(*g));
            EXPECT_EQ(captain, g->leader());
            const command* go = front_command(g);
            ASSERT_NE(nullptr, go);
            EXPECT_EQ(COMMAND_GOTO, go->commandtype);
            EXPECT_EQ(banner->xpos() + banner->sizex() / 2, go->com1)
                << "to the banner's centre";
            EXPECT_EQ(banner->ypos() + banner->sizey() / 2, go->com2);
        }
    }

    struct EdgeCase {
        short x, y, level;
        int want_x, want_y;  // the first grunt
        std::size_t count;
    };
    // 512 x 512 px field. No banner: they run to the captain.
    const std::array<EdgeCase, 3> cases = {{
        // On the east and south the grid check refuses a body whose far
        // edge reaches the map's last pixel (x + size >= width), so the
        // scan starts one tile in; the next two tiles (480 and 464) overlap
        // the captain's box (470..485), so the first grunt is four in.
        {470, 200, 10, 512 - 4 * GRID_SIZE, 200, 2},  // east
        {200, 30, 16, 200, 0, 4},                     // north, level 16: 2 + 2
        {200, 470, 10, 200, 512 - 4 * GRID_SIZE, 2},  // south
    }};
    for (const EdgeCase& e : cases) {
        TestGameWorld tw;
        open_field(tw);
        tw.world().new_specials = 1;
        walker* captain = add_captain(tw, e.x, e.y, e.level);
        ASSERT_NE(nullptr, captain);
        const Cast c = cast(captain, 4, true);
        ASSERT_TRUE(c.ok) << c.reason;
        const auto grunts = grunts_of(tw, captain);
        ASSERT_EQ(e.count, grunts.size()) << "captain at " << e.x << "," << e.y;
        EXPECT_EQ(e.want_x, grunts.front()->xpos());
        EXPECT_EQ(e.want_y, grunts.front()->ypos());
        for (const walker* g : grunts)
            EXPECT_FALSE(g->xpos() + g->sizex() > captain->xpos() &&
                         g->xpos() < captain->xpos() + captain->sizex() &&
                         g->ypos() + g->sizey() > captain->ypos() &&
                         g->ypos() < captain->ypos() + captain->sizey())
                << "a grunt at " << g->xpos() << "," << g->ypos()
                << " stands inside the captain";
        const command* go = front_command(grunts.front());
        ASSERT_NE(nullptr, go);
        EXPECT_EQ(captain->xpos(), go->com1) << "no banner: to the captain";
    }

    {
        TestGameWorld tw;
        open_field(tw);
        tw.world().new_specials = 1;
        walker* captain = add_captain(tw, 40, 200, 19);  // 2 + 3 = 5 grunts
        ASSERT_NE(nullptr, captain);
        ASSERT_TRUE(cast(captain, 4, true).ok);
        // The scan walks the captain's own row: the tiles at 32 and 48
        // overlap his box (40..55) and are skipped, never filled.
        EXPECT_EQ((std::vector<short>{0, 16, 64, 80, 96}),
                  xs_of(grunts_of(tw, captain)));
        ASSERT_TRUE(cast(captain, 4, true).ok);
        EXPECT_EQ((std::vector<short>{0, 16, 64, 80, 96, 112}),
                  xs_of(grunts_of(tw, captain)))
            << "topped up to the cap, past the grunts already standing";
        const Cast full = cast(captain, 4, true);
        EXPECT_EQ("WARBAND ALREADY HERE", full.reason);
        EXPECT_FLOAT_EQ(0.0f, full.mp_spent);
    }

    {
        // A captain 20 px from the west edge: the tile at 16 would cover
        // 16..31, inside his box (20..35), and so would 32. The second
        // grunt comes in at 48, never on top of him.
        TestGameWorld tw;
        open_field(tw);
        tw.world().new_specials = 1;
        walker* captain = add_captain(tw, 20, 200);
        ASSERT_NE(nullptr, captain);
        ASSERT_EQ(16, captain->sizex());
        ASSERT_TRUE(cast(captain, 4, true).ok);
        const auto grunts = grunts_of(tw, captain);
        EXPECT_EQ((std::vector<short>{0, 48}), xs_of(grunts));
        for (walker* g : grunts)
            EXPECT_EQ(200, g->ypos());
    }

    {
        TestGameWorld tw;
        open_field(tw);
        tw.world().new_specials = 1;
        for (int tx = 0; tx < 16; ++tx) {
            wall_tile(tw, tx, 12);
            wall_tile(tw, tx, 13);
        }
        tw.world().mysmoother.set_target(tw.world().grid);
        walker* captain = add_captain(tw, 40, 200);
        ASSERT_NE(nullptr, captain);
        const Cast c = cast(captain, 4, true);
        EXPECT_EQ("NO ROOM AT THE EDGE", c.reason);
        EXPECT_FLOAT_EQ(0.0f, c.mp_spent);
        EXPECT_TRUE(grunts_of(tw, captain).empty());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The grunts are the captain's: when he dies they go with him.
TEST(KitCaptain, warband_dies_with_the_captain)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 40, 200);
    ASSERT_NE(nullptr, captain);
    ASSERT_TRUE(cast(captain, 4, true).ok);
    const auto grunts = grunts_of(tw, captain);
    ASSERT_EQ(2u, grunts.size());
    captain->set_dead(1);
    for (walker* g : grunts) {
        g->act();
        EXPECT_TRUE(g->dead());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------- bot gates

namespace {

struct BotScene {
    TestGameWorld tw;
    walker* captain = nullptr;
    living* bot = nullptr;
    const FamilyDescriptor* fd = nullptr;

    BotScene()
    {
        open_field(tw);
        tw.world().new_specials = 1;
        captain = add_captain(tw, 200, 200);
        bot = dynamic_cast<living*>(captain);
        fd = get_family_descriptor(FAMILY_BIG_ORC);
    }

    // The gate's answer for `slot`, and the shift it chose (-1: untouched).
    std::pair<bool, int> ask(int slot)
    {
        bot->set_current_special(static_cast<char>(slot));
        bot->set_shifter_down(7);
        const bool yes = og::test::check_special_ai(*fd, bot);
        const int shift = bot->shifter_down() == 7 ? -1 : bot->shifter_down();
        return {yes, shift};
    }
};

}  // namespace

// Slot 1: howl at a foe within 130 (the orc's own gate). HOWL has no
// alternate, so the gate leaves the engine's shift coin alone: every
// answer reads "untouched" (-1). A corpse underfoot and a wound change
// nothing here; the meal is slot 2's.
//
// Proof it can fail: `self:set_shifter_down(0)` added to the staged
// kit_captain.lua's ai_howl printed
//   Expected equality of these values: (std::pair<bool, int>{false, -1})
//   Which is: (false, -1) s.ask(1) Which is: (false, 0)
// and the same (true, 0) for each howl.
TEST(KitCaptain, ai_howl_fires_when_a_foe_is_near)
{
    og::test::ScopedHookFailureGuard guard;
    BotScene s;
    ASSERT_NE(nullptr, s.bot);
    EXPECT_EQ((std::pair<bool, int>{false, -1}), s.ask(1)) << "nobody near";
    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kThem, 300, 200);
    ASSERT_NE(nullptr, foe);
    EXPECT_EQ((std::pair<bool, int>{true, -1}), s.ask(1)) << "howl";
    walker* stain = add_stain(s.tw, s.captain);
    ASSERT_NE(nullptr, stain);
    s.captain->stats()->set_hitpoints(100.0f);
    EXPECT_EQ((std::pair<bool, int>{true, -1}), s.ask(1))
        << "hurt on a corpse: still a howl";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 2: eat when below 60 % health and standing on a corpse; otherwise
// hold. The shift is left alone throughout.
// Exactly 60 % is not below 60 %: the test is made from whole numbers
// (hp x 5 < max x 3), where a float 0.6 (0.6000000238) would call 120 of
// 200 hurt.
//
// Proof it can fail: `og.fmul(self.max_hp, 3)` ->
// `og.fmul(og.fmul(self.max_hp, 0.6), 5)` in the staged kit_captain.lua
// printed, at exactly 60 %,
//   Expected equality of these values: (std::pair<bool, int>{false, -1})
//   Which is: (false, -1) s.ask(2) Which is: (true, -1)
TEST(KitCaptain, ai_eat_fires_when_hurt_on_a_corpse)
{
    og::test::ScopedHookFailureGuard guard;
    BotScene s;
    ASSERT_NE(nullptr, s.bot);
    walker* stain = add_stain(s.tw, s.captain);
    ASSERT_NE(nullptr, stain);
    EXPECT_EQ((std::pair<bool, int>{false, -1}), s.ask(2))
        << "full health on a corpse: hold";
    stain->set_dead(1);
    s.captain->stats()->set_hitpoints(100.0f);  // 100 < 0.6 * 200
    EXPECT_EQ((std::pair<bool, int>{false, -1}), s.ask(2))
        << "hurt, no corpse: hold";
    walker* corpse = add_stain(s.tw, s.captain);
    ASSERT_NE(nullptr, corpse);
    corpse->setxy(corpse->xpos() + 4, corpse->ypos() + 4);  // 32 > 24 squared px
    EXPECT_EQ((std::pair<bool, int>{false, -1}), s.ask(2))
        << "hurt, the corpse out of reach: hold";
    corpse->center_on(s.captain);
    EXPECT_EQ((std::pair<bool, int>{true, -1}), s.ask(2)) << "hurt: eat";
    s.captain->stats()->set_hitpoints(120.0f);
    EXPECT_EQ((std::pair<bool, int>{false, -1}), s.ask(2))
        << "at exactly 60 %: hold";
    s.captain->stats()->set_hitpoints(119.0f);
    EXPECT_EQ((std::pair<bool, int>{true, -1}), s.ask(2)) << "just below: eat";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 3: hook a foe within 90 while no blade is out; fan (shift) into two
// or more; otherwise hold.
TEST(KitCaptain, ai_hook_fires_when_a_foe_is_near_and_fans_into_a_crowd)
{
    og::test::ScopedHookFailureGuard guard;
    BotScene s;
    ASSERT_NE(nullptr, s.bot);
    EXPECT_FALSE(s.ask(3).first) << "nobody near";
    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kThem, 260, 200);
    ASSERT_NE(nullptr, foe);
    EXPECT_EQ((std::pair<bool, int>{true, 0}), s.ask(3)) << "hook";
    ASSERT_TRUE(cast(s.captain, 3, false).ok);
    EXPECT_FALSE(s.ask(3).first) << "blade out, one foe: hold";
    add_living(s.tw, FAMILY_SOLDIER, kThem, 200, 260);
    EXPECT_EQ((std::pair<bool, int>{true, 1}), s.ask(3)) << "fan";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 4: plant a banner on a corpse with two allies around and none
// standing; call the warband (shift) when two foes press within 120 and no
// grunt is left; otherwise hold.
TEST(KitCaptain, ai_banner_plants_among_friends_and_calls_the_warband_when_pressed)
{
    og::test::ScopedHookFailureGuard guard;
    BotScene s;
    ASSERT_NE(nullptr, s.bot);
    EXPECT_FALSE(s.ask(4).first) << "nothing to do";
    add_stain(s.tw, s.captain);
    add_living(s.tw, FAMILY_SOLDIER, kUs, 240, 200);
    EXPECT_FALSE(s.ask(4).first) << "one ally is not a crowd";
    add_living(s.tw, FAMILY_SOLDIER, kUs, 200, 240);
    EXPECT_EQ((std::pair<bool, int>{true, 0}), s.ask(4)) << "plant";
    ASSERT_TRUE(cast(s.captain, 4, false).ok);
    EXPECT_FALSE(s.ask(4).first) << "a banner already stands";

    add_living(s.tw, FAMILY_SOLDIER, kThem, 280, 200);
    add_living(s.tw, FAMILY_SOLDIER, kThem, 200, 280);
    EXPECT_EQ((std::pair<bool, int>{true, 1}), s.ask(4)) << "warband";
    s.captain->setxy(40, 200);
    ASSERT_TRUE(cast(s.captain, 4, true).ok);
    s.captain->setxy(200, 200);
    EXPECT_FALSE(s.ask(4).first) << "a grunt is still alive";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// End to end through the engine's bot path: check_special() draws the
// shift coin, the gate decides, and special() casts the howl.
TEST(KitCaptain, bot_captain_howls_through_check_special)
{
    og::test::ScopedHookFailureGuard guard;
    BotScene s;
    ASSERT_NE(nullptr, s.bot);
    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kThem, 300, 200);
    ASSERT_NE(nullptr, foe);
    foe->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    foe->myguy->constitution = 0;  // stun = 10 + level roll: never 0
    s.bot->set_current_special(1);
    s.tw.world().rng_.state_ = 4242u;
    ASSERT_TRUE(s.bot->check_special());
    ASSERT_TRUE(s.bot->special());
    EXPECT_GT(frozen(foe), 0);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// End to end through the engine's bot path on slot 2: a captain at 50 of
// 200 hp standing on a corpse; check_special() draws its coin and the gate
// says yes; special() eats the corpse.
//
// Proof it can fail: `if not hurt then return false end` ->
// `if hurt then return false end` in the staged kit_captain.lua printed
//   Value of: s.bot->check_special() Actual: false Expected: true
TEST(KitCaptain, bot_captain_eats_through_check_special)
{
    og::test::ScopedHookFailureGuard guard;
    BotScene s;
    ASSERT_NE(nullptr, s.bot);
    s.captain->stats()->set_hitpoints(50.0f);
    walker* stain = add_stain(s.tw, s.captain, 3);
    ASSERT_NE(nullptr, stain);
    s.bot->set_current_special(2);
    s.tw.world().rng_.state_ = 4242u;
    ASSERT_TRUE(s.bot->check_special());
    EXPECT_EQ(2, s.bot->current_special()) << "the meal is affordable";
    ASSERT_TRUE(s.bot->special());
    EXPECT_FLOAT_EQ(65.0f, s.captain->stats()->hitpoints())
        << "50 + corpse level 3 * corpse_heal_per_level 5";
    EXPECT_EQ(1, stain->dead()) << "the corpse is eaten";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A freshly promoted captain restarts at level 1, so the player's cycling
// key walks HOWL alone until level 4, then HOWL and EAT CORPSE, the hook at
// 7 and the banner at 10, wrapping to slot 1 each time.
//
// Proof it can fail: the eat_corpse row deleted from the staged
// living-15-orc_captain.lua (HOOK BLADE back on slot 2) printed
//   Expected equality of these values: "12341" walk(10) Which is: "12312"
//   Expected equality of these values: "EAT CORPSE" ... Which is: "HOOK BLADE"
//   Expected equality of these values: "NONE" ... Which is: "KNIFE FAN"
TEST(KitCaptain, fresh_captain_has_howl_only_until_level_4)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    walker* captain = add_captain(tw, 200, 200, 1);
    ASSERT_NE(nullptr, captain);
    const auto walk = [&](short level) {
        captain->stats()->set_level(level);
        captain->set_current_special(1);
        std::string seen = "1";
        for (int press = 0; press < level / 3 + 1; ++press) {
            sim_advance_current_special(tw.world(), *captain);
            seen += std::to_string(static_cast<int>(captain->current_special()));
        }
        return seen;
    };
    EXPECT_EQ("11", walk(1));
    EXPECT_EQ("121", walk(4));
    EXPECT_EQ("1231", walk(7));
    EXPECT_EQ("12341", walk(10));
    EXPECT_STREQ("HOWL", og::sim::special_name(get_family_descriptor(FAMILY_BIG_ORC), 1, 1));
    EXPECT_STREQ("EAT CORPSE", og::sim::special_name(get_family_descriptor(FAMILY_BIG_ORC), 2, 1));
    EXPECT_STREQ("NONE", og::sim::alternate_name(get_family_descriptor(FAMILY_BIG_ORC), 1, 1))
        << "HOWL has no alternate";
    EXPECT_STREQ("NONE", og::sim::alternate_name(get_family_descriptor(FAMILY_BIG_ORC), 2, 1));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ------------------------------------------------------ art and glyphs

// The two new entity families load their art and show in the character
// clients: the blade is the knife's frames, the banner its own four. The
// captain is not for hire: an orc becomes one by promotion only.
TEST(KitCaptain, new_kit_entities_load_art_and_glyphs)
{
    const int blade_id = fx_family("core:hook_blade");
    const int banner_id = weapon_family("core:war_banner");
    EXPECT_EQ(15, blade_id);
    EXPECT_EQ(21, banner_id);
    const EffectFamilyDescriptor* efd = get_effect_family_descriptor(blade_id);
    ASSERT_NE(nullptr, efd);
    EXPECT_EQ(U'j', efd->glyph.codepoint);
    EXPECT_EQ('j', efd->glyph.ascii);
    EXPECT_EQ(og::GlyphColor::White, efd->glyph.color);
    EXPECT_TRUE(efd->glyph.bold);
    EXPECT_FALSE(efd->glyph.transparent);
    const WeaponFamilyDescriptor* wfd =
        get_weapon_family_descriptor(banner_id);
    ASSERT_NE(nullptr, wfd);
    EXPECT_EQ(U'⚑', wfd->glyph.codepoint);
    EXPECT_EQ('P', wfd->glyph.ascii);
    EXPECT_EQ(og::GlyphColor::Team, wfd->glyph.color);
    EXPECT_FALSE(wfd->glyph.transparent);

    TestGameWorld tw;
    open_field(tw);
    walker* blade = tw.world().add_ob(Order::FX, blade_id);
    ASSERT_NE(nullptr, blade);
    EXPECT_EQ(6, blade->sizex()) << "knife.png frames";
    walker* banner = tw.world().add_weap_ob(Order::Weapon, banner_id);
    ASSERT_NE(nullptr, banner);
    EXPECT_EQ(12, banner->sizex()) << "banner.png frames";
    EXPECT_EQ(22, banner->sizey());

    const FamilyDescriptor* captain = get_family_descriptor(FAMILY_BIG_ORC);
    ASSERT_NE(nullptr, captain);
    EXPECT_FALSE(captain->is_playable) << "promotion only, never hired";
    EXPECT_EQ(999, captain->playable_order);

    // The orc and the captain still level and scale as before.
    const FamilyDescriptor* orc = get_family_descriptor(FAMILY_ORC);
    ASSERT_NE(nullptr, orc);
    for (const FamilyDescriptor* fd : {orc, captain}) {
        guy g(fd->family_id);
        const short str = g.strength;
        og::test::level_up(*fd, &g, 1);
        EXPECT_GT(g.strength, str);
    }
    living w;
    w.set_order_family(Order::Living, FAMILY_ORC);
    const float hp = w.stats()->max_hitpoints();
    og::test::set_difficulty(*orc, &w, 2);
    EXPECT_GT(w.stats()->max_hitpoints(), hp);
}
