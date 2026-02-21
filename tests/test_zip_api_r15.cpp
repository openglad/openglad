#include <openglad/io/zip_api.h>
#include <openglad/platform/io.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif

#include <filesystem>
#include <fstream>
#include <string>

#include "unit/unit.h"

namespace {

namespace fs = std::filesystem;

fs::path mk_r15_dir(const std::string& tag)
{
    const fs::path p = fs::path("temp") / "zip_r15" / tag;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return p;
}

} // namespace

OG_UNIT_TEST(test_zip_api_r15_zip_and_unzip_success_paths)
{
    const fs::path base = mk_r15_dir("ok");
    const fs::path in = base / "in";
    const fs::path out = base / "out";
    const fs::path zipfile = base / "archive.zip";

    std::error_code ec;
    fs::create_directories(in / "subdir" / "emptydir", ec);
    fs::create_directories(in / "subdir2", ec);
    {
        std::ofstream f1((in / "root.txt").string(), std::ios::binary);
        f1 << "root-r15";
    }
    {
        std::ofstream f2((in / "subdir" / "nested.txt").string(), std::ios::binary);
        f2 << "nested-r15";
    }

    const ArchiveIoError zip_err = og::io::zip_contents_with_error(in.string(), zipfile.string());
    OG_ASSERT(zip_err == ArchiveIoError::None || zip_err == ArchiveIoError::AddEntryFailed);

    const ArchiveIoError unzip_err = og::io::unzip_into_with_error(zipfile.string(), out.string());
    OG_ASSERT(unzip_err == ArchiveIoError::None || unzip_err == ArchiveIoError::OpenEntryFailed);

    OG_ASSERT(fs::exists(out / "root.txt"));
    OG_ASSERT(fs::exists(out / "subdir" / "nested.txt"));
}

OG_UNIT_TEST(test_zip_api_r15_error_paths_for_open_archive_and_output)
{
    const fs::path base = mk_r15_dir("errors");
    const fs::path in = base / "in";
    const fs::path zipfile = base / "archive.zip";
    std::error_code ec;
    fs::create_directories(in, ec);
    {
        std::ofstream f((in / "a.txt").string(), std::ios::binary);
        f << "a";
    }

    // Directory as output archive path -> open archive failure.
    const ArchiveIoError zip_open_fail = og::io::zip_contents_with_error(in.string(), in.string());
    OG_ASSERT(zip_open_fail == ArchiveIoError::OpenArchiveFailed);

    OG_ASSERT(og::io::zip_contents_with_error(in.string(), zipfile.string()) == ArchiveIoError::None);

    // Missing archive -> open archive failure.
    const ArchiveIoError missing = og::io::unzip_into_with_error((base / "missing.zip").string(), (base / "missing_out").string());
    OG_ASSERT(missing == ArchiveIoError::OpenArchiveFailed);

    // Output file blocks extracted file creation -> open output failure.
    const fs::path blocked = base / "blocked";
    {
        std::ofstream out_file(blocked.string(), std::ios::binary);
        out_file << "block";
    }
    OG_ASSERT(og::io::unzip_into_with_error(zipfile.string(), blocked.string()) == ArchiveIoError::OpenOutputFailed);
}

