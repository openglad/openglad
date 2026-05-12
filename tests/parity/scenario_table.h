// Parity harness scenario table — schema v1.
//
// SYNCHRONIZE WITH ../openglad-master/tools/parity_scenario_table.h
// This header is the single source of truth for the parity scenarios. The
// master companion copies this file byte-for-byte; any change here must be
// mirrored on the companion before the next golden capture.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace og::parity {

// Keymask bits used by scenario input scripts. These are abstract values —
// the parity harness writes them into a flat (tick, player_id, key_mask)
// table and does not interpret them through SDL key codes. Bit positions
// match KEY_UP..KEY_SPECIAL_SWITCH in include/openglad/interface/input.h.
inline constexpr std::uint32_t K_NONE           = 0u;
inline constexpr std::uint32_t K_UP             = 1u << 0;  // KEY_UP
inline constexpr std::uint32_t K_UP_RIGHT       = 1u << 1;
inline constexpr std::uint32_t K_RIGHT          = 1u << 2;  // KEY_RIGHT
inline constexpr std::uint32_t K_DOWN_RIGHT     = 1u << 3;
inline constexpr std::uint32_t K_DOWN           = 1u << 4;  // KEY_DOWN
inline constexpr std::uint32_t K_DOWN_LEFT      = 1u << 5;
inline constexpr std::uint32_t K_LEFT           = 1u << 6;  // KEY_LEFT
inline constexpr std::uint32_t K_UP_LEFT        = 1u << 7;
inline constexpr std::uint32_t K_FIRE           = 1u << 8;  // KEY_FIRE
inline constexpr std::uint32_t K_SPECIAL        = 1u << 9;  // KEY_SPECIAL
inline constexpr std::uint32_t K_SWITCH         = 1u << 10; // KEY_SWITCH
inline constexpr std::uint32_t K_SPECIAL_SWITCH = 1u << 11; // KEY_SPECIAL_SWITCH
// Aliases retained for older scenarios that pre-dated the bit re-layout.
inline constexpr std::uint32_t K_ATTACK         = K_FIRE;

struct InputEvent
{
    std::uint32_t tick;
    std::uint8_t  player_id;
    std::uint32_t key_mask;
};

// Mode of comparison applied after the dump is captured.
enum class CompareMode : std::uint8_t
{
    ByteEqual,   // canonical JSON dump must match the golden byte-for-byte
    Invariant,   // run-time predicate over the dump; no golden compare
};

// Bit set of subsystems / features a scenario exercises. Phase 03 extends
// this enum with the per-subsystem bits; the v1 schema only ships `None`.
enum class Exercises : std::uint64_t
{
    None = 0,
};

// Canonical wrapper helpers for spawn `order` values. Wrap the values defined
// in <openglad/core/order.h>; we keep this header free of that include so the
// byte-for-byte mirror on master does not require pulling in the same
// transitive headers.
inline constexpr std::uint8_t kOrderLiving    = 0;   // Order::Living
inline constexpr std::uint8_t kOrderWeapon    = 1;   // Order::Weapon
inline constexpr std::uint8_t kOrderTreasure  = 2;   // Order::Treasure
inline constexpr std::uint8_t kOrderGenerator = 3;   // Order::Generator
inline constexpr std::uint8_t kOrderFX        = 4;   // Order::FX

// One scripted spawn applied after `level.load()`. Position is in tile-space
// (multiples of GRID_SIZE if you want grid-aligned). team is `team_num`;
// `default_weapon` / `current_weapon` are zero-meaning-skip.
struct SpawnSpec
{
    std::int32_t  family;
    std::uint8_t  team;
    std::uint8_t  order;
    std::int32_t  x;
    std::int32_t  y;
    std::uint16_t default_weapon;
    std::uint16_t current_weapon;
};

struct ScenarioSpec
{
    std::string_view  id;
    std::string_view  scenario_file;
    std::uint32_t     rng_seed;
    const InputEvent* inputs;
    std::size_t       input_count;
    std::uint32_t     tick_budget;
    CompareMode       compare_mode;
    bool              is_branch_internal;
    const SpawnSpec*  spawns;
    std::size_t       spawn_count;
    std::uint8_t      player_team;
    bool              is_intentionally_empty;
    bool              fresh_arena;
    Exercises         exercises;
};

// --- Per-scenario input scripts (constexpr, no file I/O at test time) ---

inline constexpr InputEvent kInputsEmpty[1] = { {0, 0, K_NONE} };

inline constexpr InputEvent kInputsCombatAttack99[] = {
    {5,  0, K_FIRE}, {64, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialOnce20[] = {
    {20, 0, K_SPECIAL}, {21, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialBomb10[] = {
    {10, 0, K_SPECIAL}, {11, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialChain15[] = {
    {15, 0, K_SPECIAL}, {16, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialSummon8[] = {
    {8,  0, K_SPECIAL}, {9,  0, K_NONE},
};

inline constexpr InputEvent kInputsExitWalkRight[] = {
    {0,   0, K_RIGHT},
    {420, 0, K_NONE},
};

inline constexpr InputEvent kInputsScripted9301[] = {
    {0,   0, K_UP},
    {20,  0, K_NONE},
    {40,  0, K_RIGHT},
    {60,  0, K_NONE},
    {80,  0, K_FIRE},
    {100, 0, K_NONE},
};

// --- Spawn lists -----------------------------------------------------------

// Single-walker arena used by smoke_nonempty_scen99{_inputs}. A lone
// FAMILY_SOLDIER (0) is spawned far from the scen99 grid's pre-loaded
// monsters so no immediate combat draws the walker into a busy() state
// that would absorb the input. Scripted K_RIGHT must therefore manifest
// as a real xpos delta in the dump — that is what the Phase 02b verifier
// looks for. A second walker (orc, team 1) is also added so the dump
// shows the not-controlled walker is unaffected by player input.
inline constexpr SpawnSpec kSmokeArenaSpawns[] = {
    // Spawned at the centre of the 16-tile-wide scen99 grid (tile coords
    // 7,7 in 32-pixel tiles) which is a passable interior tile — picked
    // empirically so K_RIGHT can actually step the walker right one tile
    // without hitting the perimeter wall.
    { 0,  0, kOrderLiving, 224, 224, 0, 0 },  // soldier, team 0 (player)
    { 14, 1, kOrderLiving,  64,  64, 0, 0 },  // orc, team 1 (idle, far side)
};

// Smoke "inputs" twin: hold K_RIGHT for ticks 1..20 so the player walker
// moves visibly. Routed through sim_process_player_input → walkstep so the
// resulting xpos delta in the dump is a real simulation response, not a
// harness-side fake. The mask is released at tick 21 so cycle/animation
// state has a clean tail before the dump is captured.
inline constexpr InputEvent kInputsSmokeMoveRight[] = {
    {1,  0, K_RIGHT}, {21, 0, K_NONE},
};

// --- Scenario table --------------------------------------------------------

inline constexpr ScenarioSpec kScenarios[] = {
    { "ai_idle_wander_scen9301",   "scen/scen9301.fss",     0x00000001u,
      nullptr, 0,                                                       300, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "combat_attack_scen99",      "temp/scen/scen99.fss",  0x00000042u,
      kInputsCombatAttack99, std::size(kInputsCombatAttack99),          200, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "special_archmage_scen123",  "temp/scen/scen123.fss", 0x0000F00Du,
      kInputsSpecialOnce20,  std::size(kInputsSpecialOnce20),           200, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "special_cleric_scen124",    "temp/scen/scen124.fss", 0x0000F00Du,
      kInputsSpecialOnce20,  std::size(kInputsSpecialOnce20),           200, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "special_mage_scen126",      "temp/scen/scen126.fss", 0x0000F00Du,
      kInputsSpecialOnce20,  std::size(kInputsSpecialOnce20),           200, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "special_thief_scen789",     "temp/scen/scen789.fss", 0x0000F00Du,
      kInputsSpecialOnce20,  std::size(kInputsSpecialOnce20),           200, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "effect_bomb_lifetime_scen99","temp/scen/scen99.fss", 0x0000BEEFu,
      kInputsSpecialBomb10,  std::size(kInputsSpecialBomb10),           60,  CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "effect_chain_scen9410",     "temp/scen/scen9410.fss",0x0000BEEFu,
      kInputsSpecialChain15, std::size(kInputsSpecialChain15),          100, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "summon_druid_pet_scen950",  "temp/scen/scen950.fss", 0x0000CAFEu,
      kInputsSpecialSummon8, std::size(kInputsSpecialSummon8),          80,  CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "scoring_after_combat_scen99","temp/scen/scen99.fss", 0x00000042u,
      kInputsCombatAttack99, std::size(kInputsCombatAttack99),          200, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "save_roundtrip_scen99",     "temp/scen/scen99.fss",  0x00000123u,
      nullptr, 0,                                                       1,   CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "exit_trigger_scen9302",     "scen/scen9302.fss",     0x00000007u,
      kInputsExitWalkRight, std::size(kInputsExitWalkRight),            600, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "tick_cadence_scen9301",     "scen/scen9301.fss",     0x00000001u,
      nullptr, 0,                                                       600, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "rng_seed_stable_scen99",    "temp/scen/scen99.fss",  0x00000001u,
      nullptr, 0,                                                       1,   CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    { "scripted_input_scen9301",   "scen/scen9301.fss",     0x00000010u,
      kInputsScripted9301,  std::size(kInputsScripted9301),             200, CompareMode::ByteEqual, false,
      nullptr, 0, 0, false, false, Exercises::None },

    // Branch-internal companion: dirty-bit snapshot vs direct iteration.
    // No master golden — runs entirely on the branch side.
    { "snapshot_dirty_bits_scen9301","scen/scen9301.fss",   0x00000055u,
      nullptr, 0,                                                       50,  CompareMode::Invariant, true,
      nullptr, 0, 0, false, false, Exercises::None },

    // Phase 02 smoke scenarios. fresh_arena drops any walkers the loaded
    // scen file may have produced and replaces them with kSmokeArenaSpawns,
    // so the dump can demonstrate a non-empty oblist on both sides without
    // depending on any particular scen99.fss content. The _inputs variant
    // applies K_FIRE briefly so position / keys diverge from the no-input
    // smoke twin.
    { "smoke_nonempty_scen99",         "scen/scen1.fss", 0x00000042u,
      nullptr, 0,                                                       60,  CompareMode::ByteEqual, false,
      kSmokeArenaSpawns, std::size(kSmokeArenaSpawns), 0, false, true, Exercises::None },

    { "smoke_nonempty_scen99_inputs",  "scen/scen1.fss", 0x00000042u,
      kInputsSmokeMoveRight, std::size(kInputsSmokeMoveRight),          60,  CompareMode::ByteEqual, false,
      kSmokeArenaSpawns, std::size(kSmokeArenaSpawns), 0, false, true, Exercises::None },
};

inline constexpr std::size_t kScenarioCount = std::size(kScenarios);

// Number of scenarios that have (or will have) a master-side golden file.
inline constexpr std::size_t kMasterComparableScenarioCount = []() {
    std::size_t n = 0;
    for (const auto& s : kScenarios)
        if (!s.is_branch_internal) ++n;
    return n;
}();

} // namespace og::parity
