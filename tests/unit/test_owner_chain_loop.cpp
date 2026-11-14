/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// #503: every owner-chain climb ends.
//
// A walker's side, its blow's score and its kill credit are read by climbing
// its owner chain to the head (a knife to its thrower, a summon to its
// caster). Only a script's set_owner, or a forged snapshot, can make a
// walker owned by a walker it owns, over two links or more (A owns B owns
// A), and before #503 the first climb of such a chain never ended. Every
// climb now goes through walker::climb: past 16 links it asks
// walker::cut_owner_loop whether the chain loops, and a loop is cut at the
// link that closes it, with one line in the log. This game only (the old
// game has no scripts or snapshots), so both values of the setting.
//
// The scene: red (team 0) soldiers, a red knife and a green (team 1)
// soldier; red is the player's team (my_team 0).

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr unsigned char kRed = 0;
constexpr unsigned char kGreen = 1;

constexpr const char* kLoopLine = "closed an owner loop";

struct LoopScene
{
    std::unique_ptr<TestGameWorld> tw;

    explicit LoopScene(short fixes)
    {
        og::test::mount_core_pack();
        tw = std::make_unique<TestGameWorld>();
        GameWorld& w = tw->world();
        w.resize_grid(32, 32);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = fixes;
        w.new_specials = 0;
    }

    GameWorld& world() { return tw->world(); }

    walker* soldier(unsigned char team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, FAMILY_SOLDIER);
        if (w == nullptr)
            return nullptr;
        w->clear_myguy();
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_act_type(ACT_SIT);
        return w;
    }

    walker* knife(walker* thrower)
    {
        walker* k = world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
        if (k == nullptr)
            return nullptr;
        k->set_team_num(kRed);
        k->set_owner(thrower);
        k->stats()->set_level(4);
        k->stats()->set_hitpoints(1000.0f);
        return k;
    }
};

// One blow of 40 from a to b, who stands at 1000 hp: did it take any?
bool blow_lands(walker* a, walker* b)
{
    b->stats()->set_hitpoints(1000.0f);
    a->set_damage(40.0f);
    const bool landed = a->attack(b);
    return landed && b->stats()->hitpoints() < 1000.0f;
}

std::size_t count_of(const std::string& text, const std::string& what)
{
    std::size_t n = 0;
    for (std::size_t at = text.find(what); at != std::string::npos;
         at = text.find(what, at + what.size()))
        ++n;
    return n;
}

// The five climbs a test can start: the head a blow scores for, the credit,
// the two side questions and a whole blow (which asks all of them).
enum class Climb { ChainHead, CreditHead, IsFriendly, IsFriendlyToTeam, Attack };

const char* climb_name(Climb c)
{
    switch (c) {
        case Climb::ChainHead: return "chain_head";
        case Climb::CreditHead: return "credit_head";
        case Climb::IsFriendly: return "is_friendly";
        case Climb::IsFriendlyToTeam: return "is_friendly_to_team";
        case Climb::Attack: return "attack";
    }
    return "?";
}

// Starts climb c from `from`, the first climb of the scene: the one that
// meets the loop, cuts it and starts again. Its own answer must already be
// the answer of the cut chain, whose head (and topmost living) is `head`,
// a red walker: no friend of green, red's, and its blow on green lands.
void expect_first_climb(Climb c, walker* from, walker* green, walker* head)
{
    switch (c) {
        case Climb::ChainHead:
            EXPECT_EQ(head, from->chain_head()) << "the first climb's own answer";
            break;
        case Climb::CreditHead:
            EXPECT_EQ(head, from->credit_head()) << "the first climb's own answer";
            break;
        case Climb::IsFriendly:
            EXPECT_EQ(0, from->is_friendly(green)) << "the first climb's own answer";
            break;
        case Climb::IsFriendlyToTeam:
            EXPECT_EQ(1, from->is_friendly_to_team(kRed)) << "the first climb's own answer";
            break;
        case Climb::Attack:
            EXPECT_TRUE(blow_lands(from, green)) << "the first climb's own answer";
            break;
    }
}

constexpr Climb kClimbs[] = {Climb::ChainHead, Climb::CreditHead, Climb::IsFriendly,
                             Climb::IsFriendlyToTeam, Climb::Attack};

}  // namespace

// A knife k thrown by a, where a owns b and b owns a (k -> a -> b -> a). On
// both values, whichever climb meets the loop first ends: b's link back to
// a closes the loop and is cut, so the head (and the credit) is b, red. The
// knife is no friend of green, is red's, and its blow on green lands, and so
// answers the first climb itself, the one that cuts the loop. One log line
// names b and a.
// Break: in cut_owner_loop, `w->set_owner(nullptr)` -> `up->set_owner(nullptr)`
// (a's link is cut instead: the head becomes a). Break: in walker::climb, the
// restart `return climb(step, top_living);` -> `return climb(step);` (the
// first credit_head answers nobody).
TEST(OwnerChainLoop, every_climb_ends_on_a_two_link_loop_and_cuts_the_link_that_closes_it)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        for (const Climb first : kClimbs) {
            SCOPED_TRACE(testing::Message() << "fixes " << fixes << ", first climb "
                                            << climb_name(first));
            LoopScene s(fixes);
            walker* a = s.soldier(kRed, 100, 100);
            walker* b = s.soldier(kRed, 140, 100);
            walker* g = s.soldier(kGreen, 240, 240);
            ASSERT_TRUE(a && b && g);
            walker* k = s.knife(a);
            ASSERT_NE(nullptr, k);
            a->set_owner(b);
            b->set_owner(a);

            testing::internal::CaptureStderr();
            expect_first_climb(first, k, g, b);
            EXPECT_EQ(nullptr, b->owner()) << "b's link back to a closes the loop";
            EXPECT_EQ(b, a->owner()) << "a's link is kept";
            EXPECT_EQ(a, k->owner()) << "the knife's link is kept";
            EXPECT_EQ(b, k->chain_head());
            EXPECT_EQ(b, k->credit_head()) << "the topmost living";
            EXPECT_EQ(0, k->is_friendly(g));
            EXPECT_EQ(1, k->is_friendly_to_team(kRed));
            EXPECT_TRUE(blow_lands(k, g));
            const std::string said = testing::internal::GetCapturedStderr();
            EXPECT_EQ(1u, count_of(said, kLoopLine)) << said;
            EXPECT_NE(std::string::npos,
                      said.find(std::format("walker {} to walker {}", b->entity_id(),
                                            a->entity_id())))
                << said;
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// a owns b, b owns c, c owns a. From a, every climb ends: c's link back to a
// closes the loop and is cut, so the head is c, red; a is no friend of
// green, is red's, and its blow on green lands, the first climb's own answer
// included; one log line names c and a. From b the loop closes at a's link
// back to b: a's link is cut and the head is a.
// Break: in cut_owner_loop, `w->set_owner(nullptr)` -> `up->set_owner(nullptr)`.
// Break: in walker::climb, `return climb(step, top_living);` ->
// `return climb(step);`.
TEST(OwnerChainLoop, a_three_link_loop_is_cut_at_the_link_that_closes_it)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        for (const Climb first : kClimbs) {
            SCOPED_TRACE(testing::Message() << "fixes " << fixes << ", first climb "
                                            << climb_name(first));
            LoopScene s(fixes);
            walker* a = s.soldier(kRed, 100, 100);
            walker* b = s.soldier(kRed, 140, 100);
            walker* c = s.soldier(kRed, 180, 100);
            walker* g = s.soldier(kGreen, 240, 240);
            ASSERT_TRUE(a && b && c && g);
            a->set_owner(b);
            b->set_owner(c);
            c->set_owner(a);

            testing::internal::CaptureStderr();
            expect_first_climb(first, a, g, c);
            EXPECT_EQ(nullptr, c->owner()) << "c's link back to a closes the loop";
            EXPECT_EQ(b, a->owner());
            EXPECT_EQ(c, b->owner());
            EXPECT_EQ(c, a->chain_head());
            EXPECT_EQ(c, a->credit_head());
            EXPECT_EQ(0, a->is_friendly(g));
            EXPECT_EQ(1, a->is_friendly_to_team(kRed));
            EXPECT_TRUE(blow_lands(a, g));
            const std::string said = testing::internal::GetCapturedStderr();
            EXPECT_EQ(1u, count_of(said, kLoopLine)) << said;
            EXPECT_NE(std::string::npos,
                      said.find(std::format("walker {} to walker {}", c->entity_id(),
                                            a->entity_id())))
                << said;
        }

        SCOPED_TRACE(testing::Message() << "fixes " << fixes << ", from b");
        LoopScene s(fixes);
        walker* a = s.soldier(kRed, 100, 100);
        walker* b = s.soldier(kRed, 140, 100);
        walker* c = s.soldier(kRed, 180, 100);
        ASSERT_TRUE(a && b && c);
        a->set_owner(b);
        b->set_owner(c);
        c->set_owner(a);
        testing::internal::CaptureStderr();
        EXPECT_EQ(a, b->chain_head());
        const std::string said = testing::internal::GetCapturedStderr();
        EXPECT_EQ(nullptr, a->owner()) << "a's link back to b closes the loop";
        EXPECT_EQ(c, b->owner());
        EXPECT_EQ(a, c->owner());
        EXPECT_EQ(1u, count_of(said, kLoopLine)) << said;
        EXPECT_NE(std::string::npos,
                  said.find(std::format("walker {} to walker {}", a->entity_id(),
                                        b->entity_id())))
            << said;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A chain of 40 soldiers, w0 owned by w1 ... w38 by w39 (green, no owner),
// longer than any in play and with no loop: every climb reaches w39, no
// link is cut and nothing is logged.
// Break: climb's loop guard becomes a depth cap,
// `if (++links == kOwnerLoopCheck) { up->set_owner(nullptr); }` (the head
// becomes w16).
TEST(OwnerChainLoop, a_long_chain_with_no_loop_resolves_to_its_head)
{
    og::test::ScopedHookFailureGuard guard;
    constexpr std::size_t kLength = 40;
    for (const short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes);
        LoopScene s(fixes);
        std::vector<walker*> chain;
        for (std::size_t i = 0; i < kLength; ++i) {
            walker* w = s.soldier(i + 1 == kLength ? kGreen : kRed,
                                  static_cast<int>(16 + 12 * (i % 20)),
                                  static_cast<int>(16 + 40 * (i / 20)));
            ASSERT_NE(nullptr, w);
            chain.push_back(w);
        }
        for (std::size_t i = 0; i + 1 < kLength; ++i)
            chain[i]->set_owner(chain[i + 1]);

        testing::internal::CaptureStderr();
        EXPECT_EQ(chain.back(), chain.front()->chain_head());
        EXPECT_EQ(chain.back(), chain.front()->credit_head());
        EXPECT_EQ(1, chain.front()->is_friendly_to_team(kGreen));
        EXPECT_EQ(0, chain.front()->is_friendly_to_team(kRed));
        const std::string said = testing::internal::GetCapturedStderr();
        for (std::size_t i = 0; i + 1 < kLength; ++i)
            EXPECT_EQ(chain[i + 1], chain[i]->owner()) << "link " << i;
        EXPECT_EQ(nullptr, chain.back()->owner());
        EXPECT_EQ(0u, count_of(said, kLoopLine)) << said;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
