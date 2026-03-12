#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/core/constants.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include "test_game_world_fixture.h"
#include "test_gameplay_context_scope.h"
#include <gtest/gtest.h>

#ifdef TESTING
namespace og::sim {
extern std::int32_t g_test_level_tick_limit_override;
}
#endif

namespace {

struct TickWalker : walker {
    int acts = 0;
    bool act() override
    {
        acts++;
        return true;
    }
};

using SimWorldR15Fixture = TestGameWorld;

TickWalker* add_ob(SimWorldR15Fixture& fx, Order order, char family, unsigned char team, short x, short y, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = dead ? 1 : 0;
    TickWalker* out = w.get();
    fx.level.world().oblist.push_back(std::move(w));
    return out;
}

TickWalker* add_weap(SimWorldR15Fixture& fx, Order order, char family, unsigned char team, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->team_num = team;
    w->dead = dead ? 1 : 0;
    TickWalker* out = w.get();
    fx.level.world().weaplist.push_back(std::move(w));
    return out;
}

TickWalker* add_fx(SimWorldR15Fixture& fx, Order order, char family, unsigned char team, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);
    bind_test_entity_sim_context(fx.level, w.get());
    w->team_num = team;
    w->dead = dead ? 1 : 0;
    TickWalker* out = w.get();
    fx.level.world().fxlist.push_back(std::move(w));
    return out;
}

} // namespace

TEST(SimWorld, r15_normal_tick_cleanup_and_dead_entity_removal)
{
    SimWorldR15Fixture fx;
    GameWorld& world = fx.world();
    world.rng_.state_ = 42;
    world.my_team = 0;

    TickWalker* ally = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 76, 64);
    TickWalker* dead_ob = add_ob(fx, Order::Living, FAMILY_ORC, 1, 90, 64, true);
    ASSERT_TRUE(ally && enemy && dead_ob);

    TickWalker* live_weap = add_weap(fx, Order::Weapon, FAMILY_KNIFE, 0, false);
    TickWalker* dead_weap = add_weap(fx, Order::Weapon, FAMILY_KNIFE, 0, true);
    TickWalker* dead_fx = add_fx(fx, Order::FX, FAMILY_EXPLOSION, 0, true);
    ASSERT_TRUE(live_weap && dead_weap && dead_fx);

    ally->foe = dead_ob;
    ally->leader = dead_ob;
    ally->owner = dead_ob;
    ally->collide_ob = dead_ob;

    live_weap->foe = dead_ob;
    live_weap->leader = dead_ob;
    live_weap->owner = dead_ob;
    live_weap->collide_ob = dead_ob;

    fx.level.numobs = 3;

    world.tick();
    ASSERT_TRUE(world.level_done == 0);
    ASSERT_TRUE(!world.game_ended);
    ASSERT_TRUE(ally->acts > 0);
    ASSERT_TRUE(enemy->acts > 0);
    ASSERT_TRUE(live_weap->acts > 0);
    ASSERT_TRUE(ally->foe == nullptr);
    ASSERT_TRUE(ally->leader == nullptr);
    ASSERT_TRUE(ally->owner == nullptr);
    ASSERT_TRUE(ally->collide_ob == nullptr);
    ASSERT_TRUE(live_weap->foe == nullptr);
    ASSERT_TRUE(live_weap->leader == nullptr);
    ASSERT_TRUE(live_weap->owner == nullptr);
    ASSERT_TRUE(live_weap->collide_ob == nullptr);
    ASSERT_TRUE(!fx.level.world().dead_list.empty());
    ASSERT_TRUE(fx.level.world().weaplist.size() == 1);
    ASSERT_TRUE(fx.level.world().fxlist.empty());
}

TEST(SimWorld, r15_freeze_tick_and_level_done_paths)
{
    SimWorldR15Fixture fx;
    GameWorld& world = fx.world();
    world.rng_.state_ = 7;
    world.my_team = 0;

    TickWalker* ally = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 80, 64);
    TickWalker* exit_fx = add_fx(fx, Order::Treasure, FAMILY_EXIT, 0, false);
    ASSERT_TRUE(ally && enemy && exit_fx);

    world.enemy_freeze = 11;
    world.tick();
    ASSERT_TRUE(world.level_done == 1);
    ASSERT_TRUE(!world.game_ended);
    ASSERT_TRUE(ally->acts > 0);
    ASSERT_TRUE(enemy->acts == 0);

    world.enemy_freeze = 2;
    const std::size_t before_events = fx.events.size();
    world.tick();
    ASSERT_TRUE(fx.events.size() > before_events);
}

TEST(SimWorld, r15_freeze_uses_friendliness_not_team_zero)
{
    SimWorldR15Fixture fx;
    GameWorld& world = fx.world();
    world.rng_.state_ = 11;
    world.my_team = 1;

    TickWalker* friendly = add_ob(fx, Order::Living, FAMILY_SOLDIER, 1, 64, 64);
    TickWalker* hostile = add_ob(fx, Order::Living, FAMILY_ORC, 0, 80, 64);
    ASSERT_TRUE(friendly && hostile);

    world.enemy_freeze = 11;
    world.tick();

    ASSERT_TRUE(world.level_done == 2);
    ASSERT_TRUE(world.game_ended);
    ASSERT_TRUE(friendly->acts > 0);
    ASSERT_TRUE(hostile->acts == 0);
}

TEST(SimWorld, r15_end_flag_and_auto_advance_paths)
{
    SimWorldR15Fixture fx;
    GameWorld& world = fx.world();
    world.rng_.state_ = 9;
    world.my_team = 0;

    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 80, 80);
    ASSERT_TRUE(enemy != nullptr);

    world.end = 1;
    world.tick();
    ASSERT_TRUE(world.game_ended);

    SimWorldR15Fixture empty_fx;
    GameWorld& world2 = empty_fx.world();
    world2.rng_.state_ = 9;
    world2.tick();
    ASSERT_TRUE(world2.game_ended);
    ASSERT_TRUE(world2.level_done == 2);
    ASSERT_TRUE(world2.next_level == static_cast<short>(empty_fx.level.world().id + 1));
}

TEST(SimWorld, r15_reset_level_progress_clears_timeout_for_same_level)
{
#ifdef TESTING
    SimWorldR15Fixture fx;
    GameWorld& world = fx.world();
    world.rng_.state_ = 17;
    world.my_team = 0;

    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 80, 64);
    ASSERT_TRUE(enemy != nullptr);

    struct TickLimitGuard {
        std::int32_t saved = 0;
        TickLimitGuard()
            : saved(og::sim::g_test_level_tick_limit_override)
        {
            og::sim::g_test_level_tick_limit_override = 1;
        }
        ~TickLimitGuard()
        {
            og::sim::g_test_level_tick_limit_override = saved;
        }
    } guard;

    world.tick();
    ASSERT_TRUE(!world.game_ended);

    world.tick();
    ASSERT_TRUE(world.game_ended);

    world.reset_level_progress();
    world.tick();
    ASSERT_TRUE(!world.game_ended);
#endif
}
