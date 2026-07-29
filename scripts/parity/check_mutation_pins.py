#!/usr/bin/env python3
"""Verify every mutation-canary pin still anchors to the line it names.

Each Mutation in tests/parity/scenario_table.h is {file, line, from, to, why}.
The canary applies it by replacing `from` on that exact line, so a pin whose
line has drifted — or whose text has changed — silently stops mutating
anything. The canary then reports a clean run and the scenario looks guarded
when it is not. That failure mode is invisible without this check, and it is
easy to cause: any insertion earlier in a pinned file shifts every pin below it.

Run: python3 scripts/parity/check_mutation_pins.py [--fix]
Exit 0 when every pin anchors, 1 when one has drifted, 2 when an in-flight
mutation declaration is malformed. With --fix, pins whose text still occurs
elsewhere in the file are re-pointed at the nearest occurrence.

ACCEPTANCE IS EXACT-STATE. A pinned line is accepted only in a state this
table can name: clean (`from` on the line), this pin mid-mutation (`to` on the
line), or the line that results from applying exactly one DECLARED sibling pin
to the shared anchor — see accepts(). "Declared" is literal: the canary
exports OPENGLAD_MUTATION_IN_FLIGHT naming the mutation it just applied, and
without that declaration the sibling state is not accepted at all. Anything
else — a different constant, a renamed variable, a wrapped or deleted
statement — reds, because green here has to mean the pins still bite.

The acceptance rules are self-tested on every run (--self-test to run only
them): a sibling rule is one loose `in` away from accepting every state of a
shared anchor, and that is indistinguishable from working until a canary run
discovers the pins have no teeth.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import pathlib
import re
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[2]
TABLE = REPO / "tests" / "parity" / "scenario_table.h"

# The canary exports this before rebuilding a mutated tree; it carries the
# exact pin it applied, as JSON ({file, line, from, to}, or a list of those).
# It is the ONLY way the sibling-state rule below can fire, so a hand-edited
# tree — which declares nothing — is judged exactly as it was judged before
# that rule existed.
IN_FLIGHT_ENV = "OPENGLAD_MUTATION_IN_FLIGHT"

# {"<path>", <line>, "<from>", "<to>", ... — path prefixes match the canary's
# own allow-list (repo-relative source under src/, packs/ or tools/).
# Both texts are captured because a pin is equally well anchored whether the
# tree is clean (line holds `from`) or mid-mutation (line holds `to`) — see
# accepts() below.
PIN = re.compile(
    r'(\{\s*"((?:src|packs|tools)/[^"]+)"\s*,\s*)(\d+)'
    r'(\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)")',
    re.S,
)

# accepts() returns which of these explained the line, or None for "drifted".
CLEAN = "clean-state"
APPLIED = "applied-state"
SIBLING = "sibling-state"
STATES = (CLEAN, APPLIED, SIBLING)


class DeclarationError(Exception):
    """OPENGLAD_MUTATION_IN_FLIGHT does not name a real, single pin."""


def unescape(text: str) -> str:
    return text.encode().decode("unicode_escape")


def reverse_one(line: str, from_text: str, to_text: str) -> str | None:
    """Return the unique line that `from_text` -> `to_text` turned into `line`.

    None when no such line exists, or when more than one could: the canary's
    applier (scripts/parity/_apply_mutation.py) refuses ambiguous edits, so
    the inverse insists on the same uniqueness in both directions. An empty
    `to` (a deletion mutation) is not invertible and yields None.
    """
    if not from_text or not to_text or to_text == from_text:
        return None
    if line.count(to_text) != 1:
        return None
    clean = line.replace(to_text, from_text, 1)
    # Round-trip: applying the pin to the reconstruction must reproduce
    # exactly the line we were handed, with `from` unambiguous on it.
    if clean.count(from_text) != 1:
        return None
    if clean.replace(from_text, to_text, 1) != line:
        return None
    return clean


def accepts(line: str, from_text: str, to_text: str,
            group: tuple[tuple[str, str], ...] = (),
            declared: tuple[str, str] | None = None) -> str | None:
    """Name the legitimate state `line` is in for this pin, or None.

    Three states, and only three:

    CLEAN — the line still holds `from`, so the canary can apply this pin.

    APPLIED — the line holds `to`. This check is a build dependency of
    og_test_parity and the canary rebuilds that target WITH THE MUTATION
    APPLIED, so insisting on `from` alone would fail the mutated build and
    abort the canary before it could measure anything — breaking the very
    tool this check exists to protect.

    SIBLING — several pins may anchor one line (walker.cpp:1189 carries
    eight rewrites of a single 362.0f multiplier). Mid-canary for pin P the
    line holds P's `to`, and a sibling Q matches neither of Q's own sides;
    without this state the build-dep check reds on the mutated tree and the
    teeth of that whole pin class are unprovable in-harness. So Q also
    anchors when the line is EXACTLY the image of the shared anchor under
    one applied pin, which takes all four of:

      * a declaration is in force for this (path, line) — the canary says
        which mutation it applied. Nothing else may claim this state;
      * the declared pin is itself one of the pins on this anchor;
      * the line inverts uniquely through that pin (reverse_one);
      * and the reconstruction explains the WHOLE group: every pin on the
        anchor finds its own `from` there. That is what makes this
        exact-state rather than fuzzy — the recovered line has to be the
        clean line the table describes, not merely some string that happens
        to contain this pin's `from` after a substring swap.

    Drift satisfies none of them: an unrecognised constant does not invert,
    a renamed variable or a wrapped statement fails the group check, and a
    deleted statement has nothing to invert at all.
    """
    if from_text and from_text in line:
        return CLEAN
    if to_text and to_text in line:
        return APPLIED
    if declared is None or declared not in group:
        return None
    clean = reverse_one(line, declared[0], declared[1])
    if clean is None:
        return None
    if any(sib_from not in clean for sib_from, _ in group):
        return None
    return SIBLING


def collect_pins(source: str) -> list[tuple[str, int, str, str]]:
    """Every pin in the table as (path, line, from, to)."""
    return [(path, int(line_text), unescape(from_text), unescape(to_text))
            for _, path, line_text, _, from_text, to_text
            in (m.groups() for m in PIN.finditer(source))]


def collect_groups(source: str) -> dict[tuple[str, int],
                                        tuple[tuple[str, str], ...]]:
    """Pins keyed by (path, line): the shared-anchor groups accepts() needs
    to recognise — and to bound — a declared sibling's mid-mutation state."""
    groups: dict[tuple[str, int], list[tuple[str, str]]] = {}
    for path, line, from_text, to_text in collect_pins(source):
        groups.setdefault((path, line), []).append((from_text, to_text))
    return {key: tuple(value) for key, value in groups.items()}


def parse_declarations(raw: str | None, pins: list[tuple[str, int, str, str]]
                       ) -> dict[tuple[str, int], tuple[str, str]]:
    """Parse OPENGLAD_MUTATION_IN_FLIGHT into {(path, line): (from, to)}.

    Every declaration must be a pin that exists in the table — a declaration
    is permission to recognise ONE known state, never a free-form claim about
    what a line may hold. Two declarations on one anchor are refused: the
    canary applies one mutation at a time, and "two at once" describes no
    legitimate state.
    """
    if raw is None or not raw.strip():
        return {}
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise DeclarationError(f"{IN_FLIGHT_ENV} is not valid JSON: {exc}")
    entries = parsed if isinstance(parsed, list) else [parsed]
    known = set(pins)
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


def check(fix: bool = False, repo: pathlib.Path = REPO,
          table: pathlib.Path | None = None,
          declarations: dict[tuple[str, int], tuple[str, str]] | None = None
          ) -> int:
    if table is None:
        table = repo / "tests" / "parity" / "scenario_table.h"
    source = table.read_text()
    groups = collect_groups(source)
    declarations = declarations or {}
    problems: list[str] = []
    repaired: list[str] = []
    tally = {state: 0 for state in STATES}
    total = 0

    for (path, line), declared in sorted(declarations.items()):
        print(f"mutation pins: in-flight mutation declared at {path}:{line} "
              f"({describe(declared[0], declared[1])})")

    def visit(match: re.Match) -> str:
        nonlocal total
        total += 1
        head, path, line_text, tail, from_text, to_text = match.groups()
        line = int(line_text)
        wanted = unescape(from_text)
        mutated = unescape(to_text)
        target = repo / path

        if not target.exists():
            problems.append(f"{path}:{line} — file does not exist")
            return match.group(0)

        lines = target.read_text().splitlines()
        state = None
        if 1 <= line <= len(lines):
            state = accepts(lines[line - 1], wanted, mutated,
                            groups.get((path, line), ()),
                            declarations.get((path, line)))
        if state is not None:
            tally[state] += 1
            return match.group(0)

        hits = [i + 1 for i, text in enumerate(lines)
                if accepts(text, wanted, mutated)]
        if not hits:
            problems.append(
                f"{path}:{line} — anchor text no longer present anywhere\n"
                f"    wanted: {wanted[:70]}")
            return match.group(0)

        nearest = min(hits, key=lambda i: abs(i - line))
        if not fix:
            problems.append(
                f"{path}:{line} — drifted, text now at line {nearest}"
                f"{' (%d occurrences)' % len(hits) if len(hits) > 1 else ''}\n"
                f"    wanted: {wanted[:70]}")
            return match.group(0)

        repaired.append(f"{path}: {line} -> {nearest}")
        return head + str(nearest) + tail

    updated = PIN.sub(visit, source)

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
        print("\nRe-run with --fix to re-point drifted pins, then re-read the"
              "\ndiff: a pin whose surrounding code changed meaning needs a"
              "\nnew anchor, not just a new line number."
              f"\n(Mid-canary? The driver must export {IN_FLIGHT_ENV} naming"
              "\nthe applied pin; the siblings of a shared anchor are"
              "\nrecognised only against that declaration.)", file=sys.stderr)
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


def _applied(pin: tuple[str, str]) -> str:
    return _CLEAN_LINE.replace(pin[0], pin[1], 1)


def _acceptance_cases() -> list[tuple[str, str, tuple[str, str],
                                      tuple[str, str] | None, str | None]]:
    """(name, line, pin, declaration-in-force, expected state)."""
    return [
        # --- own states, no declaration needed ---
        ("clean/full", _CLEAN_LINE, _P_FULL512, None, CLEAN),
        ("clean/literal", _CLEAN_LINE, _P_LIT, None, CLEAN),
        ("clean/indent", _CLEAN_LINE, _P_INDENT, None, CLEAN),
        ("applied/own-full", _applied(_P_FULL512), _P_FULL512, None, APPLIED),
        ("applied/own-inner", _applied(_P_INNER), _P_INNER, None, APPLIED),
        # --- sibling states: only against the matching declaration ---
        ("sibling/512-declared", _applied(_P_FULL512), _P_FULL181,
         _P_FULL512, SIBLING),
        ("sibling/512-declared-literal-pin", _applied(_P_FULL512), _P_LIT,
         _P_FULL512, SIBLING),
        ("sibling/512-declared-indent-pin", _applied(_P_FULL512), _P_INDENT,
         _P_FULL512, SIBLING),
        ("sibling/inner-declared", _applied(_P_INNER), _P_FULL512,
         _P_INNER, SIBLING),
        ("sibling/literal-declared", _applied(_P_LIT), _P_FULL512,
         _P_LIT, SIBLING),
        ("sibling/indent-declared", _applied(_P_INDENT), _P_FULL512,
         _P_INDENT, SIBLING),
        # THE REGRESSION THIS FILE EXISTS FOR. The same lines with no
        # declaration, or with a declaration for a pin that is not the one
        # on the line, are NOT accepted: a state nobody claims to have
        # produced is drift, however plausibly it round-trips.
        ("sibling/undeclared", _applied(_P_FULL512), _P_FULL181, None, None),
        ("sibling/undeclared-literal-pin", _applied(_P_FULL512), _P_LIT,
         None, None),
        ("sibling/undeclared-inner", _applied(_P_INNER), _P_FULL512,
         None, None),
        ("sibling/wrong-declaration", _applied(_P_FULL512), _P_FULL181,
         _P_INNER, None),
        ("sibling/declaration-not-in-group", _applied(_P_FULL512), _P_FULL181,
         _NOT_A_PIN, None),
        # --- drift shapes: every one reds, declaration or not ---
        ("drift/unknown-constant", _INDENT + _STMT.format("999.0"), _P_LIT,
         _P_FULL512, None),
        ("drift/unknown-divisor",
         _INDENT + "weapon->set_stepsize((weapon->stepsize() * 512.0f) "
                   "/ 512.0f);", _P_FULL181, _P_FULL512, None),
        ("drift/renamed-variable",
         _INDENT + "wpn->set_stepsize((wpn->stepsize() * 512.0f) / 256.0f);",
         _P_FULL181, _P_FULL512, None),
        ("drift/wrapped-statement",
         _INDENT + "if (ok) " + _STMT.format("512.0"), _P_FULL181,
         _P_FULL512, None),
        ("drift/deleted-statement", _INDENT + "// stepsize scaling removed",
         _P_FULL181, _P_FULL512, None),
        ("drift/blank", "", _P_FULL181, _P_FULL512, None),
        ("drift/reindented", "\t\t" + _STMT.format("512.0"), _P_INDENT,
         _P_FULL512, None),
        # --- inversion edge cases ---
        ("edge/ambiguous-inversion", _applied(_P_LIT) + " // 181.0f",
         _P_FULL512, _P_LIT, None),
        ("edge/empty-to-clean", _CLEAN_LINE, (_STMT.format("362.0"), ""),
         None, CLEAN),
        ("edge/empty-to-drift", _INDENT + "// gone",
         (_STMT.format("362.0"), ""), _P_FULL512, None),
        ("edge/identity-declaration", _applied(_P_FULL512), _P_FULL181,
         (_P_FULL512[0], _P_FULL512[0]), None),
    ]


def _self_test_acceptance() -> list[str]:
    failures = []
    for name, line, pin, declared, expected in _acceptance_cases():
        got = accepts(line, pin[0], pin[1], _GROUP, declared)
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


_E2E_ASSERTIONS = 7


def _self_test_end_to_end() -> list[str]:
    """check() over a synthetic table and tree: the states and the exits."""
    failures = []
    stmt = "    value = base * 362.0f;"
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "src").mkdir()
        src = root / "src" / "fake.cpp"
        (root / "src" / "other.cpp").write_text("int untouched = 1;\n")
        table = root / "table.h"
        pin_a = ("base * 362.0f", "base * 512.0f")
        pin_b = ("base * 362.0f", "base * 181.0f")
        table.write_text(
            'constexpr Mutation kMut_a{"src/fake.cpp", 2, '
            f'"{pin_a[0]}", "{pin_a[1]}", "why"}};\n'
            'constexpr Mutation kMut_b{"src/fake.cpp", 2, '
            f'"{pin_b[0]}", "{pin_b[1]}", "why"}};\n'
            'constexpr Mutation kMut_c{"src/other.cpp", 1, '
            '"int untouched = 1;", "int untouched = 2;", "why"};\n')
        pins = collect_pins(table.read_text())
        decl_a = json.dumps({"file": "src/fake.cpp", "line": 2,
                             "from": pin_a[0], "to": pin_a[1]})

        def run(body: str, raw_decl: str | None) -> tuple[int, str, str]:
            src.write_text("int header;\n" + body + "\n")
            out, err = io.StringIO(), io.StringIO()
            try:
                declarations = parse_declarations(raw_decl, pins)
            except DeclarationError as exc:
                return 2, "", str(exc)
            with contextlib.redirect_stdout(out), \
                    contextlib.redirect_stderr(err):
                rc = check(False, root, table, declarations)
            return rc, out.getvalue(), err.getvalue()

        rc, out, err = run(stmt, None)
        if rc != 0 or "3 anchors valid (3 clean-state" not in out:
            failures.append(f"e2e/clean: rc={rc} out={out.strip()!r}")

        mutated = stmt.replace(pin_a[0], pin_a[1], 1)
        rc, out, err = run(mutated, None)
        if rc != 1 or "1 of 3 mutation pins are broken" not in err:
            failures.append(
                f"e2e/undeclared-sibling: rc={rc} err={err.strip()!r}")

        rc, out, err = run(mutated, decl_a)
        if rc != 0 or "1 applied-state" not in out \
                or "1 sibling-state" not in out:
            failures.append(
                f"e2e/declared-sibling: rc={rc} out={out.strip()!r}")

        rc, out, err = run("    value = base * 999.0f;", decl_a)
        if rc != 1 or "2 of 3 mutation pins are broken" not in err:
            failures.append(
                f"e2e/declared-drift: rc={rc} err={err.strip()!r}")

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
    return failures


def self_test(verbose: bool) -> int:
    cases = _acceptance_cases()
    described = _describe_cases()
    failures = (_self_test_acceptance() + _self_test_describe()
                + _self_test_end_to_end())
    if failures:
        print(f"check_mutation_pins self-tests: {len(failures)} FAILED",
              file=sys.stderr)
        for entry in failures:
            print("  -", entry, file=sys.stderr)
        return 1
    if verbose:
        print(f"check_mutation_pins self-tests: "
              f"{len(cases) + len(described) + _E2E_ASSERTIONS} pass "
              f"({len(cases)} acceptance cases, {len(described)} rendering, "
              f"{_E2E_ASSERTIONS} end-to-end)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true",
                        help="re-point drifted pins at the nearest occurrence")
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
