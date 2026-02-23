#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/sim_world.h>
#include <openglad/sim/irandom.h>
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

struct SimWorldR15Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    SimWorldR15Fixture()
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        gc.config = &cfg;
        set_global_context(&gc);
    }

    ~SimWorldR15Fixture()
    {
        set_global_context(nullptr);
    }
};

TickWalker* add_ob(SimWorldR15Fixture& fx, Order order, char family, unsigned char team, short x, short y, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);
    fx.level.wire_entity(w.get());
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

TickWalker* add_weap(SimWorldR15Fixture& fx, Order order, char family, unsigned char team, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);
    fx.level.wire_entity(w.get());
    w->team_num = team;
    w->dead = dead ? 1 : 0;
    TickWalker* out = w.get();
    fx.level.weaplist.push_back(std::move(w));
    return out;
}

TickWalker* add_fx(SimWorldR15Fixture& fx, Order order, char family, unsigned char team, bool dead = false)
{
    auto w = std::make_unique<TickWalker>();
    w->set_order_family(order, family);
    fx.level.wire_entity(w.get());
    w->team_num = team;
    w->dead = dead ? 1 : 0;
    TickWalker* out = w.get();
    fx.level.fxlist.push_back(std::move(w));
    return out;
}

} // namespace

OG_UNIT_TEST(test_sim_world_r15_normal_tick_cleanup_and_dead_entity_removal)
{
    SimWorldR15Fixture fx;
    og::sim::SimWorld world(42);
    fx.save.my_team = 0;

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

    fx.level.numobs = 3;

    const og::sim::TickResult result = world.tick(fx.level, fx.save, fx.enemy_freeze, 0, fx.events);
    OG_ASSERT(result.level_done == 0);
    OG_ASSERT(!result.game_ended);
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

OG_UNIT_TEST(test_sim_world_r15_freeze_tick_and_level_done_paths)
{
    SimWorldR15Fixture fx;
    og::sim::SimWorld world(7);
    fx.save.my_team = 0;

    TickWalker* ally = add_ob(fx, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 80, 64);
    TickWalker* exit_fx = add_fx(fx, Order::Treasure, FAMILY_EXIT, 0, false);
    OG_ASSERT(ally && enemy && exit_fx);

    fx.enemy_freeze = 11;
    og::sim::TickResult frozen = world.tick(fx.level, fx.save, fx.enemy_freeze, 0, fx.events);
    OG_ASSERT(frozen.level_done == 1);
    OG_ASSERT(!frozen.game_ended);
    OG_ASSERT(ally->acts > 0);
    OG_ASSERT(enemy->acts == 0);

    fx.enemy_freeze = 2;
    const std::size_t before_events = fx.events.size();
    (void)world.tick(fx.level, fx.save, fx.enemy_freeze, 0, fx.events);
    OG_ASSERT(fx.events.size() > before_events);
}

OG_UNIT_TEST(test_sim_world_r15_freeze_uses_friendliness_not_team_zero)
{
    SimWorldR15Fixture fx;
    og::sim::SimWorld world(11);
    fx.save.my_team = 1;

    TickWalker* friendly = add_ob(fx, Order::Living, FAMILY_SOLDIER, 1, 64, 64);
    TickWalker* hostile = add_ob(fx, Order::Living, FAMILY_ORC, 0, 80, 64);
    OG_ASSERT(friendly && hostile);

    fx.enemy_freeze = 11;
    const og::sim::TickResult frozen = world.tick(fx.level, fx.save, fx.enemy_freeze, 0, fx.events);

    OG_ASSERT(frozen.level_done == 2);
    OG_ASSERT(frozen.game_ended);
    OG_ASSERT(friendly->acts > 0);
    OG_ASSERT(hostile->acts == 0);
}

OG_UNIT_TEST(test_sim_world_r15_end_flag_and_auto_advance_paths)
{
    SimWorldR15Fixture fx;
    og::sim::SimWorld world(9);
    fx.save.my_team = 0;

    TickWalker* enemy = add_ob(fx, Order::Living, FAMILY_ORC, 1, 80, 80);
    OG_ASSERT(enemy != nullptr);

    og::sim::TickResult ended = world.tick(fx.level, fx.save, fx.enemy_freeze, 1, fx.events);
    OG_ASSERT(ended.game_ended);

    SimWorldR15Fixture empty_fx;
    og::sim::SimWorld world2(9);
    og::sim::TickResult auto_advance = world2.tick(empty_fx.level, empty_fx.save, empty_fx.enemy_freeze, 0, empty_fx.events);
    OG_ASSERT(auto_advance.game_ended);
    OG_ASSERT(auto_advance.level_done == 2);
    OG_ASSERT(auto_advance.next_level == static_cast<short>(empty_fx.level.id + 1));
}
