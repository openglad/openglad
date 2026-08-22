---
name: openglad-parity
description: The gameplay parity harness (og_test_parity), master-companion goldens, the mutation canary, and scenario_table.h pin discipline. Use whenever a task edits sim code under src/gameplay or packs/core, touches tests/parity, plans a golden recapture or rebaseline, adds a parity scenario, or asks whether a change "breaks parity" — and before editing ANY file named in a scenario_table.h pin, because a one-line insert silently detooths canary pins.
---

# OpenGlad parity harness

`og_test_parity` is the determinism gate: each scenario replays a scripted
sim headlessly and byte-compares the state dump against a golden captured
from the pre-networking master companion. The sim IS deterministic across
clean rebuilds — apparent non-determinism is always a stale build or a
different config sneaking in. Master itself drew AI path-recheck cadence
and elf projectile spread from libc `rand()`; the harness reproduces that
stream (`srand(1)` + a cosmetic-RNG override in `parity_runner.cpp`), which
is why production cannot byte-match master but the harness can — it proves
gameplay LOGIC given identical RNG, the strongest equivalence available
against a non-deterministic reference.

## Running it

- **Always from the repo root.** Goldens are cwd-relative: run from
  `build/ci-test` and every scenario reads as "missing golden" — mass
  drift that isn't. `ctest --preset ci-test -R '^og_test_parity$'` is the
  canonical invocation.
- Fresh worktrees fail the scen99-fixture scenarios until `og_test_level`
  has run once (it generates the gitignored `temp/scen/scen99.fss`); ctest
  DEPENDS handles ordering, standalone runs don't. Copy `temp/scen/*.fss`
  from the main tree when setting up a worktree.
- `./build/ci-test/parity_runner_smoke --scenario <id> --out <file>`
  prints the canonical branch StateDump JSON; `--evaluate-facts` runs the
  predicates. `scripts/parity/diff_dumps.py <dump> tests/parity/golden/<id>.json`
  (exit 0 = semantic match) is the authoritative branch-vs-golden check.
- **Stale-table trap** (cost hours): `kScenarios` is `inline constexpr` in
  `scenario_table.h`, and incremental builds do not reliably recompile the
  TUs embedding it. Before capturing or measuring, force-clean the dumper
  and harness object dirs (`rm -rf build/ci-test/CMakeFiles/og_test_parity.dir`
  and the companion's `parity_dump_master.dir`), rebuild, and verify the
  dump's `tick` equals the row's budget as a staleness canary.
- After a failed headless run, `git status cfg/` — a session with nothing
  mounted rewrites `cfg/openglad.yaml` via the cwd fallback; restore it
  before trusting later results.

## The master companion

Branch `parity-companion` (pushed to origin) is the master-era tree plus
recorder-only commits; its worktree conventionally lives at
`../openglad-master` (`git worktree prune && git worktree add
../openglad-master parity-companion`, rebuild `parity_dump_master` there
with the ci-test preset). It is e761-era code needing SDL2: on a machine
with only SDL3, point `PKG_CONFIG_PATH` at the `sdl2-compat.dev` and
`SDL2_mixer.dev` pkgconfig dirs (`nix build --no-link --print-out-paths
'nixpkgs#sdl2-compat.dev'` — `nix shell` alone does NOT set
PKG_CONFIG_PATH, and the `.pc` files live in the `.dev` outputs).

- `tests/parity/scenario_table.h` and the companion's
  `tools/parity_scenario_table.h` must be **byte-identical** (`cmp`)
  before any capture. Commit both trees, companion first.
- The companion build does not recompile on a header-only table change
  (source-newer-than-object guard) — delete the dumper's `.o` first or a
  new id fails with "unknown scenario".
- If a behavior depends on a render-driven field (the `drawcycle` lesson:
  render-only counters FREEZE in a headless sim), the companion must
  replicate the render-loop bump as a cosmetic override — otherwise the
  golden encodes a frozen artifact and goes green while the real game
  misbehaves. Never just skip or branch-internal such a scenario.
- Capturing the same ids into two directories before and after a
  companion edit and `diff -rq`-ing them is the cheap proof that a
  companion change moves nothing.

## Goldens and the drift ledger

`tests/parity/golden/DRIFT_LEDGER.md` is the authoritative record of every
golden that deliberately diverges from the raw companion capture:
master-era divergences root-caused to specific fixes, and **intentional
gameplay changes whose goldens were captured from the fixed branch, pinned
to the fix SHA**. A companion recapture must NEVER be pasted over a
ledger-blessed golden — the teeth floor can sit inside the cross-arm
drift. When adjudicating "does the branch match main", match the PR's
merge base, not the ancient companion: capture companion → diff branch →
on mismatch reproduce at the merge base → branch==merge-base means bless
the branch dump + add a ledger row; branch!=merge-base means PR
regression, never blessed. Any deliberate gameplay fix that moves goldens
also gets a `docs/GAMEPLAY_FIXES_FROM_CLASSIC.md` row.

## Adding a scenario (the coordinated edits)

1. `tests/parity/scenario_table.h`: `kInputs_*`, `kFamilySpawns_*`,
   `kFacts_*`, `kMut_*`, and the `ScenarioSpec` row.
2. `tests/parity/test_parity_scenarios.cpp`: `OG_PARITY_TEST(<id>)` —
   without it the per-name gtest does not exist.
3. Mirror the table to the companion (byte-identical), rebuild
   `parity_dump_master`, capture via
   `scripts/parity/capture_master_golden.sh <id>`.
4. `tests/parity/scenario_facts_generated.json` is regenerated by ctest
   (write_scenario_facts_json) — commit it.
5. Prove the mutation flips (see canary below) before calling it done.

Sim mechanics that constrain scenario design: SpawnSpec x/y are RAW
PIXELS (GRID_SIZE is 16); hp predicates are in cents (hp×100); spawns
PREPEND, so the player walker is the LAST team-0 living in the spawn
array; a team-0 treasure/prop can steal the player-control takeover —
spawn props on a non-zero team; only the player team follows the input
transcript, other teams act as AI; each hit sets `regen_delay=50`, so
scenarios ending within 50 ticks of combat see no regen drift; treasure
eating is positional — place consumables at the walker's final stop;
default fire heading is FACE_UP; dead entities are never reaped once
`game_ended` latches, so enemy-free arenas reap nothing (park a distant
hostile TOWER1 to hold `level_done=0`).

## The mutation canary (teeth oracle)

`scripts/parity/run_mutation_canary.sh` applies a scenario's `kMut_*`
from/to swap to real source, rebuilds, and requires ≥1 predicate flip.
It is NOT in CI — CI only validates that pin files/lines exist. Facts:

- `--all` exits 1 by design: the `CompareMode::Invariant` rows
  (`smoke_empty_scen99`, `snapshot_dirty_bits_scen9301`) have no
  discriminating mutation. The number that matters is GENUINE toothless
  (`grep -c '0 flips'`), which must be 0.
- The canary restores files via `git checkout --` and will DESTROY
  uncommitted changes in mutated files. On a dirty tree, drive mutations
  by hand: back up bytes → `_apply_mutation.py` → rebuild → gtest filter
  → restore the backup bytes.
- C++ canary runs leave the MUTATED binary in build/ci-test — rebuild
  before running anything else from that tree.
- Run the FULL suite under a mutation, not a single filter: collateral
  flips (a mutation that kills a whole Lua chunk reds many rows) are only
  measurable that way, and named-control greenness is the honesty check.

## Pin discipline (the six known rot modes)

Every pin is `{file, LINE, from-text, to-text}` applied on that exact
line. `scripts/parity/check_mutation_pins.py` validates anchors and is a
build dependency of og_test_parity; `--fix` re-points drifted pins — but
READ THE DIFF after, and know the blind spots:

1. **Line drift**: any insert above a pin in a pinned file shifts it;
   nothing fails, the teeth just go. After editing ANY sim file, grep it
   against scenario_table.h pins — this applies to every sim file, not
   just walker.cpp (a toast commit in a treasure file once broke 4 pins
   silently).
2. **Anchors are not teeth**: a pin can resolve perfectly yet mutate a
   value the engine no longer reads (data moved to YAML/Lua). Only a
   canary RUN detects this class. After any refactor that changes where a
   value lives, run the canary, don't just validate anchors.
3. **Generic to-texts false-positive** the checker's either-side accept:
   a short to-text like `return false` can substring-match an unrelated
   line. Only an apply attempt or staged-copy flip run catches it
   (mutate the staged `build/ci-test/packs` copy per .lua pin, require a
   flip — ~3s/pin, no rebuild).
4. **Same-text multiple occurrences**: a mechanical "+N lines" repin can
   land a pin on the WRONG occurrence of a repeated statement. After any
   mechanical repin of a text appearing on several lines, re-verify the
   flip at runtime.
5. **Lua-syntax-error to-texts** (bare mid-block `return false`) kill the
   whole chunk on apply and flip rows via collateral damage — fake teeth.
   Use `do return false end`.
6. **Boundary-dead predicates**: a mutated value landing exactly ON an
   inclusive fact bound is inert; the per-fact flip check
   (`parity_runner_smoke --evaluate-facts` under mutation) is the
   detector.

`scenario_table.h` is compiled INTO og_test_parity — after editing pins,
rebuild before trusting the in-suite gate.

## Harness blind spots (check reachability before planning a rebaseline)

- `apply_post_load_spawns` never calls `walker::set_difficulty`, so
  difficulty-scaling sim changes move ZERO goldens. Verify a change is
  reachable from the harness before predicting/promising a recapture.
- The harness is render-blind (both sides headless): render-driven
  behavior needs direct sim tests, not goldens. `worldz`/`vz` are not in
  the state dump, so Z arcs are parity-invisible by design (load-bearing).
- No parity scenario places guards or dormant walkers; act-type-specific
  code there is uncovered by goldens.
- Presentation is parity-invisible: sounds, notifications, glyphs, radar
  colors, damage that never lands. If every perturbation you try is
  inert, the honest conclusion is "no parity coverage here" — say so
  rather than inventing a proof.

## Sim-determinism test idioms (unit groups)

`og::sim::set_sim_random_override` only works in TUs compiled with
`-DTESTING`; `SimRandom::next()` is inline, so og_gameplay's copies ignore
the override and an installed spy silently records NOTHING — assertions
on it go vacuous. Instead set `world.rng_.state_ = <constant>` and assert
observable outcomes. `next(0)` returns early WITHOUT advancing state, so
"state unchanged across the call" proves no draw happened. Walker
construction draws from `walker_rng()`, which fixtures redirect, so
construction never perturbs the world stream.
