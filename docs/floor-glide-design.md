# Floor Glide — implementation-ready specification

**Feature:** animated floor transitions for OpenGlad's z-axis multifloor rendering.
**Branch:** `feature/z-axis-multifloor` (repo `/home/yans/code/openglad`). All file:line anchors verified against the working tree at commit `e8779d96` — line numbers shift as you insert code; anchor by the quoted text, not the number.
**Winning mechanism:** Design 4, "Floor Glide — continuous-Z camera dolly (fractional camera floor)", unanimous across all three judge lenses, with the grafts and trims listed in §1.

---

## 0. One-paragraph summary

When the camera's floor changes (control walker took a Z-stair, fell through air, or teleported), the rendered presentation stops snapping. A per-viewport float `camera_z` eases from the old floor to the new floor over 9–16 **render** frames, and every floor's alpha / scale / parallax-scroll is computed from the signed fractional depth `dz = f − camera_z` using the *exact formulas the steady state already uses* (view.cpp:520-528 scale/scroll, view.cpp:1193-1218 alpha). The transition is therefore not a new effect layered on top; it is the existing depth grammar with the camera's z-knob turned smoothly instead of stepped. Stairs read as a calm 267 ms rise/descent; falls as a fast 150–233 ms accelerating drop with an overshoot "thud"; teleports snap (discontinuity is the message). The integer `current_floor_` snaps to the destination on frame 0, so radar, occlusion, entity floor selection, depth-fx stories, shadows, and stair overlays keep integer semantics untouched. The final frame takes the untouched integer code path, so it is byte-identical to steady state by construction; cfg-OFF and single-floor levels never enter any new code. Zero new entity-render passes ⇒ the glide adds zero rng-during-render draws (one accepted exposure, scoped in §2.4: invisible/phantom entities on the destination floor transiently *skip* their per-pixel rng draws mid-glide via the pre-existing faded-floor layer path). No new video API.

---

## 1. Decision record (what won, what was grafted, what was cut)

| Decision | Resolution | Source |
|---|---|---|
| Core mechanism | Fractional `camera_z` dolly through existing formulas; live re-render, never pixel capture | Design 4, unanimous |
| Teleport treatment | **SNAP** — Design 4's 133 ms materialize fade is **CUT** (dims the arrival scene at the moment of maximum disorientation; deletes a cause branch, a capture scene, and coverage burden) | Judges 2+3 override Design 4 |
| Fall impact beat | Two-segment ease-in + overshoot squash **kept**, overshoot boosted **0.12 → 0.25 floors**, and the above-floor scale slope boosted **0.10 → 0.25** during falls (no-hold only) so the overhead rush and the squash are unmistakable at 320×200 and in quarter panes. Endpoint-safe: the boosted channels fade to alpha 0 or return to dz=0 before the glide ends | Judge 1 (parameters → legibility lens) |
| Landing screen-shake | **Not in v1.** Fully specified as Follow-up F2, pre-approved to ship if the fx-review capture shows the squash sub-threshold | Judge 2 (mechanism → engineering lens); judge 3 concurs |
| Departing-floor pass | **Terrain-only** (no `draw_floor_entities`) when the look-up hold is off ⇒ zero new rng draws. Entity-vanish at frame 1 is put explicitly in front of the fx-review capture; fallback ladder in §12 (4-frame entity fade → full entity pass), decided at fx-review, all rungs shippable | Design 4 + judges 1/2/3 directive |
| Ghost-hold interaction | Base-anchored curves (`base = hold ? 48 : 0`), endpoint-exact at both hold states at both ends. **No cancel, no max() kludge.** Invariant: while hold is active no drawn above-floor alpha falls below 48 | Design 4; judges 1+3 formalization |
| Multi-story span | `from_z` clamped to `to ± 3` floors (bounds layer passes and sweep speed on pathological stacks) | Design 3 graft via judge 3 |
| Re-trigger mid-glide | Uniform **newest-event-wins retarget** from the current fractional `camera_z`; no queue, no snap-back, no cause-priority special case | Designs 1+4; judge 2 (uniform is simpler to test) |
| `draw_floor_effects` invariant | Comment at view.cpp:1224-1228 is false mid-glide; **fix the comment**. Verified during spec-writing: `pointb` and all blits go through `E_Screen->render`, which `floor_layer_begin` swaps (video_sdl.cpp:1288-1292), so all camera-pass effects redirect into the layer automatically — **no gating fallback needed** | Design 2 graft via judges 2+3; verified |
| Classification Unknown ⇒ snap | Fresh-tracker / stale / mismatched records never animate | Design 2 graft via judge 3 |
| Depth-fx int `stories` step | v1 keeps the integer snap at frame 0 (masked by peak motion). Follow-up F1: `DepthFxParams` float `strength` (default 1.0 = bit-identical) | Designs 1/3/4; judge 2 designates as follow-up |
| Slide channel (Design 3) | **REJECTED**, with the reason recorded: this game's only meaning for vertical screen motion is N/S map travel; a slide transiently displaces the player's aim reference mid-combat and misreads as knockback. The dolly's scale/alpha channels already encode direction; screen-vertical motion adds noise the grammar actively fights | Design 3's own confrontation, adopted as rationale per judge 1 |
| Pixel capture of the old floor | **REJECTED**: needs a new capture API, freezes entities mid-swing ~250 ms, contains HUD text when world and UI share the 320x200 surface, and must follow world-canvas resizes. Live re-render costs at most one bounded terrain-only pass | Designs 1/3/4 consensus |
| Radar / shadows / clouds | Snap to the destination floor at frame 0 (they key off integer `current_floor_`). **Deliberate**: the minimap is an instrument answering "where am I now"; the world view answers "how did I get there" | All designs; all judges |

---

## 2. Coordinate convention and core math

One convention throughout code and tests:

```
z  = glide camera height (float), sweeping old_floor → new_floor
dz = static_cast<float>(f) − z          // per drawn floor f
     dz < 0  : floor is BELOW the camera
     dz = 0  : camera floor
     dz > 0  : floor is ABOVE the camera
u  = dz (when dz > 0)                    // "how far above", ∈ (0, 1] in practice
```

### 2.1 Per-floor presentation (continuous, exact at integer z)

Constants already in the tree: `kParallaxScroll = 0.05f` (view.cpp:175), `kParallaxScale = 0.10f` (view.cpp:183), `kMinBelowFloorScale = 0.5f` (view.cpp:190), `kFloorBelowAlphaStep = 70` / `kFloorBelowAlphaMin = 90` / `kFloorGhostAlpha = 48` (view.h:131-133), `kGlassAlpha = 100` (view.cpp:168).

New constants (view.cpp anonymous namespace, next to the parallax constants):

```cpp
inline constexpr Sint32 kGlideStairFrames    = 16;    // ~267ms @60fps
inline constexpr Sint32 kGlideFallBaseFrames = 9;     // ~150ms
inline constexpr Sint32 kGlideFallPerStory   = 3;
inline constexpr Sint32 kGlideFallMaxFrames  = 14;    // ~233ms
inline constexpr float  kGlideFallOvershoot  = 0.25f; // floors past destination
inline constexpr float  kGlideFallAboveSlope = 0.25f; // boosted above-scale slope, falls only
inline constexpr float  kGlideSpanClamp      = 3.0f;  // max fractional span
```

**Alpha** (must equal `floor_render_alpha` — view.cpp:1193-1218 — at every integer z):

```
dz <= 0 (at/below):  falpha = clamp(lround(255 + dz*70), 90, 255)
                     // dz=0 → 255 (camera floor); dz=−k → 255−70k min 90; exact in float
dz  > 0 (above):     base   = ghosts_on ? kFloorGhostAlpha(48) : 0
                     falpha = (dz >= 1) ? base
                            : lround(base + (255 − base) * (1 − dz))
                     // dz→0+ → 255 (continuous with camera floor)
                     // dz=1, hold → 48 = steady ghost;  dz=1, no hold → 0 → SKIP the pass
```

`falpha == 0` ⇒ the floor pass is skipped entirely (draws nothing; the handoff to "not drawn" is pop-free because an alpha-0 composite touches no pixels).

**Scale** (about the existing viewport centre `fcx/fcy`, view.cpp:518-519):

```
dz <= 0:  fscale = max(1 + dz * kParallaxScale, kMinBelowFloorScale)   // 1 − 0.10k, min 0.5
dz  > 0:  slope  = (cause == Fall && !ghosts_on) ? kGlideFallAboveSlope : kParallaxScale
          fscale = 1 + slope * dz
          // dz=1 steady/hold → 1.10 = the ghost loom scale players already know
          // dz=1 fall/no-hold → 1.25: the departing floor RUSHES overhead while fading to 0
```

The boosted slope is legal because it only ever shapes frames whose alpha is heading to 0 (departing floor, no hold) or whose dz returns to ≤0 before the glide ends (destination during overshoot); it never has to reconcile with a drawn steady state. Under hold the standard slope keeps u=1 landing exactly on the ghost's 1.10/48 steady presentation.

**Parallax scroll** (replaces the integer factor at view.cpp:522-524 / :777-779):

```
pf = dz * kParallaxScroll        // applied as today: topx += topx*pf, topy += topy*pf
shift applied when: floor_count > 1 && dz != 0     // inactive path: f != current_floor_, identical
```

**Rounding-collapse property (document in code):** near the end of a glide `dz` residual is ≤ 2.5e-4 (stairs) / ≤ 0.028 (falls), so `falpha` rounds to 255 and `use_layer` (view.cpp:533 — condition text unchanged: `floor_count>1 && falpha<255`) goes false: the destination floor drops back to the direct-draw path one or two frames before the glide formally ends, with sub-pixel scale/scroll error. This is desirable (no bilinear residue) and harmless.

### 2.2 Frame clock and easing

Per-viewport counter, decremented **once per that viewport's redraw** — render frames, never wall clock (shared-machine load spikes flake wall-clock tests; Emscripten-safe). Split-screen panes glide independently.

```
On trigger redraw:  glide_total_ = N;  glide_frames_left_ = N − 1;  render frame index i = 1
Each later redraw:  --glide_frames_left_;
                    if (glide_frames_left_ == 0) → glide inactive, THIS frame renders the
                        untouched integer path (frame i = N == steady state, exact)
                    else i = glide_total_ − glide_frames_left_;  t = i / N
```

Animated frames are `i = 1 .. N−1`; **t = 1 is never evaluated in transition code** — final-frame exactness is structural, not numeric (Design 4's key property; the endpoint test in §10 pins it).

**Stairs easing** (ease-out cubic — decisive launch, soft settle):

```
z(i) = to − (to − from_eff) * (1 − t)^3          t = i/N,  N = kGlideStairFrames = 16
```

Penultimate frame residual `(1/16)^3 ≈ 2.4e-4` floors ⇒ alpha within 1 quantum of steady, invisible.

**Fall easing** (two segments: gravity-shaped acceleration into an overshoot squash, then settle):

```
N = min(kGlideFallBaseFrames + kGlideFallPerStory*(stories−1), kGlideFallMaxFrames)
    stories = min(|Δ|, 3)                 // Δ=1→N=9, Δ=2→N=12, Δ≥3→N=14
M = round(0.7 * N), clamped to ≤ N−3      // N=9→M=6, N=12→M=8, N=14→M=10 (≥3 settle frames)
z_ov = to − kGlideFallOvershoot           // 0.25 floors PAST the destination, downward

i ≤ M:        z(i) = from_eff + (z_ov − from_eff) * (i/M)^2          // ease-in quadratic
M < i ≤ N−1:  s = (i−M)/(N−M)
              z(i) = z_ov + (to − z_ov) * (1 − (1−s)^2)              // ease-out settle
```

During overshoot the destination floor sits at `dz ∈ (0, 0.25]` (transiently "above"): alpha dips to ≈191 (25 % dim), scale kicks to ≈1.0625 with the boosted slope (~10 px edge displacement at a 320-wide pane, ~3 px in a 4-way ~96 px pane) — the kinesthetic "thud", composing with the untouched grey landing smear (effects.cpp fall cues) and the untouched sim snap. Settle residual at i=N−1: `(1/(N−M))^2 * 0.25 ≤ 0.028` floors ⇒ ≤7 alpha quanta to the final frame, sub-visible.

**Retarget (re-trigger mid-glide):** newest event wins, uniformly. `from_eff = current glide_camera_z_` (continuous by construction — never snaps back), `to = new floor`, cause = the NEW event's cause, `N` recomputed from the NEW event's integer span. No queue. Then apply the **span clamp**: `from_eff = clamp(from_eff, to − kGlideSpanClamp, to + kGlideSpanClamp)`.

### 2.3 Loop-bound extension

```
steady_top = (floor_count > 1 && ghosts_on) ? floor_count − 1 : current_floor_   // exists today, :505-506 / :760-761
glide_top  = glide_active ? max(steady_top, min(floor_count − 1,
                                (Sint32)std::ceil(glide_camera_z_)))
                          : steady_top
```

`ceil(z)` keeps the departing floor in the loop exactly while its `dz < 1` (alpha > 0); the `falpha==0` skip rule covers the boundary. Up-glides never extend (`ceil(z) ≤ current_floor_`). The multifloor pre-clear (view.cpp:487-490 / :746-748) is already gated `floor_count>1` and needs no change.

### 2.4 Terrain-only departing pass

```
draw_entities(f) = ghosts_on || f <= current_floor_
```

This condition is **impossible to violate in steady state** (without the hold, floors above `current_floor_` are never in the loop), so writing it unconditionally changes zero steady behavior. During a down-glide without hold, the departing floor(s) render terrain + decor only: **no `draw_walker` calls ⇒ no `walkputbuffer` INVISIBLE/PHANTOM rng draws ⇒ the glide's extra passes add zero rng draws** (up-glide floor set = post-steady set; down-glide extra passes are entity-free; hold state = today's hold pass set). Skipping `draw_floor_entities` also skips that floor's `draw_floor_effects` — correct, terrain only. Under an active hold the pass keeps entities exactly as the hold does today.

**Scope correction (found while writing test 10.2-8):** the guarantee above is *zero NEW rng draws*, not strict ON-vs-OFF rng-stream identity. Mid-glide the destination floor is `current_floor_` but renders at `falpha < 255`, so its entities take `draw_walker`'s pre-existing faded-floor layer path (plain sprite blit — no INVISIBLE_MODE/PHANTOM fill, no rng). An invisible or phantom entity standing on the destination floor therefore *skips* the per-pixel rng draws the cfg-off camera-floor frame would have burned, for up to N−1 frames per transition, so the ON and OFF render-rng streams differ on those frames. That mode switch is the shipped below-floor grammar (the same ghost-hold-class rng exposure §12 F3 already accepts), not a new entity pass; parity is render-blind (§9) and no wire state is involved, so it is recorded as an accepted exposure rather than gated. Test 10.2-8 pins the true invariant: the departing terrain-only pass consumes exactly zero rng.

Known accepted quirk (goes in front of the fx-review): entities on the departed floor vanish at frame 1 while their terrain fades over the glide. See §12 fallback ladder.

### 2.5 What deliberately snaps at frame 0 (integer `current_floor_` semantics)

Radar rebuild, entity floor selection, occlusion, `draw_upper_floor_shadows` (view.cpp:651-652 / :902-903), `draw_cloud_overlay`, depth-fx `stories` (view.cpp:622-631 / :877-886), stair-overlay floor choice, the pre-clear. During a down-glide the departing floor both casts its (new) above-floor shadows *and* is drawn fading — a ≤16-frame double representation, deliberate (removing the shadows would pop at the end). Mid-glide the avatar belongs to the destination floor and fades in with it on stairs-up (crosses 50 % opacity by frame ≈3.3 of 16, ~55 ms; the departure floor remains fully readable beneath); on descents the destination was already visible at ≥185 alpha. Glass tiles draw opaque-within-layer mid-glide (`talpha` rule, view.cpp:598-599) — ≤16-frame transient, accepted.

---

## 3. Per-cause parameter table (authoritative)

| Cause | Detect | N (frames) | Easing | camera_z path | Above-floor slope | Extra beats |
|---|---|---|---|---|---|---|
| **Stairs up** (Δ=+1) | departure cell on `prev.floor` smooths to `TYPE_ZSTAIRS` | 16 (~267 ms) | ease-out cubic | `from → to` monotone | 0.10 | destination sweeps alpha 0→255 (48→255 under hold), scale 1.10→1.00; old floor settles to exactly 0.90/185 + mist |
| **Stairs down** (Δ=−1) | same stair test | 16 | ease-out cubic | `from → to` monotone | 0.10 | destination (already visible at 0.90/185) grows/brightens into the world; departing floor lifts 1.00→1.10, alpha 255→0, **terrain-only** |
| **Air fall** (Δ<0, not stair, XY within `kFallCueMaxNudgePx`) | existing classifier logic (effects.cpp:985-1005) | 9 / 12 / 14 for 1 / 2 / ≥3 stories | ease-in quad → overshoot `to−0.25` → ease-out settle (§2.2) | non-monotone (squash) | **0.25** (no hold) / 0.10 (hold) | overshoot dip alpha≈191 + scale≈1.0625 thud; existing grey smear + existing detonation-shake mechanics untouched; multi-story = ONE continuous sweep, intermediate floors flash past |
| **Teleport** (anything else: rise without stair, drop beyond nudge, cross-map) | classifier | 0 | — | **SNAP** (today's behavior, exactly) | — | discontinuity is the message; teleporters own their FX |
| **Suppressed** (§5) | — | 0 | — | SNAP + re-baseline | — | — |

Re-trigger: newest event wins; `from_eff` = live fractional z; span clamp ±3.

---

## 4. State layout

### 4.1 `viewscreen` members (include/openglad/interface/render/view.h, insert after `ghost_hold_override_` at :186)

```cpp
// ---- Floor-glide transition (render-only; per-viewport => mirror-safe and
// split-screen-independent, exactly like current_floor_). Inactive whenever
// glide_frames_left_ == 0; the inactive render path is the pre-glide integer
// code, so cfg-off / idle / single-floor frames are byte-identical.
enum class FloorGlideCause : Sint8 { None, Stairs, Fall };
float           glide_camera_z_      = 0.0f;  // valid while active
float           glide_from_z_        = 0.0f;
Sint32          glide_to_floor_      = 0;
Sint32          glide_frames_left_   = 0;
Sint32          glide_total_         = 0;
FloorGlideCause glide_cause_         = FloorGlideCause::None;
// Trigger baseline (previous redraw's view of the world)
std::uint32_t   glide_prev_control_id_ = 0;
std::uint32_t   glide_last_seen_frame_ = 0;   // effects_frame_tick() at last update
const void*     glide_world_key_       = nullptr;  // &GameWorld identity
std::uint32_t   glide_world_tick_      = 0;   // world tick monotonicity check
```

Public introspection (unconditional, header-inline — the `effects_fall_cue_frames_left` pattern):

```cpp
[[nodiscard]] Sint32 floor_glide_frames_left() const { return glide_frames_left_; }
[[nodiscard]] float  floor_glide_camera_z()   const;  // active ? glide_camera_z_ : (float)current_floor_
[[nodiscard]] Sint32 floor_glide_cause()      const { return static_cast<Sint32>(glide_cause_); }
```

Private helpers (declared next to `draw_floor_entities` at view.h:118):

```cpp
void update_floor_glide(GameWorld& vworld, walker* controlob);   // the ONE trigger, both overloads
struct FloorPassParams {
    unsigned char falpha; float fscale; float pf;
    bool shift;      // apply the parallax topx/topy shift this pass
    bool skip;       // alpha==0: render nothing for this floor
    bool entities;   // false => terrain+decor only
};
[[nodiscard]] FloorPassParams compute_floor_pass(Sint32 f, const GameWorld& vworld,
                                                 bool ghosts_on) const;
```

### 4.2 effects.cpp classification store (src/interface/render/effects.cpp)

The existing fall-cue tracker (`effects_track_air_falls`, effects.cpp:956-1022) already computes everything needed — once-per-frame idempotent guard (:958-961), per-entity baseline `floor_track_store` (:976-981), departure-cell `TYPE_ZSTAIRS` probe via `world.smoother_for_floor(prev.floor).query_genre_x_y(cx,cy)` (:992-999), nudge test vs `kFallCueMaxNudgePx = 5*GRID_SIZE` (:437, :1000-1004), 2-frame staleness (:985). Extend it, leaving the FallCue/grey-smear logic byte-identical:

```cpp
enum class FloorChangeKind : Sint8 { Stairs, Fall, Teleport };
struct FloorChange { std::uint32_t tick; short from; short to; FloorChangeKind kind; };
static std::unordered_map<std::uint32_t, FloorChange> floor_change_store;
```

Inside the per-entity loop, where a fresh (`frame_tick − prev.tick <= 2`) floor diff is seen (**both directions** — today only `f < prev.floor` is examined; add the rise branch), record:

```
delta = f − prev.floor
delta == +1 && from_stair              → Stairs
delta >  0  otherwise                  → Teleport
delta == −1 && from_stair              → Stairs
delta <  0  && !from_stair && near_hole→ Fall      (any magnitude: multi-story falls)
delta <  0  otherwise                  → Teleport
```

(`from_stair` for ascents reuses the identical `prev.floor` departure-cell probe: the stair tile the climber departed from smooths to `TYPE_ZSTAIRS`, same as descents.)

New public accessor + declarations in include/openglad/interface/render/effects.h (next to `effects_track_air_falls` at :125):

```cpp
// Last observed floor change for entity_id, classified Stairs/Fall/Teleport.
// Returns false if none recorded. Records are pruned after a few frames.
bool effects_last_floor_change(std::uint32_t entity_id, FloorChange* out);
```

Prune in `effects_advance_frame` (effects.cpp:822-851, alongside `floor_track_store`): drop entries with `frame_tick − tick > 4`. Clear `floor_change_store` in `effects_reset_for_testing` (effects.cpp:1369-1381).

**Critical call-site note (found during verification):** the tracker currently runs ONLY from `draw_floor_effects` when cfg `dust` is on + camera pass + multifloor + `!editor_authoring_view_` (view.cpp:1250-1254). With dust off it never baselines, and classification would silently always be Unknown. Therefore `update_floor_glide` itself calls `effects_track_air_falls(vworld)` every frame (gated: `floor_count>1 && cfg.is_on("effects","floor_glide") && editor_floor_override_<0 && !editor_authoring_view_`). The once-per-frame guard makes the later dust-path call a no-op; `effects_advance_frame()` runs before all viewport redraws (screen.cpp:1249-1259), so frame_tick ordering is consistent. First frame after enabling the feature mid-level baselines only ⇒ that frame's change classifies Unknown ⇒ snap (acceptable, once).

---

## 5. Trigger + suppression ruleset (the ladder, in order)

`update_floor_glide(vworld, controlob)` **replaces** the `current_floor_` assignment in BOTH redraw overloads — view.cpp:473-475 (no-arg gameplay/mirror path) and view.cpp:737-739 (`redraw(LevelRuntimeData*, bool)` editor/tests/demo path). It reads the previous `current_floor_` before assigning; the single shared helper is the defense against the two-overload drift hazard. Behavior:

1. Compute `new_floor` exactly as today: `editor_floor_override_ >= 0 ? override : (controlob ? controlob->floor() : 0)`.
2. Run the suppression ladder. Every rung: **snap** (set `current_floor_ = new_floor`, `glide_frames_left_ = 0`, cause None) and **re-baseline** (update prev-id / world-key / world-tick / last-seen-frame), i.e. exactly today's behavior:
   - **S1** `!cfg.is_on("effects", "floor_glide")`
   - **S2** `vworld.floor_count() <= 1` (structural gate, same as the pre-clear)
   - **S3** `editor_floor_override_ >= 0`
   - **S4** `editor_authoring_view_`
   - **S5** `controlob == nullptr` (also zero the baseline id)
   - **S6** `controlob->entity_id() != glide_prev_control_id_` — possession/viewport swap/death-handoff/network keyframe retarget. **Compare entity id, never floor.** (`entity_id()` is `sim_entity.h:179`; mirrors receive it via EntitySnapshot, so all clients animate identically.)
   - **S7** world identity: `&vworld != glide_world_key_` **or** `vworld.tick_count_ < glide_world_tick_` (level load / restart / reset re-arm)
   - **S8** staleness: `effects_frame_tick() − glide_last_seen_frame_ > 2` (pause, menu, test reuse — the fall-tracker rule)
3. If `new_floor == previous current_floor_`: no trigger. If a glide is active, advance it (decrement; deactivate at 0 — §2.2). Assign `current_floor_ = new_floor`, refresh baseline, return.
4. Floor changed: run the tracker (§4.2 call-site note), then query `effects_last_floor_change(id)`.
   - **S10 (Unknown ⇒ snap):** record absent, `frame_tick − record.tick > 1`, `record.to != new_floor`, `record.from != previous current_floor_`, or `kind == Teleport` ⇒ snap + re-baseline. `TRACE("effects", "floor_glide snap kind=%d", ...)`.
5. Start / retarget: `from_eff = glide_active ? glide_camera_z_ : (float)prev_floor`; clamp `from_eff` to `new_floor ± kGlideSpanClamp`; `cause` = record kind; `N` per §3 using the integer span `|new_floor − prev_floor|`; set `glide_total_ = N`, `glide_frames_left_ = N−1`, compute `glide_camera_z_` for i=1 immediately (feedback on the trigger frame itself). `current_floor_ = new_floor`. `TRACE("effects", "floor_glide start cause=%d from=%d to=%d n=%d", ...)`.
6. Ghost-hold is **not** a suppression and **not** a cancel: pressing/releasing the look-up key mid-glide re-bases the above-floor alpha curve instantly (base 0↔48) — the same class of instant input response as the steady-state hold itself (which pops ghosts to 48 today). Invariant: while the hold is active, `floor_top` extends to `floor_count−1` as today and every drawn above-floor renders at alpha ≥ 48.

---

## 6. Render-loop integration (BOTH loop copies)

Apply identically to the no-arg loop (view.cpp:505-645) and the LevelRuntimeData loop (view.cpp:760-900). Three edits per loop, everything else textually untouched:

1. **floor_top** (view.cpp:505-506 / :760-761): wrap with the §2.3 `glide_top` max.
2. **Per-floor params** (replaces the `falpha` line at :509/:764 and the parallax/scale block at :516-528/:771-783):
   ```cpp
   const FloorPassParams fp = compute_floor_pass(f, vworld, ghosts_on);
   if (fp.skip) continue;                       // BEFORE any topx/topy shift
   const unsigned char falpha = fp.falpha;
   const Sint32 par_topx = topx, par_topy = topy;
   float fscale = fp.fscale;
   const Sint32 fcx = xloc + xview / 2;
   const Sint32 fcy = yloc + yview / 2;
   if (fp.shift)
   {
       topx = par_topx + static_cast<Sint32>(static_cast<float>(par_topx) * fp.pf);
       topy = par_topy + static_cast<Sint32>(static_cast<float>(par_topy) * fp.pf);
   }
   ```
   `compute_floor_pass` inactive branch returns **the existing integer math verbatim** — `falpha = floor_render_alpha(f)` (function untouched at view.cpp:1193, other callers unaffected), `fscale`/`pf` via the same expressions/clamps, `shift = floor_count>1 && f != current_floor_`, `skip=false`, `entities=true`. Active branch implements §2.1 + §2.4. The glide state is only ever *read* by this helper — a branch, not a blended formula — so OFF/idle output is identical arithmetic.
3. **Entity gate** (:616/:871): `if (fp.entities) draw_floor_entities(...same args...);`

Untouched by design: `use_layer` (:533/:788), `tile_alpha`/glass `talpha` (:534, :598-599 / :789, :853-854), pad-ring computation (:540-549/:795-804 — pads derive from `fscale<1` generically), `floor_layer_begin/end` calls and signature (video.h:138-146; the existing `scale/cx/cy/alpha/fx/pads` parameters carry everything — **no new video API**), DepthFxParams block (:622-631/:877-886 — integer `stories` from the already-snapped `current_floor_`; a departing above-floor gets no depth fx since `f < current_floor_` is false, correct), per-floor topx/topy restore (:644/:899 — also restores glide shifts, keeping the post-redraw topx/topy pins intact), shake bracket (:462-463 + :660 / :726-727 + :907), `publish_primary_render_sample` (one per redraw, unchanged), upper-floor shadows and weather calls.

**Comment fix** (Design 2 graft): rewrite view.cpp:1224-1228. Old claim "the camera floor never renders through the off-screen floor layer, so floor == current_floor_ implies alpha 255" is false mid-glide. New text: reflections/ripples/trails/dust/stair-overlays still key on `floor == current_floor_`; during a floor glide that pass renders through the floor layer and these draws are redirected into it (all `pointb`/blit paths write through `E_Screen->render`, which `floor_layer_begin` swaps — verified at video_sdl.cpp:542-563, 1288-1292), fading coherently with the floor. No behavioral change, no gating needed.

---

## 7. cfg key, defaults, menu

- **cfg key:** `effects` / `floor_glide`, boolean, **default ON**. Register `apply_setting("effects", "floor_glide", "on");` in the defaults block, src/resources/gparser.cpp:211-230 (append after `screen_shake` at :230). Note the comment at :223-226: `depth_fx` is deliberately not defaulted there — `floor_glide` has no migration concern, plain default.
- **Control shape:** plain boolean TOGGLE. Durations/easings are cause-derived, not player taste parameters — a cycle would double menu cost for nothing (per all designs and the brief).
- **Menu:** GRAPHICS FX subscreen, `k_graphics_fx_options_buttons` (src/interface/ui/picker.cpp:1128-1143; currently BACK + 12 toggles, rows via `effects_row_y(row) = 35 + row*23`, picker.cpp:1103). New button index 13:
  ```cpp
  button("toggle_floor_glide", "Floor glide", KEYSTATE_UNKNOWN, 15, effects_row_y(4) /*=127*/, 90, 15,
         button_action_id(ButtonAction::ToggleFloorGlide), -1, MenuNav{.up=10, .down=0}),
  ```
  Label "Floor glide" = 11 chars, fits the 90 px face at 6 px/char; sentence case matches the sibling toggles ("Screen shake"). Nav edits: `graphics_fx_back`(.up 10→13), `toggle_fire_glow`(10, .down 0→13), `toggle_ripples`(11, .down 0→13), `toggle_screen_shake`(12, .down 0→13). Single-button-row precedent: `generator_rate` at picker.cpp:1365.
- **ButtonAction:** append `ToggleFloorGlide = 94,` to the enum (include/openglad/interface/button.h:204-316; 93 = `CycleWorldScale` is the current max — verified; respect the retired-value comments, do not reuse 73/78/80). Handler: toggle the cfg key + label state refresh, cloned from `ToggleScreenShake` (84).
- **Mechanics:** follow the **openglad-menus skill** (the known 8-edit recipe) for the full chain — enum, button table, action switch, nav, defaults, `test_menu_layout` pins. SDL client only; the text/curses pickers render no floors and get no entry.

---

## 8. Ordered implementation plan

Work on `feature/z-axis-multifloor`. Build/test loop: `cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset ci-test`.

1. **effects.cpp/.h classification** (§4.2): `FloorChangeKind`/`FloorChange`/store; bidirectional recording inside `effects_track_air_falls` (effects.cpp:956-1022); accessor in effects.h (~:125); prune in `effects_advance_frame` (:822-851); clear in `effects_reset_for_testing` (:1369). Unit-testable immediately via existing headless effect tests.
2. **view.h state + hooks** (§4.1) at view.h:186; constants in view.cpp (§2.1).
3. **`update_floor_glide`** (§5) in view.cpp (place above `viewscreen::redraw`); replace the assignment at **BOTH** view.cpp:473-475 and view.cpp:737-739 with the call. This is the drift hazard — grep afterward: `grep -n "current_floor_ = (editor_floor_override_" src/interface/render/view.cpp` must return zero hits.
4. **`compute_floor_pass`** (§2.1/§6) with the inactive-branch-verbatim rule.
5. **Loop edits ×2** (§6): floor_top, params call + skip, entity gate — in the :505-645 loop, then identically in the :760-900 loop.
6. **Comment fix** at view.cpp:1224-1228 (§6).
7. **cfg default** (gparser.cpp:230) + **menu chain** (§7, via the openglad-menus skill).
8. **Test-guard updates** (§10.1) — do these before writing new tests, or the suite goes red: default-ON glide fires in every existing test that hops floors.
9. **New tests** (§10.2) + **capture scene & site card** (§10.3).
10. Full gates: `ctest --preset ci-test` (+ 30-seed `--gtest_shuffle` sweep on the touched binaries), `ci-asan`, `ci-coverage` (judge by local baseline→change delta per the known undercount), `og_test_parity` (render-blind — must stay 187/187 trivially), `scripts/check_vendor_leaks.sh`.
11. Generate the fx-review site, review the scene (explicitly judge the departing-floor entity vanish and the fall squash strength), apply §12 ladder if needed.

Estimated ~700–780 LOC: view.h ~35; view.cpp ~190; effects.h ~15; effects.cpp ~55; gparser.cpp 1; button.h 2; picker.cpp + handler ~25; tests ~350; capture scene + site card ~80.

---

## 9. Networking, clients, parity

- **Render-only.** No wire change; `kNetworkProtocolVersion` untouched (bumping it breaks 5 literal wire-byte tests — do not touch).
- **Mirrors:** the trigger reads `controlob->floor()`, which mirror walkers carry via EntitySnapshot; per-viewport state + S6/S7/S8 handle keyframe resets by snapping. All clients animate identically because the classifier and easing are deterministic functions of rendered-frame state.
- **openglad_text / openglad_curses / dedicated server:** SDL-free, render no floors — zero edits.
- **og_test_parity:** both sides headless ⇒ blind to this feature; the capture scene + endpoint tests are the real regression net (state this in the PR).

---

## 10. Test plan

### 10.1 Existing pinned tests that MUST be updated (default-ON means they fail otherwise)

| File / anchor | Edit |
|---|---|
| tests/test_render_effects.cpp `all_effects_off` (:2177) | add `"floor_glide"` to the key list |
| tests/test_render_effects.cpp `EffectsCfgGuard::kKeys` (~:72-77) | add `{"floor_glide", "on"}` (save/restore with correct default) |
| tests/test_stair_overlay.cpp `QuietEffectsGuard::kKeys` (:167-190) | add `"floor_glide"` — these tests hop floors and would otherwise render glide frames |
| tests/test_game_loop.cpp `all_capture_effects_on` (:2629; grep for the second `gameplay_rec::` variant near :2746) | add `"floor_glide"`, `"on"` |
| tests/test_menu_layout.cpp `graphics_fx_options_grid_geometry_and_nav` (:818-837) | kExpected gains `{"toggle_floor_glide", "Floor glide", 15, 127}`; count 13 → 14; nav pins per §7 |
| Audit sweep | `grep -rn "set_floor\|editor_floor_override_" tests/` — any test that changes the control walker's floor and then compares pixels must either sit under an updated guard or settle ≥16 redraws. Tests driving `editor_floor_override_` are suppressed by S3 already (cloud/depth-fx capture scenes are safe). |
| tests/test_view_redraw.cpp :404-407 | no change needed (glide never writes topx/topy outside the per-floor restore) — but run it explicitly |
| Mutation canary | **Verified 2026-07-12: the pin map (tests/parity/scenario_table.h) contains NO pins in view.cpp, effects.cpp, gparser.cpp, or picker.cpp** — all pins live in src/gameplay + gloader/save_data/input_state. Re-verify the pin list at impl time; if any have appeared, insert below the max pin per file. |

### 10.2 New tests (og_test_rendering group unless noted; table-driven where possible — the coverage gate sits at ~90.00 % and the suppression ladder must not sink it)

1. **OFF byte-identity pair** (headline; EffectsCfgGuard pattern): 2-floor world, control walker crosses a Z-stair; cfg `floor_glide` off. (a) off-vs-off: two identical runs, `grab_viewport`/`rects_equal` on every frame across the floor change; (b) off-vs-on: ON run differs from OFF on at least one mid-glide frame, and `floor_glide_frames_left()==0` throughout the OFF run. Settle the camera one redraw before any `world_to_screen` use (known trap).
2. **Single-floor structural gate:** `floor_count()==1`, cfg ON: byte-identical to cfg OFF across an (attempted) trigger; frames_left stays 0.
3. **Endpoint exactness (the no-pop pin):** ON, stair-up; render exactly N frames; grab frame N; separately snap-construct the identical steady state (fresh redraws, settled); `rects_equal`. Do **not** pin the penultimate frame byte-wise (≤1-quantum residual by design).
4. **Cause/duration matrix via introspection** (`floor_glide_frames_left`, `floor_glide_cause`): stair up ⇒ left==15 on trigger frame, cause Stairs; stair down ⇒ same; fall Δ=1 ⇒ 8; Δ=2 ⇒ 11; Δ=3+ ⇒ 13; teleport (XY beyond nudge) ⇒ 0; rise Δ=1 without stair ⇒ 0; rise Δ=2 ⇒ 0.
5. **Suppression matrix, table-driven:** {cfg off, floor_count 1, editor override, authoring view, control null, control-id swap, world-key swap, world-tick reset, staleness>2, classification-absent} ⇒ frames_left 0 and `current_floor_` snapped.
6. **Retarget continuity:** start stair glide; at ~frame 5 trigger a fall; assert cause becomes Fall, frames reset to the fall N, and `floor_glide_camera_z()` moves continuously (|z(k+1) − z(k)| bounded by the max single-frame step; never jumps to an integer).
7. **Ghost-hold invariant:** hold active during an up-glide ⇒ every drawn above-floor alpha ≥ 48 (pixel-probe a distinctive tile through the composite, or expose `compute_floor_pass` under `#ifdef TESTING`); at glide end under hold, frame N equals a settled steady hold frame (byte compare).
8. **Terrain-only departing pass:** down-glide with a living monster on the departed floor, hold off: mid-glide, pixel-probe the monster's screen position — terrain fade only, no sprite pixels; `trace_contains("effects","floor_glide")` for the start record. Companion **rng-invariance** assertion: with an INVISIBLE-mode entity on the DEPARTING floor, both the cfg-off run and every glide frame consume exactly zero ctx rng draws (the vacated floor is entity-free either way — zero new entity passes, provable). Do **not** pin cursor equality with the probe on the destination floor: mid-glide that floor renders at `falpha < 255`, its entities take the faded-floor layer path, and the invisible fill's rng draws are skipped — the accepted exposure recorded in §2.4 / R2.
9. **Multi-story fall sweep:** Δ=2, assert `floor_glide_camera_z()` passes through the intermediate floor's value (floors rushing past, one motion) and total frames == 12.
10. **topx/topy + render-sample pins:** after any glide frame, `vs->topx/topy` equal the unshaken/unshifted camera and exactly one `publish_primary_render_sample` fired (extends the existing test_view_redraw pins).
11. **Menu:** layout/nav pins per §10.1; toggle flips the cfg key (clone the screen_shake toggle test).
12. **Shuffle robustness:** run the new suites with `--gtest_shuffle` over 30 seeds (known flake-hunting pattern); ensure staleness re-baseline (S8) makes tests order-independent.

### 10.3 fx-review capture scene (ships with the feature — the user reviews effects there)

- New scene in `zz_capture_effect_scenes` (tests/test_render_effects.cpp:3954, env-gated PPM dump): 3-floor arena; hero walks up a Z-stair (~24 frames captured), pauses, walks back down (~24), then falls through a hole two stories (~20); **place pacing monsters on the departure floor for the down-stair leg** so the frame-1 entity vanish is visible; include a short look-up-hold segment during one glide.
- New card in `scripts/fx_review/make_site.py` `SCENES` (:197, insert before `all_together`): id `floor_glide`, title "Floor glide — stairs vs falls", body describing the three reads **and explicitly asking the reviewer to judge (a) the departing-floor entity vanish on the down-stair and (b) whether the fall squash reads in a quarter-size pane** — these gate §12 items F2/F3.

---

## 11. Risk register

| # | Risk | Mitigation |
|---|---|---|
| R1 | Two-overload drift (view.cpp:411 vs :678 twins) — a missed wiring silently desyncs editor/demo/capture | Single `update_floor_glide` + single `compute_floor_pass` used by both; grep check in §8 step 3; per-overload endpoint test (one via `screen::redraw`, one via `redraw(data,…)`) |
| R2 | rng-during-render perturbation | **Zero NEW rng draws by design** (terrain-only departing pass; up-glide floor set == steady set); pinned by test 10.2-8. Not strict ON-vs-OFF stream identity: invisible/phantom entities on the destination floor transiently skip their per-pixel rng draws mid-glide via the pre-existing faded-floor layer path — accepted exposure, scoped in §2.4 |
| R3 | Existing pinned tests go red under default-ON | §10.1 enumerated guard updates land before the feature default flips on; audit sweep for floor-hopping tests |
| R4 | Coverage gate (~90.00 % razor-thin) | Table-driven suppression matrix + frame-step loops; judge by local baseline→delta, wipe stale `.gcda` before re-running |
| R5 | `draw_floor_effects` invariant comment now false; glass `talpha` and camera-pass effects transient through the layer | Comment rewritten (§6); pointb redirect **verified** (video_sdl.cpp:1291) so effects fade coherently with the layer — no gate needed; ≤16-frame cosmetic transients documented |
| R6 | Avatar semi-transparent early in stairs-up | Accepted: crosses 50 % by ~frame 3 (~55 ms); departure floor fully readable beneath; inherent to the crossfade grammar all three dolly designs share (judge-reviewed) |
| R7 | Destination floor renders through the bilinear layer mid-glide (softening) | Accepted, ≤N−1 frames; rounding-collapse returns it to direct draw before the end (§2.1) |
| R8 | Down-glide double representation (departing floor fades over its own new shadows) | Deliberate — removing shadows would pop at the end; fx-review confirms |
| R9 | Depth-fx `stories` (int) steps at frame 0 while alpha/scale glide | Masked by peak motion; Follow-up F1 (float `strength`, default 1.0 bit-identical) |
| R10 | Fall squash sub-threshold at quarter-pane | Boosted parameters (0.25 overshoot, 0.25 slope ⇒ ~10 px full-pane / ~3 px quarter-pane edge kick + 25 % dim); fx-review card asks explicitly; F2 landing shake pre-specified |
| R11 | Frame-1 entity vanish on the departed floor | fx-review card asks explicitly; §12 F3 fallback ladder, every rung shippable |
| R12 | Mutation-canary pins | Verified: no pins in any edited file (pin map checked); re-verify at impl |
| R13 | Wall-clock flake on the shared dev machine | No wall clock anywhere: per-viewport redraw-counted frames + `effects_frame_tick` staleness only |
| R14 | sdl2-compat bilinear at scale >1.10 (falls reach 1.25+) | Same composite code path as the shipped 1.10 ghost loom; capture scene covers it; no alloc-size change (layer sizing unchanged) |
| R15 | 4-player cost | ≤1 extra terrain-only pass per transitioning viewport for single-story glides; a multi-story fall keeps up to `ceil(z) − current_floor_` ≤ 3 departed floors in the loop (span clamp) — still terrain-only, ≤16 frames, independent panes, bounded by the shipped look-up hold which renders EVERY floor with entities |
| R16 | Radar/shadows/clouds snap while the world glides | Deliberate and stated: instruments show instant truth; recorded as a decision, not an oversight |

---

## 12. Pre-approved follow-ups (specified now, gated on fx-review)

- **F1 — DepthFxParams float `strength`** (from Design 3): add `float strength = 1.0f` to `DepthFxParams`; transition frames pass the eased value so mist/fog ramps instead of stepping; default 1.0 keeps every existing call, golden, and capture bit-identical. Independent PR.
- **F2 — Fall landing shake** (from Design 1; ship only if fx-review says the squash under-reads): falls only, 4 frames, amplitude `2px * min(|Δ|, 3)`, deterministic hash-of-frame jitter, injected as an extra impulse inside `apply_screen_shake` (view.cpp:284-325) so it lives inside the existing save/restore bracket (restored before radar + publish — topx/topy pins stay safe); compose additively with detonation shake, clamp total to ±3 px; explicit restore assertion in its test. Caution recorded from judge 3: shake currently means "explosion nearby" — keep the amplitude at 2 px so the meanings stay distinguishable.
- **F3 — Departing-pass entity fade fallback ladder** (decided at fx-review, in order): (1) ship terrain-only as specced; (2) if the frame-1 vanish reads badly, draw the departing floor's entities fading for the **first 4 frames only** (bounded rng exposure); (3) only if that still reads badly, full-duration entity pass (ghost-hold-class rng exposure, the accepted precedent). Either answer is shippable; the capture site exists to answer exactly this.

## 13. Explicitly rejected (record in the PR description)

Vertical slide channel (screen-vertical collides with the game's only meaning for vertical motion — N/S travel — and displaces the aim reference mid-combat; scale/alpha already encode direction); pixel capture of the old floor (frozen entities, HUD contamination when the world and UI share a surface, world-canvas resize burden); teleport materialize fade (dims arrival at maximum disorientation; snap is the message); any use of `FadeBetween`/`fadeblack` (blocking, whole-canvas, TESTING-short-circuited); a multi-mode cycle control (nothing player-meaningful to cycle); wall-clock timing (flake source); protocol changes (render-only feature).
