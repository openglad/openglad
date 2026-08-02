# Sprite format and Aseprite workflow

OpenGlad sprites are **8-bit indexed-color PNGs** that share a single 256-entry palette baked into the engine. This page is the artist-facing guide for editing sprites in [Aseprite](https://www.aseprite.org/) and shipping them back to the repo.

## Why indexed-color PNGs

The original DOS game used a 256-entry VGA palette and stored every pixel as an index into that palette. The C++ port keeps that contract: the renderer, palette cycling, team-color recoloring, and translucency tables all operate on indices, not RGB. Storing sprites as indexed PNGs (color type 3, bit depth 8, with a `PLTE` and `tRNS`) means:

- The PNG byte at `(x, y)` *is* the palette index. No quantization on load.
- Index `0` is the transparent magic color, marked via `tRNS`.
- The embedded `PLTE` is verified against `our_pal_lookup` at load time. Mismatches are rejected — see [Palette contract](#palette-contract) below.

## Files an artist touches

```
pix/openglad.gpl        # GIMP palette, mirror of our_pal_lookup
pix/<basename>.png      # indexed-color sprite sheet (vertical strip of frames)
pix/<basename>.json     # Aseprite "Hash" JSON sidecar (only for multi-frame sprites)
```

`<basename>.json` is the Aseprite sidecar produced by *Export Sprite Sheet → Output → JSON Data → Hash*. The engine reads it to learn frame count, frame width, and frame height for animated sprites. Single-frame sprites are just a bare PNG.

## Loading the engine palette in Aseprite

1. *Palette → Presets → Load Palette*.
2. Browse to `pix/openglad.gpl` and open it.
3. The 256 colors appear in the palette panel in the same order as `our_pal_lookup`.

Keep `openglad.gpl` loaded as your active palette while editing. Re-running `python3 scripts/migrate_pix_to_aseprite.py --emit-gpl pix/openglad.gpl` regenerates the file from `src/resources/our_palette.cpp`, so the artifact stays in sync with the engine palette.

## Opening an existing sprite

### Single-frame sprite

Drag the PNG into Aseprite. It opens automatically in **Indexed** color mode because the file already has a `PLTE` chunk. The palette panel shows the OpenGlad palette.

### Multi-frame sprite (with a JSON sidecar)

Use *File → Import Sprite Sheet*:

1. *Source*: select `pix/<basename>.png`.
2. *Type*: choose **JSON Data**, then point at `pix/<basename>.json`.
3. Aseprite slices the vertical strip into individual frames using the rectangles in the sidecar.

Each frame in the sidecar is keyed `"<basename> <N>.aseprite"` where `N` starts at 0 (frame 0 is the topmost row of pixels). Frames are stacked vertically; there is no padding between them.

## Saving edits back

Save through *File → Export Sprite Sheet*:

- *Layout → Sheet type*: **Vertical Strip**.
- *Output → Output File*: `pix/<basename>.png`.
- *Output → JSON Data*: enable, set to **Hash**, write to `pix/<basename>.json`.
- *Sprite Sheet → Color Mode*: **Indexed**.

Do **not** use *File → Save As* with a non-indexed mode — that re-encodes pixels as RGB and breaks the palette contract.

For a single-frame sprite, just *Export Sprite Sheet* with one frame and skip the JSON output.

## Palette contract

Strict rules. Every loader checks them and refuses sprites that violate them:

- **256 entries.** No more, no fewer.
- **Order is fixed.** Do not reorder palette entries; index `N` must keep the same color it has in `our_pal_lookup`.
- **No new colors.** Pick from the existing 256. The engine validates the embedded `PLTE` against `our_pal_lookup` (within the ±1 tolerance from the 6-bit→8-bit conversion) and rejects mismatches.
- **Index 0 is transparent.** Anything you want to be see-through must use index 0.

If you need a color the palette does not have, that is a palette change, not a sprite change. Open a separate discussion before editing `our_palette.cpp`.

## Frame layout contract

- **Vertical strip.** Frames are stacked top-to-bottom in the PNG.
- **Frame 0 is the topmost frame.** Frame `k` lives at `y = k * frame_height`.
- **Uniform frame size.** All frames in a sprite share the same width and height; the JSON sidecar records that size once per frame.
- **No padding** between frames.

## Regenerating the palette artifact

```sh
python3 scripts/migrate_pix_to_aseprite.py --emit-gpl pix/openglad.gpl
```

That command reads only `src/resources/our_palette.cpp::data[]`, so it is safe to re-run any time. The committed `pix/openglad.gpl` must be byte-identical to what this command produces; the regression test `PaletteExport.gpl_matches_runtime_palette` enforces the same equivalence at runtime.

## See also

- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) — module layout and the resource pipeline.
- `src/resources/our_palette.cpp` — the canonical 6-bit VGA palette.
- `scripts/migrate_pix_to_aseprite.py` — regenerates `pix/openglad.gpl` from `src/resources/our_palette.cpp` (`--emit-gpl`).
