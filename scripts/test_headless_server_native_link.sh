#!/usr/bin/env bash

set -euo pipefail

build_dir=${1:?usage: test_headless_server_native_link.sh <build-dir>}

link_cmd=$(
    ninja -C "$build_dir" -t commands openglad_server \
        | grep -E '(^|[[:space:]])-o[[:space:]]+([^[:space:]]*/)?openglad_server([[:space:]]|$)' \
        | tail -n 1
)

if [[ -z "${link_cmd:-}" ]]; then
    printf 'Failed to locate the openglad_server link command in %s.\n' "$build_dir" >&2
    exit 1
fi

if grep -Eq '(^|[[:space:]])-lSDL2([[:space:]]|$)|(^|[[:space:]])-lSDL2_mixer([[:space:]]|$)|/libSDL2(\.so([.0-9]+)?|-[^[:space:]]+\.so([.0-9]+)?)|/libSDL2_mixer(\.so([.0-9]+)?|-[^[:space:]]+\.so([.0-9]+)?)' <<<"$link_cmd"; then
    printf 'openglad_server link command unexpectedly references SDL:\n%s\n' "$link_cmd" >&2
    exit 1
fi
