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
#include <openglad/interface/render/voxel_rig.h>

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
// Stage 2 (docs/voxel-render-design.md §10): space-carved facing models.
//
// The eight walk frames of a family are eight rotations of one character seen
// by one camera, so the model is reconstructed by carving, not authored. §10's
// raised bar makes the MODEL the product: it has to read as a better version
// of the sprite, not a degraded one. So this harness carves supersampled,
// renders through the real cube-face rasterizer, and dumps the pages a human
// judges — hero shots, turntables, the honest classic-angle comparison, and
// scenes with the models turned by curdir.
// ===========================================================================

namespace {

std::string models4_dir()
{
    return spike_dir() + "/models4";
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

// The team-band remap every walkputbuffer variant does (video_sdl.cpp:2131),
// so a sprite and a model render can be compared on equal terms.
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

struct FamilySpec
{
    const char* name;
    int family;
};

// curdir order, from gloader.cpp's bit1..bit8 comments.
const char* kFacingNames[NUM_FACINGS] = {"up",   "up-r",   "right", "down-r",
                                         "down", "down-l", "left",  "up-l"};

// The carved models the scene renders draw through, keyed by living family.
class SpikeModels : public og::render::VoxelModelSource
{
public:
    std::map<int, og::render::VoxelModel> living;
    std::map<int, og::render::VoxelModel> tiles;

    const og::render::VoxelModel* living_model(const walker& w,
                                               float& yaw_rad) const override
    {
        const auto it = living.find(static_cast<int>(w.family()));
        if (it == living.end())
            return nullptr;
        // curdir is the facing index; FACE_DOWN faces the viewer, so yaw is
        // (curdir - FACE_DOWN) * 45 degrees (voxel_scene.h).
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

// §10's terrain fixes, built from the live level art.
void build_terrain_models(SpikeModels& out, const LevelVisuals& visuals)
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
        // Trunk: a 4x4 centre column for the lower 12 voxels. Canopy: the
        // full footprint for the top 8. The base slice is GRASS, so the
        // overhang shows ground under it instead of a black trench.
        og::render::VoxelModel m = og::render::voxel_build_tree_model(
            top.data.get(), top.w, top.h,
            grass.valid() ? grass.data.get() : nullptr,
            grass.valid() ? grass.w : 0, grass.valid() ? grass.h : 0, 12, 8, 4);
        if (!m.empty())
            out.tiles.emplace(pix, std::move(m));
    }
}

// --------------------------------------------------------------------------
// Rendering a single model through the REAL rasterizer, so hero shots and the
// sprite-agreement number both come from the product path.
// --------------------------------------------------------------------------
struct ShotCamera
{
    float cam_yaw_deg = 0.0f;
    float pitch_deg = og::render::kVoxelCarveTheta;
    float scale = 1.0f;
    int w = 0;
    int h = 0;
    float view_cx = 0.0f;
    float view_cy = 0.0f;
    bool outline = true;
};

struct ModelShot
{
    Image rgb;
    std::vector<unsigned char> index;
};

std::uint32_t pack(RGB c)
{
    return 0xFF000000u | (static_cast<std::uint32_t>(c.r) << 16) |
        (static_cast<std::uint32_t>(c.g) << 8) | c.b;
}

ModelShot shoot_model(const og::render::VoxelModel& m, float model_yaw,
                      const ShotCamera& sc,
                      const std::vector<std::uint32_t>& lut,
                      unsigned char team, RGB bg = RGB{0, 0, 0},
                      const og::render::VoxelModel* shadow = nullptr)
{
    ModelShot out;
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
        sv.yaw = 0.0f;
        sv.x = -shadow->anchor_x;
        sv.y = -shadow->anchor_y;
        sv.z = -1.0f; // just under the figure's feet
        sv.material.team_color = team;
        scene.emit(sv);
    }
    og::render::VoxelVolume v;
    v.model = &m;
    v.yaw = model_yaw;
    // The raster puts the footprint centre at (v.x + anchor_x, v.y + anchor_y);
    // park that centre on the world origin so the camera can aim at (0, 0).
    v.x = -m.anchor_x;
    v.y = -m.anchor_y;
    v.z = 0.0f;
    v.material.team_color = team;
    scene.emit(v);

    og::render::VoxelCamera cam;
    cam.kind = og::render::VoxelCameraKind::Free;
    cam.cx = 0.0f;
    cam.cy = 0.0f;
    cam.yaw_deg = sc.cam_yaw_deg;
    cam.pitch_deg = sc.pitch_deg;
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
    if (sc.outline)
        raster.edge_darken(rt, 1.0f, og::render::kVoxelEdgeShade);
    for (std::size_t i = 0; i < buf.size(); ++i)
        out.rgb.px[i] = unpack(buf[i]);
    return out;
}

struct Extent
{
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

// Screen bounding box of a model at scale 1, about its footprint centre.
// Measured over the OCCUPIED cells, not the grid: a carve leaves most of its
// grid empty, and framing on the grid box shrinks the figure to a speck in
// the middle of a large canvas.
Extent model_extent(const og::render::VoxelModel& m, float model_yaw,
                    float cam_yaw_deg, float pitch_deg)
{
    const float rad = 3.14159265358979f / 180.0f;
    const float cm = std::cos(model_yaw), sm = std::sin(model_yaw);
    const float cc = std::cos(cam_yaw_deg * rad), sc = std::sin(cam_yaw_deg * rad);
    const float sp = std::sin(pitch_deg * rad), cp = std::cos(pitch_deg * rad);
    int i0 = m.w, i1 = -1, j0 = m.d, j1 = -1, k0 = m.z, k1 = -1;
    for (int k = 0; k < m.z; ++k)
        for (int j = 0; j < m.d; ++j)
            for (int i = 0; i < m.w; ++i)
            {
                if (m.occ[m.at(i, j, k)] == 0)
                    continue;
                i0 = std::min(i0, i);
                i1 = std::max(i1, i);
                j0 = std::min(j0, j);
                j1 = std::max(j1, j);
                k0 = std::min(k0, k);
                k1 = std::max(k1, k);
            }
    if (i1 < 0)
    {
        i0 = 0;
        i1 = m.w - 1;
        j0 = 0;
        j1 = m.d - 1;
        k0 = 0;
        k1 = m.z - 1;
    }
    const float hw = m.extent_x() * 0.5f, hd = m.extent_y() * 0.5f;
    const float lox = static_cast<float>(i0) * m.cell - hw;
    const float hix = static_cast<float>(i1 + 1) * m.cell - hw;
    const float loy = static_cast<float>(j0) * m.cell - hd;
    const float hiy = static_cast<float>(j1 + 1) * m.cell - hd;
    const float loz = static_cast<float>(k0) * m.cell;
    const float hiz = static_cast<float>(k1 + 1) * m.cell;
    Extent e{1e9f, 1e9f, -1e9f, -1e9f};
    for (int c = 0; c < 8; ++c)
    {
        const float lx = (c & 1) ? hix : lox;
        const float ly = (c & 2) ? hiy : loy;
        const float lz = (c & 4) ? hiz : loz;
        const float wx = lx * cm - ly * sm;
        const float wy = lx * sm + ly * cm;
        const float rx = wx * cc - wy * sc;
        const float ry = wx * sc + wy * cc;
        const float sx = rx;
        const float sy = ry * sp - lz * cp;
        e.x0 = std::min(e.x0, sx);
        e.x1 = std::max(e.x1, sx);
        e.y0 = std::min(e.y0, sy);
        e.y1 = std::max(e.y1, sy);
    }
    return e;
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

// Team 0's palette ramp base (mode_tick.cpp team_ramp_base: team * 16 + 40).
// Both the sprite row and the rig render go through it, so the comparison is
// like for like.
constexpr unsigned char kShotTeam = 40;
// Hero and turntable plates sit on a neutral ground so the drop shadow reads.
constexpr RGB kPlateBg{38, 40, 46};

// --------------------------------------------------------------------------
// Palette: the rigs take their colours from the family's own sprite. Nothing
// else about the sprite survives into a rig, so this is the only place the
// art still speaks.
// --------------------------------------------------------------------------
struct PaletteChoice
{
    og::render::RigPalette pal;
    std::string note;
};

PaletteChoice pick_palette(const og::render::VoxelCarveFrames& fr,
                           const std::vector<std::uint32_t>& lut)
{
    std::array<int, 256> hist{};
    for (int d = 0; d < NUM_FACINGS; ++d)
    {
        if (fr.frame[d] == nullptr)
            continue;
        for (int i = 0; i < fr.w * fr.h; ++i)
            ++hist[fr.frame[d][static_cast<std::size_t>(i)]];
    }
    hist[0] = 0;
    int total = 0;
    for (int c : hist)
        total += c;

    const auto rgb = [&](int i) {
        const std::uint32_t c = lut[static_cast<std::size_t>(i)];
        return RGB{static_cast<Uint8>((c >> 16) & 0xFFu),
                   static_cast<Uint8>((c >> 8) & 0xFFu),
                   static_cast<Uint8>(c & 0xFFu)};
    };
    const auto lum = [&](int i) {
        const RGB c = rgb(i);
        return 0.30f * static_cast<float>(c.r) + 0.59f * static_cast<float>(c.g) +
            0.11f * static_cast<float>(c.b);
    };
    const auto sat = [&](int i) {
        const RGB c = rgb(i);
        const int v = std::max(c.r, std::max(c.g, c.b));
        const int mn = std::min(c.r, std::min(c.g, c.b));
        return v > 0 ? static_cast<float>(v - mn) / static_cast<float>(v)
                     : 0.0f;
    };

    // This palette is laid out in eight-entry ramps, so a "colour" is a
    // BLOCK, not an index: picking two indices out of one ramp gives two
    // shades of the same thing, which is what made round-3's first pass
    // monochrome. Choose blocks first, then shades inside them.
    std::array<int, 32> block{};
    for (int i = 1; i < 248; ++i)
        block[static_cast<std::size_t>(i / 8)] += hist[static_cast<std::size_t>(i)];

    const auto top_of_block = [&](int b) {
        int win = 0, wc = -1;
        for (int i = b * 8; i < b * 8 + 8 && i < 248; ++i)
            if (hist[static_cast<std::size_t>(i)] > wc)
            {
                wc = hist[static_cast<std::size_t>(i)];
                win = i;
            }
        return win;
    };
    const auto lightest_of_block = [&](int b) {
        int win = b * 8;
        for (int i = b * 8; i < b * 8 + 8 && i < 248; ++i)
            if (lum(i) > lum(win))
                win = i;
        return win;
    };
    const auto darkest_of_block = [&](int b) {
        int win = b * 8;
        for (int i = b * 8; i < b * 8 + 8 && i < 248; ++i)
            if (lum(i) < lum(win))
                win = i;
        return win;
    };

    og::render::RigPalette p;

    // Team band first: whatever the sprite paints in 248..255 is what the
    // game recolours per team, so the rig has to spend it somewhere visible.
    int team_best = 0, team_count = 0;
    for (int i = 248; i < 256; ++i)
        if (hist[static_cast<std::size_t>(i)] > team_count)
        {
            team_count = hist[static_cast<std::size_t>(i)];
            team_best = i;
        }
    p.team = static_cast<unsigned char>(team_best);

    // Skin: an orange-ish mid tone. Its whole ramp is then off limits for
    // cloth, or the figure comes out one colour from head to boot.
    int skin = 0, skin_count = 0;
    for (int i = 1; i < 248; ++i)
    {
        const RGB c = rgb(i);
        if (c.r <= c.g || c.g < c.b || sat(i) < 0.18f || sat(i) > 0.85f ||
            lum(i) < 70.0f)
            continue;
        if (hist[static_cast<std::size_t>(i)] > skin_count)
        {
            skin_count = hist[static_cast<std::size_t>(i)];
            skin = i;
        }
    }
    const int skin_block = skin > 0 ? skin / 8 : -1;

    std::array<int, 32> order{};
    for (int i = 0; i < 32; ++i)
        order[static_cast<std::size_t>(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a2, int b2) {
        return block[static_cast<std::size_t>(a2)] >
            block[static_cast<std::size_t>(b2)];
    });

    int primary_block = -1, secondary_block = -1, grey_block = -1;
    for (int b : order)
    {
        if (block[static_cast<std::size_t>(b)] <= 0)
            continue;
        const int t = top_of_block(b);
        if (grey_block < 0 && sat(t) < 0.18f && lum(t) > 60.0f)
            grey_block = b;
        if (b == skin_block)
            continue;
        if (primary_block < 0)
            primary_block = b;
        // A second ramp only counts as a second COLOUR if the sprite spends
        // real area on it; otherwise it is outline pixels, and taking it
        // paints the legs a hue that appears nowhere on the character.
        else if (secondary_block < 0 &&
                 block[static_cast<std::size_t>(b)] * 8 >= total)
            secondary_block = b;
    }
    // A family drawn almost entirely in its own skin ramp (the orc) has
    // nothing else to spend, so keep the ramp and separate the parts by shade.
    const bool thin = primary_block < 0 ||
        block[static_cast<std::size_t>(primary_block)] * 5 < total;
    if (thin && skin_block >= 0)
    {
        secondary_block = primary_block;
        primary_block = skin_block;
    }
    if (primary_block < 0)
        primary_block = skin_block >= 0 ? skin_block : 2;

    p.skin = static_cast<unsigned char>(
        skin > 0 ? skin : lightest_of_block(primary_block));
    p.primary = static_cast<unsigned char>(top_of_block(primary_block));
    if (p.primary == p.skin)
        p.primary = static_cast<unsigned char>(darkest_of_block(primary_block));
    p.secondary = static_cast<unsigned char>(
        secondary_block >= 0 ? top_of_block(secondary_block)
                             : darkest_of_block(primary_block));
    if (p.secondary == p.primary)
        p.secondary = static_cast<unsigned char>(
            lightest_of_block(primary_block));
    p.metal = static_cast<unsigned char>(
        grey_block >= 0 && grey_block != primary_block
            ? lightest_of_block(grey_block)
            : lightest_of_block(primary_block));
    // G3: armour and cloth one ramp step darker than the sprite's own mid
    // tone, so the lightest-metal blade and the bare skin both separate from
    // it. Ramps do not all run the same direction, so step by luminance.
    {
        const int blk = p.primary / 8;
        int step = p.primary;
        float best_gap = 1e9f;
        for (int i = blk * 8; i < blk * 8 + 8 && i < 248; ++i)
        {
            const float gap = lum(p.primary) - lum(i);
            if (gap > 1.0f && gap < best_gap)
            {
                best_gap = gap;
                step = i;
            }
        }
        p.primary = static_cast<unsigned char>(step);
    }

    // Eye pits and boots want to be near black. Take the darkest thing in the
    // whole palette rather than the darkest thing the sprite happened to use:
    // a rig is not trying to reproduce the sprite's index set.
    int dark = 1;
    for (int i = 1; i < 248; ++i)
        if (lum(i) < lum(dark))
            dark = i;
    p.dark = static_cast<unsigned char>(dark);
    p.accent = p.team != 0 ? p.team : p.secondary;

    char buf[320];
    const auto show = [&](const char* n, unsigned char i) {
        const RGB c = rgb(i);
        snprintf(buf, sizeof(buf), "%s=%u(%u,%u,%u) ", n, i, c.r, c.g, c.b);
        return std::string(buf);
    };
    PaletteChoice out;
    out.pal = p;
    out.note = show("skin", p.skin) + show("primary", p.primary) +
        show("secondary", p.secondary) + show("metal", p.metal) +
        show("dark", p.dark) + show("accent", p.accent);
    out.note += p.team != 0 ? show("team", p.team) : std::string("team=none");
    return out;
}

// --------------------------------------------------------------------------
// Plates
// --------------------------------------------------------------------------
ShotCamera frame_at_scale(const og::render::VoxelModel& m, float model_yaw,
                          float cam_yaw_deg, float pitch_deg, float scale,
                          int pad)
{
    const Extent e = model_extent(m, model_yaw, cam_yaw_deg, pitch_deg);
    ShotCamera sc;
    sc.cam_yaw_deg = cam_yaw_deg;
    sc.pitch_deg = pitch_deg;
    sc.scale = scale;
    sc.w = static_cast<int>(std::ceil((e.x1 - e.x0) * scale)) + pad * 2;
    sc.h = static_cast<int>(std::ceil((e.y1 - e.y0) * scale)) + pad * 2;
    sc.view_cx = -e.x0 * scale + static_cast<float>(pad);
    sc.view_cy = -e.y0 * scale + static_cast<float>(pad);
    return sc;
}

// hero_<family>.png — front and back, on a ground shadow, at a size a human
// can actually judge.
Image build_hero(const og::render::VoxelModel& m,
                 const og::render::VoxelModel& shadow,
                 const std::vector<std::uint32_t>& lut)
{
    const float pitch = 35.0f;
    const float scale = 6.0f;
    const ShotCamera front = frame_at_scale(m, 0.0f, 30.0f, pitch, scale, 14);
    const ShotCamera back = frame_at_scale(m, 0.0f, 210.0f, pitch, scale, 14);
    const ModelShot a =
        shoot_model(m, 0.0f, front, lut, kShotTeam, kPlateBg, &shadow);
    const ModelShot b =
        shoot_model(m, 0.0f, back, lut, kShotTeam, kPlateBg, &shadow);
    Image im = make_image(a.rgb.w + b.rgb.w + 6, std::max(a.rgb.h, b.rgb.h),
                          kPlateBg);
    paste(im, 0, im.h - a.rgb.h, a.rgb);
    paste(im, a.rgb.w + 6, im.h - b.rgb.h, b.rgb);
    return upscale(im, 2);
}

// turntable_<family>.png — 16 yaws at 22.5 degrees on one shared frame.
Image build_turntable(const og::render::VoxelModel& m,
                      const og::render::VoxelModel& shadow,
                      const std::vector<std::uint32_t>& lut)
{
    constexpr int kFrames = 16;
    const float pitch = 40.0f;
    const float scale = 4.0f;
    Extent u{1e9f, 1e9f, -1e9f, -1e9f};
    for (int f = 0; f < kFrames; ++f)
    {
        const Extent e =
            model_extent(m, 0.0f, static_cast<float>(f) * 22.5f, pitch);
        u.x0 = std::min(u.x0, e.x0);
        u.x1 = std::max(u.x1, e.x1);
        u.y0 = std::min(u.y0, e.y0);
        u.y1 = std::max(u.y1, e.y1);
    }
    ShotCamera sc;
    sc.pitch_deg = pitch;
    sc.scale = scale;
    const int pad = 6;
    sc.w = static_cast<int>(std::ceil((u.x1 - u.x0) * scale)) + pad * 2;
    sc.h = static_cast<int>(std::ceil((u.y1 - u.y0) * scale)) + pad * 2;
    sc.view_cx = -u.x0 * scale + static_cast<float>(pad);
    sc.view_cy = -u.y0 * scale + static_cast<float>(pad);

    Image im = make_image(sc.w * kFrames, sc.h, kPlateBg);
    for (int f = 0; f < kFrames; ++f)
    {
        sc.cam_yaw_deg = static_cast<float>(f) * 22.5f;
        const ModelShot shot =
            shoot_model(m, 0.0f, sc, lut, kShotTeam, kPlateBg, &shadow);
        paste(im, f * sc.w, 0, shot.rgb);
    }
    return upscale(im, 2);
}

// classic_<family>.png — the "could it work under the normal camera" page.
//   row 1  the eight source facings, 8x nearest
//   row 2  the rig at the assumed game camera, 1x, upscaled 8x: this is the
//          candidate replacement sprite
//   row 3  the same camera rendered natively at 4x — the HD version
Image build_classic_page(const og::render::VoxelCarveFrames& fr,
                         const og::render::VoxelModel& m,
                         const std::vector<std::uint32_t>& lut,
                         float* agreement_out)
{
    const int cell_w = fr.w * 8;
    const int cell_h = fr.h * 8;
    Image im = make_image(cell_w * NUM_FACINGS, cell_h * 3, RGB{18, 18, 24});
    for (int d = 0; d < NUM_FACINGS; ++d)
    {
        const float yaw = og::render::voxel_facing_yaw_rad(d);

        Image src = make_image(fr.w, fr.h, RGB{18, 18, 24});
        draw_indices(src, 0, 0, fr.frame[d], fr.w, fr.h, lut, kShotTeam);
        paste(im, d * cell_w, 0, upscale(src, 8));

        ShotCamera one;
        one.cam_yaw_deg = 0.0f;
        one.pitch_deg = m.theta_deg;
        one.scale = 1.0f;
        one.w = fr.w;
        one.h = fr.h;
        one.view_cx = m.anchor_x;
        one.view_cy = m.anchor_y;
        one.outline = false; // 16 px tall: an outline would eat the figure
        const ModelShot lo =
            shoot_model(m, yaw, one, lut, kShotTeam, RGB{18, 18, 24});
        paste(im, d * cell_w, cell_h, upscale(lo.rgb, 8));

        ShotCamera hd = one;
        hd.scale = 4.0f;
        hd.w = fr.w * 4;
        hd.h = fr.h * 4;
        hd.view_cx = m.anchor_x * 4.0f;
        hd.view_cy = m.anchor_y * 4.0f;
        hd.outline = true;
        const ModelShot hi =
            shoot_model(m, yaw, hd, lut, kShotTeam, RGB{18, 18, 24});
        paste(im, d * cell_w, cell_h * 2, upscale(hi.rgb, 2));

        int matched = 0, uni = 0;
        for (int i = 0; i < fr.w * fr.h; ++i)
        {
            const unsigned char a =
                remap_team(fr.frame[d][static_cast<std::size_t>(i)], kShotTeam);
            const bool sa = fr.frame[d][static_cast<std::size_t>(i)] != 0;
            const unsigned char bb = lo.index[static_cast<std::size_t>(i)];
            const bool sb = bb != 0;
            if (sa || sb)
                ++uni;
            if (sa && sb && a == bb)
                ++matched;
        }
        agreement_out[d] = uni > 0
            ? static_cast<float>(matched) / static_cast<float>(uni)
            : 0.0f;
    }
    return im;
}

// Where the fighters actually are: the living walker with the most livings
// within one screen-ish radius. A zoomed render of a random map corner shows
// nothing; the review needs the crowd.
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

// A Free render of a real level whose livings are rigs turned by curdir, with
// §10's wall-side and tree-canopy terrain fixes applied.
void run_model_scene(const std::string& name, const char* campaign, int level,
                     const std::map<int, og::render::VoxelModel>& living)
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
    // pass, so the scene window has to be taken AFTER one redraw — reading it
    // straight after frame_camera hands you the PREVIOUS scene's window.
    ASSERT_TRUE(vs->redraw(&scr()->level_runtime_data(), false));

    const std::vector<std::uint32_t> lut = build_palette_lut();
    SpikeModels models;
    models.living = living;
    build_terrain_models(models, scr()->level_visuals());

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
    const og::render::VoxelSceneBuildStats bs =
        og::render::build_voxel_scene(scene, world, scr()->level_visuals(), bp);

    int modelled = 0;
    for (const auto& v : scene.volumes())
        if (v.model != nullptr)
            ++modelled;

    const float wide_x =
        static_cast<float>(vs->topx) + static_cast<float>(vs->xview) / 2.0f;
    const float wide_y =
        static_cast<float>(vs->topy) + static_cast<float>(vs->yview) / 2.0f;
    float crowd_x = wide_x, crowd_y = wide_y;
    densest_cluster(world, crowd_x, crowd_y);

    printf("[voxel-rigs] scene %s (%s scen%d): grid %dx%d  %d tiles, %d decor, "
           "%d entities, %d volumes carry a model; crowd at (%.0f, %.0f)\n",
           name.c_str(), campaign, level, world.grid_for_floor(0).w,
           world.grid_for_floor(0).h, bs.tiles, bs.decor, bs.entities,
           modelled, static_cast<double>(crowd_x),
           static_cast<double>(crowd_y));

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
    };
    for (const FreeSpec& f : views)
    {
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
        const auto t0 = std::chrono::steady_clock::now();
        const og::render::VoxelRasterStats rs = raster.render(scene, cam, rt);
        raster.edge_darken(rt, 2.0f, 0.72f);
        const auto t1 = std::chrono::steady_clock::now();

        Image im = make_image(kClassicW, kClassicH, RGB{0, 0, 0});
        for (std::size_t i = 0; i < buf.size(); ++i)
            im.px[i] = unpack(buf[i]);
        write_image(models4_dir(), "scene_" + name + "_" + f.tag,
                    upscale(im, 2));
        printf("  %-18s %llu faces, %llu samples, %llu writes, %.2f s\n", f.tag,
               static_cast<unsigned long long>(rs.slices),
               static_cast<unsigned long long>(rs.pixel_samples),
               static_cast<unsigned long long>(rs.pixels_written),
               std::chrono::duration<double>(t1 - t0).count());
    }

    vs->editor_authoring_view_ = false;
    vs->editor_floor_override_ = -1;
    world.delete_objects();
    world.set_floor_count(1);
}

// The cast. Everything about a rig except its colours is authored here; the
// sprite supplies the palette.
struct RigFamily
{
    const char* name;
    int family;
    og::render::RigArchetype archetype = og::render::RigArchetype::Humanoid;
    og::render::RigWeapon weapon = og::render::RigWeapon::None;
    og::render::RigTeamSlot team = og::render::RigTeamSlot::Sash;
    bool helmet = false;
    bool hood = false;
    bool hat = false;
    bool cape = false;
    bool robe = false;
    bool quiver = false;
    bool shield = false;
    bool tusks = false;
    bool ears = false;
    bool bow_pose = false;
    bool planted = false;
    bool beard = false;
    bool hair = false;
    bool cross = false;
    bool pauldrons = false;
    int widen = 0;
    int lift = 0;
};

// Lineup order, which is also build order: the two heavies at the ends, the
// robes together, the two bowmen side by side so their silhouettes have to
// differ.
const RigFamily kRigs[] = {
    {.name = "footman", .family = FAMILY_SOLDIER,
     .weapon = og::render::RigWeapon::Sword,
     .team = og::render::RigTeamSlot::Crest,
     .helmet = true, .cape = true, .pauldrons = true},
    {.name = "cleric", .family = FAMILY_CLERIC,
     .weapon = og::render::RigWeapon::Mace,
     .team = og::render::RigTeamSlot::HoodTrim,
     .hood = true, .robe = true, .shield = true, .cross = true},
    {.name = "mage", .family = FAMILY_MAGE,
     .weapon = og::render::RigWeapon::Staff,
     .team = og::render::RigTeamSlot::HatBand,
     .hat = true, .robe = true, .planted = true, .beard = true},
    {.name = "elf", .family = FAMILY_ELF,
     .weapon = og::render::RigWeapon::Bow,
     .team = og::render::RigTeamSlot::Belt,
     .ears = true, .bow_pose = true, .hair = true, .widen = -2},
    {.name = "archer", .family = FAMILY_ARCHER,
     .weapon = og::render::RigWeapon::Bow,
     .team = og::render::RigTeamSlot::HoodTrim,
     .hood = true, .quiver = true, .bow_pose = true},
    {.name = "orc", .family = FAMILY_ORC,
     .weapon = og::render::RigWeapon::Axe,
     .team = og::render::RigTeamSlot::ChestStrap,
     .tusks = true, .pauldrons = true, .widen = 2},
    {.name = "skeleton", .family = FAMILY_SKELETON,
     .archetype = og::render::RigArchetype::Skeleton,
     .weapon = og::render::RigWeapon::Sword,
     .team = og::render::RigTeamSlot::Armband},
    {.name = "ghost", .family = FAMILY_GHOST,
     .archetype = og::render::RigArchetype::Ghost,
     .team = og::render::RigTeamSlot::None, .lift = 6},
};

og::render::RigSpec spec_for(const RigFamily& f, const og::render::RigPalette& p)
{
    og::render::RigSpec s;
    s.archetype = f.archetype;
    s.pal = p;
    s.weapon = f.weapon;
    s.helmet = f.helmet;
    s.hood = f.hood;
    s.hat = f.hat;
    s.cape = f.cape;
    s.robe = f.robe;
    s.quiver = f.quiver;
    s.shield = f.shield;
    s.tusks = f.tusks;
    s.pointed_ears = f.ears;
    s.torso_widen = f.widen;
    s.lift = f.lift;
    s.team_slot = f.team;
    s.bow_pose = f.bow_pose;
    s.planted_staff = f.planted;
    s.beard = f.beard;
    s.long_hair = f.hair;
    s.chest_cross = f.cross;
    s.pauldrons = f.pauldrons;
    return s;
}

// Three families whose sprite palette cannot supply what the character needs.
// Everything else is sampled.
void override_palette(int family, og::render::RigPalette& p)
{
    if (family == FAMILY_SKELETON)
    {
        // No skin anywhere on a skeleton: bone is the light grey ramp, and
        // the sword is the middle of it so it reads as rust-pitted iron.
        p.skin = 30;
        p.primary = 29;
        p.secondary = 27;
        p.metal = 23;
        p.dark = 16;
    }
    else if (family == FAMILY_ELF)
    {
        // Nothing in the living sprites uses the palette's green ramp, so a
        // forest tunic has to be named. Brown stays for hair and legs, which
        // is also what keeps the elf and the (brown) archer apart.
        p.primary = og::render::kVoxelRigGreen;
        p.secondary = 140;
    }
    else if (family == FAMILY_ARCHER)
    {
        // The archer's sprite is mostly skin tones, which would put it in a
        // pink tunic next to the green elf. Brown, per the brief.
        p.primary = 133;
        p.secondary = 135;
    }
    else if (family == FAMILY_ORC)
    {
        // Brown hide from the sprite, but the armour goes to the bottom of
        // that ramp so the tusks and the axe read against it.
        p.primary = 143;
    }
}

} // namespace

TEST_F(VoxelModels, parametric_rigs_and_scene_renders)
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

    printf("[voxel-rigs] round-4 character pass, 2x sprite scale (cell %.2f), "
           "camera theta %.0f\n",
           static_cast<double>(og::render::kVoxelRigCell),
           static_cast<double>(og::render::kVoxelCarveTheta));

    std::map<int, og::render::VoxelModel> scene_models;
    std::vector<Image> lineup;
    int lineup_h = 0;

    for (const RigFamily& f : kRigs)
    {
        og::render::VoxelCarveFrames fr;
        if (!family_walk_frames(f.family, fr))
        {
            printf("  %-9s : no eight-facing walk art, skipped\n", f.name);
            continue;
        }
        PaletteChoice pc = pick_palette(fr, lut);
        override_palette(f.family, pc.pal);
        const auto t0 = std::chrono::steady_clock::now();
        og::render::VoxelModel rig =
            og::render::voxel_build_rig(spec_for(f, pc.pal), fr.w, fr.h);
        const auto t1 = std::chrono::steady_clock::now();
        if (rig.empty())
        {
            printf("  %-9s : rig built nothing\n", f.name);
            continue;
        }
        const og::render::VoxelModel shadow =
            og::render::voxel_build_shadow(rig, pc.pal.dark);

        float agree[NUM_FACINGS] = {};
        write_image(models4_dir(), std::string("classic_") + f.name,
                    build_classic_page(fr, rig, lut, agree));
        write_image(models4_dir(), std::string("hero_") + f.name,
                    build_hero(rig, shadow, lut));
        write_image(models4_dir(), std::string("turntable_") + f.name,
                    build_turntable(rig, shadow, lut));
        const auto t2 = std::chrono::steady_clock::now();

        const ShotCamera line =
            frame_at_scale(rig, 0.0f, 30.0f, 35.0f, 5.0f, 8);
        lineup.push_back(
            shoot_model(rig, 0.0f, line, lut, kShotTeam, kPlateBg, &shadow)
                .rgb);
        lineup_h = std::max(lineup_h, lineup.back().h);

        int vox = 0;
        for (unsigned char o : rig.occ)
            if (o != 0)
                ++vox;
        float mean = 0.0f;
        int worst = 0;
        for (int d = 0; d < NUM_FACINGS; ++d)
        {
            mean += agree[d];
            if (agree[d] < agree[worst])
                worst = d;
        }
        mean /= static_cast<float>(NUM_FACINGS);
        printf("  %-9s sprite %2dx%-2d  rig %dx%dx%d %5d vox  build %.3fs "
               "render %.2fs\n",
               f.name, fr.w, fr.h, rig.w, rig.d, rig.z, vox,
               std::chrono::duration<double>(t1 - t0).count(),
               std::chrono::duration<double>(t2 - t1).count());
        printf("            palette %s\n", pc.note.c_str());
        printf("            classic-angle agreement mean %.1f%%  worst %s "
               "%.1f%%  per-facing:",
               static_cast<double>(mean) * 100.0, kFacingNames[worst],
               static_cast<double>(agree[worst]) * 100.0);
        for (int d = 0; d < NUM_FACINGS; ++d)
            printf(" %.0f", static_cast<double>(agree[d]) * 100.0);
        printf("\n");

        scene_models.emplace(f.family, std::move(rig));
    }

    // lineup.png — the cast photo.
    if (!lineup.empty())
    {
        int total_w = 0;
        for (const Image& im : lineup)
            total_w += im.w + 4;
        Image row = make_image(total_w, lineup_h, kPlateBg);
        int x = 0;
        for (const Image& im : lineup)
        {
            paste(row, x, lineup_h - im.h, im);
            x += im.w + 4;
        }
        write_image(models4_dir(), "lineup", upscale(row, 2));
    }

    world.delete_objects();
    world.set_floor_count(1);
    run_model_scene("gladiator_scen1", "gladiator", 1, scene_models);
    if (testing::Test::HasFatalFailure())
        return;
    run_model_scene("westlands_scen1", "westlands", 1, scene_models);
}
