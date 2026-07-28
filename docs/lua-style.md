# Pack Lua Style Contract

Scope: all pack Lua — `packs/`, the example packs under `docs/modding/`, and
pack chunks embedded in C++ `R"LUA(` literals. This document is style only.
The determinism cookbook ([lua-classpacks-design.md](lua-classpacks-design.md)
§3, R1–R10) and the build lints (`scripts/check_lua_statement_lines.py`: one
statement per line, one short-circuit per line, one `function` keyword per
line; chunks compile text-only) are law underneath it and are not restated
here. Where style and cookbook appear to conflict, the cookbook wins.

This contract is a Stage-0 deliverable of the
[quality plan](lua-quality-plan.md). S1–S3 bind immediately for new pack code
and for the shipped corpus as Stage-2 batches touch it; S4–S6 reference
Stage-1 machinery (`og.use`, `og.rand0`, the property layer) and bind as it
lands. Until then the transliterated corpus is grandfathered.

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

One line, ending with the cookbook pointer:

```lua
-- core:orc — yell stun, corpse eating (cookbook: docs/lua-classpacks-design.md §3).
```

Name the family (or the file's scope, for multi-family files) and what its
hooks actually do — text no other file's header could carry. The old
four-line boilerplate header, identical across all 36 files, is deleted by
Stage 2: the cookbook applies to every pack chunk whether or not the file
says so. Files that intentionally register nothing (tower1.lua — every C++
callback was null, and registering an empty hook table is a load error) keep
the header plus the comment explaining why the chunk is empty.

## S3 — Comments

Two kinds of comment are admissible:

1. **RNG-order records.** Eval-order adjudications (the `FLAGGED` convention:
   two draws in one C++ expression, stating which order parity chose), guard
   semantics, and notes on calls that look pure but draw (`attack()`,
   `query_object_passable()`, `og.charm_duration`, …). These are load-bearing
   determinism records; they move with the code they describe and survive
   every refactor.
2. **Why-comments.** Why a kept shim is kept (S5), why a branch is spelled
   out for coverage measurability under the one-statement lint, why two
   statements cannot be reordered.

Everything else goes. Provenance (`-- transliterated from family_orc.cpp`)
cites C++ sources the §9a retirement deleted; narration restates the next
line; both are noise. The C++ op sequence a shim chain reproduces
(`-- one float division, then trunc`) is a legitimate why-comment while the
chain survives; once a Stage-1 binding absorbs the sequence, the comment
moves into the binding's C++ documentation and dies here.

## S4 — Helpers and modules

- A helper used by one file is a `local function` in that file, above first
  use.
- A helper used by two or more files goes in `packs/<id>/lib/<name>.lua`,
  loaded with `og.use("<name>")` (Stage 1): resolved within the loading pack,
  loaded once per VM in deterministic order at pack install, text-only,
  budget-metered. Lib chunks export one table of pure functions and
  constants; chunk-level mutable state is an R6 violation the lib lint
  rejects at the boundary.
- Never share helpers through the pack's global environment. Scripts load
  filename-lexicographically into one shared environment per pack, so a
  `_G`-published helper works only by a load-order accident the reader
  cannot see; `og.use` makes the dependency explicit.
- Until `og.use` lands, duplication stays. Do not invent ad-hoc sharing.
- New `lib/` files ride the MP pack-transfer manifest (protocol v10); a
  layout change re-verifies the manifest and the pack-cache regeneration
  tests (plan §2.7).

## S5 — Arithmetic shims

The cookbook decides where a shim is required; the Stage-2 audit tool decides
where one can be dropped (operands provably integer-valued and < 2^24 for
`og.f*`; both operands provably non-negative for `og.div`/`og.mod`) — by
proof through the prober, never by eyeball. Style governs what remains:

- **A kept shim site carries a one-line why.**
  `-- busy is a C++ float: per-op rounding` or
  `-- delta can be negative: C trunc, not Lua floor`. After the Stage-2
  audit, a shim with no why is a lint finding (Stage 5).
- **Never hand-inline an engine helper.** If the formula exists as a
  `combat_math.h` constexpr, the fix is a binding (Stage 1), not a Lua copy
  of it.
- **Guard trios are dead once `og.rand0` lands.** `og.rand0(n)` is exactly
  `IRandom::next(0)` semantics: `n <= 0` returns 0 without advancing the
  stream. Keep plain `og.rand` where the bound is provably positive — its
  error on `n <= 0` is a loud tripwire worth having.
- **Clamp ladders** (`if x < 0 then x = 0 end` chains, six-line min/max
  if/else) become `og.max`/`og.min`/`og.clamp` once bound; the branch then
  lives in C++, where gcov measures it.

## S6 — Legacy `s_*` methods (deprecation policy)

The flattened stats accessors (`s_hitpoints()`/`s_set_hitpoints()`, and
getter-call parentheses generally) leak the 1995 struct layout into every
expression. Once the Stage-1 property layer lands (`self.hp`, `self.level`,
`self.busy`, … with the same narrowing-setter semantics, generated from the
same registration table):

- New and refactored in-tree pack code uses properties. Fused verbs
  (`ob:add_frozen_stun(n)`, `self:heal_clamped(n, source)`) replace
  get→combine→set chains wherever a binding exists.
- The `s_*` methods stay as functional aliases indefinitely: out-of-tree
  packs keep working, and removing them would be a pack-format break nobody
  has scheduled.
- Stage 5 removes them from the docs, examples, and generated stubs (they
  are marked deprecated there) and adds a lint flagging new `s_*` call sites
  in the linted tree.

## Applying the contract

Batches go through the Stage-0 prober: parity (recorder OFF and ARMED)
188/188, the coverage report, and the pin-map check
(`scripts/parity/check_mutation_pins.py`) before a batch is real. Pure
renames are parity-neutral by construction; run parity anyway. A file a
batch touches converts to this contract wholly — a half-converted file is
harder to read than an unconverted one.
