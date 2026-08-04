# Multiplayer Game Modes

OpenGlad ships five competitive game modes in one built-in campaign:
**`org.openglad.modes`** ("Multiplayer Game Modes"), 28 scenarios. Pick it
from SET CAMPAIGN in the team-build screen's SCENARIO submenu and hit GO.
Every scenario title is prefixed with its mode:

| Mode | Levels | One line |
|------|--------|----------|
| Team Deathmatch | 300-305 | First team to the frag limit wins; kills of score-team fighters count. |
| Capture the Flag | 500-509 | Carry enemy flags home; first to the capture limit wins. |
| Onslaught | 800-803 | Destroy generators to FLIP them to your team; a team with none left is eliminated. |
| Soccer | 820-823 | Smack the ball into the enemy goal; first to the goal limit wins. |
| Mutant | 840-843 | FFA until first blood; only the Mutant can be hurt, kill it to take its place. |

Every mode's rules, scoring, win logic, and AI directors live in the
campaign's embedded Lua pack (`org.openglad.modes.core`, source under
`tools/modes_mapgen/pack/`) — the engine provides only the generic scripted
frame (`SCEN_TYPE_SCRIPTED` levels, `ModeState` replication, the respawn
engine, and the og.* mode bindings). All five modes work in local
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
campaign.yaml carries `matchup: versus`) is active. In
networked play, only the host can change them, and the lobby synchronizes them
to every client:

- **Match Teams** — `Auto` fields every team the map authors (2, 3, or 4);
  or force 2/3/4. Teams without human players get AI squads.
- **Score Limit** — `Map default` uses the map's authored limit (each mode
  reads it as its own win threshold: captures, frags, goals), or force
  1–10.

**TROOPS** sits one screen up, on **SCENARIO**, because it applies to every
campaign rather than only the versus ones. It is host-only and lobby-synced
like the pair above, and cycles three ways:

- **TROOPS: SCEN** — keep the level exactly as authored (the default).
- **TROOPS: OWN** — versus campaigns only: each team that fields company
  fighters loses the level's canned troops and generators for that team, so
  the match is played by the rosters. The level's wildlife and neutral
  garrison stay.
- **TROOPS: NONE** — remove every fighter and generator the level ships,
  on every team, wildlife included; only the players' companies (and the AI
  squads the modes field for empty teams) remain. Onslaught keeps its
  generators, which are the board rather than troops. On a classic campaign
  this makes the level a sandbox: a kill-everything level is already won,
  and its named quest NPCs are gone — except characters the level marks
  protected, which survive every setting. On classic campaigns the cycle
  skips **OWN**, which has no meaning there.

VIEW LEVEL previews the result: entries the setting will remove are flagged
in the scenario report.

Assign player teams with the **SEATS** rail in Base Camp. Use **+** to add a
local seat, then open an owned **P#** card and choose **TEAM** in its editor.
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

## The other modes, briefly

- **Team Deathmatch** (300-305, the six adapted arenas): kills of fighters
  on scoring teams count; wildlife, suicides, and environment deaths score
  nothing. Everyone respawns.
- **Onslaught** (800-803): sprawling generator fields. A lethal hit on a
  generator never destroys it — it flips, full-HP, to the attacker's team
  and starts pumping that team's fighters (existing troops keep their old
  colors). A team whose last generator flips away has a short grace to take
  one back; otherwise it is eliminated. Last team standing wins.
- **Soccer** (820-823): hit the ball to send it flying; a fast ball hurts
  and bounces off whoever it hits. Fully inside the enemy goal strip = a
  goal for the last-touching team. Dead heroes respawn.
- **Mutant** (840-843): free-for-all until the first kill crowns the
  Mutant — buffed, marked on every HUD, health always decaying, healed
  only by kills. Non-mutants cannot hurt each other; kill the Mutant to
  become it. Teleports are range-clamped so nobody blinks across the map.

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
4. Remove exits (mode maps should not have portals).

The mode's Lua validates maps at match start; a scripted map whose mode
fails to activate (or that registers no hooks) falls back to classic
rules on the next tick.
