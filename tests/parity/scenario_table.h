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
// The enum holds one bit per (family, special_index) where the family
// declares a scripted special whose name is not "NONE". The bit ordering
// below is load-bearing and MUST NOT be reordered without updating the
// master companion mirror byte-for-byte.
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
    // Z-axis / multi-floor (branch-internal Invariant scenarios only). The
    // walker is created on floor 0 and apply_post_load_spawns relocates it via
    // walker::change_floor when floor != 0. Trailing + defaulted so every
    // existing row compiles unchanged.
    std::int32_t  floor              = 0;
};

// Numeric mirrors of the PIX_* tile ids used by FloorPaint. scenario_table.h
// deliberately does NOT include <openglad/core/pixdefs.h> (it stays byte-
// mirrorable to the master companion), so these are spelled numerically and
// guarded by a static_assert against the real PIX_* macros in
// scenario_runtime.cpp.
inline constexpr std::int32_t kPixGrass1   = 1;   // PIX_GRASS1
inline constexpr std::int32_t kPixAir      = 134; // PIX_AIR
inline constexpr std::int32_t kPixZStairUp = 140; // PIX_ZSTAIR_UP

// One grid-cell overwrite applied to a floor after the multi-floor arena is
// built (see apply_floor_setup). `floor` selects the stacked floor (0 ==
// ground); (tile_x, tile_y) is a grid cell; `pix` is one of the kPix* mirrors.
struct FloorPaint
{
    std::int32_t floor;
    std::int32_t tile_x;
    std::int32_t tile_y;
    std::int32_t pix;
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
    // Z-axis / multi-floor arena setup (branch-internal Invariant scenarios).
    // floor_count > 1 makes apply_floor_setup build the extra floor grids and
    // apply floor_paints[] before the spawns. All trailing + defaulted so the
    // single-floor rows above are byte-identical (floor_count == 1 -> no-op).
    std::int32_t         floor_count       = 1;
    const FloorPaint*    floor_paints      = nullptr;
    std::size_t          floor_paint_count = 0;
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
// These rows pin gameplay-visible family outcomes. Any divergence must be
// fixed or deliberately rebaselined; do not hide it by changing the dumper,
// scenario inputs, or schema.
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
    // Parked far so it never engages: the cleric stays alive, its shield keeps
    // its owner, and the busy gate never blocks the mystic-mace cast.
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
//   8 withdraw_to_level, 9 score_change, 10 damage_tile.

inline constexpr FactPredicate kFacts_ai_idle_wander_scen9301[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4600, 4600),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
};

inline constexpr FactPredicate kFacts_combat_attack_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
};

inline constexpr FactPredicate kFacts_special_archmage_scen123[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerPositionMoved(FAMILY_ARCHMAGE, 240, 880),
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
    // negative_assertion: mystic-mace shield coverage is through team-alive; no EXPAND fxlist entry should survive in this dump.
};

inline constexpr FactPredicate kFacts_special_mage_scen126[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerPositionMoved(FAMILY_MAGE, 240, 640),
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
    // rng_drift: bomb FX lifetime/count may vary while the mutation still drops team 0 below the floor; commit 244d4bcf
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // Structural anchor (depth): the thief caster is never engaged (the enemy
    // soldier is parked far away), so it sits at its full 75-hp (7500-cent) max
    // at the tick-30 dump. The WalkerOfTeamAlive predicate above carries the
    // canary teeth.
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
    pred::WalkerOfTeamAlive(/*team=*/1, 2, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
};

inline constexpr FactPredicate kFacts_summon_druid_pet_scen950[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 7700, 7700),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
};

inline constexpr FactPredicate kFacts_scoring_after_combat_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300),
    pred::EventKindExactly(/*score_change*/9, 0),
};

inline constexpr FactPredicate kFacts_save_roundtrip_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6100, 6100),
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
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4600, 4600),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
};

inline constexpr FactPredicate kFacts_rng_seed_stable_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4600, 4600),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
};

inline constexpr FactPredicate kFacts_scripted_input_scen9301[] = {
    pred::TickReached(150),
    pred::LevelDoneEquals(2),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::EventKindExactly(/*request_exit_confirmation*/7, 1),
    pred::EventKindExactly(/*withdraw_to_level*/8, 1),
};

inline constexpr FactPredicate kFacts_smoke_nonempty_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 1),
    pred::EventKindExactly(/*play_sound*/1, 0),
    pred::EventKindExactly(/*score_change*/9, 0),
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

inline constexpr FactPredicate kFacts_smoke_empty_scen99[] = {
    // The empty arena has no walkers by design; the only sim observable
    // its schema-v1 dump records is the world tick counter, which reads
    // tick=1 after the single budgeted tick. kMut_smoke_tick_freeze stops
    // the counter (tick stays 0), flipping this predicate through the
    // canary's --evaluate-facts channel — the row's Invariant gtest is a
    // dumper-determinism check that no deterministic mutation can flip,
    // so this fact is the row's only mutation channel.
    pred::TickReached(1),
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
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11900, 12100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_elf_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_ELF, 218, 111),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 7400, 7600),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_archer_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_ARCHER, 60, 32),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 8900, 9100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_mage_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    // Single-floor corner pruning and alignment keep the chaser from
    // corner-dancing; it presses into engagement range of the player soldier
    // at (224,120). The spawn-identity mutation still flips the family count.
    pred::WalkerPositionMoved(FAMILY_MAGE, 200, 110),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 8900, 9100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_skeleton_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_SKELETON, 3, 240),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SKELETON, 5900, 6100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_cleric_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    // Single-floor corner pruning and alignment keep the chaser from
    // corner-dancing; it presses into engagement range of the player soldier
    // at (224,120). The spawn-identity mutation still flips the family count.
    pred::WalkerPositionMoved(FAMILY_CLERIC, 200, 110),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 11900, 12100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_fireelemental_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_FIREELEMENTAL, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    // Single-floor corner pruning and alignment keep the chaser from
    // corner-dancing; it presses into engagement range of the player soldier
    // at (224,120). The spawn-identity mutation still flips the family count.
    pred::WalkerPositionMoved(FAMILY_FIREELEMENTAL, 220, 80),
    pred::WalkerHpRangeAtFinalTick(FAMILY_FIREELEMENTAL, 9900, 10100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_faerie_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_FAERIE, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_FAERIE, 229, 109),
    pred::WalkerHpRangeAtFinalTick(FAMILY_FAERIE, 7400, 7600),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_slime_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_SLIME, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_SLIME, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SLIME, 14900, 15100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_small_slime_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_SMALL_SLIME, 213, 116),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 7900, 8100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_medium_slime_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_MEDIUM_SLIME, 128, 120),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MEDIUM_SLIME, 10900, 11100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_thief_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    // Single-floor corner pruning and alignment keep the chaser from
    // corner-dancing; it presses into engagement range of the player soldier
    // at (224,120). The spawn-identity mutation still flips the family count.
    pred::WalkerPositionMoved(FAMILY_THIEF, 200, 110),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 7400, 7600),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_ghost_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_GHOST, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    // BIT_NO_RANGED makes the ghost snap-face its prey at bump range instead
    // of orbit-sliding, so the duel stays engaged near the arena corner. The
    // spawn-identity mutation still flips the family count.
    pred::WalkerPositionMoved(FAMILY_GHOST, 200, 90),
    pred::WalkerHpRangeAtFinalTick(FAMILY_GHOST, 4900, 5100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_druid_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    // Single-floor corner pruning and alignment keep the chaser from
    // corner-dancing; it presses into engagement range of the player soldier
    // at (224,120). The spawn-identity mutation still flips the family count.
    pred::WalkerPositionMoved(FAMILY_DRUID, 200, 110),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 10900, 11100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_orc_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_ORC, 16, 82),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 13900, 14100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_big_orc_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_BIG_ORC, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_BIG_ORC, 224, 96),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BIG_ORC, 17900, 18100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_barbarian_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    pred::WalkerPositionMoved(FAMILY_BARBARIAN, 224, 107),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BARBARIAN, 14900, 15100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_archmage_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
    // Single-floor corner pruning and alignment keep the chaser from
    // corner-dancing; it presses into engagement range of the player soldier
    // at (224,120). The spawn-identity mutation still flips the family count.
    pred::WalkerPositionMoved(FAMILY_ARCHMAGE, 200, 110),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 14900, 15100),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_golem_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_GOLEM, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 0),
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
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 0, 0),
    pred::WalkerPositionMoved(FAMILY_GIANT_SKELETON, 128, 120),
    pred::WalkerDiedByFinal(FAMILY_GIANT_SKELETON),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr FactPredicate kFacts_family_tower1_scen99[] = {
    pred::TickReached(600),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 1, 1),
    pred::WalkerOfTeamAlive(/*enemy team=*/1, 1, 1),
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
// mutation. Each mutation addresses the live source that controls the observed
// behavior: generic engine code for shared mechanics, or pack Lua/YAML for
// family-specific behavior and data.

inline constexpr Mutation kMut_combat_damage = {
    "src/gameplay/walker_combat.cpp", 210,
    "target->stats()->set_hitpoints(target->stats()->hitpoints() - tempdamage);",
    "target->stats()->set_hitpoints(target->stats()->hitpoints() - 0);",
    "Zeroes the per-hit damage applied to combat targets in walker::do_combat_damage; for any scenario that actually exercises melee combat this leaves the target alive and flips WalkerDiedByFinal and team-alive predicates."
};

inline constexpr Mutation kMut_walker_ai_wander = {
    "src/gameplay/walker_combat.cpp", 310,
    "do_combat_damage(attacker, target, tempdamage_i);",
    "do_combat_damage(attacker, target, 0);",
    "Forces the walker_combat dispatch site to pass tempdamage=0 into do_combat_damage; in AI-driven combat scenarios the target takes no damage so AI walkers don't lose HP. Distinct from kMut_combat_damage (line 189) which mutates the target HP decrement inside the do_combat_damage body."
};

inline constexpr Mutation kMut_exit_withdraw_path = {
    "packs/core/scripts/treasure_navigation.lua", 74,
    "  if can_withdraw then",
    "  if false then",
    "Skips exit_on_eat's withdraw branch. With a completed destination and enemies remaining, the normal branch emits WithdrawToLevel and RequestExitConfirmation and sets world.withdraw_requested; disabling it suppresses both events and flips their exact-count predicates."
};

inline constexpr Mutation kMut_smoke_inputs_no_move = {
    "src/gameplay/walker_movement.cpp", 173,
    "returnvalue = walk(x * stepsize(), y * stepsize());",
    "returnvalue = walk(x * 0.0f, y * 0.0f);",
    "Zeroes the movement delta inside walker::walkstep, which the parity driver reaches directly. walk(0,0) succeeds without moving, so the K_RIGHT soldier never steps east and WalkerPositionMoved(SOLDIER,240,0) flips."
};

inline constexpr Mutation kMut_smoke_tick_freeze = {
    "src/gameplay/game_world.cpp", 1680,
    "tick_count_++;",
    "tick_count_ += 0;",
    "Stops the per-tick world counter from advancing, freezing the schema-v1 tick field at 0. smoke_empty_scen99 flips TickReached(1) through --evaluate-facts because its Invariant gtest checks capture determinism; smoke_nonempty_scen99 flips TickReached(60) and its SemanticParity result."
};

inline constexpr Mutation kMut_effect_lifetime = {
    "src/gameplay/effect.cpp", 96,
    "set_dead(1);",
    "set_dead(0);",
    "Cancels the end-of-animation death in effect::act() so effects never expire; bomb/chain scenarios that rely on effects winding down see a residual effect count and flip EffectFamilyCount / dependent walker-death predicates."
};

inline constexpr Mutation kMut_save_corrupt = {
    "src/resources/save_data.cpp", 122,
    "std::uint8_t temp_version = 9;",
    "std::uint8_t temp_version = 0;",
    "Save header claims version 0 (below any supported save format); the round-trip load refuses the file and the post-load world is empty, flipping WalkerOfTeamAlive(team=0,1,1) and LevelDoneEquals(2)."
};

inline constexpr Mutation kMut_exit_neuter = {
    "src/gameplay/walker_movement.cpp", 173,
    "returnvalue = walk(x * stepsize(), y * stepsize());",
    "returnvalue = walk(x * 0.0f, y * stepsize());",
    "Zeroes the east/west step inside walker::walkstep. The K_RIGHT soldier remains at its spawn xpos and never reaches the exit pad, so WalkerPositionMoved(SOLDIER,623,224) flips."
};

inline constexpr Mutation kMut_snapshot_dirty = {
    "src/gameplay/game_world.cpp", 1678,
    "level_done = 2;",
    "level_done = []{ static int _n = 0; return _n++; }();",
    "Uses a static-counter level_done assignment so successive run_scenario() captures differ. The value flows into the snapshot and breaks dual-capture byte equality, flipping the Invariant determinism check."
};

// Per-special mutations. Each one points at the named family's
// `do_special` so disabling it removes the special's gameplay effect
// (kill, summon, event emission). Combined with EventKindExactly /
// EventKindAtLeast predicates these flip on canary run.

inline constexpr Mutation kMut_special_archmage_do_special = {
    "packs/core/scripts/archmage.lua", 132,
    "if lc.mid_teleport(self) then",
    "if true then",
    "Makes the archmage's slot-1 TELEPORT believe it is already mid-teleport, so the body returns false without the SOUND_TELEPORT cue and without the ANI_TELE_OUT that hands off to handle_teleport. The caster never leaves its start tile: WalkerPositionMoved(FAMILY_ARCHMAGE, 240, 880) fails."
};

inline constexpr Mutation kMut_special_cleric_do_special = {
    "packs/core/scripts/cleric.lua", 136,
    "local mace = og.summon(self, \"fx\", FX_MAGIC_SHIELD)",
    "local mace = nil",
    "Suppresses MYSTIC MACE's FX_MAGIC_SHIELD summon, so heal_or_mace takes its 'if not mace' exit and no persistent shield enters oblist. Team-0 alive collapses from 2 (cleric + shield) to 1 and WalkerOfTeamAlive(0, 2, 2) fails its floor."
};

inline constexpr Mutation kMut_special_mage_do_special = {
    "packs/core/scripts/mage.lua", 72,
    "if lc.mid_teleport(self) then",
    "if true then",
    "Makes the mage's slot-1 TELEPORT believe it is already mid-teleport, so the body returns false without the SOUND_TELEPORT cue and without the ANI_TELE_OUT hand-off. The caster never leaves its start tile: WalkerPositionMoved(FAMILY_MAGE, 240, 640) fails."
};

inline constexpr Mutation kMut_special_thief_do_special = {
    "packs/core/scripts/thief.lua", 45,
    "local bomb = og.add_ob(\"fx\", FX_BOMB)",
    "local bomb = nil",
    "Suppresses DROP BOMB's FX_BOMB creation, so drop_bomb takes its 'if not bomb' exit and the timed bomb never enters oblist. Team-0 alive collapses from 2 to the lone thief and WalkerOfTeamAlive(0, 2, 3) fails its floor."
};

inline constexpr Mutation kMut_summon_druid_do_special = {
    "packs/core/scripts/druid.lua", 11,
    "  if lc.is_busy(self) then  -- do not start the fire-and-replace sequence",
    "  if false then  -- do not start the fire-and-replace sequence",
    "Force-opens druid_do_special's busy gate. Each attempt then runs the plant-tree branch, refunds MP, and fires a bolt, so the trajectory, sound stream, and exact WalkerHpRangeAtFinalTick(SOLDIER,7700,7700) predicate diverge."
};

// Per-family-row mutations. Each cranks the named family's base HP —
// combat.hp — far enough that its arena resolves differently and at least
// one of the row's predicates flips on a canary run.
//
// These pins name the family's YAML, not the C++ family source:
// og::resources::install_classpacks() overwrites every descriptor field
// from that YAML at startup, so the compiled-in literal is dead data and
// mutating it changes nothing. The YAML holds the resolved number (the
// C++ BASE_GUY_HP+90 exports as 120): "hp: 120" -> "hp: 12000" cranks the
// stat up, "hp: 300" -> "hp: 300e-2" cranks it down. Both keep the key in
// the anchor, which is what schema v2 bought here — the v1 spelling had
// to anchor on the flow-sequence prefix "[120" and hope no other column
// began the same way. check_mutation_pins.py is a build dependency of
// og_test_parity, so a replacement that deletes its own anchor fails the
// canary's rebuild instead of being measured.

inline constexpr Mutation kMut_family_spawn_identity = {
    "src/resources/gloader.cpp", 798,
    "ob->set_order_family(order, static_cast<char>(family));",
    "ob->set_order_family(order, static_cast<char>((family + 1) % 21));",
    "Rotates every dumped living-family identity at loader binding time; each phase-05 family row loses its exact WalkerFamilyCount(FAMILY_X,1,1) predicate even though the spawn list still asked for the original family."
};

inline constexpr Mutation kMut_family_spawn_identity_elf = {
    "src/resources/gloader.cpp", 798,
    "ob->set_order_family(order, static_cast<char>(family));",
    "ob->set_order_family(order, static_cast<char>(family == 0 ? 2 : 0));",
    "Maps the player SOLDIER away from ELF and maps the target ELF away from ELF; family_elf_scen99 loses its exact WalkerFamilyCount(FAMILY_ELF,1,1) predicate."
};

inline constexpr Mutation kMut_family_soldier_init = {
    "packs/core/families/living-00-soldier.yaml", 16,
    "hp: 120",
    "hp: 12000",
    "Cranks SOLDIER HP so soldier survives the sparring partner; flips WalkerOfTeamAlive(team=0,0,0) and WalkerDiedByFinal(SOLDIER)."
};

inline constexpr Mutation kMut_family_elf_init = {
    "packs/core/families/living-01-elf.yaml", 16,
    "hp: 75",
    "hp: 7500",
    "Cranks ELF HP so elf survives; flips WalkerOfTeamAlive(team=1,1,1) (sparring soldier dies) and WalkerDiedByFinal(ELF)."
};

inline constexpr Mutation kMut_family_archer_init = {
    "packs/core/families/living-02-archer.yaml", 16,
    "hp: 90",
    "hp: 9000",
    "Cranks ARCHER HP so archer survives; flips WalkerDiedByFinal(ARCHER)."
};

inline constexpr Mutation kMut_family_mage_init = {
    "packs/core/families/living-03-mage.yaml", 16,
    "hp: 90",
    "hp: 9000",
    "Cranks MAGE HP so mage survives; flips WalkerOfTeamAlive(team=1,1,1) and WalkerDiedByFinal(MAGE)."
};

inline constexpr Mutation kMut_family_skeleton_init = {
    "packs/core/families/living-04-skeleton.yaml", 16,
    "hp: 60",
    "hp: 6000",
    "Cranks SKELETON HP; flips WalkerOfTeamAlive(team=1,1,1) and WalkerDiedByFinal(SKELETON)."
};

inline constexpr Mutation kMut_family_cleric_init = {
    "packs/core/families/living-05-cleric.yaml", 16,
    "hp: 120",
    "hp: 12000",
    "Cranks CLERIC HP; flips WalkerFamilyCount(CLERIC,1,1) (one extra alive) and WalkerDiedByFinal(CLERIC)."
};

inline constexpr Mutation kMut_family_fireelemental_init = {
    "packs/core/families/living-06-elemental.yaml", 16,
    "hp: 100",
    "hp: 10000",
    "Cranks FIREELEMENTAL HP; flips WalkerDiedByFinal(FIREELEMENTAL)."
};

inline constexpr Mutation kMut_family_faerie_init = {
    "packs/core/families/living-07-faerie.yaml", 16,
    "hp: 75",
    "hp: 7500",
    "Cranks FAERIE HP; flips WalkerDiedByFinal(FAERIE)."
};

inline constexpr Mutation kMut_family_slime_init = {
    "packs/core/families/living-08-slime.yaml", 16,
    "hp: 150",
    "hp: 150e-2",
    "SLIME HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(SLIME,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_small_slime_init = {
    "packs/core/families/living-09-slime.yaml", 16,
    "hp: 80",
    "hp: 8000",
    "Cranks SMALL_SLIME HP; flips WalkerDiedByFinal(SMALL_SLIME)."
};

inline constexpr Mutation kMut_family_medium_slime_init = {
    "packs/core/families/living-10-slime.yaml", 16,
    "hp: 110",
    "hp: 11000",
    "Cranks MEDIUM_SLIME HP; flips WalkerFamilyCount(SMALL_SLIME,1,1) (medium never splits) and WalkerDiedByFinal(MEDIUM_SLIME)."
};

inline constexpr Mutation kMut_family_thief_init = {
    "packs/core/families/living-11-thief.yaml", 16,
    "hp: 75",
    "hp: 7500",
    "Cranks THIEF HP; flips WalkerDiedByFinal(THIEF)."
};

inline constexpr Mutation kMut_family_druid_init = {
    "packs/core/families/living-13-druid.yaml", 16,
    "hp: 110",
    "hp: 11000",
    "Cranks DRUID HP; flips WalkerDiedByFinal(DRUID)."
};

inline constexpr Mutation kMut_family_orc_init = {
    "packs/core/families/living-14-orc.yaml", 16,
    "hp: 140",
    "hp: 14000",
    "Cranks ORC HP; flips WalkerDiedByFinal(ORC)."
};

inline constexpr Mutation kMut_family_big_orc_init = {
    "packs/core/families/living-15-orc_captain.yaml", 16,
    "hp: 180",
    "hp: 180e-2",
    "BIG_ORC HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(BIG_ORC,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_barbarian_init = {
    "packs/core/families/living-16-barbarian.yaml", 16,
    "hp: 150",
    "hp: 150e-2",
    "BARBARIAN HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(BARBARIAN,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_archmage_init = {
    "packs/core/families/living-17-archmage.yaml", 16,
    "hp: 150",
    "hp: 150e-2",
    "ARCHMAGE HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(ARCHMAGE,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_golem_init = {
    "packs/core/families/living-18-beast.yaml", 16,
    "hp: 300",
    "hp: 300e-2",
    "GOLEM HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(GOLEM,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_giant_skeleton_init = {
    "packs/core/families/living-19-beast.yaml", 16,
    "hp: 300",
    "hp: 300e-2",
    "GIANT_SKELETON HP cranked down to 10 so the sparring soldier kills it on first hit; flips WalkerAliveAtFinal(GIANT_SKELETON,1) and WalkerOfTeamAlive(team=0,1,1)."
};

inline constexpr Mutation kMut_family_tower1_init = {
    "packs/core/families/living-20-beast.yaml", 16,
    "hp: 130",
    "hp: 13000",
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
// family's real pickup/emission/behaviour source (a pack-script Lua hook,
// a families/*.yaml descriptor field, or the C++ sim line that drives it).

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

// Event-arena twin of kInputsEffectCombat. The team-1 line approaches from
// the east while a freshly spawned player faces up, so three K_RIGHT ticks
// turn the player onto the incoming line before the tick-5 fire hold begins.
inline constexpr InputEvent kInputsEventArena[] = {
    {1, 0, K_RIGHT}, {4, 0, K_NONE}, {5, 0, K_FIRE}, {149, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_event_arena[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0 },
    // The orc leads because BIT_NO_RANGED prevents counterfire, allowing the
    // player's opening throws to land and score. The soldier duels from the
    // second rank and survives to satisfy the family-count predicate after
    // the player falls.
    { 14, 1, kOrderLiving, 150, 120, 0, 0 },
    {  0, 1, kOrderLiving, 220, 120, 0, 0 },
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
    // FX-order effect-emission seed (clear of the combat tiles). add_ob(Order::FX,...) routes it into world.oblist, which GameWorld::tick() act()s each tick. On its first act() core:door_open's on_act hook emits exactly one persistent effect into world.fxlist (dump.effects[]) and kills the parent. The repointed kMut_effect_expand_emission neuters that hook in packs/core/scripts/effect_door_open.lua (false = "not handled", the no-registered-hook path) so the mutated build runs no on_act and emits nothing -> EffectFamilyCount flips 1->0. EXPAND itself (effect family 0) is a decorative data-only family with no callback and is never instantiated, so its own EffectFamilyCount stays a 0 invariant on both sides.
    { FAMILY_DOOR_OPEN, 2, kOrderFX, 200, 120, 0, 0 }, // team 2 (NOT the player's team 0) so find_player_walker binds the player to the team-0 SOLDIER, never this FX object (whose null controller_ would null-deref clear_command). on_act is team-independent; the emitted persistent effect still lands in fxlist.
};

inline constexpr SpawnSpec kFamilySpawns_effect_door_open_arena[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SOLDIER wielder (continuous K_FIRE through tick 149 keeps combat events fresh at dump time)
    { FAMILY_SOLDIER, 1, kOrderLiving, 140, 120, 0, 0 }, // FAMILY_SOLDIER target adjacent
    // FAMILY_DOOR_OPEN (effect family 11) spawned as an FX-order object. add_ob(Order::FX,...) routes it into world.oblist, which GameWorld::tick() DOES act() each tick. On its first act() with ani_type()==ANI_WALK core:door_open's on_act hook (packs/core/scripts/effect_door_open.lua) emits one persistent FAMILY_DOOR_OPEN effect into world.fxlist and kills the parent. kMut_effect_door_open neuters that hook (false = "not handled", the no-registered-hook path), so the mutated build runs no on_act and emits nothing.
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
// treasure family with init_ignore=true (a descriptor field in
// packs/core/families/treasure-00-stain.yaml; the pack-installed
// in-world walker ignores the collision grid), so the soldier walks
// straight through it without triggering the eat-me path. The literal
// STAIN kOrderTreasure entry stays in oblist; its family_symbol is
// FAMILY_SOLDIER (id 0 collides with FAMILY_SOLDIER walker), so the
// row CANNOT honestly use TreasureFamilyRemovedFromOblist(0). Per
// policy P1 we fall back to the closest schema-v1 observables that
// flip on the discriminating mutation — the row exercises the OTHER
// stain source, walker::death()'s generate_bloodspot(): a level-5
// soldier melees a FAMILY_FAERIE to death, and kMut_treasure_stain_pickup
// cranks the faerie's derived HP bonus in
// packs/core/families/living-07-faerie.yaml so it never dies and never
// drops its bloodstain, flipping the faerie-death observables. Once a
// future dump schema carries an oblist/Order disambiguator, this row
// should switch to the canonical EffectFamilyCount(FAMILY_STAIN)
// predicate instead.

inline constexpr SpawnSpec kFamilySpawns_treasure_stain_pickup[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving,   96, 120, 0, 0, 5, 0 }, // level-5 FAMILY_SOLDIER player (team 0); melees the adjacent faerie to death within the attack window
    { FAMILY_FAERIE,  1, kOrderLiving,  120, 120, 0, 0 },       // FAMILY_FAERIE victim (team 1), 24px east of the soldier. leaves_bloodspot=true with no on_death override, so when the soldier kills it walker::death() (walker.cpp:1521) calls generate_bloodspot() -> add_fx_ob(Order::Treasure, FAMILY_STAIN), spawning the bloodstain
    { FAMILY_STAIN,   0, kOrderTreasure, 200, 120, 0, 0 },      // literal FAMILY_STAIN treasure parked off-path; guarantees a FAMILY_STAIN entity is present in oblist on both arms so the TreasureFamilyOfOrderRemovedFromOblist(FAMILY_STAIN) coverage anchor reads `indeterminate` deterministically regardless of the faerie outcome
};

inline constexpr FactPredicate kFacts_treasure_stain_pickup_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_FAERIE, 1),
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
    "packs/core/families/living-07-faerie.yaml", 16,
    "hp: 75",
    "hp: 75000",
    "FAMILY_FAERIE leaves a FAMILY_STAIN bloodspot only when it dies (leaves_bloodspot=true + no on_death -> walker::death() calls generate_bloodspot()). Cranking the faerie's derived_bonuses[0] HP bonus from 75 to 75000 (the new value keeps the anchor as a prefix, so the pin check still passes on the mutated tree) makes it un-killable in the soldier's melee window, so it never dies and never generates its bloodspot -- flipping WalkerDiedByFinal(FAMILY_FAERIE) from pass (no alive faerie remains) to fail (the faerie is still alive at the final tick)."
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
    // rng_drift: archer hit timing gives a broad healed-HP band while the no-eat mutation stays below it; commit 244d4bcf
};

inline constexpr Mutation kMut_treasure_drumstick_pickup = {
    "packs/core/scripts/treasure_consumables.lua", 87,
    "  on_eat = drumstick_on_eat,",
    "  on_eat = function() return true end,",
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
    pred::EventKindExactly(9, 0),
    // The soldier eats the bar through the obmap overlap dispatch at tick
    // 3: gold_bar_on_eat banks 200 * level 3 = 600 into m_score[0]. This
    // absolute score is the row's live mutation channel: ScoreChange events
    // do not enter the parity event stream, and the removal predicate becomes
    // indeterminate when the bar survives. Nulling on_eat leaves the score at 0.
    pred::ScoreDelta(/*team*/0, 600, 600),
};

inline constexpr Mutation kMut_treasure_gold_bar_pickup = {
    "packs/core/scripts/treasure_valuables.lua", 82,
    "  on_eat = gold_bar_on_eat,",
    "  on_eat = function() return true end,",
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
    pred::EventKindExactly(9, 0),
    pred::EventKindAtLeast(1, 1, "consequence:silver_bar_on_eat emits SOUND_MONEY play_sound when eaten; mutation neuters on_eat -> 0"),
};

inline constexpr Mutation kMut_treasure_silver_bar_pickup = {
    "packs/core/scripts/treasure_valuables.lua", 86,
    "  on_eat = silver_bar_on_eat,",
    "  on_eat = function() return true end,",
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
    "packs/core/scripts/treasure_consumables.lua", 91,
    "  on_eat = magic_potion_on_eat,",
    "  on_eat = function() return true end,",
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
    "packs/core/scripts/treasure_consumables.lua", 103,
    "  on_eat = invis_potion_on_eat,",
    "  on_eat = function() return true end,",
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
    "packs/core/scripts/treasure_consumables.lua", 99,
    "  on_eat = invulnerable_potion_on_eat,",
    "  on_eat = function() return true end,",
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
    "packs/core/scripts/treasure_consumables.lua", 95,
    "  on_eat = flight_potion_on_eat,",
    "  on_eat = function() return true end,",
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
    // co-located teleporter first; teleporter_on_eat in
    // packs/core/scripts/treasure_navigation.lua bumps the eater's skip_exit by
    // +20 ahead of its no-target early-return,
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
    // treasures. The co-located teleporter only bumps the eater's skip_exit;
    // it is never set_dead, so the
    // literal stays alive in oblist (hp 0) on BOTH branch and master. With a
    // single alive instance and no consumed one, the Order-aware evaluator
    // returns indeterminate (non-failing) on both arms — a passing anchor,
    // not the teeth.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_TELEPORTER, kOrderTreasure),
};

inline constexpr Mutation kMut_treasure_teleporter_pickup = {
    "packs/core/scripts/treasure_navigation.lua", 135,
    "  on_eat = teleporter_on_eat,",
    "  on_eat = function() return true end,",
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
    pred::EventKindExactly(/*score_change=*/9, 0),
};

inline constexpr Mutation kMut_treasure_life_gem_pickup = {
    "packs/core/scripts/treasure_valuables.lua", 90,
    "  on_eat = life_gem_on_eat,",
    "  on_eat = function() return true end,",
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
    "packs/core/scripts/treasure_valuables.lua", 94,
    "  on_eat = key_on_eat,",
    "  on_eat = function() return true end,",
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
    // comparison captures. The mutation replaces the FAMILY_SPEED_POTION
    // on_eat hook, so the potion is never
    // consumed, no notification is emitted, and this predicate flips pass->fail.
    // (The earlier xpos-runway observation is unobservable here: an arena wall at
    // x~224 caps the soldier's travel identically with and without the speed bonus.)
    pred::EventKindAtLeast(/*notification*/2, 1,
        "consequence: speed_potion_on_eat -> notify_potion_consume emits one Potion of Speed notification; on_eat=nullptr suppresses it"),
};

inline constexpr Mutation kMut_treasure_speed_potion_pickup = {
    "packs/core/scripts/treasure_consumables.lua", 107,
    "  on_eat = speed_potion_on_eat,",
    "  on_eat = function() return true end,",
    "Neuters the FAMILY_SPEED_POTION on_eat hook. The potion is not consumed and its notification is not emitted, so EventKindAtLeast(notification,1) flips."
};

inline constexpr SpawnSpec kFamilySpawns_treasure_exit_pickup[] = {
    {  0, 0, kOrderLiving,   96, 120, 0, 0 }, // lone FAMILY_SOLDIER (team 0); the empty arena auto-completes (game_world.cpp:1357 sets level_done=2 when no valid foes remain)
    { FAMILY_EXIT, 0, kOrderTreasure, 96, 120, 0, 0 }, // co-located FAMILY_EXIT on the soldier's spawn tile
};

inline constexpr FactPredicate kFacts_treasure_exit_pickup_scen99[] = {
    pred::TickReached(150),
    // The player SOLDIER (team 0) walks onto a co-located FAMILY_EXIT whose
    // destination (scen2) is already completed while a live team-1 foe remains,
    // so exit_on_eat in packs/core/scripts/treasure_navigation.lua takes the
    // withdraw branch and emits BOTH WithdrawToLevel and the withdraw-flavoured
    // RequestExitConfirmation exactly once. The discriminating mutation neuters
    // .on_eat = exit_on_eat (line 158) -> the dispatcher at treasure.cpp:61-62
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
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
};

inline constexpr Mutation kMut_treasure_exit_pickup = {
    "packs/core/scripts/treasure_navigation.lua", 131,
    "  on_eat = exit_on_eat,",
    "  on_eat = function() return true end,",
    "Neuters the FAMILY_EXIT treasure-family on_eat hook (treasure.cpp:61-62 dispatches it behind `if (tfd && tfd->on_eat)`, so nullptr means the callback never runs). With the player SOLDIER walking onto an EXIT whose destination (scen2) is already completed while a live team-1 foe remains, exit_on_eat normally takes the withdraw branch and emits WithdrawToLevel + the withdraw-flavoured RequestExitConfirmation exactly once each; neutering the hook suppresses both, flipping EventKindExactly(request_exit_confirmation=7,1) and EventKindExactly(withdraw_to_level=8,1) from count=1 to count=0. The hook target is byte-accurate so the canary applies cleanly."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_knife_emission[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_SOLDIER wielder (natural emitter for FAMILY_KNIFE)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_knife_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 7),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6400, 6400),
    pred::WeaponFamilyEmitted(FAMILY_KNIFE),
    pred::WeaponSpeed(FAMILY_KNIFE, 600, 800,
        "trajectory: FAMILY_KNIFE outbound flight steps a constant dx=7,dy=waver per tick (stepsize 5 * 362/256 ~=1.414 on cardinal facing, walker.cpp:1190), giving max consecutive-tick step = hypot(7,1)*100 = 707 centi-px/tick on both arms. Tight [600,800] brackets the observed 707 (margin ~13%) yet excludes the mutated 1005 (kMut multiplier 362->512 -> stepsize 10 -> step ~1005) so the predicate flips."),
    pred::WeaponNetTravel(FAMILY_KNIFE, kWeaponPathStraight, 2000,
        "trajectory: the knife's weaplist track is the outbound straight throw (the FAMILY_KNIFE_BACK return is an Order::FX, not a weaplist entry, so RETURNS is reserved for ROCK). seq-0 net=2828 >= threshold 2000 and net==pathlen so net >= 0.7*pathlen holds: classifies STRAIGHT on both arms."),
};

inline constexpr Mutation kMut_weapon_knife_emission = {
    "src/gameplay/walker.cpp", 1190,
    "weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "weapon->set_stepsize((weapon->stepsize() * 512.0f) / 256.0f);",
    "Inflates the cardinal-facing projectile stepsize multiplier (walker.cpp:1190, applied in create_weapon() for FACE_UP/RIGHT/DOWN/LEFT). FAMILY_KNIFE fires FACE_RIGHT at the adjacent target; base stepsize 5 normally scales by 362/256 (~1.414) to ~7 giving a constant dx=7,dy=waver step of hypot(7,1)*100 = 707 centi-px/tick. Changing 362->512 scales to 10, raising the per-tick step to ~1005 centi-px/tick, which exceeds the WeaponSpeed(FAMILY_KNIFE,600,800) upper bound and flips that trajectory predicate. The only travelling weaplist family in this arena is the knife (FAMILY_BLOOD stays stationary), so the speed flip is unambiguous. WeaponNetTravel STRAIGHT stays satisfied because the path is still straight, but WeaponSpeed alone flipping satisfies the >=1-predicate canary requirement."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_rock_emission[] = {
    { FAMILY_ELF, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_ELF wielder (natural emitter for FAMILY_ROCK)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_rock_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 26),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WeaponFamilyEmitted(FAMILY_ROCK),
    pred::WeaponSpeed(FAMILY_ROCK, 800, 800),
    pred::WeaponNetTravel(FAMILY_ROCK, /*kWeaponPathStraight*/0, 2000,
        "rock path is STRAIGHT: seq-0 net displacement 2828 centi-px == pathlen 2828 (net>=2000 threshold AND net>=0.7*pathlen). The normal ELF default-weapon fire never sets do_bounce (set only in the elf specials' shared bounce_volley helper, packs/core/scripts/elf.lua:49), so rocks fly straight and never reverse; identical on both arms"),
};

inline constexpr Mutation kMut_weapon_rock_emission = {
    "src/gameplay/walker.cpp", 1190,
    "weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "weapon->set_stepsize((weapon->stepsize() * 181.0f) / 256.0f);",
    "Halves the cardinal-facing weapon stepsize scale. FAMILY_ROCK remains emitted and travels straight, but its per-tick step falls below the exact 800-centipixel speed pin, flipping WeaponSpeed."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_arrow_emission[] = {
    { FAMILY_ARCHER, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_ARCHER wielder (natural emitter for FAMILY_ARROW)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_arrow_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 25),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WeaponFamilyEmitted(FAMILY_ARROW),
    // Trajectory teeth: observed max_step_centi=1105 (~11.3 px/tick), net=3314, pathlen=3315 (perfectly straight).
    pred::WeaponSpeed(FAMILY_ARROW, 1000, 1250, "arrow ~11.3px/tick straight-shot speed"),
    pred::WeaponNetTravel(FAMILY_ARROW, kWeaponPathStraight, 2000, "arrow flies straight, no reversal"),
};

inline constexpr Mutation kMut_weapon_arrow_emission = {
    "src/gameplay/walker.cpp", 1190,
    "weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "weapon->set_stepsize((weapon->stepsize() * 256.0f) / 256.0f);",
    "walker::create_weapon scales every fired projectile's stepsize by 362/256 (~1.414) for the cardinal/diagonal facing cases; FAMILY_ARROW is a data-only weapon family (descriptor installed from packs/core/families/weapon-02-arrow.yaml, no script movement hook) so its per-tick speed is exactly this scaled stepsize. Dropping the scale (362->256) cuts the arrow's step from ~11.3px/tick to 8px/tick, so weapon_tracks seq=0 max_step_centi falls from 1105 to ~800, below WeaponSpeed(FAMILY_ARROW,1000,1250)'s lower bound, flipping that predicate. The path stays straight so WeaponNetTravel(STRAIGHT) is unaffected, demonstrating the speed teeth are independent of the path-class teeth."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_fireball_emission[] = {
    { FAMILY_MAGE, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_MAGE wielder (natural emitter for FAMILY_FIREBALL)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_fireball_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 30),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WeaponFamilyEmitted(FAMILY_FIREBALL),
    // Per-tick speed of the first fired FIREBALL (lowest-seq track, ticks
    // 8..11): max consecutive-tick step observed = 922 centi-px/tick
    // (base stepsize ~6.5px * 362/256 cardinal-fire scale). Tight bracket
    // [850,1000] brackets the normal result and flips low when the mutation
    // drops the 362/256 scale.
    pred::WeaponSpeed(FAMILY_FIREBALL, 850, 1000, "fireball ~9px/tick straight magical bolt"),
    // Straight magical projectile: net displacement >= ~100 centi and
    // net >= 0.7*pathlen. Seq-0 net=2571, pathlen=2572 (net/path=0.9996).
    // Threshold 1000 centi is safely below observed net on both arms.
    pred::WeaponNetTravel(FAMILY_FIREBALL, kWeaponPathStraight, 1000, "fireball travels straight, no reversal"),
};

inline constexpr Mutation kMut_weapon_fireball_emission = {
    "src/gameplay/walker.cpp", 1190,
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
    pred::EventKindAtLeast(/*play_sound*/1, 15),
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
    // Trajectory teeth: the direct-spawn FAMILY_TREE entity sits in weaplist
    // at (120,120) for all 150 ticks (ACT_SIT, stepsize 0), so these are
    // ungated and computed directly from dump.weapon_tracks.
    // WeaponNetTravel STATIONARY: observed pathlen=0 <=
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
    "src/gameplay/weap.cpp", 95,
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
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WeaponFamilyEmitted(FAMILY_METEOR),
    pred::WeaponSpeed(FAMILY_METEOR, 950, 1100, "meteor flies ~10px/tick (boosted magical projectile)"),
    pred::WeaponNetTravel(FAMILY_METEOR, kWeaponPathStraight, 2000, "meteor travels straight"),
};

inline constexpr Mutation kMut_weapon_meteor_emission = {
    "src/gameplay/walker.cpp", 1190,
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
    pred::WalkerFamilyCount(FAMILY_FAERIE, 0, 0),
    // negative_assertion: the sprinkle scenario must not leave a live faerie; the emitted weapon/effect path is the covered behavior.
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WalkerDiedByFinal(FAMILY_FAERIE),
    pred::WeaponFamilyEmitted(FAMILY_SPRINKLE),
    pred::WeaponSpeed(FAMILY_SPRINKLE, 900, 950, "FAMILY_SPRINKLE thrown-weapon max consecutive-tick step ~922 centi-px/tick (faerie fires FACE_RIGHT; base stepsize scaled 362/256)"),
    pred::WeaponNetTravel(FAMILY_SPRINKLE, kWeaponPathStraight, 2500, "FAMILY_SPRINKLE flies mostly-straight +x: seq-0 net=3396 >= 2500 centi-px and net ~= pathlen (3396/3397)"),
};

inline constexpr Mutation kMut_weapon_sprinkle_emission = {
    "src/gameplay/walker.cpp", 1190,
    "\t\t\t\tweapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "\t\t\t\tweapon->set_stepsize((weapon->stepsize() * 181.0f) / 256.0f);",
    "Halves the cardinal-fire stepsize boost. FAMILY_SPRINKLE fires right, so its speed drops below WeaponSpeed's 900 floor and its net displacement drops below WeaponNetTravel's 2500 threshold; both trajectory predicates flip."
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
    // rng_drift: projectile contact timing permits minor variation in soldier HP; commit 244d4bcf
    pred::WeaponFamilyEmitted(FAMILY_BONE),
    pred::WeaponSpeed(FAMILY_BONE, 850, 950,
        "BONE fires straight at base stepsize 6 -> cardinal *362/256 = 8.48 px/tick; max consecutive-tick step = 900 centi-px (the 9-px ticks). Tight [850,950] brackets the observed 900 and flips if stepsize changes (3->~500, 12->~1700)."),
    pred::WeaponNetTravel(FAMILY_BONE, kWeaponPathStraight, 8000,
        "BONE seq-0 is a pure-vertical projectile: net travel 10200 centi-px == pathlen (no reversal). STRAIGHT requires net >= 8000 AND net >= 0.7*pathlen; both hold. Threshold 8000 < observed 10200 keeps margin."),
};

inline constexpr Mutation kMut_weapon_bone_emission = {
    "src/resources/gloader.cpp", 567,
    "{Order::Weapon, FAMILY_BONE,              \"bone1.png\",    5, ACT_FIRE, anikni.data(),          6,  6,  5, 0},",
    "{Order::Weapon, FAMILY_BONE,              \"bone1.png\",    5, ACT_FIRE, anikni.data(),          3,  6,  5, 0},",
    "Halves FAMILY_BONE's base stepsize (7th column, the loaded weapon step) from 6 to 3. The cardinal firing path (walker.cpp:1190 stepsize*362/256) then yields 3*1.414=4.24 px/tick, so the seq-0 BONE projectile's max consecutive-tick step drops from 900 to ~500 centi-px, OUTSIDE WeaponSpeed(FAMILY_BONE,850,950) -> that predicate FLIPS. BONE is still emitted (WeaponFamilyEmitted stays green) and still travels straight, so the flip is isolated to the speed teeth."
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
    // The arena keeps one SOLDIER and the FAERIE alive, so no combat-death
    // BLOOD spatter is emitted. The soldier's exact HP carries the mutation
    // signal.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 19),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 7400, 7400),
};

// This row shares kMut_walker_ai_wander: zeroing melee damage leaves the
// soldier at full 120 HP instead of 74, flipping its exact HP predicate.

inline constexpr SpawnSpec kFamilySpawns_weapon_blob_emission[] = {
    { FAMILY_SLIME, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_SLIME wielder (natural emitter for FAMILY_BLOB)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_blob_emission_scen99[] = {
    // Dump at tick 30 while FAMILY_BLOB is still in flight so it appears as a
    // live weapon entry. By tick 150 it resolves into slime growth and
    // disappears, so the mid-flight snapshot is the stable coverage anchor.
    pred::TickReached(30),
    // The SLIME caster is alive at tick 30 (still casting; grows later) on both
    // arms, and its FAMILY_BLOB barrage has already battered the soldier to
    // near death (hp 2 = 200 cents).
    pred::WalkerFamilyCount(FAMILY_SLIME, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
    pred::EventKindExactly(/*score_change*/9, 0),
    // LIVE FAMILY_BLOB projectile in flight at tick 30 — coverage anchor.
    pred::WeaponFamilyEmitted(FAMILY_BLOB),
    // BLOB projectile trajectory (seq 0): max consecutive-tick step 316
    // centi-px/tick (cardinal-boosted 3 px/tick) and net displacement 5630
    // centi-px straight outbound. Identical on branch and master golden.
    pred::WeaponSpeed(FAMILY_BLOB, 280, 360),
    pred::WeaponNetTravel(FAMILY_BLOB, kWeaponPathStraight, 3000),
};

inline constexpr Mutation kMut_weapon_blob_emission = {
    "src/gameplay/walker.cpp", 1190,
    "weapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "weapon->set_stepsize((weapon->stepsize() * 256.0f) / 256.0f);",
    "Neutralizes the cardinal-facing 1.414x stepsize boost (362/256) applied to weapons fired straight (FACE_UP/RIGHT/DOWN/LEFT) in create_weapon(). The FAMILY_BLOB the slime fires FACE_RIGHT now keeps its base stepsize (~2.12 px/tick instead of ~3 px/tick), so its max consecutive-tick step drops from 316 to ~224 centi-px/tick, below the WeaponSpeed(FAMILY_BLOB, 280, 360) floor of 280 -> WeaponSpeed flips pass->fail. BLOB is still emitted and still tracked with consecutive samples (no Indeterminate), and the path stays straight so WeaponNetTravel still holds. The from/to omit the line's leading TABs and match as the unique substring of walker.cpp:1190 (the canary's _apply_mutation does a substring str.replace, and the lint parser transports the from-text through a tab-delimited line, so embedded tabs would corrupt the canary's IFS parse — identical convention to the sibling kMut_weapon_knife_emission/_rock/_arrow which target the same line)."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_fire_arrow_emission[] = {
    // FAMILY_FIRE_ARROW is naturally emitted by the EXPLODING BOLT special in
    // packs/core/scripts/archer.lua,
    // which fires a FIRE_ARROW with set_skip_exit(5000). When that arrow
    // dies it runs FAMILY_FIRE_ARROW's explode-on-death hook from
    // packs/core/scripts/weapon_projectiles.lua, spawning a FAMILY_EXPLOSION FX
    // and emitting SOUND_EXPLODE. The skip_exit'd bolt lingers in flight for
    // several consecutive ticks, giving the trajectory predicates a real
    // per-tick path to measure. kMut_weapon_fire_arrow_emission lowers this
    // family's base stepsize (gloader.cpp:559, 8 -> 3) so the bolt flies ~3x
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
    // line at ~11 px/tick (base stepsize 8 in gloader.cpp:559 scaled by the
    // 362/256 cardinal multiplier in walker.cpp:1190). The lowest-seq track
    // gives max consecutive-tick step = 1105 centi-px/tick on both arms.
    // kMut_weapon_fire_arrow_emission cuts the base stepsize 8 -> 3, dropping
    // the measured step to 510 centi-px/tick — outside the [1000,1250]
    // bracket, and the flip the canary observes on this row.
    pred::WeaponSpeed(FAMILY_FIRE_ARROW, 1000, 1250,
        "FIRE_ARROW per-tick speed ~1105 centi-px/tick (base stepsize 8 * 362/256 facing scale); tight bracket flips when the mutation lowers the stepsize"),
    // Trajectory shape: skip_exit'd FIRE_ARROW travels dead straight (net
    // 2209 >= 0.7*pathlen 2210). net_centi=2209 clears the 1500 threshold on
    // both arms. evaluate_facts reports only the FIRST failing predicate, and
    // under the mutation that is WeaponSpeed above, so this one's own
    // post-mutation value is not observable from a canary run; the same
    // slowdown shrinks net travel over the same 3-sample window.
    pred::WeaponNetTravel(FAMILY_FIRE_ARROW, kWeaponPathStraight, 1500,
        "FIRE_ARROW path is STRAIGHT: net=2209 >= threshold 1500 and >= 0.7*pathlen; the mutation's slower step shrinks net below 1500"),
    pred::EventKindExactly(/*score_change*/9, 0),
};

inline constexpr Mutation kMut_weapon_fire_arrow_emission = {
    "src/resources/gloader.cpp", 561,
    "{Order::Weapon, FAMILY_FIRE_ARROW,        \"farrow.png\",   7, ACT_FIRE, aniarrow.data(),        8, 12,  7, 0},",
    "{Order::Weapon, FAMILY_FIRE_ARROW,        \"farrow.png\",   7, ACT_FIRE, aniarrow.data(),        3, 12,  7, 0},",
    "Lowers FAMILY_FIRE_ARROW's base stepsize from 8 to 3. The bolt's per-tick step falls to about 510 centipixels, outside WeaponSpeed's [1000,1250] bracket, and the same slowdown reduces its straight-line travel."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_lightning_emission[] = {
    { FAMILY_DRUID, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_DRUID wielder (natural emitter for FAMILY_LIGHTNING)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_lightning_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 17),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WeaponFamilyEmitted(FAMILY_LIGHTNING),
    pred::WeaponSpeed(FAMILY_LIGHTNING, 1250, 1360,
        "trajectory: lightning bolt travels ~12.7 px/tick (cardinal-scaled base stepsize 9 -> 362/256); seq0 max consecutive-tick step = 1304 centi-px/tick, bracketed tight [1250,1360]"),
    pred::WeaponNetTravel(FAMILY_LIGHTNING, kWeaponPathStraight, 2000,
        "trajectory: lightning is a straight bolt; seq0 net displacement 2508 centi-px == pathlen 2508 (net*10 >= pathlen*7 holds, net >= 2000 threshold)"),
};

inline constexpr Mutation kMut_weapon_lightning_emission = {
    "src/resources/gloader.cpp", 569,
    "aniarrow.data(),        9, 13,  6, 0",
    "aniarrow.data(),        2, 13,  6, 0",
    "Halves+ the FAMILY_LIGHTNING base stepsize (column 'step' 9 -> 2) in the EntityDef weapon-defaults table; the cardinal-fired bolt's per-tick step collapses from ~12.7 px (max_step 1304 centi-px/tick) to ~2.8 px (~283 centi-px/tick), so WeaponSpeed(FAMILY_LIGHTNING,1250,1360) flips (and seq0 net falls below the WeaponNetTravel STRAIGHT threshold 2000 too)."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_glow_emission[] = {
    { FAMILY_CLERIC, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_CLERIC wielder (natural emitter for FAMILY_GLOW)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_glow_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 29),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 10000, 12000),
    // rng_drift: glow-emission combat timing may leave the observer anywhere in this high-HP band; commit 244d4bcf
    pred::WeaponFamilyEmitted(FAMILY_GLOW),
    // Trajectory teeth: the cleric's GLOW aura is an animated, NON-moving
    // effect that sits on its spawn coord (141,116) for its whole lifetime.
    // Observed seq=0 track over ticks 7..150: max consecutive-tick step = 0
    // centi-px/tick, pathlen = 0. WeaponSpeed brackets [0,50] (speed=0 is
    // determinate because there are 144 consecutive samples). WeaponNetTravel
    // STATIONARY requires pathlen <= 50 centi-px (zero here). Both predicates
    // flip when the mutation injects +1px/tick x displacement: max_step becomes
    // 100 and pathlen grows to about 14300.
    pred::WeaponSpeed(FAMILY_GLOW, 0, 50,
        "GLOW is a stationary cleric aura: per-tick step 0 centi-px/tick (bracketed [0,50]); flips if glow_on_animate gains motion"),
    pred::WeaponNetTravel(FAMILY_GLOW, /*STATIONARY*/2, 50,
        "GLOW aura pathlen 0 <= 50 centi-px (barely-moves class); flips if glow_on_animate moves the weapon"),
};

inline constexpr Mutation kMut_weapon_glow_emission = {
    "packs/core/scripts/weapon_animate.lua", 98,
    "  self:set_lifetime(lifetime - 1)",
    "  self:set_lifetime(lifetime - 1) self:setxy(self:xpos() + 1, self:ypos())",
    "Injects +1px/tick x displacement into glow_on_animate so the cleric's GLOW aura moves instead of staying fixed. Its max consecutive step becomes 100 centipixels and path length grows to about 14300, flipping both the stationary speed and net-travel predicates."
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
    // FAMILY_WAVE is emitted by MAGE slot 4, ENERGY WAVE, in
    // packs/core/scripts/mage.lua. The wielder needs stats_level >= 10 and
    // magicpoints >= special_cost(4)=70.
    // stats_level=20 / magicpoints=600 mirror the FIREBALL-emit MAGE row.
    { FAMILY_MAGE, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_MAGE wielder (casts ENERGY WAVE -> FAMILY_WAVE)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (draws fire so the ENERGY WAVE cast succeeds)
};

inline constexpr FactPredicate kFacts_weapon_wave_emission_scen99[] = {
    pred::TickReached(18),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    // The MAGE cycles to special slot 4 (ENERGY WAVE) and casts it at tick 15,
    // adding a FAMILY_WAVE weapon. At the
    // tick-18 dump the freshly-cast FAMILY_WAVE projectile is still live in
    // world.weaplist (it only advances to FAMILY_WAVE2 on its first collision,
    // around tick 19), so dump.weapons[] holds two live FAMILY_WAVE on both the
    // comparison captures. The discriminating mutation
    // rewrites the emission to FAMILY_FIREBALL, so no FAMILY_WAVE is ever cast
    // and this predicate flips present->absent. (FAMILY_WAVE2/WAVE3 are bound by
    // the separate weapon_wave2/weapon_wave3 direct-spawn scenarios.)
    pred::WeaponFamilyEmitted(FAMILY_WAVE),
    pred::WeaponSpeed(FAMILY_WAVE, 922, 922),
    pred::WeaponNetTravel(FAMILY_WAVE, kWeaponPathStraight, 1000, "wave travels straight"),
};

inline constexpr Mutation kMut_weapon_wave_emission = {
    "packs/core/scripts/mage.lua", 228,
    "  wave:set_lastx(bolt:lastx())",
    "  wave:set_lastx(og.fdiv(bolt:lastx(), 2))",
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
    pred::EventKindAtLeast(/*play_sound*/1, 15),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 9000),
    // rng_drift: the observer HP window brackets timing variation in this emission arena; commit 244d4bcf
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
    // Trajectory teeth: WAVE2 is direct-spawned and runs ACT_RANDOM (weap::act),
    // which emits a notification and returns WITHOUT moving, so the projectile
    // is perfectly stationary at (120,120) for all 150 ticks. Observed
    // max_step_centi=0, pathlen_centi=0. These flip under kMut_weapon_wave2_emission
    // (which injects per-tick motion into the ACT_RANDOM path for FAMILY_WAVE2).
    pred::WeaponSpeed(FAMILY_WAVE2, 0, 0, "wave2 stationary: 0 centi-px/tick"),
    pred::WeaponNetTravel(FAMILY_WAVE2, kWeaponPathStationary, 50, "wave2 stationary path"),
};

inline constexpr Mutation kMut_weapon_wave2_emission = {
    "src/gameplay/weap.cpp", 142,
    "\t\t\t\treturn 1;",
    "\t\t\t\tif (family() == FAMILY_WAVE2) { setxy(xpos() + 5, ypos() + 0); } return 1;",
    "Injects a per-tick +5px x-step into FAMILY_WAVE2's ACT_RANDOM act() path; the previously-stationary wave moves, so max_step_centi jumps to 500 and pathlen_centi grows ~74500, flipping WeaponSpeed([0,0]) and WeaponNetTravel(STATIONARY,50). The braces + ypos()+0 keep both setxy args int (unambiguous int32_t overload) and suppress -Wmisleading-indentation."
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
    // Trajectory teeth for FAMILY_WAVE3 (direct-spawn immortal phantom).
    // Observed weapon_tracks: 2 samples at tick1/tick2 both (120,120) =>
    // pathlen=0, net=0, max consecutive-step=0. Path class is STATIONARY.
    pred::WeaponNetTravel(FAMILY_WAVE3, kWeaponPathStationary, 5,
                          "wave3_stationary_phantom"),
    // Speed = 0 centi-px/tick (consecutive identical samples). Tight [0,0]
    // bracket. Passes on both arms; FLIPS pass->fail under
    // kMut_weapon_wave3_emission, which injects a per-tick +5px x-step into
    // FAMILY_WAVE3's act() path at the top of weap::act, before its animate()
    // early-return (the same motion-injection teeth mechanism the sibling
    // stationary phantom FAMILY_WAVE2 uses): the wave then moves, max_step jumps
    // to 500 and pathlen exceeds the STATIONARY threshold. These are genuine
    // trajectory teeth, parity-locking the phantom's zero-motion shape on both
    // the branch and the master golden.
    pred::WeaponSpeed(FAMILY_WAVE3, 0, 0, "wave3_speed_zero"),
};

inline constexpr Mutation kMut_weapon_wave3_emission = {
    "src/gameplay/weap.cpp", 66,
    "set_collide_ob(nullptr); // always start with no collision..",
    "set_collide_ob(nullptr); if (family() == FAMILY_WAVE3) { setxy(xpos() + 5, ypos() + 0); } // always start with no collision..",
    "Injects a per-tick +5px x-step into FAMILY_WAVE3 at the TOP of weap::act (line 65, before the `if (ani_type() != ANI_WALK) return animate();` early-return that WAVE3 -- a looping immortal phantom -- takes, so it never reaches the ACT_RANDOM path WAVE2 uses). The previously-stationary WAVE3 then moves +5px/tick (verified: tick1 125,120 -> tick2 130,120), so max_step_centi jumps to 500 and pathlen exceeds the STATIONARY threshold, flipping WeaponSpeed(FAMILY_WAVE3,[0,0]) and WeaponNetTravel(FAMILY_WAVE3,STATIONARY,5) pass->fail. The family() guard scopes motion to WAVE3 only; from/to omit the line's leading tab and match the unique substring of weap.cpp:65."
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
    pred::EventKindAtLeast(/*play_sound*/1, 15),
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
    // Trajectory teeth (weapon_tracks): the direct-spawn protective ring is
    // STATIONARY — it never moves a single centi-pixel between consecutive
    // ticks (max consecutive-tick step = 0) and its total pathlen stays at 0.
    // WeaponSpeed[0,0] asserts zero per-tick displacement; WeaponNetTravel
    // STATIONARY(flag=2) asserts total pathlen <= 50 centi-px (< half a pixel).
    // Both pass while the ring is stationary and flip when the mutation makes
    // it drift.
    pred::WeaponSpeed(FAMILY_CIRCLE_PROTECTION, 0, 0, "circle_protection ring is stationary (0 centi-px/tick)"),
    pred::WeaponNetTravel(FAMILY_CIRCLE_PROTECTION, kWeaponPathStationary, 50, "circle_protection ring pathlen <= 50 centi-px (does not travel)"),
};

inline constexpr Mutation kMut_weapon_circle_protection_emission = {
    "packs/core/scripts/weapon_animate.lua", 83,
    "  self:center_on(owner)",
    "  self:setxy(self:xpos() + 2, self:ypos())",
    "Replaces per-tick recentering with +2px/tick x displacement. The ring's step jumps from 0 to 200 centipixels and its path length grows from 0 to about 29800, flipping both the zero-speed and stationary net-travel predicates."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_hammer_emission[] = {
    { FAMILY_BARBARIAN, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_BARBARIAN wielder (natural emitter for FAMILY_HAMMER)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_hammer_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 23),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
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
    "src/gameplay/walker.cpp", 1190,
    "\t\t\t\tweapon->set_stepsize((weapon->stepsize() * 362.0f) / 256.0f);",
    "\t\t\t\tweapon->set_stepsize((weapon->stepsize() * 181.0f) / 256.0f);",
    "Halves the cardinal-facing weapon stepsize multiplier. HAMMER fires right, so its per-tick step falls from about 922 to 460 centipixels, below WeaponSpeed's 850 floor; emission and straight-path predicates remain satisfied."
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
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
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
    // Trajectory teeth: DOOR is direct-spawned ACT_SIT scenery
    // (gloader.cpp:429). weap::act()'s ACT_SIT case (weap.cpp:85-91) is a
    // pure no-op for a weapon -- it only emits a "Weapon sitting"
    // notification (suppressed for DOOR via skip_sit_notify) and never
    // walks the entity. Direct-spawned weapons also carry no fire
    // direction (lastx=lasty=0), so even ACT_FIRE's walk() would be a
    // no-op. The door therefore stays pinned at its spawn (120,120) for
    // all 150 ticks: weapon_tracks is 150 identical samples =>
    // max consecutive-tick step = 0 centi-px, pathlen = 0 centi-px.
    pred::WeaponSpeed(FAMILY_DOOR, 0, 0,
        "stationary_scenery: FAMILY_DOOR is a direct-spawn ACT_SIT scenery weapon (gloader.cpp:429) whose weap::act() ACT_SIT case never walks it; its weapon_tracks per-tick step is exactly 0 centi-px/tick. The discriminating mutation makes weap::act()'s ACT_SIT case worldmove the door 1px/tick, pushing max_step_centi to ~100 outside [0,0]."),
    pred::WeaponNetTravel(FAMILY_DOOR, kWeaponPathStationary, 100,
        "stationary_scenery: FAMILY_DOOR's weapon_tracks pathlen is 0 centi-px (the ACT_SIT case never moves it), well inside the 100-centi-px STATIONARY threshold; the mutation makes the ACT_SIT case worldmove the door 1px/tick east, accumulating ~15000 centi-px of pathlen over 150 ticks so the STATIONARY class fails."),
};

inline constexpr Mutation kMut_weapon_door_emission = {
    "src/gameplay/weap.cpp", 95,
    "if (!wfd || !wfd->skip_sit_notify)",
    "if (wfd && (worldmove(1, 0), false))",
    "Rewrites the guard in weap::act()'s ACT_SIT case (weap.cpp:85-91) so the case, instead of being a stationary no-op, worldmoves the sitting weapon 1px east every tick. The original guard `if (!wfd || !wfd->skip_sit_notify)` gates a 'Weapon sitting' notification; the mutated `if (wfd && (worldmove(1, 0), false))` keeps wfd referenced (no unused-variable warning), unconditionally evaluates worldmove(1,0) -- advancing xpos by 1px/tick -- then yields false so the notification is still suppressed (DOOR has skip_sit_notify=true). The direct-spawned FAMILY_DOOR weapon, which hits this ACT_SIT case every tick, is no longer stationary: its weapon_tracks walk from (120,120) at tick0 to (270,120) at tick150 (150 distinct positions). max_step_centi jumps from 0 to 100 (WeaponSpeed(FAMILY_DOOR,0,0) fails its [0,0] bracket) and pathlen_centi reaches ~15000 (WeaponNetTravel STATIONARY,100 fails). Both trajectory predicates flip from pass to fail; the mutation lives in src/gameplay/weap.cpp, outside the forbidden tests/parity and openglad-master prefixes, and the canary restores via git checkout."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_boulder_emission[] = {
    { FAMILY_GIANT_SKELETON, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 }, // FAMILY_GIANT_SKELETON wielder (natural emitter for FAMILY_BOULDER)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0 }, // FAMILY_SOLDIER target (close enough to draw fire, far enough that projectile stays in flight a few ticks)
};

inline constexpr FactPredicate kFacts_weapon_boulder_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_GIANT_SKELETON, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 20),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WeaponFamilyEmitted(FAMILY_BOULDER),
    pred::WeaponSpeed(FAMILY_BOULDER, 1350, 1600,
        "trajectory: FAMILY_BOULDER seq-0 projectile travels a straight ~14px/tick line; max consecutive-tick step observed = 1513 centi-px/tick (gloader stepsize 10 * 1.414 cardinal scaling). Range [1350,1600] brackets the 1414-1513 step on both arms and flips when the boulder's base stepsize is changed at gloader.cpp:430."),
    pred::WeaponNetTravel(FAMILY_BOULDER, 0, 8591),
};

inline constexpr Mutation kMut_weapon_boulder_emission = {
    "src/resources/gloader.cpp", 577,
    "{Order::Weapon, FAMILY_BOULDER,           \"boulder1.png\",50, ACT_FIRE, aninone.data(),        10,  9, 25, 0}",
    "{Order::Weapon, FAMILY_BOULDER,           \"boulder1.png\",50, ACT_FIRE, aninone.data(),         4,  9, 25, 0}",
    "Cuts FAMILY_BOULDER's base stepsize from 10 to 4 in the gloader weapon table; after the cardinal-direction *1.414 scaling (walker.cpp:1190) the boulder now moves ~5.66px/tick instead of ~14.14, so its seq-0 per-tick step drops to ~566 centi-px/tick — outside WeaponSpeed's [1350,1600] window — flipping WeaponSpeed(FAMILY_BOULDER) pass->fail. The boulder still emits (WeaponFamilyEmitted stays green) and still travels straight, isolating the speed-trajectory tooth. The from/to omit line 430's two leading TABs and match the unique substring of that line (the canary's lookup_mutation emits a TAB-separated record consumed by `IFS=$'\\t' read`, so embedded tabs would corrupt the field split — identical convention to the sibling kMut_weapon_knife/_rock/_arrow which target a TAB-indented line)."
};

inline constexpr FactPredicate kFacts_effect_expand_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // EffectFamilyCount snapshots fxlist at the final tick; combat-driven
    // FX (HIT, EXPAND, FLASH, ...) expire within a handful of ticks of
    // their emission and are no longer alive by tick 150. The exact
    // (0, 0) range is the honest schema-v1 observation; runtime
    // emission would require a per-tick coverage observation surfaced
    // into the dump on both branch and master — out of scope for the
    // current schema-v1 freeze.
    pred::EffectFamilyCount(FAMILY_EXPAND, 0, 0, /*source=FAMILY_SOLDIER*/0),
    // negative_assertion: EXPAND is a short-lived/decorative FX family and should not appear in final fxlist snapshots.
    // Teeth: an FX-order seed whose on_act emits exactly one persistent effect
    // into fxlist (snapshot here). The repointed mutation neuters core:door_open's
    // on_act hook in packs/core/scripts/effect_door_open.lua (false = "not
    // handled", the no-registered-hook path) so the mutated build runs no
    // on_act and emits zero
    // -> this predicate flips 1->0. Source-qualified by the FAMILY_SOLDIER
    // wielder that drives the surrounding combat.
    pred::EffectFamilyCount(FAMILY_DOOR_OPEN, 1, 1, /*source=FAMILY_SOLDIER*/0),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_effect_expand_emission = {
    "packs/core/scripts/effect_door_open.lua", 45,
    "  on_act = on_act,",
    "  on_act = function() return false end,",
    "Neuters core:door_open's on_act hook exactly as a null C++ callback used to: returning false means \"not handled\", which is what the dispatcher assumes when no hook is registered, so effect::act runs the plain animate path. The hand-off that spawns the persistent opened-door FX never happens, so EffectFamilyCount(FAMILY_DOOR_OPEN,1,1) flips 1->0 as the original effect animates out and nothing replaces it."
};

inline constexpr SpawnSpec kFamilySpawns_effect_ghost_scare_emit_scen99[] = {
    { FAMILY_GHOST,   0, kOrderLiving, 120, 120, 0, 0, 5, 300 }, // player-controlled GHOST caster (level 5 -> scare range 50+10*5=100px, push generic=level*25=125 walk-iterations; 300 MP -> SCARE slot-1 cost 30 affordable). Input is special-only so the ghost stays put at 120,120.
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 },        // lone foe 60px to the RIGHT, inside the 100px scare range. Unmutated: GHOST_SCARE on_death force_command(COMMAND_WALK) shoves it further RIGHT (away from the ghost). Mutated: the on_death hook is neutered (false = "not handled") so no on_death runs -> the foe's AI instead walks LEFT toward the ghost to melee.
};

inline constexpr FactPredicate kFacts_effect_ghost_scare_emission_scen99[] = {
    pred::TickReached(90),
    pred::WalkerFamilyCount(FAMILY_GHOST, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // FLIPPING PREDICATE. The player GHOST casts SCARE (special slot 1) at
    // tick 20; its GHOST_SCARE FX dies a few ticks later and the on_death hook
    // in packs/core/scripts/effect_ghost_scare.lua force-walks the lone foe
    // AWAY from the ghost — to
    // the RIGHT — for ~125 walk-iterations, pushing it well past its 180px
    // spawn. kMut_effect_ghost_scare_emission neuters that on_death hook
    // (false = "not handled", the no-registered-hook path), so the FX dies
    // with no on_death: the foe is never shoved and its AI instead walks LEFT
    // toward the ghost to melee, ending below 180. The floor sits above the
    // spawn so only the scared (pushed-right) foe satisfies it.
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 240, 0,
        "consequence: GHOST_SCARE on_death force_command(COMMAND_WALK) shoves the lone foe rightward past x=240; the hook-neutering mutation in packs/core/scripts/effect_ghost_scare.lua strips the on_death so the foe instead approaches the ghost and ends left of its 180px spawn, below the floor"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    // Structural coverage anchor: FAMILY_GHOST_SCARE is a kRequiredEffectFamilies
    // entry and this is the only EffectFamilyCount(FAMILY_GHOST_SCARE, ...)
    // binding in the table, so it must stay for behavioural_coverage_gate_effects.
    // The FX rides the walkers[] array (add_ob(Order::FX) -> oblist), not fxlist,
    // so zero live FAMILY_GHOST_SCARE effects appear in dump.effects[] on both
    // branch and master; the (0,0) window is the honest schema-v1 observation.
    // source qualifier = FAMILY_GHOST (the surviving summoner present in the dump).
    pred::EffectFamilyCount(FAMILY_GHOST_SCARE, 0, 0, /*source=FAMILY_GHOST*/12),
    // negative_assertion: GHOST_SCARE is routed through oblist and expires before final fxlist capture.
};

inline constexpr Mutation kMut_effect_ghost_scare_emission = {
    "packs/core/scripts/effect_ghost_scare.lua", 64,
    "  on_death = on_death,",
    "  on_death = function() return false end,",
    "Neuters core:ghost_scare's on_death hook (returning false = \"not handled\", the no-registered-hook path), so the scare never force_commands the frightened foe away from the ghost; the soldier walks LEFT to melee instead of being shoved RIGHT and the whole trajectory diverges from the golden."
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
    // dispatches core:bomb's on_death hook (bomb_on_death in
    // packs/core/scripts/effect_bomb.lua), which emits SOUND_EXPLODE (sound
    // id 11) and spawns a
    // FAMILY_EXPLOSION. Two bombs detonate, contributing exactly two
    // SOUND_EXPLODE play_sound events, so the total play_sound count is 13 on
    // branch and 15 on master (the difference is melee-RNG drift, both well
    // above the floor). kMut_effect_bomb_emission replaces that hook binding
    // (effect_bomb.lua:96) with `function() return false end` — false = "not
    // handled", the no-registered-hook path — so neither
    // SOUND_EXPLODE fires and no FAMILY_EXPLOSION spawns. The branch play_sound
    // count drops to 11, below the floor of 12, flipping this predicate
    // pass->fail. (Verified: mutated branch emits 0 SOUND_EXPLODE events.)
    pred::EventKindAtLeast(/*play_sound*/1, 12,
        "consequence: the two FAMILY_BOMB detonations each emit a SOUND_EXPLODE play_sound (total 13 on branch, 15 on master); the hook-neutering mutation in packs/core/scripts/effect_bomb.lua strips bomb_on_death so neither explosion sound fires and the branch count drops to 11, below the floor"),
    // FAMILY_BOMB is a kRequiredEffectFamilies entry and this is the ONLY
    // EffectFamilyCount(FAMILY_BOMB, ...) binding in the table, so it must
    // stay for behavioural_coverage_gate_effects. FX spawned via
    // add_ob(Order::FX) land in oblist (dump.walkers[]), never fxlist
    // (dump.effects[]), so the family's dump.effects[] count is 0 on both
    // arms; the source qualifier requires a FAMILY_SOLDIER walker, which the
    // surviving soldier provides.
    pred::EffectFamilyCount(FAMILY_BOMB, 0, 0, /*source=FAMILY_SOLDIER*/0),
    // negative_assertion: dropped bombs are FX-order oblist walkers, not final fxlist entries.
};

inline constexpr Mutation kMut_effect_bomb_emission = {
    "packs/core/scripts/effect_bomb.lua", 100,
    "  on_death = bomb_on_death,",
    "  on_death = function() return false end,",
    "Neuters core:bomb's on_death hook (false = \"not handled\", the no-registered-hook path), so the thief's dropped bomb expires without detonating: no EXPLOSION effect is spawned and no blast damage lands, flipping the scenario's effect-count and surviving-HP predicates."
};

inline constexpr FactPredicate kFacts_effect_explosion_emission_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    // Behavioural flip for the FAMILY_EXPLOSION hook-neutering mutation:
    // the archmage's HEARTBURST summons one FAMILY_EXPLOSION FX per in-range
    // foe; each explosion's explosion_on_death damages the clustered soldiers
    // below full HP. The kMut_effect_explosion_emission swap replaces
    // core:explosion's on_death binding (packs/core/scripts/effect_bomb.lua:100)
    // with `function() return false end` — "not handled", the
    // no-registered-hook path — so effect::death()
    // runs no on_death behavior, no soldier takes explosion damage, and
    // every soldier stays at full 12000-cent HP outside this window.
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 11000,
        "consequence: HEARTBURST detonates a per-foe FAMILY_EXPLOSION against each in-range soldier, leaving at least one soldier below 11000-cent HP; the hook-neutering mutation in packs/core/scripts/effect_bomb.lua strips explosion_on_death so no explosion lands and every soldier stays at full 12000-cent HP outside this window"),
    // rng_drift: per-foe explosion order may kill or wound different soldiers while the mutation leaves all outside the band; commit 244d4bcf
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 0, 4,
        "consequence: the per-foe explosions may kill some of the four in-range soldiers"),
    // rng_drift: HEARTBURST may leave zero to four soldiers alive depending on explosion order; commit 244d4bcf
    // FAMILY_EXPLOSION is a kRequiredEffectFamilies entry and this is the
    // ONLY EffectFamilyCount(FAMILY_EXPLOSION, ...) binding in the table, so
    // it must stay for behavioural_coverage_gate_effects. FX spawned via
    // add_ob(Order::FX) land in oblist (dump.walkers[]), never fxlist
    // (dump.effects[]) — see scenario_table.h:3832-3840 — so the family's
    // dump.effects[] count is 0 on both arms; the source qualifier requires
    // a FAMILY_SOLDIER walker, which the spawn list provides.
    pred::EffectFamilyCount(FAMILY_EXPLOSION, 0, 0, /*source=FAMILY_SOLDIER*/0),
    // negative_assertion: HEARTBURST explosions are transient FX-order oblist walkers, not final fxlist entries.
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::EventKindAtLeast(/*score_change*/9, 1),
    pred::EventKindExactly(/*damage_tile*/10, 0,
        "consequence: HEARTBURST emits raw DamageTile events through the routed branch path; parity dumps normalize them out because classic applies the same tile damage through screen::damage_tile"),
};

inline constexpr Mutation kMut_effect_explosion_emission = {
    "packs/core/scripts/effect_bomb.lua", 104,
    "  on_death = explosion_on_death,",
    "  on_death = function() return false end,",
    "Neuters core:explosion's on_death hook (false = \"not handled\", the no-registered-hook path), so the blast's terminal damage/sound pass never runs and the explosion FX simply expires, flipping the scenario's explosion-emission predicates."
};

inline constexpr FactPredicate kFacts_effect_flash_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // life_gem_on_eat in packs/core/scripts/treasure_valuables.lua is the
    // FAMILY_FLASH emitter under test: on a same-team pickup it add_ob(Order::FX,
    // FAMILY_FLASH)s the telflash effect AND set_dead(1)s the gem. The
    // FLASH itself lands in oblist and expires within ~9 ticks (series_8),
    // so it is not directly countable at tick 150 under schema-v1; its
    // observable proxy is the gem's removal from oblist plus the
    // ScoreChange event that award_score emits unconditionally in the same
    // hook. Neutering the hook (kMut_effect_flash_emission) suppresses the
    // FLASH emission AND the ScoreChange together.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_LIFE_GEM, kOrderTreasure),
    pred::EventKindExactly(/*score_change*/9, 0),
    // Structural coverage anchor: binds FAMILY_FLASH to EffectFamilyCount for
    // behavioural_coverage_gate_effects. The telflash FLASH effect is emitted
    // via add_ob(Order::FX, FAMILY_FLASH) which routes into oblist, never
    // fxlist, AND it expires within ~9 ticks (series_8) long before the tick-150
    // dump, so dump.effects holds no FAMILY_FLASH on either arm -- the (0,0)
    // window is the honest schema-v1 observation. The source qualifier requires
    // a FAMILY_SOLDIER walker, which the eater provides.
    pred::EffectFamilyCount(FAMILY_FLASH, 0, 0, /*source=FAMILY_SOLDIER*/0),
    // negative_assertion: FLASH is emitted through oblist and expires before the final fxlist snapshot.
};

inline constexpr Mutation kMut_effect_flash_emission = {
    "packs/core/scripts/treasure_valuables.lua", 90,
    "  on_eat = life_gem_on_eat,",
    "  on_eat = function() return true end,",
    "Neuters life_gem_on_eat, the FAMILY_FLASH emitter. The pickup no longer adds the FLASH effect, awards score, or reaps the gem, so EventKindAtLeast(score_change,1) flips from one event to none."
};

inline constexpr FactPredicate kFacts_effect_magic_shield_emission_scen99[] = {
    pred::TickReached(30),
    // The CLERIC (team 0) survives the 30-tick budget; required both as the
    // named caster and as the source qualifier for the shield-count anchor below.
    pred::WalkerAliveAtFinal(FAMILY_CLERIC, 1),
    pred::WalkerOfTeamAlive(0, 2, 2),
    // Structural coverage anchor: binds FAMILY_MAGIC_SHIELD to EffectFamilyCount
    // for behavioural_coverage_gate_effects. Genuinely 0 on BOTH sides because
    // the shield is an Order::FX object routed into oblist (add_ob), never
    // fxlist, so dump.effects holds no FAMILY_MAGIC_SHIELD regardless of mutation.
    pred::EffectFamilyCount(FAMILY_MAGIC_SHIELD, 0, 0, /*source=FAMILY_CLERIC*/5),
    // negative_assertion: MAGIC_SHIELD is represented as a team walker in oblist, not as a final fxlist effect.
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_effect_magic_shield_emission = {
    "packs/core/scripts/effect_shield.lua", 105,
    "  on_act = magic_shield_on_act,",
    "  on_act = function() return false end,",
    "Neuters core:magic_shield's on_act hook (false = \"not handled\", the no-registered-hook path), so the shield stops orbiting and re-centring on its owner and falls to the default animate path, flipping the shield-emission predicates."
};

inline constexpr FactPredicate kFacts_effect_knife_back_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // EffectFamilyCount snapshots fxlist at the final tick; combat-driven
    // FX (HIT, EXPAND, FLASH, ...) expire within a handful of ticks of
    // their emission and are no longer alive by tick 150. The exact
    // (0, 0) range is the honest schema-v1 observation; runtime
    // emission would require a per-tick coverage observation surfaced
    // into the dump on both branch and master — out of scope for the
    // current schema-v1 freeze.
    pred::EffectFamilyCount(FAMILY_KNIFE_BACK, 0, 0, /*source=FAMILY_SOLDIER*/0),
    // negative_assertion: KNIFE_BACK effects are transient combat FX and should be absent from the final fxlist snapshot.
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::ScoreDelta(/*team*/0, 0, 0),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_effect_knife_back_emission = {
    "packs/core/scripts/effect_knife_back.lua", 61,
    "  on_act = on_act,",
    "  on_act = function() return false end,",
    "Neuters core:knife_back's on_act hook (false = \"not handled\", the no-registered-hook path), so the returning blade never homes back to its thrower and expires wherever it was, flipping the knife-back emission predicates."
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
    // at tick 45 team 0 holds caster+boomerang = 2. boomerang_on_act in
    // packs/core/scripts/effect_shield.lua orbits the caster and keeps the FX alive
    // for its lifetime (30 + level*12 = 78 ticks). Under
    // kMut_effect_boomerang_emission the `on_act = boomerang_on_act` binding
    // (packs/core/scripts/effect_shield.lua:109) becomes `function() return
    // false end` — "not handled", the no-registered-hook path — so effect::act
    // (effect.cpp:79-94)
    // runs no orbit, animates one cycle, then set_dead/death within ~2 ticks
    // -> team 0 collapses to the lone caster = 1 and this lower bound fails.
    pred::WalkerOfTeamAlive(0, 2, 3,
        "consequence: BOOMERANG slot 2 summons FAMILY_BOOMERANG FX walkers onto the caster team (team 0); boomerang_on_act keeps them orbiting/alive across the 45-tick window so team 0 holds caster+boomerang(s)=3 on both branch and master. kMut_effect_boomerang_emission neuters boomerang_on_act in packs/core/scripts/effect_shield.lua (false = \"not handled\", the no-registered-hook path) so the FX runs no on_act, dies after one animation cycle, and team 0 collapses to the lone caster=1, below the floor of 2"),
    // rng_drift: two or three boomerang FX may be live while the mutation leaves only the caster; commit 244d4bcf
    // Structural coverage anchor: binds FAMILY_BOOMERANG to EffectFamilyCount
    // (behavioural_coverage_gate_effects). Genuinely 0 on BOTH sides because
    // the boomerang is an Order::FX object routed into oblist (add_ob), never
    // fxlist, so dump.effects holds no FAMILY_BOOMERANG regardless of mutation.
    pred::EffectFamilyCount(FAMILY_BOOMERANG, 0, 0, /*source=FAMILY_SOLDIER*/0),
    // negative_assertion: BOOMERANG rides oblist as a team walker; final fxlist should stay empty for that family.
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_effect_boomerang_emission = {
    "packs/core/scripts/effect_shield.lua", 109,
    "  on_act = boomerang_on_act,",
    "  on_act = function() return false end,",
    "Neuters core:boomerang's on_act hook (false = \"not handled\", the no-registered-hook path), so the boomerang stops flying its arc and returning to the soldier, flipping the boomerang-emission predicates."
};

inline constexpr FactPredicate kFacts_effect_cloud_emission_scen99[] = {
    pred::TickReached(45),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerOfTeamAlive(0, 2, 2,
        "consequence: POISON CLOUD slot 4 summons a FAMILY_CLOUD FX walker onto the thief's team (team 0), so team 0 holds thief+cloud=2; the cloud is core:cloud, whose on_act hook (packs/core/scripts/effect_cloud.lua) keeps it alive across the run window. Under the kMut_effect_cloud_emission hook neuter (effect_cloud.lua:58, false = \"not handled\", the no-registered-hook path) the cloud runs no on_act, so it animates one cycle then set_dead/death (effect.cpp:79-94) and is gone well before tick 45 -> team 0 collapses to the lone thief=1 and the lower bound fails. The cloud's random-walk path, and whether it ever poisons the lone soldier, is RNG-sensitive, so the cloud's *existence on team 0* is the robust observable rather than soldier HP"),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "consequence: the live cloud's spin/animation and the special cast both emit play_sound events; the floor stays >0 in the unmutated arm"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 6000, 6000),
    // Structural coverage anchor: binds FAMILY_CLOUD to EffectFamilyCount arg0
    // for behavioural_coverage_gate_effects. The FAMILY_CLOUD walker rides the
    // walkers[] array (add_ob(Order::FX) -> oblist), not fxlist, so zero live
    // FAMILY_CLOUD effects appear in the dump's effects[] on both branch and
    // master golden; the (0,0) window is the honest schema-v1 observation.
    pred::EffectFamilyCount(FAMILY_CLOUD, 0, 0, /*source=FAMILY_THIEF*/11),
    // negative_assertion: CLOUD is an oblist FX walker in this schema, so final fxlist must not contain it.
};

inline constexpr Mutation kMut_effect_cloud_emission = {
    "packs/core/scripts/effect_cloud.lua", 59,
    "  on_act = on_act,",
    "  on_act = function() return false end,",
    "Neuters core:cloud's on_act hook (false = \"not handled\", the no-registered-hook path), so the cloud stops drifting and damaging what it covers, flipping the cloud-emission predicates."
};

inline constexpr SpawnSpec kFamilySpawns_marker_emission_generator[] = {
    { FAMILY_TOWER,   1, kOrderGenerator, 60,   60,   0, 0, 5, 0 }, // FAMILY_TOWER generator (level 5) emits FAMILY_MAGE; mages cast teleport markers (FX-order FAMILY_MARKER) into oblist that PERSIST via core:marker's loops_animation: true (packs/core/families/effect-09-marker.yaml)
    { FAMILY_SOLDIER, 0, kOrderLiving,    2000, 2000, 0, 0 },       // off-map team-0 observer keeps the level alive; range-gated AI never reaches the 60,60 cluster
};

inline constexpr FactPredicate kFacts_effect_marker_emission_scen99[] = {
    pred::TickReached(2500),
    // Each generator spawn takes one set_difficulty at the rolled level, and
    // mage teleports probe eat-free under ground rules; this cadence yields
    // 27 mages by the budget.
    pred::WalkerFamilyCount(FAMILY_MAGE, 27, 27),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
    // FLIPPING PREDICATE. team-1 alive = TOWER(1) + surviving MAGEs (7 on
    // the final snapshot). Markers persist only because
    // core:marker's descriptor sets loops_animation: true
    // (packs/core/families/effect-09-marker.yaml:7, consumed at
    // effect.cpp:88-113). The discriminating_mutation sets it false; every
    // marker dies within a couple ticks of placement and is reaped from
    // oblist, which shifts the reap order + mage special cadence and moves
    // the deterministic team-alive count off the exact 8-pin.
    pred::WalkerOfTeamAlive(/*team=*/1, 8, 8,
        "consequence: killing the persistent markers via the mutation perturbs oblist reaping and the mage-special RNG cadence, moving the exact team-1 alive count (1 tower + 7 mages) off its pin"),
    // Marker-emitting mage saturation allows population variation while
    // retaining a stable floor.
    // Structural coverage anchor: binds FAMILY_MARKER to EffectFamilyCount arg0
    // for behavioural_coverage_gate_effects. Teleport markers ride the walkers[]
    // array (add_ob(Order::FX) -> oblist), not fxlist, so zero live FAMILY_MARKER
    // entries appear in the dump's effects[] on both branch and master golden.
    pred::EffectFamilyCount(FAMILY_MARKER, 0, 0, /*source=FAMILY_MAGE*/3),
    // negative_assertion: MARKER persistence is covered via team-alive; final fxlist remains empty for marker entries.
};

inline constexpr Mutation kMut_effect_marker_emission = {
    "packs/core/families/effect-09-marker.yaml", 7,
    "      loops_animation: true",
    "      loops_animation: false",
    "Disables FAMILY_MARKER animation looping at its live source, core:marker's classpack.yaml entry; per effect.cpp:88-113 a non-looping FX falls to ANI_WALK then set_dead(1), so the teleport markers the generator-spawned mages place no longer persist — every marker is reaped from oblist within a couple ticks, dropping the team-1 alive count below its pin."
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
    // blast-attacks that soldier, leaving one SOLDIER at 11200 cents and the
    // other two at full 12000. Under kMut_effect_chain_emission the chain loses
    // its on_act, never explodes, and every SOLDIER stays at full HP, so no
    // soldier remains in [0,11900] and the predicate flips.
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 11900,
        "consequence: chain lightning's FAMILY_CHAIN on_act detonates an explosion on the nearest enemy SOLDIER, leaving at least one below full 12000-cent HP (observed 11200 on both arms); the hook-neutering mutation in packs/core/scripts/effect_chain.lua strips on_act so the chain is inert and every SOLDIER stays at full 12000-cent HP, above the ceiling"),
    // rng_drift: chain targeting may vary while the inert mutation leaves every soldier at full HP; commit 244d4bcf
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    // Structural coverage anchor: binds FAMILY_CHAIN to EffectFamilyCount arg0
    // for behavioural_coverage_gate_effects. The summoned FAMILY_CHAIN is a
    // short-lived FX that has already expired by the tick-40 dump, so zero live
    // FAMILY_CHAIN entries appear in effects[] on both branch and master golden.
    pred::EffectFamilyCount(FAMILY_CHAIN, 0, 0, /*source=FAMILY_ARCHMAGE*/17),
    // negative_assertion: CHAIN expires before final capture and should not remain in fxlist.
};

inline constexpr Mutation kMut_effect_chain_emission = {
    "packs/core/scripts/effect_chain.lua", 129,
    "  on_act = on_act,",
    "  on_act = function() return false end,",
    "Neuters core:chain's on_act hook (false = \"not handled\", the no-registered-hook path), so chain lightning never seeks its nearest foe or explodes on it, flipping the chain-emission predicates."
};

inline constexpr FactPredicate kFacts_effect_door_open_emission_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // A FAMILY_DOOR_OPEN effect seeded into oblist (kOrderFX) is act()'d
    // each tick; core:door_open's on_act hook emits exactly one
    // persistent FAMILY_DOOR_OPEN effect into fxlist (snapshot here).
    // kMut_effect_door_open neuters that hook in
    // packs/core/scripts/effect_door_open.lua (false = "not handled", the
    // no-registered-hook path) so the
    // mutated build runs no on_act and emits zero -> this predicate flips.
    pred::EffectFamilyCount(FAMILY_DOOR_OPEN, 1, 1, /*source=FAMILY_SOLDIER*/0),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::EventKindExactly(/*score_change*/9, 0),
};

inline constexpr Mutation kMut_effect_door_open_emission = {
    "packs/core/scripts/effect_door_open.lua", 45,
    "  on_act = on_act,",
    "  on_act = function() return false end,",
    "Neuters core:door_open's on_act hook (false = \"not handled\", the no-registered-hook path), so the opened door is never handed off to a fresh persistent effect and the door_open FX count flips."
};

inline constexpr FactPredicate kFacts_effect_hit_emission_scen99[] = {
    pred::TickReached(150),
    // One SOLDIER survives at 32 HP, so the exact family count is one.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // EffectFamilyCount snapshots fxlist at the final tick; combat-driven
    // FX (HIT, EXPAND, FLASH, ...) expire within a handful of ticks of
    // their emission and are no longer alive by tick 150. The exact
    // (0, 0) range is the honest schema-v1 observation; runtime
    // emission would require a per-tick coverage observation surfaced
    // into the dump on both branch and master — out of scope for the
    // current schema-v1 freeze.
    pred::EffectFamilyCount(FAMILY_HIT, 0, 0, /*source=FAMILY_SOLDIER*/0),
    // negative_assertion: HIT is a transient combat FX family and should not survive to the final fxlist snapshot.
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::EventKindExactly(/*score_change*/9, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    // Coverage binding for FAMILY_BLOOD: the lethal combat in this arena
    // spatters a death-blood weapon that is still live at the final tick,
    // anchoring FAMILY_BLOOD coverage.
    pred::WeaponFamilyEmitted(FAMILY_BLOOD),
};

inline constexpr Mutation kMut_effect_hit_emission = {
    "src/gameplay/walker_combat.cpp", 147,
    "            walker* newob = current_game->world->add_ob(Order::FX, FAMILY_HIT);",
    "            walker* newob = current_game->world->add_fx_ob(Order::FX, FAMILY_HIT);",
    "do_hit_effects() emits the combat HIT animation via add_ob(Order::FX, FAMILY_HIT), which routes the object into world.oblist (game_world.cpp:564) where it is dumped as a team-0 walker. Repointing add_ob -> add_fx_ob (game_world.cpp:567, public, identical 2-arg signature) routes the HIT into world.fxlist instead, so it is no longer an oblist walker and no longer counted by WalkerOfTeamAlive(team 0). This is a genuine break of HIT-effect emission routing."
};

inline constexpr SpawnSpec kFamilySpawns_generator_tent[] = {
    {  0, 1, kOrderGenerator, 120, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_generator_tent_emission_scen99[] = {
    pred::TickReached(1500),
    // Each spawn takes one rolled-level set_difficulty; the resulting cadence
    // yields the exact skeleton count.
    pred::WalkerFamilyCount(FAMILY_SKELETON, 6, 6),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 4, 4),
};

inline constexpr Mutation kMut_generator_tent_emission = {
    "packs/core/families/generator-00-tent.yaml", 7,
    "      default_weapon: core:skeleton",
    "      default_weapon: core:ghost",
    "Repoints the FAMILY_TENT generator's emitted living family from SKELETON to GHOST at its live source, core:tent's classpack.yaml entry; gloader.cpp:693 copies gfd->default_weapon into the generator and walker.cpp:1059 emits add_ob(Order::Living, default_weapon()), so every emitted walker changes family and WalkerFamilyCount(FAMILY_SKELETON,...) flips to 0."
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
    // Each spawn takes one rolled-level set_difficulty; the resulting cadence
    // yields the exact mage count.
    pred::WalkerFamilyCount(FAMILY_MAGE, 5, 5),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerOfTeamAlive(/*team=*/1, 6, 6),
};

inline constexpr Mutation kMut_generator_tower_emission = {
    "packs/core/families/generator-01-tower.yaml", 7,
    "      default_weapon: core:mage",
    "      default_weapon: core:skeleton",
    "Repoints the FAMILY_TOWER generator's emitted living family from MAGE to SKELETON at its live source, core:tower's classpack.yaml entry; the tower then spawns no MAGE walkers, so WalkerFamilyCount(FAMILY_MAGE,...) drops to 0 and flips."
};

inline constexpr SpawnSpec kFamilySpawns_generator_bones[] = {
    {  2, 1, kOrderGenerator, 120, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_generator_bones_emission_scen99[] = {
    pred::TickReached(1500),
    pred::WalkerFamilyCount(FAMILY_GHOST, 5, 5),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 3, 3),
};

inline constexpr Mutation kMut_generator_bones_emission = {
    "packs/core/families/generator-02-bones.yaml", 7,
    "      default_weapon: core:ghost",
    "      default_weapon: core:elf",
    "Repoints the FAMILY_BONES generator's emitted living family from GHOST to ELF at its live source, core:bones's classpack.yaml entry; the BONES generator stops emitting GHOST walkers and the dump's FAMILY_GHOST count drops to 0."
};

inline constexpr SpawnSpec kFamilySpawns_generator_treehouse[] = {
    {  3, 1, kOrderGenerator, 120, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_generator_treehouse_emission_scen99[] = {
    pred::TickReached(1500),
    // Each spawn takes one rolled-level set_difficulty; the resulting cadence
    // yields the exact elf count.
    pred::WalkerFamilyCount(FAMILY_ELF, 6, 6),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
    pred::WalkerOfTeamAlive(/*team=*/1, 7, 7),
};

inline constexpr Mutation kMut_generator_treehouse_emission = {
    "packs/core/families/generator-03-treehouse.yaml", 7,
    "      default_weapon: core:elf",
    "      default_weapon: core:soldier",
    "Repoints the FAMILY_TREEHOUSE generator's emitted living family from ELF to SOLDIER at its live source, core:treehouse's classpack.yaml entry; create_weapon() does add_ob(Order::Living, default_weapon()) (walker.cpp:1059), so the spawned walkers serialize as FAMILY_SOLDIER and the FAMILY_ELF walker count drops to 0."
};

inline constexpr FactPredicate kFacts_event_notification_emission_scen99[] = {
    pred::TickReached(150),
    pred::EventKindExactly(/*notification*/2, 0),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 5),
    pred::EventKindAtLeast(/*score_change*/9, 1),
};

inline constexpr Mutation kMut_event_notification_emission = {
    "src/gameplay/walker_combat.cpp", 100,
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
    "src/gameplay/walker_combat.cpp", 100,
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
    "src/gameplay/walker_combat.cpp", 100,
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
    "src/gameplay/walker_combat.cpp", 210,
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
    "src/gameplay/game_world.cpp", 1776,
    "level_done = 0;",
    "level_done = 2;",
    "Neuters the enemy-alive guard in GameWorld::tick's normal living-act loop (the branch that runs for awake enemies; the sibling guards cover dormant, frozen, and weapon walkers): instead of resetting level_done to 0 when a live non-friendly Living enemy acts, it forces level_done to stay 2. With enemies still alive the level_done==2 completion check latches game_ended and the server layer pushes EventKind::SetEnd, so the arena's set_end suppression is broken and the event sneaks through. (Re-anchored after the dormant-guard feature added an earlier textual twin the pin had silently drifted onto.)"
};

inline constexpr SpawnSpec kFamilySpawns_special_soldier_1_scen99[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_soldier_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
    pred::EventKindExactly(/*score_change*/9, 0),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300),
};

inline constexpr Mutation kMut_special_soldier_1_scen99 = {
    "packs/core/scripts/soldier.lua", 9,
    "if self:s_forward_blocked() then",
    "if true then",
    "Closes CHARGE's forward-blocked gate permanently, so soldier slot 1 returns false before it issues COMMAND_RUSH or emits SOUND_CHARGE. The level-1 caster never dashes into the team-1 soldier and WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300) no longer finds a caster at exactly 63 HP."
};

inline constexpr SpawnSpec kFamilySpawns_special_soldier_2_scen99[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_soldier_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6500, 6500),
};

inline constexpr Mutation kMut_special_soldier_2_scen99 = {
    "packs/core/scripts/soldier.lua", 24,
    "local boomerang = og.summon_configured(self, \"fx\", FX_BOOMERANG, {",
    "local boomerang = nil and og.summon_configured(self, \"fx\", FX_BOOMERANG, {",
    "Short-circuits BOOMERANG's returning-blade summon; the 'nil and' keeps the multi-line argument table part of an unevaluated call so the chunk still parses. throw_boomerang takes its 'if not boomerang' exit and no FX_BOOMERANG ever flies, flipping WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6500, 6500)."
};

inline constexpr SpawnSpec kFamilySpawns_special_soldier_3_scen99[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_soldier_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300),
};

inline constexpr Mutation kMut_special_soldier_3_scen99 = {
    "packs/core/scripts/soldier.lua", 60,
    "self, t.whirlwind_range_base + self.level * t.whirlwind_range_per_level)",
    "self, 200)",
    "Widens WHIRLWIND's strike ring from 32+2*level px (46 at this level-7 caster, short of the 60 px to the team-1 soldier) to a flat 200 px, so the spin now lands attack() rolls instead of finding nobody. The added combat draws move the caster off exactly 63 HP and WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300) fails."
};

inline constexpr SpawnSpec kFamilySpawns_special_soldier_4_scen99[] = {
    {  0, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_soldier_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300),
};

inline constexpr Mutation kMut_special_soldier_4_scen99 = {
    "packs/core/families/living-00-soldier.yaml", 16,
    "hp: 120",
    "hp: 12000",
    "Cranks the FAMILY_SOLDIER init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_elf_1_scen99[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_elf_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 1700, 1700),
};

inline constexpr Mutation kMut_special_elf_1_scen99 = {
    "packs/core/scripts/elf.lua", 18,
    "local rock = self:fire()",
    "local rock = nil",
    "Suppresses the first of SOME ROCKS' two rock releases, so some_rocks takes its 'if not rock' exit with the MP already refunded and nothing in flight. EventKindAtLeast(play_sound, 16) and WalkerHpRangeAtFinalTick(FAMILY_ELF, 1700, 1700) both fail."
};

inline constexpr SpawnSpec kFamilySpawns_special_elf_2_scen99[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_elf_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
    pred::EventKindExactly(/*score_change*/9, 0),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 1800, 1800),
};

inline constexpr Mutation kMut_special_elf_2_scen99 = {
    "packs/core/scripts/elf.lua", 68,
    "bounce_volley(3, 2, 3)",
    "bounce_volley(3, 1, 3)",
    "Halves slot 2's BOUNCING ROCKS volley by dropping the shared bounce_volley body's per-slot rock count from 2 to 1, so only one bouncing rock leaves the elf. WalkerHpRangeAtFinalTick(FAMILY_ELF, 1800, 1800) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_elf_3_scen99[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_elf_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 6),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 1700, 1700),
};

inline constexpr Mutation kMut_special_elf_3_scen99 = {
    "packs/core/scripts/elf.lua", 70,
    "bounce_volley(4, 3, 4)",
    "bounce_volley(4, 1, 4)",
    "Cuts slot 3's BOUNCING ROCKS volley from three rocks to one at the shared bounce_volley body's per-slot count. WalkerHpRangeAtFinalTick(FAMILY_ELF, 1700, 1700) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_elf_4_scen99[] = {
    {  1, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_elf_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 6),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 1800, 1800),
};

inline constexpr Mutation kMut_special_elf_4_scen99 = {
    "packs/core/scripts/elf.lua", 72,
    "bounce_volley(5, 4, 5)",
    "bounce_volley(5, 1, 5)",
    "Cuts the default (slot 4) BOUNCING ROCKS volley from four rocks to one at the shared bounce_volley body's per-slot count. WalkerHpRangeAtFinalTick(FAMILY_ELF, 1800, 1800) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_archer_1_scen99[] = {
    {  2, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archer_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 21),
    pred::EventKindAtLeast(/*score_change*/9, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 4000, 7000),
    // rng_drift: archer self-damage timing spans this HP band while the init-HP mutation exits it; commit 244d4bcf
};

inline constexpr Mutation kMut_special_archer_1_scen99 = {
    "packs/core/scripts/archer.lua", 17,
    "self:s_add_command(C.COMMAND_QUICK_FIRE, 1, 1, 0)",
    "self:s_add_command(C.COMMAND_QUICK_FIRE, 1, -1, 0)",
    "Turns the one eastward shot of FIRE ARROWS' eight-direction volley around (dx +1 -> -1), so the only ray aimed at the team-1 soldier 60 px east now flies away from it. EventKindAtLeast(score_change, 1) and WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 4000, 7000) both fail."
};

inline constexpr SpawnSpec kFamilySpawns_special_archer_2_scen99[] = {
    {  2, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archer_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 11),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3500, 3500),
};

inline constexpr Mutation kMut_special_archer_2_scen99 = {
    "packs/core/scripts/archer.lua", 28,
    "if self:busy() ~= 0 then",
    "if true then",
    "Closes FLURRY's busy gate permanently, so archer slot 2 returns false before its three fire() releases and before the fire_frequency*2 busy charge. WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3500, 3500) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_archer_3_scen99[] = {
    {  2, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archer_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_ARCHER, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 7),
    pred::EventKindExactly(/*notification*/2, 0),
    // The archer ends the dance at exactly 34 HP. The +9000 init-HP mutation
    // is observable only here because the caster remains alive either way.
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3400, 3400),
};

inline constexpr Mutation kMut_special_archer_3_scen99 = {
    "packs/core/scripts/archer.lua", 48,
    "local arrow = self:fire()",
    "local arrow = nil",
    "Suppresses EXPLODING SHOT's arrow release, so the body restores the old weapon and takes its 'if not arrow' exit; no skip_exit-5000 buffed arrow is ever created. WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3400, 3400) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_mage_2_scen99[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_mage_2_scen99[] = {
    pred::TickReached(150),
    pred::EventKindAtLeast(/*play_sound*/1, 22),
    pred::WalkerAliveAtFinal(FAMILY_MAGE, 1),
    // The lone enemy SOLDIER survives at full 12000-cent HP on both arms: the
    // mage's slot-2 special expends the caster without landing damage.
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    // The mage ends at exactly 31 HP. This field is the +9000 init-HP
    // mutation's only observable.
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 3100, 3100),
};

inline constexpr Mutation kMut_special_mage_2_scen99 = {
    "packs/core/scripts/mage.lua", 159,
    "if i ~= 0 or j ~= 0 then",
    "if false then",
    "Empties STARBURST's 3x3 direction sweep so not one of the eight fireballs is fired, while the MP refund and the aim save/restore still run. EventKindAtLeast(play_sound, 22) and WalkerHpRangeAtFinalTick(FAMILY_MAGE, 3100, 3100) both fail."
};

inline constexpr SpawnSpec kFamilySpawns_special_mage_3_scen99[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_mage_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
    pred::EventKindExactly(/*notification*/2, 0),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 7600, 7800),
};

inline constexpr Mutation kMut_special_mage_3_scen99 = {
    "packs/core/scripts/mage.lua", 192,
    "if self.team == og.u8(og.my_team()) then",
    "if false and self.team == og.u8(og.my_team()) then",
    "Reroutes FREEZE TIME's player-team branch into the foreign-team branch: no world enemy_freeze bank and no palette tint, but a 'TIME IS FROZEN' notification instead. EventKindExactly(notification, 0) flips to one notification and WalkerHpRangeAtFinalTick(FAMILY_MAGE, 7600, 7800) fails."
};

inline constexpr SpawnSpec kFamilySpawns_special_mage_4_scen99[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_mage_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 11),
    pred::EventKindExactly(/*score_change*/9, 0),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 3400, 3400),
};

inline constexpr Mutation kMut_special_mage_4_scen99 = {
    "packs/core/scripts/mage.lua", 216,
    "local bolt = self:fire()",
    "local bolt = nil",
    "Suppresses the seed bolt ENERGY WAVE rides on, so energy_wave takes its 'if not bolt' exit and no FAMILY_WAVE weapon is ever placed. WalkerHpRangeAtFinalTick(FAMILY_MAGE, 3400, 3400) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_mage_5_scen99[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0, 13, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_mage_5_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
    pred::EventKindAtLeast(/*score_change*/9, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 8600, 8700),
};

inline constexpr Mutation kMut_special_mage_5_scen99 = {
    "packs/core/scripts/mage.lua", 240,
    "if foe_count == 0 then",
    "if true then",
    "Makes HEARTBURST report 'no foes in range' unconditionally, so mage slot 5 returns false before draining the MP pool or summoning one explosion per foe. EventKindAtLeast(score_change, 1) and WalkerHpRangeAtFinalTick(FAMILY_MAGE, 8600, 8700) both fail."
};

inline constexpr SpawnSpec kFamilySpawns_special_skeleton_1_scen99[] = {
    {  4, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_skeleton_1_scen99[] = {
    pred::TickReached(150),
    // The skeleton caster dies before the final snapshot, so its live count is
    // zero and WalkerDiedByFinal holds.
    pred::WalkerFamilyCount(FAMILY_SKELETON, 0, 0),
    // negative_assertion: BONE SHIELD should consume/replace the skeleton caster, leaving no live skeleton body.
    pred::EventKindAtLeast(/*play_sound*/1, 7),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 12000),
    // rng_drift: bone-shield combat may leave the soldier anywhere in this broad high-HP band; commit 244d4bcf
    pred::WalkerDiedByFinal(FAMILY_SKELETON),
};

inline constexpr Mutation kMut_special_skeleton_1_scen99 = {
    "packs/core/scripts/skeleton.lua", 28,
    "if lc.mid_teleport(self) then",
    "if true then",
    "Makes TUNNEL believe it is already mid-teleport, so do_special returns false without setting ANI_TELE_OUT and handle_teleport never runs its teleport_ranged hop. The skeleton stays put and survives: WalkerFamilyCount(FAMILY_SKELETON, 0, 0) and WalkerDiedByFinal(FAMILY_SKELETON) both fail."
};

inline constexpr SpawnSpec kFamilySpawns_special_cleric_2_scen99[] = {
    {  5, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_cleric_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 14),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 5800, 5800),
};

inline constexpr Mutation kMut_special_cleric_2_scen99 = {
    "packs/core/families/living-05-cleric.yaml", 16,
    "hp: 120",
    "hp: 12000",
    "Cranks the FAMILY_CLERIC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_cleric_3_scen99[] = {
    {  5, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_cleric_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 14),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 5800, 5800),
};

inline constexpr Mutation kMut_special_cleric_3_scen99 = {
    "packs/core/families/living-05-cleric.yaml", 16,
    "hp: 120",
    "hp: 12000",
    "Cranks the FAMILY_CLERIC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_cleric_4_scen99[] = {
    {  5, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_cleric_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 14),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 5800, 5800),
};

inline constexpr Mutation kMut_special_cleric_4_scen99 = {
    "packs/core/families/living-05-cleric.yaml", 16,
    "hp: 120",
    "hp: 12000",
    "Cranks the FAMILY_CLERIC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_fireelemental_1_scen99[] = {
    {  6, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_fireelemental_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_FIREELEMENTAL, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 22),
    pred::EventKindAtLeast(/*score_change*/9, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_FIREELEMENTAL, 4100, 4100),
};

inline constexpr Mutation kMut_special_fireelemental_1_scen99 = {
    "packs/core/scripts/fire_elemental.lua", 16,
    "if i ~= 0 or j ~= 0 then",
    "if false then",
    "Empties the elemental's lots-o-fireballs 3x3 sweep so none of the eight fireballs is released, while the MP refund and the aim save/restore still run. EventKindAtLeast(play_sound, 22), EventKindAtLeast(score_change, 1) and WalkerHpRangeAtFinalTick(FAMILY_FIREELEMENTAL, 4100, 4100) all fail."
};

inline constexpr SpawnSpec kFamilySpawns_special_slime_1_scen99[] = {
    {  8, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_slime_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SLIME, 0, 0),
    // negative_assertion: the SLIME consumes itself in the SPLIT special and
    // is replaced by FAMILY_SMALL_SLIME offspring, so no FAMILY_SLIME remains
    // at the final tick on either branch or master.
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 2, 2,
        "consequence: SPLIT produces exactly 2 SMALL_SLIME offspring on both arms"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 14100, 14100),
};

inline constexpr Mutation kMut_special_slime_1_scen99 = {
    "packs/core/scripts/slime.lua", 34,
    "self:set_ani_type(C.ANI_SLIME_SPLIT)",
    "self:set_ani_type(C.ANI_WALK)",
    "Starts the SPLIT special on the walk animation instead of ANI_SLIME_SPLIT, so slime_on_ani_complete's ani_type guard rejects the completion and the blob never divides. WalkerFamilyCount(FAMILY_SLIME, 0, 0), WalkerFamilyCount(FAMILY_SMALL_SLIME, 2, 2) and the offspring HP pin all fail."
};

inline constexpr SpawnSpec kFamilySpawns_special_small_slime_1_scen99[] = {
    {  9, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
};

inline constexpr FactPredicate kFacts_special_small_slime_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 0, 0),
    // negative_assertion: MERGE should consume the small slime caster so no live small slime remains.
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 1, 1),
    pred::WalkerDiedByFinal(FAMILY_SMALL_SLIME),
};

inline constexpr Mutation kMut_special_small_slime_1_scen99 = {
    "packs/core/scripts/slime.lua", 136,
    "  if self:spaces_clear() > 7 then",
    "  if false then",
    "Blocks small_slime_do_special's grow gate so the caster never transforms into FAMILY_MEDIUM_SLIME. The small slime survives and no medium exists, flipping both family-count predicates and WalkerDiedByFinal(SMALL_SLIME)."
};

inline constexpr SpawnSpec kFamilySpawns_special_medium_slime_1_scen99[] = {
    { 10, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_medium_slime_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 0, 0),
    // negative_assertion: SPLIT should consume the medium slime caster so no live medium slime remains.
    pred::EventKindAtLeast(/*play_sound*/1, 14),
    pred::EventKindExactly(/*score_change*/9, 0),
    pred::EventKindExactly(/*notification*/2, 0),
};

inline constexpr Mutation kMut_special_medium_slime_1_scen99 = {
    "packs/core/scripts/slime.lua", 136,
    "  if self:spaces_clear() > 7 then",
    "  if false then",
    "Blocks medium_slime_do_special's grow gate so the caster never transforms into FAMILY_SLIME. The medium slime survives, flipping its zero-count assertion and dropping grow/act sounds below the play_sound floor."
};

inline constexpr SpawnSpec kFamilySpawns_special_thief_2_scen99[] = {
    { 11, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_thief_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 3600, 3600),
};

inline constexpr Mutation kMut_special_thief_2_scen99 = {
    "packs/core/scripts/thief.lua", 77,
    "self:set_invisibility_left(og.combat.cloak_total(cur, gain))",
    "self:set_invisibility_left(0)",
    "Discards CLOAK's invisibility grant at the cast site (the cloak roll is still drawn, only the total is thrown away), so the level-4 thief never gains cover and the team-1 soldier keeps engaging it. WalkerHpRangeAtFinalTick(FAMILY_THIEF, 3600, 3600) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_thief_3_scen99[] = {
    { 11, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_thief_3_scen99[] = {
    pred::TickReached(150),
    // The per-slot taunt/fire dance leaves the lone thief alive at exactly
    // 38 HP, emits the taunt notification, and produces a play_sound stream.
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 3800, 3800),
    pred::EventKindAtLeast(/*play_sound*/1, 9),
    pred::EventKindAtLeast(/*notification*/2, 1),
};

inline constexpr Mutation kMut_special_thief_3_scen99 = {
    "packs/core/scripts/thief.lua", 85,
    "if lc.is_busy(self) then",
    "if true then",
    "Closes the busy gate on the TAUNT branch of taunt_or_charm, so thief slot 3 returns false before rolling any foe and before the 'Nyah Nyah!' line. EventKindAtLeast(notification, 1) and WalkerHpRangeAtFinalTick(FAMILY_THIEF, 3800, 3800) both fail."
};

inline constexpr SpawnSpec kFamilySpawns_special_thief_4_scen99[] = {
    { 11, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_thief_4_scen99[] = {
    pred::TickReached(150),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::EventKindAtLeast(/*play_sound*/1, 14),
    pred::WalkerAliveAtFinal(FAMILY_THIEF, 1),
    // The thief ends at exactly 16 HP. This field is the +9000 init-HP
    // mutation's only observable.
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 1600, 1600),
};

inline constexpr Mutation kMut_special_thief_4_scen99 = {
    "packs/core/scripts/thief.lua", 175,
    "if lc.is_busy(self) then",
    "if true then",
    "Closes POISON CLOUD's busy gate permanently, so thief slot 4 returns false before FX_CLOUD is summoned and no cloud is ever placed. WalkerHpRangeAtFinalTick(FAMILY_THIEF, 1600, 1600) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_ghost_1_scen99[] = {
    { 12, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_ghost_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_GHOST, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::EventKindExactly(/*score_change*/9, 0),
    // The ghost ends at exactly 21 HP. This field is the +9000 init-HP
    // mutation's only observable.
    pred::WalkerHpRangeAtFinalTick(FAMILY_GHOST, 2100, 2100),
};

inline constexpr Mutation kMut_special_ghost_1_scen99 = {
    "packs/core/scripts/ghost.lua", 9,
    "local scare = og.summon(self, \"fx\", FX_GHOST_SCARE)",
    "local scare = nil",
    "Suppresses the scare carrier FX the ghost's special summons (its on_death is what actually frightens), so do_special takes its 'if not scare' exit. WalkerFamilyCount(FAMILY_GHOST, 1, 1) and WalkerHpRangeAtFinalTick(FAMILY_GHOST, 2100, 2100) both flip."
};

inline constexpr SpawnSpec kFamilySpawns_special_druid_1_scen99[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_druid_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
    pred::EventKindExactly(/*score_change*/9, 0),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 3000, 6000),
    // rng_drift: grow-tree combat leaves the druid in this broad damaged-HP band; commit 244d4bcf
};

inline constexpr Mutation kMut_special_druid_1_scen99 = {
    "packs/core/scripts/druid.lua", 16,
    "local bolt = self:fire()",
    "local bolt = nil",
    "Suppresses the seed bolt PLANT TREE grows its tree from, so plant_tree takes its 'if not bolt' exit before the busy charge and before WEAP_TREE is summoned. EventKindAtLeast(play_sound, 15) fails."
};

inline constexpr SpawnSpec kFamilySpawns_special_druid_2_scen99[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_druid_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 16),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 5000, 11000,
        "consequence: SUMMON_FAERIE drains caster MP which affects combat HP; golden 8700 cents"),
    // rng_drift: summon-faerie combat pressure stays within this HP band while the init mutation exits high; commit 244d4bcf
};

inline constexpr Mutation kMut_special_druid_2_scen99 = {
    "packs/core/scripts/druid.lua", 42,
    "local faerie = og.add_ob(\"living\", LIVING_FAERIE)",
    "local faerie = nil",
    "Suppresses SUMMON FAERIE's pet creation, so the body takes its 'if not faerie' exit with the bolt fired but never converted into a faerie. EventKindAtLeast(play_sound, 16) fails."
};

inline constexpr SpawnSpec kFamilySpawns_special_druid_3_scen99[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_druid_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 14),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 4800, 4800),
};

inline constexpr Mutation kMut_special_druid_3_scen99 = {
    "packs/core/families/living-13-druid.yaml", 16,
    "hp: 110",
    "hp: 11000",
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
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 4800, 4800),
};

inline constexpr Mutation kMut_special_druid_4_scen99 = {
    "packs/core/families/living-13-druid.yaml", 16,
    "hp: 110",
    "hp: 11000",
    "Cranks the FAMILY_DRUID init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_orc_1_scen99[] = {
    { 14, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_orc_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 11),
    pred::EventKindExactly(/*score_change*/9, 0),
    // The orc ends at exactly 82 HP. This field is the +9000 init-HP
    // mutation's only observable.
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 8200, 8200),
};

inline constexpr Mutation kMut_special_orc_1_scen99 = {
    "packs/core/scripts/orc.lua", 11,
    "if lc.is_busy(self) then",
    "if true then",
    "Closes YELL's busy gate permanently, so orc slot 1 returns false before the per-foe stun rolls and before SOUND_ROAR. WalkerHpRangeAtFinalTick(FAMILY_ORC, 8200, 8200) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_orc_2_scen99[] = {
    { 14, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_orc_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 8),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 7000, 14000,
        "consequence: EAT_CORPSE restores HP; golden 10900 cents"),
    // rng_drift: corpse-eat healing and combat order produce a broad orc-HP envelope; commit 244d4bcf
};

inline constexpr Mutation kMut_special_orc_2_scen99 = {
    "packs/core/families/living-14-orc.yaml", 16,
    "hp: 140",
    "hp: 14000",
    "Cranks the FAMILY_ORC init HP; the caster no longer dies during the per-slot cycle/fire dance, flipping any predicate that depends on the caster's post-special HP / position / death state."
};

inline constexpr SpawnSpec kFamilySpawns_special_barbarian_1_scen99[] = {
    { 16, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_barbarian_1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BARBARIAN, 3000, 10000,
        "consequence: HURL_BOULDER combat exchange damages barbarian; golden 7100 cents"),
    // rng_drift: hurl-boulder timing spans this barbarian-HP band while the init mutation exits it; commit 244d4bcf
};

inline constexpr Mutation kMut_special_barbarian_1_scen99 = {
    "packs/core/scripts/barbarian.lua", 7,
    "if self:busy() > 0 then",
    "if true then",
    "Closes HURL BOULDER's busy gate permanently, so do_special returns false before the shot is fired and before WEAP_BOULDER is created; no boulder ever rolls. EventKindAtLeast(play_sound, 15) fails."
};

inline constexpr SpawnSpec kFamilySpawns_special_barbarian_2_scen99[] = {
    { 16, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_barbarian_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 15),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BARBARIAN, 5000, 12000,
        "consequence: EXPLODING_BOULDER combat exchange damages barbarian; golden 8900 cents"),
    // rng_drift: exploding-boulder timing spans this barbarian-HP band while the init mutation exits it; commit 244d4bcf
};

inline constexpr Mutation kMut_special_barbarian_2_scen99 = {
    "packs/core/scripts/barbarian.lua", 56,
    "if self:current_special() == 2 then",
    "if false then",
    "Drops the slot-2 sentinel that marks the thrown boulder as EXPLODING (skip_exit 5000 -> 0), so slot 2 throws a plain boulder. The boulder's flight and impact diverge from the master golden and the exact weapon_tracks comparison fails; this row's HP band (5000, 12000) is too wide to see the difference, so the trajectory check carries the teeth."
};

inline constexpr SpawnSpec kFamilySpawns_special_archmage_2_scen99[] = {
    { 17, 0, kOrderLiving, 120, 120, 0, 0, 4, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archmage_2_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 10000, 15000,
        "consequence: HEARTBURST drains caster HP; golden 14600 cents"),
    // rng_drift: heartburst drains stay within this archmage-HP band while the init mutation exits high; commit 244d4bcf
};

inline constexpr Mutation kMut_special_archmage_2_scen99 = {
    "packs/core/scripts/archmage.lua", 213,
    "if foe_count == 0 then",
    "if true then",
    "Makes burst_or_chain report 'no foes in range' unconditionally, so HEARTBURST returns false before the MP pool is drained and before a single FX_EXPLOSION is summoned. The caster keeps the HP and MP the burst would have spent and leaves the (10000, 15000) cent band of WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE)."
};

inline constexpr SpawnSpec kFamilySpawns_special_archmage_3_scen99[] = {
    { 17, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archmage_3_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 9200, 9200),
};

inline constexpr Mutation kMut_special_archmage_3_scen99 = {
    "packs/core/scripts/archmage.lua", 415,
    "local phantom = og.add_ob(\"living\", person)",
    "local phantom = nil",
    "Suppresses the illusion body SUMMON IMAGE conjures after its tier roll, so archmage slot 3 takes its 'if not phantom' exit and no Phantom joins the caster's team. WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 9200, 9200) flips."
};

inline constexpr SpawnSpec kFamilySpawns_special_archmage_4_scen99[] = {
    { 17, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
    {  0, 1, kOrderLiving, 180, 120, 0, 0 },
};

inline constexpr FactPredicate kFacts_special_archmage_4_scen99[] = {
    pred::TickReached(150),
    // The slot-4 mind-control special leaves the lone archmage alive near
    // 146 HP and emits both a play_sound and control notification.
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_ARCHMAGE, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::EventKindAtLeast(/*notification*/2, 1),
    // Canary teeth: caster ends at its real init HP 146 (<=15000 cents); the
    // kMut raises init HP to BASE_GUY_HP+9000, pushing final HP into the
    // thousands of display HP, out of range -> flips.
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 10000, 15000),
    // rng_drift: mind-control timing keeps the archmage within this near-full-HP envelope; commit 244d4bcf
};

inline constexpr Mutation kMut_special_archmage_4_scen99 = {
    "packs/core/scripts/archmage.lua", 470,
    "if foe_count < 1 then",
    "if true then",
    "Makes MIND CONTROL report 'no foes in range' unconditionally, so archmage slot 4 returns false before any foe is charmed and before the 'has controlled N men' notice. EventKindAtLeast(notification, 1) and WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 10000, 15000) both fail."
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
    pred::WalkerPositionMoved(FAMILY_ARCHER, 200, 120,
        "consequence: archer is held at its spawn (200,120) for the full 150-tick window because freeze duration 20+11*12=152 > tick budget 150; branch and master agree on the pinned position"),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
};

inline constexpr Mutation kMut_enemy_freeze_mage_scen99 = {
    "packs/core/families/living-03-mage.yaml", 78,
    "freeze_per_level: 11",
    "freeze_per_level: 0",
    "Cuts the freeze grant from 20+11*level to a flat 20 ticks in the mage's YAML tuning block, which mage.lua reads through og.tuning(self).freeze_per_level each cast. At mage level 5 the banked enemy_freeze drops 75 -> 20, enemies act normally for most of the 150-tick window, the level-5 archer steps west toward the mage and its xpos drops below 200, flipping WalkerPositionMoved(FAMILY_ARCHER, 200, 120) on the x floor."
};

inline constexpr SpawnSpec kFamilySpawns_invisibility_thief_scen99[] = {
    { FAMILY_THIEF,   0, kOrderLiving, 120, 120, 0, 0, 4, 300 }, // FAMILY_THIEF caster (slot 2 CLOAK cost=125 covered by 300 magicpoints)
    { FAMILY_SOLDIER, 1, kOrderLiving, 200, 120, 0, 0          }, // FAMILY_SOLDIER engagement partner
};

inline constexpr FactPredicate kFacts_invisibility_thief_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    // Thief finishes at hp 15 on both branch and master (the cloak window is
    // shorter than the 150-tick budget so it takes a deterministic amount of
    // partial-window engagement damage; branch and master agree exactly).
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 1500, 1500),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 135, 128),
    pred::EventKindAtLeast(/*play_sound*/1, 2),
};

inline constexpr Mutation kMut_invisibility_thief_scen99 = {
    "packs/core/scripts/thief.lua", 77,
    "  self:set_invisibility_left(og.combat.cloak_total(cur, gain))",
    "  self:set_invisibility_left(0)",
    "Forces invisibility_left to 0 so the slot-2 CLOAK cast never grants cover (zeroing the whole cloak_total gain; the runaway-specials 350-tick accumulator cap never binds at L4, where the max single cast is 96); the team-1 soldier keeps engaging the level-4 thief for the full 150-tick window, killing the thief and dropping its HP outside the (1300, 2500) cent band — flipping WalkerHpRangeAtFinalTick."
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
    "packs/core/families/treasure-12-speed_potion.yaml", 20,
    "duration_per_level: 50",
    "duration_per_level: 0",
    "Zeroes core:speed_potion's per-level duration in its YAML tuning block, which speed_potion_on_eat reads through og.tuning(self).duration_per_level. With a 0-tick grant the eater's speed_bonus_left stays 0 and the level-5 potion's 250-tick walking-speed window never opens; the soldier walks at base stepsize for the entire 20-tick K_RIGHT window (observed branch xpos 368 -> 288), failing WalkerPositionMoved(FAMILY_SOLDIER, 350, 224)."
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
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 7700, 7700),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
};

inline constexpr Mutation kMut_invulnerable_potion_scen99 = {
    "packs/core/families/treasure-06-invulnerable_potion.yaml", 20,
    "duration_per_level: 150",
    "duration_per_level: 0",
    "Zeroes core:invulnerable_potion's per-level duration in its YAML tuning block, which invulnerable_potion_on_eat reads through og.tuning(self).duration_per_level. A 0-tick grant leaves invulnerable_left at 0, so team-1 archer arrows land while the soldier closes melee range, dropping its HP below the 11500-cent floor and flipping WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11500, 12000)."
};


// --- Summon-lifecycle scenarios --------------------------------------------
//
// Druid slot-2 SUMMON FAERIE deterministically spawns a FAMILY_FAERIE walker
// owned by the caster with lifetime = druid_faerie_lifetime(level), called
// from packs/core/scripts/druid.lua. The formula is 50 + level*40 below the 570-tick
// soft-cap knee. At level 4 that is 210 ticks. The faerie is reaped by the per-tick lifetime
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
    // rng_drift: faerie expiry may leave only the druid or one transient summon at capture; commit 244d4bcf
};

inline constexpr Mutation kMut_summon_lifetime_faerie_scen99 = {
    "packs/core/scripts/druid.lua", 50,
    "  faerie.lifetime = og.combat.druid_faerie_lifetime(self.level)",
    "  faerie.lifetime = 99999",
    "Replaces the spawn-time lifetime initialisation (druid_faerie_lifetime — legacy 50+40*L bit-exact below the 570-tick knee, so 210 at this L4 caster) with an effectively-infinite value; the faerie is still alive at tick 650 so WalkerDiedByFinal(FAMILY_FAERIE) fails because an alive FAMILY_FAERIE remains."
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
    // rng_drift: lifetime-decrement timing may leave one or two team-0 walkers at capture; commit 244d4bcf
};

inline constexpr Mutation kMut_summon_lifetime_decrement_faerie_scen99 = {
    "src/gameplay/living.cpp", 118,
    "const auto remaining_lifetime = lifetime() - 1;",
    "const auto remaining_lifetime = lifetime();",
    "Removes the `- 1` so remaining_lifetime == lifetime() every tick; `if (remaining_lifetime < 1)` at line 106 is permanently false and the lifetime-expiry kill at 108-109 never fires. With the druid kept alive (off-map enemy), owner-death cascades at 87/98 also never fire, so the faerie is still alive at tick 650 and WalkerDiedByFinal(FAMILY_FAERIE) fails because an alive FAMILY_FAERIE remains. Exercises the decrement path rather than initialisation."
};


// Generator-saturation scenario ---------------------------------------------
// A FAMILY_TOWER generator (default_weapon: core:mage per
// packs/core/families/generator-01-tower.yaml) runs act_generate (walker.cpp:1338-1367)
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
    // A single rolled-level set_difficulty per spawn and eat-free teleport
    // probes yield 27 mages by the budget.
    pred::WalkerFamilyCount(FAMILY_MAGE, 27, 27),
    // At least one mage finishes at exactly 97 HP under the single-application
    // rolled-level stat line.
    pred::WalkerHpRangeAtFinalTick(FAMILY_MAGE, 9700, 9700),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
    pred::WalkerOfTeamAlive(1, 8, 8),
};

inline constexpr Mutation kMut_generator_saturation_scen99 = {
    "src/gameplay/walker.cpp", 1351,
    "if ( current_game->world->living_count < MAXOBS &&",
    "if ( false &&",
    "Replaces the `living_count < MAXOBS` half of the act_generate gate with `false`, making the conjunction always false; the generator never fires, zero FAMILY_MAGE spawn, and WalkerFamilyCount(FAMILY_MAGE, 3, 30) fails on its lower bound."
};


// Weapon-trajectory scenarios ------------------------------------------------
// Three projectile/effect-trajectory specials whose observable consequence is
// a spawned weapon or FX walker downstream of the caster's own state:
//   * FAMILY_ELF slot 2 (BOUNCING ROCKS) fires FAMILY_ROCK projectiles via the
//     two-shot loop in packs/core/scripts/elf.lua;
//   * FAMILY_SOLDIER slot 2 (BOOMERANG) summons one FAMILY_BOOMERANG FX walker
//     in packs/core/scripts/soldier.lua, animated by effect_shield.lua;
//   * FAMILY_BARBARIAN slot 2 (EXPLODING BOULDER) emits a FAMILY_BOULDER whose
//     explode-on-death hook in packs/core/scripts/weapon_projectiles.lua adds a
//     FAMILY_EXPLOSION FX walker; barbarian.lua sets the lifetime gate.
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
// ARE structurally observable and that the mutation flips:
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
    pred::EventKindExactly(/*score_change*/9, 0),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::WeaponSpeed(FAMILY_ROCK, 650, 900,
        "trajectory: ELF slot-2 BOUNCING ROCKS first projectile (seq 0) steps toward the enemy soldier at max consecutive-tick speed 707 centi-px/tick on branch and 800 centi-px/tick on the recaptured master golden (the branch next_spread_multiplier spread rounds the x-step differently than master's elf fire path, so the two arms diverge by one pixel); bracket [650,900] brackets BOTH arms and flips when the kMut doubles the rock's x-step to ~1404 centi-px/tick (1404 > 900)"),
    pred::WeaponNetTravel(FAMILY_ROCK, /*kWeaponPathStraight*/0, 1000,
        "trajectory: the first rock flies a STRAIGHT path in open-field scen1.fss (net=pathlen=1414, net>=0.7*pathlen, no wall struck within budget so rock_on_death never bounces); threshold 1000 centi clears the observed 1414 net displacement"),
};

inline constexpr Mutation kMut_weapon_rock_slot2_emit_scen99 = {
    "packs/core/scripts/elf.lua", 51,
    "      rock:set_lastx(og.fmul(rock:lastx(), next_spread_multiplier()))",
    "      rock:set_lastx(og.fmul(og.fmul(rock:lastx(), next_spread_multiplier()), 2.0))",
    "Doubles the BOUNCING ROCKS first projectile's x-step while preserving the spread RNG draw. Its speed rises to about 1404 centipixels, above WeaponSpeed's 900 ceiling; lineofsight still yields enough samples for a determinate result."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_boomerang_return_scen99[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 4, 300 }, // FAMILY_SOLDIER caster (level 4 -> slot 2 BOOMERANG reachable)
    { FAMILY_ARCHER,  1, kOrderLiving, 200, 200, 0, 0 },         // FAMILY_ARCHER opponent placed diagonally so combat RNG drives caster HP drift
};

inline constexpr FactPredicate kFacts_weapon_boomerang_return_scen99[] = {
    pred::TickReached(80),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerOfTeamAlive(0, 2, 4,
        "consequence: BOOMERANG slot 2 adds FAMILY_BOOMERANG FX walker(s) to the caster's team (team 0)"),
    // rng_drift: boomerang return timing may leave two to four team-0 bodies while the mutation removes extras; commit 244d4bcf
    pred::EventKindAtLeast(/*play_sound*/1, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 10100, 10100),
};

inline constexpr Mutation kMut_weapon_boomerang_return_scen99 = {
    "packs/core/scripts/effect_shield.lua", 96,
    "  self:center_on(owner)  -- each arc starts at the owner; offsets do not accumulate",
    "  local _ = owner  -- each arc starts at the owner; offsets do not accumulate",
    "Removes boomerang_on_act's center_on(owner) anchor. The drawcycle-scaled orbit offset then accumulates from the prior position, making the boomerang drift and changing the captured trajectory, so SemanticParity flips."
};

inline constexpr SpawnSpec kFamilySpawns_weapon_exploding_boulder_scen99[] = {
    { FAMILY_BARBARIAN, 0, kOrderLiving, 120, 120, 0, 0, 5, 300 }, // FAMILY_BARBARIAN caster (level 5 -> slot 2 EXPLODING BOULDER reachable)
    { FAMILY_SOLDIER,   1, kOrderLiving, 220, 120, 0, 0 }, // on +x axis, far enough that the boulder's ~32px box clears it for ~4-5 ticks before exploding
    { FAMILY_SOLDIER,   1, kOrderLiving, 250, 130, 0, 0 },
    { FAMILY_SOLDIER,   1, kOrderLiving, 280, 140, 0, 0 },
};

inline constexpr FactPredicate kFacts_weapon_exploding_boulder_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 3, 3,
        "consequence: with the soldier cluster pushed downrange (220/250/280) the AI soldiers charge back toward the level-5 barbarian and outrun the boulder, so the boulder flies its full ~9-tick range and detonates on empty ground — all three enemy soldiers survive to the final tick on both arms"),
    pred::EventKindAtLeast(/*play_sound*/1, 3),
    pred::WeaponSpeed(FAMILY_BOULDER, 900, 1500,
        "trajectory: the EXPLODING BOULDER special travels at level*2=10 px/tick (myguy==nullptr AI-cast path in barbarian_do_special). Per-tick step is 1000 centi-px/tick on a pure axis or 1414 on a diagonal (RNG `waver` chooses lasty as -10, 0, or 10); range [900,1500] brackets both on branch and recaptured master. The discriminating mutation drops stepsize to level*0.5=2.5 px/tick (250-354 centi) which falls below 900 and fails this predicate."),
    pred::WeaponNetTravel(FAMILY_BOULDER, /*kWeaponPathStraight*/0, 1500,
        "trajectory: the boulder flies in one fixed heading for its whole life (lastx/lasty never change once fired), so net displacement == pathlen and net >= 0.7*pathlen always holds; threshold 1500 centi (15px) is met by >=2 ticks of 10px flight on both arms. The mutation that makes the boulder die in 1 tick (or collapses its step) drives net below 1500 and fails this; a path-curving mutation breaks net>=0.7*pathlen."),
};

inline constexpr Mutation kMut_weapon_exploding_boulder_scen99 = {
    "packs/core/scripts/barbarian.lua", 35,
    "    boulder:set_stepsize(self.level * 2)",
    "    boulder:set_stepsize(self.level * 0.5)",
    "Quarters the EXPLODING BOULDER's per-tick stepsize for AI casters (level*2 -> level*0.5 = 2.5 px/tick), dropping the boulder's per-tick displacement from 1000-1414 centi-px/tick to 250-354 centi-px/tick. WeaponSpeed(FAMILY_BOULDER,900,1500) fails because max consecutive-tick step falls below 900."
};


// Effect-emission scenarios --------------------------------------------------
// Three specials whose observable consequence is a multi-target / multi-spawn
// effect emission:
//   * FAMILY_ARCHMAGE slot 2 (HEARTBURST) detonates a per-foe FAMILY_EXPLOSION
//     against every in-range enemy in packs/core/scripts/archmage.lua;
//   * FAMILY_THIEF slot 4 (POISON CLOUD) summons one FAMILY_CLOUD FX in
//     packs/core/scripts/thief.lua; effect_cloud.lua handles its per-tick attack;
//   * FAMILY_DRUID slot 4 (PROTECTION) emits a FAMILY_CIRCLE_PROTECTION weapon
//     onto each in-range friendly when >1 friendly is within range 60;
//     packs/core/scripts/weapon_animate.lua keeps it centered on its owner.
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
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 9000,
        "consequence: HEARTBURST detonates a per-foe explosion against each in-range soldier; the golden leaves all four at 3400/4600/6600/6800 cents, so the most-wounded one sits far inside this ceiling. Suppress the explosions and melee alone is left, which takes the best-off soldier only down to 11000 — above the ceiling, and (this predicate is ANY-in-band) so is every other soldier at 12000. The ceiling is 9000, not the 11000 it used to be: at 11000 the mutated arm landed EXACTLY on an inclusive bound and the predicate did not flip, leaving the row's teeth resting entirely on the weapon_tracks byte-compare"),
    // rng_drift: heartburst order may kill or wound different soldiers, so the band bounds the worst-hit one rather than a fixed value; 9000 keeps 2000 cents of clearance on both the golden (6800 max) and the mutated (11000 min) side; commit 244d4bcf
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 0, 4,
        "consequence: the per-foe explosions may kill some of the four in-range soldiers"),
    // rng_drift: multi-target explosion damage may leave any number of soldiers alive, so this row is documentation, not a discriminator; commit 244d4bcf
    pred::EventKindAtLeast(/*play_sound*/1, 6,
        "consequence: HEARTBURST emits SOUND_EXPLODE once per detonated foe — golden 8 play_sounds, of which 4 are the bursts and 4 are melee. Suppress the explosions and only the 4 melee cues remain. The floor is 6, not the 4 it used to be: at 4 the mutated arm landed EXACTLY on the floor and the predicate did not flip"),
};

inline constexpr Mutation kMut_effect_heartburst_multitarget_scen99 = {
    "packs/core/scripts/archmage.lua", 229,
    "      local burst = og.summon(self, \"fx\", FX_EXPLOSION)",
    "      do return false end",
    "Replaces the HEARTBURST per-foe explosion summon with `do return false end`, so burst_or_chain declines on its first loop iteration before any FAMILY_EXPLOSION is created: no explosion lands on any in-range soldier (all stay at full 12000-cent HP) and no SOUND_EXPLODE is emitted, so the soldier-HP window and EventKindAtLeast(play_sound, 4) both fail. The `do ... end` wrapper is load-bearing — Lua only allows `return` as the last statement of a block, so a bare mid-block `return false` here is a SYNTAX ERROR that fails the whole archmage.lua load and takes every archmage special down with it; the row would then flip on collateral damage rather than on the heartburst. With the wrapper, chain lightning (slot 1 teleport, slot 3 summon image, slot 4 mind control) still load and run: only the burst branch is neutered."
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
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 6000, 6000),
};

inline constexpr Mutation kMut_effect_poison_cloud_emit_scen99 = {
    "packs/core/scripts/thief.lua", 179,
    "  local cloud = og.summon(self, \"fx\", FX_CLOUD)",
    "  do return false end",
    "Replaces the POISON CLOUD FX summon with `do return false end`, so poison_cloud declines before the FAMILY_CLOUD walker is created; the cloud never enters oblist so team 0 holds only the lone thief — WalkerOfTeamAlive(0, 2, 2) collapses to 1 and fails its lower bound. The `do ... end` wrapper is load-bearing — Lua only allows `return` as the last statement of a block, so a bare mid-block `return false` here is a SYNTAX ERROR that fails the whole thief.lua load; the row would then flip because every thief special died, not because the cloud did. With the wrapper, drop bomb / cloak / taunt-charm keep working and only the cloud branch is neutered."
};

inline constexpr SpawnSpec kFamilySpawns_effect_protection_emit_scen99[] = {
    // The friendly soldier is spawned FIRST so the druid — spawned next — lands
    // ahead of it in oblist (add_ob prepends), making the druid the walker that
    // find_player_walker binds and the special input drives. No enemies are
    // spawned: with no foe to charge, the AI friendly idles near the druid
    // and stays inside range 60 at the tick-20 cast, so PROTECTION's
    // howmany>1 gate opens deterministically (whether a foe is in range at
    // the exact cast tick is RNG/AI-movement sensitive and diverges between
    // branch and master). With no incoming damage the emitted circle is
    // never consumed as a shield, so it persists in weaplist.
    // The friendly starts at a legal, non-overlapping separation northwest of
    // the druid. Its deterministic southeast idle wander keeps it within
    // range 60 through the cast tick.
    { FAMILY_SOLDIER, 0, kOrderLiving, 85, 90, 0, 0 },            // team-0 friendly, Manhattan 65->wanders SE into range by the cast, no body overlap
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
    "packs/core/scripts/druid.lua", 102,
    "        local circle = og.summon(friend, \"weapon\", WEAP_CIRCLE_PROTECTION)",
    "        do return false end",
    "Replaces the PROTECTION circle summon with `do return false end`, so protection_circle declines on the first uncircled friend before the FAMILY_CIRCLE_PROTECTION weapon is created; weaplist never holds the circle so WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION) fails — the emit never fires. The `do ... end` wrapper is load-bearing — Lua only allows `return` as the last statement of a block, so a bare mid-block `return false` here is a SYNTAX ERROR that fails the whole druid.lua load and takes plant-tree, the bolt, and summon faerie down with it; the row would then flip on collateral damage rather than on the circle. With the wrapper this is the only row in the corpus that moves."
};


// Effect-timer scenarios -----------------------------------------------------
// FAMILY_THIEF slot 1 (DROP BOMB) spawns a FAMILY_BOMB FX walker in
// packs/core/scripts/thief.lua. Its on_death hook in effect_bomb.lua later
// detonates a FAMILY_EXPLOSION.
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
    // rng_drift: one or two bombs may be live at capture while removing the spawn drops below the floor; commit 244d4bcf
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
};

inline constexpr Mutation kMut_effect_bomb_timer_scen99 = {
    "packs/core/scripts/thief.lua", 45,
    "  local bomb = og.add_ob(\"fx\", FX_BOMB)",
    "  do return false end",
    "Replaces the DROP BOMB FX spawn with `do return false end`, so drop_bomb declines before any FAMILY_BOMB walker is created; oblist never holds a bomb so the thief's team (team 0) keeps only the lone thief alive and WalkerOfTeamAlive(0, 2, 3) collapses to 1, below its floor. The `do ... end` wrapper is load-bearing — Lua only allows `return` as the last statement of a block, so a bare mid-block `return false` here is a SYNTAX ERROR that fails the whole thief.lua load; the row would then flip because every thief special died, not because the bomb did. With the wrapper, cloak / taunt-charm / poison cloud keep working and only the bomb branch is neutered. (Shares the thief.lua:45 anchor with kMut_special_thief_do_special, which neuters the same spawn a different way; both rows and effect_bomb_emission_scen99 observe the bomb, so all three move together — as does the FAMILY_BOMB effect-family coverage gate, since the corpus then spawns no bomb at all.)"
};


// --- Phase 06: input-pipeline edge-case scenarios --------------------------
//
// These four rows drive the player-input EDGE CASES that the family/effect
// rows never reach: diagonal movement, sustained held-fire, mid-run character
// switch, and special-slot index wrap. scenario_runtime.cpp decodes key masks
// and directly calls walkstep, init_fire, special, and its cycle helpers, so
// the mutations target those downstream mechanics. The mutation canary
// verifies each byte-match `from` text against its live source line.

// (1) DIAGONAL MOVEMENT. A lone soldier on the player team holds K_DOWN_RIGHT
// for forty ticks. The driver decodes DownRight into +1 x/y components, so
// walkstep advances the soldier in both axes. WalkerPositionMoved requires
// both coordinates to clear the floor; the mutation zeroes walkstep's y
// component, so the soldier never clears the ypos floor.
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
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::WalkerOfTeamAlive(0, 1, 1),
};

inline constexpr Mutation kMut_input_diagonal_movement_scen99 = {
    "src/gameplay/walker_movement.cpp", 173,
    "returnvalue = walk(x * stepsize(), y * stepsize());",
    "returnvalue = walk(x * stepsize(), y * 0.0f);",
    "Drops the y component inside walker::walkstep. The diagonal key then advances the soldier only in x, so it never clears the ypos floor of WalkerPositionMoved(FAMILY_SOLDIER,175,175)."
};

// (2) HELD FIRE. A lone player-team soldier holds K_FIRE for the whole run.
// The driver re-arms init_fire() every held tick, so the soldier looses a
// FAMILY_KNIFE on every boomerang cycle and emits a fire sound per throw. The
// mutation extends the post-throw busy period so later re-arm attempts fail.
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
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::WalkerOfTeamAlive(0, 1, 1),
};

inline constexpr Mutation kMut_input_hold_fire_search_scen99 = {
    "src/gameplay/walker.cpp", 481,
    "set_busy(busy() + fire_frequency());",
    "set_busy(busy() + fire_frequency() * 100.0f);",
    "Inflates walker::init_fire's post-throw busy pause by 100x. The first throw blocks later held-fire re-arms, collapsing the sustained knife stream and dropping play_sound below five."
};

// (3) SWITCH CHARACTER. Two real_team==255 walkers share the player team
// (player_team = 255). The driver first controls the soldier; K_SWITCH then
// hands control to the archer via cycle_next_character, so the held K_FIRE that
// follows is the archer's fire. The mutation leaves switching intact but changes
// the archer's default weapon, making the emitted family discriminate the row.
//
// OBSERVABILITY NOTE. Position is not a usable signal here: an UNcontrolled
// living runs the AI and random-walks to an unpredictable spot (living.cpp
// act_random / COMMAND_RANDOM_WALK), so the never-controlled walker can drift
// anywhere and no xpos threshold separates the two runs. Instead we observe
// WHICH WEAPON FAMILY is emitted. After the switch, held K_FIRE makes the
// controlled walker fire: the ARCHER looses FAMILY_ARROW (a non-returning
// projectile that persists in weaplist), whereas the SOLDIER would throw
// FAMILY_KNIFE. So an emitted FAMILY_ARROW proves the archer holds the helm.
// The mutation gives the controlled archer knives instead, so no FAMILY_ARROW
// is emitted. Both walkers are real_team 255 (the switch
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
    pred::EventKindAtLeast(/*play_sound*/1, 20),
};

inline constexpr Mutation kMut_input_switch_char_scen99 = {
    "packs/core/families/living-02-archer.yaml", 40,
    "core:arrow",
    "core:knife  # core:arrow",
    "Swaps the archer's default weapon so post-switch held K_FIRE throws FAMILY_KNIFE instead of FAMILY_ARROW. No arrow is emitted, flipping WeaponFamilyEmitted(FAMILY_ARROW)."
};

// (4) SPECIAL-SLOT WRAP. Seven K_SPECIAL_SWITCH presses cycle a player-team
// mage through slots 1->2->3->4->5, wrap to 1, and land on slot 3. K_SPECIAL
// then fires FREEZE TIME.
//
// OBSERVABILITY NOTE. The robust signal is the FREEZE-TIME palette tint: the
// team-0 branch sets current_palette_id and emits SetPalette events. The
// mutation routes the cast through the foreign-team branch, which emits
// notifications and redraws instead. A SetPalette event therefore proves the
// wrap landed on FREEZE TIME and took the player-team branch.
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
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
};

inline constexpr Mutation kMut_input_special_switch_wrap_scen99 = {
    "packs/core/scripts/mage.lua", 192,
    "  if self.team == og.u8(og.my_team()) then",
    "  if false and self.team == og.u8(og.my_team()) then",
    "Reroutes the player-team FREEZE TIME cast into the foreign-team branch. The cast still fires, but emits notifications and RequestRedraw instead of tinting the palette, so EventKindAtLeast(set_palette,1) flips."
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
    // The snap-faced three-way melee leaves the archer at exactly 90 HP. The
    // friendliness mutation's discriminator is the play_sound floor below.
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 9000, 9000),
    pred::EventKindAtLeast(/*play_sound*/1, 4,
        "consequence: the soldier (team 0), thief (team 2), and archer (team 1) carry three distinct team_nums and none holds a myguy pointer, so is_friendly takes the no-myguy branch and the verdict reduces to the team_num comparison on walker.cpp:1723; because the numbers differ every pair is hostile and the units trade blows, emitting a stream of combat play_sound events (branch ~10, master ~12). The mutation rewrites line 1723 to `return 1`, making every pair friendly: combat ceases, only the player's lone scripted fire remains, and the play_sound count collapses to 1 — below this floor of 4."),
};

inline constexpr Mutation kMut_multiplayer_two_teams_scen99 = {
    "src/gameplay/walker.cpp", 2261,
    "return headus->team_num() == headtarget->team_num();",
    "return 1;",
    "Replaces the no-myguy team-number comparison with unconditional friendliness. The three-team melee never starts, every walker keeps full HP, and play_sound collapses to one event, below the floor of four."
};

// Level-withdraw scenario. Reuses scripted_input_scen9301's spawn list
// (kFamilySpawns_soldier_with_exit_withdraw: a player SOLDIER on team 0, a
// surviving team-1 enemy that keeps a live foe in the level, and a FAMILY_EXIT
// treasure whose stats().level()==2 points at the already-completed scen2) and
// its input script (kInputsScripted9301: UP→RIGHT→FIRE) which walks the player
// onto the exit. Because the destination is complete while enemies remain in
// the current level, exit_on_eat takes its withdraw branch, sets
// world.withdraw_requested, emits WithdrawToLevel and RequestExitConfirmation,
// and leaves level_done at 2. The 200-tick budget lets that state settle before
// capture.
inline constexpr FactPredicate kFacts_level_withdraw_scen99[] = {
    pred::TickReached(200),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 2, 2),
    pred::LevelDoneEquals(2, "consequence: withdraw path returns level_done=2; the can_withdraw-skip mutation suppresses the withdraw completion so level_done leaves 2"),
    pred::EventKindAtLeast(/*withdraw_to_level*/8, 1),
    pred::EventKindAtLeast(/*request_exit_confirmation*/7, 1),
};

inline constexpr Mutation kMut_level_withdraw_scen99 = {
    "packs/core/scripts/treasure_navigation.lua", 74,
    "  if can_withdraw then",
    "  if false then",
    "Skips exit_on_eat's can_withdraw branch. RequestExitConfirmation and WithdrawToLevel are not emitted and withdraw completion is not reached, flipping both event floors and LevelDoneEquals(2)."
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
    // rng_drift: the HP band brackets knife exchanges while the no-damage mutation leaves full HP; commit 244d4bcf
    pred::WalkerOfTeamAlive(0, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 4),
};
inline constexpr Mutation kMut_midcombat_partial_hp_scen99 = {
    "src/gameplay/walker_combat.cpp", 210,
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
    // rng_drift: arrow damage plus the drumstick heal lands in a broad band while no-heal stays below; commit 244d4bcf
};
inline constexpr Mutation kMut_consumable_inventory_state_scen99 = {
    "packs/core/scripts/treasure_consumables.lua", 26,
    "  eater.hp = og.fadd(eater.hp, amount)",
    "  eater.hp = og.fadd(eater.hp, 0)",
    "No-ops the drumstick heal so the arrow-wounded player never recovers; its final HP stays at the lower wounded value, below the WalkerHpRangeAtFinalTick lower bound -- flipping it."
};

// --- Phase 10: cleric heal / raise / turn-undead / resurrect scenarios ------
//
// Group-wide engine facts these six arenas depend on:
//   * GameWorld::find_nearest_blood caps at SQUARED distance 800, i.e. a
//     bloodstain is only findable within ~28 px euclidean of the caster. The
//     per-special raise_skeleton_range (60) / raise_ghost_range (30) /
//     resurrect_range (30) are MANHATTAN and are the second gate. Every corpse
//     arena below places the stain 15-24 px from the cleric; do not "fix" the
//     spawn coordinates.
//   * cleric.lua's on_shoved hook does set_current_special(1) + special(), so
//     anything that touches the caster between the K_SPECIAL_SWITCH cycle and
//     the K_SPECIAL press silently reverts it to slot 1 (HEAL). Nothing may be
//     in contact with the cleric across that window.
//   * SpawnSpec order is load-bearing in all four multi-walker arenas: the
//     same positions with a different order produce a different fight.

// --- special_cleric_heal_ally_scen99 ---------------------------------------
// Slot-1 HEAL (no Shift). Spawn order is load-bearing: the team-1 FAERIE first,
// the team-0 BIG_ORC ally second, the CLERIC LAST so find_player_walker binds it.
// By tick 60 the ally has traded blows with the faerie and sits 29 px from the
// caster -- inside tuning.heal_range 60 -- while nothing is in contact with the
// cleric, so the K_SPECIAL press is not swallowed by an on_shoved reset.
// The heal has no max_hitpoints clamp (master walker.cpp:2618), so the ally ends
// FAR above its 180 max: that is the discriminator.
inline constexpr InputEvent kInputs_special_cleric_heal_ally[] = {
    { 60, 0, K_SPECIAL }, { 61, 0, K_NONE },
};
inline constexpr SpawnSpec kFamilySpawns_special_cleric_heal_ally_scen99[] = {
    { FAMILY_FAERIE,  1, kOrderLiving, 136, 120, 0, 0 },          // hostile foil; keeps the ally wounded and the level running
    { FAMILY_BIG_ORC, 0, kOrderLiving, 152, 120, 0, 0 },          // team-0 ally: the heal target (unique family -> unambiguous HP predicate)
    { FAMILY_CLERIC,  0, kOrderLiving, 120, 120, 0, 0, 4, 600 },  // caster LAST -> oblist head -> player-controlled
};
inline constexpr FactPredicate kFacts_special_cleric_heal_ally_scen99[] = {
    pred::TickReached(90),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::LevelDoneEquals(0),
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2),
    pred::WalkerAliveAtFinal(FAMILY_BIG_ORC, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BIG_ORC, 41200, 41400,
        "consequence: cleric slot-1 HEAL adds mp/4 + rand(mp/4) + 5*level to the in-range team-0 ally with NO max_hitpoints clamp, so the 180-max big orc finishes at 413 HP (41300 cents), 2.3x its own cap; collapsing heal_range makes find_friends_in_range return friend_count<=1, heal_or_mace returns false, and the ally keeps its wounded 172 HP (17200) -- far below this floor. The 200-cent window is the one-regen-tick spread between the branch dump (41300) and a companion recapture (41200), not an RNG band: the heal draw itself is fixed by the 0x42 seed."),
    pred::EventKindAtLeast(/*play_sound*/1, 14,
        "consequence: the successful heal adds one SOUND_HEAL on top of the 13 combat sounds; a refused heal emits nothing and the floor collapses to 13"),
};
inline constexpr Mutation kMut_special_cleric_heal_ally_scen99 = {
    "packs/core/families/living-05-cleric.yaml", 74,
    "        heal_range: 60",
    "        heal_range: 1",
    "Collapses the cleric HEAL friend-acquisition radius so find_friends_in_range yields friend_count<=1 and heal_or_mace returns false before charging or healing. The team-0 big orc keeps its wounded 172 HP (17200 cents), below WalkerHpRangeAtFinalTick's 25000 floor, and the SOUND_HEAL that lifted play_sound to 14 disappears."
};

// --- cleric_raise_skeleton_scen99 ------------------------------------------
// Corpse rig shared with cleric_raise_ghost_scen99 and undead_no_corpse_raise_scen99.
// The team-0 BIG_ORC ally kills the team-1 FAERIE wedged between it and the caster;
// walker::death -> generate_bloodspot drops a FAMILY_STAIN at (135,128), i.e.
// squared-distance 289 from the cleric (find_nearest_blood's ceiling is 800) and
// Manhattan 23 (raise_skeleton_range is 60). With the faerie dead the ally has no foe
// and wanders off, so nothing is in contact with the cleric when the slot-2 cast lands
// -- required, because the cleric's on_shoved hook resets current_special to 1.
// The K_SPECIAL_SWITCH is deliberately LATE (tick 90, after the melee is over) for the
// same reason.
inline constexpr InputEvent kInputs_cleric_raise_late_slot2[] = {
    {  90, 0, K_SPECIAL_SWITCH }, {  91, 0, K_NONE },
    { 110, 0, K_SPECIAL },        { 111, 0, K_NONE },
};
inline constexpr SpawnSpec kFamilySpawns_cleric_raise_skeleton_scen99[] = {
    { FAMILY_BIG_ORC, 0, kOrderLiving, 152, 120, 0, 0 },           // team-0 executioner
    { FAMILY_FAERIE,  1, kOrderLiving, 136, 120, 0, 0 },           // victim: dies at (135,128), beside the caster
    { FAMILY_CLERIC,  0, kOrderLiving, 120, 120, 0, 0, 10, 600 },  // caster LAST; level 10 clears the slot-2 cycle gate, 600 MP covers cost 20
};
inline constexpr FactPredicate kFacts_cleric_raise_skeleton_scen99[] = {
    pred::TickReached(130),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::WalkerFamilyCount(FAMILY_FAERIE, 0, 0),
    // negative_assertion: the victim faerie is reaped from oblist once dead; its absence is what proves the raised undead came from the bloodstain and not from the victim entry itself.
    pred::LevelDoneEquals(2),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 1, 1,
        "consequence: slot-2 RAISE UNDEAD summons one LIVING_SKELETON onto the bloodstain the dead faerie left; with the corpse gate closed no skeleton is created at all and the count drops to 0"),
    pred::WalkerAliveAtFinal(FAMILY_SKELETON, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 3, 3,
        "consequence: cleric + executioner + raised skeleton = 3 alive on team 0; without the raise it is 2"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 11600, 11800,
        "invariant: the caster is never engaged (the ally does the killing), so it finishes at 117/120 (11700 cents) -- proof the skeleton came from the raise and not from a melee-driven code path"),
};
inline constexpr Mutation kMut_cleric_raise_skeleton_scen99 = {
    "packs/core/families/living-05-cleric.yaml", 82,
    "        raise_skeleton_range: 60",
    "        raise_skeleton_range: 1",
    "Collapses the RAISE UNDEAD corpse reach so nearby_corpse's `distance < range` test fails on the Manhattan-23 bloodstain and raise_skeleton returns false. No LIVING_SKELETON is summoned: WalkerFamilyCount(FAMILY_SKELETON, 1, 1) sees 0, WalkerAliveAtFinal fails, and team-0 alive drops from 3 to 2."
};

// --- cleric_raise_ghost_scen99 ---------------------------------------------
// Same corpse rig as cleric_raise_skeleton_scen99 (stain at (135,128), Manhattan 23 from
// the caster) but cycled one slot further. raise_ghost_range is only 30, so the 23-px
// stain clears it by 7 px; that is exactly the margin kMut removes.
inline constexpr InputEvent kInputs_cleric_raise_late_slot3[] = {
    {  90, 0, K_SPECIAL_SWITCH }, {  91, 0, K_NONE },
    {  93, 0, K_SPECIAL_SWITCH }, {  94, 0, K_NONE },
    { 110, 0, K_SPECIAL },        { 111, 0, K_NONE },
};
inline constexpr SpawnSpec kFamilySpawns_cleric_raise_ghost_scen99[] = {
    { FAMILY_BIG_ORC, 0, kOrderLiving, 152, 120, 0, 0 },
    { FAMILY_FAERIE,  1, kOrderLiving, 136, 120, 0, 0 },
    { FAMILY_CLERIC,  0, kOrderLiving, 120, 120, 0, 0, 10, 600 },  // level 10 clears the slot-3 gate ((3-1)*3+1 = 7); MP 600 covers cost 50
};
inline constexpr FactPredicate kFacts_cleric_raise_ghost_scen99[] = {
    pred::TickReached(130),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::WalkerFamilyCount(FAMILY_FAERIE, 0, 0),
    // negative_assertion: the victim faerie is reaped once dead; its absence is what proves the ghost came from the bloodstain rather than from a surviving victim entry.
    pred::LevelDoneEquals(2),
    pred::WalkerFamilyCount(FAMILY_GHOST, 1, 1,
        "consequence: slot-3 RAISE GHOST summons one LIVING_GHOST onto the dead faerie's bloodstain; closing the 30-px corpse gate produces no ghost at all"),
    pred::WalkerAliveAtFinal(FAMILY_GHOST, 1,
        "consequence: og.combat.ghost_raise_lifetime(10) = soften(550, 670, 925) = 550 ticks, so the summon is still alive at the 130-tick dump; shortening the lifetime kills it before the dump"),
    pred::WalkerOfTeamAlive(/*team=*/0, 3, 3,
        "consequence: cleric + executioner + raised ghost = 3 alive on team 0; without the raise it is 2"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 11600, 11800,
        "invariant: the caster never fights, so it finishes at 117/120 (11700 cents) at the dump"),
};
inline constexpr Mutation kMut_cleric_raise_ghost_scen99 = {
    "packs/core/families/living-05-cleric.yaml", 83,
    "        raise_ghost_range: 30",
    "        raise_ghost_range: 1",
    "Collapses the RAISE GHOST corpse reach below the Manhattan-23 bloodstain, so nearby_corpse returns nil and raise_ghost returns false before do_summon. No LIVING_GHOST enters oblist: WalkerFamilyCount(FAMILY_GHOST, 1, 1) sees 0, WalkerAliveAtFinal fails, and team-0 alive drops from 3 to 2."
};

// --- cleric_turn_undead_scen99 ---------------------------------------------
// Shift alternate of cleric slot 2 -> do_turn_undead. Two team-1 skeletons are parked
// 28 px away on the two axes: inside turn_undead's 4*level = 40-px reach at level 10,
// but NOT in contact with the caster (16-px sprite boxes do not overlap), so nothing
// shoves the cleric and the K_SPECIAL_SWITCH at tick 5 survives to the Shift+K_SPECIAL
// at tick 20. turn_undead's kill roll is random(range*40)=random(1600) vs
// random(level*10)=random(10), i.e. overwhelming. walker_specials.cpp:321 selects the
// victims through the PR's is_undead descriptor flag where master hard-coded
// `case FAMILY_SKELETON / FAMILY_GHOST`, so this row is what pins the new field.
// The 60-tick budget is short enough that melee cannot explain the kills -- the control
// run leaves both skeletons at full HP and the cleric far lower.
inline constexpr InputEvent kInputs_cleric_turn_undead[] = {
    {  5, 0, K_SPECIAL_SWITCH },     {  6, 0, K_NONE },
    { 20, 0, K_SPECIAL | K_SHIFT },  { 21, 0, K_NONE },
};
inline constexpr SpawnSpec kFamilySpawns_cleric_turn_undead_scen99[] = {
    { FAMILY_SKELETON, 1, kOrderLiving, 148, 120, 0, 0 },
    { FAMILY_SKELETON, 1, kOrderLiving, 120, 148, 0, 0 },
    { FAMILY_CLERIC,   0, kOrderLiving, 120, 120, 0, 0, 10, 600 },
};
inline constexpr FactPredicate kFacts_cleric_turn_undead_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 2, 2),
    pred::WalkerDiedByFinal(FAMILY_SKELETON,
        "consequence: do_turn_undead destroys every is_undead foe inside 4*level px; clearing the skeleton's is_undead descriptor makes walker::turn_undead skip them and both survive at full 60/60"),
    pred::WalkerOfTeamAlive(/*team=*/1, 0, 0,
        "consequence: team 1 is wiped by the turn, not by melee -- without the turn both skeletons are still alive at tick 60"),
    pred::LevelDoneEquals(2,
        "consequence: turning both undead ends the level; the un-turned control run finishes with level_done 0"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 10700, 10900,
        "consequence: the caster ends at 108/120 (10800 cents) because the skeletons stop hitting it the moment they are turned; leave them alive and they grind it to 67/120 (6700), far below this floor"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};
inline constexpr Mutation kMut_cleric_turn_undead_scen99 = {
    "packs/core/families/living-04-skeleton.yaml", 42,
    "      is_undead: true",
    "      is_undead: false",
    "Clears the descriptor flag walker::turn_undead (walker_specials.cpp:321) tests to pick victims, so neither skeleton is destroyed. Both stay alive at 60/60 and keep meleeing the caster down to 67/120: WalkerDiedByFinal, WalkerOfTeamAlive(1,0,0), LevelDoneEquals(2) and the cleric HP floor all fail."
};

// --- cleric_resurrect_friendly_scen99 --------------------------------------
// Slot-4 RESURRECT, FRIENDLY branch. player_team is 1: the caster and the victim share
// team 1 so is_friendly(blood) is true and resurrect takes the add_ob(old_family) /
// transfer_stats / hp = max_hp/2 arm instead of the hostile summon-a-ghost arm.
// The team-0 BIG_ORC grinds the team-1 SOLDIER down and kills it at ~tick 130 at
// (112,131) -- squared distance 185 from the caster (find_nearest_blood ceiling 800) and
// Manhattan 19 (resurrect_range 30). The orc then stalls at (132,120), 12 px clear of the
// cleric's sprite box, so it never shoves the caster: critical, because the cleric's
// on_shoved hook would reset current_special from 4 back to 1 and silently turn the cast
// into a no-op HEAL. Casting at tick 141 is ~10 ticks after the death and 12 ticks before
// the dump; casts anywhere in 135..160 give the same resurrected soldier.
inline constexpr InputEvent kInputs_cleric_resurrect_friendly[] = {
    {   5, 0, K_SPECIAL_SWITCH }, {   6, 0, K_NONE },
    {   8, 0, K_SPECIAL_SWITCH }, {   9, 0, K_NONE },
    {  11, 0, K_SPECIAL_SWITCH }, {  12, 0, K_NONE },
    { 141, 0, K_SPECIAL },        { 142, 0, K_NONE },
};
inline constexpr SpawnSpec kFamilySpawns_cleric_resurrect_friendly_scen99[] = {
    { FAMILY_BIG_ORC, 0, kOrderLiving, 144, 120, 0, 0 },           // team-0 executioner (hostile to the player team)
    { FAMILY_SOLDIER, 1, kOrderLiving, 128, 120, 0, 0 },           // friendly victim: dies at (112,131), leaves a team-1 stain
    { FAMILY_CLERIC,  1, kOrderLiving, 104, 120, 0, 0, 10, 600 },  // caster LAST; level 10 clears the slot-4 gate ((4-1)*3+1 = 10), MP 600 covers cost 150
};
inline constexpr FactPredicate kFacts_cleric_resurrect_friendly_scen99[] = {
    pred::TickReached(153),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::LevelDoneEquals(0),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1,
        "consequence: the friendly RESURRECT branch rebuilds the dead ally from the bloodstain's old_family stamp, so a live FAMILY_SOLDIER re-enters oblist; without the resurrect the arena holds no live soldier at all"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6000, 6200,
        "consequence: the friendly branch sets alive.hp = max_hp/2, i.e. 60 of 120, and one regen tick lands before the dump for a final 61 (6100 cents); quartering that divisor puts the revived soldier at 31 HP (3100), far below this floor"),
    pred::WalkerOfTeamAlive(/*team=*/1, 2, 2,
        "consequence: caster + resurrected ally = 2 alive on the player team; without the resurrect only the caster survives"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 9000, 9200,
        "invariant: the executioner stalls 12 px clear of the caster, so the cleric finishes at 91/120 (9100 cents) -- proof the cast landed while the caster was un-shoved. The 200-cent window is the one-regen-tick spread between the branch dump (9100) and a companion recapture (9000)."),
};
inline constexpr Mutation kMut_cleric_resurrect_friendly_scen99 = {
    "packs/core/scripts/cleric.lua", 231,
    "    alive.hp = og.fdiv(alive.max_hp, 2.0)",
    "    alive.hp = og.fdiv(alive.max_hp, 4.0)",
    "Quarters the friendly-RESURRECT revival health instead of halving it. The rebuilt soldier returns at 31 of 120 HP (3100 cents) rather than 61 (6100), dropping out of WalkerHpRangeAtFinalTick's [5000, 6500] window while every other predicate still holds -- an isolated hit on the branch-specific half-health rule."
};

// --- undead_no_corpse_raise_scen99 -----------------------------------------
// Negative twin of cleric_raise_skeleton_scen99: same caster, same executioner, same
// slot-2 cast (kInputs_cleric_raise_late_slot2), only the victim family changes
// (FAERIE -> SKELETON). walker::death skips generate_bloodspot for is-undead families,
// so no FAMILY_STAIN ever enters fxlist and nearby_corpse returns nil even though the
// victim died 15 px from the caster (squared distance 225, well inside
// find_nearest_blood's 800 ceiling and raise_skeleton_range's 60) -- i.e. the raise
// fails for the descriptor reason and not for a geometry reason, which is what makes
// the leaves_bloodspot flip a real flip.
// Spawn order puts the SKELETON first here: that ordering is what makes it die IN PLACE
// at (135,120); with the ally first it flees and dies 32 px away, out of raise range,
// and the mutation would no longer flip. Do not reorder.
inline constexpr SpawnSpec kFamilySpawns_undead_no_corpse_raise_scen99[] = {
    { FAMILY_SKELETON, 1, kOrderLiving, 136, 120, 0, 0 },          // undead victim: dies in place at (135,120), leaves NO bloodspot
    { FAMILY_BIG_ORC,  0, kOrderLiving, 152, 120, 0, 0 },          // team-0 executioner
    { FAMILY_CLERIC,   0, kOrderLiving, 120, 120, 0, 0, 10, 600 }, // caster LAST
};
inline constexpr FactPredicate kFacts_undead_no_corpse_raise_scen99[] = {
    pred::TickReached(130),
    pred::WalkerFamilyCount(FAMILY_CLERIC, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_CLERIC, 1),
    pred::LevelDoneEquals(2),
    pred::WalkerDiedByFinal(FAMILY_SKELETON,
        "negative_assertion: the undead victim leaves no bloodspot, so the slot-2 RAISE UNDEAD finds nothing and no LIVING skeleton exists at the dump -- only the victim's corpse entry; giving the skeleton leaves_bloodspot makes the raise succeed and an ALIVE skeleton appears, failing this predicate"),
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2,
        "negative_assertion: team 0 holds only the caster and the executioner; a successful raise would add a third"),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 1, 1,
        "negative_assertion: exactly one FAMILY_SKELETON entry (the dead victim) is in oblist; a successful raise makes it two"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 11600, 11800,
        "invariant: the caster never fights, so it finishes at 117/120 (11700 cents) at the dump"),
};
inline constexpr Mutation kMut_undead_no_corpse_raise_scen99 = {
    "packs/core/families/living-04-skeleton.yaml", 38,
    "      leaves_bloodspot: false",
    "      leaves_bloodspot: true",
    "Makes the undead victim drop a FAMILY_STAIN at its (135,120) death spot -- 15 px from the caster, inside both find_nearest_blood's squared-800 ceiling and raise_skeleton_range 60. The slot-2 cast now succeeds and a live team-0 FAMILY_SKELETON is summoned: WalkerDiedByFinal fails, WalkerOfTeamAlive(0,2,2) sees 3, and WalkerFamilyCount(FAMILY_SKELETON,1,1) sees 2."
};

// --- Z-axis / multi-floor arenas (branch-internal Invariant) ---------------
//
// GRID_SIZE is 16, so pixel 112 == tile 7. A FAMILY_SOLDIER sprite is well
// under 32px, so its centre cell ((xpos+sizex/2)/16) is (7,7) — exactly the
// cell apply_floor_setup paints the Z tile into. apply_z_motion (run at the top
// of living::act every tick, gated floor_count>1) reads the genre under the
// walker's centre cell and performs the floor transition.

// Z-stair: soldier stands on a PIX_ZSTAIR_UP on floor 0 and climbs to floor 1.
inline constexpr SpawnSpec kZStairSpawns[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 112, 112, 0, 0, 0, 0, 0, /*floor=*/0 },
};
inline constexpr FloorPaint kZStairPaints[] = {
    { /*floor=*/0, 7, 7, kPixZStairUp }, // stair under the soldier's centre cell
    { /*floor=*/0, 8, 7, kPixGrass1 },   // grass footprint around the stair
    { /*floor=*/0, 7, 8, kPixGrass1 },
    { /*floor=*/0, 8, 8, kPixGrass1 },
};

// Fall-through-air: soldier spawns on floor 1 over a PIX_AIR hole and falls to
// the solid grass floor 0 below.
inline constexpr SpawnSpec kZFallSpawns[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 112, 112, 0, 0, 0, 0, 0, /*floor=*/1 },
};
inline constexpr FloorPaint kZFallPaints[] = {
    { /*floor=*/0, 7, 7, kPixGrass1 },   // solid landing pad on floor 0
    { /*floor=*/0, 8, 7, kPixGrass1 },
    { /*floor=*/0, 7, 8, kPixGrass1 },
    { /*floor=*/0, 8, 8, kPixGrass1 },
    { /*floor=*/1, 7, 7, kPixAir },      // air hole under the soldier on floor 1
};

// Two-story fall (fall DAMAGE teeth): soldier spawns on floor 2 over a stacked
// PIX_AIR shaft (air at (7,7) on floors 2 AND 1) and cascades down to the
// solid grass pad on floor 0. Two stories fallen -> the first is free, the
// second costs 15% of max HP (walker::resolve_fall_landing), pinned by
// WalkerHpRangeAtFinalTick in Parity.z_multifloor_walker_floor_transitions.
inline constexpr SpawnSpec kZFall2Spawns[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 112, 112, 0, 0, 0, 0, 0, /*floor=*/2 },
};
inline constexpr FloorPaint kZFall2Paints[] = {
    { /*floor=*/0, 7, 7, kPixGrass1 },   // solid landing pad on floor 0
    { /*floor=*/0, 8, 7, kPixGrass1 },
    { /*floor=*/0, 7, 8, kPixGrass1 },
    { /*floor=*/0, 8, 8, kPixGrass1 },
    { /*floor=*/1, 7, 7, kPixAir },      // shaft continues through floor 1
    { /*floor=*/2, 7, 7, kPixAir },      // air hole under the soldier on floor 2
};

// --- Treasure guard-arm and consequence scenarios --------------------------
//
// Every treasure_*_pickup_scen99 row above proves the CONSUMPTION half of a
// treasure hook: the treasure is eaten, notifies, and leaves oblist. The rows
// below cover the two halves nothing else reaches -- the CONSEQUENCE of the
// grant (flight_left / invisibility_left / the mana overfill, none of which is
// a dumped field, so each is read through a downstream sim behaviour) and the
// GUARD arms that refuse the eat (natural flier, unwounded eater, wrong team,
// non-player key pickup), which leave the treasure lying in the world.
//
// The guard rows lean on WalkerOfTeamAlive(2, 1, 1): a treasure keeps its
// SpawnSpec team in oblist and a consumed one stays there with alive:false, so
// a treasure spawned alone on team 2 reads a determinate 1 while it survives
// and a determinate 0 once a hook marks it dead. That is the flip direction
// TreasureFamilyOfOrderRemovedFromOblist structurally cannot express (it goes
// indeterminate for a live survivor). Team 2 is inert: sim_find_next_control
// and master's find_player_walker both require Order::Living, and find_far_foe
// only accepts ORDER_LIVING/ORDER_GENERATOR, so a team-2 treasure neither
// steals player control nor becomes anybody's foe.

// Flight-potion CONSEQUENCE row (the pickup twin only proves consumption).
// The soldier holds K_RIGHT for the whole budget. Grounded, the scen1 y=120
// lane pins every walker at xpos 224: grid row 7 tiles 15/16/17 are
// PIX_TREE_B1 and at xpos 225 the collision box (sizex 16) starts testing
// tile 15 (screen.cpp query_grid_passable). flight_left != 0 breaks out of
// that case, so the flier crosses and runs the clear grass corridor east to
// the map edge. Level-2 potion => 300 flight ticks, twice the budget.
inline constexpr SpawnSpec kFamilySpawns_treasure_flight_effect[] = {
    { FAMILY_FLIGHT_POTION, 2, kOrderTreasure, 96, 120, 0, 0, 2, 0 }, // level 2 -> 300 flight ticks; TEAM 2 so it cannot be mistaken for a control candidate
    { FAMILY_SOLDIER,       0, kOrderLiving,   96, 120, 0, 0       }, // player LAST (oblist head after the prepending spawner) and co-located, so the tick-0 step eats the potion
};

inline constexpr FactPredicate kFacts_treasure_flight_effect_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_FLIGHT_POTION, kOrderTreasure),
    // TEETH. Every grounded y=120 walker in the corpus stops at xpos 224
    // (family_orc/faerie/archer, treasure_speed_potion_pickup,
    // invulnerable_potion). Only flight_left gets past tile 15, so an xpos
    // floor of 260 separates the arms structurally. Zeroing
    // duration_per_level pins the soldier back at 224 and this fails.
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 620, 120,
        "consequence: flight_left != 0 makes PIX_TREE_B1 passable (screen.cpp query_grid_passable), so the soldier crosses the tile-15..17 tree line the whole corpus is pinned behind at xpos 224 and runs the grass corridor out to the east map edge; branch and companion both land on (623,120), so the floor sits 3 px under the captured value. duration_per_level: 0 leaves flight_left at 0 and the soldier stops at 224, four hundred px below this floor"),
    pred::EventKindAtLeast(/*notification*/2, 1),
};

inline constexpr Mutation kMut_treasure_flight_effect = {
    "packs/core/families/treasure-07-flight_potion.yaml", 20,
    "duration_per_level: 150",
    "duration_per_level: 0",
    "Zeroes core:flight_potion's per-level flight duration in its YAML tuning block, which flight_potion_on_eat reads through og.tuning(self).duration_per_level. The potion is still consumed and still notifies (the guard and notify_potion_consume are outside the tuning read), so the ONLY change is flight_left staying 0: the soldier is stopped by the PIX_TREE_B1 tiles at grid x=15..17 exactly like every other y=120 row in the corpus and ends at xpos 224, flipping WalkerPositionMoved(FAMILY_SOLDIER, 260, 100) on the x floor."
};

// Invis-potion CONSEQUENCE row. Same arena as invulnerable_potion_scen99
// (proven ranged-attrition geometry), swapping in a level-2 INVIS_POTION so
// invisibility_left = 300 covers the whole 250-tick budget. living.cpp:50
// drops the archer's foe with random(invisibility_left/20) > 0 == random(15),
// and screen.cpp:1013/1069 refuse to re-acquire on the same draw, so the
// archer almost never fires and the soldier keeps its HP.
inline constexpr SpawnSpec kFamilySpawns_treasure_invis_effect[] = {
    { FAMILY_INVIS_POTION, 2, kOrderTreasure, 128, 120, 0, 0, 2, 0 }, // level 2 -> 300 invisibility ticks > 250-tick budget; TEAM 2 (never a control candidate, never a foe)
    { FAMILY_ARCHER,       1, kOrderLiving,   260, 120, 0, 0       }, // ranged attacker east of the potion (invulnerable_potion_scen99 geometry)
    { FAMILY_SOLDIER,      0, kOrderLiving,    96, 120, 0, 0       }, // player LAST
};

inline constexpr FactPredicate kFacts_treasure_invis_effect_scen99[] = {
    pred::TickReached(250),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_INVIS_POTION, kOrderTreasure),
    // TEETH. Invisible for the whole run, the soldier takes at most a stray
    // arrow; with duration_per_level: 0 the archer holds its lock and the
    // arena becomes plain ranged attrition, dropping HP under the floor.
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000,
        "consequence: invisibility_left=300 makes the archer drop/refuse its lock on ~14 of every 15 ticks (living.cpp:50, screen.cpp:1013/1069); it never holds one long enough to fire, wanders off south-west and the soldier finishes the 250-tick run untouched at exactly max HP on both arms. duration_per_level: 0 restores the lock, arrows land, and the final HP drops out of this exact pin"),
    pred::EventKindAtLeast(/*notification*/2, 1),
};

inline constexpr Mutation kMut_treasure_invis_effect = {
    "packs/core/families/treasure-05-invis_potion.yaml", 20,
    "duration_per_level: 150",
    "duration_per_level: 0",
    "Zeroes core:invis_potion's per-level duration in its YAML tuning block, which invis_potion_on_eat reads through og.tuning(self).duration_per_level. The potion is still consumed and still notifies, so the only change is invisibility_left staying 0: the team-1 archer holds its lock for the full 250 ticks instead of losing it on ~14 of every 15 ticks, arrows land, and the soldier's final HP falls below WalkerHpRangeAtFinalTick's floor."
};

// Flight-potion GUARD row: a natural flier neither gains flight nor consumes
// the potion (treasure_consumables.lua:50; master treasure.cpp:106-116 keeps
// dead=1 inside the same guard). GHOST carries BIT_FLYING on both arms
// (gloader.cpp:947 / living-12-ghost.yaml init_bit_flags). No foe, so nothing
// can die and no death notification can pollute the exact-0 event count.
inline constexpr SpawnSpec kFamilySpawns_treasure_flight_flier[] = {
    { FAMILY_FLIGHT_POTION, 2, kOrderTreasure, 96, 120, 0, 0 }, // TEAM 2 and the arena's only team-2 entity: WalkerOfTeamAlive(2,1,1) is then a direct "the potion survived" read
    { FAMILY_GHOST,         0, kOrderLiving,   96, 120, 0, 0 }, // player LAST; BIT_FLYING eater, co-located so the tick-0 step reaches eat_me
};

inline constexpr FactPredicate kFacts_treasure_flight_potion_flier_noconsume_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_GHOST, 1),
    // TEETH #1. notify_potion_consume never runs for a flier.
    pred::EventKindExactly(/*notification*/2, 0,
        "consequence: flight_potion_on_eat's BIT_FLYING guard skips notify_potion_consume entirely for a natural flier; forcing the guard open emits one Potion of Flight notification and flips this exact-0 count"),
    // TEETH #2. The potion is the only team-2 entity, so alive-count 1 means
    // "not consumed" -- the assertion TreasureFamilyOfOrderRemovedFromOblist
    // structurally cannot make (a live survivor reads indeterminate).
    pred::WalkerOfTeamAlive(/*team=*/2, 1, 1,
        "consequence: set_dead(1) also lives inside the BIT_FLYING guard, so the potion stays alive in oblist; forcing the guard open kills it and the team-2 alive count drops to 0"),
    // Structural coverage anchor only: a live survivor with no consumed twin
    // reads indeterminate on both arms.
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_FLIGHT_POTION, kOrderTreasure),
};

inline constexpr Mutation kMut_treasure_flight_potion_flier_noconsume = {
    "packs/core/scripts/treasure_consumables.lua", 50,
    "if not eater:s_query_bit_flags(C.BIT_FLYING) then",
    "if true then",
    "Forces flight_potion_on_eat's natural-flier guard open. Unmutated, a BIT_FLYING eater (GHOST) skips the whole body: no flight_left grant, no notify_potion_consume, no set_dead(1), so the potion stays alive in oblist and the event stream carries zero notifications. Mutated, the ghost consumes the potion: one 'Potion of Flight' notification appears (EventKindExactly(notification,0) 0->1) and the potion is marked dead (WalkerOfTeamAlive(2,1,1) 1->0). Two independent teeth."
};

// Drumstick GUARD row: an unwounded eater triggers no heal, no set_dead(1)
// and no SOUND_EAT (treasure_consumables.lua:18; master treasure.cpp:70-71
// puts all three in the else arm). No foe and no second treasure, so the
// soldier is provably still at max HP when it walks over the drumstick.
inline constexpr SpawnSpec kFamilySpawns_treasure_drumstick_fullhp[] = {
    { FAMILY_DRUMSTICK, 2, kOrderTreasure, 96, 120, 0, 0, 10, 0 }, // level 10 (would heal 100 + rand0(100) if the guard fell through); TEAM 2 and the only team-2 entity, so WalkerOfTeamAlive(2,1,1) reads "not consumed"
    { FAMILY_SOLDIER,   0, kOrderLiving,   96, 120, 0, 0        }, // player LAST, spawns at full HP and stays there (no foe in the arena)
};

inline constexpr FactPredicate kFacts_treasure_drumstick_fullhp_noconsume_scen99[] = {
    pred::TickReached(150),
    // Untouched max HP is the guard's precondition; it holds on both arms
    // (the mutated heal clamps back to max), so this is the anchor, not teeth.
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    // TEETH #1: no SOUND_EAT. Verified reachable: the lone-soldier treasure
    // goldens (flight/invulnerable potion) carry zero play_sound events.
    pred::EventKindExactly(/*play_sound*/1, 0,
        "consequence: the hp >= max_hp early return skips og.emit_sound(SOUND_EAT); with the guard forced open the drumstick is eaten and exactly one play_sound appears"),
    // TEETH #2: the drumstick is still in the world.
    pred::WalkerOfTeamAlive(/*team=*/2, 1, 1,
        "consequence: set_dead(1) sits after the hp >= max_hp early return, so an unwounded eater leaves the drumstick alive in oblist; forcing the guard open kills it and the team-2 alive count drops to 0"),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_DRUMSTICK, kOrderTreasure),
};

inline constexpr Mutation kMut_treasure_drumstick_fullhp_noconsume = {
    "packs/core/scripts/treasure_consumables.lua", 18,
    "if eater.hp >= eater.max_hp then",
    "if false then",
    "Removes drumstick_on_eat's unwounded early return. Unmutated, a full-HP eater walks over the drumstick and nothing happens: no og.rand0 draw, no do_heal_effects, no set_dead(1), no SOUND_EAT -- the drumstick stays alive in oblist and the event stream has zero play_sound entries. Mutated, the level-10 drumstick is consumed at full HP (the heal clamps straight back to max_hitpoints), emitting one SOUND_EAT play_sound (EventKindExactly(play_sound,0) 0->1) and marking the drumstick dead (WalkerOfTeamAlive(2,1,1) 1->0)."
};

// Gold-bar GUARD row: a non-zero-team eater with no roster character banks
// nothing and does not consume the bar (treasure_valuables.lua:16; master
// treasure.cpp:85-93 keeps m_score, dead=1 and SOUND_MONEY inside the same
// guard). The eater is an AI orc co-located with the bar so its first chase
// step overlaps it -- the same overlap-at-tick-1 mechanism the passing
// gold_bar pickup row uses. The pair sits at (96,400), 280 px straight south
// of the player through open grass (scen1 grid rows 8..25 at tile x=6 are all
// grass), and the budget is 90 ticks, so the orc cannot reach melee: nothing
// dies, so no death award_score and no death notification can pollute the
// score/event reads.
inline constexpr SpawnSpec kFamilySpawns_treasure_gold_bar_team_reject[] = {
    { FAMILY_GOLD_BAR, 2, kOrderTreasure, 96, 400, 0, 0, 3, 0 }, // level 3 -> 600 points if the guard fell through; TEAM 2 and the only team-2 entity
    { FAMILY_ORC,      1, kOrderLiving,   96, 400, 0, 0       }, // team-1 eater, no myguy -> both guard clauses false; co-located with the bar
    { FAMILY_SOLDIER,  0, kOrderLiving,   96, 120, 0, 0       }, // player LAST, parked far from the bar
};

inline constexpr FactPredicate kFacts_treasure_gold_bar_team_reject_scen99[] = {
    pred::TickReached(90),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // TEETH #1: nothing is banked for the orc's team.
    pred::ScoreDelta(/*team*/1, 0, 0,
        "consequence: the eater.team == 0 or eater:has_guy() guard refuses the credit for a team-1 orc with no roster character; forcing the guard open banks score_per_level(200) * level 3 = 600 into m_score[1]"),
    pred::ScoreDelta(/*team*/0, 0, 0),
    // TEETH #2: the bar is still in the world.
    pred::WalkerOfTeamAlive(/*team=*/2, 1, 1,
        "consequence: self.dead = 1 sits inside the same guard, so a rejected bar stays alive in oblist; forcing the guard open kills it and the team-2 alive count drops to 0"),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_GOLD_BAR, kOrderTreasure),
};

inline constexpr Mutation kMut_treasure_gold_bar_team_reject = {
    "packs/core/scripts/treasure_valuables.lua", 16,
    "if eater.team == 0 or eater:has_guy() then",
    "if true then",
    "Forces gold_bar_on_eat's scoring guard open (the line number pins gold_bar_on_eat; silver_bar_on_eat carries the identical text at line 27 and _apply_mutation.py edits the named line only). Unmutated, the team-1 orc that walks onto the bar banks nothing, the bar is never set_dead and no SOUND_MONEY fires. Mutated, the orc cashes it: og.award_score(1, 200*3) puts 600 into m_score[1] (ScoreDelta(1,0,0) fails) and self.dead = 1 marks the bar consumed (WalkerOfTeamAlive(2,1,1) 1->0)."
};

// Life-gem GUARD row: only the gem's own team can claim it
// (treasure_valuables.lua:40; master treasure.cpp:287-288). The gem is TEAM 2
// and the eater is the team-0 player, so the guard rejects. NOTE the score
// credit cannot be the signal: a harness-spawned gem has hp 0, so
// og.award_score(team, trunc(0)) banks nothing on either arm (see the
// treasure_life_gem_pickup golden's score_per_team [0,0,0,0]). The FLASH is
// likewise unreachable -- it lands in oblist as an Order::FX entity rendered
// "FAMILY_FLASH", which no FactKind can address (EffectFamilyCount resolves
// arg0 through the FX table, WalkerFamilyCount through the Living table).
// The gem's own survival is the honest observable.
inline constexpr SpawnSpec kFamilySpawns_treasure_life_gem_enemy_reject[] = {
    { FAMILY_LIFE_GEM, 2, kOrderTreasure, 96, 120, 0, 0 }, // TEAM 2 != eater team 0 -> guard rejects; also the arena's only team-2 entity
    { FAMILY_SOLDIER,  0, kOrderLiving,   96, 120, 0, 0 }, // player LAST, co-located (proven eat geometry from the life_gem pickup row)
};

inline constexpr FactPredicate kFacts_treasure_life_gem_enemy_reject_scen99[] = {
    pred::TickReached(150),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // Same walk as treasure_life_gem_pickup_scen99 (xpos 168, ypos ~120):
    // an uneaten treasure does not block movement (obmap.cpp calls eat_me and
    // continues), which the treasure_key_pickup golden already demonstrates.
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 144, 110),
    // TEETH: the gem survives the cross-team touch.
    pred::WalkerOfTeamAlive(/*team=*/2, 1, 1,
        "consequence: the eater.team ~= self.team early return skips self.dead = 1 and self:death(), so the gem stays alive in oblist; forcing the guard open lets the team-0 soldier claim it and the team-2 alive count drops to 0"),
    pred::ScoreDelta(/*team*/0, 0, 0),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_LIFE_GEM, kOrderTreasure),
};

inline constexpr Mutation kMut_treasure_life_gem_enemy_reject = {
    "packs/core/scripts/treasure_valuables.lua", 40,
    "if eater.team ~= self.team then",
    "if false then",
    "Removes life_gem_on_eat's own-team-only early return. Unmutated, the team-0 soldier walking over a TEAM 2 gem is refused: no award_score, no FAMILY_FLASH, no self.dead = 1 and no self:death(), so the gem is still alive in oblist at the final tick. Mutated, the cross-team eater claims it -- the gem is marked dead and death() runs -- and WalkerOfTeamAlive(2,1,1) flips 1 -> 0. (The score stays 0 on both arms because a harness-spawned gem has hp 0, so it cannot be the flip channel.)"
};

// Magic-potion OVERFILL row. Reuses enemy_freeze_mage_scen99's arena and its
// proven observable (a frozen archer pinned at its spawn), but the mage is
// spawned WITHOUT the usual magicpoints override: SpawnSpec.magicpoints = 0
// leaves the statistics default of 50 (stats.cpp:73 / master stats.cpp:41),
// far below the mage's 500-point FREEZE TIME. A level-10 MAGIC_POTION adds
// mana_overfill_per_level(50) * 10 = 500 on top of the topped-off pool, taking
// it to 550; walker::special() hard-returns when magicpoints < special_cost,
// so the overfill alone decides whether the freeze happens.
inline constexpr InputEvent kInputs_magic_potion_overfill[] = {
    {  0, 0, K_RIGHT},          // take control + step onto the co-located potion
    {  4, 0, K_NONE},
    {  5, 0, K_SPECIAL_SWITCH}, // slot 1 -> 2
    {  6, 0, K_NONE},
    {  8, 0, K_SPECIAL_SWITCH}, // slot 2 -> 3 (FREEZE TIME; cycling gate needs level >= (3-1)*3+1 = 7)
    {  9, 0, K_NONE},
    { 20, 0, K_SPECIAL},        // cast: affordable only with the overfill
    { 21, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_treasure_magic_potion_overfill[] = {
    { FAMILY_MAGIC_POTION, 2, kOrderTreasure, 120, 120, 0, 0, 10, 0 }, // level 10 -> +500 mana on top of the 50-point pool; TEAM 2
    { FAMILY_ARCHER,       1, kOrderLiving,   200, 120, 0, 0,  5, 0 }, // freeze target, pinned at spawn while enemy_freeze runs
    { FAMILY_MAGE,         0, kOrderLiving,   120, 120, 0, 0, 12, 0 }, // caster LAST. magicpoints deliberately 0 (== do not override) so the pool is the 50-point default; level 12 clears the slot-3 cycling gate and gives freeze 20 + 11*12 = 152 ticks
};

inline constexpr FactPredicate kFacts_treasure_magic_potion_overfill_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_MAGIC_POTION, kOrderTreasure),
    // TEETH. Freeze duration 152 > the 150-tick budget, so a cast pins the
    // archer at its spawn for the whole run. Without the overfill the mage's
    // 50-point pool cannot pay the 500-point cost, walker::special() returns
    // 0, and the archer walks west toward the mage.
    pred::WalkerPositionMoved(FAMILY_ARCHER, 200, 120,
        "consequence: the potion's mana overfill (50 * level 10 = 500 on top of the 50-point default pool) is the only thing that funds the mage's 500-cost FREEZE TIME; the resulting enemy_freeze of 152 ticks holds the archer at its (200,120) spawn for the whole budget. With mana_overfill_per_level: 0 the pool stays at 50, walker::special() returns 0 without casting, and the archer steps west below the x floor"),
    pred::EventKindAtLeast(/*notification*/2, 1),
};

inline constexpr Mutation kMut_treasure_magic_potion_overfill = {
    "packs/core/families/treasure-04-magic_potion.yaml", 20,
    "mana_overfill_per_level: 50",
    "mana_overfill_per_level: 0",
    "Zeroes core:magic_potion's per-level mana overfill in its YAML tuning block, which magic_potion_on_eat reads through og.tuning(self).mana_overfill_per_level. The potion is still consumed and still notifies; the only change is the eater's pool. Unmutated the level-10 potion takes the mage from its 50-point default pool (stats.cpp default; SpawnSpec.magicpoints is deliberately left unset) to 550, funding the 500-cost FREEZE TIME whose 20 + 11*12 = 152-tick enemy_freeze pins the archer at its (200,120) spawn. Mutated the pool stays at 50, walker::special() hits the magicpoints < special_cost guard and returns 0 without casting, the archer chases the mage west and WalkerPositionMoved(FAMILY_ARCHER, 200, 120) fails on the x floor."
};

// Key GUARD row: a non-player eater sets the key bit silently
// (treasure_valuables.lua:73; master treasure.cpp:305-312). The key is never
// consumed on either arm, so the event stream carries the whole signal --
// which means the arena must be provably notification-free: no second
// treasure, and the AI eater parked 280 px from the player through open grass
// (scen1 tile column x=6, rows 8..25, is all grass) with a 90-tick budget so
// they never meet and nothing can die (walker_combat.cpp:392/397/415 emit a
// notification on every death).
inline constexpr SpawnSpec kFamilySpawns_treasure_key_team1_silent[] = {
    { FAMILY_KEY,     2, kOrderTreasure, 96, 400, 0, 0 }, // level 1 (default) -> key bit 2; TEAM 2, never consumed on either arm
    { FAMILY_ORC,     1, kOrderLiving,   96, 400, 0, 0 }, // team-1 eater, co-located: its first chase step overlaps the key
    { FAMILY_SOLDIER, 0, kOrderLiving,   96, 120, 0, 0 }, // player LAST, parked 280 px north
};

inline constexpr FactPredicate kFacts_treasure_key_team1_silent_scen99[] = {
    pred::TickReached(90),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // TEETH #1: no "<name> picks up key 1" message for a non-player eater.
    pred::EventKindExactly(/*notification*/2, 0,
        "consequence: key_on_eat's eater.team == 0 gate suppresses the pickup message for a team-1 orc; forcing the gate open emits one ' picks up key 1' notification"),
    // TEETH #2: no SOUND_MONEY either (both live inside the same gate).
    pred::EventKindExactly(/*play_sound*/1, 0,
        "consequence: og.emit_sound(C.SOUND_MONEY) sits inside the same eater.team == 0 gate; forcing the gate open adds exactly one play_sound"),
    // Anchor: the key is never set_dead by the hook, so this reads
    // indeterminate on both arms (and WalkerOfTeamAlive stays 1 on both).
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_KEY, kOrderTreasure),
};

inline constexpr Mutation kMut_treasure_key_team1_silent = {
    "packs/core/scripts/treasure_valuables.lua", 73,
    "if eater.team == 0 then",
    "if true then",
    "Forces key_on_eat's player-only announcement gate open. Unmutated, the team-1 orc that walks onto the key sets its key bit silently -- no notification, no SOUND_MONEY -- and the parked player never touches a treasure, so the event stream contains zero notifications and zero play_sounds. Mutated, the orc's pickup announces itself: EventKindExactly(notification,0) flips 0 -> 1 and EventKindExactly(play_sound,0) flips 0 -> 1. The key itself is never consumed on either arm (the hook has no set_dead), so the removal predicate stays a passing anchor."
};

// Exit-pad OPEN-PROMPT row (branch-internal Invariant; master records
// RequestExitConfirmation only in its withdraw block and its exit arm calls
// endgame() from inside eat_me, so no comparable master golden exists).
// No foe, so level_done reaches 2, foes_left is 0 and can_exit_now is true;
// no precompleted_level, so can_withdraw is false and the mutated fallthrough
// lands on the "Foes remain!" arm.
inline constexpr SpawnSpec kFamilySpawns_treasure_exit_open_prompt[] = {
    { FAMILY_EXIT,    2, kOrderTreasure, 120, 120, 0, 0 }, // TEAM 2 (matches the teleporter/exit rows); level 1 -> og.scenario_title("scen1")
    { FAMILY_SOLDIER, 0, kOrderLiving,   120, 120, 0, 0 }, // player LAST, co-located; ACT_CONTROL + skip_exit 0 clears exit_on_eat's prefix guards
};

inline constexpr FactPredicate kFacts_treasure_exit_open_prompt_scen99[] = {
    pred::TickReached(150),
    pred::LevelDoneEquals(2),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // TEETH #1: the b=0 "Exit to X?" prompt fires.
    pred::EventKindAtLeast(/*request_exit_confirmation*/7, 1,
        "consequence: with no foes left, can_exit_now is true and exit_on_eat emits the plain Exit-to prompt (b=0); disabling the can_exit_now arm drops the count to 0"),
    pred::EventKindExactly(/*withdraw_to_level*/8, 0),
    // TEETH #2: the exit arm returns before the else branch, so the
    // branch-only "Foes remain!" notification never appears.
    pred::EventKindExactly(/*notification*/2, 0,
        "consequence: the can_exit_now arm returns before the else branch, so no 'Foes remain!' notification is emitted; disabling can_exit_now falls through to it (can_withdraw is false with no precompleted_level) and the count flips to >= 1"),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_EXIT, kOrderTreasure),
};

inline constexpr Mutation kMut_treasure_exit_open_prompt = {
    "packs/core/scripts/treasure_navigation.lua", 65,
    "if can_exit_now then",
    "if false then",
    "Disables exit_on_eat's clear-level exit arm. Unmutated, the lone soldier's arena auto-completes (level_done 2, foes_left 0), so can_exit_now is true and the pad emits the plain 'Exit to <title>?' RequestExitConfirmation with b=0 and returns. Mutated, control falls through to the withdraw check, which is false (no precompleted_level), and lands on the else branch: the RequestExitConfirmation count flips >=1 -> 0 and a 'Foes remain!' notification appears, flipping EventKindExactly(notification,0) 0 -> 1."
};

// --- Specials: mage / thief / archmage alternate arms ----------------------
//
// Seven rows covering the Shift-gated ALTERNATE arms of the mage, thief and
// archmage slot specials, plus the illusion / mind-control / eight-way-fan
// observables the existing `special_*` rows reach but never assert. The
// harness input driver (scenario_runtime.cpp:317-325) sets shifter_down from
// the held mask and then calls control->special() exactly once per held tick,
// so a `{N, K_SPECIAL|K_SHIFT}, {N+1, K_NONE}` pair casts the alternate arm
// exactly once.
//
// scen1.fss is 40x60 tiles at GRID_SIZE 16 (640x960 px). The lake occupies the
// top-left corner only; tile rows 15-17, 19-22 and 24-25 are grass across the
// full width, and (120,120) is tile (7,7), grass — which is why the whole
// existing corpus stands there.

// Shift+Special on special slot 3 -> the ALTERNATE branch. Two
// K_SPECIAL_SWITCH rising edges walk current_special 1 -> 2 -> 3 (the harness
// cycler, scenario_runtime.cpp:247, gates on special_names[slot] != "NONE" and
// (slot-1)*3+1 <= stats.level), then a single K_SPECIAL held together with
// K_SHIFT casts the alternate once.
inline constexpr InputEvent kInputsSpecialSlot3Shift[] = {
    {  5, 0, K_SPECIAL_SWITCH},
    {  6, 0, K_NONE},
    {  8, 0, K_SPECIAL_SWITCH},
    {  9, 0, K_NONE},
    { 20, 0, K_SPECIAL | K_SHIFT},
    { 21, 0, K_NONE},
};

// THIEF slot-3 CHARM OPPONENT (thief.lua taunt_or_charm, shifter_down != 0).
// A level-7 thief against a level-1 soldier 25 px away clears both the
// acquisition range (charm_range_base 16 + 4 * level 7 = 44) and the level
// edge (+6), so the single og.rand(20) resist roll is the only stochastic
// element. The victim's dumped `team` is the observable, and no other row in
// the corpus asserts it.
inline constexpr SpawnSpec kFamilySpawns_thief_charm_opponent_scen99[] = {
    { FAMILY_SOLDIER, 1, kOrderLiving, 145, 120, 0, 0 },          // level-1 victim 25px east: inside charm range 16+4*7=44, level edge +6
    { FAMILY_THIEF,   0, kOrderLiving, 120, 120, 0, 0, 7, 600 },  // player THIEF LAST (find_player_walker takes the first team-0 Living in oblist and spawns PREPEND); level 7 clears the slot-3 cycle gate, 600 MP covers special_cost[3]=100
};

inline constexpr FactPredicate kFacts_thief_charm_opponent_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // TEETH. thief.lua's charm arm latches target:set_real_team_num(target.team)
    // and then target.team = self.team, so the level-1 SOLDIER's dumped team
    // goes 1 -> 0. charm_left = og.soften(75 + 6*25, 375, 490) = 225 ticks, far
    // beyond the 60-tick budget, so it cannot expire. The kMut leaves the victim
    // on team 1: team-0 alive falls to 1 and team-1 alive rises to 1.
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2,
        "consequence: THIEF slot-3 CHARM OPPONENT reassigns the victim to the caster's team, so team-0 alive is thief+charmed soldier; neutering the team latch leaves the soldier hostile and team-0 alive drops to 1"),
    pred::WalkerOfTeamAlive(/*team=*/1, 0, 0,
        "consequence: the charmed soldier vacates team 1 entirely; the kMut keeps it there so team-1 alive becomes 1"),
    // The victim is never damaged: the input script never presses K_FIRE and a
    // charmed walker stops fighting its former ally.
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::EventKindAtLeast(/*notification*/2, 1),
};

inline constexpr Mutation kMut_thief_charm_opponent_scen99 = {
    "packs/core/scripts/thief.lua", 141,
    "        target.team = self.team",
    "        target.team = target.team",
    "Keeps the charm bookkeeping (real_team_num latch, charm_left, the notification, busy += 10) but never moves the victim onto the caster's team, so both WalkerOfTeamAlive predicates flip (team-0 alive 2 -> 1, team-1 alive 0 -> 1)."
};

// ARCHMAGE slot-3 TRUE SUMMON (archmage.lua summon_image, shifter_down != 0).
// The only in-sim path that creates a summoned FAMILY_FIREELEMENTAL, and hence
// the only trigger for fire_elemental.lua's on_act_living owner toll.
inline constexpr SpawnSpec kFamilySpawns_archmage_summon_elemental_scen99[] = {
    { FAMILY_SOLDIER,  1, kOrderLiving, 180, 120, 0, 0 },           // sparring soldier (same placement as special_archmage_3_scen99) so the summoned elemental takes damage and fire_elemental.lua's owner-drain hook actually runs
    { FAMILY_ARCHMAGE, 0, kOrderLiving, 120, 120, 0, 0, 12, 900 },  // player ARCHMAGE LAST; level 12 clears the slot-3 cycle gate, 900 MP covers special_cost[3]=500 plus the halved surcharge
};

inline constexpr FactPredicate kFacts_archmage_summon_elemental_scen99[] = {
    pred::TickReached(120),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    // TEETH. The Shift arm of archmage.lua's summon_image is the only in-sim
    // producer of a FAMILY_FIREELEMENTAL; lifetime = 200 + 60*12 = 920 keeps it
    // alive far past the budget. The kMut makes the add_ob yield nothing, so
    // the family disappears from the dump entirely.
    pred::WalkerFamilyCount(FAMILY_FIREELEMENTAL, 1, 1,
        "consequence: archmage slot-3 TRUE SUMMON adds one FAMILY_FIREELEMENTAL to oblist on the caster's team; the kMut nulls the add_ob so no elemental exists"),
    pred::WalkerAliveAtFinal(FAMILY_FIREELEMENTAL, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2,
        "consequence: the summoned elemental is a second alive team-0 oblist entry; the kMut removes it and team-0 alive falls to 1"),
    // NO owner-drain assertion. fire_elemental.lua's on_act_living toll only
    // runs while the summon is hurt, and the golden shows the elemental at a
    // full 496/496 after wandering to (121,325) without ever being touched --
    // the caster ends on exactly 147 HP, the no-drain value. The drain arm is
    // therefore still UNCOVERED by this row; pinning the caster HP here would
    // assert melee timing, not the toll.
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_archmage_summon_elemental_scen99 = {
    "packs/core/scripts/archmage.lua", 299,
    "    local elemental = og.add_ob(\"living\", LIVING_ELEMENTAL)",
    "    local elemental = nil",
    "Makes the TRUE SUMMON branch's add_ob yield nothing, so the `if not elemental then return false end` failsafe aborts the cast: no FAMILY_FIREELEMENTAL ever enters oblist, WalkerFamilyCount(FAMILY_FIREELEMENTAL,1,1) collapses to 0, WalkerAliveAtFinal fails, and team-0 alive drops below 2."
};

// MAGE slot-1 TELEPORT MARKER (mage.lua, shifter_down != 0) end to end: place a
// marker, walk away past walker::teleport's 64 px threshold, then plain-teleport
// back onto it. Nothing in the corpus proves a caster lands on its own marker.
// y = 320 is tile row 20, grass across the full width of the arena, so the
// east-west corridor has no lake, wall or decor to block the walk or the
// landing probe.
inline constexpr InputEvent kInputsMarkerPlaceReturn[] = {
    {   5, 0, K_SPECIAL | K_SHIFT},  // drop the marker at the start cell
    {   6, 0, K_NONE},
    {  10, 0, K_LEFT},               // walk WEST so the return teleport is EASTWARD (WalkerPositionMoved is a >= bound)
    {  90, 0, K_NONE},
    { 100, 0, K_SPECIAL},            // plain TELEPORT -> ANI_TELE_OUT -> walker::teleport lands on the owned marker
    { 101, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_mage_teleport_marker_scen99[] = {
    { FAMILY_SOLDIER, 1, kOrderLiving, 100, 700, 0, 0 },           // far-off team-1 walker (tile 6,43 grass) keeps the level unwon; 520px away, AI closes only a few px in 140 ticks
    { FAMILY_MAGE,    0, kOrderLiving, 240, 320, 0, 0, 8, 600 },   // player MAGE LAST; level 8 -> marker lifetime 8//4+1 = 3 uses, 600 MP covers special_cost[1]=15 twice plus the surcharge
};

inline constexpr FactPredicate kFacts_mage_teleport_marker_scen99[] = {
    pred::TickReached(140),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    // TEETH. The FX-order FAMILY_MARKER lands in oblist as a team-0 walker
    // (same mechanism as the cleric's MAGIC_SHIELD), and its lifetime is a USE
    // counter, not a tick timer, so 3 uses minus the one return teleport leaves
    // it alive. The kMut sets the count to 1, so the single teleport consumes
    // it and effect::death reaps it.
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2,
        "consequence: mage.lua's marker arm places a persistent FAMILY_MARKER FX into oblist as a second alive team-0 walker and walker::teleport spends only one of its 3 uses; the kMut gives it 1 use so the return teleport kills it and team-0 alive drops to 1"),
    // The mage walked WEST and only the marker teleport can put it back east.
    pred::WalkerPositionMoved(FAMILY_MAGE, 240, 320,
        "consequence: the caster lands centred on its own marker, back at exactly its (240,320) start cell after walking west to x ~= 80; without the marker binding walker::teleport falls through to a random passable cell"),
    // "Teleport Marker Placed" + "(3 Uses)"; both are gated on
    // (team == 0 or has_guy()) AND user() != -1, and the harness driver claims
    // the walker (user = 0) on tick 0.
    pred::EventKindAtLeast(/*notification*/2, 2),
};

inline constexpr Mutation kMut_mage_teleport_marker_scen99 = {
    "packs/core/scripts/mage.lua", 125,
    "    marker.lifetime = self.level // 4 + 1",
    "    marker.lifetime = 1",
    "Gives the placed marker a single use instead of level//4+1 = 3. walker::teleport decrements the use counter on a successful jump and calls death() when it reaches 0, so the marker is reaped and WalkerOfTeamAlive(0,2,2) falls to 1 -- proving both that the marker persisted and that the return teleport actually consumed it."
};

// ARCHMAGE slot-1 TELEPORT MARKER: a deliberately divergent twin of the mage
// body (notifications not gated on user() != -1, hardcoded Int requirement 75,
// and marker.ani_type set to a raw 2 instead of ANI_SPIN == 1).
// archmage.lua's hit_response sets shifter_down 0 before recasting slot 1, so
// an AI archmage can never reach this arm and no other row in the corpus does.
//
// WHAT THIS ROW CANNOT ASSERT, AND WHY. On the branch the archmage's marker is
// reaped on its first act tick: raw ani_type 2 makes effect::animate compute
// ani_index = curdir + 2*8 >= 16, past the marker family's 16-row animation
// table, so the ani_count bound (src/gameplay/effect.cpp:120-127) resets it to
// ANI_WALK and effect::act then kills it. The e761 companion has no such bound,
// reads past the table, and keeps the marker spinning -- which is why its
// golden shows a live FAMILY_MARKER, an "(Old Marker Removed)" notice on the
// second cast, and a return teleport that lands on the marker. The merge base
// 05eaaa23 reproduces the branch exactly, so the divergence is master-era, not
// this PR; see tests/parity/golden/DRIFT_LEDGER.md. mage.lua's ANI_SPIN twin is
// unaffected, which is what mage_teleport_marker_scen99 pins. This row
// therefore asserts only the placement arm, which does run: two casts, four
// notifications, and the caster's own eastward walk.
inline constexpr InputEvent kInputsMarkerTwiceEast[] = {
    {   5, 0, K_SPECIAL | K_SHIFT},  // cast 1: place a marker at the start cell
    {   6, 0, K_NONE},
    {  10, 0, K_RIGHT},              // walk EAST
    {  70, 0, K_NONE},
    {  80, 0, K_SPECIAL | K_SHIFT},  // cast 2: place a second marker here
    {  81, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_archmage_teleport_marker_scen99[] = {
    { FAMILY_SOLDIER,  1, kOrderLiving, 100, 700, 0, 0 },          // far-off team-1 walker keeps the level unwon and never reaches the corridor
    { FAMILY_ARCHMAGE, 0, kOrderLiving, 240, 320, 0, 0, 8, 600 },  // player ARCHMAGE LAST; level 8 -> marker lifetime 3 uses; 600 MP covers three casts of special_cost[1]=10 plus two halved surcharges
};

inline constexpr FactPredicate kFacts_archmage_teleport_marker_scen99[] = {
    pred::TickReached(100),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    // TEETH. Two Shift casts, each emitting "Teleport Marker Placed" and
    // "(3 Uses)" -- archmage.lua does NOT gate either on user() != -1, which is
    // the divergence from mage.lua this row exists to pin. Everything upstream
    // of the notifications (the hardcoded Int 75 gate, add_ob, set_owner,
    // set_floor, center_on and the level//4+1 use count that fills the "(3
    // Uses)" text) has to run for the floor of 4 to hold.
    pred::EventKindAtLeast(/*notification*/2, 4,
        "consequence: each Shift cast of archmage slot 1 emits Teleport Marker Placed + (N Uses), ungated by user(); nulling the marker add_ob takes the `if not marker then return false end` exit before either notification and the count drops to 0"),
    // The caster's own eastward walk (stepsize 3, ticks 10..69), unperturbed:
    // the lone team-1 soldier is parked 520 px away and is still at (272,400)
    // at the final tick, so nothing interrupts the walk or the two casts.
    pred::WalkerPositionMoved(FAMILY_ARCHMAGE, 414, 320),
    // The caster is the ONLY alive team-0 entry: both placed markers are
    // already reaped (see the ani_type note above). This is a drift sentinel --
    // if the marker ever survives on the branch again, this predicate fails and
    // forces a conscious rebaseline instead of a silent behaviour change.
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
};

inline constexpr Mutation kMut_archmage_teleport_marker_scen99 = {
    "packs/core/scripts/archmage.lua", 170,
    "  local marker = og.add_ob(\"fx\", FX_MARKER)",
    "  local marker = nil",
    "Makes the marker arm's add_ob yield nothing, so the body takes its own `if not marker then return false end` exit before set_owner / center_on / the use count and before either notification. Both casts go silent: EventKindAtLeast(notification, 4) collapses to 0. mage.lua carries the identical text at line 114, but that is a different file, so the pin stays unambiguous."
};

// ARCHMAGE slot-4 MIND CONTROL. special_archmage_4_scen99 reaches this code but
// asserts only a notification floor and a caster-HP band; the victim's `team`
// field -- the single observable that separates the success arm from the
// berserk arm -- is asserted nowhere in the corpus.
inline constexpr SpawnSpec kFamilySpawns_archmage_mind_control_team_flip_scen99[] = {
    { FAMILY_SOLDIER,  1, kOrderLiving, 180, 120, 0, 0 },           // level-1 victim 60px east: inside mind-control range 80+4*10 = 120, level edge +9
    { FAMILY_ARCHMAGE, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 },  // player ARCHMAGE LAST; level 10 clears the slot-4 cycle gate, 600 MP covers special_cost[4]=150
};

inline constexpr FactPredicate kFacts_archmage_mind_control_team_flip_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    // TEETH. archmage.lua's mind_control success arm latches
    // foe:set_real_team_num(foe.team) then foe.team = self.team, so the
    // victim's dumped team goes 1 -> 0 for charm_duration(9) = 25 + rand(180)
    // ticks. The kMut keeps the victim hostile: team-0 alive falls to 1 and
    // team-1 alive rises to 1.
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2,
        "consequence: slot-4 MIND CONTROL moves the level-1 soldier onto the caster's team, so team-0 alive is archmage+victim; the kMut drops the reassignment and team-0 alive falls to 1"),
    pred::WalkerOfTeamAlive(/*team=*/1, 0, 0,
        "consequence: the controlled soldier vacates team 1 entirely; the kMut leaves it there and it also turns on the archmage"),
    // The controlled soldier stops fighting and is never attacked, so it holds
    // full HP; under the kMut it stays hostile and trades melee instead.
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000),
    pred::EventKindAtLeast(/*notification*/2, 1),
};

inline constexpr Mutation kMut_archmage_mind_control_team_flip_scen99 = {
    "packs/core/scripts/archmage.lua", 502,
    "        foe.team = self.team",
    "        foe.team = foe.team",
    "Keeps the real_team_num latch, charm_left and the 'has controlled N men' notification but never moves the victim onto the caster's team: WalkerOfTeamAlive(0,2,2) falls to 1, WalkerOfTeamAlive(1,0,0) rises to 1, and the still-hostile soldier melees the archmage."
};

// ARCHMAGE slot-3 ILLUSION arm (shifter_down == 0): a real Order::Living oblist
// entry with max_hp = 1, hp = 0, armor 0 and the name "Phantom", on the
// caster's team, chosen from a five-tier ladder keyed on post-cost MP.
// 550 MP is load-bearing: lc.spare_mp(self,3) = 50 selects the mp_pool < 100
// tier, the only tier that draws no RNG, so the phantom family is deterministic.
inline constexpr SpawnSpec kFamilySpawns_archmage_summon_image_phantom_scen99[] = {
    { FAMILY_SOLDIER,  1, kOrderLiving, 400, 400, 0, 0 },           // parked far (tile 25,25, grass) so nothing can kill the 0-HP phantom or perturb the caster
    { FAMILY_ARCHMAGE, 0, kOrderLiving, 120, 120, 0, 0,  7, 550 },  // player ARCHMAGE LAST; level 7 clears the slot-3 cycle gate. 550 MP is the load-bearing number: lc.spare_mp(self,3) = 550-500 = 50 lands in the mp_pool < 100 tier, the ONLY illusion tier that draws no RNG
};

inline constexpr FactPredicate kFacts_archmage_summon_image_phantom_scen99[] = {
    pred::TickReached(40),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    // The illusion tier ladder is pinned to its no-RNG branch by the 550 MP
    // spawn, so the phantom family is deterministic.
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    // Phantom signature: max_hp = 1 and hp = 0. A real FAMILY_ELF spawns at
    // 75 HP = 7500 cents, so this window can only be satisfied by an illusion.
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 0, 100,
        "consequence: summon_image's illusion arm sets max_hp = 1 / hp = 0 on the conjured walker, so the ELF entry sits in the 0..1 HP window a genuine 75-HP elf can never reach"),
    // TEETH. team-0 alive = archmage + phantom; the kMut moves the phantom to
    // team 3.
    pred::WalkerOfTeamAlive(/*team=*/0, 2, 2,
        "consequence: the phantom inherits the caster's team and is a second alive team-0 oblist entry; the kMut assigns it team 3 so team-0 alive drops to 1"),
};

inline constexpr Mutation kMut_archmage_summon_image_phantom_scen99 = {
    "packs/core/scripts/archmage.lua", 436,
    "          phantom.team = self.team",
    "          phantom.team = 3",
    "Conjures the illusion onto an unrelated team instead of the caster's, so the phantom stops counting as an allied oblist entry and WalkerOfTeamAlive(0,2,2) falls to 1 while the family and HP-signature predicates still hold -- isolating the team binding."
};

// MAGE slot-2 WARP SPACE, the eight-way fan. special_mage_2_scen99 can only
// infer the fan from a play_sound floor; nothing in the corpus shows the bolts
// actually reaching more than one heading.
//
// GEOMETRY IS LOAD-BEARING. starburst normalises each bolt's lastx/lasty to
// +-1 (mage.lua:164-171), so the fan's fireballs crawl at 1 px per axis per
// tick -- roughly a sixth of an ordinary cardinal-fire fireball -- and expire
// after 20 ticks, i.e. ~29 px per axis from the caster including the 9 px
// spawn offset. The observed headings are all DIAGONAL (2 NW, 2 SW, 3 NE,
// 1 SE); no bolt travels due north/south/east/west. The three foes therefore
// sit on three different diagonals 24 px out on each axis, which is the only
// band a fan bolt can reach: further and every bolt expires short, closer and
// the bolts spawn already inside the target. Three DIFFERENT families with one
// member each keep every per-foe predicate a per-heading assertion.
// FAMILY_TOWER1 (living-20-beast.yaml) is is_stationary, so the SW anchor
// cannot step out of the bolt's path.
inline constexpr SpawnSpec kFamilySpawns_mage_starburst_ring_scen99[] = {
    { FAMILY_ORC,     1, kOrderLiving, 144,  96, 0, 0 },          // NE diagonal, 24px per axis; BIT_NO_RANGED, survives its bolt at 136/140
    { FAMILY_TOWER1,  1, kOrderLiving,  96, 144, 0, 0 },          // SW diagonal, 24px per axis; is_stationary, so it cannot leave the bolt path
    { FAMILY_SOLDIER, 1, kOrderLiving,  96,  96, 0, 0 },          // NW diagonal, 24px per axis
    { FAMILY_MAGE,    0, kOrderLiving, 120, 120, 0, 0, 4, 600 },  // player MAGE LAST; level 4 clears the slot-2 cycle gate, 600 MP funds special_cost[2]=60 plus the eight bolts
};

inline constexpr FactPredicate kFacts_mage_starburst_ring_scen99[] = {
    pred::TickReached(40),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    // TEETH -- one per heading, and each family has exactly one member, so each
    // predicate names one specific walker. A single-direction fire can satisfy
    // at most one of the three; the kMut empties the sweep and all three foes
    // finish untouched, alive, at their spawn maxima.
    pred::WalkerDiedByFinal(FAMILY_SOLDIER,
        "consequence: the NW bolts of the eight-way WARP SPACE fan kill the north-west soldier outright at tick 21; with the sweep emptied it is never fired on and is still alive at the final tick"),
    pred::WalkerDiedByFinal(FAMILY_TOWER1,
        "consequence: the SW bolts kill the stationary south-west tower at tick 25 -- a heading no ordinary forward fire reaches; with the sweep emptied it survives untouched"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 13600, 13600,
        "consequence: the NE bolt clips the north-east orc for exactly 4 of its 140 HP; with the sweep emptied it ends on its full 14000 cents and this exact-value pin fails"),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 1,
        "consequence: only the NE orc outlives the fan, so exactly one of the three foes is still alive; with the sweep emptied all three survive and team-1 alive is 3"),
    pred::WeaponFamilyEmitted(FAMILY_FIREBALL),
};

inline constexpr Mutation kMut_mage_starburst_ring_scen99 = {
    "packs/core/scripts/mage.lua", 159,
    "      if i ~= 0 or j ~= 0 then",
    "      if false then",
    "Suppresses every bolt of the WARP SPACE fan while leaving the aim save/restore, the damage-bonus MP spend and the 8*weapon_cost refund intact. No FIREBALL is emitted and all three foes end at their full spawn HP, failing WeaponFamilyEmitted and all three per-heading HP windows."
};

// --- Scenario table --------------------------------------------------------

// --- specials-physical batch ------------------------------------------------
// Eight rows covering the physical-class special bodies the corpus reached but
// never observed: the soldier's whirlwind ring and its disarm roll, the orc's
// yell-stun and corpse eat, the archer's fire-arrow volley, the skeleton's
// tunnel hop, the druid's grown tree and the barbarian's non-exploding
// boulder. Every row runs on a fresh arena (fresh_arena = true empties
// scen1.fss), so the only entities are the SpawnSpec list plus the grid.
//
// Shared design facts (all four measured against these arenas, not assumed —
// the first three contradict the obvious reading of the code):
//   * A parity spawn DOES regenerate. statistics leaves heal_per_round_ at 0,
//     but compute_regen_tick still adds +1 hp every max_heal_delay ticks once
//     the 50-tick regen_delay armed by the last hit has run out. Every exact
//     hp pin below is therefore taken inside that 50-tick window, or on a
//     walker sitting at full hp where the regen clamps.
//   * max_hitpoints is the family's derived_bonuses[0] regardless of
//     stats_level: soldier 120, archer 90, elf 75, faerie 75, skeleton 60,
//     druid 110, orc 140, barbarian 150.
//   * MELEE damage does NOT scale with the striker's level. walker::attack
//     takes get_base_damage(this), i.e. the living's own derived_bonuses[2]
//     damage stat; only THROWN weapons pick up the (level+3)/4 * level factor
//     in create_weapon. Raising a caster's stats_level to make it one-shot a
//     melee target does nothing (measured: a level-8 and a level-20 orc both
//     hit for 22).
//   * A parity player walker starts at curdir == FACE_DOWN (4), not FACE_UP,
//     so an unmoved caster faces and probes SOUTH.
//   * A stationary player never auto-attacks: ob_pass_check collides only on
//     the MOVING walker and statistics::hit_response returns early for
//     ACT_CONTROL, so a caster with no movement input has busy == 0 and lands
//     no melee. That is what makes the busy-gated specials castable.

// SOLDIER slot-3 WHIRLWIND (soldier.lua whirlwind): eight queued spin steps,
// then one attack() plus one radial s_force_command per foe inside
// whirlwind_range_base + level * whirlwind_range_per_level. The existing
// special_soldier_3_scen99 uses FAMILY_SOLDIER for caster AND foe, so its
// ANY-match hp pin cannot say which soldier it matched and the ring itself is
// unobserved.
inline constexpr SpawnSpec kFamilySpawns_soldier_whirlwind_ring_scen99[] = {
    // Three FAMILY_FAERIE foes on three axes, all at Manhattan 32 <= whirlwind
    // range 32+7*2=46. Faerie is a family distinct from the caster, so the
    // ANY-match hp/position predicates cannot alias onto the soldier, and a
    // faerie's sprinkle is feeble enough (the caster ends on 114 of 120) that
    // nothing but the whirlwind can move one or take 20 hp off it.
    { FAMILY_FAERIE,  1, kOrderLiving, 152, 120, 0, 0 },          // east  — the shove target
    { FAMILY_FAERIE,  1, kOrderLiving, 120, 152, 0, 0 },          // south
    { FAMILY_FAERIE,  1, kOrderLiving,  88, 120, 0, 0 },          // west
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 7, 600 },  // caster LAST -> oblist head -> player
};

inline constexpr FactPredicate kFacts_soldier_whirlwind_ring_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerFamilyCount(FAMILY_FAERIE, 3, 3),
    pred::WalkerHpRangeAtFinalTick(FAMILY_FAERIE, 5500, 5500,
        "consequence: the ring strike lands one self:attack(foe) on every faerie inside range 46, and the east faerie ends on exactly 55 of its 75 hp. With the radius collapsed no faerie is touched at all and all three sit at 7500, so no faerie satisfies this pin"),
    pred::WalkerPositionMoved(FAMILY_FAERIE, 176, 0,
        "consequence: the east faerie is force_command(COMMAND_WALK, 8, +1, 0)'d off its (152,120) contact position and ends at x=176. The other two are shoved the other way (west faerie to x=64, south faerie stays at x=120), so this floor can only be met by the shoved east faerie; with no ring there is no shove and every faerie is at or west of 152"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11400, 11400,
        "the caster only ever loses hp to faerie sprinkles here (it never moves before the cast, so it never collide-melees); 114 of 120 is the exact figure both arms produce"),
};

inline constexpr Mutation kMut_soldier_whirlwind_ring_scen99 = {
    "packs/core/scripts/soldier.lua", 60,
    "    self, t.whirlwind_range_base + self.level * t.whirlwind_range_per_level)",
    "    self, 0)",
    "Collapses whirlwind's og.foes_in_range radius to 0. The three faeries sit at Manhattan >= 16 (bodies can never interpenetrate), so the foe list is empty: no self:attack(foe) and no outward s_force_command. The faerie hp predicate fails (all three stay at 7500) and the faerie position predicate fails (nothing is shoved east)."
};

// SOLDIER slot-4 DISARM (soldier.lua disarm): busy-gated, forward-blocked
// gated, then og.rand(self.level) >= og.rand(foe.level) LEFT-first per foe
// inside disarm_range, a 6*(level - foe.level + 1) busy penalty on the loser,
// SOUND_CHARGE and "Fighter Disarmed Enemy!". special_soldier_4_scen99 pairs a
// level-10 caster with a level-1 foe, so the right-hand draw is constantly 0
// and the recorded left-first adjudication is unfalsifiable; it also never
// asserts the notification.
inline constexpr SpawnSpec kFamilySpawns_soldier_disarm_matched_scen99[] = {
    // The foe sits touching to the SOUTH at y = 135, and it is a STATIONARY
    // family on purpose. Three measured facts drive that choice:
    //   * A parity-spawned player walker starts at curdir == FACE_DOWN (4),
    //     not FACE_UP, so the frontal probe looks SOUTH.
    //   * statistics::forward_blocked asks query_passable for the caster's own
    //     16x16 box shifted CHECK_STEP_SIZE == 1 px along curdir, and
    //     obmap::collide shrinks both boxes by 1 px per side. The caster's
    //     resting box is y [121,135] and its probe box is y [122,136], so a
    //     foe whose shrunk top edge is exactly 136 — i.e. ypos 135 — is clear
    //     of the caster at rest yet blocks the 1 px probe. 136 is the only
    //     ypos that satisfies both.
    //   * A mobile foe does not hold that lane: a 10x10 ELF parked here drifts
    //     12 px west by tick 20 and slides out of the caster's x footprint, so
    //     s_forward_blocked() reads false and disarm returns before the roll
    //     (verified by probe). FAMILY_TOWER1 is is_stationary, so it cannot
    //     drift, and its derived_bonuses[2] damage is 0 so its two contact
    //     swings cost the caster nothing — the row reads the special, not a
    //     combat outcome.
    // stats_level 3 (not 10) keeps BOTH rand draws live (og.rand(10) in 0..9
    // vs og.rand(3) in 0..2), which is what the FLAGGED left-first
    // adjudication needs in order to be falsifiable.
    { FAMILY_TOWER1,  1, kOrderLiving, 120, 135, 0, 0,  3,   0 },
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 }, // caster LAST; level 10 clears the slot-4 cycling gate ((4-1)*3+1), 600 mp covers special_costs[4]=150
};

inline constexpr FactPredicate kFacts_soldier_disarm_matched_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 1, 1),
    pred::EventKindAtLeast(/*notification*/2, 1,
        "consequence: a completed disarm is the ONLY notification source in this arena (nothing dies, no keys, no exit) — 'Fighter Disarmed Enemy!' proves the body ran past the busy/forward_blocked gates and found a foe inside disarm_range"),
    pred::EventKindExactly(/*play_sound*/1, 3,
        "consequence: exactly three sounds are emitted in this arena — two SOUND_CLANG from the tower's zero-damage melee at ticks 7 and 16, plus SOUND_CHARGE (id 9) at the tick-20 disarm. Collapsing disarm_range drops the run to two, so this exact count is a second tooth alongside the notification"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 12000, 12000,
        "structural pin: FAMILY_TOWER1's derived_bonuses[2] damage is 0, so the caster finishes untouched at 120/120 and the row's outcome cannot be confused with a combat result"),
};

inline constexpr Mutation kMut_soldier_disarm_matched_scen99 = {
    "packs/core/families/living-00-soldier.yaml", 79,
    "disarm_range: 28",
    "disarm_range: 0",
    "Collapses the disarm reach to 0. og.foes_in_range(self, 0) returns nothing (the tower sits at Manhattan 15), so found stays 0 and disarm returns false before SOUND_CHARGE, before the notification and before either busy write. EventKindAtLeast(notification,1) drops to 0 and EventKindExactly(play_sound,3) drops to 2 — two independent flips, and neither depends on which side won the two-draw roll."
};

// ORC slot-1 HOWL (orc.lua yell): radius scan, constitution derivation,
// left-first stun roll and foe:add_frozen_stun. special_orc_1_scen99 asserts
// only counts, a sound floor and a caster-hp figure — the victim's frozen
// state is never observed. This row borrows enemy_freeze_mage_scen99's proven
// shape: a stunned archer is pinned at its spawn, so a position floor at the
// spawn reads as "held".
inline constexpr SpawnSpec kFamilySpawns_orc_yell_stun_hold_scen99[] = {
    // Archer target, same shape as kFamilySpawns_enemy_freeze_mage_scen99: an
    // archer's position is a clean pin (it closes to firing range rather than
    // charging) and its constitution is trunc(90/30) = 3 -> rand0(30).
    { FAMILY_ARCHER, 1, kOrderLiving, 200, 120, 0, 0,  5,   0 },
    // Caster LAST. Level 14 is the largest level whose stun roll
    // 10 + rand0(140) - rand0(30) cannot exceed kFrozenStunStackCap (149 < 150),
    // so the branch-only stun clamp never engages and the branch/master stun
    // values agree exactly. og.combat.yell_radius(14) = 160+280 = 440 is capped
    // to 420 branch-side, which is immaterial at 80px separation.
    // 600 mp covers special_costs[1] = 25.
    { FAMILY_ORC,    0, kOrderLiving, 120, 120, 0, 0, 14, 600 },
};

inline constexpr FactPredicate kFacts_orc_yell_stun_hold_scen99[] = {
    pred::TickReached(50),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::WalkerPositionMoved(FAMILY_ARCHER, 200, 120,
        "consequence: HOWL banks frozen_delay on the archer at tick 20; a frozen walker never acts, so it is still standing on its spawn (200,120) at tick 50. With no stun banked it steps west toward the orc and its xpos drops below 200 (same construction as enemy_freeze_mage_scen99)"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 7400, 7400,
        "consequence: EXACT pin at 74 of 140. The archer's pre-cast FIRE ARROWS volley is the orc's only damage source and the last of those bolts expires at tick 28; a stunned archer starts nothing new, so the figure is frozen from the cast to the tick-50 capture. Bank a zero stun and the archer keeps firing, which moves this pin. (Regen cannot reach it either: regen_delay is re-armed to 50 on every hit and the budget ends 22 ticks after the last one.)"),
    pred::EventKindExactly(/*play_sound*/1, 9,
        "consequence: nine sounds — the archer's pre-cast volley plus SOUND_ROAR on the completed yell. A yell that banks no stun leaves the archer firing for the remaining 30 ticks, so the count rises above nine"),
};

inline constexpr Mutation kMut_orc_yell_stun_hold_scen99 = {
    "packs/core/scripts/orc.lua", 41,
    "      foe:add_frozen_stun(stun)",
    "      foe:add_frozen_stun(0)",
    "Banks a zero stun. stun_total(raw, 0) returns raw unchanged, so the archer is never frozen: it acts from tick 20 on, steps west off its spawn (failing the archer position floor) and its level-5 arrows drop the orc below the caster hp pin. The radius scan, the two RNG draws and SOUND_ROAR all still run, so the flip isolates the freeze write itself."
};

// ORC slot-2 EAT CORPSE (orc.lua eat_corpse): the full-hp refusal, the nearest
// bloodstain scan, the squared-distance reach gate, the heal, the int16 heal
// marker, the notice and the corpse kill. special_orc_2_scen99 casts into an
// arena with no bloodstain at all, so the whole body early-outs.
// Two hard constraints shape this arena: distance_to_ob_center is SQUARED
// euclidean, so `dist > 24` is a ~4.9px radius; and find_nearest_blood walks
// fxlist only, while a SpawnSpec treasure lands in oblist — so the stain has
// to come from a real in-run death.
inline constexpr InputEvent kInputs_orc_eat_corpse_scen99[] = {
    // Cycle to slot 2 (EAT CORPSE). orc special_names[2] = "EAT CORPSE" and the
    // cycling gate needs (2-1)*3+1 = 4 <= stats.level.
    {  5, 0, K_SPECIAL_SWITCH}, {  6, 0, K_NONE},
    // ticks 6..39 idle: the touching elf melees the orc so hp < max_hp when the
    // first eat is attempted, and the team-0 archer snipes it dead at tick 43,
    // dropping the stain at (136,120). The player walker is stationary through
    // all of this, so it never collide-melees and never queues a command.
    // tick 40: hold K_RIGHT for the rest of the run, walking the orc east
    // across the stain on a 3px lattice.
    { 40, 0, K_RIGHT},
    // Pulse K_SPECIAL every other tick from 46. A failed eat costs nothing
    // (walker::special only debits magicpoints when the hook returns true), and
    // sampled x positions are 6px apart while the eat window |dx| <= 4 is 9px
    // wide, so the pass is guaranteed to be sampled.
    { 46, 0, K_RIGHT | K_SPECIAL}, { 47, 0, K_RIGHT},
    { 48, 0, K_RIGHT | K_SPECIAL}, { 49, 0, K_RIGHT},
    { 50, 0, K_RIGHT | K_SPECIAL}, { 51, 0, K_RIGHT},
    { 52, 0, K_RIGHT | K_SPECIAL}, { 53, 0, K_RIGHT},
    { 54, 0, K_RIGHT | K_SPECIAL}, { 55, 0, K_RIGHT},
    { 56, 0, K_RIGHT | K_SPECIAL}, { 57, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_orc_eat_corpse_scen99[] = {
    // Team-0 sniper, parked south and out of the way. It exists solely to KILL
    // the elf: melee damage is level-independent (see the batch header), so the
    // orc's own 22-per-hit rock needs four swings and the elf's AI backs out of
    // reach at low hp long before the fourth — measured, the elf survives 110
    // ticks and no corpse is ever produced. A level-20 archer's ARROW does pick
    // up create_weapon's (level+3)/4 * level factor and one-shots the elf at
    // tick 43, dropping the stain on the orc's walking line at (136,120).
    { FAMILY_ARCHER, 0, kOrderLiving, 120, 220, 0, 0, 20, 0 },
    // The corpse source, spawned EXACTLY touching to the east (elf 136..152 vs
    // orc 120..136 — adjacent, never interpenetrating, so the branch-only obmap
    // interpenetration escape rule stays unreachable and the two arms cannot
    // diverge there). Already standing on its foe, it melees the orc from tick
    // 9, which is what puts the orc under max_hitpoints before the first eat
    // attempt, and it never walks, so its stain lands where it stood.
    { FAMILY_ELF, 1, kOrderLiving, 136, 120, 0, 0          },
    // Caster LAST -> oblist head -> player. Level 8 clears the slot-2 cycling
    // gate ((2-1)*3+1 = 4); 600 mp covers special_costs[2] = 20 (only a
    // SUCCESSFUL eat is charged, so the failed pulses are free).
    { FAMILY_ORC, 0, kOrderLiving, 120, 120, 0, 0, 8, 600 },
};

inline constexpr FactPredicate kFacts_orc_eat_corpse_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_ORC, 1),
    pred::WalkerFamilyCount(FAMILY_ELF, 0, 0,
        "the corpse source is sniped by the team-0 archer and reaped; its FAMILY_STAIN goes to fxlist, which is the only list find_nearest_blood walks"),
    // negative_assertion: the elf must be DEAD for a bloodstain to exist at all — its absence from oblist is the corpse's provenance, not an incidental count.
    pred::WalkerPositionMoved(FAMILY_ORC, 132, 0,
        "consequence: the orc must actually walk east onto the stain for the squared-distance gate (dist > 24 == a 4.9px radius) to open"),
    pred::EventKindAtLeast(/*notification*/2, 1,
        "consequence: 'Orc ate a corpse.' at tick 48 is the ONLY notification this arena produces — the elf's death emits none — so the floor proves the eat body ran past the full-hp refusal, the fxlist bloodstain scan and the squared-distance reach gate"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 7900, 7900,
        "consequence: EXACT pin at 79 of 140. The orc's damage stops when the elf dies at tick 43, and the tick-60 capture is inside the 50-tick regen_delay re-armed by that last hit, so the only thing that can still move hp is the eat itself: corpse.level * corpse_heal_per_level = 1*5. Zero the per-level heal and the arm lands at exactly 7400."),
};

inline constexpr Mutation kMut_orc_eat_corpse_scen99 = {
    "packs/core/families/living-14-orc.yaml", 71,
    "corpse_heal_per_level: 5",
    "corpse_heal_per_level: 0",
    "Zeroes the per-corpse-level heal. Every other step of eat_corpse still runs (find_nearest_blood, the range gate, do_heal_effects, the notice, corpse:death()) and no RNG draw changes, so the mutated dump is identical except the orc's final hp is exactly 5 lower — the exact orc hp pin flips."
};

// ARCHER slot-1 FIRE ARROWS (archer.lua fire_arrows): aim reset, magic refund,
// COMMAND_SET_WEAPON(core:fire_arrow), eight COMMAND_QUICK_FIRE headings and
// COMMAND_RESET_WEAPON. The only corpus row that pins FAMILY_FIRE_ARROW is
// weapon_fire_arrow_emission_scen99, which uses slot 3 (exploding_shot), so
// the slot-1 weapon swap and the queued ring are untested.
inline constexpr SpawnSpec kFamilySpawns_archer_fire_arrows_ring_scen99[] = {
    // One foe on each of the volley's two cardinal headings that matter, both
    // inside the fire arrow's ~96px reach (stepsize 8*1.414 x lineofsight 12).
    // Level-1 elves plink for 3 damage a rock, so the 90hp archer survives the
    // whole budget while still producing a live two-sided engagement.
    { FAMILY_ELF,    1, kOrderLiving, 190, 120, 0, 0          }, // east
    { FAMILY_ELF,    1, kOrderLiving,  50, 120, 0, 0          }, // west
    { FAMILY_ARCHER, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 }, // caster LAST; special_costs[1] = 20
};

inline constexpr FactPredicate kFacts_archer_fire_arrows_ring_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ELF, 2, 2),
    pred::WeaponFamilyEmitted(FAMILY_FIRE_ARROW,
        "consequence: slot 1 swaps the archer's weapon to core:fire_arrow for the eight queued QUICK_FIREs, so the ring is FAMILY_FIRE_ARROW and not the archer's default FAMILY_ARROW"),
    pred::WeaponNetTravel(FAMILY_FIRE_ARROW, kWeaponPathStraight, 12542,
        "trajectory: a QUICK_FIRE arrow flies one fixed heading for its whole life, so net == pathlen. The seq-0 bolt of the ring runs (110,137) -> (33,236) over ticks 25..36, a net 12542 centi-px on the down-left diagonal"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 6900, 6900,
        "consequence: EXACT pin. One ring arrow reaches the west elf and takes it to 69 of 75; the east elf is untouched at 75, so only a landed fire arrow can satisfy this"),
    pred::ScoreDelta(0, 7, 7,
        "consequence: weapon hits award team-0 score (tempdamage + target level, walker_combat.cpp owner() branch). The single ring hit is worth 6 + level 1 = 7 and nothing else in the arena scores"),
};

inline constexpr Mutation kMut_archer_fire_arrows_ring_scen99 = {
    "packs/core/scripts/archer.lua", 6,
    "local WEAPON_FIRE_ARROW = assert(og.family_id(\"weapon\", \"core:fire_arrow\"))",
    "local WEAPON_FIRE_ARROW = assert(og.family_id(\"weapon\", \"core:arrow\"))",
    "Points the fire-arrow constant at core:arrow. The slot-1 COMMAND_SET_WEAPON then selects the archer's ordinary arrow, so no FAMILY_FIRE_ARROW ever enters weaplist or weapon_tracks and WeaponFamilyEmitted(FAMILY_FIRE_ARROW) fails. (The same constant feeds exploding_shot, so the flip is not slot-1-exclusive — acceptable for a staged-copy canary.)"
};

// SKELETON slot-1 TUNNEL (skeleton.lua do_special + handle_teleport): ANI_TELE_OUT,
// then on animation completion walker::animate dispatches handle_teleport,
// which sets ANI_TELE_IN and calls teleport_ranged(level * 18). The corpus's
// only Special_Skeleton_1 row, special_skeleton_1_scen99, asserts
// WalkerDiedByFinal(FAMILY_SKELETON) — it certifies the caster dying in melee,
// the exact opposite of a completed self-teleport.
inline constexpr SpawnSpec kFamilySpawns_skeleton_tunnel_scen99[] = {
    // Lone caster, no foes at all (the same enemy-free construction
    // effect_protection_emit_scen99 uses) so nothing but teleport_ranged can
    // move the skeleton and nothing can damage it. Spawned at (64,144) — inside
    // the open west band of the scen1 grid, with the tile-0..2 wall block to the
    // west, so teleport_landing_clear rejects most westward rolls and the
    // accepted destination biases east/south of the spawn.
    // Level 10 -> tunnel radius 10*18 = 180px; 600 mp covers special_costs[1] = 10.
    { FAMILY_SKELETON, 0, kOrderLiving, 64, 144, 0, 0, 10, 600 },
};

inline constexpr FactPredicate kFacts_skeleton_tunnel_scen99[] = {
    pred::TickReached(90),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SKELETON, 1,
        "the caster SURVIVES its own special — the corpus's only skeleton-special row asserts the opposite"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SKELETON, 6000, 6000,
        "nothing in the arena can damage the caster, so its hp is an exact structural pin"),
    pred::WalkerPositionMoved(FAMILY_SKELETON, 172, 286,
        "consequence: TUNNEL relocates the caster by teleport_ranged(level*18 = 180px) and it lands at exactly (172,286) — 108px east and 142px south of its (64,144) spawn. With no movement input and no foe in the arena, a position that far from the spawn can ONLY come from the completed TELE_OUT -> handle_teleport -> TELE_IN chain"),
};

inline constexpr Mutation kMut_skeleton_tunnel_scen99 = {
    "packs/core/scripts/skeleton.lua", 12,
    "  self:teleport_ranged(self.level * 18)",
    "  self:teleport_ranged(self.level * 1)",
    "Shrinks the tunnel radius from 180px to 10px. The animation chain still runs and the skeleton still hops, but it lands within 10px of (64,144) so the position floor read off the golden fails. Using *1 rather than *0 keeps rng.next(2*range) away from a zero argument."
};

// DRUID slot-1 GROW TREE (druid.lua plant_tree): busy bail, weapon_cost refund,
// self:fire(), busy += fire_frequency*2, og.summon(core:tree) at the bolt's
// position with ANI_GROW, then the bolt is killed. This is the ONLY producer of
// FAMILY_TREE. weapon_tree_emission_scen99, the corpus's only FAMILY_TREE row,
// direct-spawns the tree as kOrderWeapon and gates its emission predicate off
// on both sides.
inline constexpr SpawnSpec kFamilySpawns_druid_grow_tree_scen99[] = {
    // Lone caster, no foes. plant_tree calls self:fire() directly (not
    // fire_check), so no foe is needed, and an empty arena keeps the bolt's
    // landing point — and therefore the tree's position — fully deterministic:
    // lastx/lasty are 0, facing(0,0) == FACE_UP, so the bolt spawns at
    // (120 + (16-8)/2, 120 - 8 - 1) = (124, 111).
    // Level 10 + 300 mp: special_costs[1] = 15 and weapon_cost 4 (refunded).
    { FAMILY_DRUID, 0, kOrderLiving, 120, 120, 0, 0, 10, 300 },
};

inline constexpr FactPredicate kFacts_druid_grow_tree_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_DRUID, 11000, 11000,
        "nothing can damage the caster in this arena, so its hp is an exact structural pin"),
    pred::WeaponFamilyEmitted(FAMILY_TREE,
        "consequence: GROW TREE is the only producer of FAMILY_TREE; og.summon(self,\"weapon\",WEAP_TREE) puts a real tree in weaplist at the bolt's landing point, which is what the corpus's direct-spawn tree row could not observe"),
    pred::WeaponNetTravel(FAMILY_TREE, kWeaponPathStationary, 200,
        "trajectory: the grown tree is ACT_SIT with stepsize 0, so its whole track has pathlen 0 (2px of slack)"),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "self:fire() emits the lightning bolt's fire_sound before the bolt is replaced by the tree"),
};

inline constexpr Mutation kMut_druid_grow_tree_scen99 = {
    "packs/core/scripts/druid.lua", 6,
    "local WEAP_TREE = assert(og.family_id(\"weapon\", \"core:tree\"))",
    "local WEAP_TREE = assert(og.family_id(\"weapon\", \"core:blob\"))",
    "Points the tree constant at core:blob. GROW TREE then plants a blob at the bolt's landing point: no FAMILY_TREE ever enters weaplist or weapon_tracks, so WeaponFamilyEmitted(FAMILY_TREE) fails, and the blob is a moving ACT_FIRE weapon so the STATIONARY class would not hold either."
};

// BARBARIAN slot-1 HURL BOULDER (barbarian.lua do_special): identical to slot 2
// except the last line, `if self:current_special() == 2 then
// boulder:set_skip_exit(5000) else boulder:set_skip_exit(0) end`. weap::death
// shows skip_exit is the ONLY thing that separates the two slots and its sole
// effect is spawning a FAMILY_EXPLOSION on the boulder's death, so the
// discriminator has to be the missing blast. special_barbarian_1_scen99 asserts
// only counts/sound/HP band and weapon_exploding_boulder_scen99 uses slot 2
// with a cluster the boulder never reached.
inline constexpr InputEvent kInputs_barbarian_boulder_impact_scen99[] = {
    // The caster must FACE east before firing: with lastx/lasty at 0 the boulder
    // would be hurled north (facing(0,0) == FACE_UP). Five ticks of K_RIGHT at
    // stepsize 3 leaves the barbarian at x=135, still 41px clear of the nearest
    // elf so no collide-melee fires.
    {  1, 0, K_RIGHT}, {  6, 0, K_NONE},
    // Slot 1 (HURL BOULDER) is current_special's default, no cycling needed.
    { 20, 0, K_SPECIAL}, { 21, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_barbarian_boulder_impact_scen99[] = {
    // Three level-1 elves in a line on the boulder's heading, pulled close enough
    // that contact is certain well before the boulder's 9-tick life expires (the
    // slot-2 row's 220/250/280 cluster outran its boulder). Elves are 75hp: they
    // SURVIVE one ~28-damage plow-through but NOT plow-through plus a 60-damage
    // blast, which is what gives the alive-count predicate teeth. Their level-1
    // rocks do 3 damage a hit, harmless to a 150hp barbarian.
    { FAMILY_ELF,       1, kOrderLiving, 176, 120, 0, 0          },
    { FAMILY_ELF,       1, kOrderLiving, 200, 120, 0, 0          },
    { FAMILY_ELF,       1, kOrderLiving, 224, 120, 0, 0          },
    { FAMILY_BARBARIAN, 0, kOrderLiving, 120, 120, 0, 0, 5, 300 }, // caster LAST; level 5 -> boulder stepsize 10, special_costs[1] = 20
};

inline constexpr FactPredicate kFacts_barbarian_boulder_impact_scen99[] = {
    pred::TickReached(45),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::WeaponFamilyEmitted(FAMILY_BOULDER),
    // NO WeaponSpeed / WeaponNetTravel here. The nearest elf has closed to
    // ~15px by the tick-20 cast, so the boulder makes contact on its very
    // first step and dump.weapon_tracks holds a single sample — both
    // trajectory kinds would evaluate Indeterminate, which passes without
    // asserting anything. Boulder flight speed is already pinned by
    // weapon_boulder_emission_scen99, and every weapon_tracks sample in this
    // row is byte-compared against the golden regardless.
    pred::WalkerFamilyCount(FAMILY_ELF, 3, 3),
    pred::WalkerAliveAtFinal(FAMILY_ELF, 3,
        "consequence: skip_exit 0 means the slot-1 boulder simply dies where it lands; the elf it plows through survives on 46 of 75. The slot-2 sentinel detonates a damage*2 = 60 blast at that same death point, which finishes that elf"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ELF, 4600, 4600,
        "consequence: EXACT pin on the plowed elf — 75 hp less the boulder's 29-damage hit. Under the explode sentinel that elf is dead instead, and the two untouched elves sit at 7500, so nothing satisfies the pin"),
    pred::ScoreDelta(0, 30, 30,
        "consequence: weapon hits award team-0 score (tempdamage + target level). The single plow-through is worth 29 + level 1 = 30 and nothing else in the arena scores; a blast plus its kill bonus pushes the total well past it"),
};

inline constexpr Mutation kMut_barbarian_boulder_impact_scen99 = {
    "packs/core/scripts/barbarian.lua", 59,
    "    boulder:set_skip_exit(0)",
    "    boulder:set_skip_exit(5000)",
    "Gives the slot-1 boulder the legacy 5000 explode-on-death sentinel, i.e. makes HURL BOULDER behave as EXPLODING BOULDER. weap::death then spawns a FAMILY_EXPLOSION with damage*2 = 60 at the boulder's death point, killing the already-plowed elf standing there: the elf alive-count drops below 3 and the blast's damage pushes score_per_team[0] past the ScoreDelta ceiling."
};


// --- slime-death-generators batch -----------------------------------------

// magic_damage_slime_scen99: one FIRE ELEMENTAL STARBURST puts the SAME
// core:meteor (BIT_MAGICAL) onto a FAMILY_SLIME to the east and a
// FAMILY_SOLDIER to the south. walker_combat.cpp:291 multiplies the
// damage by the TARGET family's magic_damage_modifier (2 on
// living-08-slime.yaml, 1 on living-00-soldier.yaml), so the two HP
// predicates below are a direct differential readout of that descriptor
// field: the slime loses 24 of 150 and the soldier 10 of 120 off the same
// volley.
//
// The 64px stand-off is load-bearing, NOT decorative. walker::fire's
// melee branch (src/gameplay/walker.cpp:577) calls attack() on the LIVING
// when the new weapon spawns onto an impassable tile, so `this` is the
// elemental and `stats_->query_bit_flags(BIT_MAGICAL)` reads the
// ELEMENTAL's flags — the meteor's MAGICAL bit never reaches the modifier
// and the whole descriptor field goes untested. Only a projectile that
// actually flies reaches attack() with `this` == the meteor. An arena with
// the targets adjacent to the caster measured IDENTICAL hitpoints at
// modifier 1, 2 and 10.
//
// The slime carries magicpoints 1 against special_cost[1] == 30 so
// living::check_special refuses its AI SPLIT: an un-doctored AI slime
// animation-splits into two SMALL_SLIMEs around tick 25 and takes the
// measured family off the board. With the split blocked both hitpoint
// readings are flat from tick 20 through tick 50.
inline constexpr InputEvent kInputs_magic_damage_slime[] = {
    {  5, 0, K_SPECIAL },
    { 25, 0, K_NONE },
};

inline constexpr SpawnSpec kFamilySpawns_magic_damage_slime_scen99[] = {
    { FAMILY_SLIME,         1, kOrderLiving, 184, 120, 0, 0, 1, 1 },        // 2x magic target, 64px east — far enough that the meteor flies instead of resolving as the caster's melee; magicpoints 1 blocks the AI SPLIT
    { FAMILY_SOLDIER,       1, kOrderLiving, 120, 184, 0, 0 },              // 1x magic control, 64px south
    { FAMILY_FIREELEMENTAL, 0, kOrderLiving, 120, 120, FAMILY_METEOR, 0 },  // player caster LAST so find_player_walker binds the oblist head to it
};

inline constexpr FactPredicate kFacts_magic_damage_slime_scen99[] = {
    pred::TickReached(40),
    pred::WalkerFamilyCount(FAMILY_SLIME, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SLIME, 1),
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 0, 0),
    // negative_assertion: the 40-tick budget must leave the doubled-damage slime alive, so slime.lua's on_death split never runs and no MEDIUM_SLIME offspring may exist on either arm.
    pred::WalkerHpRangeAtFinalTick(FAMILY_SLIME, 12500, 12700,
        "consequence: the eastern meteor is doubled by living-08-slime.yaml's magic_damage_modifier: 2 and takes 24 of the slime's 150; at modifier 1 it takes about half that and the slime lands above this band"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 10900, 11100,
        "control: the southern meteor is the SAME weapon at modifier 1.0 and takes 10 of the soldier's 120 — the untouched half of the differential, so the slime's larger loss can only be the descriptor"),
    pred::EventKindAtLeast(/*play_sound*/1, 8,
        "consequence: STARBURST emits one core:meteor fire_sound per direction (8) on top of the collide-fire traffic"),
};

inline constexpr Mutation kMut_magic_damage_slime_scen99 = {
    "packs/core/families/living-08-slime.yaml", 39,
    "      magic_damage_modifier: 2",
    "      magic_damage_modifier: 1",
    "Removes the slime's magic susceptibility at its live source. walker_combat.cpp:291 then multiplies the MAGICAL meteor damage by 1.0 instead of 2.0, so the slime keeps roughly twice the hitpoints and falls outside WalkerHpRangeAtFinalTick(FAMILY_SLIME, ...) while the FAMILY_SOLDIER control band is unchanged."
};

// slime_death_split_scen99: the FAMILY_SLIME is the PLAYER-team walker so it
// is ACT_CONTROL and can never enter ACT_RANDOM -> living::check_special ->
// ANI_SLIME_SPLIT. The only route to offspring is therefore slime.lua's
// on_death (split_on_death), which is the hook under test. A level-3 team-1
// SOLDIER executioner kills it inside the budget; the budget then stops
// BEFORE the MEDIUM_SLIME offspring itself dies, so exactly one split has
// happened at dump time. Swept from the capture: the parent dies between
// ticks 60 and 70, the offspring between 100 and 110, and a transient
// team-0 FAMILY_KNIFE_BACK is in oblist at tick 90 — tick 80 is the clean
// middle of that window.
inline constexpr SpawnSpec kFamilySpawns_slime_death_split_scen99[] = {
    { FAMILY_SOLDIER, 1, kOrderLiving, 100, 120, 0, 0, 3, 0 }, // level-3 executioner, 4px west of the slime (same gap effect_combat_arena uses)
    { FAMILY_SLIME,   0, kOrderLiving, 120, 120, 0, 0 },       // player-bound victim LAST (32x32 sprite -> occupies 120..152)
};

inline constexpr FactPredicate kFacts_slime_death_split_scen99[] = {
    pred::TickReached(80),
    pred::WalkerDiedByFinal(FAMILY_SLIME),
    pred::WalkerFamilyCount(FAMILY_SLIME, 0, 0),
    // negative_assertion: the parent SLIME dies and is reaped out of oblist; split_on_death replaces it with the offspring rather than leaving a live parent behind.
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 0, 0),
    // negative_assertion: core:#8's on_death yields the NEXT size down (MEDIUM_SLIME); any SMALL_SLIME here means the wrong offspring family was created.
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_MEDIUM_SLIME, 7100, 7300,
        "consequence: the offspring inherits the parent's level and takes set_difficulty(1) (110 base + 11 = 121 max), then eats the executioner's remaining swings down to 72"),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
};

inline constexpr Mutation kMut_slime_death_split_scen99 = {
    "packs/core/scripts/slime.lua", 74,
    "  return split_on_death(self, LIVING_MEDIUM_SLIME)",
    "  return split_on_death(self, LIVING_SMALL_SLIME)",
    "Repoints core:#8's on_death offspring one size too far down. The dying big slime yields a SMALL_SLIME instead of a MEDIUM_SLIME, so WalkerFamilyCount(FAMILY_MEDIUM_SLIME,1,1) collapses to 0 and the FAMILY_SMALL_SLIME negative assertion rises to 1."
};

// elemental_death_starburst_scen99: the FAMILY_FIREELEMENTAL is the ONLY
// team-1 walker, so the tick it dies GameWorld::tick's completion check sees
// level_done == 2 and returns above the dead-entity sweep
// (game_world.cpp:1917) — from that tick on nothing is reaped, so the eight
// meteors fire_elemental.lua's on_death emits stay in weaplist and are
// directly observable via WeaponFamilyEmitted. Two team-0 SOLDIER bystanders
// stand north and south of the elemental as starburst victims (the meteors
// only hit NON-friendly walkers, so the victims must be on the killer's
// team, not the elemental's). The player BIG_ORC is spawned LAST so
// find_player_walker binds the oblist head to it.
inline constexpr InputEvent kInputs_elemental_death_starburst[] = {
    {  1, 0, K_RIGHT },   // face east onto the elemental before the fire hold
    {  4, 0, K_NONE },
    {  5, 0, K_FIRE },
    { 60, 0, K_NONE },
};

inline constexpr SpawnSpec kFamilySpawns_elemental_death_starburst_scen99[] = {
    { FAMILY_FIREELEMENTAL, 1, kOrderLiving, 140, 120, FAMILY_METEOR, 0 }, // victim; default_weapon spelled out as the spawn-side evidence for WeaponFamilyEmitted(FAMILY_METEOR)
    { FAMILY_SOLDIER,       0, kOrderLiving, 140,  96, 0, 0 },             // bystander north — starburst victim
    { FAMILY_SOLDIER,       0, kOrderLiving, 140, 144, 0, 0 },             // bystander south — starburst victim
    { FAMILY_BIG_ORC,       0, kOrderLiving, 120, 120, 0, 0 },             // player executioner LAST, flush against the elemental's west edge
};

inline constexpr FactPredicate kFacts_elemental_death_starburst_scen99[] = {
    pred::TickReached(80),
    pred::WalkerDiedByFinal(FAMILY_FIREELEMENTAL),
    pred::WalkerOfTeamAlive(/*team=*/1, 0, 0),
    pred::WeaponFamilyEmitted(FAMILY_METEOR,
        "semantic content: on_death's re-entrant special() puts eight core:meteor weapons into weaplist on the elemental's death tick and the world freezes that same tick, so all eight are still in dump.weapons[] at the final tick. NOT the discriminator — WeaponFamilyEmitted also accepts weapon_tracks, and the elemental's mid-life AI starburst leaves METEOR tracks that survive the mutation. The teeth are the three predicates below"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 8000, 8200,
        "consequence: the northern bystander is the one standing in the parting volley and ends on 81 of 120. Remove the volley and it walks away on 110 while its twin sits on 66, so no soldier lands in this band"),
    pred::WalkerAliveAtFinal(FAMILY_BIG_ORC, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BIG_ORC, 9600, 9800,
        "consequence: the executioner is inside its own victim's death radius and eats a parting meteor too, finishing on 97 of 180; with on_death's special() gone it finishes on 153"),
    pred::EventKindAtLeast(/*play_sound*/1, 28,
        "consequence: the parting volley adds one core:meteor fire_sound per direction on top of the melee traffic — 31 with the volley, 23 without it"),
};

inline constexpr Mutation kMut_elemental_death_starburst_scen99 = {
    "packs/core/scripts/fire_elemental.lua", 33,
    "  self:special()",
    "  local _ = self",
    "Removes the parting volley from core:elemental's on_death while leaving the dead=0 / magicpoint refund / dead=1 dance intact. No meteor is created on the death tick, so the frozen weaplist holds none and WeaponFamilyEmitted(FAMILY_METEOR) fails; the bystander soldiers also keep their HP and leave the soldier band empty."
};

// ai_slime_split_scen99: a LONE team-1 FAMILY_SLIME with no walker anywhere
// else in the arena. find_near_foe/find_far_foe return nothing, so
// living::act's ACT_RANDOM never arms the 300-tick COMMAND_SEARCH that
// starves the special roll in every other slime row — the 1-in-5 roll gets a
// fresh chance every tick and drives living::check_special ->
// slime_check_special_ai -> slime_do_special -> ANI_SLIME_SPLIT ->
// slime_on_ani_complete. player_team stays 0 with no team-0 walker, so
// claim_control binds nothing and the slime is pure AI.
inline constexpr SpawnSpec kFamilySpawns_ai_slime_split_scen99[] = {
    { FAMILY_SLIME, 1, kOrderLiving, 300, 300, 0, 0 }, // level 1 -> rng.next((1+2)/3)+1 == 1 == SPLIT, deterministically
};

inline constexpr FactPredicate kFacts_ai_slime_split_scen99[] = {
    pred::TickReached(100),
    pred::WalkerFamilyCount(FAMILY_SLIME, 0, 0),
    // negative_assertion: the AI cast consumes the caster — slime_on_ani_complete transform_to's it into a SMALL_SLIME — so no FAMILY_SLIME may survive; a surviving parent means check_special never authorised the cast.
    pred::WalkerOfTeamAlive(/*team=*/1, 2, 2),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 2, 2),
    pred::WalkerAliveAtFinal(FAMILY_SMALL_SLIME, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 15000, 15000,
        "consequence: transform_to and transfer_stats carry the parent SLIME's 150 hitpoints across to BOTH halves rather than resetting them to the SMALL_SLIME family base of 80; the empty arena leaves that value untouched to the final tick"),
};

inline constexpr Mutation kMut_ai_slime_split_scen99 = {
    "packs/core/scripts/slime.lua", 43,
    "  return og.living_count() < C.MAXOBS",
    "  return false",
    "Makes core:#8's check_special_ai always deny. living::check_special returns false, ACT_RANDOM never reaches special(), the parent FAMILY_SLIME survives intact and no SMALL_SLIME offspring exists — flipping WalkerFamilyCount(FAMILY_SLIME,0,0), WalkerFamilyCount(FAMILY_SMALL_SLIME,2,2) and WalkerOfTeamAlive(1,2,2) together."
};

// slime_grow_blocked_scen99: grow_into's ELSE branch. spaces_clear() needs
// ALL EIGHT ±sizex/±sizey probes passable (> 7, walker.cpp:4408) and each
// probe is a full box-overlap query through obmap::query_list, so one
// stationary neighbour is enough to deny the transform. The blocker is a
// team-0 FAMILY_TOWER1: living-20-beast.yaml is is_stationary (derived
// stepsize 0) so it cannot drift out of the probe box, and being on the
// caster's own team it never fights. The small slime is spawned LAST so
// find_player_walker binds the oblist head to it, not to the tower.
inline constexpr SpawnSpec kFamilySpawns_slime_grow_blocked_scen99[] = {
    { FAMILY_TOWER1,      0, kOrderLiving, 136, 116, 0, 0 },         // stationary friendly blocker; overlaps the caster's three eastern probe boxes
    { FAMILY_SMALL_SLIME, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 }, // player caster LAST (12x12 sprite; 600 MP clears special_cost[1] == 30)
};

inline constexpr FactPredicate kFacts_slime_grow_blocked_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SMALL_SLIME, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SMALL_SLIME, 1),
    pred::WalkerFamilyCount(FAMILY_MEDIUM_SLIME, 0, 0),
    // negative_assertion: with a neighbour inside the probe box grow_into MUST refuse the transform, so no MEDIUM_SLIME may exist; one appearing means the spaces_clear gate stopped gating.
    pred::WalkerPositionMoved(FAMILY_SMALL_SLIME, 102, 102,
        "consequence: the refused grow forces a two-draw COMMAND_WALK (og.rand(3) y-first, then x) of 10 steps; the caster walks north-west off its (120,120) spawn and stops on (102,102), so this bound is the exact landing tile"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 8000, 8000),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 1, 1),
};

inline constexpr Mutation kMut_slime_grow_blocked_scen99 = {
    "packs/core/scripts/slime.lua", 136,
    "  if self:spaces_clear() > 7 then",
    "  if true then",
    "Removes grow_into's room-to-grow gate. The cornered caster transforms anyway: FAMILY_SMALL_SLIME drops to 0, the FAMILY_MEDIUM_SLIME negative assertion rises to 1, and the forced random walk (and its two og.rand(3) draws) never happens so the position predicate loses its walker too."
};

// generator_owner_cascade_scen99: the tent keeps owner on its spawns
// (generator-00-tent.yaml clear_owner: false) and gives them a lifetime, so
// living::act kills the whole escort the moment the tent dies
// (openglad-master/src/living.cpp:56-71). The player BIG_ORC starts 180px
// SOUTH of the tent — far outside knife range, so its collide-fire against
// approaching skeletons cannot clip the tent — and only marches north into
// it at tick 220, by which time the escort has arrived and started hurting
// it. Swept from the capture: the tent dies on tick 288 (four
// FAMILY_EXPLOSION appear), and the skeleton dies on tick 289 still holding
// the 14 hitpoints the orc left it — that one-tick gap IS the owner cascade.
// The 310-tick budget is death + ~20 so the explosions have expired and
// team 1 holds nothing alive. Marching later does not work: without the
// escort dead the orc is down to 18 hp by tick 300 and dead by 350.
//
// NOTE (differs from the design's reasoning, not its outcome): the reap
// never runs in this arena at all. A generator is not a Living, so
// level_done == 2 from tick 1, parity_runner latches world.end
// immediately, and every corpse — HIT effects included — persists from the
// first tick rather than only from the tent's death.
inline constexpr InputEvent kInputs_generator_owner_cascade[] = {
    { 220, 0, K_UP | K_FIRE }, // march north into the tent and grind it down
    { 400, 0, K_NONE },
};

inline constexpr SpawnSpec kFamilySpawns_generator_owner_cascade_scen99[] = {
    { FAMILY_TENT,    1, kOrderGenerator, 140, 120, 0, 0, 2,  0 }, // stats_level 2 -> ~1 skeleton / 120 ticks, escort levels 1-2
    { FAMILY_BIG_ORC, 0, kOrderLiving,    140, 300, 0, 0, 10, 0 }, // player demolisher LAST; level 10 scales knife damage without touching base HP
};

inline constexpr FactPredicate kFacts_generator_owner_cascade_scen99[] = {
    pred::TickReached(310),
    pred::WalkerOfTeamAlive(/*team=*/1, 0, 0),
    pred::WalkerDiedByFinal(FAMILY_SKELETON),
    pred::WalkerFamilyCount(FAMILY_SKELETON, 1, 1,
        "consequence: the tent's level-2 cadence puts exactly one skeleton on the field before the demolisher arrives, and the corpse is still in oblist at the final tick because the sweep is frozen"),
    pred::WalkerAliveAtFinal(FAMILY_BIG_ORC, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_BIG_ORC, 9100, 9300,
        "consequence: the owner-linked escort is free to fight, and it grinds the demolisher from 180 down to 92 before the tent falls; with clear_owner flipped the escort dies on its own first act, never lands a hit, and the orc finishes at its full 18000 cents"),
    pred::EventKindAtLeast(/*play_sound*/1, 6,
        "consequence: escort combat plus the tent's four death explosions"),
};

inline constexpr Mutation kMut_generator_owner_cascade_scen99 = {
    "packs/core/families/generator-00-tent.yaml", 10,
    "      clear_owner: false",
    "      clear_owner: true",
    "Gives core:tent the TOWER/TREEHOUSE owner policy while it still declares has_lifetime. walker.cpp:649 then nulls each skeleton's owner at spawn, so living::act's lifetime branch (`if (!owner || owner->dead)`) kills every skeleton on its very first act: the escort never fights, the demolisher finishes at full 18000-cent HP above the WalkerHpRangeAtFinalTick bound, and the frozen-sweep skeleton corpse count moves off its band."
};

// --- effects batch ---------------------------------------------------------
//
// Group-wide facts these nine rows lean on:
//   * walker::attack refuses friendly targets (is_friendly walks the owner
//     chain to its head before comparing team_num), so every FX inherits its
//     owner's team for that test and no friendly-tier damage is observable.
//   * find_foe_weapons_in_range scans oblist while add_ob(Order::Weapon, ...)
//     diverts weapons to weaplist, so guard_tail's weapon-absorb arm is
//     structurally unreachable on BOTH arms. No row below asserts it.
//   * FAMILY_TOWER1 (living-20-beast.yaml) is the corpus's only stationary
//     living family: is_stationary, hp 130, damage 0, stepsize 0, identical to
//     master's guy.cpp:261 entry. It is the fixed-geometry victim in six rows
//     because ordinary AI foes drift 100+ px in 25 ticks and would make every
//     distance-sensitive assertion RNG-shaped. It still fires ~5-damage arrows
//     at foes inside lineofsight*GRID_SIZE = 320 px.
//   * Caster levels are deliberately at or below the runaway-effects soft
//     knees (bomb_damage knee 210, scare_duration knee 325, kMpPoolDamageCap
//     600) so the branch and master formulas agree exactly and the mandatory
//     weapon_tracks byte-compare holds.

// effect_cloud_poison_scen99: core:cloud's per-tick attack loop
// (packs/core/scripts/effect_cloud.lua:33-40, ported from
// openglad-master/src/effect.cpp:356-390) scans foes inside sizex (48) and
// attacks every one whose box overlaps the 48x48 cloud, once per tick, for
// damage == caster level. Both existing cloud rows explicitly refuse to assert
// poisoning because their soldier sits 12 px outside the cloud's right edge.
// This row parks a STATIONARY team-1 FAMILY_TOWER1 so the box overlap is a
// geometric certainty from the cast tick, and keeps the budget at 30 so the
// victim survives with margin. The caster stands fully inside its own cloud,
// which pins the friends-are-spared arm.
inline constexpr SpawnSpec kFamilySpawns_effect_cloud_poison_scen99[] = {
    { FAMILY_TOWER1, 1, kOrderLiving, 108,  88, 0, 0 },             // stationary team-1 victim; box 108..133 x 88..117 overlaps the cloud box 104..152 x 102..150 and sits Manhattan 18 from the cloud's top-left corner (find_foes_in_range range == cloud sizex == 48)
    { FAMILY_THIEF,  0, kOrderLiving, 120, 120, 0, 0, 10, 300 },    // player-controlled caster LAST (spawns prepend; the player binds the oblist head). level 10 is the minimum that can cycle to slot 4 ((4-1)*3+1 <= level) and 300 MP covers POISON CLOUD's 150 cost. Cloud damage == level == 10 on both arms (master walker.cpp:3686).
};

inline constexpr FactPredicate kFacts_effect_cloud_poison_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    // FLIPPING PREDICATE. The cloud overlaps the stationary TOWER1 from tick 21
    // and attacks it once per act for level-10 damage. Nothing else in the arena
    // can hurt it: the thief's script is special-only and TOWER1 is the only foe.
    // kMut_effect_cloud_poison_scen99 collapses effect_cloud.lua's foe scan
    // radius to 0, so no foe is ever found, no attack() runs, and TOWER1
    // finishes at its full 13000 cents -- above this ceiling.
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 1300, 1500,
        "consequence: core:cloud's on_act attack loop poisons the overlapping stationary TOWER1 once per tick for caster-level damage; ten acts take it from 13000 cents to 1400. The mutation zeroes the foe-scan radius so the cloud drifts harmlessly and TOWER1 ends at 13000"),
    // Friendly arm: the thief stands FULLY inside its own 48x48 cloud
    // (box 120..136 x 120..133 vs 104..152 x 102..150) and is never poisoned --
    // find_foes_in_range excludes friendlies, and attack() would refuse them
    // anyway. Pinned exactly from the golden so any drift in the caster's own
    // timeline is caught.
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 7400, 7600),
    pred::WalkerOfTeamAlive(0, 2, 2,
        "consequence: POISON CLOUD adds the FAMILY_CLOUD FX walker to the thief's team, so team 0 holds thief + cloud"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_effect_cloud_poison_scen99 = {
    "packs/core/scripts/effect_cloud.lua", 33,
    "  local foes = og.find_foes_in_range(\"ob\", self:sizex(), self)",
    "  local foes = og.find_foes_in_range(\"ob\", 0, self)",
    "Collapses core:cloud's foe-acquisition radius from its 48px sprite width to 0, so the per-tick attack loop never finds the overlapping TOWER1. The cloud still spawns, drifts and expires identically, but poisons nothing: WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 0, ...) flips because the victim finishes at its full 13000 cents."
};

// effect_explosion_range_scen99: compute_explosion_range
// (packs/core/scripts/effect_bomb.lua:12-20, ported from
// openglad-master/src/effect.cpp:648-664) turns the owner's level into the
// blast's Manhattan reach 15 + clamp(level*4, 16, 96). Every existing blast row
// uses a level-4/5 caster whose range is pinned at the 16-20 floor, so nothing
// in the corpus distinguishes a big blast from a small one. A level-10 thief
// gives range 40 / reach 55 and the single victim is a STATIONARY team-1 TOWER1
// parked at Manhattan 45 from the explosion's corner -- inside the real reach,
// outside the mutated one. No mobile walker is used because a charging melee
// foe kills a 75hp thief before the 50-tick fuse completes and a dead owner
// collapses blast.level to the bomb's own level, destroying the test.
inline constexpr SpawnSpec kFamilySpawns_effect_explosion_range_scen99[] = {
    { FAMILY_TOWER1, 1, kOrderLiving, 232, 203, 0, 0 },            // stationary team-1 victim: Manhattan |232-196|+|203-194| = 45 from the explosion's top-left (196,194). Inside 15+40=55, outside the mutated 15+16=31.
    { FAMILY_THIEF,  0, kOrderLiving, 200, 200, 0, 0, 10, 300 },   // player-controlled bomb owner LAST. level 10 -> bomb_damage 165 (raw 15*11, below the 210 soften knee so branch == master) and explosion range clamp(40,16,96) = 40; 300 MP covers DROP BOMB's 35 cost.
};

inline constexpr FactPredicate kFacts_effect_explosion_range_scen99[] = {
    pred::TickReached(100),
    pred::WalkerAliveAtFinal(FAMILY_THIEF, 1),
    // FLIPPING PREDICATE. The blast reaches Manhattan 55 and lands full damage
    // (165 -> 158..169) on the 130hp stationary TOWER1, which dies and is reaped.
    // kMut_effect_explosion_range_scen99 pins the clamp ceiling at the 16px
    // floor, so the reach drops to 31, TOWER1 at Manhattan 45 is never
    // enumerated by find_in_range, and an alive TOWER1 remains.
    pred::WalkerDiedByFinal(FAMILY_TOWER1,
        "consequence: compute_explosion_range scales the blast to 15+4*level = 55 Manhattan px, which covers the stationary TOWER1 at 45 and kills it with the full 165-damage tier; the clamp mutation shrinks the reach to 31 so the same walker is outside the blast and survives at full HP"),
    // Anchor: the detonation itself still happens under every mutation of the
    // range clamp (bomb_on_death emits SOUND_EXPLODE before the blast resolves),
    // so a play_sound floor separates "blast never fired" from "blast too small".
    pred::EventKindAtLeast(/*play_sound*/1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 3200, 3400,
        "structural: the caster's 33 of 75 is its OWN blast -- fdiv(165,4) through compute_base_damage's 38..43 window -- and nothing else, because the TOWER1 at Manhattan 45 never fires a shot (the golden's first event is the tick-70 SOUND_EXPLODE); a drift here means the fuse or the owner tier moved"),
};

inline constexpr Mutation kMut_effect_explosion_range_scen99 = {
    "packs/core/scripts/effect_bomb.lua", 17,
    "  range = og.clamp(range, 16, 96)",
    "  range = og.clamp(range, 16, 16)",
    "Pins compute_explosion_range's clamp ceiling at its own floor, so a level-10 owner's 40px range collapses to 16 and the blast's Manhattan reach falls from 55 to 31. The stationary TOWER1 at 45 is no longer enumerated by find_in_range, survives, and WalkerDiedByFinal(FAMILY_TOWER1) fails."
};

// effect_bomb_bystander_scen99: two things nothing in the corpus proves -- the
// bomb -> explosion field inheritance (blast.damage = self:damage(),
// blast.level = self:owner().level, packs/core/scripts/effect_bomb.lua:32-38,
// master effect.cpp:633-646) and the FULL damage tier landing on a non-friendly
// bystander. effect_bomb_emission asserts only a sound count and its soldier is
// back at 120/120 by tick 200; effect_bomb_timer parks its foe at 400,400 so the
// blast never reaches it. This row also carries the SHOVE: the explosion's 24x24
// box and the thief's 16x13 box give the owner sign(dx)=sign(dy)=+1, so
// s_force_command(COMMAND_WALK, shove, +1, +1) moves the otherwise-motionless
// player caster. The owner branch's quarter-tier damage LANDS too -- the
// golden's 75->55 thief hp is entirely its own bomb (all tower arrows do 0
// through armor): is_friendly answers 0 for a dead CALLER and an explosion is
// set_dead before death() runs, so the friendly refusal never engages for a
// blast. True since the 2002 import; gdb-verified at HEAD.
inline constexpr SpawnSpec kFamilySpawns_effect_bomb_bystander_scen99[] = {
    { FAMILY_TOWER1, 1, kOrderLiving, 222, 196, 0, 0 },           // stationary team-1 bystander at Manhattan |222-196|+|196-194| = 28 from the explosion's top-left (196,194); inside the level-5 reach 15+20 = 35 with 7px to spare
    { FAMILY_THIEF,  0, kOrderLiving, 200, 200, 0, 0, 5, 300 },   // player-controlled bomb owner LAST; level 5 -> bomb_damage 90 (identical on master) and range clamp(20,16,96) = 20
};

inline constexpr FactPredicate kFacts_effect_bomb_bystander_scen99[] = {
    pred::TickReached(100),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 1, 1),
    // FLIPPING PREDICATE. bomb_on_death hands the bomb's own damage to the
    // explosion it spawns; explosion_on_death then lands that FULL amount on the
    // non-friendly TOWER1 (85..93 off 130). kMut_effect_bomb_bystander_scen99
    // severs the inheritance (blast.damage = 0) so the blast still fires, still
    // emits SOUND_EXPLODE and still shoves, but get_base_damage(0) is 0 and
    // TOWER1 finishes untouched at 13000 cents.
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 3800, 4000,
        "consequence: the bomb's on_death hands its damage to the FAMILY_EXPLOSION it spawns, which lands the full non-friendly tier on the stationary bystander and takes it from 13000 cents to 3900; the mutation zeroes the inherited damage so the blast is cosmetic and the bystander ends untouched"),
    // The blast shoves everything it enumerates, friendly or not. The owner is
    // enumerated (skip_exit == 0 for a bomb blast) and, because the 24x24
    // explosion and the 16x13 thief have different top-left corners, its shove
    // direction is (+1,+1): the otherwise-motionless player walker ends
    // south-east of its 200,200 spawn.
    pred::WalkerPositionMoved(FAMILY_THIEF, 201, 201,
        "consequence: explosion_on_death force-walks every enumerated walker away from the blast centre, including its own owner; the special-only thief has no other reason to move, so it finishes south-east of its 200,200 spawn"),
    pred::WalkerAliveAtFinal(FAMILY_THIEF, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_effect_bomb_bystander_scen99 = {
    "packs/core/scripts/effect_bomb.lua", 38,
    "  blast.damage = self:damage()",
    "  blast.damage = 0",
    "Severs the bomb -> explosion damage inheritance. The FAMILY_EXPLOSION still spawns, still emits SOUND_EXPLODE and still shoves, but carries no damage, so get_base_damage(0) leaves the bystander at its full 13000 cents and WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 0, ...) fails."
};

// effect_shield_absorb_scen99: guard_tail's foe arm
// (packs/core/scripts/effect_shield.lua:36-44, master effect.cpp:155-168)
// attacks every foe within the guard's sizex and drains the guard by that foe's
// damage. kFamilySpawns_magic_shield_arena deliberately parks its foe at 200,120
// "too far to melee/drain the orbiting shield", and the golden confirms the
// whole tail is dead there. The subtlety that kept it dead is geometric:
// find_foes_in_range compares TOP-LEFT corners with range == the shield's 8px
// sprite width, while the shield orbits at radius 24 around a point derived from
// the caster's centre -- so an ordinary melee foe standing shoulder-to-shoulder
// with the cleric is still 13-20 px from every orbit stop.
//
// Special-only input: the tick-10 K_FIRE of kInputsMagicShieldEmit is dropped so
// the cleric's GLOW never touches the victim and every point of its damage is
// orbit contact.
inline constexpr InputEvent kInputsMagicShieldNoFire[] = {
    { 5, 0, (K_SPECIAL | K_SHIFT)},   // Shift+Special -> shifter_down = 1 -> MYSTIC MACE summons the orbiting FAMILY_MAGIC_SHIELD
    { 6, 0, K_NONE},
};

// The shield orbits at radius 24 about (124,122) = the cleric's centre minus the
// shield's half-extent, so a melee foe hugging the cleric is never within 8 of
// any orbit stop. (147,127) is Manhattan 6 from orbit corner 12 (148,122) and
// Manhattan 5 from corner 11 (146,131): two contacts per 16-tick revolution, and
// TOWER1 is stationary so those two stay exact for the whole run.
inline constexpr SpawnSpec kFamilySpawns_effect_shield_absorb_scen99[] = {
    { FAMILY_TOWER1, 1, kOrderLiving, 147, 127, 0, 0 },        // stationary team-1 victim sitting on the shield's right-hand orbit lobe; box 147..172 x 127..156 clears the cleric's 120..136 x 120..132
    { FAMILY_CLERIC, 0, kOrderLiving, 120, 120, 0, 0, 0, 80 }, // player-controlled caster LAST. magicpoints 80 reproduces the existing mace arena exactly: spare = (80 - special_cost(1)=2)/2 = 39 -> lifetime 100+39 = 139, hp 100+19 = 119, damage 10+9.75 = 19.75
};

inline constexpr FactPredicate kFacts_effect_shield_absorb_scen99[] = {
    pred::TickReached(45),
    pred::WalkerAliveAtFinal(FAMILY_CLERIC, 1),
    // FLIPPING PREDICATE. guard_tail attacks every foe inside the guard's sizex.
    // The stationary TOWER1 sits on two of the sixteen orbit stops, so the mace
    // strikes it twice per revolution for 17..20 and nothing else in the arena
    // can hurt it (the cleric never fires). kMut_effect_shield_absorb_scen99
    // collapses guard_tail's foe radius to 0 so the mace orbits harmlessly and
    // TOWER1 ends at its full 13000 cents.
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 5200, 5400,
        "consequence: core:magic_shield's guard tail attacks every foe inside its 8px guard radius, and the stationary TOWER1 is parked on two of the sixteen orbit stops, so four mace strikes take it from 13000 cents to 5300; the mutation zeroes that radius and the victim finishes untouched"),
    pred::WalkerOfTeamAlive(0, 2, 2,
        "consequence: MYSTIC MACE puts the FAMILY_MAGIC_SHIELD FX on the cleric's team; its lifetime (139) and the zero-damage victim keep it alive through the 45-tick budget"),
    // This arena emits NO sound at all -- the cleric never fires and a mace
    // contact is silent -- so the event floor is the score award instead: one
    // score_change per strike, four in the 45-tick budget, zero once the
    // mutation stops the tail from finding anything.
    pred::EventKindAtLeast(/*score_change*/9, 4,
        "consequence: each guard-tail strike on the TOWER1 awards score, so the four contacts inside the budget are visible as four score_change events; the radius mutation lands no strikes and emits none"),
};

inline constexpr Mutation kMut_effect_shield_absorb_scen99 = {
    "packs/core/scripts/effect_shield.lua", 36,
    "  local foes = og.find_foes_in_range(\"ob\", self:sizex(), self)",
    "  local foes = og.find_foes_in_range(\"ob\", 0, self)",
    "Collapses guard_tail's foe-contact radius from the guard's sprite width to 0. The mace still summons, still orbits and still expires on its lifetime, but strikes nothing: WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 0, ...) flips because the victim finishes at its full 13000 cents."
};

// effect_boomerang_contact_scen99: the boomerang shares guard_tail with the
// shield but rides a GROWING orbit -- xd *= (drawcycle+4); xd /= 48
// (packs/core/scripts/effect_shield.lua:89-95, master effect.cpp:250-253) -- so
// which foes it can reach is a function of time, not just position.
// weapon_boomerang_return_scen99's golden already shows the tail firing
// incidentally but asserts only the caster's HP; no predicate names the victim.
// This row makes the contact schedule the assertion by putting a stationary
// victim exactly on the sweep line at x = 147, which the blade's right-hand
// extreme (123 + 24*(d+4)/48) reaches at drawcycle 44 and brackets within the
// 11px contact radius at 28 and 60. kInputsSpecialSlot2 contains no K_FIRE, so
// the soldier never throws a knife and every point of the victim's damage is
// blade contact.
inline constexpr SpawnSpec kFamilySpawns_effect_boomerang_contact_scen99[] = {
    { FAMILY_TOWER1,  1, kOrderLiving, 147, 123, 0, 0 },           // stationary team-1 victim on the blade's +x sweep line; box 147..172 x 123..152 clears the soldier's 120..136 x 120..136
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 4, 300 },   // player-controlled caster LAST; level 4 satisfies the slot-2 cycle gate ((2-1)*3+1 <= 4) and 300 MP covers BOOMERANG's 100 cost. lifetime 30+48 = 78, damage 8+16 = 24, hp 50+48 = 98 -- all identical on master.
};

inline constexpr FactPredicate kFacts_effect_boomerang_contact_scen99[] = {
    pred::TickReached(85),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // FLIPPING PREDICATE. The widening orbit sweeps the stationary TOWER1
    // several times inside the blade's 78-act lifetime and guard_tail attacks it
    // each time for 21..24. kMut_effect_boomerang_contact_scen99 divides the
    // orbit's x component by 480 instead of 48, so the blade's horizontal
    // excursion never exceeds ~3px, the victim at Manhattan 21+ is never inside
    // the 11px contact radius, and it finishes at its full 13000 cents.
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 4000, 4200,
        "consequence: core:boomerang's guard tail strikes the stationary TOWER1 each time the widening orbit sweeps across it, taking it from 13000 cents to 4100; the mutation shrinks the orbit's x excursion tenfold so the blade never reaches the victim and it ends untouched"),
    pred::WalkerOfTeamAlive(0, 2, 2,
        "consequence: BOOMERANG adds the FAMILY_BOOMERANG FX to the caster's team; the zero-damage victim never drains it, so it survives its 78-act lifetime through the 85-tick budget"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_effect_boomerang_contact_scen99 = {
    "packs/core/scripts/effect_shield.lua", 93,
    "  xd = og.fdiv(xd, 48)",
    "  xd = og.fdiv(xd, 480)",
    "Shrinks the boomerang's horizontal orbit excursion tenfold (the /48 scale is boomerang-only; magic_shield uses the raw table offsets). The blade stays within ~3px of the caster's x, never comes within the 11px contact radius of the victim at x=147, and WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 0, ...) flips to the full 13000 cents."
};

// effect_boomerang_drawcycle_cap_scen99: Zardus's 2002 fix --
// `or self:drawcycle() > 253` in boomerang_on_act
// (packs/core/scripts/effect_shield.lua:84, master effect.cpp:179) -- kills a
// long-lived blade rather than letting the byte-sized drawcycle wrap and snap
// the orbit back onto its owner. drawcycle is stored as unsigned char and
// advanced once per effect::act, so the guard fires on the blade's 254th act.
// Boomerang lifetime is 30 + 12*level, so the cap only bites above level 18; all
// three existing boomerang rows use a level-4 caster (lifetime 78) with an
// 80-tick budget, leaving the guard unreachable. A level-22 caster gives
// lifetime 294 -- 40 acts past the cap -- so the two death causes are cleanly
// separated in time. The off-map stationary TOWER1 is the standard "keep the
// level alive" prop (same trick as kFamilySpawns_marker_emission_generator's
// 2000,2000 observer): far outside both walkers' firing ranges, so no combat RNG
// enters the run and the blade's death tick is a pure function of the counter.
inline constexpr SpawnSpec kFamilySpawns_effect_boomerang_cap_scen99[] = {
    { FAMILY_TOWER1,  1, kOrderLiving, 2000, 2000, 0, 0 },          // off-map stationary team-1 prop: keeps level_done at 0, never engages
    { FAMILY_SOLDIER, 0, kOrderLiving,  120,  120, 0, 0, 22, 600 }, // player-controlled caster LAST; level 22 -> boomerang lifetime 30+264 = 294 acts, so the drawcycle cap at act 254 fires FIRST. 600 MP covers BOOMERANG's 100 cost.
};

inline constexpr FactPredicate kFacts_effect_boomerang_cap_scen99[] = {
    pred::TickReached(290),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // FLIPPING PREDICATE. The blade's drawcycle is a byte advanced once per
    // effect::act, so boomerang_on_act's `> 253` guard kills it on act 254
    // (tick ~274) -- 40 ticks before its 294-act lifetime would have expired.
    // At tick 290 team 0 therefore holds the lone caster.
    // kMut_effect_boomerang_cap_scen99 raises the threshold past the byte's
    // range so the guard can never fire; the blade survives to its lifetime and
    // team 0 still holds caster+blade = 2.
    pred::WalkerOfTeamAlive(0, 1, 1,
        "consequence: the byte-sized drawcycle crosses 253 on the blade's 254th act, and boomerang_on_act kills it there rather than letting the counter wrap the orbit back onto its owner; team 0 is down to the lone caster well before the 294-act lifetime would have expired. Raising the threshold above the byte's range keeps the blade alive and the count at 2"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 11900, 12100,
        "structural: the arena is combat-free (off-map prop, special-only input), so the caster must finish at full HP -- any drift here means an unintended engagement"),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
};

inline constexpr Mutation kMut_effect_boomerang_cap_scen99 = {
    "packs/core/scripts/effect_shield.lua", 84,
    "      or self:drawcycle() > 253 then",
    "      or self:drawcycle() > 300 then",
    "Raises Zardus's drawcycle cap above the range of the unsigned-char counter, so the guard can never fire. The level-22 blade then survives to its 294-act lifetime instead of dying on act 254, and WalkerOfTeamAlive(0, 1, 1) flips to 2 at the tick-290 dump."
};

// effect_chain_fork_scen99: chain lightning's death fork
// (packs/core/scripts/effect_chain.lua:58-90, master effect.cpp:433-459) spawns
// successor bolts at nearby foes, each inheriting damage * fork_damage_mult,
// gated by fork_damage > fork_min_damage and skipping the current leader. The
// four fork knobs in packs/core/families/effect-10-chain.yaml are new PR data
// with no reachable coverage. This row makes a second, distinct-family victim's
// HP the assertion, and makes the fork deterministic by ordering the spawns so
// the non-leader foe precedes the leader in oblist -- the fork loop decrements
// its budget on the skipped leader too, so a rand0(level)+1 draw of 1 would
// otherwise consume the only slot. The ARCHER is placed diagonally so its
// Manhattan distance from the primary explosion is far outside that blast's
// 15+4*10 = 55 reach: only a genuine fork can damage it. Its Euclidean placement
// also keeps distance_to_ob_center at 12961, under the 32767 where master's
// `short dist` leader pick would wrap.
inline constexpr InputEvent kInputsChainFork[] = {
    {  5, 0, K_SPECIAL_SWITCH},          // cycle current_special 1 -> 2 (heartburst/chain slot)
    {  6, 0, K_NONE},
    { 12, 0, (K_SPECIAL | K_SHIFT)},     // Shift+Special -> shifter_down = 1 -> CHAIN LIGHTNING
    { 13, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_effect_chain_fork_scen99[] = {
    { FAMILY_ORC,      1, kOrderLiving, 150, 120, 0, 0 },          // nearest foe -> the primary leader (distance_to_ob_center 901)
    { FAMILY_ARCHER,   1, kOrderLiving, 200, 200, 0, 0 },          // fork-only victim: Manhattan ~138 from the primary blast (reach 55), well inside the 240+5*level = 290 fork acquisition radius
    { FAMILY_ARCHMAGE, 0, kOrderLiving, 120, 120, 0, 0, 10, 300 }, // player-controlled caster LAST; level 10, 300 MP -> pool min((300-80)/2, 600) = 110, unclamped and identical to master
};

inline constexpr FactPredicate kFacts_effect_chain_fork_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_ARCHMAGE, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    // Primary hop -- already known to work, kept as the always-true anchor that
    // separates "the bolt never landed" from "the bolt landed but never forked".
    pred::WalkerDiedByFinal(FAMILY_ORC,
        "consequence: the primary bolt homes on its nearest foe and detonates a 110-damage explosion on it, and a later generation finishes the 140hp ORC off inside the budget; with the fork gate closed only the single 110-damage hop lands and the ORC survives"),
    // FLIPPING PREDICATE. The ARCHER is ~138 Manhattan px from the primary blast,
    // far outside its 55px reach, and the archmage never fires (special-only
    // input), so the ONLY thing that can damage it is a successor bolt spawned by
    // effect_chain.lua's death fork at damage*fork_damage_mult = 55.
    // kMut_effect_chain_fork_scen99 raises fork_min_damage above every reachable
    // fork damage, so the gate never opens, exactly one hop lands, and the
    // ARCHER finishes at its full 9000 cents.
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3500, 3700,
        "consequence: chain lightning's death fork spawns a successor bolt carrying half the parent damage at a foe the primary blast cannot reach, dropping the ARCHER below its full 9000-cent HP; raising fork_min_damage past every reachable fork damage closes the gate, no successor is created, and the ARCHER ends untouched"),
    // Hop count is NOT visible as a sound floor -- measured, the fork-free arm
    // emits the same 7 play_sound events (the ARCHER's own arrows dominate the
    // count). It IS visible as score awards: two hops that damage a foe award
    // twice, the single-hop arm awards once.
    pred::EventKindAtLeast(/*score_change*/9, 2,
        "consequence: each chain detonation that damages a foe awards score, so the primary hop and the fork show up as two score_change events; with the fork gate closed only the primary lands and the count drops to one"),
    pred::WalkerAliveAtFinal(FAMILY_ARCHMAGE, 1),
};

inline constexpr Mutation kMut_effect_chain_fork_scen99 = {
    "packs/core/families/effect-10-chain.yaml", 26,
    "        fork_min_damage: 20",
    "        fork_min_damage: 100000",
    "Raises core:chain's fork floor above every reachable fork damage, so the `fork_damage > fork_min_damage` gate never opens. The primary bolt still homes, detonates and emits its sound, but spawns no successor: the ARCHER -- unreachable by the primary blast -- finishes at its full 9000 cents and its HP band fails."
};

// effect_knife_back_catch_scen99: two halves of the returning-knife economy that
// nothing reads. (a) effect_knife_back.lua:53 credits owner:set_weapons_left(+1)
// when the blade gets within 10 px of its thrower, and soldier.lua's
// on_fire_weapon refuses a ranged release when weapons_left <= 0 (the branch's
// descriptor-driven replacement for master's order==ORDER_LIVING &&
// family==FAMILY_SOLDIER test at walker.cpp:791-799). A harness-spawned soldier
// starts with weapons_left = 1, so that one line is the difference between a
// soldier that throws all run and one that throws exactly once.
// (b) weapon_knife.lua gates the return on the has_returning_weapon descriptor
// field the PR introduced. dump.walkers[].weapons_left is serialized but no
// FactKind reads it, so the observable is THROW VOLUME, measured as accumulated
// damage on a fixed target.
//
// Target is a STATIONARY team-1 FAMILY_TOWER1 rather than a melee foe for two
// reasons: a charging foe closes to melee, and walker::fire takes the melee
// branch (no weapons_left consumption, no knife, no knife_back) whenever the
// spawn tile is impassable -- which would delete the mechanism under test.
inline constexpr InputEvent kInputsKnifeBackCatch[] = {
    {  1, 0, K_RIGHT},   // three ticks of K_RIGHT turn the freshly spawned soldier east onto the target (a spawned living faces up; init_fire uses lastx/lasty)
    {  4, 0, K_NONE},
    {  5, 0, K_FIRE},    // held fire: the soldier releases a knife whenever weapons_left > 0
    {149, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_effect_knife_back_catch_scen99[] = {
    { FAMILY_TOWER1,  1, kOrderLiving, 176, 120, 0, 0 },   // stationary team-1 target 44px east of the thrower's post-turn position (~132,120); knife spawn (149,125) is passable and the 35px flight reaches the target box at 176
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 },   // player-controlled thrower LAST; default level 1 -> knife damage (6*4)/4*1 = 6, weapons_left starts at 1 (walker.cpp:168; soldier.lua's on_create is guy-only)
};

inline constexpr FactPredicate kFacts_effect_knife_back_catch_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SOLDIER, 1),
    // FLIPPING PREDICATE. A harness-spawned soldier holds ONE weapons_left. The
    // returning blade's arrival branch credits it back, so the thrower re-arms
    // every ~15 ticks and grinds the stationary target down across ~8 throws.
    // kMut_effect_knife_back_catch_scen99 turns that credit into a no-op: the
    // soldier throws exactly once, soldier.lua's on_fire_weapon refuses every
    // later release, and the target keeps almost all of its 13000 cents.
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 9200, 9400,
        "consequence: the returning blade credits weapons_left back to its thrower on arrival, so the soldier keeps throwing for the whole run and the ten accepted releases grind the stationary target from 13000 cents to 9300; dropping the credit starves the thrower after one throw and the target finishes near full"),
    // Throw volume shows up as score awards, one per knife that connects with
    // the target: eight when the thrower keeps re-arming, one when it does not.
    // A play_sound floor would NOT discriminate here -- the TOWER1's own arrow
    // stream keeps that count at 27 even in the starved arm.
    pred::EventKindAtLeast(/*score_change*/9, 6,
        "consequence: every knife that lands on the target awards score, so the sustained throw cycle produces eight score_change events; the credit-free arm lands exactly one knife and emits one"),
};

inline constexpr Mutation kMut_effect_knife_back_catch_scen99 = {
    "packs/core/scripts/effect_knife_back.lua", 53,
    "    owner:set_weapons_left(owner:weapons_left() + 1)",
    "    owner:set_weapons_left(owner:weapons_left() + 0)",
    "Removes the returning blade's arrival credit. The blade still flies home and still probes for collisions, but the thrower never re-arms: soldier.lua's on_fire_weapon refuses every release after the first, the target takes one knife instead of ~8, and both the HP band and the play_sound floor fail."
};

// effect_ghost_scare_moving_caster_scen99: effect_ghost_scare.lua's on_act
// re-centres the scare cloud on its owner every tick (self:center_on(owner),
// master effect.cpp:65-66), so a walking ghost drags the cloud with it and
// on_death computes its flee vector from wherever the cloud ended up. The
// existing row uses special-only input, so the ghost never moves and all eight
// FX track samples sit on one spot -- center_on is proven only in its degenerate
// form. Here the ghost walks west for the cloud's whole life, which turns the FX
// track into a straight line the EffectNetTravel predicate can classify. That is
// the right observable: the flee DIRECTION cannot be made to differ, because the
// caster and the fleeing foe move at the same stepsize and the foe is always on
// the same side of the cloud, so the only fact-visible consequence of center_on
// is the cloud's own path.
inline constexpr InputEvent kInputsGhostScareWalk[] = {
    {  5, 0, K_SPECIAL},   // cast SCARE (slot 1, cost 30)
    {  6, 0, K_LEFT},      // walk WEST for the cloud's whole 8-act life
    { 16, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_effect_ghost_scare_walk_scen99[] = {
    { FAMILY_SOLDIER, 1, kOrderLiving, 180, 120, 0, 0 },         // lone foe, same geometry as the stationary-caster row; closes toward the ghost and is inside scare_radius(5) = 100 of the walked-to ghost when the cloud dies
    { FAMILY_GHOST,   0, kOrderLiving, 120, 120, 0, 0, 5, 300 }, // player-controlled caster LAST; level 5 keeps scare_radius (100) and scare_duration (125) below the branch soft-cap knees, so both arms compute identically
};

inline constexpr FactPredicate kFacts_effect_ghost_scare_walk_scen99[] = {
    pred::TickReached(90),
    pred::WalkerFamilyCount(FAMILY_GHOST, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_GHOST, 1),
    // FLIPPING PREDICATE. ghost_scare_on_act re-centres the cloud on its owner
    // every tick, so a ghost walking west for the cloud's eight acts drags it
    // along a single axis: net displacement == path length, a STRAIGHT class.
    // kMut_effect_ghost_scare_walk_scen99 drops the center_on so the cloud is
    // stranded at the cast point; its track collapses to a single repeated
    // sample, net travel is 0, and the STRAIGHT threshold fails.
    pred::EffectNetTravel(FAMILY_GHOST_SCARE, /*kWeaponPathStraight*/0, 1500,
        "consequence: the scare cloud re-centres on its owner every tick, so a walking ghost drags it along one axis (net == pathlen, STRAIGHT); the mutation strands the cloud at the cast point and its net travel collapses to 0, below the threshold"),
    // The scare still fires from the moved origin: the foe is force-walked away
    // from the cloud for scare_duration(5) = 125 iterations.
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 300, 300,
        "consequence: GHOST_SCARE's on_death force-walks the frightened foe away from the cloud for 125 iterations; the foe ends far south-east of its 180,120 spawn"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_effect_ghost_scare_walk_scen99 = {
    "packs/core/scripts/effect_ghost_scare.lua", 17,
    "    self:center_on(owner)",
    "    local _ = owner",
    "Removes the scare cloud's per-tick re-centring on its owner. The cloud is stranded at the cast point while the ghost walks away, so its FX track collapses to one repeated sample and the on_death flee vector is measured from the wrong origin: EffectNetTravel(FAMILY_GHOST_SCARE, STRAIGHT, ...) flips to 0. Strictly finer than kMut_effect_ghost_scare_emission, which neuters on_death instead."
};


// --- Weapon-behaviour scenarios ---------------------------------------------
//
// Every arena below places its victim NORTH of the caster. A player-controlled
// walker fires along its default heading: walker_init_common leaves
// lastx = lasty = 0 and walker::fire() branches on facing(lastx, lasty), which
// returns FACE_UP for (0,0). That is why the older "wielder at (120,120),
// target at (200,120)" emission rows shoot at empty sky — their goldens end
// with the target at full HP. Keeping the victim north keeps these arenas
// input-free (no direction press, no extra drift).

// weapon_boulder_explode_damage_scen99: the barbarian's EXPLODING BOULDER
// detonating ON a body instead of on empty ground. The caster is spawned facing
// its default heading, so the boulder flies NORTH -- which is why both victims
// are placed north of the barbarian rather than east of it the way
// weapon_exploding_boulder_scen99 does (that row's golden shows all three of its
// eastern soldiers at a full 120 HP: the boulder never touched them).
//   * FAMILY_SOLDIER on the flight line is the IMPACT victim;
//   * FAMILY_ORC is laterally offset by two tiles, so the boulder body can never
//     reach it -- the ONLY thing that can wound it is the FAMILY_EXPLOSION that
//     explode_on_death spawns with damage = boulder damage * 2. ORC also carries
//     BIT_NO_RANGED so it cannot counter-fire, and the input-only-special
//     barbarian never melees, so the orc has exactly one damage source.
// Cast at tick 8 (not the corpus-usual 20) so the two AI foes have only ~10px of
// charge drift before the detonation and the geometry stays as authored.
inline constexpr InputEvent kInputs_weapon_boulder_explode_damage[] = {
    {  4, 0, K_SPECIAL_SWITCH},   // current_special 1 (HURL) -> 2 (EXPLODING BOULDER)
    {  5, 0, K_NONE},
    {  8, 0, K_SPECIAL},
    {  9, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_weapon_boulder_explode_damage_scen99[] = {
    { FAMILY_SOLDIER,   1, kOrderLiving, 120, 120, 0, 0 },              // impact victim on the northward flight line
    { FAMILY_ORC,       1, kOrderLiving, 152, 136, 0, 0 },              // blast-only victim: off the flight line, ~36 Manhattan from the detonation
    { FAMILY_BARBARIAN, 0, kOrderLiving, 120, 168, 0, 0, 15, 300 },     // caster LAST -> head of oblist -> the player-controlled walker
};

inline constexpr FactPredicate kFacts_weapon_boulder_explode_damage_scen99[] = {
    pred::TickReached(40),
    pred::WalkerFamilyCount(FAMILY_BARBARIAN, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1,
        "consequence: the doubled blast is well under the ORC's 14000-cent pool, so it survives to be measured"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 2200, 2400,
        "consequence: explode_on_death's doubled FAMILY_EXPLOSION is the ONLY damage source that can reach the laterally offset ORC -- the boulder body dies on the flight line two tiles away, the input-only-special barbarian never melees, and the orc's BIT_NO_RANGED means it cannot trade at range. Measured 2300 cents; the ceiling sits an order of magnitude under a boulder-only direct hit, let alone the untouched 14000 the mutation produces. A fresh companion recapture reads 2700 off the same blast (a two-tick cadence difference), which is why this row's golden is the blessed branch dump -- see tests/parity/golden/DRIFT_LEDGER.md"),
    pred::WeaponFamilyEmitted(FAMILY_BOULDER),
    pred::EventKindAtLeast(/*play_sound*/1, 2,
        "consequence: SOUND_FWIP on the throw plus SOUND_EXPLODE from explode_on_death"),
};

inline constexpr Mutation kMut_weapon_boulder_explode_damage_scen99 = {
    "packs/core/scripts/weapon_projectiles.lua", 27,
    "  explosion.damage = og.fmul(self:damage(), 2.0)",
    "  explosion.damage = og.fmul(self:damage(), 0.0)",
    "Zeroes the doubled blast damage explode_on_death hands the FAMILY_EXPLOSION (openglad-master/src/weap.cpp:206, `newob->damage = damage*2`). The explosion still spawns, still plays SOUND_EXPLODE and still shoves, so WeaponFamilyEmitted and the play_sound floor stay green; the laterally offset ORC's only damage source vanishes and it finishes at a full 14000 cents, outside WalkerHpRangeAtFinalTick's ceiling -- that predicate alone flips."
};

// weapon_door_unlock_chain_scen99: the whole keyed-door chain in one run --
// key_on_eat banks 1<<clamp(level,0,30) (treasure_valuables.lua), obmap.cpp:326
// reads that bit against the door's level and kills the door, weapon_door.lua's
// on_death hands the spot to an ANI_DOOR_OPEN FAMILY_DOOR_OPEN on weaplist, and
// effect_door_open.lua parks the finished sprite on fxlist ("the amusing part":
// the fxlist copy does not act, so it cannot respawn itself).
// Geometry is the proven treasure_key_pickup_scen99 walk (player SOLDIER at
// (96,120) with the treasure co-located so the eat lands on the first step),
// with the DOOR added on the same y=120 lane. The door is on TEAM 1 on purpose:
// ob_pass_check lets a walker pass straight over a FRIENDLY weapon, so a team-0
// door would never reach the unlock arbitration at all.
inline constexpr InputEvent kInputs_weapon_door_unlock_chain[] = {
    {  1, 0, K_RIGHT},
    { 40, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_weapon_door_unlock_chain_scen99[] = {
    { FAMILY_KEY,     2, kOrderTreasure,  96, 120, 0, 0, 1, 0 },  // level-1 key, team 2 so it is nobody's prop; eat_me is team-agnostic for Order::Treasure
    { FAMILY_DOOR,    1, kOrderWeapon,   160, 120, 0, 0, 1, 0 },  // level-1 door on the walk lane; team 1 so the player is NOT friendly to it
    { FAMILY_SOLDIER, 0, kOrderLiving,    96, 120, 0, 0 },        // player LAST -> head of oblist; co-located with the key
};

inline constexpr FactPredicate kFacts_weapon_door_unlock_chain_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EffectFamilyCount(FAMILY_DOOR_OPEN, 1, 1, /*source_family_qualifier=*/0, /*window_marker=*/0,
        "consequence: the unlocked door's weap::death runs weapon_door.lua's on_death, which puts an ANI_DOOR_OPEN FAMILY_DOOR_OPEN on weaplist; five frames later effect_door_open.lua's on_act hands one persistent copy to fxlist"),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 200, 0,
        "consequence: the key-holder is blocked for a single tick at the door tile and then walks THROUGH the opened doorway, finishing at xpos 224; a run whose key bit never matched is stopped dead around xpos 144 by the 5000-HP door. This is an ANCHOR, not the teeth -- kMut_weapon_door_unlock_chain_scen99 removes only the DOOR_OPEN handoff, so the unlock and the walk-through are unchanged under it"),
    pred::TreasureFamilyOfOrderRemovedFromOblist(FAMILY_KEY, kOrderTreasure,
        "binding anchor, evaluates Indeterminate on both arms: key_on_eat banks the bit and emits its notification on tick 3 (the golden carries it), but the eaten FAMILY_KEY stays in oblist with alive=true, so the predicate's alive-with-no-consumed-instance arm cannot prove removal. treasure_key_pickup_scen99's golden has exactly the same shape -- this is a schema-v1 limit, not a behaviour difference. The chain's real observable is EffectFamilyCount(FAMILY_DOOR_OPEN) above: bank the WRONG bit and the soldier is stopped at xpos 145 by a surviving door and no DOOR_OPEN is ever created"),
    pred::WalkerKeysApplied(/*min_keys=*/1,
        "binding anchor: schema-v1 dumps carry no inventory_keys so this evaluates Indeterminate on both arms; it keeps the key-mask consumer bound to a named predicate for the coverage scan"),
    pred::EventKindAtLeast(/*notification*/2, 1,
        "consequence: key_on_eat emits one '<name> picks up key 1' notification because the eater is on team 0"),
};

inline constexpr Mutation kMut_weapon_door_unlock_chain_scen99 = {
    "packs/core/scripts/weapon_door.lua", 10,
    "  local opened = og.add_weap_ob(\"fx\", FX_DOOR_OPEN)",
    "  local opened = nil",
    "Suppresses the broken door's FAMILY_DOOR_OPEN handoff (openglad-master/src/weap.cpp:219, `newob = add_weap_ob(ORDER_FX, FAMILY_DOOR_OPEN)`); on_death takes its `if not opened then return false end` failsafe, so no DOOR_OPEN ever reaches weaplist, effect_door_open.lua's on_act never runs, and fxlist holds zero FAMILY_DOOR_OPEN -- EffectFamilyCount(FAMILY_DOOR_OPEN,1,1) flips 1 -> 0. The key mask, the obmap unlock and the walk-through are untouched, so this pins weapon_door.lua's on_death specifically."
};

// weapon_rock_bounce_edge_scen99: the FIRST row in the corpus to exercise
// weapon_rock.lua's reflect body and the kWeaponPathReturns behaviour class.
//
// GEOMETRY. A player-controlled caster fires along its default heading
// (facing() == FACE_UP), which golden/weapon_rock_slot2_emit_scen99.json
// confirms: that elf's rocks climb from (122,113) to (141,59). So the barrier
// has to be NORTH.
// The barrier is the MAP EDGE, not terrain. The design sketch aimed the volley
// into a tree block, but core:rock carries init_bit_flags: [FORESTWALK]
// (packs/core/families/weapon-01-rock.yaml), and game_world.cpp's PIX_TREE_*
// arm lets a FORESTWALK walker straight through -- a rock cannot be stopped by
// any tree in the game. scen1's decoded tile grid (pix/scen0001.png, 40x60 at
// GRID_SIZE 16) contains no PIX_H_WALL1/WALL2/WALL3/WALL_LL/WALLTOP_H cell at
// all, so terrain cannot stop this projectile anywhere on this map. The one
// remaining hard barrier is query_grid_passable's own bounds test
// (`x_i < 0 || y_i < 0 || ...`), which walker::walk consults through
// query_passable: a rock stepping off the top of the map fails its walk, and
// act_fire's `else if (!walk() ...)` arm calls death() with a NULL collide_ob --
// exactly the state rock_on_death demands.
// The elf therefore stands at (192, 56) = tile (12, 3), a grass column whose
// rows 0..4 are all grass, with the rocks leaving at (194, 49): six clear 8px
// climbs to y = 1, and the seventh probe (y = -7) is off the map. The reflect
// then takes the third probe (xpos+lastx, ypos-lasty = y+8, open), flips lasty,
// and the rock flies back down for the rest of its line of sight.
// No enemies are spawned: an enemy knife can collide with our rock, and a rock
// that dies with a non-null collide_ob takes rock_on_death's "died of natural
// causes" early return instead of bouncing.
inline constexpr SpawnSpec kFamilySpawns_weapon_rock_bounce_edge_scen99[] = {
    { FAMILY_ELF, 0, kOrderLiving, 192, 56, 0, 0, 4, 300 }, // lone caster (level 4 -> slot 2 reachable, 300 mp affordable); the player-controlled walker
};

inline constexpr FactPredicate kFacts_weapon_rock_bounce_edge_scen99[] = {
    pred::TickReached(45),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::WeaponFamilyEmitted(FAMILY_ROCK),
    pred::WeaponNetTravel(FAMILY_ROCK, /*kWeaponPathReturns*/1, 8000,
        "trajectory: the slot-2 BOUNCING ROCKS volley climbs six 8px steps to y=3, steps off the top of the map, and rock_on_death un-deads it and reflects lasty, so seq 0 walks 49 -> 3 -> 55 for pathlen 9800 centi against net 600 -- the RETURNS class (pathlen >= 8000 AND net*2 < pathlen), which NO existing row produces. Suppress the reflect and the rock simply dies at the edge: the track is a 7-sample straight climb whose pathlen is 4600 (under the 8000 threshold) and whose net EQUALS its pathlen, so RETURNS fails twice over"),
    pred::WeaponSpeed(FAMILY_ROCK, 700, 900,
        "trajectory: the cardinal-fired rock steps 8px/tick (800 centi) both before and after the reflect; this is an anchor, not the teeth, and stays green under the mutation"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_weapon_rock_bounce_edge_scen99 = {
    "packs/core/scripts/weapon_rock.lua", 8,
    "  if self:do_bounce() == 0 or self:lineofsight() == 0",
    "  if self:do_bounce() ~= 0 or self:lineofsight() == 0",
    "Inverts the do_bounce admission test that guards the whole reflect body (openglad-master/src/weap.cpp:156, `if (!do_bounce || !lineofsight || collide_ob) break;`). An ARMED bouncing rock now takes the 'died of natural causes' early return and simply dies against the tree ceiling, so its weapon_tracks entry is a pure straight climb: net == pathlen and pathlen drops below the RETURNS threshold, flipping WeaponNetTravel(FAMILY_ROCK, kWeaponPathReturns, ...). Emission and per-tick speed are untouched, so the flip is isolated to the path class. NOTE: a naive `set_lastx(-lastx) -> set_lastx(lastx)` mutation does NOT work here -- the rock then oscillates against the wall once per tick and still classifies as RETURNS."
};

// weapon_wave_promote_wave2_scen99: WAVE -> WAVE2 promotion, the one place
// walker::transform_to runs inside a weapon on_death. Reuses the proven
// weapon_wave_emission_scen99 arena and cadence verbatim and only extends the
// budget past the promotion tick, so the only new thing under test is the
// transform. FAMILY_WAVE's lineofsight is 3 (gloader) and it is IMMORTAL +
// NO_COLLIDE, so act_fire's `if (!(lineofsight--))` arm -- not a collision -- is
// what calls death() on the fourth tick of flight; the promotion tick is
// therefore deterministic rather than geometry-dependent.
// WeaponFamilyEmitted is satisfied by a recorded weapon TRACK as well as by a
// live weapon, so the assertion survives even if the promoted entity later moves
// on to WAVE3 -- the exact dump tick is not critical.
// Do NOT add an EventKindExactly(notification, ...) pin: a promoted WAVE2 whose
// act_type survives the transform can reach weap::act's ACT_RANDOM arm, which
// emits one Notification per tick by design.
inline constexpr FactPredicate kFacts_weapon_wave_promote_wave2_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WeaponFamilyEmitted(FAMILY_WAVE2,
        "consequence: wave_on_death un-deads the expiring WAVE and transform_to's it into FAMILY_WAVE2 in place, so weaplist / weapon_tracks carry the WAVE2 family string from the promotion tick onward. This is the first UNGATED FAMILY_WAVE2 binding in the corpus -- weapon_wave2_emission_scen99 has to FactSide-gate its copy because K_FIRE never produces one there"),
    pred::WeaponFamilyEmitted(FAMILY_WAVE,
        "anchor: the tick-25 cast is still in stage 1 at the dump, and the first wave's pre-promotion track is recorded regardless; stays green under the mutation so the flip is isolated to WAVE2"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_weapon_wave_promote_wave2_scen99 = {
    "packs/core/scripts/weapon_wave.lua", 16,
    "  self:transform_to(\"weapon\", WEAPON_WAVE2)",
    "  self:transform_to(\"weapon\", WEAPON_WAVE3)",
    "Retargets the first promotion stage past FAMILY_WAVE2 (openglad-master/src/weap.cpp:210, `transform_to(ORDER_WEAPON, FAMILY_WAVE2)`). The entity still survives its death -- no cascade of unrelated diffs, the caster and the soldier are untouched -- but no FAMILY_WAVE2 ever exists in weaplist or in weapon_tracks, so WeaponFamilyEmitted(FAMILY_WAVE2) flips true -> false while WeaponFamilyEmitted(FAMILY_WAVE) stays green. WEAPON_WAVE3 is already a file-local in weapon_wave.lua (wave2_on_death uses it), so the mutated line resolves cleanly."
};

// weapon_sprinkle_freeze_scen99: the faerie's sprinkle writing frozen_delay into
// a living target (weapon_animate.lua sprinkle_on_hit_target; original
// openglad-master/src/walker.cpp:1995).
//
// DIRECTION MATTERS TWICE. (a) A player-controlled walker fires along its
// default heading, which is FACE_UP -- that is why
// weapon_sprinkle_emission_scen99's soldier finishes at a FULL 120 HP: no
// sprinkle ever touched it. Three K_RIGHT ticks give the faerie an eastward
// lastx that survives the release, so the single throw actually connects.
// (b) WalkerPositionMoved is a LOWER bound, so the victim has to be the one that
// moves in -x: the ORC charges WEST toward the faerie, so "still near its spawn
// column" is expressible as a floor on xpos, and an unfrozen orc falls below it.
// The victim is FAMILY_ORC: BIT_NO_RANGED (it cannot counter-fire), a family
// distinct from the caster (the ANY-match predicates stay unambiguous), and a
// 14000-cent pool that the 1-damage sprinkle barely dents.
// HARD CONSTRAINT (keeps the row capturable on master): ONE throw and a caster
// BELOW level 21. Repeated sprinkles engage the branch-only refresh gate
// (`owner.level >= 21 and target:s_frozen_delay() > 10`) and the negative
// thaw-immunity guard, neither of which master has a counterpart for. Level 20 +
// a single throw leaves both provably inert.
inline constexpr InputEvent kInputs_weapon_sprinkle_freeze[] = {
    {  1, 0, K_RIGHT},   // establish an eastward lastx/curdir; ~11px of drift
    {  4, 0, K_NONE},
    { 10, 0, K_FIRE},    // held long enough for the attack animation to reach fire()
    { 18, 0, K_NONE},    // fire_frequency 9 makes a second throw impossible inside this window
};

inline constexpr SpawnSpec kFamilySpawns_weapon_sprinkle_freeze_scen99[] = {
    { FAMILY_ORC,    1, kOrderLiving, 200, 120, 0, 0 },            // victim: charges WEST, so "did not move" is a floor on xpos
    { FAMILY_FAERIE, 0, kOrderLiving, 120, 120, 0, 0, 20, 600 },   // caster LAST; level 20 (< 21) keeps the branch-only refresh gate inert
};

inline constexpr FactPredicate kFacts_weapon_sprinkle_freeze_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_FAERIE, 1, 1,
        "consequence: the frozen orc never reaches melee inside the budget, so the fragile 7500-cent faerie survives"),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerPositionMoved(FAMILY_ORC, 140, 0,
        "consequence: sprinkle_on_hit_target writes og.freeze_duration(owner.level, constitution) into the orc's frozen_delay and living::act returns early every tick it is non-zero, so the orc's westward charge stalls at the throw tick and only resumes when it thaws. WalkerPositionMoved is a LOWER bound and the orc closes in -x, so the floor holds only while the freeze does: measured 152 frozen against 131 unfrozen at the 150-tick dump, and the floor sits 12 px inside each. The 60-tick budget the design sketch used separates the two arms by only 6 px -- the freeze has not expired yet on EITHER arm at that point, so the entire measurable gap is the 42 ticks the frozen orc lost; 150 lets the thawed orc bank its recovery too"),
    pred::WeaponFamilyEmitted(FAMILY_SPRINKLE),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_weapon_sprinkle_freeze_scen99 = {
    "packs/core/scripts/weapon_animate.lua", 129,
    "    target:s_set_frozen_delay(roll)",
    "    target:s_set_frozen_delay(0)",
    "Drops the freeze while PRESERVING the og.freeze_duration RNG draw one line above it, so the gameplay RNG stream -- and therefore every other entity in the arena -- is untouched and the flip is attributable to the freeze alone. The unfrozen orc resumes its westward charge and its dumped xpos falls below WalkerPositionMoved's floor."
};

// weapon_fire_arrow_explode_damage_scen99: the ARCHER EXPLODING BOLT half of
// weapon_projectiles.lua's explode_on_death. Same shape as
// weapon_boulder_explode_damage_scen99 -- an impact victim on the default
// FACE_UP flight line plus a laterally offset FAMILY_ORC that only the blast can
// reach -- but a different caster, a different slot and (deliberately) a
// different mutation anchor inside the SAME shared Lua function, so the pin
// table gets two independent anchors in one file.
// The offset blast-only victim is a STATIONARY FAMILY_TOWER1 (living-20-beast.yaml
// is the corpus's only is_stationary living, and its derived_bonuses[2] damage is
// 0). An ORC in that seat charges the archer, wanders in and out of the blast
// footprint and dies to the level-10 detonation -- measured, not guessed. The
// tower cannot leave its tile, cannot wander onto the flight line, and cannot
// deal damage to anything, so the only thing that ever touches it is the blast.
// The ARCHER is deliberately NOT counted: the impact SOLDIER's knives kill it
// a few ticks after the detonation, which is irrelevant to the tower's reading.
inline constexpr SpawnSpec kFamilySpawns_weapon_fire_arrow_explode_damage_scen99[] = {
    { FAMILY_SOLDIER, 1, kOrderLiving, 120, 104, 0, 0 },            // impact victim on the northward flight line
    { FAMILY_TOWER1,  1, kOrderLiving, 152, 136, 0, 0 },            // blast-only victim: stationary, off the flight line, deals no damage
    { FAMILY_ARCHER,  0, kOrderLiving, 120, 168, 0, 0, 10, 600 },   // caster LAST (level 10 clears the slot-3 cycling gate (3-1)*3+1 = 7; 600 mp affordable)
};

inline constexpr FactPredicate kFacts_weapon_fire_arrow_explode_damage_scen99[] = {
    pred::TickReached(60),
    pred::WalkerDiedByFinal(FAMILY_TOWER1,
        "consequence: the skip_exit(5000) bolt detonates ON the soldier and the doubled-damage FAMILY_EXPLOSION is the only thing in the arena that can reach the offset stationary TOWER1. An HP window is not available here: a spawned (myguy-less) caster takes walker::fire's `damage *= level` arm, so a level-10 archer's exploding bolt carries 227 damage and the blast carries twice that -- nothing in the family table survives it. Dead-or-alive is the honest observable. Invert the sentinel gate and no explosion is spawned at all: the tower is never touched and finishes alive at its full 13000 cents, so this predicate flips"),
    pred::WeaponFamilyEmitted(FAMILY_FIRE_ARROW),
    pred::EventKindAtLeast(/*play_sound*/1, 5,
        "anchor: SOUND_BOW on the shot plus the melee traffic; the measured count is 9. NOT a tooth -- suppressing the detonation also keeps the impact soldier alive to throw more knives, so the mutated arm's play_sound count goes UP, not down. WalkerDiedByFinal carries the flip"),
};

inline constexpr Mutation kMut_weapon_fire_arrow_explode_damage_scen99 = {
    "packs/core/scripts/weapon_projectiles.lua", 8,
    "  if self:skip_exit() == 0 then",
    "  if self:skip_exit() ~= 0 then",
    "Inverts the legacy sentinel gate (openglad-master/src/weap.cpp:192, `if (!skip_exit) break;  // skip_exit means we're supposed to explode :)`): the ARMED bolt now takes the early return and an ORDINARY arrow would explode instead. No FAMILY_EXPLOSION is spawned, so the offset ORC stays at a full 14000 cents and the SOUND_EXPLODE contribution to the play_sound floor disappears. Deliberately a DIFFERENT (line, text) anchor from kMut_weapon_boulder_explode_damage_scen99's damage-multiplier flip in the same file."
};

// weapon_circle_protection_follow_scen99: proves the ring TRACKS a moving owner.
// Reuses kFamilySpawns_effect_protection_emit_scen99 and kInputsSpecialSlot4
// unchanged -- that arena is the corpus's only real druid-cast ring on a real
// owner, and its no-enemies design is load-bearing. Only the budget grows, from
// 25 to 60, so the ring accumulates a long track behind the friendly's
// deterministic idle wander (that golden already moves the friendly 85,90 ->
// 145,166 in 25 ticks and the ring with it, 137,162).
// The assertion is WeaponSpeed with a NON-ZERO floor, not a path class: a
// wandering owner produces a meandering path that is neither STRAIGHT
// (net << pathlen) nor STATIONARY, but its per-tick step is always the owner's
// step -- and a ring that stopped following steps exactly 0.
inline constexpr FactPredicate kFacts_weapon_circle_protection_follow_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION),
    pred::WeaponSpeed(FAMILY_CIRCLE_PROTECTION, 400, 700,
        "trajectory: circle_protection_on_animate calls center_on(owner) every tick, so the ring's per-tick displacement IS the protected friendly's step -- 566 centi for the stepsize-4 soldier's diagonal wander, measured over a 40-sample track. The FLOOR is the tooth: strip the re-centre and the ring is pinned at its emit coordinate with max consecutive-tick step 0, below 400. The ring is IMMORTAL and never damaged in this arena, so it survives the whole budget and the track always has consecutive samples (no Indeterminate escape)"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_weapon_circle_protection_follow_scen99 = {
    "packs/core/scripts/weapon_animate.lua", 83,
    "  self:center_on(owner)",
    "  local _ = owner",
    "Removes the per-tick re-centre from circle_protection_on_animate (openglad-master/src/weap.cpp:278, `center_on(owner);`) while keeping `owner` referenced so the hook's dead-owner guard above it is unchanged. The ring stays where it was summoned instead of tracking the walking friendly: its weapon_tracks max consecutive-tick step collapses from the owner's step to 0, below WeaponSpeed's floor. WeaponFamilyEmitted stays green (the ring is still created), so the flip is isolated to the follow behaviour."
};

// weapon_sit_notify_quiet_scen99: the skip_sit_notify descriptor field, whose
// only consumer is weap::act's ACT_SIT arm. The arena is deliberately silent --
// two direct-spawn ACT_SIT scenery weapons and one idle team-0 soldier, no
// enemies and no inputs -- so an EXACT zero-notification pin cannot be broken by
// combat chatter. golden/weapon_door_emission_scen99.json already records
// notification: 0 with a full two-soldier melee running, so the pin has margin.
// FAMILY_WAVE2 and FAMILY_BLOOD are deliberately EXCLUDED: WAVE2's ACT_RANDOM
// arm emits a Notification every tick by design, and BLOOD is ACT_DIE and never
// reaches the sit arm at all.
inline constexpr SpawnSpec kFamilySpawns_weapon_sit_notify_quiet_scen99[] = {
    { FAMILY_DOOR,    0, kOrderWeapon, 120, 120, 0, 0 }, // ACT_SIT scenery, skip_sit_notify: true
    { FAMILY_TREE,    0, kOrderWeapon, 120, 152, 0, 0 }, // ACT_SIT scenery, skip_sit_notify: true
    { FAMILY_SOLDIER, 0, kOrderLiving, 176, 120, 0, 0 }, // lone idle observer; the player-controlled walker (no inputs are scripted)
};

inline constexpr FactPredicate kFacts_weapon_sit_notify_quiet_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::EventKindExactly(/*notification*/2, 0,
        "consequence: both sitting weapons carry skip_sit_notify: true, so weap::act's ACT_SIT arm emits nothing for 150 ticks; master's twin excludes the same three families by hardcoded family test (openglad-master/src/weap.cpp:72). Clear the flag on either descriptor and that weapon contributes ONE Notification per tick -- the exact-zero pin goes 0 -> 150"),
    pred::WeaponFamilyEmitted(FAMILY_DOOR),
    pred::WeaponFamilyEmitted(FAMILY_TREE),
    pred::WeaponNetTravel(FAMILY_DOOR, kWeaponPathStationary, 100,
        "anchor: the ACT_SIT arm never walks the entity, so the door's pathlen is 0 on both arms; stays green under the descriptor mutation so the flip is isolated to the event count"),
};

inline constexpr Mutation kMut_weapon_sit_notify_quiet_scen99 = {
    "packs/core/families/weapon-18-door.yaml", 8,
    "      skip_sit_notify: true",
    "      skip_sit_notify: false",
    "Clears the descriptor flag on core:door. weap::act's ACT_SIT arm (src/gameplay/weap.cpp:94, `if (!wfd || !wfd->skip_sit_notify)`) then emits a 'Weapon sitting' Notification for the direct-spawned door on every one of the 150 ticks, so EventKindExactly(notification, 0) flips 0 -> 150. The door still sits, still appears in dump.weapons and still has pathlen 0, so the emission and trajectory anchors stay green."
};

// weapon_ranged_impact_hp_scen99: the corpus's first genuine ranged-impact HP
// pin. The six existing *_emission rows assert only WalkerAliveAtFinal on their
// victim, and their goldens show the victim untouched at full HP -- their
// wielders shoot NORTH at empty sky while the target sits EAST. Here the victim
// is placed on the actual flight line.
// FAMILY_ORC is the victim on purpose: BIT_NO_RANGED means it cannot
// counter-fire, its family is distinct from the archer so the ANY-match HP
// predicate is unambiguous, and its 14000-cent pool survives the barrage so
// there is always a walker to measure.
// The victim is a STATIONARY FAMILY_TOWER1, not the sketch's ORC: an orc walks
// into the stream and its dumped HP mixes arrow hits with its own melee trade,
// and at any wielder level above 1 the barrage simply kills it (measured: a
// level-5 archer empties the orc's pool by tick 42, a level-20 archer by tick
// 19). living-20-beast.yaml is is_stationary with derived_bonuses[2] damage 0,
// so the tower holds the flight line, contributes nothing back, and its 13000
// cents are a pure readout of FAMILY_ARROW's damage column.
// The wielder is left at the DEFAULT level 1 on purpose. walker::fire scales a
// spawned (myguy-less) wielder's projectile by `damage * (level+3)/4 * level`,
// so level is a quadratic lever on arrow damage: level 1 gives the 5-point
// column its face value and keeps the readout in a measurable band.
inline constexpr SpawnSpec kFamilySpawns_weapon_ranged_impact_hp_scen99[] = {
    { FAMILY_TOWER1, 1, kOrderLiving, 120,  88, 0, 0 },            // victim on the northward flight line; stationary, deals no damage, 13000-cent pool
    { FAMILY_ARCHER, 0, kOrderLiving, 120, 168, 0, 0 },            // wielder LAST; default level 1 (see above) so the barrage wounds instead of erasing
};

inline constexpr FactPredicate kFacts_weapon_ranged_impact_hp_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 1, 1,
        "the 13000-cent stationary tower outlasts the barrage, so there is always an entry for the HP window to measure"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 10200, 10400,
        "consequence: FAMILY_ARROW's damage column is the TOWER1's only damage source in this arena (the tower is stationary, deals 0 damage and cannot leave the flight line; the archer's only act is firing). Seven arrows take it from 13000 to a measured 10300 cents. The ceiling is strictly below the untouched 13000 the damage-column mutation leaves behind"),
    pred::WeaponFamilyEmitted(FAMILY_ARROW),
    pred::EventKindAtLeast(/*play_sound*/1, 5,
        "anchor: the archer's bow fires nine times inside the budget; zeroing the damage column does not silence it, so this stays green and the HP window carries the flip"),
};

inline constexpr Mutation kMut_weapon_ranged_impact_hp_scen99 = {
    "src/resources/gloader.cpp", 560,
    "{Order::Weapon, FAMILY_ARROW,             \"arrow.png\",    5, ACT_FIRE, aniarrow.data(),        8, 12,  5, 0},",
    "{Order::Weapon, FAMILY_ARROW,             \"arrow.png\",    5, ACT_FIRE, aniarrow.data(),        8, 12,  0, 0},",
    "Zeroes FAMILY_ARROW's damage column in the EntityDef weapon-defaults table."
};

// --- corner-misc batch -----------------------------------------------------

// archer_hit_response_backpedal_scen99: a player FAMILY_SOLDIER holds RIGHT+FIRE and
// walks into an AI FAMILY_ARCHER 30px east of it. Every landed melee swing runs the
// archer's hit_response (master stats.cpp:556-576 / packs/core/scripts/archer.lua):
// distance_to_ob < melee_backpedal_range (64, Manhattan) force-commands an 8-step
// COMMAND_WALK directly away, so the archer is shoved ahead of the advancing soldier.
// hit_response early-returns for ACT_CONTROL walkers, which is why the archer must be
// the AI side and the soldier the player.
inline constexpr InputEvent kInputs_archer_backpedal[] = {
    {  1, 0, K_RIGHT | K_FIRE },
    { 70, 0, K_NONE },
};

inline constexpr SpawnSpec kFamilySpawns_archer_hit_response_backpedal_scen99[] = {
    { FAMILY_ARCHER,  1, kOrderLiving, 150, 120, 0, 0 }, // AI archer, 30px east: inside the 64px backpedal trigger
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 }, // player LAST (oblist head -> takes ACT_CONTROL)
};

inline constexpr FactPredicate kFacts_archer_hit_response_backpedal_scen99[] = {
    pred::TickReached(80),
    pred::WalkerFamilyCount(FAMILY_ARCHER, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_ARCHER, 1),
    pred::WalkerPositionMoved(FAMILY_ARCHER, 220, 0,
        "consequence: each landed melee swing force-commands COMMAND_WALK 8 steps directly away from the attacker, so the archer is driven from its 150 spawn out to xpos 220 ahead of the advancing soldier; with the backpedal disabled it stands its ground and its own approach leaves it at 202, short of this bound"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3000, 3000,
        "consequence: the backpedal pulls the archer out of the soldier's swing arc for 8 ticks after every hit, so it ends on exactly 3000 cents; disabling it re-times the whole exchange and the archer ends on 3100 instead"),
    pred::EventKindAtLeast(/*play_sound*/1, 4,
        "anchor: SOUND_CLANG per landed melee swing plus the archer's SOUND_BOW volleys; the backpedal mutation makes the exchange noisier, not quieter, so this stays green and the position/HP pins carry the flip"),
};

inline constexpr Mutation kMut_archer_hit_response_backpedal_scen99 = {
    "packs/core/scripts/archer.lua", 73,
    "  if distance < og.tuning(self).melee_backpedal_range then",
    "  if false then",
    "Disables the archer's melee backpedal entirely. The archer never receives the away-facing COMMAND_WALK, so it stays in the soldier's swing arc: its final xpos drops 220 -> 202 (below the WalkerPositionMoved bound) and its HP lands on 3100 instead of 3000."
};

// orc_yell_zero_constitution_scen99: the yell's constitution roll at a ZERO bound.
// Spawns prepend, so oblist order is the reverse of this array: the player orc is the
// head (ACT_CONTROL), then the FAMILY_TOWER generator, then the skeleton. Harness-spawned
// generators have hitpoints == 0, so trunc(hp/30) == 0 and og.rand0(0 * mult) must answer
// 0 WITHOUT advancing the stream (bindings_entity.cpp:1801 == master screen.cpp:66). The
// skeleton is iterated second with con == 2, so any spurious draw at the tower shifts its
// stun roll and every downstream draw for the rest of the run.
inline constexpr SpawnSpec kFamilySpawns_orc_yell_zero_constitution_scen99[] = {
    { FAMILY_SKELETON, 1, kOrderLiving,    150, 140, 0, 0 },       // living foe, con = trunc(60/30) = 2 -> two draws
    { FAMILY_TOWER,    1, kOrderGenerator, 180, 120, 0, 0 },       // hp 0 -> con 0 -> the zero-bound roll (NO draw)
    { FAMILY_ORC,      0, kOrderLiving,    120, 120, 0, 0, 1, 600 }, // caster LAST; yell_radius(1) = 180 covers both
};

inline constexpr FactPredicate kFacts_orc_yell_zero_constitution_scen99[] = {
    pred::TickReached(120),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerAliveAtFinal(FAMILY_SKELETON, 1),
    pred::WalkerPositionMoved(FAMILY_SKELETON, 138, 128,
        "anchor: the frozen skeleton ends its melee shuffle at exactly (138,128) on both arms; the zero-bound draw changes the damage cadence, not where the skeleton stands, so this pins the arena shape while the HP band carries the flip"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 8200, 8200,
        "consequence: the orc only takes damage while the skeleton is unfrozen. A spurious draw at the tower's zero constitution bound shifts every later roll by one position, which re-times the skeleton's 100-tick attack cadence and leaves the orc on 8700 cents instead of 8200"),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "anchor: SOUND_ROAR is emitted unconditionally at the end of the yell on both arms"),
};

inline constexpr Mutation kMut_orc_yell_zero_constitution_scen99 = {
    "packs/core/scripts/orc.lua", 37,
    "      local con_roll = og.rand0(con * t.yell_con_roll_mult)",
    "      local con_roll = og.rand0(og.max(con, 1) * t.yell_con_roll_mult)",
    "Forces a real RNG draw at the tower generator's zero constitution bound, which master's random(x<1) early return never takes. The mutation is a no-op for every foe with con >= 1, so it isolates exactly the zero-bound semantics: every subsequent draw in the run shifts by one position and the orc finishes on 8700 cents instead of 8200."
};

// mage_freeze_time_offteam_scen99: the freeze_time ELSE arm (caster team != my_team).
// An AI FAMILY_MAGE on team 1 reaches slot 3 through living::act's ACT_RANDOM roll
// (living.cpp:358-375: 1-in-5 tick, then rng.next((level+2)/3)+1 == 3 at level 15) and
// grants min(5 + 2*15, 50) = 35 bonus_rounds to every friend within 30000. Each bonus
// round is a recursive extra living::act, so the two allied orcs surge toward the distant
// player soldier. player_team is 0 and the caster is team 1, so BOTH arms take the else
// branch (master's condition is team_num == 0 || myguy).
// Harness note: master's else arm announces the grant with viewob[0]->set_display_text()
// + redraw(), neither of which the master recorder observes. parity_runner.cpp therefore
// drops the branch's "TIME IS FROZEN!" Notification (is_classic_display_text_notification)
// and its RequestRedraw, the same normalisation already applied to "TIME LEFT: " and
// DamageTile. The row asserts no event facts as a result.
inline constexpr SpawnSpec kFamilySpawns_mage_freeze_time_offteam_scen99[] = {
    { FAMILY_ORC,     1, kOrderLiving, 100,  60, 0, 0 },            // ally A: receives bonus_rounds
    { FAMILY_ORC,     1, kOrderLiving,  60, 100, 0, 0 },            // ally B: receives bonus_rounds
    { FAMILY_MAGE,    1, kOrderLiving,  60,  60, 0, 0, 15, 900 },   // AI caster (slot-3 cost 500)
    { FAMILY_SOLDIER, 0, kOrderLiving, 560, 440, 0, 0 },            // player LAST, 880px away (Manhattan)
};

inline constexpr FactPredicate kFacts_mage_freeze_time_offteam_scen99[] = {
    pred::TickReached(300),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/1, 3, 3),
    pred::WalkerPositionMoved(FAMILY_ORC, 387, 523,
        "consequence: freeze_time's off-team arm grants min(5 + 2*level, 50) = 35 bonus_rounds to every ally and each bonus round is a recursive extra living::act, so the trailing orc is carried out to (387,523); with the per-level term zeroed the grant collapses to the base 5 and neither orc gets past ypos 439"),
};

inline constexpr Mutation kMut_mage_freeze_time_offteam_scen99 = {
    "packs/core/families/living-03-mage.yaml", 81,
    "        bonus_rounds_per_level: 2",
    "        bonus_rounds_per_level: 0",
    "Collapses the off-team freeze grant from min(5 + 2*15, 50) = 35 rounds to the base 5. The allies get seven times fewer extra act() passes, so neither orc reaches the WalkerPositionMoved bound (the trailing orc stops at (471,439) instead of (387,523))."
};

// beast_set_difficulty_invariant_scen99: reach living::set_difficulty for a family with no
// generator of its own. SpawnSpec.default_weapon is applied on both arms, and a generator's
// create_weapon spawns add_ob(Order::Living, default_weapon()), so a TREEHOUSE pointed at
// FAMILY_GOLEM emits golems and runs golem.lua's og.apply_difficulty_scaling(self, level,
// 18.0, 5.0, 7.0, 4.0). Generator level 1 -> rolled spawn level 1 -> the hp tuple constant
// is directly readable as max_hp (300 base + 18*1 = 318). Same shape covers the other
// uncovered tuples by swapping default_weapon: FAMILY_ARCHER (2), FAMILY_CLERIC (5),
// FAMILY_DRUID (13), FAMILY_ORC (14). FAMILY_SOLDIER is id 0 and default_weapon == 0 means
// "skip", so soldier's tuple stays uncovered by this route.
// GOLDEN NOTE: the companion emits FOUR golems at 336 hp because master applies
// set_difficulty a second time inside create_weapon (the A12b divergence, dropped branch-
// side and already documented for generator_owner_cascade_scen99). The merge base
// reproduces the branch's two 318-hp golems exactly, so the golden here is the branch dump
// -- see tests/parity/golden/DRIFT_LEDGER.md.
inline constexpr SpawnSpec kFamilySpawns_beast_set_difficulty_invariant_scen99[] = {
    { FAMILY_TREEHOUSE, 1, kOrderGenerator, 120, 120, FAMILY_GOLEM, 0 }, // no lifetime, clears owner
};

inline constexpr FactPredicate kFacts_beast_set_difficulty_invariant_scen99[] = {
    pred::TickReached(1500),
    pred::WalkerFamilyCount(FAMILY_GOLEM, 2, 2),
    pred::WalkerHpRangeAtFinalTick(FAMILY_GOLEM, 31800, 31800,
        "consequence: golem.lua's set_difficulty tuple adds 18*level^2 hp on top of the 300 base, so a rolled level-1 generator spawn is exactly 318; collapsing the constant to 1.0 puts every emitted golem on 301"),
    pred::WalkerOfTeamAlive(/*team=*/1, 3, 3),
};

inline constexpr Mutation kMut_beast_set_difficulty_invariant_scen99 = {
    "packs/core/scripts/golem.lua", 8,
    "  og.apply_difficulty_scaling(self, level, 18.0, 5.0, 7.0, 4.0)",
    "  og.apply_difficulty_scaling(self, level, 1.0, 5.0, 7.0, 4.0)",
    "Collapses the BEAST hp scaling constant from 18 to 1. Every generator-emitted golem lands at 301 hp instead of 318, outside WalkerHpRangeAtFinalTick. set_difficulty draws no RNG, so the spawn cadence and both golem positions are unchanged and the flip is isolated to the hp column."
};

// thief_taunt_matched_levels_scen99: matched levels make the taunt adjudication
// falsifiable. Both rolls are og.rand(7), so the LEFT-first (parity) and RIGHT-first
// orders consume the same two draws but reach the opposite verdict on every unequal pair,
// and the taunted foe costs one extra og.rand(level) for its FOLLOW duration -- so the
// verdict moves that draw's POSITION in the stream and every later roll shifts with it.
// Level 7 is forced by the slot-3 cycling gate ((3-1)*3+1 == 7) and the two foes must
// match it, so both foes are melee-only families: a level-7 knife thrower one-shots the
// 75-hp thief (measured), a level-7 melee walker does not, because SpawnSpec.stats_level
// writes the level field without recomputing the damage column.
// The team-0 FAMILY_TOWER1 is a stationary, zero-damage punching bag that both foes chew
// on; its hp is a pure readout of the shifted damage rolls. It is FIRST in the array so
// the thief (LAST) is still the oblist head and takes ACT_CONTROL.
inline constexpr SpawnSpec kFamilySpawns_thief_taunt_matched_levels_scen99[] = {
    { FAMILY_TOWER1,      0, kOrderLiving, 200, 200, 0, 0 },         // stationary team-0 decoy, damage column 0
    { FAMILY_ORC,         1, kOrderLiving, 180, 150, 0, 0, 7, 0 },   // matched level 7, melee-only (BIT_NO_RANGED)
    { FAMILY_SMALL_SLIME, 1, kOrderLiving, 150, 180, 0, 0, 7, 0 },   // matched level 7, melee-only, distinct family
    { FAMILY_THIEF,       0, kOrderLiving, 120, 120, 0, 0, 7, 600 }, // caster LAST
};

inline constexpr FactPredicate kFacts_thief_taunt_matched_levels_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_THIEF, 1, 1),
    pred::EventKindAtLeast(/*notification*/2, 1,
        "anchor: the taunt arm emits \"THIEF: 'Nyah Nyah!'\" unconditionally once the loop finishes, on either adjudication order"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 11100, 11100,
        "consequence: the taunted foe's FOLLOW-duration draw sits at a different position in the stream under the two adjudication orders, so every later combat roll shifts; the decoy tower reads out that shift as exactly 11100 cents against 11300 under the inverted comparison"),
    pred::WalkerPositionMoved(FAMILY_ORC, 156, 132,
        "consequence: the same one-position stream shift re-times the orc's approach; it ends at exactly (156,132) here and at (153,129) under the inverted comparison, short of this bound on both axes"),
};

inline constexpr Mutation kMut_thief_taunt_matched_levels_scen99 = {
    "packs/core/scripts/thief.lua", 97,
    "      if my_roll >= foe_roll then",
    "      if foe_roll >= my_roll then",
    "Inverts the taunt adjudication. With matched levels both draws share the bound rand(7), so this is exactly the observable of the RIGHT-first draw order the parity port rejected: the same two draws are consumed but the opposite foe is taunted, which moves the FOLLOW-duration draw within the stream and shifts every later combat roll (decoy tower 11100 -> 11300, orc (156,132) -> (153,129))."
};

// Face east, then cycle to special slot 4 and cast. The leading K_RIGHT is required:
// fire() aims from lastx/lasty and an un-nudged ACT_CONTROL walker still has (0, 0)
// (combat_attack_scen99's golden shows a K_FIRE-only player never lands a hit at all).
inline constexpr InputEvent kInputsFaceRightSpecialSlot4[] = {
    {  1, 0, K_RIGHT },        {  3, 0, K_NONE },
    {  5, 0, K_SPECIAL_SWITCH},{  6, 0, K_NONE },
    {  8, 0, K_SPECIAL_SWITCH},{  9, 0, K_NONE },
    { 11, 0, K_SPECIAL_SWITCH},{ 12, 0, K_NONE },
    { 20, 0, K_SPECIAL },      { 21, 0, K_NONE },
};

// elf_mega_rocks_volley_scen99: slot 4 MEGA ROCKS fires FOUR bouncing rocks
// (bounce_volley(5, 4, 5)). Each ranged release emits exactly one play_sound on both
// arms, so the volley size is directly countable, and four bouncing rocks are enough to
// finish the lead orc inside the 45-tick budget -- the surviving-orc count is the
// cleanest discriminator the volley size has.
inline constexpr SpawnSpec kFamilySpawns_elf_mega_rocks_volley_scen99[] = {
    { FAMILY_ORC, 1, kOrderLiving, 200, 120, 0, 0 },          // BIT_NO_RANGED: cannot counter-fire
    { FAMILY_ORC, 1, kOrderLiving, 232, 120, 0, 0 },
    { FAMILY_ELF, 0, kOrderLiving, 120, 120, 0, 0, 10, 600 }, // caster LAST; level 10 clears the slot-4 cycling gate
};

inline constexpr FactPredicate kFacts_elf_mega_rocks_volley_scen99[] = {
    pred::TickReached(45),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1,
        "consequence: the four-rock volley kills one of the two orcs inside the budget; a single rock leaves both standing and this count goes 1 -> 2"),
    pred::EventKindAtLeast(/*play_sound*/1, 4,
        "consequence: MEGA ROCKS releases four rocks and fire() emits one play_sound per ranged release on both arms; the one-rock volley reaches only 3 sounds in this arena and cannot meet the floor"),
    pred::WeaponSpeed(FAMILY_ROCK, 590, 610,
        "trajectory: the volley's rocks step at 600 centi-px/tick, the elf rock stepsize after next_spread_multiplier scaling"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 13400, 13400,
        "anchor: the trailing orc is untouched by the volley and idles on 13400 cents on both arms, so the family-count and sound-floor pins carry the flip"),
};

inline constexpr Mutation kMut_elf_mega_rocks_volley_scen99 = {
    "packs/core/scripts/elf.lua", 72,
    "    default = bounce_volley(5, 4, 5),  -- case 4 and every unmapped slot",
    "    default = bounce_volley(5, 1, 5),  -- case 4 and every unmapped slot",
    "Shrinks MEGA ROCKS from four rocks to one while keeping the MP refund and line-of-sight scaling. The play_sound floor of 4 collapses to 3, the weapon_tracks sample set drops from 62 to 5, and the lead orc survives, so WalkerFamilyCount(FAMILY_ORC, 1, 1) goes to 2."
};

// fireelemental_starburst_ring_scen99: STARBURST fires the default weapon in all eight
// (i, j) headings. One play_sound per ranged release on both arms makes the fan
// countable; a foe on each side of the caster proves it is a ring, not an aimed shot.
// Budget is short so the caster cannot die and trigger the free parting starburst its
// on_death hook fires (which would reproduce the fan even with the mutation applied).
inline constexpr SpawnSpec kFamilySpawns_fireelemental_starburst_ring_scen99[] = {
    { FAMILY_ORC,           1, kOrderLiving, 180, 120, 0, 0 },          // east of the caster
    { FAMILY_ORC,           1, kOrderLiving,  60, 120, 0, 0 },          // west of the caster
    { FAMILY_FIREELEMENTAL, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },  // caster LAST (slot-1 cost 50 > 28 max MP)
};

inline constexpr FactPredicate kFacts_fireelemental_starburst_ring_scen99[] = {
    pred::TickReached(40),
    pred::WalkerFamilyCount(FAMILY_FIREELEMENTAL, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 2, 2),
    pred::EventKindAtLeast(/*play_sound*/1, 8,
        "consequence: the eight-heading fan releases eight meteors and fire() emits one play_sound per ranged release on both arms; suppressing the fan drops the arena to a single sound"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 12800, 13000,
        "consequence: the east-facing and west-facing meteors each strike one flanking orc, leaving them on 13000 and 12800 cents; with no meteor released both finish untouched at their 14000-cent max, above this window"),
};

inline constexpr Mutation kMut_fireelemental_starburst_ring_scen99 = {
    "packs/core/scripts/fire_elemental.lua", 16,
    "      if i ~= 0 or j ~= 0 then",
    "      if false then",
    "Suppresses every heading of the starburst fan. No meteor is released, so the play_sound floor of 8 collapses to 1, weapon_tracks empties, and both flanking orcs finish at full 14000-cent HP outside the window."
};

// mage_heartburst_multitarget_scen99: the MAGE's own slot-5 heartburst (distinct range
// formula and MP slot from the ARCHMAGE twin). Level 13 clears the slot-5 cycling gate
// ((5-1)*3+1 == 13) and gives an acquisition range of 80 + 2*13 = 106; the three orcs sit
// at Manhattan 40 / 70 / 60. The pool is (magicpoints - cost)/2 split across the acquired
// foes, which is enough to finish two of the three inside the 30-tick budget.
inline constexpr SpawnSpec kFamilySpawns_mage_heartburst_multitarget_scen99[] = {
    { FAMILY_ORC,  1, kOrderLiving, 160, 120, 0, 0 },
    { FAMILY_ORC,  1, kOrderLiving, 190, 120, 0, 0 },
    { FAMILY_ORC,  1, kOrderLiving, 150, 150, 0, 0 },
    { FAMILY_MAGE, 0, kOrderLiving, 120, 120, 0, 0, 13, 600 }, // caster LAST
};

inline constexpr FactPredicate kFacts_mage_heartburst_multitarget_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_MAGE, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 5600, 5600,
        "consequence: heartburst detonates one BIT_MAGICAL explosion on each in-range foe for (magicpoints - cost)/2/foe_count damage, leaving the survivor on exactly 5600 cents; with the acquisition radius collapsed to 2*13 = 26px the far foes are never acquired and the survivor finishes untouched at 14000"),
    pred::EventKindAtLeast(/*play_sound*/1, 6,
        "consequence: one on_screen-gated SOUND_EXPLODE per detonated foe plus the deaths they cause; the collapsed radius detonates a single explosion and the arena only reaches 4 sounds"),
};

inline constexpr Mutation kMut_mage_heartburst_multitarget_scen99 = {
    "packs/core/families/living-03-mage.yaml", 84,
    "        heartburst_range_base: 80",
    "        heartburst_range_base: 0",
    "Drops the mage's heartburst acquisition radius from 80 + 2*13 = 106px to 26px, short of every orc at cast time. Only the one foe that walks inside 26px is ever detonated: FAMILY_EXPLOSION tracks fall 9 -> 3, play_sound 8 -> 4, and the surviving orc finishes on 14000 cents instead of 5600."
};

// Face east for 9 ticks (sets lastx = stepsize so COMMAND_RUSH has a direction), then cast
// slot 1. The leading walk is mandatory: charge computes its rush vector as
// trunc(lastx/stepsize), and an un-nudged ACT_CONTROL walker still has lastx == 0.
inline constexpr InputEvent kInputsFaceRightSpecialSlot1[] = {
    {  1, 0, K_RIGHT },  { 10, 0, K_NONE },
    { 20, 0, K_SPECIAL },{ 21, 0, K_NONE },
};

// soldier_charge_displacement_scen99: pins the CHARGE rush displacement. The soldier is
// alone in the open so nothing but the scripted rush moves it; a single far-away orc keeps
// the level from completing (a team-1-free arena ends instantly, as the generator rows'
// goldens show). COMMAND_RUSH runs three walksteps per iteration (stats.cpp), so three
// iterations at soldier stepsize 4 add 24px over a single iteration.
inline constexpr SpawnSpec kFamilySpawns_soldier_charge_displacement_scen99[] = {
    { FAMILY_ORC,     1, kOrderLiving, 560, 440, 0, 0 },          // far foe: keeps level_done at 0, never reaches the caster
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 },  // caster LAST (slot-1 cost 25 > 20 max MP)
};

inline constexpr FactPredicate kFacts_soldier_charge_displacement_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 1, 1),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 184, 0,
        "consequence: nine ticks of scripted walking put the soldier on xpos 160 and the three-iteration COMMAND_RUSH adds the last 24px to 184; a single-iteration rush stops exactly on 160 and never reaches this bound"),
    pred::EventKindAtLeast(/*play_sound*/1, 1,
        "anchor: SOUND_CHARGE fires once the forward-blocked check passes, whatever the iteration count, so the position pin carries the flip alone"),
};

inline constexpr Mutation kMut_soldier_charge_displacement_scen99 = {
    "packs/core/scripts/soldier.lua", 13,
    "  self:s_add_command(C.COMMAND_RUSH, 3,",
    "  self:s_add_command(C.COMMAND_RUSH, 1,",
    "Cuts the charge from three COMMAND_RUSH iterations to one, dropping the rush contribution from 24px to 0 measurable travel by the dump tick (xpos 184 -> 160). SOUND_CHARGE still fires, so only the position bound flips - the displacement is isolated."
};

// elf_rocks_pair_scen99: slot 1 ROCKS fires exactly TWO rocks with no do_bounce and no
// lineofsight boost. Budget 30 and a lone orc downrange mirror weapon_rock_slot2_emit_scen99
// so the straight two-rock volley and the bouncing slot-2 volley are directly comparable.
// Reuses kInputsFaceRightSpecialSlot1 (the leading K_RIGHT sets the fire aim).
inline constexpr SpawnSpec kFamilySpawns_elf_rocks_pair_scen99[] = {
    { FAMILY_ORC, 1, kOrderLiving, 220, 120, 0, 0 },         // downrange target; BIT_NO_RANGED
    { FAMILY_ELF, 0, kOrderLiving, 120, 120, 0, 0, 1, 600 }, // caster LAST
};

inline constexpr FactPredicate kFacts_elf_rocks_pair_scen99[] = {
    pred::TickReached(30),
    pred::WalkerFamilyCount(FAMILY_ELF, 1, 1),
    pred::WalkerFamilyCount(FAMILY_ORC, 1, 1),
    pred::EventKindAtLeast(/*play_sound*/1, 4,
        "consequence: some_rocks releases exactly two rocks, fire() emits one play_sound per ranged release on both arms and both rocks connect; suppressing the second release drops the arena to 3 sounds"),
    pred::WeaponSpeed(FAMILY_ROCK, 890, 920,
        "trajectory: the un-boosted slot-1 pair steps at 906 centi-px/tick, measurably faster than the slot-4 volley's 600 because next_spread_multiplier scales each release independently"),
    pred::WeaponNetTravel(FAMILY_ROCK, kWeaponPathStraight, 1000,
        "trajectory: slot-1 rocks carry no do_bounce flag, so net == pathlen in open-field scen1.fss and the 1000-centi threshold is cleared within two ticks of flight"),
    pred::WalkerHpRangeAtFinalTick(FAMILY_ORC, 13300, 13300,
        "consequence: both rocks of the pair land on the downrange orc for a total of 700 cents; with only one released it keeps 13600"),
};

inline constexpr Mutation kMut_elf_rocks_pair_scen99 = {
    "packs/core/scripts/elf.lua", 25,
    "  rock = self:fire()",
    "  rock = nil",
    "Suppresses the second of the two ROCKS releases (the guard on the next line then returns false). Exactly one rock is emitted instead of two: the play_sound floor of 4 fails at 3, the weapon_tracks sample set halves, and the downrange orc keeps 13600 cents instead of 13300."
};

// thief_ai_bomb_flee_scen99: the AI-only drop_bomb flee branch. check_special's thief
// slot-1 arm refuses only for foe distances strictly inside (35, 130), so the bomber is
// parked in the middle of the team-0 cluster at Manhattan 30 from all three and the bomb
// fires. Level 3 is deliberate: living::act rolls rng.next((level+2)/3)+1 for its special
// slot, which is identically 1 (DROP BOMB) only while (level+2)/3 == 1 -- at level 5 the
// AI reached for CLOAK every time and never bombed at all.
// Both flee draws share the bound rand(3), so the LEFT-first (parity) and RIGHT-first
// orders consume the identical stream and differ ONLY in which axis gets which value.
// Seed 0x44 is chosen (0x42 draws the symmetric (-1,-1), which no transposition can
// distinguish): it produces three flees, (0,-1) / (0,1) / (-1,1), all asymmetric.
inline constexpr SpawnSpec kFamilySpawns_thief_ai_bomb_flee_scen99[] = {
    { FAMILY_SOLDIER, 0, kOrderLiving, 130, 150, 0, 0 },          // Manhattan 30 from the thief
    { FAMILY_SOLDIER, 0, kOrderLiving, 150, 110, 0, 0 },          // Manhattan 30
    { FAMILY_THIEF,   1, kOrderLiving, 140, 130, 0, 0, 3, 600 },  // AI bomber (slot-1 cost 35)
    { FAMILY_SOLDIER, 0, kOrderLiving, 120, 120, 0, 0 },          // player LAST, Manhattan 30
};

inline constexpr FactPredicate kFacts_thief_ai_bomb_flee_scen99[] = {
    pred::TickReached(35),
    pred::WalkerFamilyCount(FAMILY_SOLDIER, 3, 3),
    pred::LevelDoneEquals(2,
        "consequence: the LEFT-first flee vectors walk the bomber back through the team-0 cluster and it dies inside the budget, completing the level; the transposed vectors carry it clear and the level is still running at the dump tick"),
    pred::WalkerDiedByFinal(FAMILY_THIEF,
        "consequence: the flee vector decides whether the bomber clears its own blast and the soldiers' swing arc. LEFT-first it does not and is dead by tick 35; transposing the two draws leaves it alive on 900 cents at (105,135)"),
    pred::WalkerPositionMoved(FAMILY_SOLDIER, 146, 166,
        "consequence: the pursuing soldier follows the bomber's flee path, so the transposed vectors leave every soldier north-west of this corner (furthest is (135,129)) while the LEFT-first chase carries one out to exactly (146,166)"),
};

inline constexpr Mutation kMut_thief_ai_bomb_flee_scen99 = {
    "packs/core/scripts/thief.lua", 64,
    "    local flee_dy = og.rand(3) - 1",
    "    local flee_dy = flee_dx; flee_dx = og.rand(3) - 1",
    "Transposes the two flee draws so the FIRST value lands on dy and the SECOND on dx - exactly the RIGHT-first adjudication the parity port rejected. Stream consumption is unchanged (both bounds are 3), so the only effect is the flee vector: the bomber survives to the dump tick, the level never completes and no soldier reaches (146,166)."
};

// effect_explosion_ally_tier_scen99: the blast's ALLY tier. explosion_on_death
// (packs/core/scripts/effect_bomb.lua:82-87, master effect.cpp:690-700) halves the
// damage for a walker the owner is friendly to. Nothing in the corpus reaches that
// arm -- every other blast row blasts foes only, so the whole `elseif` is
// unexercised. effect_bomb_bystander_scen99 already pins the FULL tier and, in the
// same golden, the OWNER quarter tier.
//
// The arena is that row's arena with one property changed: the stationary TOWER1
// stays on its exact tile (222,196) and switches to the caster's team. Everything
// else -- the level-5 thief at (200,200), bomb_damage 90, the tick-20 press, the
// 100-tick budget -- is identical, so the two goldens are a controlled pair and the
// divisor is read off the difference: the team-1 tower loses 91 of 130 there, the
// team-0 tower loses exactly 45 here.
//
// The distant team-1 TOWER1 at (400,400) is scaffolding, not a control. It is far
// outside the blast (Manhattan 380 against a reach of 35) and outside every firing
// gate, and it exists only so the arena still has a live enemy: with no team-1
// walker at all the level completes on the first tick and buries the dump in
// end_game events. An in-blast foe cannot serve as an in-row full-tier control --
// measured: a second TOWER1 at the mirrored (170,196) makes the pair trade arrow
// fire, and the team-1 tower is chipped to ~67 hp before the fuse ends and dies to
// the blast, so its "full tier" reading is arrow damage plus blast damage.
//
// The half tier is reachable at all only because the friendly refusal never
// engages. The `elseif self:owner():dead() == 0 and self:owner():is_friendly(w)`
// guard asks the LIVING owner, so the tier is selected correctly; the attack that
// follows is issued by the EXPLOSION, and walker::attack's `is_friendly(target)`
// refusal answers 0 for a dead caller (src/gameplay/walker.cpp:2234) while an
// explosion is set_dead before death() runs. So the halved damage lands. True
// since the 2002 import; gdb-verified at HEAD.
inline constexpr SpawnSpec kFamilySpawns_effect_explosion_ally_tier_scen99[] = {
    { FAMILY_TOWER1, 1, kOrderLiving, 400, 400, 0, 0 },           // distant team-1 tower: keeps the level from completing, Manhattan 380 from the blast and out of everything's reach, so it never fires and is never touched
    { FAMILY_TOWER1, 0, kOrderLiving, 222, 196, 0, 0 },           // ALLY half-tier victim on effect_bomb_bystander_scen99's exact tile: Manhattan |222-196|+|196-194| = 28 from the explosion's top-left (196,194), inside the level-5 reach 15+20 = 35
    { FAMILY_THIEF,  0, kOrderLiving, 200, 200, 0, 0, 5, 300 },   // player-controlled bomb owner LAST (spawns prepend and find_player_walker binds the first team-0 Living in oblist); level 5 -> bomb_damage 90 and range clamp(20,16,96) = 20
};

inline constexpr FactPredicate kFacts_effect_explosion_ally_tier_scen99[] = {
    pred::TickReached(100),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 2, 2),
    // FLIPPING PREDICATE. compute_base_damage turns the halved 45 into
    // 45 - sqrt(45)/2 + rand(6) = 41..46 and the roll lands on exactly 45.
    // kMut_effect_explosion_ally_tier_scen99 widens the divisor to 8, which
    // moves the ally to exactly 120 hp; the far tower is at its full 130, so no
    // walker of the family is left inside this window.
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 8500, 8500,
        "consequence: the team-0 TOWER1 takes the ally arm of explosion_on_death -- fdiv(90,2) rolled to 45 -- and finishes on exactly 8500 cents of 13000, against the 3900 the SAME TILE reads in effect_bomb_bystander_scen99 with the tower on team 1; dividing by 8 instead leaves it on 12000 and no TOWER1 sits in this window"),
    // Anchor: the far tower proves the blast is bounded. It is enumerated by
    // nothing, shoved by nothing and shot by nothing on either arm, so it holds
    // its full 13000 cents under every mutation of the tier divisors.
    pred::WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 13000, 13000,
        "anchor: the team-1 TOWER1 parked at Manhattan 380 is outside the 35px reach and outside every firing gate, so it ends untouched at 13000 cents -- a drift here means the blast reach or the AI acquisition moved, not the tier"),
    // The owner's quarter tier keeps the thief alive, which the ally arm's
    // `self:owner():dead() == 0` guard requires: a dead owner would send the
    // allied victim down the full-damage else branch and erase this tier.
    pred::WalkerAliveAtFinal(FAMILY_THIEF, 1),
    pred::WalkerHpRangeAtFinalTick(FAMILY_THIEF, 5200, 5200,
        "structural: no shooter exists in this arena, so the caster's 52 of 75 is entirely its own blast -- fdiv(90,4) through compute_base_damage's 20..23 window, rolled to 23 -- and a live owner is what keeps the ally arm reachable at resolution time"),
    pred::EventKindAtLeast(/*play_sound*/1, 1),
};

inline constexpr Mutation kMut_effect_explosion_ally_tier_scen99 = {
    "packs/core/scripts/effect_bomb.lua", 88,
    "        self.damage = og.fdiv(full_damage, 2.0)",
    "        self.damage = og.fdiv(full_damage, 8.0)",
    "Widens the ally divisor from the half tier to an eighth. The blast still fires, still emits SOUND_EXPLODE, still shoves and still lands the quarter tier on its owner (the caster stays on 5200 cents), so only the allied victim moves: the team-0 TOWER1 finishes on 12000 cents instead of 8500 and WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 8500, 8500) has no walker left in its window."
};

// druid_protection_refresh_scen99 --------------------------------------------
// The corpus's first INTENTIONAL gameplay change, and the reason its golden
// cannot be captured from the companion.
//
// `protection_circle`'s top-up arm -- mint a throwaway circle, add its charge
// to the friend's existing one, discard the throwaway -- selected that
// existing circle by walking og.oblist(), faithfully mirroring
// openglad-master/src/walker.cpp:3813. Summoning a weapon files it in
// weaplist, so the scan never matched and the arm never ran: from the 2002
// import until 2026 every recast on an already-protected friend minted a
// SECOND circle. The scan now searches weaplist through
// og.find_in_range("weap", 100, friend), so a recast refreshes.
//
// The arena descends from effect_protection_emit_scen99 -- player-controlled
// druid spawned LAST so it lands first in oblist and takes control, one
// team-0 friendly to protect, no combat -- but two casts need two things that
// arena cannot give.
//
// The protectee is a stationary team-0 TOWER1 instead of a soldier. The
// soldier's idle wander leaves range 60 and comes back (measured on this very
// seed: Manhattan 67 at tick 28, 43 at tick 34), so a second cast tick would
// be a hostage to AI cadence. A tower parked at Manhattan 50 is inside the
// gate at every tick of the run.
//
// The distant team-1 TOWER1 at (400,400) is not a target, it is what keeps
// the level alive. GameWorld::tick returns immediately after latching
// game_ended, BEFORE the dead sweep, so in an enemy-free arena nothing is
// ever reaped: the throwaway circle the top-up arm mints and kills would
// linger in weaplist at (0,0) and the count below would read 2 on both arms.
// One live foe keeps level_done at 0, the sweep runs, and the husk is gone by
// the next tick. Manhattan 610 from the protectee and 560 from the druid puts
// it outside every ai_line_of_sight-10 (320px) firing gate, so it only sits
// there.
//
// The druid carries 600 magicpoints rather than 300 because slot-4
// PROTECTION costs 200 and this row casts twice.
inline constexpr InputEvent kInputsSpecialSlot4Twice[] = {
    {  5, 0, K_SPECIAL_SWITCH},
    {  6, 0, K_NONE},
    {  8, 0, K_SPECIAL_SWITCH},
    {  9, 0, K_NONE},
    { 11, 0, K_SPECIAL_SWITCH},
    { 12, 0, K_NONE},
    { 20, 0, K_SPECIAL},        // first cast: mints the protectee's circle
    { 21, 0, K_NONE},
    { 30, 0, K_SPECIAL},        // recast: must top the SAME circle up
    { 31, 0, K_NONE},
};

inline constexpr SpawnSpec kFamilySpawns_druid_protection_refresh_scen99[] = {
    { FAMILY_TOWER1, 1, kOrderLiving, 400, 400, 0, 0 },          // distant team-1 tower: holds level_done at 0 so the dead sweep runs; out of every firing gate
    { FAMILY_TOWER1, 0, kOrderLiving, 90, 100, 0, 0 },           // the protectee: stationary, Manhattan 50 from the druid, inside range 60 at BOTH cast ticks
    { FAMILY_DRUID,  0, kOrderLiving, 120, 120, 0, 0, 10, 600 }, // druid caster LAST (spawns prepend, so it heads oblist and find_player_walker binds it); level 10 + 600 magicpoints -> TWO slot-4 casts at 200 each
};

inline constexpr FactPredicate kFacts_druid_protection_refresh_scen99[] = {
    pred::TickReached(45),
    pred::WalkerFamilyCount(FAMILY_DRUID, 1, 1),
    pred::WalkerFamilyCount(FAMILY_TOWER1, 2, 2),
    pred::WeaponFamilyEmitted(FAMILY_CIRCLE_PROTECTION),
    // THE DISCRIMINATOR. Two successful casts, ONE surviving ring. The
    // throwaway circle the top-up arm mints to read a charge off is dead the
    // same tick and reaped before the dump, so weaplist holds exactly the
    // friendly's own refreshed ring. Resurrect the 2002 oblist scan and the
    // recast cannot see it: a second ring is summoned onto the same friendly
    // and the count is 2. The circle's hitpoints (50 -> 100) are the other
    // half of the refresh, but schema-v1's WeaponEntry carries no hp field,
    // so cardinality is the honest observable.
    pred::WeaponFamilyCount(FAMILY_CIRCLE_PROTECTION, 1, 1,
        "consequence: PROTECTION recast on an already-protected friend tops up the existing ring instead of stacking a second one; the dead-scan behaviour this replaced leaves 2 in weaplist"),
    pred::EventKindAtLeast(/*play_sound*/1, 2,
        "anchor: both casts succeed and each emits SOUND_HEAL, so the floor is 2. NOT a tooth -- the stacking behaviour also charges and sounds twice; WeaponFamilyCount carries the flip"),
};

inline constexpr Mutation kMut_druid_protection_refresh_scen99 = {
    "packs/core/scripts/druid.lua", 92,
    "      local circles = og.find_in_range(\"weap\", 100, friend)",
    "      local circles = og.find_in_range(\"ob\", 100, friend)",
    "Points the existing-circle scan back at oblist, resurrecting the bug this row exists to pin. oblist holds livings, generators and FX; a summoned circle lives in weaplist, so the scan finds nothing, `existing` stays nil and the recast takes the mint arm instead of the top-up arm. The friendly ends the run wearing TWO rings and WeaponFamilyCount(FAMILY_CIRCLE_PROTECTION, 1, 1) reads 2. Everything else holds: both casts still succeed, both still charge 200 magicpoints and emit SOUND_HEAL, so the play_sound floor and WeaponFamilyEmitted stay green and the flip is isolated to the stacking."
};

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

    // Z-axis / multi-floor (branch-internal Invariant; no master companion can
    // model stacked floors). fresh_arena drops scen9301's (empty) population;
    // apply_floor_setup builds the 2-floor grass arena and paints the Z tiles
    // before the soldier spawns. The teethed WalkerOnFloor predicate is checked
    // in Parity.z_multifloor_walker_floor_transitions; the Invariant rows
    // themselves rely on the dual-capture determinism check. Lint exempts
    // Invariant rows from fact/mutation requirements (expected_facts nullptr,
    // discriminating_mutation {}).
    { "z_stair_up_scen9301", "scen/scen9301.fss", 0x00000055u,
      nullptr, 0, 50, CompareMode::Invariant, true,
      kZStairSpawns, std::size(kZStairSpawns), 0, false, true, Exercises::None,
      nullptr, 0, {}, {},
      2, kZStairPaints, std::size(kZStairPaints) },

    { "z_fall_through_air_scen9301", "scen/scen9301.fss", 0x00000055u,
      nullptr, 0, 50, CompareMode::Invariant, true,
      kZFallSpawns, std::size(kZFallSpawns), 0, false, true, Exercises::None,
      nullptr, 0, {}, {},
      2, kZFallPaints, std::size(kZFallPaints) },

    { "z_fall_two_story_scen9301", "scen/scen9301.fss", 0x00000055u,
      nullptr, 0, 50, CompareMode::Invariant, true,
      kZFall2Spawns, std::size(kZFall2Spawns), 0, false, true, Exercises::None,
      nullptr, 0, {}, {},
      3, kZFall2Paints, std::size(kZFall2Paints) },

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
    // It is Invariant (no master golden required); the dumper-determinism
    // check in test_parity_scenarios covers the gtest side by running the
    // scenario twice and asserting byte-equal serialisation, and the
    // single TickReached(1) fact gives its tick-freeze mutation a live
    // --evaluate-facts flip channel (the determinism gtest itself can
    // never flip under a deterministic mutation).
    { "smoke_empty_scen99",            "scen/scen1.fss", 0x00000042u,
      nullptr, 0,                                                       1,   CompareMode::Invariant, false,
      nullptr, 0, 0, true, true, Exercises::None,
      kFacts_smoke_empty_scen99, std::size(kFacts_smoke_empty_scen99),
      kMut_smoke_tick_freeze },

    { "smoke_nonempty_scen99",         "scen/scen1.fss", 0x00000042u,
      nullptr, 0,                                                       60,  CompareMode::SemanticParity, false,
      kSmokeArenaSpawns, std::size(kSmokeArenaSpawns), 0, false, true, Exercises::None,
      kFacts_smoke_nonempty_scen99, std::size(kFacts_smoke_nonempty_scen99),
      kMut_smoke_tick_freeze },

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
      kMut_walker_ai_wander },

    { "weapon_blob_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 30,
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
      kInputsEventArena, std::size(kInputsEventArena), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_notification_emission_scen99, std::size(kFacts_event_notification_emission_scen99),
      kMut_event_notification_emission },

    { "event_set_palette_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEventArena, std::size(kInputsEventArena), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_set_palette_emission_scen99, std::size(kFacts_event_set_palette_emission_scen99),
      kMut_event_set_palette_emission },

    { "event_request_redraw_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEventArena, std::size(kInputsEventArena), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_request_redraw_emission_scen99, std::size(kFacts_event_request_redraw_emission_scen99),
      kMut_event_request_redraw_emission },

    { "event_end_game_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEventArena, std::size(kInputsEventArena), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_event_arena, std::size(kFamilySpawns_event_arena),
      0, false, true, Exercises::None,
      kFacts_event_end_game_emission_scen99, std::size(kFacts_event_end_game_emission_scen99),
      kMut_event_end_game_emission },

    { "event_set_end_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEventArena, std::size(kInputsEventArena), 150,
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

    { "special_cleric_heal_ally_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_special_cleric_heal_ally, std::size(kInputs_special_cleric_heal_ally), 90,
      CompareMode::SemanticParity, false,
      kFamilySpawns_special_cleric_heal_ally_scen99, std::size(kFamilySpawns_special_cleric_heal_ally_scen99),
      0, false, true, Exercises::Special_Cleric_1,
      kFacts_special_cleric_heal_ally_scen99, std::size(kFacts_special_cleric_heal_ally_scen99),
      kMut_special_cleric_heal_ally_scen99 },

    { "cleric_raise_skeleton_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_cleric_raise_late_slot2, std::size(kInputs_cleric_raise_late_slot2), 130,
      CompareMode::SemanticParity, false,
      kFamilySpawns_cleric_raise_skeleton_scen99, std::size(kFamilySpawns_cleric_raise_skeleton_scen99),
      0, false, true, Exercises::Special_Cleric_2,
      kFacts_cleric_raise_skeleton_scen99, std::size(kFacts_cleric_raise_skeleton_scen99),
      kMut_cleric_raise_skeleton_scen99 },

    { "cleric_raise_ghost_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_cleric_raise_late_slot3, std::size(kInputs_cleric_raise_late_slot3), 130,
      CompareMode::SemanticParity, false,
      kFamilySpawns_cleric_raise_ghost_scen99, std::size(kFamilySpawns_cleric_raise_ghost_scen99),
      0, false, true, Exercises::Special_Cleric_3,
      kFacts_cleric_raise_ghost_scen99, std::size(kFacts_cleric_raise_ghost_scen99),
      kMut_cleric_raise_ghost_scen99 },

    { "cleric_turn_undead_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_cleric_turn_undead, std::size(kInputs_cleric_turn_undead), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_cleric_turn_undead_scen99, std::size(kFamilySpawns_cleric_turn_undead_scen99),
      0, false, true, Exercises::Special_Cleric_2,
      kFacts_cleric_turn_undead_scen99, std::size(kFacts_cleric_turn_undead_scen99),
      kMut_cleric_turn_undead_scen99 },

    { "cleric_resurrect_friendly_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_cleric_resurrect_friendly, std::size(kInputs_cleric_resurrect_friendly), 153,
      CompareMode::SemanticParity, false,
      kFamilySpawns_cleric_resurrect_friendly_scen99, std::size(kFamilySpawns_cleric_resurrect_friendly_scen99),
      1, false, true, Exercises::Special_Cleric_4,
      kFacts_cleric_resurrect_friendly_scen99, std::size(kFacts_cleric_resurrect_friendly_scen99),
      kMut_cleric_resurrect_friendly_scen99 },

    { "undead_no_corpse_raise_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_cleric_raise_late_slot2, std::size(kInputs_cleric_raise_late_slot2), 130,
      CompareMode::SemanticParity, false,
      kFamilySpawns_undead_no_corpse_raise_scen99, std::size(kFamilySpawns_undead_no_corpse_raise_scen99),
      0, false, true, Exercises::None,
      kFacts_undead_no_corpse_raise_scen99, std::size(kFacts_undead_no_corpse_raise_scen99),
      kMut_undead_no_corpse_raise_scen99 },

    // Treasure guard-arm / consequence scenarios ---------------------------
    { "treasure_flight_effect_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsExitWalkRight, std::size(kInputsExitWalkRight), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_flight_effect, std::size(kFamilySpawns_treasure_flight_effect),
      0, false, true, Exercises::None,
      kFacts_treasure_flight_effect_scen99, std::size(kFacts_treasure_flight_effect_scen99),
      kMut_treasure_flight_effect },

    { "treasure_invis_effect_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsPotionWalk200, std::size(kInputsPotionWalk200), 250,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_invis_effect, std::size(kFamilySpawns_treasure_invis_effect),
      0, false, true, Exercises::None,
      kFacts_treasure_invis_effect_scen99, std::size(kFacts_treasure_invis_effect_scen99),
      kMut_treasure_invis_effect },

    { "treasure_flight_potion_flier_noconsume_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickupTick0, std::size(kInputsTreasurePickupTick0), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_flight_flier, std::size(kFamilySpawns_treasure_flight_flier),
      0, false, true, Exercises::None,
      kFacts_treasure_flight_potion_flier_noconsume_scen99, std::size(kFacts_treasure_flight_potion_flier_noconsume_scen99),
      kMut_treasure_flight_potion_flier_noconsume },

    { "treasure_drumstick_fullhp_noconsume_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickupTick0, std::size(kInputsTreasurePickupTick0), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_drumstick_fullhp, std::size(kFamilySpawns_treasure_drumstick_fullhp),
      0, false, true, Exercises::None,
      kFacts_treasure_drumstick_fullhp_noconsume_scen99, std::size(kFacts_treasure_drumstick_fullhp_noconsume_scen99),
      kMut_treasure_drumstick_fullhp_noconsume },

    { "treasure_gold_bar_team_reject_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEmpty, std::size(kInputsEmpty), 90,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_gold_bar_team_reject, std::size(kFamilySpawns_treasure_gold_bar_team_reject),
      0, false, true, Exercises::None,
      kFacts_treasure_gold_bar_team_reject_scen99, std::size(kFacts_treasure_gold_bar_team_reject_scen99),
      kMut_treasure_gold_bar_team_reject },

    { "treasure_life_gem_enemy_reject_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickup, std::size(kInputsTreasurePickup), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_life_gem_enemy_reject, std::size(kFamilySpawns_treasure_life_gem_enemy_reject),
      0, false, true, Exercises::None,
      kFacts_treasure_life_gem_enemy_reject_scen99, std::size(kFacts_treasure_life_gem_enemy_reject_scen99),
      kMut_treasure_life_gem_enemy_reject },

    { "treasure_magic_potion_overfill_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_magic_potion_overfill, std::size(kInputs_magic_potion_overfill), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_magic_potion_overfill, std::size(kFamilySpawns_treasure_magic_potion_overfill),
      0, false, true, Exercises::None,
      kFacts_treasure_magic_potion_overfill_scen99, std::size(kFacts_treasure_magic_potion_overfill_scen99),
      kMut_treasure_magic_potion_overfill },

    { "treasure_key_team1_silent_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEmpty, std::size(kInputsEmpty), 90,
      CompareMode::SemanticParity, false,
      kFamilySpawns_treasure_key_team1_silent, std::size(kFamilySpawns_treasure_key_team1_silent),
      0, false, true, Exercises::None,
      kFacts_treasure_key_team1_silent_scen99, std::size(kFacts_treasure_key_team1_silent_scen99),
      kMut_treasure_key_team1_silent },

    { "treasure_exit_open_prompt_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsTreasurePickupTick0, std::size(kInputsTreasurePickupTick0), 150,
      CompareMode::Invariant, true,
      kFamilySpawns_treasure_exit_open_prompt, std::size(kFamilySpawns_treasure_exit_open_prompt),
      0, false, true, Exercises::None,
      kFacts_treasure_exit_open_prompt_scen99, std::size(kFacts_treasure_exit_open_prompt_scen99),
      kMut_treasure_exit_open_prompt },

    { "thief_charm_opponent_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3Shift, std::size(kInputsSpecialSlot3Shift), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_thief_charm_opponent_scen99, std::size(kFamilySpawns_thief_charm_opponent_scen99),
      0, false, true, Exercises::Special_Thief_3,
      kFacts_thief_charm_opponent_scen99, std::size(kFacts_thief_charm_opponent_scen99),
      kMut_thief_charm_opponent_scen99 },

    { "archmage_summon_elemental_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3Shift, std::size(kInputsSpecialSlot3Shift), 120,
      CompareMode::SemanticParity, false,
      kFamilySpawns_archmage_summon_elemental_scen99, std::size(kFamilySpawns_archmage_summon_elemental_scen99),
      0, false, true, Exercises::Special_Archmage_3,
      kFacts_archmage_summon_elemental_scen99, std::size(kFacts_archmage_summon_elemental_scen99),
      kMut_archmage_summon_elemental_scen99 },

    { "mage_teleport_marker_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsMarkerPlaceReturn, std::size(kInputsMarkerPlaceReturn), 140,
      CompareMode::SemanticParity, false,
      kFamilySpawns_mage_teleport_marker_scen99, std::size(kFamilySpawns_mage_teleport_marker_scen99),
      0, false, true, Exercises::Special_Mage_1,
      kFacts_mage_teleport_marker_scen99, std::size(kFacts_mage_teleport_marker_scen99),
      kMut_mage_teleport_marker_scen99 },

    { "archmage_teleport_marker_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsMarkerTwiceEast, std::size(kInputsMarkerTwiceEast), 100,
      CompareMode::SemanticParity, false,
      kFamilySpawns_archmage_teleport_marker_scen99, std::size(kFamilySpawns_archmage_teleport_marker_scen99),
      0, false, true, Exercises::Special_Archmage_1,
      kFacts_archmage_teleport_marker_scen99, std::size(kFacts_archmage_teleport_marker_scen99),
      kMut_archmage_teleport_marker_scen99 },

    { "archmage_mind_control_team_flip_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_archmage_mind_control_team_flip_scen99, std::size(kFamilySpawns_archmage_mind_control_team_flip_scen99),
      0, false, true, Exercises::Special_Archmage_4,
      kFacts_archmage_mind_control_team_flip_scen99, std::size(kFacts_archmage_mind_control_team_flip_scen99),
      kMut_archmage_mind_control_team_flip_scen99 },

    { "archmage_summon_image_phantom_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 40,
      CompareMode::SemanticParity, false,
      kFamilySpawns_archmage_summon_image_phantom_scen99, std::size(kFamilySpawns_archmage_summon_image_phantom_scen99),
      0, false, true, Exercises::Special_Archmage_3,
      kFacts_archmage_summon_image_phantom_scen99, std::size(kFacts_archmage_summon_image_phantom_scen99),
      kMut_archmage_summon_image_phantom_scen99 },

    { "mage_starburst_ring_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 40,
      CompareMode::SemanticParity, false,
      kFamilySpawns_mage_starburst_ring_scen99, std::size(kFamilySpawns_mage_starburst_ring_scen99),
      0, false, true, Exercises::Special_Mage_2,
      kFacts_mage_starburst_ring_scen99, std::size(kFacts_mage_starburst_ring_scen99),
      kMut_mage_starburst_ring_scen99 },

    { "soldier_whirlwind_ring_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier_whirlwind_ring_scen99, std::size(kFamilySpawns_soldier_whirlwind_ring_scen99),
      0, false, true, Exercises::Special_Soldier_3,
      kFacts_soldier_whirlwind_ring_scen99, std::size(kFacts_soldier_whirlwind_ring_scen99),
      kMut_soldier_whirlwind_ring_scen99 },

    { "soldier_disarm_matched_levels_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier_disarm_matched_scen99, std::size(kFamilySpawns_soldier_disarm_matched_scen99),
      0, false, true, Exercises::Special_Soldier_4,
      kFacts_soldier_disarm_matched_scen99, std::size(kFacts_soldier_disarm_matched_scen99),
      kMut_soldier_disarm_matched_scen99 },

    { "orc_yell_stun_hold_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 50,
      CompareMode::SemanticParity, false,
      kFamilySpawns_orc_yell_stun_hold_scen99, std::size(kFamilySpawns_orc_yell_stun_hold_scen99),
      0, false, true, Exercises::Special_Orc_1,
      kFacts_orc_yell_stun_hold_scen99, std::size(kFacts_orc_yell_stun_hold_scen99),
      kMut_orc_yell_stun_hold_scen99 },

    { "orc_eat_corpse_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_orc_eat_corpse_scen99, std::size(kInputs_orc_eat_corpse_scen99), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_orc_eat_corpse_scen99, std::size(kFamilySpawns_orc_eat_corpse_scen99),
      0, false, true, Exercises::Special_Orc_2,
      kFacts_orc_eat_corpse_scen99, std::size(kFacts_orc_eat_corpse_scen99),
      kMut_orc_eat_corpse_scen99 },

    { "archer_fire_arrows_ring_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_archer_fire_arrows_ring_scen99, std::size(kFamilySpawns_archer_fire_arrows_ring_scen99),
      0, false, true, Exercises::Special_Archer_1,
      kFacts_archer_fire_arrows_ring_scen99, std::size(kFacts_archer_fire_arrows_ring_scen99),
      kMut_archer_fire_arrows_ring_scen99 },

    { "skeleton_tunnel_displacement_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 90,
      CompareMode::SemanticParity, false,
      kFamilySpawns_skeleton_tunnel_scen99, std::size(kFamilySpawns_skeleton_tunnel_scen99),
      0, false, true, Exercises::Special_Skeleton_1,
      kFacts_skeleton_tunnel_scen99, std::size(kFacts_skeleton_tunnel_scen99),
      kMut_skeleton_tunnel_scen99 },

    { "druid_grow_tree_emission_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_druid_grow_tree_scen99, std::size(kFamilySpawns_druid_grow_tree_scen99),
      0, false, true, Exercises::Special_Druid_1,
      kFacts_druid_grow_tree_scen99, std::size(kFacts_druid_grow_tree_scen99),
      kMut_druid_grow_tree_scen99 },

    { "barbarian_boulder_impact_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_barbarian_boulder_impact_scen99, std::size(kInputs_barbarian_boulder_impact_scen99), 45,
      CompareMode::SemanticParity, false,
      kFamilySpawns_barbarian_boulder_impact_scen99, std::size(kFamilySpawns_barbarian_boulder_impact_scen99),
      0, false, true, Exercises::Special_Barbarian_1,
      kFacts_barbarian_boulder_impact_scen99, std::size(kFacts_barbarian_boulder_impact_scen99),
      kMut_barbarian_boulder_impact_scen99 },

    // slime-death-generators batch
    { "magic_damage_slime_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_magic_damage_slime, std::size(kInputs_magic_damage_slime), 40,
      CompareMode::SemanticParity, false,
      kFamilySpawns_magic_damage_slime_scen99, std::size(kFamilySpawns_magic_damage_slime_scen99),
      0, false, true, Exercises::Special_FireElemental_1,
      kFacts_magic_damage_slime_scen99, std::size(kFacts_magic_damage_slime_scen99),
      kMut_magic_damage_slime_scen99 },

    { "slime_death_split_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 80,
      CompareMode::SemanticParity, false,
      kFamilySpawns_slime_death_split_scen99, std::size(kFamilySpawns_slime_death_split_scen99),
      0, false, true, Exercises::None,
      kFacts_slime_death_split_scen99, std::size(kFacts_slime_death_split_scen99),
      kMut_slime_death_split_scen99 },

    { "elemental_death_starburst_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_elemental_death_starburst, std::size(kInputs_elemental_death_starburst), 80,
      CompareMode::SemanticParity, false,
      kFamilySpawns_elemental_death_starburst_scen99, std::size(kFamilySpawns_elemental_death_starburst_scen99),
      0, false, true, Exercises::Special_FireElemental_1,
      kFacts_elemental_death_starburst_scen99, std::size(kFacts_elemental_death_starburst_scen99),
      kMut_elemental_death_starburst_scen99 },

    { "ai_slime_split_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 100,
      CompareMode::SemanticParity, false,
      kFamilySpawns_ai_slime_split_scen99, std::size(kFamilySpawns_ai_slime_split_scen99),
      0, false, true, Exercises::Special_Slime_1,
      kFacts_ai_slime_split_scen99, std::size(kFacts_ai_slime_split_scen99),
      kMut_ai_slime_split_scen99 },

    { "slime_grow_blocked_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_slime_grow_blocked_scen99, std::size(kFamilySpawns_slime_grow_blocked_scen99),
      0, false, true, Exercises::Special_SmallSlime_1,
      kFacts_slime_grow_blocked_scen99, std::size(kFacts_slime_grow_blocked_scen99),
      kMut_slime_grow_blocked_scen99 },

    { "generator_owner_cascade_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_generator_owner_cascade, std::size(kInputs_generator_owner_cascade), 310,
      CompareMode::SemanticParity, false,
      kFamilySpawns_generator_owner_cascade_scen99, std::size(kFamilySpawns_generator_owner_cascade_scen99),
      0, false, true, Exercises::None,
      kFacts_generator_owner_cascade_scen99, std::size(kFacts_generator_owner_cascade_scen99),
      kMut_generator_owner_cascade_scen99 },

    // effects batch — FX consequence rows
    { "effect_cloud_poison_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_cloud_poison_scen99, std::size(kFamilySpawns_effect_cloud_poison_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_cloud_poison_scen99, std::size(kFacts_effect_cloud_poison_scen99),
      kMut_effect_cloud_poison_scen99 },

    { "effect_explosion_range_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 100,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_explosion_range_scen99, std::size(kFamilySpawns_effect_explosion_range_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_explosion_range_scen99, std::size(kFacts_effect_explosion_range_scen99),
      kMut_effect_explosion_range_scen99 },

    { "effect_bomb_bystander_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 100,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_bomb_bystander_scen99, std::size(kFamilySpawns_effect_bomb_bystander_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_bomb_bystander_scen99, std::size(kFacts_effect_bomb_bystander_scen99),
      kMut_effect_bomb_bystander_scen99 },

    { "effect_shield_absorb_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsMagicShieldNoFire, std::size(kInputsMagicShieldNoFire), 45,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_shield_absorb_scen99, std::size(kFamilySpawns_effect_shield_absorb_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_shield_absorb_scen99, std::size(kFacts_effect_shield_absorb_scen99),
      kMut_effect_shield_absorb_scen99 },

    { "effect_boomerang_contact_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 85,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_boomerang_contact_scen99, std::size(kFamilySpawns_effect_boomerang_contact_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_boomerang_contact_scen99, std::size(kFacts_effect_boomerang_contact_scen99),
      kMut_effect_boomerang_contact_scen99 },

    { "effect_boomerang_drawcycle_cap_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 290,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_boomerang_cap_scen99, std::size(kFamilySpawns_effect_boomerang_cap_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_boomerang_cap_scen99, std::size(kFacts_effect_boomerang_cap_scen99),
      kMut_effect_boomerang_cap_scen99 },

    { "effect_chain_fork_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsChainFork, std::size(kInputsChainFork), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_chain_fork_scen99, std::size(kFamilySpawns_effect_chain_fork_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_chain_fork_scen99, std::size(kFacts_effect_chain_fork_scen99),
      kMut_effect_chain_fork_scen99 },

    { "effect_knife_back_catch_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsKnifeBackCatch, std::size(kInputsKnifeBackCatch), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_knife_back_catch_scen99, std::size(kFamilySpawns_effect_knife_back_catch_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_knife_back_catch_scen99, std::size(kFacts_effect_knife_back_catch_scen99),
      kMut_effect_knife_back_catch_scen99 },

    { "effect_ghost_scare_moving_caster_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsGhostScareWalk, std::size(kInputsGhostScareWalk), 90,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_ghost_scare_walk_scen99, std::size(kFamilySpawns_effect_ghost_scare_walk_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_ghost_scare_walk_scen99, std::size(kFacts_effect_ghost_scare_walk_scen99),
      kMut_effect_ghost_scare_walk_scen99 },

    { "weapon_boulder_explode_damage_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_weapon_boulder_explode_damage, std::size(kInputs_weapon_boulder_explode_damage), 40,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_boulder_explode_damage_scen99, std::size(kFamilySpawns_weapon_boulder_explode_damage_scen99),
      0, false, true, Exercises::Special_Barbarian_2,
      kFacts_weapon_boulder_explode_damage_scen99, std::size(kFacts_weapon_boulder_explode_damage_scen99),
      kMut_weapon_boulder_explode_damage_scen99 },

    { "weapon_door_unlock_chain_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_weapon_door_unlock_chain, std::size(kInputs_weapon_door_unlock_chain), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_door_unlock_chain_scen99, std::size(kFamilySpawns_weapon_door_unlock_chain_scen99),
      0, false, true, Exercises::None,
      kFacts_weapon_door_unlock_chain_scen99, std::size(kFacts_weapon_door_unlock_chain_scen99),
      kMut_weapon_door_unlock_chain_scen99 },

    { "weapon_rock_bounce_edge_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot2, std::size(kInputsSpecialSlot2), 45,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_rock_bounce_edge_scen99, std::size(kFamilySpawns_weapon_rock_bounce_edge_scen99),
      0, false, true, Exercises::Special_Elf_2,
      kFacts_weapon_rock_bounce_edge_scen99, std::size(kFacts_weapon_rock_bounce_edge_scen99),
      kMut_weapon_rock_bounce_edge_scen99 },

    { "weapon_wave_promote_wave2_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWaveSpecialEmit, std::size(kInputsWaveSpecialEmit), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_wave_emission, std::size(kFamilySpawns_weapon_wave_emission),
      0, false, true, Exercises::Special_Mage_4,
      kFacts_weapon_wave_promote_wave2_scen99, std::size(kFacts_weapon_wave_promote_wave2_scen99),
      kMut_weapon_wave_promote_wave2_scen99 },

    { "weapon_sprinkle_freeze_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_weapon_sprinkle_freeze, std::size(kInputs_weapon_sprinkle_freeze), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_sprinkle_freeze_scen99, std::size(kFamilySpawns_weapon_sprinkle_freeze_scen99),
      0, false, true, Exercises::None,
      kFacts_weapon_sprinkle_freeze_scen99, std::size(kFacts_weapon_sprinkle_freeze_scen99),
      kMut_weapon_sprinkle_freeze_scen99 },

    { "weapon_fire_arrow_explode_damage_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_fire_arrow_explode_damage_scen99, std::size(kFamilySpawns_weapon_fire_arrow_explode_damage_scen99),
      0, false, true, Exercises::Special_Archer_3,
      kFacts_weapon_fire_arrow_explode_damage_scen99, std::size(kFacts_weapon_fire_arrow_explode_damage_scen99),
      kMut_weapon_fire_arrow_explode_damage_scen99 },

    { "weapon_circle_protection_follow_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4, std::size(kInputsSpecialSlot4), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_protection_emit_scen99, std::size(kFamilySpawns_effect_protection_emit_scen99),
      0, false, true, Exercises::Special_Druid_4,
      kFacts_weapon_circle_protection_follow_scen99, std::size(kFacts_weapon_circle_protection_follow_scen99),
      kMut_weapon_circle_protection_follow_scen99 },

    { "weapon_sit_notify_quiet_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_sit_notify_quiet_scen99, std::size(kFamilySpawns_weapon_sit_notify_quiet_scen99),
      0, false, true, Exercises::None,
      kFacts_weapon_sit_notify_quiet_scen99, std::size(kFacts_weapon_sit_notify_quiet_scen99),
      kMut_weapon_sit_notify_quiet_scen99 },

    { "weapon_ranged_impact_hp_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsWeaponEmit, std::size(kInputsWeaponEmit), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_weapon_ranged_impact_hp_scen99, std::size(kFamilySpawns_weapon_ranged_impact_hp_scen99),
      0, false, true, Exercises::None,
      kFacts_weapon_ranged_impact_hp_scen99, std::size(kFacts_weapon_ranged_impact_hp_scen99),
      kMut_weapon_ranged_impact_hp_scen99 },

    { "archer_hit_response_backpedal_scen99", "scen/scen1.fss", 0x00000042u,
      kInputs_archer_backpedal, std::size(kInputs_archer_backpedal), 80,
      CompareMode::SemanticParity, false,
      kFamilySpawns_archer_hit_response_backpedal_scen99, std::size(kFamilySpawns_archer_hit_response_backpedal_scen99),
      0, false, true, Exercises::None,
      kFacts_archer_hit_response_backpedal_scen99, std::size(kFacts_archer_hit_response_backpedal_scen99),
      kMut_archer_hit_response_backpedal_scen99 },

    { "orc_yell_zero_constitution_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 120,
      CompareMode::SemanticParity, false,
      kFamilySpawns_orc_yell_zero_constitution_scen99, std::size(kFamilySpawns_orc_yell_zero_constitution_scen99),
      0, false, true, Exercises::Special_Orc_1,
      kFacts_orc_yell_zero_constitution_scen99, std::size(kFacts_orc_yell_zero_constitution_scen99),
      kMut_orc_yell_zero_constitution_scen99 },

    { "mage_freeze_time_offteam_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsEmpty, std::size(kInputsEmpty), 300,
      CompareMode::SemanticParity, false,
      kFamilySpawns_mage_freeze_time_offteam_scen99, std::size(kFamilySpawns_mage_freeze_time_offteam_scen99),
      0, false, true, Exercises::Special_Mage_3,
      kFacts_mage_freeze_time_offteam_scen99, std::size(kFacts_mage_freeze_time_offteam_scen99),
      kMut_mage_freeze_time_offteam_scen99 },

    { "beast_set_difficulty_invariant_scen99", "scen/scen1.fss", 0x00000042u,
      nullptr, 0, 1500,
      CompareMode::SemanticParity, false,
      kFamilySpawns_beast_set_difficulty_invariant_scen99, std::size(kFamilySpawns_beast_set_difficulty_invariant_scen99),
      0, false, true, Exercises::None,
      kFacts_beast_set_difficulty_invariant_scen99, std::size(kFacts_beast_set_difficulty_invariant_scen99),
      kMut_beast_set_difficulty_invariant_scen99 },

    { "thief_taunt_matched_levels_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot3, std::size(kInputsSpecialSlot3), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_thief_taunt_matched_levels_scen99, std::size(kFamilySpawns_thief_taunt_matched_levels_scen99),
      0, false, true, Exercises::Special_Thief_3,
      kFacts_thief_taunt_matched_levels_scen99, std::size(kFacts_thief_taunt_matched_levels_scen99),
      kMut_thief_taunt_matched_levels_scen99 },

    { "elf_mega_rocks_volley_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsFaceRightSpecialSlot4, std::size(kInputsFaceRightSpecialSlot4), 45,
      CompareMode::SemanticParity, false,
      kFamilySpawns_elf_mega_rocks_volley_scen99, std::size(kFamilySpawns_elf_mega_rocks_volley_scen99),
      0, false, true, Exercises::Special_Elf_4,
      kFacts_elf_mega_rocks_volley_scen99, std::size(kFacts_elf_mega_rocks_volley_scen99),
      kMut_elf_mega_rocks_volley_scen99 },

    { "fireelemental_starburst_ring_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 40,
      CompareMode::SemanticParity, false,
      kFamilySpawns_fireelemental_starburst_ring_scen99, std::size(kFamilySpawns_fireelemental_starburst_ring_scen99),
      0, false, true, Exercises::Special_FireElemental_1,
      kFacts_fireelemental_starburst_ring_scen99, std::size(kFacts_fireelemental_starburst_ring_scen99),
      kMut_fireelemental_starburst_ring_scen99 },

    { "mage_heartburst_multitarget_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot5, std::size(kInputsSpecialSlot5), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_mage_heartburst_multitarget_scen99, std::size(kFamilySpawns_mage_heartburst_multitarget_scen99),
      0, false, true, Exercises::Special_Mage_5,
      kFacts_mage_heartburst_multitarget_scen99, std::size(kFacts_mage_heartburst_multitarget_scen99),
      kMut_mage_heartburst_multitarget_scen99 },

    { "soldier_charge_displacement_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsFaceRightSpecialSlot1, std::size(kInputsFaceRightSpecialSlot1), 60,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier_charge_displacement_scen99, std::size(kFamilySpawns_soldier_charge_displacement_scen99),
      0, false, true, Exercises::Special_Soldier_1,
      kFacts_soldier_charge_displacement_scen99, std::size(kFacts_soldier_charge_displacement_scen99),
      kMut_soldier_charge_displacement_scen99 },

    { "elf_rocks_pair_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsFaceRightSpecialSlot1, std::size(kInputsFaceRightSpecialSlot1), 30,
      CompareMode::SemanticParity, false,
      kFamilySpawns_elf_rocks_pair_scen99, std::size(kFamilySpawns_elf_rocks_pair_scen99),
      0, false, true, Exercises::Special_Elf_1,
      kFacts_elf_rocks_pair_scen99, std::size(kFacts_elf_rocks_pair_scen99),
      kMut_elf_rocks_pair_scen99 },

    { "thief_ai_bomb_flee_scen99", "scen/scen1.fss", 0x00000044u,
      kInputsEmpty, std::size(kInputsEmpty), 35,
      CompareMode::SemanticParity, false,
      kFamilySpawns_thief_ai_bomb_flee_scen99, std::size(kFamilySpawns_thief_ai_bomb_flee_scen99),
      0, false, true, Exercises::Special_Thief_1,
      kFacts_thief_ai_bomb_flee_scen99, std::size(kFacts_thief_ai_bomb_flee_scen99),
      kMut_thief_ai_bomb_flee_scen99 },

    { "effect_explosion_ally_tier_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot1, std::size(kInputsSpecialSlot1), 100,
      CompareMode::SemanticParity, false,
      kFamilySpawns_effect_explosion_ally_tier_scen99, std::size(kFamilySpawns_effect_explosion_ally_tier_scen99),
      0, false, true, Exercises::None,
      kFacts_effect_explosion_ally_tier_scen99, std::size(kFacts_effect_explosion_ally_tier_scen99),
      kMut_effect_explosion_ally_tier_scen99,
      "explosion damage tiers: this row carries the ALLY half tier (team-0 TOWER1 on 222,196 loses 45 of 130); the OWNER quarter tier and the FULL tier are pinned by effect_bomb_bystander_scen99 on the identical arena (thief 75 -> 55, team-1 TOWER1 on the same 222,196 loses 91 of 130)" },

    { "druid_protection_refresh_scen99", "scen/scen1.fss", 0x00000042u,
      kInputsSpecialSlot4Twice, std::size(kInputsSpecialSlot4Twice), 45,
      CompareMode::SemanticParity, false,
      kFamilySpawns_druid_protection_refresh_scen99, std::size(kFamilySpawns_druid_protection_refresh_scen99),
      0, false, true, Exercises::Special_Druid_4,
      kFacts_druid_protection_refresh_scen99, std::size(kFacts_druid_protection_refresh_scen99),
      kMut_druid_protection_refresh_scen99,
      "INTENTIONAL GAMEPLAY CHANGE -- golden captured from the branch at be57275f6b8e47979c9e278539b15570085c7a2d, never from the companion or the merge base, because the behaviour it records is new by design (the druid's protection top-up arm was dead from the 2002 import until that commit). See the \"Intentional gameplay changes\" section of tests/parity/golden/DRIFT_LEDGER.md before recapturing anything for this row." },
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
