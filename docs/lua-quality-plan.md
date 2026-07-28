# Lua Corpus Quality Plan — packs/core/scripts

Branch: `feature/lua-quality`, stacked on `feature/lua-classpacks` @ `4ce63b55`
(Stages 0–1 land here; Stages 2+ follow batch by batch). Companion style
contract: [lua-style.md](lua-style.md), the Stage-0 deliverable the later
stages land on. Architecture and pack format:
[lua-classpacks-design.md](lua-classpacks-design.md); its §3 determinism
cookbook stays law throughout this plan.

## 1. Diagnosis (measured, 2026-07-27)

36 files, 3,825 lines. The corpus is a faithful transliteration of C++ that was
itself a half-port of 1995 C — and faithfulness was the point (byte-exact
parity), but it shipped the archaeology along with the behavior:

| smell | measured | example |
|---|---|---|
| arithmetic shims (`og.fadd/fsub/fmul/fdiv`, `og.div/mod`, `og.trunc`, `og.i16/i8/i32`) | **307 sites** (~8 per 100 lines) | `self:set_busy(og.fadd(self:busy(), 2.0))` for what is conceptually `busy = busy + 2` |
| 1995 locals (`tempx`, `tempy`, `newob`, `generic`) | **486 occurrences** | `local tempx = w:xpos() - self:xpos()` |
| duplicated hook helpers (same-name `local function` in ≥2 files) | `do_special`×13, `level_up`×12, `check_special_ai`×9, `set_difficulty`×7, `on_death`×5, `on_act`×5, … | every living family re-declares the same skeleton |
| hand-inlined copies of C++ helpers that exist as `constexpr` in `combat_math.h` | 3 confirmed (`yell_radius`, `stun_total` in orc.lua; more suspected in freeze/mage) | 22 lines of Lua re-implementing `combat_math.h:271/:290` because "no og.* binding exists" |
| `switch`-ladder transliterations (`elseif sp ==`) | 8 living families, up to 3 ladders each | soldier.lua's 87-line `do_special` |
| rand-guard trios (`if n > 0 then r = og.rand(n) end`) | 6 sites + 2 FLAGGED eval-order splits | orc yell: 15 lines for `10 + rand(a) - rand(b)` clamped |
| dead provenance comments referencing **deleted** C++ files | **17 of 36 files** | `-- transliterated from family_orc.cpp` (deleted by the §9a retirement) |
| 4-line cookbook boilerplate header | all 36 files | identical text, says nothing per-file |
| magic tuning constants inline in behavior code | pervasive | `160 + 20*level` cap `420`; corpse range `24`; heal `level*5`; boomerang `30 + level*12` |
| C-struct FFI leaking through the API | 214 registrations, 1,947-line hand-written `bindings_entity.cpp` | `s_hitpoints()/s_set_hitpoints()` getter/setter pairs; `og.trunc(og.fdiv(self:lastx(), self:stepsize()))` |
| no code-sharing mechanism between pack scripts | structural | the sandbox strips `require`; every chunk is an island, so duplication is forced, not chosen |

Historical layers visible in one line of orc.lua:
`tempx = og.trunc(og.fdiv(ob:s_hitpoints(), 30.0))` — 1995 field name, 2005
float division, 2025 determinism shim, all in nine tokens.

## 2. Constraints that stay binding (the plan works inside these)

1. **Byte-exact parity.** `og_test_parity` 188/188 — recorder OFF and ARMED —
   is the oracle for every batch. No refactor lands without it.
2. **Mutation-canary pins.** Pins anchor to `{file, line, text}`, pack scripts
   included. Every reflow batch runs the pin-map check
   (`scripts/parity/check_mutation_pins.py`, already a build dependency),
   re-points moved pins, and fires a targeted canary (≥1 scenario flip per
   moved pin). Per-batch overhead, not a blocker.
3. **Coverage gate**: 95% line / 100% function, per half and union. Refactors
   move the Lua denominator; new C++ bindings are new functions that must be
   100%-covered on landing.
4. **Determinism cookbook** (design doc §3, R1–R10): RNG call order, no
   `pairs`, address-free `tostring`, R6 (no mutable sim state in upvalues),
   instruction/memory budgets. The plan reduces how often authors face these
   rules by moving the sharp edges behind bindings.
5. **The lints** (`scripts/check_lua_statement_lines.py`: one statement per
   line, one short-circuit per line, one `function` keyword per line; chunks
   compile text-only). Gate-load-bearing: the new idiom must read well under
   them, which pushes branch-y micro-logic into C++ bindings — where gcov
   sees it, shrinking the documented intra-line blindness.
6. **Sandbox.** No `require`/`load`; a module system must be engine-provided
   and deterministic.
7. **MP pack transfer (protocol v10).** File-list + hash-manifest driven. New
   `lib/` files ride along, but verify the manifest and the pack-cache
   regeneration tests whenever the layout changes.
8. **wasm + budgets.** Helper indirection costs instructions. Measure
   per-scenario instruction totals before and after each stage (the budget
   hook can report them) and hold the regression under 10%.

## 3. Target idiom (worked example — orc yell stun, today 15 lines)

```lua
-- today (abridged): guard trio ×2, FLAGGED comment, hand-inlined stun_total
local r1n = self:s_level() * 10
local r1 = 0
if r1n > 0 then
  r1 = og.rand(r1n)
end
-- (same trio for r2)
local tempy = 10 + r1 - r2
if tempy < 0 then
  tempy = 0
end
ob:s_set_frozen_delay(stun_total(ob:s_frozen_delay_raw(), tempy))
```

```lua
-- target: 3 lines, lint-clean, every branch lives in a tested C++ binding
-- rng order: level roll, then constitution roll (adjudicated left-first)
local stun = og.max(0, 10 + og.rand0(self.level * 10) - og.rand0(con * 10))
ob:add_frozen_stun(stun)
```

- `og.rand0(n)`: `n <= 0 → 0` without advancing the stream — exactly
  `IRandom::next(0)` (irandom.h), which is what the guard trios hand-encode
  today.
- `og.max`/`og.min`/`og.clamp`: bindings, so the branch is C++-covered, and
  the one-short-circuit lint stops forcing six-line if/else clamps.
- `ob:add_frozen_stun(n)`: binds `combat_math::stun_total` (already
  `constexpr` in the engine) fused with the setter — deletes both
  hand-inlined Lua copies.
- `self.level`: property access via a metatable over the existing binding —
  the `s_` prefix and getter parentheses stop leaking 1995 struct layout into
  every expression.

## 4. Stages — each parity-gated, each with entry/exit proofs

### Stage 0 — Refactor tooling + style contract (no script changes)

- **Batch parity prober**, `scripts/refactor/probe.sh`: apply a candidate
  diff → parity (OFF + ARMED) → coverage report → pin-map check →
  auto-revert on any red. Drives everything after.
- **Instruction-budget baseline**: a parity-harness mode dumping per-scenario
  instruction counts, committed as baseline JSON; later stages diff against
  it (<10% regression budget).
- **API stub generator**: read the 214 registrations in `bindings_entity.cpp`
  (the registration table becomes the single source of truth) and emit
  `docs/modding/og-api.d.lua` EmmyLua stubs. Add `lua-language-server` to
  `flake.nix` — developer tools come from the flake, never hand-rolled — plus
  a CI check, advisory first, enforced by Stage 5.
- **Style contract**: [lua-style.md](lua-style.md) — naming, header
  convention, comment policy, helper/module layout, shim policy, deprecation
  policy.
- Exit: tooling demonstrated on a no-op batch; baselines committed.

### Stage 1 — C++-side API enrichment (bindings only; scripts untouched)

New surface, each function unit-tested to the 100% bar on landing, each
documenting the exact C++ op sequence it reproduces:

- **Arithmetic/branch**: `og.rand0`, `og.max`/`og.min`/`og.clamp`, `og.sign`.
- **combat_math bindings**: `yell_radius`, `stun_total` (fused setters where
  the call pattern is always get→combine→set), plus an audit of
  `combat_math.h` for every other helper the transliteration hand-copied.
- **Property layer**: a metatable over walker handles — `self.hp`,
  `self.max_hp`, `self.level`, `self.busy`, `self.team`, `self.dead`,
  `pos`/`dir` accessors — with the same narrowing-setter semantics, generated
  from the same registration table (no second source of truth). Old methods
  stay as aliases until Stage 5 deprecates them in examples and docs.
- **Verb-level fusions** for the repeated clusters:
  `self:heal_clamped(n, source)` (heal effects + clamp + notification order
  preserved) and `og.summon_configured(self, order, family, {…})` (the
  summon-then-five-setters cluster in soldier/druid/archmage).
- **Module system**: `og.use("name")` — resolves `packs/<id>/lib/<name>.lua`,
  loads once per VM in deterministic order at pack install, text-only,
  budget-metered. Lint extension: lib chunks export pure tables/functions
  only (R6 enforcement at the boundary).
- **Tuning channel**: a `tuning:` map per family in `classpack.yaml`, exposed
  as a frozen read-only table via `og.tuning(self)`. Key access only; no
  iteration, so no `pairs` tension.
- Exit: bindings live and covered; zero pack-script changes; parity trivially
  green.

### Stage 2 — Mechanical de-noising (highest value ÷ risk; batched per family)

- Guard trios → `og.rand0` (6 sites, plus an audit for unguarded `og.rand`
  where n is provably positive).
- Hand-inlined helper copies → the new bindings (delete the Lua copies).
- Clamp/min/max ladders → bindings.
- **Float-op audit tool**: classify each of the 168 `og.f*` sites — operands
  provably integer-valued and < 2^24 (exact in doubles) → plain Lua
  arithmetic; anything else keeps the shim with a one-line why. Same for the
  90 `og.div`/`og.mod` sites: both operands provably non-negative →
  keep-or-replace decided by the tool's proof, never by eyeball. Apply in
  batches through the prober; parity is the proof the classification was
  right.
- Delete the 17 dead provenance comments; collapse the 36 boilerplate headers
  to one line each ([lua-style.md](lua-style.md) S2).
- Rename the 486 archaeology locals per the style contract. Pure-rename
  batches are parity-neutral by construction; run parity anyway.
- Expected (hypothesis, measure per batch): shims 307 → <60; corpus 3,825 →
  ~3,100 lines.

### Stage 3 — Structural: kill the switch ladders and shared skeletons

- `og.register_hooks` accepts `specials = { [1]=charge, [2]=boomerang, …,
  default=… }`; the dispatcher preserves today's exact ordering and default
  semantics. The 8 families' ladders dissolve into named functions.
- Shared AI shapes → parameterized closures over constants (R6-safe):
  `check_special_ai = og.ai.foe_within(130)`.
- Common preludes (busy gate, foe acquisition, distance gate) →
  `og.use("living_common")` in `packs/core/lib/`.
- Per family, prober-gated; pins re-pointed per batch.

### Stage 4 — Data lift

- Inline tuning constants → `classpack.yaml` `tuning:` blocks (yell
  parameters, corpse-eat range/heal, lifetimes, ammo formulas, …). Scripts
  read `og.tuning(self)`; modders rebalance without touching code. Identical
  values ⇒ byte-identical parity.
- Split `classpack.yaml` (73 families, one file) into
  `packs/core/families/*.yaml`, glob-loaded in sorted order; verify the MP
  transfer manifest and the pack-cache regeneration tests.
- Canary pins whose anchors were those literals: verify the harness can stage
  and mutate YAML the way it stages `.lua`; extend it if not. Pins must keep
  their teeth across the lift.

### Stage 5 — Docs, examples, enforcement, adversarial close

- Rewrite the `docs/modding` examples (emberwisp), the openglad-modding
  skill, and the api-reference in the new idiom; regenerate stubs; deprecate
  the legacy method aliases in docs.
- New lints: forbid the raw guard-trio pattern; flag same-name multi-file
  `local function` duplicates ≥N lines (content-hash heuristic); enforce
  lua-language-server clean.
- Adversarial close, in the coverage-gate style: a final review pass tries to
  show that a readability, determinism, or coverage claim about the
  refactored corpus is false.

## 5. Metrics dashboard (reported per stage)

LOC; shim count by kind; duplicated-helper count; archaeology-local count;
comment↔code ratio; the six coverage bars; parity + canary results;
per-scenario instruction delta vs the Stage-0 baseline; binding count + a
stub-generation drift check.

## 6. Sequencing and effort

Stages 0–1 are pure additions, safe alongside other work. Stages 2–4 are
batched, each batch an afternoon-sized parity-gated unit — the corpus is only
3.8k lines, so the cost is proof, not typing. Stage 5 closes. Rough shape:
0+1 together, 2 alone, 3+4 together, 5 half-sized. Everything rides the
existing oracles: the same parity/canary/coverage machinery the gate work
hardened is what makes this refactor cheap to prove.
