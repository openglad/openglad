#include <openglad/interface/render/radar.h>
#include <openglad/platform/game_context.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/core/colors.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
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

// Give floor f an own grid filled with `tile`, matching the base extents.
static void fill_floor_grid(GameWorld& world, int f, unsigned char tile)
{
    const int gw = world.grid.w;
    const int gh = world.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * gh];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * gh, tile);
    world.grid_for_floor(f) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
}

// These radar pixel tests were authored against the historical 320x200
// viewport. Keep that exact geometry local to this suite so non-16:10 display
// aspects cannot move the probes, then restore the live canvas after each test.
class RadarMore : public testing::Test
{
protected:
    void SetUp() override
    {
        game_ = og::runtime::current_session->myscreen_;
        ASSERT_NE(nullptr, game_);
        saved_target_ = game_->active_canvas();
        game_->set_world_canvas_pinned_classic(true);
        game_->relayout_views();
        game_->set_active_canvas(CanvasTarget::World);
    }

    void TearDown() override
    {
        if (game_ == nullptr)
            return;
        game_->set_active_canvas(CanvasTarget::UI);
        game_->set_world_canvas_pinned_classic(false);
        game_->relayout_views();
        game_->set_active_canvas(saved_target_);
    }

private:
    screen* game_ = nullptr;
    CanvasTarget saved_target_ = CanvasTarget::UI;
};
} // namespace

TEST_F(RadarMore, radar_update_and_draw_covers_key_paths)
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
        control->set_team_num(1);
        control->set_view_all(5); // can_see path
    }
    if (friend_living) {
        friend_living->setxy(GRID_SIZE * 3, GRID_SIZE * 2);
        friend_living->set_team_num(1);
    }
    if (enemy_living) {
        enemy_living->setxy(GRID_SIZE * 4, GRID_SIZE * 2);
        enemy_living->set_team_num(2);
        enemy_living->set_invisibility_left(0);
    }
    if (weapon) {
        weapon->setxy(GRID_SIZE * 5, GRID_SIZE * 2);
        weapon->set_team_num(2);
    }
    if (gen) {
        gen->setxy(GRID_SIZE * 6, GRID_SIZE * 2);
        gen->set_team_num(2);
    }
    if (life_gem) {
        life_gem->setxy(GRID_SIZE * 2, GRID_SIZE * 4);
        life_gem->set_team_num(0);
        life_gem->set_dead(0);
    }
    if (exit_fx) {
        exit_fx->setxy(GRID_SIZE * 3, GRID_SIZE * 4);
        exit_fx->stats()->set_level(2);
        exit_fx->set_dead(0);
    }
    if (gold_fx) {
        gold_fx->setxy(GRID_SIZE * 4, GRID_SIZE * 4);
        gold_fx->set_dead(0);
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


TEST_F(RadarMore, radar_start_default_uses_myscreen_level_data)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(); // wrapper path start(&myscreen->level_runtime_data())
    (void)r.draw(&og::runtime::current_session->myscreen_->level_runtime_data());
}

// Westlands terrain radar colors: snow reads white, lava a STATIC ember
// orange (233 — COLOR_FIRE 224 sits in the cycled ORANGE band 224-231 and
// made whole lava fields strobe on the minimap), marsh dark green (distinct
// from the trees ramp), ash a warm dark grey (distinct from pavement 17 and
// walls 24).
TEST_F(RadarMore, westlands_tiles_map_to_pinned_radar_colors)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();

    const std::vector<unsigned char> tiles = {
        PIX_SNOW1, PIX_SNOW2, PIX_LAVA1, PIX_LAVA2,
        PIX_MARSH1, PIX_MARSH2, PIX_ASH1, PIX_ASH2,
    };
    for (int i = 0; i < static_cast<int>(tiles.size()); i++)
        set_tile(d, i, 0, tiles[static_cast<size_t>(i)]);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);
    r.update(&d);
    ASSERT_GE(static_cast<int>(r.bmp.size()), 8)
        << "radar bmp must cover the painted row";

    const std::vector<unsigned char> expected = {
        COLOR_WHITE,    COLOR_WHITE,    // snow
        233,            233,            // lava: static, NEVER the cycled band
        COLOR_GREEN + 7, COLOR_GREEN + 7, // marsh
        249,            249,            // ash
    };
    for (int i = 0; i < 8; i++)
        EXPECT_EQ(static_cast<int>(expected[static_cast<size_t>(i)]),
                  static_cast<int>(r.bmp[static_cast<size_t>(i)]))
            << "radar color for tile id "
            << static_cast<int>(tiles[static_cast<size_t>(i)]);

    // B3 guard: the lava minimap color must sit OUTSIDE both cycled palette
    // bands (water 208-223, orange 224-231) — a cycled index strobes every
    // frame on big lava fields.
    EXPECT_TRUE(r.bmp[2] < WATER_START || r.bmp[2] > ORANGE_END)
        << "lava radar color must not blink with palette cycling";
}

// B2: on multifloor levels the minimap tracks the floor being played — the
// terrain bmp re-bakes from the control walker's floor (and blips filter to
// it); single-floor levels never leave floor 0, keeping the legacy radar
// pixel-identical.
TEST_F(RadarMore, radar_terrain_follows_the_control_floor)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();

    // Floor 0 reads snow (white); floor 1 reads ash (249).
    for (int y = 0; y < d.world().grid.h; y++)
        for (int x = 0; x < d.world().grid.w; x++)
            set_tile(d, x, y, PIX_SNOW1);
    d.world().set_floor_count(2);
    fill_floor_grid(d.world(), 1, PIX_ASH1);

    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
    control->set_team_num(1);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    const Sint32 saved_override = vs->editor_floor_override_;
    vs->control = control;
    vs->radarstart = 1;
    vs->editor_floor_override_ = -1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.force_lower_position = true;
    r.start(&d);
    ASSERT_EQ(0, static_cast<int>(r.bmp_floor_)) << "starts on floor 0";
    ASSERT_EQ(COLOR_WHITE, static_cast<int>(r.bmp[0])) << "floor 0 snow baked";

    // Control walks up the stairs: the next draw re-bakes floor 1 terrain.
    control->set_floor(1);
    ASSERT_EQ(1, r.draw(&d));
    EXPECT_EQ(1, static_cast<int>(r.bmp_floor_)) << "radar follows to floor 1";
    EXPECT_EQ(249, static_cast<int>(r.bmp[0])) << "floor 1 ash baked";

    // And back down.
    control->set_floor(0);
    ASSERT_EQ(1, r.draw(&d));
    EXPECT_EQ(0, static_cast<int>(r.bmp_floor_)) << "radar returns to floor 0";
    EXPECT_EQ(COLOR_WHITE, static_cast<int>(r.bmp[0])) << "floor 0 snow again";

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    vs->editor_floor_override_ = saved_override;
}

// B2 (editor): with no control walker, the editor's floor override picks the
// radar floor, so the minimap shows the floor being edited.
TEST_F(RadarMore, radar_terrain_follows_the_editor_floor_override)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; y++)
        for (int x = 0; x < d.world().grid.w; x++)
            set_tile(d, x, y, PIX_SNOW1);
    d.world().set_floor_count(2);
    fill_floor_grid(d.world(), 1, PIX_ASH1);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    const Sint32 saved_override = vs->editor_floor_override_;
    vs->control = nullptr;
    vs->radarstart = 1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.force_lower_position = true;
    r.start(&d);

    vs->editor_floor_override_ = 1;
    ASSERT_EQ(1, r.draw(&d));
    EXPECT_EQ(1, static_cast<int>(r.bmp_floor_)) << "override picks floor 1";
    EXPECT_EQ(249, static_cast<int>(r.bmp[0])) << "edited floor's terrain";

    vs->editor_floor_override_ = -1;
    ASSERT_EQ(1, r.draw(&d));
    EXPECT_EQ(0, static_cast<int>(r.bmp_floor_))
        << "no override and no control: back to floor 0";
    EXPECT_EQ(COLOR_WHITE, static_cast<int>(r.bmp[0]));

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    vs->editor_floor_override_ = saved_override;
}

// B2 (robustness): a floor whose grid was never authored falls back to the
// base grid (no crash, no garbage), and an out-of-range walker floor clamps
// to the top floor.
TEST_F(RadarMore, radar_survives_missing_floor_grid_and_clamps_floor)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; y++)
        for (int x = 0; x < d.world().grid.w; x++)
            set_tile(d, x, y, PIX_SNOW1);
    d.world().set_floor_count(2); // floor 1 exists but has NO grid

    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
    control->set_team_num(1);
    control->set_floor(5); // beyond the top floor: must clamp, not overrun

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    const Sint32 saved_override = vs->editor_floor_override_;
    vs->control = control;
    vs->radarstart = 1;
    vs->editor_floor_override_ = -1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.force_lower_position = true;
    r.start(&d);

    ASSERT_EQ(1, r.draw(&d));
    EXPECT_EQ(1, static_cast<int>(r.bmp_floor_))
        << "floor 5 with 2 floors clamps to the top floor";
    EXPECT_EQ(COLOR_WHITE, static_cast<int>(r.bmp[0]))
        << "an unauthored floor grid falls back to the base terrain";

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    vs->editor_floor_override_ = saved_override;
}

// B2 (blips): entities blip only on the floor the radar shows.
TEST_F(RadarMore, radar_blips_filter_to_the_shown_floor)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; y++)
        for (int x = 0; x < d.world().grid.w; x++)
            set_tile(d, x, y, PIX_SNOW1);
    d.world().set_floor_count(2);
    fill_floor_grid(d.world(), 1, PIX_ASH1);

    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* buddy = d.add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, control);
    ASSERT_NE(nullptr, buddy);
    control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
    control->set_team_num(1);
    control->set_floor(1);
    buddy->setxy(GRID_SIZE * 6, GRID_SIZE * 6);
    buddy->set_team_num(1);
    buddy->set_floor(1);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    const Sint32 saved_override = vs->editor_floor_override_;
    vs->control = control;
    vs->radarstart = 1;
    vs->editor_floor_override_ = -1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.force_lower_position = true;
    r.start(&d);

    // Draw with the buddy on the radar floor, sample its blip pixel.
    ASSERT_EQ(1, r.draw(&d));
    ASSERT_EQ(1, static_cast<int>(r.bmp_floor_));
    const Sint32 blip_x = r.xloc + ((buddy->xpos() + 1) / GRID_SIZE - r.radarx);
    const Sint32 blip_y = r.yloc + ((buddy->ypos() + 1) / GRID_SIZE - r.radary);
    Uint8 on_r = 0, on_g = 0, on_b = 0;
    og::runtime::current_session->myscreen_->get_pixel(blip_x, blip_y,
                                                       &on_r, &on_g, &on_b);

    // Same scene with the buddy one floor down: the blip disappears (the
    // pixel re-blits to the baked terrain).
    buddy->set_floor(0);
    ASSERT_EQ(1, r.draw(&d));
    Uint8 off_r = 0, off_g = 0, off_b = 0;
    og::runtime::current_session->myscreen_->get_pixel(blip_x, blip_y,
                                                       &off_r, &off_g, &off_b);
    EXPECT_FALSE(on_r == off_r && on_g == off_g && on_b == off_b)
        << "a blip on another floor must not draw over this floor's radar";

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    vs->editor_floor_override_ = saved_override;
}


// Feature B: Z-stair tiles bake two STATIC pinned radar colors — YELLOW (88,
// the climb objective pops even on snow) for PIX_ZSTAIR_UP and LIGHT_BLUE
// (120, the exit-idiom cyan) for PIX_ZSTAIR_DOWN. No pulse, never ctx().rng:
// a cycled or random index would strobe the bake. Tile bytes 140/141 are
// branch-new and absent from all legacy content, so single-floor radars stay
// byte-identical by tile absence.
TEST_F(RadarMore, zstair_tiles_bake_pinned_radar_colors)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();

    set_tile(d, 0, 0, PIX_ZSTAIR_UP);
    set_tile(d, 1, 0, PIX_ZSTAIR_DOWN);
    d.world().set_floor_count(2);
    fill_floor_grid(d.world(), 1, PIX_ZSTAIR_DOWN);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    const Sint32 saved_override = vs->editor_floor_override_;
    vs->control = nullptr;
    vs->radarstart = 1;
    vs->editor_floor_override_ = -1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.force_lower_position = true;
    r.start(&d);
    ASSERT_GE(static_cast<int>(r.bmp.size()), 2)
        << "radar bmp must cover the painted tiles";
    EXPECT_EQ(88, static_cast<int>(r.bmp[0]))
        << "PIX_ZSTAIR_UP bakes YELLOW (static, pinned)";
    EXPECT_EQ(120, static_cast<int>(r.bmp[1]))
        << "PIX_ZSTAIR_DOWN bakes LIGHT_BLUE (static, pinned)";

    // Forced re-bake onto floor 1 (editor override): the stair colors bake
    // identically on stacked floors.
    vs->editor_floor_override_ = 1;
    ASSERT_EQ(1, r.draw(&d));
    EXPECT_EQ(1, static_cast<int>(r.bmp_floor_)) << "override picks floor 1";
    EXPECT_EQ(120, static_cast<int>(r.bmp[0]))
        << "floor 1's PIX_ZSTAIR_DOWN fill baked after the re-bake";

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    vs->editor_floor_override_ = saved_override;
}
