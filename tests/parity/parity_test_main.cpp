// Parity harness GoogleTest entry point.
//
// Intentionally empty: the og_test_parity binary reuses
// tests/integration_main.cpp (which initialises SDL, PhysFS, and the
// integration test cleanup listener) so the parity tests share the same
// init/teardown surface as every other og_test_* group. This file exists
// only as the documented anchor referenced by .plan/parity-harness-design.md;
// adding a competing main() here would conflict with integration_main.cpp.
