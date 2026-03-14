# Phase 16: Split `screen::act()` Into Sub-Methods (Pure Refactor)

> **See also:** [Phase 15 (GameServer/GameClient)](phase-15-server-client.md) | [Verification Strategy](docs/plans/networking/common/verification-strategy.md)

Extract the internals of `screen::act()` into three new methods, leaving `screen::act()` as a thin wrapper that calls them in sequence. **All ~1787 existing tests pass unchanged** — this is a behavior-identical refactor.

**`screen::act()` refactoring:** The current `screen::act()` does: `world_.tick()` -> dispatch events (8 early return paths identified: lines 903, 955, 1017, 1031, 1036, 1043, 1066, 1068). Split into three methods:
  - `screen::tick_world()` — calls `world_.tick()`, drains SimEventLog into a `SimEventBatch` + `GameFlowEventBatch`. Returns the two batches. Used by GameServer in Phase 17.
  - `screen::dispatch_cosmetic_events(SimEventBatch&)` — dispatches Tier 1 events (sounds, notifications, palette, redraw). Used by GameClient in Phase 17.
  - `screen::dispatch_game_flow_events(GameFlowEventBatch&)` — dispatches Tier 2 events (EndGame, SetEnd, RequestExitConfirmation, WithdrawToLevel, ScoreChange). Used by GameClient in Phase 17. These trigger UI transitions like `endgame()`, exit confirmation prompts, and withdrawal sequences.

**Refactoring complexity notes:**
- `screen::act()` has **8 early return paths** (line 903: null context, 955: EndGame, 1017: withdraw load error, 1031: withdraw save error, 1036: withdraw success, 1043: exit success, 1066: world.end, 1068: normal exit). These all need to map to game-flow events or snapshot flags rather than immediate returns.
- The exit/withdraw confirmation dialog (lines 980-1048) is now handled by the freeze-and-ask protocol (Phase 15) — no blocking `yes_or_no_prompt` call on the server.
- `DamageTile` is already moved to simulation layer in Phase 6, so it's not in the event dispatch path.

**Compatibility wrapper (`screen::act()`):** After the split, `screen::act()` becomes:
```cpp
bool screen::act() {
    auto [cosmetic, game_flow] = tick_world();
    dispatch_cosmetic_events(cosmetic);
    dispatch_game_flow_events(game_flow);
    // ... viewscreen control cleanup, same as before ...
}
```

All existing tests and the game loop call `screen::act()` exactly as before. The sub-methods are new public API that Phase 17 will use.

**Verify:** All existing ~1787 tests pass with zero changes. Game plays identically. The sub-methods are individually callable (tested via new unit tests).
