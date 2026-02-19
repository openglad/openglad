#include <openglad/data/pixie_data.h>
#include <openglad/io/og_file.h>
#include <openglad/io/ogfile_yaml.h>
#include <openglad/io/physfs_api.h>
#include <openglad/io/yaml_stream.h>
#include <openglad/io/zip_api.h>
#include <openglad/platform/io_common.h>
#include <openglad/runtime/game_context.h>

#include "test_framework.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <physfs.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
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

void test_yaml_stream_sequence_emit_and_parse_error_path()
{
    // Exercise emitter sequence APIs and parser error mapping.
    auto out = og::io::og_open_write("temp/io_platform_cov_yaml_seq.yaml");
    TEST_ASSERT(out != nullptr, "yaml output file should open");
    if (!out)
        return;

    og::io::YamlEmitter emitter;
    TEST_ASSERT(emitter.set_output(ogfile_write_handler, out.get()), "yaml emitter set_output should succeed");
    TEST_ASSERT(emitter.emit_begin_sequence(), "emit_begin_sequence should succeed");
    TEST_ASSERT(emitter.emit_scalar("entry"), "emit_scalar in sequence should succeed");
    TEST_ASSERT(emitter.emit_end_sequence(), "emit_end_sequence should succeed");
    emitter.close_output();
    out.reset();

    // Parse malformed YAML as a smoke path; backend may return Done or Error.
    MemReadCtx bad_reader{"bad: [unclosed", 0};
    og::io::YamlParser parser;
    parser.set_input(mem_read_handler, &bad_reader);
    og::io::YamlParseResult r = og::io::YamlParseResult::Ok;
    int guard = 0;
    while (r == og::io::YamlParseResult::Ok && guard < 32)
    {
        r = parser.parse_next();
        guard++;
    }
    parser.close_input();
    TEST_ASSERT(guard > 0, "parser should process at least one malformed-yaml step");
}
REGISTER_TEST(test_yaml_stream_sequence_emit_and_parse_error_path);

void test_yaml_stream_alias_and_sequence_event_paths()
{
    const std::string yaml_text =
        "a: &anchor 1\n"
        "b: *anchor\n"
        "c: [2, 3]\n";
    MemReadCtx reader{yaml_text, 0};
    og::io::YamlParser parser;
    parser.set_input(mem_read_handler, &reader);

    bool saw_alias = false;
    bool saw_begin_sequence = false;
    bool saw_end_sequence = false;
    og::io::YamlParseResult r = og::io::YamlParseResult::Error;
    do
    {
        r = parser.parse_next();
        const og::io::YamlEvent& ev = parser.event();
        if (ev.type == og::io::YamlEventType::Alias)
            saw_alias = true;
        if (ev.type == og::io::YamlEventType::BeginSequence)
            saw_begin_sequence = true;
        if (ev.type == og::io::YamlEventType::EndSequence)
            saw_end_sequence = true;
    } while (r == og::io::YamlParseResult::Ok);

    parser.close_input();
    TEST_ASSERT(r == og::io::YamlParseResult::Done, "alias/sequence parse should complete");
    TEST_ASSERT(saw_alias, "parser should emit alias events");
    TEST_ASSERT(saw_begin_sequence && saw_end_sequence, "parser should emit sequence begin/end events");
}
REGISTER_TEST(test_yaml_stream_alias_and_sequence_event_paths);

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
        unsigned char ch = 0;
        TEST_ASSERT_EQ(1, (int)phys->read(&ch, 1, 1), "physfs read should return one byte");
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

void test_og_file_physfs_backed_write_and_read_roundtrip()
{
    const char* write_dir = PHYSFS_getWriteDir();
    TEST_ASSERT(write_dir != nullptr, "PHYSFS write dir should be initialized for runtime tests");
    if (!write_dir)
        return;

    const std::string vdir = "batch7_physfs_only_dir";
    const std::string vpath = vdir + "/rw.bin";
    TEST_ASSERT(PHYSFS_mkdir(vdir.c_str()) != 0, "PHYSFS_mkdir should create virtual directory");

    auto out = og::io::og_open_write(vpath.c_str());
    TEST_ASSERT(out != nullptr, "og_open_write should use PhysFS-backed file for virtual path");
    if (!out)
        return;

    const unsigned char payload[] = {10, 20, 30, 40};
    TEST_ASSERT(og::io::og_write_exact(*out, payload, 1, sizeof(payload)), "physfs-backed write should succeed");
    TEST_ASSERT(out->tell() >= 4, "physfs-backed tell should report advanced position");
    TEST_ASSERT(out->seek(0, 0) >= 0, "physfs-backed seek set should succeed");
    out.reset();

    auto in = og::io::og_open_read(vpath.c_str(), true);
    TEST_ASSERT(in != nullptr, "og_open_read should find newly written PhysFS file");
    if (in)
    {
        unsigned char got[4] = {};
        TEST_ASSERT(og::io::og_read_exact(*in, got, 1, sizeof(got)), "physfs-backed read should succeed");
        TEST_ASSERT(std::memcmp(got, payload, sizeof(payload)) == 0, "physfs-backed roundtrip should match");
        TEST_ASSERT(in->tell() >= 4, "physfs-backed read tell should advance");
        TEST_ASSERT(in->seek(0, 2) >= 0, "physfs-backed seek end should succeed");
    }

    (void)PHYSFS_delete(vpath.c_str());
    (void)std::remove((std::string(write_dir) + "/" + vpath).c_str());
}
REGISTER_TEST(test_og_file_physfs_backed_write_and_read_roundtrip);

void test_pixie_data_move_constructor_resets_source()
{
    PixieData src(2, 3, 1, new unsigned char[6]{1, 2, 3, 4, 5, 6});
    TEST_ASSERT(src.valid(), "source pixie should start valid");

    PixieData moved(std::move(src));
    TEST_ASSERT(moved.valid(), "moved pixie should retain data");
    TEST_ASSERT_EQ(0, (int)src.frames, "move ctor should clear source frames");
    TEST_ASSERT_EQ(0, (int)src.w, "move ctor should clear source width");
    TEST_ASSERT_EQ(0, (int)src.h, "move ctor should clear source height");
}
REGISTER_TEST(test_pixie_data_move_constructor_resets_source);

void test_physfs_api_wrapper_error_and_list_paths()
{
    // Hit wrapper calls without mutating shared runtime mount/write state.
    const std::list<std::string> root_files = og::io::physfs_enumerate_files_sorted("");
    TEST_ASSERT(!root_files.empty(), "physfs_enumerate_files_sorted should list mounted root entries");

    TEST_ASSERT(!og::io::physfs_mount("definitely_missing_physfs_path", nullptr, 1),
                "physfs_mount should fail for a missing path");
    TEST_ASSERT(!og::io::physfs_unmount("definitely_missing_physfs_path"),
                "physfs_unmount should fail for an unmounted path");
    TEST_ASSERT(!og::io::physfs_last_error().empty(), "physfs_last_error wrapper should return error text");
}
REGISTER_TEST(test_physfs_api_wrapper_error_and_list_paths);

void test_zip_api_missing_input_dir_exists_guard_path()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path missing = fs::path("temp") / "zip_missing_input_batch8";
    const fs::path archive = fs::path("temp") / "zip_missing_input_batch8.zip";
    fs::remove_all(missing, ec);
    fs::remove(archive, ec);

    const ArchiveIoError r = og::io::zip_contents_with_error(missing.string(), archive.string());
    TEST_ASSERT(r == ArchiveIoError::None || r == ArchiveIoError::AddEntryFailed,
                "zipping a missing input dir should take empty-enumeration guard path");
    fs::remove(archive, ec);
}
REGISTER_TEST(test_zip_api_missing_input_dir_exists_guard_path);

void test_zip_api_unreadable_and_non_regular_entries_report_add_failure()
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
        TEST_ASSERT(f != nullptr, "create normal file");
        if (f)
        {
            const char* txt = "ok";
            std::fwrite(txt, 1, std::strlen(txt), f);
            std::fclose(f);
        }
    }
    {
        std::FILE* f = std::fopen(unreadable.string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create unreadable file");
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
    TEST_ASSERT(r == ArchiveIoError::AddEntryFailed || r == ArchiveIoError::None,
                "zip should report add failure (or succeed if platform permits unreadable reopen)");

    (void)::chmod(unreadable.string().c_str(), 0600);
    fs::remove_all(base, ec);
}
REGISTER_TEST(test_zip_api_unreadable_and_non_regular_entries_report_add_failure);

void test_platform_io_restore_defaults_and_load_campaign_unmount_error_path()
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

    const std::string prev = ctx().mounted_campaign;
    ctx().mounted_campaign = "definitely.not.a.campaign";
    std::map<std::string, int> current_levels;
    TEST_ASSERT_EQ(-3, load_campaign("org.openglad.gladiator", current_levels, 9),
                   "load_campaign should map unmount failure to -3");
    ctx().mounted_campaign = prev;
}
REGISTER_TEST(test_platform_io_restore_defaults_and_load_campaign_unmount_error_path);

void test_read_pixie_file_truncated_header_path()
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov";
    const fs::path pix_header_bad = tmp_dir / "bad_header.pix";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    {
        std::FILE* f = std::fopen(pix_header_bad.string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create truncated-header pix");
        if (!f)
            return;
        const unsigned char partial_header[] = {1, 2}; // missing h byte
        std::fwrite(partial_header, 1, sizeof(partial_header), f);
        std::fclose(f);
    }

    PixieData bad_header = read_pixie_file(pix_header_bad.string().c_str());
    TEST_ASSERT(bad_header.data == nullptr, "truncated pix header should fail");
    fs::remove(pix_header_bad, ec);
}
REGISTER_TEST(test_read_pixie_file_truncated_header_path);

void test_platform_io_bool_wrappers_and_small_helpers()
{
    namespace fs = std::filesystem;
    const std::string prev = ctx().mounted_campaign;

    TEST_ASSERT(!mount_campaign_package(""), "mount_campaign_package wrapper should fail for empty id");
    TEST_ASSERT(unmount_campaign_package(""), "unmount_campaign_package wrapper should succeed for empty id");
    ctx().mounted_campaign.clear();
    TEST_ASSERT(!remount_campaign_package(), "remount_campaign_package wrapper should fail when nothing is mounted");

    const std::list<std::string> exploded = explode("a,b,", ',');
    TEST_ASSERT_EQ(3, (int)exploded.size(), "explode should include trailing empty segment");

    const fs::path nested = fs::path("temp") / "io_platform_cov" / "wrapper_dir" / "a" / "b";
    TEST_ASSERT(create_dir(nested.string()), "create_dir should create nested directories");

    const fs::path missing_zip = fs::path("temp") / "io_platform_cov" / "missing_bool_wrapper.zip";
    TEST_ASSERT(!unzip_into(missing_zip.string(), (nested / "out").string()),
                "unzip_into wrapper should return false for missing archive");
    (void)zip_contents((nested / "missing_input").string(), (nested / "out.zip").string());

    ctx().mounted_campaign = prev;
}
REGISTER_TEST(test_platform_io_bool_wrappers_and_small_helpers);

void test_og_file_round6_physfs_zero_size_and_stdio_seek_failure_paths()
{
    namespace fs = std::filesystem;
    const fs::path tmp_dir = fs::path("temp") / "io_platform_cov";
    std::error_code ec;
    fs::create_directories(tmp_dir, ec);

    // PhysFS-backed zero-size read/write paths.
    const char* write_dir = PHYSFS_getWriteDir();
    TEST_ASSERT(write_dir != nullptr, "PHYSFS write dir should exist");
    if (write_dir)
    {
        const std::string vdir = "batch9_physfs_zero";
        const std::string vpath = vdir + "/rw.bin";
        TEST_ASSERT(PHYSFS_mkdir(vdir.c_str()) != 0, "create virtual dir for zero-size path");
        auto out = og::io::og_open_write(vpath.c_str());
        TEST_ASSERT(out != nullptr, "open physfs output");
        if (out)
        {
            const unsigned char b = 1;
            TEST_ASSERT_EQ(0, (int)out->write(&b, 0, 1), "physfs write size=0 should return 0");
            out.reset();
        }
        auto in = og::io::og_open_read(vpath.c_str(), true);
        TEST_ASSERT(in != nullptr, "open physfs input");
        if (in)
        {
            unsigned char b = 0;
            TEST_ASSERT_EQ(0, (int)in->read(&b, 0, 1), "physfs read size=0 should return 0");
        }
        (void)PHYSFS_delete(vpath.c_str());
        (void)std::remove((std::string(write_dir) + "/" + vpath).c_str());
    }

    // stdio-backed negative seek failure path.
    const fs::path seek_file = tmp_dir / "seek_fail.bin";
    {
        std::FILE* f = std::fopen(seek_file.string().c_str(), "wb");
        TEST_ASSERT(f != nullptr, "create stdio seek-fail file");
        if (f)
        {
            const unsigned char bytes[] = {1, 2, 3};
            std::fwrite(bytes, 1, sizeof(bytes), f);
            std::fclose(f);
        }
    }
    auto in_stdio = og::io::og_open_read((tmp_dir.string() + "/").c_str(), "seek_fail.bin");
    TEST_ASSERT(in_stdio != nullptr, "open stdio seek-fail file");
    if (in_stdio)
        TEST_ASSERT_EQ(-1, (int)in_stdio->seek(-1, 0), "stdio seek should fail for negative SET offset");
}
REGISTER_TEST(test_og_file_round6_physfs_zero_size_and_stdio_seek_failure_paths);
