#include <openglad/runtime/level_runtime_data.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/io.h>
#include <openglad/interface/screen.h>
#include "test_framework.h"

#include <array>
#include <cerrno>
#include <cstring>
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

static void append_i16_native(std::string& out, std::int16_t value)
{
    out.append(reinterpret_cast<const char*>(&value), sizeof(value));
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

    // Ensure the temp scen directory exists (LevelRuntimeData::save writes temp/scen/scen{id}.fss).
    fs::create_directories("temp/scen");

    og::runtime::current_session->myscreen_->world().id = 123;
    og::runtime::current_session->myscreen_->level_grid_file() = "grid_file_name_too_long"; // >8, triggers truncation warning path
    og::runtime::current_session->myscreen_->world().title = std::string(100, 'T');         // >30, triggers truncation warning path

    // Insert a nullptr object to hit the defensive serialization failure path.
    og::runtime::current_session->myscreen_->oblist().push_back(std::unique_ptr<walker>{});

    TEST_ASSERT(!og::runtime::current_session->myscreen_->save_level(), "save should fail when oblist contains nullptr");

    og::runtime::current_session->myscreen_->world().delete_objects();
}
REGISTER_TEST(test_level_data_save_truncates_fixed_fields_and_rejects_null_object);

void test_level_data_save_reports_failure_when_grid_write_fails()
{
    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_grid_file();
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_description();

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = 124;
    og::runtime::current_session->myscreen_->level_grid_file() = "missing_dir/grid";
    og::runtime::current_session->myscreen_->world().title = "grid save failure";

    fs::create_directories("temp/scen");
    std::error_code ec;
    fs::remove_all("temp/pix/missing_dir", ec);

    TEST_ASSERT(!og::runtime::current_session->myscreen_->save_level(), "save should fail when grid file cannot be written");
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::OpenWriteFailed, (int)og::runtime::current_session->myscreen_->level_io_error(),
        "save should propagate grid write failure as OpenWriteFailed");
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::OpenWriteFailed, (int)og::runtime::current_session->myscreen_->save_level_with_error(),
        "save_with_error should report grid write failure");

    og::runtime::current_session->myscreen_->world().id = old_id;
    og::runtime::current_session->myscreen_->level_grid_file() = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_description() = old_description;
}
REGISTER_TEST(test_level_data_save_reports_failure_when_grid_write_fails);

void test_level_data_save_reports_failure_when_grid_write_is_short()
{
    if (!dev_full_write_fails_as_expected()) {
        fprintf(stderr, "  INFO: /dev/full unavailable or not ENOSPC; skipping test\n");
        return;
    }

    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_grid_file();
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_description();

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = 126;
    og::runtime::current_session->myscreen_->level_grid_file() = "grid_short_write";
    og::runtime::current_session->myscreen_->world().title = "grid short write";

    fs::create_directories("temp/scen");
    fs::create_directories("temp/pix");

    const fs::path grid_path = fs::path("temp/pix") / "grid_short_write.pix";
    std::error_code ec;
    fs::remove(grid_path, ec);
    fs::create_symlink("/dev/full", grid_path, ec);
    if (ec) {
        fprintf(stderr, "  INFO: cannot symlink /dev/full (%s); skipping test\n", ec.message().c_str());
        og::runtime::current_session->myscreen_->world().id = old_id;
        og::runtime::current_session->myscreen_->level_grid_file() = old_grid_file;
        og::runtime::current_session->myscreen_->world().title = old_title;
        og::runtime::current_session->myscreen_->level_description() = old_description;
        return;
    }

    TEST_ASSERT(!og::runtime::current_session->myscreen_->save_level(),
                "save should fail when grid file write returns short");
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::OpenWriteFailed,
                   (int)og::runtime::current_session->myscreen_->level_io_error(),
                   "save should propagate short grid write as OpenWriteFailed");
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::OpenWriteFailed,
                   (int)og::runtime::current_session->myscreen_->save_level_with_error(),
                   "save_with_error should report short grid write as OpenWriteFailed");

    fs::remove(grid_path, ec);
    og::runtime::current_session->myscreen_->world().id = old_id;
    og::runtime::current_session->myscreen_->level_grid_file() = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_description() = old_description;
}
REGISTER_TEST(test_level_data_save_reports_failure_when_grid_write_is_short);

void test_level_data_save_caps_object_count_to_loader_limit()
{
    if (!dev_full_write_fails_as_expected()) {
        fprintf(stderr, "  INFO: /dev/full unavailable or not ENOSPC; skipping test\n");
        return;
    }

    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_grid_file();
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_description();

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = 125;
    og::runtime::current_session->myscreen_->level_grid_file() = "objcap";
    og::runtime::current_session->myscreen_->world().title = "object cap";
    og::runtime::current_session->myscreen_->level_description().clear();

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
    og::runtime::current_session->myscreen_->level_grid_file() = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_description() = old_description;
}
REGISTER_TEST(test_level_data_save_caps_object_count_to_loader_limit);

void test_level_data_save_reports_failure_when_scenario_write_fails()
{
    if (!dev_full_write_fails_as_expected()) {
        fprintf(stderr, "  INFO: /dev/full unavailable or not ENOSPC; skipping test\n");
        return;
    }

    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_grid_file();
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_description();

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = 125;
    og::runtime::current_session->myscreen_->level_grid_file() = "grid";
    og::runtime::current_session->myscreen_->world().title = "scenario save failure";
    og::runtime::current_session->myscreen_->level_description().clear();

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
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::SerializeFailed, (int)og::runtime::current_session->myscreen_->level_io_error(),
        "save should propagate scenario write failure as SerializeFailed");
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::SerializeFailed, (int)og::runtime::current_session->myscreen_->save_level_with_error(),
        "save_with_error should report scenario write failure");

    fs::remove(scen_file, ec);
    og::runtime::current_session->myscreen_->world().id = old_id;
    og::runtime::current_session->myscreen_->level_grid_file() = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_description() = old_description;
}
REGISTER_TEST(test_level_data_save_reports_failure_when_scenario_write_fails);

void test_level_data_load_failure_preserves_existing_world_state()
{
    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_grid_file();
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_description();

    og::runtime::current_session->myscreen_->world().create_new_grid();
    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = 19991;
    og::runtime::current_session->myscreen_->world().title = "preserve me";
    og::runtime::current_session->myscreen_->level_grid_file() = "preserve_grid";
    og::runtime::current_session->myscreen_->level_description().clear();
    og::runtime::current_session->myscreen_->level_description().push_back("keep-description");

    std::error_code ec;
    fs::remove(fs::path("scen") / "scen19991.fss", ec);

    walker* sentinel = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(sentinel != nullptr, "fixture object should be created");
    if (!sentinel) {
        og::runtime::current_session->myscreen_->world().id = old_id;
        og::runtime::current_session->myscreen_->level_grid_file() = old_grid_file;
        og::runtime::current_session->myscreen_->world().title = old_title;
        og::runtime::current_session->myscreen_->level_description() = old_description;
        return;
    }
    sentinel->team_num = 3;
    sentinel->setxy(48, 64);

    const std::size_t before_ob_count = og::runtime::current_session->myscreen_->oblist().size();
    const std::size_t before_fx_count = og::runtime::current_session->myscreen_->fxlist().size();
    const std::size_t before_weap_count = og::runtime::current_session->myscreen_->weaplist().size();

    TEST_ASSERT(!og::runtime::current_session->myscreen_->load_level(),
                "load should fail for missing scenario");
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::OpenReadFailed,
                   (int)og::runtime::current_session->myscreen_->level_io_error(),
                   "missing scenario should report OpenReadFailed");

    TEST_ASSERT_EQ(before_ob_count, og::runtime::current_session->myscreen_->oblist().size(),
                   "failed load should preserve oblist");
    TEST_ASSERT_EQ(before_fx_count, og::runtime::current_session->myscreen_->fxlist().size(),
                   "failed load should preserve fxlist");
    TEST_ASSERT_EQ(before_weap_count, og::runtime::current_session->myscreen_->weaplist().size(),
                   "failed load should preserve weaplist");
    TEST_ASSERT(og::runtime::current_session->myscreen_->world().title == "preserve me",
                "failed load should preserve world title");
    TEST_ASSERT(og::runtime::current_session->myscreen_->level_grid_file() == "preserve_grid",
                "failed load should preserve grid file");
    TEST_ASSERT(og::runtime::current_session->myscreen_->get_level_description_line(0) == "keep-description",
                "failed load should preserve description");

    walker* after = og::runtime::current_session->myscreen_->oblist().empty()
        ? nullptr
        : og::runtime::current_session->myscreen_->oblist().front().get();
    TEST_ASSERT(after == sentinel, "failed load should preserve existing object pointer");
    if (after)
    {
        TEST_ASSERT_EQ(3, (int)after->team_num, "failed load should preserve object fields");
        TEST_ASSERT_EQ(48, (int)after->xpos, "failed load should preserve object x");
        TEST_ASSERT_EQ(64, (int)after->ypos, "failed load should preserve object y");
    }

    og::runtime::current_session->myscreen_->world().delete_objects();
    og::runtime::current_session->myscreen_->world().id = old_id;
    og::runtime::current_session->myscreen_->level_grid_file() = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_description() = old_description;
}
REGISTER_TEST(test_level_data_load_failure_preserves_existing_world_state);

void test_level_data_load_reports_parse_failed_when_grid_pix_missing()
{
    constexpr int kMissingGridScenarioId = 19992;
    constexpr std::array<char, 8> kMissingGridName = {'n', 'o', 'g', 'r', 'i', 'd', '9', 'x'};

    const int old_id = og::runtime::current_session->myscreen_->world().id;
    const std::string old_grid_file = og::runtime::current_session->myscreen_->level_grid_file();
    const std::string old_title = og::runtime::current_session->myscreen_->world().title;
    const std::list<std::string> old_description = og::runtime::current_session->myscreen_->level_description();

    std::string scenario_bytes;
    scenario_bytes.append("FSS", 3);
    scenario_bytes.push_back(static_cast<char>(9));
    scenario_bytes.append(kMissingGridName.data(), kMissingGridName.size());
    std::array<char, 30> title{};
    std::memcpy(title.data(), "Missing Grid", std::strlen("Missing Grid"));
    scenario_bytes.append(title.data(), title.size());
    scenario_bytes.push_back(1); // scenario type
    append_i16_native(scenario_bytes, 1);    // par value
    append_i16_native(scenario_bytes, 4000); // time bonus limit
    append_i16_native(scenario_bytes, 0);    // object count
    scenario_bytes.push_back(0);             // description line count

    const fs::path scen_path = fs::path("scen") / "scen19992.fss";
    TEST_ASSERT(write_file_bytes(scen_path, scenario_bytes),
                "missing-grid scenario fixture should be written");

    std::error_code ec;
    fs::remove(fs::path("pix") / "nogrid9x.pix", ec);
    fs::remove("nogrid9x.pix", ec);

    og::runtime::current_session->myscreen_->world().id = kMissingGridScenarioId;
    og::runtime::current_session->myscreen_->world().title = "before missing grid load";
    og::runtime::current_session->myscreen_->level_grid_file() = "preserve_grid";
    og::runtime::current_session->myscreen_->level_description().clear();
    og::runtime::current_session->myscreen_->level_description().push_back("preserve-line");

    TEST_ASSERT(!og::runtime::current_session->myscreen_->load_level(),
                "load should fail when referenced grid pix is missing");
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::ParseFailed,
                   (int)og::runtime::current_session->myscreen_->level_io_error(),
                   "missing grid pix should map to ParseFailed");
    TEST_ASSERT_EQ((int)LevelRuntimeData::IoError::ParseFailed,
                   (int)og::runtime::current_session->myscreen_->load_level_with_error(),
                   "load_with_error should report ParseFailed for missing grid pix");

    fs::remove(scen_path, ec);
    og::runtime::current_session->myscreen_->world().id = old_id;
    og::runtime::current_session->myscreen_->level_grid_file() = old_grid_file;
    og::runtime::current_session->myscreen_->world().title = old_title;
    og::runtime::current_session->myscreen_->level_description() = old_description;
}
REGISTER_TEST(test_level_data_load_reports_parse_failed_when_grid_pix_missing);

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
