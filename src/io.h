/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __IO_H__
#define __IO_H__

#include "SDL.h"
#include <list>
#include <vector>
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

std::list<std::string> explode(const std::string& str, char delimiter = '\n');

std::string get_mounted_campaign();
bool mount_campaign_package(const std::string& id);
bool unmount_campaign_package(const std::string& id);
bool remount_campaign_package();
std::list<std::string> list_campaigns();
std::list<int> list_levels();
std::vector<int> list_levels_v();

void restore_default_campaigns();

bool save_settings();
bool load_settings();

void delete_level(int id);
void delete_campaign(const std::string& id);
void delete_user_file(const std::string& filename);

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

// Some versions of GCC have broken template type deduction, so std::find doesn't work.
template<typename iterT, typename T>
iterT list_find(iterT begin, iterT end, const T& value)
{
    while(*begin != value && begin != end)
        begin++;
    return begin;
}

#endif
