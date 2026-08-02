# Pack Lua Style Contract

Scope: all pack Lua — `packs/`, the example packs under `docs/modding/`, and
pack chunks embedded in C++ `R"LUA(` literals. This document is style only.
The determinism cookbook ([lua-classpacks-design.md](lua-classpacks-design.md)
§3, R1–R10) and the build lints (`scripts/check_lua_statement_lines.py`: one
statement per line, one short-circuit per line, one `function` keyword per
line; chunks compile text-only) are law underneath it and are not restated
here. Where style and cookbook appear to conflict, the cookbook wins.

This contract binds all in-tree pack Lua. `og.use`, `og.rand0`, walker
properties, fused verbs, and the arithmetic helpers described below are the
current API, not future migration steps.

## S1 — Naming

Locals name the quantity, not the register. `dx`, `dy`, `dist`, `foe`,
`corpse`, `target`, `found`, `stun` are the house style; `tempx`, `tempy`,
`newob`, and `generic` are 1995 register names and are banned, as is any new
`temp*`. Conventional indices (`i`, `j` in a numeric `for`) and bare
geometry (`x`, `y` where the value is a coordinate) are fine.

- Chunk-load constants are UPPER_SNAKE (`FX_BOOMERANG`), plus the
  conventional `local C = og.C` alias.
- Hook functions keep their hook-key names (`do_special`, `level_up`, …) so
  the registration table reads as identity pairs.
- RNG rolls are named for what is rolled (`level_roll`, `con_roll` — not
  `r1`, `r2`). Under R4 the draw order is contract, and the names carry that
  record through later refactors.

## S2 — File headers

The functional header is one line, ending with the cookbook pointer:

```lua
-- core:orc — yell stun, corpse eating (cookbook: docs/lua-classpacks-design.md §3).
```

Name the family (or the file's scope, for multi-family files) and what its
hooks actually do — text no other file's functional header could carry. A
concise shared copyright/porting credit may accompany it; that signed
heritage line is attribution, not boilerplate, and does not replace the
file-specific header. Do not add generic explanatory preambles: the cookbook
applies whether or not a file repeats it. Files that intentionally register
nothing (for example, a descriptor-driven family with no family-specific
hooks) keep the header plus a short explanation because registering an empty
hook table is a load error.

## S3 — Comments

Three kinds of comment are admissible:

1. **RNG-order records.** Eval-order adjudications (the `FLAGGED` convention:
   two draws in one C++ expression, stating which order parity chose), guard
   semantics, and notes on calls that look pure but draw (`attack()`,
   `query_object_passable()`, `og.charm_duration`, …). These are load-bearing
   determinism records; they move with the code they describe and survive
   every refactor.
2. **Why-comments.** Why a kept shim is kept (S5), why a branch is spelled
   out for coverage measurability under the one-statement lint, why two
   statements cannot be reordered.
3. **Heritage records.** Preserve concise authorship credits and distinctive
   original comments that explain the game's behavior or design history. The
   shared signed attribution is:
   `-- Copyright (C) 1995-2002 FSGames; ported by Sean Ford and Yan Shosh.`
   Keep established wording where it remains accurate; do not turn heritage
   comments into file-path archaeology or a change log.

Obsolete source-path narration (`-- transliterated from family_orc.cpp`)
cites deleted native sources, while line-by-line narration merely restates
the code; both are noise. The native operation sequence a shim chain
reproduces (`-- one float division, then trunc`) is a legitimate why-comment
while the chain survives; once a binding absorbs the sequence, the comment
belongs in the binding's C++ documentation instead.

## S4 — Helpers and modules

- A helper used by one file is a `local function` in that file, above first
  use.
- A helper used by two or more files goes in `packs/<id>/lib/<name>.lua`,
  loaded with `og.use("<name>")`: resolved within the loading pack, loaded
  once per VM before pack scripts in deterministic order, text-only, and
  budget-metered. Lib chunks export one table of pure functions and constants;
  chunk-level mutable state violates R6.
- Never share helpers through the pack's global environment. Scripts load
  filename-lexicographically into one shared environment per pack, so a
  `_G`-published helper works only by a load-order accident the reader
  cannot see; `og.use` makes the dependency explicit.
- New `lib/` files ride the MP pack-transfer manifest (protocol v10); a
  layout change must keep the manifest and pack-cache tests green.

## S5 — Arithmetic shims

The cookbook decides where a shim is required. Drop one only when audited
operand ranges prove the plain operation identical (float-representable
inputs and result for a removed `og.f*`; C and Lua division/remainder
semantics identical for a removed `og.div`/`og.mod`). Integer-valued inputs
and result within 2^24 are a common sufficient float proof. Style governs
what remains:

- **A kept shim site carries a one-line why.**
  `-- busy is a C++ float: per-op rounding` or
  `-- delta can be negative: C trunc, not Lua floor`.
- **Never hand-inline an engine helper.** If the formula exists as a
  `combat_math.h` constexpr, use its binding rather than copying the formula
  into Lua.
- **Use `og.rand0` instead of guard trios.** `og.rand0(n)` is exactly
  `IRandom::next(0)` semantics: `n <= 0` returns 0 without advancing the
  stream. Keep plain `og.rand` where the bound is provably positive — its
  error on `n <= 0` is a loud tripwire worth having.
- **Clamp ladders** (`if x < 0 then x = 0 end` chains, six-line min/max
  if/else) use `og.max`/`og.min`/`og.clamp`.

## S6 — Legacy `s_*` methods (compatibility policy)

The flattened stats accessors (`s_hitpoints()`/`s_set_hitpoints()`, and
getter-call parentheses generally) leak the 1995 struct layout into every
expression. The property layer exposes `self.hp`, `self.level`, and the
other documented properties with the same narrowing-setter semantics,
generated from the same registration table:

- New and refactored in-tree pack code uses properties. Fused verbs
  (`ob:add_frozen_stun(n)`, `self:heal_clamped(n, source)`) replace
  get→combine→set chains wherever a binding exists.
- The `s_*` methods remain supported aliases so out-of-tree packs keep
  working; removing them would be a pack-format compatibility break.

## Applying the contract

Changes to the core pack go through parity with the recorder off and armed,
the coverage report, and the pin-map check
(`scripts/parity/check_mutation_pins.py`). Pure renames are parity-neutral by
construction; run parity anyway. Every touched file must comply with the
whole contract.
