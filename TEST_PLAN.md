# Test Coverage Improvement Plan

This document outlines options for adding automated test coverage to OpenGlad.

## Current State

**No automated tests exist.** The project has:
- No test directories or test files
- No testing frameworks integrated
- No CI/CD pipelines running tests
- No test targets in the build system (Autotools)

The codebase is approximately 42,500 lines of C++ code with no unit test coverage.

---

## Option 1: Unit Testing with Catch2 (Recommended Starting Point)

**Why Catch2:**
- Header-only library, easy to integrate with existing Autotools build
- Compatible with C++11 (which this project uses)
- No external dependencies to manage
- Well-documented and actively maintained

**Implementation steps:**
1. Download Catch2 single-header to `src/external/catch2/`
2. Create `tests/` directory for unit tests
3. Create `tests/Makefile.am` with test targets
4. Update `configure.ac` to include test directory
5. Add `make check` target for running tests

**Priority modules to test (pure logic, minimal SDL dependencies):**
- `stats.cpp` - Combat calculations and statistics
- `obmap.cpp` - Spatial indexing and collision detection
- `util.cpp` - Utility functions
- `level_data.cpp` - Level loading and parsing logic

**Pros:** Low barrier to entry, tests run without SDL initialization
**Cons:** Cannot test SDL/rendering code paths directly

---

## Option 2: GitHub Actions CI for Build Verification

**Implementation steps:**
1. Create `.github/workflows/build.yml`
2. Configure matrix build for native (Ubuntu) and web (Emscripten) targets
3. Verify both targets compile successfully on each push/PR

**Example workflow structure:**
```yaml
name: Build
on: [push, pull_request]
jobs:
  native-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: sudo apt-get install -y libsdl2-dev libsdl2-mixer-dev libpng-dev
      - name: Build
        run: |
          ./autogen.sh
          ./configure
          make

  web-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mymindstorm/setup-emsdk@v14
      - name: Build
        run: ./scripts/build_web.sh
```

**Pros:** Catches build regressions immediately, low effort
**Cons:** Only verifies compilation, not correctness

---

## Option 3: Snapshot/Golden Testing for Rendering

**Concept:** Render specific game states and compare against known-good reference images.

**Implementation steps:**
1. Create test harness that initializes game to specific states
2. Capture framebuffer output to image files
3. Compare against reference images using pixel diff
4. Flag significant differences for review

**Pros:** Tests actual rendering output, catches visual regressions
**Cons:** Brittle when visuals intentionally change, requires maintenance

---

## Option 4: Refactor for Testability + Mocking

**Concept:** Extract pure-logic functions from SDL-coupled code to enable comprehensive unit testing.

**Implementation steps:**
1. Identify tightly-coupled code in `walker.cpp`, `screen.cpp`, `view.cpp`
2. Extract pure-logic functions into separate modules
3. Create interfaces/abstract classes for video, input, audio subsystems
4. Implement mock versions for testing
5. Use dependency injection pattern

**Pros:** Enables comprehensive testing of game logic
**Cons:** Significant refactoring effort for legacy codebase

---

## Recommended Implementation Order

| Phase | Task | Effort | Value |
|-------|------|--------|-------|
| 1 | GitHub Actions CI for build verification | Low | High |
| 2 | Integrate Catch2, add tests for `stats.cpp` | Low | Medium |
| 3 | Add unit tests for `obmap.cpp` (collision) | Medium | Medium |
| 4 | Add integration tests for level loading | Medium | Medium |
| 5 | Snapshot testing for rendering (optional) | High | Low |

---

## Testable Code Candidates

These files contain logic that could be unit tested with minimal refactoring:

| File | Lines | Testable Logic |
|------|-------|----------------|
| `src/stats.cpp` | ~800 | Damage calculations, stat modifiers, level-up logic |
| `src/obmap.cpp` | ~400 | Spatial hashing, collision queries |
| `src/util.cpp` | ~300 | String utilities, math helpers |
| `src/level_data.cpp` | ~600 | Level file parsing, object instantiation |
| `src/parser.cpp` | ~400 | Configuration file parsing |

---

## Resources

- Catch2: https://github.com/catchorg/Catch2
- GitHub Actions: https://docs.github.com/en/actions
- Emscripten CI: https://emscripten.org/docs/compiling/Building-Projects.html
