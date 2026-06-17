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

      mkOpenGlad =
        pkgs:
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
            libsndfile
            ncurses
          ];

          cmakeFlags = [
            "-DBUILD_EDITOR=ON"
            "-DBUILD_TESTING=OFF"
            "-DOPENGLAD_INSTALL_ASSETS=ON"
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
            libsndfile
            lcov
            ninja
            ncurses
            pkg-config
            python3
          ];

          shellHook = ''
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
