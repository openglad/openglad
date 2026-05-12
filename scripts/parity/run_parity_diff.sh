#!/usr/bin/env bash
# Convenience wrapper around scripts/parity/diff_dumps.py.
#
# Usage:
#   scripts/parity/run_parity_diff.sh <scenario_id> [branch_dump.json] [master_dump.json]
#
# When run with only a scenario id, defaults to comparing the freshly captured
# branch dump in build/ci-test/parity/<id>.json against the committed
# master golden at tests/parity/golden/<id>.json.

set -euo pipefail

if [[ $# -lt 1 || $# -gt 3 ]]; then
    echo "usage: $0 <scenario_id> [branch_dump.json] [master_dump.json]" >&2
    exit 2
fi

SCENARIO="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BRANCH_DUMP="${2:-${REPO_ROOT}/build/ci-test/parity/${SCENARIO}.json}"
MASTER_DUMP="${3:-${REPO_ROOT}/tests/parity/golden/${SCENARIO}.json}"

exec python3 "${SCRIPT_DIR}/diff_dumps.py" "${BRANCH_DUMP}" "${MASTER_DUMP}"
