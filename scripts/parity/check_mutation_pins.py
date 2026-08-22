#!/usr/bin/env python3
"""Verify every mutation-canary pin still anchors to the line it names.

Each Mutation in tests/parity/scenario_table.h is {file, line, from, to, why}
plus an optional context_before: a line that must sit VERBATIM within
CONTEXT_WINDOW lines above the pinned one, which is how a pin whose from-text
repeats in its file says which occurrence it means.
The canary applies it by replacing `from` on that exact line, so a pin whose
line has drifted — or whose text has changed — silently stops mutating
anything. The canary then reports a clean run and the scenario looks guarded
when it is not. That failure mode is invisible without this check, and it is
easy to cause: any insertion earlier in a pinned file shifts every pin below it.

Run: python3 scripts/parity/check_mutation_pins.py [--fix]
Exit 0 when every pin anchors, 1 when one has drifted, 2 when an in-flight
mutation declaration is malformed. With --fix, a pin whose text moved is
re-pointed — but only when exactly one line in the file can carry it.

ACCEPTANCE MIRRORS THE APPLIER — as code, not as a promise: the window rule
and the routine that evaluates it are IMPORTED from _apply_mutation.py, so
the two cannot drift. A pin is "anchored" iff that applier could apply it at
the pinned line, which means `from` occurs there EXACTLY ONCE and its
context_before, if any, is on the lines above: the applier refuses zero
occurrences (exit 6), refuses two or more as ambiguous (exit 7), and refuses a
missing context (exit 8), so a checker that accepts any of those states
certifies pins that cannot be applied. That is how
kMut_weapon_fire_arrow_emission sat green on an animation-table row for
which its `from` ("8") was ambiguous, while the EntityDef row it describes
went unmutated.

The one exception is a tree the canary itself has mutated. This check is a
build dependency of og_test_parity and the canary rebuilds that target WITH
THE MUTATION APPLIED, so the mutated line must be recognisable or the canary
aborts before it can measure anything. That state is admitted only against a
DECLARATION — the canary exports OPENGLAD_MUTATION_IN_FLIGHT naming the pin
it just applied — and only when the mutated line inverts, through the
declared pin, to EXACTLY the line HEAD holds. Nothing weaker: a substring
match on the `to` text accepts any drift that happens to contain it, which
for a `to` as generic as "return 1;" is most of them.

The acceptance rules and the repair rules are self-tested on every run
(--self-test to run only them). Both are one loose `in` away from certifying
pins with no teeth, and that is indistinguishable from working until a canary
run discovers it.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from _apply_mutation import CONTEXT_WINDOW, context_ok  # noqa: E402

REPO = pathlib.Path(__file__).resolve().parents[2]
TABLE = REPO / "tests" / "parity" / "scenario_table.h"

# The canary exports this before rebuilding a mutated tree; it carries the
# exact pin it applied, as JSON ({file, line, from, to}, or a list of those).
# It is the ONLY way the mutated-state rules below can fire, so a hand-edited
# tree — which declares nothing — is judged by the clean-state rule alone.
IN_FLIGHT_ENV = "OPENGLAD_MUTATION_IN_FLIGHT"

# {"<path>", <line>, "<from>", "<to>", "<why>"[, "<context>"]} — path prefixes
# match the canary's own allow-list (repo-relative source under src/, packs/
# or tools/). Both texts are captured because a pin has two legitimate states:
# the tree is clean (line holds `from`) or the canary has it mutated (line
# holds `to`) — see accepts() below. The rationale is matched but not
# captured: it is there only so the optional sixth field, context_before, can
# be reached past it.
PIN = re.compile(
    r'(\{\s*"((?:src|packs|tools)/[^"]+)"\s*,\s*)(\d+)'
    r'(\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)")'
    r'\s*,\s*"(?:[^"\\]|\\.)*"'
    r'(\s*,\s*"((?:[^"\\]|\\.)*)")?',
    re.S,
)

# Every Mutation initializer in the table, whether or not PIN can read it. A
# pin the regex cannot parse is not a legacy pin, it is an INVISIBLE one: no
# state is checked and nothing says so. check() insists the two counts agree.
MUTATION_HEAD = re.compile(r'constexpr\s+Mutation\s+kMut_\w+\s*=?\s*\{')

# The C++ mirror of _apply_mutation.CONTEXT_WINDOW, read back out of the table
# so the in-suite gate and the applier cannot disagree about how far above the
# pinned line a context anchor may sit.
WINDOW_MIRROR = re.compile(
    r'inline\s+constexpr\s+int\s+kMutationContextWindow\s*=\s*(\d+)\s*;')

# accepts() returns which of these explained the line, or None for "drifted".
CLEAN = "clean-state"
APPLIED = "applied-state"
SIBLING = "sibling-state"
STATES = (CLEAN, APPLIED, SIBLING)


class DeclarationError(Exception):
    """OPENGLAD_MUTATION_IN_FLIGHT does not name a real, single pin."""


def unescape(text: str) -> str:
    return text.encode().decode("unicode_escape")


def applicable(line: str, from_text: str) -> bool:
    """Could _apply_mutation.py replace `from_text` on this line?

    Exactly the applier's precondition, and deliberately no weaker: it exits 6
    when the text is absent and exits 7 when it occurs more than once, so a
    pin is anchored only where the count is one. "Contained somewhere on the
    line" is the loose version of this test, and it is what let a pin sit on
    a line carrying two copies of its `from` — green here, unappliable there.
    """
    return bool(from_text) and line.count(from_text) == 1


def anchored(lines: list[str], line_no: int, from_text: str,
             context: str = "") -> bool:
    """Could the applier apply this pin AT THIS LINE of this file?

    applicable() asks the question of one line in isolation; this asks it of a
    line in its file, which is the only place the context anchor exists. Used
    wherever a candidate home for a pin is being judged — the pinned line
    itself, and the scan for where a drifted pin's text went.
    """
    if not 1 <= line_no <= len(lines):
        return False
    return (applicable(lines[line_no - 1], from_text)
            and context_ok(lines, line_no, context))


def reverse_one(line: str, from_text: str, to_text: str) -> str | None:
    """Return the unique line that `from_text` -> `to_text` turned into `line`.

    None when no such line exists, or when more than one could: the canary's
    applier refuses ambiguous edits, so the inverse insists on the same
    uniqueness in both directions. An empty `to` (a deletion mutation) is not
    invertible and yields None.
    """
    if not from_text or not to_text or to_text == from_text:
        return None
    if line.count(to_text) != 1:
        return None
    clean = line.replace(to_text, from_text, 1)
    # Round-trip: applying the pin to the reconstruction must reproduce
    # exactly the line we were handed, with `from` unambiguous on it.
    if not applicable(clean, from_text):
        return None
    if clean.replace(from_text, to_text, 1) != line:
        return None
    return clean


def accepts(line: str, from_text: str, to_text: str,
            group: tuple[tuple[str, str], ...] = (),
            declared: tuple[str, str] | None = None,
            baseline: str | None = None) -> str | None:
    """Name the legitimate state `line` is in for this pin, or None.

    With NO declaration in force there is exactly one legitimate state:

    CLEAN — the pin applies at this line, i.e. `from` occurs on it exactly
    once (applicable()). This is the state every gate run outside the canary
    is in, and it is the state that means "the pin still has teeth".

    A declaration changes the question. The canary declares the single pin it
    just applied, so for that one (path, line) the tree is known-mutated and
    CLEAN is no longer a state it can be in. Two states are then legitimate:

    APPLIED — the line is the image of the declared pin, and the declared pin
    is this one.

    SIBLING — the line is the image of the declared pin, and this pin is a
    different pin on the same anchor (walker.cpp:1189 carries eight rewrites
    of a single 362.0f multiplier). Without this state the build-dep check
    reds on the mutated tree and the teeth of that whole pin class are
    unprovable in-harness.

    Both take all four of:

      * a declaration is in force for this (path, line) — nothing else may
        claim a mutated state;
      * the declared pin is itself one of the pins on this anchor;
      * the line inverts uniquely through that pin (reverse_one);
      * and the reconstruction IS the line HEAD holds. Not "a line that
        contains this pin's `from`" — the exact clean text, byte for byte.
        The canary refuses to start on a dirty worktree, so HEAD is the clean
        tree, and this is what makes the rule exact-state instead of a
        substring guess. Without a baseline to compare against, no mutated
        state is accepted at all.

    Drift satisfies none of it: an unrecognised constant does not invert, and
    a renamed variable, a wrapped statement, an appended comment or a deleted
    statement all reconstruct to something HEAD does not have on that line.
    """
    if declared is None:
        return CLEAN if applicable(line, from_text) else None
    if declared not in group or baseline is None:
        return None
    clean = reverse_one(line, declared[0], declared[1])
    if clean is None or clean != baseline:
        return None
    # Belt and braces: the recovered line must carry every pin on the anchor
    # unambiguously, which is the same thing check() demands of a clean tree.
    if any(not applicable(clean, sib_from) for sib_from, _ in group):
        return None
    return APPLIED if declared == (from_text, to_text) else SIBLING


def collect_pins(source: str) -> list[tuple[str, int, str, str, str]]:
    """Every pin in the table as (path, line, from, to, context_before)."""
    return [(path, int(line_text), unescape(from_text), unescape(to_text),
             unescape(context_text or ""))
            for _, path, line_text, _, from_text, to_text, _, context_text
            in (m.groups() for m in PIN.finditer(source))]


def collect_groups(source: str) -> dict[tuple[str, int],
                                        tuple[tuple[str, str], ...]]:
    """Pins keyed by (path, line): the shared-anchor groups accepts() needs
    to recognise — and to bound — a declared sibling's mid-mutation state."""
    groups: dict[tuple[str, int], list[tuple[str, str]]] = {}
    for path, line, from_text, to_text, _ in collect_pins(source):
        groups.setdefault((path, line), []).append((from_text, to_text))
    return {key: tuple(value) for key, value in groups.items()}


def collect_contexts(source: str) -> dict[tuple[str, int], tuple[str, ...]]:
    """The distinct context anchors asserted on each (path, line).

    Pins sharing an anchor describe the same line and so normally assert the
    same context; a set is kept rather than one value so that if they ever
    disagree, every one of them is still enforced.
    """
    contexts: dict[tuple[str, int], list[str]] = {}
    for path, line, _, _, context in collect_pins(source):
        seen = contexts.setdefault((path, line), [])
        if context and context not in seen:
            seen.append(context)
    return {key: tuple(value) for key, value in contexts.items()}


def parse_declarations(raw: str | None,
                       pins: list[tuple[str, int, str, str, str]]
                       ) -> dict[tuple[str, int], tuple[str, str]]:
    """Parse OPENGLAD_MUTATION_IN_FLIGHT into {(path, line): (from, to)}.

    Every declaration must be a pin that exists in the table — a declaration
    is permission to recognise ONE known state, never a free-form claim about
    what a line may hold. Two declarations on one anchor are refused: the
    canary applies one mutation at a time, and "two at once" describes no
    legitimate state.

    The declaration stays the four keys it always was. context_before is a
    property of the PIN, checked against the tree on both sides of the
    mutation; it says nothing about which state the line is in, so putting it
    on the wire would only give the canary and this file a fifth thing to
    disagree about. Pins are projected to (file, line, from, to) here.
    """
    if raw is None or not raw.strip():
        return {}
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise DeclarationError(f"{IN_FLIGHT_ENV} is not valid JSON: {exc}")
    entries = parsed if isinstance(parsed, list) else [parsed]
    known = {pin[:4] for pin in pins}
    out: dict[tuple[str, int], tuple[str, str]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise DeclarationError(
                f"{IN_FLIGHT_ENV} entries must be objects, got {entry!r}")
        missing = {"file", "line", "from", "to"} - set(entry)
        if missing:
            raise DeclarationError(
                f"{IN_FLIGHT_ENV} entry is missing {sorted(missing)}")
        try:
            line = int(entry["line"])
        except (TypeError, ValueError):
            raise DeclarationError(
                f"{IN_FLIGHT_ENV} line must be an integer, "
                f"got {entry['line']!r}")
        path = str(entry["file"])
        record = (path, line, str(entry["from"]), str(entry["to"]))
        if record not in known:
            raise DeclarationError(
                f"{IN_FLIGHT_ENV} declares a mutation that is not in the pin "
                f"table: {path}:{line}\n"
                f"    from: {record[2][:70]!r}\n"
                f"    to:   {record[3][:70]!r}")
        if (path, line) in out:
            raise DeclarationError(
                f"{IN_FLIGHT_ENV} declares two mutations on {path}:{line}; "
                "the canary applies one at a time")
        out[(path, line)] = (record[2], record[3])
    return out


def describe(from_text: str, to_text: str, width: int = 44) -> str:
    """`from -> to` with the text they share up front trimmed away.

    Pin texts on a shared anchor are near-identical whole statements, so a
    plain truncation prints the same forty characters twice and says nothing
    about which mutation is in force. Dropping the common prefix puts the
    difference first, which is what makes the declaration line worth logging.
    """
    head = 0
    while (head < len(from_text) and head < len(to_text)
           and from_text[head] == to_text[head]):
        head += 1
    lead = "..." if head else ""
    left = from_text[head:head + width] or "(nothing)"
    right = to_text[head:head + width] or "(nothing)"
    return f"{lead}{left!r} -> {lead}{right!r}"


def git_baseline_lookup(repo: pathlib.Path):
    """Return a `path -> HEAD lines | None` reader backed by `git show`.

    The clean text of a pinned file, which is what a declared mutated state is
    checked against. The canary refuses to start on a dirty worktree, so for
    every file it mutates HEAD and the pre-mutation worktree are the same
    bytes. Anything git cannot answer yields None, and accepts() then refuses
    the mutated state rather than guessing — fail closed, because the only
    caller that ever declares one is the canary, which always has git, and a
    canary that aborts is recoverable where one that measures nothing is not.

    $OG_GIT_EXECUTABLE takes precedence over PATH, the same handoff CMake
    plumbs into every Python check target (see lua_inventory.git_executable):
    a native interpreter spawned mid-build cannot always resolve a bare
    "git" from a POSIX-style PATH.
    """
    git = os.environ.get("OG_GIT_EXECUTABLE") or "git"
    cache: dict[str, list[str] | None] = {}

    def lookup(path: str) -> list[str] | None:
        if path not in cache:
            try:
                proc = subprocess.run(
                    [git, "-C", str(repo), "show", f"HEAD:{path}"],
                    capture_output=True, text=True)
                cache[path] = (proc.stdout.splitlines()
                               if proc.returncode == 0 else None)
            except OSError:
                cache[path] = None
        return cache[path]

    return lookup


def check(fix: bool = False, repo: pathlib.Path = REPO,
          table: pathlib.Path | None = None,
          declarations: dict[tuple[str, int], tuple[str, str]] | None = None,
          baseline_lookup=None) -> int:
    if table is None:
        table = repo / "tests" / "parity" / "scenario_table.h"
    if baseline_lookup is None:
        baseline_lookup = git_baseline_lookup(repo)
    source = table.read_text()
    groups = collect_groups(source)
    declarations = declarations or {}
    problems: list[str] = []
    repaired: list[str] = []
    tally = {state: 0 for state in STATES}
    total = 0
    file_lines: dict[str, list[str] | None] = {}

    def lines_of(path: str) -> list[str] | None:
        if path not in file_lines:
            target = repo / path
            file_lines[path] = (target.read_text().splitlines()
                                if target.exists() else None)
        return file_lines[path]

    def baseline_at(path: str, line: int) -> str | None:
        head = baseline_lookup(path)
        if head is None or not 1 <= line <= len(head):
            return None
        return head[line - 1]

    for (path, line), declared in sorted(declarations.items()):
        print(f"mutation pins: in-flight mutation declared at {path}:{line} "
              f"({describe(declared[0], declared[1])})")

    def visit(match: re.Match) -> str:
        nonlocal total
        total += 1
        (head, path, line_text, _tail, from_text, to_text,
         _, context_text) = match.groups()
        line = int(line_text)
        wanted = unescape(from_text)
        mutated = unescape(to_text)
        context = unescape(context_text or "")

        lines = lines_of(path)
        if lines is None:
            problems.append(f"{path}:{line} — file does not exist")
            return match.group(0)

        declared = declarations.get((path, line))
        baseline = baseline_at(path, line) if declared else None
        here = lines[line - 1] if 1 <= line <= len(lines) else None
        state = None
        if here is not None:
            state = accepts(here, wanted, mutated,
                            groups.get((path, line), ()), declared, baseline)
        # A mutation rewrites the pinned line and nothing above it, so the
        # context anchor is asserted in every state, mid-canary included.
        if state is not None and not context_ok(lines, line, context):
            problems.append(
                f"{path}:{line} — the pinned line carries the anchor text, "
                f"but the context line is not in the {CONTEXT_WINDOW} lines "
                f"above it, so the applier would refuse it (exit 8); the "
                f"block this pin names has moved or been re-indented\n"
                f"    expected above: {context[:70]}")
            return match.group(0)
        if state is not None:
            tally[state] += 1
            return match.group(0)

        # A declared anchor is mid-mutation: the canary wrote that line from
        # HEAD moments ago. Re-pointing it would move the pin onto whatever
        # the mutated tree happens to look like, so report and stop.
        if declared is not None:
            if baseline is None:
                problems.append(
                    f"{path}:{line} — a mutation is declared here but HEAD's "
                    f"copy of the line could not be read, so the mutated "
                    f"state cannot be verified (not a git checkout? set "
                    f"$OG_GIT_EXECUTABLE if git is not on PATH)")
            else:
                problems.append(
                    f"{path}:{line} — declared mid-mutation state is not the "
                    f"image of {describe(declared[0], declared[1])} over "
                    f"HEAD's line\n    HEAD: {baseline[:70]}"
                    f"\n    tree: {(here or '')[:70]}")
            return match.group(0)

        # How many occurrences the pinned line actually has decides the
        # message: "two of them" is a different bug from "none of them", and
        # only the first one used to pass.
        occurrences = here.count(wanted) if here is not None else 0
        if occurrences > 1:
            problems.append(
                f"{path}:{line} — anchor text occurs {occurrences}x on the "
                f"pinned line, so the canary's applier refuses it as "
                f"ambiguous (exit 7); this pin cannot fire\n"
                f"    wanted: {wanted[:70]}")
            return match.group(0)

        hits = [i + 1 for i in range(len(lines))
                if anchored(lines, i + 1, wanted, context)]
        if not hits:
            # With a context in force, "nowhere" has two flavours, and telling
            # them apart is the difference between "this code is gone" and
            # "the pin is looking at the wrong copy of it".
            loose = [i + 1 for i, text in enumerate(lines)
                     if applicable(text, wanted)]
            if context and loose:
                problems.append(
                    f"{path}:{line} — anchor text is at "
                    f"{', '.join(str(h) for h in loose[:8])}"
                    f"{', ...' if len(loose) > 8 else ''}, but none of those "
                    f"lines has the context line above it\n"
                    f"    wanted:         {wanted[:70]}\n"
                    f"    expected above: {context[:70]}")
            else:
                problems.append(
                    f"{path}:{line} — anchor text no longer present anywhere\n"
                    f"    wanted: {wanted[:70]}")
            return match.group(0)
        if len(hits) > 1:
            # Auto-repair picks the NEAREST match, which for a short or
            # generic `from` is an accident of layout, not the pin's meaning.
            # Refuse, name the candidates, and make a human choose the anchor.
            problems.append(
                f"{path}:{line} — drifted, and {len(hits)} lines could carry "
                f"this text ({', '.join(str(h) for h in hits[:8])}"
                f"{', ...' if len(hits) > 8 else ''}); too generic to "
                f"re-point automatically — pick the anchor by hand\n"
                f"    wanted: {wanted[:70]}")
            return match.group(0)

        nearest = hits[0]
        if not fix:
            problems.append(
                f"{path}:{line} — drifted, text now at line {nearest}\n"
                f"    wanted: {wanted[:70]}")
            return match.group(0)

        repaired.append(f"{path}: {line} -> {nearest}")
        # The pin anchors cleanly at its new line, and the summary has to say
        # so: a breakdown whose parts do not sum to the total reads like a
        # counting bug and teaches nobody anything.
        tally[CLEAN] += 1
        # Only the line number is rewritten; everything the match swallowed
        # after it — the rationale, and the context field beyond it — is
        # copied back verbatim.
        rest = match.group(0)[len(head) + len(line_text):]
        return head + str(nearest) + rest

    updated = PIN.sub(visit, source)

    # A pin the regex cannot read is checked by nothing at all, and looks
    # exactly like a table with fewer pins in it. Count the initializers
    # independently and insist the two agree, so a punctuation change that
    # slips one past PIN is a hard error instead of a silent exemption.
    heads = len(MUTATION_HEAD.findall(source))
    if heads != total:
        problems.append(
            f"{table}: {heads} Mutation initializer(s) in the table but "
            f"{total} matched the pin pattern; {abs(heads - total)} pin(s) "
            f"are being checked by nothing. Fix the initializer's spelling "
            f"or PIN in this file — do not leave it unread.")

    # The window rule lives in _apply_mutation.py; the C++ gate compiled into
    # og_test_parity re-states it as a number. Two copies, one meaning.
    mirror = WINDOW_MIRROR.search(source)
    if mirror is not None and int(mirror.group(1)) != CONTEXT_WINDOW:
        problems.append(
            f"{table}: kMutationContextWindow is {mirror.group(1)} but "
            f"_apply_mutation.CONTEXT_WINDOW is {CONTEXT_WINDOW}; the "
            f"in-suite gate and the applier would judge context anchors by "
            f"different rules")

    # Every pin's MUTATED state must be recognisable too. The canary rebuilds
    # og_test_parity with the mutation in the tree, and this check gates that
    # build: a pin whose applied state this file cannot name would abort the
    # canary instead of measuring it. Verified here on the clean anchors, so
    # the failure is a red pin-check rather than a mid-run canary crash.
    # Over the POST-repair table: with --fix off the two are the same text,
    # and with it on the re-pointed anchors are the ones that have to hold up.
    updated_contexts = collect_contexts(updated)
    for (path, line), group in sorted(collect_groups(updated).items()):
        if (path, line) in declarations:
            continue  # mid-mutation; the per-pin pass already judged it
        lines = lines_of(path)
        if lines is None or not 1 <= line <= len(lines):
            continue
        clean = lines[line - 1]
        if any(not applicable(clean, sib_from) for sib_from, _ in group):
            continue  # not a clean anchor; already reported above
        if any(not context_ok(lines, line, ctx)
               for ctx in updated_contexts.get((path, line), ())):
            continue  # context missing; already reported above
        for pin in group:
            mutated_line = clean.replace(pin[0], pin[1], 1)
            unnamed = [sib for sib in group
                       if accepts(mutated_line, sib[0], sib[1], group, pin,
                                  clean) is None]
            if unnamed:
                problems.append(
                    f"{path}:{line} — applying {describe(pin[0], pin[1])} "
                    f"produces a line this check cannot name for "
                    f"{len(unnamed)} of {len(group)} pin(s) on the anchor; "
                    f"the canary would abort on the mutated rebuild")

    if fix and repaired:
        table.write_text(updated)
        print(f"repaired {len(repaired)} pin(s):")
        for entry in repaired:
            print("  ", entry)

    if problems:
        print(f"{len(problems)} of {total} mutation pins are broken:",
              file=sys.stderr)
        for entry in problems:
            print("  -", entry, file=sys.stderr)
        print("\nRe-run with --fix to re-point pins whose text moved to one"
              "\nunambiguous line, then re-read the diff: a pin whose"
              "\nsurrounding code changed meaning needs a new anchor, not just"
              "\na new line number, and one whose text is too generic to place"
              "\nneeds a longer `from` that names the row it mutates, or a"
              "\ncontext_before naming the line above the occurrence it means."
              f"\n(Mid-canary? The driver must export {IN_FLIGHT_ENV} naming"
              "\nthe applied pin; a mutated line is recognised only against"
              "\nthat declaration and HEAD's copy of the line.)",
              file=sys.stderr)
        return 1

    breakdown = ", ".join(f"{tally[state]} {state}" for state in STATES)
    print(f"mutation pins: {total} anchors valid ({breakdown})")
    return 0


# --- self-tests -------------------------------------------------------------

_STMT = "weapon->set_stepsize((weapon->stepsize() * {}f) / 256.0f);"
_INDENT = "\t\t\t\t"
_CLEAN_LINE = _INDENT + _STMT.format("362.0")

# A miniature of the real walker.cpp:1189 group: five pins over one line,
# overlapping texts of three different widths, two of them producing the
# same mutated line.
_P_FULL512 = (_STMT.format("362.0"), _STMT.format("512.0"))
_P_FULL181 = (_STMT.format("362.0"), _STMT.format("181.0"))
_P_INNER = ("(weapon->stepsize() * 362.0f)", "(weapon->stepsize() * 256.0f)")
_P_LIT = ("362.0f", "181.0f")
_P_INDENT = (_CLEAN_LINE, _INDENT + _STMT.format("181.0"))
_GROUP = (_P_FULL512, _P_FULL181, _P_INNER, _P_LIT, _P_INDENT)
_NOT_A_PIN = (_STMT.format("362.0"), _STMT.format("999.0"))
# A pin whose texts are short and unremarkable — the shape that made an
# undeclared "`to` is somewhere on the line" test worthless.
_P_GENERIC = ("return 1;", "return 0;")


def _applied(pin: tuple[str, str]) -> str:
    return _CLEAN_LINE.replace(pin[0], pin[1], 1)


def _acceptance_cases() -> list[tuple[str, str, tuple[str, str],
                                      tuple[str, str] | None,
                                      str | None, str | None]]:
    """(name, line, pin, declaration-in-force, HEAD baseline, expected)."""
    return [
        # --- clean state: the pin applies here, nothing declared ---
        ("clean/full", _CLEAN_LINE, _P_FULL512, None, None, CLEAN),
        ("clean/literal", _CLEAN_LINE, _P_LIT, None, None, CLEAN),
        ("clean/indent", _CLEAN_LINE, _P_INDENT, None, None, CLEAN),
        # A SECOND occurrence is not a clean state: _apply_mutation exits 7 on
        # it, so the pin cannot fire and the checker must say so. This is the
        # exact shape of the dead gloader.cpp:408 pin ("8" twice on the line).
        ("clean/ambiguous-literal", _CLEAN_LINE + "  // was 362.0f", _P_LIT,
         None, None, None),
        ("clean/ambiguous-inner",
         _CLEAN_LINE + "  // (weapon->stepsize() * 362.0f)", _P_INNER,
         None, None, None),
        ("clean/absent", _INDENT + "// nothing here", _P_LIT, None, None,
         None),
        # Declared means the canary mutated this anchor; a still-clean line
        # is then an anomaly, not a state to certify.
        ("clean/under-declaration", _CLEAN_LINE, _P_LIT, _P_FULL512,
         _CLEAN_LINE, None),
        # --- applied state: own mutation, only against its own declaration --
        ("applied/declared", _applied(_P_FULL512), _P_FULL512, _P_FULL512,
         _CLEAN_LINE, APPLIED),
        ("applied/declared-inner", _applied(_P_INNER), _P_INNER, _P_INNER,
         _CLEAN_LINE, APPLIED),
        ("applied/declared-literal", _applied(_P_LIT), _P_LIT, _P_LIT,
         _CLEAN_LINE, APPLIED),
        # THE REGRESSION THIS FILE EXISTS FOR (1/2): the same lines with
        # nothing declared are drift. "`to` appears on the line" is a test a
        # ripped-out statement passes by accident.
        ("applied/undeclared", _applied(_P_FULL512), _P_FULL512, None, None,
         None),
        ("applied/undeclared-inner", _applied(_P_INNER), _P_INNER, None,
         None, None),
        ("applied/generic-to-text-in-junk",
         "\treturn 0;  // team logic ripped out entirely", _P_GENERIC,
         None, None, None),
        # --- sibling states: only against the matching declaration ---
        ("sibling/512-declared", _applied(_P_FULL512), _P_FULL181,
         _P_FULL512, _CLEAN_LINE, SIBLING),
        ("sibling/512-declared-literal-pin", _applied(_P_FULL512), _P_LIT,
         _P_FULL512, _CLEAN_LINE, SIBLING),
        ("sibling/512-declared-indent-pin", _applied(_P_FULL512), _P_INDENT,
         _P_FULL512, _CLEAN_LINE, SIBLING),
        ("sibling/inner-declared", _applied(_P_INNER), _P_FULL512,
         _P_INNER, _CLEAN_LINE, SIBLING),
        ("sibling/literal-declared", _applied(_P_LIT), _P_FULL512,
         _P_LIT, _CLEAN_LINE, SIBLING),
        ("sibling/indent-declared", _applied(_P_INDENT), _P_FULL512,
         _P_INDENT, _CLEAN_LINE, SIBLING),
        # A state nobody claims to have produced is drift, however plausibly
        # it round-trips.
        ("sibling/undeclared", _applied(_P_FULL512), _P_FULL181, None, None,
         None),
        ("sibling/undeclared-literal-pin", _applied(_P_FULL512), _P_LIT,
         None, None, None),
        ("sibling/wrong-declaration", _applied(_P_FULL512), _P_FULL181,
         _P_INNER, _CLEAN_LINE, None),
        ("sibling/declaration-not-in-group", _applied(_P_FULL512), _P_FULL181,
         _NOT_A_PIN, _CLEAN_LINE, None),
        # THE REGRESSION THIS FILE EXISTS FOR (2/2): without HEAD's line to
        # compare against, or against a DIFFERENT line than HEAD's, the
        # inversion is a guess. Trailing junk survives a substring inversion
        # and dies here.
        ("sibling/no-baseline", _applied(_P_FULL512), _P_FULL181, _P_FULL512,
         None, None),
        ("applied/baseline-mismatch", _applied(_P_FULL512) + "  // junk",
         _P_FULL512, _P_FULL512, _CLEAN_LINE, None),
        ("sibling/baseline-mismatch", _applied(_P_FULL512) + "  // junk",
         _P_FULL181, _P_FULL512, _CLEAN_LINE, None),
        # --- drift shapes: every one reds, declaration or not ---
        ("drift/unknown-constant", _INDENT + _STMT.format("999.0"), _P_LIT,
         _P_FULL512, _CLEAN_LINE, None),
        ("drift/unknown-divisor",
         _INDENT + "weapon->set_stepsize((weapon->stepsize() * 512.0f) "
                   "/ 512.0f);", _P_FULL181, _P_FULL512, _CLEAN_LINE, None),
        ("drift/renamed-variable",
         _INDENT + "wpn->set_stepsize((wpn->stepsize() * 512.0f) / 256.0f);",
         _P_FULL181, _P_FULL512, _CLEAN_LINE, None),
        ("drift/wrapped-statement",
         _INDENT + "if (ok) " + _STMT.format("512.0"), _P_FULL181,
         _P_FULL512, _CLEAN_LINE, None),
        ("drift/deleted-statement", _INDENT + "// stepsize scaling removed",
         _P_FULL181, _P_FULL512, _CLEAN_LINE, None),
        ("drift/blank", "", _P_FULL181, _P_FULL512, _CLEAN_LINE, None),
        ("drift/reindented", "\t\t" + _STMT.format("512.0"), _P_INDENT,
         _P_FULL512, _CLEAN_LINE, None),
        # --- inversion edge cases ---
        ("edge/ambiguous-inversion", _applied(_P_LIT) + " // 181.0f",
         _P_FULL512, _P_LIT, _CLEAN_LINE, None),
        ("edge/empty-to-clean", _CLEAN_LINE, (_STMT.format("362.0"), ""),
         None, None, CLEAN),
        ("edge/empty-to-drift", _INDENT + "// gone",
         (_STMT.format("362.0"), ""), _P_FULL512, _CLEAN_LINE, None),
        ("edge/identity-declaration", _applied(_P_FULL512), _P_FULL181,
         (_P_FULL512[0], _P_FULL512[0]), _CLEAN_LINE, None),
    ]


def _self_test_acceptance() -> list[str]:
    failures = []
    for name, line, pin, declared, baseline, expected in _acceptance_cases():
        got = accepts(line, pin[0], pin[1], _GROUP, declared, baseline)
        if got != expected:
            failures.append(
                f"acceptance/{name}: expected {expected}, got {got}")
    return failures


def _describe_cases() -> list[tuple[str, str, str, str]]:
    """(name, from, to, expected rendering)."""
    return [
        ("shared-prefix", _P_FULL512[0], _P_FULL512[1],
         "...'362.0f) / 256.0f);' -> ...'512.0f) / 256.0f);'"),
        ("no-shared-prefix", "abc", "xyz", "'abc' -> 'xyz'"),
        # An empty side shares no prefix, so nothing is elided from either.
        ("deletion", "drop_me();", "", "'drop_me();' -> '(nothing)'"),
        ("insertion", "", "added();", "'(nothing)' -> 'added();'"),
        # Cannot be a real pin (accepts() would call it CLEAN before anything
        # else); pinned here so the elision arithmetic has no ragged edge.
        ("identical", "same", "same", "...'(nothing)' -> ...'(nothing)'"),
    ]


def _self_test_describe() -> list[str]:
    return [f"describe/{name}: expected {want!r}, got {got!r}"
            for name, src, dst, want in _describe_cases()
            if (got := describe(src, dst)) != want]


_E2E_ASSERTIONS = 14


def _self_test_end_to_end() -> list[str]:
    """check() over a synthetic table and tree: the states, exits and repairs.

    Everything here is hermetic — the HEAD baseline is injected rather than
    read from git — so the rules are exercised identically wherever this runs.
    """
    failures = []
    stmt = "    value = base * 362.0f;"
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "src").mkdir()
        src = root / "src" / "fake.cpp"
        other = root / "src" / "other.cpp"
        other.write_text("int untouched = 1;\n")
        table_text = (
            'constexpr Mutation kMut_a{"src/fake.cpp", 2, '
            '"base * 362.0f", "base * 512.0f", "why"};\n'
            'constexpr Mutation kMut_b{"src/fake.cpp", 2, '
            '"base * 362.0f", "base * 181.0f", "why"};\n'
            'constexpr Mutation kMut_c{"src/other.cpp", 1, '
            '"int untouched = 1;", "int untouched = 2;", "why"};\n')
        table = root / "table.h"
        table.write_text(table_text)
        pin_a = ("base * 362.0f", "base * 512.0f")
        pin_b = ("base * 362.0f", "base * 181.0f")
        pins = collect_pins(table_text)
        decl_a = json.dumps({"file": "src/fake.cpp", "line": 2,
                             "from": pin_a[0], "to": pin_a[1]})
        # What HEAD holds: the clean two-line fake.cpp, always.
        head = {"src/fake.cpp": ["int header;", stmt],
                "src/other.cpp": ["int untouched = 1;"]}

        def run(body: str, raw_decl: str | None = None, fix: bool = False,
                lookup=None) -> tuple[int, str, str]:
            table.write_text(table_text)
            src.write_text("int header;\n" + body + "\n")
            out, err = io.StringIO(), io.StringIO()
            try:
                declarations = parse_declarations(raw_decl, pins)
            except DeclarationError as exc:
                return 2, "", str(exc)
            with contextlib.redirect_stdout(out), \
                    contextlib.redirect_stderr(err):
                rc = check(fix, root, table, declarations,
                           lookup if lookup is not None else head.get)
            return rc, out.getvalue(), err.getvalue()

        rc, out, err = run(stmt)
        if rc != 0 or "3 anchors valid (3 clean-state" not in out:
            failures.append(f"e2e/clean: rc={rc} out={out.strip()!r}")

        # A mutated tree with nothing declared: BOTH pins on the anchor red.
        # Before the exact-state rules the applied one was silently green.
        mutated = stmt.replace(pin_a[0], pin_a[1], 1)
        rc, out, err = run(mutated)
        if rc != 1 or "2 of 3 mutation pins are broken" not in err:
            failures.append(
                f"e2e/undeclared-mutation: rc={rc} err={err.strip()!r}")

        rc, out, err = run(mutated, decl_a)
        if rc != 0 or "1 applied-state" not in out \
                or "1 sibling-state" not in out:
            failures.append(
                f"e2e/declared-sibling: rc={rc} out={out.strip()!r}")

        # Declared, invertible, and NOT the line HEAD holds.
        rc, out, err = run(mutated + "  // junk", decl_a)
        if rc != 1 or "2 of 3 mutation pins are broken" not in err \
                or "HEAD" not in err:
            failures.append(
                f"e2e/baseline-mismatch: rc={rc} err={err.strip()!r}")

        # No HEAD to compare against: refuse the mutated state and say why,
        # rather than falling back to the substring guess this replaced.
        rc, out, err = run(mutated, decl_a, lookup=lambda path: None)
        if rc != 1 or "could not be read" not in err:
            failures.append(
                f"e2e/no-baseline: rc={rc} err={err.strip()!r}")

        rc, out, err = run("    value = base * 999.0f;", decl_a)
        if rc != 1 or "2 of 3 mutation pins are broken" not in err:
            failures.append(
                f"e2e/declared-drift: rc={rc} err={err.strip()!r}")

        # Two occurrences on the pinned line: _apply_mutation exits 7, so the
        # pin cannot fire — and the message has to say which bug this is.
        rc, out, err = run(stmt + "  // base * 362.0f")
        if rc != 1 or "occurs 2x on the pinned line" not in err:
            failures.append(f"e2e/ambiguous-line: rc={rc} err={err.strip()!r}")

        # Drift with ONE possible home: --fix re-points it.
        moved = "    value = other;\n    value = base * 362.0f;"
        rc, out, err = run(moved)
        if rc != 1 or "text now at line 3" not in err:
            failures.append(f"e2e/drift-report: rc={rc} err={err.strip()!r}")
        rc, out, err = run(moved, fix=True)
        if rc != 0 or "repaired 2 pin(s)" not in out:
            failures.append(f"e2e/fix-unique: rc={rc} out={out.strip()!r}")
        elif '"src/fake.cpp", 3,' not in table.read_text():
            failures.append("e2e/fix-unique: table was not re-pointed to 3")

        # Drift with SEVERAL possible homes: --fix must refuse. Picking the
        # nearest is how a generic `from` gets re-pointed at a decoy and
        # certified green forever after.
        ambiguous = ("    value = other;\n    int b = base * 362.0f;\n"
                     "    int c = base * 362.0f;")
        rc, out, err = run(ambiguous, fix=True)
        if rc != 1 or "too generic to re-point automatically" not in err:
            failures.append(f"e2e/fix-ambiguous: rc={rc} err={err.strip()!r}")
        elif '"src/fake.cpp", 2,' not in table.read_text():
            failures.append("e2e/fix-ambiguous: table was rewritten anyway")

        rc, out, err = run(stmt, json.dumps(
            {"file": "src/fake.cpp", "line": 2,
             "from": pin_a[0], "to": "base * 777.0f"}))
        if rc != 2 or "not in the pin table" not in err:
            failures.append(
                f"e2e/unknown-declaration: rc={rc} err={err.strip()!r}")

        rc, out, err = run(stmt, json.dumps([
            {"file": "src/fake.cpp", "line": 2,
             "from": pin_a[0], "to": pin_a[1]},
            {"file": "src/fake.cpp", "line": 2,
             "from": pin_b[0], "to": pin_b[1]}]))
        if rc != 2 or "two mutations" not in err:
            failures.append(
                f"e2e/double-declaration: rc={rc} err={err.strip()!r}")

        rc, out, err = run(stmt, "{not json")
        if rc != 2 or "not valid JSON" not in err:
            failures.append(f"e2e/bad-json: rc={rc} err={err.strip()!r}")

    # A pin that applies cleanly but whose MUTATED line cannot be named: the
    # canary would rebuild into a red pin check and abort mid-run.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "src").mkdir()
        (root / "src" / "loop.cpp").write_text("int header;\nint a = 1;\n")
        table = root / "table.h"
        # "1" -> "a" turns `int a = 1;` into `int a = a;`, which holds two
        # copies of the `to` text and so inverts to nothing.
        table.write_text('constexpr Mutation kMut_x{"src/loop.cpp", 2, '
                         '"1", "a", "why"};\n')
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = check(False, root, table, {},
                       {"src/loop.cpp": ["int header;", "int a = 1;"]}.get)
        if rc != 1 or "the canary would abort" not in err.getvalue():
            failures.append(
                f"e2e/unrecognisable-mutation: rc={rc} "
                f"err={err.getvalue().strip()!r}")
    return failures


_CONTEXT_ASSERTIONS = 9


def _self_test_context() -> list[str]:
    """The context anchor, over a file whose `from` text has a twin.

    Two identical `return 1;` bodies in one switch — weap.cpp carries five —
    is the shape the anchor exists for: the pinned line alone cannot say which
    arm it means, so nothing notices when a mechanical repin lands on the
    other one.
    """
    failures = []
    body = ("switch (act)\n"
            "{\n"
            "    case ACT_MOVE:\n"
            "        return 1;\n"
            "    case ACT_DIE:\n"
            "        return 1;\n"
            "}\n")
    head = {"src/sw.cpp": body.splitlines()}

    def run(table_text: str, source: str = body, fix: bool = False
            ) -> tuple[int, str, str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "src").mkdir()
            (root / "src" / "sw.cpp").write_text(source)
            table = root / "table.h"
            table.write_text(table_text)
            out, err = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(out), \
                    contextlib.redirect_stderr(err):
                rc = check(fix, root, table, {},
                           lambda path: head.get(path))
            return rc, out.getvalue(), err.getvalue()

    def pin(line: int, context: str | None) -> str:
        ctx = f', "{context}"' if context is not None else ""
        return (f'constexpr Mutation kMut_x{{"src/sw.cpp", {line}, '
                f'"return 1;", "return 0;", "why"{ctx}}};\n')

    die = "    case ACT_DIE:"

    # Accepted: the pin names the DIE arm and the DIE label is above it.
    rc, out, err = run(pin(6, die))
    if rc != 0 or "1 anchors valid (1 clean-state" not in out:
        failures.append(f"context/accepted: rc={rc} out={out.strip()!r}")

    # The twin: same text, same file, wrong arm. Applicable — and refused,
    # which is the whole point of the field.
    rc, out, err = run(pin(4, die))
    if rc != 1 or "context line is not in the" not in err:
        failures.append(f"context/twin-rejected: rc={rc} err={err.strip()!r}")

    # Beyond the window: the label is real, just too far up to anchor to.
    padded = ("    case ACT_DIE:\n" + "    // filler\n" * CONTEXT_WINDOW
              + "        return 1;\n")
    rc, out, err = run(pin(CONTEXT_WINDOW + 2, die), padded)
    if rc != 1 or "context line is not in the" not in err:
        failures.append(f"context/beyond-window: rc={rc} err={err.strip()!r}")

    # Re-indented: a block that changed nesting is a block whose pin has to be
    # re-read by a human, so the comparison is exact, indentation included.
    rc, out, err = run(pin(6, die.strip()))
    if rc != 1 or "context line is not in the" not in err:
        failures.append(f"context/re-indent: rc={rc} err={err.strip()!r}")

    # No context field at all: every pin written before this existed, judged
    # exactly as it was before — ambiguity in the file is not this check's
    # business, only the pinned line is.
    rc, out, err = run(pin(4, None))
    if rc != 0 or "1 anchors valid (1 clean-state" not in out:
        failures.append(f"context/legacy-empty: rc={rc} out={out.strip()!r}")

    # Drifted by one line. The context narrows the candidates to the arm the
    # pin means, so the report names a line — and --fix moves it there —
    # instead of refusing the choice between two identical bodies.
    shifted = "int header;\n" + body
    rc, out, err = run(pin(6, die), shifted)
    if rc != 1 or "text now at line 7" not in err:
        failures.append(f"context/drift-report: rc={rc} err={err.strip()!r}")
    rc, out, err = run(pin(6, None), shifted)
    if rc != 1 or "too generic to re-point automatically" not in err:
        failures.append(
            f"context/drift-without-context: rc={rc} err={err.strip()!r}")

    # An initializer PIN cannot read is not a legacy pin, it is an unchecked
    # one; the head count is what makes that a failure instead of a shrug.
    rc, out, err = run(pin(6, die) + 'constexpr Mutation kMut_y{ "src/sw.cpp"'
                                     ' /* hi */, 6, "return 1;", "return 0;",'
                                     ' "why"};\n')
    if rc != 1 or "are being checked by nothing" not in err:
        failures.append(f"context/head-count: rc={rc} err={err.strip()!r}")

    # The C++ mirror of the window rule, disagreeing with the applier.
    rc, out, err = run("inline constexpr int kMutationContextWindow = "
                       f"{CONTEXT_WINDOW + 1};\n" + pin(6, die))
    if rc != 1 or "judge context anchors by different rules" not in err:
        failures.append(f"context/window-mirror: rc={rc} err={err.strip()!r}")
    return failures


def self_test(verbose: bool) -> int:
    cases = _acceptance_cases()
    described = _describe_cases()
    failures = (_self_test_acceptance() + _self_test_describe()
                + _self_test_end_to_end() + _self_test_context())
    if failures:
        print(f"check_mutation_pins self-tests: {len(failures)} FAILED",
              file=sys.stderr)
        for entry in failures:
            print("  -", entry, file=sys.stderr)
        return 1
    if verbose:
        print(f"check_mutation_pins self-tests: "
              f"{len(cases) + len(described) + _E2E_ASSERTIONS + _CONTEXT_ASSERTIONS}"
              f" pass ({len(cases)} acceptance cases, {len(described)} "
              f"rendering, {_E2E_ASSERTIONS} end-to-end, "
              f"{_CONTEXT_ASSERTIONS} context anchor)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true",
                        help="re-point a drifted pin when exactly one line "
                             "in the file can carry its text")
    parser.add_argument("--in-flight", metavar="JSON", default=None,
                        help="declare the mutation currently applied "
                             f"(overrides ${IN_FLIGHT_ENV})")
    parser.add_argument("--self-test", action="store_true",
                        help="run only the acceptance self-tests")
    args = parser.parse_args()

    # The rules are verified before they are trusted, on every invocation:
    # a few milliseconds against a whole class of silently-toothless pins.
    if self_test(args.self_test) != 0:
        return 1
    if args.self_test:
        return 0

    raw = args.in_flight if args.in_flight is not None \
        else os.environ.get(IN_FLIGHT_ENV)
    try:
        declarations = parse_declarations(raw, collect_pins(TABLE.read_text()))
    except DeclarationError as exc:
        print(f"check_mutation_pins: {exc}", file=sys.stderr)
        return 2
    return check(args.fix, REPO, TABLE, declarations)


if __name__ == "__main__":
    sys.exit(main())
