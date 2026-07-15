# Resolution, zoom, and smoothing

OpenGlad draws through three layers:

* The **world canvas** contains gameplay scenery: maps, tiles, sprites,
  effects, and the level editor's map view. `graphics/zoom` controls its
  dimensions.
* The **UI canvas** contains menus, the picker, the intro, help screens,
  dialogs, and results. It stays at 320x200 and is always presented with
  nearest-neighbor scaling.
* During a usable SAI/Eagle frame, a transparent **gameplay UI layer** holds
  pane chrome, the HUD and FPS/CTF blocks, radar, messages, touch controls,
  mini health bars, and damage/healing numbers. It follows the world canvas
  dimensions but is composited with nearest-neighbor scaling after the
  scenery filter.

At the default zoom with smoothing off, the world and fixed UI targets share
the same 320x200 surface and texture, and the gameplay layer is inactive. This
is the byte-identical path used by the historical single-canvas renderer.

## Graphics settings

| Key | Values | Default | Effect |
| --- | --- | --- | --- |
| `fullscreen` | `off`, `borderless`, `exclusive` | `borderless` (`on`) | Native display mode. The browser page owns this on Emscripten. |
| `width`, `height` | Logical window units or physical fullscreen pixels | 640x400 | Windowed size, or the exclusive video mode. Borderless remembers the Windowed size. |
| `windowed_width`, `windowed_height` | Logical window units | 640x400 | Last applied Windowed size, retained while `width`/`height` hold an Exclusive physical mode. |
| `zoom` | `0.1` through `1.0` | `1.0` | Logical world zoom in 0.1 steps. Lower values show more of the level. |
| `smoothing` | `off`, `sai`, `eagle` | `off` | Presentation filter for the world canvas only. |

The shipped configuration writes `zoom: 1.0` and `smoothing: off`
explicitly. Missing or invalid zoom values also select 1.0. Numeric values
are rounded to the nearest 0.1 and clamped to the supported range. Unknown
smoothing values select `off`.

## Window and fullscreen resolution

The DISPLAY screen reads video modes from the display that currently owns the
window, not from a fixed preset list. Fullscreen resolutions are physical
pixel sizes. SDL's logical mode dimensions are multiplied by the mode's pixel
density, so a 1920x1080 Retina mode at 2x appears as 3840x2160. Exclusive
choices contain only modes that SDL actually enumerates.

Windowed sizes use SDL's logical window coordinates. Their fallback list is
derived from the display's usable desktop bounds, excluding taskbars and
docks. This keeps HiDPI window sizes sensible instead of treating a physical
4K mode as a 3840x2160-point window.

The renderer maps that logical window viewport onto SDL's physical output at
present time. A high-density fullscreen mode therefore fills its full pixel
backbuffer, while mouse and touch input remain in logical window coordinates.

Borderless mode always uses the desktop. Its resolution row reports the
monitor's physical size and does not cycle, while retaining the logical
Windowed size for a later switch. Entering Exclusive chooses the desktop size
when it is an enumerated fullscreen mode; otherwise it chooses the largest
enumerated mode. If SDL chooses another mode or rejects fullscreen, the mode
and resolution labels update to the state that was applied.

## Zoom and canvas size

The world canvas is derived from the classic canvas, not from the window:

```text
world width  = 320 / zoom
world height = 200 / zoom
```

The width is rounded up to a multiple of four for the software scalers. The
height uses integer division. These are the exact selector sizes:

| Zoom | World canvas |
| ---: | ---: |
| 1.0 | 320x200 |
| 0.9 | 356x222 |
| 0.8 | 400x250 |
| 0.7 | 460x285 |
| 0.6 | 536x333 |
| 0.5 | 640x400 |
| 0.4 | 800x500 |
| 0.3 | 1068x666 |
| 0.2 | 1600x1000 |
| 0.1 | 3200x2000 |

A lower value zooms out by increasing the logical canvas. For example, 0.5
shows twice as much world on each axis as 1.0. Its 640x400 canvas happens to
match the default window size, but the two settings are independent.

Resizing the window or changing fullscreen mode only changes the destination
viewport. It does not resize the world canvas or recalculate viewscreen
geometry. Rendering cost therefore follows the selected zoom instead of the
physical window size.

## Smoothing

`smoothing: off` presents the world canvas with nearest-neighbor filtering.
`sai` and `eagle` run the corresponding software 2x scaler over the world
canvas, then present that result to the viewport. Smoothing does not change
the logical canvas dimensions or pane layout.

The filter is chosen per layer. Both the fixed UI canvas and gameplay UI
overlay are always nearest, including when the scenery uses SAI or Eagle. The
software pass is capped at 4,096,000 output pixels (about 16.4 MB each for its
surface and texture), which supports zoom 1.0 through 0.3. At zoom 0.2 and 0.1
the selected smoothing preference is retained, but presentation falls back to
nearest. This avoids a 6400x4000 scratch target and a roughly 102 MB upload on
every 0.1-zoom frame.

## Compatibility with older configurations

`graphics/scale` is retired and ignored. It used to derive a world canvas
from the current window, which made both the view and rendering cost change
when the window changed.

`graphics/render` is also retired as a whole-frame presentation setting. The
renderer no longer smooths menus or gameplay chrome. A configuration without
`graphics/smoothing` inherits `render: sai` or `render: eagle` as its new
world-only smoothing mode. `render: normal` and `render: double` become
`smoothing: off`. An explicit `graphics/smoothing` value always wins.

The old action and engine enum values remain reserved for compatibility, but
the DISPLAY menu no longer exposes the whole-frame rendering control.

## In-game controls

The DISPLAY screen has two live controls:

* **Zoom** cycles from 1.0 down through each 0.1 step to 0.1, then wraps to
  1.0. A changed canvas size immediately re-lays out active viewscreens.
* **Smooth** cycles `off -> sai -> eagle -> off`. This changes only the world
  presentation path, so no layout pass is needed.

Menus remain on the fixed UI canvas while these settings are applied. Both
values are saved when OPTIONS exits, and RESTORE DEFAULTS returns them to 1.0
and `off`.

## Layout and pointer mapping

Pane geometry is a pure function of the world canvas dimensions
(`og::view_layout::compute_view_layout`): one player uses the full canvas,
two players use side-by-side halves, three players use a full-height left pane
and a split right half, and four players use quadrants. Split panes retain a
two-pixel seam. The `PREF_VIEW` chrome insets and HUD blocks keep their fixed
pixel sizes. At 320x200 the formulas reproduce the legacy tables exactly.

Pointer input maps through whichever canvas is active:

```text
canvas position = (window position - viewport offset)
                  * canvas size / viewport size
```

Menus therefore retain their 320x200 pointer space. Gameplay maps into the
selected world canvas. Overscan changes the viewport rectangle but not either
logical canvas.

The level editor still uses absolute 320x200 coordinates for its panel
chrome, so it temporarily pins the world canvas to 320x200 and restores the
zoom-derived dimensions on exit. Its map follows the selected world smoothing,
while its minimap, authoring guides, picker previews, menus, and controller
cursor use the nearest gameplay-UI layer. Fixed-coordinate in-game screens
temporarily switch to the UI canvas instead. When the world canvas is split,
they start from a nearest-scaled copy of the current world frame and restore
world routing when they close.

## Emscripten

Zoom and world smoothing use the same code path in native and Emscripten
builds. The browser backing window remains 320x200 and CSS controls its
displayed size, but the world canvas may be larger according to `zoom` before
it is presented into that backing window. Native resolution and display-mode
controls remain unavailable in the browser.

Canvas and window dimensions are render-side state. The deterministic
simulation does not read them; `scripts/check_render_no_sim_writes.sh`
enforces that boundary.
