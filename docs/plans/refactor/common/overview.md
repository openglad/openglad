# Test Infrastructure Refactor Plan

## Problem Statement

The current test infrastructure has several compounding issues:

1. **Monolithic binaries stuffed with unrelated tests.** `openglad_test` has 1239 tests,
   `og_runtime_tests` has 1047 (790 overlap with openglad_test), `og_data_tests` has 128
   (100% subset of openglad_test; 121 overlap with og_runtime_tests, 7 unique via
   test_util.cpp). Tests run sequentially within each binary — one hang kills the whole suite.

2. **No config isolation.** All test binaries share `~/.openglad/`. When CI runs
   `og_data_tests` then `og_runtime_tests` sequentially (or in parallel under ASan), they
   clobber each other's preferences, saves, and campaign data. The coverage workflow even has
   manual `rm -f "$HOME/.openglad/campaigns/org.openglad.test."*.glad` cleanup as a band-aid.

3. **442 orphaned tests.** `openglad_test` contains 442 tests not present in any module
   binary (`og_data_tests`, `og_runtime_tests`). But CI's `test.yml` only runs the module
   binaries — those 442 tests are **never executed in CI**.

4. **Massive redundancy.** `og_data_tests` (128 tests) is a 100% subset of `openglad_test`.
   `og_runtime_tests` shares 790 tests with `openglad_test`. CI runs the same tests multiple
   times while missing 442 others.

5. **Opaque failures.** When a test hangs, you see `[847/1047] test_foo ...` and then nothing
   for 10 minutes until the outer `timeout` kills the process. No way to know which test hung
   without reproducing locally.

## Goals

- **Every test runs exactly once** — no duplication, no orphans
- **Parallel execution** — CTest runs 24 binaries concurrently
- **Config isolation** — each binary gets its own temp directory, no `~/.openglad` clobbering
- **Per-binary timeouts** — 3 minutes each; a hang kills one group, not the whole suite
- **Standard test framework** — replace custom macros with GoogleTest (system dep, like SDL)
- **Coverage expanded** — remove gcovr directory excludes, backfill coverage to maintain thresholds
- **Total test count preserved** — 1787 tests (291 unit + 1496 integration)
- **Phased rollout** — each phase is independently testable and deployable

## Architecture

### Before

```
og_unit_tests      (291 tests, headless)
og_data_tests      (128 tests, subset of openglad_test)
og_runtime_tests   (1047 tests, mostly subset + 257 EXTRA)
openglad_test      (1239 tests, 442 unique to this binary)
─────────────────────────────────────
Total unique: 1787  |  Actually run in CI: 1345 (missing 442)
```

### After

```
og_unit_sim             ( 72 tests)  ─┐
og_unit_families        ( 83 tests)   ├── 4 unit groups (headless, no SDL)
og_unit_entity          ( 82 tests)   │
og_unit_data            ( 54 tests)  ─┘
og_test_walker_combat   ( 58 tests)  ─┐
og_test_walker_move     ( 36 tests)   │
og_test_walker_core     ( 98 tests)   │
og_test_families        ( 88 tests)   │
og_test_effects         ( 76 tests)   │
og_test_living          ( 90 tests)   │
og_test_stats           ( 79 tests)   │
og_test_guy             ( 76 tests)   │
og_test_game_core       ( 85 tests)   ├── 20 integration groups (SDL)
og_test_screen          ( 61 tests)   │
og_test_view            ( 78 tests)   │
og_test_rendering       (113 tests)   │
og_test_picker          ( 56 tests)   │
og_test_menu_ui         ( 38 tests)   │
og_test_input           ( 55 tests)   │
og_test_level           ( 94 tests)   │
og_test_io              (107 tests)   │
og_test_smooth          ( 57 tests)   │
og_test_external        ( 35 tests)   │
og_test_mass_coverage   (116 tests)  ─┘
────────────────────────────────────
24 binaries  |  Total: 1787  |  All run in CI: 1787
```

## Summary

| Metric | Before | After Phase 1 | After Phase 3 | After Phase 4 |
|--------|--------|---------------|---------------|---------------|
| Tests run in CI | 1345 | 1787 | 1787 | 1787 |
| Orphaned tests | 442 | 0 | 0 | 0 |
| Duplicate test runs | ~918 | 0 | 0 | 0 |
| Test binaries | 4 (overlapping) | 24 (disjoint) | 24 (disjoint) | 24 (disjoint) |
| Max binary size | 1239 tests | 116 tests | 116 tests | 116 tests |
| Test framework | Custom macros | Custom (GTest naming) | GoogleTest | GoogleTest |
| Config isolation | None | Per-process temp dir | Per-process temp dir | Per-process temp dir |
| Parallelism | Sequential | CTest --parallel | CTest --parallel | CTest --parallel |
| Per-binary timeout | None (10 min kill) | 180s per binary | 180s per binary | 180s per binary |
| Hang diagnosis | "somewhere in 1047" | "test_foo in og_test_Y" | "Suite.test in og_test_Y" | same |
| Test output | Custom stderr | Custom stderr | GoogleTest structured | same |
| Order dep detection | None | None | `--gtest_shuffle` | same |
| Coverage excludes | 5 directories | 5 directories | 5 directories | 1 (fuzz only) |
