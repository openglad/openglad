#include <openglad/gameplay/pixie_data.h>
#include <openglad/resources/campaign_yaml.h>
#include <openglad/core/test_trace.h>
#include <openglad/resources/og_file.h>
#include <openglad/legacy/base.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/io.h>
#include <openglad/resources/physfs_api.h>
#include <openglad/resources/zip_api.h>
#include <openglad/resources/io_common.h>

#include "test_save_state_guard.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <physfs.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "lodepng.h"

std::string get_asset_path();

namespace {

class ScopedEnvVar
{
public:
    explicit ScopedEnvVar(const char* name) : name_(name)
    {
        if (const char* value = std::getenv(name_))
        {
            had_value_ = true;
            old_value_ = value;
        }
    }

    ~ScopedEnvVar()
    {
        if (had_value_)
            set(old_value_);
        else
            clear();
    }

    void set(const std::string& value) const
    {
#ifdef _WIN32
        (void)_putenv_s(name_, value.c_str());
#else
        (void)setenv(name_, value.c_str(), 1);
#endif
    }

    void clear() const
    {
#ifdef _WIN32
        (void)_putenv_s(name_, "");
#else
        (void)unsetenv(name_);
#endif
    }

private:
    const char* name_;
    bool had_value_ = false;
    std::string old_value_;
};

class ScopedTraceBuffer
{
public:
    ScopedTraceBuffer() { trace_clear(); }
    ~ScopedTraceBuffer() { trace_clear(); }

    ScopedTraceBuffer(const ScopedTraceBuffer&) = delete;
    ScopedTraceBuffer& operator=(const ScopedTraceBuffer&) = delete;
};

std::vector<unsigned char> make_test_indexed_png(unsigned width,
                                                  unsigned height,
                                                  unsigned palette_entries)
{
    lodepng::State state;
    state.info_raw.colortype = LCT_PALETTE;
    state.info_raw.bitdepth = 8;
    state.info_png.color.colortype = LCT_PALETTE;
    state.info_png.color.bitdepth = 8;
    state.encoder.auto_convert = 0;

    for (unsigned i = 0; i < palette_entries; ++i)
    {
        const auto component = static_cast<unsigned char>(i);
        EXPECT_EQ(0u, lodepng_palette_add(&state.info_raw, component,
                                          component, component, 255));
        EXPECT_EQ(0u, lodepng_palette_add(&state.info_png.color, component,
                                          component, component, 255));
    }

    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<unsigned char>(i % palette_entries);

    std::vector<unsigned char> encoded;
    EXPECT_EQ(0u, lodepng::encode(encoded, pixels, width, height, state));
    return encoded;
}

void write_binary_file(const std::filesystem::path& path,
                       const std::vector<unsigned char>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good()) << path;
    if (!bytes.empty())
    {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    ASSERT_TRUE(out.good()) << path;
}

void write_text_file(const std::filesystem::path& path, std::string_view text)
{
    write_binary_file(
        path, std::vector<unsigned char>(text.begin(), text.end()));
}

} // namespace

TEST(IoPlatformCoverage, user_path_and_open_write_stdio_fallback_are_exact)
{
    namespace fs = std::filesystem;
    const fs::path output =
        fs::path(get_user_path()) / "platform_io_second_pass" /
        "absolute_stdio_fallback.bin";
    og::test::ScopedPhysicalFileState output_state(output);
    ASSERT_TRUE(output_state.ready()) << output_state.error().message();
    std::error_code ec;
    fs::create_directories(output.parent_path(), ec);
    ASSERT_FALSE(ec) << ec.message();

    ScopedEnvVar config_dir("OPENGLAD_CONFIG_DIR");
    ScopedEnvVar home("HOME");
    config_dir.clear();
    home.clear();
    EXPECT_EQ("./", get_user_path())
        << "without an explicit config directory or HOME, the cwd is used";

    static constexpr unsigned char payload[] = {0x4f, 0x47, 0x21, 0x7f};
    {
        IostreamPtr out{open_write_file(output.string().c_str())};
        ASSERT_TRUE(out != nullptr)
            << "an absolute native path must use the stdio fallback";
        ASSERT_EQ(sizeof(payload), SDL_WriteIO(out.get(), payload,
                                               sizeof(payload)));
        ASSERT_TRUE(SDL_FlushIO(out.get()));
    }

    std::ifstream in(output, std::ios::binary);
    ASSERT_TRUE(in.good());
    const std::vector<unsigned char> actual{
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>()};
    EXPECT_EQ(std::vector<unsigned char>(std::begin(payload),
                                         std::end(payload)),
              actual);
}

TEST(IoPlatformCoverage,
     io_init_rejects_double_initialization_without_state_mutation)
{
    ASSERT_TRUE(og::resources::is_initialized());
    og::test::ScopedCampaignMountState mount_state;
    const std::string mounted_before = get_mounted_campaign();
    std::string argv0 =
        (std::filesystem::path(get_asset_path()) / "og_test_io").string();
    char* argv[] = {argv0.data()};

    bool threw = false;
    std::string message;
    try
    {
        io_init(1, argv);
    }
    catch (const std::runtime_error& error)
    {
        threw = true;
        message = error.what();
    }

    EXPECT_TRUE(threw);
    EXPECT_EQ(0u, message.find("Fatal: Failed to initialize PhysFS:"))
        << message;
    EXPECT_TRUE(og::resources::is_initialized())
        << "rejecting a second initialization leaves the live instance intact";
    EXPECT_EQ(mounted_before, get_mounted_campaign());
}

TEST(IoPlatformCoverage,
     io_init_reports_invalid_write_directory_and_recovers_canonical_state)
{
    namespace fs = std::filesystem;
    ASSERT_TRUE(og::resources::is_initialized());
    og::test::ScopedCampaignMountState mount_state;

    const fs::path blocker =
        fs::path(get_user_path()) / "platform_io_second_pass" /
        "write_dir_parent_blocker";
    og::test::ScopedPhysicalFileState blocker_state(blocker);
    ASSERT_TRUE(blocker_state.ready()) << blocker_state.error().message();
    std::error_code ec;
    fs::create_directories(blocker.parent_path(), ec);
    ASSERT_FALSE(ec) << ec.message();
    {
        std::ofstream out(blocker, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << "parent is intentionally a regular file";
    }
    const fs::path invalid_config = blocker / "nested_config";

    std::string argv0 =
        (fs::path(get_asset_path()) / "og_test_io").string();
    char* argv[] = {argv0.data()};
    bool threw = false;
    bool initialized_after_failure = false;
    std::string failure_message;
    std::string write_dir_after_failure;
    {
        ScopedEnvVar config_dir("OPENGLAD_CONFIG_DIR");
        config_dir.set(invalid_config.string());
        set_mounted_campaign_for_testing("");
        io_exit();

        try
        {
            io_init(1, argv);
        }
        catch (const std::runtime_error& error)
        {
            threw = true;
            failure_message = error.what();
        }
        initialized_after_failure = og::resources::is_initialized();
        if (const char* write_dir = PHYSFS_getWriteDir())
            write_dir_after_failure = write_dir;
        if (initialized_after_failure)
            io_exit();
    }

    // Re-establish the integration test process contract before making any
    // assertions, so even a failed expectation cannot strand sibling tests.
    set_mounted_campaign_for_testing("");
    bool recovered = true;
    std::string recovery_error;
    try
    {
        io_init(1, argv);
    }
    catch (const std::runtime_error& error)
    {
        recovered = false;
        recovery_error = error.what();
    }

    ASSERT_TRUE(recovered) << recovery_error;
    EXPECT_TRUE(threw);
    EXPECT_NE(std::string::npos,
              failure_message.find(
                  "Fatal: Failed to set write directory " +
                  invalid_config.string()))
        << failure_message;
    EXPECT_TRUE(initialized_after_failure)
        << "PhysFS initialization succeeds before write-dir validation fails";
    EXPECT_TRUE(write_dir_after_failure.empty());
    EXPECT_EQ("gladiator", get_mounted_campaign());

    std::ifstream blocker_in(blocker, std::ios::binary);
    ASSERT_TRUE(blocker_in.good());
    const std::string blocker_bytes{
        std::istreambuf_iterator<char>(blocker_in),
        std::istreambuf_iterator<char>()};
    EXPECT_EQ("parent is intentionally a regular file", blocker_bytes);
    EXPECT_FALSE(fs::exists(invalid_config));
}

TEST(IoPlatformCoverage, og_file_read_write_seek_and_pixie_paths)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov";
    const fs::path bin_path = tmp_dir / "rw.bin";
    const fs::path pix_ok = tmp_dir / "ok.png";
    const fs::path pix_bad = tmp_dir / "bad.png";

    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    auto out = og::io::og_open_write(bin_path.string().c_str());
    ASSERT_TRUE(out != nullptr) << "og_open_write should create file";
    if (!out)
        return;

    const unsigned char payload[] = {1, 2, 3, 4, 5};
    ASSERT_TRUE(og::io::og_write_exact(*out, payload, 1, sizeof(payload))) << "write payload";
    ASSERT_EQ(5, static_cast<int>(out->tell())) << "tell after write";
    ASSERT_EQ(-1, static_cast<int>(out->seek(0, 99))) << "invalid whence should fail";
    ASSERT_EQ(0, static_cast<int>(out->seek(0, 0))) << "seek set should work";
    ASSERT_EQ(0, static_cast<int>(out->write(payload, 0, 1))) << "size=0 write should return 0";
    out.reset();

    auto in = og::io::og_open_read(bin_path.string().c_str(), true);
    ASSERT_TRUE(in != nullptr) << "og_open_read should open existing file";
    if (!in)
        return;

    unsigned char got[5] = {};
    ASSERT_EQ(0, static_cast<int>(in->read(got, 0, 1))) << "size=0 read should return 0";
    ASSERT_TRUE(og::io::og_read_exact(*in, got, 1, sizeof(got))) << "read payload";
    ASSERT_TRUE(std::memcmp(payload, got, sizeof(payload)) == 0) << "roundtrip payload";
    ASSERT_TRUE(og::io::og_open_read("temp/io_platform_cov/does_not_exist.bin") == nullptr) << "missing file should return null";

    {
        PixieData test_data(1, 2, 1, new unsigned char[2]{7, 8});
        ASSERT_TRUE(write_pixie_png(pix_ok.string().c_str(), test_data)) << "write valid PNG fixture";
    }

    PixieData ok = read_pixie_file(pix_ok.string().c_str());
    ASSERT_EQ(1, static_cast<int>(ok.frames)) << "png frames parsed";
    ASSERT_EQ(2, static_cast<int>(ok.w)) << "png width parsed";
    ASSERT_EQ(1, static_cast<int>(ok.h)) << "png height parsed";
    ASSERT_TRUE(ok.data != nullptr) << "png payload present";

    {
        std::FILE* f = std::fopen(pix_bad.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create truncated file";
        if (!f)
            return;
        const unsigned char junk[] = {1, 3, 1, 42};
        std::fwrite(junk, 1, sizeof(junk), f);
        std::fclose(f);
    }

    PixieData bad = read_pixie_file(pix_bad.string().c_str());
    ASSERT_TRUE(bad.data == nullptr) << "invalid PNG should fail decode";
}


TEST(IoPlatformCoverage, resources_filesystem_api_mount_read_write_exists)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::path("temp") / "resources_fs_api";
    std::error_code ec;
    fs::create_directories(base, ec);

    ASSERT_FALSE(og::resources::mount(nullptr, nullptr, 1)) << "null mount path should fail";
    ASSERT_FALSE(og::resources::mount("", nullptr, 1)) << "empty mount path should fail";
    ASSERT_FALSE(og::resources::unmount(nullptr)) << "null unmount path should fail";
    ASSERT_FALSE(og::resources::unmount("")) << "empty unmount path should fail";
    ASSERT_TRUE(og::resources::list_files(nullptr).empty()) << "null list path should return no files";
    ASSERT_TRUE(og::resources::read_file(nullptr).empty()) << "null read path should return no bytes";
    ASSERT_TRUE(og::resources::read_file("").empty()) << "empty read path should return no bytes";
    ASSERT_TRUE(og::resources::read_file("definitely_missing_resources_fs_api.bin").empty())
        << "missing read path should return no bytes";
    ASSERT_FALSE(og::resources::exists(nullptr)) << "null exists path should be false";
    ASSERT_FALSE(og::resources::exists("")) << "empty exists path should be false";

    const fs::path mounted_input = base / "mounted_input.bin";
    {
        std::FILE* f = std::fopen(mounted_input.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create mounted_input.bin";
        if (!f)
            return;
        const unsigned char payload[] = {1, 3, 5, 7};
        std::fwrite(payload, 1, sizeof(payload), f);
        std::fclose(f);
    }

    ASSERT_TRUE(og::resources::mount(base.string().c_str(), nullptr, 1)) << "filesystem mount should succeed";
    ASSERT_TRUE(og::resources::exists("mounted_input.bin")) << "filesystem exists should see mounted file";

    ASSERT_TRUE(og::resources::set_write_dir(base.string())) << "filesystem set_write_dir should allow temp writes";
    const unsigned char write_payload[] = {9, 8, 7, 6};
    ASSERT_FALSE(og::resources::write_file(nullptr, write_payload, 0)) << "null write path should fail";
    ASSERT_FALSE(og::resources::write_file("", write_payload, 0)) << "empty write path should fail";
    ASSERT_FALSE(og::resources::write_file("null_payload.bin", nullptr, 1))
        << "non-empty write with null data should fail";
    ASSERT_FALSE(og::resources::write_file("missing_parent/write_out.bin", write_payload,
                                           sizeof(write_payload))) << "missing parent write should fail";
    ASSERT_TRUE(og::resources::write_file("empty.bin", write_payload, 0)) << "zero-length write should succeed";
    ASSERT_TRUE(og::resources::read_file("empty.bin").empty()) << "empty file should read as no bytes";
    ASSERT_TRUE(og::resources::write_file("write_out.bin", write_payload,
                                          sizeof(write_payload))) << "filesystem write_file should succeed";
    ASSERT_TRUE(og::resources::exists("write_out.bin")) << "filesystem exists should see written file";

    const auto written_bytes = og::resources::read_file("write_out.bin");
    ASSERT_TRUE(written_bytes.size() == sizeof(write_payload)) << "filesystem read_file should read written bytes";
    if (written_bytes.size() == sizeof(write_payload))
    {
        ASSERT_TRUE(written_bytes[0] == 9 && written_bytes[1] == 8 &&
                    written_bytes[2] == 7 && written_bytes[3] == 6) << "written payload should round-trip";
    }

    ASSERT_TRUE(og::resources::unmount(base.string().c_str())) << "filesystem unmount should succeed";
    ASSERT_TRUE(og::resources::set_write_dir(get_user_path())) << "filesystem write dir should restore to user path";
}


TEST(IoPlatformCoverage, campaign_yaml_roundtrip_and_parse_error_paths)
{
    namespace fs = std::filesystem;
    fs::create_directories("temp");

    og::data::CampaignYaml original;
    original.title = "Round Trip";
    original.version = "2";
    original.first_level = 3;
    original.suggested_power = 7;
    original.authors = "Author";
    original.contributors = "Contributor";
    original.description = "Line one\nLine two";

    const fs::path path = fs::path("temp") / "io_platform_campaign.yaml";
    ASSERT_EQ(og::data::CampaignYamlWriteResult::Ok,
              og::data::write_campaign_yaml_with_result(path.string().c_str(), original));

    og::data::CampaignYaml parsed;
    ASSERT_EQ(og::data::CampaignYamlReadResult::Ok,
              og::data::read_campaign_yaml(path.string().c_str(), parsed));
    ASSERT_TRUE(parsed.saw_title && parsed.saw_version && parsed.saw_first_level);
    ASSERT_EQ("Round Trip", parsed.title);
    ASSERT_EQ("2", parsed.version);
    ASSERT_EQ(3, parsed.first_level);
    ASSERT_EQ(7, parsed.suggested_power);
    ASSERT_EQ("Author", parsed.authors);
    ASSERT_EQ("Contributor", parsed.contributors);
    ASSERT_EQ("Line one\nLine two", parsed.description);

    const fs::path bad_path = fs::path("temp") / "io_platform_campaign_bad.yaml";
    std::ofstream bad(bad_path);
    bad << "title: Partial\nbad: [unclosed\n";
    bad.close();

    og::data::CampaignYaml partial;
    ASSERT_EQ(og::data::CampaignYamlReadResult::ParseFailed,
              og::data::read_campaign_yaml(bad_path.string().c_str(), partial));
    ASSERT_TRUE(partial.saw_title);
    ASSERT_EQ("Partial", partial.title);

    const fs::path default_path =
        fs::path("temp") / "io_platform_default_campaign.yaml";
    ASSERT_TRUE(og::data::write_default_campaign_yaml(
        default_path.string().c_str()));
    og::data::CampaignYaml defaults;
    ASSERT_EQ(og::data::CampaignYamlReadResult::Ok,
              og::data::read_campaign_yaml(
                  default_path.string().c_str(), defaults));
    EXPECT_EQ("New Campaign", defaults.title);
    EXPECT_EQ(1, defaults.first_level);
}


TEST(IoPlatformCoverage, zip_api_roundtrip_and_error_paths)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::path("temp") / "io_platform_cov_zip_in";
    const fs::path archive = fs::path("temp") / "io_platform_cov_archive.zip";
    const fs::path out = fs::path("temp") / "io_platform_cov_zip_out";
    const fs::path missing_parent_zip = fs::path("temp") / "io_platform_cov_missing_parent" / "x.zip";

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::remove_all(out, ec);
    fs::remove(archive, ec);
    fs::remove_all(missing_parent_zip.parent_path(), ec);
    fs::create_directories(base / "sub", ec);
    fs::create_directories(base / "emptydir", ec);

    {
        std::FILE* f = std::fopen((base / "sub" / "payload.txt").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create zip input payload";
        if (!f)
            return;
        const char* text = "zip payload";
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
    }

    const ArchiveIoError zip_missing_parent_err =
        og::io::zip_contents_with_error(base.string(), missing_parent_zip.string());
    ASSERT_EQ(ArchiveIoError::CloseArchiveFailed, zip_missing_parent_err)
        << "zip with missing parent should report close failure after archive finalization";

    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(og::io::zip_contents_with_error(base.string(), archive.string()))) << "zip should succeed";
    ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenArchiveFailed), static_cast<int>(og::io::unzip_into_with_error("temp/io_platform_cov_missing.zip", out.string()))) << "unzip missing archive should fail";
    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(og::io::unzip_into_with_error(archive.string(), out.string()))) << "unzip should succeed";

    const fs::path extracted = out / "sub" / "payload.txt";
    ASSERT_TRUE(fs::exists(extracted, ec)) << "extracted payload should exist";
    ASSERT_TRUE(fs::exists(out / "emptydir", ec)) << "empty directory should be preserved";
}


TEST(IoPlatformCoverage, platform_io_campaign_error_codes)
{
    const std::string prev = get_mounted_campaign();
    set_mounted_campaign_for_testing("");

    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::EmptyId), static_cast<int>(mount_campaign_package_with_error(""))) << "empty campaign id should return EmptyId";
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::EmptyId), static_cast<int>(remount_campaign_package_with_error())) << "remount with no mounted campaign should return EmptyId";
    ASSERT_TRUE(!is_safe_campaign_id("../outside")) << "campaign ids must not contain path traversal";
    ASSERT_TRUE(!is_safe_campaign_id("bad..id")) << "campaign ids must not contain traversal-like dot segments";
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::MountFailed), static_cast<int>(mount_campaign_package_with_error("../outside"))) << "unsafe campaign id should be rejected before path construction";
    ASSERT_TRUE(!unpack_campaign("../outside")) << "unsafe campaign id should not be unpacked";
    ASSERT_TRUE(!repack_campaign("../outside")) << "unsafe campaign id should not be repacked";

    std::map<std::string, int> current_levels;
    const CampaignLoadResult r = load_campaign_with_error("definitely_missing_campaign", current_levels, 7);
    ASSERT_EQ(static_cast<int>(CampaignLoadError::MountFailed), static_cast<int>(r.error)) << "load_campaign_with_error should report mount failure";
    ASSERT_EQ(7, r.current_level) << "current level should preserve input first_level";
    ASSERT_EQ(-2, load_campaign("definitely_missing_campaign", current_levels, 7)) << "load_campaign should map mount failure to -2";

    set_mounted_campaign_for_testing(prev);
}


TEST(IoPlatformCoverage, platform_io_batch3_mount_switch_and_listing_filters)
{
    namespace fs = std::filesystem;
    const std::string prev = get_mounted_campaign();
    const std::string user = get_user_path();
    const fs::path campaigns_dir = fs::path(user) / "campaigns";
    const fs::path scen_dir = fs::path(user) / "scen";
    std::error_code ec;
    fs::create_directories(campaigns_dir, ec);
    fs::create_directories(scen_dir, ec);

    // Hit prev==id short-circuit and prev!=id unmount-failure branch.
    set_mounted_campaign_for_testing("same.id");
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None), static_cast<int>(mount_campaign_package_with_error("same.id"))) << "mount should short-circuit when requested id is already mounted";
    set_mounted_campaign_for_testing("definitely.not.a.campaign");
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::UnmountFailed), static_cast<int>(mount_campaign_package_with_error("gladiator"))) << "mount should report unmount failure when previous mounted id is invalid";

    // list_campaigns should filter non-.glad files.
    {
        std::FILE* f = std::fopen((campaigns_dir / "batch3_filter_marker.glad").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create .glad marker";
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((campaigns_dir / "batch3_filter_marker.txt").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create non-glad marker";
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((campaigns_dir / "batch3..unsafe.glad").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create unsafe .glad marker";
        if (f) std::fclose(f);
    }
    const std::list<std::string> campaigns = list_campaigns();
    ASSERT_TRUE(std::find(campaigns.begin(), campaigns.end(), "batch3_filter_marker") != campaigns.end()) << "list_campaigns should keep .glad ids";
    ASSERT_TRUE(std::find(campaigns.begin(), campaigns.end(), "batch3_filter_marker.txt") == campaigns.end()) << "list_campaigns should filter non-.glad names";
    ASSERT_TRUE(std::find(campaigns.begin(), campaigns.end(), "batch3..unsafe") == campaigns.end()) << "list_campaigns should filter unsafe .glad ids";

    // list_levels/list_levels_v should filter malformed entries and keep strict positive scen ids.
    {
        std::FILE* f = std::fopen((scen_dir / "batch3_note.txt").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create non-fss level marker";
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((scen_dir / "foo.fss").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create non-scen fss marker";
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((scen_dir / "scen0.fss").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create zero-id scen marker";
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((scen_dir / "scen987.fss").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create valid scen marker";
        if (f) std::fclose(f);
    }
    const std::list<int> levels = list_levels();
    const std::vector<int> levels_v = list_levels_v();
    ASSERT_TRUE(std::find(levels.begin(), levels.end(), 987) != levels.end()) << "list_levels should include strict positive scen id";
    ASSERT_TRUE(std::find(levels.begin(), levels.end(), 0) == levels.end()) << "list_levels should reject scen0";
    ASSERT_TRUE(std::find(levels_v.begin(), levels_v.end(), 987) != levels_v.end()) << "list_levels_v should include strict positive scen id";

    // delete_level early return path when no mounted campaign.
    set_mounted_campaign_for_testing("");
    delete_level(987);

    std::remove((campaigns_dir / "batch3_filter_marker.glad").string().c_str());
    std::remove((campaigns_dir / "batch3_filter_marker.txt").string().c_str());
    std::remove((scen_dir / "batch3_note.txt").string().c_str());
    std::remove((scen_dir / "foo.fss").string().c_str());
    std::remove((scen_dir / "scen0.fss").string().c_str());
    std::remove((scen_dir / "scen987.fss").string().c_str());
    set_mounted_campaign_for_testing(prev);
}


TEST(IoPlatformCoverage, platform_io_remount_allows_files_still_open_path)
{
    const std::string prev = get_mounted_campaign();

    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None), static_cast<int>(mount_campaign_package_with_error("gladiator"))) << "mount default campaign should succeed";

    PHYSFS_File* held = PHYSFS_openRead("campaign.yaml");
    ASSERT_TRUE(held != nullptr) << "campaign.yaml should open to hold a live PhysFS handle";
    if (held)
    {
        ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None), static_cast<int>(remount_campaign_package_with_error())) << "remount should gracefully no-op when files are still open";
        PHYSFS_close(held);
    }

    set_mounted_campaign_for_testing(prev);
}


TEST(IoPlatformCoverage, og_file_batch3_physfs_seek_and_path_overloads)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov";
    const fs::path overload_file = tmp_dir / "overload.bin";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    // Exercise path+file open-write overload and stdio-backed seek/tell path.
    auto out = og::io::og_open_write((tmp_dir.string() + "/").c_str(), "overload.bin");
    ASSERT_TRUE(out != nullptr) << "og_open_write(path,file) should create file";
    if (out)
    {
        const unsigned char bytes[] = {9, 8, 7};
        ASSERT_TRUE(og::io::og_write_exact(*out, bytes, 1, sizeof(bytes))) << "overload write payload";
        ASSERT_EQ(3, static_cast<int>(out->tell())) << "stdio tell should advance after write";
        ASSERT_EQ(3, static_cast<int>(out->seek(0, 2))) << "stdio seek end should return file size";
    }
    out.reset();

    // Exercise path+file read overload.
    auto in_overload = og::io::og_open_read((tmp_dir.string() + "/").c_str(), "overload.bin");
    ASSERT_TRUE(in_overload != nullptr) << "og_open_read(path,file) should open written file";
    if (in_overload)
    {
        ASSERT_EQ(1, static_cast<int>(in_overload->seek(1, 0))) << "stdio seek set should work";
        ASSERT_EQ(1, static_cast<int>(in_overload->tell())) << "stdio tell should track seek";
    }

    // PhysFS-backed read path and seek whence variants.
    auto phys = og::io::og_open_read("cfg/openglad.yaml", true);
    ASSERT_TRUE(phys != nullptr) << "PhysFS read for cfg/openglad.yaml should succeed";
    if (phys)
    {
        unsigned char ch = 0;
        ASSERT_EQ(1, (int)phys->read(&ch, 1, 1)) << "physfs read should return one byte";
        const std::int64_t start = phys->seek(0, 0);
        ASSERT_TRUE(start >= 0) << "physfs seek set should succeed";
        const std::int64_t cur = phys->seek(1, 1);
        ASSERT_TRUE(cur >= 1) << "physfs seek cur should advance";
        const std::int64_t end = phys->seek(0, 2);
        ASSERT_TRUE(end >= cur) << "physfs seek end should be >= current position";
        ASSERT_EQ(-1, static_cast<int>(phys->seek(-1, 0)))
            << "PhysFS must reject a negative absolute offset";
        ASSERT_EQ(-1, static_cast<int>(phys->seek(0, 99))) << "physfs invalid whence should fail";
    }

    PixieData missing = read_pixie_file("does_not_exist_batch3.png");
    ASSERT_TRUE(missing.data == nullptr) << "missing pix file should return empty PixieData";

    std::remove(overload_file.string().c_str());
}


TEST(IoPlatformCoverage, zip_api_batch3_output_open_failure_and_empty_input_dir)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::path("temp") / "io_platform_cov_zip_batch3";
    const fs::path in_empty = base / "empty_input";
    const fs::path in_one = base / "one_input";
    const fs::path archive_empty = base / "empty.zip";
    const fs::path archive_one = base / "one.zip";
    const fs::path out_blocker = base / "out_blocker";
    std::error_code ec;

    fs::remove_all(base, ec);
    fs::create_directories(in_empty, ec);
    fs::create_directories(in_one, ec);
    {
        std::FILE* f = std::fopen((in_one / "p.txt").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create zip payload";
        if (f)
        {
            const char* txt = "payload";
            std::fwrite(txt, 1, std::strlen(txt), f);
            std::fclose(f);
        }
    }

    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(og::io::zip_contents_with_error(in_empty.string(), archive_empty.string()))) << "zipping an empty existing directory should still succeed";
    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(og::io::zip_contents_with_error(in_one.string(), archive_one.string()))) << "zipping a normal directory should succeed";

    {
        std::FILE* f = std::fopen(out_blocker.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create out blocker file";
        if (f) std::fclose(f);
    }
    ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenOutputFailed), static_cast<int>(og::io::unzip_into_with_error(archive_one.string(), out_blocker.string()))) << "unzip should fail with OpenOutputFailed when outdirectory is a file";

    std::remove(out_blocker.string().c_str());
    fs::remove_all(base, ec);
}


TEST(IoPlatformCoverage, platform_io_delete_level_nonempty_campaign_path)
{
    namespace fs = std::filesystem;
    const std::string user = get_user_path();
    const std::string id = "batch5_delete_level";
    const fs::path temp_root = fs::path(user) / "temp";
    const fs::path temp_scen = temp_root / "scen";
    const fs::path temp_pix = temp_root / "pix";
    const fs::path campaigns_dir = fs::path(user) / "campaigns";
    const fs::path archive = campaigns_dir / (id + ".glad");

    std::error_code ec;
    fs::create_directories(temp_scen, ec);
    fs::create_directories(temp_pix, ec);
    fs::create_directories(campaigns_dir, ec);

    {
        std::FILE* f = std::fopen((temp_scen / "scen321.fss").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create scen file";
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((temp_pix / "scen0321.png").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create pix file";
        if (f) std::fclose(f);
    }

    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(og::io::zip_contents_with_error(temp_root.string(), archive.string()))) << "seed campaign archive should be created";

    const std::string prev = get_mounted_campaign();
    set_mounted_campaign_for_testing(id);
    delete_level(321);
    set_mounted_campaign_for_testing(prev);

    cleanup_unpacked_campaign();
    fs::remove(archive, ec);
}


TEST(IoPlatformCoverage, og_file_open_read_user_and_asset_fallbacks)
{
    namespace fs = std::filesystem;
    const fs::path user_rel = fs::path("batch5_user_read.bin");
    const fs::path user_abs = fs::path(get_user_path()) / user_rel;
    std::error_code ec;

    fs::create_directories(user_abs.parent_path(), ec);

    {
        std::FILE* f = std::fopen(user_abs.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create user fallback file";
        if (f) {
            const unsigned char b = 17;
            std::fwrite(&b, 1, 1, f);
            std::fclose(f);
        }
    }
    auto user_in = og::io::og_open_read(user_rel.string().c_str(), true);
    ASSERT_TRUE(user_in != nullptr) << "og_open_read should find file in user path fallback";
    fs::remove(user_abs, ec);
}


TEST(IoPlatformCoverage, og_file_physfs_backed_write_and_read_roundtrip)
{
    const char* write_dir = PHYSFS_getWriteDir();
    ASSERT_TRUE(write_dir != nullptr) << "PHYSFS write dir should be initialized for runtime tests";
    if (!write_dir)
        return;

    const std::string vdir = "batch7_physfs_only_dir";
    const std::string vpath = vdir + "/rw.bin";
    ASSERT_TRUE(PHYSFS_mkdir(vdir.c_str()) != 0) << "PHYSFS_mkdir should create virtual directory";

    auto out = og::io::og_open_write(vpath.c_str());
    ASSERT_TRUE(out != nullptr) << "og_open_write should use PhysFS-backed file for virtual path";
    if (!out)
        return;

    const unsigned char payload[] = {10, 20, 30, 40};
    ASSERT_TRUE(og::io::og_write_exact(*out, payload, 1, sizeof(payload))) << "physfs-backed write should succeed";
    ASSERT_TRUE(out->tell() >= 4) << "physfs-backed tell should report advanced position";
    ASSERT_TRUE(out->seek(0, 0) >= 0) << "physfs-backed seek set should succeed";
    out.reset();

    auto in = og::io::og_open_read(vpath.c_str(), true);
    ASSERT_TRUE(in != nullptr) << "og_open_read should find newly written PhysFS file";
    if (in)
    {
        unsigned char got[4] = {};
        ASSERT_TRUE(og::io::og_read_exact(*in, got, 1, sizeof(got))) << "physfs-backed read should succeed";
        ASSERT_TRUE(std::memcmp(got, payload, sizeof(payload)) == 0) << "physfs-backed roundtrip should match";
        ASSERT_TRUE(in->tell() >= 4) << "physfs-backed read tell should advance";
        ASSERT_TRUE(in->seek(0, 2) >= 0) << "physfs-backed seek end should succeed";
    }

    (void)PHYSFS_delete(vpath.c_str());
    (void)std::remove((std::string(write_dir) + "/" + vpath).c_str());
}


TEST(IoPlatformCoverage, pixie_data_move_constructor_resets_source)
{
    PixieData src(2, 3, 1, new unsigned char[6]{1, 2, 3, 4, 5, 6});
    ASSERT_TRUE(src.valid()) << "source pixie should start valid";

    PixieData moved(std::move(src));
    ASSERT_TRUE(moved.valid()) << "moved pixie should retain data";
    ASSERT_EQ(0, (int)src.frames) << "move ctor should clear source frames";
    ASSERT_EQ(0, (int)src.w) << "move ctor should clear source width";
    ASSERT_EQ(0, (int)src.h) << "move ctor should clear source height";
}


TEST(IoPlatformCoverage, physfs_api_wrapper_error_and_list_paths)
{
    // Hit wrapper calls without mutating shared runtime mount/write state.
    const std::list<std::string> root_files = og::io::physfs_enumerate_files_sorted("");
    ASSERT_TRUE(!root_files.empty()) << "physfs_enumerate_files_sorted should list mounted root entries";

    ASSERT_TRUE(!og::io::physfs_mount("definitely_missing_physfs_path", nullptr, 1)) << "physfs_mount should fail for a missing path";
    ASSERT_TRUE(!og::io::physfs_unmount("definitely_missing_physfs_path")) << "physfs_unmount should fail for an unmounted path";
    ASSERT_TRUE(!og::io::physfs_last_error().empty()) << "physfs_last_error wrapper should return error text";
}


TEST(IoPlatformCoverage, zip_api_missing_input_dir_exists_guard_path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path missing = fs::path("temp") / "zip_missing_input_batch8";
    const fs::path archive = fs::path("temp") / "zip_missing_input_batch8.zip";
    fs::remove_all(missing, ec);
    fs::remove(archive, ec);

    const ArchiveIoError r = og::io::zip_contents_with_error(missing.string(), archive.string());
    ASSERT_EQ(ArchiveIoError::None, r)
        << "zipping a missing input dir should be treated as an empty input set";
    ASSERT_FALSE(fs::exists(archive));
    fs::remove(archive, ec);
}


TEST(IoPlatformCoverage, zip_api_unreadable_and_non_regular_entries_report_add_failure)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::path("temp") / "io_platform_cov_zip_batch8";
    const fs::path archive = base / "edge_cases.zip";
    const fs::path unreadable = base / "no_read.txt";
    const fs::path normal = base / "ok.txt";
    const fs::path fifo_path = base / "ignored_fifo";

    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    fs::remove(archive, ec);

    {
        std::FILE* f = std::fopen(normal.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create normal file";
        if (f)
        {
            const char* txt = "ok";
            std::fwrite(txt, 1, std::strlen(txt), f);
            std::fclose(f);
        }
    }
    {
        std::FILE* f = std::fopen(unreadable.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create unreadable file";
        if (f)
        {
            const char* txt = "nope";
            std::fwrite(txt, 1, std::strlen(txt), f);
            std::fclose(f);
        }
    }
    (void)::chmod(unreadable.string().c_str(), 0);

    // Create a non-regular entry that should be skipped.
    (void)::mkfifo(fifo_path.string().c_str(), 0600);

    const ArchiveIoError r = og::io::zip_contents_with_error(base.string(), archive.string());
    if (::geteuid() == 0) {
        ASSERT_EQ(ArchiveIoError::None, r)
            << "root test runner can reopen chmod(0) files, and non-regular entries should be skipped";
        ASSERT_TRUE(fs::exists(archive));
    } else {
        ASSERT_EQ(ArchiveIoError::CloseArchiveFailed, r)
            << "non-root runner should fail while finalizing an archive with unreadable input";
    }

    (void)::chmod(unreadable.string().c_str(), 0600);
    fs::remove_all(base, ec);
}


TEST(IoPlatformCoverage, zip_api_batch9_permission_denied_walk_and_corrupt_unzip_paths)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::path("temp") / "io_platform_cov_zip_batch9";
    const fs::path blocked = base / "blocked";
    const fs::path blocked_child = blocked / "child";
    const fs::path archive = base / "blocked_walk.zip";
    const fs::path corrupt = base / "corrupt.zip";
    const fs::path out = base / "out";

    fs::remove_all(base, ec);
    fs::create_directories(blocked_child, ec);
    {
        std::FILE* f = std::fopen((blocked_child / "p.txt").string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create blocked child payload";
        if (f) {
            const char* txt = "payload";
            std::fwrite(txt, 1, std::strlen(txt), f);
            std::fclose(f);
        }
    }

    // Try to trigger recursive iterator increment error path under permission restrictions.
    (void)::chmod(blocked.string().c_str(), 0);
    const ArchiveIoError walk_r = og::io::zip_contents_with_error(base.string(), archive.string());
    ASSERT_EQ(ArchiveIoError::None, walk_r)
        << "permission-denied walk should skip inaccessible entries and still close the archive";
    ASSERT_TRUE(fs::exists(archive));
    (void)::chmod(blocked.string().c_str(), 0700);

    // Corrupt archive bytes should fail open/read in unzip path.
    {
        std::FILE* f = std::fopen(corrupt.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create corrupt zip bytes";
        if (f) {
            const unsigned char junk[] = {0x50, 0x4B, 0x01, 0x02, 0x00, 0x00};
            std::fwrite(junk, 1, sizeof(junk), f);
            std::fclose(f);
        }
    }
    const ArchiveIoError unzip_r = og::io::unzip_into_with_error(corrupt.string(), out.string());
    ASSERT_EQ(ArchiveIoError::OpenArchiveFailed, unzip_r)
        << "corrupt archive header should fail while opening the archive";

    fs::remove_all(base, ec);
}


TEST(IoPlatformCoverage, platform_io_restore_defaults_and_load_campaign_unmount_error_path)
{
    namespace fs = std::filesystem;
    const std::string user = get_user_path();
    const fs::path user_campaigns = fs::path(user) / "campaigns";
    const fs::path user_cfg = fs::path(user) / "cfg";
    std::error_code ec;

    // Force copy_file error branches by removing destination parent directories.
    fs::remove_all(user_campaigns, ec);
    fs::remove_all(user_cfg, ec);
    restore_default_campaigns();
    restore_default_settings();

    // Then exercise success branches by recreating destination parents.
    fs::create_directories(user_campaigns, ec);
    fs::create_directories(user_cfg, ec);
    restore_default_campaigns();
    restore_default_settings();

    const std::string prev = get_mounted_campaign();
    set_mounted_campaign_for_testing("definitely.not.a.campaign");
    std::map<std::string, int> current_levels;
    ASSERT_EQ(-3, load_campaign("gladiator", current_levels, 9)) << "load_campaign should map unmount failure to -3";
    set_mounted_campaign_for_testing(prev);
}

TEST(IoPlatformCoverage, restore_default_campaigns_ignores_nonpackages)
{
    namespace fs = std::filesystem;
    const fs::path source =
        fs::path(get_asset_path()) / "builtin" /
        "coverage_nonpackage_marker.txt";
    const fs::path destination =
        fs::path(get_user_path()) / "campaigns" /
        "coverage_nonpackage_marker.txt";
    og::test::ScopedPhysicalFileState source_state(source);
    og::test::ScopedPhysicalFileState destination_state(destination);
    ASSERT_TRUE(source_state.ready()) << source_state.error().message();
    ASSERT_TRUE(destination_state.ready())
        << destination_state.error().message();

    std::error_code ec;
    fs::create_directories(source.parent_path(), ec);
    ASSERT_FALSE(ec);
    {
        std::ofstream marker(source, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(marker.good());
        marker << "not a campaign package";
    }
    fs::remove(destination, ec);
    ASSERT_FALSE(ec);

    restore_default_campaigns();

    EXPECT_TRUE(fs::is_regular_file(source));
    EXPECT_FALSE(fs::exists(destination))
        << "only .glad packages may be restored into the user campaign dir";
}

// Installs that predate the reverse-DNS purge left stock campaigns as
// "org.openglad.<id>.glad" in the user dir. Restoring the bare-named
// builtin drops that stale twin (stock copies held no user edits — they
// were overwritten from builtin/ on every restore), while third-party
// archives that merely use the prefix are left alone.
TEST(IoPlatformCoverage, restore_default_campaigns_drops_the_legacy_twin)
{
    namespace fs = std::filesystem;
    const fs::path campaigns_dir = fs::path(get_user_path()) / "campaigns";
    const fs::path legacy_twin = campaigns_dir / "org.openglad.gladiator.glad";
    const fs::path foreign = campaigns_dir / "org.openglad.custom.glad";
    og::test::ScopedPhysicalFileState twin_state(legacy_twin);
    og::test::ScopedPhysicalFileState foreign_state(foreign);
    ASSERT_TRUE(twin_state.ready()) << twin_state.error().message();
    ASSERT_TRUE(foreign_state.ready()) << foreign_state.error().message();

    std::error_code ec;
    fs::create_directories(campaigns_dir, ec);
    ASSERT_FALSE(ec);
    {
        std::ofstream stale(legacy_twin, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stale.good());
        stale << "pre-rename stock copy";
    }
    {
        std::ofstream keeper(foreign, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(keeper.good());
        keeper << "third-party archive";
    }

    restore_default_campaigns();

    EXPECT_TRUE(fs::is_regular_file(campaigns_dir / "gladiator.glad"));
    EXPECT_FALSE(fs::exists(legacy_twin))
        << "restoring builtin <id>.glad removes the stale "
           "org.openglad.<id>.glad stock copy";
    EXPECT_TRUE(fs::is_regular_file(foreign))
        << "only twins of restored builtins are cleaned up";
}


TEST(IoPlatformCoverage, read_pixie_file_truncated_header_path)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov";
    const fs::path pix_header_bad = tmp_dir / "bad_header.png";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    {
        std::FILE* f = std::fopen(pix_header_bad.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create truncated-header pix";
        if (!f)
            return;
        const unsigned char partial_header[] = {1, 2}; // missing h byte
        std::fwrite(partial_header, 1, sizeof(partial_header), f);
        std::fclose(f);
    }

    PixieData bad_header = read_pixie_file(pix_header_bad.string().c_str());
    ASSERT_TRUE(bad_header.data == nullptr) << "truncated pix header should fail";
    fs::remove(pix_header_bad, ec);
}

TEST(IoPlatformCoverage,
     aseprite_sidecar_delimiters_and_size_limit_preserve_exact_fallback)
{
    ScopedTraceBuffer trace_guard;
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("temp") / "io_platform_cov";
    const fs::path png = dir / "resource_followup.png";
    const fs::path json = dir / "resource_followup.json";
    og::test::ScopedPhysicalFileState png_state(png);
    og::test::ScopedPhysicalFileState json_state(json);
    ASSERT_TRUE(png_state.ready()) << png_state.error().message();
    ASSERT_TRUE(json_state.ready()) << json_state.error().message();
    std::error_code ec;
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();

    constexpr int width = 4;
    constexpr int height = 3;
    const std::vector<unsigned char> expected = {
        0, 1, 2, 3,
        4, 5, 6, 7,
        8, 9, 10, 11,
    };
    auto* source = new unsigned char[expected.size()];
    std::copy(expected.begin(), expected.end(), source);
    ASSERT_TRUE(write_pixie_png(
        png.string().c_str(), PixieData(1, width, height, source)));

    struct MalformedSidecar
    {
        const char* label;
        const char* text;
        const char* reason;
        bool fatal;
    };
    const MalformedSidecar cases[] = {
        {"top-level missing delimiter",
         R"({"unknown":1 "next":2})",
         "expected ',' or '}' at top level", false},
        {"frames missing delimiter",
         R"({"frames":{"a":{"frame":{"w":4,"h":3}} "b":{"frame":{"w":4,"h":3}}}})",
         "expected ',' or '}' in frames", false},
        {"frame entry missing delimiter",
         R"({"frames":{"a":{"frame":{"w":4,"h":3} "duration":1}}})",
         "expected ',' or '}' in frame entry", false},
        {"frame rectangle missing delimiter",
         R"({"frames":{"a":{"frame":{"w":4 "h":3}}}})",
         "expected ',' or '}' in frame rect", false},
        {"meta missing delimiter",
         R"({"frames":{"a":{"frame":{"w":4,"h":3}}},"meta":{"size":{"w":4,"h":3} "app":"test"}})",
         "expected ',' or '}' in meta", false},
        {"meta size missing delimiter",
         R"({"frames":{"a":{"frame":{"w":4,"h":3}}},"meta":{"size":{"w":4 "h":3}}})",
         "expected ',' or '}' in meta.size", false},
        {"nested unknown object missing delimiter",
         R"({"unknown":{"x":1 "y":2}})",
         "expected ',' or '}'", false},
        {"footprint key is not a string",
         R"({"footprint":{1:2}})",
         "expected '\"'", true},
        {"footprint height is not an integer",
         R"({"footprint":{"w":4,"h":"3"}})",
         "expected non-negative integer", true},
        {"footprint missing delimiter",
         R"({"footprint":{"w":4 "h":3}})",
         "expected ',' or '}' in footprint", true},
    };

    for (const MalformedSidecar& row : cases)
    {
        SCOPED_TRACE(row.label);
        write_text_file(json, row.text);
        trace_clear();

        PixieData loaded = read_pixie_file(png.string().c_str());
        if (row.fatal)
        {
            EXPECT_FALSE(loaded.valid())
                << "a malformed footprint is a fatal sprite contract error";
            EXPECT_TRUE(trace_contains("io", "invalid footprint in sidecar"));
        }
        else
        {
            ASSERT_TRUE(loaded.valid())
                << "ordinary metadata errors use the documented one-frame fallback";
            ASSERT_EQ(width, static_cast<int>(loaded.w));
            ASSERT_EQ(height, static_cast<int>(loaded.h));
            ASSERT_EQ(1, static_cast<int>(loaded.frames));
            ASSERT_NE(nullptr, loaded.data);
            EXPECT_TRUE(std::equal(expected.begin(), expected.end(),
                                   loaded.data.get()))
                << "fallback must preserve every decoded palette index";
            EXPECT_TRUE(trace_contains("io", "malformed Aseprite sidecar"));
        }
        EXPECT_TRUE(trace_contains("io", row.reason))
            << "the diagnostic must identify the exact malformed construct";
    }

    write_text_file(
        json,
        R"({"frames":{"a":{"frame":{"w":4,"h":3}}},"meta":{"size":{"w":4,"h":3}},"footprint":{"w":256,"h":3}})");
    trace_clear();
    EXPECT_FALSE(read_pixie_file(png.string().c_str()).valid());
    EXPECT_TRUE(trace_contains("io", "footprint out of range in sidecar"));

    // Unknown metadata fields remain forward-compatible, including nested
    // objects. This reaches the positive counterpart of the malformed-object
    // cases above and pins exact pixels rather than only accepting the load.
    write_text_file(
        json,
        R"({"frames":{"a":{"frame":{"w":4,"h":3}}},"meta":{"size":{"w":4,"h":3,"future":{"revision":2}}}})");
    trace_clear();
    PixieData forward_compatible = read_pixie_file(png.string().c_str());
    ASSERT_TRUE(forward_compatible.valid());
    ASSERT_EQ(width, static_cast<int>(forward_compatible.w));
    ASSERT_EQ(height, static_cast<int>(forward_compatible.h));
    ASSERT_EQ(1, static_cast<int>(forward_compatible.frames));
    ASSERT_NE(nullptr, forward_compatible.data);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(),
                           forward_compatible.data.get()));
    EXPECT_FALSE(trace_contains("io", "malformed Aseprite sidecar"));

    // A sidecar over the documented 1 MiB resource limit must fall back
    // without reading or allocating from its contents.
    {
        std::ofstream oversized(json, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(oversized.good());
        oversized.seekp(1024 * 1024);
        oversized.put('x');
        ASSERT_TRUE(oversized.good());
    }
    ASSERT_EQ((1024u * 1024u) + 1u, fs::file_size(json));
    trace_clear();
    PixieData oversized_fallback = read_pixie_file(png.string().c_str());
    ASSERT_TRUE(oversized_fallback.valid());
    ASSERT_EQ(width, static_cast<int>(oversized_fallback.w));
    ASSERT_EQ(height, static_cast<int>(oversized_fallback.h));
    ASSERT_EQ(1, static_cast<int>(oversized_fallback.frames));
    ASSERT_NE(nullptr, oversized_fallback.data);
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(),
                           oversized_fallback.data.get()));
    EXPECT_TRUE(trace_contains("io", "file too large"));
}

TEST(IoPlatformCoverage,
     indexed_png_resource_limits_and_decode_failures_are_rejected)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::path("temp") / "io_platform_cov";
    const fs::path path = dir / "resource_rejection.png";
    og::test::ScopedPhysicalFileState path_state(path);
    ASSERT_TRUE(path_state.ready()) << path_state.error().message();
    std::error_code ec;
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << ec.message();

    write_binary_file(path, {});
    ASSERT_EQ(0u, fs::file_size(path));
    EXPECT_FALSE(read_pixie_file(path.string().c_str()).valid());

    {
        std::ofstream oversized(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(oversized.good());
        oversized.seekp(16 * 1024 * 1024);
        oversized.put('x');
        ASSERT_TRUE(oversized.good());
    }
    ASSERT_EQ((16u * 1024u * 1024u) + 1u, fs::file_size(path));
    EXPECT_FALSE(read_pixie_file(path.string().c_str()).valid());

    std::vector<unsigned char> too_wide =
        make_test_indexed_png(256, 1, 256);
    ASSERT_FALSE(too_wide.empty());
    lodepng::State inspected_state;
    inspected_state.decoder.color_convert = 0;
    unsigned inspected_width = 0;
    unsigned inspected_height = 0;
    ASSERT_EQ(0u, lodepng_inspect(
                      &inspected_width, &inspected_height, &inspected_state,
                      too_wide.data(), too_wide.size()));
    ASSERT_EQ(256u, inspected_width);
    ASSERT_EQ(1u, inspected_height);
    write_binary_file(path, too_wide);
    EXPECT_FALSE(read_pixie_file(path.string().c_str()).valid());

    std::vector<unsigned char> short_palette =
        make_test_indexed_png(2, 2, 4);
    ASSERT_FALSE(short_palette.empty());
    lodepng::State decoded_state;
    decoded_state.decoder.color_convert = 0;
    std::vector<unsigned char> decoded;
    unsigned decoded_width = 0;
    unsigned decoded_height = 0;
    ASSERT_EQ(0u, lodepng::decode(
                      decoded, decoded_width, decoded_height, decoded_state,
                      short_palette.data(), short_palette.size()));
    ASSERT_EQ(4u, decoded_state.info_png.color.palettesize);
    ASSERT_EQ(std::vector<unsigned char>({0, 1, 2, 3}), decoded);
    write_binary_file(path, short_palette);
    EXPECT_FALSE(read_pixie_file(path.string().c_str()).valid());

    std::vector<unsigned char> missing_image_data =
        make_test_indexed_png(2, 2, 4);
    ASSERT_GE(missing_image_data.size(), 33u);
    missing_image_data.resize(33); // signature + complete IHDR, no IDAT/IEND
    lodepng::State header_state;
    header_state.decoder.color_convert = 0;
    inspected_width = 0;
    inspected_height = 0;
    ASSERT_EQ(0u, lodepng_inspect(
                      &inspected_width, &inspected_height, &header_state,
                      missing_image_data.data(), missing_image_data.size()))
        << "fixture must pass the production inspection stage";
    decoded.clear();
    ASSERT_NE(0u, lodepng::decode(
                      decoded, decoded_width, decoded_height, header_state,
                      missing_image_data.data(), missing_image_data.size()))
        << "fixture must fail only during the production decode stage";
    write_binary_file(path, missing_image_data);
    EXPECT_FALSE(read_pixie_file(path.string().c_str()).valid());
}


TEST(IoPlatformCoverage, platform_io_bool_wrappers_and_small_helpers)
{
    namespace fs = std::filesystem;
    const std::string prev = get_mounted_campaign();

    ASSERT_TRUE(mount_campaign_package_with_error("") != CampaignPackageIoError::None) << "mount_campaign_package should fail for empty id";
    ASSERT_TRUE(unmount_campaign_package_with_error("") == CampaignPackageIoError::None) << "unmount_campaign_package should succeed for empty id";
    set_mounted_campaign_for_testing("");
    ASSERT_TRUE(remount_campaign_package_with_error() != CampaignPackageIoError::None) << "remount_campaign_package should fail when nothing is mounted";

    const std::list<std::string> exploded = explode("a,b,", ',');
    ASSERT_EQ(3, (int)exploded.size()) << "explode should include trailing empty segment";

    const fs::path nested = fs::path("temp") / "io_platform_cov" / "wrapper_dir" / "a" / "b";
    ASSERT_TRUE(create_dir(nested.string())) << "create_dir should create nested directories";

    const fs::path missing_zip = fs::path("temp") / "io_platform_cov" / "missing_bool_wrapper.zip";
    ASSERT_TRUE(unzip_into_with_error(missing_zip.string(), (nested / "out").string()) != ArchiveIoError::None) << "unzip_into should return error for missing archive";
    (void)zip_contents_with_error((nested / "missing_input").string(), (nested / "out.zip").string());

    set_mounted_campaign_for_testing(prev);
}


TEST(IoPlatformCoverage, og_file_round6_physfs_zero_size_and_stdio_seek_failure_paths)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    // PhysFS-backed zero-size read/write paths.
    const char* write_dir = PHYSFS_getWriteDir();
    ASSERT_TRUE(write_dir != nullptr) << "PHYSFS write dir should exist";
    if (write_dir)
    {
        const std::string vdir = "batch9_physfs_zero";
        const std::string vpath = vdir + "/rw.bin";
        ASSERT_TRUE(PHYSFS_mkdir(vdir.c_str()) != 0) << "create virtual dir for zero-size path";
        auto out = og::io::og_open_write(vpath.c_str());
        ASSERT_TRUE(out != nullptr) << "open physfs output";
        if (out)
        {
            const unsigned char b = 1;
            ASSERT_EQ(0, (int)out->write(&b, 0, 1)) << "physfs write size=0 should return 0";
            out.reset();
        }
        auto in = og::io::og_open_read(vpath.c_str(), true);
        ASSERT_TRUE(in != nullptr) << "open physfs input";
        if (in)
        {
            unsigned char b = 0;
            ASSERT_EQ(0, (int)in->read(&b, 0, 1)) << "physfs read size=0 should return 0";
        }
        (void)PHYSFS_delete(vpath.c_str());
        (void)std::remove((std::string(write_dir) + "/" + vpath).c_str());
    }

    // stdio-backed negative seek failure path.
    const fs::path seek_file = tmp_dir / "seek_fail.bin";
    {
        std::FILE* f = std::fopen(seek_file.string().c_str(), "wb");
        ASSERT_TRUE(f != nullptr) << "create stdio seek-fail file";
        if (f)
        {
            const unsigned char bytes[] = {1, 2, 3};
            std::fwrite(bytes, 1, sizeof(bytes), f);
            std::fclose(f);
        }
    }
    auto in_stdio = og::io::og_open_read((tmp_dir.string() + "/").c_str(), "seek_fail.bin");
    ASSERT_TRUE(in_stdio != nullptr) << "open stdio seek-fail file";
    if (in_stdio)
    {
        ASSERT_EQ(-1, (int)in_stdio->seek(-1, 0)) << "stdio seek should fail for negative SET offset";
    }
}


TEST(IoPlatformCoverage, physfs_rwops_bridge_read_write_seek_paths)
{
    const std::string vdir = "physfs_rwops_bridge";
    const std::string vpath = vdir + "/rwops.bin";
    (void)PHYSFS_delete(vpath.c_str());

    if (!PHYSFS_exists(vdir.c_str()))
    {
        ASSERT_TRUE(PHYSFS_mkdir(vdir.c_str()) != 0) << "create virtual dir for PhysFS RWops bridge";
    }

    const unsigned char payload[] = {9, 8, 7, 6, 5};
    {
        IostreamPtr out{open_write_file(vpath.c_str())};
        ASSERT_TRUE(out != nullptr) << "open PhysFS-backed SDL_IOStream output";
        if (!out)
            return;

        ASSERT_EQ(0, static_cast<int>(SDL_WriteIO(out.get(), payload, 0)))
            << "PhysFS IOStream write size=0 should return 0";
        ASSERT_EQ(static_cast<int>(sizeof(payload)),
                  static_cast<int>(SDL_WriteIO(out.get(), payload, sizeof(payload))))
            << "PhysFS IOStream should write all payload bytes";
        int bad_whence = 99; // non-const: a constant 99 trips GCC -Wconversion (outside enum range)
        ASSERT_EQ(-1, static_cast<int>(SDL_SeekIO(out.get(), 0, static_cast<SDL_IOWhence>(bad_whence))))
            << "PhysFS IOStream should reject invalid seek whence";
        ASSERT_EQ(-1, static_cast<int>(SDL_SeekIO(out.get(), -1, SDL_IO_SEEK_SET)))
            << "PhysFS RWops should reject seeks before the start";
    }

    {
        IostreamPtr in{open_read_file(vpath.c_str(), true)};
        ASSERT_TRUE(in != nullptr) << "open PhysFS-backed SDL_IOStream input";
        if (!in)
            return;

        ASSERT_EQ(static_cast<Sint64>(sizeof(payload)), SDL_GetIOSize(in.get()))
            << "PhysFS IOStream size should report the file length";
        unsigned char got[sizeof(payload)] = {};
        ASSERT_EQ(0, static_cast<int>(SDL_ReadIO(in.get(), got, 0)))
            << "PhysFS IOStream read size=0 should return 0";
        ASSERT_EQ(static_cast<int>(sizeof(got)),
                  static_cast<int>(SDL_ReadIO(in.get(), got, sizeof(got))))
            << "PhysFS IOStream should read all payload bytes";
        ASSERT_TRUE(std::memcmp(got, payload, sizeof(payload)) == 0)
            << "PhysFS RWops should roundtrip payload bytes";
        ASSERT_EQ(3, static_cast<int>(SDL_SeekIO(in.get(), -2, SDL_IO_SEEK_END)))
            << "PhysFS RWops should seek relative to end";
        ASSERT_EQ(4, static_cast<int>(SDL_SeekIO(in.get(), 1, SDL_IO_SEEK_CUR)))
            << "PhysFS RWops should seek relative to current position";
        ASSERT_EQ(0, static_cast<int>(SDL_SeekIO(in.get(), 0, SDL_IO_SEEK_SET)))
            << "PhysFS RWops should seek relative to the start";
        ASSERT_EQ(-1, static_cast<int>(SDL_SeekIO(in.get(), -99, SDL_IO_SEEK_CUR)))
            << "PhysFS RWops should reject negative current-relative seeks";
    }

    IostreamPtr missing{open_read_file((vdir + "/missing.bin").c_str(), true)};
    ASSERT_TRUE(missing == nullptr) << "missing PhysFS IOStream input should return null";
    (void)PHYSFS_delete(vpath.c_str());
}


TEST(IoPlatformCoverage, og_file_round8_seek_cur_end_and_open_write_failure_paths)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov" / "round8_seek";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    const fs::path data_path = tmp_dir / "seek_modes.bin";
    {
        auto out = og::io::og_open_write(data_path.string().c_str());
        ASSERT_TRUE(out != nullptr) << "open write for seek mode file";
        if (!out)
            return;
        const unsigned char bytes[] = {10, 20, 30, 40, 50};
        ASSERT_TRUE(og::io::og_write_exact(*out, bytes, 1, sizeof(bytes))) << "write seek mode payload";
    }

    auto in = og::io::og_open_read((tmp_dir.string() + "/").c_str(), "seek_modes.bin");
    ASSERT_TRUE(in != nullptr) << "open stdio-backed input for seek mode checks";
    if (in)
    {
        ASSERT_EQ(2, (int)in->seek(2, 0)) << "SEEK_SET should position at offset 2";
        ASSERT_EQ(3, (int)in->seek(1, 1)) << "SEEK_CUR should advance by 1";
        ASSERT_EQ(4, (int)in->seek(-1, 2)) << "SEEK_END with -1 should position at last byte";
        ASSERT_EQ(4, (int)in->tell()) << "tell should match final offset";
    }

    auto missing_parent_out = og::io::og_open_write((tmp_dir / "missing" / "nested" / "nope.bin").string().c_str());
    ASSERT_TRUE(missing_parent_out == nullptr) << "open_write should fail for missing parent directory";
}


TEST(IoPlatformCoverage, zip_platform_round8_open_archive_and_mount_error_paths)
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov" / "round8_zip";
    std::error_code ec;
    fs::remove_all(tmp_dir, ec);
    fs::create_directories(tmp_dir, ec);

    // Force open failure deterministically by using a directory path as archive output.
    const ArchiveIoError zip_err = zip_contents_with_error(tmp_dir.string(), tmp_dir.string());
    ASSERT_EQ((int)ArchiveIoError::OpenArchiveFailed, (int)zip_err) << "zip_contents_with_error should report OpenArchiveFailed for directory output path";

    const ArchiveIoError unzip_err = unzip_into_with_error((tmp_dir / "does_not_exist.zip").string(),
                                                           (tmp_dir / "out").string());
    ASSERT_EQ((int)ArchiveIoError::OpenArchiveFailed, (int)unzip_err) << "unzip_into_with_error should report OpenArchiveFailed for missing archive";

    const std::string prev = get_mounted_campaign();
    set_mounted_campaign_for_testing("");
    std::map<std::string, int> current_levels;
    ASSERT_EQ(-2, load_campaign("definitely.not.a.campaign", current_levels, 5)) << "load_campaign should map mount failure to -2";
    set_mounted_campaign_for_testing(prev);
}


TEST(IoPlatformCoverage, rwops_handlers_and_open_read_debug_fallbacks)
{
    unsigned char buffer[8] = {};
    IostreamPtr rwops{SDL_IOFromMem(buffer, sizeof(buffer))};
    ASSERT_TRUE(rwops != nullptr) << "SDL_IOFromMem should create an SDL_IOStream";
    if (!rwops)
        return;

    unsigned char payload[] = {3, 1, 4, 1};
    ASSERT_EQ(1, rwops_write_handler(rwops.get(), payload, sizeof(payload)))
        << "rwops_write_handler should report success";
    ASSERT_EQ(0, (int)SDL_SeekIO(rwops.get(), 0, SDL_IO_SEEK_SET))
        << "rewind memory RWops";

    unsigned char got[4] = {};
    std::size_t size_read = 0;
    ASSERT_EQ(1, rwops_read_handler(rwops.get(), got, sizeof(got), &size_read))
        << "rwops_read_handler should report success";
    ASSERT_EQ(sizeof(got), size_read) << "handler should report bytes read";
    ASSERT_TRUE(std::memcmp(got, payload, sizeof(got)) == 0)
        << "rwops handlers should roundtrip payload through SDL_IOStream";

    IostreamPtr missing{open_read_file("definitely_missing_debug_fallback_110.bin", true)};
    ASSERT_TRUE(missing == nullptr)
        << "debug read of missing file should exhaust cwd/user/asset fallbacks";
}


TEST(IoPlatformCoverage, new_file_error_paths_report_write_failures)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::path("temp") / "io_platform_cov" / "new_file_errors";
    const fs::path pix_blocker = base / "pix_blocker.png";
    const fs::path map_blocker = base / "map_blocker.png";
    const fs::path campaign_blocker = base / "campaign.yaml";
    const fs::path scen_blocker = base / "scen1.fss";
    std::error_code ec;

    fs::remove_all(base, ec);
    fs::create_directories(pix_blocker, ec);
    fs::create_directories(map_blocker, ec);
    fs::create_directories(campaign_blocker, ec);
    fs::create_directories(scen_blocker, ec);

    ASSERT_EQ(NewFileIoError::WriteFailed,
              create_new_pix_with_error(pix_blocker.string(), 2, 2, 7))
        << "writing a pix over a directory should report WriteFailed";
    ASSERT_EQ(NewFileIoError::WriteFailed,
              create_new_map_pix_with_error(map_blocker.string(), 2, 2))
        << "writing a map pix over a directory should report WriteFailed";
    ASSERT_EQ(NewFileIoError::OpenWriteFailed,
              create_new_campaign_descriptor_with_error(campaign_blocker.string()))
        << "campaign descriptor open should fail for a directory target";
    ASSERT_EQ(NewFileIoError::OpenWriteFailed,
              create_new_scen_file_with_error(scen_blocker.string(), "scen0001"))
        << "scenario write should map open-write failure";

    fs::remove_all(base, ec);
}


TEST(IoPlatformCoverage, sprite_sheet_apply_covers_same_pack_and_failed_unmount)
{
    const char* config_dir = std::getenv("OPENGLAD_CONFIG_DIR");
    ASSERT_TRUE(config_dir != nullptr) << "OPENGLAD_CONFIG_DIR must be set by the test harness";

    namespace fs = std::filesystem;
    const fs::path pack_dir =
        fs::path(config_dir) / "extra_pix" / "coverage_mount_state";
    std::error_code ec;
    fs::remove_all(pack_dir, ec);
    fs::create_directories(pack_dir, ec);

    const std::string orig = cfg.get_setting("graphics", "sprite_sheet");
    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_TRUE(apply_sprite_sheet_setting()) << "start from standard sprite sheet";

    cfg.apply_setting("graphics", "sprite_sheet", "coverage_mount_state");
    ASSERT_TRUE(apply_sprite_sheet_setting()) << "mount coverage sprite sheet";
    ASSERT_TRUE(apply_sprite_sheet_setting())
        << "applying the same mounted sprite sheet should short-circuit";

    ASSERT_TRUE(og::resources::unmount(pack_dir.string().c_str()))
        << "external unmount should create stale mounted-state test condition";
    cfg.apply_setting("graphics", "sprite_sheet", "");
    ASSERT_FALSE(apply_sprite_sheet_setting())
        << "stale mounted state should report failed unmount and keep state";

    ASSERT_TRUE(og::resources::mount(pack_dir.string().c_str(), "pix/", 0))
        << "restore physical mount so apply can clean up its remembered state";
    ASSERT_TRUE(apply_sprite_sheet_setting())
        << "second standard apply should unmount the restored pack";

    cfg.apply_setting("graphics", "sprite_sheet", orig);
    ASSERT_TRUE(apply_sprite_sheet_setting()) << "restore original sprite sheet";
    fs::remove_all(pack_dir, ec);
}
