# Runaway special safeguards

OpenGlad bounds special effects that could otherwise grow without limit while
preserving the original low-level behavior and RNG stream. Curves and shared
caps live in
[`combat_math.h`](../include/openglad/core/combat_math.h) and
[`combat_math.cpp`](../src/core/combat_math.cpp). Core-pack Lua applies those
policies to the individual families.

Durations are simulation ticks. At the default cadence, 300 ticks is about
25 seconds.

## 0. Compatibility rules

1. **Keep the RNG stream stable.** A capped path must not add, remove, or
   reorder gameplay RNG draws. When a random result needs a soft cap, draw
   with the original bound first and transform the result afterward.
2. **Keep identity ranges explicit.** Most cast-time curves are unchanged
   through level 13. Weapon-hit curves and cleric glow remain unchanged
   through level 20. Accumulator and queue safeguards may still bind at lower
   levels when effects are repeatedly stacked.
3. **Use deterministic arithmetic.** Simulation curves use integer
   multiplication and division, not `log`, `sqrt`, or `pow`.
4. **Do not add replicated state for these effects.** Fright metadata remains
   in the transient command queue. Thaw immunity reuses the sign of the
   existing `frozen_delay` field and masks negative values at its public
   getter.
5. **Bound both magnitude and accumulation.** A per-cast curve cannot stop an
   effect from becoming indefinite when casts stack. Freeze, stun, cloak, and
   fright therefore also have write-site or queue-level rules.
6. **Retain useful progression.** Softened values continue to grow above the
   knee. Flat caps are reserved for ranges or lifetimes where further growth
   has no useful gameplay effect.
7. **Never reduce an existing accumulator.** Accumulator helpers use
   `max(current, min(current + gain, cap))`. Values from potions, saved games,
   or snapshots that already exceed a special's cap are left alone.

## 1. Integer soft-knee curve

`og::combat::soften(raw, knee, ceiling)` is the common curve:

```text
raw <= knee:  result = raw
raw > knee:   result = knee + round_half_up(g * G / (g + G))
              g = raw - knee
              G = ceiling - knee
```

The implementation performs the round-half-up operation with integer
arithmetic:

```cpp
return knee + static_cast<int>((g * G + (g + G) / 2) / (g + G));
```

For every named pair, the curve is:

- exactly the identity at and below the knee;
- slope-one immediately above it: `soften(knee + 1) == knee + 1`;
- non-decreasing;
- bounded by the ceiling.

Knees match the original formula at the applicable identity boundary. Raising
a ceiling changes only above-knee values; moving a knee can change parity and
requires a full deterministic review.

## 2. Effect policies

The level-driven wrappers produce these values at levels 1, 5, 10, 13, 14,
20, 30, and 50:

| Policy | Formula | L1 | L5 | L10 | L13 | L14 | L20 | L30 | L50 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Scare duration | `soften(25L, 325, 375)` | 25 | 125 | 250 | 325 | 342 | 364 | 370 | 372 |
| Scare radius | `min(50 + 10L, 250)` | 60 | 100 | 150 | 180 | 190 | 250 | 250 | 250 |
| Bomb damage | `soften(15(L + 1), 210, 300)` | 30 | 90 | 165 | 210 | 223 | 258 | 277 | 287 |
| Orc yell radius | `min(160 + 20L, 420)` | 180 | 260 | 360 | 420 | 420 | 420 | 420 | 420 |
| Cleric glow bonus | `min(110L, 2200)` | 110 | 550 | 1100 | 1430 | 1540 | 2200 | 2200 | 2200 |
| Druid faerie lifetime | `soften(50 + 40L, 570, 800)` | 90 | 250 | 450 | 570 | 604 | 696 | 742 | 769 |
| Elemental lifetime | `soften(200 + 60L, 980, 1350)` | 260 | 500 | 800 | 980 | 1032 | 1177 | 1252 | 1297 |
| Image lifetime | `soften(100 + 20L, 360, 520)` | 120 | 200 | 300 | 360 | 378 | 435 | 469 | 492 |
| Skeleton lifetime | `soften(125 + 40L, 645, 900)` | 165 | 325 | 525 | 645 | 680 | 778 | 830 | 863 |
| Raised-ghost lifetime | `soften(150 + 40L, 670, 925)` | 190 | 350 | 550 | 670 | 705 | 803 | 855 | 888 |

### 2.1 Mage freeze time

The player-team branch in
[`living-03-mage.lua`](../packs/core/families/living-03-mage.lua) adds
`freeze_base + freeze_per_level * level`, currently `20 + 11L`, to the
world's pending `enemy_freeze` bank. The per-cast formula is unchanged. The
engine clamps the accumulated bank to 300 after living-special dispatch, so a
single cast is unchanged through level 25 and chain-casting cannot bank more
than 300 ticks.

The non-player branch still grants `min(5 + 2L, 50)` bonus rounds to allies.

### 2.2 Ghost scare duration

[`effect_ghost_scare.lua`](../packs/core/lib/effect_ghost_scare.lua)
computes `soften(25L, 325, 375)`. For player characters, the existing
constitution draw is subtracted afterward. Repeated scares use the fright
merge in §3.2, so their queue time does not sum.

### 2.3 Ghost scare radius

The scare radius is `min(50 + 10L, 250)` pixels. It is unchanged through level
20 and cannot grow beyond the useful combat area.

### 2.4 Orc yell stun

[`living-14-orc.lua`](../packs/core/families/living-14-orc.lua) keeps both original draws:

```text
add = max(0, 10 + rand0(10L) - rand0(10 * constitution))
```

`stun_total(raw_frozen_delay, add)` caps the victim's accumulated yell stun at
150 ticks. It returns a negative raw delay unchanged during thaw immunity.
An existing value above 150 is not reduced.

### 2.5 Orc yell radius

The yell radius is `min(160 + 20L, 420)` pixels. Level 13 reaches the cap, so
the original formula is unchanged through that level.

### 2.6 Faerie sprinkle freeze

#### 2.6a Post-draw curve

`compute_freeze_duration` keeps the original draw bound,
`40 + 2L - constitution / 21` when constitution is positive and `40 + 2L`
otherwise. The drawn roll is then softened with knee 79 and ceiling 110.
Every possible level-20 roll, 0 through 79, is unchanged. Example maximum
roll mappings are `79 -> 79`, `99 -> 91`, and `139 -> 99`.

#### 2.6b Refresh gate

[`weapon_animate.lua`](../packs/core/lib/weapon_animate.lua) always
performs the duration draw. At owner level 21 or higher, it refuses to replace
a freeze while the victim still has more than 10 visible ticks remaining.
The gate is disabled through level 20 to preserve the weapon-hit identity
range.

#### 2.6c Thaw-immunity refusal

At every owner level, sprinkle refuses to apply while
`frozen_delay_raw() < 0`. This protects player-controlled victims during the
negative thaw phase described in §3.3. The draw still occurs before the
refusal.

This is not an unconditional anti-stunlock rule. Hits at intervals of 10 ticks
or less can refresh the freeze before it reaches the transition that arms
immunity. A player-controlled victim also drains freeze in both input
processing and `living::act`; an even-duration freeze can reach zero in the
living drain and never enter the negative phase. AI victims never receive the
negative phase.

### 2.7 Thief bomb damage

[`living-11-thief.lua`](../packs/core/families/living-11-thief.lua) uses
`soften(15(L + 1), 210, 300)`. Damage is unchanged through level 13. Bomb
blast radius remains clamped to 16–96 pixels, and each blast still prepends
its own knockback of `min(2 + owner_level / 15, 8)` ticks. Multiple bombs
therefore retain their separate knockback entries.

### 2.8 Thief cloak

The cloak gain remains `20 + rand(20) * level`, with the base and roll span
coming from family tuning. `cloak_total` caps the cloak-produced total at 350
ticks without reducing a larger current value. Invisibility potions are not
subject to this cap.

### 2.9 Charm

Archmage mind control keeps the draw
`25 + rand0(20 * max(level_difference, 0))`, then softens the result with knee
264 and ceiling 350. The draw bound and count are unchanged; a level
difference of 12 remains entirely in the identity range.

Thief charm is draw-free after its resist check. Its duration is
`soften(75 + 25 * level_difference, 375, 490)`, also identity through a
difference of 12. The archmage keeps its `charm_left <= 10` refresh gate. The
thief keeps its `real_team_num == 255` charm-history gate.

### 2.10 Cleric glow

The glow lifetime bonus is `min(110L, 2200)`. This is a flat cap rather than
a soft knee because level 20 must retain its exact 2200-tick bonus. With the
weapon's initial lifetime of 350, the total is at most 2550 ticks.

### 2.11 Summon lifetimes

Druid faeries, archmage elementals and images, and cleric-raised skeletons and
ghosts use the lifetime wrappers in the table above. Each curve is unchanged
through level 13. These policies limit lifetime, not the number of simultaneous
summons.

### 2.12 MP-funded damage and lifetime

The core-pack scripts apply four flat limits to values driven by unusually
large MP pools:

| Effect | Current formula | Cap |
|---|---|---:|
| Heartburst and chain-lightning pool | `min(spare_mp / 2, 600)` | 600 |
| Mage starburst add per projectile | `min(spare_mp / 15, 40)` | 40 |
| Cleric mystic-mace lifetime | `min(100 + spare_mp / 2, 468)` | 468 |
| Archmage weapon drain/damage bonus | `min(magicpoints / 20, 50)` | 50 |

Division follows C truncation where the source value can be negative. The
limits bind well above the normal starting MP used by parity scenarios.

### 2.13 Deliberately unchanged behavior

The safeguards do not merge bomb knockback, cap potion durations, limit the
number of summons per caster, or change permanent druid trees. `MAXOBS` remains
the global 150-living backstop. Enemy mage bonus rounds, explosion radius,
bomb knockback, and several other short effects already have local flat caps.

## 3. Runtime mechanisms

### 3.1 Accumulator caps

The 300-tick `enemy_freeze` cap is applied after living-special dispatch in
[`walker_specials.cpp`](../src/gameplay/walker_specials.cpp). This keeps the
Lua mage formula and its tuning visible while guaranteeing a bounded world
bank.

`cloak_total` and `stun_total` apply their caps only at the cloak and orc-yell
write sites. Their monotonic form preserves larger values created elsewhere.
`stun_total` also discards additions during the negative thaw phase.

### 3.2 Fright queue merge

`force_command` marks externally forced command entries. Ghost scare calls
`force_fright` instead of the general force-command path:

- If the queue front is already a forced `COMMAND_WALK`, the remaining count
  becomes `max(existing, new)` and the direction changes to the newest scare.
- Otherwise, the method prepends the same forced walk that the general path
  would have created.

Only ghost scare uses this merge. Other shoves and flee commands keep their
separate prepend behavior. If a scare lands while bomb knockback is at the
front, it merges with that entry and points it away from the ghost.

The `forced` flag and command queue are transient. World snapshots clear
commands when applied rather than serializing them.

### 3.3 Signed thaw phase

`frozen_delay()` masks negative raw values to zero, while
`frozen_delay_raw()` exposes the stored value to the two apply-site checks.
When the player-side drain processes a `1 -> 0` transition,
`player_thaw_tick()` writes `-12`. `living::act` increments a negative raw
value once per tick until it reaches zero.

During that phase, the ordinary getter reports zero, so the character can act.
Sprinkle assignment and orc-yell accumulation refuse to replace the negative
value. AI drain paths continue to thaw directly to zero and do not create an
immunity phase. The cadence and even-duration limitations in §2.6c therefore
remain part of the current behavior.

## 4. Control switching and charm

### 4a. Selective switch clear

Control switches and control claims use `clear_command_for_control_switch`.
It preserves the leading run of forced walk entries, removes commands behind
that run, resets the weapon, and clears the leader. A scare or knockback at
the queue front therefore survives switching away and back.

### 4b. Charm survives a switch

The selective clear does not restore `real_team_num`, so switching no longer
removes charm. Character cycling already excludes walkers whose
`real_team_num` is not 255.

### 4c. Expiry and full clears

Charm ends through the normal `charm_left` decay in `living::act`. Full
`clear_command` still removes commands, restores the real team, and clears
the leader for level or session resets. Forced effects and charm therefore do
not cross a full reset.

## 5. Campaign compatibility

The campaign calibration floors include the accumulator and queue mechanisms
described in §3. Unlike above-knee curves, those mechanisms can change battles
at ordinary campaign levels when ghosts, orcs, or thieves stack casts.

Any change to §3 should re-run the Westlands, Long Season, and Tower
calibration suites and their playtest sweeps. A curve change that moves an
identity knee also requires the same review. Current calibration thresholds
must not be raised merely because a new run performs better.

## 6. Verification

### 6.1 Soft-cap contract

[`test_combat_softcap.cpp`](../tests/unit/test_combat_softcap.cpp) verifies:

1. exhaustive identity through every knee, slope one immediately above each
   knee, round-half-up cases, monotonicity, and ceilings;
2. the exact §2 tables, unchanged freeze/charm RNG bounds, accumulator
   semantics, and named constants;
3. continued progression from level 14 to level 30, except for the documented
   flat caps on scare radius, yell radius, glow, and the freeze bank.

### 6.2 End-to-end safeguard battery

[`test_silliness_battery.cpp`](../tests/unit/test_silliness_battery.cpp)
exercises the real family hooks and engine mechanisms.

#### 6.2.1 Mage freeze

Repeated level-30 casts keep the bank at or below 300, while a single level-20
cast still produces 240 ticks.

#### 6.2.2 Ghost scare

Five level-20 scares remain one bounded forced walk, and the newest cast
controls its direction.

#### 6.2.3 Faerie sprinkle

The battery covers successful player immunity windows, both documented player
residuals, AI gate behavior, the level-20/21 boundary, and unchanged RNG
consumption when assignment is refused.

#### 6.2.4 Thief cloak

Repeated cloak casts stop at 350, while an existing potion value of 450 is
unchanged.

#### 6.2.5 Orc yell

Repeated yells stop at 150 total stun, and a yell during the negative thaw
phase is discarded.

#### 6.2.6 Bombs

High-level damage follows the soft curve, while simultaneous bombs retain
separate bounded knockback entries.

#### 6.2.7 MP-funded damage

A 4000-MP heartburst produces a pool of 600, while values below the bind point
remain unchanged.

#### 6.2.8 Charm

High-level thief charm stays below its ceiling and expires through normal
`charm_left` decay.

#### 6.2.9 Cleric glow

Levels 20, 30, and 50 all receive the same 2200-tick lifetime bonus.

### 6.3 Switch-launder regression

The same battery verifies that fright survives a double switch, charm survives
the control-claim path, weapon and leader cleanup still occur, and a full
level-reset clear retains its original semantics.

[`test_stats_fright.cpp`](../tests/unit/test_stats_fright.cpp) separately
verifies the queue merge, selective clear, masked/raw freeze accessors, player
thaw transition, immunity climb, AI drain, and freeze-bank cap.

### 6.4 Focused commands

```sh
nix develop --command ./build/ci-test/og_unit_core \
  --gtest_filter='CombatSoftcap.*'
nix develop --command ./build/ci-test/og_unit_entity \
  --gtest_filter='StatsFright.*:SillinessBattery.*:SwitchLaunderRegression.*'
```

Changes to a curve, RNG call, Lua application site, command queue, or drain
path also require the full parity suite.

## 7. Current limitations

- Faerie freeze protection is conditional, not absolute. Sub-10-tick hit
  cadence can refresh before immunity arms, even-duration player freezes can
  thaw in the living drain, and AI victims never enter the negative phase.
- The refresh gate is disabled for owner levels 20 and below. At higher
  levels, repeated large rolls can still hold an AI victim through the
  10-tick refresh window until a small roll creates an escape.
- Summon lifetimes are bounded, but simultaneous summon counts are not.
- Potion durations are outside these special-specific caps.
- Fright metadata is transient command-queue state and is not carried by world
  snapshots.
- A scare can merge into a forced knockback already at the queue front. Other
  knockbacks still stack separately.
- Some identity boundaries are intentionally long: level-13 scare lasts 325
  ticks, thief charm at level difference 12 lasts 375, and level-20 glow adds
  2200 ticks. Lowering those boundaries is a compatibility change, not a
  tuning-only edit.
- Replays recorded under older uncapped behavior can diverge when they
  exercise one of the capped paths.
