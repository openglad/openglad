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
#   scripts/parity/capture_master_golden.sh --all
#   scripts/parity/capture_master_golden.sh <id>...
#   scripts/parity/capture_master_golden.sh --all --no-write --diff
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
SCENARIO_LISTER="${SCRIPT_DIR}/check_coverage_manifest.py"

usage() {
    cat >&2 <<EOF
usage: ${0##*/} [--all] [--out-dir PATH] [--no-write --diff] [id...]

  --all          capture every SemanticParity scenario that is not branch-internal
  --out-dir PATH write goldens under PATH (default: tests/parity/golden)
  --no-write     capture into a temporary directory instead of PATH
  --diff         with --no-write, diff PATH against the temporary capture
EOF
}

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

if [[ ! -f "${SCENARIO_LISTER}" ]]; then
    echo "error: scenario metadata helper not found: ${SCENARIO_LISTER}" >&2
    exit 2
fi

semantic_diff_check() {
    local failures=0
    local id
    local committed
    local recaptured

    for id in "${SCENARIOS[@]}"; do
        committed="${GOLDEN_DIR}/${id}.json"
        recaptured="${OUTPUT_DIR}/${id}.json"

        if [[ ! -s "${committed}" ]]; then
            echo "  FAIL: committed golden missing or empty: ${committed}" >&2
            failures=$((failures + 1))
            continue
        fi
        if [[ ! -s "${recaptured}" ]]; then
            echo "  FAIL: recaptured golden missing or empty: ${recaptured}" >&2
            failures=$((failures + 1))
            continue
        fi
        if ! python3 "${VALIDATOR}" "${committed}" >/dev/null; then
            echo "  FAIL: schema validation rejected committed ${committed}" >&2
            failures=$((failures + 1))
            continue
        fi
        if ! python3 "${VALIDATOR}" "${recaptured}" >/dev/null; then
            echo "  FAIL: schema validation rejected recaptured ${recaptured}" >&2
            failures=$((failures + 1))
            continue
        fi
    done

    if [[ ${failures} -ne 0 ]]; then
        return 1
    fi

    python3 - "${GOLDEN_DIR}" "${OUTPUT_DIR}" "${SCENARIOS[@]}" <<'PY'
import json
import sys
from pathlib import Path

committed_dir = Path(sys.argv[1])
recaptured_dir = Path(sys.argv[2])
scenario_ids = sys.argv[3:]
volatile_abs_min = 1_000_000
failures: list[str] = []


def load(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def normalize(doc: dict) -> dict:
    for bucket in ("effects", "weapons"):
        for entry in doc.get(bucket, []):
            lifetime = entry.get("lifetime")
            if isinstance(lifetime, int) and abs(lifetime) >= volatile_abs_min:
                entry["lifetime"] = 0
    return doc


for scenario_id in scenario_ids:
    left_path = committed_dir / f"{scenario_id}.json"
    right_path = recaptured_dir / f"{scenario_id}.json"
    try:
        left = normalize(load(left_path))
        right = normalize(load(right_path))
    except Exception as exc:  # noqa: BLE001 - shell caller reports this as a verifier failure.
        failures.append(f"{scenario_id}: could not load normalized JSON: {exc}")
        continue
    if left != right:
        failures.append(
            f"{scenario_id}: differs after normalizing volatile lifetime values"
        )

if failures:
    for failure in failures:
        print(f"  FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print(
    "normalized semantic diff: OK "
    f"({len(scenario_ids)} scenario(s); abs(lifetime)>={volatile_abs_min} ignored)"
)
PY
}

capture_all=false
no_write=false
diff_mode=false
SCENARIOS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)
            capture_all=true
            shift
            ;;
        --out-dir)
            if [[ $# -lt 2 ]]; then
                echo "error: --out-dir requires a path" >&2
                usage
                exit 2
            fi
            GOLDEN_DIR="$2"
            shift 2
            ;;
        --no-write)
            no_write=true
            shift
            ;;
        --diff)
            diff_mode=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            echo "error: unknown option: $1" >&2
            usage
            exit 2
            ;;
        *)
            SCENARIOS+=("$1")
            shift
            ;;
    esac
done

if [[ "${capture_all}" == true && ${#SCENARIOS[@]} -gt 0 ]]; then
    echo "error: --all cannot be combined with explicit scenario ids" >&2
    exit 2
fi

if [[ "${no_write}" == true && "${diff_mode}" != true ]]; then
    echo "error: --no-write must be combined with --diff" >&2
    exit 2
fi

if [[ "${diff_mode}" == true && "${no_write}" != true ]]; then
    echo "error: --diff must be combined with --no-write" >&2
    exit 2
fi

if [[ "${capture_all}" != true && ${#SCENARIOS[@]} -eq 0 ]]; then
    capture_all=true
fi

if [[ "${capture_all}" == true ]]; then
    declare -A semantic_ids=()
    declare -A listed_ids=()

    while IFS=$'\t' read -r id compare_mode is_branch_internal extra; do
        [[ -z "${id}" ]] && continue
        if [[ -n "${extra:-}" || -z "${compare_mode}" || -z "${is_branch_internal}" ]]; then
            echo "error: malformed scenario metadata row for ${id}" >&2
            exit 2
        fi
        if [[ "${compare_mode}" == "SemanticParity" && "${is_branch_internal}" == "false" ]]; then
            semantic_ids["${id}"]=1
        fi
    done < <(python3 "${SCENARIO_LISTER}" --emit-scenario-list)

    while IFS= read -r id; do
        [[ -z "${id}" ]] && continue
        listed_ids["${id}"]=1
        if [[ -n "${semantic_ids[${id}]+x}" ]]; then
            SCENARIOS+=("${id}")
        fi
    done < <("${MASTER_BINARY}" --list)

    missing=0
    for id in "${!semantic_ids[@]}"; do
        if [[ -z "${listed_ids[${id}]+x}" ]]; then
            echo "error: SemanticParity scenario missing from master companion --list: ${id}" >&2
            missing=$((missing + 1))
        fi
    done
    if [[ ${missing} -gt 0 ]]; then
        exit 2
    fi
fi

if [[ ${#SCENARIOS[@]} -eq 0 ]]; then
    echo "error: no scenarios resolved for capture" >&2
    exit 2
fi

OUTPUT_DIR="${GOLDEN_DIR}"
TMP_DIR=""

cleanup() {
    if [[ -n "${TMP_DIR}" ]]; then
        rm -rf "${TMP_DIR}"
    fi
}
trap cleanup EXIT

if [[ "${no_write}" == true ]]; then
    TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/openglad-golden.XXXXXX")"
    mkdir -p "${TMP_DIR}"
    if [[ -d "${GOLDEN_DIR}" ]]; then
        cp -a "${GOLDEN_DIR}/." "${TMP_DIR}/"
    fi
    OUTPUT_DIR="${TMP_DIR}"
else
    mkdir -p "${OUTPUT_DIR}"
fi

failures=0
for id in "${SCENARIOS[@]}"; do
    out="${OUTPUT_DIR}/${id}.json"
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

echo "captured ${#SCENARIOS[@]} scenario(s) into ${OUTPUT_DIR}"

if [[ "${diff_mode}" == true ]]; then
    echo "diffing ${GOLDEN_DIR} against ${OUTPUT_DIR}"
    diff_rc=0
    diff -ru "${GOLDEN_DIR}" "${OUTPUT_DIR}" || diff_rc=$?
    if [[ ${diff_rc} -eq 0 ]]; then
        exit 0
    fi
    if [[ ${diff_rc} -gt 1 ]]; then
        exit "${diff_rc}"
    fi

    echo "byte differences detected; checking for lifetime-only semantic drift"
    if semantic_diff_check; then
        echo "semantic diff check passed for ${#SCENARIOS[@]} scenario(s)"
        exit 0
    fi
    echo "semantic diff check failed" >&2
    exit 1
fi
