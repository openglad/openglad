#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

#include <physfs.h>

#include "test_framework.h"

namespace
{
struct StringCollector
{
    std::vector<std::string> values;
};

void collect_string(void* data, const char* str)
{
    StringCollector* c = static_cast<StringCollector*>(data);
    if (c && str)
        c->values.emplace_back(str);
}

struct EnumCollector
{
    std::vector<std::string> names;
};

void collect_enum(void* data, const char* /*origdir*/, const char* fname)
{
    EnumCollector* c = static_cast<EnumCollector*>(data);
    if (c && fname)
        c->names.emplace_back(fname);
}
} // namespace

static void run_physfs_searchpath_callbacks_and_mount_edges()
{
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be initialized");

    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_physfs_api_" + std::to_string(::getpid()));
    fs::create_directories(base);

    FILE* f = fopen((base / "root.txt").string().c_str(), "wb");
    TEST_ASSERT(f != nullptr, "create root.txt");
    fputs("root-data", f);
    fclose(f);

    TEST_ASSERT(PHYSFS_mount(base.string().c_str(), "/edge", 1), "mount to /edge should succeed");
    TEST_ASSERT(PHYSFS_exists("edge/root.txt"), "mounted file should exist");
    TEST_ASSERT(PHYSFS_getRealDir("edge/root.txt") != nullptr, "getRealDir should find mounted file");

    // Exercise callback-style APIs in addition to array-returning versions.
    StringCollector search_cb;
    PHYSFS_getSearchPathCallback(collect_string, &search_cb);
    TEST_ASSERT(!search_cb.values.empty(), "getSearchPathCallback should enumerate at least one path");

    char** paths = PHYSFS_getSearchPath();
    TEST_ASSERT(paths != nullptr, "getSearchPath should return a list");
    PHYSFS_freeList(paths);

    EnumCollector ec;
    PHYSFS_enumerateFilesCallback("edge", collect_enum, &ec);
    TEST_ASSERT(!ec.names.empty(), "enumerateFilesCallback should list files");

    TEST_ASSERT(PHYSFS_removeFromSearchPath(base.string().c_str()), "removeFromSearchPath should succeed");
    TEST_ASSERT(!PHYSFS_removeFromSearchPath(base.string().c_str()),
                "removing same path twice should fail");

    TEST_ASSERT(PHYSFS_addToSearchPath(base.string().c_str(), 1), "addToSearchPath should succeed");
    TEST_ASSERT(PHYSFS_removeFromSearchPath(base.string().c_str()), "removeFromSearchPath after add should succeed");
}
static void run_physfs_file_seek_tell_flush_append_and_delete_edges()
{
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be initialized");

    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_physfs_fileapi_" + std::to_string(::getpid()));
    fs::create_directories(base);

    const char* old_write = PHYSFS_getWriteDir();
    std::string old_write_s = old_write ? old_write : "";

    TEST_ASSERT(PHYSFS_setWriteDir(base.string().c_str()), "setWriteDir should succeed");
    TEST_ASSERT(PHYSFS_mount(base.string().c_str(), nullptr, 0), "mount write dir should succeed");

    PHYSFS_File* wf = PHYSFS_openWrite("io.bin");
    TEST_ASSERT(wf != nullptr, "openWrite should succeed");
    TEST_ASSERT(PHYSFS_setBuffer(wf, 256), "setBuffer(write) should succeed");
    const char* first = "abc";
    TEST_ASSERT(PHYSFS_write(wf, first, 1, 3) == 3, "write first chunk");
    TEST_ASSERT(PHYSFS_tell(wf) >= 3, "tell(write) should advance");
    TEST_ASSERT(PHYSFS_flush(wf), "flush(write) should succeed");
    TEST_ASSERT(PHYSFS_close(wf), "close(write) should succeed");

    PHYSFS_File* af = PHYSFS_openAppend("io.bin");
    TEST_ASSERT(af != nullptr, "openAppend should succeed");
    const char* second = "defg";
    TEST_ASSERT(PHYSFS_write(af, second, 1, 4) == 4, "append chunk");
    TEST_ASSERT(PHYSFS_close(af), "close(append) should succeed");

    PHYSFS_File* rf = PHYSFS_openRead("io.bin");
    TEST_ASSERT(rf != nullptr, "openRead should succeed");
    TEST_ASSERT(PHYSFS_setBuffer(rf, 128), "setBuffer(read) should succeed");
    TEST_ASSERT(PHYSFS_fileLength(rf) == 7, "fileLength should match write+append");

    char buf[8] = {};
    TEST_ASSERT(PHYSFS_read(rf, buf, 1, 3) == 3, "read first part");
    TEST_ASSERT(PHYSFS_tell(rf) == 3, "tell(read) should match consumed bytes");
    TEST_ASSERT(!PHYSFS_eof(rf), "not at eof after partial read");
    TEST_ASSERT(PHYSFS_seek(rf, 0), "seek to start should succeed");
    TEST_ASSERT(PHYSFS_read(rf, buf, 1, 7) == 7, "read full file");
    TEST_ASSERT(PHYSFS_eof(rf), "eof should be true after full read");
    TEST_ASSERT(PHYSFS_close(rf), "close(read) should succeed");

    TEST_ASSERT(PHYSFS_delete("io.bin"), "delete existing file should succeed");
    TEST_ASSERT(!PHYSFS_delete("io.bin"), "delete missing file should fail");

    (void)PHYSFS_removeFromSearchPath(base.string().c_str());
    if (!old_write_s.empty())
        (void)PHYSFS_setWriteDir(old_write_s.c_str());
}
static void run_physfs_symbolic_link_toggle_and_error_string_path()
{
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be initialized");

    PHYSFS_permitSymbolicLinks(1);
    TEST_ASSERT(PHYSFS_symbolicLinksPermitted(), "symbolic links should be enabled");
    PHYSFS_permitSymbolicLinks(0);
    TEST_ASSERT(!PHYSFS_symbolicLinksPermitted(), "symbolic links should be disabled");

    PHYSFS_File* missing = PHYSFS_openRead("definitely_missing_file_for_error_path.bin");
    TEST_ASSERT(missing == nullptr, "openRead on missing file should fail");

    const char* msg = PHYSFS_getLastError();
    TEST_ASSERT(msg != nullptr && msg[0] != '\0', "missing file should set a non-empty error string");
}

static void run_physfs_global_api_and_reinit_edges()
{
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be initialized");

    const PHYSFS_ArchiveInfo** types = PHYSFS_supportedArchiveTypes();
    TEST_ASSERT(types != nullptr, "supportedArchiveTypes should return a table");
    int type_count = 0;
    for (const PHYSFS_ArchiveInfo** i = types; i && *i; ++i)
        type_count++;
    TEST_ASSERT(type_count > 0, "supportedArchiveTypes should expose at least one type");

    PHYSFS_Version linked{};
    PHYSFS_getLinkedVersion(&linked);
    TEST_ASSERT(linked.major >= 1, "linked major version should be valid");

    const char* base = PHYSFS_getBaseDir();
    const char* user = PHYSFS_getUserDir();
    const char* sep = PHYSFS_getDirSeparator();
    TEST_ASSERT(base != nullptr && base[0] != '\0', "getBaseDir should be non-empty");
    TEST_ASSERT(user != nullptr && user[0] != '\0', "getUserDir should be non-empty");
    TEST_ASSERT(sep != nullptr && sep[0] != '\0', "getDirSeparator should be non-empty");

    StringCollector cds_cb;
    PHYSFS_getCdRomDirsCallback(collect_string, &cds_cb);
    char** cds = PHYSFS_getCdRomDirs();
    TEST_ASSERT(cds != nullptr, "getCdRomDirs should return a list");
    PHYSFS_freeList(cds);

    namespace fs = std::filesystem;
    fs::path base_dir = fs::temp_directory_path() / ("openglad_physfs_global_" + std::to_string(::getpid()));
    fs::create_directories(base_dir);
    TEST_ASSERT(PHYSFS_mount(base_dir.string().c_str(), "/global", 1), "mount for getMountPoint should succeed");
    const char* mp = PHYSFS_getMountPoint(base_dir.string().c_str());
    TEST_ASSERT(mp != nullptr, "getMountPoint should find mounted dir");
    TEST_ASSERT(PHYSFS_removeFromSearchPath(base_dir.string().c_str()), "remove mounted dir should succeed");

    // Exercise setSaneConfig path (may fail depending environment; still valuable for coverage).
    (void)PHYSFS_setSaneConfig("openglad", "edgecfg", nullptr, 0, 0);

    TEST_ASSERT(PHYSFS_deinit(), "PHYSFS_deinit should succeed");
    TEST_ASSERT(!PHYSFS_isInit(), "PHYSFS should report deinitialized state");
    TEST_ASSERT(PHYSFS_init("openglad_test"), "PHYSFS_init after deinit should succeed");
    TEST_ASSERT(PHYSFS_isInit(), "PHYSFS should be reinitialized");
}

void test_external_physfs_api_edges()
{
    run_physfs_searchpath_callbacks_and_mount_edges();
    run_physfs_file_seek_tell_flush_append_and_delete_edges();
    run_physfs_symbolic_link_toggle_and_error_string_path();
    run_physfs_global_api_and_reinit_edges();
}
REGISTER_TEST(test_external_physfs_api_edges);
