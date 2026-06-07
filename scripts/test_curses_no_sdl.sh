#!/usr/bin/env bash
#
# Asserts that the ncurses client binary is genuinely SDL-free: it must contain
# no SDL symbols (defined or undefined) and must not link libSDL2. This enforces
# the zero-SDL invariant of openglad_curses in CI, mirroring the headless
# server's link check.
#
set -euo pipefail

bin=${1:?usage: test_curses_no_sdl.sh <binary>}

if [[ ! -x "$bin" ]]; then
    printf 'test_curses_no_sdl: binary not found or not executable: %s\n' "$bin" >&2
    exit 1
fi

if nm -C "$bin" 2>/dev/null | grep -qE '\bSDL_'; then
    printf 'FAIL: %s references SDL symbols:\n' "$bin" >&2
    nm -C "$bin" 2>/dev/null | grep -E '\bSDL_' | head >&2
    exit 1
fi

if command -v ldd >/dev/null 2>&1 && ldd "$bin" 2>/dev/null | grep -qiE 'libSDL2'; then
    printf 'FAIL: %s links libSDL2:\n' "$bin" >&2
    ldd "$bin" 2>/dev/null | grep -iE 'libSDL2' >&2
    exit 1
fi

printf 'OK: %s is SDL-free\n' "$bin"
