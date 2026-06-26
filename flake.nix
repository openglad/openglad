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

          cmakeFlags = [
            "-DBUILD_DEMO=OFF"
            "-DUSE_TLS=OFF"
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

      mkMicroPather =
        pkgs:
        pkgs.stdenv.mkDerivation {
          pname = "micropather";
          version = "2016-10-18-unstable";

          src = pkgs.fetchFromGitHub {
            owner = "leethomason";
            repo = "MicroPather";
            rev = "33a3b8403f1bc3937c9d364fe6c3977169bee3b5";
            hash = "sha256-xfvzizUV53jrDc6IxPX0qAcbmhDMuOsolJwfO1jTxos=";
          };

          dontConfigure = true;

          postPatch = ''
            substituteInPlace micropather.h \
              --replace-fail '#define GRINLIZ_NO_STL' '/* OpenGlad uses MicroPather STL mode. */'
          '';

          buildPhase = ''
            runHook preBuild
            $CXX -std=c++20 -O2 -fPIC -c micropather.cpp -o micropather.o
            ar rcs libmicropather.a micropather.o
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            install -Dm644 micropather.h "$out/include/micropather.h"
            install -Dm644 libmicropather.a "$out/lib/libmicropather.a"
            runHook postInstall
          '';

          meta = {
            description = "Small A* pathfinding library";
            homepage = "https://github.com/leethomason/MicroPather";
            license = pkgs.lib.licenses.zlib;
            platforms = systems;
          };
        };

      mkOpenGlad =
        pkgs:
        let
          ixwebsocketHead = mkIXWebSocket pkgs;
          lodepngHead = mkLodePNG pkgs;
          microPatherHead = mkMicroPather pkgs;
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
            SDL2
            SDL2_mixer
            ixwebsocketHead
            libyaml
            libzip
            lodepngHead
            libsndfile
            microPatherHead
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
          microPatherHead = mkMicroPather pkgs;
        in
        pkgs.mkShell {
          packages = with pkgs; [
            SDL2
            SDL2_mixer
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
            microPatherHead
            ninja
            ncurses
            physfs
            pkg-config
            python3
            zlib
          ];

          shellHook = ''
            export CMAKE_PREFIX_PATH="${lodepngHead}:${microPatherHead}''${CMAKE_PREFIX_PATH:+:}''${CMAKE_PREFIX_PATH:-}"
            echo "OpenGlad dev shell"
            echo "  Build:  cmake --preset dev-debug && cmake --build --preset dev-debug"
            echo "  Launch: ./build/dev-debug/openglad"
            echo "  Tests:  cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test"
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
          micropather = mkMicroPather pkgs;
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
