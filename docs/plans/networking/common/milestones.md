# Key Milestones

- **Phase 0** — deterministic simulation: all gameplay `rand()` calls migrated to `SimRandom`, `pow()` replaced with bit shift, `walkrounds` dead code deleted.
- **Phase 3** — all cross-reference pointers are private with enforced setter usage. `sync_ids_from_pointers()` removed. (Dirty tracking instrumentation deferred to Phase 8.)
- **Phase 8** — all 86 serializable fields private with getter/setter pairs that call `mark_dirty()`. Compile-time enforcement of dirty tracking.
- **Phase 11** — input replay system: record a game, play it back deterministically. Invaluable for debugging later phases.
- **Phase 16** — `screen::act()` split into sub-methods. Pure refactor, all tests pass unchanged.
- **Phase 17** — game loop wired through GameServer/GameClient with InProcessTransport (zero-copy). Game plays identically to before.
- **Phase 18** — integration tests calling `game_frame()`/`screen::act()` migrated to server/client path. `screen::act()` wrapper deleted. No fallback.
- **Phase 19** — smooth visual interpolation at render framerate, eliminating 12fps teleporting.
- **Phase 23** — lobby system works, local multiplayer flows through lobby -> game -> lobby.
- **Phase 29** — two separate processes (or a browser + native) playing the same game over WebSockets.
- **Phase 32** — players connect via room codes through Cloudflare Workers relay. No port forwarding, no IP addresses. Browser and native clients play together through the relay transparently.
