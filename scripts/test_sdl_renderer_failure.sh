#!/usr/bin/env bash
# Issue #248: a startup with no usable renderer must not leave the game running
# invisibly forever. SDL's Wayland backend has no window framebuffer, so a
# machine without an accelerated renderer gets a null renderer there and the
# window is never mapped (a Wayland surface appears only after its first buffer
# commit). SDL_RENDER_DRIVER=<unavailable> reproduces that same code path under
# any video driver, with the video driver pinned so no fallback applies.
set -euo pipefail

openglad_bin=${1:?usage: test_sdl_renderer_failure.sh /path/to/openglad}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

set +e
output=$(
    timeout 45 env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=openglad-intentionally-invalid \
        OPENGLAD_CONFIG_DIR="$test_root/config" \
        "$openglad_bin" 2>&1
)
status=$?
set -e

printf '%s\n' "$output"
if (( status == 124 )); then
    printf 'openglad kept running with no renderer (invisible window, never exits)\n' >&2
    exit 1
fi
if (( status != 1 )); then
    printf 'expected clean startup failure status 1, got %d\n' "$status" >&2
    exit 1
fi

grep -Fq 'Creating screen' <<<"$output"
grep -Fq 'SDL_CreateRenderer failed' <<<"$output"
grep -Fq 'Unrecoverable error:' <<<"$output"
