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
  - Version: 2.0.3 (`src/external/physfs/physfs.h`: `PHYSFS_VER_MAJOR/MINOR/PATCH`)
  - Notes: Virtual filesystem and archive mounting.

- zlib (`src/external/physfs/zlib123`)
  - Version: 1.2.3 (`src/external/physfs/zlib123/zlib.h`: `ZLIB_VERSION`)
  - Notes: Bundled as part of the PhysFS vendoring for ZIP/LZ support.

- libzip (`src/external/libzip`)
  - Version: 0.11.1 (`src/external/libzip/zipconf.h`: `LIBZIP_VERSION`)
  - Notes: ZIP container support used by OpenGlad tooling and PhysFS integration.

- libyaml (`src/external/libyaml`)
  - Version: 0.1.4 (`src/external/libyaml/src/config.h`: `YAML_VERSION_STRING`)
  - Notes: YAML parser used via the `yam` wrapper.

- yam (`src/external/yam`)
  - Version: OpenGlad-vendored wrapper (no upstream version tracked here).
  - Notes: Small adapter around libyaml to produce higher-level events.

- MicroPather (`src/external/micropather`)
  - Version: Not encoded in headers; licensed "Grinning Lizard Utilities" (2000-2009).
  - Notes: A* pathfinding implementation.

## Upgrade Checklist

1. Record the new upstream version (or commit hash) in this file.
2. Replace the vendored subtree (avoid mixing old/new files).
3. Rebuild: `cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j24`
4. Run tests: `cd build && ctest --output-on-failure`
5. If needed, adjust wrappers (prefer changes in OpenGlad code, not in vendored code).

