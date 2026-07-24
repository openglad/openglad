#include <openglad/resources/io.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>
#include <physfs.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "zip.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

class ScopedTemporaryTree
{
public:
    explicit ScopedTemporaryTree(std::filesystem::path path)
        : path_(std::move(path))
    {
        std::filesystem::remove_all(path_, error_);
    }

    ~ScopedTemporaryTree()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
        if (error)
            ADD_FAILURE() << "temporary tree cleanup failed for " << path_
                          << ": " << error.message();
    }

    [[nodiscard]] bool ready() const noexcept { return !error_; }
    [[nodiscard]] const std::error_code& error() const noexcept
    {
        return error_;
    }

private:
    std::filesystem::path path_;
    std::error_code error_;
};

struct ZipDiscard
{
    void operator()(zip* archive) const noexcept
    {
        if (archive != nullptr)
            zip_discard(archive);
    }
};

using ScopedZipArchive = std::unique_ptr<zip, ZipDiscard>;

bool write_single_entry_zip(const std::filesystem::path& archive_path,
                            const std::string& entry_name,
                            std::string_view payload)
{
    int error = 0;
    ScopedZipArchive archive(zip_open(
        archive_path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error));
    if (archive == nullptr)
        return false;

    zip_source* source = zip_source_buffer(
        archive.get(), payload.data(), payload.size(), 0);
    if (source == nullptr)
        return false;
    if (zip_file_add(archive.get(), entry_name.c_str(), source,
                     ZIP_FL_OVERWRITE) < 0)
    {
        zip_source_free(source);
        return false;
    }

    zip* const raw_archive = archive.release();
    const int close_result = zip_close(raw_archive);
    if (close_result != 0)
        zip_discard(raw_archive);
    return close_result == 0;
}

bool write_special_entry_zip(const std::filesystem::path& archive_path,
                             const std::string& entry_name,
                             std::string_view payload,
                             const char* password = nullptr)
{
    int error = 0;
    ScopedZipArchive archive(zip_open(
        archive_path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error));
    if (archive == nullptr)
        return false;

    zip_source* source = zip_source_buffer(
        archive.get(), payload.data(), payload.size(), 0);
    if (source == nullptr)
        return false;
    const zip_int64_t index = zip_file_add(
        archive.get(), entry_name.c_str(), source, ZIP_FL_OVERWRITE);
    if (index < 0)
    {
        zip_source_free(source);
        return false;
    }
    if (zip_set_file_compression(
            archive.get(), static_cast<zip_uint64_t>(index),
            ZIP_CM_DEFLATE, 9) != 0)
    {
        return false;
    }
    if (password != nullptr &&
        zip_file_set_encryption(
            archive.get(), static_cast<zip_uint64_t>(index),
            ZIP_EM_TRAD_PKWARE, password) != 0)
    {
        return false;
    }
    zip* const raw_archive = archive.release();
    const int close_result = zip_close(raw_archive);
    if (close_result != 0)
        zip_discard(raw_archive);
    return close_result == 0;
}

std::vector<std::uint8_t> read_binary_file(
    const std::filesystem::path& path)
{
    FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr)
        return {};
    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 4096> buffer{};
    while (true)
    {
        const std::size_t count =
            std::fread(buffer.data(), 1, buffer.size(), file);
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() +
                     static_cast<std::ptrdiff_t>(count));
        if (count != buffer.size())
            break;
    }
    std::fclose(file);
    return bytes;
}

bool write_binary_file(const std::filesystem::path& path,
                       const std::vector<std::uint8_t>& bytes)
{
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr)
        return false;
    const std::size_t count =
        std::fwrite(bytes.data(), 1, bytes.size(), file);
    return std::fclose(file) == 0 && count == bytes.size();
}

std::uint16_t read_le16(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
}

std::uint32_t read_le32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void write_le32(std::vector<std::uint8_t>& bytes, std::size_t offset,
                std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::size_t find_zip_signature(const std::vector<std::uint8_t>& bytes,
                               std::uint32_t signature)
{
    for (std::size_t i = 0; i + sizeof(signature) <= bytes.size(); ++i)
    {
        if (read_le32(bytes, i) == signature)
            return i;
    }
    return bytes.size();
}

} // namespace

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

TEST(IoZipUnzip, io_zip_contents_and_unzip_into_roundtrip)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_" + std::to_string(::getpid()));
    fs::path indir = base / "in";
    fs::path outdir = base / "out";
    fs::path zipfile = base / "bundle.zip";

    ASSERT_TRUE(create_dir(indir.string())) << "create_dir(in) should succeed";
    ASSERT_TRUE(create_dir((indir / "sub").string())) << "create_dir(sub) should succeed";

    ASSERT_TRUE(write_file_bytes((indir / "a.txt").string(), "AAA")) << "write a.txt";
    ASSERT_TRUE(write_file_bytes((indir / "sub" / "b.txt").string(), "BBB")) << "write sub/b.txt";

    ASSERT_TRUE(zip_contents_with_error(indir.string(), zipfile.string()) == ArchiveIoError::None) << "zip_contents should succeed";
    ASSERT_TRUE(create_dir(outdir.string())) << "create_dir(out) should succeed";
    ASSERT_TRUE(unzip_into_with_error(zipfile.string(), outdir.string()) == ArchiveIoError::None) << "unzip_into should succeed";

    std::string a, b;
    ASSERT_TRUE(read_file_all((outdir / "a.txt").string(), &a)) << "read out a.txt";
    ASSERT_TRUE(read_file_all((outdir / "sub" / "b.txt").string(), &b)) << "read out sub/b.txt";
    ASSERT_TRUE(a == "AAA") << "a.txt contents";
    ASSERT_TRUE(b == "BBB") << "b.txt contents";
}


TEST(IoZipUnzip, io_open_read_file_prefers_cwd_fallback)
{
    // Create a file in the current working directory (ctest runs from repo root).
    std::string fname = std::string("io_tmp_") + std::to_string(::getpid()) + ".txt";
    ASSERT_TRUE(write_file_bytes(fname, "cwd")) << "write temp file in cwd";

    SDL_IOStream* rw = open_read_file(fname.c_str(), true);
    ASSERT_TRUE(rw != nullptr) << "open_read_file should open cwd file";
    char buf[8] = {0};
    size_t got = SDL_ReadIO(rw, buf, 3);
    SDL_CloseIO(rw);
    ASSERT_EQ(3, (int)got) << "should read 3 bytes";
    ASSERT_STREQ("cwd", buf) << "contents should match";

    // Clean up transient test file so it doesn't accumulate in the repo root.
    std::remove(fname.c_str());
}


TEST(IoZipUnzip, io_mount_unmount_campaign_invalid_id_paths)
{
    ASSERT_TRUE(mount_campaign_package_with_error("") != CampaignPackageIoError::None) << "mount_campaign_package(\"\") should fail";
    ASSERT_TRUE(mount_campaign_package_with_error("definitely.not.a.campaign") != CampaignPackageIoError::None) << "mount invalid campaign should fail";
    ASSERT_TRUE(unmount_campaign_package_with_error("") == CampaignPackageIoError::None) << "unmount_campaign_package(\"\") should succeed";
}


TEST(IoZipUnzip, io_mount_unmount_campaign_typed_errors)
{
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::EmptyId), static_cast<int>(mount_campaign_package_with_error(""))) << "mount_campaign_package_with_error(\"\") should return EmptyId";
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::MountFailed), static_cast<int>(mount_campaign_package_with_error("definitely.not.a.campaign"))) << "mount invalid campaign should return MountFailed";
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None), static_cast<int>(unmount_campaign_package_with_error(""))) << "unmount_campaign_package_with_error(\"\") should return None";
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::EmptyId), static_cast<int>(remount_campaign_package_with_error())) << "remount with no mounted campaign should return EmptyId";
}


TEST(IoZipUnzip, io_remount_campaign_with_open_physfs_file_is_safe)
{
    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None), static_cast<int>(mount_campaign_package_with_error("org.openglad.gladiator"))) << "mount default campaign should succeed";

    PHYSFS_File* held = PHYSFS_openRead("campaign.yaml");
    ASSERT_TRUE(held != nullptr) << "campaign.yaml should open to hold a live PhysFS handle";
    if (!held)
        return;

    ASSERT_EQ(static_cast<int>(CampaignPackageIoError::None), static_cast<int>(remount_campaign_package_with_error())) << "remount should gracefully no-op when campaign files are still open";

    PHYSFS_close(held);
}


TEST(IoZipUnzip, typed_errors)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_typed_" + std::to_string(::getpid()));
    fs::path indir = base / "typed_in";
    fs::path zipfile = base / "typed_bundle.zip";
    fs::path missing_zip = base / "does_not_exist.zip";

    ASSERT_TRUE(create_dir(indir.string())) << "create_dir typed_in should succeed";
    ASSERT_TRUE(write_file_bytes((indir / "a.txt").string(), "A")) << "write typed a.txt";

    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(zip_contents_with_error(indir.string(), zipfile.string()))) << "zip_contents_with_error should return None on success";
    ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenArchiveFailed), static_cast<int>(unzip_into_with_error(missing_zip.string(), (base / "typed_out").string()))) << "unzip_into_with_error should return OpenArchiveFailed for missing archive";
}


TEST(IoZipUnzip, io_zip_contents_with_error_missing_input_directory_path)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_missing_in_" + std::to_string(::getpid()));
    fs::path missing = base / "no_such_dir";
    fs::path zipfile = base / "empty_from_missing.zip";
    std::filesystem::create_directories(base);

    const ArchiveIoError r = zip_contents_with_error(missing.string(), zipfile.string());
    ASSERT_EQ(ArchiveIoError::None, r)
        << "missing input dir should be treated as an empty input set";
    ASSERT_FALSE(fs::exists(zipfile));
}


TEST(IoZipUnzip, io_zip_contents_with_error_open_archive_failed_path)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_zip_open_fail_" + std::to_string(::getpid()));
    fs::path indir = base / "in";
    fs::path bad_zip = indir; // output points to directory -> zip_open should fail

    ASSERT_TRUE(create_dir(indir.string())) << "create_dir in should succeed";
    ASSERT_TRUE(write_file_bytes((indir / "a.txt").string(), "A")) << "write input file";

    ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenArchiveFailed), static_cast<int>(zip_contents_with_error(indir.string(), bad_zip.string()))) << "zip_contents_with_error should return OpenArchiveFailed when output path is a directory";
}


TEST(IoZipUnzip, io_unzip_with_error_open_output_failed_path)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_unzip_open_output_fail_" + std::to_string(::getpid()));
    fs::path indir = base / "in";
    fs::path zipfile = base / "bundle.zip";
    fs::path out_as_file = base / "out_file";

    ASSERT_TRUE(create_dir(indir.string())) << "create_dir in should succeed";
    ASSERT_TRUE(write_file_bytes((indir / "a.txt").string(), "ABC")) << "write zip input";
    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(zip_contents_with_error(indir.string(), zipfile.string()))) << "zip creation should succeed";

    // out_as_file is a regular file; extracting into "out_as_file/<entry>" should fail fopen.
    ASSERT_TRUE(write_file_bytes(out_as_file.string(), "marker")) << "create output blocker file";
    ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenOutputFailed), static_cast<int>(unzip_into_with_error(zipfile.string(), out_as_file.string()))) << "unzip_into_with_error should report OpenOutputFailed when output path is blocked by a file";

    std::error_code ec;
    fs::remove_all(base, ec);
}


TEST(IoZipUnzip, io_zip_batch5_empty_directory_and_non_regular_entries)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_zip_batch5_" + std::to_string(::getpid()));
    fs::path indir = base / "in";
    fs::path zipfile = base / "bundle.zip";
    fs::path outdir = base / "out";

    ASSERT_TRUE(create_dir(indir.string())) << "create_dir input should succeed";
    ASSERT_TRUE(create_dir((indir / "empty_subdir").string())) << "create_dir empty_subdir should succeed";
    ASSERT_TRUE(write_file_bytes((indir / "root.txt").string(), "ROOT")) << "write regular file should succeed";

#if !defined(_WIN32)
    // Symlink entry exercises non-regular-file skip path in zip enumeration.
    std::error_code ec;
    fs::create_symlink(indir / "root.txt", indir / "root.link", ec);
#endif

    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(zip_contents_with_error(indir.string(), zipfile.string()))) << "zip with empty dir and non-regular entries should still succeed";
    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(unzip_into_with_error(zipfile.string(), outdir.string()))) << "unzip should succeed for archive containing empty directories";

    std::string payload;
    ASSERT_TRUE(read_file_all((outdir / "root.txt").string(), &payload)) << "unzipped root file should exist";
    ASSERT_TRUE(payload == "ROOT") << "unzipped root file content should match";
}


TEST(IoZipUnzip, io_zip_batch6_add_entry_failed_for_unreadable_file)
{
    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_io_zip_batch6_" + std::to_string(::getpid()));
    fs::path indir = base / "in";
    fs::path zipfile = base / "bundle.zip";
    fs::path blocked = indir / "blocked.txt";

    ASSERT_TRUE(create_dir(indir.string())) << "create_dir input should succeed";
    ASSERT_TRUE(write_file_bytes((indir / "ok.txt").string(), "OK")) << "write readable file";
    ASSERT_TRUE(write_file_bytes(blocked.string(), "NOPE")) << "write blocked file";

#if !defined(_WIN32)
    std::error_code ec;
    fs::permissions(blocked, fs::perms::none, fs::perm_options::replace, ec);
    const ArchiveIoError r = zip_contents_with_error(indir.string(), zipfile.string());
    if (::geteuid() == 0) {
        ASSERT_EQ(ArchiveIoError::None, r)
            << "root test runner can reopen chmod(0) files, so the archive should still close cleanly";
        ASSERT_TRUE(fs::exists(zipfile));
    } else {
        ASSERT_EQ(ArchiveIoError::CloseArchiveFailed, r)
            << "non-root runner should fail while finalizing an archive with unreadable input";
    }
    fs::permissions(blocked, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);
#else
    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(zip_contents_with_error(indir.string(), zipfile.string()))) << "windows permission behavior may not reject unreadable entry";
#endif
}


TEST(IoZipUnzip, io_unzip_rejects_zip_slip_paths)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / ("openglad_io_zipslip_" + std::to_string(::getpid()));
    const fs::path zipfile = base / "malicious.zip";
    const fs::path outdir = base / "out";
    const fs::path outside = base / "outside.txt";

    ASSERT_TRUE(create_dir(base.string())) << "create base dir should succeed";

    int err = 0;
    zip* za = zip_open(zipfile.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    ASSERT_TRUE(za != nullptr) << "zip_open malicious archive should succeed";
    if (!za)
        return;

    zip_source* safe_src = zip_source_buffer(za, "SAFE", 4, 0);
    ASSERT_TRUE(safe_src != nullptr) << "zip_source_buffer safe entry should succeed";
    if (safe_src && zip_file_add(za, "safe.txt", safe_src, ZIP_FL_OVERWRITE) < 0)
    {
        zip_source_free(safe_src);
        ASSERT_TRUE(false) << "zip_file_add safe entry should succeed";
    }

    zip_source* slip_src = zip_source_buffer(za, "EVIL", 4, 0);
    ASSERT_TRUE(slip_src != nullptr) << "zip_source_buffer zip slip entry should succeed";
    if (slip_src && zip_file_add(za, "../outside.txt", slip_src, ZIP_FL_OVERWRITE) < 0)
    {
        zip_source_free(slip_src);
        ASSERT_TRUE(false) << "zip_file_add zip slip entry should succeed";
    }

    ASSERT_EQ(0, zip_close(za)) << "zip_close malicious archive should succeed";

    ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenEntryFailed), static_cast<int>(unzip_into_with_error(zipfile.string(), outdir.string()))) << "zip slip entry should be rejected";

    std::string payload;
    ASSERT_TRUE(read_file_all((outdir / "safe.txt").string(), &payload)) << "safe file should extract";
    ASSERT_TRUE(payload == "SAFE") << "safe file content should match";
    ASSERT_TRUE(!fs::exists(outside)) << "zip slip output path outside extraction root must not be created";
}

TEST(IoZipUnzip, io_unzip_rejects_root_only_and_symlink_escape_names)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() /
        ("openglad_io_zip_path_edges_" + std::to_string(::getpid()));
    const fs::path root_zip = base / "root_only.zip";
    const fs::path symlink_zip = base / "symlink_escape.zip";
    const fs::path out = base / "out";
    const fs::path outside = base / "outside";
    ScopedTemporaryTree cleanup(base);
    ASSERT_TRUE(cleanup.ready()) << cleanup.error().message();
    ASSERT_TRUE(fs::create_directories(base));

    ASSERT_TRUE(write_single_entry_zip(root_zip, "/", "IGNORED"));
    EXPECT_EQ(ArchiveIoError::OpenEntryFailed,
              unzip_into_with_error(root_zip.string(), out.string()));
    EXPECT_TRUE(fs::is_empty(out))
        << "a root-only archive name has no safe extraction destination";

#if !defined(_WIN32)
    std::error_code ec;
    ASSERT_TRUE(fs::is_directory(out));
    ASSERT_TRUE(fs::create_directories(outside));
    fs::create_directory_symlink(outside, out / "link", ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(write_single_entry_zip(
        symlink_zip, "link/escaped.txt", "MUST NOT ESCAPE"));

    EXPECT_EQ(ArchiveIoError::OpenEntryFailed,
              unzip_into_with_error(symlink_zip.string(), out.string()));
    EXPECT_FALSE(fs::exists(outside / "escaped.txt"))
        << "canonical destination checks must reject symlink traversal";
#endif
}

TEST(IoZipUnzip, io_unzip_reports_encrypted_and_corrupt_entry_failures)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() /
        ("openglad_io_zip_entry_errors_" + std::to_string(::getpid()));
    const fs::path encrypted_zip = base / "encrypted.zip";
    const fs::path corrupt_zip = base / "corrupt_deflate.zip";
    const fs::path encrypted_out = base / "encrypted_out";
    const fs::path corrupt_out = base / "corrupt_out";
    ScopedTemporaryTree cleanup(base);
    ASSERT_TRUE(cleanup.ready()) << cleanup.error().message();
    ASSERT_TRUE(fs::create_directories(base));

    ASSERT_EQ(1, zip_encryption_method_supported(ZIP_EM_TRAD_PKWARE, 1));
    ASSERT_TRUE(write_special_entry_zip(
        encrypted_zip, "secret.txt", "classified payload",
        "resource-test-password"));
    EXPECT_EQ(ArchiveIoError::OpenEntryFailed,
              unzip_into_with_error(encrypted_zip.string(),
                                    encrypted_out.string()));
    EXPECT_FALSE(fs::exists(encrypted_out / "secret.txt"))
        << "an entry that cannot be opened must not create an output file";

    const std::string payload(16384, 'A');
    ASSERT_TRUE(write_special_entry_zip(
        corrupt_zip, "broken.txt", payload));
    std::vector<std::uint8_t> bytes = read_binary_file(corrupt_zip);
    ASSERT_FALSE(bytes.empty());
    constexpr std::uint32_t kLocalFileHeader = 0x04034b50u;
    const std::size_t local = find_zip_signature(bytes, kLocalFileHeader);
    ASSERT_LT(local + 30u, bytes.size());
    ASSERT_EQ(ZIP_CM_DEFLATE, static_cast<int>(read_le16(bytes, local + 8u)));
    const std::size_t compressed_size = read_le32(bytes, local + 18u);
    const std::size_t data_offset =
        local + 30u + read_le16(bytes, local + 26u) +
        read_le16(bytes, local + 28u);
    ASSERT_GT(compressed_size, 0u);
    ASSERT_LE(data_offset + compressed_size, bytes.size());
    // Raw DEFLATE reserves BTYPE=3 as invalid. Preserve BFINAL while making
    // the first block structurally undecodable rather than merely changing
    // payload bytes and relying on an end-of-stream CRC check.
    bytes[data_offset] =
        static_cast<std::uint8_t>((bytes[data_offset] & 0xf9u) | 0x06u);
    ASSERT_TRUE(write_binary_file(corrupt_zip, bytes));

    EXPECT_EQ(ArchiveIoError::ReadEntryFailed,
              unzip_into_with_error(corrupt_zip.string(),
                                    corrupt_out.string()));
    ASSERT_TRUE(fs::is_regular_file(corrupt_out / "broken.txt"));
    EXPECT_EQ(0u, fs::file_size(corrupt_out / "broken.txt"))
        << "the invalid first block must fail before emitting any bytes";
}

TEST(IoZipUnzip, io_unzip_rejects_declared_entry_size_above_limit)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() /
        ("openglad_io_zip_size_limit_" + std::to_string(::getpid()));
    const fs::path archive = base / "oversized_stat.zip";
    const fs::path out = base / "out";
    ScopedTemporaryTree cleanup(base);
    ASSERT_TRUE(cleanup.ready()) << cleanup.error().message();
    ASSERT_TRUE(fs::create_directories(base));
    ASSERT_TRUE(write_single_entry_zip(archive, "oversized.bin", "x"));

    std::vector<std::uint8_t> bytes = read_binary_file(archive);
    ASSERT_FALSE(bytes.empty());
    constexpr std::uint32_t kCentralFileHeader = 0x02014b50u;
    const std::size_t central = find_zip_signature(bytes, kCentralFileHeader);
    ASSERT_LT(central + 28u, bytes.size());
    write_le32(bytes, central + 24u, (64u * 1024u * 1024u) + 1u);
    ASSERT_TRUE(write_binary_file(archive, bytes));

    EXPECT_EQ(ArchiveIoError::ResourceLimitExceeded,
              unzip_into_with_error(archive.string(), out.string()));
    EXPECT_FALSE(fs::exists(out / "oversized.bin"))
        << "the declared size limit is enforced before opening output";
}

TEST(IoZipUnzip, io_zip_skips_an_existing_output_archive_inside_input)
{
    namespace fs = std::filesystem;
    const fs::path base =
        fs::temp_directory_path() /
        ("openglad_io_self_archive_" + std::to_string(::getpid()));
    const fs::path input = base / "input";
    const fs::path archive = input / "bundle.zip";
    const fs::path output = base / "output";
    ScopedTemporaryTree cleanup(base);
    ASSERT_TRUE(cleanup.ready()) << cleanup.error().message();

    ASSERT_TRUE(create_dir(input.string()));
    ASSERT_TRUE(write_file_bytes((input / "payload.txt").string(), "PAYLOAD"));
    ASSERT_EQ(ArchiveIoError::None,
              zip_contents_with_error(input.string(), archive.string()));
    ASSERT_TRUE(fs::is_regular_file(archive));

    // On the second write the archive is already one of the recursively
    // enumerated input files. It must be recognized as the output itself and
    // omitted, rather than recursively embedding its previous bytes.
    ASSERT_EQ(ArchiveIoError::None,
              zip_contents_with_error(input.string(), archive.string()));
    ASSERT_EQ(ArchiveIoError::None,
              unzip_into_with_error(archive.string(), output.string()));

    std::string payload;
    ASSERT_TRUE(read_file_all((output / "payload.txt").string(), &payload));
    EXPECT_EQ("PAYLOAD", payload);
    EXPECT_FALSE(fs::exists(output / "bundle.zip"));

}

TEST(IoZipUnzip, io_unzip_normalizes_backslashes)
{
    namespace fs = std::filesystem;
    const fs::path base =
        fs::temp_directory_path() /
        ("openglad_io_normalized_names_" + std::to_string(::getpid()));
    const fs::path zipfile = base / "names.zip";
    const fs::path outdir = base / "out";
    ScopedTemporaryTree cleanup(base);
    ASSERT_TRUE(cleanup.ready()) << cleanup.error().message();
    ASSERT_TRUE(create_dir(base.string()));

    ASSERT_TRUE(write_single_entry_zip(
        zipfile, "nested\\payload.txt", "NORMALIZED"));
    EXPECT_EQ(ArchiveIoError::None,
              unzip_into_with_error(zipfile.string(), outdir.string()));
    std::string payload;
    ASSERT_TRUE(
        read_file_all((outdir / "nested" / "payload.txt").string(), &payload));
    EXPECT_EQ("NORMALIZED", payload);
}

TEST(IoZipUnzip, io_unzip_rejects_absolute_names_without_writing_outside)
{
    namespace fs = std::filesystem;
    const fs::path base =
        fs::temp_directory_path() /
        ("openglad_io_absolute_name_" + std::to_string(::getpid()));
    const fs::path zipfile = base / "absolute.zip";
    const fs::path outdir = base / "out";
    const fs::path outside = base / "outside.txt";
    ScopedTemporaryTree cleanup(base);
    ASSERT_TRUE(cleanup.ready()) << cleanup.error().message();
    ASSERT_TRUE(create_dir(base.string()));

    ASSERT_TRUE(write_single_entry_zip(
        zipfile, outside.generic_string(), "REJECTED"));
    EXPECT_EQ(ArchiveIoError::OpenEntryFailed,
              unzip_into_with_error(zipfile.string(), outdir.string()));
    EXPECT_FALSE(fs::exists(outside))
        << "an absolute archive name cannot write outside the extraction root";
}

TEST(IoZipUnzip, io_unzip_rejects_archives_exceeding_entry_limit)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / ("openglad_io_zip_limit_" + std::to_string(::getpid()));
    const fs::path zipfile = base / "too_many_entries.zip";
    const fs::path outdir = base / "out";

    ASSERT_TRUE(create_dir(base.string())) << "create base dir should succeed";

    int err = 0;
    zip* za = zip_open(zipfile.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    ASSERT_TRUE(za != nullptr) << "zip_open large-entry archive should succeed";
    if (!za)
        return;

    // zip_source_buffer does not copy: it keeps the pointer and libzip reads it
    // during zip_close() below. The buffer must therefore outlive zip_close(), so
    // use one function-scoped buffer rather than a per-iteration stack array
    // (which would be a stack-use-after-scope read at flush time).
    static const char payload[] = "x";
    for (int i = 0; i < 4097; ++i)
    {
        zip_source* src = zip_source_buffer(za, payload, 1, 0);
        ASSERT_TRUE(src != nullptr) << "zip_source_buffer entry should succeed";
        char name[64] = {};
        std::snprintf(name, sizeof(name), "entry_%04d.txt", i);
        if (src && zip_file_add(za, name, src, ZIP_FL_OVERWRITE) < 0)
        {
            zip_source_free(src);
            ASSERT_TRUE(false) << "zip_file_add entry should succeed";
        }
    }

    ASSERT_EQ(0, zip_close(za)) << "zip_close large-entry archive should succeed";

    ASSERT_EQ(
        static_cast<int>(ArchiveIoError::ResourceLimitExceeded),
        static_cast<int>(unzip_into_with_error(zipfile.string(), outdir.string())))
        << "archives above the extraction entry cap should be rejected";

    std::error_code ec;
    fs::remove_all(base, ec);
}


TEST(IoZipUnzip, io_unzip_reports_output_write_failure)
{
#if defined(__linux__)
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / ("openglad_io_write_fail_" + std::to_string(::getpid()));
    const fs::path indir = base / "in";
    const fs::path zipfile = base / "bundle.zip";
    const fs::path dev_full = "/dev/full";

    if (!fs::exists(dev_full) || !fs::is_character_file(dev_full))
        return;

    FILE* full = std::fopen(dev_full.string().c_str(), "wb");
    if (!full)
        return;
    const bool dev_full_writes_fail = (std::fwrite("x", 1, 1, full) == 0) || (std::fclose(full) != 0);
    if (!dev_full_writes_fail)
        return;

    ASSERT_TRUE(create_dir(indir.string())) << "create_dir input should succeed";
    ASSERT_TRUE(write_file_bytes((indir / "full").string(), "payload")) << "write file that maps to /dev/full";
    ASSERT_EQ(static_cast<int>(ArchiveIoError::None), static_cast<int>(zip_contents_with_error(indir.string(), zipfile.string()))) << "zip creation should succeed";

    ASSERT_EQ(static_cast<int>(ArchiveIoError::OpenOutputFailed), static_cast<int>(unzip_into_with_error(zipfile.string(), "/dev"))) << "unzip_into_with_error should report output write/close failure";

    std::error_code ec;
    fs::remove_all(base, ec);
#endif
}
