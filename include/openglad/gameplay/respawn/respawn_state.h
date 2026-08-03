/* Respawn engine state and entry points.
 *
 * The respawn engine (queue, timers, rotation serial, team anchor arrays)
 * serves three masters: the CTF match engine, the classic difficulty-submenu
 * respawn modes, and the scripted-mode (TYPE_SCRIPTED) Lua respawn surface.
 * This header owns the state struct and the mode-agnostic entry points;
 * ctf/ctf_state.h layers the CTF-only fields on top (CtfState derives from
 * RespawnState so the two current engines and every snapshot/HUD reader keep
 * their existing member spellings — the physical storage split off CtfState
 * lands with the CTF engine retirement and the next snapshot bump).
 *
 * Leaf-header discipline: includes only <cstdint>/<vector>, forward-declares
 * the entity types. Implementation currently lives in src/gameplay/ctf/
 * ctf.cpp beside the CTF fire paths it shares.
 */
#pragma once

#include <cstdint>
#include <vector>

class GameWorld;
class walker;

namespace og::sim {

inline constexpr int kCtfMaxAnchorsPerTeam = 16;
inline constexpr int kCtfMaxRespawnEntries = 64;
inline constexpr std::uint16_t kCtfDefaultRespawnTicks = 120;    // 10 s @ 12 Hz

// SaveData/lobby/world values for the classic (non-CTF) respawn selector.
// Keep the existing numeric meanings stable for saved companies and network
// peers; the Team 1-only option is appended as value 3.
inline constexpr short kRespawnModeOff = 0;
inline constexpr short kRespawnModeHeroes = 1;
inline constexpr short kRespawnModeEveryone = 2;
inline constexpr short kRespawnModeTeamOneHeroes = 3;

// Requested-delay clamp for classic respawns (the SaveData/lobby
// ctf_respawn_ticks knob rides the same channel; out-of-range requests fall
// back to kCtfDefaultRespawnTicks).
inline constexpr short kClassicMinRespawnTicks = 12;
inline constexpr short kClassicMaxRespawnTicks = 1200;
// Blocked classic AI respawns retry on this bounded deterministic cadence.
inline constexpr std::uint16_t kClassicBlockedRetryTicks = 12;

struct CtfRespawnEntry
{
    std::uint8_t kind = 0;                  // 0 = player (revive walker_entity_id), 1 = AI (spawn family/level)
    std::uint8_t team = 0;
    std::uint8_t family = 0;                // AI only
    std::uint8_t level = 1;                 // AI only
    std::uint16_t ticks_left = 0;
    std::uint32_t walker_entity_id = 0;     // corpse id (player corpses stay in oblist)
    // Classic (non-CTF) respawn location, filled at schedule time: the
    // corpse's recorded spawn point when set, else where it fell. CTF fire
    // paths ignore these (anchor rotation stays authoritative for CTF).
    std::int16_t x = -1;
    std::int16_t y = -1;
    std::uint8_t floor = 0;
};

// Mode-neutral spelling; CtfRespawnEntry keeps the wire/test name until the
// CTF retirement rename sweep.
using RespawnEntry = CtfRespawnEntry;

// The engine-owned respawn substate. CTF, classic, and scripted modes all
// read/write the SAME storage through GameWorld::respawn.
struct RespawnState
{
    std::uint16_t respawn_ticks = kCtfDefaultRespawnTicks;
    std::uint16_t respawn_serial = 0;       // anchor rotation cursor
    std::uint8_t anchor_count[4] = {};      // team start markers (level init scan)
    std::int16_t anchor_x[4][kCtfMaxAnchorsPerTeam] = {};
    std::int16_t anchor_y[4][kCtfMaxAnchorsPerTeam] = {};
    std::vector<CtfRespawnEntry> respawn_queue;   // cap kCtfMaxRespawnEntries
};

// True when a kind-0 (player revive) entry for this corpse is still pending.
// Shared by the HUD countdown and the view-control keep-alive paths.
// (Callers passing a CtfState bind through the base subobject.)
inline bool ctf_pending_player_respawn(const RespawnState& respawn,
                                       std::uint32_t entity_id)
{
    for (const CtfRespawnEntry& entry : respawn.respawn_queue)
    {
        if (entry.kind == 0 && entry.walker_entity_id == entity_id)
            return true;
    }
    return false;
}

// --- Mode-agnostic engine entry points (impl in src/gameplay/ctf/ctf.cpp) ---

// Rescan team start markers (Order::Special / FAMILY_RESERVED_TEAM, dead ones
// included — the level bootstrap kills the markers it consumes) into the
// anchor arrays. Runs at CTF init and at the first scripted-mode tick, both
// BEFORE that tick's dead sweep can remove consumed markers.
void respawn_scan_anchors(GameWorld& world);

// Per-tick countdown + fire. Fire routing: scripted mode -> RNG-free classic
// placement + the on_respawn level hook; classic modes -> classic placement;
// CTF -> anchor rotation. Owning any control point still halves a CTF/
// scripted team's wait (cp_count is 0 outside CTF, so the halving is inert).
void respawn_run_timers(GameWorld& world);

// Win-latch flush (scripted modes): fire every queued entry through the
// RNG-free classic paths, then revive still-unqueued eligible player corpses
// in place — the CTF win-check revive-all semantics, generalized. No
// on_respawn dispatch (it can run inside a Lua binding call; the match is
// decided). Idempotent.
void respawn_flush_revive_all(GameWorld& world);

// og.respawn_schedule backend: dedupe (already queued / live duplicate),
// then the shared schedule path (queue cap 64 + bot eviction + stain/
// life-gem scrub). ticks_override > 0 replaces the resolved delay for this
// entry. Returns true when a new entry was queued.
bool respawn_schedule_corpse(GameWorld& world, walker* corpse,
                             int ticks_override);

// og.respawn_pending / og.respawn_pending_count backends.
bool respawn_pending_for(const GameWorld& world, const walker* w);
int respawn_pending_count(const GameWorld& world, int team);

// The eat-free placement probe (never query_passable — the probe-eats rule).
// floor < 0 probes the walker's own grid floor + the floor-0 obmap buckets
// (the CTF anchor probe); floor >= 0 is the floor-explicit classic probe.
bool respawn_spot_clear(GameWorld& world, walker* w, short x, short y,
                        int floor);

// Positional stain scrub (og.scrub_corpse_stain backend): kill fresh STAIN /
// LIFE_GEM drops whose sprite center lies within 8px Manhattan of the center
// of a 16px box at (x, y) — the corpse-relative rule of the schedule-path
// scrub, keyed by position (and optionally floor >= 0) instead of a corpse
// walker, so Lua can scrub after the corpse is gone. No team filter: the
// caller's hook context decides which corpses get scrubbed.
void respawn_scrub_stains_at(GameWorld& world, short x, short y, int floor);

} // namespace og::sim
