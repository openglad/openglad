#!/usr/bin/env python3
"""Lint tests/parity/scenario_table.h for Phase 01 semantic-parity contract.

Asserts:
  (a) expected_facts != nullptr iff fact_count > 0 (paired)
  (b) every ByteEqual / SemanticParity row has fact_count > 0 AND at
      least one non-TickReached predicate
  (c) every row has a non-default discriminating_mutation (file, line>0,
      from, to, rationale all non-empty), and any pin whose from-text is
      applicable on more than one line of its file carries a
      context_before that narrows the file to EXACTLY ONE line; a context
      that is equally true above several of the twins narrows nothing and
      is refused like a missing one
  (d) every special_<family>_<idx>_scen* row with idx >= 2 has a team-0
      caster SpawnSpec with stats_level >= (idx-1)*3+1 AND
      magicpoints >= 600 (the cycling and firing gate preconditions, cited
      in anchored form above `struct SpawnSpec` in the table; the 600 floor
      is the 500 MP non-sentinel cap + 100 MP headroom).
  (e) every kMut_* definition in the table is the discriminating_mutation
      of at least one ScenarioSpec row (branch-internal rows count): an
      orphan pin is a mutation nothing ever applies, so nothing measures
      whether it still has teeth
  (f) every ANCHORED source citation in tests/parity/*.h and *.cpp —
      ``src/<path>:<N> `<verbatim fragment>``` — still resolves: line N of
      <path> contains <fragment>. Bare citations (a line number with no
      backticked fragment) are outside the rule by construction.
  (g) every weapon predicate (WeaponFamilyEmitted, WeaponFamilyCount,
      WeaponSpeed, WeaponNetTravel) that names its family by a FAMILY_*
      constant names one of the weapon families in
      include/openglad/core/constants.h: the evaluator resolves the id in
      the weapon order, so an FX constant such as FAMILY_EXPLOSION (3)
      silently reads the weapon of the same number (FAMILY_FIREBALL); and
      every order-aware count (WalkerOfOrderFamilyCount,
      WalkerOfOrderFamilyAlive) writes its order as a kOrder* constant or
      its number and its family as a FAMILY_* constant or a number, and a
      FAMILY_* constant it names is a family of the order its args[1] gives

The lint is regex-based, not a full C++ parser. It expects the
scenario_table.h emitted by Phase 01 and breaks loudly on schema drift.

This lint reads only the C++ table; the matching JSON view at
``tests/parity/scenario_facts_generated.json`` may carry additional
optional keys per predicate (notably ``applies_to_branch`` and
``applies_to_master``, both default true). Those keys are emitted by
``scenario_facts_dump_main.cpp`` and are tolerated by this lint by virtue
of it not parsing the JSON at all — they pass through unobserved here.

Override the parsed table file via LINT_SCENARIO_TABLE=<path>.
``--self-test`` runs every rule on synthetic tables and pins each
verdict exactly (ctest entry ``lint_scenario_facts_selftest``).
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import re
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# The anchor rule itself, imported rather than re-derived, so this lint and
# the applier the canary actually runs cannot drift apart.
from _apply_mutation import anchor_lines  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_TABLE = REPO_ROOT / "tests" / "parity" / "scenario_table.h"


def _load_table(path: Path) -> str:
    if not path.is_file():
        sys.stderr.write(f"lint: cannot read {path}\n")
        sys.exit(2)
    return path.read_text(encoding="utf-8")


# Rule (g): the predicates whose family id the evaluator resolves in the
# weapon order (tests/parity/fact_predicate.cpp, family_symbol_by_order with
# kWeaponOrder), and where the weapon families are declared.
WEAPON_ORDER_PREDICATES = ("WeaponFamilyEmitted", "WeaponFamilyCount",
                           "WeaponSpeed", "WeaponNetTravel")
CONSTANTS_H = (Path(__file__).resolve().parents[2]
               / "include" / "openglad" / "core" / "constants.h")


# The order-aware counts (WalkerOfOrderFamilyCount, WalkerOfOrderFamilyAlive)
# carry their Order in args[1], spelt with the table's kOrder* constants
# (scenario_table.h, mirroring <openglad/core/order.h>).
ORDER_COUNT_PREDICATES = ("WalkerOfOrderFamilyCount",
                          "WalkerOfOrderFamilyAlive")
ORDER_ORDINALS = {"kOrderLiving": 0, "kOrderWeapon": 1, "kOrderTreasure": 2,
                  "kOrderGenerator": 3, "kOrderFX": 4}
ORDER_NAMES = {"kOrderLiving": "living", "kOrderWeapon": "weapon",
               "kOrderTreasure": "treasure", "kOrderGenerator": "generator",
               "kOrderFX": "FX"}
# Where constants.h declares each order's families: the comment that opens
# the order's block and the one that opens the block after it. The FAMILY_*
# names of all five blocks are one C++ namespace, so no name is declared in
# two of them.
ORDER_FAMILY_SECTIONS = {
    "kOrderLiving":    ("// Living families", "// # of families"),
    "kOrderWeapon":    ("//Weapon families", "// Treasure families"),
    "kOrderTreasure":  ("// Treasure families", "// Generator families"),
    "kOrderGenerator": ("// Generator families", "// FX families"),
    "kOrderFX":        ("// FX families", "// Special families"),
}


def order_family_symbols(
        path: Path = CONSTANTS_H) -> dict[str, dict[str, int]]:
    """{kOrder* token: {FAMILY_* name: id}} for the five orders."""
    if not path.is_file():
        sys.stderr.write(f"lint: cannot read {path}\n")
        sys.exit(2)
    text = path.read_text(encoding="utf-8")
    out: dict[str, dict[str, int]] = {}
    for order, (start, end) in ORDER_FAMILY_SECTIONS.items():
        names = {name: int(value) for name, value in re.findall(
            r"inline\s+constexpr\s+int\s+(FAMILY_\w+)\s*=\s*(\d+)\s*;",
            _slice(text, start, end))}
        if not names:
            sys.stderr.write(
                f"lint: no {ORDER_NAMES[order]} families found in {path}\n")
            sys.exit(2)
        out[order] = names
    return out


# A string literal (kept whole, so a `//` inside one is no comment), a line
# comment, or a block comment.
_COMMENT_RX = re.compile(r'"(?:\\.|[^"\\\n])*"|//[^\n]*|/\*.*?\*/', re.DOTALL)


def _strip_comments(text: str) -> str:
    """`text` with its // and /* */ comments removed. A block comment
    becomes one blank, as the compiler reads it; a line comment ends before
    its newline."""
    def keep(m: "re.Match[str]") -> str:
        tok = m.group(0)
        if tok.startswith('"'):
            return tok
        return " " if tok.startswith("/*") else ""
    return _COMMENT_RX.sub(keep, text)


def _bare(arg: str) -> str:
    """One argument with its comments and surrounding blanks removed."""
    return _strip_comments(arg).strip()


def order_token(arg: str) -> "str | None":
    """The kOrder* token an order argument names (a kOrder* constant or its
    ordinal), or None when it is neither."""
    s = _bare(arg)
    if s in ORDER_ORDINALS:
        return s
    n = parse_int_arg(s)
    for token, ordinal in ORDER_ORDINALS.items():
        if n == ordinal:
            return token
    return None


def family_value(arg: str, names: dict[str, int]) -> "int | None":
    """A family argument's id: an integer literal, or a FAMILY_* name found
    in `names`; None for anything else."""
    s = _bare(arg)
    n = parse_int_arg(s)
    if n is not None:
        return n
    return names.get(s)


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


def check_mutation_contexts(
        mutations: dict[str, dict[str, str]]) -> list[str]:
    """The mandatory-context rule, plus the anchor's own sanity check.

    A pin's from-text is replaced within ONE line, so when that text is
    applicable on several lines of the file the pin cannot say which one it
    means. Nothing goes red when a mechanical repin picks the wrong sibling:
    the anchor resolves, the mutation applies, and the scenario is guarded by
    an edit that flips nothing. So an ambiguous pin MUST carry a
    context_before — the line above the occurrence it means.

    And it must be a context that WORKS. Carrying one is not the rule; the
    rule is that the pin, read with its context, names exactly one line of the
    file. A context that is equally true above both twins — an opening brace,
    the header of the function both arms sit in — satisfies the applier at
    either of them and leaves the ambiguity exactly where it was.

    Also refuses a context that is itself one of the pin texts in the same
    file: an anchor that moves with the thing it is anchoring is no anchor.

    Files that cannot be read are left to the C++ gate, which fails on a
    missing mutation source outright.
    """
    errors: list[str] = []
    cache: dict[str, list[str] | None] = {}

    def lines_of(path: str) -> list[str] | None:
        if path not in cache:
            target = REPO_ROOT / path
            try:
                cache[path] = target.read_text(encoding="utf-8").splitlines()
            except OSError:
                cache[path] = None
        return cache[path]

    texts_by_file: dict[str, set[str]] = {}
    for m in mutations.values():
        if m.get("file"):
            bucket = texts_by_file.setdefault(m["file"], set())
            bucket.add(m.get("from", ""))
            bucket.add(m.get("to", ""))

    for name, m in sorted(mutations.items()):
        path = m.get("file", "")
        from_text = m.get("from", "")
        context = m.get("context_before", "")
        lines = lines_of(path) if path else None
        if lines is None or not from_text:
            continue
        if context and context in texts_by_file.get(path, set()):
            errors.append(
                f"{name}: context_before is itself a pin from/to text in "
                f"{path}; an anchor that a mutation can rewrite is no anchor")
        loose = anchor_lines(lines, from_text, "")
        if len(loose) <= 1:
            continue
        if not context:
            errors.append(
                f"{name}: from-text is applicable on {len(loose)} lines of "
                f"{path} ({', '.join(str(h) for h in loose[:8])}"
                f"{', ...' if len(loose) > 8 else ''}) and the pin carries no "
                f"context_before, so nothing says which one it means — add "
                f"the line above the occurrence it is about")
            continue
        # A context that narrows the file to NOTHING is a broken pin rather
        # than an ambiguous one; check_mutation_pins.py and the C++ gate both
        # say so, and with a better message. This rule owns the >1 case only.
        anchored = anchor_lines(lines, from_text, context)
        if len(anchored) > 1:
            errors.append(
                f"{name}: from-text is applicable on {len(loose)} lines of "
                f"{path} and the context_before is true above "
                f"{len(anchored)} of them "
                f"({', '.join(str(h) for h in anchored[:8])}"
                f"{', ...' if len(anchored) > 8 else ''}), so the pin still "
                f"does not say which occurrence it means — reach further up "
                f"for a line that sits above only one of them\n"
                f"    context_before: {context[:70]}")
    return errors


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


# compare_mode, is_branch_internal, spawns, spawn_count, player_team,
# is_intentionally_empty, fresh_arena: spawns and fresh_arena are captured.
_ROW_SPAWNS_ARENA_RX = re.compile(
    r"CompareMode::\w+\s*,\s*(?:true|false)\s*,"
    r"\s*([^,]+?)\s*,\s*[^,]+,\s*[^,]+,"
    r"\s*(?:true|false)\s*,\s*(true|false)\b")

_BRANCH_INTERNAL_RX = re.compile(
    r"CompareMode::(?:ByteEqual|SemanticParity|Invariant)\s*,\s*true\b"
)


def parse_scenarios(text: str, *,
                    include_branch_internal: bool = False) -> list[dict[str, str]]:
    """Return one dict per master-comparable row inside kScenarios[].

    Mirrors the companion's `list_scenarios()` filter in
    `tools/parity_dump_master.cpp` — rows marked `is_branch_internal=true`
    (the bool positional field after compare_mode) are skipped. Branch-internal
    scenarios cannot be cross-binary verified against master, so they are not
    counted by lint, canary, or the Phase 02 mirror-parity check.

    `include_branch_internal=True` keeps them, and every row then carries
    `branch_internal`. Two rules ask for that view. The
    orphan_mutation_constant rule: a branch-internal row still NAMES its pin,
    so the pin is not an orphan even though no canary mode exercises it.
    And policy P6's order-aware leg: the coverage gate reads every row's
    facts, so a treasure or generator count in a row only the current game
    runs still binds a family, and must name one the row spawns. Splitting
    the rows twice in two places is how the two views would drift apart, so
    they share this one.

    Every row also carries `spawns`, the spawn array its spawns field names
    ("nullptr" when it names none), and `fresh_arena`, read by position from
    the row with its comments removed. When those fields cannot be read,
    `spawns` is None and `fresh_arena` is True.
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
        branch_internal = bool(_BRANCH_INTERNAL_RX.search(r))
        if branch_internal and not include_branch_internal:
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
        # kFacts_* arrays, and a row that plays the Gameplay Fixes setting
        # spells the rest of its tail positionally after that
        # (`kMut_x, {}, 1, nullptr, 0, 1`): scalar tokens only, so the
        # leftmost match is the mutation, never the audit's `{}`. A
        # multi-floor tail (an identifier, std::size(...)) still matches
        # nothing and reads as default-constructed, as before.
        mut_match = re.search(
            r"(kMut_\w+|\{\s*\})"
            r"(?:\s*,\s*(?:\"(?:\\.|[^\"])*\"|\{\s*\}|nullptr|\d+))*\s*$",
            r.strip(),
            re.S,
        )
        mut_token = mut_match.group(1) if mut_match else ""
        # A row whose fields cannot be read counts as a fresh arena, the
        # shape of every row but one: P6 then asks for its spawns and says
        # so, instead of excusing the row in silence.
        fields = _ROW_SPAWNS_ARENA_RX.search(_strip_comments(r))
        out.append({
            "id":              sid,
            "raw":             r,
            "compare_mode":    mode,
            "expected_facts":  facts_name,
            "fact_count":      facts_count_expr,
            "mutation_token":  mut_token,
            "branch_internal": branch_internal,
            "spawns":          fields.group(1) if fields else None,
            "fresh_arena":     fields is None or fields.group(2) == "true",
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

# Policy P6's order-aware leg reads these orders only: in a row, nothing but
# the row's own spawns puts a treasure or a generator in the object list the
# order-aware counts read (see the leg in run_lint).
P6_SPAWN_ONLY_ORDERS = ("kOrderTreasure", "kOrderGenerator")


def parse_spawn_arrays(text: str,
                       family_ids: "dict[str, int] | None" = None
                       ) -> dict[str, list[dict]]:
    """Phase 04-prep — return {array_name: [{family, team, order,
    default_weapon, current_weapon}, ...]}.

    Used by `family_alias_mismatch` to verify that a behavioural
    predicate that names a treasure / weapon family-id has corresponding
    spawn-side evidence in the scenario's `SpawnSpec[]` array. We only
    need the first seven fields (family, team, order, x, y,
    default_weapon, current_weapon) so the parser ignores the
    Phase 01 tail fields (stats_level, magicpoints, precompleted_level).
    The table's comments are removed first, so a commented-out entry (or
    array) is no spawn. A family spelt as a FAMILY_* name resolves through
    `family_ids`; an entry whose family is neither a number nor a known name
    is dropped.
    """
    text = _strip_comments(text)
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
            family = family_value(seg_strs[0], family_ids or {})
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


def row_spawns(row: dict,
               spawn_arrays: dict[str, list[dict]]) -> "tuple[str, list[dict]]":
    """(name, entries) of the spawn array a row's spawns field names; ("",
    []) when it names none. Only a row whose fields cannot be read falls back
    to the first k*Spawns_* name in its text."""
    name = row["spawns"]
    if name is None:
        m = re.search(r"(k\w*Spawns_\w+)", row["raw"])
        name = m.group(1) if m else ""
    elif name == "nullptr":
        name = ""
    return name, spawn_arrays.get(name, [])


# Count predicates whose [min, max] pair does NOT sit at args[1]/args[2].
# WalkerOfOrderFamilyCount(family, order, min, max) and its live-only twin
# WalkerOfOrderFamilyAlive carry the Order ordinal in args[1], so their
# bounds are one position to the right; reading args[1] there would compare
# a min against an Order and silently exempt every widened range on the kind.
COUNT_BOUND_ARG_INDEX = {
    "WalkerFamilyCount":        (1, 2),
    "WeaponFamilyCount":        (1, 2),
    "WalkerOfTeamAlive":        (1, 2),
    "EffectFamilyCount":        (1, 2),
    "WalkerOfOrderFamilyCount": (2, 3),
    "WalkerOfOrderFamilyAlive": (2, 3),
}


def _classify_widened(kind: str, args: list[str]) -> "tuple[bool, str]":
    """Return (is_widened, human-readable predicate signature). Widened
    iff WalkerFamilyCount/WeaponFamilyCount/WalkerOfTeamAlive/
    WalkerOfOrderFamilyCount/WalkerOfOrderFamilyAlive with min != max OR
    WalkerHpRangeAtFinalTick with (arg2-arg1) > 200. Unparseable args
    short-circuit to "not widened" so the lint never false-fires on a novel
    call shape."""
    if kind in ("WalkerFamilyCount", "WeaponFamilyCount", "WalkerOfTeamAlive",
                "WalkerOfOrderFamilyCount", "WalkerOfOrderFamilyAlive"):
        lo_i, hi_i = COUNT_BOUND_ARG_INDEX[kind]
        if len(args) <= hi_i:
            return False, kind
        mn = parse_int_arg(args[lo_i])
        mx = parse_int_arg(args[hi_i])
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


# --- anchored citations ------------------------------------------------------

# `src/<path>:<N> `<verbatim fragment of that line>`` — the ANCHORED form of a
# source citation. The line number alone rots on the next insert above it and
# nothing notices; the backticked fragment gives the citation an oracle, which
# is this rule. A citation written WITHOUT a fragment is a bare citation and is
# outside the rule by construction: the regex needs the backticks to match.
ANCHORED_CITATION_RX = re.compile(
    r"(src/[A-Za-z0-9_./-]+\.(?:cpp|h)):(\d+) `([^`]+)`")


def check_anchored_citations(parity_dir: Path) -> list[str]:
    """Resolve every anchored citation in tests/parity/*.h and *.cpp.

    For each `src/<path>:<N> `<fragment>`` occurrence: <path> must exist,
    1 <= N <= its line count, and line N must CONTAIN <fragment> verbatim.
    When someone inserts a line above the cited one, the fragment stops
    matching and the build reds with the stale comment's own location —
    which is the whole point: a comment fix with no oracle rots again.
    """
    errors: list[str] = []
    sources = sorted(
        list(parity_dir.glob("*.h")) + list(parity_dir.glob("*.cpp")))
    cache: dict[str, "list[str] | None"] = {}
    for src_file in sources:
        try:
            text = src_file.read_text(encoding="utf-8")
        except OSError as exc:  # pragma: no cover - unreadable sibling
            errors.append(f"{src_file}: cannot read ({exc})")
            continue
        try:
            where = src_file.relative_to(REPO_ROOT).as_posix()
        except ValueError:
            where = src_file.as_posix()
        for lineno, line in enumerate(text.splitlines(), 1):
            for m in ANCHORED_CITATION_RX.finditer(line):
                cited_path, target_s, fragment = m.group(1), m.group(2), m.group(3)
                target = int(target_s)
                if cited_path not in cache:
                    cited_file = REPO_ROOT / cited_path
                    try:
                        cache[cited_path] = cited_file.read_text(
                            encoding="utf-8").splitlines()
                    except OSError:
                        cache[cited_path] = None
                cited_lines = cache[cited_path]
                if cited_lines is None:
                    errors.append(
                        f"{where}:{lineno}: anchored citation "
                        f"{cited_path}:{target} names a file that cannot be "
                        f"read (the cited file moved — re-point the citation)")
                    continue
                if not 1 <= target <= len(cited_lines):
                    errors.append(
                        f"{where}:{lineno}: anchored citation "
                        f"{cited_path}:{target} is outside the file "
                        f"(1..{len(cited_lines)}) (the cited line moved — "
                        f"re-point the citation)")
                    continue
                if fragment not in cited_lines[target - 1]:
                    errors.append(
                        f"{where}:{lineno}: anchored citation "
                        f"{cited_path}:{target} does not contain "
                        f"`{fragment}` (the cited line moved — re-point the "
                        f"citation)")
    return errors


# --- self-test --------------------------------------------------------------
#
# Eleven rules run inside run_lint() and nine of them are inline loops there,
# so nothing but a synthetic table can prove a rule still RUNS. The OK line on
# the real table cannot: a rule dropped from the concatenation at run_lint()'s
# tail leaves that line saying OK just as loudly as before.
#
# Every case pins the EXACT (rc, stdout, stderr) triple. That is the whole
# point. An expectation of "at least one error" stays green when a rule is
# broken to `if False:`, as long as some other rule still fires — which is how
# a self-test dies quietly (check_mutation_pins.py:1132-1141 records the same
# lesson about itself).

_SELF_TEST_CLEAN = '''inline constexpr Mutation kMut_a = {
    "src/fake.cpp", 10,
    "int x = 1;", "int x = 0;",
    "rationale a"
};
inline constexpr FactPredicate kFacts_one[] = {
    pred::TickReached(10),
    pred::WalkerFamilyCount(0, 1, 1),
};
inline constexpr ScenarioSpec kScenarios[] = {
    { "row_one", "scen/scen1.fss", 0x1u,
      nullptr, 0, 10,
      CompareMode::SemanticParity, false,
      nullptr, 0, 0, false, true,
      Exercises::None,
      kFacts_one, std::size(kFacts_one),
      kMut_a },
};
'''
# The baseline's pin names src/fake.cpp, which does NOT exist under the temp
# root, and that is deliberate: check_mutation_contexts skips a file it cannot
# read (see lines_of()), so the ambiguity rule stays silent on every case that
# did not ask for it. A case opts in by writing src/fake.cpp itself.


def _mutate(text: str, old: str, new: str) -> str:
    """One fixture edit, loud when its target is gone.

    A str.replace that matches nothing silently re-tests the baseline, so the
    case would keep passing while pinning nothing at all. Raising is the point.
    """
    if old not in text:
        raise AssertionError(
            f"self-test fixture edit target not found: {old!r}")
    return text.replace(old, new)


_ORPHAN_PIN = '''inline constexpr Mutation kMut_orphan = {
    "src/fake.cpp", 12,
    "int y = 1;", "int y = 0;",
    "rationale orphan"
};
'''

# A branch-internal row names its pin even though no canary mode can flip it,
# which is rule (e)'s second half: the pin is not an orphan.
_BRANCH_INTERNAL_ROW = '''    { "row_int", "scen/scen1.fss", 0x1u,
      nullptr, 0, 10,
      CompareMode::Invariant, true,
      nullptr, 0, 0, false, true,
      Exercises::None,
      kFacts_one, std::size(kFacts_one),
      kMut_orphan },
};
'''

# A multi-floor row's tail, branch-internal as the real ones are: floor count,
# paint array, paint count after the mutation and audit. Its mutation slot
# names kMut_orphan, so the orphan rule reads this row's own token: the tail
# carries an identifier and a call, which the parser does not read, so the row
# names no pin and kMut_orphan stays an orphan. A parser that swallowed any
# tail would read kMut_orphan here and turn the case green.
_Z_SHAPED_ROW = '''    { "row_z", "scen/scen1.fss", 0x1u,
      nullptr, 0, 10,
      CompareMode::Invariant, true,
      nullptr, 0, 0, false, true,
      Exercises::None,
      nullptr, 0, kMut_orphan, {},
      2, kZPaints, std::size(kZPaints) },
};
'''

_SPECIAL_ROW = '''    { "special_mage_2_scen1", "scen/scen1.fss", 0x1u,
      nullptr, 0, 10,
      CompareMode::SemanticParity, false,
      kSpawns_m, std::size(kSpawns_m), 0, false, true,
      Exercises::None,
      kFacts_one, std::size(kFacts_one),
      kMut_a },
'''


def _caster_spawns(stats_level: int, magicpoints: int) -> str:
    """The team-0 caster SpawnSpec rule (d) reads, in the table's own shape."""
    return ("inline constexpr SpawnSpec kSpawns_m[] = {\n"
            f"    {{3, 0, kOrderLiving, 100, 100, 0, 0, {stats_level}, "
            f"{magicpoints}}},\n"
            "};\n")


_TREASURE_SPAWNS = ('inline constexpr SpawnSpec kSpawns_t[] = {\n'
                    '    {5, 0, kOrderTreasure, 100, 100, 0, 0},\n'
                    '};\n')
_WEAPON_SPAWNS = ('inline constexpr SpawnSpec kSpawns_t[] = {\n'
                  '    {7, 0, kOrderWeapon, 100, 100, 0, 0},\n'
                  '};\n')
_WIELDER_SPAWNS = ('inline constexpr SpawnSpec kSpawns_t[] = {\n'
                   '    {0, 0, kOrderLiving, 100, 100, 7, 7},\n'
                   '};\n')
_GENERATOR_SPAWNS = ('inline constexpr SpawnSpec kSpawns_t[] = {\n'
                     '    {1, 0, kOrderGenerator, 100, 100, 0, 0},\n'
                     '};\n')
_DRUMSTICK_SPAWNS = ('inline constexpr SpawnSpec kSpawns_t[] = {\n'
                     '    {1, 0, kOrderTreasure, 100, 100, 0, 0},\n'
                     '};\n')
_NAMED_TREASURE_SPAWNS = ('inline constexpr SpawnSpec kSpawns_t[] = {\n'
                          '    {FAMILY_INVIS_POTION, 0, kOrderTreasure, 100, '
                          '100, 0, 0},\n'
                          '};\n')
# The invisibility potion twice, once in each comment form, and a living that
# is no evidence for it: a commented-out entry is no spawn.
_COMMENTED_TREASURE_SPAWNS = (
    'inline constexpr SpawnSpec kSpawns_t[] = {\n'
    '    // {FAMILY_INVIS_POTION, 0, kOrderTreasure, 100, 100, 0, 0},\n'
    '    /* {5, 0, kOrderTreasure, 100, 100, 0, 0}, */\n'
    '    {0, 0, kOrderLiving, 100, 100, 0, 0},\n'
    '};\n')
# A spawn array whose name is not spelt k*Spawns_*: only the row's spawns
# field says it is the row's.
_ARENA_PROPS = ('inline constexpr SpawnSpec kArenaProps[] = {\n'
                '    {5, 0, kOrderTreasure, 100, 100, 0, 0},\n'
                '};\n')
_POTION_FACT = ("pred::WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, "
                "kOrderTreasure, 1, 1),")

_FAKE_SRC = "int a;\nint firing_gate = 1;\nint b;\n"
_CITE_OK = "// gate: src/gameplay/living.cpp:2 `firing_gate = 1`\n"

_BASE_PRED = "pred::WalkerFamilyCount(0, 1, 1),"


def _pred(replacement: str) -> str:
    """The baseline with its one non-tick predicate swapped out."""
    return _mutate(_SELF_TEST_CLEAN, _BASE_PRED, replacement)


def _add_pred(extra: str) -> str:
    """The baseline with one more predicate after the non-tick one."""
    return _pred(_BASE_PRED + "\n    " + extra)


def _with_caster_row(stats_level: int, magicpoints: int) -> str:
    """The baseline plus a caster row (idx 2) and its spawn array."""
    table = _mutate(_SELF_TEST_CLEAN, '    { "row_one"',
                    _SPECIAL_ROW + '    { "row_one"')
    return _mutate(table, "inline constexpr ScenarioSpec",
                   _caster_spawns(stats_level, magicpoints)
                   + "inline constexpr ScenarioSpec")


def _with_spawns(table: str, spawns: str, name: str = "kSpawns_t") -> str:
    """Point row_one's spawn field at `name` and define that array."""
    table = _mutate(table, "nullptr, 0, 0, false, true,",
                    f"{name}, std::size({name}), 0, false, true,")
    return _mutate(table, "inline constexpr ScenarioSpec",
                   spawns + "inline constexpr ScenarioSpec")


def _with_branch_internal_potion_row() -> str:
    """The baseline plus a row only the current game runs (the
    _BRANCH_INTERNAL_ROW shape, naming kMut_a), whose own facts count an
    invisibility potion it never spawns."""
    row = _mutate(_mutate(_BRANCH_INTERNAL_ROW,
                          "kFacts_one, std::size(kFacts_one)",
                          "kFacts_int, std::size(kFacts_int)"),
                  "kMut_orphan", "kMut_a")
    table = _mutate(_SELF_TEST_CLEAN, "      kMut_a },\n};\n",
                    "      kMut_a },\n" + row)
    return _mutate(table, "inline constexpr ScenarioSpec",
                   "inline constexpr FactPredicate kFacts_int[] = {\n"
                   "    pred::TickReached(10),\n"
                   f"    {_POTION_FACT}\n"
                   "};\n"
                   "inline constexpr ScenarioSpec")


def _run_case(table: str | None,
              files: dict[str, str]) -> tuple[int, str, str, str]:
    """Run the whole lint over a synthetic repo root.

    The fixture table goes to <tmp>/tests/parity/scenario_table.h, so rule (f)
    takes that directory as its citation corpus and reports paths relative to
    the root rather than absolute ones, and REPO_ROOT is swapped to <tmp> for
    the duration so pinned and cited source files resolve inside the fixture
    too (the pattern check_mutation_pins.py already uses on this module).

    The two rc-2 paths raise SystemExit instead of returning, so they are
    caught here; the rc-0 and rc-1 paths are plain returns.
    """
    global REPO_ROOT
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        tpath = root / "tests" / "parity" / "scenario_table.h"
        tpath.parent.mkdir(parents=True, exist_ok=True)
        if table is not None:
            tpath.write_text(table, encoding="utf-8")
        for rel, body in files.items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(body, encoding="utf-8")
        out, err = io.StringIO(), io.StringIO()
        saved, REPO_ROOT = REPO_ROOT, root
        try:
            with contextlib.redirect_stdout(out), \
                    contextlib.redirect_stderr(err):
                try:
                    rc = run_lint(tpath)
                except SystemExit as exc:
                    rc = exc.code
        finally:
            REPO_ROOT = saved
        return rc, out.getvalue(), err.getvalue(), str(tpath)


def _ok(rows: int, predicates: int) -> str:
    return (f"lint_scenario_facts: OK ({rows} rows checked, "
            f"{predicates} predicates)\n")


def _fail(*messages: str) -> str:
    return ("lint_scenario_facts: FAIL\n"
            + "".join(f"  - {m}\n" for m in messages))


_OK = _ok(1, 2)
_ORPHAN_A = ("kMut_a: orphan mutation constant (referenced by no ScenarioSpec "
             "row) — attach it to a row whose facts flip under it, or delete "
             "it with a DRIFT_LEDGER 'Removed pins' note")

# name, table (None = do not write it), extra files, rc, stdout, stderr.
# `{table}` in an expected string stands for the temp table path; it is
# substituted before the comparison, which stays an exact string equality.
_SELF_TEST_CASES: list[
    tuple[str, str | None, dict[str, str], int, str, str]] = [
    ("clean", _SELF_TEST_CLEAN, {}, 0, _OK, ""),

    # (a) expected_facts and fact_count are paired.
    ("a_paired",
     _mutate(_SELF_TEST_CLEAN, "kFacts_one, std::size(kFacts_one)",
             "kFacts_one, 0"),
     {}, 1, "",
     _fail("row_one: expected_facts (kFacts_one) / fact_count (0) mismatched",
           "row_one: SemanticParity row has no expected_facts[]")),

    # (b) a compared row needs a predicate that is not a clock reading — and
    # the rule is scoped to the compared modes, which is the second case.
    ("b_tick_only",
     _mutate(_SELF_TEST_CLEAN, "    pred::WalkerFamilyCount(0, 1, 1),\n", ""),
     {}, 1, "",
     _fail("row_one: kFacts_one contains only TickReached predicates")),
    ("b_invariant_tick_only_ok",
     _mutate(
         _mutate(_SELF_TEST_CLEAN, "    pred::WalkerFamilyCount(0, 1, 1),\n",
                 ""),
         "CompareMode::SemanticParity, false", "CompareMode::Invariant, false"),
     {}, 0, _ok(1, 1), ""),
    ("b_missing_array",
     _mutate(_SELF_TEST_CLEAN, "kFacts_one, std::size(kFacts_one)",
             "kFacts_two, std::size(kFacts_two)"),
     {}, 1, "",
     _fail("row_one: expected_facts kFacts_two resolved to an empty/missing "
           "array")),

    # (c) a row's discriminating_mutation is a real, fully-filled pin. The
    # orphan line rides along on the first two: a row that names no pin (or an
    # unknown one) leaves kMut_a referenced by nobody.
    ("c_default", _mutate(_SELF_TEST_CLEAN, "      kMut_a },", "      {} },"),
     {}, 1, "",
     _fail("row_one: discriminating_mutation is default-constructed "
           "(require non-empty kMut_*)", _ORPHAN_A)),
    ("c_unknown",
     _mutate(_SELF_TEST_CLEAN, "      kMut_a },", "      kMut_zz },"),
     {}, 1, "",
     _fail("row_one: discriminating_mutation references unknown kMut_zz",
           _ORPHAN_A)),
    ("c_from_empty",
     _mutate(_SELF_TEST_CLEAN, '"int x = 1;", "int x = 0;"',
             '"", "int x = 0;"'),
     {}, 1, "", _fail("row_one: discriminating_mutation.kMut_a.from empty")),
    ("c_to_empty",
     _mutate(_SELF_TEST_CLEAN, '"int x = 1;", "int x = 0;"',
             '"int x = 1;", ""'),
     {}, 1, "", _fail("row_one: discriminating_mutation.kMut_a.to empty")),
    ("c_rationale_empty",
     _mutate(_SELF_TEST_CLEAN, '"rationale a"', '""'),
     {}, 1, "",
     _fail("row_one: discriminating_mutation.kMut_a.rationale empty")),
    ("c_line_zero",
     _mutate(_SELF_TEST_CLEAN, '"src/fake.cpp", 10,', '"src/fake.cpp", 0,'),
     {}, 1, "", _fail("row_one: discriminating_mutation.kMut_a.line <= 0")),

    # The row tail after the mutation. A coverage_audit string, a row that
    # plays the Gameplay Fixes setting (its whole tail spelled positionally,
    # `{}` audit included), and both at once all name their pin; the unknown
    # case proves the token read from that tail is the mutation, not the
    # audit's `{}`. A setting tail with a stray token is not read at all, so
    # the row fails as naming no pin rather than passing on a guess. A
    # multi-floor tail still names no pin, even with a kMut_* in its slot, so
    # the orphan rule fires for the pin that row holds.
    ("tail_audit_ok",
     _mutate(_SELF_TEST_CLEAN, "      kMut_a },", '      kMut_a, "audit" },'),
     {}, 0, _OK, ""),
    ("tail_gameplay_fixes_ok",
     _mutate(_SELF_TEST_CLEAN, "      kMut_a },",
             "      kMut_a, {}, 1, nullptr, 0, 1 },"),
     {}, 0, _OK, ""),
    ("tail_audit_and_gameplay_fixes_ok",
     _mutate(_SELF_TEST_CLEAN, "      kMut_a },",
             '      kMut_a, "audit", 1, nullptr, 0, 1 },'),
     {}, 0, _OK, ""),
    ("tail_gameplay_fixes_unknown",
     _mutate(_SELF_TEST_CLEAN, "      kMut_a },",
             "      kMut_zz, {}, 1, nullptr, 0, 1 },"),
     {}, 1, "",
     _fail("row_one: discriminating_mutation references unknown kMut_zz",
           _ORPHAN_A)),
    ("tail_gameplay_fixes_malformed",
     _mutate(_SELF_TEST_CLEAN, "      kMut_a },",
             "      kMut_a, {}, 1, nullptr, 0, x },"),
     {}, 1, "",
     _fail("row_one: discriminating_mutation is default-constructed "
           "(require non-empty kMut_*)", _ORPHAN_A)),
    ("tail_z_shape_names_no_pin",
     _mutate(_ORPHAN_PIN + _SELF_TEST_CLEAN, "      kMut_a },\n};\n",
             "      kMut_a },\n" + _Z_SHAPED_ROW),
     {}, 1, "",
     _fail("kMut_orphan: orphan mutation constant (referenced by no "
           "ScenarioSpec row) — attach it to a row whose facts flip under it, "
           "or delete it with a DRIFT_LEDGER 'Removed pins' note")),

    # The mandatory-context rule has four legs and two owners. The weak-context
    # leg ("the context_before is true above N of them") and the strong-context
    # green leg belong to check_mutation_pins.py's _self_test_context_uniqueness,
    # which calls check_mutation_contexts directly; repeating them here would be
    # the same rule tested twice. These two legs are the ones that owner does
    # not have, and they also prove run_lint() still concatenates this rule's
    # errors at all. The fixture below puts `int x = 0;` and `int x = 1;` on one
    # line each on purpose: the from-text is then unambiguous, so the weak- and
    # no-context branches cannot co-fire and hide the message being pinned.
    ("c_ambiguous_no_context", _SELF_TEST_CLEAN,
     {"src/fake.cpp": "int x = 1;\nint x = 1;\n"}, 1, "",
     _fail("kMut_a: from-text is applicable on 2 lines of src/fake.cpp (1, 2) "
           "and the pin carries no context_before, so nothing says which one "
           "it means — add the line above the occurrence it is about")),
    ("c_context_is_pin_text",
     _mutate(_SELF_TEST_CLEAN, '"rationale a"', '"rationale a", "int x = 0;"'),
     {"src/fake.cpp": "int x = 0;\nint x = 1;\n"}, 1, "",
     _fail("kMut_a: context_before is itself a pin from/to text in "
           "src/fake.cpp; an anchor that a mutation can rewrite is no "
           "anchor")),

    # (d) the caster preconditions of a special_<family>_<idx>_scen* row.
    ("d_missing_spawns",
     _mutate(_SELF_TEST_CLEAN, '"row_one"', '"special_mage_2_scen1"'),
     {}, 1, "",
     _fail("special_mage_2_scen1: idx>=2 caster row missing kSpawns "
           "reference")),
    ("d_floor_not_met", _with_caster_row(3, 600), {}, 1, "",
     _fail("special_mage_2_scen1: caster precondition not met (team-0 "
           "SpawnSpec stats_level>=4 AND magicpoints>=600)")),
    ("d_floor_met_ok", _with_caster_row(4, 600), {}, 0, _ok(2, 2), ""),

    # Policy P8: the caster of a caster row is alive and unique at tick 0, so a
    # (0, n) range over its own family asserts nothing.
    ("p8_caster_zero_min",
     _mutate(_with_caster_row(4, 600), "pred::WalkerFamilyCount(0, 1, 1),",
             "pred::WalkerFamilyCount(3, 0, 2), // intended_diff: caster may "
             "die in the crossfire; commit 0123456\n"),
     {}, 1, "",
     _fail("kFacts_one[#1] (scenario special_mage_2_scen1): caster_zero_min "
           "— WalkerFamilyCount(caster_family=3, 0, 2) vacuous; caster is "
           "alive + unique at scenario start, the only honest range is (1, 1) "
           "(policy P8)")),

    # unjustified_widening, over all three widenable kinds. The order kind is
    # here because its range arguments sit at 2 and 3, not 1 and 2.
    ("widen_unjustified", _pred("pred::WalkerFamilyCount(0, 1, 2),"), {}, 1, "",
     _fail("kFacts_one[#1]: widened predicate WalkerFamilyCount(arg0=0, 1, 2) "
           "missing `// intended_diff:` or `// rng_drift:` justification "
           "(grammar: kind ':' reason '; commit ' <hex 7..40>)")),
    ("widen_justified_ok",
     _pred("pred::WalkerFamilyCount(0, 1, 2), // intended_diff: survivor "
           "count varies with rng; commit 0123456\n"),
     {}, 0, _OK, ""),
    ("widen_order_kind",
     _pred("pred::WalkerOfOrderFamilyCount(0, 1, 1, 2),"), {}, 1, "",
     _fail("kFacts_one[#1]: widened predicate "
           "WalkerOfOrderFamilyCount(arg0=0, 1, 2) missing "
           "`// intended_diff:` or `// rng_drift:` justification (grammar: "
           "kind ':' reason '; commit ' <hex 7..40>)")),
    ("widen_order_alive_kind",
     _pred("pred::WalkerOfOrderFamilyAlive(0, 1, 1, 2),"), {}, 1, "",
     _fail("kFacts_one[#1]: widened predicate "
           "WalkerOfOrderFamilyAlive(arg0=0, 1, 2) missing "
           "`// intended_diff:` or `// rng_drift:` justification (grammar: "
           "kind ':' reason '; commit ' <hex 7..40>)")),
    ("widen_hp_span",
     _pred("pred::WalkerHpRangeAtFinalTick(0, 100, 301),"), {}, 1, "",
     _fail("kFacts_one[#1]: widened predicate "
           "WalkerHpRangeAtFinalTick(arg0=0, 100, 301) missing "
           "`// intended_diff:` or `// rng_drift:` justification (grammar: "
           "kind ':' reason '; commit ' <hex 7..40>)")),
    ("widen_hp_span_tight_ok",
     _pred("pred::WalkerHpRangeAtFinalTick(0, 100, 300),"), {}, 0, _OK, ""),

    # EffectFamilyCount must be qualified by a source family OR a tick window.
    ("effect_unqualified",
     _pred("pred::EffectFamilyCount(6, 1, 1, -1, 0),"), {}, 1, "",
     _fail("kFacts_one[#1]: unqualified EffectFamilyCount (arg3=-1, arg4=0); "
           "must have source-walker family (arg3>=0) OR tick-window upper "
           "(arg4>0)")),
    ("effect_source_ok",
     _pred("pred::EffectFamilyCount(6, 1, 1, 0, 0),"), {}, 0, _OK, ""),
    ("effect_window_ok",
     _pred("pred::EffectFamilyCount(6, 1, 1, -1, 5),"), {}, 0, _OK, ""),

    # dead_predicate: both nestings, and a single wrap that is perfectly alive.
    ("dead_branch_master",
     _pred("pred::branch_only(pred::master_only("
           "pred::WalkerFamilyCount(0, 1, 1))),"), {}, 1, "",
     _fail("kFacts_one[#1]: dead_predicate — branch_only wrapping master_only "
           "short-circuits both sides (applies_to_master=false AND "
           "applies_to_branch=false)")),
    ("dead_master_branch",
     _pred("pred::master_only(pred::branch_only("
           "pred::WalkerFamilyCount(0, 1, 1))),"), {}, 1, "",
     _fail("kFacts_one[#1]: dead_predicate — master_only wrapping branch_only "
           "short-circuits both sides (applies_to_master=false AND "
           "applies_to_branch=false)")),
    ("dead_single_wrap_ok",
     _pred("pred::branch_only(pred::WalkerFamilyCount(0, 1, 1)),"),
     {}, 0, _OK, ""),

    # vacuous_event_floor: "at least zero of them" is not an assertion.
    ("vacuous_floor", _add_pred("pred::EventKindAtLeast(0, 0),"), {}, 1, "",
     _fail("kFacts_one[#2]: vacuous_event_floor — EventKindAtLeast(arg0=0, 0) "
           "always passes; use EventKindAtLeast(*, 1) or "
           "EventKindExactly(*, n)")),
    ("vacuous_floor_one_ok", _add_pred("pred::EventKindAtLeast(0, 1),"),
     {}, 0, _ok(1, 3), ""),

    # Policy P3: a (0, 0) count is a negative assertion and must say so.
    ("zero_zero_no_negation",
     _add_pred("pred::WalkerFamilyCount(4, 0, 0),"), {}, 1, "",
     _fail("kFacts_one[#2]: zero_zero_count_no_negation — "
           "WalkerFamilyCount(arg0=4, 0, 0) needs inline "
           "`// negative_assertion: <reason>` comment between this predicate "
           "and the next (policy P3)")),
    ("zero_zero_negation_ok",
     _add_pred("pred::WalkerFamilyCount(4, 0, 0), // negative_assertion: no "
               "skeleton is spawned\n"), {}, 0, _ok(1, 3), ""),
    ("zero_zero_order_kind",
     _add_pred("pred::WalkerOfOrderFamilyCount(4, 1, 0, 0),"), {}, 1, "",
     _fail("kFacts_one[#2]: zero_zero_count_no_negation — "
           "WalkerOfOrderFamilyCount(arg0=4, 0, 0) needs inline "
           "`// negative_assertion: <reason>` comment between this predicate "
           "and the next (policy P3)")),
    ("zero_zero_order_alive_kind",
     _add_pred("pred::WalkerOfOrderFamilyAlive(4, 1, 0, 0),"), {}, 1, "",
     _fail("kFacts_one[#2]: zero_zero_count_no_negation — "
           "WalkerOfOrderFamilyAlive(arg0=4, 0, 0) needs inline "
           "`// negative_assertion: <reason>` comment between this predicate "
           "and the next (policy P3)")),
    ("zero_zero_order_alive_negation_ok",
     _add_pred("pred::WalkerOfOrderFamilyAlive(4, 1, 0, 0), "
               "// negative_assertion: the treasure is taken\n"),
     {}, 0, _ok(1, 3), ""),

    # Policy P6, family_alias_mismatch: a family id named by a predicate has to
    # be the family the row actually spawns, per order kind.
    # The order-aware leg: a treasure or generator count names a family the
    # row's spawns hold in that order, whether either side spells the family
    # as a number or a FAMILY_* name and whether the fact spells the order as
    # a kOrder* constant or its ordinal. A commented-out spawn is no spawn,
    # and the row's spawn array is the one its spawns field names, whatever
    # that array is called. A living in the spawns is no evidence for a
    # stain: the stain a death leaves sits in the effect list, which these
    # facts never count. A row that keeps the level's population is not
    # asked; a row whose arena flag cannot be read is, and so is a row only
    # the current game runs; an effect count is not.
    ("alias_order_treasure_no_spawn",
     _add_pred(_POTION_FACT), {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, kOrderTreasure) but "
           "<no spawn array> has no kOrderTreasure entry with family-id == 5, "
           "and the game makes none during a row that this fact can count "
           "(policy P6)")),
    ("alias_order_treasure_literal_no_spawn",
     _add_pred("pred::WalkerOfOrderFamilyAlive(5, kOrderTreasure, 1, 1),"),
     {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WalkerOfOrderFamilyAlive(5, kOrderTreasure) but <no spawn array> "
           "has no kOrderTreasure entry with family-id == 5, and the game "
           "makes none during a row that this fact can count (policy "
           "P6)")),
    ("alias_order_treasure_ordinal_no_spawn",
     _add_pred("pred::WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, 2, 1, 1),"),
     {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, kOrderTreasure) but "
           "<no spawn array> has no kOrderTreasure entry with family-id == 5, "
           "and the game makes none during a row that this fact can count "
           "(policy P6)")),
    ("alias_order_count_other_family",
     _with_spawns(_add_pred("pred::WalkerOfOrderFamilyCount(FAMILY_KEY, "
                            "kOrderTreasure, 1, 1),"), _TREASURE_SPAWNS),
     {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WalkerOfOrderFamilyCount(FAMILY_KEY, kOrderTreasure) but "
           "kSpawns_t has no kOrderTreasure entry with family-id == 11, and "
           "the game makes none during a row that this fact can count "
           "(policy P6)")),
    ("alias_order_treasure_ok",
     _with_spawns(_add_pred(_POTION_FACT), _TREASURE_SPAWNS),
     {}, 0, _ok(1, 3), ""),
    ("alias_order_treasure_named_spawn_ok",
     _with_spawns(_add_pred(_POTION_FACT), _NAMED_TREASURE_SPAWNS),
     {}, 0, _ok(1, 3), ""),
    ("alias_order_commented_out_spawn_is_no_evidence",
     _with_spawns(_add_pred(_POTION_FACT), _COMMENTED_TREASURE_SPAWNS),
     {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, kOrderTreasure) but "
           "kSpawns_t has no kOrderTreasure entry with family-id == 5, and "
           "the game makes none during a row that this fact can count "
           "(policy P6)")),
    ("alias_order_spawn_array_read_by_position_ok",
     _with_spawns(_add_pred(_POTION_FACT), _ARENA_PROPS, "kArenaProps"),
     {}, 0, _ok(1, 3), ""),
    ("alias_order_generator_other_order",
     _with_spawns(_add_pred("pred::WalkerOfOrderFamilyAlive(FAMILY_TOWER, "
                            "kOrderGenerator, 1, 1),"), _DRUMSTICK_SPAWNS),
     {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_TOWER, kOrderGenerator) but "
           "kSpawns_t has no kOrderGenerator entry with family-id == 1, and "
           "the game makes none during a row that this fact can count "
           "(policy P6)")),
    ("alias_order_generator_ok",
     _with_spawns(_add_pred("pred::WalkerOfOrderFamilyAlive(FAMILY_TOWER, "
                            "kOrderGenerator, 1, 1),"), _GENERATOR_SPAWNS),
     {}, 0, _ok(1, 3), ""),
    ("alias_order_stain_of_a_death_is_not_counted",
     _with_spawns(_add_pred("pred::WalkerOfOrderFamilyAlive(FAMILY_STAIN, "
                            "kOrderTreasure, 1, 1),"), _WIELDER_SPAWNS),
     {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_STAIN, kOrderTreasure) but "
           "kSpawns_t has no kOrderTreasure entry with family-id == 0, and "
           "the game makes none during a row that this fact can count "
           "(policy P6)")),
    ("alias_order_level_population_ok",
     _mutate(_add_pred(_POTION_FACT),
             "nullptr, 0, 0, false, true,", "nullptr, 0, 0, false, false,"),
     {}, 0, _ok(1, 3), ""),
    ("alias_order_unread_arena_is_asked",
     _mutate(_add_pred(_POTION_FACT),
             "nullptr, 0, 0, false, true,", "nullptr, 0, 0, false, kFresh,"),
     {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, kOrderTreasure) but "
           "<no spawn array> has no kOrderTreasure entry with family-id == 5, "
           "and the game makes none during a row that this fact can count "
           "(policy P6)")),
    ("alias_order_branch_internal_row_is_asked",
     _with_branch_internal_potion_row(), {}, 1, "",
     _fail("kFacts_int[#1] (scenario row_int): family_alias_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, kOrderTreasure) but "
           "<no spawn array> has no kOrderTreasure entry with family-id == 5, "
           "and the game makes none during a row that this fact can count "
           "(policy P6)")),
    ("alias_order_effect_not_asked_ok",
     _add_pred("pred::WalkerOfOrderFamilyAlive(FAMILY_FLASH, kOrderFX, 1, 1),"),
     {}, 0, _ok(1, 3), ""),
    # The weapon leg.
    ("alias_weapon_no_evidence",
     _with_spawns(_add_pred("pred::WeaponFamilyEmitted(7),"),
                  _TREASURE_SPAWNS), {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WeaponFamilyEmitted(7) but kSpawns_t has no kOrderLiving wielder "
           "with default_weapon==7 AND no kOrderWeapon entry with family-id "
           "== 7 paired with a `// scripted_spawn:` predicate-trail comment "
           "(policy P6)")),
    ("alias_weapon_scripted_no_comment",
     _with_spawns(_add_pred("pred::WeaponFamilyEmitted(7),"), _WEAPON_SPAWNS),
     {}, 1, "",
     _fail("kFacts_one[#2] (scenario row_one): family_alias_mismatch — "
           "WeaponFamilyEmitted(7) but kSpawns_t has no kOrderLiving wielder "
           "with default_weapon==7 AND no kOrderWeapon entry with family-id "
           "== 7 paired with a `// scripted_spawn:` predicate-trail comment "
           "(policy P6)")),
    ("alias_weapon_scripted_ok",
     _with_spawns(_add_pred("pred::WeaponFamilyEmitted(7), "
                            "// scripted_spawn: the script drops a bow\n"),
                  _WEAPON_SPAWNS), {}, 0, _ok(1, 3), ""),
    ("alias_weapon_wielder_ok",
     _with_spawns(_add_pred("pred::WeaponFamilyEmitted(7),"), _WIELDER_SPAWNS),
     {}, 0, _ok(1, 3), ""),

    # (g) family_order_mismatch: an FX constant in a weapon predicate is
    # refused; a weapon constant passes. An order-aware count is refused when
    # its FAMILY_* constant belongs to another order than the one it counts
    # in, and when its order or its family is written in a form this lint
    # cannot read; policy P6 then stays silent on it, so the fact gets one
    # line.
    ("g_fx_family_in_weapon_predicate",
     _add_pred("pred::WeaponFamilyCount(FAMILY_EXPLOSION, 1, 1),"), {}, 1, "",
     _fail("kFacts_one[#2]: family_order_mismatch — "
           "WeaponFamilyCount(FAMILY_EXPLOSION) resolves its family in the "
           "weapon order, and FAMILY_EXPLOSION is not a weapon family in "
           "include/openglad/core/constants.h; count an FX or treasure "
           "family with WalkerOfOrderFamilyCount")),
    ("g_order_kind_other_order",
     _add_pred("pred::WalkerOfOrderFamilyAlive(FAMILY_FLASH, kOrderTreasure, "
               "1, 1),"), {}, 1, "",
     _fail("kFacts_one[#2]: family_order_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_FLASH, kOrderTreasure) resolves "
           "its family in the treasure order, and FAMILY_FLASH is not a "
           "treasure family in include/openglad/core/constants.h; name a "
           "family of the order the fact counts in")),
    ("g_order_kind_unread_order",
     _add_pred("pred::WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, "
               "kOrderBogus, 1, 1),"), {}, 1, "",
     _fail("kFacts_one[#2]: family_order_mismatch — "
           "WalkerOfOrderFamilyAlive(FAMILY_INVIS_POTION, kOrderBogus) writes "
           "its order in a form this lint cannot read; write a kOrder* "
           "constant or its number")),
    ("g_order_kind_unread_family",
     _add_pred("pred::WalkerOfOrderFamilyAlive(kPotion, kOrderTreasure, "
               "1, 1),"), {}, 1, "",
     _fail("kFacts_one[#2]: family_order_mismatch — "
           "WalkerOfOrderFamilyAlive(kPotion, kOrderTreasure) writes its "
           "family in a form this lint cannot read; write a FAMILY_* "
           "constant or its number")),
    ("g_weapon_family_ok",
     _add_pred("pred::WeaponFamilyCount(FAMILY_FIREBALL, 1, 1),"), {}, 0,
     _ok(1, 3), ""),

    # (e) orphan_mutation_constant, both halves. The second row is
    # branch-internal, which the ordinary row view drops and this rule keeps:
    # the OK line still says 1 row, and the pin is no longer an orphan.
    ("e_orphan", _ORPHAN_PIN + _SELF_TEST_CLEAN, {}, 1, "",
     _fail("kMut_orphan: orphan mutation constant (referenced by no "
           "ScenarioSpec row) — attach it to a row whose facts flip under it, "
           "or delete it with a DRIFT_LEDGER 'Removed pins' note")),
    ("e_orphan_branch_internal_ref_ok",
     _mutate(_ORPHAN_PIN + _SELF_TEST_CLEAN, "      kMut_a },\n};\n",
             "      kMut_a },\n" + _BRANCH_INTERNAL_ROW),
     {}, 0, _OK, ""),

    # (f) anchored_citation. The five negative legs are the phase-3 planted
    # breaks, fixture-sized: a fragment that moved, a line past the end of the
    # file, a cited file that is gone, a src/ insert that reds BOTH citing
    # files at once (the real regression the rule exists for), and a citation
    # inside the table itself. The bare citation is the documented scope: the
    # regex needs the backticks, so it is ignored by construction.
    ("f_resolves_ok", _SELF_TEST_CLEAN,
     {"src/gameplay/living.cpp": _FAKE_SRC, "tests/parity/sib.cpp": _CITE_OK},
     0, _OK, ""),
    ("f_fragment_moved", _SELF_TEST_CLEAN,
     {"src/gameplay/living.cpp": _FAKE_SRC,
      "tests/parity/sib.cpp":
          "// gate: src/gameplay/living.cpp:1 `firing_gate = 1`\n"},
     1, "",
     _fail("tests/parity/sib.cpp:1: anchored citation "
           "src/gameplay/living.cpp:1 does not contain `firing_gate = 1` "
           "(the cited line moved — re-point the citation)")),
    ("f_outside_file", _SELF_TEST_CLEAN,
     {"src/gameplay/living.cpp": _FAKE_SRC,
      "tests/parity/sib.cpp":
          "// gate: src/gameplay/living.cpp:9 `firing_gate = 1`\n"},
     1, "",
     _fail("tests/parity/sib.cpp:1: anchored citation "
           "src/gameplay/living.cpp:9 is outside the file (1..3) (the cited "
           "line moved — re-point the citation)")),
    ("f_file_missing", _SELF_TEST_CLEAN,
     {"tests/parity/sib.cpp": _CITE_OK}, 1, "",
     _fail("tests/parity/sib.cpp:1: anchored citation "
           "src/gameplay/living.cpp:2 names a file that cannot be read (the "
           "cited file moved — re-point the citation)")),
    ("f_bare_ignored_ok", _SELF_TEST_CLEAN,
     {"src/gameplay/living.cpp": _FAKE_SRC,
      "tests/parity/sib.cpp":
          "// bare src/gameplay/living.cpp:1 firing_gate = 1 no backticks\n"},
     0, _OK, ""),
    ("f_src_insert_moves_both", _SELF_TEST_CLEAN,
     {"src/gameplay/living.cpp": "int inserted;\n" + _FAKE_SRC,
      "tests/parity/sib.cpp": _CITE_OK,
      "tests/parity/sib2.h":
          "/* gate: src/gameplay/living.cpp:2 `firing_gate = 1` */\n"},
     1, "",
     _fail("tests/parity/sib.cpp:1: anchored citation "
           "src/gameplay/living.cpp:2 does not contain `firing_gate = 1` "
           "(the cited line moved — re-point the citation)",
           "tests/parity/sib2.h:1: anchored citation "
           "src/gameplay/living.cpp:2 does not contain `firing_gate = 1` "
           "(the cited line moved — re-point the citation)")),
    ("f_in_table_itself", _CITE_OK + _SELF_TEST_CLEAN,
     {"src/gameplay/living.cpp": "int inserted;\n" + _FAKE_SRC}, 1, "",
     _fail("tests/parity/scenario_table.h:1: anchored citation "
           "src/gameplay/living.cpp:2 does not contain `firing_gate = 1` "
           "(the cited line moved — re-point the citation)")),

    # The exit-code contract: 1 for a table the lint understood and refused,
    # 2 for a table it could not read at all.
    ("rc_no_rows",
     'inline constexpr Mutation kMut_a = {"src/fake.cpp", 1, "a", "b", "c"};\n'
     'inline constexpr ScenarioSpec kScenarios[] = {\n};\n',
     {}, 1, "", "lint: no scenario rows parsed\n"),
    ("rc_no_block",
     'inline constexpr Mutation kMut_a = {"src/fake.cpp", 1, "a", "b", "c"};\n',
     {}, 2, "", "lint: could not locate kScenarios[] block\n"),
    ("rc_missing_table", None, {}, 2, "", "lint: cannot read {table}\n"),
]

_SELF_TEST_CASE_COUNT = 78


def _self_test() -> int:
    """Every rule, driven end to end on a synthetic table, verdict by verdict.

    The count constant is asserted against the table because a case table can
    also fail by shrinking: a deleted row takes its rule's only proof with it
    and nothing else notices.
    """
    failures: list[str] = []
    if len(_SELF_TEST_CASES) != _SELF_TEST_CASE_COUNT:
        failures.append(
            f"case table has {len(_SELF_TEST_CASES)} entries but "
            f"_SELF_TEST_CASE_COUNT says {_SELF_TEST_CASE_COUNT} — a case was "
            f"added or removed; update both")

    seen: set[str] = set()
    for name, table, files, want_rc, want_out, want_err in _SELF_TEST_CASES:
        if name in seen:
            failures.append(
                f"{name}: duplicate case name — a copy-pasted row shadows "
                f"nothing but still counts toward _SELF_TEST_CASE_COUNT")
        seen.add(name)
        rc, out, err, table_path = _run_case(table, files)
        want_err = want_err.replace("{table}", table_path)
        for field, got, want in (("rc", rc, want_rc),
                                 ("stdout", out, want_out),
                                 ("stderr", err, want_err)):
            if got != want:
                failures.append(
                    f"{name}.{field}: expected {want!r}, got {got!r}")

    for failure in failures:
        sys.stderr.write(f"lint_scenario_facts self-test: {failure}\n")
    if failures:
        sys.stderr.write(
            f"lint_scenario_facts self-test: {len(failures)} failure(s)\n")
        return 1
    print(f"lint_scenario_facts self-test: OK "
          f"({len(_SELF_TEST_CASES)} cases)")
    return 0


def run_lint(table: Path) -> int:
    text = _load_table(table)

    # Spec-mandated rule (Q12) — anchored_citation. Source citations in the
    # parity harness rot silently on every insert above the cited line; the
    # anchored form carries a verbatim fragment so this lint can resolve it.
    anchored_citation_errors = check_anchored_citations(table.parent)

    pred_arrays = parse_predicate_arrays(text)
    pred_calls  = parse_predicate_calls(text)
    mutations   = parse_mutation_constants(text)
    rows        = parse_scenarios(text)
    all_rows    = parse_scenarios(text, include_branch_internal=True)
    family_symbols = order_family_symbols()
    family_ids = {name: fid for names in family_symbols.values()
                  for name, fid in names.items()}
    spawn_arrays = parse_spawn_arrays(text, family_ids)

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

    # Rule (g) — family_order_mismatch. A weapon predicate resolves its
    # family id in the weapon order, so a FAMILY_* constant of another
    # order (FX, treasure, generator) compiles, lints and silently reads
    # the weapon that shares its number. Refuse a non-weapon name.
    weapon_symbols = set(family_symbols["kOrderWeapon"])
    family_order_errors: list[str] = []
    for arr_name, preds in pred_calls.items():
        for i, pred in enumerate(preds):
            if pred["kind"] not in WEAPON_ORDER_PREDICATES or not pred["args"]:
                continue
            sym = re.sub(r"/\*.*?\*/", "", pred["args"][0],
                         flags=re.DOTALL).strip()
            if re.fullmatch(r"FAMILY_\w+", sym) and sym not in weapon_symbols:
                family_order_errors.append(
                    f"{arr_name}[#{i}]: family_order_mismatch — "
                    f"{pred['kind']}({sym}) resolves its family in the weapon "
                    f"order, and {sym} is not a weapon family in "
                    f"include/openglad/core/constants.h; count an FX or "
                    f"treasure family with WalkerOfOrderFamilyCount")
    # The same mistake in an order-aware count: it resolves its family in
    # the order named in args[1], so a FAMILY_* constant of another order
    # reads the family of that order that shares its number. An order or a
    # family written in a form this lint cannot read is refused as well, in
    # every order: policy P6 reads the same two arguments, and a fact whose
    # spelling it cannot read would pass it unasked.
    for arr_name, preds in pred_calls.items():
        for i, pred in enumerate(preds):
            if pred["kind"] not in ORDER_COUNT_PREDICATES:
                continue
            args = pred["args"]
            sym = _bare(args[0]) if args else ""
            order_arg = _bare(args[1]) if len(args) > 1 else ""
            order = order_token(order_arg)
            head = f"{arr_name}[#{i}]: family_order_mismatch — {pred['kind']}"
            if order is None:
                family_order_errors.append(
                    f"{head}({sym}, {order_arg}) writes its order in a form "
                    f"this lint cannot read; write a kOrder* constant or its "
                    f"number")
            elif re.fullmatch(r"FAMILY_\w+", sym):
                if sym not in family_symbols[order]:
                    family_order_errors.append(
                        f"{head}({sym}, {order}) resolves its family in the "
                        f"{ORDER_NAMES[order]} order, and {sym} is not a "
                        f"{ORDER_NAMES[order]} family in "
                        f"include/openglad/core/constants.h; name a family "
                        f"of the order the fact counts in")
            elif parse_int_arg(sym) is None:
                family_order_errors.append(
                    f"{head}({sym}, {order}) writes its family in a form "
                    f"this lint cannot read; write a FAMILY_* constant or "
                    f"its number")

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
    # `WalkerFamilyCount(F, 0, 0)`, `WeaponFamilyCount(F, 0, 0)`,
    # `EffectFamilyCount(F, 0, 0)`,
    # `WalkerOfOrderFamilyCount(F, order, 0, 0)` or
    # `WalkerOfOrderFamilyAlive(F, order, 0, 0)` predicate is an assertion
    # that the family is absent (the live-only kind: that none is alive). That is
    # only honest as a paired negative assertion (policy P3), so the
    # source row MUST carry an inline `// negative_assertion: <reason>`
    # comment that the lint can grep for. Absent comment -> violation.
    zero_zero_count_errors: list[str] = []
    for arr_name, preds in pred_calls.items():
        for i, pred in enumerate(preds):
            kind = pred["kind"]
            if kind not in ("WalkerFamilyCount", "WeaponFamilyCount",
                            "EffectFamilyCount", "WalkerOfOrderFamilyCount",
                            "WalkerOfOrderFamilyAlive"):
                continue
            args = pred["args"]
            lo_i, hi_i = COUNT_BOUND_ARG_INDEX[kind]
            if len(args) <= hi_i:
                continue
            mn = parse_int_arg(args[lo_i])
            mx = parse_int_arg(args[hi_i])
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

        spawn_name, spawns = row_spawns(row, spawn_arrays)

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

        # family_alias_mismatch, the weapon leg: a WeaponFamilyEmitted(F)
        # predicate must have spawn-side evidence in the scenario's
        # SpawnSpec[] array: EITHER (a) order=kOrderWeapon and family=F AND
        # the predicate carries an inline `// scripted_spawn: ...` comment
        # that explains the scripted spawn; OR (b) a kOrderLiving entry
        # whose default_weapon=F (set_default_weapon). It reads F only when
        # the predicate writes it as a number. Every weapon fact in the
        # table writes a FAMILY_* name, so this leg asks none of them, and
        # it must not learn the names: a living fires its own family's
        # weapon whatever its SpawnSpec says, and specials and deaths make
        # weapons too, so most of the table's weapon facts have no such
        # evidence and are right.
        for i, pred in enumerate(preds):
            kind = pred["kind"]
            args = pred["args"]
            if not args:
                continue
            family = parse_int_arg(args[0])
            if family is None:
                continue
            trail = pred.get("trail", "")
            if kind == "WeaponFamilyEmitted":
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

    # family_alias_mismatch, the order-aware leg: a treasure or generator
    # count (WalkerOfOrderFamilyCount / WalkerOfOrderFamilyAlive) names a
    # family the row's own spawns hold in that order. These facts count the
    # object list, and in a row nothing else puts a treasure or a generator
    # there: the game makes no generator during play; the stain a dying
    # living leaves goes to the effect list (walker::generate_bloodspot,
    # add_fx_ob), which they do not read; and a life gem drops only from a
    # walker with a character record (walker::death, `if (myguy)`), which no
    # SpawnSpec gives. A row that keeps the level's population (fresh_arena
    # false) is not asked: the level file may hold either, and this lint does
    # not read it. Weapon, effect and living counts are not asked: the game
    # makes those itself (attacks, specials, deaths, summons). Rows only the
    # current game runs are asked too (see parse_scenarios).
    for row in all_rows:
        facts_ref = row["expected_facts"]
        if facts_ref == "nullptr" or not row["fresh_arena"]:
            continue
        spawn_name, spawns = row_spawns(row, spawn_arrays)
        for i, pred in enumerate(pred_calls.get(facts_ref, [])):
            if (pred["kind"] not in ORDER_COUNT_PREDICATES
                    or len(pred["args"]) < 2):
                continue
            order = order_token(pred["args"][1])
            if order not in P6_SPAWN_ONLY_ORDERS:
                continue
            sym = _bare(pred["args"][0])
            family = family_value(sym, family_symbols[order])
            if family is None:
                continue  # a spelling or another order's name: rule (g)
            if any(sp["order"] == order and sp["family"] == family
                   for sp in spawns):
                continue
            family_alias_errors.append(
                f"{facts_ref}[#{i}] (scenario {row['id']}): "
                f"family_alias_mismatch — {pred['kind']}({sym}, {order}) but "
                f"{spawn_name or '<no spawn array>'} has no {order} entry "
                f"with family-id == {family}, and the game makes none during "
                f"a row that this fact can count (policy P6)")

    # Spec-mandated rule (Q10) — orphan_mutation_constant. A kMut_* that
    # no row names is a pin nothing ever applies: check_mutation_pins keeps
    # its anchor honest, but no canary run ever asks whether it still has
    # teeth, and the rationale is free to describe flips that stopped
    # happening years ago. Branch-internal rows count as references — their
    # pin is named, even though the Invariant compare cannot flip on it.
    orphan_mutation_errors: list[str] = []
    referenced_mutations = {
        r["mutation_token"] for r in all_rows if r["mutation_token"]
    }
    for mut_name in sorted(mutations):
        if mut_name in referenced_mutations:
            continue
        orphan_mutation_errors.append(
            f"{mut_name}: orphan mutation constant (referenced by no "
            f"ScenarioSpec row) — attach it to a row whose facts flip under "
            f"it, or delete it with a DRIFT_LEDGER 'Removed pins' note")

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
        + check_mutation_contexts(mutations)
        + effect_lint_errors
        + widening_lint_errors
        + dead_predicate_errors
        + vacuous_event_floor_errors
        + zero_zero_count_errors
        + family_alias_errors
        + family_order_errors
        + caster_zero_min_errors
        + orphan_mutation_errors
        + anchored_citation_errors
    )

    if all_errors:
        sys.stderr.write("lint_scenario_facts: FAIL\n")
        for e in all_errors:
            sys.stderr.write(f"  - {e}\n")
        return 1

    print(f"lint_scenario_facts: OK ({len(rows)} rows checked, "
          f"{sum(len(p) for p in pred_calls.values())} predicates)")
    return 0


# --- CLI --------------------------------------------------------------------

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--self-test", action="store_true",
        help="run every rule on synthetic tables and exit 0/1 "
             "(ctest: lint_scenario_facts_selftest)")
    args = ap.parse_args(argv)

    if args.self_test:
        return _self_test()
    # A bare invocation still means "lint the real table": the
    # check_scenario_facts build target passes no flags.
    table_env = os.environ.get("LINT_SCENARIO_TABLE")
    return run_lint(Path(table_env) if table_env else DEFAULT_TABLE)


if __name__ == "__main__":
    sys.exit(main())
