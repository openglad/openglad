# ASan Investigation: Walker/Obmap UAF (2026-02-13)

This note captures a real ASan failure encountered while running the test
suite under the `ci-asan` preset, and the minimal repro and fix direction.

## Symptom

`ctest --preset ci-asan` failed in `openglad_test` with ASan `heap-use-after-free`.

The first observed failure was in the walker tests, where a freed `walker*`
was still reachable from collision/passability checks via the `obmap` buckets.

The corresponding logs were saved during debugging as:

- `/tmp/asan_full_walker.log` (walker subset failure)
- `/tmp/asan_walker_after_death_cleanup.log` (still failing after partial fixes)

## Minimal Repro

Build and run ASan tests:

```bash
cmake --preset ci-asan
cmake --build --preset ci-asan -j "$(nproc)"
ctest --preset ci-asan --output-on-failure
```

Fast iteration on walker tests (test binary supports substring filtering):

```bash
./build/ci-asan/openglad_test walker
./build/ci-asan/openglad_test death_fire_elemental2,walkstep_cardinals
```

## Key ASan Frames (Walker UAF)

READ site:

- `walker::is_friendly` (`src/entities/walker.cpp`)
- `ob_pass_check` -> `obmap::query_list` (`src/entities/obmap.cpp`)
- `screen::query_object_passable` (`src/runtime/screen.cpp`)
- `living::walk` -> `walker::walkstep` (`src/entities/living.cpp`, `src/entities/walker_movement.cpp`)
- `test_walker_walkstep_cardinals` (`tests/test_walker_movement.cpp`)

FREE site:

- `living::~living` (`src/entities/living.cpp`)
- `test_walker_death_fire_elemental2` (unique_ptr destructor) (`tests/test_walker_death.cpp`)

ALLOC site:

- `loader::create_walker_owned` (`src/data/gloader.cpp`)
- `guy::create_walker_owned` (`src/entities/guy.cpp`)
- `test_walker_death_fire_elemental2` (`tests/test_walker_death.cpp`)

## Secondary ASan Finding (List Iterator UAF)

After making collision cleanup more aggressive, ASan also reported a UAF on a
`std::list<walker*>` node during iteration inside `ob_pass_check`.

Cause:

- Collision handling can remove objects from the same `obmap` pile while
  iterating it (e.g. treasure `eat_me` triggers `death` -> `obmap::remove`),
  invalidating the current iterator.

Fix direction:

- Iterate the pile with an explicit iterator and advance it before calling into
  code paths that may mutate the pile.

## Resolution Summary

Fixes were applied to ensure:

- Tests do not leak global screen/LevelData state across cases (per-test cleanup).
- `ob_pass_check` iteration is robust to pile mutation.
- `LevelData::mysmoother` stays in sync with grid buffer lifetimes.

