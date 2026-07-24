/* Capture-the-flag simulation state and engine API.
 *
 * This header is deliberately leaf-level: it includes only <cstdint> and
 * <vector> and forward-declares the entity types, so game_world.h can embed
 * a CtfState member without include cycles. All per-tick CTF logic lives in
 * src/gameplay/ctf/ctf.cpp and ctf_ai.cpp.
 */
#pragma once

#include <cstdint>
#include <vector>

class GameWorld;
class walker;

namespace og::sim {

inline constexpr int kCtfMaxFlags = 4;            // one per score team
inline constexpr int kCtfMaxControlPoints = 4;
inline constexpr int kCtfMaxAnchorsPerTeam = 16;
inline constexpr int kCtfMaxRespawnEntries = 64;
inline constexpr std::uint16_t kCtfDefaultRespawnTicks = 120;    // 10 s @ 12 Hz
inline constexpr std::uint16_t kCtfDefaultFlagReturnTicks = 360;
inline constexpr std::uint32_t kCtfDefaultTimeLimitTicks = 14400;
inline constexpr std::uint8_t kCtfDefaultCaptureLimit = 3;
inline constexpr std::uint16_t kCtfCaptureScore = 400;           // m_score per capture
inline constexpr std::uint16_t kCtfCpCaptureScore = 50;          // m_score per point capture
inline constexpr int kCtfCpCaptureTicks = 36;
inline constexpr int kCtfCpPulsePeriod = 300;
inline constexpr int kCtfCpPulseRadius = 96;                     // px, localized speed pulse
inline constexpr int kCtfAiCadenceTicks = 15;

enum class CtfFlagState : std::uint8_t { AtHome = 0, Carried = 1, Dropped = 2 };

struct CtfFlag
{
    CtfFlagState state = CtfFlagState::AtHome;
    std::uint32_t carrier_entity_id = 0;    // 0 = none
    std::int16_t x = 0, y = 0;              // current position (== carrier pos while carried)
    std::int16_t home_x = 0, home_y = 0;
    std::uint16_t return_ticks = 0;         // countdown while Dropped
    std::uint32_t flag_entity_id = 0;       // the FAMILY_FLAG treasure (fxlist)
    bool present = false;                   // team has a flag on this map
};

struct CtfControlPoint
{
    std::int8_t owner = -1;                 // -1 neutral
    std::int16_t progress = 0;              // accumulates toward progress_team
    std::int8_t progress_team = -1;
    std::int16_t x = 0, y = 0;
    std::uint8_t radius_tiles = 3;
    std::uint32_t entity_id = 0;
    std::uint32_t next_pulse_tick = 0;
};

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

struct CtfState
{
    bool active = false;                    // set only by ctf_initialize_for_level / apply_snapshot
    bool init_attempted = false;            // lazy-init latch
    std::uint8_t team_count = 2;            // active team count after init
    std::uint8_t capture_limit = kCtfDefaultCaptureLimit;
    std::uint16_t respawn_ticks = kCtfDefaultRespawnTicks;
    std::uint16_t flag_return_ticks = kCtfDefaultFlagReturnTicks;
    std::uint32_t time_limit_ticks = kCtfDefaultTimeLimitTicks;  // 0 = none
    std::int8_t winner_team = -1;
    bool winner_is_player = false;
    std::uint16_t captures[4] = {};
    std::uint16_t respawn_serial = 0;       // anchor rotation cursor
    bool team_active[4] = {};
    CtfFlag flags[kCtfMaxFlags];            // index == team
    std::uint8_t cp_count = 0;
    CtfControlPoint cps[kCtfMaxControlPoints];
    std::uint8_t anchor_count[4] = {};
    std::int16_t anchor_x[4][kCtfMaxAnchorsPerTeam] = {};
    std::int16_t anchor_y[4][kCtfMaxAnchorsPerTeam] = {};
    std::vector<CtfRespawnEntry> respawn_queue;   // cap kCtfMaxRespawnEntries
};

// Full reset + scan of flags/markers/control points, inactive-team stripping,
// bot squad bootstrap, and config resolution from GameWorld::ctf_requested_*.
// Leaves ctf.active false when the map authors fewer than two flag teams.
void ctf_initialize_for_level(GameWorld& world);

// The per-tick engine, called from GameWorld::tick() on TYPE_CTF levels.
void ctf_run_tick(GameWorld& world);

// on_eat hook bodies for the FAMILY_FLAG / FAMILY_CTF_POINT treasures.
bool ctf_on_flag_touch(walker* flag, walker* eater);
bool ctf_on_point_touch(walker* point, walker* eater);

// Gate for endgame_requested / team-wipe EndGame consumers: during an active
// CTF match a team wipe never ends the level (phase 7 owns match end).
bool ctf_suppress_team_wipe_endgame(const GameWorld& world);

// --- Classic (non-CTF) respawn engine ---------------------------------------
// Reuses the CtfState respawn substate (respawn_queue / respawn_ticks) with
// classic fire rules: player corpses revive at their recorded spawn point (in
// place when it is blocked), and mode 2 additionally respawns unowned AI
// livings at their authored placement. Never draws world.rng_, and leaves
// ctf.active false so every CTF-only consumer keeps keying off it.

// Requested-delay clamp for classic respawns (the SaveData/lobby
// ctf_respawn_ticks knob rides the same channel; out-of-range requests fall
// back to kCtfDefaultRespawnTicks).
inline constexpr short kClassicMinRespawnTicks = 12;
inline constexpr short kClassicMaxRespawnTicks = 1200;
// Blocked classic AI respawns retry on this bounded deterministic cadence.
inline constexpr std::uint16_t kClassicBlockedRetryTicks = 12;

// True when the classic respawn engine owns respawns on this level: a non-CTF
// world with respawn_mode 1 (heroes) or 2 (heroes + unowned AI livings).
bool classic_respawn_active(const GameWorld& world);

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
// active CTF match or an active classic respawn mode keeps a team wipe from
// ending the level (respawns give the wiped team a way back in).
bool respawn_suppress_team_wipe_endgame(const GameWorld& world);

// Canonical color name for a score team ("RED"/"GREEN"/"BLUE"/"YELLOW"),
// matching the rendered palette ramps (team*16+40). Shared by the sim
// notifications and every win/results surface so players always see the
// same name for the same tint.
const char* ctf_team_color_name(int team);

// Which score teams author a (live) flag on this map. Non-mutating fxlist
// scan; usable before ctf_initialize_for_level runs (e.g. by team-choice
// surfaces constraining CTF team selection to authored teams).
void ctf_authored_flag_teams(const GameWorld& world, bool (&present)[4]);
[[nodiscard]] std::uint8_t
ctf_authored_flag_team_mask(const GameWorld& world) noexcept;

// True when a kind-0 (player revive) entry for this corpse is still pending.
// Shared by the HUD countdown and the view-control keep-alive paths.
inline bool ctf_pending_player_respawn(const CtfState& ctf,
                                       std::uint32_t entity_id)
{
    for (const CtfRespawnEntry& entry : ctf.respawn_queue)
    {
        if (entry.kind == 0 && entry.walker_entity_id == entity_id)
            return true;
    }
    return false;
}

// Cadence-gated AI director (role assignment for AI livings).
void ctf_run_ai_director(GameWorld& world);

// Whole-seconds estimate for a pending respawn, honoring the control-point
// acceleration (run_respawn_timers burns two ticks per tick while the team
// owns any CP). Shared by the SDL and curses HUDs so both track the sim
// rule; 12 is the sim cadence in ticks per second.
inline int ctf_respawn_seconds_left(const CtfState& ctf,
                                    const CtfRespawnEntry& entry)
{
    int ticks_per_second = 12;
    for (int i = 0; i < ctf.cp_count; ++i)
    {
        if (ctf.cps[i].owner == static_cast<std::int8_t>(entry.team))
        {
            ticks_per_second = 24;
            break;
        }
    }
    return (static_cast<int>(entry.ticks_left) + ticks_per_second - 1) /
           ticks_per_second;
}

} // namespace og::sim
