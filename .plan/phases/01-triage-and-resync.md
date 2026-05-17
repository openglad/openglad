# Phase 01 — Triage and resync uncommitted state

**Phase Name**: Commit WIP, snapshot present-day failure inventory.

**Implement Phase ID**: `01-triage-and-resync`

## Preexisting Inputs

- `.plan/goal.md`
- `.plan/parity-honest-audit.md`
- `.plan/parity-coverage-manifest.md`
- `.plan/master-companion.md`
- `tests/parity/scenario_table.h` (with uncommitted edits)
- `tests/parity/scenario_facts_generated.json` (uncommitted)
- `tests/parity/test_parity_scenarios.cpp` (uncommitted rename)
- `tests/parity/golden/*.json` (39 existing files)
- `../openglad-master/` worktree on `parity-companion`, HEAD `136ea37b...`

## New Outputs

- `.plan/parity-present-state.md` (≤200 lines) with sections:
  - **Test count snapshot** — three integers on three lines:
    `Passed: <P>`, `Skipped: <S>`, `Failing tests: <F>`. Also `Skipped scenarios after this phase: <S>`.
  - **Failing tests** — bulleted list, one per `[  FAILED  ]` line.
  - **Skipped tests** — bulleted list of every `master golden missing for ...` skip cited verbatim.
  - **Master companion SHA (pinned this phase)** — one line `Master companion SHA: <sha>`.
  - **Mirror SHA delta** — literal `sha1sum` output for `tests/parity/scenario_table.h` and `../openglad-master/tools/parity_scenario_table.h`. If not equal, line `BRANCH ≠ COMPANION — Phase 02 resyncs.`
  - **Phase plan acknowledgement** — quote verbatim the broken-state authorisation section of the plan. State explicitly that the user's verbatim goal is the override for the global "all tests pass" rule for the Phase 1–6 window only; treasure cohort + behavioural gates green at end of Phase 4 (verifier 04a); full `og_test_parity` zero FAIL zero SKIP at end of Phase 6 (verifier 06a); entire repository test suite green at end of Phase 8 (verifier 08b). Deferred weapon / effect / generator / event / special cohorts authorised to remain red across Phases 3, 4, 5 and only close in Phase 6.
- Commit of uncommitted WIP staging `tests/parity/scenario_table.h`, `tests/parity/scenario_facts_generated.json`, `tests/parity/test_parity_scenarios.cpp` as-is plus the new doc.

## File Changes

- `git add tests/parity/scenario_table.h tests/parity/scenario_facts_generated.json tests/parity/test_parity_scenarios.cpp .plan/parity-present-state.md`
- `git commit -m "parity-finish-3: phase 01 — commit treasure-row WIP and snapshot present-day failures"`

## Implementation Details

The uncommitted edits rewrite every treasure pickup row to put a soldier at `(96, 120)` + a treasure of the target family at `(160, 120)` + `kInputsTreasurePickup`. They are required by Phase 04's bring-up but currently cause 12 test failures due to family-id walker/treasure aliasing. Phase 1's job is to commit what's there.

Derive PASS / SKIP / FAIL counts from a clean build:

```
cmake --build --preset ci-test --target og_test_parity
build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p01.out
PASSED=$(grep -oE '^\[  PASSED  \] [0-9]+ tests?\.' /tmp/p01.out | awk '{print $3}' | head -1)
SKIPPED=$(grep -oE '^\[  SKIPPED \] [0-9]+ tests?\.' /tmp/p01.out | awk '{print $3}' | head -1)
FAILED=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p01.out)
```

Note: `og_test_parity --gtest_brief=1` does NOT emit a `[  FAILED  ] N tests.` summary line — only PASSED + SKIPPED summaries appear; FAILED is counted from per-test `[  FAILED  ] Parity.<id>` lines.

## Verification Phases

### `01a-check-tree-clean-and-inventory`
- **Type**: `check`
- **Bounce target**: `01-triage-and-resync`
- **Purpose**: confirm the WIP commit landed, the present-state doc exists with the right integers, and `git log -1` lists the expected files.
- **Commands**:
  ```
  git status --porcelain | grep -v '^?? .plan/.juvenal-state.json$' | wc -l    # expect 0
  test -f .plan/parity-present-state.md
  cmake --build --preset ci-test --target og_test_parity 2>&1 | tail -5
  build/ci-test/og_test_parity --gtest_brief=1 2>&1 | tee /tmp/p01.out | tail -3
  PASSED=$(grep -oE '^\[  PASSED  \] [0-9]+ tests?\.' /tmp/p01.out | awk '{print $3}' | head -1)
  SKIPPED=$(grep -oE '^\[  SKIPPED \] [0-9]+ tests?\.' /tmp/p01.out | awk '{print $3}' | head -1)
  FAILED=$(grep -cE '^\[  FAILED  \] Parity\.' /tmp/p01.out)
  grep -F "Passed: ${PASSED}"          .plan/parity-present-state.md
  grep -F "Skipped: ${SKIPPED}"        .plan/parity-present-state.md
  grep -F "Failing tests: ${FAILED}"   .plan/parity-present-state.md
  git log -1 --name-status | grep -F tests/parity/scenario_table.h
  git log -1 --name-status | grep -F tests/parity/scenario_facts_generated.json
  git log -1 --name-status | grep -F tests/parity/test_parity_scenarios.cpp
  git log -1 --name-status | grep -F .plan/parity-present-state.md
  ```

### `01b-check-companion-sha-pinned`
- **Type**: `check`
- **Bounce target**: `01-triage-and-resync`
- **Purpose**: confirm `## Master companion SHA (pinned this phase)` section names the current `../openglad-master` HEAD.
- **Commands**:
  ```
  grep -E '^Master companion SHA: [0-9a-f]{40}$' .plan/parity-present-state.md | head -1
  test "$(grep -oE '[0-9a-f]{40}' .plan/parity-present-state.md | head -1)" \
     = "$(git -C ../openglad-master rev-parse HEAD)"
  ```

## Success Criteria

- `git status --porcelain` (minus `.plan/.juvenal-state.json`) is empty.
- `.plan/parity-present-state.md` exists, contains literal PASS / SKIP / FAIL integers derived from `og_test_parity --gtest_brief=1`, and pins the companion SHA.
- HEAD includes the WIP files and the new doc.

## Git Commit Requirement

The implementer **must** stage and commit the WIP files plus `.plan/parity-present-state.md` to git before yielding. Commit message: `parity-finish-3: phase 01 — commit treasure-row WIP and snapshot present-day failures`. Do not yield with a dirty working tree (except `.plan/.juvenal-state.json`, which is ignored by verifier 01a's grep filter).
