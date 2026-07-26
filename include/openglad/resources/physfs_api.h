#pragma once

#include <list>
#include <string>

// PhysFS wrapper for OpenGlad.
// External PhysFS headers must remain in .cpp files.
namespace og::io {

// Returns a human-readable PhysFS error message for the last PhysFS error.
// Empty if no error is available.
std::string physfs_last_error();

bool physfs_init(const char* argv0);
bool physfs_deinit();
bool physfs_set_write_dir(const std::string& path);

bool physfs_mount(const std::string& path, const char* mount_point, int append_to_path);
bool physfs_unmount(const std::string& path);

// Enumerate files in a directory. Returned list is sorted.
std::list<std::string> physfs_enumerate_files_sorted(const std::string& dirname);

// True when the virtual path exists and is a directory.
bool physfs_is_directory(const std::string& path);

} // namespace og::io
