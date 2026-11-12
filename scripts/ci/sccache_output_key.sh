#!/usr/bin/env bash
# GCC coverage objects embed the path derived from -o for their runtime .gcda
# file.  sccache deliberately ignores that output path, which is normally an
# excellent cross-target optimization but is unsound for instrumented objects.
# Add the output path to sccache's documented custom C/C++ cache-key input.
# This keeps GCC's command line unchanged while separating objects whose
# runtime profile destination differs.
set -euo pipefail

if [[ "$#" -lt 2 ]]; then
    echo "usage: sccache_output_key.sh <sccache> <compiler> [arguments...]" >&2
    exit 2
fi

cache_program="$1"
shift
output=""
expect_output=0
for argument in "$@"; do
    if [[ "$expect_output" -eq 1 ]]; then
        if [[ -n "$output" || -z "$argument" ]]; then
            echo "coverage compile has a missing or duplicate -o output" >&2
            exit 2
        fi
        output="$argument"
        expect_output=0
        continue
    fi
    case "$argument" in
        -o)
            expect_output=1
            ;;
        -o?*)
            if [[ -n "$output" ]]; then
                echo "coverage compile has duplicate -o outputs" >&2
                exit 2
            fi
            output="${argument#-o}"
            ;;
    esac
done
if [[ "$expect_output" -eq 1 || -z "$output" ]]; then
    echo "coverage compile has no -o output" >&2
    exit 2
fi
if [[ "$output" == *$'\n'* || "$output" == *$'\r'* ]]; then
    echo "coverage compile output cannot contain a newline" >&2
    exit 2
fi

export SCCACHE_C_CUSTOM_CACHE_BUSTER="openglad-coverage-output-v1:$output"
exec "$cache_program" "$@"
