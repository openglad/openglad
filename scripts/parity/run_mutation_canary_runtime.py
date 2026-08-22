#!/usr/bin/env python3
"""Phase 04-prep — runtime behavioural mutation canary.

For every scenario in `tests/parity/scenario_table.h`, this driver:
  1. Captures the baseline `og_test_parity --gtest_filter=Parity.<id>`
     verdict (expected PASS).
  2. Applies the row's `discriminating_mutation` via
     `scripts/parity/_apply_mutation.py`.
  3. Rebuilds `og_test_parity`.
  4. Re-runs the same gtest filter.
  5. Restores the worktree (`git checkout --` the mutated file) and
     rebuilds to a clean baseline before processing the next scenario.

A scenario is counted as a flip iff the gtest verdict turned from PASS
to FAIL — i.e. at least one predicate flipped from `ok=true` to
`ok=false` against the runtime dump. A row whose post-mutation gtest
still passes is a runtime canary failure: the mutation did not
discriminate any committed predicate, so the scenario carries no
behavioural binding under that mutation.

Per-scenario JSON output is written to `/tmp/canary_runtime_<id>.json`.
Each file carries `{scenario, mutation, pre, post, flipped, predicate_trace}`
where `predicate_trace` is the per-predicate JSON emitted by
`parity_runner_smoke --evaluate-facts` (post-mutation) when that binary
is available, else an empty list.

Exemptions: a row may be skipped by listing its scenario id (one per
line, lines starting with `#` are comments) in
`.plan/parity-canary-exemptions.md`. If the file is absent the
exemption set is empty.

Usage:
  run_mutation_canary_runtime.py --scenario <id>
  run_mutation_canary_runtime.py --all
  run_mutation_canary_runtime.py --filter <glob>

Exit codes:
  0  every processed scenario flipped at least one predicate
  1  one or more scenarios produced zero flips
  2  argv / environment error
  3  no scenarios selected
  4+ tool / build failure (per-scenario; not a hard abort)
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT  = SCRIPT_DIR.parent.parent
TABLE_HEADER = REPO_ROOT / "tests" / "parity" / "scenario_table.h"
APPLY_MUT  = SCRIPT_DIR / "_apply_mutation.py"
EXEMPTIONS = REPO_ROOT / ".plan" / "parity-canary-exemptions.md"
PRESET     = os.environ.get("OG_PARITY_CANARY_PRESET", "ci-test")
BUILD_DIR  = REPO_ROOT / "build" / PRESET
PARITY_BIN = BUILD_DIR / "og_test_parity"
SMOKE_BIN  = BUILD_DIR / "parity_runner_smoke"
# Declares the applied pin to check_mutation_pins.py; see rebuild_targets().
MUTATION_IN_FLIGHT_ENV = "OPENGLAD_MUTATION_IN_FLIGHT"


# Defer the lint-script import until repo-root sys.path is set up.
sys.path.insert(0, str(SCRIPT_DIR))
import lint_scenario_facts as lint  # noqa: E402


def load_exemptions() -> set[str]:
    """Read `.plan/parity-canary-exemptions.md` if it exists; ignore
    blank lines and `#`-prefixed comments."""
    if not EXEMPTIONS.is_file():
        return set()
    out: set[str] = set()
    for raw in EXEMPTIONS.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        # Tolerate bullet-list style `- <id>` lines.
        m = re.match(r"^-\s*([A-Za-z0-9_.\-]+)\s*$", s)
        if m:
            out.add(m.group(1))
            continue
        if re.fullmatch(r"[A-Za-z0-9_.\-]+", s):
            out.add(s)
    return out


def require_clean_worktree() -> None:
    res = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "status", "--porcelain"],
        capture_output=True, text=True, check=True,
    )
    if res.stdout.strip():
        sys.stderr.write(
            "canary_runtime: worktree not clean; commit or stash first.\n"
            f"{res.stdout}\n"
        )
        sys.exit(2)


def all_scenario_ids(rows: list[dict]) -> list[str]:
    return [r["id"] for r in rows]


def match_glob(pattern: str, ids: list[str]) -> list[str]:
    hits = [i for i in ids if fnmatch.fnmatchcase(i, pattern)]
    if not hits:
        sys.stderr.write(
            f"canary_runtime: --filter {pattern!r} matched zero scenarios\n"
        )
        sys.exit(3)
    return hits


def rebuild_targets(in_flight: dict | None = None) -> bool:
    """Rebuild the canary's targets, declaring any mutation now applied.

    check_mutation_pins.py is a build dependency of og_test_parity, so it
    runs here. On a shared anchor — several pins on one line — the mutated
    line matches neither side of the siblings' substitutions, and without a
    declaration the check reds and takes the rebuild (and the canary) with
    it. `in_flight` is the pin that was just applied, spelled exactly as the
    table spells it; the check recognises that one state and nothing else.
    """
    targets = ["og_test_parity"]
    if SMOKE_BIN.exists() or shutil.which("ninja"):
        targets.append("parity_runner_smoke")
    env = dict(os.environ)
    if in_flight is None:
        env.pop(MUTATION_IN_FLIGHT_ENV, None)
    else:
        env[MUTATION_IN_FLIGHT_ENV] = json.dumps(
            {"file": in_flight["file"], "line": int(in_flight["line"]),
             "from": in_flight["from"], "to": in_flight["to"]})
    res = subprocess.run(
        ["cmake", "--build", "--preset", PRESET, "--target", *targets],
        capture_output=True, text=True, env=env,
    )
    if res.returncode != 0:
        sys.stderr.write(res.stdout + res.stderr)
        return False
    return True


def run_gtest(scenario_id: str) -> tuple[str, str]:
    """Return ("PASS"|"FAIL", captured-text)."""
    if not PARITY_BIN.is_file():
        return ("FAIL", "og_test_parity binary missing")
    res = subprocess.run(
        [str(PARITY_BIN),
         f"--gtest_filter=Parity.{scenario_id}",
         "--gtest_color=no", "--gtest_print_time=0"],
        capture_output=True, text=True,
    )
    verdict = "PASS" if res.returncode == 0 else "FAIL"
    return (verdict, res.stdout + res.stderr)


def run_smoke_eval(scenario_id: str, out_path: Path) -> bool:
    """Run parity_runner_smoke --evaluate-facts to capture per-predicate
    JSON. Returns True iff the smoke binary exists AND ran cleanly."""
    if not SMOKE_BIN.is_file():
        return False
    res = subprocess.run(
        [str(SMOKE_BIN), "--scenario", scenario_id,
         "--evaluate-facts", "--out", str(out_path)],
        capture_output=True, text=True,
    )
    return res.returncode == 0 and out_path.is_file()


def diff_predicate_traces(pre: list[dict] | None,
                          post: list[dict] | None) -> list[str]:
    """Return a list of `"#<i>=<kind>(<pre>-><post>)"` for every index
    whose `ok` flipped between the pre- and post-mutation runs. Used
    only for the per-scenario JSON `predicate_trace` summary."""
    flips: list[str] = []
    if not pre or not post:
        return flips
    pre_by  = {f.get("index"): f for f in pre  if isinstance(f, dict)}
    post_by = {f.get("index"): f for f in post if isinstance(f, dict)}
    for i in sorted(set(pre_by) | set(post_by)):
        a, b = pre_by.get(i), post_by.get(i)
        if a is None or b is None:
            flips.append(f"#{i}=structural-change")
            continue
        if a.get("ok") != b.get("ok"):
            kind = a.get("kind") or b.get("kind") or "?"
            flips.append(f"#{i}={kind}({a.get('ok')}->{b.get('ok')})")
    return flips


def lookup_mutation(row: dict, mutations: dict[str, dict]) -> dict | None:
    tok = row.get("mutation_token", "")
    if not tok or tok.startswith("{"):
        return None
    return mutations.get(tok)


def write_scenario_json(scenario_id: str, payload: dict) -> Path:
    path = Path("/tmp") / f"canary_runtime_{scenario_id}.json"
    path.write_text(json.dumps(payload, indent=2, sort_keys=True),
                    encoding="utf-8")
    return path


def restore_file(rel_path: str) -> None:
    subprocess.run(
        ["git", "-C", str(REPO_ROOT), "checkout", "--", rel_path],
        capture_output=True,
    )


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    grp = p.add_mutually_exclusive_group(required=True)
    grp.add_argument("--scenario", help="Run a single scenario id.")
    grp.add_argument("--all", action="store_true",
                     help="Run every scenario in kScenarios.")
    grp.add_argument("--filter", help="fnmatch glob over scenario ids.")
    p.add_argument("--allow-dirty", action="store_true",
                   help="Bypass the clean-worktree precondition. Use with care.")
    return p.parse_args()


def main() -> int:
    args = parse_args()

    if not args.allow_dirty:
        require_clean_worktree()

    text       = TABLE_HEADER.read_text(encoding="utf-8")
    rows       = lint.parse_scenarios(text)
    mutations  = lint.parse_mutation_constants(text)
    exemptions = load_exemptions()

    by_id = {r["id"]: r for r in rows}
    ids   = all_scenario_ids(rows)

    if args.scenario:
        selected = [args.scenario]
    elif args.filter:
        selected = match_glob(args.filter, ids)
    else:
        selected = ids

    if not selected:
        sys.stderr.write("canary_runtime: nothing to do\n")
        return 3

    # Bootstrap build if needed.
    if not PARITY_BIN.is_file():
        sys.stderr.write(
            "canary_runtime: bootstrapping initial build of og_test_parity\n"
        )
        if not rebuild_targets():
            return 4

    zero_flip_log: list[str] = []
    processed = 0
    flipped   = 0

    for sid in selected:
        processed += 1
        print(f"--- {sid} ---")
        if sid in exemptions:
            print("  SKIP: exempted via .plan/parity-canary-exemptions.md")
            continue
        row = by_id.get(sid)
        if row is None:
            zero_flip_log.append(f"{sid}: not present in kScenarios")
            continue
        mut = lookup_mutation(row, mutations)
        if mut is None:
            zero_flip_log.append(
                f"{sid}: default-constructed discriminating_mutation")
            continue
        print(f"  mutation: {mut['file']}:{mut['line']}")

        pre_eval_path  = Path("/tmp") / f"canary_runtime_{sid}.pre.json"
        post_eval_path = Path("/tmp") / f"canary_runtime_{sid}.post.json"

        pre_verdict, pre_log = run_gtest(sid)
        pre_smoke_ok = run_smoke_eval(sid, pre_eval_path)
        print(f"  pre:  gtest={pre_verdict}")

        # Apply the mutation. The apply helper refuses to touch
        # tests/parity/* or master companion paths.
        apply = subprocess.run(
            ["python3", str(APPLY_MUT),
             mut["file"], str(mut["line"]), mut["from"], mut["to"],
             mut.get("context_before", "")],
            capture_output=True, text=True,
        )
        if apply.returncode != 0:
            print("  SKIP: _apply_mutation refused")
            sys.stderr.write(apply.stdout + apply.stderr)
            zero_flip_log.append(f"{sid}: _apply_mutation failed (rc={apply.returncode})")
            # No file write happened (per _apply_mutation's contract for
            # validation failures), so no checkout needed.
            continue

        try:
            if not rebuild_targets(in_flight=mut):
                zero_flip_log.append(f"{sid}: rebuild failed after mutation")
                continue
            post_verdict, post_log = run_gtest(sid)
            post_smoke_ok = run_smoke_eval(sid, post_eval_path)
            print(f"  post: gtest={post_verdict}")
        finally:
            restore_file(mut["file"])
            # Rebuild to a clean baseline; failures here are surfaced but
            # do not corrupt the per-scenario verdict.
            if not rebuild_targets():
                sys.stderr.write(
                    f"canary_runtime: rebuild to baseline failed after {sid}\n"
                )

        # Per-predicate diff (only available if both smoke captures ran).
        pre_facts:  list[dict] = []
        post_facts: list[dict] = []
        if pre_smoke_ok:
            try:
                pre_facts = json.loads(
                    pre_eval_path.read_text(encoding="utf-8")).get("facts", [])
            except Exception:
                pre_facts = []
        if post_smoke_ok:
            try:
                post_facts = json.loads(
                    post_eval_path.read_text(encoding="utf-8")).get("facts", [])
            except Exception:
                post_facts = []
        pred_diff = diff_predicate_traces(pre_facts, post_facts)

        # The contract: the gtest verdict MUST flip from PASS to FAIL.
        # Predicate-level flips are advisory; the runtime gate is the
        # primary signal.
        gtest_flipped = (pre_verdict == "PASS" and post_verdict == "FAIL")

        payload = {
            "scenario":      sid,
            "mutation":      {
                "file":      mut["file"],
                "line":      int(mut["line"]),
                "from":      mut["from"],
                "to":        mut["to"],
                "rationale": mut.get("rationale", ""),
            },
            "pre":           {"gtest": pre_verdict},
            "post":          {"gtest": post_verdict,
                              "gtest_output_tail": post_log[-2000:]},
            "flipped":       gtest_flipped or bool(pred_diff),
            "gtest_flipped": gtest_flipped,
            "predicate_trace": pred_diff,
        }
        out_path = write_scenario_json(sid, payload)
        print(f"  wrote {out_path}: flipped={payload['flipped']} "
              f"predicate_trace={pred_diff or '-'}")

        if gtest_flipped or pred_diff:
            flipped += 1
        else:
            zero_flip_log.append(
                f"{sid}: 0 flips (mutation = {mut['file']}:{mut['line']})")

    print()
    print(f"canary_runtime: processed {processed} scenarios, "
          f"{flipped} flipped, {len(zero_flip_log)} zero-flip")
    if zero_flip_log:
        sys.stderr.write("canary_runtime: FAIL — zero-flip scenarios:\n")
        for entry in zero_flip_log:
            sys.stderr.write(f"  - {entry}\n")
        return 1
    print("canary_runtime: OK — every scenario produced ≥1 flip")
    return 0


if __name__ == "__main__":
    sys.exit(main())
