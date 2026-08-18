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

// #207 replay-arm policy for apply_headless_lobby_game_start_config.
enum class LobbyStartReplayArm {
    // Adopt a completed landing: a same-campaign start onto a level this
    // save marks completed auto-arms a replay excursion. For sessions with
    // no way to read the hosting player's intent — the dedicated server
    // (no player UI) and networked joiners (who cannot see whether the
    // host pressed VISIT or REPLAY) — any completed landing protects the
    // machine's own campaign position.
    AdoptCompletedLanding,
    // Seeded intent (V5 Option A): the save was seeded from a PLAYER's
    // company save and already says what that player meant — armed =
    // REPLAY, unarmed + completed = VISIT. Keep an arm that covers the
    // negotiated level of the same campaign; clear anything else; never
    // auto-arm (the auto-arm would silently convert every hosted VISIT
    // into a replay).
    SeededIntent,
};

void apply_headless_lobby_game_start_config(
    SaveData& save,
    const og::sim::LobbySaveDataEquivalent& config_save,
    LobbyStartReplayArm arm_policy = LobbyStartReplayArm::AdoptCompletedLanding);
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
