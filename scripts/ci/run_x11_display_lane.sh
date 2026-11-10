#!/usr/bin/env bash
# scripts/ci/run_x11_display_lane.sh - the real-display x11 lane (#329).
#
#   scripts/ci/run_x11_display_lane.sh <build-dir> [one] [two]
#
# Runs og_test_display_x11 against SDL's real x11 video driver on Xvfb, once
# per X topology (both when no topology is named):
#
#   one  One X screen, 1920x1080, with five smaller video modes added through
#        XRandR (1280x720, 1024x768, 800x600, 640x480, 320x240) and openbox
#        as the EWMH window manager, so exclusive and borderless fullscreen
#        requests are acknowledged. ctest label x11-one.
#   two  Two X screens (1920x1080 and 1280x1024), no Xinerama, no window
#        manager. SDL's x11 backend counts one display per X screen, so this
#        trips exclusive_mode_switch_is_safe() exactly as a dual-monitor
#        XRandR desktop would. It does not reproduce SDL bug #9560 itself.
#        Without a window manager no fullscreen request is acknowledged,
#        which is what the timed-out-request test relies on. ctest label
#        x11-two.
#
# The build dir must be configured with -DOPENGLAD_X11_DISPLAY_TESTS=ON and
# og_test_display_x11 built. The ctest entries set SDL_VIDEODRIVER=x11 and
# OG_X11_EXPECT_DISPLAYS themselves (cmake/OpenGladTests.cmake); this script
# provides the X server they talk to.
#
# Exit codes:
#   0  every test passed
#   1  a test failed (ctest's verdict)
#   2  usage error
#   7  environment abort: the X server, the modes, the screen count or the
#      window manager did not come up as specified. Nothing was tested.
#
# Called from .github/workflows/test.yml (job x11-display), never from CMake.
# Keep it bash-3.2 and POSIX-awk clean like every other script here.

set -eu

if [ "$#" -lt 1 ]; then
    echo "usage: $0 <build-dir> [one] [two]" >&2
    exit 2
fi
build_dir=$1
shift
topologies="$*"
if [ -z "$topologies" ]; then
    topologies="one two"
fi
for topology in $topologies; do
    case "$topology" in
        one|two) ;;
        *) echo "unknown topology '$topology' (want one or two)" >&2; exit 2 ;;
    esac
done
if [ ! -d "$build_dir" ]; then
    echo "build dir '$build_dir' does not exist" >&2
    exit 2
fi

# The modes run A adds. All are smaller than the 1920x1080 screen: Xvfb
# refuses modes larger than the screen it started with. 320x240 is under the
# 640x400 floor of the DISPLAY selector, so it is the filtered-out negative.
added_modes="1280x720 1024x768 800x600 640x480 320x240"
expected_mode_count=6

xvfb_pid=""
wm_pid=""
test_pid=""
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/og_x11_lane.XXXXXX")

stop_x() {
    if [ -n "$test_pid" ]; then
        kill "$test_pid" 2>/dev/null || true
        wait "$test_pid" 2>/dev/null || true
        test_pid=""
    fi
    if [ -n "$wm_pid" ]; then
        kill "$wm_pid" 2>/dev/null || true
        wait "$wm_pid" 2>/dev/null || true
        wm_pid=""
    fi
    if [ -n "$xvfb_pid" ]; then
        kill "$xvfb_pid" 2>/dev/null || true
        wait "$xvfb_pid" 2>/dev/null || true
        xvfb_pid=""
    fi
}

cleanup() {
    stop_x
    rm -rf "$work_dir"
}
trap cleanup EXIT

env_abort() {
    echo "x11 lane environment abort: $*" >&2
    if [ -f "$work_dir/xvfb.log" ]; then
        echo "--- Xvfb log ---" >&2
        cat "$work_dir/xvfb.log" >&2
    fi
    if [ -f "$work_dir/wm.log" ]; then
        echo "--- openbox log ---" >&2
        cat "$work_dir/wm.log" >&2
    fi
    exit 7
}

# Start Xvfb with the given -screen arguments and export DISPLAY. -noreset is
# load-bearing: without it the server resets when its last client
# disconnects, and every mode xrandr added is gone before SDL connects.
# -displayfd lets the server pick a free display number, so the lane never
# collides with a runner's own :99.
start_xvfb() {
    : > "$work_dir/displayfd"
    Xvfb -noreset -nolisten tcp -displayfd 9 "$@" \
        9> "$work_dir/displayfd" > "$work_dir/xvfb.log" 2>&1 &
    xvfb_pid=$!
    tries=0
    while [ ! -s "$work_dir/displayfd" ]; do
        if ! kill -0 "$xvfb_pid" 2>/dev/null; then
            xvfb_pid=""
            env_abort "Xvfb exited before reporting its display"
        fi
        tries=$((tries + 1))
        if [ "$tries" -gt 300 ]; then
            env_abort "Xvfb did not report a display within 30 s"
        fi
        sleep 0.1
    done
    display_number=$(tr -dc '0-9' < "$work_dir/displayfd")
    if [ -z "$display_number" ]; then
        env_abort "Xvfb reported no display number"
    fi
    DISPLAY=":$display_number"
    export DISPLAY
    echo "Xvfb $* on $DISPLAY (pid $xvfb_pid)"
}

verify_screen_count() {
    want=$1
    got=$(xdpyinfo | grep -c '^screen #' || true)
    if [ "$got" != "$want" ]; then
        env_abort "xdpyinfo reports $got X screens, expected $want"
    fi
    echo "verified: $got X screen(s)"
}

add_modes() {
    for mode in $added_modes; do
        w=${mode%x*}
        h=${mode#*x}
        # A plain 60 Hz timing. Xvfb accepts any well-formed modeline; the
        # clock is in MHz.
        clock=$(awk -v w="$w" -v h="$h" \
            'BEGIN { printf "%.3f", (w + 40) * (h + 10) * 60 / 1000000 }')
        xrandr --newmode "$mode" "$clock" \
            "$w" $((w + 8)) $((w + 16)) $((w + 40)) \
            "$h" $((h + 2)) $((h + 4)) $((h + 10))
        xrandr --addmode screen "$mode"
    done
    # xrandr --newmode exits 0 even when the mode will not survive, so count
    # what the server actually lists for the output.
    got=$(xrandr --query | grep -c -E '^ +[0-9]+x[0-9]+' || true)
    if [ "$got" != "$expected_mode_count" ]; then
        xrandr --query >&2 || true
        env_abort "xrandr lists $got modes, expected $expected_mode_count"
    fi
    echo "verified: $got video modes"
}

# Openbox announces itself through _NET_SUPPORTING_WM_CHECK on the root, but
# that property can appear before the manager has finished publishing its
# check-window identity and supported fullscreen protocol. Wait for the full
# EWMH handshake: the check window must point to itself, have a name, and the
# root must advertise the state transition these tests exercise. This is a
# condition-based readiness barrier, not a timing settle. Run A starts it only
# after the integration harness reports that SDL's boot window and renderer
# exist; requiring a managed client proves Openbox adopted that existing
# window before the tests continue.
start_wm() {
    openbox > "$work_dir/wm.log" 2>&1 &
    wm_pid=$!
    tries=0
    while :; do
        root_check=$(xprop -root _NET_SUPPORTING_WM_CHECK 2>/dev/null || true)
        wm_window=$(printf '%s\n' "$root_check" |
            awk '/window id #/ { print $NF; exit }')
        if [ -n "$wm_window" ]; then
            window_identity=$(xprop -id "$wm_window" \
                _NET_SUPPORTING_WM_CHECK _NET_WM_NAME 2>/dev/null || true)
            self_check=$(printf '%s\n' "$window_identity" |
                awk '/_NET_SUPPORTING_WM_CHECK.*window id #/ { print $NF; exit }')
            if [ "$self_check" = "$wm_window" ] &&
                    printf '%s\n' "$window_identity" |
                        grep -q '^_NET_WM_NAME.*=' &&
                    xprop -root _NET_SUPPORTED 2>/dev/null |
                        grep -q '_NET_WM_STATE_FULLSCREEN' &&
                    xprop -root _NET_CLIENT_LIST 2>/dev/null |
                        grep -q 'window id #'; then
                break
            fi
        fi
        if ! kill -0 "$wm_pid" 2>/dev/null; then
            wm_pid=""
            env_abort "openbox exited before announcing itself"
        fi
        tries=$((tries + 1))
        if [ "$tries" -gt 300 ]; then
            env_abort "openbox did not set _NET_SUPPORTING_WM_CHECK within 30 s"
        fi
        sleep 0.1
    done
    echo "verified: openbox completed its EWMH fullscreen handshake (pid $wm_pid)"
}

run_one_screen_ctest() {
    window_ready_file="$work_dir/window.ready"
    wm_ready_file="$work_dir/wm.ready"
    export OG_X11_WINDOW_READY_FILE="$window_ready_file"
    export OG_X11_WM_READY_FILE="$wm_ready_file"

    ctest --test-dir "$build_dir" -L x11-one --no-tests=error \
        --output-on-failure --timeout 420 &
    test_pid=$!
    tries=0
    while [ ! -f "$window_ready_file" ]; do
        if ! kill -0 "$test_pid" 2>/dev/null; then
            wait "$test_pid" 2>/dev/null || true
            test_pid=""
            env_abort "one-screen test exited before constructing its SDL window"
        fi
        tries=$((tries + 1))
        if [ "$tries" -gt 300 ]; then
            env_abort "SDL did not construct the one-screen boot window within 30 s"
        fi
        sleep 0.1
    done
    echo "verified: SDL boot window and renderer are ready before Openbox"

    start_wm
    : > "$wm_ready_file"
    if wait "$test_pid"; then
        test_status=0
    else
        test_status=$?
    fi
    test_pid=""
    unset OG_X11_WINDOW_READY_FILE OG_X11_WM_READY_FILE
    return "$test_status"
}

run_ctest() {
    label=$1
    # --no-tests=error: a build dir configured without the option (no
    # x11-* entries) must fail, not pass vacuously.
    ctest --test-dir "$build_dir" -L "$label" --no-tests=error \
        --output-on-failure --timeout 420
}

status=0
for topology in $topologies; do
    case "$topology" in
        one)
            echo "=== run A: one X screen, XRandR modes, openbox ==="
            start_xvfb -screen 0 1920x1080x24
            verify_screen_count 1
            add_modes
            run_one_screen_ctest || status=1
            ;;
        two)
            echo "=== run B: two X screens, no window manager ==="
            start_xvfb -screen 0 1920x1080x24 -screen 1 1280x1024x24
            verify_screen_count 2
            run_ctest x11-two || status=1
            ;;
    esac
    stop_x
done
exit "$status"
