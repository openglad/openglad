# OpenGlad C++ Modernization Plan

> **Target:** C++23 where supported, C++20 as baseline
> **Scope:** All primary C++ source files compiled by `scripts/build_native.sh` in `src/` (excluding `src/external/` and test-only/auxiliary sources)
> **Approach:** Phased, from safe mechanical changes to architectural refactoring
> **Guiding Principle:** Each phase should leave the codebase compiling and functional

---

## Table of Contents

0. [Phase 0: Toolchain Requirements](#phase-0-toolchain-requirements)
1. [Phase 1: Build System Modernization](#phase-1-build-system-modernization)
2. [Phase 2: Mechanical Low-Hanging Fruit](#phase-2-mechanical-low-hanging-fruit)
3. [Phase 3: Modern Types & Type Safety](#phase-3-modern-types--type-safety)
4. [Phase 4: Memory Management (Smart Pointers & RAII)](#phase-4-memory-management-smart-pointers--raii)
5. [Phase 5: Modern Idioms (Loops, Algorithms, Lambdas)](#phase-5-modern-idioms-loops-algorithms-lambdas)
6. [Phase 6: Error Handling Modernization](#phase-6-error-handling-modernization)
7. [Phase 7: Class Design & Encapsulation](#phase-7-class-design--encapsulation)
8. [Phase 8: Containers & Algorithms](#phase-8-containers--algorithms)
9. [Phase 9: Concurrency & Threading (If Applicable)](#phase-9-concurrency--threading)
10. [Appendix: File Impact Matrix](#appendix-file-impact-matrix)

---

## Phase 0: Toolchain Requirements

**Risk: NONE** | **Effort: LOW** | **Priority: PREREQUISITE**

Before any modernization work begins, verify that all target toolchains support the C++20/23 features this plan depends on.

### 0.1 Minimum Compiler Versions

| Toolchain | Minimum Version | Notes |
|-----------|----------------|-------|
| GCC | 14+ | Full C++23 `std::expected`, `std::format`, `<print>` |
| Clang | 17+ | C++23 `std::expected`; `std::format` requires libc++ 17+ |
| Emscripten (emsdk) | 3.1.44+ | Uses Clang/LLVM internally; verify SDL2 port compatibility |

### 0.2 Emscripten Feature Verification

Emscripten's C++ standard library support lags behind native compilers. Before committing to any C++20/23 feature, verify it works in the web build:

**Must verify before proceeding:**
- `std::format` — may require `-sUSE_LIBCXX=1` or a polyfill (`fmt` library)
- `std::expected` (C++23) — check Emscripten release notes for support status
- `std::span` (C++20) — generally available but confirm with emsdk version
- `constexpr` `std::array` / `std::string` — verify no link-time issues
- `std::erase_if` (C++20) — should be available but confirm

**Verification step:** Create a small test file that uses each planned feature, compile with `em++ -std=c++23`, and confirm it compiles and runs in the browser before starting Phase 1.

### 0.3 CI Matrix

**Existing CI:** A GitHub Actions pipeline (`.github/workflows/test.yml`) already runs on every push and pull request. It has two jobs:
1. **Run Tests** — builds the test binary via `scripts/build_test.sh` (compiles with `-DTESTING`) and runs all 25+ registered tests headless (`SDL_VIDEODRIVER=offscreen`)
2. **Native Build** — builds `openglad` and `openscen` via `scripts/build_native.sh` to verify compilation

The current CI uses **GCC with C++11** on `ubuntu-latest`. For modernization, expand the CI matrix to also test against:
- GCC 14 with `-std=c++23` (native Linux)
- Clang 17 with `-std=c++23` (native Linux/macOS)
- emsdk latest (web build)

Each phase should pass the existing CI pipeline before merging. Adding a C++23 CI target in Phase 1 is a prerequisite for later phases.

---

## Phase 1: Build System Modernization

**Risk: LOW** | **Effort: MEDIUM** | **Priority: DO FIRST**

The project currently uses three custom bash scripts (`scripts/build_native.sh`, `scripts/build_web.sh`, `scripts/build_test.sh`) that compile directly without a proper build system. All target **C++11** (`-std=c++11`).

### 1.1 Create CMakeLists.txt

Replace the bash scripts with a proper CMake build system that supports both native and Emscripten targets.

**Create:** `CMakeLists.txt` (project root)

```cmake
cmake_minimum_required(VERSION 3.25)
project(OpenGlad VERSION 1.0 LANGUAGES C CXX)

# Target C++23 with C++20 fallback
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fall back to C++20 if C++23 not available
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++23" HAS_CXX23)
if(NOT HAS_CXX23)
    set(CMAKE_CXX_STANDARD 20)
endif()
```

### 1.2 Modernize Compiler Flags

**Current flags** (`scripts/build_native.sh:44-69`):
```
-O2 -Wno-parentheses -std=c++11
```

**New flags:**
```cmake
# Warnings
target_compile_options(openglad PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-parentheses            # Keep for now (existing code relies on this)
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Wcast-align -Woverloaded-virtual
    -Wconversion -Wsign-conversion
    -Wnull-dereference -Wformat=2
)

# Sanitizers (Debug builds)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(openglad PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer
    )
    target_link_options(openglad PRIVATE
        -fsanitize=address,undefined
    )
endif()
```

### 1.3 Emscripten Toolchain File

**Create:** `cmake/emscripten.cmake`

Encapsulate the Emscripten-specific flags currently in `scripts/build_web.sh:68-97`:
- `-sUSE_SDL=2 -sUSE_SDL_MIXER=2`
- `-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864`
- `-sASYNCIFY -sASYNCIFY_STACK_SIZE=65536`
- `--preload-file` directives for game assets

### 1.4 Keep Build Scripts as Wrappers

Keep `scripts/build_native.sh` and `scripts/build_web.sh` but convert them to thin wrappers that invoke CMake, preserving backward compatibility for existing workflows.

### 1.5 Test Build Target

The project has an existing test suite (25+ tests in `tests/`) built via `scripts/build_test.sh`. The CMake configuration must include a test target:

```cmake
# Test build
option(BUILD_TESTING "Build the test suite" OFF)
if(BUILD_TESTING)
    # Same game sources, compiled with -DTESTING
    add_executable(openglad_test
        ${GAME_SOURCES}
        ${EXTERNAL_SOURCES}
        tests/test_main.cpp
        tests/test_framework.cpp
        tests/test_trace.cpp
        tests/test_startup.cpp
        tests/test_guy.cpp
        # ... all test source files from tests/
    )
    target_compile_definitions(openglad_test PRIVATE TESTING)
    target_link_libraries(openglad_test PRIVATE SDL2 SDL2_mixer pthread)
endif()
```

**Key details:**
- Tests compile the same game sources with `-DTESTING` (enables trace instrumentation, disables `exit()` in `quit()`, excludes `main()` from `glad.cpp`)
- Test source files are listed in `TEST_SOURCES` in `scripts/build_test.sh` — the CMake target must mirror this list
- The test binary runs headless via `SDL_VIDEODRIVER=offscreen` and `SDL_AUDIODRIVER=dummy`
- Keep `scripts/build_test.sh` as a wrapper (same as 1.4) for backward compatibility with existing CI (`.github/workflows/test.yml`)

### 1.6 Update CI for C++23

Update `.github/workflows/test.yml` to add a C++23 build matrix entry alongside the existing C++11 build. This allows incremental migration — the C++11 job continues to pass until all code is modernized, while the C++23 job validates new code.

### 1.7 Files to Create/Modify

| File | Action |
|------|--------|
| `CMakeLists.txt` | CREATE - Main build configuration (native, web, test targets) |
| `cmake/emscripten.cmake` | CREATE - Emscripten toolchain file |
| `scripts/build_native.sh` | MODIFY - Wrap CMake native build |
| `scripts/build_web.sh` | MODIFY - Wrap CMake + Emscripten |
| `scripts/build_test.sh` | MODIFY - Wrap CMake test build |
| `.github/workflows/test.yml` | MODIFY - Add C++23 matrix entry |

---

## Phase 2: Mechanical Low-Hanging Fruit

**Risk: VERY LOW** | **Effort: LOW-MEDIUM** | **Priority: DO SECOND**

These are safe, mechanical transformations that can be automated with find-and-replace and do not change program behavior.

### 2.1 Replace `NULL` with `nullptr`

**Scope: 389 occurrences across 36 files**

Top files by count:

| File | Count |
|------|-------|
| `walker.cpp` | 62 |
| `io.cpp` | 31 |
| `screen.cpp` | 29 |
| `level_editor.cpp` | 29 |
| `results_screen.cpp` | 25 |
| `picker.cpp` | 23 |
| `level_data.cpp` | 22 |
| `button.cpp` | 20 |
| `sai2x.cpp` | 14 |
| `view.cpp` | 12 |
| *(26 more files)* | *(remaining)* |

**Before:**
```cpp
// walker.cpp:49
stats = new statistics(this);
collide_ob = NULL;
foe = NULL;
leader = NULL;
owner = NULL;
```

**After:**
```cpp
stats = new statistics(this);
collide_ob = nullptr;
foe = nullptr;
leader = nullptr;
owner = nullptr;
```

**Method:** Global find-and-replace `\bNULL\b` → `nullptr` in all `.cpp` and `.h` files (excluding `src/external/`). Then fix any compilation issues from macro contexts like `base.h:102` where NULL is used in C-compatible function signatures.

**Test impact:** Also apply to test files in `tests/`. The test infrastructure (`test_interact.h`, `test_input_helpers.h`) uses NULL in some places. This is a safe mechanical change — existing tests will continue to pass after replacement.

### 2.2 Replace `#ifndef`/`#define` Include Guards with `#pragma once`

**Scope: 38 header files**

Every header in `src/` uses the old-style guard pattern.

**Before** (`walker.h:17-18`):
```cpp
#ifndef __WALKER_H
#define __WALKER_H
// ... (194 lines)
#endif
```

**After:**
```cpp
#pragma once
// ... (no closing #endif needed)
```

**Complete list of headers to convert:**

`base.h`, `button.h`, `campaign_picker.h`, `colors.h`, `effect.h`, `gloader.h`,
`gparser.h`, `graph.h`, `guy.h`, `input.h`, `io.h`, `level_data.h`,
`level_picker.h`, `living.h`, `obmap.h`, `OuyaController.h`, `picker.h`,
`pixie_data.h`, `pixie.h`, `pixien.h`, `radar.h`, `results_screen.h`,
`sai2x.h`, `save_data.h`, `screen.h`, `smooth.h`, `soundob.h`, `sounds.h`,
`stats.h`, `text.h`, `treasure.h`, `util.h`, `version.h`, `video.h`,
`view.h`, `view_sizes.h`, `walker.h`, `weap.h`

### 2.3 Add `override` to All Virtual Method Overrides

**Scope: 27 methods across 4 derived classes — 100% are missing `override`**

No `override` keyword is used anywhere in the codebase.

**Derived class: `living`** (`living.h:30-44`)

**Before:**
```cpp
class living : public walker
{
    public:
        living(const PixieData& data);
        virtual ~living();
        short          act();
        short          check_special();
        short          collide(walker  *ob);
        walker*        do_summon(char whatfamily, unsigned short lifetime);
        short          facing(short x, short y);
        void           set_difficulty(Uint32 whatlevel);
        short          shove(walker  *target, short x, short y);
        char           query_order() { return ORDER_LIVING; }
        virtual bool walk(float x, float y);
    protected:
        short act_random();
};
```

**After:**
```cpp
class living : public walker
{
    public:
        living(const PixieData& data);
        ~living() override;
        short          act() override;
        short          check_special() override;
        short          collide(walker  *ob) override;
        walker*        do_summon(char whatfamily, unsigned short lifetime) override;
        short          facing(short x, short y) override;
        void           set_difficulty(Uint32 whatlevel) override;
        short          shove(walker  *target, short x, short y) override;
        char           query_order() override { return ORDER_LIVING; }
        bool walk(float x, float y) override;
    protected:
        short act_random() override;
};
```

**Repeat for:**

- **`weap`** (`weap.h:29-43`): `~weap()`, `act()`, `animate()`, `death()`, `setxy()`, `query_order()`
- **`treasure`** (`treasure.h:29-38`): `~treasure()`, `act()`, `eat_me()`, `set_direct_frame()`, `query_order()`
- **`effect`** (`effect.h:33-40`): `~effect()`, `act()`, `animate()`, `death()`, `query_order()`
- **`screen`** (`screen.h:43`): `~screen()` (inherits from `video`)

### 2.4 Convert Old-Style `enum` to `enum class`

**Scope: 8 enums in main source**

| Location | Enum | Notes |
|----------|------|-------|
| `walker.cpp:1709` | `ExpActionEnum` | Local to function, easy conversion |
| `level_editor.cpp:83` | `ModeEnum` | File-scoped |
| `level_editor.cpp:3310` | `EventTypeEnum` | Function-local |
| `picker.cpp:83` | `PickerMenuState` | File-scoped |
| `help.cpp:498` | `HelpTab` | File-scoped |
| `glad.cpp:44` | `GameState` | File-scoped |
| `OuyaController.h:13` | `ButtonEnum` | Header, used by value - needs careful update |
| `OuyaController.h:18` | `AxisEnum` | Header, used by value |

**Before** (`glad.cpp:44`):
```cpp
enum GameState {
    GAME_STATE_INTRO,
    GAME_STATE_PICKER,
    GAME_STATE_PLAYING,
    GAME_STATE_QUIT
};
static GameState g_game_state = GAME_STATE_INTRO;
```

**After:**
```cpp
enum class GameState { Intro, Picker, Playing, Quit };
static GameState g_game_state = GameState::Intro;
```

**Note:** The `#define` constants in `base.h` (lines 154-373) for ACT types, FAMILY types, ORDER types, COMMAND types, etc. are NOT enums but `#define` macros. Converting those to `enum class` is a Phase 7 change due to their pervasive use.

### 2.5 Convert `typedef` to `using`

**Scope: 3 typedefs in main source**

**`base.h:380-384`** — typedef struct:
```cpp
// Before:
typedef struct { char r, g, b; } rgb;
// After:
struct rgb { char r, g, b; };
```

**`base.h:386`** — typedef alias:
```cpp
// Before:
typedef rgb palette[256];
// After:
using palette = rgb[256];  // Or better: using palette = std::array<rgb, 256>;
```

**`sai2x.h:6-12`** — typedef enum (combine with enum class conversion):
```cpp
// Before:
typedef enum { NoZoom = 0x01, SAI = 0x02, EAGLE = 0x03, DOUBLE = 0x04 } RenderEngine;
// After:
enum class RenderEngine { NoZoom = 0x01, SAI = 0x02, EAGLE = 0x03, DOUBLE = 0x04 };
```

### 2.6 Remove `using namespace std`

**Scope: 8 files**

| File | Line |
|------|------|
| `screen.cpp` | 36 |
| `gparser.cpp` | 38 |
| `util.cpp` | 49 |
| `level_editor.cpp` | 49 |
| `graphlib.cpp` | 26 |
| `level_picker.cpp` | 35 |
| `sound.cpp` | 32 |
| `glad.cpp` | 40 |

Remove each `using namespace std;` and prefix all `std::` types explicitly. Common types needing prefixing: `string`, `list`, `vector`, `map`, `set`, `pair`, `cout`, `endl`.

### 2.7 Remove Deprecated `register` Keyword

**Scope: 2 occurrences**

| File | Line |
|------|------|
| `sai2x.cpp` | 160 |
| `sai2x.cpp` | 418 |

**Before:**
```cpp
register int r = 0;
```
**After:**
```cpp
int r = 0;
```

### 2.8 Modernize C Header Includes

**File: `base.h:28-33`**

```cpp
// Before:
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

// After:
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
```

Also check `treasure.cpp:22` (`#include <math.h>`) and other files for C-style includes.

### 2.9 Phase 2 Test Impact

**Risk to existing tests: VERY LOW.** All Phase 2 changes are mechanical and behavior-preserving.

**Existing coverage:** The full test suite (25 tests) serves as a regression safety net for Phase 2. Key tests to watch:
- `test_guy_creation` / `test_guy_copy` — exercise `guy.h` which gets `#pragma once` and `override` changes
- `test_save_load_roundtrip` / `test_save_team_then_load` — exercise save/load code paths
- `test_fairy_death` / `test_overpowered_team` — full game integration tests that exercise walker/living/weap/treasure entity hierarchy (all getting `override` added)

**Required test updates:** Apply `NULL` → `nullptr` in test files too. No other test changes needed.

**New tests to add:** None required — these are mechanical transforms. The existing CI pipeline validates compilation and runtime behavior.

---

## Phase 3: Modern Types & Type Safety

**Risk: LOW-MEDIUM** | **Effort: MEDIUM-HIGH** | **Priority: AFTER PHASE 2**

### 3.0 Save File Versioning (Do Before 3.2)

**CRITICAL:** Before converting any binary-serialized types (especially `char name[12]` in `guy.h:49` and `stats.h:81`), implement a save file version header and migration path. The current save format in `save_data.cpp` reads/writes fixed-size structs directly to disk. Changing `char name[12]` to `std::string` will silently corrupt existing save files.

**Step 1: Add a version header to save files:**
```cpp
struct SaveFileHeader {
    char magic[4] = {'O', 'G', 'S', 'V'};  // OpenGlad SaVe
    uint32_t version = 1;                    // Increment on format changes
};
```

**Step 2: Write versioned serialization helpers:**
```cpp
// For name fields: always read/write as a fixed number of bytes on disk,
// convert to/from std::string in memory
void write_fixed_string(SDL_RWops* outfile, const std::string& s, size_t fixed_len) {
    std::vector<char> buf(fixed_len, '\0');
    std::copy_n(s.begin(), std::min(s.size(), fixed_len), buf.begin());
    SDL_RWwrite(outfile, buf.data(), fixed_len, 1);
}

std::string read_fixed_string(SDL_RWops* infile, size_t fixed_len) {
    std::vector<char> buf(fixed_len, '\0');
    SDL_RWread(infile, buf.data(), fixed_len, 1);
    return std::string(buf.data(), strnlen(buf.data(), fixed_len));
}
```

**Step 3: Version-gate format changes:**
- Version 0 (implicit): Current format, no header
- Version 1: Header present, same binary layout as v0
- Version 2+: Layout changes (e.g., variable-length strings)

Old saves without the magic header are treated as version 0 and loaded with the legacy code path.

### 3.1 Replace C-Style Casts with C++ Casts

**Scope: ~331 C-style casts across the codebase**

Most common patterns:

**Integer narrowing casts** — most frequent:
```cpp
// Before (button.cpp:254):
mytext.write_xy( (short) ( ((xloc+xend)/2) - ... ),
                 (short) (yloc + (height-(mytext.letters.h))/2),
                 label.c_str(), (unsigned char) DARK_BLUE, 1);

// After:
mytext.write_xy( static_cast<short>( ((xloc+xend)/2) - ... ),
                 static_cast<short>(yloc + (height-(mytext.letters.h))/2),
                 label.c_str(), static_cast<unsigned char>(DARK_BLUE), 1);
```

**Macro casts in headers** — `base.h` lines 327, 364-366, 373:
```cpp
// Before (base.h:327):
#define REGEN (Sint32) 4000
#define SCEN_TYPE_CAN_EXIT (char) 1

// After:
inline constexpr Sint32 REGEN = 4000;
inline constexpr char SCEN_TYPE_CAN_EXIT = 1;
```

**Priority files** (by cast count): `picker.cpp`, `walker.cpp`, `level_editor.cpp`, `view.cpp`, `button.cpp`, `effect.cpp`, `glad.cpp`, `help.cpp`

### 3.2 Replace `char[]` String Buffers with `std::string`

**Scope: 40+ char array declarations**

**High-priority targets** (used for game entity names):

**`guy.h:49`** and **`stats.h:81`**:
```cpp
// Before:
char name[12];

// After:
std::string name;
```

**Impact:** This change propagates to:
- `guy.cpp:42`: `strcpy(name, "SOLDIER")` → `name = "SOLDIER"`
- `guy.cpp:127`: `strcpy(name, copy.name)` → `name = copy.name`
- `save_data.cpp` lines 99, 169, 286, 311, 532, 603, 665: All strcpy/strncpy calls to/from name
- **Binary serialization** in `save_data.cpp` must be updated to read/write fixed-size strings from `std::string`

**Screen text buffers** (`view.h:110, 113`):
```cpp
// Before:
char textlist[MAX_MESSAGES][80];
char infotext[80];

// After:
std::array<std::string, MAX_MESSAGES> textlist;
std::string infotext;
```

**Screen name arrays** (`screen.h:98-99`):
```cpp
// Before:
char special_name[NUM_FAMILIES][NUM_SPECIALS][20];
char alternate_name[NUM_FAMILIES][NUM_SPECIALS][20];

// After:
std::array<std::array<std::string, NUM_SPECIALS>, NUM_FAMILIES> special_name;
std::array<std::array<std::string, NUM_SPECIALS>, NUM_FAMILIES> alternate_name;
```

### 3.3 Replace `sprintf`/`snprintf` with `std::format` or `std::string` Operations

**Scope: ~232 occurrences (75 sprintf + 157 snprintf)**

**Top files:** `level_editor.cpp` (30+), `picker.cpp` (20+), `walker.cpp` (40+), `view.cpp` (12)

**Before** (`glad.cpp:771`):
```cpp
char message[50];
sprintf(message, "HP: %.0f", ceilf(control->stats->hitpoints));
myscreen->viewob[0]->set_display_text(message, 30);
```

**After (C++20 std::format):**
```cpp
auto message = std::format("HP: {:.0f}", std::ceil(control->stats->hitpoints));
myscreen->viewob[0]->set_display_text(message.c_str(), 30);
```

**After (if std::format unavailable):**
```cpp
std::string message = "HP: " + std::to_string(static_cast<int>(std::ceil(control->stats->hitpoints)));
```

### 3.4 Replace C String Functions with `std::string` Methods

**Scope: ~358 occurrences**

| Function | Count | Replacement |
|----------|-------|-------------|
| `strcpy` | ~207 | `std::string` assignment or `.assign()` |
| `strlen` | ~98 | `std::string::size()` or `std::string::length()` |
| `strcat` | ~37 | `std::string::operator+=` or `std::string::append()` |
| `strncpy` | ~16 | `std::string` constructor or `.substr()` |

**Before** (`save_data.cpp:170`):
```cpp
char temp_filename[200];
strcpy(temp_filename, current_campaign.c_str());
strcat(temp_filename, ".gtl");
```

**After:**
```cpp
std::string temp_filename = current_campaign + ".gtl";
```

### 3.5 Replace `memcpy`/`memset` with C++ Equivalents

**Scope: ~33 occurrences**

**`memcpy`** (11 occurrences) → `std::copy` or `std::copy_n`:
```cpp
// Before (video.cpp:1718):
memcpy(ourpalette, newpal, 768);
// After:
std::copy_n(newpal, 768, ourpalette);
```

**`memset`** (17 occurrences) → `std::fill` or value initialization:
```cpp
// Before (gloader.cpp:339):
memset(hitpoints, 0, sizeof(float) * 200);
// After:
std::fill(std::begin(hitpoints), std::end(hitpoints), 0.0f);
```

### 3.6 Type the `void*` Pointers

**Scope: 5 occurrences in main source**

**`walker.h:154`** — pathfinding results:
```cpp
// Before:
std::vector<void*> path_to_foe;
// After:
std::vector<std::pair<int, int>> path_to_foe;  // Or a proper PathNode type
```

**`io.h:60-61`** — SDL callback handlers:
```cpp
// Before:
int rwops_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read);
int rwops_write_handler(void *data, unsigned char *buffer, size_t size);
// After (keep void* for C API compatibility, but add internal typed cast):
int rwops_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read);
// Internal: auto* rwops = static_cast<SDL_RWops*>(data);
```

### 3.7 Phase 3 Test Impact

**Risk to existing tests: MEDIUM.** Type changes to `guy.h` and `stats.h` directly affect tested code.

**Existing tests that validate Phase 3 changes:**

| Change | Tests That Cover It |
|--------|-------------------|
| `char name[12]` → `std::string` in `guy.h` | `test_guy_creation` (checks default names like "SOLDIER"), `test_guy_copy` (copies name field), `test_save_team_then_load` (persists names through save/load) |
| `char name[12]` → `std::string` in `stats.h` | `test_fairy_death`, `test_overpowered_team` (exercise stats during gameplay) |
| Save file versioning (3.0) | `test_save_load_roundtrip`, `test_save_team_then_load` (verify save/load roundtrip) |
| `sprintf`/`snprintf` → `std::format` | `test_load_multiple_levels` (level loading uses sprintf for filenames) |
| `strcpy`/`strcat` removal | `test_save_team_then_load` (save_data.cpp uses strcpy extensively) |

**Tests that will need updates:**
- `test_guy.cpp` — If `guy::name` becomes `std::string`, test assertions comparing names still work (string comparison is compatible), but any code that does `strcpy(guy.name, ...)` in test setup would need updating
- `test_save_load_team.cpp` — Tests the save/load roundtrip; after save file versioning (3.0), old test save files must still load correctly (version 0 compat path)

**New tests to add:**
- **Save file version migration test:** Create a v0 (headerless) save file, load it with the new versioned reader, verify data integrity. This is critical — the existing `test_save_load_roundtrip` tests the current format, but Phase 3.0 changes the format.
- **String boundary tests:** Test `guy::name` with empty strings, max-length strings, and strings that would have overflowed the old 12-char buffer.
- **`std::format` compatibility test:** Verify `std::format` works in the Emscripten build (part of Phase 0 verification, but should be a CI check).

**Coverage gaps exposed by Phase 3:**
- No tests for `view.cpp` text buffer rendering (Phase 3.2 changes `textlist[MAX_MESSAGES][80]` to `std::string`)
- No tests for `screen.h` special_name/alternate_name arrays
- No tests for `io.cpp` read/write handlers (Phase 3.6 void* typing)

---

## Phase 4: Memory Management (Smart Pointers & RAII)

**Risk: MEDIUM** | **Effort: HIGH** | **Priority: AFTER PHASE 3**

The codebase has **zero smart pointer usage**. All memory is managed with raw `new`/`delete` (~162 `new`, ~165 `delete`).

### 4.1 Entity Ownership: `std::unique_ptr` for Walker Lists

The central ownership pattern is `LevelData` owning walkers via `std::list<walker*>`.

**`level_data.h:88-92`:**
```cpp
// Before:
std::list<walker*> oblist;
std::list<walker*> fxlist;
std::list<walker*> weaplist;
std::list<walker*> dead_list;

// After:
std::list<std::unique_ptr<walker>> oblist;
std::list<std::unique_ptr<walker>> fxlist;
std::list<std::unique_ptr<walker>> weaplist;
std::list<std::unique_ptr<walker>> dead_list;
```

**Impact:** This is the single highest-impact change. It propagates to:
- `level_data.cpp` — `add_ob()`, `add_fx_ob()`, `add_weap_ob()`, `remove_ob()`, `delete_objects()`
- `screen.cpp` — entity iteration in `act()`, `find_near_foe()`, `find_in_range()`, etc.
- `walker.cpp` — `fire()` which creates weapons and adds to weaplist
- `gloader.cpp` — `create_walker()` which creates entity instances
- `view.cpp` — drawing code iterating over entity lists
- All `find_*` functions that return `walker*` (these become non-owning raw pointers)

**Strategy:** Non-owning references remain raw `walker*`. Only the containers that own objects use `unique_ptr`. Factory functions return `unique_ptr` that gets moved into the owning container.

```cpp
// gloader.cpp - factory returns unique_ptr:
std::unique_ptr<walker> loader::create_walker(char order, char family);

// level_data.cpp - caller moves into list:
walker* LevelData::add_ob(char order, char family, bool atstart) {
    auto ob = myloader->create_walker(order, family);
    walker* raw = ob.get();
    if (atstart) oblist.push_front(std::move(ob));
    else oblist.push_back(std::move(ob));
    return raw;  // non-owning pointer for callers
}
```

### 4.2 `viewscreen` Ownership in `screen`

**`screen.h:103`:**
```cpp
// Before:
viewscreen  * viewob[5];

// After:
std::array<std::unique_ptr<viewscreen>, 5> viewob;
```

**Motivation:** The `screen` destructor (`screen.cpp:235`) deletes `soundp` directly, then delegates to `cleanup()` which loops through and deletes all viewscreens. This works correctly but splits ownership cleanup across two code paths. Using `std::unique_ptr` expresses ownership semantics clearly and makes destruction automatic and self-documenting, eliminating the need for the manual `cleanup()` loop.

### 4.3 `statistics` Ownership in `walker`

**`walker.h:126`:**
```cpp
// Before:
statistics *stats;
// After:
std::unique_ptr<statistics> stats;
```

**`walker.cpp:49`:**
```cpp
// Before:
stats = new statistics(this);
// After:
stats = std::make_unique<statistics>(this);
```

### 4.4 `guy` Ownership in `walker`

**`walker.h:132`:**
```cpp
// Before:
guy  *myguy;
// After:
guy  *myguy;  // Non-owning — owned by SaveData::team_list
```

`myguy` is a non-owning pointer. The `guy` objects are owned by `SaveData::team_list`. This should remain a raw pointer but be documented.

### 4.5 `soundob` Ownership in `screen`

**`screen.h:101`:**
```cpp
// Before:
soundob *soundp;
// After:
std::unique_ptr<soundob> soundp;
```

### 4.6 `loader` and `obmap` Ownership in `LevelData`

**`level_data.h:86, 94`:**
```cpp
// Before:
loader* myloader;
obmap* myobmap;

// After:
std::unique_ptr<loader> myloader;
std::unique_ptr<obmap> myobmap;
```

### 4.7 `pixie*` Arrays in `LevelData` and `CampaignData`

**`level_data.h:99`:**
```cpp
// Before:
pixieN* back[PIX_MAX];

// After:
std::array<std::unique_ptr<pixieN>, PIX_MAX> back;
```

**`level_data.h:54`:**
```cpp
// Before:
pixie* icon;
// After:
std::unique_ptr<pixie> icon;
```

### 4.8 Replace `malloc`/`free` with `new`/`delete` or Smart Pointers

**`io.cpp:399`:**
```cpp
// Before:
unsigned char* data = (unsigned char*)malloc(size);
// ... use data ...
free(data);

// After:
auto data = std::make_unique<unsigned char[]>(size);
```

**`input.cpp:393-394, 864, 1446`** — `free(raw_text_input)` and `strdup()`:
```cpp
// Before:
char* raw_text_input = strdup(text);
// ... later:
free(raw_text_input);

// After:
std::string raw_text_input = text;
```

### 4.9 `PixieData` RAII

**`pixie_data.h:22-37`** — Currently has a manual `free()` method:
```cpp
// Before:
class PixieData {
public:
    unsigned char frames;
    unsigned char w, h;
    unsigned char* data;
    void free();
};

// After:
class PixieData {
public:
    unsigned char frames = 0, w = 0, h = 0;
    std::unique_ptr<unsigned char[]> data;
    // Rule of Five: defaulted move, deleted copy
    PixieData() = default;
    ~PixieData() = default;
    PixieData(PixieData&&) = default;
    PixieData& operator=(PixieData&&) = default;
    PixieData(const PixieData&) = delete;
    PixieData& operator=(const PixieData&) = delete;
};
```

**Breaking change: `data_copy()` in `gloader.cpp:317`** returns `PixieData` by value and manually copies the pixel buffer. It is called 5 times (lines 639-648) to share sprite data between similar treasure types (e.g., silver bar copies gold bar graphics). Deleting the copy constructor will break this function.

**Required fix:** Rewrite `data_copy()` to return via move semantics:
```cpp
PixieData data_copy(const PixieData& d) {
    PixieData result;
    if (!d.valid()) return result;  // moved out via NRVO or move ctor
    result.frames = d.frames;
    result.w = d.w;
    result.h = d.h;
    size_t size = d.frames * d.w * d.h;
    result.data = std::make_unique<unsigned char[]>(size);
    std::copy_n(d.data.get(), size, result.data.get());
    return result;  // NRVO or implicit move
}
```
This preserves the deep-copy semantics (each treasure type gets its own pixel buffer) while using move to return the result.

### 4.10 `delete[]` in `gloader.cpp`

**`gloader.cpp:778-785`:**
```cpp
// Before:
delete[] graphics;
delete[] animations;
delete[] act_types;
delete[] stepsizes;
delete[] lineofsight;
delete[] damage;
delete[] fire_frequency;
```

These arrays in `loader` (`gloader.h:38+`) should become `std::vector` or `std::unique_ptr<T[]>`.

### 4.11 Phase 4 Test Impact

**Risk to existing tests: HIGH.** This is the most dangerous phase for test breakage. Smart pointer conversions change ownership semantics throughout the entity system, which is exercised by the two most comprehensive tests.

**Existing tests that validate Phase 4 changes:**

| Change | Tests That Cover It |
|--------|-------------------|
| `unique_ptr<walker>` for entity lists (4.1) | `test_fairy_death` (runs full game loop with entity creation, death, and cleanup), `test_overpowered_team` (creates 14 entities, runs combat, verifies win) |
| `unique_ptr<statistics>` in walker (4.3) | `test_overpowered_team` (programmatically sets `stats->hitpoints`, `stats->max_hitpoints`, etc.), `test_fairy_death` (verifies fairy death via game loop) |
| `unique_ptr<viewscreen>` in screen (4.2) | `test_screen_creation`, all menu tests (menu rendering goes through viewscreens) |
| `unique_ptr<soundob>` in screen (4.5) | `test_sdl_init` (initializes sound subsystem) |
| PixieData RAII (4.9) | `test_load_multiple_levels` (level loading creates PixieData for tiles/sprites), `test_level_data_integrity` |

**Tests that will need updates:**
- `test_overpowered_team.cpp` — Directly accesses `walker->stats->hitpoints = 200` etc. With `unique_ptr<statistics>`, this still works via `->` but any code that copies or reassigns the stats pointer would break.
- `test_fairy_death.cpp` — Runs `glad_main()` which exercises the full entity lifecycle (creation → act → death → cleanup). This is the best integration test for validating that `unique_ptr` ownership doesn't cause use-after-free or double-delete.
- Any test that calls `picker_main()` — The picker creates/destroys UI entities (buttons, pixies, backdrops) that will be affected by smart pointer changes.

**New tests to add:**
- **Entity lifecycle test:** Create a walker via `create_walker()`, add to entity list, verify it can be found, mark as dead, run cleanup pass, verify it's removed and memory is freed (no leak). This directly validates the Phase 4.1 ownership model.
- **Entity transfer test:** Test moving a walker between lists (e.g., from `oblist` to `dead_list`) with `unique_ptr` — this is a common pattern in `screen.cpp`.
- **PixieData move semantics test:** Test that `data_copy()` returns valid data after the Phase 4.9 rewrite, and that the moved-from PixieData is empty.
- **Double-delete regression tests:** With raw pointers, double-delete was a latent risk. Add ASan to CI (Phase 1.2 enables this) and ensure the full game integration tests (`test_fairy_death`, `test_overpowered_team`) pass under ASan.

**Coverage gaps exposed by Phase 4:**
- No tests for `loader` destruction (Phase 4.10 `delete[]` arrays) — the loader is created once and never destroyed in tests
- No tests for `obmap` (spatial indexing) — Phase 4.6 changes its ownership but no test exercises collision detection directly
- No tests for `pixie`/`pixieN` destruction — sprite cleanup is untested
- No test for the `screen` destructor path — tests call `_exit()` to avoid SDL cleanup hangs, so the `screen` destructor (Phase 4.2/4.5) is never exercised in tests

**Recommendation:** Run the existing integration tests (`test_fairy_death`, `test_overpowered_team`) under AddressSanitizer after each sub-step of Phase 4. These tests exercise the deepest ownership chains and will surface use-after-free bugs immediately.

---

## Phase 5: Modern Idioms (Loops, Algorithms, Lambdas)

**Risk: LOW** | **Effort: MEDIUM** | **Priority: AFTER PHASE 2, CAN PARALLEL WITH 3-4**

### 5.1 Convert Iterator Loops to Range-Based For

**Scope: ~168 iterator-based loops across 16 files**

**Before** (`effect.cpp`):
```cpp
for(auto e = foelist.begin(); e != foelist.end(); e++)
{
    walker* ob = *e;
    // use ob
}
```

**After:**
```cpp
for(auto* ob : foelist)
{
    // use ob
}
```

**Before** (`level_editor.cpp:1169`):
```cpp
for(std::list<std::pair<SimpleButton*, std::set<SimpleButton*> > >::const_iterator e = ls.begin(); e != ls.end(); e++)
```

**After:**
```cpp
for(const auto& [btn, btnSet] : ls)  // structured binding
```

**Exception:** Loops that erase elements while iterating must keep iterator style but can use `std::erase_if` (C++20):

```cpp
// Before (screen.cpp:821-832):
for(auto e = level_data.fxlist.begin(); e != level_data.fxlist.end();)
{
    walker* ob = *e;
    if(ob && ob->dead) {
        delete ob;
        e = level_data.fxlist.erase(e);
        continue;
    }
    e++;
}

// After (C++20):
std::erase_if(level_data.fxlist, [](const auto& ob) {
    if(ob && ob->dead) { return true; }
    return false;
});
// (with unique_ptr, delete happens automatically)
```

### 5.2 Convert C-Style Index Loops Over Containers

**Scope: ~150 C-style index loops**

**Before** (`screen.cpp:799`):
```cpp
for(int i = 0; i < numviews; i++)
    viewob[i]->draw();
```

**After:**
```cpp
for(auto& view : viewob | std::views::take(numviews))  // C++20 ranges
    view->draw();
// Or simpler:
for(int i = 0; i < numviews; i++)  // Keep when index is semantically meaningful
    viewob[i]->draw();
```

### 5.3 Use `auto` Where Appropriate

Apply `auto` for:
- Iterator declarations
- Factory function return values
- Complex template types

**Do NOT use `auto` for:**
- Primitive types where the type conveys meaning (`short`, `float`, `int`)
- Return types of functions
- Class member declarations

### 5.4 Add `constexpr` Where Possible

**`base.h`** — All `#define` constants that are pure values:
```cpp
// Before (base.h:89-90):
#define MAX_LEVELS 500
#define GRID_SIZE 16

// After:
inline constexpr int MAX_LEVELS = 500;
inline constexpr int GRID_SIZE = 16;
```

**Scope:** ~100 `#define` constants in `base.h` (lines 89-393), `view.h` (lines 26-55), `stats.h` (lines 30-47)

**Do NOT convert:**
- `#define PIX(a,b) (NUM_FAMILIES*a+b)` — keep as `constexpr` function:
  ```cpp
  constexpr int PIX(int a, int b) { return NUM_FAMILIES * a + b; }
  ```
- Conditional compilation macros (`PROT_MODE`, `CHEAT_MODE`)

### 5.5 Nullable Pointer Returns — Keep Current Pattern

The `find_*` functions in `screen.h:61-69` return `walker*` where `nullptr` means "not found." **Do NOT wrap these in `std::optional<walker*>`** — a pointer is already nullable, and `std::optional<T*>` is an anti-pattern that adds a redundant "empty" state (both `std::nullopt` and `nullptr` would mean "absent").

The current `nullptr` return pattern is idiomatic C++ and should be kept:
```cpp
// This is correct and idiomatic — no changes needed:
walker* screen::find_near_foe(walker *ob) {
    // ... search ...
    if (found) return target;
    return nullptr;  // clear "not found" sentinel
}
```

**Where `std::optional` IS appropriate:** Use it for non-pointer value types where there's no natural sentinel, e.g., functions that return `int`/`float`/`short` where any value could be valid.

### 5.6 Use `std::string_view` for Read-Only String Parameters

**Before** (`screen.h:71`):
```cpp
void do_notify(const char *message, walker *who);
const char* get_scen_title(const char *filename, screen *master);
```

**After:**
```cpp
void do_notify(std::string_view message, walker *who);
std::string_view get_scen_title(std::string_view filename, screen *master);
```

**Apply to:** All `const char*` parameters that are only read, not stored.

**Caveat — null termination:** `std::string_view` is **not** guaranteed to be null-terminated. Functions that pass the argument directly to C APIs requiring null-terminated strings (e.g., `strcpy`, `fopen`, `SDL_RWFromFile`, `open_read_file`) must use `const std::string&` instead, or explicitly convert via `std::string(sv).c_str()`. For example, `screen::get_scen_title` does `strcpy(tempfile, filename)` internally, so its `filename` parameter should take `const std::string&`, not `std::string_view`.

### 5.7 Phase 5 Test Impact

**Risk to existing tests: LOW.** Most Phase 5 changes are syntactic (range-based for, auto, constexpr) and don't change behavior.

**Existing coverage:** The full test suite serves as a regression net. The integration tests (`test_fairy_death`, `test_overpowered_team`) exercise the main game loops that contain most of the iterator-based loops being converted.

**Tests that will need updates:**
- Test code itself should be modernized alongside game code (range-based for, auto, etc.) for consistency, but this is cosmetic, not functional.

**New tests to add:**
- **`constexpr` validation:** When converting `#define` constants to `constexpr` (5.4), add static_assert tests to verify values match the originals. Example:
  ```cpp
  static_assert(MAX_LEVELS == 500);
  static_assert(GRID_SIZE == 16);
  ```
  These compile-time checks catch any value mismatches during the conversion with zero runtime cost.

**Interaction with Phase 4:** Phase 5b (ownership-dependent loops using `std::erase_if` with `unique_ptr`) depends on Phase 4 completing first. The entity cleanup loops in `screen.cpp` are exercised by `test_fairy_death` (entities die and get cleaned up) — run this test after every Phase 5b change.

---

## Phase 6: Error Handling Modernization

**Risk: MEDIUM** | **Effort: MEDIUM** | **Priority: AFTER PHASE 4**

The codebase uses no C++ exceptions. All error handling is via return codes and logging.

### 6.1 Centralize Error Reporting

The existing logging system (`util.h:33-35`) is well-designed:
```cpp
void Log(const char* format, ...);
void LogWarn(const char* format, ...);
void LogError(const char* format, ...);
```

**Modernize to use `std::format`:**
```cpp
template<typename... Args>
void Log(std::format_string<Args...> fmt, Args&&... args) {
    auto msg = std::format(fmt, std::forward<Args>(args)...);
    SDL_Log("%s", msg.c_str());
}
```

### 6.2 Replace Return-Code Error Patterns with `std::expected` (C++23)

**Before** (`level_data.cpp`):
```cpp
bool LevelData::load() {
    // ... lots of code ...
    if (error) {
        Log("Failed to load level %d", id);
        return false;
    }
    return true;
}
```

**After (C++23):**
```cpp
std::expected<void, std::string> LevelData::load() {
    // ...
    if (error)
        return std::unexpected("Failed to load level " + std::to_string(id));
    return {};
}
```

**If C++23 not available**, keep `bool` returns but ensure consistent patterns.

### 6.3 Replace `short` Return Types with `bool`

Many functions return `short` where they really mean `bool` (0 = failure, 1 = success):
- `walker::act()`, `walker::animate()`, `walker::death()` — all return `short`
- `screen::act()`, `screen::redraw()` — return `short`

**This is a LARGE change** (touches virtually every method signature) and should be done incrementally per-class.

### 6.4 Consider Selective Exception Use for Fatal Errors

For truly unrecoverable situations (SDL init failure, missing critical game files), consider throwing exceptions instead of calling `exit()` or returning error codes deep in the call stack.

**Currently** (`video.cpp`):
```cpp
if (!window) {
    LogError("Failed to create window");
    exit(1);
}
```

**After:**
```cpp
if (!window) {
    throw std::runtime_error("Failed to create SDL window");
}
// Caught at top level in main()
```

### 6.5 Phase 6 Test Impact

**Risk to existing tests: LOW-MEDIUM.** Error handling changes are mostly additive, but changing return types (`short` → `bool`) affects callers.

**Existing coverage:**
- `test_load_multiple_levels` / `test_level_fallback` — exercise `LevelData::load()` error paths (loading nonexistent level 9999 falls back to level 1)
- `test_save_load_roundtrip` — exercises save/load which would be affected by `std::expected` return types

**Tests that will need updates:**
- If `walker::act()` return type changes from `short` to `bool` (6.3), no existing test directly checks the return value — the game integration tests call it indirectly through `screen::act()`.

**New tests to add:**
- **Error path tests for `std::expected`:** If `LevelData::load()` returns `std::expected`, add tests that deliberately trigger errors (corrupted files, missing files) and verify the error messages.
- **Exception safety tests:** If fatal errors throw exceptions (6.4), verify that the test harness catches them gracefully. Note: `test_main.cpp` should wrap test execution in a try/catch to handle unexpected exceptions without crashing the test runner.

**Interaction with TESTING guards:** Currently `quit()` is a no-op under `#ifdef TESTING`. If fatal errors become exceptions instead of `exit()` calls, the TESTING guard behavior may need updating — exceptions propagate naturally without needing a guard.

---

## Phase 7: Class Design & Encapsulation

**Risk: HIGH** | **Effort: VERY HIGH** | **Priority: LAST**

This phase requires the most careful work and testing.

### 7.1 Encapsulate `walker` Public Members

**Problem:** `walker.h` has **70+ public data members** directly accessed throughout the codebase.

**Strategy:** Encapsulate incrementally, one logical group at a time:

**Group 1: Combat stats** (accessed primarily via `stats` pointer):
```cpp
// Before (walker.h:123-125):
public:
    float damage;
    float fire_frequency;
    float busy;

// After:
private:
    float damage_;
    float fire_frequency_;
    float busy_;
public:
    float damage() const { return damage_; }
    void set_damage(float d) { damage_ = d; }
    float fire_frequency() const { return fire_frequency_; }
    float busy() const { return busy_; }
    void set_busy(float b) { busy_ = b; }
```

**Group 2: Position/movement:**
```cpp
// Before (walker.h:119-121):
public:
    float worldx, worldy;
    float stepsize;

// After:
private:
    float worldx_, worldy_;
    float stepsize_;
public:
    float worldx() const { return worldx_; }
    float worldy() const { return worldy_; }
    void set_world_pos(float x, float y) { worldx_ = x; worldy_ = y; }
```

**Group 3: Team/identity:**
```cpp
// Before:
public:
    unsigned char team_num;
    unsigned char real_team_num;

// After:
private:
    unsigned char team_num_;
    unsigned char real_team_num_;
public:
    unsigned char team_num() const { return team_num_; }
    void set_team_num(unsigned char t) { team_num_ = t; }
```

**Approach:** Do ONE group per PR, fix all compilation errors, test, merge.

### 7.2 Eliminate `friend` Class Abuse

**Scope: 8 friend declarations**

**`viewscreen`** (`view.h:78-81`) has 4 friends:
```cpp
friend class walker;
friend class pixieN;
friend class pixie;
friend class text;
```

**Resolution:** These friends access `bmp`, `oldbmp`, `xview`, `yview`, and viewport coordinates. Expose proper public accessor methods and remove friend declarations.

**`walker`** (`walker.h:29-30`):
```cpp
friend class statistics;
friend class command;
```

`statistics` accesses `walker`'s protected members. Provide public methods or pass needed data as parameters to `statistics` methods instead.

### 7.3 Rule of Five Compliance

**10 classes have custom destructors but are missing copy/move operations:**

| Class | Has Destructor | Copy Ctor | Copy Assign | Move Ctor | Move Assign |
|-------|---------------|-----------|-------------|-----------|-------------|
| `walker` | Yes | No | No | No | No |
| `living` | Yes | No | No | No | No |
| `weap` | Yes | No | No | No | No |
| `treasure` | Yes | No | No | No | No |
| `effect` | Yes | No | No | No | No |
| `pixie` | Yes | No | No | No | No |
| `pixieN` | Yes | No | No | No | No |
| `video` | Yes | No | No | No | No |
| `screen` | Yes | No | No | No | No |
| `loader` | Yes | No | No | No | No |

**Only `guy`** has a copy constructor (`guy.h:31`).

**Strategy:** For entity classes (walker hierarchy), delete copy and provide move:
```cpp
class walker : public pixieN {
public:
    walker(const walker&) = delete;
    walker& operator=(const walker&) = delete;
    walker(walker&&) = default;
    walker& operator=(walker&&) = default;
};
```

For `video`/`screen` (non-copyable singletons):
```cpp
class video {
public:
    video(const video&) = delete;
    video& operator=(const video&) = delete;
    video(video&&) = delete;
    video& operator=(video&&) = delete;
};
```

### 7.4 Add `const`-Correctness

**Query methods that should be `const`:**

| Class | Method | File:Line |
|-------|--------|-----------|
| `walker` | `query_order()` | `walker.h:60` |
| `walker` | `query_family()` | `walker.h:64` |
| `walker` | `query_type()` | `walker.h:94` |
| `walker` | `query_act_type()` | `walker.h:53` |
| `walker` | `query_old_act_type()` | `walker.h:55` |
| `walker` | `query_next_to()` | `walker.h:70` |
| `walker` | `spaces_clear()` | `walker.h:79` |
| `walker` | `is_friendly()` | `walker.h:92` |
| `walker` | `is_friendly_to_team()` | `walker.h:93` |
| `walker` | `distance_to_ob()` | `walker.h:88` |
| `walker` | `distance_to_ob_center()` | `walker.h:89` |
| `living` | `query_order()` | `living.h:38` |
| `weap` | `query_order()` | `weap.h:35` |
| `treasure` | `query_order()` | `treasure.h:35` |
| `effect` | `query_order()` | `effect.h:37` |
| `pixie` | `on_screen()` | `pixie.h:44-45` |
| `statistics` | `query_bit_flags()` | `stats.h:69` |
| `statistics` | `has_commands()` | `stats.h:64` |

**Parameters that should be `const`:**

| Function | Parameter | File |
|----------|-----------|------|
| `screen::query_passable()` | `walker *ob` → `const walker *ob` | `screen.h:48` |
| `screen::find_near_foe()` | `walker *ob` → `const walker *ob` | `screen.h:61` |
| `screen::find_far_foe()` | `walker *ob` → `const walker *ob` | `screen.h:62` |

### 7.5 Convert `#define` Constants to Proper Types

**`base.h` has ~200 `#define` constants** that should become:

**Order/Family constants** (`base.h:196-300`) → `enum class`:
```cpp
// Before:
#define ORDER_LIVING 0
#define ORDER_WEAPON 1
#define ORDER_TREASURE 2
#define ORDER_GENERATOR 3
#define ORDER_FX 4

// After:
enum class Order : char {
    Living = 0, Weapon = 1, Treasure = 2,
    Generator = 3, FX = 4, Special = 5, Button1 = 6
};
```

```cpp
// Before:
#define FAMILY_SOLDIER 0
#define FAMILY_ELF 1
// ... (20 more)

// After:
enum class LivingFamily : char {
    Soldier = 0, Elf = 1, Archer = 2, Mage = 3, /* ... */
};
```

**This is a MASSIVE change** — `order` and `family` are used as `char` values throughout the entire codebase. This should be the very last change.

### 7.6 Refactor Global Variables

**`base.h:87`:**
```cpp
extern screen * myscreen;
```

This global is used in nearly every source file. Options:
1. **Service locator pattern:** Create a `GameContext` class that holds `screen*`, `cfg_store*`, etc.
2. **Pass as parameter:** Thread `screen*` through function calls (most invasive but cleanest)
3. **Singleton:** Make `screen` a proper singleton with `screen::instance()`

**Recommendation:** Service locator as a first step:
```cpp
struct GameContext {
    screen* screen;
    cfg_store* config;
    // ...
    static GameContext& instance();
};
```

### 7.7 Phase 7 Test Impact

**Risk to existing tests: VERY HIGH.** Encapsulation changes (making public members private, adding getters/setters) will break every test that directly accesses entity fields.

**Existing tests that will break:**

| Encapsulation Change | Tests Affected | Fix Required |
|---------------------|---------------|-------------|
| `walker` combat stats (7.1 Group 1) | `test_overpowered_team` — directly sets `stats->hitpoints = 200`, `stats->max_hitpoints = 200`, etc. | Use new setter methods: `stats->set_hitpoints(200)` |
| `walker` team identity (7.1 Group 3) | `test_overpowered_team` — accesses `team_num` during entity setup | Use `set_team_num()` / `team_num()` |
| `guy` fields | `test_guy_creation` — checks `guy.family`, `guy.name`; `test_guy_copy` — checks `strength`, `dexterity`, `constitution`, `intelligence`, `armor`, `kills`; `test_guy_level_up` — checks `level`, `strength` | Use getter methods or keep `guy` fields public (simpler data-only struct) |
| `walker` position (7.1 Group 2) | `test_fairy_death`, `test_overpowered_team` — game loop accesses `worldx`, `worldy` internally | No test code changes needed (accessed internally), but verify integration tests pass |
| `#define` → `enum class` (7.5) | `test_guy_creation` — uses `FAMILY_SOLDIER`, `FAMILY_ARCHER`, etc.; `test_overpowered_team` — iterates `ORDER_LIVING`, `FAMILY_*` constants | Update to `Order::Living`, `LivingFamily::Soldier`, etc. |
| Global `myscreen` → GameContext (7.6) | All tests — `test_main.cpp` sets up `myscreen` as a global; test code accesses it directly | Update to `GameContext::instance().screen` |

**Strategy for test compatibility:** Do ONE encapsulation group per PR (as recommended in 7.1). After each group, update the affected test files in the same PR. The test suite provides immediate feedback on what broke.

**New tests to add:**
- **Const-correctness tests (7.4):** After adding `const` to query methods, add tests that call them through `const walker*` references to verify const-correctness compiles.
- **Deleted copy constructor tests (7.3):** Add `static_assert(!std::is_copy_constructible_v<walker>)` to verify copy is truly deleted.
- **`enum class` type safety tests (7.5):** Verify that mixing `Order` and `LivingFamily` values doesn't compile (the whole point of `enum class`).

**Coverage gaps exposed by Phase 7:**
- No tests for `viewscreen` friend removal (7.2) — no test directly tests rendering internals
- No tests for `statistics` friend removal (7.2) — stats are accessed through walker, not directly tested in isolation
- No tests for `screen` singleton / GameContext (7.6) — all tests assume the global `myscreen` pattern

---

## Phase 8: Containers & Algorithms

**Risk: LOW-MEDIUM** | **Effort: MEDIUM** | **Priority: CAN PARALLEL WITH PHASE 5**

### 8.1 Replace C Arrays with `std::array`

**`video.h:143-148`** — Fixed-size palette buffers:
```cpp
// Before:
unsigned char ourpalette[768];
unsigned char redpalette[768];
unsigned char bluepalette[768];
unsigned char dospalette[768];
unsigned char videobuffer[64000];

// After:
std::array<unsigned char, 768> ourpalette{};
std::array<unsigned char, 768> redpalette{};
std::array<unsigned char, 768> bluepalette{};
std::array<unsigned char, 768> dospalette{};
std::array<unsigned char, 64000> videobuffer{};
```

**`screen.h:80`:**
```cpp
// Before:
unsigned char newpalette[768];
// After:
std::array<unsigned char, 768> newpalette{};
```

**`gloader.h:38`:**
```cpp
// Before:
float hitpoints[200];
// After:
std::array<float, 200> hitpoints{};
```

**`gloader.cpp:36-43`** — Animation direction arrays:
```cpp
// Before:
signed char bit1[] = {(char) 1,(char) 5,(char) 1,(char) 9,(signed char) -1};     // up
signed char bit2[] = {(char) 13,(char) 17,(char) 13,(char) 21,(signed char) -1}; // up-right
// ... (8 direction arrays, plus att1-att8 attack arrays)

// After:
constexpr std::array<signed char, 5> bit1 = {1, 5, 1, 9, -1};     // up
constexpr std::array<signed char, 5> bit2 = {13, 17, 13, 21, -1}; // up-right
// ... (remove redundant C-style casts, use std::array for bounds safety)
// Note: The ani16/ani8 pointer-to-array tables that reference these
// will need std::span or const signed char* views.
```

### 8.2 Use `std::span` for Buffer Parameters (C++20)

**`video.h`** — Many methods take `unsigned char*` + size:
```cpp
// Before:
void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
             unsigned char *sourcedata);

// After:
void putdata(Sint32 startx, Sint32 starty, Sint32 xsize, Sint32 ysize,
             std::span<const unsigned char> sourcedata);
```

### 8.3 Use STL Algorithms to Replace Manual Loops

**Currently used STL algorithms:** `std::find` (7 uses), `std::sort` (1 use)

**Opportunities for `std::find_if`:**
```cpp
// Before (screen.cpp - find_near_foe pattern):
walker* closest = NULL;
Sint32 min_dist = 99999;
for(auto e = oblist.begin(); e != oblist.end(); e++) {
    walker* ob = *e;
    if (ob->team_num != myteam) {
        Sint32 d = distance(ob);
        if (d < min_dist) { min_dist = d; closest = ob; }
    }
}

// After:
auto closest = std::min_element(oblist.begin(), oblist.end(),
    [&](const auto& a, const auto& b) {
        return distance(a) < distance(b);
    });
```

**Opportunities for `std::remove_if` / `std::erase_if`:**
```cpp
// Before (screen.cpp:821-845 - dead entity cleanup):
for(auto e = level_data.fxlist.begin(); e != level_data.fxlist.end();) {
    walker* ob = *e;
    if(ob && ob->dead) { delete ob; e = level_data.fxlist.erase(e); continue; }
    e++;
}

// After (C++20, with unique_ptr from Phase 4):
std::erase_if(level_data.fxlist, [](const auto& ob) { return ob && ob->dead; });
```

### 8.4 Use Structured Bindings for Map/Pair Iteration

**`obmap.cpp`** map iterations:
```cpp
// Before:
for(auto e = pos_to_walker.begin(); e != pos_to_walker.end(); e++)
{
    auto& walkers = e->second;
}

// After:
for(auto& [pos, walkers] : pos_to_walker)
{
    // use pos, walkers directly
}
```

### 8.5 Phase 8 Test Impact

**Risk to existing tests: LOW.** Container changes (`C array` → `std::array`) are largely internal to classes that tests exercise indirectly.

**Existing coverage:**
- `test_load_multiple_levels` — exercises `gloader.cpp` arrays (animation direction arrays, hitpoints array) being converted to `std::array`
- `test_level_data_integrity` — verifies loaded level grid data, which lives in arrays being converted

**New tests to add:**
- **Bounds-checking tests:** `std::array::at()` throws on out-of-bounds access (unlike C arrays which silently corrupt). Add tests that verify array accesses stay within bounds for palette buffers (768 entries), hitpoints arrays (200 entries), and animation tables.
- **`obmap` algorithm tests (8.3):** The `std::min_element` / `std::erase_if` replacements for manual find-nearest and dead-entity loops are behavior-critical. Add unit tests for `find_near_foe()` and `find_far_foe()` with known entity positions to verify the algorithmic replacements produce identical results.

**Coverage gaps exposed by Phase 8:**
- No tests for `video.cpp` palette/buffer operations — the video buffers being converted to `std::array` (8.1) are only exercised through rendering, which runs headless (offscreen) in tests
- No tests for `obmap.cpp` spatial indexing — collision detection is untested

---

## Phase 9: Concurrency & Threading

**Risk: LOW** | **Effort: LOW** | **Priority: OPTIONAL**

The game is single-threaded by design (standard for real-time game loops). No threading code exists.

### 9.1 Assessment

- No `std::thread`, `pthread`, `std::mutex`, or `std::atomic` usage found
- The Emscripten web build requires single-threaded operation
- The game loop (`glad.cpp` main loop → `screen::act()` → `screen::redraw()`) is sequential

### 9.2 Potential Opportunities (Future)

If performance becomes an issue:

**Asset loading:** Background thread for loading `PixieData` textures during level transitions.

```cpp
// Future possibility:
std::future<PixieData> async_load = std::async(std::launch::async, [&]() {
    return read_pixie_file(filename);
});
```

**Pathfinding:** `micropather` A* searches could run on a thread pool for multiple AI entities.

**Note:** Emscripten does support `std::thread` via SharedArrayBuffer, but this adds complexity and browser compatibility issues. Keep single-threaded unless profiling shows a clear need.

### 9.3 Use `std::atomic` for Shared State (If Threading Added)

If any threading is introduced, protect shared state:
```cpp
// Currently global:
extern screen * myscreen;
// Would need:
std::atomic<bool> level_loading{false};
```

---

## Appendix: File Impact Matrix

Files sorted by total modernization touches needed, from most to least impacted:

| File | Lines | NULL | Casts | sprintf | strcpy | new/del | Loops | Priority |
|------|-------|------|-------|---------|--------|---------|-------|----------|
| `walker.cpp` | 4848 | 62 | 30+ | 40+ | 15+ | 14 | 20+ | HIGH |
| `picker.cpp` | 4428 | 23 | 20+ | 20+ | 10+ | 30 | 25+ | HIGH |
| `level_editor.cpp` | 4247 | 29 | 30+ | 30+ | 10+ | 7 | 35+ | HIGH |
| `view.cpp` | 2281 | 12 | 15+ | 12 | 5+ | 3 | 15+ | MEDIUM |
| `video.cpp` | 1974 | 6 | 10+ | 2 | 0 | 7 | 10+ | MEDIUM |
| `screen.cpp` | 1419 | 29 | 15+ | 5 | 5+ | 21 | 30+ | HIGH |
| `level_data.cpp` | 1635 | 22 | 10+ | 4 | 0 | 18 | 15+ | HIGH |
| `io.cpp` | 1017 | 31 | 5+ | 8 | 0 | 3 | 10+ | MEDIUM |
| `stats.cpp` | 1160 | 8 | 5+ | 2 | 0 | 4 | 5+ | LOW |
| `gloader.cpp` | 1138 | 6 | 5+ | 1 | 0 | 15 | 5+ | MEDIUM |
| `button.cpp` | 678 | 20 | 15+ | 0 | 0 | 1 | 5+ | LOW |
| `results_screen.cpp` | 776 | 25 | 5+ | 6 | 0 | 1 | 5+ | LOW |
| `save_data.cpp` | 736 | 6 | 5+ | 0 | 15+ | 3 | 15+ | MEDIUM |
| `guy.cpp` | 615 | 1 | 5+ | 0 | 5+ | 3 | 5+ | LOW |
| `help.cpp` | 802 | 0 | 5+ | 1 | 0 | 2 | 5+ | LOW |
| `campaign_picker.cpp` | 602 | 12 | 5+ | 10 | 0 | 4 | 5+ | LOW |
| `level_picker.cpp` | 643 | 10 | 5+ | 8 | 0 | 6 | 15+ | LOW |
| `text.cpp` | 718 | 12 | 5+ | 5 | 0 | 0 | 5+ | LOW |
| `effect.cpp` | 739 | 4 | 10+ | 0 | 0 | 1 | 6 | LOW |
| `living.cpp` | 896 | 5 | 5+ | 0 | 0 | 4 | 5+ | LOW |
| `treasure.cpp` | 364 | 3 | 5+ | 4 | 0 | 3 | 3 | LOW |

### Headers Requiring Changes

| Header | Changes Needed |
|--------|---------------|
| `base.h` | `#pragma once`, typedefs, ~200 `#define` → `constexpr`/`enum class`, C includes |
| `walker.h` | `#pragma once`, encapsulation (70+ public members), `void*` typing |
| `screen.h` | `#pragma once`, smart pointers, encapsulation |
| `video.h` | `#pragma once`, `std::array` for buffers, Rule of Five |
| `view.h` | `#pragma once`, friend removal, `std::string` for text buffers |
| `stats.h` | `#pragma once`, `override`, const-correctness, `#define` → `constexpr` |
| `level_data.h` | `#pragma once`, `unique_ptr` for owned pointers |
| `guy.h` | `#pragma once`, `std::string name`, Rule of Five |
| `pixie.h` | `#pragma once`, Rule of Five |
| `pixien.h` | `#pragma once`, Rule of Five |
| `button.h` | `#pragma once`, encapsulation |
| `gloader.h` | `#pragma once`, smart pointers, `std::array`/`std::vector` |
| `input.h` | `#pragma once`, `std::array` |
| `io.h` | `#pragma once` |

---

## Recommended Execution Order

Phase 5 is split into two sub-phases because some changes are safe mechanical transforms while others interact with ownership:

- **Phase 5a** (safe mechanical): Range-based for loops over read-only iteration, `auto`, `constexpr`, `std::string_view`, structured bindings. These don't interact with ownership and can run in parallel with Phase 2.
- **Phase 5b** (ownership-dependent): Loop transforms that erase elements (`std::erase_if`), loops that interact with `unique_ptr` containers, and any loop change that depends on Phase 4's ownership model.

```
Phase 0 (Toolchain Requirements)
  └─> Phase 1 (Build System)
        └─> Phase 2 (Low-Hanging Fruit) ── safe, mechanical, no behavior change
              ├─> Phase 3 (Type Safety) ── moderate risk, file-by-file
              │     └─> Phase 4 (Smart Pointers) ── higher risk, needs careful testing
              │           ├─> Phase 5b (Ownership-dependent loops) ── erase_if, unique_ptr iteration
              │           ├─> Phase 7 (Class Design) ── highest risk, incremental
              │           └─> Phase 6 (Error Handling) ── depends on smart pointers
              ├─> Phase 5a (Safe mechanical idioms) ── parallel with Phase 2
              └─> Phase 8 (Containers) ── can parallel with 5a

Phase 9 (Concurrency) ── optional, only if profiling shows need
```

### Testing Strategy

The project has an existing automated test suite and CI pipeline that must remain green throughout all modernization phases.

#### Existing Test Infrastructure

**Test suite:** 25+ registered tests in `tests/`, built via `scripts/build_test.sh` with `-DTESTING`. Tests run headless (`SDL_VIDEODRIVER=offscreen`, `SDL_AUDIODRIVER=dummy`).

**Test categories and what they cover:**

| Category | Count | Coverage |
|----------|-------|----------|
| Startup & init | 2 | SDL init, screen object creation |
| Menu UI navigation | 10 | Main menu, difficulty, options, help, menu loop regression |
| Team management UI | 5 | Hire/view/train team, error handling (empty team) |
| Save/load system | 3 | State roundtrip, team persistence, level fallback |
| Character data | 5 | Guy creation/copy/leveling, campaign progress, save reset |
| Level loading | 3 | Multi-version scenarios, data integrity, nonexistent level fallback |
| Full game integration | 2 | Fairy death (run level, verify loss), overpowered team (hire all types, crank stats, verify win) |

**Test infrastructure features:**
- `TRACE("category", "message")` — instrumentation macro (no-op in production builds). Tests verify code paths via `trace_contains()` / `trace_count()`.
- `interact("button_id")` / `wait_for_interactable("id", timeout)` — UI interaction system for testing blocking menu code from background threads.
- `g_picker_max_mainmenu_calls` — loop iteration limit for menu tests.
- `cleanup_picker_state()` — cleanup helper for picker globals after menu tests.

**CI pipeline:** `.github/workflows/test.yml` runs on every push and pull request:
1. **Test job** — builds and runs all tests
2. **Native build job** — verifies `openglad` and `openscen` compile

#### Per-Phase Testing Requirements

After each sub-phase:
1. **All existing tests must pass.** Run `./scripts/build_test.sh && ./openglad_test`. No regressions.
2. **CI must be green.** Push to a branch, verify the GitHub Actions pipeline passes.
3. Compile both native (`build_native.sh`) and web (`build_web.sh`) targets.
4. Run with AddressSanitizer and UBSan (Phase 1 enables these in Debug mode).
5. For high-risk phases (4, 7), also do a manual smoke test: play through at least one complete level.

#### Tests to Add During Modernization

These are prioritized by phase dependency — add each set of tests as part of the corresponding phase's PR:

**Phase 3 (Type Safety):**
- Save file version migration test (v0 headerless → v1 with header)
- String boundary tests for `guy::name` (empty, max-length, over-length)
- `std::format` Emscripten compatibility check

**Phase 4 (Smart Pointers) — HIGHEST PRIORITY for new tests:**
- Entity lifecycle test (create → add to list → find → mark dead → cleanup → verify freed)
- Entity list transfer test (move `unique_ptr` between `oblist` and `dead_list`)
- `PixieData` move semantics test (verify `data_copy()` rewrite)
- Run `test_fairy_death` and `test_overpowered_team` under ASan after every sub-step

**Phase 7 (Encapsulation):**
- `static_assert(!std::is_copy_constructible_v<walker>)` for deleted copy
- Const-correctness compilation tests via `const walker*` references
- Update all existing tests that directly access public members being encapsulated

**Phase 8 (Containers):**
- `find_near_foe()` / `find_far_foe()` unit tests with known positions
- `obmap` collision detection unit tests
- Bounds-checking tests for converted `std::array` buffers

#### Coverage Gaps (Not Tested Today)

These areas of the codebase have **no automated test coverage** and are affected by modernization:

| Area | Affected Phases | Risk |
|------|----------------|------|
| `video.cpp` rendering / palette buffers | 8 (std::array) | Low — internal to rendering |
| `obmap.cpp` collision/spatial indexing | 4 (ownership), 8 (algorithms) | Medium — correctness-critical |
| `level_editor.cpp` (openscen) | 2-8 (all phases) | Medium — large file, no tests |
| Sound system (`soundob`) | 4 (unique_ptr) | Low — simple ownership |
| `pixie`/`pixieN` sprite lifecycle | 4 (RAII), 7 (Rule of Five) | Medium — complex ownership |
| `text.cpp` rendering | 3 (std::string), 5 (string_view) | Low — display only |
| `io.cpp` PhysFS layer | 3 (void* typing), 4 (malloc→unique_ptr) | Medium — file I/O correctness |

The level editor (`openscen`) is the largest untested file (147KB, 4247 lines). It is affected by every phase but has zero test coverage. Consider adding at least a startup/init test for the editor before beginning modernization.

### Estimated Scope

| Phase | Files Touched | Estimated Changes |
|-------|--------------|-------------------|
| Phase 1 | 3-5 | New CMakeLists.txt + script updates |
| Phase 2 | 38 headers + 36 cpp | ~500 mechanical replacements |
| Phase 3 | 30+ files | ~700 type/cast changes |
| Phase 4 | 15-20 files | ~200 ownership changes |
| Phase 5 | 25+ files | ~320 loop modernizations |
| Phase 6 | 10+ files | ~50 error handling changes |
| Phase 7 | 10-15 headers + impls | ~500+ encapsulation changes |
| Phase 8 | 15+ files | ~80 container changes |
| Phase 9 | 0-2 files | Optional |
