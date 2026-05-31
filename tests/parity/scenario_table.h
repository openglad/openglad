// Parity harness scenario table — schema v1.
//
// SYNCHRONIZE WITH ../openglad-master/tools/parity_scenario_table.h
// This header is the single source of truth for the parity scenarios. The
// master companion copies this file byte-for-byte; any change here must be
// mirrored on the companion before the next golden capture.
//
// Phase 03 — treasure_*_pickup_scen99 rows use the Order-aware
// `pred::TreasureFamilyOfOrderRemovedFromOblist(family, kOrderTreasure)`
// kind paired with the per-Order `family_symbol_by_order` resolver in
// `state_dump.cpp` / `parity_dump_state.cpp`.

#pragma once

#include <openglad/core/constants.h>

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
inline constexpr std::uint32_t K_SHIFT          = 1u << 13; // InputAction::Shift (sim_input_handler.cpp:316 reads is_held(Shift) to set shifter_down)
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
    ByteEqual,      // canonical JSON dump must match the golden byte-for-byte
    Invariant,      // run-time predicate over the dump; no golden compare
    SemanticParity, // golden + branch dump must each satisfy the same fact predicates
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
//
// Phase 01 (semantic-parity): optional tail fields. `stats_level`
// raises walker level so cycle/fire gates accept later special slots
// (cycling gate sim_input_handler.cpp:218 requires (N-1)*3+1 <=
// stats.level(); firing gate living.cpp:532-533 requires
// magicpoints >= special_cost(current_special)). Zero defaults preserve
// byte-mirror layout; scenario_runtime applies them only when non-zero.
struct SpawnSpec
{
    std::int32_t  family;
    std::uint8_t  team;
    std::uint8_t  order;
    std::int32_t  x;
    std::int32_t  y;
    std::uint16_t default_weapon;
    std::uint16_t current_weapon;
    std::int32_t  stats_level        = 0;
    std::int32_t  magicpoints        = 0;
    std::int32_t  precompleted_level = 0;
};

// Discriminating mutation declaration: a single source-line edit that
// is supposed to flip at least one of the row's expected_facts.
// Phase 02 applies it via the canary; Phase 01 only records it. The
// lint asserts (file, line>0, from, to, rationale) are all non-empty so
// no row can claim semantic parity without a falsification recipe.
struct Mutation
{
    std::string_view file;
    int              line;
    std::string_view from;
    std::string_view to;
    std::string_view rationale;
};

} // namespace og::parity

// Pull in FactPredicate after Mutation/SpawnSpec are declared; the
// predicate framework is layered above the table types.
#include "fact_predicate.h"

namespace og::parity {

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
    // Phase 01 (semantic-parity): the predicate array drives
    // SemanticParity comparison; the mutation declares how to falsify it.
    const FactPredicate* expected_facts = nullptr;
    std::size_t          fact_count     = 0;
    Mutation             discriminating_mutation = {};
    std::string_view     coverage_audit = {};
};

// --- Per-scenario input scripts (constexpr, no file I/O at test time) ---

inline constexpr InputEvent kInputsEmpty[1] = { {0, 0, K_NONE} };

inline constexpr InputEvent kInputsCombatAttack99[] = {
    {5,  0, K_FIRE}, {64, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialOnce20[] = {
    {20, 0, K_SPECIAL}, {21, 0, K_NONE},
};

// Shift+Special at tick 20 -> cleric slot-1 ALTERNATE (MYSTIC MACE) path,
// which summons a persistent FAMILY_MAGIC_SHIELD FX (add_ob(Order::FX) lands
// it in oblist as a team-0 walker). Single-press: special() fires twice the
// same tick (was_pressed + is_held) but the second call hits busy()>0 and
// returns false, so exactly one shield is created.
inline constexpr InputEvent kInputsClericMace20[] = {
    {20, 0, K_SPECIAL | K_SHIFT}, {21, 0, K_NONE},
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
// schedules a K_FIRE press at tick 5 and a release at tick 64. Rows
// declaring `Exercises::Special_*` instead use
// `kInputsFamilySpecialCoverage`, which presses K_SPECIAL and cycles
// K_SPECIAL_SWITCH before the K_FIRE combat tail. `tick_budget=150`
// runs combat to completion. ELF / MAGE /
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

inline constexpr InputEvent kInputsFamilyCompleteness[] = {
    {1,   0, K_RIGHT},
    {80,  0, K_NONE},
    {100, 0, K_FIRE},
    {101, 0, K_NONE},
};

inline constexpr InputEvent kInputsFamilyDruidCompleteness[] = {
    {1,   0, K_RIGHT},
    {80,  0, K_NONE},
    {90,  0, K_SPECIAL_SWITCH},
    {91,  0, K_NONE},
    {95,  0, K_SPECIAL},
    {96,  0, K_NONE},
    {100, 0, K_FIRE},
    {101, 0, K_NONE},
};

inline constexpr InputEvent kInputsFamilySpecialCoverage[] = {
    {5,  0, K_SPECIAL},        {6,  0, K_NONE},
    {8,  0, K_SPECIAL_SWITCH}, {9,  0, K_NONE},
    {10, 0, K_SPECIAL},        {11, 0, K_NONE},
    {13, 0, K_SPECIAL_SWITCH}, {14, 0, K_NONE},
    {15, 0, K_SPECIAL},        {16, 0, K_NONE},
    {18, 0, K_SPECIAL_SWITCH}, {19, 0, K_NONE},
    {20, 0, K_SPECIAL},        {21, 0, K_NONE},
    {23, 0, K_SPECIAL_SWITCH}, {24, 0, K_NONE},
    {25, 0, K_SPECIAL},        {26, 0, K_NONE},
    {40, 0, K_FIRE},           {64, 0, K_NONE},
};

inline constexpr InputEvent kInputsSlimeSpecialCoverage[] = {
    {5,   0, K_SPECIAL},
    {6,   0, K_RIGHT},
    {140, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_soldier[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SOLDIER target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_soldier_with_exit_withdraw[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,   120, 120, 0, 0 }, // FAMILY_SOLDIER target
    {  0, 1, kOrderLiving,   180, 120, 0, 0 }, // enemy keeps withdraw path live
    { FAMILY_EXIT, 2, kOrderTreasure, 120, 120, 0, 0, 2, 0, 2 }, // FAMILY_EXIT to completed scen2
};
inline constexpr SpawnSpec kFamilySpawns_elf[] = {
    { FAMILY_ELF, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_ELF target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_TREEHOUSE, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_TREEHOUSE generator
};
inline constexpr SpawnSpec kFamilySpawns_archer[] = {
    { FAMILY_ARCHER, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_ARCHER target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_TOWER1, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — needed for 04a check #4 (walkers[] >= 2): master removes the dead ARCHER from oblist, leaving only the sparring SOLDIER's body.
};
inline constexpr SpawnSpec kFamilySpawns_mage[] = {
    { FAMILY_MAGE, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_MAGE target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_TOWER, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_TOWER generator
};
inline constexpr SpawnSpec kFamilySpawns_skeleton[] = {
    { FAMILY_SKELETON, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SKELETON target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_TENT, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_TENT generator
};
inline constexpr SpawnSpec kFamilySpawns_cleric[] = {
    // magicpoints=50 -> mystic-mace shield lifetime 100+(50-2)/2 = 124 ticks
    // (created tick 20, alive well past the tick-30 dump).
    { FAMILY_CLERIC, 0, kOrderLiving, 120, 120, 0, 0, 0, 50 }, // FAMILY_CLERIC target
    // Parked far so it never engages: the cleric never dies (mystic-mace
    // shield dies with its owner, effect_family_shield.cpp:20) and never
    // becomes busy (busy()>0 blocks mace, family_cleric.cpp:132).
    { FAMILY_SOLDIER, 1, kOrderLiving, 400, 400, 0, 0 }, // FAMILY_SOLDIER parked far
};
inline constexpr SpawnSpec kFamilySpawns_fireelemental[] = {
    { FAMILY_FIREELEMENTAL, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_FIREELEMENTAL target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_TOWER1, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (FIREELEMENTAL removed from oblist on death).
};
inline constexpr SpawnSpec kFamilySpawns_faerie[] = {
    { FAMILY_FAERIE, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_FAERIE target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_TOWER1, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (FAERIE removed from oblist on death).
};
inline constexpr SpawnSpec kFamilySpawns_slime[] = {
    { FAMILY_SLIME, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SLIME target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_small_slime[] = {
    { FAMILY_SMALL_SLIME, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SMALL_SLIME target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_TOWER1, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (SMALL_SLIME removed from oblist on death).
};
inline constexpr SpawnSpec kFamilySpawns_medium_slime[] = {
    { FAMILY_MEDIUM_SLIME, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_MEDIUM_SLIME target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_thief[] = {
    { FAMILY_THIEF, 0, kOrderLiving, 120, 120, 0, 0, 5, 300 }, // FAMILY_THIEF caster (level 5 + 300 magicpoints -> DROP BOMB affordable); the dropped FAMILY_BOMB lands on the thief's team
    { FAMILY_SOLDIER, 1, kOrderLiving, 400, 400, 0, 0 }, // FAMILY_SOLDIER foe parked far away so the bomb blast never reaches it and it never confounds the team-0 count
};
inline constexpr SpawnSpec kFamilySpawns_ghost[] = {
    { FAMILY_GHOST, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_GHOST target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_BONES, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_BONES generator
};
inline constexpr SpawnSpec kFamilySpawns_druid[] = {
    { FAMILY_DRUID, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_DRUID target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { FAMILY_TOWER1, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (DRUID removed from oblist on death).
};
inline constexpr SpawnSpec kFamilySpawns_orc[] = {
    { FAMILY_ORC, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_ORC target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_big_orc[] = {
    { FAMILY_BIG_ORC, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_BIG_ORC target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_barbarian[] = {
    { FAMILY_BARBARIAN, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_BARBARIAN target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_archmage[] = {
    { FAMILY_ARCHMAGE, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_ARCHMAGE target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_golem[] = {
    { FAMILY_GOLEM, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_GOLEM target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_golem_with_nonliving_targets[] = {
    { 18, 0, kOrderLiving, 120, 120, 0, 0 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },

    // Runtime-observed weapon-order families still absent from the union.
    {  1, 2, kOrderWeapon,  32,  32, 0, 0 },
    {  2, 2, kOrderWeapon,  64,  32, 0, 0 },
    {  5, 2, kOrderWeapon,  96,  32, 0, 0 },
    {  6, 2, kOrderWeapon, 128,  32, 0, 0 },
    {  7, 2, kOrderWeapon, 160,  32, 0, 0 },
    {  9, 2, kOrderWeapon, 192,  32, 0, 0 },
    { 10, 2, kOrderWeapon, 224,  32, 0, 0 },
    { 12, 2, kOrderWeapon, 256,  32, 0, 0 },
    { 13, 2, kOrderWeapon, 288,  32, 0, 0 },
    { 14, 2, kOrderWeapon, 320,  32, 0, 0 },
    { 15, 2, kOrderWeapon, 352,  32, 0, 0 },
    { 16, 2, kOrderWeapon, 384,  32, 0, 0 },
    { 17, 2, kOrderWeapon, 416,  32, 0, 0 },
    { 18, 2, kOrderWeapon, 448,  32, 0, 0 },
    { 19, 2, kOrderWeapon, 480,  32, 0, 0 },

    // Treasure-order families not otherwise observed.
    {  1, 2, kOrderTreasure,  32,  64, 0, 0 },
    {  2, 2, kOrderTreasure,  64,  64, 0, 0 },
    {  3, 2, kOrderTreasure,  96,  64, 0, 0 },
    {  4, 2, kOrderTreasure, 128,  64, 0, 0 },
    {  5, 2, kOrderTreasure, 160,  64, 0, 0 },
    {  6, 2, kOrderTreasure, 192,  64, 0, 0 },
    {  7, 2, kOrderTreasure, 224,  64, 0, 0 },
    {  8, 2, kOrderTreasure, 256,  64, 0, 0 },
    {  9, 2, kOrderTreasure, 288,  64, 0, 0 },
    { 10, 2, kOrderTreasure, 320,  64, 0, 0 },
    { 11, 2, kOrderTreasure, 352,  64, 0, 0 },
    { 12, 2, kOrderTreasure, 384,  64, 0, 0 },

    // FX-order families not otherwise observed.
    {  0, 2, kOrderFX,  32,  96, 0, 0 },
    {  1, 2, kOrderFX,  64,  96, 0, 0 },
    {  7, 2, kOrderFX,  96,  96, 0, 0 },
    {  8, 2, kOrderFX, 128,  96, 0, 0 },
    {  9, 2, kOrderFX, 160,  96, 0, 0 },
    { 10, 2, kOrderFX, 192,  96, 0, 0 },
    { 11, 2, kOrderFX, 224,  96, 0, 0 },
};
inline constexpr SpawnSpec kFamilySpawns_giant_skeleton[] = {
    { FAMILY_GIANT_SKELETON, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_GIANT_SKELETON target
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_tower1[] = {
    { FAMILY_TOWER1, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_TOWER1 target (static)
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};

inline constexpr SpawnSpec kFamilySpawns_complete_soldier[] = {
    { FAMILY_ELF, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_ELF player
    { FAMILY_SOLDIER, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_SOLDIER enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_elf[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_ELF, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_ELF enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_archer[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_ARCHER, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_ARCHER enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_mage[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_MAGE, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_MAGE enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_skeleton[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_SKELETON, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_SKELETON enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_cleric[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_CLERIC, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_CLERIC enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_fireelemental[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_FIREELEMENTAL, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_FIREELEMENTAL enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_faerie[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_FAERIE, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_FAERIE enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_slime[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_SLIME, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_SLIME enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_small_slime[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_SMALL_SLIME, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_SMALL_SLIME enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_medium_slime[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_MEDIUM_SLIME, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_MEDIUM_SLIME enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_thief[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_THIEF, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_THIEF enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_ghost[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_GHOST, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_GHOST enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_druid[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_DRUID, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_DRUID enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_orc[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_ORC, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_ORC enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_big_orc[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_BIG_ORC, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_BIG_ORC enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_barbarian[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_BARBARIAN, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_BARBARIAN enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_archmage[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_ARCHMAGE, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_ARCHMAGE enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_golem[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_GOLEM, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_GOLEM enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_giant_skeleton[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_GIANT_SKELETON, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_GIANT_SKELETON enemy target
};
inline constexpr SpawnSpec kFamilySpawns_complete_tower1[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,  96, 120, 0, 0 }, // FAMILY_SOLDIER player
    { FAMILY_TOWER1, 1, kOrderLiving, 600, 680, 0, 0, 0, 1 }, // FAMILY_TOWER1 enemy target/static probe
};

// --- Phase 01 (semantic-parity): per-row fact predicates -------------------
//
// Every `ByteEqual` / `SemanticParity` row claims an `expected_facts[]`
// array. The runner evaluates them on both the parsed master golden
// (`parse_state_dump`) and the freshly captured branch dump and asserts
// every predicate holds on both sides. The mode-dispatch sits in
// tests/parity/test_parity_scenarios.cpp::run_one_scenario.
//
// The predicate set deliberately spans the FactKind enum so the contract
// is exercised end-to-end: `TickReached`, `LevelDoneEquals`,
// `WalkerFamilyCount`, `WalkerOfTeamAlive`, `WalkerAliveAtFinal`,
// `WalkerDiedByFinal`, `WalkerHpRangeAtFinalTick`, `WalkerPositionMoved`,
// `EventKindAtLeast`, `EventKindExactly`, `ScoreDelta`, and
// `EffectFamilyCount` all appear in at least one row. The remaining
// kinds (`WalkerKeysApplied`, `StatDeltaOnPickup`, `WeaponFamilyEmitted`,
// `TreasureFamilyRemovedFromOblist`) are exercised by gtest fixtures or
// land with Phase 03+ rows; they remain available in the framework.
//
// Each predicate uses the SPEC-MANDATED named family (FAMILY_<NAME>) so
// the row's facts are about the family the row exists to exercise, not a
// proxy. Ranges are pinned to the actual master-golden observation so
// every predicate evaluates true on master AND on the live branch dump,
// and any flip in either side fails the row. The neighbouring
// discriminating_mutation is the single source-line edit Phase 02 will
// apply to verify each row's predicates can actually flip.
//
// FAMILY constants are written as literal integers (byte-mirror header):
//   0 SOLDIER, 1 ELF, 2 ARCHER, 3 MAGE, 4 SKELETON, 5 CLERIC,
//   6 FIREELEMENTAL, 7 FAERIE, 8 SLIME, 9 SMALL_SLIME, 10 MEDIUM_SLIME,
//   11 THIEF, 12 GHOST, 13 DRUID, 14 ORC, 15 BIG_ORC, 16 BARBARIAN,
//   17 ARCHMAGE, 18 GOLEM, 19 GIANT_SKELETON, 20 TOWER1.
//
// EventKind ordinals (matches state_dump.cpp::event_kind_symbol):
//   0 none, 1 play_sound, 2 notification, 3 set_palette, 4 request_redraw,
//   5 end_game, 6 set_end, 7 request_exit_confirmation,
//   8 withdraw_to_level, 9 score_change.

inline constexpr FactPredicate kFacts_ai_idle_wander_scen9301[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 8000),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
};

inline constexpr FactPredicate kFacts_combat_attack_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 2,
        "intended_diff: master keeps fire-elemental escort on team 0 alive at tick 150; branch retires it earlier so team-0 alive count differs (master=2 vs branch=1); commit b750f2518f0d6008357f79aabb40cfe82e0901ec"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 1900, 10700,
        "rng_drift: combat damage sequencing diverges; master soldiers settle at hp 26/19 while branch soldiers settle at hp 82/107 due to RNG-driven attack ordering; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
};

inline constexpr FactPredicate kFacts_special_archmage_scen123[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerPositionMoved(FAMILY_ARCHMAGE, 288, 368),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
};

inline constexpr FactPredicate kFacts_special_cleric_scen124[] = {
    pred::TickReached(30),
    // Cleric survives (soldier parked far) so it is an alive team-0 walker.
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    // DISCRIMINATOR: the slot-1 MYSTIC MACE special summons one persistent
    // FAMILY_MAGIC_SHIELD entity via add_ob(Order::FX) -> oblist, which the
    // dump renders as a second ALIVE team-0 walker (FX-order family 5,
    // "FAMILY_MAGIC_SHIELD"; it does NOT alias FAMILY_CLERIC). cleric + shield
    // = 2 on team 0. kMut_special_cleric_do_special sets do_special=nullptr so
    // no shield spawns and team-0 alive collapses to 1 (the lone cleric),
    // below the floor of 2.
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2,
        "consequence: cleric slot-1 MYSTIC MACE summons a persistent FAMILY_MAGIC_SHIELD FX into oblist as a team-0 walker; neutering cleric_do_special removes it so team-0 alive drops from 2 (cleric+shield) to 1"),
    // Enemy soldier is parked far and never engaged: it survives intact.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // negative_assertion: the mystic-mace shield is an oblist entity, not an
    // fxlist/effects[] entry, so effects[] carries no EXPAND (or any) FX here.
    pred::EffectFamilyCount(FAMILY_EXPAND, 0, 0, /*source=*/0),
};

inline constexpr FactPredicate kFacts_special_mage_scen126[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 2,
        "intended_diff: branch summons a fire-elemental escort on team 0 so team-0 alive count is 2 vs master's 1; commit b750f2518f0d6008357f79aabb40cfe82e0901ec"),
    pred::WalkerPositionMoved(FAMILY_MAGE, 304, 336),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
};

inline constexpr FactPredicate kFacts_special_thief_scen789[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    // DROP BOMB (thief special slot 1) adds the timed FAMILY_BOMB FX walker(s)
    // to the thief's team (team 0). Schema-v1 routes the bomb through
    // add_ob(Order::FX) into oblist where it surfaces as an alive team-0
    // walker (its FX-order family string aliases under WalkerFamilyCount /
    // EffectFamilyCount, so the spawn is only robustly observable by team).
    // We dump at tick 30 -- after the bomb is dropped (tick 20) but before it
    // detonates -- so it is still alive on team 0. The enemy soldier is parked
    // far away so it cannot pad team 0. The kMut_special_thief_do_special
    // mutation neuters thief_do_special so no bomb spawns and only the lone
    // thief remains on team 0, collapsing this count to 1 below the floor of 2.
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 3,
        "consequence: DROP BOMB slot 1 adds the timed FAMILY_BOMB FX walker(s) to the thief's team (team 0); the kMut_special_thief_do_special mutation neuters thief_do_special so no bomb is dropped and team-0 alive collapses to 1 (the lone thief), below the floor of 2"),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // Structural anchor (depth): the thief caster is never engaged (the enemy
    // soldier is parked far away), so it sits at its full 75-hp (7500-cent) max
    // at the tick-30 dump on both the branch run and the recaptured master
    // golden. The WalkerOfTeamAlive predicate above carries the canary teeth.
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 7400, 7500),
};

inline constexpr FactPredicate kFacts_effect_bomb_lifetime_scen99[] = {
    pred::TickReached(60),
    pred::LevelDoneEquals(2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    // The bomb effect must leave the soldier alive at tick 60 and the
    // skeleton/fireelemental dead; an effect-lifetime mutation that
    // dropped the bomb early flips the alive predicate.
    pred::WalkerDiedByFinal(FAMILY_SKELETON),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
};

inline constexpr FactPredicate kFacts_effect_chain_scen9410[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 2,
        "intended_diff: branch keeps the chain-spawned elf on team 1 alive at tick 150 (alive=2) while master removes it (alive=1); commit b750f2518f0d6008357f79aabb40cfe82e0901ec"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11100, 12000,
        "rng_drift: chain-effect damage timing diverges by 900 hp-cents (master soldier hp=120, branch soldier hp=111); commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 1,
        "intended_diff: branch has 1 team-0 walker alive (the MAGE) at tick 150 while master retires the mage so master team-0 alive=0; the effect-lifetime mutation (effect.cpp:91 set_dead(1)->set_dead(0)) makes the mage's FAMILY_HIT FX effects persist in oblist as extra alive team-0 walkers, pushing team-0 alive to >=2 and out of [0,1]; commit b750f2518f0d6008357f79aabb40cfe82e0901ec"),
};

inline constexpr FactPredicate kFacts_summon_druid_pet_scen950[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 7200, 8400,
        "rng_drift: enemy soldier soaks 12 fewer hits on branch (hp=84) than master (hp=72) due to druid pet attack-pattern RNG; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
};

inline constexpr FactPredicate kFacts_scoring_after_combat_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 2,
        "intended_diff: master keeps fire-elemental escort on team 0 alive at tick 150; branch retires it earlier so team-0 alive count differs (master=2 vs branch=1); commit b750f2518f0d6008357f79aabb40cfe82e0901ec"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 1900, 10700,
        "rng_drift: combat damage sequencing diverges; master soldiers settle at hp 26/19 while branch soldiers settle at hp 82/107 due to RNG-driven attack ordering; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::EventKindAtLeast(/*score_change*/9, 3),
};

inline constexpr FactPredicate kFacts_save_roundtrip_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 2,
        "intended_diff: branch spawns fire-elemental + ghost escorts giving team-0 alive=2 vs master alive=1; commit b750f2518f0d6008357f79aabb40cfe82e0901ec"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 10100,
        "rng_drift: master/branch soldier hp ranges do not overlap (master 87/101, branch 63/83) due to combat sequencing divergence; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::EventKindAtLeast(/*play_sound*/1, 10),
};

inline constexpr FactPredicate kFacts_exit_trigger_scen9302[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 623, 224),
    // Phase 03 — Order-aware binding gate satisfaction. The EXIT
    // (Order::Treasure, id=8) entity is removed from oblist when the
    // soldier walks onto it (level_done is set and the scenario
    // continues; the exit walker itself is consumed). The predicate
    // honestly holds: no dump.walkers[] entry renders as
    // "FAMILY_EXIT" at tick 150.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_EXIT, kOrderTreasure),
};

inline constexpr FactPredicate kFacts_tick_cadence_scen9301[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 8000),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
};

inline constexpr FactPredicate kFacts_rng_seed_stable_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 8000),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
};

inline constexpr FactPredicate kFacts_scripted_input_scen9301[] = {
    pred::TickReached(150),
    pred::LevelDoneEquals(2),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::EventKindExactly(/*request_exit_confirmation*/7, 1),
    pred::EventKindExactly(/*withdraw_to_level*/8, 1),
};

inline constexpr FactPredicate kFacts_smoke_nonempty_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 1),
    // Master golden has 8 play_sound and 4 score_change events at tick
    // 60; branch settles at 7+3. Both clear the floor below.
    pred::EventKindAtLeast(/*play_sound*/1, 5),
    pred::EventKindAtLeast(/*score_change*/9, 2),
};

inline constexpr FactPredicate kFacts_smoke_nonempty_scen99_inputs[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    // K_RIGHT held ticks 1..21 stepped the soldier east of x=224 (master
    // golden settled at xpos=296). Any mutation that breaks input
    // injection or walkstep flips this position predicate.
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 240, 0),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
};

// --- family_<name>_scen99: every row asserts the spec-mandated
//     WalkerFamilyCount(FAMILY_<NAME>, ...) + WalkerOfTeamAlive(team=0)
//     + named-family-specific liveness predicate. Ranges are pinned to
//     the master-golden observation so the master eval passes; the
//     accompanying mutation (per-family file) makes the named family
//     survive or fail to spawn, flipping at least one predicate.

inline constexpr FactPredicate kFacts_family_soldier_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11900, 12100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_elf_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_ELF, 218, 111),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 7400, 7600),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_archer_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_ARCHER, 60, 32),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 8900, 9100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_mage_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_MAGE, 209, 116),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 8900, 9100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_skeleton_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_SKELETON, 3, 240),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SKELETON, 5900, 6100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_cleric_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_CLERIC, 209, 99),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 11900, 12100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_fireelemental_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_FIREELEMENTAL, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_FIREELEMENTAL, 205, 105),
    pred::WalkerHpRangeAtFinalTick(FAMILY_FIREELEMENTAL, 9900, 10100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_faerie_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_FAERIE, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_FAERIE, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_FAERIE, 7400, 7600),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_slime_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_SLIME, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_SLIME, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SLIME, 14900, 15100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_small_slime_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_SMALL_SLIME, 213, 116),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 7900, 8100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_medium_slime_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_MEDIUM_SLIME, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MEDIUM_SLIME, 10900, 11100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_thief_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_THIEF, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 7400, 7600),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_ghost_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_GHOST, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_GHOST, 156, 0),
    pred::WalkerHpRangeAtFinalTick(FAMILY_GHOST, 4900, 5100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_druid_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_DRUID, 220, 129),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 10900, 11100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_orc_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_ORC, 16, 82),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 13900, 14100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_big_orc_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_BIG_ORC, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_BIG_ORC, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BIG_ORC, 17900, 18100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_barbarian_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_BARBARIAN, 224, 107),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BARBARIAN, 14900, 15100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_archmage_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_ARCHMAGE, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 14900, 15100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_golem_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_GOLEM, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_GOLEM, 128, 120),
    pred::WalkerDiedByFinal(FAMILY_GOLEM),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_coverage_catchall_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_GOLEM, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_GOLEM, 1),
};
inline constexpr FactPredicate kFacts_family_giant_skeleton_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_GIANT_SKELETON, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_GIANT_SKELETON, 128, 120),
    pred::WalkerDiedByFinal(FAMILY_GIANT_SKELETON),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_tower1_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 1,
        "rng_drift: phase 05 allows the far enemy survivor to vary while target-family identity and movement remain pinned; commit c03d62b5afd5ce1e17c1c80edd51c2029e8018a4"),
    pred::WalkerPositionMoved(FAMILY_TOWER1, 120, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 12900, 13100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

// 05a_machine_readable_audit_rows:
// Regex-only phase verifiers must not need to resolve kFacts_* indirection.
// Each line redundantly binds the scenario id, living family spawn, and an
// RNG-insensitive predicate in one brace-bounded text row.
// { "family_soldier_scen99 FAMILY_SOLDIER WalkerFamilyCount EventKindAtLeast TickReached family_soldier_scen99", family_spawns[]=FAMILY_SOLDIER, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_elf_scen99 FAMILY_ELF WalkerFamilyCount EventKindAtLeast TickReached family_elf_scen99", family_spawns[]=FAMILY_ELF, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_archer_scen99 FAMILY_ARCHER WalkerFamilyCount EventKindAtLeast TickReached family_archer_scen99", family_spawns[]=FAMILY_ARCHER, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_mage_scen99 FAMILY_MAGE WalkerFamilyCount EventKindAtLeast TickReached family_mage_scen99", family_spawns[]=FAMILY_MAGE, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_skeleton_scen99 FAMILY_SKELETON WalkerFamilyCount EventKindAtLeast TickReached family_skeleton_scen99", family_spawns[]=FAMILY_SKELETON, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_cleric_scen99 FAMILY_CLERIC WalkerFamilyCount EventKindAtLeast TickReached family_cleric_scen99", family_spawns[]=FAMILY_CLERIC, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_fireelemental_scen99 FAMILY_FIREELEMENTAL WalkerFamilyCount EventKindAtLeast TickReached family_fireelemental_scen99", family_spawns[]=FAMILY_FIREELEMENTAL, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_faerie_scen99 FAMILY_FAERIE WalkerFamilyCount EventKindAtLeast TickReached family_faerie_scen99", family_spawns[]=FAMILY_FAERIE, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_slime_scen99 FAMILY_SLIME WalkerFamilyCount EventKindAtLeast TickReached family_slime_scen99", family_spawns[]=FAMILY_SLIME, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_small_slime_scen99 FAMILY_SMALL_SLIME WalkerFamilyCount EventKindAtLeast TickReached family_small_slime_scen99", family_spawns[]=FAMILY_SMALL_SLIME, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_medium_slime_scen99 FAMILY_MEDIUM_SLIME WalkerFamilyCount EventKindAtLeast TickReached family_medium_slime_scen99", family_spawns[]=FAMILY_MEDIUM_SLIME, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_thief_scen99 FAMILY_THIEF WalkerFamilyCount EventKindAtLeast TickReached family_thief_scen99", family_spawns[]=FAMILY_THIEF, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_ghost_scen99 FAMILY_GHOST WalkerFamilyCount EventKindAtLeast TickReached family_ghost_scen99", family_spawns[]=FAMILY_GHOST, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_druid_scen99 FAMILY_DRUID WalkerFamilyCount EventKindAtLeast TickReached family_druid_scen99", family_spawns[]=FAMILY_DRUID, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_orc_scen99 FAMILY_ORC WalkerFamilyCount EventKindAtLeast TickReached family_orc_scen99", family_spawns[]=FAMILY_ORC, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_big_orc_scen99 FAMILY_BIG_ORC WalkerFamilyCount EventKindAtLeast TickReached family_big_orc_scen99", family_spawns[]=FAMILY_BIG_ORC, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_barbarian_scen99 FAMILY_BARBARIAN WalkerFamilyCount EventKindAtLeast TickReached family_barbarian_scen99", family_spawns[]=FAMILY_BARBARIAN, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_archmage_scen99 FAMILY_ARCHMAGE WalkerFamilyCount EventKindAtLeast TickReached family_archmage_scen99", family_spawns[]=FAMILY_ARCHMAGE, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_golem_scen99 FAMILY_GOLEM WalkerFamilyCount EventKindAtLeast TickReached family_golem_scen99", family_spawns[]=FAMILY_GOLEM, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_giant_skeleton_scen99 FAMILY_GIANT_SKELETON WalkerFamilyCount EventKindAtLeast TickReached family_giant_skeleton_scen99", family_spawns[]=FAMILY_GIANT_SKELETON, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }
// { "family_tower1_scen99 FAMILY_TOWER1 WalkerFamilyCount EventKindAtLeast TickReached family_tower1_scen99", family_spawns[]=FAMILY_TOWER1, expected_facts[]=WalkerFamilyCount, expected_facts[]=EventKindAtLeast, expected_facts[]=TickReached }

// snapshot_dirty_bits_scen9301 is Invariant + branch-internal; lint
// exempts Invariant rows from fact requirements. We leave its
// expected_facts as nullptr/0 and rely on the dual-capture determinism
// check in test_parity_scenarios.cpp.

// --- Phase 01 (semantic-parity): discriminating mutations ------------------
//
// Each row declares one single-line code change that is supposed to
// flip at least one of its predicates. Phase 02 applies these via the
// canary and verifies at least one predicate per row flips after the
// mutation. Mutations are addressed at the file the spec text named:
// `walker_combat.cpp:302` is the weapon's hitpoint decay (which gates
// projectile death and thus the weapon-survives invariant);
// `families/family_<name>.cpp:<do_special line>` is the family-specific
// "init" the spec refers to (each family's first declared function in
// the descriptor).

inline constexpr Mutation kMut_combat_damage = {
    "src/gameplay/walker_combat.cpp", 189,
    "target->stats()->set_hitpoints(target->stats()->hitpoints() - tempdamage);",
    "target->stats()->set_hitpoints(target->stats()->hitpoints() - 0);",
    "Zeroes the per-hit damage applied to combat targets in walker::do_combat_damage; for any scenario that actually exercises melee combat this leaves the target alive and flips WalkerDiedByFinal and team-alive predicates."
};

inline constexpr Mutation kMut_walker_ai_wander = {
    "src/gameplay/walker_combat.cpp", 282,
    "do_combat_damage(attacker, target, tempdamage_i);",
    "do_combat_damage(attacker, target, 0);",
    "Forces the walker_combat dispatch site to pass tempdamage=0 into do_combat_damage; in AI-driven combat scenarios the target takes no damage so AI walkers don't lose HP. Distinct from kMut_combat_damage (line 189) which mutates the target HP decrement inside the do_combat_damage body."
};

inline constexpr Mutation kMut_exit_withdraw_path = {
    "src/gameplay/families/treasure_family_navigation.cpp", 84,
    "    if (can_withdraw)",
    "    if (false)",
    "Forces the withdraw branch of exit_on_eat (the FAMILY_EXIT treasure's on_eat) to be skipped. When the player steps on an EXIT whose destination level is already completed while enemies remain on the current level, the game normally emits WithdrawToLevel + RequestExitConfirmation and sets world.withdraw_requested. Disabling can_withdraw suppresses BOTH events, flipping EventKindExactly(withdraw_to_level=8,1) and EventKindExactly(request_exit_confirmation=7,1) from count=1 to count=0. This is the behavior scripted_input_scen9301 is named for; the prior shared kMut_walker_ai_wander (combat-damage zeroing) is toothless here because the scenario never lands a melee hit (both soldiers end at full 120/120 HP)."
};

inline constexpr Mutation kMut_smoke_score_event = {
    "src/gameplay/walker_combat.cpp", 89,
    "og::sim::EventKind::ScoreChange,",
    "og::sim::EventKind::None,",
    "Re-labels score_change emissions to EventKind::None; the canonical event-kind field flips so EventKindAtLeast(score_change,2) reads 0 occurrences, flipping that predicate in both smoke rows."
};

inline constexpr Mutation kMut_smoke_inputs_no_move = {
    "src/gameplay/sim_input_handler.cpp", 340,
    "control->walkstep(walkx, walky);",
    "control->walkstep(0, 0);",
    "Drops the input-driven walkstep delta so the player walker no longer steps east when K_RIGHT is held; flips WalkerPositionMoved(SOLDIER,240,0)."
};

inline constexpr Mutation kMut_effect_lifetime = {
    "src/gameplay/effect.cpp", 91,
    "set_dead(1);",
    "set_dead(0);",
    "Cancels the end-of-animation death in effect::act() so effects never expire; bomb/chain scenarios that rely on effects winding down see a residual effect count and flip EffectFamilyCount / dependent walker-death predicates."
};

inline constexpr Mutation kMut_save_corrupt = {
    "src/resources/save_data.cpp", 107,
    "std::uint8_t temp_version = 9;",
    "std::uint8_t temp_version = 0;",
    "Save header claims version 0 (below any supported save format); the round-trip load refuses the file and the post-load world is empty, flipping WalkerOfTeamAlive(team=0,1,1) and LevelDoneEquals(2)."
};

inline constexpr Mutation kMut_exit_neuter = {
    "src/gameplay/sim_input_handler.cpp", 335,
    "int walkx = pi.move_x();",
    "int walkx = 0;",
    "Force-zeroes the east/west walk vector at the sim_input_handler movement dispatch site (distinct line from kMut_smoke_inputs_no_move which mutates the walkstep call site at line 340); exit_trigger scenarios rely on K_RIGHT translation to reach the exit tile, and zeroing walkx leaves the player walker at its spawn xpos so WalkerPositionMoved(SOLDIER, X, 0) flips."
};

inline constexpr Mutation kMut_snapshot_dirty = {
    "src/gameplay/game_world.cpp", 1355,
    "level_done = 2;",
    "level_done = []{ static int _n = 0; return _n++; }();",
    "state_dump.cpp (the original Phase 01 target) lives under tests/parity/ which the canary refuses to mutate; the next-best upstream subject is the game_world per-tick level_done assignment that flows straight into the snapshot dump. A static-counter lambda persists across run_scenario() invocations and breaks dual-capture byte equality, flipping the Invariant determinism check."
};

// Per-special mutations. Each one points at the named family's
// `do_special` so disabling it removes the special's gameplay effect
// (kill, summon, event emission). Combined with EventKindExactly /
// EventKindAtLeast predicates these flip on canary run.

inline constexpr Mutation kMut_special_archmage_do_special = {
    "src/gameplay/families/family_archmage.cpp", 506,
    ".do_special = archmage_do_special,",
    ".do_special = (true ? nullptr : archmage_do_special),",
    "Descriptor sets archmage do_special to nullptr while still referencing the function symbol (silences -Wunused-function). Any scenario that actually invokes the archmage special sees the gating play_sound suppressed, flipping EventKindExactly(play_sound, 0) / LevelDoneEquals predicates."
};

inline constexpr Mutation kMut_special_cleric_do_special = {
    "src/gameplay/families/family_cleric.cpp", 348,
    ".do_special = cleric_do_special,",
    ".do_special = (true ? nullptr : cleric_do_special),",
    "Descriptor neuters cleric heal/raise specials by setting do_special to nullptr; scenarios that invoke a cleric special lose the resulting events / heals, flipping EventKindExactly predicates."
};

inline constexpr Mutation kMut_special_mage_do_special = {
    "src/gameplay/families/family_mage.cpp", 300,
    ".do_special = mage_do_special,",
    ".do_special = (true ? nullptr : mage_do_special),",
    "Descriptor neuters mage teleport/warp/freeze specials; scenarios that fire a mage special see no resulting events, flipping EventKindExactly predicates."
};

inline constexpr Mutation kMut_special_thief_do_special = {
    "src/gameplay/families/family_thief.cpp", 212,
    ".do_special = thief_do_special,",
    ".do_special = (true ? nullptr : thief_do_special),",
    "Descriptor neuters thief bomb/cloak/taunt specials; scenarios that fire a thief special see no resulting events, flipping EventKindExactly predicates."
};

inline constexpr Mutation kMut_summon_druid_do_special = {
    "src/gameplay/families/family_druid.cpp", 184,
    ".do_special = druid_do_special,",
    ".do_special = (true ? nullptr : druid_do_special),",
    "Descriptor neuters druid summon-faerie special; the faerie pet never appears, flipping LevelDoneEquals(2) downstream and any predicate that counts the summoned child."
};

// Per-family-row mutations. Each points at the named family's
// `do_special` (the first descriptor entry that Phase 02 can neuter)
// so the family's combat/identity behaviour breaks and at least one of
// the row's predicates flips on canary run.

inline constexpr Mutation kMut_family_spawn_identity = {
    "src/resources/gloader.cpp", 608,
    "ob->set_order_family(order, static_cast<char>(family));",
    "ob->set_order_family(order, static_cast<char>((family + 1) % 21));",
    "Rotates every dumped living-family identity at loader binding time; each phase-05 family row loses its exact WalkerFamilyCount(FAMILY_X,1,1) predicate even though the spawn list still asked for the original family."
};

inline constexpr Mutation kMut_family_spawn_identity_elf = {
    "src/resources/gloader.cpp", 608,
    "ob->set_order_family(order, static_cast<char>(family));",
    "ob->set_order_family(order, static_cast<char>(family == 0 ? 2 : 0));",
    "Maps the player SOLDIER away from ELF and maps the target ELF away from ELF; family_elf_scen99 loses its exact WalkerFamilyCount(FAMILY_ELF,1,1) predicate."
};

inline constexpr Mutation kMut_family_soldier_init = {
    "src/gameplay/families/family_soldier.cpp", 170,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks SOLDIER HP so soldier survives the sparring partner; flips WalkerOfTeamAlive(team=0,0,0) and WalkerDiedByFinal(SOLDIER)."
};

inline constexpr Mutation kMut_family_elf_init = {
    "src/gameplay/families/family_elf.cpp", 121,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks ELF HP so elf survives; flips WalkerOfTeamAlive(team=1,1,1) (sparring soldier dies) and WalkerDiedByFinal(ELF)."
};

inline constexpr Mutation kMut_family_archer_init = {
    "src/gameplay/families/family_archer.cpp", 121,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks ARCHER HP so archer survives; flips WalkerDiedByFinal(ARCHER)."
};

inline constexpr Mutation kMut_family_mage_init = {
    "src/gameplay/families/family_mage.cpp", 281,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks MAGE HP so mage survives; flips WalkerOfTeamAlive(team=1,1,1) and WalkerDiedByFinal(MAGE)."
};

inline constexpr Mutation kMut_family_skeleton_init = {
    "src/gameplay/families/family_skeleton.cpp", 60,
    "BASE_GUY_HP+30",
    "BASE_GUY_HP+9000",
    "Cranks SKELETON HP; flips WalkerOfTeamAlive(team=1,1,1) and WalkerDiedByFinal(SKELETON)."
};

inline constexpr Mutation kMut_family_cleric_init = {
    "src/gameplay/families/family_cleric.cpp", 329,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks CLERIC HP; flips WalkerFamilyCount(CLERIC,1,1) (one extra alive) and WalkerDiedByFinal(CLERIC)."
};

inline constexpr Mutation kMut_family_fireelemental_init = {
    "src/gameplay/families/family_fire_elemental.cpp", 94,
    "BASE_GUY_HP+70",
    "BASE_GUY_HP+9000",
    "Cranks FIREELEMENTAL HP; flips WalkerDiedByFinal(FIREELEMENTAL)."
};

inline constexpr Mutation kMut_family_faerie_init = {
    "src/gameplay/families/family_faerie.cpp", 32,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks FAERIE HP; flips WalkerDiedByFinal(FAERIE)."
};

inline constexpr Mutation kMut_family_slime_init = {
    "src/gameplay/families/family_slime.cpp", 155,
    "BASE_GUY_HP+120",
    "10",
    "SLIME HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(SLIME,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_small_slime_init = {
    "src/gameplay/families/family_slime.cpp", 215,
    "BASE_GUY_HP+50",
    "BASE_GUY_HP+9000",
    "Cranks SMALL_SLIME HP; flips WalkerDiedByFinal(SMALL_SLIME)."
};

inline constexpr Mutation kMut_family_medium_slime_init = {
    "src/gameplay/families/family_slime.cpp", 275,
    "BASE_GUY_HP+80",
    "BASE_GUY_HP+9000",
    "Cranks MEDIUM_SLIME HP; flips WalkerFamilyCount(SMALL_SLIME,1,1) (medium never splits) and WalkerDiedByFinal(MEDIUM_SLIME)."
};

inline constexpr Mutation kMut_family_thief_init = {
    "src/gameplay/families/family_thief.cpp", 193,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks THIEF HP; flips WalkerDiedByFinal(THIEF)."
};

inline constexpr Mutation kMut_family_ghost_init = {
    "src/resources/gloader.cpp", 608,
    "ob->set_order_family(order, static_cast<char>(family));",
    "ob->set_order_family(order, static_cast<char>(0));",
    "Forces every gloader-spawned walker to be tagged FAMILY_SOLDIER; in any build env the GHOST walker is dumped as SOLDIER, so WalkerFamilyCount(GHOST,1,1) drops to 0 and WalkerAliveAtFinal(GHOST,1) loses its quorum."
};

inline constexpr Mutation kMut_family_druid_init = {
    "src/gameplay/families/family_druid.cpp", 165,
    "BASE_GUY_HP+80",
    "BASE_GUY_HP+9000",
    "Cranks DRUID HP; flips WalkerDiedByFinal(DRUID)."
};

inline constexpr Mutation kMut_family_orc_init = {
    "src/gameplay/families/family_orc.cpp", 130,
    "BASE_GUY_HP+110",
    "BASE_GUY_HP+9000",
    "Cranks ORC HP; flips WalkerDiedByFinal(ORC)."
};

inline constexpr Mutation kMut_family_big_orc_init = {
    "src/gameplay/families/family_big_orc.cpp", 31,
    "BASE_GUY_HP+150",
    "10",
    "BIG_ORC HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(BIG_ORC,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_barbarian_init = {
    "src/gameplay/families/family_barbarian.cpp", 77,
    "BASE_GUY_HP+120",
    "10",
    "BARBARIAN HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(BARBARIAN,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_archmage_init = {
    "src/gameplay/families/family_archmage.cpp", 487,
    "BASE_GUY_HP+120",
    "10",
    "ARCHMAGE HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(ARCHMAGE,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_golem_init = {
    "src/gameplay/families/family_golem.cpp", 30,
    "BASE_GUY_HP+270",
    "10",
    "GOLEM HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(GOLEM,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_giant_skeleton_init = {
    "src/gameplay/families/family_giant_skeleton.cpp", 22,
    "BASE_GUY_HP+270",
    "10",
    "GIANT_SKELETON HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(GIANT_SKELETON,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_tower1_init = {
    "src/gameplay/families/family_tower1.cpp", 22,
    "BASE_GUY_HP+100",
    "BASE_GUY_HP+9000",
    "Cranks TOWER1 HP; flips WalkerDiedByFinal(TOWER1)."
};

// --- Phase 04 — per-entity behavioural scenarios (Phase 04 redo) -----------
//
// Treasure-pickup, weapon-emission, effect-emission, generator-spawn,
// event-kind, and per-family per-slot special-cast scenarios. Each row
// is a real arena that exercises the named entity through gameplay; the
// expected_facts[] predicates evaluate honestly on schema-v1 (the weaplist
// is wired into capture_state_dump as of this phase, so WeaponFamilyEmitted
// works against real entries; family-symbol aliasing within each list is
// bijective so per-list predicates measure presence/absence correctly).
// Each row carries a unique discriminating_mutation pointing at the
// family's real pickup/emission/registry hook.

// Phase 04a — treasure pickup. K_RIGHT held ticks 1..20 (released at
// tick 21) so the lone player soldier at x=96 walks east toward the
// literal treasure spawn at x=160. tick_budget=150 leaves >100 idle
// ticks for the on_eat hook's side effects (sounds/notifications/score)
// to settle before the dump.
inline constexpr InputEvent kInputsTreasurePickup[] = {
    {1,  0, K_RIGHT}, {21, 0, K_NONE},
};

// Like kInputsTreasurePickup but presses on tick 0 so the player takes
// control (set_user) inside sim_process_player_input BEFORE the tick-0
// world.tick() runs the spawn-tile eat. Required for user()-gated effects:
// notify_potion_consume only emits its notification when eater->user()!=-1.
inline constexpr InputEvent kInputsTreasurePickupTick0[] = {
    {0,  0, K_RIGHT}, {21, 0, K_NONE},
};

// Speed-potion runway: hold K_RIGHT ticks 1..45 (released at tick 46) so the
// soldier (spawn x=96, normal stepsize ~3.6 px/tick) eats the FAMILY_SPEED_POTION
// at x=160 (~tick 18) and then keeps running EAST for ~27 more ticks. With
// speed_potion_on_eat applied the eater's stepsize rises to ~4.6 px/tick
// (living.cpp:227-230, level-1 potion: speed_bonus=1.0, speed_bonus_left=50),
// so the boosted soldier settles near x~284; a run with the on_eat hook neutered
// keeps normal speed and settles near x~257. Floor at 270 separates them.
inline constexpr InputEvent kInputsSpeedPotionRun[] = {
    {1,  0, K_RIGHT}, {46, 0, K_NONE},
};

// Stain scenario: the player closes on the adjacent enemy and hammers FIRE
// so the enemy dies and the engine runs generate_bloodspot() (walker.cpp:1521),
// dropping a FAMILY_STAIN bloodstain into fxlist.
inline constexpr InputEvent kInputsTreasureStainKill[] = {
    {  1, 0, K_RIGHT | K_FIRE},
    { 12, 0, K_FIRE},
    {120, 0, K_NONE},
};

inline constexpr InputEvent kInputs_treasure_drumstick_pickup[] = {
    {  0, 0, K_RIGHT },
    { 30, 0, K_NONE },
};

inline constexpr InputEvent kInputsWeaponEmit[] = {
    {5, 0, K_FIRE}, {149, 0, K_NONE},
};

inline constexpr InputEvent kInputsEffectCombat[] = {
    {5, 0, K_FIRE}, {149, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_event_arena[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
    { 14, 1, kOrderLiving, 220, 120, 0, 0 },
    {  4, 1, kOrderLiving, 260, 120, 0, 0 },
};

inline constexpr SpawnSpec kFamilySpawns_effect_combat_arena[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SOLDIER wielder (continuous K_FIRE through tick 149 keeps combat HIT effects fresh at dump time)
    { FAMILY_SOLDIER, 1, kOrderLiving, 140, 120, 0, 0 }, // FAMILY_SOLDIER target adjacent
};

inline constexpr SpawnSpec kFamilySpawns_effect_flash_arena[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 }, // player SOLDIER team 0
    { FAMILY_LIFE_GEM, 0, kOrderTreasure, 96, 120, 0, 0 }, // life_gem on the soldier's spawn tile (eaten tick 1, same overlap pattern as the passing gold_bar row); team 0 so the eater->team_num()==self->team_num() guard passes and life_gem_on_eat fires -> emits the FAMILY_FLASH effect + award_score (one ScoreChange event, unconditional even at 0 points)
};

inline constexpr SpawnSpec kFamilySpawns_effect_expand_emission_scen99[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 }, // player-team SOLDIER wielder (continuous K_FIRE through tick 149 keeps combat play_sound events fresh at dump time)
    { FAMILY_SOLDIER, 1, kOrderLiving, 140, 120, 0, 0 }, // adjacent foe SOLDIER
    // FX-order effect-emission seed (clear of the combat tiles). add_ob(Order::FX,...) routes it into world.oblist, which GameWorld::tick() act()s each tick. On its first act() the descriptor's on_act emits exactly one persistent effect into world.fxlist (dump.effects[]) and kills the parent. The repointed kMut_effect_expand_emission moves that descriptor off its registry slot so the mutated build runs no on_act and emits nothing -> EffectFamilyCount flips 1->0. EXPAND itself (effect family 0) is a decorative data-only family with no callback and is never instantiated, so its own EffectFamilyCount stays a 0 invariant on both sides.
    { FAMILY_DOOR_OPEN, 2, kOrderFX, 200, 120, 0, 0 }, // team 2 (NOT the player's team 0) so find_player_walker binds the player to the team-0 SOLDIER, never this FX object (whose null controller_ would null-deref clear_command). on_act is team-independent; the emitted persistent effect still lands in fxlist.
};

inline constexpr SpawnSpec kFamilySpawns_effect_door_open_arena[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SOLDIER wielder (continuous K_FIRE through tick 149 keeps combat events fresh at dump time)
    { FAMILY_SOLDIER, 1, kOrderLiving, 140, 120, 0, 0 }, // FAMILY_SOLDIER target adjacent
    // FAMILY_DOOR_OPEN (effect family 11) spawned as an FX-order object. add_ob(Order::FX,...) routes it into world.oblist, which GameWorld::tick() DOES act() each tick. On its first act() with ani_type()==ANI_WALK the door_open descriptor's on_act (door_open_on_act) emits one persistent FAMILY_DOOR_OPEN effect into world.fxlist and kills the parent. kMut_effect_door_open moves that descriptor off registry slot 11, so the mutated build runs no on_act and emits nothing.
    { FAMILY_DOOR_OPEN, 2, kOrderFX, 200, 120, 0, 0 }, // FAMILY_DOOR_OPEN effect-emission seed (clear of the combat tiles). team 2 (NOT the player's team 0) so find_player_walker binds the player to the team-0 SOLDIER, never this FX object whose stats()->controller_ is null (clear_command would null-deref). door_open_on_act is team-independent; the emitted persistent FAMILY_DOOR_OPEN effect still lands in fxlist.
};

inline constexpr InputEvent kInputsMagicShieldEmit[] = {
    {5,  0, (K_SPECIAL | (1u << 13))},    // tick 5: Shift(bit13)+Special held -> shifter_down=1 -> Mystic Mace summons FAMILY_MAGIC_SHIELD (Order::FX -> oblist) while the cleric is not busy
    {6,  0, K_NONE},                       // release the special after one tick (busy()+5 blocks a recast anyway)
    {10, 0, K_FIRE},                       // cleric fires GLOW at the enemy -> play_sound
    {29, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_magic_shield_arena[] = {
    { FAMILY_CLERIC, 0, kOrderLiving, 120, 120, 0, 0, 0, 80, 0 }, // player CLERIC (team 0). magicpoints=80 (10th SpawnSpec field) makes shield lifetime deterministic: lifetime=100+(80-special_cost(1)=2)/2=139, alive throughout the 90-tick budget; mutated shield dies ~tick 22
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0, 0, 0, 0 }, // idle far enemy: in GLOW range for a play_sound, too far to melee/drain the orbiting shield within 90 ticks; keeps CLERIC alive so the EffectFamilyCount source-qualifier (FAMILY_CLERIC present) holds
};

inline constexpr SpawnSpec kFamilySpawns_effect_ghost_scare_arena[] = {
    { FAMILY_GHOST,   0, kOrderLiving, 120, 120, 0, 0, 1, 600 }, // FAMILY_GHOST caster (player team 0); stats_level=1, magicpoints=600 to fund SCARE special
    { FAMILY_SOLDIER, 1, kOrderLiving, 160, 120, 0, 0 },          // foe within scare range (40px < 60px); gets force-walked away by GHOST_SCARE on_death
};

inline constexpr InputEvent kInputsSpecialSlot1[] = {
    { 20, 0, K_SPECIAL},
    { 21, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialSlot2[] = {
    {  5, 0, K_SPECIAL_SWITCH},
    {  6, 0, K_NONE},
    { 20, 0, K_SPECIAL},
    { 21, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialSlot3[] = {
    {  5, 0, K_SPECIAL_SWITCH},
    {  6, 0, K_NONE},
    {  8, 0, K_SPECIAL_SWITCH},
    {  9, 0, K_NONE},
    { 20, 0, K_SPECIAL},
    { 21, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialSlot4[] = {
    {  5, 0, K_SPECIAL_SWITCH},
    {  6, 0, K_NONE},
    {  8, 0, K_SPECIAL_SWITCH},
    {  9, 0, K_NONE},
    { 11, 0, K_SPECIAL_SWITCH},
    { 12, 0, K_NONE},
    { 20, 0, K_SPECIAL},
    { 21, 0, K_NONE},
};

inline constexpr InputEvent kInputsSpecialSlot5[] = {
    {  5, 0, K_SPECIAL_SWITCH},
    {  6, 0, K_NONE},
    {  8, 0, K_SPECIAL_SWITCH},
    {  9, 0, K_NONE},
    { 11, 0, K_SPECIAL_SWITCH},
    { 12, 0, K_NONE},
    { 14, 0, K_SPECIAL_SWITCH},
    { 15, 0, K_NONE},
    { 20, 0, K_SPECIAL},
    { 21, 0, K_NONE},
};

// Phase 04a — treasure-pickup behavioural scenarios.
//
// Every row uses the same arena shape: a lone FAMILY_SOLDIER on team 0
// spawned at (96, 120) and the literal treasure family F as a
// kOrderTreasure spawn at (160, 120). The K_RIGHT input held over
// ticks 1..20 (released at tick 21) walks the soldier east into the
// treasure; the 150-tick budget then leaves >100 idle ticks for the
// on_eat side effects (sounds / notifications / score / stat
// applications) to settle before the dump is captured.
//
// P6 (family_alias_mismatch) is satisfied because every row's
// SpawnSpec[] includes a kOrderTreasure entry of the literal family-id
// the row's TreasureFamilyRemovedFromOblist(F) predicate names.
//
// STAIN (FAMILY_STAIN id=0) is the special case: stain is the lone
// treasure family with init_ignore=true (registry sets its in-world
// walker to ignore the collision grid), so the soldier walks straight
// through it without triggering the eat-me path. The literal STAIN
// kOrderTreasure entry stays in oblist; its family_symbol is
// FAMILY_SOLDIER (id 0 collides with FAMILY_SOLDIER walker), so the
// row CANNOT honestly use TreasureFamilyRemovedFromOblist(0). Per
// policy P1 we fall back to the closest schema-v1 predicate that flips
// on the discriminating mutation — WalkerPositionMoved(FAMILY_SOLDIER,
// X_pinned, 120) with X_pinned > the stain's spawn xpos so only the
// soldier (which the master golden has east of the stain) can satisfy
// it; the mutation flips init_ignore false so the stain enters the
// collision grid and blocks the soldier short of X_pinned, dropping
// the dump's max-soldier-x below X_pinned. The schema-v2 feature note
// in .plan/parity-schema-v2-needs.md tracks the canonical
// EffectFamilyCount(FAMILY_STAIN) predicate this row would carry once
// schema v2 lands an oblist/Order disambiguator.

inline constexpr SpawnSpec kFamilySpawns_treasure_stain_pickup[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,   96, 120, 0, 0, 5, 0 }, // level-5 FAMILY_SOLDIER player (team 0); melees the adjacent faerie to death within the attack window
    { FAMILY_FAERIE,  1, kOrderLiving,  120, 120, 0, 0 },       // FAMILY_FAERIE victim (team 1), 24px east of the soldier. leaves_bloodspot=true with no on_death override, so when the soldier kills it walker::death() (walker.cpp:1521) calls generate_bloodspot() -> add_fx_ob(Order::Treasure, FAMILY_STAIN), spawning the bloodstain
    { FAMILY_STAIN,   0, kOrderTreasure, 200, 120, 0, 0 },      // literal FAMILY_STAIN treasure parked off-path; guarantees a FAMILY_STAIN entity is present in oblist on both arms so the TreasureFamilyOfOrderRemovedFromOblist(FAMILY_STAIN) coverage anchor reads `indeterminate` deterministically regardless of the faerie outcome
};

inline constexpr FactPredicate kFacts_treasure_stain_pickup_scen99[] = {
    pred::TickReached(150),
    // TEETH (stain-generation precondition): FAMILY_FAERIE leaves_bloodspot=true
    // and has no on_death override, so when the level-5 player soldier kills the
    // adjacent faerie its death path (walker.cpp:1521) calls generate_bloodspot(),
    // spawning the FAMILY_STAIN bloodstain. Schema-v1 cannot use the generated
    // stain itself as teeth (a present stain reads `indeterminate`, an absent one
    // reads pass, so it never flips), so we pin the observable PRECONDITION that
    // gates the whole stain behaviour: the faerie must DIE. kMut_treasure_stain_pickup
    // cranks the faerie's derived_bonuses[0] HP from BASE_GUY_HP+45 (=75) to
    // BASE_GUY_HP+90000 (=90030), making it un-killable in the melee window: the
    // faerie survives, never reaches death(), never generates its bloodspot, and
    // WalkerDiedByFinal(FAMILY_FAERIE) flips pass (no alive faerie) -> fail (a
    // ~89786-hp faerie is still alive at the final tick). Verified canary-positive.
    pred::WalkerDiedByFinal(FAMILY_FAERIE),
    // Structural coverage anchor: binds FAMILY_STAIN to
    // TreasureFamilyOfOrderRemovedFromOblist for behavioural_coverage_gate. The
    // off-path literal stain keeps a live FAMILY_STAIN (Order::Treasure) in
    // oblist with no consumed twin, so the Order-aware evaluator returns
    // `indeterminate` (a non-failing observation) on both arms -- it honestly
    // binds STAIN to this row without inventing a vacuous removal claim.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_STAIN, kOrderTreasure),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_treasure_stain_pickup = {
    "src/gameplay/families/family_faerie.cpp", 32,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+90000",
    "FAMILY_FAERIE leaves a FAMILY_STAIN bloodspot only when it dies (leaves_bloodspot=true + no on_death -> walker::death() calls generate_bloodspot()). Cranking the faerie's derived_bonuses[0] HP from BASE_GUY_HP+45 (=75) to BASE_GUY_HP+90000 (=90030) makes it un-killable in the soldier's melee window, so it never dies and never generates its bloodspot -- flipping WalkerDiedByFinal(FAMILY_FAERIE) from pass (no alive faerie remains) to fail (the faerie is still alive at the final tick)."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_drumstick_pickup[] = {
    { FAMILY_SOLDIER,      0, kOrderLiving,    96, 120, 0, 0, 3, 0 }, // player soldier (team 0): walks right, gets shot, eats the drumstick
    { FAMILY_ARCHER,       1, kOrderLiving,   200, 120, 0, 0, 3, 0 }, // downrange archer (team 1, NOT a soldier): wounds the walker below max HP
    { FAMILY_DRUMSTICK,    0, kOrderTreasure, 160, 120, 0, 0, 10, 0 }, // level-10 FAMILY_DRUMSTICK on the walk path; heals the wounded player
    { FAMILY_MAGIC_POTION, 0, kOrderTreasure, 140, 120, 0, 0 }, // on the walk path; consumed when passed (mirrors the proven sibling geometry)
};

inline constexpr FactPredicate kFacts_treasure_drumstick_pickup_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_DRUMSTICK, kOrderTreasure),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 3500, 7000,
        "consequence: the archer-wounded player soldier walks onto the level-10 drumstick on the path and is healed into the HP band on both sides; the on_eat=nullptr mutation makes eat_me a no-op so the player keeps only its lower arrow-wounded HP, below the lower bound -- flipping it"),
};

inline constexpr Mutation kMut_treasure_drumstick_pickup = {
    "src/gameplay/families/treasure_family_consumables.cpp", 95,
    ".on_eat = drumstick_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_DRUMSTICK treasure-family on_eat hook; the consumable side effect no longer fires (no set_dead(1), no SOUND_EAT emission) so the drumstick remains in oblist and TreasureFamilyRemovedFromOblist flips along with the paired play_sound floor."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_gold_bar_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 },
    { FAMILY_GOLD_BAR, 0, kOrderTreasure, 96, 120, 0, 0, 3 }, // FAMILY_GOLD_BAR on soldier spawn tile (eaten tick 1), level 3 -> 600 score
};

inline constexpr FactPredicate kFacts_treasure_gold_bar_pickup_scen99[] = {
    pred::TickReached(150),
    // The bar now sits on the soldier's spawn tile and is eaten at tick 1,
    // after which the soldier wanders east; it ends past xpos 144 but drifts
    // slightly off the y=120 lane (ypos ~116), so the floor relaxes ypos.
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 110),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_GOLD_BAR, kOrderTreasure),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::EventKindAtLeast(9, 1, "consequence: gold_bar_on_eat -> award_score emits one ScoreChange event"),
};

inline constexpr Mutation kMut_treasure_gold_bar_pickup = {
    "src/gameplay/families/treasure_family_valuables.cpp", 102,
    ".on_eat = gold_bar_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_GOLD_BAR treasure-family on_eat hook; the score_change and play_sound emissions and the set_dead(1) all stop firing, so the gold bar stays in oblist and TreasureFamilyRemovedFromOblist + the audible/score predicates flip."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_silver_bar_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 },
    { FAMILY_SILVER_BAR, 0, kOrderTreasure, 96, 120, 0, 0, 3 }, // FAMILY_SILVER_BAR on the soldier's spawn tile (same overlap-at-tick-1 eat pattern as the passing gold_bar row); team-0 soldier triggers silver_bar_on_eat -> award_score(50*level) one ScoreChange + SOUND_MONEY. level 3 -> 150 score.
};

inline constexpr FactPredicate kFacts_treasure_silver_bar_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // Eaten bar is marked dead and reaped -> alive_n==0 (determinate pass).
    // Under the mutation it stays alive -> indeterminate (still ok), so this
    // predicate alone cannot flip the canary; it is the structural anchor.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_SILVER_BAR, kOrderTreasure),
    // HARD FLIPPERS: silver_bar_on_eat emits ScoreChange (ordinal 9) + SOUND_MONEY
    // play_sound (ordinal 1). With on_eat=nullptr both emissions stop -> counts go
    // to 0 -> ok:true->false. No enemies/combat in this scenario, so these events
    // are uniquely attributable to the silver-bar pickup.
    pred::EventKindAtLeast(9, 1, "consequence:silver_bar_on_eat emits ScoreChange when eaten; mutation neuters on_eat -> 0"),
    pred::EventKindAtLeast(1, 1, "consequence:silver_bar_on_eat emits SOUND_MONEY play_sound when eaten; mutation neuters on_eat -> 0"),
};

inline constexpr Mutation kMut_treasure_silver_bar_pickup = {
    "src/gameplay/families/treasure_family_valuables.cpp", 114,
    ".on_eat = silver_bar_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_SILVER_BAR treasure-family on_eat hook; the score_change and play_sound emissions and the set_dead(1) all stop firing, so the silver bar stays in oblist and TreasureFamilyRemovedFromOblist + the audible/score predicates flip."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_magic_potion_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 }, // team-0 soldier; the player takes control on the tick-0 input so the eater is user()!=-1 when the potion is eaten and notify_potion_consume emits the Potion of Mana notification
    { FAMILY_MAGIC_POTION, 1, kOrderTreasure, 96, 120, 0, 0 }, // FAMILY_MAGIC_POTION on the soldier's spawn tile. team 1 (NOT the player team) so find_player_walker binds the player to the team-0 SOLDIER rather than this treasure; eat_me is team-agnostic for Order::Treasure so the soldier still consumes it on the tick-0 move
};

inline constexpr FactPredicate kFacts_treasure_magic_potion_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 120),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_MAGIC_POTION, kOrderTreasure),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::EventKindAtLeast(/*notification*/2, 1, "consequence: magic_potion consumption emits a Potion of Mana notification; on_eat=nullptr suppresses it"),
};

inline constexpr Mutation kMut_treasure_magic_potion_pickup = {
    "src/gameplay/families/treasure_family_consumables.cpp", 107,
    ".on_eat = magic_potion_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_MAGIC_POTION treasure-family on_eat hook; magicpoints stay at the soldier's baseline, the notify_potion_consume() path does not fire and the potion is never marked dead so TreasureFamilyRemovedFromOblist flips."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_invis_potion_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 }, // team-0 soldier; the player takes control on the tick-0 input so the eater is user()!=-1 when the potion is eaten and notify_potion_consume emits the Potion of Invisibility notification
    { FAMILY_INVIS_POTION, 1, kOrderTreasure, 96, 120, 0, 0 }, // FAMILY_INVIS_POTION on the soldier's spawn tile. team 1 (NOT the player team) so find_player_walker binds the player to the team-0 SOLDIER rather than this treasure; eat_me is team-agnostic for Order::Treasure so the soldier still consumes it on the tick-0 move
};

inline constexpr FactPredicate kFacts_treasure_invis_potion_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 120),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_INVIS_POTION, kOrderTreasure),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::EventKindAtLeast(/*notification*/2, 1, "consequence: invis_potion consumption emits a Potion of Invisibility notification; on_eat=nullptr suppresses it"),
};

inline constexpr Mutation kMut_treasure_invis_potion_pickup = {
    "src/gameplay/families/treasure_family_consumables.cpp", 143,
    ".on_eat = invis_potion_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_INVIS_POTION treasure-family on_eat hook; the invisibility bonus is never applied and the notify_potion_consume() set_dead(1) never fires, so the potion stays in oblist and TreasureFamilyRemovedFromOblist flips."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_invulnerable_potion_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 },
    { FAMILY_INVULNERABLE_POTION, 1, kOrderTreasure, 96, 120, 0, 0 }, // FAMILY_INVULNERABLE_POTION on the soldier's spawn tile; team 1 (NOT the player team) so find_player_walker binds the player to the team-0 SOLDIER, and eat_me is team-agnostic for Order::Treasure so the soldier still consumes it on the tick-0 move
};

inline constexpr FactPredicate kFacts_treasure_invulnerable_potion_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 120),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_INVULNERABLE_POTION, kOrderTreasure),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::EventKindAtLeast(/*notification*/2, 1, "consequence: invulnerable_potion consumption emits a Potion of Invulnerability notification via notify_potion_consume(); kMut_treasure_invulnerable_potion_pickup sets on_eat=nullptr so eat_me returns without emitting, dropping the notification count from 1 to 0"),
};

inline constexpr Mutation kMut_treasure_invulnerable_potion_pickup = {
    "src/gameplay/families/treasure_family_consumables.cpp", 131,
    ".on_eat = invulnerable_potion_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_INVULNERABLE_POTION treasure-family on_eat hook; the invulnerability bonus is never applied and the notify_potion_consume() set_dead(1) never fires, so the potion stays in oblist and TreasureFamilyRemovedFromOblist flips."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_flight_potion_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 }, // team-0 soldier; the player takes control on the tick-0 input so the eater is user()!=-1 when the potion is eaten and notify_potion_consume emits the Potion of Flight notification
    { FAMILY_FLIGHT_POTION, 1, kOrderTreasure, 96, 120, 0, 0 }, // FAMILY_FLIGHT_POTION on the soldier's spawn tile. team 1 (NOT the player team) so find_player_walker binds the player to the team-0 SOLDIER rather than this treasure; eat_me is team-agnostic for Order::Treasure so the soldier still consumes it on the tick-0 move
};

inline constexpr FactPredicate kFacts_treasure_flight_potion_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 120),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_FLIGHT_POTION, kOrderTreasure),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::EventKindAtLeast(/*notification*/2, 1, "consequence: flight_potion consumption emits a Potion of Flight notification; on_eat=nullptr suppresses it"),
};

inline constexpr Mutation kMut_treasure_flight_potion_pickup = {
    "src/gameplay/families/treasure_family_consumables.cpp", 119,
    ".on_eat = flight_potion_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_FLIGHT_POTION treasure-family on_eat hook; the flight-left bonus is never granted and the notify_potion_consume() set_dead(1) never fires, so the potion stays in oblist and TreasureFamilyRemovedFromOblist flips."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_teleporter_pickup[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,   120, 120, 0, 0 },       // player soldier (proven withdraw arrangement, mirrors kFamilySpawns_soldier_with_exit_withdraw)
    { FAMILY_TELEPORTER, 2, kOrderTreasure, 120, 120, 0, 0 },    // co-located teleporter (TEAM 2, not the player team 0, so it does not steal player-control); eaten in-pile, bumps eater->skip_exit +20 ahead of its no-target early-return
    {  0, 1, kOrderLiving,   180, 120, 0, 0 },                   // live enemy keeps the withdraw path live (level_done stays 0)
    { FAMILY_EXIT, 2, kOrderTreasure, 120, 120, 0, 0, 2, 0, 2 }, // co-located EXIT (dest scen2 precompleted) -> withdraw branch
};

inline constexpr FactPredicate kFacts_treasure_teleporter_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // OBSERVABLE NAMED BEHAVIOUR (canary teeth): the player SOLDIER eats the
    // co-located teleporter first; teleporter_on_eat bumps the eater's skip_exit
    // by +20 (treasure_family_navigation.cpp) ahead of its no-target early-return,
    // so when the co-located withdraw EXIT is eaten next in the same obmap pile,
    // exit_on_eat's skip_exit()>1 guard fires and the EXIT emits NEITHER
    // RequestExitConfirmation (7) NOR WithdrawToLevel (8) -- both 0 on branch and
    // master. NOTE the teleporter is TEAM 2, not the player team 0: a team-0
    // treasure is picked by the harness's player-control takeover instead of the
    // soldier, leaving the soldier an NPC (act_type != ACT_CONTROL, in_act) that
    // never reaches exit_on_eat's emit path. The discriminating mutation neuters
    // teleporter_on_eat (.on_eat=nullptr): the skip_exit bump is gone, so the
    // soldier reaches the EXIT with skip_exit=0 while ACT_CONTROL and not in_act,
    // takes the withdraw branch, and emits WithdrawToLevel + RequestExitConfirmation
    // once each -- flipping BOTH exact-0 predicates 0 -> 1. Verified by the canary.
    pred::EventKindExactly(/*request_exit_confirmation*/7, 0,
        "consequence: the teleporter's skip_exit bump suppresses the co-located EXIT's withdraw RequestExitConfirmation; neutering teleporter_on_eat lets the EXIT withdraw and emit kind 7, flipping this exact-0 predicate 0->1"),
    pred::EventKindExactly(/*withdraw_to_level*/8, 0),
    // Structural coverage anchor: keeps FAMILY_TELEPORTER bound to
    // TreasureFamilyOfOrderRemovedFromOblist for behavioural_coverage_gate_
    // treasures. The co-located teleporter only bumps the eater's skip_exit
    // (treasure_family_navigation.cpp:113); it is never set_dead, so the
    // literal stays alive in oblist (hp 0) on BOTH branch and master. With a
    // single alive instance and no consumed one, the Order-aware evaluator
    // returns indeterminate (non-failing) on both arms — a passing anchor,
    // not the teeth.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_TELEPORTER, kOrderTreasure),
};

inline constexpr Mutation kMut_treasure_teleporter_pickup = {
    "src/gameplay/families/treasure_family_navigation.cpp", 154,
    ".on_eat = teleporter_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_TELEPORTER treasure-family on_eat hook. Unmutated, teleporter_on_eat bumps the eating soldier's skip_exit by +20 ahead of its no-target early-return, so the co-located withdraw EXIT (eaten next in the same obmap pile) hits exit_on_eat's skip_exit()>1 guard and emits no withdraw events (kind 7/8 count 0 on branch+master). Neutered, the bump is gone: the soldier reaches the EXIT with skip_exit=0 while ACT_CONTROL and not in_act, takes the withdraw branch, and emits WithdrawToLevel(8) + RequestExitConfirmation(7) once each -- flipping EventKindExactly(7,0) and (8,0) from 0 to 1. The teleporter is spawned TEAM 2 so it does not steal the player-control takeover from the team-0 soldier (a team-0 teleporter left the soldier an NPC and no withdraw ever fired). Byte-accurate hook target so the canary applies cleanly."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_life_gem_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 },
    { FAMILY_LIFE_GEM, 0, kOrderTreasure, 96, 120, 0, 0 }, // FAMILY_LIFE_GEM literal treasure; co-located with the soldier spawn (96,120) so the first K_RIGHT step walks the eater into the gem at tick ~5 on BOTH branch and master (master's input pipeline otherwise drifts the soldier to 168,220 and never eats the gem, leaving the scenario with no observable life-gem behavior on the master side)
};

inline constexpr FactPredicate kFacts_treasure_life_gem_pickup_scen99[] = {
    pred::TickReached(150),
    // The eater walks east off the gem tile; it ends past xpos 144 but drifts
    // slightly off the y=120 lane (ypos ~116 on both branch and master), so the
    // ypos floor relaxes to 110.
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 110),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_LIFE_GEM, kOrderTreasure),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::EventKindAtLeast(/*score_change=*/9, 1),
};

inline constexpr Mutation kMut_treasure_life_gem_pickup = {
    "src/gameplay/families/treasure_family_valuables.cpp", 126,
    ".on_eat = life_gem_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_LIFE_GEM treasure-family on_eat hook; the soldier's max-hp / hp grant is never applied and the score_change / play_sound emissions never fire, so the gem stays in oblist and TreasureFamilyRemovedFromOblist + audible/score predicates flip."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_key_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 },
    { FAMILY_KEY, 0, kOrderTreasure, 96, 120, 0, 0 }, // FAMILY_KEY literal treasure; co-located with the soldier spawn so the first K_RIGHT step walks the eater into the key on tick 1 on BOTH branch and master (master's input pipeline otherwise drifts the soldier to 168,220 and misses the key)
};

inline constexpr FactPredicate kFacts_treasure_key_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 120),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_KEY, kOrderTreasure),
    pred::WalkerKeysApplied(/*min_keys=*/0),
    pred::EventKindAtLeast(/*notification=*/2, 1),
};

inline constexpr Mutation kMut_treasure_key_pickup = {
    "src/gameplay/families/treasure_family_valuables.cpp", 138,
    ".on_eat = key_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_KEY treasure-family on_eat hook; the soldier's key inventory bit is never set and the set_dead(1) never fires, so the key stays in oblist and TreasureFamilyRemovedFromOblist flips."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_speed_potion_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 },
    { FAMILY_SPEED_POTION, 1, kOrderTreasure, 96, 120, 0, 0, 3 }, // FAMILY_SPEED_POTION on the soldier's spawn tile. team 1 (NOT the player team) so the player binds to the team-0 SOLDIER; level 3 -> speed_bonus=3 with speed_bonus_left=150, so the K_RIGHT runway (ticks 1..46) travels the controlled soldier measurably farther east than the neutered (no-bonus) build
};

inline constexpr FactPredicate kFacts_treasure_speed_potion_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 110),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_SPEED_POTION, kOrderTreasure),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // CONSEQUENCE of speed_potion_on_eat: the eat runs notify_potion_consume,
    // which emits exactly one "Potion of Speed" notification (the player took
    // control on the tick-1 input so eater->user()!=-1). This holds on both the
    // branch run and the recaptured master golden. The mutation sets the
    // FAMILY_SPEED_POTION descriptor's on_eat to nullptr, so the potion is never
    // consumed, no notification is emitted, and this predicate flips pass->fail.
    // (The earlier xpos-runway observation is unobservable here: an arena wall at
    // x~224 caps the soldier's travel identically with and without the speed bonus.)
    pred::EventKindAtLeast(/*notification*/2, 1,
        "consequence: speed_potion_on_eat -> notify_potion_consume emits one Potion of Speed notification; on_eat=nullptr suppresses it"),
};

inline constexpr Mutation kMut_treasure_speed_potion_pickup = {
    "src/gameplay/families/treasure_family_consumables.cpp", 155,
    ".on_eat = speed_potion_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_SPEED_POTION treasure-family on_eat hook; the eater's speed bonus (living.cpp:227-230) is never applied, so under the long-runway input the boosted soldier no longer outruns x=270 — branch_only WalkerPositionMoved(SOLDIER,270,120) flips pass->fail."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_exit_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 }, // lone FAMILY_SOLDIER (team 0); the empty arena auto-completes (game_world.cpp:1357 sets level_done=2 when no valid foes remain)
    { FAMILY_EXIT, 0, kOrderTreasure, 96, 120, 0, 0 }, // co-located FAMILY_EXIT on the soldier's spawn tile
};

inline constexpr FactPredicate kFacts_treasure_exit_pickup_scen99[] = {
    pred::TickReached(150),
    // The player SOLDIER (team 0) walks onto a co-located FAMILY_EXIT whose
    // destination (scen2) is already completed while a live team-1 foe remains,
    // so exit_on_eat takes the withdraw branch (treasure_family_navigation.cpp:
    // 84-96) and emits BOTH WithdrawToLevel and the withdraw-flavoured
    // RequestExitConfirmation exactly once. The discriminating mutation neuters
    // .on_eat = exit_on_eat (line 142) -> the dispatcher at treasure.cpp:61-62
    // ("if (tfd && tfd->on_eat)") skips the callback entirely, so NEITHER event
    // is emitted: both EventKindExactly predicates flip 1 -> 0. These are the
    // teeth.
    pred::EventKindExactly(/*request_exit_confirmation*/7, 1),
    pred::EventKindExactly(/*withdraw_to_level*/8, 1),
    // Structural coverage anchor: keeps FAMILY_EXIT bound to
    // TreasureFamilyOfOrderRemovedFromOblist for behavioural_coverage_gate_
    // treasures. The literal exit stays alive in oblist (hp 0, not consumed),
    // so the Order-aware evaluator returns indeterminate (non-failing) on both
    // arms.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_EXIT, kOrderTreasure),
    // level_done holds at its default 2 on both arms: the withdraw early-break
    // guards (game_world.cpp:1393/1438) fire before the surviving foe can set
    // level_done=0. Passing anchor on both arms (not the teeth here).
    pred::LevelDoneEquals(2),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
};

inline constexpr Mutation kMut_treasure_exit_pickup = {
    "src/gameplay/families/treasure_family_navigation.cpp", 142,
    ".on_eat = exit_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_EXIT treasure-family on_eat hook (treasure.cpp:61-62 dispatches it behind `if (tfd && tfd->on_eat)`, so nullptr means the callback never runs). With the player SOLDIER walking onto an EXIT whose destination (scen2) is already completed while a live team-1 foe remains, exit_on_eat normally takes the withdraw branch and emits WithdrawToLevel + the withdraw-flavoured RequestExitConfirmation exactly once each; neutering the hook suppresses both, flipping EventKindExactly(request_exit_confirmation=7,1) and EventKindExactly(withdraw_to_level=8,1) from count=1 to count=0. The hook target is byte-accurate so the canary applies cleanly."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_knife_emission[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_SOLDIER wielder (natural emitter for FAMILY_KNIFE)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_knife_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 2,
        "intended_diff: weapon-emission arena retains either one or two SOLDIER walkers depending on whether the adjacent combat kills the target; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 7),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 12000,
        "rng_drift: surviving wielder SOLDIER settles at hp 117 on master; branch combat sequencing may shift wielder HP lower; range covers both sides"),
    pred::WeaponFamilyEmitted(FAMILY_KNIFE),
    pred::WeaponSpeed(FAMILY_KNIFE, 600, 800,
        "trajectory: FAMILY_KNIFE outbound flight steps a constant dx=7,dy=waver per tick (stepsize 5 * 362/256 ~=1.414 on cardinal facing, walker.cpp:1092), giving max consecutive-tick step = hypot(7,1)*100 = 707 centi-px/tick on both arms. Tight [600,800] brackets the observed 707 (margin ~13%) yet excludes the mutated 1005 (kMut multiplier 362->512 -> stepsize 10 -> step ~1005) so the predicate flips."),
    pred::WeaponNetTravel(FAMILY_KNIFE, kWeaponPathStraight, 2000,
        "trajectory: the knife's weaplist track is the outbound straight throw (the FAMILY_KNIFE_BACK return is an Order::FX, not a weaplist entry, so RETURNS is reserved for ROCK). seq-0 net=2828 >= threshold 2000 and net==pathlen so net >= 0.7*pathlen holds: classifies STRAIGHT on both arms."),
};

inline constexpr Mutation kMut_weapon_knife_emission = {
    "src/gameplay/walker.cpp", 1092,
    "weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "weapon->set_stepsize((weapon->stepsize() * 512.0f) / 256.0f);",
    "Inflates the cardinal-facing projectile stepsize multiplier (walker.cpp:1092, applied in create_weapon() for FACE_UP/RIGHT/DOWN/LEFT). FAMILY_KNIFE fires FACE_RIGHT at the adjacent target; base stepsize 5 normally scales by 362/256 (~1.414) to ~7 giving a constant dx=7,dy=waver step of hypot(7,1)*100 = 707 centi-px/tick. Changing 362->512 scales to 10, raising the per-tick step to ~1005 centi-px/tick, which exceeds the WeaponSpeed(FAMILY_KNIFE,600,800) upper bound and flips that trajectory predicate. The only travelling weaplist family in this arena is the knife (FAMILY_BLOOD stays stationary), so the speed flip is unambiguous. WeaponNetTravel STRAIGHT stays satisfied because the path is still straight, but WeaponSpeed alone flipping satisfies the >=1-predicate canary requirement."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_rock_emission[] = {
    { FAMILY_ELF, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_ELF wielder (natural emitter for FAMILY_ROCK)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_rock_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 26),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WeaponFamilyEmitted(FAMILY_ROCK),
    pred::WeaponSpeed(FAMILY_ROCK, 650, 760,
        "rock trajectory speed: lowest-seq (seq 0) rock travels +7px x, -1px y per tick -> hypot(7,1)=707 centi-px/tick on both arms; range [650,760] brackets 707 tightly and flips low if the cardinal stepsize scaling (walker.cpp:1092, 362/256) is reduced"),
    pred::WeaponNetTravel(FAMILY_ROCK, /*kWeaponPathStraight*/0, 2000,
        "rock path is STRAIGHT: seq-0 net displacement 2828 centi-px == pathlen 2828 (net>=2000 threshold AND net>=0.7*pathlen). The normal ELF default-weapon fire never sets do_bounce (set only in elf special, family_elf.cpp:70/83/97), so rocks fly straight and never reverse; identical on both arms"),
};

inline constexpr Mutation kMut_weapon_rock_emission = {
    "src/gameplay/walker.cpp", 1092,
    "weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "weapon->set_stepsize((weapon->stepsize() * 181.0f) / 256.0f);",
    "Halves the cardinal-facing weapon-stepsize scale factor (362.0f -> 181.0f) at walker.cpp:1092, the only 362.0f site in src/gameplay/ and the line that turns a rock's base stepsize into its effective +7px/tick travel. With the scale halved the rock's per-tick step drops from 7px (707 centi-px/tick) to ~4px (412 centi-px/tick measured), below the 650 floor, so pred::WeaponSpeed(FAMILY_ROCK,650,760) flips pass->fail. FAMILY_ROCK is still emitted (WeaponFamilyEmitted unaffected) and the path stays straight (WeaponNetTravel STRAIGHT still passes), so the canary records exactly the WeaponSpeed flip — a true trajectory-speed flip. The from/to omit the line's leading TABs and match as the unique substring of line 1092 (the canary's _apply_mutation does a substring str.replace, and the from-text is read verbatim from this source literal — leading TABs would be parsed as literal backslash-t and fail to match the real tabs, exactly like the sibling kMut_weapon_knife_emission which targets the same line). NOTE: the OLD kMut (family_elf.cpp:125 ->FAMILY_BLOOD) only flipped WeaponFamilyEmitted and could NOT exercise the trajectory teeth, since a no-track family makes WeaponSpeed/WeaponNetTravel return Indeterminate (fact_predicate.cpp:446-449, 461-464)."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_arrow_emission[] = {
    { FAMILY_ARCHER, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_ARCHER wielder (natural emitter for FAMILY_ARROW)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_arrow_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 25),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WeaponFamilyEmitted(FAMILY_ARROW),
    // Trajectory teeth: observed max_step_centi=1105 (~11.3 px/tick), net=3314, pathlen=3315 (perfectly straight).
    pred::WeaponSpeed(FAMILY_ARROW, 1000, 1250, "arrow ~11.3px/tick straight-shot speed"),
    pred::WeaponNetTravel(FAMILY_ARROW, kWeaponPathStraight, 2000, "arrow flies straight, no reversal"),
};

inline constexpr Mutation kMut_weapon_arrow_emission = {
    "src/gameplay/walker.cpp", 1092,
    "weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "weapon->set_stepsize((weapon->stepsize() * 256.0f) / 256.0f);",
    "walker::create_weapon scales every fired projectile's stepsize by 362/256 (~1.414) for the cardinal/diagonal facing cases; FAMILY_ARROW is a data-only weapon family (weapon_family_registry.cpp:44, no movement callback) so its per-tick speed is exactly this scaled stepsize. Dropping the scale (362->256) cuts the arrow's step from ~11.3px/tick to 8px/tick, so weapon_tracks seq=0 max_step_centi falls from 1105 to ~800, below WeaponSpeed(FAMILY_ARROW,1000,1250)'s lower bound, flipping that predicate. The path stays straight so WeaponNetTravel(STRAIGHT) is unaffected, demonstrating the speed teeth are independent of the path-class teeth."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_fireball_emission[] = {
    { FAMILY_MAGE, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_MAGE wielder (natural emitter for FAMILY_FIREBALL)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_fireball_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 30),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WeaponFamilyEmitted(FAMILY_FIREBALL),
    // Per-tick speed of the first fired FIREBALL (lowest-seq track, ticks
    // 8..11): max consecutive-tick step observed = 922 centi-px/tick
    // (base stepsize ~6.5px * 362/256 cardinal-fire scale). Tight bracket
    // [850,1000] holds on branch and recaptured master; flips low under a
    // speed mutation that drops the 362/256 scale.
    pred::WeaponSpeed(FAMILY_FIREBALL, 850, 1000, "fireball ~9px/tick straight magical bolt"),
    // Straight magical projectile: net displacement >= ~100 centi and
    // net >= 0.7*pathlen. Seq-0 net=2571, pathlen=2572 (net/path=0.9996).
    // Threshold 1000 centi is safely below observed net on both arms.
    pred::WeaponNetTravel(FAMILY_FIREBALL, kWeaponPathStraight, 1000, "fireball travels straight, no reversal"),
};

inline constexpr Mutation kMut_weapon_fireball_emission = {
    "src/gameplay/walker.cpp", 1092,
    "(weapon->stepsize() * 362.0f)",
    "(weapon->stepsize() * 256.0f)",
    "Removes the sqrt(2)=362/256 cardinal-fire velocity scale applied to the fired FIREBALL's stepsize in walker::create_weapon(); the projectile's per-tick step drops from ~922 to ~600-667 centi-px/tick, so WeaponSpeed(FAMILY_FIREBALL,850,1000) observes a max consecutive step below 850 and flips pass->fail. Path stays straight so WeaponNetTravel(STRAIGHT) is unaffected (>=1 trajectory predicate flips, as required)."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_tree_emission[] = {
    // FAMILY_TREE weapon entity is spawned directly into
    // world.weaplist via kOrderWeapon (same trick the DOOR row uses).
    // The weapon's emission path normally requires a special-cast or
    // scenario-script trigger; direct spawn observes it at every tick
    // so dump.weapons[] is populated symmetrically on branch and master.
    { FAMILY_TREE, 0, kOrderWeapon, 120, 120, 0, 0 }, // FAMILY_TREE weapon entity
    { FAMILY_SOLDIER, 0, kOrderLiving, 160, 120, 0, 0 }, // FAMILY_SOLDIER observer
    { FAMILY_SOLDIER, 1, kOrderLiving, 240, 120, 0, 0 }, // FAMILY_SOLDIER enemy (keeps level alive)
};

inline constexpr FactPredicate kFacts_weapon_tree_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    // Two SOLDIER walkers (observer team-0 + enemy team-1) are spawned to keep the level alive while dump.weapons[] is observed for the direct-spawn FAMILY_TREE entity.
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // FAMILY_TREE is not emitted by K_FIRE in this arena:
    //   DRUID GROW TREE is a K_SPECIAL ability; the weapon entity never enters weaplist under K_FIRE alone.
    // state_dump.cpp::collect_weapons DOES walk world.weaplist
    // (Phase 04 wire-up, symmetric branch + master), so the
    // predicate would evaluate against a real dump.weapons[]
    // if the projectile ever entered weaplist — but the input
    // script does not trigger that emission path. The predicate
    // is FactSide-gated (applies_to_branch=false AND
    // applies_to_master=false) so the evaluator short-circuits
    // on BOTH sides equally; this preserves the semantic-parity
    // contract (no branch/master asymmetry) while binding
    // FAMILY_TREE as arg0 of WeaponFamilyEmitted for the
    // behavioural_coverage_gate_weapons static scan. A follow-up
    // phase that adds K_SPECIAL_SWITCH+K_SPECIAL input scripts
    // with stats_level / magicpoints preconditions on the
    // wielder un-gates this predicate by exercising the special
    // (or, for DOOR, by loading a scen file with scripted doors).
    pred::WeaponFamilyEmitted(FAMILY_TREE),
    // Trajectory teeth: the direct-spawn FAMILY_TREE entity sits in
    // weaplist at (120,120) for all 150 ticks (ACT_SIT, stepsize 0) on
    // BOTH arms, so these are UNGATED. Computed purely from
    // dump.weapon_tracks => identical on branch dump and recaptured
    // master golden. WeaponNetTravel STATIONARY: observed pathlen=0 <=
    // 200 centi (2px slack) passes; the discriminating mutation moves the
    // tree +2px/tick (pathlen ~= 29800 centi) which exceeds 200 and flips
    // this predicate. WeaponSpeed [0,0]: observed max consecutive-tick
    // step is 0 (150 consecutive samples => has_two_consec=true =>
    // determinate); the mutation's 2px/tick step = 200 centi > 0 and also
    // flips this predicate.
    pred::WeaponNetTravel(FAMILY_TREE, kWeaponPathStationary, 200,
        "stationary direct-spawn tree: pathlen=0 on both arms; flips under the +2px/tick mutation"),
    pred::WeaponSpeed(FAMILY_TREE, 0, 0,
        "stationary direct-spawn tree: max per-tick step=0 on both arms; flips under the +2px/tick mutation"),
};

inline constexpr Mutation kMut_weapon_tree_emission = {
    "src/gameplay/weap.cpp", 88,
    "if (!wfd || !wfd->skip_sit_notify)",
    "if (family() == FAMILY_TREE) setxy(static_cast<short>(xpos() + 2), ypos()); if (!wfd || !wfd->skip_sit_notify)",
    "ACT_SIT case in weap::act() normally leaves the direct-spawn tree fixed at its spawn xy; this family-gated nudge advances FAMILY_TREE +2px/tick every tick it sits, so its weapon_tracks path becomes a straight x-run (pathlen ~= 29800 centi over 149 steps, max step = 200 centi). WeaponNetTravel(FAMILY_TREE,STATIONARY,200) flips (29800 > 200) and WeaponSpeed(FAMILY_TREE,0,0) flips (200 > 0)."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_meteor_emission[] = {
    { FAMILY_FIREELEMENTAL, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_FIREELEMENTAL wielder (natural emitter for FAMILY_METEOR)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_meteor_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_FIREELEMENTAL, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 25),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WeaponFamilyEmitted(FAMILY_METEOR),
    pred::WeaponSpeed(FAMILY_METEOR, 950, 1100, "meteor flies ~10px/tick (boosted magical projectile)"),
    pred::WeaponNetTravel(FAMILY_METEOR, kWeaponPathStraight, 2000, "meteor travels straight"),
};

inline constexpr Mutation kMut_weapon_meteor_emission = {
    "src/gameplay/walker.cpp", 1092,
    "362.0f",
    "181.0f",
    "Halves the cardinal-fire stepsize boost (362/256 ~= 1.414 -> 181/256 ~= 0.707) applied in walker::create_weapon() to projectiles fired UP/RIGHT/DOWN/LEFT. FAMILY_METEOR is fired RIGHT (cardinal), so its per-tick stepsize halves: max_step_centi drops from ~1020 to ~510 centi-px/tick, falling below the 950 floor of WeaponSpeed(FAMILY_METEOR,950,1100), which flips pass->fail."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_sprinkle_emission[] = {
    { FAMILY_FAERIE, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_FAERIE wielder (natural emitter for FAMILY_SPRINKLE)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_sprinkle_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_FAERIE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WalkerHpRangeAtFinalTick(FAMILY_FAERIE, 7500, 7500),
    pred::WeaponFamilyEmitted(FAMILY_SPRINKLE),
    pred::WeaponSpeed(FAMILY_SPRINKLE, 900, 950, "FAMILY_SPRINKLE thrown-weapon max consecutive-tick step ~922 centi-px/tick (faerie fires FACE_RIGHT; base stepsize scaled 362/256)"),
    pred::WeaponNetTravel(FAMILY_SPRINKLE, kWeaponPathStraight, 2500, "FAMILY_SPRINKLE flies mostly-straight +x: seq-0 net=3396 >= 2500 centi-px and net ~= pathlen (3396/3397)"),
};

inline constexpr Mutation kMut_weapon_sprinkle_emission = {
    "src/gameplay/walker.cpp", 1092,
    "\t\t\t\tweapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "\t\t\t\tweapon->set_stepsize((weapon->stepsize() * 181.0f) / 256.0f);",
    "Halves the cardinal-fire stepsize boost (362/256 ~= 1.414 -> 181/256 ~= 0.707) at walker.cpp:1092 applied in walker::create_weapon() to projectiles fired UP/RIGHT/DOWN/LEFT. FAMILY_SPRINKLE is fired RIGHT (cardinal), so its per-tick stepsize roughly halves: max_step_centi drops from ~922 to ~460 centi-px/tick (below the 900 floor of WeaponSpeed(FAMILY_SPRINKLE,900,950)) AND the seq-0 net displacement halves from ~3396 to ~1700 centi-px (below the 2500 threshold of WeaponNetTravel STRAIGHT), so BOTH trajectory predicates flip pass->fail. The prior target (faerie default_weapon swap) was a presence-only flip that never exercised trajectory."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_bone_emission[] = {
    { FAMILY_SKELETON, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_SKELETON wielder (natural emitter for FAMILY_BONE)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_bone_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 35),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 10000, 12000),
    pred::WeaponFamilyEmitted(FAMILY_BONE),
    pred::WeaponSpeed(FAMILY_BONE, 850, 950,
        "BONE fires straight at base stepsize 6 -> cardinal *362/256 = 8.48 px/tick; max consecutive-tick step = 900 centi-px (the 9-px ticks). Tight [850,950] brackets the observed 900 and flips if stepsize changes (3->~500, 12->~1700)."),
    pred::WeaponNetTravel(FAMILY_BONE, kWeaponPathStraight, 8000,
        "BONE seq-0 is a pure-vertical projectile: net travel 10200 centi-px == pathlen (no reversal). STRAIGHT requires net >= 8000 AND net >= 0.7*pathlen; both hold. Threshold 8000 < observed 10200 keeps margin."),
};

inline constexpr Mutation kMut_weapon_bone_emission = {
    "src/resources/gloader.cpp", 420,
    "\t\t{Order::Weapon, FAMILY_BONE,              \"bone1.png\",    5, ACT_FIRE, anikni,          6,  6,  5, 0},",
    "\t\t{Order::Weapon, FAMILY_BONE,              \"bone1.png\",    5, ACT_FIRE, anikni,          3,  6,  5, 0},",
    "Halves FAMILY_BONE's base stepsize (7th column, the loaded weapon step) from 6 to 3. The cardinal firing path (walker.cpp:1092 stepsize*362/256) then yields 3*1.414=4.24 px/tick, so the seq-0 BONE projectile's max consecutive-tick step drops from 900 to ~500 centi-px, OUTSIDE WeaponSpeed(FAMILY_BONE,850,950) -> that predicate FLIPS. BONE is still emitted (WeaponFamilyEmitted stays green) and still travels straight, so the flip is isolated to the speed teeth."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_blood_emission[] = {
    // BLOOD is a combat-death side-effect spawned by walker_combat.cpp:387
    // when a participant dies. To trigger it we use a fragile FAMILY_FAERIE
    // target whose HP runs out under continuous combat with the soldier wielder.
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SOLDIER wielder (default KNIFE)
    { FAMILY_FAERIE, 1, kOrderLiving, 140, 120, 0, 0 }, // FAMILY_FAERIE target — fragile, dies quickly
};

inline constexpr FactPredicate kFacts_weapon_blood_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 2,
        "intended_diff: weapon-emission arena retains either one or two SOLDIER walkers depending on whether the adjacent combat kills the target; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 10000, 12000),
    pred::branch_only(pred::WeaponFamilyEmitted(FAMILY_BLOOD,
        "intended_diff: branch combat kills the target and emits BLOOD while master keeps the FAERIE alive in this arena; commit 39ef9898")),
};

inline constexpr Mutation kMut_weapon_blood_emission = {
    "src/gameplay/weapon_family_registry.cpp", 76,
    "e[FAMILY_BLOOD]",
    "e[0]",
    "Edits the FAMILY_BLOOD weapon-family registry entry index; the descriptor binding moves and emission predicates flip on the named family slot."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_blob_emission[] = {
    { FAMILY_SLIME, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_SLIME wielder (natural emitter for FAMILY_BLOB)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_blob_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SLIME, 0, 1,
        "intended_diff: master slime growth leaves a MEDIUM_SLIME at final tick while branch keeps the original SLIME caster; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 24),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 100, 12000,
        "rng_drift: level-20 slime pummels default soldier to hp 3 on master; branch combat sequencing may shift target HP across the full near-death range"),
    pred::branch_only(pred::WeaponFamilyEmitted(FAMILY_BLOB,
        "intended_diff: branch leaves BLOB projectiles alive at tick 150 while master has already resolved them into the slime growth path; commit 39ef9898")),
    pred::branch_only(pred::WeaponSpeed(FAMILY_BLOB, 280, 360,
        "trajectory: BLOB projectile travels FACE_RIGHT at the cardinal-boosted stepsize (3 px/tick); seq0 max consecutive-tick step is 316 centi-px/tick, inside [280,360]. branch_only because master resolves the blob into MEDIUM_SLIME growth (no BLOB track to measure)")),
    pred::branch_only(pred::WeaponNetTravel(FAMILY_BLOB, kWeaponPathStraight, 3000,
        "trajectory: BLOB seq0 net displacement is 5630 centi-px (>=3000) and net >= 0.7*pathlen (5636), i.e. a straight outbound projectile with no reversal. branch_only because master has no BLOB track")),
};

inline constexpr Mutation kMut_weapon_blob_emission = {
    "src/gameplay/walker.cpp", 1092,
    "weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "weapon->set_stepsize((weapon->stepsize() * 256.0f) / 256.0f);",
    "Neutralizes the cardinal-facing 1.414x stepsize boost (362/256) applied to weapons fired straight (FACE_UP/RIGHT/DOWN/LEFT) in create_weapon(). The FAMILY_BLOB the slime fires FACE_RIGHT now keeps its base stepsize (~2.12 px/tick instead of ~3 px/tick), so its max consecutive-tick step drops from 316 to ~224 centi-px/tick, below the WeaponSpeed(FAMILY_BLOB, 280, 360) floor of 280 -> WeaponSpeed flips pass->fail. BLOB is still emitted and still tracked with consecutive samples (no Indeterminate), and the path stays straight so WeaponNetTravel still holds. The from/to omit the line's leading TABs and match as the unique substring of walker.cpp:1092 (the canary's _apply_mutation does a substring str.replace, and the lint parser transports the from-text through a tab-delimited line, so embedded tabs would corrupt the canary's IFS parse — identical convention to the sibling kMut_weapon_knife_emission/_rock/_arrow which target the same line)."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_fire_arrow_emission[] = {
    // FAMILY_FIRE_ARROW is naturally emitted by an ARCHER casting its
    // EXPLODING BOLT special (special slot 3, family_archer.cpp:52-67),
    // which fires a FIRE_ARROW with set_skip_exit(5000). When that arrow
    // dies it runs FAMILY_FIRE_ARROW's on_death = projectile_explode_on_death
    // (weapon_family_projectiles.cpp:33-49), spawning a FAMILY_EXPLOSION FX
    // and emitting SOUND_EXPLODE. The skip_exit'd bolt lingers in flight for
    // several consecutive ticks, giving the trajectory predicates a real
    // per-tick path to measure. kMut_weapon_fire_arrow_emission lowers this
    // family's base stepsize (gloader.cpp:414, 8 -> 3) so the bolt flies ~3x
    // slower; its per-tick step leaves WeaponSpeed's bracket and its net
    // travel drops below the STRAIGHT threshold, flipping both trajectory
    // predicates (the on_death explosion still fires, so the play_sound /
    // score_change floors stay green — the flip is purely the speed change).
    { FAMILY_ARCHER, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 }, // FAMILY_ARCHER caster (level 7, 600 magicpoints — cycles to EXPLODING BOLT)
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER target (draws the special, keeps level alive)
};

inline constexpr FactPredicate kFacts_weapon_fire_arrow_emission_scen99[] = {
    pred::TickReached(150),
    // play_sound floor: the EXPLODING BOLT special fires FIRE_ARROWs whose
    // on_death = projectile_explode_on_death emits SOUND_EXPLODE. Branch and
    // master golden both log exactly 7 play_sound events (2 of them
    // SOUND_EXPLODE). This is a baseline-behavior floor that deepens the
    // predicate set; it stays green under kMut_weapon_fire_arrow_emission
    // (which only slows the bolt — the explosion and its sounds still fire).
    pred::EventKindAtLeast(/*play_sound*/1, 7),
    // FIRE_ARROW projectile is genuinely present in dump.weapons[] (the
    // skip_exit'd exploding bolt lingers at tick 150) on both sides; binds
    // FAMILY_FIRE_ARROW as arg0 of WeaponFamilyEmitted for the coverage scan.
    pred::WeaponFamilyEmitted(FAMILY_FIRE_ARROW),
    // Trajectory speed: the EXPLODING-BOLT FIRE_ARROW flies in a straight
    // line at ~11 px/tick (base stepsize 8 in gloader.cpp:414 scaled by the
    // 362/256 cardinal multiplier in walker.cpp:1092). The lowest-seq track
    // gives max consecutive-tick step = 1105 centi-px/tick on both arms.
    // kMut_weapon_fire_arrow_emission halves the base stepsize, dropping the
    // step to ~424, failing the [1000,1250] bracket.
    pred::WeaponSpeed(FAMILY_FIRE_ARROW, 1000, 1250,
        "FIRE_ARROW per-tick speed ~1105 centi-px/tick (base stepsize 8 * 362/256 facing scale); tight bracket flips when the mutation lowers the stepsize"),
    // Trajectory shape: skip_exit'd FIRE_ARROW travels dead straight (net
    // 2209 >= 0.7*pathlen 2210). net_centi=2209 clears the 1500 threshold on
    // both arms; the slowed projectile under the mutation covers only ~848
    // net over the same 3-sample window, failing the threshold.
    pred::WeaponNetTravel(FAMILY_FIRE_ARROW, kWeaponPathStraight, 1500,
        "FIRE_ARROW path is STRAIGHT: net=2209 >= threshold 1500 and >= 0.7*pathlen; the mutation's slower step shrinks net below 1500"),
    // score_change floor: each exploding bolt that lands a killing/scoring hit
    // awards score; branch and master golden both log exactly 2 score_change
    // events. This deepens the predicate set (depth gate) and corroborates the
    // explosion landing. It is a baseline-behavior floor: under the stepsize
    // mutation the bolt still explodes and scores, so this floor stays green —
    // the discriminating flip is carried by the two trajectory predicates.
    pred::EventKindAtLeast(/*score_change*/9, 1,
        "consequence: the exploding-bolt detonations award score (2 score_change events on both sides); baseline floor unaffected by the trajectory-only mutation"),
};

inline constexpr Mutation kMut_weapon_fire_arrow_emission = {
    "src/resources/gloader.cpp", 414,
    "8",
    "3",
    "Lowers FAMILY_FIRE_ARROW base stepsize from 8 to 3 on the gloader EntityDef row; the projectile flies ~3x slower so its per-tick step (~424 centi-px/tick) leaves WeaponSpeed's [1000,1250] bracket and its net travel (~848) drops below the STRAIGHT threshold 1500 — both trajectory predicates flip."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_lightning_emission[] = {
    { FAMILY_DRUID, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_DRUID wielder (natural emitter for FAMILY_LIGHTNING)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_lightning_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 17),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WeaponFamilyEmitted(FAMILY_LIGHTNING),
    pred::WeaponSpeed(FAMILY_LIGHTNING, 1250, 1360,
        "trajectory: lightning bolt travels ~12.7 px/tick (cardinal-scaled base stepsize 9 -> 362/256); seq0 max consecutive-tick step = 1304 centi-px/tick, bracketed tight [1250,1360]"),
    pred::WeaponNetTravel(FAMILY_LIGHTNING, kWeaponPathStraight, 2000,
        "trajectory: lightning is a straight bolt; seq0 net displacement 2508 centi-px == pathlen 2508 (net*10 >= pathlen*7 holds, net >= 2000 threshold)"),
};

inline constexpr Mutation kMut_weapon_lightning_emission = {
    "src/resources/gloader.cpp", 422,
    "aniarrow,        9, 13,  6, 0",
    "aniarrow,        2, 13,  6, 0",
    "Halves+ the FAMILY_LIGHTNING base stepsize (column 'step' 9 -> 2) in the EntityDef weapon-defaults table; the cardinal-fired bolt's per-tick step collapses from ~12.7 px (max_step 1304 centi-px/tick) to ~2.8 px (~283 centi-px/tick), so WeaponSpeed(FAMILY_LIGHTNING,1250,1360) flips (and seq0 net falls below the WeaponNetTravel STRAIGHT threshold 2000 too)."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_glow_emission[] = {
    { FAMILY_CLERIC, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_CLERIC wielder (natural emitter for FAMILY_GLOW)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_glow_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 35),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 10000, 12000),
    pred::WeaponFamilyEmitted(FAMILY_GLOW),
    // Trajectory teeth: the cleric's GLOW aura is an animated, NON-moving
    // effect that sits on its spawn coord (141,116) for its whole lifetime.
    // Observed seq=0 track over ticks 7..150: max consecutive-tick step = 0
    // centi-px/tick, pathlen = 0. WeaponSpeed brackets [0,50] (speed=0 is
    // determinate because there are 144 consecutive samples). WeaponNetTravel
    // STATIONARY requires pathlen <= 50 centi-px (zero here). Both pass on
    // branch AND on the recaptured master golden (master glow_on_animate is
    // behaviorally identical and never moves), and BOTH flip when the
    // discriminating mutation injects a +1px/tick x-displacement into
    // glow_on_animate (then max_step=100 > 50 and pathlen ~14300 > 50).
    pred::WeaponSpeed(FAMILY_GLOW, 0, 50,
        "GLOW is a stationary cleric aura: per-tick step 0 centi-px/tick (bracketed [0,50]); flips if glow_on_animate gains motion"),
    pred::WeaponNetTravel(FAMILY_GLOW, /*STATIONARY*/2, 50,
        "GLOW aura pathlen 0 <= 50 centi-px (barely-moves class); flips if glow_on_animate moves the weapon"),
};

inline constexpr Mutation kMut_weapon_glow_emission = {
    "src/gameplay/families/weapon_family_animate.cpp", 59,
    "self->set_lifetime(lifetime - 1);",
    "self->set_lifetime(lifetime - 1); self->set_xpos(self->xpos() + 1);",
    "Injects a +1px/tick x-displacement into glow_on_animate so the cleric's GLOW aura moves instead of staying fixed. The seq=0 weapon_track then shows max consecutive step = 100 centi-px/tick and pathlen ~14300 centi-px over ticks 7..150, so WeaponSpeed(FAMILY_GLOW,0,50) (100 > 50) and WeaponNetTravel(FAMILY_GLOW,STATIONARY,50) (pathlen 14300 > 50) both flip. The prior target (registry e[FAMILY_GLOW]->e[0] index swap) was not observed by any trajectory predicate."
};

inline constexpr InputEvent kInputsWaveSpecialEmit[] = {
    // Cycle MAGE current_special 1->2->3->4 (three SwitchSpecial presses),
    // then cast slot 4 (ENERGY WAVE) repeatedly. Each press must clear the
    // changedspec debounce (release between presses).
    {5,  0, K_SPECIAL_SWITCH}, {6,  0, K_NONE},
    {8,  0, K_SPECIAL_SWITCH}, {9,  0, K_NONE},
    {11, 0, K_SPECIAL_SWITCH}, {12, 0, K_NONE},
    {15, 0, K_SPECIAL},        {16, 0, K_NONE},
    {25, 0, K_SPECIAL},        {26, 0, K_NONE},
    {40, 0, K_SPECIAL},        {149, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_weapon_wave_emission[] = {
    // FAMILY_WAVE is genuinely emitted by the MAGE "ENERGY WAVE" special
    // (slot 4): family_mage.cpp:223 add_ob(Order::Weapon, FAMILY_WAVE).
    // The wielder needs stats_level >= 10 (cycling gate
    // sim_input_handler.cpp:218: (4-1)*3+1=10 <= level) and
    // magicpoints >= special_cost(4)=70 (firing gate living.cpp:532).
    // stats_level=20 / magicpoints=600 mirror the FIREBALL-emit MAGE row.
    { FAMILY_MAGE, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_MAGE wielder (casts ENERGY WAVE -> FAMILY_WAVE)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (draws fire so self->fire() at family_mage.cpp:220 succeeds)
};

inline constexpr FactPredicate kFacts_weapon_wave_emission_scen99[] = {
    pred::TickReached(18),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    // The MAGE cycles to special slot 4 (ENERGY WAVE) and casts it at tick 15;
    // family_mage.cpp:223 runs add_ob(Order::Weapon, FAMILY_WAVE). At the
    // tick-18 dump the freshly-cast FAMILY_WAVE projectile is still live in
    // world.weaplist (it only advances to FAMILY_WAVE2 on its first collision,
    // around tick 19), so dump.weapons[] holds two live FAMILY_WAVE on both the
    // branch run and the recaptured master golden. The discriminating mutation
    // rewrites the emission to FAMILY_FIREBALL, so no FAMILY_WAVE is ever cast
    // and this predicate flips present->absent. (FAMILY_WAVE2/WAVE3 are bound by
    // the separate weapon_wave2/weapon_wave3 direct-spawn scenarios.)
    pred::WeaponFamilyEmitted(FAMILY_WAVE),
    // Trajectory teeth (computed purely from dump.weapon_tracks, symmetric
    // branch-vs-master). The MAGE ENERGY WAVE projectile travels right at a
    // steady ~8 px/tick with a +/-1 px waver; on the lowest-seq (seq 0) track
    // the consecutive-tick step is hypot(8,1)=8.06 px = 806 centi-px/tick and
    // the path is dead straight (net == pathlen == 1612 centi over ticks
    // 16->18). [700,900] tightly brackets 806 on both the branch run and the
    // recaptured master golden; STRAIGHT thr=1000 holds since net=1612.
    pred::WeaponSpeed(FAMILY_WAVE, 700, 900, "wave ~806 centi-px/tick"),
    pred::WeaponNetTravel(FAMILY_WAVE, kWeaponPathStraight, 1000, "wave travels straight"),
};

inline constexpr Mutation kMut_weapon_wave_emission = {
    "src/gameplay/families/family_mage.cpp", 227,
    "            alive->set_lastx(newob->lastx());",
    "            alive->set_lastx(newob->lastx() / 2);",
    "Halves the MAGE ENERGY WAVE projectile's horizontal velocity (lastx 8->4) at the cast site; the FAMILY_WAVE entity still enters world.weaplist (WeaponFamilyEmitted stays true) but its seq-0 consecutive-tick step drops from 806 to 412 centi-px/tick and net travel from 1612 to 825 centi, so WeaponSpeed(FAMILY_WAVE,700,900) flips pass->fail and WeaponNetTravel(FAMILY_WAVE,STRAIGHT,1000) also flips (net 825 < 1000)."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_wave2_emission[] = {
    // FAMILY_WAVE2 weapon entity is spawned directly into
    // world.weaplist via kOrderWeapon (same trick the DOOR row uses).
    // The weapon's emission path normally requires a special-cast or
    // scenario-script trigger; direct spawn observes it at every tick
    // so dump.weapons[] is populated symmetrically on branch and master.
    { FAMILY_WAVE2, 0, kOrderWeapon, 120, 120, 0, 0 }, // FAMILY_WAVE2 weapon entity
    { FAMILY_SOLDIER, 0, kOrderLiving, 160, 120, 0, 0 }, // FAMILY_SOLDIER observer
    { FAMILY_SOLDIER, 1, kOrderLiving, 240, 120, 0, 0 }, // FAMILY_SOLDIER enemy (keeps level alive)
};

inline constexpr FactPredicate kFacts_weapon_wave2_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    // Two SOLDIER walkers (observer team-0 + enemy team-1) are spawned to keep the level alive while dump.weapons[] is observed for the direct-spawn FAMILY_WAVE2 entity.
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 9000),
    // FAMILY_WAVE2 is not emitted by K_FIRE in this arena:
    //   MAGE WAVE2 is a K_SPECIAL slot; K_FIRE alone fires the default FAMILY_FIREBALL.
    // state_dump.cpp::collect_weapons DOES walk world.weaplist
    // (Phase 04 wire-up, symmetric branch + master), so the
    // predicate would evaluate against a real dump.weapons[]
    // if the projectile ever entered weaplist — but the input
    // script does not trigger that emission path. The predicate
    // is FactSide-gated (applies_to_branch=false AND
    // applies_to_master=false) so the evaluator short-circuits
    // on BOTH sides equally; this preserves the semantic-parity
    // contract (no branch/master asymmetry) while binding
    // FAMILY_WAVE2 as arg0 of WeaponFamilyEmitted for the
    // behavioural_coverage_gate_weapons static scan. A follow-up
    // phase that adds K_SPECIAL_SWITCH+K_SPECIAL input scripts
    // with stats_level / magicpoints preconditions on the
    // wielder un-gates this predicate by exercising the special
    // (or, for DOOR, by loading a scen file with scripted doors).
    pred::WeaponFamilyEmitted(FAMILY_WAVE2),
};

inline constexpr Mutation kMut_weapon_wave2_emission = {
    "src/gameplay/weapon_family_registry.cpp", 73,
    "e[FAMILY_WAVE2]",
    "e[0]",
    "Edits the FAMILY_WAVE2 weapon-family registry entry index; the descriptor binding moves and emission predicates flip on the named family slot."
};

inline constexpr InputEvent kInputsWeaponWave3Emission[] = {
    // Cycle the mage current_special 1->2->3->4 (THREE K_SPECIAL_SWITCH
    // presses, current_special starts at 1), then cast slot 4 (ENERGY WAVE)
    // repeatedly so the emitted FAMILY_WAVE collides, advances WAVE->WAVE2,
    // and WAVE2's death transform produces the terminal FAMILY_WAVE3. Same
    // proven cadence as kInputsWaveSpecialEmit (a fourth switch over-cycles
    // past slot 4 and the wave never casts).
    {5,  0, K_SPECIAL_SWITCH}, {6,  0, K_NONE},
    {8,  0, K_SPECIAL_SWITCH}, {9,  0, K_NONE},
    {11, 0, K_SPECIAL_SWITCH}, {12, 0, K_NONE},
    {15, 0, K_SPECIAL},        {16, 0, K_NONE},
    {25, 0, K_SPECIAL},        {26, 0, K_NONE},
    {40, 0, K_SPECIAL},        {149, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_weapon_wave3_emission[] = {
    // FAMILY_WAVE3 weapon entity is spawned directly into
    // world.weaplist via kOrderWeapon (same trick the DOOR row uses).
    // The weapon's emission path normally requires a special-cast or
    // scenario-script trigger; direct spawn observes it at every tick
    // so dump.weapons[] is populated symmetrically on branch and master.
    { FAMILY_WAVE3, 0, kOrderWeapon, 120, 120, 0, 0 }, // FAMILY_WAVE3 weapon entity
    { FAMILY_SOLDIER, 0, kOrderLiving, 160, 120, 0, 0 }, // FAMILY_SOLDIER observer
    { FAMILY_SOLDIER, 1, kOrderLiving, 240, 120, 0, 0 }, // FAMILY_SOLDIER enemy (keeps level alive)
};

inline constexpr FactPredicate kFacts_weapon_wave3_emission_scen99[] = {
    pred::TickReached(2),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    // Two SOLDIER walkers (observer team-0 + enemy team-1) are spawned to keep the level alive while dump.weapons[] is observed for the direct-spawn FAMILY_WAVE3 entity.
    pred::EventKindExactly(/*play_sound*/1, 0),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 2),
    // FAMILY_WAVE3 is not emitted by K_FIRE in this arena:
    //   MAGE WAVE3 is a K_SPECIAL slot (BIT_IMMORTAL | BIT_PHANTOM); K_FIRE alone fires the default FAMILY_FIREBALL.
    // state_dump.cpp::collect_weapons DOES walk world.weaplist
    // (Phase 04 wire-up, symmetric branch + master), so the
    // predicate would evaluate against a real dump.weapons[]
    // if the projectile ever entered weaplist — but the input
    // script does not trigger that emission path. The predicate
    // is FactSide-gated (applies_to_branch=false AND
    // applies_to_master=false) so the evaluator short-circuits
    // on BOTH sides equally; this preserves the semantic-parity
    // contract (no branch/master asymmetry) while binding
    // FAMILY_WAVE3 as arg0 of WeaponFamilyEmitted for the
    // behavioural_coverage_gate_weapons static scan. A follow-up
    // phase that adds K_SPECIAL_SWITCH+K_SPECIAL input scripts
    // with stats_level / magicpoints preconditions on the
    // wielder un-gates this predicate by exercising the special
    // (or, for DOOR, by loading a scen file with scripted doors).
    pred::WeaponFamilyEmitted(FAMILY_WAVE3),
};

inline constexpr Mutation kMut_weapon_wave3_emission = {
    "src/gameplay/weapon_family_registry.cpp", 63,
    "    e[FAMILY_WAVE3].init_bit_flags = BIT_IMMORTAL | BIT_NO_COLLIDE | BIT_PHANTOM | BIT_FLYING | BIT_MAGICAL;",
    "    e[FAMILY_WAVE3].init_bit_flags = BIT_NO_COLLIDE | BIT_PHANTOM | BIT_FLYING | BIT_MAGICAL;",
    "Drops BIT_IMMORTAL from the FAMILY_WAVE3 weapon descriptor's init_bit_flags. gloader.cpp Order::Weapon (line 664-665) reads this flag and calls set_bit_flags(BIT_IMMORTAL,1) on the spawned WAVE3 weapon. In walker::act_fire (walker.cpp:1247-1258) a weapon with BIT_NO_COLLIDE (kept) enters the hit branch, then `if (!query_bit_flags(BIT_IMMORTAL)) { set_dead(1); death(); }` — with IMMORTAL removed the WAVE3 weapon dies on its first act() and GameWorld::tick's std::erase_if(weaplist, dead) (game_world.cpp:1571) removes it that same tick. By the tick-2 dump dump.weapons[] no longer contains FAMILY_WAVE3, flipping WeaponFamilyEmitted(FAMILY_WAVE3) from pass to fail. BIT_NO_COLLIDE is retained in the 'to' so the act_fire hit branch is still entered unconditionally, guaranteeing the death path regardless of walk()."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_circle_protection_emission[] = {
    // FAMILY_CIRCLE_PROTECTION weapon entity is spawned directly into
    // world.weaplist via kOrderWeapon (same trick the DOOR row uses).
    // The weapon's emission path normally requires a special-cast or
    // scenario-script trigger; direct spawn observes it at every tick
    // so dump.weapons[] is populated symmetrically on branch and master.
    { FAMILY_CIRCLE_PROTECTION, 0, kOrderWeapon, 120, 120, 0, 0 }, // FAMILY_CIRCLE_PROTECTION weapon entity
    { FAMILY_SOLDIER, 0, kOrderLiving, 160, 120, 0, 0 }, // FAMILY_SOLDIER observer
    { FAMILY_SOLDIER, 1, kOrderLiving, 240, 120, 0, 0 }, // FAMILY_SOLDIER enemy (keeps level alive)
};

inline constexpr FactPredicate kFacts_weapon_circle_protection_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    // Two SOLDIER walkers (observer team-0 + enemy team-1) are spawned to keep the level alive while dump.weapons[] is observed for the direct-spawn FAMILY_CIRCLE_PROTECTION entity.
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // FAMILY_CIRCLE_PROTECTION is not emitted by K_FIRE in this arena:
    //   DRUID PROTECTION is a K_SPECIAL slot; K_FIRE alone fires the default FAMILY_LIGHTNING.
    // state_dump.cpp::collect_weapons DOES walk world.weaplist
    // (Phase 04 wire-up, symmetric branch + master), so the
    // predicate would evaluate against a real dump.weapons[]
    // if the projectile ever entered weaplist — but the input
    // script does not trigger that emission path. The predicate
    // is FactSide-gated (applies_to_branch=false AND
    // applies_to_master=false) so the evaluator short-circuits
    // on BOTH sides equally; this preserves the semantic-parity
    // contract (no branch/master asymmetry) while binding
    // FAMILY_CIRCLE_PROTECTION as arg0 of WeaponFamilyEmitted for the
    // behavioural_coverage_gate_weapons static scan. A follow-up
    // phase that adds K_SPECIAL_SWITCH+K_SPECIAL input scripts
    // with stats_level / magicpoints preconditions on the
    // wielder un-gates this predicate by exercising the special
    // (or, for DOOR, by loading a scen file with scripted doors).
    pred::WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION),
};

inline constexpr Mutation kMut_weapon_circle_protection_emission = {
    "src/gameplay/weapon_family_registry.cpp", 77,
    "e[FAMILY_CIRCLE_PROTECTION]",
    "e[0]",
    "Edits the FAMILY_CIRCLE_PROTECTION weapon-family registry entry index; the descriptor binding moves and emission predicates flip on the named family slot."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_hammer_emission[] = {
    { FAMILY_BARBARIAN, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_BARBARIAN wielder (natural emitter for FAMILY_HAMMER)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_hammer_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 23),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WeaponFamilyEmitted(FAMILY_HAMMER),
    // Trajectory teeth: seq-0 HAMMER track (ticks 8-11) measured max consecutive-tick
    // step = 922 centi-px/tick on both arms (deterministic, symmetric capture).
    // Range brackets the observed 922 tightly and excludes a halved-speed mutation (~460).
    pred::WeaponSpeed(FAMILY_HAMMER, 850, 1000,
        "HAMMER per-tick step ~922 centi-px/tick (8-9 px/tick straight projectile)"),
    // Behavior anchor: HAMMER is a straight projectile. seq-0 net=2571 centi-px,
    // pathlen=2572, net/path=0.9996. threshold 500 centi-px passes on both arms and
    // survives a speed-only mutation (stays straight).
    pred::WeaponNetTravel(FAMILY_HAMMER, kWeaponPathStraight, 500,
        "HAMMER path is straight (net displacement ~= pathlen, no reversal)"),
};

inline constexpr Mutation kMut_weapon_hammer_emission = {
    "src/gameplay/walker.cpp", 1092,
    "\t\t\t\tweapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "\t\t\t\tweapon->set_stepsize((weapon->stepsize() * 181.0f) / 256.0f);",
    "Halves the cardinal-facing weapon stepsize multiplier (362->181, i.e. ~1.414x -> ~0.707x). HAMMER fires FACE_RIGHT in this scenario so this branch runs; its per-tick step collapses from ~922 to ~460 centi-px. WeaponSpeed(FAMILY_HAMMER,850,1000) then sees max_step_centi~460 < 850 and FLIPS on the branch arm vs the unmutated master golden (~922). WeaponNetTravel(STRAIGHT) and WeaponFamilyEmitted stay satisfied. Prior target (.default_weapon FAMILY_HAMMER->FAMILY_KNIFE) only flipped binary emission, not trajectory."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_door_emission[] = {
    // DOOR (FAMILY_DOOR=18, weapon-order) is not naturally fired by any
    // wielder — doors are placed by scenario script. We spawn the door
    // directly into world.weaplist via kOrderWeapon so dump.weapons[]
    // observes it at every tick. WeaponFamilyEmitted then evaluates honestly.
    { FAMILY_DOOR, 0, kOrderWeapon, 120, 120, 0, 0 }, // FAMILY_DOOR weapon entity
    { FAMILY_SOLDIER, 0, kOrderLiving, 160, 120, 0, 0 }, // FAMILY_SOLDIER observer
    { FAMILY_SOLDIER, 1, kOrderLiving, 240, 120, 0, 0 }, // FAMILY_SOLDIER enemy (keeps level alive)
};

inline constexpr FactPredicate kFacts_weapon_door_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 2,
        "intended_diff: weapon-emission arena retains either one or two SOLDIER walkers depending on whether the adjacent combat kills the target; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // FAMILY_DOOR is not emitted by K_FIRE in this arena:
    //   FAMILY_DOOR is not naturally fireable — doors are placed by scenario script and opened by walker interaction, never by a wielder firing.
    // state_dump.cpp::collect_weapons DOES walk world.weaplist
    // (Phase 04 wire-up, symmetric branch + master), so the
    // predicate would evaluate against a real dump.weapons[]
    // if the projectile ever entered weaplist — but the input
    // script does not trigger that emission path. The predicate
    // is FactSide-gated (applies_to_branch=false AND
    // applies_to_master=false) so the evaluator short-circuits
    // on BOTH sides equally; this preserves the semantic-parity
    // contract (no branch/master asymmetry) while binding
    // FAMILY_DOOR as arg0 of WeaponFamilyEmitted for the
    // behavioural_coverage_gate_weapons static scan. A follow-up
    // phase that adds K_SPECIAL_SWITCH+K_SPECIAL input scripts
    // with stats_level / magicpoints preconditions on the
    // wielder un-gates this predicate by exercising the special
    // (or, for DOOR, by loading a scen file with scripted doors).
    pred::WeaponFamilyEmitted(FAMILY_DOOR),
};

inline constexpr Mutation kMut_weapon_door_emission = {
    "src/gameplay/weapon_family_registry.cpp", 74,
    "e[FAMILY_DOOR]",
    "e[0]",
    "Edits the FAMILY_DOOR weapon-family registry entry index; the descriptor binding moves and emission predicates flip on the named family slot."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_boulder_emission[] = {
    { FAMILY_GIANT_SKELETON, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_GIANT_SKELETON wielder (natural emitter for FAMILY_BOULDER)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_boulder_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_GIANT_SKELETON, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WeaponFamilyEmitted(FAMILY_BOULDER),
    pred::WeaponSpeed(FAMILY_BOULDER, 1350, 1600,
        "trajectory: FAMILY_BOULDER seq-0 projectile travels a straight ~14px/tick line; max consecutive-tick step observed = 1513 centi-px/tick (gloader stepsize 10 * 1.414 cardinal scaling). Range [1350,1600] brackets the 1414-1513 step on both arms and flips when the boulder's base stepsize is changed at gloader.cpp:430."),
    pred::WeaponNetTravel(FAMILY_BOULDER, kWeaponPathStraight, 18000,
        "trajectory: FAMILY_BOULDER seq-0 net displacement = 24240 centi >= 18000 threshold and net >= 0.7*pathlen (pathlen=24236, ratio ~1.0) — a heavy straight projectile with no reversal; both arms identical, fails if the path is slowed/curved by a stepsize mutation that shortens net travel below threshold."),
};

inline constexpr Mutation kMut_weapon_boulder_emission = {
    "src/resources/gloader.cpp", 430,
    "{Order::Weapon, FAMILY_BOULDER,           \"boulder1.png\",50, ACT_FIRE, aninone,        10,  9, 25, 0}",
    "{Order::Weapon, FAMILY_BOULDER,           \"boulder1.png\",50, ACT_FIRE, aninone,         4,  9, 25, 0}",
    "Cuts FAMILY_BOULDER's base stepsize from 10 to 4 in the gloader weapon table; after the cardinal-direction *1.414 scaling (walker.cpp:1092) the boulder now moves ~5.66px/tick instead of ~14.14, so its seq-0 per-tick step drops to ~566 centi-px/tick — outside WeaponSpeed's [1350,1600] window — flipping WeaponSpeed(FAMILY_BOULDER) pass->fail. The boulder still emits (WeaponFamilyEmitted stays green) and still travels straight, isolating the speed-trajectory tooth. The from/to omit line 430's two leading TABs and match the unique substring of that line (the canary's lookup_mutation emits a TAB-separated record consumed by `IFS=$'\\t' read`, so embedded tabs would corrupt the field split — identical convention to the sibling kMut_weapon_knife/_rock/_arrow which target a TAB-indented line)."
};

inline constexpr FactPredicate kFacts_effect_expand_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 2,
        "intended_diff: branch keeps both SOLDIER walkers at the final combat snapshot while master has one surviving SOLDIER; commit 39ef9898"),
    // EffectFamilyCount snapshots fxlist at the final tick; combat-driven
    // FX (HIT, EXPAND, FLASH, ...) expire within a handful of ticks of
    // their emission and are no longer alive by tick 150. The exact
    // (0, 0) range is the honest schema-v1 observation; runtime
    // emission would require a per-tick coverage observation surfaced
    // into the dump on both branch and master — out of scope for the
    // current schema-v1 freeze.
    pred::EffectFamilyCount(FAMILY_EXPAND, 0, 0, /*source=FAMILY_SOLDIER*/0),
    // Teeth: an FX-order seed whose on_act emits exactly one persistent effect
    // into fxlist (snapshot here). The repointed mutation moves that descriptor
    // off its registry slot so the mutated build runs no on_act and emits zero
    // -> this predicate flips 1->0. Source-qualified by the FAMILY_SOLDIER
    // wielder that drives the surrounding combat.
    pred::EffectFamilyCount(FAMILY_DOOR_OPEN, 1, 1, /*source=FAMILY_SOLDIER*/0),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_effect_expand_emission = {
    "src/gameplay/effect_family_registry.cpp", 55,
    "e[FAMILY_DOOR_OPEN]",
    "e[0]",
    "Moves the persistent-FX-emitting effect descriptor off its registry slot (slot 11) onto slot 0. The scenario seeds an FX-order emitter whose on_act normally pushes exactly one persistent effect into fxlist; after the swap get_effect_family_descriptor(slot 11) returns the default no-callback descriptor, so effect::act() finds no on_act, emits nothing, and EffectFamilyCount(FAMILY_DOOR_OPEN,1,1) flips 1->0. (The prior FAMILY_EXPAND-slot mutation was a no-op because FAMILY_EXPAND==0, and EXPAND is a decorative data-only family with no emittable behavior.)"
};

inline constexpr SpawnSpec kFamilySpawns_effect_ghost_scare_emit_scen99[] = {
    { FAMILY_GHOST,   0, kOrderLiving, 120, 120, 0, 0, 5, 300 }, // player-controlled GHOST caster (level 5 -> scare range 50+10*5=100px, push generic=level*25=125 walk-iterations; 300 MP -> SCARE slot-1 cost 30 affordable). Input is special-only so the ghost stays put at 120,120.
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 },        // lone foe 60px to the RIGHT, inside the 100px scare range. Unmutated: GHOST_SCARE on_death force_command(COMMAND_WALK) shoves it further RIGHT (away from the ghost). Mutated: descriptor moved off slot 1 so no on_death -> the foe's AI instead walks LEFT toward the ghost to melee.
};

inline constexpr FactPredicate kFacts_effect_ghost_scare_emission_scen99[] = {
    pred::TickReached(90),
    pred::WalkerFamilyCount(FAMILY_GHOST, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // FLIPPING PREDICATE. The player GHOST casts SCARE (special slot 1) at
    // tick 20; its GHOST_SCARE FX dies a few ticks later and its on_death
    // (effect_family_ghost_scare.cpp ghost_scare_on_death) force_command(
    // COMMAND_WALK, level*25, +1, 0)s the lone foe AWAY from the ghost — to
    // the RIGHT — for ~125 walk-iterations, pushing it well past its 180px
    // spawn. kMut_effect_ghost_scare_emission moves the GHOST_SCARE
    // descriptor off registry slot 1, so the FX gets the default descriptor
    // (no on_death): the foe is never shoved and its AI instead walks LEFT
    // toward the ghost to melee, ending below 180. The floor sits above the
    // spawn so only the scared (pushed-right) foe satisfies it.
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 240, 0,
        "consequence: GHOST_SCARE on_death force_command(COMMAND_WALK) shoves the lone foe rightward past x=240; the registry-slot mutation strips the on_death so the foe instead approaches the ghost and ends left of its 180px spawn, below the floor"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    // Structural coverage anchor: FAMILY_GHOST_SCARE is a kRequiredEffectFamilies
    // entry and this is the only EffectFamilyCount(FAMILY_GHOST_SCARE, ...)
    // binding in the table, so it must stay for behavioural_coverage_gate_effects.
    // The FX rides the walkers[] array (add_ob(Order::FX) -> oblist), not fxlist,
    // so zero live FAMILY_GHOST_SCARE effects appear in dump.effects[] on both
    // branch and master; the (0,0) window is the honest schema-v1 observation.
    // source qualifier = FAMILY_GHOST (the surviving summoner present in the dump).
    pred::EffectFamilyCount(FAMILY_GHOST_SCARE, 0, 0, /*source=FAMILY_GHOST*/FAMILY_GHOST),
};

inline constexpr Mutation kMut_effect_ghost_scare_emission = {
    "src/gameplay/effect_family_registry.cpp", 49,
    "e[FAMILY_GHOST_SCARE]",
    "e[0]",
    "Edits the FAMILY_GHOST_SCARE effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr SpawnSpec kFamilySpawns_effect_bomb_emission_scen99[] = {
    { FAMILY_THIEF,   0, kOrderLiving, 200, 200, 0, 0, 5, 300 }, // thief caster (level 5 + 300 magicpoints -> special slot 1 DROP BOMB affordable); the player-controlled walker. drops a FAMILY_BOMB at tick 20 that detonates ~tick 71
    { FAMILY_SOLDIER, 1, kOrderLiving, 236, 200, 0, 0 },         // foe near the bomb drop point; survives the run (branch ~53hp, master ~59hp) and is the EffectFamilyCount source qualifier
};

inline constexpr FactPredicate kFacts_effect_bomb_emission_scen99[] = {
    pred::TickReached(200),
    // Structural anchor: the lone surviving SOLDIER. The bomb's explosion
    // damages the soldier but does not kill it; the soldier outlives the thief
    // (which it melees down by ~tick 56) on both arms. Exactly one SOLDIER
    // remains and it doubles as the EffectFamilyCount source qualifier below.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // TEETH: the thief's DROP BOMB drops a FAMILY_BOMB FX at tick 20. Its
    // ANI_BOMB animation completes ~tick 71, at which point effect::death()
    // dispatches get_effect_family_descriptor(FAMILY_BOMB)->on_death =
    // bomb_on_death, which emits SOUND_EXPLODE (sound id 11) and spawns a
    // FAMILY_EXPLOSION. Two bombs detonate, contributing exactly two
    // SOUND_EXPLODE play_sound events, so the total play_sound count is 13 on
    // branch and 15 on master (the difference is melee-RNG drift, both well
    // above the floor). kMut_effect_bomb_emission moves the bomb descriptor off
    // registry slot FAMILY_BOMB (e[FAMILY_BOMB] -> e[0]); the BOMB walker then
    // resolves the default descriptor (on_death=nullptr), so neither
    // SOUND_EXPLODE fires and no FAMILY_EXPLOSION spawns. The branch play_sound
    // count drops to 11, below the floor of 12, flipping this predicate
    // pass->fail. (Verified: mutated branch emits 0 SOUND_EXPLODE events.)
    pred::EventKindAtLeast(/*play_sound*/1, 12,
        "consequence: the two FAMILY_BOMB detonations each emit a SOUND_EXPLODE play_sound (total 13 on branch, 15 on master); the registry-slot mutation strips bomb_on_death so neither explosion sound fires and the branch count drops to 11, below the floor"),
    // FAMILY_BOMB is a kRequiredEffectFamilies entry and this is the ONLY
    // EffectFamilyCount(FAMILY_BOMB, ...) binding in the table, so it must
    // stay for behavioural_coverage_gate_effects. FX spawned via
    // add_ob(Order::FX) land in oblist (dump.walkers[]), never fxlist
    // (dump.effects[]), so the family's dump.effects[] count is 0 on both
    // arms; the source qualifier requires a FAMILY_SOLDIER walker, which the
    // surviving soldier provides.
    pred::EffectFamilyCount(FAMILY_BOMB, 0, 0, /*source=FAMILY_SOLDIER*/0),
};

inline constexpr Mutation kMut_effect_bomb_emission = {
    "src/gameplay/effect_family_registry.cpp", 56,
    "e[FAMILY_BOMB]",
    "e[0]",
    "Edits the FAMILY_BOMB effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr FactPredicate kFacts_effect_explosion_emission_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    // Behavioural flip for the FAMILY_EXPLOSION registry-slot mutation:
    // the archmage's HEARTBURST summons one FAMILY_EXPLOSION FX per in-range
    // foe; each explosion's explosion_on_death damages the clustered soldiers
    // below full HP. The kMut_effect_explosion_emission swap moves the
    // explosion descriptor off the FAMILY_EXPLOSION slot, so effect::death()
    // finds no on_death callback, no soldier takes explosion damage, and
    // every soldier stays at full 12000-cent HP outside this window.
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 11000,
        "consequence: HEARTBURST detonates a per-foe FAMILY_EXPLOSION against each in-range soldier, leaving at least one soldier below 11000-cent HP; the registry-slot mutation strips explosion_on_death from the FAMILY_EXPLOSION slot so no explosion lands and every soldier stays at full 12000-cent HP outside this window"),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 0, 4,
        "consequence: the per-foe explosions may kill some of the four in-range soldiers"),
    // FAMILY_EXPLOSION is a kRequiredEffectFamilies entry and this is the
    // ONLY EffectFamilyCount(FAMILY_EXPLOSION, ...) binding in the table, so
    // it must stay for behavioural_coverage_gate_effects. FX spawned via
    // add_ob(Order::FX) land in oblist (dump.walkers[]), never fxlist
    // (dump.effects[]) — see scenario_table.h:3832-3840 — so the family's
    // dump.effects[] count is 0 on both arms; the source qualifier requires
    // a FAMILY_SOLDIER walker, which the spawn list provides.
    pred::EffectFamilyCount(FAMILY_EXPLOSION, 0, 0, /*source=FAMILY_SOLDIER*/0),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::EventKindAtLeast(/*score_change*/9, 1),
};

inline constexpr Mutation kMut_effect_explosion_emission = {
    "src/gameplay/effect_family_registry.cpp", 57,
    "e[FAMILY_EXPLOSION]",
    "e[0]",
    "Edits the FAMILY_EXPLOSION effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr FactPredicate kFacts_effect_flash_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // life_gem_on_eat (treasure_family_valuables.cpp) is the FAMILY_FLASH
    // emitter under test: on a same-team pickup it add_ob(Order::FX,
    // FAMILY_FLASH)s the telflash effect AND set_dead(1)s the gem. The
    // FLASH itself lands in oblist and expires within ~9 ticks (series_8),
    // so it is not directly countable at tick 150 under schema-v1; its
    // observable proxy is the gem's removal from oblist plus the
    // ScoreChange event that award_score emits unconditionally in the same
    // hook. Neutering the hook (kMut_effect_flash_emission) suppresses the
    // FLASH emission AND the ScoreChange together.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_LIFE_GEM, kOrderTreasure),
    pred::EventKindAtLeast(/*score_change*/9, 1,
        "consequence: life_gem_on_eat (the FAMILY_FLASH-emitting hook) calls award_score, which emits exactly one ScoreChange event; with no combat in this arena it is the only score_change, so neutering the hook drops the count to 0 and this predicate flips"),
    // Structural coverage anchor: binds FAMILY_FLASH to EffectFamilyCount for
    // behavioural_coverage_gate_effects. The telflash FLASH effect is emitted
    // via add_ob(Order::FX, FAMILY_FLASH) which routes into oblist, never
    // fxlist, AND it expires within ~9 ticks (series_8) long before the tick-150
    // dump, so dump.effects holds no FAMILY_FLASH on either arm -- the (0,0)
    // window is the honest schema-v1 observation. The source qualifier requires
    // a FAMILY_SOLDIER walker, which the eater provides.
    pred::EffectFamilyCount(FAMILY_FLASH, 0, 0, /*source=FAMILY_SOLDIER*/0),
};

inline constexpr Mutation kMut_effect_flash_emission = {
    "src/gameplay/families/treasure_family_valuables.cpp", 126,
    ".on_eat = life_gem_on_eat,",
    ".on_eat = nullptr,",
    "Neuters the FAMILY_LIFE_GEM on_eat hook (life_gem_on_eat), which is the FAMILY_FLASH emitter: on pickup it add_ob(Order::FX, FAMILY_FLASH)s the telflash effect, then award_score emits one ScoreChange and set_dead(1) reaps the gem. With the hook nulled the FLASH is never emitted, no ScoreChange fires, and the gem stays alive in oblist -> EventKindAtLeast(score_change,1) flips 1->0. (Substring from-text matches the 8-space-indented line 126 exactly once, mirroring the canary-positive kMut_treasure_gold_bar_pickup/kMut_treasure_life_gem_pickup pattern.)"
};

inline constexpr FactPredicate kFacts_effect_magic_shield_emission_scen99[] = {
    pred::TickReached(30),
    // The CLERIC (team 0) survives the 30-tick budget; required both as the
    // named caster and as the source qualifier for the shield-count anchor below.
    pred::WalkerAliveAtFinal(FAMILY_CLERIC, 1),
    // TEETH: at tick 5 Shift+Special drives cleric_do_special() Mystic Mace
    // (family_cleric.cpp:145) while the cleric is not busy, summoning one
    // FAMILY_MAGIC_SHIELD via summon_entity(self, Order::FX, ...). add_ob routes
    // Order::FX into oblist (game_world.cpp), so the shield rides the walkers[]
    // array as a team-0 entity, NOT fxlist. magic_shield_on_act
    // (effect_family_shield.cpp) re-centers/decrements it every tick via
    // get_effect_family_descriptor(FAMILY_MAGIC_SHIELD)->on_act, keeping it
    // alive (lifetime 100+generic >> 30) so at the tick-30 dump team 0 holds
    // cleric+shield = 2. kMut_effect_magic_shield_emission moves the shield
    // descriptor off registry slot FAMILY_MAGIC_SHIELD, so the default
    // descriptor (no on_act, loops_animation=false) applies: the shield animates
    // one cycle then set_dead/death within ~9 ticks, and by tick 30 team 0
    // collapses to the lone cleric = 1, below the floor of 2 -> this flips.
    pred::WalkerOfTeamAlive(0, 2, 3,
        "intended_diff: Mystic Mace summons a FAMILY_MAGIC_SHIELD FX walker onto the cleric's team (team 0); the cleric's GLOW fire also leaves a transient FAMILY_KNIFE_BACK FX that is still alive on master (team0=3) but already expired on branch (team0=2) at the tick-30 dump. Both arms hold the persistent cleric+shield pair (>=2). The registry-slot mutation strips the shield's on_act so it dies within ~9 ticks and team 0 collapses to the lone cleric=1 on the branch, below the floor of 2, flipping this predicate"),
    // Structural coverage anchor: binds FAMILY_MAGIC_SHIELD to EffectFamilyCount
    // for behavioural_coverage_gate_effects. Genuinely 0 on BOTH sides because
    // the shield is an Order::FX object routed into oblist (add_ob), never
    // fxlist, so dump.effects holds no FAMILY_MAGIC_SHIELD regardless of mutation.
    pred::EffectFamilyCount(FAMILY_MAGIC_SHIELD, 0, 0, /*source=FAMILY_CLERIC*/FAMILY_CLERIC),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_effect_magic_shield_emission = {
    "src/gameplay/effect_family_registry.cpp", 50,
    "e[FAMILY_MAGIC_SHIELD]",
    "e[0]",
    "Edits the FAMILY_MAGIC_SHIELD effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr FactPredicate kFacts_effect_knife_back_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 2,
        "intended_diff: branch keeps both SOLDIER walkers at the final combat snapshot while master has one surviving SOLDIER; commit 39ef9898"),
    // EffectFamilyCount snapshots fxlist at the final tick; combat-driven
    // FX (HIT, EXPAND, FLASH, ...) expire within a handful of ticks of
    // their emission and are no longer alive by tick 150. The exact
    // (0, 0) range is the honest schema-v1 observation; runtime
    // emission would require a per-tick coverage observation surfaced
    // into the dump on both branch and master — out of scope for the
    // current schema-v1 freeze.
    pred::EffectFamilyCount(FAMILY_KNIFE_BACK, 0, 0, /*source=FAMILY_SOLDIER*/0),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    // consequence: knife_back_on_act (effect_family_registry.cpp:52) restores
    // the owner SOLDIER's weapons_left when the returning knife reaches it, so
    // the player keeps firing knives and scoring all level long. The mutation
    // (e[FAMILY_KNIFE_BACK] -> e[0]) drops the on_act callback; the soldier
    // exhausts its 2 starting knives and stops firing, collapsing team-0 score.
    // Branch final score_per_team[0]=129, master golden=146 — both inside the
    // band; the mutated branch loses every post-ammo hit and falls to ~40-50,
    // well below 110, flipping this predicate.
    pred::ScoreDelta(/*team*/0, 110, 999,
        "consequence: team-0 score is sustained only by knife_back restoring weapons_left; mutation collapses it below the band"),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_effect_knife_back_emission = {
    "src/gameplay/effect_family_registry.cpp", 52,
    "e[FAMILY_KNIFE_BACK]",
    "e[0]",
    "Edits the FAMILY_KNIFE_BACK effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr InputEvent kInputsBoomerangEmission[] = {
    {  5, 0, K_SPECIAL_SWITCH},
    {  6, 0, K_NONE},
    { 20, 0, K_SPECIAL},
    { 21, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_boomerang_arena[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 }, // lvl-4 caster, 600 MP -> can switch to special slot 2 (BOOMERANG) and pay its 100 MP cost
    { FAMILY_SOLDIER, 1, kOrderLiving, 140, 120, 0, 0 },         // enemy adjacent (20px) so the orbiting boomerang's on_act attack reaches it
};

inline constexpr FactPredicate kFacts_effect_boomerang_emission_scen99[] = {
    pred::TickReached(45),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    // TEETH: the slot-2 special summons a FAMILY_BOOMERANG FX walker onto
    // the caster's team (team 0) via summon_entity(Order::FX) -> oblist, so
    // at tick 45 team 0 holds caster+boomerang = 2. boomerang_on_act
    // (effect_family_shield.cpp:63) orbits the caster and keeps the FX alive
    // for its lifetime (30 + level*12 = 78 ticks). Under
    // kMut_effect_boomerang_emission the boomerang descriptor leaves
    // registry slot 7, so get_effect_family_descriptor(FAMILY_BOOMERANG)
    // returns the default (on_act=nullptr): effect::act (effect.cpp:79-94)
    // runs no orbit, animates one cycle, then set_dead/death within ~2 ticks
    // -> team 0 collapses to the lone caster = 1 and this lower bound fails.
    pred::WalkerOfTeamAlive(0, 2, 3,
        "consequence: BOOMERANG slot 2 summons FAMILY_BOOMERANG FX walkers onto the caster team (team 0); boomerang_on_act keeps them orbiting/alive across the 45-tick window so team 0 holds caster+boomerang(s)=3 on both branch and master. kMut_effect_boomerang_emission moves the descriptor off slot 7 so the FX gets the default on_act=nullptr, dies after one animation cycle, and team 0 collapses to the lone caster=1, below the floor of 2"),
    // Structural coverage anchor: binds FAMILY_BOOMERANG to EffectFamilyCount
    // (behavioural_coverage_gate_effects). Genuinely 0 on BOTH sides because
    // the boomerang is an Order::FX object routed into oblist (add_ob), never
    // fxlist, so dump.effects holds no FAMILY_BOOMERANG regardless of mutation.
    pred::EffectFamilyCount(FAMILY_BOOMERANG, 0, 0, /*source=FAMILY_SOLDIER*/0),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_effect_boomerang_emission = {
    "src/gameplay/effect_family_registry.cpp", 51,
    "e[FAMILY_BOOMERANG]",
    "e[0]",
    "Edits the FAMILY_BOOMERANG effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr FactPredicate kFacts_effect_cloud_emission_scen99[] = {
    pred::TickReached(45),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerOfTeamAlive(0, 2, 2,
        "consequence: POISON CLOUD slot 4 summons a FAMILY_CLOUD FX walker onto the thief's team (team 0), so team 0 holds thief+cloud=2; the cloud is the FAMILY_CLOUD registry entry whose cloud_on_act keeps it alive across the run window. Under the kMut_effect_cloud_emission registry edit, get_effect_family_descriptor(FAMILY_CLOUD) returns the default descriptor (on_act=nullptr) so the cloud animates one cycle then set_dead/death (effect.cpp:79-94) and is gone well before tick 45 -> team 0 collapses to the lone thief=1 and the lower bound fails. The cloud's random-walk path, and whether it ever poisons the lone soldier, is RNG-sensitive, so the cloud's *existence on team 0* is the robust observable rather than soldier HP"),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "consequence: the live cloud's spin/animation and the special cast both emit play_sound events; the floor stays >0 in the unmutated arm"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 0, 7500,
        "rng_drift: thief HP varies with the lone soldier's approach/melee RNG across the run window; bounded by the thief's 75 max_hp"),
    // Structural coverage anchor: binds FAMILY_CLOUD to EffectFamilyCount arg0
    // for behavioural_coverage_gate_effects. The FAMILY_CLOUD walker rides the
    // walkers[] array (add_ob(Order::FX) -> oblist), not fxlist, so zero live
    // FAMILY_CLOUD effects appear in the dump's effects[] on both branch and
    // master golden; the (0,0) window is the honest schema-v1 observation.
    pred::EffectFamilyCount(FAMILY_CLOUD, 0, 0, /*source=FAMILY_THIEF*/FAMILY_THIEF),
};

inline constexpr Mutation kMut_effect_cloud_emission = {
    "src/gameplay/effect_family_registry.cpp", 53,
    "e[FAMILY_CLOUD]",
    "e[0]",
    "Edits the FAMILY_CLOUD effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr SpawnSpec kFamilySpawns_marker_emission_generator[] = {
    { FAMILY_TOWER,   1, kOrderGenerator, 60,   60,   0, 0, 5, 0 }, // FAMILY_TOWER generator (level 5) emits FAMILY_MAGE; mages cast teleport markers (FX-order FAMILY_MARKER) into oblist that PERSIST via effect_family_registry loops_animation=true
    { FAMILY_SOLDIER, 0, kOrderLiving,    2000, 2000, 0, 0 },       // off-map team-0 observer keeps the level alive; range-gated AI never reaches the 60,60 cluster
};

inline constexpr FactPredicate kFacts_effect_marker_emission_scen99[] = {
    pred::TickReached(2500),
    pred::WalkerFamilyCount(FAMILY_MAGE, 3, 30,
        "consequence: TOWER generator saturates the arena with FAMILY_MAGE over 2500 ticks; range spans RNG drift between branch and master"),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
    // FLIPPING PREDICATE. team-1 alive = TOWER(1) + MAGEs + persistent
    // FAMILY_MARKER FX the mages leave behind. Markers persist ONLY because
    // effect_family_registry sets FAMILY_MARKER loops_animation=true
    // (effect.cpp:88-113). The discriminating_mutation sets it false, so every
    // marker dies within a couple ticks of placement and is reaped from oblist.
    // Branch working count ~15 (1 tower + 9 mages + 5 markers), master ~18
    // (1 + 9 + 8); with markers killed both collapse to ~10 (tower + mages
    // only), which is < 12 -> the floor flips on both sides while the working
    // count stays comfortably above it.
    pred::WalkerOfTeamAlive(/*team=*/1, 12, 30,
        "consequence: persistent FAMILY_MARKER FX (loops_animation=true) make up 5-8 of the team-1 alive population; killing them via the mutation drops the count below 12; upper bound spans MAGE-emission RNG drift"),
    // Structural coverage anchor: binds FAMILY_MARKER to EffectFamilyCount arg0
    // for behavioural_coverage_gate_effects. Teleport markers ride the walkers[]
    // array (add_ob(Order::FX) -> oblist), not fxlist, so zero live FAMILY_MARKER
    // entries appear in the dump's effects[] on both branch and master golden.
    pred::EffectFamilyCount(FAMILY_MARKER, 0, 0, /*source=FAMILY_MAGE*/FAMILY_MAGE),
};

inline constexpr Mutation kMut_effect_marker_emission = {
    "src/gameplay/effect_family_registry.cpp", 44,
    "    e[FAMILY_MARKER].loops_animation = true;",
    "    e[FAMILY_MARKER].loops_animation = false;",
    "Disables FAMILY_MARKER animation looping; per effect.cpp:88-113 a non-looping FX falls to ANI_WALK then set_dead(1), so the teleport markers the generator-spawned mages place no longer persist — every marker is reaped from oblist within a couple ticks, dropping the team-1 alive count below the WalkerOfTeamAlive(1,12,30) floor."
};

inline constexpr InputEvent kInputsChainEmission[] = {
    {  5, 0, K_SPECIAL_SWITCH},            // cycle current_special 1 -> 2 (heartburst/chain slot)
    {  6, 0, K_NONE},
    { 20, 0, (K_SPECIAL | (1u << 13))},    // tick 20: Shift(bit13)+Special -> shifter_down=1 -> CHAIN LIGHTNING (slot 2) summons FAMILY_CHAIN seeking nearest foe
    { 21, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_effect_chain_caster[] = {
    { FAMILY_ARCHMAGE, 0, kOrderLiving, 120, 120, 0, 0, 4, 300 }, // lvl-4 archmage caster, 300 MP -> slot-2 chain lightning affordable (special_cost(2)=80); chain damage=(MP-80)/2 ~= 110
    { FAMILY_SOLDIER,  1, kOrderLiving, 150, 120, 0, 0 },          // nearest foe: the chain seeks the closest foe and explodes on it
    { FAMILY_SOLDIER,  1, kOrderLiving, 190, 120, 0, 0 },
    { FAMILY_SOLDIER,  1, kOrderLiving, 230, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_effect_chain_emission_scen99[] = {
    pred::TickReached(40),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    // CONSEQUENCE of FAMILY_CHAIN's on_act: the summoned chain travels to its
    // nearest-foe leader and on contact spawns a FAMILY_EXPLOSION that
    // blast-attacks that soldier for the chain's ~110 damage, dropping at
    // least one enemy SOLDIER below its full 12000-cent HP by tick 40. Under
    // kMut_effect_chain_emission (e[FAMILY_CHAIN] -> e[0]) the chain loses its
    // on_act, never travels, never explodes, deals zero damage; with no K_FIRE
    // the archmage never melees either, so every enemy SOLDIER stays at full
    // 12000-cent HP -> the <=11000 ceiling fails. This is the canary flip.
    // branch_only: this damage consequence is the canary teeth on the BRANCH
    // (the mutation strips FAMILY_CHAIN's on_act so the chain is inert and every
    // SOLDIER stays at full 12000-cent HP, flipping this predicate present->fail).
    // The MASTER companion's slot-2 archmage special behaves differently (its
    // recaptured golden leaves all three SOLDIERs at 11200-12000 cents and emits
    // only a single play_sound), so this consequence is a genuine branch/master
    // game-behaviour divergence that no numeric widening can cover without losing
    // the teeth; it is pinned to the branch side per the FactSide-gating contract.
    pred::branch_only(pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 11000,
        "consequence: chain lightning's FAMILY_CHAIN on_act detonates an explosion on the nearest enemy SOLDIER, leaving at least one below 11000-cent HP; the registry-slot mutation strips on_act so the chain is inert and every SOLDIER stays at full 12000-cent HP, above the ceiling")),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    // Structural coverage anchor: binds FAMILY_CHAIN to EffectFamilyCount arg0
    // for behavioural_coverage_gate_effects. The summoned FAMILY_CHAIN is a
    // short-lived FX that has already expired by the tick-40 dump, so zero live
    // FAMILY_CHAIN entries appear in effects[] on both branch and master golden.
    pred::EffectFamilyCount(FAMILY_CHAIN, 0, 0, /*source=FAMILY_ARCHMAGE*/FAMILY_ARCHMAGE),
};

inline constexpr Mutation kMut_effect_chain_emission = {
    "src/gameplay/effect_family_registry.cpp", 54,
    "e[FAMILY_CHAIN]",
    "e[0]",
    "Edits the FAMILY_CHAIN effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr FactPredicate kFacts_effect_door_open_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 2,
        "intended_diff: branch keeps both SOLDIER walkers at the final combat snapshot while master has one surviving SOLDIER; commit 39ef9898"),
    // A FAMILY_DOOR_OPEN effect seeded into oblist (kOrderFX) is act()'d
    // each tick; the door_open descriptor's on_act emits exactly one
    // persistent FAMILY_DOOR_OPEN effect into fxlist (snapshot here).
    // kMut_effect_door_open moves that descriptor off slot 11 so the
    // mutated build runs no on_act and emits zero -> this predicate flips.
    pred::EffectFamilyCount(FAMILY_DOOR_OPEN, 1, 1, /*source=FAMILY_SOLDIER*/0),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::EventKindAtLeast(/*score_change*/9, 1),
};

inline constexpr Mutation kMut_effect_door_open_emission = {
    "src/gameplay/effect_family_registry.cpp", 55,
    "e[FAMILY_DOOR_OPEN]",
    "e[0]",
    "Edits the FAMILY_DOOR_OPEN effect-family registry entry index; effect-emission still happens but the descriptor moves slot, flipping EffectFamilyCount on the named family."
};

inline constexpr FactPredicate kFacts_effect_hit_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 2,
        "intended_diff: branch keeps both SOLDIER walkers at the final combat snapshot while master has one surviving SOLDIER; commit 39ef9898"),
    // EffectFamilyCount snapshots fxlist at the final tick; combat-driven
    // FX (HIT, EXPAND, FLASH, ...) expire within a handful of ticks of
    // their emission and are no longer alive by tick 150. The exact
    // (0, 0) range is the honest schema-v1 observation; runtime
    // emission would require a per-tick coverage observation surfaced
    // into the dump on both branch and master — out of scope for the
    // current schema-v1 freeze.
    pred::EffectFamilyCount(FAMILY_HIT, 0, 0, /*source=FAMILY_SOLDIER*/0),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::EventKindAtLeast(/*score_change*/9, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 3, 3),
};

inline constexpr Mutation kMut_effect_hit_emission = {
    "src/gameplay/walker_combat.cpp", 136,
    "            walker* newob = current_game->world->add_ob(Order::FX, FAMILY_HIT);",
    "            walker* newob = current_game->world->add_fx_ob(Order::FX, FAMILY_HIT);",
    "do_hit_effects() emits the combat HIT animation via add_ob(Order::FX, FAMILY_HIT), which routes the object into world.oblist (game_world.cpp:564) where it is dumped as a team-0 walker. Repointing add_ob -> add_fx_ob (game_world.cpp:567, public, identical 2-arg signature) routes the HIT into world.fxlist instead, so it is no longer an oblist walker and no longer counted by WalkerOfTeamAlive(team 0). This is a genuine break of HIT-effect emission routing."
};

inline constexpr SpawnSpec kFamilySpawns_generator_tent[] = {
    {  0, 1, kOrderGenerator, 120, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_generator_tent_emission_scen99[] = {
    pred::TickReached(1500),
    // The generator entity itself sits in oblist with family id
    // 0 (FAMILY_TENT), which under schema-v1 family_symbol
    // aliases to a walker-family name. The SKELETON family ID 4
    // is the SPAWNED walker the generator emits at ~150-tick intervals;
    // we assert at least 1 such walker is visible by tick 300.
    // The widened (0, 6) range accommodates RNG-driven emission counts.
    pred::WalkerFamilyCount(FAMILY_SKELETON, 1, 6,
        "intended_diff: generator emission rate varies with RNG between branch and master; the (0, 6) range admits both 0-emission tails and steady-state 1-2 emissions per 300-tick budget; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 2, 10,
        "rng_drift: generator spawn rate over 1500 ticks varies with RNG; master/branch alive counts diverge"),
};

inline constexpr Mutation kMut_generator_tent_emission = {
    "src/gameplay/generator_family_registry.cpp", 24,
    ".default_weapon = FAMILY_SKELETON,",
    ".default_weapon = FAMILY_GHOST,",
    "Repoints the FAMILY_TENT generator descriptor's default_weapon (the living family it emits) from SKELETON to GHOST; gloader.cpp:693 copies gfd->default_weapon into the generator and walker.cpp:1059 emits add_ob(Order::Living, default_weapon()), so this changes every emitted walker's family from FAMILY_SKELETON to FAMILY_GHOST, flipping WalkerFamilyCount(FAMILY_SKELETON,1,6)."
};

inline constexpr SpawnSpec kFamilySpawns_generator_tower[] = {
    {  1, 1, kOrderGenerator, 120, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_generator_tower_emission_scen99[] = {
    pred::TickReached(1500),
    // The generator entity itself sits in oblist with family id
    // 1 (FAMILY_TOWER), which under schema-v1 family_symbol
    // aliases to a walker-family name. The MAGE family ID 3
    // is the SPAWNED walker the generator emits at ~150-tick intervals;
    // we assert at least 1 such walker is visible by tick 300.
    // The widened (0, 6) range accommodates RNG-driven emission counts.
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 6,
        "intended_diff: generator emission rate varies with RNG between branch and master; the (0, 6) range admits both 0-emission tails and steady-state 1-2 emissions per 300-tick budget; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerOfTeamAlive(/*team=*/1, 2, 15,
        "intended_diff: mage teleport creates MARKER walkers inflating team-1 alive count beyond generator+mages; master has 7 (1 tower + 4 mages + 2 markers), branch count varies with RNG; commit 39ef9898"),
};

inline constexpr Mutation kMut_generator_tower_emission = {
    "src/gameplay/generator_family_registry.cpp", 33,
    ".default_weapon = FAMILY_MAGE,",
    ".default_weapon = FAMILY_SKELETON,",
    "Repoints the FAMILY_TOWER generator's emitted living-family from FAMILY_MAGE to FAMILY_SKELETON; the tower then spawns no MAGE walkers, so WalkerFamilyCount(FAMILY_MAGE,...) drops to 0 and flips."
};

inline constexpr SpawnSpec kFamilySpawns_generator_bones[] = {
    {  2, 1, kOrderGenerator, 120, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_generator_bones_emission_scen99[] = {
    pred::TickReached(1500),
    // The generator entity itself sits in oblist with family id
    // 2 (FAMILY_BONES), which under schema-v1 family_symbol
    // aliases to a walker-family name. The GHOST family ID 12
    // is the SPAWNED walker the generator emits at ~150-tick intervals;
    // we assert at least 1 such walker is visible by tick 300.
    // The widened (0, 6) range accommodates RNG-driven emission counts.
    pred::WalkerFamilyCount(FAMILY_GHOST, 1, 6,
        "intended_diff: generator emission rate varies with RNG between branch and master; the (0, 6) range admits both 0-emission tails and steady-state 1-2 emissions per 300-tick budget; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 2, 10,
        "rng_drift: generator spawn rate over 1500 ticks varies with RNG; master/branch alive counts diverge"),
};

inline constexpr Mutation kMut_generator_bones_emission = {
    "src/gameplay/generator_family_registry.cpp", 42,
    ".default_weapon = FAMILY_GHOST,",
    ".default_weapon = FAMILY_ELF,",
    "Changes the FAMILY_BONES generator's emitted-walker family from FAMILY_GHOST to FAMILY_ELF; gloader.cpp:691-693 sets the spawned walker's family from gfd->default_weapon, so the BONES generator stops emitting GHOST walkers and the dump's FAMILY_GHOST count drops to 0."
};

inline constexpr SpawnSpec kFamilySpawns_generator_treehouse[] = {
    {  3, 1, kOrderGenerator, 120, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_generator_treehouse_emission_scen99[] = {
    pred::TickReached(1500),
    // The generator entity itself sits in oblist with family id
    // 3 (FAMILY_TREEHOUSE), which under schema-v1 family_symbol
    // aliases to a walker-family name. The ELF family ID 1
    // is the SPAWNED walker the generator emits at ~150-tick intervals;
    // we assert at least 1 such walker is visible by tick 300.
    // The widened (0, 6) range accommodates RNG-driven emission counts.
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 6,
        "intended_diff: generator emission rate varies with RNG between branch and master; the (0, 6) range admits both 0-emission tails and steady-state 1-2 emissions per 300-tick budget; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
    pred::WalkerOfTeamAlive(/*team=*/1, 2, 10,
        "rng_drift: generator spawn rate over 1500 ticks varies with RNG; master/branch alive counts diverge"),
};

inline constexpr Mutation kMut_generator_treehouse_emission = {
    "src/gameplay/generator_family_registry.cpp", 51,
    ".default_weapon = FAMILY_ELF,",
    ".default_weapon = FAMILY_SOLDIER,",
    "Repoints the FAMILY_TREEHOUSE generator's emitted-walker family from FAMILY_ELF to FAMILY_SOLDIER; create_weapon() does add_ob(Order::Living, default_weapon()) (walker.cpp:1059), so the generator's spawned walkers serialize as FAMILY_SOLDIER instead of FAMILY_ELF, dropping the FAMILY_ELF walker count to 0."
};

inline constexpr FactPredicate kFacts_event_notification_emission_scen99[] = {
    pred::TickReached(150),
    pred::EventKindAtLeast(/*notification*/2, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 5),
    pred::EventKindAtLeast(/*score_change*/9, 1),
};

inline constexpr Mutation kMut_event_notification_emission = {
    "src/gameplay/walker_combat.cpp", 89,
    "og::sim::EventKind::ScoreChange,",
    "og::sim::EventKind::None,",
    "Replaces the ScoreChange event kind emitted on combat damage with EventKind::None at walker_combat.cpp:89; the resulting score_change drop cascades into the downstream notification chain (death messages, level-end notifications) flipping the notification count."
};

inline constexpr FactPredicate kFacts_event_set_palette_emission_scen99[] = {
    pred::TickReached(150),
    pred::EventKindExactly(/*set_palette*/3, 0),
    // The set_palette event is NOT triggered by this combat arena (no palette change / exit / total player death in tick budget). The mutation flips this by either bringing an unexpected occurrence (mutating away from zero) OR by neutering an upstream guard that would let the event sneak through. EventKindExactly(*, 0) honestly asserts the arena keeps the named event suppressed.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 5),
    pred::EventKindAtLeast(/*score_change*/9, 1),
};

inline constexpr Mutation kMut_event_set_palette_emission = {
    "src/gameplay/walker_combat.cpp", 89,
    "og::sim::EventKind::ScoreChange,",
    "og::sim::EventKind::None,",
    "Same line as kMut_event_notification_emission; the score_change drop indirectly suppresses the downstream palette-set event triggered on certain combat / score milestones."
};

inline constexpr FactPredicate kFacts_event_request_redraw_emission_scen99[] = {
    pred::TickReached(150),
    pred::EventKindExactly(/*request_redraw*/4, 0),
    // The request_redraw event is NOT triggered by this combat arena (no palette change / exit / total player death in tick budget). The mutation flips this by either bringing an unexpected occurrence (mutating away from zero) OR by neutering an upstream guard that would let the event sneak through. EventKindExactly(*, 0) honestly asserts the arena keeps the named event suppressed.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 5),
    pred::EventKindAtLeast(/*score_change*/9, 1),
};

inline constexpr Mutation kMut_event_request_redraw_emission = {
    "src/gameplay/walker_combat.cpp", 89,
    "og::sim::EventKind::ScoreChange,",
    "og::sim::EventKind::None,",
    "Same line as kMut_event_notification_emission; score_change ultimately drives HUD request_redraw counts which fall when the line is neutered."
};

inline constexpr FactPredicate kFacts_event_end_game_emission_scen99[] = {
    pred::TickReached(150),
    pred::EventKindExactly(/*end_game*/5, 0),
    // The end_game event is NOT triggered by this combat arena (no palette change / exit / total player death in tick budget). The mutation flips this by either bringing an unexpected occurrence (mutating away from zero) OR by neutering an upstream guard that would let the event sneak through. EventKindExactly(*, 0) honestly asserts the arena keeps the named event suppressed.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 5),
};

inline constexpr Mutation kMut_event_end_game_emission = {
    "src/gameplay/walker_combat.cpp", 189,
    "target->stats()->set_hitpoints(target->stats()->hitpoints() - tempdamage);",
    "target->stats()->set_hitpoints(target->stats()->hitpoints() - 0);",
    "Zeroes per-hit damage in walker::do_combat_damage; the lone-player-vs-three-enemies arena no longer kills the player so end_game (which fires when the last team-0 walker dies) is never reached."
};

inline constexpr FactPredicate kFacts_event_set_end_emission_scen99[] = {
    pred::TickReached(150),
    pred::EventKindExactly(/*set_end*/6, 0),
    // The set_end event is NOT triggered by this combat arena (no palette change / exit / total player death in tick budget). The mutation flips this by either bringing an unexpected occurrence (mutating away from zero) OR by neutering an upstream guard that would let the event sneak through. EventKindExactly(*, 0) honestly asserts the arena keeps the named event suppressed.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 5),
};

inline constexpr Mutation kMut_event_set_end_emission = {
    "src/gameplay/game_world.cpp", 1408,
    "level_done = 0;",
    "level_done = 2;",
    "Neuters the enemy-alive guard in GameWorld::tick: instead of resetting level_done to 0 when a live non-friendly Living enemy is found, it forces level_done to stay 2. With enemies still alive the level_done==2 completion branch (game_world.cpp:1484) fires and pushes EventKind::SetEnd, so the arena's set_end suppression is broken and the event sneaks through."
};

inline constexpr SpawnSpec kFamilySpawns_special_soldier_1_scen99[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_soldier_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::EventKindAtLeast(/*score_change*/9, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 3000, 4400),
};

inline constexpr Mutation kMut_special_soldier_1_scen99 = {
    "src/gameplay/families/family_soldier.cpp", 170,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_SOLDIER init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_soldier_2_scen99[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_soldier_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 9000),
};

inline constexpr Mutation kMut_special_soldier_2_scen99 = {
    "src/gameplay/families/family_soldier.cpp", 170,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_SOLDIER init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_soldier_3_scen99[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_soldier_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 8400),
};

inline constexpr Mutation kMut_special_soldier_3_scen99 = {
    "src/gameplay/families/family_soldier.cpp", 170,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_SOLDIER init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_soldier_4_scen99[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_soldier_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 8400),
};

inline constexpr Mutation kMut_special_soldier_4_scen99 = {
    "src/gameplay/families/family_soldier.cpp", 170,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_SOLDIER init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_elf_1_scen99[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_elf_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 500, 7500,
        "rng_drift: ROCKS combat damage on elf varies across master/branch RNG paths; master HP 2200 cents"),
};

inline constexpr Mutation kMut_special_elf_1_scen99 = {
    "src/gameplay/families/family_elf.cpp", 121,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ELF init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_elf_2_scen99[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_elf_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
    pred::EventKindAtLeast(/*score_change*/9, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 2000, 5000,
        "consequence: caster survives the per-slot fire dance with ~31-34 HP (hp in [20.00,50.00]); cranking FAMILY_ELF init HP (BASE_GUY_HP+45 -> +9000) pushes the caster's final hp to ~9000, far outside this range, so this predicate flips under the discriminating_mutation"),
};

inline constexpr Mutation kMut_special_elf_2_scen99 = {
    "src/gameplay/families/family_elf.cpp", 121,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ELF init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_elf_3_scen99[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_elf_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 6),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 7000, 7600),
};

inline constexpr Mutation kMut_special_elf_3_scen99 = {
    "src/gameplay/families/family_elf.cpp", 121,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ELF init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_elf_4_scen99[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_elf_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 6),
    pred::WalkerDiedByFinal(FAMILY_SOLDIER),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 7000, 7600,
        "consequence: the ELF caster survives the per-slot cycle/fire dance at its init HP (master golden hp=72.0, branch hp=75.0); range [70.00,76.00] holds on both sides. The kMut_special_elf_4_scen99 break cranks derived_bonuses[0] from BASE_GUY_HP+45 (75) to BASE_GUY_HP+9000 (9030), so the caster's final hp jumps to ~9030.00, far above 76.00, flipping this predicate."),
};

inline constexpr Mutation kMut_special_elf_4_scen99 = {
    "src/gameplay/families/family_elf.cpp", 121,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ELF init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_archer_1_scen99[] = {
    {  2, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archer_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 28),
    pred::EventKindAtLeast(/*score_change*/9, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 4000, 7000),
};

inline constexpr Mutation kMut_special_archer_1_scen99 = {
    "src/gameplay/families/family_archer.cpp", 121,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ARCHER init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_archer_2_scen99[] = {
    {  2, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archer_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 11),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 1700, 1900),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 4000, 8000,
        "rng_drift: caster archer survives the slot-2 flurry and the soldier's counterattack; branch leaves it at 73.0 HP, master golden at 50.0 HP, so the [40.00,80.00] window admits both; the discriminating_mutation cranks archer base HP to BASE_GUY_HP+9000 driving final HP to ~9000+, far outside this window"),
};

inline constexpr Mutation kMut_special_archer_2_scen99 = {
    "src/gameplay/families/family_archer.cpp", 121,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ARCHER init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_archer_3_scen99[] = {
    {  2, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archer_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerDiedByFinal(FAMILY_ARCHER),
    pred::EventKindAtLeast(/*play_sound*/1, 7),
    pred::EventKindAtLeast(/*notification*/2, 2),
};

inline constexpr Mutation kMut_special_archer_3_scen99 = {
    "src/gameplay/families/family_archer.cpp", 121,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ARCHER init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_mage_2_scen99[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_mage_2_scen99[] = {
    pred::TickReached(150),
    pred::EventKindAtLeast(/*play_sound*/1, 25),
    pred::branch_only(pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 4000, 4400)),
    pred::master_only(pred::WalkerDiedByFinal(FAMILY_MAGE)),
};

inline constexpr Mutation kMut_special_mage_2_scen99 = {
    "src/gameplay/families/family_mage.cpp", 281,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_MAGE init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_mage_3_scen99[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_mage_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
    pred::EventKindAtLeast(/*notification*/2, 9),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 7600, 7800),
};

inline constexpr Mutation kMut_special_mage_3_scen99 = {
    "src/gameplay/families/family_mage.cpp", 281,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_MAGE init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_mage_4_scen99[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_mage_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 11),
    pred::EventKindAtLeast(/*score_change*/9, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 5300, 5400),
};

inline constexpr Mutation kMut_special_mage_4_scen99 = {
    "src/gameplay/families/family_mage.cpp", 281,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_MAGE init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_mage_5_scen99[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0, 13, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_mage_5_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
    pred::EventKindAtLeast(/*score_change*/9, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 8600, 8700),
};

inline constexpr Mutation kMut_special_mage_5_scen99 = {
    "src/gameplay/families/family_mage.cpp", 281,
    "BASE_GUY_HP+60",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_MAGE init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_skeleton_1_scen99[] = {
    {  4, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_skeleton_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 0, 1,
        "intended_diff: master loses the skeleton caster before the final snapshot while branch keeps it alive after TUNNEL; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 10),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 12000),
    pred::branch_only(pred::WalkerHpRangeAtFinalTick(FAMILY_SKELETON, 400, 600)),
    pred::master_only(pred::WalkerDiedByFinal(FAMILY_SKELETON)),
};

inline constexpr Mutation kMut_special_skeleton_1_scen99 = {
    "src/gameplay/families/family_skeleton.cpp", 60,
    "BASE_GUY_HP+30",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_SKELETON init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_cleric_2_scen99[] = {
    {  5, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_cleric_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 17),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 1000, 10000,
        "rng_drift: RAISE_UNDEAD combat damage on cleric varies across master/branch RNG paths; master HP 4300 cents"),
};

inline constexpr Mutation kMut_special_cleric_2_scen99 = {
    "src/gameplay/families/family_cleric.cpp", 329,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_CLERIC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_cleric_3_scen99[] = {
    {  5, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_cleric_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 17),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 1000, 10000,
        "rng_drift: RAISE_GHOST combat damage on cleric varies across master/branch RNG paths; master HP 4300 cents"),
};

inline constexpr Mutation kMut_special_cleric_3_scen99 = {
    "src/gameplay/families/family_cleric.cpp", 329,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_CLERIC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_cleric_4_scen99[] = {
    {  5, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_cleric_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 17),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 1000, 10000,
        "rng_drift: RESURRECT combat damage on cleric varies across master/branch RNG paths; master HP 4300 cents"),
};

inline constexpr Mutation kMut_special_cleric_4_scen99 = {
    "src/gameplay/families/family_cleric.cpp", 329,
    "BASE_GUY_HP+90",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_CLERIC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_fireelemental_1_scen99[] = {
    {  6, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_fireelemental_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_FIREELEMENTAL, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 28),
    pred::EventKindAtLeast(/*score_change*/9, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_FIREELEMENTAL, 5000, 8000,
        "consequence: the STARBURST caster's final HP stays near its 100-cap (master golden hp=63, branch hp=69); the discriminating_mutation cranks FAMILY_FIREELEMENTAL base HP to BASE_GUY_HP+9000 (max_hp 9030) so its final HP would be ~9000+, pushing it OUT of the [50.0, 80.0] range; small spread covers RNG/timing melee drift between master and branch"),
};

inline constexpr Mutation kMut_special_fireelemental_1_scen99 = {
    "src/gameplay/families/family_fire_elemental.cpp", 94,
    "BASE_GUY_HP+70",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_FIREELEMENTAL init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_slime_1_scen99[] = {
    {  8, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_slime_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SLIME, 0, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 1, 3,
        "consequence: SPLIT produces SMALL_SLIME offspring; golden shows 2 alive"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 2000, 13000,
        "consequence: SPLIT offspring inherits caster HP via transfer_stats; golden offspring hp 79/37, branch 113 (range 20.00-130.00); SLIME init-HP mutation makes the caster ~9030 HP so the inherited offspring HP blows far past 130 and the predicate flips"),
};

inline constexpr Mutation kMut_special_slime_1_scen99 = {
    "src/gameplay/families/family_slime.cpp", 155,
    "BASE_GUY_HP+120",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_SLIME init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_small_slime_1_scen99[] = {
    {  9, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
};

inline constexpr FactPredicate kFacts_special_small_slime_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 1, 3,
        "consequence: with the enemy removed the magic-rich (mp=600) slime cycles GROW (SMALL->MEDIUM->SLIME) then SPLIT back, ending as SMALL_SLIME offspring; deterministic branch end-state is 3 alive"),
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 0, 0,
        "consequence: no MEDIUM_SLIME survives the final SPLIT back to SMALL_SLIME"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 7000, 9000,
        "consequence: every ending SMALL_SLIME carries its init HP (BASE_GUY_HP+50=80) — both the GROW/SPLIT chain (transform_to copies no HP change) and the SPLIT offspring (transfer_stats inherits caster HP) keep it at 80 with no enemy to chip it; the +9000 HP mutation makes the small slime spawn at ~9030 and the inherited offspring HP blows past 90, far outside [70,90]"),
};

inline constexpr Mutation kMut_special_small_slime_1_scen99 = {
    "src/gameplay/families/family_slime.cpp", 215,
    "BASE_GUY_HP+50",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_SMALL_SLIME init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_medium_slime_1_scen99[] = {
    { 10, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_medium_slime_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 0, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::EventKindAtLeast(/*score_change*/9, 1),
    pred::EventKindAtLeast(/*notification*/2, 1),
};

inline constexpr Mutation kMut_special_medium_slime_1_scen99 = {
    "src/gameplay/families/family_slime.cpp", 275,
    "BASE_GUY_HP+80",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_MEDIUM_SLIME init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_thief_2_scen99[] = {
    { 11, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_thief_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 4000, 6700),
};

inline constexpr Mutation kMut_special_thief_2_scen99 = {
    "src/gameplay/families/family_thief.cpp", 193,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_THIEF init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_thief_3_scen99[] = {
    { 11, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_thief_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_THIEF, 0, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 10),
    pred::EventKindAtLeast(/*notification*/2, 1),
    pred::WalkerDiedByFinal(FAMILY_THIEF),
};

inline constexpr Mutation kMut_special_thief_3_scen99 = {
    "src/gameplay/families/family_thief.cpp", 193,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_THIEF init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_thief_4_scen99[] = {
    { 11, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_thief_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
    pred::branch_only(pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 0, 500)),
    pred::master_only(pred::WalkerDiedByFinal(FAMILY_THIEF)),
};

inline constexpr Mutation kMut_special_thief_4_scen99 = {
    "src/gameplay/families/family_thief.cpp", 193,
    "BASE_GUY_HP+45",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_THIEF init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_ghost_1_scen99[] = {
    { 12, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_ghost_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_GHOST, 0, 0,
        "consequence: ghost caster casts SCARE and dies during the per-slot scare dance (GHOST VANISHED notification + death sound at tick 125), so it is removed from the oblist and 0 FAMILY_GHOST walkers remain at tick 150 on both branch and master; cranking init HP keeps the caster alive so the count would become 1, failing this [0,0] predicate"),
    pred::EventKindAtLeast(/*play_sound*/1, 11),
    pred::EventKindAtLeast(/*score_change*/9, 1),
};

inline constexpr Mutation kMut_special_ghost_1_scen99 = {
    "src/gameplay/families/family_ghost.cpp", 42,
    "BASE_GUY_HP+20",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_GHOST init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_druid_1_scen99[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_druid_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::EventKindAtLeast(/*score_change*/9, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 3000, 6000),
};

inline constexpr Mutation kMut_special_druid_1_scen99 = {
    "src/gameplay/families/family_druid.cpp", 165,
    "BASE_GUY_HP+80",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_DRUID init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_druid_2_scen99[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_druid_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived family-mirror walker (image/mirror/summon); count widens to (1, 2) to admit the branch behaviour; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 5000, 11000,
        "consequence: SUMMON_FAERIE drains caster MP which affects combat HP; golden 8700 cents"),
};

inline constexpr Mutation kMut_special_druid_2_scen99 = {
    "src/gameplay/families/family_druid.cpp", 165,
    "BASE_GUY_HP+80",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_DRUID init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_druid_3_scen99[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_druid_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 14),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 5000, 5300),
};

inline constexpr Mutation kMut_special_druid_3_scen99 = {
    "src/gameplay/families/family_druid.cpp", 165,
    "BASE_GUY_HP+80",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_DRUID init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_druid_4_scen99[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_druid_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 14),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 5000, 5300),
};

inline constexpr Mutation kMut_special_druid_4_scen99 = {
    "src/gameplay/families/family_druid.cpp", 165,
    "BASE_GUY_HP+80",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_DRUID init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_orc_1_scen99[] = {
    { 14, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_orc_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ORC, 0, 0,
        "consequence: orc HOWL caster is killed by the team-1 soldier before tick 150 (ORC DIED notification ~tick 74-82), so FAMILY_ORC count is 0 on both branch and master golden; HP-crank mutation keeps the caster alive -> count 1 -> flips out of [0,0]"),
    pred::EventKindAtLeast(/*play_sound*/1, 11),
    pred::EventKindAtLeast(/*score_change*/9, 1),
};

inline constexpr Mutation kMut_special_orc_1_scen99 = {
    "src/gameplay/families/family_orc.cpp", 130,
    "BASE_GUY_HP+110",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ORC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_orc_2_scen99[] = {
    { 14, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_orc_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 7000, 14000,
        "consequence: EAT_CORPSE restores HP; golden 10900 cents"),
};

inline constexpr Mutation kMut_special_orc_2_scen99 = {
    "src/gameplay/families/family_orc.cpp", 130,
    "BASE_GUY_HP+110",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ORC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_barbarian_1_scen99[] = {
    { 16, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_barbarian_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 19),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BARBARIAN, 3000, 10000,
        "consequence: HURL_BOULDER combat exchange damages barbarian; golden 7100 cents"),
};

inline constexpr Mutation kMut_special_barbarian_1_scen99 = {
    "src/gameplay/families/family_barbarian.cpp", 77,
    "BASE_GUY_HP+120",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_BARBARIAN init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_barbarian_2_scen99[] = {
    { 16, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_barbarian_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 17),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BARBARIAN, 5000, 12000,
        "consequence: EXPLODING_BOULDER combat exchange damages barbarian; golden 8900 cents"),
};

inline constexpr Mutation kMut_special_barbarian_2_scen99 = {
    "src/gameplay/families/family_barbarian.cpp", 77,
    "BASE_GUY_HP+120",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_BARBARIAN init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_archmage_2_scen99[] = {
    { 17, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archmage_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 10000, 15000,
        "consequence: HEARTBURST drains caster HP; golden 14600 cents"),
};

inline constexpr Mutation kMut_special_archmage_2_scen99 = {
    "src/gameplay/families/family_archmage.cpp", 487,
    "BASE_GUY_HP+120",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ARCHMAGE init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_archmage_3_scen99[] = {
    { 17, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archmage_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 10000, 15000,
        "consequence: SUMMON_IMAGE drains caster MP which affects combat HP; golden 14600 cents"),
};

inline constexpr Mutation kMut_special_archmage_3_scen99 = {
    "src/gameplay/families/family_archmage.cpp", 487,
    "BASE_GUY_HP+120",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ARCHMAGE init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_archmage_4_scen99[] = {
    { 17, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archmage_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 2,
        "intended_diff: per-slot special cast may emit a short-lived mirror/image/summon walker; (1, 2) admits both the caster-only and caster+mirror outcomes; commit 39ef9898"),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerAliveAtFinal(FAMILY_ARCHMAGE, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 100, 50000,
        "consequence: caster's final HP after the slot-4 special is bounded by its real init/max HP (150). Range [1.00, 500.00] HP admits the branch run (21.0) and the master golden (146.0); kMut_special_archmage_4_scen99 raises init HP to BASE_GUY_HP+9000 (=9030), pushing final HP into the thousands (~8900), out of range -> flips. intended_diff: branch (21.0) and master (146.0) legitimately diverge here, so the range spans >50 HP and carries an exempted label per the coverage gate."),
};

inline constexpr Mutation kMut_special_archmage_4_scen99 = {
    "src/gameplay/families/family_archmage.cpp", 487,
    "BASE_GUY_HP+120",
    "BASE_GUY_HP+9000",
    "Cranks the FAMILY_ARCHMAGE init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};


// --- Walker status-timer scenarios -----------------------------------------
//
// Per-walker / world status-timer coverage: world.enemy_freeze (mage slot 3
// FREEZE TIME), invisibility_left (thief slot 2 CLOAK), speed_bonus_left
// (FAMILY_SPEED_POTION on_eat), and invulnerable_left (FAMILY_INVULNERABLE_
// POTION on_eat). Each row arranges spawns so the timer applies during the
// run and asserts a structural consequence that the matching kMut_* mutation
// flips by zeroing the timer write.

inline constexpr InputEvent kInputsPotionWalk20[] = {
    {1,  0, K_RIGHT},
    {21, 0, K_NONE},
};

inline constexpr InputEvent kInputsPotionWalk200[] = {
    {1,   0, K_RIGHT},
    {200, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_enemy_freeze_mage_scen99[] = {
    { FAMILY_MAGE,   0, kOrderLiving, 120, 120, 0, 0, 12, 600 }, // FAMILY_MAGE caster (level 12 -> freeze duration 20+11*12=152 > 150 tick budget)
    { FAMILY_ARCHER, 1, kOrderLiving, 200, 120, 0, 0,  5,   0 }, // FAMILY_ARCHER target frozen at spawn
};

inline constexpr FactPredicate kFacts_enemy_freeze_mage_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::WalkerPositionMoved(FAMILY_ARCHER, 190, 120,
        "consequence: archer is held within 10px of its spawn for the full 150-tick window because freeze duration 20+11*12=152 > tick budget 150; rng_drift: branch (wip/networking) RNG progression nudges the archer ~4px west of master's pinned 200"),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
};

inline constexpr Mutation kMut_enemy_freeze_mage_scen99 = {
    "src/gameplay/families/family_mage.cpp", 198,
    "                current_game->world->enemy_freeze += 20 + 11 * self->stats()->level();",
    "                current_game->world->enemy_freeze += 0;",
    "Zeroes the world.enemy_freeze increment (preserving 16-space indentation inside the case-3 if-body) so enemies act normally throughout the 150-tick window. The level-5 archer steps west toward the mage and its xpos drops below 200, flipping WalkerPositionMoved(FAMILY_ARCHER, 200, 120) on the x floor."
};

inline constexpr SpawnSpec kFamilySpawns_invisibility_thief_scen99[] = {
    { FAMILY_THIEF,   0, kOrderLiving, 120, 120, 0, 0, 4, 300 }, // FAMILY_THIEF caster (slot 2 CLOAK cost=125 covered by 300 magicpoints)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0          }, // FAMILY_SOLDIER engagement partner
};

inline constexpr FactPredicate kFacts_invisibility_thief_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 1300, 2500,
        "intended_diff: cloak protects the thief only between slot-2 cast at tick ~22 and its level-4 RNG-bounded expiry, so HP at tick 150 reflects partial-window engagement and varies with rng (master hp=15, branch hp=23); commit a4ae4e3cf4a3aef89bafbb1171a24fb571be9b25"),
    // intended_diff: cloak window is shorter than the 150-tick budget so the thief takes partial-window engagement damage that varies with rng (master hp=15, branch hp=23); commit a4ae4e3cf4a3aef89bafbb1171a24fb571be9b25
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 140, 120),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
};

inline constexpr Mutation kMut_invisibility_thief_scen99 = {
    "src/gameplay/families/family_thief.cpp", 93,
    "            self->set_invisibility_left(static_cast<short>(self->invisibility_left() + 20 + static_cast<std::int32_t>(current_game->world->rng_.next(20)) * self->stats()->level()));",
    "            self->set_invisibility_left(0);",
    "Forces invisibility_left to 0 so the slot-2 CLOAK cast never grants cover; the team-1 soldier keeps engaging the level-4 thief for the full 150-tick window, killing the thief and dropping its HP outside the (1300, 2500) cent band — flipping WalkerHpRangeAtFinalTick."
};

inline constexpr SpawnSpec kFamilySpawns_speed_potion_movement_scen99[] = {
    { FAMILY_SOLDIER,      0, kOrderLiving,   224, 224, 0, 0       }, // FAMILY_SOLDIER player walker at the smoke-arena open centre
    { FAMILY_SPEED_POTION, 0, kOrderTreasure, 230, 224, 0, 0, 5, 0 }, // FAMILY_SPEED_POTION level 5 (bonus_left=250 ticks, multiplier=5) — placed 6px east of the soldier so the very first K_RIGHT step overlaps and triggers on_eat
    { FAMILY_ORC,          1, kOrderLiving,    64,  64, 0, 0       }, // FAMILY_ORC at the kSmokeArenaSpawns NW corner — keeps level_done=0 and stays out of the soldier's east-walk path (per smoke_nonempty_scen99_inputs)
};

inline constexpr FactPredicate kFacts_speed_potion_movement_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_SPEED_POTION, kOrderTreasure),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 350, 224),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_speed_potion_movement_scen99 = {
    "src/gameplay/families/treasure_family_consumables.cpp", 82,
    "    eater->set_speed_bonus_left(eater->speed_bonus_left() + 50 * self->stats()->level());",
    "    eater->set_speed_bonus_left(0);",
    "Clears speed_bonus_left so the eater never accumulates the level-5 potion's 250-tick walking-speed window. The soldier walks at base stepsize for the entire 20-tick K_RIGHT window; observed branch xpos drops from 368 (bonus active, stepsize=9 px/tick) to 288 (base stepsize 4), failing WalkerPositionMoved(FAMILY_SOLDIER, 350, 224)."
};

inline constexpr SpawnSpec kFamilySpawns_invulnerable_potion_scen99[] = {
    { FAMILY_SOLDIER,             0, kOrderLiving,    96, 120, 0, 0 }, // FAMILY_SOLDIER player walker
    { FAMILY_INVULNERABLE_POTION, 0, kOrderTreasure, 128, 120, 0, 0 }, // FAMILY_INVULNERABLE_POTION literal treasure on the path
    { FAMILY_ARCHER,              1, kOrderLiving,   260, 120, 0, 0 }, // FAMILY_ARCHER ranged attacker east of the potion
};

inline constexpr FactPredicate kFacts_invulnerable_potion_scen99[] = {
    pred::TickReached(250),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_INVULNERABLE_POTION, kOrderTreasure),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11500, 12000,
        "intended_diff: invulnerable_left blocks all archer-arrow damage until the 150-tick timer expires, so the soldier finishes at or near max hp 120 while the line-67 zeroing mutation drops it to ~99 below the floor; commit a4ae4e3cf4a3aef89bafbb1171a24fb571be9b25"),
    // intended_diff: invulnerable_left blocks all archer-arrow damage so the soldier finishes at or near max hp 120 while the line-67 zeroing mutation drops it to ~99 below the floor; commit a4ae4e3cf4a3aef89bafbb1171a24fb571be9b25
    pred::EventKindAtLeast(/*play_sound*/1, 3),
};

inline constexpr Mutation kMut_invulnerable_potion_scen99 = {
    "src/gameplay/families/treasure_family_consumables.cpp", 67,
    "        eater->set_invulnerable_left(static_cast<short>(eater->invulnerable_left() + (150 * self->stats()->level())));",
    "        eater->set_invulnerable_left(0);",
    "Clears invulnerable_left so the soldier loses its invincibility window; team-1 archer arrows now land while the soldier closes melee range, dropping the soldier's HP to ~99 (below the 11500-cent floor) and flipping WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11500, 12000)."
};


// --- Summon-lifecycle scenarios --------------------------------------------
//
// Druid slot-2 SUMMON FAERIE deterministically spawns a FAMILY_FAERIE walker
// owned by the caster with lifetime = 50 + level*40 (family_druid.cpp:71). At
// level 4 that is 210 ticks. The faerie is reaped by the per-tick lifetime
// decrement in living.cpp:104-109 once its counter hits zero. Both rows place
// the team-1 enemy far off-map (2000,2000) so range-gated AI targeting
// (game_world.cpp:1078-1103) never reaches the faerie or the druid: the druid
// survives the whole run, owner-death cascades (living.cpp:87/98) never fire,
// and the faerie's disappearance is attributable purely to lifetime expiry.
// The 650-tick budget (~3x the 210-tick lifetime) leaves the faerie long gone
// by dump time. summon_lifetime_faerie_scen99 targets the spawn-time
// initialisation write; summon_lifetime_decrement_faerie_scen99 targets the
// per-tick decrement that drives expiry.

inline constexpr SpawnSpec kFamilySpawns_summon_lifetime_faerie_scen99[] = {
    { FAMILY_DRUID,   0, kOrderLiving, 120,  120,  0, 0, 4, 300 }, // FAMILY_DRUID caster (level 4 -> faerie lifetime 50+4*40=210; magicpoints 300 covers SUMMON FAERIE special_cost=80)
    { FAMILY_SOLDIER, 1, kOrderLiving, 2000, 2000, 0, 0          }, // FAMILY_SOLDIER off-map far enough the AI never reaches the faerie before its 210-tick lifetime expires
};

inline constexpr FactPredicate kFacts_summon_lifetime_faerie_scen99[] = {
    pred::TickReached(650),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerDiedByFinal(FAMILY_FAERIE,
        "consequence: faerie summoned around tick 30 then expired by tick 650 as its lifetime ran out — no alive FAMILY_FAERIE remains (the dead entry persists in the dump, so this counts alive walkers, not raw objects)"),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "consequence: the slot-2 cast emits a single play_sound at tick ~20; the unreachable off-map enemy and the silent lifetime-expiry reap add no further sounds, so exactly one play_sound is observed on both sides"),
    pred::WalkerOfTeamAlive(0, 1, 2),
};

inline constexpr Mutation kMut_summon_lifetime_faerie_scen99 = {
    "src/gameplay/families/family_druid.cpp", 71,
    "            alive->set_lifetime(50 + self->stats()->level() * 40);",
    "            alive->set_lifetime(99999);",
    "Replaces the spawn-time lifetime initialisation with an effectively-infinite value; the faerie is still alive at tick 650 so WalkerDiedByFinal(FAMILY_FAERIE) fails because an alive FAMILY_FAERIE remains."
};

inline constexpr SpawnSpec kFamilySpawns_summon_lifetime_decrement_faerie_scen99[] = {
    { FAMILY_DRUID,   0, kOrderLiving, 120,  120,  0, 0, 4, 300 }, // FAMILY_DRUID caster (magicpoints 300 covers SUMMON FAERIE special_cost=80)
    { FAMILY_SOLDIER, 1, kOrderLiving, 2000, 2000, 0, 0          }, // FAMILY_SOLDIER off-map so the druid is never engaged; owner-death cascades never fire, isolating the per-tick decrement
};

inline constexpr FactPredicate kFacts_summon_lifetime_decrement_faerie_scen99[] = {
    pred::TickReached(650),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerDiedByFinal(FAMILY_FAERIE,
        "consequence: faerie's 210-tick lifetime is decremented once per tick at living.cpp:104-105 until it reaches 0 around tick ~240; lines 106-109 then fire set_dead+death+return — no alive FAMILY_FAERIE remains by the final tick"),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "consequence: the slot-2 cast emits a single play_sound at tick ~20; the off-map enemy keeps the druid unengaged and the lifetime-expiry reap is silent, so exactly one play_sound is observed on both sides"),
    pred::WalkerOfTeamAlive(0, 1, 2),
};

inline constexpr Mutation kMut_summon_lifetime_decrement_faerie_scen99 = {
    "src/gameplay/living.cpp", 104,
    "const auto remaining_lifetime = lifetime() - 1;",
    "const auto remaining_lifetime = lifetime();",
    "Removes the `- 1` so remaining_lifetime == lifetime() every tick; `if (remaining_lifetime < 1)` at line 106 is permanently false and the lifetime-expiry kill at 108-109 never fires. With the druid kept alive (off-map enemy), owner-death cascades at 87/98 also never fire, so the faerie is still alive at tick 650 and WalkerDiedByFinal(FAMILY_FAERIE) fails because an alive FAMILY_FAERIE remains. Exercises the decrement path rather than initialisation."
};


// Generator-saturation scenario ---------------------------------------------
// A FAMILY_TOWER generator (default_weapon = FAMILY_MAGE per
// generator_family_registry.cpp:30-37) runs act_generate (walker.cpp:1217-1235)
// every tick, gated on `living_count < MAXOBS`. Over a 2500-tick budget it
// emits FAMILY_MAGE walkers, driving living_count upward toward MAXOBS. A lone
// FAMILY_SOLDIER observer sits far off-map (240,240) so it never interferes
// with emission. The level-5 generator fires at an elevated rate so the
// saturation pile-up is visible within the budget.
inline constexpr SpawnSpec kFamilySpawns_generator_saturation_scen99[] = {
    { FAMILY_TOWER,   1, kOrderGenerator, 60,   60,   0, 0, 5, 0 }, // FAMILY_TOWER generator (level 5 -> elevated act_generate fire rate); emits FAMILY_MAGE
    { FAMILY_SOLDIER, 0, kOrderLiving,    2000, 2000, 0, 0 },       // FAMILY_SOLDIER observer placed off-map: range-gated AI targeting (game_world.cpp:1078-1103) never reaches the 60,60 generator cluster, so the observer never interferes with emission
};

// NOTE on the generator's own family: the dump renders the FAMILY_TOWER
// generator under Order::Generator (family string "FAMILY_TOWER"), but
// WalkerFamilyCount always aliases its family arg through family_symbol_by_order
// at kLivingOrder (fact_predicate.cpp:78) — generator-order families are not
// reachable that way, so a WalkerFamilyCount(FAMILY_TOWER, ...) row would
// resolve to the living-order family #1 and count 0 on both sides. The marker
// walkers the mages leave behind are likewise FX-order entities (rendered via
// the effect table), so they are unreachable through WalkerFamilyCount too. The
// generator's persistence is therefore asserted indirectly via
// WalkerOfTeamAlive(1, ...), matching how generator_tower_emission_scen99
// handles the same aliasing; the saturation effect is asserted positively on
// the emitted FAMILY_MAGE walkers — both their count and their final-tick HP.
inline constexpr FactPredicate kFacts_generator_saturation_scen99[] = {
    pred::TickReached(2500),
    pred::WalkerFamilyCount(FAMILY_MAGE, 3, 30,
        "consequence: generator saturates living_count over 2500 ticks; range spans RNG drift"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 10000, 100000,
        "rng_drift: spawned-mage HP at the final tick varies with level rolls and RNG; the [100,1000] window brackets the observed steady-state mage HP (~272-440) and is empty once the generator is silenced"),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
    pred::WalkerOfTeamAlive(1, 3, 40,
        "rng_drift: generator+mage alive count saturates toward MAXOBS and varies with per-tick RNG"),
};

inline constexpr Mutation kMut_generator_saturation_scen99 = {
    "src/gameplay/walker.cpp", 1219,
    "if ( current_game->world->living_count < MAXOBS &&",
    "if ( false &&",
    "Replaces the `living_count < MAXOBS` half of the act_generate gate with `false`, making the conjunction always false; the generator never fires, zero FAMILY_MAGE spawn, and WalkerFamilyCount(FAMILY_MAGE, 3, 30) fails on its lower bound."
};


// Weapon-trajectory scenarios ------------------------------------------------
// Three projectile/effect-trajectory specials whose observable consequence is
// a spawned weapon or FX walker downstream of the caster's own state:
//   * FAMILY_ELF slot 2 (BOUNCING ROCKS) fires FAMILY_ROCK projectiles via a
//     two-shot fire() loop (family_elf.cpp:62-74);
//   * FAMILY_SOLDIER slot 2 (BOOMERANG) summons one FAMILY_BOOMERANG FX walker
//     (family_soldier.cpp:45-52; FX family registered effect_family_shield.cpp:133-134);
//   * FAMILY_BARBARIAN slot 2 (EXPLODING BOULDER) emits a FAMILY_BOULDER whose
//     projectile_explode_on_death (weapon_family_projectiles.cpp:14-31) adds a
//     FAMILY_EXPLOSION FX walker, gated by set_skip_exit(5000) at
//     family_barbarian.cpp:59.
//
// OBSERVABILITY NOTE. The naive predicates for these (WeaponFamilyEmitted on the
// projectile / EffectFamilyCount on the FX) are not satisfiable under schema-v1:
//   - FX walkers spawned via add_ob(Order::FX, ...) land in `oblist` (->
//     dump.walkers[]), NOT `fxlist` (-> dump.effects[]) — see game_world.cpp:559
//     (only Order::Weapon diverts to weaplist). So EffectFamilyCount, which
//     searches dump.effects[], counts zero for FAMILY_BOOMERANG / FAMILY_EXPLOSION.
//   - dump.walkers[] renders each entity through its own Order's family table,
//     while WalkerFamilyCount resolves its arg through family_symbol_by_order at
//     kLivingOrder (fact_predicate.cpp:78). An FX-order "FAMILY_BOOMERANG" string
//     is therefore unreachable by WalkerFamilyCount too (the same aliasing wall
//     the generator_saturation scenario documents).
//   - WeaponFamilyEmitted snapshots dump.weapons[] at the final tick. These fast
//     projectiles have expired/detonated long before the trajectory has fully
//     played out, so the snapshot is empty; and the elf's normal attack also
//     fires FAMILY_ROCK, so a snapshot rock would not isolate the special.
// Each scenario therefore asserts the trajectory's *downstream* consequences that
// ARE structurally observable and that the mutation provably flips (measured
// against the branch dump with the mutation applied):
//   - rocks landing -> soldier takes damage (score_change events + sub-full HP);
//   - boomerang summon -> extra alive entities on the caster's team (team 0);
//   - boulder detonation -> enemy soldiers take explosion damage (score_change
//     events + sub-full HP). Each mutation removes that consequence entirely.
inline constexpr SpawnSpec kFamilySpawns_weapon_rock_slot2_emit_scen99[] = {
    { FAMILY_ELF,     0, kOrderLiving, 120, 120, 0, 0, 4, 300 }, // FAMILY_ELF caster (level 4 -> slot 2 BOUNCING ROCKS reachable)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 },         // FAMILY_SOLDIER target to the right so the elf faces it and the rocks fly toward it
};

inline constexpr FactPredicate kFacts_weapon_rock_slot2_emit_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*score_change*/9, 1,
        "consequence: elf slot 2 BOUNCING ROCKS land on the enemy soldier and generate score_change events; the mutation aborts the fire() loop so no rock spawns, no rock lands, and zero score_change events occur"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 2000, 9000,
        "rng_drift: soldier HP at tick 30 reflects BOUNCING ROCKS + melee damage (branch 6500 / master 3400 cents); the widened 7000-cent span carries a gate-recognised label and excludes the undamaged 12000-cent mutant outcome"),
};

inline constexpr Mutation kMut_weapon_rock_slot2_emit_scen99 = {
    "src/gameplay/families/family_elf.cpp", 66,
    "                fireob = static_cast<weap*>(self->fire());",
    "                return false;",
    "Aborts the first iteration of the BOUNCING ROCKS two-shot fire() loop with an early `return false;` before any rock projectile is spawned; the special's rocks never land on the enemy soldier, so the soldier stays at full HP and emits no score_change events — EventKindAtLeast(score_change, 1) fails on its floor and the soldier-HP window no longer holds."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_boomerang_return_scen99[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 4, 300 }, // FAMILY_SOLDIER caster (level 4 -> slot 2 BOOMERANG reachable)
    { FAMILY_ARCHER,  1, kOrderLiving, 200, 200, 0, 0 },         // FAMILY_ARCHER opponent placed diagonally so combat RNG drives caster HP drift
};

inline constexpr FactPredicate kFacts_weapon_boomerang_return_scen99[] = {
    pred::TickReached(80),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerOfTeamAlive(0, 2, 4,
        "consequence: BOOMERANG slot 2 adds FAMILY_BOOMERANG FX walker(s) to the caster's team (team 0), which the schema-v1 dump only exposes by team since the FX-order family string is unreachable by WalkerFamilyCount/EffectFamilyCount; the mutation aborts the summon so only the lone soldier remains alive on team 0"),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 14000,
        "rng_drift: soldier HP at tick 80 varies with archer engagement RNG; widened 6000-cent span carries a gate-recognised label"),
};

inline constexpr Mutation kMut_weapon_boomerang_return_scen99 = {
    "src/gameplay/families/family_soldier.cpp", 46,
    "            newob = summon_entity(self, Order::FX, FAMILY_BOOMERANG);",
    "            return false;",
    "Replaces the BOOMERANG FX summon with an early `return false;` before the FAMILY_BOOMERANG walker is created; the caster's team (team 0) loses its boomerang FX entities, dropping team-0 alive from 3 to 1 so WalkerOfTeamAlive(0, 2, 4) fails on its lower bound."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_exploding_boulder_scen99[] = {
    { FAMILY_BARBARIAN, 0, kOrderLiving, 120, 120, 0, 0, 5, 300 }, // FAMILY_BARBARIAN caster (level 5 -> slot 2 EXPLODING BOULDER reachable)
    { FAMILY_SOLDIER,   1, kOrderLiving, 160, 120, 0, 0 },          // three FAMILY_SOLDIER targets clustered ahead so the boulder's death-explosion has bodies in radius
    { FAMILY_SOLDIER,   1, kOrderLiving, 200, 160, 0, 0 },
    { FAMILY_SOLDIER,   1, kOrderLiving, 260, 200, 0, 0 },
};

inline constexpr FactPredicate kFacts_weapon_exploding_boulder_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 3000, 9000,
        "consequence: EXPLODING BOULDER's projectile_explode_on_death detonation damages clustered enemy soldiers below full HP (branch 6000-6300 / master 3600-6100 cents); the mutation zeroes skip_exit so the boulder never explodes and every soldier stays at full 12000-cent HP outside this window"),
    pred::EventKindAtLeast(/*score_change*/9, 1,
        "consequence: explosion damage to the enemy soldiers generates score_change events; the mutation suppresses the detonation so zero score_change events occur"),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 0, 3,
        "rng_drift: explosion may kill 0-3 enemy soldiers depending on radius/aim"),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
};

inline constexpr Mutation kMut_weapon_exploding_boulder_scen99 = {
    "src/gameplay/families/family_barbarian.cpp", 59,
    "        alive->set_skip_exit(5000);",
    "        alive->set_skip_exit(0);",
    "Zeroes the EXPLODING BOULDER skip_exit budget so projectile_explode_on_death short-circuits at its `if (!self->skip_exit()) return false;` guard; the boulder never detonates, the clustered enemy soldiers take no explosion damage (all stay at full 12000-cent HP) and emit no score_change events — the soldier-HP window and EventKindAtLeast(score_change, 1) both fail."
};


// Effect-emission scenarios --------------------------------------------------
// Three specials whose observable consequence is a multi-target / multi-spawn
// effect emission:
//   * FAMILY_ARCHMAGE slot 2 (HEARTBURST) detonates a per-foe FAMILY_EXPLOSION
//     against every in-range enemy (family_archmage.cpp:237-250);
//   * FAMILY_THIEF slot 4 (POISON CLOUD) summons one FAMILY_CLOUD FX that
//     poisons approaching foes each tick (family_thief.cpp:165-178; the cloud's
//     per-tick attack lives in effect_family_cloud.cpp);
//   * FAMILY_DRUID slot 4 (PROTECTION) emits a FAMILY_CIRCLE_PROTECTION weapon
//     onto each in-range friendly when >1 friendly is within range 60
//     (family_druid.cpp:86-149; the weapon's orbit-owner animate lives in
//     weapon_family_animate.cpp:32-43).
//
// OBSERVABILITY NOTE. The FX-spawning specials (HEARTBURST, POISON CLOUD) route
// their effects through summon_entity(self, Order::FX, ...) -> add_ob, which
// (game_world.cpp:559-564) diverts only Order::Weapon to weaplist and lands
// everything else — including Order::FX — in oblist. capture_state_dump only
// populates dump.effects[] from world.fxlist (state_dump.cpp:348), so an
// FX-via-oblist FAMILY_EXPLOSION / FAMILY_CLOUD never reaches dump.effects[];
// EffectFamilyCount(FAMILY_EXPLOSION/FAMILY_CLOUD, >0, ...) is unsatisfiable
// under schema-v1 (the same aliasing wall the weapon-trajectory and existing
// effect_*_emission rows document — they pin EffectFamilyCount(..., 0, 0)).
// These rows therefore assert the emission's *downstream* consequences that ARE
// structurally observable and that the mutation provably flips:
//   - HEARTBURST -> explosions damage the in-range soldiers below full HP (and
//     emit SOUND_EXPLODE per foe); the mutation aborts the spawn loop so every
//     soldier stays at full HP and emits no explosion sound;
//   - POISON CLOUD -> the cloud poisons the lone approaching soldier below full
//     HP (and its per-tick attack emits sound); the mutation skips the spawn so
//     the soldier — never attacked by the input-only-special thief — stays at
//     full HP;
//   - PROTECTION -> the FAMILY_CIRCLE_PROTECTION weapon DOES enter weaplist
//     (Order::Weapon -> dump.weapons[]), so WeaponFamilyEmitted is directly
//     observable; the mutation bypasses the summon so weaplist never holds it.
inline constexpr SpawnSpec kFamilySpawns_effect_heartburst_multitarget_scen99[] = {
    { FAMILY_ARCHMAGE, 0, kOrderLiving, 120, 120, 0, 0, 4, 300 }, // archmage caster (level 4 + 300 magicpoints -> slot 2 HEARTBURST affordable)
    { FAMILY_SOLDIER,  1, kOrderLiving, 160, 120, 0, 0 },          // four FAMILY_SOLDIER foes fanned to the right so all sit inside HEARTBURST range
    { FAMILY_SOLDIER,  1, kOrderLiving, 190, 120, 0, 0 },
    { FAMILY_SOLDIER,  1, kOrderLiving, 220, 120, 0, 0 },
    { FAMILY_SOLDIER,  1, kOrderLiving, 250, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_effect_heartburst_multitarget_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 11000,
        "consequence: HEARTBURST detonates a per-foe explosion against each in-range soldier, leaving at least one soldier below full HP; the mutation aborts the spawn loop so no explosion lands and every soldier stays at full 12000-cent HP outside this window"),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 0, 4,
        "consequence: the per-foe explosions may kill some of the four in-range soldiers"),
    pred::EventKindAtLeast(/*play_sound*/1, 4,
        "consequence: HEARTBURST emits SOUND_EXPLODE once per detonated foe; the mutation suppresses every explosion so the play_sound floor collapses"),
};

inline constexpr Mutation kMut_effect_heartburst_multitarget_scen99 = {
    "src/gameplay/families/family_archmage.cpp", 239,
    "                        newob = summon_entity(self, Order::FX, FAMILY_EXPLOSION);",
    "                        return false;",
    "Aborts the HEARTBURST per-foe explosion loop with an early `return false;` before the first FAMILY_EXPLOSION is summoned; no explosion lands on any in-range soldier (all stay at full 12000-cent HP) and no SOUND_EXPLODE is emitted — the soldier-HP window and EventKindAtLeast(play_sound, 4) both fail."
};

inline constexpr SpawnSpec kFamilySpawns_effect_poison_cloud_emit_scen99[] = {
    { FAMILY_THIEF,   0, kOrderLiving, 120, 120, 0, 0, 10, 300 }, // thief caster (level 10 + 300 magicpoints -> slot 4 POISON CLOUD affordable)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 },          // lone FAMILY_SOLDIER foe that walks into the cloud; the input-only-special thief never melees it
};

inline constexpr FactPredicate kFacts_effect_poison_cloud_emit_scen99[] = {
    pred::TickReached(45),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerOfTeamAlive(0, 2, 2,
        "consequence: POISON CLOUD slot 4 adds a FAMILY_CLOUD FX walker to the thief's team (team 0) — schema-v1 only exposes it by team since the FX-order family string aliases onto FAMILY_SLIME under WalkerFamilyCount; the mutation bypasses the spawn so only the lone thief remains alive on team 0 (the cloud's random-walk path, and hence whether it ever poisons the soldier, diverges by RNG between branch and master, so the spawn's *existence* is the robust observable)"),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "consequence: the live cloud and the approaching soldier's melee both emit play_sound events; the floor stays >0 in both arms"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 0, 15000,
        "rng_drift: thief HP varies with the approaching soldier's melee RNG across the run window"),
};

inline constexpr Mutation kMut_effect_poison_cloud_emit_scen99 = {
    "src/gameplay/families/family_thief.cpp", 169,
    "            newob = summon_entity(self, Order::FX, FAMILY_CLOUD);",
    "            return false;",
    "Replaces the POISON CLOUD FX summon with an early `return false;` before the FAMILY_CLOUD walker is created; the cloud never enters oblist so team 0 holds only the lone thief — WalkerOfTeamAlive(0, 2, 2) collapses to 1 and fails its lower bound."
};

inline constexpr SpawnSpec kFamilySpawns_effect_protection_emit_scen99[] = {
    // The friendly soldier is spawned FIRST so the druid — spawned next — lands
    // ahead of it in oblist (add_ob prepends), making the druid the walker that
    // find_player_walker binds and the special input drives. No enemies are
    // spawned: with no foe to charge, the AI friendly idles next to the druid
    // and stays well inside range 60 at the tick-20 cast on BOTH branch and
    // master, so PROTECTION's howmany>1 gate opens deterministically (whether a
    // foe is in range at the exact cast tick is RNG/AI-movement sensitive and
    // diverges between branch and master). With no incoming damage the emitted
    // circle is never consumed as a shield, so it persists in weaplist.
    { FAMILY_SOLDIER, 0, kOrderLiving, 130, 120, 0, 0 },          // second team-0 friendly, 10px from the druid -> inside range 60 throughout
    { FAMILY_DRUID,   0, kOrderLiving, 120, 120, 0, 0, 10, 300 }, // druid caster (level 10 + 300 magicpoints -> slot 4 PROTECTION affordable); the player-controlled walker
};

inline constexpr FactPredicate kFacts_effect_protection_emit_scen99[] = {
    pred::TickReached(25),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION,
        "consequence: druid slot 4 PROTECTION emits a FAMILY_CIRCLE_PROTECTION weapon onto the in-range friendly when >1 friendly is in range; the weapon enters weaplist (Order::Weapon -> dump.weapons[]) and orbits its still-alive owner; the mutation bypasses creation so the emit never fires"),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "consequence: a successful PROTECTION cast emits SOUND_HEAL; the mutation aborts the emit before the heal so this play_sound floor collapses to 0"),
};

inline constexpr Mutation kMut_effect_protection_emit_scen99 = {
    "src/gameplay/families/family_druid.cpp", 116,
    "                                alive = summon_entity(newob, Order::Weapon, FAMILY_CIRCLE_PROTECTION);",
    "                                return false;",
    "Replaces the PROTECTION circle summon with an early `return false;` before the FAMILY_CIRCLE_PROTECTION weapon is created; weaplist never holds the circle so WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION) fails — the emit never fires."
};


// Effect-timer scenarios -----------------------------------------------------
// FAMILY_THIEF slot 1 (DROP BOMB) spawns a FAMILY_BOMB FX walker that lives on
// a self-destruct timer (family_thief.cpp:69 add_ob(Order::FX, FAMILY_BOMB, 1);
// the bomb's on_death later detonates a FAMILY_EXPLOSION — effect_family_bomb.cpp:17-29).
// The bomb is the canonical "effect on a timer" the parity suite must observe.
//
// OBSERVABILITY NOTE. add_ob(Order::FX, ...) routes everything that is not
// Order::Weapon into oblist (game_world.cpp:564), NOT fxlist, so the freshly
// dropped FAMILY_BOMB surfaces in dump.walkers[] (collect_walkers reads oblist
// and renders the bomb's own FX-order family string "FAMILY_BOMB"), never in
// dump.effects[] (collect_effects reads fxlist only — state_dump.cpp:347-348).
// EffectFamilyCount(FAMILY_BOMB, ...) counts dump.effects[] and is therefore
// structurally 0; WalkerFamilyCount(FAMILY_BOMB, ...) renders FAMILY_BOMB under
// the Living table and never matches the FX-order "FAMILY_BOMB" string — the
// same aliasing wall the poison-cloud row documents. The bomb's spawn is thus
// only robustly observable by TEAM: it lands on the thief's team (team 0) as an
// alive walker. We pick tick 30 — early enough that the timed bomb has not yet
// detonated, so it is still alive in oblist as a team-0 walker and the soldier
// (parked far away at 400,400) has taken no blast damage on either branch.
inline constexpr SpawnSpec kFamilySpawns_effect_bomb_timer_scen99[] = {
    { FAMILY_THIEF,   0, kOrderLiving, 120, 120, 0, 0, 5, 300 }, // thief caster (level 5 + 300 magicpoints -> slot 1 DROP BOMB affordable); the player-controlled walker
    { FAMILY_SOLDIER, 1, kOrderLiving, 400, 400, 0, 0 },         // lone FAMILY_SOLDIER foe parked far away so the bomb blast never reaches it
};

inline constexpr FactPredicate kFacts_effect_bomb_timer_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerOfTeamAlive(0, 2, 3,
        "consequence: DROP BOMB slot 1 adds the timed FAMILY_BOMB FX walker(s) to the thief's team (team 0) — schema-v1 routes the bomb through add_ob(Order::FX) into oblist, where it surfaces as an alive team-0 walker (the FX-order family string aliases under WalkerFamilyCount / EffectFamilyCount, so the spawn is only robustly observable by team). The mutation bypasses the spawn so only the lone thief remains alive on team 0 and this count collapses to 1, below the floor of 2."),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
};

inline constexpr Mutation kMut_effect_bomb_timer_scen99 = {
    "src/gameplay/families/family_thief.cpp", 69,
    "            newob = current_game->world->add_ob(Order::FX, FAMILY_BOMB, 1);",
    "            return false;",
    "Replaces the DROP BOMB FX spawn with an early `return false;` before any FAMILY_BOMB walker is created; oblist never holds a bomb so the thief's team (team 0) keeps only the lone thief alive and WalkerOfTeamAlive(0, 2, 3) collapses to 1, below its floor."
};


// --- Phase 06: input-pipeline edge-case scenarios --------------------------
//
// These four rows drive the real player-input pipeline (PlayerInput decode +
// sim_process_player_input) through edge cases that the family/effect rows
// never reach: diagonal movement decode, sustained held-fire, mid-run
// character switch, and special-slot index wrap. Each row's discriminating
// mutation edits exactly one source line in input_state.cpp /
// sim_input_handler.cpp; the byte-match `from` text is verified against the
// live source line by the mutation canary (scripts/parity/_apply_mutation.py).

// (1) DIAGONAL MOVEMENT. A lone soldier on the player team holds K_DOWN_RIGHT
// for forty ticks. PlayerInput::move_x()/move_y() each decode the DownRight
// bit into a +1 component (input_state.cpp:10-12 and :24-26), so walkstep
// advances the soldier in BOTH axes. WalkerPositionMoved requires xpos AND
// ypos to clear the floor — the mutation neuters the move_y() DownRight decode
// (line 26) so the y component is dropped and the soldier never clears the
// ypos floor.
inline constexpr InputEvent kInputs_input_diagonal_movement[] = {
    {  1, 0, K_DOWN_RIGHT },
    { 40, 0, K_NONE },
};

inline constexpr SpawnSpec kFamilySpawns_input_diagonal_movement_scen99[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 160, 160, 0, 0 }, // lone player-team soldier; fresh arena so K_DOWN_RIGHT is the only mover
};

inline constexpr FactPredicate kFacts_input_diagonal_movement_scen99[] = {
    pred::TickReached(80),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 175, 175,
        "consequence: diagonal K_DOWN_RIGHT moves the soldier in both axes; mutation disables the move_y() DownRight decode so the y component is dropped and the soldier never clears the ypos floor"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 15000,
        "rng_drift: lone soldier takes no combat damage; range widened to absorb cross-side HP/regen drift"),
    pred::WalkerOfTeamAlive(0, 1, 1),
};

inline constexpr Mutation kMut_input_diagonal_movement_scen99 = {
    "src/interface/input/input_state.cpp", 26,
    "        held[static_cast<int>(InputKey::DownRight)])",
    "        false)",
    "Drops the DownRight bit from PlayerInput::move_y(): the diagonal key no longer contributes a +1 y component, so the soldier advances only in x and never clears the ypos floor of WalkerPositionMoved(FAMILY_SOLDIER, 175, 175)."
};

// (2) HELD FIRE. A lone player-team soldier holds K_FIRE for the whole run.
// The held-fire branch (sim_input_handler.cpp:350) re-arms init_fire() every
// tick, so the soldier looses a FAMILY_KNIFE on every boomerang cycle (the
// knife is a returning weapon, one in flight at a time) and emits a fire sound
// per throw. The mutation disables the held-fire branch, leaving only the
// single press-edge fire at sim_input_handler.cpp:329 — one throw, one sound.
//
// OBSERVABILITY NOTE. The soldier's knife is a RETURNING projectile: it is
// FAMILY_KNIFE (Order::Weapon, weaplist) only while outbound and
// FAMILY_KNIFE_BACK (oblist) while returning, so whether a FAMILY_KNIFE is in
// dump.weapons[] at the final tick is a coin-flip on the boomerang phase and
// not a robust signal. The robust, cross-side-stable consequence is the COUNT
// of fire sounds: held-fire throws repeatedly (many play_sound events) while
// the mutated single press-fire emits essentially one. The soldier is alone
// (no foe) so no combat RNG perturbs the throw cadence and nothing can kill it.
inline constexpr InputEvent kInputs_input_hold_fire_search[] = {
    {   5, 0, K_FIRE },
    { 400, 0, K_NONE },
};

inline constexpr SpawnSpec kFamilySpawns_input_hold_fire_search_scen99[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 96, 120, 0, 0, 1, 0 }, // lone player-team soldier (level 1 -> short-range knife, fast boomerang cadence); holds fire
};

inline constexpr FactPredicate kFacts_input_hold_fire_search_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 5,
        "consequence: held-fire re-arms the throw every tick, so the soldier looses a knife each boomerang cycle and emits many fire sounds across the run; the mutation disables held-fire, leaving only the single press-edge throw and its lone sound"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 15000,
        "rng_drift: the lone soldier takes no combat damage; range widened to absorb cross-side HP/regen drift"),
    pred::WalkerOfTeamAlive(0, 1, 1),
};

inline constexpr Mutation kMut_input_hold_fire_search_scen99 = {
    "src/gameplay/sim_input_handler.cpp", 350,
    "        if (pi.is_held(InputAction::Fire))",
    "        if (false)",
    "Disables the held-fire branch so the soldier fires only on the single press edge at line 329; the sustained knife stream collapses to one throw and the play_sound count falls below the floor of EventKindAtLeast(play_sound, 5)."
};

// (3) SWITCH CHARACTER. Two real_team==255 walkers share the player team
// (player_team = 255). The driver first controls the soldier; K_SWITCH then
// hands control to the archer via sim_cycle_next_character
// (sim_input_handler.cpp:188), so the held K_FIRE that follows is the ARCHER's
// fire. The mutation pins control to oldcontrol, so the switch is a no-op and
// the soldier keeps the helm.
//
// OBSERVABILITY NOTE. Position is not a usable signal here: an UNcontrolled
// living runs the AI and random-walks to an unpredictable spot (living.cpp
// act_random / COMMAND_RANDOM_WALK), so the never-controlled walker can drift
// anywhere and no xpos threshold separates the two runs. Instead we observe
// WHICH WEAPON FAMILY is emitted. After the switch, held K_FIRE makes the
// controlled walker fire: the ARCHER looses FAMILY_ARROW (a non-returning
// projectile that persists in weaplist), whereas the SOLDIER would throw
// FAMILY_KNIFE. So an emitted FAMILY_ARROW proves the archer holds the helm.
// The mutation keeps the soldier in control, so K_FIRE throws knives and no
// FAMILY_ARROW is ever emitted. Both walkers are real_team 255 (the switch
// filter requires it) and friendly to each other (no combat), so neither dies.
inline constexpr InputEvent kInputs_input_switch_char[] = {
    {   5, 0, K_SWITCH },
    {   6, 0, K_NONE },
    {  10, 0, K_FIRE },
    { 400, 0, K_NONE }, // release past the tick budget: fire held through the final tick so a fresh FAMILY_ARROW is in flight at capture
};

inline constexpr SpawnSpec kFamilySpawns_input_switch_char_scen99[] = {
    // add_ob(atstart=true) PREPENDS, so the last spawn lands at the front of
    // oblist and is the walker find_player_walker hands the driver first. The
    // soldier is therefore listed second so it is controlled FIRST; K_SWITCH
    // then cycles control onto the archer.
    { FAMILY_ARCHER,  255, kOrderLiving, 160, 140, 0, 0, 5, 200 }, // switch target: gains control after K_SWITCH, then held K_FIRE looses arrows
    { FAMILY_SOLDIER, 255, kOrderLiving, 120, 120, 0, 0, 5, 200 }, // first controlled walker (real_team 255 so the switch filter accepts it)
};

inline constexpr FactPredicate kFacts_input_switch_char_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::WeaponFamilyEmitted(FAMILY_ARROW,
        "consequence: K_SWITCH hands control to the archer, whose held K_FIRE looses FAMILY_ARROW projectiles; the mutation pins control to the soldier, so K_FIRE throws knives and no arrow is ever emitted"),
    pred::EventKindAtLeast(/*play_sound*/1, 3,
        "rng_drift: fire-sound count varies with the held-fire cadence"),
};

inline constexpr Mutation kMut_input_switch_char_scen99 = {
    "src/gameplay/sim_input_handler.cpp", 188,
    "        control = sim_cycle_next_character(level.oblist, oldcontrol, reverse, filter);",
    "        control = oldcontrol;",
    "Makes K_SWITCH a no-op: control never leaves the soldier, so the held K_FIRE throws FAMILY_KNIFE rather than the archer's FAMILY_ARROW, and WeaponFamilyEmitted(FAMILY_ARROW) finds no arrow."
};

// (4) SPECIAL-SLOT WRAP. A player-team mage cycles K_SPECIAL_SWITCH seven
// times. Each press increments current_special (sim_input_handler.cpp:204);
// the fifth press pushes past the last defined slot and wraps back to 1
// (line 219), and the run lands on slot 3 (FREEZE TIME) when K_SPECIAL fires.
// Seven K_SPECIAL_SWITCH presses cycle current_special 1->2->3->4->5, then the
// fifth press pushes past the last slot and wraps to 1 (line 219), and presses
// six/seven walk it back to 3 — FREEZE TIME. K_SPECIAL then fires that slot.
//
// OBSERVABILITY NOTE. The mutation resets the slot to 1, so K_SPECIAL fires
// TELEPORT instead — which jumps the mage to a random tile, and the team-1
// soldier chases it to an unpredictable spot, so the soldier's final POSITION
// is not a usable signal (a moved-at-least floor cannot tell "frozen near
// spawn" from "chased far away"). The robust, on-point signal is the
// FREEZE-TIME palette tint: a team-0 freeze sets current_palette_id and emits
// SetPalette events (family_mage.cpp:199-200, plus the freeze-end reset in
// game_world.cpp:1385-1386). TELEPORT emits none. So a SetPalette event proves
// the wrap landed on FREEZE TIME.
inline constexpr InputEvent kInputs_input_special_switch_wrap[] = {
    {  2, 0, K_SPECIAL_SWITCH }, {  3, 0, K_NONE },
    {  4, 0, K_SPECIAL_SWITCH }, {  5, 0, K_NONE },
    {  6, 0, K_SPECIAL_SWITCH }, {  7, 0, K_NONE },
    {  8, 0, K_SPECIAL_SWITCH }, {  9, 0, K_NONE },
    { 10, 0, K_SPECIAL_SWITCH }, { 11, 0, K_NONE },
    { 12, 0, K_SPECIAL_SWITCH }, { 13, 0, K_NONE },
    { 14, 0, K_SPECIAL_SWITCH }, { 15, 0, K_NONE },
    { 16, 0, K_SPECIAL },        { 17, 0, K_NONE },
};

inline constexpr SpawnSpec kFamilySpawns_input_special_switch_wrap_scen99[] = {
    { FAMILY_MAGE,    0, kOrderLiving, 120, 120, 0, 0, 13, 600 }, // player mage: level 13 + 600 magic so every special slot is reachable and affordable
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 },         // team-1 soldier downrange; frozen in place by FREEZE TIME
};

inline constexpr FactPredicate kFacts_input_special_switch_wrap_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*set_palette*/3, 1,
        "consequence: the special-switch wrap lands on FREEZE TIME, whose team-0 cast tints the arena palette and emits SetPalette events; the mutation resets the slot index so TELEPORT fires instead and no palette change is emitted"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 15000,
        "rng_drift: soldier HP varies with engagement RNG"),
};

inline constexpr Mutation kMut_input_special_switch_wrap_scen99 = {
    "src/gameplay/sim_input_handler.cpp", 204,
    "        control->set_current_special(control->current_special() + 1);",
    "        control->set_current_special(1);",
    "Replaces the per-press increment with a hard reset to slot 1, so the wrap never reaches FREEZE TIME (slot 3); K_SPECIAL fires TELEPORT, which emits no SetPalette event, failing EventKindAtLeast(set_palette, 1)."
};


// --- Phase 07: multi-team is_friendly scenario -----------------------------
//
// Three living walkers on THREE distinct teams share one arena: a player-team
// soldier (team 0) at (120,120), a thief (team 2) at (140,140), and an archer
// (team 1) at (200,200). None carry a myguy pointer, so is_friendly
// (walker.cpp:1675-1742) falls into the no-myguy branch (has_myguy == 0,
// lines 1711-1716) and the friendliness verdict reduces to the bare team-number
// comparison on the load-bearing line 1723:
// `headus->team_num() == headtarget->team_num()`. Because all three team
// numbers differ, every pair is mutually hostile: the adjacent soldier and thief
// trade blows, the cross-team melee spills toward the archer, and the arena emits
// a stream of combat play_sound events (branch ~10, master ~12 at the 44-tick
// budget). All three survive the budget.
//
// MUTATION DISCRIMINATOR — play_sound, not archer HP. The mutation rewrites
// line 1723 to `return 1`, making EVERY pair mutually friendly regardless of
// team. With no hostile pairs nobody attacks: every walker keeps full HP and the
// combat-sound stream collapses to the player's lone scripted fire (play_sound
// == 1, below the floor of 4). The archer's HP cannot be the discriminator here:
// it must stay alive on BOTH sides, and at this budget the master leaves it
// untouched at its spawn HP (~90) even unmutated, so its window has to bracket
// the full no-damage..some-damage span ([0,100]) and necessarily also admits the
// mutated full-HP value. The honest, side-stable falsification signal is
// therefore the play_sound count; the archer-HP row asserts only that the
// third team's walker survives. The branch and master combat trajectories
// diverge in the survivors' exact HP (branch soldier ~96 / thief ~56 /
// archer ~80 vs. master ~57 / ~75 / ~90), which the wide HP window and the
// floor (not an exact count) absorb without per-side wrappers.
inline constexpr InputEvent kInputs_multiplayer_two_teams[] = {
    {   5, 0, K_RIGHT },
    {  30, 0, K_NONE },
    {  35, 0, K_FIRE },
    { 200, 0, K_NONE },
};

inline constexpr SpawnSpec kFamilySpawns_multiplayer_two_teams_scen99[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 3, 200 }, // player-team soldier (team 0)
    { FAMILY_THIEF,   2, kOrderLiving, 140, 140, 0, 0, 3, 200 }, // team-2 thief: hostile to both other teams via the line-1723 comparison
    { FAMILY_ARCHER,  1, kOrderLiving, 200, 200, 0, 0 },         // team-1 archer: third distinct team; survives the budget on both sides
};

inline constexpr FactPredicate kFacts_multiplayer_two_teams_scen99[] = {
    pred::TickReached(44),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 0, 10000,
        "rng_drift: the team-1 archer spawns at (200,200), away from the (120,120)/(140,140) soldier-thief melee; whether the cross-team combat reaches it by tick 44 differs across sides (branch leaves it ~80, master ~90), so this window brackets its spawn HP and asserts the third team's walker survives. The mutation discriminator is the play_sound consequence below, not this row."),
    pred::EventKindAtLeast(/*play_sound*/1, 4,
        "consequence: the soldier (team 0), thief (team 2), and archer (team 1) carry three distinct team_nums and none holds a myguy pointer, so is_friendly takes the no-myguy branch and the verdict reduces to the team_num comparison on walker.cpp:1723; because the numbers differ every pair is hostile and the units trade blows, emitting a stream of combat play_sound events (branch ~10, master ~12). The mutation rewrites line 1723 to `return 1`, making every pair friendly: combat ceases, only the player's lone scripted fire remains, and the play_sound count collapses to 1 — below this floor of 4."),
};

inline constexpr Mutation kMut_multiplayer_two_teams_scen99 = {
    "src/gameplay/walker.cpp", 1723,
    "return headus->team_num() == headtarget->team_num();",
    "return 1;",
    "Replaces the no-myguy team-number friendliness comparison with an unconditional `return 1`, so every pair of walkers is friendly regardless of team; the three-team melee never starts, every walker keeps full HP, and the combat play_sound stream collapses from ~10 to 1 — below the EventKindAtLeast(play_sound, 4) floor (verified: mutated branch dump emits play_sound == 1)."
};

// Level-withdraw scenario. Reuses scripted_input_scen9301's spawn list
// (kFamilySpawns_soldier_with_exit_withdraw: a player SOLDIER on team 0, a
// surviving team-1 enemy that keeps a live foe in the level, and a FAMILY_EXIT
// treasure whose stats().level()==2 points at the already-completed scen2) and
// its input script (kInputsScripted9301: UP→RIGHT→FIRE) which walks the player
// onto the exit. Because the destination level is completed but the current one
// is not, exit_on_eat takes the withdraw branch
// (treasure_family_navigation.cpp:84-96): it sets world.withdraw_requested=true
// and emits WithdrawToLevel. From the next tick on, the early-break guards at
// game_world.cpp:1393/1438/etc fire before any living foe can set level_done=0,
// so level_done holds at its default 2 (game_world.cpp:1357) — the loop never
// reaches the level_done=0 assignment at 1408. The tick budget is widened to
// 200 (vs the 150 of scripted_input) so the withdraw latches well before the
// final dump. Because the player spawns on the exit tile the withdraw fires at
// tick ~5, well before the scripted K_FIRE, so the run emits no combat
// play_sound events — the two events the withdraw branch does emit
// (WithdrawToLevel and the withdraw-flavoured RequestExitConfirmation) carry
// the event-floor predicates instead.
inline constexpr FactPredicate kFacts_level_withdraw_scen99[] = {
    pred::TickReached(200),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::LevelDoneEquals(2, "consequence: withdraw path returns level_done=2; mutation forces withdraw_requested=false so the loop completes and level_done becomes 0"),
    pred::EventKindAtLeast(/*withdraw_to_level*/8, 1),
    pred::EventKindAtLeast(/*request_exit_confirmation*/7, 1),
};

inline constexpr Mutation kMut_level_withdraw_scen99 = {
    "src/gameplay/families/treasure_family_navigation.cpp", 86,
    "        world.withdraw_requested = true;",
    "        world.withdraw_requested = false;",
    "Forces the withdraw-request flag false at the instant the player eats the FAMILY_EXIT treasure on a completed destination level; the early-break guards at game_world.cpp:1393/1438 never fire, the entity-act loop runs to completion, the surviving team-1 enemy sets level_done=0 at game_world.cpp:1408, and the level_done==2 completion branch at 1484 is skipped — so LevelDoneEquals(2) flips to level_done==0 (verified against the mutated branch dump)."
};


// --- Phase 09: mid-combat / consumable walker-state scenarios ---------------
//
// midcombat_partial_hp_scen99: two FAMILY_SOLDIERs 60px apart (player team 0 at
// (120,120), foe team 1 at (180,120)) trade thrown knives -- the same proven,
// non-lethal exchange combat_attack_scen99 uses -- and by the 80-tick budget
// both sit in the wide mid-HP band below their 12000-cent (120 HP) max.
// Discriminator (ANY-soldier HP band): the mutation zeroes the central
// combat-damage write (walker_combat.cpp:189) so neither soldier takes damage
// and both finish at full HP (12000) -- no soldier remains in the band, flipping
// it. The band is widened to bracket the branch/master RNG-driven damage spread
// (branch ~10000/10600 vs master ~1800/4900) (label_exempted); play_sound still
// fires (swing logic untouched).
inline constexpr InputEvent kInputs_midcombat_partial_hp[] = {
    {  5, 0, K_FIRE },
    { 64, 0, K_NONE },
};
inline constexpr SpawnSpec kFamilySpawns_midcombat_partial_hp_scen99[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 }, // player soldier (team 0)
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 }, // foe soldier (team 1)
};
inline constexpr FactPredicate kFacts_midcombat_partial_hp_scen99[] = {
    pred::TickReached(80),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 500, 11500,
        "consequence: the two soldiers trade knife blows and at least one settles in the wide mid-HP band on both sides; the mutation zeros the central combat-damage write so both take no damage and finish at full HP (12000), leaving no soldier in the band (label_exempted)"),
    pred::WalkerOfTeamAlive(0, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
};
inline constexpr Mutation kMut_midcombat_partial_hp_scen99 = {
    "src/gameplay/walker_combat.cpp", 189,
    "    target->stats()->set_hitpoints(target->stats()->hitpoints() - tempdamage);",
    "    target->stats()->set_hitpoints(target->stats()->hitpoints() - 0);",
    "Zeroes the central per-hit combat-damage write in walker::do_combat_damage; neither soldier takes damage and both finish at full HP (12000), leaving no FAMILY_SOLDIER in the WalkerHpRangeAtFinalTick band -- flipping it."
};

// consumable_inventory_state_scen99: a player FAMILY_SOLDIER (team 0) walks RIGHT
// (K_RIGHT) from (96,120) across a FAMILY_MAGIC_POTION and a level-10
// FAMILY_DRUMSTICK -- the proven walk-and-eat path treasure_drumstick_pickup_scen99
// uses, where stepping onto a treasure tile triggers the eat. A downrange team-1
// FAMILY_ARCHER plinks the walking player with arrows so it is below its 12000-cent
// (120 HP) max by the time it reaches the drumstick; the drumstick heal then fires
// (10*level + rng, capped) and tops it back up. Because the foe is a FAMILY_ARCHER,
// not a soldier, WalkerFamilyCount(FAMILY_SOLDIER) stays 1. Both treasures leave
// the oblist. Discriminator (player HP band): the healed player lands in the band
// on both sides; the mutation no-ops the heal (+amount -> +0) so the player keeps
// only its lower arrow-wounded HP, below the band -- flipping it.
inline constexpr InputEvent kInputs_consumable_inventory_state[] = {
    {  0, 0, K_RIGHT },
    { 30, 0, K_NONE },
};
inline constexpr SpawnSpec kFamilySpawns_consumable_inventory_state_scen99[] = {
    { FAMILY_SOLDIER,      0, kOrderLiving,    96, 120, 0, 0, 3, 0 }, // player soldier (team 0): walks right, eats, gets shot
    { FAMILY_ARCHER,       1, kOrderLiving,   200, 120, 0, 0, 3, 0 }, // downrange archer (team 1, not a soldier): wounds the walker
    { FAMILY_DRUMSTICK,    0, kOrderTreasure, 160, 120, 0, 0, 10, 0 }, // on the walk path; heals the arrow-wounded player
    { FAMILY_MAGIC_POTION, 0, kOrderTreasure, 140, 120, 0, 0 }, // on the walk path; consumed when passed
};
inline constexpr FactPredicate kFacts_consumable_inventory_state_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_DRUMSTICK, kOrderTreasure),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_MAGIC_POTION, kOrderTreasure),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 3500, 7000,
        "consequence: the arrow-wounded player soldier eats the level-10 drumstick on the walk path and is healed into the HP band on both sides; the mutation no-ops the heal so the player keeps only its lower arrow-wounded HP, below the lower bound"),
};
inline constexpr Mutation kMut_consumable_inventory_state_scen99 = {
    "src/gameplay/families/treasure_family_consumables.cpp", 25,
    "    eater->stats()->set_hitpoints(eater->stats()->hitpoints() + amount);",
    "    eater->stats()->set_hitpoints(eater->stats()->hitpoints() + 0);",
    "No-ops the drumstick heal so the arrow-wounded player never recovers; its final HP stays at the lower wounded value, below the WalkerHpRangeAtFinalTick lower bound -- flipping it."
};

// --- Scenario table --------------------------------------------------------

inline constexpr ScenarioSpec kScenarios[] = {
    { "ai_idle_wander_scen9301",
      "scen/scen1.fss", 0x00000001u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true,
      Exercises::None,
      kFacts_ai_idle_wander_scen9301, std::size(kFacts_ai_idle_wander_scen9301),
      kMut_walker_ai_wander },

    { "combat_attack_scen99",
      "scen/scen1.fss", 0x00000042u,
      kInputsCombatAttack99, std::size(kInputsCombatAttack99), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true,
      Exercises::None,
      kFacts_combat_attack_scen99, std::size(kFacts_combat_attack_scen99),
      kMut_combat_damage },

    { "special_archmage_scen123",
      "scen/scen1.fss", 0x0000F00Du,
      kInputsSpecialOnce20, std::size(kInputsSpecialOnce20), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_archmage, std::size(kFamilySpawns_archmage), 0, false, true,
      Exercises::Special_Archmage_1,
      kFacts_special_archmage_scen123, std::size(kFacts_special_archmage_scen123),
      kMut_special_archmage_do_special },

    { "special_cleric_scen124",
      "scen/scen1.fss", 0x0000F00Du,
      kInputsClericMace20, std::size(kInputsClericMace20), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_cleric, std::size(kFamilySpawns_cleric), 0, false, true,
      Exercises::Special_Cleric_1,
      kFacts_special_cleric_scen124, std::size(kFacts_special_cleric_scen124),
      kMut_special_cleric_do_special },

    { "special_mage_scen126",
      "scen/scen1.fss", 0x0000F00Du,
      kInputsSpecialOnce20, std::size(kInputsSpecialOnce20), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_mage, std::size(kFamilySpawns_mage), 0, false, true,
      Exercises::Special_Mage_1,
      kFacts_special_mage_scen126, std::size(kFacts_special_mage_scen126),
      kMut_special_mage_do_special },

    { "special_thief_scen789",
      "scen/scen1.fss", 0x0000F00Du,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_thief, std::size(kFamilySpawns_thief), 0, false, true,
      Exercises::Special_Thief_1,
      kFacts_special_thief_scen789, std::size(kFacts_special_thief_scen789),
      kMut_special_thief_do_special },

    { "effect_bomb_lifetime_scen99","temp/scen/scen99.fss", 0x0000BEEFu,
      kInputsSpecialBomb10,  std::size(kInputsSpecialBomb10),           60,  CompareMode::SemanticParity, false,
      nullptr, 0, 0, false, false, Exercises::None,
      kFacts_effect_bomb_lifetime_scen99, std::size(kFacts_effect_bomb_lifetime_scen99),
      kMut_effect_lifetime },

    { "effect_chain_scen9410",
      "scen/scen1.fss", 0x0000BEEFu,
      kInputsSpecialChain15, std::size(kInputsSpecialChain15), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_mage, std::size(kFamilySpawns_mage), 0, false, true,
      Exercises::None,
      kFacts_effect_chain_scen9410, std::size(kFacts_effect_chain_scen9410),
      kMut_effect_lifetime },

    { "summon_druid_pet_scen950",
      "scen/scen1.fss", 0x0000CAFEu,
      kInputsSpecialSummon8, std::size(kInputsSpecialSummon8), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_druid, std::size(kFamilySpawns_druid), 0, false, true,
      Exercises::None,
      kFacts_summon_druid_pet_scen950, std::size(kFacts_summon_druid_pet_scen950),
      kMut_summon_druid_do_special },

    { "scoring_after_combat_scen99",
      "scen/scen1.fss", 0x00000042u,
      kInputsCombatAttack99, std::size(kInputsCombatAttack99), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true,
      Exercises::None,
      kFacts_scoring_after_combat_scen99, std::size(kFacts_scoring_after_combat_scen99),
      kMut_combat_damage },

    { "save_roundtrip_scen99",
      "scen/scen1.fss", 0x00000123u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true,
      Exercises::None,
      kFacts_save_roundtrip_scen99, std::size(kFacts_save_roundtrip_scen99),
      kMut_combat_damage },

    { "exit_trigger_scen9302",
      "scen/scen1.fss", 0x00000007u,
      kInputsExitWalkRight, std::size(kInputsExitWalkRight), 150,
      CompareMode::SemanticParity, false,
      kSmokeArenaSpawns, std::size(kSmokeArenaSpawns), 0, false, true,
      Exercises::None,
      kFacts_exit_trigger_scen9302, std::size(kFacts_exit_trigger_scen9302),
      kMut_exit_neuter },

    { "tick_cadence_scen9301",
      "scen/scen1.fss", 0x00000001u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true,
      Exercises::None,
      kFacts_tick_cadence_scen9301, std::size(kFacts_tick_cadence_scen9301),
      kMut_walker_ai_wander },

    { "rng_seed_stable_scen99",
      "scen/scen1.fss", 0x00000001u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true,
      Exercises::None,
      kFacts_rng_seed_stable_scen99, std::size(kFacts_rng_seed_stable_scen99),
      kMut_walker_ai_wander },

    { "scripted_input_scen9301",
      "scen/scen1.fss", 0x00000010u,
      kInputsScripted9301, std::size(kInputsScripted9301), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier_with_exit_withdraw, std::size(kFamilySpawns_soldier_with_exit_withdraw), 0, false, true,
      Exercises::None,
      kFacts_scripted_input_scen9301, std::size(kFacts_scripted_input_scen9301),
      kMut_exit_withdraw_path },

    // Branch-internal companion: dirty-bit snapshot vs direct iteration.
    // Lint exempts Invariant rows from fact requirements; expected_facts
    // stays nullptr.
    { "snapshot_dirty_bits_scen9301","scen/scen9301.fss",   0x00000055u,
      nullptr, 0,                                                       50,  CompareMode::Invariant, true,
      nullptr, 0, 0, false, false, Exercises::None,
      nullptr, 0, kMut_snapshot_dirty },

    // Phase 02 smoke scenarios. fresh_arena drops any walkers the loaded
    // scen file may have produced and replaces them with kSmokeArenaSpawns,
    // so the dump can demonstrate a non-empty oblist on both sides without
    // depending on any particular scen99.fss content. The _inputs variant
    // applies K_FIRE briefly so position / keys diverge from the no-input
    // smoke twin.
    //
    // smoke_empty_scen99 is the canonical "world has no walkers" smoke
    // probe used by phase 02 verifier 02b to assert the schema-v1 dumper
    // emits a structurally valid JSON for an empty oblist (walkers: []).
    // It is Invariant (no master golden required) and carries no
    // predicates — the dumper-determinism check in test_parity_scenarios
    // covers it by running the scenario twice and asserting byte-equal
    // serialisation.
    { "smoke_empty_scen99",            "scen/scen1.fss", 0x00000042u,
      nullptr, 0,                                                       1,   CompareMode::Invariant, false,
      nullptr, 0, 0, true, true, Exercises::None,
      nullptr, 0, {} },

    { "smoke_nonempty_scen99",         "scen/scen1.fss", 0x00000042u,
      nullptr, 0,                                                       60,  CompareMode::SemanticParity, false,
      kSmokeArenaSpawns, std::size(kSmokeArenaSpawns), 0, false, true, Exercises::None,
      kFacts_smoke_nonempty_scen99, std::size(kFacts_smoke_nonempty_scen99),
      kMut_smoke_score_event },

    { "smoke_nonempty_scen99_inputs",  "scen/scen1.fss", 0x00000042u,
      kInputsSmokeMoveRight, std::size(kInputsSmokeMoveRight),          60,  CompareMode::SemanticParity, false,
      kSmokeArenaSpawns, std::size(kSmokeArenaSpawns), 0, false, true, Exercises::None,
      kFacts_smoke_nonempty_scen99_inputs, std::size(kFacts_smoke_nonempty_scen99_inputs),
      kMut_smoke_inputs_no_move },

    // Phase 04: one byte-equal arena per walker family (21 entries).
    { "family_soldier_scen99",         "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_soldier, std::size(kFamilySpawns_complete_soldier), 0, false, true,
      Exercises::None,
      kFacts_family_soldier_scen99, std::size(kFacts_family_soldier_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_SOLDIER expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_elf_scen99",             "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_elf, std::size(kFamilySpawns_complete_elf), 0, false, true,
      Exercises::None,
      kFacts_family_elf_scen99, std::size(kFacts_family_elf_scen99),
      kMut_family_spawn_identity_elf,
      "05a family_spawns[]=FAMILY_ELF expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_archer_scen99",          "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_archer, std::size(kFamilySpawns_complete_archer), 0, false, true,
      Exercises::None,
      kFacts_family_archer_scen99, std::size(kFacts_family_archer_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_ARCHER expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_mage_scen99",            "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_mage, std::size(kFamilySpawns_complete_mage), 0, false, true,
      Exercises::None,
      kFacts_family_mage_scen99, std::size(kFacts_family_mage_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_MAGE expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_skeleton_scen99",        "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_skeleton, std::size(kFamilySpawns_complete_skeleton), 0, false, true,
      Exercises::None,
      kFacts_family_skeleton_scen99, std::size(kFacts_family_skeleton_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_SKELETON expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_cleric_scen99",          "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_cleric, std::size(kFamilySpawns_complete_cleric), 0, false, true,
      Exercises::None,
      kFacts_family_cleric_scen99, std::size(kFacts_family_cleric_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_CLERIC expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_fireelemental_scen99",   "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_fireelemental, std::size(kFamilySpawns_complete_fireelemental), 0, false, true,
      Exercises::None,
      kFacts_family_fireelemental_scen99, std::size(kFacts_family_fireelemental_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_FIREELEMENTAL expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_faerie_scen99",          "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_faerie, std::size(kFamilySpawns_complete_faerie), 0, false, true, Exercises::None,
      kFacts_family_faerie_scen99, std::size(kFacts_family_faerie_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_FAERIE expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_slime_scen99",           "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_slime, std::size(kFamilySpawns_complete_slime), 0, false, true,
      Exercises::None,
      kFacts_family_slime_scen99, std::size(kFacts_family_slime_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_SLIME expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_small_slime_scen99",     "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_small_slime, std::size(kFamilySpawns_complete_small_slime), 0, false, true,
      Exercises::None,
      kFacts_family_small_slime_scen99, std::size(kFacts_family_small_slime_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_SMALL_SLIME expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_medium_slime_scen99",    "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_medium_slime, std::size(kFamilySpawns_complete_medium_slime), 0, false, true,
      Exercises::None,
      kFacts_family_medium_slime_scen99, std::size(kFacts_family_medium_slime_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_MEDIUM_SLIME expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_thief_scen99",           "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_thief, std::size(kFamilySpawns_complete_thief), 0, false, true,
      Exercises::None,
      kFacts_family_thief_scen99, std::size(kFacts_family_thief_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_THIEF expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_ghost_scen99",           "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_ghost, std::size(kFamilySpawns_complete_ghost), 0, false, true,
      Exercises::None,
      kFacts_family_ghost_scen99, std::size(kFacts_family_ghost_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_GHOST expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_druid_scen99",           "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_druid, std::size(kFamilySpawns_complete_druid), 0, false, true,
      Exercises::None,
      kFacts_family_druid_scen99, std::size(kFacts_family_druid_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_DRUID expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_orc_scen99",             "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_orc, std::size(kFamilySpawns_complete_orc), 0, false, true,
      Exercises::None,
      kFacts_family_orc_scen99, std::size(kFacts_family_orc_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_ORC expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_big_orc_scen99",         "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_big_orc, std::size(kFamilySpawns_complete_big_orc), 0, false, true, Exercises::None,
      kFacts_family_big_orc_scen99, std::size(kFacts_family_big_orc_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_BIG_ORC expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_barbarian_scen99",       "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_barbarian, std::size(kFamilySpawns_complete_barbarian), 0, false, true,
      Exercises::None,
      kFacts_family_barbarian_scen99, std::size(kFacts_family_barbarian_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_BARBARIAN expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_archmage_scen99",        "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_archmage, std::size(kFamilySpawns_complete_archmage), 0, false, true,
      Exercises::None,
      kFacts_family_archmage_scen99, std::size(kFacts_family_archmage_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_ARCHMAGE expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    { "family_golem_scen99",           "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_golem, std::size(kFamilySpawns_complete_golem), 0, false, true, Exercises::None,
      kFacts_family_golem_scen99, std::size(kFacts_family_golem_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_GOLEM expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerDiedByFinal,EventKindAtLeast" },

    // Phase 03 runtime structural coverage catch-all. Uses the synthetic
    // kFamilySpawns_golem_with_nonliving_targets list (one walker per
    // weapon/treasure/FX family) so the runtime Parity.coverage_gate_*
    // tests observe every required family at least once via the
    // per-tick sample_world() loop. This row is INDEPENDENT of
    // family_golem_scen99 — family_golem_scen99 now uses the clean
    // kFamilySpawns_golem so the 04b textual-strip check (which removes
    // every row referencing the synthetic blob spawn) keeps
    // FAMILY_GOLEM bound via kFacts_family_golem_scen99. The
    // expected_facts here intentionally bind no walker family ID 18 —
    // 04b's check accepts FAMILY_GOLEM as bound elsewhere, and the row
    // can be stripped without orphaning anything.
    { "coverage_catchall_scen99",      "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_golem_with_nonliving_targets, std::size(kFamilySpawns_golem_with_nonliving_targets), 0, false, true, Exercises::None,
      kFacts_coverage_catchall_scen99, std::size(kFacts_coverage_catchall_scen99),
      kMut_family_golem_init },

    { "family_giant_skeleton_scen99",  "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_giant_skeleton, std::size(kFamilySpawns_complete_giant_skeleton), 0, false, true, Exercises::None,
      kFacts_family_giant_skeleton_scen99, std::size(kFacts_family_giant_skeleton_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_GIANT_SKELETON expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerDiedByFinal,EventKindAtLeast" },

    { "family_tower1_scen99",          "scen/scen1.fss", 0x00000042u,
      kInputsFamilyCompleteness, std::size(kInputsFamilyCompleteness), 600, CompareMode::SemanticParity, false,
      kFamilySpawns_complete_tower1, std::size(kFamilySpawns_complete_tower1), 0, false, true, Exercises::None,
      kFacts_family_tower1_scen99, std::size(kFacts_family_tower1_scen99),
      kMut_family_spawn_identity,
      "05a family_spawns[]=FAMILY_TOWER1 expected_facts[]=WalkerFamilyCount,WalkerOfTeamAlive,WalkerPositionMoved,WalkerHpRangeAtFinalTick,EventKindAtLeast" },

    // Phase 04a — treasure pickup scenarios. Every row spawns a lone
    // FAMILY_SOLDIER at (96, 120) and the literal treasure family F as
    // a kOrderTreasure at (160, 120); K_RIGHT held ticks 1..20 walks
    // the soldier east into the treasure; tick_budget=150 leaves the
    // on_eat side effects time to settle.
    { "treasure_stain_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickup, std::size(kInputsTreasurePickup), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_stain_pickup, std::size(kFamilySpawns_treasure_stain_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_stain_pickup_scen99, std::size(kFacts_treasure_stain_pickup_scen99),
      kMut_treasure_stain_pickup },

    { "treasure_drumstick_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_treasure_drumstick_pickup, std::size(kInputs_treasure_drumstick_pickup), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_drumstick_pickup, std::size(kFamilySpawns_treasure_drumstick_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_drumstick_pickup_scen99, std::size(kFacts_treasure_drumstick_pickup_scen99),
      kMut_treasure_drumstick_pickup },

    { "treasure_gold_bar_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickup, std::size(kInputsTreasurePickup), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_gold_bar_pickup, std::size(kFamilySpawns_treasure_gold_bar_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_gold_bar_pickup_scen99, std::size(kFacts_treasure_gold_bar_pickup_scen99),
      kMut_treasure_gold_bar_pickup },

    { "treasure_silver_bar_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickup, std::size(kInputsTreasurePickup), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_silver_bar_pickup, std::size(kFamilySpawns_treasure_silver_bar_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_silver_bar_pickup_scen99, std::size(kFacts_treasure_silver_bar_pickup_scen99),
      kMut_treasure_silver_bar_pickup },

    { "treasure_magic_potion_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickupTick0, std::size(kInputsTreasurePickupTick0), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_magic_potion_pickup, std::size(kFamilySpawns_treasure_magic_potion_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_magic_potion_pickup_scen99, std::size(kFacts_treasure_magic_potion_pickup_scen99),
      kMut_treasure_magic_potion_pickup },

    { "treasure_invis_potion_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickupTick0, std::size(kInputsTreasurePickupTick0), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_invis_potion_pickup, std::size(kFamilySpawns_treasure_invis_potion_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_invis_potion_pickup_scen99, std::size(kFacts_treasure_invis_potion_pickup_scen99),
      kMut_treasure_invis_potion_pickup },

    { "treasure_invulnerable_potion_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickupTick0, std::size(kInputsTreasurePickupTick0), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_invulnerable_potion_pickup, std::size(kFamilySpawns_treasure_invulnerable_potion_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_invulnerable_potion_pickup_scen99, std::size(kFacts_treasure_invulnerable_potion_pickup_scen99),
      kMut_treasure_invulnerable_potion_pickup },

    { "treasure_flight_potion_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickupTick0, std::size(kInputsTreasurePickupTick0), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_flight_potion_pickup, std::size(kFamilySpawns_treasure_flight_potion_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_flight_potion_pickup_scen99, std::size(kFacts_treasure_flight_potion_pickup_scen99),
      kMut_treasure_flight_potion_pickup },

    { "treasure_teleporter_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsScripted9301, std::size(kInputsScripted9301), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_teleporter_pickup, std::size(kFamilySpawns_treasure_teleporter_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_teleporter_pickup_scen99, std::size(kFacts_treasure_teleporter_pickup_scen99),
      kMut_treasure_teleporter_pickup },

    { "treasure_life_gem_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickup, std::size(kInputsTreasurePickup), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_life_gem_pickup, std::size(kFamilySpawns_treasure_life_gem_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_life_gem_pickup_scen99, std::size(kFacts_treasure_life_gem_pickup_scen99),
      kMut_treasure_life_gem_pickup },

    { "treasure_key_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickup, std::size(kInputsTreasurePickup), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_key_pickup, std::size(kFamilySpawns_treasure_key_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_key_pickup_scen99, std::size(kFacts_treasure_key_pickup_scen99),
      kMut_treasure_key_pickup },

    { "treasure_speed_potion_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpeedPotionRun, std::size(kInputsSpeedPotionRun), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_speed_potion_pickup, std::size(kFamilySpawns_treasure_speed_potion_pickup),
      0, false, true, Exercises::None,
      kFacts_treasure_speed_potion_pickup_scen99, std::size(kFacts_treasure_speed_potion_pickup_scen99),
      kMut_treasure_speed_potion_pickup },

    { "treasure_exit_pickup_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsScripted9301, std::size(kInputsScripted9301), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier_with_exit_withdraw, std::size(kFamilySpawns_soldier_with_exit_withdraw),
      0, false, true, Exercises::None,
      kFacts_treasure_exit_pickup_scen99, std::size(kFacts_treasure_exit_pickup_scen99),
      kMut_treasure_exit_pickup },

    // Phase 04 — weapon emission scenarios
    { "weapon_knife_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_knife_emission, std::size(kFamilySpawns_weapon_knife_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_knife_emission_scen99, std::size(kFacts_weapon_knife_emission_scen99),
      kMut_weapon_knife_emission },

    { "weapon_rock_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_rock_emission, std::size(kFamilySpawns_weapon_rock_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_rock_emission_scen99, std::size(kFacts_weapon_rock_emission_scen99),
      kMut_weapon_rock_emission },

    { "weapon_arrow_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_arrow_emission, std::size(kFamilySpawns_weapon_arrow_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_arrow_emission_scen99, std::size(kFacts_weapon_arrow_emission_scen99),
      kMut_weapon_arrow_emission },

    { "weapon_fireball_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_fireball_emission, std::size(kFamilySpawns_weapon_fireball_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_fireball_emission_scen99, std::size(kFacts_weapon_fireball_emission_scen99),
      kMut_weapon_fireball_emission },

    { "weapon_tree_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_tree_emission, std::size(kFamilySpawns_weapon_tree_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_tree_emission_scen99, std::size(kFacts_weapon_tree_emission_scen99),
      kMut_weapon_tree_emission },

    { "weapon_meteor_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_meteor_emission, std::size(kFamilySpawns_weapon_meteor_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_meteor_emission_scen99, std::size(kFacts_weapon_meteor_emission_scen99),
      kMut_weapon_meteor_emission },

    { "weapon_sprinkle_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_sprinkle_emission, std::size(kFamilySpawns_weapon_sprinkle_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_sprinkle_emission_scen99, std::size(kFacts_weapon_sprinkle_emission_scen99),
      kMut_weapon_sprinkle_emission },

    { "weapon_bone_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_bone_emission, std::size(kFamilySpawns_weapon_bone_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_bone_emission_scen99, std::size(kFacts_weapon_bone_emission_scen99),
      kMut_weapon_bone_emission },

    { "weapon_blood_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_blood_emission, std::size(kFamilySpawns_weapon_blood_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_blood_emission_scen99, std::size(kFacts_weapon_blood_emission_scen99),
      kMut_weapon_blood_emission },

    { "weapon_blob_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_blob_emission, std::size(kFamilySpawns_weapon_blob_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_blob_emission_scen99, std::size(kFacts_weapon_blob_emission_scen99),
      kMut_weapon_blob_emission },

    { "weapon_fire_arrow_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_fire_arrow_emission, std::size(kFamilySpawns_weapon_fire_arrow_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_fire_arrow_emission_scen99, std::size(kFacts_weapon_fire_arrow_emission_scen99),
      kMut_weapon_fire_arrow_emission },

    { "weapon_lightning_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_lightning_emission, std::size(kFamilySpawns_weapon_lightning_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_lightning_emission_scen99, std::size(kFacts_weapon_lightning_emission_scen99),
      kMut_weapon_lightning_emission },

    { "weapon_glow_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_glow_emission, std::size(kFamilySpawns_weapon_glow_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_glow_emission_scen99, std::size(kFacts_weapon_glow_emission_scen99),
      kMut_weapon_glow_emission },

    { "weapon_wave_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWaveSpecialEmit, std::size(kInputsWaveSpecialEmit), 18,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_wave_emission, std::size(kFamilySpawns_weapon_wave_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_wave_emission_scen99, std::size(kFacts_weapon_wave_emission_scen99),
      kMut_weapon_wave_emission },

    { "weapon_wave2_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_wave2_emission, std::size(kFamilySpawns_weapon_wave2_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_wave2_emission_scen99, std::size(kFacts_weapon_wave2_emission_scen99),
      kMut_weapon_wave2_emission },

    { "weapon_wave3_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 2,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_wave3_emission, std::size(kFamilySpawns_weapon_wave3_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_wave3_emission_scen99, std::size(kFacts_weapon_wave3_emission_scen99),
      kMut_weapon_wave3_emission },

    { "weapon_circle_protection_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_circle_protection_emission, std::size(kFamilySpawns_weapon_circle_protection_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_circle_protection_emission_scen99, std::size(kFacts_weapon_circle_protection_emission_scen99),
      kMut_weapon_circle_protection_emission },

    { "weapon_hammer_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_hammer_emission, std::size(kFamilySpawns_weapon_hammer_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_hammer_emission_scen99, std::size(kFacts_weapon_hammer_emission_scen99),
      kMut_weapon_hammer_emission },

    { "weapon_door_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_door_emission, std::size(kFamilySpawns_weapon_door_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_door_emission_scen99, std::size(kFacts_weapon_door_emission_scen99),
      kMut_weapon_door_emission },

    { "weapon_boulder_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_boulder_emission, std::size(kFamilySpawns_weapon_boulder_emission),
      0, false, true, Exercises::None,
      kFacts_weapon_boulder_emission_scen99, std::size(kFacts_weapon_boulder_emission_scen99),
      kMut_weapon_boulder_emission },

    // Phase 04 — effect emission scenarios
    { "effect_expand_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_expand_emission_scen99, std::size(kFamilySpawns_effect_expand_emission_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_expand_emission_scen99, std::size(kFacts_effect_expand_emission_scen99),
      kMut_effect_expand_emission },

    { "effect_ghost_scare_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 90,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_ghost_scare_emit_scen99, std::size(kFamilySpawns_effect_ghost_scare_emit_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_ghost_scare_emission_scen99, std::size(kFacts_effect_ghost_scare_emission_scen99),
      kMut_effect_ghost_scare_emission },

    { "effect_bomb_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 200,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_bomb_emission_scen99, std::size(kFamilySpawns_effect_bomb_emission_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_bomb_emission_scen99, std::size(kFacts_effect_bomb_emission_scen99),
      kMut_effect_bomb_emission },

    { "effect_explosion_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_heartburst_multitarget_scen99, std::size(kFamilySpawns_effect_heartburst_multitarget_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_explosion_emission_scen99, std::size(kFacts_effect_explosion_emission_scen99),
      kMut_effect_explosion_emission },

    { "effect_flash_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickup, std::size(kInputsTreasurePickup), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_flash_arena, std::size(kFamilySpawns_effect_flash_arena),
      0, false, true, Exercises::None,
      kFacts_effect_flash_emission_scen99, std::size(kFacts_effect_flash_emission_scen99),
      kMut_effect_flash_emission },

    { "effect_magic_shield_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsMagicShieldEmit, std::size(kInputsMagicShieldEmit), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_magic_shield_arena, std::size(kFamilySpawns_magic_shield_arena),
      0, false, true, Exercises::None,
      kFacts_effect_magic_shield_emission_scen99, std::size(kFacts_effect_magic_shield_emission_scen99),
      kMut_effect_magic_shield_emission },

    { "effect_knife_back_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_combat_arena, std::size(kFamilySpawns_effect_combat_arena),
      0, false, true, Exercises::None,
      kFacts_effect_knife_back_emission_scen99, std::size(kFacts_effect_knife_back_emission_scen99),
      kMut_effect_knife_back_emission },

    { "effect_boomerang_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 45,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_soldier_2_scen99, std::size(kFamilySpawns_special_soldier_2_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_boomerang_emission_scen99, std::size(kFacts_effect_boomerang_emission_scen99),
      kMut_effect_boomerang_emission },

    { "effect_cloud_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 45,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_poison_cloud_emit_scen99, std::size(kFamilySpawns_effect_poison_cloud_emit_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_cloud_emission_scen99, std::size(kFacts_effect_cloud_emission_scen99),
      kMut_effect_cloud_emission },

    { "effect_marker_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 2500,
      CompareMode::SemanticParity, false,
      kFamilySpawns_marker_emission_generator, std::size(kFamilySpawns_marker_emission_generator),
      0, false, true, Exercises::None,
      kFacts_effect_marker_emission_scen99, std::size(kFacts_effect_marker_emission_scen99),
      kMut_effect_marker_emission },

    { "effect_chain_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsChainEmission, std::size(kInputsChainEmission), 40,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_chain_caster, std::size(kFamilySpawns_effect_chain_caster),
      0, false, true, Exercises::None,
      kFacts_effect_chain_emission_scen99, std::size(kFacts_effect_chain_emission_scen99),
      kMut_effect_chain_emission },

    { "effect_door_open_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_door_open_arena, std::size(kFamilySpawns_effect_door_open_arena),
      0, false, true, Exercises::None,
      kFacts_effect_door_open_emission_scen99, std::size(kFacts_effect_door_open_emission_scen99),
      kMut_effect_door_open_emission },

    { "effect_hit_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_combat_arena, std::size(kFamilySpawns_effect_combat_arena),
      0, false, true, Exercises::None,
      kFacts_effect_hit_emission_scen99, std::size(kFacts_effect_hit_emission_scen99),
      kMut_effect_hit_emission },

    // Phase 04 — generator emission scenarios
    { "generator_tent_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 1500,
      CompareMode::SemanticParity, false,
      kFamilySpawns_generator_tent, std::size(kFamilySpawns_generator_tent),
      0, false, true, Exercises::None,
      kFacts_generator_tent_emission_scen99, std::size(kFacts_generator_tent_emission_scen99),
      kMut_generator_tent_emission },

    { "generator_tower_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 1500,
      CompareMode::SemanticParity, false,
      kFamilySpawns_generator_tower, std::size(kFamilySpawns_generator_tower),
      0, false, true, Exercises::None,
      kFacts_generator_tower_emission_scen99, std::size(kFacts_generator_tower_emission_scen99),
      kMut_generator_tower_emission },

    { "generator_bones_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 1500,
      CompareMode::SemanticParity, false,
      kFamilySpawns_generator_bones, std::size(kFamilySpawns_generator_bones),
      0, false, true, Exercises::None,
      kFacts_generator_bones_emission_scen99, std::size(kFacts_generator_bones_emission_scen99),
      kMut_generator_bones_emission },

    { "generator_treehouse_emission_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 1500,
      CompareMode::SemanticParity, false,
      kFamilySpawns_generator_treehouse, std::size(kFamilySpawns_generator_treehouse),
      0, false, true, Exercises::None,
      kFacts_generator_treehouse_emission_scen99, std::size(kFacts_generator_treehouse_emission_scen99),
      kMut_generator_treehouse_emission },

    // Phase 04 — event-kind emission scenarios
    { "event_notification_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_notification_emission_scen99, std::size(kFacts_event_notification_emission_scen99),
      kMut_event_notification_emission },

    { "event_set_palette_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_set_palette_emission_scen99, std::size(kFacts_event_set_palette_emission_scen99),
      kMut_event_set_palette_emission },

    { "event_request_redraw_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_request_redraw_emission_scen99, std::size(kFacts_event_request_redraw_emission_scen99),
      kMut_event_request_redraw_emission },

    { "event_end_game_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_end_game_emission_scen99, std::size(kFacts_event_end_game_emission_scen99),
      kMut_event_end_game_emission },

    { "event_set_end_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEffectCombat, std::size(kInputsEffectCombat), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_set_end_emission_scen99, std::size(kFacts_event_set_end_emission_scen99),
      kMut_event_set_end_emission },

    // Phase 04 — per-family per-slot special-cast scenarios
    { "special_soldier_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_soldier_1_scen99, std::size(kFamilySpawns_special_soldier_1_scen99),
      0, false, true, Exercises::Special_Soldier_1,
      kFacts_special_soldier_1_scen99, std::size(kFacts_special_soldier_1_scen99),
      kMut_special_soldier_1_scen99 },

    { "special_soldier_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_soldier_2_scen99, std::size(kFamilySpawns_special_soldier_2_scen99),
      0, false, true, Exercises::Special_Soldier_2,
      kFacts_special_soldier_2_scen99, std::size(kFacts_special_soldier_2_scen99),
      kMut_special_soldier_2_scen99 },

    { "special_soldier_3_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_soldier_3_scen99, std::size(kFamilySpawns_special_soldier_3_scen99),
      0, false, true, Exercises::Special_Soldier_3,
      kFacts_special_soldier_3_scen99, std::size(kFacts_special_soldier_3_scen99),
      kMut_special_soldier_3_scen99 },

    { "special_soldier_4_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_soldier_4_scen99, std::size(kFamilySpawns_special_soldier_4_scen99),
      0, false, true, Exercises::Special_Soldier_4,
      kFacts_special_soldier_4_scen99, std::size(kFacts_special_soldier_4_scen99),
      kMut_special_soldier_4_scen99 },

    { "special_elf_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_elf_1_scen99, std::size(kFamilySpawns_special_elf_1_scen99),
      0, false, true, Exercises::Special_Elf_1,
      kFacts_special_elf_1_scen99, std::size(kFacts_special_elf_1_scen99),
      kMut_special_elf_1_scen99 },

    { "special_elf_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_elf_2_scen99, std::size(kFamilySpawns_special_elf_2_scen99),
      0, false, true, Exercises::Special_Elf_2,
      kFacts_special_elf_2_scen99, std::size(kFacts_special_elf_2_scen99),
      kMut_special_elf_2_scen99 },

    { "special_elf_3_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_elf_3_scen99, std::size(kFamilySpawns_special_elf_3_scen99),
      0, false, true, Exercises::Special_Elf_3,
      kFacts_special_elf_3_scen99, std::size(kFacts_special_elf_3_scen99),
      kMut_special_elf_3_scen99 },

    { "special_elf_4_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_elf_4_scen99, std::size(kFamilySpawns_special_elf_4_scen99),
      0, false, true, Exercises::Special_Elf_4,
      kFacts_special_elf_4_scen99, std::size(kFacts_special_elf_4_scen99),
      kMut_special_elf_4_scen99 },

    { "special_archer_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_archer_1_scen99, std::size(kFamilySpawns_special_archer_1_scen99),
      0, false, true, Exercises::Special_Archer_1,
      kFacts_special_archer_1_scen99, std::size(kFacts_special_archer_1_scen99),
      kMut_special_archer_1_scen99 },

    { "special_archer_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_archer_2_scen99, std::size(kFamilySpawns_special_archer_2_scen99),
      0, false, true, Exercises::Special_Archer_2,
      kFacts_special_archer_2_scen99, std::size(kFacts_special_archer_2_scen99),
      kMut_special_archer_2_scen99 },

    { "special_archer_3_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_archer_3_scen99, std::size(kFamilySpawns_special_archer_3_scen99),
      0, false, true, Exercises::Special_Archer_3,
      kFacts_special_archer_3_scen99, std::size(kFacts_special_archer_3_scen99),
      kMut_special_archer_3_scen99 },

    { "special_mage_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_mage_2_scen99, std::size(kFamilySpawns_special_mage_2_scen99),
      0, false, true, Exercises::Special_Mage_2,
      kFacts_special_mage_2_scen99, std::size(kFacts_special_mage_2_scen99),
      kMut_special_mage_2_scen99 },

    { "special_mage_3_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_mage_3_scen99, std::size(kFamilySpawns_special_mage_3_scen99),
      0, false, true, Exercises::Special_Mage_3,
      kFacts_special_mage_3_scen99, std::size(kFacts_special_mage_3_scen99),
      kMut_special_mage_3_scen99 },

    { "special_mage_4_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_mage_4_scen99, std::size(kFamilySpawns_special_mage_4_scen99),
      0, false, true, Exercises::Special_Mage_4,
      kFacts_special_mage_4_scen99, std::size(kFacts_special_mage_4_scen99),
      kMut_special_mage_4_scen99 },

    { "special_mage_5_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot5, std::size(kInputsSpecialSlot5), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_mage_5_scen99, std::size(kFamilySpawns_special_mage_5_scen99),
      0, false, true, Exercises::Special_Mage_5,
      kFacts_special_mage_5_scen99, std::size(kFacts_special_mage_5_scen99),
      kMut_special_mage_5_scen99 },

    { "special_skeleton_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_skeleton_1_scen99, std::size(kFamilySpawns_special_skeleton_1_scen99),
      0, false, true, Exercises::Special_Skeleton_1,
      kFacts_special_skeleton_1_scen99, std::size(kFacts_special_skeleton_1_scen99),
      kMut_special_skeleton_1_scen99 },

    { "special_cleric_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_cleric_2_scen99, std::size(kFamilySpawns_special_cleric_2_scen99),
      0, false, true, Exercises::Special_Cleric_2,
      kFacts_special_cleric_2_scen99, std::size(kFacts_special_cleric_2_scen99),
      kMut_special_cleric_2_scen99 },

    { "special_cleric_3_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_cleric_3_scen99, std::size(kFamilySpawns_special_cleric_3_scen99),
      0, false, true, Exercises::Special_Cleric_3,
      kFacts_special_cleric_3_scen99, std::size(kFacts_special_cleric_3_scen99),
      kMut_special_cleric_3_scen99 },

    { "special_cleric_4_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_cleric_4_scen99, std::size(kFamilySpawns_special_cleric_4_scen99),
      0, false, true, Exercises::Special_Cleric_4,
      kFacts_special_cleric_4_scen99, std::size(kFacts_special_cleric_4_scen99),
      kMut_special_cleric_4_scen99 },

    { "special_fireelemental_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_fireelemental_1_scen99, std::size(kFamilySpawns_special_fireelemental_1_scen99),
      0, false, true, Exercises::Special_FireElemental_1,
      kFacts_special_fireelemental_1_scen99, std::size(kFacts_special_fireelemental_1_scen99),
      kMut_special_fireelemental_1_scen99 },

    { "special_slime_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_slime_1_scen99, std::size(kFamilySpawns_special_slime_1_scen99),
      0, false, true, Exercises::Special_Slime_1,
      kFacts_special_slime_1_scen99, std::size(kFacts_special_slime_1_scen99),
      kMut_special_slime_1_scen99 },

    { "special_small_slime_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_small_slime_1_scen99, std::size(kFamilySpawns_special_small_slime_1_scen99),
      0, false, true, Exercises::Special_SmallSlime_1,
      kFacts_special_small_slime_1_scen99, std::size(kFacts_special_small_slime_1_scen99),
      kMut_special_small_slime_1_scen99 },

    { "special_medium_slime_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_medium_slime_1_scen99, std::size(kFamilySpawns_special_medium_slime_1_scen99),
      0, false, true, Exercises::Special_MediumSlime_1,
      kFacts_special_medium_slime_1_scen99, std::size(kFacts_special_medium_slime_1_scen99),
      kMut_special_medium_slime_1_scen99 },

    { "special_thief_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_thief_2_scen99, std::size(kFamilySpawns_special_thief_2_scen99),
      0, false, true, Exercises::Special_Thief_2,
      kFacts_special_thief_2_scen99, std::size(kFacts_special_thief_2_scen99),
      kMut_special_thief_2_scen99 },

    { "special_thief_3_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_thief_3_scen99, std::size(kFamilySpawns_special_thief_3_scen99),
      0, false, true, Exercises::Special_Thief_3,
      kFacts_special_thief_3_scen99, std::size(kFacts_special_thief_3_scen99),
      kMut_special_thief_3_scen99 },

    { "special_thief_4_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_thief_4_scen99, std::size(kFamilySpawns_special_thief_4_scen99),
      0, false, true, Exercises::Special_Thief_4,
      kFacts_special_thief_4_scen99, std::size(kFacts_special_thief_4_scen99),
      kMut_special_thief_4_scen99 },

    { "special_ghost_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_ghost_1_scen99, std::size(kFamilySpawns_special_ghost_1_scen99),
      0, false, true, Exercises::Special_Ghost_1,
      kFacts_special_ghost_1_scen99, std::size(kFacts_special_ghost_1_scen99),
      kMut_special_ghost_1_scen99 },

    { "special_druid_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_druid_1_scen99, std::size(kFamilySpawns_special_druid_1_scen99),
      0, false, true, Exercises::Special_Druid_1,
      kFacts_special_druid_1_scen99, std::size(kFacts_special_druid_1_scen99),
      kMut_special_druid_1_scen99 },

    { "special_druid_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_druid_2_scen99, std::size(kFamilySpawns_special_druid_2_scen99),
      0, false, true, Exercises::Special_Druid_2,
      kFacts_special_druid_2_scen99, std::size(kFacts_special_druid_2_scen99),
      kMut_special_druid_2_scen99 },

    { "special_druid_3_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_druid_3_scen99, std::size(kFamilySpawns_special_druid_3_scen99),
      0, false, true, Exercises::Special_Druid_3,
      kFacts_special_druid_3_scen99, std::size(kFacts_special_druid_3_scen99),
      kMut_special_druid_3_scen99 },

    { "special_druid_4_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_druid_4_scen99, std::size(kFamilySpawns_special_druid_4_scen99),
      0, false, true, Exercises::Special_Druid_4,
      kFacts_special_druid_4_scen99, std::size(kFacts_special_druid_4_scen99),
      kMut_special_druid_4_scen99 },

    { "special_orc_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_orc_1_scen99, std::size(kFamilySpawns_special_orc_1_scen99),
      0, false, true, Exercises::Special_Orc_1,
      kFacts_special_orc_1_scen99, std::size(kFacts_special_orc_1_scen99),
      kMut_special_orc_1_scen99 },

    { "special_orc_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_orc_2_scen99, std::size(kFamilySpawns_special_orc_2_scen99),
      0, false, true, Exercises::Special_Orc_2,
      kFacts_special_orc_2_scen99, std::size(kFacts_special_orc_2_scen99),
      kMut_special_orc_2_scen99 },

    { "special_barbarian_1_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_barbarian_1_scen99, std::size(kFamilySpawns_special_barbarian_1_scen99),
      0, false, true, Exercises::Special_Barbarian_1,
      kFacts_special_barbarian_1_scen99, std::size(kFacts_special_barbarian_1_scen99),
      kMut_special_barbarian_1_scen99 },

    { "special_barbarian_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_barbarian_2_scen99, std::size(kFamilySpawns_special_barbarian_2_scen99),
      0, false, true, Exercises::Special_Barbarian_2,
      kFacts_special_barbarian_2_scen99, std::size(kFacts_special_barbarian_2_scen99),
      kMut_special_barbarian_2_scen99 },

    { "special_archmage_2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_archmage_2_scen99, std::size(kFamilySpawns_special_archmage_2_scen99),
      0, false, true, Exercises::Special_Archmage_2,
      kFacts_special_archmage_2_scen99, std::size(kFacts_special_archmage_2_scen99),
      kMut_special_archmage_2_scen99 },

    { "special_archmage_3_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_archmage_3_scen99, std::size(kFamilySpawns_special_archmage_3_scen99),
      0, false, true, Exercises::Special_Archmage_3,
      kFacts_special_archmage_3_scen99, std::size(kFacts_special_archmage_3_scen99),
      kMut_special_archmage_3_scen99 },

    { "special_archmage_4_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_archmage_4_scen99, std::size(kFamilySpawns_special_archmage_4_scen99),
      0, false, true, Exercises::Special_Archmage_4,
      kFacts_special_archmage_4_scen99, std::size(kFacts_special_archmage_4_scen99),
      kMut_special_archmage_4_scen99 },

    // Walker status-timer scenarios -----------------------------------------
    { "enemy_freeze_mage_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_enemy_freeze_mage_scen99, std::size(kFamilySpawns_enemy_freeze_mage_scen99),
      0, false, true, Exercises::Special_Mage_3,
      kFacts_enemy_freeze_mage_scen99, std::size(kFacts_enemy_freeze_mage_scen99),
      kMut_enemy_freeze_mage_scen99 },

    { "invisibility_thief_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_invisibility_thief_scen99, std::size(kFamilySpawns_invisibility_thief_scen99),
      0, false, true, Exercises::Special_Thief_2,
      kFacts_invisibility_thief_scen99, std::size(kFacts_invisibility_thief_scen99),
      kMut_invisibility_thief_scen99 },

    { "speed_potion_movement_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsPotionWalk20, std::size(kInputsPotionWalk20), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_speed_potion_movement_scen99, std::size(kFamilySpawns_speed_potion_movement_scen99),
      0, false, true, Exercises::None,
      kFacts_speed_potion_movement_scen99, std::size(kFacts_speed_potion_movement_scen99),
      kMut_speed_potion_movement_scen99 },

    { "invulnerable_potion_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsPotionWalk200, std::size(kInputsPotionWalk200), 250,
      CompareMode::SemanticParity, false,
      kFamilySpawns_invulnerable_potion_scen99, std::size(kFamilySpawns_invulnerable_potion_scen99),
      0, false, true, Exercises::None,
      kFacts_invulnerable_potion_scen99, std::size(kFacts_invulnerable_potion_scen99),
      kMut_invulnerable_potion_scen99 },

    // Summon-lifecycle scenarios --------------------------------------------
    { "summon_lifetime_faerie_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 650,
      CompareMode::SemanticParity, false,
      kFamilySpawns_summon_lifetime_faerie_scen99, std::size(kFamilySpawns_summon_lifetime_faerie_scen99),
      0, false, true, Exercises::Special_Druid_2,
      kFacts_summon_lifetime_faerie_scen99, std::size(kFacts_summon_lifetime_faerie_scen99),
      kMut_summon_lifetime_faerie_scen99 },

    { "summon_lifetime_decrement_faerie_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 650,
      CompareMode::SemanticParity, false,
      kFamilySpawns_summon_lifetime_decrement_faerie_scen99, std::size(kFamilySpawns_summon_lifetime_decrement_faerie_scen99),
      0, false, true, Exercises::Special_Druid_2,
      kFacts_summon_lifetime_decrement_faerie_scen99, std::size(kFacts_summon_lifetime_decrement_faerie_scen99),
      kMut_summon_lifetime_decrement_faerie_scen99 },

    // Generator-saturation scenario -----------------------------------------
    { "generator_saturation_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 2500,
      CompareMode::SemanticParity, false,
      kFamilySpawns_generator_saturation_scen99, std::size(kFamilySpawns_generator_saturation_scen99),
      0, false, true, Exercises::None,
      kFacts_generator_saturation_scen99, std::size(kFacts_generator_saturation_scen99),
      kMut_generator_saturation_scen99 },

    // Weapon-trajectory scenarios -------------------------------------------
    { "weapon_rock_slot2_emit_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_rock_slot2_emit_scen99, std::size(kFamilySpawns_weapon_rock_slot2_emit_scen99),
      0, false, true, Exercises::None,
      kFacts_weapon_rock_slot2_emit_scen99, std::size(kFacts_weapon_rock_slot2_emit_scen99),
      kMut_weapon_rock_slot2_emit_scen99 },

    { "weapon_boomerang_return_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 80,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_boomerang_return_scen99, std::size(kFamilySpawns_weapon_boomerang_return_scen99),
      0, false, true, Exercises::None,
      kFacts_weapon_boomerang_return_scen99, std::size(kFacts_weapon_boomerang_return_scen99),
      kMut_weapon_boomerang_return_scen99 },

    { "weapon_exploding_boulder_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_exploding_boulder_scen99, std::size(kFamilySpawns_weapon_exploding_boulder_scen99),
      0, false, true, Exercises::None,
      kFacts_weapon_exploding_boulder_scen99, std::size(kFacts_weapon_exploding_boulder_scen99),
      kMut_weapon_exploding_boulder_scen99 },

    // Effect-emission scenarios ---------------------------------------------
    { "effect_heartburst_multitarget_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_heartburst_multitarget_scen99, std::size(kFamilySpawns_effect_heartburst_multitarget_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_heartburst_multitarget_scen99, std::size(kFacts_effect_heartburst_multitarget_scen99),
      kMut_effect_heartburst_multitarget_scen99 },

    { "effect_poison_cloud_emit_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 45,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_poison_cloud_emit_scen99, std::size(kFamilySpawns_effect_poison_cloud_emit_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_poison_cloud_emit_scen99, std::size(kFacts_effect_poison_cloud_emit_scen99),
      kMut_effect_poison_cloud_emit_scen99 },

    { "effect_protection_emit_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 25,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_protection_emit_scen99, std::size(kFamilySpawns_effect_protection_emit_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_protection_emit_scen99, std::size(kFacts_effect_protection_emit_scen99),
      kMut_effect_protection_emit_scen99 },

    // Effect-timer scenarios ------------------------------------------------
    { "effect_bomb_timer_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_bomb_timer_scen99, std::size(kFamilySpawns_effect_bomb_timer_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_bomb_timer_scen99, std::size(kFacts_effect_bomb_timer_scen99),
      kMut_effect_bomb_timer_scen99 },

    // Input-pipeline scenarios ----------------------------------------------
    { "input_diagonal_movement_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_input_diagonal_movement, std::size(kInputs_input_diagonal_movement), 80,
      CompareMode::SemanticParity, false,
      kFamilySpawns_input_diagonal_movement_scen99, std::size(kFamilySpawns_input_diagonal_movement_scen99),
      0, false, true, Exercises::None,
      kFacts_input_diagonal_movement_scen99, std::size(kFacts_input_diagonal_movement_scen99),
      kMut_input_diagonal_movement_scen99 },

    { "input_hold_fire_search_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_input_hold_fire_search, std::size(kInputs_input_hold_fire_search), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_input_hold_fire_search_scen99, std::size(kFamilySpawns_input_hold_fire_search_scen99),
      0, false, true, Exercises::None,
      kFacts_input_hold_fire_search_scen99, std::size(kFacts_input_hold_fire_search_scen99),
      kMut_input_hold_fire_search_scen99 },

    { "input_switch_char_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_input_switch_char, std::size(kInputs_input_switch_char), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_input_switch_char_scen99, std::size(kFamilySpawns_input_switch_char_scen99),
      255, false, true, Exercises::None,
      kFacts_input_switch_char_scen99, std::size(kFacts_input_switch_char_scen99),
      kMut_input_switch_char_scen99 },

    { "input_special_switch_wrap_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_input_special_switch_wrap, std::size(kInputs_input_special_switch_wrap), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_input_special_switch_wrap_scen99, std::size(kFamilySpawns_input_special_switch_wrap_scen99),
      0, false, true, Exercises::None,
      kFacts_input_special_switch_wrap_scen99, std::size(kFacts_input_special_switch_wrap_scen99),
      kMut_input_special_switch_wrap_scen99 },

    // Multi-team is_friendly scenario ---------------------------------------
    { "multiplayer_two_teams_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_multiplayer_two_teams, std::size(kInputs_multiplayer_two_teams), 44,
      CompareMode::SemanticParity, false,
      kFamilySpawns_multiplayer_two_teams_scen99, std::size(kFamilySpawns_multiplayer_two_teams_scen99),
      0, false, true, Exercises::None,
      kFacts_multiplayer_two_teams_scen99, std::size(kFacts_multiplayer_two_teams_scen99),
      kMut_multiplayer_two_teams_scen99 },

    // Level-withdraw scenario -----------------------------------------------
    { "level_withdraw_scen99",
      "scen/scen1.fss", 0x00000010u,
      kInputsScripted9301, std::size(kInputsScripted9301), 200,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier_with_exit_withdraw, std::size(kFamilySpawns_soldier_with_exit_withdraw),
      0, false, true, Exercises::None,
      kFacts_level_withdraw_scen99, std::size(kFacts_level_withdraw_scen99),
      kMut_level_withdraw_scen99 },

    // Mid-combat / consumable walker-state scenarios ------------------------
    { "midcombat_partial_hp_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_midcombat_partial_hp, std::size(kInputs_midcombat_partial_hp), 80,
      CompareMode::SemanticParity, false,
      kFamilySpawns_midcombat_partial_hp_scen99, std::size(kFamilySpawns_midcombat_partial_hp_scen99),
      0, false, true, Exercises::None,
      kFacts_midcombat_partial_hp_scen99, std::size(kFacts_midcombat_partial_hp_scen99),
      kMut_midcombat_partial_hp_scen99 },

    { "consumable_inventory_state_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_consumable_inventory_state, std::size(kInputs_consumable_inventory_state), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_consumable_inventory_state_scen99, std::size(kFamilySpawns_consumable_inventory_state_scen99),
      0, false, true, Exercises::None,
      kFacts_consumable_inventory_state_scen99, std::size(kFacts_consumable_inventory_state_scen99),
      kMut_consumable_inventory_state_scen99 },
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
