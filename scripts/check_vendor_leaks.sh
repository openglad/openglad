#!/usr/bin/env bash
# Verify no external dependency headers leak into public or internal headers.
# Only resources IO implementation files and the SDL PhysFS RWops adapter may
# include filesystem/archive dependency headers directly.
set -euo pipefail

VENDOR_PATTERNS='(physfs\.h|physfsrwops\.h|physfs_internal\.h|zip\.h|zipint\.h|zipconf\.h|yaml\.h|zlib\.h|zconf\.h|lua\.h|lauxlib\.h|lualib\.h|lua\.hpp|luaconf\.h)'
VENDOR_INCLUDE_REGEX="#include[[:space:]]*[<\"]([^\">]*/)?${VENDOR_PATTERNS}[>\"]"

status=0

# 1. Public headers must never include external dependency headers
if grep -rn --include='*.h' -E "${VENDOR_INCLUDE_REGEX}" include/openglad/ 2>/dev/null; then
    echo "ERROR: external dependency headers found in include/openglad/ (public headers)" >&2
    status=1
fi

# 1b. Lua headers may only be included by the scripting VM implementation
LUA_VENDOR='(lua\.h|lauxlib\.h|lualib\.h|lua\.hpp|luaconf\.h)'
LUA_INCLUDE_REGEX="#include[[:space:]]*[<\"]([^\">]*/)?${LUA_VENDOR}[>\"]"
if grep -rn --include='*.cpp' --include='*.h' -E "${LUA_INCLUDE_REGEX}" src/ \
    | grep -v 'src/gameplay/script/' 2>/dev/null; then
    echo "ERROR: Lua headers found outside src/gameplay/script/" >&2
    status=1
fi

# 2. Only resources IO implementation may include filesystem/archive dependency headers
FS_VENDOR='(physfs\.h|physfsrwops\.h|zip\.h|zipint\.h|zipconf\.h|yaml\.h|zlib\.h|zconf\.h)'
FS_VENDOR_INCLUDE_REGEX="#include[[:space:]]*[<\"]([^\">]*/)?${FS_VENDOR}[>\"]"
if grep -rn --include='*.cpp' --include='*.h' -E "${FS_VENDOR_INCLUDE_REGEX}" src/ \
    | grep -v 'src/resources/io/' \
    | grep -v 'src/resources/campaign_yaml.cpp' \
    | grep -v 'src/resources/classpack_yaml.cpp' \
    | grep -v 'src/resources/gparser.cpp' \
    | grep -v 'src/resources/platform_io.cpp' \
    | grep -v 'src/platform/sdl/physfs_rwops_bridge.cpp' 2>/dev/null; then
    echo "ERROR: filesystem/archive dependency headers found outside src/resources/io/" >&2
    status=1
fi

# 3. Component include boundary check (Phase 12)
# gameplay  -> core, gameplay
# resources -> core, gameplay, resources
# interface -> core, gameplay, resources, interface
# platform  -> unrestricted (top-level orchestrator)
is_allowed_dep() {
    local source_component="$1"
    local dep_component="$2"
    case "${source_component}" in
        gameplay)
            [[ "${dep_component}" == "core" || "${dep_component}" == "gameplay" ]]
            ;;
        resources)
            [[ "${dep_component}" == "core" || "${dep_component}" == "gameplay" || "${dep_component}" == "resources" ]]
            ;;
        interface)
            [[ "${dep_component}" == "core" || "${dep_component}" == "gameplay" || "${dep_component}" == "resources" || "${dep_component}" == "interface" ]]
            ;;
        platform)
            return 0
            ;;
        *)
            return 0
            ;;
    esac
}

source_component_for_file() {
    local file="$1"
    case "${file}" in
        src/gameplay/*|include/openglad/gameplay/*)
            echo "gameplay"
            ;;
        src/resources/*|include/openglad/resources/*)
            echo "resources"
            ;;
        src/interface/*|include/openglad/interface/*)
            echo "interface"
            ;;
        src/platform/*|include/openglad/platform/*)
            echo "platform"
            ;;
        *)
            echo ""
            ;;
    esac
}

dep_component_for_include_prefix() {
    local prefix="$1"
    case "${prefix}" in
        core)
            echo "core"
            ;;
        gameplay)
            echo "gameplay"
            ;;
        resources)
            echo "resources"
            ;;
        interface)
            echo "interface"
            ;;
        platform)
            echo "platform"
            ;;
        legacy)
            echo "legacy"
            ;;
        *)
            # Ignore compatibility wrapper prefixes from earlier architecture
            # stages; the component-namespaced headers above are the strict
            # enforcement surface for Phase 12.
            echo ""
            ;;
    esac
}

while IFS= read -r file; do
    source_component="$(source_component_for_file "${file}")"
    [ -z "${source_component}" ] && continue

    while IFS= read -r include_line; do
        include_target="$(printf '%s' "${include_line}" | sed -E 's/^.*<openglad\/([^>]+)>.*/\1/')"
        include_prefix="${include_target%%/*}"
        dep_component="$(dep_component_for_include_prefix "${include_prefix}")"
        [ -z "${dep_component}" ] && continue

        if ! is_allowed_dep "${source_component}" "${dep_component}"; then
            echo "ERROR: ${file} includes <openglad/${include_target}> (component=${dep_component}) but ${source_component} may not depend on it" >&2
            status=1
        fi
    done < <(grep -nE '^[[:space:]]*#include[[:space:]]*<openglad/[^>]+>' "${file}" || true)
done < <(find src/gameplay src/resources src/interface src/platform \
               include/openglad/gameplay include/openglad/resources include/openglad/interface include/openglad/platform \
               -type f \( -name '*.cpp' -o -name '*.h' \) 2>/dev/null)

if [ $status -eq 0 ]; then
    echo "External dependency header check: OK"
fi
exit $status
