# Phase 20: Lobby Data Model

> **See also:** [Phase 13 (Protocol Framing)](phase-13-transport-interface.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

**Changes:**
- New header: `include/openglad/gameplay/lobby_state.h`
- `LobbyPlayer`: name, team, character slots (references to `guy` data), ready flag, is_host, player_index (global)
- `LobbyState`: player list, campaign/scenario selection, game settings (difficulty, allied_mode matching `SaveData::allied_mode`)
- `LobbyMessage` types: join, leave, ready, team_change, start_game, settings_change
- Serialization for lobby messages using same protocol framing from Phase 13
