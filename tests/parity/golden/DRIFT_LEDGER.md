# Golden ledger

Old-game commit the goldens are captured from: `04611243`

The rule since 2026-10-04 (Yan): a parity golden is a capture from the old game — branch `parity-companion`, built
into `parity_dump_master` by `scripts/parity/build_parity_dump_master.sh` and captured by
`scripts/parity/capture_master_golden.sh` (the golden capture tool) — and never a capture from the current game. When
this ledger was rewritten on 2026-10-04, 70 of its 223 goldens still predated the rule and were current-game captures,
listed under "Open". #349 (2026-10-05) ported their fixes into the old game and closed that list: 223 of 223 goldens
now `cmp` equal to a full capture from the old game at the pin above. That PR recaptured 12 goldens from the old game
(9 whose bytes changed only in notification `a`/`b`, 2 whose level-ending tick changed its event order, and the
archmage marker row); the other 211 were already byte-identical to the capture. The sim-correctness batch (#320, #350;
2026-10-06) ported two more fixes (`cd97028a`, `a4491684`), added two rows, reshaped one, and moved the pin to `04611243`; 225 of 225
goldens `cmp` equal to a full capture at that pin. "Recapturing every golden from the old
game" below rebuilds the old game at the pin and reproduces every golden. When the current game departs from
classic behaviour on purpose, the same fix is ported into the old game's C++, one fix per commit (the message names the
current-game commit), the affected goldens are captured from the old game again, and the pin moves to the new old-game
commit; "Ported fixes" records those ports. Merge-base adjudication (the method the sections under History used)
remains a DIAGNOSTIC that tells you whether a divergence is yours, never a way to source a golden.

Terms used below: "the old game" is the `parity-companion` branch (older entries call it "the companion" or
"master"); "the current game" is this repository's game (older entries call it "the branch"); "the golden capture
tool" is `parity_dump_master` with `capture_master_golden.sh`; "the parity test driver" is `tests/parity`.

## Recapturing every golden from the old game

Anyone can rebuild the old game at the pin above and reproduce every golden byte for byte:

    cd <openglad checkout>                      # the current game; it carries flake.nix
    git fetch origin parity-companion           # never with --depth: ancestry needs history
    PIN=$(sed -n 's/^Old-game commit the goldens are captured from: `\([0-9a-f]*\)`$/\1/p' tests/parity/golden/DRIFT_LEDGER.md)
    git worktree add --detach ../openglad-old "$PIN"
    # the scen99 fixture the scen99 rows load (og_test_level writes temp/scen/)
    cmake --preset ci-test && cmake --build --preset ci-test --target og_test_level
    ctest --preset ci-test -R '^og_test_level$'
    # build the golden capture tool (2013 C++, SDL2, a hand g++ script, no CMake; ~5 min)
    nix develop . -c bash ../openglad-old/scripts/parity/build_parity_dump_master.sh
    #   without nix: apt-get install g++ pkg-config libsdl2-dev libsdl2-mixer-dev, then run the script directly
    MASTER_WORKTREE=$PWD/../openglad-old \
    OG_PARITY_SCEN99_FSS=$PWD/temp/scen/scen99.fss \
      scripts/parity/capture_master_golden.sh --all --out-dir /tmp/recapture 2>&1 | tee /tmp/recapture.log
    grep -c 'using embedded fixture' /tmp/recapture.log   # must print 0
    for f in tests/parity/golden/*.json; do
        cmp -s "$f" "/tmp/recapture/${f##*/}" || echo "differs: ${f##*/}"
    done                                                  # must print nothing

Never omit `--out-dir`: a bare invocation captures `--all` into `tests/parity/golden`. After editing an old-game
header, `rm -rf ../openglad-old/build/ci-test/obj` before rebuilding: the build script only recompiles a `.cpp` newer
than its `.o`. `scripts/check_parity_companion_refs.sh` proves the pin is on `origin/parity-companion` and that
`tests/parity/scenario_table.h` equals the pin's `tools/parity_scenario_table.h`.

## Ported fixes

Each C5 port (2026-10-04) was proven the same way, in its own worktree of the old game: every id was captured with the golden capture
tool before the port and after it (`capture_master_golden.sh --all --out-dir <dir>`, `OG_PARITY_SCEN99_FSS` exported,
the before capture `diff -rq`-identical to W5-PARITY's old-game capture), `diff -rq before after` had to name exactly
the rows the fix targets, and each of those rows had to `cmp` equal to the committed golden. A row that does not reach
byte identity is reported with its `diff_dumps.py` output and stays under Open; nobody edits a golden by hand. After
the three C5 ports were merged, a full capture at `ab0a7d2a` differed from W5-PARITY's pre-port capture in
exactly 9 ids. After the #349 ports, a full capture at `1bafb5da` differed from the `ab0a7d2a` capture in exactly
75 ids: the 70 formerly Open, minus the two score-order rows (the parity test driver now matches the old game's
unchanged bytes there), plus the 7 goldens recaptured for notification `a`/`b` alone. After the #320/#350 ports, a full
capture at `04611243` differed from the `1bafb5da` capture in exactly 5 ids (3 for #350, 2 for #320 on top;
`thief_taunt_matched_levels_scen99` also changed shape), plus the 2 new ids.

Old-game shas below are the commits on `parity-companion` as pushed to `origin/parity-companion`. Every sha in an
`old-game commit` cell, and the pin, is checked by `scripts/check_parity_companion_refs.sh`, an `og_test_parity` build
dependency: each must be on `origin/parity-companion` and an ancestor of the pin, `tests/parity/scenario_table.h` must
`cmp` equal to the pin's `tools/parity_scenario_table.h`, and every old-game commit from `a2d9d470` to the pin that
touches `src/` must have a cell. Write an old-game sha you want checked only in such a cell; prose shas are not read.

| id | fix (issue; GAMEPLAY_FIXES row) | current-game commit (master squash; pre-squash id) | old-game commit | recaptured from the old game | cmp proof | what moved vs the previous golden |
|---|---|---|---|---|---|---|
| `enemy_freeze_mage_scen99` | Freeze-time census (#231; "Freeze time cleared the level (#231)") | `25113b61` (`8726bbfb`) | `edb8550c` | 2026-10-04, at `ab0a7d2a` | `cmp <after>/enemy_freeze_mage_scen99.json tests/parity/golden/enemy_freeze_mage_scen99.json` exit 0 (before the port: 138 `diff_dumps.py` leaves, all the #231 signature); `diff -rq before after` over all 223 ids named exactly these four (C5-FREEZE, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | The frozen team-1 archer now holds the level open. `level_done` 2 → 0, and the 130 `end_game` + one `set_end` events the old golden carried from tick 20 onward are gone (`events` 140 → 9; the eight `play_sound` and the `set_palette` that make up those nine are unchanged, value for value). Because the tick (the current game's `GameWorld::tick`, the old game's `screen::act`) no longer short-circuits at `if (game_ended) return;` from tick 20 on, the stale-pointer and dead sweeps keep running: `walkers[]` loses the reaped `FAMILY_HIT` husk (3 → 2) and `weapons[]` loses five spent `FAMILY_FIRE_ARROW` (5 → 0). `rng_state` (`0xA8102E2C`), `score_per_team`, the archer's pinned (200,120) and all 93 `weapon_tracks` samples are byte-identical to the old golden. Teeth: `kFacts_enemy_freeze_mage_scen99` gained `LevelDoneEquals(0)` — the row's only fact that reads the field that moved. It fails against the pre-rebaseline golden (`level_done` 2) and against any regression that folds the census back inside the act gate. |
| `input_special_switch_wrap_scen99` | Freeze-time census (#231; "Freeze time cleared the level (#231)") | `25113b61` (`8726bbfb`) | `edb8550c` | 2026-10-04, at `ab0a7d2a` | `cmp <after>/input_special_switch_wrap_scen99.json tests/parity/golden/input_special_switch_wrap_scen99.json` exit 0 (before the port: 150 `diff_dumps.py` leaves, all the #231 signature); `diff -rq before after` over all 223 ids named exactly these four (C5-FREEZE, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | Same fix, same signature, on a second freeze arena: the special-switch wrap lands on FREEZE TIME and the frozen team-1 soldier now holds the level open. `level_done` 2 → 0, and the 134 `end_game` + one `set_end` the old golden carried from tick 16 onward are gone (`events` 138 → 3; the two `play_sound` and the `set_palette` that remain are unchanged, value for value). With the tick no longer short-circuiting, the sweeps keep running: `walkers[]` loses a `FAMILY_KNIFE_BACK` and a `FAMILY_HIT` husk (4 → 2) and `weapons[]` loses five spent `FAMILY_KNIFE` (5 → 0). `rng_state` (`0x9563F676`), `score_per_team`, both live walkers' hp and positions and all 13 `weapon_tracks` samples are byte-identical to the old golden. Teeth: `kFacts_input_special_switch_wrap_scen99` gained `LevelDoneEquals(0)`, the only fact here that reads the value that moved; it fails against the pre-rebaseline golden (`level_done` 2). |
| `special_mage_3_scen99` | Freeze-time census (#231; "Freeze time cleared the level (#231)") | `25113b61` (`8726bbfb`) | `edb8550c` | 2026-10-04, at `ab0a7d2a` | `cmp <after>/special_mage_3_scen99.json tests/parity/golden/special_mage_3_scen99.json` exit 0 (before the port: 168 `diff_dumps.py` leaves, all the #231 signature); `diff -rq before after` over all 223 ids named exactly these four (C5-FREEZE, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | Same fix, third freeze arena, but the level_done half of the signature is invisible here: the mage is level 7, so the freeze bank is 20+11*7 = 97 ticks and expires at tick 117, and the census re-counts the team-1 soldier long before the 150-tick budget ends. `level_done` reads 0 at the final tick with the bug as well as without it. What the bug left behind is the tail: the 96 `end_game` + one `set_end` from ticks 20-115 are gone (`events` 104 → 7; the five `play_sound` and two `set_palette` that remain are unchanged, value for value). The resumed sweeps drop three `FAMILY_KNIFE_BACK` and three `FAMILY_HIT` husks (`walkers[]` 8 → 2) and 38 spent `FAMILY_KNIFE` (`weapons[]` 39 → 1). Note that ids RENUMBER as well as the count dropping: the surviving knife is the same knife — team 1 at (157,125), where the old golden had two, ids 40 and 47 — but it now carries id 3. Dump ids are not game ids: the golden capture tool numbers every dumped object at capture time with one running ordinal over oblist, then fxlist, then weaplist (`tools/parity_dump_state.cpp` `collect_walkers` / `collect_effects` / `collect_weapons`, `entry.id = ++running_seq`, in the old game). After the sweep two walkers remain and fxlist is empty, so the surviving knife is ordinal 3; before it, the knife came after eight walkers and 38 other weapons and was 47. A diff that reads `id 47 → id 3` here is looking at one knife, not two. (Correction, 2026-10-04: an earlier version of this row said a sweep "frees ids again"; it does not, the ids are capture-time ordinals.) `rng_state` (`0x7AA815BE`), `score_per_team`, both live walkers and all 38 `weapon_tracks` samples are byte-identical to the old golden. Teeth: `LevelDoneEquals(0)` would be a dead fact here — it holds against the pre-rebaseline golden too — so the row carries `EventKindExactly(end_game, 0)` instead: it reads the half of the signature this arena does show, and fails against the old golden's 96 `end_game`. |
| `treasure_magic_potion_overfill_scen99` | Freeze-time census (#231; "Freeze time cleared the level (#231)") | `25113b61` (`8726bbfb`) | `edb8550c` | 2026-10-04, at `ab0a7d2a` (NOT replaced: see cmp proof) | `cmp` exit 1, reported and not blessed: the #231 half of the diff is gone (before the port: 139 leaves), and `diff_dumps.py <golden> <after capture> 2>&1` prints exactly `[1/1] events[0].a: branch=0 master=3` and `FAIL event_field events[0].a`: ONE residual leaf, class R notification metadata. The golden is left as committed and the row is also listed under Open (Update (2026-10-05): closed by #349's notification form, notif a/b; the unchanged golden now `cmp`s equal to the old game); `diff -rq before after` over all 223 ids named exactly these four (C5-FREEZE, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | Same fix, fourth freeze arena — the overfilled mage's 152-tick FREEZE TIME pins a team-1 archer, and the archer now holds the level open. `level_done` 2 → 0, and the 130 `end_game` + one `set_end` from tick 20 onward are gone (`events` 141 → 10; the eight `play_sound`, the `notification` and the `set_palette` that remain are unchanged, value for value). The resumed sweeps drop the reaped `FAMILY_HIT` husk (`walkers[]` 3 → 2) and five spent `FAMILY_FIRE_ARROW` (`weapons[]` 5 → 0). `rng_state` (`0xA8102E2C`), `score_per_team`, the archer's pinned (200,120) and all 93 `weapon_tracks` samples are byte-identical to the old golden. Teeth: `kFacts_treasure_magic_potion_overfill_scen99` gained `LevelDoneEquals(0)`, which fails against the pre-rebaseline golden (`level_done` 2). |
| `special_soldier_2_scen99` | Hostile weapon finder and shield list (#294; "Hostile weapon finder and shield list (#294)") | `966cbb55` (`8a4ac0c4`) | `84b94f9f` | 2026-10-04, at `ab0a7d2a` | `cmp <after #294>/special_soldier_2_scen99.json tests/parity/golden/special_soldier_2_scen99.json` exit 0 (before the port the old-game capture was `cmp`-identical to the pre-#315 golden); `diff -rq` of the all-ids captures before and after the #294 port named exactly the five #294 ids (C5-FINDERS, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | Measured against the pre-fix golden (the capture from merge base `b1412cab`): The two soldiers finish on 59/98 HP instead of 52/97; team-0 score is 24 instead of 25. Knife, knife-back and hit tracks fall from 48/47/38 to 42/43/32 while boomerang tracks stay at 79; RNG state also moves. |
| `weapon_boomerang_return_scen99` | Hostile weapon finder and shield list (#294; "Hostile weapon finder and shield list (#294)") | `966cbb55` (`8a4ac0c4`) | `84b94f9f` | 2026-10-04, at `ab0a7d2a` | `cmp <after #294>/weapon_boomerang_return_scen99.json tests/parity/golden/weapon_boomerang_return_scen99.json` exit 0 (before the port the old-game capture was `cmp`-identical to the pre-#315 golden); `diff -rq` of the all-ids captures before and after the #294 port named exactly the five #294 ids (C5-FINDERS, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | Measured against the pre-fix golden (the capture from merge base `b1412cab`): The soldier finishes on 102 instead of 95 HP and the boomerang on 80 instead of 90. Arrow and hit tracks fall from 6/18 to 4/12; the archer ends at (164,164) instead of (160,160), and RNG state moves. |
| `effect_boomerang_contact_scen99` | Hostile weapon finder and shield list (#294; "Hostile weapon finder and shield list (#294)") | `966cbb55` (`8a4ac0c4`) | `84b94f9f` | 2026-10-04, at `ab0a7d2a` | `cmp <after #294>/effect_boomerang_contact_scen99.json tests/parity/golden/effect_boomerang_contact_scen99.json` exit 0 (before the port the old-game capture was `cmp`-identical to the pre-#315 golden); `diff -rq` of the all-ids captures before and after the #294 port named exactly the five #294 ids (C5-FINDERS, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | Measured against the pre-fix golden (the capture from merge base `b1412cab`): The soldier finishes on 108 instead of 98 HP; the boomerang falls from 98 to 73 HP and the tower ends on 38 instead of 37. Two surviving weapons become none, arrow tracks fall from 89 to 46, events from 16 to 15 and team-0 score from 97 to 96; RNG state moves. |
| `shield_projectile_absorb_scen99` | Hostile weapon finder and shield list (#294; "Hostile weapon finder and shield list (#294)") | `966cbb55` (`8a4ac0c4`) | `84b94f9f` | 2026-10-04, at `ab0a7d2a` | `cmp` exit 0 after the #294 port (before it the old-game capture differed from the golden in 15 leaves, first `walkers[1].hp` 114.0 vs 119.0); `diff -rq` of the all-ids captures before and after the #294 port named exactly the five #294 ids (C5-FINDERS, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | New arena (added with the fix): a nearby hostile `ARROW`, a distant hostile `ARROW` and a nearby friendly `FIRE_ARROW` in `weaplist`. With the fix the nearby hostile arrow is gone, the distant hostile and friendly projectiles remain, and the guard loses 5 HP; see the wrong-list probe below. |
| `boomerang_projectile_absorb_scen99` | Hostile weapon finder and shield list (#294; "Hostile weapon finder and shield list (#294)") | `966cbb55` (`8a4ac0c4`) | `84b94f9f` | 2026-10-04, at `ab0a7d2a` | `cmp` exit 0 after the #294 port (before it: 17 leaves, first `walkers[1].hp` 93.0 vs 98.0); `diff -rq` of the all-ids captures before and after the #294 port named exactly the five #294 ids (C5-FINDERS, old-game worktree; before capture `diff -rq`-identical to W5-PARITY's). | New arena (added with the fix): a nearby hostile `ARROW`, a distant hostile `ARROW` and a nearby friendly `FIRE_ARROW` in `weaplist`. With the fix the nearby hostile arrow is gone, the distant hostile and friendly projectiles remain, and the guard loses 5 HP; see the wrong-list probe below. |
| — (zero goldens) | Ally finder, spell counts and hit-response ordering (#295; "Ally finder, spell counts and hit-response ordering (#295)") | `966cbb55` (`8a4ac0c4`) | `0d20806f` | — | `diff -rq` of the all-ids captures before and after the #295 port: EMPTY (C5-FINDERS). | Nothing in the corpus. The edge the fix changes is the low-HP hit from a new attacker (`hit_response` retargets and records the attacker before `yell_for_help` queues the flee walk); no parity arena reaches it. Its effect outside the corpus is recorded below. |
| — (zero goldens) | Native rename of the nearest-foe scan (#293) | `966cbb55` (`8a4ac0c4`) | none | — | — | Rename only, no behaviour, nothing to port; the old game keeps `find_far_foe`. The rename moved no corpus row (the wrong-list probe below kept it and stayed byte-identical to the before capture). |
| — (zero goldens) | Runaway-specials §4 selective clear on a control claim or switch (`docs/runaway-effects-design.md` §4) | `c409e7c8` | `ab0a7d2a` | — | `diff -rq` of the all-ids captures before and after the port: EMPTY, 223/223 byte-identical (C5-CLAIM, clean rebuild of the golden capture tool after the `src/stats.h` change); zero goldens move. In the current game `og_test_parity` stays 288/288 with no golden changed after the parity test driver's fold (`cda95d51`). | Ported into the old game's `src/stats.h` / `src/stats.cpp` (a `forced` flag that `force_command` sets, and `clear_command_for_control_switch`: the leading run of forced `COMMAND_WALK` entries is kept, `real_team_num` is not restored, the weapon reset and leader clear are kept), the four `src/view.cpp` claim/switch sites and the golden capture tool's `claim_control` (`tools/parity_dump_master.cpp`). The four `view.cpp` sites are compiled into the golden capture tool but never executed by it; only the `claim_control` site reaches a capture, so a clean build is the `view.cpp` sites' only evidence. The parity test driver now claims through `sim_claim_control` too (current game `cda95d51`). |
| `effect_bomb_bystander_scen99` | Stationary shooter heading after a snap-face (#350; "Stationary shooter heading after a snap-face (#350)") | (this PR) (`cb3dacc9`) | `cd97028a` | 2026-10-06, at `04611243` | `cmp <cap-final>/effect_bomb_bystander_scen99.json tests/parity/golden/effect_bomb_bystander_scen99.json` exit 0, and the capture after `cd97028a` `cmp`s equal to the current game's dump at `9a38ee54`; `diff -rq` of the all-ids captures at `1bafb5da` and after `cd97028a` named exactly the three #350 ids | `rng_state` only (`0x7920175E` → `0x0DB04883`): at tick 94 the TOWER1 snap-faces `(-1,1)`; at tick 95 its probe reads DOWN_LEFT instead of FACE_UP, the arrow keeps stepsize 8 instead of 11.3125, and the one waver draw's bound is 5 instead of 6. Every walker, weapon, event and track is byte-identical. |
| `effect_knife_back_catch_scen99` | Stationary shooter heading after a snap-face (#350; "Stationary shooter heading after a snap-face (#350)") | (this PR) (`cb3dacc9`) | `cd97028a` | 2026-10-06, at `04611243` | same shape as the row above | 957 leaves from `events[17].b`: the tower's snap-faced shots now go toward the foe; soldier 62 → 64 hp, TOWER1 85 → 86, knife tracks 10 → 9. Facts unchanged and green. |
| `thief_taunt_matched_levels_scen99` | Stationary shooter heading after a snap-face (#350; "Stationary shooter heading after a snap-face (#350)"), and the row changed shape | (this PR) (`cb3dacc9`; the reshape lands with the goldens) | `cd97028a` | 2026-10-06, at `04611243` (reshaped row) | `cmp` exit 0 against the capture at `04611243`; the OLD-spec capture after `cd97028a` also `cmp`'d equal to the current game's old-spec dump at `9a38ee54` (the port proof); the captures after `cd97028a` and after `a4491684` `cmp` equal (#320 leaves it unmoved) | Two things. (1) The fix: the team-0 decoy tower now returns fire (it fired 0 arrows at `1bafb5da`; see the correction under "#349 ports"), which retargets the foes onto it, and with the thief at (120,120) only the orc was inside the 108 px taunt range at the tick-20 cast, rolling a 5-5 tie that both adjudication orders accept: the taunt-inversion pin moved NOTHING (byte-identical arms, measured). (2) The reshape: the thief now stands at (140,140), both foes are in range at the cast (slime 5 vs 2, orc 3 vs 3), the foes go for the thief instead of the decoy, and the facts are the exact slime hp (7200, 6100 under the inversion), the exact orc hp (14000, 13100 at (192,177) under the inversion) and the decoy's full hp as a control; the orc position fact is gone (a `>=` bound both arms satisfy). The old golden's values are not comparable to the new ones. |
| `multiplayer_two_teams_scen99` | Nearest-foe adoption in the melee approach (#320; "Nearest-foe adoption in the melee approach (#320)") | (this PR) (`9a38ee54`) | `a4491684` | 2026-10-06, at `04611243` | `cmp` exit 0, and the capture after `a4491684` `cmp`s equal to the current game's dump at `9a38ee54`; `diff -rq` of the all-ids captures after `cd97028a` and after `a4491684` named exactly the two #320 ids | 160 leaves from `events[3].tick` 26 → 27: at tick 24 the team-1 archer adopts the team-2 thief (16 px) over the team-0 soldier (52 px) and its arrows land on the thief (56 hp, was 67; soldier 71, was 72). All five facts stay green. |
| `special_thief_2_scen99` | Nearest-foe adoption in the melee approach (#320; "Nearest-foe adoption in the melee approach (#320)") | (this PR) (`9a38ee54`) | `a4491684` | 2026-10-06, at `04611243` | same shape as the row above | Draw order only: the melee find now precedes the ATTACK-duration draw, and the cloaked level-4 thief's `rng(invis/20)` moves ahead of `rng(25)` at ticks 73 and 92; no foe changes (147 leaves from `events[2].tick` 93 → 94). The thief's exact hp fact is retuned to the capture's value (2800, was 2700); its pin rationale now names the real mutated value (700). |
| `tower_snap_face_heading_scen99` | Stationary shooter heading after a snap-face (#350; "Stationary shooter heading after a snap-face (#350)") | (this PR) (added with the goldens) | `cd97028a` | 2026-10-06, at `04611243` (row mirrored in `04611243`) | `cmp` exit 0 against the current game's dump (new arena) | New arena (added with the fix): the pre-reshape `thief_taunt_matched_levels` spawns with no inputs; the stationary tower's arrows hit the slime (7500 hp) where before the fix its FACE_UP probe missed and its one arrow flew due west. Tooth: the slime fact fails under the pin's revert. |
| `walk_to_foe_adopts_near_foe_scen99` | Nearest-foe adoption in the melee approach (#320; "Nearest-foe adoption in the melee approach (#320)") | (this PR) (added with the goldens) | `a4491684` | 2026-10-06, at `04611243` (row mirrored in `04611243`) | `cmp` exit 0 against the current game's dump (new arena; its bytes equal `multiplayer_two_teams_scen99`'s by construction) | New arena (added with the fix): the `multiplayer_two_teams` geometry read for the adoption; the thief ends at 5600 hp (6700 without adoption), the soldier at 7100 (7200). Tooth: the thief fact fails under the pin's `(void)near_foe;`. |
| — (zero goldens) | SwitchChar on the tick a seat claims a hero (#346) | (this PR) (`b1312009`) | none | — | The parity test driver never calls `sim_process_player_input` (`tests/parity/scenario_runtime.cpp:72-77`); its own `cycle_next_character` returns early on a null control and falls back to its held control, and the golden capture tool returns before the switch on a null or dead control (`tools/parity_dump_master.cpp:383-395`); the full capture at `04611243` `cmp`s equal to every golden without it. | Nothing in the corpus. Outside it: a SwitchChar press on the tick a seat auto-claims a hero now cycles from that hero instead of crashing the server (null entry) or re-seating the corpse and losing the press (dead entry); the old game drops the press on the null-entry tick and keeps the seat null for one frame. |
| — (zero goldens) | Weapon notices carry the HUD duration (#351) | (this PR) (`3816d724`) | none | — | Both recorders write a notification's `a` = `b` = 0 (`cee3b41f`; `parity_runner.cpp:256-268`), so the only bytes the fix changes never reach a dump. | Nothing in the corpus. Outside it: "Weapon sitting" and "Weapon N doing act random?" show for `STANDARD_TEXT_TIME` (75 cycles) like every other notice and like the old game's `do_notify`, instead of `family` cycles (12 for a glow, 14 for a wave). |

### Evidence for the #294 and #295 rows (2026-09-26, kept from the "Classic finder fixes" entry)

Before the port, the current game's fix was measured against the old game like this. Captures from the fixed source
`8a4ac0c47fd7f4d965baf38bda3f5735d6dec3fa` (the pre-squash of `966cbb55`) were compared with the 222-row before capture from merge base `b1412cab`.
A wrong-list probe kept the fixes but changed the shared shield/boomerang
call in `packs/core/lib/effect_shield.lua:27` back from `"weap"` to `"ob"`.
All 222 existing before and wrong-list dumps are **byte-identical**. Thus the
#293 native rename and #295 ally/hit-response fixes preserve the existing
corpus; the three changes in the table above require the #294 hostile-weapon fix *and* the
real weapon list. The fixed set has 224 captures: those 222 plus two new arenas.

The new `shield_projectile_absorb_scen99` and
`boomerang_projectile_absorb_scen99` each place a nearby hostile `ARROW`,
a distant hostile `ARROW` and a nearby friendly `FIRE_ARROW` in `weaplist`.
In each fixed dump the nearby hostile arrow is gone, the distant hostile and
friendly projectiles remain, and the guard loses 5 HP. The wrong-list probe
leaves both hostile arrows alive and the guard undamaged. Only the fourth fact
(index 3, hostile arrow count) fails there; all five facts pass with the
`"weap"` selector. The two new dumps keep the same events and RNG state across
that probe.

A forced clean rebuild with the retuned fact table reproduced all 224 fixed dumps byte for byte. The old game at
`e3f05b65`, before the port, with its table byte-identical to the current game's and its dumper rebuilt, produced
five dumps byte-identical to the wrong-list probe: it retained both hostile arrows in the new arenas and the old
outcomes in the three existing arenas. That is the classic behaviour the #294 port changes.

The exact caster-health facts move from 5200 to 5900 cents for
`special_soldier_2_scen99` and from 9500 to 10200 for
`weapon_boomerang_return_scen99`; their original summon-suppression and
missing-orbit-anchor mutations remain. The contact row keeps its 3600–3800
cent tower-health band and orbit-shrink mutation. The two new rows share the
wrong-list mutation described above. All five targeted canaries each flipped
one predicate and the gtest verdict, then returned to green after restoration;
none relied on the byte comparison alone.

Outside the parity corpus, the pinned pause/add-player/resume fight in
`GameLoop.midgame_add_player_resume_play_repause_keeps_transport_alive` ends
with 17 living actors instead of 12. Three isolated runs each reproduce 12
on the merge base, 17 with the fix, and 12 with only the guard list reverted.
Its exact census is updated; the RNG seeds, both surviving player seats,
re-teamed survivor and transport checks are preserved.

The #295 hit-response ordering also changes longer simulations outside the
parity corpus. On the merge base, moving only the retarget block ahead of the
yell reproduces the fixed branch's basketball results and both campaign
survival changes. Westlands L2 now keeps 3/4/4 crew members on seeds
42/1337/2025; all three runs kill the picket, cross mid-road by tick 986 and
sweep the road by tick 2623, within the unchanged 3000/8000 deadlines.
Long Season L9 keeps 7/8/8 mixed-crew members at tick 600. Its full 18-run
bracket (crew levels 4/5/6, three seeds, two rosters) still meets the original
curve-level defense requirement: at least four team-0 actors at tick 3000 on
every seed. The basketball replay pins retain a score within regulation on
all six courts and zero watchdog resets on the reference court. The updated
basketball test fails on the original engine and passes when only the retarget
ordering is changed.

The recalibrated Westlands check still fails when the F1 facing correction is
replaced with the old walk step. The new Ashfall Fair defense check fails with
only three defenders when team-0 attacks are suppressed; restoring attacks
returns it to green. Campaign layouts, crew levels and defense bands are
unchanged.

### #349 ports (2026-10-05)

The 70 rows that were Open on 2026-10-04 were closed by porting their fixes into the old game as one linear branch on
`ab0a7d2a`, one fix per commit (the message names the current-game commit it mirrors), plus two changes on the
current game's side: the archmage marker (a classic-restoring fix, below) and the parity test driver's score order
("Recorder and driver changes", below). Every old-game commit was measured additively: each id was captured with the
golden capture tool before and after it (`--all --out-dir`, `OG_PARITY_SCEN99_FSS` exported, no `using embedded
fixture` in the log), `diff -rq` named the ids it moves, and each moved id was `cmp`ed against its golden. No row
outside the 70 moved, apart from the 7 notification recaptures. The final capture at the pin `cmp`s equal to all 223
goldens.

This additive measurement in the old game supersedes the 2026-08-01 attribution, which was a subtractive bisection in
the current game (each candidate hunk reverted alone at a merge base). Both are true; they answer different questions.
The bisection found 14 rows that need both the corner-cut prune (M2) and the alignment assist (M3), because reverting
either one in the current game leaves the row red; in the old game M2 alone closes 8 of those 14, and only 6 need both.
The bisection text and every Open evidence cell are kept verbatim under History, "The Open list before #349", with a
dated pointer on each sentence this measurement refutes. The old list's "Rounding and animation" group has no rows
left: neither of its two rows is a rounding difference (the elf spread and the fireball step arithmetic are
bit-identical in both games), and the per-golden table below gives their real ports.

Labels used in both tables: **M1** melee snap-face (`fire_check` denial, gate reorder, `face_delta`, clinch breaker),
**M2** A* corner-cut prune, **M3** follow-path alignment assist, **M4** obmap interpenetration escape, **M5** shove
grid probe, **M6** A12b single `set_difficulty`, **A6/A7** teleport destination probe, **druid** PROTECTION top-up,
**score** score and kill semantics, **heal** heal and corpse notices ignore `heal_numbers`, **notif a/b** both recorders
write a notification's `a` = `b` = 0, **starburst cap** the runaway-specials 2.12 per-fireball add cap, **driver score
order** the parity test driver's score aggregate after the completion pair, **archmage fix** the current game's
`ANI_SPIN` marker with its table mirror.

| old-game commit | label: fix (issue; GAMEPLAY_FIXES row) | current-game commit | old-game site(s) | ids it moves (additive) | cmp result |
|---|---|---|---|---|---|
| `e4257a06` | **M2**: A* graph, no corner cutting past a blocked flank (#132; "Single-floor pathing wedges", rule 1) | `c409e7c8` | `src/walker.cpp` `Map::AdjacentCost`: a diagonal edge is emitted only when both orthogonal flank cells pass `query_grid_passable` for the path walker, x flank first. The current game's second flank test (a non-flyer over a `TYPE_AIR` tile) is not ported: the old game has no air tile, and on one floor the current test reduces to grid passability. | Alone on `ab0a7d2a`: 21 ids, all formerly Open. | 11 close alone; every id it moves `cmp`s equal at the pin. |
| `531f411f` | **M3**: follow-path slides to alignment at a convex corner (#132; "Single-floor pathing wedges", rule 2) | `c409e7c8` | `src/walker.cpp` `walker::follow_path_to_foe`: per-pixel, collision-checked slide toward grid alignment when a cardinal hop is blocked. The current game's Z-stair nudge (multi-floor) and its `current_game`/`world` null checks are not ported. | Alone on `ab0a7d2a` (a probe build): 26 ids, 3 close. On top of M2: 29 ids. | With M2: 20 close (the 11 M2 rows, the 3 M3 rows and the 6 rows that need both). |
| `61d982d5` | **M1**: melee snap-face (#132; "Guard-standoff melee deadlock (F1)", rules 1 and 3) | `c409e7c8` | `src/walker.h` (`FireCheckDenial`, `face_delta`); `src/walker.cpp` `walker::fire_check` (denial out-param; the reach gate now runs ahead of the `BIT_NO_RANGED` and magic-cost gates) and the new `walker::face_delta`; `src/stats.cpp` `COMMAND_ATTACK` and `facing_step_delta` | Alone on `ab0a7d2a`: 32 ids, all formerly Open. Instrumented: `face_delta` runs in 51 ids; the clinch breaker in none of 223. | 28 close alone. The gate reorder is NOT inert: an old-game build without it leaves 11 of the 28 different (marked in the per-golden table). |
| `8dfbbe61` | **M6**: generator spawns get one `set_difficulty`, not two (#132; "Generator spawn double `set_difficulty` (A12b)") | `c409e7c8` | `src/walker.cpp` `walker::create_weapon`, `ORDER_GENERATOR` branch: the `set_difficulty` call is gone; `fire()`'s single application at the rolled level is unchanged. The current game's `set_floor` and null return are not ported (one floor; the old `add_ob` never fails). | Alone on `ab0a7d2a`: 9 ids. | 7 close alone; `beast_set_difficulty_invariant_scen99` also needs M5 and `generator_tent_emission_scen99` also needs A6/A7. |
| `ec585ef3` | **M5**: the shove probes the injected walk against the grid before stealing the queue (#132; "Single-floor pathing wedges", rule 3) | `c409e7c8` | `src/living.cpp` `living::shove`, after the unchanged `random(3)` draw: `screen::query_grid_passable` on the target. The target is always a living, so the probe draws nothing in either game. | On top of M6: 2 ids, `beast_set_difficulty_invariant_scen99` and `mage_freeze_time_offteam_scen99`. | The beast row closes; the mage row closes with M2, M3 and the starburst cap. It does not move `effect_marker_emission_scen99` or `generator_saturation_scen99`. |
| `d6a705e7` | **A6/A7**: teleport destinations are probed eat-free under ground rules (#132; "Teleport destination probe eats treasures / obeys transient flight (A6/A7)") | `c409e7c8` | `src/walker.cpp`: new static `teleport_spot_blocked_by` and `teleport_landing_clear`, used by `walker::teleport` and `walker::teleport_ranged`. Not ported: floor choice and the fall-stories reset (multi-floor), the self-teleport tick stamp (CTF), the `sizez` gate (0 on one floor), and the 200-try bound on `teleport()`'s random loop (the draw sequence is identical up to 200 rejections). | On top of M6 and M5: 1 id, `generator_tent_emission_scen99` (its skeletons cast TUNNEL whenever no foe is near). | Closes it. Edge: translating the current game's marker path makes "Marker is Blocked!" reachable in the old game (its `distance = (... > 64)` stored a boolean); the notice goes through `do_notify`, but no parity row reaches it. |
| `283977dd` | **druid**: the PROTECTION top-up scans the weapon list for the circle (#146; "Druid PROTECTION top-up") | `19d7eeb3` | `src/walker.cpp` `walker::special`, druid case 4: `screen::find_in_range(level_data.weaplist, 100, newob)` with the unchanged owner/order/family filter. It skips dead walkers like `GameWorld::find_in_range`, which also skips dormant ones; no weapon is dormant. | Alone on `ab0a7d2a`: 1 id, `druid_protection_refresh_scen99`. | Closes it (recaptured later for notif a/b only). |
| `d64195c0` | **score**: score and kill semantics follow the attacker's team, not team 0 (#139; "Score and kill semantics follow the attacker's team") | `75a786a9` (its `walker_combat.cpp` hunk) | `src/walker.cpp` `walker::attack`: `getscore` is decided by the owner chain's head (`headguy->myguy` or `playerteam == save_data.my_team`, the value the golden capture tool sets from the scenario's `player_team`), and the `playerteam = 0` reset is gone. The old game's `m_score[team_num]` write stays unbounded where the current game's `award_score` drops a team past 3; no row can reach it (scoring needs the head's team to be `my_team` or a myguy). Not ported from `75a786a9`: the `is_friendly` rewrite and the SAVE_ALL protected side. | On top of druid: 1 id, `cleric_resurrect_friendly_scen99`. | Closes with M1. |
| `6f08dd15` | **score** (wording): named-NPC kill notice reads "ENEMY DEATH" only for a victim off the player's team (#188; "Score and kill semantics follow the attacker's team") | `0ed817cf` | `src/walker.cpp` `walker::attack`, kill arm. | None of 223 (`diff -rq` empty). | Inert by construction: the old game shows this notice through `viewob[0]->set_display_text`, which no capture records; the current game emits it as a sim Notification, and the parity test driver drops it by text (`tests/parity/parity_runner.cpp`: prefix "ENEMY DEATH: ", suffixes " DIED!", " Dispelled!", " Died!"). A new kill-notice wording outside that list would appear on the current game's side only. |
| `139b00b3` | **heal**: the cleric heal and orc corpse notices ignore `heal_numbers` (#105, #349; "Cleric heal and orc corpse notices ignore the heal-numbers setting") | `25eada99` | `src/walker.cpp`: the cleric's "Cleric healed N men!" and the orc's "<name> ate a corpse." `do_notify` calls are unconditional; `do_heal_effects` (render-only numbers) is unchanged. | Alone on `ab0a7d2a`: 2 ids, `orc_eat_corpse_scen99` and `special_cleric_heal_ally_scen99`. | Neither closes alone: both also need notif a/b, and the cleric row M1. |
| `cee3b41f` | **notif a/b**: notifications recorded as `a` = `b` = 0 (#349, recorder-only; "Notification metadata and level-end score order in the parity recorders") | the #349 PR (the parity test driver) | `src/screen.cpp` `screen::do_notify`, `OG_PARITY_RECORDER` block only. | On top of heal: 15 ids, every capture that holds a notification from a non-soldier notifier. | 4 close (`mage_teleport_marker_scen99`, `orc_eat_corpse_scen99`, `thief_charm_opponent_scen99`, `treasure_magic_potion_overfill_scen99`); 7 goldens that matched before differ only in `a`/`b` and were recaptured; 4 close with other ports (`archmage_teleport_marker_scen99`, `druid_protection_refresh_scen99`, `special_cleric_heal_ally_scen99`, `thief_taunt_matched_levels_scen99`). |
| `c1b22162` | **starburst cap**: the mage starburst's per-fireball MP add is capped at 40 (#132, runaway-specials 2.12; "Runaway specials: soft knees and MP-pool caps") | `c409e7c8` | `src/walker.cpp` `walker::special`, `FAMILY_MAGE` case 2: `generic` is clamped to 40 right after `generic / 15`, before the MP deduction, the damage add and the line-of-sight add. | Alone on `ab0a7d2a`: 1 id, `mage_freeze_time_offteam_scen99`. | Closes with M2, M3 and M5. |
| `c239807a` | **M4**: obmap interpenetration escape (#132; "Guard-standoff melee deadlock (F1)", rule 2) | `c409e7c8` | `src/obmap.cpp` `ob_pass_check`: a strictly separating move between two overlapped livings passes; `src/stats.cpp`: the M1 clinch-breaker comment points at it again. | None of 223 (`diff -rq` of the full captures before and after: empty). | Golden-inert; ported so both games run the same three F1 rules. |
| `1bafb5da` | **archmage fix** (table mirror, recorder-only; touches `tools/` only): the archmage marker row asserts the live marker (classic-restoring; "Archmage teleport marker died on its first tick") | the #349 PR (`tests/parity/scenario_table.h`) | `tools/parity_scenario_table.h`, byte-identical to the current game's table. | None of 223 (clean rebuild, `diff -rq` empty). | This commit is the pin. |

#### Per-golden attribution

One line per golden that was Open on 2026-10-04 or was recaptured by #349 (77). "Ports required" is the additive
measurement: the labels above that the row needs before it `cmp`s equal ("driver score order" and "archmage fix" are
current-game changes, and "notif a/b" pairs an old-game commit with a parity test driver change; every other label is
an old-game commit). "2026-08-01 / Open attribution" is what the Open list said, in one phrase; its evidence cells are
under History. Rows closed by `cmp` alone carry no further evidence here.

| id | ports required (additive) | 2026-08-01 / Open attribution | golden file changed in #349? | note |
|---|---|---|---|---|
| `archer_hit_response_backpedal_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no |  |
| `archmage_mind_control_team_flip_scen99` | notif a/b | not Open: matched the `ab0a7d2a` capture | yes: notification `a`/`b` only | no predicate reads a Notification's `a` or `b`; the byte compare carries the change |
| `archmage_teleport_marker_scen99` | M2/M3 (not separated further), notif a/b, archmage fix | R: notification `a`, plus the marker's `ani_type` bound | yes: live `FAMILY_MARKER`, 5 notifications, 95 `weapon_tracks` samples | classic-restoring (see Classic-restoring fixes); `WalkerOfTeamAlive(0,2,2)` fails against the old golden, in which the marker died on its first tick |
| `beast_set_difficulty_invariant_scen99` | M6+M5 | M6 (top table) | no | M6 fixes the golems' hp (318); M5 the emission count (2 golems, not 4) |
| `cleric_resurrect_friendly_scen99` | M1+score | score attribution plus the #132 cadence shift | no | the golden's score is `[8,27,0,0]` (the old cell's `[7,23]` was wrong); the "companion ~5-8 ticks ahead" event shift was the score attribution itself; M1 closes the rest (`rng_state`, cleric hp 83/84) |
| `consumable_inventory_state_scen99` | M1 | M1 alone (2026-08-01 M table) | no |  |
| `coverage_catchall_scen99` | notif a/b | not Open: matched the `ab0a7d2a` capture | yes: notification `a`/`b` only | no predicate reads a Notification's `a` or `b`; the byte compare carries the change |
| `druid_protection_refresh_scen99` | druid, then notif a/b | druid top-up | yes: notification `a` 13 to 0 only |  |
| `effect_bomb_bystander_scen99` | M1 | R: a bound-only RNG draw in the explosion's `get_base_damage` | no | the one differing bound is draw 132, `set_weapon_heading`'s waver on a TOWER1 probe arrow at tick 95 (5 old, 6 current): M1's `face_delta` at tick 94 gives the stationary tower (`stepsize` 0) a (0,0) heading, read as FACE_UP, so the probe is orthogonal and its step is scaled; ported literally, and the stepsize-0 heading is #350 |
| `effect_chain_fork_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no |  |
| `effect_chain_scen9410` | M2 | M2 alone | no |  |
| `effect_explosion_emission_scen99` | M3 | M3 alone | no | M3 alone also closes it on `ab0a7d2a` (probe); M2 leaves it unmoved |
| `effect_heartburst_multitarget_scen99` | M3 | M3 alone | no | M3 alone also closes it on `ab0a7d2a` (probe); M2 leaves it unmoved |
| `effect_knife_back_catch_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no |  |
| `effect_marker_emission_scen99` | M6 | M5 with M6 | no | M5 and A6/A7 do not move it in the old game |
| `effect_protection_emit_scen99` | notif a/b | not Open: matched the `ab0a7d2a` capture | yes: notification `a`/`b` only | no predicate reads a Notification's `a` or `b`; the byte compare carries the change |
| `elemental_death_starburst_scen99` | driver score order | R: `score_change` intra-tick order | yes: event order on tick 22 only | bytes equal the `ab0a7d2a` capture; no old-game commit |
| `elf_mega_rocks_volley_scen99` | M1 | `og.cosmetic_rand` spread rounding plus the F1 approach | no | needs M1's gate reorder: an old-game build without it leaves this row different; not rounding: the two games' rand streams split at draw 138 (tick 17), an extra `living::shove` `rng(3)` under the M1 approach, and the spread arithmetic is bit-identical |
| `elf_rocks_pair_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no | no rounding: the elf spread arithmetic is bit-identical in both games |
| `event_end_game_emission_scen99` | M1 | M7 (moot); any M1-M3 share not separated | no | M3 moves it without closing it; M1 alone closes it |
| `event_notification_emission_scen99` | M1 | M7 (moot); any M1-M3 share not separated | no | M3 moves it without closing it; M1 alone closes it |
| `event_request_redraw_emission_scen99` | M1 | M7 (moot); any M1-M3 share not separated | no | M3 moves it without closing it; M1 alone closes it |
| `event_set_end_emission_scen99` | M1 | M7 (moot); any M1-M3 share not separated | no | M3 moves it without closing it; M1 alone closes it |
| `event_set_palette_emission_scen99` | M1 | M7 (moot); any M1-M3 share not separated | no | M3 moves it without closing it; M1 alone closes it |
| `family_archer_scen99` | M2 | M2+M3, both required | no | M3 on top leaves it equal (the bisection's 'both required' is subtractive) |
| `family_archmage_scen99` | M2+M3 | M2+M3, both required | no |  |
| `family_barbarian_scen99` | M2+M3 | M2+M3, both required | no |  |
| `family_big_orc_scen99` | M2+M3 | M2+M3, both required | no |  |
| `family_cleric_scen99` | M2 | M2+M3, both required | no | M3 on top leaves it equal (the bisection's 'both required' is subtractive) |
| `family_druid_scen99` | M2+M3 | M2+M3, both required | no |  |
| `family_elf_scen99` | M2 | M2+M3, both required | no | M3 on top leaves it equal (the bisection's 'both required' is subtractive) |
| `family_fireelemental_scen99` | M2 | M2+M3, both required | no | M3 on top leaves it equal (the bisection's 'both required' is subtractive) |
| `family_ghost_scen99` | M1 | M1 alone (2026-08-01 M table) | no | needs M1's gate reorder: an old-game build without it leaves this row different |
| `family_golem_scen99` | M2 | M2 alone | no |  |
| `family_mage_scen99` | M2 | M2+M3, both required | no | M3 on top leaves it equal (the bisection's 'both required' is subtractive) |
| `family_medium_slime_scen99` | M2 | M2+M3, both required | no | M3 on top leaves it equal (the bisection's 'both required' is subtractive) |
| `family_orc_scen99` | M1+M2+M3 | M1+M2+M3 | no |  |
| `family_skeleton_scen99` | M2+M3 | M2+M3, both required | no |  |
| `family_slime_scen99` | M2 | M2+M3, both required | no | M3 on top leaves it equal (the bisection's 'both required' is subtractive) |
| `family_small_slime_scen99` | M1+M2+M3 | M1+M2+M3 | no | needs M1's gate reorder: an old-game build without it leaves this row different |
| `family_soldier_scen99` | M2 | M2+M3, both required | no | M3 on top leaves it equal (the bisection's 'both required' is subtractive) |
| `family_thief_scen99` | M2+M3 | M2+M3, both required | no |  |
| `fireelemental_starburst_ring_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no |  |
| `generator_bones_emission_scen99` | M6 | M6 (top table) | no |  |
| `generator_owner_cascade_scen99` | M6 | M6 (top table) | no |  |
| `generator_saturation_scen99` | M6 | M5 with M6 | no | M5 and A6/A7 do not move it in the old game |
| `generator_tent_emission_scen99` | M6+A6/A7 | M6 alone | no | its skeletons cast TUNNEL (`teleport_ranged`) whenever no foe is near, so the destination probe decides where they land; M6 alone fixes every hp, A6/A7 the emission ticks and positions |
| `generator_tower_emission_scen99` | M6 | M6 alone | no |  |
| `generator_treehouse_emission_scen99` | M6 | M6 alone | no |  |
| `invulnerable_potion_scen99` | M1 | M1 alone (2026-08-01 M table) | no |  |
| `mage_freeze_time_offteam_scen99` | M2+M3+M5+starburst cap | fireball step rounding | no | not rounding: M2 and M3 change an orc's path by 1 px at tick 98, M5 removes two shove draws at tick 162, and the level-15 mage's 900 MP binds the starburst cap at tick 271 (first-step fireball line of sight 24 capped, 27 uncapped); M1 plays no part |
| `mage_heartburst_multitarget_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no | needs M1's gate reorder: an old-game build without it leaves this row different |
| `mage_starburst_ring_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no |  |
| `mage_teleport_marker_scen99` | notif a/b | R: notification `a` | no |  |
| `multiplayer_two_teams_scen99` | M1 | M1 alone (2026-08-01 M table) | no |  |
| `orc_eat_corpse_scen99` | heal+notif a/b | R: the heal_numbers gate | no | two mechanisms: the gate, and the notice's `a` (14, the orc's family) |
| `smoke_nonempty_scen99` | M1 | M1 alone (2026-08-01 M table) | no | needs M1's gate reorder: an old-game build without it leaves this row different |
| `special_archmage_4_scen99` | notif a/b | not Open: matched the `ab0a7d2a` capture | yes: notification `a`/`b` only | no predicate reads a Notification's `a` or `b`; the byte compare carries the change |
| `special_archmage_scen123` | M2 | M2 alone | no |  |
| `special_cleric_heal_ally_scen99` | M1+heal+notif a/b | #132 M1 and/or M2/M3, plus the heal_numbers gate (R) | no | M1 alone closes every walker, hp, position, weapon and `rng_state` field; heal adds the "Cleric healed 1 man!" notice at tick 60; notif a/b zeroes its `a` |
| `special_druid_2_scen99` | M1 | M1 alone (2026-08-01 M table) | no |  |
| `special_mage_scen126` | M6 | M6 (top table) | no |  |
| `special_slime_1_scen99` | M1 | M1 alone (2026-08-01 M table) | no | needs M1's gate reorder: an old-game build without it leaves this row different |
| `special_thief_3_scen99` | notif a/b | not Open: matched the `ab0a7d2a` capture | yes: notification `a`/`b` only | no predicate reads a Notification's `a` or `b`; the byte compare carries the change |
| `thief_charm_opponent_scen99` | notif a/b | R: notification `a` | no |  |
| `thief_taunt_matched_levels_scen99` | M1, then notif a/b | #132 M1 and/or M2/M3 (top drift table) | yes: notification `a` 11 to 0 only | needs M1's gate reorder: an old-game build without it leaves this row different; [Correction (2026-10-06): the decoy fired NO arrow in either game at this pin: `face_delta` set `lastx`/`lasty` to the delta times a `stepsize` of 0, so every snap-face left a (0,0) heading and the next probe went FACE_UP and missed. #350 fixed that in both games and this golden was recaptured (and the row reshaped); see "Sim-correctness ports".] |
| `treasure_drumstick_pickup_scen99` | M1 | M1 alone (2026-08-01 M table) | no |  |
| `treasure_gold_bar_team_reject_scen99` | M1 | M2/M3 pathing, M1 excluded by construction | no | needs M1's gate reorder: an old-game build without it leaves this row different; near the end of the chase the orc enters `COMMAND_ATTACK` inside `PATHING_MIN_DISTANCE`, and the snap-face holds it at y 181; M2 and M3 leave the capture byte-identical to `ab0a7d2a` |
| `treasure_key_team1_silent_scen99` | M1 | inherited from the gold-bar row (M2/M3) | no | needs M1's gate reorder: an old-game build without it leaves this row different; same mechanism as the gold-bar row, measured on its own |
| `treasure_magic_potion_overfill_scen99` | notif a/b | R: notification `a`, residual of the #231 port | no | the #231 half was ported on 2026-10-04 (`edb8550c`) |
| `treasure_stain_pickup_scen99` | M1 | M1 alone (2026-08-01 M table) | no |  |
| `undead_no_corpse_raise_scen99` | driver score order | R: `score_change` intra-tick order | yes: event order on tick 24 only | bytes equal the `ab0a7d2a` capture; no old-game commit |
| `weapon_boulder_emission_scen99` | M3 | M3 alone | no | M3 alone also closes it on `ab0a7d2a` (probe); M2 leaves it unmoved |
| `weapon_boulder_explode_damage_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no | needs M1's gate reorder: an old-game build without it leaves this row different |
| `weapon_circle_protection_follow_scen99` | notif a/b | not Open: matched the `ab0a7d2a` capture | yes: notification `a`/`b` only | no predicate reads a Notification's `a` or `b`; the byte compare carries the change |
| `weapon_sprinkle_freeze_scen99` | M1 | #132 M1 and/or M2/M3 (top drift table) | no | needs M1's gate reorder: an old-game build without it leaves this row different |
| `weapon_wave2_emission_scen99` | notif a/b | not Open: matched the `ab0a7d2a` capture | yes: notification `a`/`b` only | no predicate reads a Notification's `a` or `b`; the byte compare carries the change |

#### Recorder and driver changes

Two recorder differences closed rows without any gameplay port.

- **Notification `a`/`b` (notif a/b).** The old recorder wrote a Notification's notifier family and team into `a`/`b`
  (`screen::do_notify`); the current game's notification event has no notifier and puts the HUD display duration in
  `a` (`SimEventLog::push_notification`), and the parity test driver used to rewrite `a`/`b` by text for three classic
  texts. No text table can satisfy the corpus (the same "Teleport Marker Placed" carries family 3 from a mage and 17
  from an archmage; a potion text carries the eater's family). Both sides now write `a` = `b` = 0: the old game in
  `cee3b41f`, the parity test driver in the #349 PR (`tests/parity/parity_runner.cpp`, which no longer keeps the
  text table). No predicate reads a Notification's `a` or `b` (`tests/parity/fact_predicate.h` has only
  `EventKindAtLeast`/`EventKindExactly` for events), so the byte compare carries the change. 9 goldens were recaptured
  for it, `a`/`b` being the only bytes that changed: `archmage_mind_control_team_flip_scen99`,
  `coverage_catchall_scen99`, `druid_protection_refresh_scen99`, `effect_protection_emit_scen99`,
  `special_archmage_4_scen99`, `special_thief_3_scen99`, `thief_taunt_matched_levels_scen99`,
  `weapon_circle_protection_follow_scen99` and `weapon_wave2_emission_scen99`.
- **Score on a level-ending tick (driver score order).** The old recorder synthesizes a tick's `score_change` from an
  end-of-tick `m_score` snapshot (`tools/parity_dump_master.cpp`), after `screen::endgame` has recorded `end_game` and
  `set_end` inside the tick; the current game emits `ScoreChange` at the award site, which the parity test driver folds
  into one per-tick aggregate. The parity test driver now emits that aggregate after the tick's completion pair, so a
  level-ending tick reads `end_game, set_end, score_change` on both sides. This is the driver side, not an old-game
  recorder change, because the old game can also end a level in the middle of `act()` (an exit pad eaten during the
  object loop, `src/treasure.cpp`; a SAVE_ALL failure, `src/walker.cpp`): a score earned later in that tick still lands
  after the completion pair in the old recorder, and an end-of-tick aggregate placed after the pair matches it in every
  case. No old-game commit; the two goldens it moves, `undead_no_corpse_raise_scen99` and
  `elemental_death_starburst_scen99`, were recaptured from the old game, and their bytes equal the `ab0a7d2a` capture.

### Sim-correctness ports (#320, #350; 2026-10-06)

Two current-game fixes were ported into the old game as one linear branch on `1bafb5da`, one fix per commit (the
message names the current-game commit it mirrors), measured additively like the #349 ports: the capture at
`1bafb5da` `cmp`s equal to every golden; `diff -rq` against the capture after the #350 port named exactly 3 ids;
`diff -rq` against the capture after the #320 port on top named exactly 2 more; the table mirror then added 2 ids and
reshaped 1 (`thief_taunt_matched_levels_scen99`). Every capture log had no `using embedded fixture` line. Each moved id
`cmp`s equal to the current game's dump with both fixes in place (the reshaped row on its old spec before the reshape
and on its new spec after it), and no row outside those moved.

The current game's two other fixes in the same batch, #346 (SwitchChar on the claim tick) and #351 (weapon notice
duration), are not ported and move no golden; the per-golden table under "Ported fixes" says why for each.

| old-game commit | label: fix (issue; GAMEPLAY_FIXES row) | current-game commit | old-game site(s) | ids it moves (additive) | cmp result |
|---|---|---|---|---|---|
| `cd97028a` | **tower heading**: a stationary shooter keeps a heading toward the foe after a snap-face (#350; "Stationary shooter heading after a snap-face (#350)") | `cb3dacc9` (this PR) | `src/walker.cpp` `walker::face_delta`: `lastx`/`lasty` are the delta times the stepsize, or times 1 when the stepsize is 0; for the only stepsize-0 shooter (TOWER1) that is the unit delta instead of (0,0). | Alone on `1bafb5da`: 3 ids, `effect_bomb_bystander_scen99`, `effect_knife_back_catch_scen99`, `thief_taunt_matched_levels_scen99`. | All three `cmp` equal to the current game (the thief_taunt row on its old spec; it was then reshaped and recaptured at `04611243`). |
| `a4491684` | **near-foe adoption**: `walk_to_foe`'s melee branch adopts the strictly nearer foe `find_near_foe` returns and aims this tick at it (#320; "Nearest-foe adoption in the melee approach (#320)") | `9a38ee54` (this PR) | `src/stats.cpp` `statistics::walk_to_foe`: the find runs ahead of the turn and the `random(25)` ATTACK draw; the result is adopted when `distance_to_ob` reads strictly under the held foe's; the dead `firstfoe` fallback (unreachable in both games) is gone. | On top of `cd97028a`: 2 ids, `multiplayer_two_teams_scen99`, `special_thief_2_scen99`. | Both `cmp` equal. |

`04611243` is the table mirror (recorder-only, `tools/` only) and the pin.

### Earlier ports (before the 2026-10-04 rule)

The old game already carried deliberate fixes before the rule was written down. The e761 rebuild itself changed `src/`
for the rows of `docs/GAMEPLAY_FIXES_FROM_CLASSIC.md` whose "Side fixed" is `parity-companion`. Recorder-only commits
(table mirrors, the golden capture tool) touch `tools/` only and have no row here.

| old-game commit | what it changed in the old game | current-game counterpart | goldens |
|---|---|---|---|
| `a2d9d470` | The e761 rebuild (2026-06-09): the recorder, plus the `parity-companion` rows of `docs/GAMEPLAY_FIXES_FROM_CLASSIC.md` | — | All of them (the baseline) |
| `4e1af2a1` | "Fix classic recorder gameplay bugs" (2026-06-09): the render-loop `drawcycle` bump in `effect::act` and `living::act`; weapon attacks stop feeding `exp` | — | Captured with it from the start |
| `a2714ec5` | The #178 generator `max_hitpoints` fix, `walker::set_difficulty`'s `ORDER_GENERATOR` arm | #178 (`docs/GAMEPLAY_FIXES_FROM_CLASSIC.md`, "Generators run with `hp > max_hp` (#178)") | None move |
| `a6ee4b08`, `15faf83e`, `268097e1`, `8d98597e` | The #266 armor roll: the 2002 `random(armor)` expectation in `get_damage_reduction`, nearest-point hit rounding, the rounded hit into exp, score and weapon self-damage, and Jonathan Dearborn's 2013 clamp comment restored | `81bd6036` (`21f2606a`) | History, "Armor-roll expectation recapture" |

## Classic-restoring fixes

A current-game deviation from the original game, fixed in the current game. The golden was always (and is again) an
old-game capture.

### SwitchChar claim timing (W5-2, 2026-10-03)

The original game claims the switched-to hero in `continuous_input()` in the
same frame as the TAB press (`glad.cpp:316`, `view.cpp:887-892`), and the
golden capture tool does the same (`tools/parity_dump_master.cpp:349`). The
current game used to claim it one `sim_process_player_input` call late, so the
new hero took one more AI act on the switch tick. That was a deviation from
the original game, not a deliberate change, and it is fixed: the game claims
in the same call through `sim_claim_control` (commit `6042d7ab`, "fix(sim):
SwitchChar claims the new hero in the same call, as the original game does"),
and the parity test driver claims through the same function.

| id | what changed | golden captured from |
|---|---|---|
| `input_switch_char_scen99` | Nothing against the original game: for a short time (W5-PARITY, `d3338e68`) this golden was a current-game dump encoding the late claim; it is captured from the old game again and is byte-identical (`cmp`) to the golden at `afb651fe`. | The old game (`capture_master_golden.sh input_switch_char_scen99`). |

### Archmage teleport marker (#349, 2026-10-05)

The archmage's teleport marker died on its first tick in the current game, in real play and not only in parity.
`packs/core/families/living-17-archmage.lua` set the marker's `ani_type` to a raw `2`, copied from the 2002 C++, which
indexes past the marker family's 16-row animation table; the current game's `ani_count` guard (`effect::animate`)
reset it to `ANI_WALK`, and `effect::act` reaped the marker on the next tick. The 2002 code reads past the table
unbounded (in the linked old-game binary, into the next table, `anicloud`), so the marker survives there: a second
cast removes it with "(Old Marker Removed)" and the return teleport lands on it. The fix places the marker with
`ANI_SPIN`, as `mage.lua` already does for the same body; its frames cycle `marker_cycle` instead of classic's
out-of-bounds `cloud_cycle` frames, which no dump records. The row's drift sentinel, `WalkerOfTeamAlive(0,1,1)`, was
written to fail the day the marker survives; it is `WalkerOfTeamAlive(0,2,2)` now, in the current game's
`tests/parity/scenario_table.h` and in the old game's byte-identical mirror (`1bafb5da`, the pin). Rejected:
copying the guard into the old game (it would write the current game's bug into classic) and growing the table to 24
rows (it would encode one binary's link order as data).

| id | what changed | golden captured from |
|---|---|---|
| `archmage_teleport_marker_scen99` | The marker is alive: a `FAMILY_MARKER` walker, five notifications including "(Old Marker Removed)" at tick 80, and 95 `weapon_tracks` samples (2 in the old golden). Its notifications carry `a` = 0 (notif a/b), and the #132 pathing ports set the far soldier's position. Tooth: `WalkerOfTeamAlive(0,2,2)` fails against the old golden. | The old game at `1bafb5da` (`capture_master_golden.sh archmage_teleport_marker_scen99`). |

## Open: goldens that are not yet old-game captures

Closed by #349 (2026-10-05): a full capture at `1bafb5da` `cmp`-equals all 223 goldens (`diff -rq` against
`tests/parity/golden` is empty). No golden is open, and the list may only shrink: a deliberate departure lands with its
old-game port and its recapture, never as a new row here. The 70 rows this section listed, with their 2026-08-01
attribution and evidence, are kept under History, "The Open list before #349"; how each one closed is in Ported fixes,
"#349 ports".

### Closed

Rows of the old drift table and M list that now match the old game byte for byte (closed 2026-10-04; measured by the
C5 recon's `cmp` of W5-PARITY's full old-game capture against the goldens at `83c3f1c0`):

- `weapon_circle_protection_follow_scen99` (drift table, the protected friendly's idle wander): the golden `cmp`s equal to the old-game capture.
- `effect_protection_emit_scen99` (M4 with M7): the tables are byte-identical now, and the golden `cmp`s equal to the old-game capture.
- `smoke_empty_scen99`: the golden was deleted on 2026-09-08 (History, "Removed goldens"); the row is `CompareMode::Invariant` and has no reader.

Closed 2026-10-05 by #349, measured by a full capture at `1bafb5da` `cmp`ed against every golden:

- All 70 rows of the former Open list: the freeze-time census residual (`treasure_magic_potion_overfill_scen99`), the druid PROTECTION top-up (`druid_protection_refresh_scen99`), the 43 rows of the #132 single-floor table and the 25 rows of the top drift table. Each one's ports are in the per-golden table under "#349 ports"; 3 of them (`archmage_teleport_marker_scen99`, `druid_protection_refresh_scen99`, `thief_taunt_matched_levels_scen99`) were recaptured from the old game, 2 (`undead_no_corpse_raise_scen99`, `elemental_death_starburst_scen99`) were recaptured with bytes equal to the `ab0a7d2a` capture, and the other 65 goldens did not change.

## History

The sections below record earlier waves as they were measured. Their text is kept; where they say a golden was
"re-blessed from the branch dump", that was the practice the 2026-10-04 rule retired.

## Armor-roll expectation recapture (2026-09-08)

**The change.** `docs/GAMEPLAY_FIXES_FROM_CLASSIC.md`, row "Damage reduction
clamped high-armor targets to 1 damage per hit (#266)": branch commits
`7d33bf3f` (damage reduction is the exact expectation of the 2002
`random(armor)` roll, `src/core/combat_math.cpp` `compute_damage_reduction`)
and `21f2606a` (the float hit becomes hitpoints as the nearest point, halves
up — `damage_to_hit_points`, used once at `src/gameplay/walker_combat.cpp:320`;
that one `tempdamage_i` then feeds the hit, the attack exp, every score award
and a weapon's self-damage). `5561272c` (the Nuthram thief back to level 8)
is level data and parity-invisible. Companion (`parity-companion`) mirror:
`a6ee4b08` (`get_damage_reduction`, operation for operation), `15faf83e`
(nearest-point rounding at the `do_combat_damage` call) and `268097e1` (the
same rounded short into `exp_from_action`, the three `m_score +=` sites and
`stats->hitpoints -= ...` for weapons — the first rounding mirror had left the
float flowing into those, so weapon hits scored one point short of the branch
and `special_archmage_3_scen99`'s elemental weapon died on a different hit).
Blast radius of each companion step was measured by capturing all 220
comparable rows before and after: `15faf83e` → `268097e1`-score-sites moved
33 rows, the self-damage integer moved exactly one, and every row either step
moved is a row the branch's own change had already moved.

**Procedure.** (1) Table consistency: the branch `tests/parity/scenario_table.h`
and the companion `tools/parity_scenario_table.h` are not byte-identical (the
branch has added rows and retuned facts since the last mirror), so for every
moved id the sim-relevant blocks — the `ScenarioSpec` row minus its
`kFacts_`/`kMut_` pointers, `kInputs*`, `kFamilySpawns*` — were compared
comment-stripped: identical for all 133 ids (the dumper reads only
`scenario_file`/`rng_seed`/`inputs`/`spawns`/`player_team`/`fresh_arena`/
`tick_budget`; nine ids differ in facts *bodies*, which the companion never
evaluates). (2) Branch `parity_runner_smoke` dumps of all 220 rows at
`21f2606a`; companion `parity_dump_master` captures of all 220 at `268097e1`;
staleness canary (`tick` == budget) clean on every dump. (3) **Pre-change
adjudication**: a worktree at `6f361230` (the commit before the change) was
built and dumped for all 133 rows below; every one of them matches its
committed golden semantically (`diff_dumps.py`), so none of these moves
pre-dates the change. (4) Source rule: a row with its own ledger row (drift
table, M1–M7 list, intentional-changes table) is re-blessed from the branch
dump; every other row takes the companion capture, and only if the branch
dump matches it semantically. (The 2026-10-04 rule retires this source rule: each of the 40 "branch dump" rows is now either closed by a C5 port or listed under Open.) (5) Facts: every predicate the change broke was
shifted by the measured delta with its width and shape kept (exact pins stay
exact); two count facts whose subject now dies were redesigned (below). Every
retune is an in-place line edit, so no `kMut_*` pin moved (217 anchors valid).

**The gate saw 65, the change moved 133.** `og_test_parity`'s SemanticParity
contract evaluates the row's predicates on both dumps and byte-compares
`weapon_tracks`; it never compares raw `walkers[].hp` or `score_per_team`.
So the pre-recapture run flagged only the 65 rows whose facts pin the moved
value or whose tracks moved; 68 more goldens moved by the `diff_dumps.py`
standard (an hp or score the facts leave unpinned) and were green. All 133
are listed; the column "gate" says which kind each was. Recapturing the
gate-green 68 changes no verdict today, but leaves the goldens describing
what the companion actually captures instead of a state neither arm produces.

**Counts.** 92 goldens from the companion capture, 40 re-blessed from the
branch dump (ledger rows), 1 not blessed.

**Not blessed — `effect_chain_emission_scen99`.** Branch and companion
disagree after the change on the third chain kill at tick 27 only:
`score_change` 205 (branch) vs 202 (companion), `score_per_team[0]` 500 vs
497, and the surviving SOLDIER on 36 vs 37 of 120; walkers, tracks and every
other event are identical, and both arms moved off the old golden by the same
damage signature (ARCHMAGE 73 → 66). Before the change the arms agreed
byte-for-byte on this row. The residual is the chain fork's damage arithmetic
(`packs/core/lib/effect_chain.lua` computes `fork_damage` as a float product
of the parent's damage; the companion's `effect.cpp` chain does its own),
which this wave did not bisect and which the ledger documents for no other
row. The old golden is kept (the row stays green: its facts hold on both
sides and the tracks are equal) and the row is reported for adjudication
rather than blessed from either arm.

**→ Resolved 2026-09-08**, see "Golden byte-compare (2026-09-08)" below: the
residual was a port regression in `packs/core/lib/effect_chain.lua`, fixed on the
branch; the golden is now a plain companion capture.

**Redesigned facts.** `weapon_bone_emission_scen99`: the caster SKELETON used
to end on exactly 1 hp (the old clamp's one-point floor kept it alive); under
the expectation it dies at tick 130, so `WalkerFamilyCount(FAMILY_SKELETON,
1, 1)` became `WalkerDiedByFinal(FAMILY_SKELETON)` — non-vacuous because the
row's `WeaponFamilyEmitted(FAMILY_BONE)` proves the caster existed and fired —
and the `play_sound` floor moved 35 → 31 (observed 36 → 32, floor kept one
below). `weapon_boulder_emission_scen99` (an M3 ledger row): the GIANT_SKELETON
used to end on 5 hp and now dies at tick 145; `WalkerFamilyCount(...GIANT_SKELETON,
1, 1)` became `WalkerDiedByFinal(FAMILY_GIANT_SKELETON)` on the same
argument (`WeaponFamilyEmitted(FAMILY_BOULDER)` holds). `special_skeleton_1_scen99`:
the skeleton dies at tick 52 instead of 60, one CLANG fewer, so the
`play_sound` floor moved 7 → 6 (exact count both times).

**Mutation canary (teeth) on retuned rows.** `scripts/parity/run_mutation_canary.sh --scenario <id>` on eight retuned rows after the recapture, each restoring the tree cleanly: `combat_attack_scen99` 2 flips (#3 WalkerHpRangeAtFinalTick, gtest); `archer_hit_response_backpedal_scen99` 3 (#3 WalkerPositionMoved, #4 WalkerHpRangeAtFinalTick, gtest); `cleric_turn_undead_scen99` 5 (#3 WalkerDiedByFinal, #4 WalkerOfTeamAlive, #5 LevelDoneEquals, #6 WalkerHpRangeAtFinalTick, gtest); `effect_explosion_ally_tier_scen99` 2 (#2 WalkerHpRangeAtFinalTick, gtest); `weapon_bone_emission_scen99` 4 (#2 EventKindAtLeast, #5 WeaponSpeed, #6 WeaponNetTravel, gtest); `special_skeleton_1_scen99` 1 (gtest only — the mutation keeps the skeleton alive, which the row reads through its golden-side comparison rather than a branch-side predicate flip in this run); `weapon_ranged_impact_hp_scen99` 2 (#2 WalkerHpRangeAtFinalTick, gtest); `orc_yell_stun_hold_scen99` 5 (#1 WalkerFamilyCount, #3 WalkerPositionMoved, #4 WalkerHpRangeAtFinalTick, #5 EventKindExactly, gtest). Zero rows with zero flips. The other retuned rows were not canaried in this wave; their pins are anchor-valid (217/217) and their bounds moved by measured deltas with the same width, which is not a substitute for a flip run.

| id | new golden source | gate | what moved (old golden → new) | facts retuned |
|---|---|---|---|---|
| `ai_idle_wander_scen9301` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 46->29 | — |
| `archer_fire_arrows_ring_scen99` | companion capture (`268097e1`) | gate-red | hp: ELF 69->68; score [7, 0, 0, 0]->[8, 0, 0, 0]; events: score_change.bx1 | `WalkerHpRangeAtFinalTick(FAMILY_ELF, 6900, 6900)` -> `WalkerHpRangeAtFinalTick(FAMILY_ELF, 6800, 6800)`; label text updated; `ScoreDelta(0, 7, 7)` -> `ScoreDelta(0, 8, 8)` |
| `archer_hit_response_backpedal_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | hp: SOLDIER 109->107, ARCHER 30->26; score [6, 0, 0, 0]->[7, 0, 0, 0]; events: score_change.bx1 | `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3000, 3000)` -> `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 2600, 2600)`; label text updated |
| `archmage_mind_control_team_flip_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ARCHMAGE 147->146 | — |
| `archmage_summon_elemental_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ARCHMAGE 147->146; score [339, 0, 0, 0]->[341, 0, 0, 0]; events: score_change.bx1 | — |
| `cleric_raise_ghost_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: CLERIC 117->116, BIG_ORC 174->173; score [44, 0, 0, 0]->[45, 0, 0, 0]; events: score_change.bx1 | — |
| `cleric_raise_skeleton_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: CLERIC 117->116, BIG_ORC 174->173; score [44, 0, 0, 0]->[45, 0, 0, 0]; events: score_change.bx1 | — |
| `cleric_resurrect_friendly_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | hp: BIG_ORC 79->70, CLERIC 91->84; score [7, 23, 0, 0]->[8, 27, 0, 0]; events: score_change.bx5 | `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 9000, 9200)` -> `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 8300, 8500)`; label text updated |
| `cleric_turn_undead_scen99` | companion capture (`268097e1`) | gate-red | hp: CLERIC 108->105 | `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 10700, 10900)` -> `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 10400, 10600)`; label text updated |
| `combat_attack_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 63->50 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 5000)` |
| `consumable_inventory_state_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 47->46, ARCHER 70->69 | — |
| `coverage_catchall_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: GOLEM 217->199 | — |
| `effect_bomb_emission_scen99` | companion capture (`268097e1`) | gate-red | pos: SOLDIER; events 15->13; rng_state; weapon_tracks | — |
| `effect_boomerang_contact_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 103->98, TOWER1 41->37; score [93, 0, 0, 0]->[97, 0, 0, 0]; events 17->16; rng_state; weapon_tracks; weapons 3->2 | `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 4000, 4200)` -> `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 3600, 3800)`; label text updated |
| `effect_boomerang_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: SOLDIER 111->109 | — |
| `effect_chain_emission_scen99` | **not blessed** — old golden kept → resolved 2026-09-08, see "Golden byte-compare" | gate-green (hp not pinned) | hp: ARCHMAGE 73->66, SOLDIER 37->36; score [496, 0, 0, 0]->[500, 0, 0, 0]; events: score_change.bx2 | — |
| `effect_chain_fork_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: ARCHMAGE 116->113; score [281, 0, 0, 0]->[282, 0, 0, 0]; events: score_change.bx1 | — |
| `effect_chain_scen9410` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: MAGE 87->86 | — |
| `effect_cloud_emission_scen99` | companion capture (`268097e1`) | gate-red | hp: THIEF 60->57 | `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 6000, 6000)` -> `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 5700, 5700)` |
| `effect_door_open_emission_scen99` | companion capture (`268097e1`) | gate-red | pos: SOLDIER; events 10->9; rng_state; weapon_tracks | — |
| `effect_expand_emission_scen99` | companion capture (`268097e1`) | gate-red | pos: SOLDIER; events 10->9; rng_state; weapon_tracks | — |
| `effect_explosion_ally_tier_scen99` | companion capture (`268097e1`) | gate-red | hp: TOWER1 85->84 | `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 8500, 8500)` -> `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 8400, 8400)`; label text updated |
| `effect_explosion_emission_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: ARCHMAGE 138->135 | — |
| `effect_explosion_range_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | score [333, 0, 0, 0]->[335, 0, 0, 0]; events: score_change.bx1 | — |
| `effect_ghost_scare_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: GHOST 46->45 | — |
| `effect_heartburst_multitarget_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: ARCHMAGE 138->135 | — |
| `effect_hit_emission_scen99` | companion capture (`268097e1`) | gate-red | pos: SOLDIER; events 10->9; rng_state; weapon_tracks | — |
| `effect_knife_back_catch_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | hp: SOLDIER 75->62, TOWER1 93->85; score [45, 0, 0, 0]->[53, 0, 0, 0]; events: score_change.bx8 | `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 9200, 9400)` -> `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 8400, 8600)`; label text updated |
| `effect_knife_back_emission_scen99` | companion capture (`268097e1`) | gate-red | pos: SOLDIER; events 10->9; rng_state; weapon_tracks | — |
| `effect_magic_shield_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: CLERIC 115->114 | — |
| `effect_poison_cloud_emit_scen99` | companion capture (`268097e1`) | gate-red | hp: THIEF 60->57 | `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 6000, 6000)` -> `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 5700, 5700)` |
| `effect_shield_absorb_scen99` | companion capture (`268097e1`) | gate-red | hp: TOWER1 53->49; score [81, 0, 0, 0]->[85, 0, 0, 0]; events: score_change.bx4 | `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 5200, 5400)` -> `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 4800, 5000)`; label text updated |
| `elemental_death_starburst_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: FIREELEMENTAL -23->-26; score [44, 0, 0, 0]->[45, 0, 0, 0]; events: score_change.bx1 | — |
| `enemy_freeze_mage_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: MAGE 24->23 | — |
| `event_end_game_emission_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 60->56, ORC 67->63; score [5, 0, 0, 0]->[6, 0, 0, 0]; events: score_change.bx1 | — |
| `event_notification_emission_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 60->56, ORC 67->63; score [5, 0, 0, 0]->[6, 0, 0, 0]; events: score_change.bx1 | — |
| `event_request_redraw_emission_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 60->56, ORC 67->63; score [5, 0, 0, 0]->[6, 0, 0, 0]; events: score_change.bx1 | — |
| `event_set_end_emission_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 60->56, ORC 67->63; score [5, 0, 0, 0]->[6, 0, 0, 0]; events: score_change.bx1 | — |
| `event_set_palette_emission_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 60->56, ORC 67->63; score [5, 0, 0, 0]->[6, 0, 0, 0]; events: score_change.bx1 | — |
| `family_archer_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 117->116 | — |
| `family_barbarian_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 96->95 | — |
| `family_big_orc_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 116->115 | — |
| `family_medium_slime_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 10->4 | — |
| `family_orc_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 99->98 | — |
| `family_soldier_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: ELF 58->57 | — |
| `generator_owner_cascade_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | walkers 20->18; hp: BIG_ORC 92->98, TENT -97->-92, SKELETON 14->32; pos: BIG_ORC,EXPLOSION,SKELETON; score [199, 0, 0, 0]->[194, 0, 0, 0]; events 175->183; rng_state; weapon_tracks; weapons 86->79 | `WalkerHpRangeAtFinalTick(FAMILY_BIG_ORC, 9100, 9300)` -> `WalkerHpRangeAtFinalTick(FAMILY_BIG_ORC, 9700, 9900)`; label text updated |
| `input_special_switch_wrap_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: MAGE 85->84 | — |
| `invisibility_thief_scen99` | companion capture (`268097e1`) | gate-red | hp: THIEF 15->2 | `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 1500, 1500)` -> `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 200, 200)` |
| `invulnerable_potion_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | hp: SOLDIER 77->64, ARCHER 52->51 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 7700, 7700)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6400, 6400)` |
| `mage_starburst_ring_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: MAGE 77->73; score [649, 0, 0, 0]->[655, 0, 0, 0]; events: score_change.bx2 | — |
| `magic_damage_slime_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: SLIME 126->125; score [36, 0, 0, 0]->[37, 0, 0, 0]; events: score_change.bx1 | — |
| `midcombat_partial_hp_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: SOLDIER 85->77 | — |
| `multiplayer_two_teams_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: THIEF 68->67 | — |
| `orc_yell_stun_hold_scen99` | companion capture (`268097e1`) | gate-red | hp: ORC 74->73 | `WalkerHpRangeAtFinalTick(FAMILY_ORC, 7400, 7400)` -> `WalkerHpRangeAtFinalTick(FAMILY_ORC, 7300, 7300)`; label text updated |
| `rng_seed_stable_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 46->29 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4600, 4600)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 2900, 2900)` |
| `save_roundtrip_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 61->48 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6100, 6100)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4800, 4800)` |
| `scoring_after_combat_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 63->50 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 5000)` |
| `slime_death_split_scen99` | companion capture (`268097e1`) | gate-red | hp: MEDIUM_SLIME 72->70 | `WalkerHpRangeAtFinalTick(FAMILY_MEDIUM_SLIME, 7100, 7300)` -> `WalkerHpRangeAtFinalTick(FAMILY_MEDIUM_SLIME, 6900, 7100)`; label text updated |
| `soldier_whirlwind_ring_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: FAERIE 56->55, FAERIE 58->57, FAERIE 55->54 | — |
| `special_archer_1_scen99` | companion capture (`268097e1`) | gate-red | hp: ARCHER 42->31, SOLDIER 116->115; score [6, 0, 0, 0]->[7, 0, 0, 0]; events: score_change.bx1 | `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 4000, 7000)` -> `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 2900, 5900)` |
| `special_archer_2_scen99` | companion capture (`268097e1`) | gate-red | hp: ARCHER 35->22 | `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3500, 3500)` -> `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 2200, 2200)` |
| `special_archer_3_scen99` | companion capture (`268097e1`) | gate-red | hp: ARCHER 34->21 | `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 3400, 3400)` -> `WalkerHpRangeAtFinalTick(FAMILY_ARCHER, 2100, 2100)` |
| `special_archmage_2_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ARCHMAGE 147->146; score [513, 0, 0, 0]->[515, 0, 0, 0]; events: score_change.bx1 | — |
| `special_archmage_3_scen99` | companion capture (`268097e1`) | gate-red | hp: ARCHMAGE 92->79; events: play_sound.ax1; rng_state | `WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 9200, 9200)` -> `WalkerHpRangeAtFinalTick(FAMILY_ARCHMAGE, 7900, 7900)` |
| `special_archmage_4_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ARCHMAGE 147->146 | — |
| `special_archmage_scen123` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: ARCHMAGE 147->146 | — |
| `special_barbarian_1_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: BARBARIAN 90->77 | — |
| `special_barbarian_2_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: BARBARIAN 95->83 | — |
| `special_cleric_2_scen99` | companion capture (`268097e1`) | gate-red | hp: CLERIC 58->46 | `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 5800, 5800)` -> `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 4600, 4600)` |
| `special_cleric_3_scen99` | companion capture (`268097e1`) | gate-red | hp: CLERIC 58->46 | `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 5800, 5800)` -> `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 4600, 4600)` |
| `special_cleric_4_scen99` | companion capture (`268097e1`) | gate-red | hp: CLERIC 58->46 | `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 5800, 5800)` -> `WalkerHpRangeAtFinalTick(FAMILY_CLERIC, 4600, 4600)` |
| `special_cleric_heal_ally_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: CLERIC 116->115, BIG_ORC 413->412, FAERIE 6->3; score [17, 0, 0, 0]->[20, 0, 0, 0]; events: score_change.bx3 | — |
| `special_druid_1_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: DRUID 47->35 | — |
| `special_druid_2_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: DRUID 101->99, FAERIE 18->15, SOLDIER 102->99; score [27, 0, 0, 0]->[30, 0, 0, 0]; events: score_change.bx3 | — |
| `special_druid_3_scen99` | companion capture (`268097e1`) | gate-red | hp: DRUID 48->36 | `WalkerHpRangeAtFinalTick(FAMILY_DRUID, 4800, 4800)` -> `WalkerHpRangeAtFinalTick(FAMILY_DRUID, 3600, 3600)` |
| `special_druid_4_scen99` | companion capture (`268097e1`) | gate-red | hp: DRUID 48->36 | `WalkerHpRangeAtFinalTick(FAMILY_DRUID, 4800, 4800)` -> `WalkerHpRangeAtFinalTick(FAMILY_DRUID, 3600, 3600)` |
| `special_elf_1_scen99` | companion capture (`268097e1`) | gate-red | hp: ELF 17->4 | `WalkerHpRangeAtFinalTick(FAMILY_ELF, 1700, 1700)` -> `WalkerHpRangeAtFinalTick(FAMILY_ELF, 400, 400)` |
| `special_elf_2_scen99` | companion capture (`268097e1`) | gate-red | hp: ELF 18->5 | `WalkerHpRangeAtFinalTick(FAMILY_ELF, 1800, 1800)` -> `WalkerHpRangeAtFinalTick(FAMILY_ELF, 500, 500)` |
| `special_elf_3_scen99` | companion capture (`268097e1`) | gate-red | hp: ELF 17->4 | `WalkerHpRangeAtFinalTick(FAMILY_ELF, 1700, 1700)` -> `WalkerHpRangeAtFinalTick(FAMILY_ELF, 400, 400)` |
| `special_elf_4_scen99` | companion capture (`268097e1`) | gate-red | hp: ELF 18->5 | `WalkerHpRangeAtFinalTick(FAMILY_ELF, 1800, 1800)` -> `WalkerHpRangeAtFinalTick(FAMILY_ELF, 500, 500)` |
| `special_fireelemental_1_scen99` | companion capture (`268097e1`) | gate-red | hp: FIREELEMENTAL 41->28 | `WalkerHpRangeAtFinalTick(FAMILY_FIREELEMENTAL, 4100, 4100)` -> `WalkerHpRangeAtFinalTick(FAMILY_FIREELEMENTAL, 2800, 2800)` |
| `special_ghost_1_scen99` | companion capture (`268097e1`) | gate-red | hp: GHOST 21->16 | `WalkerHpRangeAtFinalTick(FAMILY_GHOST, 2100, 2100)` -> `WalkerHpRangeAtFinalTick(FAMILY_GHOST, 1600, 1600)` |
| `special_mage_2_scen99` | companion capture (`268097e1`) | gate-red | hp: MAGE 31->18 | `WalkerHpRangeAtFinalTick(FAMILY_MAGE, 3100, 3100)` -> `WalkerHpRangeAtFinalTick(FAMILY_MAGE, 1800, 1800)` |
| `special_mage_3_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | hp: MAGE 78->75 | `WalkerHpRangeAtFinalTick(FAMILY_MAGE, 7600, 7800)` -> `WalkerHpRangeAtFinalTick(FAMILY_MAGE, 7300, 7500)` |
| `special_mage_4_scen99` | companion capture (`268097e1`) | gate-red | hp: MAGE 34->21 | `WalkerHpRangeAtFinalTick(FAMILY_MAGE, 3400, 3400)` -> `WalkerHpRangeAtFinalTick(FAMILY_MAGE, 2100, 2100)` |
| `special_mage_5_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: MAGE 87->86 | — |
| `special_mage_scen126` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: MAGE 86->85 | — |
| `special_medium_slime_1_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: SLIME 54->41 | — |
| `special_orc_1_scen99` | companion capture (`268097e1`) | gate-red | hp: ORC 82->69 | `WalkerHpRangeAtFinalTick(FAMILY_ORC, 8200, 8200)` -> `WalkerHpRangeAtFinalTick(FAMILY_ORC, 6900, 6900)` |
| `special_orc_2_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ORC 83->70 | — |
| `special_skeleton_1_scen99` | companion capture (`268097e1`) | gate-red | pos: SOLDIER; events 7->6; rng_state; weapon_tracks | `EventKindAtLeast(/*play_sound*/1, 7)` -> `EventKindAtLeast(/*play_sound*/1, 6)` |
| `special_slime_1_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | hp: SMALL_SLIME 141->139, SMALL_SLIME 82->70 | `WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 14100, 14100)` -> `WalkerHpRangeAtFinalTick(FAMILY_SMALL_SLIME, 13900, 13900)` |
| `special_soldier_1_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 63->50 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 5000)` |
| `special_soldier_2_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 65->52, SOLDIER 98->97; score [24, 0, 0, 0]->[25, 0, 0, 0]; events: score_change.bx1 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6500, 6500)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5200, 5200)` |
| `special_soldier_3_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 63->50 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 5000)` |
| `special_soldier_4_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 63->50 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6300, 6300)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 5000)` |
| `special_thief_2_scen99` | companion capture (`268097e1`) | gate-red | hp: THIEF 36->27 | `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 3600, 3600)` -> `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 2700, 2700)` |
| `special_thief_3_scen99` | companion capture (`268097e1`) | gate-red | hp: THIEF 38->30 | `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 3800, 3800)` -> `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 3000, 3000)` |
| `special_thief_4_scen99` | companion capture (`268097e1`) | gate-red | hp: THIEF 16->3 | `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 1600, 1600)` -> `WalkerHpRangeAtFinalTick(FAMILY_THIEF, 300, 300)` |
| `summon_druid_pet_scen950` | companion capture (`268097e1`) | gate-red | hp: DRUID 55->43, SOLDIER 77->67; score [53, 0, 0, 0]->[63, 0, 0, 0]; events: score_change.bx10 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 7700, 7700)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6700, 6700)` |
| `thief_ai_bomb_flee_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | score [64, 0, 0, 0]->[67, 0, 0, 0]; events: score_change.bx3 | — |
| `thief_charm_opponent_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: THIEF 70->69 | — |
| `thief_taunt_matched_levels_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | hp: TOWER1 111->110 | `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 11100, 11100)` -> `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 11000, 11000)`; label text updated |
| `tick_cadence_scen9301` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 46->29 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 4600, 4600)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 2900, 2900)` |
| `treasure_drumstick_pickup_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 47->46, ARCHER 70->69 | — |
| `treasure_magic_potion_overfill_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: MAGE 24->23 | — |
| `treasure_stain_pickup_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: SOLDIER 106->104, FAERIE 20->17 | — |
| `treasure_teleporter_pickup_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 45->61; pos: SOLDIER; events 14->12; rng_state; weapon_tracks | — |
| `weapon_arrow_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ARCHER 33->20 | — |
| `weapon_blob_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: SLIME 140->138 | — |
| `weapon_blood_emission_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 74->61 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 7400, 7400)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6100, 6100)` |
| `weapon_bone_emission_scen99` | companion capture (`268097e1`) | gate-red | walkers 2->1; pos: SOLDIER; events 36->32; rng_state; weapon_tracks; weapons 2->0 | `WalkerFamilyCount(FAMILY_SKELETON, 1, 1)` -> `WalkerDiedByFinal(FAMILY_SKELETON)`; `EventKindAtLeast(/*play_sound*/1, 35)` -> `EventKindAtLeast(/*play_sound*/1, 31)` |
| `weapon_boomerang_return_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 101->95, ARCHER 68->67; score [23, 0, 0, 0]->[24, 0, 0, 0]; events: score_change.bx1 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 10100, 10100)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 9500, 9500)` |
| `weapon_boulder_emission_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | walkers 2->1; events 31->32; rng_state; weapon_tracks | `WalkerFamilyCount(FAMILY_GIANT_SKELETON, 1, 1)` -> `WalkerDiedByFinal(FAMILY_GIANT_SKELETON)` |
| `weapon_boulder_explode_damage_scen99` | branch dump (ledger row; `21f2606a`) | gate-red | hp: BARBARIAN 122->120, ORC 23->21, SOLDIER 46->45; score [194, 0, 0, 0]->[197, 0, 0, 0]; events: score_change.bx2 | `WalkerHpRangeAtFinalTick(FAMILY_ORC, 2200, 2400)` -> `WalkerHpRangeAtFinalTick(FAMILY_ORC, 2000, 2200)`; label text updated |
| `weapon_circle_protection_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: SOLDIER 51->36 | — |
| `weapon_door_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: SOLDIER 51->36 | — |
| `weapon_exploding_boulder_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: BARBARIAN 110->107 | — |
| `weapon_fire_arrow_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ARCHER 34->21 | — |
| `weapon_fire_arrow_explode_damage_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | score [2708, 0, 0, 0]->[2710, 0, 0, 0]; events: score_change.bx1 | — |
| `weapon_fireball_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: MAGE 31->18 | — |
| `weapon_glow_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: CLERIC 60->47 | — |
| `weapon_hammer_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: BARBARIAN 94->81 | — |
| `weapon_knife_emission_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 64->51 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 6400, 6400)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5100, 5100)` |
| `weapon_lightning_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: DRUID 53->40 | — |
| `weapon_meteor_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: FIREELEMENTAL 43->30 | — |
| `weapon_ranged_impact_hp_scen99` | companion capture (`268097e1`) | gate-red | hp: TOWER1 103->96; score [34, 0, 0, 0]->[41, 0, 0, 0]; events: score_change.bx7 | `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 10200, 10400)` -> `WalkerHpRangeAtFinalTick(FAMILY_TOWER1, 9500, 9700)`; label text updated |
| `weapon_rock_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ELF 17->4 | — |
| `weapon_rock_slot2_emit_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: ELF 70->69 | — |
| `weapon_sprinkle_freeze_scen99` | branch dump (ledger row; `21f2606a`) | gate-green (hp not pinned) | hp: ORC 29->28; score [113, 0, 0, 0]->[114, 0, 0, 0]; events: score_change.bx1 | — |
| `weapon_tree_emission_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: SOLDIER 51->36 | — |
| `weapon_wave2_emission_scen99` | companion capture (`268097e1`) | gate-red | hp: SOLDIER 51->36 | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 5000, 9000)` -> `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 3500, 7500)` |
| `weapon_wave_promote_wave2_scen99` | companion capture (`268097e1`) | gate-green (hp not pinned) | hp: MAGE 85->84 | — |

## Golden byte-compare (2026-09-08)

**The change.** Issue #283. `og_test_parity`'s SemanticParity contract now requires
the canonical branch dump to equal the committed golden byte for byte
(`tests/parity/golden_compare.h`, the one compare both `CompareMode` arms call), in
addition to the facts holding on both dumps. The hand-rolled `weapon_tracks` loop is
gone (subsumed). There is no waiver: a row that cannot match is fixed by porting the fix into the old game and
recapturing the row from it, or the current game is fixed. `scripts/parity/diff_dumps.py`
is the triage tool (categorised first divergence); it decides nothing.

**Procedure.** (1) Compare landed with `effect_chain.lua` and every golden
untouched: red on exactly one row, `effect_chain_emission_scen99` (byte 756,
`events[9].b`, branch 114 vs golden 113), green on the other 219 — the corpus was
already byte-clean (`cmp` on all 220 recon dumps). (2) Bisect: the chain fork's
damage cut is `Sint32 generic = (damage)/2` in classic (`effect.cpp:429`, unchanged
since the import); the branch's `og.fmul` kept the fraction (from `dc1a2a89`, carried
into Lua), so third-generation bolts carried 27.5 vs 27 — invisible under the old
truncating hit, exposed by #282's nearest-point rounding. Fixed on the branch with
`og.trunc`; `docs/GAMEPLAY_FIXES_FROM_CLASSIC.md` row "Chain lightning fork damage
kept a fractional half point". With the Lua fixed and the golden still old the row was
still red with the identical message (same byte 756, same two 96-byte windows) — the
residual was the stale golden; `diff_dumps.py`'s count dropped from 5 fields to 3.
(3) Blast radius on all 220 rows: one moved. (4) Table consistency: the companion
table was re-synced byte-identical to `tests/parity/scenario_table.h` (companion
commit `e9e1f051`, recorder-only — `tools/parity_scenario_table.h` and nothing else),
the dumper's stale objects deleted and the dumper rebuilt; `--list` still prints 221
ids and every SemanticParity non-internal id in the branch manifest is present. (The
`bomb_l10_vs_cleric_l9_scen99` row landed after this paragraph was written and was
mirrored the same way in a second recorder-only companion commit, `d49b16c9`; the
companion `--list` prints 222 from there on.) Four
control captures (`combat_attack_scen99`, `weapon_knife_emission_scen99`,
`effect_chain_fork_scen99`, `effect_chain_emission_scen99`) taken before and after
the sync are `diff -rq`-identical, so the sync moved nothing. (5) Source rule as in
the armor-roll wave; merge base `81bd6036`. Companion HEAD before the sync `8d98597e`
(= `268097e1` + one comment-only commit).

**Counts.** 1 golden from the companion capture, 0 re-blessed from the branch dump.
Plus, later on the same branch, 1 NEW golden for the `bomb_l10_vs_cleric_l9_scen99`
scenario (#228): a raw companion capture at `d49b16c9`, byte-identical to the branch
dump, blessing nothing — a new row, not a moved one.

| id | new golden source | gate | what moved (old golden -> new) | facts retuned |
|---|---|---|---|---|
| `effect_chain_emission_scen99` | companion capture (`e9e1f051`) | gate-red (byte 756, `events[9].b`) | hp: ARCHMAGE 73->66, SOLDIER 37->37 (branch read 36 before the Lua fix); score [496, 0, 0, 0]->[497, 0, 0, 0]; events: score_change.b x2 (113->114, 202->202; branch read 205 before the fix) | yes, afterwards — see "Predicate teeth after the byte compare" |

## Predicate teeth after the byte compare (2026-09-08)

Byte-comparing the canonical dump (above) has a side effect on the mutation
canary: the per-name `Parity.<id>` gtest now reds whenever a mutation moves ANY
byte of the dump, so a row whose own facts are inert still shows a gtest flip
and used to be counted as guarded. `scripts/parity/run_mutation_canary.sh` now
counts predicate flips and the gtest verdict separately and fails a row with
zero PREDICATE flips (`canary: PREDICATE-TOOTHLESS`), so the debt is visible
instead of hidden behind the bytes.

Measured on the full `--all` run of 222 rows: **11 rows** flipped the gtest and
nothing else. Retuned here, each verified by staging the pin into
`build/ci-test/packs` and re-running `parity_runner_smoke --evaluate-facts`.
No golden moved — facts and comments are not in the dump — and the companion
table was re-synced byte-identically in recorder-only commit `3f6e3cbd`, with
five control captures identical before and after:

| row | dead fact | replaced by (measured golden -> mutated) |
|---|---|---|
| `effect_chain_emission_scen99` | `WalkerHpRangeAtFinalTick(FAMILY_SOLDIER, 0, 11900)` — the archmage's own melee leaves a soldier at 11500 cents under the mutation, inside the window | exact survivor hp 3700; exact `ScoreDelta(0, 497, 497)` -> 0; exact `EventKindExactly(play_sound, 13)` -> 6 |
| `effect_bomb_emission_scen99` | `EventKindAtLeast(play_sound, 12)` — the mutated run has 14, above the floor (the comment claiming 11 and "two bombs detonate" was wrong on both counts; the golden has one detonation) | `EventKindExactly(play_sound, 13)` -> 14 |
| `bomb_l10_vs_cleric_l9_scen99` | `EventKindAtLeast(play_sound, 1)` — the tick-10 melee CLANG predates the cast, so the floor held with no blast at all | `EventKindExactly(play_sound, 4)` -> 3 (this row already had two flipping facts; the floor was the vacuous one) |

### The remaining nine, retuned (2026-09-15)

The nine rows that were open debt above now each carry ONE exact fact that
flips under their own pin. The structural anchors they already had were KEPT,
not replaced -- `behavioural_coverage_gate_effects` binds its families through
the `EffectFamilyCount(F, 0, 0)` entries and `TreasureFamilyOfOrderRemovedFromOblist`
is the Order-aware removal cover -- so every new fact is an APPEND at the end of
the row's `kFacts_` array. No golden moved: facts, labels and comments are not
in the dump, and all nine goldens are byte-identical before and after.

Two rows needed a tool that did not exist: `WalkerOfOrderFamilyCount(family,
order, min, max)` (`tests/parity/fact_predicate.h`, appended last so every
existing ordinal keeps its value). `WalkerFamilyCount` / `WalkerPositionMoved` /
`WalkerHpRangeAtFinalTick` all resolve arg0 through the Living table, so FX
family id 4 would render as `FAMILY_SKELETON`, and `EffectFamilyCount` reads
`dump.effects[]`, which an `add_ob(Order::FX)` entity never reaches. The dead
`FAMILY_FLASH` entry parked in oblist is nameable by nothing else.

| row | kept anchor | added fact | golden -> mutated | flip index |
|---|---|---|---|---|
| `treasure_stain_pickup_scen99` | `TreasureFamilyOfOrderRemovedFromOblist(FAMILY_STAIN, kOrderTreasure)` | `WalkerHpRangeAtFinalTick(FAMILY_FAERIE, 1700, 1700)` | 1700 -> 7494200 cents | #5 |
| `treasure_life_gem_pickup_scen99` | `TreasureFamilyOfOrderRemovedFromOblist(FAMILY_LIFE_GEM, kOrderTreasure)` | `WalkerOfOrderFamilyCount(FAMILY_FLASH, kOrderFX, 1, 1)` | 1 -> 0 (dead FLASH at (94,118)) | #5 |
| `effect_flash_emission_scen99` | `EffectFamilyCount(FAMILY_FLASH, 0, 0)` | `WalkerOfOrderFamilyCount(FAMILY_FLASH, kOrderFX, 1, 1)` | 1 -> 0 | #5 |
| `effect_magic_shield_emission_scen99` | `EffectFamilyCount(FAMILY_MAGIC_SHIELD, 0, 0)` | `EffectNetTravel(FAMILY_MAGIC_SHIELD, kWeaponPathReturns, 10000)` | 25 samples, pathlen 22380 / net 4754 -> pathlen 0 / net 0 | #6 |
| `effect_knife_back_emission_scen99` | `EffectFamilyCount(FAMILY_KNIFE_BACK, 0, 0)` | `EventKindExactly(play_sound, 9)` | 9 -> 8 | #6 |
| `effect_boomerang_emission_scen99` | `EffectFamilyCount(FAMILY_BOOMERANG, 0, 0)` | `EffectNetTravel(FAMILY_BOOMERANG, kWeaponPathReturns, 4000)` | 25 samples, pathlen 8227 / net 1709 -> pathlen 0 / net 0 | #6 |
| `special_skeleton_1_scen99` | `WalkerDiedByFinal(FAMILY_SKELETON)` | `EventKindExactly(play_sound, 6)` | 6 -> 13 | #5 |
| `special_barbarian_2_scen99` | `WalkerHpRangeAtFinalTick(FAMILY_BARBARIAN, 5000, 12000)` | `WalkerHpRangeAtFinalTick(FAMILY_BARBARIAN, 8300, 8300)` | 8300 -> 8200 cents | #4 |
| `weapon_rock_slot2_emit_scen99` | `WeaponSpeed(FAMILY_ROCK, 650, 900)` + `WeaponNetTravel(FAMILY_ROCK, STRAIGHT, 1000)` | `WalkerPositionMoved(FAMILY_SOLDIER, 156, 120)` | (156,120) -> (152,120) | #7 |

Each row's flip was measured twice: by the staged-packs fast path (copy the
pinned pack file aside, `_apply_mutation.py` on `build/ci-test/packs/...`,
`parity_runner_smoke --evaluate-facts`, restore, `cmp`) and by
`run_mutation_canary.sh --scenario <id>`, which prints `predicate_flips=1`
naming the index in the table above and `canary: OK` for all nine at
`0a151cb8` (the commit that added the facts; ~51 s per row). The same fast
path run on the tree BEFORE that commit is the control: all nine reported
`predicate_flips=0`, which is what PREDICATE-TOOTHLESS means.

**The full `--all` re-measure (2026-09-16, `ed0076cb`).** Claimed now, and
measured through the new CI lane's own step
(`.github/workflows/parity-canary.yml`, the schedule arm):
`canary: rows=222 groups=197 rebuilds=64 zero_flips=0 predicate_toothless=0`
and `canary: OK -- every scenario flipped at least one predicate of its own`,
in 19 minutes of wall clock at `CMAKE_BUILD_PARALLEL_LEVEL=2` (165 staged-lua
groups in ~70 s with no rebuild, then 32 rebuild-cpp groups at two rebuilds
each). The minimum `pred_flips` over the whole 222-row report is 1, so the nine
rows retuned above and the seventeen orphan pins attached in the same wave are
measured rather than argued, and the PREDICATE-TOOTHLESS list is empty for the
first time since #283 made the byte compare the gtest oracle.

**Three false comments corrected in the same pass** (each on its own lines, so
no pinned `src/` or `packs/` line moved):

- `effect_boomerang_emission_scen99`'s `TEETH:` block claimed the unhandled FX
  "animates one cycle, then set_dead/death within ~2 ticks -> team 0 collapses
  to the lone caster = 1". Measured: the FX's own lifetime (30 + level*12 = 78
  ticks) outlives the 45-tick budget, so it is alive at the final tick on BOTH
  arms and `WalkerOfTeamAlive(0, 2, 3)` reads 3 either way. The block now says
  so and points at the new `EffectNetTravel` as the discriminator.
- `special_barbarian_2_scen99`'s HP-band label said "golden 8900 cents". The
  golden is 8300.
- `weapon_rock_slot2_emit_scen99`'s `WeaponSpeed` label claimed the mutation
  raises the rock to "~1404 centi-px/tick (1404 > 900)". Measured: the seq-0
  max consecutive-tick step goes 671 -> 781 and stays inside [650,900], so that
  predicate does not flip; the same false number was repeated in the pin's own
  rationale and is corrected there too. The `WeaponNetTravel` label's
  "net=pathlen=1414" was equally unmeasured (golden net 5725 of pathlen 5727).
- Two more pin rationales named flips that do not happen and were rewritten to
  the measured one: `kMut_treasure_stain_pickup` claimed a
  `WalkerDiedByFinal(FAMILY_FAERIE)` flip on a row that has never carried that
  predicate (the faerie is alive and wounded at the final tick on both arms),
  and `kMut_special_skeleton_1_scen99` claimed `WalkerFamilyCount(FAMILY_SKELETON,
  0, 0)` and `WalkerDiedByFinal(FAMILY_SKELETON)` "both fail" (the skeleton dies
  inside the budget either way; both still pass).

The two anchor comments on the FLASH rows were wrong in the other direction:
they said the telflash "is not directly countable at tick 150 under schema-v1"
and offered the gem's removal plus a `score_change` event as proxies. The
golden carries the FLASH itself -- expiring does not remove an oblist entry --
and both proxies are inert under the pin (the un-eaten gem leaves the removal
check `indeterminate`; the golden's `score_change` count is 0 on both arms).

The tally that must stay at zero unconditionally is still the "zero flips" one
above the PREDICATE-TOOTHLESS heading.

## Removed goldens

`snapshot_dirty_bits_scen9301.json` — deleted. Nothing read it. The row is
`CompareMode::Invariant` AND `is_branch_internal`, so `og_test_parity` runs it
twice and compares the two dumps to each other; the golden on disk is not one
of the two. `capture_master_golden.sh` skips it for the same reason (it
captures `SemanticParity && !is_branch_internal` only), and the other four
branch-internal rows carry no golden at all — that is the contract, and this
file was the exception nobody chose. It had not changed since #106
(`de9d02b2`), and the merge base reproduces the branch byte-for-byte, so
nothing was blessed away by removing it.

`smoke_empty_scen99.json` — deleted 2026-09-08, under the same rule. The row is
`CompareMode::Invariant` (though not branch-internal), so `run_one_scenario`
returns after the two-captures-agree check and its facts, without ever reading a
golden, and `capture_master_golden.sh` skips it as SemanticParity-only. The file
on disk was byte-identical to both the branch dump and a fresh companion capture,
so nothing was blessed away; it simply had no reader. Promoting the row to a
byte-compared mode instead was not available: `lint_scenario_facts.py` rule (b)
requires a non-`TickReached` fact on every `ByteEqual` / `SemanticParity` row, and
an arena with no walkers, effects, events or score has nothing else to assert.

The every-golden sweep is now 221 files against the 221 SemanticParity rows that
byte-compare against them (222 master-comparable rows, the 222nd being the
Invariant `smoke_empty_scen99`), with no leftover to explain.

## Removed pins (2026-09-15)

Three `kMut_*` definitions in `tests/parity/scenario_table.h` were the
discriminating mutation of no `ScenarioSpec` row. An orphan pin is not a weak
pin, it is an unmeasured one: `check_mutation_pins.py` keeps its anchor honest,
but no canary run ever applies it, so its rationale is free to describe a flip
that stopped happening and nothing contradicts it. Twenty pins were orphaned;
seventeen were attached to their `family_<x>_scen99` row (each re-measured, each
flipping that row's `WalkerHpRangeAtFinalTick` from True to False), and these
three could not be attached honestly. `lint_scenario_facts.py` grew an
`orphan_mutation_constant` rule in the same commit, so the class cannot return
silently. No golden moved: the mutation column and the rationale strings are not
in the dump.

`kMut_family_elf_init` (`packs/core/families/living-01-elf.lua:69`,
`hp = 75` -> `hp = 7500`) — the only row it could have attached to is
`family_elf_scen99`, and that row is the sole user of
`kMut_family_spawn_identity_elf`, the one bespoke loader-identity pin, and it is
bespoke for a reason: the shared `kMut_family_spawn_identity` rotates every
loaded family by one (`(family + 1) % 21`), which on this arena turns the player
SOLDIER (0) *into* an ELF as it turns the target ELF (1) into an ARCHER, so
`WalkerFamilyCount(FAMILY_ELF, 1, 1)` still counts one and the row survives the
mutation untouched. The `_elf` pin maps 0 -> ARCHER and 1 -> SOLDIER instead, so
no ELF is left to count. Attaching the hp pin would have traded one orphan for
another and dropped `src/resources/gloader.cpp:822` from four guarding rows
(`family_ghost`, `family_golem`, `family_giant_skeleton`, `family_elf`) to
three.

`kMut_family_giant_skeleton_init` (`packs/core/families/living-19-beast.lua:17`,
`hp = 300` -> `hp = 1`) — `kFamilySpawns_complete_giant_skeleton` parks the
target at (600, 680) and `family_giant_skeleton_scen99.json` shows it there with
`alive=false hp=0` while the player soldier still holds 120 hp at (224, 120): it
never fought, and its death is not a combat outcome the pin can change. The only
dump field `hp = 300 -> hp = 1` can move on that row is `max_hp`, which no
`FactKind` reads, so attaching it would have produced a PREDICATE-TOOTHLESS row
by construction — worse than the orphan, because a PREDICATE-TOOTHLESS row fails
`run_mutation_canary.sh --all`, and the standing rule for that list (see
"Predicate teeth after the byte compare" above) is that it may only shrink. The
row keeps `kMut_family_spawn_identity`.

`kMut_save_corrupt` (`src/resources/save_data.cpp:132`,
`std::uint8_t temp_version = 9;` -> `= 0;`) — the pinned line is inside
`SaveData::load`, and `tests/parity/parity_runner.cpp:461` constructs `SaveData
save;`, hands it to `set_sim_context` and `ScopedGameplayContext`, and never
calls a method on it. No scenario reaches save serialisation at all, so the pin
could only have been attached to a row it does not exercise:
`save_roundtrip_scen99` is a scen1.fss combat arena on `kMut_combat_damage`
whose id merely contains "save", which is exactly what the static gate at
`tests/parity/test_parity_coverage_gate.cpp` (the
`src/resources/save_data.cpp` + `id.find("save")` rule) accepts — that gate stays
in place as the guard against re-attaching a save pin to a row that never saves.
Teaching the runner a real save round-trip is a harness feature (new runner
behaviour on both arms, a new row, a new golden, a companion capture against
e761-era save code), not a pin fix.

## The Open list before #349 (2026-10-04)

Superseded (2026-10-05) by the additive old-game measurement in "Ported fixes", "#349 ports"; kept verbatim, with a dated correction pointer on each sentence that measurement refutes.

Tracked in #349. 70 of 223 at `ab0a7d2a` / this branch, measured by `cmp` of a full old-game capture; this list may only
shrink; a row leaves it when its fix is ported and it `cmp`s equal. Every row here predates the 2026-10-04 rule.

The evidence cells keep the wording of the adjudication that blessed each row, where "branch" is the current game
and "companion" or `openglad-master` is the old game. Each was judged by patching the scenario row into the merge base
of the PR that blessed it, rebuilding its `parity_runner_smoke` there and diffing the merge-base dump against the
current game's: `branch == merge base` meant the divergence predates that PR. The merge base differs per row:
`05eaaa23` for the wave adjudicated before that branch was rebased, `18adcafd` for rows added after the rebase, and
`81bd6036` for the golden byte-compare wave. That clause shows a row's divergence is not a regression of the PR that
blessed it. Under the rule it does not make the current-game dump a golden; the row stays here until its fix is
ported.

Rows marked **R** differ by how the two recorders represent an event, not by gameplay. They need a recorder-only
old-game commit (or a change to the parity test driver), not a behaviour port. For notifications: the old game's
recorder records the notifier's family and team in a Notification's `a`/`b` (`src/screen.cpp:1377-1384`); the current
game's `SimEventLog::push_notification` records the HUD display duration in `a` (`src/gameplay/sim_event_log.cpp:43-55`;
0 for these texts) and nothing in `b`; the parity test driver rewrites the current game's `a`/`b` for exactly three
texts (`tests/parity/parity_runner.cpp:220-237`) and leaves every other text alone. A soldier notifier (family 0)
matches by accident; any other family differs.

### Freeze-time census residual (R)

| id | differs from the old-game capture in | fix to port (current game → old-game site) | evidence |
|---|---|---|---|
| `treasure_magic_potion_overfill_scen99` **R** | `events[0].a` only, after the #231 port (C5-FREEZE: `diff_dumps.py` `[1/1] events[0].a: branch=0 master=3`): the "Potion of Mana(10)!" notification carries `a = 0` in the golden and `a = 3` (`FAMILY_MAGE`) in the old-game capture. | Notification metadata (class R): a recorder-only old-game commit or a parity-test-driver canonical form; see the paragraph above. The #231 half of this row is ported (Ported fixes). | Notification metadata, not gameplay. The companion's `screen::do_notify` records `a = who->query_family()`, `b = who->team_num` (`openglad-master/src/screen.cpp:1379-1384`); the modern sim's `SimEventLog::push_notification` has no notifier argument and puts the display `duration` in `a` (`src/gameplay/sim_event_log.cpp:42-53`). The harness already papers over three classic texts by name in `normalize_classic_notification_metadata` (`tests/parity/parity_runner.cpp:197`); potion texts were never in that list because every previous potion row used a `FAMILY_SOLDIER` eater, whose family id is 0 and so matched by accident. This row is the first with a non-soldier eater. Merge base reproduces the branch's `a = 0`. (The first cell of the original row described the `events[0].a` half; the #231 half that it pointed to is now under Ported fixes.) |

### Druid PROTECTION top-up

| id | differs from the old-game capture in | fix to port (current game → old-game site) | evidence |
|---|---|---|---|
| `druid_protection_refresh_scen99` | Recasting the druid's slot-4 PROTECTION on a friend who already carries a circle now tops that circle up (50 → 100 hp, one ring in weaplist) instead of summoning a second one (two rings). | Current game `19d7eeb3` (`be57275f`): `protection_circle`'s top-up scans `og.find_in_range("weap", 100, friend)` → the old game's `src/walker.cpp:3826-3838` oblist scan, which never finds a summoned circle. | `protection_circle`'s top-up arm selected the existing circle by walking `og.oblist()`, mirroring `openglad-master/src/walker.cpp:3813`; summoned weapons live in `weaplist`, so the scan never matched and the arm never ran between the 2002 FSGames import and 2026. Fixed on the user's order: the scan now uses `og.find_in_range("weap", 100, friend)`. Current-game commit `19d7eeb3` (#146, the master squash; the pre-squash `be57275f6b8e47979c9e278539b15570085c7a2d`, "Fix the druid's protection top-up, dead since the 2002 import", is not in the object store). The old game still stacks, so its capture reads `WeaponFamilyCount(FAMILY_CIRCLE_PROTECTION, 1, 1)` = 2 and would turn the row's discriminating predicate into a permanent red. |

### #132 single-floor fixes (M1-M6), adjudicated retroactively 2026-08-01

The top drift table below adjudicates goldens one at a time. This section adjudicates a
whole commit.

`c409e7c85` ("Multi-floor scenarios (#132)") is a ~25-commit squash. Its headline
claim is true as far as it goes: the Z-axis foundation really is byte-identical on
single-floor levels — `walker::apply_z_motion` early-returns on `floor_count() <= 1`,
and every Z rule in the pathing graph and the obmap is gated the same way. But the
squash also carried several **deliberate single-floor gameplay fixes** that the
z-axis framing never mentioned, and it regenerated **42 parity goldens branch-side
with no ledger entry of any kind**. The behaviour changes were intended; the silence
was the failure.

Two facts were established before anything was attributed. First, all 42 pre-#132
goldens (`git show c409e7c85^:tests/parity/golden/<id>.json`) byte-match a fresh
classic-companion capture — so all 42 were faithful going in, and every one of the
42 regenerations records a real behaviour change rather than scaffolding churn.
Second, all 42 currently committed goldens byte-match BOTH the merge-base dump
(`05eaaa23` — this whole section predates the rebase, so that is its merge base;
rows adjudicated afterwards name `18adcafd` in their own cell) and the branch
dump: `branch == merge base` across the whole set, so the Lua class-pack
conversion contributed nothing to this divergence.

Attribution was then experimental, the same way the top drift table below works: each
candidate hunk was reverted in isolation at the merge base (`../openglad-mastertip`),
`parity_runner_smoke` rebuilt, and the dump re-diffed against a fresh
`parity_dump_master` capture. A three-hunk **compat probe** — M1, M2 and M3 each
re-gated to `floor_count() > 1` — returns **31 of the 42** to the companion
byte-for-byte while leaving all 43 Z-axis unit tests green and all three
`z_*_scen9301` rows deterministic, which is what proves these are single-floor
gameplay fixes and not z-axis machinery. **The probe was proof only and was not
adopted**: it costs five regression pins (`SingleFloorPathing` ×3,
`MeleeStandoff.facing_denial_snaps_to_face_the_foe_in_one_tick`,
`WestlandsStandoffTest.l2_forest_road_crew_fights_past_mid_road`) and both campaign
calibration suites, and the adjudicator worktree was restored to pristine. The
remaining 11 rows are M4–M7.

| id | mechanism (file:line at branch HEAD) | goldens it explains |
|---|---|---|
| **M1** | Melee snap-face: `COMMAND_ATTACK` answers a `Facing`/`NoRanged` `fire_check` denial with `walker::face_delta` instead of the classic `walkstep` — `src/gameplay/stats.cpp:582-592` (helper `walker_movement.cpp:492`, denial out-param `walker.cpp:1228`, enum `include/openglad/gameplay/walker.h:146`). Guard-standoff melee deadlock fix, 2026-07-07. `face_delta` sets `curdir`+`enddir`+`lastx/lasty` in ONE tick; the classic walk only sets `enddir` and lets `living::act`'s pre-command turn rotate `curdir` one 45° step per tick. EAST→WEST is four steps, of which the classic walk banks one on the denial tick — hence the "≈3 ticks earlier" signature, and hence identical weapon coordinates with shifted ticks. | **9 alone**: `consumable_inventory_state_scen99`, `family_ghost_scen99`, `invulnerable_potion_scen99`, `multiplayer_two_teams_scen99`, `smoke_nonempty_scen99`, `special_druid_2_scen99`, `special_slime_1_scen99`, `treasure_drumstick_pickup_scen99`, `treasure_stain_pickup_scen99`. Plus the two M2/M3 overlaps below. [Correction (2026-10-05): additive in the old game, M1 alone closes 28 goldens, including the five `event_*_emission` rows, `effect_bomb_bystander_scen99`, both elf rock rows and both treasure rows; see "#349 ports".] |
| **M2** | A* no-corner-cut prune, `floor_count() > 1` gate **removed 2026-07-10** — `src/gameplay/gameplay_context.cpp:200`. Diagonal expansion now requires both orthogonal flanks grid-passable on every level. | 19 of the 22-row pathing set: alone in `effect_chain_scen9410`, `family_golem_scen99`, `special_archmage_scen123`; with M3 in the 14 below; with M1+M3 in the 2 overlaps. |
| **M3** | Follow-path convex-corner alignment assist, same gate removed 2026-07-10 — `src/gameplay/walker_pathing.cpp:109`. Per-pixel collision-checked slide to exact grid alignment replaces the deterministic 2-tick corner oscillation. | 19 of the same 22: alone in `effect_explosion_emission_scen99`, `effect_heartburst_multitarget_scen99`, `weapon_boulder_emission_scen99`; with M2 in the 14 below; with M1+M2 in the 2 overlaps. |
| **M2+M3** | Both chase rules required — neither `-cornercut` nor `-alignassist` alone closes the row. [Correction (2026-10-05): true of the current game (subtractive); in the old game M2 alone closes 8 of these 14 and only 6 need both; see "#349 ports".] | **14**: `family_archer_scen99`, `family_archmage_scen99`, `family_barbarian_scen99`, `family_big_orc_scen99`, `family_cleric_scen99`, `family_druid_scen99`, `family_elf_scen99`, `family_fireelemental_scen99`, `family_mage_scen99`, `family_medium_slime_scen99`, `family_skeleton_scen99`, `family_slime_scen99`, `family_soldier_scen99`, `family_thief_scen99`. |
| **M1+M2+M3** | The only two rows where the melee snap-face still matters after both chase rules are classic (`-cornercut -alignassist` alone leaves them red). | **2**: `family_orc_scen99`, `family_small_slime_scen99`. |
| **M4** | obmap interpenetration escape — a strictly separating move is no longer blocked by a walker we already overlap; `src/gameplay/obmap.cpp:388`. Guard-standoff wave, 2026-07-07. | **1**, with M7: `effect_protection_emit_scen99`. The row's own #132 comment records that its friendly was MOVED (130,120)→(85,90) precisely because the escape rule let the previously-trapped walker wander out of range, so arena and sim both changed. |
| **M5** | Shove command-theft grid probe — `living::shove` no longer steals the target's queue for a terrain-blocked inject; `src/gameplay/living.cpp:444`. The `rng(3)` draw above it stays unconditional. | **2**, with M6: `effect_marker_emission_scen99`, `generator_saturation_scen99`. (Note: the `GAMEPLAY_FIXES_FROM_CLASSIC.md` Forest Road row claims the shove probe moves zero goldens. That held for the wave it was measured against; on these two crowded generator arenas it does move them.) [Correction (2026-10-05): not in the old game: these two rows close with M6 alone, and M5's rows are `beast_set_difficulty_invariant_scen99` and `mage_freeze_time_offteam_scen99`; see "#349 ports".] |
| **M6** | **A12b** — the double `set_difficulty` dropped from `create_weapon`'s `Order::Generator` branch, so a generator spawn no longer compounds two hp boosts; `src/gameplay/walker.cpp:1152`. Deliberate semantic fix, landed in #132 (not master-era, contrary to what the `generator_owner_cascade_scen99` and `beast_set_difficulty_invariant_scen99` entries in the top drift table below imply about its age). | **3 alone**: `generator_tent_emission_scen99`, `generator_tower_emission_scen99`, `generator_treehouse_emission_scen99`. Plus the 2 M5 rows. [Correction (2026-10-05): `generator_tent_emission_scen99` needs M6 and the A6/A7 teleport destination probe; see "#349 ports".] |
| **M7** | Not a sim change at all: #132 rewrote the parity **arena definitions**, and the classic companion (`openglad-master/tools/parity_scenario_table.h`) still carries the pre-#132 ones. `kFamilySpawns_event_arena` was reordered (the team-1 soldier no longer leads) and given a new `kInputsEventArena` input script in place of `kInputsEffectCombat`; `kFamilySpawns_effect_protection_emit_scen99`'s friendly moved. These rows compare **different fights**, so no sim revert can ever close them. | **5 alone**: `event_end_game_emission_scen99`, `event_notification_emission_scen99`, `event_request_redraw_emission_scen99`, `event_set_end_emission_scen99`, `event_set_palette_emission_scen99`. Plus `effect_protection_emit_scen99` with M4. (For the five, M7 is proven necessary; whether they ALSO carry an M1–M3 contribution was not separated, because the arena restore was measured with the compat probe active.) [Correction (2026-10-05): measured since: M1 alone closes all five; see "#349 ports".] |

Provably inert across all 42, despite looking like suspects: the `fire_check` gate
reorder (reach gate moved ahead of `BIT_NO_RANGED`/no-magic — instrumented, the
reorder-observable case never occurs), the `act_guard` facing gate and its
wake-on-sight rule, the `COMMAND_ATTACK` clinch breaker, `apply_z_motion` itself,
and the `decor_conceals_at` probes in `living::act`. [Correction (2026-10-05): false for the `fire_check` gate reorder in the old game: 11 goldens need it. The clinch breaker is inert, 0 of 223; see "#349 ports".]

Read the several "#132 AI fire cadence" attributions in the top drift table below with this
partition in mind: the ≈3-tick fire shift is M1 specifically, and M1 alone accounts
for only 9 of the 42 regenerated goldens. [Correction (2026-10-05): subtractively; additively in the old game M1 alone closes 28; see "#349 ports".] Rows whose signature is chase distance or
approach geometry rather than release tick are M2/M3. Entries above that were not
individually bisected still carry their original wording.

**Rule for these rows.** The mechanisms behind them are intended behaviour, deliberately shipped, and in M2/M3's case
explicitly audited at the time (`docs/GAMEPLAY_FIXES_FROM_CLASSIC.md`, the Westlands L2 "Forest Road" row). An
old-game capture over any of these ids is wrong until the corresponding mechanism is ported into the old game; then
the recapture IS the golden. M7 is moot: the two scenario tables have been byte-identical since `e9e1f051`, and
`effect_protection_emit_scen99` (M4 with M7) is closed (below).

Status of the 42 rows today: 41 still differ from the old game, plus two top-table rows on M6
(`beast_set_difficulty_invariant_scen99`, `generator_owner_cascade_scen99`) = 43: [Correction (2026-10-05): superseded: all 43 close, and the count paragraph is replaced by the closure note under Open; see "#349 ports".]

| id | differs from the old-game capture in | fix to port (current game → old-game site) | evidence |
|---|---|---|---|
| `consumable_inventory_state_scen99`, `family_ghost_scen99`, `invulnerable_potion_scen99`, `multiplayer_two_teams_scen99`, `smoke_nonempty_scen99`, `special_druid_2_scen99`, `special_slime_1_scen99`, `treasure_drumstick_pickup_scen99`, `treasure_stain_pickup_scen99` | The M1 signature (fire shifted ≈3 ticks earlier, same weapon coordinates) | M1 melee snap-face (`src/gameplay/stats.cpp` `COMMAND_ATTACK` + `walker::face_delta`) → the old game's `COMMAND_ATTACK` | M table above; all 42 pre-#132 goldens byte-matched a companion capture, and all 42 committed goldens byte-match BOTH the merge-base dump (`05eaaa23`) and the branch dump. |
| `effect_chain_scen9410`, `family_golem_scen99`, `special_archmage_scen123` | Chase distance / approach geometry | M2 A* no-corner-cut (`src/gameplay/gameplay_context.cpp`) → the old game's A* graph | As above (`05eaaa23`). |
| `effect_explosion_emission_scen99`, `effect_heartburst_multitarget_scen99`, `weapon_boulder_emission_scen99` | Chase distance / approach geometry | M3 follow-path alignment assist (`src/gameplay/walker_pathing.cpp`) → the old game's follow-path | As above (`05eaaa23`). |
| `family_archer_scen99`, `family_archmage_scen99`, `family_barbarian_scen99`, `family_big_orc_scen99`, `family_cleric_scen99`, `family_druid_scen99`, `family_elf_scen99`, `family_fireelemental_scen99`, `family_mage_scen99`, `family_medium_slime_scen99`, `family_skeleton_scen99`, `family_slime_scen99`, `family_soldier_scen99`, `family_thief_scen99` | Chase distance / approach geometry | M2 + M3 (both required) [Correction (2026-10-05): additive: M2 alone closes eight of these; six need both; see "#349 ports".] | As above (`05eaaa23`). |
| `family_orc_scen99`, `family_small_slime_scen99` | Fire timing and approach | M1 + M2 + M3 | As above (`05eaaa23`). |
| `effect_marker_emission_scen99`, `generator_saturation_scen99` | Crowded generator arenas | M5 shove command-theft probe (`src/gameplay/living.cpp` `living::shove`) with M6 → the old game's `living::shove` and `create_weapon` [Correction (2026-10-05): M6 alone closes both; see "#349 ports".] | As above (`05eaaa23`). |
| `generator_tent_emission_scen99`, `generator_tower_emission_scen99`, `generator_treehouse_emission_scen99` | Generator spawn hp | M6 / A12b: drop the `set_difficulty` call in `create_weapon`'s generator branch (current game `src/gameplay/walker.cpp`, "A12b: no set_difficulty here") → the old game's `src/walker.cpp:2332` | As above (`05eaaa23`). [Correction (2026-10-05): `generator_tent_emission_scen99` also needs A6/A7; see "#349 ports".] |
| `event_end_game_emission_scen99`, `event_notification_emission_scen99`, `event_request_redraw_emission_scen99`, `event_set_end_emission_scen99`, `event_set_palette_emission_scen99` | The event-arena fight | M7 is gone (tables byte-identical); whatever M1-M3 share these rows carry was never separated, so port M1-M3 and re-measure [Correction (2026-10-05): M1 alone closes all five; see "#349 ports".] | As above (`05eaaa23`). |
| `generator_owner_cascade_scen99` | The tent's escort skeleton has `max_hp` 104 on the branch and 148 on the companion; the tent therefore dies on tick 287 (branch) vs 298 (companion), the escort corpse holds 14 vs 35 hp, the demolisher finishes on 92 vs 8 of 180, `score_per_team[0]` is 199 vs 200, and the whole `weapon_tracks` / `rng_state` tail follows. The skeleton spawns on the SAME tick (143) on both arms, so the generator cadence itself is unchanged. | M6 / A12b → the old game's `src/walker.cpp:2332` | Deliberate master-era fix to double-applied generator difficulty. `openglad-master/src/walker.cpp:2315-2320` calls `set_difficulty(stats->level)` inside `create_weapon`'s `ORDER_GENERATOR` branch AND again from `fire()`'s generator arm once the spawn's own level is rolled, so every generator spawn compounded two hp boosts. The branch dropped the `create_weapon` call — see the "A12b: no set_difficulty here" comment at `src/gameplay/walker.cpp` — which is why the escort is a plain level-2 skeleton at 104 instead of a compounded 148. The merge base reproduces the branch's 104/287/92 exactly, so the fix predates this PR (it is also why the four existing `generator_*_emission` goldens are on the known-drift list). |
| `beast_set_difficulty_invariant_scen99` | Generator output. The branch's TREEHOUSE emits TWO golems at 318 hp; the companion emits FOUR at 336 hp, with two extra `play_sound` events. The generator itself, the `end_game`/`set_end` counts and the scores match. | M6 / A12b → the old game's `src/walker.cpp:2332` | The A12b double `set_difficulty`, already logged for `generator_owner_cascade_scen99`. `openglad-master/src/walker.cpp:2315-2320` calls `set_difficulty` inside `create_weapon`'s `ORDER_GENERATOR` branch AND again from `fire()`'s generator arm, so each companion spawn compounds two boosts (300 + 18 + 18 = 336 against the branch's 300 + 18 = 318); the heavier golems also change the `numobs`-driven emission rate, which is why the companion lands four. The branch dropped the `create_weapon` call ("A12b: no set_difficulty here"). Merge base reproduces the branch's two 318-hp golems exactly. [Correction (2026-10-05): M6 fixes the hp; the golem count needs M5 too; see "#349 ports".] |

### Top drift table (adjudicated 2026-07 to 2026-09)

| id | differs from the old-game capture in | fix to port (current game → old-game site) | evidence |
|---|---|---|---|
| **#132 fire cadence and approach (M1/F1, M2/M3)** | | | [Correction (2026-10-05): every row of this group closes with M1 alone, except `special_cleric_heal_ally_scen99`, which also needs heal and notif a/b; see "#349 ports".] |
| `archer_hit_response_backpedal_scen99` | `weapon_tracks` only (143 samples vs 142) and `rng_state`. First divergence: the archer's `FAMILY_ARROW` seq 0 is sampled at ticks 79-80 on the branch and only at tick 80 on the companion, i.e. it was released one tick earlier. Both walkers finish on identical coordinates and identical HP (soldier 109/120 at (224,120), archer 30/90 at (220,68)), and the 18 events match value-for-value. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | AI fire cadence — the multi-floor merge #132 (`c409e7c85`) shift already logged for the cleric, treasure, starburst and chain rows. The backpedal itself, which this row pins, is byte-identical across arms. Merge base reproduces the branch exactly. |
| `effect_chain_fork_scen99` | Archer timeline. The companion has one extra event (10 vs 9): a `play_sound` (a=1) at tick 10 that the branch does not emit, and its `play_sound` (a=13) lands at tick 19 where the branch puts it at 16. `score_change` amounts 108/175 (companion) vs 105/176 (branch). ARCHMAGE hp 93 vs 116, ARCHER ypos 156 vs 160 and hp 35 vs 36, the `FAMILY_HIT` marker at (130,128) vs (127,133), the `FAMILY_STAIN` effect ypos 126 vs 120. `weapon_tracks` 33 vs 37 samples, first differing at index 0 (`FAMILY_ARROW` seq 0, tick 42, ypos 152 vs 156). `rng_state`. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | AI fire cadence. The multi-floor merge #132 (`c409e7c85`) shifted it by a few ticks, which re-times the archer's arrow stream against the chain's hop schedule and moves every downstream position. The same shift is already logged for the cleric, treasure and starburst rows. The chain result itself is unchanged in kind on both arms — the ORC dies, the ARCHER takes a fork hit — so this is timing, not mechanism. Merge base reproduces the branch exactly. |
| `effect_knife_back_catch_scen99` | Arrow timeline. TOWER1 93 vs 94 and the thrower 75 vs 77 of 120; the companion carries one extra `FAMILY_HIT` walker at (138,129). 44 vs 43 events: the two streams agree tick-for-tick through sequence 17 (tick 59) and then drift up to 8 ticks apart, with the companion's `score_change` at 107 having no branch counterpart at that tick. `score_per_team[0]` 45 vs 44. `weapon_tracks` 324 vs 321 samples, first differing at index 61 (`FAMILY_ARROW` seq 10, tick 66 on the branch vs 65 on the companion, same coordinates). `rng_state`. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | The same #132 AI fire-cadence shift, here on the stationary TOWER1's return fire. Both arms run the full returning-knife economy — ten accepted releases, `FAMILY_KNIFE_BACK` tracks on both — and the one-tick arrow offset accumulates into a 1-2 hp and one-hit-marker difference over the 150-tick budget. Merge base reproduces the branch exactly. |
| `elf_rocks_pair_scen99` | `weapon_tracks` (18 vs 10) and the target orc's xpos only: it stalls at 205 on the branch and closes to 157 on the companion over the same 30 ticks, so the two rocks — whose first two samples are byte-identical on both arms — connect at tick 23 (173,126) on the companion and tick 27 (206,127) on the branch. HP (133/140), all five events, the score and the elf's own position are identical. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | The guard-standoff melee-approach change on a single `BIT_NO_RANGED` orc. The rock pair this row pins is identical in count, release tick and initial trajectory; only the flight length differs, because the target is further away. Merge base reproduces the branch exactly. |
| `fireelemental_starburst_ring_scen99` | Whether the flanking orcs reach the caster. On the branch they stall at (69,120)/(165,120) and the elemental finishes untouched on 100/100 with the orcs on 130/128; on the companion they close to (105,114)/(135,123), melee the elemental down to 11/100 and take 115/111. The branch records all EIGHT meteors in `weapon_tracks`; the companion records six, because the due-west and due-east meteors detonate on the already-adjacent orcs before their first sample (one diagonal also differs by a pixel, (144,104) vs (143,103)). `play_sound` 11 vs 14; only the branch awards score. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | The guard-standoff melee-approach change above — both orcs are `BIT_NO_RANGED`, so e761's slide-around approach reaches bump range inside 40 ticks and the branch's face-and-hold approach does not. The eight-heading fan this row pins is emitted on both arms at the same tick from the same origin. Merge base reproduces the branch exactly. |
| `mage_heartburst_multitarget_scen99` | Which orcs are in range when the burst lands. The companion's orcs reach melee first (`FAMILY_HIT` at ticks 13 and 14, which the branch has none of), so its detonations centre on (119,134)/(146,119) against the branch's (134,134)/(177,116): the branch kills two of three orcs and leaves one on 56/140 with the mage untouched on 90/90 (score 609), the companion kills one, leaves two on 57/56 and lets them melee the mage down to 28/90 (score 422). `weapon_tracks` 17 vs 19, `play_sound` 8 vs 9. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | The same guard-standoff melee-approach change; the three foes are `BIT_NO_RANGED` orcs and their positions at the tick-20 cast decide which of them `find_foes_in_range` acquires and how the (magicpoints-cost)/2 pool is split. Merge base reproduces the branch exactly. |
| `mage_starburst_ring_scen99` | Combat outcome of the eight-way fan: branch kills both the NW soldier and the SW tower and leaves the NE orc on 136/140 (team-1 alive 1, score 649); the companion kills only the soldier and leaves the tower on 22/130 and the orc on 134/140 (team-1 alive 2, score 439). Also the caster's HP (77 vs 30), `play_sound` ids/ticks, `rng_state` and the whole `weapon_tracks` block. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | The #132 single-floor AI/pathing wave, as a set (mechanism verified by bisection, 2026-08-01 — see "#132 single-floor fixes (M1-M6)" above): the melee snap-face on a `Facing`/`NoRanged` denial (M1, `src/gameplay/stats.cpp:582`) plus the two un-gated chase rules (M2 `gameplay_context.cpp:200`, M3 `walker_pathing.cpp:109`). The row was ported into the merge-base table and re-run: the pristine merge base differs from the companion in 380 fields, and the M1+M2+M3 compat probe returns the companion outcome byte-for-byte. Which of the three dominates was not separated here — the arena puts a `BIT_NO_RANGED` orc and a soldier 24 px diagonal from the caster, so M1 is genuinely reachable. Either way it re-times both the stationary tower's arrow stream and the orc's approach; with 1 px/axis/tick fan bolts and 24 px separations, a few ticks decide which bolt reaches which foe. Merge base reproduces the branch exactly. |
| `thief_taunt_matched_levels_scen99` | Two things. (1) The companion's stationary team-0 `FAMILY_TOWER1` decoy returns fire — `FAMILY_ARROW` tracks from tick 17 — while the branch's never fires one. (2) The melee-only foes close faster on the companion: the taunted orc ends at (132,147) against the branch's (156,132), the medium slime reads 76 vs 80 and the tower 110 vs 111. Consequently the companion awards two `score_change` (score 22 vs 0) and logs 5 `play_sound` against 8, with `weapon_tracks` 18 vs 12. Both arms emit the taunt notification at tick 20 and leave the thief untouched on 75/75. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | The guard-standoff melee fix (`src/gameplay/walker.cpp:1264-1287`, "guard-standoff fix, 2026-07-07"): `walker::fire_check` runs the reach gate BEFORE the `BIT_NO_RANGED`/no-magic gates so a denial now means the foe is genuinely within reach, and the `COMMAND_ATTACK` loop faces a bump-range foe instead of sliding around it. Both foes here are melee-only families and the decoy is a stationary shooter, so both halves of the fix are visible. Merge base reproduces the branch exactly. [Correction (2026-10-06): that capture predates the #132 M1 port; at `1bafb5da` neither game's decoy fired, and since #350 both fire at the foe; see "Sim-correctness ports".] |
| `treasure_gold_bar_team_reject_scen99` | `walkers[1].ypos` only: the chasing team-1 orc is at 181 on the branch and 151 on the companion after 90 ticks. Every other field (score, events, the surviving bar) is identical. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | **Pathing**, not fire cadence (mechanism verified by bisection, 2026-08-01 — see "#132 single-floor fixes (M1-M6)" above). The #132 squash un-gated two chase rules that had been `floor_count() > 1`-only: the A* no-corner-cut prune (M2, `src/gameplay/gameplay_context.cpp:200`) and the follow-path convex-corner alignment assist (M3, `src/gameplay/walker_pathing.cpp:109`). The row was ported into the merge-base table and re-run: the pristine merge base reproduces 181, and the M1+M2+M3 compat probe returns 151 byte-for-byte. M1 (the melee snap-face) is excluded by construction — it only fires on an in-reach `fire_check` denial, and this row's own design note records that the 90-tick budget keeps the orc out of melee. Over 90 ticks the re-timed chase costs 30 px of travel. [Correction (2026-10-05): M1 is not excluded: this row closes with M1 alone, and M2/M3 leave it byte-identical to the `ab0a7d2a` capture; see "#349 ports".] |
| `treasure_key_team1_silent_scen99` | `walkers[1].ypos` only: 181 (branch) vs 151 (companion). Same arena, same 90-tick budget as the gold-bar reject row; events, scores and the surviving key are identical. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | Same #132 mechanism as the row above — the two rows share the (96,400) orc geometry, the 90-tick budget and the identical field signature. NOT independently bisected; the attribution is inherited from the gold-bar row, which was. Merge base matches the branch. [Correction (2026-10-05): measured since: M1 alone closes it; see "#349 ports".] |
| `weapon_boulder_explode_damage_scen99` | Whole detonation timeline. The barbarian finishes on 122/150 (branch) vs 97/150, the blasted ORC on 23/140 vs 27/140 at (194,94) vs (183,123), the impact SOLDIER on 46/120 vs 44/120 at (128,132) vs (128,140); the companion's oblist also carries an extra spent `FAMILY_KNIFE_BACK` husk, which re-indexes `walkers[]`. `score_per_team[0]` 194 vs 192, one `play_sound`/`score_change` pair swaps order at ticks 9-10, and the knife/knife-back `weapon_tracks` run one tick apart. Both arms detonate on the same tick and leave the same three livings alive. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | AI fire cadence -- the multi-floor merge #132 (`c409e7c85`) shift already logged for the cleric, treasure, starburst and chain rows. Here it re-times the team-1 soldier's knife against the tick-8 boulder throw, which moves the collision point a couple of pixels and the shove that follows. Merge base reproduces the branch exactly. |
| `weapon_sprinkle_freeze_scen99` | Sprinkle contact tick. The companion's throw connects at tick 16, the branch's at 18, so the companion's orc thaws two ticks sooner and banks 21 px more of its westward charge: it finishes at xpos 131 / 24 hp against the branch's 152 / 29 hp, and the branch's `FAMILY_SPRINKLE` track carries two extra samples. `score_per_team[0]` 113 vs 118. Both arms freeze the orc and both leave the faerie untouched on 75/75. | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path | The same #132 AI-cadence shift. The victim is an AI orc closing on the caster, so a two-tick difference in when it enters throw range moves the whole freeze window. NOTE for future edits: the row's `WalkerPositionMoved` floor (140) sits between the branch's frozen 152 and its own unfrozen 131, and the cross-arm cadence drift here is 21 px -- the same order as the tooth. The blessed branch dump is what `og_test_parity` re-derives deterministically, so the row is stable, but a companion recapture must never be pasted over it. Merge base reproduces the branch exactly. |
| `special_cleric_heal_ally_scen99` **R** | AI/weapon cadence (`weapon_tracks` ticks and coordinates, `events[].tick`), `walkers[].hp` (cleric 116 vs 117, big orc 413 vs 412, faerie 6 vs 1), faerie/orc `xpos`/`ypos`, one extra master `FAMILY_HIT` walker, `score_per_team[0]` 17 vs 23, `play_sound` 14 vs 15, plus one branch-only `notification` ("Cleric healed 1 man!") | The #132 M1 snap-face and/or M2/M3 chase rules (see the M table) → the old game's `COMMAND_ATTACK`, A* graph and follow-path Part 2 is **R**: the old game gates the "Cleric healed N men!" notice on `heal_numbers` (`src/walker.cpp:2646`). | Two causes. (1) AI fire cadence: the multi-floor merge #132 (`c409e7c85`) shifted it by a few ticks, which re-times the whole faerie/orc melee. (2) Heal-number split: the companion gates the "Cleric healed N men!" notify on `if (!cfg.is_on("effects","heal_numbers"))` (`openglad-master/src/walker.cpp:2632`) and `cfg_store::load_settings` defaults that setting on, so the companion emits nothing; the branch moved heal-number rendering to `walker_draw.cpp` and `cleric.lua` notifies unconditionally. Both reproduce identically at the merge base. [Correction (2026-10-05): part 1 is M1 alone; part 2 is the heal_numbers gate and the notification form; see "#349 ports".] |
| **A12b / M6 on generator-emitted livings** | | | |
| `generator_bones_emission_scen99` | `walkers[].hp` and `walkers[].max_hp` on all five emitted GHOSTs, and nothing else: 61 on the branch, 72 on the companion. The BONES generator itself, the emission ticks, all 151 events, `rng_state`, `score_per_team` and the team census are identical. | M6 / A12b → the old game's `src/walker.cpp:2332` | **M6 / A12b**, the double `set_difficulty` already adjudicated for `generator_owner_cascade_scen99` and `beast_set_difficulty_invariant_scen99` above: `openglad-master/src/walker.cpp:2315-2320` scales a generator spawn inside `create_weapon`'s `ORDER_GENERATOR` branch AND again from `fire()`'s generator arm, so each companion GHOST compounds two level-1 boosts (50 + 11 + 11 = 72) where the branch applies one (50 + 11 = 61). The #132 wave rebaselined the tent, tower and treehouse emission rows for exactly this and missed the bones row; this is that row catching up. Merge base `18adcafd` reproduces the branch's 61 byte-for-byte — the whole dump is byte-identical, which is this table's blessing condition. Teeth: `kFacts_generator_bones_emission_scen99` gained `WalkerHpRangeAtFinalTick(FAMILY_GHOST, 6100, 6100)`, which fails against the pre-rebaseline golden's 72. |
| `special_mage_scen126` | `walkers[3].hp` and `walkers[3].max_hp` only — the team-1 MAGE the TOWER generator emits: 97 on the branch, 104 on the companion. The player's team-0 mage (86/90), the TOWER, the SOLDIER, all four events, `rng_state` and the score are identical. | M6 / A12b → the old game's `src/walker.cpp:2332` | The same **M6 / A12b** double `set_difficulty`: 90 + 7 + 7 = 104 on the companion against the branch's 90 + 7 = 97. Missed by the #132 wave for the same reason the bones row was — the scaling shows up here on a generator-emitted *Living*, not on a row named `generator_*`. Merge base `18adcafd` reproduces the branch byte-for-byte. Teeth: `kFacts_special_mage_scen126` gained `WalkerHpRangeAtFinalTick(FAMILY_MAGE, 9700, 9700)`, which fails against the pre-rebaseline golden (its two mages read 86 and 104). |
| **Recorder representation (R)** | | | |
| `undead_no_corpse_raise_scen99` **R** | intra-tick event ORDER only, at tick 24: branch `score_change, end_game, set_end`; companion `end_game, set_end, score_change`. Identical values, identical tick, identical walkers/effects/scores. | **R**: the golden capture tool synthesizes `score_change` from an end-of-tick score snapshot (`tools/parity_dump_master.cpp:553-558`); a recorder-only commit emitting it at the award site | Recorder artifact, not gameplay. The companion synthesizes `score_change` from an end-of-tick `m_score` before/after snapshot (`openglad-master/tools/parity_dump_master.cpp:441,558`) while the branch emits it inline at the award site (`walker_combat.cpp:99`). Merge base matches the branch exactly. |
| `elemental_death_starburst_scen99` **R** | intra-tick event ORDER only, at tick 22: branch `score_change, end_game, set_end`; companion `end_game, set_end, score_change`. Same tick, same values, and every walker, weapon, weapon track and score is identical (the frozen weaplist holds the same eight `FAMILY_METEOR` on both arms). | **R**: same `score_change` synthesis (`tools/parity_dump_master.cpp:553-558`) | Recorder artifact, not gameplay — the same mechanism already logged for `undead_no_corpse_raise_scen99`. The companion synthesizes `score_change` from an end-of-tick `m_score` before/after snapshot (`openglad-master/tools/parity_dump_master.cpp:441,558`) while the branch emits it inline at the award site (`src/gameplay/walker_combat.cpp:99`), so it lands before rather than after the completion pair. Merge base matches the branch exactly. |
| `thief_charm_opponent_scen99` **R** | `events[2].a` only: the "Thief charmed an opponent!" notification carries `a = 0` on the branch and `a = 11` (`FAMILY_THIEF`) on the companion. Walkers, teams, HP, scores, event ticks and every other field match exactly. | **R**: Notification `a` (see the R paragraph above) | Notification metadata, the same mechanism already logged for `treasure_magic_potion_overfill_scen99`: the companion's `screen::do_notify` records `a = who->query_family()` (`openglad-master/src/screen.cpp:1379-1384`) while `SimEventLog::push_notification` has no notifier argument and puts the display `duration` in `a` (`src/gameplay/sim_event_log.cpp:42-53`). The harness papers over three classic texts by name in `normalize_classic_notification_metadata` (`tests/parity/parity_runner.cpp:197`) and this text is not one of them. Merge base reproduces the branch's `a = 0`. |
| `mage_teleport_marker_scen99` **R** | `events[0].a` and `events[1].a` only: the "Teleport Marker Placed" / "(3 Uses)" notifications carry `a = 0` on the branch and `a = 3` (`FAMILY_MAGE`) on the companion. The mage lands back on its marker at (240,320) on both sides and the `FAMILY_MARKER` walker is identical. | **R**: Notification `a` (see the R paragraph above) | Same notification-metadata mechanism as the row above. Merge base reproduces the branch's `a = 0`. |
| `archmage_teleport_marker_scen99` **R** | Notification `a` (0 vs 17) on every notice, plus a whole missing notification on the branch (companion has 5: Placed / (3 Uses) / (Old Marker Removed) / Placed / (3 Uses); branch has 4, no removal notice), no `FAMILY_MARKER` walker on the branch at all, and the resulting `rng_state` / `weapon_tracks` divergence. | Part 1 **R**: Notification `a`. Part 2: the marker's `ani_type 2` past the family table — the current game's `effect::animate` bound (`src/gameplay/effect.cpp`) → the old game's unbounded `set_frame(ani[curdir+ani_type*NUM_FACINGS][cycle])` (`src/effect.cpp:563`) | Two causes. (1) The notification-metadata mechanism above. (2) `archmage.lua` sets the marker's `ani_type` to a raw `2` — faithful to `openglad-master/src/walker.cpp:3066-3120` — but `effect::animate` on the branch bounds `ani_index = curdir + ani_type*8` against the family's real table length (`src/gameplay/effect.cpp:114-127`, the `ani_count` OOB guard). The marker family's table has 16 rows, so `ani_type 2` indexes past it, the guard resets the effect to `ANI_WALK`, and `effect::act` reaps it on the next tick. e761 has no bound, reads past the table, and the marker survives — hence its live marker, its removal notice on the second cast and its marker-targeted return teleport. `mage.lua`'s `ANI_SPIN` (== 1) twin stays inside the table and is unaffected, which is why `mage_teleport_marker_scen99` still matches. Merge base reproduces the branch exactly, so the guard predates this PR. |
| `orc_eat_corpse_scen99` **R** | One event only: the branch emits a `notification` ("Orc ate a corpse.") at tick 48 that the companion does not, so the branch's `events[]` is one entry longer and every later `end_game` shifts one sequence number. Walkers, HP (the orc is on exactly 79/140 on BOTH arms, i.e. the companion ate the corpse too), positions, scores, `rng_state` and `weapon_tracks` are identical. | **R**: the old game gates the eating notice on `heal_numbers` (`src/walker.cpp:3951`); the current game notifies unconditionally | Heal-number notification gate — the same mechanism already logged for `special_cleric_heal_ally_scen99`. The companion wraps the eating notice in `if (!cfg.is_on("effects","heal_numbers")) myscreen->do_notify(message, this)` (`openglad-master/src/walker.cpp:3936`) and `gparser` defaults that setting on, so e761 stays silent. The gate is already gone at the merge base — `openglad-mastertip/src/gameplay/families/family_orc.cpp:88` emits the notification unconditionally — and a merge-base dump of this row is a byte-for-byte MATCH with the branch, so the Lua conversion did not introduce it. [Correction (2026-10-05): two mechanisms: the heal_numbers gate and the notification `a` (14); see "#349 ports".] |
| `effect_bomb_bystander_scen99` **R** | `rng_state` ONLY: `0x7920175E` (branch) vs `0x0DB04883` (companion). Every walker, event, effect, score, weapon and weapon-track field is identical, including the bystander on exactly 39/130 and the shoved thief on 55/75 at (205,205). | **R**: the parity test driver's `rng_state` hash mixes each draw's bound (`tests/parity/parity_runner.cpp:70-75`); a bound differs in one explosion damage draw | Bound-only RNG draw. The schema-v1 `rng_state` is a rolling hash that mixes each `random(x)` call's BOUND as well as its returned value (`tests/parity/parity_runner.cpp:70-75`), so two arms that roll identical values can still hash differently if one call took a different `max_exclusive`. Every damage roll here produced the same result on both arms (identical HP everywhere), which rules out a value or ordering difference and leaves a bound difference in one of the explosion's `get_base_damage` draws. (Correction, 2026-08-01: an earlier version of this entry claimed the owner tier's damage is refused by `is_friendly` and only its bound reaches the hash. Wrong — `is_friendly` answers 0 for a dead caller and an explosion is `set_dead` before `death()` runs, so the quarter tier LANDS: the thief's 75→55 is its own bomb, identically on both arms. The bound-difference conclusion stands; the refusal narrative does not.) The two neighbouring bomb rows that do not blast a surviving foe — `effect_bomb_emission_scen99` and this batch's `effect_explosion_range_scen99` — both match the companion exactly, `rng_state` included. The merge base reproduces the branch's `rng_state` byte-for-byte. [Correction (2026-10-05): not an explosion draw and not a recorder difference: the differing bound is `set_weapon_heading`'s waver at tick 95, 5 vs 6, after M1's `face_delta` on a stationary TOWER1 at tick 94; M1 alone closes the row; see "#349 ports".] |
| **Rounding and animation** | | | [Correction (2026-10-05): this group is empty: neither row is rounding; see "#349 ports".] |
| `elf_mega_rocks_volley_scen99` | The volley's per-rock x-step and everything downstream. At tick 22 the four rocks sit at x 136/137/137/136 on the branch and 138 on all four on the companion. The companion's rocks therefore kill BOTH orcs (level complete, `level_done` 2, 19 `end_game`, score 810, two `FAMILY_HIT` husks); the branch kills one and leaves the other on 134/140 at (167,135), `level_done` 0, score 401. `weapon_tracks` 72 vs 34, `play_sound` 7 vs 9. | `next_spread_multiplier` `og.cosmetic_rand` rounding plus the F1 melee approach (current game) → the old game's elf volley spread and `COMMAND_ATTACK` | `next_spread_multiplier`'s `og.cosmetic_rand` rounding, the one-pixel divergence `weapon_rock_slot2_emit_scen99`'s golden already documents, compounded by the guard-standoff melee-approach change that decides where the two `BIT_NO_RANGED` orcs are standing when the bouncing rocks arrive. Merge base reproduces the branch exactly. [Correction (2026-10-05): no rounding mechanism exists, and `weapon_rock_slot2_emit_scen99`'s golden documents no one-pixel divergence (it is an old-game capture whose rolls and arithmetic match bit for bit); M1 alone closes this row; see "#349 ports".] |
| `mage_freeze_time_offteam_scen99` | The AI mage's itinerary after tick 132. First divergence is a 1-2 px x-step on the mage's first `FAMILY_FIREBALL`: (549,496)/(550,488) on the branch against (550,496)/(552,488) on the companion at ticks 132/133. From there the arms take different ACT_RANDOM special rolls — the branch teleports and leaves a live `FAMILY_MARKER` at (346,470) with the mage at (64,656) and eight live fireballs; the companion casts ENERGY WAVE (a `FAMILY_WAVE`/`FAMILY_WAVE2` pair the branch never emits), keeps no marker and parks the mage at (512,784). Both arms hold the mage on 86/90 and the orcs on 135/132, both emit exactly one `play_sound`, and both kill the parked player soldier at tick 137. | Projectile step rounding on the fireball release (current game) → the old game's fireball step | Projectile step rounding on the fireball release, the same class of one-pixel divergence already logged for the elf rock rows. The mage's next special roll is decided by where that fireball leaves it, so a one-pixel step forks the whole 170-tick tail. The freeze-time grant this row pins fires at tick 294 on both arms. Merge base reproduces the branch exactly, marker included. [Correction (2026-10-05): not rounding: M2+M3+M5 and the runaway-specials 2.12 starburst cap, which the 900-MP mage binds; M1 plays no part; see "#349 ports".] |
| **Score attribution** | | | |
| `cleric_resurrect_friendly_scen99` | `score_per_team` `[7,23]` vs `[136,0]`, `score_change` event count/amounts, event ticks (companion runs ~5-8 ticks ahead), `walkers[1].hp` (cleric 91 vs 90), `rng_state` | Credit the actual player team (current game `src/gameplay/walker_combat.cpp`, `playerteam == world->my_team`) → the old game's `walker::attack`, which resets `playerteam = 0` (`src/walker.cpp:2029`) | Score attribution. The companion decides `getscore` with `myguy != NULL \|\| team_num == 0` and then hard-resets `playerteam = 0` before the award (`openglad-master/src/walker.cpp:1884, 2015`), so with this row's `player_team = 1` every point still lands on team 0. Branch and merge base both use `playerteam == world->my_team` (`src/gameplay/walker_combat.cpp:266`), which credits the actual player team. Plus the #132 cadence shift. This is the corpus's first `player_team != 0` row, which is why no earlier golden surfaced it. [Correction (2026-10-05): the golden's score is `[8,27,0,0]`, not `[7,23]`; the event shift was the score attribution itself; the rest closes with M1; see "#349 ports".] |
