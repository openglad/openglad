#!/usr/bin/env python3
"""The injector escape tail has ONE implementation.

An injector that drives a BLOCKING menu from a second thread cannot stop
pressing: picker_main, picker_mainmenu_loop and run_pause_menu return only when
a click walks them out, so a tail that gives up hands the process the very hang
it exists to prevent. The rule -- press whatever door the current screen
publishes, one press per screen, until the MAIN thread signals the menu call
returned, and report the leg that gave up -- lives once, in
tests/test_escape_tail.h (escape_to_the_main_thread, its EscapeDoor table and
its hold_lap predicate). Call it; do not re-type it. That is PR #245's
no-rule-twins principle applied to the test suite: a rule exists in exactly one
implementation, or it drifts in the copies nobody edits.

This gate fails the test build on a re-typed copy.

WHAT A TWIN IS -- the rule's own defining property, not a proxy for it. The
tail's distinguishing promise is that it has no wall-clock bound: it spins on
the main thread's completion flag and nothing else. In source that promise is a
loop whose ENTIRE condition is one negated atomic load,

    while (!state->test_finished.load(std::memory_order_acquire))

with nothing else in the condition -- no deadline, no elapsed, no second
predicate -- whose body presses: interact(, interact_framed( or inject_click(.
Redundant parentheses around the load are ignored, so `while (!(x.load()))`
and `while (!((x.load(std::memory_order_acquire))))` are the same sole negated
load as the line above; a bracket is spelling, not a bound.
A loop matching both halves is doing the shared header's job by hand.

WHY THE BODY SCAN IS BRACE-MATCHED, not a window of N lines after the loop:
scripts/check_injector_settles.sh says why in its own header (a proximity
window "is a gate that can be walked around by moving the sleep two lines
further down -- and it was"). A brace-matched body is the loop's actual body,
so a twin cannot be hidden by spacing it out, and a press that happens to sit
below an unrelated short loop cannot be blamed on it.

WHAT IS NOT A TWIN, and why each shape has to survive this gate:

  * A bounded wait that presses nothing. tests/integration/test_options_menu.cpp
    waits for the picker to return with

        while (!picker_returned.load() && SDL_GetTicks() < deadline)
            SDL_Delay(50);

    Its condition is not a sole negated load (it carries a clock) and its body
    holds no press. It is a wait, not a tail: there is no menu it must close.

  * A clock-bounded press ladder. tests/integration/test_pause_menu.cpp's
    RESTART and QUIT-fade injectors ride the game-epoch edge with

        while (elapsed < kRestartStageTimeoutMs && epoch.load() == before)
            { ... interact("go") ... }

    That presses, but it is bounded and it is watching an EDGE, which is the
    rule tests/test_click_ladder.h (click_until_edge) owns. It is ladder debt
    for the settle conversion, not an escape tail, and this gate must leave it
    alone -- one script per rule.

USAGE

    scripts/check_escape_tail_twins.py [ROOT]     # default ROOT: cwd
    scripts/check_escape_tail_twins.py --self-test

Scans ROOT/tests/**/*.cpp and *.h, exempting tests/test_escape_tail.h (the one
implementation). Exit codes: 0 clean (prints "Escape-tail twin check: OK"),
1 twins found (one line each, on stdout), 2 cannot run (ROOT/tests missing --
a gate that certifies a tree it never read is worse than no gate).

Wired into the build as a dependency of og_game_test (CMakeLists.txt, beside
check_injector_settles) and self-tested by the ctest entry
check_escape_tail_twins_selftest.
"""

import os
import pathlib
import re
import sys
import tempfile

EXEMPT = "tests/test_escape_tail.h"

PRESS_RE = re.compile(r"\b(?:interact|interact_framed|inject_click)\s*\(")
# `state->test_finished`, `flow->menu_returned`, `state.test_finished`,
# `menu_returned`, `ns::flag` -- a plain name path and nothing else. An
# operator anywhere in it means the condition is not a bare flag read.
FLAG_RE = re.compile(r"^[A-Za-z_]\w*(?:\s*(?:\.|->|::)\s*[A-Za-z_]\w*)*$")


def strip_comments(text):
    """Blank out comments, keeping every byte offset and line number.

    Loop text quoted inside a ruling comment (the suite is full of it -- the
    reproduction recipes name the very shape this gate hunts) must not be
    reported as code.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            for j in range(i, min(i + 2, n)):
                out[j] = " "
            i += 2
            continue
        i += 1
    return "".join(out)


def match_delim(text, start, opener, closer):
    """Index of the delimiter matching text[start] (which must be `opener`)."""
    depth = 0
    for j in range(start, len(text)):
        if text[j] == opener:
            depth += 1
        elif text[j] == closer:
            depth -= 1
            if depth == 0:
                return j
    return -1


def split_top_level(text, sep=";"):
    parts, depth, cur = [], 0, []
    for ch in text:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return parts


def is_sole_negated_load(cond):
    """True when `cond` is exactly `!<flag>.load(...)` and nothing else."""
    cond = cond.strip()
    while cond.startswith("(") and match_delim(cond, 0, "(", ")") == len(cond) - 1:
        cond = cond[1:-1].strip()
    if not cond.startswith("!"):
        return False
    rest = cond[1:].strip()
    # `!(x.load())` and `!((x.load(std::memory_order_acquire)))` say exactly
    # what `!x.load()` says; redundant parentheses around the load are stripped
    # the same way the condition's own outer parentheses are, so a twin cannot
    # be hidden behind a pair of brackets. Only BALANCED parentheses wrapping
    # the whole of `rest` come off, so `!(a && b.load())` keeps its operator
    # and is still rejected below.
    while rest.startswith("(") and match_delim(rest, 0, "(", ")") == len(rest) - 1:
        rest = rest[1:-1].strip()
    marker = rest.find(".load")
    if marker < 0:
        return False
    flag = rest[:marker]
    if not FLAG_RE.match(flag.strip()):
        return False
    after = rest[marker + len(".load"):].lstrip()
    if not after.startswith("("):
        return False
    close = match_delim(after, 0, "(", ")")
    if close < 0:
        return False
    # A memory order is allowed; a second predicate smuggled into the argument
    # list is not.
    args = after[1:close]
    if "&&" in args or "||" in args:
        return False
    return after[close + 1:].strip() == ""


def body_after(text, index):
    """The loop body starting at `index`: brace-matched, or one statement."""
    k = index
    while k < len(text) and text[k] in " \t\r\n":
        k += 1
    if k >= len(text):
        return ""
    if text[k] == "{":
        end = match_delim(text, k, "{", "}")
        return text[k:end + 1] if end >= 0 else text[k:]
    end = split_statement_end(text, k)
    return text[k:end]


def split_statement_end(text, k):
    depth = 0
    for j in range(k, len(text)):
        if text[j] in "([{":
            depth += 1
        elif text[j] in ")]}":
            depth -= 1
        elif text[j] == ";" and depth == 0:
            return j + 1
    return len(text)


def pressed_id(body):
    """The id the loop presses, or '?' when the press names no string.

    inject_click(x, y, 100) is a real twin shape with no id at all, so this
    must answer rather than crash.
    """
    m = PRESS_RE.search(body)
    if not m:
        return "?"
    open_paren = m.end() - 1
    close = match_delim(body, open_paren, "(", ")")
    args = body[open_paren + 1:close] if close > 0 else body[open_paren + 1:]
    first = split_top_level(args, ",")[0].strip()
    if first.startswith('"'):
        lit = re.match(r'"((?:[^"\\]|\\.)*)"', first)
        if lit:
            return lit.group(1)
    return "?"


def scan_text(text):
    """(offset, pressed_id) for every hand-rolled escape loop in `text`."""
    src = strip_comments(text)
    hits = []
    for m in re.finditer(r"\b(while|for)\s*\(", src):
        open_paren = m.end() - 1
        close = match_delim(src, open_paren, "(", ")")
        if close < 0:
            continue
        header = src[open_paren + 1:close]
        if m.group(1) == "for":
            clauses = split_top_level(header, ";")
            if len(clauses) != 3:
                continue  # range-for: no condition of its own
            cond = clauses[1]
        else:
            cond = header
        if not is_sole_negated_load(cond):
            continue
        body = body_after(src, close + 1)
        if PRESS_RE.search(body):
            hits.append((m.start(), pressed_id(body)))
    for m in re.finditer(r"\bdo\b", src):
        k = m.end()
        while k < len(src) and src[k] in " \t\r\n":
            k += 1
        if k >= len(src):
            continue
        if src[k] == "{":
            end = match_delim(src, k, "{", "}")
            if end < 0:
                continue
            body = src[k:end + 1]
            after = end + 1
        else:
            after = split_statement_end(src, k)
            body = src[k:after]
        tail = re.match(r"\s*while\s*\(", src[after:])
        if not tail:
            continue
        open_paren = after + tail.end() - 1
        close = match_delim(src, open_paren, "(", ")")
        if close < 0:
            continue
        if not is_sole_negated_load(src[open_paren + 1:close]):
            continue
        if PRESS_RE.search(body):
            hits.append((m.start(), pressed_id(body)))
    return sorted(hits)


def scan_tree(root):
    """(exit code, stdout lines, stderr lines) for a tree rooted at `root`."""
    root = pathlib.Path(root)
    tests = root / "tests"
    if not tests.is_dir():
        return 2, [], ["check_escape_tail_twins: cannot find %s" % tests]
    out = []
    files = [p for p in tests.rglob("*") if p.suffix in (".cpp", ".h")]
    for path in sorted(files, key=lambda p: p.relative_to(root).as_posix()):
        rel = path.relative_to(root).as_posix()
        if rel == EXEMPT:
            continue
        text = path.read_text(errors="replace")
        for offset, click in scan_text(text):
            line = text.count("\n", 0, offset) + 1
            out.append(
                "%s:%d: hand-rolled escape loop presses '%s' -- call "
                "escape_to_the_main_thread (tests/test_escape_tail.h)"
                % (rel, line, click))
    if out:
        return 1, out, []
    return 0, ["Escape-tail twin check: OK"], []


# --------------------------------------------------------------------------
# Self-test: synthetic trees, exact exit codes, exact substrings.
# The gate is only worth its dependency edge if its own verdicts are pinned;
# scripts/test_check_script_roots.sh does the same for the two grep gates.
# --------------------------------------------------------------------------

NOT_TWINS = """
#include "test_interact.h"

// The bounded wait with no press: test_options_menu.cpp's picker return.
static void wait_out(std::atomic<bool>& picker_returned) {
    const Uint64 deadline = SDL_GetTicks() + 10000;
    while (!picker_returned.load() && SDL_GetTicks() < deadline)
        SDL_Delay(50);
}

// The clock-bounded press ladder: test_pause_menu.cpp's GO ladder.
static void restart_stage(std::atomic<int>& epoch, int before) {
    Uint64 elapsed = 0;
    while (elapsed < kRestartStageTimeoutMs && epoch.load() == before) {
        if (has_interactable("go"))
            interact("go");
        SDL_Delay(100);
        elapsed += 100;
    }
}
"""

TWIN_WHILE = """
static int escape(Flow* flow, int leg) {
    while (!flow->test_finished.load()) {
        if (has_interactable("pause_resume"))
            interact("pause_resume");
        SDL_Delay(100);
    }
    return leg;
}
"""

TWIN_FOR = """
static void escape_for(std::atomic<bool>& menu_returned) {
    for (; !menu_returned.load(std::memory_order_acquire);) {
        (void)interact("back");
        SDL_Delay(50);
    }
}
"""

TWIN_DO = """
static void escape_do(std::atomic<bool>& menu_returned) {
    do {
        (void)interact("back");
    } while (!menu_returned.load());
}
"""

TWIN_BRACELESS = """
static void escape_bare(std::atomic<bool>& x) {
    while (!x.load()) interact("back");
}
"""

TWIN_INJECT_CLICK = """
static void escape_raw(std::atomic<bool>& x) {
    while (!x.load()) {
        inject_click(160, 120, 100);
        SDL_Delay(100);
    }
}
"""

TWIN_PAREN = """
static void escape_paren(std::atomic<bool>& x) {
    while (!(x.load())) {
        interact("back");
        SDL_Delay(50);
    }
}
"""


def _write(root, rel, text):
    path = pathlib.Path(root) / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def _run(root):
    code, out, err = scan_tree(root)
    return code, "\n".join(out), "\n".join(err)


def _case(number, root, want_code, want_in, want_stream="stdout"):
    code, out, err = _run(root)
    stream = out if want_stream == "stdout" else err
    if code != want_code or (want_in and want_in not in stream):
        print("FAIL case %d: expected rc %d%s" %
              (number, want_code,
               (" and %s containing %r" % (want_stream, want_in))
               if want_in else ""))
        print("  observed rc %d" % code)
        print("  stdout: %s" % out)
        print("  stderr: %s" % err)
        return False
    print("PASS case %d" % number)
    return True


def self_test():
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        # 1: both not-twin shapes, in a file that is otherwise ordinary.
        root1 = os.path.join(tmp, "case1")
        _write(root1, "tests/integration/clean.cpp", NOT_TWINS)
        ok &= _case(1, root1, 0, "Escape-tail twin check: OK")

        # 2: a while twin next to them -- named by path:line and pressed id.
        root2 = os.path.join(tmp, "case2")
        _write(root2, "tests/integration/clean.cpp", NOT_TWINS)
        _write(root2, "tests/integration/twin.cpp", TWIN_WHILE)
        code, out, err = _run(root2)
        line = TWIN_WHILE.split("\n").index(
            "    while (!flow->test_finished.load()) {") + 1
        want = "tests/integration/twin.cpp:%d" % line
        if code != 1 or want not in out or "presses 'pause_resume'" not in out:
            print("FAIL case 2: expected rc 1 and stdout naming %s and "
                  "presses 'pause_resume'" % want)
            print("  observed rc %d" % code)
            print("  stdout: %s" % out)
            print("  stderr: %s" % err)
            ok = False
        else:
            print("PASS case 2")

        # 3: the one implementation is exempt -- the same text, in the header.
        root3 = os.path.join(tmp, "case3")
        _write(root3, "tests/test_escape_tail.h", TWIN_WHILE)
        ok &= _case(3, root3, 0, "Escape-tail twin check: OK")

        # 4: a for-loop twin.
        root4 = os.path.join(tmp, "case4")
        _write(root4, "tests/integration/twin_for.cpp", TWIN_FOR)
        ok &= _case(4, root4, 1, "presses 'back'")

        # 5: a do-while twin.
        root5 = os.path.join(tmp, "case5")
        _write(root5, "tests/integration/twin_do.cpp", TWIN_DO)
        ok &= _case(5, root5, 1, "presses 'back'")

        # 6: no tests/ root at all -- cannot run, and must say so.
        root6 = os.path.join(tmp, "case6")
        pathlib.Path(root6).mkdir(parents=True, exist_ok=True)
        ok &= _case(6, root6, 2, "cannot find", "stderr")

        # 7: a braceless twin -- the body is one statement, not a block.
        root7 = os.path.join(tmp, "case7")
        _write(root7, "tests/integration/twin_bare.cpp", TWIN_BRACELESS)
        ok &= _case(7, root7, 1, "presses 'back'")

        # 8: a twin that presses raw coordinates -- no id to name, no crash.
        root8 = os.path.join(tmp, "case8")
        _write(root8, "tests/integration/twin_raw.cpp", TWIN_INJECT_CLICK)
        ok &= _case(8, root8, 1, "presses '?'")

        # 9: a twin whose load wears redundant parentheses. `!(x.load())` is
        # the same unbounded spin as `!x.load()`, so a bracket must not buy an
        # exemption.
        root9 = os.path.join(tmp, "case9")
        _write(root9, "tests/integration/twin_paren.cpp", TWIN_PAREN)
        ok &= _case(9, root9, 1, "presses 'back'")
    if not ok:
        return 1
    print("check_escape_tail_twins --self-test: all cases PASS")
    return 0


def main(argv):
    if len(argv) > 1 and argv[1] == "--self-test":
        return self_test()
    root = argv[1] if len(argv) > 1 else "."
    code, out, err = scan_tree(root)
    for line in out:
        print(line)
    for line in err:
        print(line, file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
