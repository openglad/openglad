// Parity coverage targets — the static catalog of every gameplay entity
// the parity harness must exercise across the cumulative scenario set.
//
// Single source of truth used by:
//   * tests/parity/test_parity_coverage_gate.cpp — runtime subset check
//   * scripts/parity/check_coverage_manifest.py  — pre-build static check
//   * .plan/parity-coverage-manifest.md          — long-form contract
//
// Adding a `FAMILY_*` to `include/openglad/core/constants.h` or an
// `EventKind` to `include/openglad/gameplay/event.h` without updating
// this header (and the manifest) trips the static check; adding an
// entry here without a covering scenario trips the runtime gate.

#pragma once

#include <openglad/core/constants.h>

#include <cstdint>
#include <string_view>
#include <utility>

namespace og::parity {

// All 21 living families (FAMILY_SOLDIER..FAMILY_TOWER1).
inline constexpr std::int32_t kRequiredWalkerFamilies[] = {
    FAMILY_SOLDIER,
    FAMILY_ELF,
    FAMILY_ARCHER,
    FAMILY_MAGE,
    FAMILY_SKELETON,
    FAMILY_CLERIC,
    FAMILY_FIREELEMENTAL,
    FAMILY_FAERIE,
    FAMILY_SLIME,
    FAMILY_SMALL_SLIME,
    FAMILY_MEDIUM_SLIME,
    FAMILY_THIEF,
    FAMILY_GHOST,
    FAMILY_DRUID,
    FAMILY_ORC,
    FAMILY_BIG_ORC,
    FAMILY_BARBARIAN,
    FAMILY_ARCHMAGE,
    FAMILY_GOLEM,
    FAMILY_GIANT_SKELETON,
    FAMILY_TOWER1,
};

// All 13 effect (FX) families.
inline constexpr std::int32_t kRequiredEffectFamilies[] = {
    FAMILY_EXPAND,
    FAMILY_GHOST_SCARE,
    FAMILY_BOMB,
    FAMILY_EXPLOSION,
    FAMILY_FLASH,
    FAMILY_MAGIC_SHIELD,
    FAMILY_KNIFE_BACK,
    FAMILY_BOOMERANG,
    FAMILY_CLOUD,
    FAMILY_MARKER,
    FAMILY_CHAIN,
    FAMILY_DOOR_OPEN,
    FAMILY_HIT,
};

// All 20 weapon families (FAMILY_KNIFE..FAMILY_BOULDER).
inline constexpr std::int32_t kRequiredWeaponFamilies[] = {
    FAMILY_KNIFE,
    FAMILY_ROCK,
    FAMILY_ARROW,
    FAMILY_FIREBALL,
    FAMILY_TREE,
    FAMILY_METEOR,
    FAMILY_SPRINKLE,
    FAMILY_BONE,
    FAMILY_BLOOD,
    FAMILY_BLOB,
    FAMILY_FIRE_ARROW,
    FAMILY_LIGHTNING,
    FAMILY_GLOW,
    FAMILY_WAVE,
    FAMILY_WAVE2,
    FAMILY_WAVE3,
    FAMILY_CIRCLE_PROTECTION,
    FAMILY_HAMMER,
    FAMILY_DOOR,
    FAMILY_BOULDER,
};

// All 13 treasure families (FAMILY_STAIN..FAMILY_SPEED_POTION).
inline constexpr std::int32_t kRequiredTreasureFamilies[] = {
    FAMILY_STAIN,
    FAMILY_DRUMSTICK,
    FAMILY_GOLD_BAR,
    FAMILY_SILVER_BAR,
    FAMILY_MAGIC_POTION,
    FAMILY_INVIS_POTION,
    FAMILY_INVULNERABLE_POTION,
    FAMILY_FLIGHT_POTION,
    FAMILY_EXIT,
    FAMILY_TELEPORTER,
    FAMILY_LIFE_GEM,
    FAMILY_KEY,
    FAMILY_SPEED_POTION,
};

// All 4 generator families (FAMILY_TENT..FAMILY_TREEHOUSE).
inline constexpr std::int32_t kRequiredGeneratorFamilies[] = {
    FAMILY_TENT,
    FAMILY_TOWER,
    FAMILY_BONES,
    FAMILY_TREEHOUSE,
};

// (family, special_index) pairs for every family whose descriptor binds
// a non-null `do_special` AND whose `special_names[idx]` is not "NONE".
// 42 pairs total; ordering matches the bit positions in
// `enum class Exercises` in scenario_table.h. The structure here is
// `pair<family_id, special_index>` per the Phase 03 contract.
inline constexpr std::pair<std::int32_t, std::uint8_t> kRequiredSpecials[] = {
    // FAMILY_SOLDIER (0): CHARGE, BOOMERANG, WHIRLWIND, DISARM
    {FAMILY_SOLDIER, 1}, {FAMILY_SOLDIER, 2}, {FAMILY_SOLDIER, 3}, {FAMILY_SOLDIER, 4},
    // FAMILY_ELF (1): ROCKS, BOUNCING ROCKS, LOTS OF ROCKS, MEGA ROCKS
    {FAMILY_ELF, 1}, {FAMILY_ELF, 2}, {FAMILY_ELF, 3}, {FAMILY_ELF, 4},
    // FAMILY_ARCHER (2): FIRE ARROWS, BARRAGE, EXPLODING BOLT
    {FAMILY_ARCHER, 1}, {FAMILY_ARCHER, 2}, {FAMILY_ARCHER, 3},
    // FAMILY_MAGE (3): TELEPORT, WARP SPACE, FREEZE TIME, ENERGY WAVE, HEARTBURST
    {FAMILY_MAGE, 1}, {FAMILY_MAGE, 2}, {FAMILY_MAGE, 3}, {FAMILY_MAGE, 4}, {FAMILY_MAGE, 5},
    // FAMILY_SKELETON (4): TUNNEL
    {FAMILY_SKELETON, 1},
    // FAMILY_CLERIC (5): HEAL, RAISE UNDEAD, RAISE GHOST, RESURRECT
    {FAMILY_CLERIC, 1}, {FAMILY_CLERIC, 2}, {FAMILY_CLERIC, 3}, {FAMILY_CLERIC, 4},
    // FAMILY_FIREELEMENTAL (6): STARBURST
    {FAMILY_FIREELEMENTAL, 1},
    // FAMILY_SLIME (8): SPLIT
    {FAMILY_SLIME, 1},
    // FAMILY_SMALL_SLIME (9): GROW
    {FAMILY_SMALL_SLIME, 1},
    // FAMILY_MEDIUM_SLIME (10): GROW
    {FAMILY_MEDIUM_SLIME, 1},
    // FAMILY_THIEF (11): DROP BOMB, CLOAK, TAUNT ENEMY, POISON CLOUD
    {FAMILY_THIEF, 1}, {FAMILY_THIEF, 2}, {FAMILY_THIEF, 3}, {FAMILY_THIEF, 4},
    // FAMILY_GHOST (12): SCARE
    {FAMILY_GHOST, 1},
    // FAMILY_DRUID (13): GROW TREE, SUMMON FAERIE, REVEAL, PROTECTION
    {FAMILY_DRUID, 1}, {FAMILY_DRUID, 2}, {FAMILY_DRUID, 3}, {FAMILY_DRUID, 4},
    // FAMILY_ORC (14): HOWL, EAT CORPSE
    {FAMILY_ORC, 1}, {FAMILY_ORC, 2},
    // FAMILY_BARBARIAN (16): HURL BOULDER, EXPLODING BOULDER
    {FAMILY_BARBARIAN, 1}, {FAMILY_BARBARIAN, 2},
    // FAMILY_ARCHMAGE (17): TELEPORT, HEARTBURST, SUMMON IMAGE, MIND CONTROL
    {FAMILY_ARCHMAGE, 1}, {FAMILY_ARCHMAGE, 2}, {FAMILY_ARCHMAGE, 3}, {FAMILY_ARCHMAGE, 4},
};

// The nine non-`None` `EventKind` symbols, matching the canonical names
// emitted by tests/parity/state_dump.cpp::event_kind_symbol.
inline constexpr std::string_view kRequiredEventKinds[] = {
    "play_sound",
    "notification",
    "set_palette",
    "request_redraw",
    "end_game",
    "set_end",
    "request_exit_confirmation",
    "withdraw_to_level",
    "score_change",
};

} // namespace og::parity
