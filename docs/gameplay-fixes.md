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

TELEPORT, REVEAL, CLOAK and ENERGY WAVE are unchanged as casts (their bot
rules are in [Bots](#bots)): each does something whenever it is cast
(ENERGY WAVE crosses walls along the facing). CHARGE still refuses `PATH
BLOCKED` when something stands against the soldier's front and otherwise
moves the fighter; fix U changes only how it asks.

The four newest fixes, in play:

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

`NO ROOM TO SUMMON` (SUMMON ELEMENTAL) and `COULD NOT FIRE` (the elf's
volleys, GROW TREE, SUMMON FAERIE) keep their text; with the setting on they
cost nothing extra (SUMMON ELEMENTAL) or exactly one swing (a blocked
volley or bolt).

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
  would be placed on holds no wall, door, map edge or foe's body (a body of
  the elf's own team passes, as the rock passes it). The classic elf threw
  at nobody.
- **Archer.** FIRE ARROWS, the ring, wants at least two living foes inside
  the arrows' flight, or one the archer does not face. BARRAGE wants the
  archer's foe, a living one, faced and inside the flight. EXPLODING BOLT
  wants that foe faced and inside the flight, with no ally of the archer,
  and not the archer, inside the blast around it.
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
  that far along the heading, through walls). HEARTBURST wants a foe
  inside its own reach, 80 px plus 2 per level.
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
  threshold as before; otherwise it answers with one HEARTBURST or CHAIN
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
  bolt). SUMMON FAERIE wants a living foe within 130 px and no faerie of
  the druid's alive. REVEAL is never cast by a bot: it changes a radar
  only a player sees. PROTECTION wants an ally within 60 px with a living
  foe at blows with it and no fresh circle (none, or one under 50 hp).
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
  barbarian inside it.
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
  payoff rule on every slot neither habit casts at nothing any more.

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

`tests/integration/test_level_play.cpp` (binary `og_test_level_play`) checks
the rules in a real level: it plays The Tryxian Chronicles' first level, its
104 bots on four warring teams plus a crew of 12 heroes at level 14, for 2400
ticks on three seeds, and counts every cast, every refusal and every roll a
gate holds. On the shipped default the bots cast about <measured> times and
<measured> different specials, among them the eight Shift arms the level
reaches (MYSTIC MACE, CHAIN LIGHTNING, TELEPORT MARKER, MINE, CHARM
OPPONENT, TURN UNDEAD, WAIL, HASTE SELF) and the crew's slot-5 HEARTBURST
and FREEZE TIME, none at nobody (each special whose rule names a foe has
one within its reach, worked from the gate's own arithmetic), REVEAL never,
and no gate says yes to a cast that then refuses for the fact the gate
reads. With the setting off the same level shows <measured> casts at
nobody, REVEAL <measured> times and <measured> such refusals, so the test
tells the rules from their absence.

The same run counts the refusals fixes V and W are about. Pooled over the
three seeds on the shipped default, before fixes T to W the soldier bots
met `PATH BLOCKED` 139 times to 61 charges (a wall or the map edge 74
times, a foe already touching 19, an ally 12, a door 2), the elves `COULD
NOT FIRE` 80 times to 44 volleys (a wall on the rock's pad 57 times, a foe
on it 15), and casts were refused `SPECIAL BUSY` 154 times. With the fixes
in they read `PATH BLOCKED` <measured> to <measured> charges, `COULD NOT
FIRE` <measured> to <measured> volleys and `SPECIAL BUSY` <measured>, and
the test caps each at its measured count plus a third (at least 3). The
gates hold <measured> rolls, each of which now takes the bot's ordinary
act. The `SPECIAL BUSY` refusals left come from casts no gate is asked
about, among them the mage bot's own hit response: below its flee
threshold it teleports without asking its gate, and a mage struck again
while that teleport plays is refused.

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
  mirror `<sha>` (the five new arenas), then `<sha>`, the probes the fixes
  ask with and no behaviour (the passability probe's quiet flag, a
  detached body probe that draws nothing, the stain walk and the rock's
  pad; and, not behind the setting, a guard the current game already had:
  an ownerless probe over an arrow slit no longer dereferences a null
  owner; no golden moved), `<sha>` (T), `<sha>` (U), `<sha>` (V: the
  soldier bot's CHARGE), `<sha>` (V: the elf bot's volley), `<sha>` (W:
  the busy casts' gates read busy) and `<sha>` (W: a refused roll is not
  the act), with the table mirror `<sha>` after them. `<sha>` is the
  commit the goldens are captured from: <measured> of <measured> goldens
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
| B, C, L | `mage_bot_freeze_no_foe_fixes_scen99`, `mage_bot_freeze_alone_fixes_scen99`, `mage_bot_freeze_engaged_fixes_scen99`, `mage_bot_heartburst_fixes_scen99`, `archmage_bot_summon_no_foe_fixes_scen99`, `cleric_bot_raise_or_turn_fixes_scen99`, `thief_bot_charm_or_taunt_fixes_scen99`, `archmage_hit_stale_shift_fixes_scen99` |
| M | `thief_bot_charm_hero_fixes_scen99`, `archmage_bot_mind_control_hero_fixes_scen99` |
| Q | `archer_bot_bolt_at_blows_fixes_scen99`, `archer_bot_ring_one_foe_fixes_scen99`, `archmage_bot_image_one_foe_fixes_scen99`, `barbarian_bot_boulder_far_fixes_scen99`, `barbarian_bot_explode_lone_fixes_scen99`, `cleric_bot_mace_alone_fixes_scen99`, `cleric_bot_resurrect_foe_stain_fixes_scen99`, `druid_bot_faerie_alone_fixes_scen99`, `druid_bot_protection_idle_fixes_scen99`, `druid_bot_tree_behind_fixes_scen99`, `elemental_bot_starburst_one_foe_fixes_scen99`, `elf_bot_rocks_far_fixes_scen99`, `mage_bot_marker_home_fixes_scen99`, `mage_bot_warp_two_at_blows_fixes_scen99`, `soldier_bot_boomerang_far_fixes_scen99`, `soldier_bot_charge_unaimed_fixes_scen99`, `soldier_bot_disarm_front_fixes_scen99`, `soldier_bot_whirlwind_one_foe_fixes_scen99`, `thief_bot_bomb_ally_fixes_scen99`, `thief_bot_bomb_far_fixes_scen99`, `thief_bot_cloud_alone_fixes_scen99` (21 new arenas, each with its setting-off twin `<arena>_scen99`) |
| R | `archmage_bot_struck_fixes_scen99` (no setting-off twin: see below) |
| T | `cleric_raise_blocked_corpse_fixes_scen99` (a new arena, with its setting-off twin) |
| U | `soldier_charge_front_foe_fixes_scen99` (a new arena, with its setting-off twin) |
| V | `soldier_bot_charge_door_fixes_scen99`, `elf_bot_rocks_blocked_fixes_scen99` (two new arenas, each with its setting-off twin) |
| W | `soldier_bot_refused_roll_acts_fixes_scen99` (a new arena, with its setting-off twin) |

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
recordings read follows within their budgets. W moved <measured> of the
48 rows with the setting on: a bot in each meets a refused roll within
the row's budget and now takes its ordinary act;
`tests/parity/golden/DRIFT_LEDGER.md` lists them. Each exact fact a
recapture broke was worked out again from the new capture and its pin
proven again.

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
- <measured>, whose arena no seed or tuning could build: the unit tests
  under its fix pin its rule instead.
- `archmage_bot_struck_scen99`: the struck archmage's arena has no
  setting-off twin, because the two games' classic hit responses differ.
  The old game's `walker::special` always answers 0, so after a summon its
  hit response never returns and its "then leave! :)" second cast never
  happens, while the current game's classic hit response uses the cast's
  real answer. No golden reaches that difference; it is recorded in the
  ledger.

## Not gated

Three changes in this work apply whatever the setting:

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
| 1 | 1 | 8 8 8 | <measured> |
| 2 | 2 | 8 8 8 | <measured> |
| 3 | 2 | 7 8 7 | <measured> |
| 4 | 3 | 8 7 8 | <measured> |
| 5 | 3 | 8 6 4 | <measured> |
| 6 | 4 | 8 7 7 | <measured> |
| 7 | 4 | 1 1 0 | <measured> |
| 8 | 5 | 0 3 6 | <measured> |
| 9 | 6 | 0 2 1 | <measured> |
| 10 | 5 | 1 0 0 | <measured> |
| 11 | 6 | 5 5 4 | <measured> |
| 12 | 6 | 0 0 0 | <measured> |
| 13 | 6 | 1 0 0 | <measured> |
| 14 | 7 | 0 0 0 | <measured> |
| 15 | 7 | 2 1 1 | <measured> |
| 16 | 8 | 3 3 2 | <measured> |
| 17 | 8 | 3 6 6 | <measured> |
| 19 | 6 | 5 7 5 | <measured> |
| 20 | 7 | 0 0 0 | <measured> |
| 21 | 7 | 8 7 8 | <measured> |
| 22 | 8 | 0 2 0 | <measured> |
| 23 | 8 | 8 6 5 | <measured> |
| 24 | 8 | 3 5 5 | <measured> |
| 25 | 9 | 8 4 5 | <measured> |
| 26 | 9 | 8 8 8 | <measured> |

**Long Season**

| level | crew | before T to W | after |
|---|---|---|---|
| 1 | 1 | 8 8 8 | <measured> |
| 2 | 1 | 6 6 7 | <measured> |
| 3 | 2 | 8 8 8 | <measured> |
| 4 | 2 | 2 3 3 | <measured> |
| 5 | 3 | 8 6 4 | <measured> |
| 6 | 3 | 7 6 5 | <measured> |
| 7 | 4 | 8 7 8 | <measured> |
| 8 | 4 | 6 7 7 | <measured> |
| 9 | 5 | 8 8 8 | <measured> |
| 10 | 5 | 4 6 7 | <measured> |
| 11 | 5 | 6 7 7 | <measured> |
| 12 | 6 | 6 6 7 | <measured> |
| 13 | 6 | 5 7 4 | <measured> |
| 14 | 7 | 7 7 8 | <measured> |
| 15 | 7 | 8 8 8 | <measured> |
| 16 | 7 | 7 5 7 | <measured> |
| 17 | 8 | 0 0 0 | <measured> |
| 18 | 8 | 0 0 0 | <measured> |
| 19 | 8 | 8 8 8 | <measured> |

**Tower** (floor, crew)

| floor | crew | before T to W | after |
|---|---|---|---|
| 1 | 1 | 8 7 7 | <measured> |
| 5 | 2 | 3 0 8 | <measured> |
| 10 | 3 | 0 0 0 | <measured> |
| 15 | 4 | 0 0 0 | <measured> |
| 20 | 6 | 0 0 0 | <measured> |

**Imaginations** (level 1, crew 1): `6 7 6` before, `<measured>` after.

With both settings off, the four fixes are not asked and no line is
expected to move: <measured>. With New Specials on and this setting off,
the kits' busy guards (behind New Specials, not this setting) can move a
line: <measured>. The asserting runs (seed 42 against each level's floor
and ceiling; every seed on the Tower): <measured>. Lines that moved by two
or more survivors on seed 42, and the fix that moved each, found by taking
the fixes out one at a time: <measured>. Levels eased, each with a dated
note in its builder: <measured>. No floor or ceiling was lowered and no
seed was changed; a Westlands level that falls under its floor goes to the
maintainer with the three seeds and the fix that moved it (see
[Rulings](#rulings-2026-10-29)).

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
#400), asked nine questions. The maintainer has not answered them yet;
each line below is the default the design took, and what the game does
today, until he does.

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
   maintainer's word, as level 24 was. Default: so.

One precedent, recorded with them: fixes H and I made a classic free
swing paid, because the swing was the cast's own (the volley and the bolt
fire at the foe they meet); fix U removes a free swing instead, because
that swing was never the cast's: CHARGE and DISARM only asked whether
their front was blocked, and a question must not attack.

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
