[![GitHub license](https://img.shields.io/github/license/openglad/openglad)](LICENSE) [![GitHub all releases](https://img.shields.io/github/downloads/openglad/openglad/total)](https://github.com/openglad/openglad/releases) [![Latest release](https://img.shields.io/github/v/release/openglad/openglad)](https://github.com/openglad/openglad/releases/latest)

# <img src="https://avatars.githubusercontent.com/u/4483637?s=200&v=4" align="right" alt="logo" width="40" /> Openglad

Openglad is a port of the open-sourced dos game known as Gladiator
(http://fsgames.com/glad/). It is a top-view gauntlet style RPG that
features fast paced multiplayer action, several different classes, and
a scenario editor. This port adds networked multiplayer, five more
campaigns, seven versus modes, a browser build and a modding API in Lua.

**Play in your browser:** https://openglad.pages.dev · **Download:**
[latest release](https://github.com/openglad/openglad/releases/latest)
(Linux, Windows, macOS) · **Build from source:** [docs/INSTALL.md](docs/INSTALL.md)

Versions are `2.<commit count>`. The main menu shows the version and the
commit of the build you are running.

<table>
<tr>
<td><img src="https://raw.githubusercontent.com/openglad/openglad-screenshots/0b68eb3b99dc95e39b16ee00b9077e063f9a5d26/readme/mainmenu.png" width="400" alt="Main menu: Begin New Game, Continue, Load, Level Editor, Game Settings, Cloud Saves, Help, Quit, with the build version and commit stamped at the bottom"></td>
<td><img src="https://raw.githubusercontent.com/openglad/openglad-screenshots/0b68eb3b99dc95e39b16ee00b9077e063f9a5d26/readme/gameplay.png" width="400" alt="A red gladiator cut off in the woods by a magenta warband, health bars over every fighter"></td>
</tr>
<tr>
<td align="center"><sub>Main menu, stamped with its version and commit</sub></td>
<td align="center"><sub>The original Gladiator campaign</sub></td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/openglad/openglad-screenshots/0b68eb3b99dc95e39b16ee00b9077e063f9a5d26/readme/basecamp-four-seats.png" width="400" alt="Base Camp: a roster of eight gladiators above a rail of four local seats bound to WASD, the arrows, IJKL and TFGH"></td>
<td><img src="https://raw.githubusercontent.com/openglad/openglad-screenshots/0b68eb3b99dc95e39b16ee00b9077e063f9a5d26/readme/networking-lobby.png" width="400" alt="The NETWORKING screen hosting a room with two machines connected"></td>
</tr>
<tr>
<td align="center"><sub>Base Camp with four local seats</sub></td>
<td align="center"><sub>Hosting a networked room</sub></td>
</tr>
<tr>
<td><img src="https://raw.githubusercontent.com/openglad/openglad-screenshots/0b68eb3b99dc95e39b16ee00b9077e063f9a5d26/readme/mode-ctf.png" width="400" alt="A Capture the Flag match: the red squad holds the arena plaza while green closes in from the field, under the mode's announcement stack"></td>
<td><img src="https://raw.githubusercontent.com/openglad/openglad-screenshots/0b68eb3b99dc95e39b16ee00b9077e063f9a5d26/readme/ninefold-court.png" width="400" alt="The Ninefold Court: a Lua level script firing the ninefold judgment pulse"></td>
</tr>
<tr>
<td align="center"><sub>Capture the Flag</sub></td>
<td align="center"><sub>A Lua level script passing judgment (the Ninefold Court)</sub></td>
</tr>
</table>

## What's in it

**Playing**

- **Seven campaigns.** The original *Gladiator* (37 scenarios) and *The Tryxian Chronicles* (13) as released in 2002, plus five written for this port: *War of the Westlands* (25), *The Long Season* (19), *Imaginations* (a kid-submitted dream level), *The Endless Tower* (floors generated as you climb) and the *Multiplayer Game Modes* package. Each campaign is a plain source tree under [`campaigns/`](campaigns/), composed into a `.glad` archive at build time.
- **Seven versus modes, 40 arenas** — Team Deathmatch, Capture the Flag, Onslaught, Soccer, Basketball, Mutant and Free-for-All. Rules, scoring and bot behaviour are campaign Lua, and every mode works split-screen, online, in the terminal client and in the browser ([docs/mp-game-modes.md](docs/mp-game-modes.md)).
- **Multi-floor levels** — stairs, drops, and a camera that glides between floors ([docs/z-axis-design.md](docs/z-axis-design.md), [docs/floor-glide-design.md](docs/floor-glide-design.md)).
- **Difficulty, respawns and a real pause menu** — respawn rules (off / heroes / everyone) and permadeath per match, a PAUSED menu with per-player settings, named keyboard layouts and joystick support ([docs/game-modes.md](docs/game-modes.md), [docs/pause-menu-design.md](docs/pause-menu-design.md)).

**Multiplayer**

- **Four players on one screen, sixteen seats in a lobby** — local seats and networked machines in the same match; the simulation is server-authoritative, and players can join a session already in progress ([docs/ARCHITECTURE.md § Networking](docs/ARCHITECTURE.md#networking-and-multiplayer)).
- **A public relay, no port forwarding** — host a room, share its `GLAD-XXXX` code, friends join from any client; the relay is a Cloudflare Worker in [`relay/`](relay/) that also holds cloud saves ([relay/README.md](relay/README.md)).
- **Base Camp** — companies that persist across sessions, hiring and training, a seats rail for local and remote players, and campaign zones a campaign's own Lua can decorate ([docs/company-basecamp-design.md](docs/company-basecamp-design.md)).

**Modding**

- **Every class, weapon, effect and treasure is a Lua class pack** in [`packs/core/`](packs/core/), loaded through the same path third-party packs use ([docs/modding/api-reference.md](docs/modding/api-reference.md), [docs/lua-classpacks-design.md](docs/lua-classpacks-design.md)).
- **Scriptable levels and campaigns** — per-level hooks, scenario pickers and persistent company decisions, so a campaign can remember what you did ([docs/campaign-scripting-design.md](docs/campaign-scripting-design.md)).
- **Openscen**, the built-in level editor, and an indexed-PNG sprite pipeline that round-trips through Aseprite ([docs/scen.txt](docs/scen.txt), [docs/sprite-format.md](docs/sprite-format.md)).

**Ways to run it**

- **Browser** — the WebAssembly build at [openglad.pages.dev](https://openglad.pages.dev) runs on desktop and phone and can be added to a phone's home screen (it ships a [web app manifest](web/manifest.webmanifest)); every version deployed stays playable from [openglad.pages.dev/versions](https://openglad.pages.dev/versions/) ([docs/INSTALL.md § Web Build](docs/INSTALL.md#web-build-emscripten)).
- **Terminal** — `openglad_curses`, a zero-SDL roguelike-style client with the same menus, saves and online play ([docs/ncurses-client.md](docs/ncurses-client.md)).
- **Headless** — `openglad_server` hosts dedicated matches and `openglad_text` drives the game from a script, for servers and CI ([docs/INSTALL.md § Build Targets](docs/INSTALL.md#build-targets)).
- **Native** — Linux, Windows and macOS builds on every [release](https://github.com/openglad/openglad/releases), and a Nix flake.

**Under the hood**

- C++20 in five components with an enforced dependency direction, GoogleTest unit and integration groups, a byte-exact parity harness that pins gameplay against the classic simulation, and a coverage gate at 95 % of lines and 100 % of functions over `src/` and the pack Lua ([docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/GAMEPLAY_FIXES_FROM_CLASSIC.md](docs/GAMEPLAY_FIXES_FROM_CLASSIC.md)).
- Every build stamps `v2.<commit count>` and its git hash on the main menu; each push to `master` becomes a GitHub release and a versioned web URL automatically.

**Note:** as of July 1st, 2002, Gladiator is open sourced under the GPL.
The Forgotten Sages Development Team has graciously open sourced it,
and the developers of the Snowstorm team are sincerely thankful for
all the work that was saved.

*Thank you, FSGames.*

## Playing

The manual at [openglad.org/manual](https://openglad.org/manual) covers the
classes, controls, items, and scenarios. Pressing **F1** in a level opens the
in-game help, which is also readable as [glad.hlp](docs/glad.hlp).

### Multiplayer setup

Use **+** on Base Camp's **SEATS** rail to add local players. Open one of your
**P#** cards to choose that player's team and controls, or to remove the seat.
Put seats on the same team for co-op, or choose different teams for a versus
or mixed-team match. Use **<** and **>** when more than four seats are
connected. Seat count and team choices belong to the current session and are
not stored in a company save.

Player numbers are shared across the lobby. Your cards say **YOU**; cards
owned by another network client show its company abbreviation and are
read-only. Open **SCENARIO → VIEW LEVEL** for the complete seat and team
overview; the host's match settings sit on **SCENARIO** itself and
cross-control on **DIFFICULTY**. See
[Companies and Base Camp](docs/company-basecamp-design.md) for the full
multiplayer behavior.

## Documentation

* [Install / Build](docs/INSTALL.md) — CMake presets, native, web, previews and releases
* [Architecture](docs/ARCHITECTURE.md) — components, dependency rules, data flow, networking, CI
* [Modding API](docs/modding/api-reference.md) and the [class-pack design](docs/lua-classpacks-design.md)
* [Game modes](docs/mp-game-modes.md)
* [ncurses client](docs/ncurses-client.md)
* [Manual](https://openglad.org/manual)
* [Editing with Openscen](docs/scen.txt)
* [Cheats](docs/cheats.txt)
* [Gladiator ver. 3.8 manual & revision history](docs/glad.txt) (written by FSGames)

## License

GPL v2 — see [LICENSE](LICENSE).
