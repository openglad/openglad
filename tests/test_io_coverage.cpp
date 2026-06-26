#include <openglad/resources/campaign_yaml.h>
#include <openglad/resources/io_common.h>
#include <openglad/platform/game_context.h>
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


TEST(IoCoverage, io_platform_helpers_explode_and_archive_bool_wrappers)
{
    std::list<std::string> parts = explode("a::b::", ':');
    ASSERT_TRUE(!parts.empty()) << "explode should return at least one token";

    const bool unzip_ok = unzip_into_with_error("temp/no_such_archive.zip", "temp/no_such_archive_out") == ArchiveIoError::None;
    ASSERT_TRUE(!unzip_ok) << "unzip should return error for missing archive";
}
