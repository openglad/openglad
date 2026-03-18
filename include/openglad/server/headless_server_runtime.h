#pragma once

#include <openglad/gameplay/lobby_server.h>

class GameWorld;
class LevelRuntimeData;
class SaveData;

namespace og::sim {
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
bool load_headless_level_from_save(LevelRuntimeData& level_data,
                                   SaveData& save,
                                   int difficulty_setting,
                                   og::sim::SimEventLog& events);
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

} // namespace og::server
