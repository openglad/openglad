#include <openglad/legacy/base.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/resources/our_palette.h>

#include "lodepng.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct CampaignFixture {
    std::string old_campaign;

    CampaignFixture() {
        old_campaign = get_mounted_campaign();
        if (!old_campaign.empty())
            (void)unmount_campaign_package_with_error(old_campaign);
        (void)mount_campaign_package_with_error("org.openglad.gladiator");
    }

    ~CampaignFixture() {
        (void)unmount_campaign_package_with_error("org.openglad.gladiator");
        if (!old_campaign.empty() && old_campaign != "org.openglad.gladiator")
            (void)mount_campaign_package_with_error(old_campaign);
    }
};

class AsepriteRoundTripFixture : public ::testing::Test {
protected:
    std::filesystem::path tmp_dir = std::filesystem::path("temp") / "aseprite_round_trip";

    void SetUp() override {
        std::error_code ec;
        std::filesystem::create_directories(tmp_dir, ec);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tmp_dir, ec);
    }
};

void copy_file_overwrite(const std::filesystem::path& src,
                         const std::filesystem::path& dst)
{
    std::error_code ec;
    std::filesystem::copy_file(
        src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << "copy " << src << " -> " << dst << ": " << ec.message();
}

// Fill a lodepng State with the engine's canonical 256-entry palette and
// optional per-entry alpha (entry 0 = transparent).
void install_canonical_palette(lodepng::State& state)
{
    for (unsigned i = 0; i < 256; ++i) {
        const unsigned r6 = our_pal_lookup(static_cast<int>(i * 3));
        const unsigned g6 = our_pal_lookup(static_cast<int>(i * 3 + 1));
        const unsigned b6 = our_pal_lookup(static_cast<int>(i * 3 + 2));
        const auto r8 = static_cast<unsigned char>((r6 * 255u) / 63u);
        const auto g8 = static_cast<unsigned char>((g6 * 255u) / 63u);
        const auto b8 = static_cast<unsigned char>((b6 * 255u) / 63u);
        const auto a8 = static_cast<unsigned char>((i == 0) ? 0u : 255u);
        lodepng_palette_add(&state.info_raw, r8, g8, b8, a8);
        lodepng_palette_add(&state.info_png.color, r8, g8, b8, a8);
    }
}

void write_png_bytes(const std::filesystem::path& path,
                     const std::vector<unsigned char>& bytes)
{
    std::ofstream f(path, std::ios::binary);
    ASSERT_TRUE(f.good()) << "open for write: " << path;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_F(AsepriteRoundTripFixture, multiframe_sprite_via_json_sidecar)
{
    CampaignFixture campaign;

    PixieData original = read_pixie_file("cleric.png");
    ASSERT_TRUE(original.valid()) << "cleric.png should load via JSON sidecar";
    ASSERT_EQ(24, static_cast<int>(original.frames));
    ASSERT_GT(static_cast<int>(original.w), 0);
    ASSERT_GT(static_cast<int>(original.h), 0);

    const std::filesystem::path png_copy = tmp_dir / "cleric_copy.png";
    const std::filesystem::path json_copy = tmp_dir / "cleric_copy.json";

    {
        // write_pixie_png consumes the data unique_ptr; copy first so we can
        // compare byte-for-byte after re-reading.
        const std::size_t total =
            static_cast<std::size_t>(original.w) *
            static_cast<std::size_t>(original.h) *
            static_cast<std::size_t>(original.frames);
        std::vector<unsigned char> expected(
            original.data.get(), original.data.get() + total);

        auto* buf = new unsigned char[total];
        std::memcpy(buf, original.data.get(), total);
        PixieData copy(original.frames, original.w, original.h, buf);
        ASSERT_TRUE(write_pixie_png(png_copy.string().c_str(), copy));

        copy_file_overwrite("pix/cleric.json", json_copy);

        PixieData round = read_pixie_file(png_copy.string().c_str());
        ASSERT_TRUE(round.valid()) << "round-tripped cleric should reload";
        ASSERT_EQ(static_cast<int>(original.frames),
                  static_cast<int>(round.frames));
        ASSERT_EQ(static_cast<int>(original.w), static_cast<int>(round.w));
        ASSERT_EQ(static_cast<int>(original.h), static_cast<int>(round.h));
        ASSERT_EQ(0, std::memcmp(round.data.get(), expected.data(), total))
            << "pixel indices must round-trip byte-identical";
    }
}

TEST_F(AsepriteRoundTripFixture, single_frame_sprite_no_json_needed)
{
    CampaignFixture campaign;

    PixieData original = read_pixie_file("16braz1.png");
    ASSERT_TRUE(original.valid()) << "16braz1.png should load";
    ASSERT_EQ(1, static_cast<int>(original.frames));
    ASSERT_GT(static_cast<int>(original.w), 0);
    ASSERT_GT(static_cast<int>(original.h), 0);

    const std::filesystem::path png_copy = tmp_dir / "16braz1_copy.png";

    const std::size_t total =
        static_cast<std::size_t>(original.w) *
        static_cast<std::size_t>(original.h) *
        static_cast<std::size_t>(original.frames);
    std::vector<unsigned char> expected(
        original.data.get(), original.data.get() + total);

    auto* buf = new unsigned char[total];
    std::memcpy(buf, original.data.get(), total);
    PixieData copy(original.frames, original.w, original.h, buf);
    ASSERT_TRUE(write_pixie_png(png_copy.string().c_str(), copy));

    // No JSON sidecar — single-frame PNGs round-trip without one.
    PixieData round = read_pixie_file(png_copy.string().c_str());
    ASSERT_TRUE(round.valid());
    ASSERT_EQ(1, static_cast<int>(round.frames));
    ASSERT_EQ(static_cast<int>(original.w), static_cast<int>(round.w));
    ASSERT_EQ(static_cast<int>(original.h), static_cast<int>(round.h));
    ASSERT_EQ(0, std::memcmp(round.data.get(), expected.data(), total))
        << "single-frame pixel indices must round-trip byte-identical";
}

TEST_F(AsepriteRoundTripFixture, palette_mismatch_rejected)
{
    constexpr unsigned W = 8;
    constexpr unsigned H = 4;
    std::vector<unsigned char> raw(W * H, 0);
    for (unsigned i = 0; i < W * H; ++i)
        raw[i] = static_cast<unsigned char>(i & 0x07);

    lodepng::State state;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;
    state.encoder.auto_convert = 0;
    install_canonical_palette(state);

    // Knock entry 4's red channel off by 10 in both info_raw and info_png so
    // lodepng accepts it as self-consistent input but the engine rejects it.
    auto bump = [](unsigned char* pal, std::size_t entry) {
        const int orig = pal[entry * 4 + 0];
        const int bumped = (orig + 10) & 0xFF;
        pal[entry * 4 + 0] = static_cast<unsigned char>(bumped);
    };
    bump(state.info_raw.palette, 4);
    bump(state.info_png.color.palette, 4);

    std::vector<unsigned char> png_bytes;
    const unsigned err = lodepng::encode(png_bytes, raw.data(), W, H, state);
    ASSERT_EQ(0u, err) << "encode: " << lodepng_error_text(err);

    const std::filesystem::path bad = tmp_dir / "wrong_palette.png";
    write_png_bytes(bad, png_bytes);

    PixieData result = read_pixie_file(bad.string().c_str());
    ASSERT_FALSE(result.valid()) << "PNG with palette off by 10 must be rejected";
}

TEST_F(AsepriteRoundTripFixture, foreign_color_type_rejected)
{
    constexpr unsigned W = 6;
    constexpr unsigned H = 5;

    // 32-bit RGBA — engine accepts only indexed.
    {
        std::vector<unsigned char> rgba(W * H * 4, 0);
        for (unsigned i = 0; i < W * H; ++i) {
            rgba[i * 4 + 0] = static_cast<unsigned char>(i * 3);
            rgba[i * 4 + 1] = static_cast<unsigned char>(i * 5);
            rgba[i * 4 + 2] = static_cast<unsigned char>(i * 7);
            rgba[i * 4 + 3] = 255;
        }
        lodepng::State state;
        state.info_raw.colortype = LCT_RGBA;
        state.info_raw.bitdepth = 8;
        state.info_png.color.colortype = LCT_RGBA;
        state.info_png.color.bitdepth = 8;
        state.encoder.auto_convert = 0;

        std::vector<unsigned char> png_bytes;
        const unsigned err = lodepng::encode(png_bytes, rgba.data(), W, H, state);
        ASSERT_EQ(0u, err) << "encode rgba: " << lodepng_error_text(err);

        const std::filesystem::path rgba_path = tmp_dir / "rgba32.png";
        write_png_bytes(rgba_path, png_bytes);

        PixieData result = read_pixie_file(rgba_path.string().c_str());
        ASSERT_FALSE(result.valid()) << "32-bit RGBA PNG must be rejected";
    }

    // LCT_GREY 8-bit — engine no longer accepts grayscale sprites.
    {
        std::vector<unsigned char> grey(W * H, 0);
        for (unsigned i = 0; i < W * H; ++i)
            grey[i] = static_cast<unsigned char>(i * 11);

        lodepng::State state;
        state.info_raw.colortype = LCT_GREY;
        state.info_raw.bitdepth = 8;
        state.info_png.color.colortype = LCT_GREY;
        state.info_png.color.bitdepth = 8;
        state.encoder.auto_convert = 0;

        std::vector<unsigned char> png_bytes;
        const unsigned err = lodepng::encode(png_bytes, grey.data(), W, H, state);
        ASSERT_EQ(0u, err) << "encode grey: " << lodepng_error_text(err);

        const std::filesystem::path grey_path = tmp_dir / "grey8.png";
        write_png_bytes(grey_path, png_bytes);

        PixieData result = read_pixie_file(grey_path.string().c_str());
        ASSERT_FALSE(result.valid()) << "LCT_GREY 8-bit PNG must be rejected";
    }
}
