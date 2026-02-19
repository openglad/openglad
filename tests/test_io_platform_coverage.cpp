#include <openglad/data/pixie_data.h>
#include <openglad/io/og_file.h>
#include <openglad/io/yaml_stream.h>
#include <openglad/io/zip_api.h>
#include <openglad/platform/io_common.h>
#include <openglad/runtime/game_context.h>

#include "test_framework.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

struct MemReadCtx {
    std::string data;
    std::size_t pos = 0;
};

int mem_read_handler(void* data, unsigned char* buffer, std::size_t size, std::size_t* size_read)
{
    auto* ctx = static_cast<MemReadCtx*>(data);
    const std::size_t remain = (ctx->pos < ctx->data.size()) ? (ctx->data.size() - ctx->pos) : 0;
    const std::size_t n = std::min(size, remain);
    if (n > 0)
        std::memcpy(buffer, ctx->data.data() + ctx->pos, n);
    ctx->pos += n;
    *size_read = n;
    return 1;
}

} // namespace

void test_og_file_read_write_seek_and_pixie_paths()
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov";
    const fs::path bin_path = tmp_dir / "rw.bin";
    const fs::path pix_ok = tmp_dir / "ok.pix";
    const fs::path pix_bad = tmp_dir / "bad.pix";

    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    auto out = og::io::og_open_write(bin_path.string().c_str());
    TEST_ASSERT(out != nullptr, "og_open_write should create file");
    if (!out)
        return;

    const unsigned char payload[] = {1, 2, 3, 4, 5};
    TEST_ASSERT(og::io::og_write_exact(*out, payload, 1, sizeof(payload)), "write payload");
    TEST_ASSERT_EQ(5, static_cast<int>(out->tell()), "tell after write");
    TEST_ASSERT_EQ(-1, static_cast<int>(out->seek(0, 99)), "invalid whence should fail");
    TEST_ASSERT_EQ(0, static_cast<int>(out->seek(0, 0)), "seek set should work");
    TEST_ASSERT_EQ(0, static_cast<int>(out->write(payload, 0, 1)), "size=0 write should return 0");
    out.reset();

    auto in = og::io::og_open_read(bin_path.string().c_str(), true);
    TEST_ASSERT(in != nullptr, "og_open_read should open existing file");
    if (!in)
        return;

    unsigned char got[5] = {};
    TEST_ASSERT_EQ(0, static_cast<int>(in->read(got, 0, 1)), "size=0 read should return 0");
    TEST_ASSERT(og::io::og_read_exact(*in, got, 1, sizeof(got)), "read payload");
    TEST_ASSERT(std::memcmp(payload, got, sizeof(payload)) == 0, "roundtrip payload");
    TEST_ASSERT(og::io::og_open_read("temp/io_platform_cov/does_not_exist.bin") == nullptr,
                "missing file should return null");

    {
        std::FILE* f = std::fopen(pix_ok.string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create valid pix");
        if (!f)
            return;
        const unsigned char header[] = {2, 2, 1}; // frames=2, w=2, h=1 => 4-byte payload
        const unsigned char pix_data[] = {7, 8, 9, 10};
        std::fwrite(header, 1, sizeof(header), f);
        std::fwrite(pix_data, 1, sizeof(pix_data), f);
        std::fclose(f);
    }

    PixieData ok = read_pixie_file(pix_ok.string().c_str());
    TEST_ASSERT_EQ(2, static_cast<int>(ok.frames), "pix frames parsed");
    TEST_ASSERT_EQ(2, static_cast<int>(ok.w), "pix width parsed");
    TEST_ASSERT_EQ(1, static_cast<int>(ok.h), "pix height parsed");
    TEST_ASSERT(ok.data != nullptr, "pix payload present");

    {
        std::FILE* f = std::fopen(pix_bad.string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create truncated pix");
        if (!f)
            return;
        const unsigned char header[] = {1, 3, 1}; // expects 3 payload bytes
        const unsigned char short_data[] = {42};
        std::fwrite(header, 1, sizeof(header), f);
        std::fwrite(short_data, 1, sizeof(short_data), f);
        std::fclose(f);
    }

    PixieData bad = read_pixie_file(pix_bad.string().c_str());
    TEST_ASSERT(bad.data == nullptr, "truncated pix should fail payload read");
}
REGISTER_TEST(test_og_file_read_write_seek_and_pixie_paths);

void test_yaml_stream_emitter_and_parser_paths()
{
    const std::string yaml_text =
        "root:\n"
        "  k1: v1\n"
        "  k2: v2\n";
    MemReadCtx reader{yaml_text, 0};
    og::io::YamlParser parser;
    parser.set_input(mem_read_handler, &reader);

    bool saw_pair = false;
    bool saw_scalar = false;
    bool saw_begin_mapping = false;
    og::io::YamlParseResult r = og::io::YamlParseResult::Error;
    do
    {
        r = parser.parse_next();
        const og::io::YamlEvent& ev = parser.event();
        if (ev.type == og::io::YamlEventType::Pair)
            saw_pair = true;
        if (ev.type == og::io::YamlEventType::Scalar)
            saw_scalar = true;
        if (ev.type == og::io::YamlEventType::BeginMapping)
            saw_begin_mapping = true;
    } while (r == og::io::YamlParseResult::Ok);

    parser.close_input();
    TEST_ASSERT(r == og::io::YamlParseResult::Done, "parse should complete successfully");
    TEST_ASSERT(saw_pair, "parser should see pair events");
    TEST_ASSERT(saw_scalar, "parser should see scalar events");
    TEST_ASSERT(saw_begin_mapping, "parser should see mapping events");
}
REGISTER_TEST(test_yaml_stream_emitter_and_parser_paths);

void test_zip_api_roundtrip_and_error_paths()
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
        TEST_ASSERT(f != nullptr, "create zip input payload");
        if (!f)
            return;
        const char* text = "zip payload";
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
    }

    const ArchiveIoError zip_missing_parent_err =
        og::io::zip_contents_with_error(base.string(), missing_parent_zip.string());
    TEST_ASSERT(
        zip_missing_parent_err == ArchiveIoError::OpenArchiveFailed ||
        zip_missing_parent_err == ArchiveIoError::CloseArchiveFailed,
        "zip with missing parent should fail");

    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::None),
                   static_cast<int>(og::io::zip_contents_with_error(base.string(), archive.string())),
                   "zip should succeed");
    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenArchiveFailed),
                   static_cast<int>(og::io::unzip_into_with_error("temp/io_platform_cov_missing.zip", out.string())),
                   "unzip missing archive should fail");
    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::None),
                   static_cast<int>(og::io::unzip_into_with_error(archive.string(), out.string())),
                   "unzip should succeed");

    const fs::path extracted = out / "sub" / "payload.txt";
    TEST_ASSERT(fs::exists(extracted, ec), "extracted payload should exist");
    TEST_ASSERT(fs::exists(out / "emptydir", ec), "empty directory should be preserved");
}
REGISTER_TEST(test_zip_api_roundtrip_and_error_paths);

void test_platform_io_campaign_error_codes()
{
    const std::string prev = ctx().mounted_campaign;
    ctx().mounted_campaign.clear();

    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::EmptyId),
                   static_cast<int>(mount_campaign_package_with_error("")),
                   "empty campaign id should return EmptyId");
    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::EmptyId),
                   static_cast<int>(remount_campaign_package_with_error()),
                   "remount with no mounted campaign should return EmptyId");

    std::map<std::string, int> current_levels;
    const CampaignLoadResult r = load_campaign_with_error("definitely_missing_campaign", current_levels, 7);
    TEST_ASSERT_EQ(static_cast<int>(CampaignLoadError::MountFailed),
                   static_cast<int>(r.error),
                   "load_campaign_with_error should report mount failure");
    TEST_ASSERT_EQ(7, r.current_level, "current level should preserve input first_level");
    TEST_ASSERT_EQ(-2, load_campaign("definitely_missing_campaign", current_levels, 7),
                   "load_campaign should map mount failure to -2");

    ctx().mounted_campaign = prev;
}
REGISTER_TEST(test_platform_io_campaign_error_codes);

void test_platform_io_batch3_mount_switch_and_listing_filters()
{
    namespace fs = std::filesystem;
    const std::string prev = ctx().mounted_campaign;
    const std::string user = get_user_path();
    const fs::path campaigns_dir = fs::path(user) / "campaigns";
    const fs::path scen_dir = fs::path(user) / "scen";
    std::error_code ec;
    fs::create_directories(campaigns_dir, ec);
    fs::create_directories(scen_dir, ec);

    // Hit prev==id short-circuit and prev!=id unmount-failure branch.
    ctx().mounted_campaign = "same.id";
    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None),
                   static_cast<int>(mount_campaign_package_with_error("same.id")),
                   "mount should short-circuit when requested id is already mounted");
    ctx().mounted_campaign = "definitely.not.a.campaign";
    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::UnmountFailed),
                   static_cast<int>(mount_campaign_package_with_error("org.openglad.gladiator")),
                   "mount should report unmount failure when previous mounted id is invalid");

    // list_campaigns should filter non-.glad files.
    {
        std::FILE* f = std::fopen((campaigns_dir / "batch3_filter_marker.glad").string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create .glad marker");
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((campaigns_dir / "batch3_filter_marker.txt").string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create non-glad marker");
        if (f) std::fclose(f);
    }
    const std::list<std::string> campaigns = list_campaigns();
    TEST_ASSERT(std::find(campaigns.begin(), campaigns.end(), "batch3_filter_marker") != campaigns.end(),
                "list_campaigns should keep .glad ids");
    TEST_ASSERT(std::find(campaigns.begin(), campaigns.end(), "batch3_filter_marker.txt") == campaigns.end(),
                "list_campaigns should filter non-.glad names");

    // list_levels/list_levels_v should filter malformed entries and keep strict positive scen ids.
    {
        std::FILE* f = std::fopen((scen_dir / "batch3_note.txt").string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create non-fss level marker");
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((scen_dir / "foo.fss").string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create non-scen fss marker");
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((scen_dir / "scen0.fss").string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create zero-id scen marker");
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((scen_dir / "scen987.fss").string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create valid scen marker");
        if (f) std::fclose(f);
    }
    const std::list<int> levels = list_levels();
    const std::vector<int> levels_v = list_levels_v();
    TEST_ASSERT(std::find(levels.begin(), levels.end(), 987) != levels.end(),
                "list_levels should include strict positive scen id");
    TEST_ASSERT(std::find(levels.begin(), levels.end(), 0) == levels.end(),
                "list_levels should reject scen0");
    TEST_ASSERT(std::find(levels_v.begin(), levels_v.end(), 987) != levels_v.end(),
                "list_levels_v should include strict positive scen id");

    // delete_level early return path when no mounted campaign.
    ctx().mounted_campaign.clear();
    delete_level(987);

    std::remove((campaigns_dir / "batch3_filter_marker.glad").string().c_str());
    std::remove((campaigns_dir / "batch3_filter_marker.txt").string().c_str());
    std::remove((scen_dir / "batch3_note.txt").string().c_str());
    std::remove((scen_dir / "foo.fss").string().c_str());
    std::remove((scen_dir / "scen0.fss").string().c_str());
    std::remove((scen_dir / "scen987.fss").string().c_str());
    ctx().mounted_campaign = prev;
}
REGISTER_TEST(test_platform_io_batch3_mount_switch_and_listing_filters);

void test_og_file_batch3_physfs_seek_and_path_overloads()
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov";
    const fs::path overload_file = tmp_dir / "overload.bin";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    // Exercise path+file open-write overload and stdio-backed seek/tell path.
    auto out = og::io::og_open_write((tmp_dir.string() + "/").c_str(), "overload.bin");
    TEST_ASSERT(out != nullptr, "og_open_write(path,file) should create file");
    if (out)
    {
        const unsigned char bytes[] = {9, 8, 7};
        TEST_ASSERT(og::io::og_write_exact(*out, bytes, 1, sizeof(bytes)), "overload write payload");
        TEST_ASSERT_EQ(3, static_cast<int>(out->tell()), "stdio tell should advance after write");
        TEST_ASSERT_EQ(3, static_cast<int>(out->seek(0, 2)), "stdio seek end should return file size");
    }
    out.reset();

    // Exercise path+file read overload.
    auto in_overload = og::io::og_open_read((tmp_dir.string() + "/").c_str(), "overload.bin");
    TEST_ASSERT(in_overload != nullptr, "og_open_read(path,file) should open written file");
    if (in_overload)
    {
        TEST_ASSERT_EQ(1, static_cast<int>(in_overload->seek(1, 0)), "stdio seek set should work");
        TEST_ASSERT_EQ(1, static_cast<int>(in_overload->tell()), "stdio tell should track seek");
    }

    // PhysFS-backed read path and seek whence variants.
    auto phys = og::io::og_open_read("cfg/openglad.yaml", true);
    TEST_ASSERT(phys != nullptr, "PhysFS read for cfg/openglad.yaml should succeed");
    if (phys)
    {
        const std::int64_t start = phys->seek(0, 0);
        TEST_ASSERT(start >= 0, "physfs seek set should succeed");
        const std::int64_t cur = phys->seek(1, 1);
        TEST_ASSERT(cur >= 1, "physfs seek cur should advance");
        const std::int64_t end = phys->seek(0, 2);
        TEST_ASSERT(end >= cur, "physfs seek end should be >= current position");
        TEST_ASSERT_EQ(-1, static_cast<int>(phys->seek(0, 99)), "physfs invalid whence should fail");
    }

    PixieData missing = read_pixie_file("does_not_exist_batch3.pix");
    TEST_ASSERT(missing.data == nullptr, "missing pix file should return empty PixieData");

    std::remove(overload_file.string().c_str());
}
REGISTER_TEST(test_og_file_batch3_physfs_seek_and_path_overloads);

void test_zip_api_batch3_output_open_failure_and_empty_input_dir()
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
        TEST_ASSERT(f != nullptr, "create zip payload");
        if (f)
        {
            const char* txt = "payload";
            std::fwrite(txt, 1, std::strlen(txt), f);
            std::fclose(f);
        }
    }

    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::None),
                   static_cast<int>(og::io::zip_contents_with_error(in_empty.string(), archive_empty.string())),
                   "zipping an empty existing directory should still succeed");
    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::None),
                   static_cast<int>(og::io::zip_contents_with_error(in_one.string(), archive_one.string())),
                   "zipping a normal directory should succeed");

    {
        std::FILE* f = std::fopen(out_blocker.string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create out blocker file");
        if (f) std::fclose(f);
    }
    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenOutputFailed),
                   static_cast<int>(og::io::unzip_into_with_error(archive_one.string(), out_blocker.string())),
                   "unzip should fail with OpenOutputFailed when outdirectory is a file");

    std::remove(out_blocker.string().c_str());
    fs::remove_all(base, ec);
}
REGISTER_TEST(test_zip_api_batch3_output_open_failure_and_empty_input_dir);

void test_platform_io_delete_level_nonempty_campaign_path()
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
        TEST_ASSERT(f != nullptr, "create scen file");
        if (f) std::fclose(f);
    }
    {
        std::FILE* f = std::fopen((temp_pix / "scen0321.pix").string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create pix file");
        if (f) std::fclose(f);
    }

    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::None),
                   static_cast<int>(og::io::zip_contents_with_error(temp_root.string(), archive.string())),
                   "seed campaign archive should be created");

    const std::string prev = ctx().mounted_campaign;
    ctx().mounted_campaign = id;
    delete_level(321);
    ctx().mounted_campaign = prev;

    cleanup_unpacked_campaign();
    fs::remove(archive, ec);
}
REGISTER_TEST(test_platform_io_delete_level_nonempty_campaign_path);

void test_og_file_open_read_user_and_asset_fallbacks()
{
    namespace fs = std::filesystem;
    const fs::path user_rel = fs::path("batch5_user_read.bin");
    const fs::path user_abs = fs::path(get_user_path()) / user_rel;
    std::error_code ec;

    fs::create_directories(user_abs.parent_path(), ec);

    {
        std::FILE* f = std::fopen(user_abs.string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create user fallback file");
        if (f) {
            const unsigned char b = 17;
            std::fwrite(&b, 1, 1, f);
            std::fclose(f);
        }
    }
    auto user_in = og::io::og_open_read(user_rel.string().c_str(), true);
    TEST_ASSERT(user_in != nullptr, "og_open_read should find file in user path fallback");
    fs::remove(user_abs, ec);
}
REGISTER_TEST(test_og_file_open_read_user_and_asset_fallbacks);
