#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>

#include <memory>
#include <string>

#include "unit/unit.h"

namespace {

struct LevelFixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{1};

    LevelFixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_to_list(LevelFixture& fx, std::list<std::unique_ptr<walker>>& ls,
                    Order o, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(o, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->setxy(x, y);
    w->team_num = team;
    w->real_team_num = 255;
    w->dead = 0;
    walker* out = w.get();
    ls.push_back(std::move(w));
    if (o == Order::Living)
        fx.level.numobs++;
    return out;
}

} // namespace

OG_UNIT_TEST(test_level_data_grid_and_description_paths)
{
    LevelFixture fx;
    fx.level.resize_grid(2, 2); // invalid no-op
    OG_ASSERT(fx.level.grid.w == 40);
    OG_ASSERT(fx.level.grid.h == 60);

    fx.level.resize_grid(50, 50);
    OG_ASSERT(fx.level.grid.w == 50);
    OG_ASSERT(fx.level.grid.h == 50);

    fx.level.description.clear();
    fx.level.description.push_back("line-1");
    OG_ASSERT(fx.level.get_description_line(0) == "line-1");
    OG_ASSERT(fx.level.get_description_line(3).empty());

    fx.level.set_draw_pos(3, 4);
    fx.level.add_draw_pos(2, -1);
    OG_ASSERT(fx.level.topx == 5);
    OG_ASSERT(fx.level.topy == 3);
}

OG_UNIT_TEST(test_level_data_passable_and_range_queries)
{
    LevelFixture fx;
    walker* self = add_to_list(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_to_list(fx, fx.level.oblist, Order::Living, FAMILY_ORC, 1, 80, 64);
    walker* player = add_to_list(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 96, 64);
    walker* weapon = add_to_list(fx, fx.level.weaplist, Order::Weapon, FAMILY_KNIFE, 1, 72, 64);
    walker* stain = add_to_list(fx, fx.level.fxlist, Order::Treasure, FAMILY_STAIN, 0, 70, 64);
    OG_ASSERT(self && foe && player && weapon && stain);

    player->user = 0;
    self->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    OG_ASSERT(fx.level.query_grid_passable(10, 10, self));
    self->stats()->set_bit_flags(BIT_ETHEREAL, 0);
    OG_ASSERT(!fx.level.query_grid_passable(-1, 10, self));

    const int tile = 4 + 4 * fx.level.grid.w;
    fx.level.grid.data[tile] = PIX_H_WALL1;
    self->setxy(4 * GRID_SIZE, 4 * GRID_SIZE);
    OG_ASSERT(!fx.level.query_grid_passable(self->xpos, self->ypos, self));

    self->dead = 1;
    OG_ASSERT(fx.level.query_object_passable(self->xpos, self->ypos, self));
    self->dead = 0;
    (void)fx.level.query_passable(self->xpos, self->ypos, self);

    OG_ASSERT(fx.level.find_near_foe(self) != nullptr);
    OG_ASSERT(fx.level.find_far_foe(self) != nullptr);
    OG_ASSERT(fx.level.find_nearest_blood(self) == stain);
    OG_ASSERT(fx.level.find_nearest_player(self) == player);

    std::int32_t count = 0;
    (void)fx.level.find_in_range(fx.level.oblist, 200, &count, self);
    OG_ASSERT(count >= 2);
    (void)fx.level.find_foes_in_range(fx.level.oblist, 200, &count, self);
    OG_ASSERT(count >= 1);
    (void)fx.level.find_foe_weapons_in_range(fx.level.weaplist, 200, &count, self);
    (void)fx.level.find_friends_in_range(fx.level.oblist, 200, &count, self);
}

OG_UNIT_TEST(test_level_data_remove_and_remaining_foes_paths)
{
    LevelFixture fx;
    walker* self = add_to_list(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_to_list(fx, fx.level.oblist, Order::Living, FAMILY_ORC, 1, 80, 64);
    OG_ASSERT(self && foe);
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));

    const short foes_before = remaining_foes(fx.level, self);
    OG_ASSERT(foes_before >= 1);

    OG_ASSERT(fx.level.remove_ob(foe) == 1);
    const short foes_after = remaining_foes(fx.level, self);
    OG_ASSERT(foes_after <= foes_before);

    fx.level.delete_objects();
    OG_ASSERT(fx.level.oblist.empty());
    OG_ASSERT(fx.level.fxlist.empty());
    OG_ASSERT(fx.level.weaplist.empty());
}
