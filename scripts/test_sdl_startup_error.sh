#!/usr/bin/env bash
set -euo pipefail

openglad_bin=${1:?usage: test_sdl_startup_error.sh /path/to/openglad}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

set +e
output=$(
    env \
        SDL_VIDEODRIVER=openglad-intentionally-invalid \
        SDL_AUDIODRIVER=dummy \
        OPENGLAD_CONFIG_DIR="$test_root/config" \
        "$openglad_bin" 2>&1
)
status=$?
set -e

printf '%s\n' "$output"
if (( status != 1 )); then
    printf 'expected clean startup failure status 1, got %d\n' "$status" >&2
    exit 1
fi

grep -Fq 'Creating screen' <<<"$output"
grep -Fq 'Unrecoverable error:' <<<"$output"
grep -Fq 'SDL_CreateWindow failed' <<<"$output"
