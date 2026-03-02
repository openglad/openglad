#!/usr/bin/env bash
# Verify no vendor/third-party headers leak into public or internal headers.
# Only resources IO implementation files may include filesystem/archive vendor
# headers directly.
set -euo pipefail

VENDOR_PATTERNS='(physfs\.h|physfsrwops\.h|physfs_internal\.h|zip\.h|zipint\.h|zipconf\.h|yaml\.h|yam\.h|zlib\.h|zconf\.h)'
VENDOR_INCLUDE_REGEX="#include[[:space:]]*[<\"]([^\">]*/)?${VENDOR_PATTERNS}[>\"]"

status=0

# 1. Public headers must never include vendor headers
if grep -rn --include='*.h' -E "${VENDOR_INCLUDE_REGEX}" include/openglad/ 2>/dev/null; then
    echo "ERROR: vendor headers found in include/openglad/ (public headers)" >&2
    status=1
fi

# 2. Only resources IO implementation may include filesystem/archive vendor headers
FS_VENDOR='(physfs\.h|physfsrwops\.h|zip\.h|zipint\.h|zipconf\.h|yaml\.h|yam\.h|zlib\.h|zconf\.h)'
FS_VENDOR_INCLUDE_REGEX="#include[[:space:]]*[<\"]([^\">]*/)?${FS_VENDOR}[>\"]"
if grep -rn --include='*.cpp' --include='*.h' -E "${FS_VENDOR_INCLUDE_REGEX}" src/ \
    | grep -v 'src/resources/io/' | grep -v 'src/resources/platform_io.cpp' | grep -v 'ogfile_yaml' 2>/dev/null; then
    echo "ERROR: filesystem/archive vendor headers found outside src/resources/io/" >&2
    status=1
fi

if [ $status -eq 0 ]; then
    echo "Vendor header check: OK"
fi
exit $status
