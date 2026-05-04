#!/usr/bin/env bash

set -euo pipefail

server_bin=${1:?usage: test_headless_server_cli.sh <server-bin>}

check_failure() {
    local description=$1
    local expected=$2
    shift 2

    local output=""
    local status=0
    set +e
    output=$("$server_bin" "$@" 2>&1)
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

check_help() {
    local output=""
    local status=0
    set +e
    output=$("$server_bin" --help 2>&1)
    status=$?
    set -e

    if [[ $status -ne 0 ]]; then
        printf 'Expected --help to exit 0, got %d.\n' "$status" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi

    if [[ $output != *"Usage: openglad_server [options]"* ]]; then
        printf 'Expected --help output to contain usage text.\n' >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi
}

check_help
check_failure "unknown option" "Unknown argument:" --unknown-option
check_failure "missing host value" "--host requires a value" --host
check_failure "invalid port" "--port requires a positive integer" --port -1
check_failure "missing fps value" "--fps requires a positive integer" --fps
check_failure "invalid fps" "--fps requires a positive integer" --fps abc
check_failure "non-positive fps" "--fps requires a positive integer" --fps 0
