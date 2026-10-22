#!/usr/bin/env bash
# Guard the CHANGELOG! keyboard pictures against drifting from the key tables.
#
# The chain (docs/ability-select-design.md F.4):
#   1. tests/unit/test_keyboard_map_asset.cpp builds the text form of the
#      default keys from the live key tables and fails unless it equals
#      scripts/keyboard_map/keyboard_map.txt;
#   2. this script fails unless that text file hashes to the value in
#      scripts/keyboard_map/keyboard_map.stamp;
#   3. and unless both pictures (pix/changelog/keyboard_belt.png and
#      keyboard_belt_web.png) hash to theirs.
# Only scripts/keyboard_map/make_keyboard_map.py --write rewrites the stamp,
# and it draws both pictures from the text file in the same run. So a key
# change that rewrote the text without redrawing (or a picture edited by
# hand) fails here, on every lane.
#
# Bash 3.2 safe (the macOS lane): no mapfile, no associative arrays.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAMP="${ROOT}/scripts/keyboard_map/keyboard_map.stamp"

if [[ ! -f "${STAMP}" ]]; then
    echo "ERROR: cannot find ${STAMP}" >&2
    exit 2
fi

hash_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        echo "ERROR: neither sha256sum nor shasum is installed" >&2
        exit 2
    fi
}

status=0
count=0
while IFS= read -r line || [[ -n "${line}" ]]; do
    line="${line%$'\r'}"
    [[ -z "${line}" ]] && continue
    want="${line%%  *}"
    rel="${line#*  }"
    if [[ "${want}" == "${line}" || -z "${rel}" ]]; then
        echo "ERROR: ${STAMP}: malformed line: ${line}" >&2
        exit 2
    fi
    count=$((count + 1))
    if [[ ! -f "${ROOT}/${rel}" ]]; then
        echo "check_keyboard_map: ${rel} is missing" >&2
        status=1
        continue
    fi
    got="$(hash_of "${ROOT}/${rel}")"
    if [[ "${got}" != "${want}" ]]; then
        echo "check_keyboard_map: ${rel} changed without a redraw" >&2
        echo "  stamp: ${want}" >&2
        echo "  file:  ${got}" >&2
        status=1
    fi
done < "${STAMP}"

if [[ "${count}" -ne 3 ]]; then
    echo "check_keyboard_map: ${STAMP} names ${count} files, want 3" >&2
    status=1
fi

if [[ "${status}" -ne 0 ]]; then
    echo "The CHANGELOG! keyboard pictures no longer match their key list." >&2
    echo "Redraw them: nix develop -c python3 scripts/keyboard_map/make_keyboard_map.py --write" >&2
    exit 1
fi
echo "check_keyboard_map: keyboard_map.txt and both pictures match the stamp"
