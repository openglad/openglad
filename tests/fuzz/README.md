# Production parser fuzzing

`fuzz_scenario` calls `og::data::load_level` and
`save_level_scenario_file`. `fuzz_savefile` calls `SaveData::load` and
`SaveData::save`. Neither harness implements a substitute parser.

Both use `runtime.cpp`: a private filesystem and config directory per process,
staged game assets and core packs, fixed auxiliary PNG grids, and a fresh
`GameWorld`, `SaveData`, event log and seeded RNG per input. Campaign mounts
are removed between inputs. Scenario inputs mutate FSS bytes; the auxiliary
PNGs are fixed, and the scenario writer saves FSS payloads only.

An accepted input must report success consistently with its I/O error value.
Loaded scenarios require a valid grid and entities with valid stats/team IDs;
loaded saves require a bounded roster with no empty declared slots. Both harnesses then require
save → load → save to reach identical canonical bytes. Rejected inputs return
normally; sanitizer errors and violated properties abort with a reproducer.
These properties do not prove complete field preservation, gameplay behavior,
PNG decoder coverage, or independence from every possible input ordering.

The `ci-fuzz` Nix producer compiles the production game libraries, Lua and
harnesses with Clang's `fuzzer-no-link`, ASan, UBSan and float-cast-overflow
instrumentation; each executable links libFuzzer. `TESTING` is disabled. The
runner checks compilation records for both production parser translation
units before executing inputs. Nix also instruments the separately built
IXWebSocket and LodePNG dependencies; other packaged dependencies retain their
package build settings.

From a clean checkout without an existing `build/ci-fuzz` tree:

```bash
nix develop --command bash -c 'which cmake c++ emcc'
nix develop .#ci-fuzz --command bash -c '
  nix build .#ci-fuzz --out-link build/bundle-fuzz --cores 3
  python3 scripts/ci/nix_bundle.py activate --bundle build/bundle-fuzz --repo "$PWD"
'
```

Generate modern seeds directly with the production writers:

```bash
nix develop .#ci-fuzz --command build/ci-fuzz/fuzz_scenario --generate-seeds build/seeds/scenario
nix develop .#ci-fuzz --command build/ci-fuzz/fuzz_savefile --generate-seeds build/seeds/savefile
```

Scenario seeds cover the writer's v9, v10 multi-floor and v11 decor formats;
save seeds use v21 with empty and populated companies. Generation checks that
the production reader accepts the generated files and reaches the intended
roster/entity/floor/decor fields. The committed legacy files under `corpus/`
may be malformed; retaining them exercises rejection paths and does not make
them evidence of successful deep parsing.

The runner generates seeds, imports committed and cached inputs, replays the
whole corpus with `-runs=0`, then mutates it with a dictionary and value
profiling. To run both targets locally:

```bash
nix develop .#ci-fuzz --command bash -c '
  for target in scenario savefile; do
    python3 scripts/fuzz/run.py --target "$target" --seconds 120 --seed 1 \
      --corpus "build/fuzz-corpus/$target" --output "build/fuzz-results/$target"
  done
'
```

Add `--minimize` to run libFuzzer's coverage-preserving corpus merge after
successful fuzzing. CI gives each target 120 seconds on PRs/manual dispatches
and 1,800 seconds nightly. Nightly runs use the run number as their mutation
seed and persist only successfully minimized corpora. Inputs are bounded to
1 MiB, 15 seconds each and 2 GiB RSS. A failed phase stops the run without a
retry; process timeouts also fail.

Evidence lives under the requested output directory: `summary.json` records
binary/source hashes, revision, phase results and corpus sizes; phase JSON
records elapsed time and reported initial/final libFuzzer edge/feature counts;
`*.log.gz` retains full output, `generated-seeds/` the modern inputs, and
`crashes/` reproducers. CI uploads `fuzz-evidence-<target>`. Read those results
before claiming successful parser exploration or branch growth; the harness
implementation alone establishes neither.

Replay a retained crash against the same binary and staged assets:

```bash
nix develop .#ci-fuzz --command bash -c '
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    build/ci-fuzz/fuzz_scenario build/fuzz-results/scenario/crashes/crash-HASH
'
```

Use `fuzz_savefile` and its corresponding path for a savefile reproducer.
