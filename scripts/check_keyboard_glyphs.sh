#!/usr/bin/env bash
# Guard the keyboard picture's glyph atlas against drifting from its
# generator.
#
# The keyboard picture (KEYMAP, CHANGELOG!) prints its DejaVu labels from a
# committed glyph atlas, pix/keyboard/glyphs.png, and its table,
# src/interface/ui/keyboard_glyphs.inc, both written by
# scripts/keyboard_picture/generate_glyphs.py. --check fails unless the atlas
# and the generator hash to what the table records (an atlas edited by hand,
# or a generator edited without regenerating, fails on every lane), and,
# where Pillow and the DejaVu fonts are present (the nix dev shell), unless a
# fresh render equals the committed atlas and table.
#
# Bash 3.2 safe (the macOS lane): no mapfile, no associative arrays.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GENERATOR="${ROOT}/scripts/keyboard_picture/generate_glyphs.py"

PYTHON=""
for candidate in python3 python; do
    if command -v "${candidate}" >/dev/null 2>&1 &&
        "${candidate}" -c 'import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)' >/dev/null 2>&1; then
        PYTHON="${candidate}"
        break
    fi
done
if [[ -z "${PYTHON}" ]]; then
    echo "ERROR: check_keyboard_glyphs needs Python 3 (python3 or python on PATH)" >&2
    exit 2
fi

if ! "${PYTHON}" "${GENERATOR}" --check; then
    echo "Regenerate the atlas: nix develop -c python3 scripts/keyboard_picture/generate_glyphs.py" >&2
    exit 1
fi
