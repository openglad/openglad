# Golden drift ledger

Goldens listed here were captured from the branch, not from the e761 companion
(`../openglad-master`, branch `parity-companion`), because a fresh companion
capture disagreed with the branch. Each entry was adjudicated the same way: the
scenario row was patched into the PR's merge base `05eaaa23`
(`../openglad-mastertip`), its `parity_runner_smoke` rebuilt, and the merge-base
dump diffed against the branch dump. `branch == merge base` means the
divergence is a master-era change that predates this PR, so the branch dump is
the honest golden. `branch != merge base` is a PR regression and is never
blessed — it is reported instead.

| id | fields differing from the companion capture | suspected master-era cause |
|---|---|---|
| `special_cleric_heal_ally_scen99` | AI/weapon cadence (`weapon_tracks` ticks and coordinates, `events[].tick`), `walkers[].hp` (cleric 116 vs 117, big orc 413 vs 412, faerie 6 vs 1), faerie/orc `xpos`/`ypos`, one extra master `FAMILY_HIT` walker, `score_per_team[0]` 17 vs 23, `play_sound` 14 vs 15, plus one branch-only `notification` ("Cleric healed 1 man!") | Two causes. (1) AI fire cadence: the multi-floor merge #132 (`c409e7c85`) shifted it by a few ticks, which re-times the whole faerie/orc melee. (2) Heal-number split: the companion gates the "Cleric healed N men!" notify on `if (!cfg.is_on("effects","heal_numbers"))` (`openglad-master/src/walker.cpp:2632`) and `cfg_store::load_settings` defaults that setting on, so the companion emits nothing; the branch moved heal-number rendering to `walker_draw.cpp` and `cleric.lua` notifies unconditionally. Both reproduce identically at the merge base. |
| `cleric_resurrect_friendly_scen99` | `score_per_team` `[7,23]` vs `[136,0]`, `score_change` event count/amounts, event ticks (companion runs ~5-8 ticks ahead), `walkers[1].hp` (cleric 91 vs 90), `rng_state` | Score attribution. The companion decides `getscore` with `myguy != NULL \|\| team_num == 0` and then hard-resets `playerteam = 0` before the award (`openglad-master/src/walker.cpp:1884, 2015`), so with this row's `player_team = 1` every point still lands on team 0. Branch and merge base both use `playerteam == world->my_team` (`src/gameplay/walker_combat.cpp:266`), which credits the actual player team. Plus the #132 cadence shift. This is the corpus's first `player_team != 0` row, which is why no earlier golden surfaced it. |
| `undead_no_corpse_raise_scen99` | intra-tick event ORDER only, at tick 24: branch `score_change, end_game, set_end`; companion `end_game, set_end, score_change`. Identical values, identical tick, identical walkers/effects/scores. | Recorder artifact, not gameplay. The companion synthesizes `score_change` from an end-of-tick `m_score` before/after snapshot (`openglad-master/tools/parity_dump_master.cpp:441,558`) while the branch emits it inline at the award site (`walker_combat.cpp:99`). Merge base matches the branch exactly. |
