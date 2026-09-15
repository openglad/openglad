---
name: openglad-dev-environment
description: Local development environment for OpenGlad — the nix flake, local-vs-CI configuration divergence, accepted local test failures and standing rulings, git hygiene traps, agent-orchestration traps, and setting up a fresh machine. Use whenever a local test result disagrees with CI, a tool seems missing, a full local ctest run has unexplained reds, worktrees or subagents produce surprising state, or development moves to a new machine.
---

# OpenGlad dev environment

AGENTS.md is the base contract: everything builds inside `nix develop`,
tools come from `flake.nix` (add missing ones there — never hand-roll a
utility a package provides, never apt/snap-install, never source
~/emsdk), presets only, no in-source configure. This file covers what
AGENTS.md doesn't: divergence, accepted failures, and traps.

## Local green is not CI green (five known divergences)

1. **VALIDATE_SERIALIZATION=ON** is set in CI's test/drift/asan/tsan
   lanes and the ci-asan cache, OFF in a default local ci-test build. It
   round-trips every typed in-process message requiring equality —
   asymmetric (de)serialization passes locally, fails CI
   deterministically. Reproduce with the flag or the ci-asan preset.
2. **The TSan lane compiles with clang -Werror** (incl.
   tautological-compare); ci-test is GCC and misses that class. Sweep
   new wire/guard code with clang syntax-only.
3. **The Campaign Regeneration Drift job builds all standalone tools
   fresh** (the mapgens + grid_migrate compile headless TUs directly);
   local ctest never links them. After moving/splitting any
   headless-shared TU, build the tools locally.
4. **CI coverage accumulates .gcda across `--repeat until-pass:3`**, so
   local single-run numbers undercount CI. Judge coverage work by local
   before/after DELTA, never absolutes (see openglad-test-integrity).
5. **The nix cc-wrapper injects -O2 into every local compile.** Its
   hardening set (`NIX_HARDENING_ENABLE=... fortify3 ...`) prepends the
   optimization flag; `NIX_DEBUG=1 g++ ...` prints

       extra flags before to .../gcc-15.2.0/bin/g++:
         -fPIC
         -fstack-clash-protection
         -O2
         -U_FORTIFY_SOURCE
         ...

   so inside `nix develop` the ci-asan preset (CMAKE_BUILD_TYPE=Debug,
   inherited from dev-debug) builds at `-O2 -g` plus sanitizers, while
   CI's Ubuntu GCC does not. Consequence: a TU
   that includes `<regex>` fails locally under GCC 15 + -O2 + ASan/UBSan
   with `bits/std_function.h:407:42: error: ... may be used uninitialized
   [-Werror=maybe-uninitialized]` — a libstdc++ false positive, not our
   bug (the same TU without sanitizers compiles clean, and forcing -O0
   only trades it for glibc's `_FORTIFY_SOURCE requires compiling with
   optimization`). `<regex>` is therefore banned repo-wide and gated by
   scripts/check_no_std_regex.sh; write predicates or exact-string
   oracles instead (tests/unit/test_version.cpp,
   tests/curses/test_curses_mount_guard.cpp are the shapes).

## Accepted local failures and standing rulings

- `emscripten_build_test` fails on machines without `$EMSDK` set — this
  is ACCEPTED, and the maintainer has explicitly ordered the build
  script NOT to be patched around it ("emscripten fuckery, not
  necessary"; a fix was authored and reverted twice). Exclude it from
  local full-suite runs (`ctest --preset ci-test -E
  emscripten_build_test`); CI's wasm lanes are unaffected. It reports as
  a Timeout because the broken build churns past the cap. Do not
  reintroduce a script fix; do not report it as a regression.
- The og_unit_sim websocket loopback test and the injector-heavy SDL
  suites (menu_ui, picker, view, basecamp) are wall-clock flaky under
  machine load — check `uptime` before trusting a timeout, adjudicate
  with CI's own `--repeat until-pass:3 --timeout 420` isolated, and get
  fast signal from headless og_unit_* first.
- Standing rulings like the emscripten one are invisible to subagents.
  Any agent that may run the full suite — fixers and gate-runners above
  all — gets the ruling IN ITS PROMPT, or it will "fix" the accepted
  failure (this happened twice in one session). Before pushing a branch
  containing subagent commits, diff the commit list against known
  rulings.

## cfg clobber hazard

A headless test binary run by hand with no campaign mounted can error
out mid-session and rewrite the TRACKED `cfg/openglad.yaml` via the cwd
fallback — the next og_test_io run then fails a settings test with a
bizarre wrong-value diff. After any manually-run failing headless
binary: `git status cfg/`, restore if dirty. Tests driving full
text-client sessions must mount a campaign up front
(`restore_default_campaigns()` + `mount_campaign_package_with_error`).

## Git hygiene traps (each cost real time)

- `git stash` on an already-committed tree saves nothing, and a later
  `stash pop` resurrects a FOREIGN old stash entry into a conflicted
  merge. To baseline-check committed work, use a worktree, never stash.
- `git checkout <file>` to strip a temp harness WIPES uncommitted
  sibling edits in that file — and a gate run without rebuilding masks
  the loss via stale binaries. Strip harness code by text marker; always
  rebuild before gating.
- `git add -A` sweeps stray user files (recordings, notes at repo root)
  into commits. Audit `git status` between gate-run and commit; the
  gate audit is stale the moment new files appear.
- Coverage builds: incremental ci-coverage rebuilds leave stale `.gcda`
  ("overwriting ... different checksum") — delete them after building,
  before running tests.

## Agent-orchestration traps

- Worktree subagents can spawn at the branch's PARENT (or master), not
  the launching HEAD — this recurred across many waves. Always state
  the intended base SHA in the prompt and have the agent verify
  (`git reset --hard <sha>` + a marker file) before working.
- Fresh worktrees lack the gitignored `temp/scen/*.fss` parity fixtures
  (copy from the main tree) and inherit no built assets.
- High concurrent agent counts trip server-side rate limits (~9
  concurrent on a shared machine); run waves of ~3 with a barrier.
  Agents killed mid-edit leave partial trees — `git restore` and rerun
  rather than debugging half-applied edits.
- **Never reply to a running workflow agent with SendMessage.** The
  address in its `<agent-message from=...>` resumes a SECOND copy of
  its transcript alongside the original; both then edit the same
  worktree and clobber each other (PR #262: duplicate L1-L6 tests and
  reverted Lua). Answer ownership questions by amending the next
  agent's prompt or by letting the agent take its stated default.
- **Long builds/test runs must be detached.** The harness kills a
  foreground or background Bash command at its 10-minute cap, and a
  loaded shared box (load 100+) pushes a full ci-test build past it.
  Run `setsid nohup <script> &` writing to a log and watch the log's
  terminal markers with Monitor (or an `until`-loop) instead of
  re-launching the build every ten minutes.
- **Never fan out N concurrent full builds.** Wave-2 of PR #262 ran four
  worktree builds in parallel while `/tmp` was tmpfs and OOMed the
  server; even on disk, cap each agent's `CMAKE_BUILD_PARALLEL_LEVEL`
  so the sum stays near the core count — and on a container guest read
  the cgroup first, see below.
- Delegation split (maintainer budget rule, also in AGENTS.md): the
  expensive tier only for design, review, and irreducibly complex
  implementation; the cheaper tier for recon, mechanical work, gates,
  audits, media, PR mechanics. Surface architecture alternatives to the
  maintainer BEFORE building when a pivot would trash the work.

## This box is a container guest: check the cgroup, not nproc

Each item is a command to run, with today's reading on this machine as
the example. Re-run them on a new box; never carry the numbers over.

- Parallelism follows the memory cgroup, not the core count.
  `cat /sys/fs/cgroup/memory.max` → `8589934592` (8 GiB);
  `cat /sys/fs/cgroup/cpu.max` → `1200000 100000` (12 CPUs); `nproc` →
  `12`. Overcommit the 8 GiB — -j$(nproc), or a second build alongside
  the first — and cc1plus is OOM-killed on the heavy test TUs:
  `g++: fatal error: Killed signal terminated program cc1plus`. Use
  `-j3` and run one build at a time.
- Disk: `df -h . /tmp` from the repo root →

      /dev/mapper/ubuntu--vg-ubuntu--lv  7.3T  3.8T  3.1T  55% /home/yans/code/openglad
      overlay                            512G  485G   28G  95% /

  The repo is a bind mount of the host LV (the one entry for it in
  `/proc/self/mountinfo`); `/` and `/tmp` — hence the scratchpad — are
  the container overlay, at 95 % today. df's answer for a path outside
  the repo mount is not trustworthy here: it names the LV for sibling
  worktrees under /home/yans/code/, and a 500 MB probe file written into
  one of them did not move `df /`, while the same file in the scratchpad
  did. Before parking a multi-GB build tree outside the repo, write a
  probe and re-read `df /`.
- Run og_unit_*/og_test_* binaries from the REPO ROOT: ctest gives them
  `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}` (cmake/OpenGladTests.cmake), so
  they resolve assets relative to it. `cd build/ci-test && ./og_unit_data`
  fails 8 ExampleClassPack tests that pass from the root
  (`./build/ci-test/og_unit_data` → 534/534) — phantom reds that look
  like a real regression. The exceptions are the three standalone
  lifecycle tests (og_test_sdl_video_lifecycle,
  og_test_sdl_renderer_fallback, og_test_runtime_bootstrap_lifecycle),
  which ctest runs on `${CMAKE_BINARY_DIR}`, and og_test_curses, which
  finds its sources through the compile definition
  OG_CURSES_TESTS_SOURCE_DIR.
- Keep-going is ninja's flag and belongs after `--`:
  `cmake --build --preset ci-test -j3 -- -k 0`. Spelled before the `--`,
  cmake answers `Unknown argument -k` and builds nothing; left out
  entirely, the first -Werror TU hides the rest of the failure set.
- `pkill -f <pattern>` matches the agent's own `bash -c` argv and kills
  the session shell: the pattern sits in the command line doing the
  killing (`pgrep -af 'og_test_zzz_probe'` listed this shell
  and nothing else). Use the bracket idiom — `pkill -f '[o]g_test_foo'`,
  which matched nothing here — or `pgrep -x` and kill by PID.
- gh here is 2.46.0, which has no `--json` on `pr checks`:
  `gh pr checks 292 --json state` answers `unknown flag: --json`. Use
  plain `gh pr checks <n>`, or `gh run list --json` / `gh run view
  --json`.

## Fresh-machine setup (beyond `git clone` + nix)

- `nix develop` provides the toolchain (GCC, cmake/ninja, SDL3, emcc,
  ffmpeg, imagemagick). Note SDL2 in the shell is sdl2-compat over SDL3,
  not real SDL2 — CI uses real libsdl2 where it needs SDL2.
- **gcovr is in the nix dev shell**, so local coverage runs need no separate
  install. CI's Ubuntu runner still installs gcovr with pip before configuring
  the coverage preset.
- Parity companion: `git worktree add ../openglad-master
  parity-companion` and build `parity_dump_master` there (SDL2-era —
  see openglad-parity for the pkg-config recipe).
- `temp/scen/*.fss` fixtures regenerate via a full ctest run
  (og_test_level writes them).
- Relay/Pages deploys read credentials from the gitignored `./env`
  (never printed — see AGENTS.md Secrets).
- Playwright browser installs may need a platform override on very new
  Ubuntu (`PLAYWRIGHT_HOST_PLATFORM_OVERRIDE=ubuntu24.04-x64`) or the
  nixpkgs `playwright-driver.browsers` path.
