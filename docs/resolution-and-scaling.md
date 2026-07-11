# Resolution and scaling

OpenGlad historically rendered everything into one fixed 320x200 canvas and
stretched it to the window. The renderer now owns **two logical canvases**:

* **World canvas** — gameplay: viewscreens, HUD panels, radar, the level
  editor's map view. Variable dimensions, controlled by `graphics/scale`.
* **UI canvas** — menus, picker, intro, help, dialogs. **Pinned at 320x200
  forever**, so classic menu layouts, mouse translation (logical 320x200
  coordinates) and pixel captures stay valid at any world resolution.

While the world canvas is at the default 320x200 the two canvases share one
surface/texture pair, byte-identical to the historical single-canvas
renderer.

## cfg keys (`graphics` section)

| Key | Values | Meaning |
|-----|--------|---------|
| `width`, `height` | pixels | Window size (default 640x400). |
| `render` | `normal`, `sai`, `eagle` (`double` parses but behaves as `normal`, as it always has) | The **legacy** present engine for the classic 320x200 canvas. |
| `scale` | *(omitted)*, `off`, `1`, `2`, `3`, `4`, `8`, `sai`, `eagle` | **Optional** world-canvas scale factor — see below. |

### `graphics/scale`

* **Omitted / `off` / unrecognized** — classic behavior: one 320x200 world
  canvas presented through the `render` engine. Every pre-existing cfg (which
  cannot contain the key) behaves exactly as before; this is the byte-identical
  default.
* **`1`, `2`, `3`, `4`, `8`** — the world canvas is sized `window / N`
  (width rounded down to a multiple of 4, both axes clamped to a **320x200
  minimum**) and presented with an unfiltered (nearest) GPU stretch, so each
  world pixel covers about N×N window pixels. `1` = native-resolution
  gameplay; `2` on the default 640x400 window reproduces the classic look.
* **`sai`, `eagle`** — the world canvas is sized `window / 2` (same clamp),
  software-smoothed 2x by the Super2xSaI / SuperEagle scaler, then
  GPU-stretched. This generalizes the old `render: sai` look to any window
  size.

### Interaction between `render` and `scale`

`scale` overrides the **world** present path only. The UI canvas — and, when
`scale` is absent, the world canvas too — always presents through the
`render` engine. So old cfgs are untouched, and e.g. `render: sai` +
`scale: 1` gives smoothed menus with native-resolution gameplay.

### In-game control (OPTIONS)

The OPTIONS screen has a **Scale** button (id `world_scale`, action
`CycleWorldScale`) that steps `graphics/scale` through
`off -> 1 -> 2 -> sai -> eagle -> 3 -> 4 -> 8 -> off` and applies each value
to the live renderer immediately (`video::reapply_world_scale`, then
`screen::relayout_views` when the canvas dims moved). Applying mid-menu is
safe because menus draw and present on the UI canvas, which the key never
touches. The **rendering engine** button (`graphics/render`, still marked
"needs restart" for its own present path) remains a separate, independent
setting on the same screen. RESTORE DEFAULTS re-derives the world canvas too,
so the live renderer always matches the restored cfg. Both settings persist
when OPTIONS exits (`cfg.save_settings()`).

## Pointer mapping

`handle_mouse_event` converts window pointer coordinates by the **active
canvas** dims: `canvas = (window - viewport_offset) * (canvas_dim /
viewport_dim)`. Menus (UI canvas active) keep the classic logical 320x200
mouse space; gameplay and the level editor (world canvas active) map to the
world canvas — identical to the classic constants in every default run, and
under the editor's classic pin. The overscan viewport rect is shared by both
canvases; only the divisor is per-layer. Touch/controller conversions follow
the same rule.

### Why the 320x200 minimum?

The classic panes are the smallest geometry the game was written against:

* the sprite clipper and viewscreen math assume at least classic pane sizes,
* the radar is a fixed 60x44 block anchored to the pane's bottom-right,
* the score-panel HUD draws fixed-size text/bar blocks in the `PREF_VIEW`
  chrome insets (up to 106/60 px per side in single-player),

so `window/scale` is clamped up to 320x200 — a too-small window simply gets a
coarser-than-requested stretch.

## Viewscreen layout

Pane geometry is a pure function of the world canvas dimensions
(`og::view_layout::compute_view_layout`, `include/openglad/interface/render/view_layout.h`):
1p full-canvas; 2p side-by-side halves with a 2px seam; 3p full-height left
pane plus a split right half; 4p quadrants. The `PREF_VIEW` inset modes keep
their fixed-pixel chrome margins (the HUD blocks anchored there do not grow).
At 320x200 the formulas reproduce the legacy hardcoded tables verbatim
(pinned by `tests/unit/test_view_layout.cpp`).

On a window resize under a non-legacy `scale`, the world canvas is re-derived
from the new window size and every viewscreen is re-laid out
(`screen::relayout_views`).

## Deliberate pins and follow-ups

* **Level editor** — its panel chrome and mouse mapping still use absolute
  320x200-era coordinates, so it pins the classic world canvas for its whole
  session and restores the scale-derived canvas on exit. Follow-up:
  right-anchor the chrome from `canvas_w` and drop the pin.
* **Demo compositor** (`openglad_demo`) — assumes 320x200 session cells;
  forces `scale: off`.
* **Emscripten** — keeps the classic single-canvas path (`scale` is ignored;
  the wasm window is forced to 320x200). Follow-up once the wasm present path
  is exercised.
* **In-game dialogs** (quit prompt, per-player options menu) draw at
  320x200-era coordinates onto the world canvas; on large canvases they
  appear top-left rather than centered. Cosmetic, non-default-only.
* The deterministic sim never reads canvas or window dimensions
  (`scripts/check_render_no_sim_writes.sh`).
