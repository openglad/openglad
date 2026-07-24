#include <openglad/resources/campaign_yaml.h>
#include <openglad/resources/io_common.h>
#include <openglad/platform/game_context.h>
#include "test_save_state_guard.h"
#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>
#include <string>

TEST(IoCoverage, campaign_yaml_reports_error_on_invalid_stream)
{
    namespace fs = std::filesystem;
    fs::create_directories("temp");
    const fs::path path = fs::path("temp") / "invalid_campaign.yaml";
    std::ofstream out(path);
    out << "title: Broken\nroot: [1, 2\n";
    out.close();

    og::data::CampaignYaml metadata;
    const auto result = og::data::read_campaign_yaml(path.string().c_str(), metadata);

    ASSERT_EQ(og::data::CampaignYamlReadResult::ParseFailed, result);
    ASSERT_TRUE(metadata.saw_title) << "fields before a parse error should remain available";
    ASSERT_EQ("Broken", metadata.title);
}

TEST(IoCoverage, campaign_yaml_preharvest_retains_only_simple_top_level_pairs)
{
    namespace fs = std::filesystem;
    fs::create_directories("temp");
    const fs::path path = fs::path("temp") / "campaign_preharvest_edges.yaml";
    og::test::ScopedPhysicalFileState path_state(path);
    ASSERT_TRUE(path_state.ready()) << path_state.error().message();
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out << "title : Harvested Title \t\r\n"
           "bareword\n"
           ": empty-key\n"
           "bad key: ignored\n"
           "broken: [one, two\n";
    out.close();

    og::data::CampaignYaml metadata;
    EXPECT_EQ(og::data::CampaignYamlReadResult::ParseFailed,
              og::data::read_campaign_yaml(path.string().c_str(), metadata));
    EXPECT_TRUE(metadata.saw_title)
        << "simple fields parsed before a YAML error remain useful to callers";
    EXPECT_EQ("Harvested Title", metadata.title)
        << "preharvest trims ASCII spaces, tabs, and CR";
    EXPECT_FALSE(metadata.saw_authors);

}

TEST(IoCoverage, campaign_yaml_handles_nested_sequences_and_alias_events)
{
    namespace fs = std::filesystem;
    fs::create_directories("temp");
    const fs::path path = fs::path("temp") / "campaign_structures.yaml";
    og::test::ScopedPhysicalFileState path_state(path);
    ASSERT_TRUE(path_state.ready()) << path_state.error().message();
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good());
    out << "title: Structural Campaign\n"
           "nested:\n"
           "  child: ignored\n"
           "items: [one, two]\n"
           "anchor: &shared value\n"
           "authors: *shared\n";
    out.close();

    og::data::CampaignYaml metadata;
    ASSERT_EQ(og::data::CampaignYamlReadResult::Ok,
              og::data::read_campaign_yaml(path.string().c_str(), metadata));
    EXPECT_TRUE(metadata.saw_title);
    EXPECT_EQ("Structural Campaign", metadata.title);
    EXPECT_TRUE(metadata.saw_authors);
    EXPECT_EQ("*shared", metadata.authors)
        << "the recovery preharvest preserves the literal alias token while "
           "the event parser safely consumes the alias event";

}

#if defined(__linux__)
TEST(IoCoverage, campaign_yaml_reports_output_device_write_failure)
{
    namespace fs = std::filesystem;
    const fs::path dev_full = "/dev/full";
    ASSERT_TRUE(fs::exists(dev_full));
    ASSERT_TRUE(fs::is_character_file(dev_full));

    og::data::CampaignYaml metadata;
    metadata.title = "Output failure";
    metadata.version = "1";
    metadata.first_level = 1;
    metadata.description.assign(1024 * 1024, 'x');

    EXPECT_EQ(og::data::CampaignYamlWriteResult::WriteFailed,
              og::data::write_campaign_yaml_with_result(
                  dev_full.string().c_str(), metadata));
}
#endif


TEST(IoCoverage, io_platform_helpers_explode_and_archive_bool_wrappers)
{
    std::list<std::string> parts = explode("a::b::", ':');
    ASSERT_TRUE(!parts.empty()) << "explode should return at least one token";

    const bool unzip_ok = unzip_into_with_error("temp/no_such_archive.zip", "temp/no_such_archive_out") == ArchiveIoError::None;
    ASSERT_TRUE(!unzip_ok) << "unzip should return error for missing archive";
}
