// Direct tests for the low-level blit primitives every other renderer oracle
// stands on: the pointb family, the line/box fills, the putdata/putbuffer
// span blitters and walkputbuffer's edge clipping. Each case reads the pixels
// back with get_pixel, so a primitive that draws nothing, draws one cell, or
// draws opaquely where it should blend fails here instead of quietly moving
// every sprite in the game.
#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <openglad/interface/screen.h>

#include <cstring>
#include <ostream>
#include <span>

// myscreen is now a macro defined in base.h (via game_session.h)

namespace
{
// Palette index 7 is (56,56,56) -> RGB(224,224,224): a bright ground that any
// blend of a darker colour visibly changes.
constexpr unsigned char kGround = 7;
// Palette index 100 is (45,24,45) -> RGB(180,96,180). Indices 7, 40, 47, 50,
// 60, 80, 100, 150 and 250 are all unique in our.pal, so get_pixel's reverse
// palette scan round-trips them exactly.
constexpr unsigned char kInk = 100;
// Full-screen clipping port for the buffer blitters.
constexpr Sint32 kPortX0 = 0, kPortY0 = 0, kPortX1 = 319, kPortY1 = 199;

screen* scr()
{
    return og::runtime::current_session->myscreen_;
}

struct RGB
{
    Uint8 r = 0, g = 0, b = 0;
    bool operator==(const RGB&) const = default;
};

std::ostream& operator<<(std::ostream& os, const RGB& c)
{
    return os << "RGB(" << static_cast<int>(c.r) << ',' << static_cast<int>(c.g)
              << ',' << static_cast<int>(c.b) << ')';
}

// The true colour at (x, y) — used wherever a blend has no palette index.
RGB px(int x, int y)
{
    RGB c;
    scr()->get_pixel(x, y, &c.r, &c.g, &c.b);
    return c;
}

// The palette index at (x, y); 0 is the cleared buffer.
int idx(int x, int y)
{
    int index = 0;
    return scr()->get_pixel(x, y, &index);
}

// An untouched pixel is compared as a raw RGB triple, never as a palette
// index: get_pixel's reverse-palette scan answers 0 both for a genuinely
// cleared pixel AND for any colour that is not in the palette at all, so
// `idx(neighbour) == 0` would also accept a break that smeared an
// off-palette blend across the neighbourhood.
constexpr RGB kCleared{0, 0, 0};

// A bright opaque ground to blend against.
void ground_box(int x, int y, int w, int h)
{
    scr()->fastbox(x, y, w, h, kGround, 1);
}
} // namespace


// ---------------------------------------------------------------------------
// video::pointb variants
// ---------------------------------------------------------------------------

TEST(VideoExtra, pointb_inks_exactly_the_named_pixel)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();
    s->pointb(50, 50, kInk);
    EXPECT_EQ(static_cast<int>(kInk), idx(50, 50))
        << "pointb must write the palette colour to exactly that pixel";
    EXPECT_EQ(kCleared, px(51, 50)) << "the pixel to the right must stay cleared";
    EXPECT_EQ(kCleared, px(49, 50)) << "the pixel to the left must stay cleared";
    EXPECT_EQ(kCleared, px(50, 51)) << "the pixel below must stay cleared";
    EXPECT_EQ(kCleared, px(50, 49)) << "the pixel above must stay cleared";
    s->clearbuffer();
}


TEST(VideoExtra, pointb_alpha_blends_toward_the_colour_between_ground_and_opaque)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();
    ground_box(40, 50, 40, 1);
    const RGB ground = px(60, 50);
    ASSERT_EQ(static_cast<int>(kGround), idx(60, 50)) << "ground laid down";

    s->pointb(62, 50, kInk);              // opaque reference on the same ground
    const RGB opaque = px(62, 50);
    ASSERT_NE(ground, opaque) << "the ink must differ from the ground";

    s->pointb(60, 50, kInk, 128);
    const RGB blended = px(60, 50);
    EXPECT_NE(ground, blended) << "a half-alpha plot must change the pixel";
    EXPECT_NE(opaque, blended)
        << "a half-alpha plot must not land on the opaque colour";
    // The PROPORTION, not just "somewhere between": blend_pixel computes
    // dst + (((src - dst) * alpha) >> 8) per channel, so ink 100 (180,96,180)
    // at alpha 128 over ground 7 (224,224,224) is
    //   224 + ((180-224)*128 >> 8) = 202,  224 + ((96-224)*128 >> 8) = 160.
    // A blend that used alpha/2 (a stray `>> 9`) would land on (213,192,213)
    // and still sit strictly between the two endpoints, so the inequalities
    // above cannot see it.
    EXPECT_EQ((RGB{202, 160, 202}), blended)
        << "alpha 128 is exactly dst + ((src-dst)*128 >> 8) per channel";
    s->pointb(68, 50, kInk, 64);
    EXPECT_EQ((RGB{213, 192, 213}), px(68, 50))
        << "and alpha 64 moves exactly a quarter of the way -- two points fix"
           " the blend's slope";

    // The two endpoints are exact: alpha 0 keeps the ground, alpha 255 is the
    // opaque write.
    s->pointb(64, 50, kInk, 255);
    EXPECT_EQ(opaque, px(64, 50)) << "alpha 255 must equal the opaque plot";
    s->pointb(66, 50, kInk, 0);
    EXPECT_EQ(ground, px(66, 50)) << "alpha 0 must leave the ground alone";

    EXPECT_EQ(ground, px(59, 50)) << "neighbours must not be blended";
    EXPECT_EQ(ground, px(61, 50)) << "neighbours must not be blended";
    s->clearbuffer();
}


TEST(VideoExtra, pointb_rgb_writes_that_exact_triple)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();
    s->pointb(70, 50, 200, 100, 50);
    Uint8 r = 0, g = 0, b = 0;
    s->get_pixel(70, 50, &r, &g, &b);
    EXPECT_EQ(200, r) << "the direct-RGB overload writes red verbatim";
    EXPECT_EQ(100, g) << "the direct-RGB overload writes green verbatim";
    EXPECT_EQ(50, b) << "the direct-RGB overload writes blue verbatim";
    EXPECT_EQ(kCleared, px(71, 50)) << "and only that one pixel";
    EXPECT_EQ(kCleared, px(70, 51)) << "and only that one pixel";
    s->clearbuffer();
}


// The pointb(offset, colour) overload's canvas-width decode is pinned by
// VideoEffectsPrims.offset_plot_arithmetic_follows_the_active_canvas_width
// (320- and 384-wide decodes plus a get_pixel(offset) round trip).


// ---------------------------------------------------------------------------
// video::hor_line / ver_line
// ---------------------------------------------------------------------------

TEST(VideoExtra, hor_line_inks_exactly_length_cells_and_both_overloads_agree)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();

    s->hor_line(10, 10, 50, kInk);           // 4-arg: always to the buffer
    EXPECT_EQ(static_cast<int>(kInk), idx(10, 10)) << "the first cell is inked";
    EXPECT_EQ(static_cast<int>(kInk), idx(59, 10))
        << "all 50 cells are inked (x .. x+length-1)";
    EXPECT_EQ(kCleared, px(60, 10)) << "length is exclusive at the far end";
    EXPECT_EQ(kCleared, px(9, 10)) << "nothing is inked before x";
    EXPECT_EQ(kCleared, px(10, 11)) << "a horizontal line must not ink the row below";
    EXPECT_EQ(kCleared, px(10, 9)) << "a horizontal line must not ink the row above";

    // The 5-arg form with tobuffer=1 draws the same run, and tobuffer=0
    // forwards to the 4-arg form (video_sdl.cpp hor_line).
    s->hor_line(10, 20, 50, kInk, 1);
    s->hor_line(10, 30, 50, kInk, 0);
    for (int x = 8; x < 62; x++)
    {
        EXPECT_EQ(idx(x, 10), idx(x, 20))
            << "the tobuffer=1 overload must draw the same run at x=" << x;
        EXPECT_EQ(idx(x, 10), idx(x, 30))
            << "the tobuffer=0 overload must forward to it at x=" << x;
    }
    s->clearbuffer();
}


TEST(VideoExtra, hor_line_alpha_blends_every_cell_at_the_given_alpha)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();
    ground_box(5, 30, 80, 2);
    const RGB ground = px(10, 30);

    s->pointb(70, 30, kInk);                 // opaque reference
    const RGB opaque = px(70, 30);
    s->pointb(72, 30, kInk, 128);            // single-pixel blend reference
    const RGB blended = px(72, 30);
    ASSERT_NE(opaque, blended) << "control: alpha 128 differs from opaque";
    ASSERT_NE(ground, blended) << "control: alpha 128 differs from the ground";

    s->hor_line_alpha(10, 30, 50, kInk, 128);
    EXPECT_EQ(blended, px(10, 30))
        << "the first cell must be the alpha blend, not an opaque write";
    EXPECT_EQ(blended, px(30, 30)) << "and so must the middle";
    EXPECT_EQ(blended, px(59, 30)) << "and so must the last cell";
    EXPECT_EQ(ground, px(60, 30)) << "length is exclusive at the far end";
    EXPECT_EQ(ground, px(9, 30)) << "nothing before x is blended";
    EXPECT_EQ(ground, px(10, 31)) << "the row below must be untouched";
    s->clearbuffer();
}


TEST(VideoExtra, ver_line_inks_exactly_length_cells_downward_and_both_overloads_agree)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();

    s->ver_line(10, 10, 50, kInk);
    EXPECT_EQ(static_cast<int>(kInk), idx(10, 10)) << "the first cell is inked";
    EXPECT_EQ(static_cast<int>(kInk), idx(10, 59))
        << "all 50 cells run downward (y .. y+length-1)";
    EXPECT_EQ(kCleared, px(10, 60)) << "length is exclusive at the bottom";
    EXPECT_EQ(kCleared, px(10, 9)) << "nothing above y is inked";
    EXPECT_EQ(kCleared, px(11, 10))
        << "a vertical line must not run across the row (transposed blit)";
    EXPECT_EQ(kCleared, px(59, 10))
        << "a vertical line must not run across the row (transposed blit)";

    s->ver_line(20, 10, 50, kInk, 1);
    s->ver_line(30, 10, 50, kInk, 0);
    for (int y = 8; y < 62; y++)
    {
        EXPECT_EQ(idx(10, y), idx(20, y))
            << "the tobuffer=1 overload must draw the same run at y=" << y;
        EXPECT_EQ(idx(10, y), idx(30, y))
            << "the tobuffer=0 overload must forward to it at y=" << y;
    }
    s->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::putdata variants
// ---------------------------------------------------------------------------

TEST(VideoExtra, putdata_span_copies_every_non_zero_byte_and_skips_index_zero)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[16*16];
    memset(testbmp, 50, sizeof(testbmp));
    testbmp[0] = 0;                       // index 0 is transparent
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    ground_box(48, 48, 24, 24);
    const RGB ground = px(50, 50);
    s->putdata(50, 50, 16, 16, span);

    EXPECT_EQ(ground, px(50, 50)) << "source index 0 must be left transparent";
    EXPECT_EQ(50, idx(51, 50)) << "the source byte is written verbatim";
    EXPECT_EQ(50, idx(65, 65)) << "the whole 16x16 rect is written";
    EXPECT_EQ(ground, px(66, 65)) << "and nothing past the rect's right edge";
    EXPECT_EQ(ground, px(50, 66)) << "and nothing past the rect's bottom edge";
    s->clearbuffer();
}


TEST(VideoExtra, putdata_alpha_blends_non_zero_bytes_and_skips_index_zero)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[16*16];
    memset(testbmp, 80, sizeof(testbmp));
    testbmp[0] = 0;
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    ground_box(68, 48, 40, 20);
    const RGB ground = px(70, 50);
    s->pointb(100, 50, 80);
    const RGB opaque = px(100, 50);
    s->pointb(102, 50, 80, 128);
    const RGB blended = px(102, 50);
    ASSERT_NE(opaque, blended) << "control: alpha 128 differs from opaque";
    ASSERT_NE(ground, blended) << "control: alpha 128 differs from the ground";

    s->putdata_alpha(70, 50, 16, 16, span, 128);
    EXPECT_EQ(ground, px(70, 50)) << "source index 0 must be left transparent";
    EXPECT_EQ(blended, px(71, 50))
        << "non-zero bytes must blend at the given alpha, not overwrite";
    EXPECT_EQ(blended, px(85, 65)) << "to the last pixel of the rect";
    EXPECT_EQ(ground, px(86, 65)) << "and no further";
    s->clearbuffer();
}


TEST(VideoExtra, putdatatext_fills_non_zero_bytes_through_the_palette_lut)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[8*8];
    memset(testbmp, 60, sizeof(testbmp));
    testbmp[0] = 0;
    const auto span = std::span<const unsigned char>(testbmp, 64);

    s->clearbuffer();
    ground_box(88, 48, 14, 14);
    const RGB ground = px(90, 50);
    s->putdatatext(90, 50, 8, 8, span);

    EXPECT_EQ(ground, px(90, 50)) << "source index 0 must be left transparent";
    EXPECT_EQ(60, idx(91, 50)) << "the glyph byte is filled in its palette colour";
    EXPECT_EQ(60, idx(97, 57)) << "the whole 8x8 glyph is filled";
    EXPECT_EQ(ground, px(98, 50)) << "and nothing past the glyph's right edge";
    EXPECT_EQ(ground, px(90, 58)) << "and nothing past the glyph's bottom edge";
    s->clearbuffer();
}


TEST(VideoExtra, putdata_color_replaces_bytes_over_247_with_the_team_colour_itself)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[16*16];
    memset(testbmp, 248, sizeof(testbmp)); // > 247 triggers team color
    testbmp[0] = 0;                        // transparent
    testbmp[1] = 50;                       // <= 247: passes through unchanged
    testbmp[2] = 250;                      // no ramp: still exactly the colour
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    ground_box(48, 68, 24, 24);
    const RGB ground = px(50, 70);
    s->putdata(50, 70, 16, 16, span, kInk);

    EXPECT_EQ(ground, px(50, 70)) << "source index 0 must be left transparent";
    EXPECT_EQ(50, idx(51, 70)) << "bytes <= 247 must pass through unremapped";
    EXPECT_EQ(static_cast<int>(kInk), idx(52, 70))
        << "byte 250 becomes the team colour itself, with no ramp";
    EXPECT_EQ(static_cast<int>(kInk), idx(53, 70)) << "and so does byte 248";
    EXPECT_EQ(static_cast<int>(kInk), idx(65, 85)) << "to the last pixel of the rect";
    EXPECT_EQ(ground, px(66, 85)) << "and no further";
    s->clearbuffer();
}


TEST(VideoExtra, putdatatext_color_replaces_bytes_over_247_with_the_team_colour_itself)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[8*8];
    memset(testbmp, 248, sizeof(testbmp));
    testbmp[0] = 0;
    testbmp[1] = 50;
    testbmp[2] = 250;
    const auto span = std::span<const unsigned char>(testbmp, 64);

    s->clearbuffer();
    ground_box(68, 68, 14, 14);
    const RGB ground = px(70, 70);
    s->putdatatext(70, 70, 8, 8, span, kInk);

    EXPECT_EQ(ground, px(70, 70)) << "source index 0 must be left transparent";
    EXPECT_EQ(50, idx(71, 70)) << "bytes <= 247 must pass through unremapped";
    EXPECT_EQ(static_cast<int>(kInk), idx(72, 70))
        << "byte 250 becomes the team colour itself, with no ramp";
    EXPECT_EQ(static_cast<int>(kInk), idx(73, 70)) << "and so does byte 248";
    EXPECT_EQ(static_cast<int>(kInk), idx(77, 77)) << "to the last pixel of the glyph";
    EXPECT_EQ(ground, px(78, 70)) << "and no further";
    s->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::fastbox_outline
// ---------------------------------------------------------------------------

TEST(VideoExtra, fastbox_outline_draws_the_border_and_leaves_the_interior)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();
    s->fastbox_outline(10, 10, 40, 30, kInk);

    EXPECT_EQ(static_cast<int>(kInk), idx(10, 10)) << "top-left corner";
    EXPECT_EQ(static_cast<int>(kInk), idx(50, 10)) << "top-right corner (x+w)";
    EXPECT_EQ(static_cast<int>(kInk), idx(10, 40)) << "bottom-left corner (y+h)";
    EXPECT_EQ(static_cast<int>(kInk), idx(50, 40)) << "bottom-right corner";
    EXPECT_EQ(static_cast<int>(kInk), idx(30, 10)) << "top edge";
    EXPECT_EQ(static_cast<int>(kInk), idx(30, 40)) << "bottom edge";
    EXPECT_EQ(static_cast<int>(kInk), idx(10, 25)) << "left edge";
    EXPECT_EQ(static_cast<int>(kInk), idx(50, 25)) << "right edge";
    EXPECT_EQ(kCleared, px(30, 25)) << "the interior must stay untouched (outline, not fill)";
    EXPECT_EQ(kCleared, px(11, 11)) << "the interior must stay untouched (outline, not fill)";
    EXPECT_EQ(kCleared, px(51, 10)) << "nothing past the right edge";
    EXPECT_EQ(kCleared, px(10, 41)) << "nothing past the bottom edge";
    s->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::point (screen buffer version)
// ---------------------------------------------------------------------------

TEST(VideoExtra, point_forwards_to_the_buffer_plot)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();
    s->point(50, 50, kInk);
    EXPECT_EQ(static_cast<int>(kInk), idx(50, 50))
        << "point() must reach the buffer through pointb()";
    EXPECT_EQ(kCleared, px(51, 50)) << "and touch nothing else";
    EXPECT_EQ(kCleared, px(50, 51)) << "and touch nothing else";
    s->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::putbuffer / putbuffer_alpha
// ---------------------------------------------------------------------------

TEST(VideoExtra, putbuffer_copies_the_tile_opaquely_including_index_zero)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[16*16];
    memset(testbmp, kInk, sizeof(testbmp));
    testbmp[0] = 0;                       // tiles are opaque: 0 is a colour
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    ground_box(48, 48, 24, 24);
    const RGB ground = px(50, 50);
    s->putbuffer(50, 50, 16, 16, kPortX0, kPortY0, kPortX1, kPortY1, span);

    EXPECT_EQ(kCleared, px(50, 50))
        << "tiles copy opaquely: source index 0 overwrites the ground";
    EXPECT_EQ(static_cast<int>(kInk), idx(51, 50)) << "the tile byte is copied verbatim";
    EXPECT_EQ(static_cast<int>(kInk), idx(65, 65)) << "the whole 16x16 tile is copied";
    EXPECT_EQ(ground, px(66, 65)) << "and nothing past the tile's right edge";
    EXPECT_EQ(ground, px(50, 66)) << "and nothing past the tile's bottom edge";
    s->clearbuffer();
}


TEST(VideoExtra, putbuffer_alpha_blends_the_tile_at_the_given_alpha)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[16*16];
    memset(testbmp, kInk, sizeof(testbmp));
    testbmp[0] = 0;
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    ground_box(40, 48, 80, 24);
    const RGB ground = px(50, 50);
    s->pointb(100, 50, kInk);
    const RGB opaque = px(100, 50);
    s->pointb(102, 50, kInk, 128);
    const RGB blended = px(102, 50);
    s->pointb(104, 50, 0, 128);
    const RGB blended_black = px(104, 50);
    ASSERT_NE(opaque, blended) << "control: alpha 128 differs from opaque";
    ASSERT_NE(ground, blended) << "control: alpha 128 differs from the ground";

    s->putbuffer_alpha(50, 50, 16, 16, kPortX0, kPortY0, kPortX1, kPortY1,
                       span, 128);
    EXPECT_EQ(blended_black, px(50, 50))
        << "tiles have no transparent index: byte 0 blends like any other";
    EXPECT_EQ(blended, px(51, 50))
        << "each tile byte must blend at the given alpha, not overwrite";
    EXPECT_EQ(blended, px(65, 65)) << "to the last pixel of the tile";
    EXPECT_EQ(ground, px(66, 65)) << "and no further";
    s->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::draw_rect_filled (alpha-proportional fill)
// ---------------------------------------------------------------------------

TEST(VideoExtra, draw_rect_filled_blends_the_whole_rect_at_the_alpha_it_was_given)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    constexpr unsigned char kFill = 150;
    s->clearbuffer();
    ground_box(8, 8, 120, 26);
    const RGB ground = px(15, 15);

    s->pointb(100, 15, kFill);
    const RGB opaque = px(100, 15);
    s->pointb(102, 15, kFill, 64);
    const RGB quarter = px(102, 15);
    s->pointb(104, 15, kFill, 192);
    const RGB three_quarter = px(104, 15);
    ASSERT_NE(ground, quarter) << "control: a quarter blend changes the pixel";
    ASSERT_NE(opaque, quarter) << "control: a quarter blend is not opaque";
    ASSERT_NE(quarter, three_quarter)
        << "control: the two alphas must land on different colours";

    s->draw_rect_filled(10, 10, 30, 20, kFill, 64);
    EXPECT_EQ(quarter, px(10, 10)) << "first pixel of the rect";
    EXPECT_EQ(quarter, px(15, 15))
        << "a quarter-alpha fill must be the quarter-alpha blend";
    EXPECT_EQ(quarter, px(39, 29)) << "last pixel of the rect (x+w-1, y+h-1)";
    EXPECT_EQ(ground, px(40, 29)) << "w is exclusive";
    EXPECT_EQ(ground, px(10, 30)) << "h is exclusive";

    // The same rect at a higher alpha lands on the higher-alpha blend, so the
    // alpha argument really reaches the fill instead of being hardcoded.
    s->draw_rect_filled(50, 10, 30, 20, kFill, 192);
    EXPECT_EQ(three_quarter, px(55, 15))
        << "a three-quarter-alpha fill must be the three-quarter blend";
    EXPECT_NE(px(15, 15), px(55, 15))
        << "the two fills must not land on the same colour";
    s->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::walkputbuffer clipping edge cases
// ---------------------------------------------------------------------------

namespace
{
// A 16x16 sprite whose left half is one colour and right half another, so a
// clip that draws the wrong source columns (or shifts them) is visible.
constexpr unsigned char kLeftHalf = 60;
constexpr unsigned char kRightHalf = 100;

void fill_half_and_half(unsigned char (&bmp)[16*16])
{
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 16; col++)
            bmp[row * 16 + col] =
                (col < 8) ? kLeftHalf : kRightHalf;
}

// The same trick rotated: the top eight rows are one colour, the bottom eight
// another, so a top clip that draws the wrong source rows (or the whole sprite
// pushed down the port) is visible.
constexpr unsigned char kTopHalf = kLeftHalf;
constexpr unsigned char kBottomHalf = kRightHalf;

void fill_top_and_bottom(unsigned char (&bmp)[16*16])
{
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 16; col++)
            bmp[row * 16 + col] =
                (row < 8) ? kTopHalf : kBottomHalf;
}
} // namespace

TEST(VideoExtra, walkputbuffer_left_clip_drops_the_hidden_columns_without_shifting)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[16*16];
    fill_half_and_half(testbmp);
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    // xmin = portstartx - walkerstartx = 8: source columns 8..15 draw at 0..7.
    s->walkputbuffer(-8, 50, 16, 16, kPortX0, kPortY0, kPortX1, kPortY1, span, 40);

    EXPECT_EQ(static_cast<int>(kRightHalf), idx(0, 50))
        << "the sprite's source column 8 lands on the port's left edge";
    EXPECT_EQ(static_cast<int>(kRightHalf), idx(7, 50))
        << "the visible half is exactly 8 columns wide";
    EXPECT_EQ(kCleared, px(8, 50)) << "the clipped columns must not shift into view";
    EXPECT_EQ(static_cast<int>(kRightHalf), idx(7, 65))
        << "every row of the visible half draws";
    EXPECT_EQ(kCleared, px(7, 66)) << "and no row past the sprite's last";
    for (int x = 0; x < 20; x++)
        EXPECT_NE(static_cast<int>(kLeftHalf), idx(x, 50))
            << "the clipped-off source columns must not be drawn, at x=" << x;
    s->clearbuffer();
}


TEST(VideoExtra, walkputbuffer_right_clip_stops_at_the_exclusive_port_edge)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[16*16];
    fill_half_and_half(testbmp);
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    // xmax = portendx - walkerstartx = 9: source columns 0..8 draw at 310..318.
    s->walkputbuffer(310, 50, 16, 16, kPortX0, kPortY0, kPortX1, kPortY1, span, 40);

    EXPECT_EQ(static_cast<int>(kLeftHalf), idx(310, 50))
        << "the sprite starts at its own x";
    EXPECT_EQ(static_cast<int>(kLeftHalf), idx(317, 50))
        << "the sprite's left half draws in full";
    EXPECT_EQ(static_cast<int>(kRightHalf), idx(318, 50))
        << "source column 8 lands on the last column inside the port";
    EXPECT_EQ(kCleared, px(319, 50)) << "portendx is exclusive: column 319 is untouched";
    EXPECT_EQ(static_cast<int>(kRightHalf), idx(318, 65))
        << "every row draws the same clipped width";
    EXPECT_EQ(kCleared, px(0, 51))
        << "the clipped columns must not wrap onto the next row";
    EXPECT_EQ(kCleared, px(0, 50)) << "nothing is drawn at the far left";
    s->clearbuffer();
}


// A sprite that hangs off the TOP of the port loses its first rows; the rows
// that remain must land on the port edge, not be pushed down it (the sprite
// would swim as it walked over the top edge of a split-screen pane).
TEST(VideoExtra, walkputbuffer_top_clip_draws_the_lower_source_rows_at_the_port_edge)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    unsigned char testbmp[16*16];
    fill_top_and_bottom(testbmp);
    const auto span = std::span<const unsigned char>(testbmp, 256);

    // Control: unclipped, row 0 is the top half and row 8 the bottom half, so
    // both colours are ones this blitter really can draw.
    s->clearbuffer();
    s->walkputbuffer(100, 50, 16, 16, kPortX0, kPortY0, kPortX1, kPortY1, span, 40);
    ASSERT_EQ(static_cast<int>(kTopHalf), idx(100, 50))
        << "control: the unclipped sprite draws its top half first";
    ASSERT_EQ(static_cast<int>(kBottomHalf), idx(100, 58))
        << "control: source row 8 is the bottom half";

    s->clearbuffer();
    // ymin = portstarty - walkerstarty = 8: source rows 8..15 draw at y 0..7.
    s->walkputbuffer(50, -8, 16, 16, kPortX0, kPortY0, kPortX1, kPortY1, span, 40);

    EXPECT_EQ(static_cast<int>(kBottomHalf), idx(50, 0))
        << "the sprite's source row 8 lands on the port's top edge";
    EXPECT_EQ(static_cast<int>(kBottomHalf), idx(50, 7))
        << "the visible half is exactly 8 rows tall";
    EXPECT_EQ(kCleared, px(50, 8))
        << "the clipped rows must not shift the sprite down the port";
    EXPECT_EQ(static_cast<int>(kBottomHalf), idx(65, 7))
        << "every column of the visible rows draws";
    EXPECT_EQ(kCleared, px(49, 0)) << "nothing is drawn left of the sprite's x";
    EXPECT_EQ(kCleared, px(66, 0)) << "nothing is drawn past the sprite's 16th column";
    for (int y = 0; y < 20; y++)
        EXPECT_NE(static_cast<int>(kTopHalf), idx(50, y))
            << "the clipped-off source rows must not be drawn, at y=" << y;
    s->clearbuffer();
}


// A sprite that hangs off the bottom of a viewport must be cut at the port
// edge, not drawn over the pane below it (the classic split-screen bug: the
// lower seat's view gains a stripe of the upper seat's sprites). The clip
// belongs to the port rectangle, so the SAME sprite at the SAME place draws
// its remaining rows once the port extends far enough to hold them.
TEST(VideoExtra, video_walkputbuffer_clips_at_the_port_bottom_edge)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    constexpr int kSpriteColor = 47;
    constexpr int kSpriteX = 50;
    constexpr int kSpriteY = 112;
    constexpr int kPortBottom = 120;
    unsigned char testbmp[16*16];
    memset(testbmp, kSpriteColor, sizeof(testbmp));
    const auto span = std::span<const unsigned char>(testbmp, 256);

    scr->clearbuffer();
    scr->walkputbuffer(kSpriteX, kSpriteY, 16, 16, 0, 0, 319, kPortBottom,
                       span, 40, NORMAL_MODE, 0, 0, 0);
    int index = 0;
    EXPECT_EQ(kSpriteColor, scr->get_pixel(kSpriteX, kSpriteY, &index))
        << "the sprite's first row is inside the port and must be drawn";
    EXPECT_EQ(kSpriteColor,
              scr->get_pixel(kSpriteX, kPortBottom - 1, &index))
        << "the last row inside the port must be drawn";
    EXPECT_EQ(0, scr->get_pixel(kSpriteX, kPortBottom, &index))
        << "the first row past the port edge must be untouched";
    EXPECT_EQ(0, scr->get_pixel(kSpriteX, kSpriteY + 15, &index))
        << "and so must the sprite's own last row";

    // Same sprite, taller port: the rows the clip removed come back, so the
    // zeros above are the clip and not a sprite that never drew.
    scr->clearbuffer();
    scr->walkputbuffer(kSpriteX, kSpriteY, 16, 16, 0, 0, 319, 199,
                       span, 40, NORMAL_MODE, 0, 0, 0);
    EXPECT_EQ(kSpriteColor, scr->get_pixel(kSpriteX, kPortBottom, &index))
        << "with the port extended to 199, the row the clip removed draws";
    EXPECT_EQ(kSpriteColor, scr->get_pixel(kSpriteX, kSpriteY + 15, &index))
        << "and so does the sprite's own last row: the zeros above were the"
           " port clip, not a sprite that never drew";
    scr->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::walkputbuffertext
// ---------------------------------------------------------------------------

TEST(VideoExtra, walkputbuffertext_fills_non_zero_bytes_and_recolours_bytes_over_247)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    constexpr unsigned char kTeam = 40;
    unsigned char testbmp[16*16];
    memset(testbmp, kInk, sizeof(testbmp));
    testbmp[0] = 0;                    // index 0 is transparent
    testbmp[2 * 16 + 2] = 248;         // > 247 -> teamcolor + (255 - byte) = 47
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    ground_box(48, 48, 24, 24);
    const RGB ground = px(50, 50);
    s->walkputbuffertext(50, 50, 16, 16, kPortX0, kPortY0, kPortX1, kPortY1,
                         span, kTeam);

    EXPECT_EQ(ground, px(50, 50)) << "source index 0 must be left transparent";
    EXPECT_EQ(static_cast<int>(kInk), idx(51, 50))
        << "the source byte is filled in its own palette colour";
    EXPECT_EQ(static_cast<int>(kInk), idx(65, 65))
        << "the whole 16x16 rect is filled";
    EXPECT_EQ(47, idx(52, 52))
        << "a byte over 247 is recoloured to teamcolor + (255 - byte)";
    EXPECT_EQ(ground, px(66, 65)) << "and nothing past the rect's right edge";
    EXPECT_EQ(ground, px(50, 66)) << "and nothing past the rect's bottom edge";
    s->clearbuffer();
}


TEST(VideoExtra, walkputbuffertext_alpha_blends_the_source_shape_at_the_given_alpha)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    // kTeam fills the block, so both arms of the text ink rule (ink >247 ->
    // teamcolor, a literal byte -> itself) land on the same index here; the
    // pins below are about SHAPE and ALPHA.
    constexpr unsigned char kTeam = 40;
    unsigned char testbmp[16*16];
    memset(testbmp, kTeam, sizeof(testbmp));
    testbmp[0] = 0;                       // index 0 is transparent
    for (int row = 0; row < 16; row++)
        testbmp[row * 16 + 4] = 0;        // a transparent column through it
    const auto span = std::span<const unsigned char>(testbmp, 256);

    s->clearbuffer();
    ground_box(48, 48, 60, 60);
    const RGB ground = px(50, 50);
    s->pointb(100, 50, kTeam);
    const RGB opaque = px(100, 50);
    s->pointb(102, 50, kTeam, 128);
    const RGB half = px(102, 50);
    s->pointb(104, 50, kTeam, 64);
    const RGB quarter = px(104, 50);
    ASSERT_NE(ground, half) << "control: a half blend changes the ground";
    ASSERT_NE(opaque, half) << "control: a half blend is not the opaque write";
    ASSERT_NE(half, quarter) << "control: the two alphas differ";

    s->walkputbuffertext_alpha(50, 50, 16, 16, kPortX0, kPortY0, kPortX1,
                               kPortY1, span, kTeam, 128);
    EXPECT_EQ(ground, px(50, 50)) << "source index 0 must be left transparent";
    EXPECT_EQ(half, px(51, 50))
        << "non-zero bytes must blend at the given alpha, not overwrite";
    EXPECT_EQ(half, px(65, 65)) << "to the last pixel of the rect";
    EXPECT_EQ(ground, px(54, 50))
        << "the transparent source column must stay unblended";
    EXPECT_EQ(ground, px(54, 65)) << "for every row of the sprite";
    EXPECT_EQ(ground, px(66, 65)) << "and nothing past the rect's right edge";
    EXPECT_EQ(ground, px(50, 66)) << "and nothing past the rect's bottom edge";

    // The alpha argument reaches the blend instead of being hardcoded.
    s->walkputbuffertext_alpha(50, 70, 16, 16, kPortX0, kPortY0, kPortX1,
                               kPortY1, span, kTeam, 64);
    EXPECT_EQ(quarter, px(51, 70)) << "a quarter-alpha blit is the quarter blend";
    EXPECT_NE(px(51, 50), px(51, 70))
        << "the two alphas must not land on the same colour";
    s->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::clearbuffer with rect
// ---------------------------------------------------------------------------

TEST(VideoExtra, clearbuffer_rect_zeroes_only_that_rectangle)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    constexpr unsigned char kPaint = 50;
    s->clearbuffer();
    s->fastbox(0, 0, 320, 200, kPaint, 1);
    ASSERT_EQ(static_cast<int>(kPaint), idx(10, 10))
        << "control: the whole canvas is painted before the clear";
    ASSERT_EQ(static_cast<int>(kPaint), idx(110, 110)) << "control: painted";

    s->clearbuffer(10, 10, 100, 100);
    EXPECT_EQ(kCleared, px(10, 10)) << "the rect's first pixel is cleared";
    EXPECT_EQ(kCleared, px(109, 109))
        << "the rect's last pixel (x+w-1, y+h-1) is cleared";
    EXPECT_EQ(static_cast<int>(kPaint), idx(9, 10))
        << "the column left of the rect must survive";
    EXPECT_EQ(static_cast<int>(kPaint), idx(10, 9))
        << "the row above the rect must survive";
    EXPECT_EQ(static_cast<int>(kPaint), idx(110, 109))
        << "w is exclusive: the column right of the rect must survive";
    EXPECT_EQ(static_cast<int>(kPaint), idx(109, 110))
        << "h is exclusive: the row below the rect must survive";
    EXPECT_EQ(static_cast<int>(kPaint), idx(319, 199))
        << "clearing a rect must not clear the whole canvas";
    s->clearbuffer();
}


// ---------------------------------------------------------------------------
// video::draw_text_bar (already tested but exercise more)
// ---------------------------------------------------------------------------

TEST(VideoExtra, draw_text_bar_fills_face_12_with_its_indented_border_at_full_width)
{
    screen* const s = scr();
    ASSERT_NE(nullptr, s);
    s->clearbuffer();
    // Reference plots for the four faces draw_text_bar uses. The greys are not
    // unique in our.pal, so compare true colours rather than palette indices.
    s->pointb(0, 190, 12);
    const RGB face = px(0, 190);
    s->pointb(2, 190, 10);
    const RGB top_edge = px(2, 190);
    s->pointb(4, 190, 15);
    const RGB bottom_edge = px(4, 190);
    s->pointb(6, 190, 11);
    const RGB left_edge = px(6, 190);
    ASSERT_NE(face, top_edge) << "control: the faces must be distinguishable";
    ASSERT_NE(face, bottom_edge) << "control: the faces must be distinguishable";
    ASSERT_NE(face, left_edge) << "control: the faces must be distinguishable";
    ASSERT_NE(top_edge, bottom_edge)
        << "control: the light and dark edges must differ";

    s->draw_text_bar(0, 0, 320, 10);

    EXPECT_EQ(face, px(160, 5)) << "the bar's interior is the grey face 12";
    EXPECT_EQ(face, px(319, 5))
        << "a full-width bar fills to the last canvas column";
    EXPECT_EQ(top_edge, px(160, 0)) << "the top edge is the light face 10";
    EXPECT_EQ(bottom_edge, px(160, 10)) << "the bottom edge is the dark face 15";
    EXPECT_EQ(left_edge, px(0, 5)) << "the left edge is face 11";
    EXPECT_EQ(RGB{}, px(160, 11))
        << "y2 is the bar's last row: the row below stays cleared";
    EXPECT_EQ(RGB{}, px(160, 180))
        << "and the bar must not fill the rest of the canvas";
    s->clearbuffer();
}

