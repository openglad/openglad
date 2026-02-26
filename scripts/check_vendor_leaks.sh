#!/usr/bin/env bash
# Verify no vendor/third-party headers leak into public or internal headers.
# Only src/resources/io/ and src/platform/sdl/io/ may include filesystem/archive vendor headers.
set -euo pipefail

VENDOR_PATTERNS='(physfs\.h|physfsrwops\.h|physfs_internal\.h|zip\.h|zipint\.h|zipconf\.h|yaml\.h|yam\.h|zlib\.h|zconf\.h|SDL(_[A-Za-z0-9]+)?\.h)'

status=0

# Transitional allowlist while interface decoupling from platform headers is in progress.
# New non-platform platform includes should be added by explicit exception only.
is_allowed_platform_include() {
    local file="$1"
    case "$file" in
        src/interface/input/input.cpp|\
        src/interface/render/graphlib.cpp|\
        src/interface/render/pal32.cpp|\
        src/interface/render/pixie.cpp|\
        src/interface/render/radar.cpp|\
        src/interface/render/sai2x.cpp|\
        src/interface/render/text.cpp|\
        src/interface/render/view.cpp|\
        src/interface/render/walker_draw.cpp|\
        src/interface/screen.cpp|\
        src/interface/ui/button.cpp|\
        src/interface/ui/campaign_picker.cpp|\
        src/interface/ui/help.cpp|\
        src/interface/ui/intro.cpp|\
        src/interface/ui/level_editor.cpp|\
        src/interface/ui/level_editor_file_ops.cpp|\
        src/interface/ui/level_editor_tools.cpp|\
        src/interface/ui/level_editor_ui.cpp|\
        src/interface/ui/level_picker.cpp|\
        src/interface/ui/picker.cpp|\
        src/interface/ui/picker_accessible_levels.cpp|\
        src/interface/ui/picker_dialogs.cpp|\
        src/interface/ui/picker_input.cpp|\
        src/interface/ui/picker_main_menu.cpp|\
        src/interface/ui/picker_team_build.cpp|\
        src/interface/ui/results_screen.cpp|\
        src/gameplay/smooth.cpp)
            return 0
            ;;
    esac
    return 1
}

# Transitional allowlist for existing SDL includes in non-platform implementation files.
is_allowed_sdl_include() {
    local file="$1"
    case "$file" in
        src/interface/input/input.cpp|\
        src/interface/render/graphlib.cpp|\
        src/interface/render/obmap_debug_draw.cpp|\
        src/interface/render/pal32.cpp|\
        src/interface/render/pixie.cpp|\
        src/interface/render/sai2x.cpp|\
        src/interface/render/text.cpp|\
        src/interface/render/view.cpp|\
        src/interface/render/walker_draw.cpp|\
        src/interface/ui/campaign_picker.cpp|\
        src/interface/ui/help.cpp|\
        src/interface/ui/level_editor.cpp|\
        src/interface/ui/level_editor_ui.cpp|\
        src/interface/ui/level_picker.cpp|\
        src/interface/ui/picker.cpp|\
        src/interface/ui/picker_dialogs.cpp|\
        src/interface/ui/picker_input.cpp|\
        src/interface/ui/picker_main_menu.cpp|\
        src/interface/ui/picker_team_build.cpp|\
        src/interface/ui/results_screen.cpp)
            return 0
            ;;
    esac
    return 1
}

# 1. Public headers must never include vendor headers
if grep -rn --include='*.h' -E "#include.*${VENDOR_PATTERNS}" \
    include/openglad/core include/openglad/gameplay include/openglad/resources include/openglad/interface include/openglad/legacy \
    2>/dev/null; then
    echo "ERROR: vendor headers found in non-platform public headers" >&2
    status=1
fi

# 1b. Non-platform source/header files must not include SDL implementation headers.
SDL_VENDOR='(SDL(_[A-Za-z0-9]+)?\.h)'
sdl_hits="$(grep -rn --include='*.h' --include='*.cpp' -E "#include.*${SDL_VENDOR}" \
    src/core src/gameplay src/resources src/interface include/openglad/core include/openglad/gameplay include/openglad/resources include/openglad/interface include/openglad/legacy \
    2>/dev/null || true)"

if [ -n "$sdl_hits" ]; then
    sdl_violation=0
    while IFS= read -r hit; do
        [ -z "$hit" ] && continue
        hit_file="${hit%%:*}"
        if is_allowed_sdl_include "$hit_file"; then
            continue
        fi
        echo "$hit"
        sdl_violation=1
    done <<< "$sdl_hits"
    if [ $sdl_violation -ne 0 ]; then
        echo "ERROR: SDL headers found in non-platform source/header files" >&2
        status=1
    fi
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
#   core -> core
#   gameplay -> core, gameplay
#   resources -> core, gameplay, resources
#   interface -> core, gameplay, resources, interface
#   platform -> unrestricted
while IFS= read -r file; do
    component=""
    allowed=""
    case "$file" in
        src/core/*)
            component="core"
            allowed="core"
            ;;
        src/gameplay/*)
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
        src/platform/*)
            component="platform"
            allowed=""
            ;;
        *)
            continue
            ;;
    esac

    if [ "$component" = "platform" ]; then
        continue
    fi

    while IFS= read -r include_path; do
        [ -z "$include_path" ] && continue
        include_root="$(printf '%s' "$include_path" | cut -d'/' -f2)"
        case "$include_root" in
            core|gameplay|resources|interface|platform) ;;
            *) continue ;;
        esac

        if [ "$include_root" = "platform" ]; then
            if is_allowed_platform_include "$file"; then
                continue
            fi
            echo "ERROR: component include violation: $file includes $include_path (allowed: $allowed)" >&2
            status=1
            continue
        fi

        ok=1
        for a in $allowed; do
            if [ "$include_root" = "$a" ]; then
                ok=0
                break
            fi
        done
        if [ $ok -ne 0 ]; then
            echo "ERROR: component include violation: $file includes $include_path (allowed: $allowed)" >&2
            status=1
        fi
    done < <(grep -Eo '#include[[:space:]]+<openglad/[A-Za-z0-9_./-]+>' "$file" \
        | sed -E 's@#include[[:space:]]+<([^>]+)>@\1@' \
        | sort -u)
done < <(find src -type f \( -name '*.cpp' -o -name '*.h' \))

if [ $status -eq 0 ]; then
    echo "Component include check: OK"
fi

# 4. Public header graph checks for include/openglad/<layer>/.
# Transitional guardrail: public component headers must not depend on platform.
while IFS= read -r file; do
    case "$file" in
        include/openglad/core/*)
            ;;
        include/openglad/gameplay/*)
            ;;
        include/openglad/resources/*)
            ;;
        include/openglad/interface/*)
            ;;
        *)
            continue
            ;;
    esac

    while IFS= read -r include_root; do
        [ -z "$include_root" ] && continue
        case "$include_root" in
            core|gameplay|resources|interface|legacy|platform) ;;
            *) continue ;;
        esac

        if [ "$include_root" = "platform" ]; then
            echo "ERROR: header include violation: $file includes openglad/platform" >&2
            status=1
        fi
    done < <(grep -Eo '#include[[:space:]]+<openglad/[a-z_]+/' "$file" \
        | sed -E 's@#include[[:space:]]+<openglad/([a-z_]+)/@\1@' \
        | sort -u)
done < <(find include/openglad -type f -name '*.h')

if [ $status -eq 0 ]; then
    echo "Header include graph check: OK"
fi

# 5. Detect cycles in the public include graph (include/openglad/**/*.h).
# Build directed edges: header -> directly included public header.
edge_file="$(mktemp)"
err_file="$(mktemp)"
trap 'rm -f "$edge_file" "$err_file"' EXIT

while IFS= read -r file; do
    from="${file#./}"
    from="${from#include/}"
    while IFS= read -r include_path; do
        [ -z "$include_path" ] && continue
        case "$include_path" in
            openglad/*.h)
                if [ -f "include/$include_path" ]; then
                    printf '%s %s\n' "$from" "$include_path" >> "$edge_file"
                fi
                ;;
        esac
    done < <(grep -Eo '#include[[:space:]]+<openglad/[A-Za-z0-9_./-]+>' "$file" \
        | sed -E 's@#include[[:space:]]+<([^>]+)>@\1@' \
        | sort -u)
done < <(find include/openglad -type f -name '*.h')

if [ -s "$edge_file" ]; then
    if ! tsort "$edge_file" >/dev/null 2>"$err_file"; then
        echo "ERROR: public header include cycle detected" >&2
        cat "$err_file" >&2
        status=1
    fi
fi

if [ $status -eq 0 ]; then
    echo "Header cycle check: OK"
fi

exit $status
