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
# both cheaper and a strictly stronger statement. A screen that run_menu_screen
# does not host has its own oracle instead (the text editor:
# og::input_native::text_input_is_active(); the help viewer: yield_count();
# the campaign picker: its own counters), and a click is proven consumed by
# the value, label or trace it writes — the shared ladder in
# tests/test_click_ladder.h, or click_cycle_step in test_options_menu.cpp.
#
# SCOPE — two tiers, because the conversion is landing file by file and a gate
# that reds nine files at once is not a gate anyone can land.
#
#   Tier 1 (FILES): files where only the 750 ms fadeblack cargo cult has been
#   removed. Rule: no literal SDL_Delay(750) anywhere in the file. These still
#   carry shorter flat settles (300 / 500 / 900 / 1000 ms) that the conversion
#   has not reached yet; the tier exists so the one break that is fixed cannot
#   come back while the rest is being finished.
#
#   Tier 2 (CONVERTED_FILES): fully converted files, where the only sleeps
#   left are poll intervals inside a wait-on-condition helper. Rule: EVERY
#   SDL_Delay argument in the file is either an integer literal <= 100 (a
#   press hold or a poll tick) or an expression whose text contains "poll"
#   (poll_interval, kHirePollMs, static_cast<Uint32>(poll_interval)).
#   SDL_Delay(300), SDL_Delay(500), SDL_Delay(750) and named settle constants
#   such as SDL_Delay(kUiSettleMs) all fail: a settle spelled as a constant is
#   still a clock. Tier 2 implies tier 1 (no argument it accepts is 750), so a
#   file may be listed in both or in tier 2 alone — tests/test_click_ladder.h
#   is tier 2 only.
#
# A file graduates from pending to tier 1 to tier 2 as its conversion lands;
# move its name out of the pending list below in the same commit. That list is
# a census snapshot, not a hand-kept guess — regenerate it with
#
#   for f in $(grep -rl SDL_Delay tests/); do
#       grep -on 'SDL_Delay([^)]*)' "$f" | sed "s|^|$f:|"
#   done | grep -viE 'poll|SDL_Delay\(([0-9]|[1-9][0-9]|100)\)' | cut -d: -f1 | sort -u
#
# and subtract the names already in FILES and CONVERTED_FILES below. As of this
# commit that leaves these ungated by either tier: test_back_to_mainmenu.cpp,
# test_campaign_sprite_uaf.cpp, test_campaign_zone_ui.cpp,
# test_canvas_scale.cpp, test_company_list.cpp, test_fade_ownership.cpp,
# test_fairy_death.cpp, test_go_no_team.cpp, test_help.cpp,
# test_level_editor_issue12_wall_crash.cpp, test_lineup_ui.cpp,
# test_menu_engine.cpp, test_menu_pins.cpp, test_networking_menu.cpp,
# test_networking_uxshots.cpp, test_overpowered_team.cpp,
# test_picker_funcs.cpp, test_seat_chip.cpp, test_uxshots_probe.cpp,
# test_view_team.cpp. Two more census hits are not settles and need no
# conversion: test_menu_frame_wait.cpp's 750 sits inside a comment describing
# the cargo cult, and tests/test_input_helpers.h's static_cast<Uint32>(delay_ms)
# is a caller-chosen press hold. On top of that, every tier-1 file still carries
# its own sub-750 settles, which tier 1 by construction does not look at. The
# gate must never pass — or describe itself — by looking away.
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
    tests/integration/test_level_editor_interactions.cpp
    tests/integration/test_picker_detail_menu_driven.cpp
    tests/integration/test_train_team.cpp
)

CONVERTED_FILES=(
    tests/test_interact.h
    tests/test_click_ladder.h
    tests/integration/test_difficulty.cpp
    tests/integration/test_hire_team.cpp
    tests/integration/test_cloud_ui.cpp
)

status=0
tier1_failed=0
tier2_failed=0

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
        tier1_failed=1
    fi
done

# Tier 2: every SDL_Delay argument must be a poll tick, not a settle.
# grep -o would give the arguments but lose the line numbers a maintainer
# needs, so read the numbered lines and peel each SDL_Delay( ... ) off the
# line in turn — a line carrying two calls is checked twice.
for file in "${CONVERTED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "ERROR: $file is listed in check_injector_settles.sh but does not exist" >&2
        status=1
        continue
    fi
    while IFS= read -r numbered; do
        lineno=${numbered%%:*}
        rest=${numbered#*:}
        while [[ "$rest" == *"SDL_Delay("* ]]; do
            rest=${rest#*SDL_Delay(}
            arg=${rest%%)*}
            trimmed=$(printf '%s' "$arg" | tr -d '[:space:]')
            if [[ "$trimmed" =~ ^[0-9]+$ ]] && [ "$trimmed" -le 100 ]; then
                continue
            fi
            if printf '%s' "$arg" | grep -qi 'poll'; then
                continue
            fi
            echo "$file:$lineno: 'SDL_Delay($arg)'" >&2
            status=1
            tier2_failed=1
        done
    done < <(grep -n 'SDL_Delay(' "$file" || true)
done

if [ "$tier1_failed" -ne 0 ]; then
    echo "ERROR: flat 750 ms fade settles found in a converted file." >&2
    echo "       Use wait_for_menu_frames(n) — fades are a single blit under" >&2
    echo "       TESTING, so the sleep waits for an animation that never runs." >&2
    echo "       See CLAUDE.md, \"Testing Menu UI / Interactive Flows\"." >&2
fi

if [ "$tier2_failed" -ne 0 ]; then
    echo "ERROR: flat settle in a fully converted injector file; use" >&2
    echo "       wait_for_menu_frames(n), the screen's own oracle, or the" >&2
    echo "       shared click ladder (tests/test_click_ladder.h); a poll" >&2
    echo "       interval must be spelled with poll." >&2
    echo "       See CLAUDE.md, \"Testing Menu UI / Interactive Flows\"." >&2
fi

if [ "$status" -ne 0 ]; then
    exit 1
fi

echo "Injector settle check: OK"
