# Gameplay Fixes

**Gameplay Fixes** is a setting, on by default, that fixes the bugs in the
classic specials: a disarm that sped up the foe it hit, a Turn Undead that
could not reach a body next to the cleric, a heal that pushed an ally past
full health, bots that cast at nobody, and more. With the setting off the
game is the classic game exactly, bugs included: every parity golden
captured with it off is unchanged, and the old game at the parity commit
carries the same fixes behind the same setting, so the goldens for both
values of the setting are captured from the old game.

The bugs come from the specials survey; each fix below names what a player
sees with the setting off and with it on. New Specials, the sibling setting
on the same screen, is described in [new-specials.md](new-specials.md); the
two are independent.

## The setting

| client | where |
|---|---|
| SDL | Game Settings → Gameplay FX → **Gameplay Fixes** (green on, red off), the row below New Specials |
| text (`openglad_text`) | the game-settings prompt `Gameplay fixes: <on\|off>. Change (on/off, blank keeps current):`, or `--gameplay-fixes 0\|1` for one run |
| curses | Game Settings → `Gameplay fixes (on/off):` |

The row flips a per-machine preference (`cfg` key `gameplay/gameplay_fixes`,
default `on`). That preference seeds the session's value once per session.
In a networked game the host decides: a joiner's click only changes what it
will use when it hosts. The value is never written into a company save.

It is a simulation setting and travels exactly as New Specials does, one
hop at a time:

1. **Preference → session.** `og::ui::seed_gameplay_fixes_from_cfg` sets
   `SaveData::gameplay_fixes` (session-only, default 0) from the cfg key, in
   `GameSession`, the text picker and the curses picker.
2. **SaveData → lobby.** `LobbySettings::gameplay_fixes` (`int16`, lobby
   default 1, anything but 0 or 1 is reset to the fallback) is copied from
   and back to every peer's save, and `LobbySaveDataEquivalent::gameplay_fixes`
   carries it to the dedicated server.
3. **Lobby → world.** `InitialSetupMessage::gameplay_fixes` (`int16`, right
   after `new_specials`) is sent by `GameServer` and by the staged lobby
   (`MatchStage`); `GameClient` copies it into `GameWorld::gameplay_fixes`.
   Both `sync_world_from_save_data` twins (`Screen` and the headless server)
   stamp the same field from the save.
4. **World → mirrors and replays.** `WorldSnapshot::gameplay_fixes`
   (`uint8`) rides every keyframe and delta, and a replay compares it. Lua
   reads it as `og.match_setting("gameplay_fixes")`; C++ reads
   `og::sim::session_gameplay_fixes()`.

`GameWorld::gameplay_fixes` defaults to 0, so every hand-built world and
every parity recording plays the classic game unless something sets it. The
parity runner sets it from the scenario row (`ScenarioSpec::gameplay_fixes`).

Versions: protocol v21, snapshot v17, replay v23.

## The fixes

Each row reads: what happens with the setting off (the classic game), and
what happens with it on. A refusal spends nothing: no magic, no rest, no
sound, no busy time, with one exception the maintainer chose: a DISARM
that every foe in reach resisted costs half its price and the fighter's
usual pause.

| | special | off (classic) | on |
|---|---|---|---|
| A | Soldier DISARM | A won roll against a foe two or more levels above the soldier staggers it by a negative amount, so it attacks sooner. "Fighter Disarmed Enemy!" prints whenever a foe was in reach, even when every roll was lost. | A stagger that is not positive is not applied. With nobody inside the 28 px reach the cast is refused as `NO ONE TO DISARM` and costs nothing. With foes in reach but nobody staggered (every roll lost, or every foe too strong) it is refused as `DISARM RESISTED` and costs half the price (75 of 150 MP) and the usual 5 ticks of busy; the message and the full price come only with a real stagger. |
| B | Mage and archmage bots | A bot mage casts any special only when no foe or more than three foes are within 110 px (the TELEPORT rule, applied to every slot), so WARP SPACE, FREEZE TIME and ENERGY WAVE go off at nobody, and a mage with 500 magic spends it on a crowd at its own feet. Archmage slots 1 to 3 have no gate at all. | Each slot asks about its own reach (see [Bots](#bots)); FREEZE TIME asks about the allies' fight instead: three foes at blows with allies, or an ally at blows near death. |
| C | Bot Shift choice | A bot's Shift is a coin: a cleric throws TURN UNDEAD at living foes, a thief tries to charm a foe that will resist, an archmage chains or summons the elemental at random. | The coin is still drawn, then the gate sets Shift on purpose (see [Bots](#bots)); a cleric whose hero lacks the 60 INT for TURN UNDEAD never picks it. |
| D | Archmage SUMMON ELEMENTAL | The surcharge (half the magic above the slot's price) is taken before the placement check, so `NO ROOM TO SUMMON` still costs half the spare pool. | The surcharge is taken once the elemental stands; no room costs nothing. |
| E | Cleric TURN UNDEAD | The reach is 4 px per level, so below level 4 it cannot reach a body standing next to the cleric. With living foes in reach and no undead, it spends the slot's magic, plays its sound and turns nothing. | The reach is at least 60 px (the reach RAISE UNDEAD already has). With no undead foe in reach it is refused as `NO UNDEAD IN RANGE`. The roll keeps its classic strength (160 per cleric level), so the wider ring never changes who resists. |
| F | Archmage MIND CONTROL | A foe that resists goes berserk onto a random team colour, which can be the archmage's own: the "failed" control then joins the caster. | A berserk foe that rolls the caster's colour takes the next colour instead. It still goes berserk; it never joins the caster. |
| G | Thief CHARM OPPONENT | The charm tries the first eligible foe in the world's list, which may be the far one. | It tries the nearest eligible foe (ties go to list order). |
| H | Elf BOUNCING ROCKS, LOTS OF ROCKS, MEGA ROCKS | Each volley refunds one weapon cost more than the rocks it throws (1 MP for an elf). A volley against a foe touching the elf swings once, fails, and keeps the refund: a free swing. | The refund is exactly the rocks thrown; a volley that cannot throw takes back the rocks that never flew, so it costs exactly the one swing. ROCKS, already exact, gets only the take-back. |
| I | Druid GROW TREE and SUMMON FAERIE | Against a foe touching the druid, the cast swings at it, fails with `COULD NOT FIRE`, and keeps the refunded bolt: a free swing. | The refunded bolt is taken back: the cast costs the swing (4 MP). |
| J | Cleric HEAL | An ally can be healed above its maximum health, and stays there. | Health stops at the maximum. The number shown and the experience earned are what really landed; the magic cost is unchanged. |
| K | WHIRLWIND, TAUNT, SCARE, HOWL | With nobody in reach each still succeeds: the spin, the taunt, the fright and the howl spend their magic (and SCARE and HOWL start their rest) at nobody. | Each is refused as `NO FOE IN RANGE`. WHIRLWIND, TAUNT and HOWL count anything they can act on, generators included; SCARE counts only living foes, the only ones a fright moves. |
| L | Bot slot roll | A bot rolls a slot from 1 to (level + 2) / 3 and folds anything above 4 into slot 1: a level-13 mage never rolls HEARTBURST (slot 5), and a level-16 soldier rolls CHARGE half the time. | The roll stops at the family's last named slot that the level has opened, and nothing is folded. |
| M | Charm and seats | An enemy thief or archmage can charm a hero a player is driving. A seat can be handed a walker that is only on its team because it is charmed. | A driven hero resists (the rule POSSESS already has). A seat never claims a charmed walker (see [Charm and seats](#charm-and-seats)). |

TELEPORT, REVEAL, CLOAK, ENERGY WAVE and CHARGE are unchanged: each does
something whenever it is cast (ENERGY WAVE crosses walls along the facing;
CHARGE already refuses `PATH BLOCKED` and otherwise moves the fighter).

## Refusals at a glance

Every refusal is at most 24 characters, so it fits the compact HUD. All
but one spend nothing; `DISARM RESISTED` costs half the price and the
usual busy.

| special | refusal with the setting on | when |
|---|---|---|
| DISARM | `NO ONE TO DISARM` | nothing inside the 28 px reach (free) |
| DISARM | `DISARM RESISTED` | foes in reach, none staggered (half price, 75 MP, and 5 busy) |
| TURN UNDEAD | `NO UNDEAD IN RANGE` | foes in reach, none of them undead |
| WHIRLWIND | `NO FOE IN RANGE` | nothing inside the spin (32 + 2 px per level) |
| TAUNT | `NO FOE IN RANGE` | nothing inside the taunt reach (80 + 4 px per level) |
| SCARE | `NO FOE IN RANGE` | no living foe inside the scare radius |
| HOWL | `NO FOE IN RANGE` | nothing inside the howl radius |

`NO ROOM TO SUMMON` (SUMMON ELEMENTAL) and `COULD NOT FIRE` (the elf's
volleys, GROW TREE, SUMMON FAERIE) keep their text; with the setting on they
cost nothing extra (SUMMON ELEMENTAL) or exactly one swing (a blocked
volley or bolt).

## Bots

A bot picks a slot, draws its Shift coin, drops a Shift it cannot pay, then
asks the slot's gate whether to cast. With the setting on every draw the
classic bot makes is still made, in the same order: the gates read the
world with finders that draw nothing, and where they choose the Shift they
write it after the coin with `set_shifter_down`, as the classic cleric gate
already did. A gate that newly says no removes the hit response's one
follow-up draw on that tick; that happens only with the setting on.

- **Mage.** Slot 1 (TELEPORT) keeps the classic rule: flee when no foe or
  more than three foes are within 110 px. WARP SPACE and ENERGY WAVE want a
  foe within the same 110 px. FREEZE TIME is worth its 500 magic for the
  free blows the time stop hands the allies, so it waits for their fight:
  at least three foes within a blow of allies (the mage itself not
  counted), or an ally at blows that is under a quarter of its health. How
  close the foes stand to the mage is not asked: three foes closing on the
  mage are a reason for a 15-magic TELEPORT, not a 500-magic freeze. "Within
  a blow" is the engine's melee reach, a gap of at most 5 px between the two
  bodies (the 6 px weapon box sits 1 px outside the attacker and reaches
  gaps 1 to 6, and the collision check moves the other body's near edge in
  by 1 px), the same at every level; a knife thrown from 45 px is not a
  blow. A mage on another team takes the same cue for its own freeze, which
  grants its side bonus rounds; a placed mage holds 50 plus 14 times its
  level squared magic, so from level 6 up it can pay. HEARTBURST wants a foe
  inside its own reach (80 + 2 px per level). The TELEPORT MARKER coin is
  kept: a marker halves an enemy mage's spare magic and sends it back to a
  known spot, a weakness the player profits from.
- **Archmage.** TELEPORT has the mage's rule. HEARTBURST or CHAIN LIGHTNING:
  the coin's choice is kept when it has a target, swapped when only the
  other has one, and the cast refused when neither has. SUMMON IMAGE or
  ELEMENTAL wants a foe within 200 px and picks the ELEMENTAL only when the
  spare magic is at least the slot's own price (500, so 1000 MP or more),
  else the IMAGE. A struck archmage bot's summon uses the same rule; a
  player archmage's Shift stays the Shift key. Two things about these gates,
  stated plainly:
  - The chain gate uses the chain's reach by level (200 + 7 px per level).
    The cast itself uses 200 + half the archmage's Intelligence + 2 px per
    level for an archmage with a character record, so such a bot can be
    told "the chain has a target" by the gate and then refused
    `NO FOE IN RANGE` by the cast. The refusal costs nothing. It is a
    candidate for a later refinement, not a bug in play.
  - The chain's reach always covers the burst's, so the "chain chosen, no
    target in chain reach" case can only fall back to no cast; it can never
    swap to a burst that has a target. The case is kept because it is the
    honest reading of the rule.
- **Cleric.** Slots 2 and 3 cast TURN UNDEAD only with an undead foe inside
  the turn reach, else they raise (a raise with no corpse is refused for
  free). A cleric whose hero has under 60 INT cannot cast TURN UNDEAD
  (`60 INT REQUIRED`, 5 busy), so the gate never picks it for such a hero.
  Slot 1 is unchanged: HEAL with any friend within 60 px, else the MYSTIC
  MACE at half mana.
- **Thief.** Slot 3 tries CHARM OPPONENT only when a foe it can take (never
  charmed, alive, at or below the thief's level, not a hero a player
  drives) is inside charm reach, else it taunts. CLOAK and POISON CLOUD are
  unchanged.
- **Every family.** The slot roll stops at the last named, opened slot (fix
  L), so a level-13 mage can roll HEARTBURST and a level-16 soldier rolls
  each of its four specials equally.

## Charm and seats

- **A driven hero resists a charm** (issue #355, both games). An enemy
  thief's CHARM OPPONENT and an enemy archmage's MIND CONTROL skip a hero a
  player is driving, by the same rule POSSESS uses (`walker:seat_driven()`:
  a user tag, or the player-control act). A skipped hero costs the caster
  no roll and gives no free hit; with nobody else eligible the charm is
  refused as `NO VALID TARGET`.
- **A seat never claims a charmed walker** (issue #354, the current game
  only). The seat scans skip a walker whose charm timer is running and
  whose real team is recorded: it is on our team only because our side
  charmed it. A freshly spawned walker (charm timer 0) is still claimable.
  One visible consequence: a team whose only bodies are walkers it charmed
  now ends the game instead of handing a seat a borrowed walker, and a
  player joining mid-level in a local game is given the stock soldier
  instead (in a networked game the seat follows the team).

The old game has the same seat scans, but no parity capture can run them, so
#354 is not ported there.

## Parity

The parity goldens are captures from the old game (branch
`parity-companion`). With this setting:

- The 227 existing rows are captured with the setting off, as they always
  were. None of their goldens changed: with the setting off both games play
  the classic game byte for byte.
- Each fix a parity scenario can reach has a row with the setting on, and,
  where the row is a new arena, an off sibling beside it. Twins of existing
  rows end in `_fixes_scen99`; new arenas come as `<arena>_scen99` (off) and
  `<arena>_fixes_scen99` (on). The scenario table carries the setting per
  row (`ScenarioSpec::gameplay_fixes`, a trailing field, 0 by default).
- The fixes are ported into the old game one commit per fix, behind the
  same setting (`screen::gameplay_fixes`, set per row by the golden capture
  tool): the plumbing `7bf3e95d`, then `a11c0c9c` (K), `ef0c9446` (A),
  `9c453c82` (J), `090ab5b9` (E), `124c0efa` (D), `ce726a7d` (F), `628c15ec`
  (G, which also skips a driven hero in the thief's charm), `c4b229f7` (H),
  `68a84937` (I), `84ce0969` (the bots: B, C and L), and `379c8516` (M, the
  archmage's charm arm). After the maintainer's rulings: `2dc64408` (A: a
  resisted DISARM pays half its magic and its busy), `8928bb32` (C: the INT
  rule) and, not behind the setting, `cf5b1fe2` (MIND CONTROL against a
  higher-level foe lasts 25 ticks). After the FREEZE TIME ruling:
  `9b8d4503` (B: the mage bot's FREEZE TIME waits for the allies' fight),
  with the table mirrors `4d0d441e`, `f9000fb9` and `92015aa8` around it.
  `92015aa8` is the commit the goldens are captured from.
  `tests/parity/golden/DRIFT_LEDGER.md` records each one.

The rows with the setting on, by fix:

| fix | rows (setting on) |
|---|---|
| K | `soldier_whirlwind_no_foe_fixes_scen99`, `thief_taunt_no_foe_fixes_scen99`, `ghost_scare_no_foe_fixes_scen99`, `orc_howl_no_foe_fixes_scen99` |
| A | `soldier_disarm_higher_foe_fixes_scen99`, `soldier_disarm_resisted_price_fixes_scen99`, `soldier_disarm_resisted_busy_fixes_scen99` |
| J | `special_cleric_heal_ally_fixes_scen99` |
| E | `cleric_turn_undead_reach_fixes_scen99`, `cleric_turn_no_undead_fixes_scen99` |
| D | `archmage_summon_no_room_fixes_scen99` |
| F | `archmage_mind_control_berserk_fixes_scen99` |
| G | `thief_charm_nearest_fixes_scen99` |
| H | `elf_volley_refund_fixes_scen99`, `elf_rocks_blocked_fixes_scen99` |
| I | `druid_tree_blocked_fixes_scen99` |
| B, C, L | `mage_bot_freeze_no_foe_fixes_scen99`, `mage_bot_freeze_alone_fixes_scen99`, `mage_bot_freeze_engaged_fixes_scen99`, `mage_bot_heartburst_fixes_scen99`, `archmage_bot_summon_no_foe_fixes_scen99`, `cleric_bot_raise_or_turn_fixes_scen99`, `thief_bot_charm_or_taunt_fixes_scen99`, `archmage_hit_stale_shift_fixes_scen99` |
| M | `thief_bot_charm_hero_fixes_scen99`, `archmage_bot_mind_control_hero_fixes_scen99` |

Rows deliberately not added:

- `cleric_turn_undead_scen99`: identical with the setting on (both
  skeletons are inside both reaches, and the roll strength is unchanged).
- `special_soldier_3_scen99`: identical with the setting on (its foe walks
  into the spin before the cast, so both values spin at it). WHIRLWIND at
  nobody has its own arena, `soldier_whirlwind_no_foe_scen99` and
  `soldier_whirlwind_no_foe_fixes_scen99`, with a foe that never moves.
- A level-16 bot soldier (`soldier_bot_l16_roll`): its roll stops at slot 4
  with the setting on instead of folding 5 and 6 into slot 1, but the two
  recordings differ only in the random stream's state, which no fact can
  read. The bound is pinned by `mage_bot_heartburst_fixes_scen99`.
- `mage_freeze_time_offteam_scen99`: it differs with the setting on, but no
  fact can bound an orc's travel from above; the mage gate is pinned by
  its own rows instead (`mage_bot_freeze_no_foe`, `mage_bot_freeze_alone`
  and `mage_bot_freeze_engaged`, each with and without the setting).
- The existing elf volley and druid rows: identical with the setting on
  (magic is not in the recording; the new rows show it through what the
  magic buys next).
- The three existing charm rows: one foe in reach, so "the nearest" changes
  nothing.
- Every other existing row was run on both values of the setting in the
  current game. Six differ with it on: `special_cleric_heal_ally_scen99`
  (its twin is above), `mage_freeze_time_offteam_scen99` (above), and four
  that move only through bot drift or the random stream,
  `effect_marker_emission_scen99`, `generator_saturation_scen99`,
  `thief_taunt_matched_levels_scen99` and `tower_snap_face_heading_scen99`.
  The fixes behind those four are pinned by the new rows. The rest,
  `cleric_resurrect_friendly_scen99`, `soldier_disarm_matched_levels_scen99`
  and `special_soldier_4_scen99` among them,
  record the same bytes on both values, so they need no twin.
- `NO ONE TO DISARM` has no row: a refusal string is not recorded and the
  cast spends nothing on either value, so an arena's two captures would be
  byte-identical; a current-game test pins it.
- Fixes N, O and M's seat half (#354): no parity row can reach them (see
  [Not gated](#not-gated) and [Charm and seats](#charm-and-seats)).
- MIND CONTROL against a higher-level foe: the old game drew a garbage
  charm duration there (a negative bound cast to unsigned, stored in a
  short); the current game draws nothing and gives 25 ticks. The old game
  now does the same, whatever the setting (`cf5b1fe2`); no row reaches it,
  and a full capture moved no golden. The soft cap above an edge of 12
  (the current game's runaway knee) remains the one unported, unreached
  difference.

## Not gated

Three changes in this work apply whatever the setting:

- **N. A slot that is not in play says so.** `walker::special()` refuses a
  slot priced 5000 (an undeclared slot, or one New Specials hides) before it
  compares the price with the walker's magic, as `NO SPECIAL AVAILABLE`
  (`SpecialFailure::Unavailable`). Nothing in play reaches it: the player's
  special cycling and the bots' slot pick both skip such slots. The old
  game has the same hole and no row reaches it, so it is not ported.
- **O. METEOR RAIN holds its fire over an ally** (New Specials only). A bot
  fire elemental's METEOR RAIN gate refuses when an ally stands in the
  strike box: within 71 px of the target on each axis (the rain lands
  within its spread of the target and each blast reaches 31 px; the rain
  hurts allies at half). With New Specials off the gate is not asked.
- **P. The archer's comment** on its default cast now names the slots it
  serves (slot 3 and every unmapped slot; slot 4 is undeclared). Comment
  only.

## Balance

All four calibration suites were measured on both values of the setting,
and on both values of New Specials, with their own seeds (42, 1337 and
2025). Each cell is the crew's survivors at tick 600 for the three seeds.
The suites' asserting runs (seed 42 against each level's floor) play the
shipped default, Gameplay Fixes on and New Specials on. No floor was
lowered. Four levels fell under their floors on the shipped default, all
because the crew did worse, and each was treated as a balance bug:
Westlands 11 by a rule added to fix C (a cleric bot never picks a TURN
UNDEAD its hero lacks the INT for), Long Season 2, 5 and 8 by easing the
levels themselves, whose orders of battle had been tuned against a crew the
classic HEAL kept above full health. With the setting off and New Specials
off, every measured line equals the line measured before the fixes, except
the three eased Long Season levels, whose classic numbers are the new ones
below.

What moves with the setting on, and why:

- The crew cleric, from crew level 4 (Westlands 6 to 26, Long Season 7 to
  19, Tower floors 15 and 20), raises instead of throwing TURN UNDEAD at
  living foes.
- A crew barbarian at crew level 7 to 9 rolls EXPLODING BOULDER half the
  time instead of a third (the slot roll stops at its second slot).
- Enemy mages and archmages keep the magic they used to waste at nobody,
  and enemy clerics raise instead of fizzling.
- The crew cleric's HEAL stops at full health. The classic heal took a
  lightly scratched crew well over its maximum (a soldier at 367 of 286 on
  Westlands 11, the elf at 197 of 154 on Long Season 8), and that extra
  health was spent in the fight. This is what took the early Long Season
  levels (2, 5 and 8) under their floors on seed 42, and why those levels
  were eased; levels 3 and 4 move a little, both ways, for the same reason.
- A soldier bot of level 10 and up that is resisted on DISARM now pays
  75 MP and waits 5 ticks (it paid nothing before the ruling).

**Westlands** (survivors at tick 600, seeds 42 1337 2025)

| level | crew | New Specials off: setting off | setting on | New Specials on: setting off | setting on |
|---|---|---|---|---|---|
| 1 | 1 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| 2 | 2 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| 3 | 2 | 8 8 6 | 8 8 7 | 7 8 7 | 8 8 7 |
| 4 | 3 | 8 8 8 | 8 8 7 | 8 8 7 | 8 7 7 |
| 5 | 3 | 8 8 4 | 8 5 5 | 8 8 4 | 8 5 5 |
| 6 | 4 | 5 8 7 | 5 7 8 | 5 8 7 | 5 7 8 |
| 7 | 4 | 0 0 0 | 0 1 0 | 0 0 0 | 0 0 0 |
| 8 | 5 | 0 1 4 | 0 1 4 | 1 1 2 | 2 0 2 |
| 9 | 6 | 2 0 0 | 2 0 0 | 1 1 2 | 1 1 2 |
| 10 | 5 | 0 0 0 | 0 0 1 | 0 0 0 | 0 0 0 |
| 11 | 6 | 7 6 6 | 3 4 4 | 4 3 5 | 6 5 6 |
| 12 | 6 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 13 | 6 | 2 0 0 | 0 0 0 | 0 0 1 | 0 0 1 |
| 14 | 7 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 15 | 7 | 3 3 3 | 3 3 3 | 4 0 2 | 5 0 4 |
| 16 | 8 | 2 5 3 | 3 5 3 | 3 3 2 | 3 4 3 |
| 17 | 8 | 5 6 4 | 5 4 6 | 6 6 6 | 5 7 5 |
| 19 | 6 | 6 7 5 | 6 6 7 | 7 5 7 | 7 5 6 |
| 20 | 7 | 0 0 0 | 0 1 0 | 0 0 0 | 0 0 0 |
| 21 | 7 | 8 7 8 | 8 8 5 | 8 8 8 | 8 8 8 |
| 22 | 8 | 0 2 0 | 0 2 0 | 0 0 0 | 1 0 0 |
| 23 | 8 | 5 6 6 | 4 7 6 | 6 7 8 | 5 7 6 |
| 24 | 8 | 1 4 7 | 6 5 4 | 4 6 4 | 6 5 2 |
| 25 | 9 | 8 8 8 | 6 8 5 | 1 8 7 | 8 3 7 |
| 26 | 9 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |

**Long Season**

| level | crew | New Specials off: setting off | setting on | New Specials on: setting off | setting on |
|---|---|---|---|---|---|
| 1 | 1 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| 2 | 1 | 7 7 7 | 7 6 6 | 7 7 7 | 7 6 6 |
| 3 | 2 | 8 7 8 | 7 7 8 | 8 7 8 | 7 7 8 |
| 4 | 2 | 4 2 3 | 3 1 5 | 3 2 3 | 3 1 5 |
| 5 | 3 | 8 8 8 | 7 8 7 | 8 8 8 | 7 8 7 |
| 6 | 3 | 7 6 4 | 7 6 4 | 7 6 4 | 7 6 4 |
| 7 | 4 | 8 8 6 | 8 8 6 | 8 8 6 | 8 8 6 |
| 8 | 4 | 3 7 8 | 5 6 6 | 7 6 7 | 5 8 5 |
| 9 | 5 | 7 8 7 | 8 8 8 | 8 8 8 | 8 8 8 |
| 10 | 5 | 6 7 7 | 4 5 6 | 7 7 6 | 5 5 4 |
| 11 | 5 | 7 7 8 | 8 5 7 | 7 7 4 | 7 5 7 |
| 12 | 6 | 3 8 8 | 3 8 5 | 3 8 8 | 6 8 6 |
| 13 | 6 | 5 6 4 | 6 6 6 | 4 6 4 | 6 7 4 |
| 14 | 7 | 7 7 8 | 7 8 8 | 7 7 8 | 8 8 8 |
| 15 | 7 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| 16 | 7 | 6 8 6 | 7 7 6 | 6 8 6 | 7 7 6 |
| 17 | 8 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 18 | 8 | 0 0 1 | 0 0 0 | 0 0 0 | 0 0 0 |
| 19 | 8 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |

**Imaginations** (level 1, crew 1): `6 6 6` in all four columns.

**Tower** (floor, crew)

| floor | crew | New Specials off: setting off | setting on | New Specials on: setting off | setting on |
|---|---|---|---|---|---|
| 1 | 1 | 8 8 7 | 8 8 7 | 8 8 7 | 8 8 7 |
| 5 | 2 | 2 4 8 | 2 6 7 | 1 4 8 | 1 6 7 |
| 10 | 3 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 15 | 4 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 20 | 6 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |

**Levels that fell below their floor on the shipped default, and what was
done.** Westlands 11 (3 against 4): fix C's INT rule; it now reads `6 5 6`.
Long Season 2 (5 against 6), 5 (2 against 5) and 8 (2 against 3): the
levels were eased (The Ferry Right has one brigand fewer on the west
approach, Two Banners one picket fewer, and The Paymaster Vanishes fields
its five terrace-2 bruisers at level 1 instead of 2); they now read
`7 6 6`, `7 8 7` and `5 8 5`. No floor moved. Long Season 2's seeds 1337
and 2025 now sit exactly on its floor of 6. The easing moved the same
levels' lines with the setting off too, on both values of New Specials:
Long Season 2 went from `6 8 8` to `7 7 7`, Long Season 5 from `6 6 7` to
`8 8 8`, and Long Season 8 from `3 8 5` to `3 7 8` (New Specials off) and
from `6 8 5` to `7 6 7` (New Specials on). None of them is under its floor.

On seed 42, the seed the suites assert, every level of every suite is at
or above its floor and inside its ceiling with the shipped default. The
other two seeds are measured, not asserted, and some read outside the
seed-42 bands, with the shipped default as with the classic game:
Westlands 5 reads `8 5 5` against its floor 6 and Westlands 25 `8 3 7`
against its floor 5 (the classic game reads `8 8 4` on Westlands 5 and
`0 1 4` on Westlands 8, whose ceiling is 2).

With New Specials off and the setting on, a pair that is measured but not
asserted, two levels read under their floor on seed 42: Westlands 11
`3 4 4` against 4 (the INT rule moves that pair's seed 42 from 4 to 3 and
its seed 1337 from 2 to 4) and Westlands 23 `4 7 6` against 5. Long
Season 8 read `0 4 7` against 3 on that pair before the level was eased;
it now reads `5 6 6`. The Westlands 11 move on that pair is recorded, not
fixed: the same rule takes the shipped default from 3 to 6 there, and a
second rule for one seed of a pair nobody asserts would be tuning to a
seed.

**The other change outside the setting that moves a calibration line.**
Besides the three eased Long Season levels above, one more line moves with
the setting off. With New Specials on and this setting off, Westlands
level 8 reads `1 1 2`
(seed 42: 1 survivor, the crew dealt 1537 damage) where it read `2 1 2`
(2293 dealt) before this work. The cause is fix O, METEOR RAIN holding its
fire over an ally: the same run with only that check taken out of the pack
reads `2 1 2` again, and every Westlands line then equals the measurement
from before this work. Every other line of every suite is unchanged with
the setting off, with New Specials on or off, except the three eased Long
Season levels. Level 8's floor is 0, so no test moves; other than the
eased levels, it is the one change outside the setting that a calibration
suite can see.

## The scene tests

Every fix above has a scene that plays it in the real game, in the
`GameplayFixes` suite of `tests/integration/test_special_scenes.cpp` (binary
`og_test_special_scenes`). Each scene is played twice from the same seed and
the same keys, the setting off and then on, with New Specials off in both so
every Shift is the classic one. The test asserts what the player sees on
each side and what must never show (the classic bug with the setting off,
the fix with it on), and hashes the seat's viewport after every frame to
prove the two runs identical until the frame the fix parts them.
Twenty-one scenes: A has three (resisted, nobody to disarm, the stagger), E
two, H two, K four, M two, and the rest one each. They run on every lane
like any other test, one gtest each.

`scripts/media/capture_gameplay_fixes.sh` records them: it runs the suite
with `OG_FX_CAPTURE_DIR` set, under which the shared play-through also
writes every frame of both runs and the index of the still, and encodes
each pair side by side (SETTING OFF on the left, SETTING ON on the right)
with a frame counter.

## Rulings (2026-10-16)

The maintainer answered the nine questions this work asked; each answer is
in effect.

1. **Turn Undead reach.** The reach is the larger of 4 px per level and
   60 px; the roll keeps its classic strength. Ruling: as built.
2. **A disarm with foes in reach that staggers nobody** costs half the
   price (75 of 150 MP) and the usual 5 busy and is refused as
   `DISARM RESISTED`; with nobody in reach it is refused free as
   `NO ONE TO DISARM`. Ruling: so, in both games (`2dc64408`).
3. **The DISARM detail text** now says what the special does: it staggers
   the foe you face so it waits before its next swing, and a stronger foe
   resists. Ruling: rewritten.
4. **MIND CONTROL's duration for a higher-level foe** in the old game drew
   a garbage number. Ruling: ported, not behind the setting, as its own
   commit (`cf5b1fe2`); no row reaches it and a full capture moved no
   golden.
5. **#354 is fixed in the current game only.** Ruling: leave it.
6. **The calibration suites assert on the shipped default** (both settings
   on). Ruling: so; a dip where the crew does worse is a balance bug, never
   a lowered floor (see [Balance](#balance)).
7. **Integration tests play the setting on.** Ruling: as built.
8. **HEAL and PROTECTION excluding the caster**, and **MIND CONTROL's
   25-tick duration against an equal-level foe**, stay as they are.
   Ruling: leave both.
9. **CI timeout.** The Run Tests lane's cap in `.github/workflows/test.yml`
   goes from 40 to 50 minutes (the lane ran 37 minutes on this PR and on
   master). Ruling: raised, in its own commit.

## Out of scope

Unchanged on purpose, with or without the setting:

- a level-1 bot that cannot afford its first special (the classic design);
- HEAL and PROTECTION never including the caster;
- MIND CONTROL's 25-tick charm against an equal-level foe;
- the poison cloud's damage of one point per thief level per tick;
- WARBAND repeating while Special is held;
- LEGION in the respawn modes;
- a dug-in skeleton holding the level open;
- bone walls and missiles;
- the radar on the frame a hero dies;
- captain promotion in the text and curses pickers;
- the mana regeneration pause (#360) and item specials (#177);
- new specials for the classes that have few.
