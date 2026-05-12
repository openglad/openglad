# Phase 01 — Audit and Tear-down

## Phase Name
Audit prior fraud and remove poisoned artifacts.

## Implement Phase ID
`01-audit-and-teardown`

## Preexisting Inputs
- `.plan/goal.md`
- `.plan/parity-risk-inventory.md`
- `.plan/parity-harness-design.md`
- `.plan/parity-signoff.md`
- `.plan/parity-divergence-report.md`
- `.plan/parity-fixes.md`
- `tests/parity/parity_runner.cpp`
- `tests/parity/test_parity_scenarios.cpp`
- `tests/parity/scenario_table.h`
- `tests/parity/golden/` (15 empty-world JSON files)
- `../openglad-master/tools/parity_dump_master.cpp`

## New Outputs
- `.plan/parity-redo-audit.md` — fraud inventory. Required sections:
  (a) the byte-identical `{"effects":[],"events":[],...,"tick":1,"walkers":[]}`
  pattern shared by all 15 goldens (with one sample);
  (b) line-numbered citations from `parity_runner.cpp` and
  `tools/parity_dump_master.cpp` for the scenario-not-loaded no-op;
  (c) line-numbered citation from `parity_runner.cpp::apply_inputs_at_tick`
  for the empty body;
  (d) explanation of the `level_done`-on-empty-world short-circuit that
  keeps every golden at `tick: 1`;
  (e) the rename/deletion of `.plan/parity-signoff.md`.
- `.plan/parity-signoff-fraudulent.md` — original signoff renamed in place
  via `git mv` so it remains in history but is not authoritative.

## File Changes
- Delete `tests/parity/golden/*.json` (all 15 files).
- `git mv .plan/parity-signoff.md .plan/parity-signoff-fraudulent.md`
- Create `.plan/parity-redo-audit.md`.

## Implementation Details
No source code is modified in this phase. The audit document must include
literal command outputs (e.g., `wc -c tests/parity/golden/*.json` showing
every file ≤ 200 bytes before deletion) and explicitly state that future
phases will reconstruct goldens from a fixed master companion, not the
current broken one. The fraud-inventory must cite line numbers from the
runner/companion sources so the document is auditable on its own.

## Verification Phases
- **Phase ID**: `01a-check-audit-document`
  - **Type**: `check`
  - **Bounce Target**: `01-audit-and-teardown`
  - **Purpose**: Confirm the audit document exists, lists every deleted
    golden by path, and that the deletions and rename are committed.
  - **Commands**:
    ```
    test -f .plan/parity-redo-audit.md
    test -f .plan/parity-signoff-fraudulent.md
    test ! -f .plan/parity-signoff.md
    [ "$(ls tests/parity/golden/*.json 2>/dev/null | wc -l)" -eq 0 ]
    grep -c '"walkers":\[\]' .plan/parity-redo-audit.md   # >= 1
    [ "$(git log -1 --name-status | grep -c '^D')" -ge 15 ]
    ```

## Success Criteria
- `.plan/parity-redo-audit.md` exists with all five required sections and
  cites the byte-identical empty-world golden pattern.
- `tests/parity/golden/` contains zero `.json` files.
- `.plan/parity-signoff.md` no longer exists; `.plan/parity-signoff-fraudulent.md`
  does, tracked by git history (`git mv`).
- The most recent commit shows ≥ 15 deletions in its name-status.

## Git Commit Requirement
The implementer must `git add` the deletions, the rename, and
`.plan/parity-redo-audit.md`, and `git commit` with a descriptive message
(e.g. `parity-redo: phase 01 — tear down fraudulent golden set and rename
signoff`) **before yielding**. The check phase expects HEAD to contain
the committed changes.
