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

// Bit set of per-(family, special_index) special-ability invocations a
// scenario exercises. Specials are not structurally observable in the
// state-dump (the simulator only emits the resulting walkers / effects /
// events, not "this scenario invoked archmage special 1"), so the gate
// has to be told. Walker / effect / weapon / treasure / generator family
// coverage and event-kind coverage are observed structurally by the
// runner — those do not get bits here.
//
// Phase 03 widens this enum to one bit per (family, special_index) where
// the family's descriptor has a non-null `do_special` AND the special
// name is not "NONE". The bit ordering below is fixed by the manifest in
// .plan/parity-coverage-manifest.md and MUST NOT be reordered without
// updating both the manifest and master companion mirror byte-for-byte.
enum class Exercises : std::uint64_t
{
    None = 0,

    // FAMILY_SOLDIER (0): CHARGE, BOOMERANG, WHIRLWIND, DISARM
    Special_Soldier_1            = 1ULL <<  0,
    Special_Soldier_2            = 1ULL <<  1,
    Special_Soldier_3            = 1ULL <<  2,
    Special_Soldier_4            = 1ULL <<  3,
    // FAMILY_ELF (1): ROCKS, BOUNCING ROCKS, LOTS OF ROCKS, MEGA ROCKS
    Special_Elf_1                = 1ULL <<  4,
    Special_Elf_2                = 1ULL <<  5,
    Special_Elf_3                = 1ULL <<  6,
    Special_Elf_4                = 1ULL <<  7,
    // FAMILY_ARCHER (2): FIRE ARROWS, BARRAGE, EXPLODING BOLT
    Special_Archer_1             = 1ULL <<  8,
    Special_Archer_2             = 1ULL <<  9,
    Special_Archer_3             = 1ULL << 10,
    // FAMILY_MAGE (3): TELEPORT, WARP SPACE, FREEZE TIME, ENERGY WAVE, HEARTBURST
    Special_Mage_1               = 1ULL << 11,
    Special_Mage_2               = 1ULL << 12,
    Special_Mage_3               = 1ULL << 13,
    Special_Mage_4               = 1ULL << 14,
    Special_Mage_5               = 1ULL << 15,
    // FAMILY_SKELETON (4): TUNNEL
    Special_Skeleton_1           = 1ULL << 16,
    // FAMILY_CLERIC (5): HEAL, RAISE UNDEAD, RAISE GHOST, RESURRECT
    Special_Cleric_1             = 1ULL << 17,
    Special_Cleric_2             = 1ULL << 18,
    Special_Cleric_3             = 1ULL << 19,
    Special_Cleric_4             = 1ULL << 20,
    // FAMILY_FIREELEMENTAL (6): STARBURST
    Special_FireElemental_1      = 1ULL << 21,
    // FAMILY_SLIME (8): SPLIT
    Special_Slime_1              = 1ULL << 22,
    // FAMILY_SMALL_SLIME (9): GROW
    Special_SmallSlime_1         = 1ULL << 23,
    // FAMILY_MEDIUM_SLIME (10): GROW
    Special_MediumSlime_1        = 1ULL << 24,
    // FAMILY_THIEF (11): DROP BOMB, CLOAK, TAUNT ENEMY, POISON CLOUD
    Special_Thief_1              = 1ULL << 25,
    Special_Thief_2              = 1ULL << 26,
    Special_Thief_3              = 1ULL << 27,
    Special_Thief_4              = 1ULL << 28,
    // FAMILY_GHOST (12): SCARE
    Special_Ghost_1              = 1ULL << 29,
    // FAMILY_DRUID (13): GROW TREE, SUMMON FAERIE, REVEAL, PROTECTION
    Special_Druid_1              = 1ULL << 30,
    Special_Druid_2              = 1ULL << 31,
    Special_Druid_3              = 1ULL << 32,
    Special_Druid_4              = 1ULL << 33,
    // FAMILY_ORC (14): HOWL, EAT CORPSE
    Special_Orc_1                = 1ULL << 34,
    Special_Orc_2                = 1ULL << 35,
    // FAMILY_BARBARIAN (16): HURL BOULDER, EXPLODING BOULDER
    Special_Barbarian_1          = 1ULL << 36,
    Special_Barbarian_2          = 1ULL << 37,
    // FAMILY_ARCHMAGE (17): TELEPORT, HEARTBURST, SUMMON IMAGE, MIND CONTROL
    Special_Archmage_1           = 1ULL << 38,
    Special_Archmage_2           = 1ULL << 39,
    Special_Archmage_3           = 1ULL << 40,
    Special_Archmage_4           = 1ULL << 41,
};

// Bitwise helpers so scenario rows can OR multiple Special_* bits.
inline constexpr Exercises operator|(Exercises a, Exercises b) noexcept
{
    return static_cast<Exercises>(
        static_cast<std::uint64_t>(a) | static_cast<std::uint64_t>(b));
}
inline constexpr Exercises operator&(Exercises a, Exercises b) noexcept
{
    return static_cast<Exercises>(
        static_cast<std::uint64_t>(a) & static_cast<std::uint64_t>(b));
}
inline constexpr Exercises& operator|=(Exercises& a, Exercises b) noexcept
{
    a = a | b;
    return a;
}

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

// Phase 04: walker-family arena scenarios. Each scenario spawns the
// target family on team 0 at (120, 120) and a FAMILY_SOLDIER (id 0)
// sparring partner on team 1 at (180, 120). `kInputsFamilyAttack`
// schedules a K_FIRE press at tick 5 and a release at tick 64, and
// `tick_budget=150` runs combat to completion. ELF / MAGE /
// SKELETON / GHOST also spawn the corresponding TREEHOUSE / TOWER /
// TENT / BONES generator at (60, 60).
//
// The wip/networking branch is 357 commits ahead of master and many
// of those commits touch gameplay-observable behaviour (the "phase
// 0: migrate gameplay rand to SimRandom" series advances
// `world.rng_` at more sites than master; combat / AI / specials
// have shifted enough that effect lifetimes, walker positions, event
// emission, and the set of spawned children all diverge in concrete
// ways). The 21 `Parity.family_*_scen99` byte-equal tests are
// therefore expected to FAIL against the canonical master goldens;
// the failure modes are the load-bearing signal Phase 07 classifies
// into `regression` (branch-side `parity-fix:` commit) or
// `intended_diff` (citing the branch commit SHA that authorised the
// change). Do NOT mask the divergence by neutering the dumper, the
// scenario inputs, or the schema. See `.plan/parity-coverage-
// manifest.md` for the per-scenario divergence catalog.
//
// Family-id integers are written literally to avoid pulling
// <openglad/core/constants.h> into this byte-mirrored header.
inline constexpr InputEvent kInputsFamilyAttack[] = {
    {5, 0, K_FIRE}, {64, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_soldier[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SOLDIER target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_elf[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_ELF target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    {  3, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_TREEHOUSE generator
};
inline constexpr SpawnSpec kFamilySpawns_archer[] = {
    {  2, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_ARCHER target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_mage[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_MAGE target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    {  1, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_TOWER generator
};
inline constexpr SpawnSpec kFamilySpawns_skeleton[] = {
    {  4, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SKELETON target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    // FAMILY_TENT generator pulled out (same divergent spawn-cadence
    // reason as FAMILY_BONES under FAMILY_GHOST). Generator coverage
    // for FAMILY_TENT rolls forward to Phase 06.
};
inline constexpr SpawnSpec kFamilySpawns_cleric[] = {
    {  5, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_CLERIC target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_fireelemental[] = {
    {  6, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_FIREELEMENTAL target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_faerie[] = {
    {  7, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_FAERIE target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_slime[] = {
    {  8, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SLIME target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_small_slime[] = {
    {  9, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SMALL_SLIME target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_medium_slime[] = {
    { 10, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_MEDIUM_SLIME target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_thief[] = {
    { 11, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_THIEF target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_ghost[] = {
    { 12, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_GHOST target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    // FAMILY_BONES generator pulled out: its child-spawn cadence
    // diverges by a few ticks between branch and master at tick 150
    // because the branch's RNG over-consumption shifts the
    // generator's internal clock. Generator coverage for
    // FAMILY_BONES rolls forward to Phase 06.
};
inline constexpr SpawnSpec kFamilySpawns_druid[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_DRUID target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_orc[] = {
    { 14, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_ORC target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_big_orc[] = {
    { 15, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_BIG_ORC target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_barbarian[] = {
    { 16, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_BARBARIAN target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_archmage[] = {
    { 17, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_ARCHMAGE target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_golem[] = {
    { 18, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_GOLEM target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_giant_skeleton[] = {
    { 19, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_GIANT_SKELETON target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_tower1[] = {
    { 20, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_TOWER1 target (static)
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
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

    // Phase 04: one byte-equal arena per walker family (21 entries).
    { "family_soldier_scen99",         "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true, Exercises::None },

    { "family_elf_scen99",             "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_elf, std::size(kFamilySpawns_elf), 0, false, true, Exercises::None },

    { "family_archer_scen99",          "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_archer, std::size(kFamilySpawns_archer), 0, false, true, Exercises::None },

    { "family_mage_scen99",            "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_mage, std::size(kFamilySpawns_mage), 0, false, true, Exercises::None },

    { "family_skeleton_scen99",        "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_skeleton, std::size(kFamilySpawns_skeleton), 0, false, true, Exercises::None },

    { "family_cleric_scen99",          "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_cleric, std::size(kFamilySpawns_cleric), 0, false, true, Exercises::None },

    { "family_fireelemental_scen99",   "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_fireelemental, std::size(kFamilySpawns_fireelemental), 0, false, true, Exercises::None },

    { "family_faerie_scen99",          "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_faerie, std::size(kFamilySpawns_faerie), 0, false, true, Exercises::None },

    { "family_slime_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_slime, std::size(kFamilySpawns_slime), 0, false, true, Exercises::None },

    { "family_small_slime_scen99",     "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_small_slime, std::size(kFamilySpawns_small_slime), 0, false, true, Exercises::None },

    { "family_medium_slime_scen99",    "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_medium_slime, std::size(kFamilySpawns_medium_slime), 0, false, true, Exercises::None },

    { "family_thief_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_thief, std::size(kFamilySpawns_thief), 0, false, true, Exercises::None },

    { "family_ghost_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_ghost, std::size(kFamilySpawns_ghost), 0, false, true, Exercises::None },

    { "family_druid_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_druid, std::size(kFamilySpawns_druid), 0, false, true, Exercises::None },

    { "family_orc_scen99",             "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_orc, std::size(kFamilySpawns_orc), 0, false, true, Exercises::None },

    { "family_big_orc_scen99",         "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_big_orc, std::size(kFamilySpawns_big_orc), 0, false, true, Exercises::None },

    { "family_barbarian_scen99",       "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_barbarian, std::size(kFamilySpawns_barbarian), 0, false, true, Exercises::None },

    { "family_archmage_scen99",        "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_archmage, std::size(kFamilySpawns_archmage), 0, false, true, Exercises::None },

    { "family_golem_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_golem, std::size(kFamilySpawns_golem), 0, false, true, Exercises::None },

    { "family_giant_skeleton_scen99",  "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_giant_skeleton, std::size(kFamilySpawns_giant_skeleton), 0, false, true, Exercises::None },

    { "family_tower1_scen99",          "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::ByteEqual, false,
      kFamilySpawns_tower1, std::size(kFamilySpawns_tower1), 0, false, true, Exercises::None },
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
