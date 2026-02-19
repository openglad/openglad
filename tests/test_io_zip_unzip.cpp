#include <openglad/platform/io.h>
#include "test_framework.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

static bool write_file_bytes(const std::string& path, const std::string& contents)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    size_t written = fwrite(contents.data(), 1, contents.size(), f);
    fclose(f);
    return written == contents.size();
}

static bool read_file_all(const std::string& path, std::string* out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::string tmp;
    char buf[256];
    while (1) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n == 0)
            break;
        tmp.append(buf, n);
    }
    fclose(f);
    if (out)
        *out = tmp;
    return true;
}

void test_io_zip_contents_and_unzip_into_roundtrip()
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_" + std::to_string(::getpid()));
    fs::path indir = base / "in";
    fs::path outdir = base / "out";
    fs::path zipfile = base / "bundle.zip";

    TEST_ASSERT(create_dir(indir.string()), "create_dir(in) should succeed");
    TEST_ASSERT(create_dir((indir / "sub").string()), "create_dir(sub) should succeed");

    TEST_ASSERT(write_file_bytes((indir / "a.txt").string(), "AAA"), "write a.txt");
    TEST_ASSERT(write_file_bytes((indir / "sub" / "b.txt").string(), "BBB"), "write sub/b.txt");

    TEST_ASSERT(zip_contents(indir.string(), zipfile.string()), "zip_contents should succeed");
    TEST_ASSERT(create_dir(outdir.string()), "create_dir(out) should succeed");
    TEST_ASSERT(unzip_into(zipfile.string(), outdir.string()), "unzip_into should succeed");

    std::string a, b;
    TEST_ASSERT(read_file_all((outdir / "a.txt").string(), &a), "read out a.txt");
    TEST_ASSERT(read_file_all((outdir / "sub" / "b.txt").string(), &b), "read out sub/b.txt");
    TEST_ASSERT(a == "AAA", "a.txt contents");
    TEST_ASSERT(b == "BBB", "b.txt contents");
}
REGISTER_TEST(test_io_zip_contents_and_unzip_into_roundtrip);

void test_io_open_read_file_prefers_cwd_fallback()
{
    // Create a file in the current working directory (ctest runs from repo root).
    std::string fname = std::string("io_tmp_") + std::to_string(::getpid()) + ".txt";
    TEST_ASSERT(write_file_bytes(fname, "cwd"), "write temp file in cwd");

    SDL_RWops* rw = open_read_file(fname.c_str(), true);
    TEST_ASSERT(rw != nullptr, "open_read_file should open cwd file");
    char buf[8] = {0};
    size_t got = SDL_RWread(rw, buf, 1, 3);
    SDL_RWclose(rw);
    TEST_ASSERT_EQ(3, (int)got, "should read 3 bytes");
    TEST_ASSERT_STR_EQ("cwd", buf, "contents should match");

    // Clean up transient test file so it doesn't accumulate in the repo root.
    std::remove(fname.c_str());
}
REGISTER_TEST(test_io_open_read_file_prefers_cwd_fallback);

void test_io_mount_unmount_campaign_invalid_id_paths()
{
    TEST_ASSERT(!mount_campaign_package(""), "mount_campaign_package(\"\") should fail");
    TEST_ASSERT(!mount_campaign_package("definitely.not.a.campaign"), "mount invalid campaign should fail");
    TEST_ASSERT(unmount_campaign_package(""), "unmount_campaign_package(\"\") should succeed");
}
REGISTER_TEST(test_io_mount_unmount_campaign_invalid_id_paths);

void test_io_mount_unmount_campaign_typed_errors()
{
    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::EmptyId),
        static_cast<int>(mount_campaign_package_with_error("")),
        "mount_campaign_package_with_error(\"\") should return EmptyId");
    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::MountFailed),
        static_cast<int>(mount_campaign_package_with_error("definitely.not.a.campaign")),
        "mount invalid campaign should return MountFailed");
    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None),
        static_cast<int>(unmount_campaign_package_with_error("")),
        "unmount_campaign_package_with_error(\"\") should return None");
    TEST_ASSERT_EQ(static_cast<int>(CampaignPackageIoError::EmptyId),
        static_cast<int>(remount_campaign_package_with_error()),
        "remount with no mounted campaign should return EmptyId");
}
REGISTER_TEST(test_io_mount_unmount_campaign_typed_errors);

void test_io_zip_unzip_typed_errors()
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_typed_" + std::to_string(::getpid()));
    fs::path indir = base / "typed_in";
    fs::path zipfile = base / "typed_bundle.zip";
    fs::path missing_zip = base / "does_not_exist.zip";

    TEST_ASSERT(create_dir(indir.string()), "create_dir typed_in should succeed");
    TEST_ASSERT(write_file_bytes((indir / "a.txt").string(), "A"), "write typed a.txt");

    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::None),
        static_cast<int>(zip_contents_with_error(indir.string(), zipfile.string())),
        "zip_contents_with_error should return None on success");
    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenArchiveFailed),
        static_cast<int>(unzip_into_with_error(missing_zip.string(), (base / "typed_out").string())),
        "unzip_into_with_error should return OpenArchiveFailed for missing archive");
}
REGISTER_TEST(test_io_zip_unzip_typed_errors);

void test_io_zip_contents_with_error_missing_input_directory_path()
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_missing_in_" + std::to_string(::getpid()));
    fs::path missing = base / "no_such_dir";
    fs::path zipfile = base / "empty_from_missing.zip";
    std::filesystem::create_directories(base);

    const ArchiveIoError r = zip_contents_with_error(missing.string(), zipfile.string());
    TEST_ASSERT(r == ArchiveIoError::None || r == ArchiveIoError::OpenArchiveFailed,
                "missing input dir should not report add-entry errors");
}
REGISTER_TEST(test_io_zip_contents_with_error_missing_input_directory_path);

void test_io_zip_contents_with_error_open_archive_failed_path()
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_zip_open_fail_" + std::to_string(::getpid()));
    fs::path indir = base / "in";
    fs::path bad_zip = indir; // output points to directory -> zip_open should fail

    TEST_ASSERT(create_dir(indir.string()), "create_dir in should succeed");
    TEST_ASSERT(write_file_bytes((indir / "a.txt").string(), "A"), "write input file");

    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenArchiveFailed),
        static_cast<int>(zip_contents_with_error(indir.string(), bad_zip.string())),
        "zip_contents_with_error should return OpenArchiveFailed when output path is a directory");
}
REGISTER_TEST(test_io_zip_contents_with_error_open_archive_failed_path);

void test_io_unzip_with_error_open_output_failed_path()
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_unzip_open_output_fail_" + std::to_string(::getpid()));
    fs::path indir = base / "in";
    fs::path zipfile = base / "bundle.zip";
    fs::path out_as_file = base / "out_file";

    TEST_ASSERT(create_dir(indir.string()), "create_dir in should succeed");
    TEST_ASSERT(write_file_bytes((indir / "a.txt").string(), "ABC"), "write zip input");
    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::None),
        static_cast<int>(zip_contents_with_error(indir.string(), zipfile.string())),
        "zip creation should succeed");

    // out_as_file is a regular file; extracting into "out_as_file/<entry>" should fail fopen.
    TEST_ASSERT(write_file_bytes(out_as_file.string(), "marker"), "create output blocker file");
    TEST_ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenOutputFailed),
        static_cast<int>(unzip_into_with_error(zipfile.string(), out_as_file.string())),
        "unzip_into_with_error should report OpenOutputFailed when output path is blocked by a file");
}
REGISTER_TEST(test_io_unzip_with_error_open_output_failed_path);
