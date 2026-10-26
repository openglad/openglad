#!/usr/bin/env bash
# Guard the ability icons against drifting from their grids.
#
# Every ability icon is a text grid (scripts/ability_icons/grids/<name>.txt,
# and a pack's under scripts/ability_icons/pack_grids/<pack>/) compiled to a
# PNG by scripts/ability_icons/generate.py. --check fails unless every grid
# passes the structural gates (not a filled tile, the edge left to the slot,
# at most 16 colours, no cycled / team / UI band, a locked copy that stands
# off the slot fill), every PNG is its grid re-encoded byte for byte, and
# every PNG under pix/icons/ has a grid. So a picture edited by hand, or a
# grid edited without regenerating, fails the build on every lane.
#
# Bash 3.2 safe (the macOS lane): no mapfile, no associative arrays.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GENERATOR="${ROOT}/scripts/ability_icons/generate.py"

PYTHON=""
for candidate in python3 python; do
    if command -v "${candidate}" >/dev/null 2>&1 &&
        "${candidate}" -c 'import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)' >/dev/null 2>&1; then
        PYTHON="${candidate}"
        break
    fi
done
if [[ -z "${PYTHON}" ]]; then
    echo "ERROR: check_ability_icons needs Python 3 (python3 or python on PATH)" >&2
    exit 2
fi

if ! "${PYTHON}" "${GENERATOR}" --check; then
    echo "Regenerate the icons: python3 scripts/ability_icons/generate.py" >&2
    echo "(a gate refusal names the grid and the rule it breaks; see scripts/ability_icons/README.md)" >&2
    exit 1
fi
