#!/usr/bin/env python3
"""Stage-2 float-op / integer-shim audit for packs/core/scripts.

Classifies every arithmetic-shim call site — og.fadd/fsub/fmul/fdiv,
og.div/og.mod, og.trunc, og.i8/i16/i32/u8/u16/u32 — as either:

  PROVABLY-EXACT  the shim's C++-precision detour provably computes the same
                  value plain Lua arithmetic would, so a rewrite is emitted.
  KEEP            no local proof; the site keeps its shim and gets the
                  style-S5 one-line why-comment.

Proof rules (all derived from the shim semantics in script_host.cpp):

  og.fadd/fsub  (float)a op (float)b — exact iff both operands are provably
                integer-valued with |operand| and |result| < 2^24 (every such
                integer is exactly representable in binary32, and the float
                op then rounds nothing).  Rewrite: `a + b` / `a - b`.
  og.fmul       same, with |a*b| < 2^24.  Rewrite: `a * b`.
  og.fdiv       exact only when the divisor is an integer literal power of
                two and the numerator is a provably integer value with
                |n| < 2^24 (a binary32 division by 2^k only shifts the
                exponent).  Rewrite: `a / b` (result stays a Lua float,
                matching og.fdiv's lua_pushnumber).
  og.div        C-truncation integer division.  Lua `//` floors, and floor
                == trunc iff the quotient is non-negative: rewrite `a // b`
                only when a is provably >= 0 and b provably >= 1, both
                provably integer SUBTYPE (so `//` keeps integer subtype).
  og.mod        C remainder (sign of dividend).  Lua `%` matches iff a >= 0
                and b >= 1: rewrite `a % b` under the same proof.
  og.trunc      identity iff the operand is provably integer SUBTYPE
                (integer-valued float subtype still needs the trunc for the
                float->integer subtype conversion).  Rewrite: the operand.
  og.iN/og.uN   identity iff the operand's provable range fits the target
                type.  Rewrite: the operand (integer subtype required).

Everything else is KEEP with a generated reason.  Deliberately conservative:
a wrong PROVABLY-EXACT costs a parity batch cycle, a wrong KEEP costs one
shim.  Parity (recorder OFF and ARMED) remains the final judge of every
applied batch.

Outputs:
  --manifest FILE   JSON manifest of every site (default:
                    build/refactor-audit/shim_manifest.json)
  (default)         per-file unified diff of the EXACT rewrites plus KEEP
                    why-comments, to stdout
  --apply           write the rewrites/comments into the tree
  --no-why          skip the KEEP why-comment insertion (rewrites only)
  --summary         per-file/per-kind site table only, no diff

The og.rand audit rides along in the manifest: unguarded og.rand sites are
listed with whether n is provably >= 1 (a provably-positive bound is the
loud-tripwire keep from style S5; an unprovable one is flagged for lane
attention).

A why-comment insertion or a rewrite that changes line count in a pinned
file shifts mutation-canary pins: every application batch must re-point its
pins and prove a canary flip (see s2_partition.json for the per-lane map).
"""

from __future__ import annotations

import argparse
import collections
import difflib
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lua_corpus import (  # noqa: E402
    REPO, Analyzer, Call, Source, corpus_files, find_calls, load_stub_types,
)

TWO24 = 1 << 24

SHIM_KINDS = [
    "og.fadd", "og.fsub", "og.fmul", "og.fdiv",
    "og.div", "og.mod", "og.trunc",
    "og.i8", "og.i16", "og.i32", "og.u8", "og.u16", "og.u32",
]

NARROW_RANGES = {
    "og.i8": (-128, 127, "int8"),
    "og.i16": (-32768, 32767, "int16 (short)"),
    "og.i32": (-(1 << 31), (1 << 31) - 1, "int32"),
    "og.u8": (0, 255, "uint8"),
    "og.u16": (0, 65535, "uint16"),
    "og.u32": (0, (1 << 32) - 1, "uint32"),
}

# Float-typed engine fields, for sharper KEEP why-comments.
FLOAT_FIELD_HINTS = {
    "busy": "busy is a C++ float",
    "s_hitpoints": "hitpoints is a C++ float",
    "s_max_hitpoints": "max_hitpoints is a C++ float",
    "s_magicpoints": "magicpoints is a C++ float",
    "s_max_magicpoints": "max_magicpoints is a C++ float",
    "damage": "damage is a C++ float",
    "lastx": "lastx is a C++ float",
    "lasty": "lasty is a C++ float",
    "stepsize": "stepsize is a C++ float",
    "worldx": "worldx is a C++ float",
    "worldy": "worldy is a C++ float",
    "fire_frequency": "fire_frequency is a C++ float",
}


def needs_parens(arg: str) -> bool:
    """Whether an argument expression needs parens when spliced next to a
    binary operator: any top-level +/- (lower precedence than * and /)."""
    depth = 0
    for i, ch in enumerate(arg):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif depth == 0 and ch in "+-":
            if i == 0 and ch == "-":  # leading unary minus
                continue
            return True
    return False


def wrap(arg: str) -> str:
    arg = " ".join(arg.split())  # collapse a wrapped multi-line argument
    return f"({arg})" if needs_parens(arg) else arg


def site_context_needs_parens(src: Source, call: Call) -> bool:
    """True when the call site is an operand of a surrounding expression, so
    a multi-token replacement must be parenthesized."""
    code = src.code_text()
    j = call.start - 1
    while j >= 0 and code[j] in " \n":
        j -= 1
    before = code[j] if j >= 0 else ""
    k = call.close_paren + 1
    while k < len(code) and code[k] in " \n":
        k += 1
    after = code[k] if k < len(code) else ""
    prefix = code[: j + 1].rstrip()
    safe_before = before in "=(,{" or prefix.endswith(("return", "then", "and", "or"))
    safe_after = after in "),}" or after == ""
    return not (safe_before and safe_after)


def float_hint(arg_text: str) -> str | None:
    for meth, hint in FLOAT_FIELD_HINTS.items():
        if re.search(r":\s*%s\s*\(" % re.escape(meth), arg_text):
            return hint
    return None


def classify(src: Source, an: Analyzer, call: Call, kind: str) -> dict:
    """Returns the manifest record for one shim site.  ``short`` is the
    style-S5 why-comment text for KEEP sites; ``reason`` carries the full
    analyzer trace for the manifest."""
    line = src.line_of(call.start)
    res: dict = {
        "file": str(src.path.relative_to(REPO)), "line": line, "kind": kind,
        "text": " ".join(call.full_text(src).split()),
    }

    def keep(short: str, detail: str = "") -> dict:
        res.update({"class": "KEEP", "short": short,
                    "reason": f"{short}{' — ' + detail if detail else ''}"})
        return res

    def exact(rewrite: str, proof: str) -> dict:
        if site_context_needs_parens(src, call) and needs_parens(rewrite):
            rewrite = f"({rewrite})"
        res.update({"class": "PROVABLY-EXACT", "rewrite": rewrite,
                    "reason": proof})
        return res

    if any(call.arg_has_comment(src, i) for i in range(len(call.args))):
        return keep("shim kept (mechanical): comment inside argument",
                    "rewriting would swallow the comment; hand-simplify")

    args = [call.arg_code_text(src, i) for i in range(len(call.args))]

    if kind in ("og.fadd", "og.fsub", "og.fmul", "og.fdiv"):
        if len(args) != 2:
            return keep("unexpected arity")
        va, vb = an.eval(args[0]), an.eval(args[1])
        op = {"og.fadd": "+", "og.fsub": "-", "og.fmul": "*", "og.fdiv": "/"}[kind]
        for label, v, atext in (("lhs", va, args[0]), ("rhs", vb, args[1])):
            if not v.int_valued:
                hint = float_hint(atext) or float_hint(args[0]) or float_hint(args[1])
                if hint:
                    return keep(f"{hint}: per-op float rounding",
                                f"{label}: {v.why}")
                return keep("operand not provably integer-valued: "
                            "per-op float rounding", f"{label}: {v.why}")
        if kind == "og.fdiv":
            m = re.fullmatch(r"(\d+)(?:\.0)?", args[1])
            if not (m and int(m.group(1)) > 0 and
                    (int(m.group(1)) & (int(m.group(1)) - 1)) == 0):
                return keep("float division: quotient not exactly representable",
                            "divisor is not a literal power of two")
            if not va.bounded_by(TWO24):
                return keep("float division kept: numerator magnitude "
                            "not provably < 2^24", va.why)
            return exact(f"{wrap(args[0])} / {args[1]}",
                         f"integer/2^k float division is exact; {va.why}")
        if not (va.bounded_by(TWO24) and vb.bounded_by(TWO24)):
            return keep("float op kept: operand magnitude not provably < 2^24",
                        f"lhs {va.why}; rhs {vb.why}")
        result = Analyzer._arith(va, vb, op)
        if not result.bounded_by(TWO24):
            return keep("float op kept: result magnitude not provably < 2^24",
                        f"lhs {va.why}; rhs {vb.why}")
        # integer-valued 5.0-style literals become integer literals
        parts = [wrap(re.sub(r"\b(\d+)\.0\b", r"\1", a)) for a in args]
        return exact(f"{parts[0]} {op} {parts[1]}",
                     f"both operands integer-valued, |result| < 2^24 "
                     f"(lhs: {va.why}; rhs: {vb.why})")

    if kind in ("og.div", "og.mod"):
        if len(args) != 2:
            return keep("unexpected arity")
        va, vb = an.eval(args[0]), an.eval(args[1])
        op = "//" if kind == "og.div" else "%"
        if not (va.int_subtype and vb.int_subtype):
            return keep(f"{kind} kept: operand not provably integer subtype",
                        f"lhs {va.why}; rhs {vb.why}")
        if not va.nonneg():
            return keep("dividend can be negative: C trunc, not Lua floor",
                        va.why)
        if not vb.positive():
            return keep("divisor not provably positive", vb.why)
        return exact(f"{wrap(args[0])} {op} {wrap(args[1])}",
                     f"non-negative // positive matches C truncation "
                     f"(lhs: {va.why}; rhs: {vb.why})")

    if kind == "og.trunc":
        if len(args) != 1:
            return keep("unexpected arity")
        va = an.eval(args[0])
        if va.int_subtype:
            return exact(wrap(args[0]),
                         f"operand already integer subtype ({va.why})")
        hint = float_hint(args[0])
        if hint:
            return keep(f"{hint}: C float->int truncation", va.why)
        return keep("C float->int truncation is real here", va.why)

    lo, hi, tname = NARROW_RANGES[kind]
    if len(args) != 1:
        return keep("unexpected arity")
    va = an.eval(args[0])
    if va.int_subtype and va.lo is not None and va.hi is not None \
            and lo <= va.lo and va.hi <= hi:
        return exact(wrap(args[0]),
                     f"operand provably within {tname} range ({va.why})")
    return keep(f"narrows to {tname} like the C++ destination", va.why)


# ---------------------------------------------------------------------------
# og.rand audit (information only — rewrites live in rewrite_rand0.py)
# ---------------------------------------------------------------------------


def audit_rand(src: Source, an: Analyzer) -> list[dict]:
    out = []
    for call in find_calls(src, "og.rand"):
        if len(call.args) != 1:
            continue
        arg = call.arg_code_text(src, 0)
        v = an.eval(arg)
        out.append({
            "file": str(src.path.relative_to(REPO)),
            "line": src.line_of(call.start),
            "arg": arg,
            "provably_positive": bool(v.positive()),
            "note": ("bound provably >= 1: plain og.rand stays as the loud "
                     "tripwire (style S5)" if v.positive() else
                     f"bound NOT provably positive ({v.why}) — either a "
                     "guard trio covers it (see rewrite_rand0.py) or the "
                     "site leans on a runtime invariant; lane attention "
                     "if neither"),
        })
    return out


# ---------------------------------------------------------------------------
# Rewriting passes
# ---------------------------------------------------------------------------


def apply_exact_pass(src: Source, an: Analyzer) -> tuple[str, list[dict], bool]:
    """One pass: classify all sites, splice non-nested EXACT rewrites.
    Returns (new_text, all_site_records, changed)."""
    pairs: list[tuple[Call, dict]] = []
    for kind in SHIM_KINDS:
        for call in find_calls(src, kind):
            pairs.append((call, classify(src, an, call, kind)))
    pairs.sort(key=lambda p: p[0].start)
    sites = [s for _, s in pairs]

    edits: list[tuple[int, int, str]] = []
    covered: list[tuple[int, int]] = []
    for call, site in pairs:
        if site["class"] != "PROVABLY-EXACT":
            continue
        span = (call.start, call.close_paren + 1)
        if any(a <= span[0] and span[1] <= b and (a, b) != span
               for a, b in covered):
            continue  # nested inside an outer rewrite: next pass gets it
        covered.append(span)
        edits.append((span[0], span[1], site["rewrite"]))
    new_text = src.text
    for start, end, repl in sorted(edits, reverse=True):
        new_text = new_text[:start] + repl + new_text[end:]
    return new_text, sites, bool(edits)


def insert_why_comments(src: Source, keeps: list[dict]) -> str:
    """Insert one `-- shim kept: ...` line above each KEEP statement that has
    no adjacent commentary.  Manifest keeps the reason either way."""
    per_line: dict[int, list[str]] = collections.defaultdict(list)
    for site in keeps:
        idx = src.statement_first_line(site["line"] - 1)
        if site["short"] not in per_line[idx]:
            per_line[idx].append(site["short"])
    lines = list(src.lines)
    for idx in sorted(per_line, reverse=True):
        prev = idx - 1
        while prev >= 0 and src.is_blank_line(prev):
            prev -= 1
        if prev >= 0 and src.is_comment_line(prev):
            continue  # existing commentary wins (S3: no duplicate narration)
        if "--" in src.lines[idx]:
            continue  # inline comment present
        indent = re.match(r"\s*", src.lines[idx]).group(0)
        joined = "; ".join(per_line[idx])
        lines.insert(idx, f"{indent}-- shim kept: {joined}.")
    return "\n".join(lines) + ("\n" if src.text.endswith("\n") else "")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Stage-2 arithmetic-shim audit (see module docstring)")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--no-why", action="store_true",
                    help="do not insert KEEP why-comments")
    ap.add_argument("--summary", action="store_true",
                    help="print the site table only, no diff")
    ap.add_argument("--manifest", type=pathlib.Path,
                    default=REPO / "build" / "refactor-audit" / "shim_manifest.json")
    ap.add_argument("--files", nargs="*", metavar="NAME")
    args = ap.parse_args()

    stub_types = load_stub_types()
    if not stub_types:
        print("warning: could not parse docs/modding/og-api.d.lua; "
              "every integer-typed getter degrades to KEEP", file=sys.stderr)

    manifest: dict = {"sites": [], "rand_audit": []}
    table: dict = collections.defaultdict(lambda: collections.defaultdict(int))
    changed_files = 0

    for path in corpus_files(args.files):
        original = Source(path)

        # Fixpoint over nested EXACT sites (an outer rewrite re-exposes its
        # inner shim verbatim; 3 passes covers the deepest corpus nesting).
        src = original
        first_sites: list[dict] | None = None
        for _ in range(3):
            an = Analyzer(stub_types)
            an.load_locals(src)
            new_text, sites, changed = apply_exact_pass(src, an)
            if first_sites is None:
                first_sites = sites
                manifest["rand_audit"].extend(audit_rand(src, an))
            if not changed:
                break
            src = Source(path, new_text)

        manifest["sites"].extend(first_sites or [])
        for s in first_sites or []:
            table[path.name][s["class"]] += 1
            table[path.name]["total"] += 1

        if args.summary:
            continue

        new_text = src.text
        if not args.no_why:
            rewritten = Source(path, new_text)
            an2 = Analyzer(stub_types)
            an2.load_locals(rewritten)
            keeps: list[dict] = []
            for kind in SHIM_KINDS:
                for call in find_calls(rewritten, kind):
                    r = classify(rewritten, an2, call, kind)
                    if r["class"] == "KEEP":
                        keeps.append(r)
            new_text = insert_why_comments(rewritten, keeps)

        if new_text != original.text:
            changed_files += 1
            if args.apply:
                path.write_text(new_text)
            else:
                rel = path.relative_to(REPO)
                sys.stdout.write("".join(difflib.unified_diff(
                    original.text.splitlines(True), new_text.splitlines(True),
                    fromfile=f"a/{rel}", tofile=f"b/{rel}")))

    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=1))

    total: dict = collections.defaultdict(int)
    print(f"{'file':<28}{'sites':>6}{'exact':>7}{'keep':>6}", file=sys.stderr)
    for fname in sorted(table):
        row = table[fname]
        print(f"{fname:<28}{row['total']:>6}{row['PROVABLY-EXACT']:>7}"
              f"{row['KEEP']:>6}", file=sys.stderr)
        for k in ("total", "PROVABLY-EXACT", "KEEP"):
            total[k] += row[k]
    print(f"{'TOTAL':<28}{total['total']:>6}{total['PROVABLY-EXACT']:>7}"
          f"{total['KEEP']:>6}", file=sys.stderr)
    unproven = [r for r in manifest["rand_audit"] if not r["provably_positive"]]
    print(f"og.rand sites: {len(manifest['rand_audit'])} "
          f"({len(unproven)} with non-provable bound — see manifest)",
          file=sys.stderr)
    print(f"manifest: {args.manifest}", file=sys.stderr)
    if not args.summary:
        print(f"shim_audit: {changed_files} file(s) "
              f"{'applied' if args.apply else 'would change (dry-run)'}",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
