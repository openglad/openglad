# Risk Mitigation

**Order-dependent tests:** Some tests assume state from prior tests (e.g.,
`cleanup_picker_state()` patterns). Splitting into smaller groups in Phase 1 may
surface these. Fix by adding proper setup/teardown. Run `--gtest_shuffle` during
Phase 3 verification to proactively find more.

**Mechanical rewrite errors (Phase 2):** The `REGISTER_TEST` -> `TEST()` and
`TEST_ASSERT` -> `ASSERT_*` rewrites are repetitive but error-prone in bulk. Mitigate
by scripting the transform (sed/python) and verifying test counts match exactly after
the rewrite. The compatibility macros ensure the rewritten tests still compile and
run against the custom framework before GTest is introduced.

**Build time:** 24 link steps instead of 4. Each integration binary links against
`og_game_test` (already built once as a static lib, now includes `glad.cpp`); each
unit binary links against `og_game`. Incremental cost is just linking. Ninja
parallelizes this. GoogleTest itself (Phase 3) is pre-built from the system package —
no compile overhead.

**Thread-based interactive tests in Group 14 (menu_ui):** These are the most likely to
hang. With 38 tests in the group and a 3-minute timeout, a hang is immediately
attributable. If this group proves chronically flaky, it can be further split.

**Phase 2 compatibility fidelity:** The custom compatibility macros approximate but
don't perfectly replicate GTest behavior. Key difference: `TEST_F` in the compatibility
layer calls `SetUp()`/`TearDown()` as regular methods, not via GTest's test runner.
This is sufficient for the 6 fixture-based tests and is replaced by real GTest in
Phase 3.
