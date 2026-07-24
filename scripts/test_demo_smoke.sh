#!/usr/bin/env bash
set -euo pipefail

demo_bin=${1:?usage: test_demo_smoke.sh /path/to/openglad_demo}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

set +e
output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=2x1 \
        OPENGLAD_DEMO_MAX_FRAMES=1 \
        OPENGLAD_DEMO_SEED=1 \
        OPENGLAD_CONFIG_DIR="$test_root/config" \
        "$demo_bin" 2>&1
)
status=$?
set -e

printf '%s\n' "$output"
if (( status != 0 )); then
    printf 'openglad_demo smoke exited with status %d\n' "$status" >&2
    exit 1
fi

grep -Fq 'grid: 2x1 = 2 sessions' <<<"$output"
grep -Fq 'openglad_demo: seed 1' <<<"$output"
mapfile -t scenarios < <(
    sed -n 's/^  session [01]: scenario \([0-9][0-9]*\)$/\1/p' \
        <<<"$output"
)
if (( ${#scenarios[@]} != 2 )); then
    printf 'expected two demo session scenario logs, got %d\n' \
        "${#scenarios[@]}" >&2
    exit 1
fi
readonly demo_pool=' 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 9411 9412 9413 9414 '
for scenario in "${scenarios[@]}"; do
    if [[ "$demo_pool" != *" $scenario "* ]]; then
        printf 'scenario %s is not in the production demo pool\n' \
            "$scenario" >&2
        exit 1
    fi
done
if [[ "${scenarios[0]}" == "${scenarios[1]}" ]]; then
    printf 'demo selector repeated a scenario before exhausting its pool\n' >&2
    exit 1
fi
grep -Fq \
    'openglad_demo: 2 sessions initialized, spawning 2 worker threads' \
    <<<"$output"
