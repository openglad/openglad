# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

OpenGlad is a cross-platform C++ port of the DOS game "Gladiator" - a top-down gauntlet-style action RPG with multiplayer support and a built-in scenario editor. Licensed under GPL v2. The current focus is on the web/Emscripten build.

## Build Commands

### Web Build (Primary)

**Prerequisite: Install Emscripten SDK**
```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh  # Run in each new terminal, or add to shell profile
```

**Build:**
```bash
./scripts/build_web.sh
```

This compiles everything with Emscripten, packages game assets, and outputs to `dist/`:
- `play.html` - HTML shell with canvas
- `play.js` - JavaScript runtime glue
- `play.wasm` - WebAssembly binary
- `play.data` - Packaged game assets

**Run locally:**
```bash
cd dist && python3 -m http.server 8080
# Then open http://localhost:8080/index.html  # or http://localhost:8080/play.html
```

### Native Build (Autotools)

```bash
./autogen.sh
./configure
make
./openglad    # Run game
./openscen    # Run level editor
```

## Architecture

### Core Class Hierarchy

```
walker (base entity class)
├── living (player/AI entities with health, AI behavior)
├── weap (projectiles and weapons)
├── treasure (collectible items)
└── effect (visual effects)

screen (main game world container)
├── viewscreen[] (up to 4 player viewports for split-screen)
├── level_data (current level layout and objects)
└── obmap (spatial indexing for collision detection)

video (SDL2 graphics layer)
└── pixel buffer manipulation, rendering primitives
```

### Key Source Files

| File | Purpose |
|------|---------|
| `src/glad.cpp` | Entry point, main game loop, Emscripten frame wrapper |
| `src/walker.cpp` | Base entity logic (142KB) - movement, combat, behavior |
| `src/screen.cpp` | Game world state, entity management |
| `src/view.cpp` | Viewport/camera rendering (multiplayer split-screen) |
| `src/video.cpp` | SDL2 graphics abstraction, pixel buffer ops |
| `src/picker.cpp` | Team selection/hiring UI (129KB) |
| `src/level_editor.cpp` | Scenario editor - openscen (147KB) |
| `src/input.cpp` | Keyboard/controller handling |
| `src/stats.cpp` | Combat statistics and calculations |

### Emscripten Integration

The web build uses conditional compilation in `src/glad.cpp`:

```cpp
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// Uses emscripten_set_main_loop for browser's requestAnimationFrame
// FrameState struct manages timing to maintain game's intended frame rate
#endif
```

The HTML shell template is at `web/shell.html` - handles canvas scaling, loading UI, and WebGL context.

### Bundled Libraries (src/external/)

- **physfs/** - Virtual filesystem, ZIP archive support
- **micropather/** - A* pathfinding algorithm
- **libyaml/** + **yam/** - YAML configuration parsing
- **libzip/** + **zlib123/** - Compression and archives

### Game Assets

- `pix/` - 235 sprite/tileset files (.pix format)
- `sound/` - Audio files (WAV, OGG)
- `cfg/` - Configuration (openglad.yaml)
- `extra_campaigns/` - Additional game scenarios
- `builtin/` - Core game resources

## Dependencies

**Web build:** Emscripten SDK with SDL2 ports (handled automatically via `-sUSE_SDL=2 -sUSE_SDL_MIXER=2`)

**Native build:** SDL2, SDL2_mixer, libpng, C++11 compiler

## Web Build Details

The build script (`scripts/build_web.sh`) compiles with these key Emscripten flags:
- `-sUSE_SDL=2 -sUSE_SDL_MIXER=2` - SDL2 ports
- `-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864` - 64MB initial heap
- `-sASYNCIFY` - Async support for file I/O
- `--preload-file` - Packages cfg/, pix/, sound/, etc. into play.data

Canvas renders at 320x200 base resolution, scaled with integer factors for crisp pixels (`image-rendering: pixelated`).

## Game Flow

1. `main()` → SDL init, create screen
2. `intro_main()` → splash screen
3. `picker_main()` → team selection
4. `glad_main()` → main game loop
5. `screen->act()` → game logic per frame
6. `screen->redraw()` → render to display
