/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// PhysFS wrapper implementation.

#include <openglad/io/physfs_api.h>

#include "physfs.h"

#include <memory>

namespace og::io {

namespace {
struct PhysfsFileListDeleter {
    void operator()(char** list) const
    {
        if (list)
            PHYSFS_freeList(list);
    }
};
} // namespace

std::string physfs_last_error()
{
    const PHYSFS_ErrorCode code = PHYSFS_getLastErrorCode();
    const char* msg = PHYSFS_getErrorByCode(code);
    return msg ? std::string(msg) : std::string();
}

bool physfs_init(const char* argv0)
{
    return PHYSFS_init(argv0) != 0;
}

bool physfs_deinit()
{
    return PHYSFS_deinit() != 0;
}

bool physfs_set_write_dir(const std::string& path)
{
    return PHYSFS_setWriteDir(path.c_str()) != 0;
}

bool physfs_mount(const std::string& path, const char* mount_point, int append_to_path)
{
    return PHYSFS_mount(path.c_str(), mount_point, append_to_path) != 0;
}

bool physfs_unmount(const std::string& path)
{
    return PHYSFS_unmount(path.c_str()) != 0;
}

bool physfs_exists(const std::string& path)
{
    return PHYSFS_exists(path.c_str()) != 0;
}

std::list<std::string> physfs_enumerate_files_sorted(const std::string& dirname)
{
    std::list<std::string> out;
    std::unique_ptr<char*, PhysfsFileListDeleter> files(PHYSFS_enumerateFiles(dirname.c_str()));
    for (char** p = files.get(); p != nullptr && *p != nullptr; ++p)
        out.push_back(*p);
    out.sort();
    return out;
}

} // namespace og::io
