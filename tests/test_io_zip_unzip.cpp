#include "io.h"
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
}
REGISTER_TEST(test_io_open_read_file_prefers_cwd_fallback);

void test_io_mount_unmount_campaign_invalid_id_paths()
{
    TEST_ASSERT(!mount_campaign_package(""), "mount_campaign_package(\"\") should fail");
    TEST_ASSERT(!mount_campaign_package("definitely.not.a.campaign"), "mount invalid campaign should fail");
    TEST_ASSERT(unmount_campaign_package(""), "unmount_campaign_package(\"\") should succeed");
}
REGISTER_TEST(test_io_mount_unmount_campaign_invalid_id_paths);

