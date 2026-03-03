#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

namespace og::resources {

bool init(const char* argv0);
bool deinit();
bool set_write_dir(const std::string& path);

// Mirrors PhysFS semantics:
// - append_to_path == 0 prepends mount
// - append_to_path != 0 appends mount
bool mount(const char* archive, const char* mountpoint = nullptr,
           int append_to_path = 1);
bool unmount(const char* archive);

std::list<std::string> list_files(const char* dirname);
std::vector<std::uint8_t> read_file(const char* path);
bool write_file(const char* path, const void* data, std::size_t len);
bool exists(const char* path);

std::string filesystem_last_error();

} // namespace og::resources
