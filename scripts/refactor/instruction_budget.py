#!/usr/bin/env python3
"""Aggregate and gate the parity harness's per-scenario Lua instruction totals.

The raw input is the file OPENGLAD_LUA_INSTRUCTION_REPORT points og_test_parity
at: one "<scenario-id>\t<total>" line per scenario RUN, where <total> is the
exact cumulative VM instruction count the world-host budget hook observed for
that run (ScriptHost drops to a per-instruction budget-hook cadence under the
same variable — see budget_check_cadence in src/gameplay/script/script_host.cpp
and append_instruction_report in tests/parity/parity_runner.cpp). Scenarios
that run more than once in a suite pass (the Invariant dual-captures, the
smoke-divergence re-runs) appear once per run; the sim is deterministic, so
every run of one scenario must report the same total, and both subcommands
refuse the file when any two disagree rather than average away a
determinism bug.

Subcommands:

  recapture --raw R.tsv --out baseline.json
      Fold the raw runs into {scenario-id: total} and write the baseline
      JSON (sorted keys, stable formatting), stamping the CLEAN tree's HEAD
      SHA inside. Re-capture is a deliberate act with two hard rules, both
      enforced here:
        * the working tree must be CLEAN (`git status --porcelain` empty) —
          a baseline captured on a dirty tree describes a tree no commit
          names, and checking that tree against it is a self-comparison
          that can never fail (the exact dishonest-pass this tool once
          allowed; see the tree_sha "…-dirty-stage45" incident);
        * the result must be committed AS ITS OWN COMMIT, with the
          mechanism and per-scenario movement in the message (the
          adbd62da precedent), before any `check` will accept it.
      The tree SHA is derived from git, never taken as an argument — there
      is deliberately no way to stamp a SHA the tree does not have.

  check --raw R.tsv --baseline baseline.json [--max-regression 0.10]
      First verify the baseline is trustworthy AGAINST GIT (see the
      refusal list below), then aggregate the candidate run the same way
      as recapture and fail (exit 1) when any scenario's total exceeds its
      baseline by more than --max-regression (default 10%, the budget the
      refactor plan holds helper indirection to). Improvements and
      sub-threshold growth pass. A scenario with a zero baseline fails on
      ANY growth (any increase from zero is more than 10%). Scenarios
      present only in the baseline are reported as warnings, not failures
      — scenario presence is og_test_parity's coverage gate's job, not
      this tool's; scenarios present only in the candidate (new
      scenarios) are reported and pass.

Baseline refusals (exit 2, "BASELINE REFUSED (<name>)"): the comparison
base must be a COMMITTED baseline captured on a clean, committed, ancestor
tree — anything else turns the gate into a self-comparison. `check`
refuses when:

  dirty-capture         the stored tree_sha is not a bare 40-hex commit SHA
                        (e.g. carries a "-dirty…" suffix): the capture tree
                        was never a commit, so there is nothing honest to
                        compare against.
  uncommitted-baseline  the baseline file on disk is untracked or differs
                        from the copy committed at HEAD: a freshly
                        (re)captured baseline is exactly this, so a
                        capture-then-check-on-the-same-tree loop can never
                        pass. Commit the re-baseline (its own commit) first.
  unknown-commit        tree_sha parses but names no commit in this
                        repository.
  not-ancestor          tree_sha is a real commit but not an ancestor of
                        HEAD: the baseline belongs to some other line of
                        history and its numbers say nothing about this one.

`recapture` refuses a dirty tree the same way (exit 2,
"RECAPTURE REFUSED (dirty-tree)").

Staged-tree refusals (exit 2, "MEASUREMENT REFUSED (<name>)"): og_test_parity
resolves its assets exe-adjacent (get_asset_path), so the numbers in the raw
report describe the STAGED copies under build/ci-test — not the repo tree the
baseline SHA names. `cmake -E copy_directory` staging historically never
deleted removed files, and one stale staged packs/core/lib/*.lua shifted every
scenario by a constant on a git-clean tree (the zz_probe incident). The build
now mirror-stages (delete+copy, CMakeLists stage_runtime_assets), and both
subcommands independently refuse to trust a report unless the staged
sim-input trees (packs/, builtin/, cfg/ — the exe-adjacent inputs the
headless sim reads; pix/ and sound/ are render-only) are byte-identical to
the repo working tree:

  staged-missing        the staged root (default build/ci-test, override with
                        --staged-root) or one of its sim-input dirs does not
                        exist — whatever produced the raw report, it was not
                        the documented staged binary.
  staged-drift          a staged file is stale (deleted/renamed in the repo
                        but still staged), missing, or differs in bytes from
                        the repo tree. Rebuild (mirror staging heals this)
                        and re-measure.

This attests the staged tree at CHECK time, not at the instant the report
was generated — a report measured earlier against different staging can
still slip through if the tree was rebuilt in between. Like the tree_sha
value provenance, that deliberate-fraud shape stays review's job.

Exit status: 0 clean, 1 gate failure or malformed input, 2 baseline,
recapture, or staged-tree refusal (protocol violation).
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys

SCHEMA = "og-lua-instruction-baseline/1"

REFUSAL_EXIT = 2

# The exe-adjacent staged trees the headless sim reads (get_asset_path
# resolves next to the binary). pix/ and sound/ are staged too but are
# render-only and unread by og_test_parity, so they are not asserted.
STAGED_SIM_DIRS = ("packs", "builtin", "cfg")


class Refusal(SystemExit):
    """A named protocol refusal (exit status 2)."""

    def __init__(self, kind: str, name: str, detail: str):
        super().__init__(REFUSAL_EXIT)
        print(f"instruction-budget: {kind} REFUSED ({name}): {detail}",
              file=sys.stderr)


def git_output(repo: pathlib.Path, *args: str) -> str | None:
    """stdout of `git -C repo args...`, or None on nonzero exit."""
    proc = subprocess.run(["git", "-C", str(repo), *args],
                          capture_output=True)
    if proc.returncode != 0:
        return None
    return proc.stdout.decode(errors="replace")


def read_raw(path: pathlib.Path) -> dict[str, int]:
    """Parse a raw report into {scenario: total}, refusing indeterminism."""
    totals: dict[str, int] = {}
    problems: list[str] = []
    lines = path.read_text().splitlines()
    for lineno, line in enumerate(lines, 1):
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 2 or not parts[1].isdigit():
            problems.append(f"{path}:{lineno}: malformed line: {line!r}")
            continue
        scenario, total = parts[0], int(parts[1])
        if scenario in totals and totals[scenario] != total:
            problems.append(
                f"{path}:{lineno}: scenario '{scenario}' reported {total} "
                f"but an earlier run reported {totals[scenario]} — "
                f"per-scenario instruction counts must be deterministic")
            continue
        totals[scenario] = total
    if not totals:
        problems.append(f"{path}: no scenario totals found — was "
                        "og_test_parity run with "
                        "OPENGLAD_LUA_INSTRUCTION_REPORT set to this path?")
    if problems:
        raise SystemExit("\n".join(problems))
    return totals


def repo_of(path: pathlib.Path) -> pathlib.Path | None:
    """The git work-tree root containing path (a dir), or None."""
    top = git_output(path, "rev-parse", "--show-toplevel")
    if top is None:
        return None
    return pathlib.Path(top.strip())


def tree_files(root: pathlib.Path) -> dict[str, pathlib.Path]:
    """{relative-posix-path: absolute path} for every file under root."""
    return {p.relative_to(root).as_posix(): p
            for p in sorted(root.rglob("*")) if p.is_file()}


def verify_staged_assets(repo: pathlib.Path,
                         staged_root: pathlib.Path | None) -> None:
    """Refuse (exit 2) unless the staged sim-input trees match the repo.

    og_test_parity reads its packs/builtin/cfg exe-adjacent, so the raw
    report describes the STAGED trees, not the repo. A stale staged file
    (the zz_probe incident) skews every scenario while the repo looks
    clean. Mirror staging (CMake) heals drift on every build; this check
    refuses to compare or capture through drift that a build has not yet
    healed. See the module docstring for the refusal names.
    """
    if staged_root is None:
        staged_root = repo / "build" / "ci-test"
    if not staged_root.is_dir():
        raise Refusal(
            "MEASUREMENT", "staged-missing",
            f"staged root {staged_root} does not exist — the raw report "
            "cannot have come from the documented staged binary; build "
            "the ci-test preset (or point --staged-root at the build "
            "directory that produced the report)")
    diffs: list[str] = []
    for name in STAGED_SIM_DIRS:
        src_dir = repo / name
        dst_dir = staged_root / name
        if not src_dir.is_dir():
            diffs.append(f"{name}/: missing from the repo tree itself")
            continue
        if not dst_dir.is_dir():
            raise Refusal(
                "MEASUREMENT", "staged-missing",
                f"{dst_dir} is not staged — build the preset so "
                "stage_runtime_assets mirrors the repo tree first")
        src = tree_files(src_dir)
        dst = tree_files(dst_dir)
        for rel in sorted(set(dst) - set(src)):
            diffs.append(f"{name}/{rel}: staged but absent from the repo "
                         "tree (stale leftover)")
        for rel in sorted(set(src) - set(dst)):
            diffs.append(f"{name}/{rel}: in the repo tree but not staged")
        for rel in sorted(set(src) & set(dst)):
            if src[rel].read_bytes() != dst[rel].read_bytes():
                diffs.append(f"{name}/{rel}: staged bytes differ from the "
                             "repo tree")
    if diffs:
        shown = "\n".join("    " + d for d in diffs[:8])
        more = f"\n    ... and {len(diffs) - 8} more" if len(diffs) > 8 else ""
        raise Refusal(
            "MEASUREMENT", "staged-drift",
            "the staged sim-input trees under "
            f"{staged_root} differ from the repo tree — the raw report "
            "measured different Lua/level/config bytes than the tree "
            "being attested. Rebuild (staging mirrors the repo) and "
            f"re-measure:\n{shown}{more}")


def cmd_recapture(args: argparse.Namespace) -> int:
    out = pathlib.Path(args.out).resolve()
    repo = repo_of(out.parent if out.parent.is_dir() else pathlib.Path.cwd())
    if repo is None:
        raise Refusal("RECAPTURE", "dirty-tree",
                      f"{out} is not inside a git work tree — a baseline "
                      "only means something as a committed file whose "
                      "capture commit git can verify")
    status = git_output(repo, "status", "--porcelain")
    if status is None:
        raise SystemExit(f"git status failed in {repo}")
    if status.strip():
        first = "\n".join("    " + ln
                          for ln in status.strip().splitlines()[:8])
        raise Refusal(
            "RECAPTURE", "dirty-tree",
            "the working tree has uncommitted changes — a baseline "
            "captured here would describe a tree no commit names, and a "
            "check on this same tree would be a self-comparison that can "
            "never fail. Commit or stash first, then re-run. Dirty:\n"
            + first)
    head = git_output(repo, "rev-parse", "HEAD")
    if head is None:
        raise SystemExit(f"git rev-parse HEAD failed in {repo}")
    tree_sha = head.strip()
    verify_staged_assets(
        repo,
        pathlib.Path(args.staged_root) if args.staged_root else None)
    totals = read_raw(pathlib.Path(args.raw))
    doc = {
        "schema": SCHEMA,
        "tree_sha": tree_sha,
        "cadence": "per-instruction",
        "scenarios": dict(sorted(totals.items())),
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(doc, indent=2, sort_keys=False) + "\n")
    print(f"instruction baseline: {len(totals)} scenarios -> {out} "
          f"(clean tree {tree_sha[:12]})")
    print("now COMMIT this file as its own commit (mechanism + "
          "per-scenario movement in the message — the adbd62da "
          "precedent); `check` refuses an uncommitted baseline.")
    return 0


def load_baseline(path: pathlib.Path) -> dict:
    doc = json.loads(path.read_text())
    if doc.get("schema") != SCHEMA:
        raise SystemExit(
            f"{path}: schema {doc.get('schema')!r} is not {SCHEMA!r}; "
            "refusing to compare across formats")
    return doc


def verify_baseline_against_git(path: pathlib.Path, doc: dict) -> None:
    """Refuse (exit 2) unless path is a committed baseline whose capture
    tree is a clean, real, ancestor commit — see the module docstring."""
    tree_sha = str(doc.get("tree_sha", ""))
    if not re.fullmatch(r"[0-9a-f]{40}", tree_sha):
        raise Refusal(
            "BASELINE", "dirty-capture",
            f"{path} stores tree_sha {tree_sha!r}, which is not a bare "
            "40-hex commit SHA — it was captured on a tree no commit "
            "names (a dirty tree), so comparing against it proves "
            "nothing. Re-capture on a clean tree with `recapture` and "
            "commit the result.")
    repo = repo_of(path.parent)
    if repo is None:
        raise Refusal(
            "BASELINE", "uncommitted-baseline",
            f"{path} is not inside a git work tree — the comparison base "
            "must be a committed file git can vouch for")
    rel = path.resolve().relative_to(repo).as_posix()
    committed = subprocess.run(
        ["git", "-C", str(repo), "cat-file", "blob", f"HEAD:{rel}"],
        capture_output=True)
    if committed.returncode != 0:
        raise Refusal(
            "BASELINE", "uncommitted-baseline",
            f"{rel} is not tracked at HEAD — a freshly captured baseline "
            "is exactly this; commit the re-baseline as its own commit "
            "first (the adbd62da precedent)")
    if committed.stdout != path.read_bytes():
        raise Refusal(
            "BASELINE", "uncommitted-baseline",
            f"{rel} on disk differs from the copy committed at HEAD — "
            "an in-place re-capture must be committed (its own commit) "
            "before it can serve as a comparison base; a working-tree "
            "baseline checked against the same working tree is a "
            "self-comparison that can never fail")
    if git_output(repo, "cat-file", "-e", f"{tree_sha}^{{commit}}") is None:
        raise Refusal(
            "BASELINE", "unknown-commit",
            f"{path} claims capture tree {tree_sha[:12]}, which names no "
            "commit in this repository")
    ancestor = subprocess.run(
        ["git", "-C", str(repo), "merge-base", "--is-ancestor", tree_sha,
         "HEAD"], capture_output=True)
    if ancestor.returncode != 0:
        raise Refusal(
            "BASELINE", "not-ancestor",
            f"{path} was captured on {tree_sha[:12]}, which is not an "
            "ancestor of HEAD — its numbers describe some other line of "
            "history")


def cmd_check(args: argparse.Namespace) -> int:
    baseline_path = pathlib.Path(args.baseline)
    doc = load_baseline(baseline_path)
    verify_baseline_against_git(baseline_path, doc)
    repo = repo_of(baseline_path.parent)
    if repo is not None:  # unreachable None: the baseline verify refused
        verify_staged_assets(
            repo,
            pathlib.Path(args.staged_root) if args.staged_root else None)
    base = {str(k): int(v) for k, v in doc["scenarios"].items()}
    cur = read_raw(pathlib.Path(args.raw))

    regressions: list[str] = []
    improvements = 0
    for scenario in sorted(set(base) & set(cur)):
        b, c = base[scenario], cur[scenario]
        limit = b * (1.0 + args.max_regression)
        if c > limit:
            pct = (c - b) / b * 100.0 if b else float("inf")
            regressions.append(
                f"  {scenario}: {b} -> {c} (+{pct:.1f}%, limit "
                f"+{args.max_regression * 100:.0f}%)")
        elif c < b:
            improvements += 1

    only_base = sorted(set(base) - set(cur))
    only_cur = sorted(set(cur) - set(base))
    for scenario in only_base:
        print(f"warning: baseline scenario '{scenario}' missing from this "
              "run (og_test_parity's coverage gate owns scenario presence)")
    for scenario in only_cur:
        print(f"note: scenario '{scenario}' has no baseline yet "
              f"(current total {cur[scenario]})")

    compared = len(set(base) & set(cur))
    print(f"instruction budget: {compared} scenarios compared, "
          f"{improvements} improved, {len(regressions)} regressed beyond "
          f"{args.max_regression * 100:.0f}%")
    if regressions:
        print("instruction-budget REGRESSIONS:")
        print("\n".join(regressions))
        print("(rebaseline deliberately via scripts/refactor/README.md if "
              "the growth is accepted)")
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    rec = sub.add_parser(
        "recapture",
        help="deliberately re-capture the baseline: clean tree required, "
             "HEAD SHA stamped by git, result must be committed as its "
             "own commit")
    rec.add_argument("--raw", required=True,
                     help="raw TSV written by og_test_parity")
    rec.add_argument("--out", required=True, help="baseline JSON to write")
    rec.add_argument("--staged-root", default=None,
                     help="build directory whose staged packs/builtin/cfg "
                          "must mirror the repo tree "
                          "(default: <repo>/build/ci-test)")
    rec.set_defaults(func=cmd_recapture)

    chk = sub.add_parser("check", help="gate a raw report against a "
                                       "committed baseline (git-verified)")
    chk.add_argument("--raw", required=True,
                     help="raw TSV written by og_test_parity")
    chk.add_argument("--baseline", required=True, help="baseline JSON")
    chk.add_argument("--max-regression", type=float, default=0.10,
                     help="max allowed per-scenario growth (default 0.10)")
    chk.add_argument("--staged-root", default=None,
                     help="build directory whose staged packs/builtin/cfg "
                          "must mirror the repo tree "
                          "(default: <repo>/build/ci-test)")
    chk.set_defaults(func=cmd_check)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
