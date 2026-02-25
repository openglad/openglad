#pragma once

#include <list>
#include <string>

namespace og::gameplay {
class GameWorld;
}

struct LevelVisuals;

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
                og::gameplay::GameWorld& world,
                LevelVisuals& visuals,
                LevelFileMetadata& metadata,
                LevelFileIoError* out_error = nullptr);

bool save_level(og::gameplay::GameWorld& world,
                LevelVisuals& visuals,
                const std::string& path,
                const LevelFileMetadata& metadata,
                LevelFileIoError* out_error = nullptr);

// Read a scenario title from a .fss file. Returns "none" on failure.
std::string load_scenario_title(const char* filename);

} // namespace og::data
