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
        [ RESTART MISSION           ]      hidden when networked
        [ QUIT MISSION              ]
        ---- PLAYERS ----
        [ P1 · WASD                 ]      one row per LOCAL seat
        [ P2 · ARROWS               ]
        [ + ADD PLAYER              ]      hidden when networked / 4 seats
```

- Buttons are 140px wide (23-char budget), centered column, MenuSpecRow
  dispatch, ids `pause_resume`, `pause_restart`, `pause_quit`,
  `pause_player_0..3`, `pause_add_player` (distinctive ids — stale picker
  `back` buttons survive `glad_main`, tests must not collide).
- Player rows are labeled `P{n} · {mapping display name}` — the named-mapping
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
- Base Camp seat settings gains the same INPUT cycler row (new row band at
  y≈62 — the y=38 band is full). Both screens share the row handlers.
- REMOVE PLAYER mid-game performs the local mid-game seat removal (§5) after
  a `no_or_yes_prompt` confirm. In Base Camp it keeps today's behavior.
- REMAP keeps the existing wizard, which already accepts joystick events.
  While the menu is open the world stays paused, so the blocking prompts are
  safe; the remap poll callback also pumps `local_transport_shadow_pump_paused`.

### 2.3 Base Camp seat cards

`"P1 YOU "` → `"P1 {SHORT} "` for local seats, where SHORT is the mapping
short name (≤5 chars: `WASD`, `ARROW`, `IJKL`, `TFGH`, `JOY1`, or the derived
movement cluster for customs). Budget: the 57px card face is exactly 9 chars
and the trailing space is load-bearing (team chip clearance): `"P2 ARROW "` =
9. Foreign seats keep `"P3 {ABBR} "`. MATCHUP identity strings are unchanged.

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
   Base Camp `[+]` would.
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

## 7. Non-goals

- No terminal (text/curses) pause menu.
- No networked mid-game add/remove; no per-seat leave in networked play
  (QUIT keeps the all-or-nothing withdraw semantics).
- No changes to the global CONTROLS screen layout (its summary lines already
  show the derived names implicitly).
- No `SDL_Gamepad` layer (raw joystick API only, matching `JoyData`).
- No replay-format bump.
