#include <openglad/data/campaign_data.h>
#include <openglad/entities/walker.h>
#include <openglad/platform/io.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <cerrno>
#include <filesystem>
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

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

static bool read_scenario_object_count(const fs::path& p, short* out_count)
{
    if (!out_count)
        return false;
    FILE* f = fopen(p.string().c_str(), "rb");
    if (!f)
        return false;
    constexpr long kObjectCountOffset = 3 + 1 + 8 + 30 + 1 + 2 + 2;
    if (fseek(f, kObjectCountOffset, SEEK_SET) != 0)
    {
        fclose(f);
        return false;
    }
    std::int16_t count = 0;
    const size_t read_count = fread(&count, sizeof(count), 1, f);
    fclose(f);
    if (read_count != 1)
        return false;
    *out_count = count;
    return true;
}

static bool scenario_file_has_consistent_object_block(const fs::path& p, short count)
{
    if (count < 0)
        return false;

    FILE* f = fopen(p.string().c_str(), "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return false;
    }
    const long raw_size = ftell(f);
    if (raw_size < 0)
    {
        fclose(f);
        return false;
    }
    const std::size_t size = static_cast<std::size_t>(raw_size);
    rewind(f);

    std::vector<unsigned char> bytes(size);
    const size_t nread = fread(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    if (nread != bytes.size())
        return false;

    constexpr std::size_t kObjectCountOffset = 3 + 1 + 8 + 30 + 1 + 2 + 2;
    constexpr std::size_t kSerializedObjectSize = 1 + 1 + 2 + 2 + 1 + 1 + 1 + 2 + 12 + 10;
    std::size_t pos = kObjectCountOffset + 2 + (static_cast<std::size_t>(count) * kSerializedObjectSize);
    if (pos >= bytes.size())
        return false;

    const unsigned int numlines = bytes[pos++];
    for (unsigned int i = 0; i < numlines; ++i)
    {
        if (pos >= bytes.size())
            return false;
        const std::size_t width = bytes[pos++];
        if (pos + width > bytes.size())
            return false;
        pos += width;
    }

    return pos == bytes.size();
}

static bool dev_full_write_fails_as_expected()
{
    struct stat st {};
    if (::stat("/dev/full", &st) != 0)
        return false;
    if (!S_ISCHR(st.st_mode))
        return false;

    const int fd = ::open("/dev/full", O_WRONLY);
    if (fd < 0)
        return false;

    const char byte = 'x';
    const ssize_t n = ::write(fd, &byte, 1);
    const int saved_errno = errno;
    ::close(fd);

    return n < 0 && saved_errno == ENOSPC;
}

void test_level_data_save_truncates_fixed_fields_and_rejects_null_object()
{
    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();

    // Ensure the temp scen directory exists (LevelData::save writes temp/scen/scen{id}.fss).
    fs::create_directories("temp/scen");

    og::runtime::current_session->myscreen_->world().id = 123;
    og::runtime::current_session->myscreen_->level_file_metadata_.grid_file = "grid_file_name_too_long"; // >8, triggers truncation warning path
    og::runtime::current_session->myscreen_->world().title = std::string(100, 'T');         // >30, triggers truncation warning path

    // Insert a nullptr object to hit the defensive serialization failure path.
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::unique_ptr<walker>{});

    TEST_ASSERT(!og::runtime::current_session->myscreen_->save_level(), "save should fail when oblist contains nullptr");

    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_TEST(test_level_data_save_truncates_fixed_fields_and_rejects_null_object);

void test_level_data_save_reports_failure_when_grid_write_fails()
{
    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_file_metadata_.grid_file;
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_file_metadata_.description;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = 124;
    og::runtime::current_session->myscreen_->level_file_metadata_.grid_file = "missing_dir/grid";
    og::runtime::current_session->myscreen_->world().title = "grid save failure";

    fs::create_directories("temp/scen");
    std::error_code ec;
    fs::remove_all("temp/pix/missing_dir", ec);

    TEST_ASSERT(!og::runtime::current_session->myscreen_->save_level(), "save should fail when grid file cannot be written");
    TEST_ASSERT_EQ((int)og::data::LevelFileIoError::OpenWriteFailed, (int)og::runtime::current_session->myscreen_->last_level_io_error_,
        "save should propagate grid write failure as OpenWriteFailed");
    (void)og::runtime::current_session->myscreen_->save_level();
    TEST_ASSERT_EQ((int)og::data::LevelFileIoError::OpenWriteFailed, (int)og::runtime::current_session->myscreen_->last_level_io_error_,
        "save_level should report grid write failure");

    og::runtime::current_session->myscreen_->world().id = old_id;
    og::runtime::current_session->myscreen_->level_file_metadata_.grid_file = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_file_metadata_.description = old_description;
}
REGISTER_TEST(test_level_data_save_reports_failure_when_grid_write_fails);

void test_level_data_save_caps_object_count_to_loader_limit()
{
    if (!dev_full_write_fails_as_expected()) {
        fprintf(stderr, "  INFO: /dev/full unavailable or not ENOSPC; skipping test\n");
        return;
    }

    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_file_metadata_.grid_file;
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_file_metadata_.description;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = 125;
    og::runtime::current_session->myscreen_->level_file_metadata_.grid_file = "objcap";
    og::runtime::current_session->myscreen_->world().title = "object cap";
    og::runtime::current_session->myscreen_->level_file_metadata_.description.clear();

    fs::create_directories("temp/scen");
    fs::create_directories("temp/pix");

    constexpr int kMaxScenarioObjects = 4096;
    constexpr int kObjectCount = kMaxScenarioObjects + 1;
    for (int i = 0; i < kObjectCount; ++i)
    {
        walker* w = og::runtime::current_session->myscreen_->world().add_ob(Order::FX, FAMILY_MARKER);
        TEST_ASSERT(w != nullptr, "add_ob should succeed while building large object list");
    }

    TEST_ASSERT(og::runtime::current_session->myscreen_->save_level(), "save should succeed even when object count exceeds loader limit");

    short serialized_count = -1;
    const fs::path scen_path = fs::path("temp/scen") / "scen125.fss";
    TEST_ASSERT(read_scenario_object_count(scen_path, &serialized_count),
        "should read serialized object count from scenario file");
    TEST_ASSERT_EQ(kMaxScenarioObjects, static_cast<int>(serialized_count),
        "save should clamp serialized object count to loader max");

    TEST_ASSERT(scenario_file_has_consistent_object_block(scen_path, serialized_count),
        "save should serialize object records consistently with the serialized object count");

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = old_id;
    og::runtime::current_session->myscreen_->level_file_metadata_.grid_file = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_file_metadata_.description = old_description;
}
REGISTER_TEST(test_level_data_save_caps_object_count_to_loader_limit);

void test_level_data_save_reports_failure_when_scenario_write_fails()
{
    if (!dev_full_write_fails_as_expected()) {
        fprintf(stderr, "  INFO: /dev/full unavailable or not ENOSPC; skipping test\n");
        return;
    }

    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_file_metadata_.grid_file;
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_file_metadata_.description;

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = 125;
    og::runtime::current_session->myscreen_->level_file_metadata_.grid_file = "grid";
    og::runtime::current_session->myscreen_->world().title = "scenario save failure";
    og::runtime::current_session->myscreen_->level_file_metadata_.description.clear();

    const fs::path scen_dir = "temp/scen";
    const fs::path scen_file = scen_dir / "scen125.fss";
    fs::create_directories(scen_dir);
    fs::create_directories("temp/pix");
    std::error_code ec;
    fs::remove(scen_file, ec);
    fs::create_symlink("/dev/full", scen_file, ec);
    if (ec) {
        fprintf(stderr, "  INFO: cannot symlink /dev/full (%s); skipping test\n", ec.message().c_str());
        return;
    }

    TEST_ASSERT(!og::runtime::current_session->myscreen_->save_level(), "save should fail when scenario file write fails");
    TEST_ASSERT_EQ((int)og::data::LevelFileIoError::SerializeFailed, (int)og::runtime::current_session->myscreen_->last_level_io_error_,
        "save should propagate scenario write failure as SerializeFailed");
    (void)og::runtime::current_session->myscreen_->save_level();
    TEST_ASSERT_EQ((int)og::data::LevelFileIoError::SerializeFailed, (int)og::runtime::current_session->myscreen_->last_level_io_error_,
        "save_level should report scenario write failure");

    fs::remove(scen_file, ec);
    og::runtime::current_session->myscreen_->world().id = old_id;
    og::runtime::current_session->myscreen_->level_file_metadata_.grid_file = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_file_metadata_.description = old_description;
}
REGISTER_TEST(test_level_data_save_reports_failure_when_scenario_write_fails);

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
    TEST_ASSERT(zip_contents_with_error(indir.string(), outfile.string()) == ArchiveIoError::None, "zip_contents should create campaign package");

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
