# Refactor tooling (Lua quality plan, stages 0 + 2)

Tooling that makes pack-Lua refactor batches cheap to prove. The contract it
enforces is the one `docs/lua-classpacks-design.md` §3 makes binding:
byte-exact parity (recorder off AND armed), mutation-pin anchoring, the
combined coverage bar, and — for refactors that add helper indirection — a
per-scenario Lua instruction budget.

## Stage-2 mechanical rewriters

Six composable tools generate the Stage-2 de-noising batches over
`packs/core/scripts`. Each is dry-run by default (unified diff to stdout),
writes only under `--apply`, takes `--files` to scope a lane's batch, and
shares the comment/string-aware source model in `lua_corpus.py`. Apply
order (later tools assume the earlier ones' output shapes):

| order | tool | what it does |
|---|---|---|
| 1 | `rewrite_combat.py` | deletes hand-inlined combat_math copies, rewrites calls to `og.combat.*`, fuses the orc stun into `ob:add_frozen_stun` (curated site table; aborts loudly if any site drifted) |
| 2 | `rewrite_rand0.py` | guard trios and the cleric guard helper -> `og.rand0` |
| 3 | `rewrite_clamp.py` | clamp/min/max ladders -> `og.max`/`og.min`/`og.clamp`; sign idioms -> `og.sign` (std:: tie semantics preserved; setter-wrapped clamps deliberately untouched) |
| 4 | `rewrite_headers.py` | 36 boilerplate headers -> curated one-line S2 headers, preserving load-bearing per-file notes verbatim |
| 5 | `strip_provenance.py` | dead `family_*.cpp` cites and stale guard-wrapper comments (curated surgeries keep every genuine RNG-order record); exits 1 on any unhandled dead cite |
| 6 | `shim_audit.py` | classifies all arithmetic-shim sites PROVABLY-EXACT (emits the plain-Lua rewrite) vs KEEP (emits the S5 why-comment); writes `build/refactor-audit/shim_manifest.json` incl. the unguarded-og.rand audit |

Proof rules and shim semantics are documented in each tool's docstring; the
audit is deliberately conservative (unknown ⟹ KEEP — parity is the final
judge, and a wrong EXACT costs a batch cycle).

**Validated end-to-end 2026-07-28** on the full corpus in one shot: all six
applied -> statement lint green -> `og_test_parity` **187/187 semantic
scenarios byte-exact, recorder OFF and ARMED** (the 188th, the pin-anchor
gate, red exactly as designed — line shifts await per-batch pin re-points)
-> instruction budget **147/159 scenarios improved, 0 regressed >10%**.
Net on the corpus: 3,825 -> 3,646 lines; shim call sites 262 -> 208;
guard trios 0 remaining. The applied state was then reverted: lanes apply
these tools batch-by-batch through `probe.sh`, re-pointing pins per batch.

### Lane partition

`s2_partition.py` writes the committed `s2_partition.json`: the 36 files in
3 lanes balanced by line count (A/B/C ~1,273/1,280/1,272 lines), each lane
carrying the subset of `scenario_table.h` mutation pins that anchor into
its files (12/17/20 pins). Every line-shifting batch re-points its lane's
pins (`scripts/parity/check_mutation_pins.py --fix` for the mechanics) and
proves >= 1 canary scenario flip per moved pin — anchors are not teeth.
`s2_partition.py --check` verifies the committed JSON is current.

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

**The comparison base is git-verified.** Before comparing anything,
`instruction_budget.py check` refuses (exit 2, a named
`BASELINE REFUSED (...)` error) any baseline that is not a committed file
captured on a clean ancestor commit:

* `dirty-capture` — the stored tree_sha is not a bare 40-hex commit SHA
  (a `-dirty…` suffix means the capture tree was never a commit);
* `uncommitted-baseline` — the file on disk is untracked or differs from
  the copy committed at HEAD (a freshly re-captured baseline is exactly
  this, which is why capture-then-check-on-the-same-tree can never pass);
* `unknown-commit` / `not-ancestor` — the tree_sha names no commit here,
  or one on some other line of history.

This closes the self-baseline hole: a baseline re-captured on the tree
being checked made `--budget-check` compare the tree to itself and pass
vacuously (the `…-dirty-stage45` incident).

### Re-capturing the baseline

Deliberate act, its own commit — never a drive-by, and never part of a
probe candidate:

```bash
cmake --build --preset ci-test --target og_test_parity
raw=$(mktemp)
OPENGLAD_LUA_INSTRUCTION_REPORT="$raw" ./build/ci-test/og_test_parity
python3 scripts/refactor/instruction_budget.py recapture \
    --raw "$raw" --out scripts/refactor/baseline/instruction_baseline.json
```

`recapture` refuses a dirty tree (`RECAPTURE REFUSED (dirty-tree)`, exit
2) and stamps the clean tree's HEAD SHA itself — there is no flag to
supply a SHA the tree does not have. Commit the result **as its own
commit**, with the mechanism and the per-scenario movement in the message
(the adbd62da precedent): `check` refuses the new baseline until it is
committed. The gate exists to catch runaway growth, not to freeze a
justified one-time constant — but the justification lives in the
re-baseline commit, where review can weigh it.

`recapture` also refuses a raw report in which two runs of one scenario
disagree — per-scenario totals are deterministic (repeat runs within a
suite pass match exactly, and recorder off vs armed produce byte-identical
report files), so a disagreement is a determinism bug, not noise.
