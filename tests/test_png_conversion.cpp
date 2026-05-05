#include <openglad/legacy/base.h>
#include <openglad/resources/io_common.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/core/pixdefs.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

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
