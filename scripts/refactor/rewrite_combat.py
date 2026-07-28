#!/usr/bin/env python3
"""Hand-inlined combat_math copies -> og.combat.* / fused verbs (Stage 2).

Lane A bound eight combat_math.h helpers as og.combat.* (yell_radius,
stun_total, bomb_damage, cloak_total, glow_bonus, druid_faerie_lifetime,
skeleton_lifetime, ghost_raise_lifetime) plus the fused verb
ob:add_frozen_stun(n) == ob:s_set_frozen_delay(stun_total(
ob:s_frozen_delay_raw(), n)).  This tool deletes every hand-inlined Lua copy
in the corpus and rewrites its call sites onto the bindings.

The site list is CURATED — the full corpus audit behind it:

  orc.lua      local fn yell_radius   (160+20L cap 420)      -> og.combat.yell_radius
               local fn stun_total    + the raw/set pair     -> ob:add_frozen_stun
  cleric.lua   local fn glow_bonus    (110L cap 2200)        -> og.combat.glow_bonus
               local fn skeleton_lifetime  (og.soften copy)  -> og.combat.skeleton_lifetime
               local fn ghost_raise_lifetime (og.soften copy)-> og.combat.ghost_raise_lifetime
  thief.lua    inline og.soften(15*(L+1),210,300)            -> og.combat.bomb_damage
               inline max/min cloak accumulator              -> og.combat.cloak_total
  druid.lua    inline og.soften(50+40L,570,800)              -> og.combat.druid_faerie_lifetime

  NOT rewritten (checked, out of scope by design):
    thief.lua og.soften(75+25*diff,375,490) — thief charm duration, not one
      of the eight combat_math bindings.
    weapon_animate.lua s_set_frozen_delay(roll) — a RAW freeze SET (sprinkle
      §2.6), not the stun_total accumulator; add_frozen_stun would change it.
    slime.lua calculate_level/calculate_exp — guy.cpp helpers, unbound.
    effect_bomb.lua compute_explosion_range — effect.cpp helper, unbound.

Every curated operation must match EXACTLY ONCE or the tool aborts without
writing anything (a drifted site means the curation needs re-verifying, not
a silent partial rewrite).  Deleted local functions take their contiguous
leading comment block with them: those blocks say "no og.* binding exists",
which the binding just falsified, and per style S3 the op-sequence record
moves into the binding's C++ documentation.

Parity (OFF + ARMED) gates the applied batch; pinned-file line shifts need
the pin re-point + canary flip proof (orc/cleric/thief/druid all carry pins
— see s2_partition.json).
"""

from __future__ import annotations

import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lua_corpus import Source, find_calls, run_rewriter  # noqa: E402


# --------------------------------------------------------------------------
# curated operations
# --------------------------------------------------------------------------

# op kinds:
#   ("delete_fn", name, [required body substrings])
#   ("call_to_combat", name)          NAME(args) -> og.combat.NAME(args)
#   ("fuse_frozen_stun",)             local raw = R:s_frozen_delay_raw()
#                                     R:s_set_frozen_delay(stun_total(raw, E))
#                                       -> R:add_frozen_stun(E)
#   ("replace_lines", [stripped originals], [replacements (indent of first)])
SITES: dict[str, list[tuple]] = {
    "orc.lua": [
        ("delete_fn", "yell_radius", ["160 + 20 * level", "420"]),
        ("delete_fn", "stun_total", ["cur_raw < 0", "150"]),
        ("call_to_combat", "yell_radius"),
        ("fuse_frozen_stun",),
    ],
    "cleric.lua": [
        ("delete_fn", "glow_bonus", ["110 * level", "2200"]),
        ("delete_fn", "skeleton_lifetime", ["og.soften(125 + 40 * level, 645, 900)"]),
        ("delete_fn", "ghost_raise_lifetime", ["og.soften(150 + 40 * level, 670, 925)"]),
        ("call_to_combat", "glow_bonus"),
        ("call_to_combat", "skeleton_lifetime"),
        ("call_to_combat", "ghost_raise_lifetime"),
    ],
    "thief.lua": [
        ("replace_lines",
         ["-- og::combat::bomb_damage(L): §2.7 legacy 15*(L+1) below knee 210",
          "-- (= L13); ceiling 300",
          "newob:set_damage(og.soften(15 * (self:s_level() + 1), 210, 300))"],
         ["newob:set_damage(og.combat.bomb_damage(self:s_level()))"]),
        ("replace_lines",
         ["-- cloak: og::combat::cloak_total(cur, gain) inlined —",
          "-- new = max(cur, min(cur + gain, kInvisibilityCloakCap=350))",
          "local cur = self:invisibility_left()",
          "local gain = 20 + og.rand(20) * self:s_level()",
          "local summed = cur + gain",
          "local capped = math.min(summed, 350)",
          "self:set_invisibility_left(math.max(capped, cur))"],
         ["-- cloak",
          "local cur = self:invisibility_left()",
          "local gain = 20 + og.rand(20) * self:s_level()",
          "self:set_invisibility_left(og.combat.cloak_total(cur, gain))"]),
    ],
    "druid.lua": [
        ("replace_lines",
         ["-- og::combat::druid_faerie_lifetime (include/openglad/core/combat_math.h):",
          "-- soften(50 + 40*L, kFaerieLifeKnee=570, kFaerieLifeCeiling=800). No",
          "-- direct og.* binding exists; composed from the bound og.soften.",
          "alive:set_lifetime(og.soften(50 + 40 * self:s_level(), 570, 800))"],
         ["alive:set_lifetime(og.combat.druid_faerie_lifetime(self:s_level()))"]),
    ],
}


class Abort(Exception):
    pass


def _find_fn_block(src: Source, name: str) -> tuple[int, int]:
    """[first, last] 0-based inclusive line span of `local function name(...)`
    through its matching `end` at the same indent, plus the contiguous
    comment block directly above."""
    starts = [i for i, ln in enumerate(src.lines)
              if re.match(r"local function %s\(" % re.escape(name), ln)]
    if len(starts) != 1:
        raise Abort(f"local function {name}: found {len(starts)} definitions")
    first = starts[0]
    indent = ""
    last = None
    for j in range(first + 1, len(src.lines)):
        if re.fullmatch(r"end\s*", src.code_line(j)):
            last = j
            break
    if last is None:
        raise Abort(f"local function {name}: no closing end")
    top = first
    while top - 1 >= 0 and src.is_comment_line(top - 1):
        top -= 1
    return top, last


def transform(src: Source):
    ops = SITES.get(src.path.name)
    if not ops:
        return None, []
    notes: list[str] = []
    lines = list(src.lines)
    drop: set[int] = set()
    replace: dict[int, list[str]] = {}

    for op in ops:
        if op[0] == "delete_fn":
            _, name, must = op
            top, last = _find_fn_block(src, name)
            body = "\n".join(src.lines[top:last + 1])
            for frag in must:
                if frag not in body:
                    raise Abort(f"{name}: expected fragment {frag!r} missing "
                                "— curation drifted; refusing")
            for k in range(top, last + 1):
                drop.add(k)
            # swallow the following blank line when one precedes too
            if last + 1 < len(lines) and not lines[last + 1].strip() \
                    and top - 1 >= 0 and not lines[top - 1].strip():
                drop.add(last + 1)
            notes.append(f"deleted local function {name} "
                         f"(lines {top + 1}-{last + 1})")

        elif op[0] == "call_to_combat":
            _, name = op
            calls = [c for c in find_calls(src, name)
                     if not src.lines[src.line_of(c.start) - 1]
                     .lstrip().startswith("local function")]
            if not calls:
                raise Abort(f"{name}: no call sites found")
            for c in calls:
                idx = src.line_of(c.start) - 1
                if idx in drop:
                    continue
                new = lines[idx].replace(f"{name}(", f"og.combat.{name}(", 1)
                if new == lines[idx]:
                    raise Abort(f"{name}: call at line {idx + 1} not rewritten")
                lines[idx] = new
                notes.append(f"call site line {idx + 1}: {name} -> "
                             f"og.combat.{name}")

        elif op[0] == "fuse_frozen_stun":
            pat_raw = re.compile(
                r"(\s*)local (\w+) = (\w+):s_frozen_delay_raw\(\)\s*$")
            fused = 0
            for idx, ln in enumerate(lines):
                m = pat_raw.match(src.code_line(idx)) if idx < len(src.lines) else None
                if not m:
                    continue
                ind, rawvar, recv = m.groups()
                # find the set line, allowing comment lines between
                j = idx + 1
                while j < len(lines) and src.is_comment_line(j):
                    j += 1
                m2 = re.fullmatch(
                    re.escape(ind) + r"%s:s_set_frozen_delay\(stun_total\(%s, (\w+)\)\)\s*"
                    % (re.escape(recv), re.escape(rawvar)),
                    src.code_line(j) if j < len(src.lines) else "")
                if not m2:
                    continue
                uses = sum(1 for k, l2 in enumerate(lines)
                           if k not in (idx, j) and k not in drop
                           and re.search(r"\b%s\b" % re.escape(rawvar),
                                         src.code_line(k) if k < len(src.lines) else l2))
                if uses:
                    raise Abort(f"fuse_frozen_stun: {rawvar} used elsewhere")
                drop.add(idx)
                replace[j] = [f"{ind}{recv}:add_frozen_stun({m2.group(1)})"]
                notes.append(f"lines {idx + 1}/{j + 1}: raw+stun_total set "
                             f"-> {recv}:add_frozen_stun({m2.group(1)})")
                fused += 1
            if fused != 1:
                raise Abort(f"fuse_frozen_stun: matched {fused} sites, "
                            "expected exactly 1")

        elif op[0] == "replace_lines":
            _, originals, replacements = op
            stripped = [ln.strip() for ln in lines]
            hits = [k for k in range(len(lines) - len(originals) + 1)
                    if [s for s in stripped[k:k + len(originals)]] == originals]
            if len(hits) != 1:
                raise Abort(f"replace_lines: pattern starting {originals[0]!r} "
                            f"matched {len(hits)} times, expected 1")
            k = hits[0]
            indent = re.match(r"\s*", lines[k + len(originals) - 1]).group(0)
            replace[k] = [indent + r for r in replacements]
            for kk in range(k + 1, k + len(originals)):
                drop.add(kk)
            notes.append(f"lines {k + 1}-{k + len(originals)}: inline copy -> "
                         f"{replacements[-1].strip()}")

    out: list[str] = []
    for idx, ln in enumerate(lines):
        if idx in replace:
            out.extend(replace[idx])
        elif idx not in drop:
            out.append(ln)
    return out, notes


def safe_transform(src: Source):
    try:
        return transform(src)
    except Abort as a:
        print(f"rewrite_combat: ABORT in {src.path.name}: {a}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    run_rewriter("rewrite_combat",
                 "hand-inlined combat_math copies -> og.combat.* / fused verbs",
                 safe_transform)
