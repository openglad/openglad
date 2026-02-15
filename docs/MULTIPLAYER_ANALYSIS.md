# Network Multiplayer Analysis for OpenGlad

This document analyzes the OpenGlad codebase to evaluate approaches for adding
network multiplayer support. It covers the current architecture (as relevant to
multiplayer), evaluates three candidate networking models, recommends an
approach, and outlines an implementation plan.

---

## Table of Contents

- [1. Current Architecture Overview](#1-current-architecture-overview)
  - [1.1 Game Loop](#11-game-loop)
  - [1.2 Local Multiplayer](#12-local-multiplayer)
  - [1.3 Input System](#13-input-system)
  - [1.4 Game State](#14-game-state)
  - [1.5 Random Number Generation](#15-random-number-generation)
  - [1.6 Simulation Layer](#16-simulation-layer)
  - [1.7 Serialization](#17-serialization)
- [2. Networking Model Evaluation](#2-networking-model-evaluation)
  - [2.1 Option A: Client-Server with State Replication](#21-option-a-client-server-with-state-replication)
  - [2.2 Option B: Deterministic Lockstep](#22-option-b-deterministic-lockstep)
  - [2.3 Option C: Client-Server with Input Forwarding (Hybrid)](#23-option-c-client-server-with-input-forwarding-hybrid)
- [3. Recommendation](#3-recommendation)
- [4. Key Technical Challenges](#4-key-technical-challenges)
- [5. Implementation Plan](#5-implementation-plan)
- [6. Appendix: Key File Reference](#6-appendix-key-file-reference)

---

## 1. Current Architecture Overview

### 1.1 Game Loop

The game loop lives in `game_frame_with_result()` (`src/runtime/game_loop.cpp`)
and runs a simple sequential pipeline each frame:

```
game_frame_with_result()
  1. SDL_PollEvent → screen::input(event)    // dispatch per-viewscreen
  2. screen::act()                            // game logic tick
  3. screen::redraw()                         // render
  4. ctx().poll_input()                       // snapshot InputState
  5. screen::continuous_input()               // apply held keys → entity commands
  6. FPS cap via time_delay(timer_wait)
```

**Key observation:** The loop is **not fixed-timestep**. Frame pacing is
controlled by `screen::timer_wait` (default 6, roughly ~60 FPS target), but
`screen::act()` advances one "tick" per call regardless of wall-clock delta.
There is no accumulator or interpolation — each frame is exactly one simulation
step. This means the simulation speed is tied to the frame rate. On the native
build, `time_delay()` throttles to the target; on Emscripten,
`requestAnimationFrame` drives it at display refresh rate with an accumulator
gate (`emscripten_frame_wrapper` in `glad.cpp`).

### 1.2 Local Multiplayer

OpenGlad supports 1–4 player split-screen. The system is structured as:

- **`screen::numviews`** (1–4): number of active viewports.
- **`screen::viewob[4]`**: array of `unique_ptr<viewscreen>`, one per player.
  Split-screen layout is configured in `screen::initialize_views()`
  (`src/runtime/screen.cpp:220`).
- **`viewscreen::control`**: pointer to the `walker` entity that the player
  directly controls. Each viewscreen has `mynum` (0–3) which doubles as
  the player index.
- **`walker::user`**: signed char (-1 = AI, 0–3 = human player index).
  Player-controlled entities have `act_type = ACT_CONTROL`, which makes
  `walker::act()` / `living::act()` return immediately (the entity is driven
  by `continuous_input()` instead of AI).
- **`walker::team_num`**: 0–7 team assignment. Players are on team 0 by
  default; enemies on other teams.

Player count is stored in `save_data.numplayers` and persists across saves.
The picker UI sets this during team creation (`src/ui/picker.cpp`). On
platforms with `USE_TOUCH_INPUT` defined, multiplayer is disabled entirely
(`DISABLE_MULTIPLAYER` guard in `save_data.cpp:45-46`).

### 1.3 Input System

The input system has two layers:

**Low-level (SDL):** `src/input/input.cpp` provides per-player key queries:
- `isPlayerHoldingKey(player_index, key_enum)` — continuous state polling
- `didPlayerPressKey(player_index, key_enum, event)` — event-based press
- `player_keys[4][16]` — key binding arrays (keyboard)
- `player_joy[4]` — joystick binding objects

**High-level (GameContext):** `include/openglad/runtime/game_context.h` defines:
```cpp
struct PlayerInput {
    bool held[16];     // continuous key state
    bool pressed[16];  // one-shot presses this frame
    int move_x() const;  // derived -1/0/+1
    int move_y() const;
};
struct InputState {
    PlayerInput players[4];
    bool quit_requested;
};
```
`input_state_from_sdl()` populates this from SDL state each frame. The
`InputState` struct is compact, well-defined, and already player-indexed —
making it an excellent candidate for network serialization.

**Input flow per frame:**
1. `SDL_PollEvent` → `viewscreen::input(event)` handles discrete actions
   (yell, switch character, fire on press, shifter toggle).
2. `viewscreen::continuous_input()` reads held keys via
   `isPlayerHoldingKey(mynum, ...)` and translates to entity commands:
   walk directions, fire, special abilities.
3. Commands flow into `walker::walkstep()`, `walker::init_fire()`,
   `walker::special()`, etc.

### 1.4 Game State

The full game state is large and distributed across several structures:

**Per-entity (walker and subclasses):**
- Position: `worldx_`, `worldy_` (float), `xpos`, `ypos` (short, in pixie base)
- Movement: `stepsize`, `lastx`, `lasty`, `curdir`, `enddir`
- Combat: `damage`, `fire_frequency`, `busy`, `weapons_left`
- Stats: `statistics` object (HP, MP, attack, defense, speed, level, commands)
- Status effects: `invulnerable_left`, `invisibility_left`, `flight_left`,
  `speed_bonus_left`, `charm_left_`, `frozen_delay`
- AI: `act_type`, `foe`, `leader`, `owner`, `action`
- Animation: `ani_type`, `cycle`, `drawcycle`
- Identity: `order`, `family`, `team_num`, `user`, `dead`

**Per-level (`LevelData`):**
- `oblist` — `std::list<unique_ptr<walker>>` of all living/generator entities
- `weaplist` — weapons/projectiles in flight
- `fxlist` — effects and background objects
- `dead_list` — recently killed entities (deferred cleanup)
- `grid` — tile map
- `myobmap` — spatial hash for collision

**Per-game (`SaveData`):**
- `team_list[24]` — persistent player characters (`guy` objects)
- `numplayers`, `current_campaign`, `current_level`
- `score`, `cash`, `m_score[4]`, `m_totalcash[4]`

**Entity count:** The `MAXOBS` constant is 150 for generators, but entity
lists are unbounded `std::list`. A typical level might have 20–80 entities
active. Each entity has ~50+ mutable fields.

### 1.5 Random Number Generation

**Current production RNG:** The global `random(Uint32 x)` function
(`src/runtime/screen.cpp:102`) wraps C stdlib `rand()`:
```cpp
Uint32 random(Uint32 x) {
    if (x < 1) return 0;
    return static_cast<Uint32>(rand()) % x;
}
```
This is **not deterministic across platforms** — `rand()` behavior is
implementation-defined. There is no explicit seeding at game start in the
production path.

**RNG usage is pervasive:** `rng(x)` (a macro/inline dispatching to
`random()`) is called throughout entity AI (`living::act()`,
`walker::act_random()`), combat (`walker::attack()`), special abilities,
movement randomization, and generator spawn logic. A rough grep shows 200+
call sites.

**Modernized RNG infrastructure:** The `GameContext` provides an `IRandom*`
interface with three implementations:
- `ProductionRandom` — wraps `random()` (non-deterministic)
- `SeededRandom` — LCG with explicit seed (deterministic)
- `FixedRandom` — returns constant (for tests)

The `og::sim::Simulator` has its own internal LCG (`rng_state_`). However,
the entity code still calls `rng()` (the global), not the context's `IRandom`.

**Bottom line:** The game is **not currently deterministic**. Achieving
determinism would require routing all ~200+ `rng()` call sites through a
single seeded RNG, and auditing for other non-determinism (float ordering,
uninitialized state, pointer-address-dependent iteration, etc.).

### 1.6 Simulation Layer

The `og::sim::Simulator` (`src/sim/simulator.cpp`) is explicitly documented as
a **skeleton** for future migration:

> "This is the foundational abstraction for separating game logic from
> SDL/rendering. Currently a skeleton; real game logic in screen::act()
> will be migrated here incrementally."

Currently it runs as a "shadow sim" alongside the real game loop
(`game_loop.cpp:137-150`), consuming `InputSnapshot` and producing events
that are immediately discarded. The real game logic remains in:
- `screen::act()` — iterates entity lists, calls `walker::act()`
- `viewscreen::continuous_input()` — translates player keys to entity actions
- Entity methods: `walker::walk()`, `walker::fire()`, `walker::attack()`,
  `living::act()`, etc.

**Simulation/rendering separation:** Poor. The `screen` class extends `video`
(the graphics layer). Entities (`walker`) extend `pixieN` (animated sprite).
Game logic and rendering state are interleaved in the same objects. The `act()`
methods directly mutate positions and stats that `draw()` methods read.

### 1.7 Serialization

**Save files (.gtl):** Binary format (version 9) with fixed-layout fields.
Serializes `guy` records (persistent character data), campaign progress,
scores, and team composition. Does NOT serialize in-game entity state
(positions, HP, active projectiles, etc.).

**Level files (.fss):** Binary format (versions 2–9) storing tile grid,
entity spawn positions, scenario metadata. Read-only during gameplay.

**No general-purpose state serialization:** There is no mechanism to
serialize the full live game state (entity lists with all their mutable
fields) to a byte stream. This would need to be built for any networking
approach that involves state replication.

---

## 2. Networking Model Evaluation

### 2.1 Option A: Client-Server with State Replication

**Model:** One machine runs the authoritative game simulation (the "server").
Clients send their inputs to the server. The server runs `screen::act()` and
periodically sends full or delta state snapshots back to clients for rendering.

**Pros:**
- Cheat-resistant: server is authoritative; clients cannot manipulate state.
- Tolerant of non-determinism: only the server runs the simulation, so
  platform-specific `rand()` differences don't matter.
- Clients can join/leave or reconnect by receiving a fresh state snapshot.
- Supports spectators naturally.

**Cons:**
- **Massive engineering effort for state serialization.** Every mutable field
  on every entity type (`walker`, `living`, `weap`, `treasure`, `effect`,
  `statistics`, `guy`, `obmap` spatial hash, command queues, etc.) must be
  serializable. The entity hierarchy has 50+ mutable fields per walker and
  complex pointer-based relationships (`foe`, `leader`, `owner`, `collide_ob`).
  These raw pointers would need to be converted to network-safe IDs.
- **High bandwidth.** With 20–80 entities, each having ~200+ bytes of mutable
  state, full snapshots could be 4–16 KB per frame at 60 FPS = 240–960 KB/s.
  Delta compression would reduce this but adds significant complexity.
- **Requires deep refactoring.** The `screen` class mixes simulation and
  rendering; the server would need to run `screen::act()` without any SDL/video
  initialization. Currently `screen` extends `video`, making headless operation
  impossible without refactoring.
- **Latency.** Clients see state delayed by network RTT. Input response feels
  sluggish without client-side prediction, which requires partial simulation
  on the client — adding even more complexity.

**Estimated effort:** Very high (months). Requires state serialization
framework, entity ID system, pointer-to-ID translation, delta compression,
client-side prediction (optional but important for feel), and headless server
mode requiring significant refactoring of the screen/video inheritance.

### 2.2 Option B: Deterministic Lockstep

**Model:** All peers run identical simulations. Each frame, peers exchange
their `InputState` (or `InputSnapshot`). Once all inputs for frame N are
received, every peer advances the simulation identically. With matching seeds
and identical inputs, all peers produce the same game state.

**Pros:**
- **Minimal bandwidth.** Only inputs are transmitted: `PlayerInput` is 16
  bools + derived values ≈ ~4 bytes per player per frame. For 4 players at
  60 FPS, that's roughly 960 bytes/second total — trivially small.
- **No state serialization needed.** Peers run the full simulation locally, so
  there's no need to serialize or transmit entity state.
- **Architectural alignment.** The `og::sim` module was designed with this
  model in mind: `InputSnapshot` with 4-player inputs, deterministic `step()`
  with seeded RNG, event stream output. The `InputState` struct in
  `GameContext` maps directly to a network message.
- **Simpler initial implementation.** The transport layer only needs to send
  small input packets and handle synchronization barriers.

**Cons:**
- **Requires strict determinism.** This is the fundamental challenge. Currently:
  - `random()` wraps `rand()` which is platform-dependent.
  - ~200+ call sites use `rng()` which must all be routed through a shared
    seeded RNG.
  - Float operations can vary across compilers/platforms (though x86-64 and
    ARM64 IEEE 754 compliance makes this less of a concern in practice).
  - Entity iteration order must be stable (currently `std::list` which is
    insertion-ordered — good).
  - Any uninitialized memory or pointer-comparison-dependent logic would
    cause divergence.
- **Lockstep latency.** The simulation cannot advance until all players'
  inputs arrive. With 100ms RTT, this means ~100ms input lag per frame.
  Mitigation: input delay buffering (run the simulation N frames behind input
  collection, allowing network transit time during the buffer window).
- **No late join or reconnect.** Without state snapshots, a disconnected player
  cannot rejoin mid-game. The entire game would need to restart or the
  disconnected player would need to replay all inputs from the beginning.
  (Mitigation: periodic state checkpoints, but this brings back serialization
  complexity.)
- **Debugging desyncs is notoriously difficult.** When two peers diverge, it's
  hard to identify which operation caused the discrepancy. Typically requires
  per-frame state hashing and bisection debugging.

**Estimated effort:** Moderate-high. The determinism work is substantial
(auditing all RNG calls, auditing float operations, adding sync verification)
but doesn't require architectural refactoring. The networking layer is simple.

### 2.3 Option C: Client-Server with Input Forwarding (Hybrid)

**Model:** One peer acts as the "host" running the simulation. Other peers
send inputs to the host. The host distributes all collected inputs to all
peers (including itself). All peers run the simulation with the same inputs.
Unlike pure lockstep, the host is authoritative — periodic state hashes from
the host serve as ground truth, and desynced clients can be corrected.

This is effectively **lockstep with an authoritative host** — a common model
for games like Age of Empires, StarCraft, and many RTS games.

**Pros:**
- **Low bandwidth** (same as lockstep — input-only transmission).
- **Simpler than full state replication** — no entity serialization needed for
  normal operation.
- **Authority for dispute resolution.** If a desync is detected via hash
  mismatch, the host's state is canonical. This is gentler than pure lockstep
  where any desync is fatal.
- **Natural host migration path.** The host is just a peer that also relays
  inputs. If the host disconnects, another peer could take over (though this
  adds complexity).
- **Aligned with existing architecture.** Same `InputState` exchange as
  lockstep, with the addition of a relay/authority role.

**Cons:**
- **Still requires determinism** for normal operation. The same ~200+ RNG call
  sites must be audited and migrated. The hash-based desync detection is a
  safety net, not a substitute for determinism.
- **Desync recovery is complex.** If a client desyncs, correcting it requires
  either a full state snapshot (bringing back serialization) or disconnecting
  the client.
- **Host has an advantage.** The host peer has zero network latency for their
  own inputs. Other peers experience the host's relay latency.
- **Still has lockstep-style latency.** All peers wait for inputs before
  advancing (though input delay buffering applies here too).

**Estimated effort:** Moderate-high (similar to lockstep, plus host
authority and relay logic).

---

## 3. Recommendation

**Recommended approach: Option C (Client-Server with Input Forwarding), with
a phased rollout that starts from Option B (Deterministic Lockstep) as the
foundation.**

### Rationale

1. **The codebase is architecturally aligned with input-exchange models.**
   The `og::sim` module, `InputSnapshot`, `InputState`, and `GameContext`
   infrastructure were designed with deterministic simulation in mind. The
   existing "shadow sim" pipeline in `game_loop.cpp` demonstrates the
   intended runtime→sim data flow.

2. **State serialization is impractical in the near term.** The entity
   system's deep pointer graphs (`foe`, `leader`, `owner`, `collide_ob`),
   mixed simulation/rendering inheritance (`walker→pixieN→pixie`), and
   lack of entity IDs make full state serialization a multi-month project
   that would require fundamental architectural changes.

3. **Bandwidth favors input exchange.** At ~1 KB/s for 4 players, input
   exchange works over any connection. State replication at 60 FPS would
   require either aggressive delta compression or reduced update rates,
   both adding complexity and degrading the experience.

4. **The determinism investment pays for itself.** Regardless of the
   networking model, making the simulation deterministic enables:
   - Replay recording (just store inputs + seed)
   - Automated regression testing (compare output hashes)
   - Ghost/replay features
   - The foundation for any future netcode improvements

5. **The hybrid host-authority model adds a practical safety net.** Pure
   lockstep is fragile — any determinism bug causes a hard desync. The
   host-authority layer allows detecting desyncs (via periodic state hashes)
   and gracefully handling them (disconnect the desynced client) rather than
   having all peers diverge silently.

6. **The game's scale is manageable.** With 4 players max, low entity
   counts (20–80), and a 60 FPS target, the synchronization requirements
   are well within the capabilities of a lockstep-style model. This is not
   an MMO or a twitch shooter — it's a cooperative gauntlet game where
   100–150ms of input delay is tolerable.

### Latency Mitigation

The recommended approach uses **input delay buffering** (also called "input
delay" or "pipeline depth"):

- Collect inputs for frame N, but don't simulate frame N until N+D (where
  D is the delay buffer, typically 2–4 frames = 33–67ms at 60 FPS).
- During the D-frame window, inputs are transmitted to all peers.
- If an input hasn't arrived by the time its frame is due, either wait
  (causing a hitch) or predict the input (repeat last known input).

At 3 frames of delay (50ms) and typical internet RTT of 30–80ms, most
inputs will arrive in time without hitches.

---

## 4. Key Technical Challenges

### 4.1 Determinism (Critical — Must Solve First)

**RNG migration:** All ~200+ `rng()` call sites must be routed through a
single, seeded, platform-independent RNG. The `SeededRandom` class in
`game_context.h` already provides the right implementation (LCG with
`1103515245 * state + 12345`). Steps:
1. Replace the global `random()` function body with a call to `ctx().rng->next()`.
2. Verify all entity code calls `rng()` (the macro) rather than `rand()` directly.
3. Seed the RNG identically on all peers at level start.

**Float determinism:** The game uses `float` extensively for positions,
damage, and step sizes. IEEE 754 compliance on modern x86-64 and ARM64
platforms means basic arithmetic (+, -, *, /) produces identical results.
Risks:
- `sin()`, `cos()`, `sqrt()` in standard libraries may differ. Audit usage
  (appears limited to angle calculations in `walker::facing()`).
- Compiler optimizations (`-ffast-math`) must be avoided.
- Floating-point contraction (FMA) must be controlled via compiler flags.

**Entity iteration order:** `std::list` preserves insertion order, so
`oblist`, `weaplist`, and `fxlist` iteration is stable. Verify no code sorts
these lists or iterates in pointer-address order.

**State initialization:** All entity fields must be explicitly initialized.
Audit `walker`, `living`, `weap`, `treasure`, `effect` constructors for
uninitialized members.

### 4.2 Network Transport Layer

**Protocol:** UDP with a thin reliability layer for input packets. Inputs
are small (~4 bytes per player) and time-sensitive. TCP's head-of-line
blocking would cause unnecessary stalls.

**Packet format (proposed):**
```
InputPacket:
  uint32  frame_number
  uint8   player_index
  uint16  input_bits       // 16 keys as a bitmask
  uint32  state_hash       // periodic (every 60 frames)
```

**Library options:**
- **ENet** — lightweight, reliable UDP, C library, well-suited for game
  networking. Cross-platform (Windows, Linux, macOS). Would be a new
  vendored dependency.
- **SDL_net** — SDL companion library for basic TCP/UDP sockets. Very minimal;
  would need to build reliability on top.
- **GameNetworkingSockets** (Valve) — full-featured, but heavy dependency.
- **Raw BSD sockets** — maximum control, but significant boilerplate.

**Recommendation:** ENet. It provides connection management, reliable and
unreliable channels, bandwidth management, and is small enough to vendor
(~5k lines of C).

### 4.3 Session Management

**Lobby/connection:** Before gameplay starts, peers need to discover each
other, negotiate player slots, and synchronize the start. Options:
- **LAN discovery:** UDP broadcast on the local network.
- **Direct IP:** User enters the host's IP address manually.
- **Relay/matchmaking server:** Out of scope for initial implementation.

**Level synchronization:** All peers must load the same campaign and level.
Since campaign data is read-only during gameplay, the host can transmit
the campaign ID and level number; clients load from their local copy.
Clients without the campaign would need to be rejected or have the campaign
transferred (future enhancement).

### 4.4 Headless Host Mode

The host needs to run the simulation without necessarily rendering (for
dedicated server support in the future). Currently `screen extends video`,
coupling simulation to SDL graphics initialization. Options:
- **Short term:** The host is always a playing peer (renders normally).
  No headless mode needed initially.
- **Long term:** Factor simulation state out of `screen` into a separate
  `GameWorld` class that `screen` wraps for rendering. The `og::sim`
  module is the intended destination for this logic.

### 4.5 Game Flow Integration

The picker/menu UI, team selection, and level transitions use blocking
loops that would need adaptation for networked play. The host controls
level transitions; clients follow. Key integration points:
- `picker_main()` — team selection (could be done pre-connection)
- `glad_main()` / `game_frame()` — gameplay loop (needs sync)
- `screen::endgame()` — level completion (host decides, broadcasts)

---

## 5. Implementation Plan

### Phase 0: Foundation — Deterministic Simulation (4–6 weeks)

**Goal:** Make the game simulation bit-for-bit deterministic when given the
same seed and input sequence.

1. **Replace global RNG with seeded RNG.**
   - Modify `random()` to use `ctx().rng->next()` instead of `rand()`.
   - Add seed parameter to game initialization.
   - Verify with test: run two `GameSession` instances with same seed and
     same inputs; assert identical state hashes after N frames.

2. **Add state hashing.**
   - Implement `screen::compute_state_hash()` that hashes entity positions,
     HP, and key state fields.
   - Run hash after each `screen::act()` call.
   - Add determinism regression test: record inputs + hash per frame, replay
     and verify hashes match.

3. **Audit and fix non-determinism sources.**
   - Audit all `rand()` / `srand()` calls (should only be in `random()`).
   - Audit float operations for platform sensitivity.
   - Ensure all entity fields are zero-initialized in constructors.
   - Verify entity list iteration order is stable.

4. **Decouple InputState from SDL.**
   - Ensure `InputState` can be populated from network data (not only SDL).
   - This is mostly done: `input_state_from_sdl()` already populates a
     struct that could equally be filled from a network packet.

### Phase 1: Network Transport Layer (3–4 weeks)

**Goal:** Establish peer-to-peer connectivity with reliable input exchange.

1. **Vendor ENet** (or chosen library) into `third_party/`.
2. **Create `og_net` module** in `src/net/`, `include/openglad/net/`.
   - `NetHost` — creates and manages a host endpoint.
   - `NetClient` — connects to a host.
   - `NetSession` — manages the connection lifecycle and input exchange.
3. **Define wire protocol.**
   - `InputPacket`: frame number, player index, input bitmask.
   - `SyncPacket`: periodic state hash for desync detection.
   - `ControlPacket`: game start, level change, player join/leave.
4. **Implement input exchange loop.**
   - Host collects inputs from all clients + self.
   - Host broadcasts collected inputs for each frame.
   - Clients buffer inputs until the target frame's inputs are all received.
5. **Add LAN discovery** (UDP broadcast) and direct-IP connection.

### Phase 2: Game Loop Integration (3–4 weeks)

**Goal:** Run a networked game with synchronized simulation.

1. **Add input delay buffer** to the game loop.
   - Modify `game_frame_with_result()` to defer simulation by D frames.
   - Local input goes into the buffer at frame N; simulation runs frame N-D
     when all inputs for N-D are available.
2. **Synchronize game start.**
   - Host signals "start" with level ID, RNG seed, player assignments.
   - All peers load the level and begin simulation on the same frame.
3. **Integrate desync detection.**
   - Every 60 frames, peers exchange state hashes.
   - On mismatch, log a warning (and optionally disconnect the desynced client).
4. **Handle disconnections.**
   - If a client disconnects, their entities continue with last-known input
     (or switch to AI).
   - If the host disconnects, end the game (host migration deferred).

### Phase 3: UI and Polish (2–3 weeks)

**Goal:** User-facing multiplayer experience.

1. **Add "Network Game" option to main menu.**
   - Host Game: start a host, show IP/port, wait for players.
   - Join Game: enter IP, connect, wait for host to start.
2. **Network lobby screen.**
   - Show connected players, their team selections.
   - Host controls "Start Game" button.
3. **Team selection for network games.**
   - Each player selects their team locally before connecting, OR
   - Use a shared picker where each player edits their own team slot.
4. **In-game network status HUD.**
   - Ping display, connection quality indicator.
   - "Waiting for player..." message on network stalls.
5. **Chat** (optional): simple text messages between peers.

### Phase 4: Hardening and Advanced Features (Ongoing)

1. **Desync investigation tooling.** Per-frame state dumps, binary diff
   comparison, automated desync bisection.
2. **Network statistics.** Packet loss, latency histogram, jitter buffer
   status.
3. **Host migration.** If the host disconnects, another peer takes over.
   Requires lightweight state snapshot.
4. **NAT traversal.** UDP hole punching or STUN/TURN for connections across
   NATs (for internet play beyond LAN).
5. **Replay system.** Record seed + inputs → replay file. Verify with
   hash comparison. Share replays.
6. **Dedicated server mode.** Headless host that runs simulation without
   rendering. Requires factoring simulation out of `screen`.

---

## 6. Appendix: Key File Reference

### Game Loop and Simulation
| File | Relevance |
|------|-----------|
| `src/runtime/game_loop.cpp` | Frame loop: `game_frame_with_result()`, shadow sim integration |
| `src/runtime/screen.cpp:647` | `screen::act()` — main simulation tick |
| `src/runtime/screen.cpp:636` | `screen::continuous_input()` — player input application |
| `src/sim/simulator.cpp` | Skeleton deterministic simulator |
| `include/openglad/sim/simulator.h` | `InputSnapshot`, `State`, `Simulator` types |
| `include/openglad/sim/event.h` | `EventKind` enum, `Event` struct |

### Input System
| File | Relevance |
|------|-----------|
| `include/openglad/runtime/game_context.h` | `InputState`, `PlayerInput`, `InputKey`, `IRandom` |
| `src/input/input.cpp` | `isPlayerHoldingKey()`, `didPlayerPressKey()`, key bindings |
| `src/render/view.cpp:902` | `viewscreen::continuous_input()` — keys → entity commands |
| `src/render/view.cpp:442` | `viewscreen::input(event)` — discrete input events |

### Entity System
| File | Relevance |
|------|-----------|
| `include/openglad/entities/walker.h` | Base entity class — all mutable state fields |
| `src/entities/walker.cpp:994` | `walker::act()` — base entity tick |
| `src/entities/living.cpp:60` | `living::act()` — AI entity tick with extensive RNG usage |
| `src/entities/walker_combat.cpp` | Combat logic, damage calculation |
| `src/entities/walker_movement.cpp` | Movement and collision |
| `src/entities/obmap.cpp` | Spatial hash for collision detection |

### Serialization
| File | Relevance |
|------|-----------|
| `src/data/save_data.cpp` | Save file format (GTL v9), team serialization |
| `src/data/level_data.cpp` | Level file format (FSS v2-9), entity spawn data |
| `include/openglad/data/save_data.h` | `SaveData` struct, `SaveDataIoError` |
| `include/openglad/data/level_data.h` | `LevelData` struct, `CampaignData` |

### Session and Context
| File | Relevance |
|------|-----------|
| `include/openglad/runtime/game_session.h` | `GameSession` RAII root, session config |
| `src/runtime/game_session.cpp` | Session lifecycle, RNG setup |
| `src/runtime/game_context.cpp` | `ctx()` global, input polling |

### UI and Menus
| File | Relevance |
|------|-----------|
| `src/ui/picker.cpp` | Team selection, main menu loop |
| `src/ui/picker_main_menu.cpp` | Main menu state machine |
| `src/glad.cpp` | Entry point, Emscripten frame wrapper, game state machine |

### Random Number Generation
| File | Relevance |
|------|-----------|
| `src/runtime/screen.cpp:102` | Global `random()` function (wraps `rand()`) |
| `include/openglad/runtime/game_context.h:31` | `IRandom` interface, `SeededRandom`, `FixedRandom` |
| `src/sim/simulator.cpp:20` | Simulator's internal LCG |
