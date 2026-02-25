# Known Cyclic Dependencies

**Part of:** [Component Architecture Plan](README.md)

---

The CMakeLists.txt uses `--start-group`/`--end-group` (`CMakeLists.txt:749-766`)
to paper over link-order issues caused by these inter-module cycles. Phase 12
wants to enforce acyclic dependencies — these are the cycles to break:

1. **og_render ↔ og_runtime** — `view.h` includes `game_session.h`; runtime
   includes render headers
2. **og_input ↔ og_runtime** — Header-level cycle path: `input.h` →
   `game_session.h` → (runtime depends on) `game_context.h` →
   `input/input_state.h`. Link-level cycle: `og_input` uses `GameSession`
   types, `og_runtime` uses `InputState` types.
3. **og_entities ↔ og_data** — entity source files include `level_data.h`,
   `save_data.h`, `gparser.h`; data code includes `walker.h`, `guy.h`
4. **og_entities ↔ og_runtime** — entity code includes `game_session.h`;
   runtime includes entity headers
5. **og_ui ↔ og_runtime** — UI headers include `screen.h`; runtime includes UI
   headers
6. **og_ui → og_entities** — `results_screen.h` includes `guy.h`
7. **og_render → og_data** — `view.h` and `radar.h` include `level_data.h`;
   `pixie.h` includes `pixie_data.h`

## Which Phases Break Each Cycle

- **Cycles 3, 4:** Broken by [Phases 1–5](phase-01a.md). Entities stop reaching into
  data/runtime for `sim_*` access — they use `current_game->` instead.
- **Cycles 1, 2:** Broken by [Phase 10](phase-10.md). Render/input stop needing
  `game_session.h` directly after the directory reorg and video split.
- **Cycles 5, 6, 7:** Broken by [Phase 10](phase-10.md). UI/render references to
  `screen.h`, `level_data.h`, `guy.h` resolve when files move to their
  target components and dependencies flow inward.
