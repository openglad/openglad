# Weapon-Trajectory Parity Coverage — Final Verification & Honest Report

Date: 2026-05-30
Branch: `wip/networking` (companion worktree `../openglad-master` on `parity-companion`)

## 1. What was added

### Schema (additive, `schema_version` stays "v1")
- New `WeaponTrackSample` struct (`tests/parity/state_dump.h`, mirrored in
  `../openglad-master/tools/parity_dump_state.h`): `{tick, id, family, xpos, ypos, seq}`.
- New top-level `StateDump.weapon_tracks` array — per-tick samples of each live
  weaplist weapon, sorted canonically (family, seq, tick) between "walkers" and
  the existing "weapons" key by `canonical_serialize`.
- Both tick loops now call a `sample_weapon_tracks()` lambda *after every*
  `world.tick()`, symmetrically on both arms:
  - Branch: `tests/parity/parity_runner.cpp` (seq keyed by stable `entity_id`).
  - Master: `../openglad-master/tools/parity_dump_master.cpp` (master has no
    entity_id, so seq is keyed by `walker*` with a tick-gap heuristic that
    starts a fresh flight when the same pointer reappears after a gap; this
    reproduces the branch's contiguous-per-flight, first-appearance seq order
    because both arms spawn weapons in the same deterministic sequence).
- `parse_state_dump` parses `weapon_tracks` when present and is **tolerant**:
  legacy goldens lacking the key parse fine (empty `weapon_tracks`), and the
  two trajectory predicates then return Indeterminate (pass). No existing
  golden required recapture; the 156 pre-existing scenarios stayed green.

### Predicates (pure functions of `weapon_tracks`, identical on both arms)
- `WeaponSpeed(family, min_centi, max_centi, label)` — representative speed =
  MAX consecutive-tick step magnitude (centi-pixels/tick = 100 * px) of the
  lowest-seq track for the family. No track or no consecutive-tick pair →
  Indeterminate.
- `WeaponNetTravel(family, behavior_flag, threshold_centi, label)` with three
  path classes:
  - `STRAIGHT` (0): net displacement >= threshold AND net >= 0.7*pathlen.
  - `RETURNS`  (1): pathlen >= threshold AND net < 0.5*pathlen.
  - `STATIONARY` (2): pathlen <= threshold.
- `WeaponTrackSample` round-trips through serialize/parse (unit test
  `parse_state_dump_round_trips_weapon_tracks`); legacy shape tolerated
  (`parse_state_dump_tolerates_legacy_v1_shape`).

## 2. Final test count (step 1)

`./build/ci-test/og_test_parity` from repo root: **184 tests, 184 PASSED, 0 FAILED.**
All coverage / behavioural gates pass; gates were not weakened.

## 3. Mirror integrity (step 2)

`diff -q tests/parity/scenario_table.h ../openglad-master/tools/parity_scenario_table.h`
→ **IDENTICAL** (byte-for-byte).

## 4. Symmetric emission spot-check (step 4)

`weapon_knife_emission_scen99`: both branch dump and master golden emit a
`FAMILY_KNIFE` `weapon_tracks` block, sampled once per tick, seq-0 starting at
tick 8 (141,125). Per-tick step ~7px → ~707 (branch) / ~728 (master) centi-px,
both inside `WeaponSpeed(KNIFE,600,800)`. Tracks are *symmetric* (same family,
same per-tick granularity, same seq-0 origin) without being byte-identical —
exactly the tolerance the predicates are designed around. The predicate
classifies seq-0 as STRAIGHT on both arms.

## 5. Mutation-canary results (step 3) — clean tree, one rebuild per scenario

All 23 listed scenarios record **flips >= 1** (canary PASS, tree restored after
each). Honest classification by *whether a trajectory predicate flipped*:

### A. Trajectory teeth CONFIRMED — WeaponSpeed and/or WeaponNetTravel flipped (20)
| Scenario | flipped trajectory predicate(s) |
|---|---|
| weapon_knife_emission_scen99 | WeaponSpeed |
| weapon_rock_emission_scen99 | WeaponSpeed |
| weapon_arrow_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_fireball_emission_scen99 | WeaponSpeed |
| weapon_meteor_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_bone_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_fire_arrow_emission_scen99 | WeaponSpeed |
| weapon_lightning_emission_scen99 | WeaponSpeed |
| weapon_hammer_emission_scen99 | WeaponSpeed |
| weapon_sprinkle_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_boulder_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_wave_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_glow_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_tree_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_wave2_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_circle_protection_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_door_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_blood_emission_scen99 | WeaponSpeed + WeaponNetTravel |
| weapon_rock_slot2_emit_scen99 | WeaponSpeed |
| weapon_exploding_boulder_scen99 | WeaponSpeed + WeaponNetTravel |

Note: DOOR / BLOOD / CIRCLE_PROTECTION use `WeaponSpeed(fam,0,0)` +
`WeaponNetTravel(STATIONARY)`. Their canary mutations make a normally-stationary
weapon start moving, so speed leaves [0,0] / pathlen exceeds the threshold —
a genuine stationary→moving trajectory flip (canary confirmed
`WeaponSpeed(True->False)`).

### B. Canary PASSES but NO trajectory predicate flipped — trajectory teeth NOT demonstrated (3)
These scenarios still record flips >= 1 (so the canary is satisfied), but the
flip is carried by a **non-trajectory** predicate. I do NOT claim weapon-speed
or weapon-path teeth for them.

- **weapon_blob_emission_scen99** — flips via `EventKindAtLeast` +
  `WalkerHpRangeAtFinalTick`. The BLOB `WeaponSpeed`/`WeaponNetTravel`
  predicates are `branch_only` (master resolves the blob into MEDIUM_SLIME
  growth, no BLOB track). I reproduced the mutation manually: the slowed BLOB
  produces only **1 seq-0 sample** post-mutation, so `WeaponSpeed` goes
  **Indeterminate (pass)** rather than flipping. The in-table doc-comment
  claiming "WeaponSpeed flips pass->fail" is **inaccurate**; the real flip is
  the combat-timing/HP change. BLOB's trajectory predicates parity-lock the
  branch track but have no demonstrated speed/path teeth.

- **weapon_wave3_emission_scen99** — flips via `WeaponFamilyEmitted`. The
  WAVE3 trajectory predicates are `WeaponSpeed(0,0)` + `WeaponNetTravel(STATIONARY)`;
  the discriminating mutation kills the immortal phantom at tick 1, dropping the
  track to 0 samples, so both trajectory predicates go **Indeterminate (pass)**.
  This is honestly documented in the table comment. WAVE3 has a symmetric
  zero-motion *parity lock* but no demonstrated trajectory *flip-teeth*.

- **weapon_boomerang_return_scen99** — flips via `WalkerOfTeamAlive`. This
  scenario has **no trajectory predicates at all**: the boomerang is summoned as
  an `Order::FX` walker, never enters `world.weaplist`, and therefore never
  appears in `weapon_tracks` (verified: the only tracked family in this arena is
  FAMILY_ARROW from the archer). The boomerang RETURN path is structurally
  unmeasurable through the weapon_tracks schema. The intended `RETURNS`
  path-class demonstration could NOT be realized here.

## Summary of teeth coverage
- **20 / 23** weapon-trajectory scenarios have canary-confirmed trajectory teeth
  (WeaponSpeed and/or WeaponNetTravel flips under the discriminating mutation).
- **3 / 23** (BLOB, WAVE3, BOOMERANG) pass the canary on non-trajectory
  predicates; their weapon-speed/path teeth are NOT demonstrated. BLOB and WAVE3
  go Indeterminate under the mutation (insufficient post-mutation samples);
  BOOMERANG is an FX walker that never enters weapon_tracks.
- The `RETURNS` path class has predicate support and unit-test coverage but is
  not exercised by any canary-flipping scenario (ROCK/boomerang return paths are
  either an Order::FX or do not reverse in the chosen arenas).

## Notes
- Both worktrees are clean and committed (branch HEAD 028431ac; master HEAD
  2172f91f). `tests/parity/scenario_facts_generated.json` is committed and clean
  after build.
- No coverage gate was weakened; the pre-existing 156 scenarios remained green
  without golden recapture.
