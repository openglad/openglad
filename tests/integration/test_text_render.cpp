#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <gtest/gtest.h>

#include <cstddef>

// myscreen is now a macro defined in base.h (via game_session.h)

// ---------------------------------------------------------------------------
// text::query_width
// ---------------------------------------------------------------------------

TEST(TextRender, text_query_width_empty)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_normal.query_width("");
    ASSERT_EQ(0, (int)w) << "empty string width is 0";
}


TEST(TextRender, text_query_width_single)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_normal.query_width("A");
    ASSERT_TRUE(w > 0) << "single char has width > 0";
}


TEST(TextRender, text_query_width_long)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_normal.query_width("Hello World");
    ASSERT_TRUE(w > 0) << "long string has positive width";

    Sint32 w2 = og::runtime::current_session->myscreen_->text_normal.query_width("Hi");
    ASSERT_TRUE(w > w2) << "longer string is wider";
}


TEST(TextRender, text_query_width_big)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_big.query_width("Test");
    ASSERT_TRUE(w > 0) << "big font width > 0";
}


// ---------------------------------------------------------------------------
// text::write_xy variations (all to buffer)
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_xy_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 10, "Buffer text", (short)1);
}


TEST(TextRender, text_write_xy_color_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 20, "Colored buffer", (unsigned char)WHITE, (short)1);
}


TEST(TextRender, text_write_xy_flat_recolors_every_opaque_glyph_pixel)
{
    screen* const output = og::runtime::current_session->myscreen_;
    text& font = output->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());

    constexpr Sint32 x = 10;
    constexpr Sint32 y = 40;
    constexpr unsigned char background = 13;
    constexpr unsigned char ink = 64;
    output->fastbox(x, y, font.sizex, font.sizey, background);
    ASSERT_EQ(1, font.write_xy_flat(x, y, "M", ink, 1));

    const std::size_t stride = static_cast<std::size_t>(font.sizex) *
                               static_cast<std::size_t>(font.sizey);
    const unsigned char* const glyph =
        font.letters->data.get() + static_cast<std::size_t>('M') * stride;
    for (Sint32 row = 0; row < font.sizey; ++row) {
        for (Sint32 col = 0; col < font.sizex; ++col) {
            int actual = -1;
            output->get_pixel(x + col, y + row, &actual);
            const unsigned char source =
                glyph[static_cast<std::size_t>(row * font.sizex + col)];
            EXPECT_EQ(source == 0 ? background : ink, actual)
                << "glyph pixel " << col << "," << row;
        }
    }
}


TEST(TextRender, text_write_xy_no_color)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 30, "No color text");
}


// ---------------------------------------------------------------------------
// text::write_y variations
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_y_basic)
{
    og::runtime::current_session->myscreen_->text_normal.write_y(50, "Y text");
}


TEST(TextRender, text_write_y_color)
{
    og::runtime::current_session->myscreen_->text_normal.write_y(60, "Y colored", (unsigned char)RED);
}


TEST(TextRender, text_write_y_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_y(70, "Y buffer", (short)1);
}


TEST(TextRender, text_write_y_color_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_y(80, "Y color buf", (unsigned char)DARK_GREEN, (short)1);
}


// ---------------------------------------------------------------------------
// text::write_xy_center variations
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_xy_center_alpha)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_center_alpha(160, 100, WHITE, 128, "Alpha center");
}


TEST(TextRender, text_write_xy_center_shadow)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_center_shadow(160, 110, WHITE, "Center shadow");
}


// ---------------------------------------------------------------------------
// text::write_char_xy variations
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_char_xy_basic)
{
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(10, 120, 'A');
}


TEST(TextRender, text_write_char_xy_color)
{
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(20, 120, 'B', (unsigned char)RED);
}


TEST(TextRender, text_write_char_xy_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(30, 120, 'C', (short)1);
}


TEST(TextRender, text_write_char_xy_color_to_buffer)
{
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(40, 120, 'D', (unsigned char)DARK_BLUE, (short)1);
}


// ---------------------------------------------------------------------------
// text::write_y with viewscreen
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_y_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_y(50, "VS Y text", vs);
}


TEST(TextRender, text_write_y_color_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_y(60, "VS Y color", (unsigned char)RED, vs);
}


// ---------------------------------------------------------------------------
// text::write_xy with viewscreen
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_xy_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 50, "VS text", vs);
}


TEST(TextRender, text_write_xy_color_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_xy(10, 60, "VS color", (unsigned char)WHITE, vs);
}


// ---------------------------------------------------------------------------
// text::write_char_xy with viewscreen
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_char_xy_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(10, 70, 'X', vs);
}


TEST(TextRender, text_write_char_xy_color_viewscreen)
{
    viewscreen* vs = og::runtime::current_session->myscreen_->viewob[0].get();
    if (!vs) return;
    og::runtime::current_session->myscreen_->text_normal.write_char_xy(20, 70, 'Y', (unsigned char)DARK_GREEN, vs);
}


// ---------------------------------------------------------------------------
// text::write_xy_shadow
// ---------------------------------------------------------------------------

TEST(TextRender, text_write_xy_shadow_color)
{
    og::runtime::current_session->myscreen_->text_normal.write_xy_shadow(50, 50, RED, "Red shadow");
}


// ---------------------------------------------------------------------------
// big text
// ---------------------------------------------------------------------------

TEST(TextRender, text_big_write_xy_color)
{
    og::runtime::current_session->myscreen_->text_big.write_xy(10, 150, "Big colored", (unsigned char)WHITE);
}


TEST(TextRender, text_big_write_y)
{
    og::runtime::current_session->myscreen_->text_big.write_y(160, "Big centered");
}

namespace
{
// How a write arm turns a font byte into a canvas pixel. Transparent source
// bytes (0) always leave the background and literal palette bytes always keep
// themselves; the three arms differ only in what they do with the font's
// recolourable ink range (>247):
enum class GlyphInk
{
    Recolored,  // putdatatext(..., color): the caller's colour exactly
    Raw,        // putdatatext(...) with no colour: the font byte itself
    TeamShifted // walkputbuffertext: colour + (255 - source), the team ramp
};

void expect_glyph_at(screen* out, text& font, Sint32 x, Sint32 y, char letter,
                     unsigned char ink, int background, GlyphInk mode,
                     const char* what)
{
    const std::size_t stride = static_cast<std::size_t>(font.sizex) *
                               static_cast<std::size_t>(font.sizey);
    const unsigned char* const glyph =
        font.letters->data.get() +
        static_cast<std::size_t>(static_cast<unsigned char>(letter)) * stride;
    int lit = 0;
    for (Sint32 row = 0; row < font.sizey; ++row)
        for (Sint32 col = 0; col < font.sizex; ++col)
        {
            const unsigned char source =
                glyph[static_cast<std::size_t>(row * font.sizex + col)];
            int expected = background;
            if (source != 0)
            {
                if (source <= 247)
                    expected = static_cast<int>(source);
                else if (mode == GlyphInk::Recolored)
                    expected = static_cast<int>(ink);
                else if (mode == GlyphInk::Raw)
                    expected = static_cast<int>(source);
                else
                    expected = static_cast<int>(
                        static_cast<unsigned char>(ink + (255 - source)));
            }
            int actual = -1;
            out->get_pixel(x + col, y + row, &actual);
            ASSERT_EQ(expected, actual)
                << what << ": glyph '" << letter << "' pixel " << col << ","
                << row;
            if (source != 0)
                lit++;
        }
    ASSERT_GT(lit, 0) << what << ": '" << letter << "' has no ink at all";
}
} // namespace

// The write arms that go straight to the canvas (no viewscreen, no buffer
// flag) must land at the coordinates the caller passed, and advance one
// character width per character. Everything that positions text against a
// fixed layout — HUD labels, the level name, menu captions — depends on both.
TEST(TextRender, direct_canvas_write_arms_land_at_absolute_coordinates)
{
    screen* const out = og::runtime::current_session->myscreen_;
    text& font = out->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());
    ASSERT_LT(font.sizex, 9) << "text_normal is the small monospaced font";

    constexpr Sint32 x = 24;
    constexpr Sint32 y = 40;
    constexpr int background = 13;
    constexpr unsigned char ink = 64;
    const Sint32 advance = font.sizex + 1;

    out->fastbox(x - 2, y - 2, advance * 4 + 4, font.sizey + 4,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(1, font.write_xy(x, y, "AB", ink));
    expect_glyph_at(out, font, x, y, 'A', ink, background,
                    GlyphInk::Recolored, "write_xy first");
    expect_glyph_at(out, font, x + advance, y, 'B', ink, background,
                    GlyphInk::Recolored, "write_xy second");

    // The single-character arms, with and without an explicit colour.
    const Sint32 y2 = y + font.sizey + 6;
    out->fastbox(x - 2, y2 - 2, advance * 4 + 4, font.sizey + 4,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(1, font.write_char_xy(x, y2, 'C', ink));
    expect_glyph_at(out, font, x, y2, 'C', ink, background,
                    GlyphInk::Recolored, "write_char_xy");
    EXPECT_EQ(1, font.write_char_xy(x + 2 * advance, y2, 'D'));
    expect_glyph_at(out, font, x + 2 * advance, y2, 'D', 0, background,
                    GlyphInk::Raw, "write_char_xy without a colour");

    // write_y centres the same run on the 320-wide UI raster.
    const Sint32 y3 = y2 + font.sizey + 6;
    out->fastbox(0, y3 - 2, 320, font.sizey + 4,
                 static_cast<unsigned char>(background));
    font.write_y(y3, "AB", ink);
    const Sint32 centered_x = (320 - 2 * advance) / 2;
    expect_glyph_at(out, font, centered_x, y3, 'A', ink, background,
                    GlyphInk::Recolored, "write_y first");
    expect_glyph_at(out, font, centered_x + advance, y3, 'B', ink, background,
                    GlyphInk::Recolored, "write_y second");
    out->clearbuffer();
}

// The viewscreen arms are the same writes in PANE coordinates: a seat's HUD
// text is authored at (0,0) of its own pane and must appear at the pane's
// origin on the canvas. If the offset were dropped, every split-screen seat
// would print its labels over seat 1's view.
TEST(TextRender, viewscreen_write_arms_offset_by_the_pane_origin)
{
    screen* const out = og::runtime::current_session->myscreen_;
    text& font = out->text_normal;
    viewscreen* const vs = out->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    ASSERT_NE(nullptr, font.letters);

    const Sint32 saved_xloc = vs->xloc;
    const Sint32 saved_yloc = vs->yloc;
    const Sint32 saved_endx = vs->endx;
    const Sint32 saved_endy = vs->endy;
    // A bottom-right seat's pane.
    vs->xloc = 40;
    vs->yloc = 30;
    vs->endx = 300;
    vs->endy = 180;

    constexpr Sint32 x = 6;
    constexpr Sint32 y = 8;
    constexpr int background = 13;
    constexpr unsigned char ink = 64;
    const Sint32 advance = font.sizex + 1;

    out->clearbuffer();
    out->fastbox(vs->xloc, vs->yloc, 120, 3 * (font.sizey + 6),
                 static_cast<unsigned char>(background));
    EXPECT_EQ(1, font.write_xy(x, y, "AB", ink, vs));
    expect_glyph_at(out, font, vs->xloc + x, vs->yloc + y, 'A', ink,
                    background, GlyphInk::TeamShifted,
                    "write_xy(viewscreen) first");
    expect_glyph_at(out, font, vs->xloc + x + advance, vs->yloc + y, 'B', ink,
                    background, GlyphInk::TeamShifted,
                    "write_xy(viewscreen) second");
    int at_origin = -1;
    out->get_pixel(x, y, &at_origin);
    EXPECT_EQ(0, at_origin)
        << "pane text must not also appear at the canvas origin";

    const Sint32 y2 = y + font.sizey + 6;
    EXPECT_EQ(1, font.write_char_xy(x, y2, 'C', ink, vs));
    expect_glyph_at(out, font, vs->xloc + x, vs->yloc + y2, 'C', ink,
                    background, GlyphInk::TeamShifted,
                    "write_char_xy(viewscreen)");
    EXPECT_EQ(1, font.write_char_xy(x + 2 * advance, y2, 'D', vs));
    expect_glyph_at(out, font, vs->xloc + x + 2 * advance, vs->yloc + y2, 'D',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted,
                    "write_char_xy(viewscreen) default colour");

    vs->xloc = saved_xloc;
    vs->yloc = saved_yloc;
    vs->endx = saved_endx;
    vs->endy = saved_endy;
    out->clearbuffer();
}
