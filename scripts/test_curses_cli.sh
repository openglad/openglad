#!/usr/bin/env bash
set -euo pipefail

curses_bin=${1:?usage: test_curses_cli.sh <openglad_curses-bin> <og_git_hash.h>}
stamp_header=${2:?usage: test_curses_cli.sh <openglad_curses-bin> <og_git_hash.h>}

if [ ! -x "$curses_bin" ]; then
    echo "FAIL: Cannot find executable: $curses_bin" >&2
    exit 1
fi
if [ ! -f "$stamp_header" ]; then
    echo "FAIL: Cannot find the build stamp: $stamp_header" >&2
    exit 1
fi

check_success() {
    local description=$1
    local expected=$2
    shift 2

    local output=""
    local status=0
    set +e
    output=$("$curses_bin" "$@" 2>&1)
    status=$?
    set -e

    if [[ $status -ne 0 ]]; then
        printf 'Expected zero exit for %s, got %d.\n' "$description" "$status" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi

    if [[ -n "$expected" && $output != *"$expected"* ]]; then
        printf 'Expected output for %s to contain: %s\n' "$description" "$expected" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi
}

# Every failure case pins the EXACT exit status, because that status is the one
# observable separating "the option parser refused" from "the terminal refused".
# src/platform/curses/main.cpp:17-18 exits 2 when parse_app_options() returns
# false without should_exit; run_curses_app's own failures (no tty, lobby) come
# back as 1 — and under CTest there is never a tty, so ANY run that gets past
# the parser exits 1 anyway. With only "non-zero" as the oracle, a parser that
# stopped refusing a missing value would fall through, exit 1, and stay green
# (while reaching io_init()+cfg.load_settings() with no OPENGLAD_CONFIG_DIR set).
check_failure() {
    local description=$1
    local want_status=$2
    local expected=$3
    shift 3

    local output=""
    local status=0
    set +e
    output=$("$curses_bin" "$@" 2>&1)
    status=$?
    set -e

    if [[ $status -ne $want_status ]]; then
        printf 'Expected exit %d for %s, got %d.\n' "$want_status" "$description" "$status" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi

    if [[ -n "$expected" && $output != *"$expected"* ]]; then
        printf 'Expected output for %s to contain: %s\n' "$description" "$expected" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi
}

check_success "long help" "Usage: openglad_curses [options]" --help
check_success "version" "openglad " --version
# #508: the version number is shown only by a build of a commit on master's
# history; any other build prints its commit alone. The line this binary must
# print is written here from the build's own og_git_hash.h, not asked of
# og::version, so a binary that ignores the stamped master flag fails: the CI
# test builds are off master and hold it to "openglad commit <hash>", a master
# build to "openglad version 2.<count> (<hash>)". Only stdout is compared:
# the line is printed there, and a notice an instrumented build writes on
# stderr (a sanitizer or coverage message) is not part of it. Bash 3.2 and
# BSD sed safe.
stamp_value() {
    sed -n "s/^#define $1 $2\$/\\1/p" "$stamp_header"
}
stamp_on_master=$(stamp_value OPENGLAD_VERSION_ON_MASTER '\([01]\)')
stamp_version=$(stamp_value OPENGLAD_VERSION_STRING '"\([0-9][0-9]*\.[0-9][0-9]*\)"')
stamp_hash=$(stamp_value OPENGLAD_GIT_HASH '"\([0-9a-z+]*\)"')
if [[ -z $stamp_on_master || -z $stamp_version || -z $stamp_hash ]]; then
    printf 'FAIL: %s lacks the master flag, the version or the hash:\n' \
        "$stamp_header" >&2
    cat "$stamp_header" >&2
    exit 1
fi
if [[ $stamp_on_master == 1 ]]; then
    want_version_line="openglad version $stamp_version ($stamp_hash)"
else
    want_version_line="openglad commit $stamp_hash"
fi
version_status=0
version_line=$("$curses_bin" --version 2>/dev/null) || version_status=$?
if [[ $version_status -ne 0 || $version_line != "$want_version_line" ]]; then
    printf 'FAIL: --version (exit %d) printed:\n%s\n' "$version_status" \
        "$version_line" >&2
    printf 'but %s (OPENGLAD_VERSION_ON_MASTER %s) says the line is:\n%s\n' \
        "$stamp_header" "$stamp_on_master" "$want_version_line" >&2
    printf '(a binary older than the stamp also lands here: rebuild openglad_curses)\n' >&2
    exit 1
fi
check_success "short help" "Usage: openglad_curses [options]" -h
check_success "option parsing before help" "Usage: openglad_curses [options]" \
    --campaign gladiator \
    --level 2 \
    --save cli_test_save \
    --seed 123 \
    --difficulty 2 \
    --host \
    --port 23456 \
    --join ws://127.0.0.1:23457 \
    --relay http://127.0.0.1:8787 \
    --no-unicode \
    --no-color \
    --help

# Parser refusals: exit 2, before run_curses_app (so before io_init/load_settings).
check_failure "unknown option" 2 "unknown option" --not-a-real-option
check_failure "missing campaign" 2 "" --campaign
check_failure "missing level" 2 "" --level
check_failure "missing save" 2 "" --save
check_failure "missing seed" 2 "" --seed
check_failure "missing difficulty" 2 "" --difficulty
check_failure "missing port" 2 "" --port
check_failure "missing join" 2 "" --join
check_failure "missing relay" 2 "" --relay

tmp_config=$(mktemp -d)
trap 'rm -rf "$tmp_config"' EXIT
# The one case that DOES reach run_curses_app: a fully valid command line that
# the terminal then rejects, which is exit 1 (curses_app.cpp's CursesTerminal
# constructor throw), not the parser's 2.
OPENGLAD_CONFIG_DIR="$tmp_config/config/" check_failure \
    "non-terminal startup" \
    1 \
    "standard input/output is not a terminal" \
    --campaign gladiator \
    --level 1 \
    --save cli_test_save \
    --seed 123 \
    --difficulty 1 \
    --no-unicode \
    --no-color
