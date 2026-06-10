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

// GameServer dead-respawn input gate: true while the (dead) control walker
// has a pending CTF respawn and its input should be ignored.
bool ctf_player_input_should_wait(const GameWorld& world, const walker* control);

// Gate for endgame_requested / team-wipe EndGame consumers: during an active
// CTF match a team wipe never ends the level (phase 7 owns match end).
bool ctf_suppress_team_wipe_endgame(const GameWorld& world);

// Cadence-gated AI director (role assignment for AI livings).
void ctf_run_ai_director(GameWorld& world);

} // namespace og::sim
