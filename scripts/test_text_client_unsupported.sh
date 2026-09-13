#!/usr/bin/env bash
set -euo pipefail

TEXT_BIN="${1:-${OPENGLAD_TEXT:-./openglad_text}}"
TEXT_TIMEOUT="${OPENGLAD_TEXT_TIMEOUT:-55}"
if [ ! -x "$TEXT_BIN" ]; then
    echo "FAIL: Cannot find executable: $TEXT_BIN" >&2
    exit 1
fi

TMPOUT=$(mktemp)
TMPOERR=$(mktemp)
TMPHOME=$(mktemp -d)
trap 'rm -f "$TMPOUT" "$TMPOERR"; rm -rf "$TMPHOME"' EXIT

printf 'input 0 left\nquit\n' | HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" --protocol --level 1 --seed 42 > "$TMPOUT" 2>/dev/null
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: openglad_text unsupported-path run exited with code $rc" >&2
    exit 1
fi

export TMPOUT
python3 - << 'PY'
import json, os, sys

lines = [l.strip() for l in open(os.environ['TMPOUT'], encoding='utf-8', errors='replace') if l.strip()]
if len(lines) < 3:
    print(f'FAIL: expected at least 3 JSON lines, got {len(lines)}', file=sys.stderr)
    sys.exit(1)

ready = json.loads(lines[0])
if ready.get('status') != 'ready':
    print('FAIL: first line is not ready', file=sys.stderr)
    sys.exit(1)

error = json.loads(lines[1])
if error.get('cmd') != 'error' or 'unknown command: input' not in error.get('message', ''):
    print(f'FAIL: unsupported input command did not emit expected error: {lines[1]}', file=sys.stderr)
    sys.exit(1)

quit_msg = json.loads(lines[2])
if quit_msg.get('status') != 'ok':
    print('FAIL: quit confirmation missing', file=sys.stderr)
    sys.exit(1)

print('PASS: unsupported protocol command emits expected error')
PY

HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" --probe-unsupported-warnings > /dev/null 2> "$TMPOERR"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: openglad_text warning probe run exited with code $rc" >&2
    exit 1
fi

export TMPOERR
python3 - << 'PY'
import os, sys

stderr_text = open(os.environ['TMPOERR'], encoding='utf-8', errors='replace').read()
expected = [
    "level_data_draw_impl: not supported in headless mode",
    "create_level_render not supported in headless mode",
    "yes_or_no_prompt: not supported in headless mode, returning default",
    "get_input_events: not supported in headless mode",
    "find_follow_leader: not supported in headless mode",
    "load_map_data: not supported in headless mode",
    "input_state_from_sdl: not supported in headless mode",
]

for needle in expected:
    count = stderr_text.count(needle)
    if count != 1:
        print(f"FAIL: expected warning '{needle}' exactly once, found {count}", file=sys.stderr)
        sys.exit(1)

print("PASS: unsupported headless warnings emitted exactly once")
PY

if printf '2\n6\n7\n7\n' | HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" \
    "$TEXT_BIN" --level 9999 > "$TMPOUT" 2> "$TMPOERR"; then
    echo "FAIL: invalid-level picker run unexpectedly succeeded" >&2
    exit 1
else
    rc=$?
fi
if [ $rc -ne 1 ]; then
    echo "FAIL: invalid-level picker run exited with code $rc instead of 1" >&2
    exit 1
fi
if ! grep -Fq 'picker error: protocol session failed with code 1' "$TMPOERR"; then
    echo "FAIL: invalid-level picker error was not propagated by main" >&2
    exit 1
fi

# --team-level / --campaign-state are inputs to the --protocol session
# assembler only; the picker's GO stages from its own save and would drop them
# silently (#247). Each must be refused loudly rather than ignored.
for flag_args in "--team-level 5" "--campaign-state wp9_flag=3"; do
    # shellcheck disable=SC2086
    if HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" $flag_args \
        < /dev/null > "$TMPOUT" 2> "$TMPOERR"; then
        echo "FAIL: '$flag_args' without --protocol unexpectedly succeeded" >&2
        exit 1
    else
        rc=$?
    fi
    if [ $rc -ne 1 ]; then
        echo "FAIL: '$flag_args' without --protocol exited with $rc instead of 1" >&2
        exit 1
    fi
    if ! grep -Fq -- '--team-level and --campaign-state require --protocol' "$TMPOERR"; then
        echo "FAIL: '$flag_args' without --protocol did not print the refusal" >&2
        exit 1
    fi
    if [ -s "$TMPOUT" ]; then
        echo "FAIL: '$flag_args' without --protocol produced stdout before refusing" >&2
        exit 1
    fi
done

echo "PASS: picker-mode runs refuse the protocol-only session flags"

# The paired positive arm: with --protocol the same flag reaches the seeder and
# the session comes up ready. A key the save's write choke rejects fails the
# session with status 1 instead of seeding a half-applied campaign.
printf 'quit\n' | HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" \
    --protocol --level 1 --seed 42 --campaign-state wp9_flag=3,wp9_other=1 \
    > "$TMPOUT" 2> "$TMPOERR"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: --protocol --campaign-state run exited with code $rc" >&2
    exit 1
fi
if ! head -n 1 "$TMPOUT" | grep -Fq '"status":"ready"'; then
    echo "FAIL: --protocol --campaign-state run did not report ready: $(head -n 1 "$TMPOUT")" >&2
    exit 1
fi

if printf 'quit\n' | HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" \
    --protocol --level 1 --seed 42 --campaign-state BadKey=3 \
    > "$TMPOUT" 2> "$TMPOERR"; then
    echo "FAIL: --campaign-state BadKey unexpectedly seeded the session" >&2
    exit 1
else
    rc=$?
fi
if [ $rc -ne 1 ]; then
    echo "FAIL: --campaign-state BadKey exited with $rc instead of 1" >&2
    exit 1
fi
if ! grep -Fq 'Rejected --campaign-state key BadKey' "$TMPOERR"; then
    echo "FAIL: --campaign-state BadKey did not name the rejected key" >&2
    exit 1
fi

echo "PASS: --protocol --campaign-state seeds valid keys and refuses invalid ones"

# A --campaign-state token with no '=' is a usage error: print the usage line,
# exit 0, and start no session at all (no ready banner on stdout).
HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" \
    --protocol --campaign-state wp9_flag < /dev/null > "$TMPOUT" 2> "$TMPOERR"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: malformed --campaign-state token exited with $rc instead of 0" >&2
    exit 1
fi
if ! grep -Fq -- '--campaign-state expects key=value[,key=value...]' "$TMPOERR"; then
    echo "FAIL: malformed --campaign-state token did not print the usage line" >&2
    exit 1
fi
if [ -s "$TMPOUT" ]; then
    echo "FAIL: malformed --campaign-state token still started a session: $(cat "$TMPOUT")" >&2
    exit 1
fi

echo "PASS: a malformed --campaign-state token aborts before any session starts"

echo "PASS: text picker propagates protocol startup failures"
