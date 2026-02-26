#!/usr/bin/env bash
# Verify no vendor/third-party headers leak into public or internal headers.
# Only src/resources/io/ and src/platform/sdl/io/ may include filesystem/archive vendor headers.
set -euo pipefail

VENDOR_PATTERNS='(physfs\.h|physfsrwops\.h|physfs_internal\.h|zip\.h|zipint\.h|zipconf\.h|yaml\.h|yam\.h|zlib\.h|zconf\.h)'

status=0

# 1. Public headers must never include vendor headers
if grep -rn --include='*.h' -E "#include.*${VENDOR_PATTERNS}" include/openglad/ 2>/dev/null; then
    echo "ERROR: vendor headers found in include/openglad/ (public headers)" >&2
    status=1
fi

# 2. Only resource I/O implementation files may include filesystem/archive vendor headers
FS_VENDOR='(physfs\.h|physfsrwops\.h|zip\.h|zipint\.h|zipconf\.h|yaml\.h|yam\.h|zlib\.h|zconf\.h)'
if grep -rn --include='*.cpp' --include='*.h' -E "#include.*${FS_VENDOR}" src/ \
    | grep -v 'src/resources/io/' | grep -v 'src/platform/sdl/io/' | grep -v 'ogfile_yaml' 2>/dev/null; then
    echo "ERROR: filesystem/archive vendor headers found outside src/resources/io/" >&2
    status=1
fi

if [ $status -eq 0 ]; then
    echo "Vendor header check: OK"
fi
exit $status
