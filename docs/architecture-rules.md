# OpenGlad Architecture Rules

This document defines the module boundaries, dependency rules, and coding conventions
that are enforced by the build system and CI. Changes that violate these rules will
fail the build.

## Module Layout

```
include/openglad/<module>/   — public headers (stable API surface)
src/<module>/                — private implementation
third_party/<lib>/           — vendored external libraries
tests/unit/                  — headless unit tests (no SDL)
tests/                       — integration tests (full SDL)
```

## Module Dependency Direction

Dependencies flow inward toward purity. Arrows indicate allowed dependencies:

```
apps (openglad, openscen)
  → ui → runtime → sim → core
  → render → core
  → input → core
runtime → data → core
runtime → io → core
platform → io → core
```

### Forbidden Dependencies

- `og::sim` must NOT depend on SDL, render, ui, or platform.
- `og::data` must NOT depend on screen, UI globals, or rendering types.
- `og::core` must NOT depend on any other module.
- `og::ui` must NOT directly mutate deep sim internals; it issues commands to runtime.

## Vendor Header Isolation

**Enforced by:** `scripts/check_vendor_leaks.sh` (run at build time)

- Public headers (`include/openglad/`) must NEVER include vendor headers.
- Only `src/io/` may include filesystem/archive vendor headers (physfs, libzip, libyaml, yam).
- Only `src/entities/` may include micropather.h (pathfinding).
- All other modules are vendor-free.

## Legacy Header Guardrail

**Enforced by:** `scripts/check_graph_includes.sh` (run at build time)

- `graph.h` was the old umbrella header. Its allowlist is now EMPTY.
- New code must never include `graph.h`; use explicit narrow includes instead.

## Ownership Rules

### RAII Roots

- `og::runtime::GameSession` is the RAII root for all runtime state.
  It owns the screen, prefs, RNG, and installs legacy global shims.
- Production `main()` constructs a GameSession; tests construct one per test.

### Smart Pointers

- **Owning:** `std::unique_ptr<T>` (default). `std::shared_ptr<T>` only if lifetimes truly overlap.
- **Non-owning:** `T&` or `T*` with clear lifetime documentation.
- **No raw owning `new`/`delete`** in non-vendored code.

### Legacy Globals (Transitional)

These globals exist for backward compatibility. Prefer the `GameContext` accessors:

| Global | Owned By | Accessor |
|--------|----------|----------|
| `myscreen` | `GameSession::screen_owner_` | `ctx().active_screen()` |
| `theprefs` | `GameSession::ctx_.prefs` | `ctx().active_prefs()` |
| `cfg` | file-scope in `gparser.cpp` | `ctx().active_config()` |

## Simulation Architecture

### Deterministic Sim

- `og::sim::Simulator` runs headless, SDL-free.
- Given the same seed + input sequence, produces identical state and events.
- Events use `og::sim::EventKind` enum (Damage, Death, Spawn, PlaySound, etc.).
- Runtime feeds inputs to the simulator via `step(InputSnapshot, dt)`.

### Event Stream

The simulation emits semantic events. Runtime/render layers consume them:
- Sound requests → audio subsystem
- Visual FX → renderer
- Text popups → UI layer

## UI Architecture

### State Machine Pattern

- UI controllers use `og::ui::PickerState` to track menu state.
- UI produces `og::ui::Command` values and `og::ui::MenuViewModel` data.
- Renderer consumes view models; UI does not own renderer resources directly.
- `picker_cleanup_resources()` centralizes picker resource cleanup (no screen destruction).

## Constants and Types

- Prefer `inline constexpr` over `#define` for numeric constants.
- Prefer `enum class` over plain `enum` or `#define` for related constant sets.
- Key constants already converted: KEY_UP..KEY_CHEAT, MAX_BUTTONS, MAX_TEAM_SIZE,
  ButtonAction, Order, MenuResult, SaveDataIoError, EventKind.

## Build Presets

| Preset | Purpose |
|--------|---------|
| `ci-test` | Standard build + all tests |
| `ci-asan` | ASan + UBSan sanitizer build + all tests |
| `dev-debug` | Development debug build |
| `dev-release` | Development optimized build |

## CI Gates

The following checks run at build time:
1. `check_graph_h_includes` — no new graph.h includes
2. `check_vendor_leaks` — vendor headers stay in io boundary
3. `og_unit_tests` — headless sim/session tests (no SDL)
4. `openglad_test` — full integration test suite
5. `og_data_tests` — data/IO module tests
6. `og_runtime_tests` — runtime module tests
7. ASan/UBSan — at major milestones via ci-asan preset

## Adding New Code

1. Place public headers in `include/openglad/<module>/`.
2. Place implementation in `src/<module>/`.
3. Add source files to the appropriate `OG_*_SOURCES` list in CMakeLists.txt.
4. Respect module dependency rules above.
5. Write unit tests for pure logic; integration tests for SDL-dependent flows.
6. Run `cmake --build --preset ci-test && ctest --preset ci-test` before committing.
