#include <openglad/legacy/base.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/pixie_data.h>
#include <openglad/resources/our_palette.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/core/pixdefs.h>

#include "lodepng.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
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

} // namespace

TEST(PngConversion, tile_dimensions)
{
    PixieData data = read_pixie_file("16grass1.png");
    ASSERT_TRUE(data.valid()) << "16grass1.png should load successfully";
    ASSERT_EQ(1, static_cast<int>(data.frames)) << "16grass1.png should have 1 frame";
    ASSERT_EQ(16, static_cast<int>(data.w)) << "16grass1.png should be 16px wide";
    ASSERT_EQ(16, static_cast<int>(data.h)) << "16grass1.png should be 16px tall";
}

TEST(PngConversion, multiframe_sprite_dimensions)
{
    PixieData data = read_pixie_file("archer.png");
    ASSERT_TRUE(data.valid()) << "archer.png should load successfully";
    ASSERT_EQ(24, static_cast<int>(data.frames)) << "archer.png should have 24 frames";
    ASSERT_EQ(16, static_cast<int>(data.w)) << "archer.png should be 16px wide";
    ASSERT_EQ(16, static_cast<int>(data.h)) << "archer.png frame height should be 16px";
}

TEST(PngConversion, pixel_data_valid_palette_indices)
{
    PixieData data = read_pixie_file("16grass1.png");
    ASSERT_TRUE(data.valid()) << "16grass1.png should load";

    int nonzero = 0;
    size_t total = static_cast<size_t>(data.w) * data.h * data.frames;
    for (size_t i = 0; i < total; i++) {
        if (data.data[i] != 0) nonzero++;
    }
    ASSERT_GT(nonzero, 0) << "Grass tile should have non-zero (non-transparent) pixels";
    ASSERT_GT(nonzero, static_cast<int>(total) / 2)
        << "Grass tile should be mostly non-transparent";
}

TEST(PngConversion, scenario_grid_loads)
{
    CampaignFixture fixture;

    PixieData grid = read_pixie_file("scen1.png");
    ASSERT_TRUE(grid.valid()) << "scen1.png should load from campaign";
    ASSERT_EQ(1, static_cast<int>(grid.frames)) << "Grid should have 1 frame";
    ASSERT_EQ(40, static_cast<int>(grid.w)) << "scen1 grid should be 40 wide";
    ASSERT_EQ(60, static_cast<int>(grid.h)) << "scen1 grid should be 60 tall";
}

TEST(PngConversion, scenario_grid_not_all_walls)
{
    CampaignFixture fixture;

    PixieData grid = read_pixie_file("scen1.png");
    ASSERT_TRUE(grid.valid()) << "scen1.png should load";

    size_t total = static_cast<size_t>(grid.w) * grid.h;
    int distinct_values = 0;
    bool seen[256] = {};
    for (size_t i = 0; i < total; i++) {
        if (!seen[grid.data[i]]) {
            seen[grid.data[i]] = true;
            distinct_values++;
        }
    }
    ASSERT_GE(distinct_values, 2)
        << "Grid should have at least 2 distinct tile types (not all walls)";

    int walkable = 0;
    for (size_t i = 0; i < total; i++) {
        unsigned char t = grid.data[i];
        if (t == PIX_GRASS1 || t == PIX_GRASS2 || t == PIX_GRASS3 || t == PIX_GRASS4
            || t == PIX_FLOOR_PAVEL || t == PIX_FLOOR_PAVER
            || t == PIX_FLOOR_PAVEU || t == PIX_FLOOR_PAVED
            || t == PIX_PAVEMENT1 || t == PIX_PAVEMENT2 || t == PIX_PAVEMENT3)
            walkable++;
    }
    ASSERT_GT(walkable, 0)
        << "Grid should have some walkable tiles (grass/floor)";
}

TEST(PngConversion, level_load_produces_valid_grid)
{
    CampaignFixture fixture;

    LevelRuntimeData level(1, true);
    bool loaded = level.load();
    ASSERT_TRUE(loaded) << "Level 1 should load successfully";
    ASSERT_TRUE(level.world().grid.valid()) << "Level 1 grid should be valid after load";
    ASSERT_GT(level.world().pixmaxx, 0) << "Level 1 pixmaxx should be positive";
    ASSERT_GT(level.world().pixmaxy, 0) << "Level 1 pixmaxy should be positive";
}

TEST(PngConversion, text_sprite_dimensions)
{
    PixieData data = read_pixie_file("text.png");
    ASSERT_TRUE(data.valid()) << "text.png should load successfully";
    ASSERT_EQ(125, static_cast<int>(data.frames)) << "text.png should have 125 frames";
    ASSERT_EQ(5, static_cast<int>(data.w)) << "text.png should be 5px wide";
    ASSERT_EQ(6, static_cast<int>(data.h)) << "text.png frame height should be 6px";
}

TEST(PngConversion, stale_campaign_overwritten)
{
    CampaignFixture fixture;

    PixieData grid = read_pixie_file("scen1.png");
    ASSERT_TRUE(grid.valid()) << "scen1.png should be loadable after campaign restore";
}

namespace {

bool find_png_chunk(const std::vector<unsigned char>& bytes,
                    const char (&type)[5],
                    std::size_t& out_offset,
                    std::size_t& out_length)
{
    // PNG: 8-byte signature, then chunks of [4-byte length][4-byte type][data][4-byte CRC].
    if (bytes.size() < 8) return false;
    std::size_t pos = 8;
    while (pos + 8 <= bytes.size()) {
        const std::size_t len =
            (static_cast<std::size_t>(bytes[pos]) << 24) |
            (static_cast<std::size_t>(bytes[pos + 1]) << 16) |
            (static_cast<std::size_t>(bytes[pos + 2]) << 8) |
            (static_cast<std::size_t>(bytes[pos + 3]));
        if (pos + 8 + len + 4 > bytes.size()) return false;
        if (std::memcmp(&bytes[pos + 4], type, 4) == 0) {
            out_offset = pos + 8;
            out_length = len;
            return true;
        }
        pos += 8 + len + 4;
    }
    return false;
}

std::vector<unsigned char> read_file_bytes(const std::string& path)
{
    std::vector<unsigned char> out;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        out.resize(static_cast<std::size_t>(sz));
        std::fread(out.data(), 1, out.size(), f);
    }
    std::fclose(f);
    return out;
}

void write_file_bytes(const std::string& path, const std::vector<unsigned char>& bytes)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_TRUE(f != nullptr) << "open for write: " << path;
    if (!f) return;
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
}

// PNG CRC-32 — standard table-driven implementation (poly 0xEDB88320).
unsigned png_crc32(const unsigned char* buf, std::size_t len)
{
    static unsigned table[256];
    static bool inited = false;
    if (!inited) {
        for (unsigned n = 0; n < 256; ++n) {
            unsigned c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        inited = true;
    }
    unsigned c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i)
        c = table[(c ^ buf[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void write_be32(unsigned char* dst, unsigned v)
{
    dst[0] = static_cast<unsigned char>((v >> 24) & 0xFFu);
    dst[1] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    dst[2] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    dst[3] = static_cast<unsigned char>(v & 0xFFu);
}

} // namespace

TEST(IndexedPngEncoding, png_is_indexed_color)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "indexed_png_encoding";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);
    const fs::path path = tmp_dir / "indexed.png";

    {
        auto* buf = new unsigned char[8];
        for (int i = 0; i < 8; ++i) buf[i] = static_cast<unsigned char>(i);
        PixieData data(1, 8, 1, buf);
        ASSERT_TRUE(write_pixie_png(path.string().c_str(), data));
    }

    auto bytes = read_file_bytes(path.string());
    ASSERT_GT(bytes.size(), 33u) << "PNG must include at least signature + IHDR";

    std::size_t ihdr_off = 0, ihdr_len = 0;
    ASSERT_TRUE(find_png_chunk(bytes, "IHDR", ihdr_off, ihdr_len));
    ASSERT_EQ(13u, ihdr_len) << "IHDR length";
    // IHDR layout: width(4) height(4) bitdepth(1) colortype(1) compression(1) filter(1) interlace(1)
    const unsigned char colortype_byte = bytes[ihdr_off + 9];
    ASSERT_EQ(3, static_cast<int>(colortype_byte))
        << "IHDR color type must be 3 (indexed)";

    std::size_t plte_off = 0, plte_len = 0;
    ASSERT_TRUE(find_png_chunk(bytes, "PLTE", plte_off, plte_len));
    ASSERT_EQ(768u, plte_len) << "PLTE chunk should hold 256*3 bytes";

    std::size_t trns_off = 0, trns_len = 0;
    ASSERT_TRUE(find_png_chunk(bytes, "tRNS", trns_off, trns_len));
    ASSERT_GE(trns_len, 1u) << "tRNS chunk should be present";
    ASSERT_EQ(0, static_cast<int>(bytes[trns_off]))
        << "tRNS[0] should mark palette entry 0 fully transparent";

    // Decode via lodepng to confirm it agrees we wrote indexed color.
    lodepng::State state;
    state.decoder.color_convert = 0;
    std::vector<unsigned char> pixels;
    unsigned w = 0, h = 0;
    const unsigned err = lodepng::decode(pixels, w, h, state, bytes.data(), bytes.size());
    ASSERT_EQ(0u, err) << "lodepng decode: " << lodepng_error_text(err);
    ASSERT_EQ(static_cast<unsigned>(LCT_PALETTE),
              static_cast<unsigned>(state.info_png.color.colortype));
    ASSERT_EQ(256u, static_cast<unsigned>(state.info_png.color.palettesize));

    fs::remove(path, ec);
}

TEST(IndexedPngEncoding, round_trip_pixel_indices)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "indexed_png_encoding";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);
    const fs::path path = tmp_dir / "round_trip.png";

    // Frame metadata for read_pixie_file comes from sprite_manifest.txt, not from
    // the PNG; since this temp file has no manifest entry it round-trips as a
    // single-frame sprite of height H*F. The pixel bytes themselves must still
    // match exactly.
    constexpr int W = 17;
    constexpr int H = 27;
    constexpr std::size_t total = static_cast<std::size_t>(W) * H;

    auto* buf = new unsigned char[total];
    std::mt19937 rng(0xc0ffeeu);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<unsigned char> expected(total);
    for (std::size_t i = 0; i < total; ++i) {
        const auto v = static_cast<unsigned char>(dist(rng));
        buf[i] = v;
        expected[i] = v;
    }

    PixieData data(1, W, H, buf);
    ASSERT_TRUE(write_pixie_png(path.string().c_str(), data));

    PixieData round = read_pixie_file(path.string().c_str());
    ASSERT_TRUE(round.valid());
    ASSERT_EQ(1, static_cast<int>(round.frames));
    ASSERT_EQ(W, static_cast<int>(round.w));
    ASSERT_EQ(H, static_cast<int>(round.h));
    ASSERT_EQ(0, std::memcmp(round.data.get(), expected.data(), total))
        << "pixel indices must round-trip byte-identical";

    fs::remove(path, ec);
}

TEST(IndexedPngEncoding, rejects_wrong_palette)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "indexed_png_encoding";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);
    const fs::path good = tmp_dir / "good.png";
    const fs::path bad = tmp_dir / "wrong_palette.png";

    {
        auto* buf = new unsigned char[4];
        buf[0] = 0; buf[1] = 1; buf[2] = 2; buf[3] = 3;
        PixieData data(1, 4, 1, buf);
        ASSERT_TRUE(write_pixie_png(good.string().c_str(), data));
    }

    auto bytes = read_file_bytes(good.string());
    std::size_t plte_off = 0, plte_len = 0;
    ASSERT_TRUE(find_png_chunk(bytes, "PLTE", plte_off, plte_len));
    ASSERT_EQ(768u, plte_len);

    // Swap palette entries 1 and 2 (RGB triplets at offsets 3..5 and 6..8).
    for (std::size_t c = 0; c < 3; ++c)
        std::swap(bytes[plte_off + 3 + c], bytes[plte_off + 6 + c]);

    // Recompute the PLTE chunk's CRC: CRC covers chunk type + chunk data.
    std::vector<unsigned char> crc_input;
    crc_input.reserve(4 + plte_len);
    crc_input.insert(crc_input.end(), bytes.begin() + plte_off - 4, bytes.begin() + plte_off);
    crc_input.insert(crc_input.end(), bytes.begin() + plte_off, bytes.begin() + plte_off + plte_len);
    const unsigned new_crc = png_crc32(crc_input.data(), crc_input.size());
    write_be32(&bytes[plte_off + plte_len], new_crc);

    write_file_bytes(bad.string(), bytes);

    PixieData result = read_pixie_file(bad.string().c_str());
    ASSERT_FALSE(result.valid()) << "PNG with mismatched palette must be rejected";

    fs::remove(good, ec);
    fs::remove(bad, ec);
}
