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
will use when it hosts, and the SETUP wizard's MATCH recap shows the host's
value as `GAMEPLAY FIXES: ON` or `GAMEPLAY FIXES: OFF`. The value is never
written into a company save.

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
sound, no busy time.

| | special | off (classic) | on |
|---|---|---|---|
| A | Soldier DISARM | A won roll against a foe two or more levels above the soldier staggers it by a negative amount, so it attacks sooner. "Fighter Disarmed Enemy!" prints whenever a foe was in reach, even when every roll was lost. | A stagger that is not positive is not applied. If nobody was staggered the cast is refused as `DISARM RESISTED`, so the 150 MP and the message come only with a real stagger. |
| B | Mage and archmage bots | A bot mage casts any special only when no foe or more than three foes are within 110 px (the TELEPORT rule, applied to every slot), so WARP SPACE, FREEZE TIME and ENERGY WAVE go off at nobody. Archmage slots 1 to 3 have no gate at all. | Each slot asks about its own reach (see [Bots](#bots)). |
| C | Bot Shift choice | A bot's Shift is a coin: a cleric throws TURN UNDEAD at living foes, a thief tries to charm a foe that will resist, an archmage chains or summons the elemental at random. | The coin is still drawn, then the gate sets Shift on purpose (see [Bots](#bots)). |
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

Every refusal is at most 24 characters, so it fits the compact HUD, and
spends nothing.

| special | refusal with the setting on | when |
|---|---|---|
| DISARM | `DISARM RESISTED` | foes in reach, none staggered |
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
  more than three foes are within 110 px. WARP SPACE, FREEZE TIME and
  ENERGY WAVE want a foe within the same 110 px. HEARTBURST wants a foe
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
  free). Slot 1 is unchanged: HEAL with any friend within 60 px, else the
  MYSTIC MACE at half mana.
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
  archmage's charm arm). `379c8516` is the commit the goldens are captured
  from: the captures matched the facts the new rows already carried, so no
  further table mirror was needed.
  `tests/parity/golden/DRIFT_LEDGER.md` records each one.

The rows with the setting on, by fix:

| fix | rows (setting on) |
|---|---|
| K | `soldier_whirlwind_no_foe_fixes_scen99`, `thief_taunt_no_foe_fixes_scen99`, `ghost_scare_no_foe_fixes_scen99`, `orc_howl_no_foe_fixes_scen99` |
| A | `soldier_disarm_higher_foe_fixes_scen99` |
| J | `special_cleric_heal_ally_fixes_scen99` |
| E | `cleric_turn_undead_reach_fixes_scen99`, `cleric_turn_no_undead_fixes_scen99` |
| D | `archmage_summon_no_room_fixes_scen99` |
| F | `archmage_mind_control_berserk_fixes_scen99` |
| G | `thief_charm_nearest_fixes_scen99` |
| H | `elf_volley_refund_fixes_scen99`, `elf_rocks_blocked_fixes_scen99` |
| I | `druid_tree_blocked_fixes_scen99` |
| B, C, L | `mage_bot_freeze_no_foe_fixes_scen99`, `mage_bot_heartburst_fixes_scen99`, `archmage_bot_summon_no_foe_fixes_scen99`, `cleric_bot_raise_or_turn_fixes_scen99`, `thief_bot_charm_or_taunt_fixes_scen99`, `archmage_hit_stale_shift_fixes_scen99` |
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
  `mage_bot_freeze_no_foe_fixes_scen99` instead.
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
- Fixes N, O and M's seat half (#354): no parity row can reach them (see
  [Not gated](#not-gated) and [Charm and seats](#charm-and-seats)).
- MIND CONTROL against a higher-level foe: the old game draws a garbage
  charm duration there (a negative bound cast to unsigned, stored in a
  short); the current game draws nothing and gives 25 ticks. No row reaches
  it, and it is recorded in the ledger, not ported.

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
setting off and New Specials off, and pass unchanged; nothing was re-pinned.
With the setting off, every measured line equals the line measured before
the fixes, for all four suites, with New Specials off.

What moves with the setting on, and why:

- The crew cleric, from crew level 4 (Westlands 6 to 26, Long Season 7 to
  19, Tower floors 15 and 20), raises instead of throwing TURN UNDEAD at
  living foes.
- A crew barbarian at crew level 7 to 9 rolls EXPLODING BOULDER half the
  time instead of a third (the slot roll stops at its second slot).
- Enemy mages and archmages keep the magic they used to waste at nobody,
  and enemy clerics raise instead of fizzling. This is what moves the
  early Long Season levels (2 to 5, crew level 1 to 3), where the crew's
  own cleric cannot turn undead yet: the enemy bots got better.

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
| 11 | 6 | 7 6 6 | 4 2 4 | 4 3 5 | 3 5 6 |
| 12 | 6 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 13 | 6 | 2 0 0 | 0 0 0 | 0 0 1 | 0 0 1 |
| 14 | 7 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 15 | 7 | 3 3 3 | 3 3 3 | 4 0 2 | 5 0 4 |
| 16 | 8 | 2 5 3 | 3 5 3 | 3 3 2 | 3 4 3 |
| 17 | 8 | 5 6 4 | 5 4 6 | 6 6 6 | 5 7 5 |
| 19 | 6 | 6 7 5 | 6 7 6 | 7 5 7 | 7 5 6 |
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
| 2 | 1 | 6 8 8 | 5 8 8 | 6 8 8 | 5 8 8 |
| 3 | 2 | 8 7 8 | 7 7 8 | 8 7 8 | 7 7 8 |
| 4 | 2 | 4 2 3 | 3 1 5 | 3 2 3 | 3 1 5 |
| 5 | 3 | 6 6 7 | 2 5 6 | 6 6 7 | 2 5 6 |
| 6 | 3 | 7 6 4 | 7 6 4 | 7 6 4 | 7 6 4 |
| 7 | 4 | 8 8 6 | 8 8 6 | 8 8 6 | 8 8 6 |
| 8 | 4 | 3 8 5 | 0 4 7 | 6 8 5 | 2 5 5 |
| 9 | 5 | 7 8 7 | 8 8 8 | 8 8 8 | 8 8 8 |
| 10 | 5 | 6 7 7 | 4 5 6 | 7 7 6 | 5 5 4 |
| 11 | 5 | 7 7 8 | 7 5 5 | 7 7 4 | 8 5 4 |
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

**Levels whose seed-42 survivors fall below the level's floor with the
setting on.** The asserting runs play the setting off, so none of these
fails a test; they are listed for a decision, not pinned:

- New Specials off: Westlands 23 (4, floor 5); Long Season 2 (5, floor 6),
  5 (2, floor 5), 8 (0, floor 3).
- New Specials on (the shipped pair): Westlands 11 (3, floor 4); Long
  Season 2 (5, floor 6), 5 (2, floor 5), 8 (2, floor 3).
- Tower: every seed stays inside its floors.

**The one change outside the setting that moves a calibration line.** With
New Specials on and this setting off, Westlands level 8 reads `1 1 2`
(seed 42: 1 survivor, the crew dealt 1537 damage) where it read `2 1 2`
(2293 dealt) before this work. The cause is fix O, METEOR RAIN holding its
fire over an ally: the same run with only that check taken out of the pack
reads `2 1 2` again, and every Westlands line then equals the measurement
from before this work. Every other line of every suite is unchanged with
the setting off, with New Specials on or off. Level 8's floor is 0, so no
test moves; it is the one change outside the setting that a calibration
suite can see.

## Questions (the defaults in play)

Each question has a default this work takes; the maintainer decides.

1. **Turn Undead reach.** With the setting on the reach is the larger of
   4 px per level and 60 px; the roll keeps its classic strength (160 per
   level), so a wider ring never changes who resists. Alternative: let the
   roll follow the reach, which would also change who resists among undead
   already in reach.
2. **A disarm that staggers nobody** is refused for free as
   `DISARM RESISTED`, so the fighter can try again at once. Alternative:
   the 150 MP and the busy time are still paid, as for a resisted charm.
3. **The DISARM detail text** ("lose the strength of attacks") stays,
   although the effect is a stagger; the help text says "a stronger foe
   resists".
4. **MIND CONTROL's duration for a higher-level foe** in the old game draws
   a garbage number. No row reaches it; it is recorded in the ledger and
   not ported. Alternative: an ungated port in the old game as its own
   commit.
5. **#354 is fixed in the current game only**: no capture can run the old
   game's seat scans, so a port would be code nothing observes.
   Alternative: port the old game's four scans anyway.
6. **The calibration suites assert with the setting off**; both values are
   measured and tabulated above. If they should play the shipped default,
   the moved floors get dated re-pins in a follow-up commit, only where the
   move is neutral or in the player's favour.
7. **Integration tests play the setting on** (the shipped default, as for
   New Specials); the tests that pin a classic bug set it off explicitly and
   have twins with it on. Alternative: integration plays off and only the
   new tests turn it on.
8. **HEAL and PROTECTION excluding the caster**, and **MIND CONTROL's
   25-tick duration against an equal-level foe**, stay as they are; changing
   either is a balance ruling.
9. **CI timeout.** If the Run Tests lane goes past 39 minutes with the new
   rows, its timeout goes from 40 to 50 minutes in this work.

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
