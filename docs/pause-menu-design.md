# Pause menu, named input mappings, and joystick support

Design for three coupled features:

1. Replace the Esc "pause, then abort prompt" flow with a full **PAUSED menu**
   (QUIT / RESTART / per-player settings / ADD PLAYER).
2. Give player input mappings **names** ("WASD", "ARROWS", "IJKL", "TFGH",
   "JOY1", …) shown in Base Camp and cycled from per-player settings.
3. Revive **joystick support**, assigned per seat through the same INPUT
   cycler.

Status: implemented on `feature/pause-menu`.

## 1. Current state (what this replaces)

- `game_loop.cpp` Esc handler: first Esc sends a pause request
  (`local_transport_shadow_toggle_pause` → server sets `world_.paused`, skips
  world ticks), second Esc opens `yes_or_no_prompt("Abort Mission")`. The
  pause visual is display text ("PAUSED" + "ESC again: Quit?").
- Per-player key profiles: 4 fixed slots, each with a 4-dir and an 8-dir
  17-key map (`player_mode_keys[4][2][17]`), persisted flat in
  `cfg/openglad.yaml` (`controls: playerN_*`, 212 keys). No names anywhere;
  `playerN_default_profile` is the only identity (which factory table RESET
  restores).
- Joystick: `JoyData`, event decode, per-tick sampling, and the remap prompt
  all work and are tested — but `SDL_INIT_JOYSTICK` is never initialized at
  boot, so zero devices ever enumerate. No assignment UI, no persistence, no
  hotplug, and a bound joystick *overrides* the keyboard per action.

## 2. UX spec

### 2.1 PAUSED menu (SDL clients only)

Esc/Backspace during gameplay opens the menu (no more "second Esc" state).
The world pauses underneath (same server pause as today). Esc again, RESUME,
or BACK closes it and resumes. The `!event.key.repeat` guard stays (web
Backspace autorepeats).

Layout on the 320×200 UI canvas over a darkened world snapshot
(`prepare_ui_canvas_from_world` + `darken_screen`, the `yes_or_no_prompt`
recipe):

```
                    P A U S E D
        [ RESUME                    ]
        [ RESTART MISSION           ]      hidden when networked or when the
        [ QUIT MISSION              ]      progression suppresses retry
        ---- PLAYERS ----
        [ P1: WASD                  ]      one row per LOCAL seat
        [ P2: ARROWS                ]
        [ + ADD PLAYER              ]      hidden when networked / 4 seats
```

(The separator is ":" — the bitmap font has glyphs 0..124 only, so "·" would
render as the fallback glyph.)

- Buttons are 140px wide (23-char budget), centered column, MenuSpecRow
  dispatch, ids `pause_resume`, `pause_restart`, `pause_quit`,
  `pause_player_0..3`, `pause_add_player` (distinctive ids — stale picker
  `back` buttons survive `glad_main`, tests must not collide).
- Player rows are labeled `P{n}: {mapping display name}` — the named-mapping
  feature is visible right here.
- QUIT keeps the existing confirm (`yes_or_no_prompt("Abort Mission", ...)`)
  and the existing per-role abort semantics (authoritative ends the level for
  all; joiner requests abort and keeps looping until the server terminal
  broadcast).
- RESTART confirms, then ends the level with the retry path: no results
  screen, no roster persist (`ending=1, withdrawn`), display `world().retry`
  set so the native `do { glad_main } while (retry)` loop relaunches from the
  pre-level save. Web: the emscripten state machine learns to check
  `world().retry` on `done` and re-enter Playing instead of falling back to
  the picker. RESTART is hidden in networked sessions.
- Implementation: a `MenuScreenSpec` run through `run_menu_screen`, hosted
  from the game loop's Esc branch, wrapped in `ScopedUiCanvas`. NOT added to
  the `MenuScreenId` registry (the registry sweep would run it in picker
  context); it lives in a new `src/interface/ui/pause_menu.cpp`.
  `polls_lobby=false`, `remote_start=None`, `backdrop=false`, `enter=None`.
- On exit: `release_mouse()`, `clear_transient_input_state()`,
  `clear_allbuttons()`, world redraw on the World target, `redrawme = 1`.

Keeping the transport alive while the menu blocks (the two pause traps):

- `frame_tick` calls a new `local_transport_shadow_pump_paused(session)`:
  polls the server step (world tick already suspended by the pause) and
  drains client mirrors, so networked peers keep their keepalives and the
  inbound queue never overflows.
- Pause keep-alive: `GameServer::handle_pause_request` learns that a repeat
  request from the *same owner* while a pause is pending refreshes
  `opened_at_ms` instead of being rejected (no wire change — same empty
  `PauseBroadcastMessage`). The menu re-requests every ~20 s, defeating the
  60 s auto-resume for as long as it is open.
- The 5 s `PAUSE_RATE_LIMIT_MS` no longer applies to the host peer (the local
  machine's own display client). Remote peers keep the anti-grief limit.
  Without this, closing and reopening the menu within 5 s leaves the world
  running behind an open menu.
- The third pause trap: suspension time must never count against input
  starvation. While the menu blocks, no in-process peer sends input (and the
  forced per-step keyframes suppress client heartbeats via the
  outbound-activity window), so clearing the suspension used to expose
  minutes-stale input stamps and mass-disconnect every peer on RESUME.
  `clear_pause_state`/`clear_pending_exit_prompt` restamp input freshness,
  and same-process peers (`GameServer::mark_peer_local`) are never timeout-
  or desync-disconnected at all — those semantics exist for remote
  transports.

Curses/text clients: unchanged. Their Esc = withdraw prompt is the terminal
analogue; no terminal PAUSED projection.

### 2.2 Per-player settings (in-game and Base Camp)

Selecting `P{n} · {name}` opens the player screen (same shape as Base Camp
seat settings, minus TEAM):

```
        LOCAL PLAYER 2  ·  P2
        [ BACK ]
        [ INPUT: ARROWS ]     cycler
        [ 8-DIRECTION   ]     mode toggle
        [ REMAP         ]     existing remap wizard
        [ RESET         ]
        [ REMOVE PLAYER ]     hidden when networked or last local seat
        ... live binding grid (existing seat-settings content draw) ...
```

- The **INPUT cycler** cycles: `WASD`, `ARROWS`, `IJKL`, `TFGH`, any saved
  custom mappings, then `JOY1`, `JOY2`, … for each connected joystick.
  Entries already held by another *active* seat are skipped (two players can
  only share a mapping through deliberate remapping, never through the
  cycler).
- Base Camp seat settings gains the same INPUT cycler row (at y=62, under the
  full y=38 band; the live-bindings panel moved down to y=84 to make room).
  Both screens share the cycler helper (`og::ui::cycle_player_input`).
- REMOVE PLAYER mid-game performs the local mid-game seat removal (§5) after
  a `no_or_yes_prompt` confirm. In Base Camp it keeps today's behavior.
- REMAP keeps the existing wizard, which already accepts joystick events.
  While the menu is open the world stays paused, so the blocking prompts are
  safe; the remap poll callback also pumps `local_transport_shadow_pump_paused`.

### 2.3 Base Camp seat cards

`"P1 YOU "` → `"P1 {SHORT}  "` for local seats, where SHORT is the mapping
short name (≤5 chars: `WASD`, `ARROW`, `IJKL`, `TFGH`, `JOY1`, or the derived
movement cluster for customs). Budget: the seat card face is exactly 11 chars
(70px / 6) and BOTH trailing spaces are load-bearing — they centre the visible
ink over the zone the team chip leaves rather than over the whole face:
`"P2 ARROW  "` = 10, and the widest shape, `"P16 ARROW  "`, is exactly 11. The
rail holds only this machine's seats,
so no card carries a foreign company abbreviation any more. The shared seat
identity strings are unchanged.

## 3. Named mappings

### 3.1 Naming rule (derived, not stored)

A mapping's name is a pure function of its movement keys (active mode):

- UP/LEFT/DOWN/RIGHT = W/A/S/D → `WASD`; arrows → `ARROWS`; I/J/K/L →
  `IJKL`; T/F/G/H → `TFGH` (checked against the *factory movement cluster*
  of each table, in both modes).
- Joystick-driven seat → `JOY{n}` (1-based device slot).
- Anything else → the four cardinal key names concatenated UP,LEFT,DOWN,RIGHT
  (e.g. `WQSE`), uppercased, truncated to 6; unnamable keys → `CUSTOM`.

So "names update as needed" falls out automatically: remap FIRE and you are
still on WASD; remap movement and the name follows the keys. The formatter is
pure, SDL-free, and lives in `picker_common`-adjacent code
(`src/interface/input/input_mappings.cpp`) so it is headlessly unit-testable.

### 3.2 Mapping library (cfg-persisted)

New cfg category `mappings`: each saved mapping is
`mapping{K}_name`, `mapping{K}_mode`, `mapping{K}_mode4_key{0..16}`,
`mapping{K}_mode8_key{0..16}`. The four factory layouts are implicit (never
written unless customized). Semantics:

- **Cycling INPUT to a name** loads that library entry (factory table if no
  entry) into the seat's two mode maps + mode, and sets
  `playerN_default_profile` when the name is a factory one (so RESET keeps
  working unchanged).
- **Remapping a seat** writes through to the library under the seat's
  (re-derived) name. A customized WASD is saved as the `WASD` entry — cycle
  away and back, or restart the game, and the customization is still there.
- The existing per-seat `controls:` block stays untouched as the live-seat
  copy (load order and the legacy `playerN_keyK` compatibility path are
  unchanged; the atomic `default_profile` permutation validation is
  unchanged).

## 4. Joystick

Building on the gap analysis (everything below `JoyData` works today):

- **Boot**: `joystick_init_subsystem()` at the top of `init_input()`. The
  positional auto-bind (`device i → player i`) is REMOVED — a seat only gets
  a joystick through the INPUT cycler (or a persisted assignment).
- **Assignment model**: `assign_joystick_to_player(p, device)` /
  `clear_player_joystick(p)` own `player_joy[p]`, with the default layout
  synthesis `JoyData(int)` already provides. The cycler skips devices
  assigned to other seats. `setKeyFromEvent`'s last-device-wins takeover is
  constrained to the seat's assigned device when one is set.
- **Default button layout**: button 0 → SPECIAL, button 1 → FIRE (reversed
  by user request), then SPECIAL SWITCH / YELL / SHIFTER / SWITCH on
  buttons 2-5. Synthesis is defensive: only axes resting inside the dead
  zone become movement cardinals, capability counts are clamped
  non-negative, and a device that opens with nothing bindable is refused
  outright (the enumerated-but-unopenable udev case surfaces a
  "COULD NOT OPEN JOYn" popup instead of a silent no-op).
- **Hotplug**: `SDL_EVENT_JOYSTICK_ADDED/REMOVED` enter the native_input seam
  (`EventType::JoyDeviceAdded/Removed`); on removal the affected seat falls
  back to its keyboard mapping with a display notification; on add the
  cycler options refresh (mandatory on web, where the Gamepad API only
  exposes devices after a button press).
- **Hats**: `JoyHatMotion` gets its `handle_events` dispatch case (it is the
  primary web d-pad path).
- **Persistence**: `controls: playerN_joystick_guid` (SDL joystick GUID
  string). On boot/hotplug, a device matching a seat's saved GUID
  re-attaches automatically.
- **Display**: seat-settings binding grid and control summaries show
  joystick bindings (`B0`, `AX0+`, `HAT↑` style) instead of the misleading
  keyboard names when a seat is joystick-driven; summary stays ≤48 chars.
- **Cleanup in the same change**: the dead `#ifdef OUYA` blocks (~200 lines
  in the input hot path) are deleted; `handle_joy_event` is reduced to its
  one real job (key_press_event_ + hat dispatch); the in-game options menu's
  `J` toggle (positional rebind + full subsystem restart) is removed in favor
  of the INPUT cycler.
- **Keyboard override semantics kept**: an assigned joystick still overrides
  keyboard for actions with a joystick binding (deliberate — the seat's name
  says JOY1). Unbound actions (PREFS etc.) still come from the keyboard maps.

No protocol change: joystick input flows into the same `InputState` bits.

## 5. Mid-game ADD / REMOVE local player (non-networked sessions only)

New functions in `local_transport_shadow.cpp` (platform layer; zero sim-file
inserts, zero parity-golden movement, zero protocol bump):

**Add (seat N, N = current count):**
1. `save_data.numplayers = N+1` first (`compute_local_player_count` is the
   hidden coupling), then `numviews = N+1`, construct `viewob[N]` with
   `compute_view_layout(N+1, N, ...)`, `relayout_views()`.
2. Server side (activated session + context guard): claim an unclaimed
   deployed hero of the chosen team via the existing scan; if none, spawn a
   stock soldier-class walker at an anchor-probed spot (`respawn_spot_clear`,
   no RNG draw, no `myguy` — so the roster is not polluted at level end).
3. New in-process client transport → `connect_client` →
   `bind_player(peer, N, team, control, N)` →
   `send_initial_snapshot(peer)` → **`set_player_control(N, control)`**
   (bind_player alone does not broadcast ControlChange — issue-#175-class
   trap).
4. Append `LocalTransportClient{input_slots={N}}` +
   `configure_background_game_client`. Input for slot N is already sampled
   every frame; it was just unrouted.
5. Team choice: the team of view 0 in allied/co-op saves (the common case);
   the new seat's controls come from the rotated profile pool exactly as a
   Base Camp **ADD PLAYER** slot would.
6. The local lobby seat count is synced at level end so the added player
   survives the return to Base Camp.

**Remove (any local seat, ≥2 seats):**
1. `set_player_control(idx, nullptr)` FIRST (broadcast entity 0 — the
   disconnect path never clears `player_controls_` itself), then
   `disconnect_client(peer_idx)` (never peer 0 — host kick cascades). The
   walker keeps living as AI on its team.
2. Renumber survivors down: server rebind (sorted, deterministic), client
   `input_slots`, then rebuild the display views for the new count and
   re-derive controls from the ControlChange state.
3. `compact_player_controls_after_removal(idx, count)` keeps key profiles
   aligned (existing, tested primitive).
4. `save_data.numplayers = count-1`, `relayout_views()`, lobby sync at level
   end, display notification ("PLAYER 3 LEFT").

Determinism rules honored: binds/renumbers happen between ticks while paused,
never draw `world.rng_`, and iterate seats in sorted player_index order.
Replay: recorded input already carries all 4 slots; the mid-level layout
change on playback is accepted as a known cosmetic limitation (a
`kReplayFormatVersion` bump is out of scope).

## 6. Test plan

- **Pure units first** (`og_unit_*`): mapping-name derivation, library cfg
  round-trip (local `cfg_store`, never disk), cycler option enumeration,
  short-name budgets (≤5), pause-menu label formatter.
- **Menu engine pins** (`og_test_menu_engine`): pause menu + player screen
  exact tables (ids/geometry/nav/hidden variants: solo, splitscreen,
  networked, 4-seat), seat-settings re-pin with the INPUT row.
- **Frame-level** (`og_test_game_core`): rewrite the two Esc tests
  (`test_game_loop.cpp:3601/:3685`) for menu-open/resume/quit/restart;
  keep the key-repeat guard pin; keep-alive refresh + host rate-limit
  exemption units in `og_unit_sim` server tests.
- **Mid-game seats** (`og_test_game_core` + `og_unit_core`): add/remove
  through a real `glad_init` shadow (idiom at `test_game_loop.cpp:3757`),
  view_layout projections per count, profile compaction interplay,
  roster-purity after a stock-walker join (level-end `update_guys`).
- **Joystick** (`og_test_input`): extend `test_input_joystick.cpp` with SDL
  virtual devices — assignment API, hotplug add/remove, GUID persistence
  round-trip, cycler skip rules, hat dispatch.
- **Label sweep**: `test_menu_layout` card formula + 9-char budget,
  `test_view_team` literals, `test_ctf_ui` trace literal, text negative
  assertions, curses untouched.
- **E2E**: rewrite `wasm-touch.spec.js` BACK-BACK flow for the menu; web
  RESTART path.
- **Screenshots**: `test_uxshots_probe.cpp` captures of the PAUSED menu and
  player screen; visual read-back before claiming success.

## 7. Menu consolidation (round 2, issue #169)

The old per-player in-game options menu (`viewscreen::options_menu`, keys
1-4) is retired. Where every row went:

### 7.1 Unified player screen (Base Camp seat settings AND pause player
screen share one geometry)

```
  [BACK]              LOCAL PLAYER n · Pm
  [4/8-DIRECTION][REMAP     ][RESET     ]     y=38 band (unchanged)
  [INPUT: WASD  ][ZOOM: GAME]                 y=62 band
  +----------------------------+ [RADAR: ON ]  right stack y=84/106/128/150
  | MOVEMENT      ACTIONS      | [HP:    ON ]
  | (binding panel, 12..208)   | [FOES:  ON ]
  |                            | [SCORE: ON ]
  +----------------------------+
  [TEAM n       ][REMOVE PLAYER]              y=169 (variant-gated)
```

- The binding panel narrows to x=12..208 (movement col x=20, actions x=104);
  the HUD toggles stack on the right at x=214, w=90, 22px pitch.
- RADAR/FOES/SCORE toggle `prefs[PREF_RADAR/FOES/SCORE]`. HP is ON/OFF only
  (ON = PREF_LIFE_BOTH, OFF = PREF_LIFE_OFF; the TEXT/BARS/SMALL states are
  retired — SMALL was a dead state rendering as BOTH). PREF_OVERLAY keeps
  its render behavior but loses its (historically invisible) UI row.
- ZOOM is a real per-view zoom-out with values GAME (default — follow
  `graphics/zoom`), then 0.9x..0.5x, riding the SAME single-resample pipeline
  as the global zoom. The first shipped implementation re-rendered every frame
  through the floor-layer compositor at `fscale * frame_zoom`; that stacked a
  per-frame bilinear resample (`floor_layer_end`'s `SDL_SCALEMODE_LINEAR`
  squeeze) under the presentation stretch — double filtering, visible smudge —
  and is deleted, not tuned. The architecture that replaced it:

  **Invariant (the deliverable): gameplay pixels are resampled exactly once,
  by the presentation path.** Every view renders 1:1 at native density on the
  world canvas; the only scaling any world pixel receives is the present-time
  texture blit (nearest, exactly like global zoom).

  - *Effective zoom* of view i = global zoom × per-view scale
    (`n_i = 10 - view_zoom_step_`, so n ∈ 10..5 tenths). The world canvas is
    derived from the MINIMUM effective zoom over the live views: composed
    percent = `zoom_steps × n_min` through
    `og::compute_zoom_canvas_dims_pct` (the steps function delegates to it at
    pct = steps×10, floor-math-identical). `n_min == 10` takes the untouched
    global-only path, so zoom OFF for every view is byte-identical by
    construction.
  - *Slots vs windows.* `viewscreen::resize(whatmode)` computes the SLOT
    (the baseline gameplay-UI layout projected onto the canvas — exactly the
    pre-existing rect) and the WINDOW = slot × `n_min / n_i`, anchored at the
    slot's top-left. The view renders its window 1:1; a view at the minimum
    zoom fills its slot exactly. All-off ⇒ window == slot everywhere.
  - *Partitioned presentation.* When any window ≠ slot,
    `screen::relayout_views` publishes {src=window, dst=slot} canvas-space
    rects through `video::set_world_present_slices`. `Screen::swap` presents
    the whole canvas exactly as before and then overlays one
    `SDL_RenderTexture(tex, &src, &dst)` per slice — each gameplay pixel still
    crosses exactly one GPU nearest resample. An empty slice list is the
    untouched single-blit path. The capture/backdrop composition
    (`compose_gameplay_ui_for_capture`, `prepare_ui_canvas_from_world`)
    applies the same slices so screenshots and modal backdrops show what the
    player sees. A single view at minimum zoom degenerates to window == slot
    == whole canvas: literally the global-zoom frame, no slices at all.
  - *Budgets.* The composed percent rides the existing clamp machinery
    (`zoom_canvas_fits_budget_pct`, `constrain_world_canvas_dims`,
    `kWorldCanvasPixelBudget`); the per-view cycler skips steps whose composed
    canvas would not fit (`video::world_zoom_composition_fits`). The old
    per-frame layer budget (`kPerViewZoomLayerPixelBudget`,
    `resolve_frame_zoom`) is gone with the layer path.
  - *Multifloor.* The floor-layer compositor still exists — solely for its
    original job: faded/parallax floors and the glide. Only the base-zoom
    resample died. Larger windows can push a faded floor past the layer
    budget; that falls back to the direct-alpha draw exactly as deep global
    zoom always has.
  - *Mid-game changes* (pause-menu cycling, seat add/remove) call
    `relayout_views`, which recomposes the canvas, windows and slices —
    render/geometry only, the sim and transports are never touched.
  - HUD/radar projection is unchanged: `ScopedGameplayUiViewLayout` +
    `project_world_point_to_gameplay_ui` already map the (now larger) window
    onto the stable zoom-1.0 pane. (`draw_mode_beacons` was the exception —
    it mixed the world `topx` with the swapped UI `xloc` until issue #220;
    it now projects through the shared `GameplayUiProjector`, which
    `draw_small_health_bar` also uses for its width/height pane-ratio
    scaling, issue #244.) The editor's classic-canvas pin forces
    per-view zoom inert (window == slot). When the platform has no partition
    seam (`world_present_partition_supported` false) the row is disabled,
    never wrong.
  - *Known scope limit:* pointer→world-canvas mapping stays whole-canvas
    uniform, so inside a SLICED pane (a split where another pane is deeper)
    a hypothetical world-space pointer would be offset. Gameplay uses no
    world-space pointer (menus are on the UI canvas; web touch play is
    single-seat, where window == slot and no slice exists), so nothing
    user-visible depends on it today.
- Persistence: prefs stay the runtime carrier; new cfg keys
  `controls: playerN_hud_radar/_hud_life/_hud_foes/_hud_score/_view_zoom`
  (defaults registered so RESTORE SETTINGS keeps them), written by the two
  screens' persist calls; keyprefs.dat prefs seed the cfg once when the
  keys are absent, after which the file is legacy.

### 7.2 PAUSED menu gains VIEW TEAM and BRIEFING

```
        [ RESUME          ]  y=32
        [ RESTART MISSION ]  y=50
        [ QUIT MISSION    ]  y=68
        [VIEW TEAM][BRIEFING] y=86 (two 66px faces)
        - PLAYERS -           y≈107
        [ P1: WASD        ]  y=112 (players at 18px pitch)
        [ + ADD PLAYER    ]  y=182 (hidden at 4 seats)
```

- BRIEFING calls `read_scenario(scr)` (nesting is safe; the backdrop
  restore heals its scribbles). The in-game Shift+`/` briefing chord is
  DELETED — it was any shifter + raw `/`, i.e. P2's SPECIAL key.
- VIEW TEAM hosts `viewscreen::view_team` for view 0 with a poll callback
  so the pause keep-alive survives its wait loop; the world-canvas redraw
  dance on return is preserved (re-homed regression test).

### 7.3 Global migrations

- SPEED → GAME SETTINGS cycler (display 1..11), cfg `gameplay/timer_wait`
  (default 6) applied at level start; live change still stamps
  `pending_timer_wait_request_` (host-authoritative, relay warning kept).
- COLOR CYCLING → GRAPHICS FX at (115,127), cfg `effects/color_cycling`
  default ON (identity is pinned against ON — this effect inverts the
  off-byte-identical convention).
- BRIGHTNESS → DISPLAY at fx_row_y(5), overscan-style -/+ pair, cfg
  `graphics/brightness`; gamma moves out of `viewscreen::gamma` and is
  re-applied after every `set_palette`, fixing the long-standing bug where
  every keyframe palette sync silently reset it.

### 7.4 Deletions

`viewscreen::options_menu`, `set_key_prefs`, `view_key_bindings`,
`get_keypress`, `change_speed`, `change_gamma`, the `viewscreen::gamma`
member, the KEY_PREFS dispatch, and the Shift+`/` chord all go.
InputAction slot 14 stays RESERVED (wire format pins NUM_INPUT_KEYS == 16);
its default bindings become KEYCODE_UNKNOWN, the cfg loader ignores a
persisted slot-14 value and the writer writes UNKNOWN back, freeing keys
1-4 for player mappings on the next save. keyprefs.dat's key half was
already vestigial, so `init_allkeys`, `GameSession::allkeys_`,
`viewscreen::mykeys` and `options::save` go with it; the file is now
read-only, seeding HUD prefs at view construction until
`apply_hud_settings_from_cfg` has migrated a player into cfg.

The Emscripten regression the retired KEY_PREFS test covered (a blocking
in-game modal must yield through `og::input_native::sleep_ms`, the call
that suspends under `-sASYNCIFY`) moves onto the PAUSED menu as
`PauseMenuFlow.blocking_menu_yields_to_the_browser_each_iteration`.

### 7.5 The global CONTROLS screen goes too

Once every seat owns mode / REMAP / RESET / INPUT on its own player screen,
the CONTROLS subscreen under GAME SETTINGS (four player sections of
mode + remap, plus RESET ALL) is the same four rows drawn four times. It is
deleted: its GAME SETTINGS door, its spec and registry row, and the
`OpenControlSettings` / `ToggleControlMode` / `EditPlayerKeymap` button
dispatch ids (the functions stay — the player screens call them directly).
RESET ALL has no successor by intent; per-seat RESET is the replacement, one
seat at a time. Nothing is lost on the persistence side either: CONTROLS
wrote the controls cfg and the mapping library on exit, and both player
screens already write both after every change.

SPEED stays in the right column and moves up to `options_col_y(6)` — the
band the CONTROLS door used to occupy, and the first one whose x=210 face no
longer collides with a 90px door. Both columns then end on the same rhythm
instead of leaving SPEED stranded two rows below everything else. The Sound
row, 5px right of the doors under it since forever, joins their column edge
in the same pass.

## 8. Non-goals

- No terminal (text/curses) pause menu.
- No networked mid-game add/remove; no per-seat leave in networked play
  (QUIT keeps the all-or-nothing withdraw semantics).
- No replacement for RESET ALL CONTROLS: the global CONTROLS screen is
  deleted outright (§7.5) and per-seat RESET is the only reset.
- No `SDL_Gamepad` layer (raw joystick API only, matching `JoyData`).
- No replay-format bump.
