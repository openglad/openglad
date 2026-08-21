# The Staged Lobby (#218)

Every lobby assembles the match's REAL world before GO, previews that world,
and launches by ADOPTING it. "Preview == launch" is not an agreement between
two derivations any more — it is one world, byte-testable end to end.

## The one staging pipeline

`og::server::MatchStage` (`include/openglad/server/match_stage.h`,
`src/server/match_stage.cpp`, SDL-free) is owned by each lobby OWNER: the SDL
solo/split picker (`LocalPickerLobbyClient`), the SDL network host
(`HostPickerLobbyClient`), both curses host construction sites, and the
dedicated server's lobby loop. It builds the staged world through the
dedicated-server pipeline — `apply_headless_lobby_game_start_config` +
`load_headless_level_from_save` + `spawn_team_from_save` — then runs level
`on_load` (`GameWorld::run_pending_level_on_load`, the verbatim factor of the
tick-side dispatch) and mode init (`og::sim::mode_stage_init`, the factored
step 0 of `mode_run_tick`; the lazy tick-1 arm serves un-staged worlds) for
real, at stage time. Header residency note: `match_stage.{h,cpp}` and
`headless_server_runtime.{h,cpp}` keep their `openglad/server/` paths while
being compiled into `og_interface` AND the headless source lists — the
`level_runtime_data.cpp` dual-listing precedent (no headless binary links
`og_interface`, so no duplicate symbols).

**Determinism (the match-id latch).** The owner draws a `match_seed` from
non-sim entropy once per round (re-latched at `resume_after_level`); every
restage pins `world.rng_.state_` AND the process-global weather roll sequence
to it before the load. Identical `MatchStageInputs` therefore produce a
byte-identical staged keyframe — the preview can never flicker, and the #235
squad permutation is drawn once from the pinned stream and latched in a
replicated mode var.

**The change key** is exactly the launch inputs — the lobby save equivalent,
the player bindings, the difficulty, the seed, the replay arm, and a digest
of the host save's campaign-state book + completed levels
(`host_save_stage_digest`; stamped by `observe_inputs` each poll and
re-checked by `ensure_current` at GO, so a mid-lobby `og.campaign_state_set`
write restages a reactive level instead of launching the pre-decision world)
— computed per poll from the SAME functions the launch consumes. Ready bits
and denials are deliberately excluded (a ready-up flip never restages).
Restage is always dispose-and-rebuild behind a 250 ms trailing-edge debounce;
GO forces `ensure_current()` so a stale stage can never launch; a failed
stage denies GO through `StartDenialReason::StageFailed`
(`LobbyServer::set_start_gate`).

**Dormancy.** Nothing constructs a `GameServer` over the staged world, so
nothing can tick it or drain its announcement queue. `on_load`/init
announcements are queued once per stage, stamped tick 1, and appended into
the live session's log at adoption — the first ticked drain delivers them.

## The wire (protocol v13)

`StagedMatchSetup` (23) and `StagedMatchKeyframe` (24) wrap a COMPLETE
InitialSetup message and a COMPLETE Peek keyframe of the staged world,
generation-stamped and broadcast after every restage (per-peer catch-up for
late connectors). Joiners own a `StagedPreviewMirror`: the retained newest
generation-paired bytes applied into a headless LOCAL level load — mandatory,
since a snapshot cannot rebuild a level — with honest
Unavailable/retry-on-campaign-catch-up degradation. Snapshot format stays
v10 (mode/respawn/RNG/weather/control policy already replicate); replay stays
v15.

## The preview reads the staged world

`build_scenario_roster_report(staged, status, save, fallback_world)` censuses
OBSERVABLE facts of the world the launch adopts: mode name from the
replicated `ModeState`, activity from the live per-team census, fills from
provenance (`COMPANY` = has_guy; `MAP TROOPS` = unmarked livings;
`BOT SQUAD`/`MATCHED BOTS` = the modes.core `BOT_MARK_BIT` stat tag, matched
per the shared `MATCHED.SIZE` var; `GENERATORS`), anchors from the real
staged scan. Refusal disambiguates through `level_hook_kinds_for`: an
attempted-and-refused `on_mode_init` keeps the verbatim
"MATCH WILL NOT START: FEWER THAN 2 TEAMS"; a hook-less scripted level takes
the count-only `effective_team_mask` fallback. Dormant (delayed-spawn)
walkers are excluded exactly as the keyframe capture excludes them — the
documented carve-out; they reveal at their authored tick after launch. The
carve-out runs on BOTH sides of the pane: `match.census_inputs`
(mode_match.lua) skips dormant walkers too, through the read-only
`w:dormant()` binding, so the activation decision and the rendered census
never disagree about a delayed-spawn team.

Surfaces: SDL VIEW LEVEL renders the staged world in a 303x76 band (the
render copy is the SDL-hooked scratch level healed from the SAME serialized
pair bytes on host and joiner; borrowed `viewob[0]`, direct-geometry resize,
control-less pan camera); curses draws the glyph band in the network lobby
and in the solo picker's viewer (`CursesRenderer::draw_preview`); the text
client stages locally with its session-latched `--seed`. MatchStage loads
are HEADLESS-hooked only — lobby-poll restaging never touches the SDL loader
(#162 stays closed, TRAIN included).

## Launch = adoption

- **Dedicated server + curses sessions**: object handoff (`MatchStage::take`)
  — the staged `LevelRuntimeData` IS the live server world.
- **SDL shadow**: content transfer (`adopt_staged_world` over
  `replace_loaded_world_state`) plus the explicit carries a move cannot make
  — world id, the staged WorldScripts VM (`adopt_scripts_from`: the staged
  on_load's `og.set_entity_hooks` registrations and module state live in the
  VM registry and nowhere else; the VM binds its world through the
  thread-local context at dispatch, so it follows the content), RNG stream,
  control policy/machine map, guy id counter, campaign vars, the TRUTHFULLY
  claimed on_load latch, the staged SaveData.
  The display world stays an ordinary keyframe-healed client (LevelVisuals
  are not snapshot-carried, so every client loads its own level for art).
- **Replay playback** keeps the legacy display-seed path: the recording's
  initial snapshot IS a staged world of its era.
- The deployed roster spawn has ONE production home,
  `og::server::spawn_team_from_save` (the SDL display's inline copy is
  collapsed onto it; the text client's crew assembler is deliberately not a
  twin — CLI family list, users/real teams, pinned legacy baselines).

**#239 dies structurally**: `GameServer::step` holds tick 1 (and the event
drain) while any seeded client is unready at level start — hosted, dedicated,
joiner, curses-local, and the dedicated server's in-session transitions all
ride the same gate, bounded by the ready deadline. The deadline covers BOTH
gate-closing states: it is stamped at every keyframe send AND at the re-setup
itself (`prepare_clients_for_loaded_level`), so a transition-limbo peer that
heartbeats but never re-readies is cut one window after the reload instead of
freezing everyone. InitialSetup carries a v13 `setup_generation` (bumped per
ready-resetting re-setup) so clients treat a SAME-level reload (quit-mission
withdraw) as a transition and re-ready, while mid-level resends (control
mapping, reconnect catch-up) keep the generation and stay non-transitional.
The curses sessions' 4/6 burned handshake ticks are gone; every launch begins
at a dormant tick 0.

## The plan phase is retired (D42)

`on_mode_init` runs once, at staging, in a real world — so the plan seam
(`LevelHook::ModePlan`, the plan-dispatch fence, `build_match_plan_inputs`,
`hooks::level_mode_plan`, `match_plan.{h,cpp}`) is deleted whole. The rules
keep one home: `match.activation` + `match.fills` (lib/mode_match.lua), fed
by `match.census_inputs()` over the live world, consumed by each mode's
in-body `decide` fold. See `docs/matched-teams-design.md` D41 (history) and
D42 (the retirement).

## Test spine

`og_unit_stage` (`test_match_stage.cpp`, `test_staged_report.cpp`,
`test_staged_rules.cpp`): restage byte-identity + divergence oracles, seed →
squad-code pins, dormancy, on_load-once, wire-pair == world, mirror heals
byte-identical + report line-identical, the staged report line shapes, the
16-row activation sweep (direct-Lua probe), the apply-executes-decision
matrix (16 cases x 5 modes), and per-mode staged-vs-adopted byte identity.
Integration: the level-start gate red-then-green battery (#239 shapes), the
launch-equals-preview hash, the staged-pane injector flow, the uxshot band
guard, curses host/join band cell-identity, and the text census determinism
pins.
