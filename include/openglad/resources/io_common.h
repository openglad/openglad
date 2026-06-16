/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// SDL-free platform I/O declarations.
// Shared by both SDL and headless builds.

#include <openglad/resources/campaign_io.h>
#include <openglad/resources/filesystem_sync.h>

#include <cstddef>
#include <list>
#include <string>
#include <string_view>
#include <vector>

class PixieData;

void io_init(int argc, char* argv[]);
void io_exit();

std::string get_user_path();
bool create_dir(const std::string& dirname);

std::list<std::string> list_files(const std::string& dirname);

std::list<std::string> explode(const std::string& str, char delimiter = '\n');

std::string get_mounted_campaign();
#ifdef TESTING
void set_mounted_campaign_for_testing(const std::string& id);
#endif
bool is_safe_campaign_id(std::string_view id);
bool is_safe_virtual_basename(std::string_view name,
                              std::size_t max_length = 64);

enum class CampaignPackageIoError {
    None = 0,
    EmptyId,
    MountFailed,
    UnmountFailed,
};

enum class ArchiveIoError {
    None = 0,
    OpenArchiveFailed,
    AddEntryFailed,
    OpenEntryFailed,
    OpenOutputFailed,
    ReadEntryFailed,
    CloseArchiveFailed,
    ResourceLimitExceeded,
};

[[nodiscard]] CampaignPackageIoError mount_campaign_package_with_error(const std::string& id);
[[nodiscard]] CampaignPackageIoError unmount_campaign_package_with_error(const std::string& id);
[[nodiscard]] CampaignPackageIoError remount_campaign_package_with_error();
std::list<std::string> list_campaigns();
std::list<int> list_levels();
std::vector<int> list_levels_v();

void restore_default_campaigns();
void restore_default_settings();

bool save_settings();
bool load_settings();

void delete_level(int id);
void delete_campaign(const std::string& id);

[[nodiscard]] ArchiveIoError zip_contents_with_error(const std::string& indirectory, const std::string& outfile);
[[nodiscard]] ArchiveIoError unzip_into_with_error(const std::string& infile, const std::string& outdirectory);

bool unpack_campaign(const std::string& campaign_id);
bool repack_campaign(const std::string& campaign_id);

void cleanup_unpacked_campaign();

enum class NewFileIoError {
    None = 0,
    InvalidDimensions,
    OpenWriteFailed,
    WriteFailed,
};

[[nodiscard]] NewFileIoError create_new_map_pix_with_error(const std::string& filename, int w, int h);
[[nodiscard]] NewFileIoError create_new_pix_with_error(const std::string& filename, int w, int h, unsigned char fill_color = 0);
[[nodiscard]] NewFileIoError create_new_campaign_descriptor_with_error(const std::string& filename);
[[nodiscard]] NewFileIoError create_new_scen_file_with_error(const std::string& scenfile, const std::string& gridname);

void load_map_data(PixieData* whereto);
