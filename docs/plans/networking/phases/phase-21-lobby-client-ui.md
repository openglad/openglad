# Phase 21: Lobby Client UI

> **See also:** [Phase 19 (Lobby Data Model)](phase-19-lobby-data-model.md) | [Phase 20 (Lobby Server)](phase-20-lobby-server.md) | [Verification Strategy](../common/verification-strategy.md)

Refactor picker to operate as a lobby client. The current picker flow is: `picker_main()` (`picker.cpp:366-377`) -> `picker_initialize_shared_menu_state()` -> `run_picker()` state machine -> team selection via `IPickerClient` -> sets `g_start_game_requested` flag -> state machine transitions to gameplay.

**Important architecture note:** The picker has two code paths:
- **Native:** `picker_main()` is a blocking call (traditional event loop)
- **Emscripten:** Non-blocking state machine driven by `picker_frame()` (returns bool for state transitions), with `picker_init()`, `picker_check_start_requested()`, `picker_cleanup_for_game()`, `picker_reinit_after_game()` lifecycle functions (all in `picker.cpp:1642-1703`). The Emscripten frame wrapper (`glad.cpp:158-170`) calls `picker_frame()` each browser frame and transitions to `GameState::Playing` when it returns true.

Both paths must be updated for lobby integration.

**Changes:**
- `src/interface/ui/picker.cpp` and related picker files (`picker_team_build.cpp`, `picker_common.cpp`)
- Picker reads from `LobbyState` for player list / team assignments instead of only local `SaveData`
- Team selection syncs to lobby server via messages (currently just writes `SaveData::numplayers` directly via `set_player_mode()` at `picker.cpp:1209`)
- Game start mechanism: currently sets `g_start_game_requested` flag (checked by `picker_check_start_requested()` at `picker.cpp:1642-1646`). Change this to send a lobby start message instead, with the lobby confirming before the flag is set.
- Single-player: lobby auto-populated, transparent to user — `SdlPickerClient` creates local lobby internally
- **Emscripten path:** `picker_frame()` polls for lobby state updates each frame. Lobby "game start" confirmation sets `g_start_game_requested`, triggering the `GameState::Picker -> GameState::Playing` transition via the existing state machine.
- **Native path:** lobby events are processed within the blocking `run_picker()` loop. Start request sends lobby message and waits for lobby confirmation before proceeding to `glad_main()`.
