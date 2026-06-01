# Phase 10 — Mutation canary green

## Phase Name
Every non-exempt scenario row has a `discriminating_mutation` that flips ≥1 predicate; exempt list is finite and justified.

## Implement Phase ID
`10-mutation-canary-green`

## Preexisting Inputs
- `scripts/parity/run_mutation_canary.sh`.
- `scripts/parity/run_mutation_canary_runtime.py` (with `load_exemptions()` at lines 74-91).
- `.plan/parity-canary-exemptions.md` (may not yet exist — phase creates it).
- `tests/parity/scenario_table.h` (final shape from phase 09).
- `tests/parity/scenario_facts_generated.json`.
- `../openglad-master/build/ci-test/parity_dump_master`.
- `../openglad-master/tools/parity_scenario_table.h`.

## New Outputs
- For every row added in phases 05–09 whose `discriminating_mutation` is incomplete or whose canary does not flip, supply a real mutation (a one-line `sed`-applicable change in `tests/parity/scenario_table.h` or in source).
- `.plan/parity-canary-exemptions.md` listing each exempt scenario with a 1–2 sentence justification in the format `load_exemptions()` accepts. Literal grammar (per `scripts/parity/run_mutation_canary_runtime.py:74-91`):
  - Each exempt scenario id appears on its own line as a Markdown bullet `- <scenario_id>` at column 0 (no nested indentation).
  - The bullet's scenario id token is `\S+` after the leading `- `.
  - Free-form prose (rationale, headings, paragraphs) may appear between bullets and is ignored by the parser.
  - Pipe-table rows (`| a | b |`) are NOT understood and MUST NOT be used.
  - Example block (exactly two exempt rows):
    ```
    ## Exempt scenarios

    Justification for each row is captured below the bullet.

    - rng_seed_stable_scen99
      The Invariant compare-mode row has no `discriminating_mutation` because the
      invariant under test is "RNG seed survives load" — every mutation we could
      apply either changes the invariant (cheating) or has nothing to do with
      seed survival (vacuous). Verified by hand 2026-05-17.

    - smoke_noinput_scen99
      The canary's discriminating_mutation tick-budget bump leaves all
      expected_facts trivially satisfied because no input arrives within the
      window. Replaced with a non-trivial input would change the scenario's
      semantic identity; accept canary exemption.
    ```
  - Verifier 10b uses `load_exemptions()` itself as the parsing authority — if the file parses to a non-empty Python `set`, the format is by definition correct.
- A list of every non-exempt scenario id and the flipped-predicate count from the canary run (recorded inside `.plan/parity-canary-exemptions.md` or attached doc, as the implementer prefers).
- Mirror updated (if scenario_table.h was edited to add/repair mutations).

## File Changes
- `tests/parity/scenario_table.h` (mutations updated).
- `.plan/parity-canary-exemptions.md` (new or updated).
- `../openglad-master/tools/parity_scenario_table.h` (mirror, if branch table was edited).
- `tests/parity/scenario_facts_generated.json` (regenerate if scenario_table.h changed).

## Implementation Details
- Run `scripts/parity/run_mutation_canary.sh --all` once to surface the non-exempt zero-flip rows.
- For each such row, choose a mutation that demonstrably changes observable behaviour: alter the spawn family, change a special-button keystroke, advance/retreat the tick budget, swap a treasure id. The mutation must trip at least one `expected_facts[]` predicate.
- For rows where no mutation can flip a predicate without changing the scenario's identity (typically `Invariant` rows or `smoke_noinput`), add the row to `.plan/parity-canary-exemptions.md` with a written justification.

## Verification Phases

### `10a-check-canary-all-flip`
- Type: `check`
- Bounce target: `10-mutation-canary-green`
- Purpose: Every non-exempt scenario row flips at least one predicate under its `discriminating_mutation`; every exempt row is explicitly listed.
- Commands:
  - `scripts/parity/run_mutation_canary.sh --all 2>&1 | tee /tmp/p10a.out`. Exit 0.
  - In-line `python3 -c '<...>'`: parse every per-scenario `/tmp/canary_runtime_<id>.json`, assert every non-exempt id has `"flipped":` value ≥ 1, and every exempt id is listed in `.plan/parity-canary-exemptions.md` (exempt set loaded via the runtime's own `load_exemptions()`).

### `10b-check-exemptions-parseable`
- Type: `check`
- Bounce target: `10-mutation-canary-green`
- Purpose: `.plan/parity-canary-exemptions.md` parses with `load_exemptions()` to a non-empty set; pipe-table bypass is rejected.
- Commands:
  - `python3 -c "import sys; sys.path.insert(0, 'scripts/parity'); import run_mutation_canary_runtime; s = run_mutation_canary_runtime.load_exemptions('.plan/parity-canary-exemptions.md'); print(s); sys.exit(0 if s else 1)"` exits 0 and prints a non-empty set.
  - Anti-cheat: create throwaway worktree `/tmp/parity-bypass-10b` via `git worktree add /tmp/parity-bypass-10b HEAD`. Add a `| pipe | table | row |` line to `/tmp/parity-bypass-10b/.plan/parity-canary-exemptions.md`. Then re-run `load_exemptions('/tmp/parity-bypass-10b/.plan/parity-canary-exemptions.md')` and assert the returned set is identical to the unmutated set (or that the parser raises) — i.e. the pipe-table row is NOT silently parsed as an exemption. Cleanup: `git worktree remove --force /tmp/parity-bypass-10b`.

### `10c-check-no-suite-regression`
- Type: `check`
- Bounce target: `10-mutation-canary-green`
- Purpose: Full parity suite still green; mirror byte-equal.
- Commands:
  - `build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p10c.out`. `grep -cE '^\[  FAILED  \]' /tmp/p10c.out` equals `0`. `grep -cE '^\[  SKIPPED \]' /tmp/p10c.out` equals `0`.
  - `cmp tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h` exits 0.

## Success Criteria
- All three check phases (`10a`, `10b`, `10c`) pass.
- Every non-exempt scenario row flips ≥1 predicate under its discriminating mutation.
- `.plan/parity-canary-exemptions.md` parses with `load_exemptions()` to a non-empty set.
- Anti-cheat pipe-table bypass does not slip past the parser.
- Full parity suite `[  FAILED  ] 0`, `[  SKIPPED ] 0`.
- Mirror byte-equal.

## Git Commit Requirement
Commit BOTH worktrees before yielding (companion commit needed only if scenario_table.h changed).

Companion (in `../openglad-master`, only if branch scenario_table.h changed):
```
git -C ../openglad-master add tools/parity_scenario_table.h
git -C ../openglad-master commit -m "parity-companion: phase 10 — mirror mutation tweaks"
```

Branch:
```
git add tests/parity/scenario_table.h \
        tests/parity/scenario_facts_generated.json \
        .plan/parity-canary-exemptions.md
git commit -m "parity-cov: phase 10 — mutation canary green"
```
