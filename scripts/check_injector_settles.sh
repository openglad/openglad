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
# test_campaign_zone_ui.cpp, test_uxshots_probe.cpp and others); add each file
# here as its own conversion lands, so the gate never passes by looking away.
#
# Wired into the build as a dependency of og_game_test (CMakeLists.txt,
# beside check_vendor_leaks), so a reintroduced settle fails the test build
# rather than waiting for someone to run this by hand.
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
    tests/integration/test_pause_menu.cpp
    tests/integration/test_ctf_ui.cpp
    tests/integration/test_cloud_ui.cpp
)

status=0

for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "ERROR: $file is listed in check_injector_settles.sh but does not exist" >&2
        status=1
        continue
    fi
    # Any flat 750 ms sleep at all, not just one within N lines of a
    # wait_for_interactable. A proximity window is a gate that can be walked
    # around by moving the sleep two lines further down — and it was: the
    # first pass of this conversion left five 750 ms settles standing in these
    # files (three behind click_until_interactable, two behind
    # wait_for_team_menu) precisely because they sat outside the window. In a
    # converted file there is no legitimate 750, so the rule is simply none.
    hits=$(grep -nF 'SDL_Delay(750)' "$file" | sed "s|^|$file:|" || true)
    if [ -n "$hits" ]; then
        echo "$hits" >&2
        status=1
    fi
done

if [ "$status" -ne 0 ]; then
    echo "ERROR: flat 750 ms fade settles found in a converted file." >&2
    echo "       Use wait_for_menu_frames(n) — fades are a single blit under" >&2
    echo "       TESTING, so the sleep waits for an animation that never runs." >&2
    echo "       See CLAUDE.md, \"Testing Menu UI / Interactive Flows\"." >&2
    exit 1
fi

echo "Injector settle check: OK"
