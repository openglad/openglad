# Building OpenGlad

OpenGlad uses **CMake 3.25+** as its primary build system. It targets **C++20** and builds on Linux, macOS, Windows, and the web (via Emscripten).

## Dependencies

### Linux (Debian/Ubuntu)

```bash
sudo apt-get install cmake ninja-build pkg-config \
  libphysfs-dev libzip-dev libyaml-dev zlib1g-dev
```

SDL3 is auto-fetched (pinned `release-3.4.8`) when no system sdl3 is found;
install `libsdl3-dev` where available (Ubuntu 25.04+) to use a system copy.

### macOS (Homebrew)

```bash
brew install cmake ninja pkg-config sdl3 physfs libzip libyaml zlib
```

### Windows

Install [vcpkg](https://github.com/microsoft/vcpkg) and use the vcpkg preset (see below), or install SDL3 manually and ensure it is on the system `PKG_CONFIG_PATH`.

### Nix

OpenGlad includes a Nix flake for Linux development and packaged native builds.
On a bare container with Nix flakes enabled, enter the development environment
with:

```bash
nix develop
```

The shell provides the native CMake toolchain and dependencies used by the
documented presets: CMake, Ninja, pkg-config, SDL3, ncurses, GTest,
lua-language-server, coverage tools, debugger tools, and common script
utilities.

Inside the shell, use the standard CMake presets:

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug
./build/dev-debug/openglad
```

The flake also exposes a packaged build that installs runtime assets and wraps
the executables so they launch from the correct asset directory:

```bash
nix build .#openglad
nix run .#
```

`nix build` produces wrappers for `openglad`, `openscen`, `openglad_text`,
`openglad_server`, `openglad_curses`, and `openglad_demo`.

---

## Quick Start (CMake Presets)

The recommended way to build is with CMake presets. These are defined in `CMakePresets.json`.

### Development Debug Build

```bash
cmake --preset dev-debug
cmake --build --preset dev-debug
```

This produces `openglad` and `openscen` (the level editor) in `build/dev-debug/`.

### Run the Game

```bash
./build/dev-debug/openglad
```

### Run Tests

```bash
cmake --preset ci-test
cmake --build --preset ci-test
ctest --preset ci-test
```

---

## All CMake Presets

| Preset | Purpose |
|--------|---------|
| `dev-debug` | Development debug build (Ninja, tests enabled) |
| `dev-release` | Optimized build (RelWithDebInfo, no tests) |
| `ci-test` | CI standard build + full test suite |
| `ci-asan` | AddressSanitizer + UBSan build + tests |
| `ci-tsan` | ThreadSanitizer build + tests |
| `ci-coverage` | Coverage-instrumented build (used by the coverage gate) |
| `ci-fuzz` | libFuzzer targets |
| `dev-debug-vcpkg` | Debug build using vcpkg toolchain |
| `dev-debug-conan` | Debug build using Conan toolchain |
| `web-emscripten` | Emscripten WebAssembly build |

### vcpkg Toolchain

```bash
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset dev-debug-vcpkg
cmake --build --preset dev-debug-vcpkg
```

### Conan Toolchain

```bash
conan install . --output-folder=build/conan --build=missing
cmake --preset dev-debug-conan
cmake --build --preset dev-debug-conan
```

---

## Web Build (Emscripten)

The web build compiles to WebAssembly, allowing the game to run in a browser.

### Prerequisites

Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh   # Run in each new terminal, or add to shell profile
```

### Build

```bash
./scripts/build_web.sh
```

This outputs to `dist/`:
- `play.html` — HTML shell with canvas
- `play.js` — JavaScript runtime
- `play.wasm` — WebAssembly binary
- `play.data` — Packaged game assets
- `manifest.webmanifest`, `icon.svg`, `icon-192.png`, `icon-512.png`,
  `apple-touch-icon.png` — install metadata and platform icons for
  standalone/fullscreen play

On supporting mobile browsers, the manifest also makes **Add to Home Screen**
available. The installed app permits both portrait and landscape play.

### Run Locally

```bash
cd dist && python3 -m http.server 8080
# Open http://localhost:8080/index.html
```

### Pull Request Previews

After the required WASM Playwright tests pass, CI deploys same-repository pull
requests to a stable Cloudflare Pages address:

```text
https://pr-<number>.openglad.pages.dev
```

Each update to the pull request replaces that alias with the newest successful
build. The workflow also records the link as a GitHub Deployment and in its job
summary. Pull requests from forks and Dependabot pull requests still build and
test, but are not deployed: repository Cloudflare credentials are never made
available to their code, and their artifacts are not promoted by a privileged
follow-up workflow.

Maintainer setup for `.github/workflows/wasm-e2e.yml`:

- Configure the `CLOUDFLARE_API_TOKEN` Actions secret with Account /
  Cloudflare Pages / Edit permission.
- Configure the `CLOUDFLARE_ACCOUNT_ID` Actions secret for the account that
  owns the `openglad` Pages project.
- Keep preview deployments enabled for that project. CI deploys the synthetic
  branch `pr-<number>`, which Cloudflare aliases to
  `pr-<number>.openglad.pages.dev`; it does not replace the production
  `openglad.pages.dev` deployment.
- If multiplayer should work in previews, configure the Pages preview
  environment's `RELAY` service binding to the `openglad-relay` Worker.
  Without that preview binding the static game still loads, but `/relay`
  requests return `503`.

---

## Build Targets

The CMake build produces these targets:

| Target | Description |
|--------|-------------|
| `openglad` | The game binary |
| `openscen` | The level editor (compiled with `-DOPENSCEN`) |
| `og_unit_*` | Headless unit test binaries |
| `og_test_*` | SDL integration test binaries |
| `openglad_text` | Headless text client used by script-based CTest entries |

### Build Specific Targets

```bash
cmake --build --preset dev-debug --target openglad
cmake --build --preset ci-test -j"$(nproc)"
ctest --test-dir build/ci-test --parallel "$(nproc)" --output-on-failure --timeout 180
ctest --test-dir build/ci-test -L unit
ctest --test-dir build/ci-test -R og_test_walker
```

---

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_TESTING` | OFF | Build the test suite |
| `BUILD_EDITOR` | ON | Build the scenario editor (openscen) |
| `OPENGLAD_INSTALL_ASSETS` | ON | Install runtime asset directories |
| `ENABLE_COVERAGE` | OFF | Enable gcov/lcov coverage instrumentation |
| `ENABLE_SANITIZERS` | OFF | Enable AddressSanitizer + UBSan |
| `ENABLE_CLANG_TIDY` | OFF | Enable clang-tidy during compilation |
| `ENABLE_CPPCHECK` | OFF | Enable cppcheck during compilation |
| `OPENGLAD_USE_SYSTEM_DEPS` | ON | Prefer system/package-manager dependencies over FetchContent fallbacks |
| `OPENGLAD_REQUIRE_SYSTEM_DEPS` | OFF | Fail configure if package dependencies are unavailable |
| `OPENGLAD_FETCH_DEPS` | ON | Fetch missing upstream dependencies with CMake FetchContent |
| `OPENGLAD_FETCH_IXWEBSOCKET` | ON | Fetch pinned upstream IXWebSocket when no package is provided |

---

## Convenience Scripts

Shell scripts in `scripts/` provide quick-build shortcuts:

| Script | Description |
|--------|-------------|
| `scripts/build_native.sh` | Quick native release build |
| `scripts/build_test.sh` | Build all grouped test binaries |
| `scripts/build_web.sh` | Emscripten/WASM build |
| `scripts/build_coverage.sh` | Coverage report generation |

---

## Install (System-Wide)

```bash
cmake --preset dev-release
cmake --build --preset dev-release
sudo cmake --install build/dev-release
```

This installs:
- Binaries to `${CMAKE_INSTALL_BINDIR}` (typically `/usr/local/bin`)
- Libraries to `${CMAKE_INSTALL_LIBDIR}`
- Game assets to `${CMAKE_INSTALL_DATADIR}/openglad`
- CMake config files for downstream projects
