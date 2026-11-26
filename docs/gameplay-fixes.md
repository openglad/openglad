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
| M | Charm and seats | An enemy thief or archmage can charm a hero a player is driving. A seat can be handed a walker that is only on its team because it is charmed. | A driven hero resists (the rule POSSESS already has). A seat never claims a charmed walker (see [Charm and seats](#charm-and-seats)). A seat already driving a walker when its charm runs out keeps it, on both settings. |
| Q | Bot payoff gates | Elf, archer, druid, barbarian, soldier, thief, ghost, orc and cleric bots cast specials at nobody, at foes out of reach or with allies in the blast: an elf throws rocks with no foe in sight, a thief drops its bomb at a foe 130 px or more away, a cleric throws its mace with nobody near. | Every special a bot can roll has the rule under which it pays off (see [Bots](#bots)); a Shift arm is its own special for a bot, with its own rule. |
| R | The struck archmage's order | A struck archmage bot with a foe within 200 px casts its 500-magic summon first and then tries slot 2 (fix AE). | It answers with one HEARTBURST or CHAIN LIGHTNING chosen by the slot-2 rule: no summon, no second cast. Below its flee threshold it still teleports. |
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
| AC | Elf volleys, druid GROW TREE and SUMMON FAERIE, archer EXPLODING BOLT, barbarian HURL BOULDER and EXPLODING BOULDER, mage ENERGY WAVE at a wall or the map edge | Classic: the cast draws the throw's waver and refuses; the elf's refund leaves it a point ahead (fix H's off column). With fixes H and I alone the elf's and druid's casts paid the one swing (1 MP for the elf, 4 for the druid) for a rock or bolt that met the wall and struck nothing; the other four pay their shot on both values (1 MP for the archer, 2 for the barbarian, 5 for the mage). | A pad blocked by terrain refuses `COULD NOT FIRE` before anything is paid: nothing drawn, nothing spent. A body on the pad keeps the paid blow. |
| AD | A charmed tent or bones troop, a charmed summon | It wears the charmer's colour but keeps its owner's side: it fights its charmer and is struck by its charmer's team. | It fights for the colour it wears, as everything does with the setting on (fix AZ), and heads its own owner chain while the charm lasts, so its blows' experience and credit are its own; its owner, its charm clock and its tent's cascade are unchanged. |
| AE | The struck archmage's classic order | The reply goes on after a cast that worked, as the old game's does: a summon that stands still draws the slot-2 coin (the summon's busy refuses the cast), a chain is followed only by a HEARTBURST attempt its own busy refuses, and a HEARTBURST is cast once. | A cast that worked ends the reply; after a chain or a HEARTBURST that leaves the price of a TELEPORT, slot 2 is cast again (a second HEARTBURST, "then leave! :)"). With the setting on only a seat-driven archmage takes this order (a struck bot answers by fix R). |
| AF | Who acts during FREEZE TIME | A Living or a generator acts only when it is on team 0 itself, as in the old game: with a player colour other than red the freeze stops the player's own side and lets team 0 fight on (#86), a charmed troop of a living enemy tent or an enemy's summon the player charmed acts because it wears red, and a summon of the player's caster that an enemy charmed is frozen. | It acts when it is friendly to the player's team, which with the setting on means it wears the player's colour itself (fix AZ; #92). The level census reads friendliness on both settings. |
| AG | A forestwalker's step in the trees | Every act in the trees takes the forest penalty off the step the walker carries and adds any haste to the result. A hastened elf whose haste beats its penalty steps farther every act it stays in the trees and keeps that size until it leaves them (a level-2 speed potion with no penalty: a 204 px step after 100 acts, over walls, water and bodies); without a haste the step crawls to 1 px within a few acts. | The penalty comes off the normal step every act: in the trees the step is the normal step less the penalty, never below 1 px, plus any haste, once. A hero elf with 30 dexterity walks 3 px, not 1. |
| AH | Cleric TURN UNDEAD on a warded skeleton or ghost | Only the family decides, as in the old game: a skeleton or ghost carrying `BIT_INVINCIBLE` or an invulnerability potion (one it drank, or a New Specials ghost's PHASE) is rolled for and turned like any other. | A warded undead is never turned, rolled for or counted: turning is damage, and the ward stops damage. With nothing else turnable in reach the cast is refused as `NO UNDEAD IN RANGE` (fix E), and the cleric bot's gate does not count it. |
| AI | FREEZE TIME's form | A mage on the red team or any hero stops time for everyone else, whatever team the player is on: a hero mage of another colour (a second seat's, a charmed one) turns the screen blue. A mage on the player's team that is neither red nor a hero gives its side bonus rounds instead. | Only a mage on the player's team stops time; any other mage, hero or not, gives its own side bonus rounds ("TIME IS FROZEN! (N rounds)"). |
| AJ | The exit of a level the player may leave at any time, to a level already won | With foes standing it asks "Withdraw to X?" first (yes abandons the level and reloads the company's save); a no asks "Exit to X?" next, and yes there wins the level. | It asks "Exit to X?" only. |
| AK | "X picks up key N" | The line shows in the view of the player whose hero picked the key up, or in every view when no player drives that hero (a computer-run teammate). | It shows in every view, so the party learns who carries the key. |
| AL | Archmage MIND CONTROL on more than one foe | The cast costs its price alone, however many foes it takes, as the old game's does (its charge for each further man lowers only a local). | Each foe taken past the first costs 10 magic more, never more than the magic left after the price. |
| AM | A poison cloud wedged against a wall | It stays where it is until its walk runs out (#233), then draws a new one. | A tick that neither moves it nor turns it drops the stuck walk and draws a fresh direction, so it drifts on. |
| AN | The archmage bot's CHAIN LIGHTNING rule, for a hero | No rule: the bot casts its coin's arm (fix B's off column). | The rule measures the chain with the cast's own reach: 200 px plus half its Intelligence plus 2 per level for an archmage with a character record (a hero the computer plays), 200 px plus 7 per level for a monster. Before, it measured every archmage by level, so such a hero was told "the chain has a target" and then refused `NO FOE IN RANGE`, or turned to HEARTBURST or held when the chain would have landed. |
| AO | CHARGE's strike on a dead body | The rush strikes a body that is already dead (in play, a door its own step opened with a key): the strike draws its damage roll and does nothing to the body, and the rush still erases the dead body's orders and forces a four-step walk on it. Nothing a player sees follows: a dead walker never acts, and every revival clears its orders. | The dead body is left as it lies: the damage roll is still drawn, its orders stay and no walk is forced. |
| AP | Whose side a walker takes in the tick its owner dies | Friendliness climbs a walker's owner chain only up to a dead owner, while the score, the experience and the kill credit of its blows climb through it: a summon or a shot of a caster that was charmed and then killed is asked for friendliness as its own colour but credited to the caster's, until the tick ends and the dead owner is dropped. | Friendliness reads each walker's own colour (fix AZ), so an owner's death changes no side. The score gate's hero, the experience and the kill credit of a blow still climb through a dead owner and stop at a charmed walker (fix AD), so a dead caster's summon's and shots' blows are the caster's for the rest of that tick. The mutant mode's damage gate reads the same climb, so there too a charmed summon heads its own chain. |
| AQ | A player's slide along a wall | Each half of the slide is a real step whether the slide takes it or not: on a half a body blocks, the probe eats a treasure it reaches before the body, bumps the body (a swing at a foe), opens a door the player holds the key to and posts "Key N needed!" at a locked one. | Each half is asked quietly first, as the right-hand walk's probes are (fix Y); only a free half takes the step, which acts as before (a treasure the slide moves onto is eaten). A blocked half eats nothing, swings at nothing and opens nothing: a keyed door beside a wall opens when the player walks into it, not when the slide brushes it. |
| AR | The druid bot's SUMMON FAERIE rule | No rule (fix Q's off column). With the setting on before this fix the rule asked the bolt's pad alone: a bolt passes an ally, a faerie does not, so with an ally standing where the faerie appears the rule said yes and the cast refused `NO ROOM TO SUMMON`. | The rule also asks the cast's own question for the faerie where the bolt stands, quietly, with a faerie of the druid's (never the druid's own body, #423): an ally, an enemy door or tree, an enemy weapon in flight or an arrow slit there is a no. |
| AS | Archer BARRAGE (the cast and the bot rule) | The cast fires three arrows and answers yes whatever each does: at a wall face or the map edge no arrow leaves and it still costs the 60; with a body on the arrow's pad it lands three melee blows for the 60. The bot rule asks for the foe faced and in flight, never the pad. | A pad blocked by terrain refuses `COULD NOT FIRE` before anything is paid (as fix AC). A first arrow that cannot leave (a body on the pad takes its blow, or an enemy weapon wins the clash) refuses `COULD NOT FIRE` after that one blow and fires no more: it costs the one swing, never the 60. The bot rule also asks the arrow's pad, an enemy weapon on it included (fix AV's question). |
| AT | A walker asked to face the way it already faces | The turn moves it one notch counter-clockwise anyway and points its heading that way. Four callers ask it: a bot within 100 px of a foe it already faces is turned off it on the tick it re-plans, and the swing it then tries only turns it back, so that swing never starts (measured on Tryxian level 1, six seeds of 2400 ticks: 2,307 such tries, 0 swings); a chaser whose shot already lands, so it faces its foe, turns off it before it attacks and spends its next act turning back; a bot whose shot is refused while it faces its foe (a melee-only bot always) turns off it; a wall-hugging bot still mid-turn can be turned off its new heading. | It stays as it is: its facing, heading and walk frame do not change, and the turn reports done. The bot at its foe swings when it is not busy (measured: 2,291 swings in 2,383 tries; 80 waited for their busy time, 12 turned because their heading pointed elsewhere); the chaser attacks on its next act. Fix BB then makes that swing ask whether it can land. |
| AV | The elf bot's volley rules | No rule (fix Q). With the setting on before this fix the rule asked the rock's pad quietly, and a quiet question passes an enemy weapon in flight, door or grown tree, which `fire()` meets on 4 rolls in 10: the cast then refused `COULD NOT FIRE` after the rule's yes. | The rule cannot draw the clash roll, so it takes it as lost: an enemy weapon in flight, door or grown tree on the rock's pad is a no. It holds about six casts in ten that would have flown. |
| AW | The experience an owned effect's blow pays | A blow by an effect a hero owns (a thief's bomb or mine blast and POISON CLOUD, a magic shield or boomerang, chain lightning's or an exploding shot's blast) pays the hero its attack experience twice: once as a thrown thing, once more as a summon's blow. | It pays once, as a thrown thing does: the experience of one melee blow of the same damage. |
| AX | A foe's raised ghost, elemental and phantom on Easy or Hard | The cleric's RAISE GHOST and the archmage's SUMMON ELEMENTAL and phantom scale the summon for the difficulty before it takes its caster's team, while it is still on team 0, which the difficulty never scales: a foe's ghost, elemental and phantom keep their Normal hit points, magic and damage while their caster takes the percent. | The summon takes its caster's team first, so a foe's takes the percent as its caster does: on Hard twice the hit points, magic and damage, on Easy half (a phantom keeps its one hit point). A summon of the player's side is on team 0 either way and is never scaled. |
| AZ | Whose side a shot, a blast, a spell, a summon or a troop fights for, and whom its blow and its kill pay | A thing fights for the head of its owner chain, so a shot, a blast, a summon or a troop changes side when its maker is charmed, mind-controlled or killed, even in flight. A thief's bomb, its blast, a mine's or an exploding shot's blast, ENERGY WAVE, a boulder and a returning knife wear no colour (red): a dead thrower's blast strikes his own side in full and pays red. A blow's score goes to the colour the hitter wears while the gate that decides a score is due reads the chain's head. | Everything fights for the colour it wears, and nothing follows its maker. A shot, a bomb, a blast, a spell effect, a summon and a tent's troop take their maker's team when made and keep it: whom they hit and spare, whom a blast halves, which team a blow's score and a kill pay (a kill in every mode, even when the maker has died), and which team takes an Onslaught post all read that colour. The one exception is a thing that rides a walker (a ring of protection, a mace, a boomerang, a hook blade, a scare, IMMOLATE's fire, a returning knife on its way home, the F2 cheat's shield): it goes with him and fights for whatever side he is on now. Experience and kills on a hero's record, and whom the victim turns on, stay with the maker, and a walker is never credited with his own death. |
| BB | A bot within 100 px of its foe, on the tick it re-plans | It turns a notch toward its foe, starts its ATTACK order and calls for a swing along its heading, asking nothing. That swing never starts: a bot already facing its foe is stepped a notch off it by the classic turn (the one fix AT removes: with it a walker asked to face the way it faces stays put) and the call only turns it back; a bot not facing its foe is turned a notch toward it and, by the same call, a notch back toward the facing it had, so it never completes its aim there (measured on Tryxian level 1, six seeds of 2400 ticks: 14,547 such calls, 0 swings). With fix AT alone, as the setting on played before this fix, the bot that faced its foe did swing or shoot from there, whatever stood between: melee-only bots started 181 swings, 172 of them from more than 20 px, and 17 landed; the others started 2,054 shots, 895 into a wall and 877 at a foe out of reach, and 234 hit anything; each paid its busy time and its weapon's magic. 11,968 calls turned and turned back. | The turn and the swing agree. A bot not yet facing its foe turns a notch toward it and keeps that aim, so its next acts finish the turn, one notch an act; the turn is its act (a tower faces it at once, fix BE). A bot facing its foe swings or shoots along that facing, and only when the blow can land: a bot that shoots asks its line check (the foe in the weapon's reach, the magic to pay, a clear line to a body); a melee-only bot asks whether a foe's body stands on its weapon's pad. Measured the same way: melee-only bots started 23 swings and 19 landed; the others started 186 shots and 133 hit; no call turned back. |
| BC | A melee-only bot (orc, ghost, small slime) in its ATTACK order | Once its foe is inside its weapon's reach, which is tens of pixels and not only touching, the order only turns the bot to face it: for the order's 30 to 54 acts it takes no step and starts no swing. Its blows are its bumps, a step of its own into a body between orders, and with fix BB one swing on the tick it re-plans. | A bot that already faces its foe on its own floor fights. When a foe's body stands on its weapon's pad it swings, one blow per weapon pause while its magic lasts (9 acts for the orc and the ghost, 14 for the small slime: the rate a player gets by holding the key into a foe). An orc pays 2 magic a swing; an orc out of magic still swings every 9 acts but lands nothing until its magic returns, as a player's orc does (#574). Otherwise it takes a step toward the foe, sliding along a wall or shoving past an ally that is in the way (#587). A bot that still has to turn only turns on that act, and a foe on another floor is only faced, as before. Measured on the Westlands levels, twenty seeds, 600 ticks, before these fixes and after them: acts in which such a bot's order only faced its foe, counted while it stood within 30 px of it, not busy, with no swing started for 60 ticks, 27,692 to 2,257 (what remains are bots that had to turn that round); ticks it stands within 30 px of its foe on its floor, not busy, with no blow landed for 60 ticks, 98,023 to 40,599 (that count skips a bot while it is busy, and these bots now start nearly twice as many swings, 1.69 to 3.11 per 100 ticks near a foe, so the two sides of that count are not like for like; of what remains, 1 tick is a bot whose magic is under its weapon's cost); its damage per 100 ticks near a foe 21.66 to 45.39, its blows 0.98 to 2.20. On The Long Season the same counts read 20,390 to 6,129 ticks and 39.12 to 79.56 damage. A bot whose foe is in reach behind a wall pushes against the wall for as long as the foe stays in reach, up to the whole order, where with the setting off it stands facing its foe (#582): a wall refuses 6% of the steps the fix takes on Westlands, 21% on The Long Season and 49% on Imaginations. The crews pay for it: the levels where they fell were eased in their builders where lowering a group of fighters brought them back or nearer; five levels that fell were not eased and stay harder, two because no step brought them back and three because their orcs, ghosts and small slimes that fight before tick 600 are already at level 1 (see [Balance](#balance)). |
| BD | The line check a tower or shooting bot asks before it fires | The check moves its test weapon 0, 1, 3, 6, 10 ... steps out from the pad, each spot cut to a whole pixel, while the shot flies one step an act. A foe between two tested spots is stepped over: a tower's arrow misses about two in five of the distances inside its reach, a druid's lightning about half, a golem's boulder, a fire elemental's meteor and a faerie's sprinkle a fifth to a quarter. The tower stands silent; a bot that can walk walks closer instead of shooting. The same loop can step over a one-tile wall and pass a shot that then hits the wall. | The check flies its test weapon the way the shot flies: the pad, then the shot's own step once for each step a fresh shot moves. It passes when the pad or a step meets a body the shot would strike and refuses at a wall, at the map's edge or when the flight runs out. What it still does not share with the shot: it draws its own sideways waver, so far shots are judged as often right as shots land, not shot for shot; it takes an arrow slit as open and the shot itself takes the slit's gamble, so an arrow can still break on a slit wall; it does not count the range a shot loses over trees and shrub, nor a rock or knife that drops a floor over open air; and it does not ask a family's own veto (a soldier with no knife left). The range, the drop and the veto are #572. It still passes on an enemy tree or shot in its line on the same roll the real shot makes there; an enemy door refuses it (Gameplay Fixes BF). A tower that shoots more runs out of magic sooner and then waits for it. Measured on the Westlands levels, twenty seeds, 600 ticks, before these fixes and after them: checks refused, at a wall or as a miss, although a quiet flight of the same weapon lands on the foe, 8,092 for the levels' shooters and 8,846 for the crew, to 0 and 0 (The Long Season 6,628 and 10,860 to 0 and 0); on the one level with arrow slits whose checks were counted before these fixes as well as after, The Tryxian Chronicles' first level (six seeds of 2,400 ticks), 1,422 and 209 to 0 and 0. A tower's shots per 100 ticks near a foe, on the Imaginations level whose two towers see a foe: with the setting on, 7.3 before these fixes and 8.8 after; the setting off reads 8.7 there; that level's towers' checks refused for want of magic go from 6 to 425 with the setting on (135 with it off). On Gladiator 8, 9, 10 and 12, whose towers see a foe (twenty seeds, after these fixes), the towers fire 7.63 shots per 100 ticks near a foe with the setting off and 10.52 with it on (before Gameplay Fixes BF; 10.21 with it). What a check meets when it passes although its flight meets no fighter or tent: on Gladiator and The Tryxian Chronicles, with the setting on and before Gameplay Fixes BF, 98,695 of 104,342 such passes ended on an enemy door (a door has 5,000 hit points, so the shot is spent on it) and 5,582 on an enemy shot in flight; on the four generated campaigns 2,726 of 2,731 on a shot in flight; Westlands, The Long Season and the Tower place no door, and Imaginations does, but no counted check met one (see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 6). The classic check's false passes were of another kind: 22,431 of its 118,220 on the classic campaigns ended on a fighter the shot could not reach. |
| BE | A bot whose shot is refused while it looks away from its foe | It turns a notch toward the foe and its next act turns it straight back, because the turn never records where it is going; the search it queued starts an act late. | The turn is kept: the bot's next acts finish it, one notch an act, and then its search runs. A tower, which has no walk to carry its aim, faces the foe at once; the same turn serves fix BB's near branch, so a tower within 100 px of a foe it does not face now faces it on that act too. Measured on the Westlands levels, twenty seeds, 600 ticks, before these fixes and after them: acts spent turning back after a refused shot's turn 41,803 to 0; acts until a bot that did not face its foe faces it 2.80 to 1.12 for the melee-only bots, 2.68 to 1.13 for the levels' shooters and 2.51 to 1.12 for the crew. That mean counts only the bots that faced their foe within 12 acts of the turn: before, about one in eight had not (5,544 of 42,038), after about one in 170 (247 of 40,970). |
| BF | A bot leaves a door alone | A door is a weapon-order body of 5,000 hit points that no foe search picks, and three rules written for an enemy thing reach it. A tower's or a shooting bot's line check passes on any body its test weapon meets, a door included: the test weapon meets an enemy door, as it meets every enemy weapon-order body, on the clash roll, and a door that stops it passes the check as a body does, so a bot whose foe stands behind a door shoots into the door and the shot is spent on it. A walker that walks into an enemy door swings at it, as at every body a bump attacks. And a blow makes whatever it lands on its owner's foe, a door included, after which the bot shoots at the door as at any foe until it takes another foe or the door falls. With the setting on before this fix, on Gladiator and The Tryxian Chronicles (twenty seeds of each level), 98,695 of the 104,342 checks that passed although no fighter or tent stood in the flight ended on an enemy door. | A bot attacks a door only when it is its foe, and Gameplay Fixes BL makes a door its foe only as a last resort: three rules no longer make a bot attack a door, so its line check is refused at a door, it starts no swing at a door it walks into, and a blow on a door names no foe. An enemy door on the shot's path refuses the line check, always, as a wall does: the test weapon meets the door without the clash roll and draws nothing for it, on the pad and on every step, and the check answers no. A bot (a walker no player drives) that walks into an enemy door starts no swing at it; the door stops its step as before. A blow that lands on a door, anyone's shot, blast or spell, does not make the door its owner's foe; the door still takes the blow. One exception, in the line check and the bump: a door that already is the bot's foe (a level script or a mod can name one, and Gameplay Fixes BL hands one to a bot that has no enemy it can get at) passes the check and is swung at when the bot walks into it; a blow on it names no foe either, and it stays the foe because nothing names another, so it is fought like any foe. A player's own fighter still swings at a door the player walks it into. A bot is a walker that carries no seat number: a walker with one is a player's fighter to the rule, so the measuring crews of the tests, which seat their first four fighters while nobody drives them, still swing at a door with those four (#593). A grown tree, an enemy shot in flight and every other body are as before: a tree and a shot pass the check on their clash roll, a bot that walks into a tree or a fighter swings at it, and a blow on either makes it the foe. The shot itself is unchanged: it still takes the clash roll at a door. What still strikes a door for a bot, each counted below: a special cast from a distance at a foe behind the door (no special's rule asks the path to its foe, #591), a CHARGE whose last step meets a door, a shot or a swing started at a foe that meets a door on its way, a pad that holds both the foe and a door, and a tent whose spawn pad lies on another side's door. What follows: a bot that holds the door's key walks into the door and opens it, where it stood and shot at it before; a bot with no key whose foe stands behind the door lets that foe go (Gameplay Fixes BJ) and, with no enemy it can get at, is handed the door as its foe (Gameplay Fixes BL), which it then breaks down, the door's own rule here; and a door still loses hit points to a shot that strays into it, to a player's blows and to whoever fights a door that is its foe, and still dies at 0 and opens. With the setting off every walker's bump on an enemy door swings and every blow names its foe, as in the classic game. Measured with the setting on, on Gladiator and The Tryxian Chronicles (twenty seeds of each level, 600 ticks), on one game with the door rules switched off and on, before any door rule, with the line check's part alone and with all three parts and Gameplay Fixes BG and BH: checks that passed on an enemy door 117,386, 80,470 (every one on a door that already was the shooter's foe) and 0; a door made a bot's foe by the bot's own shot 9,238, 5,107 and 0 times; bots' swings at a door they walked into 20,616, 22,585 and 0 (7,341 and 6,488 of them at a door that already was the bot's foe); blows that landed on a door 38,922, 31,648 and 1,204; the doors' hit points lost 1,801,741, 1,405,275 and 46,309; doors gone by tick 600 178, 181 and 2 of 6,680 (erased by a guard 144, 161 and 0, broken under blows 26, 14 and 0, opened with a key 8, 6 and 2). Of the 1,204 blows left, 553 are the measuring crews' seated fighters' (#593) and 651 the bots': the archer's FIRE ARROWS, whose eight fire arrows fly every way at once, 381; other specials 114, of them 101 cast from a distance at a foe; a shot or a swing started at a foe that met a door on its way 140; a pad that held both the foe and a door 16; a CHARGE and a tent's spawn none. On The Tryxian Chronicles' first level played to tick 2,400 (six seeds) the doors lost 117,396, 124,911 and 832 hit points, and 38, 51 and 0 of its 180 doors were gone at the end. With these three rules alone, before Gameplay Fixes BI to BL, a bot with no key stood at a door more: the ticks in which a locked door refused a step of one of the levels' bots on the classic campaigns went 101,179, 109,216 and 233,215, and of them the ticks in which the bot's live foe, not a door, stood farther off than the door 35,520, 42,638 and 210,419 (at the first two points that count leaves out a bot whose foe was the door itself, and both leave out the acts a bot spent finishing a swing or a shot at the door, so both rise more than the time bots stand at doors); with every rule they and the crew's make 10,422 stays, of which 9,237 ended within 60 ticks, 593 within 600, 547 lasted to tick 600 and in 45 the bot died, and the 840 stays longer than 60 ticks span 182,542 of the 264,369 ticks that all the stays span, 69 in 100 (see [Balance](#balance) and [Rulings (2026-11-16)](#rulings-2026-11-16), entry 6). |
| BG | A guard passes a door or a grown tree by | The guard of a cleric's MAGIC SHIELD, a soldier's BOOMERANG and an orc captain's HOOK BLADE destroys every enemy weapon-order body inside its ring, each costing it that body's damage in hit points, and passes by only the solid scenery a pack declares, a bone wall or a war banner (#472). The rule is meant for shots in flight, but a door (5,000 hit points) and a grown tree are weapon-order bodies too, so a guard that passes an enemy door or tree erases it at once, with no key and no blow (#588). On Gladiator and The Tryxian Chronicles (twenty seeds of each level, 600 ticks, the setting on, with the line check's part of Gameplay Fixes BF) 161 of the 181 doors that were gone at tick 600 went that way. | The guard passes an enemy door and an enemy grown tree by, as it passes a bone wall or a war banner by: neither is destroyed and the guard pays nothing for it. A door then needs its key or blows, and a tree has to be cut down. Every other body the guard meets is destroyed as before: a shot in flight, a cleric's glow, a boulder, a blood stain, an enemy's circle of protection (#592). A door or a tree is weapon order and that family, because a golem's family number is a door's. The three guards ask one question for it, so the hook blade passes them by too. The grown tree was added in this work, for the maintainer to overrule (the alternative: the door alone, as he asked). Measured with the setting on, on Gladiator and The Tryxian Chronicles (twenty seeds of each level, 600 ticks), on one game before any door rule, with the line check's part of Gameplay Fixes BF alone and with every door rule: doors a guard erased 144, 161 and 0, grown trees a guard erased 20, 15 and 0, and on The Tryxian Chronicles' first level played to tick 2,400 doors a guard erased 33, 39 and 0; a guard erased no circle of protection in any measured battle, where no bot cast one and the only circles were the 260 placed on Tryxian 107 (see [Balance](#balance)). |
| BH | A bot's cast wants its pad clear of a door, a grown tree and a shot in flight | No rule (fix Q). With the setting on before this fix the rules of the druid's GROW TREE and SUMMON FAERIE, the barbarian's HURL BOULDER and EXPLODING BOULDER, the mage's ENERGY WAVE and the archer's EXPLODING BOLT (fix AA) asked their weapon's pad the plain way: a wall, the map's edge, a foe's body or a tent on it said no, but an enemy door, an enemy grown tree or an enemy shot in flight passed, because a quiet weapon probe passes every enemy weapon-order body; `fire()` meets such a body on 4 rolls in 10, so the cast was then refused `COULD NOT FIRE` and the bot had paid its swing for nothing (#590). With Gameplay Fixes BG more doors stand, and on The Tryxian Chronicles' first level (six seeds of 2,400 ticks) the druid's GROW TREE met that refusal 11 times. | The five rules ask the strict question of fix AV, as the elf's volleys and BARRAGE do: an enemy door, grown tree or shot in flight on the pad is a no, the clash roll taken as lost and nothing drawn; a body of the caster's own side still passes. SUMMON FAERIE already said no to such a body through the faerie's room (fix AR: its faerie stands on the bolt's spot in a box of the bolt's size), so for it the strict question changes no answer in stock play. The rule asks the pad, the first step of the flight, not the path: a cast from a few steps back at a foe behind a door or a wall is still made (#591). A player's cast is not asked about. Measured with the setting on, on one game with this rule switched off and on: on that level and those seeds the refusals read 0 before any door rule, 0 with the line check's part of Gameplay Fixes BF alone, 11 with every door rule but this one and 0 with it; on Gladiator and The Tryxian Chronicles (twenty seeds of each level, 600 ticks) the druid's, barbarian's, mage's and archer's casts refused `COULD NOT FIRE` after their rule's yes read 47 without it and 0 with it, and the rule said no on 1,312 pads the plain question passed: the barbarian's throws 1,115 (an enemy shot in flight on the pad 1,111 times, a door 4), GROW TREE 149 (a shot 130, a door 19), SUMMON FAERIE 47 (the faerie's room said no to each of them too), ENERGY WAVE 1 and EXPLODING BOLT none. Westlands, The Long Season and the Tower place no door, no grown tree and no druid, but the rule holds a barbarian's throw while an enemy shot in flight is on its pad (on the forty seeds 1,451 times on Westlands, 1,123 on The Long Season and 86 on the Tower), so their runs change: none of their levels fell by the measure's three errors and none rose by two, and The Long Season 4 reads two errors under, 1.38 to 1.20; on Imaginations, which places doors, it alone moved the survivors: level 3 rose by two errors, 7.28 to 7.45 (see [Balance](#balance)). |
| BI | A door a bot cannot open is a wall to it | No rule. The route finder prices a door like any body, ten a cell, and asks the terrain alone whether a cell can be stood on, so a bot's route to its foe or to a point runs through a locked door whenever going round costs more, and a bot standing just under a door, its corner a pixel inside the door's cell, is routed from that cell straight into the door. The straight walk toward a foe or a point asks the terrain alone and steps into the door. The push an ally gives (fix X) asks the terrain alone too, so a bot standing at a door is sent into it again and again and loses its own orders each time. | For a bot (a walker no player drives), a door it cannot open, an enemy door whose key it does not hold, is a wall to its route, its straight walk and the push an ally gives it. The route is planned on a map of where the bot's kind of fighter can stand with such doors shut: it goes round such a door as round a wall, takes no diagonal past one, starts from the cell the bot's body overlaps most when its corner sits in a door's cell, and ends beside a foe whose own cell such a door closes. A route that could only fail is not searched; the route stays empty, as a failed search leaves it. A stair whose landing such a door closes is no way up or down. The straight walk takes no step whose box meets such a door, and an ally pushed toward one keeps its orders. A door the bot can open (its own side's, or one it holds the key to) is walked through and costs ten a cell, as before; a bot whose foe is a door that bars it (a level script or a mod can name one) is routed into that door and no other. A player's own fighter routes, walks and is pushed as before. A tower and a guard at its post are bots here too: a tower never moves, so its route and straight walk only turn it (along the way round such a door rather than toward the door), and a guard or a tower standing at such a door is not pushed into it. Limits: the ATTACK order's step, a FOLLOW order's step, a hurt bot's flight and the wall-following walk still step toward a door, so a bot whose foe is near (within 100 px, or anywhere on a map of more than 200 bodies, where no route is planned) can still be seen pushing at a door, as at a wall (#582); a teleporter is not a way round; a replay recorded with the setting on before this fix parts from its recording at the first route, step or push a door changes. Measured on a measuring build of 2026-11-20 on Gladiator and The Tryxian Chronicles (twenty seeds of each level, 600 ticks, the setting on), the levels' bots' stays at a locked door longer than 60 ticks went from 826 to 152. |
| BJ | A bot treats an enemy that only a locked door keeps it from as if it were not there | No rule. A bot takes, holds and walks at an enemy it could reach only through a door it cannot open: the foe searches answer the nearest enemy whatever stands between, a foe handed over by a blow, by the bot's own shot or by a script is held, the near branch of the chase counts every enemy within 100 px, an ally following the player's fighter takes its leader's foe, and a cast rule counts every enemy in its range, so an archer behind a locked door fires rings of arrows at a room it cannot get into. With Gameplay Fixes BI such a door is a wall to the bot's route, so it pushes at the door or stands beside it for the enemy behind. | For a bot (a walker no player drives, not a tower and not a guard at its post), an enemy that only a door it cannot open keeps it from is not there: with every such door open the bot could walk to the enemy, with them shut it cannot, and its own shot does not land on the enemy from where it stands. The quick foe search passes such a candidate after its cloak roll and goes on; the full scan passes it for both its picks, and with it the world's hand-out of a foe after each act; a held foe of that kind is let go at the top of the bot's next SEARCH or ATTACK round, its orders, its leader and its route cleared, which catches a foe handed over by a blow, by the bot's own shot landing, by a yell or by a script; the near branch counts no such foe; an ally following the player's fighter does not take its leader's foe of that kind and follows instead; and while a bot's cast rule is asked, the range finder leaves such enemies out. A bot with no foe that has such an enemy on its floor wanders no more: it is handed the door in its way as its foe when it has no enemy it can get at (Gameplay Fixes BL), and stays where it is when there is none to hand. An enemy the bot's shot lands on from where it stands (through an arrow slit, across water) is kept only by the shot: the bot faces it, shoots, and takes no step for it. When a door opens, the enemy is an enemy again on the next tick. A foe behind a wall or across water with no door in the way, a foe with a way round, a foe on another floor, a player's fighter, a tower and a guard at its post are as before. Limits: a struck bot still takes its striker at the blow and lets it go on its next round; a cast rule that wants no enemy near (the mage's TELEPORT, the skeleton's TUNNEL) says yes more often, which is an idle bot's cast and stays; a teleporter is not a way round; a bot can still be seen pushing at a door when its enemy is near and has a way round (the ATTACK order's step, as at a wall, #582); two sides a locked door keeps apart no longer stand and wait, each bot breaks the door in its way down (Gameplay Fixes BL); a replay recorded with the setting on before this fix parts from its recording at the first enemy let go or passed over. Measured on a measuring build of 2026-11-20 on Gladiator and The Tryxian Chronicles (twenty seeds of each level, 600 ticks, the setting on): the levels' bots' stays at a locked door longer than 60 ticks went from 826 before any door rule (152 with Gameplay Fixes BI) to 66 with this rule too, and the ticks a locked door refused a bot a step from 233,215 to 44,392; with Gameplay Fixes BI and every part of this rule but its cast rules' part, archers waiting behind a locked door cast a special at nobody 5,525 times, and with that part never. |
| BK | A bot that holds the key to a door on its way walks its route into the door | No rule. Once its enemy is within 100 px, or anywhere on a map of more than 200 bodies, a bot's chase skips the route finder and steps straight at the enemy (the near branch), whatever stands between. A bot that holds the key of the door between it and its enemy therefore steps at the wall beside the door instead of walking into the door, which its key would open. With Gameplay Fixes BI and BJ such a bot walks its route while its enemy is farther off, then pushes along the wall beside the door once it comes near. | For a bot (a walker no player drives, not a tower and not a guard at its post) whose way to its foe on its own floor only its key opens (it holds the key of a door on its floor, walks to the foe with the doors it cannot open shut, and holding no key would have no way), the near branch is not taken: its route is planned as for a far foe, it follows it, and its step into the door's cell opens the door with its key. The question is asked only where the near branch would be taken, and it draws nothing; when it acts, the near branch's draws (its foe search's cloak rolls, the ATTACK length, the swing) are not made. Nobody goes to fetch a key: this is the use of a key a bot already holds, by a monster as by a teammate. A bot with no key, a foe with a way that needs no key, a foe on another floor, a player's fighter, a tower and a guard at its post are as before. Limits: nothing changes in the ATTACK order, so a bot already in it when its enemy came near keeps its steps (as at a wall, #582); a replay recorded with the setting on before this fix parts from its recording at the first chase that plans a route instead. Measured on a measuring build of 2026-11-20 (a staged room, twenty seeds, 600 ticks, the setting on): a bot handed the key of the room's door at tick 200 opened it in 20 runs of 20, 48 or 49 ticks after the key, against 1 run of 20 with Gameplay Fixes BI and BJ alone; on Gladiator and The Tryxian Chronicles (twenty seeds of each level, 600 ticks) the rule changed the branch on 35 re-plans of the levels' monsters and moved no survivor or damage figure. |
| BL | A bot with no enemy it can get at breaks down the locked door in its way | No rule. A door no bot can open stands until a key opens it or it falls to blows. With Gameplay Fixes BI and BJ a bot whose only enemies stand behind a locked door holds no foe and stays where it is, so two sides a locked door keeps apart stand and wait for as long as the level lasts. | For a bot (a walker no player drives, not a tower and not a guard at its post) that has no enemy it can get at (one it walks to with the doors it cannot open shut, or one its shot lands on from where it stands) while an enemy on its floor is one only a locked door keeps it from, the door in its way becomes its foe: the world's hand-out of a foe after each act, when it finds none, and the bot's own decision of what to do with no foe hand it the door. The door in its way is found by a walk over the bot's map, from the regions the bot gets to (its own, and those its stairs lead to, one way, at no cost) through the doors that bar it to a region from which an enemy living or tent can be reached; each door entered counts one, so two doors in a row count two, with floor between them or none. Of the doors on the bot's floor beside its regions, the one through which the fewest doors are entered is handed, then the nearest (in cells from the bot), then the topmost, then the leftmost; a door that leads to no enemy (a closet's) is handed to nobody. A bot that fights only hand to hand (the orc and the small slime; the ghost has the same flag but walks through walls, so no door shuts it out) walks into the door: on its first chase after the door is handed it plans its route into the door (fix BI's route ends in the door that is its foe), at any distance and on a map of more than 200 bodies, never by the chase's near branch, and it follows that route from the cell it now stands in, read anew on every step as a route planned there would read it, so its last step meets the door from every side, from below and from the right too, where it rests one pixel inside the door's cell; its bump swings at the door (fix BF lets a bot swing at a door that is its foe), and an enemy near it that it could not get at even with every door open (across water, walled in) no longer pulls it off its door into an attack. A bot that shoots keeps its walk and shoots at the door from where its line check passes (the line check passes on the door that is the shooter's foe). It keeps the door while it still has no enemy it can get at and the door still leads to an enemy, even when a better door appears; when only another door does, it takes that one; the moment it can get at an enemy (a key it is given included: a door it can open is no door in its way) it lets the door go, its orders, its leader and its route cleared, and the hand-out of the same tick gives it that enemy. This keep is asked at the top of its rounds and after every act, so a bot in the middle of a swing lets go too, and of the door rules it alone lets a door go: one act in five a bot with no orders runs its random act, which one time in 80 throws its foe away for a fresh search of the foes near it; a bot holding a door does not (its draw of 80 is made as before, but the fresh search and its cloak rolls are not), so it never drops its door there and is never handed another door for having stepped aside. What gives any bot a new foe still gives a bot holding a door one: a swing or a shot at the door that passes through it onto a fighter makes that fighter its striker's foe, a bot struck by a new attacker takes the attacker, a yell gives the yeller's friends near it the yeller's attacker, and a bot that shoots, within 100 px of its door (or on a map of more than 200 bodies), takes an enemy within 100 px of it, nearer than the door, that it cannot get at even with every door open (walled in), by the chase's near branch, which a melee bot holding a door no longer takes. Every judgment still sees the held door as shut, so the enemy behind it stays out of reach until the door falls or opens. Monsters shut in a room break their door as the player's teammates outside do: no rule asks whose side a bot is on. A door keeps its 5,000 hit points, so under blows it holds for a minute or two; the key stays the fast way. Nothing is stored for the rule: the door a bot holds is its foe, which a snapshot carries. A player's fighter, a tower and a guard at its post are never handed a door. Limits: a swing or a shot at the door still takes the clash roll and passes through it 6 times in 10, a shot striking the room behind (the struck fighter is its striker's foe for a round, fix BJ lets it go, and the door is handed again); a crowd at a one-cell doorway strikes the door one bot at a time; a swing can strike the door beside the one it aimed at (a door next to a door); a fighter whose weapon is wider than the doorway lands nothing on a door set in a wall (the cleric: the wall cells beside a one-cell door refuse its 20 px glow, as they refuse a player's cleric's); a fighter wider than the doorway is handed no such door, since it could not pass it even open (the medium slime, the two beasts); a melee bot with no route to its door (a body wider than a cell at one door of a two-door doorway) takes the old walk and strikes the door seldom; a bot that holds the key of a door behind a crowd of its own side pressed against that door is held off it (the crowd breaks the door instead); a door a level script or a mod named a bot's foe is let go like any other when the bot can get at an enemy or the door leads to none; a replay recorded with the setting on before this fix parts from its recording at the first door handed. Measured on a measuring build of 2026-11-24 (a staged room of twelve orcs behind a door of a third side, four crew bots outside, twenty seeds, 1,800 ticks, the setting on): every bot of both sides was handed the door on the first tick; a closet's door beside the crew took no blow; while an enemy the crew could walk to lived, the crew landed no blow on any door, and when one appeared every crew bot let its door go one tick later. Measured on 2026-11-26 with melee bots walking into their door and every bot keeping its door through its random act, in the same room in the unit tests' world, where every damage draw answers its least (the least a blow can do): the door fell in 6 runs of 20 (ticks 1,141 to 1,771), to the barbarian alone in 13 of 20 (ticks 1,359 to 1,587) and to the twelve orcs alone in none (on average 1,713 of its 5,000 hit points off by tick 1,800: one orc at a time strikes it from the doorway); over those 60 runs a bot holding a door drew its random act's one-in-80 97 times, each a time it kept its door; with one fighter of each family alone outside and the orcs inside at their posts (six seeds of 1,800 ticks) the door fell to the soldier in 5 runs of 6 and to the fire elemental, the thief, the orc captain and the barbarian in 6 of 6, an orc took 1,498 of its hit points on average, a small slime 145 and a faerie 370 (their blows are light), the cleric none, and the medium slime, the two beasts and the ghost were never handed it. |

TELEPORT, REVEAL, CLOAK and ENERGY WAVE are unchanged as casts (their bot
rules are in [Bots](#bots)): each does something whenever it is cast
(ENERGY WAVE crosses walls along the facing), except an ENERGY WAVE whose
first step is a wall face or past the map's edge, which refuses in both
games and with the setting on spends nothing (fix AC). CHARGE still refuses `PATH
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

Fixes X to BL, in play:

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
  2026-11-01, before fix BC: an orc, ghost or slime has no aimed attack
  (its shot is refused before any aim; the attack order only faces a foe
  inside its weapon's reach), so its blows are bumps, and the right-hand
  walk's probes were about 43 % of them in the classic. Asked wholly
  quietly, those bots
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
  the pad too (`G.pad_clear`; since fix BH the strict
  `G.pad_clear_of_weapons`). The archer asks it with the fire arrow (9x9) its
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
  because its gate asks the pad first (fixes V and AA). Issue #476 found
  three more casts that fire a weapon from the pad and paid for nothing
  at terrain: the archer's EXPLODING BOLT, the barbarian's HURL BOULDER
  and EXPLODING BOULDER, and the mage's ENERGY WAVE call `fire()` first,
  and `fire()` charges the shot (1 MP, 2 and 5) and draws the waver before
  its pad probe refuses. The old game does the same, so the classic cast
  is unchanged. With the setting on these casts ask the pad first too, the
  archer with the 9x9 fire arrow its bolt fires rather than its own 7x7
  arrow (`gf.pad_blocked_by_terrain(self, weapon)`), and refuse before
  they pay. A body on the pad keeps the paid blow here as well.
- **AD.** A tent or a bones pile owns the troops it makes, and every
  question of whose side a walker is on climbs its owner chain to the top:
  `is_friendly`, the blow's friend test and the score's team. A thief's
  CHARM OPPONENT or an archmage's MIND CONTROL on such a troop, or on an
  enemy caster's summon, changes its colour but not its owner, so it wears
  the charmer's colour and answers for its owner's side: it fights its
  charmer and is struck by its charmer's team. With the setting on the
  troop fights for the colour it wears (fix AZ: every walker does), and
  the owner-chain climb, which still names the person behind a blow, stops
  at it while the charm runs, so its blows' experience and credit are its
  own. Its owner, its clock and the cascade when its tent falls are
  unchanged. A body a ghost rides stops the climbs too, on both values
  (#430, under [Not gated](#not-gated)). The level-play test met the
  classic shape once: an enemy thief counted a crew-raised ghost as a foe
  and taunted it while the ghost wore the thief's own colour, because the
  ghost's chain still answered for the crew's cleric.
- **AE.** A struck archmage that no bot rule answers for (every archmage
  with the setting off, a seat-driven one with it on) takes the classic
  order: SUMMON first when a foe is within 200 px, then slot 2, CHAIN
  LIGHTNING or HEARTBURST on a coin. The old game's `walker::special()`
  has answered 0 on every path since the 2002 import, so its reply never
  ended because a cast worked: a summon that stands goes on to the coin,
  and the summon's 15 ticks of busy refuse the cast the coin picks; a
  chain goes on to a HEARTBURST attempt that its own busy refuses; and the
  "then leave! :)" follow-up, a second HEARTBURST when the price of a
  TELEPORT is left, never ran. This game read the cast's real answer, so
  with the setting off it ended the reply after a summon and cast that
  second HEARTBURST after a chain or a burst (#422). With the setting off
  it now ignores the answer, as the old game does, draw for draw. With the
  setting on nothing changes: a cast that worked ends the reply, after the
  follow-up for slot 2. No earlier recording reached the struck classic
  path, so every golden is unchanged; the fix's proof is
  `archmage_bot_struck_scen99`, the struck archmage arena with the setting
  off, captured from the old game (see [Parity](#parity)).
- **AF.** During FREEZE TIME the player's side acts and everyone else's
  Livings and generators stand still; effects, treasures and weapons
  always act. The old game asks whether the walker itself is on team 0,
  red, the colour of the classic player. This game asked since #92
  (`ec263a26`, fixing #86) whether it is friendly to the player's team,
  the head of its owner chain wearing the player's colour, the rule that
  completion and respawn already read. The two answers differ for a player
  colour other than red (Base Camp's seats, a networked team), where the
  old rule freezes the player's own side and lets team 0 fight on, and for
  a walker whose colour is not its chain's: a charmed troop of a living
  enemy tent, or an enemy's summon the player charmed, wears red but
  answers for its owner, and the player's summon that an enemy charmed is
  the reverse. With the setting off the gate asks team 0 again, as in the
  old game (#461); with it on it asks friendliness, as before. Fix AZ
  makes friendliness read each walker's own colour, so with the setting on
  a walker acts during FREEZE TIME when it wears the player's colour,
  whatever its owner wears. Only the act gate follows the setting: the
  level census (a frozen foe still holds the level open, #231) reads
  friendliness on both settings, as the old game's does.
- **AG.** In the trees a forestwalker (the elf, and any pack class with
  forest walk) steps shorter by its forest penalty: 4 - dexterity/10 for a
  hero, 4 - level/2 for a placed or bot walker, never below 0. The classic
  game takes the penalty off the step the walker carries from its last act
  and adds a speed potion's (or HASTEN's) bonus to that, so in the trees the
  step changes by the bonus less the penalty every act (#462; the old game
  has the same arithmetic). A hastened elf whose bonus beats its penalty
  steps farther every act and keeps that size until it leaves the trees:
  with no penalty and a level-2 potion, 204 px a step after 100 acts, and a
  walk checks only where it lands, so it steps over walls, water and
  bodies. Without a bonus every elf with a penalty slows to 1 px within a
  few acts, so dexterity below 40 makes no lasting difference. With the
  setting on the penalty comes off the normal step every act and the bonus
  is added once on top: a hero elf with 30 dexterity walks 3 px in the
  trees, and a hastened one the normal step less the penalty plus the bonus
  while the bonus lasts. Level 3 to 7 elves and hero elves of dexterity 11
  to 39 (on the elf's 4 px step) are faster in the trees than in the
  classic game.
- **AH.** TURN UNDEAD turns a skeleton or a ghost (or any pack family that
  declares itself undead) that loses the resistance roll. In the old game
  the kill loop asks only the family, so a warded one, carrying
  `BIT_INVINCIBLE` or with an invulnerability potion running, is rolled
  for and turned like any other: turning never goes through the blow's
  ward. This game spared it on both settings since #195 (`be8a6d09`, "the
  isle ward survives real play"), because turning is damage. With the
  setting off it is now turned again, as in the old game; with the setting
  on it is spared before the rolls, never counted by the cleric bot's gate,
  and a ring that holds nothing else turnable is refused as `NO UNDEAD IN
  RANGE` (fix E), so the cast spends nothing (#474). With the setting off a
  New Specials ghost's PHASE no longer saves it from TURN UNDEAD. The ward in
  `do_combat_damage` is unchanged: only a scripted match's turn and a
  level's damage gate reach it, and the old game has neither.
- **AI.** FREEZE TIME has two forms. A mage on the player's side stops
  time for everyone else: the world's freeze bank grows by 20 + 11 per
  level and the screen turns blue. Any other mage gives itself and every
  friend on the map bonus rounds instead, 5 + 2 per level up to 50. The
  old game picks the form by `team_num == 0 || myguy`: the red team, or
  any hero. This game has picked it by the player's team since the
  companies work (#139, `75a786a9`): a hero of another colour that banked
  the global stop froze itself and its own allies. With the setting off FREEZE TIME now asks what the old game asks
  (#477); with it on, the player's team. The two tests agree when the
  player is red and the mage is not a hero of another colour, which is
  every parity row's arena, so no golden moves.
- **AJ.** Some levels let the player leave while foes still stand (the
  level type "can exit whenever"). When such a level's exit leads to a
  level the company has already won and this one is not won yet, the old
  game asks "Withdraw to X?" first: yes abandons the level and reloads the
  save, and no falls through to its exit test, which passes on this level
  type, so it asks "Exit to X?" next, where yes ends the level as won. This
  game has asked only "Exit to X?" there since `91175553` (#98), which put
  the exit test first. With the setting off the pad asks Withdraw first
  again and the server asks Exit after a no, for the same level and to the
  same player (#481); with it on, Exit only. A prompt that closes without
  the player's answer (its 15 seconds ran out, or the hero who stepped on
  the pad died) asks nothing more. No parity row plays such a level, so no
  golden moves.
- **AK.** A walker on the red team that picks up a key it does not hold
  yet announces it: "Gorik picks up key 3". The old game hands the line to
  `do_notify(message, eater)`, which shows it in the view whose hero picked
  the key up and in every view only when no player drives that hero. This
  game has shown it in every view since the per-seat notification work
  (`25113b61`, #254), because keys are per walker and the line is how the
  rest of the party learns who can open the door. With the setting off the
  line goes to the picker's view again (#482); with it on, every view. The
  recording keeps a notification's text only, so no golden moves.
- **AL.** MIND CONTROL takes foes while its budget lasts: the price covers
  the first, and each further one needs 10 of the magic left after the
  price. The old game counts that budget down to choose how many it takes,
  and then works out the charge for the extra men the same way, on a local
  that never reaches the archmage's magic, so its cast costs its price
  alone (#485). This game took the extra 10 a man since #79. With the
  setting off it now costs the price alone, as the old game's does; with
  it on the charge is kept: a level-10 archmage with 400 magic that takes
  five men keeps 250 with the setting off and 210 with it on. Both settings
  take the same men. No parity recording controls more than one man with
  magic to spare, so every golden is unchanged.
- **AM.** A thief's poison cloud drifts by random walks. One that drifts
  into a wall (or the map's edge) can be wedged: its step, the half steps
  and the slide all fail and its facing is put back, so the tick moves
  nothing. The old game only steps the walk while the cloud has one, so a
  wedged cloud sits against the wall until its walk of up to 19 steps runs
  out (#233 saw it frozen there). This game has re-rolled since `28f8168a`
  (PR #245): a tick that neither moved the cloud nor turned it drops the
  stuck walk and draws a fresh direction and length. With the setting off
  the cloud keeps the stuck walk again (#486); with it on, the re-roll. A
  cloud that slid or turned keeps its walk and draws nothing on both
  values. No parity row wedges a cloud, so no golden moves.
- **AN.** The archmage bot's HEARTBURST / CHAIN LIGHTNING rule (fix Q)
  keeps the coin's arm when that arm has a foe in reach. It measured the
  chain at 200 px plus 7 per level for every archmage, which is the cast's
  reach for a monster only: an archmage with a character record (a
  promoted hero the computer plays when it is not on a seat) reaches 200 px
  plus half its Intelligence plus 2 per level (#453). A level-14 hero with
  60 Intelligence reaches 258 px, not 298: with its only foe at 280 px the
  rule kept the chain and the cast refused `NO FOE IN RANGE`. A level-1
  hero with 200 Intelligence reaches 302 px, not 207: with its foe at
  300 px the rule turned to HEARTBURST, which has no target there, and
  held. With the setting on the rule now asks the cast's own reach, so the
  rule and the cast agree. The struck bot's answer (fix R) asks the same
  rule. No parity recording fields an archmage with a character record, so
  every golden is unchanged.
- **AO.** CHARGE's rush strikes the body the collision field names after
  its last step, then erases that body's orders and walks it four steps. A
  rush that opens a door with a key strikes the door its own step has just
  killed: the strike draws its damage roll and does nothing to a dead body,
  and the classic rush still erases the dead door's orders and forces the
  walk on it (#426). With the setting on a body that is dead once the rush
  has struck (that door, or a foe the strike itself killed) is left as it
  lies: the damage roll is still drawn, but its orders stay and no walk is
  forced. Nothing a player sees changes, since a dead walker never acts and
  every revival clears its orders, and no recording holds a dead body's
  orders, so no golden moves.
- **AP.** A summon's or a shot's side is read by climbing its owner chain
  to the head (a knife to its thrower, a summon to its caster). The
  classic game climbed two ways: friendliness stopped at a dead owner, so
  the walker then stood for itself, while a blow's score, experience and
  kill credit climbed through it. In the tick a charmed caster dies, its
  summon and its shots were asked for friendliness as their own colour but
  credited to the caster's (#429). With the setting on friendliness no
  longer climbs at all (fix AZ: every walker fights for the colour it
  wears), so a dead owner changes no side. The climb a blow's score always
  took stays for the person behind a blow, through a dead owner and
  stopping at a self-loop and at a charmed walker (fix AD): the score
  gate's hero, the experience and the kill credit of a dead caster's
  summon's or shot's blows are the caster's for the rest of that tick. A
  body a ghost rides stops every climb on both values (#430). The mutant
  mode's damage gate reads the same climb. No recording stages a charm, a
  death and a blow in one tick, so no golden moves. A walker that dies
  charmed keeps its colour and its charm clock until it is revived (#505);
  with the setting on that clock decides only whom its summons' blows
  credit for the rest of that tick.
- **AQ.** When a player's step meets a wall, the walk tries to slide along
  it: it asks the half step up or down and the half step across, and takes
  each half that is free. In the classic game each half is asked with a
  real step, taken or not: on a half that a body blocks, the probe eats a
  treasure it reaches before the body, swings at the body when it is a
  foe, opens a door the player holds the key to and posts "Key N needed!"
  at a locked one (#434). With the setting on each half is asked quietly
  first, as the right-hand walk's probes are (fix Y), and only a free half
  takes the step, which eats what it moves onto as before. One thing a
  player sees: a keyed door beside a wall opens when the player walks into
  it, not when the slide brushes it. The new pair `player_slide_potion_scen99`
  and `player_slide_potion_fixes_scen99` records it; no existing golden
  moves.
- **AR.** The druid bot's SUMMON FAERIE rule (fix Q) asked the bolt's pad.
  A bolt passes an ally and a faerie does not, so with an ally standing
  where the faerie would appear the rule said yes and the cast refused `NO
  ROOM TO SUMMON` (#435). With the setting on the rule also asks the cast's
  own room question for a faerie of the druid's where the bolt stands,
  quietly and never with the druid's own body (#423): an ally, an enemy
  door or tree, an enemy weapon in flight or an arrow slit there is a no.
  With the setting off a bot has no such rule, so no setting-off golden
  moves; the new pair `druid_bot_faerie_room_scen99` and its setting-on
  twin records it.
- **AS.** The archer's BARRAGE fires three arrows and answers yes whatever
  each does: at a wall face or the map's edge no arrow leaves and it still
  costs its 60 magic, and with a body on the arrow's pad it lands three
  melee blows for the 60 (#436). With the setting on a pad blocked by
  terrain refuses `COULD NOT FIRE` before anything is paid (as fix AC does
  for the other casts), and a first arrow that cannot leave refuses `COULD
  NOT FIRE` after its one blow: the cast costs that swing, never the 60.
  The bot rule asks the arrow's pad as well, an enemy weapon on it included
  (fix AV's question). With the setting off the cast still calls `fire()`
  three times in order, so no setting-off golden moves; three new pairs
  record it.
- **AT.** A walker turns one notch a call toward the facing it is asked
  for. Asked for the facing it already has, the classic turn still moves it
  one notch counter-clockwise and points its heading that way (#446). A bot
  within 100 px of a foe it already faces is turned off it on the tick it
  re-plans, and the swing it then tries only turns it back, so that swing
  never starts; a chaser whose shot would land turns off its foe before it
  attacks; a bot whose shot is refused while it faces its foe (a
  melee-only bot always) is turned off it. With the setting on the walker
  stays as it is, facing, heading and walk frame, and the turn reports
  done, so a bot standing at its foe swings when it is not busy. Measured
  on The Tryxian Chronicles' first level, six seeds of 2400 ticks: 2,307
  such tries and no swing with the setting off, 2,291 swings in 2,383 tries
  with it on. It moves 44 rows with the setting on (see [Parity](#parity));
  no setting-off golden moves.
- There is no fix AU: #450 asked a second time for fix M's seat half (see
  [Charm and seats](#charm-and-seats)), so no new letter was given.
- **AV.** The elf bot's volley rules (fixes Q and V) asked the rock's pad
  quietly, and a quiet question passes an enemy weapon in flight, a door or
  a grown tree, which `fire()` meets on 4 rolls in 10: the cast then
  refused `COULD NOT FIRE` after the rule's yes (#451). A rule may not draw
  that roll, so with the setting on it takes it as lost: any such body on
  the rock's pad is a no, which holds about six casts in ten that would
  have flown. With the setting off a bot has no such rule, so no
  setting-off golden moves; the new pair `elf_bot_rocks_tree_pad_scen99`
  and its setting-on twin records it.
- **AW.** A blow by something a hero owns pays the hero the blow's
  experience. `walker::attack` pays it for anything that is not a living
  (the arm for thrown things) and again for any owned hitter that is not a
  projectile (the arm meant for summons), so an effect, which is neither,
  paid twice (#494). The thief's bomb is such an effect: its blast takes the
  thief's level, so each blow it lands paid twice what the same blow struck
  by hand pays. With the setting on the second arm pays only a summon. The
  other owned effects that strike are POISON CLOUD and a mine's puff, a
  magic shield and a boomerang, chain lightning's blast and an archer's or
  barbarian's exploding shot, and with New Specials a fire elemental's
  embers, IMMOLATE's contact burn and its meteors, and the orc captain's
  hook blade; each now pays once (a SUPERNOVA blast goes off after its
  caster is gone, owns itself and pays no one). Experience is not in the
  recordings (the dump holds each walker's place, hit points, team and
  life), so no golden can move.
- **AX.** The difficulty scales a walker by its team: on Easy or Hard every
  walker off team 0, the player's side, takes the percent, and none on team
  0 does (#493). A fresh summon is on team 0 until its caster's team is
  written, and three summons are scaled before that write, in the old game
  as here: the cleric's RAISE GHOST and the archmage's SUMMON ELEMENTAL and
  phantom. So a foe cleric's ghost and a foe archmage's elementals and
  phantoms kept their Normal hit points, magic and damage while their
  caster was scaled (#495). With the setting on they take the caster's team
  before they are scaled, so a foe's takes the percent and a player's still
  does not. Every other summon and spawn (the cleric's skeleton and its
  RESURRECT ghost, WARBAND's grunts, LEGION's risen, a slime's child,
  generator spawns, the bot squads) already took its team first, on both
  settings. The druid's faerie, the friendly RESURRECT and a slime's
  fission are never scaled on any team, in the old game as here, and
  neither is a faerie's WISH (one of this game's New Specials); all stay
  so. Every recording plays at Normal, where the percent gives a summon's
  whole-number values back unchanged, so no golden moves.
- **AZ.** Every friend-or-foe question in the classic game asks the head
  of each walker's owner chain (a knife to its thrower, a summon to its
  caster, a troop to its tent), so a thing fights for whatever its maker
  is now: when a charm, MIND CONTROL or a death changes the maker's side,
  its shots in flight, its summons and its troops change side with it. And
  a thief's bomb, its blast, a mine's and an exploding shot's blast,
  ENERGY WAVE, a boulder and a returning knife are made with no colour
  (red), so a thrower who dies before his bomb goes off leaves a blast
  that strikes his own side in full and pays red for every blow (#533). A
  blow's score went to the hitter's own colour while the gate that decides
  a score is due read the chain's head (#502). With the setting on
  everything fights for the colour it wears and nothing follows its maker
  (the maintainer's ruling: shots "should all stay with the team they were
  when cast"). A thing takes its maker's team when it is made and keeps
  it: whom it may hit, whom it spares, whom a blast halves (the blast's
  own colour, so a dead or charmed thrower's blast still softens on his
  side), which team its blow's score pays and which team its kill counts
  for all read that colour. A summon stays on the team it was summoned for
  whatever happens to its caster, and a tent's troop on the team the tent
  had when it made it. The one exception is a thing that rides the walker
  who owns it (a ring of protection, a cleric's mace, a soldier's
  boomerang, a captain's hook blade, a ghost's scare, IMMOLATE's fire, a
  returning knife on its way home, the F2 cheat's magic shield): it is part
  of that walker and goes with him (the maintainer: "obviously they should
  go with the walker"), so it fights for whatever side he is on now, never
  against him, and its blows score and count for that side. What belongs
  to a person stays with the maker: experience and
  kills on a hero's record, the hero whose record opens the score gate,
  whom the victim turns on, and a blast's quarter on its own thrower. A
  walker is never credited with his own death: when the person behind a
  blow is the walker it kills, the death is announced as one by his own
  side, with no kill on his record, no kill experience and no kill score.
  With the setting off the classic game plays byte for byte. This rule
  replaces fix AY: with it a seat's hand-off and Switch Character ask the
  same colour test, so AY's extra question is gone (#496). What a player
  sees with the setting on:
  - An arrow, a knife or a fireball loosed before its maker was charmed
    keeps fighting for the colour it was shot in: it hits his new friends,
    and him too should he stand in its way, and pays its own colour.
  - A bomb or a mine keeps the colour its thief had when he laid it, and
    so does its blast: it halves on that colour's side, strikes everyone
    else in full and its own thrower at a quarter. A thief charmed during
    the fuse is struck at a quarter by his own bomb, and his new friends
    in full.
  - A dead thrower's blast halves on his side and pays his team (#533).
    His team earns score only when it is the player's team: a hero on
    another team scores through his hero record while he lives, but once
    he is dead his shot or blast heads its own chain, the score gate is
    closed and the same blow pays nobody. That is the one case where the
    maker's death changes the pay; it is left for a later round (#560).
  - A summon fights for the colour it was summoned for: it fights its own
    caster once he is charmed, and a summon a charmed caster made for our
    side stays ours when the charm ends, so a seat whose hero died is
    handed it and a seat on it can Switch Character back to its heroes
    (#496, #536). A tent's troop keeps the colour the tent had when it
    made it, so it fights its tent if a script recolours the tent.
  - A ring of protection, a mace, a boomerang, a hook blade, a ghost's
    scare, IMMOLATE's fire, a returning knife on its way home and the F2
    cheat's shield go with the walker they ride. While he is charmed they
    fight for the charmer's side: the ring blocks his old friends and lets
    his new friends pass, the boomerang, the mace and the hook blade strike
    his old friends, a ghost's scare frightens them and IMMOLATE's fire
    burns them, and none of them blocks, strikes or frightens him or his
    new friends. A blow of one of them scores and counts for the charmer's
    team, and a post one of them strikes down in Onslaught flips to the
    charmer's colour. All of it turns back when the charm ends. A ring
    whose walker has died fights for the colour it was made with again
    (that a ring outlives its walker at all is an old fault of both games,
    #557). An ember IMMOLATE drops while its elemental is charmed wears the
    charmer's colour; one dropped before keeps the colour it was dropped
    with.
  - Everything else a walker made meets him by its colour once he is
    charmed: his poison cloud poisons him, he trips his own mine, the
    embers he dropped before the charm burn him while he stands on them,
    his war banner keeps rallying and healing the side it was planted for
    and stops rallying and healing him (and frightens him if he is weaker
    than it), his tree or bone wall blocks him and he attacks it when he
    bumps it, and a cleric is blocked by his own glows and swings at them.
  - A dying tent's four blasts halve on the tent's own side. In Team
    Deathmatch a red tent's blast that kills a red fighter is a red
    teamkill and takes the kill away from an enemy who had wounded him a
    moment before; one that kills a green fighter is a red frag.
  - A blast that kills its own charmed thrower, or a summon its own
    charmed caster, credits him no kill; the blow's own damage still
    scores for the colour the blast or the summon wears. Team Deathmatch,
    Onslaught and Mutant count that death as his suicide, no frag. Free
    For All never reads the killer, only the team the blow is stamped
    with: the blast wears the thrower's old band and he dies wearing the
    charmer's, so his old band gains a frag for the death of its own
    fighter (classic, the charmer's band loses one, the suicide shape).
  - In every mode a kill by a shot or a blast whose maker has died since
    counts for the colour it wears (in Team Deathmatch a kill of its own
    colour is a teamkill). An Onslaught post struck down flips to the
    colour the thing that struck it fights for, even when its maker has
    been charmed or has died since. A shot that touches a soccer ball or
    swats a basketball plays for its own colour, and a thing that rides a
    walker for his.
  The pairs `archer_charmed_arrow_in_flight_scen99`,
  `thief_bomb_charmed_thrower_scen99` and `thief_bomb_dead_thrower_scen99`,
  each with its setting-on twin, record it, and four setting-on rows move
  (see [Parity](#parity)).
- There is no fix BA: #505 needed no change to the game. A walker that
  dies charmed keeps heading its owner chain until the tick ends, so its
  summons' blows are credited to it for that tick (see AP above); with the
  setting on no side reads that climb (fix AZ), so no new letter was
  given.
- **BB.** A bot that re-plans with a foe within 100 px stops pathing,
  turns a notch toward the foe, starts an ATTACK order and calls for a
  swing along its heading, asking nothing (#497). The turn never writes the
  facing the bot is turning to, so that call turns a bot that did not face
  its foe straight back to the facing it had: it never completes its aim
  there. A bot that already faced its foe was turned off it by the classic
  turn and the call only turned it back; with fix AT, as the setting on
  played before this fix, it stays facing and the call swung or shot
  whatever stood between: at the air, into a wall, at a foe out of reach.
  With the setting on the turn and the swing agree. A bot not facing its
  foe keeps the foe's direction as the facing it turns to, so its next acts
  finish the turn, one notch an act, and the turn is its act (a tower faces
  it at once, fix BE). This does not
  make the bots aim faster: before, the ATTACK order snapped such a bot
  round on its next act, so a bot one notch off now reaches its first blow
  an act sooner and one three or four notches off an act or two later;
  what changes is that the turn is no longer undone. A bot facing its foe
  swings or shoots along that facing, and only when the blow can land: a
  bot that shoots asks its line check, and a melee-only bot, which that
  check always refuses, asks whether a foe's body stands on its weapon's
  pad. With the setting off the classic statements run in the classic
  order, so no setting-off golden moves; the rows it moves with the setting
  on are listed under [Parity](#parity), and [Bots](#bots) has the measure.
- **BC.** In its ATTACK order a bot whose shot is refused because it is
  melee-only (an orc, a ghost, a small slime, with its foe inside its
  weapon's reach) is only turned to face the foe (#447): for the order's
  30 to 54 acts it takes no step and starts no swing, at any distance
  inside that reach. With the setting on a bot that already faces its foe
  on its own floor swings when a foe's body stands on its weapon's pad,
  one blow per weapon pause while its magic lasts, and otherwise takes a
  step toward the foe. The guard-standoff fix stays for every other case:
  a shooter turned off its foe, the setting off, a bot that had to turn, a
  foe on another floor. With the setting off the classic statements run in
  the classic order, so no setting-off golden moves; the rows it moves
  with the setting on are listed under [Parity](#parity).
- **BD.** A tower or a shooting bot asks its line check before it fires,
  and the check moves its test weapon 0, 1, 3, 6, 10 ... steps out from
  the pad, each spot cut to a whole pixel, while the shot flies one step an
  act (#538; #72 reported it, #82 fixed it, and the rebaseline against the
  classic game put the classic loop back before the setting existed). So
  it steps over a foe that stands between two tested spots, and a tower
  stands silent or a bot walks closer for a shot it already had; it can
  also step over a one-tile wall and pass a shot that then breaks on it.
  With the setting on the check flies its test weapon the way the shot
  flies: the pad as `fire()` asks it, then the shot's own step (one
  function serves the shot and the check) once for each step a fresh shot
  moves, and it passes when the pad or a step meets a body the shot would
  strike. At an arrow slit the check takes the slit as open and the shot
  itself takes the slit's gamble, as the classic check did at most
  distances. What the check costs, measured on The Tryxian Chronicles'
  first level (six seeds of 2,400 ticks, after these fixes): its flights
  test 26.4 boxes a tick with the setting on, where the classic loop tests
  23.1 with it off; on a few levels (Tryxian 109, 114 and 115, Gladiator
  17, the Tower's floor 20) single ticks test far more on either setting,
  up to 2,838 boxes in one tick on Tryxian 114 with the setting on and
  1,790 there with it off. With the setting off the classic statements run
  in the classic order, so no setting-off golden moves; the rows it moves
  with the setting on are listed under [Parity](#parity).
- **BE.** A bot that sees a foe inside its sight box but cannot shoot it,
  and does not face it, turns a notch toward it (#513). The turn never
  records the facing the bot is turning to, so its very next act turns it
  straight back, and the search it queued starts an act late: bots twitch
  on the spot. With the setting on the turn is kept: the bot keeps its
  foe's direction as the facing it turns to, its next acts finish the turn,
  one notch an act, and then its search runs. A tower faces its foe at
  once, heading included: it has no walk to carry its aim, and a notch
  would leave the heading its line check and its shot fly along on the old
  line (#577). The turn is the one fix BB's near branch takes, so a
  tower within 100 px of a foe it does not face now faces it at once
  there too. A bot that already faces its foe is as before. With the
  setting off the classic statements run in the classic order, so no
  setting-off golden moves; the rows it moves with the setting on are
  listed under [Parity](#parity).
- **BF.** A bot attacks doors by three classic rules written for an enemy
  thing: a tower's or a shooting bot's line check passes on an enemy door
  its test weapon meets (on the clash roll, as every enemy weapon-order
  body is met, and a door that stops it passes as a body does), so a bot
  whose foe stands behind a door shoots into the door, which has 5,000 hit
  points; a walker that walks into an enemy door swings at it; and a blow
  makes whatever it lands on its owner's foe, a door included, so the bot
  then shoots at the door. With the setting on a bot leaves a door alone,
  as the maintainer asked on 2026-11-18 and 2026-11-19: an enemy door on
  the shot's path refuses the check, always, as a wall does (the test
  weapon meets the door without the clash roll and draws nothing for it,
  on the pad and on every step); a bot that walks into an enemy door
  starts no swing at it; and a blow that lands on a door does not make the
  door its owner's foe. A door that already is the bot's foe (a level
  script or a mod can name one, and Gameplay Fixes BL hands one to a bot
  that has no enemy it can get at) is fought as before: the check passes
  on it and the bump swings at it, and a blow on it names no foe but
  leaves it the foe, since nothing names another. So a bot attacks a door
  only when it is its foe, and Gameplay Fixes BL makes a door its foe only
  as a last resort. A player's own fighter still
  swings at a door the player walks it into. A bot is a walker that
  carries no seat number: a walker with one is a player's fighter to the
  rule, so the measuring crews of the tests, which seat their first four
  fighters while nobody drives them, still swing at a door with those four
  (#593). A grown tree, an enemy shot in flight and every other body are
  as before, and the shot itself still takes the clash roll at a door.
  These three rules are not all that strikes a door. What still does, for
  a bot: a special cast from a distance at a foe behind the door (no
  special's rule asks the path to its foe, #591), a CHARGE whose last step
  meets a door, a shot or a swing started at a foe that meets a door on
  its way, a pad that holds both the foe and a door, and a tent whose
  spawn pad lies on another side's door. Measured with the setting on, on
  Gladiator and The Tryxian Chronicles, bots landed 38,364 blows on doors
  before any door rule, 31,136 with the line check's part alone (a shot
  that landed on a door still made the door its shooter's foe then) and
  651 with all three parts and Gameplay Fixes BG and BH: the archer's FIRE
  ARROWS, whose eight fire arrows fly every way at once, 381, other
  specials 114, of them 101 cast from a distance at a foe, a shot or a
  swing started at a foe 140, a pad that held both the foe and a door 16,
  a CHARGE and a tent none; the measuring crews' seated fighters landed
  553 more. The doors' hit points lost went 1,801,741, 1,405,275 and
  46,309 (see [Balance](#balance)). With the setting off the classic
  statements run in the classic order, so no setting-off golden moves; the
  rows it moves with the setting on are listed under [Parity](#parity).
- **BG.** The guard of a cleric's MAGIC SHIELD, a soldier's BOOMERANG and
  an orc captain's HOOK BLADE destroys every enemy weapon-order body
  inside its ring, each costing it that body's damage in hit points, and
  passes by only the solid scenery a pack declares, a bone wall or a war
  banner (#472). The rule is meant for shots in flight, but a door and a
  grown tree are weapon-order bodies too, so a guard that passes an enemy
  door (5,000 hit points) or tree erases it at once, with no key and no
  blow (#588): on Gladiator and The Tryxian Chronicles 161 of the 181
  doors that were gone at tick 600 went that way. Shown that on
  2026-11-19, the maintainer answered: "why does boomerang and magic
  shield destroy doors? this seems like a bug". With the setting on the
  guard passes an enemy door and an enemy grown tree by, as it passes a
  bone wall or a war banner by, and pays nothing for them; every other
  body it meets is destroyed as before. The grown tree was added in this
  work, for him to overrule (the alternative is the door alone): it is the
  same bug at the same statement, and a guard destroys shots in flight
  while a thing that stands is cut down by blows. The three guards ask one
  question, `guard_passes_by` in `packs/core/lib/gameplay_fixes.lua`.
  Measured with the setting on, on Gladiator and The Tryxian Chronicles,
  doors a guard erased went 161 to 0 and grown trees 15 to 0 (144 and 20
  before any door rule); a guard still destroys an enemy's circle of
  protection (#592), and erased none in the measured battles, where no bot
  cast one. With the setting off the classic guard destroys both, so no
  setting-off golden moves; the rows that show it with the setting off and
  on are listed under [Parity](#parity).
- **BH.** Six casts fire a weapon from the caster's pad, and their five
  bot rules (fix AA; the barbarian's two throws share one) asked that pad
  the plain way: the druid's GROW TREE and SUMMON FAERIE, the barbarian's
  HURL BOULDER and EXPLODING BOULDER, the mage's ENERGY WAVE and the
  archer's EXPLODING BOLT. The plain question passes an enemy door, an
  enemy grown tree and an enemy shot in flight, which `fire()` meets on 4
  rolls in 10, so the cast was refused `COULD NOT FIRE` after the rule's
  yes and the bot paid its swing for nothing (#590). Once a guard passed
  doors by (Gameplay Fixes BG) more doors stood, and the test that plays
  The Tryxian Chronicles' first level counted 11 such refusals of GROW
  TREE against its cap of 3. With the setting on the five rules ask the
  strict question of fix AV (`G.pad_clear_of_weapons`), as the elf's
  volleys and BARRAGE do: any such body on the pad is a no, the clash roll
  taken as lost and nothing drawn, and a body of the caster's own side
  still passes. That test's count reads 0, and on Gladiator and The
  Tryxian Chronicles such refusals went from 47 to 0. For SUMMON FAERIE
  the faerie's room (fix AR) already said no to those bodies, so its
  answer does not change in stock play. The rule asks the pad, not the
  path: a cast from a few steps back at a foe behind a door or a wall is
  still made (#591). It reaches levels with no door too: a barbarian's
  throw now waits while an enemy shot in flight is on its pad, which moves
  the runs of Westlands, The Long Season, the Tower and Imaginations with
  the setting on (see [Balance](#balance)). With the setting off a bot has
  no such rule, so no setting-off golden moves; the rows that show it with
  the setting off and on are listed under [Parity](#parity) (SUMMON FAERIE
  has none, since its faerie's room gives the same answers).
- **BI.** The route finder priced a locked door like any body, ten a cell,
  and planned through it whenever going round cost more; the straight walk
  toward a foe or a point stepped into it; and an ally's push (fix X) sent
  a bot standing at a door into it again and again, each time taking its
  orders. The maintainer asked on 2026-11-19 why doors were treated
  differently from a wall, since standing next to them makes no sense.
  With the setting on a door a bot cannot open is a wall to it in those
  three places: its route is planned on a map of where its kind of fighter
  can stand with such doors shut (it goes round, takes no diagonal past a
  door's corner, starts from the cell its body overlaps most when its
  corner sits in a door's cell, ends beside a foe whose own cell a door
  closes, and a stair whose landing a door closes is no way up), a route
  that could only fail is not searched, the straight walk takes no step
  into such a door, and a pushed ally at one keeps its orders. A door the
  bot can open still costs ten a cell; a player's own fighter is not
  affected. A tower and a guard at its post are bots to this rule: a
  tower's route and straight walk only turn it, and a guard or a tower
  at such a door is not pushed into it. The ATTACK order's step, FOLLOW's
  step, the flight and the wall-following walk are not changed, so a bot
  whose foe is near can still push at a door as at a wall (#582). With
  the setting off every walker routes, walks and is pushed as in the
  classic game, so no setting-off golden moves.
- **BJ.** A bot took, held and walked at an enemy it could reach only
  through a door it cannot open: the foe searches answered the nearest
  enemy whatever stood between, a foe handed over by a blow or a script
  was held, the chase's near branch and a cast rule counted every enemy
  in range, and an ally took its leader's foe; with fix BI such a door is
  a wall to its route, so it pushed at the door or stood beside it. With
  the setting on a bot treats such an enemy as if it were not there: the
  searches pass it over (after its cloak roll), a held one is let go at
  the top of the bot's next SEARCH or ATTACK round, the near branch and
  the cast rules do not count it, a follower does not take it, and a bot
  with no foe that has such an enemy on its floor wanders no more: it is
  handed the door in its way (Gameplay Fixes BL), or stays where it is
  when there is none to hand. An
  enemy its shot lands on from where it stands (through an arrow slit) is
  kept only by the shot: the bot faces it and shoots without a step. When
  the door opens the enemy is an enemy again on the next tick. A foe
  behind a wall with no door in the way, a foe with a way round, a
  player's fighter, a tower and a guard at its post are as before. With
  the setting off no question is asked, so no setting-off golden moves.
- **BK.** Once its enemy was within 100 px (or anywhere on a map of more
  than 200 bodies) a bot's chase stepped straight at it, so a bot holding
  the key of the door between them pushed along the wall beside the door
  instead of walking into it. The maintainer asked on 2026-11-20 why a
  bot with a key opened its door one time in twenty. With the setting on
  a bot whose way to its foe only its key opens (it holds the key of a
  door on its floor, and a bot of its kind holding no key would have no
  way) plans its route as for a far foe and follows it; its step into the
  door's cell opens the door. The question is asked only where the near
  branch would be taken and draws nothing. Nobody fetches a key: a
  monster handed its room's key walks out as a teammate walks in. Nothing
  changes in the ATTACK order. With the setting off no question is asked,
  so no setting-off golden moves.
- **BL.** With Gameplay Fixes BI and BJ a bot whose only enemies stood
  behind a locked door held no foe and stood where it was, so two sides a
  door kept apart stood and waited. The maintainer ruled on 2026-11-24:
  "everyone should try to break down the door when there is no alternative
  (and stop when the alternative is pathable)", and "let's actually not
  treat the player's side special here". With the setting on a bot that
  has no enemy it can get at (on foot with the doors it cannot open shut,
  or by a shot that lands from where it stands), while an enemy on its
  floor is one only a locked door keeps it from, is handed the door in its
  way as its foe, by the world's hand-out after its act or by its own
  decision with no foe: of the doors beside the regions it gets to, the
  one through which the fewest doors lead to an enemy's region (two doors
  in a row count two; a stair costs nothing and leads one way), then the
  nearest, the topmost and the leftmost. A door that leads to no enemy is
  never handed. A bot that fights only hand to hand walks into it from any
  side and at any distance (its route planned on its first chase after the
  door is handed, never by the chase's near branch) and swings at it; a
  bot that shoots shoots at it from where its line check passes. The bot
  keeps the door while the door still leads to an enemy and it still has
  none it can get at (no flip to a better door), takes another door only
  when its own no longer leads anywhere, and lets the door go the moment
  it can get at an enemy, a key it is given included, the hand-out of the
  same tick giving it that enemy; the keep is asked at the top of its
  rounds and after every act, and of the door rules it alone lets a door
  go: the bot keeps its door through its random act, whose one-in-80 fresh
  search for a near foe (and its cloak rolls) is not made for it, while
  its draw of 80 is. A blow that passes through the door onto a fighter, a
  new attacker's blow, a friend's yell and a shooter's chase near its door
  still give it a new foe, as they give any bot one. Monsters shut in a
  room do this as much as the crew outside. A door keeps its 5,000 hit
  points, so it holds for a minute or two under blows. Nothing is stored:
  the held door is the bot's foe, which a snapshot carries. A player's
  fighter, a tower and a guard at its post are never handed a door. A
  swing or a shot at the door still passes through it on six rolls in ten,
  a crowd at a one-cell doorway strikes it one bot at a time, a swing can
  strike the door beside it, a fighter whose weapon is wider than the
  doorway (the cleric) lands nothing on a door set in a wall, a melee bot
  with no route to its door takes the old walk, a bot holding the key of a
  door behind a crowd of its own side pressed against that door is held
  off it, and a door a script named a bot's foe is let go like any other.
  With the setting off nothing is asked, so no setting-off golden moves.

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
| the elf's volleys, GROW TREE, SUMMON FAERIE, BARRAGE, EXPLODING BOLT, HURL BOULDER, EXPLODING BOULDER, ENERGY WAVE | `COULD NOT FIRE` | a wall face or the map edge on the weapon's pad, or a tree crown on a weapon that cannot cross the forest (free: nothing drawn, no swing or shot; fix AC, and fix AS for BARRAGE) |
| BARRAGE | `COULD NOT FIRE` | the first arrow cannot leave: a body on the pad takes its blow, or an enemy weapon there wins the clash roll (the one swing; the 60 is never charged; fix AS) |

`NO ROOM TO SUMMON` (SUMMON ELEMENTAL) and `COULD NOT FIRE` keep their
text. With the setting on `NO ROOM TO SUMMON` costs nothing extra. A pad
blocked by terrain costs nothing (AC); a body on the pad costs exactly the
one swing (H, I; BARRAGE, AS). A door, a grown tree or an enemy weapon in
flight on the pad is seen only by the strict pad question, which the bot
rule of every cast the table lists under `COULD NOT FIRE` asks (the elf's
volleys since fix AV, BARRAGE since fix AS, the others since fix BH);
`fire()`'s own probe passes such a body only six times in ten, so a cast
a player orders at one can still cost the swing.

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
  15 px plus 4 px per level: 19 px at level 1, never over 111; allies in it take
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
  body of the elf's own team passes, as the rock passes it). A door, a
  grown tree or a weapon in flight of the elf's foes is in the way too
  (fix AV): `fire()` meets one on 4 rolls in 10, a roll the rule may not
  draw, so the rule takes it as lost. The classic elf threw at nobody.
- **Archer.** FIRE ARROWS, the ring, wants at least two living foes inside
  the arrows' flight, or one the archer does not face. BARRAGE wants the
  archer's foe, a living one, faced and inside the flight, and the arrow's
  pad clear of terrain, bodies and enemy weapons (fix AS, with fix AV's
  question: the cast refuses when its first arrow cannot leave, and a rule
  may not draw the clash roll). EXPLODING BOLT
  wants that foe faced and inside the flight, with no ally of the archer,
  and not the archer, inside the blast around it, and the fire arrow's pad
  clear (fix AA: the bolt is a fire arrow, 9x9, so the pad is asked for it
  and not for the 7x7 arrow the archer holds; since fix BH an enemy door,
  grown tree or shot in flight on it is in the way too, as for the elf).
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
  clear (fix AA; since fix BH an enemy door, grown tree or shot in flight
  on it is in the way too, as for the elf). HEARTBURST wants a foe inside
  its own reach, 80 px plus 2 per level. A struck mage bot below its flee threshold teleports without
  asking a gate, as the classic one does; struck again while that teleport
  plays, it casts nothing (#408: the hit response asks `mid_teleport`
  first, as the TELEPORT gate does).
- **Archmage.** TELEPORT and the MARKER have the mage's rules, with
  HEARTBURST (80) as the special the halved pool must still cover.
  HEARTBURST or CHAIN LIGHTNING: the coin's choice is kept when it has a
  target, swapped when only the other has one, and the cast refused when
  neither has. Each reach is the cast's own: HEARTBURST 80 px plus 2 per
  level, CHAIN LIGHTNING 200 px plus 7 per level for a monster and 200 px
  plus half its Intelligence plus 2 per level for an archmage with a
  character record (fix AN: before, the rule measured every archmage's
  chain by level, so a hero the computer plays could be told "the chain has
  a target" and then refused for nothing by the cast). The chain counts
  only foes on the archmage's own floor, the only ones its bolt can strike
  first (#455); HEARTBURST counts a foe on any floor, since it bursts each
  foe on that foe's floor. SUMMON
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
  a second cast (the classic game summons first and then tries slot 2,
  fix AE). A player archmage's Shift stays the Shift key.
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
  bolt), and the bolt's pad clear (fix AA; since fix BH an enemy door,
  grown tree or shot in flight on it is in the way too, as for the elf).
  SUMMON FAERIE wants a living foe within 130 px, no faerie of the druid's
  alive, the pad clear the same strict way (fix BH) and room for the
  faerie where the bolt stands (fix AR: an ally on the spot
  passes the bolt, not the faerie; the question is asked with a faerie of
  the druid's, never the druid's own body).
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
  barbarian inside it. Both want the boulder's pad clear (fix AA; since
  fix BH an enemy door, grown tree or shot in flight on it is in the way
  too, as for the elf).
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
`G.can_pay` drops a Shift arm the bot cannot pay. `G.can_pay` reads the
price the engine charges (`walker:cast_cost`, `og::sim::cast_cost`): a
hidden or channelling walker's next press is free, and a slot New Specials
takes out of play is never paid (#437). With the setting on the
engine then checks the slot and arm the gate ended on against what the cast
will charge, and answers no when the bot cannot pay it. Only the hit
response can meet that no: the act rolls a special only when the bot has
slot 1's magic, and a slot it cannot pay is folded onto slot 1 before the
gate is asked.

Near its foe (fix BB). A bot that re-plans with a foe within 100 px stops
pathing, turns toward the foe and starts an ATTACK order. With the setting
on a bot that does not face the foe yet keeps turning toward it on its next
acts, one notch an act, and a bot that faces it starts a swing or a shot
there only when it can land: a shot asks the line check the ATTACK order
asks on every act, and a melee-only bot (the orc, the ghost, the small
slime), which that check always refuses, asks whether a foe's body stands on
its weapon's pad, the one place its swing can land. Measured over the
Westlands calibration (25 levels, three seeds, 600 ticks), per 100 ticks a
bot spends within 100 px of its foe on its floor, as swings started / blows
landed / damage / kills: melee-only bots 1.85 / 0.94 / 22.2 / 0.027 with the
setting off, 2.48 / 0.97 / 21.1 / 0.024 with it on before the fix and 1.67 /
0.98 / 21.9 / 0.031 after; the level's shooters 5.42 / 5.00 / 162.9 / 0.392,
5.48 / 4.73 / 154.0 / 0.379 and 5.21 / 4.78 / 154.8 / 0.370; the crew 8.15 /
8.11 / 100.8 / 0.340, 8.03 / 7.88 / 99.1 / 0.360 and 7.83 / 7.95 / 100.2 /
0.366. The swings the fix removes are the ones at the air: before it 1,559
of the 2,025 swings melee-only bots started there on Westlands were dropped
at a clear pad and 252 met terrain; after it they start 217 there, each with
a foe's body on the pad, and 181 land (30 found no body on the pad when the
blow fell, and for the other 6 the run recorded no outcome). These figures
were counted on a build of the fix with counters added, the flying bot's
change of #447 included; the game as shipped plays the four calibration
suites and the level-play test below exactly as that build did, row for row
and line for line.

Measured again after fixes BC, BD and BE (2026-11-17), with the flyer's
own-floor foe (#512) in, over the Westlands calibration (25 levels, twenty
seeds, 600 ticks), per 100 ticks a bot spends within 100 px of its foe on
its floor, as swings or shots started / blows landed / damage / kills:
melee-only bots 1.79 / 0.91 / 20.54 / 0.03 with the setting off, 1.69 /
0.98 / 21.66 / 0.03 with it on before these fixes and 3.11 / 2.20 / 45.39 /
0.08 after; the level's shooters 5.42 / 5.00 / 166.28 / 0.39, 5.23 / 4.79 /
154.76 / 0.37 and 5.31 / 4.87 / 163.37 / 0.40; the crew 8.10 / 8.08 /
102.12 / 0.36, 7.78 / 7.80 / 97.93 / 0.35 and 7.98 / 7.94 / 101.14 / 0.37.
On The Long Season the melee-only bots read 2.68 / 1.17 / 39.62 / 0.03,
2.29 / 1.20 / 39.12 / 0.03 and 4.20 / 2.82 / 79.56 / 0.13, and the level's
shooters 6.19 / 5.58 / 133.95 / 0.31, 6.02 / 5.49 / 131.75 / 0.32 and 6.19 /
5.57 / 133.56 / 0.33. Fix BC about doubles what the melee-only bots do; the
shooters and the crew move by a few percent. The setting-off figures are
the game after these fixes, where only #512 acts with the setting off
(before them the Westlands melee-only bots read 1.78 / 0.90 / 20.46 with
it off). These figures were counted on a build with counters added, and
every battle it counted, in the four calibration suites and in the
level-play test, plays exactly as the game's own, line for line; "before
these fixes" is master `d6d0741b7`, counted the same way. The fixes' own
counts are in their rows under [The fixes](#the-fixes).

What the gates still do not see:

- The soldier's empty hands. A soldier whose blades are all out has its
  throw vetoed by its own weapon hook, which the pad question cannot see.
  No soldier special fires a weapon, so no gate meets it today.

The list had a first point, a door, a grown tree or an enemy weapon in
flight on the pad of the druid's, barbarian's and mage's rules and the
archer's EXPLODING BOLT, which a weapon asked quietly passes and `fire()`
meets on 4 rolls in 10. Gameplay Fixes BH closes it: those rules ask the
strict question the elf's volleys and BARRAGE ask (AV, AS).

`tests/integration/test_level_play.cpp` (binary `og_test_level_play`) checks
the rules in a real level: it plays The Tryxian Chronicles' first level, its
104 bots on four warring teams plus a crew of 12 heroes at level 14, for 2400
ticks on six seeds, and counts every cast, every refusal and every roll a
gate holds. On the shipped default the bots cast about 490 times a seed (2915 on
the six seeds) and 52 different specials, among them the eight Shift arms the level
reaches (MYSTIC MACE 34, MINE 31, CHARM OPPONENT 30, TURN UNDEAD 23, CHAIN
LIGHTNING 20, HASTE SELF 13, TELEPORT MARKER 8, WAIL 4) and the crew's slot-5
HEARTBURST (the crew mages roll FREEZE TIME too, and its gate held all 28 rolls),
none at nobody (each special whose rule names a foe has
one within its reach, worked from the gate's own arithmetic), REVEAL never,
and no gate says yes to a cast that then refuses for the fact the gate
reads. With the setting off the same six seeds show 397 casts at
nobody, REVEAL 46 times and 375 such refusals, so the test
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

Fixes AR, AS and AV close those shapes, and #437 moves the New Specials kit
gates. Measured on the six seeds against the same tree with those fixes'
Lua taken out (on that tree the elves read 11 and the druid's `NO ROOM TO
SUMMON` 2, one each on seeds 42 and 2): the elves' `COULD NOT FIRE` reads
0, the druid's `NO ROOM TO SUMMON` after its rule's yes 0, and the four
families' `COULD NOT FIRE` 0, BARRAGE's new refusal included. The test caps
the elves' count and the druid's at 3 (a measured 0). The rules hold more
rolls (5416 to 5427): BARRAGE falls from 127 casts to 87, ROCKS from 87 to
82 and SUMMON FAERIE from 14 to 11, each still well above the floor the
test keeps for it. Measured again on the merged tree, with fix AT and the
rest of this batch in: every capped count still reads 0 (the elves', the
druid's and the rest), BARRAGE 65, ROCKS 84 and SUMMON FAERIE 23, and the
rules hold 5371 rolls; the caps stay at 3. Measured again with fix BB and
the flying bot's change of #447 in: the bots cast 584, 514, 484, 480, 471
and 467 times on seeds 42, 1337, 2025, 1, 2 and 3, BARRAGE 92, ROCKS 89
and SUMMON FAERIE 15, and the rules hold 5270 rolls. Every capped count
reads 0 but one: the druid, barbarian, mage and archer bots' `COULD NOT
FIRE` reads 1 (cap 3). It is a druid bot's GROW TREE on seed 2, tick 154,
whose bolt's pad held an enemy knife in flight: the gate's quiet question
passes a weapon in flight, and `fire()` met the knife and lost the weapon
clash roll to it, the shape Gameplay Fixes BH later closed (once the
first point under "What the gates still do not see" above). Measured
again with fixes BC, BD and BE and the flyer's
own-floor foe (#512) in: the bots cast 464, 418, 525, 445, 523 and 540
times on seeds 42, 1337, 2025, 1, 2 and 3, BARRAGE 69, ROCKS 79 and
SUMMON FAERIE 16, the rules hold 5516 rolls, and every capped count
reads 0. Measured again with Gameplay Fixes BF, BG and BH in (2026-11-19):
the druid, barbarian, mage and archer bots' `COULD NOT FIRE` read 2 with
BF (the mage's ENERGY WAVE) and 11 with BG (all the druid's GROW TREE, 3 with
the Shift key up and 8 with it down; the door half of BG alone moves them),
and 0 with BH; the rules hold 5763 rolls and the cap stays at 3.

## Charm and seats

- **A driven hero resists a charm** (issue #355, both games). An enemy
  thief's CHARM OPPONENT and an enemy archmage's MIND CONTROL skip a hero a
  player is driving, by the same rule POSSESS uses (`walker:seat_driven()`:
  a user tag, or the player-control act). A skipped hero costs the caster
  no roll and gives no free hit; with nobody else eligible the charm is
  refused as `NO VALID TARGET`.
- **A seat never claims a charmed walker** (issues #354 and #450, both
  games). The seat scans skip a walker whose charm timer is
  running and whose real team is recorded: it is on our team only because
  our side charmed it. Every scan a seat takes a body through asks this:
  the hand-off when the walker a seat drives dies, the entry claim, the
  server's bind claim and a networked join. Switch Character has refused
  such a walker since 2013 (it refuses any walker with a real team recorded).
  A freshly spawned walker (charm timer 0) is still claimable. One visible
  consequence: a team whose only bodies are walkers it charmed now ends the
  game instead of handing a seat a borrowed walker, and a player joining
  mid-level in a local game is given the stock soldier instead (in a
  networked game the seat follows the team). No parity row can reach it:
  both recordings claim control their own way (the first live walker of
  the player's team) and never run a seat's hand-off. #450 asked for this
  rule a second time; it is fix M's seat half, so no new letter is
  assigned.
- **A seat keeps the walker it drives when its charm runs out** (#450,
  both settings, both games). The walker goes back to its own side with
  the seat still on it: a seat looks for a new body only when it has none
  or its body is dead, and never asks a live body's team. This is the
  maintainer's ruling: refuse a charmed walker for the hand-off, keep one
  already controlled.
- **A summon of our colour is ours** (fix AZ, #496, #536). With the
  setting on a summon fights for the colour it wears, so a summon a
  charmed caster made for us is handed to a seat whose hero died like any
  walker of the team, and a seat on it can Switch Character back to its
  heroes. With the setting off the classic scans and the switch disagree
  about it, as before: the hand-off takes it, and Switch Character, which
  asks the heads of both owner chains, refuses it.

The old game's death searches (`viewscreen::input` and
`viewscreen::continuous_input`, three passes each) ask the same question
behind the setting since `787eb516` (#450). No parity capture runs them, so
the port moved no golden. Fix AY's question, ported into the first two
passes of the same searches as `3a21c890`, was taken out again when fix AZ
made it the colour test the scans already ask (`98d05c56` reverts
`3a21c890`); the third pass is #139's difference (see
GAMEPLAY_FIXES_FROM_CLASSIC.md).

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
  the setting on moved). The table mirrors `f8832c0b` and `180e03c4` (the
  mutation pins' lines after the semantics comments; no golden moved), then
  `8e296bf0`, the setting-off twin of the struck archmage arena, fix AE's
  proof (one new golden; no existing golden moved). Fixes AF to AN, AC
  for #476, #473 and #483: the table mirror `50edf40f` (fix AG's forest
  arena and #476's three edge-pad arenas, each with the setting off and
  on), then `d49fd762` (AF), `aa5b1aa5` (AG; its row with the setting on
  moved), `737270ea` (AH), `4a9802a3` (AI), `80c4249b` (AJ), `a3ea0248`
  (AK), `d0ee2ffd` (AL), `adbaa300` (AM), `fbcfb9db` (AN) and `805208eb`
  (AC for #476; its three rows with the setting on moved), and, not behind
  the setting, `d3d594e3` (#473: a team-0 walker with no player character
  and its shots take the difficulty) and `5a40b266` (#483: the bot gates'
  blast reach has no floor). Eight new goldens; no existing golden moved.
  Fixes AO to AV, M's seat half for #450 and the revert for #493: the table
  mirror `195a3a29` (the new arenas of AQ, AR, AS, AT and AV, each with the
  setting off and on; the fact readings of #448 and #449; #438's three
  re-staged arenas), then `8bf6f8e5` (AO), `e7fce82e` (AP), `442572be` (AQ;
  its row with the setting on moved), `9ac9e49f` (AR; its row with the
  setting on moved), `2efc30c6` (AV; its row with the setting on moved),
  `1f5e0181` (AS; its three rows with the setting on moved), `915a99ee`
  (AT; the 44 rows below and the setting-on rows of the new arenas of AS,
  AT and AV moved), `787eb516` (M's seat half for #450), then `43f30937`
  reverts `d3d594e3` (#493, not behind the setting), and the table mirror
  `d4fa405f` (the facts the recaptures broke, worked out again from the
  new goldens, and four setting-on rows on a seed of their own; those four
  moved). Seventeen new goldens and 43 recaptured; no setting-off golden
  moved. The table mirrors `b518f501` and `fbea813d` carried mutation
  pins the testing refresh moved; no golden moved. Fixes AZ, AW, AX, AY
  and BB: `bc75c886` (AZ, a blow's team score pays the team its chain head
  wears), `3f8080e1` (AW, an owned effect's blow pays its hero once),
  `9532f53d` (AX, a foe's summon takes the difficulty as its caster does),
  `3a21c890` (AY, a dead hero's seat is handed only a walker that fights
  for its side) and `9ad29b55` (BB, a bot near its foe turns to it for good
  and swings only when the blow can land; the 28 rows below moved), then
  `fbcab638`, which corrects the AZ port so that with the setting on a
  team outside the score table is paid nothing, as in this game (no
  golden moved), and the table mirrors `ae5c6c7c` (the facts BB's
  recaptures broke in sixteen rows, worked out again, and the archmage's
  MIND CONTROL berserk row with the setting on back on its twin's seed;
  that row moved), `35e5f6eb` (four rows' facts and texts corrected after
  review; nothing moved), `e31bb4ca` (the cleric bot's RESURRECT row with
  the setting on, on a seed of its own; that row moved), `65ef41ef` (one
  label reworded; nothing moved) and `30ada1c5` (the new arenas of AZ and
  BB, each with the setting off and on, captured there; two pins and two
  citations re-pointed by line, corrected comments, and the fact kind
  that #498 retired dropped from the old game's fact header; nothing else
  moved). Four new goldens and 28 recaptured; no setting-off golden moved.
  Then fix AZ as the maintainer ruled it (everything fights for the colour
  it wears, and a thing that rides a walker goes with him): the table
  mirror `9df7fc3a` first (the three new arenas below, each with the
  setting off and on, and the pins the rule moved; nothing else moved),
  then `98d05c56` (reverts AY's port `3a21c890`; nothing moved), `ac32063a`
  and `d4a87048` (revert the first AZ port's correction `fbcab638` and the
  first AZ port `bc75c886`, fix AW's lines kept; the colour-score row with
  the setting on moved, and the next commit put it back), `fbefdccc` (the
  stamps: the four rows below and the dead thrower's arena with the
  setting on moved) and `b1252282` (the readers and the rider mark: the
  other two new arenas with the setting on moved). Six new goldens and four
  recaptured; no setting-off golden moved at any step. Then the line check
  (BD), the kept turn (BE) and the ATTACK order (BC): the table mirror
  `977e870b` first (the three new arenas below, each with the setting off
  and on; nothing else moved), then `08b8ca26` (BD), `7cdbeab2` (BE) and
  `740af495` (BC), one per commit, each moving only rows with the setting
  on (the lists below), and the table mirror `941e2d2b` (the facts of the
  moved rows; nothing moved). Six new goldens and 53 recaptured; no
  setting-off golden moved at any step. Then the table mirror `e6663739`
  (fourteen setting-on rows given a fact of their own rule again, see
  [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1: twelve
  recaptured, no setting-off golden moved), the table mirror
  `46a09a28` (the fifteenth, `soldier_charge_ally`, given an arena of
  its own, the same entry: one recaptured, no setting-off golden moved),
  the table mirror `fb6473a0` (the same entry once more: one of the
  fifteen reads a band of its blows instead of one value, one fact that
  stated a fight's outcome is dropped, comments corrected; nothing
  moved), the table mirror `adfa676e` (the same entry after its second
  review: `orc_right_walk_potion` ends at tick 25 on its twin's seed in
  place of a seed of its own; one recaptured, no setting-off golden
  moved), the table mirror `7c33f42b` (the moved rows after their
  review: `thief_charm_nearest` takes an arena and a budget of its own,
  the same entry; two rows read the bands their rule gives and a third
  counts the heal's notice; one recaptured, no setting-off golden moved),
  the table mirror `608060fd` (the texts of four moved rows after the
  second review; nothing moved) and the table mirror `447be5aa` (the words
  of three rows; nothing moved). Then the door rule (BF): `2ed82025` (BF,
  one row with the setting on moved, below) and the table mirror
  `b4467150` (the rule's new arena, with the setting off and on; the
  setting-on twin of `soldier_bot_charge_door` in an arena and on a seed
  of its own, below; the words of two rows). Two new goldens and one
  recaptured; no setting-off golden moved. Then the table mirror
  `53976fa5` (the words of three pins of the pad rules, Gameplay Fixes BH;
  nothing moved). Then the bump and the blow of the door rule (BF) and the
  guard (BG): `d3755554` (BF) and `dff9b822` (BG), one per commit, neither
  moving a recording, and the table mirror `8c0cdcd1` (their four new
  arenas, each with the setting off and on, below; nothing else moved).
  Eight new goldens; no setting-off golden moved. Then the pad rules (BH):
  `5b973aa5` (BH, moving no recording), the table mirror `710489a6` (its
  four new arenas, each with the setting off and on, below; nothing else
  moved) and the table mirror `b822bb83` (the words of three of them;
  nothing moved). Eight new goldens; no setting-off golden moved. Then
  the door rules (BI, BJ, BK, BL), whose rows came before their ports:
  the table mirrors `39235067` (their seventeen pairs, below) and
  `8f0a3513` (two more pairs, `orc_bot_room_opens` and
  `archer_bot_random_act_keeps_door`, and the words of four rows;
  nothing else moved). 38 new goldens; no existing golden moved. Their
  nineteen setting-on twins were captured before the old game has the
  four door rules, so they record the old game's play with the setting
  on (its earlier ported fixes) without the door rules, and they differ
  from this game's run of the same row until the rules are ported.
  `8f0a3513` is the commit the goldens are captured from: 446 of 446
  goldens `cmp` equal to a full capture there, and every other capture
  there `cmp`s equal to this game's run of the same row.
  `tests/parity/golden/DRIFT_LEDGER.md` records each one.

The rows with the setting on, by fix:

| fix | rows (setting on) |
|---|---|
| K | `soldier_whirlwind_no_foe_fixes_scen99`, `thief_taunt_no_foe_fixes_scen99`, `ghost_scare_no_foe_fixes_scen99`, `orc_howl_no_foe_fixes_scen99` (the setting-on twin of `soldier_whirlwind_no_foe` has an arena and inputs of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| A | `soldier_disarm_higher_foe_fixes_scen99`, `soldier_disarm_resisted_price_fixes_scen99`, `soldier_disarm_resisted_busy_fixes_scen99` |
| J | `special_cleric_heal_ally_fixes_scen99` |
| E | `cleric_turn_undead_reach_fixes_scen99`, `cleric_turn_no_undead_fixes_scen99` |
| D | `archmage_summon_no_room_fixes_scen99` (the setting-on twin has an arena, inputs and a budget of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| F | `archmage_mind_control_berserk_fixes_scen99` (the setting-on twin has a seed of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| G | `thief_charm_nearest_fixes_scen99` (the setting-on twin has spawns and a budget of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| H | `elf_volley_refund_fixes_scen99`, `elf_rocks_blocked_fixes_scen99` (the setting-on twin of `elf_volley_refund` has spawns of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| I | `druid_tree_blocked_fixes_scen99` |
| B, C, L | `mage_bot_freeze_no_foe_fixes_scen99`, `mage_bot_freeze_alone_fixes_scen99`, `mage_bot_freeze_engaged_fixes_scen99`, `mage_bot_heartburst_fixes_scen99`, `archmage_bot_summon_no_foe_fixes_scen99`, `cleric_bot_raise_or_turn_fixes_scen99`, `thief_bot_charm_or_taunt_fixes_scen99`, `archmage_hit_stale_shift_fixes_scen99` (the first two in arenas of their own since fixes X to AD: see below; the setting-on twin of `mage_bot_heartburst` has a budget of its own and that of `thief_bot_charm_or_taunt` a seed of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| M | `thief_bot_charm_hero_fixes_scen99`, `archmage_bot_mind_control_hero_fixes_scen99` |
| Q | `archer_bot_bolt_at_blows_fixes_scen99`, `archer_bot_ring_one_foe_fixes_scen99`, `archmage_bot_image_one_foe_fixes_scen99`, `barbarian_bot_boulder_far_fixes_scen99`, `barbarian_bot_explode_lone_fixes_scen99`, `cleric_bot_mace_alone_fixes_scen99`, `cleric_bot_resurrect_foe_stain_fixes_scen99`, `druid_bot_faerie_alone_fixes_scen99`, `druid_bot_protection_idle_fixes_scen99`, `druid_bot_tree_behind_fixes_scen99`, `elemental_bot_starburst_one_foe_fixes_scen99`, `elf_bot_rocks_far_fixes_scen99`, `mage_bot_marker_home_fixes_scen99`, `mage_bot_warp_two_at_blows_fixes_scen99`, `soldier_bot_boomerang_far_fixes_scen99`, `soldier_bot_charge_unaimed_fixes_scen99`, `soldier_bot_disarm_front_fixes_scen99`, `soldier_bot_whirlwind_one_foe_fixes_scen99`, `thief_bot_bomb_ally_fixes_scen99`, `thief_bot_bomb_far_fixes_scen99`, `thief_bot_cloud_alone_fixes_scen99` (21 new arenas, each with its setting-off twin `<arena>_scen99`; the setting-on twin of `soldier_bot_charge_unaimed` has a budget of its own and that of `cleric_bot_resurrect_foe_stain` a seed of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| R | `archmage_bot_struck_fixes_scen99` (its setting-off twin, `archmage_bot_struck_scen99`, came with fix AE: see below) |
| T | `cleric_raise_blocked_corpse_fixes_scen99` (a new arena, with its setting-off twin; the setting-on twin has a seed of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| U | `soldier_charge_front_foe_fixes_scen99` (a new arena, with its setting-off twin) |
| V | `soldier_bot_charge_door_fixes_scen99`, `elf_bot_rocks_blocked_fixes_scen99` (two new arenas, each with its setting-off twin; the setting-on twin of `soldier_bot_charge_door` has spawns and a seed of its own since fix BF: see below) |
| W | `soldier_bot_refused_roll_acts_fixes_scen99` (a new arena, with its setting-off twin) |
| X | `orc_stale_shove_fixes_scen99` (a new arena, with its setting-off twin) |
| Y | `orc_right_walk_potion_fixes_scen99` (a new arena, with its setting-off twin), and the 13 recaptured rows below; the amendment (a melee-only bot bumps its own foe) recaptured `orc_right_walk_potion_fixes_scen99`, `mage_bot_freeze_engaged_fixes_scen99` and `elemental_struck_short_pool_fixes_scen99` again; the setting-on twin of `orc_right_walk_potion` has a budget of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1 |
| Z | `soldier_charge_ally_fixes_scen99` (a new arena, with its setting-off twin; the setting-on twin has spawns of its own, with inputs, a budget and a player team of its own: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1) |
| AA | `druid_bot_faerie_pad_body_fixes_scen99`, `barbarian_bot_boulder_pad_body_fixes_scen99`, `mage_bot_wave_pad_body_fixes_scen99`, `druid_bot_tree_pad_body_fixes_scen99` (four new arenas, each with its setting-off twin) |
| AB | `elemental_struck_short_pool_fixes_scen99` (a new arena, with its setting-off twin), and the 10 recaptured rows below |
| AC | `elf_rocks_edge_pad_fixes_scen99` (a new arena, with its setting-off twin); for #476, `archer_bolt_edge_pad_fixes_scen99`, `barbarian_boulder_edge_pad_fixes_scen99` and `mage_wave_edge_pad_fixes_scen99` (three new arenas, each with its setting-off twin); the setting-on twins of `elf_rocks_edge_pad` and `archer_bolt_edge_pad` have spawns of their own, the first inputs and a budget too: see [Rulings (2026-11-16)](#rulings-2026-11-16), entry 1 |
| AD | `thief_charm_tent_troop_fixes_scen99` (a new arena, with its setting-off twin; the setting-on twin has spawns of its own: see below) |
| AG | `elf_forest_haste_fixes_scen99` (a new arena, with its setting-off twin) |
| AQ | `player_slide_potion_fixes_scen99` (a new arena, with its setting-off twin) |
| AR | `druid_bot_faerie_room_fixes_scen99` (a new arena, with its setting-off twin) |
| AS | `archer_bot_barrage_pad_body_fixes_scen99`, `archer_barrage_edge_pad_fixes_scen99` and `archer_barrage_pad_body_fixes_scen99` (three new arenas, each with its setting-off twin) |
| AT | `orc_bot_faces_foe_fixes_scen99` (a new arena, with its setting-off twin), and the 44 rows below that it moves |
| AV | `elf_bot_rocks_tree_pad_fixes_scen99` (a new arena, with its setting-off twin) |
| AZ | `thief_bomb_colour_score_fixes_scen99` (with its setting-off twin); `archer_charmed_arrow_in_flight_fixes_scen99`, `thief_bomb_charmed_thrower_fixes_scen99` and `thief_bomb_dead_thrower_fixes_scen99` (three new arenas, each with its setting-off twin); and the four rows below that it moves |
| BB | `mage_bot_far_foe_fixes_scen99` (a new arena, with its setting-off twin), and the rows below that it moves |
| BC | `orc_attack_order_blows_fixes_scen99` (a new arena, with its setting-off twin), and the rows below that it moves |
| BD | `tower_foe_between_spots_fixes_scen99` (a new arena, with its setting-off twin), and the rows below that it moves |
| BE | `orc_refused_shot_turn_fixes_scen99` (a new arena, with its setting-off twin), and the rows below that it moves |
| BF | `tower_foe_behind_door_fixes_scen99` (a new arena, with its setting-off twin), and the row below that it moves; for the bump and the blow, `orc_bot_bumps_door_fixes_scen99` and `tower_blow_names_door_fixes_scen99` (two new arenas, each with its setting-off twin: see below) |
| BG | `cleric_shield_door_tree_fixes_scen99` and `soldier_boomerang_door_tree_fixes_scen99` (two new arenas, each with its setting-off twin: see below) |
| BH | `druid_bot_grow_tree_tree_pad_fixes_scen99`, `barbarian_bot_boulder_tree_pad_fixes_scen99`, `mage_bot_wave_door_pad_fixes_scen99` and `archer_bot_bolt_door_pad_fixes_scen99` (four new arenas, each with its setting-off twin: see below; SUMMON FAERIE has none) |
| BI | `orc_bot_route_door_fence_fixes_scen99` (a bot's route goes round a fence of doors it cannot open, where the classic route runs through it) and `orc_bots_shove_at_door_fence_fixes_scen99` (an ally pressed against the fence is not shoved along it), two new arenas, each with its setting-off twin |
| BJ | `orc_bot_foe_in_door_ring_fixes_scen99` (the foe search passes a ringed enemy over), `orc_bot_handed_foe_let_go_fixes_scen99` (a ringed enemy a knife handed the bot is let go at its next round), `orc_bot_near_ring_crowded_fixes_scen99` (on a crowded map the near branch counts no ringed enemy), `archer_bot_shot_past_door_fence_fixes_scen99` (an archer shut in a pen holds its foe by a shot through a gap and takes no step), `soldier_bot_outside_ring_fixes_scen99` (the shot term reads a soldier's reach without its weapon hook) `archer_bot_ring_no_cast_fixes_scen99` (the ring rule of FIRE ARROWS counts no ringed enemy) and `orc_bot_room_opens_fixes_scen99` (a bot that held the door in its way takes the enemy in the room in the tick a key opens the room's door, acting after the key holder), seven new arenas, each with its setting-off twin |
| BK | `orc_bot_keyed_at_wall_fixes_scen99` (a bot holding the key of the one door its key opens walks round the ring to it instead of pushing at the near doors), a new arena with its setting-off twin; and `orc_bot_keyed_door_scen99` with `orc_bot_keyed_door_fixes_scen99`, the key test the door rules share, whose fact (a door the key holder's step meets opens) holds on both settings |
| BL | `orc_bot_breaks_ring_door_fixes_scen99` (a bot that gets at no enemy is handed the ring's door and strikes it), `orc_bot_lets_door_go_fixes_scen99` (it lets the door go when an enemy walks out of its pen), `orc_bot_two_rings_fixes_scen99` (the door behind which an enemy is reached through the fewest doors is handed, not the nearer one), and for the walk of a melee bot into the door it holds, `orc_bot_routes_to_held_door_fixes_scen99` (it plans its route into the door within 100 px), `orc_bot_handed_door_replans_fixes_scen99` (it re-plans at once when a door is handed), `orc_bot_held_door_start_cell_fixes_scen99` and `orc_bot_hops_into_held_door_fixes_scen99` (from beside the door, its corner inside the door's cell, it steps from its start cell into the door), and `archer_bot_random_act_keeps_door_fixes_scen99` (a bot that shoots keeps the door it holds through its random act's one-in-80 and starts its shot at the door on that act), eight new arenas, each with its setting-off twin |

Fix AE has no row with the setting on (with it on a struck bot answers by
fix R, and no row reaches a seat-driven archmage's hit response); its proof
is a row with the setting off, `archmage_bot_struck_scen99`, the struck
archmage arena of `archmage_bot_struck_fixes_scen99`. Until the fix the
struck arena had no setting-off twin, because the two games' classic hit
responses differed: the old game's `walker::special` always answers 0, so
after a summon its hit response never returned and its "then leave! :)"
second cast never happened, while this game's used the cast's real answer.
Since the fix the old game's capture, at `8e296bf0`, is byte-identical to
this game's recording. The classic reply summons an image (a skeleton
phantom) with the Shift the coin left, goes on to the slot-2 coin, which
falls on CHAIN LIGHTNING, and the summon's busy refuses the chain and the
HEARTBURST attempt after it. That coin is one draw more than this game drew
before the fix, so the archmage's blow on the orc rolls its damage one draw
later (8, not 7): the row's mutation puts back the old read and its orc-hp
fact fails.

Fix AG's arena, `elf_forest_haste_scen99` and `elf_forest_haste_fixes_scen99`:
a level-10 player elf (no forest penalty) stands on a level-3 speed potion in
scen1's forest and walks down and left for six steps. With the setting off
its step grows by the bonus every act, 4, 7, 10, 13, 16 and 19 px, and it
ends 69 px down and left of its spawn; with the setting on it steps 4 and
then 7 px, 39 px each way. The walk is a diagonal, so each row's one position fact is exact:
a shorter walk fails the y bound and a longer one the x bound. Measured in
the current game, the fix moves no existing row on either value: no row
hastens a forestwalker, and the setting-on rows' elves stand on grass (the
bots among them are level 1, whose forest step is 1 px both ways).

Fix AI has no new row: every FREEZE TIME row plays a red player and a mage
that is red or not a hero, where both values of the setting cast the same
form (`special_mage_3_scen99`, `input_special_switch_wrap_scen99`,
`enemy_freeze_mage_scen99`, `mage_freeze_time_offteam_scen99` and the mage
bot freeze rows), so every golden is unchanged. The semantic tests pin the
two cases where the tests differ, on both values.

Fix AJ has no row: every exit row plays scen99, which is not a level the
player may leave at any time, so both values ask the same question.

Fix AK has no row: the key rows record a notification's text and not whose
view it goes to, so both values record the same bytes.

Fix AM has no row: no cloud row wedges its cloud (`effect_cloud_emission`,
`effect_poison_cloud_emit`, `effect_cloud_poison`, `thief_bot_cloud_alone`
and its twin with the setting on), so both values draw the same stream.

Fix AO has no row: the fix changes only a dead body's orders, which no
recording holds (the dump records a walker's place, hit points, team and
life); a dead walker never acts and every revival clears its orders, and
the strike and its damage roll stay, so an arena's two captures would be
byte-identical. `GameplayFixesWalk` in `tests/unit/test_gameplay_fixes_walk.cpp`
pins it, with a corpse and with a door the rush's own step opens.

Fix AP has no row: with the setting on friendliness reads each walker's
own colour (fix AZ), and the climb through a dead owner now decides only
the score gate's hero, the experience and the kill credit, which no
recording carries (a parity spawn has no character record). The unit tests
`GameplayFixesChain` (`tests/unit/test_gameplay_fixes_charm.cpp`) and
`ModesMutant.the_damage_gate_reads_the_games_owner_climb` pin it.

Fix AQ's arena, `player_slide_potion_scen99` and
`player_slide_potion_fixes_scen99`: the player soldier walks up-right into
a friendly TOWER1 whose bottom row lies on its top row, so the full and the
baby step are stopped and the player slides right along the tower. A magic
potion stands at the player's top-left, where only the first round's half
up (the half the slide never takes) meets it, one 32 px column before the
tower, so that probe reaches it first. With the setting off the probe eats
it; with the setting on the half is asked quietly, the tower answers, and
the potion stands to the end. The scene does not need terrain: every
blocker is a body, because a half that terrain blocks never reaches the
bodies on either value.

Fix AT's arena, `orc_bot_faces_foe_scen99` and `orc_bot_faces_foe_fixes_scen99`:
a hostile level-1 orc bot spawns facing down at the idle player soldier
straight below it, 19 px between their boxes. Its first act is a HOWL, its
second act_random, whose refused shot asks the orc to turn to the facing it
has: with the setting off it steps a notch and spends its next act turning
back, with it on it stays. Its path counter starts at 7, so its next six
chase calls are walks of 3 px that bring it to 1 px from the soldier
without a step that bumps, and the seventh re-plans: the foe is within
100 px, so the near branch turns the orc to the facing it has again and
calls init_fire. With the setting off the orc steps a notch and init_fire
only turns it back, and the ATTACK it queued holds it facing the soldier
for at least 30 rounds, so no blow can land before tick 41. With the
setting on it keeps facing, init_fire starts the swing on tick 9, and the
three-frame attack animation ends on tick 11, when fire() finds the
soldier on its pad: one blow, and the recording ends there (B = 11). The
blow is the orc's own 23 damage rolled as 23 - sqrt(23)/2 + rng(4), and
the spawned soldier has no armor (only `set_difficulty` gives a placed
walker armor, and the recording never calls it), so the soldier ends at
96 to 99 of its 120 hp with the setting on and at 120 with it off.
Classic, the orc's next swing waits for its own 30-round ATTACK; with the
setting on the ATTACK order (Gameplay Fixes BC) swings again once the
weapon's pause is over, past B either way (`orc_attack_order_blows` runs
on to those blows). The pin inverts the gate: the classic row then lands
the blow and the fixed row does not.

Fix AT moves these 44 existing rows with the setting on, as measured in the
current game before the port; the old game's capture after `915a99ee` moves
exactly these 44 (and the setting-on rows of the new arenas of AS, AT and
AV), and each is recaptured from the old game after it: `archer_bolt_edge_pad`, `archer_bot_bolt_at_blows`,
`archer_bot_ring_one_foe`, `archmage_bot_image_one_foe`,
`archmage_bot_struck`, `archmage_hit_stale_shift`,
`archmage_mind_control_berserk`, `archmage_summon_no_room`,
`barbarian_bot_boulder_pad_body`, `barbarian_bot_explode_lone`,
`barbarian_boulder_edge_pad`, `cleric_bot_raise_or_turn`,
`cleric_bot_resurrect_foe_stain`, `cleric_raise_blocked_corpse`,
`cleric_turn_no_undead`, `druid_bot_faerie_pad_body`,
`druid_bot_tree_behind`, `druid_bot_tree_pad_body`, `druid_tree_blocked`,
`elemental_bot_starburst_one_foe`, `elemental_struck_short_pool`,
`elf_bot_rocks_far`, `elf_rocks_blocked`, `elf_rocks_edge_pad`,
`elf_volley_refund`, `mage_bot_freeze_engaged`, `mage_bot_heartburst`,
`mage_bot_warp_two_at_blows`, `mage_wave_edge_pad`,
`orc_right_walk_potion`, `orc_stale_shove`, `soldier_bot_boomerang_far`,
`soldier_bot_charge_door`, `soldier_bot_charge_unaimed`,
`soldier_bot_disarm_front`, `soldier_bot_whirlwind_one_foe`,
`soldier_charge_front_foe`, `soldier_disarm_higher_foe`,
`soldier_whirlwind_no_foe`, `special_cleric_heal_ally`,
`thief_bot_bomb_ally`, `thief_bot_charm_hero`, `thief_bot_charm_or_taunt`
and `thief_charm_nearest` (each `_fixes_scen99`). In 26 of them the
first difference is an event that now comes sooner (in 20, one tick
sooner): a bot that faces its foe no longer spends an act turning off it
and back. In six only the random stream at the end differs. The facts of
18 of them no longer held and were worked out again from the new goldens;
one more, `soldier_bot_whirlwind_one_foe`, kept its facts, but its pin no
longer flipped one (the spin at one foe now kills the tower too, only
later), so it gained the fact the pin moves: the bot's hp. In four of them
the recording no longer reached the fix the row is about, so their pins
changed nothing, and the setting-on twin takes a seed of its own, found
as before by asking for the first seed where the row's own pin changes the
outcome: `archmage_mind_control_berserk` (176: on the old seed the control
roll succeeded, so fix F's berserk colour was never drawn),
`cleric_raise_blocked_corpse` (20: the faerie died beyond the corpse
finder's reach), `thief_bot_charm_or_taunt` (107: the bot rolled no slot 3
in the run) and `cleric_bot_resurrect_foe_stain` (3: the cleric never cast
near the stain). The table mirror `d4fa405f` carries the seeds; the
capture there moves those four rows alone, and each `cmp`s equal to the
current game's dump. After fix BB two of them took another seed (Rulings
(2026-11-14), entry 13): the archmage row is back on its twin's 198 and
the cleric row is on 84. One of the 44, `archer_bot_bolt_at_blows`, was also
re-staged for #438 (the archer one pixel lower), so its pair is captured
anew rather than recaptured. No setting-off golden moves: every
setting-off capture is byte-identical before and after `915a99ee`.

Fix AW has no row: experience is not in the recording. `SemWalker3`
(`tests/unit/test_sem_walker_3.cpp`) pins it with a bomb and with a bare
owned effect, each against the hero's own melee blow of the same damage.
The old game's capture after its port (`3f8080e1`) is byte-identical to
the one before it.

Fix AX has no row: every recording plays at Normal (the current game's
parity runner never sets a difficulty, which reads as 100 %, and the old
game's capture tool keeps `current_difficulty = 1`; `ScenarioSpec` has no
difficulty field), and at 100 % the percent multiplies a summon's
whole-number hit points, magic and damage by 100 and divides them back
exactly, so both settings record the same bytes. The parity entries were
run with the fix and no row moved, and the old game's capture after its
port (`9532f53d`) is byte-identical to the one before it. `DifficultyScaling` in
`tests/unit/test_difficulty_scaling.cpp` pins it on both settings at Easy
and Hard.

Fix AZ's arena, `thief_bomb_colour_score_scen99` and
`thief_bomb_colour_score_fixes_scen99`: the bomb arena of
`effect_bomb_bystander_scen99` with other colours. The player thief stands
on team 1 (the row's player team) and the stationary level-1 tower on team
2. Classic, the thief's bomb and the blast it leaves wear no colour, team
0; with the setting on the bomb takes the thief's colour when it is
dropped and the blast the bomb's. The thief stands on the player's team,
so the score gate opens and the blast's blow on the tower scores its
damage plus the tower's level for the colour the blast wears: with the
setting off red is paid it, with the setting on team 1, and the other team
nothing. With the setting off the blow takes 90 to 92 of the tower's 130
hp (the bystander row's tower fact) and pays 91 to 93. With the setting on
the tower strikes the thief sooner: a tick sooner with fix AT and two more
with the kept turn (Gameplay Fixes BE), its blows falling every five ticks
from tick 2 where the classic tower's fall from tick 5. So the blast's
damage roll is another draw of the stream, and that row's bands are every
value the roll allows: 85 to 93 hp, 86 to 94 paid. The blast's quarter on
the thief is a blow on its own side, and the tower's blows (the thief's
body holds its pad, so it looses no arrow) have a team-2 head that is no
hero, so neither scores. The setting-off row's pin moves the classic
payment to the chain head's team; the setting-on row's pin leaves the
blast red, so it pays nobody. Both rows were captured from the old game at
`30ada1c5`, after the first port of AZ (`bc75c886`, corrected by
`fbcab638`); the rule as it stands pays the same team on both rows, so
both still `cmp` equal to this game's run. The rule's port kept them:
taking the first port out (`d4a87048`) left the setting-on row's blast red
for one commit, and the stamps (`fbefdccc`) gave it the thief's colour
back, byte for byte the recording. The kept turn's port (`7cdbeab2`) moved
the setting-on row's blows, and it was captured from the old game again
after that port.

Fix AZ's arrow arena, `archer_charmed_arrow_in_flight_scen99` and
`archer_charmed_arrow_in_flight_fixes_scen99`: a hostile level-1 archer
bot stands on open ground 80 px above a level-20 player thief, and a far
tower of the archer's holds the level open. From tick 6 the archer looses
its FIRE ARROWS ring, on the same ticks on both settings, and the arrow
aimed straight down at the thief leaves on tick 13; with nothing in its
way it strikes him on tick 18. On tick 15 the thief charms the archer.
Classic, the arrow in the air follows the charmed archer onto the thief's
side and passes him, so he ends whole. With the setting on it keeps the
colour it was shot in and strikes him once, for 6 or 7 (the fire arrow's 7
rolled against no armour: only the difficulty gives a placed walker
armour, and the parity runner never applies it). No fact can name one
arrow among several (#552), so the classic row cannot read its own premise,
an arrow aimed at the thief in the air when the charm lands: its pin
carries it, and so does the setting-on twin on the same inputs and ticks,
whose thief takes exactly that arrow's blow. The setting-off row's pin
stops the charm, so the arrow and the later ones strike the thief; the
setting-on row's makes friendliness climb the owner chain again. Both rows
are captures from the old game at `b1252282`: the setting-off one played
the same before the port, and the setting-on one took this game's answer
with the readers' port, `b1252282` (the thief ends at 69 hp).

Fix AZ's charmed-thrower arena, `thief_bomb_charmed_thrower_scen99` and
`thief_bomb_charmed_thrower_fixes_scen99`: a hostile level-3 thief bot
drops its bomb point blank beside a level-20 player thief, on tick 1 with
the setting on and on tick 2 classic (on its first act the bot has no foe
yet, and with no foe the classic gate bombs only with three foes within
110 px, while the setting-on gate asks for a foe inside the blast), and the
player charms it on tick 6. Classic, the blast follows
the charmed bot and halves on the player: 27 to 31 of his 75. With the
setting on it wears the colour the bomb was dropped in, team 1's, and
strikes him in full: 56 to 62. The setting-off row's pin stops the charm;
the setting-on row's leaves the bomb red, so its blast halves on the
player again. Both rows are captures from the old game at `b1252282`: the
setting-off one played the same before the port, and the setting-on one
took this game's answer with the readers' port, `b1252282` (the player
ends at 13 hp).

Fix AZ's dead-thrower arena (#533), `thief_bomb_dead_thrower_scen99` and
`thief_bomb_dead_thrower_fixes_scen99`: a hostile level-1 thief bot drops
its bomb beside a team-2 tower, and the player's level-7 archer kills the
thief with one arrow before the bomb goes off. A dead thrower's bomb owns
itself, so its blast reaches only 19 px (the bomb's own level) and strikes
the tower alone, in full on both settings. Classic, the blast is red, the
player's colour, so it pays the player's team its blow on the tower beside
the arrow's blow and kill: 205 to 225, in two score changes. With the
setting on it wears the thief's colour and pays nobody: 177 to 193, in
one. The classic row shows its own premise: with a live thrower the head
of the blast's chain would be the thief, whose team is not the player's,
and the blast would pay nobody on either setting. The setting-on row's
score reads only the arrow, so its tower's hit points (99 to 103 of 130:
the blast's full blow and nothing else) show that the blast went off; no
fact reads that the thief died first (no fact reads an order of ticks,
#552), which rests on the staging it shares with the classic row. The
setting-off row's pin stamps the bomb with the thief's colour classic too;
the setting-on row's leaves it red. Both rows are captures from the old
game at `b1252282`: the setting-off one played the same before the port,
and the setting-on one took this game's answer with the stamps' port,
`fbefdccc` (the score went from 218 to 187).

No pair shows a summon that stays on its team when its caster is charmed:
every summon a bot casts on a fixed seed fights with blows whose count
after a charm is a run of bot rolls (the druid's faerie freezes with
sprinkles of 1 damage, and the archmage's image is refused with the
setting on unless two foes hunt the archmage), so a fact would be a number
read off one run; the unit tests pin it. Nor can a row show a dead
thrower's blast halving on his own side: with the setting on a living of
the thrower's colour inside the blast stops the bot from dropping the bomb
at all. Nor does any row play a walker that is charmed while a ring, a
mace or a boomerang sits on him: with the old game's rider rule taken out
all 384 captures stay byte-identical, so that part rests on the unit tests
here and on reading there (#558 asks for such a pair). Kits, possession,
the modes and level scripts are not in the old game.

Fix AZ moves four existing rows with the setting on, as measured in the
current game before the port (each `_fixes_scen99`). In each only the team
of one thing a walker made changes, from 0 to 1, and with it the walkers'
order in the dump, which follows the team: `thief_bot_bomb_ally` (the
thief's bomb), and `soldier_bot_charge_door`,
`soldier_bot_whirlwind_one_foe` and `cleric_turn_no_undead` (a soldier's
returning knife). Every fact of the four holds. The old game's stamps
(`fbefdccc`) move exactly these four the same way, and each is recaptured
from the old game at `b1252282`, byte-identical to this game's run. No
setting-off row moves.

Fix BB's arena, `mage_bot_far_foe_scen99` and
`mage_bot_far_foe_fixes_scen99`: a hostile level-1 mage bot spawns facing
down at the idle player soldier straight below it, 86 px between their
boxes. Its only special, TELEPORT, is held while one to three foes (one in
this arena) stand within 110 px, on both settings, and its fireball, shot
straight, reaches a foe 64 px off (the fireball's step 6 and range 7, scaled
for a straight shot to 8 and 8). Its first act rolls TELEPORT and the rule
holds it; with the setting off that roll is the act, with it on (fix W) the
act goes on and starts the hunt, so the classic mage runs one act behind
from there. Its path counter starts at 7, so six throttled walks of 2 px
bring it to 90 px from the soldier and the seventh call re-plans, on tick 9
with the setting off and tick 8 with it on: the foe is within 100 px, so the
near branch turns the mage to the facing it has, starts an ATTACK order and
asks for a swing. With the setting off the turn steps a notch and the swing
only turns it back; with it on the mage keeps facing (fix AT) and asks its
line check first, which answers that the soldier is beyond the fireball's
64 px, so it throws nothing. Before the fix the setting-on game threw a
fireball at the air there. The ATTACK order then walks the mage on, 2 px an
act, so at the end of the recording (B = 11) the classic mage stands 16 px
down and the fixed one 18 px, no fireball flies and the soldier is whole. B
is the tick an unasked swing at the re-plan has put its fireball in the air
on both rows (tick 10 with the setting on, tick 11 off). Each row's pin puts
the unasked swing back: the setting-on row's makes the near branch's line
check answer yes, the setting-off row's inverts fix AT's gate so the classic
mage keeps facing and its swing throws; either way a fireball flies by B.
The two rows say the same thing on purpose: with fix BB the setting-on game
does here what the classic game does.
Both rows are captured from the old game at `30ada1c5`, after the port
(`9ad29b55`), and each `cmp`s equal to this game's run.

Fix BB moved these 28 existing rows with the setting on, as measured in the
current game before the port (each `_fixes_scen99`); the old game's capture
after the port, `9ad29b55`, moved exactly these and no other row, and each
was recaptured from the old game. The facts of 17 no longer held:
`archer_barrage_edge_pad`, `archer_bolt_edge_pad`, `archmage_mind_control_berserk`,
`archmage_summon_no_room`, `barbarian_boulder_edge_pad`,
`cleric_bot_raise_or_turn`, `druid_tree_blocked`,
`elemental_struck_short_pool`, `elf_rocks_blocked`, `elf_rocks_edge_pad`,
`elf_volley_refund`, `mage_bot_heartburst`, `mage_wave_edge_pad`,
`orc_stale_shove`, `soldier_disarm_resisted_busy`,
`soldier_whirlwind_no_foe` and `thief_charm_nearest`. In the other 11 only
the bytes differed: `archer_bot_bolt_at_blows`, `archmage_bot_image_one_foe`,
`archmage_hit_stale_shift`, `cleric_bot_resurrect_foe_stain`,
`mage_bot_freeze_engaged`, `orc_right_walk_potion`,
`soldier_charge_front_foe`, `soldier_disarm_higher_foe`,
`soldier_disarm_resisted_price`, `thief_bot_charm_or_taunt` and
`thief_charm_tent_troop`. Sixteen of the 17 had their facts worked out
again from the rules after the recapture, never read off the new run (the
table mirror `ae5c6c7c` carries them, and `35e5f6eb` the review's
corrections). The seventeenth, `archmage_mind_control_berserk`, and one of
the 11, `cleric_bot_resurrect_foe_stain`, no longer reached the rule they
test, so their pins changed nothing: each setting-on twin took another
seed while its setting-off twin stayed as it is (Rulings (2026-11-14),
entry 13), and was recaptured from the old game on it.
`orc_bot_faces_foe_fixes_scen99`, fix AT's own row,
does not move: the orc stands 1 px above the soldier, the pad holds the
soldier's body and the swing starts on tick 9 as before. No setting-off row
moves.

Fix BD moved these 25 existing rows with the setting on (each `_fixes_scen99`),
as the old game's capture after `08b8ca26` shows: `archer_barrage_edge_pad`, `archer_bolt_edge_pad`, `archer_bot_ring_one_foe`, `archmage_bot_image_one_foe`, `archmage_bot_mind_control_hero`, `archmage_summon_no_room`, `barbarian_bot_boulder_far`, `barbarian_bot_explode_lone`, `barbarian_boulder_edge_pad`, `cleric_bot_raise_or_turn`, `cleric_bot_resurrect_foe_stain`, `druid_bot_tree_behind`, `elf_bot_rocks_far`, `elf_rocks_edge_pad`, `elf_volley_refund`, `mage_bot_freeze_engaged`, `mage_bot_heartburst`, `mage_wave_edge_pad`, `orc_stale_shove`, `soldier_bot_boomerang_far`, `soldier_bot_charge_door`, `soldier_whirlwind_no_foe`, `thief_bomb_dead_thrower`, `thief_bot_bomb_ally`, `thief_bot_bomb_far`. Fix BE then moved these 42:
`archer_barrage_edge_pad`, `archer_bolt_edge_pad`, `archer_bot_barrage_pad_body`, `archer_bot_bolt_at_blows`, `archer_bot_ring_one_foe`, `archmage_bot_mind_control_hero`, `archmage_hit_stale_shift`, `archmage_summon_no_room`, `barbarian_bot_boulder_pad_body`, `barbarian_boulder_edge_pad`, `cleric_bot_raise_or_turn`, `cleric_bot_resurrect_foe_stain`, `cleric_raise_blocked_corpse`, `druid_bot_faerie_pad_body`, `druid_bot_faerie_room`, `druid_bot_tree_pad_body`, `druid_tree_blocked`, `elemental_bot_starburst_one_foe`, `elemental_struck_short_pool`, `elf_bot_rocks_blocked`, `elf_bot_rocks_far`, `elf_bot_rocks_tree_pad`, `elf_rocks_blocked`, `elf_rocks_edge_pad`, `mage_bot_freeze_engaged`, `mage_bot_heartburst`, `mage_bot_warp_two_at_blows`, `mage_bot_wave_pad_body`, `mage_wave_edge_pad`, `orc_stale_shove`, `soldier_bot_boomerang_far`, `soldier_bot_charge_unaimed`, `soldier_charge_ally`, `soldier_charge_front_foe`, `soldier_disarm_higher_foe`, `soldier_disarm_resisted_busy`, `soldier_disarm_resisted_price`, `special_cleric_heal_ally`, `thief_bomb_colour_score`, `thief_bot_bomb_ally`, `thief_bot_charm_or_taunt`, `thief_charm_nearest`. Fix BC then moved these 6: `archmage_mind_control_berserk`, `elemental_struck_short_pool`, `mage_bot_freeze_engaged`, `orc_right_walk_potion`, `thief_bot_bomb_ally`, `thief_charm_nearest`. Each was recaptured from the old game at
`941e2d2b`, byte-identical to this game's run, together with the setting-on rows of the three new arenas. In
most of them a tower now faces its foe on its first act (BE) and its flown check (BD) passes or not on its own
sideways waver draw, so the tower's first shot and every shot after it now hang on that draw. The facts of 23
rows no longer held. Four were worked out again from the rules as bands whose count the rule fixes
(`druid_tree_blocked`, `elf_rocks_blocked`, `soldier_charge_front_foe`, and `soldier_disarm_resisted_busy`'s
sound count), `archer_barrage_edge_pad` as a band of one to three strikes (an arrow of the tower's in the air can
take one or two of BARRAGE's three), one now asserts the cap itself beside the heal's one notice
(`special_cleric_heal_ally`), and the rest were dropped where the value hangs on a line check's waver draw or a
fight whose stream the ports parted; each changed array of the table says which. At that capture the staging of `archmage_mind_control_berserk`,
`cleric_bot_resurrect_foe_stain`, `cleric_raise_blocked_corpse`, `orc_right_walk_potion`, `soldier_whirlwind_no_foe`
and `thief_bot_charm_or_taunt` no longer reached the rule the row was written for. No setting-off row
moves. The fifteen rows whose own pin then changed no fact were each given a fact of their own rule again,
with an arena, inputs, a budget or a seed of their own or a worked-out reading of the fixed stream: the twelve
whose run changed first were captured again at `e6663739` (`orc_right_walk_potion` once more at `adfa676e`, on
its twin's seed with a budget of 25 ticks), and `soldier_charge_ally`, given an arena of its own
last, at `46a09a28` ([Rulings (2026-11-16)](#rulings-2026-11-16), entry 1). After the review of the ports,
`mage_wave_edge_pad` reads the band of the wave's four strikes in place of one value, and `thief_charm_nearest`,
whose own pin had changed only whether the thief lived, took an arena and a budget of its own and was captured
again at `7c33f42b` (the same entry).

Fix BF's arena, `tower_foe_behind_door_scen99` and `tower_foe_behind_door_fixes_scen99`: a hostile tower faces the
idle player soldier below it, 66 px lower and 5 px to the side, well inside its arrow's reach of 154 px, and a
closed door of a third team stands between them, the soldier right below the door. With the setting off the tower's
line check passes at the door or at the soldier behind it, whatever it draws, and the tower shoots on tick 4: one
shot by the end of the recording (tick 8). With the setting on the door refuses every check and the tower starts no
shot; the soldier ends with all its hit points. The recording carries no door's hit points and no tower's magic, so
the rows count the shots by the bow's sound. The classic row's pin turns the flown check on with the setting off
(its tower then shoots nothing); the setting-on row's lets a check the door stopped pass again (its tower then
shoots on tick 3). Both rows are captured from the old game at `b4467150`, after the port (`2ed82025`), and each
`cmp`s equal to this game's run.

Fix BF moved one existing row with the setting on, as the old game's capture after `2ed82025` shows:
`soldier_bot_charge_door_fixes_scen99`. Its tower no longer shoots across the door, and its soldier bot, whose own
foe stands behind the door too, is refused its knife's line check by the door, so it walks toward the tower until
the door stops it (in that capture its bump then strikes the door or throws a knife into it, as a bot's bump did
on both settings before the bump's part of fix BF), and in the recording's ten ticks it never rolls CHARGE with a
heading and its front clear. Its pin then changed nothing, on every seed from 0 to 240. The setting-on twin took
an arena and a seed of its own: the door 14 px lower, so that its top row still meets the soldier's bottom row
(the rush's path) while the knife flies above it, the tower 8 px higher, its arrows above the door too, and seed
0, the first from 0 on which the gate is asked with the door in the rush's path (17 of seeds 0 to 40 are). Its
fact reads three sounds again (the tower's two shots and the soldier's knife) and four under its pin (the charge).
The setting-off twin keeps its arena, its seed and its golden. No setting-off row moves.

Fix BF's bump and blow and fix BG's guard have two arenas each, every one with its setting-off twin; neither port moved
an existing row. `orc_bot_bumps_door`: an orc bot walks toward the idle player soldier, its foe far below, and a closed
door of a third team stands right below the orc, so its walk meets the door at once. With the setting off the bump swings
at the door and the blow lands on it and clangs (one sound by the end of the recording, tick 10); with the setting on the
bot starts no swing and nothing sounds. `tower_blow_names_door`: the seat sits on a tower (team 255) and shoots one arrow
into a closed door below it, then SWITCH hands the seat to another fighter and the tower is a bot. With the setting off
the blow made the door the tower's foe, and the bot shoots at the door (two more shots by tick 24); with the setting on
the blow named no foe, and the bot, which holds no foe its arrows reach and cannot walk, shoots nothing more.
`cleric_shield_door_tree` and `soldier_boomerang_door_tree`: the arenas of `shield_projectile_absorb` and
`boomerang_projectile_absorb` with an enemy door and an enemy grown tree where their hostile arrow stood, so the guard
finds both in its ring on its first act. With the setting off the guard destroys both (neither is in the recording's
last tick); with the setting on it passes both by. A door's and a tree's damage is 0, so the guard pays nothing on either
setting, and the recording shows the rule only by the door and the tree standing or gone. Each row has a pin on its
rule's own line: the classic rows' pins turn the rule on with the setting off (the bump's and the guard's) or keep the
blow from naming a door with the setting off; the setting-on rows' pins drop the door term from the bump, let the blow
name the door again, and drop the door (the shield's row) or the tree (the boomerang's row) from the guard's question.
All eight rows are captured from the old game at `8c0cdcd1`, after the ports (`d3755554`, `dff9b822`), and each `cmp`s
equal to this game's run.

Fix BH has four arenas, one for each ported call a recording can tell
apart (the barbarian's two throws share one call, shown by HURL BOULDER),
every one with its setting-off twin; its port moved no existing row. Each
stands a hostile bot caster on the lower ground of
`elf_bot_rocks_tree_pad`, heading up, with an enemy body on the pad its
cast fires from and a tower of the player's side past it, so that every
other term of the cast's rule says yes: `druid_bot_grow_tree_tree_pad`
(GROW TREE, an enemy grown tree on the bolt's pad),
`barbarian_bot_boulder_tree_pad` (HURL BOULDER, an enemy grown tree on the hammer's pad), `mage_bot_wave_door_pad`
(ENERGY WAVE, an enemy door on the fireball's pad, with the three more towers the classic gate wants) and
`archer_bot_bolt_door_pad` (EXPLODING BOLT, an enemy door on the fire arrow's pad). With the setting off the classic bot
casts on tick 0, on the first seed from 0 on which it rolls that cast inside the recording's three ticks (2 for the
druid, the barbarian and the mage; 5 for the archer, whose tick-0 cast on seed 2 is BARRAGE): the druid's bolt, the
barbarian's hammer and the mage's fireball pass the body on their clash rolls, so a second tree grows, a boulder rolls
and a wave rides; the archer's fire arrow loses a clash roll, so its blow lands on the door and clangs. With the setting
on each rule asks the pad the strict way and holds the cast, and nothing sounds. The classic rows' pins turn the
family's fixed rule on with the setting off (the archer's through the combinator its classic gate shares,
`G.with_classic`); the setting-on rows' pins ask the plain pad question again. SUMMON FAERIE has no row: its faerie's
room (fix AR) says no to every enemy door, grown tree or shot in flight the strict pad says no to, its faerie standing
on the bolt's spot in a box of the bolt's size, so no recording can tell the two questions apart there; the strict half
of `G.summon_pad_clear` is shown by `BotGatesLib.summon_pad_clear_asks_the_weapons_pad_the_strict_way`. All eight rows
are captured from the old game at `710489a6`, after the port (`5b973aa5`), and again at `b822bb83` with the same bytes,
and each `cmp`s equal to this game's run. Captured from a copy of the old game without the port, the bot of each of the
four setting-on rows casts at the body on its pad (its cast sounds on tick 0), and each classic row reads byte for byte
as with the port: so each of the four ported calls is what its setting-on row shows.

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
- Fixes N, O and M's seat half (#354, #450): no parity row can reach them
  (see [Not gated](#not-gated) and [Charm and seats](#charm-and-seats)).
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
- The modding API's changes (#454, #464, #465, #469, #479, #480), the
  hook-failure line (#463) and the match recap (#467): no shipped pack reaches the new arms, and no
  recording holds a log line or a recap; the core pack the runner mounts
  loads and runs the same bytes.
- #413, #414, #415 and #416: the display and the lobby, which no recording
  reaches.
- #468 and #445: the box a sound carries is not in the recording, and every
  sound now placed asks the recorder's 320 x 200 view of the walker the old
  game asked `on_screen()` of, so every row records the old game's sounds;
  none moved.
- #484: no behaviour changes, and none of the display fields is recorded.
- #440: no behaviour changes, and experience is not in the recording, so
  no row could see it.
- #491: no behaviour changes; every row plays a level of type 0 and no
  scenario field sets the type. A row with the setting off on a level that
  carries the free-exit bit with another one would differ from the old game
  on purpose, since the old game asks for the whole type there.
- #503: no recording builds an owner loop, and below 16 links the walk
  changes nothing, so every row records the same bytes.
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

Thirty-six changes apply whatever the setting, three from the work that
added it (N, O and P), six from the second bugs batch, eighteen from the
issues it filed and nine from the third bugs batch; the last three entries
record differences kept on purpose (#484, #440, #491):

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
  hurts allies at half). With New Specials off the gate is not asked. The
  rain strikes only its own floor, so only an ally on that floor holds it,
  and the second foe of its cluster must be a living foe on that floor: a
  tent is no second foe, as it is none for any other kit rule, and a held
  foe on another floor is no target, so the bot holds (#487).
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
  only. With the Gameplay Fixes setting on the record's team is the colour
  of the thing that struck (fix AZ), and a blow with no living behind it
  records its colour with no walker.
- **A probe no longer grows the world (#417).** Every read-only look into
  the obmap's buckets (the spawn-spot probe, the landing and wake probes,
  the path search's cost, the text client's grid and object commands)
  reads `obmap::find_list`, which answers nothing for an empty cell
  instead of making an empty bucket there; since #427
  `GameWorld::find_near_foe` reads with it too (below), so no game code
  makes an empty bucket. No byte of the sim changes. The obmap debug
  overlay draws fewer empty boxes. The old game's spawn probe already
  reads with a find.
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
- **Every cheat key changes the game (#432).** In local play the screen
  the cheat keys wrote is a copy of the game that the next snapshot
  overwrites, so after F1 was fixed (#413) the other keys still changed
  nothing a player could keep: the hit points, the magic, the level,
  flight, invincibility, speed, invisibility and the new shape were back a
  tick later, the magic shield vanished, the foes F12 killed stood up
  again, and the team hop never moved the seat. Every key now changes the
  game itself, the way F1 does, with the game's own rules running (a kill
  credits the hero; a hop moves the seat to the new body and drops the
  orders its AI had queued, as in the old game), and the snapshot shows it
  on every view. A hop that landed leaves the save on the new team when
  the level ends, as the old game's hop does; the company's money stays
  with the lowest team on its roster. In a networked game no key does
  anything, for the host or a joiner (a host's cheat would change every
  player's game, and a joiner has no game of its own to change); before, a
  key there changed only the pressing machine's copy until the next
  snapshot. A replay plays back without the cheats, which are not recorded
  input: before, only F1 made a cheated level's replay play back
  differently; now any cheat key does, from the first key pressed. Replays
  of levels played without cheats are untouched, and a playback still
  refuses every key. This game only.
- **Water and lava flow again (#414).** Since June 2026 (`de9d02b2`) every
  snapshot repainted the palette and put the water and lava colour bands
  back: measured over six frames, the bands' turn read 1, 0, 0, 1, 0, 0
  where the cycle reads 1, 1, 1, 2, 2, 2, two frames shown in turn. One
  writer, `screen::show_palette_id`, now paints only when the palette
  shown changes; anything else that writes the colours marks it unknown,
  and a mission's end forces the restore. This game only.
- **Off-screen sounds are silent again (#468, #445).** In the old game every
  in-world sound played only while its walker was on screen; the current
  game played every one of them on every machine, so a fight across the map
  sounded next to you. Each such sound is now placed at its walker: the
  event carries the walker's box, and each machine plays it only while one
  of its own seat views shows that box, by the old game's test (a box
  touching a view's edge counts). The shot and the clang, the death cry
  (placed at the hitter, as the old game asked), the generator's explosions,
  the classic casts (CHARGE, DISARM, HEAL, TURN UNDEAD, HEARTBURST at the
  mage, PROTECTION, HOWL), the pickups (a key at its eater), the blasts and
  the New Specials kits are placed; the YO, a seat's failure clang and the
  modes' and level scripts' announcements stay cues every machine plays.
  Floors are not checked, and the camera pane does not count as a view. No
  byte of the recording changes: the box rides a field the recording leaves
  out, and the parity recorder's 320 x 200 test is asked of the same walker
  the old game asked. This game only.
- **A walker spends every banked bonus round (#460).** An enemy mage's
  FREEZE TIME banks extra rounds for itself and its allies (5 + 2 x level,
  at most 50 a cast, and the casts add up), and a walker spends its whole
  bank in its next act, one extra act per round. This game cut the bank to
  50 at the start of that act and dropped the rest, because each round was
  one more level of recursion: two mages of level 11 or more casting in
  the same tick bank more than 50 (up to 100), and the old game spends
  every one. The act now spends the rounds in a loop, all of them, as the
  old game does, without the stack. Rounds banked during those acts still
  wait for the next tick. No recording banks more than 50, so no golden
  moves.
- **A forestwalker upstairs reads its own floor's trees (#459).** The
  forest pace (the shorter step and the point of magic in the trees), a
  projectile's range spent over trees and the elf's hiding in them all
  read the trees of the floor the walker is on. Before, on a level with
  more than one floor, the tree test read floor 0 whatever the walker's
  floor, while the SHRUB half of the same test already read its own: an
  elf upstairs slowed over open grass wherever floor 0 had trees, and
  walked at full speed, in plain sight, through trees painted on its own
  floor. Single-floor levels, and so every recording, are unchanged. This
  game only: the old game has one floor.
- **A player's wall slide keeps the old game's pace (#470).** A player
  walking diagonally into a wall slides along it, one pixel up or down and
  one sideways per round, and the old game runs a round for every started
  unit of the step size (its int counter is compared with the float step
  size). This game had made the step size a whole number first, so a hired
  hero, whose step size gains its dexterity / 54, slid up to a pixel less
  per step: about a third slower at a step size of 2.2. It now runs the
  old game's rounds. A whole-number step size slides as before, and so does
  every parity row.
- **A random TELEPORT lands wherever a cell is clear (#471).** With no
  usable marker the blink draws random cells until one is clear, as the
  old game does. This game stopped after 200 draws and never tested the
  200th, so on a level where clear cells are rare (1 %) about one cast in
  seven left the mage where it was, the cast paid. It now tests every draw
  and gives up only after 64 draws per cell per floor, which a level with
  any clear cell practically never reaches; on a level with no clear cell
  at all, where the old game never stops drawing, the caster stays put.
- **A level a seat plays never times out (#466).** Every level used to end
  itself after 36000 ticks, about 49 minutes at the default speed, as a
  retreat: "Mission timed out. Retreating.", no next level, the level's
  progress lost. The old game has no limit. Now only a level nobody plays
  times out that way: a spectator room, a 0-player autoplay, a server room
  every player has left (no connected client holds a seat and no dropped
  player is still in its grace window). The lobby's match time limit stays
  below that net. A level a seat plays runs as long as it is played.
- **A corpse is reached on its own floor (#456).** On a level with stacked
  floors the corpse finder answered the nearest stain on any floor: an orc
  or an orc captain on a balcony ate the corpse below, a cleric with the
  setting off raised it, and a nearer stain on another floor hid the one at
  the caster's feet. It now answers only a stain on the finder's floor, as
  the stain walk of fix T already did, on both settings. The old game has
  no floors.
- **Allies are not scaled by the difficulty (#493).** On Easy or Hard no
  living on the player's team (team 0), and no shot, treasure or effect of
  that team, takes the difficulty percent, on both settings, as in the old
  game: a placed allied NPC (the SAVE_ALL Bearer, an allied war-host, a
  team-0 bot squad), a player's summon (a raised skeleton or ghost, a
  WARBAND grunt, a slime child, the archmage's elementals and phantoms) and
  their shots keep their hit points, magic and damage. A generator's own
  hit points take the percent on every team, as in the old game. Before, a
  team-0 walker with no player character took the percent with its body
  (A12a) and, from #473, with its shots, so on Hard a player's raised
  skeleton had twice the hit points and damage. Both are reverted on the
  maintainer's ruling, and so is their port to the old game. With the
  setting off a summon its caster's team reaches only after it is scaled
  (the cleric's RAISE GHOST, the archmage's elementals and phantoms) is
  still on team 0 then, so a foe's keeps 100% too, as in the old game; with
  it on it takes the team first and the percent with it (fix AX). Every
  other team takes the percent as before, and at 100% nothing changes, so
  no golden moves.
- **CHAIN LIGHTNING with nobody on its floor fails free (#455).** The
  chain's bolt strikes first the nearest foe on the archmage's own floor
  and cannot arc through floors, but the cast paid its busy, a shot on a
  hero's record, the bolt and half the spare pool before it looked: with
  the foes in reach all on another floor it bought a bolt with no foe to
  lead it, which died on its first tick. Such a cast now refuses `NO FOE IN
  RANGE` before it pays anything, as it does with nobody in reach at all.
  On one floor nothing changes (every foe in reach is then on the
  archmage's floor), so no recording moves. The bot's rule with the setting
  on counts the same foes ([Bots](#bots)). The old game has no floors, so
  nothing is ported.
- **A burning fire elemental bot asks only for the quench (#458)** (New
  Specials only). While IMMOLATE burns every slot is priced 0 and STARBURST,
  METEOR RAIN and REKINDLE / SUPERNOVA refuse `QUENCH IMMOLATE FIRST`. The
  IMMOLATE rule asked whether the fire burns; the other three did not, and
  at a price of 0 the pay test always passed, so a burning bot rolled casts
  that could only refuse. The three rules now say no while the fire burns,
  on both values of the setting, as fix O's ally test does: a kit rule.
  STARBURST's rule still runs the family's own foe search first, so its
  draws are unchanged. A burning elemental cornered below a fifth of its
  health no longer turns to SUPERNOVA (its cast refused anyway). No
  recording plays New Specials, and the old game has none of these casts.
- **WARBAND comes in on the nearest edge (#478)** (New Specials only). The
  grunts come in on the map edge nearest the captain, but the east and
  south gaps were measured from his top-left corner, so each came out his
  own size (16 px) too long: within 16 px of the map's middle the grunts
  came from the west or north edge when the east or south one was nearer.
  Each gap is now measured from the side of his body facing its edge; the
  tie order (west, east, north, south) and the scan inward are unchanged.
  The old game has no WARBAND.
- **The war banner's fright stays on its floor (#475)** (New Specials
  only). The banner heals only the allies on its own floor and rallies
  only there, but it frightened every weaker foe in its radius on any
  floor, so a banner on a balcony sent the weaker foes downstairs running.
  It now frightens only the weaker living foes on its floor. A bot
  captain's WAR BANNER rule counted the two friends it wants round the
  corpse on any floor; it now counts those on his floor, where the banner
  will stand. The old game has no banner.
- **A ridden body fights for the ghost's side (#430)** (New Specials
  only). POSSESS turns its host to the ghost's colour but left the host's
  owner in place, so a body a tent, a bones pile or an enemy caster owns
  still answered for its owner in every question of sides: the ghost's
  side struck it, its old side would not, and it would not strike its old
  side. The owner-chain climb now stops at a body a ghost rides, on both
  values of the setting, as fix AD stops it at a charmed walker; with the
  Gameplay Fixes setting on friendliness reads the body's own colour
  anyway (fix AZ). The body fights, scores and is credited for the ghost's
  side. Its owner, its lifetime and its tie to its tent are unchanged, and
  when the ride ends the chain is what it was. Undead resist POSSESS, so
  the shipped bodies it reaches are an enemy caster's summons. No
  recording plays New Specials, and the old game has no possession.
- **A ridden body goes home when a script kills its ghost, and the player
  gets the next hero (#499, #535).** A ride
  comes from POSSESS (New Specials) or from a level script's `og.possess`,
  which rides with New Specials off too. A ghost that dies while it rides
  frees its body, which goes back to its own side. A level script can also
  mark the ghost dead without its death running (`set_dead`); then the body
  only dropped the link and kept the ghost's colour for the rest of the
  level, while since #430 it answered for its owner again: one colour, the
  other side. The ride now ends there as the ghost's death ends it, whether
  the ghost is still in the world or was cleared away at the end of the
  tick: the body goes back to its own team (into a thief's charm record
  when a charm took it meanwhile), a seat's tag goes back to the ghost, as
  when the ghost dies, and neither is linked any more, so a later death of
  the ghost changes nothing. The ghost's body stays hidden: no burst, and it
  blocks nothing. Whichever way the script kills the ghost, the player whose
  seat drove the body used to go on steering it, now a fighter for the
  other side, until it died (#535). Now the body is left to itself and the
  seat goes to the dead ghost, so the player is handed the next hero of the
  team as after any hero's death, or, in a mode that brings its fighters
  back (Mutant's decay can kill a ghost Mutant inside a body), waits for the
  ghost to stand up and drives it again. A seat parked by a dropped
  connection goes to the dead ghost too and waits there while it is parked,
  as it waits on any hero that died under it: it is handed no new hero to
  steer with the dropped player's last input. No recording plays a ride,
  and the old game has no possession.
- **The Raspberry Isle's bone-tent Dreamkin fight for the crew (#431).**
  On the isle a fifth of each college's spawns change sides at birth and
  are named Dreamkin. The bone tent keeps its spawns as its own: their
  owner, a clock of 800 + 11 × its level ticks, death when the tent falls.
  So its Dreamkin wore the crew's colour but answered for the college: the
  crew's heroes and bots counted it a foe and struck it, and it would not
  strike the college. A Dreamkin now leaves the tent and the clock behind
  at birth, as the mage tower's and the treehouse's spawns already do. It
  fights for the crew, lives until it is killed, and outlives the tent,
  which moves the level's balance a little toward the crew. This game
  only: the old game has no level scripts.
- **A magic shield or a boomerang passes a bone wall or war banner by
  (#472).** The cleric's MAGIC SHIELD and the soldier's BOOMERANG destroy
  every foe shot inside their guard radius, each costing the guard the
  shot's damage in hit points. That took the New Specials BONE WALL and
  WAR BANNER too, solid scenery a foe has to chop down: a guard passing an
  enemy wall or banner erased it. The guard now passes solid scenery by,
  as the captain's hook blade already did (and with Gameplay Fixes BG a
  door and a grown tree too, the BG row). Every shot a recording meets is
  a classic weapon, which the guard destroys as before, so no recording
  moves; the old game has neither the wall nor the banner.
- **A low-level blast reaches as far as in the old game (#483).** An
  explosion hits everything within 15 px plus its range, 4 px per level of
  its owner, capped at 96, and 16 for a magical blast. The C++
  modernization (`dc1a2a89`) added a floor of 16 to that range, so a level
  1 to 3 owner's blast reached 31 px where the old game's reaches 19, 23
  and 27: a newly hired thief's bomb caught foes, allies and the thief
  itself that the classic bomb missed. The floor is gone on both values,
  and the bot gates' copy of the reach (`G.blast_reach`, asked by the
  thief's bomb and mine and the archer's and barbarian's exploding casts)
  lost it too, so a low-level thief bot bombs only a foe inside the blast
  it really has. The old game's explosion has no floor; its copy of the
  gates' reach, asked only with the setting on, is ported. Every golden is
  an old-game capture made without the floor, so none moves.
- **A door upstairs opens by its own floor's walls (#457).** A breaking
  door turns its opening sideways when the tile above it is a wall, and
  otherwise keeps the opening's facing (and, a 2002 quirk kept, turns the
  dying door up). The check read floor 0's terrain whatever floor the door
  stood on, so on a level with more floors a door under a wall upstairs
  could open the wrong way when floor 0 was open there, and the reverse. It
  now reads the door's own floor. On one floor nothing changes, and every
  parity door stands on floor 0, so no golden moves. This game only: the
  old game has one floor.
- **A foe search forgets a walker the world no longer holds (#427).** The
  quick foe search (`GameWorld::find_near_foe`) erased a pile entry the
  world no longer held from that one pile only: the walker stayed in its
  other piles and in `obmap::walker_to_pos`, where every passability probe
  still read it as a body (a freed pointer, had it been freed), and the
  emptied pile stayed. The search also made an empty pile at every cell its
  spiral passed. The pile now forgets such a walker whole
  (`obmap::forget_untracked`: every pile it is filed under and
  `walker_to_pos`, its pointer compared and never read), a pile it leaves
  empty goes with its index entry, and the search reads the pile with
  `obmap::find_list`, which makes none. No byte of the sim changes: in play
  no walker the world let go is still filed (a death and every removal
  file it out), so no recording moves. This game only: the old game's
  search trusts its pile.
- **A removal reads only the obmap's own record (#504).** `obmap::remove`
  takes a walker out of every pile `obmap::walker_to_pos` files it under.
  For a walker `walker_to_pos` did not know, it read the walker's position
  and looked in the piles there, which for a freed walker reads freed
  memory. It now answers from `walker_to_pos` alone, as the old game's
  removal does: such a walker is a miss and nothing changes, its pointer
  used only as a key. In play the piles and `walker_to_pos` always agree
  (only a test files a walker by hand), so the old lookup never found
  anything and no byte of the sim changes; no recording moves. The lookup
  was the current game's own (`dc1a2a89`), so nothing is ported.
- **An owner loop no longer hangs the game (#503).** Only a script's
  `set_owner`, or a forged snapshot, could make a walker owned by a walker
  it owns, over two links or more, and the first blow or side question
  about it looped forever. Every climb of an owner chain now ends: past 16
  links it looks for a loop and cuts it at the link that closes it, with
  one line in the log; the guard before a foe scan cuts a loop at the same
  link. `set_owner` now refuses, with a script error naming both walkers, a
  link that would close a loop; a walker that owns itself stays legal (a
  shot whose thrower died owns itself). No recording has a loop, and the
  old game has no scripts, so nothing is ported. `OwnerChainLoop`
  (`tests/unit/test_owner_chain_loop.cpp`) pins it on both values, with a
  two-link loop, a three-link loop and a 40-link chain with no loop.
- **A flying bot whose foe is on another floor takes a foe on its own
  floor (#447, #512).** A flyer never changes floors, so a foe on another
  floor is one it can only hover over. On a tick it re-plans within 100 px
  of such a foe, it now takes the foe the quick foe search answers when
  that one stands on its own floor, even when it is farther than the foe
  it holds; before, it kept the foe it could not reach unless the other
  was nearer on the map. Since #512 every foe search answers a flyer on a
  level with floors the nearest foe on its own floor when there is one,
  however far: the quick search and its fallback, the world's hand-out of
  a foe to a walker that has none, the ordinary act's own search, a
  guard's, the special gates that ask for a foe within some distance, and
  what a pack's script is answered when it asks for a near foe. A flyer
  far from the spot under its foe still flies there first and takes the
  foe on its own floor when it re-plans within 100 px (#584). With no foe
  on its floor it keeps the foe it has: the rule of floors. A tower, which
  cannot leave its floor either, is not part of this change (#571). A
  flyer that holds a foe on its own floor keeps #320's rule (a strictly
  nearer foe) alone.
  Measured on the Westlands levels (twenty seeds, 600 ticks), the ticks a
  flyer held a foe on another floor while a hostile stood on its own floor
  went from 192,973 to 43,303 with the setting on and from 174,149 to
  37,074 with it off. Of what is left, about seven ticks in ten are a flyer
  searching or walking, and 7,120 are a flyer in its ATTACK order facing a
  foe on another floor while a hostile stands on its own floor (53,066
  before): a flyer that holds a foe on another floor is asked for one on
  its own floor only near the spot under that foe and not during that
  order (#584).
  Only a level with more than one floor has a flyer here, so no recording
  moves on either value, and the old game has one floor, so nothing is
  ported.
  `SemAi4.a_flyer_whose_foe_is_on_another_floor_takes_the_foe_on_its_own_floor`,
  `SemWorld.the_full_foe_scan_answers_a_flyer_the_nearest_foe_on_its_own_floor`,
  `SemWorld.the_own_floor_preference_is_for_a_living_that_flies_on_a_level_with_floors`,
  `SemWorld.the_own_floor_preference_draws_what_the_scan_always_drew`,
  `SemWorld.the_world_hands_a_flyer_with_no_foe_one_on_its_own_floor` and
  `SemWorld.a_walker_flies_with_the_flag_or_with_flight_ticks_left` pin it
  on both values (see GAMEPLAY_FIXES_FROM_CLASSIC.md for the measure).
- **A hero of a pack class from 128 to 255 keeps its class (#500).** #443
  made a walker's family an unsigned byte, but a company character's own
  record kept a signed one, from the moment the hero was made. A hero of
  class 200 read back as -56: it spawned and drew as a soldier with a
  soldier's speed, its TRAIN panel showed a soldier's stats, the results
  screen called it BEAST with no specials, it was worth nothing to the
  company, and the Base Camp lobby, which refuses a class outside 0..255,
  removed it from the company at the next menu frame, for a solo company
  and for a host's own heroes alike; a joiner's hero reached the host,
  which built it as a soldier. The record, the hero's snapshot and every
  client's copy now keep the class as an unsigned byte. The save, the
  lobby and the snapshot always carried the unsigned byte, so no save,
  protocol, snapshot or replay version moves. No shipped pack has a hero
  class above 20 and HIRE offers only the core classes, so a hero reaches
  such a class only through a pack's promotion or a save that carries one.
  A hero whose class is 128 to 255 and that no installed pack provides now
  stays in the company and reads its class's own empty stat row, exactly
  as classes 21 to 127 already do (a soldier's body, no class base for
  speed, sight or damage); before, such a hero was removed from the
  company when Base Camp opened. This game only.
- **A new game skips the campaign list (#490).** BEGIN NEW GAME founds the
  company on the default campaign, Gladiator, and goes from name entry to
  Base Camp with the campaign's introduction in between, on every client,
  as the old game did; another campaign is chosen from Base Camp through
  SCENARIO > SET CAMPAIGN (the Scenario submenu's Set Campaign on the
  terminals). This game only.
- **A build off master shows its commit, not a version number (#508).**
  The `2.<count>` version is shown only by a build of a commit on master's
  history; a pull-request preview or a local branch build shows its commit
  hash alone on the main menu, in `-v` / `--version` and in the help
  footer. So does a local build of a master commit with changes to tracked
  files not yet committed: it shows the hash with its `+` (`7e7f4079+`),
  like a branch build, because it is not the commit it counts. This game
  only.
- **The terminal client's New Ability Select answer takes effect at once
  and is saved (#398, #525).** In the terminal client, answering
  `New ability select` on the Game Settings page changed the setting but
  not the copy that the ability keys, the status line and the PAUSED menu's
  ABILITIES and KEYS entries read, so for the rest of that run they went on
  as before. The SDL row and both terminal prompts now switch through one
  function, `og::ui::set_ability_select_pref`, which refreshes that copy.
  Neither terminal client's Game Settings page wrote the settings file
  either, so its New Specials, Gameplay Fixes and New Ability Select
  answers were gone at the next start. Both pages now write the file when
  they are left with an answer changed, as the SDL options screen does
  when it closes, through one function, `og::ui::save_settings_if_changed`;
  a visit that changes nothing leaves the file alone, and a write that
  fails says so on the page in one line. This game only (the old game has
  neither the setting nor a terminal client).
- **The simulation records every display effect (#484).** Not a change. The
  old game decided the melee lunge, the hit and heal numbers, a walled-in
  walker's red 1, the hurt flash, the recoil and the hit animation where it
  made them, from its own machine's toggles (the lunge only on screen). The
  current game records them all and each machine's drawing applies its own
  toggles, as it has since `25eada99`: the server has no views and no
  toggles, every machine has its own, and the lunge and the recoil travel in
  snapshots and in replays. With an effect off neither game draws it; two
  transients differ, the tail of a lunge on a walker that scrolls into view
  within three acts of the blow, and a number made in the 1.7 s or so before
  damage numbers are turned on. `SemPresentation`
  (`tests/unit/test_sem_presentation_1.cpp`) pins it with every toggle off.
- **A kill pays the kill bonus alone (#440).** Not a change to the
  simulation. Every blow that lands pays the hero at the head of the
  hitter's chain the experience of its damage at the level gap, and a kill
  adds the kill bonus, the experience of 20 damage at that gap. The old game
  pays the killing blow's experience a second time with the bonus; this
  game has paid it once since #78 (`2fd29075`), on both values of the
  setting: a level-4 hero who kills a level-4 foe with an 8-point blow earns
  72 + 181 = 253 here and 325 in the old game. Kills, scenario kills and the
  level tally count once in both. Experience is not in the recording, so no
  golden shows it, and nothing is ported.
  `SemWalker3.a_kill_adds_the_kill_bonus_alone_to_its_heros_experience`
  pins it on both values, for a melee blow, a thrown knife and a summon's
  blow.
- **A level's type is a set of flags (#491).** Not a change to the
  simulation. A level the player may leave at any time carries the
  free-exit bit of its type, and the type's other bits are the editor's
  other goals (Beat generators, Protect NPCs) and the Tower's mark. This
  game reads the bit on both values of the setting, so a level with the bit
  and another goal still offers its exit with foes standing: seven
  Westlands levels carry Protect NPCs with it, and the Tower's Gate its
  tower mark. The old game reads the bit with the setting on (fix AJ's
  port) and with it off asks for the whole type, so there such a level
  plays as an ordinary one: with foes standing its pad withdraws to a won
  level or does nothing, and a declined withdraw asks nothing more. Both
  editors toggle each goal as a bit, the 2002 header numbers the types as
  powers of two and the old game reads Protect NPCs as a bit, so the flags
  stay. No classic level carries two bits and no parity row plays a
  free-exit level, so no golden moves and nothing is ported.
  `SemExitPad.the_free_exit_bit_counts_whatever_other_type_bits_the_level_carries`
  and
  `GameServerCoverage.the_exit_after_a_declined_withdraw_reads_the_free_exit_bit_whatever_other_bits_are_set`
  pin it.

The modding API and its logs changed in the same batch too, whatever the
setting. No shipped pack or campaign reaches the new arms, so no player sees
them; a pack that does gets the documented answer, or a load error that names
its mistake:

- **A score award to a team past the table is ignored (#454).**
  `og.award_score(team, points)` checks the team as the Lua number it is
  before narrowing it to a byte, so 256, 257 or -255 is ignored like any
  team outside 0..3, as the API reference says: no score and no
  `ScoreChange`. Before, 256 credited team 0, and 257 and -255 team 1. The
  shipped treasures pass the eater's team, which is already a byte. This
  game only (the old game has no Lua API).
- **A failed family hook's log line says what ran in its place (#463).**
  A family hook that errors is treated as absent, and its caller's engine
  default runs where it has one: the stain after `on_death`, the default
  gains after `level_up`, the 11×L² scaling after `set_difficulty`, attack
  the attacker after `hit_response`, and so on for twelve of the
  twenty-three hooks. The operator line said "nothing ran" for every hook;
  it now says "the engine's default ran in its place" for those twelve, and
  "it has no engine default, so nothing ran in its place" for `do_special`
  and the notification hooks. Both say that what the hook changed before
  its error stays. Only the log line changes. This game only.
- **A misspelled level or entity hook is a load error (#464).**
  `og.register_level_hooks` read only its nine names, so
  `{ on_load = f, on_tik = g }` loaded and `on_tik` never ran. Every key is
  now checked before anything installs, in the declaration pass too: an
  unknown name is refused with a did-you-mean, a key that is not a string
  by its type, and the refused call installs nothing. `og.set_entity_hooks`,
  whose one name is `on_death`, refuses the same way. Every shipped
  registration is spelled right. This game only.
- **The match recap sees an every-level mode that refused (#467).** The
  recap tells a refusing mode from a scripted level with none by asking
  whether an `on_mode_init` hook is there. It counted only the level's own
  registrations, so a pack whose `on_mode_init` was registered for every
  level (`-1`) and refused got the count-only "MATCH: N AUTHORED TEAMS"
  lines, though the hook had run. It now asks the lookup the dispatchers
  make (the level's own hook, else the every-level one) and reads "MATCH
  WILL NOT START". A mode refuses by raising; returning `false` does not
  refuse. The recap only; the SDL, text and curses clients share it. This
  game only.
- **A list field refuses a named entry (#469).** The class-pack lists
  (`flags`, `names`, the pack's `authors`, the `specials` list, `og.anims`
  frames and each row) were read 1..#t, so a named entry was dropped
  unread: `flags = { FLYING = true }` loaded as no flags at all. A list is
  now read off its keys: a named entry is a load error naming it, and so is
  a key that is not a position (`[0]`, `[1.5]`, `[true]`). `ext` is refused
  inside a pure list, which has no block to extend; the `specials` list
  keeps its named keys, `default_cast` and `ext`. Every shipped list is
  positional. This game only.
- **A bad `wire_id` is a load error (#479).** A family's `wire_id` is an
  integer 0..255 or the word `"auto"`. The declaration took any integer and
  any string, and the installer then logged "bad wire_id" and skipped the
  family, so a pack lost it with no load error. Now 300 or -1 is refused
  where it is written ("is outside 0..255"), and so is any string but
  `"auto"`, a numeric one such as `"61"` included; `"atuo"` gets a
  did-you-mean. The installer's guard stays for data built in C++. Every
  shipped `wire_id` is in range. This game only.
- **A body probe refuses a family the game cannot make (#480).**
  `og.body_spot_near`, and the `body_spot_clear` and `body_ground_clear`
  probes behind it, build a detached body of the family asked for. The
  loader stands family 0 in for an id with no slot and a soldier in for a
  living with no family, so those probes answered for the stand-in where
  the API reference promises `nil`. A probe now answers no unless the
  body is exactly the order and family asked for. The cleric's raise and
  the parity spawn-room gate ask only real families. This game only.
- **A pack family id from 128 to 255 is its own family (#443).** A walker
  kept its family in a signed byte, so on x86 a family id of 128 or more
  read back negative. The walker drew as family 0 and found no family, a
  generator or a summon of that family made a family 0 body, a corpse
  raised a family 0 body, and a networked machine turned it into family 0.
  The walker, its corpse and the snapshot now keep the id as an unsigned
  byte, so every id 0..255 a pack can claim is its own family on every
  machine. No shipped pack claims an id above 24 (the core pack's are
  0..21, the modes pack's 13, 14 and 21..24). A company character's own
  record followed in #500. This game only.
- **A loud passability probe answers only for the prober's floor (#465).**
  `og.query_passable` and `og.query_object_passable` take an optional
  floor. Their ground half read that floor, but their bodies half can see
  only the prober's own floor, and their collision route eats, opens or
  swings there, so a floor-1 ask from floor 0 mixed two floors. Naming the
  prober's own floor is now the no-floor answer, and naming another is a
  script error that points at `og.spawn_spot_clear` (bodies and terrain)
  and `og.query_grid_passable` (terrain). No shipped script passes a floor
  to either. This game only.
- **A vetoed shot leaves no weapon (#428).** A family's `on_fire_weapon`
  hook that answers `false` blocks the shot. By then `walker::fire()` has
  built the weapon, placed it on the pad and aimed it, and it left that
  weapon alive, where every other refusal in `fire()` kills it (a pad
  nobody can stand on, a swing, a melee-only family): a hook that did not
  kill the weapon itself left a shot flying that nobody fired, counted or
  heard. The veto now kills the weapon. The soldier's out-of-blades veto,
  the one shipped hook that refuses, kills its weapon itself, and the
  archmage's never refuses; the modding guide's emberwisp example refuses
  without killing it, and its refused shot no longer flies. This game only
  (the old game has no family hooks).

## Balance

**Update (2026-11-02):** the suites no longer assert survivor counts;
the floors and ceilings named below were deleted (ruling 16 of the
[rulings](#rulings-2026-11-01)), and the tables stay as the measurements
they were.

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
as first built, `7 5 8` with fix Y amended. Levels 2 and 3 read `8 8 8` and
`8 8 7` before and `8 8 8` on both after; the three levels sum 67, 68 and 68.
Later work moved level 1 again: when fixes AO to AV began it read `7 7 6`
(levels 2 and 3 `8 8 8` and `8 8 7`, sum 67), and on their merged tree, with
the Raspberry Isle's bone-tent Dreamkin fighting for the crew (#431), it
reads `5 8 8`, levels 2 and 3 `8 8 8` and `7 7 7` (sum 66). These are
records, not pins: no Imaginations test asserts a survivor count. The other
campaigns' sums over the same three seeds, before fixes AO to AV and on
their merged tree: Westlands 317 and 322, Long Season 345 and 345, the
Tower 32 and 41.

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
calibration test files are untouched since PR #404; on 2026-11-02 the
floors and ceilings were then deleted from all four under ruling 16's
update). The two Westlands
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

**After fixes BC, BD and BE and the flyer's own-floor foe (2026-11-17).**
The four calibration suites were measured on the game before these fixes
(master `d6d0741b7`), after them (the line check, the kept turn, the
ATTACK order and #512, on the same level files) and after the easing
(the same game on the eased level files), on both values of the
setting, with New Specials on. Each battle is the suites' stand-in crew of
eight (four soldiers, an elf, an archer, a cleric and a barbarian, at the
level's crew level) for 600 ticks, on forty seeds: the twenty these docs
have always used (42, 1337, 2025 and 1 to 17), then 101 to 120. Both games
play the same seeds on the same layouts, so a level is judged by the
paired difference: for each seed the survivors after less the survivors
before, their mean m, and its error se (the standard deviation of the
forty differences over the square root of 40). A level fell when m is
three errors or more below zero (m <= -3 se), is back when m >= -se, and
reads too far when m is positive and at least two errors (m >= 2 se).
The maintainer ruled that a level whose crew survivors fell is eased
([Rulings (2026-11-16)](#rulings-2026-11-16), entries 2 and 3). This work
eases a level only when all three of these hold: it fell with the setting
on; its fall came with the ATTACK order (fix BC's own commit moved it by
two errors or more); and it read at least one survivor of eight before
these fixes. The second and third are this work's reading of the ruling
and were not ruled by him (entry 6). One step of the easing lowers one
placement group of the level's hostile orcs, ghosts or small slimes by one
level, the strongest group first, and no fighter is taken away. The tools
are committed: each suite prints its per-seed survivors and damage taken
when `OG_CALIBRATION_SEEDS` names the seeds, and
`scripts/calibration/seed_table.py` plays a suite on a seed list and
judges two runs.

In the tables "before" and "after" are the two games and "on" and "off"
the setting. Each cell is the crew's survivors at tick 600, the mean over
the forty seeds with its error, and, with the setting on, the mean over
the docs' twenty in brackets; "difference" is m with its error. "Moved by" names each fix
whose own commit moved the level by two errors or more with the setting
on, with the difference that commit made and its error: BD the line check,
BE the kept turn, BC the ATTACK order, #512 the flyer's own-floor foe.
With the setting off only #512 can act (the other three are behind the
setting), and only on a level with floors and flyers. The "after"
columns are the game with these fixes on the level files before any
easing; the two columns "after the easing" of Westlands and The Long
Season are the same game on the eased level files (a level whose file
did not change reads there what it read after the fixes), and in those
two columns the mean over the docs' twenty is in brackets with either
setting.

**Westlands** (forty seeds)

| level | crew | before, on | after, on | difference, on | before, off | after, off | moved by, on | after the easing, on | after the easing, off |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 7.95 (0.03) [8.00] | 7.15 (0.13) [7.20] | -0.80 (0.14) | 7.97 (0.03) | 7.97 (0.03) | BC -0.78 (0.15) | 7.15 (0.13) [7.20] | 7.97 (0.03) [8.00] |
| 2 | 2 | 8.00 (0.00) [8.00] | 6.97 (0.16) [6.85] | -1.02 (0.16) | 8.00 (0.00) | 8.00 (0.00) | BC -1.02 (0.16) | 6.97 (0.16) [6.85] | 8.00 (0.00) [8.00] |
| 3 | 2 | 7.62 (0.11) [7.70] | 6.08 (0.22) [6.20] | -1.55 (0.26) | 7.40 (0.13) | 7.40 (0.13) | BC -1.32 (0.22) | 7.38 (0.14) [7.40] | 7.47 (0.11) [7.55] |
| 4 | 3 | 7.67 (0.07) [7.75] | 7.03 (0.15) [6.95] | -0.65 (0.16) | 7.70 (0.07) | 7.70 (0.07) | BC -0.60 (0.19) | 7.58 (0.09) [7.60] | 7.67 (0.08) [7.75] |
| 5 | 3 | 5.85 (0.33) [6.05] | 4.95 (0.33) [4.95] | -0.90 (0.43) | 6.72 (0.21) | 6.72 (0.21) | BD +0.97 (0.38), BC -1.55 (0.45) | 4.95 (0.33) [4.95] | 6.72 (0.21) [6.80] |
| 6 | 4 | 6.95 (0.16) [7.05] | 6.40 (0.23) [6.40] | -0.55 (0.28) | 6.55 (0.23) | 6.55 (0.23) |  | 6.40 (0.23) [6.40] | 6.55 (0.23) [6.20] |
| 7 | 4 | 0.15 (0.06) [0.10] | 0.15 (0.08) [0.10] | 0.00 (0.10) | 0.23 (0.08) | 0.15 (0.07) | BC -0.33 (0.08) | 0.15 (0.08) [0.10] | 0.15 (0.07) [0.10] |
| 8 | 5 | 2.58 (0.40) [2.20] | 2.70 (0.38) [3.20] | +0.12 (0.57) | 2.88 (0.40) | 3.38 (0.34) | BE -1.07 (0.49) | 2.70 (0.38) [3.20] | 3.38 (0.34) [3.15] |
| 9 | 6 | 1.02 (0.21) [1.10] | 0.85 (0.18) [0.95] | -0.17 (0.28) | 1.05 (0.24) | 1.05 (0.24) |  | 0.85 (0.18) [0.95] | 1.05 (0.24) [0.85] |
| 10 | 5 | 0.10 (0.05) [0.05] | 0.00 (0.00) [0.00] | -0.10 (0.05) | 0.20 (0.09) | 0.20 (0.09) | BC -0.15 (0.06) | 0.00 (0.00) [0.00] | 0.20 (0.09) [0.20] |
| 11 | 6 | 5.35 (0.24) [5.40] | 3.50 (0.32) [3.60] | -1.85 (0.40) | 4.72 (0.25) | 4.72 (0.25) | BC -1.15 (0.42) | 5.28 (0.30) [5.10] | 5.75 (0.26) [5.45] |
| 12 | 6 | 0.00 (0.00) [0.00] | 0.00 (0.00) [0.00] | 0.00 (0.00) | 0.00 (0.00) | 0.00 (0.00) |  | 0.00 (0.00) [0.00] | 0.00 (0.00) [0.00] |
| 13 | 6 | 0.12 (0.06) [0.20] | 0.00 (0.00) [0.00] | -0.12 (0.06) | 0.35 (0.09) | 0.35 (0.09) | BC -0.12 (0.05) | 0.00 (0.00) [0.00] | 0.35 (0.09) [0.20] |
| 14 | 7 | 0.20 (0.18) [0.05] | 0.12 (0.12) [0.25] | -0.07 (0.22) | 0.23 (0.16) | 0.23 (0.16) |  | 0.12 (0.12) [0.25] | 0.23 (0.16) [0.15] |
| 15 | 7 | 1.85 (0.22) [1.70] | 1.85 (0.25) [2.00] | 0.00 (0.28) | 2.42 (0.21) | 2.42 (0.21) |  | 1.85 (0.25) [2.00] | 2.42 (0.21) [2.15] |
| 16 | 8 | 2.80 (0.21) [2.65] | 0.55 (0.12) [0.70] | -2.25 (0.26) | 2.83 (0.18) | 3.10 (0.13) | BC -2.40 (0.21) | 0.55 (0.12) [0.70] | 3.10 (0.13) [3.05] |
| 17 | 8 | 5.45 (0.20) [5.25] | 5.30 (0.24) [5.60] | -0.15 (0.32) | 5.97 (0.21) | 5.97 (0.21) | BD +0.62 (0.28), BC -0.70 (0.29) | 5.30 (0.24) [5.60] | 5.97 (0.21) [5.85] |
| 19 | 6 | 5.35 (0.16) [5.15] | 0.60 (0.12) [0.55] | -4.75 (0.20) | 5.80 (0.14) | 5.80 (0.14) | BC -4.55 (0.25) | 5.33 (0.25) [4.95] | 7.83 (0.06) [7.85] |
| 20 | 7 | 1.82 (0.37) [1.60] | 1.07 (0.27) [1.30] | -0.75 (0.38) | 1.45 (0.32) | 1.45 (0.32) |  | 1.07 (0.27) [1.30] | 1.45 (0.32) [1.95] |
| 21 | 7 | 7.45 (0.13) [7.40] | 7.42 (0.11) [7.45] | -0.03 (0.18) | 7.55 (0.11) | 7.55 (0.11) |  | 7.42 (0.11) [7.45] | 7.55 (0.11) [7.65] |
| 22 | 8 | 0.42 (0.13) [0.60] | 0.15 (0.08) [0.10] | -0.28 (0.16) | 0.68 (0.16) | 0.68 (0.16) |  | 0.15 (0.08) [0.10] | 0.68 (0.16) [0.60] |
| 23 | 8 | 5.90 (0.23) [5.95] | 2.58 (0.30) [2.55] | -3.33 (0.42) | 6.22 (0.20) | 6.22 (0.20) | BC -3.35 (0.37) | 5.53 (0.28) [5.15] | 7.17 (0.16) [7.05] |
| 24 | 8 | 4.83 (0.27) [4.75] | 2.73 (0.23) [2.95] | -2.10 (0.39) | 4.75 (0.24) | 4.75 (0.24) | BC -1.75 (0.28) | 4.50 (0.24) [4.55] | 5.58 (0.23) [5.45] |
| 25 | 9 | 6.65 (0.19) [6.85] | 6.80 (0.20) [7.15] | +0.15 (0.25) | 6.60 (0.17) | 6.60 (0.17) |  | 6.80 (0.20) [7.15] | 6.60 (0.17) [6.90] |
| 26 | 9 | 8.00 (0.00) [8.00] | 8.00 (0.00) [8.00] | 0.00 (0.00) | 8.00 (0.00) | 8.00 (0.00) |  | 8.00 (0.00) [8.00] | 8.00 (0.00) [8.00] |

**The Long Season** (forty seeds)

| level | crew | before, on | after, on | difference, on | before, off | after, off | moved by, on | after the easing, on | after the easing, off |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 7.95 (0.03) [8.00] | 7.75 (0.08) [7.80] | -0.20 (0.08) | 7.97 (0.03) | 7.97 (0.03) | BC -0.25 (0.08) | 7.75 (0.08) [7.80] | 7.97 (0.03) [7.95] |
| 2 | 1 | 6.17 (0.17) [6.30] | 6.47 (0.18) [6.50] | +0.30 (0.26) | 6.75 (0.17) | 6.75 (0.17) |  | 6.47 (0.18) [6.50] | 6.75 (0.17) [6.95] |
| 3 | 2 | 7.88 (0.05) [8.00] | 7.75 (0.07) [7.75] | -0.12 (0.09) | 7.90 (0.05) | 7.90 (0.05) |  | 7.75 (0.07) [7.75] | 7.90 (0.05) [7.95] |
| 4 | 2 | 2.52 (0.28) [2.10] | 1.38 (0.24) [1.40] | -1.15 (0.33) | 3.55 (0.28) | 3.55 (0.28) | BC -1.77 (0.38) | 1.38 (0.24) [1.40] | 3.55 (0.28) [3.70] |
| 5 | 3 | 6.00 (0.31) [6.35] | 6.15 (0.27) [6.00] | +0.15 (0.46) | 6.72 (0.20) | 6.72 (0.20) |  | 6.15 (0.27) [6.00] | 6.72 (0.20) [6.55] |
| 6 | 3 | 6.08 (0.17) [6.30] | 5.95 (0.19) [6.45] | -0.12 (0.21) | 5.65 (0.22) | 5.65 (0.22) | BD -0.93 (0.33), BE +0.80 (0.32) | 5.95 (0.19) [6.45] | 5.65 (0.22) [5.60] |
| 7 | 4 | 7.53 (0.12) [7.65] | 7.80 (0.08) [7.85] | +0.28 (0.16) | 7.83 (0.06) | 7.83 (0.06) |  | 7.80 (0.08) [7.85] | 7.83 (0.06) [7.85] |
| 8 | 4 | 6.30 (0.29) [6.35] | 6.33 (0.25) [6.10] | +0.03 (0.35) | 6.30 (0.24) | 6.30 (0.24) |  | 6.33 (0.25) [6.10] | 6.30 (0.24) [6.15] |
| 9 | 5 | 7.95 (0.03) [7.95] | 7.90 (0.05) [7.95] | -0.05 (0.05) | 7.95 (0.03) | 7.95 (0.03) |  | 7.90 (0.05) [7.95] | 7.95 (0.03) [7.95] |
| 10 | 5 | 5.10 (0.27) [5.05] | 5.33 (0.32) [5.50] | +0.23 (0.38) | 5.50 (0.25) | 5.50 (0.25) |  | 5.33 (0.32) [5.50] | 5.50 (0.25) [5.25] |
| 11 | 5 | 6.38 (0.18) [6.40] | 6.75 (0.20) [6.55] | +0.38 (0.28) | 6.62 (0.15) | 6.70 (0.14) |  | 6.75 (0.20) [6.55] | 6.70 (0.14) [6.75] |
| 12 | 6 | 6.12 (0.24) [6.05] | 6.00 (0.27) [6.25] | -0.12 (0.40) | 6.20 (0.24) | 6.20 (0.24) |  | 6.00 (0.27) [6.25] | 6.20 (0.24) [6.55] |
| 13 | 6 | 5.40 (0.32) [5.50] | 5.12 (0.29) [5.10] | -0.28 (0.38) | 4.72 (0.28) | 4.72 (0.28) |  | 5.12 (0.29) [5.10] | 4.72 (0.28) [4.95] |
| 14 | 7 | 7.67 (0.11) [7.45] | 7.53 (0.12) [7.40] | -0.15 (0.10) | 7.67 (0.11) | 7.67 (0.11) |  | 7.53 (0.12) [7.40] | 7.67 (0.11) [7.65] |
| 15 | 7 | 8.00 (0.00) [8.00] | 7.92 (0.04) [8.00] | -0.07 (0.04) | 8.00 (0.00) | 8.00 (0.00) |  | 7.92 (0.04) [8.00] | 8.00 (0.00) [8.00] |
| 16 | 7 | 5.90 (0.24) [5.60] | 0.03 (0.03) [0.05] | -5.88 (0.24) | 5.70 (0.25) | 5.70 (0.25) | BC -5.72 (0.26) | 4.90 (0.33) [5.05] | 4.92 (0.34) [5.05] |
| 17 | 8 | 0.00 (0.00) [0.00] | 0.00 (0.00) [0.00] | 0.00 (0.00) | 0.00 (0.00) | 0.00 (0.00) |  | 0.00 (0.00) [0.00] | 0.00 (0.00) [0.00] |
| 18 | 8 | 0.05 (0.03) [0.00] | 0.12 (0.12) [0.00] | +0.07 (0.13) | 0.07 (0.04) | 0.07 (0.04) |  | 0.12 (0.12) [0.00] | 0.07 (0.04) [0.05] |
| 19 | 8 | 8.00 (0.00) [8.00] | 8.00 (0.00) [8.00] | 0.00 (0.00) | 8.00 (0.00) | 8.00 (0.00) |  | 8.00 (0.00) [8.00] | 8.00 (0.00) [8.00] |

**Imaginations** (forty seeds)

| level | crew | before, on | after, on | difference, on | before, off | after, off | moved by, on |
|---|---|---|---|---|---|---|---|
| 1 | 1 | 6.53 (0.14) [6.60] | 5.97 (0.20) [5.95] | -0.55 (0.22) | 6.30 (0.16) | 6.30 (0.16) | BC -0.72 (0.25) |
| 2 | 1 | 7.95 (0.03) [7.95] | 8.00 (0.00) [8.00] | +0.05 (0.03) | 8.00 (0.00) | 8.00 (0.00) |  |
| 3 | 1 | 7.75 (0.09) [7.75] | 7.28 (0.12) [7.25] | -0.47 (0.14) | 7.65 (0.10) | 7.65 (0.10) | BC -0.33 (0.13) |

**The Tower** (forty seeds)

| floor | crew | before, on | after, on | difference, on | before, off | after, off | moved by, on |
|---|---|---|---|---|---|---|---|
| 1 | 1 | 7.35 (0.14) [7.35] | 7.25 (0.12) [7.35] | -0.10 (0.14) | 7.42 (0.12) | 7.42 (0.12) |  |
| 5 | 2 | 1.38 (0.37) [1.40] | 1.30 (0.40) [1.45] | -0.07 (0.21) | 1.75 (0.38) | 1.75 (0.38) |  |
| 10 | 3 | 0.70 (0.28) [0.90] | 0.75 (0.30) [1.00] | +0.05 (0.38) | 1.20 (0.37) | 1.18 (0.37) |  |
| 15 | 4 | 1.05 (0.30) [1.00] | 1.12 (0.30) [1.20] | +0.07 (0.33) | 1.10 (0.27) | 1.27 (0.30) | BC -0.68 (0.24) |
| 20 | 6 | 0.00 (0.00) [0.00] | 0.00 (0.00) [0.00] | 0.00 (0.00) | 0.00 (0.00) | 0.00 (0.00) |  |

Summed over the levels, per run of the campaign, with the setting on:
Westlands 104.10 before and 82.95 after (m -21.15, se 1.30), The Long
Season 111.00 and 104.28 (-6.72, 1.20), Imaginations 22.23 and 21.25
(-0.97, 0.26), the Tower 10.47 and 10.43 (-0.05, 0.54). Commit by commit,
in the order they landed (the line check, the kept turn, the ATTACK order,
#512): Westlands +0.53 (1.10), -1.73 (1.31), -20.27 (1.26) and +0.33
(0.38); The Long Season +0.88 (1.22), +1.90 (1.17), -9.53 (0.97) and +0.03
(0.04); Imaginations +0.03 (0.29), +0.05 (0.28), -1.05 (0.25) and 0.00;
the Tower +0.28 (0.49), +0.23 (0.51), -0.68 (0.24) and +0.12 (0.21). The
ATTACK order carries the cost. With the setting off: Westlands 106.28 and
106.97 (+0.70, 0.34), The Long Season 113.12 and 113.20 (+0.07, 0.04),
Imaginations 21.95 both, the Tower 11.47 and 11.62 (+0.15, 0.21). After
the fixes and before the easing, with the setting on these levels fell:
Westlands 1, 2, 3, 4, 11, 16, 19, 23 and 24, The Long Season 4 and 16, and
Imaginations 3; no Tower floor fell. With the setting off no level of any
campaign fell.

**The easing (2026-11-17 and 2026-11-18).** Twelve levels met all three
conditions. Nine of them have a group of orcs, ghosts or small slimes
above level 1 that fights before tick 600, and so have steps: Westlands
2, 3, 4, 11, 16, 19, 23 and 24 and level 16 of The Long Season. The other
three, Westlands 1, The Long Season 4 and Imaginations 3, have none
(listed below). No Tower floor fell, and no level fell without the ATTACK
order. Each of the nine was eased in its builder
(`tools/westlands_mapgen`, `tools/longseason_mapgen`) one step at a time,
and each step tried was generated and measured on the forty seeds. A level's
steps lower only its groups that fight before tick 600: a fighter that
arrives at tick 600 or later is never lowered, because the count cannot
judge a step on it, and neither is anything a generator spawns. On
2026-11-17 every level was first set at half its steps (rounded up), and a
level already back there was then set at the fewest steps that read back
(The Great River); the eight levels that were not back at half were
brought to the maintainer, and the end state below is the one he approved
on 2026-11-18 ([Rulings (2026-11-16)](#rulings-2026-11-16), entry 3).
Seven levels end eased. The Forest Road (Westlands 2) and The White City
(Westlands 16) are left as they were, because no step brought them back,
down to every group that fights before tick 600 at level 1. Two end states
leave the order of the steps, as he approved: The Dead Marshes lowers the
four marsh-lights (ghosts) of one mere (P3) without those of the next
(P4), which share one loop with them in the builder, and The Mountain of
Fire keeps its two strongest groups at their old levels. No fighter was
taken away and no count of a level changed. A dated note above the
changed placements in each builder says what the level ends at.

Every step measured, on the forty seeds, with the docs' twenty in
brackets. "Steps" counts the steps taken in their order (the group with
the strongest fighters first) out of the level's whole count; "as above"
keeps the row above it; a row that names half and a group is half the
steps with that group alone one level lower. The difference is against the
game before these fixes, with the setting on, with the measure's word for
it ("two to three errors under" is a fall less than the measure can
prove). Where the setting off was not measured the row says so. The groups
have their builders' names; P1 to P6 are the six meres of The Dead
Marshes, P2 the great mere and P5 the small one.

| level | steps | what is lowered | the setting on [twenty] | against before these fixes, on | the setting off [twenty] |
|---|---|---|---|---|---|
| The Forest Road (Westlands 2) | none (the end state) | nothing: the level as it was | 6.97 (0.16) [6.85] | -1.02 (0.16), fell | 8.00 (0.00) [8.00] |
|  | 5 of 9 (half) | the four pursuit riders (ghosts) and pocket 2's three wolves (orcs) at level 1, from 2 | 7.05 (0.14) [7.10] | -0.95 (0.14), fell | 7.97 (0.03) [8.00] |
|  | 9 of 9 | as at half, and the four guard wolves of bend 3 and seg D at level 1, from 2 | 7.05 (0.14) [7.10] | -0.95 (0.14), fell | 7.97 (0.03) [8.00] |
| The Last Ford (Westlands 3) | none | nothing | 6.08 (0.22) [6.20] | -1.55 (0.26), fell | 7.40 (0.13) [7.45] |
|  | 2 of 3 (half) | the five first-beat Riders (ghosts) at level 2, from 4 | 7.08 (0.18) [7.05] | -0.55 (0.21), two to three errors under | 7.47 (0.11) [7.55] |
|  | 3 of 3 (the end state) | the five Riders at level 1 | 7.38 (0.14) [7.40] | -0.25 (0.19), not back | 7.47 (0.11) [7.55] |
| The Hidden Refuge (Westlands 4) | none | nothing | 7.03 (0.15) [6.95] | -0.65 (0.16), fell | 7.70 (0.07) [7.60] |
|  | 1 of 2 (half) | the six scouts (orcs) at level 1, from 2 | 7.50 (0.11) [7.55] | -0.17 (0.11), not back | 7.60 (0.10) [7.60] |
|  | 2 of 2 (the end state) | as at half, and the twelve dawn orcs at level 1, from 2 | 7.58 (0.09) [7.60] | -0.10 (0.10), back | 7.67 (0.08) [7.75] |
| The Great River (Westlands 11) | none | nothing | 3.50 (0.32) [3.60] | -1.85 (0.40), fell | 4.72 (0.25) [4.60] |
|  | 1 of 15 | one of the two ghosts of isle B at level 4, from 5 | 4.53 (0.31) [4.10] | -0.82 (0.37), two to three errors under | not measured |
|  | 2 of 15 (the end state) | both ghosts of isle B at level 4 | 5.28 (0.30) [5.10] | -0.07 (0.40), back | 5.75 (0.26) [5.45] |
|  | 3 of 15 | as above, and the four risen ghosts at level 4, from 5 | 5.30 (0.30) [5.10] | -0.05 (0.40), back | not measured |
|  | 4 of 15 | as above, and one ghost of isle B at level 3 | 5.65 (0.24) [5.25] | +0.30 (0.34), back | not measured |
|  | 8 of 15 (half) | both ghosts of isle B at level 2 and the four risen ghosts at 3 | 5.17 (0.28) [5.05] | -0.17 (0.40), back | not measured |
| The White City (Westlands 16) | none (the end state) | nothing: the level as it was | 0.55 (0.12) [0.70] | -2.25 (0.26), fell | 3.10 (0.13) [3.05] |
|  | 5 of 10 (half) | the breach column's twelve orcs at level 2, from 3; the plain lattice's thirty orcs at 1, 1 and 2, from 2, 3 and 4; the six town ghosts at 2, from 4; the four plain ghosts stay at 3 | 0.85 (0.19) [1.05] | -1.95 (0.29), fell | 1.52 (0.21) [1.60] |
|  | 10 of 10 | every group at level 1 | 1.55 (0.27) [1.45] | -1.25 (0.37), fell | 1.32 (0.24) [1.20] |
|  | not a step | the fighters as they were and the three tents at level 5, from 7 (a tent is a generator, which no step lowers) | 0.60 (0.16) [0.70] | -2.20 (0.31), fell | 2.73 (0.17) [2.70] |
| The Dead Marshes (Westlands 19) | none | nothing | 0.60 (0.12) [0.55] | -4.75 (0.20), fell | 5.80 (0.14) [5.95] |
|  | 10 of 20 (half) | P1's five lights (ghosts) at levels 1 and 2, from 3 and 4; the great mere's five lights at 1 and its elder light at 2, from 3 and 4 and the elder's 5; P3's and P4's eight lights at 2 and 3, from 3 and 4; the small mere's pair at 3, from 4; P6's three lights at 1 and 2 and its elder at 3, from 3 and 4 and the elder's 5 | 4.78 (0.24) [5.05] | -0.57 (0.31), not back | 7.53 (0.10) [7.55] |
|  | 11 of 20 | as at half, and P3's and P4's eight lights (one loop) at 1 and 2 | 6.33 (0.21) [6.45] | +0.97 (0.27), too far | 7.90 (0.05) [7.85] |
|  | 12 of 20 | as above, and one of the small mere's pair at 2 | 6.40 (0.27) [6.30] | +1.05 (0.32), too far | 7.92 (0.04) [7.90] |
|  | 13 of 20 | as above, and the other at 2 | 6.90 (0.17) [6.90] | +1.55 (0.25), too far | 7.95 (0.03) [7.90] |
|  | 14 of 20 | as above, and P6's lights at 1 and its elder at 2 | 6.97 (0.17) [6.80] | +1.62 (0.24), too far | 7.92 (0.04) [7.85] |
|  | 15 of 20 | as above, and P1's lights at 1 | 7.05 (0.12) [7.10] | +1.70 (0.21), too far | 8.00 (0.00) [8.00] |
|  | 20 of 20 | every light at level 1 | 7.25 (0.16) [7.20] | +1.90 (0.23), too far | 8.00 (0.00) [8.00] |
|  | half and P3 (the end state) | as at half, and P3's four lights alone at 1 and 2 | 5.33 (0.25) [4.95] | -0.03 (0.29), back | 7.83 (0.06) [7.85] |
|  | half and P4 | as at half, and P4's four lights alone at 1 and 2 | 5.25 (0.25) [5.75] | -0.10 (0.33), back | 7.85 (0.06) [7.70] |
|  | half and the small mere | as at half, and both of the small mere's pair at 2 | 5.05 (0.28) [5.30] | -0.30 (0.33), back | 7.83 (0.06) [7.80] |
|  | half and one light | as at half, and one of the small mere's pair at 2 | 4.78 (0.27) [5.05] | -0.57 (0.33), not back | 7.78 (0.08) [7.80] |
|  | half and P6 | as at half, and P6's lights at 1 and its elder at 2 | 4.65 (0.26) [4.80] | -0.70 (0.33), two to three errors under | 7.70 (0.08) [7.85] |
| The Ash Plains (Westlands 23) | none | nothing | 2.58 (0.30) [2.55] | -3.33 (0.42), fell | 6.22 (0.20) [6.05] |
|  | 7 of 14 (half) | the central host's twelve orcs at level 2, from 3; the first of the two orcs that watch the north ford of the first river at 2 and the second at 3, both from 5; the six wraiths (ghosts) at 3, from 4; the west column's eight orcs stay at 2 | 5.22 (0.29) [5.30] | -0.68 (0.37), not back | 7.17 (0.17) [7.05] |
|  | 8 of 14 (the end state) | as at half, and the second ford watch at 2 | 5.53 (0.28) [5.15] | -0.38 (0.34), not back | 7.17 (0.16) [7.05] |
|  | 9 of 14 | as above, and the six wraiths at 2 | 6.88 (0.20) [6.90] | +0.97 (0.30), too far | 7.35 (0.10) [7.15] |
|  | 10 of 14 | as above, and the west column at 1 | 7.08 (0.18) [7.05] | +1.18 (0.33), too far | 7.47 (0.15) [7.60] |
|  | 11 of 14 | as above, and the central host at 1 | 7.40 (0.15) [7.35] | +1.50 (0.30), too far | 7.55 (0.13) [7.60] |
|  | 14 of 14 | every group at level 1 | 7.03 (0.17) [7.15] | +1.12 (0.27), too far | 7.45 (0.11) [7.35] |
| The Mountain of Fire (Westlands 24) | none | nothing | 2.73 (0.23) [2.95] | -2.10 (0.39), fell | 4.75 (0.24) [4.70] |
|  | 14 of 27 (half) | the plain ranks' thirty orcs at levels 2 and 3, from 3 and 4; the six ghost outriders at 2 and 3, from 5 and 6; the four Undergate chamber guards (orcs) at 4, from 6; the two caldera ghosts at 4, from 8 | 4.12 (0.27) [3.75] | -0.70 (0.41), not back | 5.00 (0.22) [5.15] |
|  | 27 of 27 | every group at level 1 | 5.53 (0.20) [5.45] | +0.70 (0.35), back (1.97 errors) | 5.28 (0.27) [5.05] |
|  | ranks one level, outriders three (the end state) | the ranks and the outriders as at half; the chamber guards at 6 and the caldera ghosts at 8, as they were | 4.50 (0.24) [4.55] | -0.33 (0.37), back | 5.58 (0.23) [5.45] |
|  | ranks one level, outriders two | the ranks at 2 and 3 and the outriders at 3 and 4; nothing else | 4.08 (0.23) [4.10] | -0.75 (0.36), two to three errors under | 5.75 (0.24) [5.60] |
|  | outriders two levels | the outriders at 3 and 4; nothing else | 3.15 (0.23) [3.25] | -1.68 (0.38), fell | 4.92 (0.26) [4.85] |
|  | outriders one level | the outriders at 4 and 5; nothing else | 2.90 (0.24) [3.05] | -1.93 (0.38), fell | 4.85 (0.24) [4.85] |
| The Frozen Ford (Long Season 16) | none | nothing | 0.03 (0.03) [0.05] | -5.88 (0.24), fell | 5.70 (0.25) [5.45] |
|  | 4 of 15 | the six bank wolves (orcs) at level 4, the four ice wraiths (ghosts) and the first pursuit's three ghosts at 5, all from 6 | 2.70 (0.47) [1.90] | -3.20 (0.53), fell | 7.80 (0.06) [7.75] |
|  | 5 of 15 | as above, and the ice wraiths at 4 | 2.75 (0.47) [1.90] | -3.15 (0.53), fell | 7.80 (0.06) [7.75] |
|  | 6 of 15 | as above, and the first pursuit at 4 | 3.23 (0.49) [2.35] | -2.67 (0.54), fell | 7.88 (0.05) [7.85] |
|  | 7 of 15 (the end state) | as above, and the bank wolves at 3 | 4.90 (0.33) [5.05] | -1.00 (0.48), two to three errors under | 4.92 (0.34) [5.05] |
|  | 8 of 15 (half) | as above, and the ice wraiths at 3 | 4.47 (0.35) [4.65] | -1.43 (0.47), fell | 4.17 (0.27) [4.20] |
|  | 9 of 15 | as above, and the first pursuit at 3 | 4.45 (0.33) [4.70] | -1.45 (0.45), fell | 4.17 (0.24) [4.30] |
|  | 10 of 15 | as above, and the bank wolves at 2 | 2.80 (0.33) [2.85] | -3.10 (0.47), fell | 2.75 (0.31) [3.10] |
|  | 12 of 15 | as above, and the ice wraiths and the first pursuit at 2 | 3.50 (0.34) [3.55] | -2.40 (0.42), fell | 2.77 (0.29) [2.75] |
|  | 15 of 15 | every group at level 1 | 2.62 (0.37) [3.20] | -3.27 (0.45), fell | 1.77 (0.28) [1.40] |

Where each level ends, and why (survivors of eight with the setting on:
before these fixes, after them on the level files before any easing, and
after the easing, with the docs' twenty in brackets, then how far under
the level before these fixes it reads and the error of that gap):

- **The Last Ford (Westlands 3)**: the five first-beat Riders, ghosts that
  come at tick 400, from level 4 to 1, the last of its three steps: 7.62
  [7.70], 6.08 [6.20], 7.38 [7.40]; 0.25 under, error 0.19. No group that
  fights before tick 600 is left to lower; ten orcs at levels 2 and 3 wake
  at tick 800 and five ghosts at level 4 at tick 1200.
- **The Hidden Refuge (Westlands 4)**: the six scouts, and the twelve dawn
  orcs who come at tick 500, from level 2 to 1, both its steps: 7.67
  [7.75], 7.03 [6.95], 7.58 [7.60]; 0.10 under, error 0.10, back.
- **The Great River (Westlands 11)**: the two ghosts of isle B from level
  5 to 4, two of its fifteen steps, eased on 2026-11-17 and unchanged
  since: 5.35 [5.40], 3.50 [3.60], 5.28 [5.10]; 0.07 under, error 0.40,
  back. It is the fewest steps that read back: at one step it read 4.53
  [4.10], and at three it read back as well, 5.30 [5.10].
- **The Dead Marshes (Westlands 19)**: half its twenty steps, and P3's
  four marsh-lights (ghosts) one level more. P1's five lights go from
  levels 3 and 4 to 1 and 2, the great mere's five from 3 and 4 to 1 and
  its elder light from 5 to 2, P3's four from 3 and 4 to 1 and 2, P4's
  four from 3 and 4 to 2 and 3, the small mere's pair from 4 to 3, and
  P6's three from 3 and 4 to 1 and 2 with its elder from 5 to 3: 5.35
  [5.15], 0.60 [0.55], 5.33 [4.95]; 0.03 under, error 0.29, back. Half
  its steps read 4.78 [5.05], not back; the next step lowers P3's and
  P4's lights together, one loop in the builder, and read 6.33 [6.45], too
  far. Of the groups lowered alone from half, P3 came nearest: P4 read
  5.25 [5.75], the small mere's pair 5.05 [5.30], one of the pair 4.78
  [5.05] and P6 4.65 [4.80].
- **The Ash Plains (Westlands 23)**: one step past half: the central
  host's twelve orcs from level 3 to 2, the two orcs that watch the first
  river's north ford from 5 to 2, and the six wraiths from 4 to 3; the
  west column's eight orcs stay at 2. 5.90 [5.95], 2.58 [2.55], 5.53
  [5.15]; 0.38 under, error 0.34. The next step, the wraiths at 2, read
  6.88 [6.90], too far.
- **The Mountain of Fire (Westlands 24)**: the plain ranks' thirty orcs
  from levels 3 and 4 to 2 and 3, and the six ghost outriders, who come at
  tick 400, from 5 and 6 to 2 and 3; the four Undergate chamber guards
  (orcs at level 6) and the two caldera ghosts (level 8) keep their
  levels. 4.83 [4.75], 2.73 [2.95], 4.50 [4.55]; 0.33 under, error 0.37,
  back by the rule. Half its steps, which lowered those six fighters as
  well, read 4.12 [3.75]: lowering them changed the runs seed by seed (on
  34 of the forty seeds) but moved the average by no more than its error
  (0.38, error 0.42). With fewer steps on the ranks and the outriders it
  read 4.08 [4.10] or less.
- **The Frozen Ford (Long Season 16)**: seven of its fifteen steps, one
  short of half: the six bank wolves from level 6 to 3, the four ice
  wraiths from 6 to 4, and the first pursuit's three ghosts, who come at
  tick 350, from 6 to 4. 5.90 [5.60], 0.03 [0.05], 4.90 [5.05]; 1.00
  under, error 0.48, the nearest any step came: half its steps read 4.47
  [4.65], one step past half 4.45 [4.70], and every other step measured
  3.50 [3.55] or less.
- **The Forest Road (Westlands 2)**, not eased: 8.00 [8.00], 6.97 [6.85],
  6.97 [6.85]. With the four pursuit riders (ghosts) and pocket 2's three
  wolves (orcs) at level 1, from 2, it read 7.05 [7.10], and the four
  guard wolves of bend 3 and seg D at level 1 as well changed no seed:
  with every group that fights before tick 600 at level 1 it does not come
  back. Beyond tick 600 it keeps two riders at level 3, who ride out at
  tick 900, and a wolf at level 2, who wakes at tick 2300. Its level file
  is the one from before any easing.
- **The White City (Westlands 16)**, not eased: 2.80 [2.65], 0.55 [0.70],
  0.55 [0.70]. Half its steps read 0.85 [1.05], and every group at level 1
  read 1.55 [1.45] (with the setting off 1.32 [1.20], where the level as
  it was reads 3.10 [3.05]); its three tents at level 5 instead of 7, the
  fighters as they were, read 0.60 [0.70]. Its level file is the one from
  before any easing.

The Last Ford, The Ash Plains and The Mountain of Fire end 0.25, 0.38 and
0.33 under, with errors of 0.19, 0.34 and 0.37: gaps of about the same
size, which the measure calls not back, not back and back only because The
Mountain of Fire's gap is inside one error. The Frozen Ford ends 1.00
under, error 0.48.

**The levels not eased, and why.** Survivors of eight with the setting on,
before these fixes and after them; their level files did not change, so
they read the same after the easing.

- Fell, but their orcs, ghosts and small slimes that fight before tick
  600 are already at level 1 (a step lowers no other fighter), and the
  maintainer ruled that no fighter is taken away: Westlands 1,
  7.95 to 7.15 (m -0.80, se 0.14), whose two ghosts at level 2 wake at
  tick 600; The Long Season 4, 2.52 to 1.38 (-1.15, 0.33); Imaginations 3,
  7.75 to 7.28 (-0.47, 0.14).
- Fell by two to three errors, less than the measure can prove: Westlands
  5, 5.85 to 4.95 (-0.90, 0.43); The Long Season 1, 7.95 to 7.75 (-0.20,
  0.08); Imaginations 1, 6.53 to 5.97 (-0.55, 0.22).
- Fell, and lowering every group that fights before tick 600 does not
  bring them back: The Forest Road and The White City (above).
- Fell, but not with the ATTACK order: none.
- Under one survivor before these fixes, where the count cannot show a
  fall or an easing; in parentheses the crew's damage taken per battle,
  before and after: Westlands 7, 0.15 to 0.15 (2,041 to 2,053); 10, 0.10
  to 0.00 (2,236 to 2,218); 12, 0.00 to 0.00 (2,590 to 2,719); 13, 0.12 to
  0.00 (2,437 to 2,344); 14, 0.20 to 0.12 (2,941 to 2,826); 22, 0.42 to
  0.15 (2,848 to 3,029); The Long Season 17, 0.00 to 0.00 (3,128 to 3,021)
  and 18, 0.05 to 0.12 (2,849 to 2,890); the Tower's floor 10, 0.70 to
  0.75 (2,012 to 1,896), and floor 20, 0.00 to 0.00 (2,656 to 2,646).
- All eight alive on every seed before these fixes: Westlands 26 and The
  Long Season 19 still are, while the crew takes more damage (56 to 68 and
  406 to 560); The Long Season 15 reads 7.92 after them (117 to 213); The
  Forest Road fell (above).
- The Tower: no floor fell against the game before these fixes (the table
  above), so no Tower table changed.

**With the setting off.** The easing is in the level files, and a level
file serves both settings, so the classic setting plays the eased levels
too, as the maintainer accepted
([Rulings (2026-11-16)](#rulings-2026-11-16), entry 2). Before these fixes
and after the easing, with the setting off: The Great River reads 4.72
[4.60] and 5.75 [5.45] (m +1.02, se 0.37), The Dead Marshes 5.80 [5.95]
and 7.83 [7.85] (+2.02, 0.15), The Ash Plains 6.22 [6.05] and 7.17 [7.05]
(+0.95, 0.24) and The Mountain of Fire 4.75 [4.70] and 5.58 [5.45] (+0.82,
0.34): easier, each by two errors or more. The Last Ford 7.40 [7.45] and
7.47 [7.55] (+0.07, 0.06) and The Hidden Refuge 7.70 [7.60] and 7.67
[7.75] (-0.03, 0.10) read as before: neither moved by two errors, and
the measure calls both back. The Forest Road and The White City, not
eased, read 8.00 [8.00] and 3.10 [3.05], as after the fixes (8.00 [8.00]
and 2.83 [2.90] before them).

The Frozen Ford reads lower with the setting off after the easing, not
easier: 5.70 [5.45] before these fixes and after them, 4.92 [5.05] after
the easing (m -0.78, se 0.41). With one step fewer, the bank wolves still
at level 4, it read 7.88 [7.85] with the setting off, and at half its
steps 4.17 [4.20]: with the setting off its reading falls when the bank
wolves go from 4 to 3. Why is not traced.

Summed over the levels, per run of the campaign, at four points (before
these fixes; after them, on the level files before any easing; at the
state of 2026-11-17 (half the steps, The Great River at two of its
fifteen); after the easing): Westlands with the setting on
104.10, 82.95, 94.80 and 96.03 (after the easing against before these
fixes, m -8.07, se 1.23), with it off 106.28, 106.97, 109.30 and 111.85
(+5.58, 0.52); The Long Season with the setting on 111.00, 104.28, 108.72
and 109.15 (-1.85, 1.23), with it off 113.12, 113.20, 111.67 and 112.42
(-0.70, 0.41). Imaginations and the Tower read after the easing what they
read after the fixes (21.25 and 10.43 with the setting on, 21.95 and 11.62
with it off).

What the count of the first 600 ticks does not see on the eased levels.
Fighters that arrive at tick 600 or later are outside the count and
outside the steps: on The Last Ford ten orcs at levels 2 and 3 (tick 800),
five ghosts at level 4 (1200), and big orcs and skeletons (1600); on The
Hidden Refuge six ghosts at level 3 and ten skeletons (1000); on The Dead
Marshes six ghosts at level 4 (800); on The Ash Plains six orcs at level 3
(700); on The Frozen Ford the second pursuit's three ghosts at level 7
(700) and four barbarians (1000). The Great River and The Mountain of Fire
place no foe that late (their last hostile arrivals, the risen ghosts and
the outriders, come at ticks 500 and 400); on The Mountain of Fire the
crew's ally the White Rider, an archmage at level 10, arrives at tick 900,
outside the count as well. And the steps lower placed fighters only,
never what a generator spawns: bone piles, which spawn ghosts, stand on
The Last Ford, The Hidden Refuge, The Great River, The Dead Marshes
(four), The Ash Plains and The Mountain of Fire; tents, which spawn
skeletons, on all of those but The Dead Marshes; The Mountain of Fire has
a mage tower as well, and The Frozen Ford has no generator. The playtest
sweeps below run to tick 6,000 and see those later fights.

The three playtest sweeps (`scripts/westlands_playtest.sh` and its two
siblings) drive the text client over every level of a campaign on seeds
42, 1337 and 2025 for up to 6,000 ticks, with the setting on. Before these
fixes and after them, on the same level files: Westlands cleared its foes
on 29 of its 75 runs and on 14 after, ended with 39 crew members alive and
14 after, and lost a protected hero on 21 runs and on 26 after (levels 1,
2, 4, 8, 10, 11, 13, 17 and 20 read differently; levels 2 and 8 lost the
hero on all three seeds after the fixes and on none before); The Long
Season cleared 27 of its 57 runs and 23 after, with 58 crew members alive
and 43, and lost no hero (levels 1, 3, 10, 14, 15 and 19 read
differently); Imaginations cleared 3 of its 9 runs before and after, with
8 crew members alive and 9.

After the easing, on the eased level files: Westlands cleared its foes on
17 of its 75 runs, ended with 16 crew members alive and lost a protected
hero on 28 runs (at the state of 2026-11-17: 17, 14 and 27). Against the
game after the fixes four Westlands levels read differently, all of them
eased: The Last Ford ends with 2 crew members alive over its three runs
(none at the earlier points), The Hidden Refuge clears its foes on all
three runs again (on two after the fixes), The Great River on two (on none
after the fixes, on all three before them), and The Mountain of Fire loses
the Bearer, the campaign's protected hero, on two of its three runs, where
it lost him on none before these fixes or after them and on one at the
state of 2026-11-17 (half its steps). Why is not traced: the count of
its first 600 ticks reads the level back, and the Bearer is first counted
dead at ticks 2,400 and 2,700 (the sweep counts every 300 ticks). The
Forest Road cleared its foes on all three runs before these fixes and on
none since, and loses the Bearer on all three. The Long
Season (23 of 57 runs cleared, 43 crew members alive, no hero lost) and
Imaginations (3 of 9 cleared, 9 alive) read level for level as after the
fixes. A sweep is three seeds per level: a record, not the measure a level
is eased by, and no level was added to the easing because of it.

**The eased and kept levels on the finished game (2026-11-19).** The
easing above stands on the numbers read after it, which are the ones the
maintainer approved. The door rules came after it (Gameplay Fixes BF, a
bot leaves a door alone; BG, a guard passes a door or a grown tree by; and
BH, a bot's cast wants its pad clear of a door, a grown tree and a shot in
flight; see "The door rules" below): BF and BG reach no level of Westlands
or The Long Season, which place no door, no grown tree and no druid, but
BH does, with the setting on, through the barbarians' throws. On the
finished game the twelve levels that met the easing's three conditions
read, with the setting on, on the forty seeds (the docs' twenty in
brackets): Westlands 1 7.15 [7.20], The Forest Road 6.97 [6.85], The Last
Ford 7.38 [7.40], The Hidden Refuge 7.58 [7.60], The White City 0.55
[0.70] and The Dead Marshes 5.33 [4.95], as after the easing; The Ash
Plains 5.53 [5.25] and The Mountain of Fire 4.50 [4.45], the same
forty-seed mean as after the easing on other seeds ([5.15] and [4.55]
then); The Great River 5.38 [5.50] against 5.28 [5.10] after the easing
(+0.10, error 0.21); The Long Season 4 1.20 [1.15] against 1.38 [1.40]
(-0.17, 0.09), two errors under, less than the measure can prove; The
Frozen Ford 4.80 [4.85] against 4.90 [5.05] (-0.10, 0.09); and
Imaginations 3 7.45 [7.50] against 7.28 [7.25] (+0.17, 0.09), two errors
above. None of them is eased again.

The tower's snap-face (fix BE: a tower faces its foe at once) was measured
alone, with the setting on, on the docs' twenty seeds: the kept turn's
commit against the same commit with the tower's case taken out. The
Tower's five floors read the same survivors seed for seed; Imaginations 1,
whose two towers see a foe, reads 6.70 with it and 6.30 without (+0.40,
error 0.17); four of the fifty classic levels (Gladiator 8, 9 and 10 and
Tryxian 105) read other survivors on 7 to 12 of their twenty seeds, their
means inside their error (+0.45 (0.56), +0.15 (0.18), -0.15 (0.21)
and +0.10 (0.42)); the other 46 read the same seed for seed. No level
falls.

The arrow slit, measured both ways after these fixes with the setting on:
the line check takes a slit as open, as built, against a check that takes
the slit's gamble too. 24 of the 50 classic levels have slit cells (20 of
the 37 Gladiator levels the campaign lists, and Tryxian 103, 104, 108 and
115). On those 24, twenty seeds each: crew survivors 1,467 as built
against 1,494 (of 3,840); checks whose flight crossed a slit passed 13,801
and refused 9,192, against 11,231 and 11,244 (4,573 of those test flights
lost the gamble); real shots that reached a slit 7,217, of which 2,043
broke on it, against 6,840 and 1,443. Golems started 373 shots against
255, fire elementals 2,156 against 1,760. As built no check is refused at
a slit although the shot would land; with the gamble 672 of the levels'
shooters' checks and 110 of the crew's are, every one at a slit. One slit
level moves by two errors, Gladiator 3 (0.70 survivors as built against
1.50, error 0.39), one level in 24, which is what chance gives; with the
gamble the campaigns move by Gladiator +1.45 (1.64) and Tryxian -0.10
(0.50), per run of the campaign. On The
Tryxian Chronicles' first level (six seeds of 2,400 ticks) the crew ends
with 36 alive against 37 (of 72). The classic check, the setting off, on
the same 24 levels: crew survivors 1,599, checks over a slit passed 11,612
and refused 15,662.

**Gladiator and The Tryxian Chronicles.** No calibration suite plays them,
so `tests/unit/test_classic_campaigns_calibration.cpp` measures every level
the two campaigns list with the same stand-in crew, 600 ticks on the docs'
twenty seeds, before these fixes and after them. Its crew level per level
(`kClassicCrewLevel`) is a measuring device, fitted once on the game
before these fixes as the level at which the eight average nearest four
survivors on three seeds; it says nothing about what crew a player brings.
With the setting off both campaigns read exactly as before, seed by seed
(they have one floor, so #512 cannot act). With the setting on, summed per
run of the campaign, Gladiator reads 127.40 before and 117.50 after (m
-9.90, se 3.72) and The Tryxian Chronicles 44.45 and 38.90 (-5.55, 1.33).
On twenty seeds these levels fell by two errors or more: Gladiator 7 (3.85
to 2.30) and 18 (6.25 to 3.40), Tryxian 105 (2.10 to 0.60) and 111 (3.95
to 0.15); Tryxian 108 went from 0.45 to 0.00 (2.9 errors, under one
survivor before); Gladiator 8 (3.15 to 4.50) and 40 (2.70 to 4.15) rose by
two errors or more. Nothing there is eased: the maintainer's ruling leaves
the classic campaigns as they are (entry 2 of the
[Rulings (2026-11-16)](#rulings-2026-11-16)). In the table survivors are
the mean per battle with its error, and damage taken is the crew's mean
per battle, rounded; with the setting off the two games read the same.

| campaign, level | crew | survivors, on: before | after | difference | damage taken, on: before | after | setting off: survivors | damage taken |
|---|---|---|---|---|---|---|---|---|
| Gladiator 1 | 1 | 7.75 (0.10) | 7.85 (0.08) | +0.10 (0.14) | 278 | 299 | 7.70 (0.11) | 415 |
| Gladiator 2 | 1 | 1.45 (0.52) | 2.20 (0.55) | +0.75 (0.68) | 1,290 | 1,253 | 6.40 (0.31) | 672 |
| Gladiator 3 | 1 | 1.15 (0.33) | 0.70 (0.24) | -0.45 (0.23) | 1,563 | 1,586 | 3.65 (0.56) | 1,458 |
| Gladiator 4 | 1 | 7.55 (0.15) | 7.15 (0.22) | -0.40 (0.27) | 737 | 829 | 7.65 (0.11) | 723 |
| Gladiator 5 | 1 | 3.95 (0.55) | 4.05 (0.52) | +0.10 (0.75) | 1,085 | 1,090 | 4.55 (0.48) | 1,060 |
| Gladiator 6 | 4 | 4.80 (0.63) | 5.40 (0.60) | +0.60 (0.88) | 1,844 | 1,634 | 5.45 (0.56) | 1,797 |
| Gladiator 7 | 2 | 3.85 (0.66) | 2.30 (0.42) | -1.55 (0.74) | 1,610 | 1,767 | 4.05 (0.44) | 1,693 |
| Gladiator 8 | 7 | 3.15 (0.38) | 4.50 (0.46) | +1.35 (0.45) | 2,537 | 2,387 | 4.55 (0.48) | 2,196 |
| Gladiator 9 | 3 | 4.80 (0.60) | 3.40 (0.72) | -1.40 (0.81) | 1,748 | 1,915 | 3.60 (0.69) | 1,729 |
| Gladiator 10 | 9 | 6.15 (0.30) | 6.20 (0.25) | +0.05 (0.43) | 1,596 | 1,618 | 5.95 (0.28) | 1,695 |
| Gladiator 11 | 9 | 4.65 (0.53) | 4.90 (0.39) | +0.25 (0.61) | 2,492 | 2,330 | 4.70 (0.39) | 2,442 |
| Gladiator 12 | 1 | 0.00 (0.00) | 0.00 (0.00) | 0.00 (0.00) | 1,305 | 1,286 | 0.00 (0.00) | 1,292 |
| Gladiator 13 | 12 | 3.80 (0.68) | 2.65 (0.65) | -1.15 (1.01) | 3,292 | 3,504 | 2.40 (0.60) | 3,590 |
| Gladiator 14 | 3 | 4.45 (0.47) | 3.05 (0.43) | -1.40 (0.72) | 1,712 | 1,893 | 5.10 (0.35) | 1,640 |
| Gladiator 15 | 2 | 5.20 (0.43) | 4.85 (0.48) | -0.35 (0.67) | 1,219 | 1,209 | 5.75 (0.22) | 1,134 |
| Gladiator 16 | 15 | 1.70 (0.45) | 0.95 (0.33) | -0.75 (0.53) | 4,376 | 4,759 | 0.95 (0.26) | 5,033 |
| Gladiator 17 | 2 | 1.50 (0.56) | 0.60 (0.35) | -0.90 (0.73) | 1,306 | 1,401 | 1.70 (0.52) | 1,321 |
| Gladiator 18 | 1 | 6.25 (0.23) | 3.40 (0.41) | -2.85 (0.41) | 508 | 950 | 6.35 (0.33) | 464 |
| Gladiator 19 | 7 | 2.05 (0.42) | 2.25 (0.42) | +0.20 (0.61) | 3,021 | 2,935 | 2.90 (0.54) | 2,848 |
| Gladiator 20 | 10 | 1.05 (0.44) | 0.55 (0.33) | -0.50 (0.55) | 3,057 | 3,089 | 0.45 (0.33) | 3,141 |
| Gladiator 21 | 12 | 4.35 (0.42) | 3.80 (0.55) | -0.55 (0.54) | 3,437 | 3,438 | 4.40 (0.43) | 3,442 |
| Gladiator 22 | 8 | 3.15 (0.55) | 4.25 (0.67) | +1.10 (0.84) | 3,079 | 2,784 | 3.70 (0.59) | 3,080 |
| Gladiator 23 | 15 | 2.50 (0.49) | 1.60 (0.47) | -0.90 (0.68) | 4,605 | 4,471 | 1.70 (0.44) | 4,727 |
| Gladiator 24 | 4 | 5.15 (0.53) | 5.70 (0.41) | +0.55 (0.67) | 1,511 | 1,460 | 3.35 (0.64) | 2,146 |
| Gladiator 25 | 4 | 1.25 (0.35) | 0.75 (0.32) | -0.50 (0.47) | 1,946 | 1,964 | 1.40 (0.28) | 1,941 |
| Gladiator 30 | 5 | 2.40 (0.41) | 3.05 (0.45) | +0.65 (0.55) | 1,717 | 1,490 | 3.10 (0.45) | 1,617 |
| Gladiator 35 | 10 | 2.45 (0.54) | 1.70 (0.47) | -0.75 (0.68) | 3,479 | 3,872 | 1.35 (0.37) | 3,625 |
| Gladiator 36 | 9 | 3.05 (0.61) | 3.55 (0.66) | +0.50 (0.85) | 3,101 | 2,907 | 1.10 (0.38) | 3,914 |
| Gladiator 37 | 13 | 4.45 (0.37) | 4.40 (0.45) | -0.05 (0.61) | 3,169 | 3,076 | 4.80 (0.46) | 3,230 |
| Gladiator 38 | 1 | 0.00 (0.00) | 0.00 (0.00) | 0.00 (0.00) | 1,260 | 1,255 | 0.00 (0.00) | 1,282 |
| Gladiator 39 | 10 | 0.10 (0.07) | 0.00 (0.00) | -0.10 (0.07) | 3,129 | 3,101 | 0.00 (0.00) | 3,106 |
| Gladiator 40 | 15 | 2.70 (0.47) | 4.15 (0.35) | +1.45 (0.57) | 4,133 | 3,773 | 3.05 (0.50) | 4,072 |
| Gladiator 41 | 3 | 2.40 (0.43) | 2.25 (0.37) | -0.15 (0.52) | 1,913 | 1,958 | 2.20 (0.41) | 1,855 |
| Gladiator 42 | 5 | 4.75 (0.45) | 4.30 (0.57) | -0.45 (0.84) | 2,019 | 1,859 | 4.30 (0.49) | 1,973 |
| Gladiator 43 | 2 | 1.80 (0.53) | 1.25 (0.41) | -0.55 (0.71) | 1,872 | 1,746 | 1.85 (0.54) | 1,810 |
| Gladiator 44 | 8 | 5.05 (0.58) | 4.05 (0.64) | -1.00 (0.84) | 2,367 | 2,419 | 5.45 (0.44) | 2,211 |
| Gladiator 45 | 2 | 6.60 (0.41) | 5.75 (0.45) | -0.85 (0.57) | 687 | 748 | 6.70 (0.36) | 506 |
| Tryxian 103 | 5 | 6.70 (0.35) | 7.05 (0.39) | +0.35 (0.53) | 647 | 668 | 7.60 (0.17) | 486 |
| Tryxian 104 | 1 | 7.45 (0.28) | 7.45 (0.26) | 0.00 (0.38) | 198 | 225 | 7.20 (0.25) | 224 |
| Tryxian 105 | 13 | 2.10 (0.51) | 0.60 (0.31) | -1.50 (0.48) | 3,932 | 4,585 | 1.95 (0.57) | 3,896 |
| Tryxian 106 | 3 | 7.50 (0.21) | 7.75 (0.12) | +0.25 (0.25) | 329 | 199 | 6.90 (0.31) | 644 |
| Tryxian 107 | 5 | 6.60 (0.37) | 7.35 (0.33) | +0.75 (0.43) | 702 | 460 | 0.05 (0.05) | 2,330 |
| Tryxian 108 | 14 | 0.45 (0.15) | 0.00 (0.00) | -0.45 (0.15) | 4,082 | 4,118 | 0.45 (0.17) | 4,196 |
| Tryxian 109 | 15 | 0.05 (0.05) | 0.00 (0.00) | -0.05 (0.05) | 4,769 | 4,489 | 0.00 (0.00) | 4,629 |
| Tryxian 110 | 7 | 2.70 (0.52) | 2.25 (0.47) | -0.45 (0.71) | 2,177 | 2,422 | 3.20 (0.35) | 2,393 |
| Tryxian 111 | 3 | 3.95 (0.46) | 0.15 (0.08) | -3.80 (0.44) | 1,368 | 1,920 | 3.65 (0.50) | 1,442 |
| Tryxian 112 | 6 | 3.45 (0.60) | 3.40 (0.69) | -0.05 (0.77) | 2,554 | 2,427 | 1.10 (0.40) | 2,777 |
| Tryxian 113 | 1 | 0.00 (0.00) | 0.00 (0.00) | 0.00 (0.00) | 1,315 | 1,297 | 0.00 (0.00) | 1,297 |
| Tryxian 114 | 13 | 3.30 (0.48) | 2.80 (0.42) | -0.50 (0.59) | 3,421 | 3,782 | 2.25 (0.31) | 4,416 |
| Tryxian 115 | 6 | 0.20 (0.12) | 0.10 (0.07) | -0.10 (0.14) | 2,291 | 2,267 | 0.10 (0.07) | 2,266 |

**The mode courts.** The twelve bot games of the seven modes (CTF, Team
Deathmatch, Free For All, Mutant, the six basketball courts 824 to 829,
soccer and Onslaught) were each played before these fixes and after them,
with the setting on (`OG_MODES_GAMEPLAY_FIXES=1`) and as CI plays them,
with the setting off. Each is one stream, so the lines show whether the
games still work, not by how much they changed. Every game's own test
passes on both values, before and after, and with the setting off every
line reads as before. With it on: CTF's captures 1 and 1 before, 1 and 0
after; Team Deathmatch's kills 4 and 3, then 3 and 4; Free For All's frags
16, then 18; Mutant one crowning both times; the basketball courts'
baskets 6 then 7 (court 824), 9 then 10 (825, whose game ends on tick
8,155 against 7,690), 19 then 15 (826), 9 and 9 (827), 5 then 7 (828) and
4 and 4 (829), the winning team changing on five of the six courts.
Soccer and Onslaught: each game's own test holds with the setting on.

**The door rules (2026-11-19).** Three rules of these fixes leave doors
alone with the setting on: Gameplay Fixes BF, a bot leaves a door alone
(its line check is refused at an enemy door, it starts no swing at a door
it walks into, and a blow on a door names no foe); Gameplay Fixes BG, a
guard passes a door or a grown tree by; and Gameplay Fixes BH, a bot's
cast wants its pad clear of a door, a grown tree and a shot in flight.
They were measured on the finished game with the setting on and off, and
with the setting on in a measuring build of it, never committed, that can
switch each rule off. With every rule switched off it plays seed for seed
as the game before any door rule (Gladiator and The Tryxian Chronicles
read as after fixes BC, BD and BE, the "after" column of the table above;
Imaginations as after the easing; level play's six lines as they read
then); with the line check's part of BF alone, as the game first measured
on 2026-11-19; and with every rule but BH, as the game read with BF and BG
before BH was added (level play's 11 refusals, below). The numbers come in
threes, in this order: before any door rule, with the line check's part
alone, and with every rule.

With the calibration suites. With the setting off every row of every suite
reads as before, seed for seed, survivors and damage taken: Westlands, The
Long Season, Imaginations and the Tower as after the easing, and Gladiator
and The Tryxian Chronicles as before these fixes; and the twelve mode bot
games read their lines as before with either setting, each game's own test
passing. Westlands, The Long Season and the Tower place no door, no grown
tree and no druid, so BF and BG cannot reach them: with BH switched off
and every other rule on, each of their rows reads, seed for seed, what it
read after the easing. BH reaches them through the barbarians' throws: on
the forty seeds an enemy shot in flight on the pad held a barbarian's
throw 1,451 times on Westlands (194 of them the crew's barbarian's), 1,123
on The Long Season (307) and 86 on the Tower (85). So with the setting on
their runs change: Westlands reads otherwise than after the easing on 144
of its 1,000 level runs, The Long Season on 153 of 760 and the Tower on 21
of 200, in survivors or damage taken. No level fell by the measure's rule
and none rose by two errors. Per run of the campaign Westlands reads 95.90
survivors against 96.03 after the easing (m -0.12, se 0.41), The Long
Season 108.55 against 109.15 (-0.60, 0.35) and the Tower 10.38 against
10.43 (-0.05, 0.08); The Long Season 4 reads 1.20 against 1.38 (-0.17,
0.09), two errors under, less than the measure can prove, and the largest
fall there, The Long Season 5, reads 5.72 against 6.15 (-0.42, 0.22),
under two errors. The twelve levels the easing judged are given above as
they read on the finished game; nothing is eased again. Imaginations
places doors, but in its counted ticks no check met one, no blow landed on
one and no door went, at any of the three points, so BH alone moves its
survivors (an enemy shot in flight on the pad held a barbarian's throw 84
times on the twenty counted seeds, 74 of them the crew's barbarian's):
level 3 reads 7.45 against 7.28 (+0.17, 0.09), two errors above, and
levels 1 and 2 read 6.00 and 8.00 against 5.97 and 8.00.

Level play (The Tryxian Chronicles' first level, six seeds of 2,400
ticks): casters with two or more specials 211, 222 and 232 pooled (the
floor is 180), and 212 with every rule but BH; the crew alive at tick 2400
by seed 11, 8, 3, 3, 5 and 6 (36), 7, 8, 9, 8, 8 and 7 (47), and 9, 11, 7,
8, 9 and 7 (51), and with every rule but BH 8, 8, 7, 10, 10 and 10 (53).
Six seeds prove little about survivors: the campaigns' twenty and forty
seeds above and below are the measure. The druid's, barbarian's, mage's
and archer's casts refused `COULD NOT FIRE` after their rule's yes read 0,
0 and 0 there, and 11 with every rule but BH. The playtest sweeps, with
the setting on, against the eased files as they read before the door
rules: Westlands differs on 3 of its 25 levels (cleared 17 and 17 of its
75 runs, crew alive 16 and 16, a protected hero lost on 28 and 27 runs),
The Long Season on 4 of 19 (cleared 23 and 23, crew alive 43 and 44), and
Imaginations on one level, The Castle of Gold (the campaign cleared 3 and
2 of its 9 runs, crew alive 9 and 6). On Westlands and The Long Season,
which place no door, no grown tree and no druid, only BH can have moved
the sweeps (the sweeps were not played with BH switched off). A sweep is
three seeds per level: a record, not a measure.

Gladiator and The Tryxian Chronicles, the setting on, on the docs' twenty
seeds, per run of the campaign: Gladiator reads 117.50, 116.40 and 116.65
survivors (with every rule against before any door rule m -0.85, se 2.12),
The Tryxian Chronicles 38.90, 39.20 and 38.65 (-0.25, 0.66). On twenty
seeds two errors is the most the measure can say: no level fell at two
errors against any point, and five rose by two errors against at least one
of them; nothing there is eased. The largest fall is Gladiator 36's, 3.55
to 2.00 against both earlier points (m -1.55, se 0.83), under two errors.
None of its 60 doors goes at any point; with every rule its bots no longer
wear them down (3,517 hit points lost at each earlier point, none now),
and a locked door refused a step of one of its bots on 10,675 bot-ticks (a
bot-tick is one tick of one bot) against 3,584, none of them the crew's.
In the table the crew's survivors per battle at four points, and what
changed in the level:

| level | before any door rule | the line check's part alone | every rule but BH | every rule | rose by two errors against | what changed |
|---|---|---|---|---|---|---|
| Gladiator 10 | 6.20 | 6.65 | 6.65 | 6.70 | before any door rule, +0.50 (0.24) | with the line check's part already; none of its 140 doors goes, and they lose 1,260, 456 and 82 hit points |
| Gladiator 37 | 4.40 | 3.85 | 5.65 | 5.60 | the line check's part alone, +1.75 (0.66) | its 180 doors all stand at tick 600, where a guard erased 82 and 94 of them; a locked door refused a step of a bot, the crew's included, on 72,047 bot-ticks against 22,393, the bot's foe beyond the door on 71,965 against 8,276; and 297 of its 500 hostiles are alive at the end against 149 |
| Gladiator 41 | 2.25 | 1.25 | 1.90 | 2.10 | the line check's part alone, +0.85 (0.36) | back to before any door rule (-0.15, 0.54); none of its 300 doors goes at any point; a locked door refused a step of a bot on 3,573 bot-ticks against 1,309, the bot's foe beyond the door on 3,300 against 714, the crew's 2,212 and 710 of them |
| Gladiator 44 | 4.05 | 4.05 | 4.05 | 5.00 | each of the three earlier points, +0.95 (0.46) | BH alone (the level has no door): an enemy shot in flight on the pad held 117 casts, the crew's barbarian's throw 103 times, the level's druids' GROW TREE 13 times and SUMMON FAERIE once, and 74 of its 700 hostiles are alive at the end against 129 |
| Gladiator 45 | 5.75 | 6.15 | 5.60 | 5.85 | every rule but BH, +0.25 (0.12) | BH: an enemy shot in flight on the pad held the crew's barbarian's throw 14 times |

Against the game before these fixes, with every rule and the setting on,
Gladiator reads 116.65 survivors per run of the campaign against 127.40 (m
-10.75, se 4.26) and The Tryxian Chronicles 38.65 against 44.45 (-5.80,
1.38). On the twenty seeds these levels read two errors or more under:
Gladiator 7 (3.85 to 2.05), 13 (3.80 to 2.15), 14 (4.45 to 2.95) and 18
(6.25 to 4.00), Tryxian 105 (2.10 to 0.45) and 111 (3.95 to 0.15); Tryxian
108 reads 0.05 against 0.45 (under one survivor before); Gladiator 8 (3.15
to 4.30) and 40 (2.70 to 4.15) read two errors or more over. Gladiator 13
and 14 are new in that list: before any door rule they read 2.65 and 3.05,
under two errors. Nothing there is eased.

What the rules do at doors was counted in the same measuring build, the
setting on: on Gladiator and The Tryxian Chronicles (twenty seeds of each
level, 600 ticks; the numbers below are theirs unless said), on level
play, and on the four shipped Onslaught courts, each played once from one
seed by the mode's own bots for up to 3,600 ticks (the four ran 9,647,
9,330 and 10,558 ticks in all; the Onslaught bot game of the tests plays a
court with no door).

- Checks that passed on an enemy door: 117,386, 80,470 and 0 (on a door
  that was not the shooter's foe 50,009, 0 and 0); level play 7,981,
  11,725 and 0; the Onslaught courts 958, 997 and 0. Counted as the
  question was put to the maintainer (checks that passed although no
  fighter or tent stood in the flight and ended on an enemy door): 98,695,
  76,688 and 0. A door refused 0, 30,080 and 36,568 checks (level play 0,
  1,094 and 14,709).
- A door became a bot's foe by the bot's own shot landing on it 9,238,
  5,107 and 0 times, and otherwise 3, 2 and 0; bot-ticks with a door as
  the bot's foe 492,467, 464,159 and 0, so the exception for a door that
  is the foe is no longer reached in stock play. Swings and shots started
  with a door as the bot's foe: 36,240, 28,809 and 0.
- A bot's swing at a door it walked into: 20,616, 22,585 and 0, of them at
  a door that already was its foe 7,341, 6,488 and 0 (level play 667, 700
  and 0; the Onslaught courts 1,136, 911 and 0). The measuring crews seat
  their first four fighters while nobody drives them, and a walker with a
  seat number is a player's fighter to the rule (#593): those four swung
  at a door they walked into 149, 311 and 580 times, the door their foe or
  not.
- Blows that landed on a door: the bots' from beside it 18,610, 16,834 and
  152, and from a distance 19,754, 14,302 and 499; the seated fighters'
  558, 512 and 553. The doors' hit points lost: 1,801,741, 1,405,275 and
  46,309, of them the seated fighters' 12,223, 12,991 and 14,848 (level
  play 117,396, 124,911 and 832; the Onslaught courts 13,234, 10,187 and
  163); the blows of shots whose thrower had died, 8, 2 and 2, with 353,
  120 and 65 hit points, are in neither count.
- What still strikes a door for a bot with every rule, by cause (651
  blows): the archer's FIRE ARROWS, a special whose eight fire arrows fly
  every way at once, 381; other specials 114, of them 101 cast from a
  distance at a foe (the archers' BARRAGE 82 and the elves' rocks 19; no
  special's rule asks the path to its foe, #591), 12 that fly every way at
  once (the fire elementals' STARBURST 11 and a faerie's GLIMMER 1) and 1
  a fire elemental's own pad; a shot or a swing started at a foe that met
  a door on its way 140, a soldier's knife coming back to him among them;
  a pad that held both the foe and a door 16; a CHARGE whose last step met
  a door none (2 before any door rule); a tent's spawn none at any point;
  and 2 more came from a knife and a rock whose thrower had died. Blows on
  a door that was the bot's foe: 18,204, 17,186 and 0. Level play: 16
  blows (a special 3, strays 13); the Onslaught courts 10 (a shared pad 3,
  strays 7).
- Every door that went, with its cause: of the 6,680 that stood at the
  start of the classic battles (each level's doors counted once for each
  of its battles), 178, 181 and 2 were gone at tick 600: erased by a guard
  144, 161 and 0, broken under blows 26, 14 and 0, opened with a key 8, 6
  and 2, and nothing else took one; level play 38, 51 and 0 of 180 (a
  guard 33, 39 and 0, blows 5, 12 and 0); the Onslaught courts none at any
  point. Grown trees: the druids grew 75, 70 and 65, and 42, 34 and 25 of
  them went, erased by a guard 20, 15 and 0 and cut down 22, 19 and 25.
  Circles of protection: 260 stood at the start (Tryxian 107's thirteen in
  each of its twenty battles), no bot cast one in any measured battle, 13
  went at every point, each with its hit points spent, and an enemy guard
  erased none at any point (#592: a guard still erases one; nothing is
  built for it).
- A bot with no key at a door it cannot open (#582): the ticks in which a
  locked door refused a step of a bot, whatever its foe, went 101,179,
  109,216 and 233,215 for the levels' bots and 800, 1,633 and 3,632 for
  the crew (level play 2,959, 3,324 and 24,203, and for the crew 116, 9
  and 653; the Onslaught courts 4,675, 3,747 and 8,310). Of them, the
  ticks in which the bot's live foe on its floor, not a door, stood
  farther off than the door went 35,520, 42,638 and 210,419 for the
  levels' bots and 477, 939 and 3,344 for the crew (level play 1,129,
  1,468 and 23,961; the Onslaught courts 780, 785 and 6,832). At the first
  two points this second count leaves out a bot whose foe was the door
  itself, and a door was then a bot's foe on 492,467 and 464,159 bot-ticks
  (above). The first count reads low at those points too: a bot that swung
  at the door it walked into (20,616 and 22,585 swings, above), or that
  stood beside a door that was its foe and swung or shot at it, spent the
  acts that finish each swing beside the door and tried no step in them,
  because a walker finishes its swing before it does anything else; with
  every one of the three rules no bot started a swing or a shot at a door
  (Gameplay Fixes BL, built later, makes a door a bot's foe as a last
  resort, and the bot then strikes it). So the first
  count's rise, 2.3 times what it read before any door rule, and the
  second's, 5.9 times, are both more than the rise in the time bots stand
  at doors, which was not measured; the stays below are made of the second
  count's ticks. With every rule, the crew's included, the archers have
  85,330 of the second count, the soldiers 40,295, the elves 24,834, the
  druids 24,127, the skeletons 16,895 and the small slimes 11,486, most on
  Gladiator 37 (71,965), 22 (33,661) and 11 (20,766) and Tryxian 109
  (18,597). Counted as stays (from a bot's first such tick to its last,
  with no gap of more than 20 ticks), the crew's included: 5,000, 5,590
  and 10,422; with every rule 9,237 of them ended within 60 ticks, 593
  within 600, 547 lasted to tick 600 and in 45 the bot died there. By
  count most stays are short, by time they are not: the 840 stays longer
  than 60 ticks, 8 in 100, span 182,542 of the 264,369 ticks that all the
  stays span, 69 in 100 (before any door rule 172 of 5,000 stays and
  19,086 of 56,816 ticks, 34 in 100). Of the 547 still there at tick 600,
  303 had lasted 60 ticks or less and 244 longer, 385 ticks on average
  (before any door rule 9 of 137 had lasted longer). On level play 2,843
  stays: 2,756 ended within 60 ticks, 61 within 600, 15 lasted to tick
  2,400 and in 11 the bot died; the 62 longer than 60 ticks span 7,876 of
  the 33,907 ticks the stays span.
- The casts: the druid's, barbarian's, mage's and archer's casts refused
  `COULD NOT FIRE` after their rule's yes 44, 41 and 0, and 47 with every
  rule but BH. With every rule BH said no on a pad the plain question
  passed 1,312 times: the barbarian's throws 1,115 (an enemy shot in
  flight on the pad 1,111, a door 4), GROW TREE 149 (a shot 130, a door
  19), SUMMON FAERIE 47 (a door 25, a shot 22; the faerie's room said no
  to each as well), ENERGY WAVE 1 and EXPLODING BOLT none; on level play
  71 (GROW TREE 38, SUMMON FAERIE 30, ENERGY WAVE 3, a door on the pad all
  but once).
- Grown trees and enemy shots in flight still pass a check on their clash
  roll, as the maintainer ruled.

With the line check's part alone, as it was first measured: the door
refused 30,080 checks, 22,021 of them the ATTACK order's, the levels'
archers 12,303, elves 5,307, skeletons 4,707, soldiers 2,675 and druids
1,853, 14,661 of them on the pad; on the ticks in which a door refused a
bot's check the bot moved on 15,331, turned on 861 and stood on 13,652;
blows on fighters per 100 ticks near a foe hardly moved for those shooters
(the archers 3.10 to 3.09, the elves 4.96 to 5.00, the skeletons 5.17 to
5.15, the soldiers 4.42 to 4.46; with every rule they read 3.21, 5.45,
5.31 and 4.61, but at the first two points a door could be a bot's foe, so
their ticks near a foe hold ticks near a door); and Imaginations'
survivors read the same on every seed, the crew's damage taken on The
Castle of Gold moving on two of the forty seeds, 11 and 106: on the twenty
counted seeds, seed 11 among them, no check met a door, so what moved them
is the clash roll the rule no longer draws for a test weapon near a door
(that roll is drawn for every enemy weapon-order body in the 32-pixel map
cells the test weapon's box covers, before their boxes are compared),
which moves the random stream; and on twenty seeds 19 classic levels
changed, Gladiator 41 falling at two errors (2.25 to 1.25) and Gladiator
10 (6.20 to 6.65) and Tryxian 103 (7.05 to 7.45) rising by two errors.

What the three rules did not do, with their numbers: a bot whose foe was
behind a door it could not open walked into the door and was stopped
there, as at a wall, and stayed (#582; the stays above); a door still
takes the blows of strays (above); a door that a level script or a mod
named a bot's foe was fought as before, which no stock level does (no
bot-tick with a door as its foe, above); a grown tree or an enemy shot in
flight still passes a check; and a guard still erases a circle of
protection (#592), though no bot cast one in any measured battle.
Gameplay Fixes BJ and BL, built later, change the first and the third: a
bot lets go of a foe that only a locked door keeps it from and, with no
enemy it can get at, is handed the door in its way as its foe; and a door
a script names a bot's foe is let go like any other once the bot can get
at an enemy or the door leads to none.

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
5. **#354 is fixed in the current game only.** Ruling: leave it. (Later
   overtaken by #450, which asked for fix M's seat half in the old game
   too: ported there behind the setting in `787eb516`; no golden moved.)
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
    cast. Default: the follow-up, not this work. Taken on 2026-11-05 in
    this work after all, as fix AE (the setting-off reply ignores the
    answer; `archmage_bot_struck_scen99`, the setting-off twin captured from
    the old game, is its proof).
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
    is re-pinned lower. Default: so. **Update (2026-11-02):** the maintainer
    deleted the survivor pins instead. The four calibration suites
    (`tests/unit/test_westlands_calibration.cpp`,
    `test_longseason_calibration.cpp`, `test_imaginations_calibration.cpp`,
    `test_tower_calibration.cpp`) held one chaotic trajectory, seed 42,
    against survivor floors and ceilings cut from itself; on the three rows
    measured over a hundred seeds the unchanged game misses its own band on
    5 to 28 of them (Westlands 9 on 5, Westlands 11 on 24, Westlands 5 on 28
    in the T to W pass), so the suites went red on nearly every simulation
    change and were re-pinned eight times since July. A lottery is not a
    rule; the semantic tests (`tests/unit/test_sem_*.cpp`) now assert every
    game rule directly. Gone: every floor and ceiling, and Long Season's
    Ashfall Fair defense band (team 0 at four or more standing at tick 3000
    on three seeds, the same kind of survivor count). Asserted exactly on
    every row of all four suites, as the Westlands suite already did: the
    level loads and deploys the whole crew, it fields an army (on the Tower,
    at least a third of the generator's foe count), and the crew takes and
    deals damage. The measuring modes (`WESTLANDS_CALIBRATION_MEASURE=1` and
    its siblings) stay as tools: they assert nothing and run only when
    asked. The two Westlands lines this ruling was asked about go with the
    pins; no level was eased and none is re-pinned.
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

## Rulings (2026-11-08)

The first review round's issues (#426 to #451, with #440, #491 and #493 from
the second) asked five questions in the chat and took the rest as defaults.
Each line below is what the maintainer said, or the default taken, and what
the game does today.

1. **#450: a dead hero's seat and a charmed walker.** Refuse charmed
   characters for the hand-off, but a seat already driving one keeps it.
   This is fix M's seat half asked again, so it gets no letter of its own:
   with the setting on the death hand-off skips a walker that is on the team
   only because it is charmed, and a seat on a walker whose charm runs out
   keeps it, on both settings.
2. **#440: the kill experience.** Documented under gameplay fixes and left
   as it is: the old game pays the action's experience and the kill bonus,
   this game the kill bonus alone since #78. Not behind the setting.
3. **#491: the free-exit level type.** The level type stays a set of flags,
   so a level that carries the free-exit bit with other bits may be left at
   any time. Recorded as a difference kept on purpose; the old game reads
   the bit too with the setting on since fix AJ's port.
4. **#493: allies and the difficulty.** Allies are not scaled: no walker on
   the player's team takes the difficulty percent, a level-placed NPC, a
   summon or their shots. Fix A12a and the #473 shot rule are reverted here
   and the old game's copy of that rule is reverted too, so both games scale
   the other teams only, as the original did. Open: whether a human team
   other than team 0 in the multiplayer modes should be exempt as well.
5. **#448 and #449: two parity facts.** Fixed on the test side: a team count
   counts livings only, so a TELEPORT MARKER no longer counts, and the
   treasure-taken fact is retired for the count form that can fail. The
   rows that used them were derived again from their old-game recordings.
6. **Defaults taken, no question asked.** AO (#426) a rush's strike leaves a
   dead body as it lies; AP (#429) one owner-chain climb, through a dead
   owner, stopping at a charmed head; AQ (#434) a seat's wall slide asks
   each half quietly before it steps; AR (#435) the druid bot asks whether
   the faerie has room; AS (#436) the archer bot's BARRAGE asks the pad and
   gives the price back when the first arrow cannot leave; AT (#446) a
   walker asked to face the way it already faces stays put; AV (#451) the
   elf bot's volley rules say no to any enemy weapon on the pad; #431 drops
   the bone tent as owner of the isle's defectors; #438 re-stages two
   arenas and records them again from the old game.
7. **Four setting-on recordings took a new seed** when fix AT moved the
   stream and on the old seed the row no longer reached what it tests:
   `archmage_mind_control_berserk_fixes_scen99` (0xB0),
   `cleric_raise_blocked_corpse_fixes_scen99` (0x14),
   `thief_bot_charm_or_taunt_fixes_scen99` (0x6B) and
   `cleric_bot_resurrect_foe_stain_fixes_scen99` (0x3). Their setting-off
   twins keep their seeds. The maintainer has been told; the alternative
   (a pin inside the block the row is about) exists for the archmage row
   only.
8. **One test reworked rather than re-pinned.** The mid-game ADD PLAYER
   case asserted how many livings a 420-tick brawl leaves; it now asserts
   only what it needs (both heroes stand, the lead team is those two, every
   re-teamed walker is on the board).

## Rulings (2026-11-14)

The third bugs batch (#494 to #505 and #508, with #398, #432, #441, #447
and #490) asked its questions in the chat. The maintainer first said three
things: take the proposed issues, leave the flaky tests for a separate
round, and add #490. On 2026-11-15 he answered the rest, one question at a
time. Each entry below is his answer and what the game does because of it;
where an entry was only told to him, or is still open, it says so.

1. **#447: what the count was.** The issue counted, on Westlands 7, 16 and
   22, the ticks a melee-only bot stands within 30 px of its live foe
   without a swing. Traced over all 25 levels before this batch's fixes
   (setting off / on: 28,158 / 14,882 such ticks), 17,520 / 9,112 are ghosts
   whose foe is on another floor of a level with several floors: a flyer
   never changes floors and the distance does not see floors. That is all of
   level 7 (the ghosts fly on the ground north of the wall; the crew they
   hold stands on the wall's gallery one floor up, and the hostiles left on
   the ground, some of the crew and the level's mages, stand beyond the
   ghosts' quick foe search), 94 percent of level 22 (where, on every such
   tick, no hostile stood on the ghosts' floor anywhere on the map) and 70
   percent of level 16. It is this game's rule of floors, not a classic
   stall, and the old game cannot show it. One part of it was a bug and is
   fixed on both settings: a flyer that had a foe on its own floor within
   its quick foe search (about 160 px) kept the one it could not reach.
   Beyond that reach it still keeps it, so the change is partial (see
   GAMEPLAY_FIXES_FROM_CLASSIC.md; #512 later took that part, see
   [Not gated](#not-gated)). The rest of the count, on one floor with
   nothing between, is a melee-only bot standing in its ATTACK order, which
   only faces the foe (both games); see the next entry. None of it was a
   guard at its post, a frozen bot or a bot that did not act. The issue's
   own figures (4587, 2464, 1492) could not be reproduced with its stated
   definition (8914, 3548, 2921). With fix BB the "no swing started" count
   rises with the setting on (to 25,121 over the 25 levels), because a swing
   at the air no longer counts as fighting. The other count, "no blow landed
   for 60 ticks", reads 15,218 on one floor after fix BB against 11,259
   before it, and the two figures are not like for like: that count skips a
   bot while it is busy, and every swing fix BB removes made its bot busy
   for a few ticks, which now count. How much of the rise that accounts for
   was not measured; blows landed and damage per 100 ticks near a foe did
   not fall (see [Bots](#bots)).
2. **#447: the melee-only bot's ATTACK order.** Left for a later round of
   its own (the maintainer: "let's do A and handle that later"); #447 stays
   open. A melee-only bot has no aimed blow: its ATTACK order only faces
   its foe, and its blows are its bumps. Letting it swing in that order
   when a foe's body stands on its weapon's pad, and step toward the foe
   otherwise, was built and measured: it halves the one-floor stall (11,259
   to 5,023 ticks with no blow landed for 60) and doubles what these bots
   do, per 100 ticks near a foe from 0.97 blows and 21.1 damage to 2.06 and
   44.3, kills from 0.024 to 0.080, and the Westlands crews lose a fifth of
   their survivors (322 to 255 over 75 battles; level 19 from 18 to 4,
   level 23 from 16 to 3). That is a change of the campaigns' difficulty,
   so it waits for a round that eases the campaigns' levels with it.
3. **Whose side a thing is on (#502, and with it #496 and #533).** The
   batch first fixed #502 by making a blow's score ask its thrower's team.
   The maintainer rejected that: "why can't the blast track its own team
   number independently?", and "everything should just work normally as
   expected, including arrows and other shots. They should all stay with
   the team they were when cast. Like, they should be independent of their
   originator, right?" He ruled the same for summons (a summon stays on the
   team it was summoned for, whatever happens to its caster) and for tents
   ("once a tent makes a dude, the dude should stay on the team the tent
   was on when it was made"). The cause he pointed at was real: the bomb,
   its blast, a mine's blast, an exploding shot's blast, ENERGY WAVE, the
   boulder and the returning knife were never given a team number, so they
   were red. Fix AZ is now that rule: with the setting on everything
   fights for the colour it wears and nothing follows its maker (see fix AZ
   under [The fixes](#the-fixes) for all a player sees). Fix AY is gone,
   because under the rule its extra question is the plain colour test;
   fix AP no longer decides a side; #496 and #533 close with it, and #536
   and #537 close with the setting on. Taken from the rule without a
   question of their own, and told to the maintainer: a kill by a shot or
   a blast whose maker has died counts for its colour in every mode; a
   dead thrower's blast halves on his own side; nobody is credited with
   his own death; a shot does not spare its shooter; a returning knife
   never cuts the hand that catches it. A thing that sits on a walker (a
   ring of protection, a mace, a boomerang, a hook blade, a ghost's scare,
   IMMOLATE's fire) is the one exception: asked whether it should keep the
   colour it was cast in, the maintainer answered "obviously they should go
   with the walker". While its walker is charmed it fights for the charmer's
   side, and it comes back with him when the charm ends. Two more were
   asked afterwards. In Free For All a charmed fighter killed by his own
   bomb or his own summon gives his old band a frag (see fix AZ); the
   maintainer: "leave it as built". And a team's score still follows the
   maker in one case: a hero on a team other than the player's team scores
   through his hero record, so once he is dead his shots and summons pay
   nobody. The maintainer left that for later, in one round with #506: the
   score test is to ask which teams people play (#560).
4. **#505: a walker that dies charmed.** No change of its own, so there is
   no fix BA. With the setting on no side reads the owner chain any more
   (entry 3), so the question of who heads a dead charmed walker's chain
   arises only for the person behind a blow: the walker keeps heading its
   chain until the tick ends, and its summons' blows are credited to it
   for that tick.
5. **#503: owner loops.** A script's `set_owner` that would close an owner
   loop fails with an error (the maintainer's answer), and every climb
   cuts a loop a snapshot brings in where it closes.
6. **#495 (AX): whose summons the difficulty scales.** A summon is scaled
   as its caster is (the maintainer's answer): it takes its caster's team
   before it is scaled, so a human team other than team 0 gets scaled
   summons as its hero is. Which human-played teams the difficulty should
   exempt at all stays #506.
7. **#499: a ride whose ghost a script marks dead.** The ridden body goes
   home at its next act and the ghost stays out of sight. The maintainer
   ruled that the seat moves too: when a ride ends with its ghost dead,
   the seat goes to the dead ghost, so the ordinary death hand-off gives
   the player the next hero (#535).
8. **#500: a hero of a class no installed pack provides.** Left as it is
   (the maintainer's answer): the hero stays in the company, with a
   soldier's body and its class's own empty stat row, as classes 21 to 127
   already had. #528 stays open.
9. **#432: the cheat keys.** All twelve keys change the game, and none
   does anything in a networked game, for now (the maintainer: "leave an
   issue opened to revisit this in the future"; that is #543, which also
   covers replays, where cheats are not recorded). The team hop keeps the
   seat's starting team for Switch Character and the death hand-off, as in
   the old game; that was not asked.
10. **#490: a new game.** The campaign's introduction (the opening text of
    its `campaign.yaml`, not a level's own text) still plays once, between
    name entry and Base Camp (the maintainer's answer), and a new company
    always starts on Gladiator; another campaign is set from Base Camp.
11. **#508: the version a build shows.** Only a clean build of a commit on
    master's history shows the number. A build off master shows its commit
    only, and so does a local build of a master commit with changes not
    yet committed, with its `+` (the maintainer's answer).
12. **#398: Game Settings in the terminal clients.** The text and curses
    clients save the Game Settings answers to the settings file, as the
    SDL game does (the maintainer's answer; #525).
13. **Two setting-on recordings took another seed after fix BB**
    (a bot near its foe turns to it for good and swings only when the
    blow can land): on the old seed the row no longer reached the rule it
    tests, and its pin changed nothing.
    `archmage_mind_control_berserk_fixes_scen99` went back to its twin's
    seed 198 (0xC6), where the entry of 2026-11-08 had given it 0xB0: fix
    BB holds the orc's swing at the air that parted the stream before the
    cast, so on 0xB0 the control roll now succeeds and fix F's rule (a
    failed MIND CONTROL's berserk foe never takes the caster's team) is
    never reached. `cleric_bot_resurrect_foe_stain_fixes_scen99` took seed
    84 (0x54), where that entry gave it 0x3: on 0x3 the cleric now walks
    off without asking for RESURRECT near the faerie's stain. Each is the
    first seed from 0 where the row's own pin changes what its facts read;
    the spawns, inputs, budgets and facts stay, and the setting-off twins
    keep their seeds and recordings. The maintainer was told of this and
    has not ruled on it. Two rows' facts now read another
    walker, because fix BB left the one they read dead:
    `cleric_bot_raise_or_turn_fixes_scen99` reads the tower's hit points
    and counts the faerie as gone (the faerie now falls to the cleric's
    swing and the tower lives), and `mage_bot_heartburst_fixes_scen99`
    reads the mage's hit points (all three towers now fall). Alternatives:
    give the two re-seeded rows spawns of their own that reach the rule
    on any stream, and re-stage the two re-aimed rows so that the walkers
    they read before stay alive.

## Rulings (2026-11-16)

The line check, the kept turn and the ATTACK order (Gameplay Fixes BD,
BE, BC; #538, #513, #447), the flyer's own-floor foe (#512), and the door
rules his answers of 2026-11-18 and 2026-11-19 asked for (Gameplay Fixes
BF and BG, and BH, built when BG turned a test red). Each entry below is
what the maintainer said, or what was only told to him, and what the game
or its tests do because of it. He answered the re-balance on 2026-11-16,
how a level is eased on 2026-11-17, how the easing ends, the arrow slit
and what a shooter may fire at on 2026-11-18, and why bots attack doors
and guards erase them on 2026-11-19.

1. **Sixteen setting-on rows were given a fact of their own rule again
   after the three fixes were ported: of the fifteen the ports left with
   none, thirteen took an arena, inputs, a budget or a seed of their own
   and two keep both their arena and their seed; the review of the ports
   found a sixteenth.** The ports had moved all
   fifteen so that their own pin no longer changed a fact. Each now states
   the consequence of its own rule again, by the first of three cures that
   works. Two keep their arena and seed, because their rule leaves nothing
   but one draw fewer, and read the fixed stream, worked out from their
   blows: `cleric_bot_raise_or_turn_fixes_scen99` as a band (the big orc's
   163 to 166 hp, whatever its three arrows roll; 180 under its pin), and
   `elemental_struck_short_pool_fixes_scen99` as one value (the struck
   orc's 84 hp), because there no count, presence or band changes under
   its pin: the orc reads 85, inside any band of its two blows.
   Eight take an arena, inputs or a budget of their own. Seven of them reach
   the rule whatever the towers' line checks draw:
   `soldier_whirlwind_no_foe_fixes_scen99` (a foe that never moves, which
   the caster walks up to), `elf_volley_refund_fixes_scen99`,
   `elf_rocks_edge_pad_fixes_scen99` and `archer_bolt_edge_pad_fixes_scen99`
   (the tower far away, and the elf at the edge then holds fire),
   `archmage_summon_no_room_fixes_scen99` (the eight blockers are the
   hostile towers, and the summon and the burst come early),
   `soldier_bot_charge_unaimed_fixes_scen99` (the run ends after tick 0)
   and `mage_bot_heartburst_fixes_scen99` (the run ends after tick 4).
   The eighth, `orc_right_walk_potion_fixes_scen99`, keeps its twin's seed
   and ends at tick 25: with 150 ticks the ATTACK order (Gameplay Fixes
   BC) lets the orc kill the tower and walk over the potion, which is then
   gone with or without the quiet probes, while inside 25 ticks the orc is
   still at blows with the tower and has taken a right-hand-walk act whose
   right-side probe covers the potion, on every seed from 0 to 199. It
   first took a seed of its own, 16, one of 54 in 200 on which the potion
   lasted the 150 ticks.
   Four take a seed of their own, each the first from 0 where the row
   reaches its rule and its own pin changes what its fact reads:
   `thief_bot_charm_or_taunt_fixes_scen99` seed 135 (0x87),
   `archmage_mind_control_berserk_fixes_scen99` 30 (0x1E),
   `cleric_bot_resurrect_foe_stain_fixes_scen99` 15 (0xF) and
   `cleric_raise_blocked_corpse_fixes_scen99` 2; for the first three the
   rule waits on a roll that no arena can take out of the question: the
   thief's and the cleric's own special roll, and the cast's colour roll.
   An orc above the caster's level would take the cast's other roll, the
   control roll, out of the question (it always resists, with no draw),
   but the two mind control twins keep the equal-level orc they share.
   The setting-off twins keep their arenas, seeds and recordings.
   `soldier_charge_ally_fixes_scen99` took an arena of its own
   last: in its old arena the rush's own walk shoved the ally as well, so
   fix Z left one damage draw fewer, which moved no hit point. Now the
   player casts CHARGE on a soldier and hands the seat to an archer with
   SWITCH, and the soldier finishes the rush as a bot into the seated
   archer, whom no walk shoves; a wall under both keeps the bot's blocked
   steps from sliding off. With the setting on the archer keeps its spawn
   point; under the row's own pin, which strikes it as the classic rush
   does, it is walked 16 px west. The row keeps its seed 24, and every
   seed from 0 to 199 reaches the rule. A bot's own CHARGE did not stage
   it in the arrangement tried, the hero between the bot and its foe: with
   the setting on the bot's CHARGE rule (Gameplay Fixes V) refuses a rush
   whose path meets an ally before a foe.
   `thief_charm_nearest_fixes_scen99`, a sixteenth row, was found after
   the fifteen: its facts still changed under its own pin, but only in
   whether the thief lived, a fight's outcome. It took an arena of its
   own the same way: its soldier stands 40 px south of the thief on a
   team of its own and the run ends at tick 25, so the team each foe ends
   on says whom the charm took, on every seed from 0 to 199 but the 8 on
   which the charm's own one-in-twenty roll resists.
   Alternatives: seeds of their own for the ten restaged rows (four of
   the fifteen had taken another seed twice already); for the ally a seed
   at which its old arena's position fact flips, a place its own acts take
   it to on that seed; and for the mind control twin spawns of its own
   with an orc above the caster's level, which takes the control roll out
   and still needs a seed for the colour roll. The maintainer was told of
   this and has not ruled on it.
2. **The re-balance: "A is good".** The maintainer's answer, on
   2026-11-16, to what is done about the crews' loss under fix BC: the
   levels are re-balanced in the generators of Westlands, The Long Season,
   Imaginations and the Tower. Only a level whose average of crew
   survivors fell under the fixes is eased, and only until the shipped
   default (the setting on) is back to the average that level read before
   these fixes, within the error of that average. A level file serves both
   settings, so an eased level gets easier with the setting off too, and
   he accepted that. Gladiator, The Tryxian Chronicles and the mode courts
   are not touched; what the fixes do to them is measured and recorded
   (see [Balance](#balance)). No survivor number becomes a test pin again
   (those pins were deleted on 2026-11-02, ruling 16 of the
   [rulings](#rulings-2026-11-01)); the result is recorded in these docs
   and in a dated note in each eased builder. No level is eased through
   its script, by a percent laid over every fighter, or by a change to a
   family's own numbers.
3. **How a level is eased: "i would say b".** His answer, on 2026-11-17:
   group by group, and no fighter is ever taken away. One step lowers one
   placement group of the level's hostile orcs, ghosts or small slimes,
   the fighters fix BC made stronger, by one level, the group with the
   strongest fighters first. A level whose fighters are already at level
   1 has no step: it stays a little harder, and it is reported to him with
   its numbers. A level that is not back after half its steps stops there
   and is brought to him. The other answers he was offered: where every
   group is already at level 1, up to two fighters fewer; or every step to
   the end, however far.
   Eight levels were not back at half their steps, and on 2026-11-18 he
   was shown how each would end: the nine levels by name with their
   forty-seed averages before these fixes, after them and at the proposed
   end, with the setting on, and what is done to each; that The Forest
   Road (Westlands 2) and The White City (Westlands 16) stay clearly
   harder, because no step brings them back (a step lowers only a group
   that fights before tick 600) and no fighter is taken away; that The
   Frozen Ford (Long Season 16) ends one survivor under, and The Last Ford
   (Westlands 3) and The Ash Plains (Westlands 23) a quarter to a third of
   a survivor under; and that most eased levels get easier with the
   setting off (The Dead Marshes 5.80 [5.95] to 7.83 [7.85]). The other
   answer named for the two harder levels was to take fighters away after
   all. He asked what level the crew is (eight bots at the level the
   campaign's curve gives a fresh team for that level), then answered, on
   2026-11-18: "yeah, sure, the easing is okay". So the easing goes past
   half where a further step brings a level back or nearer without going
   too far (The Last Ford, The Hidden Refuge, The Dead Marshes, The Ash
   Plains), stops one step short of half where that comes nearest (The
   Frozen Ford), keeps The Mountain of Fire's two strongest groups at
   their old levels, and is undone where no step brings the level back:
   The Forest Road reads 8.00 [8.00] before these fixes and 6.97 [6.85]
   after the easing, The White City 2.80 [2.65] and 0.55 [0.70]. Three
   levels end a little under on purpose: The Last Ford 7.38 [7.40] against
   7.62 [7.70] (0.25 under, error 0.19), The Ash Plains 5.53 [5.15]
   against 5.90 [5.95] (0.38, 0.34) and The Frozen Ford 4.90 [5.05]
   against 5.90 [5.60] (1.00, 0.48). Forty seeds, survivors of eight with
   the setting on, the docs' twenty in brackets; every step and its
   reading is under [Balance](#balance).
4. **The shape of fix BC, and why the gentler shapes were not taken.** In
   its ATTACK order a melee-only bot that already faces its foe on its own
   floor swings when a foe's body stands on its weapon's pad, and
   otherwise steps toward the foe: the remedy the maintainer was shown
   when he answered "let's do A and handle that later" (Rulings
   (2026-11-14), entry 2). Three gentler shapes were built beside it, and
   each of the four was measured on a trial build, 2026-11-16 (Westlands,
   twenty seeds, 600 ticks; the sixth column sums the four generated
   campaigns, per run of all four):

   | shape | ticks a melee-only bot stands within 30 px of its foe on its floor, not busy, with no blow landed for 60 ticks | acts its ATTACK order only faced its foe, counted while it stood within 30 px of it, not busy, with no swing started for 60 ticks | acts its ATTACK order spent only facing, all ticks | its damage per 100 ticks near a foe | crew survivors, against the game before these fixes | Westlands 19 / The Long Season 16, crew survivors |
   |---|---|---|---|---|---|---|
   | before these fixes | 98,023 | 27,692 | the whole order | 21.66 | 0 | 5.15 / 5.60 |
   | swing at a body on the pad, else step (fix BC's shape) | 42,578 | 2,319 | 0 | 44.85 | -28.8 | 1.35 / 0.00 |
   | one blow per order, then face | 43,618 | 2,070 | 89,703 | 37.49 | -16.2 | 2.85 / 0.10 |
   | keep the search, no order | 59,999 | 676 | no order (61,130 searches kept; its standing ticks spent following a wall rise from 11,850 to 19,048) | 30.09 | -13.2 | 2.30 / 0.50 |
   | swing at a body on the pad, never step | 48,994 | 693 | 177,914 | 34.97 | -19.8 | 1.90 / 0.10 |

   Two cautions on the table. The 60-tick count skips a bot while it is
   busy, and fix BC keeps these bots busy far more often, so the second
   column is not like for like; the third, the acts the order only faced,
   is the direct count. And fix BC's 0 in the fourth column is 0 by
   construction, since its rule has no branch that only faces a bot
   already facing its foe; what is left in its third column are rounds on
   which the bot still had to turn. Why this shape: it is the only one
   that leaves no stand in the order (the two shapes that pause after a
   blow still spend the acts of the fourth column only facing, in stands
   too short for the 60-tick count to see); the shape that drops the order
   does not stand but circles its foe, because the foe's own body is a
   blocked front to the right-hand walk, and it removes the least
   standing; and
   none of them spares the levels: the same levels fall under all four
   (the last column), so the re-balance is the same work whichever is
   built. It is also the rule a shooter already follows in the same order
   (shoot when the shot can land, else step) and the rate a player gets
   by holding the key into a foe, one blow per weapon pause. Dropping the
   order would undo part of fix BB, whose near-branch tests turn red
   under that shape.
5. **Level play's "two or more specials" floor is pooled over the six
   seeds (#515).** The level-play test asked each of its six seeds for at
   least 30 casters with two or more specials, a floor one seed alone can
   miss. It now asks the six seeds together for at least 180, six times
   the same 30, and still prints each seed's line. That is a looser bound
   than six floors of 30, and the remedy the test's own header gives for a
   change of the game: more seeds pooled, never a narrower test. With
   fixes BC, BD and BE and the flyer's own-floor foe (#512) in, the six
   seeds read 41, 41, 34, 33, 29 and 33 casters with two or more specials
   (pooled 211), where the game before them read 45, 48, 45, 36, 39 and 41
   (pooled 254); seed 2's 29 is under the old per-seed floor. The crew
   alive at tick 2400 reads 11, 8, 3, 3, 5 and 6 (before: 10, 6, 4, 10, 8
   and 10); that floor, at least one crew member, stays per seed. With the
   line check's part of the door rule (Gameplay Fixes BF) as well the six
   seeds read 29, 41, 40, 39, 39 and 34 (pooled 222) and the crew alive at
   tick 2400 7, 8, 9, 8, 8 and 7 (see [Balance](#balance)). With every
   door rule (Gameplay Fixes BF whole, BG and BH) they read 38, 34, 42,
   36, 41 and 41 (pooled 232) and the crew alive at tick 2400 9, 11, 7, 8,
   9 and 7. Decided in this work under the test's own rule.
6. **The questions, and the defaults built.**
   - Arrow slits: asked 2026-11-17 and answered 2026-11-18: "for the arrow
     slit, i agree with you on (a)". (a) is the rule as built, so it
     stands: the line check takes a slit as open, and the shot itself
     takes the slit's gamble. The other rule, which he did not take, is a
     check that takes the gamble too: on the 24 classic levels with slit
     cells (twenty seeds each, the setting on) golems would start 255
     shots where they start 373 and fire elementals 1,760 where they start
     2,156, and the crews would keep 1,494 survivors of 3,840 against
     1,467; both are measured under [Balance](#balance).
   - What a shooter may fire at: asked 2026-11-18, with two answers
     offered (leave the line check as it is, or let only fighters and
     tents count) and these numbers: with the setting on, on Gladiator and
     The Tryxian Chronicles (twenty seeds of each level), 98,695 of the
     104,342 checks that passed although no fighter or tent stood in the
     flight ended on an enemy door, 5,582 on an enemy shot in flight and
     40 on a grown tree; on The Tryxian Chronicles' first level (six seeds
     of 2,400 ticks) 7,166 of 7,235 ended on a door, a third of the
     soldiers' passed checks there (4,169 of 12,371) and nearly two thirds
     of the thieves' (2,394 of 3,733). He answered the same day: "let's
     fix the shooter thing now. I think just excluding doors is
     sufficient; the rest should stay included". Built as Gameplay Fixes
     BF: with the setting on an enemy door on the shot's path refuses the
     line check, as a wall does, on the pad and on every step; the check's
     test weapon meets the door without the clash roll and draws nothing
     for it; a door that is the shooter's own foe passes, as every foe
     does; a grown tree, an enemy shot in flight and every other body are
     as before; the shot itself still takes the clash roll at a door; and
     with the setting off nothing changes. Three choices were made in this
     work on the way, for him to overrule, each with its alternative: a
     door always refuses (a check that kept the clash roll at a door would
     still pass a fifth to a third of the checks at a foe behind it, and
     the shot each started would mostly be spent on the door); a door that
     is the bot's own foe passes (with no exception a level script's or a
     mod's "attack this door" would never fire); and the refusal is the
     one a wall gives (a refusal of its own would change nothing a bot
     does, since no caller tells the two apart). Grown trees and shots in
     flight stay as they were, as he ruled: on the same classic levels
     checks that passed on no fighter but on a grown tree read 40 before
     the rule and 64 after, and on an enemy shot in flight 5,582 and
     5,675. Measured (see [Balance](#balance)): no check passes on an
     enemy door that is not its shooter's foe any more (50,009 such passes
     before). In the count he was shown, the 98,695 passes that ended on
     an enemy door were 38,867 on a door that was not its shooter's foe
     and 59,828 on one that already was; after the rule 76,688 of the
     82,451 such passes still ended on a door, every one a door that was
     the foe. The exception for a door that is the foe was reached in
     stock play, because a shot that landed on a door made the door its
     shooter's foe on both settings (`walker::attack`): checks that passed
     on such a door went from 67,377 to 80,470, and the doors still lost
     1,405,275 hit points against 1,801,741 (on The Tryxian Chronicles'
     first level played to tick 2,400 they lost more, 124,911 against
     117,396). The alternatives for that were asked on 2026-11-19: with
     the setting on, a blow on a door does not make the door its owner's
     foe (a new rule in both games), or no exception for a door that is
     the foe at all; the first is built, with the rest of a bot leaving a
     door alone (the BF row). Doors went as often with the rule as before
     it: of the 6,680 that stood at the start of the classic battles 178
     were gone at tick 600 before the rule and 181 after, 26 and 14 of
     them broken under blows, 8 and 6 opened with a key, and 144 and 161
     erased by a soldier's BOOMERANG or a cleric's MAGIC SHIELD, whose
     guard killed an enemy door inside its ring with either setting and in
     the old game; with the setting on a guard now passes a door and a
     grown tree by (Gameplay Fixes BG, the BG row).
   - Why bots attack doors, and guards erase them: shown on 2026-11-19 the
     numbers of the line check's part above (a shot that landed on a door
     made the door its shooter's foe, a bot that walked into a door swung
     at it, and 161 of the 181 doors gone by tick 600 had been erased by a
     guard), he answered the same day: "why are bots even attacking doors?
     this seems idiotic. also, why does boomerang and magic shield destroy
     doors? this seems like a bug". Read in this work as: bots do not
     attack doors, and a guard that erases a door is a bug; he was told so
     before the build. Built, behind the setting and in both games:
     Gameplay Fixes BF widened to a bot leaves a door alone (beside the
     line check's refusal, a bot that walks into an enemy door starts no
     swing at it, and a blow that lands on a door names no foe), and
     Gameplay Fixes BG, a guard passes a door or a grown tree by. Choices
     made in this work on the way, for him to overrule, each with its
     alternative: a player's own fighter still swings at a door it walks
     into (the alternative: nobody does); a door that already is a bot's
     foe is fought as before, by the line check and the bump (the
     alternative: no exception, and a script's "attack this door" would do
     nothing); the grown tree is passed by as well as the door (the
     alternative: the door alone, as he asked); the guard's rule is behind
     the setting and in the old game too (the alternative, on both
     settings, would change the classic game); and a bot whose foe is
     behind a door it cannot open stays pressed against the door, as at a
     wall (#582; the alternative, a rule that sends it elsewhere, is not
     built; the maintainer later ruled for one: Gameplay Fixes BJ lets
     that foe go and BL hands the bot the door as a last resort). Found
     while building, and what was done, each with its alternative: once a
     guard passed doors by, the level-play test went
     red, because the doors that now stood met five rules' plain pad
     question (11 refusals of GROW TREE against its cap of 3), and the
     five rules were given the strict pad question, Gameplay Fixes BH
     (#590; the alternatives: the door half of BG dropped, against his
     words, or the cap raised, against the test's own rule); the exception
     for a door that already is the foe was dropped from the blow's part
     of BF, because naming the foe a bot already holds changed nothing a
     player can see (the alternative: keep a term no recording could
     show); what still strikes a door gets no rule here, the special cast
     from a distance being #591 (the alternative: a special asks the line
     check before it pays); and a walker with a seat number is a player's
     fighter to the rule, so the measuring crews' four seated fighters
     still swing at doors (#593). Measured (see [Balance](#balance)),
     before any door rule, with the line check's part alone and with every
     rule, on Gladiator and The Tryxian Chronicles: checks that passed on
     an enemy door 117,386, 80,470 and 0; bots' swings at a door they
     walked into 20,616, 22,585 and 0 (7,341 and 6,488 of them at a door
     that already was the bot's foe); blows that landed on a door 38,922,
     31,648 and 1,204, of the last 553 by the seated fighters and of the
     bots' 651 the archer's FIRE ARROWS 381, other specials 114 (101 of
     them cast from a distance at a foe), shots and swings started at a
     foe that met a door 140 and a pad holding the foe and a door 16 (a
     CHARGE and a tent none); the doors' hit points lost 1,801,741,
     1,405,275 and 46,309; and doors gone by tick 600 178, 181 and 2 of
     6,680, the last two opened with a key and none erased by a guard (144
     and 161 before). A bot with no key then stood pressed at a door more
     (before Gameplay Fixes BI to BL): a locked door refused a step of one
     of the levels' bots on 233,215 bot-ticks (one tick of one bot) against
     101,179 before any door rule,
     and with the bot's foe beyond the door on 210,419 against 35,520, a
     count that before the rules left out a bot whose foe was the door;
     before the rules both counts also left out the acts a bot spent
     finishing a swing or a shot at the door, so both rise more than the
     time bots stand at doors; nearly nine stays in ten end within 60
     ticks, but the stays longer than that span 69 ticks in 100 of the
     time bots stay there (34 before any door rule).
   - When a level counts as fallen or back: decided in this work, and told
     to the maintainer. The ruling's "twenty-seed average" is read as the
     average over forty seeds, the twenty these docs use and 101 to 120,
     and a level is judged by the paired difference of the two runs and
     its error (see [Balance](#balance)): it fell when the difference is
     three errors or more below zero, and it is back when it is no more
     than one error below. The other reading, within the error of the
     earlier average alone, is a smaller yardstick and would ease further.
   - Levels the count cannot judge: not yet asked; the default was built: a
     level whose crew read under one survivor of eight before these
     fixes, or keeps all eight alive on every seed after them as before,
     is left as it is, because its count of survivors cannot show a fall
     or an easing there. The other choice is to ease those levels too,
     judged by the damage the crew takes. His ruling names the Tower's
     generator too; the default changes no Tower table even if a floor
     falls, because every floor of a band and every run in progress share
     those tables, and brings such a floor to him with its numbers (none
     fell).
   - A fall another fix made: decided in this work, not ruled by him. A
     level is eased only when its fall came with fix BC's own commit:
     lowering its orcs, ghosts and small slimes takes back what fix BC
     gave them and does not answer a fall that another fix made. A level
     that fell without it is brought to him with its numbers fix by fix.
7. **Found on the way and filed** (none is fixed here):
   - #571: a tower whose foe is on another floor stops shooting and plans
     a way there that it can never walk; a guard holding its post keeps
     aiming at such a foe and passes over one on its own floor.
   - #572: the line check does not know three things the shot meets: the
     range it loses over trees and shrub (the trees' range is read from a
     wrong cell), a knife or rock that drops a floor over open air, and
     the soldier's limit on blades in the air.
   - #573: a yell, a follower's leader and a knife or rock that drops a
     floor hand a walker a foe on another floor.
   - #574: a swing or shot that cannot be paid for in magic still costs the
     animation and the weapon's pause.
   - #575: the short cut for a crowded map counts the dead test weapons of
     the line checks asked earlier in the same tick.
   - #576: a walker stuck in terrain gets one tick of flight and is treated
     as a flyer for that act.
   - #577: a tower turned one notch keeps its old heading.
   - #578: the ghost's wail scene checks each pack's flight by counting a
     strike, so the second pack's flight is asserted by nothing.
   - #579: an arrow slit's gamble measures the shot's distance from where
     it stands, not from the cell it enters (in both games).
   - #580: a bot's even snap turns its facing to an axis and leaves its
     heading on the diagonal.
   - #581: the difficulty percentages are written twice (the dedicated
     server keeps its own copy).
   - #582: with fix BC a melee-only bot whose foe is in reach behind a wall
     pushes against the wall for as long as the foe stays in reach, up to
     its whole ATTACK order.
   - #583: with fix BC a melee-only bot beside a smaller foe bumps it and
     shuffles for a few acts before its pad meets the body.
   - #584: on a level with floors a flyer that holds a foe on another floor
     is asked for a foe on its own floor only near the spot under that
     foe, and not during its ATTACK order.
   - #585: with fix BC a melee-only bot that a mod sends at a mode ball
     with an ATTACK order walks over the ball and back (no stock mode does
     it).
   - #586: two pack scripts write out what flying is instead of asking
     `walker::flies` (they agree for every value the game writes).
   - #587: with fix BC a melee-only bot behind an ally shoves it toward
     the foe for as long as the foe stays in reach: two times in three the
     ally loses its orders and is walked four steps at the foe (found by
     reading, not counted).
   - #591: no special's rule asks the path to its foe, so a special cast
     from a distance at a foe behind a door or a wall is made, and its
     shots strike the door or the wall.
   - #592: a guard erases an enemy's circle of protection, as it erased a
     door (none was erased in the measured battles).
   - #593: the measuring crews of the tests seat their first four fighters
     while nobody drives them, so to Gameplay Fixes BF those four are a
     player's fighters.

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
