# Codebase Reduction Audit (Estimated vs Actual)

## Scope and method
- Branch audited: `analysis/codebase-size-reduction` (synced with `origin/analysis/codebase-size-reduction` on 2026-02-23).
- Baseline estimate source: `CODEBASE_REDUCTION_ANALYSIS.md`.
- Implementation mapping method:
  - Located implementation commits by `git log master..HEAD --oneline` and `Finding N:` commit subjects.
  - Calculated line deltas from each commit with `git show --numstat <commit>` and summed by finding.
  - Included fix/revert commits in the same finding total (Finding 2 fix; Findings 20/30 reverts).
- Line accounting convention used in this report:
  - `net = added - removed`.
  - Negative net means codebase reduction.

## High-level result
- Estimated reduction from findings with line estimates (1-27, 29; excluding 28 informational and 20/30 disk-only): **~4,698 to ~5,298 lines**.
- Actual from implemented findings on this branch (including fixes/reverts): **-2,053 net lines**.
- Gap to estimate: **~2,645 to ~3,245 lines**.

Why the user-visible branch delta is only about ~1,300:
- `git diff --numstat master...HEAD` is **+8,453 / -9,732 / net -1,279**.
- Findings-only work is `-2,053`, but this branch also includes:
  - `a8327e0` adding `CODEBASE_REDUCTION_ANALYSIS.md` (**+782** lines), and
  - `78309da` unrelated cleanup (**-8** lines).
- `-2,053 + 782 - 8 = -1,279`.

## Per-finding comparison

| # | Finding | Estimated reduction | Commit(s) | Actual net (added-removed) | Status | Notes |
|---:|---|---:|---|---:|---|---|
| 1 | Table-driven `gloader.cpp` | 290 | `71e05be` | -274 | Implemented | Close to estimate; small helper/data additions offset some removals. |
| 2 | Consolidate versioned test files | 2,500-3,100 | `91a2137`, `748019a` | -353 | Implemented | Largest miss. Consolidation still required large merged test files; boilerplate removal was far less than projected. |
| 3 | Deduplicate `level_up()` | 140 | `121ff51` | -134 | Implemented | Near estimate. |
| 4 | Deduplicate `set_difficulty()` | 50 | `6a93ed7` | -15 | Implemented | Helper extraction added structure; fewer net removals than expected. |
| 5 | Deduplicate `check_special_ai()` | 48 | `65cf504` | -19 | Implemented | Same pattern as #4: reusable helper but modest net deletion. |
| 6 | Remove `MenuNav` factories | 84 | `84b08ae` | -98 | Implemented | Exceeded estimate. |
| 7 | Remove ButtonAction wrappers/aliases | 103 | `25b8010` | -106 | Implemented | Exceeded estimate slightly. |
| 8 | Remove bool I/O wrappers | 72 | `7746aac` | -57 | Implemented | Good reduction, but short of estimate after call-site updates. |
| 9 | Remove dead code in `walker.cpp` | 37 | `a4ec52f` | -40 | Implemented | Slightly exceeded estimate. |
| 10 | Remove commented-out blocks | 170 | _(none)_ | 0 | Not implemented | No `Finding 10:` commit present on branch. |
| 11 | Refactor `smooth.cpp` switches/tables | 200 | `0ff79be` | -276 | Implemented | Exceeded estimate significantly. |
| 12 | Text formatting helper | 50 | `1b103cd` | -43 | Implemented | Close to estimate. |
| 13 | Potion notification helper | 20 | `315336b` | -14 | Implemented | Partial net reduction. |
| 14 | Entity name helper | 24 | `61346c0` | -7 | Implemented | Small net reduction due to helper/call-site overhead. |
| 15 | Pathfinding macro dedup | 6 | `0870998` | +9 | Implemented (net increase) | Shared header introduced more lines than removed at call sites. |
| 16 | Remove legacy OuyaController code | 133 | `339b6b3` | -180 | Implemented | Exceeded estimate materially. |
| 17 | Template-ify registries | 200 | `0621bcd` | -88 | Implemented | Structural abstraction added template/base code, cutting net savings. |
| 18 | Consolidate small registry headers | 30 | `2c214f5` | -27 | Implemented | Close to estimate. |
| 19 | Inline trivial input wrappers | 130 | `ea7ba3d` | -70 | Implemented | Moderate shortfall from call-site and API-shape changes. |
| 20 | Remove 13 unreferenced `.pix` files | 13 files / 38 KB | `f76f687`, `93894a5` | 0 | Reverted | Implemented then reverted; net zero contribution. |
| 21 | Delete `graph.h` umbrella | 36 | `4051a8a` | -97 | Implemented | Exceeded estimate because related legacy check script/allowlist cleanups were also removed. |
| 22 | Extract `summon_entity()` helper | 30 | `9a1c954` | +1 | Implemented (net increase) | New shared header/helper outweighed per-site deletions. |
| 23 | Build script boilerplate extraction | 80 | `769591a` | -3 | Implemented | Very small net gain; factoring into common script mostly moved lines rather than deleting them. |
| 24 | Derive CMake test lists | 70 entries | `88d5336` | +22 | Implemented (net increase) | CMake list-derivation logic is more explicit/maintainable but longer in source lines. |
| 25 | Remove transitional `GameContext` wrapper | 100 | `75c1828` | -142 | Implemented | Exceeded estimate. |
| 26 | Remove registry init guards | 25 | `3ae815c` | +5 | Implemented (net increase) | Added startup init callsites and declarations offset guard removals. |
| 27 | Remove trivial accessors | 40 | `f71016d` | -36 | Implemented | Close to estimate. |
| 28 | GPL header overhead (informational) | N/A (do not remove) | _(skipped)_ | 0 | Skipped | Correctly skipped; non-actionable policy item. |
| 29 | Foe-finding iteration helper | 30 | `e9aa5cb` | -11 | Implemented | Refactoring overhead reduced net deletion. |
| 30 | Symlink asset staging | Disk only (~500 MB) | `167c37b`, `22ebf72` | 0 | Reverted | Implemented then reverted; no line impact. |

## Biggest shortfalls

By minimum estimate shortfall (`estimated_min - actual_reduction`, where actual reduction is `-net`):

1. **Finding 2:** shortfall **~2,147 lines** (`2500` est vs `353` actual)
2. **Finding 10:** shortfall **170 lines** (`170` est vs `0` actual; not implemented)
3. **Finding 17:** shortfall **112 lines** (`200` est vs `88` actual)
4. **Finding 24:** shortfall **92 lines** (`70` est vs net increase of `22`)
5. **Finding 23:** shortfall **77 lines** (`80` est vs `3` actual)
6. **Finding 19:** shortfall **60 lines** (`130` est vs `70` actual)

## Why the estimate missed

Common causes seen across commits:

1. **Optimistic estimate quality (especially Finding 2).**
   - The estimate assumed substantial duplicated test boilerplate could be removed during consolidation.
   - Actual consolidation created large canonical test files and preserved many assertions/setup paths, so gross adds remained high.

2. **Refactor overhead offsetting deletions.**
   - Several findings introduced new helper headers, templates, or list-derivation scaffolding (#15, #22, #24, #26), causing net growth or minimal shrink.

3. **Work was not fully executed for at least one planned item.**
   - Finding 10 has no implementation commit on this branch.

4. **Reverts removed expected contribution.**
   - Findings 20 and 30 were explicitly reverted, resulting in net zero branch impact from those items.

5. **Some estimates were conservative and were beaten.**
   - A few findings overdelivered (#11, #16, #21, #25), but these gains were not enough to offset the large misses above.

## Totals recap

- Estimated reducible lines (line-based findings): **~4,698 to ~5,298**.
- Actual from findings implemented on this branch (with fixes/reverts): **2,053 lines removed net**.
- Remaining gap to estimate: **~2,645 to ~3,245 lines**.
- Observable branch delta vs `master`: **1,279 lines removed net** (after including analysis-doc addition and unrelated cleanup).
