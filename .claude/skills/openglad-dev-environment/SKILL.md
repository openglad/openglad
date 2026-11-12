---
name: openglad-dev-environment
description: Local development environment for OpenGlad — the nix flake, local-vs-CI configuration divergence, first-attempt test results and delegated lanes, git hygiene traps, agent-orchestration traps, and setting up a fresh machine. Use whenever a local test result disagrees with CI, a tool seems missing, a full local ctest run has unexplained reds, worktrees or subagents produce surprising state, or development moves to a new machine.
---

# OpenGlad dev environment

AGENTS.md is the base contract: everything builds inside `nix develop`,
tools come from `flake.nix` (add missing ones there — never hand-roll a
utility a package provides, never apt/snap-install, never source
~/emsdk), presets only, no in-source configure. This file covers what
AGENTS.md doesn't: configuration differences, fixture isolation, and traps.

## Matching the native CI configuration

The Linux native lanes produce one authoritative Nix bundle for the exact
checkout revision, then run consumers against that bundle. ASan and coverage
first divide the generated compile-command inventory across eight cache-warming
workers. They execute the exact assigned compiler commands through CMake's
captured launcher (not Ninja object edges, which carry broad target-order
dependencies) and transport immutable `sccache` result entries plus the
preprocessor indexes they wrote, which go only to the same revision's producer
(it then skips re-preprocessing) and never into the cross-commit seed. The
producer verifies their complete
disjoint plan, performs the ordinary full configure/build, and refuses any
compiler-cache miss,
including every archive, link and build gate, against the merged cache. The
cache seed is an explicit Nix input, never mutable state read through the
sandbox. Use the lean
`nix develop .#ci-native` shell to restore and run a downloaded bundle:
`python3 scripts/ci/nix_bundle.py restore --cache build/nix-cache --repo "$PWD"`.
Restore imports the dependency closure and verifies the product NAR before
moving its build tree into place, without importing a second product copy
into the Nix store. Canary bundles retain objects for incremental rebuilds
using their existing CMake cache, dependency paths and lane options. Other
bundles strip objects; rebuild them through the Nix producer, or configure
a separate fresh build tree in the full shell. These CI shells omit custom
dependency packages that consumers obtain from the bundle
closure, so entering a consumer shell does not build those packages again.

Every GitHub-hosted Nix job must install through
`.github/actions/install-nix`. The local composite action retains the pinned
upstream installer but puts a retrying curl wrapper in front of both its small
installer-script download and the official installer's large binary-tarball
download. Do not bypass it with a direct `cachix/install-nix-action` use: the
upstream action retries the first request, but the official installer does not
retry the tarball request that failed in production CI.

Coverage uses GCC; ASan, TSan and fuzzing use pinned Clang. Native CI bundles
retain file/line diagnostics with `-g1`, packed relocations and compressed
debug sections. Nix `strictoverflow` hardening is disabled because its wrapping
arithmetic flags suppress signed-overflow UBSan reports. Producer jobs run the
bundled `og_sanitizer_diagnostics` fixture on every invocation, including cache
hits; the checker requires the expected diagnostic and exact source line.

Use the full default `nix develop` shell for a fresh local CMake configure;
it also provides Emscripten and media tools. Run the mandatory tool lookup in
that shell before a build task. The native CI shell intentionally omits
Emscripten. Do not reset an imported bundle's cache with a fresh configure or
change its compiler/hardening settings.

- `VALIDATE_SERIALIZATION=ON` round-trips typed in-process messages. Match the
  lane's value when reproducing failures; an unvalidated local run cannot prove
  the serialization gate.
- TSan focuses on `og_unit_network`, which owns real WebSocket/relay threads
  and server/client/lobby handoffs. `og_unit_sim` retains deterministic gameplay.
- The normal canary lane builds the standalone mapgens and runs campaign drift
  and the X11 cases. A local unit run does not replace those checks.
- Native gates run each planned test once. Retrying a failed test is diagnostic
  only; preserve its first failure and fix it. Coverage measurements use clean
  profiles and a complete population, never accumulated retry profiles.
- The default Nix development wrapper can inject optimization through fortify.
  The `ci-native` shell matches native CI's hardening/compiler settings; do not
  reuse its configured tree with another compiler or hardening mode. `<regex>`
  remains banned by `scripts/check_no_std_regex.sh`; use exact-string oracles
  or predicates instead.

The **x11-display CI job** (#329) runs og_test_display_x11 on SDL's real
x11 driver under Xvfb; no plain local ctest runs it (the group is built and
registered only with `-DOPENGLAD_X11_DISPLAY_TESTS=ON`). Reproduce it with
the tree's own flake, which carries Xvfb/xrandr/openbox/xprop/xdpyinfo:
`nix develop <tree> -c bash scripts/ci/run_x11_display_lane.sh build/ci-test [one|two]`
after configuring with the option and building `og_test_display_x11`.
Starting Xvfb by hand, keep `-noreset`: without it the server resets when
its last client disconnects and every xrandr-added mode is gone before SDL
connects.

## Failures and delegated lanes

Every failure in a run you own needs a fix or an explicit current user waiver.
Historical notes about a flaky socket, slow menu or missing EMSDK are not a
waiver. Native CI records `emscripten_build_test` as delegated to the separately
required WASM graph; use the generated inventory and its completeness check,
not an ad hoc exclusion, when reproducing that split.

A loaded machine can expose a bad timeout or injector handshake. Record the
load and reproduce the failure, then fix its cause. Do not increase deadlines,
add retries, or declare a passing rerun the gate result. Keep the first log.

## Selecting bounded test groups

Slow GoogleTest binaries have several CTest entries. An anchored base name,
for example `^og_test_level$`, selects only its first partition. Use a prefix
such as `^og_test_level($|_part_)` to run that complete group; inspect the CTest
inventory when selecting several groups.

Campaign calibration cases follow the same rule: the original case name owns
partition zero, and `_part_*` cases retain the other original rows. A direct
filter such as `WestlandsCampaignTest.curve_*` runs the complete curve. No
calibration case skips an ASan run for its execution budget.

## Private native fixtures

CTest group entries use `tests/fixtures/run_isolated.py`. Each process gets
private installed assets, campaigns, cwd scratch and executable companions;
its harness also creates a private config/save directory. Missing-file and
malformed-config tests may change those copies without racing another group.
Only remove a resource lock or `RUN_SERIAL` after a concurrent run proves its
reason no longer applies; private files do not remove CPU or network limits.

Use the same launcher for a filtered binary run:

```bash
nix develop .#ci-native --command python3 tests/fixtures/run_isolated.py . \
  build/ci-test/og_unit_data --gtest_filter='GparserUnit.*'
```

A direct binary invocation bypasses that isolation and can still write through
a cwd fallback. Inspect `git status cfg/` after such a failure; never discard
someone else's configuration edits. Source-oracle helpers resolve the real
checkout through their canonical source paths, rather than treating the private
workspace as a Git repository.

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
- Fresh worktrees inherit no built assets. CTest creates scen99 through its
  fixture setup; direct runs use `tests/fixtures/scenario99.py setup "$PWD"`.
  Do not copy an unknown fixture from another checkout.
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
  the container overlay, at 95 % today. Which filesystem a sibling tree
  under /home/yans/code/ consumes depends on whether it is a symlink or
  a real directory. The lane worktrees are symlinks INTO the repo mount:
  `readlink -f /home/yans/code/og-audit-lane1` →
  `/home/yans/code/openglad/.claude/worktrees/og-audit-lane1`, and
  `git worktree list` shows all six lanes under `.claude/worktrees/`, so
  they consume the LV and df reports them correctly. /home/yans/code
  itself is on the overlay — `stat -c %d /home/yans/code` → `81`, the
  same device as `stat -c %d /`, against `64513` for the repo mount —
  so a worktree or build tree created there as a real directory eats the
  overlay instead. `/home/yans/code/openglad-master` is one such real
  directory (`stat -c %d` → `81`). Before parking a multi-GB tree, write
  a 500 MB probe and re-read `df /` and `df <repo>`. Today's probe, inside
  the symlinked lane: LV available 3323193264 → 3322680052 KB (−513212),
  overlay available unmoved (27688392 → 27689020, noise).
- Run CTest from the repo root so the selected preset resolves correctly.
  Group entries arrange their own private runtime cwd through the launcher
  above. Standalone lifecycle tests and the curses harness retain their own
  CTest working directories; use their registered entries to reproduce them.
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
  ffmpeg, imagemagick). The shell also carries `sdl2-compat` and
  `SDL2_mixer` (dev outputs) for the parity companion's build script
  only; the game itself finds and links SDL3.
- gcovr is in the Nix environments used by the coverage lane and local
  measurements; no separate pip installation is needed.
- The old game (branch `parity-companion`, the source of every parity
  golden): to rebuild it and reproduce every golden, follow
  `tests/parity/golden/DRIFT_LEDGER.md`, "Recapturing every golden from the
  old game". For a recapture the worktree is at the ledger's pin, not the
  branch tip; the build traps are in openglad-parity, "The old game".
- Filtered parity CTest runs include `scenario99_setup` and cleanup
  automatically; they do not require `og_test_level`.
- Relay/Pages deploys read credentials from the gitignored `./env`
  (never printed — see AGENTS.md Secrets).
- Playwright browser installs may need a platform override on very new
  Ubuntu (`PLAYWRIGHT_HOST_PLATFORM_OVERRIDE=ubuntu24.04-x64`) or the
  nixpkgs `playwright-driver.browsers` path.
