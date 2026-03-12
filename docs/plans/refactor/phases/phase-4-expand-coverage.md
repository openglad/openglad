# Phase 4: Expand Coverage

**Goal:** Remove the gcovr directory excludes that currently hide large portions of
the codebase from coverage measurement. Backfill test coverage as needed to maintain
the existing threshold requirements (line 85%, function 90%).

## Current Excludes

The `coverage.yml` gcovr config currently excludes:
- `src/interface/` — screen, viewscreen, level runtime, input, buttons
- `src/platform/sdl/` — game entry, video, audio
- `src/ui/` — picker, menus, level editor, intro
- `src/sdl_client/` — text client
- `src/fuzz/` — fuzz targets
- `src/test_trace.cpp` — test infrastructure

## Approach

Remove excludes incrementally, one directory at a time. For each:

1. Remove the exclude from gcovr config
2. Measure the coverage drop
3. Add targeted tests to recover the threshold
4. Verify CI coverage job passes

Suggested order (smallest coverage gap first):
1. `src/test_trace.cpp` — trivial, already well-exercised by tests
2. `src/sdl_client/` — small, has dedicated text client tests
3. `src/platform/sdl/` — game entry + video, exercised by integration tests
4. `src/ui/` — menus/picker, exercised by Group 13-14 tests
5. `src/interface/` — largest, includes screen/view/input

`src/fuzz/` should remain excluded (fuzz targets aren't test code).

## Verification

1. Coverage thresholds maintained (line 85%, function 90%) after each exclude removal
2. No regressions in existing tests
