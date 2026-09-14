#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { push_test_context(ctx); }
    ~GlobalContextGuard() { pop_test_context(); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};
} // namespace

TEST(StatsHitResponseMore, statistics_hit_response_archer_runs_away_and_queues_walk)
{
    og::runtime::current_session->myscreen_->world().delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    walker* archer = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ARCHER);
    walker* foe = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_TRUE(archer != nullptr && foe != nullptr) << "actors created";
    if (!(archer && foe))
        return;

    archer->setxy(100, 100);
    foe->setxy(120, 100); // within < 64 distance
    archer->stats()->clear_command();
    archer->set_foe(nullptr);

    archer->stats()->hit_response(foe);

    // packs/core/families/living-02-archer.lua:65-79: a new attacker is taken
    // as the foe with both distance caches reset to 15000, and a foe closer
    // than melee_backpedal_range (64) forces a WALK of 8 in the sign of
    // (self - foe), i.e. due west from (100,100) with the orc at (120,100).
    EXPECT_EQ(foe, archer->foe()) << "the hit archer takes its attacker as foe";
    EXPECT_EQ(15000u, archer->stats()->last_distance())
        << "the Lua response resets the distance caches to 15000";
    EXPECT_EQ(15000, archer->stats()->current_distance())
        << "the Lua response resets the distance caches to 15000";

    ASSERT_EQ(1u, archer->stats()->commands.size())
        << "archer hit_response should queue exactly one walk-away command";
    const command& away = archer->stats()->commands.front();
    EXPECT_EQ(COMMAND_WALK, away.commandtype) << "the back-pedal is a walk";
    EXPECT_EQ(8, away.commandcount) << "the archer back-pedals for 8 rounds";
    EXPECT_EQ(-1, away.com1) << "it walks AWAY from the foe: sign(100-120) == -1";
    EXPECT_EQ(0, away.com2) << "no north/south component: sign(100-100) == 0";
    EXPECT_TRUE(away.forced) << "s_force_command marks the entry forced";

    // Control: the same hit from beyond melee_backpedal_range retargets but
    // queues no back-pedal at all.
    archer->set_foe(nullptr);
    archer->stats()->clear_command();
    foe->setxy(100 + 200, 100);
    archer->stats()->hit_response(foe);
    EXPECT_EQ(foe, archer->foe()) << "a distant attacker is still taken as foe";
    EXPECT_FALSE(archer->stats()->has_commands())
        << "no back-pedal is queued from outside melee_backpedal_range";

    og::runtime::current_session->myscreen_->world().delete_objects();
}

