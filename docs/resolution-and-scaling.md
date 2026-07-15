# Resolution, zoom, and smoothing

OpenGlad renders three logical layers:

* The **world canvas** contains maps, tiles, sprites, effects, and the level
  editor's map view. Its size follows the logical window and `graphics/zoom`.
* The **UI canvas** contains menus, the picker, the intro, help screens,
  dialogs, and results. It remains 320x200 and always uses nearest-neighbor
  presentation.
* A transparent **gameplay UI layer** holds pane chrome, the HUD, radar,
  messages, touch controls, mini health bars, and damage or healing numbers.
  It retains classic pixel density, matches the zoom-1 world aspect, and is
  composited with nearest-neighbor sampling after the zoomed scenery.

At a 320x200 logical window, `zoom: 1.0` with smoothing off can share the
historical surface and texture. Larger windows use a window-sized world
canvas, a classic-density gameplay UI, and the separate fixed menu UI.

## Graphics settings

| Key | Values | Default | Effect |
| --- | --- | --- | --- |
| `fullscreen` | `off`, `borderless`, `exclusive` | `borderless` (`on`) | Native display mode. The browser page owns this on Emscripten. |
| `width`, `height` | Logical window units or physical fullscreen pixels | 640x400 | Windowed size, or the exclusive video mode. Borderless remembers the Windowed size. |
| `windowed_width`, `windowed_height` | Logical window units | 640x400 | Last applied Windowed size, retained while `width`/`height` hold an Exclusive physical mode. |
| `zoom` | `0.1` through `1.0` | `1.0` | Logical world zoom in 0.1 steps. Lower values show more of the level. |
| `smoothing` | `off`, `sai`, `eagle` | `off` | Presentation filter for world scenery only. |

The shipped configuration writes `zoom: 1.0` and `smoothing: off`. Missing or
invalid zoom values also select 1.0. Numeric values are rounded to the nearest
0.1 and clamped to the supported range. Unknown smoothing values select
`off`.

## Window and fullscreen resolution

The DISPLAY screen reads video modes from the display that currently owns the
window, not from a fixed preset list. Fullscreen resolutions are physical
pixel sizes. SDL's logical mode dimensions are multiplied by the mode's pixel
density, so a 1920x1080 Retina mode at 2x appears as 3840x2160. Exclusive
choices contain only modes that SDL enumerates.

Windowed sizes use SDL's logical window coordinates. Their fallback list is
derived from the display's usable desktop bounds, excluding taskbars and
docks. This keeps HiDPI window sizes sensible instead of treating a physical
4K mode as a 3840x2160-point window.

The renderer maps the logical viewport onto SDL's physical output at present
time. A high-density fullscreen mode fills its physical backbuffer while
mouse and touch input remain in logical window coordinates.

Borderless mode always uses the desktop. Its resolution row reports the
monitor's physical size and does not cycle, while retaining the logical
Windowed size for a later switch. Entering Exclusive chooses the desktop size
when it is an enumerated fullscreen mode; otherwise it chooses the largest
enumerated mode. If SDL chooses another mode or rejects fullscreen, the mode
and resolution labels update to the state that was applied.

Exclusive mode is unavailable on multi-display X11 desktops. SDL's XRandR
mode-switch path can disable a monitor and fail to re-enable it when the
resized root screen no longer contains the other outputs. OpenGlad uses
Borderless there, including when a saved configuration requests Exclusive.
Single-display X11, Wayland, Windows, and other backends retain the normal
Exclusive choices.

## Zoom and canvas size

`zoom: 1.0` restores the `graphics/scale: 1` behavior on `master`: the world
canvas follows the logical window. This means a widescreen window shows more
world horizontally instead of stretching a 320x200 frame across the display.

For zoom value `z`, the dimensions are derived as follows:

```text
base width   = max(logical window width, 320)
base height  = max(logical window height, 200)
world width  = base width / z, rounded down to a multiple of 4
world height = base height / z
```

Integer division rounds down. The four-pixel width alignment is required by
the software scalers and partial-present paths. For the default 640x400
window, the selector produces:

| Zoom | World canvas |
| ---: | ---: |
| 1.0 | 640x400 |
| 0.9 | 708x444 |
| 0.8 | 800x500 |
| 0.7 | 912x571 |
| 0.6 | 1064x666 |
| 0.5 | 1280x800 |
| 0.4 | 1600x1000 |
| 0.3 | 2132x1333 |
| 0.2 | 3200x2000 |

The 0.1 canvas would be 6400x4000, so it is not offered at this window size.
The available lower limit depends on the window, the renderer's maximum
texture dimension, and an 8,388,608-pixel world-canvas budget. The budget
bounds the ARGB8888 surface and streaming texture to roughly 64 MiB combined.
At common logical sizes, and assuming the renderer's texture limit is large
enough, the deepest choices are:

| Logical window | Deepest zoom |
| ---: | ---: |
| 320x200 | 0.1 |
| 640x400 | 0.2 |
| 1280x720 | 0.4 |
| 1920x1080 | 0.5 |
| 2560x1440 | 0.7 |
| 3440x1440 | 0.8 |
| 3840x2160 | 1.0 |

Zoom 1.0 remains the exact window-sized baseline even when the pixel budget
would exclude a deeper step. A separate 16,777,216-pixel absolute ceiling
uniformly caps exceptionally large baselines (above ordinary 4K/5K desktops)
before allocation while retaining their aspect ratio. The selector wraps at
the safe lower limit. A
configuration that requests an unavailable step is raised to the deepest
supported value, and the effective value is written back to the live config.
Canvas replacement is transactional: if SDL still rejects an allocation, the
previous working canvas remains active.

Resizing the window or completing a fullscreen transition recomputes the
world canvas and viewscreen layout. Rendering cost therefore follows both the
window size and the selected zoom.

Gameplay chrome does not inherit the world resolution or zoom. Its baseline
starts at 320x200 and expands the short axis to match the zoom-1 world aspect:
16:10 uses 320x200, 16:9 uses approximately 356x200, and 4:3 uses 320x240.
That keeps bitmap text and HUD controls at their historical readable density
while the world uses the display's available logical resolution.

## Aspect ratio and pointer mapping

Every active canvas is aspect-fitted inside the overscan viewport. A
window-derived world canvas normally fills the viewport; the fixed menu UI is
centered with black pillarbox or letterbox bars. The aspect-expanded gameplay
UI follows the world destination without visibly distorting its pixels.

Mouse and touch coordinates use the same fitted rectangle as presentation:

```text
canvas position = (window position - fitted viewport offset)
                  * canvas size / fitted viewport size
```

Menus retain their 320x200 pointer space. Ordinary gameplay mouse coordinates
map into World; touch controls map into the stable gameplay-UI space so their
hit targets do not move or shrink with zoom. Overscan changes the available
viewport rectangle but not these logical coordinate systems.

## Smoothing and bounded fallbacks

`smoothing: off` presents world scenery with nearest-neighbor sampling. `sai`
and `eagle` run the corresponding software 2x scaler over the world canvas,
then present the result. Smoothing does not change the logical canvas size or
pane layout.

The filter applies only to scenery. The fixed UI canvas and gameplay UI layer
always use nearest-neighbor sampling, so menus, text, pane chrome, HUD, radar,
and messages do not enter the SAI/Eagle pass.

The doubled SAI/Eagle scratch is capped at 4,096,000 output pixels and must
also fit the renderer's texture limit. This allows at most a 1,024,000-pixel
source canvas. For example, a 640x400 window can use smart smoothing through
zoom 0.5; a 1920x1080 world canvas is already above the scratch budget at
zoom 1.0. When the scratch is too large or allocation fails, the selected
preference is retained but that frame is presented nearest.
The DISPLAY label adds `N/A` to a selected SAI/Eagle mode while the current
canvas or renderer cannot allocate that bounded scratch.

The multifloor compositor has a separate 4,096,000-pixel source-layer budget.
If a large world canvas exceeds it, extra floors are still drawn, but the
fallback omits their parallax scaling and depth effect. These limits prevent
deep zoom or large desktops from retaining very large software surfaces and
uploading them every frame.

## Compatibility with older configurations

`graphics/scale` is retired and ignored. There is no automatic translation of
old `off`, `2`, `4`, or other scale values. An older configuration without a
`graphics/zoom` key therefore receives the new default, 1.0, which matches
the old `graphics/scale: 1` window-relative canvas.

`graphics/render` is also retired as a whole-frame presentation setting. If
`graphics/smoothing` is absent, `render: sai` or `render: eagle` is inherited
as the corresponding world-only smoothing preference. Other legacy render
values become `smoothing: off`. An explicit `graphics/smoothing` value always
wins. The old action and engine enum values remain reserved, but the DISPLAY
menu no longer exposes whole-frame filtering.

## In-game controls

The DISPLAY screen has two live controls:

* **Zoom** cycles from 1.0 down to the deepest value supported by the current
  window and renderer, then wraps to 1.0. A canvas-size change immediately
  re-lays out active viewscreens.
* **Smooth** cycles `off -> sai -> eagle -> off`. It changes only the world
  presentation path, so no layout pass is needed.

Menus remain on the fixed UI canvas while settings are applied. Both values
are saved when OPTIONS exits, and RESTORE DEFAULTS returns them to 1.0 and
`off`.

## Layout and fixed-coordinate screens

Pane geometry begins in the stable gameplay-UI canvas
(`og::view_layout::compute_view_layout`) and its rectangle edges are projected
into World (`og::view_layout::project_view_layout`). One player uses the full canvas,
two players use side-by-side halves, three players use a full-height left pane
and a split right half in FULL mode, and four players use quadrants. Split
panes retain a two-pixel UI seam. This paired layout keeps world clipping
aligned with fixed-size chrome at fractional zoom. At 320x200 the formulas
reproduce the historical tables exactly.

The level editor still uses absolute 320x200 coordinates for its panel
chrome, so it temporarily pins the world canvas to 320x200 and restores the
window-relative zoom dimensions on exit. Its map follows the selected world
smoothing, while its minimap, authoring guides, picker previews, menus, and
controller cursor use the nearest gameplay-UI layer. Other fixed-coordinate
in-game screens temporarily switch to the UI canvas. When the world canvas is
separate, they seed that UI canvas from a nearest-scaled copy of the current
world frame and restore world routing when they close.

## Emscripten

Zoom and world smoothing use the same code path in native and Emscripten
builds. The page chooses an integral CSS-logical canvas size that fits the
browser viewport. Zoom 1.0 uses that logical size for World, while SDL's
high-density output keeps the physical WebGL backing at device-pixel
resolution. Lower zoom values enlarge only World; gameplay UI remains at its
classic-density aspect-matched size.

Browser resize and Fullscreen API events reapply the CSS-logical size. This
also repairs an SDL3 Emscripten fullscreen-exit race that can otherwise leave
the canvas backing at a stale viewport size after browser-reserved Escape.
Canvas focus is restored after leaving fullscreen. Native resolution and
display-mode controls remain unavailable in the browser.

Canvas and window dimensions are render-side state. The deterministic
simulation does not read them; `scripts/check_render_no_sim_writes.sh`
enforces that boundary.
