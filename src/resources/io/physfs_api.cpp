/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// PhysFS wrapper implementation.

#include <openglad/resources/physfs_api.h>
#include <openglad/resources/filesystem.h>

namespace og::io {

std::string physfs_last_error()
{
    return og::resources::last_error();
}

bool physfs_init(const char* argv0)
{
    return og::resources::init(argv0);
}

bool physfs_deinit()
{
    return og::resources::deinit();
}

bool physfs_set_write_dir(const std::string& path)
{
    return og::resources::set_write_dir(path);
}

bool physfs_mount(const std::string& path, const char* mount_point, int append_to_path)
{
    return og::resources::mount(path, mount_point, append_to_path);
}

bool physfs_unmount(const std::string& path)
{
    return og::resources::unmount(path);
}

std::list<std::string> physfs_enumerate_files_sorted(const std::string& dirname)
{
    return og::resources::enumerate_files_sorted(dirname);
}

} // namespace og::io
