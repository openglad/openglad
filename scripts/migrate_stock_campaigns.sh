#!/usr/bin/env bash
# Migrate the three hand-authored stock packages (gladiator, tryxian, arenas)
# to the BASE + DECOR tile layering (.fss v11) with tools/grid_migrate.
#
# grid_migrate proves equivalence in-process (per-cell passability across the
# five mover archetypes, concealment, damage semantics, door-frame genre,
# entity-stream identity, footing) and exits nonzero on ANY mismatch, so a
# successful run is the proof obligation, not just a conversion. Weather
# outdoor-vote changes are printed per level (report-only).
#
# The generated packages (ctf, concept, westlands) are NOT migrated here —
# they regenerate from their mapgen tools (scripts/generate_*.sh), which now
# author decor planes directly. Note ctf_mapgen adapts levels from the
# MOUNTED gladiator package, so regenerate CTF only after this script (and a
# re-stage) has run.
#
# The tool reads its input from the staged assets next to the binary
# (build/ci-test/builtin), so the stage_runtime_assets dependency keeps the
# input in sync with the repo; outputs land in the repo builtin/ and the
# script re-stages afterwards so tests see the migrated packages.
#
# Usage: scripts/migrate_stock_campaigns.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/ci-test"

cd "${REPO_ROOT}"

if [ ! -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    cmake --preset ci-test
fi

cmake --build "${BUILD_DIR}" --target grid_migrate -- -j8

for id in org.openglad.gladiator org.openglad.tryxian org.openglad.arenas; do
    "${BUILD_DIR}/grid_migrate" "${id}" "builtin/${id}.glad"
done

# Re-stage so tests (and a subsequent CTF regeneration, which adapts from the
# gladiator package) read the migrated inputs.
cmake --build "${BUILD_DIR}" --target stage_runtime_assets -- -j8
