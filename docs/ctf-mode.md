# Capture the Flag

OpenGlad ships a Capture-the-Flag game mode as a built-in campaign:
**`org.openglad.ctf`** ("Capture the Flag"), levels 500–509. Pick it from
SET CAMPAIGN in the team-build screen's SCENARIO submenu and hit GO.

## Rules

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

Three CTF match settings appear in **MATCHUP**
(Base Camp → SCENARIO → MATCHUP) when the CTF campaign is active. In
networked play, only the host can change them, and the lobby synchronizes them
to every client:

- **CTF Teams** — `Auto` fields every team the map authors (2, 3, or 4);
  or force 2/3/4. Teams without human players get AI squads.
- **Capture Limit** — `Map default` uses the map's authored limit
  (3 unless the map says otherwise; the CROSSFIRE finale plays to 5),
  or force 1–10.
- **Scenario Troops** — keep the level's authored fighters, or field only
  the players' selected company fighters.

Assign player teams with the **SEATS** rail in Base Camp. Use **+** to add a
local seat, then open an owned **P#** card and choose **TEAM** in its editor.
The rail shows four cards at a time and **<**/**>** page through larger network
lobbies. Put several seats on one team for co-op, or spread them across teams
for a versus or mixed-team match. Remote cards are read-only.

The same editor selects 4- or 8-direction movement, remaps that local player's
keys, resets that player's controls, and removes the seat. Removing a machine's
last network seat leaves it connected as a spectator; **+** brings it back.
Seat membership and CTF team choices belong to the current session, not the
company file. A fighter's color remains its combat allegiance, so changing a
player-seat assignment does not recolor the company roster. The assignments
work locally, in split-screen and networked games, and through the dedicated
server.

## The maps

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
order, so a sparse custom map with flags on teams 0, 2, and 3 uses teams 0 and
2 in a two-team match. The campaign loops: winning CROSSFIRE (509) wraps the
rotation back to FIRST BLOOD (500).

## AI

Bots play the objective. Each squad fields attackers (fetch the nearest
enemy flag), escorts (shadow the carrier), defenders (hold the flag home),
interceptors (hunt the enemy carrying *your* flag), and a retriever (race to
a dropped flag). Carriers run home and fight only when cornered.

## Making your own CTF map

`tools/ctf_mapgen` (built via `scripts/generate_ctf_campaign.sh`) generates
the shipped campaign and is the reference for authoring:

1. A CTF level is a normal `.fss` scenario whose **type byte has bit `0x8`**
   set (`SCEN_TYPE_CTF`).
2. Place one **FLAG** treasure (family 13) per team, team byte = owner. Its
   position is that team's base. A flag's stat level ≥ 2 sets the map's
   capture limit.
3. Give each team a cluster of team start markers — they double as respawn
   anchors.
4. Optionally place **CTF POINT** treasures (family 14, team 7) at contested
   spots, and remove exits (CTF maps should not have portals).

The engine validates maps at match start; a map with fewer than two flag
teams falls back to classic rules.
