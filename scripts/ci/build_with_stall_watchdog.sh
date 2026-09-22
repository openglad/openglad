#!/usr/bin/env bash
# scripts/ci/build_with_stall_watchdog.sh — run `cmake --build` under a
# stall watchdog that captures the stuck process tree and resumes the build.
#
#   scripts/ci/build_with_stall_watchdog.sh <build-dir> [cmake --build args...]
#
# Why this exists (#309): roughly one Windows release build in three froze for
# good on the MSYS2/mingw64 runner. Every capture looks the same — ninja
# prints "Linking CXX static library libog_interface.a" (once
# libog_platform_ws_transport.a), then nothing, ever: not the three or four
# compiles that were already in flight, not a single new edge, until the
# job's 30-minute cap cancels it. Ninja prints an edge when it FINISHES on a
# piped stdout, so the archive itself completed; whatever ran next never
# returned. The stall is timing-dependent (a rerun of the same commit passes)
# and it cannot be reproduced off the runner, so the only place the stuck
# state can be looked at is on the runner, at the moment it is stuck.
#
# What this does:
#   1. Runs `cmake --build` in the background and watches the build's
#      .ninja_log, which ninja appends to (and flushes) each time an edge
#      finishes. "No edge finished for OG_BUILD_STALL_SECS" is the stall
#      oracle — it does not depend on anything printed to stdout, and the
#      build's own stdout is left untouched so the capture sees the build
#      exactly as it hangs (piping it through a file changes the very
#      pipe topology under suspicion).
#   2. On a stall: dumps the process tree under the build (per-process CPU,
#      thread wait reasons, stacks where a debugger is present, crash
#      reports, memory, disk — see win_build_stall.ps1), lists the edges that
#      were in flight (outputs on disk that .ninja_log never recorded), kills
#      the tree, deletes those half-written outputs so ninja cannot mistake
#      a truncated .obj for a finished one, and runs the build again. Ninja
#      resumes from the edges it recorded.
#   3. Gives up after OG_BUILD_ATTEMPTS builds. Every stall is reported as a
#      workflow warning and in the step summary, so a "green" job that
#      needed the watchdog is never mistaken for a clean one.
#
# Portable bash: the dump and the tree kill have a PowerShell path (the
# runner) and a procps path (everything else), so the wrapper can be
# exercised locally.

set -u

if [ $# -lt 1 ]; then
    echo "usage: $0 <build-dir> [cmake --build args...]" >&2
    exit 2
fi

build_dir=$1
shift

stall_secs=${OG_BUILD_STALL_SECS:-180}
poll_secs=${OG_BUILD_POLL_SECS:-10}
max_attempts=${OG_BUILD_ATTEMPTS:-3}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ninja_log="$build_dir/.ninja_log"

on_windows() {
    case "$(uname -s 2>/dev/null)" in
        MSYS*|MINGW*|CYGWIN*) return 0 ;;
    esac
    return 1
}

# The Windows PID of a background job: cygwin/msys PIDs differ from the
# ones the rest of the OS knows.
win_pid_of() {
    ps -p "$1" 2>/dev/null | awk 'NR > 1 { print $4 }'
}

powershell_exe() {
    if command -v powershell.exe >/dev/null 2>&1; then
        command -v powershell.exe
    else
        echo "/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    fi
}

run_stall_ps() {
    local action=$1 winpid=$2
    "$(powershell_exe)" -NoProfile -NonInteractive -ExecutionPolicy Bypass \
        -File "$(cygpath -w "$script_dir/win_build_stall.ps1")" \
        -Action "$action" -RootPid "$winpid" \
        -BuildDir "$(cygpath -w "$build_dir")"
}

# Recorded outputs: every path in the fourth tab-separated column of the
# ninja log (v6: start end mtime output hash), backslashes normalised.
recorded_outputs() {
    [ -f "$ninja_log" ] || return 0
    awk -F'\t' '!/^#/ && NF >= 4 { gsub(/\\/, "/", $4); print $4 }' "$ninja_log"
}

# Outputs on disk that the log never recorded: those belong to the edges
# that were in flight when the build stalled. Only build products ninja
# would trust by mtime alone are listed.
unrecorded_outputs() {
    local recorded
    recorded=$(mktemp)
    recorded_outputs | sort -u > "$recorded"
    (
        cd "$build_dir" || exit 0
        find . \( -name '*.obj' -o -name '*.o' -o -name '*.a' \
                  -o -name '*.exe' -o -name '*.glad' \) -type f 2>/dev/null \
            | sed 's|^\./||' | sort -u
    ) | comm -23 - "$recorded"
    rm -f "$recorded"
}

# A process and all of its descendants, leaves last.
descendants_posix() {
    local pid=$1 child
    echo "$pid"
    for child in $(pgrep -P "$pid" 2>/dev/null); do
        descendants_posix "$child"
    done
}

dump_posix() {
    local pid=$1
    echo "process tree under $pid:"
    # shellcheck disable=SC2046
    ps --forest -o pid,ppid,stat,etime,time,rss,args -p $(descendants_posix "$pid" | paste -sd, -) 2>/dev/null \
        || ps -ef 2>/dev/null | head -100
}

kill_tree_posix() {
    local pid=$1 p
    for p in $(descendants_posix "$pid" | tac); do
        kill -KILL "$p" 2>/dev/null || true
    done
}

report_stall() {
    local attempt=$1 last_edge=$2 inflight=$3
    local title="Windows build stall (#309)"
    # A workflow annotation is one line; the in-flight list is joined.
    local inflight_line
    inflight_line=$(printf '%s\n' "$inflight" | paste -sd, - | sed 's/,/, /g')
    echo "::warning title=${title}::attempt ${attempt}: no edge finished for ${stall_secs}s after \"${last_edge}\"; in flight: ${inflight_line:-<none recorded>}. The stuck tree was dumped above and the build resumed."
    if [ -n "${GITHUB_STEP_SUMMARY:-}" ]; then
        {
            echo "### ${title}"
            echo
            echo "Build attempt ${attempt} stalled: no ninja edge finished for ${stall_secs}s."
            echo
            echo "- last finished edge: \`${last_edge}\`"
            echo "- outputs in flight (deleted before the retry):"
            if [ -n "$inflight" ]; then
                # shellcheck disable=SC2016  # the backticks are markdown
                printf '%s\n' "$inflight" | sed 's/^/  - `/; s/$/`/'
            else
                echo "  - (none recorded)"
            fi
            echo
            echo "The process dump is in the job log under \"stall diagnostics\"."
        } >> "$GITHUB_STEP_SUMMARY"
    fi
}

# One build attempt. Returns the build's exit status, or 75 (EX_TEMPFAIL)
# after a stall has been dumped and the tree killed.
build_once() {
    local attempt=$1
    shift
    cmake --build "$build_dir" "$@" &
    local pid=$!
    local last_size=-1 last_change=$SECONDS size

    while kill -0 "$pid" 2>/dev/null; do
        sleep "$poll_secs"
        size=$(stat -c %s "$ninja_log" 2>/dev/null || echo 0)
        if [ "$size" != "$last_size" ]; then
            last_size=$size
            last_change=$SECONDS
            continue
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
        if [ $((SECONDS - last_change)) -ge "$stall_secs" ]; then
            local last_edge inflight
            last_edge=$(recorded_outputs | tail -n 1)
            inflight=$(unrecorded_outputs)

            echo "::group::stall diagnostics (attempt ${attempt}: no edge finished for ${stall_secs}s)"
            echo "last recorded edge output: ${last_edge:-<none>}"
            echo "outputs on disk the log never recorded (edges in flight):"
            printf '%s\n' "${inflight:-<none>}"
            echo "last ten .ninja_log lines:"
            tail -n 10 "$ninja_log" 2>/dev/null || true
            if on_windows; then
                local winpid
                winpid=$(win_pid_of "$pid")
                echo "build root: msys pid ${pid}, windows pid ${winpid:-?}"
                if [ -n "$winpid" ]; then
                    run_stall_ps Dump "$winpid" || echo "(dump script failed: $?)"
                fi
            else
                dump_posix "$pid"
            fi
            echo "::endgroup::"

            echo "killing the stalled build tree"
            if on_windows && [ -n "${winpid:-}" ]; then
                run_stall_ps Kill "$winpid" || true
            else
                kill_tree_posix "$pid"
            fi
            wait "$pid" 2>/dev/null || true

            if [ -n "$inflight" ]; then
                echo "deleting the in-flight outputs so the retry rebuilds them:"
                (
                    cd "$build_dir" && printf '%s\n' "$inflight" | while IFS= read -r f; do
                        [ -n "$f" ] && rm -f -- "$f" && echo "  rm $f"
                    done
                )
            fi
            report_stall "$attempt" "${last_edge:-<none>}" "$inflight"
            return 75
        fi
    done

    wait "$pid"
}

attempt=1
while :; do
    build_once "$attempt" "$@"
    status=$?
    if [ "$status" -ne 75 ]; then
        exit "$status"
    fi
    if [ "$attempt" -ge "$max_attempts" ]; then
        echo "::error title=Windows build stall (#309)::every one of ${max_attempts} build attempt(s) stalled; giving up. See the stall diagnostics groups in this log."
        exit 1
    fi
    attempt=$((attempt + 1))
    echo "resuming the build (attempt ${attempt} of ${max_attempts})"
done
