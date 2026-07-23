# OpenGlad ncurses Client (`openglad_curses`)

A fully-featured, **zero-SDL** terminal client for OpenGlad. It is a *different
renderer and input backend* for the same game: it reuses the engine's
deterministic simulation, its data-driven menu model, its save/level/campaign
loading, and its server-authoritative networking. Only rendering (pixels →
characters) and input (SDL events → key presses) are new.

This document is the design spec and the contract that the implementation
follows.

---

## Goals & non-goals

**Goals**

- Zero SDL dependency. Links the same SDL-free components the headless server
  links (`og_core`, `og_gameplay`, `og_resources`, `og_platform_ws_transport`),
  plus ncurses. No `og_interface` / `og_platform_sdl`.
- Full menu flow, reusing the existing data-driven menu model
  (`IPickerClient` / `run_picker` / `menu_model` / `picker_common`): new game,
  continue, hire, train, view roster, save/load, campaign select, level select,
  options, help, difficulty, Base Camp, and **networking (host & join)**.
- Real-time gameplay rendered as a roguelike: each tile is a character, each
  "dude" (walker) is a character chosen to approximate its family, drawn on the
  **nearest tile to its actual pixel coordinates** (entities move smoothly in
  the sim at pixel granularity; we snap to `pixel / GRID_SIZE` for display).
- Networked multiplayer (host + join) over the same `GameServer`/`GameClient`/
  `LobbyServer`/WebSocket transports the SDL client and dedicated server use.
- Extremely well tested, maintaining the project's coverage standard, with all
  tests runnable headlessly in CI (no TTY required).

**Non-goals**

- Split-screen / multiple local players on one terminal (single local player).
- The standalone pixel **level editor** (`openscen`). That is a separate tool,
  not part of the game client; the headless clients already exclude it.
- Audio. Sound sim-events are surfaced as optional message-log lines, not played.
- Pixel-faithful menu art. Menus are rendered natively for the terminal from the
  same *menu model*, not by down-sampling the 320×200 framebuffer.

---

## Why this is feasible (the seams)

The engine is already split so that simulation, data, and networking are
SDL-free; SDL lives only in the renderer/input/menu *front end*. The ncurses
client plugs into the same seams the headless `openglad_text` and
`openglad_server` binaries already use.

| Concern | Engine seam we reuse | SDL-free? |
|---|---|---|
| Simulation step | `GameWorld::tick()` via `GameServer::step()` | yes (`og_gameplay`) |
| Client mirror | `GameClient` (applies snapshots into a `GameWorld*`) | yes (`og_gameplay`) |
| Transport (local) | `InProcessTransport` | yes (`og_gameplay`) |
| Transport (net) | `WebSocketServer/ClientTransport`, `RelayWebSocketTransport`, `MultiplexTransport` | yes (`og_platform_ws_transport`) |
| Lobby | `LobbyServer` + lobby messages | yes (`og_gameplay`) |
| Level load / team spawn / transitions | `og::server::*` in `headless_server_runtime.cpp` | yes |
| Menu state machine | `og::ui::run_picker(IPickerClient&)` | yes (`picker_state.cpp`) |
| Menu definitions / business logic | `menu_model.cpp`, `picker_common.cpp` (`HireSession`/`TrainSession`/team ops) | yes |
| Input contract | `InputState` / `PlayerInput` / `InputAction` | yes (`og_gameplay`) |
| Renderable state | `GameWorld::grid` (tiles) + `oblist`/`weaplist`/`fxlist` (entities) | yes |

**Build template:** `openglad_curses` is `openglad_server`'s SDL-free source
list (`headless_server_runtime` + `headless_tick_interval` + `platform_headless`
+ `walker_headless` + `input_state` + `game_context` + `session_state` +
`level_runtime_data` + `platform_bridge`) **plus** the reusable menu sources
(`menu_model`, `picker_common`, `picker_state`) **plus** the curses front end,
linking `og_core og_gameplay og_resources og_platform_ws_transport` + ncurses.

---

## One gameplay frame

The client always runs through the engine's client-server architecture (the same
as the SDL client), so local and networked share one code path. For a local
game both ends are in-process:

```
read terminal keys ──► CursesInput ──► InputState (player 0)
        │
        ▼
GameClient::send_input(InputState, server_tick+1)      // onto the transport
        │
        ▼  (host/local only)
GameServer::step()                                      // poll inputs, GameWorld::tick(),
        │                                               // broadcast snapshot + event batch
        ▼
GameClient::poll_messages()                             // apply snapshot into mirror GameWorld,
        │                                               // fire control-mapping / event-batch callbacks
        ▼
CursesRenderer::draw(mirror_world)                      // tiles + dudes + HUD + log
        │
        ▼
check mirror_world.end                                  // 0 = continue; else win/lose/transition
        │
        ▼
pace to next sim tick (IClock::sleep_until)
```

- **Local single-player:** one `GameServer` over an `InProcessTransport`, one
  `GameClient` bound to a mirror `GameWorld`. The server world is loaded and the
  team spawned with `og::server::load_headless_level_from_save`. Level
  transitions use `og::server::complete_headless_level_and_load_next`.
- **Host:** a `LobbyServer` over a `MultiplexTransport` (in-process loopback +
  `WebSocketServerTransport` [+ optional relay]); on start, a `GameServer` over
  the same combined transport, plus an in-process `GameClient` for the host's own
  display. Models `server_main.cpp`, not the SDL `local_transport_shadow`.
- **Join:** a `WebSocket`/relay client transport, `server_peer_id = 1`, a
  `GameClient` bound to a mirror world. No local server.

The renderer reads only the mirror `GameWorld`:
`world.grid` (one byte per tile; classify via `world.mysmoother.query_genre_x_y(tx,ty)`
→ `TYPE_*`) and `world.oblist` / `weaplist` / `fxlist` (each a
`unique_ptr<walker>` with `order()`, `family()`, `team_num()`, `user()`,
`xpos()`/`ypos()` in pixels; `GRID_SIZE == 16`). The followed avatar is the
walker whose id is in `GameClient::controlled_entity_ids()[0]`.

---

## Modules (all behind testable interfaces)

```
include/openglad/platform/curses/
  clock.h            IClock + SteadyClock + FakeClock
  terminal.h         ITerminal, Cell, Key (event type + mods), KeyCode, Color enums
  kitty_keys.h       Kitty keyboard protocol decoder (pure; byte stream -> Key events)
  curses_terminal.h  CursesTerminal : ITerminal      (real ncurses; not run in CI)
  headless_terminal.h HeadlessTerminal : ITerminal   (in-memory grid + scripted keys)
  glyph_map.h        Glyph (char + Color + bold); pure mapping functions
  curses_input.h     CursesInput : key events -> InputState via the config bindings
  curses_renderer.h  CursesRenderer : draw mirror GameWorld + HUD + message log
  curses_picker_client.h CursesPickerClient : og::ui::IPickerClient
  curses_game_runtime.h  GameRunResult run_curses_level(...) ; loop + pacing
  curses_network.h   host/join bring-up (transports + lobby + handoff)
  curses_app.h       CursesApp : top-level wiring (terminal, clock, options)
src/platform/curses/   one .cpp per header + main.cpp
tests/curses/          one test file per module (see Test plan)
```

Dependencies (everything flows toward leaves; no cycles):

```
main ─► curses_app ─► curses_picker_client ─► {menu_model, picker_common, picker_state}
                          │                 ─► curses_game_runtime ─► {curses_renderer, curses_input,
                          │                                            curses_network, headless_server_runtime,
                          │                                            GameServer/GameClient/transports}
                          └─► curses_network
curses_renderer ─► {glyph_map, terminal, GameWorld}
curses_input    ─► {terminal, InputState, session keybindings}
curses_terminal ─► {terminal, kitty_keys}
glyph_map, terminal, kitty_keys, clock : leaves
```

### `ITerminal`

A minimal terminal surface so all rendering/menu/input logic is testable without
a TTY:

```cpp
struct Cell { char32_t ch; Color fg; Color bg; bool bold; };

class ITerminal {
public:
  virtual ~ITerminal() = default;
  virtual int rows() const = 0;
  virtual int cols() const = 0;
  virtual void clear() = 0;
  virtual void put(int row, int col, char32_t ch, Color fg, Color bg, bool bold) = 0;
  virtual void put_str(int row, int col, std::string_view s, Color fg, Color bg, bool bold) = 0;
  virtual void present() = 0;                 // flush back buffer to screen
  // Input. block==false returns Key::None when nothing is ready.
  virtual Key poll_key(bool block) = 0;
  virtual void set_cursor_visible(bool) = 0;
  virtual void beep() = 0;
};
```

`CursesTerminal` uses ncurses **only for rendering** (`initscr`/`init_pair`/
`mvaddstr`/`refresh`, `setlocale` for wide glyphs, ASCII/monochrome fallback). For
**input** it does not use `getch()` at all: at startup it runs the Kitty keyboard
protocol capability handshake (`CSI ? u` + a Primary-Device-Attributes probe), and
if the terminal does not advertise support it **throws and the client aborts** —
there is no legacy fallback. On success it enables the protocol (key-up/down +
real modifiers + standalone Ctrl/Alt) plus focus reporting, then reads raw bytes
from the tty and feeds them to a pure `kitty::Decoder` (see `kitty_keys.h`).
`SIGWINCH` yields a `Resize` key; `SIGINT`/`SIGTERM`/`atexit` restore the keyboard
mode and leave curses so the protocol is never left enabled in the user's shell.
`HeadlessTerminal` stores a `rows*cols` vector of `Cell`, a scripted
`std::deque<Key>` for input (now including release/modifier events), and exposes
accessors (`cell_at`, `text_row`, `find_char`) for assertions.

### `Glyph` mapping (the character table)

Pure functions in `glyph_map.{h,cpp}`. Colors are an abstract `Color` enum mapped
to terminal colors by the terminal backend. Tiles are dim/background; entities
are bright/foreground and drawn on top, so glyphs only need to be unique *within
a layer*. Team determines entity color (`team_num`); the followed avatar is drawn
as `@` and bold.

Living families (`Order::Living`, id → glyph):

| id | family | glyph | id | family | glyph |
|----|--------|-------|----|--------|-------|
| 0 | SOLDIER | `S` | 11 | THIEF | `t` |
| 1 | ELF | `e` | 12 | GHOST | `g` |
| 2 | ARCHER | `a` | 13 | DRUID | `d` |
| 3 | MAGE | `m` | 14 | ORC | `o` |
| 4 | SKELETON | `k` | 15 | BIG_ORC (captain) | `O` |
| 5 | CLERIC | `c` | 16 | BARBARIAN | `B` |
| 6 | FIREELEMENTAL | `E` | 17 | ARCHMAGE | `M` |
| 7 | FAERIE | `f` | 18 | GOLEM | `G` |
| 8 | SLIME (big) | `J` | 19 | GIANT_SKELETON | `K` |
| 9 | SMALL_SLIME | `i` | 20 | TOWER1 | `Y` |
| 10 | MEDIUM_SLIME | `j` | | | |

Other orders:

- `Order::Treasure`: gold/silver bars → `$`; drumstick (food) → `%`; any potion →
  `!`; key → `[`; life gem → `+`; exit → `>`; teleporter → `<`; stain → (skip,
  floor).
- `Order::Weapon`: **every** weapon family has a visible glyph (nothing you throw
  is invisible) — knife/arrow/bone → `'`; rock/boulder/hammer/fireball/meteor →
  `*`; lightning → `↯`; tree → `♣`; blob → `o`; wave → `≈`; circle-protection →
  `○`; door → `+`; default → `*`.
- `Order::Generator`: tent → `^`; tower → `T`; bones → `n`; treehouse → `A`.
- `Order::FX` (effects — the visible part of most specials, in `fxlist`): **every**
  effect family has a visible glyph and `fxlist` is drawn — boomerang → `%`; magic
  shield → `○`; explosion → `✺`; bomb → `◍`; ghost-scare → `!`; cloud → `☁`; etc.
  (The lasting bloodstain is `FAMILY_STAIN`, a treasure, which stays blank so it
  doesn't litter the floor.) Draw order is fxlist → oblist → weaplist, so the
  followed `@` is never hidden by an effect and a shot in flight sits on top.

Tiles (`TYPE_*` → glyph; Unicode with ASCII fallback in parentheses):

| genre | glyph | genre | glyph |
|-------|-------|-------|-------|
| TYPE_GRASS | `.` | TYPE_WALL | `█` (`#`) |
| TYPE_GRASS_DARK | `,` | TYPE_CARPET | `=` |
| TYPE_GRASS_LIGHT | `` ` `` | TYPE_WATER | `≈` (`~`) |
| TYPE_DIRT | `:` | TYPE_TREES | `♣` (`&`) |
| TYPE_DIRT_DARK | `;` | TYPE_UNKNOWN | (space) |
| TYPE_COBBLE | `▒` (`%`) | | |

Decor (tile layering, `decor_glyph(id)` — the per-floor decor plane drawn over
the base tile): decor **wins** when it defines a glyph — torches → `!` bold
yellow; brazier → `☼` (`o`) bold red; boulders → `o` white; columns → `|`
white; shrub → `"` bold green (marsh reeds are the same shape non-bold).
Ground litter (pebbles, bones) returns `nullopt` and inherits the base tile
glyph, as do `DECOR_NONE` and out-of-registry bytes. Legacy combined
torch/boulder tiles were genre-`TYPE_UNKNOWN` blanks, so the override is a
strict information improvement.

Team → color (`team_num`): 0 red, 1 green, 2 blue, 3 yellow, 4 magenta, 5 cyan,
6 white, 7 bright-black. `world.my_team` is the player's side; the followed
avatar is `@` bold in the player color.

### `CursesInput`

Input is **exact**, not synthesized. Because `CursesTerminal` runs the Kitty
keyboard protocol, every key event carries a transition type — **press**,
**repeat**, or **release** — so `CursesInput` keeps a precise held set: a Press
marks the bound `InputAction` held (and pressed this frame), a Release clears it.
There is no decay window, no clock, no guessing. `move_x/move_y` derive from the
held directions exactly as the engine expects. A focus-out event clears all held
keys so nothing sticks down when the window loses focus.

**Bindings come from the game's config, not a hardcoded map.** A terminal key is
converted to an SDL keycode (`CursesInput::keycode_for_key`) and then matched
against the player's bound keys in `current_session->player_keys_[0]` — the same
table the SDL client builds. The app populates it the same way the SDL client
does, via `load_player_control_settings_from_cfg(cfg)` (which reads
`controls.player1_mode{4,8}_key{0..15}` and `player1_mode`). Rebinding a key in the
config — or in the SDL keybind screen — changes the curses controls too. Nothing
here decides on its own that a given key moves, fires, or quits.

- The configured **4/8-direction mode is honored** (no longer forced): in
  8-direction mode the default player-1 cluster is the classic `W E D C X Z A Q`
  ring (Up/UpRight/Right/DownRight/Down/DownLeft/Left/UpLeft) — `q` is up-left, `x`
  is down — and two cardinals held at once also move diagonally; in 4-direction
  mode only the four cardinals (`W A S D` by default) are bound. Real key-up/down
  is what makes both modes work — holding two keys is reliable now.
- All other actions resolve through the same table, and **Ctrl/Alt now work**:
  the protocol reports standalone modifier keys, so the default `Fire = LeftCtrl`
  and `Special = LeftAlt` are delivered as their own press/release events (no need
  to rebind them to printable keys).
- Meta keys are fixed and are *not* player bindings: `Esc`
  (`MetaAction::OpenGameMenu`) opens the in-game menu / withdraws on its **press**
  event. Everything else the level loop reads is fed straight through the bindings.

Menu navigation keys (in `CursesPickerClient`) are separate TUI navigation, not
gameplay bindings: up/down or `j/k` to move, `enter`/`space` to select,
`Esc`/`q`/`backspace` to go back, digit keys to jump.

### `CursesRenderer`

Draws three regions to an `ITerminal`:

1. **Viewport** centered on the followed avatar: for each visible cell, draw the
   tile glyph; then overlay entities (`oblist` then `weaplist`) at
   `(xpos/16 - cam_x, ypos/16 - cam_y)`, skipping dead/invisible and out-of-range.
   The followed avatar is `@` bold.
2. **HUD** (side or bottom bar): followed character name + family, HP/MP bars,
   level, team score/gold, level title, remaining-foes/objective hint.
3. **Message log**: last N lines, fed from sim/game-flow event batches
   (notifications; optionally sound-event labels).

Pure given an `ITerminal` + a `const GameWorld&` + the followed entity id, so it
is fully assertable against a `HeadlessTerminal`.

### `CursesPickerClient`

Implements `og::ui::IPickerClient` and is driven by the shared
`og::ui::run_picker`. Mirrors `TextPickerClient` but renders rich TUI menus via
`ITerminal` and reads keys from the terminal. Reuses `picker_menu_definition`,
`HireSession`, `TrainSession`, `reset_for_new_game`, `initialize_starting_team`,
campaign listing, save/load. `run_game()` calls `run_curses_level(...)` (local)
or the networked runtime. `configure_networking()` / `host_game()` /
`join_game()` open the networking submenu and build a host/join lobby via
`curses_network`.

### `CursesGameRuntime` + `CursesNetwork`

`run_curses_level` owns one level's loop for the local/host case: build the
server world (`load_headless_level_from_save`), `InProcessTransport`,
`GameServer`, in-process `GameClient` bound to a mirror world; wire client
callbacks (control mapping → followed id; event batches → message log + end latch;
**exit-prompt → latched** for the loop to resolve, NOT auto-accepted). When the
server asks before leaving the level, the loop shows a `y/n` prompt at the top of
the frame (before reading movement, since the mission is frozen) and answers via
`respond_to_exit_prompt` — so the player can **decline** and keep playing. Then
loop send-input → step → poll-messages → render → pace until `mirror_world.end`.
Returns a
`GameRunResult { ending, next_level, withdrew }`. The picker advances the
campaign with `complete_headless_level_and_load_next` / `withdraw_headless_level`
between levels (single-player parity).

`curses_network` provides the host and join bring-up (transport construction
lifted from the SDL-free portions of the lobby clients, handoff modeled on
`server_main.cpp`) and the networked variant of the level loop (host runs the
`GameServer`; join only polls its `GameClient`).

The lobby roster uses the same lobby-wide P# display ordinals as SDL Base Camp.
`<`/`>` (or the arrow keys) moves between seats owned by this terminal and `t`
cycles the selected seat's team; remote seats are read-only. One ncurses
process currently advertises one local seat (split-screen remains a non-goal),
while lobbies may contain more than four seats across network clients. The
terminal Main menu no longer carries the old 1–4 player-count rows. SDL Base
Camp's **+** and remove-seat lifecycle is unavailable in ncurses, so additional
players join from separate clients.

---

## Test plan (CI-safe, no TTY)

All tests use `HeadlessTerminal` + `FakeClock`, so they run headlessly (no TTY) —
the tty-specific bits of `CursesTerminal` (raw I/O, signals) are the only untested
code; the parsing they depend on is covered by the `kitty_keys` decoder tests.
Every case lives in a single CTest binary, **`og_test_curses`** (unit + component
+ integration + networking, ~210 cases), plus the **`openglad_curses_link_no_sdl`**
CTest that asserts the shipped binary has zero SDL symbols.

1. **glyph_map** — every living family id → expected glyph; treasure/weapon/
   generator subtypes; **every weapon family AND every effect family is visible**
   (not skip/blank — nothing thrown or cast is invisible, boomerang → `%`); all 11
   tile genres; team → color; followed-avatar override.
2. **kitty_keys** — the protocol decoder: printable/named/modifier/function keys;
   press/repeat/release event types; modifier bitfields; arrows (letter form) and
   `~`/SS3 functional keys; focus events; partial reads that wait for the rest;
   capability responses skipped (not surfaced as keys); and the
   support-detection handshake (kitty reply vs DA-only).
3. **curses_input** — keys resolve through the player's bound keys, never a
   hardcoded map: with the default 8-direction cluster `q`→up-left and `x`→down
   (the regression the user hit), rebinding a key (explicit table or **loaded from
   a `cfg_store`**) changes which terminal key fires which action, the configured
   **4/8-direction mode is honored**, and `Esc` is the only meta key. Plus exact
   **press/release** held tracking (held until release, no decay), **Ctrl/Alt via
   standalone modifier keys**, hold-fire-while-moving, and focus-out clearing held.
4. **curses_renderer** — build a small `GameWorld` (hand-placed grid + a few
   walkers), render, assert glyphs/colors at expected cells; pixel→tile placement
   (`/16`), nearest-tile rounding, and viewport centering/clipping at map edges; an
   **off-grid border** (not grass) marks the arena edge; HUD shows the character
   **name and current special**; an **effect in `fxlist` renders** (boomerang `%`)
   and an exit renders `>`; message log accumulates notifications; dead/invisible
   skipped.
5. **curses_picker_client** — script keys through `run_picker`: new game populates
   team; hire adds a member (gold deducted); train raises a stat (cost); view
   roster; set level/campaign; save then load round-trips; difficulty + player
   count cycle; quit unwinds. Assertions on `SaveData`.
6. **curses_game_runtime (local)** — load real level 1, run the loop headlessly;
   assert the mirror world is populated (team + foes), the followed avatar
   resolves, movement input moves the avatar, `Esc` withdraws while a non-meta key
   like `q` does **not**, and the **exit prompt is asked, not auto-accepted** —
   `n` declines (stay), `y` accepts. A **server-forwarded `EndGame` event durably
   ends the session** (the latched end survives later delta snapshots that
   re-serialize the server world's `end=0`), and `advance_save_after_win`
   advances the campaign + persists XP/gold on a win while a loss/withdraw leaves
   the roster untouched.
7. **curses_network** — host + join over an in-process transport: both mirror
   worlds converge by entity id + position; lobby roster sync; host input
   propagates to the joiner's mirror; cancel tears down cleanly.
8. **sdl-free guard** — `openglad_curses_link_no_sdl` runs `nm`/`ldd` on the
   shipped binary and fails on any SDL symbol or `libSDL2` dependency (mirrors
   `openglad_server_link_no_sdl`).

### Known limitations

- **Requires a Kitty-keyboard-protocol terminal.** This is what buys real
  key-up/down, modifiers, and standalone Ctrl/Alt. There is no legacy fallback: on
  a terminal without it, the client prints a message and exits. Supported by kitty,
  foot, WezTerm, Ghostty, and recent Alacritty/Konsole/iTerm2; VTE-based terminals
  (gnome-terminal) need a recent enough version.
- **Between-level flow is single-level-per-start.** Single-player returns to the
  team-build menu after each level (campaign progress persisted to the save);
  networked play returns every peer to the lobby after each level and does not
  yet persist per-player campaign progress across networked levels.
- **Networked exit prompts auto-accept.** The declinable y/n exit prompt is
  wired for the local single-player session; the networked host/join sessions
  still auto-consent (a per-peer prompt over the wire is future work).
- **Time bonus.** `advance_save_after_win` awards score×2 gold but omits the
  per-level time bonus the SDL/text clients add, so curses gold can be slightly
  lower on a fresh completion.
- The standalone pixel **level editor** (`openscen`) is out of scope.

---

## Usage

```
openglad_curses [--campaign <id>] [--level <n>] [--host [--port <p>]] \
                [--join <url>] [--no-unicode] [--no-color]
```

No arguments → splash → main menu (single-player). `--host` opens the networking
host lobby; `--join <url>` joins. The client requires an interactive terminal that
speaks the **Kitty keyboard protocol** (it aborts with a message otherwise); all
automated testing uses `HeadlessTerminal`.
