# OpenGlad Architecture

OpenGlad is a cross-platform C++ port of the DOS game **Gladiator** (1995) — a top-down, gauntlet-style action RPG with up to 4-player split-screen multiplayer, 15+ character classes, a built-in scenario editor, and campaign support. Licensed under GPL v2.

The codebase has been through an aggressive modernization (branch `cpp-modernization-plan`) that introduced modular architecture with enforced dependency rules, RAII ownership, a deterministic simulation layer, concurrent multi-session support, and a modern CMake build system targeting C++20.

---

## Table of Contents

- [Repository Layout](#repository-layout)
- [Module Structure](#module-structure)
- [Dependency Direction Rules](#dependency-direction-rules)
- [Key Data Structures](#key-data-structures)
- [Concurrent Sessions](#concurrent-sessions)
- [Data-Driven Family System](#data-driven-family-system)
- [Game Loop](#game-loop)
- [Networking and Multiplayer](#networking-and-multiplayer)
- [Campaigns, Levels, and Scenarios](#campaigns-levels-and-scenarios)
- [Custom Sprite Packs](#custom-sprite-packs)
- [Build System](#build-system)
- [Test Structure](#test-structure)
- [Important Files and Entry Points](#important-files-and-entry-points)

---

## Repository Layout

```
openglad/
├── include/openglad/       Public headers (stable module API surface)
│   ├── core/               Foundation utilities and shared contracts
│   ├── gameplay/           Simulation + entities + world state
│   ├── resources/          Save/level/config/filesystem/archive APIs
│   ├── interface/          UI, input translation, rendering-facing APIs
│   ├── platform/           Runtime/session/platform bridges (SDL, text client)
│   └── legacy/             Transitional headers (base.h, etc.)
│
├── src/                    Private implementation
│   ├── core/               combat_math.cpp, util.cpp
│   ├── gameplay/           walker/living/weap/treasure/effect, game_world, sim input
│   ├── resources/          gloader, gparser, level/save IO, filesystem/zip/yaml
│   ├── interface/          screen/view/render/text, picker/help/editor/results UI
│   └── platform/           sdl runtime/session/loop/audio/video, text client
│
├── tests/                  Integration test suite (SDL)
│   └── unit/               Headless unit tests
│
├── cmake/                  CMake support files
├── scripts/                Build, test, and CI scripts
├── web/                    Emscripten HTML shell and landing page
├── docs/                   Architecture documentation
│   └── external-dependencies.md  Package targets and FetchContent pins
│
├── packs/                  Class packs (built-in core pack: families/*.lua declarations, lib/ modules)
├── cfg/                    Runtime configuration (openglad.yaml)
├── pix/                    Indexed-color sprite PNGs + Aseprite JSON sidecars (see [docs/sprite-format.md](sprite-format.md))
├── sound/                  Audio files (WAV, OGG)
├── campaigns/              Built-in campaign SOURCE trees (one dir per campaign id;
│                           the build composes each into build/<preset>/builtin/<id>.glad)
├── scen/                   Scenario data files
│
├── CMakeLists.txt          Build entry point (options, helpers, targets, install)
├── cmake/                  Build modules included by CMakeLists.txt:
│                           OpenGladSources (component source lists),
│                           OpenGladDependencies (external packages/FetchContent),
│                           OpenGladTests (test binaries + CTest),
│                           OpenGladCoverage (the coverage gate)
├── CMakePresets.json        Build presets (dev, CI, web)
└── CLAUDE.md               AI assistant instructions
```

---

## Module Structure

OpenGlad now uses **4 top-level components + a core foundation layer**. The
component targets are:

- `og_core`
- `og_gameplay`
- `og_resources`
- `og_interface`
- `og_platform_sdl`

Legacy fine-grained module names (`sim`, `data`, `entities`, `runtime`,
`render`, `input`, `ui`, etc.) remain visible in paths and headers, but
build/dependency enforcement is now done at the component level.

### `og_core` (foundation)

Pure utilities and fundamental types (`combat_math`, `util`, constants, common
interfaces). No gameplay/session ownership logic.

### `og_gameplay`

Deterministic simulation and entity behavior:

- `GameWorld`, `GameplayContext`
- `walker` family (`living`, `weap`, `treasure`, `effect`)
- pathing, combat, AI families, simulation input handling
- **networking core** (SDL-free): `GameServer` (authoritative sim host),
  `GameClient` (mirror), `LobbyServer`, `WorldSnapshot` (full + delta state
  serialization), and the `ITransport` interface with the in-process and
  multiplex implementations. See [Networking and Multiplayer](#networking-and-multiplayer).

Gameplay is intentionally sandboxable and does not own rendering/audio devices.

### `og_resources`

Data and persistence:

- campaign/level/save/config loading and parsing
- filesystem/archive/yaml wrappers
- pixie/asset loading (indexed-color PNGs + Aseprite JSON sidecars; artist
  workflow and palette contract are documented in
  [docs/sprite-format.md](sprite-format.md))
- the game-mode seam (`og::mode::IProgression` meta-progression interface,
  the shared level-win fold, and the campaign.yaml `mode:` identity key; the
  two-tier mode taxonomy and the add-a-mode checklist are documented in
  [docs/game-modes.md](game-modes.md))

### `og_interface`

UI, view, and presentation logic:

- menus (`picker`, dialogs, campaign/level picker, results/help/intro)
- rendering helpers (`view`, `text`, `radar`, `walker_draw`)
- level editor tooling/state
- player input translation and UI model state

### `og_platform_sdl`

SDL-specific platform/runtime wiring:

- `GameSession`, lifecycle, loop bridges, native input event bridge
- SDL video/audio integration and startup entrypoint support
- the lobby/gameplay bridge: `local_transport_shadow` (runs every game through
  the client-server architecture) and the picker lobby clients
  (`picker_lobby_client` / `picker_lobby_network_client`)

This is the outermost layer and can include all component headers.

Two thin transport libraries sit beside the SDL platform layer and link the
WebSocket dependency:

- `og_platform_ws_transport` — native WebSocket client/server + relay transports
  (links `og_ext_ixwebsocket`, backed by a package-manager IXWebSocket or the
  pinned upstream FetchContent dependency)
- `og_platform_emscripten_transport` — browser WebSocket + relay transports

---

## Dependency Direction Rules

Dependencies now flow through components:

```
og_core
  ↑
og_gameplay
  ↑
og_resources
  ↑
og_interface
  ↑
og_platform_sdl
```

### Include Boundary Rules

Each component target is built with restricted include roots:

- `og_gameplay`: `core/`, `gameplay/`
- `og_resources`: `core/`, `gameplay/`, `resources/`
- `og_interface`: `core/`, `gameplay/`, `resources/`, `interface/`
- `og_platform_sdl`: full tree

### Enforcement

Build and CI checks enforce boundaries:

1. CMake target-level include root restriction per component
2. `scripts/check_vendor_leaks.sh` external dependency include checks
3. `scripts/check_vendor_leaks.sh` component dependency include checks

### Runtime Context and Thread-Local Rules

Phase 12 retired global context fallback behavior:

- `set_global_context()` is removed
- `ctx()` is strictly session-backed (`current_session->ctx_`) and asserts when no session exists
- gameplay thread-local game-state pointer: `current_game`
- platform thread-local game-state pointer: `current_session`

### Global State Audit (Phase 12)

Allowed process-level globals (documented exceptions):

- `cfg` (process configuration)
- `E_Screen` (global display singleton handle)
- joystick handles and hardware scratch globals (`joysticks`, etc.)
- text/render process scratch state (`letters1`, `letters_big`, `text_buffer`)
- SAI2x color masks/line buffers
- intro palette globals (`pal`, `mypalette`)

Allowed thread-local exception:

- `grass_rng` (rendering scratch RNG; non-authoritative game state)

Accepted immutable registries:

- family/effect/treasure/generator/weapon registries (`s_registry` init-once statics)

---

## Key Data Structures

### Entity System

**`walker`** is the base class for all game entities. It extends `SimEntity` (SDL-free base providing position, size, identity, state, and animation frames) and holds an optional `WalkerRender` component for graphics. It provides movement, combat, AI, and lifecycle management.

```
walker
├── Order order     — entity type (FAMILY_SOLDIER, FAMILY_MAGE, ...)
├── statistics stats — HP, MP, attack, defense, speed, ...
├── guy* myguy      — link to persistent character data (for player characters)
├── obmap* myobmap  — spatial hash for collision queries
├── commands[]      — AI command queue
└── virtual methods: act(), animate(), collide(), walk(), fire(), ...
    (draw methods live in render layer: draw_walker(), draw_walker_tile())
```

**`guy`** is the persistent player character record, tracking name, family (class), stats, experience, and level. Stored in `save_data.team_list[]` as `unique_ptr<guy>`.

**`obmap`** is a 2D spatial hash grid. Entities register their positions; collision queries are O(1) for nearby entities.

### Game World

**`screen`** (extends `video`) is the game world container:

```
screen
├── level_data     — current level tiles, objects, and metadata
│   ├── grid[][]   — tile array
│   ├── oblist     — list<walker*> of all entities
│   └── fxlist     — list<walker*> of effects/FX layer
├── save_data      — current game progress, team roster, scores
│   ├── team_list[MAX_TEAM_SIZE] — array<unique_ptr<guy>, 24>
│   ├── current_campaign
│   └── completed_levels set
├── video_impl_    — platform renderer and logical canvas routing
├── viewscreen[4]  — split-screen viewports (1–4 players)
└── timer_wait     — frame rate control
```

The display backend keeps gameplay and fixed-coordinate UI in separate
logical spaces. At zoom 1.0, the world uses master's shipped 320x200 density
and expands only the needed axis to match the display aspect; lower zoom values
enlarge that canvas to show more of the level. Menus remain 320x200. Each
active canvas is aspect-fitted into the output viewport, so widescreen
displays add centered bars around the fixed UI instead of stretching it.
Window resize and completed fullscreen transitions rebuild the world canvas
and viewscreen layout. See
[Resolution, zoom, and smoothing](resolution-and-scaling.md) for the sizing,
resource limits, and legacy-config behavior.

### Session and Context

**`GameSession`** is the RAII root that owns all runtime state. Multiple sessions can coexist in a single process (see [Concurrent Sessions](#concurrent-sessions)).

```
GameSession
├── screen_owner_       — unique_ptr<screen>
├── session_surface_    — SDL_Surface* (per-session 320x200 render target; null if display owner)
├── ctx_                — GameContext (dependency injection)
│   ├── game_screen     — pointer to screen
│   ├── prefs           — unique_ptr<options>
│   ├── config          — cfg_store*
│   ├── rng             — IRandom* (injectable: ProductionRandom, SeededRandom, FixedRandom)
│   ├── input           — InputState (per-frame snapshot)
│   └── sim_events      — unique_ptr<SimEventLog> (event accumulator for sim/render decoupling)
├── production_rng_     — default RNG (used unless allocate_seeded_rng)
├── seeded_rng_         — unique_ptr<SeededRandom> (optional, per-session deterministic RNG)
└── session pointers    — `myscreen_` and `theprefs_` owned by SessionState,
    accessed via `og::runtime::current_session`
```

**`GameSession::Config`** controls session creation:

| Field | Default | Purpose |
|-------|---------|---------|
| `numviews` | 1 | Number of split-screen views |
| `allocate_screen` | true | Create a screen object |
| `create_display` | true | Own the SDL display (false = sub-session sharing a window) |
| `install_legacy_globals` | true | Install `myscreen`/`theprefs` globals |
| `allocate_seeded_rng` | false | Create a per-session deterministic RNG |
| `rng_seed` | 0 | Seed for the per-session RNG |

### Simulation Events and Event Log

The `og::sim` module defines typed events for decoupling game logic from rendering/audio. Entity code emits events during simulation ticks via `SimEventLog`; the runtime layer drains and dispatches them after each tick.

```cpp
enum class EventKind : uint32_t {
    None = 0,
    PlaySound = 4,     // Request sound: a=sound_id, b=0
    Notification = 8,  // Text notification: message in text field
    SetPalette = 11,   // Request palette change: a=0 normal, a=1 blue/freeze
    RequestRedraw = 12 // Force full screen redraw
};

struct Event {
    uint32_t tick;
    EventKind kind;
    uint32_t a, b;       // event-specific payload
    std::string text;    // optional text payload for Notification events
};
```

**Event flow:**
```
Entity code (walker::act, combat, specials, treasure pickup, ...)
  → og::sim::emit_sound(id)           // instead of myscreen->soundp->play_sound()
  → og::sim::emit_notification(msg)   // instead of viewob->set_display_text()
  → og::sim::emit_event(kind, a, b)   // generic structured event
  → SimEventLog accumulates events
       ↓
Runtime layer (after SimWorld::tick() returns)
  → drain SimEventLog
  → dispatch: play sounds, show notifications, apply palette/redraw requests
```

**`SimEventLog`** is owned by `GameContext` (`ctx().sim_events`), making it globally accessible to entity code without passing extra parameters through the call chain.

---

## Concurrent Sessions

The engine supports multiple `GameSession` instances coexisting in a single process. This enables the demo binary (12 AI-controlled games in a grid) and is the foundation for the networked multiplayer architecture (a host runs an authoritative server session alongside its own display session — see [Networking and Multiplayer](#networking-and-multiplayer)).

### SessionScope RAII Guard

`GameSession::SessionScope` is a nested RAII class that makes a session "active" by swapping thread-local globals:

```cpp
{
    auto scope = session.activate();  // saves current globals, installs session's globals
    game_frame(screen, state);        // all code sees this session's screen/prefs/ctx
}                                     // ~SessionScope restores previous globals
```

**On activation:**
- Saves current `thread_local current_session` and `current_game`
- Installs the session's globals
- If the session has a `session_surface_`, swaps `E_Screen->render` to point to it

**On destruction:**
- Restores the previous render surface (if swapped)
- Restores previous session globals

### Per-Session Render Surfaces

Sessions that don't own the display (`create_display=false`) allocate a private 320x200 32-bit `SDL_Surface`. When activated via `SessionScope`, `E_Screen->render` is redirected to this surface, so all rendering calls write to the session's private buffer instead of the shared display.

The display-owning "host" session renders directly to the display and doesn't allocate a separate surface.

### Demo Binary (`openglad_demo`)

Demonstrates concurrent sessions by running N independent AI-controlled games in a grid layout:

```
Main thread                          Worker threads (1 per session)
─────────                            ──────────────────────────────
Signal workers to tick        ──→    game_frame() (sim only, no render)
Wait for completion           ←──    Signal done
For each session:
  SessionScope activate
  Render to session_surface_
Composite all surfaces into grid
SDL_RenderPresent (once)
```

- Queries native display resolution to calculate grid size (e.g., 4×3 on 1280×600+)
- 1 host session (owns SDL window) + N sub-sessions (headless render targets)
- Each sub-session gets a unique random seed and random scenario assignment
- All sessions run in spectator mode (0-player, fully AI-controlled)

### Spectator Mode (0-Player)

Setting `save_data.numplayers = 0` enables spectator mode:

- Uses 1 viewscreen (camera only, no player control)
- Skips all player input processing (movement, fire, special, yell)
- Only `InputAction::SwitchChar` works (cycles camera target)
- All characters remain AI-controlled

In a network lobby, removing the machine's final owned seat through Base
Camp's **SPECTATE** action leaves that client connected without a gameplay
binding; **+** adds an active seat again. The old main-menu player-count
right-click no longer exists.

---

## Data-Driven Family System

Families are class-pack data. A pack's `families/*.lua` chunks call
`og.family(order, {...})`, which populates the five runtime registries and
binds that family's behavior hooks. The built-in classes use this same path
through `packs/core/`, so the engine has no separate compiled soldier, mage,
weapon, effect, or treasure implementation.

Descriptors retain the fields generic engine code needs: wire id, display
names, stats, art and animation references, presentation data, flags, and
per-family tuning. Simulation call sites dispatch behavior through
`og::script::hooks`; pack-installed descriptors carry no native behavior
callbacks.

**Adding a new family:** create a class pack containing a descriptor, any Lua
hooks it needs, and optional art. Pin an existing `wire_id` only when
intentionally replacing that slot; otherwise use deterministic automatic
assignment. Follow the schema and determinism rules in
[`lua-classpacks-design.md`](lua-classpacks-design.md) and the complete API in
[`modding/api-reference.md`](modding/api-reference.md).

**Key files:**
- `packs/core/families/`, `packs/core/lib/` — built-in pack declarations
  and the behavior modules they share
- `src/gameplay/script/family_decl.cpp` — `og.family` / `og.anims` /
  `og.pack`: the declaration pass that harvests data and the bind pass that
  installs hooks
- `src/resources/packs.cpp` — mount, memoize, and install class packs
- `include/openglad/gameplay/families/` — descriptor and registry APIs
- `include/openglad/gameplay/script/family_hooks.h`,
  `src/gameplay/script/world_scripts.cpp` — Lua registration and dispatch

---

## Game Loop

### Native Build Flow

```
main() [src/platform/sdl/glad.cpp]
  ├── io_init()                    Initialize PhysFS filesystem
  ├── cfg.load_settings()          Load openglad.yaml configuration
  ├── GameSession session(cfg)     RAII: create screen, prefs, install globals
  ├── init_input()                 Initialize keyboard/controller mappings
  ├── intro_main()                 Display splash screen
  ├── picker_main()                Enter team picker (blocking menu loop)
  │   ├── picker_mainmenu_loop()   Main menu: New Game / Continue / Load / Quit
  │   │   └── picker buttons → HireMenu, TrainMenu, ViewMenu, ...
  │   └── glad_main()              Start gameplay (called when player clicks GO)
  │       └── game loop (blocking)
  ├── text_shutdown()              Clean up text system
  └── ~GameSession()               Restore globals, free screen
```

### Per-Frame Game Loop

```
game_frame(screen& s, GameLoopFrameState& st)
  ├── SDL_PollEvent → screen::input()    Handle input events
  ├── screen::continuous_input()         Process held keys
  ├── local_transport_shadow_send_input()  Queue local InputState
  ├── local_transport_shadow_finish_tick() Run server tick + client mirror sync
  │   ├── GameWorld::tick()                Deterministic simulation
  │   │   ├── for each entity in oblist:
  │   │   │   └── walker::act()            AI, movement, combat, specials
  │   │   ├── dead entity cleanup
  │   │   ├── treasure/effect lifecycle
  │   │   └── check level completion
  │   ├── capture_snapshot()               Build authoritative state
  │   └── apply_snapshot()                 Update local client mirror
  │       └── emit events → SimEventLog
  ├── dispatch_sim_events()              Play sounds, show notifications
  └── screen::redraw()                   Render frame
      ├── Clear buffer
      ├── Draw tile grid
      ├── draw_walker() for each visible entity (render layer)
      ├── Score panel / HUD
      ├── Radar minimap
      └── Present to display
```

### Emscripten (Web) Build Flow

The web build cannot use blocking loops. Instead, a state machine drives the game from `requestAnimationFrame`:

```
main()
  ├── ... same init as native ...
  ├── picker_init()
  └── emscripten_set_main_loop(emscripten_frame_wrapper)

emscripten_frame_wrapper()    (called ~60 FPS by browser)
  ├── Accumulate delta time
  ├── Check if enough time for a game frame
  └── switch (g_game_state):
      ├── Picker:  picker_frame()  → if start requested → Playing
      ├── Playing: game_frame()    → if done → Picker
      └── Quit:    cancel main loop
```

---

## Networking and Multiplayer

OpenGlad multiplayer is **server-authoritative**: one peer (the host) runs the
deterministic simulation; every peer (including the host) renders a **mirror** of
the authoritative state that it receives as snapshots. Each machine may
contribute up to 4 local seats, with up to 16 seats lobby-wide. The same
machinery also runs single-player and local split-screen — see [Local Transport
Shadow](#local-transport-shadow).

The current network compatibility line is lobby/gameplay protocol **v12**, world
snapshot format **v10**, and replay format **v14**. Protocol v12 replaces the
flattened CTF snapshot block with the generic RespawnState + ModeState blocks
(every scripted mode replicates through them) and appends
`LobbySettings.shared_teams`; v9 added stable, server-issued lobby seat
identity and authoritative per-recipient ownership on top of the
Company/Base Camp multiplayer state introduced by v8. Team changes
and exact-seat removal use that identity rather than the mutable dense `P#`.
Peers reject incompatible versions during the handshake, while snapshot and
replay readers independently reject unsupported payload formats.

### Core components (`og_gameplay`, SDL-free)

| Component | File | Role |
|-----------|------|------|
| `GameServer` | `src/gameplay/game_server.cpp` | Owns the authoritative `GameWorld`. Applies player inputs, steps the sim, broadcasts snapshots + game-flow events, tracks connected clients and disconnects. |
| `GameClient` | `src/gameplay/game_client.cpp` | Mirror. Sends this peer's input, applies snapshots to its local mirror world, raises callbacks (initial setup, control mapping, sim/game-flow event batches, exit/pause prompts). |
| `WorldSnapshot` | `src/gameplay/world_snapshot.cpp` | Serializes world state. Full **keyframes** and **deltas** against a baseline; per-entity `GuySnapshot`s; deterministic hashing for desync detection. |
| `ITransport` | `src/gameplay/net_transport.cpp` | Typed message channel (snapshots, input, event batches, lobby messages, hello/heartbeat, exit/pause). Implementations below. |
| `LobbyServer` | `src/gameplay/lobby_server.cpp` | Pre-game lobby: roster, seat lifecycle and team assignment, settings (campaign/scenario/difficulty), host election, start handshake. |

**Transports** (`ITransport` implementations):

- `InProcessTransport` (`og_gameplay`) — in-memory loopback (local play + the
  host's own connection to its server)
- `MultiplexTransport` (`og_gameplay`) — fans one server out over several
  transports (e.g. local + WebSocket + relay) so the host serves loopback and
  remote peers together
- WebSocket client/server + relay (`og_platform_ws_transport`, native) and the
  Emscripten equivalents (`og_platform_emscripten_transport`, browser)

### Local Transport Shadow

`src/platform/sdl/local_transport_shadow.cpp` is the bridge that makes **every**
game — single-player, local split-screen, and networked — run through the
client-server architecture. During `glad_main`, each frame:

```
local_transport_shadow_send_input()   queue this peer's InputState
local_transport_shadow_finish_tick()
  ├── (host only) server_session.activate(); GameServer::step()
  │     └── GameWorld::tick() → capture snapshot → broadcast deltas + events
  └── display_session: drain client(s) → apply snapshot to the mirror world,
        dispatch sim/game-flow event batches → render
```

The host carries **two** `GameSession`s: an authoritative *server session* (the
real sim) and its own *display session* (a mirror, like any client). A pure
client carries only a display session and a `GameClient`. For local/single-player
play the server and the single client are both in-process (`InProcessTransport`).

### Lobby → gameplay → lobby flow

Base Camp coordinates a networked start. Its seat rail is a four-card view of
the authoritative lobby roster, with paging for larger lobbies and **+** for
adding a local seat. An owned card opens its machine-local control profile and
next-level team assignment; a foreign card is read-only. The interface keeps
stable seat tokens behind the dense display ordinals, so removing a middle
seat cannot retarget a command to one of its siblings.

`IPickerLobbyClient`
(`src/interface/ui/picker_lobby_client.cpp`,
`picker_lobby_network_client.cpp`) has three implementations:

- `LocalPickerLobbyClient` — single-player / local split-screen
- `HostPickerLobbyClient` — hosts a `LobbyServer` over a `MultiplexTransport`
- `JoinPickerLobbyClient` — connects to a remote host

```
Base Camp / picker_team_build (lobby)     each peer
  ├── poll lobby, show status lines
  ├── add/remove local seats and edit owned seats
  ├── host: request_start_game() ──┐
  └── join: wait for handoff       │   StartGame broadcast
                                   ▼
  install_gameplay_runtime() → reset_network_{host,client}_transport_shadow()
        creates the per-level GameServer / GameClient on the live transport
  glad_main()  ── play the level ──
  glad_main returns (world.end != 0) → back to Base Camp
  resume_after_level()  reuse the SAME connection for the next level
```

Between levels every peer returns to Base Camp (single-player parity).
The lobby connection **persists across `glad_main`** — `install_gameplay_runtime`
keeps the host/join client's transport refs and `LobbyServer` alive (dormant
during gameplay; the lobby is never polled inside the game loop), and
`resume_after_level()` re-syncs over the live socket rather than reconnecting.
Finishing a level (win, exit-portal, or withdraw) ends every peer's display
(terminal `EndGame`); the host advances the campaign cursor and the menu starts
the next level fresh.

### Per-player save isolation

A networked session must not let one player's active company gain or lose another
player's characters. The combined live roster is written to a transient
`"netsession"` slot; each player's private company slot (`save0` by default) only
ever receives **its own**
characters (matched by transient `guy::owner_player_index` / `owner_save_slot`
tags carried on the snapshot wire), advanced as if it had played solo — see
`SaveData::merge_owned_guys_from` and `persist_owned_characters_to_save0`.

---

## Campaigns, Levels, and Scenarios

### Structure

- **Campaign** — A collection of levels with a progression order. Shipped as `.glad` packages (zip archives). The seven built-in campaigns are committed as plain source trees under `campaigns/<id>/` (campaign.yaml, scen/, pix/, icon.png); the build composes each tree into `build/<preset>/builtin/<id>.glad` with `scripts/make_glad.py` — a deterministic writer (stored members, bytewise-sorted paths, fixed timestamps), so identical trees always produce byte-identical archives. Pack-bearing campaigns (modes, concept) keep their pack Lua single-sourced under `tools/<tool>/pack/`, mapped to `packs/<pack-id>/` in the archive at composition time. `tests/unit/test_builtin_archives.cpp` pins the staged archives against the source trees byte-for-byte.
- **Level** — A single scenario file defining a tile grid, entity placements, objectives, and intro text.
- **Scenario** — The in-game term for a level. Each has a numeric ID; the player progresses through them sequentially.

### Level Data Format

Levels are stored in a binary `.fss` format (versioned). The `level_data` class handles loading and saving:

```
level_data
├── grid[GRID_SIZE][GRID_SIZE]  — tile type array (grass, stone, water, ...)
├── oblist                       — entity spawn positions and types
├── title, description           — level metadata
├── par_value                    — target completion score
├── exits[]                      — connections to other levels
└── version                      — format version for migration
```

### Save Data

`save_data` stores the player's progress across levels:

```
save_data
├── team_list[24]           — array of unique_ptr<guy> (player characters)
├── current_campaign        — active campaign name
├── current_level           — next level to play
├── completed_levels        — set of finished level IDs
├── score, cash             — team resources
└── numplayers              — runtime-only 0–4 local seat/view projection
```

Save slots are numbered 0–9. Slot 0 is the auto-save. The game saves after each completed level.
The local player count (including spectator mode at 0), active seat tokens, and
seat-team assignments are session state. They are not restored from or
persisted in a company save. Although `numplayers` remains as an in-memory
compatibility projection in `SaveData`, GTL writers emit a fixed one-player
marker and current readers ignore the old field. Direction modes and keymaps
live in the shared configuration, outside company storage.

### Campaign Packages

Campaigns can be distributed as ZIP archives. The `zip_api` module handles creation and extraction. PhysFS mounts campaign directories as virtual filesystems, allowing the game to load assets from ZIP files transparently.

### Multiplayer Game Modes (scripted levels)

The five competitive modes (player guide: [docs/mp-game-modes.md](mp-game-modes.md)) are
level-driven Lua: a level whose type byte carries `GameWorld::TYPE_SCRIPTED` (0x20) hands its
match rules to the mounted campaign pack's per-level hook registrations
(`og.register_level_hooks`: `on_mode_init`/`on_mode_tick`/`on_damage`/`on_respawn`/
`on_entity_death`). `og::sim::mode_run_tick` (src/gameplay/mode/mode_tick.cpp) owns the fixed
frame: lazy activation (a scripted map whose init fails falls to classic rules the next tick),
the per-tick win-latch re-assert (`og.end_level`/`og.declare_winner` write the latch; the tick
entry zeroes the world's end fields), the respawn timers, and the `on_mode_tick` dispatch.
`og::sim::ModeState` (64 replicated int32 vars + a generic HUD channel: 4 text lines, 4 entity
beacons, a mode name) is the ONLY durable home for Lua mode state; clients render ModeState,
never mode names in C++.

The respawn engine (`src/gameplay/respawn/respawn.cpp`, state in
`respawn/respawn_state.h`) survived the CTF engine retirement: queue, timers, team anchor
arrays, RNG-free revive/placement paths, and the classic difficulty-submenu modes. Scripted
modes schedule through `og.respawn_schedule` (eligibility is Lua's) and reposition in
`on_respawn`; corpses persist in `oblist` so control bindings and per-player save merging
survive, and team wipes never end an undecided scripted match.

`RespawnState` and `ModeState` replicate as their own `WorldSnapshot` blocks (snapshot v10,
protocol v12, replay v14), so mirrors, late joiners, replays, and the curses/text HUDs need no
extra wire messages — a mid-join keyframe restore carries a running match. Match settings
(`ctf_team_count`/`ctf_capture_limit`/`ctf_respawn_ticks`/`ctf_strip_scenario_troops` — the
storage names keep their historical prefix; Lua reads them as `og.match_setting`) follow the
versioned SaveData (GTL v10) → LobbySettings → game start → `GameWorld::ctf_requested_*` path.
`LobbySettings.shared_teams` (v12) carries the versus/shared-teams rule on the wire (the host
derives it from the campaign's `matchup: versus` yaml key), and the lobby also carries the
loaded map's authored-team mask (start markers) so every peer agrees on sparse team domains.
With the type bit clear, every scripted path reduces to one false branch: zero extra RNG draws,
events, or entity changes (classic parity preserved).

The shipped modes campaign (28 scenarios: TDM 300-305, CTF 500-509, Onslaught 800-803,
Soccer 820-823, Mutant 840-843) is generated into `campaigns/modes/` by
`tools/modes_mapgen` via `scripts/generate_modes_campaign.sh`; the build composes that one
tree — including the hand-authored pack at `packs/modes.core/` (five mode
scripts, shared libs, flag/waypoint/ball families, sprites), which the generator never
rewrites — into the archive, so luals/lint/coverage see
exactly the shipped bytes. `scripts/gen_modes_sprites.py` paints the pack sprites.

---

## Custom Sprite Packs

Players can drop PNG overrides into `~/.openglad/extra_pix/<pack-name>/` and select the active pack from the Options menu. PhysFS mounts the chosen directory over `pix/` at startup and on selection change, so custom assets are picked up without a restart.

### Directory Convention

Packs are flat subdirectories of the user data `extra_pix/` folder (created alongside `campaigns/`, `save/`, and `cfg/` at startup):

```
~/.openglad/extra_pix/
    my-pack/
        soldier.png
        knife.png
        ...
```

Each subdirectory name is the pack identifier. Files shadow built-in `pix/` assets by exact filename match.

### How It Works

1. **Settings persistence** — Active pack stored in `openglad.yaml` under `graphics` / `sprite_sheet` (empty string = no override).
2. **Startup mount** — `apply_sprite_sheet_setting()` in `src/resources/io/platform_io_common.cpp` is called in `glad.cpp` after `cfg.load_settings()` and before `GameSession` construction. It mounts the chosen pack directory at the PhysFS `"pix/"` mount point with prepend priority.
3. **Hot-swap** — When the user changes the selection from the Options menu, `apply_sprite_sheet_setting()` unmounts the old pack and mounts the new one, then `loader::reload_graphics_if_stale()` reloads all sprite data. Reloading frees buffers that live render components still borrow (menu buttons hold `pixieN` facings pointers into loader memory, and the SCENARIO screen keeps a loaded world), so it is only safe from a handler that returns `MENU_REDRAW`: the menu frame skeleton then re-creates every button pixie (`reset_buttons` → `init_buttons`) and re-runs the level-reload guard **before** anything draws. It is *not* safe because "no live walkers exist while the options menu is open" — that was never true.

### Precedence vs. campaign art (issue #162)

Campaign packages (`.glad`) also PREPEND on mount and may ship their own `pix/` entity art, which would silently outrank the sheet after any mid-session campaign switch. The rule is: **an explicitly configured user sprite sheet wins `pix/` lookups for the exact filenames it ships; the campaign wins for everything else.** `mount_campaign_package_with_error()` enforces it by calling `reassert_sprite_sheet_mount()` (re-prepend of the sheet) after every successful campaign mount, so the outcome no longer depends on mount order. Campaign class-pack sprites (`packs/<id>/sprites/*.png`) resolve by full virtual path and can never be shadowed by a sheet.

### Campaign-switch reloads (issue #162)

Loaders record the `og::resources::sprite_source_generation()` they last rebuilt at; campaign mounts/unmounts and sprite-sheet (re)mounts bump the counter (the editor-save `remount_campaign_package_with_error()` deliberately does **not** — it restores the identical source set). Every campaign-switch flow calls `loader::reload_graphics_if_stale()` at a render-safe point (picker campaign handlers, the gameplay-entry net in `game.cpp`, editor entry/campaign-switch, replay bootstrap, the headless wire chokepoint in `platform_headless.cpp`, and the text/curses pickers); a same-campaign level advance is a generation-check no-op. The networked lobby-poll campaign sync deliberately never reloads mid-menu-frame — the gameplay-entry net covers it. Under `TESTING`, `create_walker_owned` traces when a walker is created from a stale loader so tests can pin that the enumerated flows never enter gameplay stale.

### Key Files

| File | Role |
|------|------|
| `src/resources/io/platform_io_common.cpp` | `apply_sprite_sheet_setting()` / `reassert_sprite_sheet_mount()` — PhysFS mount/unmount logic, campaign-mount precedence |
| `include/openglad/resources/gloader.h` | `loader::reload_graphics()` / `reload_graphics_if_stale()`, `og::resources::sprite_source_generation()` |
| `src/interface/ui/picker.cpp` | `pick_spritesheet()` submenu and Options menu button |

---

## Build System

### CMake (Primary)

The project uses CMake 3.25+ with preset-based configuration. The root
`CMakeLists.txt` holds the options, the shared helper functions, and the
component/executable/install rules; four modules under `cmake/` carry the bulk
and are `include()`d from it, so they run in the same scope:

| Module | Contents |
|--------|----------|
| `cmake/OpenGladSources.cmake` | The component source lists — the single place a new `.cpp` is registered |
| `cmake/OpenGladDependencies.cmake` | External packages, with FetchContent as the fallback |
| `cmake/OpenGladTests.cmake` | Test executables, groups, and CTest registrations |
| `cmake/OpenGladCoverage.cmake` | The combined C++ + Lua coverage gate and the API stub drift check |

`GAME_SOURCES_NO_MAIN` is derived from the component lists rather than
maintained beside them, so a file added to a component reaches every target
that needs it.

**Configure and build:**
```bash
cmake --preset ci-test        # Configure
cmake --build --preset ci-test # Build
ctest --preset ci-test         # Run tests
```

### CMake Presets

| Preset | Purpose |
|--------|---------|
| `dev-debug` | Development debug build (Ninja, tests enabled) |
| `dev-release` | Optimized build (RelWithDebInfo, tests off) |
| `ci-test` | CI standard build + all tests |
| `ci-asan` | ASan + UBSan sanitizer build |
| `ci-tsan` | ThreadSanitizer build |
| `ci-coverage` | Coverage-instrumented build (the coverage gate) |
| `ci-fuzz` | libFuzzer targets |
| `dev-debug-vcpkg` | With vcpkg toolchain |
| `dev-debug-conan` | With Conan toolchain |
| `web-emscripten` | Emscripten/WebAssembly build |

### CMake Targets

**Component libraries** (build/dependency enforcement level):
`og_core`, `og_gameplay`, `og_resources`, `og_interface`, `og_platform_sdl`

**External libraries:**
`og_ext_lodepng`, `og_ext_yaml`, `og_ext_zlib`, `og_ext_libzip`, `og_ext_physfs`, `og_ext_ixwebsocket`, `og_lua`

**Aggregate target:**
`og_game` — INTERFACE library linking all component libraries with `--start-group`/`--end-group` for cyclic resolution.

**Executables:**
- `openglad` — The game
- `openscen` — The level editor (same source as openglad, compiled with `-DOPENSCEN`)
- `openglad_demo` — Multi-session demo (N concurrent AI-controlled games in a grid)
- `openglad_text` — Headless text-mode client (no SDL)
- `openglad_curses` — Zero-SDL **ncurses** client: a roguelike terminal renderer +
  keyboard input backend over the same simulation, menus, save/level loading, and
  networking. See [docs/ncurses-client.md](ncurses-client.md).

**Test executables (run via `ctest --preset ci-test`):**
- `og_unit_*` (e.g. `og_unit_sim`, `og_unit_families`, `og_unit_entity`, `og_unit_data`) — headless unit binaries
- `og_test_*` (e.g. `og_test_walker_combat` … `og_test_mass_coverage`) — SDL integration group binaries
- `og_test_curses` — ncurses client unit + integration + networking tests (SDL-free, TTY-free)
- `openglad_text_sim` — Text client simulation tests
- `openglad_text_picker_interactive` — Text client picker tests
- `openglad_text_unsupported` — Text client unsupported-operation tests
- `emscripten_build_test` — WebAssembly build verification (skipped on native)

### Compiler Settings

- **C++ Standard:** C++20 (`CMAKE_CXX_STANDARD 20`)
- **C Standard:** C11 (for fetched/package dependency C libraries)
- **Warnings:** `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
  -Wshadow` (project code only). `-Wsign-conversion` is named explicitly
  because GCC's C++ `-Wconversion` does not imply it while Clang's does.
  The tree is warning-free under both, and `OPENGLAD_WARNINGS_AS_ERRORS`
  is ON in the `ci-test`, `ci-asan` and `ci-tsan` presets to keep it that
  way — a new warning is a build failure, not a log line.
- **Fetched compatibility code:** Compiled with `-w` (dependency warnings suppressed)
- **Sanitizers:** Optional ASan + UBSan via `ENABLE_SANITIZERS`

### SDL and Headless Build Targets

The project builds three executables from shared source with platform-specific implementations:

- **`openglad`** (SDL client) — Full graphical game with rendering, audio, and input via SDL3. SDL platform code lives in `src/platform/sdl/` and `src/interface/`.
- **`openglad_text`** (headless client) — SDL-free text-mode client for simulation, testing, and scripting. Headless platform code lives in `src/platform/text/`.
- **`openglad_server`** (headless host) — SDL-free dedicated server that hosts a networked game (runs the authoritative `GameServer` + `LobbyServer` with no display); `src/server/headless_server_runtime.cpp`.
- **`openglad_curses`** (ncurses client) — SDL-free *playable* terminal client. It links the same SDL-free components the dedicated server does, plus the shared menu model (`menu_model`/`picker_common`/`picker_state`) and a new ncurses front end (`src/platform/curses/`). It runs every game through the engine's own client-server architecture (`InProcessTransport` for local play, WebSocket for networked), rendering the mirror `GameWorld` as a roguelike (one character per tile, one character per "dude" on its nearest tile) and translating key presses into the engine's `InputState`. All of its logic sits behind an `ITerminal`/`IClock` seam so the whole client is tested headlessly with no TTY (`og_test_curses`).

The SDL, text, server, and curses targets link the same core components (`og_core`, `og_gameplay`, `og_resources`; SDL also links `og_interface` and `og_platform_sdl`; the server and curses clients additionally link `og_platform_ws_transport`). The SDL/headless boundary is enforced via link-time dispatch: shared code calls functions declared in `level_data_hooks.h` (e.g., `create_level_render`, `level_data_wire_entity_from_screen`), which have separate implementations in `sdl_context_services.cpp` (SDL) and `platform_headless.cpp` (headless).

**Key boundary files:**

| File | Purpose |
|------|---------|
| `src/interface/sdl_context_services.cpp` | SDL implementations: view control wiring, entity rendering hooks, level draw |
| `src/interface/walker_render_bridge.cpp` | SDL `walker` member functions: render component, destructor, frame management |
| `src/platform/text/platform_headless.cpp` | Headless stubs: filesystem init, no-op/warning render functions |
| `src/platform/text/walker_headless.cpp` | Headless `walker` member functions: no render component, sim-only frame tracking |
| `include/openglad/resources/level_data_hooks.h` | Shared declarations enforcing signature parity between SDL and headless |

**Render component pattern:** `walker` holds an optional `std::unique_ptr<WalkerRender> render_`. SDL builds create the component in `attach_render()`; headless builds leave it null. Entity code checks `if (render_)` before delegating to the render component. `LevelData` follows the same pattern with `std::unique_ptr<LevelRender> renderer_`.

### Legacy Build Scripts

Shell scripts in `scripts/` provide convenience wrappers:

| Script | Purpose |
|--------|---------|
| `build_native.sh` | Quick native build via CMake dev-release preset |
| `build_test.sh` | Build all grouped test binaries via CMake |
| `build_web.sh` | Emscripten/WASM build to `dist/` |
| `build_coverage.sh` | Coverage instrumentation with lcov report |

### Web Build

The Emscripten build compiles to WebAssembly with the SDL3 port:

```bash
source /path/to/emsdk/emsdk_env.sh
./scripts/build_web.sh
# Output: dist/play.html, play.js, play.wasm, play.data
cd dist && python3 -m http.server 8080
```

Key flags: `--use-port=sdl3`, `-sASYNCIFY`, `-sALLOW_MEMORY_GROWTH=1`, `-sINITIAL_MEMORY=67108864` (64MB).

---

## Test Structure

### Test Pyramid

```
┌─────────────────────────────────────────┐
│       SDL Integration Test Groups       │  Full game flows split across
│              (og_test_*)                │  many binaries, runs via CTest
├─────────────────────────────────────────┤
│         Text Client Tests               │  Headless simulation, picker
│     (openglad_text_*, 3 CTest entries)  │  No display required
├─────────────────────────────────────────┤
│           Headless Unit Tests           │  Pure logic, no SDL init
│              (og_unit_*)                │  GameSession RAII, sim determinism,
│                                         │  session isolation, spectator mode
└─────────────────────────────────────────┘
```

### Test Frameworks

All native test binaries use real GoogleTest (`<gtest/gtest.h>`).

**Integration tests** (`tests/integration/integration_main.cpp`):
- SDL-backed GoogleTest binaries grouped under `og_test_*`
- Standard `TEST(Suite, Name)` and `TEST_F(Fixture, Name)` cases with `ASSERT_*` / `EXPECT_*`
- Binary-local listing/filtering via `--gtest_list_tests`, `--gtest_filter`, and `--gtest_shuffle`
- Per-test world cleanup handled by a GoogleTest event listener after each case
- Trace system: `TRACE("category", "message")` for behavioral verification

**Unit tests** (`tests/unit/unit_main.cpp`):
- Headless GoogleTest binaries grouped under `og_unit_*`
- Shared headless `GameSession` plus fallback world/save/event context restored by a listener
- No SDL initialization; pure logic only

### UI/Menu Testing Pattern

Menu functions block in event loops. Tests use a separate thread to drive navigation:

```cpp
static int injector_thread(void* data) {
    wait_for_interactable("button_id", 5000);  // Wait for button to exist
    SDL_Delay(1500);                            // Wait for fadeblack animation
    interact("button_id");                      // Click by ID
    return 0;
}

TEST(MenuFlow, picker_main_unwinds) {
    SDL_Thread* t = SDL_CreateThread(injector_thread, "inj", nullptr);
    g_picker_max_mainmenu_calls = 1;  // Limit loop iterations
    picker_main(0, NULL);             // Blocks until menus unwind
    SDL_WaitThread(t, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
}
```

### CI Pipeline

The main GitHub Actions workflow (`.github/workflows/test.yml`) runs:

1. **test** — Build all test binaries and run `ctest --parallel`
2. **build** — Native release build (`openglad`, `openscen`)
3. **headless-server** — SDL-free `openglad_text` / `openglad_server` build and test
4. **asan** — ASan + UBSan build and test
5. **tsan** — ThreadSanitizer build and test

Alongside it: `coverage.yml` (the line/function coverage gate), `fuzz.yml`,
`nightly.yml` (nightly native builds + release, plus a backstop Cloudflare
Pages production deploy), and `wasm-e2e.yml` (Playwright WebAssembly
end-to-end tests + PR preview deploys, and the production deploy — relay
worker then Pages — on every push to master).

---

## Important Files and Entry Points

| File | Purpose |
|------|---------|
| `src/platform/sdl/glad.cpp` | **Entry point.** `main()`, Emscripten frame wrapper, game state machine |
| `src/platform/sdl/glad_gameplay.cpp` | `glad_main` / `glad_init`: per-level gameplay bring-up and teardown |
| `src/platform/sdl/game_session.cpp` | RAII root: creates screen, prefs, SessionScope, per-session surfaces |
| `src/platform/sdl/demo.cpp` | Multi-session demo: N concurrent AI games in a grid |
| `src/platform/game_context.cpp` | `GameContext` and `ctx()` global accessor |
| `src/platform/sdl/game_loop.cpp` | Per-frame loop: `game_frame()` and `game_frame_with_result()` |
| `src/interface/screen.cpp` | Game world wrapper: `tick_world()` drives `GameWorld::tick()`, `redraw()` renders + dispatches events |
| `src/gameplay/game_world.cpp` | `GameWorld::tick()` — the deterministic, server-authoritative simulation step |
| `src/gameplay/walker.cpp` | Base entity class — all game objects inherit from this |
| `src/gameplay/living.cpp` | AI behavior for enemies and NPCs |
| `src/gameplay/families/` | Family string-id / namespace resolution (the descriptors themselves come from class packs) |
| `packs/core/` | Built-in class pack: `families/*.lua` declarations, `lib/` behavior modules |
| `src/gameplay/sim_event_log.cpp` | Event accumulator: decouples sim from rendering/audio |
| `src/gameplay/game_server.cpp` | Authoritative `GameServer` (multiplayer + local runtime) |
| `src/gameplay/game_client.cpp` | `GameClient` mirror |
| `src/gameplay/lobby_server.cpp` | Pre-game `LobbyServer` |
| `src/gameplay/world_snapshot.cpp` | World state snapshot/delta serialization |
| `src/gameplay/net_transport.cpp` | `ITransport` interface + message serialization |
| `src/platform/sdl/local_transport_shadow.cpp` | Runs every game through the client-server runtime; lobby↔gameplay bridge |
| `src/interface/ui/picker_lobby_network_client.cpp` | Host / Join lobby clients (WebSocket + relay) |
| `src/interface/ui/picker.cpp` | Team selection UI — main menu loop |
| `src/interface/ui/level_editor.cpp` | Scenario editor (openscen binary) |
| `src/interface/render/graphlib.cpp` | SDL3 graphics layer — pixel buffer / draw primitives |
| `src/interface/render/view.cpp` | Viewport/camera system, split-screen rendering |
| `src/interface/render/walker_draw.cpp` | Entity draw methods (extracted from `walker.cpp`) |
| `src/resources/level_file_io.cpp` | Level file loading and saving |
| `src/resources/save_data.cpp` | Save game serialization |
| `src/interface/input/input.cpp` | Keyboard/controller event handling |
| `include/openglad/platform/game_session.h` | GameSession + SessionScope + Config definitions |
| `include/openglad/gameplay/families/family_descriptor.h` | `FamilyDescriptor` struct for data-driven classes |
| `include/openglad/gameplay/families/family_registry.h` | Family registry lookup API |
| `CMakeLists.txt` | Build entry point — options, helpers, component targets, install rules |
| `cmake/OpenGladSources.cmake` | Component source lists — where a new `.cpp` gets registered |
| `cmake/OpenGladTests.cmake` | Test binaries and CTest registrations |
| `CMakePresets.json` | Build presets for dev, CI, and web |
| `tests/integration/integration_main.cpp` | Integration test runner entry point |
| `tests/unit/unit_main.cpp` | Unit test runner entry point |
