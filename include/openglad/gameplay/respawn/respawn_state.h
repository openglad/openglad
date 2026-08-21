/* Respawn engine state and entry points.
 *
 * The respawn engine (queue, timers, rotation serial, team anchor arrays)
 * serves two masters: the classic difficulty-submenu respawn modes and the
 * scripted-mode (TYPE_SCRIPTED) Lua respawn surface (og.respawn_*). It is
 * what survived the CTF engine retirement: flag/control-point/win logic
 * moved to campaign-pack Lua, the RNG-free placement and revive machinery
 * stayed here (src/gameplay/respawn/respawn.cpp).
 *
 * Leaf-header discipline: includes only <cstdint>/<vector>, forward-declares
 * the entity types.
 */
#pragma once

#include <cstdint>
#include <vector>

class GameWorld;
class walker;

namespace og::sim {

inline constexpr int kRespawnMaxAnchorsPerTeam = 16;
inline constexpr int kRespawnMaxQueueEntries = 64;
inline constexpr std::uint16_t kRespawnDefaultTicks = 120;    // 10 s @ 12 Hz

// SaveData/lobby/world values for the classic respawn selector. Keep the
// existing numeric meanings stable for saved companies and network peers;
// the Team 1-only option is appended as value 3.
inline constexpr short kRespawnModeOff = 0;
inline constexpr short kRespawnModeHeroes = 1;
inline constexpr short kRespawnModeEveryone = 2;
inline constexpr short kRespawnModeTeamOneHeroes = 3;

// Requested-delay clamp for classic respawns (the SaveData/lobby
// ctf_respawn_ticks knob rides the same channel; out-of-range requests fall
// back to kRespawnDefaultTicks).
inline constexpr short kClassicMinRespawnTicks = 12;
inline constexpr short kClassicMaxRespawnTicks = 1200;
// Blocked classic AI respawns retry on this bounded deterministic cadence.
inline constexpr std::uint16_t kClassicBlockedRetryTicks = 12;

struct RespawnEntry
{
    std::uint8_t kind = 0;                  // 0 = player (revive walker_entity_id), 1 = AI (spawn family/level)
    std::uint8_t team = 0;
    std::uint8_t family = 0;                // AI only
    std::uint8_t level = 1;                 // AI only
    std::uint16_t ticks_left = 0;
    std::uint32_t walker_entity_id = 0;     // corpse id (scheduled corpses stay in oblist until fire)
    // Respawn location, filled at schedule time: the corpse's recorded spawn
    // point when set, else where it fell.
    std::int16_t x = -1;
    std::int16_t y = -1;
    std::uint8_t floor = 0;
};

// The engine-owned respawn substate. Classic and scripted modes read/write
// the SAME storage through GameWorld::respawn; snapshot v10 replicates it
// as its own block.
struct RespawnState
{
    std::uint16_t respawn_ticks = kRespawnDefaultTicks;
    std::uint16_t respawn_serial = 0;       // anchor rotation cursor
    std::uint8_t anchor_count[4] = {};      // team start markers (level init scan)
    std::int16_t anchor_x[4][kRespawnMaxAnchorsPerTeam] = {};
    std::int16_t anchor_y[4][kRespawnMaxAnchorsPerTeam] = {};
    std::vector<RespawnEntry> respawn_queue;   // cap kRespawnMaxQueueEntries
};

// True when a kind-0 (player revive) entry for this corpse is still pending.
// Shared by the HUD countdown and the view-control keep-alive paths.
inline bool respawn_pending_player(const RespawnState& respawn,
                                   std::uint32_t entity_id)
{
    for (const RespawnEntry& entry : respawn.respawn_queue)
    {
        if (entry.kind == 0 && entry.walker_entity_id == entity_id)
            return true;
    }
    return false;
}

// Whole-seconds estimate for a pending respawn (flat ticks -> seconds; the
// CTF control-point acceleration retired with the CTF engine). Shared by
// the SDL and curses HUDs; 12 is the sim cadence in ticks per second.
inline int respawn_seconds_left(const RespawnEntry& entry)
{
    return (static_cast<int>(entry.ticks_left) + 11) / 12;
}

// --- Classic (difficulty-submenu) respawn engine ---------------------------
// Player corpses revive at their recorded spawn point (in place when it is
// blocked), mode 2 additionally respawns unowned AI livings at their
// authored placement, and mode 3 respawns only Team 1 heroes (the
// player-facing Team 1 is internal team 0). Never draws world.rng_.

// True when the classic respawn engine owns respawns on this level: a world
// without an active scripted mode and one of the supported nonzero respawn
// modes.
bool classic_respawn_active(const GameWorld& world);

// True when a dead player-controlled hero is guaranteed to remain in the
// active respawn engine instead of being replaced by the normal control
// reacquisition scan. Used by the authoritative input path so a seat keeps
// the same entity assignment throughout its countdown. Scripted arm: a
// queued entry + an undecided match keep the seat.
bool respawn_retains_player_control(const GameWorld& world,
                                    const walker* control);

// The classic per-tick engine, called from GameWorld::tick() AFTER the level
// completion decision (its end-of-level revive-all must observe game_ended
// the same tick) and BEFORE the dead sweep (the death scan needs this tick's
// corpses). No-op unless classic_respawn_active(world).
void classic_respawn_run_tick(GameWorld& world);

// End-of-level revive-all: schedule this tick's still-unseen corpses (a hero
// dying on the very tick the level ends has no queue entry yet), fire every
// pending player (kind-0) respawn immediately, and clear the queue. Called
// in-tick on every end shape, and directly by the synchronous exit-accept /
// display-persist paths, which never tick the world between the accept and
// the roster persist. Idempotent; no-op unless classic_respawn_active(world).
void classic_respawn_flush_pending(GameWorld& world);

// True while a foe is merely awaiting a classic respawn: a hostile pending
// queue entry, or a hostile corpse the death scan (which runs after the
// completion decision) will still schedule this tick. The extermination win
// check treats these as live foes, so "endless battle" (mode 2) cannot be
// won by out-racing the respawn delay. Always false when the engine is off.
bool classic_respawn_pending_hostile_foe(GameWorld& world);

// Combined gate for the endgame_requested / team-wipe EndGame consumers: an
// undecided scripted match or an active classic respawn mode keeps a team
// wipe from ending the level (respawns give the wiped team a way back in).
bool respawn_suppress_team_wipe_endgame(const GameWorld& world,
                                        short wiped_team = -1);

// --- Mode-agnostic engine entry points (impl in respawn.cpp) ---------------

// Rescan team start markers (Order::Special / FAMILY_RESERVED_TEAM, dead ones
// included — the level bootstrap kills the markers it consumes) into the
// anchor arrays. Runs at the first scripted-mode tick, BEFORE that tick's
// dead sweep can remove consumed markers.
void respawn_scan_anchors(GameWorld& world);

// Per-tick countdown + fire. Fire routing: scripted mode -> RNG-free classic
// placement + the on_respawn level hook; classic modes -> classic placement.
void respawn_run_timers(GameWorld& world);

// Win-latch flush (scripted modes): fire every queued entry through the
// RNG-free classic paths, then revive still-unqueued eligible player corpses
// in place — the CTF win-check revive-all semantics, generalized. No
// on_respawn dispatch (it can run inside a Lua binding call; the match is
// decided). Idempotent.
void respawn_flush_revive_all(GameWorld& world);

// og.respawn_schedule backend: dedupe (already queued / live duplicate),
// then the shared schedule path (queue cap 64 + bot eviction). Life gems
// scrub at schedule; every stain — player and AI — remains until the
// respawn fires. ticks_override > 0 replaces the resolved delay for this
// entry. Returns true when a new entry was queued.
bool respawn_schedule_corpse(GameWorld& world, walker* corpse,
                             int ticks_override);

// og.respawn_pending / og.respawn_pending_count backends.
bool respawn_pending_for(const GameWorld& world, const walker* w);
int respawn_pending_count(const GameWorld& world, int team);

// The eat-free placement probe (never query_passable — the probe-eats rule).
// floor < 0 probes the walker's own grid floor + the floor-0 obmap buckets
// (the anchor probe); floor >= 0 is the floor-explicit classic probe.
bool respawn_spot_clear(GameWorld& world, walker* w, short x, short y,
                        int floor);

// Positional stain scrub (og.scrub_corpse_stain backend): preserve a STAIN
// tied to a pending respawn (player or AI); otherwise kill fresh STAIN /
// LIFE_GEM drops whose sprite center lies within 8px Manhattan of the center
// of a 16px box at (x, y). This is the fire-path rule keyed by position (and
// optionally floor >= 0) instead of a corpse walker, so Lua can scrub after
// the corpse is gone. No team filter: the caller's hook context decides which
// corpses get scrubbed.
void respawn_scrub_stains_at(GameWorld& world, short x, short y, int floor);

} // namespace og::sim
