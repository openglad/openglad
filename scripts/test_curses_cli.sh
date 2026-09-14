#!/usr/bin/env bash
set -euo pipefail

curses_bin=${1:?usage: test_curses_cli.sh <openglad_curses-bin>}

if [ ! -x "$curses_bin" ]; then
    echo "FAIL: Cannot find executable: $curses_bin" >&2
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
check_success "version" "openglad version 2." --version
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
