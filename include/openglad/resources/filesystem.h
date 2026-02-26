#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

namespace og::resources {

std::string last_error();
int last_error_code();
bool last_error_is_files_still_open();
bool last_error_is_not_mounted();
bool init(const char* argv0);
bool deinit();
bool set_write_dir(const std::string& path);

bool mount(const std::string& archive, const char* mountpoint = nullptr, int append_to_path = 1);
bool unmount(const std::string& archive);
bool exists(const char* path);

std::vector<std::uint8_t> read_file(const char* path);
bool write_file(const char* path, const void* data, std::size_t len);
std::list<std::string> enumerate_files_sorted(const std::string& dirname);

} // namespace og::resources
