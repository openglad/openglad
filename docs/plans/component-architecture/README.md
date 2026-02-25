# Component Architecture Migration Plan

**Branch:** `feat/desingletonize`
**Date:** 2026-02-24
**Builds on:** desingletonize-globals work (largely complete; original plan file
deleted — relevant remaining items subsumed by Phase 4 and Phase 12 below),
remaining-singletons audit (deleted — relevant content inlined into Phase 12
below)

---

## Goal

Restructure OpenGlad from 10 fine-grained modules into 4 top-level components
plus a core foundation layer, with strict dependency rules enforced at build
time.

The **gameplay** component is fully self-sufficient: no calls to rendering,
input, sound, file I/O, or UI. It is a pure simulation sandbox.

---

## Navigation

| File | Contents |
|------|----------|
| [target-architecture.md](target-architecture.md) | Components, dependency rules, diagram, module mapping |
| [key-types.md](key-types.md) | GameWorld, LevelVisuals, screen, GameplayContext, IRenderComponent |
| [phase-01a.md](phase-01a.md) | Phase 1a: Move entity lists to GameWorld |
| [phase-01b.md](phase-01b.md) | Phase 1b: Spatial data, queries, finders, metadata |
| [phase-02.md](phase-02.md) | Phase 2: Absorb legacy simulation layer into GameWorld |
| [phase-03.md](phase-03.md) | Phase 3: Game state flags, eliminate TickResult |
| [phase-04.md](phase-04.md) | Phase 4: GameplayContext and current_game |
| [phase-05.md](phase-05.md) | Phase 5: Decouple SaveData from Gameplay |
| [phase-06.md](phase-06.md) | Phase 6: Move gloader to resources |
| [phase-07.md](phase-07.md) | Phase 7: Kill LevelData, create LevelVisuals |
| [phase-08.md](phase-08.md) | Phase 8: IRenderComponent in Gameplay |
| [phase-09.md](phase-09.md) | Phase 9: Split the Data Layer |
| [phase-10.md](phase-10.md) | Phase 10: Directory reorganization |
| [phase-11.md](phase-11.md) | Phase 11: Inter-component interfaces |
| [phase-12.md](phase-12.md) | Phase 12: Enforce dependencies and clean up |
| [cyclic-dependencies.md](cyclic-dependencies.md) | Known cycles and which phases break them |
| [testing-strategy.md](testing-strategy.md) | Test helpers, RAII patterns, per-phase migration |
| [hard-parts.md](hard-parts.md) | Hard parts and mitigations |

---

## Phase Sequence

All phases run sequentially, one at a time. Each phase must pass full ctest
before the next begins.

```
Phase 1a (GameWorld shell, entity lists)
   ▼
Phase 1b (spatial data, queries, finders, metadata)
   ▼
Phase 2  (absorb legacy simulation layer into GameWorld)
   ▼
Phase 3  (game state flags, eliminate TickResult)
   ▼
Phase 4  (current_game thread-local)          ← moved up: only needs Phases 1a–3
   ▼
Phase 5  (SaveData decoupling + sim_config removal)
   ▼
Phase 6  (move gloader to resources)
   ▼
Phase 7  (kill LevelData, create LevelVisuals)
   ▼
Phase 8  (IRenderComponent)
   ▼
Phase 9  (remaining data layer split)
   ▼
Phase 10 (directory reorganization + video split + ownership transfer)
   ▼
Phase 11 (inter-component interfaces)
   ▼
Phase 12 (enforcement + cleanup)
```

**Why this order:**

- **Phases 1a–3** create GameWorld incrementally: entity lists (1a), spatial
  data + queries (1b), then tick logic (2), then state flags (3). Each is
  independently testable and bisectable.
- **Phase 4** (current_game) comes right after Phase 3. It only needs GameWorld
  to exist with the right fields (Phases 1a–3) — it does NOT need gloader moved
  or LevelData killed. Moving it up front unblocks Phase 5 (SaveData decoupling)
  earlier.
- **Phase 5** (SaveData decoupling) needs `current_game->world->` to replace
  `sim_save->` references — entities have no other path to reach GameWorld fields.
- **Phase 6** (gloader) can now use the cleaner wiring: since Phase 4 already
  introduced `current_game`, no transitional `sim_*` pointer wiring is needed
  in the factory callback. Entities use `current_game->` from the start.
- **Phase 7** (kill LevelData) depends on Phase 6 (gloader moved out of
  LevelData).
- **Phase 8** (IRenderComponent) runs after Phases 4–5 to avoid merge conflicts
  in `walker.h` / `SimEntity` — all three modify these files.
- **Phase 9** depends on Phase 5. SaveData must be fully decoupled from entity
  code before moving it to resources.
- **Phase 10** is the big mechanical move — much easier after the logical splits
  (1–9) are in place. Includes the video/screen inheritance split and the
  GameWorld ownership transfer from screen to GameSession.
- **Phases 11–12** are cleanup/enforcement after reorganization.

---

## Relationship to Existing Desingletonize Work

This plan builds on the desingletonize work (largely complete). Some remaining
desingletonize phases are subsumed:

| Desingletonize Phase | Status | Disposition |
|---|---|---|
| Phase 0 (const sweep) | Done | N/A |
| Phase 1 (eliminate ctx globals) | Partial | Subsumed by Phase 4 here (`current_game` replaces `ctx()`) |
| Phases 2-9 | Done | N/A |
| Phase 10 (final verification) | Not started | Subsumed by Phase 12 here |

The key change: instead of `ctx()` → `current_session->ctx_`, we go to
`current_game->` in gameplay code (and `current_game->world->rng_` for the
RNG). `current_session` remains at the platform level for interface/UI code
that needs session state.

---

## Summary: Before vs After

### Before (current)

```
10 modules (core, sim, data, entities, io, runtime, render, input, ui, platform)
+ sdl_client/ and text_client/ overlays
screen class: game state + rendering + sound in one object
legacy simulation layer: separate class with tick logic, tick counters, RNG
LevelData: entity lists + rendering data + loader in one class
Entity code: holds 6 sim_* pointers, references SaveData, calls UI prompts
Thread-local: current_session (GameSession*) at platform level
Global accessor: ctx() with fallback/override machinery
```

### After (target)

```
4 components + core (gameplay, resources, interface, platform)
GameWorld class: pure game state + tick logic + RNG, no rendering, no I/O
No legacy simulation layer class (absorbed into GameWorld)
No LevelData class (replaced by GameWorld + LevelVisuals)
Entity code: accesses current_game->world-> for everything (including RNG),
  no SaveData, no UI calls, no sim_config (effects checks in interface layer)
Thread-local in gameplay: current_game (GameplayContext*)
Thread-local in platform: current_session (GameSession*) — for UI/display code
No ctx() — retired
```

### New Ownership Model

```
Platform (GameSession)
├── Creates and owns GameplayContext → sets current_game
├── Creates and owns GameWorld → populates from save/level files via resources
│   ├── GameWorld.rng_ (deterministic LCG, accessed via current_game->world->rng_)
│   ├── GameWorld.entity_factory (callback wired to gloader in resources)
│   ├── GameWorld.difficulty (copied from GameSession at level start)
│   └── GameWorld.withdraw_requested (set by WithdrawToLevel event, tick() early-outs)
├── Creates and owns screen (display shell) → references GameWorld
├── Orchestrates game loop:
│   1. Poll input (platform)
│   2. Translate to game commands (interface)
│   3. Set current_game, call GameWorld::tick() (gameplay)
│   4. Drain sim events (platform dispatches sound, interface dispatches visuals)
│   5. Call screen::redraw() (interface)
│   6. Present frame (platform)
└── After level: read scores/stats from GameWorld, update SaveData, save via resources
```
