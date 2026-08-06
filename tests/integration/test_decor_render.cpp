/* Render-pass tests for BASE+DECOR tile layering: the decor pass runs right
 * after draw_tile in BOTH viewscreen::redraw bodies through the TRANSPARENT
 * sprite path (index-0 pixels leave the base tile visible), ghosted upper
 * floors composite decor with their floor, the direct alpha path blends, the
 * radar bakes decor override colors, and concealing decor (SHRUB) suppresses
 * the forestwalk walker sprite exactly like TYPE_TREES.
 *
 * Camera-dependent probes settle the camera with one redraw BEFORE reading
 * topx/topy (the FX test trap), and every test restores world/cfg/control
 * state so the suite survives --gtest_shuffle.
 */
#include <openglad/core/colors.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/input.h>
#include <openglad/interface/level_render.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/interface/render/radar.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/game_context.h>
#include <openglad/resources/gparser.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <format>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

extern cfg_store cfg;

namespace
{

screen* scr()
{
    return og::runtime::current_session->myscreen_;
}

viewscreen* view0()
{
    return scr()->viewob[0].get();
}

void prepare_world()
{
    GameWorld& world = scr()->world();
    world.create_new_grid();
    world.delete_objects();
    world.mysmoother.set_target(world.grid);
}

void restore_world(viewscreen* vs)
{
    vs->control = nullptr;
    GameWorld& world = scr()->world();
    world.delete_objects();
    world.set_floor_count(1);
    world.decor.free();
}

void fill_base(GameWorld& world, unsigned char tile)
{
    const std::size_t n =
        static_cast<std::size_t>(world.grid.w) * world.grid.h;
    std::fill(world.grid.data.get(), world.grid.data.get() + n, tile);
}

void set_base(GameWorld& world, int f, int x, int y, unsigned char tile)
{
    PixieData& g = world.grid_for_floor(f);
    g.data[static_cast<std::size_t>(y) * g.w + static_cast<std::size_t>(x)] = tile;
}

// Give floor f an all-`tile` grid matching the base extents (multifloor).
void fill_floor_grid(GameWorld& world, int f, unsigned char tile)
{
    const int gw = world.grid.w;
    const int gh = world.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), tile);
    world.grid_for_floor(f) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
    world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
}

// Allocate an all-DECOR_NONE decor plane for floor f (grid dims).
void give_decor_plane(GameWorld& world, int f)
{
    const PixieData& g = world.grid_for_floor(f);
    auto* buf = new unsigned char[static_cast<std::size_t>(g.w) * g.h]();
    world.decor_for_floor(f) = PixieData(1, g.w, g.h, buf);
}

void set_decor(GameWorld& world, int f, int x, int y, unsigned char id)
{
    PixieData& d = world.decor_for_floor(f);
    d.data[static_cast<std::size_t>(y) * d.w + static_cast<std::size_t>(x)] = id;
}

struct RGB
{
    Uint8 r = 0, g = 0, b = 0;
};

RGB px(int x, int y)
{
    RGB c;
    scr()->get_pixel(x, y, &c.r, &c.g, &c.b);
    return c;
}

bool same(const RGB& a, const RGB& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

std::vector<RGB> grab_rect(int x0, int y0, int w, int h)
{
    std::vector<RGB> out;
    out.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            out.push_back(px(x0 + i, y0 + j));
    return out;
}

int count_diff(const std::vector<RGB>& a, const std::vector<RGB>& b)
{
    int n = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i++)
        if (!same(a[i], b[i]))
            n++;
    return n;
}

// Screen-space top-left corner of grid cell (gx, gy) for this view.
void cell_origin(const viewscreen* vs, int gx, int gy, int& sx, int& sy)
{
    sx = gx * GRID_SIZE - vs->topx + vs->xloc;
    sy = gy * GRID_SIZE - vs->topy + vs->yloc;
}

bool do_redraw_data(viewscreen* vs)
{
    return vs->redraw(&scr()->level_runtime_data(), false);
}

// Simulate the look-up hold (the only way to see floors ABOVE the camera;
// floors below render depth-faded regardless): bind P1's KEY_LOOKUP to 'v'
// and swap the session's SDL keystate pointer for a writable fake with that
// key down — redraw() recomputes ghost_hold_override_ from the real key
// state every frame. Same shape as test_render_effects' guards.
struct LookUpHoldGuard
{
    const bool* old_keystates;
    int old_binding;
    std::array<bool, SDL_SCANCODE_COUNT> fake{};

    LookUpHoldGuard()
        : old_keystates(og::runtime::current_session->keystates_)
        , old_binding(og::runtime::current_session->player_keys_[0][KEY_LOOKUP])
    {
        og::runtime::current_session->player_keys_[0][KEY_LOOKUP] = KEYCODE_v;
        og::runtime::current_session->keystates_ = fake.data();
        set(true);
    }
    ~LookUpHoldGuard()
    {
        og::runtime::current_session->keystates_ = old_keystates;
        og::runtime::current_session->player_keys_[0][KEY_LOOKUP] = old_binding;
    }
    LookUpHoldGuard(const LookUpHoldGuard&) = delete;
    LookUpHoldGuard& operator=(const LookUpHoldGuard&) = delete;

    void set(bool held)
    {
        const SDL_Scancode sc = SDL_GetScancodeFromKey(SDLK_V, nullptr);
        if (sc >= 0 && sc < SDL_SCANCODE_COUNT)
            fake[static_cast<std::size_t>(sc)] = held;
    }
};

struct GlobalContextGuard
{
    explicit GlobalContextGuard(GameContext* c) { push_test_context(c); }
    ~GlobalContextGuard() { pop_test_context(); }
    GlobalContextGuard(const GlobalContextGuard&) = delete;
    GlobalContextGuard& operator=(const GlobalContextGuard&) = delete;
};

// Pixel probes must not see the FX passes (shadows/weather/... darken or
// overlay the probed cells, and earlier tests in this binary flip these keys
// through their own guards). Save each key, force it off, restore on exit;
// missing keys restore to their production defaults (same convention as
// test_render_effects.cpp's EffectsCfgGuard).
class EffectsOffGuard
{
public:
    EffectsOffGuard()
    {
        for (const auto& [key, fallback] : kKeys)
        {
            (void)fallback;
            saved_.emplace_back(key, cfg.get_setting("effects", key));
            cfg.apply_setting("effects", key, "off");
        }
    }
    ~EffectsOffGuard()
    {
        for (std::size_t i = 0; i < saved_.size(); i++)
            cfg.apply_setting("effects", saved_[i].first,
                              saved_[i].second.empty() ? kKeys[i].second
                                                       : saved_[i].second);
    }
    EffectsOffGuard(const EffectsOffGuard&) = delete;
    EffectsOffGuard& operator=(const EffectsOffGuard&) = delete;

private:
    static constexpr std::pair<const char*, const char*> kKeys[] = {
        {"shadows", "on"},      {"reflections", "on"}, {"weather", "on"},
        {"attack_lunge", "on"}, {"hit_recoil", "off"}, {"ripples", "on"},
        {"trails", "on"},       {"dust", "on"},        {"fire_glow", "on"},
        {"depth_fx", "fog"},    {"screen_shake", "on"},
    };
    std::vector<std::pair<std::string, std::string>> saved_;
};

class DecorRender : public testing::Test
{
protected:
    void SetUp() override
    {
        scr()->set_world_canvas_pinned_classic(true);
        scr()->relayout_views();
    }

    void TearDown() override
    {
        scr()->set_active_canvas(CanvasTarget::UI);
        scr()->set_world_canvas_pinned_classic(false);
        scr()->relayout_views();
    }
};

} // namespace

// The decor cell (base GRASS3 + DECOR_BOULDER_2) must render byte-identical
// to the legacy combined tile (base PIX_BOULDER_2): that cut-out measured
// ZERO residual pixels, so the transparent sprite pass over the base tile is
// pixel-for-pixel the legacy look. Checked in BOTH redraw bodies.
TEST_F(DecorRender, boulder_decor_matches_legacy_combined_tile_in_both_redraws)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsOffGuard effects_off;
    GameWorld& world = scr()->world();
    fill_base(world, PIX_GRASS3);

    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    // Settle the camera before deriving any screen coordinate.
    ASSERT_TRUE(do_redraw_data(vs));
    const int cx = (vs->topx + vs->xview / 2) / GRID_SIZE;
    const int cy = (vs->topy + vs->yview / 2) / GRID_SIZE;
    const int decor_gx = cx + 2, legacy_gx = cx + 2;
    const int decor_gy = cy - 2, legacy_gy = cy - 1;

    set_base(world, 0, legacy_gx, legacy_gy, PIX_BOULDER_2); // legacy tile
    give_decor_plane(world, 0);
    set_decor(world, 0, decor_gx, decor_gy, DECOR_BOULDER_2);

    int dx = 0, dy = 0, lx = 0, ly = 0;
    for (const bool use_data_redraw : {true, false})
    {
        if (use_data_redraw)
            ASSERT_TRUE(do_redraw_data(vs));
        else
            ASSERT_TRUE(vs->redraw());
        cell_origin(vs, decor_gx, decor_gy, dx, dy);
        cell_origin(vs, legacy_gx, legacy_gy, lx, ly);
        const std::vector<RGB> decor_rect =
            grab_rect(dx, dy, GRID_SIZE, GRID_SIZE);
        const std::vector<RGB> legacy_rect =
            grab_rect(lx, ly, GRID_SIZE, GRID_SIZE);
        ASSERT_EQ(0, count_diff(decor_rect, legacy_rect))
            << (use_data_redraw ? "redraw(data)" : "redraw()")
            << ": base+decor must reproduce the legacy combined tile";
    }

    // Transparency: erasing the decor byte restores the pure base tile, and
    // the changed pixels are exactly the sprite's opaque cut-out pixels.
    const std::vector<RGB> with_decor = grab_rect(dx, dy, GRID_SIZE, GRID_SIZE);
    set_decor(world, 0, decor_gx, decor_gy, DECOR_NONE);
    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> without = grab_rect(dx, dy, GRID_SIZE, GRID_SIZE);
    const int changed = count_diff(with_decor, without);
    EXPECT_GT(changed, 0) << "boulder sprite must draw";
    EXPECT_LT(changed, GRID_SIZE * GRID_SIZE)
        << "index-0 pixels must stay transparent (base visible through decor)";

    restore_world(vs);
}

// Ghosted multifloor: under the look-up hold, decor on the floor above the
// camera composites through the floor layer (visible ghost); with the hold
// released that floor is not drawn at all.
TEST_F(DecorRender, upper_floor_decor_ghosts_under_look_up_hold)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsOffGuard effects_off;
    GameWorld& world = scr()->world();
    fill_base(world, PIX_GRASS1);
    world.set_floor_count(2);
    fill_floor_grid(world, 1, PIX_AIR); // empty above, except one plank cell

    LookUpHoldGuard hold; // ghost path for the captures below

    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w; // camera floor 0

    ASSERT_TRUE(do_redraw_data(vs));
    const int gx = (vs->topx + vs->xview / 2) / GRID_SIZE + 3;
    const int gy = (vs->topy + vs->yview / 2) / GRID_SIZE - 2;
    set_base(world, 1, gx, gy, PIX_FLOOR1);

    // Baseline: plank cell above, no decor plane.
    ASSERT_TRUE(do_redraw_data(vs));
    int sx = 0, sy = 0;
    cell_origin(vs, gx, gy, sx, sy);
    const std::vector<RGB> no_decor = grab_rect(sx, sy, GRID_SIZE, GRID_SIZE);

    give_decor_plane(world, 1);
    set_decor(world, 1, gx, gy, DECOR_BRAZIER);
    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> ghosted = grab_rect(sx, sy, GRID_SIZE, GRID_SIZE);
    EXPECT_GT(count_diff(ghosted, no_decor), 0)
        << "ghosted upper-floor decor must show through the floor layer";

    hold.set(false); // release: the default (no-ghost) presentation
    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> unghosted = grab_rect(sx, sy, GRID_SIZE, GRID_SIZE);
    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> unghosted2 = grab_rect(sx, sy, GRID_SIZE, GRID_SIZE);
    EXPECT_EQ(0, count_diff(unghosted, unghosted2))
        << "with the hold released the cell must be stable frame to frame";
    EXPECT_GT(count_diff(unghosted, ghosted), 0)
        << "the released frame must not draw the floor-above decor";

    restore_world(vs);
}

// LevelRender::draw_decor guards + the direct alpha path: DECOR_NONE and
// out-of-range ids draw nothing; alpha<255 blends (differs from both the
// untouched frame and the opaque draw).
TEST_F(DecorRender, draw_decor_guards_and_alpha_path)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsOffGuard effects_off;
    GameWorld& world = scr()->world();
    fill_base(world, PIX_GRASS1);
    LevelRender* renderer = scr()->level_visuals_.renderer_.get();
    ASSERT_NE(nullptr, renderer);

    vs->control = nullptr;
    scr()->level_visuals_.topx = 0;
    scr()->level_visuals_.topy = 0;
    ASSERT_TRUE(do_redraw_data(vs)); // settle

    const int gx = (vs->topx + vs->xview / 2) / GRID_SIZE;
    const int gy = (vs->topy + vs->yview / 2) / GRID_SIZE;
    int sx = 0, sy = 0;
    cell_origin(vs, gx, gy, sx, sy);
    const int wx = gx * GRID_SIZE, wy = gy * GRID_SIZE;

    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> base = grab_rect(sx, sy, GRID_SIZE, GRID_SIZE);

    // Guards: none of these may touch a pixel.
    renderer->draw_decor(DECOR_NONE, wx, wy, vs, 255);
    renderer->draw_decor(-1, wx, wy, vs, 255);
    renderer->draw_decor(DECOR_MAX, wx, wy, vs, 255);
    renderer->draw_decor(DECOR_MAX + 40, wx, wy, vs, 128);
    EXPECT_EQ(0, count_diff(grab_rect(sx, sy, GRID_SIZE, GRID_SIZE), base))
        << "guarded draw_decor calls must not draw";

    // Opaque path.
    renderer->draw_decor(DECOR_BOULDER_1, wx, wy, vs, 255);
    const std::vector<RGB> opaque = grab_rect(sx, sy, GRID_SIZE, GRID_SIZE);
    const int opaque_diff = count_diff(opaque, base);
    EXPECT_GT(opaque_diff, 0) << "opaque decor must draw";
    EXPECT_LT(opaque_diff, GRID_SIZE * GRID_SIZE)
        << "opaque decor keeps index-0 transparency";

    // Alpha path: redraw to restore the base, then blend at 128.
    ASSERT_TRUE(do_redraw_data(vs));
    renderer->draw_decor(DECOR_BOULDER_1, wx, wy, vs, 128);
    const std::vector<RGB> blended = grab_rect(sx, sy, GRID_SIZE, GRID_SIZE);
    EXPECT_GT(count_diff(blended, base), 0) << "alpha decor must draw";
    EXPECT_GT(count_diff(blended, opaque), 0)
        << "alpha 128 must differ from the opaque draw (a real blend)";

    restore_world(vs);
}

// Radar terrain bake: decor override colors reproduce the legacy combined
// tiles (torch/brazier fire, boulder wall-grey, pebble randomized green),
// SHRUB gets the trees green, and BONES inherits its base color.
TEST_F(DecorRender, radar_bakes_decor_override_colors)
{
    FixedRandom fixed_rng(1);
    GameContext c;
    c.rng = &fixed_rng;
    GlobalContextGuard guard(&c);

    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsOffGuard effects_off;
    GameWorld& world = scr()->world();
    fill_base(world, PIX_GRASS1);

    set_base(world, 0, 0, 0, PIX_WALLSIDE_C);
    set_base(world, 0, 1, 0, PIX_FLOOR1);
    set_base(world, 0, 2, 0, PIX_GRASS2);
    set_base(world, 0, 3, 0, PIX_GRASS_DARK_1);
    // (4,0) grass + shrub, (5,0) grass + bones, (6,0) plain grass control.

    give_decor_plane(world, 0);
    set_decor(world, 0, 0, 0, DECOR_TORCH1);
    set_decor(world, 0, 1, 0, DECOR_BRAZIER);
    set_decor(world, 0, 2, 0, DECOR_BOULDER_1);
    set_decor(world, 0, 3, 0, DECOR_PEBBLES);
    set_decor(world, 0, 4, 0, DECOR_SHRUB);
    set_decor(world, 0, 5, 0, DECOR_BONES);

    radar r(vs, scr(), 0);
    r.update(&scr()->level_runtime_data());
    const int sizex = r.sizex;
    auto baked = [&](int x, int y) -> int {
        return r.bmp[static_cast<std::size_t>(x + sizex * y)];
    };

    EXPECT_EQ(COLOR_FIRE, baked(0, 0)) << "torch -> fire (legacy combined)";
    EXPECT_EQ(COLOR_FIRE, baked(1, 0)) << "brazier -> fire";
    EXPECT_EQ(24, baked(2, 0)) << "boulder -> wall grey (legacy combined)";
    EXPECT_GE(baked(3, 0), COLOR_GREEN + 3) << "pebbles: randomized green";
    EXPECT_LE(baked(3, 0), COLOR_GREEN + 5) << "pebbles: randomized green";
    EXPECT_EQ(COLOR_TREES, baked(4, 0)) << "shrub -> trees green";
    EXPECT_EQ(baked(6, 0), baked(5, 0))
        << "bones inherit the base tile color";

    restore_world(vs);
}

// Concealing decor (SHRUB) suppresses the forestwalk walker sprite in the
// draw pass exactly like standing in TYPE_TREES: most sprite pixels vanish
// (INVISIBLE_MODE), while the same walker without the shrub draws normally.
// The camera stays anchored on a separate control walker throughout, so
// every probe reads the same screen rect.
TEST_F(DecorRender, shrub_conceals_forestwalk_walker_sprite)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsOffGuard effects_off;
    GameWorld& world = scr()->world();
    fill_base(world, PIX_GRASS1);

    walker* anchor = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, anchor);
    anchor->setxy(160, 120);
    vs->control = anchor;

    walker* elf = world.add_ob(Order::Living, FAMILY_ELF);
    ASSERT_NE(nullptr, elf);
    elf->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    const short elf_x = 160 + 3 * GRID_SIZE, elf_y = 120;
    const short away_x = 160 - 4 * GRID_SIZE,
                away_y = static_cast<short>(120 + 4 * GRID_SIZE);
    elf->setxy(elf_x, elf_y);

    ASSERT_TRUE(do_redraw_data(vs)); // settle camera
    const int gx = elf_x / GRID_SIZE;
    const int gy = elf_y / GRID_SIZE;
    int sx = 0, sy = 0;
    cell_origin(vs, gx, gy, sx, sy);
    // Probe a generous rect covering the sprite wherever it straddles cells.
    const int probe_w = GRID_SIZE * 2, probe_h = GRID_SIZE * 2;

    give_decor_plane(world, 0);
    for (int dy = 0; dy <= 1; dy++)
        for (int dx = 0; dx <= 1; dx++)
            set_decor(world, 0, gx + dx, gy + dy, DECOR_SHRUB);

    // Concealed scene, then the same shrub backdrop with the elf elsewhere.
    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> hidden = grab_rect(sx, sy, probe_w, probe_h);
    elf->setxy(away_x, away_y);
    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> empty_shrub = grab_rect(sx, sy, probe_w, probe_h);
    const int hidden_pixels = count_diff(hidden, empty_shrub);

    // Visible scene: erase the shrub cells, same walker, same camera.
    for (int dy = 0; dy <= 1; dy++)
        for (int dx = 0; dx <= 1; dx++)
            set_decor(world, 0, gx + dx, gy + dy, DECOR_NONE);
    elf->setxy(elf_x, elf_y);
    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> visible = grab_rect(sx, sy, probe_w, probe_h);
    elf->setxy(away_x, away_y);
    ASSERT_TRUE(do_redraw_data(vs));
    const std::vector<RGB> empty_grass = grab_rect(sx, sy, probe_w, probe_h);
    const int visible_pixels = count_diff(visible, empty_grass);

    // The concealed walker draws through INVISIBLE_MODE: the sprite FILL is
    // dithered away (a fill pixel survives with probability 9/1000), leaving
    // only the flat single-color outline halo. So the walker's contribution
    // to the concealed scene must collapse to (nearly) one distinct color,
    // while the visible sprite carries real shading.
    auto changed_colors = [](const std::vector<RGB>& scene,
                             const std::vector<RGB>& backdrop) {
        std::vector<RGB> out;
        for (std::size_t i = 0; i < scene.size(); i++)
            if (!same(scene[i], backdrop[i]))
                out.push_back(scene[i]);
        return out;
    };
    auto distinct_colors = [](const std::vector<RGB>& v) {
        std::vector<RGB> u;
        for (const RGB& c : v)
        {
            bool found = false;
            for (const RGB& d : u)
                if (same(c, d))
                    found = true;
            if (!found)
                u.push_back(c);
        }
        return static_cast<int>(u.size());
    };
    const std::vector<RGB> hidden_px = changed_colors(hidden, empty_shrub);
    const std::vector<RGB> visible_px = changed_colors(visible, empty_grass);

    EXPECT_GT(visible_pixels, 0) << "walker must draw on plain grass";
    EXPECT_LT(hidden_pixels, visible_pixels)
        << "shrub conceal must suppress sprite fill pixels";
    EXPECT_GE(distinct_colors(visible_px), 3)
        << "visible sprite must carry real shading";
    EXPECT_LE(distinct_colors(hidden_px), 2)
        << "concealed walker may contribute only the flat outline halo "
        << "(plus rare dither survivors), got " << distinct_colors(hidden_px)
        << " colors over " << hidden_px.size() << " pixels";

    restore_world(vs);
}

// ---------------------------------------------------------------------------
// Decor-sampler capture for visual review: every decor id on 2+ bases, with
// live palette cycling so the torch/brazier flames animate (their pixels sit
// in the cycled ORANGE band — do_cycle IS the flame animation). Skipped
// unless OG_FX_CAPTURE_DIR is set, so it costs nothing in normal ctest runs.
// Dumps P6 PPM frames to $OG_FX_CAPTURE_DIR/decor_sampler/NNN.ppm; run with
// OG_FX_CAPTURE_DIR=<dir> and --gtest_filter='DecorRender.zz_capture_decor_sampler'.
// ---------------------------------------------------------------------------
TEST_F(DecorRender, zz_capture_decor_sampler)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";

    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsOffGuard effects_off;
    GameWorld& world = scr()->world();
    fill_base(world, PIX_GRASS1);

    walker* control = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(160, 120);
    vs->control = control;
    ASSERT_TRUE(do_redraw_data(vs)); // settle the camera
    const int cx = (vs->topx + vs->xview / 2) / GRID_SIZE;
    const int cy = (vs->topy + vs->yview / 2) / GRID_SIZE;

    give_decor_plane(world, 0);
    struct Cell
    {
        int dx, dy;
        unsigned char base;
        unsigned char decor;
    };
    const Cell cells[] = {
        // torches: brick wallside (implied base), grass, snow
        {-6, -5, PIX_WALLSIDE_C, DECOR_TORCH1},
        {-4, -5, PIX_WALLSIDE_C, DECOR_TORCH2},
        {-2, -5, PIX_WALLSIDE_C, DECOR_TORCH3},
        {0, -5, PIX_GRASS2, DECOR_TORCH1},
        {2, -5, PIX_SNOW1, DECOR_TORCH1},
        // brazier: plank floor (implied base), grass, snow, ash
        {-6, -3, PIX_FLOOR1, DECOR_BRAZIER},
        {-4, -3, PIX_GRASS2, DECOR_BRAZIER},
        {-2, -3, PIX_SNOW1, DECOR_BRAZIER},
        {0, -3, PIX_ASH1, DECOR_BRAZIER},
        // boulders: grass (implied), snow, ash
        {-6, -1, PIX_GRASS2, DECOR_BOULDER_1},
        {-4, -1, PIX_GRASS3, DECOR_BOULDER_2},
        {-2, -1, PIX_GRASS2, DECOR_BOULDER_3},
        {0, -1, PIX_GRASS2, DECOR_BOULDER_4},
        {2, -1, PIX_SNOW1, DECOR_BOULDER_1},
        {4, -1, PIX_ASH1, DECOR_BOULDER_2},
        // pebbles: dark grass (implied), grass, snow
        {-6, 1, PIX_GRASS_DARK_1, DECOR_PEBBLES},
        {-4, 1, PIX_GRASS2, DECOR_PEBBLES},
        {-2, 1, PIX_SNOW1, DECOR_PEBBLES},
        // columns (dual-use tile art, corner transparency): plank floor + grass
        {0, 1, PIX_FLOOR1, DECOR_COLUMN_BOTTOM},
        {1, 1, PIX_GRASS1, DECOR_COLUMN_TOP},
        // shrub + bones: grass, snow, ash
        {-6, 3, PIX_GRASS1, DECOR_SHRUB},
        {-4, 3, PIX_SNOW1, DECOR_SHRUB},
        {-2, 3, PIX_GRASS1, DECOR_BONES},
        {0, 3, PIX_ASH1, DECOR_BONES},
    };
    for (const Cell& c : cells)
    {
        set_base(world, 0, cx + c.dx, cy + c.dy, c.base);
        set_decor(world, 0, cx + c.dx, cy + c.dy, c.decor);
    }

    const char* base_dir = getenv("OG_FX_CAPTURE_DIR");
    const std::string dir = std::string(base_dir) + "/decor_sampler";
    std::filesystem::create_directories(dir);
    Sint32 cyc = 0;
    for (int f = 0; f < 60; f++)
    {
        ASSERT_TRUE(do_redraw_data(vs));
        scr()->do_cycle(cyc++, 3); // real-loop palette cycling: flames flicker
        const std::vector<RGB> shot = grab_rect(
            static_cast<int>(vs->xloc), static_cast<int>(vs->yloc),
            static_cast<int>(vs->xview), static_cast<int>(vs->yview));
        const std::string path = dir + std::format("/{:03d}.ppm", f);
        FILE* fp = fopen(path.c_str(), "wb");
        ASSERT_NE(nullptr, fp);
        fprintf(fp, "P6\n%d %d\n255\n", static_cast<int>(vs->xview),
                static_cast<int>(vs->yview));
        for (const RGB& p : shot)
        {
            fputc(p.r, fp);
            fputc(p.g, fp);
            fputc(p.b, fp);
        }
        fclose(fp);
    }

    restore_world(vs);
}
