#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/core/colors.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/family_presentation.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include "test_save_state_guard.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
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
    d.world().grid.data[static_cast<std::size_t>(y * d.world().grid.w + x)] = t;
}

// Give floor f an own grid filled with `tile`, matching the base extents.
static void fill_floor_grid(GameWorld& world, int f, unsigned char tile)
{
    const int gw = world.grid.w;
    const int gh = world.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), tile);
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
            set_tile(d, x, y, tiles[static_cast<std::size_t>(idx++)]);

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
    screen* const active_screen = og::runtime::current_session->myscreen_;
    viewscreen* vs = active_screen->viewob[0].get();
    ASSERT_TRUE(vs != nullptr) << "viewscreen exists";
    if (!vs)
        return;

    GameWorld& world = active_screen->world();
    const bool created_grid = !world.grid.valid();
    if (created_grid)
        world.create_new_grid();
    struct GridRestore
    {
        GameWorld& world;
        bool created;
        ~GridRestore()
        {
            if (created)
                world.delete_grid();
        }
    } grid_restore{world, created_grid};

    ASSERT_TRUE(world.grid.valid());
    ASSERT_NE(nullptr, world.grid.data.get());

    struct ByteRestore
    {
        unsigned char* value;
        unsigned char original;
        ~ByteRestore()
        {
            if (value != nullptr)
                *value = original;
        }
    };

    ByteRestore tile_restore{world.grid.data.get(), world.grid.data[0]};
    unsigned char* decor_cell = world.decor.valid()
        ? world.decor.data.get()
        : nullptr;
    ByteRestore decor_restore{
        decor_cell, decor_cell != nullptr ? *decor_cell : DECOR_NONE};

    world.grid.data[0] = PIX_GRASS1;
    if (decor_cell != nullptr)
        *decor_cell = DECOR_NONE;

    radar r(vs, active_screen, 0);
    r.start(); // wrapper path start(&myscreen->level_runtime_data())
    ASSERT_FALSE(r.bmp.empty());
    const unsigned char grass_color = r.bmp[0];
    EXPECT_EQ(COLOR_GREEN + 3, static_cast<int>(grass_color));

    world.grid.data[0] = PIX_WATER1;
    r.update(); // wrapper path update(&myscreen->level_runtime_data())
    ASSERT_FALSE(r.bmp.empty());
    EXPECT_NE(grass_color, r.bmp[0]);
    EXPECT_EQ(1, r.draw());
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

TEST_F(RadarMore, every_water_dominant_shoreline_uses_the_water_color)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    constexpr std::array<unsigned char, 11> water_tiles = {
        PIX_WATER1,        PIX_WATER2,        PIX_WATER3,
        PIX_WATERGRASS_LL, PIX_WATERGRASS_LR, PIX_WATERGRASS_UL,
        PIX_WATERGRASS_UR, PIX_WATERGRASS_U,  PIX_WATERGRASS_L,
        PIX_WATERGRASS_R,  PIX_WATERGRASS_D,
    };
    for (std::size_t i = 0; i < water_tiles.size(); ++i)
        set_tile(d, static_cast<int>(i), 0, water_tiles[i]);

    viewscreen* const vs =
        og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);
    r.update(&d);
    ASSERT_GE(r.bmp.size(), water_tiles.size());
    for (std::size_t i = 0; i < water_tiles.size(); ++i)
    {
        EXPECT_EQ(COLOR_BLUE + 2, static_cast<int>(r.bmp[i]))
            << "radar color for water tile "
            << static_cast<int>(water_tiles[i]);
    }
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


// --- descriptor-driven treasure blips -------------------------------------
//
// The radar's per-family colour switch is gone: every treasure blip reads
// og::RadarBlip off its family descriptor, so a class-pack treasure lights up
// the minimap with no engine change. Two invariants the switch also carried
// and these tests pin, because the radar draws from the GAME rng and its call
// count is part of that stream:
//   * a family with no colour draws nothing AND rolls nothing;
//   * jitter == 0 makes no rng call at all.

namespace
{
// Neutralises every draw (always 0) while counting how many were made.
class CountingRandom final : public IRandom
{
public:
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        calls++;
        last_max = max_exclusive;
        return 0;
    }
    int calls = 0;
    std::uint32_t last_max = 0;
};

class ScopedEffectDescriptorRestore final
{
public:
    ScopedEffectDescriptorRestore(int family,
                                  EffectFamilyDescriptor descriptor)
        : family_(family), descriptor_(std::move(descriptor))
    {
    }

    ~ScopedEffectDescriptorRestore()
    {
        EXPECT_TRUE(set_effect_family_descriptor(family_, descriptor_))
            << "failed to restore effect family " << family_;
    }

    ScopedEffectDescriptorRestore(const ScopedEffectDescriptorRestore&) =
        delete;
    ScopedEffectDescriptorRestore& operator=(
        const ScopedEffectDescriptorRestore&) = delete;

private:
    int family_;
    EffectFamilyDescriptor descriptor_;
};

// Palette index the radar actually painted at a grid cell.
int blip_index_at(const radar& r, int grid_x, int grid_y)
{
    int index = 0;
    og::runtime::current_session->myscreen_->get_pixel(
        r.xloc + grid_x - r.radarx, r.yloc + grid_y - r.radary, &index);
    return index;
}

std::array<int, 3> blip_rgb_at(const radar& r, int grid_x, int grid_y)
{
    Uint8 red = 0;
    Uint8 green = 0;
    Uint8 blue = 0;
    og::runtime::current_session->myscreen_->get_pixel(
        r.xloc + grid_x - r.radarx, r.yloc + grid_y - r.radary,
        &red, &green, &blue);
    return {red, green, blue};
}

std::array<int, 3> palette_rgb(int index)
{
    int red = 0;
    int green = 0;
    int blue = 0;
    query_palette_reg(static_cast<unsigned char>(index), &red, &green, &blue);
    return {red * 4, green * 4, blue * 4};
}
} // namespace

// #267: descriptor landmarks can be acting Order::FX entities in oblist,
// not treasures in fxlist. Render that path in the declared colour while a
// quiet FX family remains invisible. Sixteen consecutive frames prove both
// pulse sizes and every brightness phase without depending on the
// process-global phase at which this test begins.
TEST_F(RadarMore, landmark_fx_in_oblist_use_their_descriptor_ping)
{
    og::test::ScopedCampaignMountState mount_restore;
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    const int soccer_family =
        og::families::resolve_family_string_id(Order::FX, "modes:ball");
    const int basketball_family =
        og::families::resolve_family_string_id(Order::FX, "modes:bball");
    const int shadow_family =
        og::families::resolve_family_string_id(Order::FX, "modes:bshadow");
    ASSERT_GE(soccer_family, 0);
    ASSERT_GE(basketball_family, 0);
    ASSERT_GE(shadow_family, 0);

    const EffectFamilyDescriptor* soccer_d =
        get_effect_family_descriptor(soccer_family);
    const EffectFamilyDescriptor* basketball_d =
        get_effect_family_descriptor(basketball_family);
    const EffectFamilyDescriptor* shadow_d =
        get_effect_family_descriptor(shadow_family);
    ASSERT_NE(nullptr, soccer_d);
    ASSERT_NE(nullptr, basketball_d);
    ASSERT_NE(nullptr, shadow_d);
    EXPECT_EQ(COLOR_WHITE, soccer_d->radar.color);
    EXPECT_EQ(0, soccer_d->radar.jitter);
    EXPECT_TRUE(soccer_d->radar.landmark);
    EXPECT_TRUE(soccer_d->radar.ping);
    EXPECT_EQ(og::kRadarColorNone, basketball_d->radar.color);
    EXPECT_EQ(0, basketball_d->radar.jitter);
    EXPECT_FALSE(basketball_d->radar.landmark);
    EXPECT_FALSE(basketball_d->radar.ping);
    EXPECT_EQ(COLOR_YELLOW, shadow_d->radar.color);
    EXPECT_EQ(0, shadow_d->radar.jitter);
    EXPECT_TRUE(shadow_d->radar.landmark);
    EXPECT_TRUE(shadow_d->radar.ping);

    CountingRandom counter;
    GameContext c;
    c.rng = &counter;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; ++y)
        for (int x = 0; x < d.world().grid.w; ++x)
            set_tile(d, x, y, PIX_COBBLE_1);

    struct PlacedFx
    {
        walker* ob;
        int family;
        int gx;
        int gy;
        int base;
    };
    auto place = [&d](int family, int gx, int gy, int base) {
        walker* ob = d.add_ob(Order::FX, family);
        EXPECT_NE(nullptr, ob) << "family " << family;
        if (ob != nullptr)
        {
            ob->setxy(static_cast<short>(GRID_SIZE * gx),
                      static_cast<short>(GRID_SIZE * gy));
            ob->set_dead(0);
        }
        return PlacedFx{ob, family, gx, gy, base};
    };
    const PlacedFx soccer =
        place(soccer_family, 8, 8, soccer_d->radar.color);
    const PlacedFx shadow =
        place(shadow_family, 16, 8, shadow_d->radar.color);
    const PlacedFx basketball = place(basketball_family, 24, 8, 0);
    ASSERT_NE(nullptr, soccer.ob);
    ASSERT_NE(nullptr, basketball.ob);
    ASSERT_NE(nullptr, shadow.ob);

    auto pointer_count = [](const GameWorld::EntityList& list,
                            const walker* needle) {
        return std::count_if(list.begin(), list.end(),
                             [needle](const auto& entry) {
                                 return entry.get() == needle;
                             });
    };
    for (const PlacedFx* placed : {&soccer, &basketball, &shadow})
    {
        EXPECT_EQ(Order::FX, placed->ob->query_order());
        EXPECT_EQ(1, pointer_count(d.world().oblist, placed->ob));
        EXPECT_EQ(0, pointer_count(d.world().fxlist, placed->ob));
    }

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = nullptr;
    vs->radarstart = 1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);
    std::array<int, 2> five_pixel_frames{};
    std::array<int, 2> nine_pixel_frames{};
    std::array<std::array<int, 4>, 2> color_frames{};
    const std::array<const PlacedFx*, 2> landmarks = {&soccer, &shadow};
    std::array<std::array<std::array<int, 3>, 4>, 2> expected_colors{};
    for (std::size_t i = 0; i < landmarks.size(); ++i)
        for (std::size_t phase = 0; phase < expected_colors[i].size(); ++phase)
            expected_colors[i][phase] =
                palette_rgb(landmarks[i]->base + static_cast<int>(phase));

    counter.calls = 0;
    for (int frame = 0; frame < 16; ++frame)
    {
        ASSERT_EQ(1, r.draw(&d));
        const std::array<int, 3> background = blip_rgb_at(r, 30, 20);
        for (std::size_t i = 0; i < landmarks.size(); ++i)
        {
            const PlacedFx& landmark = *landmarks[i];
            const std::array<int, 3> center =
                blip_rgb_at(r, landmark.gx, landmark.gy);
            const auto phase = std::find(expected_colors[i].begin(),
                                         expected_colors[i].end(), center);
            ASSERT_NE(expected_colors[i].end(), phase)
                << "frame " << frame << " landmark " << i;
            color_frames[i][static_cast<std::size_t>(
                std::distance(expected_colors[i].begin(), phase))]++;

            int painted = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const std::array<int, 3> pixel =
                        blip_rgb_at(r, landmark.gx + dx, landmark.gy + dy);
                    if (pixel != background)
                    {
                        painted++;
                        EXPECT_EQ(center, pixel)
                            << "frame " << frame << " landmark " << i;
                    }
                }
            if (painted == 5)
                five_pixel_frames[i]++;
            else if (painted == 9)
                nine_pixel_frames[i]++;
            else
                ADD_FAILURE() << "frame " << frame << " landmark " << i
                              << " painted " << painted << " pixels";
        }
        EXPECT_EQ(background, blip_rgb_at(r, basketball.gx, basketball.gy))
            << "quiet FX furniture must stay off the radar";
    }

    EXPECT_EQ(0, counter.calls) << "jitter 0 must not touch the game RNG";
    for (std::size_t i = 0; i < landmarks.size(); ++i)
    {
        EXPECT_EQ(8, five_pixel_frames[i]);
        EXPECT_EQ(8, nine_pixel_frames[i]);
        for (int count : color_frames[i])
            EXPECT_EQ(4, count);
    }

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
}

// A landmark created with og.add_fx_ob has no acting-object or beacon proxy.
// It still uses the same public descriptor presentation as an oblist FX.
// Same-family controls on another floor and just outside the radar prove the
// direct fxlist path keeps both existing clipping gates.
TEST_F(RadarMore, landmark_fx_in_fxlist_draw_without_a_beacon)
{
    og::test::ScopedCampaignMountState mount_restore;
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    const int shadow_family =
        og::families::resolve_family_string_id(Order::FX, "modes:bshadow");
    ASSERT_GE(shadow_family, 0);
    const EffectFamilyDescriptor* shadow_d =
        get_effect_family_descriptor(shadow_family);
    ASSERT_NE(nullptr, shadow_d);
    ASSERT_TRUE(shadow_d->radar.landmark);
    ASSERT_TRUE(shadow_d->radar.ping);
    ASSERT_EQ(COLOR_YELLOW, shadow_d->radar.color);
    ASSERT_EQ(0, shadow_d->radar.jitter);

    CountingRandom counter;
    GameContext c;
    c.rng = &counter;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; ++y)
        for (int x = 0; x < d.world().grid.w; ++x)
            set_tile(d, x, y, PIX_COBBLE_1);
    d.world().set_floor_count(2);
    fill_floor_grid(d.world(), 1, PIX_COBBLE_1);

    constexpr int landmark_gx = 8;
    constexpr int landmark_gy = 8;
    constexpr int other_floor_gx = 16;
    walker* landmark = d.add_fx_ob(Order::FX, shadow_family);
    walker* other_floor = d.add_fx_ob(Order::FX, shadow_family);
    ASSERT_NE(nullptr, landmark);
    ASSERT_NE(nullptr, other_floor);
    landmark->setxy(GRID_SIZE * landmark_gx, GRID_SIZE * landmark_gy);
    landmark->set_floor(0);
    landmark->set_team_num(1);
    landmark->set_invisibility_left(120);
    other_floor->setxy(GRID_SIZE * other_floor_gx,
                       GRID_SIZE * landmark_gy);
    other_floor->set_floor(1);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    const Sint32 saved_override = vs->editor_floor_override_;
    vs->control = nullptr; // radar team 0 opposes the invisible landmark
    vs->radarstart = 1;
    vs->editor_floor_override_ = 0;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);
    ASSERT_GT(r.xview, 1);
    ASSERT_GT(r.yview, landmark_gy + 1);
    ASSERT_LT(r.xloc + r.xview,
              og::runtime::current_session->myscreen_->canvas_w());
    const int outside_gx = r.radarx - 1;
    walker* outside = d.add_fx_ob(Order::FX, shadow_family);
    ASSERT_NE(nullptr, outside);
    // The renderer maps `(xpos + 1) / GRID_SIZE`; -9, not -8, is grid -1
    // under C++ truncation toward zero.
    outside->setxy(GRID_SIZE * outside_gx - 1, GRID_SIZE * landmark_gy);
    outside->set_floor(0);

    EXPECT_EQ(3u, d.world().fxlist.size());
    EXPECT_EQ(0u, d.world().oblist.size());
    for (const og::sim::ModeBeacon& beacon : d.world().mode.beacons)
        EXPECT_EQ(0, beacon.entity_id);

    counter.calls = 0;
    ASSERT_EQ(1, r.draw(&d));
    const std::array<int, 3> background = blip_rgb_at(r, 30, 20);
    const std::array<int, 3> center =
        blip_rgb_at(r, landmark_gx, landmark_gy);
    std::array<std::array<int, 3>, 4> expected_colors{};
    for (std::size_t phase = 0; phase < expected_colors.size(); ++phase)
        expected_colors[phase] =
            palette_rgb(shadow_d->radar.color + static_cast<int>(phase));
    const auto phase =
        std::find(expected_colors.begin(), expected_colors.end(), center);
    ASSERT_NE(expected_colors.end(), phase);

    int painted = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const std::array<int, 3> pixel =
                blip_rgb_at(r, landmark_gx + dx, landmark_gy + dy);
            if (pixel != background)
            {
                painted++;
                EXPECT_EQ(center, pixel);
            }
        }
    const int expected_painted =
        std::distance(expected_colors.begin(), phase) < 2 ? 5 : 9;
    EXPECT_EQ(expected_painted, painted);
    EXPECT_EQ(background, blip_rgb_at(r, other_floor_gx, landmark_gy));
    EXPECT_EQ(background, blip_rgb_at(r, outside_gx + 1, landmark_gy));
    EXPECT_EQ(0, counter.calls) << "jitter 0 must not touch the game RNG";

    // Jitter is also resolved by the direct descriptor path. Move the
    // off-screen control to the filtered floor so exactly one eligible FX
    // consumes exactly one draw.
    outside->set_floor(1);
    const EffectFamilyDescriptor saved_shadow = *shadow_d;
    {
        ScopedEffectDescriptorRestore restore(shadow_family, saved_shadow);
        EffectFamilyDescriptor jittered_shadow = saved_shadow;
        jittered_shadow.radar.jitter = 1;
        ASSERT_TRUE(
            set_effect_family_descriptor(shadow_family, jittered_shadow));
        counter.calls = 0;
        counter.last_max = 0;
        ASSERT_EQ(1, r.draw(&d));
        EXPECT_EQ(1, counter.calls);
        EXPECT_EQ(1u, counter.last_max);
    }

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
    vs->editor_floor_override_ = saved_override;
}

// A pulse centered on any radar edge still visits all eight neighboring
// offsets. The viewport's right/bottom endpoints are exclusive, just like
// radar::on_screen; none of those neighbors may bleed into the one-pixel
// perimeter outside the minimap. Four corner landmarks exercise all edges at
// once, and sixteen draws prove the clip under every pulse size/colour phase.
TEST_F(RadarMore, ping_landmarks_clip_to_all_four_radar_edges)
{
    og::test::ScopedCampaignMountState mount_restore;
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    const int soccer_family =
        og::families::resolve_family_string_id(Order::FX, "modes:ball");
    ASSERT_GE(soccer_family, 0);
    const EffectFamilyDescriptor* soccer_d =
        get_effect_family_descriptor(soccer_family);
    ASSERT_NE(nullptr, soccer_d);
    ASSERT_TRUE(soccer_d->radar.landmark);
    ASSERT_TRUE(soccer_d->radar.ping);
    ASSERT_EQ(COLOR_WHITE, soccer_d->radar.color);
    ASSERT_EQ(0, soccer_d->radar.jitter);

    CountingRandom counter;
    GameContext c;
    c.rng = &counter;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; ++y)
        for (int x = 0; x < d.world().grid.w; ++x)
            set_tile(d, x, y, PIX_COBBLE_1);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = nullptr;
    vs->radarstart = 1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);
    ASSERT_GT(r.xview, 1);
    ASSERT_GT(r.yview, 1);
    ASSERT_GT(r.xloc, 0);
    ASSERT_GT(r.yloc, 0);
    ASSERT_LT(r.xloc + r.xview,
              og::runtime::current_session->myscreen_->canvas_w());
    ASSERT_LT(r.yloc + r.yview,
              og::runtime::current_session->myscreen_->canvas_h());

    const std::array<std::array<int, 2>, 4> corners = {{
        {0, 0},
        {r.xview - 1, 0},
        {0, r.yview - 1},
        {r.xview - 1, r.yview - 1},
    }};
    for (const auto& corner : corners)
    {
        walker* ball = d.add_ob(Order::FX, soccer_family);
        ASSERT_NE(nullptr, ball);
        ball->setxy(static_cast<short>(corner[0] * GRID_SIZE),
                    static_cast<short>(corner[1] * GRID_SIZE));
    }

    std::array<std::array<int, 3>, 4> expected_colors{};
    for (std::size_t phase = 0; phase < expected_colors.size(); ++phase)
        expected_colors[phase] =
            palette_rgb(soccer_d->radar.color + static_cast<int>(phase));
    const std::array<int, 3> black = palette_rgb(PURE_BLACK);
    std::array<int, 4> color_frames{};
    counter.calls = 0;
    for (int frame = 0; frame < 16; ++frame)
    {
        // Re-arm the complete outside ring. The terrain blit is clipped to
        // [0,xview) x [0,yview), so only an escaping pulse can alter it.
        for (int gx = -1; gx <= r.xview; ++gx)
        {
            og::runtime::current_session->myscreen_->pointb(
                r.xloc + gx, r.yloc - 1, PURE_BLACK);
            og::runtime::current_session->myscreen_->pointb(
                r.xloc + gx, r.yloc + r.yview, PURE_BLACK);
        }
        for (int gy = 0; gy < r.yview; ++gy)
        {
            og::runtime::current_session->myscreen_->pointb(
                r.xloc - 1, r.yloc + gy, PURE_BLACK);
            og::runtime::current_session->myscreen_->pointb(
                r.xloc + r.xview, r.yloc + gy, PURE_BLACK);
        }

        ASSERT_EQ(1, r.draw(&d));
        const std::array<int, 3> center =
            blip_rgb_at(r, corners[0][0], corners[0][1]);
        const auto phase =
            std::find(expected_colors.begin(), expected_colors.end(), center);
        ASSERT_NE(expected_colors.end(), phase) << "frame " << frame;
        color_frames[static_cast<std::size_t>(
            std::distance(expected_colors.begin(), phase))]++;
        for (const auto& corner : corners)
            EXPECT_EQ(center, blip_rgb_at(r, corner[0], corner[1]))
                << "frame " << frame << " corner (" << corner[0] << ", "
                << corner[1] << ")";

        for (int gx = -1; gx <= r.xview; ++gx)
        {
            EXPECT_EQ(black, blip_rgb_at(r, gx, -1))
                << "frame " << frame << " top outside x=" << gx;
            EXPECT_EQ(black, blip_rgb_at(r, gx, r.yview))
                << "frame " << frame << " bottom outside x=" << gx;
        }
        for (int gy = 0; gy < r.yview; ++gy)
        {
            EXPECT_EQ(black, blip_rgb_at(r, -1, gy))
                << "frame " << frame << " left outside y=" << gy;
            EXPECT_EQ(black, blip_rgb_at(r, r.xview, gy))
                << "frame " << frame << " right outside y=" << gy;
        }
    }

    EXPECT_EQ(0, counter.calls) << "jitter 0 must not touch the game RNG";
    for (int count : color_frames)
        EXPECT_EQ(4, count);

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
}

// Soccer publishes the SAME acting ball through both presentation channels:
// an oblist FX landmark and scripted beacon 0. The landmark is public even
// while invisible to this opposing radar; the beacon must not overwrite its
// descriptor-coloured pulse with a second centre dot.
TEST_F(RadarMore, active_soccer_beacon_deduplicates_its_public_ball_landmark)
{
    og::test::ScopedCampaignMountState mount_restore;
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    const int soccer_family =
        og::families::resolve_family_string_id(Order::FX, "modes:ball");
    const int quiet_family =
        og::families::resolve_family_string_id(Order::FX, "modes:bball");
    ASSERT_GE(soccer_family, 0);
    ASSERT_GE(quiet_family, 0);
    const EffectFamilyDescriptor* soccer_d =
        get_effect_family_descriptor(soccer_family);
    ASSERT_NE(nullptr, soccer_d);
    ASSERT_TRUE(soccer_d->radar.landmark);
    ASSERT_TRUE(soccer_d->radar.ping);
    ASSERT_EQ(COLOR_WHITE, soccer_d->radar.color);
    ASSERT_EQ(0, soccer_d->radar.jitter);

    CountingRandom counter;
    GameContext c;
    c.rng = &counter;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; ++y)
        for (int x = 0; x < d.world().grid.w; ++x)
            set_tile(d, x, y, PIX_COBBLE_1);

    walker* ball = d.add_ob(Order::FX, soccer_family);
    walker* quiet = d.add_ob(Order::FX, quiet_family);
    ASSERT_NE(nullptr, ball);
    ASSERT_NE(nullptr, quiet);
    constexpr int ball_gx = 10;
    constexpr int ball_gy = 10;
    constexpr int quiet_gx = 20;
    constexpr int quiet_gy = 10;
    ball->setxy(GRID_SIZE * ball_gx, GRID_SIZE * ball_gy);
    ball->set_team_num(1);
    ball->set_invisibility_left(120);
    quiet->setxy(GRID_SIZE * quiet_gx, GRID_SIZE * quiet_gy);

    d.world().type |= GameWorld::TYPE_SCRIPTED;
    d.world().mode.active = true;
    d.world().mode.beacons[0].entity_id =
        static_cast<std::int32_t>(ball->entity_id());
    // Omitted Lua team argument: the legacy beacon path would use the
    // invisible ball's team colour and make the duplicate centre visible.
    d.world().mode.beacons[0].team = 255;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = nullptr; // default radar team 0 opposes the invisible ball
    vs->radarstart = 1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);
    counter.calls = 0;
    ASSERT_EQ(1, r.draw(&d));
    const std::array<int, 3> background = blip_rgb_at(r, 30, 20);
    const std::array<int, 3> center = blip_rgb_at(r, ball_gx, ball_gy);
    std::array<std::array<int, 3>, 4> expected_colors{};
    for (std::size_t phase = 0; phase < expected_colors.size(); ++phase)
        expected_colors[phase] =
            palette_rgb(soccer_d->radar.color + static_cast<int>(phase));
    const auto phase =
        std::find(expected_colors.begin(), expected_colors.end(), center);
    ASSERT_NE(expected_colors.end(), phase);

    int painted = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const std::array<int, 3> pixel =
                blip_rgb_at(r, ball_gx + dx, ball_gy + dy);
            if (pixel != background)
            {
                painted++;
                EXPECT_EQ(center, pixel)
                    << "a duplicate beacon must not recolour the centre";
            }
        }
    const int expected_painted =
        std::distance(expected_colors.begin(), phase) < 2 ? 5 : 9;
    EXPECT_EQ(expected_painted, painted)
        << "one descriptor pulse must paint its exact phase shape";
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            EXPECT_EQ(background,
                      blip_rgb_at(r, quiet_gx + dx, quiet_gy + dy));
    EXPECT_EQ(0, counter.calls) << "jitter 0 must not touch the game RNG";

    // Mutation tooth for the dedupe itself: two identical jitter-0 pulses
    // leave the same final pixels and consume no RNG, so that production
    // contract alone cannot distinguish one paint from two. Temporarily give
    // this descriptor a one-value jitter span. The oblist landmark must make
    // exactly one draw; removing beacon_target_drawn makes the matching beacon
    // draw again and raises this count to two. The scoped guard restores the
    // mounted registry even when an ASSERT aborts this block.
    const EffectFamilyDescriptor saved_soccer = *soccer_d;
    {
        ScopedEffectDescriptorRestore restore(soccer_family, saved_soccer);
        EffectFamilyDescriptor jittered_soccer = saved_soccer;
        jittered_soccer.radar.jitter = 1;
        ASSERT_TRUE(
            set_effect_family_descriptor(soccer_family, jittered_soccer));
        counter.calls = 0;
        counter.last_max = 0;
        ASSERT_EQ(1, r.draw(&d));
        EXPECT_EQ(1, counter.calls)
            << "the exact scripted beacon target must not repaint its "
               "oblist landmark";
        EXPECT_EQ(1u, counter.last_max);
    }
    const EffectFamilyDescriptor* restored_soccer =
        get_effect_family_descriptor(soccer_family);
    ASSERT_NE(nullptr, restored_soccer);
    EXPECT_EQ(saved_soccer.radar.jitter, restored_soccer->radar.jitter);
    EXPECT_EQ(saved_soccer.radar.color, restored_soccer->radar.color);
    EXPECT_EQ(saved_soccer.radar.landmark, restored_soccer->radar.landmark);
    EXPECT_EQ(saved_soccer.radar.ping, restored_soccer->radar.ping);

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
}

// Basketball's drawn entity rises by fake z, while beacon 0 intentionally
// follows its fxlist shadow at the true ground spot. At a 40-pixel apex the
// radar must show exactly one yellow descriptor pulse at the shadow and
// leave the lifted, non-landmark ball position untouched.
TEST_F(RadarMore, active_basketball_beacon_pulses_only_at_the_ground_shadow)
{
    og::test::ScopedCampaignMountState mount_restore;
    restore_default_campaigns();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    const int basketball_family =
        og::families::resolve_family_string_id(Order::FX, "modes:bball");
    const int shadow_family =
        og::families::resolve_family_string_id(Order::FX, "modes:bshadow");
    ASSERT_GE(basketball_family, 0);
    ASSERT_GE(shadow_family, 0);
    const EffectFamilyDescriptor* basketball_d =
        get_effect_family_descriptor(basketball_family);
    const EffectFamilyDescriptor* shadow_d =
        get_effect_family_descriptor(shadow_family);
    ASSERT_NE(nullptr, basketball_d);
    ASSERT_NE(nullptr, shadow_d);
    ASSERT_FALSE(basketball_d->radar.landmark);
    ASSERT_FALSE(basketball_d->radar.ping);
    ASSERT_TRUE(shadow_d->radar.landmark);
    ASSERT_TRUE(shadow_d->radar.ping);
    ASSERT_EQ(COLOR_YELLOW, shadow_d->radar.color);
    ASSERT_EQ(0, shadow_d->radar.jitter);

    CountingRandom counter;
    GameContext c;
    c.rng = &counter;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; ++y)
        for (int x = 0; x < d.world().grid.w; ++x)
            set_tile(d, x, y, PIX_COBBLE_1);

    walker* ball = d.add_ob(Order::FX, basketball_family);
    walker* shadow = d.add_fx_ob(Order::FX, shadow_family);
    ASSERT_NE(nullptr, ball);
    ASSERT_NE(nullptr, shadow);
    // These are the real sync_render offsets for ground center (214,214)
    // and z=40: both 12x12 sprites subtract their 6-pixel half-size.
    ball->setxy(208, 168);
    shadow->setxy(208, 208);
    const int lifted_gx = (ball->xpos() + 1) / GRID_SIZE;
    const int lifted_gy = (ball->ypos() + 1) / GRID_SIZE;
    const int ground_gx = (shadow->xpos() + 1) / GRID_SIZE;
    const int ground_gy = (shadow->ypos() + 1) / GRID_SIZE;
    ASSERT_GE(std::abs(ground_gy - lifted_gy), 3)
        << "the pulse neighborhoods must not overlap";

    d.world().type |= GameWorld::TYPE_SCRIPTED;
    d.world().mode.active = true;
    d.world().mode.beacons[0].entity_id =
        static_cast<std::int32_t>(shadow->entity_id());
    d.world().mode.beacons[0].team = 255;

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = nullptr;
    vs->radarstart = 1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);
    counter.calls = 0;
    ASSERT_EQ(1, r.draw(&d));
    const std::array<int, 3> background = blip_rgb_at(r, 30, 20);
    const std::array<int, 3> center = blip_rgb_at(r, ground_gx, ground_gy);
    std::array<std::array<int, 3>, 4> expected_colors{};
    for (std::size_t phase = 0; phase < expected_colors.size(); ++phase)
        expected_colors[phase] =
            palette_rgb(shadow_d->radar.color + static_cast<int>(phase));
    const auto phase =
        std::find(expected_colors.begin(), expected_colors.end(), center);
    ASSERT_NE(expected_colors.end(), phase);

    int painted = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const std::array<int, 3> pixel =
                blip_rgb_at(r, ground_gx + dx, ground_gy + dy);
            if (pixel != background)
            {
                painted++;
                EXPECT_EQ(center, pixel);
            }
        }
    const int expected_painted =
        std::distance(expected_colors.begin(), phase) < 2 ? 5 : 9;
    EXPECT_EQ(expected_painted, painted)
        << "the ground proxy must paint its exact phase shape";
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            EXPECT_EQ(background,
                      blip_rgb_at(r, lifted_gx + dx, lifted_gy + dy))
                << "the fake-z ball itself is quiet on radar";
    EXPECT_EQ(0, counter.calls) << "jitter 0 must not touch the game RNG";

    // The shadow now publishes through both the fxlist descriptor and beacon
    // channels. A one-value jitter span makes duplicate painting observable:
    // the descriptor must roll once, then reserve its matching beacon slot.
    const EffectFamilyDescriptor saved_shadow = *shadow_d;
    {
        ScopedEffectDescriptorRestore restore(shadow_family, saved_shadow);
        EffectFamilyDescriptor jittered_shadow = saved_shadow;
        jittered_shadow.radar.jitter = 1;
        ASSERT_TRUE(
            set_effect_family_descriptor(shadow_family, jittered_shadow));
        counter.calls = 0;
        counter.last_max = 0;
        ASSERT_EQ(1, r.draw(&d));
        EXPECT_EQ(1, counter.calls)
            << "the beacon must not repaint its fxlist landmark";
        EXPECT_EQ(1u, counter.last_max);
    }

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
}

TEST_F(RadarMore, treasure_blips_and_rng_draws_come_from_the_descriptor)
{
    CountingRandom counter;
    GameContext c;
    c.rng = &counter;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; y++)
        for (int x = 0; x < d.world().grid.w; x++)
            set_tile(d, x, y, PIX_SNOW1); // bakes COLOR_WHITE everywhere

    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
    control->set_team_num(0);
    control->set_view_all(1); // treasure sight: loot blips are gated on this

    struct Placed { walker* ob; int gx; int gy; };
    auto place = [&](std::int32_t family, int gx, int gy) {
        walker* ob = d.add_fx_ob(Order::Treasure, family);
        EXPECT_NE(nullptr, ob) << "family " << family;
        if (ob != nullptr) {
            ob->setxy(GRID_SIZE * gx, GRID_SIZE * gy);
            ob->set_dead(0);
        }
        return Placed{ob, gx, gy};
    };
    const Placed gold = place(FAMILY_GOLD_BAR, 6, 6);
    const Placed exit_ob = place(FAMILY_EXIT, 8, 8);
    const Placed gem = place(FAMILY_LIFE_GEM, 10, 10);
    ASSERT_NE(nullptr, gold.ob);
    ASSERT_NE(nullptr, exit_ob.ob);
    ASSERT_NE(nullptr, gem.ob);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = control;
    vs->radarstart = 1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);
    ASSERT_EQ(0, r.radarx);
    ASSERT_EQ(0, r.radary);

    // --- with treasure sight ------------------------------------------
    counter.calls = 0;
    ASSERT_EQ(1, r.draw(&d));
    EXPECT_EQ(3, counter.calls)
        << "one flicker roll for the followed control, one for the gold bar "
           "(jitter 5), one for the exit (jitter 7) — the life gem has no "
           "colour and must roll nothing";
    EXPECT_EQ(COLOR_YELLOW, blip_index_at(r, gold.gx, gold.gy))
        << "gold bar blips at its descriptor colour";
    EXPECT_EQ(COLOR_CYAN, blip_index_at(r, exit_ob.gx, exit_ob.gy))
        << "exit blips at its descriptor colour";
    // The baked-terrain reference: an empty cell of the same snow fill. (The
    // palette holds several identical whites, so compare against what the
    // radar actually painted rather than the COLOR_WHITE index.)
    const int bare_terrain = blip_index_at(r, 20, 20);
    EXPECT_EQ(bare_terrain, blip_index_at(r, gem.gx, gem.gy))
        << "a colourless family leaves the baked terrain alone";

    // --- without treasure sight ---------------------------------------
    control->set_view_all(0);
    counter.calls = 0;
    ASSERT_EQ(1, r.draw(&d));
    EXPECT_EQ(2, counter.calls)
        << "the loot roll disappears with treasure sight; the exit still "
           "flickers because navigation markers ignore view_all";
    EXPECT_EQ(bare_terrain, blip_index_at(r, gold.gx, gold.gy))
        << "loot needs treasure sight";
    EXPECT_EQ(COLOR_CYAN, blip_index_at(r, exit_ob.gx, exit_ob.gy))
        << "the exit is a landmark every player sees";

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;
}

// Patch a descriptor the way a class-pack install does and the blip follows:
// the engine has no idea the speed potion "should" light up the minimap — the
// core pack simply ships it colourless.
TEST_F(RadarMore, a_pack_can_give_any_treasure_family_a_radar_blip)
{
    const TreasureFamilyDescriptor* original =
        get_treasure_family_descriptor(FAMILY_SPEED_POTION);
    ASSERT_NE(nullptr, original);
    const TreasureFamilyDescriptor saved = *original;
    ASSERT_EQ(og::kRadarColorNone, saved.radar.color)
        << "the core speed potion draws no blip";

    CountingRandom counter;
    GameContext c;
    c.rng = &counter;
    GlobalContextGuard guard(&c);

    LevelRuntimeData d(1);
    d.create_new_grid();
    for (int y = 0; y < d.world().grid.h; y++)
        for (int x = 0; x < d.world().grid.w; x++)
            set_tile(d, x, y, PIX_SNOW1);

    walker* control = d.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
    control->set_view_all(1);
    walker* potion = d.add_fx_ob(Order::Treasure, FAMILY_SPEED_POTION);
    ASSERT_NE(nullptr, potion);
    potion->setxy(GRID_SIZE * 7, GRID_SIZE * 7);
    potion->set_dead(0);

    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    walker* saved_control = vs->control;
    const short saved_radarstart = vs->radarstart;
    vs->control = control;
    vs->radarstart = 1;

    radar r(vs, og::runtime::current_session->myscreen_, 0);
    r.start(&d);

    counter.calls = 0;
    ASSERT_EQ(1, r.draw(&d));
    const int unblipped = blip_index_at(r, 7, 7);
    const int bare_terrain = blip_index_at(r, 20, 20);
    const int calls_before = counter.calls;

    TreasureFamilyDescriptor patched = saved;
    patched.radar = og::RadarBlip{COLOR_PURPLE, 0}; // static: rolls nothing
    ASSERT_TRUE(set_treasure_family_descriptor(FAMILY_SPEED_POTION, patched));
    counter.calls = 0;
    ASSERT_EQ(1, r.draw(&d));
    const int blipped = blip_index_at(r, 7, 7);
    const int calls_after = counter.calls;
    ASSERT_TRUE(set_treasure_family_descriptor(FAMILY_SPEED_POTION, saved));

    vs->control = saved_control;
    vs->radarstart = saved_radarstart;

    EXPECT_EQ(bare_terrain, unblipped) << "core speed potion: no blip";
    EXPECT_EQ(COLOR_PURPLE, blipped) << "the pack's colour, straight through";
    EXPECT_EQ(calls_before, calls_after)
        << "jitter 0 must not add an rng draw";
}
