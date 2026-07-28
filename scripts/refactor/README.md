# Refactor tooling (Lua quality plan, stage 0)

Tooling that makes pack-Lua refactor batches cheap to prove. The contract it
enforces is the one `docs/lua-classpacks-design.md` §3 makes binding:
byte-exact parity (recorder off AND armed), mutation-pin anchoring, the
combined coverage bar, and — for refactors that add helper indirection — a
per-scenario Lua instruction budget.

## probe.sh — batch parity prober

```bash
# candidate = the tree's dirty state
scripts/refactor/probe.sh

# candidate = a patch file (applied onto a clean tree)
scripts/refactor/probe.sh my-batch.patch

# with the instruction-budget gate
scripts/refactor/probe.sh --budget-check my-batch.patch
```

Gates, in order: `build` (which already runs the pack-Lua statement lint and
the mutation-pin check as build dependencies), `parity-off`,
`parity-armed`, `coverage`, `pins`, and optionally `budget`. Fail-fast: a
red gate prints its log tail, later gates report `skip`, the tree is
auto-reverted to HEAD, and the exit status is 1.

**The candidate is never lost.** Before any gate runs it is stored as a git
stash entry (`git stash push --include-untracked`) and immediately
re-applied; the stash SHA is printed at the start and in the verdict. After
a red probe, `git stash apply <sha>` brings the candidate back. After a
green probe the candidate is still applied in the tree and the stash entry
remains as a backup.

### Coverage modes

* default — **Lua-only**: the full ci-test ctest suite runs with the
  recorder armed and `scripts/coverage/coverage_report.py` applies the
  95% line / 100% function bar to the Lua half. The C++ half is reported
  unmeasured (a ci-test build has no gcov data). This is the fast mode
  refactor batches iterate under, and for pack-script-only candidates it is
  the half that moves.
* `--full-coverage` — the **full cycle**: configure + build the
  `ci-coverage` preset, wipe stale `.gcda`, `coverage_run`, then
  `coverage_report`, gating the C++ half, the Lua half AND the union.
  Slower; run it before merging anything that touches C++.
* `--skip-coverage` — no coverage gate at all. Iteration speed only; never
  evidence for a landing.

## Instruction budget

`ScriptHost` counts VM instructions in its budget hook. With
`OPENGLAD_LUA_INSTRUCTION_REPORT=<file>` set, the parity harness appends one
`<scenario-id>\t<total>` line per scenario run — the world host's cumulative
count: pack-script replay plus every hook dispatch — and `ScriptHost` drops
to a per-instruction hook cadence so the totals are exact instead of
quantized to the 4096-instruction budget-check interval. The variable is
read-only reporting: sims are byte-identical with and without it (the parity
gate runs in both modes), budget semantics are unchanged, and goldens are
untouched.

`scripts/refactor/baseline/instruction_baseline.json` is the committed
baseline (the SHA of the tree it was captured on is inside).
`probe.sh --budget-check` reruns the report and fails when any scenario's
total grew more than 10% over the baseline (`--max-regression` to adjust) —
the budget the refactor plan holds helper indirection to.

### Re-capturing the baseline

Deliberate act, its own commit — never a drive-by:

```bash
cmake --build --preset ci-test --target og_test_parity
raw=$(mktemp)
OPENGLAD_LUA_INSTRUCTION_REPORT="$raw" ./build/ci-test/og_test_parity
python3 scripts/refactor/instruction_budget.py aggregate \
    --raw "$raw" --tree-sha "$(git rev-parse HEAD)" \
    --out scripts/refactor/baseline/instruction_baseline.json
```

The aggregator refuses a raw report in which two runs of one scenario
disagree — per-scenario totals are deterministic (repeat runs within a
suite pass match exactly, and recorder off vs armed produce byte-identical
report files), so a disagreement is a determinism bug, not noise.
