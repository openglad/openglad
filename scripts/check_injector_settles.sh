#!/usr/bin/env bash
# Injector settles must be conditions, not clocks.
#
# The menu injector suite used to settle every screen transition with
#
#     wait_for_interactable("some_id", 5000);
#     SDL_Delay(750);  // Wait for fadeblack animation
#
# on the authority of one line in CLAUDE.md. There is no fade to wait for in
# a TESTING build: FadeBetween is a single SDL_BlitSurface (the #ifdef TESTING
# branch in src/platform/sdl/video_sdl.cpp, which traces "FadeBetween:
# skipping animation (test mode)"), and src/interface/ui/menu_screen_runner.cpp
# says the same in its own comment. Every one of those 750 ms was spent
# waiting for an animation that does not exist, and none of them proved the
# incoming screen had composed.
#
# The replacement is wait_for_menu_frames(n) in tests/test_interact.h: it
# returns as soon as run_menu_screen has completed n more frames, which is
# both cheaper and a strictly stronger statement.
#
# SCOPE, deliberately narrow for now: this check covers the files the
# counts-not-clocks conversion has actually reached. The same pattern still
# exists in other injector suites (test_company_list.cpp, test_lineup_ui.cpp,
# test_ctf_ui.cpp, test_uxshots_probe.cpp and others); add each file here as
# its own conversion lands, so the gate never passes by looking away.
set -euo pipefail

FILES=(
    tests/test_interact.h
    tests/integration/test_difficulty.cpp
    tests/integration/test_hire_team.cpp
    tests/integration/test_save_load_team.cpp
    tests/integration/test_new_game.cpp
    tests/integration/test_options_menu.cpp
    tests/integration/test_help_smoke.cpp
    tests/integration/test_campaign_and_level_picker.cpp
)

status=0

for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "ERROR: $file is listed in check_injector_settles.sh but does not exist" >&2
        status=1
        continue
    fi
    # A flat 750 ms sleep within three lines after a wait_for_interactable is
    # the cargo-culted fade settle.
    hits=$(awk '
        /wait_for_interactable/ { armed = 3; next }
        armed > 0 && /SDL_Delay\(750\)/ { printf "%s:%d:%s\n", FILENAME, FNR, $0 }
        armed > 0 { armed-- }
    ' "$file")
    if [ -n "$hits" ]; then
        echo "$hits" >&2
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "ERROR: flat fade settles found next to wait_for_interactable." >&2
    echo "       Use wait_for_menu_frames(n) — fades are a single blit under" >&2
    echo "       TESTING, so the sleep waits for an animation that never runs." >&2
    echo "       See CLAUDE.md, \"Testing Menu UI / Interactive Flows\"." >&2
    exit 1
fi

echo "Injector settle check: OK"
