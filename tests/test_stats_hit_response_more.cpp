#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/platform/game_context.h>
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
    archer->foe = nullptr;

    archer->stats()->hit_response(foe);
    ASSERT_TRUE(archer->stats()->has_commands()) << "archer hit_response should queue a walk away command";

    og::runtime::current_session->myscreen_->world().delete_objects();
}

