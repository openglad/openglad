#!/bin/bash
# Package a self-contained, relocatable Windows bundle of OpenGlad.
#
# Ported from the OpenLieroX Windows packaging pipeline (MSYS2 MinGW64) and
# adapted to OpenGlad's layout. Meant to run from an MSYS2 MINGW64 shell so
# that ntldd/cygpath are available and the DLLs resolve to /mingw64/bin.
#
# Layout produced (run-in-place — matches get_asset_path(), which resolves the
# asset root to the directory of the executable; on Windows the loader also
# searches the executable's directory for DLLs, so everything sits flat):
#
#   openglad-<version>-windows/
#     openglad.exe        <- game binary
#     openscen.exe        <- level editor
#     *.dll               <- MinGW runtime + SDL2 DLLs the binaries need
#     cfg/ pix/ sound/ builtin/ gamecontrollerdb.txt   <- runtime assets
#
# Usage: scripts/package_windows_bundle.sh <build-dir>
#   <build-dir> defaults to build/dev-release
set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$(pwd)"

BUILD_DIR="${1:-build/dev-release}"

GAME_BIN="${BUILD_DIR}/openglad.exe"
EDITOR_BIN="${BUILD_DIR}/openscen.exe"

if [[ ! -f "${GAME_BIN}" ]]; then
    echo "ERROR: ${GAME_BIN} not found — build the project first" >&2
    exit 1
fi

# Version string: prefer a git description so bundle names are unique and
# traceable; fall back to the CMake project version when git is unavailable.
if VERSION="$(git describe --tags --always --dirty 2>/dev/null)"; then
    :
else
    VERSION="$(grep -oP 'project\(OpenGlad VERSION \K[0-9.]+' CMakeLists.txt || echo unknown)"
fi

PACKAGE_NAME="openglad-${VERSION}-windows"
PACKAGE_DIR="distrib/${PACKAGE_NAME}"

echo "==> Packaging ${PACKAGE_NAME} from ${BUILD_DIR}"

rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}"

# --- Binaries ---------------------------------------------------------------
cp -v "${GAME_BIN}" "${PACKAGE_DIR}/"
if [[ -f "${EDITOR_BIN}" ]]; then
    cp -v "${EDITOR_BIN}" "${PACKAGE_DIR}/"
fi

# --- Runtime assets (must sit next to the binary) ---------------------------
cp -av cfg pix sound builtin "${PACKAGE_DIR}/"
cp -v gamecontrollerdb.txt "${PACKAGE_DIR}/"

# --- Bundle the MinGW runtime + SDL2 DLLs -----------------------------------
# ntldd -R walks the full dependency tree and prints lines like:
#   libfoo.dll => C:\msys64\mingw64\bin\libfoo.dll (0x...)
# We keep only the ones under mingw64 (system DLLs like KERNEL32 live in
# C:\Windows and must NOT be bundled), map the Windows path back to a POSIX
# path with cygpath, and copy once across both binaries (they share most DLLs).
{
    for bin in "${PACKAGE_DIR}/openglad.exe" "${PACKAGE_DIR}/openscen.exe"; do
        [[ -f "$bin" ]] || continue
        ntldd -R "$bin"
    done
} \
    | awk -F'=>' '/=>/ { print $2 }' \
    | awk '{ print $1 }' \
    | grep -i 'mingw64' \
    | sort -u \
    | while read -r dll; do
        winpath="$(cygpath -u "$dll")"
        cp -n "$winpath" "${PACKAGE_DIR}/"
      done

cd "${REPO_ROOT}"
echo "==> Bundle ready: ${PACKAGE_DIR}"
ls -la "${PACKAGE_DIR}"
