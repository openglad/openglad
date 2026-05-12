#!/usr/bin/env bash
# Capture master-side parity golden dumps for every master-comparable scenario.
#
# Drives the Phase 05 master companion binary
# (../openglad-master/build/ci-test/parity_dump_master, built on branch
# parity-companion in the sibling worktree) once per scenario, writes the
# canonical JSON to tests/parity/golden/<id>.json, and validates each dump
# against schema v1 via validate_schema.py.
#
# Usage:
#   scripts/parity/capture_master_golden.sh           # capture all scenarios
#   scripts/parity/capture_master_golden.sh <id>...   # capture specific scenarios
#
# Environment overrides:
#   MASTER_WORKTREE   path to the master worktree (default: ../openglad-master)
#   MASTER_BINARY     path to parity_dump_master   (default: $MASTER_WORKTREE/build/ci-test/parity_dump_master)
#   GOLDEN_DIR        destination directory       (default: tests/parity/golden)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

MASTER_WORKTREE="${MASTER_WORKTREE:-${REPO_ROOT}/../openglad-master}"
MASTER_BINARY="${MASTER_BINARY:-${MASTER_WORKTREE}/build/ci-test/parity_dump_master}"
GOLDEN_DIR="${GOLDEN_DIR:-${REPO_ROOT}/tests/parity/golden}"
VALIDATOR="${SCRIPT_DIR}/validate_schema.py"

if [[ ! -x "${MASTER_BINARY}" ]]; then
    echo "error: master companion binary not found or not executable:" >&2
    echo "       ${MASTER_BINARY}" >&2
    echo "" >&2
    echo "Build it from the master worktree:" >&2
    echo "  cd ${MASTER_WORKTREE} && git checkout parity-companion" >&2
    echo "  cmake --build --preset ci-test --target parity_dump_master" >&2
    exit 2
fi

if [[ ! -f "${VALIDATOR}" ]]; then
    echo "error: validator not found: ${VALIDATOR}" >&2
    exit 2
fi

mkdir -p "${GOLDEN_DIR}"

if [[ $# -gt 0 ]]; then
    SCENARIOS=("$@")
else
    mapfile -t SCENARIOS < <("${MASTER_BINARY}" --list)
fi

if [[ ${#SCENARIOS[@]} -eq 0 ]]; then
    echo "error: no scenarios resolved from master companion --list" >&2
    exit 2
fi

failures=0
for id in "${SCENARIOS[@]}"; do
    out="${GOLDEN_DIR}/${id}.json"
    printf 'capturing %-32s -> %s\n' "${id}" "${out}"
    if ! "${MASTER_BINARY}" --scenario "${id}" --out "${out}"; then
        echo "  FAIL: parity_dump_master --scenario ${id} exited non-zero" >&2
        failures=$((failures + 1))
        continue
    fi
    if ! python3 "${VALIDATOR}" "${out}" >/dev/null; then
        echo "  FAIL: schema validation rejected ${out}" >&2
        failures=$((failures + 1))
    fi
done

if [[ ${failures} -gt 0 ]]; then
    echo "${failures} scenario(s) failed capture or validation" >&2
    exit 1
fi

echo "captured ${#SCENARIOS[@]} scenario(s) into ${GOLDEN_DIR}"
