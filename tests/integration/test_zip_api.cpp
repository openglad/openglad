#include <openglad/resources/zip_api.h>
#include <openglad/resources/io.h>
#if __has_include(<catch2/catch_test_macros.hpp>)
#include <catch2/catch_test_macros.hpp>
#endif
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

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

TEST(ZipApi, r15_zip_and_unzip_success_paths)
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
    ASSERT_EQ(ArchiveIoError::None, zip_err);

    const ArchiveIoError unzip_err = og::io::unzip_into_with_error(zipfile.string(), out.string());
    ASSERT_EQ(ArchiveIoError::None, unzip_err);

    ASSERT_TRUE(fs::exists(out / "root.txt"));
    ASSERT_TRUE(fs::exists(out / "subdir" / "nested.txt"));
}

TEST(ZipApi, r15_error_paths_for_open_archive_and_output)
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
    ASSERT_TRUE(zip_open_fail == ArchiveIoError::OpenArchiveFailed);

    ASSERT_TRUE(og::io::zip_contents_with_error(in.string(), zipfile.string()) == ArchiveIoError::None);

    // Missing archive -> open archive failure.
    const ArchiveIoError missing = og::io::unzip_into_with_error((base / "missing.zip").string(), (base / "missing_out").string());
    ASSERT_TRUE(missing == ArchiveIoError::OpenArchiveFailed);

    // Output file blocks extracted file creation -> open output failure.
    const fs::path blocked = base / "blocked";
    {
        std::ofstream out_file(blocked.string(), std::ios::binary);
        out_file << "block";
    }
    ASSERT_TRUE(og::io::unzip_into_with_error(zipfile.string(), blocked.string()) == ArchiveIoError::OpenOutputFailed);
}

namespace {

std::uint32_t crc32_of(const std::string& bytes)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char ch : bytes)
    {
        crc ^= static_cast<unsigned char>(ch);
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void put16(std::string& out, std::uint32_t v)
{
    out.push_back(static_cast<char>(v & 0xFFu));
    out.push_back(static_cast<char>((v >> 8) & 0xFFu));
}

void put32(std::string& out, std::uint32_t v)
{
    put16(out, v & 0xFFFFu);
    put16(out, (v >> 16) & 0xFFFFu);
}

// A minimal STORED zip (no compression, no extra fields) whose entries are
// written verbatim — including names libzip's own writer would never emit.
std::string stored_zip(
    const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::string body;
    std::string central;
    for (const auto& [name, data] : entries)
    {
        const auto offset = static_cast<std::uint32_t>(body.size());
        const std::uint32_t crc = crc32_of(data);
        const auto size = static_cast<std::uint32_t>(data.size());
        const auto name_len = static_cast<std::uint32_t>(name.size());
        put32(body, 0x04034b50u);
        put16(body, 20); put16(body, 0); put16(body, 0);  // version, flags, stored
        put16(body, 0); put16(body, 0x21);                // time, date
        put32(body, crc); put32(body, size); put32(body, size);
        put16(body, name_len); put16(body, 0);
        body += name;
        body += data;

        put32(central, 0x02014b50u);
        put16(central, 20); put16(central, 20); put16(central, 0);
        put16(central, 0); put16(central, 0); put16(central, 0x21);
        put32(central, crc); put32(central, size); put32(central, size);
        put16(central, name_len); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0);
        put32(central, offset);
        central += name;
    }
    std::string out = body + central;
    put32(out, 0x06054b50u);
    put16(out, 0); put16(out, 0);
    put16(out, static_cast<std::uint32_t>(entries.size()));
    put16(out, static_cast<std::uint32_t>(entries.size()));
    put32(out, static_cast<std::uint32_t>(central.size()));
    put32(out, static_cast<std::uint32_t>(body.size()));
    put16(out, 0);
    return out;
}

} // namespace

// An archive entry with an EMPTY name names no file: extraction skips it and
// carries on, so the rest of the package still unpacks and the archive is
// not reported broken. Control: the same archive without the nameless entry
// extracts the same file with the same result.
TEST(ZipApi, unzip_skips_an_entry_with_an_empty_name)
{
    const fs::path base = mk_r15_dir("empty_name");
    const fs::path zipfile = base / "nameless.zip";
    const fs::path control_zip = base / "control.zip";
    {
        std::ofstream f(zipfile, std::ios::binary);
        f << stored_zip({{"", ""}, {"keep.txt", "keep"}});
    }
    {
        std::ofstream f(control_zip, std::ios::binary);
        f << stored_zip({{"keep.txt", "keep"}});
    }

    ASSERT_EQ(ArchiveIoError::None,
              og::io::unzip_into_with_error(control_zip.string(),
                                            (base / "control_out").string()))
        << "control: the hand-built archive format is one libzip reads";
    ASSERT_TRUE(fs::exists(base / "control_out" / "keep.txt"));

    const fs::path out = base / "out";
    EXPECT_EQ(ArchiveIoError::None,
              og::io::unzip_into_with_error(zipfile.string(), out.string()))
        << "a nameless entry must be skipped, not fail the unpack";
    std::ifstream kept(out / "keep.txt", std::ios::binary);
    const std::string kept_bytes{std::istreambuf_iterator<char>(kept),
                                 std::istreambuf_iterator<char>()};
    EXPECT_EQ("keep", kept_bytes) << "the named entry after it still extracts";
    std::size_t files = 0;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(out, ec))
        if (entry.is_regular_file())
            ++files;
    EXPECT_EQ(1u, files) << "the nameless entry must produce no file";
}
