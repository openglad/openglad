#ifndef __IO_H__
#define __IO_H__

#include "SDL.h"
#include <list>
#include <string>

void io_init(int argc, char* argv[]);
void io_exit();

SDL_RWops* open_read_file(const char* file);
SDL_RWops* open_read_file(const char* path, const char* file);
SDL_RWops* open_write_file(const char* file);
SDL_RWops* open_write_file(const char* path, const char* file);

std::list<std::string> list_files(const std::string& dirname);

bool mount_campaign_package(const std::string& id);
bool unmount_campaign_package(const std::string& id);
std::list<std::string> list_campaigns();
std::list<std::string> list_levels();


int rwops_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read);

bool zip_contents(const std::string& indirectory, const std::string& outfile);
bool unzip_into(const std::string& infile, const std::string& outdirectory);

#endif
