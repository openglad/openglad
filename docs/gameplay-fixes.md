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
| B | Mage and archmage bots | A bot mage casts any special only when no foe or more than three foes are within 110 px (the TELEPORT rule, applied to every slot), so WARP SPACE, FREEZE TIME and ENERGY WAVE go off at nobody, and a mage with 500 magic spends it on a crowd at its own feet. Archmage slots 1 to 3 have no gate at all. | Each slot asks about its own reach; FREEZE TIME asks about the allies' fight instead: three foes at blows with allies, or an ally at blows near death. Every special a bot can roll asks its own payoff rule (see [Bots](#bots)). |
| C | Bot Shift choice | A bot's Shift is a coin: a cleric throws TURN UNDEAD at living foes, a thief tries to charm a foe that will resist, an archmage chains or summons the elemental at random. | The coin is still drawn, then the gate sets Shift on purpose (see [Bots](#bots)); a cleric whose hero lacks the 60 INT for TURN UNDEAD never picks it. Every Shift arm is its own special with its own rule for a bot; the marker is no longer a coin. |
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
| Q | Bot payoff gates | Elf, archer, druid, barbarian, soldier, thief, ghost, orc and cleric bots cast specials at nobody, at foes out of reach or with allies in the blast: an elf throws rocks with no foe in sight, a thief drops its bomb at a foe 130 px or more away, a cleric throws its mace with nobody near. | Every special a bot can roll has the rule under which it pays off (see [Bots](#bots)); a Shift arm is its own special for a bot, with its own rule. |
| R | The struck archmage's order | A struck archmage bot with a foe within 200 px casts its 500-magic summon first and then casts slot 2 twice. | It answers with one HEARTBURST or CHAIN LIGHTNING chosen by the slot-2 rule: no summon, no second cast. Below its flee threshold it still teleports. |
| S | WHIRLWIND, DISARM, BARRAGE and EXPLODING BOLT after a shot | A hero (any walker with a character record: a player's, and every hero of a crew a bot drives) that has fired once can never cast these four again: each refuses as `SPECIAL BUSY`, for good. A hero fires on a fractional delay, the class's delay less a 47th of its dexterity (a soldier with 20 dexterity waits 5.57 ticks); busy counts down by one a tick only while it is above zero, so after a shot it rests a fraction below zero, and these four refuse while busy is not zero. | Busy at or below zero is not busy, as it already is for every other special. The firing delay and the countdown are unchanged, so nothing fires sooner. |
| T | Cleric RAISE UNDEAD, RAISE GHOST, RESURRECT | The stain's own 16x16 box is the test, only the nearest stain, the body at the corner, a drumstick under the corpse is eaten. | The body's own box, the first usable stain of those the finder answers, nudged up to 16 px; nothing eaten. |
| U | Soldier CHARGE and DISARM's front test | The question swings and adds busy, eats a treasure, opens a door. | Asked quietly: no swing, no busy, nothing eaten or opened, and the collision field untouched; a refusal spends nothing. |
| V | Soldier and elf bot gates | The gate said yes through a wall and onto a blocked pad. | The gate asks the cast's front, path and pad. |
| W | A bot's refused roll | The roll was the act; busy casts rolled while busy. | The ordinary act; the gate reads the cast's busy. |
| X | A walker's shove | A step that fails shoves the last ally any probe of the act met, from wherever the walker now stands: a walker a probe carried across a teleporter shoves the ally it left behind from the far pad, and a step the grid stopped shoves a body an earlier box met. The shoved ally loses its orders, and a shoved cleric heals at nobody. | Only a body the step's own boxes meet from where the walker stands is shoved. |
| Y | A bot's right-hand walk | Looking for a way round eats a potion one pixel to the side, swings at a body it grazes and turns the bot. | Asked quietly: the same way round, nothing eaten, no door opened, no swing at a body it merely grazed. A melee-only bot that finds its own foe in a probe's way still bumps it, as the classic did: that is how such a bot fights. |
| Z | CHARGE's strike | The rush strikes whatever body the collision field names after its last step, an ally included. An ally takes no damage, but the strike draws the damage roll, erases the ally's orders and walks it four steps, a seated hero too; a rush a wall stopped can strike a body met on an earlier step. | The rush strikes only a foe its last step ran into; an ally in the way gets the ordinary shove. |
| AA | Druid, barbarian, mage (ENERGY WAVE) and archer (EXPLODING BOLT) bot gates | Yes with a wall, the map edge or a foe's body on the weapon's pad; the cast refuses `COULD NOT FIRE`. | The gate asks the pad, as the elf's does (fix V); the archer asks it for the fire arrow it fires. |
| AB | A struck bot's cast | The hit response folds a slot the bot cannot pay onto slot 1 whatever slot 1 costs, the gate says yes, the roll is drawn and the cast refuses `NOT ENOUGH MP`. | A yes the bot cannot pay is a no: the roll is not drawn and the bot holds. |
| AC | Elf volleys, druid GROW TREE and SUMMON FAERIE at a wall or the map edge | Classic: the cast draws the throw's waver and refuses; the elf's refund leaves it a point ahead (fix H's off column). With fixes H and I alone the cast paid the one swing (1 MP for the elf, 4 for the druid) for a rock or bolt that met the wall and struck nothing. | A pad blocked by terrain refuses `COULD NOT FIRE` before the refund and the swing: nothing drawn, nothing spent. A body on the pad keeps H's and I's paid blow. |
| AD | A charmed tent or bones troop, a charmed summon | It wears the charmer's colour but keeps its owner's side: it fights its charmer and is struck by its charmer's team. | It heads its own owner chain while the charm lasts; its owner, its charm clock and its tent's cascade are unchanged. |

TELEPORT, REVEAL, CLOAK and ENERGY WAVE are unchanged as casts (their bot
rules are in [Bots](#bots)): each does something whenever it is cast
(ENERGY WAVE crosses walls along the facing). CHARGE still refuses `PATH
BLOCKED` when something stands against the soldier's front and otherwise
moves the fighter; fix U changes only how it asks, and fix Z what the rush
strikes.

Fixes T to W, in play:

- **T.** A raised body stands where it fits. The cast walks the stains its
  finder answers, nearest first, and raises the first one inside the
  cast's range whose body (a skeleton, a ghost, or for RESURRECT the
  corpse's own family when the stain is friendly and a ghost when it is
  not) has room on the corpse or up to 16 px from it; the caster's own
  body is in the way like any other. A corpse against a wall, in a
  one-tile corridor or under the cleric now rises, and `CORPSE IS BLOCKED`
  means that no stain in range had room. The nearest usable stain still
  decides RESURRECT, so a foe's stain in front of a friendly one rises as
  a ghost, as before. The finder looks no further than it did: its limit
  is a squared centre distance under 800 (`walker::distance_to_ob_center`),
  about 28 px of centre, so the cast's 60 and 30 px ranges never reach
  past it, and the sentence about it under [Bots](#bots) stands (issue
  #390 read the 800 as pixels). With the setting on the raise no longer
  eats a drumstick under the corpse; the body eats it when it walks over
  it, as any living does.
- **U.** CHARGE and DISARM both ask "is my front blocked?", a one-pixel
  step along the soldier's heading. In the classic game that question is
  a real bump: against a body it starts a swing (busy, and two acts later
  a knife blow that costs 2 MP and hurts the foe), it eats a treasure in
  front and opens a door the soldier has the key for, and a bot whose
  heading is stale is turned a notch. With the setting on the same answer
  comes back and nothing else happens, so a refused CHARGE costs nothing
  and a DISARM adds only its own 5 ticks of busy (a bot's 11 and a
  level-10 hero's 9.7 before).
- **V.** The soldier bot's CHARGE gate and the elf bot's volley gate ask
  what the casts ask, quietly (see [Bots](#bots)). Before, the CHARGE gate
  read positions only and said yes through the Labyrinth's one-cell walls
  and into a foe already touching the soldier, and the elf's gate never
  looked at the spot its rock would leave from.
- **W.** A bot rolls for its special one act in five. In the classic game
  that roll was the act: a gate's no, or a cast's refusal, left the bot
  standing for the tick. With the setting on a roll that cast nothing
  takes the act the coin would have given it, and each gate whose cast
  first asks "am I busy?" (or "am I mid-teleport?", or "am I resting?")
  asks the same before it says yes.

Fixes X to AD, in play:

- **X.** A walker whose step is refused shoves the ally in its way: the
  shove draws a roll, and two times in three the ally loses its orders
  and is sent four steps along the shover's path (a seat is never
  shoved). The classic walk shoves the body the collision field names,
  which is the last body any probe of the act met, not necessarily one
  the failed step touched. A probe that crosses a
  teleporter carries the walker away mid-step, and the walk then shoves,
  from the far pad, the ally it left behind; a step the grid stopped
  shoves a body an earlier box of the same walk met. On The Tryxian
  Chronicles' first level that was 23 shoves from more than 100 px (the
  farthest 624 px), 7 of which took the ally's orders, and a shoved cleric
  casts its shove heal at nobody. Issue #403 compared two counters ("5 on
  master, 23 with the batch": 5 and 7 are the thefts, 23 the far
  attempts); both are 0 with the fix. With the setting on the walk shoves
  only a body that one of the step's own three boxes (the step, and the
  step rounded up and down, as the walk probes it) meets from where the
  walker stands now. The collision field itself is left as it was, so
  what else reads it reads the classic value.
- **Y.** A bot that cannot walk straight at its foe follows its right hand
  round the obstacle (and a bot sent to a point does the same). Before
  each step it asks whether the spots one pixel to its right, its right
  front and its right back are blocked. In the classic game each of those
  questions is a real bump, as fix U's front test was: it eats a potion or
  a meal one pixel aside that the bot never stands on (a magic potion took
  an orc from 10 MP to 550 in the test), swings at a body it grazes (busy,
  then a thrown knife) and turns the bot. With the setting on the
  questions are asked quietly: the same answers, so the same way round,
  and nothing eaten, no swing, no turn. One exception, measured
  2026-11-01: an orc, ghost or slime has no aimed attack (its shot is
  refused before any aim; the attack order only faces a foe at bump
  range), so its blows are bumps, and the right-hand walk's probes were
  about 43 % of them in the classic. Asked wholly quietly, those bots
  started a third fewer swings, landed an eighth fewer blows and dealt a
  fifth less damage across the four campaigns, and one that stopped
  exactly against a standing foe never swung again. So a melee-only bot
  whose own foe stands where a probe looks still runs into it, as the
  classic did. Everything else the probe meets stays untouched, and a bot
  with a ranged weapon throws no knife at a body it grazed. The crew no
  longer picks up a potion beside its path, which can move a battle (see
  [Balance](#balance)).
- **Z.** CHARGE runs three acts of three steps and after each act strikes
  the body the collision field names, erases that body's orders and walks
  it four steps along the rush ("a violent shove", the 2002 comment says).
  Issue #406 read this as a charge that hurts an ally. It never does:
  `walker::attack` refuses a friend. But it draws the damage roll before it
  refuses, and the order theft and the four-step walk ran on the ally
  anyway, a seated hero included, with no wall check. A rush a wall stopped
  could also strike a body named from an earlier step (fix X's shape).
  With the setting on the rush strikes only a body that is not friendly
  and that the last step's box ran into; an ally in the way gets the
  ordinary shove of the walk (two times in three, never a seat).
- **AA.** The druid's GROW TREE and SUMMON FAERIE, the barbarian's HURL
  BOULDER and EXPLODING BOULDER, the mage's ENERGY WAVE and the archer's
  EXPLODING BOLT fire a weapon from the caster's pad, as the elf's volleys
  do, and fix V gave only the elf's gate the pad question. Their gates said
  yes with a wall, the map edge or a foe's body on the pad, and the cast
  refused `COULD NOT FIRE`: on The Tryxian Chronicles' first level, with
  fixes T to W in, 16 times for the druid and 3 each for the barbarian,
  the mage and the archer. With the setting on each of these gates asks
  `G.pad_clear` too. The archer asks it with the fire arrow (9x9) its
  EXPLODING BOLT fires, not the arrow (7x7) it holds: the pad question
  now takes the weapon family the cast will fire (#419). Issue #419's
  worry, that the soldier's, the cleric's and the archmage's weapon hooks
  resize the weapon after the pad is asked, does not hold: none of those
  specials fires through the pad question, and none of the hooks moves
  the pad (a test compares the asked pad with `fire()`'s on every
  heading). The EXPLODING BOLT's swap of weapons was the one real
  difference. The pad question does not see everything that stops a
  weapon; see [Bots](#bots) for what is left.
- **AB.** A struck bot answers with its special one time in three when its
  gate says yes. Before it asks, it folds a slot it cannot pay onto slot 1
  and drops a Shift arm it cannot pay. Issue #409 read the fold as coming
  after the gate; it comes before. What nothing asked was whether slot 1
  itself could be paid, and the hit response, unlike the act, has no guard
  for it (the act rolls a special only when the bot has slot 1's magic).
  So the gate said yes, the roll was drawn, and the cast refused `NOT
  ENOUGH MP`: 19 times on The Tryxian Chronicles' first level, every one on
  slot 1 (the elemental 16, the soldier 3), the same in the old game. With
  the setting on the gate's answer (`living::check_special`) also asks
  whether the bot can pay the slot and arm the gate ended on, after the
  gate has chosen the arm: a yes the bot cannot pay is a no, the roll is
  not drawn and the bot holds. The act never reaches a slot it cannot pay,
  so it does not change.
- **AC.** Fixes H and I made a blocked volley or bolt cost exactly the one
  swing, because with a foe on the pad the swing is the cast's own blow.
  Issue #410 found the same swing paid with nothing on the pad to strike:
  a rock whose pad lies on a wall face or past the map edge cost the elf
  1 MP with the setting on (a druid's bolt 4) and hit nothing. With the
  setting on the elf's four volleys, GROW TREE and SUMMON FAERIE ask what
  the pad holds first (`walker:fire_pad_answer`); a pad blocked by terrain
  refuses `COULD NOT FIRE` before the refund and the swing, drawing and
  spending nothing. Terrain is a wall face, the map edge, and a tree crown
  for a weapon that cannot cross the forest (the druid's bolt; the elf's
  rock crosses it). Water is never terrain here: every weapon crosses it.
  A body on the pad keeps H's and I's paid blow (question 1 of the
  [rulings](#rulings-2026-11-01)). A bot never meets the new refusal,
  because its gate asks the pad first (fixes V and AA).
- **AD.** A tent or a bones pile owns the troops it makes, and every
  question of whose side a walker is on climbs its owner chain to the
  top: `is_friendly`, the blow's friend test and the score's team. A
  thief's CHARM OPPONENT or an archmage's MIND CONTROL on such a troop,
  or on an enemy caster's summon, changes its colour but not its owner,
  so it wears the charmer's colour and answers for its owner's side: it
  fights its charmer and is struck by its charmer's team. With the setting
  on every such climb stops at a charmed walker (one whose charm clock is
  running), so the troop fights for the colour it wears until the charm
  ends. Its owner, its clock and the cascade when its tent falls are
  unchanged. The level-play test met the classic shape once: an enemy
  thief counted a crew-raised ghost as a foe and taunted it while the ghost
  wore the thief's own colour, because the ghost's chain still answered
  for the crew's cleric.

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
| RAISE UNDEAD, RAISE GHOST, RESURRECT | `CORPSE IS BLOCKED` | no room for the body within 16 px of the corpse, on every stain in range (free) |
| CHARGE | `PATH BLOCKED` | a wall, a body or a door one pixel ahead along the heading (free: no swing, no busy) |
| the elf's volleys, GROW TREE, SUMMON FAERIE | `COULD NOT FIRE` | a wall face or the map edge on the weapon's pad, or a tree crown on the druid's (free: nothing drawn, no swing; fix AC) |

`NO ROOM TO SUMMON` (SUMMON ELEMENTAL) and `COULD NOT FIRE` keep their
text. With the setting on `NO ROOM TO SUMMON` costs nothing extra. A pad
blocked by terrain costs nothing (AC); a body on the pad costs exactly the
one swing (H, I). A door, a grown tree or an enemy weapon in flight on the
pad is not seen by the pad question, and `fire()`'s own probe passes such a
body only six times in ten, so a cast at one can still cost the swing.

## Bots

A bot picks a slot, draws its Shift coin, drops a Shift it cannot pay, then
asks the slot's gate whether to cast. With the setting on every draw the
classic bot makes is still made, in the same order: the gates read the
world with finders that draw nothing, a family whose classic gate looked
for a foe still looks for it first, and where a gate chooses the Shift it
writes it after the coin with `set_shifter_down`, as the classic cleric
gate already did. A gate that newly says no removes the hit response's one
follow-up draw on that tick; that happens only with the setting on. Fix W
adds draws after the classic ones, never before or between them: where the
classic bot's act ended on a refused roll, the fixed bot goes on to draw
its ordinary act.

With the setting on, every special a bot can roll has a rule of its own:
the condition under which that special pays off, judged by what it does
and what it costs, never the caster's own danger when a cheaper answer
exists. A Shift arm is its own special for a bot, with its own rule, and
the rule picks the arm; the coin is still drawn and then overwritten. The
rules share one small library (`packs/core/lib/gates.lua`) of predicates
such as "at least N living foes within R px", "the bot faces the foe" and
"the foe is within the weapon's flight", and the old game carries the same
predicates once (`src/bot_gates.h`). Three measures recur:

- "Within a blow" is the engine's melee reach, a gap of at most 5 px
  between the two bodies (the 6 px weapon box sits 1 px outside the
  attacker and reaches gaps 1 to 6, and the collision check moves the other
  body's near edge in by 1 px), the same at every level; a knife thrown
  from 45 px is not a blow.
- "The flight" of a fired weapon is how far it goes from this caster
  toward the foe, measured as the gap between the two bodies. A bot's
  weapon leaves along the heading of its last step (at spawn that is
  upward, whatever way the sprite looks), so "faces the foe" means that
  heading points at it. The weapon's table range gains one step per three
  levels (a hero's character record adds a few more), a straight shot
  flies about 1.2 times as many steps of 1.4 times the length, and the
  first step only turns the weapon. A level-1 rock reaches a 35 px gap on a
  diagonal and 56 px straight; a fire arrow 88 and 147; a fireball 36 and
  59; a meteor 56 and 89.
- "The blast" of a bomb, an exploding bolt or an exploding boulder reaches
  15 px plus 4 px per level, never under 31 nor over 111; allies in it take
  half the damage and the caster a quarter, so a blast rule refuses when an
  ally of the caster stands in it.

Every other distance is the engine's ordinary one, between the bodies'
top-left corners. A generator (a tent, a tower that makes things) is never
counted where the rule says "living foe"; WHIRLWIND and HOWL count it,
because the spin strikes it and the yell stuns it (fix K).

- **Soldier.** CHARGE wants the classic window, a foe one to three squares
  off, and a living foe in the path the rush runs; and the front is clear
  and the rush's path to that foe is open (fix V): the cast's own front
  test, asked quietly, finds nothing against the soldier (a foe already
  touching it is a blow, not a charge), and no wall, ally, door or
  generator stands on the steps before the foe. The rush walks three
  acts of three steps along the soldier's heading and strikes and shoves
  the first foe its body meets, the window's foe or any other: a foe in
  the way pays even when the heading does not point at the window's foe,
  and a foe inside the window but past the nine steps does not. A soldier
  that has not stepped yet has no heading and rushes nowhere. BOOMERANG
  wants a living foe inside the blade's widest orbit, 17 px plus 6 per
  level, and no blade of this soldier out. WHIRLWIND wants at least two
  bodies it can strike inside the spin (32 px plus 2 per level); one in
  reach is what the free swing hits. DISARM wants a living foe blocking the
  soldier's front within its 28 px reach that is not above the soldier's
  level.
- **Elf.** ROCKS and BOUNCING ROCKS want the elf's nearest living foe faced
  and inside the volley's flight (the bouncing volley flies half again as
  far). LOTS OF ROCKS and MEGA ROCKS want the same nearest foe faced and in
  flight, and at least two living foes inside their longer flights. Every
  volley also wants the rock's pad clear (fix V): the spot the first rock
  would be placed on holds no wall face, no map edge and no foe's body (a
  body of the elf's own team passes, as the rock passes it; a door, a grown
  tree or a weapon in flight passes the question too, see below). The
  classic elf threw at nobody.
- **Archer.** FIRE ARROWS, the ring, wants at least two living foes inside
  the arrows' flight, or one the archer does not face. BARRAGE wants the
  archer's foe, a living one, faced and inside the flight. EXPLODING BOLT
  wants that foe faced and inside the flight, with no ally of the archer,
  and not the archer, inside the blast around it, and the fire arrow's pad
  clear (fix AA: the bolt is a fire arrow, 9x9, so the pad is asked for it
  and not for the 7x7 arrow the archer holds).
- **Mage.** Slot 1 keeps the classic rule: TELEPORT when no foe or more
  than three foes are within 110 px. The TELEPORT MARKER is no longer a
  coin: with nobody within 110 px the mage marks where it stands when no
  marker of its own stands yet, a hero has the 75 INT the cast asks, and
  half the magic left after the price still covers WARP SPACE, so marking
  never costs it its fight; from then on its wander teleports go home to
  the marker. WARP SPACE is a point-blank ring: its fireballs crawl one
  pixel a tick, so at level 7 with a full pool the ring reaches about 20 px
  around the mage; it wants at least two living foes inside it, or one at
  the mage's side that it does not face. FREEZE TIME is worth its 500 magic
  for the free blows the time stop hands the allies, so it waits for their
  fight: at least three foes within a blow of allies (the mage itself not
  counted), or an ally at blows that is under a quarter of its health; how
  close the foes stand to the mage is not asked, and a mage on another
  team takes the same cue for its own freeze. ENERGY WAVE wants the mage's
  nearest living foe within 110 px to be one it faces (the wave runs about
  that far along the heading, through walls) and the pad it leaves from
  clear (fix AA). HEARTBURST wants a foe inside its own reach, 80 px plus 2
  per level. A struck mage bot below its flee threshold teleports without
  asking a gate, as the classic one does; struck again while that teleport
  plays, it casts nothing (#408: the hit response asks `mid_teleport`
  first, as the TELEPORT gate does).
- **Archmage.** TELEPORT and the MARKER have the mage's rules, with
  HEARTBURST (80) as the special the halved pool must still cover.
  HEARTBURST or CHAIN LIGHTNING: the coin's choice is kept when it has a
  target, swapped when only the other has one, and the cast refused when
  neither has (the chain gate uses the chain's reach by level, 200 px plus
  7 per level, so a bot with a character record can be told "the chain has
  a target" and then refused for nothing by the cast, which uses half its
  Intelligence instead; a candidate for a later refinement). SUMMON
  ELEMENTAL wants a living foe within 200 px and spare magic of at least
  the slot's own 500. SUMMON IMAGE, a phantom with one hit point, is a
  decoy, so it pays before the blows land: at least two living foes within
  200 px are hunting the archmage and none is within a blow of it yet (at
  blows, HEARTBURST is the cheaper answer), its health is above the flee
  threshold (below it TELEPORT is the answer), no phantom of its own
  stands, and magic is left after the 500 for a HEARTBURST. MIND CONTROL
  wants a foe it can take (never charmed, not a hero a player drives, not
  above the archmage's level) inside its reach, 80 px plus 4 per level,
  and the slot not resting. A struck archmage bot teleports below its flee
  threshold as before, and casts nothing while that teleport plays (#408,
  the mage's guard); otherwise it answers with one HEARTBURST or CHAIN
  LIGHTNING chosen by the slot-2 rule, never with a summon and never with
  a second cast (the classic game summoned first and then cast slot 2
  twice). A player archmage's Shift stays the Shift key.
- **Cleric.** HEAL wants an ally within 60 px that is below its full
  health (the classic gate healed any friend and the cast refused). MYSTIC
  MACE wants no such ally, a living foe within 48 px (the mace orbits on a
  24 px circle and strikes what it touches), no mace of this cleric out,
  at least half the pool, and a hero with the 50 INT the cast asks. A
  shoved cleric heals, never maces. Slots 2 and 3 cast TURN UNDEAD only
  with an undead foe inside the turn reach and the hero's 60 INT; else
  RAISE UNDEAD wants a corpse the body can rise on within 60 px and a
  living foe within 130, RAISE GHOST such a corpse within 30 px and a
  living foe within 130 (the gate asks the cast's own question, fix T; the
  stain finder itself never looks past about 28 px of centre, so a stain
  is in practice one at the cleric's feet). RESURRECT wants the first
  usable stain within 30 px to be friendly; a foe's stain is RAISE GHOST's
  job at a third of the price.
- **Thief.** DROP BOMB wants a living foe inside the bomb's blast around
  the thief and no ally inside it; never a foe beyond it (the classic gate
  bombed at a foe 130 px or more away). With New Specials on, MINE instead
  wants a foe past the blast and within 130 px that is walking at the
  thief, fewer than three mines down, no ally in the blast, and the 70 to
  pay it; never the coin. CLOAK wants a living foe within 130 px that is
  hunting the thief, and no invisibility left. TAUNT wants a living foe
  inside the taunt's reach and an ally inside it too (foes pulled onto a
  thief nobody backs is the thief's own danger, bought for 100; a tent
  cannot follow a taunt, so it is no payoff). CHARM OPPONENT, as before,
  only a foe it can take inside charm reach. POISON CLOUD wants at least
  two living foes within a blow of the thief and no cloud of the thief's
  alive.
- **Ghost.** SCARE wants a living foe inside the scare radius (50 px plus
  10 per level, at most 250; the classic gate used 130, which the fright
  reaches only from level 8), and SCARE not resting.
- **Druid.** GROW TREE wants the druid's nearest living foe within 60 px
  faced and not yet within a blow (a foe touching the druid blocks the
  bolt), and the bolt's pad clear (fix AA). SUMMON FAERIE wants a living
  foe within 130 px, no faerie of the druid's alive, and the pad clear.
  REVEAL is never cast by a bot: it changes a radar only a player sees.
  PROTECTION wants an ally within 60 px with a living foe at blows with it
  and no fresh circle (none, or one under 50 hp).
- **Orc.** HOWL wants a body the yell can stun (a living foe or a
  generator) inside the yell (160 px plus 20 per level, at most 420), not
  130, and HOWL not resting. EAT CORPSE wants the orc below 60 percent of
  its health and a stain under it, whatever the foes are doing (the
  classic gate asked for a foe within 130 and let the meal refuse).
- **Barbarian.** HURL BOULDER wants the nearest living foe faced and
  inside the boulder's flight: eight steps of 2 px per level (at most 15;
  a hero's steps come from its strength), so a 16 px gap at level 1 and
  120 px from level 8. EXPLODING BOULDER wants that foe to have a second
  living foe inside the blast around it, with no ally and not the
  barbarian inside it. Both want the boulder's pad clear (fix AA).
- **Fire elemental.** STARBURST, the ring of meteors, wants at least two
  living foes inside the ring's flight, or one the elemental does not
  face.
- **Skeleton.** TUNNEL keeps the classic rule, nobody within 80 px, with
  the classic count.
- **Slime.** SPLIT and GROW are unchanged: one body becoming two is the
  slime's whole game, and GROW refuses for free without room.
- **The New Specials kits** (faerie, captain, skeleton, elemental, ghost,
  thief) have their rules described in [new-specials.md](new-specials.md);
  they are behind that setting, not this one.
- **Every family.** The slot roll stops at the last named, opened slot (fix
  L), so a level-13 mage can roll HEARTBURST and a level-16 soldier rolls
  each of its four specials equally. An unaffordable roll still folds to
  slot 1 and the hit response still asks the slot the bot holds; with a
  payoff rule on every slot neither habit casts at nothing any more, and
  with fix AB a slot 1 the bot cannot pay is a no too.

A gate asks the cast's own first guard (fix W). A cast that opens by
refusing `SPECIAL BUSY` while its caster is busy, mid-teleport or resting
has a gate that asks the same thing first, with the same predicate
(`living_common`'s `is_busy` and `mid_teleport`, fix S's
`special_busy` for the soldier's and archer's four, the faerie's own
`resting`): the soldier's WHIRLWIND and DISARM, the archer's BARRAGE and
EXPLODING BOLT, the mage's and archmage's TELEPORT and TELEPORT MARKER,
the archmage's HEARTBURST, CHAIN LIGHTNING, SUMMON ELEMENTAL and MIND
CONTROL, the cleric's MYSTIC MACE and TURN UNDEAD, all four druid
specials, the thief's TAUNT, CHARM OPPONENT and POISON CLOUD, both
barbarian specials, the orc's HOWL and the skeleton's TUNNEL. A gate that
acquires a foe the classic way still does so first. CHARGE, BOOMERANG,
the elf's volleys, HEAL, the raises, DROP BOMB, MINE, CLOAK, SCARE, the
slime and the fire elemental do not read busy and are left alone. The
New Specials kits' gates ask the same guards (the captain's HOWL and
KNIFE FAN, the ghost's SIPHON, the faerie's GLIMMER and BLINK, the
skeleton's DIG IN); those gates are keyed on New Specials, so their
guards hold whatever this setting says.

A roll the gate or the cast refuses is not the bot's act (fix W). In the
classic game a bot's one-in-five special roll was its act for the tick,
cast or not: a gate's no or a cast's refusal left it standing. With the
setting on, a roll that cast nothing (a gate's no, or a cast that refused
after the gate's yes) takes the act the coin would have given it: the
second one-in-five coin, then a random walk or the hunt for its foe. A
roll that cast still ends the act, and a struck bot's hit response is
unchanged (it has no act to fall into). A refusal that already acted,
the resisted DISARM's half price and busy (fix A), is followed by the
ordinary act too.

A yes the bot cannot pay is a no (fix AB). The gate chooses the arm, and
`G.can_pay` drops a Shift arm the bot cannot pay; with the setting on the
engine then checks the slot and arm the gate ended on against what the cast
will charge, and answers no when the bot cannot pay it. Only the hit
response can meet that no: the act rolls a special only when the bot has
slot 1's magic, and a slot it cannot pay is folded onto slot 1 before the
gate is asked.

What the gates still do not see:

- A door, a grown tree or an enemy weapon in flight on the pad. The pad
  question is asked with a weapon, and a weapon passes other weapon-order
  bodies (doors and trees are weapon-order) when it is asked quietly;
  `fire()` asks loudly and passes them six times in ten. A cast at such a
  pad can still refuse `COULD NOT FIRE` and pay its swing.
- Room for the faerie. SUMMON FAERIE asks the bolt's pad, not whether the
  faerie's body has room where it would stand, so with a clear pad it can
  still refuse `NO ROOM TO SUMMON` (an ally on the spot).
- The soldier's empty hands. A soldier whose blades are all out has its
  throw vetoed by its own weapon hook, which the pad question cannot see.
  No soldier special fires a weapon, so no gate meets it today.

`tests/integration/test_level_play.cpp` (binary `og_test_level_play`) checks
the rules in a real level: it plays The Tryxian Chronicles' first level, its
104 bots on four warring teams plus a crew of 12 heroes at level 14, for 2400
ticks on six seeds, and counts every cast, every refusal and every roll a
gate holds. On the shipped default the bots cast about 480 times a seed (2887 on
the six seeds) and 47 different specials, among them the eight Shift arms the level
reaches (MYSTIC MACE 31, CHARM OPPONENT 31, MINE 24, CHAIN LIGHTNING 20, TURN
UNDEAD 15, WAIL 10, TELEPORT MARKER 9, HASTE SELF 7) and the crew's slot-5 HEARTBURST
and FREEZE TIME, none at nobody (each special whose rule names a foe has
one within its reach, worked from the gate's own arithmetic), REVEAL never,
and no gate says yes to a cast that then refuses for the fact the gate
reads. With the setting off the same level shows 245 casts at
nobody, REVEAL 30 times and 199 such refusals, so the test
tells the rules from their absence.

The same run counts the refusals fixes V and W are about. Pooled over the
three seeds on the shipped default, before fixes T to W the soldier bots
had met `PATH BLOCKED` 139 times to 61 charges (a wall or the map edge 74
times, a foe already touching 19, an ally 12, a door 2, the rest a blocker
gone by the end of the tick or a caster gone), the elves `COULD NOT FIRE` 80
times to 44 volleys (a wall on the rock's pad 57 times, a foe on it 15, the
rest the same), and casts were refused `SPECIAL BUSY` 154 times. With
fixes T to W in they read `PATH BLOCKED` 0 to 35 charges, `COULD NOT
FIRE` 5 to 51 volleys and `SPECIAL BUSY` 4, and the gates held 2698
rolls, each of which now takes the bot's ordinary act. The `SPECIAL BUSY`
refusals left then came from casts no gate was asked about, among them
the mage bot's own hit response: below its flee threshold it teleported
without asking its gate, and a mage struck again while that teleport
played was refused (#408 removed that call).

For fixes X to AD it counts the far shoves, the price refusals and the
pad refusals. With fixes T to W in, it read 23 shoves from more than 100 px (7 of them taking the ally's
orders), 19 `NOT ENOUGH MP` refusals, and the druid, barbarian, mage and
archer bots' `COULD NOT FIRE` 16, 3, 3 and 3 times. With fixes X to AD in
it reads, on the six seeds, 0 far shoves, 0 `NOT ENOUGH MP`, `COULD NOT
FIRE` 0 for those four families and 11 for the elves (2, 0, 0, 4, 4 and 1
a seed), `SPECIAL BUSY` 0 and `PATH BLOCKED` 0; the gates hold
5170 rolls. The test caps each at its measured count plus a third
(at least 3), and far shoves at 0: `SPECIAL BUSY`, `NOT ENOUGH MP`, `PATH
BLOCKED` and the four families' `COULD NOT FIRE` at 3, the elves' at 15.
Before the amendment of fix Y (a melee-only bot still bumps its own foe)
the elves read 5 (cap 7) and the four families 2, both the druid's SUMMON
FAERIE; the amendment moves the whole stream, and the caps were measured
again on it. What is left of `COULD NOT FIRE` and `NO ROOM TO SUMMON` comes
from the shapes the gates do not see (above): each of the elves' 11 is an
enemy weapon in flight on the rock's pad (9 knives, 2 rocks), which the
gate's quiet question passes and `fire()`'s loud one met and lost the
weapon clash roll to; the druid's one `NO ROOM TO SUMMON` (seed 3) is
the faerie's own room, which no gate asks about.

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
  After the bot payoff gates ruling (Q and R): the shared predicates
  `d179d3ae` (no behaviour), the arenas' table mirror `7bd2d19d`, then one
  commit per family's rule set, `edfeb3fc` (soldier), `a4677a26` (elf),
  `c6f93cc9` (archer), `3d8ae98a` (mage), `16abf504` (barbarian),
  `b63c5a29` (cleric), `62bf0963` (thief), `fbe8d6f2` (ghost), `0a42040a`
  (druid), `a81dbc46` (orc), `99b873be` (fire elemental), `744a8f32`
  (archmage) and `b6d05567` (the struck archmage, R); the table mirrors
  `93b718e8` and `c14bde27` after them; and `734229df`, which gives the
  ghost's ported gate the current game's 250 px cap on the scare radius.
  After the campaign calibration: `238b5322` (the soldier bot's CHARGE
  wants a living foe in the path its rush runs) and its table mirror
  `5d4ddd77`; no golden moved. Fix S was ported as `4440414c`, with the
  table mirror `813d5f31`; no golden moved. Fixes T to W: the table
  mirror `0f1c407d` (the five new arenas), then `81b24d2e`, the probes the fixes
  ask with and no behaviour (the passability probe's quiet flag, a
  detached body probe that draws nothing, the stain walk and the rock's
  pad; and, not behind the setting, a guard the current game already had:
  an ownerless probe over an arrow slit no longer dereferences a null
  owner; no golden moved), `1dfa6ab5` (T), `d983e120` (U), `4a94b64a` (V: the
  soldier bot's CHARGE), `ecd10c07` (V: the elf bot's volley), `b77fa3ec` (W:
  the busy casts' gates read busy) and `5bb8b94f` (W: a refused roll is not
  the act), with the table mirror `d6c9e094` after them. Then, not behind
  the setting, `0182905f`: a summoned MYSTIC MACE starts centred on its
  caster, at its level, as the current game's `og.summon` has done since
  `19d7eeb3` (one golden moved, the cleric bot's raise-or-turn row with the
  setting on); and the table mirror `8fdb91b8`, which gives the archmage
  bot's summon row with the setting on spawns of its own. Fixes X to AD:
  the table mirror `15f14eb9` (the new arenas and the two mage freeze rows'
  own spawns), then `7b611c1f`, the probes and helpers the fixes ask with and
  no behaviour (the test of whether a step's boxes meet a body, beside the
  obmap's collision rule; the pad question taking a weapon family and
  saying what it met; the right-hand walk's probes taking a quiet flag,
  still loud everywhere; `set_weapon_heading` placing the weapon with
  `fire()`'s pad table; the unused `bg_corpse_near` deleted; no golden
  moved), `bf38a718` (X), `7f37d034` (Y), `bac61dd0` (Z), `38d71581` (AA),
  `157e2619` (AB), `a5900f72` (AC) and `00887f9c` (AD), with the table
  mirror `16f7503e` after them (the exact facts the recaptures broke, worked
  out again, and four setting-on rows on a seed or spawns of their own:
  see below). The amendment to Y: the table mirror `e4780cfc` (five pins
  re-pointed by line and the struck orc's hp in the elemental row worked out
  again; no golden moved), then `7ff1654a` (a melee-only bot whose own foe
  stands where a right-hand probe looked still bumps it; three goldens with
  the setting on moved).
  `7ff1654a` is the commit the goldens are captured from: 351 of 351 goldens
  `cmp` equal to a full capture there.
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
| B, C, L | `mage_bot_freeze_no_foe_fixes_scen99`, `mage_bot_freeze_alone_fixes_scen99`, `mage_bot_freeze_engaged_fixes_scen99`, `mage_bot_heartburst_fixes_scen99`, `archmage_bot_summon_no_foe_fixes_scen99`, `cleric_bot_raise_or_turn_fixes_scen99`, `thief_bot_charm_or_taunt_fixes_scen99`, `archmage_hit_stale_shift_fixes_scen99` (the first two in arenas of their own since fixes X to AD: see below) |
| M | `thief_bot_charm_hero_fixes_scen99`, `archmage_bot_mind_control_hero_fixes_scen99` |
| Q | `archer_bot_bolt_at_blows_fixes_scen99`, `archer_bot_ring_one_foe_fixes_scen99`, `archmage_bot_image_one_foe_fixes_scen99`, `barbarian_bot_boulder_far_fixes_scen99`, `barbarian_bot_explode_lone_fixes_scen99`, `cleric_bot_mace_alone_fixes_scen99`, `cleric_bot_resurrect_foe_stain_fixes_scen99`, `druid_bot_faerie_alone_fixes_scen99`, `druid_bot_protection_idle_fixes_scen99`, `druid_bot_tree_behind_fixes_scen99`, `elemental_bot_starburst_one_foe_fixes_scen99`, `elf_bot_rocks_far_fixes_scen99`, `mage_bot_marker_home_fixes_scen99`, `mage_bot_warp_two_at_blows_fixes_scen99`, `soldier_bot_boomerang_far_fixes_scen99`, `soldier_bot_charge_unaimed_fixes_scen99`, `soldier_bot_disarm_front_fixes_scen99`, `soldier_bot_whirlwind_one_foe_fixes_scen99`, `thief_bot_bomb_ally_fixes_scen99`, `thief_bot_bomb_far_fixes_scen99`, `thief_bot_cloud_alone_fixes_scen99` (21 new arenas, each with its setting-off twin `<arena>_scen99`) |
| R | `archmage_bot_struck_fixes_scen99` (no setting-off twin: see below) |
| T | `cleric_raise_blocked_corpse_fixes_scen99` (a new arena, with its setting-off twin) |
| U | `soldier_charge_front_foe_fixes_scen99` (a new arena, with its setting-off twin) |
| V | `soldier_bot_charge_door_fixes_scen99`, `elf_bot_rocks_blocked_fixes_scen99` (two new arenas, each with its setting-off twin) |
| W | `soldier_bot_refused_roll_acts_fixes_scen99` (a new arena, with its setting-off twin) |
| X | `orc_stale_shove_fixes_scen99` (a new arena, with its setting-off twin) |
| Y | `orc_right_walk_potion_fixes_scen99` (a new arena, with its setting-off twin), and the 13 recaptured rows below; the amendment (a melee-only bot bumps its own foe) recaptured `orc_right_walk_potion_fixes_scen99`, `mage_bot_freeze_engaged_fixes_scen99` and `elemental_struck_short_pool_fixes_scen99` again |
| Z | `soldier_charge_ally_fixes_scen99` (a new arena, with its setting-off twin) |
| AA | `druid_bot_faerie_pad_body_fixes_scen99`, `barbarian_bot_boulder_pad_body_fixes_scen99`, `mage_bot_wave_pad_body_fixes_scen99`, `druid_bot_tree_pad_body_fixes_scen99` (four new arenas, each with its setting-off twin) |
| AB | `elemental_struck_short_pool_fixes_scen99` (a new arena, with its setting-off twin), and the 10 recaptured rows below |
| AC | `elf_rocks_edge_pad_fixes_scen99` (a new arena, with its setting-off twin) |
| AD | `thief_charm_tent_troop_fixes_scen99` (a new arena, with its setting-off twin; the setting-on twin has spawns of its own: see below) |

The bot payoff gates moved five existing rows with the setting on, each
recaptured from the old game after its port: `cleric_bot_raise_or_turn_fixes_scen99`
(the raise now waits for a corpse), `thief_bot_charm_hero_fixes_scen99` (with
no ally the taunt is refused), and three rows whose level-1 soldier bot
stands in its CHARGE window on tick 0 with no heading and now holds the rush:
`cleric_turn_no_undead_fixes_scen99`, `thief_charm_nearest_fixes_scen99` and
`thief_bot_charm_or_taunt_fixes_scen99`. The last also had its arena rebuilt
(an orc of the thief's inside its blast, seed 27, a 20-tick budget) so that
its setting-on row reaches the charm rule again rather than ending on a
tick-1 bomb; its setting-off twin `thief_bot_charm_or_taunt_scen99` was
recaptured with the new arena, the one setting-off golden this work changed,
and only because its arena changed. Every other setting-off golden is
byte-identical. Three rows expected to move did not:
`archmage_hit_stale_shift_fixes_scen99` never strikes the archmage (its
summon is the bot's own slot-3 roll), and `mage_bot_freeze_no_foe_fixes_scen99`
and `mage_bot_heartburst_fixes_scen99` record the same bytes under the new
mage rules.

Fixes T to W moved existing rows with the setting on, each recaptured
from the old game after its port; no setting-off golden changed. T moved
none: the cleric bot rows' gates now ask the cast's walk, and on their
arenas it answers as the old probe did. U moved
`soldier_bot_disarm_front_fixes_scen99`, in the random stream only (the
loud front test turned the bot a notch; the quiet one does not). The three
player DISARM rows it was expected to move record the same bytes: a
player's stale heading only sets where it will turn, and nothing these
recordings read follows within their budgets. W moved 33 of the
48 rows with the setting on (W1 two, W2 32, one of them in both): a bot in each meets a refused roll within
the row's budget and now takes its ordinary act;
`tests/parity/golden/DRIFT_LEDGER.md` lists them. Each exact fact a
recapture broke was worked out again from the new capture and its pin
proven again.

The second bugs batch (fixes X to AD) moved 23 existing rows with the
setting on, each recaptured from the old game after its port; no
setting-off golden changed. Measured in the current game before the port,
Y moved thirteen: `archmage_hit_stale_shift`, `cleric_bot_raise_or_turn`,
`cleric_bot_resurrect_foe_stain`, `cleric_raise_blocked_corpse`,
`druid_tree_blocked`, `elf_rocks_blocked`, `mage_bot_freeze_engaged`,
`soldier_bot_disarm_front`, `soldier_charge_front_foe`,
`soldier_disarm_higher_foe`, `soldier_disarm_resisted_busy`,
`soldier_disarm_resisted_price` and `thief_bot_bomb_ally` (each
`_fixes_scen99`): in each a bot's right-hand walk now asks its side
probes quietly, and the random stream parts there. AB moved ten:
`archer_bot_bolt_at_blows`, `archer_bot_ring_one_foe`,
`archmage_summon_no_room`, `druid_bot_tree_behind`, `elf_volley_refund`,
`mage_bot_heartburst`, `mage_bot_warp_two_at_blows`,
`soldier_bot_whirlwind_one_foe`, `soldier_whirlwind_no_foe` and
`special_cleric_heal_ally` (each `_fixes_scen99`), every one through a
struck bot short of its slot-1 price, most of them the arenas' target
towers. X, Z, AA, AC and AD moved none; in particular
`elf_rocks_blocked_fixes_scen99` and `druid_tree_blocked_fixes_scen99`,
whose pads hold a body, keep H's and I's paid blow under AC (they move
under Y only). The old game's captures, one after each port, name the
same rows: X, Z, AA, AC and AD move only their own new rows, Y moves
its thirteen and AB its ten (and the new rows the ledger lists), and nine
of Y's thirteen move again under AB in the old game (a target tower in
each is struck short of its slot-1 price), where the current game's
bisection saw them move once. The amendment to Y (a melee-only bot bumps
its own foe, ported in `7ff1654a`) moved one of them again,
`mage_bot_freeze_engaged`, and two newer rows whose orcs fight at blows,
`orc_right_walk_potion` and `elemental_struck_short_pool` (each
`_fixes_scen99`; in the last the struck orc now ends at 67 hp, not 96).
The old game's capture after the port moves exactly these three, and each
`cmp`s equal to the current game's dump. Of Y's thirteen, the other twelve are byte-equal before and after: eleven
have no melee-only bot, and in `thief_bot_bomb_ally` no bump of its orc
changes a byte. After the last port every moved capture
`cmp`s equal to the current game's dump but one:
`thief_charm_tent_troop_fixes_scen99` differed only by the four explosion
sounds of the tent dying at tick 238. The old game plays a generator's
explosion only on screen, and the tent stood below the classic 320 x 200
view, while the current game sounds it wherever the tent stands.

The recaptures also broke exact facts in eight of the rows:
`elf_rocks_blocked`, `druid_tree_blocked`, `mage_bot_heartburst`,
`cleric_bot_raise_or_turn`, `special_cleric_heal_ally`,
`archmage_summon_no_room`, `mage_bot_warp_two_at_blows` and
`cleric_raise_blocked_corpse` (each `_fixes_scen99`). Each was worked out
again from the new capture and its pin proven again. Four setting-on rows
had lost what their mutation shows, and take a seed or spawns of their
own while their setting-off twins stay as they were:
`cleric_raise_blocked_corpse_fixes_scen99` takes seed 15 (on the old seed
its faerie died only at the cast, so no skeleton could rise),
`soldier_disarm_higher_foe_fixes_scen99` seed 2 (the soldier no longer won
its roll), `cleric_bot_resurrect_foe_stain_fixes_scen99` seed 939 (no
RESURRECT roll came while the stain was in reach), and
`thief_charm_tent_troop_fixes_scen99` stands its tent at (160, 150),
inside the view, as the engaged freeze arena did for an orc's HOWL. The
table mirror `16f7503e` carries them; the capture there moves those four rows
alone, and each `cmp`s equal to the current game's dump.

A setting-on twin may stand in an arena of its own. The maintainer ruled
this in PR #404: when a row's arena leaves its setting-on mutation nothing
a fact can read, the setting-on twin gets spawns of its own and its
setting-off twin stays as it was. The first row to take it was
`archmage_bot_summon_no_foe_fixes_scen99` (`8fdb91b8`): its archmage
starts in scen1's corner at (60, 60), where nothing can stand beside it,
so the summon its mutated gate let through was refused `NO ROOM TO
SUMMON` and nothing showed. The spawn-room check that came with #423
found two more such casters: `mage_bot_freeze_no_foe_fixes_scen99` and
`mage_bot_freeze_alone_fixes_scen99` had their mage in the lake at that
corner, where a living is given a tick of flight and loses 1 hp a tick.
Both now start at (160, 160) on grass, with their seeds and budgets kept,
each recaptured from the old game; their setting-off twins are untouched.

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
- Fix S: no parity row can reach it. A parity spawn has no character
  record, so its fire delay is a whole number and busy always counts down
  to exactly zero; the old game's port (`4440414c`) moves no golden. The
  current game's tests reach it with a hero instead (see [The scene
  tests](#the-scene-tests) and `GameplayFixesSpecialBusy` in
  `tests/integration/test_gameplay_fixes_special_busy.cpp`).
- MIND CONTROL against a higher-level foe: the old game drew a garbage
  charm duration there (a negative bound cast to unsigned, stored in a
  short); the current game draws nothing and gives 25 ticks. The old game
  now does the same, whatever the setting (`cf5b1fe2`); no row reaches it,
  and a full capture moved no golden. The soft cap above an edge of 12
  (the current game's runaway knee) remains the one unported, unreached
  difference.
- The bot payoff gates whose rule no arena can show (each has a unit test
  that reaches it instead): BARRAGE (a bot that walked at its foe is aimed
  at it, and a second foe makes the ring pay too); ENERGY WAVE (the classic
  gate casts only with no foe or four or more within 110 px, and four
  towers around the mage always put one on its heading); MIND CONTROL (the
  cast already refuses for free before anything happens, so the gate's no
  changes only the tick); RAISE UNDEAD and RAISE GHOST (without a corpse
  the raise refuses before anything a recording shows); CLOAK
  (invisibility is not recorded, and a cloak with no hunter differs only
  in the random stream); SCARE (with the setting on the cast already
  refuses with no living foe in the radius); REVEAL (a refused REVEAL
  changes only the druid's busy time); HOWL (every foe within 130 px is
  within the yell, so the rule only adds casts at 130 to 180 px, where a
  bot searches its foe for a tick or two); EAT CORPSE (a parity spawn
  cannot start hurt, and a tower that hurts it is a foe within 130 px
  within a few ticks). The second to fourth elf volleys and the New
  Specials kits have no arena either: the volleys share the first volley's
  rule, and no parity row plays New Specials.
- W's busy guards have no arena of their own. On the act path a gate's
  busy no and the cast's `SPECIAL BUSY` refusal are both silent and, with
  W's fall-through, take the same ordinary act, so the two recordings
  would be the same bytes. On the hit path the
  guard removes the follow-up draw a gate's yes makes; that is pinned by
  the rows W moved and by `GameplayFixesBots` in
  `tests/unit/test_gameplay_fixes_bots.cpp`, which asks every guarded gate
  with its caster busy.
- `archmage_bot_struck_scen99`: the struck archmage's arena has no
  setting-off twin, because the two games' classic hit responses differ.
  The old game's `walker::special` always answers 0, so after a summon its
  hit response never returns and its "then leave! :)" second cast never
  happens, while the current game's classic hit response uses the cast's
  real answer. No golden reaches that difference; it is recorded in the
  ledger.
- Fix X's own shape, the far shove across a teleporter: no arena can
  stage it. An arena's spawns go into the world's list of walkers, and a
  teleporter finds its partner only among the effects, so an arena's pad
  goes nowhere. The unit tests in `tests/unit/test_gameplay_fixes_walk.cpp`
  (`GameplayFixesWalk`) stage it with a real pad pair, and the level-play
  test counts far shoves.
- #408: the skipped call was refused before it did anything, so both
  values record the same bytes; `StatsHitResponseTeleport` in
  `tests/integration/test_stats_hit_response_teleport.cpp` pins it.
- #411: the kill credit is a field of the current game alone, not in the
  recording; the mode tests (`ModesOnslaught`, `ModesTdm`) pin it.
- #417 and #418: no byte of the sim changes; every row proves it.
- #413, #414, #415 and #416: the display and the lobby, which no recording
  reaches.
- Fix AA's two terrain pads. `archer_bot_bolt_pad_terrain`: a bot faces
  up at its spawn and no tile that stops a weapon sits directly above
  ground on scen1, so the only terrain arena is an archer beside a tree
  trunk turned by a blocked first walk; over seeds 0 to 400 the two runs
  then differ only in the random stream or the archer's final position
  in one direction, so no fact lets each row's own pin flip a fact of its
  own. `GameplayFixesBots.archer_exploding_bolt_asks_the_fire_arrows_own_pad`
  and the gate's unit tests stand for it. `druid_bot_tree_pad_terrain`:
  with fix AC a GROW TREE at terrain refuses before it spends, and a
  refused cast and a gate's no take the same ordinary act, so the gate's
  pad term has nothing to show on terrain; `druid_bot_tree_pad_body` (a
  tower on the bolt's pad) took its place.

## Not gated

Nine changes apply whatever the setting, three from the work that added it (N, O
and P) and six from the second bugs batch:

- **N. A slot that is not in play says so.** `walker::special()` refuses a
  slot priced 5000 (an undeclared slot, or one New Specials hides) before it
  compares the price with the walker's magic, as `NO SPECIAL AVAILABLE`
  (`SpecialFailure::Unavailable`). Nothing in play reaches it: the player's
  special cycling and the bots' slot pick both skip such slots, and a New
  Ability Select key naming one is refused by the server first, as NO
  ABILITY THERE ([ability-select-design.md](ability-select-design.md), C.7). The old
  game has the same hole and no row reaches it, so it is not ported.
- **O. METEOR RAIN holds its fire over an ally** (New Specials only). A bot
  fire elemental's METEOR RAIN gate refuses when an ally stands in the
  strike box: within 71 px of the target on each axis (the rain lands
  within its spread of the target and each blast reaches 31 px; the rain
  hurts allies at half). With New Specials off the gate is not asked.
- **P. The archer's comment** on its default cast now names the slots it
  serves (slot 3 and every unmapped slot; slot 4 is undeclared). Comment
  only.
- **The mage's flee teleport casts nothing mid-teleport (#408).** The mage's
  and the archmage's hit responses teleport below their flee threshold;
  struck again while that teleport plays, they now skip the call
  (`lc.mid_teleport` first), which the cast refused as `SPECIAL BUSY` anyway
  before drawing, spending or moving anything. No byte changes on either
  value; only the refusal's trace is gone. The old game refuses the same
  call silently, so nothing is ported.
- **A tent's or bones' troop is its own kill credit (#411).** The modes
  credit a kill to the attacker the blow records (TDM's frags, Onslaught's
  10 points, LEGION's raise, the elemental's double burn on the foe that
  last struck it). The record named the top of the attacker's owner chain
  only when that top was a living, so a troop's chain, which ends at its
  tent, recorded nobody, or left the victim's earlier attacker in place.
  It now records the topmost living on the chain (`walker::credit_head`):
  the troop itself, a weapon's thrower, a summon's caster. Classic team
  score and experience still go to the chain's top: a tent's troop never
  earned experience in either game, and it pays the classic team score
  when its tent is on the player's team. The record exists in this game
  only.
- **A probe no longer grows the world (#417).** Every read-only look into
  the obmap's buckets (the spawn-spot probe, the landing and wake probes,
  the path search's cost, the text client's grid and object commands)
  reads `obmap::find_list`, which answers nothing for an empty cell
  instead of making an empty bucket there; `obmap_get_list` stays for
  `GameWorld::find_near_foe`, which edits the bucket it reads. No byte of
  the sim changes. The obmap debug overlay draws fewer empty boxes. The old
  game's spawn probe already reads with a find.
- **One pad table (#418).** `walker::set_weapon_heading`, which places the
  test weapon of a shooter's line-of-fire check, had its own copy of the
  eight-heading pad table; it now places the weapon with
  `walker::weapon_pad`, the table `fire()` uses. The gate predicate
  `G.corpse_near`, which no pack read since fix T, is gone, and so is the
  old game's `bg_corpse_near`. No byte of the sim changes.
- **The F1 FREEZE TIME cheat freezes the level (#413).** In local play the
  screen the cheat wrote is a copy that the next snapshot overwrites, so
  the freeze froze nobody and the blue lasted a tick (the issue saw only
  the colour). The cheat now writes the server world: the snapshot carries
  the freeze and the blue to every view, and FREEZE TIME's own rule thaws
  it. In a networked game it does nothing (a host's cheat would change
  every player's game, and a joiner has no server world). A replay recorded
  while cheating plays back differently, because a cheat is not recorded
  input. This game only.
- **Water and lava flow again (#414).** Since June 2026 (`de9d02b2`) every
  snapshot repainted the palette and put the water and lava colour bands
  back: measured over six frames, the bands' turn read 1, 0, 0, 1, 0, 0
  where the cycle reads 1, 1, 1, 2, 2, 2, two frames shown in turn. One
  writer, `screen::show_palette_id`, now paints only when the palette
  shown changes; anything else that writes the colours marks it unknown,
  and a mission's end forces the restore. This game only.

## Balance

All four calibration suites were measured on both values of the setting,
and on both values of New Specials, with their own seeds (42, 1337 and
2025). Each cell is the crew's survivors at tick 600 for the three seeds.
The suites' asserting runs (seed 42 against each level's floor and
ceiling; every seed on the Tower) play the shipped default, Gameplay Fixes
on and New Specials on, and all of them pass. No floor or ceiling was
moved. The tables are measured with the bot payoff gates (fixes Q and R)
in, after the calibration that followed them (the soldier's CHARGE rule
and The Old Count's Vault, below), and with fix S in (see "After fix S"
below for what it moved), before fixes T to W (see "After fixes T, U, V
and W" below for the lines measured with them).

With the setting off and New Specials off, every line equals the line
measured before the bot payoff gates, and the line measured before the
fixes, except the four eased Long Season levels below (2, 5 and 8 before
the bot gates, 12 after them). With New Specials
on and the setting off, lines move: the kits' bot rules (the faerie, the
captain, the skeleton, the elemental, the ghost's WAIL and PHASE and the
thief's MINE) are behind New Specials, not this setting.

What moves with the setting on, and why:

- Every bot casts a special only where it pays off (see [Bots](#bots)).
  The crew's elf, archer and barbarian throw only at a foe they face
  inside the flight, a crew soldier CHARGEs only when a foe stands in the
  path its rush runs, and the crew cleric heals only a hurt ally and swings
  its mace only with a foe at its orbit. The enemy's bots cast better
  too: a thief bombs only with the crew inside its blast and lays its
  mines under a foe walking at it, elves and archers volley only at
  someone, and orcs howl from the yell's full radius. That the crew takes
  more from them is the game playing as it should.
- A gate that refuses a cast also skips the random draw the classic
  bot's hit response made after it, so the whole random stream of a
  600-tick battle shifts. On one seed a line can move by several
  survivors either way without any rule being better or worse for the
  crew; the twenty-seed means below are what tell a real move from that.
- The crew cleric, from crew level 4 (Westlands 6 to 26, Long Season 7 to
  19, Tower floors 15 and 20), raises instead of throwing TURN UNDEAD at
  living foes, and raises only with a corpse at its feet and a foe to
  fight.
- A crew barbarian at crew level 7 to 9 rolls EXPLODING BOULDER half the
  time instead of a third (the slot roll stops at its second slot).
- Enemy mages and archmages keep the magic they used to waste at nobody.
- The crew cleric's HEAL stops at full health. The classic heal took a
  lightly scratched crew well over its maximum (a soldier at 367 of 286 on
  Westlands 11, the elf at 197 of 154 on Long Season 8), and that extra
  health was spent in the fight. This is what took the early Long Season
  levels (2, 5 and 8) under their floors on seed 42 before the bot gates,
  and why those levels were eased.
- A soldier bot of level 10 and up that is resisted on DISARM now pays
  75 MP and waits 5 ticks (it paid nothing before the ruling).

**Westlands** (survivors at tick 600, seeds 42 1337 2025)

| level | crew | New Specials off: setting off | setting on | New Specials on: setting off | setting on |
|---|---|---|---|---|---|
| 1 | 1 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| 2 | 2 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| 3 | 2 | 8 8 6 | 7 8 5 | 8 8 6 | 7 8 7 |
| 4 | 3 | 8 8 8 | 8 7 8 | 7 8 7 | 8 7 8 |
| 5 | 3 | 8 8 4 | 8 6 4 | 8 8 4 | 8 6 4 |
| 6 | 4 | 5 8 7 | 8 7 7 | 5 8 7 | 8 7 7 |
| 7 | 4 | 0 0 0 | 1 1 0 | 0 0 0 | 1 1 0 |
| 8 | 5 | 0 1 4 | 0 0 6 | 1 1 2 | 0 3 6 |
| 9 | 6 | 2 0 0 | 0 2 2 | 1 1 2 | 0 2 1 |
| 10 | 5 | 0 0 0 | 0 0 0 | 1 0 0 | 1 0 0 |
| 11 | 6 | 7 6 6 | 5 6 7 | 6 5 5 | 5 5 4 |
| 12 | 6 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 13 | 6 | 2 0 0 | 0 0 0 | 0 0 1 | 1 0 0 |
| 14 | 7 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 15 | 7 | 3 3 3 | 2 3 0 | 4 2 3 | 2 1 1 |
| 16 | 8 | 2 5 3 | 6 3 4 | 5 2 3 | 3 3 2 |
| 17 | 8 | 5 6 4 | 6 6 7 | 5 8 8 | 3 6 6 |
| 19 | 6 | 6 7 5 | 5 5 6 | 5 6 5 | 5 7 5 |
| 20 | 7 | 0 0 0 | 0 0 6 | 0 0 0 | 0 0 0 |
| 21 | 7 | 8 7 8 | 8 8 8 | 8 8 7 | 8 7 8 |
| 22 | 8 | 0 2 0 | 4 0 0 | 0 1 0 | 0 2 0 |
| 23 | 8 | 5 6 6 | 5 4 4 | 4 5 4 | 8 6 5 |
| 24 | 8 | 1 4 7 | 5 4 3 | 5 5 1 | 3 5 5 |
| 25 | 9 | 8 8 8 | 5 6 7 | 4 6 5 | 8 4 5 |
| 26 | 9 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |

**Long Season**

| level | crew | New Specials off: setting off | setting on | New Specials on: setting off | setting on |
|---|---|---|---|---|---|
| 1 | 1 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| 2 | 1 | 7 7 7 | 6 6 7 | 7 7 7 | 6 6 7 |
| 3 | 2 | 8 7 8 | 8 8 8 | 8 7 8 | 8 8 8 |
| 4 | 2 | 4 2 3 | 2 1 3 | 4 2 3 | 2 3 3 |
| 5 | 3 | 8 8 8 | 8 6 4 | 8 8 8 | 8 6 4 |
| 6 | 3 | 7 6 4 | 7 6 5 | 7 6 4 | 7 6 5 |
| 7 | 4 | 8 8 6 | 8 7 8 | 8 8 6 | 8 7 8 |
| 8 | 4 | 3 7 8 | 7 7 6 | 3 7 7 | 6 7 7 |
| 9 | 5 | 7 8 7 | 8 8 8 | 8 8 7 | 8 8 8 |
| 10 | 5 | 6 7 7 | 5 6 7 | 3 6 5 | 4 6 7 |
| 11 | 5 | 7 7 8 | 6 7 8 | 7 7 8 | 6 7 7 |
| 12 | 6 | 8 5 7 | 6 6 7 | 8 5 7 | 6 6 7 |
| 13 | 6 | 5 6 4 | 5 8 4 | 4 4 2 | 5 7 4 |
| 14 | 7 | 7 7 8 | 7 7 8 | 7 7 8 | 7 7 8 |
| 15 | 7 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| 16 | 7 | 6 8 6 | 7 5 7 | 6 8 6 | 7 5 7 |
| 17 | 8 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 18 | 8 | 0 0 1 | 0 0 0 | 0 0 0 | 0 0 0 |
| 19 | 8 | 8 8 8 | 8 8 8 | 8 8 8 | 8 8 8 |

**Tower** (floor, crew)

| floor | crew | New Specials off: setting off | setting on | New Specials on: setting off | setting on |
|---|---|---|---|---|---|
| 1 | 1 | 8 8 7 | 8 7 7 | 8 8 7 | 8 7 7 |
| 5 | 2 | 2 4 8 | 0 0 8 | 1 4 8 | 3 0 8 |
| 10 | 3 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |
| 15 | 4 | 0 0 0 | 0 4 0 | 0 0 0 | 0 0 0 |
| 20 | 6 | 0 0 0 | 0 0 0 | 0 0 0 | 0 0 0 |

**Imaginations** (level 1, crew 1): `6 6 6` with the setting off, `6 7 6`
with it on, on both values of New Specials.

**Before the bot payoff gates: levels that fell below their floor, and
what was done.** Westlands 11 (3 against 4): fix C's INT rule; it then
read `6 5 6`. Long Season 2 (5 against 6), 5 (2 against 5) and 8 (2
against 3): the levels were eased (The Ferry Right has one brigand fewer
on the west approach, Two Banners one picket fewer, and The Paymaster
Vanishes fields its five terrace-2 bruisers at level 1 instead of 2); they
then read `7 6 6`, `7 8 7` and `5 8 5`. No floor moved. The easing moved
the same levels' lines with the setting off too, on both values of New
Specials: Long Season 2 went from `6 8 8` to `7 7 7`, Long Season 5 from
`6 6 7` to `8 8 8`, and Long Season 8 from `3 8 5` to `3 7 8` (New
Specials off) and from `6 8 5` to `7 6 7` (New Specials on). None of them
was under its floor. With New Specials off and the setting on, Westlands
11 read `3 4 4` against its floor of 4 on that unasserted pair; the move
was recorded, not fixed, because the same rule took the shipped default
from 3 to 6 there, and a second rule for one seed of a pair nobody
asserts would be tuning to a seed.

**After the bot payoff gates: levels outside their band, and what was
done.** With the gates in, five lines read outside their band on seed 42:
Westlands 8 read 3 against its ceiling of 2 and Westlands 9 read 8
against its ceiling of 3 (the crew did better than the level allows),
Westlands 23 read 4 against its floor of 5, Long Season 7 read 5 against
its floor of 6, and Tower floor 1 read 5 against its floor of 6. They were
treated as balance bugs, as the earlier ones were.

- One rule was wrong: the soldier's CHARGE. The gates first asked that the
  soldier's heading point at the foe in the classic window. The classic
  rush pays whichever foe is in its path: it runs three acts of three
  steps along the heading and, after each act, strikes the body it ran
  into and shoves it four steps. Counted over the crew soldiers' CHARGE
  rolls on the three seeds across the three campaigns, the aimed rule
  refused about 2,200 rushes that would have struck a foe (the window's
  foe at a slant the eight-way heading missed, or another foe in the way)
  and allowed about 400 that stop short of anyone (a foe past about 50 px
  is inside the window's 75 but past the nine steps). The rule now asks
  for what the crew was getting: the classic window, and a living foe in
  the path the rush runs (see [Bots](#bots)). With it all five lines are
  back in band on seed 42: Westlands 8 reads 0, Westlands 9 reads 0,
  Westlands 23 reads 7, Long Season 7 reads 8 and Tower floor 1 reads 8
  (`8 7 7` on the tower's run seeds, each over the floor).
- One level was eased. The new rule took Long Season 12, The Old Count's
  Vault (crew 6), to 1 on seed 42 against its floor of 2 (it read 8 and 7
  on the other two seeds, and its twenty-seed mean rose, 5.65 to 5.90).
  The second skeleton of the breach watch now stands at level 2 instead of
  3, the smaller of the two edits tried (taking that skeleton out read
  `7 4 5`); the level reads `6 6 7`, where it read `1 8 7`. With the
  setting off it now reads `8 5 7` on both values of New Specials (it read
  `3 8 8`).
- No floor and no ceiling moved. With Westlands 8 and 9 at 0 on seed 42
  no ceiling is breached, so the war rows' rule (the seed-42 reading plus
  two, at most 7) re-pinned none.

Twenty seeds (42, 1337, 2025 and 1 to 17, measured for this check only)
show how far those lines and the lines that moved most went on average.
"Classic" is the shipped default with every bot rule back at its text
before the bot gates and "first gates" the bot gates with the aimed CHARGE
rule, both measured before The Old Count's Vault was eased (only its row
is affected); "now" is the shipped default as merged.

| line | its pin | classic | first gates | now |
|---|---|---|---|---|
| Westlands 8 | ceiling 2 | 2.95 | 2.35 | 2.35 |
| Westlands 9 | ceiling 3 | 1.35 | 1.55 | 1.35 |
| Westlands 23 | floor 5 | 6.20 | 5.80 | 6.00 |
| Long Season 7 | floor 6 | 7.40 | 7.30 | 7.75 |
| Long Season 8 | floor 3 | 6.35 | 5.15 | 5.40 |
| Long Season 10 | floor 1 | 5.55 | 5.00 | 5.50 |
| Long Season 12 | floor 2 | 6.60 | 5.65 | 6.20 |
| Tower floor 1 | floor 6 | 7.50 | 7.55 | 7.30 |
| Tower floor 15 | floor 0 | 2.20 | 0.95 | 0.95 |
| Westlands, summed | | 104.8 | 106.0 | 105.8 |
| Long Season, summed | | 110.8 | 109.9 | 111.0 |
| Tower, summed | | 12.1 | 11.2 | 11.4 |

One seed's survivors on these lines spread by 0.7 to 2.3, so the
difference of two twenty-seed means carries a standard error of about
0.25 to 0.7: a half-survivor move is about one standard error. The
enemy thief's rules account for most of Long Season 8's fall.

**Recorded, not eased.** These lines are read against the game before the
bot gates (the shipped default with every bot rule at its earlier text),
on the three seeds and on twenty seeds, measured as the "classic" column
above. Three lines read under their floor on a seed the suites do not
assert: Long Season 5 (floor 5) reads `8 6 4` (it read `7 8 7`; its
twenty-seed mean went from 6.30 to 6.60), Westlands 5 (floor 6) `8 6 4`
(it read `8 5 5`, already under on two seeds; 6.55 to 6.35) and Westlands
25 (floor 5) `8 4 5` (it read `8 3 7`, already under on seed 1337; 6.05 to
6.25). Six lines fell a point or more on two of the three seeds while
staying at or over their floor: Westlands 11, 15, 16 and 19 and Long
Season 4 and 14 (Westlands 9 fell too, a war row whose floor is 0). Over
twenty seeds none of the six fell by more than about one standard error,
and three rose: Westlands 11 went from 4.30 to 5.45, Westlands 15 from
2.40 to 2.35, Westlands 16 from 2.45 to 2.95, Westlands 19 from 5.70 to
5.60, Long Season 4 from 2.55 to 3.20 and Long Season 14 from 7.75 to
7.55. Long Season 2 reads exactly its floor of 6 on seed 42 (`6 6 7`,
where it read `7 6 6`; 6.45 to 6.20) and Long Season 14 exactly its floor
of 7. Easing a level whose twenty-seed mean did not fall would tune it to
the three measured seeds, so none of these was eased.

With New Specials on and the setting off, a pair that is measured but not
asserted, Westlands 23 reads `4 5 4` against its floor of 5 (it read
`6 7 8` before the kits' bot rules) and Westlands 25 `4 6 5` (it read
`1 8 7` before them, already under). With New Specials off and the
setting on, the other unasserted pair, three lines read under their floor
on a seed: Westlands 3 `7 8 5` (floor 6; it read `8 8 7` before the bot
gates), Westlands 23 `5 4 4` (`4 7 6`, already under on seed 42) and Long
Season 5 `8 6 4` (floor 5; `7 8 7`). Westlands 5 reads under on seed 2025
in all four columns, both settings off included, so it predates this work.
Over a ceiling on a seed the suites do not assert: Westlands 8 reads
`0 3 6` on the shipped default against its ceiling of 2 (it read `2 0 2`
before the bot gates; its twenty-seed mean fell, 2.95 to 2.35, so the
crew does no better on average), and with New Specials off and the
setting on Westlands 8 reads `0 0 6` and Westlands 20 `0 0 6` against
ceilings of 2 and 5. The ceilings are asserted on seed 42 alone, where
both read 0.

**After fix S.** The crews are heroes with character records, so a crew
soldier or archer that has thrown once could not cast WHIRLWIND, DISARM,
BARRAGE or EXPLODING BOLT again before fix S; now it can. With the setting
off no line moved (both setting-off columns read as before). With it on,
eleven lines moved on a seed: on the shipped default Westlands 13 (`2 0 0`
to `1 0 0`), 17 (`5 6 6` to `3 6 6`), 21 (`8 8 8` to `8 7 8`), 23 (`7 5 7`
to `8 6 5`) and 24 (`3 5 4` to `3 5 5`) and Long Season 11 (`6 8 7` to
`6 7 7`) and 16 (`6 7 7` to `7 5 7`); with New Specials off, Westlands
11, 15, 17, 21, 22, 24 and 25 and Long Season 11 and 16. The Tower,
Imaginations and the level-play test did not move. Every asserted run
passes and no floor or ceiling moved. Every moved line stays inside its
band on all three seeds but one: with New Specials off and the setting
on, a pair nobody asserts, Westlands 22 reads `4 0 0` against its ceiling of 3 (it read
`2 0 0`); the ceiling is asserted on seed 42 of the shipped default
alone, where the level reads 0. Over twenty seeds (42, 1337, 2025 and 1 to
17) on the shipped default, no line's mean moved by more than 0.40
survivors, about one standard error: the largest moves are Westlands 15
(2.35 to 2.75), Long Season 8 (5.40 to 5.10), Long Season 10 (5.50 to
5.25) and Westlands 21 (7.80 to 7.65), and the sums go from 105.85 to
106.25 (Westlands) and from 111.00 to 110.55 (Long Season). The
twenty-seed table above is measured before fix S.

**After fixes T, U, V and W (2026-10-29).** The four suites were measured
again on the shipped default (Gameplay Fixes on, New Specials on) with
fixes T to W in, on the same three seeds. "Before" is the shipped default
just before them, measured at the start of that work; it equals the last
column of the tables above.

What the four fixes change in a battle. W moves the whole random stream:
every refused roll of every bot now draws its ordinary act, and with the
busy guards in a bot's gate says no where its cast would have refused, so
on one seed a line can move by several survivors either way, as it did
after the bot payoff gates. T lets the clerics on both sides raise beside
walls, in corridors and on the corpse they stand over. U takes the free
knife blow out of a blocked CHARGE or DISARM. V spends fewer soldier and
elf rolls on casts that refuse.

**Westlands** (survivors at tick 600, seeds 42 1337 2025, the shipped default)

| level | crew | before T to W | after |
|---|---|---|---|
| 1 | 1 | 8 8 8 | 8 8 8 |
| 2 | 2 | 8 8 8 | 8 8 8 |
| 3 | 2 | 7 8 7 | 8 8 6 |
| 4 | 3 | 8 7 8 | 7 7 8 |
| 5 | 3 | 8 6 4 | 5 7 5 |
| 6 | 4 | 8 7 7 | 7 5 4 |
| 7 | 4 | 1 1 0 | 0 0 0 |
| 8 | 5 | 0 3 6 | 2 6 4 |
| 9 | 6 | 0 2 1 | 2 1 0 |
| 10 | 5 | 1 0 0 | 0 0 0 |
| 11 | 6 | 5 5 4 | 4 1 6 |
| 12 | 6 | 0 0 0 | 0 0 0 |
| 13 | 6 | 1 0 0 | 1 1 2 |
| 14 | 7 | 0 0 0 | 0 0 0 |
| 15 | 7 | 2 1 1 | 3 4 1 |
| 16 | 8 | 3 3 2 | 2 3 3 |
| 17 | 8 | 3 6 6 | 8 7 8 |
| 19 | 6 | 5 7 5 | 5 4 6 |
| 20 | 7 | 0 0 0 | 3 0 2 |
| 21 | 7 | 8 7 8 | 8 6 7 |
| 22 | 8 | 0 2 0 | 0 0 0 |
| 23 | 8 | 8 6 5 | 8 7 6 |
| 24 | 8 | 3 5 5 | 5 7 4 |
| 25 | 9 | 8 4 5 | 8 8 6 |
| 26 | 9 | 8 8 8 | 8 8 8 |

**Long Season**

| level | crew | before T to W | after |
|---|---|---|---|
| 1 | 1 | 8 8 8 | 8 8 8 |
| 2 | 1 | 6 6 7 | 7 6 7 |
| 3 | 2 | 8 8 8 | 8 8 8 |
| 4 | 2 | 2 3 3 | 1 1 2 |
| 5 | 3 | 8 6 4 | 8 5 5 |
| 6 | 3 | 7 6 5 | 5 7 5 |
| 7 | 4 | 8 7 8 | 8 7 8 |
| 8 | 4 | 6 7 7 | 6 8 7 |
| 9 | 5 | 8 8 8 | 8 8 8 |
| 10 | 5 | 4 6 7 | 1 7 6 |
| 11 | 5 | 6 7 7 | 5 8 6 |
| 12 | 6 | 6 6 7 | 6 6 7 |
| 13 | 6 | 5 7 4 | 4 4 6 |
| 14 | 7 | 7 7 8 | 8 8 8 |
| 15 | 7 | 8 8 8 | 8 8 8 |
| 16 | 7 | 7 5 7 | 4 7 5 |
| 17 | 8 | 0 0 0 | 0 0 0 |
| 18 | 8 | 0 0 0 | 0 0 0 |
| 19 | 8 | 8 8 8 | 8 8 8 |

**Tower** (floor, crew)

| floor | crew | before T to W | after |
|---|---|---|---|
| 1 | 1 | 8 7 7 | 8 7 5 |
| 5 | 2 | 3 0 8 | 2 0 8 |
| 10 | 3 | 0 0 0 | 0 0 0 |
| 15 | 4 | 0 0 0 | 0 8 0 |
| 20 | 6 | 0 0 0 | 0 0 0 |

**Imaginations** (level 1, crew 1): `6 7 6` before, `7 6 7` after.

With both settings off, the four fixes are not asked and no line is
expected to move: none of the 52 lines did (Westlands 25, Long Season 19,
Imaginations 3, Tower 5, identical before and after). With New Specials on and this setting off,
the kits' busy guards (behind New Specials, not this setting) can move a
line: 14 lines moved, thirteen Westlands levels (4, 9, 10, 11, 13, 15, 16, 17, 20,
21, 23, 24, 25) and the Tower's floor 15, every one from those guards and none
from WARBAND's price; none of them is asserted, since the asserting run plays
both settings on. The asserting runs (seed 42 against each level's floor
and ceiling; every seed on the Tower): three lines out of band, Westlands 5 on
seed 42 (5 standing, floor 6), the Tower's floor 1 on seed 2025 (5, floor 6)
and its floor 15 on seed 1337 (8, ceiling 7); every other line in band. Lines that moved by two
or more survivors on seed 42: Westlands 5 (8 to 5), 8 (0 to 2), 17 (3 to 8),
20 (0 to 3) and 24 (3 to 5), Long Season 10 (4 to 1) and 16 (7 to 4), none on
the Tower. Taking the fixes out one at a time shows the three out-of-band lines
moved by V, W1 and W2 together (each moves them; V alone or W2 alone taken
out puts all three back in band), and 100 sim seeds per line read the same
mean before and after (Westlands 5: 6.21 and 6.16, where the unchanged game
misses the floor of 6 on 28 of 100 seeds; Tower floor 1: 7.50 on both; floor
15 on the seed-1337 layout: 4.86 and 4.28), so the moves are the stream, not
the levels' heat; the lines that stayed in band were not bisected one by one. Levels eased, each with a dated
note in its builder: none, because no level got hotter (the crew worse on
two seeds or more), so the protocol allowed no ease. No floor or ceiling was lowered and no
seed was changed; a Westlands level that falls under its floor goes to the
maintainer with the three seeds and the fix that moved it (see
[Rulings](#rulings-2026-10-29)). **Update (2026-10-31):** the maintainer accepted the three moves as the
stream, this once: Westlands 5's floor went 6 to 5, the Tower's floor 1 floor
6 to 5 and its floor 15 ceiling 7 to none, each with a dated note in its test
(`tests/unit/test_westlands_calibration.cpp`,
`tests/unit/test_tower_calibration.cpp`), and the asserting runs pass.

**After fixes X to AD (2026-11-01).** The four suites were measured again on
the shipped default with fixes X to AD in, on the same three seeds, twice:
with the fixes as first built, and again after fix Y's amendment (ruling 4).
"Before" is the "after" column of the tables above; "now" is the game with
fix Y amended.

What the fixes change in a battle. X takes the far and stale shoves out,
and with them a draw each and a stolen order; Y stops the crew and the
enemy eating a potion or a meal one pixel beside their path and swinging
at a body they graze that they are not fighting; a melee-only bot still
bumps its own foe (the first form of Y, the middle column of the tables
below, took a third of the orc, ghost and slime swings and a fifth of
their damage, and the crews came out a little ahead of the classic: Westlands 323 vs 317
survivors over 75 battles, Long Season 348 vs 328); Z leaves an ally's
orders alone when a CHARGE meets it; AA spends fewer druid, barbarian,
mage and archer rolls on casts that refuse; AB makes a struck bot short of
magic hold instead of rolling; AD puts a charmed troop on its charmer's
side. AC changes nothing for a bot (its gate asks the pad first). Each
moves the random stream, so on one seed a line can move by several
survivors either way. With New Specials on, #411 also reaches the
campaigns: a tent's or bones' troop that strikes an elemental is the foe
its IMMOLATE burns double.

**Westlands** (survivors at tick 600, seeds 42 1337 2025, the shipped default)

| level | crew | before X to AD | after X to AD as first built | now (fix Y amended) |
|---|---|---|---|---|
| 1 | 1 | 8 8 8 | 8 8 8 | 8 8 8 |
| 2 | 2 | 8 8 8 | 8 8 8 | 8 8 8 |
| 3 | 2 | 8 8 6 | 6 6 7 | 6 6 7 |
| 4 | 3 | 7 7 8 | 8 7 8 | 8 7 8 |
| 5 | 3 | 5 7 5 | 7 8 6 | 7 8 2 |
| 6 | 4 | 7 5 4 | 3 8 7 | 3 8 7 |
| 7 | 4 | 0 0 0 | 0 0 0 | 0 0 0 |
| 8 | 5 | 2 6 4 | 2 5 4 | 2 5 2 |
| 9 | 6 | 2 1 0 | 7 2 2 | 7 2 2 |
| 10 | 5 | 0 0 0 | 0 0 0 | 0 0 0 |
| 11 | 6 | 4 1 6 | 3 6 3 | 1 4 5 |
| 12 | 6 | 0 0 0 | 0 0 0 | 0 0 0 |
| 13 | 6 | 1 1 2 | 0 0 0 | 0 0 0 |
| 14 | 7 | 0 0 0 | 0 0 0 | 0 0 0 |
| 15 | 7 | 3 4 1 | 4 4 3 | 3 5 3 |
| 16 | 8 | 2 3 3 | 6 1 4 | 4 2 4 |
| 17 | 8 | 8 7 8 | 6 4 6 | 4 5 7 |
| 19 | 6 | 5 4 6 | 6 6 4 | 6 6 5 |
| 20 | 7 | 3 0 2 | 0 2 3 | 1 4 2 |
| 21 | 7 | 8 6 7 | 8 8 8 | 7 7 8 |
| 22 | 8 | 0 0 0 | 2 0 1 | 1 0 1 |
| 23 | 8 | 8 7 6 | 7 4 7 | 5 7 6 |
| 24 | 8 | 5 7 4 | 5 6 2 | 5 2 2 |
| 25 | 9 | 8 8 6 | 7 6 6 | 6 7 5 |
| 26 | 9 | 8 8 8 | 8 8 8 | 8 8 8 |
| sum | | 318 | 323 | 309 |

**Long Season**

| level | crew | before X to AD | after X to AD as first built | now (fix Y amended) |
|---|---|---|---|---|
| 1 | 1 | 8 8 8 | 8 8 8 | 8 8 8 |
| 2 | 1 | 7 6 7 | 6 6 8 | 6 6 8 |
| 3 | 2 | 8 8 8 | 8 8 7 | 8 8 8 |
| 4 | 2 | 1 1 2 | 3 0 3 | 1 5 3 |
| 5 | 3 | 8 5 5 | 8 6 7 | 8 6 7 |
| 6 | 3 | 5 7 5 | 6 4 7 | 6 4 7 |
| 7 | 4 | 8 7 8 | 8 8 8 | 8 8 8 |
| 8 | 4 | 6 8 7 | 8 7 6 | 8 7 6 |
| 9 | 5 | 8 8 8 | 8 8 8 | 8 8 8 |
| 10 | 5 | 1 7 6 | 5 6 7 | 5 7 7 |
| 11 | 5 | 5 8 6 | 8 7 7 | 8 7 6 |
| 12 | 6 | 6 6 7 | 6 7 5 | 5 7 5 |
| 13 | 6 | 4 4 6 | 7 6 5 | 5 6 5 |
| 14 | 7 | 8 8 8 | 8 8 8 | 8 8 8 |
| 15 | 7 | 8 8 8 | 8 8 8 | 8 8 8 |
| 16 | 7 | 4 7 5 | 8 4 8 | 6 7 8 |
| 17 | 8 | 0 0 0 | 0 0 0 | 0 0 0 |
| 18 | 8 | 0 0 0 | 0 0 0 | 0 0 0 |
| 19 | 8 | 8 8 8 | 8 8 8 | 8 8 8 |
| sum | | 329 | 348 | 350 |

**Tower** (floor, crew)

| floor | crew | before X to AD | after X to AD as first built | now (fix Y amended) |
|---|---|---|---|---|
| 1 | 1 | 8 7 5 | 7 8 7 | 7 8 7 |
| 5 | 2 | 2 0 8 | 0 5 7 | 0 5 7 |
| 10 | 3 | 0 0 0 | 0 0 0 | 0 0 0 |
| 15 | 4 | 0 8 0 | 0 3 0 | 0 3 0 |
| 20 | 6 | 0 0 0 | 0 0 0 | 0 0 0 |
| sum | | 38 | 37 | 37 |

**Imaginations** (level 1, crew 1): `7 6 7` before, `7 5 8` after fixes X to AD
as first built, `7 5 8` now. Levels 2 and 3 read `8 8 8` and `8 8 7` before
and `8 8 8` on both after; the three levels sum 67, 68 and 68.

The asserting runs (seed 42 against each level's floor and ceiling; every
seed on the Tower): Long Season, Imaginations and the Tower are in band in
both later columns, so no level was eased. Westlands has two lines out of
band on seed 42, in both later columns: level 9 reads 7 against its band
of 0 to 3, and level 11 reads 1 now (3 as first built) against its band
of 4 to 8. Taking the fixes out one at a time on the first build: level
9's 7 needs X, Y and AB together (without X it reads 1, without Y 1,
without AB 3; adding them in order, it reads 2, 1, 3, 3 and, with AB, 7);
level 11's 3 came with AA on top of X, Y, Z and AB (6 before AA; without
X it reads 6, without Y 5, without AA 6). The amendment leaves level 9
exactly as it was on every one of a hundred seeds (the level has almost no
melee-only bot near the crew, so the kept bump never fires there; what
moved it is the rest of Y, which the amendment keeps), and it moved level
11's seed 42 from 3 to 1, the stream. A hundred seeds per line (42, 1337,
2025 and 1000 to 1096; sd over the hundred, SE = sd / 10):

| line | build | mean | sd | SE | out of band |
|---|---|---|---|---|---|
| Westlands 9 (band 0 to 3) | before X to AD | 1.29 | 1.46 | 0.15 | 5 of 100 above 3 |
| Westlands 9 | after X to AD as first built | 0.83 | 1.39 | 0.14 | 5 of 100 above 3 |
| Westlands 9 | now | 0.83 | 1.39 | 0.14 | 5 of 100 above 3 |
| Westlands 11 (band 4 to 8) | before X to AD | 4.77 | 1.86 | 0.19 | 24 of 100 below 4 |
| Westlands 11 | after X to AD as first built | 4.51 | 1.73 | 0.17 | 30 of 100 below 4 |
| Westlands 11 | now | 4.66 | 1.76 | 0.18 | 25 of 100 below 4 |

Level 9's 7 is the crew doing better than its ceiling on one seed, and
on average it does not: seed 42's 7 is a two-in-a-hundred draw, the game
before the fixes breaks the ceiling on 5 seeds of the hundred too, and the
mean fell from 1.29 to 0.83 (about two combined standard errors) on a war
level whose floor is 0, the same on the first build and now. Level 11's
mean is within one standard error of both earlier columns, and the game
before the fixes already misses the floor on 24 seeds of the hundred, its
own seed 42 reading exactly the floor: the stream, not a level that got
hotter. The survivors over the 75 Westlands battles read 318, 323 and 309;
paired battle by battle, now is 9 under the game before the fixes (SE 15)
and 14 under the first build (SE 9). What the amendment did to the sums:
Westlands 323 to 309, Long Season 348 to 350, Imaginations 68 to 68, the
Tower 37 to 37. It gives back the classic bump a melee-only bot made, the
part of Y that put the crews a little ahead of the game without Y (317 and
328); Westlands lands 9 under the game before the fixes, inside one
standard error, and the two lines measured over a hundred seeds read no
hotter than they did on the first build. No level was eased, and no floor,
ceiling or seed changed in any calibration test in this work (the four
calibration test files are untouched since PR #404). The two Westlands
lines go to the maintainer with the three seeds and the fixes that moved
them (ruling 16 of the [rulings](#rulings-2026-11-01)).

**The other change outside the setting that moves a calibration line.**
Besides the four eased Long Season levels, and the kits' bot rules
above, one more line moved with the setting off before the bot gates.
With New Specials on and this setting off, Westlands level 8 reads
`1 1 2` (seed 42: 1 survivor, the crew dealt 1537 damage) where it read
`2 1 2` (2293 dealt) before this work. The cause is fix O, METEOR RAIN
holding its fire over an ally: the same run with only that check taken
out of the pack read `2 1 2` again. Level 8's floor is 0, so no test
moves.

## The scene tests

Fixes A to M, S and U each have a scene that plays them in the real game,
in the `GameplayFixes` suite of `tests/integration/test_special_scenes.cpp` (binary
`og_test_special_scenes`). Each scene is played twice from the same seed and
the same keys, the setting off and then on, with New Specials off in both so
every Shift is the classic one. The test asserts what the player sees on
each side and what must never show (the classic bug with the setting off,
the fix with it on), and hashes the seat's viewport after every frame to
prove the two runs identical until the frame the fix parts them.
Twenty-three scenes: A has three (resisted, nobody to disarm, the stagger),
E two, H two, K four, M two, and the rest one each (S: a soldier hero
throws once, then spins at a foe beside him; with the setting off the spin
says `SPECIAL BUSY`. U, `u_charge_front_foe`: a soldier hero presses
CHARGE into a foe standing against his front; both runs say `PATH
BLOCKED`, and with the setting off the front test swings, the knife lands
and its 2 MP are spent, while with it on the foe and the pool are
untouched). They run on every lane like any other test, one gtest each.
The other fixes are bot rules or happen where no seat watches; their unit
tests and parity rows prove them.

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

## Rulings (2026-10-29)

Fixes T to W, and the other changes of the same work (#365, #375, #389,
#400), asked nine questions. The maintainer answered on 2026-10-31: the
defaults below stand, and for the calibration lines (9) he accepted the
three moves as the stream, this once. Each line below is the default
taken, and what the game does today.

1. **#371's shape.** The busy guard in every gate and the full
   fall-through: a refused roll, the gate's or the cast's, takes the
   ordinary act as if the coin had not come up, the shape first chosen
   for #371. It moves most of the 48 recordings with the setting on (each
   recaptured from the old game) and the balance tables (measured again;
   a real dip is eased in its level). The cheaper alternative is the guard
   plus a fall-through only after the cast refused (a gate's no keeps
   ending the act), which moves few recordings. Default: as built.
2. **#390's reach.** The stains walked are the ones the finder answers
   (about 28 px of centre), so the 60 and 30 px ranges reach no further
   than before. The alternative, every stain within 60 or 30 px, is a
   reach and balance change. Default: as built.
3. **#390's RESURRECT.** The first usable stain decides friend or foe, as
   before: a nearer foe's stain rises as a ghost before a friendly one
   behind it. Preferring a friendly stain would change what a player's
   RESURRECT does. Default: as built.
4. **#375's numbers.** WARBAND costs 120 (an untrained level-10 captain
   has 133 magic, so it is castable where it opens, for nearly the whole
   pool), and its grunts stand at a third of the captain's level. 200, the
   number in the issue, makes WARBAND uncastable at level 10 without
   trained intelligence (the first untrained level that can pay is 16).
   Default: 120.
5. **#389's loyalty.** A captured tent's or bones' old troops are
   released: they keep their colour, fight for it, can take the post
   back, and their kills pay their colour 10 each. The alternative hands
   them to the captor, which contradicts
   [mp-game-modes.md](mp-game-modes.md) and needs a charmed troop handled.
   Default: release.
6. **#390's nudge and the caster.** The body may stand up to 16 px from
   the corpse, and the caster is in the way like any body; the nudge is
   what lets a cleric standing on the corpse raise it (the skeleton rises
   12 px below). An 8 px nudge fixes the wall and the corridor but not
   that. Default: 16 px.
7. **#365's tint.** FREEZE TIME's blue screen is the 1995 loop as written
   (red and green halved from palette entry 32), not behind any setting,
   and the normal colours come back when a mission ends. A weaker tint or
   an Effects toggle are the alternatives. Default: as written.
8. **#369's touching foe.** A foe already pressed against the soldier's
   front is a blow, not a charge: the gate says no, the cast (with the
   setting on) refuses `PATH BLOCKED` for free, and the melee is the bot's
   ordinary attack. The alternative lets the rush strike it, a reach
   change outside this work. Default: as built.
9. **Calibration dips.** A Long Season or Imaginations level that falls
   under its floor is eased in its builder, as before; a Westlands level
   is reported with the three seeds and the fix that moved it, for the
   maintainer's word, as level 24 was. Default: so. **Update (2026-10-31):**
   the three lines fixes T to W put out of band (Westlands 5 on seed 42, the
   Tower's floors 1 and 15) were accepted as the stream, this once, and
   re-pinned with dated notes; see "After fixes T, U, V and W".

One precedent, recorded with them: fixes H and I made a classic free
swing paid, because the swing was the cast's own (the volley and the bolt
fire at the foe they meet); fix U removes a free swing instead, because
that swing was never the cast's: CHARGE and DISARM only asked whether
their front was blocked, and a question must not attack. Fix AC keeps H's
and I's paid blow and refuses before paying only where no blow could land
(a wall face, the map edge).

## Rulings (2026-11-01)

Fixes X to AD, and the other changes of the same work (#408, #411, #413 to
#421, #423), asked seventeen questions, #422 among them, and an eighteenth
when fix Y was amended (carried in ruling 4). Each line below is the default
taken, and what the game does today.

1. **#410: a blocked volley.** A rock or bolt whose pad holds terrain (a
   wall face, the map edge, or a tree crown for the druid's bolt) is
   refused `COULD NOT FIRE` before the refund and the swing, so it costs
   nothing; a rock that strikes a foe at point-blank range still costs its
   one swing, as fix H ruled (and fix I for the druid). That closes the
   issue's case, 1 MP for a swing at nothing, moves no existing recording
   and keeps clubbing a point-blank foe with ROCKS. The alternative makes
   every refused volley free: it reverses H's ruling, moves
   `elf_rocks_blocked_fixes_scen99` and `druid_tree_blocked_fixes_scen99`,
   and rewrites a test, two scenes and the precedent above. Default: as
   built.
2. **#403 and #406: the shape.** A walker shoves, and a rush strikes, only
   a body its step's box meets from where it stands; the collision field
   and the snapshots stay classic. The alternative is the issue's draft,
   which empties the collision field at the top of every step and so also
   changes what a bot's wall-stopped CHARGE strikes. Default: as built.
3. **#406: allies in a CHARGE.** With the setting on a CHARGE never erases
   an ally's orders or walks it off (the walk's ordinary shove, two times
   in three and never a seat, still nudges it). The alternative keeps the
   classic violent shove of allies and drops only the damage roll.
   Default: as built.
4. **#405: how far the quiet probes reach.** The three right-hand probes,
   the right-hand walk and a bot's walk to a point (this game only, the
   same setting). A seat's slide along a wall keeps its two loud probes:
   that is a player's own step, not in the issue. Default: as built.
   Amended 2026-11-01: asked wholly quietly, the probes took away a third
   of the orc, ghost and slime swings, an eighth of their blows and a
   fifth of their damage over the four campaigns, and an orc that stops
   exactly against a standing foe never swung again. The game now keeps
   the bump for a melee-only bot's own foe and nothing else (a potion
   stands, a door stays shut, an ally and a passing hostile are untouched,
   a soldier throws no knife at a body it grazed); a bot's walk to a point
   stays wholly quiet. Two alternatives: (1) the bump for every bot at
   blows with its own foe, which gives a ranged bot back its classic knife
   along its heading at a foe touching its side (2 MP and a draw, mostly
   at air): not taken, ranged bots fight through their aimed shot already;
   (2) the bump only from the two forward probes (front, right front),
   where the swing can land: not taken, a new unmeasured rule; the full
   classic cadence is what the campaigns were tuned at. Default: melee-only
   bots, their own foe, all four probes.
5. **#409: the shape.** With the setting on a struck bot holds a cast it
   cannot pay; the gate's yes is for a slot it can pay and the follow-up
   roll is not drawn. Ported, with a new pair of recordings where the
   capture shows it. The alternative skips only the refused call, which
   changes no byte and needs no port but leaves the gate saying yes to a
   cast the bot cannot pay. Default: as built.
6. **#411: the credit.** The kill record (TDM, Onslaught, the mode scripts)
   credits the topmost living on the chain, not behind the setting (this
   game only); classic team score and experience are untouched. The
   alternative also pays a hero experience for its troop's kill, a new
   rule that would be gated and ported. Default: as built.
7. **#412: the shape and the setting.** A charmed walker heads its own
   chain while its charm clock runs, behind the setting and ported; its
   owner, its 811-tick clock and the cascade when its tent falls are
   unchanged, so a charmed troop is still its tent's. The test reads the
   charm's clock, not the walker's recorded real team, which level hooks,
   map generators, the parity runners and the possession rider also write
   with no charm behind it. The alternatives: no setting, treating the
   colour and the loyalty disagreeing as garbage behaviour (as MIND
   CONTROL's duration was), or cutting the owner as Onslaught's release
   does, which leaves the troop ownerless for good and moves the
   generator's rate draw. Default: as built.
8. **#413: where the cheat works.** The fixed F1 cheat works in local play
   only; in a networked game it does nothing. A replay recorded while
   cheating plays back differently (a cheat is not recorded input, and
   local play always records, so refusing the cheat while recording would
   refuse it always). The alternative lets a host cheat for everyone.
   Default: as built.
9. **#415: the typed name.** On the host's GO the half-typed name is
   dropped, in RENAME and in HIRE ME; the hire stands with its suggested
   name, as CANCEL leaves it (the match was set up at the confirmation, so
   a rename could not reach it anyway). The alternative keeps the typed
   name in HIRE ME. Default: as built.
10. **The level-play caps.** The rule PR #404 used, the measured count
    plus a third and at least 3: `SPECIAL BUSY` 3 (measured 0), `NOT ENOUGH MP`
    3 (measured 0; it was capped at 100, "stay rare"), one pooled cap for the
    druid, archer, barbarian and mage `COULD NOT FIRE` 3 (measured 0), the
    elves' cap 15 (measured 11 after the amendment; it was 7), `PATH BLOCKED`
    3 (measured 0), and far shoves 0. A floor the new
    gates breach (DRUID SUMMON FAERIE at least 2, BARBARIAN HURL BOULDER at
    least 1) is a balance question, never a lowered floor. Default: so.
11. **#419: the shape.** The pad question takes the weapon family the cast
    fires (the archer passes its fire arrow); the families' weapon hooks
    are not run as a dry run, because the soldier's and the archmage's
    write the caster's magic and blades and a dry run would not be pure.
    A test proves the hooks do not move the pad. The soldier's veto when
    no blade is left stays a gap no gate asks about. Default: as built.
12. **#422, not fixed here.** The old game's `walker::special()` has
    answered 0 on every path since the 2002 import, so its classic archmage
    hit response never returns after a cast, while this game's does. The
    recommendation is that the old game is the classic side: in a
    follow-up pull request, this game's setting-off archmage hit response
    ignores the cast's answer (`living-17-archmage.lua:131`, `:142`,
    `:153`), keeping every draw and cast the old game makes, and
    `archmage_bot_struck` then gets a setting-off twin captured from the
    old game. The alternative changes the old game to answer whether it
    cast. Default: the follow-up, not this work.
13. **#423: the two mage rows.** `mage_bot_freeze_no_foe_fixes_scen99` and
    `mage_bot_freeze_alone_fixes_scen99` take spawns of their own at (160,
    160), each recaptured from the old game, under the PR #404 ruling (see
    [Parity](#parity)); their classic twins are untouched. The alternative
    leaves them in the lake and lists them with a reason. Default: as
    built.
14. **A CHANGELOG! entry.** None: these are bug fixes, and a player reads
    them as the game playing as it should. The alternative is one line for
    the three a player sees on screen (the cheat, the water and lava, the
    kick popup). Default: none.
15. **#407: how many gates.** The four the issue names. Not added: a gate
    for the archer's BARRAGE (three arrows into a wall for 60 magic is a
    question of payoff, not a refusal) and a probe of the faerie's room for
    SUMMON FAERIE's `NO ROOM TO SUMMON` (one refusal in the census; a
    follow-up issue). Default: so.
16. **Calibration dips.** A Long Season or Imaginations level that falls
    under its floor is eased in its builder; a Westlands level is reported
    with the bisect and the three seeds for the maintainer's word; nothing
    is re-pinned lower. Default: so.
17. **The new pairs of recordings.** The far shove across a teleporter
    cannot be staged in an arena, so the unit tests and the level-play
    count stand for it. Each proposed pair (the near stale shove, the
    potion beside a right-hand walk, the CHARGE into an ally, the struck
    elemental short of magic, the ROCKS at the map edge, the charmed tent
    troop, and the two terrain pads of fix AA) is kept only if its two
    captures differ and a fact reads the difference; the three body pads
    of fix AA follow the elf's precedent. Default: so; kept: the near
    stale shove, the potion, the CHARGE into an ally, the struck elemental,
    the ROCKS at the map edge, the charmed tent troop and four body pads
    (GROW TREE took a body pad in place of its terrain pad); dropped: the
    archer's terrain pad (no fact pair in any scen1 arena).

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
