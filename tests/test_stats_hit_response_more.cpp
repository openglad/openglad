#include "graph.h"
#include "runtime/game_context.h"
#include "test_framework.h"

extern screen* myscreen;

namespace
{
struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* ctx) { set_global_context(ctx); }
    ~GlobalContextGuard() { set_global_context(nullptr); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};
} // namespace

void test_statistics_hit_response_archer_runs_away_and_queues_walk()
{
    myscreen->level_data.delete_objects();

    FixedRandom fixed_rng(1);
    GameContext c;
    c.game_screen = myscreen;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    walker* archer = myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    walker* foe = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(archer != nullptr && foe != nullptr, "actors created");
    if (!(archer && foe))
        return;

    archer->setxy(100, 100);
    foe->setxy(120, 100); // within < 64 distance
    archer->stats()->clear_command();
    archer->foe = nullptr;

    archer->stats()->hit_response(foe);
    TEST_ASSERT(archer->stats()->has_commands(), "archer hit_response should queue a walk away command");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_statistics_hit_response_archer_runs_away_and_queues_walk);

