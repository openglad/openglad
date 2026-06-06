#!/usr/bin/env bash
# Guard against the "drawcycle" class of bug.
#
# The render layer (src/interface/render/) must be READ-ONLY on network-synced
# entity state: it reads walker/entity fields to draw them, but the authoritative
# (headless) sim owns those fields. A render-side WRITE of a sim-read field is a
# latent bug — in the dedicated/headless server the render path never runs, so
# the field freezes at its spawn value and any sim logic that reads it breaks
# (e.g. drawcycle drove the boomerang/magic-shield orbit and the archmage's
# periodic bonus-viewing; freezing it hung the boomerang on the player). It is
# also invisible to the parity harness unless the master companion replicates the
# render bump. So: render must never call a synced-field setter.
#
# The authoritative list of synced fields is derived from the snapshot APPLY path
# in src/gameplay/world_snapshot.cpp (the single source of truth), so this guard
# auto-tracks new fields.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SNAPSHOT_APPLY="${ROOT}/src/gameplay/world_snapshot.cpp"
RENDER_DIR="${ROOT}/src/interface/render"

if [[ ! -f "${SNAPSHOT_APPLY}" ]]; then
    echo "ERROR: cannot find ${SNAPSHOT_APPLY}" >&2
    exit 2
fi
if [[ ! -d "${RENDER_DIR}" ]]; then
    echo "ERROR: cannot find ${RENDER_DIR}" >&2
    exit 2
fi

# Synced-field setters = every `entity.set_X(snapshot.X...)` in the apply path.
mapfile -t SETTERS < <(
    grep -oE 'entity\.set_[a-z_]+\(snapshot\.' "${SNAPSHOT_APPLY}" \
        | sed -E 's/entity\.(set_[a-z_]+)\(.*/\1/' \
        | sort -u
)

if [[ "${#SETTERS[@]}" -eq 0 ]]; then
    echo "ERROR: derived 0 synced-field setters from ${SNAPSHOT_APPLY}." >&2
    echo "       The apply-path pattern likely changed; update this guard." >&2
    exit 2
fi

status=0
for setter in "${SETTERS[@]}"; do
    if hits=$(grep -rnE "\b${setter}\(" "${RENDER_DIR}" 2>/dev/null); then
        echo "ERROR: render layer writes synced entity field via ${setter}():" >&2
        echo "${hits}" >&2
        status=1
    fi
done

if [[ "${status}" -ne 0 ]]; then
    cat >&2 <<'EOF'

The render layer (src/interface/render/) must be READ-ONLY on synced entity
state — the authoritative sim owns these fields. A render-side write freezes the
field in the headless server (the "drawcycle" bug). Move the write into the sim
(effect::act / living::act / the relevant family on_act), where it is captured
into the snapshot and validated by the parity harness.
EOF
    exit 1
fi

echo "OK: render layer writes no synced entity fields (${#SETTERS[@]} checked)."
