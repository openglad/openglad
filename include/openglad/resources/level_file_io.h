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
    // Provenance mark (SCEN_TYPE_GENERATED in the .fss type byte): true
    // when a campaign generator emitted the scen. File-side metadata only —
    // the loader strips the bit before GameWorld::type is assigned and the
    // writer ORs it back from here, so the sim never sees it. Classic
    // files carry 0 and default to false.
    bool generated = false;
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

// Serialize only the scenario (.fss) payload. Does not write the grid .png file.
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

// --- Tower Climb user-dir level pipeline (docs/tower-triple-design.md D7). ---
// Generated tower floors are written DIRECTLY to <user_path>/scen/ and
// <user_path>/pix/ (the PhysFS write dir), NOT through the editor's temp/
// staging — no runtime repack/remount. Loads fall through the mounted
// tower .glad miss (it carries only the Gate, ids >= 701 are never members)
// to the appended user-path mount. Same payload/PNG writers as save_level,
// so the v9/v10/v11 version cascade is preserved (multifloor floors emit
// v10/v11 automatically). metadata.grid_file may be left empty: it defaults
// to the 8-char grid-field convention "scen{:04d}" (id <= 9999).
bool save_level_to_user_dir(GameWorld& world,
                            int id,
                            const LevelFileMetadata& metadata,
                            LevelFileIoError* out_error = nullptr);

// True when the level's COMPLETE required file set exists non-empty in
// user_path: the .fss, the floor-0 grid PNG, and every "{grid}_fN" extra
// floor plane the .fss's floor_count implies. A torn set (truncated .fss,
// zero-byte or missing plane — e.g. interrupted IDBFS persistence) reads as
// absent so the caller regenerates the whole floor from (seed, N). Decor
// ("_dN") planes are not required: the writer skips empty ones, so absence
// is ambiguous and the loader treats decor loss as cosmetic.
bool tower_floor_files_exist(int id);

// Remove scen{id}.fss plus every derived grid/decor PNG ("scen{:04d}*.png")
// for the id from user_path. Returns true when anything was removed — the
// run-start prune loop `for (id = 701; delete_tower_floor_files(id); ++id)`
// relies on the first missing floor returning false (floors are contiguous).
bool delete_tower_floor_files(int id);

// Read a scenario title from a .fss file with typed error reporting.
LevelFileIoError load_scenario_title_with_error(const char* filename,
                                                std::string& out_title);

// Read a scenario title from a .fss file. Returns "none" on failure.
std::string load_scenario_title(const char* filename);

} // namespace og::data
