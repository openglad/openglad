# Pointer Modernization (Phase 7) Summary

## Scope audited
- `walker*` lifecycle across `loader`, `LevelData`, `screen`, `view`, and combat/movement logic.
- Picker/menu globals (`current_guy`, `old_guy`, backdrops/logo pixies, button arrays).
- Remaining raw `new`/`delete` patterns in non-vendored code.

## Ownership findings

### `walker*`
- Owner: `LevelData` owns walkers via `std::list<std::unique_ptr<walker>>` in `oblist`, `fxlist`, `weaplist`, and `dead_list`.
- Creation: `loader::create_walker_owned()` is the owning factory; raw-return `create_walker()` is legacy compatibility.
- Deletion: list erase/clear in `LevelData::remove_ob()` / `delete_objects()` performs destruction consistently.
- Sharing/transfer: no shared ownership; many raw pointers (`foe`, `leader`, `owner`, `view::control`) are observers.
- Risks:
  - Stale observer pointers are the primary hazard class (not double-free of walkers).
  - `view::control` stale-pointer cleanup is already handled in `LevelData::delete_objects()`.
  - Weapon owner lifetime is intentionally extended via `dead_list` to reduce dangling `owner` references.
- Modernization decision: keep observer raw pointers; keep ownership as `unique_ptr` in `LevelData`.

### Picker/menu pixies (`backdrops`, `main_*_pix`)
- Owner in production: `std::array<std::unique_ptr<pixieN>, 5> backdrops`, `std::unique_ptr<pixieN> main_title_logo_pix`, `main_columns_pix`.
- Prior risk: several tests declared these as raw pointers and manually deleted them, creating type/lifetime mismatch and potential double-free/UAF.
- Modernization decision: tests now use matching `unique_ptr` declarations and `.reset()` cleanup.

### Picker guy pointers (`current_guy`, `old_guy`)
- Owner in production: `current_guy` is `std::unique_ptr<guy>`.
- `old_guy` is a non-owning alias (typically points into `SaveData::team_list`).
- Prior risk: several tests treated `current_guy` as raw-owned and manually `delete`d it.
- Modernization decision: tests now model ownership correctly (`std::unique_ptr` for owned, raw for observer alias).

### `screen* myscreen`
- Current model: global raw pointer created in app/test startup and torn down in picker shutdown (or process exit in tests).
- Modernization candidacy: conceptually single-owner (`unique_ptr`-friendly), but conversion is high-impact because of broad `extern screen*` usage and context aliasing (`ctx().game_screen`).
- Decision: leave as-is in this phase; conversion should be done as a dedicated follow-up to avoid broad churn/regression risk.

### `vbutton* allbuttons[MAX_BUTTONS]`
- Current model: manual `new/delete` array ownership in `init_buttons()` and picker cleanup.
- Modernization candidacy: could be migrated to `std::array<std::unique_ptr<vbutton>, MAX_BUTTONS>`, but many call sites rely on raw-pointer semantics and sentinel-like null checks.
- Decision: leave as-is in this phase; treat as a focused refactor candidate.

## Changes implemented in this phase
- `LevelData::add_ob()`, `add_fx_ob()`, `add_weap_ob()` now consume `loader::create_walker_owned()` directly and move into owning lists (no transient raw-owned walker creation path).
- Updated picker-related tests to align with production ownership types:
  - `current_guy` handled as `std::unique_ptr<guy>`.
  - Pixie globals handled as `std::unique_ptr` / `std::array<std::unique_ptr<...>>`.
  - Manual deletes replaced with `.reset()` or scope-owned smart pointers.

## Verification
- Build: `cmake --preset ci-test && cmake --build --preset ci-test` passed.
- Tests: `ctest --preset ci-test` passed (5/5).
