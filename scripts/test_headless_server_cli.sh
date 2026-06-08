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
    local flag=$1
    local output=""
    local status=0
    set +e
    output=$("$server_bin" "$flag" 2>&1)
    status=$?
    set -e

    if [[ $status -ne 0 ]]; then
        printf 'Expected %s to exit 0, got %d.\n' "$flag" "$status" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi

    if [[ $output != *"Usage: openglad_server [options]"* ]]; then
        printf 'Expected %s output to contain usage text.\n' "$flag" >&2
        printf '%s\n' "$output" >&2
        exit 1
    fi
}

check_help --help
check_help -h
check_failure "unknown option" "Unknown argument:" --unknown-option
check_failure "missing host value" "--host requires a value" --host
check_failure "invalid port" "--port requires a positive integer" --port -1
check_failure "empty port" "--port requires a positive integer" --port ""
check_failure "out of range port" "--port requires a positive integer" --port 999999999999999999999999
check_failure "missing port value" "--port requires a positive integer" --port
check_failure "non-numeric port" "--port requires a positive integer" --port nope
check_failure "missing lobby poll value" "--lobby-poll-ms requires a non-negative integer" --lobby-poll-ms
check_failure "out of range lobby poll" "--lobby-poll-ms requires a non-negative integer" --lobby-poll-ms 999999999999999999999999
check_failure "invalid lobby poll" "--lobby-poll-ms requires a non-negative integer" --lobby-poll-ms -1
check_failure "non-numeric lobby poll" "--lobby-poll-ms requires a non-negative integer" --lobby-poll-ms nope
check_failure "missing fps value" "--fps requires a positive integer" --fps
check_failure "out of range fps" "--fps requires a positive integer" --fps 999999999999999999999999
check_failure "invalid fps" "--fps requires a positive integer" --fps abc
check_failure "non-positive fps" "--fps requires a positive integer" --fps 0

check_server_start_shutdown() {
    local port=""
    port=$(python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
)
    local tmpdir=""
    tmpdir=$(mktemp -d)
    local output=""
    output=$(mktemp)
    local pid=""
    OPENGLAD_CONFIG_DIR="$tmpdir" "$server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --lobby-poll-ms 0 \
        --fps 60 \
        > "$output" 2>&1 &
    pid=$!

    local saw_listening=0
    for _ in $(seq 1 50); do
        if grep -q "headless_server_listening" "$output"; then
            saw_listening=1
            break
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done

    kill -TERM "$pid" 2>/dev/null || true
    local status=0
    wait "$pid" || status=$?

    if [[ $saw_listening -ne 1 ]]; then
        printf 'Expected server to report listening before shutdown.\n' >&2
        cat "$output" >&2
        rm -rf "$tmpdir" "$output"
        exit 1
    fi

    if [[ $status -ne 0 ]]; then
        printf 'Expected server to handle SIGTERM with status 0, got %d.\n' "$status" >&2
        cat "$output" >&2
        rm -rf "$tmpdir" "$output"
        exit 1
    fi

    rm -rf "$tmpdir" "$output"
}

check_server_start_shutdown
