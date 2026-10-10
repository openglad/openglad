# New Specials

Six classes gain new specials: the thief, the ghost, the faerie, the orc
captain, the skeleton and the fire elemental. Each new special belongs to
one setting, **New Specials**, which is on by default. With it off, every
class plays exactly as it did before the kits existed. The players' list of
moves, with their numbers, is in [classes.txt](classes.txt) (the entries
marked `+`). This page covers the setting, how the engine hides the kits
when it is off, the possession rules, and the notes a player or a modder
needs.

## The setting

| client | where |
|---|---|
| SDL | Game Settings → Gameplay FX → **New Specials** (green on, red off) |
| text (`openglad_text`) | the game-settings prompt `New specials: <on\|off>. Change (on/off, blank keeps current):`, or `--new-specials 0\|1` for one run |
| curses | Game Settings → `New specials (on/off):` |

The row flips a per-machine preference (`cfg` key `gameplay/new_specials`,
default `on`). That preference seeds the session's value once per session.
In a networked game the host decides: a joiner's click only changes what it
will use when it hosts, and the lobby's RULES column shows every peer the
host's value as `NEW SPECIALS: ON` or `NEW SPECIALS: OFF`. The value is never
written into a company save.

It is a simulation setting, so it travels the same way as the respawn,
generator and fallen-hero settings, one hop at a time:

1. **Preference → session.** `og::ui::seed_new_specials_from_cfg` sets
   `SaveData::new_specials` (session-only, default 0) from the cfg key, in
   `GameSession`, the text picker and the curses picker.
2. **SaveData → lobby.** `LobbySettings::new_specials` (`int16`, lobby
   default 1, anything but 0 or 1 is reset to the fallback) is copied from
   and back to every peer's save beside `infinite_gold`, and
   `LobbySaveDataEquivalent::new_specials` carries it to the dedicated
   server.
3. **Lobby → world.** `InitialSetupMessage::new_specials` (`int16`) is sent
   by `GameServer` and by the staged lobby (`MatchStage`); `GameClient`
   copies it into `GameWorld::new_specials`. Both `sync_world_from_save_data`
   twins (`Screen` and the headless server) stamp the same field from the
   save. A level file's own scratch world never sets it.
4. **World → mirrors and replays.** `WorldSnapshot::new_specials` (`uint8`)
   rides every keyframe and delta, and a replay compares it. Lua reads it as
   `og.match_setting("new_specials")`.

`GameWorld::new_specials` defaults to 0, so every hand-built world, and every
parity recording, plays the classic kits unless something sets it. The
parity runner sets it to 0 explicitly.

Versions: protocol v19, snapshot v15, replay v21.

## How the kits stay hidden when the setting is off

A pack marks a new special with `new_kit = true` on its `specials` entry, or
inside its `alternate` (the thief's MINE and the ghost's WAIL hang off
classic slots, so only their alternates are marked). Nothing is stripped
from the table when the setting is off. Instead every reader asks one view
(`og::sim::specials_view`), which answers the table as this session sees
it:

- **A hidden slot** reads `NONE` with cost 5000. The player's special
  cycling skips it, the HUD never shows it, and a bot's slot pick passes
  over it.
- **A hidden alternate** reads `NONE`, `walker:alternate_down()` answers
  false, and its price is never charged. An alternate on a hidden slot is
  hidden too, whatever its own mark says.
- **A priced alternate.** `alternate = { name, mp_cost, new_kit, detail }`:
  with Shift held, the engine gates and charges the alternate's own price,
  and the HUD turns red below it. Without `mp_cost` it costs the slot's
  price, as every classic alternate does. `detail` is the one line the
  DETAILS screen prints under the alternate's Shift line (below).
- **The bot drops a shift it cannot pay.** A bot draws its Shift coin as it
  always did. If the coin says Shift and the slot's alternate has a price
  the bot cannot pay, the Shift is dropped. No classic alternate has a price,
  so this never fires with the setting off.
- **Bot gates answer true when the setting is off.** A struck bot asks its
  current slot whether to cast, and the faerie's and the captain's slot 1
  are new. Every new gate's first statement is
  `if og.match_setting("new_specials") == 0 then return true end`. A
  `false` would skip a random draw the classic game makes.

Two states make every slot free. While a walker is **HIDDEN** (a dug-in
skeleton, a ghost riding a body) or **CHANNELLING** (a burning fire
elemental, a skeleton in the four ticks it takes to sink), every slot costs
it 0, because a hidden walker regains no magic and IMMOLATE drains it: the
press that surfaces or quenches must not need magic the walker cannot have.
Because the rule frees every slot, not only the one that started it, the
kits refuse the walker's other specials themselves: a burning elemental
answers `QUENCH IMMOLATE FIRST`, and a sinking or buried skeleton answers
`SPECIAL BUSY`. A toggle also refuses its own second press for 10 ticks
after the first (`DIG IN SETTLING`, `IMMOLATE SETTLING`). A refusal is
silent while the key is held, so holding Special cannot flip a toggle
straight back, and a second tap after 10 ticks does what it says.

**An engine note.** With the setting off a hidden slot costs 5000, but
`walker::special()` itself does not refuse the slot: a walker forced onto a
hidden slot with 5000 or more magic would cast its new special. That cannot
happen in play, because the player's cycling and the bot's slot pick both
read `NONE` and never put a walker on a hidden slot; only a test that sets
the slot by hand can reach it. A guard there would need a per-family rule
(the classic fire elemental's empty slots used to fall through to its
starburst), so the engine is left as it is.

## Engine state

Three per-entity fields, all in every snapshot:

| field | type | dirty bit | meaning |
|---|---|---|---|
| `kit_state` | `uint8` | 94 | `KIT_HIDDEN` 1, `KIT_FEARLESS` 2, `KIT_WARD` 4, `KIT_CHANNEL` 8, `KIT_QUARTER_FREEZE` 16 (on a weapon: the freeze it lays is a quarter of the rolled one; GLIMMER's sprinkles) |
| `possess_link` | `uint32` | 93 | the entity id of the possession partner, 0 when none |
| `possess_ticks` | `int16` | 95 | on the host: ticks left; 0 = for good, which the core kit never asks for |

They are kept apart from the classic bit flags because those are copied
onto a corpse stain and wiped by a transform. A hidden walker does not act,
is out of the collision table, is skipped by every finder, target scan and
seat claim, cannot be hit, and is drawn only for its own team (an outline
and a dither in SDL, its glyph in curses). A FEARLESS walker never flees;
so does any walker standing within a friendly war banner's
`rally_radius` (96 pixels) on its floor while the setting is on. A WARD
cancels the next death once (REASSEMBLE). That blow is not a kill: nobody
is told of a death and whoever struck it is credited no kill.

Timers and windows hang on one invisible helper effect,
`core:kit_marker` (effect wire id 17). Its role sits in its animation byte,
its lifetime counts down in its handler, and its age is measured from the
tick it was spawned. The role numbers are engine constants
(`include/openglad/gameplay/kit_marker_role.h`), exported to Lua as
`og.C.MARKER_*`:

| role | value | used by |
|---|---|---|
| `BURROW` | 1 | skeleton DIG IN (the sink, the regeneration, the pop) |
| `LEGION` | 2 | skeleton LEGION's 300-tick window |
| `IMMOLATION` | 3 | fire elemental IMMOLATE (drain, contact burn, embers) |
| `PHASE_VEIL` | 4 | ghost PHASE |
| `METEOR_RAIN` | 5 | fire elemental METEOR RAIN |
| `WARD` | 6 | skeleton REASSEMBLE's clock (the ward bit is still what the engine spends) |

New wire ids: effects 13 `core:mine`, 14 `core:wail`, 15 `core:hook_blade`,
16 `core:ember`, 17 `core:kit_marker`; weapons 20 `core:bone_wall`, 21
`core:war_banner`. A pack's `wire_id = "auto"` now starts at 21, and at 22
for weapons.

## Possession

A ghost (POSSESS, slot 3) touches a foe and rides it. The ghost hides inside
and follows the host; the host fights for the ghost's side; and if a player
drove the ghost, that player's seat now drives the host, with the host's
weapon and the host's own specials (a possessed mage teleports, a possessed
slime splits). The host's original team is kept on the ghost's
`real_team_num` until the ride ends.

**Refusals**, each with its cause. None of them spends magic or draws a
random number:

| reason | cause |
|---|---|
| `PHASED` | the ghost is in PHASE and cannot touch anyone |
| `CANNOT POSSESS` | the ghost is charmed (its own true team is already waiting on `real_team_num`), or the engine refused a rider that is dead, hidden, dormant or already riding |
| `NO HOST IN REACH` | no living foe within 24 pixels on the ghost's floor, or the host is dead, hidden or dormant |
| `UNDEAD RESIST` | the host is undead |
| `HERO RESISTS` | a player's seat drives the host; a seated hero is never possessed, whoever casts |
| `ALREADY POSSESSED` | another ghost already rides the host |
| `HOST IS CHARMED` | the host is under a thief's charm |

Past the refusals the host may resist: the ghost rolls against its level,
the host against its constitution. A host that resists strikes the ghost
once, and the magic is spent.

**How long.** The ride grows with the log of the levels the ghost has over
its host and is capped at one minute: `possess_base` (120 ticks, 10 s)
plus `possess_log_scale` (200) times ln(1 + gap), read from a whole-number
table, never past `possess_cap` (720 ticks). A host that out-levels the
ghost is ridden `possess_min` (120 ticks). Every ride ends.

| levels the ghost has over its host | ticks | seconds on the HUD |
|---|---|---|
| none, or the host is higher | 120 | 10 |
| 1 | 258 | 22 |
| 2 | 340 | 29 |
| 5 | 478 | 40 |
| 6 | 510 | 43 |
| 9 (a level-10 ghost on a level-1 mage) | 580 | 49 |
| 19 and more | 720 | 60 |

**How it ends.**

- The timer runs out: the ghost reappears where the host stands.
- The player presses **Switch Character** in the body: the ghost steps out
  (Special stays the host's special). The next press cycles heroes as
  usual.
- The host dies: the ghost is cast out and takes half of the killing blow's
  excess (how far below zero it drove the host's health) as damage, which
  can kill it.
- The ghost dies while it rides (only a script can do that): the host goes
  free.

**Further rules.**

- A hidden ghost answers only Special and Switch Character; it does not
  walk, fire or regain magic.
- A thief who charms a possessed host: when the ride ends, the host's true
  team goes back into its charm record and it stays with the charmer until
  the charm runs out, then goes home. It never stays on the ghost's side.
- A possessed body never hides, so a possessed skeleton cannot DIG IN
  (`POSSESSED BODY`).
- A level that ends during a ride keeps the ghost's hero record like any
  other hero's; the host is left behind like any other enemy. The seat
  drives the host at that moment, so the host is the one that walks onto
  the exit.
- A snapshot carries all three fields, so a client that joins late, or a
  restored world, resumes the ride exactly.
- A bot ghost possesses too: a foe within 24 pixels that it may take and
  whose level is not above its own.
- If Special is still held after the possessing press, the press that
  possessed does not carry over on the next tick, but a key held for three
  ticks or more casts the host's own special from the second tick on.

## Player notes

**Thief.**
- Holding Shift + Special lays mines one per tick, up to the three a thief
  may have down, as a held key repeats a bomb.
- A dead thief's mines stay armed until they expire, and still set each
  other off.
- Mines are invisible to the other team on the SDL screen, on the radar and
  in the curses client; the thief's own team sees a faint shimmer, or the
  mine's glyph in curses.
- Fliers (faeries, ghosts, anything flying) float over mines.
- While its thief is charmed, a mine treats as foes whoever the thief's
  current side does; the chain between mines still follows the team the
  mine was laid for.

**Ghost.**
- WAIL is a small puff of the scare's own sparkles (`expand8.png`), not a
  bolt. It travels 6 pixels a tick for up to 60 ticks, homes on its foe's
  centre and frights it when its 8x8 core touches; then a fresh puff hops
  on to a foe within 120 pixels of the one it struck, at most three more
  times. It does not treat fliers differently. Its numbers
  (`wail_bolt_life`, `wail_bolt_step`, `wail_fork_px`, `wail_hops`,
  `wail_frame_ticks`) are on the `core:wail` family, not the ghost.
- SIPHON touches once per attack pause: a tap is one touch, and a held key
  touches again only when the ghost's pause has run (7 ticks for a bare
  ghost, a little less for a nimble hero). Inside the pause it answers
  `SPECIAL BUSY` and spends nothing. A touch that finds no foe still spends
  its mana and sets no pause.
- A resisted POSSESS lets the host strike once.
- A ghost riding a body regains no magic.
- PHASE lasts 48 ticks at double speed, and the ghost cannot attack while
  it lasts, not even on the tick it casts. A bot ghost never phases again
  while its veil is up.
- A phased ghost shows to everyone as a faint shimmer with its team's
  outline, so friend and foe can see where it went.

**Faerie.**
- BLINK hops `blink_base` + `blink_per_level` x level pixels (26 at level
  1, 44 at level 10) in one of eight directions, picked at random; when
  that spot is blocked it tries the other directions, then two thirds and
  one third of the hop. It costs 20 magic, SWAP 24.
- After a BLINK or a SWAP the faerie rests: her attack pause plus
  `blink_cooldown` (12 ticks, one second). A held key cannot hop again
  until the rest is over, and she cannot sprinkle or SWAP during it. The
  pause after an ordinary sprinkle does not block a hop, so a fighting
  faerie can still slip away between two sprinkles.
- SWAP never drops a walker onto a spot it could not stand on, such as
  water (`SWAP BLOCKED`).
- A bot faerie only picks SWAP when it can pay for it; short of the price it
  blinks away instead, or holds.
- HASTEN keeps a stronger speed bonus already running (a high-level speed
  potion is not cut down to the haste).
- GLIMMER pauses the faerie like one attack. Each of its sprinkles is
  marked, and the freeze it lays is a quarter of the rolled one (about
  half a second at level 4); the roll itself is drawn as for any sprinkle.
- WISH kills the faerie: her stain and, for a hero, her life gem stay.

**Orc captain.**
- The captain cannot be hired: it is the orc's promotion only (an orc of
  level 5, on the DETAILS screen). As in the classic game the promoted
  captain starts again at level 1, with HOWL alone; EAT CORPSE opens at
  level 4, HOOK BLADE / KNIFE FAN at 7, WAR BANNER / WARBAND at 10. With
  the setting off the captain has no specials, as in the classic game.
- HOOK BLADE spirals out from the captain like the boomerang, from 12 to
  60 pixels over 24 ticks and back over 24, one turn every 16 ticks. The
  first foe it touches is frozen and reeled to the captain's feet, 8
  pixels a tick, then stays frozen 12 ticks more. A wall stops the reel
  where the foe stands.
- Planting a WAR BANNER while one of this captain's banners stands
  strikes the old one quietly (a flash, no explosion, no notice) and turns
  his warband grunts to the new one. A refused plant (no corpse) leaves
  the old banner standing.
- The banner's aura numbers (`banner_pulse`, `banner_radius`,
  `banner_regen`, `banner_fright`) are on the `core:war_banner` family,
  because a banner outlives its captain.
- Warband grunts gather at the middle of a standing banner. When no
  banner stands, they run to where the captain stood when they were
  called, then follow him.
- The hook blade cuts down any enemy missile that comes close while it
  spins, but passes an enemy war banner or bone wall by: those have to be
  chopped down.
- A bot captain howls when a foe is within 130 pixels. EAT CORPSE has its
  own bot gate: it eats only below 60 percent of its health (at exactly 60
  percent it does not) and standing on a corpse.

**Skeleton.**
- DIG IN costs one magic every 4 ticks while the skeleton is buried, and
  it surfaces when the magic runs out, or after 300 ticks, whichever comes
  first; coming up is free. The cast refuses (`NEED MANA TO STAY DOWN`)
  when the pool after the price would be under 8. A dug-in enemy skeleton
  keeps the level open until it surfaces.
- REASSEMBLE's ward lasts 360 ticks (30 seconds) and costs one magic every
  6 ticks while it is armed; it fades when either runs out, with a notice
  ("<name>'s ward fades"). A seated hero's notice goes to that seat; a bot
  skeleton has no seat, so its notice goes to everyone. A skeleton that is
  buried and warded pays both drains.
- BONE STORM shatters this skeleton's standing walls, each into eight
  bones, and throws nothing from the skeleton; with no wall standing it
  refuses (`NO WALL STANDING`) and spends nothing.
- LEGION raises half its kills: a coin is drawn for every kill that could
  rise, and on the other side the corpse stays where it fell.
- While it sinks and while it is buried, a skeleton keeps DIG IN in hand:
  Switch Special cannot turn the next press into a TUNNEL.
- Bone walls stop bodies, not most missiles: an enemy missile passes a wall
  about six times in ten.
- LEGION in a respawn mode: a hero killed under LEGION both respawns and
  leaves a risen skeleton, because the corpse a respawn waits on is never
  consumed.
- A skeleton that REASSEMBLEs was not killed: the blow that felled it shows
  no death message and no "All foes defeated!", and counts no kill for
  whoever struck it.

**Fire elemental.**
- Quenching IMMOLATE is free; until then the elemental casts nothing else.
- METEOR RAIN strikes every 6 ticks (10 strikes over its 60 ticks), each
  for 8 + 2 a level (22 at level 7, 28 at level 10) before armour.
- SUPERNOVA's main blast does one and a half times the elemental's health
  plus 15 a level, and eight more blasts go off around it, 40 pixels out
  on each axis (the diagonal ones about 56 pixels out), each with half the
  main blast's damage and its reach: about 135 pixels from the elemental
  at level 10. The elemental is gone when they go off, so each blast is
  its own owner, and allies take every one of them in full.
- A standing burner keeps one ember under its feet: a new one drops (every
  4 ticks) only where no ember of its own still burns, so embers trail
  behind a moving elemental and do not pile up under a still one.
- The parting starburst on death always fires the starburst, whatever slot
  the elemental had selected, and it fires on top of SUPERNOVA.

**Everyone.**
- A toggle (DIG IN, IMMOLATE) ignores its own second press for 10 ticks.
- Holding Special repeats a mine, a hasten or a WARBAND call once per tick
  while magic lasts, as it repeats a bomb (a WARBAND call stops at six
  grunts). BLINK and SWAP wait out their rest, and SIPHON, GLIMMER and
  KNIFE FAN their attack pause, before the next.
- With the setting off two things still look different: in SDL a foe now
  sees the thief's poison cloud fade in and out instead of popping in at
  full, and a curses player sees their own team's cloud glyph during those
  ticks; and when a hero dies, its view holds still until the next hero is
  picked instead of flashing the map's top-left corner for a frame. That is
  drawing only; the game plays the same.

## Refusals at a glance

Every refusal is at most 24 characters, so it fits the compact HUD. The
`COULD NOT ...` reasons appear only when the world is full of entities.

| special | refusals |
|---|---|
| MINE | `MINE LIMIT REACHED`, `COULD NOT CREATE MINE` |
| WAIL | `NO FOE TO WAIL AT`, `COULD NOT CREATE WAIL` |
| SIPHON | `PHASED`, `SPECIAL BUSY` (an empty touch still spends) |
| POSSESS | see the table above |
| PHASE | `ALREADY PHASED`, `COULD NOT PHASE` |
| BLINK / SWAP | `SPECIAL BUSY`, `NOWHERE TO BLINK` / `NO ONE IN SIGHT`, `SWAP BLOCKED` |
| GLIMMER | `SPECIAL BUSY` |
| HASTEN / HASTE SELF | `NO ALLY IN REACH` |
| WISH | `NO FALLEN ALLY NEARBY`, `COULD NOT RESURRECT` |
| HOWL | the orc's own (`SPECIAL BUSY`) |
| EAT CORPSE | the orc's own (`ALREADY AT FULL HEALTH`, `NO CORPSE NEARBY`, `NO CORPSE IN RANGE`) |
| HOOK BLADE / KNIFE FAN | `BLADE ALREADY OUT`, `COULD NOT MAKE A BLADE` / `SPECIAL BUSY` |
| WAR BANNER / WARBAND | `NO CORPSE TO PLANT ON`, `COULD NOT RAISE BANNER` / `WARBAND ALREADY HERE`, `NO ROOM AT THE EDGE` |
| DIG IN | `SPECIAL BUSY`, `DIG IN SETTLING`, `POSSESSED BODY`, `NEED MANA TO STAY DOWN`, `COULD NOT DIG IN` |
| BONE WALL / BONE STORM | `NO ROOM FOR A WALL` / `NO WALL STANDING`; both `SPECIAL BUSY` while dug in |
| REASSEMBLE / LEGION | `ALREADY WARDED`, `COULD NOT WARD` / `LEGION ALREADY RISING`, `COULD NOT CALL LEGION`; both `SPECIAL BUSY` while dug in |
| STARBURST | `QUENCH IMMOLATE FIRST` while IMMOLATE burns (otherwise the classic starburst) |
| IMMOLATE | `IMMOLATE SETTLING`, `COULD NOT IGNITE` |
| METEOR RAIN | `NO TARGET IN RANGE`, `COULD NOT CALL METEORS`, `QUENCH IMMOLATE FIRST` |
| REKINDLE / SUPERNOVA | `ALREADY AT FULL HEALTH`, `QUENCH IMMOLATE FIRST` |

## What the HUD shows

The seat that owns a running timed effect sees how long it has left, in
seconds, in the manner of the frozen-time cell: `POSSESS: 49s`, `PHASE: 4s`,
`HASTE: 8s`, `DIG IN: 25s`, `REASSEMBLE: 30s`, `IMMOLATE: 12s`,
`LEGION: 25s`. The SDL HUD draws it in yellow at the foot of the seat's
view (one row above a running time freeze, which keeps its own row), the
curses status line adds `POSSESS 49s` after the time, and the text client's
`state` line gives every living `"timer"` and `"timer_ticks"`. All three
read one answer, `og::sim::seat_timer`, from fields every snapshot already
carries, so a mirror shows what the host would. When several run, the one
ending soonest is shown. With the setting off nothing is shown, the speed
potion's haste included, so the classic HUD does not change. The banner,
bone walls, mines, warband grunts and meteor rain are things on the map,
not countdowns, and are not shown.

The number is the effect's real end. DIG IN, REASSEMBLE and IMMOLATE end
when their clock or the mana runs out, whichever is first, so after every
drain their handler cuts the marker's clock down to what the remaining
mana can buy: a ward bought with 136 magic reads 30 s, and the same ward
after LEGION has spent the pool reads what the last 16 magic buy (8 s).

## What the DETAILS screen shows

TRAIN -> DETAILS composes the "Character Special Abilities" page from the
specials table as the session sees it (`og::ui::detail_page`), so the page
changes with the New Specials setting and can never name a special the
character does not have. Each special's text is the `detail` on its row in
the pack. A slot the character has reached is a block: the name in red
and its text under it; a slot with an alternate in play adds a
`Shift: <name>` line and the alternate's one-line `detail`. A slot the
character has not reached yet is one line at the foot of its column,
`Eat Corpse: lvl 4`. Slots 1 and 2 are in the left column, 3 to 5 in the
right. A character with no special in play reads `No special abilities.`
The HIRE box's last line is generated the same way
(`og::ui::specials_summary`): `Special:` and the slot names in play, in
slot order.
