// Render-pass tests for the multifloor FX pre-pass: ground shadows under
// living units / weapons-in-flight and glass reflections on the camera floor
// (draw_walker_shadow / draw_walker_reflection wired into draw_floor_entities
// and draw_obs). Effects OFF must render byte-identically, so every test
// compares an off-run against an on-run of the same scene.
#include <openglad/interface/render/walker_draw.h>
#include <openglad/interface/render/effects.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/gparser.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pal32.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <array>
#include <cstdlib>
#include <string>
#include <utility>
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

// Save/restore the effect keys this pass reads, plus the ones whose defaults
// (now on) would otherwise leak into the scene under test. Missing keys
// restore to their production defaults (gparser load_settings).
class EffectsCfgGuard
{
public:
    EffectsCfgGuard()
    {
        for (const auto& [key, fallback] : kKeys)
        {
            (void)fallback;
            saved_.emplace_back(key, cfg.get_setting("effects", key));
        }
    }
    ~EffectsCfgGuard()
    {
        for (size_t i = 0; i < saved_.size(); i++)
            cfg.apply_setting("effects", saved_[i].first,
                              saved_[i].second.empty() ? kKeys[i].second
                                                       : saved_[i].second);
    }
    EffectsCfgGuard(const EffectsCfgGuard&) = delete;
    EffectsCfgGuard& operator=(const EffectsCfgGuard&) = delete;

private:
    static constexpr std::pair<const char*, const char*> kKeys[] = {
        {"shadows", "on"},        {"reflections", "on"}, {"weather", "on"},
        {"attack_lunge", "on"},   {"hit_recoil", "off"}, {"ripples", "on"},
        {"trails", "on"},         {"dust", "on"},        {"fire_glow", "on"},
        {"depth_tint", "on"},     {"screen_shake", "on"},
    };
    std::vector<std::pair<std::string, std::string>> saved_;
};

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

// The blend of PURE_BLACK never raises a channel and strictly lowers any
// channel large enough to lose >= 1 from `c - c*alpha/256`.
bool darkened(const RGB& on, const RGB& off)
{
    return on.r <= off.r && on.g <= off.g && on.b <= off.b &&
        (on.r + on.g + on.b) < (off.r + off.g + off.b);
}

// Ground-plane screen anchor of a walker (no interpolation client active in
// these tests, so worldx/worldy are authoritative): where its shadow anchors.
void ground_anchor(const walker& w, const viewscreen* vs,
                   Sint32& sx, Sint32& sy)
{
    sx = static_cast<Sint32>(w.worldx() - static_cast<float>(vs->topx) +
                             static_cast<float>(vs->xloc));
    sy = static_cast<Sint32>(w.worldy() - static_cast<float>(vs->topy) +
                             static_cast<float>(vs->yloc));
}

// Pick a screen pixel guaranteed to hold shadow, not sprite: target row
// sy+h-t samples source row h-1-2t (nudged +2px x); rows the sprite overlaps
// (t >= 1) qualify only where the sprite pixel on top is transparent.
bool find_shadow_probe(const unsigned char* bmp, int w, int h,
                       int& out_col, int& out_t)
{
    for (int t = 0; t < (h + 1) / 2; t++)
        for (int c = 0; c < w; c++)
        {
            if (!bmp[(h - 1 - 2 * t) * w + c])
                continue;
            if (t > 0)
            {
                const int sprite_col = c + 2; // shadow is nudged +2px
                if (sprite_col < w && bmp[(h - t) * w + sprite_col])
                    continue;
            }
            out_col = c;
            out_t = t;
            return true;
        }
    return false;
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

} // namespace

TEST(RenderEffects, shadow_draws_darker_pixel_below_feet)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");

    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_NE(nullptr, w->bmp_data());

    int probe_col = 0, probe_t = 0;
    ASSERT_TRUE(find_shadow_probe(w->bmp_data(), w->sizex(), w->sizey(),
                                  probe_col, probe_t))
        << "soldier sprite must have an opaque shadow-source pixel";

    // OFF: no trace, remember the untouched ground pixel below the feet.
    cfg.apply_setting("effects", "shadows", "off");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "shadows"))
        << "shadows off must not trace";
    Sint32 sx = 0, sy = 0;
    ground_anchor(*w, vs, sx, sy);
    const int probe_x = sx + 2 + probe_col;
    const int probe_y = sy + w->sizey() - probe_t;
    const RGB off = px(probe_x, probe_y);

    // ON: one shadow drawn, that pixel darkens (black blended over ground).
    cfg.apply_setting("effects", "shadows", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "shadows floor=0 n=1"))
        << "one shadow expected on the camera floor";
    ASSERT_FALSE(trace_contains("effects", "reflections"))
        << "reflections stay off";
    const RGB on = px(probe_x, probe_y);
    ASSERT_TRUE(darkened(on, off))
        << "shadow pixel below the feet must darken (" << int(on.r) << ","
        << int(on.g) << "," << int(on.b) << ") vs (" << int(off.r) << ","
        << int(off.g) << "," << int(off.b) << ")";

    restore_world(vs);
}

TEST(RenderEffects, weapon_shadow_stays_at_ground_when_raised_by_worldz)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");

    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    // A knife in flight, arcing 12px above its ground spot.
    walker* knife = scr()->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->setxy(200, 120);
    knife->set_worldz(12.0f);
    ASSERT_NE(nullptr, knife->bmp_data());

    int probe_col = 0, probe_t = 0;
    ASSERT_TRUE(find_shadow_probe(knife->bmp_data(), knife->sizex(),
                                  knife->sizey(), probe_col, probe_t))
        << "knife sprite must have an opaque shadow-source pixel";

    cfg.apply_setting("effects", "shadows", "off");
    ASSERT_TRUE(do_redraw(vs));
    Sint32 kx = 0, ky = 0;
    ground_anchor(*knife, vs, kx, ky);
    // Probe on the GROUND plane: the raised sprite occupies rows above
    // ky + sizey - 1 - worldz, so its shadow row is not the sprite.
    const int probe_x = kx + 2 + probe_col;
    const int probe_y = ky + knife->sizey() - probe_t;
    const RGB off = px(probe_x, probe_y);

    cfg.apply_setting("effects", "shadows", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "shadows floor=0 n=2"))
        << "soldier + knife both cast shadows";
    const RGB on = px(probe_x, probe_y);
    ASSERT_TRUE(darkened(on, off))
        << "airborne weapon's shadow must stay on the ground plane";

    restore_world(vs);
}

TEST(RenderEffects, draw_obs_legacy_path_draws_shadow_prepass)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "shadows", "on");

    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs)); // establish camera + current_floor_

    // The level-editor entity path shares the same pre-pass per floor.
    trace_clear();
    ASSERT_TRUE(vs->draw_obs(&scr()->level_runtime_data()));
    ASSERT_TRUE(trace_contains("effects", "shadows floor=0 n=1"))
        << "draw_obs must run the shadow pre-pass";

    restore_world(vs);
}

TEST(RenderEffects, reflection_draws_on_camera_floor_glass)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "weather", "off");
    const std::string saved_ghost = cfg.get_setting("graphics", "floor_ghost");
    cfg.apply_setting("graphics", "floor_ghost", "on");

    // Two floors: grass below, an all-glass camera floor above the walker.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GLASS));

    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->set_floor(1);
    w->setxy(160, 120);
    vs->control = w;

    // A weapon in flight reflects too (the pass scans both entity lists).
    walker* knife = world.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->set_floor(1);
    knife->setxy(200, 120);

    // OFF: capture the reflection rect (mirrored sprite area below the feet).
    cfg.apply_setting("effects", "reflections", "off");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "reflections"))
        << "reflections off must not trace";
    Sint32 sx = 0, sy = 0;
    ground_anchor(*w, vs, sx, sy);
    const int rect_y = sy + w->sizey();
    const std::vector<RGB> off_rect =
        grab_rect(sx, rect_y, w->sizex(), w->sizey());

    // ON: the trace fires for the camera floor and the glass rect changes.
    cfg.apply_setting("effects", "reflections", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "reflections floor=1 n=2"))
        << "soldier + knife both reflect on the camera floor";
    const std::vector<RGB> on_rect =
        grab_rect(sx, rect_y, w->sizex(), w->sizey());
    ASSERT_FALSE(rects_equal(off_rect, on_rect))
        << "reflection must alter pixels in the glass below the walker";

    cfg.apply_setting("graphics", "floor_ghost",
                      saved_ghost.empty() ? "on" : saved_ghost);
    restore_world(vs);
}

TEST(RenderEffects, reflection_absent_without_glass)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "weather", "off");
    const std::string saved_ghost = cfg.get_setting("graphics", "floor_ghost");
    cfg.apply_setting("graphics", "floor_ghost", "on");

    // Same two-floor scene, but the camera floor holds no glass at all: the
    // cheap grid pre-check must skip the blit, so nothing traces or changes.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));

    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->set_floor(1);
    w->setxy(160, 120);
    vs->control = w;

    cfg.apply_setting("effects", "reflections", "off");
    ASSERT_TRUE(do_redraw(vs));
    Sint32 sx = 0, sy = 0;
    ground_anchor(*w, vs, sx, sy);
    const int rect_y = sy + w->sizey();
    const std::vector<RGB> off_rect =
        grab_rect(sx, rect_y, w->sizex(), w->sizey());

    cfg.apply_setting("effects", "reflections", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "reflections"))
        << "no glass under the walker: no reflection may draw";
    const std::vector<RGB> on_rect =
        grab_rect(sx, rect_y, w->sizex(), w->sizey());
    ASSERT_TRUE(rects_equal(off_rect, on_rect))
        << "reflections on over no glass must render byte-identically";

    cfg.apply_setting("graphics", "floor_ghost",
                      saved_ghost.empty() ? "on" : saved_ghost);
    restore_world(vs);
}

TEST(RenderEffects, invisible_and_phantom_walkers_cast_neither)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "on");
    cfg.apply_setting("effects", "reflections", "on");
    cfg.apply_setting("effects", "weather", "off");

    // All-glass single floor so every eligible walker would reflect.
    GameWorld& world = scr()->world();
    const std::size_t cells =
        static_cast<std::size_t>(world.grid.w) * world.grid.h;
    std::fill(world.grid.data.get(), world.grid.data.get() + cells,
              static_cast<unsigned char>(PIX_GLASS));

    walker* visible = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* hidden = world.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* phantom = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, visible);
    ASSERT_NE(nullptr, hidden);
    ASSERT_NE(nullptr, phantom);
    visible->setxy(160, 120);
    hidden->setxy(200, 120);
    hidden->set_invisibility_left(50);
    phantom->setxy(120, 120);
    phantom->stats()->set_bit_flags(BIT_PHANTOM, 1);
    vs->control = visible;

    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "shadows floor=0 n=1"))
        << "only the visible walker casts a shadow";
    ASSERT_TRUE(trace_contains("effects", "reflections floor=0 n=1"))
        << "only the visible walker reflects";

    restore_world(vs);
}

TEST(RenderEffects, shadow_anchor_follows_lunge_and_recoil_offsets)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "shadows", "on");
    // The shadow anchors where the SPRITE is drawn, so it must apply the
    // same lunge/recoil offsets draw_walker does when those effects are on.
    cfg.apply_setting("effects", "attack_lunge", "on");
    cfg.apply_setting("effects", "hit_recoil", "on");

    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs)); // establish camera geometry

    w->set_attack_lunge(1.0f);
    w->set_attack_lunge_angle(0.0f);
    w->set_hit_recoil(1.0f);
    w->set_hit_recoil_angle(0.0f);
    EXPECT_TRUE(draw_walker_shadow(*w, vs))
        << "lunging/recoiling walker still casts a shadow";
    w->set_attack_lunge(0.0f);
    w->set_hit_recoil(0.0f);

    restore_world(vs);
}

namespace
{

// Walker-free multifloor scene pinned to `floor` via the editor override
// (no control walker, so nothing animates between redraws: consecutive
// redraws of the same scene are byte-identical unless clouds change them).
void fill_camera_grid(unsigned char tile);
void all_effects_off();

void setup_cloud_scene(viewscreen* vs, int floors, int floor)
{
    prepare_world();
    GameWorld& world = scr()->world();
    if (floors > 1)
    {
        world.set_floor_count(floors);
        for (int f = 1; f < floors; f++)
            fill_floor_grid(world, f, static_cast<unsigned char>(PIX_GRASS1));
    }
    vs->control = nullptr;
    vs->editor_floor_override_ = floor;
}

void teardown_cloud_scene(viewscreen* vs)
{
    vs->editor_floor_override_ = -1;
    restore_world(vs);
}

std::vector<RGB> grab_viewport(const viewscreen* vs)
{
    return grab_rect(static_cast<int>(vs->xloc), static_cast<int>(vs->yloc),
                     static_cast<int>(vs->xview), static_cast<int>(vs->yview));
}

} // namespace

TEST(RenderEffects, clouds_draw_on_top_floor_and_gate_off_byte_identically)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Clouds);

    // cfg OFF (the client display opt-out): no trace, and two redraws render
    // byte-identically even though the WORLD kind says Clouds (the overlay
    // leaves no residual state behind).
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "clouds"))
        << "weather off must not trace";
    const std::vector<RGB> off_a = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off_b = grab_viewport(vs);
    ASSERT_TRUE(rects_equal(off_a, off_b))
        << "weather off must render byte-identically frame to frame";

    // ON: the trace fires for the top floor and some pixels lighten.
    cfg.apply_setting("effects", "weather", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "clouds floor=1"))
        << "cloud overlay must trace on the top floor";
    const std::vector<RGB> on = grab_viewport(vs);
    ASSERT_FALSE(rects_equal(off_a, on))
        << "cloud banks must alter viewport pixels";

    teardown_cloud_scene(vs);
}

// A WeatherKind::None level draws NOTHING even with the display key on: the
// world's synced kind is the authoritative gate, cfg only opts a client out.
TEST(RenderEffects, weather_none_with_cfg_on_renders_byte_identically)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor (gates open)
    scr()->world().set_weather(WeatherKind::None);

    // Tick 0 would flash lightning if a Rain kind leaked through.
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);

    cfg.apply_setting("effects", "weather", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "clouds"))
        << "kind None must not draw banks";
    ASSERT_FALSE(trace_contains("effects", "rain"))
        << "kind None must not rain";
    ASSERT_FALSE(trace_contains("effects", "lightning"))
        << "kind None must not flash";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "kind None + cfg on must render byte-identically to cfg off";

    teardown_cloud_scene(vs);
}

TEST(RenderEffects, clouds_absent_below_the_top_floor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    setup_cloud_scene(vs, 2, 0); // camera UNDER the top floor
    scr()->world().set_weather(WeatherKind::Clouds);

    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);

    cfg.apply_setting("effects", "weather", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "clouds"))
        << "no clouds may draw while a floor sits overhead";
    const std::vector<RGB> on = grab_viewport(vs);
    ASSERT_TRUE(rects_equal(off, on))
        << "clouds on a lower floor must render byte-identically";

    teardown_cloud_scene(vs);
}

TEST(RenderEffects, clouds_absent_on_single_floor_indoor_levels)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    setup_cloud_scene(vs, 1, 0); // single floor: its floor 0 IS the top
    scr()->world().set_weather(WeatherKind::Clouds);
    // Carpet is a TYPE_CARPET (indoor) genre: the outdoor heuristic must
    // keep a dungeon dry no matter the world kind or cfg toggle.
    fill_camera_grid(static_cast<unsigned char>(PIX_CARPET_M));

    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);

    cfg.apply_setting("effects", "weather", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "clouds floor"))
        << "indoor single-floor levels have no cloud layer";
    ASSERT_TRUE(trace_contains("effects", "verdict=0"))
        << "the terrain heuristic must vote indoor for a carpet dungeon";
    const std::vector<RGB> on = grab_viewport(vs);
    ASSERT_TRUE(rects_equal(off, on))
        << "clouds on an indoor single-floor level must render byte-identically";

    teardown_cloud_scene(vs);
}

TEST(RenderEffects, clouds_appear_on_single_floor_outdoor_levels)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    setup_cloud_scene(vs, 1, 0);
    scr()->world().set_weather(WeatherKind::Clouds);
    // An all-grass field is unambiguously open sky.
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));

    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);

    cfg.apply_setting("effects", "weather", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "clouds floor=0"))
        << "an outdoor single-floor level must get the cloud layer";
    ASSERT_TRUE(trace_contains("effects", "verdict=1"))
        << "the terrain heuristic must vote outdoor for a grass field";
    ASSERT_FALSE(rects_equal(off, grab_viewport(vs)))
        << "clouds over a grass field must change pixels";

    teardown_cloud_scene(vs);
}

TEST(RenderEffects, outdoor_heuristic_threshold_and_per_tick_memo)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "weather", "on");
    setup_cloud_scene(vs, 1, 0);
    scr()->world().set_weather(WeatherKind::Clouds);

    GameWorld& world = scr()->world();
    const int total = static_cast<int>(world.grid.w) * world.grid.h;
    const auto fill_first_n_grass = [&](int n) {
        fill_camera_grid(static_cast<unsigned char>(PIX_CARPET_M));
        std::fill(world.grid.data.get(), world.grid.data.get() + n,
                  static_cast<unsigned char>(PIX_GRASS1));
    };

    // Exactly half outdoor meets the threshold (outdoor * 2 >= total)...
    fill_first_n_grass((total + 1) / 2);
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "verdict=1"))
        << "half-outdoor terrain must meet the threshold";

    // ...one tile fewer does not.
    fill_first_n_grass((total + 1) / 2 - 1);
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "verdict=0"))
        << "just-under-half outdoor terrain must stay indoor";

    // The verdict is memoized per frame tick: a second redraw on the same
    // tick reuses it (one heuristic trace), the next tick recomputes and
    // sees an in-place grid edit.
    fill_first_n_grass(total);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "verdict=1"))
        << "the same tick must reuse the memoized indoor verdict";
    effects_advance_frame();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "verdict=1"))
        << "the next tick must recompute and see the repainted grass";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// Weather blends must NOT requantize into the color-cycled palette bands
// (water 208-223 / orange 224-231): blend_pixel's 8-bpp path maps the
// blended RGB back to a palette index, and a hit inside a cycled band
// twinkles as do_cycle rotates it — pale-blue "rain-like" pixels inside
// the banks, worst over water tiles whose blues neighbor the band. Both
// kinds are checked (the kinds are exclusive, so one at a time).
TEST(RenderEffects, weather_blends_survive_palette_cycling)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "weather", "on");
    setup_cloud_scene(vs, 1, 0);
    // Grass field with a water block: water is outdoor genre (heuristic
    // stays open-sky) and its blues sit right next to the cycled band -
    // the worst case for blend requantization. The Rain pass adds the blue
    // streak blends, whose pale-blue results are the likeliest band hits.
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    GameWorld& world = scr()->world();
    for (int j = 4; j < 12; j++)
        for (int i = 4; i < 16; i++)
            world.grid.data[i + world.grid.w * j] =
                static_cast<unsigned char>(PIX_WATER1);

    for (const WeatherKind kind :
         {WeatherKind::Clouds, WeatherKind::Rain, WeatherKind::Snow})
    {
        world.set_weather(kind);
        effects_reset_for_testing();
        effects_advance_frame();
        effects_advance_frame(); // tick 2: past the lightning flash
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        const char* expected_trace = kind == WeatherKind::Clouds
            ? "clouds floor=0"
            : (kind == WeatherKind::Rain ? "rain floor=0" : "snow floor=0");
        ASSERT_TRUE(trace_contains("effects", expected_trace))
            << "the overlay must draw for this scene to prove anything";
        const std::vector<RGB> before = grab_viewport(vs);

        // Rotate the cycled bands and repaint at the SAME tick. The water
        // TILES legitimately animate with the cycle; only pixels the
        // overlay blended must hold still, so compare the rows BELOW the
        // water block: pure grass + weather blends.
        for (int i = 0; i < 3; i++)
            scr()->do_cycle(0, 1);
        ASSERT_TRUE(do_redraw(vs));
        const std::vector<RGB> after = grab_viewport(vs);
        size_t grass_diffs = 0;
        const int vw = vs->endx - vs->xloc;
        for (size_t i = 0; i < before.size(); i++)
        {
            const int y = static_cast<int>(i) / vw;
            if (y >= 13 * GRID_SIZE && !same(before[i], after[i]))
                grass_diffs++;
        }
        EXPECT_EQ(0u, grass_diffs)
            << "weather blends over grass must not land in cycled palette "
               "bands (kind " << static_cast<int>(kind) << ")";

        // 16 total rotations restore both bands.
        for (int i = 0; i < 13; i++)
            scr()->do_cycle(0, 1);
    }
    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

TEST(RenderEffects, clouds_drift_when_the_tick_advances)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "on");
    setup_cloud_scene(vs, 2, 1);
    scr()->world().set_weather(WeatherKind::Clouds);

    // Same tick: the overlay is a pure function of (world anchor, tick).
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> frame_a = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> frame_b = grab_viewport(vs);
    ASSERT_TRUE(rects_equal(frame_a, frame_b))
        << "same tick must reproduce the same cloud banks";

    // Advancing the tick (screen::redraw does this once per frame) moves
    // the wind drift, so the banks shift.
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> frame_c = grab_viewport(vs);
    ASSERT_FALSE(rects_equal(frame_a, frame_c))
        << "advancing the render tick must drift the clouds";

    teardown_cloud_scene(vs);
}

// ---- Batch 2: water reflections (shared `reflections` key) + ripples ----

namespace
{

// Fill the base floor's (floor 0) grid with one tile id.
void fill_camera_grid(unsigned char tile)
{
    GameWorld& world = scr()->world();
    const std::size_t cells =
        static_cast<std::size_t>(world.grid.w) * world.grid.h;
    std::fill(world.grid.data.get(), world.grid.data.get() + cells, tile);
}

} // namespace

TEST(RenderEffects, water_reflection_draws_on_camera_floor_water)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "off");

    // Single floor of pure water: floor 0 IS the camera floor.
    fill_camera_grid(static_cast<unsigned char>(PIX_WATER1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    // OFF: no trace, and the scene is stable frame to frame (baseline).
    cfg.apply_setting("effects", "reflections", "off");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "reflections"))
        << "reflections off must not trace";
    const std::vector<RGB> off_a = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off_b = grab_viewport(vs);
    ASSERT_TRUE(rects_equal(off_a, off_b))
        << "reflections off must render byte-identically frame to frame";
    Sint32 sx = 0, sy = 0;
    ground_anchor(*w, vs, sx, sy);
    const int rect_y = sy + w->sizey();
    const std::vector<RGB> off_rect =
        grab_rect(sx, rect_y, w->sizex(), w->sizey());

    // ON: water mirrors exactly like glass (shared reflective-tile LUT).
    cfg.apply_setting("effects", "reflections", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "reflections floor=0 n=1"))
        << "the soldier must reflect in the water";
    const std::vector<RGB> on_rect =
        grab_rect(sx, rect_y, w->sizex(), w->sizey());
    ASSERT_FALSE(rects_equal(off_rect, on_rect))
        << "water reflection must alter pixels below the walker";

    restore_world(vs);
}

TEST(RenderEffects, water_reflection_absent_on_watergrass_edge_tiles)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "off");

    // Edge tiles are part grass: the per-tile LUT must exclude them, so the
    // pre-check skips the blit entirely.
    fill_camera_grid(static_cast<unsigned char>(PIX_WATERGRASS_U));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    cfg.apply_setting("effects", "reflections", "off");
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);

    cfg.apply_setting("effects", "reflections", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "reflections"))
        << "watergrass edge tiles must not mirror";
    const std::vector<RGB> on = grab_viewport(vs);
    ASSERT_TRUE(rects_equal(off, on))
        << "reflections on over edge tiles must render byte-identically";

    restore_world(vs);
}

// Westlands reflective tiles: molten lava and marsh pools mirror entities
// through the same LUT/blit path as water and glass.
TEST(RenderEffects, reflection_draws_on_lava_and_marsh)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "off");

    for (const int pix_id : {PIX_LAVA1, PIX_MARSH2})
    {
        prepare_world();
        fill_camera_grid(static_cast<unsigned char>(pix_id));
        walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, w);
        // Flyer: nothing stands ON lava, but a flyer crossing it must still
        // mirror (reflections key on the tile, not on passability).
        w->stats()->set_bit_flags(BIT_FLYING, 1);
        w->setxy(160, 120);
        vs->control = w;

        cfg.apply_setting("effects", "reflections", "off");
        ASSERT_TRUE(do_redraw(vs));
        Sint32 sx = 0, sy = 0;
        ground_anchor(*w, vs, sx, sy);
        const int rect_y = sy + w->sizey();
        const std::vector<RGB> off_rect =
            grab_rect(sx, rect_y, w->sizex(), w->sizey());

        cfg.apply_setting("effects", "reflections", "on");
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_TRUE(trace_contains("effects", "reflections floor=0 n=1"))
            << "the walker must reflect over tile id " << pix_id;
        ASSERT_FALSE(rects_equal(off_rect,
                                 grab_rect(sx, rect_y, w->sizex(), w->sizey())))
            << "reflection must alter pixels below the walker on tile id "
            << pix_id;
        restore_world(vs);
    }
}

// Snow and ash are dry ground: no reflection may draw there.
TEST(RenderEffects, reflection_absent_on_snow_and_ash)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "off");

    for (const int pix_id : {PIX_SNOW1, PIX_ASH2})
    {
        prepare_world();
        fill_camera_grid(static_cast<unsigned char>(pix_id));
        walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, w);
        w->setxy(160, 120);
        vs->control = w;

        cfg.apply_setting("effects", "reflections", "off");
        ASSERT_TRUE(do_redraw(vs));
        const std::vector<RGB> off = grab_viewport(vs);

        cfg.apply_setting("effects", "reflections", "on");
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_FALSE(trace_contains("effects", "reflections"))
            << "tile id " << pix_id << " must not mirror";
        ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
            << "reflections on over tile id " << pix_id
            << " must render byte-identically";
        restore_world(vs);
    }
}

// A wader in the bog makes ripple rings exactly like pure water; lava makes
// none (nothing stands on it — a flyer hovering over it fails the feet-tile
// check).
TEST(RenderEffects, ripples_draw_on_marsh_but_never_on_lava)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "on");

    for (const int pix_id : {PIX_MARSH1, PIX_MARSH2})
    {
        fill_camera_grid(static_cast<unsigned char>(pix_id));
        walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, w);
        w->setxy(160, 120);
        vs->control = w;
        effects_reset_for_testing();
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_TRUE(trace_contains("effects", "ripples floor=0 n=1"))
            << "the wader must ripple on marsh tile id " << pix_id;
        scr()->world().delete_objects();
        vs->control = nullptr;
    }

    // Lava: a flyer hovers over it, but the feet tile is not in the ripple
    // set — no rings betray it.
    fill_camera_grid(static_cast<unsigned char>(PIX_LAVA1));
    walker* flyer = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, flyer);
    flyer->stats()->set_bit_flags(BIT_FLYING, 1);
    flyer->setxy(160, 120);
    vs->control = flyer;
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "ripples"))
        << "no ripple rings may draw over lava";

    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, ripples_draw_rings_under_walker_on_water)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");

    fill_camera_grid(static_cast<unsigned char>(PIX_WATER2));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    // OFF: no trace, and the scene is stable frame to frame (baseline).
    cfg.apply_setting("effects", "ripples", "off");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "ripples"))
        << "ripples off must not trace";
    const std::vector<RGB> off_a = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off_b = grab_viewport(vs);
    ASSERT_TRUE(rects_equal(off_a, off_b))
        << "ripples off must render byte-identically frame to frame";
    // Rings center on the ground line below the feet and stay within the
    // largest ellipse (rx 8, ry 4).
    Sint32 sx = 0, sy = 0;
    ground_anchor(*w, vs, sx, sy);
    const int cx = sx + w->sizex() / 2;
    const int cy = sy + w->sizey();
    const std::vector<RGB> off_box = grab_rect(cx - 9, cy - 5, 19, 11);
    const RGB off_left = px(cx - 12, cy);
    const RGB off_right = px(cx + 12, cy);
    const RGB off_below = px(cx, cy + 6);

    // ON: the trace fires and ring pixels appear inside the feet box only.
    cfg.apply_setting("effects", "ripples", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "ripples floor=0 n=1"))
        << "one walker ripples on the camera floor";
    const std::vector<RGB> on_box = grab_rect(cx - 9, cy - 5, 19, 11);
    ASSERT_FALSE(rects_equal(off_box, on_box))
        << "ripple rings must alter water pixels around the feet";
    ASSERT_TRUE(same(off_left, px(cx - 12, cy)))
        << "no ring pixel may land beyond rx=8 (left)";
    ASSERT_TRUE(same(off_right, px(cx + 12, cy)))
        << "no ring pixel may land beyond rx=8 (right)";
    ASSERT_TRUE(same(off_below, px(cx, cy + 6)))
        << "no ring pixel may land beyond ry=4 (below)";

    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, ripples_absent_on_grass_and_for_ineligible_walkers)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "on");

    // Dry land: nothing ripples even with the key on, byte-identically.
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    effects_reset_for_testing();
    cfg.apply_setting("effects", "ripples", "off");
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);
    cfg.apply_setting("effects", "ripples", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "ripples"))
        << "feet on grass must not ripple";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "ripples on over grass must render byte-identically";

    // Water floor, but an airborne Living and a Weapon make no rings.
    fill_camera_grid(static_cast<unsigned char>(PIX_WATER1));
    w->set_worldz(8.0f);
    walker* knife = scr()->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->setxy(200, 120);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "ripples"))
        << "airborne units and weapons must not ripple";

    // Feet back in the water: the soldier ripples, the knife still not.
    w->set_worldz(0.0f);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "ripples floor=0 n=1"))
        << "only the wading soldier ripples";

    // Invisible units must not give themselves away.
    w->set_invisibility_left(50);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "ripples"))
        << "invisible units must not ripple";

    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, ripples_absent_on_noncamera_floors)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "on");
    const std::string saved_ghost = cfg.get_setting("graphics", "floor_ghost");
    cfg.apply_setting("graphics", "floor_ghost", "on");

    // Water on floor 0, grass camera floor above: the wader below the camera
    // must not ripple (its floor never runs the camera pass).
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_camera_grid(static_cast<unsigned char>(PIX_WATER1));
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));

    walker* wader = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, wader);
    wader->setxy(160, 120);
    walker* watcher = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, watcher);
    watcher->set_floor(1);
    watcher->setxy(160, 120);
    vs->control = watcher;

    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "ripples"))
        << "wading below the camera floor must not ripple";

    // Cross-check the negative: camera down on floor 0 -> the wader ripples.
    vs->control = wader;
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "ripples floor=0 n=1"))
        << "the same wader ripples once its floor is the camera floor";

    cfg.apply_setting("graphics", "floor_ghost",
                      saved_ghost.empty() ? "on" : saved_ghost);
    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, ripples_deterministic_and_drift_with_tick)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "on");

    fill_camera_grid(static_cast<unsigned char>(PIX_WATER3));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs)); // establish camera geometry
    Sint32 sx = 0, sy = 0;
    ground_anchor(*w, vs, sx, sy);
    const int cx = sx + w->sizex() / 2;
    const int cy = sy + w->sizey();

    // 5 ticks always change at least one ring radius (7 radii over a
    // 32-tick period: floor((p+5)*7/32) != floor(p*7/32) for every phase).
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> a0 = grab_rect(cx - 9, cy - 5, 19, 11);
    for (int i = 0; i < 5; i++)
        effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> a5 = grab_rect(cx - 9, cy - 5, 19, 11);
    ASSERT_FALSE(rects_equal(a0, a5))
        << "advancing the render tick must expand the rings";

    // Reset + replay reproduces both frames exactly.
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> b0 = grab_rect(cx - 9, cy - 5, 19, 11);
    ASSERT_TRUE(rects_equal(a0, b0))
        << "tick 0 must replay identically after a reset";
    for (int i = 0; i < 5; i++)
        effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> b5 = grab_rect(cx - 9, cy - 5, 19, 11);
    ASSERT_TRUE(rects_equal(a5, b5))
        << "tick 5 must replay identically after a reset";

    effects_reset_for_testing();
    restore_world(vs);
}

// ---- Batch 2: weather — cloud ground-shadows + rain + lightning ----

namespace
{

// Channel-wise "never lower, strictly brighter in sum": the blend of a
// white-ish source over any pixel with channel headroom.
bool lightened(const RGB& on, const RGB& off)
{
    return on.r >= off.r && on.g >= off.g && on.b >= off.b &&
        (on.r + on.g + on.b) > (off.r + off.g + off.b);
}

size_t count_changed(const std::vector<RGB>& off, const std::vector<RGB>& on)
{
    size_t n = 0;
    for (size_t i = 0; i < off.size(); i++)
        if (!same(off[i], on[i]))
            n++;
    return n;
}

size_t count_lightened(const std::vector<RGB>& off, const std::vector<RGB>& on)
{
    size_t n = 0;
    for (size_t i = 0; i < off.size(); i++)
        if (lightened(on[i], off[i]))
            n++;
    return n;
}

} // namespace

TEST(RenderEffects, cloud_ground_shadows_darken_pixels_displaced_from_banks)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Clouds);

    // OFF baseline: the ground shadows ride the Clouds kind.
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "clouds"))
        << "weather off must not trace";
    const std::vector<RGB> off = grab_viewport(vs);

    // ON: banks brighten their own pixels while their ground shadows — the
    // field sampled at the fixed sun offset — darken pixels where no bank
    // floats overhead. The palette tops out at 228, so the white bank blend
    // can never lower a channel: every darkened pixel proves a shadow.
    cfg.apply_setting("effects", "weather", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "clouds floor=1"))
        << "cloud overlay must trace on the top floor";
    const std::vector<RGB> on = grab_viewport(vs);
    size_t darker = 0, lighter = 0;
    for (size_t i = 0; i < off.size(); i++)
    {
        if (darkened(on[i], off[i]))
            darker++;
        if (lightened(on[i], off[i]))
            lighter++;
    }
    ASSERT_GT(darker, 0u)
        << "ground shadows must darken pixels displaced from the banks";
    ASSERT_GT(lighter, 0u) << "the banks themselves keep brightening pixels";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

TEST(RenderEffects, rain_streaks_fall_sparsely_full_screen)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Rain);

    // cfg OFF baseline at tick 2 (outside the lightning schedule): no
    // trace, stable frame to frame.
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "rain"))
        << "weather off must not trace";
    ASSERT_FALSE(trace_contains("effects", "lightning"))
        << "no lightning with the display key off";
    const std::vector<RGB> off = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "weather off must render byte-identically frame to frame";

    // ON at the same tick: streaks over the WHOLE viewport (the wet-mask
    // coupling to the cloud field is gone), still sparse by occupancy.
    cfg.apply_setting("effects", "weather", "on");
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "rain floor=1"))
        << "rain must trace when streaks drew";
    ASSERT_FALSE(trace_contains("effects", "lightning"))
        << "tick 2 sits outside the flash schedule";
    const std::vector<RGB> on = grab_viewport(vs);
    const size_t changed = count_changed(off, on);
    ASSERT_GT(changed, 0u) << "some streak pixels must draw";
    ASSERT_LT(changed, off.size() / 20)
        << "rain must stay sparse (under 5% of the viewport)";
    ASSERT_GT(count_lightened(off, on), 0u)
        << "white streak heads must read as lighter pixels";
    bool blue_shift = false;
    for (size_t i = 0; i < off.size() && !blue_shift; i++)
        blue_shift = !same(off[i], on[i]) && on[i].b > off[i].b;
    ASSERT_TRUE(blue_shift)
        << "streak pixels must shift toward pale blue-white";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// Rain must read as FALLING, not twinkling: the streak is longer than the
// per-tick fall, so most streak pixels are still lit one tick later (the
// streak slides through them). The original 3px/5px-per-tick rain had ZERO
// same-position overlap between frames and read as random glitch pixels.
// Rain falls at a slight ANGLE: the streak field is sheared 1px right per
// 4px down, and the fall translates along that slant. Shifting tick-T's
// rain mask by the slant vector must reproduce tick-T+4's mask far better
// than a straight-down shift does.
TEST(RenderEffects, rain_falls_at_a_slant)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "weather", "on");
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor

    const int vw = vs->endx - vs->xloc;
    const auto rain_mask_at_tick = [&](int tick) {
        scr()->world().set_weather(WeatherKind::None);
        effects_reset_for_testing();
        for (int t = 0; t < tick; t++)
            effects_advance_frame();
        do_redraw(vs);
        const std::vector<RGB> off = grab_viewport(vs);
        scr()->world().set_weather(WeatherKind::Rain);
        effects_reset_for_testing();
        for (int t = 0; t < tick; t++)
            effects_advance_frame();
        do_redraw(vs);
        const std::vector<RGB> on = grab_viewport(vs);
        std::vector<bool> rain(on.size(), false);
        for (size_t i = 0; i < on.size(); i++)
            rain[i] = !same(off[i], on[i]);
        return rain;
    };

    // Ticks 2 and 6 (both outside the lightning flash window).
    const std::vector<bool> a = rain_mask_at_tick(2);
    const std::vector<bool> b = rain_mask_at_tick(6);
    // 4 ticks at fall speed 3 = 12px down; the 1-in-4 shear adds 3px right.
    const auto shifted_match = [&](int dx, int dy) {
        size_t lit = 0, hit = 0;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (!a[i])
                continue;
            const int x = static_cast<int>(i) % vw + dx;
            const int y = static_cast<int>(i) / vw + dy;
            if (x < 0 || x >= vw || y < 0 ||
                y >= static_cast<int>(a.size()) / vw)
                continue;
            lit++;
            if (b[static_cast<size_t>(y) * static_cast<size_t>(vw) +
                  static_cast<size_t>(x)])
                hit++;
        }
        return lit == 0 ? 0.0 : static_cast<double>(hit) /
                                    static_cast<double>(lit);
    };
    const double along_slant = shifted_match(3, 12);
    const double straight_down = shifted_match(0, 12);
    fprintf(stderr, "  [rain slant] along=%.2f down=%.2f\n", along_slant,
            straight_down);
    EXPECT_GE(along_slant, 0.8)
        << "the rain pattern must translate along the slant vector";
    EXPECT_LT(straight_down, along_slant - 0.3)
        << "a straight-down shift must NOT reproduce the pattern (rain is "
           "angled now)";

    scr()->world().set_weather(WeatherKind::None);
    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

TEST(RenderEffects, rain_streaks_overlap_frame_to_frame)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Rain);

    // Rain-only pixel sets at ticks 2 and 3 (outside the lightning
    // schedule), each isolated against a weather-off baseline at the same
    // tick.
    const auto rain_pixels_at_tick = [&](int tick) {
        cfg.apply_setting("effects", "weather", "off");
        effects_reset_for_testing();
        for (int t = 0; t < tick; t++)
            effects_advance_frame();
        do_redraw(vs);
        const std::vector<RGB> off = grab_viewport(vs);
        cfg.apply_setting("effects", "weather", "on");
        effects_reset_for_testing();
        for (int t = 0; t < tick; t++)
            effects_advance_frame();
        do_redraw(vs);
        const std::vector<RGB> on = grab_viewport(vs);
        std::vector<bool> rain(on.size(), false);
        for (size_t i = 0; i < on.size(); i++)
            rain[i] = !same(off[i], on[i]);
        return rain;
    };

    const std::vector<bool> a = rain_pixels_at_tick(2);
    const std::vector<bool> b = rain_pixels_at_tick(3);
    size_t lit = 0, still_lit = 0;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i])
        {
            lit++;
            if (b[i])
                still_lit++;
        }
    ASSERT_GT(lit, 0u) << "tick 2 must have rain pixels to measure";
    const double overlap =
        static_cast<double>(still_lit) / static_cast<double>(lit);
    fprintf(stderr, "  [rain overlap] %zu/%zu = %.2f\n", still_lit, lit,
            overlap);
    EXPECT_GE(overlap, 0.4)
        << "consecutive frames must relight most streak pixels (falling "
           "motion, not teleporting sparkle)";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

TEST(RenderEffects, rain_absent_below_top_floor_and_on_single_floor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");

    // A floor overhead: no rain in the covered space. Tick 0 would flash
    // lightning, so byte-identity here proves the whole weather block gates.
    setup_cloud_scene(vs, 2, 0);
    scr()->world().set_weather(WeatherKind::Rain);
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> covered_off = grab_viewport(vs);
    cfg.apply_setting("effects", "weather", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "rain"))
        << "no rain may fall under a floor overhead";
    ASSERT_FALSE(trace_contains("effects", "lightning"))
        << "no lightning under a floor overhead";
    ASSERT_TRUE(rects_equal(covered_off, grab_viewport(vs)))
        << "a Rain kind on a lower floor must render byte-identically";
    teardown_cloud_scene(vs);

    // Indoor single-floor levels have no weather layer at all.
    setup_cloud_scene(vs, 1, 0);
    scr()->world().set_weather(WeatherKind::Rain);
    fill_camera_grid(static_cast<unsigned char>(PIX_CARPET_M));
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> single_off = grab_viewport(vs);
    cfg.apply_setting("effects", "weather", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "rain"))
        << "indoor single-floor levels never rain";
    ASSERT_FALSE(trace_contains("effects", "lightning"))
        << "indoor single-floor levels never flash";
    ASSERT_TRUE(rects_equal(single_off, grab_viewport(vs)))
        << "rain on an indoor single-floor level must render byte-identically";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

TEST(RenderEffects, lightning_flashes_on_schedule_while_raining)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Rain);

    // The schedule flashes ticks 0 (strike) and 1 (afterglow) of each
    // period: both frames trace and brighten (nearly) the whole viewport —
    // the only exceptions are pixels also hit by a blue streak tail.
    for (int tick = 0; tick < 2; tick++)
    {
        cfg.apply_setting("effects", "weather", "off");
        effects_reset_for_testing();
        for (int i = 0; i < tick; i++)
            effects_advance_frame();
        ASSERT_TRUE(do_redraw(vs));
        const std::vector<RGB> off = grab_viewport(vs);

        cfg.apply_setting("effects", "weather", "on");
        effects_reset_for_testing();
        for (int i = 0; i < tick; i++)
            effects_advance_frame();
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_TRUE(trace_contains("effects", "lightning floor=1"))
            << "flash frame " << tick << " must trace";
        const std::vector<RGB> on = grab_viewport(vs);
        ASSERT_GT(count_lightened(off, on), off.size() * 9 / 10)
            << "flash frame " << tick << " must brighten the viewport";
    }

    // Two ticks in the flash is over: rain still falls, no lightning.
    cfg.apply_setting("effects", "weather", "on");
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "lightning"))
        << "tick 2 sits outside the flash schedule";
    ASSERT_TRUE(trace_contains("effects", "rain floor=1"))
        << "rain keeps falling between flashes";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

TEST(RenderEffects, weather_deterministic_and_rain_falls_with_tick)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "on");
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Rain);

    // Rain only (ticks 2/3 dodge the flash): the streak space scrolls down
    // between ticks, so consecutive ticks differ while one tick repeats.
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> a2 = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(a2, grab_viewport(vs)))
        << "the same tick must reproduce the same streaks";
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> a3 = grab_viewport(vs);
    ASSERT_FALSE(rects_equal(a2, a3))
        << "advancing the render tick must move the rain";

    // The Clouds kind (banks + ground shadows): reset + replay reproduces
    // both frames exactly too (the kinds are exclusive, so swap the world).
    scr()->world().set_weather(WeatherKind::Clouds);
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> b2 = grab_viewport(vs);
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> b3 = grab_viewport(vs);
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(b2, grab_viewport(vs)))
        << "tick 2 must replay identically after a reset";
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(b3, grab_viewport(vs)))
        << "tick 3 must replay identically after a reset";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}


// Kinds are mutually exclusive by design: a Clouds sky never rains or
// flashes (even on the lightning schedule's tick 0), and a Rain sky draws
// no banks or ground shadows. The kind traces are authoritative: Clouds
// traces whenever its branch runs, rain/lightning trace whenever they draw.
TEST(RenderEffects, weather_kinds_are_mutually_exclusive)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "weather", "on");
    setup_cloud_scene(vs, 2, 1); // top floor: gates open

    // Clouds at tick 0 — the tick the Rain schedule would flash lightning.
    scr()->world().set_weather(WeatherKind::Clouds);
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> base0 = grab_viewport(vs);
    cfg.apply_setting("effects", "weather", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "clouds floor=1"));
    ASSERT_FALSE(trace_contains("effects", "rain"))
        << "a Clouds sky must not rain";
    ASSERT_FALSE(trace_contains("effects", "lightning"))
        << "a Clouds sky must not flash even on the schedule tick";
    ASSERT_LT(count_lightened(base0, grab_viewport(vs)), base0.size() * 9 / 10)
        << "no full-viewport lightning blend may fire for Clouds";

    // Rain at tick 2 (outside the flash): streaks but no banks/shadows.
    scr()->world().set_weather(WeatherKind::Rain);
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> base2 = grab_viewport(vs);
    cfg.apply_setting("effects", "weather", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "rain floor=1"));
    ASSERT_FALSE(trace_contains("effects", "clouds"))
        << "a Rain sky must not draw cloud banks";
    ASSERT_LT(count_changed(base2, grab_viewport(vs)), base2.size() / 20)
        << "bank + shadow blends would change far more than sparse streaks";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// Full-screen rain: the retired build masked streaks to "wet" cloud-field
// regions; a Rain level now rains over the whole open sky. Every viewport
// quadrant must see streak pixels at one tick — a wet mask left dry zones.
TEST(RenderEffects, rain_falls_across_the_full_viewport_not_a_wet_mask)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Rain);

    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame(); // tick 2: outside the lightning schedule
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);
    cfg.apply_setting("effects", "weather", "on");
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> on = grab_viewport(vs);

    const int vw = static_cast<int>(vs->xview);
    const int vh = static_cast<int>(vs->yview);
    std::array<size_t, 4> quadrant_hits = {};
    for (size_t i = 0; i < off.size(); i++)
    {
        if (same(off[i], on[i]))
            continue;
        const int x = static_cast<int>(i) % vw;
        const int y = static_cast<int>(i) / vw;
        const size_t q = (x >= vw / 2 ? 1u : 0u) + (y >= vh / 2 ? 2u : 0u);
        quadrant_hits[q]++;
    }
    for (size_t q = 0; q < quadrant_hits.size(); q++)
        EXPECT_GT(quadrant_hits[q], 0u)
            << "quadrant " << q << " must see rain (no dry wet-mask zones)";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// ---------------------------------------------------------------------------
// Snow (WeatherKind::Snow): the gentle sibling of rain — 4px streaks at
// 1px/tick, half slant alternating per 32px world-column band, occupancy 3/8,
// no lightning. The rain pins above must keep passing untouched (the shared
// streak path computes bit-identical rain).
// ---------------------------------------------------------------------------

// The engine's streak hash, replicated for the slant-band sign (the test
// derives each 32px column band's lean direction exactly as the shader does).
std::uint32_t test_hash_u32(std::uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;
    return v;
}

TEST(RenderEffects, snow_streaks_fall_sparsely)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Snow);

    // cfg OFF baseline: no trace, byte-identical frame to frame.
    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "snow"))
        << "weather off must not trace";
    const std::vector<RGB> off = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "weather off must render byte-identically frame to frame";

    // ON: pale streaks over the whole sky, softer than rain (occupancy 3/8
    // x streak 4/64 = ~2.3% lit).
    cfg.apply_setting("effects", "weather", "on");
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "snow floor=1"))
        << "snow must trace when flakes drew";
    ASSERT_FALSE(trace_contains("effects", "rain"))
        << "a Snow sky must not trace rain";
    const std::vector<RGB> on = grab_viewport(vs);
    const size_t changed = count_changed(off, on);
    ASSERT_GT(changed, off.size() / 200)
        << "snow density must exceed 0.5% of the viewport";
    ASSERT_LT(changed, off.size() * 35 / 1000)
        << "snow must stay a soft fall (under 3.5% of the viewport)";
    ASSERT_GT(count_lightened(off, on), 0u)
        << "white flake heads must read as lighter pixels";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

TEST(RenderEffects, snow_absent_below_top_floor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    setup_cloud_scene(vs, 2, 0); // a floor overhead
    scr()->world().set_weather(WeatherKind::Snow);

    cfg.apply_setting("effects", "weather", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> covered_off = grab_viewport(vs);
    cfg.apply_setting("effects", "weather", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "snow"))
        << "no snow may fall under a floor overhead";
    ASSERT_TRUE(rects_equal(covered_off, grab_viewport(vs)))
        << "a Snow kind on a lower floor must render byte-identically";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// Snow must read as FALLING: a 4px streak at 1px/tick relights 3 of its 4
// pixels on the next tick (75% theoretical overlap; rain pins 70%).
TEST(RenderEffects, snow_streaks_overlap_frame_to_frame)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Snow);

    const auto snow_pixels_at_tick = [&](int tick) {
        cfg.apply_setting("effects", "weather", "off");
        effects_reset_for_testing();
        for (int t = 0; t < tick; t++)
            effects_advance_frame();
        do_redraw(vs);
        const std::vector<RGB> off = grab_viewport(vs);
        cfg.apply_setting("effects", "weather", "on");
        effects_reset_for_testing();
        for (int t = 0; t < tick; t++)
            effects_advance_frame();
        do_redraw(vs);
        const std::vector<RGB> on = grab_viewport(vs);
        std::vector<bool> snow(on.size(), false);
        for (size_t i = 0; i < on.size(); i++)
            snow[i] = !same(off[i], on[i]);
        return snow;
    };

    const std::vector<bool> a = snow_pixels_at_tick(2);
    const std::vector<bool> b = snow_pixels_at_tick(3);
    size_t lit = 0, still_lit = 0;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i])
        {
            lit++;
            if (b[i])
                still_lit++;
        }
    ASSERT_GT(lit, 0u) << "tick 2 must have snow pixels to measure";
    const double overlap =
        static_cast<double>(still_lit) / static_cast<double>(lit);
    fprintf(stderr, "  [snow overlap] %zu/%zu = %.2f\n", still_lit, lit,
            overlap);
    EXPECT_GE(overlap, 0.6)
        << "consecutive frames must relight most flake pixels (drifting "
           "motes, not twinkle)";

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// Snow leans HALF as hard as rain (1px per 8px down) and each 32px
// world-column band alternates lean direction by hash: shifting tick-T's
// mask by each band's own (s, 8) vector must reproduce tick-T+8's mask,
// while a global straight-down shift must not.
TEST(RenderEffects, snow_falls_at_a_gentle_slant)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "weather", "on");
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor

    const int vw = vs->endx - vs->xloc;
    const auto snow_mask_at_tick = [&](int tick) {
        scr()->world().set_weather(WeatherKind::None);
        effects_reset_for_testing();
        for (int t = 0; t < tick; t++)
            effects_advance_frame();
        do_redraw(vs);
        const std::vector<RGB> off = grab_viewport(vs);
        scr()->world().set_weather(WeatherKind::Snow);
        effects_reset_for_testing();
        for (int t = 0; t < tick; t++)
            effects_advance_frame();
        do_redraw(vs);
        const std::vector<RGB> on = grab_viewport(vs);
        std::vector<bool> snow(on.size(), false);
        for (size_t i = 0; i < on.size(); i++)
            snow[i] = !same(off[i], on[i]);
        return snow;
    };

    const std::vector<bool> a = snow_mask_at_tick(2);
    const std::vector<bool> b = snow_mask_at_tick(10); // T + 8
    // 8 ticks at fall speed 1 = 8px down; each band adds its sign's 1px lean.
    const int vh = static_cast<int>(a.size()) / vw;
    const auto match_fraction = [&](bool per_band, int fixed_dx) {
        size_t lit = 0, hit = 0;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (!a[i])
                continue;
            const int x = static_cast<int>(i) % vw;
            const int y = static_cast<int>(i) / vw;
            const int wx = x + static_cast<int>(vs->topx);
            const int s =
                (test_hash_u32(static_cast<std::uint32_t>(wx >> 5)) & 1u)
                    ? 1 : -1;
            const int dx = per_band ? s : fixed_dx;
            const int tx = x + dx;
            const int ty = y + 8;
            if (tx < 0 || tx >= vw || ty < 0 || ty >= vh)
                continue;
            lit++;
            if (b[static_cast<size_t>(ty) * static_cast<size_t>(vw) +
                  static_cast<size_t>(tx)])
                hit++;
        }
        return lit == 0 ? 0.0 : static_cast<double>(hit) /
                                    static_cast<double>(lit);
    };
    const double along_bands = match_fraction(true, 0);
    const double straight_down = match_fraction(false, 0);
    fprintf(stderr, "  [snow slant] bands=%.2f down=%.2f\n", along_bands,
            straight_down);
    EXPECT_GE(along_bands, 0.75)
        << "the snow pattern must translate along each band's slant vector";
    EXPECT_LT(straight_down, along_bands - 0.3)
        << "a straight-down shift must NOT reproduce the pattern (snow "
           "drifts at a gentle slant)";

    scr()->world().set_weather(WeatherKind::None);
    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// No lightning under snow: ticks 0/1 are the Rain flash window; a Snow sky
// crossing the same schedule boundary must neither trace nor flash.
TEST(RenderEffects, snow_has_no_lightning)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    setup_cloud_scene(vs, 2, 1); // camera on the TOP floor
    scr()->world().set_weather(WeatherKind::Snow);

    for (int tick = 0; tick < 2; tick++)
    {
        cfg.apply_setting("effects", "weather", "off");
        effects_reset_for_testing();
        for (int i = 0; i < tick; i++)
            effects_advance_frame();
        ASSERT_TRUE(do_redraw(vs));
        const std::vector<RGB> off = grab_viewport(vs);

        cfg.apply_setting("effects", "weather", "on");
        effects_reset_for_testing();
        for (int i = 0; i < tick; i++)
            effects_advance_frame();
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_FALSE(trace_contains("effects", "lightning"))
            << "snow must never flash (schedule tick " << tick << ")";
        ASSERT_TRUE(trace_contains("effects", "snow floor=1"))
            << "flakes still fall on the flash-window ticks";
        ASSERT_LT(count_lightened(off, grab_viewport(vs)), off.size() * 9 / 10)
            << "no full-viewport flash blend may fire under snow";
    }

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// All four Westlands terrains are open country: a single-floor map of each
// must vote outdoor so weather reaches it (otherwise the blizzard override
// would never show its own snowfall).
TEST(RenderEffects, westlands_terrains_vote_outdoor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "weather", "on");
    setup_cloud_scene(vs, 1, 0);
    scr()->world().set_weather(WeatherKind::Clouds);

    for (const int pix_id : {PIX_SNOW1, PIX_LAVA1, PIX_MARSH1, PIX_ASH1,
                             PIX_SNOW2, PIX_LAVA2, PIX_MARSH2, PIX_ASH2})
    {
        fill_camera_grid(static_cast<unsigned char>(pix_id));
        effects_reset_for_testing();
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_TRUE(trace_contains("effects", "verdict=1"))
            << "tile id " << pix_id << " must vote outdoor";
        ASSERT_TRUE(trace_contains("effects", "clouds floor=0"))
            << "weather must reach a single-floor map of tile id " << pix_id;
    }

    effects_reset_for_testing();
    teardown_cloud_scene(vs);
}

// The authoritative roll helper: traces kind + seed and reproduces the same
// kind for the same level id under the default-0 test nonce (level-id kinds
// pinned in tests/unit/test_weather.cpp).
TEST(RenderEffects, weather_roll_traces_and_is_deterministic_per_level)
{
    GameWorld& world = scr()->world();
    const int saved_id = world.id;

    world.id = 7;
    og::set_weather_roll_sequence(0u);
    trace_clear();
    world.roll_weather();
    ASSERT_TRUE(trace_contains("game", "weather roll=2 seed=7 seq=0"))
        << "the roll site must trace the rolled kind, seed and sequence";
    ASSERT_EQ(WeatherKind::Rain, world.weather());
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    ASSERT_EQ(WeatherKind::Rain, world.weather())
        << "same (id, nonce, sequence) must reproduce the same kind";

    world.id = 2;
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    ASSERT_EQ(WeatherKind::Clouds, world.weather());

    world.id = saved_id;
    world.set_weather(WeatherKind::None);
}

// ---- Batch 2: per-entity render store, trails, dust, fire glow ----

namespace
{

// Intent isolation: turn every effects key this pass reads off, then each
// test re-enables only the key under scrutiny.
void all_effects_off()
{
    for (const char* key : {"shadows", "reflections", "weather",
                            "ripples", "trails", "dust", "fire_glow",
                            "depth_tint", "screen_shake"})
        cfg.apply_setting("effects", key, "off");
}

int world_to_screen_x(const viewscreen* vs, int wx)
{
    return wx - vs->topx + vs->xloc;
}

int world_to_screen_y(const viewscreen* vs, int wy)
{
    return wy - vs->topy + vs->yloc;
}

// The blend of the stable fire orange (palette 234 = 230,109,0 — the
// non-cycled copy of the fire ramp) can only push a pixel toward warm:
// red never drops, blue never rises, and their gap strictly widens.
bool warm_shifted(const RGB& on, const RGB& off)
{
    return on.r >= off.r && on.b <= off.b &&
        (static_cast<int>(on.r) - static_cast<int>(on.b)) >
            (static_cast<int>(off.r) - static_cast<int>(off.b));
}

} // namespace

TEST(RenderEffects, render_store_pushes_once_per_tick_prunes_and_resets)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "trails", "on");

    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* knife = scr()->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->setxy(200, 120);
    const std::uint32_t id = knife->entity_id();
    ASSERT_NE(0u, id);

    // Only the weapon is tracked (the soldier is no dust candidate on a
    // single-floor level), and a frame tick accepts exactly one push.
    effects_reset_for_testing();
    ASSERT_EQ(0u, effects_store_size());
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_EQ(1u, effects_store_size()) << "only the weapon may be tracked";
    ASSERT_EQ(1u, effects_store_depth(id));
    ASSERT_TRUE(do_redraw(vs)); // a second viewport in the same frame
    ASSERT_EQ(1u, effects_store_depth(id))
        << "a second draw in the same tick must not re-push";
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_EQ(2u, effects_store_depth(id));

    // Depth caps at the store's ring size (6).
    for (int i = 0; i < 8; i++)
    {
        effects_advance_frame();
        ASSERT_TRUE(do_redraw(vs));
    }
    ASSERT_EQ(6u, effects_store_depth(id));

    // Prune boundary: an entity idle for exactly 120 ticks survives, one
    // more tick and effects_advance_frame drops it.
    for (int i = 0; i < 120; i++)
        effects_advance_frame();
    ASSERT_EQ(1u, effects_store_size())
        << "120 idle ticks must not prune yet";
    effects_advance_frame();
    ASSERT_EQ(0u, effects_store_size()) << "121 idle ticks must prune";

    // Re-tracked from scratch on the next draw; reset clears everything.
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_EQ(1u, effects_store_depth(id))
        << "pruning must have discarded the old history";
    effects_reset_for_testing();
    ASSERT_EQ(0u, effects_store_size());

    restore_world(vs);
}

TEST(RenderEffects, trails_draw_fading_dots_behind_moving_weapons)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    // A plain knife (white trail) and a fireball (fire-colored trail), both
    // flying 24px right between the two frames — far enough that the dot at
    // the old position is clear of the sprite at the new one.
    walker* knife = scr()->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    walker* fireball = scr()->world().add_ob(Order::Weapon, FAMILY_FIREBALL);
    ASSERT_NE(nullptr, knife);
    ASSERT_NE(nullptr, fireball);
    ASSERT_NE(nullptr, knife->bmp_data());
    ASSERT_NE(nullptr, fireball->bmp_data());

    // OFF run: fly the weapons, no trace, capture the final frame.
    cfg.apply_setting("effects", "trails", "off");
    effects_reset_for_testing();
    knife->setxy(200, 90);
    fireball->setxy(200, 150);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    knife->setxy(224, 90);
    fireball->setxy(224, 150);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "trails"))
        << "trails off must not trace";
    // Dot anchors: the OLD position centers, in screen coords.
    const int kdot_x = world_to_screen_x(vs, 200 + knife->sizex() / 2);
    const int kdot_y = world_to_screen_y(vs, 90 + knife->sizey() / 2);
    const int fdot_x = world_to_screen_x(vs, 200 + fireball->sizex() / 2);
    const int fdot_y = world_to_screen_y(vs, 150 + fireball->sizey() / 2);
    const RGB off_kdot = px(kdot_x, kdot_y);
    const RGB off_fdot = px(fdot_x, fdot_y);

    // ON run: identical flight, dots appear at the old positions.
    cfg.apply_setting("effects", "trails", "on");
    effects_reset_for_testing();
    knife->setxy(200, 90);
    fireball->setxy(200, 150);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "trails"))
        << "the first frame has no history to trail";
    effects_advance_frame();
    knife->setxy(224, 90);
    fireball->setxy(224, 150);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "trails floor=0 n=2"))
        << "both weapons must leave a dot";
    ASSERT_TRUE(lightened(px(kdot_x, kdot_y), off_kdot))
        << "the knife's white dot must lighten its old position";
    ASSERT_TRUE(warm_shifted(px(fdot_x, fdot_y), off_fdot))
        << "the fireball's dot must burn in fire colors";

    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, trails_absent_without_motion_and_off_camera_floor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "trails", "on");

    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* knife = scr()->world().add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->setxy(200, 120);

    // Stationary weapon: history accumulates but every segment sits closer
    // than 1px to its successor, so no dot may draw.
    cfg.apply_setting("effects", "trails", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);
    cfg.apply_setting("effects", "trails", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "trails"))
        << "a resting weapon must not trail";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "trails on with a resting weapon must render byte-identically";

    // A weapon below the camera floor is never tracked: the pre-pass only
    // pushes and draws on the camera-floor pass.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    w->set_floor(1);
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    knife->setxy(224, 120);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "trails"))
        << "a weapon on a lower floor must not trail";
    ASSERT_EQ(0u, effects_store_size())
        << "off-camera-floor weapons must not enter the store";

    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, dust_specks_fall_under_movers_on_the_floor_above)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    const std::string saved_ghost = cfg.get_setting("graphics", "floor_ghost");
    cfg.apply_setting("graphics", "floor_ghost", "off");

    // Camera floor 0, a mover on floor 1 overhead (invisible: ghosts off),
    // so the ONLY on-vs-off delta can be its falling dust.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, mover);
    mover->set_floor(1);

    // OFF run: the mover walks 6px right across two frames.
    cfg.apply_setting("effects", "dust", "off");
    effects_reset_for_testing();
    mover->setxy(200, 120);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    mover->setxy(206, 120);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "dust"))
        << "dust off must not trace";
    const std::vector<RGB> off = grab_viewport(vs);

    // ON run: identical walk; the second frame sheds specks.
    cfg.apply_setting("effects", "dust", "on");
    effects_reset_for_testing();
    mover->setxy(200, 120);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "dust"))
        << "the first frame has no motion history yet";
    effects_advance_frame();
    mover->setxy(206, 120);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "dust floor=0 n=1"))
        << "one mover overhead must shed dust";
    const std::vector<RGB> on = grab_viewport(vs);

    // Geometry: every changed pixel sits in the speck column under the
    // mover — center x +/- (6px jitter + 2px speck), y from 10px above its
    // top edge through the 12-step fall.
    const int cx = world_to_screen_x(vs, 206 + mover->sizex() / 2);
    const int top = world_to_screen_y(vs, 120) - 10;
    size_t changed = 0;
    for (size_t i = 0; i < off.size(); i++)
    {
        if (same(off[i], on[i]))
            continue;
        changed++;
        const int x = static_cast<int>(vs->xloc) +
            static_cast<int>(i % static_cast<size_t>(vs->xview));
        const int y = static_cast<int>(vs->yloc) +
            static_cast<int>(i / static_cast<size_t>(vs->xview));
        ASSERT_GE(x, cx - 6) << "speck left of the jitter range at y=" << y;
        ASSERT_LE(x, cx + 7) << "speck right of the jitter range at y=" << y;
        ASSERT_GE(y, top) << "speck above the drop start at x=" << x;
        ASSERT_LE(y, top + 13) << "speck below the fall length at x=" << x;
    }
    ASSERT_GT(changed, 0u) << "at least one speck pixel must draw";

    cfg.apply_setting("graphics", "floor_ghost",
                      saved_ghost.empty() ? "on" : saved_ghost);
    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, dust_absent_when_still_on_top_floor_or_single_floor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "dust", "on");
    const std::string saved_ghost = cfg.get_setting("graphics", "floor_ghost");
    cfg.apply_setting("graphics", "floor_ghost", "off");

    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, mover);
    mover->set_floor(1);
    mover->setxy(200, 120);

    // Standing still overhead: tracked, but no dust shakes loose — and the
    // frames stay byte-identical to dust-off.
    cfg.apply_setting("effects", "dust", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);
    cfg.apply_setting("effects", "dust", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "dust"))
        << "a motionless walker overhead must not shed dust";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "dust on with a still mover must render byte-identically";
    ASSERT_EQ(1u, effects_store_size())
        << "the still mover is tracked for motion detection";

    // Camera on the TOP floor: nothing exists overhead, so a mover on the
    // camera floor sheds nothing.
    w->set_floor(1);
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    mover->setxy(206, 120);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "dust"))
        << "no dust may fall on the top floor";

    // Single-floor levels have nobody overhead by construction.
    w->set_floor(0);
    mover->set_floor(0);
    world.set_floor_count(1);
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    mover->setxy(212, 120);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "dust"))
        << "single-floor levels never shed dust";

    cfg.apply_setting("graphics", "floor_ghost",
                      saved_ghost.empty() ? "on" : saved_ghost);
    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, fire_glow_warms_pixels_around_fire_entities)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* fireball = scr()->world().add_ob(Order::Weapon, FAMILY_FIREBALL);
    ASSERT_NE(nullptr, fireball);
    fireball->setxy(220, 120);
    ASSERT_NE(nullptr, fireball->bmp_data());
    // Probe geometry below assumes the glow kernel (radius 12) reaches past
    // the sprite: the probes sit 2px outside the sprite but inside the rim.
    ASSERT_LE(fireball->sizex(), 18);
    ASSERT_LE(fireball->sizey(), 18);
    // An explosion FX glows through the same post-pass (fxlist coverage).
    walker* boom = scr()->world().add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, boom);
    boom->setxy(80, 60);

    // OFF: no trace, stable frames; remember the pixel below the fireball.
    cfg.apply_setting("effects", "fire_glow", "off");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "fire_glow"))
        << "fire glow off must not trace";
    // Map AFTER the first redraw: it pans the camera onto the control, and
    // world_to_screen reads the settled topx/topy.
    const int cx = world_to_screen_x(vs, 220 + fireball->sizex() / 2);
    const int cy = world_to_screen_y(vs, 120 + fireball->sizey() / 2);
    const std::vector<RGB> off_a = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(off_a, grab_viewport(vs)))
        << "fire glow off must render byte-identically frame to frame";
    const RGB off_below = px(cx, cy + 10);

    // ON: both fire entities glow; grass below the fireball warms.
    cfg.apply_setting("effects", "fire_glow", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "fire_glow floor=0 n=2"))
        << "the fireball and the explosion must both glow";
    ASSERT_TRUE(warm_shifted(px(cx, cy + 10), off_below))
        << "the glow must warm the ground below the fireball";

    // The glow follows the worldz raise: lifted 20px, it reaches beside the
    // airborne sprite and no longer touches the ground ring.
    fireball->set_worldz(20.0f);
    const int side_x = cx + fireball->sizex() / 2 + 2;
    cfg.apply_setting("effects", "fire_glow", "off");
    ASSERT_TRUE(do_redraw(vs));
    const RGB off_side = px(side_x, cy - 20);
    const RGB off_ground = px(cx, cy + 10);
    cfg.apply_setting("effects", "fire_glow", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "fire_glow floor=0 n=2"));
    ASSERT_TRUE(warm_shifted(px(side_x, cy - 20), off_side))
        << "the raised glow must reach beside the airborne fireball";
    ASSERT_TRUE(same(off_ground, px(cx, cy + 10)))
        << "the raised glow must leave the ground ring untouched";
    fireball->set_worldz(0.0f);

    effects_reset_for_testing();
    restore_world(vs);
}

// Pins the REFINED flicker: the glow breathes (slow triangle + per-cycle
// jitter) instead of re-rolling at frame rate. Old behavior jumped up to
// 30% of the kernel between frames and fails the per-frame bound here.
TEST(RenderEffects, fire_glow_breathes_smoothly_instead_of_strobing)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "fire_glow", "on");

    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* fireball = scr()->world().add_ob(Order::Weapon, FAMILY_FIREBALL);
    ASSERT_NE(nullptr, fireball);
    fireball->setxy(220, 120);
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs)); // settle the camera before mapping
    const int cx = world_to_screen_x(vs, 220 + fireball->sizex() / 2);
    const int cy = world_to_screen_y(vs, 120 + fireball->sizey() / 2);

    // Sum the red channel over a 13px row through the glow's lower ring
    // (outside the sprite): amplifies the flicker signal ~13x over one
    // pixel while the static grass background cancels frame to frame.
    const auto row_sum_r = [&]() {
        int sum = 0;
        for (int dx = -6; dx <= 6; dx++)
            sum += px(cx + dx, cy + 10).r;
        return sum;
    };

    int prev = row_sum_r();
    int max_step = 0;
    int lo = prev, hi = prev;
    // 96 ticks covers two full fast-pulse cycles (48 ticks each), so the
    // full pulse amplitude lands in the window regardless of the entity's
    // phase offset, plus most of one slow swell.
    for (int t = 1; t <= 96; t++)
    {
        effects_advance_frame();
        ASSERT_TRUE(do_redraw(vs));
        const int cur = row_sum_r();
        max_step = std::max(max_step, std::abs(cur - prev));
        lo = std::min(lo, cur);
        hi = std::max(hi, cur);
        prev = cur;
    }
    fprintf(stderr, "  [glow pulse] max_step=%d swing=%d\n", max_step, hi - lo);
    EXPECT_LE(max_step, 30)
        << "per-frame glow change must stay gentle (strobe regression)";
    // Pins PERCEPTIBILITY, not just presence: the retired ±5%/1.8s single
    // wave read as static in-game; the dual-triangle pulse must swing far
    // enough to be plainly visible. (Threshold calibrated empirically —
    // see the printed swing.)
    EXPECT_GE(hi - lo, 60)
        << "the glow's pulse must be clearly visible across the window";

    effects_reset_for_testing();
    restore_world(vs);
}

// The glow (and fire trail dots) must paint with the STATIC fire ramp copy
// (palette 234), not COLOR_FIRE=224: 224 is ORANGE_START, the first index of
// the band do_cycle rotates every cycle tick in-game, and a glow painted
// with it strobes at the cycle rate regardless of how smooth the alpha is.
TEST(RenderEffects, fire_glow_color_survives_palette_cycling)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "fire_glow", "on");

    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* fireball = scr()->world().add_ob(Order::Weapon, FAMILY_FIREBALL);
    ASSERT_NE(nullptr, fireball);
    fireball->setxy(220, 120);
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs)); // settle the camera before mapping
    const int cx = world_to_screen_x(vs, 220 + fireball->sizex() / 2);
    const int cy = world_to_screen_y(vs, 120 + fireball->sizey() / 2);

    // The glow ring row below the sprite (the sprite itself legitimately
    // uses the cycled fire band — its shimmer is the game's fire animation,
    // so only probe OUTSIDE the sprite where pure glow lands on grass).
    const auto ring_row = [&]() {
        std::vector<RGB> row;
        for (int dx = -6; dx <= 6; dx++)
            row.push_back(px(cx + dx, cy + 10));
        return row;
    };

    const std::vector<RGB> before = ring_row();
    // Rotate the cycled palette bands (orange 8-wide, water 16-wide) a few
    // steps, redraw at the SAME tick, and the glow must not change color.
    for (int i = 0; i < 3; i++)
        scr()->do_cycle(0, 1);
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> after = ring_row();
    bool equal = before.size() == after.size();
    for (size_t i = 0; equal && i < before.size(); i++)
        equal = same(before[i], after[i]);
    EXPECT_TRUE(equal)
        << "the glow ring must not shift color when the palette bands cycle";

    // 16 total rotations return both bands to identity (16 is a multiple of
    // the orange band's 8 and equals the water band's 16).
    for (int i = 0; i < 13; i++)
        scr()->do_cycle(0, 1);

    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, fire_glow_deterministic_flicker_and_camera_floor_gate)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "fire_glow", "on");

    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* fireball = scr()->world().add_ob(Order::Weapon, FAMILY_FIREBALL);
    ASSERT_NE(nullptr, fireball);
    fireball->setxy(220, 120);
    ASSERT_TRUE(do_redraw(vs)); // establish camera geometry
    const int cx = world_to_screen_x(vs, 220 + fireball->sizex() / 2);
    const int cy = world_to_screen_y(vs, 120 + fireball->sizey() / 2);

    // Deterministic: the same tick reproduces the glow box exactly, and a
    // reset + replay reproduces it again.
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> a2 = grab_rect(cx - 14, cy - 14, 29, 29);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(a2, grab_rect(cx - 14, cy - 14, 29, 29)))
        << "the same tick must reproduce the same flicker";
    effects_reset_for_testing();
    effects_advance_frame();
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(a2, grab_rect(cx - 14, cy - 14, 29, 29)))
        << "tick 2 must replay identically after a reset";

    // The flicker drifts with the tick: within a handful of frames the glow
    // box must change at least once.
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> t0 = grab_rect(cx - 14, cy - 14, 29, 29);
    bool flickered = false;
    for (int t = 1; t <= 16 && !flickered; t++)
    {
        effects_advance_frame();
        ASSERT_TRUE(do_redraw(vs));
        flickered = !rects_equal(t0, grab_rect(cx - 14, cy - 14, 29, 29));
    }
    ASSERT_TRUE(flickered) << "the glow must flicker as the tick advances";

    // Camera-floor gate: with the camera a floor above, the fireball's
    // floor never runs the post-pass — no trace, byte-identical on-vs-off.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    w->set_floor(1);
    cfg.apply_setting("effects", "fire_glow", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);
    cfg.apply_setting("effects", "fire_glow", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "fire_glow"))
        << "a fire entity below the camera floor must not glow";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "fire glow below the camera floor must render byte-identically";

    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, depth_tint_cools_below_floor_pixels)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    const std::string saved_ghost = cfg.get_setting("graphics", "floor_ghost");
    cfg.apply_setting("graphics", "floor_ghost", "on");

    // Two floors; the camera floor has one air hole at grid (5,5) — world px
    // 80..95 — revealing the faded floor-0 composite beneath it.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    PixieData& top = world.grid_for_floor(1);
    top.data[5 + top.w * 5] = static_cast<unsigned char>(PIX_AIR);
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->set_floor(1);
    w->setxy(160, 120);
    vs->control = w;

    // OFF: no trace; capture the below-floor content through the hole.
    cfg.apply_setting("effects", "depth_tint", "off");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "depth_tint"))
        << "depth tint off must not trace";
    const int hx = world_to_screen_x(vs, 88);
    const int hy = world_to_screen_y(vs, 88);
    const RGB off = px(hx, hy);
    ASSERT_TRUE(off.r > 0 || off.g > 0)
        << "the air hole must reveal visible below-floor content";

    // ON: the below composite blends toward the cold blue-grey target —
    // this must be a PERCEPTIBLE hue shift, not just a dimming. On pure
    // grass (b == 0) the blue channel must actually RISE (the retired
    // multiplicative mod could never do that, which made the "tint"
    // invisible on green terrain).
    cfg.apply_setting("effects", "depth_tint", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "depth_tint floor=0"))
        << "the below floor's composite must be tinted";
    const RGB on = px(hx, hy);
    ASSERT_LT(on.g, off.g) << "green must fall toward the cold target";
    ASSERT_GT(static_cast<int>(on.b), static_cast<int>(off.b) + 10)
        << "blue must visibly rise toward the cold target (perceptible hue)";
    const int off_blue_share = static_cast<int>(off.b) * 100 /
        std::max(1, off.r + off.g + off.b);
    const int on_blue_share = static_cast<int>(on.b) * 100 /
        std::max(1, on.r + on.g + on.b);
    ASSERT_GT(on_blue_share, off_blue_share + 5)
        << "the below-floor pixel must read COLDER (larger blue share)";

    // CHARACTERS on the below floor tint too: they are drawn INTO the same
    // composited layer as the tiles, so the cold cast covers everything on
    // that floor — pin it on a sprite pixel (the soldier's red armor).
    walker* below_guy = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, below_guy);
    below_guy->set_floor(0);
    below_guy->setxy(static_cast<short>(80), static_cast<short>(80));
    cfg.apply_setting("effects", "depth_tint", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    // Find a strong red sprite pixel through the hole.
    int sx = -1, sy = -1;
    int best_redness = 0;
    RGB sprite_off;
    for (int dy = -10; dy <= 10; dy++)
        for (int dx = -10; dx <= 10; dx++)
        {
            const RGB c = px(hx + dx, hy + dy);
            const int redness = static_cast<int>(c.r) - static_cast<int>(c.b);
            if (c.r > 80 && redness > best_redness)
            {
                sx = hx + dx;
                sy = hy + dy;
                sprite_off = c;
                best_redness = redness;
            }
        }
    ASSERT_GE(sx, 0) << "the below-floor soldier must be visible in the hole";
    cfg.apply_setting("effects", "depth_tint", "on");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const RGB sprite_on = px(sx, sy);
    ASSERT_LT(sprite_on.r, sprite_off.r)
        << "the below-floor sprite's red must fall toward the cold target";
    ASSERT_GT(static_cast<int>(sprite_on.b), static_cast<int>(sprite_off.b) + 10)
        << "the below-floor sprite must gain blue: characters tint too";

    // Deterministic: a reset + replay reproduces the tinted probe box.
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> box = grab_rect(hx - 4, hy - 4, 9, 9);
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(box, grab_rect(hx - 4, hy - 4, 9, 9)))
        << "the tinted composite must replay identically";

    cfg.apply_setting("graphics", "floor_ghost",
                      saved_ghost.empty() ? "on" : saved_ghost);
    restore_world(vs);
}

TEST(RenderEffects, depth_tint_leaves_ghost_floors_above_untinted)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    const std::string saved_ghost = cfg.get_setting("graphics", "floor_ghost");
    cfg.apply_setting("graphics", "floor_ghost", "on");

    // Camera on floor 1 of 3: floor 0 composites tinted, floor 2 ghosts
    // above through the SAME cached layer surface right after it. The
    // hole-free opaque camera floor fully occludes the tinted floor 0, so
    // the frame may differ from the untinted run ONLY if floor_layer_end
    // leaked the color mod into the ghost composite.
    GameWorld& world = scr()->world();
    world.set_floor_count(3);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 2, static_cast<unsigned char>(PIX_GRASS1));
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->set_floor(1);
    w->setxy(160, 120);
    vs->control = w;

    cfg.apply_setting("effects", "depth_tint", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);

    cfg.apply_setting("effects", "depth_tint", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "depth_tint floor=0"))
        << "the occluded below floor still composites tinted";
    ASSERT_FALSE(trace_contains("effects", "depth_tint floor=2"))
        << "ghost floors above the camera must never tint";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "a leaked color mod would tint the ghost floor above";

    cfg.apply_setting("graphics", "floor_ghost",
                      saved_ghost.empty() ? "on" : saved_ghost);
    restore_world(vs);
}

TEST(RenderEffects, screen_shake_jolts_the_frame_and_restores_the_camera)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    // The default random-grass grid gives the frame texture, so any 1-2px
    // camera jolt must change pixels.
    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* boom = scr()->world().add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, boom);
    boom->setxy(200, 100);

    // Find a tick whose jitter is nonzero (the hash may land on (0,0)).
    effects_reset_for_testing();
    int dx = 0, dy = 0;
    for (int t = 0; t < 32 && dx == 0 && dy == 0; t++)
    {
        effects_screen_shake_offset(1, dx, dy);
        if (dx == 0 && dy == 0)
            effects_advance_frame();
    }
    ASSERT_TRUE(dx != 0 || dy != 0) << "no jolting tick within 32 frames";

    // OFF at that tick: no trace; baseline frame + camera.
    cfg.apply_setting("effects", "screen_shake", "off");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "screen_shake"))
        << "screen shake off must not trace";
    const std::vector<RGB> off = grab_viewport(vs);
    const Sint32 topx_off = vs->topx;
    const Sint32 topy_off = vs->topy;

    // ON at the same tick with the identical world: the frame jolts, and
    // topx/topy come back restored after the draw.
    cfg.apply_setting("effects", "screen_shake", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "screen_shake s=1"))
        << "one detonation shakes at strength 1";
    ASSERT_FALSE(rects_equal(off, grab_viewport(vs)))
        << "the jolted frame must differ from the unshaken frame";
    ASSERT_EQ(topx_off, vs->topx) << "topx must be restored after the draw";
    ASSERT_EQ(topy_off, vs->topy) << "topy must be restored after the draw";

    // The published render sample reports the UNSHAKEN camera even while
    // the frame jolts (net render sampling must not see the shake).
    og::runtime::reset_runtime_trace_capture_state();
    const bool saved_active = og::runtime::current_session->gameplay_active_;
    og::runtime::current_session->gameplay_active_ = true;
    cfg.apply_setting("effects", "screen_shake", "off");
    ASSERT_TRUE(do_redraw(vs));
    const auto plain = og::runtime::latest_runtime_render_sample();
    cfg.apply_setting("effects", "screen_shake", "on");
    ASSERT_TRUE(do_redraw(vs));
    const auto shook = og::runtime::latest_runtime_render_sample();
    og::runtime::current_session->gameplay_active_ = saved_active;
    ASSERT_TRUE(plain.has_value());
    ASSERT_TRUE(shook.has_value());
    ASSERT_EQ(plain->camera_topx, shook->camera_topx);
    ASSERT_EQ(plain->camera_topy, shook->camera_topy);
    ASSERT_EQ(plain->camera_topx_float, shook->camera_topx_float);
    ASSERT_EQ(plain->camera_topy_float, shook->camera_topy_float);

    // Determinism: the same tick reproduces the jolted frame exactly, and
    // the offset sequence replays after a reset, bounded by the strength.
    const std::vector<RGB> shaken = grab_viewport(vs);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(shaken, grab_viewport(vs)))
        << "the same tick must reproduce the same jolt";
    std::vector<std::pair<int, int>> seq;
    effects_reset_for_testing();
    for (int t = 0; t < 16; t++)
    {
        effects_screen_shake_offset(2, dx, dy);
        ASSERT_GE(dx, -2);
        ASSERT_LE(dx, 2);
        ASSERT_GE(dy, -2);
        ASSERT_LE(dy, 2);
        seq.emplace_back(dx, dy);
        effects_advance_frame();
    }
    bool varied = false;
    for (const auto& p : seq)
        varied = varied || p != seq[0];
    ASSERT_TRUE(varied) << "the jitter must vary across 16 ticks";
    effects_reset_for_testing();
    for (int t = 0; t < 16; t++)
    {
        effects_screen_shake_offset(2, dx, dy);
        ASSERT_EQ(seq[static_cast<size_t>(t)].first, dx);
        ASSERT_EQ(seq[static_cast<size_t>(t)].second, dy);
        effects_advance_frame();
    }
    effects_screen_shake_offset(0, dx, dy);
    ASSERT_EQ(0, dx);
    ASSERT_EQ(0, dy);

    effects_reset_for_testing();
    restore_world(vs);
}

TEST(RenderEffects, screen_shake_gates_editor_other_floors_and_off_screen)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "screen_shake", "on");

    walker* w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* boom = scr()->world().add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, boom);
    boom->setxy(200, 100);

    // Park the tick on a nonzero jitter so a missing gate WOULD move pixels.
    effects_reset_for_testing();
    int dx = 0, dy = 0;
    for (int t = 0; t < 32 && dx == 0 && dy == 0; t++)
    {
        effects_screen_shake_offset(1, dx, dy);
        if (dx == 0 && dy == 0)
            effects_advance_frame();
    }
    ASSERT_TRUE(dx != 0 || dy != 0) << "no jolting tick within 32 frames";

    // Level editor path (floor override): never shakes — byte-identical.
    vs->editor_floor_override_ = 0;
    cfg.apply_setting("effects", "screen_shake", "off");
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> editor_frame = grab_viewport(vs);
    cfg.apply_setting("effects", "screen_shake", "on");
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "screen_shake"))
        << "the editor path must not shake";
    ASSERT_TRUE(rects_equal(editor_frame, grab_viewport(vs)))
        << "the editor path must render byte-identically with shake on";
    vs->editor_floor_override_ = -1;

    // A detonation on another floor does not reach the camera.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    w->set_floor(1); // camera floor 1; the boom stays on floor 0
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "screen_shake"))
        << "a detonation below the camera floor must not shake";

    // Nor does one outside the viewport.
    boom->set_floor(1);
    boom->setxy(600, 700); // far off the ~(16..336, 36..236) view rect
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "screen_shake"))
        << "an off-screen detonation must not shake";

    // Three on-screen detonations cap the strength at 2.
    world.set_floor_count(1);
    w->set_floor(0);
    boom->set_floor(0);
    boom->setxy(200, 100);
    walker* boom2 = world.add_ob(Order::FX, FAMILY_EXPLOSION);
    walker* boom3 = world.add_ob(Order::FX, FAMILY_BOMB);
    ASSERT_NE(nullptr, boom2);
    ASSERT_NE(nullptr, boom3);
    boom2->setxy(190, 140);
    boom3->setxy(170, 90);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "screen_shake s=2"))
        << "explosion + bomb FX count together, capped at strength 2";

    effects_reset_for_testing();
    restore_world(vs);
}



// ---------------------------------------------------------------------------
// FX-capture: effect close-up scenes for the visual review site
// (scripts/fx_review). Skipped unless OG_FX_CAPTURE_DIR is set, so it costs
// nothing in normal ctest runs. Each scene renders through the real
// renderer with live palette cycling and dumps P6 PPM frames to
// $OG_FX_CAPTURE_DIR/<scene>/NNN.ppm; encode with scripts/fx_review/make_site.py.
// Run standalone with OG_FX_CAPTURE_DIR=<dir>:
//   ./build/ci-test/og_test_rendering --gtest_filter='RenderEffects.zz_capture_effect_scenes'
// ---------------------------------------------------------------------------

namespace fx_capture
{

void dump_frame(viewscreen* vs, const char* scene, int frame)
{
    const char* base = getenv("OG_FX_CAPTURE_DIR");
    ASSERT_NE(nullptr, base);
    const int vw = vs->endx - vs->xloc;
    const int vh = vs->endy - vs->yloc;
    const std::vector<RGB> shot = grab_viewport(vs);
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s", base, scene);
    std::filesystem::create_directories(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/%03d.ppm", base, scene, frame);
    FILE* fp = fopen(path, "wb");
    ASSERT_NE(nullptr, fp);
    fprintf(fp, "P6\n%d %d\n255\n", vw, vh);
    for (const RGB& p : shot)
    {
        fputc(p.r, fp);
        fputc(p.g, fp);
        fputc(p.b, fp);
    }
    fclose(fp);
}

} // namespace fx_capture

TEST(RenderEffects, zz_capture_effect_scenes)
{
    if (!getenv("OG_FX_CAPTURE_DIR"))
        GTEST_SKIP() << "set OG_FX_CAPTURE_DIR to record";

    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    GameWorld& world = scr()->world();
    EffectsCfgGuard guard;
    Sint32 cyc = 0;

    const auto run_scene = [&](const char* name, int frames,
                               const std::function<void()>& setup,
                               const std::function<void(int)>& per_frame) {
        prepare_world();
        all_effects_off();
        world.set_weather(WeatherKind::None);
        fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
        walker* control = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, control);
        control->setxy(static_cast<short>(160), static_cast<short>(120));
        vs->control = control;
        setup();
        effects_reset_for_testing();
        ASSERT_TRUE(do_redraw(vs)); // settle the camera
        for (int wu = 0; wu < 8; wu++) // warmup: converge alpha surfaces
        {
            effects_advance_frame();
            ASSERT_TRUE(do_redraw(vs));
            scr()->do_cycle(cyc++, 3);
        }
        for (int f = 0; f < frames; f++)
        {
            per_frame(f);
            effects_advance_frame();
            ASSERT_TRUE(do_redraw(vs));
            scr()->do_cycle(cyc++, 3); // real-loop palette cycling
            fx_capture::dump_frame(vs, name, f);
        }
        world.delete_objects();
        world.set_floor_count(1);
        vs->control = nullptr;
        effects_reset_for_testing();
    };

    walker* mover = nullptr;
    walker* arc = nullptr;
    walker* extra = nullptr;
    // Fire sprites spawn on a grey frame; advance them onto the flame art.
    const auto fire_frame = [&](walker* w) { if (w) w->set_frame(1); };

    run_scene("shadows", 72,
        [&] {
            cfg.apply_setting("effects", "shadows", "on");
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->setxy(static_cast<short>(90), static_cast<short>(90));
            arc = world.add_ob(Order::Weapon, FAMILY_KNIFE);
            arc->setxy(static_cast<short>(110), static_cast<short>(150));
        },
        [&](int f) {
            mover->setxy(static_cast<short>(90 + f * 2), static_cast<short>(90));
            arc->setxy(static_cast<short>(110 + f * 2), static_cast<short>(150));
            arc->set_worldz(24.0f * std::abs(std::sin(static_cast<float>(f) * 0.13f)));
        });

    run_scene("glass_reflections", 72,
        [&] {
            cfg.apply_setting("effects", "reflections", "on");
            for (int j = 5; j < 11; j++)
                for (int i = 5; i < 15; i++)
                    world.grid.data[i + world.grid.w * j] =
                        static_cast<unsigned char>(PIX_GLASS);
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->setxy(static_cast<short>(70), static_cast<short>(100));
        },
        [&](int f) { mover->setxy(static_cast<short>(70 + f * 2), static_cast<short>(100)); });

    run_scene("water_reflections", 72,
        [&] {
            cfg.apply_setting("effects", "reflections", "on");
            for (int j = 8; j < 13; j++)
                for (int i = 2; i < 22; i++)
                    world.grid.data[i + world.grid.w * j] =
                        static_cast<unsigned char>(PIX_WATER1);
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->setxy(static_cast<short>(70), static_cast<short>(118));
        },
        [&](int f) { mover->setxy(static_cast<short>(70 + f * 2), static_cast<short>(118)); });

    run_scene("ripples", 96,
        [&] {
            cfg.apply_setting("effects", "ripples", "on");
            for (int j = 5; j < 13; j++)
                for (int i = 3; i < 19; i++)
                    world.grid.data[i + world.grid.w * j] =
                        static_cast<unsigned char>(PIX_WATER1);
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->setxy(static_cast<short>(70), static_cast<short>(140));
            extra = world.add_ob(Order::Living, FAMILY_SOLDIER);
            extra->setxy(static_cast<short>(200), static_cast<short>(120));
        },
        [&](int f) { mover->setxy(static_cast<short>(70 + f), static_cast<short>(140)); });

    run_scene("clouds", 144,
        [&] {
            cfg.apply_setting("effects", "weather", "on");
            for (int j = 9; j < 14; j++)
                for (int i = 12; i < 21; i++)
                    world.grid.data[i + world.grid.w * j] =
                        static_cast<unsigned char>(PIX_WATER1);
            world.set_weather(WeatherKind::Clouds);
        },
        [&](int) {});

    run_scene("rain", 128,
        [&] {
            cfg.apply_setting("effects", "weather", "on");
            for (int j = 9; j < 14; j++)
                for (int i = 12; i < 21; i++)
                    world.grid.data[i + world.grid.w * j] =
                        static_cast<unsigned char>(PIX_WATER1);
            world.set_weather(WeatherKind::Rain);
            // Wind the deterministic weather clock so the scheduled
            // lightning flash lands early in the recorded loop.
            for (int t = 0; t < 592; t++)
                effects_advance_frame();
        },
        [&](int) {});

    run_scene("fire_glow", 160,
        [&] {
            cfg.apply_setting("effects", "fire_glow", "on");
            mover = world.add_ob(Order::Living, FAMILY_FIREELEMENTAL);
            mover->setxy(static_cast<short>(110), static_cast<short>(90));
            arc = world.add_ob(Order::Weapon, FAMILY_FIREBALL);
            arc->setxy(static_cast<short>(210), static_cast<short>(130));
            fire_frame(arc);
        },
        [&](int) {});

    run_scene("trails", 72,
        [&] {
            cfg.apply_setting("effects", "trails", "on");
            arc = world.add_ob(Order::Weapon, FAMILY_FIREBALL);
            arc->setxy(static_cast<short>(60), static_cast<short>(80));
            fire_frame(arc);
            extra = world.add_ob(Order::Weapon, FAMILY_KNIFE);
            extra->setxy(static_cast<short>(260), static_cast<short>(150));
        },
        [&](int f) {
            arc->setxy(static_cast<short>(60 + f * 3), static_cast<short>(80 + f));
            extra->setxy(static_cast<short>(260 - f * 3), static_cast<short>(150));
        });

    run_scene("dust", 96,
        [&] {
            cfg.apply_setting("effects", "dust", "on");
            world.set_floor_count(2);
            fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->set_floor(1);
            mover->setxy(static_cast<short>(140), static_cast<short>(110));
        },
        [&](int f) {
            mover->setxy(static_cast<short>(140 + ((f % 40) < 20 ? f % 20 : 20 - f % 20) * 2), static_cast<short>(110));
        });

    run_scene("depth_tint", 96,
        [&] {
            cfg.apply_setting("effects", "depth_tint", "on");
            cfg.apply_setting("effects", "shadows", "on");
            cfg.apply_setting("graphics", "floor_ghost", "on");
            world.set_floor_count(3);
            for (int fl = 1; fl < 3; fl++)
            {
                fill_floor_grid(world, fl, static_cast<unsigned char>(PIX_GRASS1));
                PixieData& g = world.grid_for_floor(fl);
                for (int j = 4; j < 10; j++)
                    for (int i = 5; i < 13; i++)
                        g.data[i + g.w * j] = static_cast<unsigned char>(PIX_AIR);
            }
            vs->control->set_floor(2);
            // Characters on the exposed lower floors: one pacing one floor
            // down, one two floors down — both must read progressively colder.
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->set_floor(1);
            mover->setxy(static_cast<short>(90), static_cast<short>(90));
            extra = world.add_ob(Order::Living, FAMILY_SOLDIER);
            extra->set_floor(0);
            extra->setxy(static_cast<short>(150), static_cast<short>(110));
        },
        [&](int f) {
            mover->setxy(static_cast<short>(90 + ((f % 48) < 24 ? f % 24 : 24 - f % 24) * 2),
                         static_cast<short>(90));
            vs->control->setxy(static_cast<short>(150 + f / 4), static_cast<short>(120));
        });

    run_scene("screen_shake", 48,
        [&] {
            cfg.apply_setting("effects", "screen_shake", "on");
            extra = world.add_ob(Order::FX, FAMILY_EXPLOSION);
            extra->setxy(static_cast<short>(170), static_cast<short>(110));
        },
        [&](int) {});

    run_scene("all_together", 144,
        [&] {
            for (const char* key : {"shadows", "reflections", "weather",
                                    "ripples", "trails", "fire_glow",
                                    "screen_shake"})
                cfg.apply_setting("effects", key, "on");
            for (int j = 9; j < 14; j++)
                for (int i = 2; i < 22; i++)
                    world.grid.data[i + world.grid.w * j] =
                        static_cast<unsigned char>(PIX_WATER1);
            world.set_weather(WeatherKind::Clouds);
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->setxy(static_cast<short>(70), static_cast<short>(130));
            arc = world.add_ob(Order::Weapon, FAMILY_FIREBALL);
            arc->setxy(static_cast<short>(40), static_cast<short>(60));
            fire_frame(arc);
            extra = world.add_ob(Order::FX, FAMILY_EXPLOSION);
            extra->setxy(static_cast<short>(250), static_cast<short>(90));
        },
        [&](int f) {
            mover->setxy(static_cast<short>(70 + f), static_cast<short>(130));
            arc->setxy(static_cast<short>(40 + f * 2), static_cast<short>(60 + f / 2));
            arc->set_worldz(18.0f * std::abs(std::sin(static_cast<float>(f) * 0.1f)));
        });
}

// Pin the TESTING capture hook in screen::buffer_to_screen: with OG_DUMP_DIR
// set, every 3rd presented frame lands as a P6 PPM. scripts/fx_review's menu
// tours depend on this (blocking menu loops never return to a test loop).
#include <fstream>

TEST(RenderEffects, capture_dump_hook_writes_ppm_frames)
{
    const std::string dir =
        std::string(::testing::TempDir()) + "og_dump_hook_pin";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    setenv("OG_DUMP_DIR", dir.c_str(), 1);
    for (int i = 0; i < 3; i++)
        scr()->buffer_to_screen(0, 0, 320, 200);
    unsetenv("OG_DUMP_DIR");

    // 3 presents guarantee at least one dump regardless of the every-3rd
    // counter's phase.
    int frames = 0;
    std::string first;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().extension() == ".ppm")
        {
            frames++;
            if (first.empty() || e.path().string() < first)
                first = e.path().string();
        }
    ASSERT_GE(frames, 1) << "hook should write every 3rd presented frame";
    std::ifstream f(first, std::ios::binary);
    char header[2] = {0, 0};
    f.read(header, 2);
    ASSERT_EQ('P', header[0]);
    ASSERT_EQ('6', header[1]);
    std::filesystem::remove_all(dir);
}
