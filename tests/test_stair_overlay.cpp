// B1 regression: the stair direction affordance. PIX_ZSTAIR_UP/DOWN tiles on
// the CAMERA floor get a soft alpha-pulsed chevron blended over the tile art
// (under the sprites) so the player can tell which way stairs go. Core
// usability, deliberately NOT an "effects" cfg toggle. Guarantees pinned
// here:
//   - chevron pixels appear over stair tiles and pulse with the render tick;
//   - scenes without stair tiles render byte-identically (the overlay pass
//     touches zero pixels);
//   - only the camera floor's stairs get the overlay;
//   - the level editor's floor-override draw never shows it (authoring view
//     stays exact).
#include <openglad/interface/render/effects.h>
#include <openglad/interface/render/radar.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/gparser.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
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
    vs->editor_floor_override_ = -1;
    scr()->world().delete_objects();
    scr()->world().set_floor_count(1);
    scr()->world().set_weather(WeatherKind::None);
}

bool do_redraw(viewscreen* vs)
{
    return vs->redraw(&scr()->level_runtime_data(), false);
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
    out.reserve(static_cast<size_t>(w) * static_cast<size_t>(h));
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            out.push_back(px(x0 + i, y0 + j));
    return out;
}

bool rects_equal(const std::vector<RGB>& a, const std::vector<RGB>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (!same(a[i], b[i]))
            return false;
    return true;
}

void fill_camera_grid(unsigned char tile)
{
    GameWorld& world = scr()->world();
    const std::size_t cells =
        static_cast<std::size_t>(world.grid.w) * world.grid.h;
    std::fill(world.grid.data.get(), world.grid.data.get() + cells, tile);
}

// Give floor f an own grid filled with `tile` (multifloor test worlds).
void fill_floor_grid(GameWorld& world, int f, unsigned char tile)
{
    const int gw = world.grid.w;
    const int gh = world.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * gh];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * gh, tile);
    world.grid_for_floor(f) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
    world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
}

void set_tile(PixieData& grid, int gx, int gy, unsigned char t)
{
    if (gx >= 0 && gy >= 0 && gx < grid.w && gy < grid.h)
        grid.data[gx + grid.w * gy] = t;
}

// Screen-space top-left corner of tile (gx, gy) — valid AFTER a redraw has
// settled the camera on the control walker.
void tile_screen_corner(const viewscreen* vs, int gx, int gy,
                        int& sx, int& sy)
{
    sx = gx * GRID_SIZE - vs->topx + vs->xloc;
    sy = gy * GRID_SIZE - vs->topy + vs->yloc;
}

// Optional visual still for review: P6 PPM of the viewport when
// OG_FX_CAPTURE_DIR is set (costs nothing in normal ctest runs).
void dump_viewport_ppm(viewscreen* vs, const char* name)
{
    const char* base = getenv("OG_FX_CAPTURE_DIR");
    if (!base)
        return;
    const int vw = vs->endx - vs->xloc;
    const int vh = vs->endy - vs->yloc;
    std::filesystem::create_directories(base);
    const std::string path = std::string(base) + "/" + name + ".ppm";
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp)
        return;
    fprintf(fp, "P6\n%d %d\n255\n", vw, vh);
    for (int j = 0; j < vh; j++)
        for (int i = 0; i < vw; i++)
        {
            const RGB p = px(vs->xloc + i, vs->yloc + j);
            fputc(p.r, fp);
            fputc(p.g, fp);
            fputc(p.b, fp);
        }
    fclose(fp);
}

// Quiet the cfg-gated effects so the only scene difference under test is the
// stair overlay itself. Restores prior values on destruction.
class QuietEffectsGuard
{
public:
    QuietEffectsGuard()
    {
        for (const char* key : kKeys)
        {
            saved_.emplace_back(key, cfg.get_setting("effects", key));
            cfg.apply_setting("effects", key, "off");
        }
    }
    ~QuietEffectsGuard()
    {
        for (auto& [key, value] : saved_)
            cfg.apply_setting("effects", key,
                              value.empty() ? "on" : value);
    }
    QuietEffectsGuard(const QuietEffectsGuard&) = delete;
    QuietEffectsGuard& operator=(const QuietEffectsGuard&) = delete;

private:
    static constexpr const char* kKeys[] = {
        "shadows", "reflections", "weather", "ripples",
        "trails",  "dust",        "fire_glow", "depth_fx",
        "screen_shake", "floor_glide",
    };
    std::vector<std::pair<std::string, std::string>> saved_;
};

// These pixel probes were authored against the historical 320x200 layout.
// Pin the classic canvas for each test so non-16:10 display aspects cannot
// move their coordinates, then restore the live aspect-relative canvas.
class StairOverlay : public testing::Test
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

TEST_F(StairOverlay, chevrons_draw_over_stair_tiles_and_pulse)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    QuietEffectsGuard quiet;
    GameWorld& world = scr()->world();
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));

    walker* control = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(160, 120);
    vs->control = control;

    // Settle the camera on the control walker before deriving screen coords.
    ASSERT_TRUE(do_redraw(vs));
    const int stair_gx = (vs->topx + vs->xview / 2) / GRID_SIZE + 2;
    const int stair_gy = (vs->topy + vs->yview / 2) / GRID_SIZE;
    set_tile(world.grid, stair_gx, stair_gy,
             static_cast<unsigned char>(PIX_ZSTAIR_UP));
    set_tile(world.grid, stair_gx - 4, stair_gy,
             static_cast<unsigned char>(PIX_ZSTAIR_DOWN));

    int sx = 0, sy = 0;
    tile_screen_corner(vs, stair_gx, stair_gy, sx, sy);

    // Editor floor-override draw: same scene + tick, NO overlay — this is
    // the with-vs-without baseline AND the editor exemption pin.
    effects_reset_for_testing();
    vs->editor_floor_override_ = 0;
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("render", "stair_overlay"))
        << "the level editor's floor-override draw must not show the overlay";
    const std::vector<RGB> editor_rect =
        grab_rect(sx, sy, GRID_SIZE + 2, GRID_SIZE + 2);

    // Play draw: overlay pixels appear over the stair tile.
    vs->editor_floor_override_ = -1;
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("render", "stair_overlay floor=0"))
        << "camera-floor stair tiles must draw the direction overlay";
    const std::vector<RGB> play_rect =
        grab_rect(sx, sy, GRID_SIZE + 2, GRID_SIZE + 2);
    ASSERT_FALSE(rects_equal(editor_rect, play_rect))
        << "chevron must alter pixels over the stair tile";
    dump_viewport_ppm(vs, "stair_overlay_play");

    // The pulse breathes: a later render tick blends a different alpha.
    for (int i = 0; i < 16; i++)
        effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> later_rect =
        grab_rect(sx, sy, GRID_SIZE + 2, GRID_SIZE + 2);
    ASSERT_FALSE(rects_equal(play_rect, later_rect))
        << "overlay alpha must pulse with the render tick";

    // Deterministic: reset + replay reproduces the first frame exactly.
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> replay_rect =
        grab_rect(sx, sy, GRID_SIZE + 2, GRID_SIZE + 2);
    ASSERT_TRUE(rects_equal(play_rect, replay_rect))
        << "overlay must be a pure function of the render tick";

    effects_reset_for_testing();
    restore_world(vs);
}

TEST_F(StairOverlay, scenes_without_stairs_render_byte_identically)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    QuietEffectsGuard quiet;
    GameWorld& world = scr()->world();
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));

    walker* control = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(160, 120);
    vs->control = control;

    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("render", "stair_overlay"))
        << "no stair tiles: the overlay pass must not fire";
    const std::vector<RGB> first =
        grab_rect(vs->xloc, vs->yloc, vs->xview, vs->yview);

    // A different render tick must not change a single pixel.
    for (int i = 0; i < 16; i++)
        effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> second =
        grab_rect(vs->xloc, vs->yloc, vs->xview, vs->yview);
    ASSERT_TRUE(rects_equal(first, second))
        << "without stairs the overlay must touch zero pixels";

    effects_reset_for_testing();
    restore_world(vs);
}

// Visual-review still (skipped unless OG_FX_CAPTURE_DIR is set): a grass
// field with a lava lake, an up-stair and a down-stair beside the control
// walker, plus the minimap — three render ticks so the chevron pulse and the
// (static) lava radar color can be eyeballed.
TEST_F(StairOverlay, zz_capture_stair_and_lava_scene)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";

    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    QuietEffectsGuard quiet;
    GameWorld& world = scr()->world();
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));

    walker* control = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(160, 120);
    vs->control = control;
    ASSERT_TRUE(do_redraw(vs)); // settle camera

    const int cgx = (vs->topx + vs->xview / 2) / GRID_SIZE;
    const int cgy = (vs->topy + vs->yview / 2) / GRID_SIZE;
    set_tile(world.grid, cgx + 2, cgy,
             static_cast<unsigned char>(PIX_ZSTAIR_UP));
    set_tile(world.grid, cgx - 2, cgy,
             static_cast<unsigned char>(PIX_ZSTAIR_DOWN));
    for (int j = -1; j <= 1; j++)
        for (int i = -2; i <= 2; i++)
            set_tile(world.grid, cgx + i, cgy + 3 + j,
                     static_cast<unsigned char>((i + j) % 2 == 0
                                                    ? PIX_LAVA1
                                                    : PIX_LAVA2));

    radar r(vs, scr(), 0);
    r.force_lower_position = true;
    r.start(&scr()->level_runtime_data());

    effects_reset_for_testing();
    const char* names[3] = {"stair_lava_t0", "stair_lava_t16",
                            "stair_lava_t32"};
    for (int shot = 0; shot < 3; shot++)
    {
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_EQ(1, r.draw(&scr()->level_runtime_data()));
        dump_viewport_ppm(vs, names[shot]);
        for (int i = 0; i < 16; i++)
            effects_advance_frame();
    }

    effects_reset_for_testing();
    restore_world(vs);
}

TEST_F(StairOverlay, overlay_follows_the_camera_floor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    QuietEffectsGuard quiet;
    GameWorld& world = scr()->world();
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));

    walker* control = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(160, 120);
    vs->control = control;

    // Stairs exist ONLY on floor 1.
    ASSERT_TRUE(do_redraw(vs)); // settle camera
    const int gx = (vs->topx + vs->xview / 2) / GRID_SIZE + 2;
    const int gy = (vs->topy + vs->yview / 2) / GRID_SIZE;
    set_tile(world.grid_for_floor(1), gx, gy,
             static_cast<unsigned char>(PIX_ZSTAIR_DOWN));

    // Camera on floor 0: floor 1's stairs draw no overlay (even as ghosts).
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("render", "stair_overlay"))
        << "non-camera floors must not draw the overlay";

    // Camera on floor 1: its stair now pulses.
    control->set_floor(1);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("render", "stair_overlay floor=1"))
        << "the camera floor's stairs draw the overlay";

    control->set_floor(0);
    effects_reset_for_testing();
    restore_world(vs);
}
