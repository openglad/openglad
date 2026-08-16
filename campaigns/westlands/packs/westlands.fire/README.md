# westlands.fire — The Company Fire

Hand-authored campaign pack for War of the Westlands (generator-exempt:
`export_campaign_tree` never touches `packs/` or this file; the level
content around it is regenerated wholesale by `tools/westlands_mapgen`,
which stages this tree into its self-checked package).

The MISSIONS button opens the company's banked fire, not a menu: THE ROAD
(level select in the fiction's words, mirroring the shipped exit graph),
the QUARTERMASTER (wages, bread, pack-weight), and THE LEDGER (a lines-only
recap of what the road has cost).

## Decisions and their keys

| Key | Set by | Sim consequence |
|---|---|---|
| `watch_paid` | THE WATCH'S PAY, 900g, offered once level 7 is cleared | Level 15: Wall-Warden (lvl 6) and two Watchmen (lvl 5) join the gate |
| `delve_counted` | COUNT THE DELVE GOLD (+800g), offered while `completed(9) and not completed(11)` | Level 11: two Gold-Wraiths (team-2 ghosts, lvl 6) rise off the far bank |
| `provisions` | PROVISION PACKS, 300g each, capped at 3 | Levels 13-17 and 19-23: one drumstick per tier, plus a magic potion from tier 2, beside the lead start marker |
| `sneak_bread` | BREAD FOR SNEAK, 60g, offered in the 19..21 window | Level 21: Sneak waits at lvl 5 on half strength (property writes; the betrayal itself stands) |
| `delve_sunk` | SINK IT IN THE RIVER — **not a registered var**; narration only | None, by construction |

Level 26: when `watch_paid` and `sneak_bread` are set and `delve_counted`
is not, The Pilgrim (team-0 archmage, lvl 10) waits on the quay.

Every consequence hook's first statement is its `var == 0` early return, so
with no decisions recorded the shipped levels run byte-identical (all
calibration floors, battle smokes, and parity goldens hold). All spawn
tiles are fixed data in `lib/spawns.lua`; the pack draws no randomness
anywhere. Spawned allies keep the default ACT_RANDOM (they are relief that
marches, not posts that hold — the mapgen allied hold-post rule binds only
ACT_GUARD placements).

The COUNT/SINK pair and both priced kindnesses are one-shot (the row
vanishes once its key is set, and each action re-checks its key against a
direct dispatch), and the provision cap hides the row at tier 3 — so the
26→1 replay loop and the backtrack exits can re-farm score, but never
re-open a source or a sink.

## Networked play

Campaign state is per-machine, like the campaign cursor: the host's picker
drives the shared session, while each company file keeps its own decision
book — a guest's ledger speaks only for its own company, never for the
table. On a dedicated server every var reads 0 and the levels run stock.
