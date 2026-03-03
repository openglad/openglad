#pragma once

#include <cstdint>

#include <openglad/resources/gparser.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/platform/game_context.h>
#include <openglad/gameplay/irandom.h>
#include <openglad/gameplay/sim_event_log.h>
#include "test_gameplay_context_scope.h"

// Phase 1a helper: minimal gameplay world + event log fixture.
struct TestGameWorld
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;
    ScopedGameplayContext gameplay;

    explicit TestGameWorld(int level_id = 1)
        : level(level_id, true)
        , gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        level.set_sim_context(&save, &level.world().enemy_freeze, &events, &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
    }

    ~TestGameWorld()
    {
        pop_test_context();
    }

    GameWorld& world() { return level.world(); }
    const GameWorld& world() const { return level.world(); }
};
