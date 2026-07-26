/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/zip_api.h>

#include <filesystem>
#include <fstream>
#include <string>

std::string get_asset_path();

// Campaign-embedded class packs: a .glad carrying packs/<id>/scripts/*.lua
// merges into the virtual packs/ tree on mount, and the pack-script registry
// follows mounts (refresh on mount/unmount).

namespace {

constexpr const char* kPackCampaignId = "org.test.packcarrier";
constexpr const char* kEmbeddedScript = "og.log('embedded pack loaded')\n";

bool script_registered(const char* pack_id)
{
    for (const auto& s : og::script::pack_scripts())
        if (s.pack_id == pack_id)
            return true;
    return false;
}

class CampaignPacksTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        // Unit binaries skip io_init's asset mounts; the shipped packs dir
        // must be reachable for the core-pack assertions (append mount is
        // idempotent).
        (void)og::resources::mount((get_asset_path() + "packs/").c_str(),
                                   "packs/", 1);
        og::resources::refresh_pack_scripts();
        previous_ = get_mounted_campaign();
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        remove_fake_package();
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

    static bool install_pack_campaign()
    {
        namespace fs = std::filesystem;
        const fs::path staging =
            fs::path(get_user_path()) / "pack_test_staging" / kPackCampaignId;
        std::error_code ec;
        fs::create_directories(staging / "packs" / "embeddedtest" / "scripts",
                               ec);
        if (ec)
            return false;
        {
            std::ofstream out(staging / "campaign.yaml");
            out << "format: 1\ntitle: Pack Carrier\nfirst_level: 1\n";
            if (!out)
                return false;
        }
        {
            std::ofstream out(staging / "packs" / "embeddedtest" / "scripts" /
                              "hello.lua");
            out << kEmbeddedScript;
            if (!out)
                return false;
        }
        const fs::path archive = fs::path(get_user_path()) / "campaigns" /
                                 (std::string(kPackCampaignId) + ".glad");
        return zip_contents_with_error(staging.string(), archive.string()) ==
               ArchiveIoError::None;
    }

    static void remove_fake_package()
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove(fs::path(get_user_path()) / "campaigns" /
                       (std::string(kPackCampaignId) + ".glad"),
                   ec);
        fs::remove_all(fs::path(get_user_path()) / "pack_test_staging", ec);
    }

private:
    std::string previous_;
};

}  // namespace

TEST_F(CampaignPacksTest, embedded_pack_scripts_follow_campaign_mounts)
{
    ASSERT_TRUE(install_pack_campaign());
    EXPECT_FALSE(script_registered("embeddedtest"));

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kPackCampaignId));
    EXPECT_TRUE(script_registered("embeddedtest"))
        << "campaign mount must pull embedded pack scripts into the registry";

    ASSERT_EQ(CampaignPackageIoError::None,
              unmount_campaign_package_with_error(kPackCampaignId));
    EXPECT_FALSE(script_registered("embeddedtest"))
        << "campaign unmount must drop its embedded pack scripts";
}

TEST_F(CampaignPacksTest, core_pack_survives_campaign_cycling)
{
    ASSERT_TRUE(install_pack_campaign());
    const unsigned gen_before = og::script::pack_scripts_generation();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kPackCampaignId));
    EXPECT_TRUE(script_registered("core"))
        << "the shipped core pack stays registered across campaign mounts";
    EXPECT_NE(gen_before, og::script::pack_scripts_generation())
        << "mount must bump the generation so world VMs rebuild";
}
