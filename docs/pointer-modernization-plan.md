# Pointer Modernization Plan (src/ only)

Branch: `cpp-modernization-plan`  
Scope: `src/**` excluding `src/external/**`  
Goal: Reduce memory-safety risk and simplify ownership by moving owning raw pointers to RAII (`std::unique_ptr`, value semantics), tightening non-owning pointer usage (`T*`/`std::span`/references), and migrating C-string APIs to `std::string`/`std::string_view` where it improves safety.

This document is based on a fresh scan of the current tree. The codebase already has meaningful C++ modernization (notably pervasive `std::unique_ptr` in game object containers and `PixieData`).

## 1. Executive Summary

### 1.1 Baseline Scan Totals (regex-based)

Counts below are intended for planning and prioritization. They are not perfect (some patterns match declarations or commented-out code), but they highlight hotspots.

- `operator new` (likely allocations): 22 lines
  - Primary sites: `src/ui/picker.cpp`, `src/ui/intro.cpp`, `src/ui/picker_team_build.cpp`, `src/input/button.cpp`, `src/data/gloader.cpp`, `src/entities/guy.cpp`, `src/glad.cpp`
- `operator delete` (delete statements): 25 lines
  - Primary sites: `src/ui/picker.cpp`, `src/ui/intro.cpp`, `src/ui/picker_team_build.cpp`, `src/input/button.cpp`, `src/ui/results_screen.cpp`, `src/entities/guy.cpp`
- `new[]` / `delete[]`: no active code usage found
  - One commented-out `new[]` line: `src/render/view.cpp:180`
- `malloc()` / `free()` (C allocator): no usage found
  - `.free()` *methods* exist and are called (e.g., `PixieData::free()`), but those are RAII resets, not C `free()`.
- `char*` / `const char*`: 302 occurrences
  - Mostly filenames, UI strings, SDL APIs, and legacy interfaces declared in `src/base.h`
- `unsigned char*`: 41 occurrences
  - Typically pixel buffers and image data (`pixie`, `pixieN`, software render paths)
- `void*`: 22 occurrences
  - Largely SDL callback/data plumbing and `micropather` API requirements
- `T**` (double pointers): 26 occurrences
  - Includes `char** argv`, PhysFS file lists (`char**`), and animation tables (`signed char**`)
- C-string function usage (e.g., `strlen`, `strcmp`, `strcpy`, `sprintf`): 40 occurrences
  - Most are `strlen`; `strcpy` occurrences are currently in commented-out code

### 1.2 Ownership State Today (high-level)

What is already in good shape:

- `PixieData` is move-only and owns pixel data via `std::unique_ptr<unsigned char[]>` (`src/data/pixie_data.h`).
- Game object collections in `LevelData` are already `std::list<std::unique_ptr<walker>>` (`src/data/level_data.h`).
- `loader` already has an owned factory for walkers: `create_walker_owned()` returning `std::unique_ptr<walker>` (`src/data/gloader.h`, `src/data/gloader.cpp`).

Remaining ownership debt clusters around:

- Menu/UI lifetime management (`backdrops[]`, `allbuttons[]`, `myscreen` global).
- Factories returning owning raw pointers (notably `loader::create_pixieN()`).
- Short-lived heap objects that should be stack objects (`guy::query_heart_value()`, intro pixies).

## 2. Risk Assessment

### 2.1 Low-Risk Changes (recommended early)

- Replace short-lived `new`/`delete` with stack allocation when the object never escapes scope.
  - Example: `src/entities/guy.cpp` uses `new guy(...)` purely for comparison.
- Replace “create + delete” patterns with existing owned factories (`create_walker_owned()`).
  - This is mechanical and local when the pointer does not escape.
- Convert owning raw pointers stored in fixed-size globals to `std::unique_ptr` containers while preserving non-owning access via `.get()`.
  - Example: `backdrops[5]`, `main_title_logo_pix`, `main_columns_pix`.

### 2.2 Medium-Risk Changes (requires careful coordination)

- Converting global ownership like `screen* myscreen` to `std::unique_ptr<screen>`.
  - Many call sites treat `myscreen` as “always valid”.
  - Migration must avoid accidental `nullptr` dereferences during teardown transitions.
- Migrating `vbutton` internals from raw pointers to `std::unique_ptr`.
  - Touches UI code paths broadly (menu creation, redraw, teardown).

### 2.3 High-Risk Changes (defer until after Phase 1-2 are stable)

- Signature changes in widely-included headers like `src/base.h` (switching `char*`/`const char*` to `std::string_view`/`std::string`).
  - These changes cascade through most modules and can be noisy.
- Changing pointer-heavy legacy animation types (`signed char**`) to safer containers.
  - Needs a clear model: static data vs owned data, plus performance constraints.

## 3. Prioritized Phases

### Phase 1: Memory Safety Bugs (UAF/double-free/leaks)

Focus: eliminate the most failure-prone patterns first (manual deletes, ownership ambiguity, early-return leaks).

**Work items**

- `src/entities/guy.cpp`
  - Change: Replace heap allocation in `guy::query_heart_value()` (`new guy(...)` + `delete`) with a stack `guy normal(family);`.
  - Complexity: S
  - Dependencies: none
  - Expected impact: removes unnecessary heap traffic and eliminates leak risk on early returns.

- `src/ui/picker_team_build.cpp`
  - Change: Convert `current_guy` from owning raw pointer to `std::unique_ptr<guy>`.
    - Replace `delete current_guy; current_guy=nullptr;` with `current_guy.reset();`.
    - Replace `current_guy = new guy(...)` with `current_guy = std::make_unique<guy>(...);`.
    - Where `current_guy` is transferred into `team_list[i]`, move ownership: `ourteam[i] = std::move(current_guy);`.
  - Complexity: M
  - Dependencies: Phase 2 work in `SaveData` call sites is minimal because `team_list` is already `std::unique_ptr<guy>`.
  - Risk notes:
    - This area has many branches where `current_guy` is deleted/reset; consolidating ownership prevents accidental double-free and makes control flow easier to reason about.

- `src/ui/picker.cpp`
  - Change: Convert menu resource teardown to RAII, so `picker_quit()` becomes mostly `reset()`/container clears and cannot double-delete.
  - Complexity: M
  - Dependencies: Phase 2 “backdrops”/“myscreen” ownership work (below).

**Phase 1 exit criteria**

- No `delete` of `current_guy` remains (replaced by `unique_ptr` resets/moves).
- `guy::query_heart_value()` contains no heap allocation.
- AddressSanitizer run (see Testing Strategy) shows no leaks/UAF in menu flows.

---

### Phase 2: Owning Pointers With Single Ownership -> `std::unique_ptr`

Focus: turn remaining “obvious owners” into RAII owners, and make factories return owning types.

**Work items**

- `src/data/gloader.h`, `src/data/gloader.cpp`
  - Change: Add `create_pixieN_owned()` returning `std::unique_ptr<pixieN>` (mirroring `create_walker_owned()`), and reimplement `create_pixieN()` as `.release()` for legacy call sites.
  - Complexity: S
  - Dependencies: none
  - Follow-up: migrate call sites to the owned API and delete the raw API once unused.

- `src/input/button.cpp`, `src/input/button.h`
  - Change: Make `vbutton` own `mypixie` via `std::unique_ptr<pixieN>`.
    - Update `set_graphic()` to assign from `create_pixieN_owned()`.
    - Remove explicit `delete mypixie;` and rely on destruction/reset.
  - Complexity: M
  - Dependencies: `create_pixieN_owned()` (above)

- `src/ui/picker.cpp`, `src/input/button.cpp`
  - Change: Convert `backdrops[5]` from `pixieN*` to `std::array<std::unique_ptr<pixieN>, 5>`.
    - Expose non-owning access where needed via `.get()`.
  - Complexity: M
  - Dependencies: `create_pixieN_owned()` (optional, since picker currently constructs `pixieN` directly)

- `src/glad.cpp`, `src/base.h`, `src/ui/picker.cpp`
  - Change: Convert global `screen *myscreen` to `std::unique_ptr<screen> myscreen`.
    - Allocation: `myscreen = std::make_unique<screen>(1);` (`src/glad.cpp`)
    - Teardown: `myscreen.reset();` (`src/ui/picker.cpp`)
    - Call sites: use `myscreen.get()` only when a `screen*` is required by signature.
  - Complexity: L (touches many compilation units via `base.h`)
  - Dependencies: coordinate with Phase 1 `picker_*` changes to avoid transitional `nullptr` hazards.

- Replace local heap walkers with owned factory usage:
  - `src/ui/picker_team_build.cpp` (function `show_guy(...)`)
  - `src/ui/results_screen.cpp` (function `show_guy(...)`)
  - Change: `auto mywalker = myscreen->level_data.myloader->create_walker_owned(...);` and delete the corresponding `delete mywalker;`.
  - Complexity: S
  - Dependencies: none (owned factory already exists)

- `src/ui/intro.cpp`
  - Change: Replace `pixie*` locals allocated via `new` with stack objects:
    - `pixie gladiator(gladdata);` etc.
  - Complexity: S
  - Dependencies: none
  - Notes: `pixie` is non-copyable/non-movable, which is compatible with stack allocation.

**Phase 2 exit criteria**

- No call sites of `loader::create_walker()` remain in code that immediately `delete`s the result.
- `vbutton` has no `delete` operations.
- UI/global resources (`myscreen`, `backdrops`) have a single clear owner.

---

### Phase 3: Shared Ownership -> `std::shared_ptr`

Current status: there is no `std::shared_ptr` usage in `src/` today, and most core structures are already cleanly single-owned with `std::unique_ptr`.

Policy for this codebase:

- Prefer `std::unique_ptr` + non-owning `T*`/`T&`/`std::span` for access.
- Introduce `std::shared_ptr` only when:
  - An object’s lifetime must extend beyond all obvious owners (e.g., asynchronous operations, callbacks surviving beyond scope), and
  - A clear “one owner” model is impossible or significantly more complex.

Potential candidates to evaluate (if/when they appear):

- Long-lived UI resources referenced from multiple systems with uncertain teardown ordering.
- Cross-thread resource lifetimes (none observed in current tree).

**Phase 3 exit criteria**

- Documented lifetime model for any new shared ownership use-case.
- If `shared_ptr` is introduced, enforce `weak_ptr` for back-references to avoid cycles.

---

### Phase 4: Raw Arrays -> `std::vector` / `std::array` / `std::span`

Focus: eliminate pointer+length pairs and raw fixed-size arrays where they obscure bounds, and prefer explicit-size types.

**Work items**

- `src/render/video.h`, `src/render/video.cpp`
  - Change: Replace `unsigned char* video::getbuffer()` with a bounds-aware API:
    - Option A: `std::span<unsigned char> getbuffer();`
    - Option B: `std::array<unsigned char, 64000>& buffer();`
  - Complexity: M
  - Dependencies: update all call sites.

- `src/render/pixie.h`, `src/render/pixie.cpp`, `src/render/pixien.h`, `src/render/pixien.cpp`
  - Change: Treat pixel data as `std::span<const unsigned char>` rather than naked pointers where possible.
    - Keep raw pointers only where required by SDL (`SDL_Surface*`) or performance-critical tight loops, but isolate them.
  - Complexity: M
  - Dependencies: ensure `PixieData` outlives any `pixieN` referencing `data.data.get()`.

- `src/ui/intro.cpp`
  - Change: Convert local fixed-size arrays to `std::array`:
    - `unsigned char mypalette[768]` -> `std::array<unsigned char, 768>`
    - `int pal[256][3]` -> `std::array<std::array<int, 3>, 256>`
  - Complexity: S
  - Dependencies: none

- `src/render/sai2x.cpp`
  - Change: Convert `static unsigned char *src_line[4]` / `dst_line[2]` to `std::array<unsigned char*, N>` (or `std::array<std::span<...>, N>` where feasible).
  - Complexity: S
  - Dependencies: none

**Phase 4 exit criteria**

- Buffer-returning APIs expose bounds (span/array/vector).
- Raw stack arrays are limited to trivial local scratch space and are replaced when they represent stable structured data.

---

### Phase 5: C Strings -> `std::string` / `std::string_view`

Focus: reduce null-termination hazards and accidental buffer overflows; clarify ownership of returned strings.

**Work items**

- `src/base.h` (and corresponding definitions)
  - Change: Migrate public APIs that consume strings to `std::string_view` where ownership is not transferred.
    - Examples:
      - `short init_sound(char *filename, ...)` -> `short init_sound(std::string_view filename, ...)`
      - `const char* get_scen_title(const char *filename, ...)` -> `std::string get_scen_title(std::string_view filename, ...)` (or `std::optional<std::string>`)
  - Complexity: L
  - Dependencies: broad call site updates, and careful review of any code expecting mutable buffers.

- `src/render/text.*`, `src/ui/picker_team_build.cpp`
  - Change: Replace APIs returning `char*` (e.g., `text::input_string(...)`) with `std::optional<std::string>` or `std::string`.
    - Example call site: `src/ui/picker_team_build.cpp:1900`
  - Complexity: M
  - Dependencies: adjust UI flows that currently treat returned `char*` as a borrowed buffer.

- `src/ui/picker_team_build.cpp`
  - Change: Replace `get_saved_name(...) -> const char*` (with static buffers) with `std::string`.
  - Complexity: M
  - Dependencies: update any UI text routines expecting `const char*` (most already accept `std::string` via `.c_str()`).

**Phase 5 exit criteria**

- No APIs return writable `char*` for UI input.
- Static buffers used as return values are removed or strongly documented and wrapped.

---

### Phase 6: Cosmetic / Style / Non-Owning Pointer Hygiene

Focus: make remaining raw pointers clearly non-owning and reduce ambiguity.

**Work items**

- Prefer references for “required” parameters (`T&`) instead of nullable pointers (`T*`) where call sites always provide a valid object.
  - Example candidates:
    - Many `screen*` parameters that are never null in practice.
- Add `[[nodiscard]]` on factory functions returning owning types to prevent accidental leaks.
  - Example: `create_walker_owned()`, `create_pixieN_owned()`.
- Tighten const-correctness for shared static data tables:
  - Convert `signed char**` animation tables to `const signed char* const*` (or `std::span`) if they are truly immutable.
- Keep `void*` only at FFI boundaries; wrap where possible:
  - `micropather` API requires `void*` (`src/entities/walker_pathing.cpp`, `src/entities/walker.h`); document and isolate conversions.

**Phase 6 exit criteria**

- Remaining raw pointers are non-owning by convention and ideally by type (references/spans).
- Factories clearly encode ownership in return types.

## 4. Per-Phase File Checklist (Quick Index)

Phase 1 (bugs)

- `src/entities/guy.cpp` (stack-allocate `normal`)
- `src/ui/picker_team_build.cpp` (`current_guy` -> `unique_ptr`)
- `src/ui/picker.cpp` (RAII teardown patterns)

Phase 2 (unique ownership)

- `src/data/gloader.h`, `src/data/gloader.cpp` (`create_pixieN_owned`)
- `src/input/button.h`, `src/input/button.cpp` (`vbutton::mypixie` -> `unique_ptr`, `allbuttons` ownership)
- `src/ui/picker.cpp` (`backdrops` -> `array<unique_ptr<...>>`, `myscreen` teardown)
- `src/glad.cpp`, `src/base.h` (`myscreen` -> `unique_ptr`)
- `src/ui/results_screen.cpp`, `src/ui/picker_team_build.cpp` (use `create_walker_owned`)
- `src/ui/intro.cpp` (stack pixies)

Phase 4 (arrays/spans)

- `src/render/video.h`, `src/render/video.cpp` (`getbuffer()` -> span/array ref)
- `src/render/pixie.*`, `src/render/pixien.*` (prefer spans for pixel data views)
- `src/ui/intro.cpp` (palette arrays -> `std::array`)
- `src/render/sai2x.cpp` (pointer arrays -> `std::array`)

Phase 5 (strings)

- `src/base.h` + implementations (signature migration)
- `src/render/text.*` + call sites (`input_string` returns)
- `src/ui/picker_team_build.cpp` (`get_saved_name` return type)

## 5. Testing Strategy

Modernizing ownership is a behavioral change even when logic remains identical. The primary risk is lifetime/teardown ordering and implicit aliasing assumptions.

### 5.1 Build Modes

- Debug + sanitizers:
  - `-fsanitize=address,undefined` (ASan/UBSan)
  - `-fno-omit-frame-pointer` for usable stack traces
- Release build smoke tests to catch performance regressions in render loops after span/vector changes.

### 5.2 Scenario Coverage (manual + scripted where possible)

Menu/UI lifetime (covers most remaining `new`/`delete` sites):

1. Start app -> enter main menu -> exit via normal quit path (ensures `picker_quit()` teardown is safe).
2. Start new game -> team build -> cycle guy types repeatedly -> rename -> hire -> delete all -> repeat (stress `current_guy`, UI buttons, and pixie lifetimes).
3. Load/save flows (covers `get_saved_name`, RWops usage).

Gameplay loop coverage:

1. Start a scenario -> finish -> show results screen (covers `results_screen.cpp` walker creation).
2. Re-enter menu and start another scenario (teardown order stress).

### 5.3 Instrumentation

- ASan leak checks after repeated menu/game cycles.
- Add lightweight runtime assertions during migration:
  - e.g., `assert(myscreen != nullptr)` at key entry points, removed after Phase 2 stabilizes.
- Prefer targeted tests around factories:
  - Ensure `create_pixieN_owned()` returns non-null when old `create_pixieN()` did, and that destructor paths free SDL surfaces correctly.

### 5.4 Tooling

- `clang-tidy` (run incrementally per phase):
  - `cppcoreguidelines-owning-memory`
  - `modernize-make-unique`
  - `modernize-use-nullptr`
  - `bugprone-use-after-move` (once `unique_ptr` moves become common)

## Appendix A: Primary Hotspot Files

- Global ownership / UI teardown: `src/ui/picker.cpp`, `src/glad.cpp`, `src/base.h`
- UI object lifetimes: `src/input/button.cpp`, `src/ui/picker_team_build.cpp`
- Factories returning owning pointers: `src/data/gloader.cpp`, `src/data/gloader.h`
- Temporary heap objects: `src/entities/guy.cpp`, `src/ui/intro.cpp`, `src/ui/results_screen.cpp`

