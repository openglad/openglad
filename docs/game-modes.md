# Game Modes — the Two-Tier Seam

How OpenGlad adds game modes without forking the engine. The seam ships in
`og_resources`:

- `include/openglad/resources/game_mode.h` — `og::mode::IProgression`, the
  `ProgressionKind` dispatch, and mode identity.
- `include/openglad/resources/progression.h` — the shared level-win fold
  (`og::progression::apply_win_fold`) and the CTF rematch predicate.

## Taxonomy

**Tier A — sim-ruleset modes** (CTF today; a future horde mode) change what
happens *within a tick*: win conditions, team-wipe suppression, per-tick
scoring. They live in `og_gameplay`, are deterministic, snapshot-visible,
parity-relevant, and irreducibly expensive. Tier A gets **no abstraction** —
it gets the recipe below, paid in full per mode.

**Tier B — meta-progression modes** (Tower Climb; future boss rush / time
attack / draft) change what happens *between levels*: sequencing, run
lifecycle, persistence policy, results. They live behind
`og::mode::IProgression`. Design theorem: **a Tier-B mode must be
implementable with no `og_gameplay` edits and no wire changes.** Tower Climb
is the existence proof. The tiers compose: a future mode may pair a Tier-B
object with a Tier-A engine.

**Classic-respawn is NOT a mode.** It is a shared engine (`CtfState` respawn
substate with `ctf.active == false`) that composes with modes via the
`respawn_mode` knob; the seam's `clamp_respawn_mode` hook is how a mode
constrains it.

## Mode identity

Source of truth: campaign package metadata. `campaign.yaml` carries an
optional `mode:` key (absent/`classic` → Classic; `tower` → Tower; unknown
string → Classic + one LogError). Old binaries silently ignore the key and
load the package as an ordinary campaign — graceful degradation.

The writer emits `mode` **only when non-empty**, keeping every existing
(mode-less) campaign repack byte-stable. This is load-bearing and pinned by
`tests/unit/test_game_mode.cpp`.

`og::data::mounted_campaign_mode()` (campaign_metadata.cpp) memoizes the
mounted campaign's mode string and is invalidated with the display-title
cache on mount changes. `og::mode::current_progression()` dispatches on it —
nothing new to sync, nothing new on the wire, ever (the lobby already syncs
`campaign_id`).

Forward sim identity: tower levels are authored with the
`SCEN_TYPE_TOWER`/`GameWorld::TYPE_TOWER` bit (0x10). Display-only in v1;
never runtime-mutated; a level authored with both the CTF bit (0x8) and the
tower bit resolves CTF-first.

## The shared win fold

The four historically duplicated win-finalize sites now converge on
`og::progression::apply_win_fold`:

| Site | File | Kept site-owned |
|---|---|---|
| `screen::endgame` win block | `src/interface/screen.cpp` | `get_time_bonus` source, `!networked` + `persist_after_win()` save0 tail |
| `finalize_level_and_advance_cursor` | `src/platform/sdl/local_transport_shadow.cpp` | `get_time_bonus` source, netsession + owned-merge persist tail |
| `complete_headless_level_and_load_next` | `src/server/headless_server_runtime.cpp` | `calculate_headless_time_bonus`, primary-team totals, checkpoint + reload tail |
| `advance_save_after_win` | `src/platform/curses/curses_game_runtime.cpp` | zero time bonus (historical), caller-resolved `next_level < 0 → scen_num+1` |

Fold order (donated by the shadow site): fold `m_score` into
`m_totalscore`/`m_totalcash(×2)` → time bonus iff `!already_completed` →
zero `m_score` → `add_level_completed` (gated on
`marks_level_completed()` and the CTF rematch shape) → cursor advance
through `advance_cursor` (which may HOLD) + `current_levels` write →
`update_guys`. Callers sync save-from-world FIRST, compute the bonus from
the LIVE `m_score` SECOND, fold THIRD, then run their persist tails.

**Idempotence contract:** in local play both the server-side shadow finalize
and the display-side `screen::endgame` fold. A second pass must be a no-op;
`advance_cursor` implementations must be idempotent. Callers fill
`WinFoldContext::finished_level` from the pre-fold cursor — that is how a
re-entrant second fold on the same SaveData detects the already-advanced
cursor and skips the bonus/mark/advance steps instead of marking the
destination level completed. Pinned by the apply-twice == apply-once test in
`tests/unit/test_progression.cpp`.

## Run-end routing

`IProgression::on_run_ended` fires once per non-win exit (Classic: no-op) at:

- `screen::endgame` top, before the results screen, for every `ending != 0`
  (team wipe, timeout, withdraw, protect-fail).
- `finalize_withdraw_and_advance_cursor` (local transport shadow) — the
  server-side withdraw/quit-mission path.
- `local_transport_shadow_abort_level` (local transport shadow) — Esc-abort /
  quit-to-menu on the authoritative server save. This exit never passes
  `screen::endgame` or the withdraw finalize on the server side, so it is a
  routing site of its own; it pairs with the display-side `screen::endgame`
  hook that fires on the mirrored save when the broadcast `world.end`
  arrives.
- `withdraw_headless_level` (headless server runtime) — dedicated-server
  withdraw/quit; a dedicated-server team wipe mutates no server save, so
  each display client routes its own hook.
- `LocalCursesSession::commit_result_to_save` loss branch (curses client).

A single conceptual exit may route the hook on more than one SaveData (the
server's and the display's, mirrored state) — implementations must be
idempotent, like the fold.

## Results surfaces

`suppress_retry()`, `ending_popup()`, and `results_summary_lines()` are the
mode's results hooks, and both frontends already dispatch on them:

- Curses: `mission_verdict_line` (via its optional save/world parameters)
  appends `results_summary_lines`.
- SDL (`results_screen.cpp`): `show_mode_ending_popup` is consulted at the
  top of every ending branch, before the CTF popup; the RETRY button is
  hidden and its click/nav paths gated on `suppress_retry()`; the overview
  page injects `results_summary_lines` after the CTF banner block.

Classic returns nothing everywhere, so legacy surfaces render unchanged.
Only the CTF popup itself (`show_ctf_ending_popup`) remains hardcoded in the
dispatch chain — migrating CTF onto `ending_popup` is optional and out of
scope.

## Tier-A recipe (NO seam code — every anchor verified on this branch)

A sim-ruleset mode = all of the following, paid in full:

1. Core identity: a `SCEN_TYPE_*` bit (core/constants.h) + matching
   `GameWorld::TYPE_*` constant (game_world.h).
2. POD value-struct state embedded on `GameWorld` beside `CtfState ctf`
   (game_world.h:397; leaf-header discipline per ctf_state.h:1-14), reset at
   level load, and listed in BOTH `LevelRuntimeData` copy lists + the
   old-world reset (level_runtime_data.cpp:275-277, 623-625, 644).
3. A `<mode>_run_tick` free function joining the tick fork at
   game_world.cpp:1785-1828, with the lazy-init `init_attempted` latch
   (ctf.cpp:1018-1024) and **the win latch that RE-ASSERTS
   `game_ended/ending/next_level` every tick** (tick entry resets them,
   game_world.cpp:1617-1621 — this trap has bitten twice).
4. A clause in `respawn_suppress_team_wipe_endgame` (ctf.cpp:1161-1183; all
   three consumers — view.cpp:1393, game_server.cpp:1414/2148 — already gate
   on it).
5. If replicated: a snapshot block appended AFTER the CTF block
   (world_snapshot.cpp:807/841/2393/2839/2969 + OG_REPLAY_COMPARE), plus the
   protocol/snapshot/replay triple version bump and the 5 literal wire-byte
   test re-pins (test_net_transport.cpp:240, 829-830, 858-859, ~2654;
   test_input_state_net.cpp:133, 149).

Grafted disciplines: (a) if the tick-fork condition is ever refactored, the
CTF gate `!(init_attempted && !active)` must be reproduced EXACTLY,
including owning the failing-init tick — pin with a 4-state truth-table unit
test; (b) route new declarations into game_world.cpp via game_world.h
includes, never by adding `#include` lines to game_world.cpp (mutation-canary
pins 1620/1622 sit high; any line added above them shifts them silently).
Create a `mode_run_completion_tick` dispatcher only when a THIRD sim engine
appears.

## The future-mode checklist (mode #3's bill, row by row)

1. **Identity**: a campaign.yaml `mode:` string + a `kind_for_mode_string`
   case + a dispatch case in `progression_for_kind` (the exhaustive
   no-default switch makes a missed case a compile-time complaint).
2. **One `IProgression` subclass** (stateless if possible; persistent stats
   = version-gated SaveData fields, append-only).
3. **Content**: a campaign package (kShelf slot + shelf-test re-pin in
   tests/unit/test_picker_common.cpp) and/or a generator over the mapgen
   builder library.
4. **Sequencing/persistence**: `advance_cursor` / `marks_level_completed` /
   `persist_after_win` / `on_run_ended` overrides — ZERO edits to the four
   finalize sites.
5. **Results**: `ending_popup` / `results_summary_lines` / `suppress_retry`
   — ZERO edits to the results_screen dispatch.
6. **Knob policy**: `clamp_respawn_mode` (generalize to a knob-clamp struct
   when a second knob needs clamping).
7. **Sim rules** (only if genuinely new): the Tier-A recipe above, paid in
   full.

Cross-cutting invariants that bind every mode: deterministic SDL-free sim
(no `world.rng_` / `ctx().rng` / libc `rand()` on existing-content paths);
single-player byte-identity for content that doesn't opt in; zero wire
changes for Tier-B; append-only save format; no wall-clock timing in tests;
mutation-canary pin hygiene (grep `"src/` in tests/parity/scenario_table.h
before every commit touching a pinned file).

Deferred upgrades, named so they land on the right feature's bill:

- **Results-provider hoist** (a routing object instead of per-surface `if`
  dispatch): pay it at mode #4, when the accumulation is real.
- **In-session (non-return-to-menu) level advance**: every mode currently
  returns to the team-build menu between levels; a seamless mode must fund
  the currently-unreachable in-session reload path
  (`prepare_clients_for_loaded_level` beyond return-to-lobby mode).
- **Editor repack of moded packages**: the editor's campaign-info round-trip
  (`campaign_data_to_yaml`, level_runtime_data.cpp) does not carry `mode:`;
  editing a moded campaign's info in the editor drops the key. Acceptable
  while mode packages are generator-built; fund `CampaignData.mode` when a
  mode package becomes editor-authored.
