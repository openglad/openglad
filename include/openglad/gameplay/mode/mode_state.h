/* Scripted-mode (TYPE_SCRIPTED) replicated state and engine API.
 *
 * A level authored with SCEN_TYPE_SCRIPTED (0x20) hands its match rules to
 * campaign-pack Lua (og.register_level_hooks: on_mode_init / on_mode_tick /
 * on_damage / on_respawn / on_entity_death). ModeState is the ONLY durable
 * home for that Lua's mode state: 64 int32 vars, a generic HUD channel
 * (4 text lines, 4 entity beacons, a short mode name), and the win latch
 * that og.end_level / og.declare_winner write and mode_run_tick re-asserts
 * every tick (GameWorld::tick zeroes game_ended/ending/next_level at entry).
 *
 * Wire-replicated: the snapshot v10 scripted-mode block carries ModeState
 * to mirrors wholesale (world_snapshot.cpp, serialize_mode_state), so
 * mirrors render HUD/beacons/win state straight from the applied snapshot.
 *
 * Leaf-header discipline (ctf_state.h precedent): includes only
 * <array>/<cstdint>, forward-declares GameWorld.
 */
#pragma once

#include <array>
#include <cstdint>

class GameWorld;

namespace og::sim {

inline constexpr int kModeVarCount = 64;
inline constexpr int kModeHudLines = 4;
inline constexpr int kModeHudTextBytes = 26;   // 25 chars + NUL (notification budget)
inline constexpr int kModeBeacons = 4;
inline constexpr int kModeNameBytes = 12;      // 11 chars + NUL

// Kill-attribution freshness window (D3): on_entity_death passes the killer
// only when the walker's last combat stamp is at most this many world ticks
// old (4 s @ 12 Hz); older stamps read as environment deaths (nil, -1).
inline constexpr std::uint32_t kKillAttributionTicks = 48;

struct ModeHudLine
{
    std::uint8_t team = 255;              // 0-3 or band 16-31 ramp; 255 = default
    std::array<char, kModeHudTextBytes> text{};   // NUL-terminated, clamped on write
};

struct ModeBeacon
{
    std::int32_t entity_id = 0;           // 0 = empty slot
    std::uint8_t team = 255;
};

struct ModeState
{
    bool active = false;                  // set on a successful on_mode_init
    bool init_attempted = false;          // lazy-init latch (CTF discipline)
    bool win_latched = false;
    std::int8_t winner_team = -1;         // -1 = undecided
    bool winner_is_player = false;
    std::int8_t win_ending = 0;
    std::int16_t win_next_level = -1;
    std::array<char, kModeNameBytes> name{};        // "CTF", "MUTANT", ... (og.set_mode_name)
    std::array<std::int32_t, kModeVarCount> vars{}; // og.mode_get / og.mode_set
    std::array<ModeHudLine, kModeHudLines> hud{};
    std::array<ModeBeacon, kModeBeacons> beacons{};
};

// The scripted-mode per-tick engine, called from GameWorld::tick() behind
// the TYPE_SCRIPTED gate (ahead of the CTF branch). Lazy init dispatches
// on_mode_init once; afterwards: win-latch re-assert, engine respawn timers,
// on_mode_tick, win-latch fold.
void mode_run_tick(GameWorld& world);

// The once-only mode activation (mode_run_tick's step 0, factored so a
// staged lobby world can init before its first tick): latch init_attempted
// FIRST, resolve respawn_ticks from the lobby request, scan team start
// markers into the anchor arrays while consumed markers are still in oblist,
// dispatch on_mode_init, and set active on success. Latch-guarded on
// init_attempted — mode_run_tick's lazy arm calls the SAME function for
// un-staged worlds (unit tests, openglad_text, dedicated levels 2+), so
// init exists exactly once per world whichever site reaches it first. Both
// latch bytes replicate in ModeState, so adoption never re-inits.
void mode_stage_init(GameWorld& world);

// True when this world is a scripted level with a successfully initialized
// mode — the gate every scripted-only engine behavior keys on.
bool mode_scripted_active(const GameWorld& world);

// og.declare_winner backend: on first win-latch arming, flush-revive every
// pending respawn/corpse (respawn_flush_revive_all), THEN compute
// winner_is_player as "any live myguy walker with team_num() == team" (the
// exact CTF win-check semantics) and latch ending 0 with next_level id+1 on
// a player win, id (the rematch shape) otherwise. Last write wins.
void mode_declare_winner(GameWorld& world, int team);

// og.end_level backend: arm (or overwrite) the win latch with an explicit
// ending/next_level pair. Fires the same first-arming revive flush.
void mode_end_level(GameWorld& world, int ending, int next_level);

// Authored team mask for versus maps: teams with at least one start marker
// (Order::Special / FAMILY_RESERVED_TEAM; dead markers included, matching
// the respawn anchor scan).
std::uint8_t authored_team_mask(const GameWorld& world);

// THE one copy of the team-activation clamp rule: authored teams activate in
// index order until the requested count (<= 0 = all authored; otherwise
// clamped to [2, SCORE_TEAM_COUNT]). Shared by the lobby's selectable-team
// mask, the og.effective_team_mask binding, and the VIEW LEVEL preview's
// count-only fallback (a scripted level with no staged world or no
// on_mode_init hook). Deliberately count-only (D29): the roster-aware
// activation rule lives in mode Lua alone (lib/mode_match.lua
// match.activation, consumed by each mode's decide fold at staged init) —
// its one-time C++ twin, roster_effective_team_mask, is deleted (#218).
std::uint8_t effective_team_mask(std::uint8_t authored, int requested) noexcept;

// Canonical color name for a score team ("RED"/"GREEN"/"BLUE"/"YELLOW")
// or an FFA band byte 16-31 (the 16 kFfaRampBases names), matching the
// rendered palette ramps. Default stays YELLOW. ctf_team_color_name
// forwards here.
const char* team_color_name(int team);

// FFA fighter-band ramp table (docs/ffa-design.md §4): color index c (team
// byte kFfaTeamBase+c) -> 8-shade palette ramp base. Thirteen existing good
// ramps plus TEAL/GOLD/SLATE (168/176/184) synthesized into the flat-grey
// palette hole; no window touches the color-cycled 208-231 range.
inline constexpr std::array<unsigned char, 16> kFfaRampBases = {
    40,  56,  72,  88,  104, 120, 136, 152,
    200, 32,  128, 144, 192, 168, 176, 184};

// True for the team identities the scripted scoring/win/HUD surface accepts:
// classic score teams (t < SCORE_TEAM_COUNT) or the FFA fighter band
// [kFfaTeamBase, kFfaTeamBase + kFfaTeamCount) (docs/ffa-design.md §5).
// Callers reject negative t first (every og.* binding guard does).
bool is_scoring_identity(int t);

// Palette ramp base for a team byte: band bytes map through kFfaRampBases;
// everything else keeps the classic team*16+40 formula (uchar wrap).
// walker::query_team_color delegates here (docs/ffa-design.md §4).
unsigned char team_ramp_base(int team);

} // namespace og::sim
