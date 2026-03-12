#include <openglad/interface/render/radar.h>
#include <openglad/platform/game_context.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <vector>

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

static void set_tile(LevelRuntimeData& d, int x, int y, unsigned char t)
{
    if (x < 0 || y < 0 || x >= d.world().grid.w || y >= d.world().grid.h)
        return;
    d.world().grid.data[y * d.world().grid.w + x] = t;
}
} // namespace

TEST(RadarMore, radar_update_and_draw_covers_key_paths)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();

    // Place representative tiles to cover many radar::update() switch cases.
    const std::vector<unsigned char> tiles = {
        PIX_GRASS1, PIX_GRASS2, PIX_GRASS3, PIX_GRASS4,
        PIX_GRASS_DARK_1, PIX_GRASS_DARK_2, PIX_GRASS_DARK_3, PIX_GRASS_DARK_4,
        PIX_GRASS_DARK_LL, PIX_GRASS_DARK_UR, PIX_GRASS_RUBBLE,
        PIX_GRASS_LIGHT_1, PIX_GRASS_LIGHT_TOP, PIX_GRASS_LIGHT_RIGHT, PIX_GRASS_LIGHT_BOTTOM,
        PIX_TREE_M1, PIX_TREE_T1, PIX_TREE_B1,
        PIX_PAVEMENT1, PIX_COBBLE_1, PIX_FLOOR1,
        PIX_DIRT_1, PIX_DIRT_DARK_1,
        PIX_CLIFF_TOP, PIX_CARPET_M, PIX_H_WALL1,
        PIX_WATER1, PIX_WATERGRASS_LL, PIX_GRASSWATER_LL,
        PIX_WALLSIDE1, PIX_TORCH1,
    };
    int idx = 0;
    for (int y = 0; y < d.world().grid.h && idx < (int)tiles.size(); y++)
        for (int x = 0; x < d.world().grid.w && idx < (int)tiles.size(); x++)
            set_tile(d, x, y, tiles[idx++]);

    // Place a few objects to exercise radar::draw object filtering and colors.
    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* friend_living = d.add_ob(Order::Living, FAMILY_ELF);
    walker* enemy_living = d.add_ob(Order::Living, FAMILY_ORC);
    walker* life_gem = d.add_fx_ob(Order::Treasure, FAMILY_LIFE_GEM);
    walker* exit_fx = d.add_fx_ob(Order::Treasure, FAMILY_EXIT);
    walker* gold_fx = d.add_fx_ob(Order::Treasure, FAMILY_GOLD_BAR);
    walker* weapon = d.add_weap_ob(Order::Weapon, FAMILY_ARROW);
    walker* gen = d.add_ob(Order::Generator, FAMILY_TENT);

    if (control) {
        control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
        control->team_num = 1;
        control->view_all = 5; // can_see path
    }
    if (friend_living) {
        friend_living->setxy(GRID_SIZE * 3, GRID_SIZE * 2);
        friend_living->team_num = 1;
    }
    if (enemy_living) {
        enemy_living->setxy(GRID_SIZE * 4, GRID_SIZE * 2);
        enemy_living->team_num = 2;
        enemy_living->invisibility_left = 0;
    }
    if (weapon) {
        weapon->setxy(GRID_SIZE * 5, GRID_SIZE * 2);
        weapon->team_num = 2;
    }
    if (gen) {
        gen->setxy(GRID_SIZE * 6, GRID_SIZE * 2);
        gen->team_num = 2;
    }
    if (life_gem) {
        life_gem->setxy(GRID_SIZE * 2, GRID_SIZE * 4);
        life_gem->team_num = 0;
        life_gem->dead = 0;
    }
    if (exit_fx) {
        exit_fx->setxy(GRID_SIZE * 3, GRID_SIZE * 4);
        exit_fx->stats()->level = 2;
        exit_fx->dead = 0;
    }
    if (gold_fx) {
        gold_fx->setxy(GRID_SIZE * 4, GRID_SIZE * 4);
        gold_fx->dead = 0;
    }

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = control;
    vs->radarstart = 0; // force radar::start path on first draw

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.force_lower_position = true;
    r.start(&d);
    ASSERT_TRUE(r.draw(&d) == 1) << "radar draw should succeed";

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
}


TEST(RadarMore, radar_start_default_uses_myscreen_level_data)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(); // wrapper path start(&myscreen->level_runtime_data())
    (void)r.draw(&og::runtime::current_session->myscreen_->level_runtime_data());
}

