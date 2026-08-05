/* Decor cut-out sprite art (scripts/generate_decor_art.py -> pix/16d*.png):
 * asset conformance for the BASE+DECOR tile layering.
 *
 * Composite fidelity: each cut-out, drawn over its implied base tile, must
 * reproduce the legacy combined tile within the design thresholds (residual
 * <= 60/256 px, max channel delta <= 64, every residual pixel a same-band
 * shade shift); the brazier shape stencil is exact over PIX_FLOOR1 by
 * construction. Palette budgets pin the transparency and cycled-band rules:
 * only torch/brazier fire may touch the cycled ORANGE band 224-231 (do_cycle
 * IS the flame animation), nothing touches the WATER band 208-223 (the
 * glass-flashing precedent), and nothing reaches the >=248 team-recolor
 * range (walkputbuffer would repaint it with the team color).
 */
#include <openglad/core/decordefs.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/our_palette.h>
#include <openglad/gameplay/pixie_data.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>

namespace {

inline constexpr int kTilePixels = 16 * 16;

// 8-bit channel of a palette index (our.pal stores 6-bit values).
int rgb8(int index, int channel)
{
    return static_cast<int>(our_pal_lookup(index * 3 + channel)) * 4;
}

int max_channel_delta(int a, int b)
{
    int worst = 0;
    for (int c = 0; c < 3; c++)
    {
        const int d = rgb8(a, c) - rgb8(b, c);
        worst = std::max(worst, d < 0 ? -d : d);
    }
    return worst;
}

// Palette bands of the migration art. A residual pixel (composite differs
// from the legacy combined tile) must be a shade shift WITHIN one band --
// greys against greys, greens against greens -- never structural.
enum class Band
{
    None,
    Grey,
    Green,
    Brown,
    Flame,
};

Band band_of(int index)
{
    if (index >= 16 && index <= 31)
        return Band::Grey;
    if ((index >= 60 && index <= 63) || (index >= 160 && index <= 167))
        return Band::Green;
    if (index >= 132 && index <= 143)
        return Band::Brown;
    if (index >= 224 && index <= 231)
        return Band::Flame;
    return Band::None;
}

PixieData load16(const std::string& file)
{
    PixieData p = read_pixie_file(file.c_str());
    EXPECT_TRUE(p.valid()) << "pix/" << file
                           << " must parse (256-entry our.pal palette)";
    if (p.valid())
    {
        EXPECT_EQ(1, static_cast<int>(p.frames)) << file;
        EXPECT_EQ(16, static_cast<int>(p.w)) << file;
        EXPECT_EQ(16, static_cast<int>(p.h)) << file;
    }
    return p;
}

struct FidelityRow
{
    const char* decor;   // cut-out sprite
    const char* base;    // implied base tile it was measured over
    const char* legacy;  // the legacy combined tile it reproduces
    int pinned_residual; // exact residual pixel count (committed art)
};

// Pinned from the generator's own self-check (scripts/generate_decor_art.py
// asserts the same numbers before writing): regenerated art that drifts from
// these values is a generator regression, not "close enough".
constexpr FidelityRow kFidelity[] = {
    {"16dtorch1.png", "16brickc.png", "16torch1.png", 21},
    {"16dtorch2.png", "16brickc.png", "16torch2.png", 21},
    {"16dtorch3.png", "16brickc.png", "16torch3.png", 21},
    {"16dstone1.png", "16grass2.png", "16stone1.png", 3},
    {"16dstone2.png", "16grass3.png", "16stone2.png", 0},
    {"16dstone3.png", "16grass2.png", "16stone3.png", 3},
    {"16dstone4.png", "16grass2.png", "16stone4.png", 3},
    {"16dpebble.png", "16grassd.png", "16grassr.png", 54},
    {"16dbraz.png", "16floor.png", "16braz1.png", 0}, // shape stencil: exact
};

} // namespace

TEST(DecorArt, composites_reproduce_the_legacy_combined_tiles)
{
    for (const FidelityRow& row : kFidelity)
    {
        const PixieData decor = load16(row.decor);
        const PixieData base = load16(row.base);
        const PixieData legacy = load16(row.legacy);
        ASSERT_TRUE(decor.valid() && base.valid() && legacy.valid());

        int residual = 0;
        int worst_delta = 0;
        for (int i = 0; i < kTilePixels; i++)
        {
            const int d = decor.data[i];
            const int composed = (d != 0) ? d : base.data[i];
            const int want = legacy.data[i];
            if (composed == want)
                continue;
            residual++;
            worst_delta = std::max(worst_delta,
                                   max_channel_delta(composed, want));
            EXPECT_NE(Band::None, band_of(composed))
                << row.decor << ": residual pixel " << i
                << " outside every known band (index " << composed << ")";
            EXPECT_EQ(static_cast<int>(band_of(composed)),
                      static_cast<int>(band_of(want)))
                << row.decor << ": residual pixel " << i
                << " must be a same-band shade shift (" << composed << " vs "
                << want << ")";
        }
        EXPECT_EQ(row.pinned_residual, residual)
            << row.decor << " over " << row.base << " vs " << row.legacy;
        EXPECT_LE(residual, 60) << row.decor << ": design threshold";
        EXPECT_LE(worst_delta, 64)
            << row.decor << ": max channel delta threshold";
    }
}

TEST(DecorArt, palette_budgets_pin_transparency_and_cycled_band_rules)
{
    const struct
    {
        const char* file;
        bool flame; // fire sprites keep pixels in the cycled ORANGE band
    } rows[] = {
        {"16dtorch1.png", true},  {"16dtorch2.png", true},
        {"16dtorch3.png", true},  {"16dbraz.png", true},
        {"16dstone1.png", false}, {"16dstone2.png", false},
        {"16dstone3.png", false}, {"16dstone4.png", false},
        {"16dpebble.png", false}, {"16dshrub.png", false},
        {"16dbones.png", false},  {"16colm0.png", false},
        {"16colm1.png", false},
    };
    for (const auto& row : rows)
    {
        const std::string label = row.file;
        const PixieData p = load16(row.file);
        ASSERT_TRUE(p.valid()) << label;

        int transparent = 0, foreground = 0, water = 0, orange = 0,
            recolor = 0;
        for (int i = 0; i < kTilePixels; i++)
        {
            const int v = p.data[i];
            if (v == 0)
            {
                transparent++;
                continue;
            }
            foreground++;
            if (v >= 208 && v <= 223)
                water++;
            if (v >= 224 && v <= 231)
                orange++;
            if (v >= 248)
                recolor++;
        }
        EXPECT_GE(transparent, 1)
            << label << ": decor must be a transparent cut-out, not a tile";
        EXPECT_GE(foreground, 1) << label << ": cut-out must not be empty";
        EXPECT_EQ(0, water)
            << label << ": WATER cycled band is forbidden (flashing)";
        EXPECT_EQ(0, recolor)
            << label << ": >=248 would be team-recolored by walkputbuffer";
        if (row.flame)
            EXPECT_GE(orange, 1)
                << label << ": fire must keep cycled flame pixels";
        else
            EXPECT_EQ(0, orange)
                << label
                << ": non-flame decor may not touch a cycled band (flashing)";
    }
}

// The registry ids the art table (graphlib.cpp load_decor_data) maps onto are
// persisted bytes in shipped decor planes; re-pin the count here so an id
// renumbering that would silently shuffle every level's decor art fails fast.
TEST(DecorArt, decor_id_space_matches_the_art_table)
{
    ASSERT_EQ(14, static_cast<int>(DECOR_MAX));
    ASSERT_EQ(1, static_cast<int>(DECOR_TORCH1));
    ASSERT_EQ(9, static_cast<int>(DECOR_PEBBLES));
    ASSERT_EQ(11, static_cast<int>(DECOR_COLUMN_TOP));
    ASSERT_EQ(13, static_cast<int>(DECOR_BONES));
}
