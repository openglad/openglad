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

# 3. Component include dependency checks (Phase 12).
# Enforce allowed header roots for component implementation files.
# Rule set:
#   gameplay -> core, gameplay
#   resources -> core, gameplay, resources
#   interface -> core, gameplay, resources, interface
#   platform/sdl -> unrestricted
while IFS= read -r file; do
    component=""
    allowed=""
    case "$file" in
        src/core/*|src/gameplay/*)
            component="gameplay"
            allowed="core gameplay"
            ;;
        src/resources/*)
            component="resources"
            allowed="core gameplay resources"
            ;;
        src/interface/*)
            component="interface"
            allowed="core gameplay resources interface"
            ;;
        src/platform/sdl/*)
            component="platform_sdl"
            allowed=""
            ;;
        *)
            continue
            ;;
    esac

    if [ "$component" = "platform_sdl" ]; then
        continue
    fi

    while IFS= read -r include_root; do
        [ -z "$include_root" ] && continue
        case "$include_root" in
            core|gameplay|resources|interface) ;;
            *) continue ;;
        esac
        ok=1
        for a in $allowed; do
            if [ "$include_root" = "$a" ]; then
                ok=0
                break
            fi
        done
        if [ $ok -ne 0 ]; then
            echo "ERROR: component include violation: $file includes openglad/$include_root (allowed: $allowed)" >&2
            status=1
        fi
    done < <(grep -Eo '#include[[:space:]]+<openglad/[a-z_]+/' "$file" \
        | sed -E 's@#include[[:space:]]+<openglad/([a-z_]+)/@\1@' \
        | sort -u)
done < <(find src -type f \( -name '*.cpp' -o -name '*.h' \))

if [ $status -eq 0 ]; then
    echo "Component include check: OK"
fi
exit $status
