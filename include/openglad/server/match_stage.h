/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * og::server::MatchStage — the staged-lobby authoritative world (#218).
 *
 * Each lobby OWNER (SDL solo/split picker, SDL network host, curses hosts,
 * the dedicated server) owns one MatchStage. It builds the match's REAL
 * world in the lobby, before GO, through the dedicated-server pipeline
 * (apply_headless_lobby_game_start_config + load_headless_level_from_save +
 * spawn_team_from_save), runs level on_load and mode init for real at stage
 * time, and holds the result dormant: nothing constructs a GameServer over
 * it, so nothing can tick it or drain its announcement queue. The preview
 * reads this world; the launch adopts it.
 *
 * Determinism (the match-id latch): identical MatchStageInputs produce a
 * byte-identical staged keyframe. The sim RNG is pinned to the match seed
 * BEFORE the level load (walker construction and the teleport fallback draw
 * from it) and the process-global weather roll sequence is pinned beside it,
 * so a restage with unchanged inputs cannot flicker the preview.
 *
 * Dual residency note: this header keeps its include/openglad/server/ path
 * while src/server/match_stage.cpp is compiled into the og_interface
 * component AND into every headless binary's source list — the exact
 * level_runtime_data.cpp precedent (no headless binary links og_interface,
 * so there are no duplicate symbols).
 *
 * campaign_tag rule (GTL v16): staged saves built off the wire equivalent
 * start every guy's campaign_tag at 0; each owning machine re-stamps its own
 * tags on its own merge (merge_owned_guys_from) — the stage never persists.
 */
#pragma once

#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/headless_server_runtime.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class GameWorld;
class LevelRuntimeData;

namespace og::server {

// The change key: exactly the launch inputs, nothing else. Deliberately
// EXCLUDES ready bits, denials and request ids — a ready-up flip must never
// restage. The owner recomputes this after every lobby poll from the SAME
// functions the launch consumes (build_save_data_equivalent /
// build_player_bindings) and hands it to observe_inputs().
struct MatchStageInputs {
    og::sim::LobbySaveDataEquivalent equivalent;
    std::vector<og::sim::LobbyPlayerBinding> bindings;
    std::int32_t difficulty = 1;
    // The match-id RNG latch. Drawn by the owner from non-sim entropy once
    // per round (re-latched at resume_after_level); it never rides the wire —
    // its consequences ride the staged keyframe (rng_state is replicated).
    std::uint32_t match_seed = 0;

    bool operator==(const MatchStageInputs&) const = default;
};

enum class StageStatus : std::uint8_t {
    Empty,
    Staged,
    Failed,
};

// Trailing-edge restage debounce. Not an optimization: a restage is a full
// level load + per-world Lua VM rebuild, and a tick-0 world keyframes on
// every send — knob cycling must coalesce into one rebuild + one broadcast.
inline constexpr std::uint64_t kStageDebounceMs = 250;

// Per-owner staging configuration, fixed at construction.
struct MatchStageConfig {
    // Networked owners install the real control policy from the bindings;
    // solo/local owners stamp the legacy shared pool instead.
    bool networked = true;
    // Replay-arm policy for apply_headless_lobby_game_start_config: the
    // dedicated server adopts completed landings; player-hosted sessions
    // seed the host's own intent (V5 Option A).
    LobbyStartReplayArm arm_policy = LobbyStartReplayArm::AdoptCompletedLanding;
    // Optional host company save seeded UNDER the equivalent (the curses-host
    // shape): carries completed_levels, the campaign_state decision book and
    // the transient replay arm the wire equivalent cannot. Non-owning; must
    // outlive the stage. nullptr = the dedicated server's fresh-save shape.
    const SaveData* host_company_save = nullptr;
};

class MatchStage
{
public:
    explicit MatchStage(MatchStageConfig config = {});
    ~MatchStage();

    MatchStage(const MatchStage&) = delete;
    MatchStage& operator=(const MatchStage&) = delete;

    // Record the current change key. The first key (or any key while nothing
    // is staged) stages immediately; a changed key marks the stage dirty and
    // stamps the debounce clock; an identical key is a no-op.
    void observe_inputs(const MatchStageInputs& inputs, std::uint64_t now_ms);

    // Restage when dirty and the trailing-edge debounce window has elapsed.
    // Called from every owner poll.
    void maintain(std::uint64_t now_ms);

    // GO: force a synchronous restage when dirty (a stale stage can never
    // launch). Returns true when a staged world is current.
    bool ensure_current(std::uint64_t now_ms);

    // Destroy the staged world/VM/save and clear the announcement queue —
    // one disposal boundary. Restage is always dispose-and-rebuild, never
    // in-place (GameWorld::clear() does not reset rng_/tick counters/mode
    // latches).
    void dispose();

    // Owner-side failure funnel: the change-key BUILD can throw before any
    // inputs reach observe_inputs (LobbyServer::build_save_data_equivalent's
    // 24-roster cap). The owner catches and lands here — dispose + Failed +
    // a generation bump — never a process exit out of the poll loop.
    void mark_failed(std::string_view failure);

    [[nodiscard]] StageStatus status() const noexcept { return status_; }
    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] std::uint32_t stage_generation() const noexcept
    {
        return stage_generation_;
    }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] std::uint32_t match_seed() const noexcept
    {
        return inputs_.match_seed;
    }

    // The staged authoritative world (nullptr unless status() == Staged).
    [[nodiscard]] GameWorld* world() noexcept;
    [[nodiscard]] const GameWorld* world() const noexcept;

    // The undrained init/on_load announcement queue. Every event is stamped
    // tick 1 (the tick those announcements carry when init runs lazily), so
    // adoption appends them into the live session's log verbatim
    // (SimEventLog::append) and the first ticked drain delivers them once.
    [[nodiscard]] const og::sim::SimEventLog& staged_events() const noexcept
    {
        return staged_events_;
    }
    [[nodiscard]] std::vector<og::sim::Event> take_events();

    // The staged save the launch adopts (session fields like
    // keep_fallen_heroes / cross_control included).
    [[nodiscard]] SaveData& staged_save() noexcept { return staged_save_; }
    [[nodiscard]] const SaveData& staged_save() const noexcept
    {
        return staged_save_;
    }

    // The serialized broadcast pair (protocol v13), rebuilt on every
    // successful stage: a COMPLETE InitialSetup wire message (the level
    // metadata a snapshot cannot carry; my_team is the owner's — the launch
    // still sends each peer its own per-seat setup) and a COMPLETE keyframe
    // SnapshotMessage of the dormant tick-0 world (Peek — a staged capture
    // must never consume dirty masks). Host pane, joiner mirror and the wire
    // all read these same bytes. A staged world whose keyframe cannot fit
    // the u16 wire cap FAILS the stage with a legible error instead of
    // throwing out of the owner's poll loop.
    [[nodiscard]] const std::vector<std::uint8_t>& staged_setup_bytes()
        const noexcept
    {
        return staged_setup_bytes_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& staged_keyframe_bytes()
        const noexcept
    {
        return staged_keyframe_bytes_;
    }

private:
    void stage_now(std::uint64_t now_ms);
    void wire_stage_context();
    // False = the staged world cannot fit the u16 wire cap (status Failed,
    // legible error set).
    [[nodiscard]] bool build_staged_wire_bytes(GameWorld& staged_world);

    MatchStageConfig config_;
    MatchStageInputs inputs_;
    bool has_inputs_ = false;
    bool dirty_ = false;
    std::uint64_t dirty_since_ms_ = 0;
    std::uint32_t stage_generation_ = 0;
    StageStatus status_ = StageStatus::Empty;
    std::string error_;

    SaveData staged_save_;
    og::sim::SimEventLog staged_events_;
    std::unique_ptr<LevelRuntimeData> staged_level_;
    std::vector<std::uint8_t> staged_setup_bytes_;
    std::vector<std::uint8_t> staged_keyframe_bytes_;

    // The stage's own GameplayContext (the obmap/current_game discipline):
    // installed via save/restore swap for every stage/dispose, exactly like
    // the curses sessions bracket their loads.
    GameplayContext stage_ctx_;
    IRandom* stage_rng_ptr_ = nullptr;
    bool stage_active_ = false;
};

// The match-id latch source: non-sim entropy (std::random_device xor the
// steady clock), drawn by each owner ONCE per round — at lobby open and again
// at every resume_after_level (each round is a fresh match; every restage
// WITHIN a round reuses the latched seed, so identical inputs cannot flicker
// the preview).
[[nodiscard]] std::uint32_t draw_match_seed();

// The owners' debounce clock (SDL-free steady clock, milliseconds).
[[nodiscard]] std::uint64_t stage_clock_now_ms();

// Owner-side delivery bookkeeping for the staged pair (protocol v13).
struct StageBroadcastState {
    std::uint32_t last_sent_generation = 0;
    std::vector<og::sim::PeerId> known_peers;
};

// Delivery half of an owner poll step: broadcast the staged pair when the
// stage generation moved since the last delivery, plus per-peer catch-up
// sends for newly connected peers (a spectator connect changes no stage
// input, so it would otherwise never see a pair). `transport` may be null:
// the solo owner reads its stage in-process and puts nothing on the wire.
void deliver_staged_pair(MatchStage& stage,
                         og::sim::ITransport* transport,
                         StageBroadcastState& broadcast);

// One whole owner poll step: record the change key, run the trailing-edge
// debounce, then deliver (deliver_staged_pair). Owners that hit the
// change-key build throw call stage.mark_failed() instead of this.
void drive_lobby_stage(MatchStage& stage,
                       const MatchStageInputs& inputs,
                       std::uint64_t now_ms,
                       og::sim::ITransport* transport,
                       StageBroadcastState& broadcast);

} // namespace og::server
