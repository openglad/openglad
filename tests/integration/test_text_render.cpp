#include <openglad/interface/screen.h>
#include <openglad/interface/render/view.h>
#include <gtest/gtest.h>

#include <cstddef>

// myscreen is now a macro defined in base.h (via game_session.h)

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

const unsigned char* glyph_bytes(text& font, char letter)
{
    const std::size_t stride = static_cast<std::size_t>(font.sizex) *
                               static_cast<std::size_t>(font.sizey);
    return font.letters->data.get() +
           static_cast<std::size_t>(static_cast<unsigned char>(letter)) * stride;
}

void expect_glyph_at(screen* out, text& font, Sint32 x, Sint32 y, char letter,
                     unsigned char ink, int background, GlyphInk mode,
                     const char* what)
{
    const unsigned char* const glyph = glyph_bytes(font, letter);
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

// The shadowed arms paint the whole glyph in PURE_BLACK+2 one pixel down and
// left, then the glyph itself in `color` on top. Model both passes over the
// union box so a missing shadow pass, a shadow drawn at the wrong offset and a
// glyph that never lands on top are all separately visible.
void expect_shadowed_glyph_at(screen* out, text& font, Sint32 x, Sint32 y,
                              char letter, unsigned char ink, int background,
                              const char* what)
{
    const unsigned char* const glyph = glyph_bytes(font, letter);
    const auto source_at = [&](Sint32 row, Sint32 col) -> unsigned char {
        if (row < 0 || col < 0 || row >= font.sizey || col >= font.sizex)
            return 0;
        return glyph[static_cast<std::size_t>(row * font.sizex + col)];
    };
    int shadow_pixels = 0;
    for (Sint32 py = y; py <= y + font.sizey; ++py)
        for (Sint32 px = x - 1; px <= x + font.sizex; ++px)
        {
            int expected = background;
            const unsigned char under = source_at(py - (y + 1), px - (x - 1));
            if (under != 0)
                expected = under <= 247
                               ? static_cast<int>(under)
                               : static_cast<int>(PURE_BLACK + 2);
            const unsigned char over = source_at(py - y, px - x);
            if (over != 0)
                expected = over <= 247 ? static_cast<int>(over)
                                       : static_cast<int>(ink);
            int actual = -1;
            out->get_pixel(px, py, &actual);
            ASSERT_EQ(expected, actual)
                << what << ": '" << letter << "' canvas pixel " << px << ","
                << py;
            if (under > 247 && over == 0)
                shadow_pixels++;
        }
    ASSERT_GT(shadow_pixels, 0)
        << what << ": '" << letter << "' left no visible shadow at all";
}
} // namespace

// ---------------------------------------------------------------------------
// text::query_width
// ---------------------------------------------------------------------------

TEST(TextRender, text_query_width_empty)
{
    Sint32 w = og::runtime::current_session->myscreen_->text_normal.query_width("");
    ASSERT_EQ(0, (int)w) << "empty string width is 0";
}


// The small font is monospaced: every glyph costs sizex+1 pixels. Every
// centred label, every right-aligned number and every menu caption is laid
// out from this number, so it is pinned exactly rather than as "positive".
TEST(TextRender, small_font_query_width_is_one_advance_per_character)
{
    text& font = og::runtime::current_session->myscreen_->text_normal;
    ASSERT_EQ(0, font.query_width("")) << "an empty run measures zero";
    ASSERT_LT(font.sizex, 9) << "text_normal is the small monospaced font";
    const Sint32 advance = font.sizex + 1;

    ASSERT_EQ(advance, font.query_width("A"))
        << "one character costs one advance (sizex + 1)";
    ASSERT_EQ(2 * advance, font.query_width("Hi"))
        << "two characters cost two advances";
    ASSERT_EQ(11 * advance, font.query_width("Hello World"))
        << "the small font is monospaced: width is (sizex + 1) * length";
}


// The big font is proportional: 'A'..']' cost sizex, everything else sizex-1.
TEST(TextRender, big_font_query_width_charges_uppercase_the_wider_advance)
{
    text& big = og::runtime::current_session->myscreen_->text_big;
    ASSERT_EQ(0, big.query_width("")) << "an empty run measures zero";
    ASSERT_GE(big.sizex, 9) << "text_big takes the proportional branch";

    ASSERT_EQ(4 * big.sizex, big.query_width("TEST"))
        << "uppercase bytes (65..93) each cost sizex";
    ASSERT_EQ(big.sizex + 3 * (big.sizex - 1), big.query_width("Test"))
        << "lowercase bytes each cost sizex - 1";
}


// ---------------------------------------------------------------------------
// text::write_xy variations (all to buffer)
// ---------------------------------------------------------------------------

// The to_buffer arms go through walkputbuffertext, which ramps the font's
// recolourable bytes as colour + (255 - source) -- the team ramp the HUD and
// the in-world labels are drawn with -- and report the pixel width they
// consumed so callers can lay out the next run.
TEST(TextRender, buffered_write_xy_ramps_each_glyph_and_returns_the_run_width)
{
    screen* const out = og::runtime::current_session->myscreen_;
    text& font = out->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());
    ASSERT_LT(font.sizex, 9) << "text_normal is the small monospaced font";

    constexpr Sint32 x = 10;
    constexpr int background = 13;
    const Sint32 advance = font.sizex + 1;

    // The default-colour overload forwards DEFAULT_TEXT_COLOR.
    constexpr Sint32 y = 10;
    out->fastbox(x, y, 3 * advance, font.sizey,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(2 * advance, font.write_xy(x, y, "AB", (short)1))
        << "the buffered arm returns the width it consumed";
    expect_glyph_at(out, font, x, y, 'A',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted, "write_xy(to_buffer) first");
    expect_glyph_at(out, font, x + advance, y, 'B',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted, "write_xy(to_buffer) second");

    // The coloured overload ramps from the caller's colour instead.
    constexpr Sint32 y2 = 20;
    constexpr unsigned char ink = 64;
    out->fastbox(x, y2, 3 * advance, font.sizey,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(2 * advance, font.write_xy(x, y2, "AB", ink, (short)1))
        << "the coloured buffered arm returns the same width";
    expect_glyph_at(out, font, x, y2, 'A', ink, background,
                    GlyphInk::TeamShifted, "write_xy(colour, to_buffer) first");
    expect_glyph_at(out, font, x + advance, y2, 'B', ink, background,
                    GlyphInk::TeamShifted,
                    "write_xy(colour, to_buffer) second");
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


// ---------------------------------------------------------------------------
// text::write_y variations
// ---------------------------------------------------------------------------

// write_y is the centring arm: it starts the run at (320 - len*advance)/2 on
// the 320-wide UI raster. The buffered overloads are what the in-game
// message lines use, so the centre must be exact, not merely non-zero.
TEST(TextRender, buffered_write_y_centres_the_run_on_the_320_raster)
{
    screen* const out = og::runtime::current_session->myscreen_;
    text& font = out->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());
    ASSERT_LT(font.sizex, 9) << "text_normal is the small monospaced font";

    constexpr int background = 13;
    const Sint32 advance = font.sizex + 1;
    const Sint32 centered_x = (320 - 2 * advance) / 2;

    constexpr Sint32 y = 70;
    out->fastbox(0, y, 320, font.sizey,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(2 * advance, font.write_y(y, "AB", (short)1))
        << "the buffered centring arm returns the run width";
    int at_left_margin = -1;
    out->get_pixel(0, y, &at_left_margin);
    EXPECT_EQ(background, at_left_margin)
        << "a centred run must not start at the left margin";
    expect_glyph_at(out, font, centered_x, y, 'A',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted, "write_y(to_buffer) first");
    expect_glyph_at(out, font, centered_x + advance, y, 'B',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted, "write_y(to_buffer) second");

    constexpr Sint32 y2 = 80;
    constexpr unsigned char ink = 64;
    out->fastbox(0, y2, 320, font.sizey,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(2 * advance, font.write_y(y2, "AB", ink, (short)1))
        << "the coloured buffered centring arm returns the run width";
    expect_glyph_at(out, font, centered_x, y2, 'A', ink, background,
                    GlyphInk::TeamShifted, "write_y(colour, to_buffer) first");
    expect_glyph_at(out, font, centered_x + advance, y2, 'B', ink, background,
                    GlyphInk::TeamShifted, "write_y(colour, to_buffer) second");
}


// ---------------------------------------------------------------------------
// text::write_xy_center variations
// ---------------------------------------------------------------------------

// The floating damage numbers ride on write_xy_center_alpha: the run is
// centred on the x it is given (half a run width to the left) and the glyph
// is blended at the caller's coverage. Alpha 255 must paint the ink outright
// and alpha 0 must leave the canvas untouched.
TEST(TextRender, write_xy_center_alpha_centres_the_run_and_honours_alpha)
{
    screen* const out = og::runtime::current_session->myscreen_;
    text& font = out->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());

    constexpr int background = 13;
    constexpr unsigned char ink = 64;
    constexpr Sint32 center_x = 160;
    const Sint32 advance = font.sizex + 1;
    const Sint32 x0 = center_x - (2 * advance) / 2;

    // Fully opaque: every lit font byte becomes the ink, transparent bytes
    // keep the background. walkputbuffertext_alpha passes the caller's colour
    // straight to the blend, so even the font's literal bytes land as ink.
    constexpr Sint32 y = 100;
    out->fastbox(x0 - 4, y, 2 * advance + 8, font.sizey,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(1, font.write_xy_center_alpha(center_x, y, ink, 255, "AB"))
        << "the centred arm reports 1";
    for (int glyph_index = 0; glyph_index < 2; ++glyph_index)
    {
        const char letter = static_cast<char>('A' + glyph_index);
        const unsigned char* const glyph = glyph_bytes(font, letter);
        const Sint32 gx = x0 + glyph_index * advance;
        int lit = 0;
        for (Sint32 row = 0; row < font.sizey; ++row)
            for (Sint32 col = 0; col < font.sizex; ++col)
            {
                const unsigned char source =
                    glyph[static_cast<std::size_t>(row * font.sizex + col)];
                int actual = -1;
                out->get_pixel(gx + col, y + row, &actual);
                ASSERT_EQ(source == 0 ? background : static_cast<int>(ink),
                          actual)
                    << "alpha 255 glyph '" << letter << "' pixel " << col << ","
                    << row;
                if (source != 0)
                    lit++;
            }
        ASSERT_GT(lit, 0) << "'" << letter << "' has no ink at all";
    }
    int left_of_run = -1;
    out->get_pixel(x0 - 2, y, &left_of_run);
    EXPECT_EQ(background, left_of_run)
        << "the run is centred, so nothing is painted left of its start";

    // Fully transparent: the same call must not change a single pixel.
    constexpr Sint32 y2 = 110;
    out->fastbox(x0 - 4, y2, 2 * advance + 8, font.sizey,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(1, font.write_xy_center_alpha(center_x, y2, ink, 0, "AB"))
        << "the centred arm reports 1 even at zero coverage";
    for (Sint32 row = 0; row < font.sizey; ++row)
        for (Sint32 col = 0; col < 2 * advance + 8; ++col)
        {
            int actual = -1;
            out->get_pixel(x0 - 4 + col, y2 + row, &actual);
            ASSERT_EQ(background, actual)
                << "alpha 0 must leave the canvas untouched at " << col << ","
                << row;
        }
}


// The shadowed centred arm is what the level banner uses: the glyph is laid
// down twice, once in near-black one pixel down-left and once in the caller's
// colour on top.
TEST(TextRender, write_xy_center_shadow_lays_black_under_the_offset_glyph)
{
    screen* const out = og::runtime::current_session->myscreen_;
    text& font = out->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());

    constexpr int background = 13;
    constexpr unsigned char ink = 64;
    constexpr Sint32 center_x = 160;
    constexpr Sint32 y = 130;
    const Sint32 advance = font.sizex + 1;
    const Sint32 x0 = center_x - advance / 2;

    out->fastbox(x0 - 4, y - 2, font.sizex + 8, font.sizey + 6,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(1, font.write_xy_center_shadow(center_x, y, ink, "A"))
        << "the shadowed centred arm reports 1";
    expect_shadowed_glyph_at(out, font, x0, y, 'A', ink, background,
                             "write_xy_center_shadow");
}


// ---------------------------------------------------------------------------
// text::write_char_xy variations
// ---------------------------------------------------------------------------

// The single-glyph buffered arm reports 1 for a glyph it painted (0 for an
// empty span) and ramps it exactly like the string arm.
TEST(TextRender, buffered_write_char_xy_paints_one_ramped_glyph)
{
    screen* const out = og::runtime::current_session->myscreen_;
    text& font = out->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());

    constexpr int background = 13;
    constexpr Sint32 y = 120;
    out->fastbox(30, y, 20, font.sizey,
                 static_cast<unsigned char>(background));

    EXPECT_EQ(1, font.write_char_xy(30, y, 'C', (short)1))
        << "the buffered glyph arm reports the glyph it painted";
    expect_glyph_at(out, font, 30, y, 'C',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted, "write_char_xy(to_buffer)");

    EXPECT_EQ(1, font.write_char_xy(40, y, 'D',
                                   static_cast<unsigned char>(DARK_BLUE),
                                   (short)1))
        << "the coloured buffered glyph arm reports the glyph it painted";
    expect_glyph_at(out, font, 40, y, 'D',
                    static_cast<unsigned char>(DARK_BLUE), background,
                    GlyphInk::TeamShifted,
                    "write_char_xy(colour, to_buffer)");
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

    // The overloads that take no colour must forward DEFAULT_TEXT_COLOR to
    // the coloured arm above, not drop the write.
    const Sint32 y4 = y3 + font.sizey + 6;
    out->fastbox(x - 2, y4 - 2, advance * 4 + 4, font.sizey + 4,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(1, font.write_xy(x, y4, "A"))
        << "write_xy(x, y, string) forwards to the coloured arm";
    expect_glyph_at(out, font, x, y4, 'A',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::Recolored, "write_xy default colour");

    const Sint32 y5 = y4 + font.sizey + 6;
    out->fastbox(0, y5 - 2, 320, font.sizey + 4,
                 static_cast<unsigned char>(background));
    EXPECT_EQ(1, font.write_y(y5, "AB"))
        << "write_y(y, string) forwards to the coloured centring arm";
    expect_glyph_at(out, font, centered_x, y5, 'A',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::Recolored, "write_y default colour first");
    expect_glyph_at(out, font, centered_x + advance, y5, 'B',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::Recolored, "write_y default colour second");
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
    const Sint32 centered_x = (320 - 2 * advance) / 2;

    out->clearbuffer();
    // Five rows of pane-relative text, wide enough for the centred runs.
    out->fastbox(vs->xloc, vs->yloc, 260, 6 * (font.sizey + 6),
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

    // The pane-relative string arm without a colour, and the centring arms:
    // write_y centres on the 320 raster first, THEN offsets by the pane.
    const Sint32 y3 = y2 + font.sizey + 6;
    EXPECT_EQ(1, font.write_xy(x, y3, "AB", vs))
        << "write_xy(x, y, string, viewscreen) forwards the default colour";
    expect_glyph_at(out, font, vs->xloc + x, vs->yloc + y3, 'A',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted,
                    "write_xy(viewscreen) default colour first");
    expect_glyph_at(out, font, vs->xloc + x + advance, vs->yloc + y3, 'B',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted,
                    "write_xy(viewscreen) default colour second");

    const Sint32 y4 = y3 + font.sizey + 6;
    EXPECT_EQ(1, font.write_y(y4, "AB", ink, vs));
    expect_glyph_at(out, font, vs->xloc + centered_x, vs->yloc + y4, 'A', ink,
                    background, GlyphInk::TeamShifted,
                    "write_y(colour, viewscreen) first");
    expect_glyph_at(out, font, vs->xloc + centered_x + advance,
                    vs->yloc + y4, 'B', ink, background,
                    GlyphInk::TeamShifted, "write_y(colour, viewscreen) second");

    const Sint32 y5 = y4 + font.sizey + 6;
    EXPECT_EQ(1, font.write_y(y5, "AB", vs));
    expect_glyph_at(out, font, vs->xloc + centered_x, vs->yloc + y5, 'A',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted, "write_y(viewscreen) first");
    expect_glyph_at(out, font, vs->xloc + centered_x + advance,
                    vs->yloc + y5, 'B',
                    static_cast<unsigned char>(DEFAULT_TEXT_COLOR), background,
                    GlyphInk::TeamShifted, "write_y(viewscreen) second");

    vs->xloc = saved_xloc;
    vs->yloc = saved_yloc;
    vs->endx = saved_endx;
    vs->endy = saved_endy;
    out->clearbuffer();
}
