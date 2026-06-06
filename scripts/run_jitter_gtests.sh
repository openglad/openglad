#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <og_test_game_core> <og_test_view> <og_unit_sim>" >&2
    exit 2
fi

FILTER='*Jitter*'

extract_selected_tests() {
    awk '
        /^[^[:space:]].*\.$/ {
            suite = $1
            next
        }
        /^[[:space:]]+[A-Za-z0-9_].*/ {
            if (suite != "") {
                test = $1
                print suite test
            }
        }
    '
}

for binary in "$@"; do
    if [[ ! -x "$binary" ]]; then
        echo "error: missing executable $binary" >&2
        exit 1
    fi

    list_output="$("$binary" --gtest_list_tests --gtest_filter="$FILTER")"
    selected_tests="$(printf '%s\n' "$list_output" | extract_selected_tests)"

    if [[ -z "$selected_tests" ]]; then
        echo "error: $binary selected zero *Jitter* tests" >&2
        printf '%s\n' "$list_output" >&2
        exit 1
    fi

    echo "== $binary =="
    printf '%s\n' "$selected_tests"
done

for binary in "$@"; do
    "$binary" --gtest_filter="$FILTER"
done
