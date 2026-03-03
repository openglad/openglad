# Target Architecture

**Part of:** [Component Architecture Plan](README.md)

---

## Components

| Component | Purpose |
|-----------|---------|
| **core** | Foundation: logging, math, constants, type utilities |
| **gameplay** | Self-sufficient game simulation: entities, rules, world state |
| **resources** | File I/O, asset loading, serialization (levels, saves, config, sprites) |
| **interface** | Menus, sprites, logical rendering, input mapping, buttons |
| **platform** | SDL/text backends, audio playback, window management, orchestration |

## Dependency Rules

```
core      →  (nothing)
gameplay  →  core
resources →  core, gameplay (needs gameplay type definitions to serialize them)
interface →  core, gameplay, resources
platform  →  core, gameplay, interface, resources
```

**Gameplay calls nothing outside core.** No SDL, no file I/O, no UI prompts,
no sound. It produces events; the caller drains them.

## The Thread-Local

Gameplay has exactly one thread-local global: `current_game`
(`og::gameplay::GameplayContext*`). Platform-level code creates and installs it
before entering gameplay.

## Diagram

```
┌─────────────────────────────────────────────────┐
│                   platform                      │
│  GameSession, main(), SDL init, audio playback  │
│  sdl_client/, text_client/                      │
└──────┬──────────────┬──────────────┬────────────┘
       │              │              │
┌──────▼──────┐  ┌────▼────┐  ┌─────▼──────┐
│  interface  │  │resources│  │ (gameplay)  │
│  menus, UI  │  │ file IO │  │ (accessed   │
│  sprites    │  │ assets  │  │  directly)  │
│  rendering  │  │ config  │  │             │
└──────┬──────┘  └────┬────┘  └─────────────┘
       │              │
       ▼              ▼
┌──────┴──────────────┴──────┐
│          gameplay           │
│  GameWorld, entities, sim   │
│  thread_local current_game  │
└──────────────┬──────────────┘
               │
         ┌─────▼─────┐
         │   core    │
         │ log, math │
         └───────────┘
```

---

## Current Module → Component Mapping

| Current Module | Target Component | Notes |
|---|---|---|
| `og_core` (math, logging, util) | **core** | Unchanged |
| `og_sim` (SimWorld, events, IRandom) | **gameplay** | SimWorld absorbed into GameWorld |
| `og_entities` (walker hierarchy, families) | **gameplay** | Remove render/save/UI coupling |
| `og_data` — in-memory types: guy, statistics | **gameplay** | LevelData eliminated — replaced by GameWorld |
| `og_data` — file I/O: gloader, gparser, pixie_data, level/save serialization | **resources** | gloader source is in `og_runtime` (`src/runtime/gloader.cpp`), header in `og_data` (`include/openglad/data/gloader.h`) |
| `og_io` (PhysFS, zip, yaml) | **resources** | As-is |
| `og_render` (video, pixie, view, text, walker_draw, pal32, radar) | **interface** | Logical rendering |
| `og_input` (input mapping, buttons) | **interface** | User interaction |
| `og_ui` (picker, editor, intro, help, results) | **interface** | Menu system |
| `og_runtime` — game state: screen::act(), passability, entity finders | **gameplay** (as GameWorld) | Extracted from screen |
| `og_runtime` — display: screen::redraw(), viewscreens, score_panel | **interface** | |
| `og_runtime` — orchestration: GameSession, game loop driver | **platform** | |
| `og_platform` — sound loading | **resources** | |
| `og_platform` — sound playback | **platform** | |
| `sdl_client/` | **platform/sdl** | SDL backend |
| `text_client/` | **platform/text** | Headless backend |
