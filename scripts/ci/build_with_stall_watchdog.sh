#!/usr/bin/env bash
# scripts/ci/build_with_stall_watchdog.sh — run `cmake --build` under a
# stall watchdog that captures the stuck process tree and resumes the build.
#
#   scripts/ci/build_with_stall_watchdog.sh <build-dir> [cmake --build args...]
#
# Why this exists (#309): roughly one Windows release build in three froze for
# good on the MSYS2/mingw64 runner, at "Linking CXX static library ..." and
# then nothing until the job's 30-minute cap. The cause was found with this
# watchdog's second live capture and fixed in CMakeLists.txt: the shell-script
# lint targets ran a bare `scripts/x.sh`, which on Windows means
# `cmd.exe /C "... && x.sh"`, which means ShellExecuteEx and the "How do you
# want to open this file?" picker; usually it returned at once (the lints
# never ran on Windows), and sometimes its COM handshake never came back and
# ninja waited on that one edge forever once every other edge was done.
#
# The watchdog stays because the class of failure is real on this runner:
# a build that stops making progress dies at the cap with nothing to read.
# Now it dies in OG_BUILD_STALL_SECS with the stuck tree, its threads' wait
# reasons and their stacks in the log - and it FAILS, because after #309 a
# stall is a new bug, not a flake to retry (OG_BUILD_ATTEMPTS is 1 unless a
# capture hunt sets it higher).
#
# What this does:
#   1. Runs `cmake --build` in the FOREGROUND, exactly as the workflow did
#      before - same stdin, same stdout, same signal disposition, same place
#      in the process tree. The watchdog is the background job, not the
#      build: bash hands a background job /dev/null as stdin, and a build
#      that hangs on some pipe or console interaction must not be run under
#      a different topology than the one it hangs in, or the capture never
#      happens and the cause stays unknown.
#   2. The watchdog watches the build's .ninja_log, which ninja appends to
#      (and flushes) each time an edge finishes. "No edge finished for
#      OG_BUILD_STALL_SECS" is the stall oracle; it depends on nothing the
#      build prints.
#   3. On a stall: dumps the process tree under this shell, minus the
#      watchdog's own subtree (per-process CPU, thread wait reasons, stacks
#      where a debugger is present, crash reports, memory, disk - see
#      win_build_stall.ps1), lists the edges that were in flight (outputs on
#      disk that .ninja_log never recorded), kills the build's tree, and
#      leaves a marker. The shell then deletes those half-written outputs so
#      ninja cannot mistake a truncated .obj for a finished one, and either
#      fails (the default) or, when OG_BUILD_ATTEMPTS allows, runs the build
#      again; ninja resumes from the edges it recorded.
#   4. Every stall is a workflow warning and a step-summary entry, so a job
#      that stalled is never mistaken for a clean one whatever its colour.
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
max_attempts=${OG_BUILD_ATTEMPTS:-1}
script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ninja_log="$build_dir/.ninja_log"
stall_marker=$(mktemp)
rm -f "$stall_marker"

on_windows() {
    case "$(uname -s 2>/dev/null)" in
        MSYS*|MINGW*|CYGWIN*) return 0 ;;
    esac
    return 1
}

# The Windows PID of a cygwin/msys process: the two PID spaces differ.
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

# $1 action, $2 root windows pid (the shell), $3 windows pid whose subtree
# is skipped (the watchdog).
# The dump is bounded (OG_BUILD_DUMP_SECS) so that a probe that hangs can
# never turn a captured stall into a job that dies at the cap anyway.
run_stall_ps() {
    local action=$1 root=$2 exclude=$3
    timeout -k 10 "${OG_BUILD_DUMP_SECS:-420}" \
    "$(powershell_exe)" -NoProfile -NonInteractive -ExecutionPolicy Bypass \
        -File "$(cygpath -w "$script_dir/win_build_stall.ps1")" \
        -Action "$action" -RootPid "$root" -ExcludePid "$exclude" \
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
    # CMakeFiles/<version>/CompilerId*/a.exe are configure-time probes, not
    # ninja outputs: never in the log, never in flight.
    (
        cd "$build_dir" || exit 0
        find . \( -name '*.obj' -o -name '*.o' -o -name '*.a' \
                  -o -name '*.exe' -o -name '*.glad' \) -type f \
             -not -path './CMakeFiles/[0-9]*/CompilerId*' 2>/dev/null \
            | sed 's|^\./||' | sort -u
    ) | comm -23 - "$recorded"
    rm -f "$recorded"
}

# A process and all of its descendants, parents first.
descendants_posix() {
    local pid=$1 child
    echo "$pid"
    for child in $(pgrep -P "$pid" 2>/dev/null); do
        descendants_posix "$child"
    done
}

# The build's processes: everything under this shell except the shell
# itself and the watchdog's own subtree.
build_tree_posix() {
    local root=$1 exclude=$2
    comm -23 <(descendants_posix "$root" | grep -vx "$root" | sort) \
             <(descendants_posix "$exclude" | sort)
}

dump_posix() {
    local pids
    pids=$(build_tree_posix "$1" "$2" | paste -sd, -)
    echo "build processes under shell $1 (watchdog $2 excluded): ${pids:-<none>}"
    [ -n "$pids" ] && ps --forest -o pid,ppid,stat,etime,time,rss,args -p "$pids" 2>/dev/null
}

kill_tree_posix() {
    local p
    for p in $(build_tree_posix "$1" "$2" | tac); do
        kill -KILL "$p" 2>/dev/null || true
    done
}

# Windows, without PowerShell: every direct child of the shell except the
# watchdog, by Windows PID, through taskkill's own tree walk. Runs after the
# PowerShell kill as well, so the build comes back even when that script
# cannot run at all - the first real capture lost its retry to a parse
# error there. cygwin ps columns: PID PPID PGID WINPID ...
kill_tree_windows_fallback() {
    local shell=$1 watchdog=$2 winpid
    for winpid in $(ps 2>/dev/null | awk -v p="$shell" -v w="$watchdog" 'NR > 1 && $2 == p && $1 != w { print $4 }'); do
        MSYS2_ARG_CONV_EXCL='*' taskkill /F /T /PID "$winpid" >/dev/null 2>&1 || true
    done
}

# The background job. Polls the ninja log; on a stall, dumps and kills the
# build and leaves the marker for the foreground shell.
watchdog() {
    local attempt=$1
    local last_size=-1 last_change=$SECONDS size
    local shell_pid=$$ self_pid=$BASHPID
    while :; do
        sleep "$poll_secs"
        size=$(stat -c %s "$ninja_log" 2>/dev/null || echo 0)
        if [ "$size" != "$last_size" ]; then
            last_size=$size
            last_change=$SECONDS
            continue
        fi
        [ $((SECONDS - last_change)) -ge "$stall_secs" ] || continue

        # The marker goes down first: the moment the build dies below, the
        # foreground shell resumes, and it must find the marker and wait for
        # this dump to finish instead of reading the kill as a plain failure.
        : > "$stall_marker"
        local last_edge inflight
        last_edge=$(recorded_outputs | tail -n 1)
        inflight=$(unrecorded_outputs)
        {
            echo "::group::stall diagnostics (attempt ${attempt}: no edge finished for ${stall_secs}s)"
            echo "last recorded edge output: ${last_edge:-<none>}"
            echo "outputs on disk the log never recorded (edges in flight):"
            printf '%s\n' "${inflight:-<none>}"
            echo "last ten .ninja_log lines:"
            tail -n 10 "$ninja_log" 2>/dev/null || true
        }
        if on_windows; then
            local shell_win self_win
            shell_win=$(win_pid_of "$shell_pid")
            self_win=$(win_pid_of "$self_pid")
            echo "shell: msys pid ${shell_pid} / windows pid ${shell_win:-?}; watchdog: msys pid ${self_pid} / windows pid ${self_win:-?}"
            if [ -n "$shell_win" ]; then
                run_stall_ps Dump "$shell_win" "${self_win:-0}" || echo "(dump script failed: $?)"
            fi
        else
            dump_posix "$shell_pid" "$self_pid"
        fi
        echo "::endgroup::"

        echo "killing the stalled build tree"
        if on_windows; then
            [ -n "${shell_win:-}" ] && { run_stall_ps Kill "$shell_win" "${self_win:-0}" || true; }
            kill_tree_windows_fallback "$shell_pid" "$self_pid"
        else
            kill_tree_posix "$shell_pid" "$self_pid"
        fi
        {
            printf '%s\n' "${last_edge:-<none>}"
            printf '%s\n' "$inflight"
        } > "$stall_marker"
        exit 0
    done
}

report_stall() {
    local attempt=$1 last_edge=$2 inflight=$3
    local title="Windows build stall (#309)"
    # A workflow annotation is one line; the in-flight list is joined.
    local inflight_line
    inflight_line=$(printf '%s\n' "$inflight" | paste -sd, - | sed 's/,/, /g')
    echo "::warning title=${title}::attempt ${attempt}: no edge finished for ${stall_secs}s after \"${last_edge}\"; in flight: ${inflight_line:-<none recorded>}. The stuck tree is dumped above."
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
    rm -f "$stall_marker"

    watchdog "$attempt" &
    local wd=$!

    cmake --build "$build_dir" "$@"
    local status=$?

    if [ -e "$stall_marker" ]; then
        # The watchdog is mid-capture: let it finish the dump and the kill.
        wait "$wd" 2>/dev/null
    else
        kill "$wd" 2>/dev/null
        wait "$wd" 2>/dev/null
        [ -e "$stall_marker" ] || return "$status"
        # It flagged a stall in the instant between the build ending and
        # the kill; the build is complete, so this is a plain exit.
        rm -f "$stall_marker"
        return "$status"
    fi

    local last_edge inflight
    last_edge=$(head -n 1 "$stall_marker")
    inflight=$(tail -n +2 "$stall_marker" | grep -v '^$')
    rm -f "$stall_marker"
    if [ -n "$inflight" ]; then
        echo "deleting the in-flight outputs so the retry rebuilds them:"
        (
            cd "$build_dir" && printf '%s\n' "$inflight" | while IFS= read -r f; do
                [ -n "$f" ] && rm -f -- "$f" && echo "  rm $f"
            done
        )
    fi
    report_stall "$attempt" "$last_edge" "$inflight"
    return 75
}

attempt=1
while :; do
    build_once "$attempt" "$@"
    status=$?
    if [ "$status" -ne 75 ]; then
        exit "$status"
    fi
    if [ "$attempt" -ge "$max_attempts" ]; then
        echo "::error title=Windows build stall (#309)::every one of ${max_attempts} build attempt(s) stalled. A stall is a bug, not a flake: read the stall diagnostics group in this log."
        exit 1
    fi
    attempt=$((attempt + 1))
    echo "resuming the build (attempt ${attempt} of ${max_attempts})"
done
