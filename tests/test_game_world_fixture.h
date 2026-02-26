#pragma once

#include <cstdint>

#include <openglad/data/gparser.h>
#include <openglad/data/level_data.h>
#include <openglad/data/save_data.h>
#include <openglad/runtime/game_context.h>
#include <openglad/sim/irandom.h>
#include <openglad/sim/sim_event_log.h>

// Phase 1a helper: minimal gameplay world + event log fixture.
struct TestGameWorld
{
    LevelData level;
    SaveData save;
    std::int32_t enemy_freeze = 0;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;

    explicit TestGameWorld(int level_id = 1)
        : level(level_id, true)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        set_global_context(&gc);
    }

    ~TestGameWorld()
    {
        set_global_context(nullptr);
    }

    GameWorld& world() { return level.world(); }
    const GameWorld& world() const { return level.world(); }
};
