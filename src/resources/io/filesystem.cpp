#include <openglad/resources/filesystem.h>
#include <openglad/resources/og_file.h>

#include "physfs.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>

namespace {
struct PhysfsFileListDeleter {
    void operator()(char** list) const
    {
        if (list != nullptr)
            PHYSFS_freeList(list);
    }
};
} // namespace

namespace og::resources {

bool init(const char* argv0)
{
    return PHYSFS_init(argv0) != 0;
}

bool deinit()
{
    return PHYSFS_deinit() != 0;
}

bool is_initialized()
{
    return PHYSFS_isInit() != 0;
}

bool set_write_dir(const std::string& path)
{
    return PHYSFS_setWriteDir(path.c_str()) != 0;
}

bool mount(const char* archive, const char* mountpoint, int append_to_path)
{
    if (archive == nullptr || archive[0] == '\0')
        return false;
    return PHYSFS_mount(archive, mountpoint, append_to_path) != 0;
}

bool unmount(const char* archive)
{
    if (archive == nullptr || archive[0] == '\0')
        return false;
    return PHYSFS_unmount(archive) != 0;
}

std::list<std::string> list_files(const char* dirname)
{
    std::list<std::string> files;
    if (dirname == nullptr)
        return files;

    std::unique_ptr<char*, PhysfsFileListDeleter> list(
        PHYSFS_enumerateFiles(dirname));
    if (!list)
        return files;

    for (char** e = list.get(); *e != nullptr; ++e)
        files.emplace_back(*e);

    files.sort();
    return files;
}

std::vector<std::uint8_t> read_file(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return {};

    og::io::OgFilePtr file = og::io::og_open_read(path, false);
    if (!file)
        return {};

    std::vector<std::uint8_t> bytes;
    std::array<std::uint8_t, 4096> chunk{};
    while (true)
    {
        const std::size_t got = file->read(chunk.data(), 1, chunk.size());
        if (got == 0)
            break;
        bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + got);
        if (got < chunk.size())
        {
            break;
        }
    }
    return bytes;
}

bool write_file(const char* path, const void* data, std::size_t len)
{
    if (path == nullptr || path[0] == '\0')
        return false;
    if (data == nullptr && len > 0)
        return false;
    if (len >
        static_cast<std::size_t>(std::numeric_limits<PHYSFS_sint64>::max()))
    {
        return false;
    }

    PHYSFS_File* file = PHYSFS_openWrite(path);
    if (file == nullptr)
        return false;

    bool write_ok = true;
    if (len > 0)
    {
        const PHYSFS_sint64 wrote =
            PHYSFS_writeBytes(file, data, static_cast<PHYSFS_uint64>(len));
        write_ok = wrote == static_cast<PHYSFS_sint64>(len);
    }

    const bool close_ok = PHYSFS_close(file) != 0;
    return write_ok && close_ok;
}

bool exists(const char* path)
{
    if (path == nullptr || path[0] == '\0')
        return false;
    return PHYSFS_exists(path) != 0;
}

std::string filesystem_last_error()
{
    const PHYSFS_ErrorCode code = PHYSFS_getLastErrorCode();
    const char* msg = PHYSFS_getErrorByCode(code);
    return msg ? std::string(msg) : std::string();
}

} // namespace og::resources
