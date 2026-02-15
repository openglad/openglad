#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ALLOWLIST="${PROJECT_ROOT}/cmake/graph_h_allowlist.txt"

if [[ ! -f "${ALLOWLIST}" ]]; then
    echo "Missing allowlist: ${ALLOWLIST}" >&2
    exit 1
fi

# Strip comments and blank lines from allowlist into a temp file for grep
ALLOWED_ENTRIES=$(sed '/^[[:space:]]*$/d; /^#/d' "${ALLOWLIST}")

include_pattern_legacy_quoted='^[[:space:]]*#include[[:space:]]*"graph\.h"'
include_pattern_public_angle='^[[:space:]]*#include[[:space:]]*<openglad/legacy/graph\.h>'

status=0
while IFS= read -r -d '' file; do
    if grep -qE "${include_pattern_legacy_quoted}|${include_pattern_public_angle}" "${file}"; then
        rel="${file#${PROJECT_ROOT}/}"
        if [ -z "${ALLOWED_ENTRIES}" ] || ! echo "${ALLOWED_ENTRIES}" | grep -Fxq "${rel}"; then
            echo "New disallowed graph.h include in ${rel}" >&2
            status=1
        fi
    fi
done < <(find "${PROJECT_ROOT}/src" -type f -name '*.cpp' ! -path "${PROJECT_ROOT}/third_party/*" -print0)

# Check for stale allowlist entries
if [ -n "${ALLOWED_ENTRIES}" ]; then
    while IFS= read -r rel; do
        file="${PROJECT_ROOT}/${rel}"
        if [[ ! -f "${file}" ]]; then
            echo "Stale graph.h allowlist entry (missing file): ${rel}" >&2
            status=1
            continue
        fi
        if ! grep -qE "${include_pattern_legacy_quoted}|${include_pattern_public_angle}" "${file}"; then
            echo "Stale graph.h allowlist entry (include removed): ${rel}" >&2
            status=1
        fi
    done <<< "${ALLOWED_ENTRIES}"
fi

if [[ ${status} -ne 0 ]]; then
    echo "graph.h include guard failed. Keep only transitional allowlist entries." >&2
    exit 1
fi
