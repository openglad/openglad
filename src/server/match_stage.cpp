/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The staged-lobby authoritative world (#218) — see match_stage.h for the
 * ownership and determinism contract. Dual-listed like level_runtime_data.cpp
 * (og_interface component + every headless source list).
 */
#include <openglad/server/match_stage.h>

#include <openglad/core/util.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/level_data_hooks.h>

#include <array>
#include <exception>
#include <utility>

namespace og::server {

namespace {

// Save/restore context swap (the curses-session bracketing). NOT
// GameplayContextGuard: the guard asserts against re-entrant installation of
// a DIFFERENT context, and lobby owners (the SDL picker especially) stage
// while their own session context is installed.
class ScopedStageContext
{
public:
    explicit ScopedStageContext(GameplayContext* ctx)
        : previous_(current_game)
    {
        current_game = ctx;
    }

    ~ScopedStageContext() { current_game = previous_; }

    ScopedStageContext(const ScopedStageContext&) = delete;
    ScopedStageContext& operator=(const ScopedStageContext&) = delete;

private:
    GameplayContext* previous_;
};

} // namespace

MatchStage::MatchStage(MatchStageConfig config)
    : config_(config)
{
}

MatchStage::~MatchStage()
{
    dispose();
}

GameWorld* MatchStage::world() noexcept
{
    return staged_level_ ? &staged_level_->world() : nullptr;
}

const GameWorld* MatchStage::world() const noexcept
{
    return staged_level_ ? &staged_level_->world() : nullptr;
}

std::vector<og::sim::Event> MatchStage::take_events()
{
    return staged_events_.drain();
}

void MatchStage::observe_inputs(const MatchStageInputs& inputs,
                                std::uint64_t now_ms)
{
    if (has_inputs_ && inputs == inputs_ && status_ != StageStatus::Empty)
        return;

    inputs_ = inputs;
    has_inputs_ = true;
    if (status_ == StageStatus::Empty)
    {
        // First stage (lobby entry, or the round after a dispose) is
        // immediate — the preview should not wait out a debounce for the
        // initial world.
        stage_now(now_ms);
        return;
    }
    dirty_ = true;
    dirty_since_ms_ = now_ms;
}

void MatchStage::maintain(std::uint64_t now_ms)
{
    if (!dirty_ || !has_inputs_)
        return;
    if (now_ms < dirty_since_ms_ ||
        now_ms - dirty_since_ms_ < kStageDebounceMs)
        return;
    stage_now(now_ms);
}

bool MatchStage::ensure_current(std::uint64_t now_ms)
{
    if (has_inputs_ && (dirty_ || status_ == StageStatus::Empty))
        stage_now(now_ms);
    return status_ == StageStatus::Staged;
}

void MatchStage::dispose()
{
    if (staged_level_)
    {
        // Tear the world down under its own context (obmap discipline).
        ScopedStageContext context_guard(&stage_ctx_);
        staged_level_.reset();
    }
    stage_ctx_ = {};
    stage_rng_ptr_ = nullptr;
    stage_active_ = false;
    staged_events_.clear();
    staged_events_.current_tick_ = 0;
    status_ = StageStatus::Empty;
    error_.clear();
}

void MatchStage::wire_stage_context()
{
    GameWorld& staged_world = staged_level_->world();
    stage_rng_ptr_ = &staged_world.rng_;
    staged_level_->set_sim_context(&staged_save_,
                                   &staged_world.enemy_freeze,
                                   &staged_events_,
                                   stage_rng_ptr_,
                                   &cfg);
    stage_ctx_ = {};
    stage_ctx_.world = &staged_world;
    stage_ctx_.save = &staged_save_;
    stage_ctx_.sim_events = &staged_events_;
    stage_ctx_.config = &cfg;
    stage_ctx_.session_rng_ref = &stage_rng_ptr_;
    stage_ctx_.gameplay_active_ref = &stage_active_;
}

void MatchStage::stage_now(std::uint64_t now_ms)
{
    (void)now_ms;
    dirty_ = false;

    // Dispose-and-rebuild, never in-place: one disposal boundary owns the
    // world, the per-world Lua VM and the obmap; the event clear-then-requeue
    // means exactly one copy of each announcement survives a restage.
    dispose();

    try
    {
        // 1. The staged save: optionally the host company's history under
        //    the negotiated lobby config (the curses-host V5 Option A shape);
        //    the dedicated server's shape is the fresh save.
        staged_save_.reset();
        if (config_.host_company_save != nullptr)
        {
            copy_headless_server_save_data(staged_save_,
                                           *config_.host_company_save);
        }
        apply_headless_lobby_game_start_config(staged_save_,
                                               inputs_.equivalent,
                                               config_.arm_policy);

        // 2. Fresh headless-hooked level runtime + this stage's own context.
        //    HEADLESS hooks only, on every platform: lobby-poll restaging
        //    must never touch the SDL loader (#162).
        staged_level_ = std::make_unique<LevelRuntimeData>(
            staged_save_.scen_num,
            /*headless=*/true,
            &headless_level_data_hooks());
        wire_stage_context();
        GameWorld& staged_world = staged_level_->world();
        ScopedStageContext context_guard(&stage_ctx_);

        // 3. Pin every entropy source BEFORE the load: the sim RNG (walker
        //    construction and the teleport fallback draw from it during the
        //    load/spawn) and the process-global weather roll sequence (the
        //    authoritative load rolls this level's weather; the pin makes
        //    every restage reproduce the same kind).
        staged_world.rng_.state_ = inputs_.match_seed;
        og::set_weather_roll_sequence(inputs_.match_seed);

        // 4. The dedicated-server pipeline: campaign load, level load with
        //    first-level fallback, difficulty pass, roster spawn, completed-
        //    level purge, prepare_world_for_gameplay (tick 0 + event clear).
        if (!load_headless_level_from_save(*staged_level_,
                                           staged_save_,
                                           inputs_.difficulty,
                                           staged_events_,
                                           /*authoritative=*/true))
        {
            // context_guard is still installed: the teardown runs under the
            // staged world's own context.
            staged_level_.reset();
            stage_rng_ptr_ = nullptr;
            status_ = StageStatus::Failed;
            error_ = "staged level load failed";
            ++stage_generation_;
            return;
        }

        // 5. Control policy: networked owners stamp the real §4.4 policy from
        //    the lobby bindings; solo/local stamps the legacy shared pool so
        //    no local round can ever run owner-locked.
        if (config_.networked)
        {
            og::sim::install_control_policy(staged_world,
                                            /*networked=*/true,
                                            staged_save_.cross_control != 0,
                                            inputs_.bindings,
                                            staged_save_.team_list);
        }
        else
        {
            std::array<std::uint8_t, og::sim::kPlayerMachineSlots> no_machines;
            no_machines.fill(og::sim::kPlayerMachineNone);
            og::sim::set_control_policy(staged_world,
                                        og::sim::kControlPolicyLegacy,
                                        no_machines);
        }

        // 6. Announcements queued from here carry the tick-1 stamp they carry
        //    today (GameWorld::tick sets events.current_tick_ = tick_count_
        //    before dispatching init when init runs lazily).
        staged_events_.current_tick_ = 1;

        // 7. Level on_load, for real: spawns land in this world (visible in
        //    the preview — the #239 Westlands case), announcements queue in
        //    staged_events_. The latch is consumed by running the real code.
        staged_world.run_pending_level_on_load();

        // 8. Mode init, for real — the same tick-side gate (a failed init is
        //    NOT a stage failure: it is the honest refused-match shape, mode
        //    inactive, classic rules; mode_stage_init self-latches).
        if ((staged_world.type & GameWorld::TYPE_SCRIPTED) != 0)
            og::sim::mode_stage_init(staged_world);

        status_ = StageStatus::Staged;
        error_.clear();
        ++stage_generation_;
    }
    catch (const std::exception& stage_error)
    {
        if (staged_level_)
        {
            ScopedStageContext dispose_guard(&stage_ctx_);
            staged_level_.reset();
        }
        stage_ctx_ = {};
        stage_rng_ptr_ = nullptr;
        staged_events_.clear();
        status_ = StageStatus::Failed;
        error_ = stage_error.what();
        ++stage_generation_;
        LogError("match_stage_failed error={}\n", error_);
    }
}

} // namespace og::server
