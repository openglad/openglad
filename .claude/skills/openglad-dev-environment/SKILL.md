---
name: openglad-dev-environment
description: Local development environment for OpenGlad — the nix flake, tmpfs build-dir placement and cleanup, local-vs-CI configuration divergence, accepted local test failures and standing rulings, git hygiene traps, agent-orchestration traps, and setting up a fresh machine. Use whenever you are about to configure or build any preset, a local test result disagrees with CI, a tool seems missing, a full local ctest run has unexplained reds, worktrees or subagents produce surprising state, or development moves to a new machine.
---

# OpenGlad dev environment

AGENTS.md is the base contract: everything builds inside `nix develop`,
tools come from `flake.nix` (add missing ones there — never hand-roll a
utility a package provides, never apt/snap-install, never source
~/emsdk), presets only, no in-source configure. This file covers what
AGENTS.md doesn't: divergence, accepted failures, and traps.

## Builds live in tmpfs

`/tmp` is tmpfs on this machine (~61G). Build trees go in
`/tmp/openglad-builds/<checkout>/<preset>`, where `<checkout>` is the
basename of the source tree — so the main repo and each worktree get
their own subfolder and never collide. Presets hardcode `binaryDir` to
`${sourceDir}/build/<preset>`, so the mechanism is a per-preset
symlink, not a `-B` override (`cmake --build --preset` would ignore
one). Before configuring ANY preset, run this idempotent snippet:

```bash
preset=ci-test                      # whichever preset you're building
tgt="/tmp/openglad-builds/$(basename "$PWD")/$preset"
mkdir -p "$tgt" build
[ -L "build/$preset" ] || rm -rf "build/$preset"
ln -sfn "$tgt" "build/$preset"
```

- Run it EVERY time before `cmake --preset ...`: tmpfs is wiped on
  reboot, leaving `build/<preset>` a dangling symlink that makes
  configure fail; the `mkdir -p` heals it.
- Symlink per-preset only — never the whole `build/` dir. `build/media`
  (capture tooling output) and other non-preset contents stay real.
- RAM is shared and each preset tree runs to a few GB (more when
  FetchContent builds SDL3), so cleanup is part of the workflow, not
  optional.

**Cleanup.** When a build tree is no longer needed (task done, branch
merged, preset abandoned), `rm -rf` its tmpfs dir and the symlink. And
whenever you're about to build — or otherwise notice the folder —
sweep anything over a day old plus the dangling symlinks it leaves:

```bash
find /tmp/openglad-builds -mindepth 2 -maxdepth 2 \
  ! -newermt '1 day ago' -exec rm -rf {} + 2>/dev/null
find build -maxdepth 1 -xtype l -delete 2>/dev/null
```

(Preset-dir mtime is a good staleness proxy: ninja rewrites
`.ninja_log` at the build root on every build.) The parity companion
worktree's build follows the same pattern.

## Local green is not CI green (four known divergences)

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
- Delegation split (maintainer budget rule, also in AGENTS.md): the
  expensive tier only for design, review, and irreducibly complex
  implementation; the cheaper tier for recon, mechanical work, gates,
  audits, media, PR mechanics. Surface architecture alternatives to the
  maintainer BEFORE building when a pivot would trash the work.

## Fresh-machine setup (beyond `git clone` + nix)

- `nix develop` provides the toolchain (GCC, cmake/ninja, SDL3, emcc,
  ffmpeg, imagemagick). Note SDL2 in the shell is sdl2-compat over SDL3,
  not real SDL2 — CI uses real libsdl2 where it needs SDL2.
- **gcovr is NOT in the flake** (CI pip-installs it). Local coverage
  runs need it on PATH; the clean fix is adding it to the flake dev
  shell.
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
