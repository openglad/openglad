#pragma once

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/resources/save_data.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <filesystem>
#include <string_view>
#include <vector>

namespace og::fuzz {

// One private filesystem per process; every input gets fresh gameplay state.
// All parsing and serialization stays in the production resource libraries.
void initialize(const char* executable);
void write_input(std::string_view relative, const std::uint8_t* data,
                 std::size_t size);
void require(bool condition, const char* message);
std::vector<std::uint8_t> read_output(std::string_view relative);
void export_seed(std::string_view relative, const std::filesystem::path& destination);
void check_seed_isolation(const std::filesystem::path& directory,
                          std::string_view canonical,
                          int (*test_input)(const std::uint8_t*, std::size_t));

class Input {
public:
    Input();
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    SaveData save;
    og::sim::SimEventLog events;
    std::unique_ptr<GameWorld> world;
    SeededRandom rng{1};
};

} // namespace og::fuzz
