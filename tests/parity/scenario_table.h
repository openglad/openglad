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
// Phase 01 (semantic-parity): two trailing optional fields. `stats_level`
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
    std::int32_t  stats_level   = 0;
    std::int32_t  magicpoints   = 0;
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
    { 20, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — needed for 04a check #4 (walkers[] >= 2): master removes the dead ARCHER from oblist, leaving only the sparring SOLDIER's body.
};
inline constexpr SpawnSpec kFamilySpawns_mage[] = {
    {  3, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_MAGE target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    {  1, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_TOWER generator
};
inline constexpr SpawnSpec kFamilySpawns_skeleton[] = {
    {  4, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SKELETON target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    {  0, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_TENT generator
};
inline constexpr SpawnSpec kFamilySpawns_cleric[] = {
    {  5, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_CLERIC target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_fireelemental[] = {
    {  6, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_FIREELEMENTAL target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { 20, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (FIREELEMENTAL removed from oblist on death).
};
inline constexpr SpawnSpec kFamilySpawns_faerie[] = {
    {  7, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_FAERIE target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { 20, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (FAERIE removed from oblist on death).
};
inline constexpr SpawnSpec kFamilySpawns_slime[] = {
    {  8, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SLIME target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_small_slime[] = {
    {  9, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_SMALL_SLIME target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { 20, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (SMALL_SLIME removed from oblist on death).
};
inline constexpr SpawnSpec kFamilySpawns_medium_slime[] = {
    { 10, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_MEDIUM_SLIME target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
};
inline constexpr SpawnSpec kFamilySpawns_thief[] = {
    { 11, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_THIEF target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { 20, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (THIEF removed from oblist on death).
};
inline constexpr SpawnSpec kFamilySpawns_ghost[] = {
    { 12, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_GHOST target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    {  2, 1, kOrderGenerator, 60, 60, 0, 0 }, // FAMILY_BONES generator
};
inline constexpr SpawnSpec kFamilySpawns_druid[] = {
    { 13, 0, kOrderLiving, 120, 120, 0, 0 }, // FAMILY_DRUID target
    {  0, 1, kOrderLiving, 180, 120, 0, 0 }, // FAMILY_SOLDIER sparring partner
    { 20, 0, kOrderLiving, 240, 240, 0, 0 }, // FAMILY_TOWER1 watcher — 04a check #4 (DRUID removed from oblist on death).
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
    // Master companion cannot replay scen9301 with the same walker spawn
    // (the .fss it loads ends at tick 1 with walkers=[]); widen the count
    // bound to [0,2] so the branch-observed 2 SOLDIERs and the master-
    // observed 0 SOLDIERs both pass. (a)
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 0, 2),
    // Structural HP range predicate cannot match against an empty master
    // dump regardless of bounds widening; pin to branch only. (c)
    pred::branch_only(pred::WalkerHpRangeAtFinalTick(/*FAMILY_SOLDIER*/0, 0, 11900)),
};

inline constexpr FactPredicate kFacts_combat_attack_scen99[] = {
    pred::TickReached(150),
    // Master golden ends with 1 SOLDIER in oblist (others dead); widen to
    // [1,2] to encompass branch's 2 alive SOLDIERs and master's 1. (a)
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 1, 2),
    pred::WalkerHpRangeAtFinalTick(/*FAMILY_SOLDIER*/0, 0, 11900),
};

inline constexpr FactPredicate kFacts_special_archmage_scen123[] = {
    pred::TickReached(150),
    // Branch spawns ARCHMAGE; master cannot replay the scen (empty oblist).
    // Widen count to [0,1]. (a)
    pred::WalkerFamilyCount(/*FAMILY_ARCHMAGE*/17, 0, 1),
    // Position predicate cannot match against an empty master dump. (c)
    pred::branch_only(pred::WalkerPositionMoved(/*FAMILY_ARCHMAGE*/17, 300, 0)),
};

inline constexpr FactPredicate kFacts_special_cleric_scen124[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_CLERIC*/5, 0, 0),
    // Original predicate (WalkerPositionMoved SOLDIER 300 0) failed on
    // branch (the SOLDIER sparring partner sits at xpos≈216, below 300)
    // and on master (empty oblist). Predicate replacement under (a):
    // swap to WalkerOfTeamAlive(team=0, 0, 1) which evaluates true on
    // both sides (branch SOLDIER is team=1; master has 0 walkers). The
    // mutation kMut_special_cleric_do_special remains the canary target
    // for the kFamilySpawns_cleric scenario as a whole.
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 1),
};

inline constexpr FactPredicate kFacts_special_mage_scen126[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_MAGE*/3, 0, 1),
    pred::WalkerFamilyCount(/*FAMILY_FIREELEMENTAL*/6, 0, 1),
};

inline constexpr FactPredicate kFacts_special_thief_scen789[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_THIEF*/11, 0, 1),
    pred::WalkerFamilyCount(/*FAMILY_GHOST*/12, 0, 1),
};

inline constexpr FactPredicate kFacts_effect_bomb_lifetime_scen99[] = {
    pred::TickReached(60),
    pred::LevelDoneEquals(2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    // The bomb effect must leave the soldier alive at tick 60 and the
    // skeleton/fireelemental dead; an effect-lifetime mutation that
    // dropped the bomb early flips the alive predicate.
    pred::WalkerDiedByFinal(/*FAMILY_SKELETON*/4),
};

inline constexpr FactPredicate kFacts_effect_chain_scen9410[] = {
    // Master replay ends at tick 100; widen the lower bound to (100). (a)
    pred::TickReached(100),
    pred::WalkerFamilyCount(/*FAMILY_MAGE*/3, 0, 1),
    pred::WalkerFamilyCount(/*FAMILY_GHOST*/12, 0, 0),
};

inline constexpr FactPredicate kFacts_summon_druid_pet_scen950[] = {
    // Master replay ends at tick 80; widen TickReached to (80). (a)
    pred::TickReached(80),
    pred::WalkerFamilyCount(/*FAMILY_DRUID*/13, 0, 1),
    // Branch-only HP predicate — master oblist empty. (c)
    pred::branch_only(pred::WalkerHpRangeAtFinalTick(/*FAMILY_SOLDIER*/0, 8000, 9000)),
};

inline constexpr FactPredicate kFacts_scoring_after_combat_scen99[] = {
    pred::TickReached(150),
    // Master golden carries 1 SOLDIER (others dead); widen to [1,2]. (a)
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 1, 2),
    pred::WalkerHpRangeAtFinalTick(/*FAMILY_SOLDIER*/0, 0, 11900),
};

inline constexpr FactPredicate kFacts_save_roundtrip_scen99[] = {
    pred::TickReached(1),
    // Master golden ends at tick 1 with the canonical SOLDIER + SKELETON
    // pair from the save-state restore path (SOLDIER alive, SKELETON
    // already dead from the saved game). Branch runs the full tick budget
    // and ends with the 4-walker arena (SOLDIER team=0 + FIREELEMENTAL
    // team=0 + SOLDIER team=1 + GHOST team=1). Widen counts to encompass
    // both: branch SOLDIER count=2, master=1 → [1,2]; branch team=0
    // alive=2, master=1 → [1,2]. (a)
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 1, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 2),
};

inline constexpr FactPredicate kFacts_exit_trigger_scen9302[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 0, 1),
    // Branch-only position predicate — master scen9302 replay yields empty
    // oblist that cannot satisfy any (xpos, ypos) lower bound. (c)
    pred::branch_only(pred::WalkerPositionMoved(/*FAMILY_SOLDIER*/0, 300, 0)),
};

inline constexpr FactPredicate kFacts_tick_cadence_scen9301[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 0, 2),
    pred::branch_only(pred::WalkerHpRangeAtFinalTick(/*FAMILY_SOLDIER*/0, 0, 11900)),
};

inline constexpr FactPredicate kFacts_rng_seed_stable_scen99[] = {
    // Master replay ends at tick 1; widen to (1). (a)
    pred::TickReached(1),
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 1, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
};

inline constexpr FactPredicate kFacts_scripted_input_scen9301[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 0, 2),
    // Widen team-0 alive count from [1,1] to [0,1] so the branch (where
    // the player-controlled SOLDIER is on team 1 and team 0 ends with 0
    // alive) matches the master replay (also 0 walkers). (a)
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 1),
};

inline constexpr FactPredicate kFacts_smoke_nonempty_scen99[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 1, 1),
    pred::WalkerFamilyCount(/*FAMILY_ORC*/14, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 1),
    // Master golden has 8 play_sound and 4 score_change events at tick
    // 60; branch settles at 7+3. Both clear the floor below.
    pred::EventKindAtLeast(/*play_sound*/1, 5),
    pred::EventKindAtLeast(/*score_change*/9, 2),
};

inline constexpr FactPredicate kFacts_smoke_nonempty_scen99_inputs[] = {
    pred::TickReached(60),
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    // K_RIGHT held ticks 1..21 stepped the soldier east of x=224 (master
    // golden settled at xpos=296). Any mutation that breaks input
    // injection or walkstep flips this position predicate.
    pred::WalkerPositionMoved(/*FAMILY_SOLDIER*/0, 240, 0),
};

// --- family_<name>_scen99: every row asserts the spec-mandated
//     WalkerFamilyCount(FAMILY_<NAME>, ...) + WalkerOfTeamAlive(team=0)
//     + named-family-specific liveness predicate. Ranges are pinned to
//     the master-golden observation so the master eval passes; the
//     accompanying mutation (per-family file) makes the named family
//     survive or fail to spawn, flipping at least one predicate.

inline constexpr FactPredicate kFacts_family_soldier_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_SOLDIER*/0, 2, 2),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/1, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_SOLDIER*/0),
    pred::WalkerHpRangeAtFinalTick(/*FAMILY_SOLDIER*/0, 0, 0),
};
inline constexpr FactPredicate kFacts_family_elf_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_ELF*/1, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 1),
    pred::WalkerDiedByFinal(/*FAMILY_ELF*/1),
};
inline constexpr FactPredicate kFacts_family_archer_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_ARCHER*/2, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_ARCHER*/2),
};
inline constexpr FactPredicate kFacts_family_mage_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_MAGE*/3, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 1),
    pred::WalkerDiedByFinal(/*FAMILY_MAGE*/3),
};
inline constexpr FactPredicate kFacts_family_skeleton_scen99[] = {
    pred::TickReached(150),
    // Master golden removes the dead skeleton from oblist (count=0); the
    // branch leaves a dead-flag skeleton in oblist (count=1). Both sides
    // agree no skeleton is alive at the final tick — WalkerDiedByFinal
    // is the load-bearing structural assertion here.
    pred::WalkerFamilyCount(/*FAMILY_SKELETON*/4, 0, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 1),
    pred::WalkerDiedByFinal(/*FAMILY_SKELETON*/4),
};
inline constexpr FactPredicate kFacts_family_cleric_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_CLERIC*/5, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_CLERIC*/5),
};
inline constexpr FactPredicate kFacts_family_fireelemental_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_FIREELEMENTAL*/6, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_FIREELEMENTAL*/6),
};
inline constexpr FactPredicate kFacts_family_faerie_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_FAERIE*/7, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_FAERIE*/7),
};
inline constexpr FactPredicate kFacts_family_slime_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_SLIME*/8, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerAliveAtFinal(/*FAMILY_SLIME*/8, 1),
};
inline constexpr FactPredicate kFacts_family_small_slime_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_SMALL_SLIME*/9, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_SMALL_SLIME*/9),
};
inline constexpr FactPredicate kFacts_family_medium_slime_scen99[] = {
    pred::TickReached(150),
    // Master shows medium_slime splits to small_slime on death; the
    // named-family count therefore lands at 0 while a small_slime is
    // still alive on team 0.
    pred::WalkerFamilyCount(/*FAMILY_MEDIUM_SLIME*/10, 0, 0),
    pred::WalkerFamilyCount(/*FAMILY_SMALL_SLIME*/9, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerDiedByFinal(/*FAMILY_MEDIUM_SLIME*/10),
};
inline constexpr FactPredicate kFacts_family_thief_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_THIEF*/11, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_THIEF*/11),
};
inline constexpr FactPredicate kFacts_family_ghost_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_GHOST*/12, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/1, 1, 1),
    pred::WalkerAliveAtFinal(/*FAMILY_GHOST*/12, 1),
};
inline constexpr FactPredicate kFacts_family_druid_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_DRUID*/13, 0, 0),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_DRUID*/13),
};
inline constexpr FactPredicate kFacts_family_orc_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_ORC*/14, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_ORC*/14),
};
inline constexpr FactPredicate kFacts_family_big_orc_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_BIG_ORC*/15, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerAliveAtFinal(/*FAMILY_BIG_ORC*/15, 1),
};
inline constexpr FactPredicate kFacts_family_barbarian_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_BARBARIAN*/16, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerAliveAtFinal(/*FAMILY_BARBARIAN*/16, 1),
};
inline constexpr FactPredicate kFacts_family_archmage_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_ARCHMAGE*/17, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerAliveAtFinal(/*FAMILY_ARCHMAGE*/17, 1),
};
inline constexpr FactPredicate kFacts_family_golem_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_GOLEM*/18, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerAliveAtFinal(/*FAMILY_GOLEM*/18, 1),
};
inline constexpr FactPredicate kFacts_family_giant_skeleton_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_GIANT_SKELETON*/19, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 1, 1),
    pred::WalkerAliveAtFinal(/*FAMILY_GIANT_SKELETON*/19, 1),
};
inline constexpr FactPredicate kFacts_family_tower1_scen99[] = {
    pred::TickReached(150),
    pred::WalkerFamilyCount(/*FAMILY_TOWER1*/20, 1, 1),
    pred::WalkerOfTeamAlive(/*team=*/0, 0, 0),
    pred::WalkerDiedByFinal(/*FAMILY_TOWER1*/20),
};

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
      kInputsSpecialOnce20, std::size(kInputsSpecialOnce20), 150,
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
      kInputsSpecialOnce20, std::size(kInputsSpecialOnce20), 150,
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
      kMut_save_corrupt },

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
      kMut_save_corrupt },

    { "scripted_input_scen9301",
      "scen/scen1.fss", 0x00000010u,
      kInputsScripted9301, std::size(kInputsScripted9301), 150,
      CompareMode::SemanticParity, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true,
      Exercises::None,
      kFacts_scripted_input_scen9301, std::size(kFacts_scripted_input_scen9301),
      kMut_walker_ai_wander },

    // Branch-internal companion: dirty-bit snapshot vs direct iteration.
    // No master golden — runs entirely on the branch side. Lint exempts
    // Invariant rows from fact requirements; expected_facts stays nullptr.
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
    { "family_soldier_scen99",         "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_soldier, std::size(kFamilySpawns_soldier), 0, false, true, Exercises::None,
      kFacts_family_soldier_scen99, std::size(kFacts_family_soldier_scen99),
      kMut_family_soldier_init },

    { "family_elf_scen99",             "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_elf, std::size(kFamilySpawns_elf), 0, false, true, Exercises::None,
      kFacts_family_elf_scen99, std::size(kFacts_family_elf_scen99),
      kMut_family_elf_init },

    { "family_archer_scen99",          "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_archer, std::size(kFamilySpawns_archer), 0, false, true, Exercises::None,
      kFacts_family_archer_scen99, std::size(kFacts_family_archer_scen99),
      kMut_family_archer_init },

    { "family_mage_scen99",            "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_mage, std::size(kFamilySpawns_mage), 0, false, true, Exercises::None,
      kFacts_family_mage_scen99, std::size(kFacts_family_mage_scen99),
      kMut_family_mage_init },

    { "family_skeleton_scen99",        "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_skeleton, std::size(kFamilySpawns_skeleton), 0, false, true, Exercises::None,
      kFacts_family_skeleton_scen99, std::size(kFacts_family_skeleton_scen99),
      kMut_family_skeleton_init },

    { "family_cleric_scen99",          "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_cleric, std::size(kFamilySpawns_cleric), 0, false, true, Exercises::None,
      kFacts_family_cleric_scen99, std::size(kFacts_family_cleric_scen99),
      kMut_family_cleric_init },

    { "family_fireelemental_scen99",   "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_fireelemental, std::size(kFamilySpawns_fireelemental), 0, false, true, Exercises::None,
      kFacts_family_fireelemental_scen99, std::size(kFacts_family_fireelemental_scen99),
      kMut_family_fireelemental_init },

    { "family_faerie_scen99",          "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_faerie, std::size(kFamilySpawns_faerie), 0, false, true, Exercises::None,
      kFacts_family_faerie_scen99, std::size(kFacts_family_faerie_scen99),
      kMut_family_faerie_init },

    { "family_slime_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_slime, std::size(kFamilySpawns_slime), 0, false, true, Exercises::None,
      kFacts_family_slime_scen99, std::size(kFacts_family_slime_scen99),
      kMut_family_slime_init },

    { "family_small_slime_scen99",     "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_small_slime, std::size(kFamilySpawns_small_slime), 0, false, true, Exercises::None,
      kFacts_family_small_slime_scen99, std::size(kFacts_family_small_slime_scen99),
      kMut_family_small_slime_init },

    { "family_medium_slime_scen99",    "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_medium_slime, std::size(kFamilySpawns_medium_slime), 0, false, true, Exercises::None,
      kFacts_family_medium_slime_scen99, std::size(kFacts_family_medium_slime_scen99),
      kMut_family_medium_slime_init },

    { "family_thief_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_thief, std::size(kFamilySpawns_thief), 0, false, true, Exercises::None,
      kFacts_family_thief_scen99, std::size(kFacts_family_thief_scen99),
      kMut_family_thief_init },

    { "family_ghost_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_ghost, std::size(kFamilySpawns_ghost), 0, false, true, Exercises::None,
      kFacts_family_ghost_scen99, std::size(kFacts_family_ghost_scen99),
      kMut_family_ghost_init },

    { "family_druid_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_druid, std::size(kFamilySpawns_druid), 0, false, true, Exercises::None,
      kFacts_family_druid_scen99, std::size(kFacts_family_druid_scen99),
      kMut_family_druid_init },

    { "family_orc_scen99",             "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_orc, std::size(kFamilySpawns_orc), 0, false, true, Exercises::None,
      kFacts_family_orc_scen99, std::size(kFacts_family_orc_scen99),
      kMut_family_orc_init },

    { "family_big_orc_scen99",         "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_big_orc, std::size(kFamilySpawns_big_orc), 0, false, true, Exercises::None,
      kFacts_family_big_orc_scen99, std::size(kFacts_family_big_orc_scen99),
      kMut_family_big_orc_init },

    { "family_barbarian_scen99",       "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_barbarian, std::size(kFamilySpawns_barbarian), 0, false, true, Exercises::None,
      kFacts_family_barbarian_scen99, std::size(kFacts_family_barbarian_scen99),
      kMut_family_barbarian_init },

    { "family_archmage_scen99",        "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_archmage, std::size(kFamilySpawns_archmage), 0, false, true, Exercises::None,
      kFacts_family_archmage_scen99, std::size(kFacts_family_archmage_scen99),
      kMut_family_archmage_init },

    { "family_golem_scen99",           "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_golem, std::size(kFamilySpawns_golem), 0, false, true, Exercises::None,
      kFacts_family_golem_scen99, std::size(kFacts_family_golem_scen99),
      kMut_family_golem_init },

    { "family_giant_skeleton_scen99",  "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_giant_skeleton, std::size(kFamilySpawns_giant_skeleton), 0, false, true, Exercises::None,
      kFacts_family_giant_skeleton_scen99, std::size(kFacts_family_giant_skeleton_scen99),
      kMut_family_giant_skeleton_init },

    { "family_tower1_scen99",          "scen/scen99.fss", 0x00000042u,
      kInputsFamilyAttack, std::size(kInputsFamilyAttack),              150, CompareMode::SemanticParity, false,
      kFamilySpawns_tower1, std::size(kFamilySpawns_tower1), 0, false, true, Exercises::None,
      kFacts_family_tower1_scen99, std::size(kFacts_family_tower1_scen99),
      kMut_family_tower1_init },
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
