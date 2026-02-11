# Baseline Metrics Guardrails

Use `scripts/collect_baseline_metrics.sh` to capture the Phase 0 baseline metrics defined in `MODERNIZATION_PLAN.md`.

Captured metrics:
- Configure time
- Build time (targeted: `openglad`, `og_data_tests`, `og_runtime_tests`)
- Test duration (`og_data_tests`, `og_runtime_tests`)
- Binary sizes (`openglad`, `og_data_tests`, `og_runtime_tests`)
- Sanitizer guardrail status (tracked by CI `ci-asan` job)

## Local usage

```bash
./scripts/collect_baseline_metrics.sh
```

Optional hard guardrails (non-zero values enforce failure):

```bash
OPENGLAD_MAX_BUILD_SECONDS=300 \
OPENGLAD_MAX_TEST_SECONDS=120 \
OPENGLAD_MAX_OPENG_LAD_BYTES=7000000 \
OPENGLAD_MAX_RUNTIME_TEST_BYTES=9000000 \
OPENGLAD_MAX_DATA_TEST_BYTES=9000000 \
./scripts/collect_baseline_metrics.sh
```

Output is written to `build/baseline-metrics/metrics/baseline_metrics.txt`.
