/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <functional>
#include <list>
#include <string>

class GameWorld;
class PixieData;

namespace og::io {
class OgFile;
}

namespace og::data {

struct LevelFileMetadata {
    std::string grid_file;
    std::list<std::string> description;
};

enum class LevelFileIoError {
    None = 0,
    OpenReadFailed,
    OpenWriteFailed,
    InvalidHeader,
    ParseFailed,
    UnsupportedVersion,
    SerializeFailed
};

bool load_level(const std::string& path,
                GameWorld& world,
                LevelFileMetadata& metadata,
                LevelFileIoError* out_error = nullptr);

bool load_level(const std::string& path,
                GameWorld& world,
                std::string& grid_file,
                std::list<std::string>& description,
                const std::function<void()>& prepare_for_load,
                LevelFileIoError* out_error = nullptr);

bool save_level(GameWorld& world,
                const std::string& path,
                const LevelFileMetadata& metadata,
                LevelFileIoError* out_error = nullptr);

// Serialize only the scenario (.fss) payload. Does not write the grid .pix file.
bool save_level_scenario_file(GameWorld& world,
                              const std::string& path,
                              const LevelFileMetadata& metadata,
                              LevelFileIoError* out_error = nullptr);

// Helpers kept for compatibility tests and legacy wrappers.
short load_version_2(og::io::OgFile& infile, GameWorld* world, LevelFileMetadata* metadata);
short load_version_3(og::io::OgFile& infile, GameWorld* world, LevelFileMetadata* metadata);
short load_version_4(og::io::OgFile& infile, GameWorld* world, LevelFileMetadata* metadata);
short load_version_5(og::io::OgFile& infile, GameWorld* world, LevelFileMetadata* metadata);
short load_version_6(og::io::OgFile& infile, GameWorld* world, LevelFileMetadata* metadata,
                     short version);
short load_scenario_version(og::io::OgFile& infile, GameWorld* world,
                            LevelFileMetadata* metadata, short version);

bool save_grid_file(const char* gridname, const PixieData& grid);

// Read a scenario title from a .fss file with typed error reporting.
LevelFileIoError load_scenario_title_with_error(const char* filename,
                                                std::string& out_title);

// Read a scenario title from a .fss file. Returns "none" on failure.
std::string load_scenario_title(const char* filename);

} // namespace og::data
