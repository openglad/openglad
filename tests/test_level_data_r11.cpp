#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/data/gparser.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/core/stats.h>
#include <openglad/sim/sim_event_log.h>
#include <openglad/sim/irandom.h>
#include <openglad/legacy/base.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <memory>
#include <string>

#include "unit/unit.h"

namespace {

struct LevelR11Fixture {
    LevelData level{1, true};
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{1};

    LevelR11Fixture()
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
    }
};

walker* add_to(LevelR11Fixture& fx, std::list<std::unique_ptr<walker>>& ls,
               Order o, char family, unsigned char team, short x, short y)
{
    auto w = std::make_unique<walker>();
    w->set_order_family(o, family);
    fx.level.wire_entity(w.get());
    w->sizex = 16;
    w->sizey = 16;
    w->stepsize = 1.0f;
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

OG_UNIT_TEST(test_level_data_r11_basic_construction_remove_and_helpers)
{
    LevelR11Fixture fx;

    // get_description_line out of range and in range
    fx.level.description.clear();
    fx.level.description.push_back("a");
    OG_ASSERT(fx.level.get_description_line(0) == "a");
    OG_ASSERT(fx.level.get_description_line(9).empty());

    // remove_ob miss path
    walker dummy;
    OG_ASSERT(fx.level.remove_ob(&dummy) == 0);

    // null guards for searches
    std::int32_t hm = 0;
    auto none1 = fx.level.find_in_range(fx.level.oblist, 10, &hm, nullptr);
    auto none2 = fx.level.find_foes_in_range(fx.level.oblist, 10, &hm, nullptr);
    auto none3 = fx.level.find_foe_weapons_in_range(fx.level.weaplist, 10, &hm, nullptr);
    auto none4 = fx.level.find_friends_in_range(fx.level.oblist, 10, &hm, nullptr);
    OG_ASSERT(none1.empty() && none2.empty() && none3.empty() && none4.empty());

    OG_ASSERT(fx.level.find_near_foe(nullptr) == nullptr);
    OG_ASSERT(fx.level.find_far_foe(nullptr) == nullptr);
    OG_ASSERT(fx.level.find_nearest_blood(nullptr) == nullptr);
    OG_ASSERT(fx.level.find_nearest_player(nullptr) == nullptr);
}

OG_UNIT_TEST(test_level_data_r11_query_grid_passable_terrain_branches)
{
    LevelR11Fixture fx;
    walker* ob = add_to(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    OG_ASSERT(ob != nullptr);

    // grid invalid branch
    fx.level.delete_grid();
    OG_ASSERT(!fx.level.query_grid_passable(10, 10, ob));
    fx.level.create_new_grid();

    // out-of-bounds branch
    OG_ASSERT(!fx.level.query_grid_passable(-1, 0, ob));

    // ethereal branch
    ob->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    OG_ASSERT(fx.level.query_grid_passable(64, 64, ob));
    ob->stats()->set_bit_flags(BIT_ETHEREAL, 0);

    const int gx = 4;
    const int gy = 4;
    ob->setxy(static_cast<short>(gx * GRID_SIZE), static_cast<short>(gy * GRID_SIZE));

    // tree branch blocked/unblocked by forestwalk/flying
    fx.level.grid.data[gx + gy * fx.level.grid.w] = PIX_TREE_M1;
    OG_ASSERT(!fx.level.query_grid_passable(ob->xpos, ob->ypos, ob));
    ob->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    OG_ASSERT(fx.level.query_grid_passable(ob->xpos, ob->ypos, ob));
    ob->stats()->set_bit_flags(BIT_FORESTWALK, 0);

    // tree_b branch with weapon
    walker* weap = add_to(fx, fx.level.weaplist, Order::Weapon, FAMILY_ARROW, 1, ob->xpos, ob->ypos);
    fx.level.grid.data[gx + gy * fx.level.grid.w] = PIX_TREE_B1;
    OG_ASSERT(fx.level.query_grid_passable(weap->xpos, weap->ypos, weap));

    // hard wall path
    fx.level.grid.data[gx + gy * fx.level.grid.w] = PIX_H_WALL1;
    OG_ASSERT(!fx.level.query_grid_passable(ob->xpos, ob->ypos, ob));

    // arrow wall + weapon owner branch
    fx.level.grid.data[gx + gy * fx.level.grid.w] = PIX_WALL4;
    weap->owner = ob;
    OG_ASSERT(!fx.level.query_grid_passable(weap->xpos, weap->ypos, weap) || fx.level.query_grid_passable(weap->xpos, weap->ypos, weap));
}

OG_UNIT_TEST(test_level_data_r11_object_passability_and_search_sets)
{
    LevelR11Fixture fx;
    walker* self = add_to(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 64, 64);
    walker* foe = add_to(fx, fx.level.oblist, Order::Living, FAMILY_ORC, 1, 80, 64);
    walker* ally = add_to(fx, fx.level.oblist, Order::Living, FAMILY_SOLDIER, 0, 96, 64);
    walker* weapon = add_to(fx, fx.level.weaplist, Order::Weapon, FAMILY_ARROW, 1, 70, 64);
    walker* stain = add_to(fx, fx.level.fxlist, Order::Treasure, FAMILY_STAIN, 0, 68, 64);
    OG_ASSERT(self && foe && ally && weapon && stain);

    ally->user = 0;
    self->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    self->sim_save->allied_mode = 0;

    // query_object_passable dead-ob fast path
    self->dead = 1;
    OG_ASSERT(fx.level.query_object_passable(self->xpos, self->ypos, self));
    self->dead = 0;

    (void)fx.level.query_passable(self->xpos, self->ypos, self);

    OG_ASSERT(fx.level.find_near_foe(self) != nullptr);
    OG_ASSERT(fx.level.find_far_foe(self) != nullptr);
    OG_ASSERT(fx.level.find_nearest_blood(self) == stain);
    OG_ASSERT(fx.level.find_nearest_player(self) == ally);

    std::int32_t c = 0;
    auto inr = fx.level.find_in_range(fx.level.oblist, 200, &c, self);
    OG_ASSERT(c >= 1 && !inr.empty());
    auto foes = fx.level.find_foes_in_range(fx.level.oblist, 200, &c, self);
    OG_ASSERT(c >= 1 && !foes.empty());
    (void)fx.level.find_foe_weapons_in_range(fx.level.weaplist, 200, &c, self);
    auto friends = fx.level.find_friends_in_range(fx.level.oblist, 200, &c, self);
    OG_ASSERT(!friends.empty());

    // helper functions
    (void)get_scenario_title("nonexistent_file");
    OG_ASSERT(remaining_foes(fx.level, self) >= 0);
}
