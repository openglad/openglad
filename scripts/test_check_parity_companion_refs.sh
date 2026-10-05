#!/usr/bin/env bash
# The teeth of scripts/check_parity_companion_refs.sh, on a temp repository.
#
# The temp repo plays both trees: branch `old` is the old game (an e761
# rebuild commit, then ports), `refs/remotes/origin/parity-companion` is what
# was pushed, and the work tree is the current game (ledger + table). Each
# case breaks one thing the gate exists for and pins the exit code and the
# named line: 0 clean / skipped, 1 violation, 2 cannot run.
set -euo pipefail

SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK="${SCRIPTS_DIR}/check_parity_companion_refs.sh"
tmp="$(mktemp -d)"
trap 'rm -rf -- "${tmp}"' EXIT INT TERM
out="${tmp}/stdout"; err="${tmp}/stderr"; rc=0; CASES=0
unset GITHUB_ACTIONS OG_PARITY_REQUIRE_REF OG_PARITY_COMPANION_REF

run_check() {  # run_check <root> [ledger]
    rc=0
    bash "${CHECK}" "$@" >"${out}" 2>"${err}" || rc=$?
}
fail() {
    echo "FAIL case $1: $2 (rc ${rc})" >&2
    sed 's/^/  out: /' "${out}" >&2; sed 's/^/  err: /' "${err}" >&2
    exit 1
}
expect_rc() { [[ "${rc}" -eq "$2" ]] || fail "$1" "expected rc $2"; }
expect_out() { grep -qF -- "$2" "${out}" || fail "$1" "stdout lacks '$2'"; }
expect_err() { grep -qF -- "$2" "${err}" || fail "$1" "stderr lacks '$2'"; }
pass() { CASES=$((CASES + 1)); echo "PASS case ${CASES}"; }

g() { git -C "${REPO}" -c user.name=t -c user.email=t@t "$@"; }

REPO="${tmp}/repo"
mkdir -p "${REPO}"
g init -q -b current
# the old game: e761 rebuild, then one port
g checkout -q -b old
mkdir -p "${REPO}/src" "${REPO}/tools"
echo base > "${REPO}/src/walker.cpp"
echo 'table v1' > "${REPO}/tools/parity_scenario_table.h"
g add src tools; g commit -q -m "Rebuild parity companion from e761 baseline"
BASE="$(g rev-parse --short=8 HEAD)"
echo port1 >> "${REPO}/src/walker.cpp"
g commit -q -am "Port fix one"
PORT1="$(g rev-parse --short=8 HEAD)"
echo port2 >> "${REPO}/src/walker.cpp"
g commit -q -am "Port fix two"
PORT2="$(g rev-parse --short=8 HEAD)"
# pushed: only the first port
g update-ref refs/remotes/origin/parity-companion "${PORT1}"
# the current game's work tree
g checkout -q --orphan current2
g rm -rq --cached . ; rm -rf "${REPO}/src" "${REPO}/tools"
mkdir -p "${REPO}/tests/parity/golden"
echo 'table v1' > "${REPO}/tests/parity/scenario_table.h"
export OG_PARITY_E761_REBUILD="${BASE}"

write_ledger() {  # write_ledger <pin> <cell> [<cell>...]
    local pin="$1"; shift
    {
        printf '# Golden ledger\n\nOld-game commit the goldens are captured from: `%s`\n\n' "${pin}"
        printf '| id | old-game commit | note |\n|---|---|---|\n'
        printf '| base | `%s` | the rebuild |\n' "${BASE}"
        for c in "$@"; do printf '| x | %s | y |\n' "${c}"; done
        printf '\n| id | differs | evidence |\n|---|---|---|\n'
        printf '| `a` | `[7,23]` vs `deadbeef` \\| x | z |\n'
    } > "${REPO}/tests/parity/golden/DRIFT_LEDGER.md"
}

# case 1: clean (the pin is the pushed tip)
write_ledger "${PORT1}" "\`${PORT1}\`" 'none' '—'
run_check "${REPO}"; expect_rc 1 0; expect_out 1 'check_parity_companion_refs: OK'; pass

# case 2: a port committed locally and never pushed
write_ledger "${PORT2}" "\`${PORT1}\`" "\`${PORT2}\`"
run_check "${REPO}"; expect_rc 2 1; expect_out 2 "${PORT2} is not an ancestor of origin/parity-companion"; pass

# case 3: pushed now; the same ledger is clean
g update-ref refs/remotes/origin/parity-companion old
run_check "${REPO}"; expect_rc 3 0; pass

# case 4: a src/ change on the old game with no ledger cell
write_ledger "${PORT2}" "\`${PORT1}\`"
run_check "${REPO}"; expect_rc 4 1; expect_out 4 'Port fix two changes src/ and has no'; pass

# case 5: a ledger port beyond the pin (pushed, never recaptured)
write_ledger "${PORT1}" "\`${PORT1}\`" "\`${PORT2}\`"
run_check "${REPO}"; expect_rc 5 1; expect_out 5 "${PORT2} is not an ancestor of the pin"; pass

# case 6: the current game's table drifted from the pin's
write_ledger "${PORT2}" "\`${PORT1}\` and \`${PORT2}\`"
echo 'table v2' > "${REPO}/tests/parity/scenario_table.h"
run_check "${REPO}"; expect_rc 6 1; expect_out 6 'scenario_table.h differs from'; pass
echo 'table v1' > "${REPO}/tests/parity/scenario_table.h"

# case 7: no pin line / two pin lines
write_ledger "${PORT2}" "\`${PORT1}\`, \`${PORT2}\`"
sed -i.bak '/^Old-game commit the goldens/d' "${REPO}/tests/parity/golden/DRIFT_LEDGER.md"
run_check "${REPO}"; expect_rc 7 2; expect_err 7 'has 0 pin lines'; pass

# case 8: an unbackticked sha is malformed, not skipped
write_ledger "${PORT2}" "\`${PORT1}\`" "${PORT2}"
run_check "${REPO}"; expect_rc 8 2; expect_err 8 'malformed old-game commit'; pass

# case 9: ref absent locally is a SKIPPED pass ...
write_ledger "${PORT2}" "\`${PORT1}\`" "\`${PORT2}\`"
OG_PARITY_COMPANION_REF=origin/absent run_check "${REPO}"
expect_rc 9 0; expect_out 9 'SKIPPED'; pass
# case 10: ... and a failure where the ref is required (CI)
GITHUB_ACTIONS=true OG_PARITY_COMPANION_REF=origin/absent run_check "${REPO}"
expect_rc 10 2; expect_err 10 'OG_PARITY_REQUIRE_REF=1'; pass

# case 11: a missing ledger is "cannot run"
run_check "${REPO}" "${tmp}/no_such.md"; expect_rc 11 2; expect_err 11 'cannot find'; pass

# case 12: a sha this clone does not have (a stale local ref) is a SKIPPED
# pass that names the sha and says to fetch ...
UNKNOWN=abcdef1234567
write_ledger "${PORT2}" "\`${PORT1}\`" "\`${PORT2}\`" "\`${UNKNOWN}\`"
run_check "${REPO}"; expect_rc 12 0; expect_out 12 'SKIPPED'
expect_out 12 "${UNKNOWN} is not a commit in this clone"
expect_out 12 'git fetch origin parity-companion'; pass
# case 13: ... and a violation where the ref is required (CI)
GITHUB_ACTIONS=true run_check "${REPO}"
expect_rc 13 1; expect_out 13 "${UNKNOWN} is not a commit in this clone (never pushed"; pass

echo "test_check_parity_companion_refs: ${CASES}/${CASES} PASS"
