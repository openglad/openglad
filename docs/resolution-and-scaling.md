# Resolution, zoom, and smoothing

OpenGlad renders three logical layers:

* The **world canvas** contains maps, tiles, sprites, effects, and the level
  editor's map view. Its size follows the display aspect and `graphics/zoom`.
* The **UI canvas** contains menus, the picker, the intro, help screens,
  dialogs, and results. It remains 320x200 and always uses nearest-neighbor
  presentation.
* A transparent **gameplay UI layer** holds pane chrome, the HUD, radar,
  messages, touch controls, mini health bars, and damage or healing numbers.
  It retains classic pixel density, matches the zoom-1 world aspect, and is
  composited with nearest-neighbor sampling after the zoomed scenery.

At a 16:10 aspect, `zoom: 1.0` with smoothing off can share the historical
320x200 surface and texture at any window size. Other aspects expand the
needed axis instead of stretching it; zoomed-out worlds use a separate canvas.

## Graphics settings

| Key | Values | Default | Effect |
| --- | --- | --- | --- |
| `fullscreen` | `off`, `borderless`, `exclusive` | `borderless` (`on`) | Native display mode. On Emscripten, use the page's fullscreen button. |
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

`zoom: 1.0` restores the shipped/default behavior on `master`: a 320x200
world at 16:10, enlarged to the display. Unlike master, other aspects expand
one axis (approximately 356x200 at 16:9 and 320x240 at 4:3), so sprites
keep the same physical scale without being stretched.

For zoom value `z`, the dimensions are derived as follows:

```text
base canvas  = 320x200, with one axis expanded to the display aspect
world width  = base width / z, rounded down to a multiple of 4
world height = base height / z
```

Integer division rounds down. The four-pixel width alignment is required by
the software scalers and partial-present paths. For the default 640x400
window, the selector produces:

| Zoom | World canvas |
| ---: | ---: |
| 1.0 | 320x200 |
| 0.9 | 352x222 |
| 0.8 | 400x250 |
| 0.7 | 456x285 |
| 0.6 | 532x333 |
| 0.5 | 640x400 |
| 0.4 | 800x500 |
| 0.3 | 1064x666 |
| 0.2 | 1600x1000 |
| 0.1 | 3200x2000 |

The available lower limit depends on aspect ratio, the renderer's maximum
texture dimension, and an 8,388,608-pixel world-canvas budget. The budget
bounds the ARGB8888 surface and streaming texture to roughly 64 MiB combined.
Ordinary 16:10, 16:9, and 4:3 displays offer the complete 0.1 through 1.0
range regardless of physical resolution. Very wide or tall aspects may omit
the deepest steps; for example:

| Display aspect example | Deepest zoom |
| ---: | ---: |
| 320x200 | 0.1 |
| 1920x1080 | 0.1 |
| 1024x768 | 0.1 |
| 3440x1440 | 0.2 |
| 720x1280 | 0.2 |

Zoom 1.0 remains the classic-density, aspect-expanded baseline even when the
pixel budget would exclude a deeper step. A separate 16,777,216-pixel
absolute ceiling uniformly caps hostile extreme-aspect baselines before
allocation while retaining their aspect ratio. The selector wraps at the
safe lower limit. A configuration that requests an unavailable step is
raised to the deepest supported value, and the effective value is written
back to the live config.
Canvas replacement is transactional: if SDL still rejects an allocation, the
previous working canvas remains active.

Resizing the window or completing a fullscreen transition recomputes the
world canvas and viewscreen layout. Canvas rendering cost therefore follows
the display aspect and selected zoom, not its raw pixel count.

Gameplay chrome does not inherit the world resolution or zoom. Its baseline
starts at 320x200 and expands one axis to match the zoom-1 world aspect:
16:10 uses 320x200, 16:9 uses approximately 356x200, and 4:3 uses 320x240.
That keeps bitmap text, HUD controls, sprites, and tiles at their historical
readable scale when zoom is 1.0.

The one chrome element that does track zoom is the mini health bar's
footprint. Its anchor and its width are both projected from the world pane
into the gameplay-UI pane, so the bar stays exactly as wide as the sprite it
belongs to at every zoom level. Its 1-pixel stroke and 1-pixel black outline
stay at gameplay-UI density: the UI-to-world ratio never exceeds 1, so a
scaled stroke would round back to 1 anyway, and a sub-pixel stroke would
vanish at deep zoom. Score-panel bars, the radar, messages, and damage numbers
remain fully pinned.

## Aspect ratio and pointer mapping

Every active canvas is aspect-fitted inside the overscan viewport. An
aspect-derived world canvas normally fills nearly all of it; the fixed menu UI
is centered with black pillarbox or letterbox bars. Gameplay UI is fitted from
its own stable dimensions, so fractional World rounding cannot squeeze HUD or
text pixels into a slightly different aspect.

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
source canvas. Common display aspects can use smart smoothing through zoom
0.3; deeper zooms exceed the scratch budget. When the scratch is too large or
allocation fails, the selected preference is retained but that frame is
presented nearest.
The DISPLAY label adds `N/A` to a selected SAI/Eagle mode while the current
canvas or renderer cannot allocate that bounded scratch.

The multifloor compositor has a separate 4,096,000-pixel source-layer budget.
If a large world canvas exceeds it, extra floors are still drawn, but the
fallback omits their parallax scaling and depth effect. These limits prevent
deep zoom or extreme display aspects from retaining very large software
surfaces and uploading them every frame.

## Compatibility with older configurations

`graphics/scale` is retired and ignored. There is no automatic translation of
old `off`, `2`, `4`, or other scale values. An older configuration without a
`graphics/zoom` key therefore receives the new default, 1.0, which matches
master's shipped/default classic-density (`scale: off`) world size.

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
are saved when OPTIONS exits, and RESTORE SETTINGS returns them to 1.0 and
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
aspect-relative zoom dimensions on exit. Its map follows the selected world
smoothing, while its minimap, authoring guides, picker previews, menus, and
controller cursor use the nearest gameplay-UI layer. Other fixed-coordinate
in-game screens temporarily switch to the UI canvas. When the world canvas is
separate, they seed that UI canvas from a nearest-scaled copy of the current
world frame and restore world routing when they close.

## Emscripten

Zoom and world smoothing use the same code path in native and Emscripten
builds. The outer canvas fills the live visual viewport, including tall phone
portrait viewports. SDL's high-density output keeps the physical WebGL backing
at device-pixel resolution. Intro and menu screens remain a centered,
aspect-fitted 320x200 surface inside that canvas; gameplay instead expands the
classic-density World and gameplay-UI canvases along the needed axis. Thus a
portrait phone reveals more world vertically without stretching sprites or
shrinking the bitmap HUD into a landscape strip. Lower zoom values enlarge
only World; gameplay UI remains at its classic-density aspect-matched size.

Browser resize and Fullscreen API events reapply the CSS-logical size. This
also repairs an SDL3 Emscripten fullscreen-exit race that can otherwise leave
the canvas backing at a stale viewport size after browser-reserved Escape.
Canvas focus is restored after leaving fullscreen. Native resolution and
display-mode controls remain unavailable in the browser. The on-page
fullscreen button enters whole-app fullscreen from an explicit user gesture,
so the canvas, touch controls, and text-entry field all remain available;
browser-reserved Escape exits it. Web builds never bind game actions to
Escape: Backspace is the universal back/cancel/menu key (remapped at the SDL
event source, and disabled while a text field is active so Backspace still
deletes characters — see `include/openglad/interface/web_back_key.h`), so
leaving fullscreen with Escape has no in-game side effect and no Keyboard
Lock is required.

Canvas and window dimensions are render-side state. The deterministic
simulation does not read them; `scripts/check_render_no_sim_writes.sh`
enforces that boundary.
