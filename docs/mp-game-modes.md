# Multiplayer Game Modes

OpenGlad ships seven competitive game modes in one built-in campaign:
**`modes`** ("Multiplayer Game Modes"), 39 scenarios. Pick it
from SET CAMPAIGN in the team-build screen's SCENARIO submenu and hit GO.
Every scenario title is prefixed with its mode:

| Mode | Levels | One line |
|------|--------|----------|
| Team Deathmatch | 300-305 | First team to the frag limit wins; kills of score-team fighters count. |
| Capture the Flag | 500-509 | Carry enemy flags home; first to the capture limit wins. |
| Onslaught | 800-803 | Destroy generators to FLIP them to your team; a team with none left is eliminated. |
| Soccer | 820-823 | Smack the ball into the enemy goal; first to the goal limit wins. |
| Basketball | 824-828 | Carry or shoot the ball through an enemy hoop; first to the point limit wins. |
| Mutant | 840-843 | Up to sixteen lone competitors; first blood crowns the Mutant, and only the Mutant can be hurt — kill it to take its place. |
| Free For All | 850-855 | Every fighter for itself, each in a color of its own; first to the frag limit wins. |

Every mode's rules, scoring, win logic, and AI directors live in the
campaign's embedded Lua pack (`modes.core`, hand-authored at
`campaigns/modes/packs/modes.core/`) — the
engine provides only the generic scripted
frame (`SCEN_TYPE_SCRIPTED` levels, `ModeState` replication, the respawn
engine, and the og.* mode bindings). All seven modes work in local
split-screen, networked play (mid-join included), the text and curses
clients, the dedicated server, and the web build.

## Capture the Flag rules


- Each active team owns a flag standing at its base. Touch an enemy flag to
  pick it up (multiple enemy flags can be carried at once).
- Carry it back to your own flag stand to **capture** — your own flag must be
  at home, so defend it. First team to the capture limit wins.
- A carrier who dies drops the flag where they fell. Enemies may pick it up
  again; a teammate's touch returns it home instantly, and an untouched flag
  returns itself after 30 seconds. Flags dropped over water or walls return
  home immediately.
- **Spell teleports drop the flag at the caster's feet** — every mage,
  archmage, and skeleton blink (marker beacons included, however short the
  hop) leaves every carried flag behind at the departure point. Riding a
  **map teleporter pad carries the flag through**. Humans and AI alike.
- Everyone **respawns**: fallen fighters return at their team's spawn anchors
  after a delay (default 10 seconds). A match never ends by team wipe.
- **Waypoints** (control points) sit at contested spots. Outnumbering
  everyone else on one (a strict majority of the fighters in its circle)
  for a few seconds claims it; even contests freeze the meter, and the
  owning team drains an attacker's progress at the same rate it accrued.
  Holding a waypoint halves your team's respawn wait and pulses a speed
  boost to teammates nearby. Captures also score classic points (gold for
  the winners' coffers).
- If the time limit runs out, the team with the most captures wins
  (score breaks ties).

## Match setup

Two match settings appear in **MATCHUP**
(Base Camp → SCENARIO → MATCHUP) when a versus campaign (one whose
campaign.yaml carries `matchup: versus`) is active; the curses client still
titles that screen `CTF Teams`, from when the versus campaign was CTF only. In
networked play, only the host can change them, and the lobby synchronizes them
to every client:

- **Match Teams** — `Auto` fields every team the map authors (2, 3, or 4);
  or force 2/3/4. Teams without human players get AI squads. Under
  `TROOPS: OWN`/`FAIR` the count works the same way — `Auto` means the
  map's own team count, an explicit count fields that many sides — with
  your deployed company's teams always staying and the rest backfilling in
  team order (TROOPS only decides what the AI squads are made of).
- **Score Limit** — `Map default` uses the map's authored limit (each mode
  reads it as its own win threshold: captures, frags, goals), or force
  1–10.

**TROOPS** sits one screen up, on **SCENARIO**, because it applies to every
campaign rather than only the versus ones. It is host-only and lobby-synced
like the pair above, and has three states:

- **TROOPS: ALL** — keep the level exactly as authored (the default).
- **TROOPS: OWN** — remove every fighter and generator the level ships, on
  every team, wildlife included; only the players' companies (and the AI
  squads the modes field for empty teams) remain. Onslaught keeps its
  generators, which are the board rather than troops. On a classic campaign
  this makes the level a sandbox: a kill-everything level is already won,
  and its named quest NPCs are gone — except characters the level marks
  protected, which survive either setting.
- **TROOPS: FAIR** — strip exactly as `OWN` does, and size the AI squads the
  modes field to the players (see "Matched squads" below). On a classic
  campaign — where no mode fields squads — it plays exactly as `OWN`.

Every state applies on every campaign, so the control reads the same
everywhere. An older save may carry a retired in-between state that stripped
only the roster teams' canned troops; it now behaves as **OWN**, and cycling
the control from it returns to **ALL**.

VIEW LEVEL previews the result: entries the setting will remove are flagged
in the scenario report.

Assign player teams with the seat rail in Base Camp. Use **+** at its left end
to add a local seat, then open an owned **P#** card and choose **TEAM** in its
editor.
The rail shows four cards at a time and **<**/**>** page through larger network
lobbies. Put several seats on one team for co-op, or spread them across teams
for a versus or mixed-team match. Remote cards are read-only.

The same editor selects 4- or 8-direction movement, remaps that local player's
keys, resets that player's controls, and removes the seat. Removing a machine's
last network seat leaves it connected as a spectator; **+** brings it back.
Seat membership and match team choices belong to the current session, not the
company file. A fighter's color remains its combat allegiance, so changing a
player-seat assignment does not recolor the company roster. The assignments
work locally, in split-screen and networked games, and through the dedicated
server.

### Matched squads (TROOPS: FAIR)

**TROOPS: FAIR** is the third state of the SCENARIO screen's TROOPS control.
It clears the board exactly as `OWN` does — the deployed companies are the
match — and then any AI squad a mode fields is sized to the players instead
of to the difficulty formula.

- The mode weighs every player's fighters on the field — health, armor,
  damage, firing rate, footspeed — and averages that across the human teams.
  Each AI squad is then fielded at whichever level lands closest to that
  number, with the first few of its members promoted one level further to
  fine-tune the fit.
- Squad size never moves: five fighters as usual, one per seat in Mutant.
  Only levels move, and only between 1 and 9. A lone level-1 soldier still
  faces a full squad — matching gets you as close as five bodies allow, not
  level with you.
- The match announces itself once at the start: **TEAMS MATCHED**, or
  **TEAMS MATCHED (LIMIT)** when a squad hit the level floor or ceiling and
  the fit is as close as it can get.
- DIFFICULTY still applies on top, and it bites harder than it looks: it
  scales health and damage together, so Easy leaves a matched squad far
  under your strength and Hard far over it. Normal is the fair fight.
- Teams with players on them are never filled or altered, and a squad wiped
  out and re-fielded comes back at the strength it was matched to. Who gets
  a squad follows `OWN`'s rule: a lone company gets one AI opponent; two or
  more companies fight each other and no squad is fielded at all.
- Onslaught ignores the sizing: its fighters come out of generators rather
  than squads, so FAIR strips there like `OWN` and the armies are unchanged.
- On a classic campaign no mode fields squads, so FAIR is simply `OWN`.

With several human teams the target is their average, so an even match for
one side can still be a hard one for a weaker ally.

## The CTF maps

The roster ramps playable area, walk time, and team count, alternating
originals and adapted classics (the playtest cut the giant maps — short
walks and dense fights won):

| # | Map | Teams | Size | Source |
|---|-----|-------|------|--------|
| 500 | CTF: FIRST BLOOD | 2 | 40×30 | original — tight starter arena |
| 501 | CTF: A BORDER FORT | 2 | 30×30 | classic scen42, siege duel |
| 502 | CTF: CASTLE CORNER | 2 | 30×40 | classic scen38, castle hall vs. mustering yard |
| 503 | CTF: THE OUTPOST | 2 | 40×60 | classic scen9, one-gate compound siege |
| 504 | CTF: RIVER RUN | 2 | 60×40 | original — bridged river lanes |
| 505 | CTF: TRIAD | 3 | 51×51 | original — 120° radial forts |
| 506 | CTF: THE UNDERPASS | 2 | 60×20 | classic scen36, single-tunnel corridor brawl |
| 507 | CTF: DUNGEON OF STARS | 4 | 70×70 | classic scen23, teleporter quadrants |
| 508 | CTF: CENTWHEIT MANOR | 3 | 50×50 | classic scen35, manor grounds |
| 509 | CTF: CROSSFIRE | 4 | 60×60 | original — pinwheel alleys, the finale (plays to 5 caps) |

Maps authored for more teams than the match fields simply mothball the extra
bases. An explicit team count takes the first N authored flag teams in team-ID
order, so a sparse custom map authored for teams 0, 2, and 3 uses teams 0 and
2 in a two-team match (the authored domain comes from team start markers). The campaign loops: winning CROSSFIRE (509) wraps the
rotation back to FIRST BLOOD (500).

## CTF AI

Bots play the objective. Each squad fields attackers (fetch the nearest
enemy flag), escorts (shadow the carrier), defenders (hold the flag home),
interceptors (hunt the enemy carrying *your* flag), and a retriever (race to
a dropped flag). Carriers run home and fight only when cornered.

## Basketball rules

- The ball opens at center court with a **jump ball**, and every basket or
  reset restarts it there: three seconds where nobody may touch it, then it
  pops up and the first fighter to reach it owns it. There is no possession
  arrow — every restart is a scramble.
- Walk onto a loose ball to pick it up. You then **carry** it: it rides with
  you, and the carrier is marked on every radar. The shadow on the floor is
  the ball's real position; the ball itself is drawn at its height above it.
- **Throwing is your fire input.** The next weapon you fire while carrying is
  spent on the throw instead, and where you aim decides which throw it is: an
  arc shot at a hoop in range, a pass to the teammate you are pointing at (a
  fast chest pass up close, a floating lob further out), or a flat throw at
  nothing in particular. Aiming dead at a cutter passes even when a hoop sits
  behind them.
- **Two ways to score.** Carry the ball into the carpet under an enemy hoop
  and you **dunk** it for 2 — it cannot miss, but you have to walk it through
  the defense. An arc shot is worth **2 from inside the painted ring, 3 from
  beyond it**; the further out you release, the wider it scatters, and every
  enemy crowding you at release scatters it further. The dunk ignores pressure
  entirely.
- **Reading the hoops.** A live hoop is an orange rim marked at four points
  in its defending team's colour, standing on the painted carpet that is the
  dunk zone. Its net ripples when a ball drops through and the rim flashes
  when a shot clangs off it, so you can call a make or a miss from the far
  end of the court.
- **The shot clock is 35 seconds**, and it belongs to the team rather than the
  carrier. Releasing a shot clears it. Losing the ball loose does not: a
  fumble your own side scoops back up, an uncaught pass, a throw rolled dead —
  all keep the same deadline, so missing on purpose buys nothing. Your score
  line grows a countdown at 10 seconds and calls "SHOT CLOCK!" at 3. Let it
  run out and the ball turns over, with your team barred from it for the next
  10 seconds.
- **Take a hit and you fumble.** Any real hit on the carrier pops the ball
  loose — scratch damage never does, and you get a moment's grace right after
  a pickup. That is the steal, and hounding the carrier is the on-ball
  defender's whole job.
- **Blocks and goaltending.** Shoot straight into an enemy's face and they
  **deny** it: while the ball is still at body height in the first instants
  after release, an enemy on it caroms the shot off their chest into a live
  scramble — "DENIED!", a deflection, never a catch. Past that window nobody
  blocks a shot with their body: only a weapon reaching the ball low swats it
  away, which is a clean block and leaves the ball live. The top of an
  arc is out of reach by design. Swatting a *falling* shot near the rim
  is **goaltending**, and the basket counts anyway. Chest passes fly low
  enough to be picked off by hand; a lob floats over everyone's heads
  and is only contestable at its ends.
- **Rebounds and banks.** A miss clangs off the rim into a live scramble;
  anyone may tip it in the air or scoop it off the floor. A ball dropping
  through any hoop scores **2 for whoever touched it last** — tip-ins,
  put-backs and bank shots all count. Low throws bounce off walls (that is the
  bank); shots and lobs clear them. Knock it through your *own* hoop and the
  points go to the opposition.
- First team to the point limit (21, or 11 on THE PLAYGROUND) wins; otherwise
  the clock decides it on points, and a shot already in the air at the buzzer
  is allowed to land. Fallen fighters respawn on the usual difficulty setting
  (five seconds by default), and a team wiped off the floor is put back at the
  next restart.
- Bots play the scheme rather than the ball: a handler who shoots when open,
  drives when pressed and passes when covered, two cutters working the wings,
  a rim protector in the dunk lane, and an on-ball defender hunting the
  fumble.
- **The drumsticks come back.** Ball contact chips everyone all match long, so
  every court's food returns to its own spot on a twenty-second trickle — one
  drumstick per refill, and a fighter standing on a spot holds it empty until
  they move.

## The basketball courts

| # | Court | Teams | Size | Twist |
|---|-------|-------|------|-------|
| 824 | Basketball: CENTER COURT | 2 | 45×25 | the reference floor — full arc, nothing in the way |
| 825 | Basketball: THE PLAYGROUND | 2 | 31×19 | cramped, short arc, everything is a three; plays to 11 |
| 826 | Basketball: FOUR HOOPS | 4 | 41×41 | a hoop on every wall; every rebound has four claimants |
| 827 | Basketball: THE BANKHOUSE | 2 | 45×27 | jutting backboards and pillars — straight passes die, banks live |
| 828 | Basketball: BENCHWARMERS | 2 | 47×29 | a tent per side raising skeleton substitutes, four at a time |

Rim size never changes; the courts vary the arc, the walk and the furniture.
FOUR HOOPS fielded with fewer than four teams simply leaves the unused hoops
dead — they neither score nor need defending, and a dead goal is bare carpet
with no rim standing on it, so the live baskets are obvious at a glance.

## Free For All rules

- **Every character you field is its own fighter.** FFA does not read the
  lobby: at the gate every deployed character — yours, your ally's, and the
  second one you seated on your own team — is put on a color of its own,
  and from there everyone is hostile to everyone. The hero you drive fights
  the rest of your own company, and MATCHUP's Match Teams has nothing to
  say about it.
- **Sixteen fighters, controlled heroes first.** Every seat's controlled
  hero takes a slot before any benchwarmer does, so a full lobby never
  loses a player to the cap; anyone past sixteen is turned away at the gate
  with a toast counting them. Bots fill the field out to the arena's
  fighter count — eight on THE MELEE, sixteen on the last three — cycling
  through soldier, archer, elf, mage and thief.
- **Sixteen colors, drawn fresh every match**: RED, GREEN, BLUE, YELLOW,
  MAGENTA, CYAN, TAN, ROSE, LAVENDER, SALMON, ORANGE, PINK, VIOLET, and
  three mixed for this mode — TEAL, GOLD and SLATE. Who wears which is
  shuffled at the gate, so last match's color tells you nothing about this
  one; within a match the color is the fighter, on the sprite, the radar
  dot, the beacon and the HUD row alike.
- **A kill is a point.** Killing another fighter scores 1. Killing yourself
  — in practice, dying on your own color while charmed — costs 1, and a
  count can go below zero. Wildlife, your own summons, and deaths the map
  hands out score nothing; a kill your generator or your summon lands
  counts as yours.
- **First to 15 kills wins** — the limit every arena authors; MATCHUP's
  Score Limit forces 1-10 as it does everywhere else. If the ten minutes
  run out first the most kills wins, and a tie goes to the earlier color in
  the list above.
- **You come back in seven and a half seconds, anywhere.** A start marker
  is a position pool here rather than a base: the map's four clusters are
  walked in rotation, so you return wherever the rotation lands, never at
  "your" corner. Nobody is eliminated and no wipe ends a match.
- **Join mid-match and you are in the fight at once**: you take the first
  free color, or the lowest-scoring bot's if all sixteen are spoken for —
  that bot leaves and its count restarts at zero for you. Arrive at a full
  field with no bots left to make room and you fight on your seat's color
  instead, hostile to everyone all the same.
- **The winner is a name, not a color.** The scoreboard line becomes
  `WINNER: <name>` in the winner's color — the character's own name,
  falling back to the color name for a bot or an unnamed fighter — and the
  results screen banner names the winning color.
- **The HUD carries the standing**: a **1ST** and a **2ND** row, each with
  a fighter's name and count in that fighter's color, and a **GOAL** row
  with the limit. Once the leader comes within three of the limit it grows
  a beacon on every radar — and every bot on the field turns and hunts it.
  The classic score box beside those rows is not the standing; it reads
  `SC: 0` all match.
- **No switching characters.** The others in your company are competitors
  in their own right, fighting under their own AI, so the switch-character
  input finds nothing to switch to: you play the one hero you are bound to,
  start to finish.
- **The arena is cleared at the gate.** Whatever garrison, squad or
  generator the map ships is retired before the first tick; wildlife stays
  as arena furniture unless TROOPS clears it too. Food and speed potions
  come back at their pads every 15 seconds — sixteen fighters eat a map
  bare.
- **TROOPS: FAIR plays as OWN here.** FFA retires the map's own fighters on
  every setting, and FAIR's matching never reaches its bots: they come in
  at the level DIFFICULTY implies rather than sized to your company.
- **The terminal clients only have eight colors**, so two fighters can
  share one in the curses and text views; the HUD names are what tell them
  apart.
- **Cloaking drops you off every radar but your own.** A radar keeps an
  invisible fighter's blip only for viewers wearing its color, and in a
  free-for-all nobody else wears yours.

## The FFA arenas

| # | Arena | Fighters | Size | Twist |
|---|-------|----------|------|-------|
| 850 | FFA: THE MELEE | 8 | 34×34 | an open colosseum under a ring of columns — the whole fight in one sightline |
| 851 | FFA: CROSSFIRE | 10 | 40×40 | a cobble island in a drowning moat, four plank bridges in |
| 852 | FFA: SHARDS | 12 | 44×44 | a broken hall of rooms, boulders strewn; no sightline longer than one room |
| 853 | FFA: THE ROSE | 16 | 45×45 | eight wall spokes off a cobble heart, one center pad, every petal a lane back to it |
| 854 | FFA: SCRAMBLE | 16 | 48×48 | a lattice of stub walls and twin mid-field pads — cover everywhere, safety nowhere |
| 855 | FFA: NIGHTFALL | 16 | 50×50 | a torch-lit corridor loop with offset mouths, chamber at the heart |

The fighter counts are the bot-fill targets, not caps: field more characters
than the number above and they all play, up to sixteen.

## The other modes, briefly

- **Team Deathmatch** (300-305, the six adapted arenas): kills of fighters
  on scoring teams count; wildlife, suicides, and environment deaths score
  nothing. Everyone respawns.
- **Onslaught** (800-803): sprawling generator fields. A lethal hit on a
  generator never destroys it — it flips, full-HP, to the attacker's team
  and starts pumping that team's fighters (existing troops keep their old
  colors). A team whose last generator flips away has a short grace to take
  one back — its HUD row counts the seconds down — and is eliminated when
  the clock runs out. Last team standing wins.
- **Soccer** (820-823): hit the ball to send it flying; a fast ball hurts
  and bounces off whoever it hits. Fully inside the enemy goal strip = a
  goal for the last-touching team. Dead heroes respawn, and the pitch's
  drumsticks come back to their own spots on a fifteen-second trickle — a
  full pitch is the hungriest field in the campaign.
- **Mutant** (840-843): a true free-for-all until the first kill crowns
  the Mutant. Every deployed character enters as its own competitor in a
  color of its own — up to sixteen of them, whatever teams the lobby put
  their seats on, two players sharing a team included — with bots filling
  the field out to four on the shipped arenas. The crown then works as it
  always did: the Mutant is buffed, marked on every HUD, its health always
  decaying and healed only by kills. Non-mutants cannot hurt each other;
  kill the Mutant to become it. Teleports are range-clamped so nobody
  blinks across the map.

## Making your own mode map

`tools/modes_mapgen` (built via `scripts/generate_modes_campaign.sh`)
generates the shipped campaign and is the reference for authoring:

1. A mode level is a normal `.fss` scenario whose **type byte has bit
   `0x20`** set (`SCEN_TYPE_SCRIPTED`). The campaign pack's Lua registers
   per-level hooks by scenario id (`pack/lib/mode_levels.lua` is the
   generated manifest).
2. Give each team a cluster of team start markers — they define the
   authored team domain AND double as respawn anchors.
3. CTF maps place one **flag** treasure (wire byte 13, `modes:flag`) per
   team and optional **waypoint** treasures (wire byte 14,
   `modes:waypoint`); both families ship IN the campaign pack.
4. Basketball maps paint a 3×3 dunk carpet per hoop — its center tile marks
   the rim — and the manifest records each hoop center, the three-point
   radius, and the jump-ball spot. The ball, its shadow and the rims are
   spawned by the mode, not authored.
5. FFA maps still author four marker clusters, but **interleaved** around
   the arena instead of parked in four corners: the mode reads them as
   position pools rather than bases and walks them in rotation, so
   consecutive spawns land far apart. Keep the list a whole number of
   rounds (spot *i* belongs to team *i* mod 4) or one pool ends up with
   more stops than the others. The manifest row's `fighters` count is the
   bot-fill target, and there is nothing else to author — no bases, no
   objective props.
6. Remove exits (mode maps should not have portals).

The mode's Lua validates maps at match start; a scripted map whose mode
fails to activate (or that registers no hooks) falls back to classic
rules on the next tick.
