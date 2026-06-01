## Findings — Plan Review (parity-finish-3, checker after the three-blocker refinement)

The prior three blocking issues (Phase 03 using flags introduced only in
Phase 04, Phase 04 verifier 04b consuming `--emit-scenario-list` that no
phase added, and Phase 03's false claim of "preexisting per-Order
helpers" in `state_dump.cpp`) are now resolved. The plan correctly:

- replaces the `capture_master_golden.sh --all --no-write --diff`
  invocation in Phase 03 with an inline `for g in tests/parity/golden/*.json`
  shell loop that uses only the companion binary's existing
  single-scenario contract;
- extends Phase 01's `check_coverage_manifest.py` work to add BOTH
  `--emit-gap-table` and `--emit-scenario-list` under a new `argparse`
  front-end (also reflected in §4 "Critical Files");
- restates that `state_dump.cpp` today has ONE bare `family_symbol`
  function called by all three collectors, and that Phase 03 introduces
  `family_symbol_by_order(order, family_id)` plus 5 per-Order tables and
  rewrites all three call sites (the "reuse pre-existing per-Order
  helpers" wording is gone);
- tightens 04a's PASSED assertion from a ≥-bound to an exact
  `P + F + S` (=150 today) derivation;
- routes Phase 08's `requires_rng_insensitive_predicate` exempt set
  through computed `compare_mode` from `scenario_facts_generated.json`
  instead of a hand-edited list;
- makes Phase 11's CI conditional total (`if [ -f .github/workflows/ci.yml ]; then …; else true; fi`);
- adds a 03b assertion that `parity_dump_master` mtime is newer than
  both `parity_dump_state.cpp` AND `parity_dump_state.h`.

Topology, artifact flow, and contract conformance remain sound: 11
implement phases × 3 verifiers each = 44 phases, every check has a
single fixed `bounce_target`, `Preexisting Inputs` and `New Outputs`
are cleanly separated, inline-only YAML and commit-before-yield are
restated, and existing artefacts are consumed in place.

One **new blocking issue** has been introduced by the refinement of
Phase 11. It stems from a piece of guidance in the previous findings
that was itself factually wrong; the plan adopted it verbatim.

### Blocking

1. **Phase 11 Bypass F asserts three `ADD_FAILURE() << "master golden missing"`
   conversions but only one site can plausibly carry that wording.**

   Phase 11 §"New Outputs" §F now says:

   > **F**: Delete a golden to make a row skip → flip
   > `test_parity_scenarios.cpp` so missing-golden + `compare_mode == SemanticParity`
   > becomes `ADD_FAILURE` instead of `GTEST_SKIP`. The file currently
   > has THREE missing-golden `GTEST_SKIP` call sites
   > (`tests/parity/test_parity_scenarios.cpp:108`, `:118`, `:144`); Phase
   > 11 converts all three. Verifier asserts
   > `grep -c 'GTEST_SKIP() << "master golden missing'
   >   tests/parity/test_parity_scenarios.cpp` equals `0` and
   > `grep -c 'ADD_FAILURE() << "master golden missing'
   >   tests/parity/test_parity_scenarios.cpp` equals `3`.

   Inspection of the live file shows three GTEST_SKIP sites with three
   distinct literal messages — only one of them mentions "master golden
   missing":

   | Line | Branch | Literal message |
   |------|--------|-----------------|
   | 108  | `compare_mode == SemanticParity` && golden absent | `"master golden missing for "` |
   | 118  | `compare_mode == ByteEqual` && golden absent       | `"golden not yet captured for "` |
   | 144  | `OG_PARITY_TEST(NAME)` macro, scenario not in `kScenarios` | `"scenario \"" #NAME "\" is not present in kScenarios; "` |

   So `grep -c 'GTEST_SKIP() << "master golden missing'` currently
   returns `1` (not `3`), and the verifier's pre-conversion count
   assertion (`equals 0` after Phase 11) is already half-satisfied by a
   one-site flip. To make the post-conversion count
   `'ADD_FAILURE() << "master golden missing'` equal `3`, the
   implementer would have to:
   - flip :118 (a *ByteEqual* missing-golden site, which the narrative
     explicitly excludes by the "`compare_mode == SemanticParity`"
     scope) AND rewrite its message from `"golden not yet captured for "`
     to `"master golden missing for "` — a misleading rewrite, since the
     site fires under ByteEqual, not SemanticParity;
   - flip :144 (the *missing-scenario-in-kScenarios* SKIP inside the
     `OG_PARITY_TEST` macro, a structurally different failure mode that
     has nothing to do with a missing golden file) AND rewrite its
     message similarly — which destroys the original failure-mode
     distinction and makes future debugging materially worse.

   Either the verifier is asserting the wrong count or the implementer
   has to lie in source-text wording. As written the verifier trips on
   first invocation, and the prior-findings author's recommendation to
   "specify all three" was a mistake.

   **Fix**:
   - Scope Phase 11 §F + verifier 11a's grep to the **single** :108
     `compare_mode == SemanticParity` site. The narrative already says
     "when `compare_mode == SemanticParity`" — make the verifier match
     that.
   - Verifier 11a's assertions become:
     - `grep -c 'GTEST_SKIP() << "master golden missing' tests/parity/test_parity_scenarios.cpp` equals `0`.
     - `grep -c 'ADD_FAILURE() << "master golden missing' tests/parity/test_parity_scenarios.cpp` equals `1`.
   - Drop the "THREE … (`:108`, `:118`, `:144`); Phase 11 converts all
     three" sentence. If the plan wants to harden the other two paths
     (ByteEqual missing-golden, missing-scenario macro), it should
     introduce them as **separate** bypasses with their own literal
     greps and their own per-site rationales — not lump them under
     Bypass F.

### Non-blocking but worth tightening

- **`kRequiredSpecials` field-name references** (Phase 01 §1
  "Per-target coverage gap inventory" bullet 6) call out
  `kRequiredSpecials[i].family` and `kRequiredSpecials[i].slot_index`,
  but the constant is declared `std::pair<std::int32_t, std::uint8_t>`,
  so the members are `.first` and `.second`. The verifier 01c still
  works because the implementer plainly reverse-maps the int to the
  family symbol and prints the slot ordinal, but the field-name prose
  is technically wrong. A one-line correction ("`(family,
  special_index)` pairs read as `pair::first` and `pair::second`") would
  prevent the workflow writer from emitting verifier code that looks up
  named fields that do not exist.

- **Phase 03 `family_symbol` removal-vs-wrapper rule** is left to the
  implementer's judgment ("removed (or kept as a thin wrapper …) only
  if other callers exist in `state_dump.cpp`"). The verifier 03b only
  asserts the *three collectors* no longer call the bare `family_symbol(`.
  If the legacy symbol survives as an unused helper, the regex check
  still passes, but no verifier asserts the wrapper's correctness. A
  cleaner contract: delete `family_symbol` outright; verifier asserts
  `grep -c '^std::string family_symbol' state_dump.cpp` equals `0`.
  Equivalent: keep the wrapper but assert `family_symbol(x)` returns
  identical strings to `family_symbol_by_order(Order::Living, x)` via a
  pure unit test in `og_unit_parity` (new).

- **Phase 04 `check_coverage_manifest.py --emit-scenario-list` output
  contract** is described as `<id>\t<compare_mode>\t<is_branch_internal>`
  but no test rows or example lines are given. Verifier 04b's
  enumeration logic depends on parsing this with split-by-tab semantics;
  consider pinning one literal example line in §4 to keep the workflow
  writer from inventing a different separator. (Phase 04 verifier
  parses but the parsing rule is implicit.)

- **`.plan/parity-canary-exemptions.md` format** is asserted by 10b to
  be `load_exemptions()`-parseable, but `load_exemptions()` lives at
  `scripts/parity/run_mutation_canary_runtime.py:74-91`. The plan should
  cite the literal grammar accepted (the comment in §10 says "`- <id>`
  bullets only; the runtime parser does not understand pipe-tables").
  Move the literal grammar — at minimum a single example line — into
  Phase 10's "New Outputs" so the implementer doesn't reach for the
  source to derive the format.

### Verdict

The plan is structurally sound and 95% of the way there. The single
remaining blocker (Phase 11 Bypass F's three-site claim) is a concrete
prompt-contract error: it tells the implementer to make changes that
contradict the same paragraph's `compare_mode == SemanticParity` scope
and configures a verifier whose literal grep cannot match the legitimate
post-state. It must be fixed before workflow generation, otherwise the
generated 11a verifier will bounce on its first invocation regardless
of how the implementer interprets the conflicting prose.
