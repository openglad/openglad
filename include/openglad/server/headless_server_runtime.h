#pragma once

#include <openglad/gameplay/lobby_server.h>

class GameWorld;
class LevelRuntimeData;
class SaveData;

namespace og::sim {
class GameServer;
class SimEventLog;
}

namespace og::server {

void copy_headless_server_save_data(SaveData& destination,
                                    const SaveData& source);
void apply_headless_lobby_game_start_config(
    SaveData& save,
    const og::sim::LobbySaveDataEquivalent& config_save);
void sync_headless_server_save_data_from_world(SaveData& save,
                                               const GameWorld& world);
// `authoritative` marks a SERVER world load: it rolls the per-level weather
// after the level is prepared. Client mirror worlds (curses/text displays
// that reuse this loader) MUST pass false — they receive the weather kind
// through the world snapshot instead of rolling their own.
bool load_headless_level_from_save(LevelRuntimeData& level_data,
                                   SaveData& save,
                                   int difficulty_setting,
                                   og::sim::SimEventLog& events,
                                   bool authoritative);
bool complete_headless_level_and_load_next(LevelRuntimeData& level_data,
                                           SaveData& active_save,
                                           SaveData& checkpoint_save,
                                           int difficulty_setting,
                                           og::sim::SimEventLog& events,
                                           int next_level);
bool withdraw_headless_level(LevelRuntimeData& level_data,
                             SaveData& active_save,
                             SaveData& checkpoint_save,
                             int difficulty_setting,
                             og::sim::SimEventLog& events,
                             int destination_level);

// Install the authoritative dedicated-server persistence/transition hooks on
// a live GameServer. Kept here so the production server and headless runtime
// tests exercise the same callback closures.
void install_headless_server_callbacks(og::sim::GameServer& game_server,
                                       LevelRuntimeData& level_data,
                                       SaveData& active_save,
                                       SaveData& checkpoint_save,
                                       int difficulty_setting,
                                       og::sim::SimEventLog& events);

} // namespace og::server
