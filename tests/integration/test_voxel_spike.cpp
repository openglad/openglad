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

#include <array>
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
// by one camera, so the model is reconstructed by carving, not authored. This
// half of the harness carves one model per family, measures how well a
// re-render of the model reproduces each source frame, and dumps the pictures
// that answer "does it still look like the same character".
// ===========================================================================

namespace {

std::string models_dir()
{
    return spike_dir() + "/models";
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

void draw_indices(Image& im, int x0, int y0, const unsigned char* idx, int w,
                  int h, const std::vector<std::uint32_t>& lut)
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
                unpack(lut[static_cast<std::size_t>(c)]);
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

// §10's two terrain fixes, built from the live level art.
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
    for (int pix : {PIX_TREE_T1, PIX_TREE_M1, PIX_TREE_ML, PIX_TREE_MR,
                    PIX_TREE_MT, PIX_TREE_B1})
    {
        const PixieData& top = visuals.pixdata[pix];
        if (!top.valid())
            continue;
        // Trunk: a 4x4 centre column for the lower 12 voxels. Canopy: the
        // full footprint for the top 8. Together they are still
        // kVoxelHeightTree tall, so nothing else in the scene moves.
        og::render::VoxelModel m = og::render::voxel_build_tree_model(
            top.data.get(), top.w, top.h, 12, 8, 4);
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

// One family page: the eight source frames over the eight re-renders of the
// carved model, at one theta.
Image build_facing_strip(const og::render::VoxelCarveFrames& fr,
                         const og::render::VoxelModel& model,
                         const std::vector<std::uint32_t>& lut)
{
    const int cw = fr.w + 2;
    const int ch = fr.h + 2;
    Image im = make_image(cw * NUM_FACINGS, ch * 2, RGB{24, 24, 32});
    std::vector<unsigned char> render(
        static_cast<std::size_t>(fr.w) * static_cast<std::size_t>(fr.h), 0u);
    for (int d = 0; d < NUM_FACINGS; ++d)
    {
        draw_indices(im, d * cw + 1, 1, fr.frame[d], fr.w, fr.h, lut);
        og::render::voxel_model_render_indices(
            model, og::render::voxel_facing_yaw_rad(d), model.theta_deg,
            render.data(), fr.w, fr.h, model.anchor_x, model.anchor_y);
        draw_indices(im, d * cw + 1, ch + 1, render.data(), fr.w, fr.h, lut);
    }
    return im;
}

// Sixteen yaws at 22.5 degrees, pitch 45 — the model turned on the spot.
Image build_turntable(const og::render::VoxelModel& model,
                      const std::vector<std::uint32_t>& lut)
{
    constexpr int kFrames = 16;
    constexpr float kPitch = 45.0f;
    const float sp = std::sin(kPitch * 3.14159265358979f / 180.0f);
    const float cp = std::cos(kPitch * 3.14159265358979f / 180.0f);
    const float diag = std::sqrt(static_cast<float>(model.w * model.w +
                                                    model.d * model.d));
    const int cw = static_cast<int>(std::ceil(diag)) + 4;
    const int ch = static_cast<int>(std::ceil(
                       diag * 0.5f * sp + static_cast<float>(model.z) * cp)) +
        6;
    const float ax = static_cast<float>(cw) * 0.5f;
    const float ay = static_cast<float>(ch) - 2.0f - diag * 0.5f * sp;
    Image im = make_image(cw * kFrames, ch, RGB{24, 24, 32});
    std::vector<unsigned char> render(
        static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch), 0u);
    for (int f = 0; f < kFrames; ++f)
    {
        const float yaw = static_cast<float>(f) * 22.5f * 3.14159265358979f /
            180.0f;
        og::render::voxel_model_render_indices(model, yaw, kPitch,
                                               render.data(), cw, ch, ax, ay);
        draw_indices(im, f * cw, 0, render.data(), cw, ch, lut);
    }
    return im;
}

// A Free render of a real level whose livings are carved models turned by
// curdir, with §10's wall-side and tree-canopy terrain fixes applied.
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
    printf("[voxel-models] scene %s (%s scen%d): grid %dx%d topx=%d topy=%d "
           "oblist=%d\n",
           name.c_str(), campaign, level, world.grid_for_floor(0).w,
           world.grid_for_floor(0).h, static_cast<int>(vs->topx),
           static_cast<int>(vs->topy), static_cast<int>(world.oblist.size()));
    printf("  %d tiles, %d decor, %d entities, %d volumes carry a model\n",
           bs.tiles, bs.decor, bs.entities, modelled);

    const float focus_x =
        static_cast<float>(vs->topx) + static_cast<float>(vs->xview) / 2.0f;
    const float focus_y =
        static_cast<float>(vs->topy) + static_cast<float>(vs->yview) / 2.0f;

    struct FreeSpec
    {
        const char* tag;
        float yaw;
        float pitch;
        float scale;
    };
    const FreeSpec views[] = {
        {"y30_p60", 30.0f, 60.0f, 1.0f},
        {"y45_p45", 45.0f, 45.0f, 1.0f},
        {"y30_p60_zoom2", 30.0f, 60.0f, 2.0f},
    };
    for (const FreeSpec& f : views)
    {
        std::vector<std::uint32_t> buf(
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

        Image im = make_image(kClassicW, kClassicH, RGB{0, 0, 0});
        for (std::size_t i = 0; i < buf.size(); ++i)
            im.px[i] = unpack(buf[i]);
        write_image(models_dir(), "scene_" + name + "_" + f.tag,
                    upscale(im, 2));
        printf("  %s: %llu slices, %llu samples, %llu writes\n", f.tag,
               static_cast<unsigned long long>(rs.slices),
               static_cast<unsigned long long>(rs.pixel_samples),
               static_cast<unsigned long long>(rs.pixels_written));
    }

    vs->editor_authoring_view_ = false;
    vs->editor_floor_override_ = -1;
    world.delete_objects();
    world.set_floor_count(1);
}

// curdir order, from gloader.cpp's bit1..bit8 comments.
const char* kFacingNames[NUM_FACINGS] = {"up",   "up-r", "right", "down-r",
                                         "down", "down-l", "left", "up-l"};

const char* core_family_name(int family)
{
    static const char* const kNames[] = {
        "soldier", "elf",    "archer",   "mage",      "skeleton",
        "cleric",  "firelem","faerie",   "slime",     "small_slime",
        "medium_slime", "thief", "ghost", "druid",    "orc",
        "orc_captain",  "barbarian", "archmage", "golem", "giant_skeleton",
        "tower1"};
    if (family < 0 || family >= static_cast<int>(std::size(kNames)))
        return "unknown";
    return kNames[family];
}

} // namespace

TEST_F(VoxelModels, carve_facings_and_scene_renders)
{
    if (spike_dir().empty())
        GTEST_SKIP() << "set OG_VOXEL_SPIKE_DIR to record";

    ASSERT_TRUE(mount_scene_campaign("gladiator"));
    all_effects_off();

    // Which livings does gladiator scen1 actually put on the field?
    GameWorld& world = scr()->world();
    world.delete_objects();
    world.id = 1;
    scr()->save_data.scen_num = 1;
    ASSERT_TRUE(scr()->load_level()) << "gladiator scen1";
    std::set<int> in_scen1;
    for (const auto& u : world.oblist)
    {
        walker* const w = u.get();
        if (w != nullptr && w->query_order() == Order::Living)
            in_scen1.insert(static_cast<int>(w->family()));
    }
    printf("[voxel-models] gladiator scen1: grid %dx%d oblist=%d campaign=%s\n",
           world.grid_for_floor(0).w, world.grid_for_floor(0).h,
           static_cast<int>(world.oblist.size()),
           scr()->save_data.current_campaign.c_str());
    printf("[voxel-models] gladiator scen1 livings:");
    for (int f : in_scen1)
        printf(" %s(%d)", core_family_name(f), f);
    printf("\n");

    const std::vector<std::uint32_t> lut = build_palette_lut();

    std::vector<FamilySpec> fams = {
        {"footman", FAMILY_SOLDIER},  {"archer", FAMILY_ARCHER},
        {"orc", FAMILY_ORC},          {"skeleton", FAMILY_SKELETON},
        {"mage", FAMILY_MAGE},        {"elf", FAMILY_ELF},
    };
    for (int f : in_scen1)
    {
        bool have = false;
        for (const FamilySpec& fs : fams)
            if (fs.family == f)
                have = true;
        if (!have)
            fams.push_back({core_family_name(f), f});
    }

    // --- convention probe -------------------------------------------------
    // The facing -> yaw map is derived in voxel_scene.h from gloader's bit1..
    // bit8 comments and the FACE_* constants. Check it against the art rather
    // than trusting the derivation: carve the same frames under every
    // rotational offset and both handednesses. A wrong map intersects the
    // eight silhouettes in the wrong relative orientations, so its agreement
    // collapses; the derived map should win.
    {
        og::render::VoxelCarveFrames probe;
        if (family_walk_frames(FAMILY_SOLDIER, probe))
        {
            printf("[voxel-models] facing convention probe (footman, "
                   "theta 55): yaw_d = sign * (d - 4 + offset) * 45\n");
            for (int sign = 1; sign >= -1; sign -= 2)
                for (int off = 0; off < NUM_FACINGS; ++off)
                {
                    og::render::VoxelCarveParams cp;
                    cp.theta_deg = 55.0f;
                    cp.custom_yaw = true;
                    for (int d = 0; d < NUM_FACINGS; ++d)
                        cp.yaw_rad[static_cast<std::size_t>(d)] =
                            static_cast<float>(sign * ((d - FACE_DOWN + off) * 45)) *
                            3.14159265358979f / 180.0f;
                    const og::render::VoxelCarveReport r =
                        og::render::voxel_carve(probe, cp);
                    printf("    sign %+d offset %d : mean %.1f%%  iou %.1f%%  "
                           "%d voxels%s\n",
                           sign, off,
                           static_cast<double>(r.agreement_mean) * 100.0,
                           static_cast<double>(r.silhouette_iou_mean) * 100.0,
                           r.voxel_count,
                           (sign == 1 && off == 0) ? "   <- derived map" : "");
                }
        }
    }

    const float thetas[] = {45.0f, 55.0f, 65.0f};
    constexpr int kThetas = 3;
    double theta_sum[kThetas] = {};
    int theta_n[kThetas] = {};
    std::vector<std::array<og::render::VoxelModel, kThetas>> carved(fams.size());

    printf("[voxel-models] agreement = matched palette index / union of "
           "opaque pixels, per facing (up, up-r, right, down-r, down, "
           "down-l, left, up-l)\n");
    for (std::size_t fi = 0; fi < fams.size(); ++fi)
    {
        const FamilySpec& fs = fams[fi];
        og::render::VoxelCarveFrames fr;
        if (!family_walk_frames(fs.family, fr))
        {
            printf("  %-10s : no eight-facing walk art, skipped\n", fs.name);
            continue;
        }
        printf("  %-10s  sprite %dx%d\n", fs.name, fr.w, fr.h);
        for (int ti = 0; ti < kThetas; ++ti)
        {
            og::render::VoxelCarveParams cp;
            cp.theta_deg = thetas[ti];
            const og::render::VoxelCarveReport rep = og::render::voxel_carve(fr, cp);
            if (rep.model.empty())
            {
                printf("    theta %2.0f : carved to nothing\n",
                       static_cast<double>(thetas[ti]));
                continue;
            }
            printf("    theta %2.0f  grid %dx%dx%d  anchor (%.2f,%.2f)  "
                   "%d voxels  %.3f s  mean %.1f%%  iou %.1f%%  worst %s "
                   "%.1f%%\n",
                   static_cast<double>(thetas[ti]), rep.model.w, rep.model.d,
                   rep.model.z, static_cast<double>(rep.model.anchor_x),
                   static_cast<double>(rep.model.anchor_y), rep.voxel_count,
                   rep.carve_seconds,
                   static_cast<double>(rep.agreement_mean) * 100.0,
                   static_cast<double>(rep.silhouette_iou_mean) * 100.0,
                   kFacingNames[rep.worst_facing],
                   static_cast<double>(rep.agreement[rep.worst_facing]) * 100.0);
            printf("             per-facing:");
            for (int d = 0; d < NUM_FACINGS; ++d)
                printf(" %s=%.0f%%", kFacingNames[d],
                       static_cast<double>(rep.agreement[d]) * 100.0);
            printf("\n");

            char fname[128];
            snprintf(fname, sizeof(fname), "strip_%s_t%.0f", fs.name,
                     static_cast<double>(thetas[ti]));
            write_image(models_dir(), fname,
                        upscale(build_facing_strip(fr, rep.model, lut), 4));

            theta_sum[ti] += static_cast<double>(rep.agreement_mean);
            ++theta_n[ti];
            carved[fi][static_cast<std::size_t>(ti)] = rep.model;
        }
    }

    int best_ti = 0;
    for (int ti = 0; ti < kThetas; ++ti)
    {
        const double m = theta_n[ti] > 0 ? theta_sum[ti] / theta_n[ti] : 0.0;
        printf("[voxel-models] theta %2.0f mean agreement over %d families: "
               "%.1f%%\n",
               static_cast<double>(thetas[ti]), theta_n[ti], m * 100.0);
        if (theta_n[ti] > 0 && theta_n[best_ti] > 0 &&
            m > theta_sum[best_ti] / theta_n[best_ti])
            best_ti = ti;
    }
    printf("[voxel-models] RECOMMENDED theta = %.0f\n",
           static_cast<double>(thetas[best_ti]));

    // Turntables for the first two families that carved.
    int turned = 0;
    for (std::size_t fi = 0; fi < fams.size() && turned < 2; ++fi)
    {
        const og::render::VoxelModel& m =
            carved[fi][static_cast<std::size_t>(best_ti)];
        if (m.empty())
            continue;
        char fname[128];
        snprintf(fname, sizeof(fname), "turntable_%s", fams[fi].name);
        write_image(models_dir(), fname, upscale(build_turntable(m, lut), 4));
        ++turned;
    }

    // The scene renders draw livings as carved models turned by curdir.
    std::map<int, og::render::VoxelModel> living;
    for (std::size_t fi = 0; fi < fams.size(); ++fi)
    {
        const og::render::VoxelModel& m =
            carved[fi][static_cast<std::size_t>(best_ti)];
        if (!m.empty())
            living.emplace(fams[fi].family, m);
    }

    world.delete_objects();
    world.set_floor_count(1);
    run_model_scene("gladiator_scen1", "gladiator", 1, living);
    if (testing::Test::HasFatalFailure())
        return;
    run_model_scene("westlands_scen1", "westlands", 1, living);
}
