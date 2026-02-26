#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/gameplay/walker.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/core/constants.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <memory>
#include "unit/unit.h"

namespace {

struct TickWalker : walker {
    int acts = 0;
    bool act() override
    {
        acts++;
        return true;
    }
};

struct GameWorldR15Fixture {
    og::gameplay::GameWorld level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    GameWorldR15Fixture()
    {
        level.myobmap = std::make_unique<obmap>();
        level.id = 1;
        level.create_new_grid();
        level.enemy_freeze = 0;
        level.set_sim_context(&save, &level.enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        set_global_context(&gc);
    }

    ~GameWorldR15Fixture()
    {
        set_global_context(nullptr);
    }
};

TickWalker* add_ob(GameWorldR15Fixture& fx, Order order, char family, unsigned char team, short x, short y, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);

    w->setxy(x, y);
    w->sizex = 16;
    w->sizey = 16;
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = dead ? 1 : 0;
    TickWalker* out = w.get();
    fx.level.oblist.push_back(std::move(w));
    return out;
}

TickWalker* add_weap(GameWorldR15Fixture& fx, Order order, char family, unsigned char team, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);

    w->team_num = team;
    w->dead = dead ? 1 : 0;
    TickWalker* out = w.get();
    fx.level.weaplist.push_back(std::move(w));
    return out;
}

TickWalker* add_fx(GameWorldR15Fixture& fx, Order order, char family, unsigned char team, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);

    w->team_num = team;
    w->dead = dead ? 1 : 0;
    TickWalker* out = w.get();
    fx.level.fxlist.push_back(std::move(w));
    return out;
}

} // namespace

static void tick_world(og::gameplay::GameWorld& world, og::sim::SimEventLog& events)
{
    og::gameplay::GameplayContext local_ctx;
    local_ctx.world = &world;
    local_ctx.sim_events = &events;
    og::gameplay::GameplayContext* prev = og::gameplay::current_game;
    og::gameplay::current_game = &local_ctx;
    world.tick();
    og::gameplay::current_game = prev;
}

OG_UNIT_TEST(test_game_world_r15_normal_tick_cleanup_and_dead_entity_removal)
{
    GameWorldR15Fixture fx;
    og::gameplay::GameWorld& world = fx.level;
    world.rng_.state_ = 42;
    fx.save.my_team = 0;
    world.my_team = fx.save.my_team;

    TickWalker* ally = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 76, 64);
    TickWalker* dead_ob = add_ob(fx, Order::Living, FAMILY_ORC, 1, 90, 64, true);
    OG_ASSERT(ally && enemy && dead_ob);

    TickWalker* live_weap = add_weap(fx, Order::Weapon, FAMILY_KNIFE, 0, false);
    TickWalker* dead_weap = add_weap(fx, Order::Weapon, FAMILY_KNIFE, 0, true);
    TickWalker* dead_fx = add_fx(fx, Order::FX, FAMILY_EXPLOSION, 0, true);
    OG_ASSERT(live_weap && dead_weap && dead_fx);

    ally->foe = dead_ob;
    ally->leader = dead_ob;
    ally->owner = dead_ob;
    ally->collide_ob = dead_ob;

    live_weap->foe = dead_ob;
    live_weap->leader = dead_ob;
    live_weap->owner = dead_ob;
    live_weap->collide_ob = dead_ob;

    fx.level.living_count = 3;

    tick_world(world, fx.events);
    OG_ASSERT(world.level_done == 0);
    OG_ASSERT(!world.game_ended);
    OG_ASSERT(ally->acts > 0);
    OG_ASSERT(enemy->acts > 0);
    OG_ASSERT(live_weap->acts > 0);
    OG_ASSERT(ally->foe == nullptr);
    OG_ASSERT(ally->leader == nullptr);
    OG_ASSERT(ally->owner == nullptr);
    OG_ASSERT(ally->collide_ob == nullptr);
    OG_ASSERT(live_weap->foe == nullptr);
    OG_ASSERT(live_weap->leader == nullptr);
    OG_ASSERT(live_weap->owner == nullptr);
    OG_ASSERT(live_weap->collide_ob == nullptr);
    OG_ASSERT(!fx.level.dead_list.empty());
    OG_ASSERT(fx.level.weaplist.size() == 1);
    OG_ASSERT(fx.level.fxlist.empty());
}

OG_UNIT_TEST(test_game_world_r15_freeze_tick_and_level_done_paths)
{
    GameWorldR15Fixture fx;
    og::gameplay::GameWorld& world = fx.level;
    world.rng_.state_ = 7;
    fx.save.my_team = 0;
    world.my_team = fx.save.my_team;

    TickWalker* ally = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 80, 64);
    TickWalker* exit_fx = add_fx(fx, Order::Treasure, FAMILY_EXIT, 0, false);
    OG_ASSERT(ally && enemy && exit_fx);

    world.enemy_freeze = 11;
    tick_world(world, fx.events);
    OG_ASSERT(world.level_done == 1);
    OG_ASSERT(!world.game_ended);
    OG_ASSERT(ally->acts > 0);
    OG_ASSERT(enemy->acts == 0);

    world.enemy_freeze = 2;
    const std::size_t before_events = fx.events.size();
    tick_world(world, fx.events);
    OG_ASSERT(fx.events.size() > before_events);
}

OG_UNIT_TEST(test_game_world_r15_freeze_uses_friendliness_not_team_zero)
{
    GameWorldR15Fixture fx;
    og::gameplay::GameWorld& world = fx.level;
    world.rng_.state_ = 11;
    fx.save.my_team = 1;
    world.my_team = fx.save.my_team;

    TickWalker* friendly = add_ob(fx, Order::Living, FAMILY_SOLDIER, 1, 64, 64);
    TickWalker* hostile = add_ob(fx, Order::Living, FAMILY_ORC, 0, 80, 64);
    OG_ASSERT(friendly && hostile);

    world.enemy_freeze = 11;
    tick_world(world, fx.events);

    OG_ASSERT(world.level_done == 2);
    OG_ASSERT(world.game_ended);
    OG_ASSERT(friendly->acts > 0);
    OG_ASSERT(hostile->acts == 0);
}

OG_UNIT_TEST(test_game_world_r15_end_flag_and_auto_advance_paths)
{
    GameWorldR15Fixture fx;
    og::gameplay::GameWorld& world = fx.level;
    world.rng_.state_ = 9;
    fx.save.my_team = 0;
    world.my_team = fx.save.my_team;

    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 80, 80);
    OG_ASSERT(enemy != nullptr);

    world.end = 1;
    tick_world(world, fx.events);
    OG_ASSERT(world.game_ended);

    GameWorldR15Fixture empty_fx;
    og::gameplay::GameWorld& world2 = empty_fx.level;
    world2.rng_.state_ = 9;
    world2.my_team = empty_fx.save.my_team;
    tick_world(world2, empty_fx.events);
    OG_ASSERT(world2.game_ended);
    OG_ASSERT(world2.level_done == 2);
    OG_ASSERT(world2.next_level == static_cast<short>(empty_fx.level.id + 1));
}
