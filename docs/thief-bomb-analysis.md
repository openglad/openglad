# Thief bomb analysis (#228)

Issue #228 reports a level-10 thief's DROP BOMB one-shotting a level-9 cleric,
and asks whether that is reasonable. This document answers it from the code: it
reads the bomb's damage, blast, fuse and friendly-fire tiers out of the branch,
compares them against the 2002/2013 originals in the parity companion, puts the
bomb next to every other attack in the game at the same level, and states what
can and cannot produce the reported kill.

Every number below is read from the cited line or computed from the cited
formula. The reference for "what classic did" is the parity companion worktree
(`../openglad-master/src`: `walker.cpp`, `effect.cpp`, `living.cpp`,
`view.cpp`) and the 2002 import commit `9990ec7d`.

Sim cadence is 12 ticks/s. A "roll window" is the `floor(sqrt(d))` equally
likely outcomes of `compute_base_damage` (`src/core/combat_math.cpp:64-69`):
`d - sqrt(d)/2 + rng.next(floor(sqrt(d)))`, where `next(n)` returns `0..n-1`
(`include/openglad/gameplay/game_world.h:55-63`). Landed damage is
`floor(x + 0.5)` after armor (`combat_math.cpp:52-56`).

## 1. What the bomb does, precisely

| step | where | value |
|---|---|---|
| cast | `packs/core/families/living-11-thief.lua:44-71` `drop_bomb` | costs 35 MP (`:207`); adds **no `busy`** (poison cloud, taunt and heartburst do); no cast sound |
| damage | `include/openglad/core/combat_math.h:282-285` `bomb_damage(L) = soften(15(L+1), 210, 300)` | L10 = **165**, bit-identical to 2002's `(level+1)*15` (companion `walker.cpp:2599`) through L13; the soft cap only *lowers* L14+ (L15 233 vs 240, L20 258 vs 315) |
| what does NOT scale it | the bomb is an FX: it never passes `configure_weapon_profile_base` (`src/gameplay/walker.cpp:1174`) nor `set_difficulty` | difficulty 50/100/200 % does not touch it; strength does not; the `BIT_MAGICAL` barbarian ×0.5 does not |
| fuse | `ANI_BOMB` row = 50 frames (`src/resources/gloader.cpp:151-154`), one per tick, then `effect::act` → `death()` | **50 ticks = 4.2 s**, level-independent; every detonating golden fires `SOUND_EXPLODE` (id 11) at tick 70 for a tick-20 cast |
| blast | `packs/core/lib/effect_bomb.lua:8-19` `clamp(4L, 16, 96)`, search radius `15 + range` (`:51`) | L10 reach **55 px Manhattan** (top-left to top-left, `walker::distance_to_ob`); L≥24 saturates at 111 |
| walls / LOS | none checked (`:66-71` filters dead / other-floor / TREASURE / FX only) | blasts through walls, as in 2002 (`effect.cpp:648-706`) |
| shove | `:75-77` `min(2 + L//15, 8)` ticks of forced walk away from the centre | 2 ticks at L10 |
| tiers | `:79-93` | owner **¼**, `owner:is_friendly(w)` **½**, everyone else **full**; the ally tier collapses to full when the owner is dead (`:85`) |
| hit | `walker::attack` (`src/gameplay/walker_combat.cpp:237-`) | jitter → armor reduction (`combat_math.cpp:23-47`: exact expectation of the 2002 `random(armor)` roll, a flat `(armor-1)/2` while armor ≤ damage) → `floor(d+0.5)` |
| stacking | nothing dedups: each bomb is its own explosion FX and its own `attack()` | N bombs = N full hits; explosions do not chain-detonate each other (FX are filtered) |

**The ally tier is reachable, and a golden proves it.**
`effect_explosion_ally_tier_scen99` lands 45 of a 90-damage bomb on a team-0
TOWER1 (84/130) where the same tile on team 1 reads 39/130. The `is_friendly`
early-return in `attack()` (`walker_combat.cpp:253`) does not engage, because
the caller is the *explosion*, which is `set_dead` before `death()` runs, and
`is_friendly` answers 0 for a dead caller (`walker.cpp:2510-2511`); the ½ test
on `:85` is evaluated on the owner, who is alive. The same mechanism lands the
owner's ¼ tier (thief 75→55 in `effect_bomb_bystander_scen99`;
`DRIFT_LEDGER.md:35` records the earlier wrong claim and its correction). The
friendly-fire cost is therefore real: **a level-10 player thief bombing at its
own feet takes 32..37 of its 229 HP, and a level-9 cleric ally inside 55 px
takes 71..79** (half tier 82.5 → window 78.1..86.1 → minus armor 7 → 71..79).

Deviations from 2002, and whether they matter at the reported levels:

| deviation | introduced | effect | reachable at L9/L10? |
|---|---|---|---|
| soft cap `soften(…,210,300)` | `c409e7c8` / `19d7eeb3` | nerf above L13 only | **no** — identity at L10 |
| `range < 16 → 16` floor | `dc1a2a89` (PR #25) | L1–3 reach 19/23/27 → 31 | **no** — L≥4 unchanged; unrecorded in GAMEPLAY_FIXES (see housekeeping) |
| list order (`atstart` "underneath" flag) | dead since 2013: `LevelData::add_ob` ignored it (companion `level_data.cpp:368-384`) | none | not a deviation |
| jitter `d - sqrt(d)/2 + rand(floor sqrt d)` | Dearborn `071f9821`, 2013 | mean −0.92 vs 2002's integer 165 | yes, ~1 point |
| armor: `random(armor)` → `armor/2` (2013) → exact expectation (#282) | `071f9821`, then `7d33bf3f` | 2002 mean −7.0 (armor 15); 2013 −7.5 + truncation; #282 −7.0 + round-half-up | yes, ~±1 point |
| **press-edge cast + held cast in the same tick** | 2013 (companion `view.cpp:866` event cast + `view.cpp:1019` held cast); the 2002 import (`9990ec7d` `view.cpp:853`) polled only the held key | one tap of Special = **two casts**; see §3.4 | **yes — this is the one deviation that matters** |

Net per bomb: a level-10 bomb on a 15-armor target did **158 mean in 2002, 156
in 2013, 157 today**. No commit in the project's history has made a single bomb
stronger. What 2013 changed is how many bombs one keypress arms.

## 2. The reported case on paper

### 2.1 The two clerics a "level 9 cleric" can be

| | max HP | armor | source |
|---|---|---|---|
| **player character** (hired L1, levelled, untrained) | **340** | **15** | `src/gameplay/guy.cpp:437-439,452-454,478,488`: `120 + 10 + 3·con`; cleric base con 6 / armor 7 (`living-05-cleric.lua:312-313`) + `kDefaultLevelUpGains{8,6,8,8,1}` (`family_descriptor.h:87`, the cleric scripts no `level_up`) × 8 levels → con 70, armor 15 |
| enemy / placed NPC at 100 % | 849 | 40.5 | `living-05-cleric.lua:294` `apply_difficulty_scaling(9, 12, 4, 0.5)`: 120 + 9·81; 0.5·81 |

Player crews never go through `set_difficulty` (`game.cpp:161-166` runs it on
the *placed* list before the roster spawns; `guy_create.cpp:26-47`;
`respawn.cpp:545` is AI-only), so a player cleric's HP is exactly the guy
formula. Training in the base camp only raises these numbers.

### 2.2 One level-10 bomb vs a level-9 cleric

Pre-armor window: 165 − 6.42 + {0..11} = **158.58 … 169.58**, mean 164.08.

| target | armor → reduction | damage min / **mean** / max | HP left | one-shot? |
|---|---|---|---|---|
| player cleric L9, 340 HP | 15 → 7.0 exactly (`k=15`: `(15d − 105)/15 = d − 7`) | 152 / **157** / 163 | **177 … 188** (52–55 % of the bar) | **NO** — 177 HP margin on the worst roll |
| same, pre-#282 formula | 15 → 7.5, truncate | 151 / 156 / 162 | 178 … 189 | NO (#282 delta = +1) |
| enemy cleric L9, 849 HP | 40 → 19.5 | 139 / 145 / 150 | 699 … 710 | NO (17 % of the bar) |

For one bomb to kill a 340/15 cleric on *any* roll the raw bomb value must be
≥ ~340, which `soften` caps at 300 — **impossible at any thief level on this
branch**; on the unsoftened 2002 curve it takes a level-22 thief (lucky roll)
or level 23 (guaranteed). The reported kill is not one explosion from full HP.

### 2.3 What does reproduce it

| explosions landing within one fuse | total on the cleric | 340-HP cleric |
|---|---|---|
| 1 | 152–163 | survives, 177+ left |
| 2 | 304–326 | **survives from full, 14–36 left** — dies if it had already lost ≥ 36 HP (any earlier hit does that) |
| 3 | 456–489 | dies from full, every roll |

Three candidates. Which one is even *available* depends on what the thief was,
so they are ordered by the reading of the report rather than by mechanism —
"a level 10 thief blows away my level 9 cleric" in campaign play means an enemy
bot, and a bot never takes the input path of candidate 2:

1. **An AI thief being meleed.** Every hit it takes while its current special is
   DROP BOMB is a 1-in-3 bomb (`stats.cpp:670`), and the flee it queues
   after each drop does not suppress this (`hit_response` is exempt only for
   `ACT_CONTROL`, `stats.cpp:651`). A cleric hitting every 8 ticks arms ~2 bombs
   per 50-tick fuse; a three-hero party ~6. The golden
   `thief_ai_bomb_flee_scen99` shows three live `FAMILY_BOMB` from one caster by
   tick 35 under three meleeing soldiers. This is the only stacker available in
   campaign play.
2. **A human thief tapped Special once — and that thief was HOSTILE to the
   cleric**, i.e. a versus/FFA mode, not your own party. Until this branch,
   one tap armed **two** bombs on the same tick (§3.4): 304–326 at the full
   tier on the cleric. A cleric missing 36 HP — one enemy knife, one earlier
   bomb tick, anything — died, and the two sprites overlapped and detonated on
   the same tick, so it read as one bomb. Holding the key for half a second
   armed seven. The precondition matters: your *own* thief's bombs hit allies
   at the ½ tier (§1, 71–79 on a 340/15 cleric), so two of those are 142–158
   and five are needed for a kill. This path is closed on this branch (§3.4).
3. **The cleric was not at full HP** and 157 off the bar read as "blown away".

Distinguishing them needs the save, the kill feed, or the answer to one
question: **was the thief a player or a bot?** The arithmetic supports only
these three. The input quirk of candidate 2 is worth fixing whatever the
answer — it is a bug in its own right (§3.4) — but it explains this report only
in the hostile-player reading.

### 2.4 Every family at level 9 vs one level-10 bomb

Player characters (untrained curve; armor ≤ damage so reduction = `(armor−1)/2`):

| family @ L9 | HP / armor | damage | HP left | % of bar lost |
|---|---|---|---|---|
| soldier | 358 / 17 | 151–162 | 196–207 | 44 |
| elf | 244 / 16 | 151–162 | 82–93 | 64 |
| archer | 310 / 13 | 153–164 | 146–157 | 51 |
| mage | 208 / 13 | 153–164 | 44–55 | **76** |
| cleric | 340 / 15 | 152–163 | 177–188 | 47 |
| thief | 217 / 13 | 153–164 | 53–64 | 73 |
| druid | 354 / 15 | 152–163 | 191–202 | 45 |
| orc | 486 / 19 | 150–161 | 325–336 | 32 |
| barbarian | 490 / 16 | 151–162 | 328–339 | 32 |
| archmage | 268 / 13 | 153–164 | 104–115 | 59 |

Enemy NPCs at level 9 (100 %): every family survives; worst is the mage (657 HP,
armor 40 → 145 mean = 22 %); default-curve classes with armor 162 take 78–89
(8 % of 966+).

**No level-9 walker of any family, player or enemy, dies to one level-10 bomb.**

### 2.5 The highest level at which one L10 bomb can still one-shot

Player characters at full HP:

| family | highest level still killable | detail |
|---|---|---|
| soldier | 1 (1 roll in 12) | L1 166 HP vs 155–166 |
| elf | 4 | L4 154 HP < 155 min; L5 172 survives with 8–19 |
| archer | 3 (2 rolls in 12) | L3 166 vs 156–167; L4 190 safe |
| mage | 5 (7 rolls in 12) | L5 160 vs 155–166; L6 172 survives with 7–18 |
| cleric | 1 | L2 172 survives with 6–17 |
| thief | 4 (10 rolls in 12) | L5 169 survives with 3–14 |
| druid | 1 (6 rolls in 12) | L2 186 safe |
| orc, barbarian, archmage | never | L1 orc 198, barb 202, archmage 172 (4 HP margin) |

Enemies (100 %): level ≤ 2 of every family, plus the level-3 mage (153 HP). A
level-3 cleric (201) or thief (174, armor 18) survives.

## 3. Comparative context

### 3.1 Level-10 player character, one cast, vs the 340/15 cleric

| attack | raw | after jitter + armor | % of cleric bar | MP | notes |
|---|---|---|---|---|---|
| **thief DROP BOMB** | 165 | 152–163 | 45–48 | 35 | AoE reach 55, 50-tick fuse, ¼ self / ½ allies |
| archmage HEARTBURST / CHAIN | 205 | 191–204 | 56–60 | 80 | instant, r=100, caster exempt |
| mage HEARTBURST | 195 (pool at full 490 MP) | 181–193 | 53–57 | 100 | instant, pool split across foes |
| archer EXPLODING BOLT | 57.5 + 115 blast | ~150–167 combined | 44–49 | 70 | ranged, instant, same 55 reach |
| barbarian EXPLODING BOULDER | 49.4 + 98.8 blast | ~126–142 | 37–42 | 30 | ranged, instant |
| mage fireball (primary) | 38.2 | 28–34 | ~9 | 5 | |
| druid lightning (primary) | 30.8 | 21–26 | ~7 | 4 | druid has no burst special |
| thief knife (primary) | 25.9 | 16–21 | ~5.5 | 1 | one every 3 ticks (busy 2.45) |
| soldier WHIRLWIND | 41 | ~30 | 9 | 120 | |

Per cast the bomb ranks **fourth**, behind archmage and mage and roughly level
with the archer's exploding bolt. Its two distinguishing properties are cost
(35 MP → 4.5 dmg/MP after armor, the best AoE rate in the game together with
the barbarian boulder) and the absence of any cast delay.

**Bombs per bar:** a player L10 thief has 256 MP → 7 bombs, regen 1.5 MP/tick →
one bomb every 23 ticks sustained, so **two bombs are always affordable inside
one 50-tick fuse** even at one cast per tap. An enemy L10 thief has 1100 MP →
31 bombs, regen 0.5/tick.

**Knives vs bomb, thief vs the cleric:** 18 knives at 3 ticks = 54 ticks
(4.5 s), 18 MP for a kill; the bomb does 157 mean per 35 MP and takes 50 ticks
to go off. Per MP the knife is 4× better; per second they are comparable; the
bomb is the only thing the thief has that hits a *group*.

### 3.2 Level-10 enemies, one hit, vs the same cleric (100 % difficulty)

NPC weapons get `damage · (L+3)/4 · L` (`walker.cpp:1180-1194`, identical to
companion `walker.cpp:2344-2352`); the bomb gets neither factor. Windows use
`next(floor√d)` = `0..floor−1`.

| enemy L10 attack | raw | on the cleric | hits to kill |
|---|---|---|---|
| **elemental meteor** | 390 | 373–391 | **1 — a guaranteed one-shot** |
| mage / archmage fireball | 325 | 309–326 | 2 (leaves 14–31) |
| barbarian hammer | 292.5 | 277–293 | 2 |
| archer fire arrow | 227.5 | 213–227 | 2 |
| **thief knife** | **195** | **181–193** | **2, ten ticks apart (fire_delay 5)** |
| druid lightning | 195 | 181–193 | 2 |
| **thief bomb** | **165** | **152–163** | 3 |

**A level-10 enemy thief's ordinary knife out-damages its bomb**, and two knives
kill the cleric in under a second. On HARD every row doubles except the bomb
(weapon `set_difficulty`, `walker.cpp:2452-2458`; the bomb never calls it): the
knife becomes a 390-point one-shot while the bomb stays at 165.

### 3.3 Is the bomb an outlier on the power curve?

Player HP is linear in level (cleric: 148 + 24 per level); the bomb is linear
(15 per level to L13, then flattening); NPC weapon damage is quadratic
(`L(L+3)/4 · base`).

| level | bomb | same-level player cleric HP | bomb / HP | enemy knife | bomb / knife |
|---|---|---|---|---|---|
| 4 | 75 | 220 | 0.34 | 42 | 1.79 |
| 6 | 105 | 268 | 0.39 | 81 | 1.30 |
| 8 | 135 | 316 | 0.43 | 132 | 1.02 |
| 10 | 165 | 364 | 0.45 | 195 | 0.85 |
| 12 | 195 | 412 | 0.47 | 270 | 0.72 |
| 15 | 233 | 484 | 0.48 | 405 | 0.58 |
| 20 | 258 | 604 | 0.43 | 690 | 0.37 |

The bomb stays between a third and a half of an equal-level cleric's bar at
every level, and it is the **slowest-scaling attack an enemy owns** — the knife
overtakes it at level 8. It is the norm for the curve, not an outlier; the
outliers are the quadratic NPC primaries.

### 3.4 One tap, two bombs: the player input path (fixed on this branch)

`sim_process_player_input` (`src/gameplay/sim_input_handler.cpp`) calls
`player_cast_special` on `was_pressed(Special)` **and again** on
`is_held(Special)` in the same tick. `input_state_from_sdl` derives
`pressed = held && !was_held` (`src/interface/sdl_context_services.cpp:87-89`),
so both were true on the key-down frame. `player_cast_special` returns on the
first success without any debounce (`SimInputDebounce` only throttles the
failure cues), `walker::special` (`src/gameplay/walker_specials.cpp`) has no
cooldown gate, and `drop_bomb` sets no `busy`. Both calls run in production
(`game_server.cpp`). Before the fix:

| input | bombs armed | MP | on the 340/15 cleric |
|---|---|---|---|
| one tap | **2**, same tick, same tile | 70 | 304–326 — kills anything missing ≥ 36 HP |
| tap + hold 6 ticks (0.5 s) | 7 (MP-limited at 256) | 245 | 1064–1141 |

Provenance: the 2013 companion has the same pair (`view.cpp:866` press event +
`view.cpp:1019` held poll in `continuous_input`), so this is 2013-faithful; the
2002 import (`9990ec7d` `view.cpp:853`) polled only the held state, once per
game cycle (`timer_wait` 6 × 13.6 ms ≈ 82 ms, the same ~12 Hz the sim ticks
at today), and had no press-edge cast at all. In 2002 a tap armed one bomb per
cycle it spanned — usually one, two across a cycle boundary, none if the whole
tap fell between two polls; 2013 added the press-edge cast so a tap is never
missed, but left the held poll running on the same frame.

**The fix, on this branch: the held arm yields the press tick.** `is_held` casts
only when `was_pressed` did not run this tick, so a tap is exactly one cast and
holding fires once per tick from the next tick on — the 2002 rhythm without the
missed taps. It is one guard at the input site rather than a `busy` latch in
each unlatched special, because the special bodies are shared with the AI:
`busy > 0` blocks `init_fire`, so a latch would cost every AI elf, elemental,
ghost, slime and bombing thief a fire tick per cast, move their parity rows,
and deviate from 2002 (which had no `busy` there). The guard is player-only by
construction. Regression pin:
`tests/integration/test_sim_input_unit.cpp`
`sim_input_one_press_is_one_cast_for_an_unlatched_special` (red on the old
handler: two bombs and 70 MP from one press frame).

The same double fire applied to every special without a same-tick latch of
its own — thief CLOAK, the elf's four rock volleys, elemental STARBURST, mage
WARP SPACE / ENERGY WAVE / FREEZE TIME / HEARTBURST, ghost SCARE, slime SPLIT
and GROW, and the corpse-consuming cleric RAISE GHOST / RESURRECT and orc EAT
CORPSE when a second corpse is in range. The soldier, archer, druid,
barbarian, archmage and skeleton specials, orc HOWL, thief TAUNT / CHARM /
POISON CLOUD, cleric HEAL / RAISE UNDEAD / TURN UNDEAD and the mage teleports
already declined the second call (a `busy` check or a teleport animation
state). **No parity golden can see any of this**: the harness driver
(`tests/parity/scenario_runtime.cpp`) casts once per held tick with no press
arm, so the fix moves zero goldens, and every bomb golden shows exactly one
owner-tier hit before and after.

## 4. Counterplay and cost

### 4.1 Escaping the fuse

Blast reach is 55 Manhattan from the explosion's top-left. A cleric in melee
sits ~20 px from it and must cover ~36 px to be clear. Player stepsize is the
family base plus `dex/54` (`guy.cpp:458-459`, `:491-494`), so levelled
characters are faster than the family table suggests.

| walker | stepsize | ticks to clear | fraction of the 50-tick fuse |
|---|---|---|---|
| player cleric L9 (dex 55) | 3.02 | 12 | 24 % |
| player mage L9 (dex 54) | 3.00 | 12 | 24 % |
| player thief L10 (dex 120) | 7.22 | 5 | 10 % |
| family base, slowest (cleric/mage, 2) | 2 | 18 | 36 % |
| family base, soldier/elf/archer (4) | 4 | 9 | 18 % |

A player who *sees* the bomb walks out of it with 30+ ticks to spare, even as
the slowest class. The problems are perceptual, not mechanical: `drop_bomb`
emits no sound (the only audio is `SOUND_EXPLODE` at detonation), the bomb is
drawn under a melee scrum, and the 2-tick shove at detonation is too late to
matter. **Bots never escape**: there is no bomb-avoidance code anywhere in the
AI (`grep FAMILY_BOMB src/gameplay/living.cpp` is empty). The one indirect
escape is that a *chased* AI thief flees 20 ticks at stepsize 5 after each drop
(`living-11-thief.lua:62-69`), dragging a pursuer up to 100 px — whether the
pursuer is still inside the blast at tick 50 depends on geometry, not on a rule.

### 4.2 The thief's own cost

The thief takes ¼ of every bomb it drops at its feet: raw 41.25 → window
38.0..43.0 → minus its own armor 14 (reduction 6.5) → **32..37 of 229 HP** at
L10 as a player. Seven bombs at its own feet can kill a full-HP level-10 player
thief, eight always do. It has the lowest HP of any playable class (75 base,
con +4/level, tied with elf/mage/archmage) and its HP gap widens with level
(L15: 289 vs soldier 502). Every ally within 55 px takes ½ (71..79 on a level-9
cleric). The 2002 help text calls this out: "explode and hurt the unwary, friend
or foe!" (`picker.cpp:2842`, identical to the companion).

### 4.3 MP economics

35 MP is the cheapest AoE in the game (whirlwind 120, heartburst 80/100,
exploding bolt 70, boulder 30). Regen makes it free in practice for a player
thief (a bomb every 23 ticks) and for an L10+ enemy (31 bombs on the bar).
There is no per-caster cap on armed bombs and no `busy`.

### 4.4 AI cadence — is 2026 bombing more than 2002?

No. Line-by-line identical (`living.cpp:382-395` ≡ companion
`living.cpp:342-360`; `stats.cpp:669-670` ≡ companion `stats.cpp:578-580`;
`living-11-thief.lua:11-38` ≡ companion `living.cpp:588-607`; flee command,
35/130/110 constants, 1-in-5 and 1-in-3 dice, `(L+2)/3` slot draw all
unchanged). What that cadence is, for an L10 enemy thief with a foe ≤ 35 px
(melee) or ≥ 130 px:

| trigger | per-event chance | when it can fire | rate while in melee |
|---|---|---|---|
| `hit_response` on every hit taken, while `current_special == 1` (`stats.cpp:669-670`) | 1/3 | every hit, including during the post-drop flee; only `ACT_CONTROL` is exempt (`:651`) | **~⅓ of hits taken: ~2 per fuse from one cleric (hit every 8 ticks), ~6 from a three-hero party** |
| `ACT_RANDOM` per tick: 1/5 × slot draw 1/4 (`living.cpp:382-386`) | 1/20 | **only on empty-queue ticks**: `living::act` returns from `do_command()` before the act switch (`living.cpp:322-327`), and in melee the queue holds a 30+rng(25)-tick `COMMAND_ATTACK` (`stats.cpp:1084`, `:1287`), a 300-tick `COMMAND_SEARCH` (`living.cpp:412`) or the 20-tick flee | a fraction of a bomb per fuse |

`hit_response` is the stacker, not the per-tick roll. Between 35 and 130 px the
AI refuses to bomb at all (`distance < 130 and distance > 35 → false`) — a
faithful but inverted-looking gate; do not "fix" it, it is load-bearing for
`thief_ai_bomb_flee_scen99` and would multiply bomb rates.

### 4.5 Interaction with #282

#282 moved the bomb by about **+1 point** (armor 15: mean reduction 7.5 → 7.0,
truncation → round-half-up). It moved the *player's* hits on high-armor enemies
enormously: a level-10 enemy thief (armor 200) went from a 1-damage sponge to
taking ~5 of a 45-point blow. Before #282 a level-10 enemy thief was unkillable
and players learned to avoid it; now it is killable, so players stand in melee
with it — which is precisely the ≤ 35 px band where the AI bombs, and where
`hit_response` converts every player hit into a 1-in-3 bomb. That is the one
mechanism by which #282 can make bombs *feel* new: not more damage per bomb, but
more bombs under the player's feet because the player now chooses to fight the
thief. This is a hypothesis consistent with the numbers, not a measurement.

## 5. Verdict

**The single bomb is reasonable: faithful to 2002 to within one point and
balanced by the game's own curve. The reported kill needs two bombs in one
fuse — from the 2002 `hit_response` stack if the thief was a bot, from the 2013
one-tap-arms-two input quirk if it was a hostile player. The quirk was not
reasonable either way, and this branch fixes it (§3.4).** Evidence, ranked:

1. **One explosion cannot do it.** A 340/15 cleric keeps 177–188 HP from one
   level-10 bomb; no level-9 walker of any family dies to one; the soft cap
   makes a one-bomb kill of that cleric impossible at any thief level
   (§2.2, §2.4).
2. **It is not the top of the burst table** — archmage 205, mage 195 per cast;
   an L10 enemy elemental one-shots the cleric with a primary attack and an L10
   enemy thief's *knife* hits harder than its bomb (§3.1, §3.2).
3. **It scales slowest of anything an enemy has**, ratio to same-level HP flat
   at 0.34–0.48, overtaken by the knife at level 8 (§3.3).
4. **Its costs are real**: a 4.2-second fuse walkable by the slowest class, ¼
   self-damage, ½ to allies (golden-proven), a 75-base-HP caster (§4).
5. **Provenance**: the only value change ever made to bomb damage is a nerf
   above L13; AI cadence is byte-for-byte 2002 (§4.4).
6. **The stack is where the kill lives**, and two of its three sources are
   inherited quirks rather than design: the 2002 `hit_response` stack (§4.4,
   faithful, and the only one a campaign bot can use) and the 2013 press+held
   double cast (§3.4, not in 2002, reachable against your cleric only when
   the thief is a hostile player — an allied thief's bombs land at the ½ tier —
   and closed on this branch).

### Options, ranked

**Option 1 (recommended): no damage change. Pin it, document it, and ask two
questions on #228.** Add the parity scenario of §6, post the §2 table, and ask
(a) was the thief a player or a bot, and (b) is there a save or kill feed. What
would change the verdict: a dump showing a full-HP (≥ 340) level-9 cleric losing
≥ 340 HP between one `SOUND_EXPLODE` and the next tick, i.e. a single explosion
FX doing more than 170 — nothing in the code can produce that, so if it is
observed it is a bug, not balance. Optional polish for the perceptual half of §4.1, with its
parity cost separated: a **radar blip** for armed bombs is render-only and moves
zero goldens, but a **drop sound** is not free — every `og.emit_sound` is
serialised into the dump's `events[]` (the bomb's own `SOUND_EXPLODE` is
`{"kind":"play_sound","a":11,"tick":70}` in
`tests/parity/golden/bomb_l10_vs_cleric_l9_scen99.json`, emitted by
`packs/core/lib/effect_bomb.lua:27`), so one `og.emit_sound` in `drop_bomb`
adds an event and shifts every later `sequence` in each of the 8 goldens that
contain a `FAMILY_BOMB` (`effect_bomb_bystander`, `effect_explosion_range`,
`bomb_l10_vs_cleric_l9`, `effect_explosion_ally_tier`, `effect_bomb_timer`,
`effect_bomb_emission`, `special_thief_scen789`, `thief_ai_bomb_flee`). Under
the #283 byte compare all eight go red: 8 ledger rows with branch-sourced
goldens plus a `GAMEPLAY_FIXES_FROM_CLASSIC.md` row, since 2002 dropped the
bomb silently.

**Option 2 (done on this branch): one cast per tap, fixed at the input site.**
§3.4's double cast is closed by making the held arm yield the tick on which the
press arm ran. The alternative considered — a 1-tick `busy` on `drop_bomb`
(refuse while `lc.is_busy(self)`, set `busy = 1` on success, the pattern
`cloak`/`taunt` already use) — would have worked for the player, since
`living::act` decrements `busy` before the next tick's input, but the cast body
is shared with the AI: `busy > 0` blocks `init_fire`, so every AI thief would
lose a fire tick per bomb, `thief_ai_bomb_flee_scen99` (two of whose three
bombs share the tile (142,130)) would move, the same latch would be owed to
ten other unlatched specials with their own AI rows, and all of it deviates
from 2002, where none of them had a `busy`. The input-site guard moves zero
goldens and needs no ledger row; its `GAMEPLAY_FIXES_FROM_CLASSIC.md` row is
"One Special tap cast twice".

**Option 3: cap simultaneous armed bombs per caster.** `drop_bomb` returns
`false` (no MP spent) while the caster already owns ≥ N live `core:bomb` FX.
N = 2 keeps a player's two-per-fuse rhythm and stops the AI's 3–6 stack (max
per fuse 304–326: never a kill from full, still a kill of a wounded cleric). Parity: `thief_ai_bomb_flee_scen99` has three live bombs from one
caster and **moves at N = 2** (ledger row + branch golden);
`effect_bomb_emission_scen99` drops exactly one bomb (its golden has one
`SOUND_EXPLODE`, at tick 70) and does not move at any N; the other seven bomb
rows drop one each. Needs a canary pin on the new gate. Only if the maintainer
judges the 2002 `hit_response` stack unfair as well.

**Option 4 (not recommended): change the number** — lower the `soften` knee, or
lengthen the fuse. The single-bomb number is not the problem (§2); a knee at 150
breaks `effect_explosion_range_scen99`'s "165, below the knee so branch ==
master" identity and forces a branch-sourced ledger golden; fuse changes move
the detonation tick in all nine bomb rows for no balance gain (the fuse is
already walkable).

### Housekeeping surfaced by this analysis

Independent of the verdict:

- The `range < 16` floor from PR #25 is an unrecorded 2002 deviation — it wants
  a `GAMEPLAY_FIXES_FROM_CLASSIC.md` row, or a revert.
- `tests/unit/test_gladiator_levels.cpp:71-77` and the PR #282 caption say 133
  armor for Saffron where the code path gives 128 (`statistics.h:220` starts
  placed walkers at 0; `set_derived_stats` never writes armor); the 128..133 pin
  hides the discrepancy.
- ~~The comment on `kFacts_effect_bomb_emission_scen99` ("Two bombs detonate")
  misdescribes its golden, which has one detonation.~~ **Fixed** in the same
  table pass as this document's review: the golden has one detonation and 13
  play_sounds, and the row's `EventKindAtLeast(play_sound, 12)` floor was dead
  under its own mutation (which reads 14) — it is now an exact count of 13. See
  `tests/parity/golden/DRIFT_LEDGER.md`, "Predicate teeth after the byte
  compare".
- The `effect_explosion_range_scen99` comments quote the pre-rounding window
  "158..169" where the landed values on an armor-0 target are 159..170 —
  cosmetic; no fact reads it.

## 6. What is pinned

`bomb_l10_vs_cleric_l9_scen99` (`tests/parity/scenario_table.h`) turns §2 into a
regression pin. It is the only row in the corpus that ties **caster level to
bomb damage**: every other bomb row fixes the level and perturbs the range clamp
(`effect_explosion_range_scen99`) or the damage inheritance
(`effect_bomb_bystander_scen99`) instead. A level-10 thief bombs at its own feet
with a level-9 cleric beside it; the golden was captured from the master
companion and is byte-identical to the branch dump, which is the identity §1
predicts (165 is below the 210 `soften` knee, so branch and 2002 agree exactly).

**What the harness can and cannot say.** `SpawnSpec` carries level and magic
points but no hitpoints and no armor, and `apply_post_load_spawns` never runs
`set_difficulty` or the `guy` path. The spawned cleric is therefore the *loader*
body — 120 HP, armor 0 — not the 340/15 player character of §2.1, and the
spawned thief is a 75-HP loader body rather than the 229/14 player thief of
§4.2. The row pins the level → damage step and the tier that lands; the
player-body arithmetic of §2 is paper, and stays paper.

The loader cleric is not a passive target, which is the one thing the design
of this row got wrong before it was measured. It is an AI with a foe: it closes
and melees the thief every ~9 ticks from tick 11 (the `FAMILY_HIT` tracks at
(204,202)), and from farther away it fires GLOW, so it has magic points too.
The consequence is that the 75-HP loader thief arrives at the detonation with
less than the 32..37 its own quarter tier is about to take, and **dies to its
own bomb** at tick 73. A player thief in the same spot would keep 192..197 of
229. The cleric, by contrast, takes no damage but the blast — the player thief
in this scenario holds only `K_SPECIAL` and never fires a weapon — so its death
is entirely attributable to the bomb, which is what the row is for.

Golden (`tests/parity/golden/bomb_l10_vs_cleric_l9_scen99.json`): cast at tick
20, `SOUND_EXPLODE` at tick 70, the blast resolving at tick 73 with two death
sounds and one `score_change` of 439 to team 0, the far hostile TOWER1
untouched at 130/130 holding `level_done = 0` so both corpses are reaped, and
`tick` 80 as the stale-table canary.

The row's mutation arms a level-1 bomb from a level-10 thief
(`bomb_damage(self.level)` → `bomb_damage(1)`, raw 30 instead of 165). Three of
the facts flip under it, alongside the byte compare:

| fact | what it reads |
|---|---|
| `WalkerDiedByFinal(FAMILY_CLERIC)` | 165 raw rolls 158.58..169.58 and lands 159..170 on an armor-0 body, killing 120 HP. The mutated raw 30 lands 27..31 and the cleric walks away with ~90. **Flips.** |
| `ScoreDelta(0, 439, 439)` | the cleric kill is the arena's only scoring event; a surviving cleric leaves hit XP alone. **Flips.** |
| `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 13000, 13000)` | the far hostile bounds the blast: reach, not damage, if it ever moves. |
| `EventKindExactly(play_sound, 4)` | CLANG at tick 10 (the cleric's melee), `SOUND_EXPLODE` at 70, two DIE2 at 73. **Flips** — the mutated run has three, the cleric's death sound missing. The floor of 1 this replaced was vacuous: the tick-10 CLANG predates the tick-20 cast, so it held with no bomb at all. |

The caster's own fate is deliberately *not* a fact, because the thief dies at
tick 73 on both arms. The cleric lands six melee hits by tick 58 (the
`FAMILY_HIT` tracks are the same twelve samples in both dumps, and nothing
lands after 58), which leaves the thief under even the level-1 bomb's own
quarter tier — raw 7.5, 6..7 landed — so the mutated blast kills it too and a
"the thief died" predicate is inert. The owner tier at this same caster level is
pinned discriminatingly by `effect_explosion_range_scen99`, which parks a
stationary tower instead of a live melee foe.
