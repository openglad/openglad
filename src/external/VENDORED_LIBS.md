# Vendored Libraries (Version Tracking)

This repo vendors several upstream libraries under `src/external/`.

These are largely upstream C/C++ codebases that may use legacy patterns (e.g. `sprintf`,
C casts, etc.). We intentionally build these as separate CMake targets and suppress
warnings for them to keep warnings actionable in OpenGlad code while still allowing
sanitizers/tests to catch real issues.

## Policy

- Prefer upgrading by replacing the vendored subtree with a known-good upstream release.
- Avoid "refactoring in place" in vendored code unless there is a targeted correctness fix.
- Keep OpenGlad code interacting with these libraries behind small wrappers where practical.

## Libraries

- PhysicsFS (`src/external/physfs`)
  - Version: 3.2.0 (`src/external/physfs/physfs.h`: `PHYSFS_VER_MAJOR/MINOR/PATCH`)
  - Upstream: https://github.com/icculus/physfs
  - Notes: Virtual filesystem and archive mounting. Uses built-in miniz for ZIP support.

- zlib (`src/external/physfs/zlib123`)
  - Version: 1.3.1 (`src/external/physfs/zlib123/zlib.h`: `ZLIB_VERSION`)
  - Upstream: https://github.com/madler/zlib
  - Notes: Used by libzip for compression. PhysFS 3.x uses its own built-in miniz instead.

- libzip (`src/external/libzip`)
  - Version: 1.11.3 (`src/external/libzip/zipconf.h`: `LIBZIP_VERSION`)
  - Upstream: https://github.com/nih-at/libzip
  - Notes: ZIP container support used directly by OpenGlad for campaign package creation/extraction.
    config.h and zipconf.h are manually maintained for Linux. zip_err_str.c is auto-generated
    from zip.h/zipint.h error definitions.

- libyaml (`src/external/libyaml`)
  - Version: 0.2.5 (`src/external/libyaml/src/config.h`: `YAML_VERSION_STRING`)
  - Upstream: https://github.com/yaml/libyaml
  - Notes: YAML parser used via the `yam` wrapper.

- yam (`src/external/yam`)
  - Version: 0.1.0 (OpenGlad-vendored wrapper, no upstream repository).
  - Notes: Small C++ adapter around libyaml to produce higher-level events.

- MicroPather (`src/external/micropather`)
  - Version: Latest from upstream master (2016-10-18 commit).
  - Upstream: https://github.com/leethomason/MicroPather
  - Notes: A* pathfinding implementation. GRINLIZ_NO_STL is commented out to use std::vector.

## Upgrade Checklist

1. Record the new upstream version (or commit hash) in this file.
2. Replace the vendored subtree (avoid mixing old/new files).
3. Rebuild: `cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j24`
4. Run tests: `cd build && ctest --output-on-failure`
5. If needed, adjust wrappers (prefer changes in OpenGlad code, not in vendored code).

