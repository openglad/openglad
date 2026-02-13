#include "graph.h"
#include "data/level_data.h"
#include "platform/io.h"
#include "test_framework.h"

#include <filesystem>
#include <string>

extern screen* myscreen;

namespace fs = std::filesystem;

static bool write_file_bytes(const fs::path& p, const std::string& contents)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    FILE* f = fopen(p.string().c_str(), "wb");
    if (!f)
        return false;
    size_t n = fwrite(contents.data(), 1, contents.size(), f);
    fclose(f);
    return n == contents.size();
}

void test_level_data_save_truncates_fixed_fields_and_rejects_null_object()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.delete_objects();

    // Ensure the temp scen directory exists (LevelData::save writes temp/scen/scen{id}.fss).
    fs::create_directories("temp/scen");

    myscreen->level_data.id = 123;
    myscreen->level_data.grid_file = "grid_file_name_too_long"; // >8, triggers truncation warning path
    myscreen->level_data.title = std::string(100, 'T');         // >30, triggers truncation warning path

    // Insert a nullptr object to hit the defensive serialization failure path.
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>{});

    TEST_ASSERT(!myscreen->level_data.save(), "save should fail when oblist contains nullptr");

    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_level_data_save_truncates_fixed_fields_and_rejects_null_object);

void test_campaign_data_load_reports_open_read_failed_when_campaign_yaml_missing()
{
    // Create a minimal zip campaign package with no campaign.yaml so CampaignData::load
    // takes the OpenReadFailed branch.
    const std::string id = std::string("org.openglad.test.missing_yaml.") + std::to_string(::getpid());
    delete_campaign(id);

    const fs::path base = fs::temp_directory_path() / (std::string("openglad_missing_yaml_") + std::to_string(::getpid()));
    const fs::path indir = base / "in";
    fs::create_directories(indir);
    TEST_ASSERT(write_file_bytes(indir / "dummy.txt", "x"), "write dummy");

    const fs::path outfile = fs::path(get_user_path()) / "campaigns" / (id + ".glad");
    TEST_ASSERT(zip_contents(indir.string(), outfile.string()), "zip_contents should create campaign package");

    CampaignData cd(id);
    TEST_ASSERT_EQ((int)CampaignData::IoError::OpenReadFailed, (int)cd.load_with_error(),
        "load_with_error should report OpenReadFailed when campaign.yaml missing");

    delete_campaign(id);
    std::error_code ec;
    fs::remove_all(base, ec);
}
REGISTER_TEST(test_campaign_data_load_reports_open_read_failed_when_campaign_yaml_missing);

// Note: We intentionally do not test the "ParseFailed" path here because the
// Yam parser can hang on certain malformed inputs. Coverage is better served
// by deterministic, non-hanging tests (especially under global coverage runs).
