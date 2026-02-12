# OpenGlad Modernization Opportunities Audit

Scope: project code (`src/`, `tests/`, `CMakeLists.txt`, `scripts/`, `cmake/`) on branch `cpp-modernization-plan`.

This report focuses on remaining modernization/refactoring opportunities after the existing modular CMake split, `graph.h` include guardrail, GameContext service interfaces, and sanitizer/baseline CI work.

## Priority Legend
- **High**: correctness/safety risk, crash/data corruption potential, or major architectural blocker.
- **Medium**: meaningful maintainability/testability improvements with moderate migration cost.
- **Low**: cleanup and consistency improvements with lower immediate impact.

## 1. Code Quality (ownership, globals, macros, manual memory)

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| CQ-01 | Unbounded team deserialization can write past fixed team array (`team_list[MAX_TEAM_SIZE]`). | `src/save_data.cpp:296` | Clamp/validate `listsize` before loop; reject malformed save when `listsize > MAX_TEAM_SIZE`; keep stream aligned for backward compatibility. | High | None |
| CQ-02 | Null-dereference risk: `create_walker` result is dereferenced without null check in FX/weapon paths. | `src/level_data.cpp:421`, `src/level_data.cpp:433` | Mirror `add_ob` guard: `if (!w) return nullptr;` before `w->myobmap = ...`; propagate load/save parse error. | High | None |
| CQ-03 | Raw owning pointers and manual deletes for picker assets/buttons (`backdrops`, `main_*_pix`, `localbuttons`, `allbuttons`). | `src/picker.cpp:227`, `src/picker.cpp:265`, `src/picker.cpp:286`, `src/picker.cpp:109`; `src/button.cpp:490` | Replace with `std::array<std::unique_ptr<...>>` and RAII container for active menu buttons; remove manual delete branches. | High | AH-01, API-02 |
| CQ-04 | `vbutton` callback dispatch uses integer macro IDs + giant switch, reducing type safety and discoverability. | `src/button.h:206`, `src/button.cpp:575` | Use `enum class ButtonAction` + `std::function<Sint32(Sint32)>`/callable table; keep serialized IDs separate if needed. | Medium | TS-01 |
| CQ-05 | Heavy global mutable state (`myscreen`, `cfg`, picker globals, input globals) still drives core flow. | `src/base.h:86`, `src/gparser.h:37`, `src/picker.cpp:105`, `src/input.cpp:55` | Continue migration toward `GameContext` ownership; convert globals to context-owned services/state structs. | High | AR-01, API-01 |
| CQ-06 | `read_one_line` heap-allocates per line with raw `new[]/delete[]`. | `src/help.cpp:53`, `src/help.cpp:365` | Replace with `std::string` or `std::array<char, HELP_WIDTH>` return by value. | Medium | MC-03 |
| CQ-07 | Debug `printf` remains in config parsing path. | `src/gparser.cpp:135` | Route through structured logger or remove in non-debug builds. | Low | EH-02 |

## 2. Modern C++ Adoption

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| MC-01 | Legacy `std::list` used broadly where random access or cache locality matters. | `src/level_data.h:119`, `src/screen.cpp:1240`, `src/io.cpp:313` | Prefer `std::vector` for traversal-heavy collections; keep list only where iterator stability is required. | Medium | AR-02 |
| MC-02 | C arrays and fixed char buffers dominate serialization paths. | `src/level_data.cpp:1063`, `src/save_data.cpp:104`, `src/io.cpp:1054` | Use `std::array<std::byte, N>`/`std::array<char, N>` + explicit serialization helpers, bounds-checked conversion utilities. | Medium | TS-02, EH-01 |
| MC-03 | Text input APIs return pointers to static buffers, not value types. | `src/text.cpp:448`, `src/text.cpp:592`, `src/text.h:61` | Return `std::optional<std::string>` (or struct with `accepted` flag + value). Remove shared static buffers. | Medium | API-04 |
| MC-04 | `rand()`/`srand()` usage still exists in gameplay and file generation. | `src/screen.cpp:96`, `src/glad.cpp:251`, `src/level_data.cpp:530`, `src/io.cpp:947` | Standardize on injected RNG (`IRandom` + `<random>` engines) for deterministic tests and uniform behavior. | Medium | AR-01 |
| MC-05 | Manual pointer arithmetic / C-style writes in rendering paths. | `src/sai2x.cpp:587`, `src/video.cpp:452` | Encapsulate pixel writes behind typed helpers (`std::span<std::uint32_t>`) to reduce aliasing/UB risk. | Medium | RM-02 |

## 3. Architecture (coupling, god objects, separation)

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| AR-01 | GameContext exists but code often bypasses it and accesses globals directly. | `src/glad.cpp:25`, `src/picker.cpp:109`, `src/input.cpp:55` | Complete inversion: modules take explicit service/state references; keep `ctx()` as transitional adapter only. | High | CQ-05 |
| AR-02 | Very large “god” translation units make ownership and behavior hard to evolve safely. | `src/level_editor.cpp` (~3945 LOC), `src/picker_team_build.cpp` (~2257), `src/view.cpp` (~2250), `src/walker.cpp` (~2084), `src/video.cpp` (~2021) | Split by responsibility (UI model, input handlers, rendering, persistence, rules) with narrow interfaces per module. | High | AH-02, API-01 |
| AR-03 | Emscripten picker path duplicates native picker initialization flow almost line-for-line. | `src/picker.cpp:208`, `src/picker.cpp:1430` | Extract shared setup/teardown functions reused by native and web state machines. | Medium | CQ-03 |
| AR-04 | `graph.h` remains an aggregate include point across many core files, sustaining transitive coupling. | `src/graph.h:19`, includes in 30+ files (e.g. `src/walker.cpp:23`, `src/video.cpp:19`, `src/game_loop.cpp:5`) | Continue replacing `graph.h` with minimal direct includes per TU; enforce with CI check expansion. | High | AH-01 |

## 4. Type Safety

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| TS-01 | Macro integer IDs used as pseudo-enums (keys/actions/buttons). | `src/input.h:126`, `src/button.h:206` | Replace with `enum class` and typed conversion helpers. | Medium | API-02 |
| TS-02 | Serialization fields use `char/short` for version/count/type; implicit signedness/size ambiguity. | `src/level_data.cpp:1266`, `src/level_data.h:105`, `src/save_data.cpp:283` | Use fixed-width integer types (`std::uint8_t`, `std::uint16_t`, etc.) and explicit endian-aware read/write helpers. | High | EH-01 |
| TS-03 | `list_find` helper dereferences iterator before end-check (UB). | `src/io.h:113` | Remove helper and use standard algorithms; if needed, fix condition order to `begin != end && *begin != value`. | High | None |
| TS-04 | Opaque pointer casting through `void*` for custom events weakens type guarantees. | `src/OuyaController.cpp:335`, `src/input.cpp:1359` | Use strongly typed payload structs allocated per event, or encode in `SDL_UserEvent` integer fields with validated enum conversion. | Medium | TH-03 |

## 5. Error Handling

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| EH-01 | Many low-level reads/writes ignore return counts (partial I/O can silently corrupt state). | `src/level_data.cpp:1280`, `src/graphlib.cpp:61`, multiple `SDL_RWwrite` in `src/level_data.cpp:1450+` | Standardize `read_exact/write_exact` wrappers everywhere; propagate `IoError` with context. | High | TS-02 |
| EH-02 | Mixed hard termination (`exit`) and recoverable paths creates unpredictable control flow. | `src/graphlib.cpp:58`, `src/sound.cpp:86`, `src/game.cpp:211`, `src/picker.cpp:654` | Replace process termination in library/gameplay code with typed error return (`expected`-style) up to top-level policy handler. | High | AR-01 |
| EH-03 | Iterator invalidation bug in campaign listing logic can skip entries / invoke UB on increment after erase. | `src/io.cpp:316` | Rewrite loop as `for (auto it = ls.begin(); it != ls.end(); )` with erase-continue pattern. | High | None |
| EH-04 | `goto`-based cleanup in title loading is correct but inconsistent with newer RAII style in repo. | `src/screen.cpp:1104` | Replace with scoped RAII wrapper for `SDL_RWops*` and direct early returns. | Low | RM-01 |

## 6. Resource Management (files/SDL/OpenGL/RAII)

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| RM-01 | `Screen` destructor does not destroy SDL window (`SDL_DestroyWindow` commented out). | `src/sai2x.cpp:745` | Reinstate explicit window destruction (or RAII deleter wrappers for window/renderer/texture/surface). | High | None |
| RM-02 | Global screen pointer ownership (`E_Screen`) and manual lifetime in `video`. | `src/video.cpp:114`, `src/video.cpp:120`, `src/sai2x.h:45` | Convert to `std::unique_ptr<Screen>` owned by `video`, remove global external pointer dependency. | High | AR-01 |
| RM-03 | Known intentional global font leak remains unresolved. | `src/text.cpp:49` | Convert static `PixieData` resources to process-lifetime RAII singleton with explicit shutdown or `std::shared_ptr` cache. | Medium | MC-03 |
| RM-04 | Widespread manual `SDL_RWclose` patterns increase leak/early-return risk. | `src/io.cpp`, `src/level_data.cpp`, `src/save_data.cpp`, `src/view.cpp` | Introduce `unique_ptr<SDL_RWops, SDL_RWclose_deleter>` wrappers and standard helpers. | Medium | EH-01 |

## 7. Threading / Concurrency

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| TH-01 | Trace buffer is unsynchronized; tests create injector threads that can race `push_back/clear/iterate`. | `src/test_trace.h:28`, `src/test_trace.cpp:8` | Guard trace buffer with mutex or lock-free ring buffer; expose thread-safe API. | High | None |
| TH-02 | Mixed atomic/non-atomic test state (`g_test_game_epoch` atomic but `g_test_in_game` plain bool). | `src/picker.cpp:118`, `src/picker.cpp:121` | Make both atomic or move to a mutex-protected state struct. | Medium | None |
| TH-03 | Global `allbuttons` locking only occurs in `init_buttons`; readers/mutators elsewhere are unlocked. | `src/button.cpp:485`, read paths `src/button.cpp:328`, `src/picker_input.cpp:51` | Centralize menu state ownership on main thread or consistently guard all accesses with the same lock. | Medium | CQ-03 |
| TH-04 | Emscripten sync gate uses `volatile` + polling sleep; not a robust synchronization primitive. | `src/io.cpp:495`, `src/io.cpp:536` | Use atomics / callback-based continuation or an async init state machine without busy waiting. | Medium | AR-03 |

## 8. Build System / Toolchain

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| BS-01 | Web build script uses `-std=c++11`, diverging from CMake C++20 baseline. | `scripts/build_web.sh:97`, `CMakeLists.txt:6` | Build web target through CMake only; enforce one language standard across targets. | Medium | None |
| BS-02 | `scripts/build_web.sh` duplicates source lists/flags from CMake, creating drift risk. | `scripts/build_web.sh` (source lists and defines) | Remove duplicated script pipeline or make it thin wrapper around CMake preset. | Medium | BS-01 |
| BS-03 | Module test binaries are built but not registered in CTest. | `CMakeLists.txt:743`, `CMakeLists.txt:753`, only `add_test` at `CMakeLists.txt:764` | Add `add_test` entries for `og_data_tests` and `og_runtime_tests`; include in CI matrix. | Medium | TG-01 |
| BS-04 | Coverage script excludes a major module (`level_editor.cpp`), masking modernization risk there. | `scripts/build_coverage.sh:59` | Replace hard exclusion with separate coverage target and explicit allowlist/justification gating. | Medium | TG-02 |
| BS-05 | Static analysis tools not yet integrated as first-class targets. | `CMakeLists.txt` (no clang-tidy/cppcheck hooks) | Add optional `ENABLE_CLANG_TIDY`/`ENABLE_CPPCHECK` and CI job for modernization debt tracking. | Low | AH-01 |

## 9. Testing Gaps / Testability

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| TG-01 | CTest executes only aggregate test binary by default; module binaries are not part of standard test execution. | `CMakeLists.txt:764` | Register and run `og_data_tests`/`og_runtime_tests` in local and CI test workflows. | Medium | BS-03 |
| TG-02 | Coverage reporting intentionally omits level editor, reducing confidence in one of the largest modules. | `scripts/build_coverage.sh:59` | Reintroduce editor coverage with dedicated smoke/integration subset and quarantined flaky tests if needed. | Medium | BS-04, AR-02 |
| TG-03 | Platform-specific input/controller paths remain weakly isolated (OUYA/custom SDL user events). | `src/OuyaController.cpp`, `src/input.cpp:1359` | Add focused unit tests around event payload translation and failure cases; abstract device adapters for host-only tests. | Medium | TS-04 |

## 10. Dead Code / Legacy Artifacts

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| DC-01 | `#if 0` blocks and commented-out legacy branches remain in runtime paths. | `src/picker.cpp:292`, `src/glad.cpp:679` | Remove obsolete blocks or convert to documented feature flags. | Low | None |
| DC-02 | Unused parameters indicate stale APIs (`atstart`, `myscreen`, `cache_weapons`). | `src/level_data.cpp:401`, `src/gloader.cpp:792`, `src/gloader.cpp:833` | Remove or implement parameter behavior; simplify call signatures. | Medium | API-03 |
| DC-03 | Legacy compatibility helpers/comments no longer true (“std::find broken”) and can hide defects. | `src/io.h:109` | Delete outdated workaround and rely on standard library. | Medium | TS-03 |

## 11. Header Hygiene / Include-What-You-Use

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| AH-01 | Circular/heavy include chain: `base.h -> input.h -> video.h -> base.h`. | `src/base.h:35`, `src/input.h:28`, `src/video.h:21` | Break cycles via forward declarations and narrower headers (`types`, `constants`, `interfaces`). | High | AR-04 |
| AH-02 | `graph.h` transitively includes nearly all gameplay/render types; most files include it directly. | `src/graph.h:19` | Continue replacing with direct includes + IWYU checks; keep allowlist shrinking per phase. | High | AR-04 |
| AH-03 | `screen.h` includes many heavy headers it does not need in public interface. | `src/screen.h:21-34` | Forward-declare where possible; move implementation includes to `screen.cpp`. | Medium | AH-01 |

## 12. API Design / Ownership Semantics

| ID | Issue | Where | Modernized version | Priority | Dependencies |
|---|---|---|---|---|---|
| API-01 | Ownership semantics are often implicit: raw pointer returns may be owning (`create_walker`) or non-owning (`find_*`). | `src/gloader.h:34`, `src/screen.h:73` | Adopt explicit ownership vocabulary (`unique_ptr` for owning factories, `observer_ptr`/references for borrowed). | High | CQ-05 |
| API-02 | Menu action APIs are integer-based and string/ID-heavy, causing brittle coupling between UI definitions and execution. | `src/button.h:206`, `src/button.cpp:575` | Replace with typed action enums + command objects; isolate menu view model from action executor. | Medium | TS-01 |
| API-03 | Serialization APIs expose dual bool/error-code interfaces inconsistently (`save()` + `save_with_error()`), leading to ignored diagnostics. | `src/level_data.h:137`, `src/save_data.h:74`, `src/io.h:57` | Collapse onto one result type (`expected<void, IoError>` style), with logging at call boundaries only. | Medium | EH-01 |
| API-04 | Inconsistent naming and style in adjacent APIs (`getDescriptionLine` vs `get_description_line`). | `src/level_data.h:80`, `src/level_data.h:157` | Standardize naming convention (snake_case or camelCase) and provide compatibility shims during migration. | Low | None |

---

## High-Priority Cross-Cutting Risks (Recommended first)

1. Bounds/null correctness in deserialization/object creation (`CQ-01`, `CQ-02`, `EH-03`, `TS-03`).
2. Replace abrupt process termination paths with structured errors (`EH-02`).
3. Eliminate major lifetime leaks and global ownership ambiguity (`RM-01`, `RM-02`, `CQ-03`).
4. Reduce architectural coupling and header cycles (`AR-01`, `AR-04`, `AH-01`).
5. Stabilize multithreaded test instrumentation (`TH-01`, `TH-03`).

---

## Suggested Phased Roadmap

### Phase 1: Correctness/Safety Hardening
- Implement: `CQ-01`, `CQ-02`, `EH-03`, `TS-03`, `EH-01` (critical file I/O paths first).
- Add regression tests for malformed save/scenario files and null factory returns.

### Phase 2: Error/Lifetime Foundations
- Implement: `EH-02`, `RM-01`, `RM-02`, `RM-04`.
- Introduce RAII wrappers for SDL resources and RWops.

### Phase 3: Decouple Runtime State
- Implement: `AR-01`, `CQ-05`, `API-01`, `API-02`, `AR-03`.
- Consolidate picker/menu state into explicit objects; remove global menu/button state.

### Phase 4: Type/API Cleanup
- Implement: `TS-01`, `TS-02`, `MC-02`, `API-03`, `API-04`, `DC-02`.
- Replace macro ID spaces with typed enums and conversion boundaries.

### Phase 5: Header & Build Hygiene
- Implement: `AH-01`, `AH-02`, `AH-03`, `BS-01`, `BS-02`, `BS-05`.
- Shrink `graph.h` footprint and enforce include discipline in CI.

### Phase 6: Testing and Coverage Closure
- Implement: `BS-03`, `BS-04`, `TG-01`, `TG-02`, `TG-03`, `TH-01`, `TH-02`, `TH-04`.
- Ensure module binaries run in CI and coverage includes editor pathways.

### Phase 7: Opportunistic Modern C++ Optimizations
- Implement: `MC-01`, `MC-03`, `MC-04`, `MC-05`, `CQ-06`, `CQ-07`, `DC-01`, `DC-03`.
- Prioritize low-risk replacements and consistency cleanups after safety/architecture phases.

---

## Notes
- Existing modernization groundwork is strong (modular targets, GameContext seams, sanitizer support).
- The main remaining risk concentration is legacy global state + manual ownership in menu/input/render subsystems.
- Most “modern C++” gains should be staged after safety fixes to avoid compounding migration risk.
