#!/usr/bin/env python3
"""Lint tests/parity/scenario_table.h for Phase 01 semantic-parity contract.

Asserts:
  (a) expected_facts != nullptr iff fact_count > 0 (paired)
  (b) every ByteEqual / SemanticParity row has fact_count > 0 AND at
      least one non-TickReached predicate
  (c) every row has a non-default discriminating_mutation (file, line>0,
      from, to, rationale all non-empty)
  (d) every special_<family>_<idx>_scen* row with idx >= 2 has a team-0
      caster SpawnSpec with stats_level >= (idx-1)*3+1 AND
      magicpoints >= 600 (cycling gate sim_input_handler.cpp:218 +
      firing gate living.cpp:532-533 preconditions; the 600 floor is
      500 MP non-sentinel cap + 100 MP headroom).

The lint is regex-based, not a full C++ parser. It expects the
scenario_table.h emitted by Phase 01 and breaks loudly on schema drift.

This lint reads only the C++ table; the matching JSON view at
``tests/parity/scenario_facts_generated.json`` may carry additional
optional keys per predicate (notably ``applies_to_branch`` and
``applies_to_master``, both default true). Those keys are emitted by
``scenario_facts_dump_main.cpp`` and are tolerated by this lint by virtue
of it not parsing the JSON at all — they pass through unobserved here.

Override the parsed table file via LINT_SCENARIO_TABLE=<path>.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TABLE = REPO_ROOT / "tests" / "parity" / "scenario_table.h"


def _load_table(path: Path) -> str:
    if not path.is_file():
        sys.stderr.write(f"lint: cannot read {path}\n")
        sys.exit(2)
    return path.read_text(encoding="utf-8")


def _slice(text: str, start_marker: str, end_marker: str) -> str:
    s = text.find(start_marker)
    if s < 0:
        return ""
    e = text.find(end_marker, s)
    if e < 0:
        return ""
    return text[s:e]


def parse_predicate_arrays(text: str) -> dict[str, list[str]]:
    """Return {array_name: [predicate_kind_or_label, ...]}."""
    out: dict[str, list[str]] = {}
    rx = re.compile(
        r"inline\s+constexpr\s+FactPredicate\s+(kFacts_\w+)\s*\[\]\s*=\s*\{([^}]*)\};",
        re.DOTALL,
    )
    for m in rx.finditer(text):
        name = m.group(1)
        body = m.group(2)
        kinds = re.findall(r"pred::(\w+)\s*\(", body)
        out[name] = kinds
    return out


def parse_predicate_calls(text: str) -> dict[str, list[dict]]:
    """{array_name: [{kind, raw_args, trail}, ...]} where raw_args is the
    parenthesised argument list of each pred::Kind(...) call as written
    in the source. `trail` captures the source between this predicate's
    closing `)` (plus the comma) and the next predicate's `pred::` token
    (or the end of the array body); the unjustified_widening rule scans
    `trail` for inline `// intended_diff:` / `// rng_drift:` markers.
    The lint inspects raw_args by position to enforce per-FactKind shape
    rules."""
    out: dict[str, list[dict]] = {}
    rx = re.compile(
        r"inline\s+constexpr\s+FactPredicate\s+(kFacts_\w+)\s*\[\]\s*=\s*\{([^}]*)\};",
        re.DOTALL,
    )
    for m in rx.finditer(text):
        name = m.group(1)
        body = m.group(2)
        # Per-predicate spans so we can compute the trailing region.
        spans: list[tuple[int, int, str, list[str]]] = []
        i = 0
        while True:
            mm = re.search(r"pred::(\w+)\s*\(", body[i:])
            if mm is None:
                break
            kind = mm.group(1)
            call_start = i + mm.start()
            arg_start = i + mm.end()
            depth = 1
            j = arg_start
            in_str = False
            while j < len(body) and depth > 0:
                c = body[j]
                if in_str:
                    if c == "\\":
                        j += 2
                        continue
                    if c == '"':
                        in_str = False
                else:
                    if c == '"':
                        in_str = True
                    elif c == "(":
                        depth += 1
                    elif c == ")":
                        depth -= 1
                        if depth == 0:
                            break
                j += 1
            raw_args = body[arg_start:j]
            # Split args on top-level commas.
            args: list[str] = []
            cur = ""
            depth2 = 0
            in_str2 = False
            for c in raw_args:
                if in_str2:
                    cur += c
                    if c == '"':
                        in_str2 = False
                    continue
                if c == '"':
                    in_str2 = True
                    cur += c
                    continue
                if c in "([{":
                    depth2 += 1
                elif c in ")]}":
                    depth2 -= 1
                if c == "," and depth2 == 0:
                    args.append(cur.strip())
                    cur = ""
                else:
                    cur += c
            if cur.strip():
                args.append(cur.strip())
            spans.append((call_start, j + 1, kind, args))
            i = j + 1
        preds: list[dict] = []
        for idx, (cs, ce, kind, args) in enumerate(spans):
            next_start = spans[idx + 1][0] if idx + 1 < len(spans) else len(body)
            trail = body[ce:next_start]
            preds.append({"kind": kind, "args": args, "trail": trail})
        out[name] = preds
    return out


def parse_int_arg(arg: str) -> "int | None":
    """Parse an integer constant ignoring `/*comment*/` and trailing
    whitespace. Returns None if the arg is not a bare int literal."""
    # Strip C-style comments.
    s = re.sub(r"/\*.*?\*/", "", arg, flags=re.DOTALL).strip()
    # Strip designated-init prefixes like /*tick window upper marker*/.
    if not s:
        return None
    m = re.fullmatch(r"-?\d+", s)
    if not m:
        return None
    try:
        return int(s)
    except ValueError:
        return None


def parse_mutation_constants(text: str) -> dict[str, dict[str, str]]:
    """{const_name: {file,line,from,to,rationale}}; lint doesn't deeply parse."""
    out: dict[str, dict[str, str]] = {}
    head_rx = re.compile(
        r"inline\s+constexpr\s+Mutation\s+(kMut_\w+)\s*=\s*\{",
    )
    for hm in head_rx.finditer(text):
        name = hm.group(1)
        start = hm.end()
        # String-aware brace balance from the opening brace.
        depth = 1
        i = start
        in_str = False
        while i < len(text) and depth > 0:
            c = text[i]
            if in_str:
                if c == "\\":
                    i += 2
                    continue
                if c == '"':
                    in_str = False
            else:
                if c == '"':
                    in_str = True
                elif c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    if depth == 0:
                        break
            i += 1
        body = text[start:i].strip()
        # Five comma-separated initialisers, plus the optional sixth
        # (context_before) added later (top-level commas; strings can't
        # nest braces here so a simple split-on-comma works after length-
        # preserving string masking).
        masked = re.sub(
            r'"(?:\\.|[^"\\])*"',
            lambda mm: " " * len(mm.group(0)),
            body,
        )
        fields_idx = []
        depth = 0
        start = 0
        for i, ch in enumerate(masked):
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                fields_idx.append((start, i))
                start = i + 1
        fields_idx.append((start, len(masked)))
        parts = [body[s:e].strip() for s, e in fields_idx]
        # Strip wrapping quotes for strings and decode the C++ escape
        # sequences the compiler would resolve, so the recovered value
        # equals the runtime string literal (and thus matches the
        # on-disk source line _apply_mutation.py edits). `line` is a bare
        # integer left untouched.
        def unquote(s: str) -> str:
            if s.startswith('"') and s.endswith('"'):
                inner = s[1:-1]
                out_chars: list[str] = []
                i = 0
                while i < len(inner):
                    c = inner[i]
                    if c == "\\" and i + 1 < len(inner):
                        nxt = inner[i + 1]
                        out_chars.append(
                            {
                                "t": "\t",
                                "n": "\n",
                                "r": "\r",
                                '"': '"',
                                "\\": "\\",
                            }.get(nxt, "\\" + nxt)
                        )
                        i += 2
                        continue
                    out_chars.append(c)
                    i += 1
                return "".join(out_chars)
            return s
        if len(parts) >= 5:
            out[name] = {
                "file":      unquote(parts[0]),
                "line":      parts[1],
                "from":      unquote(parts[2]),
                "to":        unquote(parts[3]),
                "rationale": unquote(parts[4]),
                # Optional: the line that must sit verbatim above the pinned
                # one. Absent on every pin whose from-text is unique in its
                # file, which is most of them.
                "context_before": unquote(parts[5]) if len(parts) >= 6 else "",
            }
    return out


def _balanced_block(text: str, marker: str) -> str:
    """Return the {...} content following `marker`, brace-balanced."""
    idx = text.find(marker)
    if idx < 0:
        return ""
    brace = text.find("{", idx)
    if brace < 0:
        return ""
    depth = 0
    for i in range(brace, len(text)):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1 : i]
    return ""


_BRANCH_INTERNAL_RX = re.compile(
    r"CompareMode::(?:ByteEqual|SemanticParity|Invariant)\s*,\s*true\b"
)


def parse_scenarios(text: str) -> list[dict[str, str]]:
    """Return one dict per master-comparable row inside kScenarios[].

    Mirrors the companion's `list_scenarios()` filter in
    `tools/parity_dump_master.cpp` — rows marked `is_branch_internal=true`
    (the bool positional field after compare_mode) are skipped. Branch-internal
    scenarios cannot be cross-binary verified against master, so they are not
    counted by lint, canary, or the Phase 02 mirror-parity check.
    """
    body = _balanced_block(text, "inline constexpr ScenarioSpec kScenarios[]")
    if not body:
        sys.stderr.write("lint: could not locate kScenarios[] block\n")
        sys.exit(2)
    # Each row is brace-bounded; split on top-level {...} blocks.
    rows: list[str] = []
    depth = 0
    start = -1
    for i, ch in enumerate(body):
        if ch == "{":
            if depth == 0:
                start = i + 1
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start >= 0:
                rows.append(body[start:i])
                start = -1
    out = []
    for r in rows:
        if _BRANCH_INTERNAL_RX.search(r):
            continue
        # ID is the first quoted token.
        m = re.search(r'"([^"]+)"', r)
        if not m:
            continue
        sid = m.group(1)
        # Detect compare_mode token.
        mode = "ByteEqual"
        if "SemanticParity" in r:
            mode = "SemanticParity"
        elif "Invariant" in r:
            mode = "Invariant"
        # expected_facts token (kFacts_* or nullptr). Look at the row's
        # tail, after the Exercises:: token; the leading inputs field
        # would otherwise match nullptr,0.
        tail_after_exercises = r
        ex_idx = r.find("Exercises::")
        if ex_idx >= 0:
            # skip past the Exercises::Foo token + trailing comma
            after = r[ex_idx:]
            comma = after.find(",")
            if comma >= 0:
                tail_after_exercises = after[comma + 1 :]
        ef = re.search(
            r"(kFacts_\w+|nullptr)\s*,\s*(std::size\(kFacts_\w+\)|0)",
            tail_after_exercises,
        )
        if ef:
            facts_name = ef.group(1)
            facts_count_expr = ef.group(2)
        else:
            facts_name = "nullptr"
            facts_count_expr = "0"
        # discriminating mutation: kMut_* identifier (or '{}' default) at
        # the tail. Newer rows may carry one trailing coverage_audit string
        # after the mutation for static checkers that do not resolve
        # kFacts_* arrays.
        mut_match = re.search(
            r"(kMut_\w+|\{\s*\})(?:\s*,\s*\"(?:\\.|[^\"])*\")?\s*$",
            r.strip(),
            re.S,
        )
        mut_token = mut_match.group(1) if mut_match else ""
        out.append({
            "id":              sid,
            "raw":             r,
            "compare_mode":    mode,
            "expected_facts":  facts_name,
            "fact_count":      facts_count_expr,
            "mutation_token":  mut_token,
        })
    return out


SPECIAL_RX = re.compile(r"^special_(?P<family>[a-z_]+?)(?:_(?P<idx>\d+))?_scen\d+$")

# Phase 04-prep — lower-snake family name -> integer family-id. Mirrors
# family_symbol() in tests/parity/state_dump.cpp; used by the
# `caster_zero_min` rule to map a `special_<family>_<idx>_scen*` row to
# the integer the caster's WalkerFamilyCount predicate would carry.
FAMILY_NAME_TO_ID: dict[str, int] = {
    "soldier":         0,
    "elf":             1,
    "archer":          2,
    "mage":            3,
    "skeleton":        4,
    "cleric":          5,
    "fireelemental":   6,
    "faerie":          7,
    "slime":           8,
    "small_slime":     9,
    "medium_slime":   10,
    "thief":          11,
    "ghost":          12,
    "druid":          13,
    "orc":            14,
    "big_orc":        15,
    "barbarian":      16,
    "archmage":       17,
    "golem":          18,
    "giant_skeleton": 19,
    "tower1":         20,
}

# Phase 04-prep — recogniser for the `// negative_assertion: <reason>`
# inline comment that licenses `WalkerFamilyCount(F, 0, 0)` and
# `EffectFamilyCount(F, 0, 0)` predicates per policy decision P3.
NEGATIVE_ASSERTION_RX = re.compile(
    r"//\s*negative_assertion\s*:\s*(?P<reason>\S.*)"
)

# Phase 04-prep — recogniser for the `// scripted_spawn: ...` inline
# comment that licenses a `WeaponFamilyEmitted(F)` predicate whose
# spawn-side evidence is a `kOrderWeapon` script entry (policy P5).
SCRIPTED_SPAWN_RX = re.compile(
    r"//\s*scripted_spawn\s*:\s*(?P<reason>\S.*)"
)

# unjustified_widening rule grammar — inline C++ comment attached to a
# widened predicate. Must include a reason of at least 20 characters and
# a `; commit <hex 7..40>` trailing token.
WIDENING_JUSTIFICATION_RX = re.compile(
    r"//\s*(?P<kind>intended_diff|rng_drift)\s*:\s*(?P<reason>.{20,}?)\s*;\s*commit\s+(?P<sha>[0-9a-fA-F]{7,40})\b"
)

# Maximum hp-cents span a WalkerHpRangeAtFinalTick range may carry without
# triggering the unjustified_widening rule. Mirrors the spec threshold of
# 200 hp-cents = 2.0 hp.
HP_RANGE_TIGHT_MAX_CENTS = 200


def parse_spawn_arrays(text: str) -> dict[str, list[dict]]:
    """Phase 04-prep — return {array_name: [{family, team, order,
    default_weapon, current_weapon}, ...]}.

    Used by `family_alias_mismatch` to verify that a behavioural
    predicate that names a treasure / weapon family-id has corresponding
    spawn-side evidence in the scenario's `SpawnSpec[]` array. We only
    need the first seven fields (family, team, order, x, y,
    default_weapon, current_weapon) so the parser ignores the
    Phase 01 tail fields (stats_level, magicpoints, precompleted_level).
    """
    out: dict[str, list[dict]] = {}
    head_rx = re.compile(
        r"inline\s+constexpr\s+SpawnSpec\s+(k\w+)\s*\[\]\s*=\s*\{",
    )
    for hm in head_rx.finditer(text):
        name = hm.group(1)
        start = hm.end()
        depth = 1
        i = start
        in_str = False
        while i < len(text) and depth > 0:
            c = text[i]
            if in_str:
                if c == "\\":
                    i += 2
                    continue
                if c == '"':
                    in_str = False
            else:
                if c == '"':
                    in_str = True
                elif c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    if depth == 0:
                        break
            i += 1
        body = text[start:i]
        entries: list[dict] = []
        # Each entry is `{ family, team, order, x, y, default_weapon, current_weapon, ... }`.
        for entry_match in re.finditer(r"\{([^{}]*)\}", body):
            inner = entry_match.group(1)
            # Mask string literals so commas inside them don't split.
            masked = re.sub(
                r'"(?:\\.|[^"\\])*"',
                lambda mm: " " * len(mm.group(0)),
                inner,
            )
            parts: list[tuple[int, int]] = []
            depth2 = 0
            seg_start = 0
            for j, ch in enumerate(masked):
                if ch == "(":
                    depth2 += 1
                elif ch == ")":
                    depth2 -= 1
                elif ch == "," and depth2 == 0:
                    parts.append((seg_start, j))
                    seg_start = j + 1
            parts.append((seg_start, len(masked)))
            seg_strs = [inner[s:e].strip() for s, e in parts]
            if len(seg_strs) < 7:
                continue
            family = parse_int_arg(seg_strs[0])
            team   = parse_int_arg(seg_strs[1])
            order  = re.sub(r"/\*.*?\*/", "", seg_strs[2], flags=re.DOTALL).strip()
            default_weapon = parse_int_arg(seg_strs[5])
            current_weapon = parse_int_arg(seg_strs[6])
            if family is None or team is None:
                continue
            entries.append({
                "family":         family,
                "team":           team,
                "order":          order,
                "default_weapon": default_weapon if default_weapon is not None else 0,
                "current_weapon": current_weapon if current_weapon is not None else 0,
            })
        out[name] = entries
    return out


def _classify_widened(kind: str, args: list[str]) -> "tuple[bool, str]":
    """Return (is_widened, human-readable predicate signature). Widened
    iff WalkerFamilyCount/WeaponFamilyCount/WalkerOfTeamAlive with
    arg1 != arg2 OR WalkerHpRangeAtFinalTick with (arg2-arg1) > 200.
    Unparseable args short-circuit to "not widened" so the lint never
    false-fires on a novel call shape."""
    if kind in ("WalkerFamilyCount", "WeaponFamilyCount", "WalkerOfTeamAlive"):
        if len(args) < 3:
            return False, kind
        mn = parse_int_arg(args[1])
        mx = parse_int_arg(args[2])
        if mn is None or mx is None:
            return False, kind
        if mn == mx:
            return False, f"{kind}(arg0={args[0]}, {mn}, {mx})"
        return True, f"{kind}(arg0={args[0]}, {mn}, {mx})"
    if kind == "WalkerHpRangeAtFinalTick":
        if len(args) < 3:
            return False, kind
        mn = parse_int_arg(args[1])
        mx = parse_int_arg(args[2])
        if mn is None or mx is None:
            return False, kind
        if (mx - mn) <= HP_RANGE_TIGHT_MAX_CENTS:
            return False, f"{kind}(arg0={args[0]}, {mn}, {mx})"
        return True, f"{kind}(arg0={args[0]}, {mn}, {mx})"
    return False, kind


def main() -> int:
    table_env = os.environ.get("LINT_SCENARIO_TABLE")
    table = Path(table_env) if table_env else DEFAULT_TABLE
    text = _load_table(table)

    pred_arrays = parse_predicate_arrays(text)
    pred_calls  = parse_predicate_calls(text)
    mutations   = parse_mutation_constants(text)
    rows        = parse_scenarios(text)
    spawn_arrays = parse_spawn_arrays(text)

    # Spec-mandated rule (Phase 03 unjustified_widening): every widened
    # range predicate (WalkerFamilyCount / WalkerOfTeamAlive with
    # arg1 != arg2, or WalkerHpRangeAtFinalTick with span > 200 hp-cents)
    # must carry an inline `// intended_diff: <reason ≥20 chars>;
    # commit <sha 7..40>` OR `// rng_drift: ...; commit ...` justification
    # placed in the source span between the predicate's closing `)` and
    # the next predicate. Catches future regressions where someone widens
    # a range to mask a divergence rather than narrow to exact-value
    # semantics or annotate the rationale.
    widening_lint_errors: list[str] = []
    for arr_name, preds in pred_calls.items():
        for i, pred in enumerate(preds):
            widened, sig = _classify_widened(pred["kind"], pred["args"])
            if not widened:
                continue
            trail = pred.get("trail", "")
            if not WIDENING_JUSTIFICATION_RX.search(trail):
                widening_lint_errors.append(
                    f"{arr_name}[#{i}]: widened predicate {sig} missing "
                    f"`// intended_diff:` or `// rng_drift:` justification "
                    f"(grammar: kind ':' reason '; commit ' <hex 7..40>)")

    # Spec-mandated rule: every EffectFamilyCount predicate must be
    # qualified by EITHER a source-walker family (`arg3 >= 0`) OR a
    # tick-window upper bound (`arg4 > 0`). Unqualified predicates
    # collapse to "count effects of family X" with no link to the
    # producing scenario state, so they cannot reliably flip on a
    # discriminating mutation. Reject them.
    effect_lint_errors: list[str] = []
    for arr_name, preds in pred_calls.items():
        for i, pred in enumerate(preds):
            if pred["kind"] != "EffectFamilyCount":
                continue
            args = pred["args"]
            # pred::EffectFamilyCount(family, mn, mx, source_qualifier, window=0, label="")
            arg3 = parse_int_arg(args[3]) if len(args) >= 4 else None
            arg4 = parse_int_arg(args[4]) if len(args) >= 5 else None
            source_qualified = arg3 is not None and arg3 >= 0
            window_qualified = arg4 is not None and arg4 > 0
            if not source_qualified and not window_qualified:
                effect_lint_errors.append(
                    f"{arr_name}[#{i}]: unqualified EffectFamilyCount "
                    f"(arg3={arg3}, arg4={arg4}); must have source-walker "
                    f"family (arg3>=0) OR tick-window upper (arg4>0)")

    # Phase 04-prep — dead_predicate rule. Detects rows where the
    # outer pred:: wrapper is `branch_only(pred::master_only(...))` (or
    # the symmetric inversion). The runtime gate also asserts this at
    # test time (`Parity.behavioural_coverage_gate_no_dead_predicates`);
    # the lint catches the violation pre-build so source-level patches
    # never reach a build that would otherwise pass.
    dead_predicate_errors: list[str] = []
    for arr_name, preds in pred_calls.items():
        for i, pred in enumerate(preds):
            kind = pred["kind"]
            args = pred["args"]
            raw_args = ",".join(args)
            outer_is_branch_only = kind == "branch_only"
            outer_is_master_only = kind == "master_only"
            if outer_is_branch_only and "master_only" in raw_args:
                dead_predicate_errors.append(
                    f"{arr_name}[#{i}]: dead_predicate — "
                    f"branch_only wrapping master_only short-circuits both "
                    f"sides (applies_to_master=false AND applies_to_branch=false)")
            elif outer_is_master_only and "branch_only" in raw_args:
                dead_predicate_errors.append(
                    f"{arr_name}[#{i}]: dead_predicate — "
                    f"master_only wrapping branch_only short-circuits both "
                    f"sides (applies_to_master=false AND applies_to_branch=false)")

    # Phase 04-prep — vacuous_event_floor rule. Rejects
    # `EventKindAtLeast(*, 0)`: the predicate "at least 0 events of
    # kind K" always passes, so it carries no behavioural binding and
    # cannot be flipped by any mutation (policy P4).
    vacuous_event_floor_errors: list[str] = []
    for arr_name, preds in pred_calls.items():
        for i, pred in enumerate(preds):
            if pred["kind"] != "EventKindAtLeast":
                continue
            args = pred["args"]
            if len(args) < 2:
                continue
            mn = parse_int_arg(args[1])
            if mn == 0:
                vacuous_event_floor_errors.append(
                    f"{arr_name}[#{i}]: vacuous_event_floor — "
                    f"EventKindAtLeast(arg0={args[0]}, 0) always passes; "
                    f"use EventKindAtLeast(*, 1) or EventKindExactly(*, n)")

    # Phase 04-prep — zero_zero_count_no_negation rule. A
    # `WalkerFamilyCount(F, 0, 0)`, `WeaponFamilyCount(F, 0, 0)` or
    # `EffectFamilyCount(F, 0, 0)` predicate is an assertion that the
    # family is absent. That is
    # only honest as a paired negative assertion (policy P3), so the
    # source row MUST carry an inline `// negative_assertion: <reason>`
    # comment that the lint can grep for. Absent comment -> violation.
    zero_zero_count_errors: list[str] = []
    for arr_name, preds in pred_calls.items():
        for i, pred in enumerate(preds):
            kind = pred["kind"]
            if kind not in ("WalkerFamilyCount", "WeaponFamilyCount",
                            "EffectFamilyCount"):
                continue
            args = pred["args"]
            if len(args) < 3:
                continue
            mn = parse_int_arg(args[1])
            mx = parse_int_arg(args[2])
            if mn != 0 or mx != 0:
                continue
            trail = pred.get("trail", "")
            if not NEGATIVE_ASSERTION_RX.search(trail):
                zero_zero_count_errors.append(
                    f"{arr_name}[#{i}]: zero_zero_count_no_negation — "
                    f"{kind}(arg0={args[0]}, 0, 0) needs inline "
                    f"`// negative_assertion: <reason>` comment between "
                    f"this predicate and the next (policy P3)")

    # Phase 04-prep — family_alias_mismatch and caster_zero_min rules.
    # Both require mapping a scenario row -> its kFacts_* predicate
    # array and its kSpawns_* spawn array. Iterate `rows` and apply
    # both checks per scenario.
    family_alias_errors: list[str] = []
    caster_zero_min_errors: list[str] = []
    for row in rows:
        sid       = row["id"]
        facts_ref = row["expected_facts"]
        if facts_ref == "nullptr":
            continue
        preds = pred_calls.get(facts_ref, [])
        if not preds:
            continue

        spawn_ref_match = re.search(r"(k\w*Spawns_\w+)", row["raw"])
        spawn_name = spawn_ref_match.group(1) if spawn_ref_match else ""
        spawns     = spawn_arrays.get(spawn_name, [])

        # caster_zero_min — special_<family>_<idx>_scen* rows where the
        # caster's WalkerFamilyCount predicate has min=0 max=2. The
        # caster is the team-0 wielder in the scenario's spawn list,
        # which is guaranteed alive at scenario start, so the only
        # honest range for the caster family is (1, 1) at minimum.
        sm = SPECIAL_RX.match(sid)
        caster_family_id: "int | None" = None
        if sm:
            name = sm.group("family")
            caster_family_id = FAMILY_NAME_TO_ID.get(name)
            if caster_family_id is None:
                # Spawn-list fallback: take the first team-0 living entry's
                # family-id, which is the caster by spawn convention.
                for sp in spawns:
                    if sp.get("team") == 0 and sp.get("order", "").endswith("Living"):
                        caster_family_id = sp.get("family")
                        break
        if caster_family_id is not None:
            for i, pred in enumerate(preds):
                if pred["kind"] != "WalkerFamilyCount":
                    continue
                args = pred["args"]
                if len(args) < 3:
                    continue
                arg0 = parse_int_arg(args[0])
                mn   = parse_int_arg(args[1])
                mx   = parse_int_arg(args[2])
                if arg0 == caster_family_id and mn == 0 and mx == 2:
                    caster_zero_min_errors.append(
                        f"{facts_ref}[#{i}] (scenario {sid}): "
                        f"caster_zero_min — WalkerFamilyCount("
                        f"caster_family={caster_family_id}, 0, 2) "
                        f"vacuous; caster is alive + unique at scenario "
                        f"start, the only honest range is (1, 1) "
                        f"(policy P8)")

        # family_alias_mismatch — each TreasureFamilyRemovedFromOblist(F)
        # or WeaponFamilyEmitted(F) predicate must have spawn-side
        # evidence in the scenario's SpawnSpec[] array:
        #   * TreasureFamilyRemovedFromOblist(F): an entry with
        #     order=kOrderTreasure and family=F.
        #   * WeaponFamilyEmitted(F): EITHER (a) order=kOrderWeapon and
        #     family=F AND the predicate carries an inline
        #     `// scripted_spawn: ...` comment that explains the
        #     scripted spawn; OR (b) a kOrderLiving entry whose
        #     default_weapon=F (set_default_weapon).
        for i, pred in enumerate(preds):
            kind = pred["kind"]
            args = pred["args"]
            if not args:
                continue
            family = parse_int_arg(args[0])
            if family is None:
                continue
            trail = pred.get("trail", "")
            if kind == "TreasureFamilyRemovedFromOblist":
                ok = any(
                    sp.get("family") == family and sp.get("order") == "kOrderTreasure"
                    for sp in spawns
                )
                if not ok:
                    family_alias_errors.append(
                        f"{facts_ref}[#{i}] (scenario {sid}): "
                        f"family_alias_mismatch — TreasureFamilyRemovedFromOblist"
                        f"({family}) but {spawn_name or '<no spawn array>'} has "
                        f"no kOrderTreasure entry with family-id == {family} "
                        f"(policy P6)")
            elif kind == "WeaponFamilyEmitted":
                via_weapon_order = any(
                    sp.get("family") == family and sp.get("order") == "kOrderWeapon"
                    for sp in spawns
                )
                via_default_weapon = any(
                    sp.get("order") == "kOrderLiving" and
                    sp.get("default_weapon") == family
                    for sp in spawns
                )
                has_scripted_spawn_comment = bool(SCRIPTED_SPAWN_RX.search(trail))
                ok_a = via_weapon_order and has_scripted_spawn_comment
                ok_b = via_default_weapon
                if not (ok_a or ok_b):
                    family_alias_errors.append(
                        f"{facts_ref}[#{i}] (scenario {sid}): "
                        f"family_alias_mismatch — WeaponFamilyEmitted("
                        f"{family}) but {spawn_name or '<no spawn array>'} "
                        f"has no kOrderLiving wielder with default_weapon=={family} "
                        f"AND no kOrderWeapon entry with family-id == {family} "
                        f"paired with a `// scripted_spawn:` predicate-trail "
                        f"comment (policy P6)")

    if not rows:
        sys.stderr.write("lint: no scenario rows parsed\n")
        return 1
    errors: list[str] = []

    for row in rows:
        sid  = row["id"]
        mode = row["compare_mode"]
        ef   = row["expected_facts"]
        fc   = row["fact_count"]
        mut  = row["mutation_token"]

        # (a) expected_facts != nullptr iff fact_count > 0 (paired).
        has_facts_ptr = ef != "nullptr"
        has_facts_cnt = fc != "0"
        if has_facts_ptr != has_facts_cnt:
            errors.append(
                f"{sid}: expected_facts ({ef}) / fact_count ({fc}) mismatched")

        # (b) ByteEqual/SemanticParity require fact_count > 0 + non-tick fact.
        if mode in ("ByteEqual", "SemanticParity"):
            if not has_facts_ptr or not has_facts_cnt:
                errors.append(
                    f"{sid}: {mode} row has no expected_facts[]")
            else:
                kinds = pred_arrays.get(ef, [])
                if not kinds:
                    errors.append(
                        f"{sid}: expected_facts {ef} resolved to an empty/missing array")
                else:
                    non_tick = [k for k in kinds if k != "TickReached"]
                    if not non_tick:
                        errors.append(
                            f"{sid}: {ef} contains only TickReached predicates")

        # (c) discriminating_mutation must be non-default.
        if not mut or mut.startswith("{"):
            errors.append(
                f"{sid}: discriminating_mutation is default-constructed (require non-empty kMut_*)")
        elif mut not in mutations:
            errors.append(
                f"{sid}: discriminating_mutation references unknown {mut}")
        else:
            m = mutations[mut]
            if not m.get("file"):
                errors.append(f"{sid}: discriminating_mutation.{mut}.file empty")
            try:
                if int(m.get("line", "0")) <= 0:
                    errors.append(f"{sid}: discriminating_mutation.{mut}.line <= 0")
            except ValueError:
                errors.append(f"{sid}: discriminating_mutation.{mut}.line is not an int")
            if not m.get("from"):
                errors.append(f"{sid}: discriminating_mutation.{mut}.from empty")
            if not m.get("to"):
                errors.append(f"{sid}: discriminating_mutation.{mut}.to empty")
            if not m.get("rationale"):
                errors.append(f"{sid}: discriminating_mutation.{mut}.rationale empty")

        # (d) special_<family>_<idx>_scen*: caster precondition. idx>=2 needs
        #     stats_level >= (idx-1)*3+1 AND magicpoints >= 600 on a team-0
        #     SpawnSpec. idx omitted -> idx=1 -> no precondition required.
        sm = SPECIAL_RX.match(sid)
        if sm and sm.group("idx"):
            idx = int(sm.group("idx"))
            if idx >= 2:
                # Search the SpawnSpec the row points at (its kFamilySpawns_*
                # or kCasterSpawns_* identifier) inside the table text for a
                # team-0 entry meeting the level/MP floor. Phase 06 wires
                # full enforcement; until any slot->=2 row exists, this
                # check short-circuits.
                spawn_ref = re.search(r"(k\w*Spawns_\w+)", row["raw"])
                if not spawn_ref:
                    errors.append(
                        f"{sid}: idx>={idx} caster row missing kSpawns reference")
                else:
                    array_name = spawn_ref.group(1)
                    arr_body = _slice(
                        text,
                        f"inline constexpr SpawnSpec {array_name}[]",
                        "};",
                    )
                    if not arr_body:
                        errors.append(
                            f"{sid}: cannot find SpawnSpec array {array_name}")
                    else:
                        rows_in = re.findall(r"\{([^}]*)\}", arr_body)
                        ok_floor = False
                        floor_level = (idx - 1) * 3 + 1
                        for rr in rows_in:
                            parts = [p.strip() for p in rr.split(",")]
                            if len(parts) < 7:
                                continue
                            try:
                                team = int(parts[1])
                            except ValueError:
                                continue
                            if team != 0:
                                continue
                            try:
                                stats_level = int(parts[7]) if len(parts) > 7 else 0
                            except ValueError:
                                stats_level = 0
                            try:
                                magicpoints = int(parts[8]) if len(parts) > 8 else 0
                            except ValueError:
                                magicpoints = 0
                            if stats_level >= floor_level and magicpoints >= 600:
                                ok_floor = True
                                break
                        if not ok_floor:
                            errors.append(
                                f"{sid}: caster precondition not met "
                                f"(team-0 SpawnSpec stats_level>={floor_level} "
                                f"AND magicpoints>=600)")

    all_errors = (
        errors
        + effect_lint_errors
        + widening_lint_errors
        + dead_predicate_errors
        + vacuous_event_floor_errors
        + zero_zero_count_errors
        + family_alias_errors
        + caster_zero_min_errors
    )

    if all_errors:
        sys.stderr.write("lint_scenario_facts: FAIL\n")
        for e in all_errors:
            sys.stderr.write(f"  - {e}\n")
        return 1

    print(f"lint_scenario_facts: OK ({len(rows)} rows checked, "
          f"{sum(len(p) for p in pred_calls.values())} predicates)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
