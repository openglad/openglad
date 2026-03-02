#pragma once

#include <openglad/runtime/level_runtime_data.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/sim_event_log.h>

class cfg_store;

// Test-only helper: install a temporary gameplay context and restore the
// previous thread-local context on scope exit.
class ScopedGameplayContext
{
public:
    ScopedGameplayContext(LevelRuntimeData& level, SaveData& save,
                          og::sim::SimEventLog& events, cfg_store& config)
        : previous_(current_game)
    {
        context_.world = &level.world();
        context_.save = &save;
        context_.sim_events = &events;
        context_.config = &config;
        current_game = &context_;
    }

    ~ScopedGameplayContext()
    {
        current_game = previous_;
    }

    ScopedGameplayContext(const ScopedGameplayContext&) = delete;
    ScopedGameplayContext& operator=(const ScopedGameplayContext&) = delete;
    ScopedGameplayContext(ScopedGameplayContext&&) = delete;
    ScopedGameplayContext& operator=(ScopedGameplayContext&&) = delete;

private:
    GameplayContext context_;
    GameplayContext* previous_ = nullptr;
};

inline void bind_test_entity_sim_context(LevelRuntimeData& level, walker* entity)
{
    (void)level;
    (void)entity;
}
