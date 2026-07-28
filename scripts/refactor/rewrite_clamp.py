#!/usr/bin/env python3
"""Clamp/min/max ladder -> og.max/og.min/og.clamp/og.sign rewriter (Stage 2).

og.max/og.min/og.clamp are the std::max/std::min/std::clamp bindings — ties
answer the FIRST argument (og.max(a,b) is b only when a < b) — and og.sign
is the total sign function (-1/0/1 as an integer).  Every shape below maps
onto those semantics value-exactly; the emitted operand order preserves the
original tie behavior where a tie is even observable (it never is for these
shapes: every tie picks between equal values).

Recognized shapes (X a local, K/A/B side-effect-free per
lua_corpus.is_pure_expr — a bound that draws RNG or runs a command must
never be duplicated):

  L1  if X < K then X = K end            ->  X = og.max(X, K)
      if X <= K then X = K end           ->  X = og.max(X, K)   (tie: equal)
      if X > K then X = K end            ->  X = og.min(X, K)
      if X >= K then X = K end           ->  X = og.min(X, K)   (tie: equal)
  L2  if K < X then X = K end            ->  X = og.min(X, K)
      if K > X then X = K end            ->  X = og.max(X, K)
  L3  local Y = K ; if X > K then Y = X end  -> local Y = og.max(X, K)
      local Y = K ; if X < K then Y = X end  -> local Y = og.min(X, K)
  L4  if A < B then X = A else X = B end ->  X = og.min(A, B)
      (all four comparators; operands may be any pure exprs; the then/else
      values must be exactly the compared operands)
  L5  if A > B then X = B else X = A end and friends — same table as L4.
  L6  if X < LO then X = LO elseif X > HI then X = HI end
                                         ->  X = og.clamp(X, LO, HI)
  L7  step-toward pair (chain/knife_back):
      if (D) > S then X = S else X = D end       -> X = og.min(D, S)
      if (D) > S then X = -S else X = D' end     -> X = og.max(D', -S)
         where D' is D with the subtraction operands swapped (D' == -D)
  SGN if X ~= 0 then X = og.div(X, math.abs(X)) end        -> X = og.sign(X)
      if X ~= 0 then X = og.i16(og.div(X, math.abs(X))) end -> X = og.sign(X)
      (og.sign(0) == 0, so collapsing the guard is value-exact; the i16 of
      +/-1 is the identity)

  PEEPHOLE  two adjacent rewritten lines
      X = og.min(X, HI) ; X = og.max(X, LO)   (either order, LO <= HI when
      both are integer literals)             ->  X = og.clamp(X, LO, HI)

Setter-wrapped clamps (barbarian stepsize, drumstick overheal) are left
alone on purpose: converting a guarded setter into an unconditional write
changes dirty-bit traffic, which is not provably sim-neutral.  The manifest
note in shim_audit covers the arithmetic; heal_clamped-style fusions are a
lane judgment, not a mechanical rewrite.

Parity (OFF + ARMED) gates every applied batch; pinned-file line-count
changes need the pin re-point + canary flip proof.
"""

from __future__ import annotations

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lua_corpus import Source, is_pure_expr, run_rewriter  # noqa: E402

NAME = r"[A-Za-z_]\w*"
# K: literal number, C.NAME, ALL_CAPS local constant, or plain local name
KEXPR = r"-?\d+(?:\.\d+)?|C\.\w+|[A-Za-z_]\w*"


def _pure(e: str) -> bool:
    return is_pure_expr(e)


def _swap_sub(expr: str) -> str | None:
    """A - B  ->  B - A for a single top-level subtraction, else None."""
    depth = 0
    for i, ch in enumerate(expr):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "-" and depth == 0 and i > 0:
            left, right = expr[:i].strip(), expr[i + 1:].strip()
            if left and right and "-" not in right:
                return f"{right} - {left}"
    return None


def transform(src: Source):
    lines = list(src.lines)
    code = [src.code_line(i) for i in range(len(lines))]
    notes: list[str] = []
    out: list[str] = []
    i = 0
    n = len(lines)

    def cl(k: int) -> str:
        return code[k].rstrip() if k < n else ""

    changed = False
    while i < n:
        # ---- SGN --------------------------------------------------------
        m = re.fullmatch(r"(\s*)if (%s) ~= 0(?:\.0)? then" % NAME, cl(i))
        if m and i + 2 < n:
            ind, x = m.group(1), m.group(2)
            body = cl(i + 1).strip()
            if (re.fullmatch(re.escape(ind) + r"end", cl(i + 2)) and body in (
                f"{x} = og.div({x}, math.abs({x}))",
                f"{x} = og.i16(og.div({x}, math.abs({x})))",
            )):
                out.append(f"{ind}{x} = og.sign({x})")
                notes.append(f"line {i + 1}: sign idiom on {x} -> og.sign")
                i += 3
                changed = True
                continue

        # ---- L6 if/elseif clamp ----------------------------------------
        m = re.fullmatch(r"(\s*)if (%s) < (%s) then" % (NAME, KEXPR), cl(i))
        if m and i + 4 < n:
            ind, x, lo = m.groups()
            if (cl(i + 1).strip() == f"{x} = {lo}"
                    and re.fullmatch(re.escape(ind) +
                                     r"elseif %s > (%s) then" %
                                     (re.escape(x), KEXPR), cl(i + 2))
                    and re.fullmatch(re.escape(ind) + r"end", cl(i + 4))):
            # (hi captured below)
                m2 = re.fullmatch(re.escape(ind) + r"elseif %s > (%s) then" %
                                  (re.escape(x), KEXPR), cl(i + 2))
                hi = m2.group(1)
                if cl(i + 3).strip() == f"{x} = {hi}" and _pure(lo) and _pure(hi):
                    out.append(f"{ind}{x} = og.clamp({x}, {lo}, {hi})")
                    notes.append(f"line {i + 1}: if/elseif ladder -> og.clamp")
                    i += 5
                    changed = True
                    continue

        # ---- L3 decl + guard -------------------------------------------
        m = re.fullmatch(r"(\s*)local (%s) = (%s)" % (NAME, KEXPR), cl(i))
        if m and i + 3 < n:
            ind, y, k = m.groups()
            m1 = re.fullmatch(
                re.escape(ind) + r"if (.+?) ([<>]) %s then" % re.escape(k),
                cl(i + 1))
            if m1 and cl(i + 2).strip() == f"{y} = {m1.group(1).strip()}" \
                    and re.fullmatch(re.escape(ind) + r"end", cl(i + 3)):
                x, op = m1.group(1).strip(), m1.group(2)
                if _pure(x) and _pure(k):
                    fn = "og.max" if op == ">" else "og.min"
                    out.append(f"{ind}local {y} = {fn}({x}, {k})")
                    notes.append(f"line {i + 1}: decl-guard -> {fn}")
                    i += 4
                    changed = True
                    continue

        # ---- L1 / L2 self-clamp ----------------------------------------
        m = re.fullmatch(r"(\s*)if (.+?) ([<>]=?) (.+?) then", cl(i))
        if m and i + 2 < n and re.fullmatch(re.escape(m.group(1)) + r"end",
                                            cl(i + 2)):
            ind, lhs, op, rhs = m.group(1), m.group(2).strip(), m.group(3), \
                m.group(4).strip()
            body = cl(i + 1).strip()
            mx = re.fullmatch(r"(%s) = (.+)" % NAME, body)
            if mx:
                x, assigned = mx.group(1), mx.group(2).strip()
                fn = None
                if lhs == x and assigned == rhs and _pure(rhs):
                    fn = "og.max" if op in ("<", "<=") else "og.min"
                elif rhs == x and assigned == lhs and _pure(lhs):
                    fn = "og.min" if op in ("<", "<=") else "og.max"
                    # if K < X then X = K  -> min; if K > X then X = K -> max
                if fn:
                    k = assigned
                    out.append(f"{ind}{x} = {fn}({x}, {k})")
                    notes.append(f"line {i + 1}: self-clamp -> {fn}")
                    i += 3
                    changed = True
                    continue

        # ---- L4/L5/L7 if/else two-way ----------------------------------
        m = re.fullmatch(r"(\s*)if \(?(.+?)\)? ([<>]=?) (.+?) then", cl(i))
        if m and i + 4 < n:
            ind = m.group(1)
            a, op, b = m.group(2).strip(), m.group(3), m.group(4).strip()
            m_then = re.fullmatch(r"(%s) = (.+)" % NAME, cl(i + 1).strip())
            is_else = re.fullmatch(re.escape(ind) + r"else", cl(i + 2))
            m_else = re.fullmatch(r"(%s) = (.+)" % NAME, cl(i + 3).strip())
            is_end = re.fullmatch(re.escape(ind) + r"end", cl(i + 4))
            if m_then and is_else and m_else and is_end \
                    and m_then.group(1) == m_else.group(1):
                x = m_then.group(1)
                tv, ev = m_then.group(2).strip(), m_else.group(2).strip()
                fn = None
                if _pure(a) and _pure(b):
                    gt = op in (">", ">=")
                    if {tv, ev} == {a, b} and tv != ev:
                        # cond a<b: then==a -> min ; then==b -> max (and the
                        # >= / <= tie cases pick equal values either way)
                        picks_smaller = (tv == a) != gt
                        fn = "og.min" if picks_smaller else "og.max"
                        args = f"{tv}, {ev}"
                    elif gt and tv == f"-{b}" and _swap_sub(a) == ev:
                        # L7 negative arm: if (A-B) > S then X=-S else X=B-A
                        fn = "og.max"
                        args = f"{ev}, -{b}"
                    elif not gt and tv == f"-{b}" and _swap_sub(a) == ev:
                        fn = "og.min"
                        args = f"{ev}, -{b}"
                if fn:
                    out.append(f"{ind}{x} = {fn}({args})")
                    notes.append(f"line {i + 1}: if/else two-way -> {fn}")
                    i += 5
                    changed = True
                    continue

        out.append(lines[i])
        i += 1

    if not changed:
        return None, notes

    # ---- peephole: adjacent min+max on one var -> clamp -----------------
    merged: list[str] = []
    j = 0
    while j < len(out):
        m1 = re.fullmatch(r"(\s*)(%s) = og\.min\(\2, (%s)\)" % (NAME, KEXPR),
                          out[j])
        m2 = re.fullmatch(r"(\s*)(%s) = og\.max\(\2, (%s)\)" % (NAME, KEXPR),
                          out[j + 1]) if j + 1 < len(out) else None
        if m1 and m2 and m1.group(1) == m2.group(1) \
                and m1.group(2) == m2.group(2):
            lo, hi = m2.group(3), m1.group(3)
            ok = True
            if re.fullmatch(r"-?\d+", lo) and re.fullmatch(r"-?\d+", hi):
                ok = int(lo) <= int(hi)
            if ok:
                merged.append(f"{m1.group(1)}{m1.group(2)} = "
                              f"og.clamp({m1.group(2)}, {lo}, {hi})")
                notes.append(f"peephole: min+max on {m1.group(2)} -> og.clamp")
                j += 2
                continue
        m1 = re.fullmatch(r"(\s*)(%s) = og\.max\(\2, (%s)\)" % (NAME, KEXPR),
                          out[j])
        m2 = re.fullmatch(r"(\s*)(%s) = og\.min\(\2, (%s)\)" % (NAME, KEXPR),
                          out[j + 1]) if j + 1 < len(out) else None
        if m1 and m2 and m1.group(1) == m2.group(1) \
                and m1.group(2) == m2.group(2):
            lo, hi = m1.group(3), m2.group(3)
            ok = True
            if re.fullmatch(r"-?\d+", lo) and re.fullmatch(r"-?\d+", hi):
                ok = int(lo) <= int(hi)
            if ok:
                merged.append(f"{m1.group(1)}{m1.group(2)} = "
                              f"og.clamp({m1.group(2)}, {lo}, {hi})")
                notes.append(f"peephole: max+min on {m1.group(2)} -> og.clamp")
                j += 2
                continue
        merged.append(out[j])
        j += 1

    return merged, notes


if __name__ == "__main__":
    run_rewriter("rewrite_clamp", "clamp/min/max/sign ladders -> og.*",
                 transform)
