#!/usr/bin/env bash
# Retired build macros must stay retired.
#
# Four preprocessor macros used to fork this codebase:
#
#     USE_TOUCH_INPUT     DISABLE_MULTIPLAYER
#     USE_CONTROLLER_INPUT    FAKE_TOUCH_EVENTS
#
# None of them was defined by any build file in this repository's history
# except the Code::Blocks project target deleted in 49ea10bb, so since the
# CMake migration every arm they guarded was code that no shipping build
# compiled. `git log -S '-DUSE_CONTROLLER_INPUT' --all` and
# `git log -S '-DREDUCE_OVERSCAN' --all` are empty; the other two appear only
# in that one deleted project file. PR #292 deleted the arms.
#
# They are not coming back, and reintroducing one would not restore a
# feature:
#
#   * Touch input is LIVE through two other seams. handle_events()
#     (src/interface/input.cpp) and the editor's handle_basic_editor_event()
#     route SDL FingerMotion/Up/Down into handle_mouse_event, and the web
#     shell's on-screen overlay writes touch_keystate, which
#     isPlayerHoldingKey ORs into the keyboard state. The retired
#     USE_TOUCH_INPUT arm was a redundant SECOND touch implementation that
#     could not be switched on as written: it called
#     input_touch_has_alternate(), defined in og_platform_sdl, from
#     og_interface — an inverted component dependency that does not link.
#     Its on-screen button geometry was likewise a dead twin; the real
#     overlay geometry is CSS in web/shell.html.
#
#   * Multiplayer is LIVE through the lobby/seat machinery.
#     DISABLE_MULTIPLAYER guarded a second main menu and a second seat
#     screen — a rule twin under the maintainer's no-rule-twins principle —
#     plus save-slot and picker shims reachable only from the
#     USE_TOUCH_INPUT arms above.
#
# So a new `#ifdef USE_TOUCH_INPUT` is never a build variant being restored;
# it is dead code being re-added, or a live rule being forked in two. This
# gate makes that fail the build instead of surviving as green dead weight,
# the same way check_injector_settles.sh keeps flat fade settles out.
#
# Deliberately NOT covered: ANDROID, OUYA, __IPHONEOS__ and REDUCE_OVERSCAN.
# Those are platform-port hooks, not dead build variants; deciding their
# fate is a product-direction question, tracked separately.
#
# Wired into the build as a dependency of og_interface (CMakeLists.txt,
# beside check_render_no_sim_writes), so every configuration that builds the
# interface library runs it.
set -euo pipefail

MACROS=(
    USE_TOUCH_INPUT
    DISABLE_MULTIPLAYER
    USE_CONTROLLER_INPUT
    FAKE_TOUCH_EVENTS
)

# Search roots: the compiled tree plus the build files that configure it.
# .github/workflows and flake.nix are deliberately NOT scanned -- a stray
# -DUSE_TOUCH_INPUT added to a CI lane or to the dev shell would slip past
# this gate, but it would switch nothing on, because PR #292 deleted every
# arm; the thing worth failing the build over is an #ifdef coming BACK.
ROOTS=(
    src
    include
    tests
    cmake
    scripts
    CMakeLists.txt
    CMakePresets.json
    web
)

present=()
for root in "${ROOTS[@]}"; do
    if [ -e "$root" ]; then
        present+=("$root")
    fi
done

if [ "${#present[@]}" -eq 0 ]; then
    echo "ERROR: check_retired_build_macros.sh found none of its search roots;" >&2
    echo "       run it from the repository root." >&2
    exit 1
fi

# -w so a longer identifier that merely contains one of these names (there is
# none today, but the gate should accuse only the real thing) does not trip
# it. The script excludes itself: the names above are the documentation.
pattern=$(IFS='|'; echo "${MACROS[*]}")
hits=$(grep -rnwE "$pattern" "${present[@]}" \
           --exclude=check_retired_build_macros.sh || true)

if [ -n "$hits" ]; then
    echo "$hits" >&2
    echo "ERROR: a retired build macro reappeared." >&2
    echo "       USE_TOUCH_INPUT, DISABLE_MULTIPLAYER, USE_CONTROLLER_INPUT" >&2
    echo "       and FAKE_TOUCH_EVENTS were never defined by any CMake build;" >&2
    echo "       their arms were deleted in PR #292. Touch lives in" >&2
    echo "       handle_events/handle_basic_editor_event plus the web overlay's" >&2
    echo "       touch_keystate; multiplayer lives in the lobby/seat machinery." >&2
    echo "       See the header of this script." >&2
    exit 1
fi

echo "Retired build macro check: OK"
