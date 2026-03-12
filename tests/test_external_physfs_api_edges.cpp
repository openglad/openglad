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
    ASSERT_TRUE(PHYSFS_isInit()) << "PHYSFS should be initialized";

    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_physfs_api_" + std::to_string(::getpid()));
    fs::create_directories(base);

    FILE* f = fopen((base / "root.txt").string().c_str(), "wb");
    ASSERT_TRUE(f != nullptr) << "create root.txt";
    fputs("root-data", f);
    fclose(f);

    ASSERT_TRUE(PHYSFS_mount(base.string().c_str(), "/edge", 1)) << "mount to /edge should succeed";
    ASSERT_TRUE(PHYSFS_exists("edge/root.txt")) << "mounted file should exist";
    ASSERT_TRUE(PHYSFS_getRealDir("edge/root.txt") != nullptr) << "getRealDir should find mounted file";

    // Exercise callback-style APIs in addition to array-returning versions.
    StringCollector search_cb;
    PHYSFS_getSearchPathCallback(collect_string, &search_cb);
    ASSERT_TRUE(!search_cb.values.empty()) << "getSearchPathCallback should enumerate at least one path";

    char** paths = PHYSFS_getSearchPath();
    ASSERT_TRUE(paths != nullptr) << "getSearchPath should return a list";
    PHYSFS_freeList(paths);

    EnumCollector ec;
    PHYSFS_enumerateFilesCallback("edge", collect_enum, &ec);
    ASSERT_TRUE(!ec.names.empty()) << "enumerateFilesCallback should list files";

    ASSERT_TRUE(PHYSFS_unmount(base.string().c_str())) << "unmount should succeed";
    ASSERT_TRUE(!PHYSFS_unmount(base.string().c_str())) << "removing same path twice should fail";

    ASSERT_TRUE(PHYSFS_addToSearchPath(base.string().c_str(), 1)) << "addToSearchPath should succeed";
    ASSERT_TRUE(PHYSFS_unmount(base.string().c_str())) << "unmount after add should succeed";
}
static void run_physfs_file_seek_tell_flush_append_and_delete_edges()
{
    ASSERT_TRUE(PHYSFS_isInit()) << "PHYSFS should be initialized";

    namespace fs = std::filesystem;
    fs::path base = fs::temp_directory_path() / ("openglad_physfs_fileapi_" + std::to_string(::getpid()));
    fs::create_directories(base);

    const char* old_write = PHYSFS_getWriteDir();
    std::string old_write_s = old_write ? old_write : "";

    ASSERT_TRUE(PHYSFS_setWriteDir(base.string().c_str())) << "setWriteDir should succeed";
    ASSERT_TRUE(PHYSFS_mount(base.string().c_str(), nullptr, 0)) << "mount write dir should succeed";

    PHYSFS_File* wf = PHYSFS_openWrite("io.bin");
    ASSERT_TRUE(wf != nullptr) << "openWrite should succeed";
    ASSERT_TRUE(PHYSFS_setBuffer(wf, 256)) << "setBuffer(write) should succeed";
    const char* first = "abc";
    ASSERT_TRUE(PHYSFS_write(wf, first, 1, 3) == 3) << "write first chunk";
    ASSERT_TRUE(PHYSFS_tell(wf) >= 3) << "tell(write) should advance";
    ASSERT_TRUE(PHYSFS_flush(wf)) << "flush(write) should succeed";
    ASSERT_TRUE(PHYSFS_close(wf)) << "close(write) should succeed";

    PHYSFS_File* af = PHYSFS_openAppend("io.bin");
    ASSERT_TRUE(af != nullptr) << "openAppend should succeed";
    const char* second = "defg";
    ASSERT_TRUE(PHYSFS_write(af, second, 1, 4) == 4) << "append chunk";
    ASSERT_TRUE(PHYSFS_close(af)) << "close(append) should succeed";

    PHYSFS_File* rf = PHYSFS_openRead("io.bin");
    ASSERT_TRUE(rf != nullptr) << "openRead should succeed";
    ASSERT_TRUE(PHYSFS_setBuffer(rf, 128)) << "setBuffer(read) should succeed";
    ASSERT_TRUE(PHYSFS_fileLength(rf) == 7) << "fileLength should match write+append";

    char buf[8] = {};
    ASSERT_TRUE(PHYSFS_read(rf, buf, 1, 3) == 3) << "read first part";
    ASSERT_TRUE(PHYSFS_tell(rf) == 3) << "tell(read) should match consumed bytes";
    ASSERT_TRUE(!PHYSFS_eof(rf)) << "not at eof after partial read";
    ASSERT_TRUE(PHYSFS_seek(rf, 0)) << "seek to start should succeed";
    ASSERT_TRUE(PHYSFS_read(rf, buf, 1, 7) == 7) << "read full file";
    ASSERT_TRUE(PHYSFS_eof(rf)) << "eof should be true after full read";
    ASSERT_TRUE(PHYSFS_close(rf)) << "close(read) should succeed";

    ASSERT_TRUE(PHYSFS_delete("io.bin")) << "delete existing file should succeed";
    ASSERT_TRUE(!PHYSFS_delete("io.bin")) << "delete missing file should fail";

    (void)PHYSFS_unmount(base.string().c_str());
    if (!old_write_s.empty())
        (void)PHYSFS_setWriteDir(old_write_s.c_str());
}
static void run_physfs_symbolic_link_toggle_and_error_string_path()
{
    ASSERT_TRUE(PHYSFS_isInit()) << "PHYSFS should be initialized";

    PHYSFS_permitSymbolicLinks(1);
    ASSERT_TRUE(PHYSFS_symbolicLinksPermitted()) << "symbolic links should be enabled";
    PHYSFS_permitSymbolicLinks(0);
    ASSERT_TRUE(!PHYSFS_symbolicLinksPermitted()) << "symbolic links should be disabled";

    PHYSFS_File* missing = PHYSFS_openRead("definitely_missing_file_for_error_path.bin");
    ASSERT_TRUE(missing == nullptr) << "openRead on missing file should fail";

    const char* msg = PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
    ASSERT_TRUE(msg != nullptr && msg[0] != '\0') << "missing file should set a non-empty error string";
}

static void run_physfs_global_api_and_reinit_edges()
{
    ASSERT_TRUE(PHYSFS_isInit()) << "PHYSFS should be initialized";

    const PHYSFS_ArchiveInfo** types = PHYSFS_supportedArchiveTypes();
    ASSERT_TRUE(types != nullptr) << "supportedArchiveTypes should return a table";
    int type_count = 0;
    for (const PHYSFS_ArchiveInfo** i = types; i && *i; ++i)
        type_count++;
    ASSERT_TRUE(type_count > 0) << "supportedArchiveTypes should expose at least one type";

    PHYSFS_Version linked{};
    PHYSFS_getLinkedVersion(&linked);
    ASSERT_TRUE(linked.major >= 1) << "linked major version should be valid";

    const char* base = PHYSFS_getBaseDir();
    const char* user = PHYSFS_getUserDir();
    const char* sep = PHYSFS_getDirSeparator();
    ASSERT_TRUE(base != nullptr && base[0] != '\0') << "getBaseDir should be non-empty";
    ASSERT_TRUE(user != nullptr && user[0] != '\0') << "getUserDir should be non-empty";
    ASSERT_TRUE(sep != nullptr && sep[0] != '\0') << "getDirSeparator should be non-empty";

    StringCollector cds_cb;
    PHYSFS_getCdRomDirsCallback(collect_string, &cds_cb);
    char** cds = PHYSFS_getCdRomDirs();
    ASSERT_TRUE(cds != nullptr) << "getCdRomDirs should return a list";
    PHYSFS_freeList(cds);

    namespace fs = std::filesystem;
    fs::path base_dir = fs::temp_directory_path() / ("openglad_physfs_global_" + std::to_string(::getpid()));
    fs::create_directories(base_dir);
    ASSERT_TRUE(PHYSFS_mount(base_dir.string().c_str(), "/global", 1)) << "mount for getMountPoint should succeed";
    const char* mp = PHYSFS_getMountPoint(base_dir.string().c_str());
    ASSERT_TRUE(mp != nullptr) << "getMountPoint should find mounted dir";
    ASSERT_TRUE(PHYSFS_unmount(base_dir.string().c_str())) << "unmount dir should succeed";

    // Exercise setSaneConfig path (may fail depending environment; still valuable for coverage).
    (void)PHYSFS_setSaneConfig("openglad", "edgecfg", nullptr, 0, 0);

    ASSERT_TRUE(PHYSFS_deinit()) << "PHYSFS_deinit should succeed";
    ASSERT_TRUE(!PHYSFS_isInit()) << "PHYSFS should report deinitialized state";
    ASSERT_TRUE(PHYSFS_init("openglad_test")) << "PHYSFS_init after deinit should succeed";
    ASSERT_TRUE(PHYSFS_isInit()) << "PHYSFS should be reinitialized";
}

TEST(ExternalPhysfsApiEdges, external_physfs_api_edges)
{
    run_physfs_searchpath_callbacks_and_mount_edges();
    run_physfs_file_seek_tell_flush_append_and_delete_edges();
    run_physfs_symbolic_link_toggle_and_error_string_path();
    run_physfs_global_api_and_reinit_edges();
}

