# External Dependencies

OpenGlad does not keep third-party source trees in the repository. CMake resolves
external libraries in this order:

1. Use package-manager/system dependencies when available.
2. If allowed, fetch the pinned upstream source with `FetchContent`.
3. If `OPENGLAD_REQUIRE_SYSTEM_DEPS=ON`, fail instead of fetching.

The Nix flake provides all native dependencies for local development and the Nix
package build configures with `OPENGLAD_REQUIRE_SYSTEM_DEPS=ON` and
`OPENGLAD_FETCH_DEPS=OFF`.

## Upstream Pins

| Dependency | Package target | FetchContent pin | Upstream |
|------------|----------------|------------------|----------|
| zlib | `ZLIB::ZLIB` | `v1.3.2` | https://github.com/madler/zlib |
| libzip | `libzip::zip` or `PkgConfig::OG_LIBZIP` | `v1.11.4` | https://github.com/nih-at/libzip |
| libyaml | `PkgConfig::OG_LIBYAML`, `yaml::yaml`, or `yaml` | `0.2.5` | https://github.com/yaml/libyaml |
| PhysFS | `PhysFS::PhysFS`, `PhysFS::physfs`, `unofficial::physfs::physfs`, `physfs`, or `PkgConfig::OG_PHYSFS` | `release-3.2.0` | https://github.com/icculus/physfs |
| lodepng | `lodepng` library plus `lodepng.h` | `ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a` | https://github.com/lvandeve/lodepng |
| MicroPather | `micropather` library plus `micropather.h` | `33a3b8403f1bc3937c9d364fe6c3977169bee3b5` | https://github.com/leethomason/MicroPather |
| IXWebSocket | `ixwebsocket::ixwebsocket` or `ixwebsocket` | `64fae7676bd8fe31f7cb4bcde7a6841892dad65e` | https://github.com/machinezone/IXWebSocket |

`src/resources/campaign_yaml.cpp` and `src/resources/gparser.cpp` use libyaml
directly so YAML mechanics stay behind resource-level APIs.

## Update Checklist

1. Update the dependency pin in `CMakeLists.txt`.
2. If the flake packages the dependency directly, update the matching derivation
   and source hash in `flake.nix`.
3. Configure once with packages only:
   `cmake -S . -B build/pkg -DOPENGLAD_REQUIRE_SYSTEM_DEPS=ON -DOPENGLAD_FETCH_DEPS=OFF`.
4. Configure once with fetches forced:
   `cmake -S . -B build/fetch -DOPENGLAD_USE_SYSTEM_DEPS=OFF`.
5. Build and run the relevant tests.
