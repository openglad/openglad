# New Specials

Six classes gain new specials: the thief, the ghost, the faerie, the orc
captain, the skeleton and the fire elemental. Each new special belongs to
one setting, **New Specials**, which is on by default. With it off, every
class plays exactly as it did before the kits existed, with one exception:
FREEZE TIME, HOWL, SCARE and MIND CONTROL rest between casts whatever the
setting (see [Rests](#rests)). The players' list of
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
will use when it hosts. The value is never written into a company save.

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

Versions: protocol v21, snapshot v17, replay v23 (v19/v15/v21 brought the
setting; v20/v16/v22 the rest clock, below; v21/v17/v23 the Gameplay Fixes
setting, docs/gameplay-fixes.md).

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
  `false` would skip a random draw the classic game makes. The one gate
  that answers no with the setting off is a classic special's rest (see
  Rests), and the old game at the parity commit carries the same rule.
  Every kit gate that chooses a Shift arm chooses only an arm the bot can
  pay. With the setting on, each kit gate asks when its special pays off,
  as the classic specials' gates do under Gameplay Fixes
  ([gameplay-fixes.md](gameplay-fixes.md), Bots); the kit rules are in
  the player notes below.

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

**An engine note.** With the setting off a hidden slot costs 5000, and
`walker::special()` refuses a slot priced 5000 or more before it looks at
the walker's magic: the cast answers `NO SPECIAL AVAILABLE`
(`SpecialFailure::Unavailable`) whatever the pool, so a walker forced onto
a hidden slot casts nothing. Nothing in play reaches that, since the
player's cycling and the bot's slot pick both read `NONE` and never put a
walker on a hidden slot; only a test that sets the slot by hand can. The
classic fire elemental still casts its starburst from slot 1 (its
`default_cast`). This refusal applies whatever the Gameplay Fixes setting
says ([gameplay-fixes.md](gameplay-fixes.md), "Not gated").

## Engine state

Three per-entity fields, all in every snapshot:

| field | type | dirty bit | meaning |
|---|---|---|---|
| `kit_state` | `uint8` | 94 | `KIT_HIDDEN` 1, `KIT_FEARLESS` 2, `KIT_WARD` 4, `KIT_CHANNEL` 8, `KIT_QUARTER_FREEZE` 16 (on a weapon: the freeze it lays is a quarter of the rolled one; GLIMMER's sprinkles), `KIT_NOVA` 32 (on a blast: a SUPERNOVA blast, which spares its own team half the damage by team colour) |
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
| 2 | 340 | 28 |
| 5 | 478 | 40 |
| 6 | 510 | 43 |
| 9 (a level-10 ghost on a level-1 mage) | 580 | 48 |
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
- A bot ghost wails instead of scaring only when exactly one living foe
  is inside the scare radius and a second stands within 120 pixels of it,
  the chain's shape, and it has the 50 to pay; a crowd gets SCARE.
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
- A bot thief lays a MINE instead of its bomb only where a foe will walk:
  a foe past the bomb's blast and within 130 pixels that is walking at the
  thief, fewer than three mines down, no ally in the blast, and the 70 in
  hand; it never lays one on the coin.

**Ghost.**
- WAIL is a small puff of the scare's own sparkles (`expand8.png`), not a
  bolt. It travels 6 pixels a tick for up to 60 ticks, homes on its foe's
  centre and frights it when its 8x8 core touches, for a quarter of the
  scare's time (the scare's fright divided by `wail_fright_divisor`, a
  hero's constitution roll taken off after the cut: 62 ticks from a level
  10 ghost, 93 at the scare's ceiling); then a fresh puff hops on to a
  foe within 120 pixels of the one it struck, at most three more times.
  It does not treat fliers differently. Its numbers (`wail_bolt_life`,
  `wail_bolt_step`, `wail_fork_px`, `wail_hops`, `wail_frame_ticks`,
  `wail_fright_divisor`) are on the `core:wail` family, not the ghost.
- SIPHON touches once per attack pause: a tap is one touch, and a held key
  touches again only when the ghost's pause has run (7 ticks for a bare
  ghost, a little less for a nimble hero). Inside the pause it answers
  `SPECIAL BUSY` and spends nothing. A touch that finds no foe still spends
  its mana and sets no pause.
- A resisted POSSESS lets the host strike once.
- A ghost riding a body regains no magic.
- PHASE lasts 48 ticks at double speed, and the ghost cannot attack while
  it lasts, not even on the tick it casts. A bot ghost never phases again
  while its veil is up, and phases only when SCARE cannot answer: SCARE
  resting, or no living foe inside the scare radius.
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
  blinks away instead, or holds. Its foe counts are of living foes: a tent
  does not make a faerie blink.
- HASTEN keeps a stronger speed bonus already running (a high-level speed
  potion is not cut down to the haste).
- A bot faerie hastens a fighting ally that is not already quick, and
  hastens it again once that haste has worn off. Before the speed counter
  was fixed to count down to 0 (`docs/GAMEPLAY_FIXES_FROM_CLASSIC.md`) an
  ally hastened once was never hastened again.
- GLIMMER costs 40 and rests 15 seconds between casts (see Rests below);
  it also pauses the faerie like one attack. Each of its sprinkles is
  marked, and the freeze it lays is a quarter of the rolled one (about
  half a second at level 4); the roll itself is drawn as for any sprinkle.
- WISH kills the faerie: her stain and, for a hero, her life gem stay.

**Orc captain.**
- The captain cannot be hired: it is the orc's promotion only (an orc of
  level 5, on the DETAILS screen). As in the classic game the promoted
  captain starts again at level 1, with HOWL and EAT CORPSE both open;
  HOOK BLADE / KNIFE FAN opens at 7, WAR BANNER / WARBAND at 10. With the
  setting off the captain has no specials, as in the classic game.
- EAT CORPSE's row in the specials table says `level = 1`: it is the one
  slot in the game that opens before the classic level for its place
  (slot N at level 3N - 2). The cycle key, a bot's slot roll, the DETAILS
  page and the results screen all ask the same rule
  (`og::sim::slot_open_level`). A level-1 captain has a 25-point pool, so
  one HOWL (25) or one EAT CORPSE (20) a full pool.
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
- A bot captain howls when a living foe or a generator is inside the yell
  (160 pixels plus 20 per level, at most 420). EAT CORPSE has its own bot
  gate: it eats only below 60 percent of its health (at exactly 60 percent
  it does not) and standing on a corpse. HOOK BLADE wants a living foe
  within the blade's 60 pixels, no blade out and the 40 in hand; KNIFE FAN
  wants at least two living foes inside the knives' flight on the three
  lines it throws; WAR BANNER also wants a living foe within 120 pixels.

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
- A skeleton keeps only so many bone walls standing, more as it levels:
  8 x level / (level + 3), rounded half up (the tuning keys
  `wall_cap_ceiling` 8 and `wall_cap_knee` 3). That is 2 walls at level 1,
  3 at level 2, 4 at level 3, 5 at level 5, 6 at level 9, 7 at level 13
  and 8 from level 45 on, never more. The count is of segments, three to a
  wall: when a new wall would put the skeleton over, its oldest segments
  fall quietly (no bones, no storm, no notice) until the new wall fits, so
  a wall a foe has chipped holds less room and only as many old segments
  fall as are needed. The new wall itself is built as before (`NO ROOM FOR
  A WALL` where it cannot stand, and then nothing falls).
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
- A bot skeleton raises its BONE WALL across the line to the nearest
  living foe within 60 pixels; BONE STORM counts the foes at the wall (two
  within 60 pixels of a standing segment), not at the skeleton; REASSEMBLE
  wants a living foe within 60 pixels; LEGION counts only living foes that
  are not undead, the ones that can rise. DIG IN counts living foes: a tent
  does not press a skeleton underground.

**Fire elemental.**
- Quenching IMMOLATE is free; until then the elemental casts nothing else.
- METEOR RAIN strikes every 6 ticks (10 strikes over its 60 ticks), each
  for 8 + 2 a level (22 at level 7, 28 at level 10) before armour.
- SUPERNOVA's main blast does one and a half times the elemental's health
  plus 15 a level, and eight more blasts go off around it, 40 pixels out
  on each axis (the diagonal ones about 56 pixels out), each with half the
  main blast's damage and its reach: about 135 pixels from the elemental
  at level 10. Allies take each blast at half and foes in full: the
  elemental is gone when they go off, so each blast is its own owner and
  remembers it is a nova, sparing its own team colour half the damage.
- A standing burner keeps one ember under its feet: a new one drops (every
  4 ticks) only where no ember of its own still burns, so embers trail
  behind a moving elemental and do not pile up under a still one.
- The parting starburst on death always fires the starburst, whatever slot
  the elemental had selected, and it fires on top of SUPERNOVA.
- A bot lights IMMOLATE with a living foe within a blow and more than 60
  magic, and quenches it when no living foe is within 40 pixels, so the
  burn stops draining the pool for nothing. REKINDLE is never forced on a
  pool that cannot pay it.

**Everyone.**
- A toggle (DIG IN, IMMOLATE) ignores its own second press for 10 ticks.
- Holding Special repeats a mine, a hasten or a WARBAND call once per tick
  while magic lasts, as it repeats a bomb (a WARBAND call stops at six
  grunts). BLINK and SWAP wait out their rest, and SIPHON and KNIFE FAN
  their attack pause, before the next. GLIMMER, FREEZE TIME, HOWL, SCARE /
  WAIL and MIND CONTROL wait out their rest (below).
- With the setting off two things still look different: in SDL a foe now
  sees the thief's poison cloud fade in and out instead of popping in at
  full, and a curses player sees their own team's cloud glyph during those
  ticks; and when a hero dies, its view holds still until the next hero is
  picked instead of flashing the map's top-left corner for a frame. That is
  drawing only. In play, the rests of FREEZE TIME, HOWL, SCARE and MIND
  CONTROL (below) hold with the setting off too.

## Rests

The specials that can upset a whole fight wait between casts. A rest does
not hold up ordinary attacks or any other special; it only refuses a second
cast of the same special until it is over, naming the seconds left (rounded
as the HUD rounds them), for example `FREEZE TIME IN 24S`. A refused cast
spends nothing.

| special | rest | refusal | with the setting off |
|---|---|---|---|
| FREEZE TIME (mage) | 30 s (360 ticks) | `FREEZE TIME IN 30S` | rests too |
| HOWL (orc) | 10 s (120 ticks) | `HOWL IN 10S` | rests too |
| SCARE (ghost) | 8 s (96 ticks) | `SCARE IN 8S` | rests too |
| MIND CONTROL (archmage) | 20 s (240 ticks) | `MIND CONTROL IN 20S` | rests too |
| GLIMMER (faerie) | 15 s (180 ticks) | `GLIMMER IN 15S` | no GLIMMER |
| WAIL (ghost; one rest with SCARE) | 8 s (96 ticks) | `WAIL IN 8S` | no WAIL |
| HOWL (orc captain) | 10 s (120 ticks) | `HOWL IN 10S` | no captain's specials |

The four classic rests are not the setting's: they are a fix to the classic
specials, made in this game and in the old game at the parity commit alike
([GAMEPLAY_FIXES_FROM_CLASSIC.md](GAMEPLAY_FIXES_FROM_CLASSIC.md)).

The lengths are tuning keys on each family (`glimmer_rest`, `freeze_rest`,
`howl_rest`, `scare_rest`, `charm_rest`). A bot does not pick a resting
special: its gate for that slot answers no until the rest is over. A rest
is not shown in the HUD countdown (it is not an effect on the player).

The rest is a clock on the caster: `special_rest_left` (ticks) and
`special_rest_slot` (the 1-based slot that rests, 0 for none), two walker
fields that ride every snapshot (protocol v20). `living::act` counts the
clock down one tick per turn the caster takes, right after the speed bonus,
so it keeps counting while its caster is frozen, runs faster in bonus rounds
as haste does, and waits while time is stopped for the caster's side (the
caster takes no turn). One wrapper starts and reads it
(`packs/core/lib/living_common.lua`, `with_rest`), and the bots' gates read it
(`gate_with_rest`). Each walker has one clock, which is enough: no class has
two rested specials (SCARE and WAIL share the ghost's slot 1).

## Refusals at a glance

Every refusal is at most 24 characters, so it fits the compact HUD. The
`COULD NOT ...` reasons appear only when the world is full of entities.

| special | refusals |
|---|---|
| MINE | `MINE LIMIT REACHED`, `COULD NOT CREATE MINE` |
| WAIL | `WAIL IN 8S` (the rest it shares with SCARE), `NO FOE TO WAIL AT`, `COULD NOT CREATE WAIL` |
| SIPHON | `PHASED`, `SPECIAL BUSY` (an empty touch still spends) |
| POSSESS | see the table above |
| PHASE | `ALREADY PHASED`, `COULD NOT PHASE` |
| BLINK / SWAP | `SPECIAL BUSY`, `NOWHERE TO BLINK` / `NO ONE IN SIGHT`, `SWAP BLOCKED` |
| GLIMMER | `GLIMMER IN 15S` (its rest), `SPECIAL BUSY` |
| HASTEN / HASTE SELF | `NO ALLY IN REACH` |
| WISH | `NO FALLEN ALLY NEARBY`, `COULD NOT RESURRECT` |
| HOWL | `HOWL IN 10S` (its rest), the orc's own (`SPECIAL BUSY`) |
| FREEZE TIME, SCARE, MIND CONTROL (classic) | their rest, whatever the setting: `FREEZE TIME IN 30S`, `SCARE IN 8S`, `MIND CONTROL IN 20S` |
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
seconds, in the manner of the frozen-time cell: `POSSESS: 48s`, `PHASE: 4s`,
`HASTE: 8s`, `DIG IN: 25s`, `REASSEMBLE: 30s`, `IMMOLATE: 12s`,
`LEGION: 25s`, and for the classic timers on the seat's own character
`SUMMONED` (a summoned body the seat drives), `CHARMED`, `STUNNED`,
`INVISIBLE` (the thief's CLOAK, the invisibility potion), `INVINCIBLE` (the
invulnerability potion) and `FLYING` (the flight potion). The SDL HUD draws it in yellow at the foot of the seat's
view (one row above a running time freeze, which keeps its own row), the
curses status line adds `POSSESS 48s` after the time, and the text client's
`state` line gives every living `"timer"` and `"timer_ticks"`. All three
read one answer, `og::sim::seat_timer`, from fields every snapshot already
carries, so a mirror shows what the host would. When several run, the one
ending soonest is shown; a tie goes to the kit's effects first (POSSESS,
PHASE, DIG IN, REASSEMBLE, IMMOLATE, LEGION), then SUMMONED, CHARMED,
STUNNED, INVISIBLE, INVINCIBLE, FLYING, HASTE. With the setting off the
kit's effects cannot exist and are never shown, but the classic timers
are: this is a general HUD fix, drawing only, and the game itself does not
change. FLYING shows only above 1 tick: a walker held over water keeps a
flight of 1 every tick. HASTE shows the speed field as it is: it counts
down to 0, one tick of speed for each. PHASE writes
its own invulnerability and speed beside its veil, so while the veil runs
the cell says PHASE and never INVINCIBLE or HASTE. The banner, bone walls,
mines, warband grunts, the seat's own summons, the cleric's mace and
meteor rain are things on the map, not countdowns, and are not shown;
nor is the druid's reveal (the radar shows it) or fright (its own SCARED
row).

Every countdown is in whole seconds, rounded to the nearest (the game runs
12 ticks a second, so half a second rounds up): 580 ticks read 48 s. The
last 5 ticks of an effect read `0s`, and the cell stays up, at `0s`, until
the effect ends. The frozen-time cell (the time freeze that stops every
foe) reads the same way, on every client and with the setting on or off:
`TIME LEFT: 25s` on the SDL HUD (`T: 25s` in a narrow pane) and `TIME 25s`
on the curses status line, through the same `og::sim::timer_seconds`. So do
the other countdowns a player sees: `SCARED: 2s` while a fright drives the
seat's walker, the respawn countdown (`RESPAWN IN 5` on the SDL HUD,
`RESPAWN 5` on the curses status line) and the next `WAVE: 4s`; there is no
other ticks-to-seconds rule. Only the display changed: the freeze itself,
the respawn queue and the text client's raw `"timer_ticks"` stay in ticks.

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
`Hook Blade: lvl 7`. Slots 1 and 2 are in the left column, 3 to 5 in the
right. A character with no special in play reads `No special abilities.`
The HIRE box's last line is generated the same way
(`og::ui::specials_summary`): `Special:` and the slot names in play, in
slot order.

## The scene tests

Every new special has a scene that plays it in the real game, in the
`NewSpecials` suite of `tests/integration/test_special_scenes.cpp` (binary
`og_test_special_scenes`): the seat's own hero at level 10 casts it with the
keys a player would press, every other actor is staged on the authoritative
server world, and the test asserts on the display what the special visibly
does, its tells: the mine on the ground and the orc that trips it, the wail
hopping through a pack, the HUD cell counting a rest down. Twenty-two
scenes: one per special or chained pair of specials, three of a bot using one, and one of a classic
special's countdown with the setting off. They run on every lane like any
other test, one gtest each.

The pictures in the pull request are made from those tests, not the other
way round. `scripts/media/capture_new_specials.sh` runs the suite with
`OG_FX_CAPTURE_DIR` set, under which the shared play-through also writes
every frame and the index of the frame to keep as a still, and encodes a
GIF, a review strip and the still per scene. The scenes play in their
declared order in one process; seven of them show slightly different fire
glow and dust when played alone, because the render-effects clock is global
and runs on from one scene to the next, so published pictures come from the
full run.
