#!/usr/bin/env bash
# Retired build macros must stay retired.
#
# Eight preprocessor macros used to fork this codebase:
#
#     USE_TOUCH_INPUT         DISABLE_MULTIPLAYER
#     USE_CONTROLLER_INPUT    FAKE_TOUCH_EVENTS
#     ANDROID                 OUYA
#     __IPHONEOS__            REDUCE_OVERSCAN
#
# The first four were never defined by any build file in this repository's
# history except the Code::Blocks project target deleted in 49ea10bb, so
# since the CMake migration every arm they guarded was code that no shipping
# build compiled. `git log -S '-DUSE_CONTROLLER_INPUT' --all` and
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
# The other four are Jonathan Dearborn's 2013 port hooks (issue #302), and
# none of them is set by anything this repository builds:
#
#   * ANDROID is defined automatically by the Android NDK toolchain
#     (-DANDROID). The repository has no Android toolchain file, preset or
#     CI lane, so no build here ever sets it.
#   * __IPHONEOS__ was SDL2's iOS platform macro. SDL3 renamed it
#     SDL_PLATFORM_IOS, so under the pinned SDL3 nothing defines it even on
#     iOS. The live iOS fork is SDL_PLATFORM_IOS (see
#     display_settings_platform_rewire in menu_screen_specs.cpp).
#   * OUYA and REDUCE_OVERSCAN were defined by nothing — no toolchain, no
#     preset, no build file in history.
#
# Their arms (an Android soft-keyboard sleep, Android/iOS window and path
# forks, an OUYA keyboard-panning cutout, an 8 px/6 px overscan inset) were
# deleted for #302. A port to one of those platforms starts from the live
# seams (SDL_PLATFORM_*, the runtime overscan_percentage_ setting), not from
# these names.
#
# So a new `#ifdef USE_TOUCH_INPUT` (or `#ifdef OUYA`) is never a build
# variant being restored; it is dead code being re-added, or a live rule
# being forked in two. This gate makes that fail the build instead of
# surviving as green dead weight, the same way check_injector_settles.sh
# keeps flat fade settles out.
#
# What it matches is what a RETURNING macro looks like, not prose: a
# preprocessor directive naming one of the eight (#if/#ifdef/#ifndef/#elif/
# #define/#undef, backslash-continued lines joined first) in the compiled
# tree, or a -D / compile-definition naming one in the build files. Prose is
# deliberately allowed: Jonathan Dearborn's 2013 comment above
# overscan_percentage_ in include/openglad/interface/session_state.h
# ("10% (0.10f) is recommended on OUYA") is heritage and stays verbatim.
#
# Wired into the build as a dependency of og_interface (CMakeLists.txt,
# beside check_render_no_sim_writes), so every configuration that builds the
# interface library runs it. Portable to bash 3.2 and POSIX awk (the macOS
# Release lane): no mapfile/readarray, no awk escapes beyond POSIX.
set -euo pipefail

MACROS=(
    USE_TOUCH_INPUT
    DISABLE_MULTIPLAYER
    USE_CONTROLLER_INPUT
    FAKE_TOUCH_EVENTS
    ANDROID
    OUYA
    __IPHONEOS__
    REDUCE_OVERSCAN
)

# Directive roots: the compiled tree (and the web shell, whose C++ glue lives
# beside it). Definition roots: the build files that configure it.
# .github/workflows and flake.nix are deliberately NOT scanned -- a stray
# -DUSE_TOUCH_INPUT added to a CI lane or to the dev shell would slip past
# this gate, but it would switch nothing on, because every arm is deleted;
# the thing worth failing the build over is an #ifdef coming BACK.
DIRECTIVE_ROOTS=(
    src
    include
    tests
    web
)
DEFINITION_ROOTS=(
    cmake
    scripts
    CMakeLists.txt
    CMakePresets.json
)

dir_present=()
for root in "${DIRECTIVE_ROOTS[@]}"; do
    if [ -e "$root" ]; then
        dir_present+=("$root")
    fi
done
def_present=()
for root in "${DEFINITION_ROOTS[@]}"; do
    if [ -e "$root" ]; then
        def_present+=("$root")
    fi
done

if [ "${#dir_present[@]}" -eq 0 ] || [ "${#def_present[@]}" -eq 0 ]; then
    echo "ERROR: check_retired_build_macros.sh is missing its search roots;" >&2
    echo "       run it from the repository root." >&2
    exit 1
fi

# Explicit identifier boundaries rather than \b or grep -w, so the same
# expression means the same thing to GNU grep, BSD grep and POSIX awk: a
# longer identifier that merely contains one of these names does not trip it.
names=$(IFS='|'; echo "${MACROS[*]}")
word="(^|[^A-Za-z0-9_])(${names})([^A-Za-z0-9_]|$)"

# 1. Preprocessor directives in C/C++ sources, in ONE awk process over every
#    file (a per-file loop forks a thousand times, which is slow on Windows'
#    bash). awk joins backslash-continued lines first, so a name on the
#    second line of a multi-line #if is found and reported at the
#    directive's first line.
directive_hits=$(find "${dir_present[@]}" -type f \( -name '*.c' -o -name '*.cc' \
        -o -name '*.cpp' -o -name '*.cxx' -o -name '*.h' -o -name '*.hh' \
        -o -name '*.hpp' -o -name '*.inl' -o -name '*.ipp' -o -name '*.m' \
        -o -name '*.mm' \) -exec awk -v word="$word" '
    function check(text, at) {
        if (text ~ /^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef|elif|elifdef|elifndef|define|undef)([^A-Za-z0-9_]|$)/ &&
            text ~ word)
            print FILENAME ":" at ":" text
    }
    FNR == 1 { pending = "" }
    {
        line = $0
        sub(/\r$/, "", line)
        if (pending != "") { joined = pending " " line } else { joined = line; start = FNR }
        if (line ~ /\\$/) {
            sub(/\\$/, "", joined)
            pending = joined
            next
        }
        pending = ""
        check(joined, start)
    }
' {} + | LC_ALL=C sort -t: -k1,1 -k2,2n)

# 2. Definitions in the build files: -DNAME, add_compile_definitions(NAME),
#    target_compile_definitions(... NAME), add_definitions(-DNAME). The
#    script excludes itself: the names above are its documentation.
#    -D is followed directly by the name, so it gets its own arm; the
#    CMake commands take the name as a later argument on the same line.
definition_hits=$(grep -rnE "(-D[[:space:]]*|(add_compile_definitions|target_compile_definitions|add_definitions|COMPILE_DEFINITIONS).*[^A-Za-z0-9_])(${names})([^A-Za-z0-9_]|$)" \
        "${def_present[@]}" --exclude=check_retired_build_macros.sh || true)

hits=$(printf '%s\n%s\n' "$directive_hits" "$definition_hits" | sed '/^$/d')

if [ -n "$hits" ]; then
    echo "$hits" >&2
    echo "ERROR: a retired build macro reappeared." >&2
    echo "       USE_TOUCH_INPUT, DISABLE_MULTIPLAYER, USE_CONTROLLER_INPUT" >&2
    echo "       and FAKE_TOUCH_EVENTS were never defined by any CMake build;" >&2
    echo "       their arms were deleted in PR #292. Touch lives in" >&2
    echo "       handle_events/handle_basic_editor_event plus the web overlay's" >&2
    echo "       touch_keystate; multiplayer lives in the lobby/seat machinery." >&2
    echo "       ANDROID, OUYA, __IPHONEOS__ and REDUCE_OVERSCAN guarded port" >&2
    echo "       hooks no build here defines; their arms were deleted for #302" >&2
    echo "       (iOS forks on SDL_PLATFORM_IOS). See the header of this script." >&2
    exit 1
fi

echo "Retired build macro check: OK"
