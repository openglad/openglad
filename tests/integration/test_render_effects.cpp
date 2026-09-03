// Render-pass tests for the multifloor FX pre-pass: ground shadows under
// living units / weapons-in-flight and glass reflections on the camera floor
// (draw_walker_shadow / draw_walker_reflection wired into draw_floor_entities
// and draw_obs). Effects OFF must render byte-identically, so every test
// compares an off-run against an on-run of the same scene.
#include <openglad/interface/render/walker_draw.h>
#include <openglad/platform/sai2x.h>
#include <openglad/interface/render/effects.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/render/pal32.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/gparser.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/runtime_trace.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/render/pal32.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

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

// The GRAPHICS FX click handler (button.cpp), driven directly for the live
// cyclemode pin.
Sint32 toggle_color_cycling();

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
        {"depth_fx", "fog"},      {"screen_shake", "on"},
        {"floor_glide", "on"},
        {"mini_hp_bar", "on"},    {"hit_flash", "on"},
        {"hit_anim", "on"},       {"damage_numbers", "off"},
        {"heal_numbers", "on"},
        // The one key whose identity state is ON (palette cycling has run
        // since the screen constructor) — see the color-cycling pins.
        {"color_cycling", "on"},
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

class RenderSceneGuard
{
public:
    explicit RenderSceneGuard(viewscreen* view)
        : view_(view)
        , saved_tick_(scr()->world().tick_count_)
    {
    }

    ~RenderSceneGuard()
    {
        restore_world(view_);
        scr()->world().tick_count_ = saved_tick_;
    }

    RenderSceneGuard(const RenderSceneGuard&) = delete;
    RenderSceneGuard& operator=(const RenderSceneGuard&) = delete;

private:
    viewscreen* view_ = nullptr;
    std::uint32_t saved_tick_ = 0u;
};

bool do_redraw(viewscreen* vs)
{
    return vs->redraw(&scr()->level_runtime_data(), false);
}

int canonical_palette_index(unsigned char color)
{
    int red = 0;
    int green = 0;
    int blue = 0;
    query_palette_reg(color, &red, &green, &blue);
    for (int index = 0; index < 256; ++index)
    {
        int candidate_red = 0;
        int candidate_green = 0;
        int candidate_blue = 0;
        query_palette_reg(static_cast<unsigned char>(index),
                          &candidate_red, &candidate_green, &candidate_blue);
        if (candidate_red == red && candidate_green == green &&
            candidate_blue == blue)
        {
            return index;
        }
    }
    return static_cast<int>(color);
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
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), tile);
    world.grid_for_floor(f) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
    world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
}

// Swap the session's SDL keystate pointer for a writable fake so redraw sees
// chosen keys "physically held" (KEY_LOOKUP is read through
// isPlayerHoldingKey at render time). Same shape as test_game_loop's guard.
struct SessionKeyStateGuard
{
    const bool* old_keystates = nullptr;
    std::array<bool, SDL_SCANCODE_COUNT> fake_keystates{};

    SessionKeyStateGuard()
        : old_keystates(og::runtime::current_session->keystates_)
    {
        fake_keystates.fill(false);
        og::runtime::current_session->keystates_ = fake_keystates.data();
    }

    ~SessionKeyStateGuard()
    {
        og::runtime::current_session->keystates_ = old_keystates;
    }

    void set(SDL_Keycode key, bool pressed)
    {
        const SDL_Scancode scancode = SDL_GetScancodeFromKey(key, nullptr);
        if (scancode >= 0 && scancode < SDL_SCANCODE_COUNT)
            fake_keystates[static_cast<std::size_t>(scancode)] = pressed;
    }
};

struct KeyBindingGuard
{
    int player;
    int key_enum;
    int old_key;

    KeyBindingGuard(int player_, int key_enum_, int new_key)
        : player(player_)
        , key_enum(key_enum_)
        , old_key(og::runtime::current_session->player_keys_[player_][key_enum_])
    {
        og::runtime::current_session->player_keys_[player][key_enum] = new_key;
    }

    ~KeyBindingGuard()
    {
        og::runtime::current_session->player_keys_[player][key_enum] = old_key;
    }
};

// These pixel-level effect tests pin the historical 320x200 layout they were
// authored against. The fixture restores the live aspect-derived canvas after
// every test so no classic geometry leaks onward on non-16:10 displays.
class RenderEffects : public testing::Test
{
protected:
    void SetUp() override
    {
        scr()->set_world_canvas_pinned_classic(true);
        scr()->relayout_views();
        scr()->set_active_canvas(CanvasTarget::World);
    }

    void TearDown() override
    {
        scr()->set_active_canvas(CanvasTarget::UI);
        scr()->set_world_canvas_pinned_classic(false);
        scr()->relayout_views();
    }
};

} // namespace

TEST_F(RenderEffects, shadow_draws_darker_pixel_below_feet)
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

TEST_F(RenderEffects, weapon_shadow_stays_at_ground_when_raised_by_worldz)
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

TEST_F(RenderEffects, draw_obs_legacy_path_draws_shadow_prepass)
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

TEST_F(RenderEffects, reflection_draws_on_camera_floor_glass)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "weather", "off");

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

    restore_world(vs);
}

TEST_F(RenderEffects, reflection_absent_without_glass)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "weather", "off");

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

    restore_world(vs);
}

TEST_F(RenderEffects, invisible_and_phantom_walkers_cast_neither)
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

TEST_F(RenderEffects, shadow_anchor_follows_lunge_and_recoil_offsets)
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

TEST_F(RenderEffects, walker_sprite_applies_lunge_and_recoil_as_render_offsets)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    RenderSceneGuard scene_guard(vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "attack_lunge", "on");
    cfg.apply_setting("effects", "hit_recoil", "on");
    cfg.apply_setting("effects", "mini_hp_bar", "off");

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs));

    Sint32 sx = 0;
    Sint32 sy = 0;
    ground_anchor(*w, vs, sx, sy);
    const float original_worldx = w->worldx();
    const float original_worldy = w->worldy();

    std::vector<RGB> stationary;
    {
        ScopedCanvasTarget world_canvas(*scr(), CanvasTarget::World);
        scr()->clearbuffer();
        w->set_attack_lunge(0.0f);
        w->set_hit_recoil(0.0f);
        ASSERT_TRUE(draw_walker(*w, vs, 254u, false));
        stationary = grab_rect(sx - 1, sy - 1, w->sizex() + 12, w->sizey() + 2);
    }

    std::vector<RGB> offset;
    {
        ScopedCanvasTarget world_canvas(*scr(), CanvasTarget::World);
        scr()->clearbuffer();
        w->set_attack_lunge(1.0f);
        w->set_attack_lunge_angle(0.0f);
        w->set_hit_recoil(1.0f);
        w->set_hit_recoil_angle(0.0f);
        ASSERT_TRUE(draw_walker(*w, vs, 254u, false));
        offset = grab_rect(sx - 1, sy - 1, w->sizex() + 12, w->sizey() + 2);
    }

    EXPECT_FALSE(rects_equal(stationary, offset));
    EXPECT_FLOAT_EQ(original_worldx, w->worldx());
    EXPECT_FLOAT_EQ(original_worldy, w->worldy());

    w->set_attack_lunge(0.0f);
    w->set_hit_recoil(0.0f);
}

TEST_F(RenderEffects, mini_health_bar_uses_threshold_colors_and_hides_at_full)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    RenderSceneGuard scene_guard(vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "mini_hp_bar", "on");

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    w->stats()->set_max_hitpoints(100.0f);
    w->set_last_hitpoints(80.0f);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs));

    const float world_x = w->worldx() - static_cast<float>(vs->topx) +
        static_cast<float>(vs->xloc);
    const float world_bottom = w->worldy() - static_cast<float>(vs->topy) +
        static_cast<float>(vs->yloc + w->sizey());
    const auto [bar_x, walker_bottom] =
        vs->project_world_point_to_gameplay_ui(world_x, world_bottom);
    const Sint32 bar_y = walker_bottom + 1;

    const auto render_bar_color = [&](float hitpoints) {
        w->stats()->set_hitpoints(hitpoints);
        ScopedGameplayUiCanvas gameplay_ui(*scr());
        scr()->clearbuffer();
        int background_index = -1;
        scr()->get_pixel(bar_x + 1, bar_y, &background_index);
        draw_small_health_bar(w, vs);
        int color_index = -1;
        scr()->get_pixel(bar_x + 1, bar_y, &color_index);
        return std::pair{background_index, color_index};
    };

    const auto low = render_bar_color(25.0f);
    const auto middle = render_bar_color(50.0f);
    const auto high = render_bar_color(75.0f);
    const auto full = render_bar_color(100.0f);
    EXPECT_EQ(canonical_palette_index(LOW_HP_COLOR), low.second);
    EXPECT_EQ(canonical_palette_index(MID_HP_COLOR), middle.second);
    EXPECT_EQ(canonical_palette_index(LIGHT_GREEN), high.second);
    EXPECT_EQ(full.first, full.second);
    EXPECT_FLOAT_EQ(100.0f, w->stats()->hitpoints());
    EXPECT_FLOAT_EQ(100.0f, w->stats()->max_hitpoints());
}

// Issue #149 identity pin. The bar's footprint is now projected from the world
// pane into the gameplay-UI pane. At zoom 1.0 those two rectangles are the same
// rectangle, so the projection must be the exact identity and the bar must keep
// its historical width to the pixel.
TEST_F(RenderEffects, mini_health_bar_width_matches_sprite_at_zoom_one)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    RenderSceneGuard scene_guard(vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "mini_hp_bar", "on");

    ASSERT_EQ(scr()->world_canvas_w(), scr()->gameplay_ui_canvas_w())
        << "this pin assumes the default zoom-1.0 canvas";

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(94.0f);
    w->set_last_hitpoints(94.0f);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs));

    const Sint32 sprite_w = w->sizex();
    const float world_x = w->worldx() - static_cast<float>(vs->topx) +
        static_cast<float>(vs->xloc);
    const float world_bottom = w->worldy() - static_cast<float>(vs->topy) +
        static_cast<float>(vs->yloc + w->sizey());
    const auto [bar_x, walker_bottom] =
        vs->project_world_point_to_gameplay_ui(world_x, world_bottom);
    const Sint32 bar_y = walker_bottom + 1;
    ASSERT_LT(bar_x + sprite_w + 8, vs->endx)
        << "the sampled columns must stay inside the pane";

    ScopedGameplayUiCanvas gameplay_ui(*scr());
    scr()->clearbuffer();
    int background_index = -1;
    scr()->get_pixel(bar_x + sprite_w + 8, bar_y, &background_index);
    draw_small_health_bar(w, vs);

    int hp_index = -1;
    scr()->get_pixel(bar_x, bar_y, &hp_index);
    ASSERT_NE(background_index, hp_index) << "the bar must have been drawn";

    int run = 0;
    while (bar_x + run < vs->endx)
    {
        int sample = -1;
        scr()->get_pixel(bar_x + run, bar_y, &sample);
        if (sample != hp_index)
            break;
        ++run;
    }

    // Same expression the renderer uses: (Sint32)((float)bar_w * ratio).
    const Sint32 expected_cur_w =
        static_cast<Sint32>(static_cast<float>(sprite_w) * (94.0f / 100.0f));
    EXPECT_EQ(expected_cur_w + 1, run)
        << "at zoom 1.0 the filled HP run must stay at the raw sprite scale";

    int outline_index = -1;
    scr()->get_pixel(bar_x - 1, bar_y, &outline_index);
    ASSERT_NE(background_index, outline_index) << "the outline must be drawn";
    int right_edge = -1;
    scr()->get_pixel(bar_x + sprite_w + 1, bar_y, &right_edge);
    EXPECT_EQ(outline_index, right_edge)
        << "the outline's right edge must land at bar_x + sizex + 1";
}

// Issue #244 zoom-1.0 identity pin. The bar's fill height is now scaled by the
// vertical pane ratio, which must be the exact identity at zoom 1.0: the
// classic block stays 2 fill rows + a 1-px outline above and below, 4
// scanlines total, to the pixel.
TEST_F(RenderEffects, mini_health_bar_height_matches_classic_at_zoom_one)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    RenderSceneGuard scene_guard(vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "mini_hp_bar", "on");

    ASSERT_EQ(scr()->world_canvas_w(), scr()->gameplay_ui_canvas_w())
        << "this pin assumes the default zoom-1.0 canvas";

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(94.0f);
    w->set_last_hitpoints(94.0f);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs));

    const Sint32 sprite_w = w->sizex();
    const float world_x = w->worldx() - static_cast<float>(vs->topx) +
        static_cast<float>(vs->xloc);
    const float world_bottom = w->worldy() - static_cast<float>(vs->topy) +
        static_cast<float>(vs->yloc + w->sizey());
    const auto [bar_x, walker_bottom] =
        vs->project_world_point_to_gameplay_ui(world_x, world_bottom);
    const Sint32 bar_y = walker_bottom + 1;
    ASSERT_LT(bar_x + sprite_w + 8, vs->endx)
        << "the sampled columns must stay inside the pane";

    ScopedGameplayUiCanvas gameplay_ui(*scr());
    scr()->clearbuffer();
    int background_index = -1;
    scr()->get_pixel(bar_x + sprite_w + 8, bar_y, &background_index);
    draw_small_health_bar(w, vs);

    int hp_index = -1;
    scr()->get_pixel(bar_x, bar_y, &hp_index);
    ASSERT_NE(background_index, hp_index) << "the bar must have been drawn";

    int vrun = 0;
    while (bar_y + vrun < vs->endy)
    {
        int sample = -1;
        scr()->get_pixel(bar_x, bar_y + vrun, &sample);
        if (sample != hp_index)
            break;
        ++vrun;
    }
    EXPECT_EQ(2, vrun)
        << "at zoom 1.0 the classic fill must stay exactly 2 rows tall";

    int outline_index = -1;
    scr()->get_pixel(bar_x - 1, bar_y, &outline_index);
    ASSERT_NE(background_index, outline_index) << "the outline must be drawn";
    int top_row = -1;
    scr()->get_pixel(bar_x, bar_y - 1, &top_row);
    EXPECT_EQ(outline_index, top_row)
        << "the outline's top row must sit directly above the fill";
    int bottom_row = -1;
    scr()->get_pixel(bar_x, bar_y + 2, &bottom_row);
    EXPECT_EQ(outline_index, bottom_row)
        << "the outline's bottom row must close the classic 4-scanline block";
    int below_block = -1;
    scr()->get_pixel(bar_x, bar_y + 3, &below_block);
    EXPECT_EQ(background_index, below_block)
        << "nothing may be drawn below the classic block";
}

TEST_F(RenderEffects, damage_number_visibility_and_expiration_preserve_cache)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    RenderSceneGuard scene_guard(vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "mini_hp_bar", "off");
    cfg.apply_setting("effects", "attack_lunge", "off");
    cfg.apply_setting("effects", "hit_recoil", "off");
    cfg.apply_setting("effects", "damage_numbers", "off");
    cfg.apply_setting("effects", "heal_numbers", "on");

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    scr()->world().tick_count_ = 80u;
    w->damage_numbers.emplace_back(
        w->worldx(), w->worldy(), 12.0f, RED, scr()->world().tick_count_);

    const float hidden_t = w->damage_numbers.front().t;
    const float hidden_y = w->damage_numbers.front().y;
    ASSERT_TRUE(draw_walker(*w, vs));
    ASSERT_EQ(1u, w->damage_numbers.size());
    EXPECT_FLOAT_EQ(hidden_t, w->damage_numbers.front().t);
    EXPECT_FLOAT_EQ(hidden_y, w->damage_numbers.front().y);
    EXPECT_EQ(0u, damage_number_render_state_count(scr()));

    cfg.apply_setting("effects", "damage_numbers", "on");
    w->damage_numbers.front().t = 0.01f;
    ASSERT_TRUE(draw_walker(*w, vs));
    EXPECT_TRUE(w->damage_numbers.empty());
    EXPECT_EQ(0u, damage_number_render_state_count(scr()));
}

TEST_F(RenderEffects, dead_walkers_are_rejected_by_world_and_tile_draws)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    RenderSceneGuard scene_guard(vs);

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    w->set_dead(1);

    EXPECT_FALSE(draw_walker(*w, vs));
    EXPECT_FALSE(draw_walker_tile(*w, vs));
}

TEST_F(RenderEffects, debug_path_toggle_adds_overlay_without_mutating_path)
{
    viewscreen* const vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    RenderSceneGuard scene_guard(vs);
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "mini_hp_bar", "off");
    cfg.apply_setting("effects", "damage_numbers", "off");
    cfg.apply_setting("effects", "heal_numbers", "off");

    walker* const w = scr()->world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs));

    const auto make_path_state = [](int x, int y) {
        const intptr_t encoded =
            static_cast<intptr_t>(y / GRID_SIZE) * MAP_WIDTH + x / GRID_SIZE;
        return reinterpret_cast<PathState>(encoded);
    };
    w->path_to_foe = {
        make_path_state(112, 96),
        make_path_state(208, 96),
    };
    const std::vector<PathState> expected_path = w->path_to_foe;
    const bool previous_debug_paths =
        og::runtime::current_session->debug_draw_paths_;

    std::vector<RGB> without_path;
    {
        ScopedCanvasTarget world_canvas(*scr(), CanvasTarget::World);
        scr()->clearbuffer();
        og::runtime::current_session->debug_draw_paths_ = false;
        const bool drew_without_path = draw_walker(*w, vs);
        EXPECT_TRUE(drew_without_path);
        without_path = grab_rect(vs->xloc, vs->yloc, vs->xview, vs->yview);
    }

    std::vector<RGB> with_path;
    {
        ScopedCanvasTarget world_canvas(*scr(), CanvasTarget::World);
        scr()->clearbuffer();
        og::runtime::current_session->debug_draw_paths_ = true;
        const bool drew_with_path = draw_walker(*w, vs);
        EXPECT_TRUE(drew_with_path);
        with_path = grab_rect(vs->xloc, vs->yloc, vs->xview, vs->yview);
    }
    og::runtime::current_session->debug_draw_paths_ = previous_debug_paths;

    EXPECT_FALSE(rects_equal(without_path, with_path));
    EXPECT_EQ(expected_path, w->path_to_foe);
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

TEST_F(RenderEffects, clouds_draw_on_top_floor_and_gate_off_byte_identically)
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
TEST_F(RenderEffects, weather_none_with_cfg_on_renders_byte_identically)
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

TEST_F(RenderEffects, clouds_absent_below_the_top_floor)
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

TEST_F(RenderEffects, clouds_absent_on_single_floor_indoor_levels)
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

TEST_F(RenderEffects, clouds_appear_on_single_floor_outdoor_levels)
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

TEST_F(RenderEffects, outdoor_heuristic_threshold_and_per_tick_memo)
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
TEST_F(RenderEffects, weather_blends_survive_palette_cycling)
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
            world.grid.data[static_cast<std::size_t>(i + world.grid.w * j)] =
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

TEST_F(RenderEffects, clouds_drift_when_the_tick_advances)
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

TEST_F(RenderEffects, water_reflection_draws_on_camera_floor_water)
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

TEST_F(RenderEffects, water_reflection_absent_on_watergrass_edge_tiles)
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
TEST_F(RenderEffects, reflection_draws_on_lava_and_marsh)
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
TEST_F(RenderEffects, reflection_absent_on_snow_and_ash)
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

// Marsh is thick bog: only a MOVING wader makes ripple rings (playtest bug
// #14 — a standing unit ringed forever); lava makes none (nothing stands on
// it — a flyer hovering over it fails the feet-tile check).
TEST_F(RenderEffects, ripples_draw_on_marsh_only_while_wading_never_on_lava)
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
        // Frame 0 primes the motion history; frame 1 still standing: the
        // bog stays quiet.
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        effects_advance_frame();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_FALSE(trace_contains("effects", "ripples"))
            << "standing on marsh tile id " << pix_id << " must not ripple";
        // Wading (2px per frame) makes rings.
        w->setxy(static_cast<short>(w->xpos() + 2), w->ypos());
        effects_advance_frame();
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_TRUE(trace_contains("effects", "ripples floor=0 n=1"))
            << "the moving wader must ripple on marsh tile id " << pix_id;
        // Stopping again silences the bog on the next frame.
        effects_advance_frame();
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_FALSE(trace_contains("effects", "ripples"))
            << "a wader that stopped on marsh tile id " << pix_id
            << " must stop rippling";
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

TEST_F(RenderEffects, ripples_draw_rings_under_walker_on_water)
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

TEST_F(RenderEffects, ripples_absent_on_grass_and_for_ineligible_walkers)
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

TEST_F(RenderEffects, ripples_absent_on_noncamera_floors)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    cfg.apply_setting("effects", "shadows", "off");
    cfg.apply_setting("effects", "reflections", "off");
    cfg.apply_setting("effects", "weather", "off");
    cfg.apply_setting("effects", "ripples", "on");

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

    effects_reset_for_testing();
    restore_world(vs);
}

TEST_F(RenderEffects, ripples_deterministic_and_drift_with_tick)
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

TEST_F(RenderEffects, cloud_ground_shadows_darken_pixels_displaced_from_banks)
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

TEST_F(RenderEffects, rain_streaks_fall_sparsely_full_screen)
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
TEST_F(RenderEffects, rain_falls_at_a_slant)
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

TEST_F(RenderEffects, rain_streaks_overlap_frame_to_frame)
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

TEST_F(RenderEffects, rain_absent_below_top_floor_and_on_single_floor)
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

TEST_F(RenderEffects, lightning_flashes_on_schedule_while_raining)
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

TEST_F(RenderEffects, weather_deterministic_and_rain_falls_with_tick)
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
TEST_F(RenderEffects, weather_kinds_are_mutually_exclusive)
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
TEST_F(RenderEffects, rain_falls_across_the_full_viewport_not_a_wet_mask)
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

TEST_F(RenderEffects, snow_streaks_fall_sparsely)
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

TEST_F(RenderEffects, snow_absent_below_top_floor)
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
TEST_F(RenderEffects, snow_streaks_overlap_frame_to_frame)
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
TEST_F(RenderEffects, snow_falls_at_a_gentle_slant)
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
TEST_F(RenderEffects, snow_has_no_lightning)
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
TEST_F(RenderEffects, westlands_terrains_vote_outdoor)
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
TEST_F(RenderEffects, weather_roll_traces_and_is_deterministic_per_level)
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
                            "depth_fx", "screen_shake", "floor_glide"})
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

TEST_F(RenderEffects, render_store_pushes_once_per_tick_prunes_and_resets)
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

TEST_F(RenderEffects, trails_draw_fading_dots_behind_moving_weapons)
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

TEST_F(RenderEffects, trails_absent_without_motion_and_off_camera_floor)
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

TEST_F(RenderEffects, dust_specks_fall_under_movers_on_the_floor_above)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    // Camera floor 0, a mover on floor 1 overhead (invisible: no look-up
    // hold), so the ONLY on-vs-off delta can be its falling dust.
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

    effects_reset_for_testing();
    restore_world(vs);
}

TEST_F(RenderEffects, dust_absent_when_still_on_top_floor_or_single_floor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "dust", "on");

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

    effects_reset_for_testing();
    restore_world(vs);
}

// ---- Falling cue: render-only air-fall transition (shares the dust key) ----

TEST_F(RenderEffects, fall_cue_plays_smear_then_puff_after_an_air_fall)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    // Camera floor 0, a faller overhead on floor 1. The sim's air fall is an
    // instant floor teleport (change_floor + same x/y); the render-only cue
    // must play a short smear + landing puff at the landing spot.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    walker* faller = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, faller);
    faller->set_floor(1);
    faller->setxy(200, 120);
    const std::uint32_t id = faller->entity_id();
    ASSERT_NE(0u, id);

    // OFF run: the identical fall with the dust key off — no trace, no cue
    // state, capture the landed frame as the byte-identity baseline.
    cfg.apply_setting("effects", "dust", "off");
    effects_reset_for_testing();
    faller->set_floor(1);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    faller->set_floor(0); // the instant fall, straight down
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "fall_cue"))
        << "dust off must not track or draw falling cues";
    ASSERT_EQ(0u, effects_fall_cue_frames_left(id));
    const std::vector<RGB> off = grab_viewport(vs);

    // ON run: same fall. The landing frame starts an 8-frame cue.
    cfg.apply_setting("effects", "dust", "on");
    effects_reset_for_testing();
    faller->set_floor(1);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "fall_cue"))
        << "no cue before the floor drops";
    effects_advance_frame();
    faller->set_floor(0);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "fall_cue start id="))
        << "the floor drop must start a cue";
    ASSERT_TRUE(trace_contains("effects", "fall_cue floor=0"))
        << "the cue must draw on the landing floor";
    ASSERT_EQ(8u, effects_fall_cue_frames_left(id));
    const std::vector<RGB> on = grab_viewport(vs);

    // Geometry: every changed pixel belongs to the smear column above the
    // faller (2px wide around its center x, from kFallCueDropPx + streak
    // above the feet down to the feet). The puff hasn't started yet.
    const int cx = world_to_screen_x(vs, 200 + faller->sizex() / 2);
    const int feet = world_to_screen_y(vs, 120 + faller->sizey());
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
        ASSERT_GE(x, cx - 2) << "smear pixel left of the column at y=" << y;
        ASSERT_LE(x, cx + 2) << "smear pixel right of the column at y=" << y;
        ASSERT_GE(y, feet - 24 - 10) << "smear pixel above the drop at x=" << x;
        ASSERT_LE(y, feet) << "smear pixel below the feet at x=" << x;
    }
    ASSERT_GT(changed, 0u) << "the landing frame must draw smear pixels";

    // The last three cue frames add the landing puff: pixels appear beside
    // the smear column (the expanding ring reaches rx=8 around the feet).
    for (int i = 0; i < 6; i++)
        effects_advance_frame();
    ASSERT_EQ(2u, effects_fall_cue_frames_left(id));
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "fall_cue floor=0"));
    const std::vector<RGB> puff = grab_viewport(vs);
    bool puff_ring_pixel = false;
    for (size_t i = 0; i < off.size(); i++)
    {
        if (same(off[i], puff[i]))
            continue;
        const int x = static_cast<int>(vs->xloc) +
            static_cast<int>(i % static_cast<size_t>(vs->xview));
        if (x < cx - 3 || x > cx + 3)
            puff_ring_pixel = true;
    }
    ASSERT_TRUE(puff_ring_pixel)
        << "the landing puff must spread wider than the smear column";

    // Played out: the cue expires, draws nothing, and is pruned.
    for (int i = 0; i < 2; i++)
        effects_advance_frame();
    ASSERT_EQ(0u, effects_fall_cue_frames_left(id));
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "fall_cue"))
        << "an expired cue must not draw";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "after the cue expires the scene must match the dust-off baseline";

    effects_reset_for_testing();
    restore_world(vs);
}

TEST_F(RenderEffects, fall_cue_absent_for_stair_descents_teleports_and_dust_off)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "dust", "on");

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
    const std::uint32_t id = mover->entity_id();

    // A Z-stair descent: the mover relocates in place FROM a stair tile of
    // floor 1 — a deliberate move down, not a fall. No cue may start.
    PixieData& upper = world.grid_for_floor(1);
    const Sint32 gi = (200 + mover->sizex() / 2) / GRID_SIZE;
    const Sint32 gj = (120 + mover->sizey() / 2) / GRID_SIZE;
    upper.data[static_cast<std::size_t>(gi + static_cast<Sint32>(upper.w) * gj)] =
        static_cast<unsigned char>(PIX_ZSTAIR_DOWN);
    mover->set_floor(1);
    mover->setxy(200, 120);
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    mover->set_floor(0);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "fall_cue"))
        << "a stair descent must not read as a fall";
    ASSERT_EQ(0u, effects_fall_cue_frames_left(id));
    upper.data[static_cast<std::size_t>(gi + static_cast<Sint32>(upper.w) * gj)] =
        static_cast<unsigned char>(PIX_GRASS1);

    // A cross-floor teleport: the landing lies far beyond the sim's 4-cell
    // A5 landing nudge, so it cannot be an air fall.
    mover->set_floor(1);
    mover->setxy(200, 120);
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    mover->set_floor(0);
    mover->setxy(40, 40);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "fall_cue"))
        << "a far cross-floor teleport must not read as a fall";
    ASSERT_EQ(0u, effects_fall_cue_frames_left(id));

    // Dust off: the whole pass is gated — the identical fall scene renders
    // byte-identically to a baseline with no fall machinery at all.
    mover->set_floor(1);
    mover->setxy(200, 120);
    cfg.apply_setting("effects", "dust", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    mover->set_floor(0);
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);
    // Replay with dust on but WITHOUT a floor change: no cue, identical too.
    cfg.apply_setting("effects", "dust", "on");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "fall_cue"))
        << "no floor change: no cue";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "dust on without a fall must render byte-identically";

    effects_reset_for_testing();
    restore_world(vs);
}

// ---------------------------------------------------------------------------
// Upper-floor shadow pass (the DEFAULT multifloor look): solid tiles of
// floors above the camera cast flat SE-offset footprint shadows and entities
// up there cast blob shadows; holding KEY_LOOKUP ADDS the floors above as
// faint ghosts (the only way to see them). Floors BELOW the camera always
// render depth-faded, held or not; single-floor stays byte-identical.
// ---------------------------------------------------------------------------

TEST_F(RenderEffects, overhang_shadow_darkens_camera_floor_under_solid_upper_tiles)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_AIR));

    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    // All-air floor above: no shadow pass output, remember the clean frame.
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("render", "overhang_shadow"))
        << "an all-air upper floor must cast nothing";
    const std::vector<RGB> air = grab_viewport(vs);

    // Solidify a 2x2 tile block on floor 1 to the right of the camera center.
    PixieData& upper = world.grid_for_floor(1);
    const Sint32 gi = (vs->topx + vs->xview / 2 + 48) / GRID_SIZE;
    const Sint32 gj = (vs->topy + vs->yview / 2) / GRID_SIZE;
    ASSERT_GE(gi, 0);
    ASSERT_GE(gj, 0);
    ASSERT_LT(gi + 1, static_cast<Sint32>(upper.w));
    ASSERT_LT(gj + 1, static_cast<Sint32>(upper.h));
    for (Sint32 j = gj; j <= gj + 1; j++)
        for (Sint32 i = gi; i <= gi + 1; i++)
            upper.data[static_cast<std::size_t>(i + static_cast<Sint32>(upper.w) * j)] =
                static_cast<unsigned char>(PIX_GRASS1);

    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("render", "overhang_shadow floor=1"))
        << "solid upper tiles must cast the overhang shadow";
    ASSERT_FALSE(trace_contains("render", "blob_shadow"))
        << "no entities live on the upper floor";
    const std::vector<RGB> on = grab_viewport(vs);

    auto rect_index = [&](int wx, int wy) -> size_t
    {
        const int sx = world_to_screen_x(vs, wx) - static_cast<int>(vs->xloc);
        const int sy = world_to_screen_y(vs, wy) - static_cast<int>(vs->yloc);
        EXPECT_GE(sx, 0);
        EXPECT_LT(sx, static_cast<int>(vs->xview));
        EXPECT_GE(sy, 0);
        EXPECT_LT(sy, static_cast<int>(vs->yview));
        return static_cast<size_t>(sy) * static_cast<size_t>(vs->xview) +
            static_cast<size_t>(sx);
    };

    // Interior of the footprint (block center + the 1-story SE offset of
    // 2px): a flat darkened blend, never dithered away (14px from any rim).
    const size_t inside =
        rect_index(gi * GRID_SIZE + GRID_SIZE + 2, gj * GRID_SIZE + GRID_SIZE + 2);
    ASSERT_TRUE(darkened(on[inside], air[inside]))
        << "the footprint interior must darken under a solid upper tile";
    // Well away from the block the upper floor is still air: untouched.
    const size_t outside =
        rect_index((gi - 4) * GRID_SIZE + 8, gj * GRID_SIZE + 8);
    ASSERT_TRUE(same(on[outside], air[outside]))
        << "pixels under upper-floor AIR must not darken";

    restore_world(vs);
}

TEST_F(RenderEffects, overhang_coverage_mask_is_pixel_identical_to_legacy_queries)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();

    GameWorld& world = scr()->world();
    world.set_floor_count(3);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_AIR));
    fill_floor_grid(world, 2, static_cast<unsigned char>(PIX_AIR));
    world.delete_objects(); // isolate tile footprints from upstairs blobs
    vs->control = nullptr;
    vs->current_floor_ = 0;
    scr()->set_active_canvas(CanvasTarget::World);

    // Irregular, different silhouettes on both upper floors exercise joined
    // interiors, holes, checkerboard rims and the twice-darkened 4px band.
    for (Sint32 f = 1; f <= 2; ++f)
    {
        PixieData& grid = world.grid_for_floor(static_cast<int>(f));
        for (Sint32 gj = 0; gj < static_cast<Sint32>(grid.h); ++gj)
            for (Sint32 gi = 0; gi < static_cast<Sint32>(grid.w); ++gi)
                if (((gi * 7 + gj * 11 + f * 3) % 7) < 4)
                    grid.data[static_cast<std::size_t>(gi + static_cast<Sint32>(grid.w) * gj)] =
                        static_cast<unsigned char>(PIX_GRASS1);
    }

    const Sint32 saved_topx = vs->topx;
    const Sint32 saved_topy = vs->topy;
    const Sint32 view_width = vs->endx - vs->xloc;
    const Sint32 view_height = vs->endy - vs->yloc;
    const Sint32 world_width =
        static_cast<Sint32>(world.grid_for_floor(1).w) * GRID_SIZE;
    const Sint32 world_height =
        static_cast<Sint32>(world.grid_for_floor(1).h) * GRID_SIZE;
    const std::array<std::pair<Sint32, Sint32>, 4> camera_origins = {{
        {-7, -5},
        {3, 11},
        {GRID_SIZE - 4, GRID_SIZE + 1},
        {world_width - view_width + 3, world_height - view_height + 2},
    }};

    auto seed_canvas = [&]()
    {
        for (Sint32 vy = 0; vy < view_height; ++vy)
            for (Sint32 vx = 0; vx < view_width; ++vx)
                scr()->pointb(vs->xloc + vx, vs->yloc + vy,
                              static_cast<unsigned char>(
                                  16 + ((vx * 13 + vy * 29) % 200)));
    };

    for (const auto& [topx, topy] : camera_origins)
    {
        SCOPED_TRACE(testing::Message() << "camera=" << topx << "," << topy);
        vs->topx = topx;
        vs->topy = topy;

        seed_canvas();
        EXPECT_TRUE(draw_upper_floor_shadows(vs, world));
        const std::vector<RGB> masked = grab_viewport(vs);
        const UpperFloorShadowMaskStats stats =
            upper_floor_shadow_mask_stats_for_testing();

        // Repaint the exact same target, then run the removed implementation's
        // coverage lambda and draw order verbatim as a reference renderer.
        seed_canvas();
        for (Sint32 f = 1; f <= 2; ++f)
        {
            const Sint32 offset = f * 2;
            const PixieData& grid = world.grid_for_floor(static_cast<int>(f));
            const Sint32 gw = static_cast<Sint32>(grid.w);
            const Sint32 gh = static_cast<Sint32>(grid.h);
            auto legacy_covered = [&](Sint32 wx, Sint32 wy)
            {
                const Sint32 sx = wx - offset;
                const Sint32 sy = wy - offset;
                if (sx < 0 || sy < 0)
                    return false;
                const Sint32 gi = sx / GRID_SIZE;
                const Sint32 gj = sy / GRID_SIZE;
                if (gi >= gw || gj >= gh)
                    return false;
                return grid.data[static_cast<std::size_t>(gi + gw * gj)] !=
                    static_cast<unsigned char>(PIX_AIR);
            };

            for (Sint32 y = vs->yloc; y < vs->endy; ++y)
            {
                const Sint32 wy = y - vs->yloc + vs->topy;
                for (Sint32 x = vs->xloc; x < vs->endx; ++x)
                {
                    const Sint32 wx = x - vs->xloc + vs->topx;
                    if (!legacy_covered(wx, wy))
                        continue;
                    const bool rim = !legacy_covered(wx - 1, wy) ||
                        !legacy_covered(wx + 1, wy) ||
                        !legacy_covered(wx, wy - 1) ||
                        !legacy_covered(wx, wy + 1);
                    if (rim && (((x + y) & 1) != 0))
                        continue;
                    scr()->pointb(x, y, PURE_BLACK, 70);
                    const bool band = !legacy_covered(wx - 4, wy) ||
                        !legacy_covered(wx + 4, wy) ||
                        !legacy_covered(wx, wy - 4) ||
                        !legacy_covered(wx, wy + 4);
                    if (band)
                        scr()->pointb(x, y, PURE_BLACK, 50);
                }
            }
        }
        const std::vector<RGB> legacy = grab_viewport(vs);
        EXPECT_TRUE(rects_equal(masked, legacy))
            << "expanded mask must preserve every legacy blend and dither";

        EXPECT_EQ(static_cast<std::size_t>(2) *
                      static_cast<std::size_t>(view_width + 8) *
                      static_cast<std::size_t>(view_height + 8),
                  stats.expanded_mask_pixels);
        EXPECT_GT(stats.shadowed_pixels, 0u);
        EXPECT_GT(stats.source_tile_probes, 0u);
        EXPECT_GT(stats.replaced_coverage_queries,
                  stats.source_tile_probes * 100u)
            << "the mask must replace hundreds of repeated per-pixel grid "
               "coverage evaluations per source-tile probe";
    }

    vs->topx = saved_topx;
    vs->topy = saved_topy;
    restore_world(vs);
}

TEST_F(RenderEffects, blob_shadow_marks_upper_floor_walker_on_camera_floor)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    // An all-AIR floor above isolates the blob: no overhang footprints, and
    // without the look-up hold the upper-floor sprite itself is never drawn.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_AIR));

    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("render", "blob_shadow"))
        << "nobody is upstairs yet";
    const std::vector<RGB> before = grab_viewport(vs);

    walker* upper = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, upper);
    upper->set_floor(1);
    upper->setxy(200, 120);

    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("render", "blob_shadow floor=1 n=1"))
        << "the upstairs walker must cast one blob shadow";
    const std::vector<RGB> after = grab_viewport(vs);

    // Every changed pixel is a darkened blob pixel inside the squashed
    // footprint: anchor + the 1-story SE offset (2px), height <= 1/3 of the
    // sprite rising from one row below the feet, 2px trimmed off each side.
    const Sint32 spritew = upper->sizex();
    const Sint32 spriteh = upper->sizey();
    const int bx0 = world_to_screen_x(vs, 200) + 2 + 2;
    const int bx1 = world_to_screen_x(vs, 200) + 2 + static_cast<int>(spritew) - 2;
    const int feet = world_to_screen_y(vs, 120) + 2 + static_cast<int>(spriteh);
    const int by0 = feet - (static_cast<int>(spriteh) + 2) / 3;
    size_t changed = 0;
    for (size_t i = 0; i < before.size(); i++)
    {
        if (same(before[i], after[i]))
            continue;
        changed++;
        const int x = static_cast<int>(vs->xloc) +
            static_cast<int>(i % static_cast<size_t>(vs->xview));
        const int y = static_cast<int>(vs->yloc) +
            static_cast<int>(i / static_cast<size_t>(vs->xview));
        ASSERT_GE(x, bx0) << "blob pixel left of the trimmed footprint, y=" << y;
        ASSERT_LT(x, bx1) << "blob pixel right of the trimmed footprint, y=" << y;
        ASSERT_GE(y, by0) << "blob pixel above the squashed height, x=" << x;
        ASSERT_LE(y, feet) << "blob pixel below the feet row, x=" << x;
        ASSERT_TRUE(darkened(after[i], before[i]))
            << "a blob pixel must darken, (" << x << "," << y << ")";
    }
    ASSERT_GT(changed, 0u) << "the blob shadow must touch at least one pixel";

    restore_world(vs);
}

TEST_F(RenderEffects, look_up_hold_swaps_shadow_frame_for_ghost_frame)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    // Camera on the middle floor of three so BOTH ghost effects exist: the
    // faded floor below and the ghost floor above (with an air hole).
    GameWorld& world = scr()->world();
    world.set_floor_count(3);
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_COBBLE_1));
    fill_floor_grid(world, 2, static_cast<unsigned char>(PIX_GRASS1));
    PixieData& top = world.grid_for_floor(2);
    top.data[static_cast<std::size_t>(5 + static_cast<Sint32>(top.w) * 5)] =
        static_cast<unsigned char>(PIX_AIR);

    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->set_floor(1);
    w->setxy(160, 120);
    vs->control = w;
    walker* below = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, below);
    below->setxy(140, 100);
    walker* above = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, above);
    above->set_floor(2);
    above->setxy(190, 130);

    // Default (no hold): the standing presentation — floors BELOW the camera
    // already faded (the fade never needs the hold), the floor above absent,
    // its solid tiles shadow-cast instead.
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("render", "overhang_shadow floor=2"));
    EXPECT_FALSE(vs->ghost_hold_override_);
    EXPECT_EQ(255, vs->floor_render_alpha(1)) << "camera floor opaque";
    EXPECT_EQ(255 - static_cast<int>(viewscreen::kFloorBelowAlphaStep),
              static_cast<int>(vs->floor_render_alpha(0)))
        << "the floor below fades even with the look-up key released";
    const std::vector<RGB> shadow_frame = grab_viewport(vs);

    // HOLD the look-up key: ADDS the floor above as a ghost.
    KeyBindingGuard bind(0, KEY_LOOKUP, KEYCODE_v);
    SessionKeyStateGuard keystates;
    keystates.set(SDLK_V, true);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("render", "overhang_shadow"))
        << "the held frame draws the ghost above, not the shadow pass";
    EXPECT_TRUE(vs->ghost_hold_override_)
        << "redraw must latch the hold for the floor_top extension";
    // Pin the held rendering: camera floor opaque, the floor below one fade
    // step down (identical to the released frame), the floor above at the
    // ghost alpha.
    EXPECT_EQ(255, vs->floor_render_alpha(1));
    EXPECT_EQ(255 - static_cast<int>(viewscreen::kFloorBelowAlphaStep),
              static_cast<int>(vs->floor_render_alpha(0)));
    EXPECT_EQ(static_cast<int>(viewscreen::kFloorGhostAlpha),
              static_cast<int>(vs->floor_render_alpha(2)));
    const std::vector<RGB> ghost_frame = grab_viewport(vs);
    ASSERT_FALSE(rects_equal(ghost_frame, shadow_frame))
        << "ghost and shadow presentations must render differently";

    // Held frames replay byte-identically (the pinned ghost rendering).
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(ghost_frame, grab_viewport(vs)))
        << "a second held frame must equal the first byte for byte";

    // Release: straight back to the shadow look, byte for byte (the fade
    // below is in BOTH frames — only the ghost above comes and goes).
    keystates.set(SDLK_V, false);
    ASSERT_TRUE(do_redraw(vs));
    EXPECT_FALSE(vs->ghost_hold_override_);
    ASSERT_TRUE(rects_equal(shadow_frame, grab_viewport(vs)))
        << "releasing the key must restore the default shadow frame";

    restore_world(vs);
}

TEST_F(RenderEffects, single_floor_renders_byte_identical_in_all_floor_view_modes)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    GameWorld& world = scr()->world();
    world.set_floor_count(1);
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;

    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("render", "overhang_shadow"))
        << "single-floor worlds short-circuit the shadow pass";
    const std::vector<RGB> baseline = grab_viewport(vs);

    KeyBindingGuard bind(0, KEY_LOOKUP, KEYCODE_v);
    SessionKeyStateGuard keystates;
    keystates.set(SDLK_V, true);
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(baseline, grab_viewport(vs)))
        << "single-floor: the look-up hold must change nothing";

    restore_world(vs);
}

TEST_F(RenderEffects, fire_glow_warms_pixels_around_fire_entities)
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
TEST_F(RenderEffects, fire_glow_breathes_smoothly_instead_of_strobing)
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
TEST_F(RenderEffects, fire_glow_color_survives_palette_cycling)
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

TEST_F(RenderEffects, fire_glow_deterministic_flicker_and_camera_floor_gate)
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

TEST_F(RenderEffects, depth_fx_tint_cools_below_floor_pixels)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    // No look-up hold anywhere in this test: the fade-below floor layer that
    // carries the tint renders unconditionally, so depth tint is visible in
    // NORMAL play whenever an air hole exposes a lower floor.

    // Two floors; the camera floor has one air hole at grid (5,5) — world px
    // 80..95 — revealing the faded floor-0 composite beneath it.
    GameWorld& world = scr()->world();
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    PixieData& top = world.grid_for_floor(1);
    top.data[static_cast<std::size_t>(5 + top.w * 5)] = static_cast<unsigned char>(PIX_AIR);
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->set_floor(1);
    w->setxy(160, 120);
    vs->control = w;

    // OFF: no trace; capture the below-floor content through the hole.
    cfg.apply_setting("effects", "depth_fx", "off");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("effects", "depth_fx"))
        << "depth_fx off must not trace";
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
    cfg.apply_setting("effects", "depth_fx", "tint");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "depth_fx mode=1 floor=0"))
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
    cfg.apply_setting("effects", "depth_fx", "off");
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
    cfg.apply_setting("effects", "depth_fx", "tint");
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

    restore_world(vs);
}

// REGRESSION PIN: lower floors seen through PIX_AIR holes must render
// depth-faded (and depth-tintable) in NORMAL play — no look-up hold anywhere
// in this test. Latching the fade-below layer behind the hold once made
// lower floors render opaque and full-brightness through air holes.
TEST_F(RenderEffects, lower_floor_fades_and_tints_without_look_up)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    // Single-floor baseline: the same grass field the hole will expose,
    // rendered opaque at full brightness (camera on it).
    GameWorld& world = scr()->world();
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs)); // settle the camera before world_to_screen
    ASSERT_TRUE(do_redraw(vs));
    // Probe box centered on world (88,88): grid cell (5,5), world px 80..95.
    const int hx = world_to_screen_x(vs, 88);
    const int hy = world_to_screen_y(vs, 88);
    const std::vector<RGB> flat = grab_rect(hx - 4, hy - 4, 9, 9);

    // 3-floor world, camera on TOP: one air hole in the camera floor exposes
    // floor 1 one story down (uniform grass, so the parallax shift/scale of
    // the below-floor composite still samples grass in the probe box).
    world.set_floor_count(3);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 2, static_cast<unsigned char>(PIX_GRASS1));
    PixieData& top = world.grid_for_floor(2);
    top.data[static_cast<std::size_t>(5 + top.w * 5)] = static_cast<unsigned char>(PIX_AIR);
    w->set_floor(2);

    cfg.apply_setting("effects", "depth_fx", "off");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    EXPECT_FALSE(vs->ghost_hold_override_) << "nothing holds the look-up key";
    const std::vector<RGB> faded = grab_rect(hx - 4, hy - 4, 9, 9);
    auto sums = [](const std::vector<RGB>& v, long& r, long& g, long& b)
    {
        r = g = b = 0;
        for (const RGB& c : v)
        {
            r += c.r;
            g += c.g;
            b += c.b;
        }
    };
    long fr = 0, fg = 0, fb = 0, dr = 0, dg = 0, db = 0;
    sums(flat, fr, fg, fb);
    sums(faded, dr, dg, db);
    ASSERT_GT(dr + dg + db, 0L)
        << "the air hole must reveal visible (not black) lower-floor content";
    ASSERT_LT(dr + dg + db, (fr + fg + fb) * 9 / 10)
        << "the lower floor must render DARKER than the same grass rendered "
           "single-floor: the depth fade must not need the look-up hold";

    // Depth tint on: the same released frame goes cold (green falls, blue
    // share rises) — the tint rides the always-on fade layer.
    cfg.apply_setting("effects", "depth_fx", "tint");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "depth_fx mode=1 floor=1"))
        << "the exposed below floor must composite tinted without any hold";
    const std::vector<RGB> tinted = grab_rect(hx - 4, hy - 4, 9, 9);
    long tr = 0, tg = 0, tb = 0;
    sums(tinted, tr, tg, tb);
    ASSERT_LT(tg, dg) << "green must fall toward the cold target";
    const long off_blue_share = db * 100 / std::max(1L, dr + dg + db);
    const long on_blue_share = tb * 100 / std::max(1L, tr + tg + tb);
    ASSERT_GT(on_blue_share, off_blue_share + 5)
        << "the exposed lower floor must read COLDER under depth_fx tint";

    restore_world(vs);
}

TEST_F(RenderEffects, floor_layer_failure_fades_tiles_and_walkers_in_direct_fallback)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "depth_fx", "off");

    GameWorld& world = scr()->world();
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* camera = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, camera);
    camera->setxy(160, 120);
    vs->control = camera;
    ASSERT_TRUE(do_redraw(vs)); // settle camera/viewport
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> opaque_terrain = grab_viewport(vs);
    const int frame_w = static_cast<int>(vs->xview);

    // Expose floor 0 through an all-air camera floor, then force the source
    // allocation seam to fail. The lower terrain must remain visible but use
    // its depth alpha; the failed layer must never be treated as active.
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_AIR));
    camera->set_floor(1);
    const int first_fallbacks =
        scr()->floor_layer_fallback_count_for_testing();
    scr()->fail_next_floor_layer_allocation_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    EXPECT_EQ(first_fallbacks + 1,
              scr()->floor_layer_fallback_count_for_testing());
    EXPECT_TRUE(trace_contains(
        "render", "floor_layer_fallback reason=allocation"));
    EXPECT_FALSE(scr()->floor_layer_redirect_active_for_testing());
    const std::vector<RGB> faded_terrain = grab_viewport(vs);

    bool found_dim_terrain = false;
    for (int y = 32; y < 64 && !found_dim_terrain; ++y)
        for (int x = 32; x < 64 && !found_dim_terrain; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame_w) + static_cast<std::size_t>(x);
            found_dim_terrain = darkened(faded_terrain[index],
                                         opaque_terrain[index]);
        }
    ASSERT_TRUE(found_dim_terrain)
        << "direct fallback terrain must stay visible and depth-faded";

    // Add an actor to that lower floor and force the same fallback again.
    // Capture a final single-floor frame as its opaque reference, then find
    // one sprite pixel proving the fallback actor is visible but not opaque.
    walker* below = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, below);
    below->set_floor(0);
    below->setxy(80, 80);
    scr()->fail_next_floor_layer_allocation_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> faded_with_walker = grab_viewport(vs);
    EXPECT_FALSE(scr()->floor_layer_redirect_active_for_testing());

    world.set_floor_count(1);
    camera->set_floor(0);
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> opaque_with_walker = grab_viewport(vs);

    Sint32 sx = 0, sy = 0;
    ground_anchor(*below, vs, sx, sy);
    bool found_faded_walker = false;
    for (int y = 0; y < below->sizey() && !found_faded_walker; ++y)
        for (int x = 0; x < below->sizex() && !found_faded_walker; ++x)
        {
            const int px_x = static_cast<int>(sx) + x - static_cast<int>(vs->xloc);
            const int px_y = static_cast<int>(sy) + y - static_cast<int>(vs->yloc);
            if (px_x < 0 || px_y < 0 || px_x >= frame_w ||
                px_y >= static_cast<int>(vs->yview))
                continue;
            const std::size_t index =
                static_cast<std::size_t>(px_y) * static_cast<std::size_t>(frame_w) + static_cast<std::size_t>(px_x);
            found_faded_walker =
                !same(opaque_with_walker[index], opaque_terrain[index]) &&
                !same(faded_with_walker[index], faded_terrain[index]) &&
                !same(faded_with_walker[index], opaque_with_walker[index]);
        }
    ASSERT_TRUE(found_faded_walker)
        << "fallback walker must draw with floor alpha: visible, never opaque";

    restore_world(vs);
}

TEST_F(RenderEffects, depth_fx_leaves_ghost_floors_above_untinted)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    // Ghost floors above exist only under the look-up hold now: hold
    // KEY_LOOKUP for the whole scene.
    KeyBindingGuard bind(0, KEY_LOOKUP, KEYCODE_v);
    SessionKeyStateGuard keystates;
    keystates.set(SDLK_V, true);

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

    cfg.apply_setting("effects", "depth_fx", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> off = grab_viewport(vs);

    cfg.apply_setting("effects", "depth_fx", "tint");
    effects_reset_for_testing();
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "depth_fx mode=1 floor=0"))
        << "the occluded below floor still composites tinted";
    ASSERT_FALSE(trace_contains("effects", "depth_fx mode=1 floor=2"))
        << "ghost floors above the camera must never tint";
    ASSERT_TRUE(rects_equal(off, grab_viewport(vs)))
        << "a leaked color mod would tint the ghost floor above";

    restore_world(vs);
}

namespace
{

// The shared depth-fx scene: camera on the top of 3 floors, an air hole at
// grid (5,5) punched through BOTH upper floors, so the probe box reads the
// bottom floor two stories down (the strongest treatment). The MIDDLE
// floor's hole is widened to 3x3 cells: its rim (which composites at a
// different fade alpha and parallax scale) stays well clear of the probe
// box, so every probed pixel belongs to the bottom floor's composite over
// the black-cleared base. Returns the hole-centre screen coords in hx/hy.
void setup_depth_fx_scene(viewscreen* vs, int& hx, int& hy)
{
    GameWorld& world = scr()->world();
    world.set_floor_count(3);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_GRASS1));
    fill_floor_grid(world, 2, static_cast<unsigned char>(PIX_GRASS1));
    PixieData& mid = world.grid_for_floor(1);
    for (int j = 4; j <= 6; j++)
        for (int i = 4; i <= 6; i++)
            mid.data[static_cast<std::size_t>(i + mid.w * j)] = static_cast<unsigned char>(PIX_AIR);
    PixieData& top = world.grid_for_floor(2);
    top.data[static_cast<std::size_t>(5 + top.w * 5)] = static_cast<unsigned char>(PIX_AIR);
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->set_floor(2);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs)); // settle the camera before world_to_screen
    hx = world_to_screen_x(vs, 88);
    hy = world_to_screen_y(vs, 88);
}

// One deterministic frame of the scene at effects tick `tick` under depth
// mode `mode` ("" = leave the key ABSENT), returning the hole probe box.
std::vector<RGB> depth_fx_frame(viewscreen* vs, const char* mode,
                                std::uint32_t tick, int hx, int hy)
{
    if (*mode)
        cfg.apply_setting("effects", "depth_fx", mode);
    else
        cfg.data["effects"].erase("depth_fx");
    effects_reset_for_testing();
    for (std::uint32_t t = 0; t < tick; t++)
        effects_advance_frame();
    do_redraw(vs);
    return grab_rect(hx - 6, hy - 6, 13, 13);
}

} // namespace

// The headline animation pin: fog is the only depth mode whose pixels move
// with the effects frame tick — tint/haze/mist render the same bytes at
// tick 0 and tick 8, fog does not.
TEST_F(RenderEffects, depth_fx_fog_animates_while_the_other_modes_hold_still)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    int hx = 0, hy = 0;
    setup_depth_fx_scene(vs, hx, hy);

    for (const char* mode : {"tint", "haze", "mist"})
    {
        const std::vector<RGB> t0 = depth_fx_frame(vs, mode, 0, hx, hy);
        const std::vector<RGB> t8 = depth_fx_frame(vs, mode, 8, hx, hy);
        ASSERT_TRUE(rects_equal(t0, t8))
            << "mode '" << mode << "' must be static across the frame tick";
    }

    trace_clear();
    const std::vector<RGB> f0 = depth_fx_frame(vs, "fog", 0, hx, hy);
    ASSERT_TRUE(trace_contains("effects", "depth_fx mode=4 floor=0"))
        << "fog must treat the bottom floor's composite";
    ASSERT_TRUE(trace_contains("effects", "depth_fx mode=4 floor=1"))
        << "fog must treat the middle floor's composite";
    const std::vector<RGB> f8 = depth_fx_frame(vs, "fog", 8, hx, hy);
    ASSERT_FALSE(rects_equal(f0, f8))
        << "fog must DRIFT: 8 ticks later the patches have moved";
    // Deterministic: replaying tick 0 reproduces frame 0 exactly.
    ASSERT_TRUE(rects_equal(f0, depth_fx_frame(vs, "fog", 0, hx, hy)))
        << "the fog frame must replay byte-identically at the same tick";

    restore_world(vs);
}

// An ABSENT effects/depth_fx key must render exactly the default, fog —
// the runtime mirror of the cfg loader's default/migration.
TEST_F(RenderEffects, depth_fx_absent_key_renders_as_fog)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    int hx = 0, hy = 0;
    setup_depth_fx_scene(vs, hx, hy);

    const std::vector<RGB> fog = depth_fx_frame(vs, "fog", 4, hx, hy);
    const std::vector<RGB> absent = depth_fx_frame(vs, "", 4, hx, hy);
    ASSERT_TRUE(rects_equal(fog, absent))
        << "a missing depth_fx key must render byte-identical to fog";
    // ...and a junk value falls back to the default too, instead of
    // rendering as some accidental mode.
    const std::vector<RGB> junk = depth_fx_frame(vs, "purple", 4, hx, hy);
    ASSERT_TRUE(rects_equal(fog, junk))
        << "an unrecognized depth_fx value must read as the default (fog)";

    restore_world(vs);
}

// Scene-level mist pin: through the air hole, every pixel of the mist frame
// is either the untouched off-frame pixel or ONE exact mist color — no
// alpha-blended in-betweens anywhere, and the checkerboard density lands
// near its nominal share.
TEST_F(RenderEffects, depth_fx_mist_scene_has_no_blended_in_between_colors)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();
    int hx = 0, hy = 0;
    setup_depth_fx_scene(vs, hx, hy);

    const std::vector<RGB> off = depth_fx_frame(vs, "off", 0, hx, hy);
    const std::vector<RGB> mist = depth_fx_frame(vs, "mist", 0, hx, hy);
    ASSERT_EQ(off.size(), mist.size());
    int changed = 0;
    RGB mist_color{};
    for (std::size_t i = 0; i < mist.size(); i++)
    {
        if (same(mist[i], off[i]))
            continue;
        if (changed == 0)
            mist_color = mist[i];
        else
            ASSERT_TRUE(same(mist[i], mist_color))
                << "every misted pixel must be the SAME exact color (index "
                << i << "): the dither never blends";
        changed++;
    }
    // Two stories down = the (x+y)&1 checkerboard: about half the box.
    // (The box is 169 px; the composite's parallax scale can slide a couple
    // of border pixels in or out, so pin a loose band around 50%.)
    ASSERT_GT(changed, static_cast<int>(mist.size()) * 3 / 10)
        << "the 2-story checkerboard must mist roughly half the hole";
    ASSERT_LT(changed, static_cast<int>(mist.size()) * 7 / 10)
        << "mist must never flood the hole solid";

    restore_world(vs);
}

// Single-floor levels must render byte-identically in EVERY depth mode: the
// below-floor layer path never runs, so the selector cannot touch a classic
// level no matter its value.
TEST_F(RenderEffects, depth_fx_single_floor_byte_identity_in_every_mode)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    prepare_world();
    EffectsCfgGuard guard;
    all_effects_off();

    GameWorld& world = scr()->world();
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w);
    w->setxy(160, 120);
    vs->control = w;
    ASSERT_TRUE(do_redraw(vs)); // settle the camera

    cfg.apply_setting("effects", "depth_fx", "off");
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> base = grab_viewport(vs);

    for (const char* mode : {"tint", "haze", "mist", "fog"})
    {
        cfg.apply_setting("effects", "depth_fx", mode);
        effects_reset_for_testing();
        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_FALSE(trace_contains("effects", "depth_fx"))
            << "mode '" << mode << "' must never trace on a single floor";
        ASSERT_TRUE(rects_equal(base, grab_viewport(vs)))
            << "mode '" << mode
            << "' must render a single-floor level byte-identically";
    }

    restore_world(vs);
}

TEST_F(RenderEffects, screen_shake_jolts_the_frame_and_restores_the_camera)
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

TEST_F(RenderEffects, screen_shake_gates_editor_other_floors_and_off_screen)
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

    // An ARMED thief bomb (FAMILY_BOMB, fuse burning) must NOT add shake:
    // one explosion + one armed bomb on-screen jolt at strength 1, not 2.
    // (The bomb's detonation spawns a FAMILY_EXPLOSION child that shakes.)
    world.set_floor_count(1);
    w->set_floor(0);
    boom->set_floor(0);
    boom->setxy(200, 100);
    walker* armed_bomb = world.add_ob(Order::FX, FAMILY_BOMB);
    ASSERT_NE(nullptr, armed_bomb);
    armed_bomb->setxy(170, 90);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "screen_shake s=1"))
        << "an armed bomb must not shake — only its explosion child does";

    // Three on-screen explosions cap the strength at 2.
    walker* boom2 = world.add_ob(Order::FX, FAMILY_EXPLOSION);
    walker* boom3 = world.add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, boom2);
    ASSERT_NE(nullptr, boom3);
    boom2->setxy(190, 140);
    boom3->setxy(210, 130);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("effects", "screen_shake s=2"))
        << "multiple explosions cap the shake at strength 2";

    effects_reset_for_testing();
    restore_world(vs);
}

// ---------------------------------------------------------------------------
// Floor glide: the continuous-Z camera dolly across classified floor changes
// (docs/floor-glide-design.md). cfg "effects" floor_glide, default ON.
//
// Trigger recipe used throughout: the classifier baselines an entity's floor
// once per effects_frame_tick, so a glide needs (1) a settled redraw that
// baselines the tracker, (2) one frame-clock advance, (3) an in-place
// set_floor + redraw — that redraw is the trigger frame (frames_left == N-1,
// camera feedback at render frame i = 1). Later redraws at a frozen tick
// advance the glide one render frame each (the S8 staleness rung tolerates a
// tick diff <= 2, and 0 always passes). No wall clock anywhere.
// ---------------------------------------------------------------------------

namespace
{

// Grass world with `floors` stacked floors and the control walker standing
// still at (160,120) on `control_floor`.
walker* setup_glide_world(viewscreen* vs, int floors, int control_floor)
{
    prepare_world();
    GameWorld& world = scr()->world();
    world.set_floor_count(floors);
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    for (int f = 1; f < floors; f++)
        fill_floor_grid(world, f, static_cast<unsigned char>(PIX_GRASS1));
    walker* control = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (control == nullptr)
        return nullptr;
    control->set_floor(static_cast<short>(control_floor));
    control->setxy(160, 120);
    vs->control = control;
    return control;
}

// Stamp `pix` on `floor`'s grid at the walker's center cell. The classifier's
// departure-cell probe reads this cell on the floor the walker LEFT, so an
// in-place +/-1 step over a Z-stair tile classifies as Stairs.
void put_tile_under(GameWorld& world, const walker* w, int floor,
                    unsigned char pix)
{
    PixieData& g = world.grid_for_floor(floor);
    const Sint32 cx =
        (static_cast<Sint32>(w->xpos()) + w->sizex() / 2) / GRID_SIZE;
    const Sint32 cy =
        (static_cast<Sint32>(w->ypos()) + w->sizey() / 2) / GRID_SIZE;
    g.data[static_cast<std::size_t>(cx + static_cast<Sint32>(g.w) * cy)] = pix;
}

// One redraw snaps whatever glide state the viewscreen carried over (S8's
// unsigned staleness wraps after effects_reset_for_testing), settles the
// camera, and first-sights the fall tracker; the second redraw re-baselines
// on a steady frame; the final advance leaves the NEXT in-place floor change
// exactly 1 tick fresh — classifiable — when the caller redraws.
void settle_glide_baseline(viewscreen* vs)
{
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
    ASSERT_TRUE(do_redraw(vs));
    effects_advance_frame();
}

// The render sample publishes only for viewport 0 during active gameplay.
struct GameplayActiveGuard
{
    bool prev;
    GameplayActiveGuard()
        : prev(og::runtime::current_session->gameplay_active_)
    {
        og::runtime::current_session->gameplay_active_ = true;
    }
    ~GameplayActiveGuard()
    {
        og::runtime::current_session->gameplay_active_ = prev;
    }
    GameplayActiveGuard(const GameplayActiveGuard&) = delete;
    GameplayActiveGuard& operator=(const GameplayActiveGuard&) = delete;
};

} // namespace

// 10.2-1: the headline OFF byte-identity pair. (a) two identical cfg-off
// stair crossings render byte-identically frame for frame; (b) the cfg-on
// run diverges on at least one mid-glide frame while OFF never activates.
TEST_F(RenderEffects, floor_glide_off_is_byte_identical_and_on_diverges)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();

    GameWorld& world = scr()->world();
    walker* control = setup_glide_world(vs, 2, 0);
    ASSERT_NE(nullptr, control);
    put_tile_under(world, control, 0,
                   static_cast<unsigned char>(PIX_ZSTAIR_UP));

    // One stair-up crossing, replayed identically: grab the trigger frame
    // and the 7 frames after it (mid-glide when ON: N = 16).
    const auto run = [&](const char* mode, std::vector<std::vector<RGB>>* out,
                         bool expect_active) {
        cfg.apply_setting("effects", "floor_glide", mode);
        control->set_floor(0);
        control->setxy(160, 120);
        settle_glide_baseline(vs);
        control->set_floor(1); // the in-place Z-stair step
        out->clear();
        for (int f = 0; f < 8; f++)
        {
            ASSERT_TRUE(do_redraw(vs));
            if (!expect_active)
            {
                ASSERT_EQ(0, vs->floor_glide_frames_left())
                    << mode << " run must never activate, frame " << f;
            }
            out->push_back(grab_viewport(vs));
        }
        if (expect_active)
        {
            ASSERT_GT(vs->floor_glide_frames_left(), 0)
                << "the ON run must still be mid-glide at frame 8 of 16";
        }
    };

    std::vector<std::vector<RGB>> off_a, off_b, on;
    run("off", &off_a, false);
    run("off", &off_b, false);
    ASSERT_EQ(off_a.size(), off_b.size());
    for (size_t f = 0; f < off_a.size(); f++)
        ASSERT_TRUE(rects_equal(off_a[f], off_b[f]))
            << "cfg-off replays must be byte-identical, frame " << f;

    run("on", &on, true);
    bool diverged = false;
    for (size_t f = 0; f < on.size(); f++)
        diverged = diverged || !rects_equal(off_a[f], on[f]);
    ASSERT_TRUE(diverged)
        << "the ON run must differ from OFF on some mid-glide frame";

    effects_reset_for_testing();
    restore_world(vs);
}

// 10.2-2: single-floor structural gate (S2). cfg ON renders byte-identically
// to cfg OFF on a 1-floor level, and even a forced floor hop snaps.
TEST_F(RenderEffects, floor_glide_single_floor_structural_gate)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();

    GameWorld& world = scr()->world();
    walker* control = setup_glide_world(vs, 1, 0);
    ASSERT_NE(nullptr, control);
    put_tile_under(world, control, 0,
                   static_cast<unsigned char>(PIX_ZSTAIR_UP));

    const auto run = [&](const char* mode,
                         std::vector<std::vector<RGB>>* out) {
        cfg.apply_setting("effects", "floor_glide", mode);
        control->setxy(120, 120);
        settle_glide_baseline(vs);
        out->clear();
        for (int f = 0; f < 4; f++)
        {
            // Stride across the stair tile: the only would-be trigger a
            // single-floor level can offer.
            control->setxy(static_cast<std::int32_t>(120 + f * 8),
                           std::int32_t{120});
            effects_advance_frame();
            ASSERT_TRUE(do_redraw(vs));
            ASSERT_EQ(0, vs->floor_glide_frames_left())
                << mode << " run frame " << f;
            out->push_back(grab_viewport(vs));
        }
    };
    std::vector<std::vector<RGB>> off, on;
    run("off", &off);
    run("on", &on);
    ASSERT_EQ(off.size(), on.size());
    for (size_t f = 0; f < off.size(); f++)
        ASSERT_TRUE(rects_equal(off[f], on[f]))
            << "single-floor ON must equal OFF byte for byte, frame " << f;

    // A forced floor hop in a floor_count()==1 world: S2 snaps with cfg ON
    // (driven through the trigger helper directly — the out-of-range floor
    // is never rendered).
    cfg.apply_setting("effects", "floor_glide", "on");
    settle_glide_baseline(vs);
    control->set_floor(1);
    vs->update_floor_glide(world, control);
    EXPECT_EQ(0, vs->floor_glide_frames_left());
    EXPECT_EQ(1, vs->current_floor_) << "S2 still snaps current_floor_";
    EXPECT_EQ(0, vs->floor_glide_cause());
    control->set_floor(0);
    vs->update_floor_glide(world, control);
    EXPECT_EQ(0, vs->current_floor_);

    effects_reset_for_testing();
    restore_world(vs);
}

// 10.2-3: endpoint exactness — the no-pop pin. Frame N of a stair glide is
// byte-identical to the snap-constructed steady state (the final frame takes
// the untouched integer path, structurally). The penultimate frame is
// deliberately NOT pinned byte-wise (<= 1-quantum residual by design).
TEST_F(RenderEffects, floor_glide_endpoint_frame_matches_snapped_steady_state)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "floor_glide", "on");

    GameWorld& world = scr()->world();
    walker* control = setup_glide_world(vs, 2, 0);
    ASSERT_NE(nullptr, control);
    put_tile_under(world, control, 0,
                   static_cast<unsigned char>(PIX_ZSTAIR_UP));

    settle_glide_baseline(vs);
    control->set_floor(1);
    trace_clear();
    ASSERT_TRUE(do_redraw(vs)); // trigger: render frame i = 1
    ASSERT_EQ(15, vs->floor_glide_frames_left());
    ASSERT_EQ(1, vs->floor_glide_cause()) << "cause Stairs";
    ASSERT_TRUE(trace_contains("effects", "floor_glide start cause=1"));
    for (int f = 0; f < 15; f++)
        ASSERT_TRUE(do_redraw(vs));
    ASSERT_EQ(0, vs->floor_glide_frames_left())
        << "redraw 16 of 16 must render the untouched integer path";
    const std::vector<RGB> end_frame = grab_viewport(vs);

    // Snap-construct the identical steady state: cfg OFF renders the
    // pre-glide integer path over the same scene at the same frame_tick.
    cfg.apply_setting("effects", "floor_glide", "off");
    ASSERT_TRUE(do_redraw(vs));
    const std::vector<RGB> steady = grab_viewport(vs);
    ASSERT_TRUE(rects_equal(end_frame, steady))
        << "the final glide frame must equal steady state byte for byte";

    // Twin-drift pin (R1): the no-arg gameplay overload drives the same
    // trigger through the shared helper.
    cfg.apply_setting("effects", "floor_glide", "on");
    control->set_floor(0);
    effects_reset_for_testing();
    ASSERT_TRUE(vs->redraw());
    effects_advance_frame();
    ASSERT_TRUE(vs->redraw());
    effects_advance_frame();
    control->set_floor(1);
    ASSERT_TRUE(vs->redraw());
    ASSERT_EQ(15, vs->floor_glide_frames_left())
        << "the no-arg redraw overload must start the same glide";

    effects_reset_for_testing();
    restore_world(vs);
}

// 10.2-4: cause/duration matrix via introspection. Stairs = 16 frames both
// directions; falls scale 9/12/14 with the story count (clamped at 3);
// teleports (rise without stair, multi-floor stair jump, drop beyond the
// landing nudge) never animate.
TEST_F(RenderEffects, floor_glide_cause_and_duration_matrix)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "floor_glide", "on");
    GameWorld& world = scr()->world();

    struct Case
    {
        const char* name;
        int floors;
        int from;
        int to;
        bool stair_under; // Z-stair on the departure floor's cell
        bool xy_jump;     // relocate beyond the fall nudge => Teleport
        Sint32 want_left; // frames_left on the trigger frame (N - 1)
        Sint32 want_cause; // 0 None / 1 Stairs / 2 Fall
    };
    const Case cases[] = {
        {"stair_up", 2, 0, 1, true, false, 15, 1},
        {"stair_down", 2, 1, 0, true, false, 15, 1},
        {"fall_1_story", 2, 1, 0, false, false, 8, 2},
        {"fall_2_stories", 3, 2, 0, false, false, 11, 2},
        {"fall_3_stories_clamps_n", 4, 3, 0, false, false, 13, 2},
        {"fall_4_stories_still_clamped", 5, 4, 0, false, false, 13, 2},
        {"teleport_drop_beyond_nudge", 2, 1, 0, false, true, 0, 0},
        {"rise_without_stair", 2, 0, 1, false, false, 0, 0},
        {"stair_jump_2_up", 3, 0, 2, true, false, 0, 0},
    };
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.name);
        walker* control = setup_glide_world(vs, c.floors, c.from);
        ASSERT_NE(nullptr, control);
        if (c.stair_under)
            put_tile_under(world, control, c.from,
                           static_cast<unsigned char>(
                               c.to > c.from ? PIX_ZSTAIR_UP
                                             : PIX_ZSTAIR_DOWN));
        settle_glide_baseline(vs);
        control->set_floor(static_cast<short>(c.to));
        if (c.xy_jump) // 6 cells > the 5-cell kFallCueMaxNudgePx
            control->setxy(static_cast<std::int32_t>(160 + 6 * GRID_SIZE),
                           std::int32_t{120});
        ASSERT_TRUE(do_redraw(vs));
        EXPECT_EQ(c.want_left, vs->floor_glide_frames_left());
        EXPECT_EQ(c.want_cause, vs->floor_glide_cause());
        EXPECT_EQ(c.to, vs->current_floor_)
            << "integer current_floor_ snaps to the destination at frame 0";
        effects_reset_for_testing();
        restore_world(vs);
    }
}

// 10.2-5: table-driven suppression ladder. Every rung snaps (frames_left 0,
// cause None, current_floor_ assigned exactly as the pre-glide code did);
// the unsuppressed control row proves the shared attempt is glide-worthy.
TEST_F(RenderEffects, floor_glide_suppression_ladder_snaps)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    GameWorld& world = scr()->world();

    enum class Rung
    {
        None,        // control row: the attempt must actually glide
        CfgOff,      // S1
        EditorOverride, // S3 (S2 is covered by the structural-gate test)
        Authoring,   // S4
        ControlNull, // S5
        ControlSwap, // S6
        WorldKey,    // S7 identity
        WorldTick,   // S7 tick monotonicity
        Stale,       // S8
        NoRecord,    // S10 classification absent
    };
    struct Row
    {
        const char* name;
        Rung rung;
    };
    const Row rows[] = {
        {"control_row_glides", Rung::None},
        {"s1_cfg_off", Rung::CfgOff},
        {"s3_editor_override", Rung::EditorOverride},
        {"s4_authoring_view", Rung::Authoring},
        {"s5_control_null", Rung::ControlNull},
        {"s6_control_id_swap", Rung::ControlSwap},
        {"s7_world_key_swap", Rung::WorldKey},
        {"s7_world_tick_reset", Rung::WorldTick},
        {"s8_staleness", Rung::Stale},
        {"s10_classification_absent", Rung::NoRecord},
    };
    for (const Row& row : rows)
    {
        SCOPED_TRACE(row.name);
        cfg.apply_setting("effects", "floor_glide", "on");
        walker* control = setup_glide_world(vs, 2, 0);
        ASSERT_NE(nullptr, control);
        put_tile_under(world, control, 0,
                       static_cast<unsigned char>(PIX_ZSTAIR_UP));
        if (row.rung == Rung::WorldTick)
            world.tick_count_ = 100; // baselined during settle
        settle_glide_baseline(vs);

        Sint32 want_floor = 1;
        switch (row.rung)
        {
        case Rung::None:
            control->set_floor(1);
            break;
        case Rung::CfgOff:
            cfg.apply_setting("effects", "floor_glide", "off");
            control->set_floor(1);
            break;
        case Rung::EditorOverride:
            vs->editor_floor_override_ = 0;
            control->set_floor(1);
            want_floor = 0; // the override IS the floor
            break;
        case Rung::Authoring:
            vs->editor_authoring_view_ = true;
            control->set_floor(1);
            break;
        case Rung::ControlNull:
            control->set_floor(1);
            vs->control = nullptr;
            want_floor = 0; // no control walker => floor 0
            break;
        case Rung::ControlSwap:
        {
            walker* other = world.add_ob(Order::Living, FAMILY_SOLDIER);
            ASSERT_NE(nullptr, other);
            other->set_floor(1);
            other->setxy(160, 120);
            vs->control = other; // possession/handoff: id mismatch
            break;
        }
        case Rung::WorldKey:
            vs->glide_world_key_ = nullptr; // level-load identity change
            control->set_floor(1);
            break;
        case Rung::WorldTick:
            world.tick_count_ = 50; // ran backwards: restart/reset
            control->set_floor(1);
            break;
        case Rung::Stale:
            effects_advance_frame(); // settle left diff 1; push it to 3
            effects_advance_frame();
            control->set_floor(1);
            break;
        case Rung::NoRecord:
            // Burn the tick's tracker run BEFORE the hop: the once-per-frame
            // guard then skips it on the trigger redraw => no record => S10.
            ASSERT_TRUE(do_redraw(vs));
            control->set_floor(1);
            break;
        }

        trace_clear();
        ASSERT_TRUE(do_redraw(vs));
        if (row.rung == Rung::None)
        {
            EXPECT_EQ(15, vs->floor_glide_frames_left());
            EXPECT_EQ(1, vs->floor_glide_cause());
            EXPECT_TRUE(trace_contains("effects", "floor_glide start"));
        }
        else
        {
            EXPECT_EQ(0, vs->floor_glide_frames_left());
            EXPECT_EQ(0, vs->floor_glide_cause());
            EXPECT_FALSE(trace_contains("effects", "floor_glide start"));
        }
        EXPECT_EQ(want_floor, vs->current_floor_);

        // Undo the rung's world/view mutations.
        vs->editor_floor_override_ = -1;
        vs->editor_authoring_view_ = false;
        world.tick_count_ = 0;
        effects_reset_for_testing();
        restore_world(vs);
    }
}

// 10.2-6: newest-event-wins retarget. A fall triggered mid-stair-glide
// switches the cause and duration and continues from the live fractional
// camera height — never a snap-back to an integer.
TEST_F(RenderEffects, floor_glide_retarget_switches_cause_and_stays_continuous)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "floor_glide", "on");

    GameWorld& world = scr()->world();
    walker* control = setup_glide_world(vs, 2, 0);
    ASSERT_NE(nullptr, control);
    put_tile_under(world, control, 0,
                   static_cast<unsigned char>(PIX_ZSTAIR_UP));

    settle_glide_baseline(vs);
    control->set_floor(1);
    ASSERT_TRUE(do_redraw(vs)); // stair-up trigger, i = 1 of 16
    ASSERT_EQ(15, vs->floor_glide_frames_left());
    ASSERT_EQ(1, vs->floor_glide_cause());

    // Advance to mid-glide, checking the per-frame step stays bounded.
    float prev_z = vs->floor_glide_camera_z();
    for (int f = 0; f < 4; f++)
    {
        ASSERT_TRUE(do_redraw(vs));
        const float z = vs->floor_glide_camera_z();
        ASSERT_LE(std::fabs(z - prev_z), 0.35f)
            << "stair easing must move in bounded per-frame steps";
        prev_z = z;
    }
    ASSERT_EQ(11, vs->floor_glide_frames_left());
    const float z_before = vs->floor_glide_camera_z();
    ASSERT_GT(z_before, 0.0f);
    ASSERT_LT(z_before, 1.0f);

    // Retarget: an in-place fall back to floor 0 (grass departure cell on
    // floor 1, zero XY nudge). Newest event wins uniformly.
    effects_advance_frame();
    control->set_floor(0);
    ASSERT_TRUE(do_redraw(vs));
    EXPECT_EQ(2, vs->floor_glide_cause()) << "cause switches to Fall";
    EXPECT_EQ(8, vs->floor_glide_frames_left())
        << "duration resets to the fall N (9) at trigger";
    EXPECT_EQ(0, vs->current_floor_);
    const float z_after = vs->floor_glide_camera_z();
    EXPECT_LE(std::fabs(z_after - z_before), 0.35f)
        << "retarget continues from the live fractional height";
    EXPECT_GT(z_after, 0.0f) << "never snaps back to an integer";
    EXPECT_LT(z_after, 1.0f);

    effects_reset_for_testing();
    restore_world(vs);
}

// 10.2-7: ghost-hold invariant. While the look-up hold is active, every
// drawn above-camera floor keeps alpha >= kFloorGhostAlpha through the whole
// glide, and the final frame equals the settled steady hold frame.
TEST_F(RenderEffects, floor_glide_ghost_hold_keeps_above_floor_alpha)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "floor_glide", "on");

    GameWorld& world = scr()->world();
    walker* control = setup_glide_world(vs, 2, 0);
    ASSERT_NE(nullptr, control);
    put_tile_under(world, control, 0,
                   static_cast<unsigned char>(PIX_ZSTAIR_UP));

    KeyBindingGuard bind(0, KEY_LOOKUP, KEYCODE_v);
    SessionKeyStateGuard keystates;
    keystates.set(SDLK_V, true);

    settle_glide_baseline(vs);
    control->set_floor(1);
    ASSERT_TRUE(do_redraw(vs)); // stair-up trigger under the hold
    ASSERT_EQ(15, vs->floor_glide_frames_left());
    ASSERT_TRUE(vs->ghost_hold_override_);

    // Every glide frame: the destination floor is above the fractional
    // camera (dz > 0) until the sweep completes; base-anchored curves keep
    // its drawn alpha at or above the steady ghost value.
    while (vs->floor_glide_frames_left() > 0)
    {
        const viewscreen::FloorPassParams fp =
            vs->compute_floor_pass(1, world, true);
        if (!fp.skip)
        {
            EXPECT_GE(static_cast<int>(fp.falpha),
                      static_cast<int>(viewscreen::kFloorGhostAlpha))
                << "hold-active above-floor alpha fell below the ghost floor";
        }
        EXPECT_TRUE(fp.entities)
            << "the hold keeps the full entity pass, exactly as steady holds";
        ASSERT_TRUE(do_redraw(vs));
    }
    const std::vector<RGB> end_frame = grab_viewport(vs);

    // The settled steady hold frame, snap-constructed with cfg OFF at the
    // same frame_tick, must match byte for byte.
    cfg.apply_setting("effects", "floor_glide", "off");
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(rects_equal(end_frame, grab_viewport(vs)))
        << "glide end under the hold must equal the steady hold frame";

    effects_reset_for_testing();
    restore_world(vs);
}

// 10.2-8a: the departing floor renders terrain-only during a no-hold
// down-glide — a monster on it contributes ZERO pixels from the trigger
// frame on (the frame-1 entity vanish, deliberate and fx-review-gated).
TEST_F(RenderEffects, floor_glide_departing_pass_is_terrain_only)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "floor_glide", "on");

    GameWorld& world = scr()->world();

    // Two runs of the same stairs-down descent (floor 1 -> 0), with and
    // without a monster on the departure floor; grab the steady frame plus
    // the trigger + 4 mid-glide frames.
    Sint32 spritew = 0, spriteh = 0;
    const auto run = [&](bool with_monster, std::vector<RGB>* steady,
                         std::vector<std::vector<RGB>>* glide_frames) {
        walker* control = setup_glide_world(vs, 2, 1);
        ASSERT_NE(nullptr, control);
        put_tile_under(world, control, 1,
                       static_cast<unsigned char>(PIX_ZSTAIR_DOWN));
        if (with_monster)
        {
            walker* monster = world.add_ob(Order::Living, FAMILY_SOLDIER);
            ASSERT_NE(nullptr, monster);
            monster->set_floor(1);
            monster->setxy(200, 120);
            spritew = monster->sizex();
            spriteh = monster->sizey();
        }
        settle_glide_baseline(vs);
        ASSERT_TRUE(do_redraw(vs));
        *steady = grab_viewport(vs);
        effects_advance_frame(); // the extra steady redraw burned tick 2
        control->set_floor(0);   // the in-place stair descent
        trace_clear();
        glide_frames->clear();
        for (int f = 0; f < 5; f++)
        {
            ASSERT_TRUE(do_redraw(vs));
            ASSERT_GT(vs->floor_glide_frames_left(), 0)
                << "frame " << f << " of 5 must be mid-glide (N = 16)";
            glide_frames->push_back(grab_viewport(vs));
        }
        ASSERT_TRUE(trace_contains("effects", "floor_glide start cause=1"));
        effects_reset_for_testing();
        restore_world(vs);
    };

    std::vector<RGB> steady_with, steady_without;
    std::vector<std::vector<RGB>> glide_with, glide_without;
    run(true, &steady_with, &glide_with);
    run(false, &steady_without, &glide_without);

    ASSERT_FALSE(rects_equal(steady_with, steady_without))
        << "the probe is meaningless unless the monster shows up steady";
    ASSERT_EQ(glide_with.size(), glide_without.size());

    // Mid-glide the monster may contribute exactly ONE thing: its blob
    // shadow (the deliberate <=16-frame double representation — the shadow
    // pass keys off the already-snapped integer floor and draws post-loop at
    // unscaled screen coords). Its SPRITE must contribute nothing: every
    // with/without difference must sit inside the squashed shadow footprint
    // at the monster's feet, never in the body rows above it.
    viewscreen* vsp = view0();
    ASSERT_GT(spritew, 0);
    ASSERT_GT(spriteh, 0);
    const int mx = world_to_screen_x(vsp, 200);
    const int my = world_to_screen_y(vsp, 120);
    const int feet = my + 2 + static_cast<int>(spriteh); // 1-story SE nudge
    const int shadow_top = feet - (static_cast<int>(spriteh) + 2) / 3 - 2;
    const int sx0 = mx;
    const int sx1 = mx + 2 + static_cast<int>(spritew) + 2;
    for (size_t f = 0; f < glide_with.size(); f++)
    {
        size_t changed = 0;
        for (size_t i = 0; i < glide_with[f].size(); i++)
        {
            if (same(glide_with[f][i], glide_without[f][i]))
                continue;
            changed++;
            const int x = static_cast<int>(vsp->xloc) +
                static_cast<int>(i % static_cast<size_t>(vsp->xview));
            const int y = static_cast<int>(vsp->yloc) +
                static_cast<int>(i / static_cast<size_t>(vsp->xview));
            ASSERT_GE(y, shadow_top)
                << "sprite pixel above the shadow band leaked into glide "
                << "frame " << f << " at (" << x << "," << y
                << ") — the departing pass must be terrain-only";
            ASSERT_LE(y, feet + 1) << "stray pixel below the feet, frame " << f;
            ASSERT_GE(x, sx0) << "stray pixel left of the footprint, frame " << f;
            ASSERT_LE(x, sx1) << "stray pixel right of the footprint, frame " << f;
        }
        ASSERT_GT(changed, 0u)
            << "the blob shadow (deliberate double representation) must "
            << "still anchor the monster, frame " << f;
    }
}

// 10.2-8b: rng-invariance companion, pinned on the invariant the design
// actually guarantees: the glide's EXTRA passes (the terrain-only departing
// floor) add ZERO rng draws. An INVISIBLE-mode entity consumes one ctx rng
// draw per opaque pixel when drawn on the direct camera-floor path, so we
// park one on the DEPARTING floor: cfg-off frames never draw that floor
// after the snap, and glide frames render it terrain-only — both must
// consume exactly zero rng.
//
// (Deliberately narrower than a strict ON-vs-OFF cursor-equality pin: an
// entity on the DESTINATION floor mid-glide takes the pre-existing
// faded-floor layer path — plain sprite, no INVISIBLE_MODE, no rng — so
// cursor equality against the cfg-off camera-floor frame does NOT hold
// there. That mode switch is the shipped below-floor grammar, not a new
// entity pass; recorded as an accepted exposure in
// docs/floor-glide-design.md §2.4 / R2.)
TEST_F(RenderEffects, floor_glide_adds_zero_render_rng_draws)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();

    struct CountingRandom : public IRandom
    {
        std::uint64_t calls = 0;
        std::uint32_t next(std::uint32_t) override
        {
            ++calls;
            return 0; // deterministic: every invisible pixel draws
        }
    };
    // RAII: a fatal ASSERT anywhere below must not leave the global rng
    // dangling at a dead stack object and poison every later test.
    struct RngSwapGuard
    {
        IRandom* saved;
        explicit RngSwapGuard(IRandom* replacement) : saved(ctx().rng)
        {
            ctx().rng = replacement;
        }
        ~RngSwapGuard() { ctx().rng = saved; }
    };
    CountingRandom counter;
    RngSwapGuard rng_swap(&counter);

    GameWorld& world = scr()->world();

    // Sanity for the counter itself: an invisible ally on the CAMERA floor
    // takes the direct INVISIBLE_MODE path and consumes rng draws.
    {
        cfg.apply_setting("effects", "floor_glide", "on");
        walker* control = setup_glide_world(vs, 2, 0);
        ASSERT_NE(nullptr, control);
        walker* ghost = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, ghost);
        ghost->setxy(120, 120);
        ghost->set_invisibility_left(100);
        settle_glide_baseline(vs);
        const std::uint64_t before = counter.calls;
        ASSERT_TRUE(do_redraw(vs));
        ASSERT_GT(counter.calls, before)
            << "the probe entity must consume rng on the camera floor";
        effects_reset_for_testing();
        restore_world(vs);
    }

    const auto run = [&](const char* mode, std::vector<std::uint64_t>* out,
                         bool expect_active) {
        cfg.apply_setting("effects", "floor_glide", mode);
        walker* control = setup_glide_world(vs, 2, 1);
        ASSERT_NE(nullptr, control);
        put_tile_under(world, control, 1,
                       static_cast<unsigned char>(PIX_ZSTAIR_DOWN));
        walker* ghost = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, ghost);
        ghost->set_floor(1); // DEPARTING floor after the descent below
        ghost->setxy(120, 120);
        ghost->set_invisibility_left(100);
        settle_glide_baseline(vs);
        control->set_floor(0); // the in-place stair descent
        out->clear();
        for (int f = 0; f < 5; f++)
        {
            const std::uint64_t before = counter.calls;
            ASSERT_TRUE(do_redraw(vs));
            out->push_back(counter.calls - before);
            if (expect_active)
            {
                ASSERT_GT(vs->floor_glide_frames_left(), 0)
                    << "the ON run must be mid-glide at frame " << f;
            }
            else
            {
                ASSERT_EQ(0, vs->floor_glide_frames_left());
            }
        }
        effects_reset_for_testing();
        restore_world(vs);
    };

    std::vector<std::uint64_t> off_calls, on_calls;
    run("off", &off_calls, false);
    run("on", &on_calls, true);

    ASSERT_EQ(off_calls.size(), on_calls.size());
    for (size_t f = 0; f < off_calls.size(); f++)
    {
        EXPECT_EQ(0u, off_calls[f])
            << "cfg-off must not draw the vacated floor, frame " << f;
        EXPECT_EQ(0u, on_calls[f])
            << "the terrain-only departing pass added rng draws, frame " << f;
    }
}

// 10.2-9: multi-story fall = one continuous sweep. Delta 2 runs 12 frames,
// the camera height crosses the intermediate floor mid-sweep and overshoots
// past the destination (the landing squash) before settling.
TEST_F(RenderEffects, floor_glide_multistory_fall_sweeps_through_and_overshoots)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "floor_glide", "on");

    walker* control = setup_glide_world(vs, 3, 2);
    ASSERT_NE(nullptr, control);

    settle_glide_baseline(vs);
    control->set_floor(0); // two-story in-place air fall
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_EQ(11, vs->floor_glide_frames_left()) << "N = 12 for 2 stories";
    ASSERT_EQ(2, vs->floor_glide_cause());

    std::vector<float> zs;
    zs.push_back(vs->floor_glide_camera_z());
    int extra = 0;
    while (vs->floor_glide_frames_left() > 0 && extra < 32)
    {
        ASSERT_TRUE(do_redraw(vs));
        zs.push_back(vs->floor_glide_camera_z());
        extra++;
    }
    ASSERT_EQ(11, extra) << "total glide = 12 render frames";
    EXPECT_GT(zs.front(), 1.8f) << "the sweep starts just below floor 2";
    bool crossed_intermediate = false;
    float min_z = 99.0f;
    for (size_t k = 0; k + 1 < zs.size(); k++)
        if (zs[k] >= 1.0f && zs[k + 1] <= 1.0f)
            crossed_intermediate = true;
    for (const float z : zs)
        min_z = std::min(min_z, z);
    EXPECT_TRUE(crossed_intermediate)
        << "one continuous sweep must pass the intermediate floor's height";
    EXPECT_LE(min_z, -0.2f)
        << "the fall must overshoot 0.25 floors past the destination";
    EXPECT_EQ(0.0f, vs->floor_glide_camera_z())
        << "inactive accessor reports the integer floor";

    effects_reset_for_testing();
    restore_world(vs);
}

// 10.2-10: camera and render-sample pins. Every glide frame restores
// topx/topy to the unshifted camera (the per-floor parallax shift never
// leaks out of the floor loop) and publishes exactly one primary render
// sample per redraw.
TEST_F(RenderEffects, floor_glide_restores_camera_and_publishes_one_sample)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    EffectsCfgGuard guard;
    all_effects_off();
    cfg.apply_setting("effects", "floor_glide", "on");

    GameWorld& world = scr()->world();
    walker* control = setup_glide_world(vs, 2, 0);
    ASSERT_NE(nullptr, control);
    put_tile_under(world, control, 0,
                   static_cast<unsigned char>(PIX_ZSTAIR_UP));

    GameplayActiveGuard active;
    og::runtime::reset_runtime_trace_capture_state();

    settle_glide_baseline(vs);
    const Sint32 steady_topx = vs->topx;
    const Sint32 steady_topy = vs->topy;
    ASSERT_TRUE(do_redraw(vs)); // sample-seq baseline (burns tick 2's tracker)
    const auto base = og::runtime::latest_runtime_render_sample();
    ASSERT_TRUE(base.has_value());
    std::uint64_t prev_seq = base->render_sample_seq;

    effects_advance_frame(); // fresh tick so the hop classifies
    control->set_floor(1);
    for (int f = 0; f < 6; f++)
    {
        ASSERT_TRUE(do_redraw(vs));
        if (f == 0)
        {
            ASSERT_EQ(15, vs->floor_glide_frames_left());
        }
        EXPECT_EQ(steady_topx, vs->topx)
            << "glide frame " << f << " leaked a shifted topx";
        EXPECT_EQ(steady_topy, vs->topy)
            << "glide frame " << f << " leaked a shifted topy";
        const auto sample = og::runtime::latest_runtime_render_sample();
        ASSERT_TRUE(sample.has_value());
        EXPECT_EQ(prev_seq + 1u, sample->render_sample_seq)
            << "exactly one render sample per redraw, frame " << f;
        EXPECT_EQ(steady_topx, sample->camera_topx);
        EXPECT_EQ(steady_topy, sample->camera_topy);
        prev_seq = sample->render_sample_seq;
    }

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

TEST_F(RenderEffects, zz_capture_effect_scenes)
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
                    world.grid.data[static_cast<std::size_t>(i + world.grid.w * j)] =
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
                    world.grid.data[static_cast<std::size_t>(i + world.grid.w * j)] =
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
                    world.grid.data[static_cast<std::size_t>(i + world.grid.w * j)] =
                        static_cast<unsigned char>(PIX_WATER1);
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->setxy(static_cast<short>(70), static_cast<short>(140));
            extra = world.add_ob(Order::Living, FAMILY_SOLDIER);
            extra->setxy(static_cast<short>(200), static_cast<short>(120));
        },
        [&](int f) { mover->setxy(static_cast<short>(70 + f), static_cast<short>(140)); });

    // Marsh (bugs #13/#14): static pale glints under live palette cycling
    // (nothing may blink) and rings ONLY under the wading mover — the
    // standing unit's bog stays quiet.
    run_scene("marsh_ripples", 36,
        [&] {
            cfg.apply_setting("effects", "ripples", "on");
            fill_camera_grid(static_cast<unsigned char>(PIX_MARSH1));
            for (int j = 0; j < static_cast<int>(world.grid.h); j++)
                for (int i = 0; i < static_cast<int>(world.grid.w); i++)
                    if ((i + j) % 2)
                        world.grid.data[static_cast<std::size_t>(i + world.grid.w * j)] =
                            static_cast<unsigned char>(PIX_MARSH2);
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->setxy(static_cast<short>(70), static_cast<short>(140));
            extra = world.add_ob(Order::Living, FAMILY_SOLDIER);
            extra->setxy(static_cast<short>(200), static_cast<short>(120));
        },
        [&](int f) { mover->setxy(static_cast<short>(70 + f * 2), static_cast<short>(140)); });

    run_scene("clouds", 144,
        [&] {
            cfg.apply_setting("effects", "weather", "on");
            for (int j = 9; j < 14; j++)
                for (int i = 12; i < 21; i++)
                    world.grid.data[static_cast<std::size_t>(i + world.grid.w * j)] =
                        static_cast<unsigned char>(PIX_WATER1);
            world.set_weather(WeatherKind::Clouds);
        },
        [&](int) {});

    run_scene("rain", 128,
        [&] {
            cfg.apply_setting("effects", "weather", "on");
            for (int j = 9; j < 14; j++)
                for (int i = 12; i < 21; i++)
                    world.grid.data[static_cast<std::size_t>(i + world.grid.w * j)] =
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

    // Falling cue: a walker strides along a solid upper-floor platform (its
    // overhang shadow + dust read overhead), walks off its edge onto air at
    // frame 16 (the sim's instant floor drop, replicated here), and the
    // render-only cue smears its descent + puffs the landing. A second lap
    // repeats the fall so the loop reads on the review site.
    run_scene("fall_cue", 64,
        [&] {
            cfg.apply_setting("effects", "dust", "on");
            world.set_floor_count(2);
            fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_AIR));
            PixieData& platform = world.grid_for_floor(1);
            for (int j = 5; j < 8; j++)
                for (int i = 5; i < 12; i++)
                    platform.data[static_cast<std::size_t>(i + platform.w * j)] =
                        static_cast<unsigned char>(PIX_GRASS1);
            mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
            mover->set_floor(1);
            mover->setxy(static_cast<short>(88), static_cast<short>(100));
        },
        [&](int f) {
            const int lap = f % 32;
            if (lap == 0)
            {
                mover->set_floor(1);
                mover->setxy(static_cast<short>(88), static_cast<short>(100));
            }
            else if (lap < 16)
                mover->setxy(static_cast<short>(88 + lap * 6),
                             static_cast<short>(100));
            else if (lap == 16)
                mover->set_floor(0); // walked off the platform edge: the fall
        });

    // Floor-glide close-ups (docs/floor-glide-design.md §10.3), split so each
    // review question gets its own loop: the stairs read (with the departing-
    // floor entity vanish to judge) and the fall read (with the squash
    // strength to judge at pane sizes).
    {
        KeyBindingGuard bind(0, KEY_LOOKUP, KEYCODE_v);
        SessionKeyStateGuard keystates;
        run_scene("floor_glide_stairs", 96,
            [&] {
                cfg.apply_setting("effects", "floor_glide", "on");
                world.set_floor_count(3);
                fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_AIR));
                fill_floor_grid(world, 2, static_cast<unsigned char>(PIX_AIR));
                PixieData& mid = world.grid_for_floor(1);
                for (int j = 4; j < 11; j++)
                    for (int i = 4; i < 16; i++)
                        mid.data[static_cast<std::size_t>(i + mid.w * j)] =
                            static_cast<unsigned char>(PIX_GRASS1);
                // Stair pair at the hero's destination cell: UP on floor 0,
                // DOWN directly above it on floor 1.
                vs->control->setxy(static_cast<short>(168),
                                   static_cast<short>(120));
                put_tile_under(world, vs->control, 0,
                               static_cast<unsigned char>(PIX_ZSTAIR_UP));
                put_tile_under(world, vs->control, 1,
                               static_cast<unsigned char>(PIX_ZSTAIR_DOWN));
                vs->control->setxy(static_cast<short>(120),
                                   static_cast<short>(120));
                // Pacing monsters on floor 1 — the departure floor of the
                // down-stair leg: they vanish at glide frame 1 while their
                // terrain fades (the fx-review question).
                mover = world.add_ob(Order::Living, FAMILY_SOLDIER);
                mover->set_floor(1);
                mover->setxy(static_cast<short>(200), static_cast<short>(130));
                extra = world.add_ob(Order::Living, FAMILY_ELF);
                extra->set_floor(1);
                extra->setxy(static_cast<short>(130), static_cast<short>(90));
            },
            [&](int f) {
                const int pace = (f % 32) < 16 ? (f % 16) : 16 - (f % 16);
                mover->setxy(static_cast<short>(200 + pace * 2),
                             static_cast<short>(130));
                extra->setxy(static_cast<short>(130 + pace * 2),
                             static_cast<short>(90));
                if (f < 8) // approach the stair
                    vs->control->setxy(static_cast<short>(120 + f * 6),
                                       static_cast<short>(120));
                else if (f == 8)
                    vs->control->setxy(static_cast<short>(168),
                                       static_cast<short>(120));
                else if (f == 10)
                    vs->control->set_floor(1); // the up-glide (16 frames)
                // A short look-up-hold blip mid-up-glide: the above-floor
                // alpha re-bases to the ghost curve instantly, no cancel.
                keystates.set(SDLK_V, f >= 13 && f < 21);
                if (f == 55)
                    vs->control->set_floor(0); // the down-glide: entity vanish
            });
    }

    run_scene("floor_glide_fall", 56,
        [&] {
            cfg.apply_setting("effects", "floor_glide", "on");
            cfg.apply_setting("effects", "dust", "on"); // landing smear rides dust
            world.set_floor_count(3);
            fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_AIR));
            fill_floor_grid(world, 2, static_cast<unsigned char>(PIX_AIR));
            // Offset platforms: the top platform's east edge hangs over air
            // on BOTH floors below, so the two-story drop reads clean.
            PixieData& mid = world.grid_for_floor(1);
            for (int j = 4; j < 9; j++)
                for (int i = 3; i < 9; i++)
                    mid.data[static_cast<std::size_t>(i + mid.w * j)] =
                        static_cast<unsigned char>(PIX_GRASS1);
            PixieData& top = world.grid_for_floor(2);
            for (int j = 5; j < 8; j++)
                for (int i = 4; i < 9; i++)
                    top.data[static_cast<std::size_t>(i + top.w * j)] =
                        static_cast<unsigned char>(PIX_GRASS1);
            vs->control->set_floor(2);
            vs->control->setxy(static_cast<short>(80), static_cast<short>(96));
        },
        [&](int f) {
            const int lap = f % 28;
            if (lap == 0)
            {
                // Reset for the second lap: a stairless two-story rise is a
                // Teleport — it snaps by design (discontinuity is the message).
                vs->control->set_floor(2);
                vs->control->setxy(static_cast<short>(80),
                                   static_cast<short>(96));
            }
            else if (lap < 10)
                vs->control->setxy(static_cast<short>(80 + lap * 7),
                                   static_cast<short>(96));
            else if (lap == 10)
                vs->control->set_floor(0); // off the edge: two stories down
        });

    // The depth close-ups read through the fade-below floor layers, which
    // render unconditionally (no look-up hold): this is the normal-play view
    // straight down through the air holes. One scene per depth_fx mode, all
    // from the same 3-floor vantage; fog (the default) is the hero and runs
    // longer so its drift reads on the review site.
    const auto depth_scene = [&](const char* scene, const char* mode,
                                 int frames) {
        run_scene(scene, frames,
            [&] {
                cfg.apply_setting("effects", "depth_fx", mode);
                cfg.apply_setting("effects", "shadows", "on");
                world.set_floor_count(3);
                for (int fl = 1; fl < 3; fl++)
                {
                    fill_floor_grid(world, fl,
                                    static_cast<unsigned char>(PIX_GRASS1));
                    PixieData& g = world.grid_for_floor(fl);
                    for (int j = 4; j < 10; j++)
                        for (int i = 5; i < 13; i++)
                            g.data[static_cast<std::size_t>(i + g.w * j)] =
                                static_cast<unsigned char>(PIX_AIR);
                }
                vs->control->set_floor(2);
                // Characters on the exposed lower floors: one pacing one
                // floor down, one two floors down — both must read
                // progressively deeper.
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
    };
    depth_scene("depth_fog", "fog", 60);
    depth_scene("depth_haze", "haze", 30);
    depth_scene("depth_mist", "mist", 30);
    depth_scene("depth_tint", "tint", 30);

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
                    world.grid.data[static_cast<std::size_t>(i + world.grid.w * j)] =
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

TEST_F(RenderEffects, capture_dump_hook_writes_ppm_frames)
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

// Floor-presentation v2 gate regressions: capture/spectator cameras (which
// drive the floor via editor_floor_override_) MUST render the overhang
// shadows — only the level editor's authoring view suppresses them.
TEST_F(RenderEffects, overhang_shadows_render_under_spectator_floor_override)
{
    viewscreen* vs = view0();
    ASSERT_NE(nullptr, vs);
    GameWorld& world = scr()->world();
    EffectsCfgGuard guard;
    prepare_world();
    all_effects_off();
    fill_camera_grid(static_cast<unsigned char>(PIX_GRASS1));
    world.set_floor_count(2);
    fill_floor_grid(world, 1, static_cast<unsigned char>(PIX_AIR));
    PixieData& up = world.grid_for_floor(1);
    for (int j = 4; j < 8; j++)
        for (int i = 4; i < 10; i++)
            up.data[static_cast<std::size_t>(i + up.w * j)] = static_cast<unsigned char>(PIX_GRASS1);
    walker* control = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, control);
    control->setxy(static_cast<short>(160), static_cast<short>(120));
    vs->control = nullptr;
    vs->editor_floor_override_ = 0; // spectator/capture camera on floor 0
    vs->editor_authoring_view_ = false;
    effects_reset_for_testing();
    ASSERT_TRUE(do_redraw(vs));
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_TRUE(trace_contains("render", "overhang_shadow"))
        << "spectator floor-override draws must include the shadow pass";

    // The editor's authoring view suppresses it.
    vs->editor_authoring_view_ = true;
    trace_clear();
    ASSERT_TRUE(do_redraw(vs));
    ASSERT_FALSE(trace_contains("render", "overhang_shadow"))
        << "the editor authoring view must stay clean";

    vs->editor_floor_override_ = -1;
    vs->editor_authoring_view_ = false;
    world.delete_objects();
    world.set_floor_count(1);
    effects_reset_for_testing();
    restore_world(vs);
}

// Render-cost benchmark for large world canvases (the "fullscreen at small
// sprite scale is 1fps" report). Env-gated: set OG_BENCH=1; prints ms/frame
// per effect configuration to stderr so a profiler run can be focused.
TEST_F(RenderEffects, zz_bench_large_canvas_redraw)
{
    if (getenv("OG_BENCH") == nullptr)
        GTEST_SKIP() << "set OG_BENCH=1 to run the large-canvas benchmark";

    scr()->set_world_canvas_pinned_classic(false);
    EffectsCfgGuard cfg_guard;
    // A REAL level (objects, terrain variety) rather than the empty fixture
    // grid: weather/depth effects gate on outdoor terrain, and tile/sprite
    // draw costs only show against real content.
    scr()->save_data.scen_num = 1;
    ASSERT_TRUE(scr()->load_level());
    scr()->world().mysmoother.set_target(scr()->world().grid);
    viewscreen* vs = view0();

    E_Screen->set_world_canvas_size(1920, 1200);
    scr()->set_active_canvas(CanvasTarget::World);
    scr()->relayout_views();
    ASSERT_TRUE(do_redraw(vs)); // settle camera on the new canvas

    struct Config { const char* name; WeatherKind weather; const char* depth; };
    const Config configs[] = {
        {"bare",          WeatherKind::None,   "off"},
        {"depth_fog",     WeatherKind::None,   "fog"},
        {"clouds",        WeatherKind::Clouds, "off"},
        {"rain",          WeatherKind::Rain,   "off"},
        {"snow",          WeatherKind::Snow,   "off"},
        {"clouds_fog",    WeatherKind::Clouds, "fog"},
    };
    for (const Config& c : configs)
    {
        scr()->world().set_weather(c.weather);
        cfg.apply_setting("effects", "depth_fx", c.depth);
        ASSERT_TRUE(do_redraw(vs)); // warm
        const Uint64 t0 = SDL_GetTicks();
        constexpr int kFrames = 40;
        for (int i = 0; i < kFrames; ++i)
        {
            ASSERT_TRUE(do_redraw(vs));
            scr()->buffer_to_screen(0, 0, scr()->canvas_w(), scr()->canvas_h());
        }
        const Uint64 elapsed = SDL_GetTicks() - t0;
        fprintf(stderr, "[bench 1920x1200] %-12s %6.2f ms/frame\n", c.name,
                static_cast<double>(elapsed) / kFrames);
    }

    // Deepest zoom exercises the actual 3200x2000 raw upload that replaced
    // the old fullscreen-relative small-sprite path.
    scr()->world().set_weather(WeatherKind::None);
    cfg.apply_setting("effects", "depth_fx", "off");
	E_Screen->set_world_zoom(1, og::WorldScaleMode::Integer);
	scr()->relayout_views();
	ASSERT_TRUE(do_redraw(vs));
	{
		const Uint64 t0 = SDL_GetTicks();
		constexpr int kFrames = 10;
		for (int i = 0; i < kFrames; ++i)
		{
			ASSERT_TRUE(do_redraw(vs));
			scr()->buffer_to_screen(0, 0, scr()->canvas_w(), scr()->canvas_h());
		}
		const Uint64 elapsed = SDL_GetTicks() - t0;
		fprintf(stderr, "[bench 3200x2000] zoom_0.1_raw %6.2f ms/frame\n",
		        static_cast<double>(elapsed) / kFrames);
	}

	// Measure the live world-only smart-filter path at zoom 0.5, whose
	// 1280x800 doubled scratch is within the production pixel budget.
	struct SmartConfig { const char* name; og::WorldScaleMode mode; };
	for (const SmartConfig& smart : {
	         SmartConfig{"world_sai", og::WorldScaleMode::Sai},
	         SmartConfig{"world_eagle", og::WorldScaleMode::Eagle}})
    {
		E_Screen->set_world_zoom(5, smart.mode);
		scr()->relayout_views();
		E_Screen->begin_gameplay_frame();
		ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
        ASSERT_TRUE(do_redraw(vs));
        scr()->buffer_to_screen(0, 0, scr()->canvas_w(), scr()->canvas_h()); // warm render2
		ASSERT_TRUE(E_Screen->last_world_present_used_smart_surface());
        const Uint64 t0 = SDL_GetTicks();
        constexpr int kFrames = 10;
        for (int i = 0; i < kFrames; ++i)
        {
			E_Screen->begin_gameplay_frame();
			ASSERT_TRUE(E_Screen->gameplay_ui_overlay_active());
            ASSERT_TRUE(do_redraw(vs));
            scr()->buffer_to_screen(0, 0, scr()->canvas_w(), scr()->canvas_h());
        }
        const Uint64 elapsed = SDL_GetTicks() - t0;
		fprintf(stderr, "[bench 640x400] %-12s %6.2f ms/frame\n", smart.name,
                static_cast<double>(elapsed) / kFrames);
    }

	E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer);
    scr()->set_active_canvas(CanvasTarget::UI);
    scr()->relayout_views();
    restore_world(vs);
}

// ---------------------------------------------------------------------------
// The two global settings that moved out of the retired in-game options menu
// and into GRAPHICS FX / DISPLAY (docs/pause-menu-design.md §7.3).
//
// COLOR CYCLING inverts this suite's usual convention. Every other effects/*
// key is "off => no extra work => byte-identical"; palette cycling has been
// ON since the screen constructor, so ON is the identity state and OFF is
// the change. The pins below are written that way round on purpose.

namespace
{
// An independent oracle for adjust_palette's transform: each component is
// scaled by (100 + 10 * steps)% and offset by `steps`, then clamped to the
// 6-bit VGA range.
unsigned char gamma_component(unsigned char value, int steps)
{
    int scaled = (static_cast<int>(value) * (100 + steps * 10)) / 100 + steps;
    return static_cast<unsigned char>(std::clamp(scaled, 0, 63));
}

std::array<unsigned char, 768> gamma_expected(
    const std::array<unsigned char, 768>& source, int steps)
{
    std::array<unsigned char, 768> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = gamma_component(source[i], steps);
    return out;
}

// The SetPalette sim event the server emits on every keyframe (and on the
// cheat palette flip): screen.cpp turns it into a raw set_palette, which is
// exactly what used to wipe the player's brightness.
void dispatch_set_palette_event(int palette_id)
{
    og::sim::SimEventBatch batch;
    og::sim::Event event;
    event.kind = og::sim::EventKind::SetPalette;
    event.a = static_cast<std::uint32_t>(palette_id);
    event.b = 0u;
    batch.events.push_back(std::move(event));
    scr()->dispatch_cosmetic_events(batch);
}

class BrightnessGuard
{
public:
    BrightnessGuard()
        : saved_cfg_(cfg.get_setting("graphics", "brightness"))
        , saved_steps_(display_brightness_steps())
    {}
    ~BrightnessGuard()
    {
        cfg.apply_setting("graphics", "brightness",
                          saved_cfg_.empty() ? "0" : saved_cfg_);
        set_display_brightness_steps(saved_steps_);
        set_palette(scr()->ourpalette);
    }
    BrightnessGuard(const BrightnessGuard&) = delete;
    BrightnessGuard& operator=(const BrightnessGuard&) = delete;

private:
    std::string saved_cfg_;
    Sint32 saved_steps_;
};
} // namespace

// BRIGHTNESS: the applied gamma now lives in session state and set_palette
// re-applies it, so the palette swaps that used to silently reset it (the
// keyframe SetPalette event, the cheat palette, level start) no longer can.
TEST_F(RenderEffects, brightness_survives_every_palette_set)
{
    BrightnessGuard guard;
    const std::array<unsigned char, 768> base = scr()->ourpalette;
    const std::array<unsigned char, 768> blue = scr()->bluepalette;

    set_display_brightness_steps(2);
    set_palette(scr()->ourpalette);
    const std::array<unsigned char, 768> lit = gamma_expected(base, 2);
    EXPECT_TRUE(std::equal(lit.begin(), lit.end(),
                           og::runtime::current_session->curpal_.begin()))
        << "set_palette must hand the display the gamma-corrected palette";
    EXPECT_FALSE(std::equal(base.begin(), base.end(),
                            og::runtime::current_session->curpal_.begin()))
        << "a +2 brightness must actually change the displayed colors";

    // The regression: a keyframe palette sync re-sets the base palette.
    dispatch_set_palette_event(0);
    EXPECT_TRUE(std::equal(lit.begin(), lit.end(),
                           og::runtime::current_session->curpal_.begin()))
        << "the keyframe SetPalette event must not wipe brightness";

    // The cheat/blue palette takes the same correction.
    dispatch_set_palette_event(1);
    const std::array<unsigned char, 768> lit_blue = gamma_expected(blue, 2);
    EXPECT_TRUE(std::equal(lit_blue.begin(), lit_blue.end(),
                           og::runtime::current_session->curpal_.begin()))
        << "the blue palette must be corrected too, not reset to raw";

    // Darkening is the same path with the opposite sign.
    set_display_brightness_steps(-3);
    dispatch_set_palette_event(0);
    const std::array<unsigned char, 768> dark = gamma_expected(base, -3);
    EXPECT_TRUE(std::equal(dark.begin(), dark.end(),
                           og::runtime::current_session->curpal_.begin()));

    // Identity: at the shipped default the palette reaches the display
    // byte-for-byte, exactly as before this hook existed.
    set_display_brightness_steps(0);
    dispatch_set_palette_event(0);
    EXPECT_TRUE(std::equal(base.begin(), base.end(),
                           og::runtime::current_session->curpal_.begin()))
        << "brightness 0 must be byte-identical to the un-hooked path";
}

// The boot seed: the applied gamma comes from cfg, which is what the old
// prefs[PREF_GAMMA] never did (viewscreen::gamma was constructed at 0 and
// the stored pref was never read back).
TEST_F(RenderEffects, brightness_seeds_and_reloads_from_cfg)
{
    BrightnessGuard guard;

    cfg.apply_setting("graphics", "brightness", "-2");
    reload_display_brightness_from_cfg();
    EXPECT_EQ(-2, display_brightness_steps());
    set_palette(scr()->ourpalette);
    const std::array<unsigned char, 768> dark =
        gamma_expected(scr()->ourpalette, -2);
    EXPECT_TRUE(std::equal(dark.begin(), dark.end(),
                           og::runtime::current_session->curpal_.begin()));

    // Out-of-range and unparseable stored values never reach the palette.
    cfg.apply_setting("graphics", "brightness", "99");
    reload_display_brightness_from_cfg();
    EXPECT_EQ(og::ui::kBrightnessStepMax, display_brightness_steps());
    cfg.apply_setting("graphics", "brightness", "bright");
    reload_display_brightness_from_cfg();
    EXPECT_EQ(0, display_brightness_steps());
}

// COLOR CYCLING: ON is today's behavior. The palette bands rotate exactly as
// they always have; OFF is the new state, and it is the one that changes
// what the player sees.
TEST_F(RenderEffects, color_cycling_on_is_the_classic_rotation)
{
    EffectsCfgGuard guard;
    const short saved_mode = scr()->cyclemode;

    cfg.apply_setting("effects", "color_cycling", "on");
    scr()->cyclemode = cfg.is_on("effects", "color_cycling") ? 1 : 0;
    ASSERT_EQ(1, scr()->cyclemode) << "ON must drive the live cycle flag";

    set_palette(scr()->ourpalette);
    const std::array<unsigned char, 768> before =
        og::runtime::current_session->curpal_;

    // What the game loop does once per sim tick (game_loop.cpp: if
    // (s.cyclemode) s.do_cycle(...)).
    if (scr()->cyclemode)
        scr()->do_cycle(0, 1);
    const std::array<unsigned char, 768> after =
        og::runtime::current_session->curpal_;

    // One rotation step: each orange index takes the color of the index
    // below it, and the band's first index takes the old last one.
    for (int index = ORANGE_END; index > ORANGE_START; --index)
    {
        const std::size_t dst = static_cast<std::size_t>(index) * 3;
        const std::size_t src = static_cast<std::size_t>(index - 1) * 3;
        for (std::size_t c = 0; c < 3; ++c)
            EXPECT_EQ(before[src + c], after[dst + c])
                << "orange index " << index << " component " << c;
    }
    for (std::size_t c = 0; c < 3; ++c)
        EXPECT_EQ(before[static_cast<std::size_t>(ORANGE_END) * 3 + c],
                  after[static_cast<std::size_t>(ORANGE_START) * 3 + c]);
    // The water band rotates on the same tick.
    for (int index = WATER_END; index > WATER_START; --index)
    {
        const std::size_t dst = static_cast<std::size_t>(index) * 3;
        const std::size_t src = static_cast<std::size_t>(index - 1) * 3;
        for (std::size_t c = 0; c < 3; ++c)
            EXPECT_EQ(before[src + c], after[dst + c])
                << "water index " << index << " component " << c;
    }
    // Nothing outside the two cycling bands moves.
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        const std::size_t index = i / 3;
        if (index >= WATER_START && index <= ORANGE_END)
            continue;
        EXPECT_EQ(before[i], after[i]) << "palette byte " << i;
    }

    scr()->cyclemode = saved_mode;
    set_palette(scr()->ourpalette);
}

// OFF freezes the bands: the loop's guard skips do_cycle entirely, so lava
// and water stop moving and the palette stays exactly where it was.
TEST_F(RenderEffects, color_cycling_off_freezes_the_bands)
{
    EffectsCfgGuard guard;
    const short saved_mode = scr()->cyclemode;

    cfg.apply_setting("effects", "color_cycling", "off");
    scr()->cyclemode = cfg.is_on("effects", "color_cycling") ? 1 : 0;
    ASSERT_EQ(0, scr()->cyclemode);

    set_palette(scr()->ourpalette);
    const std::array<unsigned char, 768> before =
        og::runtime::current_session->curpal_;
    for (int tick = 0; tick < 4; ++tick)
        if (scr()->cyclemode)
            scr()->do_cycle(tick, 1);
    EXPECT_TRUE(std::equal(before.begin(), before.end(),
                           og::runtime::current_session->curpal_.begin()))
        << "with cycling off no palette index may move";

    scr()->cyclemode = saved_mode;
    set_palette(scr()->ourpalette);
}

// The live toggle: clicking the GRAPHICS FX row flips cfg AND the live
// cyclemode, so the change is visible without leaving the menu.
TEST_F(RenderEffects, color_cycling_toggle_flips_cfg_and_the_live_flag)
{
    EffectsCfgGuard guard;
    const short saved_mode = scr()->cyclemode;

    cfg.apply_setting("effects", "color_cycling", "on");
    scr()->cyclemode = 1;

    toggle_color_cycling();
    EXPECT_FALSE(cfg.is_on("effects", "color_cycling"));
    EXPECT_EQ(0, scr()->cyclemode) << "the click must reach the live flag";

    toggle_color_cycling();
    EXPECT_TRUE(cfg.is_on("effects", "color_cycling"));
    EXPECT_EQ(1, scr()->cyclemode);

    scr()->cyclemode = saved_mode;
}

// Uncapped render paths advance the effects frame tick on the wall clock (one
// advance per 14 ms, the classic 72 fps cadence) instead of once per call, so
// machine-rate presenting cannot speed up weather/ripple/trail animation. The
// gate is exercised through the injected-clock entry point; the default
// per-call behavior every other test relies on is pinned at the end.
TEST(EffectsCadence, wall_clock_gates_frame_advancement)
{
	effects_reset_for_testing();
	ASSERT_EQ(0u, effects_frame_tick());

	effects_set_wall_clock_cadence(true);
	// First call configures the pacer: deadline lands one interval out, so
	// no advance yet.
	effects_advance_frame_at(100000);
	ASSERT_EQ(0u, effects_frame_tick())
	    << "first gated call must only arm the 14 ms deadline";
	effects_advance_frame_at(100014);
	ASSERT_EQ(1u, effects_frame_tick()) << "deadline reached: one advance";
	effects_advance_frame_at(100020);
	ASSERT_EQ(1u, effects_frame_tick())
	    << "mid-interval call must not advance";
	effects_advance_frame_at(100028);
	ASSERT_EQ(2u, effects_frame_tick()) << "next deadline: one advance";
	// A machine-rate burst inside one interval still yields zero advances.
	for (std::uint32_t now = 100029; now < 100042; ++now)
		effects_advance_frame_at(now);
	ASSERT_EQ(2u, effects_frame_tick())
	    << "13 calls inside one interval must not advance the tick";

	effects_set_wall_clock_cadence(false);
	effects_advance_frame();
	ASSERT_EQ(3u, effects_frame_tick())
	    << "with the cadence off every call advances, as all capped paths "
	       "and tests rely on";

	effects_reset_for_testing();
}
