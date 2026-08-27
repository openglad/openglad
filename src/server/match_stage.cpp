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

#include <openglad/core/test_trace.h>
#include <openglad/core/util.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/level_data_hooks.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <random>
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

std::uint64_t host_save_stage_digest(const SaveData* save)
{
    if (save == nullptr)
        return 0;

    // FNV-1a 64. Order-stable by construction: campaign_state is a std::map
    // whose per-campaign entry lists are kept sorted by key (the
    // campaign_state_set write choke), and completed_levels is a map of
    // sorted sets.
    std::uint64_t hash = 14695981039346656037ull;
    const auto fold_byte = [&hash](std::uint8_t byte) {
        hash ^= byte;
        hash *= 1099511628211ull;
    };
    const auto fold_i32 = [&fold_byte](std::int32_t value) {
        const auto raw = static_cast<std::uint32_t>(value);
        fold_byte(static_cast<std::uint8_t>(raw & 0xffu));
        fold_byte(static_cast<std::uint8_t>((raw >> 8) & 0xffu));
        fold_byte(static_cast<std::uint8_t>((raw >> 16) & 0xffu));
        fold_byte(static_cast<std::uint8_t>((raw >> 24) & 0xffu));
    };
    const auto fold_string = [&fold_byte, &fold_i32](const std::string& s) {
        fold_i32(static_cast<std::int32_t>(s.size()));
        for (const char c : s)
            fold_byte(static_cast<std::uint8_t>(c));
    };

    for (const auto& [campaign, entries] : save->campaign_state)
    {
        fold_string(campaign);
        for (const auto& [key, value] : entries)
        {
            fold_string(key);
            fold_i32(value);
        }
    }
    for (const auto& [campaign, levels] : save->completed_levels)
    {
        fold_string(campaign);
        for (const int level : levels)
            fold_i32(level);
    }
    return hash;
}

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

MatchStage::TakenStage MatchStage::take()
{
    TakenStage taken;
    if (status_ != StageStatus::Staged || !staged_level_)
        return taken;

    taken.events = staged_events_.drain();
    taken.level = std::move(staged_level_);
    // The moved-out level still points at this stage's save/events/rng
    // through its sim context; the caller MUST rewire set_sim_context to its
    // own session before anything touches the world. dispose() resets the
    // remaining bookkeeping (the level pointer is already null, so nothing
    // is destroyed).
    dispose();
    return taken;
}

void MatchStage::observe_inputs(const MatchStageInputs& inputs,
                                std::uint64_t now_ms)
{
    // The owner's key deliberately omits what the stage can read itself:
    // stamp the live host-save digest here, every poll, so a mid-lobby
    // og.campaign_state_set write moves the key even though no lobby
    // knob/roster byte changed.
    MatchStageInputs stamped = inputs;
    stamped.host_save_digest = host_save_stage_digest(config_.host_company_save);
    if (has_inputs_ && stamped == inputs_ && status_ != StageStatus::Empty)
        return;

    inputs_ = stamped;
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
    // GO re-checks the host-save digest directly: a campaign-state write can
    // land between the last poll's observe_inputs and the GO click, and a
    // stale stage must never launch.
    if (has_inputs_)
    {
        const std::uint64_t digest =
            host_save_stage_digest(config_.host_company_save);
        if (digest != inputs_.host_save_digest)
        {
            inputs_.host_save_digest = digest;
            dirty_ = true;
        }
    }
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
    staged_setup_bytes_.clear();
    staged_keyframe_bytes_.clear();
    status_ = StageStatus::Empty;
    error_.clear();
}

void MatchStage::mark_failed(std::string_view failure)
{
    // The owner calls this from EVERY poll while its change-key build keeps
    // throwing (the 24-roster cap): a repeat of the same failure must not
    // churn the generation or spam the log.
    if (status_ == StageStatus::Failed && error_ == failure)
        return;
    dispose();
    // Poison the recorded key: the throwing build never reached
    // observe_inputs, so the stored inputs are the LAST GOOD key — a lobby
    // that recovers back to exactly that key must restage, not no-op.
    has_inputs_ = false;
    status_ = StageStatus::Failed;
    error_ = failure;
    ++stage_generation_;
    LogError("match_stage_failed error={}\n", error_);
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

        // 8b. The MODE-LESS stage step (docs/lineup-design.md C2), for real,
        //     on the same world: when nothing claimed the level in step 8 —
        //     a classic map, or a scripted one whose init refused — the
        //     lineup hook packs/core registers applies the per-team map-unit
        //     strip and the FILL squads here, so a classic campaign's
        //     preview IS its launch. No hook registered means no write and
        //     no RNG draw, so an unmodded classic world stages exactly the
        //     bytes it staged before this seam existed.
        staged_world.run_lineup_stage_step();

        // 9. Serialize the broadcast pair (v13). An oversize keyframe fails
        //    the stage with a legible error — GO is denied through the start
        //    gate instead of a throw escaping the owner's poll loop.
        if (!build_staged_wire_bytes(staged_world))
        {
            // context_guard is still installed: the teardown runs under the
            // staged world's own context.
            staged_level_.reset();
            stage_rng_ptr_ = nullptr;
            staged_events_.clear();
            ++stage_generation_;
            return;
        }

        status_ = StageStatus::Staged;
        error_.clear();
        ++stage_generation_;
        TRACE("stage", "restaged gen=%u level=%d",
              static_cast<unsigned>(stage_generation_),
              static_cast<int>(staged_world.id));
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
        staged_setup_bytes_.clear();
        staged_keyframe_bytes_.clear();
        status_ = StageStatus::Failed;
        error_ = stage_error.what();
        ++stage_generation_;
        LogError("match_stage_failed error={}\n", error_);
    }
}

bool MatchStage::build_staged_wire_bytes(GameWorld& staged_world)
{
    staged_setup_bytes_.clear();
    staged_keyframe_bytes_.clear();

    og::sim::InitialSetupMessage setup;
    setup.level_id = staged_world.id;
    setup.level_title = staged_world.title;
    setup.level_type = static_cast<std::int8_t>(staged_world.type);
    setup.par_value = staged_world.par_value;
    setup.time_bonus_limit = staged_world.time_bonus_limit;
    setup.difficulty = staged_world.difficulty;
    setup.pixmaxx = staged_world.pixmaxx;
    setup.pixmaxy = staged_world.pixmaxy;
    // Broadcast, not per-seat: my_team is the owner's and the controlled-id
    // block stays empty (controls are claimed by the launch GameServer, which
    // still sends every peer its own per-seat InitialSetup).
    setup.my_team = staged_world.my_team;
    setup.allied_mode = staged_world.allied_mode;
    setup.current_scenario = staged_world.current_scenario;
    setup.respawn_mode = staged_world.respawn_mode;
    setup.generator_rate = staged_world.generator_rate;
    setup.guys = og::sim::collect_initial_setup_guys(staged_world);
    setup.completed_levels.assign(staged_world.completed_levels.begin(),
                                  staged_world.completed_levels.end());

    try
    {
        staged_setup_bytes_ = og::sim::serialize_initial_setup_message(setup);
        staged_keyframe_bytes_ = og::sim::serialize_snapshot(
            og::sim::peek_keyframe_snapshot(staged_world));
    }
    catch (const std::exception&)
    {
        // serialize_snapshot throws when the COMPRESSED keyframe exceeds the
        // u16 wire length — fold it into the size refusal below.
        staged_setup_bytes_.clear();
        staged_keyframe_bytes_.clear();
    }

    if (staged_setup_bytes_.empty() || staged_keyframe_bytes_.empty() ||
        staged_setup_bytes_.size() > og::sim::kMaxStagedInnerMessageBytes ||
        staged_keyframe_bytes_.size() > og::sim::kMaxStagedInnerMessageBytes)
    {
        staged_setup_bytes_.clear();
        staged_keyframe_bytes_.clear();
        status_ = StageStatus::Failed;
        error_ = "staged world exceeds the wire message size cap";
        LogError("match_stage_failed error={}\n", error_);
        return false;
    }

    return true;
}

bool adopt_staged_world(LevelRuntimeData& dst_level,
                        SaveData& dst_save,
                        MatchStage& stage)
{
    if (stage.status() != StageStatus::Staged || stage.world() == nullptr)
        return false;

    GameWorld& staged = *stage.world();
    GameWorld& dst = dst_level.world();

    // Content transfer through the load pipeline's own commit step: entities
    // (with their owned guys), grid/decor/obmap/floors, mode + respawn
    // latches, level scalars. The world id rides beside it (the load path
    // sets it before its own load; the latch claim below keys on it).
    dst.id = staged.id;
    replace_loaded_world_state(&dst_level, staged);

    // The staged VM moves WITH the content: the staged on_load's dynamic
    // registrations — og.set_entity_hooks tables keyed by the just-moved
    // entity ids, module-local state — live in the world's WorldScripts VM
    // and nowhere else. A fresh destination VM would replay pack scripts but
    // hold none of them, and the claimed on_load latch below (correctly)
    // prevents re-registration — the court.lua boss on_death shape would
    // silently never fire on the SDL launch path.
    dst.adopt_scripts_from(staged);

    // What the move deliberately leaves to the caller:
    dst.control_policy = staged.control_policy;
    dst.player_machine = staged.player_machine;
    dst.guy_id_counter = staged.guy_id_counter;
    dst.keep_fallen_heroes = staged.keep_fallen_heroes;
    dst.campaign_vars = staged.campaign_vars;
    // Tick 1 continues the exact RNG stream the staged preview froze at.
    dst.rng_.state_ = staged.rng_.state_;
    // The REAL latch transferred truthfully: the staged world ran on_load
    // and dst IS that world's state — the first tick must not re-run it.
    // (Must run after the caller's reset_level_progress, which re-arms it.)
    dst.claim_level_load_latch();
    // Same transfer for the mode-less stage step (C2): the stager ran it in
    // step 8b on every world no mode owned, so dst's first tick must not run
    // it a second time and spawn a second set of FILL squads. Claimed on
    // exactly the worlds step 8b covered — a mode-owned world never reaches
    // the classic arm that would consume the claim, and leaving one latched
    // there would suppress the NEXT level's step.
    if (!staged.mode.active)
        dst.claim_staged_lineup_stage();

    // The staged save becomes the session save (team_list, cross_control,
    // replay arm, campaign_state — the launch inputs the wire equivalent
    // materialized at stage time).
    copy_headless_server_save_data(dst_save, stage.staged_save());

    return true;
}

// --- StagedPreviewMirror (C9) ------------------------------------------------

StagedPreviewMirror::StagedPreviewMirror() = default;

StagedPreviewMirror::~StagedPreviewMirror()
{
    dispose();
}

const GameWorld* StagedPreviewMirror::world() const noexcept
{
    if (status_ != MirrorStatus::Staged || !mirror_level_)
        return nullptr;
    return &mirror_level_->world();
}

GameWorld* StagedPreviewMirror::world() noexcept
{
    if (status_ != MirrorStatus::Staged || !mirror_level_)
        return nullptr;
    return &mirror_level_->world();
}

bool StagedPreviewMirror::retained_pair(
    std::uint32_t& generation,
    const std::vector<std::uint8_t>*& setup_bytes,
    const std::vector<std::uint8_t>*& keyframe_bytes) const
{
    if (pending_setup_bytes_.empty() || pending_keyframe_bytes_.empty())
        return false;
    generation = pending_generation_;
    setup_bytes = &pending_setup_bytes_;
    keyframe_bytes = &pending_keyframe_bytes_;
    return true;
}

void StagedPreviewMirror::receive_setup(
    const og::sim::StagedMatchSetupMessage& message)
{
    if (message.setup_bytes.empty())
        return;
    if (message.stage_generation != pending_generation_ ||
        pending_setup_bytes_.empty())
    {
        // A setup for a new generation opens a fresh pair: any retained
        // keyframe belonged to the previous setup and can no longer pair.
        pending_generation_ = message.stage_generation;
        pending_keyframe_bytes_.clear();
        attempted_pending_ = false;
    }
    else if (pending_setup_bytes_ != message.setup_bytes)
    {
        // Same generation, different bytes (an owner-side resend replacing a
        // corrupted delivery): fresh content re-arms the apply.
        attempted_pending_ = false;
    }
    pending_setup_bytes_ = message.setup_bytes;
}

void StagedPreviewMirror::receive_keyframe(
    const og::sim::StagedMatchKeyframeMessage& message)
{
    // Generation pairing: a keyframe that does not match the last received
    // setup's generation is dropped — owners send setup first on a per-peer
    // ordered transport, so a mismatch means a restage raced this pair and
    // the newest pair is (or will shortly be) on the wire.
    if (message.snapshot_bytes.empty() || pending_setup_bytes_.empty() ||
        message.stage_generation != pending_generation_)
    {
        TRACE("stage", "mirror dropped unpaired keyframe gen=%u pending=%u",
              static_cast<unsigned>(message.stage_generation),
              static_cast<unsigned>(pending_generation_));
        return;
    }
    if (attempted_pending_ && pending_keyframe_bytes_ == message.snapshot_bytes)
        return; // broadcast + per-peer catch-up duplicate of the same pair
    pending_keyframe_bytes_ = message.snapshot_bytes;
    attempted_pending_ = false;
}

void StagedPreviewMirror::ensure_applied(std::string_view current_campaign,
                                         int difficulty,
                                         std::uint64_t now_ms)
{
    if (pending_setup_bytes_.empty() || pending_keyframe_bytes_.empty())
        return;
    if (attempted_pending_)
    {
        if (status_ != MirrorStatus::Unavailable)
            return;
        // Retry an honest failure when the local campaign catches up to the
        // setup (the lobby-sync race) or on the slow clock (a class-pack
        // transfer finishing its mount) — the owner never resends on OUR
        // account.
        const bool campaign_moved = current_campaign != last_attempt_campaign_;
        const bool retry_due = now_ms >= last_attempt_ms_ + kMirrorRetryMs;
        if (!campaign_moved && !retry_due)
            return;
    }
    attempted_pending_ = true;
    last_attempt_ms_ = now_ms;
    last_attempt_campaign_ = current_campaign;
    // Every attempt — success or honest failure — is a preview refresh.
    ++refresh_serial_;
    apply_retained_pair(current_campaign, difficulty);
}

void StagedPreviewMirror::wire_mirror_context()
{
    GameWorld& mirror_world = mirror_level_->world();
    mirror_rng_ptr_ = &mirror_world.rng_;
    mirror_level_->set_sim_context(&mirror_save_,
                                   &mirror_world.enemy_freeze,
                                   &mirror_events_,
                                   mirror_rng_ptr_,
                                   &cfg);
    mirror_ctx_ = {};
    mirror_ctx_.world = &mirror_world;
    mirror_ctx_.save = &mirror_save_;
    mirror_ctx_.sim_events = &mirror_events_;
    mirror_ctx_.config = &cfg;
    mirror_ctx_.session_rng_ref = &mirror_rng_ptr_;
    mirror_ctx_.gameplay_active_ref = &mirror_active_;
}

void StagedPreviewMirror::dispose_level()
{
    if (mirror_level_)
    {
        ScopedStageContext context_guard(&mirror_ctx_);
        mirror_level_.reset();
    }
    mirror_ctx_ = {};
    mirror_rng_ptr_ = nullptr;
    mirror_active_ = false;
    mirror_events_.clear();
    loaded_level_id_ = -1;
    loaded_scenario_ = -1;
    loaded_campaign_.clear();
}

void StagedPreviewMirror::dispose()
{
    dispose_level();
    pending_generation_ = 0;
    pending_setup_bytes_.clear();
    pending_keyframe_bytes_.clear();
    attempted_pending_ = false;
    last_attempt_ms_ = 0;
    last_attempt_campaign_.clear();
    status_ = MirrorStatus::Empty;
    error_.clear();
    applied_generation_ = 0;
    // refresh_serial_ stays monotonic: disposal is itself a state a keyed
    // preview must not confuse with the last applied generation.
}

void StagedPreviewMirror::fail(std::string_view why)
{
    // Poison the loaded-level identity: a failed apply may have left the
    // world half-written, so the retry (or the next pair) must rebuild.
    loaded_level_id_ = -1;
    status_ = MirrorStatus::Unavailable;
    error_ = why;
    LogError("staged_preview_mirror_unavailable error={}\n", error_);
}

void StagedPreviewMirror::apply_retained_pair(std::string_view current_campaign,
                                              int difficulty)
{
    const std::optional<og::sim::InitialSetupMessage> setup =
        og::sim::deserialize_initial_setup_message(pending_setup_bytes_);
    if (!setup.has_value())
    {
        fail("staged setup decode failed");
        return;
    }

    og::sim::WorldSnapshot snapshot;
    try
    {
        snapshot = og::sim::deserialize_snapshot(pending_keyframe_bytes_);
    }
    catch (const std::exception&)
    {
        fail("staged keyframe decode failed");
        return;
    }

    try
    {
        const bool rebuild = !mirror_level_ ||
            loaded_level_id_ != setup->level_id ||
            loaded_scenario_ != setup->current_scenario ||
            loaded_campaign_ != current_campaign;
        if (rebuild)
        {
            dispose_level();
            // A minimal roster-less save: the local load exists for the grid,
            // decor and authored entities (named NPCs survive only through
            // it); every replicated fact is healed by the keyframe apply.
            mirror_save_.reset();
            og::sim::LobbySaveDataEquivalent equivalent;
            equivalent.current_campaign = std::string(current_campaign);
            equivalent.scen_num = static_cast<short>(setup->level_id);
            equivalent.numplayers = 1;
            apply_headless_lobby_game_start_config(
                mirror_save_, equivalent,
                LobbyStartReplayArm::AdoptCompletedLanding);
            mirror_level_ = std::make_unique<LevelRuntimeData>(
                static_cast<short>(setup->level_id),
                /*headless=*/true,
                &headless_level_data_hooks());
            wire_mirror_context();
            ScopedStageContext context_guard(&mirror_ctx_);
            if (!load_headless_level_from_save(*mirror_level_,
                                               mirror_save_,
                                               difficulty,
                                               mirror_events_,
                                               /*authoritative=*/false) ||
                mirror_save_.scen_num != setup->level_id)
            {
                // The second clause is the first-level fallback: a level this
                // machine cannot load locally must not masquerade as the
                // staged one (the keyframe grid would not even fit it).
                mirror_level_.reset();
                mirror_rng_ptr_ = nullptr;
                fail("staged preview level load failed");
                return;
            }
            loaded_level_id_ = setup->level_id;
            loaded_scenario_ = setup->current_scenario;
            loaded_campaign_ = current_campaign;
        }

        GameWorld& mirror_world = mirror_level_->world();
        ScopedStageContext context_guard(&mirror_ctx_);
        og::sim::apply_initial_setup_to_world(mirror_world, *setup);
        // apply_snapshot self-installs its snapshot context and event
        // suppression guard: a mirror can never manufacture announcements,
        // and the tick-0 apply's on_load re-arm is inert (mirrors never
        // tick).
        if (!og::sim::apply_snapshot(mirror_world, snapshot))
        {
            fail("staged keyframe apply failed");
            return;
        }

        status_ = MirrorStatus::Staged;
        error_.clear();
        applied_generation_ = pending_generation_;
        TRACE("stage", "mirror applied gen=%u level=%d",
              static_cast<unsigned>(applied_generation_),
              static_cast<int>(mirror_world.id));
    }
    catch (const std::exception& apply_error)
    {
        if (mirror_level_)
        {
            ScopedStageContext dispose_guard(&mirror_ctx_);
            mirror_level_.reset();
        }
        mirror_ctx_ = {};
        mirror_rng_ptr_ = nullptr;
        fail(apply_error.what());
    }
}

std::uint32_t draw_match_seed()
{
    std::random_device device;
    const std::uint64_t clock_bits = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return device() ^ static_cast<std::uint32_t>(clock_bits) ^
        static_cast<std::uint32_t>(clock_bits >> 32);
}

std::uint64_t stage_clock_now_ms()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void drive_lobby_stage(MatchStage& stage,
                       const MatchStageInputs& inputs,
                       std::uint64_t now_ms,
                       og::sim::ITransport* transport,
                       StageBroadcastState& broadcast)
{
    stage.observe_inputs(inputs, now_ms);
    stage.maintain(now_ms);
    deliver_staged_pair(stage, transport, broadcast);
}

void deliver_staged_pair(MatchStage& stage,
                         og::sim::ITransport* transport,
                         StageBroadcastState& broadcast)
{
    if (transport == nullptr)
        return;

    const std::vector<og::sim::PeerId> peers = transport->connected_peers();
    const bool generation_moved =
        stage.stage_generation() != broadcast.last_sent_generation;
    const bool pair_available = stage.status() == StageStatus::Staged &&
        !stage.staged_setup_bytes().empty() &&
        !stage.staged_keyframe_bytes().empty();

    if (pair_available)
    {
        const std::vector<std::uint8_t> setup_wire =
            og::sim::serialize_staged_match_setup_message(
                {.stage_generation = stage.stage_generation(),
                 .setup_bytes = stage.staged_setup_bytes()});
        const std::vector<std::uint8_t> keyframe_wire =
            og::sim::serialize_staged_match_keyframe_message(
                {.stage_generation = stage.stage_generation(),
                 .snapshot_bytes = stage.staged_keyframe_bytes()});
        // The wrapper serializers refuse oversize (they cannot here — the
        // stage already enforced the inner cap), but stay defensive.
        if (!setup_wire.empty() && !keyframe_wire.empty())
        {
            if (generation_moved)
            {
                // Restage completed since the last delivery: one broadcast
                // pair, setup first (clients pair a keyframe with the last
                // received setup's generation).
                transport->broadcast(setup_wire);
                transport->broadcast(keyframe_wire);
                TRACE("stage", "broadcast gen=%u",
                      static_cast<unsigned>(stage.stage_generation()));
            }
            else
            {
                // Catch-up: a peer that connected after the last broadcast
                // (spectator connects change no stage input) gets the current
                // pair per-peer.
                for (const og::sim::PeerId peer : peers)
                {
                    const bool known =
                        std::find(broadcast.known_peers.begin(),
                                  broadcast.known_peers.end(),
                                  peer) != broadcast.known_peers.end();
                    if (known)
                        continue;
                    transport->send(peer, setup_wire);
                    transport->send(peer, keyframe_wire);
                    TRACE("stage", "catchup peer=%u gen=%u",
                          static_cast<unsigned>(peer),
                          static_cast<unsigned>(stage.stage_generation()));
                }
            }
        }
    }

    broadcast.last_sent_generation = stage.stage_generation();
    broadcast.known_peers = peers;
}

} // namespace og::server
