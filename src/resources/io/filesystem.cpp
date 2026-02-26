#include <openglad/resources/filesystem.h>

#include "physfs.h"

#include <memory>

namespace og::resources {

namespace {
struct PhysfsFileListDeleter {
    void operator()(char** list) const
    {
        if (list)
            PHYSFS_freeList(list);
    }
};

struct PhysfsFileDeleter {
    void operator()(PHYSFS_File* file) const
    {
        if (file)
            PHYSFS_close(file);
    }
};
} // namespace

std::string last_error()
{
    const PHYSFS_ErrorCode code = PHYSFS_getLastErrorCode();
    const char* msg = PHYSFS_getErrorByCode(code);
    return msg ? std::string(msg) : std::string();
}

bool init(const char* argv0)
{
    return PHYSFS_init(argv0) != 0;
}

bool deinit()
{
    return PHYSFS_deinit() != 0;
}

bool set_write_dir(const std::string& path)
{
    return PHYSFS_setWriteDir(path.c_str()) != 0;
}

bool mount(const std::string& archive, const char* mountpoint, int append_to_path)
{
    return PHYSFS_mount(archive.c_str(), mountpoint, append_to_path) != 0;
}

bool unmount(const std::string& archive)
{
    return PHYSFS_unmount(archive.c_str()) != 0;
}

bool exists(const char* path)
{
    return path && PHYSFS_exists(path) != 0;
}

std::vector<std::uint8_t> read_file(const char* path)
{
    std::vector<std::uint8_t> out;
    if (!path)
        return out;

    std::unique_ptr<PHYSFS_File, PhysfsFileDeleter> file(PHYSFS_openRead(path));
    if (!file)
        return out;

    const PHYSFS_sint64 len = PHYSFS_fileLength(file.get());
    if (len <= 0)
        return out;

    out.resize(static_cast<std::size_t>(len));
    const PHYSFS_sint64 got = PHYSFS_readBytes(file.get(), out.data(), len);
    if (got != len)
        out.clear();
    return out;
}

bool write_file(const char* path, const void* data, std::size_t len)
{
    if (!path || (len > 0 && data == nullptr))
        return false;

    PHYSFS_File* f = PHYSFS_openWrite(path);
    if (!f)
        return false;

    const PHYSFS_sint64 wrote = PHYSFS_writeBytes(f, data, static_cast<PHYSFS_sint64>(len));
    const bool close_ok = PHYSFS_close(f) != 0;
    return wrote == static_cast<PHYSFS_sint64>(len) && close_ok;
}

std::list<std::string> enumerate_files_sorted(const std::string& dirname)
{
    std::list<std::string> out;
    std::unique_ptr<char*, PhysfsFileListDeleter> files(PHYSFS_enumerateFiles(dirname.c_str()));
    for (char** p = files.get(); p != nullptr && *p != nullptr; ++p)
        out.push_back(*p);
    out.sort();
    return out;
}

} // namespace og::resources
