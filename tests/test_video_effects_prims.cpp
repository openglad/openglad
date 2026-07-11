// Direct tests for the multifloor-FX blit primitives:
// walkputbuffer_shadow (squashed black ground shadow), walkputbuffer_reflect
// (vertically flipped reflection masked to the reflective tiles — glass +
// pure water by default, caller-supplied LUT), and floor_layer_end's
// per-mode depth treatments (apply for one composite, reset after).
#include <openglad/interface/screen.h>
#include <openglad/interface/base.h>
#include <openglad/interface/render/effects.h>
#include <openglad/interface/render/depth_fx.h>
#include <gtest/gtest.h>

#include <array>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
// Full-screen clipping port.
constexpr Sint32 kPortX0 = 0, kPortY0 = 0, kPortX1 = 320, kPortY1 = 200;
// Palette index 7 is (56,56,56) -> RGB(224,224,224): bright enough that a
// black or dark blend is strictly darker in every channel.
constexpr unsigned char kBrightBG = 7;
// Palette index 8 is (1,1,1) -> RGB(4,4,4): a dark opaque sprite color.
constexpr unsigned char kDarkSprite = 8;

screen* scr()
{
    return og::runtime::current_session->myscreen_;
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

bool darker(const RGB& a, const RGB& bg)
{
    return a.r < bg.r && a.g < bg.g && a.b < bg.b;
}

void fill_bright(int x, int y, int w, int h)
{
    scr()->fastbox(x, y, w, h, kBrightBG, 1);
}
} // namespace

TEST(VideoEffectsPrims, shadow_feet_pixel_lands_just_below_feet)
{
    screen* s = scr();
    s->clearbuffer();
    const int x = 100, y = 100, w = 3, h = 4;
    fill_bright(x - 2, y - 2, w + 8, h + 8);
    RGB bg = px(x, y);

    // Single opaque pixel at the feet (bottom row, col 1).
    std::vector<unsigned char> src(static_cast<size_t>(w * h), 0);
    src[static_cast<size_t>((h - 1) * w + 1)] = 40;

    s->walkputbuffer_shadow(x, y, w, h, kPortX0, kPortY0, kPortX1, kPortY1, src, 90);

    ASSERT_TRUE(darker(px(x + 1, y + h), bg))
        << "feet pixel must shadow one row below the feet";
    for (int j = -2; j <= h + 3; j++)
        for (int i = -2; i <= w + 2; i++)
        {
            if (i == 1 && j == h)
                continue;
            ASSERT_TRUE(same(px(x + i, y + j), bg))
                << "unexpected shadow pixel at offset " << i << "," << j;
        }
}

TEST(VideoEffectsPrims, shadow_squashes_to_half_height)
{
    screen* s = scr();
    s->clearbuffer();
    const int x = 120, y = 80, w = 2, h = 4;
    fill_bright(x - 2, y - 2, w + 8, h + 8);
    RGB bg = px(x, y);

    std::vector<unsigned char> src(static_cast<size_t>(w * h), 40); // fully opaque

    s->walkputbuffer_shadow(x, y, w, h, kPortX0, kPortY0, kPortX1, kPortY1, src, 90);

    // A 4-row sprite squashes to 2 shadow rows: y+h and y+h-1.
    for (int i = 0; i < w; i++)
    {
        ASSERT_TRUE(darker(px(x + i, y + h), bg)) << "bottom shadow row, col " << i;
        ASSERT_TRUE(darker(px(x + i, y + h - 1), bg)) << "top shadow row, col " << i;
    }
    ASSERT_TRUE(same(px(x, y), bg)) << "sprite head area untouched";
    ASSERT_TRUE(same(px(x, y + 1), bg)) << "sprite torso area untouched";
    ASSERT_TRUE(same(px(x, y + 2), bg)) << "row above shadow untouched";
    ASSERT_TRUE(same(px(x, y + h + 1), bg)) << "row below shadow untouched";
}

TEST(VideoEffectsPrims, shadow_transparent_source_draws_nothing)
{
    screen* s = scr();
    s->clearbuffer();
    const int x = 60, y = 60, w = 4, h = 4;
    fill_bright(x - 2, y - 2, w + 8, h + 8);
    RGB bg = px(x, y);

    std::vector<unsigned char> src(static_cast<size_t>(w * h), 0); // all transparent

    s->walkputbuffer_shadow(x, y, w, h, kPortX0, kPortY0, kPortX1, kPortY1, src, 90);
    // Degenerate height early-out (no rows to draw).
    s->walkputbuffer_shadow(x, y, w, 0, kPortX0, kPortY0, kPortX1, kPortY1, src, 90);

    for (int j = -2; j <= h + 3; j++)
        for (int i = -2; i <= w + 2; i++)
            ASSERT_TRUE(same(px(x + i, y + j), bg))
                << "transparent source must not shadow at " << i << "," << j;
}

TEST(VideoEffectsPrims, shadow_clips_at_all_four_port_edges)
{
    screen* s = scr();
    s->clearbuffer();
    const Sint32 px0 = 150, py0 = 150, px1 = 170, py1 = 170;
    const int w = 6, h = 6; // shadow rows land at y+4..y+6
    std::vector<unsigned char> src(static_cast<size_t>(w * h), 40);
    fill_bright(130, 130, 60, 60);
    RGB bg = px(140, 140);

    // Left edge: only columns >= px0 draw.
    s->walkputbuffer_shadow(147, 152, w, h, px0, py0, px1, py1, src, 90);
    ASSERT_TRUE(same(px(149, 158), bg)) << "left of port untouched";
    ASSERT_TRUE(darker(px(150, 158), bg)) << "inside left edge drawn";

    // Right edge: columns >= px1 clipped.
    s->walkputbuffer_shadow(167, 152, w, h, px0, py0, px1, py1, src, 90);
    ASSERT_TRUE(darker(px(169, 158), bg)) << "inside right edge drawn";
    ASSERT_TRUE(same(px(170, 158), bg)) << "right of port untouched";

    // Top edge: shadow rows above py0 skipped (rows 149..151 -> 150,151 drawn).
    s->walkputbuffer_shadow(155, 145, w, h, px0, py0, px1, py1, src, 90);
    ASSERT_TRUE(same(px(157, 149), bg)) << "above port untouched";
    ASSERT_TRUE(darker(px(157, 150), bg)) << "inside top edge drawn";

    // Bottom edge: shadow rows at/below py1 skipped (rows 171,170,169 -> only 169).
    s->walkputbuffer_shadow(155, 165, w, h, px0, py0, px1, py1, src, 90);
    ASSERT_TRUE(darker(px(157, 169), bg)) << "inside bottom edge drawn";
    ASSERT_TRUE(same(px(157, 170), bg)) << "below port untouched";
    ASSERT_TRUE(same(px(157, 171), bg)) << "below port untouched";

    // Fully outside left/right: early-outs draw nothing.
    fill_bright(130, 130, 60, 60);
    s->walkputbuffer_shadow(170, 152, w, h, px0, py0, px1, py1, src, 90);
    s->walkputbuffer_shadow(144, 152, w, h, px0, py0, px1, py1, src, 90);
    for (int j = 130; j < 190; j++)
        for (int i = 130; i < 190; i++)
            ASSERT_TRUE(same(px(i, j), bg))
                << "fully-outside shadow must not draw at " << i << "," << j;
}

TEST(VideoEffectsPrims, reflect_draws_only_over_glass_tiles)
{
    screen* s = scr();
    s->clearbuffer();
    // 4x4 tile grid with glass only at tile (1,1): world px 16..31 square.
    std::vector<unsigned char> grid(16, 0);
    grid[1 + 4 * 1] = PIX_GLASS;
    fill_bright(10, 10, 50, 50);
    RGB bg = px(12, 12);

    const int w = 6, h = 6;
    std::vector<unsigned char> src(static_cast<size_t>(w * h), kDarkSprite);

    // Fully over glass: every covered pixel is blended.
    s->walkputbuffer_reflect(18, 18, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             src, 40, 200, grid, 4, 4, 0, 0);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ASSERT_TRUE(darker(px(18 + i, 18 + j), bg))
                << "glass-covered reflection pixel at " << i << "," << j;

    // Straddling the glass tile's corner: only px<32 in both axes draw.
    s->walkputbuffer_reflect(28, 28, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             src, 40, 200, grid, 4, 4, 0, 0);
    ASSERT_TRUE(darker(px(31, 31), bg)) << "still over glass";
    ASSERT_TRUE(same(px(32, 31), bg)) << "right of glass tile masked";
    ASSERT_TRUE(same(px(31, 32), bg)) << "below glass tile masked";
    ASSERT_TRUE(same(px(32, 32), bg)) << "diagonal off glass masked";

    // Entirely over non-glass: nothing drawn.
    s->walkputbuffer_reflect(40, 40, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             src, 40, 200, grid, 4, 4, 0, 0);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ASSERT_TRUE(same(px(40 + i, 40 + j), bg))
                << "non-glass reflection pixel at " << i << "," << j;
}

TEST(VideoEffectsPrims, reflect_flips_sprite_vertically)
{
    screen* s = scr();
    s->clearbuffer();
    std::vector<unsigned char> grid(16, PIX_GLASS); // all glass: mask passes
    const int x = 20, y = 20, w = 2, h = 4;
    fill_bright(x - 2, y - 2, w + 8, h + 8);
    RGB bg = px(x, y);

    // Single opaque pixel at the sprite's TOP row, col 0.
    std::vector<unsigned char> src(static_cast<size_t>(w * h), 0);
    src[0] = kDarkSprite;

    s->walkputbuffer_reflect(x, y, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             src, 40, 200, grid, 4, 4, 0, 0);

    ASSERT_TRUE(darker(px(x, y + h - 1), bg))
        << "top source row must land on the bottom target row";
    for (int j = -2; j <= h + 2; j++)
        for (int i = -2; i <= w + 2; i++)
        {
            if (i == 0 && j == h - 1)
                continue;
            ASSERT_TRUE(same(px(x + i, y + j), bg))
                << "unexpected reflection pixel at offset " << i << "," << j;
        }
}

TEST(VideoEffectsPrims, reflect_remaps_team_colors)
{
    screen* s = scr();
    s->clearbuffer(); // uniform background: equal blends compare equal
    std::vector<unsigned char> grid(16, PIX_GLASS);
    const unsigned char teamcolor = 40;
    const Uint8 alpha = 200;

    // Source color 255 remaps to teamcolor + (255-255) == teamcolor.
    std::vector<unsigned char> remap_src(1, 255);
    std::vector<unsigned char> plain_src(1, teamcolor);
    s->walkputbuffer_reflect(10, 10, 1, 1, kPortX0, kPortY0, kPortX1, kPortY1,
                             remap_src, teamcolor, alpha, grid, 4, 4, 0, 0);
    s->walkputbuffer_reflect(20, 10, 1, 1, kPortX0, kPortY0, kPortX1, kPortY1,
                             plain_src, teamcolor, alpha, grid, 4, 4, 0, 0);
    // Reference: the same palette color blended directly.
    s->pointb(30, 10, teamcolor, alpha);

    RGB reference = px(30, 10);
    ASSERT_FALSE(same(reference, px(31, 10))) << "reference blend must be visible";
    ASSERT_TRUE(same(px(10, 10), reference)) << "color >247 remaps to the team color";
    ASSERT_TRUE(same(px(20, 10), reference)) << "colors <=247 draw unremapped";
}

TEST(VideoEffectsPrims, reflect_clips_at_all_four_port_edges)
{
    screen* s = scr();
    s->clearbuffer();
    // All-glass 20x20 grid so the port rect is what masks, not the tiles.
    std::vector<unsigned char> grid(400, PIX_GLASS);
    const Sint32 px0 = 150, py0 = 150, px1 = 170, py1 = 170;
    const int w = 6, h = 6;
    std::vector<unsigned char> src(static_cast<size_t>(w * h), kDarkSprite);
    fill_bright(130, 130, 60, 60);
    RGB bg = px(140, 140);

    // Left edge.
    s->walkputbuffer_reflect(147, 155, w, h, px0, py0, px1, py1,
                             src, 40, 200, grid, 20, 20, 0, 0);
    ASSERT_TRUE(same(px(149, 157), bg)) << "left of port untouched";
    ASSERT_TRUE(darker(px(150, 157), bg)) << "inside left edge drawn";

    // Right edge.
    s->walkputbuffer_reflect(167, 155, w, h, px0, py0, px1, py1,
                             src, 40, 200, grid, 20, 20, 0, 0);
    ASSERT_TRUE(darker(px(169, 157), bg)) << "inside right edge drawn";
    ASSERT_TRUE(same(px(170, 157), bg)) << "right of port untouched";

    // Top edge: with the vertical flip, the clipped top rows must sample the
    // sprite's BOTTOM rows. Mark only the top source row: rows y..y+1 clip
    // away, and the marked row lands at y+h-1 = 152, inside the port.
    std::vector<unsigned char> top_src(static_cast<size_t>(w * h), 0);
    for (int i = 0; i < w; i++)
        top_src[static_cast<size_t>(i)] = kDarkSprite;
    s->walkputbuffer_reflect(155, 147, w, h, px0, py0, px1, py1,
                             top_src, 40, 200, grid, 20, 20, 0, 0);
    ASSERT_TRUE(same(px(157, 149), bg)) << "above port untouched";
    ASSERT_TRUE(darker(px(157, 152), bg)) << "flipped top row drawn inside port";

    // Bottom edge: rows at/below py1 clipped.
    s->walkputbuffer_reflect(155, 166, w, h, px0, py0, px1, py1,
                             src, 40, 200, grid, 20, 20, 0, 0);
    ASSERT_TRUE(darker(px(157, 169), bg)) << "inside bottom edge drawn";
    ASSERT_TRUE(same(px(157, 170), bg)) << "below port untouched";

    // Fully outside: early-outs draw nothing.
    fill_bright(130, 130, 60, 60);
    s->walkputbuffer_reflect(170, 155, w, h, px0, py0, px1, py1,
                             src, 40, 200, grid, 20, 20, 0, 0); // x >= portendx
    s->walkputbuffer_reflect(155, 170, w, h, px0, py0, px1, py1,
                             src, 40, 200, grid, 20, 20, 0, 0); // y >= portendy
    s->walkputbuffer_reflect(140, 155, w, h, px0, py0, px1, py1,
                             src, 40, 200, grid, 20, 20, 0, 0); // fully left: rowsize <= 0
    for (int j = 130; j < 190; j++)
        for (int i = 130; i < 190; i++)
            ASSERT_TRUE(same(px(i, j), bg))
                << "fully-outside reflection must not draw at " << i << "," << j;
}

TEST(VideoEffectsPrims, reflect_bounds_checks_grid_lookup)
{
    screen* s = scr();
    s->clearbuffer();
    std::vector<unsigned char> grid(16, PIX_GLASS);
    const int w = 4, h = 4;
    std::vector<unsigned char> src(static_cast<size_t>(w * h), kDarkSprite);
    fill_bright(10, 10, 40, 40);
    RGB bg = px(12, 12);

    // Negative world x -> gx < 0; huge world y -> gy >= gridh. Both skip
    // without touching grid memory.
    s->walkputbuffer_reflect(20, 20, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             src, 40, 200, grid, 4, 4, -100, 0);
    s->walkputbuffer_reflect(20, 20, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             src, 40, 200, grid, 4, 4, 0, 1000);
    // Transparent source over glass: skipped before any grid lookup.
    std::vector<unsigned char> clear_src(static_cast<size_t>(w * h), 0);
    s->walkputbuffer_reflect(20, 20, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             clear_src, 40, 200, grid, 4, 4, 0, 0);

    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ASSERT_TRUE(same(px(20 + i, 20 + j), bg))
                << "out-of-grid or transparent pixel drew at " << i << "," << j;
}

TEST(VideoEffectsPrims, reflect_default_mask_covers_glass_and_pure_water_only)
{
    screen* s = scr();
    s->clearbuffer();
    // One tile of each interesting id in an 8x1 grid: the default mask
    // (reflective_tiles()) must plot over glass and the pure water tiles but
    // never over watergrass edge tiles, grass, or void.
    const std::array<unsigned char, 8> tiles = {
        PIX_GLASS,         PIX_WATER1, PIX_WATER2,        PIX_WATER3,
        PIX_WATERGRASS_U,  PIX_GRASS1, PIX_WATERGRASS_LL, 0};
    std::vector<unsigned char> grid(tiles.begin(), tiles.end());
    const int w = 128, h = 16; // covers all 8 tiles
    std::vector<unsigned char> src(static_cast<size_t>(w * h), kDarkSprite);
    fill_bright(0, 0, w, h);
    RGB bg = px(8, 8);

    s->walkputbuffer_reflect(0, 0, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             src, 40, 200, grid, 8, 1, 0, 0);

    for (int i = 0; i < 8; i++)
    {
        const RGB c = px(i * 16 + 8, 8);
        if (i < 4)
            ASSERT_TRUE(darker(c, bg))
                << "tile id " << int(tiles[static_cast<size_t>(i)])
                << " must reflect by default";
        else
            ASSERT_TRUE(same(c, bg))
                << "tile id " << int(tiles[static_cast<size_t>(i)])
                << " must stay unmirrored by default";
    }
}

TEST(VideoEffectsPrims, reflect_honors_caller_supplied_mask)
{
    screen* s = scr();
    s->clearbuffer();
    // A caller mask marking ONLY grass overrides the default: glass stays
    // untouched, grass reflects.
    std::array<bool, 256> mask{};
    mask[PIX_GRASS1] = true;
    std::vector<unsigned char> grid = {PIX_GLASS, PIX_GRASS1};
    const int w = 32, h = 8;
    std::vector<unsigned char> src(static_cast<size_t>(w * h), kDarkSprite);
    fill_bright(0, 0, w, h);
    RGB bg = px(8, 4);

    s->walkputbuffer_reflect(0, 0, w, h, kPortX0, kPortY0, kPortX1, kPortY1,
                             src, 40, 200, grid, 2, 1, 0, 0, mask);

    ASSERT_TRUE(same(px(8, 4), bg))
        << "glass is not in the caller's mask: no reflection";
    ASSERT_TRUE(darker(px(24, 4), bg))
        << "grass is in the caller's mask: reflection drawn";

    // The exposed default LUT itself pins the production tile set.
    const std::array<bool, 256>& lut = reflective_tiles();
    ASSERT_TRUE(lut[PIX_GLASS] && lut[PIX_WATER1] && lut[PIX_WATER2] &&
                lut[PIX_WATER3])
        << "default LUT must mark glass + pure water";
    ASSERT_TRUE(lut[PIX_LAVA1] && lut[PIX_LAVA2] && lut[PIX_MARSH1] &&
                lut[PIX_MARSH2])
        << "default LUT must mark the Westlands lava + marsh tiles";
    ASSERT_FALSE(lut[PIX_SNOW1] || lut[PIX_SNOW2] || lut[PIX_ASH1] ||
                 lut[PIX_ASH2])
        << "snow and ash are dry ground: never reflective";
    int marked = 0;
    for (bool b : lut)
        marked += b ? 1 : 0;
    ASSERT_EQ(8, marked) << "default LUT must mark exactly the 8 tiles";
}

namespace
{
// The shared depth-fx composite fixture: draw an opaque uniform box into the
// redirected off-screen layer, then composite it back 1:1. Alpha 255 makes
// the final blend a plain copy, so the read-back IS the treated layer pixel
// — every per-mode formula below pins exact bytes.
constexpr int kFxLX = 40, kFxLY = 40, kFxLW = 32, kFxLH = 32;

void depth_composite(DepthFxParams fx, unsigned char alpha = 255,
                     unsigned char color = kBrightBG)
{
    screen* s = scr();
    s->clearbuffer();
    s->floor_layer_begin(kFxLX, kFxLY, kFxLW, kFxLH);
    s->fastbox(kFxLX, kFxLY, kFxLW, kFxLH, color, 1); // into the layer
    s->floor_layer_end(kFxLX, kFxLY, kFxLW, kFxLH, 1.0f,
                       kFxLX + kFxLW / 2, kFxLY + kFxLH / 2, alpha, fx);
}

// The legacy blend-toward-target step, exactly as floor_layer_end computes
// it (integer, truncating division).
Uint8 blend_toward(Uint8 c, int target, int t)
{
    return static_cast<Uint8>(c + ((target - c) * t) / 255);
}
} // namespace

TEST(VideoEffectsPrims, floor_layer_end_tint_pins_legacy_bytes_and_never_leaks)
{
    const int cx = kFxLX + kFxLW / 2, cy = kFxLY + kFxLH / 2;

    // Baseline: an untreated composite (mode Off) is a plain copy of the
    // layer color.
    depth_composite({});
    const RGB base = px(cx, cy);
    ASSERT_TRUE(base.r > 0 && base.g > 0 && base.b > 0)
        << "the composite must land on the render surface";

    // Tint one story down: EXACTLY the retired depth_tint pixels — the
    // strength-52 blend toward (58,74,140), truncating integer math.
    depth_composite({DepthFxMode::Tint, 1, 0});
    const RGB t1 = px(cx, cy);
    ASSERT_EQ(blend_toward(base.r, 58, 52), t1.r) << "legacy tint red, 1 story";
    ASSERT_EQ(blend_toward(base.g, 74, 52), t1.g) << "legacy tint green, 1 story";
    ASSERT_EQ(blend_toward(base.b, 140, 52), t1.b) << "legacy tint blue, 1 story";

    // Two (and more) stories down: the legacy strength-96 blend.
    depth_composite({DepthFxMode::Tint, 2, 0});
    const RGB t2 = px(cx, cy);
    ASSERT_EQ(blend_toward(base.r, 58, 96), t2.r) << "legacy tint red, 2 stories";
    ASSERT_EQ(blend_toward(base.g, 74, 96), t2.g) << "legacy tint green, 2 stories";
    ASSERT_EQ(blend_toward(base.b, 140, 96), t2.b) << "legacy tint blue, 2 stories";

    // The tick must not matter: tint is static by construction.
    depth_composite({DepthFxMode::Tint, 1, 1234});
    ASSERT_TRUE(same(px(cx, cy), t1)) << "tint must ignore the frame tick";

    // Leak check: the layer is repainted per pass, so an Off composite right
    // after a deep tint must reproduce the baseline exactly.
    depth_composite({});
    ASSERT_TRUE(same(px(cx, cy), base))
        << "a mode-Off composite must be bit-identical to the baseline";
    // ...including at a fade alpha (the old default-arg path).
    depth_composite({}, 200);
    const RGB faded_off = px(cx, cy);
    depth_composite({DepthFxMode::Off, 3, 77}, 200);
    ASSERT_TRUE(same(px(cx, cy), faded_off))
        << "mode Off must be a no-op regardless of stories/frame";
}

TEST(VideoEffectsPrims, floor_layer_end_haze_lifts_toward_pale_steel)
{
    const int cx = kFxLX + kFxLW / 2, cy = kFxLY + kFxLH / 2;

    depth_composite({});
    const RGB base = px(cx, cy);

    // One story: the 77/255 (~30%) blend toward (150,160,175), exact bytes.
    depth_composite({DepthFxMode::Haze, 1, 0});
    const RGB h1 = px(cx, cy);
    ASSERT_EQ(blend_toward(base.r, 150, 77), h1.r) << "haze red, 1 story";
    ASSERT_EQ(blend_toward(base.g, 160, 77), h1.g) << "haze green, 1 story";
    ASSERT_EQ(blend_toward(base.b, 175, 77), h1.b) << "haze blue, 1 story";

    // Deeper pulls harder (2 stories = 154), and haze ignores the tick.
    depth_composite({DepthFxMode::Haze, 2, 0});
    const RGB h2 = px(cx, cy);
    ASSERT_EQ(blend_toward(base.r, 150, 154), h2.r) << "haze red, 2 stories";
    depth_composite({DepthFxMode::Haze, 2, 999});
    ASSERT_TRUE(same(px(cx, cy), h2)) << "haze must ignore the frame tick";

    // The strength ramp caps at 3+ stories (210) instead of overshooting.
    depth_composite({DepthFxMode::Haze, 5, 0});
    const RGB h5 = px(cx, cy);
    ASSERT_EQ(blend_toward(base.r, 150, 210), h5.r) << "haze caps at 210";
}

TEST(VideoEffectsPrims, floor_layer_end_mist_dithers_exact_color_no_blends)
{
    depth_composite({});
    std::vector<RGB> base(static_cast<std::size_t>(kFxLW) * kFxLH);
    for (int y = 0; y < kFxLH; y++)
        for (int x = 0; x < kFxLW; x++)
            base[static_cast<std::size_t>(y * kFxLW + x)] =
                px(kFxLX + x, kFxLY + y);

    // The mist mask mirrors the renderer's hash stipple exactly (screen
    // coordinates; deterministic; NOT an ordered lattice — (x+y) masks read
    // as diagonal stripes). 1 story ~25% density, 2+ stories ~50%.
    const auto mist_hash = [](int sx, int sy) {
        Uint32 m = (static_cast<Uint32>(sx) * 0x9E3779B1u) ^
                   (static_cast<Uint32>(sy) * 0x85EBCA77u);
        m ^= m >> 15;
        m *= 0x2C1B3C6Du;
        m ^= m >> 12;
        return m & 3u;
    };
    // Story 1: hash-selected ~25% of pixels are EXACTLY the mist color
    // (150,160,175); every other pixel is EXACTLY the original. No alpha
    // blending anywhere: zero in-between colors.
    const RGB mist{150, 160, 175};
    depth_composite({DepthFxMode::Mist, 1, 0});
    for (int y = 0; y < kFxLH; y++)
        for (int x = 0; x < kFxLW; x++)
        {
            const RGB got = px(kFxLX + x, kFxLY + y);
            const bool lattice = mist_hash(kFxLX + x, kFxLY + y) < 1u;
            const RGB want =
                lattice ? mist
                        : base[static_cast<std::size_t>(y * kFxLW + x)];
            ASSERT_TRUE(same(got, want))
                << "mist(1 story) at " << x << "," << y
                << " must be " << (lattice ? "the exact mist entry"
                                           : "the untouched original");
        }

    // Story 2+: ~50% hash density, same exact two-value rule.
    depth_composite({DepthFxMode::Mist, 2, 0});
    int replaced = 0;
    for (int y = 0; y < kFxLH; y++)
        for (int x = 0; x < kFxLW; x++)
        {
            const RGB got = px(kFxLX + x, kFxLY + y);
            const bool lattice = mist_hash(kFxLX + x, kFxLY + y) < 2u;
            const RGB want =
                lattice ? mist
                        : base[static_cast<std::size_t>(y * kFxLW + x)];
            ASSERT_TRUE(same(got, want))
                << "mist(2 stories) at " << x << "," << y;
            replaced += lattice ? 1 : 0;
        }
    // Hash density: ~50% with binomial spread, and NOT the ordered diagonal
    // lattice (the stripe-pattern regression this replaced).
    ASSERT_NEAR(kFxLW * kFxLH / 2, replaced, kFxLW * kFxLH / 10)
        << "2 stories must mist about half the pixels";
    int diag_matches = 0;
    for (int y = 0; y < kFxLH; y++)
        for (int x = 0; x < kFxLW; x++)
            if ((mist_hash(kFxLX + x, kFxLY + y) < 2u) ==
                ((((kFxLX + x) + (kFxLY + y)) & 1) != 0))
                diag_matches++;
    ASSERT_LT(diag_matches, (kFxLW * kFxLH * 3) / 4)
        << "the stipple must not degenerate into the old diagonal lattice";

    // Mist ignores the tick.
    depth_composite({DepthFxMode::Mist, 2, 4321});
    for (int y = 0; y < kFxLH; y += 5)
        for (int x = 0; x < kFxLW; x += 5)
        {
            const bool lattice = mist_hash(kFxLX + x, kFxLY + y) < 2u;
            const RGB want =
                lattice ? mist
                        : base[static_cast<std::size_t>(y * kFxLW + x)];
            ASSERT_TRUE(same(px(kFxLX + x, kFxLY + y), want))
                << "mist must ignore the frame tick";
        }
}

TEST(VideoEffectsPrims, floor_layer_end_fog_drifts_with_the_tick_over_a_haze_base)
{
    // Fog = the haze wash plus noise-driven patches toward a lighter
    // fog-white: on a DARK base (so the fog color sits above the hazed
    // pixels in every channel) no channel can land below the plain haze
    // bytes, and the patch field must both repeat exactly for one tick and
    // move across ticks.
    depth_composite({DepthFxMode::Haze, 1, 0}, 255, kDarkSprite);
    std::vector<RGB> haze(static_cast<std::size_t>(kFxLW) * kFxLH);
    for (int y = 0; y < kFxLH; y++)
        for (int x = 0; x < kFxLW; x++)
            haze[static_cast<std::size_t>(y * kFxLW + x)] =
                px(kFxLX + x, kFxLY + y);

    depth_composite({DepthFxMode::Fog, 1, 0}, 255, kDarkSprite);
    std::vector<RGB> fog0(haze.size());
    int brightened = 0;
    std::array<bool, 256> red_levels{};
    for (int y = 0; y < kFxLH; y++)
        for (int x = 0; x < kFxLW; x++)
        {
            const std::size_t i = static_cast<std::size_t>(y * kFxLW + x);
            fog0[i] = px(kFxLX + x, kFxLY + y);
            const RGB& hz = haze[i];
            ASSERT_TRUE(fog0[i].r >= hz.r && fog0[i].g >= hz.g &&
                        fog0[i].b >= hz.b)
                << "fog patches only lift the haze base, never darken";
            if (!same(fog0[i], hz))
                brightened++;
            red_levels[fog0[i].r] = true;
        }
    ASSERT_GT(brightened, 0) << "some fog patch must cover the box";
    int distinct_reds = 0;
    for (bool hit : red_levels)
        distinct_reds += hit ? 1 : 0;
    ASSERT_GE(distinct_reds, 3)
        << "the fog load must VARY across the box (noise-patch structure), "
           "not land as one uniform second wash";

    // Determinism: the same tick reproduces the same bytes.
    depth_composite({DepthFxMode::Fog, 1, 0}, 255, kDarkSprite);
    for (int y = 0; y < kFxLH; y += 3)
        for (int x = 0; x < kFxLW; x += 3)
            ASSERT_TRUE(same(px(kFxLX + x, kFxLY + y),
                             fog0[static_cast<std::size_t>(y * kFxLW + x)]))
                << "fog at one tick must replay identically";

    // Drift: 8 ticks later the patch field has moved.
    depth_composite({DepthFxMode::Fog, 1, 8}, 255, kDarkSprite);
    bool moved = false;
    for (int y = 0; y < kFxLH && !moved; y++)
        for (int x = 0; x < kFxLW && !moved; x++)
            moved = !same(px(kFxLX + x, kFxLY + y),
                          fog0[static_cast<std::size_t>(y * kFxLW + x)]);
    ASSERT_TRUE(moved) << "fog must drift with the effects frame tick";

    // Deeper floors fog harder: the per-pixel patch alpha caps HIGHER at 2+
    // stories. Pin it on the sampler itself — scan for a bank-core pixel
    // that saturates the 1-story cap and check the 2-story value passes it,
    // while clear and unsaturated pixels agree between depths.
    bool found_saturated = false;
    int a1_cap = 0;
    for (int y = 0; y < 256 && !found_saturated; y++)
        for (int x = 0; x < 256 && !found_saturated; x++)
        {
            const int a1 = depth_fog_alpha_at(x, y, 0, 1);
            const int a2 = depth_fog_alpha_at(x, y, 0, 2);
            ASSERT_GE(a2, a1) << "depth can only add fog, never remove it";
            if (a1 == 0)
            {
                ASSERT_EQ(0, a2)
                    << "clear sky at one story must stay clear deeper down";
            }
            if (a2 > a1)
            {
                found_saturated = true;
                a1_cap = a1;
            }
        }
    ASSERT_TRUE(found_saturated)
        << "some bank core must exceed the 1-story alpha cap";
    ASSERT_GE(a1_cap, 40) << "the 1-story cap must sit in the spec'd "
                             "~40-60 patch-alpha band";
}
