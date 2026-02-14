# Analysis: Extracting Character Types into Dynamic Modules

**Date:** 2025-02-14
**Branch:** cpp-modernization-plan

## Goal

Evaluate extracting each character family (soldier, elf, mage, etc.) into its own
dynamically loadable module (e.g. `.so` / `.dll`).

## Current Type System

### Two Axes: Order + Family

Defined in `include/openglad/legacy/base.h`:

- **Order** (`enum class`): `Living`, `Weapon`, `Treasure`, `Generator`, `FX`, `Special`, `Button1`
- **Family** (plain `int` constants): 21 living families (`FAMILY_SOLDIER=0` through `FAMILY_TOWER1=20`), plus separate overlapping int ranges for weapon families, treasure families, effect families, etc.

The `family` field is a `char` on the `walker` base class. There is **no polymorphism at the family level** — all character types are the same C++ class (`living`). All differentiation is done via switch/case on the `family` field and data tables indexed by family.

### What's Already Data-Driven (easy to externalize)

| Data | Location |
|------|----------|
| Base stats (STR/DEX/CON/INT/ARMOR/LVL) | `statlist[NUM_FAMILIES]` in `src/entities/guy.cpp` |
| Hiring costs | `costlist[NUM_FAMILIES]` in `src/entities/guy.cpp` |
| Derived combat stats (HP/MP/ATK/DEF/SPD) | `derived_bonuses[NUM_FAMILIES]` in `src/entities/guy.cpp` |
| Stat upgrade costs | `statcosts[NUM_FAMILIES]` in `src/entities/guy.cpp` |
| Sprite filenames | `gloader` constructor in `src/data/gloader.cpp` |
| Animation rigs | Pointer arrays in `src/data/gloader.cpp` |
| Default weapon, special costs, bit flags | `set_walker()` switch in `src/data/gloader.cpp` |
| Special ability display names | `special_name[][]` in `src/runtime/screen.cpp` |
| UI display names | `get_family_string()` in `src/ui/picker.cpp` |

## AI Impact

The core AI loop (`walker::act()`, pathfinding, target selection) is **type-agnostic**. But three hardcoded switch points would need to cross the module boundary:

1. **`living::check_special()`** (`src/entities/living.cpp:537-679`) — Per-type AI deciding *when* to use specials. Soldier checks close-range, archer checks medium range, mage checks if far from enemies, cleric checks if friends need healing.

2. **`statistics::hit_response()`** (`src/core/stats.cpp:472-612`) — Per-type reaction to being hit. Mage/archmage teleport away, archer stays at range, default yells for help.

3. **`living::set_difficulty()`** (`src/entities/living.cpp:682-796`) — Per-type difficulty scaling multipliers.

These would each need to become virtual dispatch or callback hooks.

## The Big One: `walker::special()`

`src/entities/walker_specials.cpp` is a **~1,600-line switch statement** implementing every class's unique special abilities:

- **Soldier:** charge, boomerang, whirlwind, disarm
- **Elf:** rock barrages (bouncing, lots, mega)
- **Archer:** fire arrows, barrage, exploding bolt
- **Mage:** teleport, warp space, freeze time, energy wave, heartburst
- **Archmage:** teleport, heartburst, chain lightning, summon image/elemental, mind control
- **Cleric:** heal, mystic mace, raise skeleton, raise ghost, resurrect
- **Druid:** plant tree, summon faerie, reveal, circle of protection
- **Thief:** bomb, cloak, taunt/charm, poison cloud
- **Ghost:** scare
- **Fire Elemental:** starburst
- **Orc:** howl, eat corpse
- **Slime:** grow/split
- **Skeleton:** tunnel
- **Barbarian:** hurl, exploding boulder

Already isolated in its own file, each case is self-contained. This is the primary candidate for modularization.

## Scattered Type-Specific Logic

Family-specific branching was found in **~18 source files**:

| File | What's there |
|------|-------------|
| `walker.cpp` | Soldier ranged-weapon limit, archmage bonus damage, mage/skeleton teleport anim, fire elemental/slime death behavior, undead bloodspot exceptions |
| `living.cpp` | Fire elemental summoned drain, archmage auto-view-all, `check_special()`, `set_difficulty()` |
| `walker_combat.cpp` | Skeleton/ghost = undead for XP, faerie freeze on hit |
| `weap.cpp` | Boulder explosion, circle-of-protection orbiting, door behavior |
| `effect.cpp` | Per-effect-family behavior (explosions, ghost scare, chain lightning, bombs) |
| `treasure.cpp` | Per-treasure-family `eat_me()` |
| `guy.cpp` | `upgrade_to_level()` per-family stat growth |
| `gloader.cpp` | `set_walker()` initialization, sprite/animation assignment |
| `stats.cpp` | `hit_response()` AI |
| `screen.cpp` | `special_name[][]` display strings |
| `picker.cpp`, `picker_team_build.cpp` | Family display names, team builder UI |
| `radar.cpp` | Per-family radar dot colors |
| `input.cpp`, `score_panel.cpp`, `view.cpp`, `results_screen.cpp` | Special ability name display |
| `level_editor.cpp` | Family display |

## Other Issues

1. **No family-level polymorphism.** All types are `living`. Need either a `FamilyDescriptor` struct with callbacks or virtual subclasses per type — both are significant changes.

2. **Family is a `char` used as array index.** Fixed-size `NUM_FAMILIES=21` arrays everywhere. A plugin system needs a registry for dynamic IDs and indirection on every table access.

3. **Weapon/effect families are entangled with living families.** Each living type has a default weapon family, and specials spawn specific weapon+effect families. A "soldier module" must also define knife behavior and whirlwind effects. The module boundary gets blurry.

4. **Save format coupling.** The family `char` is serialized. Dynamic IDs would break save compatibility unless a name-based mapping layer is added.

## Recommended Approach

A **`FamilyDescriptor` table** approach rather than full `dlopen`-based dynamic loading:

```cpp
struct FamilyDescriptor {
    const char* name;
    StatBlock base_stats;
    DerivedBonuses derived;
    const char* sprite_file;
    AnimSequence* anim;
    int default_weapon_family;
    unsigned char radar_color;

    // behavioral callbacks
    void (*special)(walker* self, int special_index);
    bool (*check_special_ai)(living* self, int special_index);
    void (*hit_response)(walker* self, walker* attacker);
    void (*on_death)(walker* self);
    void (*level_up)(guy* self, int new_level);
    void (*set_difficulty)(living* self, int difficulty);
    void (*init_walker)(walker* self);  // replaces set_walker() switch
};
```

Register descriptors at startup, each type in its own `.cpp` file. This gets ~80% of the modularity benefit (self-contained, testable, swappable types) without the pain of shared library loading, dynamic family IDs, save format breakage, and cross-module pointer issues. Can graduate to actual `.so` loading later once the descriptor interface is stable.

### Effort Estimate

The hardest part is not the architecture — it's hunting down and relocating ~18 files' worth of scattered one-off family checks into the descriptor callbacks. The `walker::special()` function is already well-isolated and maps directly. The scattered one-liners in `walker.cpp`, `living.cpp`, `walker_combat.cpp`, etc. are the real grunt work.
