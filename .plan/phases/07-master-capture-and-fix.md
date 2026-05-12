# Phase 07 — Master Re-capture Verification and Regression Triage

## Phase Name
Verify (do not overwrite) goldens, classify divergences, fix.

## Implement Phase ID
`07-master-capture-and-fix`

## Preexisting Inputs
- All outputs of Phases 04-06 (canonical goldens already committed
  under `tests/parity/golden/`).
- `scripts/parity/capture_master_golden.sh` — extended in this phase to
  accept a destination directory argument so re-capture writes to a
  throwaway location.
- `../openglad-master/build/ci-test/parity_dump_master` rebuilt against
  the master companion SHA recorded in
  `.plan/parity-coverage-manifest.md` frontmatter.
- `.plan/parity-fixes.md` — reused as divergence log, rewritten to
  reflect real classifications.
- `.plan/parity-coverage-manifest.md` (frontmatter
  `master_companion_sha:` from Phase 02).
- `scripts/parity/audit_event_coverage.py` (Phase 06; extended here to
  support `--reject-empty-walkers`).
- `tests/parity/parity_runner_smoke_main.cpp` (Phase 02; extended here
  to support `--list`).
- `src/gameplay/walker_combat.cpp:302` (canary perturbation site).

## New Outputs
- `.plan/parity-fixes.md` rewritten with real divergence classifications
  observed by re-capturing into `/tmp/golden-rebuild/` and diffing
  against the committed golden tree. Each row is either:
  - `regression` — with a code-fix commit SHA on the branch, or
  - `intended_diff` — with the branch commit SHA authorising the change.
  Wording such as "no divergences observed vacuously" is forbidden. If
  re-capture yields zero diffs, the document states so explicitly and
  records the scenario count diffed.
- Branch-side source-code fixes (in `src/` or `include/`) for any real
  regressions surfaced by the diff, committed with `parity-fix:` tagged
  messages. The committed goldens are **not** overwritten except via
  the intended_diff path.
- `.plan/parity-coverage-manifest.md` annotated with the final master
  companion SHA and the branch HEAD SHA at the time of re-capture.

## File Changes
- Modified: `scripts/parity/capture_master_golden.sh` — accept a
  destination-directory argument (defaults preserved for Phase 04-06
  callers), iterate `parity_dump_master --list`, write per-scenario
  JSON, schema-validate, abort on malformed output.
- Modified: `tests/parity/parity_runner_smoke_main.cpp` — add `--list`
  that prints every scenario id in `kScenarios` one per line.
- Modified: `scripts/parity/audit_event_coverage.py` — add
  `--reject-empty-walkers` mode rejecting any scenario with
  `tick_budget > 1` and `is_intentionally_empty == false` whose dump
  has empty `walkers[]`.
- Modified (rare): `tests/parity/golden/*.json` — only via the
  intended_diff path, never as a blind re-capture. Any commit modifying
  a golden must also modify `.plan/parity-fixes.md` with a matching
  `intended_diff:` row citing a branch commit SHA.
- Modified: `.plan/parity-fixes.md`.
- Modified: `.plan/parity-coverage-manifest.md`.
- Possibly modified: `src/gameplay/*`, `src/interface/*`, etc. — only
  if real regressions surface.

## Implementation Details
- Phase 07 is a **verification** pass, not a re-capture pass over the
  committed goldens. The re-capture writes into `/tmp/golden-rebuild/`
  and the agent runs `diff -r tests/parity/golden/ /tmp/golden-rebuild/`.
  Expected outcome: **zero diffs**.
- Re-capture sequence:
  1. `mkdir -p /tmp/golden-rebuild && rm -rf /tmp/golden-rebuild/*`.
  2. `(cd ../openglad-master && cmake --build --preset ci-test
      --target parity_dump_master)`.
  3. `scripts/parity/capture_master_golden.sh /tmp/golden-rebuild/`.
  4. `diff -r tests/parity/golden/ /tmp/golden-rebuild/`.
- When a diff fires the agent inspects the branch-side code path and
  either fixes branch code (commit message tagged `parity-fix:`) or
  classifies as `intended_diff` citing the branch commit SHA that
  authorised the behaviour change. Blindly re-capturing the golden is
  forbidden by contract item #9; verifier `07b` enforces this.
- The `07c` canary automates the prior signoff's manual canary by
  perturbing the damage line at `src/gameplay/walker_combat.cpp:302`
  (current statement
  `stats_->set_hitpoints(stats_->hitpoints() - tempdamage_i);`) via a
  one-line `sed` replacing `- tempdamage_i)` with `- (tempdamage_i + 1))`,
  rebuilding `og_test_parity`, asserting non-zero exit, then reverting
  via `git checkout -- src/gameplay/walker_combat.cpp` and rebuilding
  to confirm a clean tree still passes.

## Verification Phases
- **Phase ID**: `07a-check-all-goldens-present`
  - **Type**: `check`
  - **Bounce Target**: `07-master-capture-and-fix`
  - **Purpose**: Confirm every scenario id has exactly one matching
    golden file and that no golden is illegitimately empty.
  - **Commands**:
    ```
    cmake --build --preset ci-test --target parity_runner_smoke
    expected=$(./build/ci-test/parity_runner_smoke --list | wc -l)
    actual=$(ls tests/parity/golden/*.json | wc -l)
    [ "$expected" = "$actual" ]
    for id in $(./build/ci-test/parity_runner_smoke --list); do
      test -f "tests/parity/golden/${id}.json"
    done
    python3 scripts/parity/audit_event_coverage.py \
        --reject-empty-walkers tests/parity/golden/
    ```

- **Phase ID**: `07b-check-parity-clean`
  - **Type**: `check`
  - **Bounce Target**: `07-master-capture-and-fix`
  - **Purpose**: Confirm the parity suite passes 100%, re-capture
    produces zero diffs against the committed goldens, and any
    `intended_diff` row cites a branch commit that exists in `git log`.
  - **Commands**:
    ```
    cmake --build --preset ci-test --target og_test_parity
    ctest --preset ci-test -R '^og_test_parity' --output-on-failure
    rm -rf /tmp/golden-rebuild && mkdir -p /tmp/golden-rebuild
    (cd ../openglad-master && cmake --build --preset ci-test \
        --target parity_dump_master)
    scripts/parity/capture_master_golden.sh /tmp/golden-rebuild/
    diff -r tests/parity/golden/ /tmp/golden-rebuild/
    # Validate every intended_diff cites an existing branch commit.
    python3 - <<'PY'
    import re, subprocess, pathlib
    doc = pathlib.Path('.plan/parity-fixes.md').read_text()
    for sha in re.findall(r'intended_diff:.*?([0-9a-f]{7,40})', doc):
        subprocess.check_call(['git', 'cat-file', '-e', sha + '^{commit}'])
    PY
    ```
    All commands exit 0.

- **Phase ID**: `07c-check-noop-perturbation`
  - **Type**: `check`
  - **Bounce Target**: `07-master-capture-and-fix`
  - **Purpose**: Confirm the harness is load-bearing by perturbing a
    single gameplay constant and asserting at least one parity test
    fails; revert and confirm the clean tree still passes.
  - **Commands**:
    ```
    [ -z "$(git status --porcelain)" ]
    sed -i 's/- tempdamage_i)/- (tempdamage_i + 1))/' \
        src/gameplay/walker_combat.cpp
    cmake --build --preset ci-test --target og_test_parity
    if ctest --preset ci-test -R '^og_test_parity' \
         --output-on-failure ; then
      echo "ERROR: canary failed to fire"
      git checkout -- src/gameplay/walker_combat.cpp
      exit 1
    fi
    git checkout -- src/gameplay/walker_combat.cpp
    cmake --build --preset ci-test --target og_test_parity
    ctest --preset ci-test -R '^og_test_parity' --output-on-failure
    ```

## Success Criteria
- Every scenario id printed by `parity_runner_smoke --list` has a
  matching `tests/parity/golden/<id>.json`.
- No non-intentionally-empty scenario has an empty `walkers[]` golden.
- `ctest --preset ci-test -R '^og_test_parity'` reports 100% pass.
- `diff -r tests/parity/golden/ /tmp/golden-rebuild/` produces zero
  output.
- Every `intended_diff` entry in `.plan/parity-fixes.md` cites a
  reachable branch commit SHA.
- Canary perturbation breaks at least one parity test; reverting
  restores a clean pass.

## Git Commit Requirement
The implementer must `git add` updated scripts, the rewritten
`.plan/parity-fixes.md`, manifest SHA annotations, and any
`parity-fix:` code fixes, then `git commit` (one or more commits, each
tagged either `parity-redo: phase 07 — ...` or `parity-fix: ...`)
**before yielding**. If the intended_diff path is used the same commit
must touch both the modified golden and `.plan/parity-fixes.md`. The
check phase expects HEAD to contain all such commits and the working
tree to be clean.
