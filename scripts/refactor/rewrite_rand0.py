#!/usr/bin/env python3
"""Guard-trio -> og.rand0 rewriter (Stage 2).

og.rand0(n) is exactly IRandom::next with the real n <= 0 contract: 0 is
returned WITHOUT advancing the generator — precisely what the transliterated
guard trios hand-encode around og.rand's n <= 0 error.  This tool recognizes
the corpus' guard shapes and collapses each to one og.rand0 call:

  shape A — inline trio (orc yell, ghost-scare resist, drumstick heal,
            chain fork):

      local roll = 0                 local roll = og.rand0(bound)
      if bound > 0 then         ->
        roll = og.rand(bound)
      end

    The bound must be a bare local name or a provably PURE expression
    (lua_corpus.is_pure_expr): the original evaluates it twice (guard +
    call), the rewrite once, so an impure bound would change draw order.
    Comment lines above the trio are left in place (strip_provenance.py
    owns stale-guard commentary).

  shape B — guard helper (cleric's rand_level): a local function whose whole
            body is the return-form guard

      local function NAME(p)
        local n = <expr over p>
        if n > 0 then
          return og.rand(n)
        end
        return 0
      end

    The helper is deleted and every `NAME(arg)` call becomes
    `og.rand0(<expr with p := arg>)`.  The helper's contiguous leading
    comment block is deleted with it when every line of it is guard
    boilerplate (mentions of the n <= 0 error / non-advancing next(0));
    otherwise it is left for strip_provenance.py to adjudicate.

Draw-order safety: og.rand0 evaluates nothing the guard did not, performs
the identical single draw when n > 0 and the identical zero draws when
n <= 0, so the RNG stream is byte-identical by construction.  Parity (OFF +
ARMED) still gates every applied batch, and any line-count change in a
pinned file requires the pin re-point + canary flip proof.
"""

from __future__ import annotations

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lua_corpus import Source, is_pure_expr, run_rewriter  # noqa: E402

GUARD_BOILERPLATE = re.compile(
    r"og\.rand.*(?:raise|error)|rng_?\.next\(0\)|without advancing|"
    r"WITHOUT advancing|hence the guard|bound is\s*$|guarded|"
    r"never negative|value-preserving",
)


def _trio_rewrites(src: Source) -> list[tuple[int, int, str]]:
    """[(first_line_idx, line_count, replacement)] for shape A."""
    out = []
    lines = src.lines
    n = len(lines)
    i = 0
    while i < n - 3:
        m0 = re.fullmatch(r"(\s*)local (\w+) = 0\s*", src.code_line(i).rstrip("\n"))
        if not m0:
            i += 1
            continue
        indent, var = m0.group(1), m0.group(2)
        m1 = re.fullmatch(
            re.escape(indent) + r"if (.+?) > 0 then\s*",
            src.code_line(i + 1).rstrip("\n"))
        if not m1:
            i += 1
            continue
        bound = m1.group(1).strip()
        m2 = re.fullmatch(
            re.escape(indent) + r"\s+" + re.escape(var) +
            r" = og\.rand\((.+?)\)\s*",
            src.code_line(i + 2).rstrip("\n"))
        m3 = re.fullmatch(re.escape(indent) + r"end\s*",
                          src.code_line(i + 3).rstrip("\n"))
        if not (m2 and m3 and m2.group(1).strip() == bound):
            i += 1
            continue
        if not (re.fullmatch(r"[A-Za-z_]\w*", bound) or is_pure_expr(bound)):
            i += 1
            continue
        out.append((i, 4, f"{indent}local {var} = og.rand0({bound})"))
        i += 4
    return out


def _helper_rewrites(src: Source) -> tuple[list[tuple[int, int, str | None]],
                                           dict[str, tuple[str, str]],
                                           list[str]]:
    """Shape B: [(first_line, count, None=delete)], {name: (param, expr)},
    notes."""
    dels: list[tuple[int, int, str | None]] = []
    helpers: dict[str, tuple[str, str]] = {}
    notes: list[str] = []
    lines = src.lines
    i = 0
    while i + 5 < len(lines):
        m = re.fullmatch(r"local function (\w+)\((\w+)\)\s*",
                         src.code_line(i).rstrip("\n"))
        if not m:
            i += 1
            continue
        name, param = m.group(1), m.group(2)
        # exact 7-line form:
        #   local function NAME(p) / local n = E / if n > 0 then /
        #   return og.rand(n) / end / return 0 / end
        seven = [src.code_line(i + k).strip() for k in range(0, 8)
                 if i + k < len(lines)]
        if len(seven) >= 7:
            mb = re.fullmatch(r"local (\w+) = (.+)", seven[1])
            if (mb and seven[2] == f"if {mb.group(1)} > 0 then"
                    and seven[3] == f"return og.rand({mb.group(1)})"
                    and seven[4] == "end"
                    and seven[5] == "return 0"
                    and seven[6] == "end"):
                expr = mb.group(2).strip()
                if is_pure_expr(expr):
                    # delete the preceding pure-guard comment block too
                    first = i
                    j = i - 1
                    block = []
                    while j >= 0 and src.is_comment_line(j):
                        block.append(src.lines[j])
                        j -= 1
                    if block and all(GUARD_BOILERPLATE.search(b) or
                                     not b.strip("- ").strip()
                                     for b in block):
                        first = j + 1
                    dels.append((first, i + 7 - first, None))
                    helpers[name] = (param, expr)
                    notes.append(f"guard helper {name}() deleted; call sites "
                                 f"inline og.rand0({expr})")
                    i += 7
                    continue
        i += 1
    return dels, helpers, notes


def transform(src: Source):
    notes: list[str] = []
    trio = _trio_rewrites(src)
    dels, helpers, hnotes = _helper_rewrites(src)
    notes += hnotes
    if not trio and not helpers:
        return None, notes

    drop: set[int] = set()
    replace: dict[int, str] = {}
    for first, count, repl in trio:
        replace[first] = repl
        for k in range(first + 1, first + count):
            drop.add(k)
        notes.append(f"trio at line {first + 1} -> {repl.strip()}")
    for first, count, _ in dels:
        for k in range(first, first + count):
            drop.add(k)
        # also swallow one trailing blank line to avoid double blanks
        nxt = first + count
        if nxt < len(src.lines) and src.is_blank_line(nxt) and \
                first - 1 >= 0 and src.is_blank_line(first - 1):
            drop.add(nxt)

    new_lines: list[str] = []
    for idx, line in enumerate(src.lines):
        if idx in replace:
            new_lines.append(replace[idx])
        elif idx not in drop:
            new_lines.append(line)

    # inline surviving helper call sites
    for name, (param, expr) in helpers.items():
        call_re = re.compile(r"(?<![\w.:])%s\(\s*([A-Za-z_]\w*)\s*\)" % re.escape(name))

        def sub(m: re.Match) -> str:
            arg = m.group(1)
            inlined = re.sub(r"\b%s\b" % re.escape(param), arg, expr)
            return f"og.rand0({inlined})"

        new_lines = [call_re.sub(sub, ln) if not ln.lstrip().startswith("--")
                     else ln for ln in new_lines]
    return new_lines, notes


if __name__ == "__main__":
    run_rewriter("rewrite_rand0", "guard-trio -> og.rand0", transform)
