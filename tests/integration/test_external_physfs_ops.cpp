#include <filesystem>
#include <string>

#include <unistd.h>

#include <physfs.h>

#include <gtest/gtest.h>

TEST(ExternalPhysfsOps, external_physfs_write_read_stat_and_delete)
{
    ASSERT_TRUE(PHYSFS_isInit()) << "PHYSFS should be initialized";

    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_physfs_" + std::to_string(::getpid()));
    fs::create_directories(base);

    const char* old_write = PHYSFS_getWriteDir();
    std::string old_write_s = old_write ? old_write : "";

    ASSERT_TRUE(PHYSFS_setWriteDir(base.string().c_str())) << "PHYSFS_setWriteDir should succeed";
    // Make the write dir visible to read/enumeration APIs.
    ASSERT_TRUE(PHYSFS_mount(base.string().c_str(), nullptr, 0)) << "PHYSFS_mount(write dir) should succeed";

    // Create a directory and write a file.
    ASSERT_TRUE(PHYSFS_mkdir("subdir")) << "PHYSFS_mkdir should succeed";

    PHYSFS_File* wf = PHYSFS_openWrite("subdir/hello.txt");
    ASSERT_TRUE(wf != nullptr) << "PHYSFS_openWrite should succeed";
    const char* msg = "hello physfs\n";
    constexpr PHYSFS_uint64 msg_len = 12;
    ASSERT_TRUE(PHYSFS_writeBytes(wf, msg, msg_len) == static_cast<PHYSFS_sint64>(msg_len))
        << "PHYSFS_writeBytes should write all bytes";
    ASSERT_TRUE(PHYSFS_close(wf)) << "PHYSFS_close(write) should succeed";

    ASSERT_TRUE(PHYSFS_exists("subdir/hello.txt")) << "PHYSFS_exists should report file";
    PHYSFS_Stat stat{};
    ASSERT_TRUE(PHYSFS_stat("subdir", &stat)) << "PHYSFS_stat should describe the directory";
    ASSERT_EQ(PHYSFS_FILETYPE_DIRECTORY, stat.filetype) << "PHYSFS_stat should report a directory";

    // Read back.
    PHYSFS_File* rf = PHYSFS_openRead("subdir/hello.txt");
    ASSERT_TRUE(rf != nullptr) << "PHYSFS_openRead should succeed";
    const PHYSFS_sint64 len = PHYSFS_fileLength(rf);
    ASSERT_TRUE(len >= 12) << "PHYSFS_fileLength should report file length";
    char buf[32]{};
    PHYSFS_sint64 got = PHYSFS_readBytes(rf, buf, sizeof(buf));
    ASSERT_TRUE(got > 0) << "PHYSFS_read should read some bytes";
    (void)PHYSFS_close(rf);

    // Enumerate and free list (touch list alloc paths).
    char** files = PHYSFS_enumerateFiles("subdir");
    ASSERT_TRUE(files != nullptr) << "PHYSFS_enumerateFiles should succeed";
    PHYSFS_freeList(files);

    // Delete and confirm.
    ASSERT_TRUE(PHYSFS_delete("subdir/hello.txt")) << "PHYSFS_delete should succeed";
    ASSERT_TRUE(!PHYSFS_exists("subdir/hello.txt")) << "file should no longer exist";

    (void)PHYSFS_unmount(base.string().c_str());

    // Restore previous write dir (best-effort).
    if (!old_write_s.empty())
        (void)PHYSFS_setWriteDir(old_write_s.c_str());
}
