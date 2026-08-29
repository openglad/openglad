// Voxel renderer SPIKE measurement harness (docs/voxel-render-design.md §9).
//
// For each real scene it renders the level twice — once through today's
// blitter path (viewscreen::redraw) and once through the voxel Classic camera
// — dumps both plus a magenta diff as P6 PPM, and prints the mismatch count.
// It then renders four Free (orthographic yaw/pitch) views of the same scene.
//
// Nothing here asserts: during the spike the comparison is a MEASUREMENT.
// Skipped unless OG_VOXEL_SPIKE_DIR is set, so it costs nothing in ctest.
// Run standalone with:
//   OG_VOXEL_SPIKE_DIR=/tmp/voxel ./build/ci-test/og_test_rendering --gtest_filter='VoxelSpike.*'
#include <openglad/interface/render/voxel_scene.h>
#include <openglad/interface/render/voxel_scene_builder.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/level_visuals.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/platform/game_session.h>

#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

extern cfg_store cfg;

namespace {

constexpr int kClassicW = 320;
constexpr int kClassicH = 200;

screen* scr()
{
    return og::runtime::current_session->myscreen_;
}

viewscreen* view0()
{
    return scr()->viewob[0].get();
}

struct RGB
{
    Uint8 r = 0, g = 0, b = 0;
    bool operator==(const RGB& o) const
    {
        return r == o.r && g == o.g && b == o.b;
    }
};

RGB unpack(std::uint32_t xrgb)
{
    return RGB{static_cast<Uint8>((xrgb >> 16) & 0xFFu),
               static_cast<Uint8>((xrgb >> 8) & 0xFFu),
               static_cast<Uint8>(xrgb & 0xFFu)};
}

// The 256-entry palette LUT the blitters consult, rebuilt from curpal.
// video_sdl.cpp:706 maps each entry with SDL_MapRGB(pal[i]*4); on the 32bpp
// XRGB canvas that is exactly the 8-bit channel triple, and screen::get_pixel
// reads it back through SDL_GetRGB — so comparing RGB triples is exact
// without reaching into the SDL backend at all.
std::vector<std::uint32_t> build_palette_lut()
{
    std::vector<std::uint32_t> lut(256, 0u);
    const unsigned char* pal = og::runtime::current_session->curpal_.data();
    for (int i = 0; i < 256; ++i)
    {
        const std::uint32_t r = static_cast<std::uint32_t>(pal[i * 3] * 4);
        const std::uint32_t g = static_cast<std::uint32_t>(pal[i * 3 + 1] * 4);
        const std::uint32_t b = static_cast<std::uint32_t>(pal[i * 3 + 2] * 4);
        lut[static_cast<std::size_t>(i)] =
            0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return lut;
}

std::vector<RGB> grab_canvas(int w, int h)
{
    std::vector<RGB> out;
    out.reserve(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            RGB c;
            scr()->get_pixel(x, y, &c.r, &c.g, &c.b);
            out.push_back(c);
        }
    return out;
}

std::string spike_dir()
{
    const char* base = getenv("OG_VOXEL_SPIKE_DIR");
    return base ? std::string(base) : std::string();
}

void write_ppm(const std::string& scene, const std::string& name,
               const std::vector<RGB>& px, int w, int h)
{
    const std::string dir = spike_dir() + "/" + scene;
    std::filesystem::create_directories(dir);
    const std::string path = dir + "/" + name + ".ppm";
    FILE* fp = fopen(path.c_str(), "wb");
    ASSERT_NE(nullptr, fp) << path;
    fprintf(fp, "P6\n%d %d\n255\n", w, h);
    for (const RGB& p : px)
    {
        fputc(p.r, fp);
        fputc(p.g, fp);
        fputc(p.b, fp);
    }
    fclose(fp);
}

void write_ppm_xrgb(const std::string& scene, const std::string& name,
                    const std::vector<std::uint32_t>& buf, int w, int h)
{
    std::vector<RGB> px;
    px.reserve(buf.size());
    for (std::uint32_t c : buf)
        px.push_back(unpack(c));
    write_ppm(scene, name, px, w, h);
}

void all_effects_off()
{
    for (const char* key : {"shadows", "reflections", "weather", "ripples",
                            "trails", "dust", "fire_glow", "depth_fx",
                            "screen_shake", "floor_glide", "attack_lunge",
                            "hit_recoil", "hit_flash", "hit_anim",
                            "mini_hp_bar", "damage_numbers", "heal_numbers",
                            "gore", "color_cycling"})
        cfg.apply_setting("effects", key, "off");
}

bool mount_scene_campaign(const char* campaign)
{
    restore_default_campaigns();
    restore_default_settings();
#ifdef TESTING
    set_mounted_campaign_for_testing("");
#endif
    scr()->save_data.current_campaign = campaign;
    return mount_campaign_package_with_error(campaign) ==
        CampaignPackageIoError::None;
}

// Frame the camera on a living walker (or the grid centre), WITHOUT making it
// view->control: a control walker feeds compute_outline, which would put an
// OUTLINE_MODE blit on named enemies and take the comparison off the plain
// material the spike implements.
void frame_camera(viewscreen* vs, GameWorld& world, int floor)
{
    float wx = 0.0f, wy = 0.0f;
    bool found = false;
    for (const auto& uptr : world.oblist)
    {
        walker* const w = uptr.get();
        if (w == nullptr || w->dead() || w->dormant())
            continue;
        if (w->query_order() != Order::Living)
            continue;
        if (world.floor_count() > 1 && static_cast<int>(w->floor()) != floor)
            continue;
        wx = w->worldx();
        wy = w->worldy();
        found = true;
        break;
    }
    if (!found)
    {
        const PixieData& g = world.grid_for_floor(floor);
        wx = static_cast<float>(g.w * GRID_SIZE) / 2.0f;
        wy = static_cast<float>(g.h * GRID_SIZE) / 2.0f;
    }
    const int topx = static_cast<int>(wx) - (vs->xview - 32) / 2;
    const int topy = static_cast<int>(wy) - (vs->yview - 32) / 2;
    scr()->set_level_draw_pos(topx, topy);
}

struct SceneSpec
{
    const char* name;
    const char* campaign;
    int level;
    int floor; // -1 = whatever the level's floor 0 is
};

// One scene: load, render both paths, dump, report.
void run_scene(const SceneSpec& spec)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    ASSERT_TRUE(mount_scene_campaign(spec.campaign))
        << "campaign " << spec.campaign;
    all_effects_off();

    GameWorld& world = scr()->world();
    world.delete_objects();
    world.id = static_cast<short>(spec.level);
    scr()->save_data.scen_num = static_cast<short>(spec.level);
    ASSERT_TRUE(scr()->load_level())
        << "load " << spec.campaign << " scen" << spec.level;
    scr()->clear_all_view_text();

    const int floor_count = world.floor_count();
    const int floor = (spec.floor >= 0 && spec.floor < floor_count)
        ? spec.floor
        : 0;

    // No control walker (see frame_camera). editor_authoring_view_ keeps the
    // default multifloor look — the upper-floor blob shadows, which have no
    // cfg gate — out of the comparison; editor_floor_override_ pins the
    // camera floor so the pass runs opaque (falpha 255), which is the only
    // pass the spike's plain material models.
    vs->control = nullptr;
    vs->editor_floor_override_ = floor;
    vs->editor_authoring_view_ = true;
    frame_camera(vs, world, floor);

    ASSERT_TRUE(vs->redraw(&scr()->level_runtime_data(), false));
    const std::vector<RGB> old_px = grab_canvas(kClassicW, kClassicH);

    // ---- voxel Classic ----
    const std::vector<std::uint32_t> lut = build_palette_lut();
    std::vector<std::uint32_t> buf(
        static_cast<std::size_t>(kClassicW) * kClassicH, 0u);
    // Outside the viewport lives HUD chrome the voxel scene does not own; seed
    // it from the old canvas so the dumped PPM reads as a whole frame. The
    // mismatch count below is taken over the VIEWPORT only.
    for (std::size_t i = 0; i < buf.size(); ++i)
        buf[i] = 0xFF000000u | (static_cast<std::uint32_t>(old_px[i].r) << 16) |
            (static_cast<std::uint32_t>(old_px[i].g) << 8) | old_px[i].b;
    for (int y = vs->yloc; y < vs->endy && y < kClassicH; ++y)
        for (int x = vs->xloc; x < vs->endx && x < kClassicW; ++x)
            buf[static_cast<std::size_t>(y) * kClassicW +
                static_cast<std::size_t>(x)] = 0xFF000000u;

    og::render::VoxelScene scene;
    og::render::VoxelSceneBuildParams bp;
    bp.topx = vs->topx;
    bp.topy = vs->topy;
    bp.xview = vs->xview;
    bp.yview = vs->yview;
    bp.floor_from = floor;
    bp.floor_to = floor;
    bp.floor_stride = 0.0f;
    bp.draw_dormant = true; // mirrors editor_floor_override_ >= 0
    bp.skip_hit_fx = true;  // mirrors cfg effects/hit_anim off
    const og::render::VoxelSceneBuildStats bs = og::render::build_voxel_scene(
        scene, world, scr()->level_visuals(), bp);

    og::render::VoxelCamera classic;
    classic.kind = og::render::VoxelCameraKind::Classic;
    classic.topx = vs->topx;
    classic.topy = vs->topy;
    classic.xloc = vs->xloc;
    classic.yloc = vs->yloc;

    og::render::VoxelRenderTarget rt;
    rt.pixels = buf.data();
    rt.pitch_px = kClassicW;
    rt.w = kClassicW;
    rt.h = kClassicH;
    rt.clip_x0 = vs->xloc;
    rt.clip_y0 = vs->yloc;
    rt.clip_x1 = vs->endx;
    rt.clip_y1 = vs->endy;
    rt.lut256 = lut.data();

    og::render::VoxelRaster raster;
    const og::render::VoxelRasterStats rs = raster.render(scene, classic, rt);

    // ---- compare over the viewport ----
    std::vector<RGB> new_px;
    new_px.reserve(buf.size());
    for (std::uint32_t c : buf)
        new_px.push_back(unpack(c));

    int mismatches = 0;
    int printed = 0;
    std::string first_lines;
    for (int y = vs->yloc; y < vs->endy && y < kClassicH; ++y)
        for (int x = vs->xloc; x < vs->endx && x < kClassicW; ++x)
        {
            const std::size_t i =
                static_cast<std::size_t>(y) * kClassicW +
                static_cast<std::size_t>(x);
            if (old_px[i] == new_px[i])
                continue;
            ++mismatches;
            if (printed < 10)
            {
                char line[160];
                snprintf(line, sizeof(line),
                         "    (%3d,%3d) old=%3u,%3u,%3u  new=%3u,%3u,%3u\n", x,
                         y, old_px[i].r, old_px[i].g, old_px[i].b, new_px[i].r,
                         new_px[i].g, new_px[i].b);
                first_lines += line;
                ++printed;
            }
        }

    std::vector<RGB> diff = old_px;
    for (int y = vs->yloc; y < vs->endy && y < kClassicH; ++y)
        for (int x = vs->xloc; x < vs->endx && x < kClassicW; ++x)
        {
            const std::size_t i =
                static_cast<std::size_t>(y) * kClassicW +
                static_cast<std::size_t>(x);
            if (old_px[i] == new_px[i])
            {
                diff[i].r = static_cast<Uint8>(diff[i].r / 4);
                diff[i].g = static_cast<Uint8>(diff[i].g / 4);
                diff[i].b = static_cast<Uint8>(diff[i].b / 4);
            }
            else
            {
                diff[i] = RGB{255, 0, 255};
            }
        }

    write_ppm(spec.name, "old", old_px, kClassicW, kClassicH);
    write_ppm(spec.name, "new", new_px, kClassicW, kClassicH);
    write_ppm(spec.name, "diff", diff, kClassicW, kClassicH);

    const int vw = static_cast<int>(vs->endx - vs->xloc);
    const int vh = static_cast<int>(vs->endy - vs->yloc);
    printf("[voxel-spike] %s (%s scen%d floor %d/%d)\n", spec.name,
           spec.campaign, spec.level, floor, floor_count);
    printf("  viewport %dx%d at (%d,%d)  topx=%d topy=%d  grid %dx%d\n", vw, vh,
           static_cast<int>(vs->xloc), static_cast<int>(vs->yloc),
           static_cast<int>(vs->topx), static_cast<int>(vs->topy),
           world.grid_for_floor(floor).w, world.grid_for_floor(floor).h);
    printf("  scene: %d tiles, %d decor, %d entities (%d would take a "
           "non-plain blit today)\n",
           bs.tiles, bs.decor, bs.entities, bs.special_mode_entities);
    printf("  classic raster: %llu volumes, %llu slices, %llu samples, "
           "%llu writes\n",
           static_cast<unsigned long long>(rs.volumes),
           static_cast<unsigned long long>(rs.slices),
           static_cast<unsigned long long>(rs.pixel_samples),
           static_cast<unsigned long long>(rs.pixels_written));
    printf("  MISMATCHES: %d / %d viewport pixels\n", mismatches, vw * vh);
    if (mismatches > 0)
        printf("%s", first_lines.c_str());

    // ---- Free views ----
    struct FreeSpec
    {
        const char* name;
        float yaw;
        float pitch;
        float scale;
    };
    const FreeSpec frees[] = {
        {"free_y30_p60", 30.0f, 60.0f, 1.0f},
        {"free_y45_p45", 45.0f, 45.0f, 1.0f},
        {"free_y00_p35", 0.0f, 35.0f, 1.0f},
        {"free_y30_p60_x2", 30.0f, 60.0f, 2.0f},
    };

    // The Free scene is the whole floor stack, with floors at real height.
    og::render::VoxelScene fscene;
    og::render::VoxelSceneBuildParams fbp = bp;
    fbp.floor_from = 0;
    fbp.floor_to = floor_count - 1;
    fbp.floor_stride = og::render::kVoxelFloorStride;
    // Widen the tile window so a rotated view is not cropped to the classic
    // window's axis-aligned box.
    fbp.topx = vs->topx - vs->xview;
    fbp.topy = vs->topy - vs->yview;
    fbp.xview = vs->xview * 3;
    fbp.yview = vs->yview * 3;
    const og::render::VoxelSceneBuildStats fbs =
        og::render::build_voxel_scene(fscene, world, scr()->level_visuals(), fbp);
    printf("  free scene: %d tiles, %d decor, %d entities\n", fbs.tiles,
           fbs.decor, fbs.entities);

    const float focus_x =
        static_cast<float>(vs->topx) + static_cast<float>(vs->xview) / 2.0f;
    const float focus_y =
        static_cast<float>(vs->topy) + static_cast<float>(vs->yview) / 2.0f;

    for (const FreeSpec& f : frees)
    {
        std::vector<std::uint32_t> fbuf(
            static_cast<std::size_t>(kClassicW) * kClassicH, 0xFF000000u);
        og::render::VoxelCamera cam;
        cam.kind = og::render::VoxelCameraKind::Free;
        cam.cx = focus_x;
        cam.cy = focus_y;
        cam.yaw_deg = f.yaw;
        cam.pitch_deg = f.pitch;
        cam.scale = f.scale;
        cam.view_cx = static_cast<float>(kClassicW) / 2.0f;
        cam.view_cy = static_cast<float>(kClassicH) / 2.0f;

        og::render::VoxelRenderTarget frt;
        frt.pixels = fbuf.data();
        frt.pitch_px = kClassicW;
        frt.w = kClassicW;
        frt.h = kClassicH;
        frt.clip_x0 = 0;
        frt.clip_y0 = 0;
        frt.clip_x1 = kClassicW;
        frt.clip_y1 = kClassicH;
        frt.lut256 = lut.data();

        og::render::VoxelRaster fraster;
        const og::render::VoxelRasterStats frs =
            fraster.render(fscene, cam, frt);
        write_ppm_xrgb(spec.name, f.name, fbuf, kClassicW, kClassicH);
        printf("  %s: %llu slices, %llu samples, %llu writes\n", f.name,
               static_cast<unsigned long long>(frs.slices),
               static_cast<unsigned long long>(frs.pixel_samples),
               static_cast<unsigned long long>(frs.pixels_written));
    }

    vs->editor_authoring_view_ = false;
    vs->editor_floor_override_ = -1;
    world.delete_objects();
    world.set_floor_count(1);
}

class VoxelSpike : public testing::Test
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
        restore_default_settings();
    }
};

} // namespace

TEST_F(VoxelSpike, classic_parity_and_free_views)
{
    if (spike_dir().empty())
        GTEST_SKIP() << "set OG_VOXEL_SPIKE_DIR to record";

    const SceneSpec scenes[] = {
        {"gladiator_scen1", "gladiator", 1, -1},
        {"westlands_scen1", "westlands", 1, -1},
        {"tower_scen700", "tower", 700, -1},
        // The shipped tower level is single-floor (its upper floors are
        // generated per run), so the multifloor Classic pass rides a real
        // two-floor level instead: concept scen600 (campaigns/concept/pix has
        // scen0600_f1.png). Camera floor 0 is the only floor a multifloor
        // level draws opaquely — every floor BELOW the camera composites
        // through the fade layer, which the spike's plain material does not
        // model (§5 keeps that choreography).
        {"concept_scen600_multifloor", "concept", 600, 0},
        {"modes_soccer_scen820", "modes", 820, -1},
    };
    for (const SceneSpec& s : scenes)
    {
        run_scene(s);
        if (testing::Test::HasFatalFailure())
            return;
    }
}
