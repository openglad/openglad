#!/bin/bash
# Package a self-contained, relocatable Linux bundle of OpenGlad.
#
# Layout produced (run-in-place — matches get_asset_path(), which resolves the
# asset root to the directory of the executable):
#
#   openglad-<version>-linux-bundle/
#     openglad            <- game binary  (rpath $ORIGIN/lib)
#     openscen            <- level editor (rpath $ORIGIN/lib)
#     cfg/ pix/ sound/ builtin/ gamecontrollerdb.txt   <- runtime assets
#     lib/*.so*           <- bundled non-glibc shared libraries
#
# Usage: scripts/package_linux_bundle.sh <build-dir>
#   <build-dir> defaults to build/dev-release
set -euo pipefail

cd "$(dirname "$0")/.."
REPO_ROOT="$(pwd)"

BUILD_DIR="${1:-build/dev-release}"

# Version string: prefer a git description so bundle names are unique and
# traceable; fall back to the CMake project version when git is unavailable.
if VERSION="$(git describe --tags --always --dirty 2>/dev/null)"; then
    :
else
    VERSION="$(grep -oP 'project\(OpenGlad VERSION \K[0-9.]+' CMakeLists.txt || echo unknown)"
fi

PACKAGE_NAME="openglad-${VERSION}-linux-bundle"
PACKAGE_DIR="distrib/${PACKAGE_NAME}"

echo "==> Packaging ${PACKAGE_NAME} from ${BUILD_DIR}"

rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}/lib"

# --- Binaries ---------------------------------------------------------------
cp -v "${BUILD_DIR}/openglad" "${PACKAGE_DIR}/"
if [[ -x "${BUILD_DIR}/openscen" ]]; then
    cp -v "${BUILD_DIR}/openscen" "${PACKAGE_DIR}/"
fi

# --- Runtime assets (must sit next to the binary) ---------------------------
cp -av cfg pix sound builtin "${PACKAGE_DIR}/"
cp -v gamecontrollerdb.txt "${PACKAGE_DIR}/"

# --- Bundle non-glibc shared libraries --------------------------------------
# We must NOT bundle any part of glibc. Its sub-libraries (libpthread, libm,
# libdl, librt, ...) share internal GLIBC_PRIVATE symbols with libc.so.6 and
# are only ABI-compatible with the exact libc.so.6 they shipped with. The
# final binary always loads the *host's* libc.so.6 and dynamic loader (baked
# into the ELF interpreter / NEEDED, not overridable by rpath), so bundling a
# foreign libpthread.so.0 next to a host libc.so.6 breaks with e.g.
#   undefined symbol: __libc_pthread_init, version GLIBC_PRIVATE
# Building against an older glibc than the deploy target and letting the whole
# glibc family resolve from the host is both correct and forward-safe.
GLIBC_EXCLUDE='/(ld-linux[^/]*|libc|libpthread|libm|libmvec|libdl|librt|libresolv|libutil|libanl|libBrokenLocale|libnss_[^/]*|libcrypt)\.so'

# Collect the (deduplicated) union of shared libraries across all binaries,
# then copy once — openglad and openscen share most of their dependencies.
{
    for bin in "${PACKAGE_DIR}/openglad" "${PACKAGE_DIR}/openscen"; do
        [[ -x "$bin" ]] || continue
        ldd "$bin" | grep "=> /" | awk '{print $3}'
    done
} | grep -vE "${GLIBC_EXCLUDE}" | sort -u \
  | xargs -I '{}' cp -v '{}' "${PACKAGE_DIR}/lib/"

# --- Patch rpaths so the bundle is relocatable ------------------------------
cd "${PACKAGE_DIR}"
for bin in openglad openscen; do
    [[ -x "$bin" ]] || continue
    patchelf --set-rpath '$ORIGIN/lib' --force-rpath "$bin"
done
# Transitive deps of bundled libs resolve from the same lib/ directory.
if compgen -G 'lib/*.so*' > /dev/null; then
    patchelf --set-rpath '$ORIGIN' --force-rpath lib/*.so*
fi

cd "${REPO_ROOT}"
echo "==> Bundle ready: ${PACKAGE_DIR}"
ls -la "${PACKAGE_DIR}"
