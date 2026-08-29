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
// Stage 2 round 6 (docs/voxel-render-design.md §12): per-facing sprite
// reliefs.
//
// The parametric rigs were rejected for inventing shapes and losing the art.
// A relief invents nothing: its front face IS the sprite frame, and the only
// addition is thickness taken from the silhouette's own distance transform.
// The plane is tilted to the game camera's elevation, so at that pitch the
// relief projects to exactly the pixels the stamp blits — the classic look is
// reproduced by construction. Turning the camera swaps which facing frame is
// shown, which is how the artists' side and back art gets used.
// ===========================================================================

namespace {

std::string models6_dir()
{
    return spike_dir() + "/models6";
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

// The team-band remap every walkputbuffer variant does (video_sdl.cpp:2131),
// so a sprite and a relief render can be compared on equal terms.
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

// Team 0's palette ramp base (mode_tick.cpp team_ramp_base: team * 16 + 40).
constexpr unsigned char kShotTeam = 40;
constexpr RGB kPlateBg{38, 40, 46};
constexpr float kTheta = og::render::kVoxelReliefTheta;

// The first walk frame of each facing: gloader.cpp's living tables are 8 rows
// of ANI_WALK followed by 8 of ANI_ATTACK (animan = bit1..bit8, att1..att8),
// and walker::animate() reads them as ani[curdir + ani_type * NUM_FACINGS],
// so row ANI_WALK * NUM_FACINGS + d is facing d's walk cycle.
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

// curdir order, from gloader.cpp's bit1..bit8 comments.
const char* kFacingNames[NUM_FACINGS] = {"up",   "up-r",   "right", "down-r",
                                         "down", "down-l", "left",  "up-l"};

// --------------------------------------------------------------------------
// The relief source: every entity of every order gets the facing frame that
// matches where it is pointing RELATIVE TO THE CAMERA.
// --------------------------------------------------------------------------
class ReliefModels : public og::render::VoxelModelSource
{
public:
    mutable og::render::VoxelReliefCache cache;
    std::map<int, og::render::VoxelModel> tiles;

    const og::render::VoxelRelief* relief_for(const walker& w,
                                              float camera_yaw_deg) const override
    {
        loader* const L = scr()->myloader;
        if (L == nullptr)
            return nullptr;
        const Order order = w.query_order();
        const int family = static_cast<int>(w.family());
        const int depth = og::render::voxel_relief_max_depth(order);

        int dir = static_cast<int>(static_cast<unsigned char>(w.curdir()));
        if (dir < 0 || dir >= NUM_FACINGS)
            dir = FACE_DOWN;
        // The camera turns the world by +yaw, so an entity's facing APPEARS
        // rotated by +yaw too: the frame to show is the one whose canonical
        // facing is entity_yaw + camera_yaw. (Checked against the raster's own
        // rotation: at yaw 90, world +x maps to screen-down, so a
        // right-facing walker must show its DOWN frame.)
        const float ent_yaw = static_cast<float>((dir - FACE_DOWN) * 45);
        int shown = FACE_DOWN + static_cast<int>(std::lround(
                                    (ent_yaw + camera_yaw_deg) / 45.0f));
        shown = ((shown % NUM_FACINGS) + NUM_FACINGS) % NUM_FACINGS;

        // Its own facing: take the walker's live frame pointer, which is by
        // definition the one the stamp would blit.
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

    const og::render::VoxelModel* living_model(const walker&,
                                               float&) const override
    {
        return nullptr; // rigs are withdrawn as the shape source (§12)
    }
    const og::render::VoxelModel* tile_model(int pix) const override
    {
        const auto it = tiles.find(pix);
        return it == tiles.end() ? nullptr : &it->second;
    }
};

// §10's terrain fixes, unchanged: terrain stays stage-1 extrusion.
void build_terrain_models(ReliefModels& out, const LevelVisuals& visuals)
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

// A flat dark ellipse at the relief's foot line, for the plates.
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

Shot shoot_relief(const og::render::VoxelRelief& r, const ShotCamera& sc,
                  const std::vector<std::uint32_t>& lut, unsigned char team,
                  RGB bg, const og::render::VoxelModel* shadow)
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
    v.relief = &r;
    v.x = 0.0f;
    v.y = 0.0f;
    v.z = 0.0f;
    v.material.team_color = team;
    scene.emit(v);

    og::render::VoxelCamera cam;
    cam.kind = og::render::VoxelCameraKind::Free;
    cam.cx = 0.0f;
    cam.cy = 0.0f;
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

// The camera that makes a relief land on its own frame, pixel for pixel.
// Derived in the raster: the frame's bottom edge sits on the ground at the
// foot line, so pixel (0,0) projects to h*sin(theta) - (h-1) plus the view
// centre; putting the view centre at the negative of that lands it on (0,0)
// and every pixel (u,v) on exactly (u, v).
ShotCamera exact_camera(const og::render::VoxelRelief& r, float scale)
{
    const float sp = std::sin(kTheta * 3.14159265358979f / 180.0f);
    ShotCamera sc;
    sc.yaw = 0.0f;
    sc.pitch = kTheta;
    sc.scale = scale;
    sc.w = static_cast<int>(std::lround(static_cast<float>(r.w) * scale));
    sc.h = static_cast<int>(std::lround(static_cast<float>(r.h) * scale));
    sc.view_cx = 0.0f;
    sc.view_cy = (static_cast<float>(r.h - 1) -
                  static_cast<float>(r.h) * sp) * scale;
    return sc;
}

struct Extent
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
};

// Screen bounds of a relief at scale 1 with the camera aimed at the origin.
Extent relief_extent(const og::render::VoxelRelief& r, float yaw, float pitch)
{
    const float rad = 3.14159265358979f / 180.0f;
    const float sp = std::sin(kTheta * rad), cp = std::cos(kTheta * rad);
    const float cy = std::cos(yaw * rad), sy = std::sin(yaw * rad);
    const float ux = cy, uy = -sy, uz = 0.0f;
    const float vx = sp * sy, vy = sp * cy, vz = -cp;
    const float tx = -cp * sy, ty = -cp * cy, tz = -sp;
    const float ax = static_cast<float>(r.w) * 0.5f;
    const float ay = static_cast<float>(r.h);
    const float ox = ax - ax * ux - static_cast<float>(r.h - 1) * vx;
    const float oy = ay - ax * uy - static_cast<float>(r.h - 1) * vy;
    const float oz = 0.0f - ax * uz - static_cast<float>(r.h - 1) * vz;
    const float csp = std::sin(pitch * rad), ccp = std::cos(pitch * rad);
    const float ccy = std::cos(yaw * rad), csy = std::sin(yaw * rad);
    Extent e;
    for (int c = 0; c < 8; ++c)
    {
        const float fu = (c & 1) ? static_cast<float>(r.w) : 0.0f;
        const float fv = (c & 2) ? static_cast<float>(r.h) : 0.0f;
        const float ft = (c & 4) ? static_cast<float>(r.depth - 1) : 0.0f;
        const float X = ox + fu * ux + fv * vx + ft * tx;
        const float Y = oy + fu * uy + fv * vy + ft * ty;
        const float Z = oz + fu * uz + fv * vz + ft * tz;
        const float rx = X * ccy - Y * csy;
        const float ry = X * csy + Y * ccy;
        const float sx = rx;
        const float syy = ry * csp - Z * ccp;
        e.x0 = std::min(e.x0, sx);
        e.x1 = std::max(e.x1, sx);
        e.y0 = std::min(e.y0, syy);
        e.y1 = std::max(e.y1, syy);
    }
    return e;
}

ShotCamera frame_relief(const og::render::VoxelRelief& r, float yaw,
                        float pitch, float scale, int pad)
{
    const Extent e = relief_extent(r, yaw, pitch);
    ShotCamera sc;
    sc.yaw = yaw;
    sc.pitch = pitch;
    sc.scale = scale;
    sc.w = static_cast<int>(std::ceil((e.x1 - e.x0) * scale)) + pad * 2;
    sc.h = static_cast<int>(std::ceil((e.y1 - e.y0) * scale)) + pad * 2;
    sc.view_cx = -e.x0 * scale + static_cast<float>(pad);
    sc.view_cy = -e.y0 * scale + static_cast<float>(pad);
    return sc;
}

// Which facing frame a down-facing entity shows to a camera at this yaw.
int shown_facing(int entity_dir, float camera_yaw_deg)
{
    const float ent = static_cast<float>((entity_dir - FACE_DOWN) * 45);
    int shown = FACE_DOWN + static_cast<int>(std::lround(
                                (ent + camera_yaw_deg) / 45.0f));
    return ((shown % NUM_FACINGS) + NUM_FACINGS) % NUM_FACINGS;
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

// Where the fighters actually are: the living walker with the most livings
// within one screen-ish radius.
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

// Free renders of a real level with a relief on every entity. The scene is
// rebuilt per view because which facing frame each relief shows depends on
// where the camera is standing.
void run_relief_scene(const std::string& name, const char* campaign, int level)
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
    // set_level_draw_pos only reaches viewscreen::topx/topy through a draw
    // pass, so the scene window has to be taken AFTER one redraw.
    ASSERT_TRUE(vs->redraw(&scr()->level_runtime_data(), false));

    const std::vector<std::uint32_t> lut = build_palette_lut();
    ReliefModels models;
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
        float scale;
        bool crowd;
    };
    const FreeSpec views[] = {
        {"y30_p60", 30.0f, 60.0f, 1.0f, false},
        {"crowd_z3_y30_p60", 30.0f, 60.0f, 3.0f, true},
        {"crowd_z3_y45_p45", 45.0f, 45.0f, 3.0f, true},
        {"crowd_z3_y00_p55", 0.0f, kTheta, 3.0f, true},
    };
    int relief_volumes = 0;
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
        const og::render::VoxelSceneBuildStats bs =
            og::render::build_voxel_scene(scene, world, scr()->level_visuals(),
                                          bp);
        relief_volumes = 0;
        for (const auto& v : scene.volumes())
            if (v.relief != nullptr)
                ++relief_volumes;

        std::vector<std::uint32_t> buf(
            static_cast<std::size_t>(kClassicW) * kClassicH, 0xFF000000u);
        og::render::VoxelCamera cam;
        cam.kind = og::render::VoxelCameraKind::Free;
        cam.cx = f.crowd ? crowd_x : wide_x;
        cam.cy = f.crowd ? crowd_y : wide_y;
        cam.yaw_deg = f.yaw;
        cam.pitch_deg = f.pitch;
        cam.scale = f.scale;
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
        const og::render::VoxelRasterStats rs = raster.render(scene, cam, rt);
        // No silhouette darkening: the sprites carry their own outlines (§12).

        Image im = make_image(kClassicW, kClassicH, RGB{0, 0, 0});
        for (std::size_t i = 0; i < buf.size(); ++i)
            im.px[i] = unpack(buf[i]);
        write_image(models6_dir(), "scene_" + name + "_" + f.tag,
                    upscale(im, 2));
        if (f.tag == std::string("y30_p60"))
            printf("[voxel-relief] scene %s (%s scen%d): %d tiles, %d decor, "
                   "%d entities, %d reliefs; crowd at (%.0f, %.0f)\n",
                   name.c_str(), campaign, level, bs.tiles, bs.decor,
                   bs.entities, relief_volumes,
                   static_cast<double>(crowd_x), static_cast<double>(crowd_y));
        printf("  %-18s %llu quads, %llu samples, %llu writes\n", f.tag,
               static_cast<unsigned long long>(rs.slices),
               static_cast<unsigned long long>(rs.pixel_samples),
               static_cast<unsigned long long>(rs.pixels_written));
    }
    printf("  relief cache: %d frames\n",
           static_cast<int>(models.cache.size()));

    vs->editor_authoring_view_ = false;
    vs->editor_floor_override_ = -1;
    world.delete_objects();
    world.set_floor_count(1);
}

} // namespace

TEST_F(VoxelModels, sprite_reliefs_and_scene_renders)
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

    printf("[voxel-relief] per-facing sprite reliefs, plane tilted to "
           "theta %.0f, thickness = clamp(EDT, 1, %d) for livings\n",
           static_cast<double>(kTheta),
           og::render::kVoxelReliefDepthLiving);

    og::render::VoxelReliefCache cache;
    std::map<int, std::array<const og::render::VoxelRelief*, NUM_FACINGS>> built;
    std::vector<Image> lineup, lineup_orig;
    int lineup_h = 0, lineup_orig_h = 0;

    for (const FamilySpec& fs : kFamilies)
    {
        og::render::VoxelCarveFrames fr;
        if (!family_walk_frames(fs.family, fr))
        {
            printf("  %-9s : no eight-facing walk art, skipped\n", fs.name);
            continue;
        }
        std::array<const og::render::VoxelRelief*, NUM_FACINGS> rels{};
        for (int d = 0; d < NUM_FACINGS; ++d)
            rels[static_cast<std::size_t>(d)] = cache.get(
                fr.frame[d], fr.w, fr.h,
                og::render::kVoxelReliefDepthLiving);
        built.emplace(fs.family, rels);

        // ---- fidelity page ----
        const int cw = fr.w * 6;
        const int ch = fr.h * 6;
        Image page = make_image(cw * NUM_FACINGS, ch * 3, RGB{18, 18, 24});
        double sum = 0.0;
        int worst = 0;
        double per[NUM_FACINGS] = {};
        for (int d = 0; d < NUM_FACINGS; ++d)
        {
            const og::render::VoxelRelief& r =
                *rels[static_cast<std::size_t>(d)];
            Image src = make_image(fr.w, fr.h, RGB{18, 18, 24});
            draw_indices(src, 0, 0, fr.frame[d], fr.w, fr.h, lut, kShotTeam);
            paste(page, d * cw, 0, upscale(src, 6));

            const Shot one = shoot_relief(r, exact_camera(r, 1.0f), lut,
                                          kShotTeam, RGB{18, 18, 24}, nullptr);
            paste(page, d * cw, ch, upscale(one.rgb, 6));

            const Shot hd = shoot_relief(r, exact_camera(r, 6.0f), lut,
                                         kShotTeam, RGB{18, 18, 24}, nullptr);
            paste(page, d * cw, ch * 2, hd.rgb);

            int matched = 0, uni = 0;
            for (int i = 0; i < fr.w * fr.h; ++i)
            {
                const unsigned char raw =
                    fr.frame[d][static_cast<std::size_t>(i)];
                const unsigned char a = remap_team(raw, kShotTeam);
                const bool sa = raw != 0;
                const unsigned char b = one.index[static_cast<std::size_t>(i)];
                const bool sb = b != 0;
                if (sa || sb)
                    ++uni;
                if (sa && sb && a == b)
                    ++matched;
            }
            per[d] = uni > 0 ? 100.0 * static_cast<double>(matched) /
                    static_cast<double>(uni)
                             : 100.0;
            sum += per[d];
            if (per[d] < per[worst])
                worst = d;
        }
        write_image(models6_dir(), std::string("fidelity_") + fs.name, page);
        printf("  %-9s sprite %2dx%-2d  fidelity mean %.2f%%  worst %s "
               "%.2f%%  per-facing:",
               fs.name, fr.w, fr.h, sum / NUM_FACINGS, kFacingNames[worst],
               per[worst]);
        for (int d = 0; d < NUM_FACINGS; ++d)
            printf(" %.0f", per[d]);
        printf("\n");

        // ---- lineup cell: pitch 55, yaw 30, scale 6, on a shadow ----
        const int shown = shown_facing(FACE_DOWN, 30.0f);
        const og::render::VoxelRelief& lr =
            *rels[static_cast<std::size_t>(shown)];
        const og::render::VoxelModel shadow = make_shadow(fr.w, fr.h, 16);
        ShotCamera lc = frame_relief(lr, 30.0f, kTheta, 6.0f, 10);
        lineup.push_back(
            shoot_relief(lr, lc, lut, kShotTeam, kPlateBg, &shadow).rgb);
        lineup_h = std::max(lineup_h, lineup.back().h);
        Image orig = make_image(fr.w + 4, fr.h + 4, kPlateBg);
        draw_indices(orig, 2, 2, fr.frame[shown], fr.w, fr.h, lut, kShotTeam);
        lineup_orig.push_back(upscale(orig, 6));
        lineup_orig_h = std::max(lineup_orig_h, lineup_orig.back().h);

        // ---- turntable: 16 camera yaws, the entity keeps facing down ----
        {
            constexpr int kFrames = 16;
            Extent u;
            for (int f = 0; f < kFrames; ++f)
            {
                const int sd = shown_facing(FACE_DOWN,
                                            static_cast<float>(f) * 22.5f);
                const Extent e = relief_extent(
                    *rels[static_cast<std::size_t>(sd)],
                    static_cast<float>(f) * 22.5f, kTheta);
                u.x0 = std::min(u.x0, e.x0);
                u.x1 = std::max(u.x1, e.x1);
                u.y0 = std::min(u.y0, e.y0);
                u.y1 = std::max(u.y1, e.y1);
            }
            ShotCamera sc;
            sc.pitch = kTheta;
            sc.scale = 4.0f;
            const int pad = 6;
            sc.w = static_cast<int>(std::ceil((u.x1 - u.x0) * sc.scale)) +
                pad * 2;
            sc.h = static_cast<int>(std::ceil((u.y1 - u.y0) * sc.scale)) +
                pad * 2;
            sc.view_cx = -u.x0 * sc.scale + static_cast<float>(pad);
            sc.view_cy = -u.y0 * sc.scale + static_cast<float>(pad);
            Image strip = make_image(sc.w * kFrames, sc.h, kPlateBg);
            for (int f = 0; f < kFrames; ++f)
            {
                sc.yaw = static_cast<float>(f) * 22.5f;
                const int sd = shown_facing(FACE_DOWN, sc.yaw);
                paste(strip, f * sc.w, 0,
                      shoot_relief(*rels[static_cast<std::size_t>(sd)], sc, lut,
                                   kShotTeam, kPlateBg, &shadow)
                          .rgb);
            }
            write_image(models6_dir(), std::string("turntable_") + fs.name,
                        strip);
        }

        // ---- lean: the relief seen away from its own tilt ----
        if (std::string(fs.name) == "footman" || std::string(fs.name) == "orc")
        {
            Image lean = make_image(0, 0, kPlateBg);
            std::vector<Image> cells;
            int hh = 0;
            // 25 is outside the design's 40-70 range and is here only as
            // evidence that the thickness mechanism works at all: between 40
            // and 70 the lean is about a pixel and a half and invisible.
            for (float pitch : {25.0f, 40.0f, kTheta, 70.0f})
            {
                ShotCamera pc = frame_relief(lr, 30.0f, pitch, 6.0f, 10);
                cells.push_back(
                    shoot_relief(lr, pc, lut, kShotTeam, kPlateBg, &shadow)
                        .rgb);
                hh = std::max(hh, cells.back().h);
            }
            int tw = 0;
            for (const Image& c : cells)
                tw += c.w + 6;
            lean = make_image(tw, hh, kPlateBg);
            int x = 0;
            for (const Image& c : cells)
            {
                paste(lean, x, hh - c.h, c);
                x += c.w + 6;
            }
            write_image(models6_dir(), std::string("lean_") + fs.name, lean);
        }
    }

    const auto row = [&](std::vector<Image>& cells, int hh,
                         const std::string& name) {
        if (cells.empty())
            return;
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
        write_image(models6_dir(), name, im);
    };
    row(lineup, lineup_h, "lineup");
    row(lineup_orig, lineup_orig_h, "lineup_orig");

    world.delete_objects();
    world.set_floor_count(1);
    run_relief_scene("gladiator_scen1", "gladiator", 1);
    if (testing::Test::HasFatalFailure())
        return;
    run_relief_scene("westlands_scen1", "westlands", 1);
}
