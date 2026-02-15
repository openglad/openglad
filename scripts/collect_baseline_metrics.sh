#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build/baseline-metrics"
OUT_DIR="${BUILD_DIR}/metrics"
OUT_FILE="${OUT_DIR}/baseline_metrics.txt"

MAX_BUILD_SECONDS="${OPENGLAD_MAX_BUILD_SECONDS:-0}"
MAX_TEST_SECONDS="${OPENGLAD_MAX_TEST_SECONDS:-0}"
MAX_OPENG_LAD_BYTES="${OPENGLAD_MAX_OPENG_LAD_BYTES:-0}"
MAX_RUNTIME_TEST_BYTES="${OPENGLAD_MAX_RUNTIME_TEST_BYTES:-0}"
MAX_DATA_TEST_BYTES="${OPENGLAD_MAX_DATA_TEST_BYTES:-0}"

mkdir -p "${OUT_DIR}"

GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
fi

pushd "${PROJECT_ROOT}" >/dev/null

configure_start=$(date +%s)
cmake -S . -B "${BUILD_DIR}" -G "${GENERATOR}" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DBUILD_EDITOR=ON
configure_end=$(date +%s)

build_start=$(date +%s)
cmake --build "${BUILD_DIR}" --target openglad og_data_tests og_runtime_tests -j"$(nproc)"
build_end=$(date +%s)

runtime_test_start=$(date +%s)
"${BUILD_DIR}/og_runtime_tests" >/dev/null
runtime_test_end=$(date +%s)

data_test_start=$(date +%s)
"${BUILD_DIR}/og_data_tests" >/dev/null
data_test_end=$(date +%s)

openglad_size=$(stat -c%s "${BUILD_DIR}/openglad")
runtime_test_size=$(stat -c%s "${BUILD_DIR}/og_runtime_tests")
data_test_size=$(stat -c%s "${BUILD_DIR}/og_data_tests")

configure_seconds=$((configure_end - configure_start))
build_seconds=$((build_end - build_start))
runtime_test_seconds=$((runtime_test_end - runtime_test_start))
data_test_seconds=$((data_test_end - data_test_start))
total_test_seconds=$((runtime_test_seconds + data_test_seconds))

cat > "${OUT_FILE}" <<METRICS
OpenGlad baseline metrics
Date: $(date -u +"%Y-%m-%dT%H:%M:%SZ")

configure_seconds=${configure_seconds}
build_seconds=${build_seconds}
runtime_test_seconds=${runtime_test_seconds}
data_test_seconds=${data_test_seconds}
total_test_seconds=${total_test_seconds}

openglad_size_bytes=${openglad_size}
og_runtime_tests_size_bytes=${runtime_test_size}
og_data_tests_size_bytes=${data_test_size}

sanitizer_guardrail=ci-asan preset in .github/workflows/test.yml
METRICS

status=0
if [[ "${MAX_BUILD_SECONDS}" -gt 0 && "${build_seconds}" -gt "${MAX_BUILD_SECONDS}" ]]; then
    echo "Build guardrail exceeded: ${build_seconds}s > ${MAX_BUILD_SECONDS}s" >&2
    status=1
fi
if [[ "${MAX_TEST_SECONDS}" -gt 0 && "${total_test_seconds}" -gt "${MAX_TEST_SECONDS}" ]]; then
    echo "Test guardrail exceeded: ${total_test_seconds}s > ${MAX_TEST_SECONDS}s" >&2
    status=1
fi
if [[ "${MAX_OPENG_LAD_BYTES}" -gt 0 && "${openglad_size}" -gt "${MAX_OPENG_LAD_BYTES}" ]]; then
    echo "Binary size guardrail exceeded for openglad: ${openglad_size} > ${MAX_OPENG_LAD_BYTES}" >&2
    status=1
fi
if [[ "${MAX_RUNTIME_TEST_BYTES}" -gt 0 && "${runtime_test_size}" -gt "${MAX_RUNTIME_TEST_BYTES}" ]]; then
    echo "Binary size guardrail exceeded for og_runtime_tests: ${runtime_test_size} > ${MAX_RUNTIME_TEST_BYTES}" >&2
    status=1
fi
if [[ "${MAX_DATA_TEST_BYTES}" -gt 0 && "${data_test_size}" -gt "${MAX_DATA_TEST_BYTES}" ]]; then
    echo "Binary size guardrail exceeded for og_data_tests: ${data_test_size} > ${MAX_DATA_TEST_BYTES}" >&2
    status=1
fi

cat "${OUT_FILE}"

popd >/dev/null
exit "${status}"
