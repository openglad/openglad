{ pkgs, self, mkIXWebSocket, mkLodePNG, mkLuaSource }:
let
  lib = pkgs.lib;
  python = pkgs.python3.withPackages (ps: [ ps.pillow ]);
  luaSource = mkLuaSource pkgs;
  tools = with pkgs; [
    bash binutils cmake coreutils dejavu_fonts findutils gawk git gnugrep gnused
    ninja pkg-config python ripgrep zstd
  ];
  dependencies = with pkgs; [
    sdl3 gtest libyaml libzip libsndfile ncurses openssl physfs zlib
  ];
  # Nix strictoverflow hardening injects -fwrapv, which suppresses signed
  # integer overflow diagnostics. Keep ordinary arithmetic semantics in every
  # producer, instrumented dependency, and matching consumer shell.
  mkLane = lane:
    let
      tsan = lane == "tsan";
      asan = lane == "asan";
      fuzz = lane == "fuzz";
      canary = lane == "canary";
      compileCache = asan || lane == "coverage";
      stdenv = if tsan || asan || fuzz then pkgs.clangStdenv else pkgs.stdenv;
      sanitizer = if tsan then "thread" else
        lib.optionalString fuzz "fuzzer-no-link," + "address,undefined,float-cast-overflow";
      # Minimal debug info keeps sanitizer file/line stacks without repeating
      # full C++ type/local-variable DWARF in every statically linked test.
      # Coverage reads counters and notes, never DWARF, and -g1 measured 11
      # to 17 percent of every coverage object (2026-11-12): that lane
      # compiles, links, packs and ships its tests without it.
      #
      # Optimisation. Coverage must stay at -O0: the measured function and
      # line set is whatever the compiler emits (CMakeLists.txt, "Coverage
      # instrumentation measures"). ASan+UBSan measures nothing of the kind,
      # and at -O0 its compute-bound tests spent 3.4 to 8 times what they do
      # at -O1 (2026-11-13, four groups, same machine: 69/31/39/13 s against
      # 14/9/5/2 s) for about 1.7 times the compile time. -O1 is the level the
      # sanitizer documentation recommends; sibling-call optimisation stays
      # off so every frame of a report is still on the stack. TSan and the
      # canary keep -O0: TSan runs one wall-clock-bound network group and the
      # canary recompiles after every mutation. The fuzz lane was not
      # measured and is unchanged.
      optimization = if asan then "-O1 -fno-optimize-sibling-calls" else "-O0";
      flags = optimization + lib.optionalString (lane != "coverage") " -g1"
        + lib.optionalString (tsan || asan || fuzz)
        " -fno-omit-frame-pointer -fsanitize=${sanitizer}"
        + lib.optionalString fuzz " -fno-sanitize-recover=all";
      # IXWebSocket owns worker threads: using an uninstrumented package in
      # TSan hides races across the transport boundary. Build it with the
      # lane's compiler and sanitizer, at the same pinned upstream revision.
      ix = (mkIXWebSocket (pkgs // { inherit stdenv; })).overrideAttrs (old: {
        pname = "ixwebsocket-ci-${lane}";
        dontStrip = true;
        hardeningDisable = [ "fortify" "fortify3" "strictoverflow" ];
        cmakeFlags = old.cmakeFlags ++ [ "-DCMAKE_BUILD_TYPE=Debug" ];
        preConfigure = ''
          cmakeFlagsArray+=(
            "-DCMAKE_C_FLAGS_DEBUG=${flags}"
            "-DCMAKE_CXX_FLAGS_DEBUG=${flags}"
          )
        '';
      });
      lodepng = (mkLodePNG (pkgs // { inherit stdenv; })).overrideAttrs (old: {
        pname = "lodepng-ci-${lane}";
        dontStrip = true;
        hardeningDisable = [ "fortify" "fortify3" "strictoverflow" ];
        buildPhase = builtins.replaceStrings [ "-O2" ] [ flags ] old.buildPhase;
      });
      preset = if canary then "ci-test" else "ci-${lane}";
      targets = if tsan then [ "og_unit_network" "og_sanitizer_diagnostics" ]
        else if fuzz then [ "fuzz_scenario" "fuzz_savefile" "stage_runtime_assets" "og_sanitizer_diagnostics" ]
        else if canary then [
        "og_test_parity" "parity_runner_smoke" "og_test_display_x11"
        "concept_mapgen" "imaginations_mapgen" "longseason_mapgen"
        "modes_mapgen" "tower_mapgen" "westlands_mapgen" "grid_migrate"
      ] else [];
      canonicalSource = "/tmp/openglad-ci-${lane}/source";
    in stdenv.mkDerivation {
      pname = "openglad-ci-${lane}";
      version = "2.${toString (self.revCount or 0)}";
      outputs = [ "out" ] ++ lib.optionals compileCache [ "sccache" ];
      src = self;
      ciCommitCount = toString (self.revCount or 0);
      ciGitHash = if self ? rev then builtins.substring 0 8 self.rev
        else if self ? dirtyRev then builtins.substring 0 8 self.dirtyRev
        else "nogit";
      ciSourceHasHistory = self ? rev || self ? dirtyRev;
      # Workflows override these with an explicit, content-addressed seed and
      # one compile-command shard.  Keeping them as derivation attributes makes
      # the sandbox hermetic: no compiler process reads a mutable host cache.
      sccacheSeed = "";
      ciCompileShards = "0";
      ciCompileShard = "-1";
      # "1": eight compile workers warmed the seed and the producer must find
      # every object in it. "0": no fan-out ran (scripts/ci/compile_fanout.py
      # judged the change small) and the producer compiles what is missing.
      ciCompileWarmed = "1";
      nativeBuildInputs = tools ++ lib.optionals compileCache [ pkgs.sccache ];
      buildInputs = dependencies ++ [ ix lodepng ];
      OG_KEYBOARD_FONT_DIR = "${pkgs.dejavu_fonts}/share/fonts/truetype";
      hardeningDisable = [ "fortify" "fortify3" "strictoverflow" ];
      # Test execution is deliberately NOT a derivation checkPhase. Every
      # consumer runs its shard afresh even when this output is substituted.
      doCheck = false;
      dontUseCmakeConfigure = true;
      dontUseNinjaBuild = true;
      dontUseNinjaInstall = true;
      dontStrip = true;
      dontPatchELF = true;
      configurePhase = ''
        runHook preConfigure
        if [ "$ciSourceHasHistory" = 1 ] && [ "$ciCommitCount" = 0 ]; then
          echo "Dirty Git builds need history metadata: use scripts/ci/nix_bundle.py build --lane ${lane}" >&2
          exit 1
        fi
        # Also safe on single-user Nix installations without mount isolation.
        # Never reuse another build's directory or a consumer's checkout alias.
        if [ -e ${canonicalSource} ] || [ -L ${canonicalSource} ]; then
          echo "Native CI source path already in use: ${canonicalSource}" >&2
          exit 1
        fi
        trap 'rm -rf ${canonicalSource}' EXIT
        mkdir -p ${canonicalSource}
        cp -a . ${canonicalSource}/
        chmod -R u+w ${canonicalSource}
        cd ${canonicalSource}
        # Build-time inventory lints require git ls-files. This index describes
        # exactly the flake source; consumers use their actual checkout and
        # history for heritage/companion gates, never this temporary index.
        git init --quiet
        git add --all --force
        # mkDerivation gives every shard a distinct output hash and therefore
        # distinct wrapper-only -frandom-seed and self-rpath flags. They do not
        # describe source semantics, but letting them vary makes independently
        # warmed objects differ from the authoritative producer. Canonicalize
        # both before CMake captures the environment in its compiler launcher.
        read -r -a nixCompileFlags <<< "$NIX_CFLAGS_COMPILE"
        for index in "''${!nixCompileFlags[@]}"; do
          if [[ "''${nixCompileFlags[$index]}" == -frandom-seed=* ]]; then
            nixCompileFlags[$index]=-frandom-seed=openglad-ci-${lane}
          fi
        done
        export NIX_CFLAGS_COMPILE="''${nixCompileFlags[*]}"
        read -r -a nixLinkerFlags <<< "$NIX_LDFLAGS"
        stableLinkerFlags=()
        index=0
        while [ "$index" -lt "''${#nixLinkerFlags[@]}" ]; do
          if [[ "''${nixLinkerFlags[$index]}" == -rpath && \
                "''${nixLinkerFlags[$((index + 1))]:-}" == "$out/lib" ]]; then
            index=$((index + 2))
            continue
          fi
          stableLinkerFlags+=("''${nixLinkerFlags[$index]}")
          index=$((index + 1))
        done
        export NIX_LDFLAGS="''${stableLinkerFlags[*]}"
        # CMake invokes build scripts through the discovered bash/Python.
        # Rewriting shebangs would invalidate checked generator source hashes.
        # Nix wrappers learn dependency include/link paths through these
        # flags. CMake then treats those paths as implicit and may omit them
        # after regeneration. Upstream launchers retain the exact producer
        # options for incremental builds in a lean consumer shell.
        environmentLauncher="$(python3 scripts/ci/nix_bundle.py compiler-launcher \
          --env ${pkgs.coreutils}/bin/env)"
        compilerLauncher="$environmentLauncher"
        ${lib.optionalString compileCache ''
          export SCCACHE_DIR="$NIX_BUILD_TOP/openglad-sccache"
          # Two lanes plus the exact-NAR caches must coexist within GitHub's
          # repository cache budget. Entries are compressed; cap the live
          # sandbox cache at 2 GiB so a seed plus one complete cold build
          # never evicts an entry the same build still needs. The published
          # seed is capped separately below.
          export SCCACHE_CACHE_SIZE=2G
          mkdir -p "$SCCACHE_DIR"
          if [ -n "$sccacheSeed" ]; then
            if [ ! -d "$sccacheSeed/cache" ]; then
              echo "sccache seed has no cache directory: $sccacheSeed" >&2
              exit 1
            fi
            cp -a --reflink=auto "$sccacheSeed/cache/." "$SCCACHE_DIR/"
            chmod -R u+w "$SCCACHE_DIR"
          fi
          # Every object keeps its target path in its cache identity.
          # Coverage objects need it: they embed their -o-derived .gcda
          # destination. ASan objects did without it for the sake of
          # cross-target reuse, which saved 94 of 1,314 compilations and
          # made a hundred test targets ask for one cache entry at once;
          # sccache then failed to extract some of those reads, and after
          # a cold fan-out the producer had 27 such repairs against a
          # budget of 26 (GitHub run 43544998292, 2026-11-14). With one
          # entry per object the coverage producer has never repaired one.
          cacheKeyArguments=(
            --cache-key-shell ${pkgs.bash}/bin/bash
            --cache-key-wrapper ${canonicalSource}/scripts/ci/sccache_output_key.sh
          )
          compilerLauncher="$(python3 scripts/ci/nix_bundle.py compiler-launcher \
            --env ${pkgs.coreutils}/bin/env \
            --cache ${pkgs.sccache}/bin/sccache --cache-dir "$SCCACHE_DIR" \
            "''${cacheKeyArguments[@]}")"
        ''}
        cmake --preset ${preset} \
          -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
          -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_EXE_LINKER_FLAGS='-Wl,-z,pack-relative-relocs -Wl,--compress-debug-sections=zlib' \
          -DCMAKE_C_COMPILER_LAUNCHER="$compilerLauncher" \
          -DCMAKE_CXX_COMPILER_LAUNCHER="$compilerLauncher" \
          -DCMAKE_C_LINKER_LAUNCHER="$environmentLauncher" \
          -DCMAKE_CXX_LINKER_LAUNCHER="$environmentLauncher" \
          -DCMAKE_C_FLAGS_DEBUG='${flags}' \
          -DCMAKE_CXX_FLAGS_DEBUG='${flags}' \
          ${lib.optionalString canary "-DOPENGLAD_X11_DISPLAY_TESTS=ON"} \
          -DVALIDATE_SERIALIZATION=ON \
          -DOPENGLAD_REQUIRE_SYSTEM_DEPS=ON \
          -DOPENGLAD_FETCH_DEPS=OFF \
          -DOPENGLAD_FETCH_IXWEBSOCKET=OFF \
          -DFETCHCONTENT_SOURCE_DIR_LUA=${luaSource} \
          -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
          -DOPENGLAD_COMMIT_COUNT="$ciCommitCount" \
          -DOPENGLAD_GIT_HASH="$ciGitHash"
        runHook postConfigure
      '';
      buildPhase = ''
        runHook preBuild
        ${lib.optionalString compileCache "sccache --zero-stats"}
        if [ "$ciCompileShards" -gt 0 ]; then
          python3 scripts/ci/compile_cache.py warm \
            --build build/${preset} --lane ${lane} --revision '${self.rev or "dirty"}' \
            --shards "$ciCompileShards" --shard "$ciCompileShard" \
            --jobs "$NIX_BUILD_CORES" --out build/compile-cache-shard.json \
            --cache-program ${pkgs.sccache}/bin/sccache
        else
          cmake --build --preset ${preset} --parallel "$NIX_BUILD_CORES" \
            ${lib.optionalString (targets != []) "--target ${lib.concatStringsSep " " targets}"} -- -k 0
          ${lib.optionalString compileCache ''
            if [ "$ciCompileWarmed" = 1 ]; then
              python3 scripts/ci/compile_cache.py verify-producer \
                --cache-program ${pkgs.sccache}/bin/sccache --max-fallbacks 32
            else
              python3 scripts/ci/compile_cache.py verify-producer \
                --cache-program ${pkgs.sccache}/bin/sccache --unwarmed
            fi
          ''}
        fi
        ${lib.optionalString compileCache ''
          sccache --show-stats
          sccache --stop-server
        ''}
        # Observe the completed build graph before pack removes objects and
        # archives. This is a disk snapshot, not an exact peak measurement.
        du --bytes --summarize build/${preset}
        runHook postBuild
      '';
      installPhase = ''
        runHook preInstall
        if [ "$ciCompileShards" -gt 0 ]; then
          mkdir -p "$out"
          cp build/compile-cache-shard.json "$out/"
          publishSeed=()
          if [ -n "$sccacheSeed" ]; then
            publishSeed=(--seed "$sccacheSeed")
          fi
          ${lib.optionalString compileCache ''
            python3 scripts/ci/compile_cache.py publish \
              --cache "$SCCACHE_DIR" "''${publishSeed[@]}" --output "$sccache" \
              --lane ${lane} --revision '${self.rev or "dirty"}' \
              --shard-manifest build/compile-cache-shard.json
          ''}
        else
          python3 scripts/ci/nix_bundle.py pack \
            --source ${canonicalSource} --build build/${preset} \
            --output "$out" --lane ${lane} --revision '${self.rev or "dirty"}'
          ${lib.optionalString compileCache ''
            # Every compile worker and the producer restore this seed, copy
            # it into the store and again into the sandbox, so its size is
            # paid nine times per run. A complete build publishes about
            # 0.4 GiB; 1 GiB keeps the entries this build read (sccache
            # refreshes their timestamps, and publication retains newest
            # first) plus more than one older generation.
            python3 scripts/ci/compile_cache.py publish \
              --cache "$SCCACHE_DIR" --output "$sccache" \
              --lane ${lane} --revision '${self.rev or "dirty"}' \
              --max-bytes 1073741824
          ''}
        fi
        runHook postInstall
      '';
      passthru = { inherit preset canonicalSource; };
    };
  mkShellWith = stdenv: packages: pkgs.mkShell.override { inherit stdenv; } {
    inherit packages;
    hardeningDisable = [ "fortify" "fortify3" "strictoverflow" ];
    shellHook = ''
      export CMAKE_BUILD_PARALLEL_LEVEL="''${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
      export OG_KEYBOARD_FONT_DIR="${pkgs.dejavu_fonts}/share/fonts/truetype"
    '';
  };
  # Bundle-consuming lanes pass no extras: their instrumented IXWebSocket/
  # LodePNG closures and cached CMake paths are already retained.
  mkShell = stdenv: extraPackages: mkShellWith stdenv (
    tools ++ dependencies ++ extraPackages ++ [
      pkgs.emscripten pkgs.gcovr pkgs.lcov pkgs.lua-language-server
      pkgs.llvmPackages.llvm
      pkgs.xvfb-run pkgs.xvfb pkgs.xrandr pkgs.openbox pkgs.xprop pkgs.xdpyinfo
    ]);
in {
  ci-coverage = mkLane "coverage";
  ci-asan = mkLane "asan";
  ci-tsan = mkLane "tsan";
  ci-canary = mkLane "canary";
  ci-fuzz = mkLane "fuzz";
  # The native incremental shell configures from source, so its dependency
  # set must include the same patched custom packages as the native producer.
  # Sanitizer shells consume a bundle whose instrumented closures are already
  # retained and must not pick up separate uninstrumented custom packages.
  nativeShell = mkShell pkgs.stdenv [ (mkIXWebSocket pkgs) (mkLodePNG pkgs) ];
  # Every coverage job enters its shell before doing anything, and the
  # stages run in series, so each needless store path is fetched once per
  # stage on the critical path. The coverage jobs build the lane derivation, run
  # CTest, gcovr and the checkout lints: they need the producer's own tools
  # and libraries plus gcovr and LuaLS, and none of emscripten, LLVM or the
  # X11 display stack that the general native shell carries for other lanes.
  coverageShell = mkShellWith pkgs.stdenv (
    tools ++ dependencies ++ [ pkgs.gcovr pkgs.lua-language-server ]);
  # The same reasoning for the other lanes (2026-11-13: the general shell
  # was a 1.7 GiB download that also BUILT the plain IXWebSocket and LodePNG
  # packages in every job, none of which a bundle consumer links).
  #
  # ASan, TSan and the fuzzers build their lane with Clang and run its
  # tests: the lane's own tools and libraries plus LLVM, whose symbolizer
  # turns a sanitizer report into file and line.
  sanitizerShell = mkShellWith pkgs.clangStdenv (
    tools ++ dependencies ++ [ pkgs.llvmPackages.llvm ]);
  # The canary recompiles the restored tree after every C++ mutation with
  # the lane's GCC, and the X11 display lane drives real X screens.
  canaryShell = mkShellWith pkgs.stdenv (
    tools ++ dependencies ++ [
      pkgs.xvfb-run pkgs.xvfb pkgs.xrandr pkgs.openbox pkgs.xprop pkgs.xdpyinfo
    ]);
  # The completeness jobs read plans and results and compile nothing.
  verifyShell = pkgs.mkShellNoCC { packages = [ pkgs.python3 ]; };
}
