# Phase 27: Headless Server Binary

> **See also:** [Phase 14 (GameServer)](phase-14-server-client.md) | [Phase 24 (WebSocket Server)](phase-24-websocket-server.md) | [Context (Module Placement)](../common/context.md) | [Verification Strategy](../common/verification-strategy.md)

New CMake target `openglad_server` — no SDL, no rendering. Follow the `openglad_text` target (`CMakeLists.txt:938`) as a proven SDL-free precedent.

**Changes:**
- New `src/server/server_main.cpp`
- Links: `og_gameplay`, `og_core`, `og_resources`, `og_ext_ixwebsocket` + ws transport
- Does NOT link: `og_interface`, `og_platform_sdl` (no SDL, no rendering, no UI)
- Note: `walker` already delegates rendering to an optional `render_` component (`std::unique_ptr<IRenderComponent>`), so headless walkers just have `render_ = nullptr`
- Runs LobbyServer -> GameServer loop
- Minimal GameContext setup without screen/SDL — needs `IRandom`, `SimEventLog`, but not `InputState` polling (server receives input over network)
- **Headless level loading:** The headless server needs `entity_factory`, `entity_configurator`, and `entity_derived_stats` callbacks to create entities during `read_scenario()` and `apply_snapshot()`. The `openglad_text` target already solves this with `walker_headless.cpp` and `platform_headless.cpp` (headless stubs for walker render components and platform hooks). The server binary reuses these same stubs — entities are created with `render_ = nullptr`, and `entity_configurator` provides sim-relevant data (frame counts) without loading sprite pixel data.
- Requires careful CMakeLists.txt changes: new executable target similar to `openglad`/`openscen`/`openglad_demo` but with different link set. Must verify no transitive SDL dependencies leak through gameplay modules.
- **CI guardrail:** Add a CI build step that compiles `openglad_server` without SDL2 available (e.g., `cmake -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON`) to guarantee no SDL leakage. This is the kind of thing that breaks silently when someone adds an innocent-looking `#include` to a gameplay header.
