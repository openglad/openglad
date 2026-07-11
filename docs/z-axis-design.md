# Z-Axis / Multi-Floor Design

Status: **in progress** (branch `feature/z-axis-multifloor`, started 2026-06-28).
This document is the source of truth for the Z-axis feature. The phase checklist
at the bottom tracks progress.

## Goal

Add a Z (height) axis and stacked floors to OpenGlad:

- Scenarios can have multiple stacked **floors**.
- New tiles: **air** (see + fall through to the floor below), **glass** (walkable
  but transparent), **directional drop-block** edges (walkable, block falling
  toward one edge), and **Z-stairs/ladders** that move an entity between floors
  (distinct from the existing flat "stair" decoration tiles).
- Upper floors render with increasing transparency.
- Projectiles/specials get a vertical velocity + gravity (knives/fireballs drift
  up/down, elf rocks arc down) and can drop through air to a lower floor.
- Characters have real height (a Z-elongated cylinder over their 2D hitbox).
- Pathfinding keeps working: non-flyers route around air holes; flyers may fly
  over air on their own floor but **cannot** change floors.

## User decisions (2026-06-28)

1. **Air = fall-through.** A ground unit that moves onto an air tile falls to the
   floor below. AI **pathing** still treats air as blocked (enemies route around
   pits and change floors only via stairs); players and knockback can drop through.
2. **Projectiles drop floors** — per-weapon `can_drop_floors` flag (true for
   thrown/rock families, false for orbit FX like boomerang/shield). Deterministic
   `vz`/gravity, **no new RNG draw**.
3. **Rendering:** camera floor fully opaque; floors below fade with depth; floors
   above near-invisible ghost (still drawn). Exposed as an options toggle.
4. **Cross-floor AI = yes.** Ground enemies may chase foes onto other floors via
   Z-stairs. Consequence: there is **no same-floor foe filter** — foe acquisition
   stays floor-agnostic (byte-identical to today on single-floor levels); the
   cross-floor A\* stair-edges plus per-floor line-of-sight do the work.

## Core invariant (the thing that keeps the build green)

The world is **always internally multi-floor with a default of exactly one floor**.
Every Z behavior is gated behind a single cheap check:

```cpp
if (world->floor_count() > 1) { /* Z behavior */ }
```

plus per-entity fast paths (`floor()==0`, `worldz()==0`, `vz()==0`,
`sizez()==0` "full height" sentinel). When a level has one floor, every code path
must execute byte-for-byte the same statements as today. This protects:

- the ~180 byte-exact **parity goldens** (`og_test_parity`),
- the byte-identical **A\*** solver (`astar.cpp` is never touched),
- the **wire protocol** / replay / snapshot determinism.

**Determinism rules (do not violate):**
- Never add an `rng_.next()` on the move / fire / foe-acquisition path for the
  single-floor case. Z RNG draws (if ever needed) gate on `floor_count>1` or use
  deterministic constants / the cosmetic RNG slot.
- Never reorder the 8-neighbor loop in `PathingMap::adjacent_cost` (the count and
  order of `query_grid_passable` calls is part of the determinism contract via the
  arrow-wall RNG). Append vertical edges strictly **after** the 8 horizontals.
- Never re-encode existing v2–9 scenario files to v10. Save v9 when
  `floor_count==1`; v10 only when a level genuinely has >1 floor.

Verify with `og_test_parity` (run-twice byte identity) after **every** phase.

## Data model

### `og::sim::SimEntity` (include/openglad/gameplay/sim_entity.h)
Add next to `worldx_value_`/`worldy_value_`:
- `float worldz_value_ = 0.0f;`  — authoritative sub-floor height
- `short floor_ = 0;`            — which floor the entity is on
- `short sizez_ = 0;`            — cylinder height; **0 = full/unbounded** sentinel

Getters/setters `worldz()/set_worldz`, `floor()/set_floor`, `sizez()/set_sizez`
each `mark_dirty(...)`.

### `walker` (include/openglad/gameplay/walker.h)
Add `float vz_` via `OG_WALKER_DIRTY_FIELD` (marks `BIT_VZ`). Only projectiles use
it.

### Dirty bits (include/openglad/gameplay/dirty_field_bits.h)
Append (word 1, 38 bits free):
```
BIT_WORLDZ = 86, BIT_VZ = 87, BIT_SIZEZ = 88, BIT_FLOOR = 89
FIELD_COUNT 86 -> 90   (static_assert -> BIT_FLOOR + 1 == FIELD_COUNT)
```

### `GameWorld` (include/openglad/gameplay/game_world.h)
Replace `PixieData grid; smoother mysmoother; std::unique_ptr<obmap> myobmap;`
with:
```cpp
struct Floor { PixieData grid; smoother smoother_; std::unique_ptr<obmap> obmap; };
std::vector<Floor> floors_;   // size 1 by default
```
Plus accessors `grid_for_floor(int)`, `smoother_for_floor(int)`,
`obmap_for_floor(int)`, `floor_count()`. `floors_[0]` is constructed identically
to today's single instances (byte-identical spatial-hash traversal + RNG).
`pixmaxx`/`pixmaxy` stay shared (all floors share footprint). `grid_dirty_tiles_`
gains a floor component.

Passability/`damage_tile` get an explicit-floor overload; the 3-arg forms forward
`ob->floor()`:
```cpp
bool query_grid_passable(float x, float y, walker* ob);                 // uses ob->floor()
bool query_grid_passable(float x, float y, walker* ob, int floor);      // explicit
```

### Tiles & genres
- pixdefs.h: append `PIX_AIR, PIX_GLASS, PIX_DROPBLOCK_UP/RIGHT/DOWN/LEFT,
  PIX_ZSTAIR_UP, PIX_ZSTAIR_DOWN` starting at the current `PIX_MAX` (134); raise
  `PIX_MAX`.
- terrain_types.h: append `TYPE_AIR=11, TYPE_GLASS=12, TYPE_DROP_BLOCK=13,
  TYPE_ZSTAIRS=14`.
- smooth.cpp `PIX_to_genre[]`: map the new tiles, but **omit them from
  `smoother::smooth()`'s genre switch** so they route to the unchanged default
  (inert, no autotiling — precedent: `PIX_CLIFF_*`).

### Pathfinding state (include/openglad/gameplay/pathfinding_grid.h)
```
MAP_HEIGHT = 256
FLOOR_STRIDE = MAP_WIDTH * MAP_HEIGHT   (= 102400)
state = floor*FLOOR_STRIDE + (y/16)*MAP_WIDTH + (x/16)
GET_STATE_FLOOR(s) = s / FLOOR_STRIDE
GET_STATE_X/Y mask the floor via (s % FLOOR_STRIDE)
```
Max single-floor index 254*400+254 = 101854 < FLOOR_STRIDE, so floor 0 yields the
identical numeric value and the mask is a no-op → A\* node expansion byte-identical.
`astar.cpp` is **not** modified.

## Passability / movement / falls

- **Grid layer** (`query_grid_passable`): select `floors_[floor].grid`; add new
  switch arms **after** all existing arms (no new RNG):
  - `PIX_AIR` → `flyer ? true : (Order::Weapon ? true : true_for_movement)`.
    Air is walkable for movement (then you fall); pathing blocks it separately.
  - `PIX_GLASS` → walkable (like floor).
  - `PIX_DROPBLOCK_*` → horizontally walkable (the directional block affects
    falling, not horizontal passability).
  - `PIX_ZSTAIR_*` → walkable (floor change triggered after the move).
- **Object layer** (`query_object_passable` → obmap `ob_pass_check`): early
  `different floor → continue`; then a cylinder z-overlap guard
  `[worldz, worldz+sizez]` before the 2D `collide()` — with `sizez==0` = full
  height so two floor-0 legacy entities always overlap exactly as today.
- **Vertical update** `apply_vertical()` (called from weap/effect/living act,
  strictly skipped when `vz==0` on a solid tile): `worldz += vz; vz += gravity;`
  when an entity is over an air tile with no support it decrements `floor` and
  lands on the first solid tile below; falling past floor 0 = pit death.
- **Z-stairs**: after a successful `walk()`, if the destination tile is a Z-stair
  and `floor_count>1`, move the entity to the linked floor/cell.

## Pathfinding (cross-floor)

All additive, double-gated (`floor_count>1` and tile data):
- `adjacent_cost`: decode floor from `state`; generate the 8 horizontals on that
  floor via the floor-aware `query_grid_passable`, keeping loop order/cost math
  EXACTLY. For non-flyers, air neighbors are skipped (route around pits). Only when
  `floor_count>1` and the current cell is a Z-stair, append **one** vertical
  neighbor on the linked floor, strictly after the 8. Flyers get no vertical
  neighbor (cannot change floors).
- `least_cost_estimate`: pure 2D distance when start/end share a floor (always true
  single-floor); add an admissible floor-transition term only when floors differ.
- `follow_path_to_foe`: when the next node's floor differs, invoke the
  floor-transition mover; else the existing walkstep logic runs verbatim.

## Projectiles

- `WeaponFamilyDescriptor` + gloader `EntityDef` table gain
  `init_vz / gravity / init_sizez / can_drop_floors` (defaults 0/0/0/false applied
  like `stepsize`), so initial weapon state + goldens do not move.
- `walker::act_fire` integrates `worldz/vz` after `walk()`, strictly gated on
  `vz!=0 || floor>0 || destination genre is air/glass`. Drop-through-air decrements
  floor; `can_drop_floors` controls whether a projectile may leave its floor.
- Tuning: thrown knives small up/down drift; fireballs slight; elf rock negative
  `init_vz` (arc up then fall). Rock bounce on death becomes floor/z aware.
- Boomerang/magic-shield are orbit-table driven: set `worldz` from `drawcycle`,
  inherit owner floor via `center_on`; `can_drop_floors=false`.

## AI / targeting / LOS

- **No same-floor foe filter** (decision 4). Foe acquisition
  (`find_near_foe`/`find_far_foe`/`find_*_in_range`/`find_nearest_player`) stays
  floor-agnostic → byte-identical to today on single-floor.
- `fire_check` raymarch + LOS query the shooter's floor grid (cannot shoot through
  solid floors; air/glass let sight/fire pass downward where appropriate).
- `distance_to_ob`/`distance_to_ob_center` stay **pure 2D** (a z term would
  reorder `find_far_foe` selection and break parity).
- Cross-floor chase emerges from the A\* stair-edges: an enemy acquires a foe on
  another floor, can't shoot it (different floor), paths to a stair, climbs, engages.
  **Subtlety (fixed):** `statistics::walk_to_foe` gates the expensive A* behind a
  *2D* proximity short-circuit (`distance_to_ob`, which has no floor term). A foe
  directly above/below reads as "close", so the AI skipped A* and fell back to
  `direct_walk` — running *under* the target on its own floor. Fix: a `cross_floor`
  flag (`floor_count()>1 && foe->floor()!=floor()`) forces `find_path_to_foe` and
  never abandons the chase on 2D proximity. And `follow_path_to_foe` nudges the
  walker's *center* (not corner) onto the Z-stair cell, since `apply_z_motion`
  probes the center — else a sized walker deadlocks at the stair edge. Both gated
  `floor_count()>1`; single-floor is byte-identical (`og_test_parity`). Regression:
  `ZAxis.cross_floor_ai_chases_foe_through_stair`.

## Rendering (outside the parity gate; read-only Z)

- Collapse the two near-duplicate `viewscreen::redraw` bodies into one first.
- `viewscreen` gets a read-only `current_floor` from the control walker.
- Background: loop floors bottom-up to camera floor; camera floor opaque; floors
  above near-invisible — each tile drawn with a per-floor alpha.
- `draw_obs`: floor-parameterized passes interleaved with tile passes; filter
  entities by `floor()`.
- `walker_draw.cpp`: subtract `z` from `yscreen` at the four draw sites
  (`- static_cast<Sint32>(z)`), gated so `z==0` reproduces current output.
- New video primitive: a full-color, team-recolored `walkputbuffer_alpha` (clone
  `walkputbuffer` clip/recolor loop but blend with alpha). Tiles reuse
  `putbuffer_alpha`; accel path uses `SDL_SetSurfaceAlphaMod` on the cached surface.
- Curses renderer + radar made floor-aware in the same changeset (new air/glass
  glyphs; restrict to active floor).
- All gated `floor_count>1`. Defer the optional per-floor y-sort.

## Level format v10 (gated)

- Bump `kScenarioVersion` 9 → 10 (src/resources/level_file_io.cpp).
- `LevelFileMetadata.grid_file` `std::string` → `std::vector<std::string>`
  (one PNG per floor, convention `pix/scenNNNN_fK.png`).
- v10 reads: a `floor_count` byte, then per-floor grid name, then per-object
  `floor`/`z` (appended fields, default 0), then a trailing **Z-stair link table**
  (`(srcFloor,x,y) -> (dstFloor,x,y)`).
- v2–9 read path **untouched** → floors_ resized to 1, `floors_[0].grid` identical
  bytes. `save_level` writes v9 when `floor_count==1`, v10 only when >1.

## Snapshot / replay / wire

- `EntitySnapshot` += `float worldz; float vz; std::int16_t sizez;
  std::uint8_t floor;`; add 4 rows to the `kEntitySnapshotFields[]` table.
- `GridTileSnapshot` += `std::uint8_t floor`; `WorldSnapshot` += `floor_count`,
  full grid data spans `floor_count*w*h` (special-case `floor_count==1` =
  legacy single-grid shape).
- Bump `kSnapshotFormatVersion` 5→6, `kNetworkProtocolVersion` 3→4,
  `kReplayFormatVersion` 6→7 in ONE atomic commit. Re-pin the ~5 literal
  wire-byte tests. Clamp incoming `floor` to `[0, floor_count)` in `apply_entity`.

## Parity strategy

- Parity dump (`state_dump.cpp`) is independent of `EntitySnapshot`. Add
  `floor/z/sizez` to the four dump structs with defaults and emit them in
  `canonical_serialize` **only when non-default** → single-floor dumps stay
  byte-identical, no golden regenerates. `validate_schema.py` treats them as
  optional; `fact_predicate.cpp` handlers tolerate them.
- Z parity scenarios (added later, optional) must be `CompareMode::Invariant` +
  `is_branch_internal=true` (no master companion can model Z); each needs a real
  discriminating mutation + teethed FactPredicate.

## Concept Playground campaign (lands last)

- `builtin/org.openglad.concept.glad`, campaign id `org.openglad.concept`, level
  ids **600–604** (the 600 range is free).
- New tool `tools/concept_mapgen/{main.cpp,grid_painters.cpp}` mirroring
  `tools/ctf_mapgen` (SDL-free, EXCLUDE_FROM_ALL; links og_core/og_gameplay/
  og_resources). Writes the campaign yaml + icon + per-level v10 .fss + per-floor
  PNGs, zips to `campaigns/`, mounts, **self-checks** each level (reload; assert
  floor_count; assert an air tile is impassable to a ground probe's *path* but a
  flyer can fly over; assert a stair link round-trips), copies to `builtin/`.
- `scripts/generate_concept_campaign.sh` (copy of the CTF one).
- Five levels, one concept each: **600 Stairs** (two floors joined by a Z-stair),
  **601 Mind the Gap** (air-hole grid: non-flyer paths around, flyer flies over),
  **602 Glasshouse** (glass floor, see + fight a floor below), **603 Drop Zone**
  (directional drop-block edges + an air pit you fall through), **604 Arc Range**
  (open arena showing knife/fireball drift + elf-rock arc over a pit).
- Linked with `FAMILY_EXIT` treasures so `picker_accessible_levels` unlocks them in
  order. Campaign picker needs no change (metadata-driven discovery, as CTF=500
  proves). **Not** added to `tests/parity/scenario_table.h`.
- DONE since: six epic multifloor war stories were authored here as levels
  605-610, then MOVED to the "War of the Westlands" story campaign
  (`builtin/org.openglad.westlands.glad`, generated by the multi-file
  `tools/westlands_mapgen` + `scripts/generate_westlands_campaign.sh`),
  renumbered into its level graph: 605 The Deeping Wall → 15, 606 The Wizard's
  Vale → 14, 607 The Bridge of Shadow → 8, 608 Under the Mountain → 6, 609 The
  Black Gate → 17, 610 The Frozen Wall → 7 (exits repointed, story briefings
  and cast per the campaign design). The concept package keeps the five demos,
  with 604's exit looping home to 600. Both tools' self-checks audit per-team
  army counts, aligned stair pairs on every floor boundary, and entity footing;
  westlands_mapgen additionally validates every exit destination against the
  registered id set (warn-only for planned-but-unbuilt levels until the
  campaign is complete). `tests/unit/test_concept_levels.cpp` (og_unit_data)
  pins the demo package; a westlands test file follows with the full campaign.

## Phase checklist

- [x] **P0** Baseline: clean tree is fully GREEN via ctest (the "2 scen99 parity
  failures" were a cwd artifact of running the binary from repo root; cfg-clobber
  under parallel load is the other gotcha — always run via ctest + `git checkout
  cfg/`). NOTE: editing a public header requires `cmake --preset ci-test`
  (reconfigure) to refresh the `build/ci-test/component_includes/` copy, else
  builds compile against a STALE header and falsely succeed.
- [x] **P1** Entity Z data model + dirty bits + snapshot/replay/wire bump (+re-pin).
  worldz/floor/sizez on SimEntity, vz on walker; BIT_* 86-89; EntitySnapshot +4
  fields/table rows; kSnapshotFormatVersion 6, kNetworkProtocolVersion 4,
  kReplayFormatVersion 7; wire-byte tests re-pinned. Verified green.
- [x] **P2** Per-floor `GameWorld` containers (default 1, no new tiles). ExtraFloor
  vector + floor_count()/grid_for_floor()/smoother_for_floor()/obmap_for_floor()
  (out-of-range → floor 0, doubles as clamp) + set_floor_count(); query_* split
  into 4-arg(floor)+3-arg forwarders. Floor 0 == legacy members (byte-identical).
- [x] **P3** Level format v10 (gated; v2–9 untouched). kScenarioVersion 10; per-object
  floor/z packed into reserved/filler bytes; trailing floor_count byte; extra floor
  grids by derived name "{grid}_f{N}.png". save writes v9 when single-floor
  (byte-identical). **No stair-link table** — Z-stairs are positional (vertically
  aligned cells, UP→+1/DOWN→-1). Verified green.
- [x] **P4** New tiles/genres/passability. PIX_AIR..PIX_ZSTAIR_DOWN (134-141, PIX_MAX
  142, both core+legacy pixdefs); TYPE_AIR..TYPE_ZSTAIRS (inert in autotiler);
  all new tiles `break` (walkable) in query_grid_passable. Verified green.
- [x] **P5** Cylinder collision + falls + Z-stairs. SINGLE floor-keyed obmap (bucket
  numx += floor*256, floor 0 identical; all ~15 myobmap sites unchanged); cylinder
  z-overlap in ob_pass_check (sizez==0 = full-height sentinel); walker::change_floor
  + apply_z_motion (fall-through-air → floor below / pit death; Z-stairs up/down;
  flyers never z-move; z_cooldown_ server-only transient) wired into living::act,
  gated floor_count>1.
- [x] **P6** Pathfinding floor encoding + cross-floor stair edges. FLOOR_STRIDE in
  pathfinding_grid.h (floor 0 numerically identical → A* byte-identical); MAKE_STATE
  gains floor (both copies: walker_pathing.cpp + gameplay_context.cpp); adjacent_cost
  decodes floor, avoids air for non-flyers, appends positional Z-stair edge (UP/DOWN);
  least_cost_estimate floor term; follow_path_to_foe waits on the stair cell for
  apply_z_motion. All gated floor_count>1.

> **Verification protocol note:** `NetTransportWebSocketServer...at_12hz` (in og_unit_sim)
> is a documented wall-clock flake under machine contention (90s budget; passes in
> ~120ms isolated). When the full `-j` suite reports ONLY og_unit_sim failing, re-run
> that websocket test isolated; 3/3 isolated passes == environmental flake, not a regression.
- [x] **P7** Projectile vz/gravity + specials. WeaponFamilyDescriptor +
  init_vz/gravity/init_sizez/can_drop_floors (defaults 0/false); gloader applies them;
  act_fire integrates worldz/vz/gravity + drops through air (gated vz!=0||worldz!=0||
  multifloor); knife (0.35/0.05) + rock (0.7/0.09) arc + can_drop_floors. create_weapon:
  projectiles/summons inherit the shooter's floor. **worldz is invisible to the parity
  dump** (confirmed: knife/rock arcs keep parity green).
- [x] **P8** AI floor-aware fire — achieved with NO extra code: foe acquisition stays
  floor-agnostic (decision 4); fire_check raymarches the floor-inheriting weapon through
  the floor-keyed obmap → naturally can't hit cross-floor foes → AI paths via stairs (P6)
  then engages on arrival. distance_to_ob stays pure-2D (parity).
- [x] **P9** Rendering. viewscreen::current_floor_ from the control walker; both
  redraw bodies draw floors 0..current_floor bottom-up (upper-floor air = empty
  graphics → reveals the floor below, no alpha needed); upper floors skip the
  out-of-bounds wall border; draw_obs layers entities the same way (filtered by
  floor); walker_draw subtracts worldz from yscreen (projectile arcs visible).
  Single-floor (current_floor 0) renders byte-identical (og_test_view green).
  Curses/radar floor-awareness deferred (SDL view is the showcase).
- [x] **P9 (alpha)** Upper-floor ghosting + lower-floor fade. New `walkputbuffer_alpha`
  primitive (video/screen/sdl_video) + alpha-capable `draw_tile`/`pixie::draw`; both
  redraw bodies draw floors 0..N-1 with per-floor opacity (camera opaque, below faded,
  above ghost ~48α) and interleaved entities; `draw_walker` alpha path. Gated
  floor_count>1 (single-floor byte-identical). Default-on; cfg toggle is a follow-up.
- [x] **P9 (curses/radar)** Curses renders the followed walker's floor (terrain +
  filtered entities) with air/glass/stair glyphs; radar blips filtered to the control
  floor. Single-floor unchanged.
- [x] **P10** Level editor multi-floor authoring. `current_floor` in LevelEditorState;
  terrain paint/get/clear/smooth routed through `grid_for_floor(current_floor)`;
  PageUp/PageDown switch floors, Ctrl+PageUp adds one (`add_floor`); new Z tiles in the
  brush palette; objects placed/selected on the current floor (`some_hit` floor filter);
  floor indicator in the panel; viewscreen `editor_floor_override_` so the editor (no
  control walker) renders the edited floor. Keyboard-only (no new buttons → editor
  interaction hit-regions unchanged). Coverage exercised via the in-source TESTING helper.
  Z-stairs need no link UI (positional). 
- [x] **P11** Concept Playground campaign — tools/concept_mapgen +
  scripts/generate_concept_campaign.sh build builtin/org.openglad.concept.glad
  (levels 600-604: Stairs, Mind the Gap, Glasshouse, Drop Zone, Arc Range), each
  multi-floor; metadata-driven discovery needs no picker change. **Self-check
  passes**: every level reloads with floor_count 2 and all floor grids valid.
  Surfaced + fixed a core load bug: `replace_loaded_world_state` (the
  load-into-temp-then-move path) now carries `extra_floors_` across and re-targets
  per-floor smoothers, so multi-floor levels load correctly in the real game too.

## Deferred / follow-up
All originally-deferred caveats are now implemented (see the checklist above):
multi-floor editor authoring (P10), upper-floor alpha ghosting + lower-floor
fade, curses/radar floor-awareness, and Invariant Z parity scenarios.

DONE since: per-object explicit Z within a floor. The v10 level format already
round-trips each object's `worldz` (sub-floor z, save/load in `level_file_io`);
`apply_z_motion` never resets it, so an authored height persists. The editor adds
a brush Z height (keys **.** raise / **,** lower, shown in the panel) applied on
placement, so objects can be elevated on a floor (renders higher; the cylinder
lets low shots pass under).

DONE since: a per-player *Options* toggle (the in-game options menu, key **G**)
disables floor ghosting/fade — `PREF_FLOOR_GHOST` (0 = on, so existing prefs
files keep ghosting), read by `viewscreen::floor_render_alpha` (returns 255 when
off) and the floor-draw loop (draws only `0..camera`, no above-ghosts, when off).

DONE since: glass now reads as glass, not air — `graphlib.cpp::load_map_data`
loads a floor graphic for `PIX_GLASS` and the floor draw loop renders it at a
faint `kGlassAlpha` (capped at the floor's own alpha) so the floor below still
shows through (`view.cpp`). A bespoke glass texture (glints/edges) instead of the
reused floor tile would be a nicer future asset, but the cue exists.

### Vertical parallax (fake-3D floor depth)

Render the stacked floors with a sense of camera height, so the 2D game reads a
bit more 3D. Visual-only (gated `floor_count()>1`; single-floor byte-identical;
**no sim/parity impact** — parity is render-blind).

- **DONE — parallax shift.** Each non-camera floor scrolls at
  `(1 + (f-current_floor_)*kParallaxScroll)` of the camera floor's rate (the
  per-floor `topx/topy` is scaled in the bottom-up floor loop in
  `viewscreen::redraw` and restored after that floor's tiles + entities draw).
  Floors below lag, floors above lead, so they slide relative to the camera floor
  as the player moves (e.g. around a pit) — the depth/angle cue the player sees.
  `kParallaxScroll = 0.05` (subtle); camera floor stays 1:1. Combines with the
  existing alpha ghosting (`floor_render_alpha`) — shift + fade convey depth.
  Seam-free because the whole floor shifts uniformly.
- **DONE — per-floor scale by Z distance (smooth, off-screen layer).** Each
  non-camera floor renders at `(1 + (f-current_floor_)*kParallaxScale)`
  (=0.10/floor) about the viewport centre — floors below shrink, floors above
  enlarge, as if seen from a high camera. Implemented as off-screen-layer
  compositing (NOT a per-tile position scale, which left sub-pixel "weird lines
  on grass" seams): when `floor_count>1 && falpha<255`, `video::floor_layer_begin`
  lazily allocates an alpha-capable ARGB layer, clears the viewport region to
  transparent, and redirects the tile/sprite blits to it (they draw OPAQUE, so
  un-drawn cells stay transparent and air holes still reveal floors below);
  `floor_layer_end` bilinear-stretches the layer about the centre with
  `SDL_SoftStretchLinear` (seam-free smooth scaling) and alpha-blends it over the
  real surface at the floor's depth alpha. Layer surfaces are cached members
  (freed in `~sdl_video`). The camera floor and ghosting-off floors draw straight
  to the screen (no layer), so single-floor is byte-identical (og_test_parity).
  kParallaxScale=0.10, kParallaxScroll=0.05.
  Combined with the shift + alpha ghosting, the stack now reads clearly 2.5D.
