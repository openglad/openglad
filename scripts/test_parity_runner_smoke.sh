#!/usr/bin/env bash
# CLI contract for tests/parity/parity_runner_smoke — the standalone driver
# that produces the canonical branch StateDump used for golden comparison and
# for the mutation canary's flip check.
#
# The bug this pins: the tool used to exit 0 and write a structurally valid
# dump even when its PhysFS bootstrap had mounted no campaign at all, so every
# scenario ran against an empty arena and the dump disagreed with every golden
# for a reason nothing in the output named.
set -euo pipefail

smoke_bin=${1:?usage: test_parity_runner_smoke.sh /path/to/parity_runner_smoke}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

fail() { printf '%s\n' "$1" >&2; exit 1; }

# --- --list needs no bootstrap at all --------------------------------------
list_output=$("$smoke_bin" --list)
grep -Fqx 'smoke_nonempty_scen99' <<<"$list_output" ||
    fail '--list did not enumerate smoke_nonempty_scen99'

# --- A broken config dir must abort loudly and write nothing ----------------
# `campaigns` is a regular FILE here, so the bootstrap cannot create the
# directory the campaign archives are restored into: the same end state as a
# read-only or otherwise unusable install.
broken_dir="$test_root/broken"
mkdir -p "$broken_dir"
: > "$broken_dir/campaigns"
broken_out="$test_root/broken.json"

set +e
broken_output=$(
    env OPENGLAD_CONFIG_DIR="$broken_dir" \
        "$smoke_bin" --scenario smoke_nonempty_scen99 --out "$broken_out" 2>&1
)
broken_status=$?
set -e

printf '%s\n' "$broken_output"
if (( broken_status == 0 )); then
    fail 'parity_runner_smoke reported success against a broken campaign mount'
fi
if (( broken_status != 3 )); then
    fail "expected exit 3 for a broken campaign mount, got $broken_status"
fi
if [[ -e "$broken_out" ]]; then
    fail 'parity_runner_smoke wrote a dump for a scenario that never loaded'
fi
grep -Fq 'refusing to write a dump' <<<"$broken_output" ||
    fail 'the refusal did not say it was refusing to write'
grep -Fq "$broken_dir" <<<"$broken_output" ||
    fail 'the refusal did not name the config dir it used'

# The same refusal on the stdout path: no dump on the wire either.
set +e
broken_stdout=$(
    env OPENGLAD_CONFIG_DIR="$broken_dir" \
        "$smoke_bin" --scenario smoke_nonempty_scen99 2>/dev/null
)
broken_stdout_status=$?
set -e
if (( broken_stdout_status != 3 )); then
    fail "expected exit 3 on the stdout path, got $broken_stdout_status"
fi
if grep -Fq 'schema_version' <<<"$broken_stdout"; then
    fail 'parity_runner_smoke printed a stub dump instead of refusing'
fi

# --- A healthy private config dir produces a real dump ----------------------
healthy_dir="$test_root/healthy"
healthy_out="$test_root/healthy.json"
env OPENGLAD_CONFIG_DIR="$healthy_dir" \
    "$smoke_bin" --scenario smoke_nonempty_scen99 --out "$healthy_out" \
    >/dev/null 2>&1 ||
    fail 'parity_runner_smoke failed against an empty (but usable) config dir'
[[ -s "$healthy_out" ]] || fail 'no dump was written for a healthy run'
grep -Fq '"schema_version":"v1"' "$healthy_out" ||
    fail 'the dump is not a schema-v1 state dump'
grep -Fq '"tick":60' "$healthy_out" ||
    fail 'the dump did not run the row tick budget'
[[ -f "$healthy_dir/campaigns/gladiator.glad" ]] ||
    fail 'the bootstrap did not restore the default campaign archive'

# The row's own predicates are the load-sensitive assertion: a scenario that
# ran against an empty arena fails them.
facts_output=$(
    env OPENGLAD_CONFIG_DIR="$healthy_dir" \
        "$smoke_bin" --scenario smoke_nonempty_scen99 --evaluate-facts 2>/dev/null
)
printf '%s\n' "$facts_output"
grep -Fq '"kind"' <<<"$facts_output" || fail '--evaluate-facts emitted no facts'
if grep -Fq '"ok": false' <<<"$facts_output"; then
    fail 'a predicate failed on a healthy run'
fi
if grep -Fq '"indeterminate": true' <<<"$facts_output"; then
    fail 'a predicate was indeterminate on a healthy run'
fi

# A row that builds its own arena is exempt from the load requirement: the
# four scen9301 rows point at a header-only fixture on purpose, and
# run_mutation_canary_runtime.py --all walks them. (The exemption is keyed on
# that fixture, not on is_branch_internal — treasure_exit_open_prompt_scen99
# is branch-internal and loads a real level.)
env OPENGLAD_CONFIG_DIR="$healthy_dir" \
    "$smoke_bin" --scenario snapshot_dirty_bits_scen9301 \
    --out "$test_root/internal.json" >/dev/null 2>&1 ||
    fail 'a branch-internal row was refused over its by-design stub level'
[[ -s "$test_root/internal.json" ]] ||
    fail 'no dump was written for a branch-internal row'

# ...but a broken bootstrap still refuses even there: that failure is the
# environment, not the row.
set +e
env OPENGLAD_CONFIG_DIR="$broken_dir" \
    "$smoke_bin" --scenario snapshot_dirty_bits_scen9301 \
    --out "$test_root/internal-broken.json" >/dev/null 2>&1
internal_broken_status=$?
set -e
if (( internal_broken_status != 3 )); then
    fail "expected exit 3 for a branch-internal row under a broken bootstrap, got $internal_broken_status"
fi
if [[ -e "$test_root/internal-broken.json" ]]; then
    fail 'a broken bootstrap still wrote a branch-internal dump'
fi

# --- With no OPENGLAD_CONFIG_DIR the tool stays out of the real install ------
# It used to fall through to $HOME/.openglad and rewrite the developer's
# campaign archives on every single run.
fake_home="$test_root/home"
mkdir -p "$fake_home"
env -u OPENGLAD_CONFIG_DIR HOME="$fake_home" \
    "$smoke_bin" --scenario smoke_nonempty_scen99 --out "$test_root/nohome.json" \
    >/dev/null 2>&1 ||
    fail 'parity_runner_smoke failed with OPENGLAD_CONFIG_DIR unset'
[[ -s "$test_root/nohome.json" ]] ||
    fail 'no dump was written with OPENGLAD_CONFIG_DIR unset'
if [[ -e "$fake_home/.openglad" ]]; then
    fail 'parity_runner_smoke wrote into $HOME/.openglad'
fi

# ...and the scratch directory it picked is removed on the way out, so a
# machine that runs the canary a few hundred times does not accumulate one
# restored campaign set per run.
if compgen -G "${TMPDIR:-/tmp}/openglad-parity-smoke-config-*" >/dev/null; then
    fail 'parity_runner_smoke left its private config dir behind'
fi

# --- Concurrent runs must not tear each other's mounts ----------------------
# The scratch dir has to be PER PROCESS. `restore_default_campaigns` copies
# every builtin .glad with overwrite_existing on every run, so a shared
# directory means one run truncating the archive another has mounted: eight
# concurrent runs against the old fixed path failed seven times with exit 3.
declare -a conc_status=()
for i in $(seq 1 8); do
    (
        env -u OPENGLAD_CONFIG_DIR HOME="$fake_home" \
            "$smoke_bin" --scenario smoke_nonempty_scen99 \
            --out "$test_root/conc-$i.json" >/dev/null 2>&1
        printf '%s\n' "$?" > "$test_root/conc-$i.status"
    ) &
done
wait
for i in $(seq 1 8); do
    status=$(cat "$test_root/conc-$i.status")
    conc_status+=("$status")
    if [[ "$status" != 0 ]]; then
        fail "concurrent run $i exited $status; the private config dir is being shared"
    fi
    [[ -s "$test_root/conc-$i.json" ]] ||
        fail "concurrent run $i wrote no dump"
done
# Same scenario, same seed, eight processes: the dumps must agree.
for i in $(seq 2 8); do
    cmp -s "$test_root/conc-1.json" "$test_root/conc-$i.json" ||
        fail "concurrent run $i produced a different dump from run 1"
done

printf 'parity_runner_smoke CLI contract OK\n'
