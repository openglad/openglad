# Phase 22: Lobby -> Game Transition

> **See also:** [Phase 20 (Lobby Server)](phase-20-lobby-server.md) | [Phase 21 (Lobby Client UI)](phase-21-lobby-client-ui.md) | [Phase 14 (GameServer/GameClient)](phase-14-server-client.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Wire lobby completion to GameServer/GameClient creation.

**Changes:**
- `src/platform/sdl/glad_gameplay.cpp` — `glad_init()` accepts config from lobby (player assignments, team data, campaign/level selection)
- `src/platform/sdl/glad.cpp` — Emscripten state machine (`GameState::Picker` -> `GameState::Playing` transition at lines ~158-171) gets lobby-aware transition
- SaveData populated from lobby state before level load
- `screen::ready_for_battle(numviews)` called with numviews derived from lobby player count
