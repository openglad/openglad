/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include "../campaign_sprite_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/og_file.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Issue #162: campaign-shipped entity sprites and pack-family sprites must
// reach the loader when a campaign is mounted mid-session, the editor-save
// remount must NOT invalidate loaders (D13), and an explicitly configured
// user sprite sheet must outrank campaign pix/ art for the names it ships
// regardless of mount order.

namespace {

constexpr const char* kFixtureId = "org.test.sprite162";
constexpr const char* kSheetName = "sheet162";

struct PixieSnapshot {
    unsigned char frames = 0;
    unsigned char w = 0;
    unsigned char h = 0;
    std::vector<unsigned char> pixels;

    bool operator==(const PixieSnapshot&) const = default;
};

PixieSnapshot snapshot_pixie(const PixieData* pix)
{
    PixieSnapshot snap;
    if (pix == nullptr || !pix->valid())
        return snap;
    snap.frames = pix->frames;
    snap.w = pix->w;
    snap.h = pix->h;
    const std::size_t len =
        static_cast<std::size_t>(pix->frames) * pix->w * pix->h;
    snap.pixels.assign(pix->data.get(), pix->data.get() + len);
    return snap;
}

class CampaignSpriteReloadTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        // Unit binaries skip io_init's asset mounts; the loader needs the
        // shipped pix/ tree for its core sprites (idempotent append).
        ASSERT_TRUE(og::resources::mount(
            (get_asset_path() + "pix/").c_str(), "pix/", 1));
        previous_mount_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("org.openglad.gladiator"));

        original_sheet_ = cfg.get_setting("graphics", "sprite_sheet");
        cfg.apply_setting("graphics", "sprite_sheet", "");
        ASSERT_TRUE(apply_sprite_sheet_setting());
    }

    void TearDown() override
    {
        cfg.apply_setting("graphics", "sprite_sheet", original_sheet_);
        (void)apply_sprite_sheet_setting();

        const std::string mounted = get_mounted_campaign();
        if (mounted == kFixtureId)
            (void)unmount_campaign_package_with_error(mounted);
        og::test162::remove_sprite_campaign(kFixtureId);
        remove_sheet_fixture();
        if (!previous_mount_.empty())
            (void)mount_campaign_package_with_error(previous_mount_);
        else
            (void)mount_campaign_package_with_error("org.openglad.gladiator");
    }

    static std::filesystem::path sheet_dir()
    {
        return std::filesystem::path(get_user_path()) / "extra_pix" /
               kSheetName;
    }

    static std::vector<std::uint8_t> sheet_footman_bytes()
    {
        return {0x51, 0x52, 0x53, 0x54};
    }

    static void install_sheet_fixture()
    {
        std::error_code ec;
        std::filesystem::create_directories(sheet_dir(), ec);
        ASSERT_FALSE(ec) << ec.message();
        ASSERT_TRUE(og::test162::write_bytes(sheet_dir() / "footman.png",
                                             sheet_footman_bytes()));
    }

    static void remove_sheet_fixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(sheet_dir(), ec);
    }

private:
    std::string previous_mount_;
    std::string original_sheet_;
};

}  // namespace

TEST_F(CampaignSpriteReloadTest,
       mount_bumps_generation_and_stale_reload_swaps_campaign_art)
{
    loader l;
    const PixieSnapshot stock_soldier =
        snapshot_pixie(l.graphics_for(Order::Living, FAMILY_SOLDIER));
    ASSERT_FALSE(stock_soldier.pixels.empty())
        << "the stock footman sprite must be loadable";

    const PixieData donor = read_pixie_file("elf.png");
    ASSERT_TRUE(donor.valid());
    const PixieSnapshot donor_snap = snapshot_pixie(&donor);
    ASSERT_NE(stock_soldier, donor_snap)
        << "fixture premise: the donor art must differ from stock footman";

    ASSERT_TRUE(og::test162::install_sprite_carrier_campaign(kFixtureId));
    const unsigned gen_before = og::resources::sprite_source_generation();
    std::map<std::string, int> levels;
    ASSERT_EQ(1, load_campaign(kFixtureId, levels, 1));
    EXPECT_NE(gen_before, og::resources::sprite_source_generation())
        << "a campaign mount must bump the sprite-source generation";

    EXPECT_TRUE(l.reload_graphics_if_stale())
        << "a loader built before the mount must report itself stale";
    EXPECT_EQ(donor_snap,
              snapshot_pixie(l.graphics_for(Order::Living, FAMILY_SOLDIER)))
        << "after the reload the soldier must wear the campaign's shipped art";

    EXPECT_FALSE(l.reload_graphics_if_stale())
        << "a second reload with no source change must be a no-op";
}

TEST_F(CampaignSpriteReloadTest,
       pack_family_resolves_after_reload_not_the_soldier_fallback)
{
    loader l;  // built before the pack exists

    ASSERT_TRUE(og::test162::install_sprite_carrier_campaign(kFixtureId));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kFixtureId));

    const int family = og::families::resolve_family_string_id(
        Order::Living, og::test162::kBallFamilyId);
    ASSERT_GE(family, NUM_FAMILIES)
        << "the embedded pack family must land in a mod slot";

    // The pre-fix behavior this feature exists to kill: descriptors follow
    // the mount, the loader does not, and the family silently degrades to a
    // soldier.
    std::unique_ptr<walker> stale = l.create_walker_owned(Order::Living,
                                                          family);
    ASSERT_NE(stale, nullptr);
    EXPECT_EQ(FAMILY_SOLDIER,
              static_cast<int>(static_cast<unsigned char>(stale->family())))
        << "premise: a stale loader falls back to the soldier";

    EXPECT_TRUE(l.reload_graphics_if_stale());
    const PixieData* pix = l.graphics_for(Order::Living, family);
    ASSERT_NE(pix, nullptr)
        << "the pack sprite must load through the reload";
    EXPECT_EQ(10, static_cast<int>(pix->w));
    EXPECT_EQ(10, static_cast<int>(pix->h));
    EXPECT_EQ(24, static_cast<int>(pix->frames))
        << "frame metadata must come from the pack's own sidecar";

    std::unique_ptr<walker> fresh = l.create_walker_owned(Order::Living,
                                                          family);
    ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(family,
              static_cast<int>(static_cast<unsigned char>(fresh->family())))
        << "after the reload the pack family must resolve as itself";
}

TEST_F(CampaignSpriteReloadTest, editor_save_remount_keeps_the_generation)
{
    ASSERT_TRUE(og::test162::install_sprite_carrier_campaign(kFixtureId));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kFixtureId));

    loader l;  // built with the fixture mounted: current by construction
    ASSERT_FALSE(l.reload_graphics_if_stale());

    const unsigned gen_before = og::resources::sprite_source_generation();
    ASSERT_EQ(CampaignPackageIoError::None,
              remount_campaign_package_with_error());
    EXPECT_EQ(gen_before, og::resources::sprite_source_generation())
        << "D13: an editor-save remount restores the identical source set "
           "and must not mark live loaders stale";
    EXPECT_FALSE(l.reload_graphics_if_stale());
}

TEST_F(CampaignSpriteReloadTest, unmount_bumps_the_generation)
{
    ASSERT_TRUE(og::test162::install_sprite_carrier_campaign(kFixtureId));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kFixtureId));

    const unsigned gen_before = og::resources::sprite_source_generation();
    ASSERT_EQ(CampaignPackageIoError::None,
              unmount_campaign_package_with_error(kFixtureId));
    EXPECT_NE(gen_before, og::resources::sprite_source_generation())
        << "an unmount drops the campaign's art from the search path";
}

// The mount-order-dependent precedence from the issue text: before the fix,
// whichever of {sheet, campaign} mounted LAST answered pix/ lookups. The
// rule now is: the user's sheet wins for the exact names it ships, the
// campaign wins for everything else — in both mount orders.
TEST_F(CampaignSpriteReloadTest,
       user_sheet_wins_for_names_it_ships_even_when_campaign_mounts_later)
{
    install_sheet_fixture();
    cfg.apply_setting("graphics", "sprite_sheet", kSheetName);
    ASSERT_TRUE(apply_sprite_sheet_setting());

    ASSERT_TRUE(og::test162::install_sprite_carrier_campaign(kFixtureId));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kFixtureId));

    EXPECT_EQ(sheet_footman_bytes(),
              og::resources::read_file("pix/footman.png"))
        << "the sheet must shadow footman.png even though the campaign "
           "package mounted (and prepended) after it";
    EXPECT_EQ(og::test162::campaign_only_bytes(),
              og::resources::read_file("pix/only_in_campaign.png"))
        << "the campaign must win the names the sheet does not ship";
}

TEST_F(CampaignSpriteReloadTest,
       sheet_and_campaign_mount_order_flip_converges)
{
    // Order A (boot order — always worked): campaign, then sheet.
    ASSERT_TRUE(og::test162::install_sprite_carrier_campaign(kFixtureId));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kFixtureId));
    install_sheet_fixture();
    cfg.apply_setting("graphics", "sprite_sheet", kSheetName);
    ASSERT_TRUE(apply_sprite_sheet_setting());
    const std::vector<std::uint8_t> order_a =
        og::resources::read_file("pix/footman.png");

    // Order B (the regression case): sheet mounted, campaign re-mounted on
    // top of it.
    ASSERT_EQ(CampaignPackageIoError::None,
              unmount_campaign_package_with_error(kFixtureId));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kFixtureId));
    const std::vector<std::uint8_t> order_b =
        og::resources::read_file("pix/footman.png");

    EXPECT_EQ(sheet_footman_bytes(), order_a);
    EXPECT_EQ(order_a, order_b)
        << "precedence must not depend on mount order";
}

TEST_F(CampaignSpriteReloadTest, reassert_without_a_sheet_is_a_noop)
{
    const unsigned gen_before = og::resources::sprite_source_generation();
    EXPECT_TRUE(reassert_sprite_sheet_mount());
    EXPECT_EQ(gen_before, og::resources::sprite_source_generation());
}

TEST_F(CampaignSpriteReloadTest, reassert_with_a_sheet_bumps_the_generation)
{
    install_sheet_fixture();
    cfg.apply_setting("graphics", "sprite_sheet", kSheetName);
    ASSERT_TRUE(apply_sprite_sheet_setting());

    const unsigned gen_before = og::resources::sprite_source_generation();
    EXPECT_TRUE(reassert_sprite_sheet_mount());
    EXPECT_NE(gen_before, og::resources::sprite_source_generation());
    EXPECT_EQ(sheet_footman_bytes(),
              og::resources::read_file("pix/footman.png"))
        << "the sheet must still answer after the re-assert cycle";
}

TEST_F(CampaignSpriteReloadTest,
       apply_sprite_sheet_setting_bumps_only_on_real_change)
{
    install_sheet_fixture();
    cfg.apply_setting("graphics", "sprite_sheet", kSheetName);
    ASSERT_TRUE(apply_sprite_sheet_setting());
    const unsigned gen_mounted = og::resources::sprite_source_generation();

    // Unchanged selection: the early-out must not bump.
    ASSERT_TRUE(apply_sprite_sheet_setting());
    EXPECT_EQ(gen_mounted, og::resources::sprite_source_generation());

    // Clearing the selection unmounts: that is a change.
    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting());
    EXPECT_NE(gen_mounted, og::resources::sprite_source_generation());
}
