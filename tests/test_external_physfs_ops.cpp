#include <filesystem>
#include <string>

#include <unistd.h>

#include <physfs.h>

#include "test_framework.h"

void test_external_physfs_write_read_stat_and_delete()
{
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be initialized");

    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_physfs_" + std::to_string(::getpid()));
    fs::create_directories(base);

    const char* old_write = PHYSFS_getWriteDir();
    std::string old_write_s = old_write ? old_write : "";

    TEST_ASSERT(PHYSFS_setWriteDir(base.string().c_str()), "PHYSFS_setWriteDir should succeed");
    // Make the write dir visible to read/enumeration APIs.
    TEST_ASSERT(PHYSFS_mount(base.string().c_str(), nullptr, 0), "PHYSFS_mount(write dir) should succeed");

    // Create a directory and write a file.
    TEST_ASSERT(PHYSFS_mkdir("subdir"), "PHYSFS_mkdir should succeed");

    PHYSFS_File* wf = PHYSFS_openWrite("subdir/hello.txt");
    TEST_ASSERT(wf != nullptr, "PHYSFS_openWrite should succeed");
    const char* msg = "hello physfs\n";
    TEST_ASSERT(PHYSFS_write(wf, msg, 1, 12) == 12, "PHYSFS_write should write all bytes");
    TEST_ASSERT(PHYSFS_close(wf), "PHYSFS_close(write) should succeed");

    TEST_ASSERT(PHYSFS_exists("subdir/hello.txt"), "PHYSFS_exists should report file");
    TEST_ASSERT(PHYSFS_isDirectory("subdir"), "PHYSFS_isDirectory should report dir");

    // Read back.
    PHYSFS_File* rf = PHYSFS_openRead("subdir/hello.txt");
    TEST_ASSERT(rf != nullptr, "PHYSFS_openRead should succeed");
    const PHYSFS_sint64 len = PHYSFS_fileLength(rf);
    TEST_ASSERT(len >= 12, "PHYSFS_fileLength should report file length");
    char buf[32]{};
    PHYSFS_sint64 got = PHYSFS_read(rf, buf, 1, sizeof(buf));
    TEST_ASSERT(got > 0, "PHYSFS_read should read some bytes");
    (void)PHYSFS_close(rf);

    // Enumerate and free list (touch list alloc paths).
    char** files = PHYSFS_enumerateFiles("subdir");
    TEST_ASSERT(files != nullptr, "PHYSFS_enumerateFiles should succeed");
    PHYSFS_freeList(files);

    // Delete and confirm.
    TEST_ASSERT(PHYSFS_delete("subdir/hello.txt"), "PHYSFS_delete should succeed");
    TEST_ASSERT(!PHYSFS_exists("subdir/hello.txt"), "file should no longer exist");

    (void)PHYSFS_unmount(base.string().c_str());

    // Restore previous write dir (best-effort).
    if (!old_write_s.empty())
        (void)PHYSFS_setWriteDir(old_write_s.c_str());
}
REGISTER_TEST(test_external_physfs_write_read_stat_and_delete);
