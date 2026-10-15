/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Bots and the New Specials setting. With the setting off a bot must roll,
// pick and pay exactly as it did before the setting existed; the one new C++
// bot rule (a bot that rolled the shift but cannot pay a PRICED alternate
// drops the shift) never fires then, because no classic alternate is
// priced. The per-kit bot gates (each kit's `ai =`) are pinned by the kit
// test files; these rows pin the engine around them, through test-only
// declarations installed over the golem's slot (kit_probe_family.h).

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "kit_probe_family.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>

namespace {

using og::test::ProbeFamily;

// The first world-RNG state from which the next draw of next(bound) is
// `want`: pins the bot's coin without a spy (the sim calls the LCG inline).
std::uint32_t state_where_next_is(std::uint32_t bound, std::uint32_t want)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        if (probe.next(bound) == want)
            return seed;
    }
}

living* add_bot(TestGameWorld& tw, int family, short level, float mp)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->setxy(160, 160);
    w->set_team_num(1);
    w->set_real_team_num(255);
    w->set_act_type(ACT_RANDOM);
    w->stats()->set_level(level);
    w->stats()->set_max_magicpoints(mp);
    w->stats()->set_magicpoints(mp);
    return dynamic_cast<living*>(w);
}

struct ActOutcome {
    std::uint32_t rng_state = 0;
    float x = 0.0f;
    float y = 0.0f;
    int current_special = 0;
    float mp = 0.0f;
};

// One act() of a fresh level-10 bot of `family` with 500 MP, from an RNG
// state whose first draw is next(5) == 0 (the 1-in-5 "do our special" roll
// in living::act's ACT_RANDOM arm).
ActOutcome act_once(int family, short new_specials)
{
    TestGameWorld tw;
    tw.world().new_specials = new_specials;
    living* bot = add_bot(tw, family, 10, 500.0f);
    EXPECT_NE(nullptr, bot);
    if (bot == nullptr)
        return {};
    tw.world().rng_.state_ = state_where_next_is(5, 0);
    bot->act();
    ActOutcome out;
    out.rng_state = tw.world().rng_.state_;
    out.x = bot->worldx();
    out.y = bot->worldy();
    out.current_special = static_cast<int>(bot->current_special());
    out.mp = bot->stats()->magicpoints();
    return out;
}

}  // namespace

// A family whose every special is new (the faerie's shape) is, with the
// setting off, a family with no specials: its bot takes the plain
// act_random() path, draw for draw, exactly as the same family with no
// specials declared at all. With the setting on the same roll goes to the
// special arm instead (a slot draw and the shift coin), so the streams part.
TEST(KitBots, bot_roll_is_identical_with_new_specials_off)
{
    og::test::ScopedHookFailureGuard guard;
    const ActOutcome classic = act_once(FAMILY_GOLEM, 0);  // no specials
    ActOutcome off;
    ActOutcome on;
    {
        ProbeFamily probe(og::test::kAllNewProbeDecl);
        ASSERT_EQ(1, probe.installed());
        off = act_once(FAMILY_GOLEM, 0);
        on = act_once(FAMILY_GOLEM, 1);
    }
    EXPECT_EQ(classic.rng_state, off.rng_state)
        << "the setting-off bot draws exactly what a bot with no specials "
           "draws";
    EXPECT_FLOAT_EQ(classic.x, off.x);
    EXPECT_FLOAT_EQ(classic.y, off.y);
    EXPECT_EQ(classic.current_special, off.current_special);
    EXPECT_FLOAT_EQ(classic.mp, off.mp);

    EXPECT_NE(classic.rng_state, on.rng_state)
        << "with the setting on the roll reaches the special arm";
    EXPECT_GE(on.current_special, 1);
    EXPECT_LE(on.current_special, 4);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The one C++ bot rule (D3), on the probe: slot 1 is DROP BOMB (35) with the
// priced new-kit alternate MINE (70); slot 3 is HEAL (2) with the unpriced
// classic alternate MYSTIC MACE; slot 4 is the new-kit PHASE (60).
TEST(KitBots, unaffordable_priced_alternate_drops_the_shift_only_when_priced)
{
    og::test::ScopedHookFailureGuard guard;
    ProbeFamily probe;
    ASSERT_EQ(1, probe.installed());
    const std::uint32_t coin_heads = state_where_next_is(2, 1);

    const auto check = [&](short new_specials, int slot, float mp) {
        TestGameWorld tw;
        tw.world().new_specials = new_specials;
        living* bot = add_bot(tw, FAMILY_GOLEM, 10, mp);
        EXPECT_NE(nullptr, bot);
        bot->set_current_special(static_cast<char>(slot));
        tw.world().rng_.state_ = coin_heads;
        EXPECT_TRUE(bot->check_special()) << "no ai gate: always allowed";
        struct Result {
            short shift;
            int slot;
            unsigned short price;
        };
        return Result{bot->shifter_down(),
                      static_cast<int>(bot->current_special()),
                      og::sim::cast_cost(*bot, bot->current_special(),
                                         bot->shifter_down() != 0)};
    };

    // Slot 1, 50 MP: MINE (70) is unaffordable. On: the shift is dropped and
    // the bomb (35) is what the cast gate sees. Off: MINE is not in play, the
    // shift stays, and the shifted price is the bomb's.
    const auto on = check(1, 1, 50.0f);
    EXPECT_EQ(0, on.shift) << "an unaffordable priced alternate drops the shift";
    EXPECT_EQ(1, on.slot);
    EXPECT_EQ(35, on.price);
    const auto off = check(0, 1, 50.0f);
    EXPECT_EQ(1, off.shift) << "with the setting off nothing is dropped";
    EXPECT_EQ(1, off.slot);
    EXPECT_EQ(35, off.price);
    // Affordable (80 MP): the shift stays and MINE is what it pays.
    const auto rich = check(1, 1, 80.0f);
    EXPECT_EQ(1, rich.shift);
    EXPECT_EQ(70, rich.price);

    // An unpriced alternate (MYSTIC MACE) never drops the shift, on or off,
    // even with 5 MP (the cleric bot case).
    for (const short flag : {short{0}, short{1}}) {
        const auto mace = check(flag, 3, 5.0f);
        EXPECT_EQ(1, mace.shift) << "flag " << flag;
        EXPECT_EQ(3, mace.slot) << "flag " << flag;
        EXPECT_EQ(2, mace.price) << "flag " << flag;
    }

    // The fall-back: slot 4 (PHASE, 60) is unaffordable at 50 MP, so the bot
    // falls back to slot 1 with the shift still down; MINE is unaffordable
    // there too, so the shift is dropped after the fall-back as well.
    const auto fallback = check(1, 4, 50.0f);
    EXPECT_EQ(1, fallback.slot) << "unaffordable slot falls back to slot 1";
    EXPECT_EQ(0, fallback.shift)
        << "the fall-back slot's priced alternate is checked too";
    EXPECT_EQ(35, fallback.price);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
