# Voxel world renderer — design (spike stage)

**Issue:** #264 ("Voxels???"). **Branch:** `feature/voxel-render`, based on
master `8e0142c2`. All file:line anchors are against that commit; anchor by the
quoted text when lines drift.

**Maintainer rulings so far (2026-08-29):**

- The 2D game is *also* voxel-rendered — one renderer, one scene model, the
  classic look is just a camera. No second renderer kept alive ("no rule
  twins").
- Pixel parity with today's output is the goal, **not an obsession**: best
  effort, with every deviation named and justified in §7.
- Camera controls are for demo renders only. The game stays locked to the
  classic camera for now.
- Gameplay must not change. The sim never reads render data (§6).

---

## 0. One-paragraph summary

Every drawable thing — tile, decor, sprite frame — becomes a *volume*: its
existing 8-bit palette-index bitmap extruded straight up by a per-kind height.
A software rasterizer draws volumes as stacked slices into the XRGB world
canvas through the same palette LUT the blitters use today, with a per-pixel
depth buffer. A *camera* is a projection `(x, y, z) → (sx, sy, depth)`. The
**Classic** camera collapses every volume to its base slice and projects it to
exactly the screen position the current blitters use, with depth = the current
painter order — so its output is the current output by construction. The
**Free** camera is an orthographic yaw/pitch/zoom view of the same scene where
the slices stack into real columns. Heights are render-only data; the sim is
untouched, so parity goldens do not move and every gameplay rule (shooting
through allies, flying over water, wall passability) is exactly what it is
today.

---

## 1. What exists today (survey, condensed)

- The world canvas is `SDL_PIXELFORMAT_XRGB8888` (`sai2x.cpp:825`). Palette
  indices are converted at blit time by `palette_color_lut()`
  (`video_sdl.cpp:706`). There is no paletted framebuffer.
- Draw order is **strict list order, no y-sort**: per floor bottom-up, tiles
  (opaque `putbuffer`), decor (transparent), then `fxlist → oblist → weaplist`
  in list order (`view.cpp:1821 draw_floor_entities`). Effects pre/post passes
  bracket the entity pass.
- Tiles are 16×16 palette indices (`GRID_SIZE = 16`); sprites are stacked
  frames in a `PixieData` (`pixie_data.h`), index 0 transparent, 248..255 the
  team band remapped to `teamcolor + (255 - c)`.
- Sprite screen position is `(worldx - topx + xloc, worldy - worldz - topy +
  yloc)` (`walker_draw.cpp:543`) plus lunge/recoil offsets, then one of the
  `walkputbuffer` variants: plain, `_alpha`, `_flash`, and the 12-arg mode
  variant (`INVISIBLE_MODE` rng-thinned, `OUTLINE_MODE`, `PHANTOM_MODE`
  read-back shifts) at `video_sdl.cpp:2063-2860`.
- Multi-floor: floors composite through an ARGB layer with per-floor alpha,
  parallax scroll and scale (`view.cpp:604 compute_floor_pass`,
  `floor_layer_begin/end`), plus the floor-glide camera dolly.
- The sim already has a z axis: `worldz()` / `sizez()` (`sim_entity.h:67`),
  cylinder z-overlap in `ob_pass_check` (`obmap.cpp:369`). The basketball's
  fake-Z is Lua-side `setxy(gx, gy - z_px)` and a shadow frame strip.
- No golden-image tests exist. `tests/integration/test_render_effects.cpp`
  is the byte-comparison idiom (`grab_rect` + `rects_equal`). Capture tooling:
  `fx_capture::dump_frame` (P6 PPM), `scripts/fx_review/`,
  `scripts/media/capture_showcase.sh`.
- `openscen` draws through the same `viewscreen::redraw`; the curses client
  reads `GameWorld` directly and never touches the canvas.

---

## 2. Scene model

World units are pixels: x right, y down (as today), **z up**, one voxel = one
pixel cube. A tile is 16×16×h.

```
struct VoxelVolume {
    const unsigned char* texels;  // w*h palette indices, index 0 = empty
    int w, h;                     // footprint in pixels
    float x, y, z;                // base corner (world px); z = base height
    int height;                   // extrusion in voxels; 0 = flat (one slice)
    Material material;            // team color, alpha, mode flags (§4)
    int rank;                     // painter rank for the Classic camera (§3)
};
```

A volume is a texture extruded by `height`: column `(px, py)` exists iff
`texels[py*w+px] != 0`, and occupies `z .. z+height`. The top slice carries the
texel colour; lower slices carry the same texel darkened by a fixed side shade
(§4). That is sprite-stacking, chosen over true per-face voxel raster because
(a) it is what our art is — top-down 16-colour frames have no side detail to
recover, (b) it needs one primitive (draw a textured quad at a z with a depth
test), (c) the Classic camera falls out of it trivially.

**Heights are render data**, owned by the renderer, never by the sim:

| kind | height | source |
|---|---|---|
| floor tiles (grass, dirt, snow…) | 0 | per-`PIX_*` table in the renderer |
| walls (`TYPE_WALL` genre, wallsides, arrow walls) | 16 | same table |
| water / lava / marsh / glass | 0, base z −2 | same table (reflections later) |
| trees | 20 | same table |
| decor | 0..12 by kind | `DECOR_*` table |
| living | 12 | family descriptor `render.height`, default 12 |
| weapons / projectiles | 3, base z +8 | descriptor, default: lifted (§7 D3) |
| fx | 8 | descriptor default |
| treasure | 3 | descriptor default |
| upper floors | stacked at z = floor × 24 | renderer constant |

The per-family value lives in the pack Lua family descriptor as a render-only
field (`family_descriptor.h` grows one int; loader default when absent). No
snapshot, replay or save field changes — the value never crosses the wire.

## 3. Cameras

A camera is `Projection project(float x, float y, float z) → {sx, sy, depth}`
plus a flag saying whether volumes are collapsed.

**Classic** (the game camera, locked for now):

```
collapse = true            // every volume drawn as its single base slice
sx = x - topx + xloc
sy = y - z - topy + yloc   // the existing worldz raise, walker_draw.cpp:543
depth = rank               // painter order, NOT z
depth test: pass if depth >= stored   (ties overwrite ⇒ list order wins)
```

`rank` is the position in today's draw sequence: floor index, then
{tiles, decor, entities} band, then list index. Because the scene is emitted
in exactly today's order and ties overwrite, the depth buffer is inert in
Classic — it exists so the same rasterizer serves both cameras.

**Free** (demo renders): orthographic, yaw θ about z through the camera
target, pitch φ (90° = top-down), scale s:

```
(x', y') = rotate_z(x - cx, y - cy, θ)
sx = x' * s + view_cx
sy = (y' * sinφ - z * cosφ) * s + view_cy
near = z * sinφ - y' * cosφ      // larger = nearer; depth test keeps nearer
collapse = false
```

Perspective is deliberately out of scope: it buys nothing for 320×200 art
and costs a divide per pixel in wasm.

Rasterizing one slice: the slice is a world-space rectangle at height `z0+s`
with corners `(x, y)…(x+w, y+h)`. Project the four corners (affine under both
cameras), take the screen bounding box, and for every pixel inside it
inverse-map to texel space (nearest), skip index 0, apply the material, depth
test, write `lut[index]` (or the shaded RGB). Under Classic the inverse map is
the identity, so the top-slice path degenerates to today's blit loop —
including the exact clip against `(xloc, yloc, endx, endy)`.

## 4. Materials — the per-pixel logic exists once

The colour logic in the `walkputbuffer` family (index-0 skip, team band
remap, alpha blend, hurt flash, invisible thinning, outline, phantom
read-back shift) becomes one `Material` applied per emitted pixel. In Classic
the top slice + identity map reproduces each variant; in Free the same
material applies to every slice. Consequences:

- The `walkputbuffer*` bodies are **deleted** once their call sites emit
  volumes; a variant survives only as a Material flag. That is the no-twins
  rule applied to blitters.
- The rng-thinning of `INVISIBLE_MODE` keeps drawing from the render-side rng
  it draws from today (it is already render-only; see the floor-glide doc's
  rng note).
- `PHANTOM_MODE` reads the destination pixel back. It reads whatever the
  depth-tested framebuffer holds at that moment — identical under Classic,
  merely plausible under Free.

Side shade for Free: slices below the top are drawn at RGB × 0.72 (a fixed
constant; there is no index-space darkening LUT in the tree and the
`SHIFT_LIGHTER/DARKER` `%8` heuristic is too coarse). Palette cycling
(water 208–223, orange 224–231) keeps working because the LUT is consulted
per pixel at draw time.

## 5. Integration plan (after the spike)

The floor-pass framework in `viewscreen::redraw` (`view.cpp:794-984`) —
per-floor alpha layer, parallax, glide, effects pre/post passes, cloud
overlay, shake — is **kept**. It is camera-independent choreography, and
re-deriving it geometrically in Free view would be a twin of the effects
system. What changes inside it:

1. `renderer->draw_tile / draw_decor` and `draw_walker`'s blit calls emit
   `VoxelVolume`s into a per-view `VoxelScene` instead of blitting.
2. The scene is flushed once per floor pass through the view's camera into
   the active render surface (which may be the floor layer — `floor_layer_begin`
   swaps `E_Screen->render`, so the flush target follows it for free).
3. Health bars, damage numbers, beacons and the radar are GameplayUI overlays
   anchored through `GameplayUiProjector`; they get their anchor from
   `camera.project(foot)` instead of the screen-y arithmetic. Identity under
   Classic.
4. Old blit bodies deleted as their last caller goes.

Per-view camera state hangs off `viewscreen` next to the PR #269 camera-pane
state; the game constructs Classic only. A `--voxel-demo` path in the media
capture tooling renders Free views for screenshots.

## 6. Gameplay: unchanged by construction

The renderer only reads `GameWorld`. Every rule the maintainer asked about
lives in the sim and never consults render data:

- **Shooting through a teammate**: `obmap.cpp:361` "Let our own team's
  weapons pass over us" in `ob_pass_check`. Unchanged. Visually, in Free
  view, the arrow passes through the ally's column — mitigated by D3.
- **Walls / water / flying**: `game_world.cpp:677 query_grid_passable`,
  `BIT_FLYING || flight_left()` bypasses. Heights in §2 are never read by it.
- **Line of sight**: the 2D strip check at `walker.cpp:1275`. Stays 2D.
- **Cylinder z / multifloor**: `worldz`/`sizez` are sim state the renderer
  *reads* for slice base height. It never writes them.
- **Parity**: `og_test_parity` dumps sim state; no golden moves. The pixel
  gate is separate (§8).

If heights are ever to *matter* (arrows over low walls, high ground), that is
a lobby-negotiated sim ruleset with its own goldens — the #205 shape — and a
different PR.

## 7. Deviations register (Classic camera vs today)

Each entry is a place the voxel Classic output is allowed to differ, why, and
how it is measured. Anything not listed here must be byte-identical.

| id | deviation | justification | status |
|---|---|---|---|
| D1 | none expected in Classic for tiles, decor, plain sprites | identity map + painter depth | spike measures |
| D2 | directional sprites have no side art; in Free view an east-facing fighter is a flat-topped silhouette | our art is top-down; per-family heightmap siblings can come later | Free only |
| D3 | projectiles / flying entities drawn lifted (`z + 8`) in Free view so they clear fighter columns | the sim passes them through allies; a ground-height arrow would visibly clip | Free only (Classic collapses z-lift? **no** — the lift is applied only when `!collapse`, so Classic is unaffected) |
| D4 | `PHANTOM_MODE` read-back sees depth-tested pixels | inherent; identical in Classic | measured |
| D5 | multifloor parallax/scale handled by the existing floor layer, not by camera geometry | keeping the effects choreography single-sourced | design choice |

## 8. Byte-identity gate

A new integration test loads real levels (gladiator `scen1`, a westlands
level, a multifloor tower floor, a modes ball level), renders each through the
old path (`grab_rect` of the world canvas) and through the voxel Classic
camera, and asserts equality with effects OFF; a second pass with the default
effects ON reports the mismatch count and pins it at 0 for D1 scenes. During
the spike the same comparison is a *measurement* (count + PPM diff dump under
`OG_VOXEL_SPIKE_DIR`), not an assertion.

## 9. Spike scope (this stage)

Goal: the picture that decides whether to continue, plus a parity number.

- New: `include/openglad/interface/render/voxel_scene.h`,
  `src/interface/render/voxel_raster.cpp` — scene, cameras, slice rasterizer
  into a caller-supplied XRGB buffer (`std::span<std::uint32_t>`, pitch,
  clip rect, 256-entry LUT). No SDL in the interface component.
- Scene builder from a `GameWorld` + `LevelVisuals` for floor 0 (plus upper
  floors stacked, if cheap): tiles, decor, entities in today's list order
  with the plain material (index 0 skip, team band). Modes, flash, alpha,
  HP bars, effects passes: **out of scope** for the spike.
- An env-gated integration test (`OG_VOXEL_SPIKE_DIR`) that for each scene:
  renders old-path 320×200, renders voxel Classic, writes both + a diff PPM,
  prints the mismatch count; then renders 3–4 Free views (e.g. θ=30°/φ=60°,
  θ=45°/φ=45°, θ=0°/φ=35°, plus a 2× zoom) to PPM. `convert` (imagemagick,
  in the flake) turns them into PNGs for review.
- Nothing in the game path changes in the spike. No deletions yet.

Exit criteria: mismatch count 0 on the effects-OFF scenes (or each nonzero
explained by a D-entry), and a Free-view render the maintainer wants to keep
looking at.

---

## 10. Stage 2 — carved models (maintainer direction, 2026-08-29)

Ruling: the stamp extrusions proved the pipeline; entities should now be
**real voxel models that rotate with the walker's facing**. The eight facing
frames of a family are eight rotations of one character seen by one camera —
so the model is *reconstructed*, not authored, by **space carving**:

- Assume the game camera is orthographic at elevation θ above the ground
  plane (θ is a tunable; the sprite art reads as roughly 45–65°).
- Model grid `16×16×Z` (x = width, y = depth, z = up), Z ≈ 20. For each of
  the 8 facings d (yaw_d = the direction's compass angle): rotate the voxel
  grid by yaw_d, project each voxel through the camera onto the facing-d
  sprite frame; a voxel whose projection lands on a transparent pixel in ANY
  facing is carved away.
- Color: a surviving voxel is *seen* by a view if no surviving voxel projects
  to the same pixel nearer the camera; it takes its palette index (indices,
  never RGB — the team band must survive) from the views that see it
  (nearest-depth wins, majority as the tiebreak). Unseen interior voxels copy
  their nearest surface neighbour.
- Validation is a measurement, not a gate: re-render the carved model at each
  of the 8 facings under the assumed camera and report per-facing pixel
  agreement vs the original frame. The comparison strip (sprite row vs
  re-render row, all 8 facings, a few θ values) is the deliverable that
  answers "what does it look like, compared to the original".

**Parity consequence, decided:** the Classic camera keeps drawing the sprite
frames — they are exactly the baked view of the model from the game camera,
i.e. an imposter cache, not a renderer twin. Free-camera views draw the
carved model rotated by `curdir` (yaw += 45° per step) so facing is real.
Terrain stays extruded, with two look fixes from stage 1: wall columns sample
the matching `PIX_WALLSIDE*` art on their side slices, and trees get a
canopy profile (narrow trunk, wide top) instead of flat extrusion.

**§10 bar, raised (maintainer, 2026-08-29):** the models are the product, not
a fidelity exercise. Acceptance = "badass models that could work under normal
camera angles and look incredible": re-rendered at the classic game angle a
model must read as a *better* version of the sprite, not a degraded one.
Expected consequences for the pipeline (whatever it takes): supersampled
carving (frames upscaled 2–4× → 32³/64³ grids), post-carve cleanup (floating
voxel pruning, hole fill, optional bilateral symmetry prior), baked shading
(palette-ramp AO / edge darkening) so faces read at small sizes, and
per-family parameter polish. Sprite-agreement % remains a sanity check only —
the gate is Fable's visual review.

## 11. Carving verdict and the rig pivot (2026-08-29)

**Space carving is a dead end for shape — measured, not guessed.** Rounds 1–2
(`bd4be050`, `dea559ed`) carved 64³ hulls with normal-aligned colour, cleanup,
baked AO and a cube-face renderer. The classic-angle re-renders read as the
characters, but every hero view is a solid of revolution ("mushrooms"). The
probe: relabelling the eight facings by any multiple of 90° leaves the hull
essentially unchanged (IoU 58–61% between all offsets; only mirroring drops it
to ~40%). A 16×16 near-top-down sprite's eight silhouettes are nearly
rotation-invariant, so their intersection is by construction a body of
revolution, and photo-consistency cannot break it because the distinguishing
features (the footman's red band, the orc's harness) appear in every facing.
No carver tuning fixes this. The carver stays only as an optional colour
sampler.

**Pivot: parametric voxel rigs.** Shape comes from a stylized voxel-art rig
per archetype (humanoid, skeleton variant, ghost sheet, slime blob, robe) —
chunky proportions, 32 voxels tall (2× sprite scale), built from box /
ellipsoid / cylinder primitives with mirrored symmetry — plus a per-family
part list (helmet, hood, hat, cape, robe, sword / bow / staff / axe, quiver,
shield, tusks, ears, horns) and palette indices sampled from the family's own
sprite, team-band indices where the sprite uses team colour. Rendering reuses
the round-2 AO + cube-face lighting. Integration home after the prototype:
a render-only `voxel = { rig = ..., parts = ..., colors = ... }` block in the
pack family descriptor (families are Lua mods), loader default = plain
humanoid coloured from the sprite. Rigs are also the path to animated
walk/attack poses in Free view later. The classic camera is unaffected
(sprites remain the imposter — §10).

## 12. Rigs rejected — reliefs from the sprites (maintainer, 2026-08-29)

Ruling: the parametric rigs (rounds 3–5) "took a lot of liberties; the art
style is completely lost. I want the same art style, as close as possible to
the original look, just with voxel models that concretize to our current
shapes." Rigs are withdrawn as the shape source.

**Construction: per-facing voxel bas-reliefs.** Every sprite frame becomes a
relief whose front face IS the frame — exact silhouette, exact palette
indices, no invented parts, no lighting applied to the front face — and whose
thickness is a heightfield from the silhouette's Euclidean distance transform
(`h = clamp(k·EDT, 1, max_by_order)`; living ≈ 6, treasure 3, weapons/FX 2),
side voxels taking the nearest front pixel's index. The relief plane is
perpendicular to the game camera (elevation θ ≈ 55°), positioned so the
frame's pixels project to their exact screen positions under that camera —
which is why the classic look is reproduced by construction and the Classic
camera (collapsed) stays byte-identical. Under the Free camera the relief
billboards toward the camera's yaw, and the walker shows the facing frame
matching its facing relative to the camera (8-rotation scheme, 45° bins), so
turning the camera reveals the art the artists drew for that side, with
volume and side shading (sides ×0.75, never the front). Applies to every
order — fighters, treasure, projectiles, effects — so no flat stamps remain.
Pitch far from θ shows the relief's lean; the Free camera's useful pitch
range is therefore ~40–70°, which is the top-down game's natural range.

## 13. Reliefs: fidelity without volume — the fitted-body pivot (2026-08-29)

Round 6 (`2a381b79`) implemented §12 and measured 100.00% fidelity on all
8 families × 8 facings with Classic parity intact — and **no visible volume**:
a relief that billboards to the camera hides its thickness directly behind
its front face (0.26 px/layer of side at pitch 40/70; ~1.5 px total). The
lineup is the sprites with drop shadows; the turntable is a Doom-style frame
swap. Honest, style-perfect, not a voxel model. Reliefs stay in the tree as
the fallback for entities no template fits.

**Pivot: fitted bodies, textured by the art.** "Concretize the sprite" =
(1) a small parametric body plan per archetype — humanoid: head ellipsoid,
torso box, two arms, two legs, optional cape sheet, optional weapon rod
(side/angle/length), optional headgear; blob for ghosts/slimes — with every
dimension an integer parameter; (2) the parameters SOLVED, not chosen: for
each family minimise the silhouette mismatch between the body rendered at the
game camera (elevation θ, itself fitted from a small set) rotated to each of
the 8 facings and the family's 8 first-walk frames (symmetric IoU loss,
coordinate descent with restarts; templates compete, best loss wins);
(3) colour by back-projecting the 8 frames onto the fitted surface — each
surface voxel takes the palette index of the frame whose view best aligns
with its normal among the views that see it (the round-2 machinery), team
band preserved; unseen voxels copy the nearest textured neighbour. Front
faces get no AO; face shading only ×0.85/×0.70 on non-front faces so the
palette reads as pixel art. The result projects to an approximation of each
facing at the game camera (report agreement per facing) and is a solid,
sprite-coloured figure from every other angle. The rig template code is
reused for primitives only; no hand-chosen proportions or invented parts
survive. Classic camera unchanged (sprites remain the imposter).
