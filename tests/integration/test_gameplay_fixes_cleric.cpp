// The cleric with the Gameplay Fixes setting: TURN UNDEAD's reach and its
// refusal, and HEAL's cap at full health. Each case casts through the real
// walker::special() on both values of the setting; the classic value (0) is
// the game as it always was, bugs included.
//
// TURN UNDEAD. The classic reach is 4 px per level, so below level 4 it
// cannot reach a foe beside the cleric (bodies are 16 px). With the setting
// on the reach is never under the cleric's turn_undead_range_min (60 px,
// the raise's own reach), while the roll keeps its classic strength, 160
// per level, so a wider ring never changes who resists. A ring with foes
// but nothing undead in it is refused, "NO UNDEAD IN RANGE", and spends
// nothing.
//
// HEAL. The classic heal adds its whole amount, so a nearly-healthy ally
// can end far above its maximum. With the setting on the ally stops at full
// health, and the number shown and the experience earned are what really
// landed. The mana spent is unchanged.

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
#include <openglad/core/sound_ids.h>
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

// A cleric with a guy (so a heal earns experience and the INT gates apply),
// owned by the caller, at (100, 100), with 500 MP.
walker* make_cleric(unsigned char team, short level)
{
    ensure_level_loaded();
    guy g(FAMILY_CLERIC);
    g.teamnum = team;
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

walker* add_foe(char family, int x, int y, short level)
{
    walker* w = the_world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(2);
    w->stats()->set_level(level);
    w->set_dead(0);
    w->setxy(static_cast<Sint32>(x), static_cast<Sint32>(y));
    return w;
}

// Every draw is the top of its range: turn_undead's `next(bound) >
// next(level * 10)` is won by the cleric, a heal draws its biggest base.
class CeilingRandom : public IRandom
{
public:
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        return (max_exclusive == 0) ? 0 : (max_exclusive - 1);
    }
};

int heal_sounds()
{
    int n = 0;
    for (const og::sim::Event& e : current_game->sim_events->events())
        if (e.kind == og::sim::EventKind::PlaySound && e.a == SOUND_HEAL)
            ++n;
    return n;
}

struct Cast {
    bool ok = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
};

// TURN UNDEAD (slot 2's alternate) through walker::special().
Cast turn_undead(walker* cleric)
{
    cleric->myguy->intelligence = 100;  // turning needs 60
    cleric->set_current_special(2);
    cleric->set_shifter_down(1);
    current_game->sim_events->clear();
    Cast c;
    c.ok = cleric->special(&c.why, &c.reason);
    return c;
}

// Whether the cleric wins turn_undead's roll `next(bound) > next(level * 10)`
// from `seed`. C++ leaves the order of the two draws to the compiler, so
// `cleric_first` says which operand draws first.
bool roll_wins(std::uint32_t seed, std::uint32_t bound, std::uint32_t resist,
               bool cleric_first)
{
    og::sim::SimRandom probe(seed);
    const std::uint32_t first = probe.next(cleric_first ? bound : resist);
    const std::uint32_t second = probe.next(cleric_first ? resist : bound);
    return cleric_first ? first > second : second > first;
}

// The same answer whichever operand draws first.
bool roll_wins_either_order(std::uint32_t seed, std::uint32_t bound,
                            std::uint32_t resist, bool want)
{
    return roll_wins(seed, bound, resist, true) == want &&
           roll_wins(seed, bound, resist, false) == want;
}

class GameplayFixesCleric : public ::testing::Test {
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

// A level-5 cleric (classic reach 20 px) and one skeleton 50 px away. The
// classic cast finds nobody in reach and refuses; with the setting on the
// reach is 60 px and the skeleton is turned.
// RED on the classic reach: with the setting on the cast is refused too.
TEST_F(GameplayFixesCleric, turn_undead_reaches_sixty_px_when_on)
{
    for (const short fixes : {short{0}, short{1}}) {
        the_world().delete_objects();
        the_world().gameplay_fixes = fixes;
        walker* cleric = make_cleric(0, 5);
        ASSERT_NE(nullptr, cleric);
        walker* skeleton = add_foe(FAMILY_SKELETON, 150, 100, 1);
        ASSERT_NE(nullptr, skeleton);
        ASSERT_EQ(50, static_cast<int>(cleric->distance_to_ob(skeleton)));

        const float mp_before = cleric->stats()->magicpoints();
        Cast c;
        {
            CeilingRandom ceiling;
            ScopedSimRandom scoped(&ceiling);
            c = turn_undead(cleric);
        }
        if (fixes == 0) {
            EXPECT_FALSE(c.ok) << "the classic reach of a level-5 cleric is 20";
            EXPECT_EQ(std::string("NO FOE IN RANGE"), c.reason);
            EXPECT_EQ(0, static_cast<int>(skeleton->dead()));
            EXPECT_FLOAT_EQ(mp_before, cleric->stats()->magicpoints());
        } else {
            EXPECT_TRUE(c.ok) << "with the setting on the reach is 60: "
                              << c.reason;
            EXPECT_EQ(1, static_cast<int>(skeleton->dead()))
                << "and the skeleton is turned";
            EXPECT_EQ(1, heal_sounds());
            EXPECT_FLOAT_EQ(mp_before - 20.0f, cleric->stats()->magicpoints());
        }
        delete cleric;
    }
}

// A level-10 cleric (classic reach 40 px) and one living orc 50 px away, no
// undead. The classic cast finds nobody in reach; with the setting on the
// orc is inside the 60 px reach, but there is nothing to turn, and the cast
// says so. Neither spends anything.
// RED on the classic reach: with the setting on the reason is
// "NO FOE IN RANGE".
TEST_F(GameplayFixesCleric, turn_undead_refuses_without_undead_when_on)
{
    for (const short fixes : {short{0}, short{1}}) {
        the_world().delete_objects();
        the_world().gameplay_fixes = fixes;
        walker* cleric = make_cleric(0, 10);
        ASSERT_NE(nullptr, cleric);
        walker* orc = add_foe(FAMILY_ORC, 150, 100, 1);
        ASSERT_NE(nullptr, orc);

        const float mp_before = cleric->stats()->magicpoints();
        Cast c;
        {
            CeilingRandom ceiling;
            ScopedSimRandom scoped(&ceiling);
            c = turn_undead(cleric);
        }
        EXPECT_FALSE(c.ok) << "setting " << fixes;
        EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, c.why);
        EXPECT_EQ(std::string(fixes != 0 ? "NO UNDEAD IN RANGE"
                                         : "NO FOE IN RANGE"),
                  c.reason)
            << "setting " << fixes;
        EXPECT_EQ(0, heal_sounds()) << "a refused cast is silent";
        EXPECT_FLOAT_EQ(mp_before, cleric->stats()->magicpoints())
            << "and spends nothing";
        EXPECT_EQ(0, static_cast<int>(orc->dead()));
        delete cleric;
    }
}

// The roll keeps its classic strength through the cast. A level-5 cleric
// and a level-10 skeleton 10 px away (inside both reaches). From a world
// state where the skeleton resists the classic bound (5 * 160 = 800) but
// would lose to the wide ring's own (60 * 40 = 2400), it resists on both
// values, and the cast draws the same two numbers on both.
// RED under the mutation "do_turn_undead passes 3 * self.level as the
// power": the bound with the setting on becomes 2400 and the skeleton is
// turned.
TEST_F(GameplayFixesCleric, turn_undead_roll_keeps_the_classic_strength)
{
    std::uint32_t start = 1;
    while (!roll_wins_either_order(start, 800, 100, false) ||
           !roll_wins_either_order(start, 2400, 100, true))
        ++start;
    og::sim::SimRandom two_draws(start);
    (void)two_draws.next(800);
    (void)two_draws.next(100);

    for (const short fixes : {short{0}, short{1}}) {
        the_world().delete_objects();
        the_world().gameplay_fixes = fixes;
        walker* cleric = make_cleric(0, 5);
        ASSERT_NE(nullptr, cleric);
        walker* skeleton = add_foe(FAMILY_SKELETON, 110, 100, 10);
        ASSERT_NE(nullptr, skeleton);

        the_world().rng_.state_ = start;
        const Cast c = turn_undead(cleric);
        EXPECT_TRUE(c.ok) << "setting " << fixes << ": " << c.reason;
        EXPECT_EQ(0, static_cast<int>(skeleton->dead()))
            << "the classic strength: the skeleton resists; setting " << fixes;
        EXPECT_EQ(two_draws.state_, the_world().rng_.state_)
            << "one roll, two draws; setting " << fixes;
        delete cleric;
    }
}

// The ON twin of WalkerSpecials.cleric_heal_drives_simulation (which pins
// the classic numbers, 545 and 482): the same two allies at 20 of 100 HP,
// the same 2000 MP pool and floor stream. With the setting on both stop at
// 100, each floating number shows the 80 that landed, and the cleric pays
// exactly what the classic heal costs (1530 MP left).
// RED on the classic heal: 545 and 482.
TEST_F(GameplayFixesCleric, heal_stops_at_full_health_when_on)
{
    GameWorld& world = the_world();
    world.delete_objects();
    world.gameplay_fixes = 1;
    walker* w = make_cleric(1, 5);
    ASSERT_NE(nullptr, w);
    w->set_current_special(1);
    w->set_shifter_down(0);
    w->stats()->set_magicpoints(2000);

    walker* ally1 = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* ally2 = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, ally1);
    ASSERT_NE(nullptr, ally2);
    ally1->set_team_num(1);
    ally2->set_team_num(1);
    ally1->setxy(108, 100);
    ally2->setxy(112, 100);
    for (walker* ally : {ally1, ally2}) {
        ally->stats()->set_max_hitpoints(100);
        ally->stats()->set_hitpoints(20);
        ally->damage_numbers.clear();
    }
    {
        FixedRandom floor_rng(0u);
        ScopedSimRandom scoped(&floor_rng);
        ASSERT_TRUE(w->special()) << "the heal fires with wounded allies";
    }
    EXPECT_FLOAT_EQ(100.0f, ally1->stats()->hitpoints())
        << "never above full health (the classic heal leaves 545)";
    EXPECT_FLOAT_EQ(100.0f, ally2->stats()->hitpoints())
        << "never above full health (the classic heal leaves 482)";
    for (walker* ally : {ally1, ally2}) {
        ASSERT_FALSE(ally->damage_numbers.empty());
        EXPECT_FLOAT_EQ(80.0f, ally->damage_numbers.back().value)
            << "the number shown is what landed";
    }
    EXPECT_FLOAT_EQ(1530.0f, w->stats()->magicpoints())
        << "the mana is spent as in the classic heal";
    delete w;
}

// One ally at 90 of 100 HP, a level-5 cleric with 400 MP, every draw at its
// ceiling: the heal amount is (100 + 99) + 25 = 224 at a cost of 99, and
// the experience is next(20 * value) / level, so it shows which value the
// reward was computed from. Classic: the ally ends at 314, the number reads
// 224, the reward is (20 * 224 - 1) / 5 = 895. With the setting on: 100,
// 10, and (20 * 10 - 1) / 5 = 39. The mana is the same on both values.
// RED on the classic heal: 314, 224 and 895 with the setting on.
TEST_F(GameplayFixesCleric, heal_reward_and_number_are_what_landed)
{
    for (const short fixes : {short{0}, short{1}}) {
        GameWorld& world = the_world();
        world.delete_objects();
        world.gameplay_fixes = fixes;
        walker* w = make_cleric(1, 5);
        ASSERT_NE(nullptr, w);
        ASSERT_NE(nullptr, w->myguy);
        w->set_current_special(1);
        w->set_shifter_down(0);
        w->stats()->set_magicpoints(400);
        walker* ally = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, ally);
        ally->set_team_num(1);
        ally->setxy(108, 100);
        ally->stats()->set_max_hitpoints(100);
        ally->stats()->set_hitpoints(90);
        ally->damage_numbers.clear();

        const auto exp_before = w->myguy->exp;
        {
            CeilingRandom ceiling;
            ScopedSimRandom scoped(&ceiling);
            GameContext gc;
            gc.rng = &ceiling;  // the experience roll's stream
            push_test_context(&gc);
            const bool ok = w->special();
            pop_test_context();
            ASSERT_TRUE(ok) << "setting " << fixes;
        }
        EXPECT_FLOAT_EQ(fixes != 0 ? 100.0f : 314.0f,
                        ally->stats()->hitpoints())
            << "setting " << fixes;
        ASSERT_FALSE(ally->damage_numbers.empty());
        EXPECT_FLOAT_EQ(fixes != 0 ? 10.0f : 224.0f,
                        ally->damage_numbers.back().value)
            << "the floating number; setting " << fixes;
        EXPECT_EQ(fixes != 0 ? 39 : 895,
                  static_cast<int>(w->myguy->exp - exp_before))
            << "the experience; setting " << fixes;
        EXPECT_FLOAT_EQ(400.0f - 99.0f - 2.0f, w->stats()->magicpoints())
            << "the cost and the slot's own 2 MP; setting " << fixes;
        delete w;
    }
}

// A heal that stays under the maximum is the classic heal on both values:
// the two allies of the ON twin above, with 600 max HP, end at 545 and 482.
// RED under the mutation "the cap is applied without the over-maximum
// test": both end at 600.
TEST_F(GameplayFixesCleric, heal_below_full_health_is_unchanged)
{
    for (const short fixes : {short{0}, short{1}}) {
        GameWorld& world = the_world();
        world.delete_objects();
        world.gameplay_fixes = fixes;
        walker* w = make_cleric(1, 5);
        ASSERT_NE(nullptr, w);
        w->set_current_special(1);
        w->set_shifter_down(0);
        w->stats()->set_magicpoints(2000);
        walker* ally1 = world.add_ob(Order::Living, FAMILY_SOLDIER);
        walker* ally2 = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, ally1);
        ASSERT_NE(nullptr, ally2);
        ally1->set_team_num(1);
        ally2->set_team_num(1);
        ally1->setxy(108, 100);
        ally2->setxy(112, 100);
        for (walker* ally : {ally1, ally2}) {
            ally->stats()->set_max_hitpoints(600);
            ally->stats()->set_hitpoints(20);
        }
        {
            FixedRandom floor_rng(0u);
            ScopedSimRandom scoped(&floor_rng);
            ASSERT_TRUE(w->special()) << "setting " << fixes;
        }
        EXPECT_FLOAT_EQ(545.0f, ally1->stats()->hitpoints())
            << "setting " << fixes;
        EXPECT_FLOAT_EQ(482.0f, ally2->stats()->hitpoints())
            << "setting " << fixes;
        EXPECT_FLOAT_EQ(1530.0f, w->stats()->magicpoints())
            << "setting " << fixes;
        delete w;
    }
}
