#include <array>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include <openglad/interface/render/pal32.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/our_palette.h>
#include <gtest/gtest.h>

TEST(Palette, set_and_query_reg)
{
    std::array<unsigned char, 768> pal{};
    // Keep values in the classic 0-63 VGA range that this game expects.
    for (size_t i = 0; i < pal.size(); i++)
        pal[i] = static_cast<unsigned char>(i % 64);

    ASSERT_TRUE(set_palette(pal) == 1) << "set_palette should succeed";

    int r = -1, g = -1, b = -1;
    query_palette_reg(10, &r, &g, &b);
    ASSERT_EQ(static_cast<int>(pal[30]), r) << "palette reg R should match";
    ASSERT_EQ(static_cast<int>(pal[31]), g) << "palette reg G should match";
    ASSERT_EQ(static_cast<int>(pal[32]), b) << "palette reg B should match";

    set_palette_reg(10, 1, 2, 3);
    query_palette_reg(10, &r, &g, &b);
    ASSERT_EQ(1, r) << "set_palette_reg should update R";
    ASSERT_EQ(2, g) << "set_palette_reg should update G";
    ASSERT_EQ(3, b) << "set_palette_reg should update B";
}


TEST(Palette, adjust_clamps)
{
    std::array<unsigned char, 768> pal{};
    // A small known palette that will exercise both clamp directions.
    pal[0] = 0;
    pal[1] = 1;
    pal[2] = 63;

    // Lighten: 63 stays clamped, low values increase.
    adjust_palette(pal, 5);
    int r = -1, g = -1, b = -1;
    query_palette_reg(0, &r, &g, &b);
    ASSERT_TRUE(r >= 5) << "adjust_palette should lighten low channel values";
    ASSERT_TRUE(g >= 6) << "adjust_palette should lighten low channel values (g)";
    ASSERT_EQ(63, b) << "adjust_palette should clamp at 63";

    // Darken: all channels clamp at 0.
    adjust_palette(pal, -10);
    query_palette_reg(0, &r, &g, &b);
    ASSERT_EQ(0, r) << "adjust_palette should clamp at 0 (r)";
    ASSERT_EQ(0, g) << "adjust_palette should clamp at 0 (g)";
    ASSERT_EQ(0, b) << "adjust_palette should clamp at 0 (b)";
}


TEST(Palette, cycle_basic)
{
    std::array<unsigned char, 768> pal{};

    // Encode 4 palette entries (0..3) with distinct RGB triplets.
    // Entry i uses (i, i+1, i+2).
    for (int idx = 0; idx <= 3; idx++) {
        pal[static_cast<std::size_t>(idx * 3 + 0)] = static_cast<unsigned char>(idx);
        pal[static_cast<std::size_t>(idx * 3 + 1)] = static_cast<unsigned char>(idx + 1);
        pal[static_cast<std::size_t>(idx * 3 + 2)] = static_cast<unsigned char>(idx + 2);
    }

    // Cycle entries 1..3 by shift=1. Entry 1 should take on old entry 2's color.
    cycle_palette(pal, 1, 3, 1);

    int r = -1, g = -1, b = -1;
    query_palette_reg(1, &r, &g, &b);
    ASSERT_EQ(2, r) << "cycle_palette should rotate entry 1 from entry 2 (r)";
    ASSERT_EQ(3, g) << "cycle_palette should rotate entry 1 from entry 2 (g)";
    ASSERT_EQ(4, b) << "cycle_palette should rotate entry 1 from entry 2 (b)";
}

TEST(Palette, save_palette_is_stubbed_out)
{
    std::array<unsigned char, 768> pal{};
    ASSERT_EQ(0, save_palette(pal)) << "save_palette should remain a no-op in SDL-free tests";
}


TEST(PaletteExport, gpl_matches_runtime_palette)
{
    auto file = og::io::og_open_read("pix/openglad.gpl");
    if (!file)
        file = og::io::og_open_read("openglad.gpl");
    ASSERT_TRUE(file != nullptr) << "pix/openglad.gpl must be readable via og_open_read";

    std::vector<char> blob;
    char chunk[4096];
    for (;;) {
        const std::size_t got = file->read(chunk, 1, sizeof(chunk));
        if (got == 0)
            break;
        blob.insert(blob.end(), chunk, chunk + got);
    }
    ASSERT_FALSE(blob.empty()) << "pix/openglad.gpl must not be empty";

    std::istringstream stream(std::string(blob.begin(), blob.end()));
    std::string line;

    ASSERT_TRUE(std::getline(stream, line)) << "expected GIMP Palette header";
    ASSERT_EQ("GIMP Palette", line) << "first line must be 'GIMP Palette'";

    int header_lines_consumed = 1;
    while (std::getline(stream, line)) {
        ++header_lines_consumed;
        if (line.empty())
            continue;
        if (line[0] == '#')
            break;
        // Tolerate Name:/Columns: and any other prelude lines until '#'.
    }
    ASSERT_LE(header_lines_consumed, 16) << "header should be short; runaway parser";

    int parsed = 0;
    while (parsed < 256) {
        ASSERT_TRUE(std::getline(stream, line))
            << "ran out of lines after " << parsed << " palette entries";
        if (line.empty())
            continue;
        std::istringstream parts(line);
        int r = -1, g = -1, b = -1;
        ASSERT_TRUE(static_cast<bool>(parts >> r >> g >> b))
            << "could not parse RGB triple at entry " << parsed << ": '" << line << "'";
        ASSERT_GE(r, 0); ASSERT_LE(r, 255);
        ASSERT_GE(g, 0); ASSERT_LE(g, 255);
        ASSERT_GE(b, 0); ASSERT_LE(b, 255);

        const int expect_r = (static_cast<int>(our_pal_lookup(parsed * 3 + 0)) * 255) / 63;
        const int expect_g = (static_cast<int>(our_pal_lookup(parsed * 3 + 1)) * 255) / 63;
        const int expect_b = (static_cast<int>(our_pal_lookup(parsed * 3 + 2)) * 255) / 63;
        ASSERT_LE(std::abs(r - expect_r), 1) << "entry " << parsed << " R drift";
        ASSERT_LE(std::abs(g - expect_g), 1) << "entry " << parsed << " G drift";
        ASSERT_LE(std::abs(b - expect_b), 1) << "entry " << parsed << " B drift";
        ++parsed;
    }
    ASSERT_EQ(256, parsed) << "must parse exactly 256 palette entries";
}

