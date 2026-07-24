#!/usr/bin/env bash

set -euo pipefail

server_bin=${1:?usage: test_headless_server_cli.sh <server-bin>}
server_pid=""
server_tmpdir=""
server_output=""
server_collision_root=""

cleanup_server_probe() {
    if [[ -n $server_pid ]]; then
        kill -TERM "$server_pid" 2>/dev/null || true
        wait "$server_pid" 2>/dev/null || true
        server_pid=""
    fi
    if [[ -n $server_collision_root ]]; then
        rm -rf -- "$server_collision_root"
        server_collision_root=""
    fi
    if [[ -n $server_tmpdir ]]; then
        rm -rf -- "$server_tmpdir"
        server_tmpdir=""
    fi
    if [[ -n $server_output ]]; then
        rm -f -- "$server_output"
        server_output=""
    fi
}

trap cleanup_server_probe EXIT

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
    server_tmpdir=$(mktemp -d)
    server_output=$(mktemp)
    OPENGLAD_CONFIG_DIR="$server_tmpdir" "$server_bin" \
        --host 127.0.0.1 \
        --port "$port" \
        --lobby-poll-ms 0 \
        --fps 60 \
        > "$server_output" 2>&1 &
    server_pid=$!

    local saw_listening=0
    for _ in $(seq 1 50); do
        if grep -q "headless_server_listening" "$server_output"; then
            saw_listening=1
            break
        fi
        if ! kill -0 "$server_pid" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done

    if [[ $saw_listening -ne 1 ]]; then
        printf 'Expected server to report listening before shutdown.\n' >&2
        cat "$server_output" >&2
        exit 1
    fi

    server_collision_root=$(mktemp -d)
    local collision_output=""
    local collision_status=0
    set +e
    collision_output=$(
        OPENGLAD_CONFIG_DIR="$server_collision_root" \
            python3 - "$server_bin" "$port" <<'PY'
import subprocess
import sys

try:
    completed = subprocess.run(
        [
            sys.argv[1],
            "--host", "127.0.0.1",
            "--port", sys.argv[2],
            "--lobby-poll-ms", "0",
            "--fps", "60",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=5,
        check=False,
    )
except subprocess.TimeoutExpired:
    print("collision probe timed out")
    raise SystemExit(124)

sys.stdout.write(completed.stdout)
raise SystemExit(completed.returncode)
PY
    )
    collision_status=$?
    set -e
    if [[ $collision_status -eq 124 || $collision_status -eq 137 ]]; then
        printf 'Second server did not reject the occupied endpoint promptly.\n' >&2
        printf '%s\n' "$collision_output" >&2
        exit 1
    fi
    local expected_collision=""
    expected_collision="headless_server_fatal WebSocketServerTransport failed to listen on 127.0.0.1:$port"
    if [[ $collision_status -eq 0 ||
          $collision_output != *"$expected_collision"* ]]; then
        printf 'Expected a second server on the occupied endpoint to fail cleanly.\n' >&2
        printf 'Expected diagnostic: %s\n' "$expected_collision" >&2
        printf '%s\n' "$collision_output" >&2
        exit 1
    fi
    rm -rf -- "$server_collision_root"
    server_collision_root=""

    kill -TERM "$server_pid" 2>/dev/null || true
    local status=0
    wait "$server_pid" || status=$?
    server_pid=""

    if [[ $status -ne 0 ]]; then
        printf 'Expected server to handle SIGTERM with status 0, got %d.\n' "$status" >&2
        cat "$server_output" >&2
        exit 1
    fi

    rm -rf -- "$server_tmpdir"
    server_tmpdir=""
    rm -f -- "$server_output"
    server_output=""
}

check_server_start_shutdown
