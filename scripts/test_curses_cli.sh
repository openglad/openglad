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

check_failure() {
    local description=$1
    local expected=$2
    shift 2

    local output=""
    local status=0
    set +e
    output=$("$curses_bin" "$@" 2>&1)
    status=$?
    set -e

    if [[ $status -eq 0 ]]; then
        printf 'Expected non-zero exit for %s, got 0.\n' "$description" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi

    if [[ $output != *"$expected"* ]]; then
        printf 'Expected output for %s to contain: %s\n' "$description" "$expected" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi
}

check_success "long help" "Usage: openglad_curses [options]" --help
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

check_failure "unknown option" "unknown option" --not-a-real-option
check_failure "missing campaign" "" --campaign
check_failure "missing level" "" --level
check_failure "missing save" "" --save
check_failure "missing seed" "" --seed
check_failure "missing difficulty" "" --difficulty
check_failure "missing port" "" --port
check_failure "missing join" "" --join
check_failure "missing relay" "" --relay

tmp_config=$(mktemp -d)
trap 'rm -rf "$tmp_config"' EXIT
OPENGLAD_CONFIG_DIR="$tmp_config/config/" check_failure \
    "non-terminal startup" \
    "standard input/output is not a terminal" \
    --campaign gladiator \
    --level 1 \
    --save cli_test_save \
    --seed 123 \
    --difficulty 1 \
    --no-unicode \
    --no-color
