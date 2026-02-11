#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ALLOWLIST="${PROJECT_ROOT}/cmake/graph_h_allowlist.txt"

if [[ ! -f "${ALLOWLIST}" ]]; then
    echo "Missing allowlist: ${ALLOWLIST}" >&2
    exit 1
fi

declare -A allowed=()
while IFS= read -r line; do
    [[ -z "${line}" ]] && continue
    [[ "${line}" =~ ^# ]] && continue
    allowed["${line}"]=1
done < "${ALLOWLIST}"

status=0
while IFS= read -r file; do
    rel="${file#${PROJECT_ROOT}/}"
    if [[ -z "${allowed[${rel}]:-}" ]]; then
        echo "New disallowed graph.h include in ${rel}" >&2
        status=1
    fi
done < <(rg -l '^\s*#include\s+"graph\.h"' "${PROJECT_ROOT}/src"/*.cpp)

for rel in "${!allowed[@]}"; do
    file="${PROJECT_ROOT}/${rel}"
    if [[ ! -f "${file}" ]]; then
        echo "Stale graph.h allowlist entry (missing file): ${rel}" >&2
        status=1
        continue
    fi
    if ! rg -q '^\s*#include\s+"graph\.h"' "${file}"; then
        echo "Stale graph.h allowlist entry (include removed): ${rel}" >&2
        status=1
    fi
done

if [[ ${status} -ne 0 ]]; then
    echo "graph.h include guard failed. Keep only transitional allowlist entries." >&2
    exit 1
fi
