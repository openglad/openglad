// Parity harness scenario table — schema v1.
//
// SYNCHRONIZE WITH ../openglad-master/tools/parity_scenario_table.h
// This header is the single source of truth for the parity scenarios. The
// Phase 05 master companion copies this file byte-for-byte; any change here
// must be mirrored on the companion before the next golden capture.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace og::parity {

// Keymask bits used by scenario input scripts. These are abstract values —
// the parity harness writes them into a flat (tick, player_id, key_mask)
// table and does not interpret them through SDL key codes.
inline constexpr std::uint32_t K_NONE     = 0u;
inline constexpr std::uint32_t K_UP       = 1u << 0;
inline constexpr std::uint32_t K_DOWN     = 1u << 1;
inline constexpr std::uint32_t K_LEFT     = 1u << 2;
inline constexpr std::uint32_t K_RIGHT    = 1u << 3;
inline constexpr std::uint32_t K_ATTACK   = 1u << 4;
inline constexpr std::uint32_t K_SPECIAL  = 1u << 5;

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

struct ScenarioSpec
{
    std::string_view id;
    std::string_view scenario_file;
    std::uint32_t    rng_seed;
    const InputEvent* inputs;
    std::size_t      input_count;
    std::uint32_t    tick_budget;
    CompareMode      compare_mode;
    bool             is_branch_internal; // no master golden expected
};

// --- Per-scenario input scripts (constexpr, no file I/O at test time) ---

inline constexpr InputEvent kInputsEmpty[1] = { {0, 0, K_NONE} };

inline constexpr InputEvent kInputsCombatAttack99[] = {
    {5,  0, K_ATTACK}, {64, 0, K_NONE},
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
    {80,  0, K_ATTACK},
    {100, 0, K_NONE},
};

// --- Scenario table (15 master-comparable + 1 branch-internal = 16) ---

inline constexpr ScenarioSpec kScenarios[] = {
    { "ai_idle_wander_scen9301",   "scen/scen9301.fss",     0x00000001u,
      nullptr, 0,                                                       300, CompareMode::ByteEqual, false },

    { "combat_attack_scen99",      "temp/scen/scen99.fss",  0x00000042u,
      kInputsCombatAttack99, std::size(kInputsCombatAttack99),          200, CompareMode::ByteEqual, false },

    { "special_archmage_scen123",  "temp/scen/scen123.fss", 0x0000F00Du,
      kInputsSpecialOnce20,  std::size(kInputsSpecialOnce20),           200, CompareMode::ByteEqual, false },

    { "special_cleric_scen124",    "temp/scen/scen124.fss", 0x0000F00Du,
      kInputsSpecialOnce20,  std::size(kInputsSpecialOnce20),           200, CompareMode::ByteEqual, false },

    { "special_mage_scen126",      "temp/scen/scen126.fss", 0x0000F00Du,
      kInputsSpecialOnce20,  std::size(kInputsSpecialOnce20),           200, CompareMode::ByteEqual, false },

    { "special_thief_scen789",     "temp/scen/scen789.fss", 0x0000F00Du,
      kInputsSpecialOnce20,  std::size(kInputsSpecialOnce20),           200, CompareMode::ByteEqual, false },

    { "effect_bomb_lifetime_scen99","temp/scen/scen99.fss", 0x0000BEEFu,
      kInputsSpecialBomb10,  std::size(kInputsSpecialBomb10),           60,  CompareMode::ByteEqual, false },

    { "effect_chain_scen9410",     "temp/scen/scen9410.fss",0x0000BEEFu,
      kInputsSpecialChain15, std::size(kInputsSpecialChain15),          100, CompareMode::ByteEqual, false },

    { "summon_druid_pet_scen950",  "temp/scen/scen950.fss", 0x0000CAFEu,
      kInputsSpecialSummon8, std::size(kInputsSpecialSummon8),          80,  CompareMode::ByteEqual, false },

    { "scoring_after_combat_scen99","temp/scen/scen99.fss", 0x00000042u,
      kInputsCombatAttack99, std::size(kInputsCombatAttack99),          200, CompareMode::ByteEqual, false },

    { "save_roundtrip_scen99",     "temp/scen/scen99.fss",  0x00000123u,
      nullptr, 0,                                                       1,   CompareMode::ByteEqual, false },

    { "exit_trigger_scen9302",     "scen/scen9302.fss",     0x00000007u,
      kInputsExitWalkRight, std::size(kInputsExitWalkRight),            600, CompareMode::ByteEqual, false },

    { "tick_cadence_scen9301",     "scen/scen9301.fss",     0x00000001u,
      nullptr, 0,                                                       600, CompareMode::ByteEqual, false },

    { "rng_seed_stable_scen99",    "temp/scen/scen99.fss",  0x00000001u,
      nullptr, 0,                                                       1,   CompareMode::ByteEqual, false },

    { "scripted_input_scen9301",   "scen/scen9301.fss",     0x00000010u,
      kInputsScripted9301,  std::size(kInputsScripted9301),             200, CompareMode::ByteEqual, false },

    // Branch-internal companion: dirty-bit snapshot vs direct iteration.
    // No master golden — runs entirely on the branch side.
    { "snapshot_dirty_bits_scen9301","scen/scen9301.fss",   0x00000055u,
      nullptr, 0,                                                       50,  CompareMode::Invariant, true },
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
