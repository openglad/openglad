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

#include <openglad/interface/render/voxel_carve.h>
#include <openglad/interface/render/voxel_art.h>
#include <openglad/interface/render/voxel_figure.h>
#include <openglad/interface/render/voxel_fit.h>
#include <openglad/interface/render/voxel_relief.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
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
    // Unmount the CURRENT package before clearing the mounted-campaign state.
    // set_mounted_campaign_for_testing("") makes the next mount unconditional
    // (restore_default_campaigns has just rewritten the .glad files under it),
    // but it also hides the previous package from mount_campaign_package_impl,
    // which is the only thing that ever unmounts one. Without this the
    // packages pile up in the search path, and PhysFS will not reorder an
    // archive that is already mounted — so a scene that re-mounts a campaign
    // an earlier test already mounted silently reads a DIFFERENT campaign's
    // scen1.fss. (Cost: gladiator scen1 rendered as westlands scen1.)
    const std::string prev = get_mounted_campaign();
    if (!prev.empty())
        (void)unmount_campaign_package_with_error(prev);
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

// ===========================================================================
// Stage 2 round 12 (docs/voxel-render-design.md §16): refined AUTHORED figures.
//
// Every algorithmic route was closed by ruling — carving gives solids of
// revolution, reliefs give cards, fitting gives lumps, hulls give dirt. The
// figures are now drawn by hand from the sprite frames and live in the repo
// as text (assets/voxelart/<family>.voxtxt), the way the sprites live in it
// as PNG. This harness loads them and renders the same set of pictures, so
// the comparison against every earlier round is like for like. Reliefs
// survive only as the comparison panel and as the fallback for non-living
// orders.
// ===========================================================================

namespace {

// Round 12 writes its own set; models11/ stays as the rejected tall-doll
// picture.
constexpr const char* kRoundDir = "models12";

std::string models_dir()
{
    return spike_dir() + "/" + kRoundDir;
}

struct Image
{
    int w = 0;
    int h = 0;
    std::vector<RGB> px;
};

Image make_image(int w, int h, RGB bg)
{
    Image im;
    im.w = w;
    im.h = h;
    im.px.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), bg);
    return im;
}

std::uint32_t pack(RGB c)
{
    return 0xFF000000u | (static_cast<std::uint32_t>(c.r) << 16) |
        (static_cast<std::uint32_t>(c.g) << 8) | c.b;
}

unsigned char remap_team(unsigned char c, unsigned char team)
{
    return c >= 248 ? static_cast<unsigned char>(team + (255 - c)) : c;
}

void draw_indices(Image& im, int x0, int y0, const unsigned char* idx, int w,
                  int h, const std::vector<std::uint32_t>& lut,
                  unsigned char team)
{
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const unsigned char c =
                idx[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                    static_cast<std::size_t>(x)];
            if (c == 0)
                continue;
            const int dx = x0 + x;
            const int dy = y0 + y;
            if (dx < 0 || dy < 0 || dx >= im.w || dy >= im.h)
                continue;
            im.px[static_cast<std::size_t>(dy) * static_cast<std::size_t>(im.w) +
                  static_cast<std::size_t>(dx)] =
                unpack(lut[static_cast<std::size_t>(remap_team(c, team))]);
        }
}

Image upscale(const Image& src, int f)
{
    Image out = make_image(src.w * f, src.h * f, RGB{0, 0, 0});
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x)
            out.px[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(out.w) +
                   static_cast<std::size_t>(x)] =
                src.px[static_cast<std::size_t>(y / f) *
                           static_cast<std::size_t>(src.w) +
                       static_cast<std::size_t>(x / f)];
    return out;
}

void paste(Image& dst, int x0, int y0, const Image& src)
{
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x)
        {
            const int dx = x0 + x, dy = y0 + y;
            if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h)
                continue;
            dst.px[static_cast<std::size_t>(dy) *
                       static_cast<std::size_t>(dst.w) +
                   static_cast<std::size_t>(dx)] =
                src.px[static_cast<std::size_t>(y) *
                           static_cast<std::size_t>(src.w) +
                       static_cast<std::size_t>(x)];
        }
}

Image crop(const Image& src, int x0, int y0, int w, int h, RGB bg)
{
    Image out = make_image(w, h, bg);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const int sx = x0 + x, sy = y0 + y;
            if (sx < 0 || sy < 0 || sx >= src.w || sy >= src.h)
                continue;
            out.px[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                   static_cast<std::size_t>(x)] =
                src.px[static_cast<std::size_t>(sy) *
                           static_cast<std::size_t>(src.w) +
                       static_cast<std::size_t>(sx)];
        }
    return out;
}

void write_image(const std::string& dir, const std::string& name,
                 const Image& im)
{
    std::filesystem::create_directories(dir);
    const std::string path = dir + "/" + name + ".ppm";
    FILE* fp = fopen(path.c_str(), "wb");
    ASSERT_NE(nullptr, fp) << path;
    fprintf(fp, "P6\n%d %d\n255\n", im.w, im.h);
    for (const RGB& p : im.px)
    {
        fputc(p.r, fp);
        fputc(p.g, fp);
        fputc(p.b, fp);
    }
    fclose(fp);
}

constexpr unsigned char kShotTeam = 40;
constexpr RGB kPlateBg{38, 40, 46};
constexpr float kPi = 3.14159265358979f;
constexpr float kTheta = og::render::kVoxelFigureTheta;

bool family_walk_frames(int family, og::render::VoxelCarveFrames& out)
{
    loader* const L = scr()->myloader;
    if (L == nullptr)
        return false;
    const PixieData* const pd = L->graphics_for(Order::Living, family);
    if (pd == nullptr || !pd->valid())
        return false;
    const int slot = loader::slot_for(Order::Living, family);
    if (slot < 0)
        return false;
    const signed char* const* ani =
        L->animations[static_cast<std::size_t>(slot)];
    if (ani == nullptr)
        return false;
    out.w = pd->w;
    out.h = pd->h;
    for (int d = 0; d < NUM_FACINGS; ++d)
    {
        const signed char* const row = ani[ANI_WALK * NUM_FACINGS + d];
        if (row == nullptr || row[0] < 0)
            return false;
        const int f = static_cast<int>(row[0]);
        if (f >= static_cast<int>(pd->frames))
            return false;
        out.frame[d] = pd->data.get() +
            static_cast<std::size_t>(f) * static_cast<std::size_t>(pd->w) *
                static_cast<std::size_t>(pd->h);
    }
    return true;
}


int shown_facing(int entity_dir, float camera_yaw_deg)
{
    const float ent = static_cast<float>((entity_dir - FACE_DOWN) * 45);
    int shown = FACE_DOWN + static_cast<int>(std::lround(
                                (ent + camera_yaw_deg) / 45.0f));
    return ((shown % NUM_FACINGS) + NUM_FACINGS) % NUM_FACINGS;
}

float plane_yaw_for(float camera_yaw_deg)
{
    const int bin = static_cast<int>(std::lround(camera_yaw_deg / 45.0f));
    return static_cast<float>(bin * 45) * kPi / 180.0f;
}

// Livings draw the voting-hull figure; every other order keeps the round-8
// relief, which is still the right answer for a grave cross.
class SceneModels : public og::render::VoxelModelSource
{
public:
    explicit SceneModels(float theta) : cache(theta) {}
    mutable og::render::VoxelReliefCache cache;
    std::map<int, og::render::VoxelModel> tiles;
    std::map<int, og::render::VoxelModel> figures;

    const og::render::VoxelRelief* relief_for(const walker& w,
                                              float camera_yaw_deg,
                                              float& plane_yaw_rad) const override
    {
        loader* const L = scr()->myloader;
        if (L == nullptr)
            return nullptr;
        const Order order = w.query_order();
        if (order == Order::Living &&
            figures.find(static_cast<int>(w.family())) != figures.end())
            return nullptr; // the solid covers this one
        const int family = static_cast<int>(w.family());
        const int depth = og::render::voxel_relief_max_depth(order);
        plane_yaw_rad = plane_yaw_for(camera_yaw_deg);
        int dir = static_cast<int>(static_cast<unsigned char>(w.curdir()));
        if (dir < 0 || dir >= NUM_FACINGS)
            dir = FACE_DOWN;
        const int shown = shown_facing(dir, camera_yaw_deg);
        if (shown == dir && w.bmp_data() != nullptr && w.sizex() > 0 &&
            w.sizey() > 0)
            return cache.get(w.bmp_data(), w.sizex(), w.sizey(), depth);
        const PixieData* const pd = L->graphics_for(order, family);
        if (pd == nullptr || !pd->valid())
            return nullptr;
        const int slot = loader::slot_for(order, family);
        int frame = -1;
        if (slot >= 0)
        {
            const signed char* const* ani =
                L->animations[static_cast<std::size_t>(slot)];
            const int rows = L->animation_counts[static_cast<std::size_t>(slot)];
            int type = static_cast<int>(w.ani_type());
            if (type < 0)
                type = 0;
            const int row = shown + type * NUM_FACINGS;
            if (ani != nullptr && rows > 0 && row < rows && ani[row] != nullptr)
            {
                const signed char* const seq = ani[row];
                int len = 0;
                while (len < 128 && seq[len] != -1)
                    ++len;
                int c = static_cast<int>(w.cycle());
                if (c < 0 || c >= len)
                    c = 0;
                if (len > 0 && seq[c] >= 0)
                    frame = static_cast<int>(seq[c]);
            }
        }
        if (frame < 0 || frame >= static_cast<int>(pd->frames))
            frame = 0;
        const unsigned char* const ptr = pd->data.get() +
            static_cast<std::size_t>(frame) * static_cast<std::size_t>(pd->w) *
                static_cast<std::size_t>(pd->h);
        return cache.get(ptr, pd->w, pd->h, depth);
    }

    const og::render::VoxelModel* living_model(const walker& w,
                                               float& yaw_rad) const override
    {
        const auto it = figures.find(static_cast<int>(w.family()));
        if (it == figures.end())
            return nullptr;
        int dir = static_cast<int>(static_cast<unsigned char>(w.curdir()));
        if (dir < 0 || dir >= NUM_FACINGS)
            dir = FACE_DOWN;
        yaw_rad = og::render::voxel_facing_yaw_rad(dir);
        return &it->second;
    }
    const og::render::VoxelModel* tile_model(int pix) const override
    {
        const auto it = tiles.find(pix);
        return it == tiles.end() ? nullptr : &it->second;
    }
};

void build_terrain_models(SceneModels& out, const LevelVisuals& visuals)
{
    out.tiles.clear();
    const PixieData& side = visuals.pixdata[PIX_WALLSIDE1];
    for (int pix : {PIX_H_WALL1, PIX_WALL_LL, PIX_WALL2, PIX_WALL3, PIX_WALL4,
                    PIX_WALL5, PIX_WALLSIDE1, PIX_WALLSIDE_L, PIX_WALLSIDE_R,
                    PIX_WALLSIDE_C, PIX_WALLSIDE_CRACK_C1,
                    PIX_WALL_ARROW_GRASS, PIX_WALL_ARROW_FLOOR,
                    PIX_WALL_ARROW_GRASS_DARK, PIX_WALLTOP_H})
    {
        const PixieData& top = visuals.pixdata[pix];
        if (!top.valid() || !side.valid())
            continue;
        og::render::VoxelModel m = og::render::voxel_build_wall_model(
            top.data.get(), top.w, top.h, side.data.get(), side.w, side.h,
            og::render::kVoxelHeightWall);
        if (!m.empty())
            out.tiles.emplace(pix, std::move(m));
    }
    const PixieData& grass = visuals.pixdata[PIX_GRASS1];
    for (int pix : {PIX_TREE_T1, PIX_TREE_M1, PIX_TREE_ML, PIX_TREE_MR,
                    PIX_TREE_MT, PIX_TREE_B1})
    {
        const PixieData& top = visuals.pixdata[pix];
        if (!top.valid())
            continue;
        og::render::VoxelModel m = og::render::voxel_build_tree_model(
            top.data.get(), top.w, top.h,
            grass.valid() ? grass.data.get() : nullptr,
            grass.valid() ? grass.w : 0, grass.valid() ? grass.h : 0, 12, 8, 4);
        if (!m.empty())
            out.tiles.emplace(pix, std::move(m));
    }
}

class VoxelModels : public testing::Test
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

namespace {

struct ShotCamera
{
    float yaw = 0.0f;
    float pitch = kTheta;
    float scale = 1.0f;
    int w = 0;
    int h = 0;
    float view_cx = 0.0f;
    float view_cy = 0.0f;
};

struct Shot
{
    Image rgb;
    std::vector<unsigned char> index;
};

og::render::VoxelModel make_shadow(int w, int h, unsigned char idx)
{
    og::render::VoxelModel m;
    m.w = w;
    m.d = std::max(4, w / 2);
    m.z = 1;
    m.cell = 1.0f;
    m.cube_faces = true;
    m.anchor_x = static_cast<float>(w) * 0.5f;
    m.anchor_y = static_cast<float>(h);
    const std::size_t n =
        static_cast<std::size_t>(m.w) * static_cast<std::size_t>(m.d);
    m.occ.assign(n, 0u);
    m.index.assign(n, 0u);
    m.lit.assign(n, 1u);
    m.shade.assign(n, 0u);
    const float ex = static_cast<float>(m.w - 1) * 0.5f;
    const float ey = static_cast<float>(m.d - 1) * 0.5f;
    for (int j = 0; j < m.d; ++j)
        for (int i = 0; i < m.w; ++i)
        {
            const float dx = (static_cast<float>(i) - ex) / (ex + 0.5f);
            const float dy = (static_cast<float>(j) - ey) / (ey + 0.5f);
            if (dx * dx + dy * dy > 1.0f)
                continue;
            const std::size_t s = m.at(i, j, 0);
            m.occ[s] = 1u;
            m.index[s] = idx;
        }
    return m;
}

// Render one volume (figure or relief) through the real cube-face rasterizer.
Shot shoot(const og::render::VoxelModel* model,
           const og::render::VoxelRelief* relief, float yaw_rad,
           const ShotCamera& sc, float aim_x, float aim_y,
           const std::vector<std::uint32_t>& lut, unsigned char team, RGB bg,
           const og::render::VoxelModel* shadow)
{
    Shot out;
    out.rgb = make_image(sc.w, sc.h, bg);
    out.index.assign(
        static_cast<std::size_t>(sc.w) * static_cast<std::size_t>(sc.h), 0u);
    std::vector<std::uint32_t> buf(
        static_cast<std::size_t>(sc.w) * static_cast<std::size_t>(sc.h),
        pack(bg));

    og::render::VoxelScene scene;
    if (shadow != nullptr && !shadow->empty())
    {
        og::render::VoxelVolume sv;
        sv.model = shadow;
        sv.x = 0.0f;
        sv.y = 0.0f;
        sv.z = -1.0f;
        sv.material.team_color = team;
        scene.emit(sv);
    }
    og::render::VoxelVolume v;
    v.model = model;
    v.relief = relief;
    v.yaw = yaw_rad;
    v.x = 0.0f;
    v.y = 0.0f;
    v.z = 0.0f;
    v.material.team_color = team;
    scene.emit(v);

    og::render::VoxelCamera cam;
    cam.kind = og::render::VoxelCameraKind::Free;
    cam.cx = aim_x;
    cam.cy = aim_y;
    cam.yaw_deg = sc.yaw;
    cam.pitch_deg = sc.pitch;
    cam.scale = sc.scale;
    cam.view_cx = sc.view_cx;
    cam.view_cy = sc.view_cy;

    og::render::VoxelRenderTarget rt;
    rt.pixels = buf.data();
    rt.pitch_px = sc.w;
    rt.w = sc.w;
    rt.h = sc.h;
    rt.clip_x0 = 0;
    rt.clip_y0 = 0;
    rt.clip_x1 = sc.w;
    rt.clip_y1 = sc.h;
    rt.lut256 = lut.data();
    rt.index_plane = out.index.data();

    og::render::VoxelRaster raster;
    (void)raster.render(scene, cam, rt);
    for (std::size_t i = 0; i < buf.size(); ++i)
        out.rgb.px[i] = unpack(buf[i]);
    return out;
}

// The same camera with room around the sprite box, because an authored
// figure is a whole character and stands taller than the icon the sprite
// draws. Row 1 pads the frame the same way, so the two rows stay aligned.
constexpr int kFidelityPad = 6;

ShotCamera game_camera(int sprite_w, int sprite_h, float scale, int pad)
{
    const float sp = std::sin(kTheta * kPi / 180.0f);
    ShotCamera sc;
    sc.yaw = 0.0f;
    sc.pitch = kTheta;
    sc.scale = scale;
    sc.w = static_cast<int>(
        std::lround(static_cast<float>(sprite_w + pad * 2) * scale));
    sc.h = static_cast<int>(
        std::lround(static_cast<float>(sprite_h + pad * 2) * scale));
    sc.view_cx = static_cast<float>(pad) * scale;
    sc.view_cy = (static_cast<float>(sprite_h - 1) -
                  static_cast<float>(sprite_h) * sp +
                  static_cast<float>(pad)) * scale;
    return sc;
}

struct Extent
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
};

Extent model_extent(const og::render::VoxelModel& m, float model_yaw,
                    float cam_yaw, float pitch)
{
    const float rad = kPi / 180.0f;
    const float cm = std::cos(model_yaw), sm = std::sin(model_yaw);
    const float cc = std::cos(cam_yaw * rad), sc2 = std::sin(cam_yaw * rad);
    const float sp = std::sin(pitch * rad), cp = std::cos(pitch * rad);
    const float hw = m.extent_x() * 0.5f, hd = m.extent_y() * 0.5f;
    Extent e;
    for (int k = 0; k < m.z; ++k)
        for (int j = 0; j < m.d; ++j)
            for (int i = 0; i < m.w; ++i)
            {
                if (m.occ[m.at(i, j, k)] == 0)
                    continue;
                for (int c = 0; c < 8; ++c)
                {
                    const float lx =
                        (static_cast<float>(i) + ((c & 1) ? 1.0f : 0.0f)) *
                            m.cell - hw;
                    const float ly =
                        (static_cast<float>(j) + ((c & 2) ? 1.0f : 0.0f)) *
                            m.cell - hd;
                    const float lz =
                        (static_cast<float>(k) + ((c & 4) ? 1.0f : 0.0f)) *
                        m.cell;
                    const float X = m.anchor_x + lx * cm - ly * sm;
                    const float Y = m.anchor_y + lx * sm + ly * cm;
                    const float rx = (X - m.anchor_x) * cc - (Y - m.anchor_y) * sc2;
                    const float ry = (X - m.anchor_x) * sc2 + (Y - m.anchor_y) * cc;
                    const float sy2 = ry * sp - lz * cp;
                    e.x0 = std::min(e.x0, rx);
                    e.x1 = std::max(e.x1, rx);
                    e.y0 = std::min(e.y0, sy2);
                    e.y1 = std::max(e.y1, sy2);
                }
            }
    return e;
}

ShotCamera frame_model(const og::render::VoxelModel& m, float model_yaw,
                       float cam_yaw, float pitch, float scale, int pad)
{
    const Extent e = model_extent(m, model_yaw, cam_yaw, pitch);
    ShotCamera sc;
    sc.yaw = cam_yaw;
    sc.pitch = pitch;
    sc.scale = scale;
    sc.w = static_cast<int>(std::ceil((e.x1 - e.x0) * scale)) + pad * 2;
    sc.h = static_cast<int>(std::ceil((e.y1 - e.y0) * scale)) + pad * 2;
    sc.view_cx = -e.x0 * scale + static_cast<float>(pad);
    sc.view_cy = -e.y0 * scale + static_cast<float>(pad);
    return sc;
}

struct FamilySpec
{
    const char* name;
    int family;
};

const FamilySpec kFamilies[] = {
    {"footman", FAMILY_SOLDIER}, {"archer", FAMILY_ARCHER},
    {"orc", FAMILY_ORC},         {"skeleton", FAMILY_SKELETON},
    {"mage", FAMILY_MAGE},       {"elf", FAMILY_ELF},
    {"ghost", FAMILY_GHOST},     {"cleric", FAMILY_CLERIC},
};

std::string voxel_art_dir()
{
    const char* d = getenv("OG_VOXEL_ART_DIR");
    return d ? std::string(d) : std::string("assets/voxelart");
}

// Load every family's authored figure. A parse error is a hard failure: a
// figure that silently fails to load is a family that silently reverts to a
// relief, which is exactly the confusion this round exists to end.
bool load_figures(std::map<int, og::render::VoxelModel>& out,
                  std::string& error)
{
    out.clear();
    for (const FamilySpec& fs : kFamilies)
    {
        og::render::VoxelModel m;
        const std::string path =
            voxel_art_dir() + "/" + fs.name + ".voxtxt";
        if (!og::render::voxel_art_load_file(path, m, error))
            return false;
        out.emplace(fs.family, std::move(m));
    }
    return true;
}

int voxel_count(const og::render::VoxelModel& m)
{
    int n = 0;
    for (unsigned char o : m.occ)
        if (o != 0)
            ++n;
    return n;
}

void densest_cluster(GameWorld& world, float& out_x, float& out_y)
{
    constexpr float kRadius = 110.0f;
    int best = -1;
    for (const auto& u : world.oblist)
    {
        walker* const w = u.get();
        if (w == nullptr || w->dead() || w->dormant() ||
            w->query_order() != Order::Living)
            continue;
        int count = 0;
        for (const auto& v : world.oblist)
        {
            walker* const o = v.get();
            if (o == nullptr || o->dead() || o->dormant() ||
                o->query_order() != Order::Living)
                continue;
            const float dx = o->worldx() - w->worldx();
            const float dy = o->worldy() - w->worldy();
            if (dx * dx + dy * dy <= kRadius * kRadius)
                ++count;
        }
        if (count > best)
        {
            best = count;
            out_x = w->worldx();
            out_y = w->worldy();
        }
    }
}

void run_figure_scene(const std::string& name, const char* campaign, int level,
                      const std::map<int, og::render::VoxelModel>& figures)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    ASSERT_TRUE(mount_scene_campaign(campaign)) << campaign;
    all_effects_off();

    GameWorld& world = scr()->world();
    world.delete_objects();
    world.id = static_cast<short>(level);
    scr()->save_data.scen_num = static_cast<short>(level);
    ASSERT_TRUE(scr()->load_level()) << campaign << " scen" << level;
    scr()->clear_all_view_text();

    vs->control = nullptr;
    vs->editor_floor_override_ = 0;
    vs->editor_authoring_view_ = true;
    frame_camera(vs, world, 0);
    ASSERT_TRUE(vs->redraw(&scr()->level_runtime_data(), false));

    const std::vector<std::uint32_t> lut = build_palette_lut();
    SceneModels models(kTheta);
    models.figures = figures;
    build_terrain_models(models, scr()->level_visuals());

    const float wide_x =
        static_cast<float>(vs->topx) + static_cast<float>(vs->xview) / 2.0f;
    const float wide_y =
        static_cast<float>(vs->topy) + static_cast<float>(vs->yview) / 2.0f;
    float crowd_x = wide_x, crowd_y = wide_y;
    densest_cluster(world, crowd_x, crowd_y);

    struct FreeSpec
    {
        const char* tag;
        float yaw;
        float pitch;
    };
    const FreeSpec views[] = {
        {"crowd_z3_y20_p50", 20.0f, 50.0f},
        {"crowd_z3_y45_p45", 45.0f, 45.0f},
        {"crowd_z3_y00_p55", 0.0f, kTheta},
    };
    for (const FreeSpec& f : views)
    {
        og::render::VoxelScene scene;
        og::render::VoxelSceneBuildParams bp;
        bp.topx = vs->topx - vs->xview;
        bp.topy = vs->topy - vs->yview;
        bp.xview = vs->xview * 3;
        bp.yview = vs->yview * 3;
        bp.floor_from = 0;
        bp.floor_to = world.floor_count() - 1;
        bp.floor_stride = og::render::kVoxelFloorStride;
        bp.draw_dormant = true;
        bp.skip_hit_fx = true;
        bp.models = &models;
        bp.camera_yaw_deg = f.yaw;
        (void)og::render::build_voxel_scene(scene, world,
                                            scr()->level_visuals(), bp);
        std::vector<std::uint32_t> buf(
            static_cast<std::size_t>(kClassicW) * kClassicH, 0xFF000000u);
        og::render::VoxelCamera cam;
        cam.kind = og::render::VoxelCameraKind::Free;
        cam.cx = crowd_x;
        cam.cy = crowd_y;
        cam.yaw_deg = f.yaw;
        cam.pitch_deg = f.pitch;
        cam.scale = 3.0f;
        cam.view_cx = static_cast<float>(kClassicW) / 2.0f;
        cam.view_cy = static_cast<float>(kClassicH) / 2.0f;
        og::render::VoxelRenderTarget rt;
        rt.pixels = buf.data();
        rt.pitch_px = kClassicW;
        rt.w = kClassicW;
        rt.h = kClassicH;
        rt.clip_x0 = 0;
        rt.clip_y0 = 0;
        rt.clip_x1 = kClassicW;
        rt.clip_y1 = kClassicH;
        rt.lut256 = lut.data();
        og::render::VoxelRaster raster;
        (void)raster.render(scene, cam, rt);
        Image im = make_image(kClassicW, kClassicH, RGB{0, 0, 0});
        for (std::size_t i = 0; i < buf.size(); ++i)
            im.px[i] = unpack(buf[i]);
        write_image(models_dir(), "scene_" + name + "_" + f.tag,
                    upscale(im, 2));
    }

    vs->editor_authoring_view_ = false;
    vs->editor_floor_override_ = -1;
    world.delete_objects();
    world.set_floor_count(1);
}

} // namespace

TEST_F(VoxelModels, authored_figures)
{
    if (spike_dir().empty())
        GTEST_SKIP() << "set OG_VOXEL_SPIKE_DIR to record";

    ASSERT_TRUE(mount_scene_campaign("gladiator"));
    all_effects_off();
    GameWorld& world = scr()->world();
    world.delete_objects();
    world.id = 1;
    scr()->save_data.scen_num = 1;
    ASSERT_TRUE(scr()->load_level()) << "gladiator scen1";
    const std::vector<std::uint32_t> lut = build_palette_lut();

    std::map<int, og::render::VoxelModel> figures;
    std::string err;
    ASSERT_TRUE(load_figures(figures, err)) << err;

    printf("[voxel-art] authored figures from %s\n", voxel_art_dir().c_str());
    std::vector<Image> lineup, lineup_orig;
    int lh = 0, oh = 0;
    for (const FamilySpec& fs : kFamilies)
    {
        og::render::VoxelCarveFrames fr;
        if (!family_walk_frames(fs.family, fr))
            continue;
        const og::render::VoxelModel& model = figures[fs.family];
        const og::render::VoxelModel shadow = make_shadow(fr.w, fr.h, 16);

        const int cw = (fr.w + kFidelityPad * 2) * 6;
        const int ch = (fr.h + kFidelityPad * 2) * 6;
        Image page = make_image(cw * NUM_FACINGS, ch * 3, RGB{18, 18, 24});
        for (int d = 0; d < NUM_FACINGS; ++d)
        {
            Image src = make_image(fr.w + kFidelityPad * 2,
                                   fr.h + kFidelityPad * 2, RGB{18, 18, 24});
            draw_indices(src, kFidelityPad, kFidelityPad, fr.frame[d], fr.w,
                         fr.h, lut, kShotTeam);
            paste(page, d * cw, 0, upscale(src, 6));

            const float yaw = og::render::voxel_facing_yaw_rad(d);
            const Shot lo =
                shoot(&model, nullptr, yaw,
                      game_camera(fr.w, fr.h, 1.0f, kFidelityPad), 0.0f, 0.0f,
                      lut, kShotTeam, RGB{18, 18, 24}, nullptr);
            paste(page, d * cw, ch, upscale(lo.rgb, 6));
            const Shot hd =
                shoot(&model, nullptr, yaw,
                      game_camera(fr.w, fr.h, 6.0f, kFidelityPad), 0.0f, 0.0f,
                      lut, kShotTeam, RGB{18, 18, 24}, nullptr);
            paste(page, d * cw, ch * 2, hd.rgb);
        }
        write_image(models_dir(), std::string("fidelity_") + fs.name, page);
        printf("  %-9s %2dx%2dx%-2d  %4d voxels\n", fs.name, model.w, model.d,
               model.z, voxel_count(model));

        const float yaw0 = og::render::voxel_facing_yaw_rad(FACE_DOWN);
        ShotCamera lc = frame_model(model, yaw0, 20.0f, 45.0f, 6.0f, 10);
        lineup.push_back(shoot(&model, nullptr, yaw0, lc, model.anchor_x,
                               model.anchor_y, lut, kShotTeam, kPlateBg,
                               &shadow)
                             .rgb);
        lh = std::max(lh, lineup.back().h);
        {
            const int shown = shown_facing(FACE_DOWN, 20.0f);
            Image o = make_image(fr.w + 4, fr.h + 4, kPlateBg);
            draw_indices(o, 2, 2, fr.frame[shown], fr.w, fr.h, lut, kShotTeam);
            lineup_orig.push_back(upscale(o, 6));
            oh = std::max(oh, lineup_orig.back().h);
        }
        {
            ShotCamera fc = frame_model(model, yaw0, 20.0f, 45.0f, 6.0f, 10);
            ShotCamera bc = frame_model(model, yaw0, 200.0f, 45.0f, 6.0f, 10);
            const Image a = shoot(&model, nullptr, yaw0, fc, model.anchor_x,
                                  model.anchor_y, lut, kShotTeam, kPlateBg,
                                  &shadow)
                                .rgb;
            const Image b = shoot(&model, nullptr, yaw0, bc, model.anchor_x,
                                  model.anchor_y, lut, kShotTeam, kPlateBg,
                                  &shadow)
                                .rgb;
            Image hero = make_image(a.w + b.w + 6, std::max(a.h, b.h),
                                    kPlateBg);
            paste(hero, 0, hero.h - a.h, a);
            paste(hero, a.w + 6, hero.h - b.h, b);
            write_image(models_dir(), std::string("hero_") + fs.name, hero);
        }
    }

    const auto row = [&](std::vector<Image>& cells, int hh,
                         const std::string& nm) {
        int tw = 0;
        for (const Image& c : cells)
            tw += c.w + 4;
        Image im = make_image(tw, hh, kPlateBg);
        int x = 0;
        for (const Image& c : cells)
        {
            paste(im, x, hh - c.h, c);
            x += c.w + 4;
        }
        write_image(models_dir(), nm, im);
    };
    row(lineup, lh, "lineup");
    row(lineup_orig, oh, "lineup_orig");

    world.delete_objects();
    world.set_floor_count(1);
    run_figure_scene("gladiator_scen1", "gladiator", 1, figures);
    if (testing::Test::HasFatalFailure())
        return;
    run_figure_scene("westlands_scen1", "westlands", 1, figures);
}

namespace {

struct Box
{
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    bool edge = false;
};

void accumulate(Box& b, const Image& im, RGB bg)
{
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
        {
            if (im.px[static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(im.w) +
                      static_cast<std::size_t>(x)] == bg)
                continue;
            b.x0 = std::min(b.x0, x);
            b.x1 = std::max(b.x1, x);
            b.y0 = std::min(b.y0, y);
            b.y1 = std::max(b.y1, y);
            if (x == 0 || y == 0 || x == im.w - 1 || y == im.h - 1)
                b.edge = true;
        }
}

void write_sequence(const std::string& dir, const std::string& prefix,
                    const std::vector<Image>& frames, const Box& b, int pad,
                    int cw, int chh)
{
    const int x0 = std::max(0, b.x0 - pad);
    const int y0 = std::max(0, b.y0 - pad);
    const int w = std::min(cw - x0, b.x1 - b.x0 + 1 + pad * 2);
    const int h = std::min(chh - y0, b.y1 - b.y0 + 1 + pad * 2);
    char name[64];
    for (std::size_t i = 0; i < frames.size(); ++i)
    {
        snprintf(name, sizeof(name), "%s_%03d", prefix.c_str(),
                 static_cast<int>(i));
        write_image(dir, name, crop(frames[i], x0, y0, w, h, kPlateBg));
    }
    printf("    %-24s %d frames, cell %dx%d%s\n", prefix.c_str(),
           static_cast<int>(frames.size()), w, h,
           b.edge ? "   *** CLIPPED ***" : "");
}

} // namespace

TEST_F(VoxelModels, figure_animations)
{
    if (spike_dir().empty())
        GTEST_SKIP() << "set OG_VOXEL_SPIKE_DIR to record";

    const std::string dir = models_dir() + "/anim";
    constexpr int kCanvas = 260;
    ASSERT_TRUE(mount_scene_campaign("gladiator"));
    all_effects_off();
    GameWorld& world = scr()->world();
    world.delete_objects();
    world.id = 1;
    scr()->save_data.scen_num = 1;
    ASSERT_TRUE(scr()->load_level());
    const std::vector<std::uint32_t> lut = build_palette_lut();
    og::render::VoxelReliefCache cache(kTheta);

    std::map<int, og::render::VoxelModel> figures;
    std::string err;
    ASSERT_TRUE(load_figures(figures, err)) << err;

    printf("[voxel-art] animations\n");
    for (const FamilySpec& fs : kFamilies)
    {
        og::render::VoxelCarveFrames fr;
        if (!family_walk_frames(fs.family, fr))
            continue;
        const og::render::VoxelModel& model = figures[fs.family];
        const og::render::VoxelModel shadow = make_shadow(fr.w, fr.h, 16);
        const float yaw0 = og::render::voxel_facing_yaw_rad(FACE_DOWN);

        ShotCamera sc;
        sc.pitch = 45.0f;
        sc.scale = 6.0f;
        sc.w = kCanvas;
        sc.h = kCanvas;
        sc.view_cx = static_cast<float>(kCanvas) * 0.5f;
        sc.view_cy = static_cast<float>(kCanvas) * 0.62f;
        std::vector<Image> spin;
        Box box;
        for (int t = 0; t < 72; ++t)
        {
            sc.yaw = static_cast<float>(t * 5);
            spin.push_back(shoot(&model, nullptr, yaw0, sc,
                                 model.anchor_x, model.anchor_y, lut,
                                 kShotTeam, kPlateBg, &shadow)
                               .rgb);
            accumulate(box, spin.back(), kPlateBg);
        }
        write_sequence(dir, std::string("spin_") + fs.name, spin, box, 6,
                       kCanvas, kCanvas);

        if (std::string(fs.name) != "footman" && std::string(fs.name) != "orc")
            continue;

        // card vs solid vs art, on one orbit.
        std::array<const og::render::VoxelRelief*, NUM_FACINGS> rels{};
        for (int d = 0; d < NUM_FACINGS; ++d)
            rels[static_cast<std::size_t>(d)] = cache.get(
                fr.frame[d], fr.w, fr.h,
                og::render::kVoxelReliefDepthLiving);
        std::vector<Image> cmp;
        Box cbox;
        const int panel = kCanvas;
        for (int t = 0; t < 72; ++t)
        {
            const float yaw = static_cast<float>(t * 5);
            sc.yaw = yaw;
            const int shown = shown_facing(FACE_DOWN, yaw);
            const Image card =
                shoot(nullptr, rels[static_cast<std::size_t>(shown)],
                      plane_yaw_for(yaw), sc, static_cast<float>(fr.w) * 0.5f,
                      static_cast<float>(fr.h), lut, kShotTeam, kPlateBg,
                      &shadow)
                    .rgb;
            const Image solid =
                shoot(&model, nullptr, yaw0, sc, model.anchor_x,
                      model.anchor_y, lut, kShotTeam, kPlateBg, &shadow)
                    .rgb;
            Image art = make_image(panel, panel, kPlateBg);
            Image cell = make_image(fr.w, fr.h, kPlateBg);
            draw_indices(cell, 0, 0, fr.frame[shown], fr.w, fr.h, lut,
                         kShotTeam);
            const Image big = upscale(cell, 6);
            paste(art, (panel - big.w) / 2, (panel - big.h) / 2, big);
            Image three = make_image(panel * 3 + 24, panel, kPlateBg);
            paste(three, 0, 0, card);
            paste(three, panel + 12, 0, solid);
            paste(three, panel * 2 + 24, 0, art);
            cmp.push_back(std::move(three));
            accumulate(cbox, cmp.back(), kPlateBg);
        }
        write_sequence(dir, std::string("compare_") + fs.name, cmp, cbox, 6,
                       panel * 3 + 24, panel);

        if (std::string(fs.name) != "footman")
            continue;
        std::vector<Image> tilt;
        Box tbox;
        ShotCamera tc = sc;
        tc.yaw = 20.0f;
        for (int t = 0; t < 80; ++t)
        {
            const int step = (t < 40) ? t : (79 - t);
            tc.pitch = 90.0f - static_cast<float>(step) * (75.0f / 39.0f);
            tilt.push_back(shoot(&model, nullptr, yaw0, tc,
                                 model.anchor_x, model.anchor_y, lut,
                                 kShotTeam, kPlateBg, &shadow)
                               .rgb);
            accumulate(tbox, tilt.back(), kPlateBg);
        }
        write_sequence(dir, "tilt_footman", tilt, tbox, 6, kCanvas, kCanvas);
    }
}
