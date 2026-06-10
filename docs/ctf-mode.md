# Capture the Flag

OpenGlad ships a Capture-the-Flag game mode as a built-in campaign:
**`org.openglad.ctf`** ("Capture the Flag"), levels 500–509. Pick it from
SET CAMPAIGN in the team-build menu and hit GO.

## Rules

- Each active team owns a flag standing at its base. Touch an enemy flag to
  pick it up (multiple enemy flags can be carried at once).
- Carry it back to your own flag stand to **capture** — your own flag must be
  at home, so defend it. First team to the capture limit wins.
- A carrier who dies drops the flag where they fell. Enemies may pick it up
  again; a teammate's touch returns it home instantly, and an untouched flag
  returns itself after 30 seconds. Flags dropped over water or walls return
  home immediately.
- Everyone **respawns**: fallen fighters return at their team's spawn anchors
  after a delay (default 10 seconds). A match never ends by team wipe.
- **Waypoints** (control points) sit at contested spots. Standing alone on
  one for a few seconds claims it; holding it halves your team's respawn
  wait and pulses a speed boost to teammates nearby. Captures also score
  classic points (gold for the winners' coffers).
- If the time limit runs out, the team with the most captures wins
  (score breaks ties).

## Match setup

Two settings appear in the team-build menu when the CTF campaign is active
(host-only in networked play; they ride the lobby like the PVP toggle):

- **CTF Teams** — `Auto` fields every team the map authors (2, 3, or 4);
  or force 2/3/4. Teams without human players get AI squads.
- **Capture Limit** — `Map default` uses the map's authored limit
  (3 unless the map says otherwise; Lagaren plays to 5), or force 1–10.

Allied and PvP play work exactly like the classic game: **PVP: Ally** puts
every human on one team against AI squads; **PVP: Enemy** gives each player
their own team. All of it works locally, split-screen, networked, on the
dedicated server, and in the text/curses clients.

## The maps

| # | Map | Teams | Source |
|---|-----|-------|--------|
| 500 | CTF: LAGAREN BATTLEGROUND | 2 | classic scen25, the pitched two-army field (plays to 5 caps) |
| 501 | CTF: DUNGEON OF STARS | 4 | classic scen23, teleporter quadrants |
| 502 | CTF: TIC AND TAC | 2 | classic scen11, fort vs. field |
| 503 | CTF: A BORDER FORT | 2 | classic scen42, siege duel |
| 504 | CTF: LAKE TACONA | 2 | classic scen45, lake shores + island teleporters |
| 505 | CTF: CITY OF NUTHRAM | 3 | classic scen17, city districts |
| 506 | CTF: FIRST BLOOD | 2 | original — tight starter arena |
| 507 | CTF: RIVER RUN | 2 | original — bridged river lanes |
| 508 | CTF: TRIAD | 3 | original — 120° radial forts |
| 509 | CTF: CROSSFIRE | 4 | original — pinwheel alleys |

Maps authored for more teams than the match fields simply mothball the extra
bases. The campaign loops: winning the last map starts the rotation over.

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
