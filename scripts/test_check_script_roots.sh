#!/usr/bin/env bash
# The scan-root preambles of scripts/check_no_std_regex.sh and
# scripts/check_vendor_leaks.sh.
#
# Both gates used to be blind in exactly the same way: the verdict was an
# `if grep ...` and GNU grep returns 2 when a scan root is missing — EVEN when
# it also printed a match — so rc=2 is falsy, the gate skips its ERROR arm and
# prints its success line. Run either script from a directory that is not the
# repo root (a stray `cd`, a copied CMake command, a future ctest entry without
# WORKING_DIRECTORY) and it certified a tree it never read. check_vendor_leaks
# had the same hole with the diagnostic hidden behind `2>/dev/null`.
#
# So the exit codes are the contract, and this test pins all three of them on
# temp trees: 0 = scanned and clean, 1 = violation found (and NAMED), 2 = the
# gate could not run. Case 3 and case 5 are the blind cases; they observed rc 0
# with the success line before the preambles existed.
#
# Temp trees only: the case-2 tree is the one place a banned include line is
# written, and it lives under mktemp. scripts/ is not scanned by either gate,
# so this script's own text cannot trip them.
set -euo pipefail

SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf -- "${tmp}"' EXIT INT TERM

out="${tmp}/stdout"
err="${tmp}/stderr"
rc=0

run_check() {  # run_check <script-name> <root>
    rc=0
    bash "${SCRIPTS_DIR}/$1" "$2" >"${out}" 2>"${err}" || rc=$?
}

fail() {  # fail <case> <why>
    echo "FAIL case $1: $2" >&2
    echo "  observed rc: ${rc}" >&2
    echo "  --- stdout ---" >&2
    sed 's/^/  /' "${out}" >&2
    echo "  --- stderr ---" >&2
    sed 's/^/  /' "${err}" >&2
    exit 1
}

expect_rc() {  # expect_rc <case> <expected>
    [[ "${rc}" -eq "$2" ]] || fail "$1" "expected rc $2"
}
expect_stdout() {  # expect_stdout <case> <substring>
    grep -qF -- "$2" "${out}" || fail "$1" "stdout does not contain '$2'"
}
expect_no_stdout() {  # expect_no_stdout <case> <substring>
    if grep -qF -- "$2" "${out}"; then
        fail "$1" "stdout must NOT contain '$2'"
    fi
}
expect_stderr() {  # expect_stderr <case> <substring>
    grep -qF -- "$2" "${err}" || fail "$1" "stderr does not contain '$2'"
}

make_regex_tree() {  # every root check_no_std_regex scans, one clean TU
    local root="$1"
    mkdir -p "${root}/src" "${root}/include" "${root}/tests" "${root}/tools"
    printf 'int ok() { return 0; }\n' > "${root}/src/ok.cpp"
}

make_vendor_tree() {  # every root check_vendor_leaks scans, one clean TU
    local root="$1"
    mkdir -p "${root}/include/openglad/gameplay" \
             "${root}/include/openglad/resources" \
             "${root}/include/openglad/interface" \
             "${root}/include/openglad/platform" \
             "${root}/src/gameplay" "${root}/src/resources" \
             "${root}/src/interface" "${root}/src/platform"
    printf 'int a() { return 0; }\n' > "${root}/src/gameplay/a.cpp"
}

REGEX_OK_LINE='check_no_std_regex: no TU includes <regex>'
VENDOR_OK_LINE='External dependency header check: OK'

# --- case 1: a complete, clean tree is certified clean (rc 0) ---------------
make_regex_tree "${tmp}/c1"
run_check check_no_std_regex.sh "${tmp}/c1"
expect_rc 1 0
expect_stdout 1 "${REGEX_OK_LINE}"
echo "PASS case 1"

# --- case 2: a violation exits 1 and NAMES the file ------------------------
# (the control for the rc-capture rewrite: the gate must not have inverted)
make_regex_tree "${tmp}/c2"
printf '#include <regex>\n' > "${tmp}/c2/src/evil.cpp"
run_check check_no_std_regex.sh "${tmp}/c2"
expect_rc 2 1
expect_stdout 2 'src/evil.cpp:1'
expect_no_stdout 2 "${REGEX_OK_LINE}"
echo "PASS case 2"

# --- case 3: the blind case — a violation plus a missing root --------------
# grep returns 2 here, so the old `if grep` gate printed evil.cpp AND then the
# success line, exit 0. The preamble must refuse before grep runs.
cp -a "${tmp}/c2" "${tmp}/c3"
rm -rf "${tmp}/c3/tests"
run_check check_no_std_regex.sh "${tmp}/c3"
expect_rc 3 2
expect_stderr 3 'cannot find'
expect_no_stdout 3 "${REGEX_OK_LINE}"
echo "PASS case 3"

# --- case 4: the vendor gate certifies a complete, clean tree (rc 0) -------
make_vendor_tree "${tmp}/c4"
run_check check_vendor_leaks.sh "${tmp}/c4"
expect_rc 4 0
expect_stdout 4 "${VENDOR_OK_LINE}"
echo "PASS case 4"

# --- case 5: the vendor gate's blind case — a missing component root -------
cp -a "${tmp}/c4" "${tmp}/c5"
rm -rf "${tmp}/c5/src/platform"
run_check check_vendor_leaks.sh "${tmp}/c5"
expect_rc 5 2
expect_stderr 5 'cannot find'
expect_no_stdout 5 "${VENDOR_OK_LINE}"
echo "PASS case 5"

echo "test_check_script_roots: 5/5 PASS"
