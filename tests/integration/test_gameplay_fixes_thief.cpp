// The thief with the Gameplay Fixes setting: CHARM OPPONENT takes the
// nearest foe it can take, never a hero a player drives (#355), and TAUNT
// with nobody in reach is refused for free. Each case casts through the
// real walker::special() on both values of the setting; the classic value
// (0) is the game as it always was, bugs included.
//
// CHARM. The classic cast walks the foes in list order and charms the first
// eligible one, wherever it stands inside the reach. With the setting on it
// takes the nearest, and a hero a seat drives resists as it does POSSESS.
// The one roll (og.rand(20), a 1 in 20 resist) is drawn on both values.
//
// TAUNT. The classic taunt with nobody in reach still costs its 100 MP and
// shouts "Nyah Nyah!" at nobody; with the setting on it is refused, "NO FOE
// IN RANGE", and spends nothing.
//
// A level-7 thief: charm reach 16 + 4 * 7 = 44 px, taunt reach
// 80 + 4 * 7 = 108 px.

#include <openglad/interface/game_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include "test_sim_random_scope.h"
#include <cstdint>
#include <string>

namespace {

GameWorld& the_world()
{
    return og::runtime::current_session->myscreen_->world();
}

void ensure_level_loaded()
{
    if (og::runtime::current_session->myscreen_ == nullptr)
        return;
    GameWorld& world = the_world();
    if (world.grid.valid() && world.pixmaxx > 0 && world.pixmaxy > 0)
        return;
    world.id = 1;
    (void)og::runtime::current_session->myscreen_->load_level();
}

// A level-7 team-2 thief, owned by the caller, at (100, 100), with 500 MP.
walker* make_thief(short level = 7)
{
    ensure_level_loaded();
    guy g(FAMILY_THIEF);
    g.teamnum = 2;
    g.upgrade_to_level(level, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w) {
        w->setxy(100, 100);
        w->stats()->set_magicpoints(500);
        w->stats()->set_max_magicpoints(500);
        w->set_busy(0);
    }
    return w.release();
}

walker* add_living(char family, unsigned char team, int x, int y, short level)
{
    walker* w = the_world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->stats()->set_level(level);
    w->set_dead(0);
    w->setxy(static_cast<Sint32>(x), static_cast<Sint32>(y));
    return w;
}

struct Cast {
    bool ok = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
};

// Slot 3 (TAUNT ENEMY) with the shift (CHARM OPPONENT) or without.
Cast cast_slot3(walker* thief, short shift)
{
    thief->set_current_special(3);
    thief->set_shifter_down(shift);
    current_game->sim_events->clear();
    Cast c;
    c.ok = thief->special(&c.why, &c.reason);
    return c;
}

int notifications()
{
    int n = 0;
    for (const og::sim::Event& e : current_game->sim_events->events())
        if (e.kind == og::sim::EventKind::Notification)
            ++n;
    return n;
}

class GameplayFixesThief : public ::testing::Test {
protected:
    short fixes_before_ = 0;
    void SetUp() override { fixes_before_ = the_world().gameplay_fixes; }
    void TearDown() override
    {
        the_world().gameplay_fixes = fixes_before_;
        the_world().delete_objects();
    }
};

}  // namespace

// Two orcs inside charm reach, the far one (40 px) earlier in the list than
// the near one (20 px), and a roll that cannot resist (every draw at 1, so
// og.rand(20) is 1). The classic cast charms the far orc; with the setting
// on it charms the near one.
// RED on the classic cast: with the setting on the far orc is charmed.
TEST_F(GameplayFixesThief, charm_takes_the_nearest_foe_when_on)
{
    for (const short fixes : {short{0}, short{1}}) {
        the_world().delete_objects();
        the_world().gameplay_fixes = fixes;
        walker* thief = make_thief();
        ASSERT_NE(nullptr, thief);
        walker* far = add_living(FAMILY_ORC, 0, 140, 100, 1);
        walker* near = add_living(FAMILY_ORC, 0, 120, 100, 1);
        ASSERT_NE(nullptr, far);
        ASSERT_NE(nullptr, near);
        ASSERT_EQ(40, static_cast<int>(thief->distance_to_ob(far)));
        ASSERT_EQ(20, static_cast<int>(thief->distance_to_ob(near)));

        Cast c;
        {
            FixedRandom one(1u);
            ScopedSimRandom scoped(&one);
            c = cast_slot3(thief, 1);
        }
        EXPECT_TRUE(c.ok) << "setting " << fixes << ": " << c.reason;
        walker* taken = fixes != 0 ? near : far;
        walker* left = fixes != 0 ? far : near;
        EXPECT_EQ(2, static_cast<int>(taken->team_num()))
            << (fixes != 0 ? "the near orc" : "the first orc in the list")
            << " fights for the thief; setting " << fixes;
        EXPECT_EQ(0, static_cast<int>(taken->real_team_num()));
        EXPECT_GT(static_cast<int>(taken->charm_left()), 0);
        EXPECT_EQ(0, static_cast<int>(left->team_num()))
            << "the other orc is untouched; setting " << fixes;
        EXPECT_EQ(255, static_cast<int>(left->real_team_num()));
        delete thief;
    }
}

// The charm draws its one roll on both values: from the same world state,
// with two orcs it could take, the cast leaves the stream one og.rand(20)
// further on, and exactly one orc changes sides.
// RED under the mutation "the charm loop loses its one-target latch"
// (the `if handled ~= 0 then break end` lines removed): two draws.
TEST_F(GameplayFixesThief, charm_draws_once_on_both_values)
{
    std::uint32_t start = 1;
    while (og::sim::SimRandom(start).next(20) == 0)
        ++start;
    og::sim::SimRandom one_roll(start);
    (void)one_roll.next(20);

    for (const short fixes : {short{0}, short{1}}) {
        the_world().delete_objects();
        the_world().gameplay_fixes = fixes;
        walker* thief = make_thief();
        ASSERT_NE(nullptr, thief);
        walker* a = add_living(FAMILY_ORC, 0, 130, 100, 1);
        walker* b = add_living(FAMILY_ORC, 0, 100, 125, 1);
        ASSERT_NE(nullptr, a);
        ASSERT_NE(nullptr, b);

        the_world().rng_.state_ = start;
        const Cast c = cast_slot3(thief, 1);
        EXPECT_TRUE(c.ok) << "setting " << fixes << ": " << c.reason;
        EXPECT_EQ(one_roll.state_, the_world().rng_.state_)
            << "one roll; setting " << fixes;
        const int taken = (a->team_num() == 2 ? 1 : 0) +
                          (b->team_num() == 2 ? 1 : 0);
        EXPECT_EQ(1, taken) << "one orc charmed; setting " << fixes;
        delete thief;
    }
}

// #355: an enemy thief and a hero a player drives (a user tag and
// ACT_CONTROL), 30 px away, level 1 against the thief's 7, the roll pinned
// so a charm cannot resist. The classic cast takes the hero onto the
// thief's team. With the setting on the hero resists as it does POSSESS:
// the cast is refused, "NO VALID TARGET", it draws nothing, spends nothing
// and gives no free attack.
// RED on the classic cast: with the setting on the hero's team becomes 2.
TEST_F(GameplayFixesThief, charm_skips_a_seated_hero_when_on)
{
    // A world state whose og.rand(20) is not the 1-in-20 resist.
    std::uint32_t start = 1;
    while (og::sim::SimRandom(start).next(20) == 0)
        ++start;
    for (const short fixes : {short{0}, short{1}}) {
        the_world().delete_objects();
        the_world().gameplay_fixes = fixes;
        walker* thief = make_thief();
        ASSERT_NE(nullptr, thief);
        walker* hero = add_living(FAMILY_SOLDIER, 0, 130, 100, 1);
        ASSERT_NE(nullptr, hero);
        hero->set_user(0);
        hero->set_act_type(ACT_CONTROL);

        const float mp_before = thief->stats()->magicpoints();
        the_world().rng_.state_ = start;
        const Cast c = cast_slot3(thief, 1);
        if (fixes == 0) {
            EXPECT_TRUE(c.ok) << c.reason;
            EXPECT_EQ(2, static_cast<int>(hero->team_num()))
                << "the classic charm takes the hero";
        } else {
            EXPECT_EQ(start, the_world().rng_.state_) << "no roll drawn";
            EXPECT_FALSE(c.ok);
            EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, c.why);
            EXPECT_EQ(std::string("NO VALID TARGET"), c.reason);
            EXPECT_EQ(0, static_cast<int>(hero->team_num()));
            EXPECT_EQ(255, static_cast<int>(hero->real_team_num()));
            EXPECT_EQ(0, static_cast<int>(hero->charm_left()));
            EXPECT_EQ(nullptr, hero->foe()) << "no free attack on a skip";
            EXPECT_FLOAT_EQ(mp_before, thief->stats()->magicpoints());
            EXPECT_FLOAT_EQ(0.0f, thief->busy());
        }
        delete thief;
    }
}

// #355 with somebody else to take: the seated hero (30 px) and an orc
// (35 px). With the setting on the cast passes over the nearer hero and
// charms the orc.
// RED on the classic cast: the hero, first in the list, is charmed.
TEST_F(GameplayFixesThief, charm_takes_the_npc_beside_a_seated_hero_when_on)
{
    the_world().delete_objects();
    the_world().gameplay_fixes = 1;
    walker* thief = make_thief();
    ASSERT_NE(nullptr, thief);
    walker* hero = add_living(FAMILY_SOLDIER, 0, 130, 100, 1);
    walker* orc = add_living(FAMILY_ORC, 0, 100, 135, 1);
    ASSERT_NE(nullptr, hero);
    ASSERT_NE(nullptr, orc);
    hero->set_user(0);
    hero->set_act_type(ACT_CONTROL);

    Cast c;
    {
        FixedRandom one(1u);
        ScopedSimRandom scoped(&one);
        c = cast_slot3(thief, 1);
    }
    EXPECT_TRUE(c.ok) << c.reason;
    EXPECT_EQ(0, static_cast<int>(hero->team_num())) << "the hero resists";
    EXPECT_EQ(255, static_cast<int>(hero->real_team_num()));
    EXPECT_EQ(2, static_cast<int>(orc->team_num())) << "the orc is taken";
    delete thief;
}

// TAUNT with nobody inside its 108 px: the classic taunt shouts, adds 2
// busy and costs its 100 MP; with the setting on it is refused, "NO FOE IN
// RANGE", silent, with nothing spent.
// RED on the classic taunt: with the setting on the cast succeeds.
TEST_F(GameplayFixesThief, taunt_refuses_with_no_foe_when_on)
{
    for (const short fixes : {short{0}, short{1}}) {
        the_world().delete_objects();
        the_world().gameplay_fixes = fixes;
        walker* thief = make_thief();
        ASSERT_NE(nullptr, thief);
        ASSERT_NE(nullptr, add_living(FAMILY_ORC, 0, 300, 100, 1));  // 200 px

        const float mp_before = thief->stats()->magicpoints();
        const Cast c = cast_slot3(thief, 0);
        if (fixes == 0) {
            EXPECT_TRUE(c.ok) << "the classic taunt at nobody";
            EXPECT_EQ(1, notifications()) << "Nyah Nyah!";
            EXPECT_FLOAT_EQ(2.0f, thief->busy());
            EXPECT_FLOAT_EQ(mp_before - 100.0f, thief->stats()->magicpoints());
        } else {
            EXPECT_FALSE(c.ok);
            EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, c.why);
            EXPECT_EQ(std::string("NO FOE IN RANGE"), c.reason);
            EXPECT_EQ(0, notifications()) << "a refused taunt is silent";
            EXPECT_FLOAT_EQ(0.0f, thief->busy());
            EXPECT_FLOAT_EQ(mp_before, thief->stats()->magicpoints());
        }
        delete thief;
    }
}
