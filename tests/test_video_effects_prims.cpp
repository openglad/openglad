// Direct tests for the multifloor-FX blit primitives:
// walkputbuffer_shadow (squashed black ground shadow), walkputbuffer_reflect
// (vertically flipped reflection masked to the reflective tiles — glass +
// pure water by default, caller-supplied LUT), and floor_layer_end's
// depth-tint color mod (apply for one composite, reset after).
#include <openglad/interface/screen.h>
#include <openglad/interface/base.h>
#include <openglad/interface/render/effects.h>
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

TEST(VideoEffectsPrims, floor_layer_end_cold_tint_shifts_hue_and_never_leaks)
{
    screen* s = scr();
    const int lx = 40, ly = 40, lw = 32, lh = 32;
    const int cx = lx + lw / 2, cy = ly + lh / 2;

    // One faded-floor composite: draw an opaque bright box into the
    // redirected off-screen layer, then composite it back 1:1 at alpha 200
    // with the given tint strength.
    auto composite = [&](unsigned char strength)
    {
        s->clearbuffer();
        s->floor_layer_begin(lx, ly, lw, lh);
        s->fastbox(lx, ly, lw, lh, kBrightBG, 1); // redirected into the layer
        s->floor_layer_end(lx, ly, lw, lh, 1.0f, cx, cy, 200, strength);
    };

    // Baseline: an untinted composite over the cleared surface.
    composite(0);
    const RGB base = px(cx, cy);
    ASSERT_TRUE(base.r > 0 && base.g > 0 && base.b > 0)
        << "the composite must land on the render surface";

    // Tinted: the blend must SHIFT HUE toward the cold target, not merely
    // scale channels — blue must move toward the target even when the source
    // has more blue than the target scales to (the old multiplicative mod
    // could never raise a low channel, which made the tint invisible on
    // pure-green terrain).
    composite(96);
    const RGB tinted = px(cx, cy);
    ASSERT_LT(tinted.r, base.r) << "warm channels must fall toward the cold target";
    ASSERT_LT(tinted.g, base.g) << "green must fall toward the cold target";
    ASSERT_NE(tinted.b, base.b) << "blue must move toward the target share";
    const int base_blue_share = static_cast<int>(base.b) * 100 /
        (base.r + base.g + base.b);
    const int tint_blue_share = static_cast<int>(tinted.b) * 100 /
        (tinted.r + tinted.g + tinted.b);
    ASSERT_GT(tint_blue_share, base_blue_share)
        << "the tinted pixel must read COLDER (larger blue share)";

    // Stronger tint pulls further.
    composite(200);
    const RGB deep = px(cx, cy);
    const int deep_blue_share = static_cast<int>(deep.b) * 100 /
        (deep.r + deep.g + deep.b);
    ASSERT_GT(deep_blue_share, tint_blue_share)
        << "tint strength must scale the shift";

    // Leak check: the layer is repainted per pass, so a strength-0 composite
    // right after a deep tint must reproduce the baseline exactly.
    composite(0);
    ASSERT_TRUE(same(px(cx, cy), base))
        << "a strength-0 composite must be bit-identical to the baseline";
}
