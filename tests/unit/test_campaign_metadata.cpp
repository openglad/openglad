// Display-title helpers: campaign_display_title reads the "title" key from a
// package's campaign.yaml (without disturbing the active mount), and
// scenario_display_name formats "N. Title" from the mounted campaign's
// scen<N>.fss header. Both fall back to raw-id / "N. Level N" on any failure.

#include <gtest/gtest.h>

#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_file_io.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>

namespace {

constexpr const char* kGladiatorId = "org.openglad.gladiator";
constexpr const char* kCtfId = "org.openglad.ctf";

// Mounts the shipped Gladiator campaign for the duration of one test and
// restores the previous mount in teardown so sibling tests see an untouched
// state. Caches are cleared on both ends so memoized titles never leak
// across tests (shuffle safety).
class CampaignMetadataTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        og::data::clear_campaign_metadata_cache();
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(kGladiatorId))
            << "builtin/org.openglad.gladiator.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error(get_mounted_campaign());
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
        og::data::clear_campaign_metadata_cache();
    }

    // Packs a one-file .glad (campaign.yaml with the given content) into the
    // user campaigns dir. Returns false on packing failure.
    static bool install_fake_package(const std::string& id,
                                     const std::string& yaml_content)
    {
        namespace fs = std::filesystem;
        const fs::path staging =
            fs::path(get_user_path()) / "meta_test_staging" / id;
        std::error_code ec;
        fs::create_directories(staging, ec);
        if (ec)
            return false;
        if (!yaml_content.empty())
        {
            std::ofstream out(staging / "campaign.yaml");
            out << yaml_content;
            if (!out)
                return false;
        }
        const fs::path archive =
            fs::path(get_user_path()) / "campaigns" / (id + ".glad");
        return zip_contents_with_error(staging.string(), archive.string()) ==
            ArchiveIoError::None;
    }

    static void remove_fake_package(const std::string& id)
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::remove(fs::path(get_user_path()) / "campaigns" / (id + ".glad"), ec);
        fs::remove_all(fs::path(get_user_path()) / "meta_test_staging", ec);
        og::data::clear_campaign_metadata_cache();
    }

private:
    std::string previous_;
};

TEST_F(CampaignMetadataTest, mounted_campaign_title_from_yaml)
{
    ASSERT_EQ("Gladiator", og::data::campaign_display_title(kGladiatorId));
}

TEST_F(CampaignMetadataTest, unmounted_campaign_title_via_private_mount)
{
    ASSERT_EQ("Capture the Flag", og::data::campaign_display_title(kCtfId));

    // The private-mountpoint lookup must not disturb the active mount.
    ASSERT_EQ(kGladiatorId, get_mounted_campaign());
    ASSERT_EQ("Gladiator", og::data::campaign_display_title(kGladiatorId));
    ASSERT_NE("none", og::data::load_scenario_title("scen1"))
        << "active campaign mount should still serve scenario files";
}

TEST_F(CampaignMetadataTest, unknown_campaign_falls_back_to_raw_id)
{
    const std::string id = "org.openglad.does_not_exist";
    ASSERT_EQ(id, og::data::campaign_display_title(id));
    ASSERT_EQ("", og::data::campaign_display_title(""));
}

TEST_F(CampaignMetadataTest, package_without_usable_title_falls_back_to_raw_id)
{
    // No campaign.yaml at all.
    const std::string no_yaml = "org.openglad.test_no_yaml";
    ASSERT_TRUE(install_fake_package(no_yaml, ""));
    EXPECT_EQ(no_yaml, og::data::campaign_display_title(no_yaml));
    remove_fake_package(no_yaml);

    // campaign.yaml present but no title key.
    const std::string no_title = "org.openglad.test_no_title";
    ASSERT_TRUE(install_fake_package(no_title,
        "format_version: 1\nversion: 1\n"));
    EXPECT_EQ(no_title, og::data::campaign_display_title(no_title));
    remove_fake_package(no_title);

    // title key present but empty.
    const std::string empty_title = "org.openglad.test_empty_title";
    ASSERT_TRUE(install_fake_package(empty_title,
        "format_version: 1\ntitle: ''\nversion: 1\n"));
    EXPECT_EQ(empty_title, og::data::campaign_display_title(empty_title));
    remove_fake_package(empty_title);
}

TEST_F(CampaignMetadataTest, mounted_package_without_title_falls_back_to_raw_id)
{
    // Exercise the fast-path fallback: the broken package is the ACTIVE mount.
    const std::string no_title = "org.openglad.test_mounted_no_title";
    ASSERT_TRUE(install_fake_package(no_title,
        "format_version: 1\nversion: 1\n"));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(no_title));
    EXPECT_EQ(no_title, og::data::campaign_display_title(no_title));

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kGladiatorId));
    remove_fake_package(no_title);
}

TEST_F(CampaignMetadataTest, scenario_display_name_uses_fss_title)
{
    const std::string expected_title = og::data::load_scenario_title("scen1");
    ASSERT_NE("none", expected_title);
    ASSERT_FALSE(expected_title.empty());
    ASSERT_EQ(std::format("1. {}", expected_title),
              og::data::scenario_display_name(1));
}

TEST_F(CampaignMetadataTest, missing_scenario_falls_back_to_level_number)
{
    ASSERT_EQ("9876. Level 9876", og::data::scenario_display_name(9876));
}

TEST_F(CampaignMetadataTest, scenario_names_are_keyed_by_mounted_campaign)
{
    const std::string gladiator_name = og::data::scenario_display_name(1);
    ASSERT_NE("1. Level 1", gladiator_name);

    // CTF levels start at 500, so scen1 only resolves under gladiator. A
    // stale cache entry keyed on number alone would leak the gladiator title.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kCtfId));
    EXPECT_EQ("1. Level 1", og::data::scenario_display_name(1));

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kGladiatorId));
    EXPECT_EQ(gladiator_name, og::data::scenario_display_name(1));
}

TEST_F(CampaignMetadataTest, failed_lookup_heals_once_package_is_mounted)
{
    // A lookup before the package exists memoizes the raw-id fallback...
    const std::string id = "org.openglad.test_late_install";
    ASSERT_EQ(id, og::data::campaign_display_title(id));

    // ...but mounting the package after an out-of-band install must serve
    // the real title, not the stale negative entry.
    ASSERT_TRUE(install_fake_package(id,
        "format_version: 1\ntitle: Late Arrival\nversion: 1\n"));
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(id));
    EXPECT_EQ("Late Arrival", og::data::campaign_display_title(id));

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(kGladiatorId));
    remove_fake_package(id);
}

TEST_F(CampaignMetadataTest, cache_survives_repeat_calls_and_clear)
{
    const std::string title_first = og::data::campaign_display_title(kCtfId);
    const std::string scen_first = og::data::scenario_display_name(1);

    // Memoized second call.
    ASSERT_EQ(title_first, og::data::campaign_display_title(kCtfId));
    ASSERT_EQ(scen_first, og::data::scenario_display_name(1));

    // Fresh lookups after invalidation return the same values.
    og::data::clear_campaign_metadata_cache();
    ASSERT_EQ(title_first, og::data::campaign_display_title(kCtfId));
    ASSERT_EQ(scen_first, og::data::scenario_display_name(1));
}

} // namespace
