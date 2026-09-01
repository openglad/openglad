# Camera Viewscreens — implementation-ready specification

**Feature:** engine-owned camera viewscreens — panes that follow an arbitrary walker without being a player seat — creatable, retargetable, and destroyable from mode Lua. First consumer: the ball camera in Soccer and Basketball (issue #224).
**Branch:** `feature/camera-views` (repo `/home/yans/code/openglad`, forked from master `7f81461d`). All file:line anchors verified against that tree — line numbers shift as you insert code; anchor by the quoted text, not the number.
**Winning mechanism:** the MINIMAL-BLAST skeleton (camera as a parallel member outside the seat world), selected by adversarial design review 61–54 over the issue-sketch INTEGRATED alternative, with eight grafts and rulings from the review folded in below. Rulings that would otherwise look arbitrary are marked "(design-review ruling)".

---

## 0. One-paragraph summary

Mode Lua on the host declares "a camera follows this entity" through a new replicated `ModeState` slot (`og.set_camera_view(0, ball)`), exactly the beacon precedent: an int32 entity id plus a style byte, riding the existing wholesale ModeState replication into every snapshot, delta, hash, and replay compare. Each machine's **display** screen — never the authoritative server screen — materializes the camera as a parallel `std::unique_ptr<viewscreen>` member on `screen`, deliberately **not** an element of `viewob[]`: `numviews` keeps its 24-year-old meaning of "local human seats", and every one of the ~40 `for (i < numviews)` seat loops (input dispatch, HUD density, seat claiming, seat surgery, results classification, pause seats, player-count fallback) keeps its semantics without edits, because the camera is unreachable from them **by construction**. With 3 local seats and style `auto`, the camera docks into the free fourth quadrant — the three seats re-lay through the real `compute_view_layout(4, i, …)` via a new `layout_pane_count()` indirection at the three layout call sites; with 1/2/4 seats (or style `inset`) it draws as a small centered overlay on the GameplayUI canvas, above all seat HUD chrome and below the pause modal. The docked-vs-inset decision is resolved per machine from local seat count and never touches the wire, so a 3-seat host and a 1-seat joiner each render correctly with zero desync. When no camera is declared, `layout_pane_count() == numviews` and every changed call site is value-identical: the OFF state is byte-identical to master.

---

## 1. Hard constraints (standing rules — binding on every future edit here)

1. The authoritative/server screen NEVER materializes camera views (`tests/integration/test_game_loop.cpp:915-919` pin stays verbatim, as teeth).
2. Version bumps are ONE coordinated commit: `kNetworkProtocolVersion` 16→17, `kSnapshotFormatVersion` 12→13, `kReplayFormatVersion` 18→19, plus every literal wire-byte pin and payload-offset pin RE-DERIVED from serializer output (never +N by hand).
3. `VALIDATE_SERIALIZATION=ON` round-trips every message requiring equality: validate on WRITE (the Lua binding); read-side defense must be identity for every legitimately-written value.
4. Render paths make ZERO game-rng calls (the radar "jitter-0 discipline").
5. Mode Lua runs HOST-ONLY and its output replicates; local seat count MUST NOT be exposed to Lua or folded into any replicated value. Docked-vs-inset is resolved per machine by the interface layer.
6. "No rule twins": one rule, one implementation — the camera channel is not a beacon reuse, and layout geometry comes from the one `compute_view_layout` pipeline.
7. Existing 1–4 seat visuals stay byte-identical when no camera is declared, and a docked camera must NOT change the numviews-keyed HUD rules for existing seats (compact panel `score_panel.cpp:886-887`, radar alpha `radar.cpp:335`, border suppression `screen.cpp:1834-1836` stay keyed on `numviews` = humans).
8. `compare_mode_snapshot_state` / `find_first_snapshot_difference` learn every new replicated field, or divergence surfaces as an unexplained `snapshot_hash` mismatch.
9. Per-tick Lua cost matters (instruction-budget probes run at a 10×-reduced budget): the API is set-once in `on_mode_init`, not per-tick re-assertion.
10. `mynum == 4` is OOB for `players[MAX_PLAYERS]`; `prefs[10]` is uninitialized for index ≥ 4; `kPvpConstructionOrder[4]` is OOB. The camera's identity makes every such site safe by construction, not by luck.
11. Parity goldens are blind here (render/UI + ModeState are not in the state dump); the mirror-desync oracle and direct tests are the coverage. `og_test_parity` stays 187/187 (no touched file carries mutation-canary pins — verified: pinned files are effect/game_world/living/walker/walker_combat/walker_movement/weap/gloader/save_data, none edited).

---

## 2. Decision record (what won, what was grafted, what was ruled)

| Decision | Resolution | Source |
|---|---|---|
| Representation | Parallel `camera_view_` member on `screen`; camera never in `viewob[]`; `numviews` stays a pure seat count | D2 skeleton (design-review ruling: asymmetric failure modes — a missed future hook is a benign omission, a missed seat-loop guard is an OOB/desync) |
| Wire slot count | **1** (`kModeCameraViews = 1`) | D1 graft (design-review ruling: the interface can materialize at most one pane; a wire slot that can never render is a standing API lie; this project bumps protocols routinely) |
| Serialization placement | Inside the mode block, appended after the beacons loop | Both designs agree |
| Read-side style defense | Identity clamp in `deserialize_mode_state` + consumer-side belt | D1 graft + D2 belt (design-review ruling: identity-for-legit-values is VALIDATE_SERIALIZATION-safe, same shape as the existing name NUL forcing in the same function) |
| Sync seam | First statement of `screen::redraw()` | D1 graft (design-review ruling: covers `render_pending_redraw`, the demo/capture path — which never runs `game_frame` — and direct `redraw()` calls in tests; the server never calls it) |
| Server-screen belt | Display-screen identity (`this == og::runtime::current_session->myscreen_`), not a transport-mode predicate | Design-review ruling (D2's cited `view.cpp:1458-1463` predicate is a transport-mode check that fires on display screens — a misread; do not use it) |
| Target loss | Keep the pane AND keep drawing with `control = nullptr` (LevelVisuals free-camera fallback) | D1 graft (design-review ruling: a skipped draw leaves a stale/black quadrant; the fallback is the correct degradation) |
| Docked resolution | `style == auto && local seats == 3`, per machine, off-wire; no `"docked"` force value exists | Both designs agree |
| Docked chrome | The camera hook draws its own bevel on GameplayUI; `draw_panel_chrome` untouched | D2 (design-review ruling: zero edits to a numviews-keyed rule beats re-keying it) |
| Inset geometry (4 seats) | GameplayUI-canvas coords, `w = ui_w*3/10`, `h = ui_h*3/10`, min 96×60, centered | D2, pinned by review (2 seats moved off this row by maintainer ruling — see the next-but-one row) |
| Inset geometry (1 seat) | **A second minimap**: the radar block mirrored — same size, same right edge, stacked one radar margin above the radar's rect — drawn at **0.25 zoom** (`kCameraMinimapZoomDenominator = 4`): a 4x-wider/taller world window projected directly into the final pane raster | Maintainer ruling (a centered pane at 1 seat sits exactly on the player's own hero, because the seat camera centres him; and the radar-sized rect at 1:1 is too zoomed-in to be useful) |
| Inset geometry (2 seats) | **Near-minimap ×2**: the one-seat second-minimap block for each seat, stacked above that seat's radar inside its side-by-side pane, with the radar's footprint and the same 0.25 zoom; one camera viewscreen, two draw rects (`camera_pane_rects_`), each projected directly into its final pane raster | Maintainer ruling (replaces the centered 1:1 inset at 2 seats; 4 seats keep it) |
| Relayout ordering | `camera_docked_` recomputed at the TOP of `relayout_views()` (before the seat resize loop consumes `layout_pane_count()`); camera geometry applied at the tail | Design-review ruling (a tail-hook recompute lets 4 seats resize against a stale docked flag for one relayout — 5-pane default-arm overlap) |
| Chrome scope in camera redraw | Explicit `bool camera_view_` flag on `viewscreen`; both redraw overloads skip the `ScopedGameplayUiCanvas`/`ScopedGameplayUiViewLayout` block when set | Design-review ruling (do not rely on the `mynum=-1 → quadrant 3` clamp coincidence in the default layout arm) |
| Hardenings taken regardless | `kPvpConstructionOrder` explicit `n < 4` bound in `screen::reset`; `viewscreen::prefs` zero-init | D1 graft (two-line closures of known OOB/uninit classes, inert for existing behavior) |
| Multi-floor targets | Per-frame retarget adopts the resolved target's floor onto the camera view (bare set-floor Teleport snap is acceptable for a camera pane) | Design-review ruling (neither design specified it) |
| Replay-compare labels | `mode.cameras[0].entity_id` / `.style`, matching the beacons `std::format` style at `replay.cpp:428-441` | D1 graft |

---

## 3. A — Wire/state channel

### Layout

New replicated sub-array inside `ModeState` (`include/openglad/gameplay/mode/mode_state.h:50-63`), the beacon shape:

```cpp
// mode_state.h — after kModeBeacons (line 30)
inline constexpr int kModeCameraViews = 1;
inline constexpr std::uint8_t kCameraStyleAuto  = 0;  // interface resolves docked/inset per machine
inline constexpr std::uint8_t kCameraStyleInset = 1;  // always inset
inline constexpr std::uint8_t kCameraStyleMax   = 1;

// after struct ModeBeacon (lines 44-48)
struct ModeCameraView
{
    std::int32_t entity_id = 0;  // 0 = slot empty (beacon convention)
    std::uint8_t style = 0;      // kCameraStyleAuto / kCameraStyleInset
};

// inside ModeState, appended AFTER `beacons` (line 62)
std::array<ModeCameraView, kModeCameraViews> cameras{};
```

- **Slot count = 1** (design-review ruling; see §2). Wire cost: 5 bytes on every snapshot and delta — ModeState replicates wholesale (`capture_mode_state` `snapshot.mode = world.mode` at `src/gameplay/world_snapshot.cpp:2602`, apply `:2649`, delta merge `baseline.mode = delta.mode` at `:3280`), so capture/apply/merge need zero edits.
- **Field types:** `entity_id` is the standard weak int32 reference resolved via `GameWorld::find_by_id` per consumer per frame; negative/stale ids fail to resolve, same as beacons. `style` is a plain byte; **no team byte** (a camera is team-less; the beacon's team byte exists only to color the arrow).
- Leaf-header discipline: `<array>`/`<cstdint>` only, plain `uint8_t` constants, no interface include.

### Serialization placement

Append the camera loop **inside `serialize_mode_state` / `deserialize_mode_state`, immediately after the beacons loop** (`src/gameplay/world_snapshot.cpp:860-885` / `:888-916`). The mode block's wire layout stays colocated with its serializer (block-order comment `world_snapshot.cpp:990-992`); all four named offset constants in `tests/unit/test_mode_snapshot.cpp:356-359` (77/81/90/359) precede the beacons and survive.

**Read-side style defense (design-review ruling), in `deserialize_mode_state`:**

```cpp
cam.style = (cam.style <= og::sim::kCameraStyleMax) ? cam.style : og::sim::kCameraStyleAuto;
```

Identity for every legitimately-written value (the binding only emits 0/1), so round-trip equality holds under `VALIDATE_SERIALIZATION=ON` — the same shape as the existing NUL re-termination of `name`/`hud` text in the same function (`:890-916`). `entity_id` needs no clamp: unknown/negative ids simply fail `find_by_id`. **Belt:** consumers additionally treat an unresolvable id as "declared but degraded" (§5), so a crafted snapshot can never select an invalid geometry path.

### Hash / delta / replay-compare integration

- Snapshot CRC: automatic — `serialize_world_state` is emitted for keyframe, delta, and hash payloads (`world_snapshot.cpp:1438, 1569, 1502`). Only the authority writes the slot (mode Lua runs host-only, `src/gameplay/mode/mode_tick.cpp:188-225`, called solely from `GameWorld::tick`).
- Constraint 8: `compare_mode_snapshot_state` (`src/gameplay/replay.cpp:377-444`) gains a camera loop directly after the beacons loop at `:428-441`, labels `mode.cameras[0].entity_id` / `mode.cameras[0].style` in the beacons `std::format` style. `find_first_snapshot_difference` (`:939-949`) routes through it for free.
- Replay: no new file structures; the format version bumps because snapshot bytes changed.

### Version bump — ONE coordinated commit (constraint 2)

| Symbol | From → To | Site |
|---|---|---|
| `kNetworkProtocolVersion` | 16 → 17 | `include/openglad/gameplay/net_transport.h:170` + v17 history-comment entry (`:110-170`) |
| `kSnapshotFormatVersion` | 12 → 13 | `include/openglad/gameplay/world_snapshot.h:35` |
| `kReplayFormatVersion` | 18 → 19 | `include/openglad/gameplay/replay.h:36` + rationale comment |

### Full pin-fallout list (all values RE-DERIVED from serializer output, never +N)

| # | Pin | Change |
|---|---|---|
| 1 | `tests/unit/test_net_transport.cpp:266` — `{0x10, 0x01, 0x11, 0x22}` | leading protocol byte → `0x11` |
| 2 | `tests/unit/test_net_transport.cpp:1304-1305` — hello frame (header + protocol/min_protocol + adjacent snapshot-format byte) | re-derive the whole array by printing `serialize_hello` output |
| 3 | `tests/unit/test_input_state_net.cpp:133`, `:149` — leading `0x10` | → `0x11` (**NOT** `:155` — payload data, not a version) |
| 4 | `tests/integration/test_replay.cpp:510-518` | rename `format_version_18_rejects_v17` → `..._19_rejects_v18`; `static_assert(... == 19)`; `old_header[4] = 18` |
| 5 | `tests/unit/test_world_snapshot_coverage.cpp:35` — `kSerializedWorldStateBytes = 532` | → **537** (1 slot × 5 bytes); derived offsets `:36-45` recompute; verify by round-trip print |
| 6 | `tests/unit/test_world_snapshot.cpp:2418` — `kEntityCountOffset = 537` | → **542**; fix the "fixed 404-byte mode block" prose at `:2412-2417` → 409 bytes |
| 7 | `tests/unit/test_mode_snapshot.cpp:348-359` | named constants 77/81/90/359 **survive**; layout comment's match-knob offset 486 → **491**; `populate_full_mode_state` (`:97-160`), match assertions (`:213-218`), `expect_snapshot_mode_defaults` (`:265-267`) all learn the camera slot |
| 8 | Symbolic rejection literals (`test_net_transport.cpp:1349`, `test_input_state_net.cpp:187`, `tests/unit/test_replay.cpp:347`) | survive automatically; verify still ≠ new versions |
| 9 | `tests/unit/test_platform_headless.cpp:862-867`, `:874-876` — mode JSON literals | repinned for the `"cameras"` key (§8) |
| — | **Not disturbed (verified — do not churn):** LobbyState offsets 59/84 in `test_net_transport.cpp:3062-3145` (LobbySettings-driven); the .gtl save format (ModeState is not persisted); parity goldens (`state_dump.cpp` does not dump ModeState) | |

> **Trap (carry into the bump commit message):** the NEW world-state size (**537**) numerically equals the OLD entity-count offset (**537**). A hand-edit that confuses the two constants passes a careless local read-through; re-derive both by printing, and check them against each other.

---

## 4. B — Engine view model

### Representation: a parallel member, not `viewob[]`

`screen` (`include/openglad/interface/screen.h:412-413`) gains:

```cpp
// Camera view: engine-owned, display-side only, NOT a seat.
// Lives outside viewob[]/numviews by design — no seat loop can reach it.
std::unique_ptr<viewscreen> camera_view_;
std::int32_t camera_entity_id_ = 0;   // last-synced declaration
std::uint8_t camera_style_ = 0;
bool camera_docked_ = false;          // per-machine resolution (§6)
```

plus methods `sync_camera_views()`, `relayout_camera_view()`, `draw_camera_view_world()`, `draw_camera_view_ui()`, and `int layout_pane_count() const` — returns `numviews + (camera_docked_ ? 1 : 0)`.

**Why not `viewob[4]`:** slot 4 satisfies the indexed `i < numviews` loops, but range-for loops over the whole array *do* reach it (`src/interface/sdl_context_services.cpp:136-142` control clear; `src/platform/sdl/game.cpp:263` claim loop; `tests/integration/test_glad_hud.cpp:352-378` ViewSetRestore), `screen::cleanup` resets it implicitly rather than by named intent, and 4-seat + camera coexistence saturates the array. A parallel member makes every hazard-class site safe **by construction**.

### Camera viewscreen identity

Constructed via a new factory `viewscreen::make_camera(screen*)` (the normal ctor at `src/interface/render/view.cpp:351-397` is seat-flavored):

- **`camera_view_ = true`** — a new explicit `bool` flag on `viewscreen`, set only by the factory (design-review ruling). Both `viewscreen::redraw` overloads **skip the chrome-scope block** (`ScopedGameplayUiCanvas` + `ScopedGameplayUiViewLayout`, `view.cpp:944-955` and `~1221-1231`) when set — the camera must not depend on the coincidence that `compute_view_layout(…, mynum=-1, …)`'s default arm clamps to quadrant 3. The flag is also the greppable handle for `TESTING` asserts.
- `mynum = -1` — safe in all mynum roles because nothing dispatches to it: it never enters `screen::input/process_input` (not in `viewob`), so the `players[mynum]` OOB sites (`view.cpp:1324`, `:1372`) are unreachable; `look_up_key_held` guards `mynum < 0` (`view.cpp:327-337`); `publish_primary_render_sample`'s `mynum != 0` gate (`view.cpp:231-235`) keeps the parity render sample on seat 0; `options::load` (`view.cpp:2271-2278`) and `load/save_player_hud_settings_from_cfg` (`src/interface/input/input_state.cpp:972, 1004`) refuse it, touching no player cfg keys.
- `global_player_index_ = -1` — the documented "no seat" sentinel; silences the radar gate (`view.cpp:948-952`) and HUD ownership tests **without** setting `following_` (avoiding the FOLLOWING caption at `src/interface/score_panel.cpp:739`, which fires on `following_` alone).
- `following_ = false`; `prefs` explicitly seeded (`PREF_VIEW = FULL`, radar/overlay off); `apply_hud_settings_from_cfg` skipped entirely.

**Independent hardenings (taken regardless — design-review ruling):**
- `viewscreen::prefs` gets an in-class initializer `signed char prefs[10] = {};` (`include/openglad/interface/render/view.h:248`). All four seats overwrite it via `options::load`, so it is inert for existing behavior and closes the uninitialized-read class wholesale.
- `screen::reset` (`src/interface/screen.cpp:1305-1322`) bounds its `kPvpConstructionOrder` index explicitly at `n < 4` — the latent OOB (guard admits `numviews == 5`, array is 4-wide) is closed even though this design never raises `numviews`.

### Audited seat-loop list

**Sites needing NO change** — the camera is not in `viewob[]` and `numviews` never counts it, so these loops cannot observe it:

| Site | Why safe |
|---|---|
| `screen.cpp:1471` clear, `:1553` input, `:1564` continuous_input, `:1573` process_input, `:1826-1845` draw_panel_chrome, `:1853-1870` do_notify, `:1872-1878` clear_all_view_text | iterate `viewob[i < numviews]` only |
| `screen.cpp:82-91` find_follow_leader, `:200-218` cleanup_dead_view_controls, `:240-287` sound/notification targeting | same; the camera keeps its target across "dead" sweeps by construction |
| `screen.cpp:1192-1202` min_view_zoom_scale_num; pause `view_zoom_step_allowed` (`pause_menu.cpp:1084-1101`) | the camera never votes on canvas zoom |
| `screen.cpp:1155-1189` initialize_views, `:1294-1330` reset | seat constructors only; `numviews ≤ 4` invariant preserved |
| `results_screen.cpp:110-122` win/lose classification | camera excluded → VICTORY/DEFEAT never downgraded to MATCH OVER |
| `score_panel.cpp:705-1010` per-view HUD loop, `:886-887` compact rule; `radar.cpp:335` alpha rule; `screen.cpp:1834-1836` border suppression | constraint 7 satisfied **by construction** — all stay keyed on `numviews` = humans |
| `pause_menu.cpp:1048-1102` seat_view / zoom loop / collect_pause_seats | camera never addressable as a pause seat |
| `glad_gameplay.cpp:183-241`, `game.cpp:260-289` seat claiming | camera never handed a hero / `set_user(4)` |
| `local_transport_shadow.cpp:514-523` compute_local_player_count, `:1133-1206` control sync + team stamp, `:1446-1466` mutation gate, `:1942-2172` seat add/remove, `:2247-2706` server seeding, pause-overlay loops `:1019-1078`/`:2963-2980` | `numviews` stays a pure seat count; seat surgery writes indices ≤ 3, never the camera member; camera control is never snapshot-stomped |
| `glad.cpp:103-112` + `tests/e2e/wasm-game.spec.js:781,789` | `__opengladNumViews` unchanged |
| `pixie.cpp:195-203` on_screen | no live callers (verified); unchanged |

**Sites CHANGED (the accepted explicit hooks):**

1. `screen.h` — the members/methods above.
2. `screen::cleanup` (`screen.cpp:1250-1262`) — one line: `camera_view_.reset(); camera_docked_ = false;`.
3. `screen::redraw` (`screen.cpp:1477-1495`) — `sync_camera_views()` as the **first statement** (§5); after the seat loop, `draw_camera_view_world()` (docked pane world pixels; no-op otherwise).
4. `screen::relayout_views` (`screen.cpp:1213-1248`) — recompute `camera_docked_` at the **top**, before the seat resize loop consumes `layout_pane_count()`; apply camera geometry via `relayout_camera_view()` at the tail (design-review ruling — a tail-only recompute lets 4 seats resize against a stale docked flag for one relayout).
5. `viewscreen::resize(char)` (`view.cpp:1906`), `GameplayUiProjector` ctor (`view.cpp:1945-1972`), `ScopedGameplayUiViewLayout` ctor (`view.cpp:2016-2047`) — replace `active_screen()->numviews` with `active_screen()->layout_pane_count()` in the `compute_view_layout` call. When no docked camera exists, `layout_pane_count() == numviews`: value-identical, the byte-identity OFF state (constraint 7).
6. `view.h` — `camera_view_` flag + prefs zero-init; `view.cpp` — `make_camera` factory + the two chrome-scope skips.
7. `game_loop.cpp` — inset draw hook at the two seams (§6). (The sync call site lives in `redraw()`, not here.)

---

## 5. C — Lifecycle

**Owner:** the interface layer, per machine (constraint 5). One idempotent, diff-based pass:

**`screen::sync_camera_views()` runs as the first statement of `screen::redraw()`** (`screen.cpp:1477`) (design-review ruling). This seam covers the main loop (`game_loop.cpp` → `s.redraw()`), `render_pending_redraw` (`draw_panels` → `redraw`), the demo/capture path (`demo.cpp:651-660` calls `draw_panels`/`redraw` directly and never runs `game_frame` — the proof-media plan depends on this), and every integration test that calls `redraw()` directly — while the authoritative server never calls `redraw()` at all.

**Gate:** `(world().type & TYPE_SCRIPTED) && world().mode.active && world().mode.cameras[0].entity_id != 0` governs *materialization and retargeting*; when the gate fails while `camera_view_` exists, the pass must still run its **destroy** branch (otherwise a cleared slot strands a live pane). **Plus the display-screen identity belt** (design-review ruling): skip the sync when `og::runtime::current_session != nullptr && this != og::runtime::current_session->myscreen_` — exact screen identity, so a test that calls `server_screen->redraw()` can never materialize a camera on the authority. (The transport-mode predicate at `view.cpp:1458-1463` is NOT a server-screen detector — it fires on display screens; do not use it.)

The pass diffs the declaration (`camera_entity_id_`/`camera_style_`/local seat count/canvas dims) against current state:

- **Materialize:** slot 0 `entity_id != 0`, no `camera_view_` → build via `make_camera`, resolve docked-vs-inset (§6), if docked call `relayout_views()` once (seats re-arm into quadrants against the new `layout_pane_count()`), size the camera via direct-geometry `resize(x, y, w, h)` (`view.cpp:1842-1863` — pins `slot == window`, publishes no present slice, never perturbs the presentation partition `screen.cpp:1231-1246`).
- **Retarget (every frame):** `camera_view_->control = world().find_by_id(entity_id)` with the standard triple guard (`nullptr || dead() || dormant()`) — the demo-Boss re-aim pattern (`src/platform/sdl/demo.cpp:229-266`). **Target loss: keep the pane and keep drawing with `control = nullptr`** — `viewscreen::redraw` falls back to the LevelVisuals free camera (confirmed by the `demo.cpp:238` comment), a wide static shot rather than a stale/black quadrant (design-review ruling). **Multi-floor:** the retarget adopts the resolved target's floor onto the camera view each frame (the follow-camera set-floor mechanism; the bare-set-floor Teleport snap is acceptable — desirable, even — for a camera pane) (design-review ruling).
- **Destroy:** slot cleared (`entity_id == 0`) → reset the member; if it was docked, `relayout_views()` once (seats fall back to the 3-view layout).
- **Docked-flip on seat change:** seat add/remove ends in `relayout_views()` (`local_transport_shadow.cpp:2035, 2170`), which re-resolves `camera_docked_` at its top (§4.5) and recomputes geometry. 3→4 seats flips docked→inset with **no intermediate 5-pane layout observable** (pinned by test). No stranded state: geometry is always derived, never accumulated.
- **Idempotence across teardown:** `cleanup()` / `ready_for_battle()` / `load_saved_game` (`game.cpp:110-120`) destroy the camera via the cleanup hook; the next `redraw()` re-materializes it from the still-replicated ModeState.
- **End of match:** the win latch freezes mode Lua (`mode_tick.cpp:204-208`), so the declaration freezes; the camera keeps drawing the frozen target through the results fade (results classification unaffected — §4); level-end `cleanup()` destroys it. Next level: `world.mode = {}` on load (`src/resources/level_file_io.cpp:234`) clears the slot for free.
- **Staged/preview worlds:** picker previews draw via `viewscreen::redraw` on a borrowed view, never `screen::redraw` on a gameplay session, and the identity belt covers any residual path — pinned by a dedicated test (§9) (design-review ruling).
- **Mirrors, first frame:** mirrors never run mode Lua; the camera appears only after the first snapshot lands. The host-and-join test asserts the camera is ABSENT before the first apply and present after (§9).
- **Per-frame cost:** steady state is two integer compares and one `find_by_id`; construction happens only on change — no per-frame ctor/cfg/radar churn.

---

## 6. D — Layout & render

### Per-machine style resolution (constraint 5)

`camera_docked_ = (style == kCameraStyleAuto) && (numviews == 3)`. With 1/2/4 local seats — or style `inset` — the camera is an inset on the GameplayUI canvas: near-minimap blocks at 1 and 2 seats, the centered pane at 4 (the one `switch` in `relayout_camera_view`). Computed independently on every machine from **local** `numviews`; the replicated declaration carries no seat-count-derived decision, so host and joiners with different seat counts each render correctly and nothing desyncs.

### Docked (3 seats)

- Seats: `layout_pane_count() == 4` flows into the three layout call sites (§4.5), so `compute_view_layout(4, i, mode, …)` puts seats 0–2 in quadrants 0–2 via the existing `default:` arm (`include/openglad/interface/render/view_layout.h:142-152`) — **no new layout arm, no change to `view_layout.h`**; every pin in `tests/unit/test_view_layout.cpp` survives untouched.
- Camera: `relayout_camera_view()` computes `project_view_layout(compute_view_layout(4, 3, kModeFull, ui_w, ui_h))` — the same pure pipeline seats use, called explicitly — then applies it via direct-geometry `resize(x, y, w, h)`. Quadrant 3, zoom-neutral, no present slice.
- Draw: `draw_camera_view_world()` in `screen::redraw` calls the camera's `redraw()` (radar/HUD silenced by `global_player_index_ = -1`; chrome scope skipped by the `camera_view_` flag; text feed empty because `do_notify` can't reach it). A minimal bevel border for the camera pane is drawn by the camera hook itself on the GameplayUI canvas — `draw_panel_chrome` (`screen.cpp:1826-1845`) is untouched, so the `numviews == 4` border-suppression rule never fires at 3 seats (constraint 7).
- **Accepted quirk:** player 0 keeps its full (non-compact) panel and opaque radar in a quadrant-sized pane, because the density rules stay keyed on `numviews == 3` (constraint 7 mandates this). The panel anchors to pane margins so it degrades dense, not corrupt; verified visually in the WP4 capture gate. If genuinely unacceptable, that is a maintainer ruling to amend constraint 7, not a silent re-key.

### Inset (1/2/4 seats)

- Geometry (pinned), **4 seats**: **GameplayUI canvas coordinates** (fixed classic density — immune to world-canvas zoom recomposition): `w = ui_w * 3/10`, `h = ui_h * 3/10` (min 96×60), centered: `x = (ui_w - w)/2`, `y = (ui_h - h)/2`. The centre is a pane boundary there (nobody's hero sits there) and the bottom-right corner already belongs to another seat's radar, so centred is right there. (This row used to cover 2 seats as well; the maintainer moved 2 seats to the near-minimap rule below.)
- Geometry (pinned), **2 seats — near-minimap ×2 (maintainer ruling)**: each seat gets the one-seat second-minimap block from its own UI pane, computed with `compute_view_layout(2, i, seat PREF_VIEW, ui_w, ui_h)` and stacked above that seat's radar. Both blocks show the same target with `kCameraMinimapZoomDenominator` zoom. At the classic canvas over scen820, the blocks are `(111, 136, 44, 28)` and `(272, 136, 44, 28)`. `screen::camera_pane_rects_` holds the draw geometry; `draw_camera_view_ui` renders each final pane directly. The border and stale-pixel scrub still visit every rect. `camera_docked_`, `layout_pane_count()`, present slices, and the world canvas are unchanged. One `switch` in `relayout_camera_view` owns the style rule: one seat gets one minimap, two seats get two minimaps, and four seats get the centered inset.
- Geometry (pinned), **1 seat — the SECOND MINIMAP (maintainer ruling)**: the camera uses the radar block mirrored above the radar. `radar_block_extents(grid_w, grid_h)` supplies its size and `radar_block_for_pane(...)` supplies its placement, so radar and camera geometry share one rule. At the classic canvas over scen820, the camera is `(272, 136, 44, 28)` and the radar is `(272, 168, 44, 28)`. The pane keeps that final rect and shows a world window four times wider and taller. `viewscreen::render_denominator_` enlarges the world window without enlarging `xview` or `yview`; tile bounds use the world-window dimensions, and tiles, decor, walkers, shadows, reflections, particles, glows, floor masks, and weather project into final pane coordinates. Indexed images use `video::putbuffer_projected`, whose loop visits destination pixels and samples the source image for each pixel. No full-scene scratch surface or camera downsample API exists. Docked and four-seat panes use denominator 1 and retain the classic arithmetic.
- **Draw order — above HUD chrome, below pause UI.** Both seams must be patched (`hud` recon):
  1. `og::runtime::detail::render_pending_redraw`, `src/platform/sdl/game_loop.cpp:~359` — after `score_panel(&s, 1)`, before `s.buffer_to_screen(…)`;
  2. the main render path, `game_loop.cpp:~561-567` — after `score_panel(&s)`, before `s.refresh()`.

  At each seam: `{ ScopedGameplayUiCanvas ui(s); s.draw_camera_view_ui(); }` — the established idiom (touch controls do exactly this at `:560-565`). `draw_camera_view_ui()` renders the camera's world content via the data overload `camera_view_->redraw(&level_runtime_data, /*draw_radar=*/false)` under a canvas scope with the inset direct geometry — the composition the picker staged preview ships today (`src/interface/ui/picker_team_build.cpp:996-1103`: `ScopedBorrowedView` + direct-geometry resize + `redraw(data, false)` inside a canvas scope) — plus a 1px border. The GameplayUI overlay composites after all world slices (`src/platform/sdl/sai2x.cpp:1860-1913`), so the inset sits above every piece of seat HUD chrome; the pause menu draws on its own separate 320×200 modal canvas with its own present (`pause_menu.cpp:1352-1356`), so "below pause UI" is free.
- Because the inset never touches the World canvas or the layout: `layout_pane_count()` stays `== numviews`, `relayout_views` never sees it, the layout-conformance helpers (`test_game_loop.cpp:5786-5806, 6333-6356`) stay green by construction.
- **Open question resolved at WP4-prototype time (design-review ruling):** verify whether the GameplayUI canvas is cleared every frame. If the overlay persists across frames, every structural camera transition (destroy, docked↔inset flip, geometry change) must clear the previous inset rect on the GameplayUI canvas and set `redrawme = 1`; an integration pixel probe at the old inset location after a nil-clear pins it either way.

### Suppression mechanisms (all by-construction, no new gates in HUD code)

- Radar: `global_player_index_ = -1` fails the gate at `view.cpp:948-952`/`1223-1229`; inset additionally passes `draw_radar = false`. The 60×44 radar-anchor overflow hazard for small panes (`radar.cpp:210-267`) is unreachable as a hard property.
- Seat HUD / mode panel / beacons / respawn countdown / FOLLOWING caption / notification feed / pause banner: the camera is absent from every loop that draws them (`score_panel.cpp:705-731`, `screen.cpp:1853-1870`, `local_transport_shadow.cpp:1019-1078`).
- Per-walker UI overlays — mini HP bars and damage/heal numbers (code-review finding R1): a camera pane draws NONE of them. Both blocks in `walker_draw.cpp` are gated on the pane's explicit `camera_view_` flag (the chrome-scope mechanism, never the `mynum==-1` clamp coincidence), because their GameplayUI projection is keyed on `layout_pane_count()` + `mynum` and has no camera arm — a 1-seat inset would scale them across the full UI canvas and 4 seats would stamp them into seat 3's quadrant.
- Zoom composition: absent from `min_view_zoom_scale_num` and `view_zoom_step_allowed`; direct geometry pins `slot == window` → zero present slices → the no-camera present path is byte-identical.
- **Jitter-0 discipline (constraint 4):** the camera draw paths make zero game-rng calls — they render exactly what a seat pane renders; the sync pass is pure integer state; the camera never draws a radar (the beacon-rng site).

---

## 7. E — Lua API

```lua
og.set_camera_view(slot, entity_or_nil [, opts])
-- slot: 0 (raises outside [0, kModeCameraViews))
-- entity_or_nil: walker handle to follow; nil clears the slot
-- opts (optional table): { style = "auto" | "inset" }  -- default "auto"
```

- **Semantics:** host-authored, replicated declaration: "a camera exists and follows this entity". `"auto"` = each machine docks it into the free quadrant when it locally has exactly 3 seats, else draws a centered inset. `"inset"` = always inset everywhere. **There is deliberately no `"docked"` force**: docked-ness depends on local seat count, which differs per machine (constraint 5) — a forced value would be a replicated decision derived from seat count, the exact desync the constraint forbids.
- **Implementation:** `og_set_camera_view` in `src/gameplay/script/bindings_entity.cpp`, modeled 1:1 on `og_set_beacon` (`:2642-2668`): `world_arg`, slot bounds check with a range-naming error, `resolve_walker_or_nil` (nil → default-constructed slot), opts validated in the `og_summon_configured` shape (`:956-1051` — typed `lua_getfield` checks, `lua_next` walk, unknown-key rejection, nothing applied until all checks pass), style string mapped to byte {auto=0, inset=1}, then a plain POD write `world->mode.cameras[slot] = {…}`. No events, no RNG. All validation write-side (constraint 3).
- **Registration:** one row `{"set_camera_view", og_set_camera_view}` in `kOgWorldFuncs`' scripted-mode block (`:3224-3246`, beside `set_beacon` at `:3233`). The load-time world fence wraps it automatically (`:3562-3582`); it must NOT join `kUnfencedWorldFuncs`.
- **Stub regeneration:** canonical leading comment `// og.set_camera_view(slot, entity_or_nil [, opts]) — …`, then regenerate `docs/modding/og-api.d.lua` via `scripts/modding/gen_api_stubs.py` (`api_stub_check` gates `coverage_report`, `cmake/OpenGladCoverage.cmake:106-142`).
- **Cost (constraint 9):** set-**once** in `on_mode_init` — the ball/shadow ids are stable for the level (spawned once, never destroyed; kickoff resets re-spot, never respawn — `mode_soccer_impl.lua:154-167`). ModeState replicates wholesale every snapshot regardless, and the display re-resolves the id every frame. Zero per-tick instructions; the 500k-budget probes (`test_modes_soccer.cpp:2530-2556`) gain one init-time call only.

---

## 8. F — Mode integration / G — Headless surfaces

- **Soccer** (`campaigns/modes/packs/modes.core/lib/mode_soccer_impl.lua`): in `on_mode_init`, after the ball spawn + `S.BALL_ENTITY` bank (`:965-971`): `og.set_camera_view(0, ball)`.
- **Basketball** (`mode_basketball_impl.lua`): in `on_mode_init` after the shadow spawn (`:2384-2395`): `og.set_camera_view(0, shadow)` — the **shadow** (`S.SHADOW_ENTITY`), not the ball: the ball entity draws lifted by up to ~40px of fake Z (`sync_render` `:1759-1765`) and a ball-following camera would bob on every shot arc; the shadow is the stable ground-truth position (the same reason beacon 0 is the shadow, `:1766`).
- **Authoring location:** impl-side, not the `mode_levels.lua` manifest — both modes want the camera unconditionally; a manifest key would be a second site expressing one rule with no consumer of the variability ("no rule twins"). The manifest (`mode_levels.lua:744-769`) is the documented future home if per-level opt-out is ever wanted.
- **Intermission/kickoff:** no extra calls — resets re-spot the same entity; the camera glides with it. Mode binders (`scripts/mode_soccer.lua`, `mode_basketball.lua`): untouched.
- **openglad_text** `json_mode` (`src/platform/text/text_protocol.cpp:150-186`): after the beacons array, emit `"cameras":[{"id":N,"style":S}]` occupied-slots-only (the beacon idiom) — the observability handle for headless real-session proof. Pin fallout: both JSON literals in `test_platform_headless.cpp:862-876` repinned.
- **Curses policy: ignore the channel.** `openglad_curses` has one viewport and no viewscreen concept (`src/platform/curses` never references `viewob`/`numviews`); its beacon treatment (bold glyph) already marks the ball. No code change; recorded here so a later "curses split view" doesn't look like an omission.

---

## 9. H — Test plan

**Moved pins:** exactly the §3 fallout table (items 1–9). **Everything else stays green untouched**, notably: `test_game_loop.cpp:915-919` (server no-extra-views — kept verbatim, now doubly protective), all of `test_view_layout.cpp`, the layout-conformance helpers, the seat add/remove numviews pins (`test_game_loop.cpp:6404-6428, 6516-6533`), the wasm e2e numviews pins, LobbyState offsets, parity goldens.

**Parity (constraint 11):** no touched file carries mutation-canary pins (verified against `tests/parity/scenario_table.h`). `og_test_parity` runs 187/187 after each WP; `check_mutation_pins.py` is a build dep and catches accidents.

**New tests, per layer (no new test binaries → `scripts/coverage/recorder_processes.txt` untouched):**

- *Unit, `og_unit_mode`* — `tests/unit/test_mode_bindings.cpp` (template `:230-250`): set slot 0, clear with nil, slot 1 pcall-fails (out of range at 1 slot), `style="inset"` maps to 1, unknown style string fails, unknown opts key fails, fence raises at pack top level. `test_mode_snapshot.cpp`: camera in populate/match/defaults + offset comment re-derivation. **Crafted style byte > 1 deserializes to `kCameraStyleAuto`; legitimate bytes round-trip byte-identical** (the §3 defense).
- *Unit, `og_unit_sim`* — round-trip + delta-merge of the camera slot (`test_world_snapshot.cpp`); replay diff labels `mode.cameras[0].entity_id`/`.style` (template `tests/unit/test_replay.cpp:554-596`); the re-derived wire pins.
- *Unit, `og_unit_soccer` / `og_unit_basketball`* — after init: `mode.cameras[0].entity_id == var(BALL_ENTITY)` (soccer) / `== var(SHADOW_ENTITY)` (basketball, pinning the fake-Z decision); budget probes still green.
- *Mirror oracle* — extend `replicate_to_mirror` (`tests/modes_pack_fixture.h:963-990`): server sets a camera, keyframe → mirror apply → `compute_snapshot_hash` equal and `mirror.mode.cameras[0]` populated.
- *Unit, `og_unit_headless_platform`* — repinned JSON literals + a populated-cameras shape.
- *Integration* — new file `tests/integration/test_camera_view.cpp`, added to `ALL_INTEGRATION_TEST_SOURCES` and assigned to the **existing** `og_test_view` group (`cmake/OpenGladTests.cmake:440-456`; existing group avoids the recorder_processes set-equality trap):
  1. **Constraint-7 ON-state pin, written FIRST, before the docked path lands** (design-review ruling): 3 seats + docked camera live → seat 0 panel non-compact, seat radar alpha 255, seat borders exactly today's 3-view chrome. Holds by construction in this skeleton; the pin exists so a future refactor cannot silently re-key the rules.
  2. Display screen materializes `camera_view_` when the mirror's ModeState declares slot 0; server screen's `camera_view_` stays null and `viewob[3..5)` null (re-assert the 915-919 property on both screens).
  3. 3 seats + auto → docked: each seat's live rect equals `project_view_layout(compute_view_layout(4, i, …))`, camera rect equals quadrant 3.
  4. 1 seat + auto → inset: camera has inset UI-coords geometry; seat geometry byte-identical to the no-camera run.
  5. Seat add 3→4 flips docked→inset **with no intermediate 5-pane layout observable** (design-review ruling); seat remove flips back.
  6. `ready_for_battle`/`load_saved_game` teardown → next redraw re-materializes (idempotence).
  7. Unresolvable id → pane persists and keeps drawing via the free-camera fallback; no layout change.
  8. OFF-state byte-identity: no declaration → `layout_pane_count() == numviews`, zero present-slice or geometry deltas.
  9. **Staged preview** of a camera-declaring mode draws with `camera_view_ == nullptr` and no geometry change (design-review ruling).
  10. Inset pixel probe: after a nil-clear, the old inset rect on GameplayUI holds no stale camera pixels (shape depends on the WP4 canvas-clearing answer, §6).
- *Real-session proofs (not fixtures):*
  - (a) `openglad_text` driving a real soccer level (scen820) over the real client/server path, asserting the `"cameras"` key in `json_mode` — a genuine headless session.
  - (b) SDL proof: 3-seat and 1-seat soccer sessions via the demo/capture harness (`scripts/media/capture_showcase.sh`, output to `build/media/`, published to openglad-screenshots per the standing rule) showing the docked quadrant and the centered inset live.
  - (c) **Host-and-join** `glad_init` soccer session over the local transport shadow, run in the ASan lane (design-review ruling): authority screen camera-free (re-assert the pin), the display mirror's camera **absent before the first snapshot apply and present after**, camera follows the replicated ball id; merged with the presentation-divergence proof — host 3 seats shows docked while joiner 1 seat shows inset, zero desync strikes.
- *Coverage lane:* every new C++ function (binding, sync pass, draw hooks, factory) entered by at least one test (function bar = 100%); new Lua lines exercised by the mode tests; `api_stub_check` green.

---

## 10. I — Work packages

| # | Package | Kind | Depends | Verification gate |
|---|---|---|---|---|
| WP1 | **Wire channel + version bump** — `ModeCameraView` in mode_state.h; serialize/deserialize append + style identity clamp; `compare_mode_snapshot_state` loop; the coordinated triple bump; ALL §3 pin re-derivations; mode-snapshot/world-snapshot/replay/net-transport test updates | MECHANICAL (follow the bump checklist exactly; re-derive by printing serializer output; mind the 537/537 trap) | — | `og_unit_sim`, `og_unit_mode` green; VALIDATE_SERIALIZATION lanes green; `og_test_parity` 187/187 |
| WP2 | **og.set_camera_view binding** — shim + validation + registration row + canonical comment + stub regen | MECHANICAL | WP1 | `og_unit_mode` binding tests; `api_stub_check`; `check_luals` |
| WP3 | **Interface view model + lifecycle** — camera member/factory + `camera_view_` flag, `mynum=-1` identity, prefs zero-init + kPvpConstructionOrder clamp, `layout_pane_count()` + the 3 layout call sites, `sync_camera_views` as `redraw()`'s first statement + gate + identity belt, relayout top-recompute/tail-apply, cleanup hook, docked draw + bevel | **HARD** | WP1 | integration tests 1–9 (constraint-7 pin written first); OFF-state byte-identity; server pin; `og_test_view` full group; ASan lane |
| WP4 | **Inset render at the seams** — `draw_camera_view_ui` via the preview mechanism, both game_loop seams, border, stale-pixel answer + probe test | **HARD** (least-proven render path — prototype the GameplayUI-canvas world draw first) | WP3 | integration tests 4, 10; visual capture proof (both styles); no present-slice deltas |
| WP5 | **Mode Lua** — one line each in soccer/basketball init + mode unit tests + mirror-oracle tests | MECHANICAL | WP2 | `og_unit_soccer`/`og_unit_basketball` incl. budget probes; mirror hash tests; Lua line gates |
| WP6 | **Headless surfaces** — json_mode `"cameras"` + repin headless literals; curses no-op recorded | MECHANICAL | WP1 | `og_unit_headless_platform`; openglad_text CTest entries |
| WP7 | **Proof + ship** — real-session proofs (text client, 3-seat/1-seat captures, host+join ASan), full `ctest --preset ci-test`, coverage lane, PR media to screenshots repo | MECHANICAL | WP3–6 | full CI green incl. coverage + wasm e2e; PR checklist |

Order: WP1 → {WP2, WP3, WP6} → {WP5, WP4} → WP7. WP1 is one commit (constraint 2). WP3 and WP4 are the only packages needing the expensive model.

---

## 11. Top 5 risks, ranked

1. **Inset = live-world viewscreen drawn into the GameplayUI canvas** — the only genuinely novel render path. Mitigated: composes two shipped mechanisms (picker staged preview `picker_team_build.cpp:996-1103`; touch-controls seam idiom `game_loop.cpp:560-565`); WP4 starts with a standalone prototype gate. Detected by the inset integration test + mandatory visual capture. Fallback (pre-approved deviation if untenable): draw inset world pixels on the World canvas below chrome, keep only the border on GameplayUI, record the §6 deviation for a maintainer ruling.
2. **`layout_pane_count()` at the three layout call sites perturbing existing geometry.** Mitigated: pure `numviews + docked?1:0`, value-identical with no camera; detected by the untouched `test_view_layout.cpp` pins, the layout-conformance helpers on camera-free runs, and the docked test pinning seat rects to `compute_view_layout(4, i, …)`.
3. **Version-bump pin fallout done wrong** (hand-edited +N offsets, stale hello bytes — the "silently stale for months" precedent; plus the 537/537 numeric coincidence, §3). Mitigated: one commit, every literal re-derived by printing serializer output; detected by VALIDATE_SERIALIZATION lanes, the round-trip suites, and the replay-compare loop turning any missed field into a named diff.
4. **Lifecycle thrash/staleness** — materialization churn, docked/inset flapping, stale declarations across transitions or the win-latch freeze, relayout-ordering staleness. Mitigated: diff-based sync (construct only on change), docked-ness recomputed at relayout top (design-review ruling), keep-pane-keep-drawing on target loss, explicit cleanup hook + next-redraw re-materialization; detected by integration tests 5–7 under `--gtest_shuffle` and the ASan lane.
5. **Player 0's full-density HUD in a quadrant pane under docked mode.** Constraint 7 forces the density rules to stay keyed on `numviews == 3`. Mitigated: the panel anchors to pane margins — dense, not corrupt; detected by the WP4 3-seat visual capture; if unacceptable, it needs a maintainer ruling to amend constraint 7, never a silent re-key.
