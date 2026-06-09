#include <openglad/resources/io.h>
#include <gtest/gtest.h>
#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
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

    SDL_RWops* rw = open_read_file(fname.c_str(), true);
    ASSERT_TRUE(rw != nullptr) << "open_read_file should open cwd file";
    char buf[8] = {0};
    size_t got = SDL_RWread(rw, buf, 1, 3);
    SDL_RWclose(rw);
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
