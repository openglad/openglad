#include <gtest/gtest.h>

#include <openglad/core/irandom.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>

#include <array>
#include <cstring>
#include <span>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
screen* vd_screen()
{
    return og::runtime::current_session->myscreen_;
}

// Palette index of one canvas pixel. get_pixel() answers with the first
// palette entry whose rgb matches, and every index these tests assert on is
// its own first match in our_palette.cpp, so index equality is exact.
int vd_index(screen* s, int x, int y)
{
    int index = -1;
    return s->get_pixel(x, y, &index);
}

// Raw rgb of one canvas pixel, for the blended/off-palette colours where no
// honest index exists.
std::array<Uint8, 3> vd_rgb(screen* s, int x, int y)
{
    std::array<Uint8, 3> value{};
    s->get_pixel(x, y, &value[0], &value[1], &value[2]);
    return value;
}

// How many pixels of row y in columns [x0,x1] carry palette index `color`.
int vd_row_hits(screen* s, int y, int x0, int x1, int color)
{
    int hits = 0;
    for (int x = x0; x <= x1; ++x)
    {
        if (vd_index(s, x, y) == color)
            ++hits;
    }
    return hits;
}

// The column of row y carrying `color`, or -1 when the row has none.
int vd_row_hit_column(screen* s, int y, int x0, int x1, int color)
{
    for (int x = x0; x <= x1; ++x)
    {
        if (vd_index(s, x, y) == color)
            return x;
    }
    return -1;
}

// video_sdl.cpp's file-local rng() helper draws from ctx().rng, so the
// invisibility gate and the phantom smear are only deterministic while a
// scripted IRandom is installed there.
struct ScopedVideoRandom
{
    IRandom* saved;
    explicit ScopedVideoRandom(IRandom* replacement) : saved(ctx().rng)
    {
        ctx().rng = replacement;
    }
    ~ScopedVideoRandom() { ctx().rng = saved; }
};
} // namespace

// ---------------------------------------------------------------------------
// walkputbuffer - the big pixel-copying function in video.cpp
// ---------------------------------------------------------------------------

TEST(VideoDraw, walkputbuffer_normal_copies_every_opaque_byte_into_its_own_cell)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    // Solid 16x16 sprite of palette index 100, with one transparent hole.
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    testbmp[5*16 + 5] = 0;

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    s->walkputbuffer(50, 50, 16, 16,
                     0, 0, 319, 199,
                     bmp_span, 40);

    EXPECT_EQ(100, vd_index(s, 50, 50))
        << "NORMAL walkputbuffer puts the first source byte at (x,y)";
    EXPECT_EQ(100, vd_index(s, 65, 65))
        << "the last source byte lands at (x+w-1, y+h-1)";
    EXPECT_EQ(100, vd_index(s, 60, 55))
        << "interior cells keep their own palette index";
    EXPECT_EQ(0, vd_index(s, 55, 55))
        << "index 0 is transparent: the hole keeps the cleared canvas";
    EXPECT_EQ(0, vd_index(s, 66, 65))
        << "nothing is written past the sprite's right edge";
    EXPECT_EQ(0, vd_index(s, 50, 66))
        << "nothing is written below the sprite's last row";
    EXPECT_EQ(0, vd_index(s, 49, 50))
        << "nothing is written left of the sprite";
}

TEST(VideoDraw, video_walkputbuffer_preserves_transparency_and_team_recolor)
{
    screen* const s = og::runtime::current_session->myscreen_;
    s->clearbuffer();
    s->fastbox(8, 8, 24, 4, 7, 1);

    // 0 stays transparent. 255 remaps to teamcolor, 248 remaps to
    // teamcolor+7, and an ordinary palette index stays unchanged.
    const std::array<unsigned char, 4> source = {0, 255, 248, 40};
    constexpr unsigned char teamcolor = 40;
    s->walkputbuffer(10, 10, 4, 1, 0, 0, 320, 200, source, teamcolor);
    s->pointb(20, 10, 7);
    s->pointb(21, 10, teamcolor);
    s->pointb(22, 10, static_cast<unsigned char>(teamcolor + 7));
    s->pointb(23, 10, 40);

    const auto rgb = [s](int x, int y) {
        std::array<Uint8, 3> value{};
        s->get_pixel(x, y, &value[0], &value[1], &value[2]);
        return value;
    };
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(rgb(20 + i, 10), rgb(10 + i, 10)) << "pixel " << i;
}

TEST(VideoDraw, video_walkputbuffer_alpha_matches_point_blending)
{
    screen* const s = og::runtime::current_session->myscreen_;
    s->clearbuffer();
    s->fastbox(8, 18, 24, 4, 7, 1);

    const std::array<unsigned char, 3> source = {0, 255, 40};
    constexpr unsigned char teamcolor = 40;
    constexpr Uint8 alpha = 137;
    s->walkputbuffer_alpha(
        10, 20, 3, 1, 0, 0, 320, 200, source, teamcolor, alpha);
    // Reference pixels use the original scalar primitive.
    s->pointb(20, 20, 7);
    s->pointb(21, 20, teamcolor, alpha);
    s->pointb(22, 20, 40, alpha);

    const auto rgb = [s](int x, int y) {
        std::array<Uint8, 3> value{};
        s->get_pixel(x, y, &value[0], &value[1], &value[2]);
        return value;
    };
    for (int i = 0; i < 3; ++i)
        EXPECT_EQ(rgb(20 + i, 20), rgb(10 + i, 20)) << "pixel " << i;
}

TEST(VideoDraw, video_walkputbuffer_alpha_clips_every_port_edge)
{
    screen* const s = og::runtime::current_session->myscreen_;
    constexpr int port_start = 150;
    constexpr int port_end = 170;
    constexpr int sprite_size = 6;
    const std::array<unsigned char, sprite_size * sprite_size> source = [] {
        std::array<unsigned char, sprite_size * sprite_size> pixels{};
        pixels.fill(40);
        return pixels;
    }();
    const auto rgb = [s](int x, int y) {
        std::array<Uint8, 3> value{};
        s->get_pixel(x, y, &value[0], &value[1], &value[2]);
        return value;
    };

    s->clearbuffer();
    s->fastbox(140, 140, 40, 40, 7, 1);
    const auto background = rgb(140, 140);

    s->walkputbuffer_alpha(
        147, 155, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40, 255);
    EXPECT_EQ(background, rgb(149, 157));
    EXPECT_NE(background, rgb(150, 157));

    s->walkputbuffer_alpha(
        167, 155, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40, 255);
    EXPECT_NE(background, rgb(169, 157));
    EXPECT_EQ(background, rgb(170, 157));

    s->walkputbuffer_alpha(
        155, 147, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40, 255);
    EXPECT_EQ(background, rgb(157, 149));
    EXPECT_NE(background, rgb(157, 150));

    s->walkputbuffer_alpha(
        155, 167, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40, 255);
    EXPECT_NE(background, rgb(157, 169));
    EXPECT_EQ(background, rgb(157, 170));

    s->walkputbuffer_alpha(
        port_end, 155, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40, 255);
    s->walkputbuffer_alpha(
        155, port_end, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40, 255);
    EXPECT_EQ(background, rgb(175, 155));
    EXPECT_EQ(background, rgb(155, 175));
}

TEST(VideoDraw, video_walkputbuffertext_clips_every_port_edge)
{
    screen* const s = og::runtime::current_session->myscreen_;
    constexpr int port_start = 150;
    constexpr int port_end = 170;
    constexpr int sprite_size = 6;
    const std::array<unsigned char, sprite_size * sprite_size> source = [] {
        std::array<unsigned char, sprite_size * sprite_size> pixels{};
        pixels.fill(40);
        return pixels;
    }();
    const auto rgb = [s](int x, int y) {
        std::array<Uint8, 3> value{};
        s->get_pixel(x, y, &value[0], &value[1], &value[2]);
        return value;
    };

    s->clearbuffer();
    s->fastbox(140, 140, 40, 40, 7, 1);
    const auto background = rgb(140, 140);

    s->walkputbuffertext(
        147, 155, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40);
    EXPECT_EQ(background, rgb(149, 157));
    EXPECT_NE(background, rgb(150, 157));

    s->walkputbuffertext(
        167, 155, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40);
    EXPECT_NE(background, rgb(169, 157));
    EXPECT_EQ(background, rgb(170, 157));

    s->walkputbuffertext(
        155, 147, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40);
    EXPECT_EQ(background, rgb(157, 149));
    EXPECT_NE(background, rgb(157, 150));

    s->walkputbuffertext(
        155, 167, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40);
    EXPECT_NE(background, rgb(157, 169));
    EXPECT_EQ(background, rgb(157, 170));

    s->walkputbuffertext(
        port_end, 155, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40);
    s->walkputbuffertext(
        155, port_end, sprite_size, sprite_size,
        port_start, port_start, port_end, port_end, source, 40);
    EXPECT_EQ(background, rgb(175, 155));
    EXPECT_EQ(background, rgb(155, 175));
}

TEST(VideoDraw, legacy_unbuffered_lines_forward_and_offscreen_lines_do_not_draw)
{
    screen* const s = og::runtime::current_session->myscreen_;
    const auto rgb = [s](int x, int y) {
        std::array<Uint8, 3> value{};
        s->get_pixel(x, y, &value[0], &value[1], &value[2]);
        return value;
    };

    s->clearbuffer();
    s->pointb(30, 30, 40);
    const auto expected = rgb(30, 30);
    s->hor_line(10, 10, 3, 40, 0);
    s->ver_line(20, 10, 3, 40, 0);
    for (int offset = 0; offset < 3; ++offset)
    {
        EXPECT_EQ(expected, rgb(10 + offset, 10));
        EXPECT_EQ(expected, rgb(20, 10 + offset));
    }

    const auto capture_frame = [s] {
        std::vector<Uint8> pixels;
        pixels.reserve(static_cast<std::size_t>(s->canvas_w()) *
                       static_cast<std::size_t>(s->canvas_h()) * 3u);
        for (int y = 0; y < s->canvas_h(); ++y)
        {
            for (int x = 0; x < s->canvas_w(); ++x)
            {
                Uint8 red = 0;
                Uint8 green = 0;
                Uint8 blue = 0;
                s->get_pixel(x, y, &red, &green, &blue);
                pixels.push_back(red);
                pixels.push_back(green);
                pixels.push_back(blue);
            }
        }
        return pixels;
    };
    const std::vector<Uint8> before_offscreen_lines = capture_frame();
    s->draw_line(-20, 100, -10, 110, 40);
    s->draw_line(100, -20, 110, -10, 40);
    s->draw_line(400, 100, 410, 110, 40);
    s->draw_line(100, 300, 110, 310, 40);
    EXPECT_EQ(before_offscreen_lines, capture_frame());
}


TEST(VideoDraw, walkputbuffer_outline_mode_paints_the_border_cells_in_the_outline_colour)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    s->walkputbuffer(50, 50, 16, 16,
                     0, 0, 319, 199,
                     bmp_span, 40,
                     OUTLINE_MODE, 0, OUTLINE_NAMED, 0);

    EXPECT_EQ(OUTLINE_NAMED, vd_index(s, 50, 50))
        << "the top-left cell is an edge cell and wears the outline colour";
    EXPECT_EQ(OUTLINE_NAMED, vd_index(s, 65, 50))
        << "curx == walkerwidth-1 is an edge cell";
    EXPECT_EQ(OUTLINE_NAMED, vd_index(s, 50, 65))
        << "cury == totrows-1 is an edge cell";
    EXPECT_EQ(OUTLINE_NAMED, vd_index(s, 65, 65))
        << "the bottom-right cell is an edge cell";
    EXPECT_EQ(OUTLINE_NAMED, vd_index(s, 58, 50))
        << "the whole top row is outlined, not just its corners";
    EXPECT_EQ(100, vd_index(s, 51, 51))
        << "interior opaque cells keep their own palette index";
    EXPECT_EQ(100, vd_index(s, 60, 60))
        << "interior opaque cells keep their own palette index";
    EXPECT_EQ(0, vd_index(s, 66, 50))
        << "the outline does not spill past the sprite box";
}


TEST(VideoDraw, walkputbuffer_phantom_shift_random_smears_the_canvas_never_the_sprite)
{
    screen* const s = vd_screen();
    const std::array<unsigned char, 1> solid{1};

    // Ground: the cell the sprite covers is index 10, its right neighbour 40.
    const auto seed_ground = [s] {
        s->clearbuffer();
        s->pointb(51, 51, 10);
        s->pointb(52, 51, 40);
    };

    seed_ground();
    ASSERT_EQ(10, vd_index(s, 51, 51)) << "ground under the phantom cell";
    ASSERT_EQ(40, vd_index(s, 52, 51)) << "ground right of the phantom cell";

    {
        FixedRandom rng_one(1);
        ScopedVideoRandom scoped(&rng_one); // rng(2) -> 1
        s->walkputbuffer(51, 51, 1, 1, 0, 0, 319, 199, solid, 40,
                         static_cast<unsigned char>(PHANTOM_MODE), 0, 0,
                         static_cast<unsigned char>(SHIFT_RANDOM));
    }
    EXPECT_EQ(40, vd_index(s, 51, 51))
        << "rng(2)==1 copies the canvas pixel at buffoff+1 onto buffoff";
    EXPECT_EQ(40, vd_index(s, 52, 51))
        << "the sampled neighbour is read, not moved";

    seed_ground();
    {
        FixedRandom rng_zero(0);
        ScopedVideoRandom scoped(&rng_zero); // rng(2) -> 0
        s->walkputbuffer(51, 51, 1, 1, 0, 0, 319, 199, solid, 40,
                         static_cast<unsigned char>(PHANTOM_MODE), 0, 0,
                         static_cast<unsigned char>(SHIFT_RANDOM));
    }
    EXPECT_EQ(10, vd_index(s, 51, 51))
        << "rng(2)==0 copies the cell onto itself; the sprite's own colour 1 "
           "never reaches the canvas in PHANTOM_MODE";
}


TEST(VideoDraw, walkputbuffer_invisible_mode_gates_interior_cells_on_the_rng)
{
    screen* const s = vd_screen();
    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    constexpr unsigned char kOutline = 1;

    s->clearbuffer();
    {
        FixedRandom rng_zero(0);
        ScopedVideoRandom scoped(&rng_zero); // rng(1) -> 0, not > 8: draw
        s->walkputbuffer(50, 50, 16, 16, 0, 0, 319, 199, bmp_span, 40,
                         INVISIBLE_MODE, /*invisibility*/ 1, kOutline, 0);
    }
    EXPECT_EQ(kOutline, vd_index(s, 50, 50))
        << "an edge cell wears the outline colour before the rng gate runs";
    EXPECT_EQ(kOutline, vd_index(s, 65, 65))
        << "the far edge cell wears the outline colour too";
    EXPECT_EQ(100, vd_index(s, 51, 51))
        << "rng(invisibility) <= 8 draws the interior cell's own colour";
    EXPECT_EQ(100, vd_index(s, 60, 60))
        << "rng(invisibility) <= 8 draws the interior cell's own colour";

    s->clearbuffer();
    {
        FixedRandom rng_nine(9);
        ScopedVideoRandom scoped(&rng_nine); // rng(10) -> 9, > 8: skip
        s->walkputbuffer(50, 50, 16, 16, 0, 0, 319, 199, bmp_span, 40,
                         INVISIBLE_MODE, /*invisibility*/ 10, kOutline, 0);
    }
    EXPECT_EQ(kOutline, vd_index(s, 50, 50))
        << "the outline survives however invisible the body is";
    EXPECT_EQ(0, vd_index(s, 51, 51))
        << "rng(invisibility) > 8 leaves the interior cell untouched";
    EXPECT_EQ(0, vd_index(s, 60, 60))
        << "rng(invisibility) > 8 leaves the interior cell untouched";
}


TEST(VideoDraw, walkputbuffer_flash_paints_opaque_cells_in_the_brightened_colour)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    unsigned char testbmp[16*16];
    memset(testbmp, 100, sizeof(testbmp));
    testbmp[5*16 + 5] = 0;

    auto bmp_span = std::span<const unsigned char>(testbmp, 256);
    s->walkputbuffer_flash(50, 50, 16, 16,
                           0, 0, 319, 199,
                           bmp_span, 40);

    // Palette entry 100 is (45,24,45) in 6-bit registers, so the flash rgb is
    // (45*4=180 > 155 -> 255, 24*4=96 -> 96+100=196, 180 -> 255).
    const std::array<Uint8, 3> flashed{255, 196, 255};
    const std::array<Uint8, 3> black{0, 0, 0};
    EXPECT_EQ(flashed, vd_rgb(s, 50, 50))
        << "the first opaque cell is painted in the brightened flash colour";
    EXPECT_EQ(flashed, vd_rgb(s, 65, 65))
        << "the last opaque cell is painted in the brightened flash colour";
    EXPECT_EQ(flashed, vd_rgb(s, 60, 55))
        << "every opaque cell flashes the same colour";
    EXPECT_EQ(black, vd_rgb(s, 55, 55))
        << "index-0 cells stay transparent under the flash";
    EXPECT_EQ(black, vd_rgb(s, 66, 50))
        << "the flash does not spill past the sprite box";
}


// ---------------------------------------------------------------------------
// putdata - another big pixel function
// ---------------------------------------------------------------------------

TEST(VideoDraw, putdata_writes_each_non_zero_source_byte_verbatim)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    unsigned char testbmp[16*16];
    memset(testbmp, 50, sizeof(testbmp));
    testbmp[3*16 + 4] = 0;

    s->putdata(50, 50, 16, 16, testbmp);

    EXPECT_EQ(50, vd_index(s, 50, 50)) << "putdata starts at (startx,starty)";
    EXPECT_EQ(50, vd_index(s, 65, 65)) << "putdata ends at (x+xsize-1,y+ysize-1)";
    EXPECT_EQ(0, vd_index(s, 54, 53)) << "index 0 is transparent";
    EXPECT_EQ(0, vd_index(s, 66, 65)) << "nothing is written past the box";
    EXPECT_EQ(0, vd_index(s, 50, 66)) << "nothing is written below the box";
}


TEST(VideoDraw, putdata_at_origin_starts_at_the_first_row_and_column)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    unsigned char testbmp[8*8];
    memset(testbmp, 75, sizeof(testbmp));

    s->putdata(0, 0, 8, 8, testbmp);

    EXPECT_EQ(75, vd_index(s, 0, 0)) << "the origin pixel is written";
    EXPECT_EQ(75, vd_index(s, 7, 7)) << "the 8x8 box ends at (7,7)";
    EXPECT_EQ(0, vd_index(s, 8, 0)) << "column 8 is outside the box";
    EXPECT_EQ(0, vd_index(s, 0, 8)) << "row 8 is outside the box";
}


// ---------------------------------------------------------------------------
// draw_box variations
// ---------------------------------------------------------------------------

TEST(VideoDraw, box_zero_size_inks_exactly_one_pixel)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->draw_box(50, 50, 50, 50, 100, 0, 1);

    EXPECT_EQ(100, vd_index(s, 50, 50))
        << "a degenerate box has xlength=ylength=1 and inks its own pixel";
    EXPECT_EQ(0, vd_index(s, 49, 50)) << "no pixel left of the box";
    EXPECT_EQ(0, vd_index(s, 51, 50)) << "no pixel right of the box";
    EXPECT_EQ(0, vd_index(s, 50, 49)) << "no pixel above the box";
    EXPECT_EQ(0, vd_index(s, 50, 51)) << "no pixel below the box";
}


TEST(VideoDraw, box_large_filled_covers_every_row_of_the_canvas)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->draw_box(0, 0, 319, 199, 50, 1, 1);

    EXPECT_EQ(50, vd_index(s, 0, 0)) << "the filled box starts at its corner";
    EXPECT_EQ(50, vd_index(s, 319, 199))
        << "the filled box covers its last row and column";
    EXPECT_EQ(50, vd_index(s, 319, 0)) << "the first row is filled to the right edge";
    EXPECT_EQ(50, vd_index(s, 0, 199)) << "the last row is filled from the left edge";
    EXPECT_EQ(50, vd_index(s, 160, 100)) << "the interior is filled too";
    EXPECT_EQ(320, vd_row_hits(s, 100, 0, 319, 50))
        << "every pixel of an interior row is filled";
}


// ---------------------------------------------------------------------------
// draw_button variations with different depths
// ---------------------------------------------------------------------------

TEST(VideoDraw, button_border_zero_fills_the_flat_face_with_index_13)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->draw_button(10, 10, 50, 30, 0, 1);

    EXPECT_EQ(13, vd_index(s, 10, 10)) << "border 0 has no bevel: the corner is face";
    EXPECT_EQ(13, vd_index(s, 50, 30)) << "the face reaches the bottom-right corner";
    EXPECT_EQ(13, vd_index(s, 30, 20)) << "the middle is face too";
    EXPECT_EQ(41, vd_row_hits(s, 20, 10, 50, 13))
        << "each filled row spans x1..x2 inclusive";
    EXPECT_EQ(0, vd_index(s, 51, 30)) << "nothing is drawn right of x2";
    EXPECT_EQ(0, vd_index(s, 9, 10)) << "nothing is drawn left of x1";
    EXPECT_EQ(0, vd_index(s, 10, 31)) << "nothing is drawn below y2";
}


TEST(VideoDraw, button_border_three_insets_one_bevel_ring_per_recursion)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->draw_button(60, 10, 100, 30, 3, 1);

    // Each ring draws top 15, bottom 11, left 14, right 12 in that order, so
    // the left ver_line owns the ring's left corners.
    EXPECT_EQ(14, vd_index(s, 60, 10)) << "outer ring corner: left bevel wins";
    EXPECT_EQ(14, vd_index(s, 61, 11)) << "second ring is inset by one pixel";
    EXPECT_EQ(14, vd_index(s, 62, 12)) << "third ring is inset by two pixels";
    EXPECT_EQ(13, vd_index(s, 63, 13))
        << "after three rings the recursion fills with the face colour";
    EXPECT_EQ(15, vd_index(s, 70, 10)) << "outer ring top run is index 15";
    EXPECT_EQ(15, vd_index(s, 70, 11)) << "second ring top run is index 15";
    EXPECT_EQ(15, vd_index(s, 70, 12)) << "third ring top run is index 15";
    EXPECT_EQ(11, vd_index(s, 70, 30)) << "outer ring bottom run is index 11";
    EXPECT_EQ(12, vd_index(s, 100, 20)) << "outer ring right run is index 12";
    EXPECT_EQ(12, vd_index(s, 99, 20)) << "second ring right run is index 12";
    EXPECT_EQ(0, vd_index(s, 101, 20)) << "nothing is drawn right of x2";
}


// ---------------------------------------------------------------------------
// draw_dialog
// ---------------------------------------------------------------------------

TEST(VideoDraw, dialog_small_returns_x1_plus_6_and_draws_frame_and_bars)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    const int result = s->draw_dialog(10, 10, 100, 60, "Small");

    ASSERT_EQ(16, result) << "draw_dialog returns x1+6, the text's left edge";
    EXPECT_EQ(14, vd_index(s, 10, 10))
        << "the depth-1 button frame's corner is its left bevel";
    EXPECT_EQ(15, vd_index(s, 50, 10)) << "the frame's top run is index 15";
    EXPECT_EQ(10, vd_index(s, 15, 14))
        << "the header bar's top indent line is index 10";
    EXPECT_EQ(11, vd_index(s, 14, 15))
        << "the header bar's left indent line is index 11";
    EXPECT_EQ(12, vd_index(s, 15, 15)) << "the header bar's face is index 12";
    EXPECT_EQ(10, vd_index(s, 15, 30))
        << "the body bar starts at y1+20 with its own indent line";
    EXPECT_EQ(12, vd_index(s, 15, 31)) << "the body bar's face is index 12";
    EXPECT_EQ(0, vd_index(s, 101, 10)) << "nothing is drawn right of x2";
}


TEST(VideoDraw, dialog_large_renders_its_header_through_the_big_font)
{
    screen* const s = vd_screen();
    s->clearbuffer();
    trace_clear();

    const int result = s->draw_dialog(5, 5, 310, 190, "Large Dialog Title");

    ASSERT_EQ(11, result) << "draw_dialog returns x1+6";
    ASSERT_TRUE(trace_contains("dialog", "header='Large Dialog Title'"))
        << "the header string reaches the big-font writer";
    ASSERT_TRUE(trace_contains("dialog", "valid=1"))
        << "the big font is loaded, so the header is really drawn";
    ASSERT_TRUE(trace_contains("dialog", "sizex=9 sizey=12"))
        << "the header is measured with the 9x12 big-font glyph box";
    EXPECT_EQ(12, vd_index(s, 160, 100))
        << "the body bar's face fills the dialog's interior";
    EXPECT_EQ(14, vd_index(s, 5, 5)) << "the frame corner is the left bevel";
    EXPECT_EQ(10, vd_index(s, 160, 25))
        << "the body bar's top indent line sits at y1+20";
}


// ---------------------------------------------------------------------------
// draw_line variations (Bresenham algorithm)
// ---------------------------------------------------------------------------

TEST(VideoDraw, line_steep_positive_plots_one_pixel_per_row_between_its_endpoints)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->draw_line(50, 10, 60, 100, 77);

    EXPECT_EQ(77, vd_index(s, 50, 10)) << "the first endpoint is plotted";
    EXPECT_EQ(77, vd_index(s, 60, 100)) << "the last endpoint is plotted";
    EXPECT_EQ(77, vd_index(s, 55, 55)) << "the midpoint of the steep run";
    for (int y = 10; y <= 100; ++y)
    {
        EXPECT_EQ(1, vd_row_hits(s, y, 45, 65, 77))
            << "row " << y << " of a steep line carries exactly one pixel";
    }
    EXPECT_EQ(0, vd_row_hits(s, 9, 45, 65, 77)) << "no pixel above the line";
    EXPECT_EQ(0, vd_row_hits(s, 101, 45, 65, 77)) << "no pixel below the line";
}


TEST(VideoDraw, line_steep_reversed_endpoints_anchor_the_rounding_at_the_first_point)
{
    screen* const s = vd_screen();

    // Forward: rounding walks out of (50,10), so row 18 is still column 50.
    s->clearbuffer();
    s->draw_line(50, 10, 60, 100, 77);
    EXPECT_EQ(50, vd_row_hit_column(s, 18, 45, 65, 77))
        << "the forward line's row 18 sits at column 50";

    // Reversed: the same geometry walked from (60,100) rounds the other way,
    // so row 18 lands one column further right. draw_line is NOT endpoint-order
    // symmetric, and the run is still connected one-pixel-per-row.
    s->clearbuffer();
    s->draw_line(60, 100, 50, 10, 88);
    EXPECT_EQ(88, vd_index(s, 60, 100)) << "the first endpoint is plotted";
    EXPECT_EQ(88, vd_index(s, 50, 10)) << "the last endpoint is plotted";
    EXPECT_EQ(88, vd_index(s, 55, 55)) << "the midpoint of the steep run";
    EXPECT_EQ(51, vd_row_hit_column(s, 18, 45, 65, 88))
        << "the reversed line's row 18 sits at column 51, not 50";
    EXPECT_EQ(0, vd_index(s, 50, 18))
        << "the forward line's row-18 column stays clear when reversed";
    for (int y = 10; y <= 100; ++y)
    {
        EXPECT_EQ(1, vd_row_hits(s, y, 45, 65, 88))
            << "row " << y << " of the reversed line carries exactly one pixel";
    }
}


TEST(VideoDraw, line_single_point_plots_exactly_its_one_endpoint)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->draw_line(50, 50, 50, 50, 99);

    EXPECT_EQ(99, vd_index(s, 50, 50)) << "a zero-length line plots its endpoint";
    EXPECT_EQ(0, vd_index(s, 49, 50)) << "and nothing left of it";
    EXPECT_EQ(0, vd_index(s, 51, 50)) << "and nothing right of it";
    EXPECT_EQ(0, vd_index(s, 50, 49)) << "and nothing above it";
    EXPECT_EQ(0, vd_index(s, 50, 51)) << "and nothing below it";
}


// ---------------------------------------------------------------------------
// draw_rect_filled with various alpha values
// ---------------------------------------------------------------------------

TEST(VideoDraw, rect_filled_zero_alpha_leaves_the_destination_alone)
{
    screen* const s = vd_screen();
    s->clearbuffer();
    s->fastbox(50, 50, 30, 20, 7, 1);
    const auto ground = vd_rgb(s, 55, 55);
    const auto corner_ground = vd_rgb(s, 50, 50);

    // Negative control: the colour we are about to blend in is a different one.
    s->pointb(200, 150, 150);
    ASSERT_NE(ground, vd_rgb(s, 200, 150))
        << "colour 150 must differ from the ground, or alpha 0 proves nothing";

    s->draw_rect_filled(50, 50, 30, 20, 150, 0);

    EXPECT_EQ(ground, vd_rgb(s, 55, 55))
        << "alpha 0 blends nothing into the destination pixel";
    EXPECT_EQ(corner_ground, vd_rgb(s, 50, 50))
        << "alpha 0 leaves the rect's first pixel alone";
    EXPECT_EQ(7, vd_index(s, 79, 69))
        << "alpha 0 leaves the rect's last pixel alone";
}


TEST(VideoDraw, rect_filled_half_alpha_matches_the_scalar_point_blend)
{
    screen* const s = vd_screen();
    s->clearbuffer();
    s->fastbox(80, 50, 30, 20, 7, 1);
    s->fastbox(200, 50, 1, 1, 7, 1); // same ground for the reference pixel
    const auto ground = vd_rgb(s, 200, 50);

    s->draw_rect_filled(80, 50, 30, 20, 200, 128);
    s->pointb(200, 50, 200, 128); // the original scalar primitive

    const auto reference = vd_rgb(s, 200, 50);
    ASSERT_NE(ground, reference)
        << "a half blend of colour 200 over ground 7 must move the pixel";
    EXPECT_EQ(reference, vd_rgb(s, 85, 55))
        << "an interior pixel is the half-blend of colour 200 over the ground";
    EXPECT_EQ(reference, vd_rgb(s, 80, 50))
        << "the rect's first pixel blends the same way";
    EXPECT_EQ(reference, vd_rgb(s, 109, 69))
        << "the rect's last pixel blends the same way";
    EXPECT_EQ(0, vd_index(s, 79, 50))
        << "the pixel left of the rect is untouched (cleared canvas)";
    EXPECT_EQ(0, vd_index(s, 80, 70))
        << "the row below the rect is untouched (cleared canvas)";
}


TEST(VideoDraw, rect_filled_opaque_lands_exactly_on_the_colour_inside_the_rect)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->draw_rect_filled(110, 50, 30, 20, 250, 255);
    s->pointb(200, 50, 250); // opaque reference through pointb

    // Compare rgb, not index: a blended colour has no honest palette index.
    const auto opaque = vd_rgb(s, 200, 50);
    const std::array<Uint8, 3> black{0, 0, 0};
    ASSERT_NE(black, opaque) << "colour 250 is not the cleared canvas";
    EXPECT_EQ(opaque, vd_rgb(s, 110, 50)) << "alpha 255 lands on the colour";
    EXPECT_EQ(opaque, vd_rgb(s, 139, 69))
        << "the rect covers w x h pixels from (x,y)";
    EXPECT_EQ(black, vd_rgb(s, 140, 50)) << "column x+w is outside the rect";
    EXPECT_EQ(black, vd_rgb(s, 110, 70)) << "row y+h is outside the rect";
}


// ---------------------------------------------------------------------------
// fastbox
// ---------------------------------------------------------------------------

TEST(VideoDraw, fastbox_large_fills_exactly_w_by_h_pixels)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->fastbox(0, 0, 100, 100, 50);

    EXPECT_EQ(50, vd_index(s, 0, 0)) << "the fill starts at (x,y)";
    EXPECT_EQ(50, vd_index(s, 99, 99)) << "the fill ends at (x+w-1,y+h-1)";
    EXPECT_EQ(100, vd_row_hits(s, 50, 0, 199, 50))
        << "each filled row is exactly w pixels wide";
    EXPECT_EQ(0, vd_index(s, 100, 0)) << "column x+w is outside the fill";
    EXPECT_EQ(0, vd_index(s, 0, 100)) << "row y+h is outside the fill";
}


TEST(VideoDraw, fastbox_small_inks_exactly_one_pixel)
{
    screen* const s = vd_screen();
    s->clearbuffer();

    s->fastbox(100, 100, 1, 1, 200);

    EXPECT_EQ(200, vd_index(s, 100, 100)) << "a 1x1 fastbox inks its own pixel";
    EXPECT_EQ(0, vd_index(s, 101, 100)) << "and nothing right of it";
    EXPECT_EQ(0, vd_index(s, 100, 101)) << "and nothing below it";
    EXPECT_EQ(0, vd_index(s, 99, 100)) << "and nothing left of it";
}


// ---------------------------------------------------------------------------
// text rendering through video's text objects
// ---------------------------------------------------------------------------

TEST(VideoDraw, text_write_xy_inks_every_glyph_byte_in_the_requested_colour)
{
    screen* const s = vd_screen();
    text& font = s->text_normal;
    ASSERT_NE(nullptr, font.letters) << "the small font must be loaded";
    ASSERT_TRUE(font.letters->valid()) << "the small font must be loaded";
    const int glyph_w = font.letters->w;
    const int glyph_h = font.letters->h;
    ASSERT_GT(glyph_w, 0) << "a zero-wide glyph box paints nothing (issue #259)";
    ASSERT_GT(glyph_h, 0) << "a zero-high glyph box paints nothing (issue #259)";

    const std::string_view message = "Test text";
    for (const char letter : message)
    {
        ASSERT_LT(static_cast<int>(static_cast<unsigned char>(letter)),
                  static_cast<int>(font.letters->frames))
            << "glyph '" << letter << "' must exist without the '?' fallback";
    }

    s->clearbuffer();
    font.write_xy(10, 10, message, WHITE);

    // The oracle is the font asset itself: each non-zero glyph byte must show
    // up as WHITE (the glyph bytes are all in the >247 team-colour band, which
    // putdatatext replaces with the requested colour), and each zero byte must
    // leave the cleared canvas alone.
    const std::size_t stride =
        static_cast<std::size_t>(glyph_w) * static_cast<std::size_t>(glyph_h);
    int expected_ink = 0;
    int painted_ink = 0;
    for (std::size_t i = 0; i < message.size(); ++i)
    {
        const auto letter = static_cast<unsigned char>(message[i]);
        const unsigned char* glyph =
            font.letters->data.get() + static_cast<std::size_t>(letter) * stride;
        const int origin_x = 10 + static_cast<int>(i) * (glyph_w + 1);
        for (int gy = 0; gy < glyph_h; ++gy)
        {
            for (int gx = 0; gx < glyph_w; ++gx)
            {
                const unsigned char byte =
                    glyph[static_cast<std::size_t>(gy) * static_cast<std::size_t>(glyph_w) +
                          static_cast<std::size_t>(gx)];
                const int expected = byte == 0 ? 0 : static_cast<int>(WHITE);
                if (byte != 0)
                    ++expected_ink;
                const int actual = vd_index(s, origin_x + gx, 10 + gy);
                if (actual == static_cast<int>(WHITE))
                    ++painted_ink;
                ASSERT_EQ(expected, actual)
                    << "glyph '" << message[i] << "' byte (" << gx << "," << gy
                    << ") at (" << origin_x + gx << "," << 10 + gy << ")";
            }
        }
        if (i + 1 < message.size())
        {
            EXPECT_EQ(0, vd_index(s, origin_x + glyph_w, 10))
                << "the one-pixel gap column between glyphs stays clear";
        }
    }
    ASSERT_GT(expected_ink, 20) << "the oracle itself must expect real ink";
    EXPECT_EQ(expected_ink, painted_ink)
        << "write_xy inks exactly the font's non-zero bytes";
    EXPECT_EQ(0, vd_index(s, 9, 10)) << "nothing is inked left of x";
    EXPECT_EQ(0, vd_index(s, 10, 9)) << "nothing is inked above y";
    EXPECT_EQ(0, vd_index(s, 10, 10 + glyph_h))
        << "nothing is inked below the glyph box";
}


TEST(VideoDraw, video_text_write_xy_center)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_center(160, 100, WHITE, "Centered");
}


TEST(VideoDraw, video_text_write_xy_shadow)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_shadow(10, 30, WHITE, "Shadow text");
}


TEST(VideoDraw, video_text_big)
{
    og::runtime::current_session->myscreen_->text_big.write_xy(10, 50, "Big text", WHITE);
}
