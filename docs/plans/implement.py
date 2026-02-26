#!/usr/bin/env python3
"""
Automated implementation pipeline for OpenGlad component architecture migration.

Uses Codex CLI agents for implementation and multi-role verification.
Each phase goes through:
  1. Implementation by an expert engineer agent
  2. Verification by 5 checker roles (any failure triggers re-implementation)
  3. All checks pass → move to next phase

Usage:
    ./implement.py              # Run all phases from the beginning
    ./implement.py 3            # Start from phase 3
    ./implement.py --resume     # Resume from where the last run was interrupted
    ./implement.py --rewind     # Like --resume but re-run the last worker
    ./implement.py --rewind 3   # Like --resume but back up 3 workers
    ./implement.py --dry-run    # Print prompts without running anything
"""

import subprocess
import sys
import os
import re
import tempfile
import argparse
import textwrap
import time
from datetime import datetime, timedelta
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
PLAN_DIR = REPO_ROOT / "docs" / "plans" / "component-architecture"
LOG_DIR = REPO_ROOT / "docs" / "plans" / "logs"
PHASES = ["1a", "1b", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"]
MAX_RETRIES = 5

CODEX = ["npx", "@openai/codex@latest"]

CHECKER_ROLES = [
    "software tester",
    "senior software tester",
    "senior engineer",
    "software architect",
    "project manager",
]


def now() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def elapsed(start: float) -> str:
    d = timedelta(seconds=int(time.time() - start))
    return str(d)


def log(msg: str, *, indent: int = 0):
    prefix = "  " * indent
    print(f"[{now()}] {prefix}{msg}", flush=True)


def write_log_file(phase_id: str, attempt: int, step: str, output: str):
    """Write agent output to a log file for post-mortem inspection."""
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    safe_step = step.replace(" ", "_")
    filename = LOG_DIR / f"phase-{phase_id}_attempt-{attempt}_{safe_step}_{ts}.log"
    filename.write_text(output)
    log(f"Full output saved to {filename.relative_to(REPO_ROOT)}", indent=2)


def phase_file(phase_id: str) -> Path:
    """Map phase ID ("1a", "2", "10") to its markdown plan file."""
    if phase_id[-1].isalpha():
        num_part = phase_id[:-1]
        letter = phase_id[-1]
        return PLAN_DIR / f"phase-{int(num_part):02d}{letter}.md"
    return PLAN_DIR / f"phase-{int(phase_id):02d}.md"


def run_codex(prompt: str) -> tuple[int, str, str]:
    """
    Run a Codex agent with the given prompt. Returns (exit_code, output, duration).
    """
    fd, output_file = tempfile.mkstemp(suffix=".txt", prefix="codex_output_")
    os.close(fd)

    cmd = list(CODEX) + ["exec", "--dangerously-bypass-approvals-and-sandbox"]
    cmd.extend(["-C", str(REPO_ROOT)])
    cmd.extend(["-o", output_file])
    cmd.append(prompt)

    start = time.time()
    result = subprocess.run(cmd, capture_output=True, text=True)
    returncode = result.returncode
    duration = elapsed(start)

    # Read the agent's last message from the output file
    output = ""
    try:
        with open(output_file) as f:
            output = f.read()
    except FileNotFoundError:
        output = "(no output captured)"
    finally:
        try:
            os.unlink(output_file)
        except FileNotFoundError:
            pass

    return returncode, output, duration


# ---------------------------------------------------------------------------
# Prompts
# ---------------------------------------------------------------------------

IMPLEMENT_PROMPT = textwrap.dedent("""\
    You are an expert software engineer implementing Phase {phase_id} of the
    OpenGlad component architecture migration.

    The detailed phase plan is at: docs/plans/component-architecture/{phase_filename}
    The overall plan overview is at: docs/plans/component-architecture/README.md
    The project guide is at: CLAUDE.md

    Read ALL of these files before writing any code.

    Requirements:
    - Implement every step described in the phase plan
    - Follow existing patterns and conventions in the codebase
    - All tests must pass:
        cmake --preset ci-test && cmake --build -j 6 --preset ci-test && ctest --preset ci-test
    - ALL TESTS MUST PASS! THERE IS NO SUCH THING AS PRE-EXISTING FAILURES, FLAKY TESTS, ETC
    - IF TESTS DON'T PASS, FIX THEM
    - ALL TESTS MUST PASS
    - Do NOT modify any plan files under docs/plans/ — they are read-only references
    - Commit your changes with a clear, descriptive commit message
    - Push to the current branch when done
    {retry_context}
""")

CHECKER_INSTRUCTIONS = {
    "software tester": textwrap.dedent("""\
        Your job is to verify that ALL tests pass both locally and on CI.

        YOU MUST ACTUALLY RUN THE TESTS YOURSELF. Do not skip, assume, or hand-wave.

        The environment is fully configured and working. The `gh` CLI is authenticated
        and works. The build toolchain is installed. There are ZERO pre-existing failures.
        The codebase before any phase changes passes 100% of tests. Therefore:
        - ANY test failure is a real regression caused by the implementation.
        - ANY build error is a real problem.
        - ANY timeout is a real problem (tests that hang = FAIL).
        - There are NO flaky tests. There are NO known issues. There are NO acceptable failures.
        - Do NOT rationalize, excuse, or explain away failures. A failure is a failure.

        Steps:
        1. Run the full test suite locally:
             cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test
           YOU MUST RUN THIS COMMAND AND WAIT FOR IT TO COMPLETE.
           If any test fails, times out, or the build fails: report FAIL immediately.
        2. Check CI status on GitHub using `gh`:
             gh run list --limit 5
             gh run view  (pick the most recent run for this branch)
           If CI is still running, wait for it: gh run watch
           If CI fails: report FAIL immediately.
        3. If ANY test fails locally or on CI for ANY reason whatsoever, report FAIL
           with full details. There are absolutely no allowable causes of failure.
    """),

    "senior software tester": textwrap.dedent("""\
        Your job is to verify test integrity — tests pass on CI and haven't been
        sabotaged or weakened to hide breakage.

        The environment is fully configured and working. The `gh` CLI is authenticated
        and works. Before any phase changes, 100% of tests pass. The environment is
        perfect. There are ZERO pre-existing issues of any kind.

        Steps:
        1. Run the full test suite yourself:
             cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test
           YOU MUST ACTUALLY RUN THIS. Do not skip it. Any failure, timeout, or hang = FAIL.
        2. Check CI status with `gh` (it works, it's authenticated):
             gh run list --limit 5 && gh run view
           If CI is running, wait: gh run watch
           Any CI failure = FAIL. No exceptions. No excuses. No "pre-existing" issues.
        3. Review the git diff for this phase's commits. Look for:
           - Tests that were deleted or had assertions removed/weakened
           - #ifdef TESTING guards that hide production bugs
           - Tests that were skipped or marked expected-fail
           - Mocked-out functionality that should be tested for real
           - Test timeouts being increased to hide hangs
        4. If you find evidence of test sabotage, CI failures, local test failures,
           timeouts, or ANY problem whatsoever, report FAIL. There are absolutely
           no allowable causes of failure.
    """),

    "senior engineer": textwrap.dedent("""\
        Your job is to verify implementation completeness and code quality.

        Steps:
        1. Read the phase plan at docs/plans/component-architecture/{phase_filename}
        2. Check every step listed — was it actually implemented?
        3. Review the code changes (git log + git diff) for:
           - Obvious bugs or logic errors
           - Missing error handling at system boundaries
           - Clean, readable code following project conventions
           - No unnecessary complexity or over-engineering
           - Correct ownership semantics (unique_ptr for owning, refs for borrows)
        4. Verify commit messages are clear and accurate.
        5. If the phase is incomplete or has quality issues, report FAIL.
    """),

    "software architect": textwrap.dedent("""\
        Your job is to verify architectural correctness.

        Steps:
        1. Read the phase plan at docs/plans/component-architecture/{phase_filename}
        2. Read docs/plans/component-architecture/target-architecture.md
        3. Verify the implementation matches the planned design.
        4. Check dependency directions — no new violations of module rules.
           Run: scripts/check_vendor_leaks.sh (if it exists)
        5. Verify no new circular dependencies were introduced.
        6. Check that abstractions are clean and module boundaries respected.
        7. If there are architectural issues, report FAIL.
    """),

    "project manager": textwrap.dedent("""\
        Your job is to verify phase completion and readiness for the next phase.

        Steps:
        1. Read the phase plan at docs/plans/component-architecture/{phase_filename}
        2. Verify ALL deliverables listed in the plan are complete.
        3. Check that forwarding accessors were added/removed as specified.
        4. Verify code compiles and all tests pass.
        5. Check git history is clean (proper commits, no WIP, no merge conflicts).
        6. If the next phase exists, check there are no blockers for starting it.
        7. If anything is incomplete, report FAIL.
    """),
}

CHECKER_PROMPT = textwrap.dedent("""\
    You are a {role} reviewing Phase {phase_id} of the OpenGlad component
    architecture migration.

    The phase plan is at: docs/plans/component-architecture/{phase_filename}
    The project guide is at: CLAUDE.md

    {instructions}

    IMPORTANT: After your review, you MUST end your response with exactly one of:

        VERDICT: PASS
        VERDICT: FAIL: <concise reason>

    Do NOT modify any plan files under docs/plans/ — they are read-only references.
    Be thorough but fair. Only fail for genuine issues, not style nitpicks.
""")


def build_implement_prompt(phase_id: str, previous_failure: str = "") -> str:
    pf = phase_file(phase_id)
    retry_context = ""
    if previous_failure:
        retry_context = (
            "\n    IMPORTANT: A previous attempt failed verification.\n"
            f"    Failure details:\n\n{previous_failure}\n\n"
            "    Fix these issues in your implementation.\n"
        )
    return IMPLEMENT_PROMPT.format(
        phase_id=phase_id,
        phase_filename=pf.name,
        retry_context=retry_context,
    )


def build_checker_prompt(phase_id: str, role: str) -> str:
    pf = phase_file(phase_id)
    instructions = CHECKER_INSTRUCTIONS[role].format(phase_filename=pf.name)
    return CHECKER_PROMPT.format(
        role=role,
        phase_id=phase_id,
        phase_filename=pf.name,
        instructions=instructions,
    )


def parse_verdict(output: str) -> tuple[bool, str]:
    """Parse checker output for VERDICT line. Returns (passed, reason)."""
    for line in reversed(output.splitlines()):
        line = line.strip()
        if line.startswith("VERDICT: PASS"):
            return True, ""
        if line.startswith("VERDICT: FAIL"):
            reason = line.split("VERDICT: FAIL:", 1)[-1].strip() if "FAIL:" in line else "unspecified"
            return False, reason
    return False, "checker did not emit a VERDICT line"


# ---------------------------------------------------------------------------
# Resume support
# ---------------------------------------------------------------------------

def _parse_log_filename(name: str) -> dict | None:
    """Parse a log filename into its components.

    Format: phase-{id}_attempt-{n}_{step}_{YYYYMMDD_HHMMSS}.log
    """
    m = re.match(
        r"phase-(.+?)_attempt-(\d+)_(.+?)_(\d{8}_\d{6})\.log",
        name,
    )
    if not m:
        return None
    return {
        "phase": m.group(1),
        "attempt": int(m.group(2)),
        "step": m.group(3),
        "timestamp": m.group(4),
    }


def find_resume_point() -> dict | None:
    """Scan log directory to find where the last run was interrupted.

    Returns a dict with:
        phase: str           - phase ID to resume from
        attempt: int         - attempt number to resume at
        skip_implement: bool - whether the implement step already completed
        passed_checkers: list[str] - checker roles that already passed this attempt
        previous_failure: str      - failure context from last failed checker (if any)
    Or None if no logs are found / all phases are already done.
    """
    if not LOG_DIR.exists():
        return None

    entries = []
    for path in LOG_DIR.glob("phase-*_attempt-*.log"):
        parsed = _parse_log_filename(path.name)
        if parsed:
            parsed["path"] = path
            entries.append(parsed)

    if not entries:
        return None

    # Sort by timestamp to find the most recent activity
    entries.sort(key=lambda e: e["timestamp"])
    latest = entries[-1]
    phase_id = latest["phase"]
    attempt = latest["attempt"]

    # Collect all entries for this phase and attempt, in chronological order
    phase_entries = [
        e for e in entries
        if e["phase"] == phase_id and e["attempt"] == attempt
    ]
    phase_entries.sort(key=lambda e: e["timestamp"])

    has_implement = any(e["step"] == "implement" for e in phase_entries)

    # Map underscored log step names back to role names with spaces
    step_to_role = {role.replace(" ", "_"): role for role in CHECKER_ROLES}

    passed_checkers = []
    previous_failure = ""

    for entry in phase_entries:
        if entry["step"] == "implement":
            continue
        role = step_to_role.get(entry["step"])
        if not role:
            continue
        content = entry["path"].read_text()
        passed, reason = parse_verdict(content)
        if passed:
            passed_checkers.append(role)
        else:
            # This checker failed — everything after it never ran
            previous_failure = (
                f"{role}: {reason}\n"
                f"Full review (last 3000 chars):\n{content[-3000:]}"
            )
            break

    # If all checkers passed, the phase completed successfully — advance to the next
    if has_implement and len(passed_checkers) == len(CHECKER_ROLES):
        try:
            idx = PHASES.index(phase_id)
        except ValueError:
            return None
        if idx + 1 >= len(PHASES):
            return None  # was the last phase, nothing to resume
        return {
            "phase": PHASES[idx + 1],
            "attempt": 1,
            "skip_implement": False,
            "passed_checkers": [],
            "previous_failure": "",
        }

    # A checker failed — need to re-implement on a fresh attempt
    if previous_failure:
        return {
            "phase": phase_id,
            "attempt": attempt + 1,
            "skip_implement": False,
            "passed_checkers": [],
            "previous_failure": previous_failure,
        }

    # Implementation completed but checkers were interrupted mid-run — resume checking
    if has_implement:
        return {
            "phase": phase_id,
            "attempt": attempt,
            "skip_implement": True,
            "passed_checkers": passed_checkers,
            "previous_failure": "",
        }

    # Implementation itself was interrupted — re-run it
    return {
        "phase": phase_id,
        "attempt": attempt,
        "skip_implement": False,
        "passed_checkers": [],
        "previous_failure": "",
    }


# ---------------------------------------------------------------------------
# Status display
# ---------------------------------------------------------------------------

def show_status():
    """Scan log directory and print a pipeline status summary."""
    if not LOG_DIR.exists():
        print("Pipeline Status")
        print("===============")
        print("\nNo logs found. Pipeline has not been started.")
        return

    # Parse all log entries
    entries = []
    for path in LOG_DIR.glob("phase-*_attempt-*.log"):
        parsed = _parse_log_filename(path.name)
        if parsed:
            parsed["path"] = path
            entries.append(parsed)

    if not entries:
        print("Pipeline Status")
        print("===============")
        print("\nNo logs found. Pipeline has not been started.")
        return

    entries.sort(key=lambda e: e["timestamp"])

    # Group by phase
    phase_data = {}
    for e in entries:
        phase_data.setdefault(e["phase"], []).append(e)

    # Map underscored step names back to role names
    step_to_role = {role.replace(" ", "_"): role for role in CHECKER_ROLES}

    # Determine status per phase
    completed = []
    current = None
    pending = []

    for phase_id in PHASES:
        if phase_id not in phase_data:
            pending.append(phase_id)
            continue

        phase_entries = phase_data[phase_id]
        all_attempts = sorted(set(e["attempt"] for e in phase_entries))

        # Check each attempt (highest first) for full completion
        phase_completed = False
        for att in reversed(all_attempts):
            att_entries = [e for e in phase_entries if e["attempt"] == att]
            att_entries.sort(key=lambda e: e["timestamp"])

            has_impl = any(e["step"] == "implement" for e in att_entries)
            att_passed = []
            for entry in att_entries:
                if entry["step"] == "implement":
                    continue
                role = step_to_role.get(entry["step"])
                if not role:
                    continue
                content = entry["path"].read_text()
                passed, _ = parse_verdict(content)
                if passed:
                    att_passed.append(role)
                else:
                    break

            if has_impl and len(att_passed) == len(CHECKER_ROLES):
                ts_str = att_entries[-1]["timestamp"]
                ts = datetime.strptime(ts_str, "%Y%m%d_%H%M%S").strftime(
                    "%Y-%m-%d %H:%M")
                completed.append((phase_id, att, ts))
                phase_completed = True
                break

        if not phase_completed:
            # Show the most recent attempt as "current"
            max_attempt = all_attempts[-1]
            latest_entries = [e for e in phase_entries if e["attempt"] == max_attempt]
            latest_entries.sort(key=lambda e: e["timestamp"])

            has_implement = any(e["step"] == "implement" for e in latest_entries)
            passed_checkers = []
            failed_checker = None
            for entry in latest_entries:
                if entry["step"] == "implement":
                    continue
                role = step_to_role.get(entry["step"])
                if not role:
                    continue
                content = entry["path"].read_text()
                passed, reason = parse_verdict(content)
                if passed:
                    passed_checkers.append(role)
                else:
                    failed_checker = (role, reason)
                    break

            current = {
                "phase": phase_id,
                "attempt": max_attempt,
                "entries": latest_entries,
                "has_implement": has_implement,
                "passed_checkers": passed_checkers,
                "failed_checker": failed_checker,
            }

    # Print
    print("Pipeline Status")
    print("===============")

    if completed:
        print("\nCompleted:")
        for phase_id, attempts, ts in completed:
            pf = phase_file(phase_id).name
            print(f"  Phase {phase_id} ({pf}): DONE (attempt {attempts}, completed {ts})")

    if current:
        p = current
        print(f"\nCurrent:")
        print(f"  Phase {p['phase']}, attempt {p['attempt']}:")

        # Implement step
        impl_entries = [e for e in p["entries"] if e["step"] == "implement"]
        if impl_entries:
            ts = datetime.strptime(impl_entries[-1]["timestamp"],
                                   "%Y%m%d_%H%M%S").strftime("%Y-%m-%d %H:%M")
            print(f"    {'implement':<22} DONE   {ts}")
        else:
            print(f"    {'implement':<22} -")

        # Checker steps
        for role in CHECKER_ROLES:
            step_name = role.replace(" ", "_")
            role_entries = [e for e in p["entries"]
                           if e["step"] == step_name and e["attempt"] == p["attempt"]]
            if role_entries:
                content = role_entries[-1]["path"].read_text()
                passed, reason = parse_verdict(content)
                ts = datetime.strptime(role_entries[-1]["timestamp"],
                                       "%Y%m%d_%H%M%S").strftime("%Y-%m-%d %H:%M")
                if passed:
                    print(f"    {role:<22} PASS   {ts}")
                else:
                    reason_short = reason[:60] if reason else "unspecified"
                    print(f"    {role:<22} FAIL   \"{reason_short}\"")
            else:
                print(f"    {role:<22} -")

    if pending:
        print(f"\nPending:")
        print(f"  Phase {', '.join(pending)}")

    if not completed and not current:
        print("\nNo activity found.")


# ---------------------------------------------------------------------------
# Main loop
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Run the OpenGlad migration pipeline")
    parser.add_argument("start_phase", nargs="?", default=None,
                        help="Phase to start from (e.g. '3' or '1b')")
    parser.add_argument("--resume", action="store_true",
                        help="Resume from where the last run was interrupted")
    parser.add_argument("--rewind", nargs="?", const=1, type=int, default=None,
                        metavar="N",
                        help="Like --resume but rewind N workers (default 1)")
    parser.add_argument("--status", action="store_true",
                        help="Show pipeline status and exit")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print prompts without executing anything")
    parser.add_argument("--max-retries", type=int, default=MAX_RETRIES,
                        help=f"Max retries per phase (default {MAX_RETRIES})")
    args = parser.parse_args()

    if args.resume and args.start_phase:
        print("ERROR: --resume and start_phase are mutually exclusive")
        sys.exit(1)

    if args.rewind is not None and args.start_phase:
        print("ERROR: --rewind and start_phase are mutually exclusive")
        sys.exit(1)

    if args.rewind is not None and args.resume:
        print("ERROR: --rewind and --resume are mutually exclusive")
        sys.exit(1)

    if args.status and (args.resume or args.rewind is not None
                        or args.dry_run or args.start_phase):
        print("ERROR: --status cannot be combined with other options")
        sys.exit(1)

    if args.status:
        show_status()
        sys.exit(0)

    # --rewind implies --resume behaviour with rollback
    if args.rewind is not None:
        args.resume = True

    # Determine phase list
    phases = list(PHASES)

    # Resume support
    resume_state = None
    if args.resume:
        resume_state = find_resume_point()
        if resume_state is None:
            log("No previous run logs found, starting from the beginning")
        else:
            phase_id = resume_state["phase"]
            if phase_id not in phases:
                log(f"Resume phase '{phase_id}' not in phase list, "
                    f"starting from the beginning")
                resume_state = None
            else:
                # Apply --rewind: back up N workers from the resume point.
                # Workers in order: [implement, checker0, checker1, ..., checker4]
                if args.rewind is not None and args.rewind > 0:
                    # When a checker failed, find_resume_point() already
                    # advanced to the next attempt with a clean slate.
                    # Revert to the *actual* failing attempt so we can
                    # rewind from where work actually stopped.
                    if (resume_state.get("previous_failure")
                            and resume_state["attempt"] > 1):
                        orig_attempt = resume_state["attempt"] - 1
                        resume_state["attempt"] = orig_attempt

                        # Rescan logs for the failing attempt
                        step_to_role = {
                            r.replace(" ", "_"): r for r in CHECKER_ROLES
                        }
                        att_entries = []
                        if LOG_DIR.exists():
                            pat = (f"phase-{resume_state['phase']}"
                                   f"_attempt-{orig_attempt}_*.log")
                            for path in LOG_DIR.glob(pat):
                                parsed = _parse_log_filename(path.name)
                                if parsed:
                                    parsed["path"] = path
                                    att_entries.append(parsed)
                        att_entries.sort(key=lambda e: e["timestamp"])

                        has_impl = any(
                            e["step"] == "implement" for e in att_entries
                        )
                        passed = []
                        had_failure = False
                        for e in att_entries:
                            if e["step"] == "implement":
                                continue
                            role = step_to_role.get(e["step"])
                            if not role:
                                continue
                            content = e["path"].read_text()
                            ok, _ = parse_verdict(content)
                            if ok:
                                passed.append(role)
                            else:
                                had_failure = True
                                break

                        resume_state["skip_implement"] = has_impl
                        resume_state["passed_checkers"] = passed

                        # Count workers that actually ran, including
                        # the failed checker itself
                        ran = (int(has_impl) + len(passed)
                               + int(had_failure))
                        completed = max(0, ran - args.rewind)
                    else:
                        # Normal rewind (interrupted, no failure)
                        completed = 0
                        if resume_state["skip_implement"]:
                            completed += 1
                        completed += len(resume_state["passed_checkers"])
                        completed = max(0, completed - args.rewind)

                    if completed == 0:
                        resume_state["skip_implement"] = False
                        resume_state["passed_checkers"] = []
                    else:
                        resume_state["skip_implement"] = True
                        resume_state["passed_checkers"] = \
                            CHECKER_ROLES[:completed - 1]

                    # Clear previous failure context since we're replaying
                    resume_state["previous_failure"] = ""

                    log(f"Rewound {args.rewind} worker(s) from resume point")

                phases = phases[phases.index(phase_id):]
                parts = []
                if resume_state["skip_implement"]:
                    parts.append("implement")
                if resume_state["passed_checkers"]:
                    parts.append(f"{len(resume_state['passed_checkers'])} checker(s)")
                skip_desc = " + ".join(parts)
                log(f"Resuming: phase {phase_id}, attempt {resume_state['attempt']}"
                    + (f", skipping {skip_desc}" if skip_desc else ""))

    if args.start_phase:
        if args.start_phase not in phases:
            print(f"ERROR: Unknown phase '{args.start_phase}'. "
                  f"Valid: {', '.join(phases)}")
            sys.exit(1)
        phases = phases[phases.index(args.start_phase):]

    # Validate phase files exist
    for p in phases:
        pf = phase_file(p)
        if not pf.exists():
            print(f"ERROR: Phase file not found: {pf}")
            sys.exit(1)

    log("=" * 60)
    log(f"OpenGlad Migration Pipeline")
    log(f"Phases: {', '.join(phases)}")
    log(f"Max retries per phase: {args.max_retries}")
    log(f"Codex command: {' '.join(CODEX)}")
    log(f"Repo root: {REPO_ROOT}")
    if args.dry_run:
        log("MODE: DRY RUN (no agents will be spawned)")
    log("=" * 60)

    pipeline_start = time.time()
    completed_phases = []

    # ---------- Main pipeline ----------
    for phase_idx, phase in enumerate(phases):
        previous_failure = ""
        attempt = 0
        skip_implement = False
        passed_checkers = []
        phase_start = time.time()

        # Apply resume state for the first resumed phase only
        if resume_state and phase == resume_state["phase"]:
            attempt = resume_state["attempt"] - 1  # will be incremented in loop
            skip_implement = resume_state["skip_implement"]
            passed_checkers = list(resume_state["passed_checkers"])
            previous_failure = resume_state.get("previous_failure", "")
            resume_state = None  # only apply once

        log("")
        log("#" * 60)
        log(f"PHASE {phase}  ({phase_idx + 1}/{len(phases)})")
        log(f"Plan file: {phase_file(phase).relative_to(REPO_ROOT)}")
        log("#" * 60)

        while True:
            attempt += 1

            if attempt > args.max_retries:
                log(f"FATAL: Phase {phase} failed after {args.max_retries} attempts "
                    f"({elapsed(phase_start)} elapsed)")
                log(f"Completed phases before failure: {', '.join(completed_phases) or 'none'}")
                log(f"Pipeline total time: {elapsed(pipeline_start)}")
                sys.exit(1)

            log("")
            log(f"--- Phase {phase}, Attempt {attempt}/{args.max_retries} ---")
            if previous_failure:
                log(f"Retrying due to: {previous_failure[:200]}...")

            # --- Step 1: Implement ---
            if skip_implement:
                log("Skipping implement step (resuming from checkers)", indent=1)
                skip_implement = False  # only skip once
            else:
                passed_checkers = []  # new implementation invalidates prior checker results
                log("Spawning implementer agent...", indent=1)
                prompt = build_implement_prompt(phase, previous_failure)
                if args.dry_run:
                    log(f"[DRY RUN] prompt ({len(prompt)} chars): {prompt[:300]}...", indent=1)
                else:
                    rc, output, duration = run_codex(prompt)
                    log(f"Implementer finished: exit={rc}, duration={duration}, "
                        f"output={len(output)} chars", indent=1)
                    write_log_file(phase, attempt, "implement", output)
                    if rc != 0:
                        log(f"Implementer FAILED (exit {rc}), will retry", indent=1)
                        # Show last few lines of output for quick debugging
                        tail = output.strip().splitlines()[-5:]
                        for line in tail:
                            log(f"  | {line}", indent=1)
                        previous_failure = (
                            f"Implementation agent exited with code {rc}.\n"
                            f"Output (last 3000 chars):\n{output[-3000:]}"
                        )
                        continue
                    log("Implementer succeeded", indent=1)

            # --- Step 2: Verify with all checkers ---
            all_passed = True
            for checker_idx, checker in enumerate(CHECKER_ROLES):
                if checker in passed_checkers:
                    log(f"Checker {checker_idx + 1}/{len(CHECKER_ROLES)}: "
                        f"{checker} (already passed, skipping)", indent=1)
                    continue

                log(f"Checker {checker_idx + 1}/{len(CHECKER_ROLES)}: {checker}", indent=1)
                prompt = build_checker_prompt(phase, checker)

                if args.dry_run:
                    log(f"[DRY RUN] prompt ({len(prompt)} chars)", indent=2)
                    continue

                rc, output, duration = run_codex(prompt)
                log(f"Finished: exit={rc}, duration={duration}, "
                    f"output={len(output)} chars", indent=2)
                write_log_file(phase, attempt, checker, output)

                if rc != 0:
                    reason = f"checker process exited with code {rc}"
                    log(f"FAIL: {reason}", indent=2)
                    tail = output.strip().splitlines()[-5:]
                    for line in tail:
                        log(f"  | {line}", indent=2)
                    previous_failure = (
                        f"{checker}: {reason}\n"
                        f"Output (last 3000 chars):\n{output[-3000:]}"
                    )
                    all_passed = False
                    break

                passed, reason = parse_verdict(output)
                if passed:
                    log("PASS", indent=2)
                else:
                    log(f"FAIL: {reason}", indent=2)
                    previous_failure = (
                        f"{checker}: {reason}\n"
                        f"Full review (last 3000 chars):\n{output[-3000:]}"
                    )
                    all_passed = False
                    break

            if all_passed:
                completed_phases.append(phase)
                log("")
                log(f">>> Phase {phase} COMPLETE ({elapsed(phase_start)})")
                log(f"    Progress: {len(completed_phases)}/{len(phases)} phases done")
                log(f"    Pipeline elapsed: {elapsed(pipeline_start)}")
                break

    log("")
    log("=" * 60)
    log(f"ALL {len(phases)} PHASES COMPLETE!")
    log(f"Total time: {elapsed(pipeline_start)}")
    log("=" * 60)


if __name__ == "__main__":
    main()
