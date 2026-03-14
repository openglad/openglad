# Phase 21: Lobby Server Logic

> **See also:** [Phase 20 (Lobby Data Model)](phase-20-lobby-data-model.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

**Changes:**
- New files: `include/openglad/gameplay/lobby_server.h`, `src/gameplay/lobby_server.cpp`
- Manages lobby state machine: join/leave, team assignment, ready-up, start transition
- First connected client = host (admin: pick scenario, force start)
- Broadcasts `LobbyState` on every change
- Populates equivalent of `SaveData` fields: `numplayers`, `allied_mode`, `team_list`, `current_campaign`, `scen_num`
