#ifndef __IO_H__
#define __IO_H__

#include "SDL.h"
#include <list>
#include <string>

void io_init(int argc, char* argv[]);
void io_exit();


std::string get_user_path();
bool create_dir(const std::string& dirname);

SDL_RWops* open_read_file(const char* file);
SDL_RWops* open_read_file(const char* path, const char* file);
SDL_RWops* open_write_file(const char* file);
SDL_RWops* open_write_file(const char* path, const char* file);

std::list<std::string> list_files(const std::string& dirname);

std::string get_mounted_campaign();
bool mount_campaign_package(const std::string& id);
bool unmount_campaign_package(const std::string& id);
std::list<std::string> list_campaigns();
std::list<std::string> list_levels();


int rwops_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read);
int rwops_write_handler(void *data, unsigned char *buffer, size_t size);

bool zip_contents(const std::string& indirectory, const std::string& outfile);
bool unzip_into(const std::string& infile, const std::string& outdirectory);

bool unpack_campaign(const std::string& campaign_id);
bool repack_campaign(const std::string& campaign_id);

void cleanup_unpacked_campaign();

bool create_new_map_pix(const std::string& filename, int w, int h);
bool create_new_pix(const std::string& filename, int w, int h, unsigned char fill_color = 0);
bool create_new_campaign_descriptor(const std::string& filename);
bool create_new_scen_file(const std::string& scenfile, const std::string& gridname);

#endif
