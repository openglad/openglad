# Phase 03 — Coverage Manifest and Gate

## Phase Name
Define and enforce coverage taxonomy.

## Implement Phase ID
`03-coverage-manifest-and-gate`

## Preexisting Inputs
- `tests/parity/scenario_table.h` (extended in Phase 02 with `SpawnSpec`,
  `spawns`, `exercises`, `fresh_arena`, `player_team`,
  `is_intentionally_empty`).
- `../openglad-master/tools/parity_scenario_table.h` (mirror).
- `include/openglad/core/constants.h` (defines `FAMILY_*` constants and
  `MAX_TREASURE`).
- `include/openglad/gameplay/event.h` (defines
  `enum class EventKind`).
- `src/gameplay/effect_family_registry.cpp` (effect family registration).
- `src/gameplay/families/family_*.cpp` (family descriptors with
  `set_do_special(...)` bindings).
- `tests/parity/state_dump.cpp::kind_string` (canonical symbolic names
  for `EventKind`, lines 134-147).
- Outputs of Phase 02 (`og_test_parity` binary builds cleanly).

## New Outputs
- `.plan/parity-coverage-manifest.md` — long-form table of every
  coverage target with a `covering_scenario_id` column (initially most
  rows say `(none yet)`). Frontmatter includes
  `master_companion_sha:` (recorded from Phase 02's master commit) so
  Phases 04-07 diff against a fixed master.

- `tests/parity/coverage_targets.h` declaring:
  - `inline constexpr std::int32_t kRequiredWalkerFamilies[]` — all 21
    families from `FAMILY_SOLDIER..FAMILY_TOWER1`.
  - `inline constexpr std::int32_t kRequiredEffectFamilies[]` — all 13.
  - `inline constexpr std::int32_t kRequiredWeaponFamilies[]` — all 20
    from `FAMILY_KNIFE..FAMILY_BOULDER`.
  - `inline constexpr std::int32_t kRequiredTreasureFamilies[]` —
    `FAMILY_STAIN..FAMILY_SPEED_POTION`.
  - `inline constexpr std::pair<std::int32_t, std::uint8_t> kRequiredSpecials[]`
    enumerating every `(family, special_index)` pair where
    `special_index in [1..NUM_SPECIALS-1]` and the family's descriptor
    defines a non-null `do_special`.
  - `inline constexpr std::string_view kRequiredEventKinds[]` — the nine
    non-`None` `EventKind` symbols, matching `state_dump.cpp::kind_string`.

- `tests/parity/test_parity_coverage_gate.cpp` — added to the existing
  `og_add_test_group(parity ...)` source list (no new executable
  target). Cases under the `Parity` GoogleTest suite, selectable via
  `--gtest_filter='Parity.coverage_gate*'`:
  - `Parity.coverage_gate_walker_families`
  - `Parity.coverage_gate_specials`
  - `Parity.coverage_gate_effect_families`
  - `Parity.coverage_gate_weapon_families`
  - `Parity.coverage_gate_treasure_families`
  - `Parity.coverage_gate_event_kinds`
  - `Parity.coverage_gate` (umbrella requiring all above)

  Each case runs every scenario in `kScenarios` once via `run_scenario`,
  collects the union of `walker.family`, `effect.family`, `event.kind`,
  and per-scenario `spec.exercises` bits, and asserts the corresponding
  required array is a subset of the observed union. The fixture builds
  the union once in `SetUpTestSuite()` and caches it. Failures print a
  structured list of every uncovered target.

- `scripts/parity/check_coverage_manifest.py` — pre-build static check.
  Parses `include/openglad/core/constants.h` for every
  `inline constexpr int FAMILY_*` and `include/openglad/gameplay/event.h`
  for `EventKind`. Asserts `tests/parity/coverage_targets.h` is a
  superset of the parsed entities. Exits non-zero with named missing
  entries.

## File Changes
- New: `.plan/parity-coverage-manifest.md`
- New: `tests/parity/coverage_targets.h`
- New: `tests/parity/test_parity_coverage_gate.cpp`
- New: `scripts/parity/check_coverage_manifest.py`
- Modified: `tests/parity/scenario_table.h` — widen
  `enum class Exercises : std::uint64_t` (Phase 02 left it as `None = 0`)
  with one bit per coverage target not structurally observable (e.g.
  `Special_Archmage_1 = 1ULL<<0`, ...).
- Modified: `../openglad-master/tools/parity_scenario_table.h` (mirror
  the `Exercises` widening byte-for-byte).
- Modified: `CMakeLists.txt` — add
  `tests/parity/test_parity_coverage_gate.cpp` to the existing
  `og_add_test_group(parity ...)` source list at line 1807. No new
  `add_executable(...)`.
- Modified: `.plan/parity-harness-design.md` — append "Phase 03 redo:
  coverage gate" section pointing to the manifest and gate test; list
  v1-compatible spec extensions.

## Implementation Details
- The manifest is the single source of truth for "what must be tested".
  Adding a family to `constants.h` without updating the manifest fails
  the static check; adding a manifest entry without a covering scenario
  fails the runtime gate.
- `kRequiredSpecials[]` is populated by inspecting each
  `src/gameplay/families/family_*.cpp` for `set_do_special(...)`
  bindings; if grepping is brittle, the manifest may instead enumerate
  per family-descriptor `special_count` and require
  `(family, 1..special_count)`.
- `kRequiredEventKinds[]` lists the nine non-`None` `EventKind` symbols.
- At Phase 03 completion the coverage gate is expected to **fail at
  runtime** because Phases 04-06 have not yet filled coverage; this is
  intended behaviour and forms the basis of verifier `03b`.

## Verification Phases
- **Phase ID**: `03a-check-manifest-completeness`
  - **Type**: `check`
  - **Bounce Target**: `03-coverage-manifest-and-gate`
  - **Purpose**: Confirm the static manifest enumerates every family,
    weapon, effect, treasure, generator, special-ability index, and
    `EventKind` declared in headers and family descriptors.
  - **Commands**:
    ```
    python3 scripts/parity/check_coverage_manifest.py    # exits 0
    cmake --build --preset ci-test --target og_test_parity
    ```

- **Phase ID**: `03b-check-gate-fails-on-omission`
  - **Type**: `check`
  - **Bounce Target**: `03-coverage-manifest-and-gate`
  - **Purpose**: Confirm the coverage gate is a real **runtime** gate
    that fails when a covering scenario is removed. Build failure is
    NOT an acceptable substitute — the agent fails the check if the
    binary cannot be built.
  - **Commands**:
    ```
    git stash push -m phase03b -- tests/parity/scenario_table.h
    # Edit scenario_table.h to remove one entry (the lone covering
    # scenario recorded by Phase 03 in .plan/parity-coverage-manifest.md)
    cmake --build --preset ci-test --target og_test_parity   # must succeed
    ! ./build/ci-test/og_test_parity \
        --gtest_filter='Parity.coverage_gate*'                # must fail
    git stash pop
    cmake --build --preset ci-test --target og_test_parity
    ```
    The failure output must name the now-uncovered target.

## Success Criteria
- `scripts/parity/check_coverage_manifest.py` exits 0.
- `og_test_parity` builds with the new coverage-gate cases compiled in.
- The coverage gate fails at runtime, naming uncovered targets (Phases
  04-06 will subsequently fill those gaps).
- A flip-test (stash one entry from `kScenarios`) demonstrates the gate
  is responsive to scenario removals.

## Git Commit Requirement
The implementer must `git add` the new files and edits, and `git commit`
with a descriptive message (e.g. `parity-redo: phase 03 — coverage
manifest and runtime gate`) **before yielding**. Master-side mirror of
the `Exercises` enum widening must also be committed on
`../openglad-master`'s `parity-companion` branch. The check phase
expects both worktrees to have the commits at HEAD.
