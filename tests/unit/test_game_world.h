#pragma once

#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/sim_event_log.h>

// RAII helper for unit tests that need a minimal GameWorld + SimEventLog.
// No thread-local installation needed at this stage (Phase 1a).
// In Phase 4, this will be extended into TestGameplayContext which also
// installs the current_game thread-local.
class TestGameWorld {
public:
    TestGameWorld() = default;
    ~TestGameWorld() = default;

    og::gameplay::GameWorld world;
    og::sim::SimEventLog events;
};
