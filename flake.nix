{
  description = "OpenGlad native build and development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = nixpkgs.lib.genAttrs systems;

      mkPkgs = system: import nixpkgs { inherit system; };

      mkIXWebSocket =
        pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "ixwebsocket";
          version = "11.4.6-unstable-2026-06-26";

          src = pkgs.fetchFromGitHub {
            owner = "machinezone";
            repo = "IXWebSocket";
            rev = "64fae7676bd8fe31f7cb4bcde7a6841892dad65e";
            hash = "sha256-2QWIpLVIs2vGuMEhewDyihYdDQBz7SsOtfZ6pE67j2Q=";
          };

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
          ];

          buildInputs = with pkgs; [
            openssl
          ];

          # TLS is required: the default multiplayer relay lives on Cloudflare
          # (https:// room create + wss:// room sockets).
          cmakeFlags = [
            "-DBUILD_DEMO=OFF"
            "-DUSE_TLS=ON"
            "-DUSE_OPEN_SSL=ON"
            "-DUSE_ZLIB=OFF"
            "-DIXWEBSOCKET_INSTALL=ON"
          ];

          meta = {
            description = "C++ WebSocket client/server library";
            homepage = "https://github.com/machinezone/IXWebSocket";
            license = pkgs.lib.licenses.bsd3;
            platforms = systems;
          };
        };

      mkLodePNG =
        pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "lodepng";
          version = "2026-06-26-unstable";

          src = pkgs.fetchFromGitHub {
            owner = "lvandeve";
            repo = "lodepng";
            rev = "ed6fe5825c6a4fbb7f58ab35a4231c7543cd452a";
            hash = "sha256-tf6XGwiartgREoEBA/jTAZpIgMg378Ds2aal3nJSA0A=";
          };

          dontConfigure = true;

          buildPhase = ''
            runHook preBuild
            $CXX -std=c++20 -O2 -fPIC -c lodepng.cpp -o lodepng.o
            ar rcs liblodepng.a lodepng.o
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            install -Dm644 lodepng.h "$out/include/lodepng.h"
            install -Dm644 liblodepng.a "$out/lib/liblodepng.a"
            runHook postInstall
          '';

          meta = {
            description = "PNG encoder and decoder in C and C++";
            homepage = "https://github.com/lvandeve/lodepng";
            license = pkgs.lib.licenses.zlib;
            platforms = systems;
          };
        };

      mkOpenGlad =
        pkgs:
        let
          ixwebsocketHead = mkIXWebSocket pkgs;
          lodepngHead = mkLodePNG pkgs;
        in
        pkgs.stdenv.mkDerivation {
          pname = "openglad";
          version = "1.1.1";

          src = self;

          nativeBuildInputs = with pkgs; [
            bash
            cmake
            findutils
            gnugrep
            gnused
            makeWrapper
            ninja
            pkg-config
          ];

          buildInputs = with pkgs; [
            sdl3
            ixwebsocketHead
            libyaml
            libzip
            lodepngHead
            libsndfile
            ncurses
            physfs
            zlib
          ];

          cmakeFlags = [
            "-DBUILD_EDITOR=ON"
            "-DBUILD_TESTING=OFF"
            "-DOPENGLAD_INSTALL_ASSETS=ON"
            "-DOPENGLAD_REQUIRE_SYSTEM_DEPS=ON"
            "-DOPENGLAD_FETCH_DEPS=OFF"
            "-DOPENGLAD_FETCH_IXWEBSOCKET=OFF"
          ];

          postPatch = ''
            patchShebangs scripts
          '';

          postInstall = ''
            asset_dir="$out/share/openglad"
            mkdir -p "$asset_dir"

            for exe in openglad openscen openglad_server openglad_curses openglad_text openglad_demo; do
              if [ -x "$out/bin/$exe" ]; then
                mv "$out/bin/$exe" "$asset_dir/$exe"
              elif [ -x "$exe" ]; then
                install -Dm755 "$exe" "$asset_dir/$exe"
              fi

              if [ -x "$asset_dir/$exe" ]; then
                makeWrapper "$asset_dir/$exe" "$out/bin/$exe" \
                  --chdir "$asset_dir"
              fi
            done
          '';

          doInstallCheck = true;
          installCheckPhase = ''
            runHook preInstallCheck

            export HOME="$TMPDIR/home"
            export OPENGLAD_CONFIG_DIR="$TMPDIR/openglad-config"
            export SDL_AUDIODRIVER=dummy
            export SDL_VIDEODRIVER=dummy
            mkdir -p "$HOME" "$OPENGLAD_CONFIG_DIR"

            "$out/bin/openglad" -h >/dev/null
            "$out/bin/openglad_text" --help >/dev/null
            "$out/bin/openglad_server" --help >/dev/null

            runHook postInstallCheck
          '';

          meta = {
            description = "Open-source port of the Gladiator top-view gauntlet RPG";
            homepage = "https://github.com/openglad/openglad";
            license = pkgs.lib.licenses.gpl2Plus;
            mainProgram = "openglad";
            platforms = systems;
          };
        };

      mkDevShell =
        pkgs:
        let
          ixwebsocketHead = mkIXWebSocket pkgs;
          lodepngHead = mkLodePNG pkgs;
        in
        pkgs.mkShell {
          packages = with pkgs; [
            sdl3
            bash
            binutils
            clang
            cmake
            coreutils
            findutils
            gawk
            gdb
            git
            gnugrep
            gnused
            gtest
            ixwebsocketHead
            libyaml
            libzip
            libsndfile
            lodepngHead
            lcov
            # Pack-Lua editor/CI tooling: reads the generated og.* stubs
            # (docs/modding/og-api.d.lua) through the repo-root .luarc.json.
            # scripts/check_luals.py and the check_luals CMake target gate
            # coverage_report: zero diagnostics under packs/ and
            # docs/modding/.
            lua-language-server
            ninja
            ncurses
            physfs
            pkg-config
            python3
            # Local coverage runs use the same gcovr report input as CI.
            gcovr
            zlib
            # Web target: emcmake/emcc for the web-emscripten preset, so the
            # wasm build is reachable from the dev shell instead of needing a
            # separately installed emsdk.
            emscripten
            # Headless capture + media: xvfb-run gives an X server for the SDL
            # client when the dummy driver is not enough, and ffmpeg is the
            # encoder scripts/media/capture_showcase.sh uses to turn captured
            # frames into the shipped GIFs and PNGs.
            xvfb-run
            ffmpeg
            # scripts/media/make_lua_ownership_overlays.py draws the Lua/engine
            # ownership rectangles over the captured stills; it shells out to
            # `magick` for the compositing and needs a real TrueType file for
            # the badge digits and the legend (no DejaVu ships system-wide
            # here, and the script takes a font path, not a fontconfig name).
            imagemagick
            dejavu_fonts
            # Browser for the Playwright wasm e2e suite. Playwright refuses to
            # install its own build on some hosts ("does not support chromium
            # on ubuntu26.04-x64"), so playwright.config.js honours
            # OG_CHROMIUM_PATH and the shellHook points it here.
            chromium
            nodejs
          ];

          shellHook = ''
            export CMAKE_PREFIX_PATH="${lodepngHead}''${CMAKE_PREFIX_PATH:+:}''${CMAKE_PREFIX_PATH:-}"
            # emcc writes its cache next to the (read-only) store path by
            # default; point it somewhere writable or the first web build dies.
            export EM_CACHE="''${EM_CACHE:-$PWD/build/.emscripten-cache}"
            export OG_CHROMIUM_PATH="''${OG_CHROMIUM_PATH:-${pkgs.chromium}/bin/chromium}"
            echo "OpenGlad dev shell"
            echo "  Build:  cmake --preset dev-debug && cmake --build --preset dev-debug"
            echo "  Launch: ./build/dev-debug/openglad"
            echo "  Tests:  cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test"
            echo "  Web:    ./scripts/build_web.sh   (emcc $(emcc --version 2>/dev/null | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' || echo '?'))"
            echo "  LuaLS:  python3 scripts/check_luals.py   (enforced: gates coverage_report)"
          '';
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = mkPkgs system;
        in
        {
          default = mkOpenGlad pkgs;
          ixwebsocket = mkIXWebSocket pkgs;
          lodepng = mkLodePNG pkgs;
          openglad = mkOpenGlad pkgs;
        }
      );

      apps = forAllSystems (
        system:
        {
          default = {
            type = "app";
            program = "${self.packages.${system}.default}/bin/openglad";
          };
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = mkPkgs system;
        in
        {
          default = mkDevShell pkgs;
        }
      );
    };
}
