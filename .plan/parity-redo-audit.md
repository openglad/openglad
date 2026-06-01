# Parity Redo — Phase 01 Audit

This document is the fraud inventory for the gameplay parity comparison
framework that was signed off in
`.plan/parity-signoff.md` (now renamed to
`.plan/parity-signoff-fraudulent.md` in the same commit that introduces
this file). The framework, as it stood at the prior signoff, was
green-by-construction: both sides exercise an empty `GameWorld` that
short-circuits on tick 1, so every golden is a 123-byte stub describing
no game state. The sections below cite the exact source lines and
artifact bytes that prove this.

Future phases of the parity-redo workflow will reconstruct goldens from
a fixed master companion, not the current broken one — none of the
existing golden bytes are retained as authoritative.

## (a) Byte-identical empty-world goldens

All fifteen committed goldens in `tests/parity/golden/` are exactly 123
bytes and follow the identical schema-v1 stub
`{"effects":[],"events":[],...,"tick":1,"walkers":[]}`. Only the
`rng_state` field varies across files (and even that variation is
shallow — see `## (d)` below: every dump is produced at `tick: 1` from
an `oblist`-empty world, so `rng_state` simply echoes back the scenario
seed without any draws).

Literal `wc -c tests/parity/golden/*.json` output captured before the
deletions in this phase:

```
 123 tests/parity/golden/ai_idle_wander_scen9301.json
 123 tests/parity/golden/combat_attack_scen99.json
 123 tests/parity/golden/effect_bomb_lifetime_scen99.json
 123 tests/parity/golden/effect_chain_scen9410.json
 123 tests/parity/golden/exit_trigger_scen9302.json
 123 tests/parity/golden/rng_seed_stable_scen99.json
 123 tests/parity/golden/save_roundtrip_scen99.json
 123 tests/parity/golden/scoring_after_combat_scen99.json
 123 tests/parity/golden/scripted_input_scen9301.json
 123 tests/parity/golden/special_archmage_scen123.json
 123 tests/parity/golden/special_cleric_scen124.json
 123 tests/parity/golden/special_mage_scen126.json
 123 tests/parity/golden/special_thief_scen789.json
 123 tests/parity/golden/summon_druid_pet_scen950.json
 123 tests/parity/golden/tick_cadence_scen9301.json
1845 total
```

Every file is ≤ 200 bytes. One literal sample
(`tests/parity/golden/combat_attack_scen99.json`, verbatim):

```
{"effects":[],"events":[],"rng_state":"0x00000042","schema_version":"v1","score_per_team":[0,0,0,0],"tick":1,"walkers":[]}
```

The other fourteen files differ only in the `rng_state` literal
(`0x00000001`, `0x00000007`, `0x00000010`, `0x00000042`, `0x00000123`,
`0x0000BEEF`, `0x0000CAFE`, `0x0000F00D`). `effects`, `events`,
`walkers` are all `[]` in every file; `score_per_team` is `[0,0,0,0]`;
`tick` is `1`; `schema_version` is `"v1"`. A `combat_attack` scenario,
a `special_archmage` scenario, and a `summon_druid_pet` scenario are
indistinguishable at the byte level except for their seed echo. That
is not a parity result — it is the fingerprint of a no-op.

This is the entire pool of "evidence" the prior signoff cited as
proving gameplay parity.

## (b) Scenario-not-loaded no-op — branch runner and master companion

Both sides of the harness explicitly admit in their own source comments
that they do not load the scenario referenced by `spec.scenario_file`.
The runner constructs an empty `GameWorld`, ticks it `tick_budget`
times against an empty `oblist`/`weaplist`/`fxlist`, and dumps the
result. Because the world starts empty, the dump is empty, and the two
sides agree trivially.

Branch runner — `tests/parity/parity_runner.cpp`:

- Lines 23–34, `run_scenario`:
  ```
  23  RunOutcome run_scenario(const ScenarioSpec& spec)
  24  {
  25      RunOutcome out;
  26
  27      // Phase 04 skeleton: scaffolds the canonical drive loop. Scenario files
  28      // referenced by spec.scenario_file are not yet loaded here — that is the
  29      // Phase 06 task. The runner therefore exercises an empty GameWorld, which
  30      // is sufficient to validate determinism plumbing (seed → rng_state → dump)
  31      // without depending on the level loader or PhysFS mount layout.
  32      GameWorld world(spec.rng_seed);
  33      world.rng_.state_ = spec.rng_seed;
  34      out.loaded = false;
  ```
  Line 34 hard-codes `out.loaded = false` — the runner records, in its
  own outcome, that no scenario was loaded. Phase 06 was supposed to
  "wire loading"; instead, Phase 06 captured goldens from this same
  empty-world runner and Phase 07/08 signed them off.

Master companion — `../openglad-master/tools/parity_dump_master.cpp`:

- Lines 62–66, `run`:
  ```
  62  int run(const og::parity::ScenarioSpec& spec, const std::string& out_path)
  63  {
  64      GameWorld world(spec.rng_seed);
  65      world.rng_.state_ = spec.rng_seed;
  ```
- Lines 77–83 (inside the tick loop, the comment is candid about the
  no-op):
  ```
  77      for (std::uint32_t t = 0; t < spec.tick_budget; ++t)
  78      {
  79          // Phase 05 skeleton: scenario input scripts are declared in the table
  80          // but not yet routed into GameWorld. This mirrors the branch runner
  81          // (tests/parity/parity_runner.cpp); both sides exercise an empty world
  82          // until Phase 06 wires loading and input application.
  83          world.tick();
  ```

Two empty `GameWorld`s producing the same dump is not gameplay parity.
It is the cheapest possible self-consistency check on the dump emitter
and the seed echo, dressed up as evidence.

## (c) `apply_inputs_at_tick` empty body

`tests/parity/parity_runner.cpp::apply_inputs_at_tick`, lines 9–19:

```
 9  void apply_inputs_at_tick(const ScenarioSpec& spec, std::uint32_t tick)
10  {
11      // Phase 04 skeleton: scenario input scripts are declared in the table
12      // but not yet routed into og::sim::GameWorld. The branch's input plumbing
13      // flows through GameSession/local_transport_shadow rather than a single
14      // public field on GameWorld, so wiring is deferred to Phase 06/07 when
15      // the master companion is also exercised. We still iterate the script
16      // so any future hook attaches in the canonical location.
17      (void)spec;
18      (void)tick;
19  }
20
```

The function body is two `(void)` discards. The
`scripted_input_scen9301` scenario in particular is sold by the prior
signoff as covering subsystem 11 ("Per-frame transport shadow,
single-player path") via `local_transport_shadow`; the implementation
applies zero inputs and asserts equality against a 123-byte empty-world
dump. Nothing about `local_transport_shadow` is exercised.

## (d) `level_done`-on-empty-world short-circuit pinning `tick: 1`

The reason every golden's `tick` field is `1` rather than `tick_budget`
is a structural property of `GameWorld::tick()` interacting with an
empty `oblist`. The runner short-circuits on the first iteration of its
own loop, before tick 2 is ever produced.

`src/gameplay/game_world.cpp::GameWorld::tick`:

- Line 1355 — at the top of every tick:
  ```
  1355      level_done = 2; // unless we find valid foes while looping
  ```
- Lines 1389–1434 — the only code paths that flip `level_done` back to
  `0` are inside the `for (auto& uptr : oblist)` walker-act loop
  (lines 1406, 1430) and the weapon-act loop (line 1453). With
  `oblist`, `weaplist`, and `fxlist` all empty (no scenario loaded,
  see `## (b)`), none of those branches execute.
- Lines 1482–1488 — at the bottom of the tick, because `level_done`
  was never reset to `0` (no valid foes were found):
  ```
  1482      if (level_done == 2)
  1483      {
  1484          game_ended = true;
  1485          ending = 0;
  1486          next_level = static_cast<short>(id + 1);
  1487          return;
  1488      }
  ```
  The tick returns with `level_done == 2` after `tick_count_` was
  incremented exactly once (line 1357).

Back in `tests/parity/parity_runner.cpp::run_scenario`:

- Lines 36–46 — the harness's own short-circuit:
  ```
  36      for (std::uint32_t t = 0; t < spec.tick_budget; ++t)
  37      {
  38          apply_inputs_at_tick(spec, t);
  39          world.tick();
  40          if (world.level_done != 0 && !out.early_stopped)
  41          {
  42              out.early_stopped   = true;
  43              out.early_stop_tick = world.tick_count_;
  44              break;
  45          }
  46      }
  ```
  `world.level_done` is `2` after the first tick (see above), `!= 0`
  is true, `break` fires, and `tick_count_` is `1`. Every golden's
  `"tick":1` is the runner observing its own "the level is over because
  there is nothing in it" exit on the very first iteration. This is
  independent of `tick_budget` — a budget of 5 or 5000 produces the
  same `tick: 1` dump.

Combine `(b)` + `(c)` + `(d)`: an empty world is constructed, no
inputs are applied, the world declares itself finished on tick 1, the
emitter prints the empty state, and the harness calls that a pass.

## (e) Rename of `.plan/parity-signoff.md`

`.plan/parity-signoff.md` is being renamed to
`.plan/parity-signoff-fraudulent.md` in the same commit that introduces
this audit and deletes the fifteen goldens. The rename uses `git mv` so
the file's history is preserved, but the file is no longer the
authoritative parity signoff — it is retained only as evidence of what
was claimed and merged. No content of that file is edited in this
phase; later phases of the parity-redo workflow will write a new,
honest signoff document under a fresh filename and will reconstruct
goldens from a master companion that actually loads scenarios and
applies inputs.

## Disposition of artifacts in this commit

- `tests/parity/golden/*.json` (15 files): deleted in this commit.
  Future phases will regenerate goldens from a fixed master companion,
  not from the current broken one — the bytes that existed here are
  not retained, not stashed, and not consulted again.
- `.plan/parity-signoff.md` → `.plan/parity-signoff-fraudulent.md`:
  renamed via `git mv` so the prior claims remain visible in history
  while being clearly demoted.
- `.plan/parity-redo-audit.md` (this file): added.

No source code under `src/` or `tests/` is modified in this phase —
the runner, the companion, the scenario table, the dump emitter, and
the parity test file are all left untouched so that later phases can
rewrite them with the broken behaviour still in place as a reference.

As a direct consequence, the fifteen master-comparable
`Parity.*_scen*` sub-tests in `og_test_parity` will report
`FAILED` ("golden not yet captured for <id>") on the next test run
until later phases regenerate goldens at the expected paths. That
failure is the intended and immediate signal of this teardown — the
prior signoff's `pass` rows were only `pass` because the goldens
themselves were empty-world stubs (see `## (a)`); removing the stubs
without weakening the test is what exposes the gap. The
branch-internal `snapshot_dirty_bits_scen9301` sub-test does not
consult a golden and continues to run unaffected.

