#!/usr/bin/env python3
"""Sprite/palette tooling.

Two modes:

* Default (one-shot migration): rewrite every sprite PNG as indexed-color and
  emit per-PNG Aseprite "Hash" JSON sidecars, then delete
  ``pix/sprite_manifest.txt``. Refuses to run after the manifest is gone.

* ``--emit-gpl <path>``: regenerate the GIMP palette artifact from
  ``src/resources/our_palette.cpp`` alone. Re-runnable, has no dependency on
  the (now-deleted) sprite manifest.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import struct
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PIX_DIR = REPO_ROOT / "pix"
MANIFEST_PATH = PIX_DIR / "sprite_manifest.txt"
PALETTE_SRC = REPO_ROOT / "src" / "resources" / "our_palette.cpp"
GLADIATOR_GLAD = REPO_ROOT / "builtin" / "org.openglad.gladiator.glad"

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


# ---------------------------------------------------------------------------
# Palette loading
# ---------------------------------------------------------------------------

def load_palette_6bit() -> bytes:
    """Parse the 768-byte 6-bit VGA palette literal out of our_palette.cpp."""
    text = PALETTE_SRC.read_text(encoding="utf-8")
    m = re.search(r"static\s+const\s+unsigned\s+char\s+data\s*\[\s*\]\s*=\s*\{",
                  text)
    if not m:
        raise RuntimeError(f"could not locate palette literal in {PALETTE_SRC}")
    start = m.end()
    end = text.find("};", start)
    if end < 0:
        raise RuntimeError(f"could not locate end of palette literal in {PALETTE_SRC}")
    body = text[start:end]
    nums = [int(t) for t in re.findall(r"-?\d+", body)]
    if len(nums) != 768:
        raise RuntimeError(
            f"expected 768 palette bytes in {PALETTE_SRC}, got {len(nums)}")
    for n in nums:
        if n < 0 or n > 63:
            raise RuntimeError(f"palette byte out of 6-bit range: {n}")
    return bytes(nums)


def palette_8bit_rgb(pal_6bit: bytes) -> bytes:
    """Convert 256x3 6-bit values to 256x3 8-bit values via (v*255)//63."""
    out = bytearray(768)
    for i, v in enumerate(pal_6bit):
        out[i] = (v * 255) // 63
    return bytes(out)


def trns_bytes() -> bytes:
    """tRNS: index 0 fully transparent, indices 1..255 fully opaque."""
    buf = bytearray(256)
    buf[0] = 0x00
    for i in range(1, 256):
        buf[i] = 0xFF
    return bytes(buf)


# ---------------------------------------------------------------------------
# Manifest
# ---------------------------------------------------------------------------

def load_manifest() -> dict[str, tuple[int, int, int]]:
    if not MANIFEST_PATH.exists():
        raise RuntimeError(
            f"{MANIFEST_PATH} not found — script is one-shot and refuses to "
            "rerun after migration.")
    out: dict[str, tuple[int, int, int]] = {}
    for raw in MANIFEST_PATH.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 4:
            raise RuntimeError(f"bad manifest line: {raw!r}")
        fname, frames, w, h = parts[0], int(parts[1]), int(parts[2]), int(parts[3])
        out[fname] = (frames, w, h)
    return out


# ---------------------------------------------------------------------------
# PNG read/write (no Pillow)
# ---------------------------------------------------------------------------

def _crc(chunk_type: bytes, data: bytes) -> int:
    return zlib.crc32(chunk_type + data) & 0xFFFFFFFF


def read_png_chunks(blob: bytes) -> tuple[dict, list[tuple[bytes, bytes]]]:
    if blob[:8] != PNG_SIGNATURE:
        raise RuntimeError("not a PNG (bad signature)")
    pos = 8
    chunks: list[tuple[bytes, bytes]] = []
    ihdr: dict | None = None
    while pos < len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        ctype = blob[pos + 4:pos + 8]
        cdata = blob[pos + 8:pos + 8 + length]
        # crc = blob[pos+8+length : pos+12+length]  # not validated; trusted input
        pos += 12 + length
        chunks.append((ctype, cdata))
        if ctype == b"IHDR":
            (w, h, bd, ct, cm, fm, im) = struct.unpack(">IIBBBBB", cdata)
            ihdr = dict(width=w, height=h, bit_depth=bd, color_type=ct,
                        compression=cm, filter=fm, interlace=im)
        if ctype == b"IEND":
            break
    if ihdr is None:
        raise RuntimeError("PNG missing IHDR")
    return ihdr, chunks


def decode_pixels(ihdr: dict, chunks: list[tuple[bytes, bytes]]) -> bytes:
    """Decode IDAT chunks into raw, unfiltered pixel indices.

    Supports: 8-bit grayscale (channels=1), 8-bit indexed (channels=1).
    These are the only modes we encounter in the existing pix/ tree. Pixel
    values are already palette indices in both cases (grayscale stored 0..255
    indices that we re-interpret as palette indices, since the original
    encoder dumped indices into LCT_GREY).
    """
    if ihdr["interlace"] != 0:
        raise RuntimeError("interlaced PNGs not supported")
    if ihdr["bit_depth"] != 8:
        raise RuntimeError(f"unsupported bit depth: {ihdr['bit_depth']}")
    if ihdr["color_type"] not in (0, 3):
        raise RuntimeError(f"unsupported color type: {ihdr['color_type']}")
    channels = 1
    bpp = channels  # bytes per pixel
    w = ihdr["width"]
    h = ihdr["height"]
    raw_compressed = b"".join(d for t, d in chunks if t == b"IDAT")
    raw = zlib.decompress(raw_compressed)
    expected = h * (1 + w * bpp)
    if len(raw) != expected:
        raise RuntimeError(f"unexpected raw size {len(raw)} != {expected}")

    out = bytearray(w * h * bpp)
    prev_row = bytearray(w * bpp)
    for y in range(h):
        row_start = y * (1 + w * bpp)
        ftype = raw[row_start]
        row = bytearray(raw[row_start + 1:row_start + 1 + w * bpp])
        if ftype == 0:  # None
            pass
        elif ftype == 1:  # Sub
            for i in range(bpp, len(row)):
                row[i] = (row[i] + row[i - bpp]) & 0xFF
        elif ftype == 2:  # Up
            for i in range(len(row)):
                row[i] = (row[i] + prev_row[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(len(row)):
                left = row[i - bpp] if i >= bpp else 0
                up = prev_row[i]
                row[i] = (row[i] + (left + up) // 2) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(len(row)):
                a = row[i - bpp] if i >= bpp else 0
                b = prev_row[i]
                c = prev_row[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa = abs(p - a)
                pb = abs(p - b)
                pc = abs(p - c)
                if pa <= pb and pa <= pc:
                    pred = a
                elif pb <= pc:
                    pred = b
                else:
                    pred = c
                row[i] = (row[i] + pred) & 0xFF
        else:
            raise RuntimeError(f"unsupported filter type: {ftype}")
        out[y * w * bpp:(y + 1) * w * bpp] = row
        prev_row = row
    return bytes(out)


def encode_indexed_png(width: int, height: int, indices: bytes,
                       palette_rgb: bytes, trns: bytes) -> bytes:
    """Build an indexed-color PNG (color type 3, bit depth 8) deterministically.

    Layout: IHDR, PLTE, tRNS, IDAT, IEND. IDAT is filter type 0 per row,
    zlib level 9.
    """
    if len(palette_rgb) != 768:
        raise RuntimeError("palette must be 256*3 bytes")
    if len(trns) != 256:
        raise RuntimeError("tRNS must be 256 bytes")
    if len(indices) != width * height:
        raise RuntimeError("pixel count mismatch")

    out = bytearray()
    out += PNG_SIGNATURE

    def emit(ctype: bytes, data: bytes) -> None:
        out.extend(struct.pack(">I", len(data)))
        out.extend(ctype)
        out.extend(data)
        out.extend(struct.pack(">I", _crc(ctype, data)))

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
    emit(b"IHDR", ihdr)
    emit(b"PLTE", palette_rgb)
    emit(b"tRNS", trns)

    # Filter type 0 per scanline.
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(indices[y * width:(y + 1) * width])
    compressed = zlib.compress(bytes(raw), level=9)
    emit(b"IDAT", compressed)
    emit(b"IEND", b"")
    return bytes(out)


def rewrite_png_indexed(blob: bytes, palette_rgb: bytes,
                        trns: bytes) -> tuple[bytes, int, int]:
    """Rewrite a PNG blob in indexed-color form, preserving pixel indices.

    Returns (new_blob, width, height).
    """
    ihdr, chunks = read_png_chunks(blob)
    pixels = decode_pixels(ihdr, chunks)
    new_blob = encode_indexed_png(ihdr["width"], ihdr["height"], pixels,
                                  palette_rgb, trns)
    return new_blob, ihdr["width"], ihdr["height"]


# ---------------------------------------------------------------------------
# Aseprite JSON sidecar
# ---------------------------------------------------------------------------

def write_aseprite_sidecar(png_path: Path, frames: int, frame_w: int,
                           frame_h: int) -> None:
    basename = png_path.stem  # e.g. "cleric"
    img_name = png_path.name  # e.g. "cleric.png"
    total_h = frame_h * frames
    frames_obj: dict[str, dict] = {}
    for k in range(frames):
        frames_obj[f"{basename} {k}.aseprite"] = {
            "frame": {"x": 0, "y": k * frame_h, "w": frame_w, "h": frame_h},
            "rotated": False,
            "trimmed": False,
            "spriteSourceSize": {"x": 0, "y": 0, "w": frame_w, "h": frame_h},
            "sourceSize": {"w": frame_w, "h": frame_h},
            "duration": 100,
        }
    payload = {
        "frames": frames_obj,
        "meta": {
            "app": "https://www.aseprite.org/",
            "version": "1.3.7",
            "image": img_name,
            "format": "I8",
            "size": {"w": frame_w, "h": total_h},
            "scale": "1",
            "frameTags": [],
            "layers": [{"name": "Layer 1", "opacity": 255, "blendMode": "normal"}],
            "slices": [],
        },
    }
    sidecar = png_path.with_suffix(".json")
    with sidecar.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2)
        f.write("\n")


# ---------------------------------------------------------------------------
# Archive repacking
# ---------------------------------------------------------------------------

def repack_gladiator_archive(palette_rgb: bytes, trns: bytes) -> None:
    if not GLADIATOR_GLAD.exists():
        raise RuntimeError(f"missing archive: {GLADIATOR_GLAD}")
    # Build the replacement archive next to the target so os.replace() can do
    # an atomic rename on the same filesystem.
    with tempfile.TemporaryDirectory(dir=str(GLADIATOR_GLAD.parent)) as tmp:
        tmpdir = Path(tmp)
        with zipfile.ZipFile(GLADIATOR_GLAD, "r") as zf:
            entries = zf.namelist()
            data: dict[str, bytes] = {}
            for name in entries:
                # ZipFile lists directory entries with a trailing slash; we drop
                # those to avoid emitting empty directory entries (the consumer
                # does not need them).
                if name.endswith("/"):
                    continue
                data[name] = zf.read(name)

        for name, blob in list(data.items()):
            is_pix_png = name.startswith("pix/") and name.endswith(".png")
            is_top_icon = name == "icon.png"
            if is_pix_png or is_top_icon:
                new_blob, _w, _h = rewrite_png_indexed(blob, palette_rgb, trns)
                data[name] = new_blob

        out_path = tmpdir / "out.glad"
        with zipfile.ZipFile(out_path, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=9) as zf:
            for name in sorted(data.keys()):
                # Use a fixed timestamp for determinism.
                zi = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
                zi.compress_type = zipfile.ZIP_DEFLATED
                zf.writestr(zi, data[name], compresslevel=9)
        out_path.replace(GLADIATOR_GLAD)




# ---------------------------------------------------------------------------
# Consistency pass
# ---------------------------------------------------------------------------

def verify_indexed_png(path: Path) -> None:
    blob = path.read_bytes()
    ihdr, chunks = read_png_chunks(blob)
    if ihdr["color_type"] != 3:
        raise RuntimeError(f"{path}: not indexed (color_type={ihdr['color_type']})")
    plte = next((d for t, d in chunks if t == b"PLTE"), None)
    trns = next((d for t, d in chunks if t == b"tRNS"), None)
    if plte is None or len(plte) != 768:
        raise RuntimeError(f"{path}: missing/invalid PLTE")
    if trns is None:
        raise RuntimeError(f"{path}: missing tRNS")


def verify_sidecar_roundtrip(path: Path) -> None:
    json.loads(path.read_text(encoding="utf-8"))


# ---------------------------------------------------------------------------
# GIMP palette (.gpl) emission
# ---------------------------------------------------------------------------

def format_gpl(palette_rgb: bytes) -> str:
    """Render a 256-entry GIMP palette from an 8-bit RGB triplet stream.

    Format: ``GIMP Palette`` header, ``Name:`` and ``Columns:`` lines, a
    ``#`` comment marker, then 256 lines of ``%3d %3d %3d\\tcolor_NNN``. The
    layout is fixed so the artifact is byte-deterministic across reruns.
    """
    if len(palette_rgb) != 768:
        raise RuntimeError("palette must be 256*3 bytes")
    lines = ["GIMP Palette", "Name: OpenGlad", "Columns: 16", "#"]
    for i in range(256):
        r = palette_rgb[i * 3 + 0]
        g = palette_rgb[i * 3 + 1]
        b = palette_rgb[i * 3 + 2]
        lines.append(f"{r:3d} {g:3d} {b:3d}\tcolor_{i:03d}")
    return "\n".join(lines) + "\n"


def emit_gpl(out_path: Path) -> None:
    palette_6 = load_palette_6bit()
    palette_rgb = palette_8bit_rgb(palette_6)
    out_path.write_text(format_gpl(palette_rgb), encoding="utf-8")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def run_migration() -> int:
    if not MANIFEST_PATH.exists():
        print(f"error: {MANIFEST_PATH} not present — migration already ran",
              file=sys.stderr)
        return 1

    palette_6 = load_palette_6bit()
    palette_rgb = palette_8bit_rgb(palette_6)
    trns = trns_bytes()
    manifest = load_manifest()

    # Enumerate pix/*.png explicitly (so the manifest itself is excluded).
    pix_pngs = sorted(Path(p) for p in glob.glob(str(PIX_DIR / "*.png")))
    pix_names = {p.name for p in pix_pngs}

    missing_in_manifest = sorted(pix_names - manifest.keys())
    missing_on_disk = sorted(manifest.keys() - pix_names)
    if missing_in_manifest:
        print(f"error: PNGs missing from manifest: {missing_in_manifest}",
              file=sys.stderr)
        return 2
    if missing_on_disk:
        print(f"error: manifest entries with no PNG: {missing_on_disk}",
              file=sys.stderr)
        return 2

    sidecar_count = 0
    for png in pix_pngs:
        frames, fw, fh = manifest[png.name]
        blob = png.read_bytes()
        new_blob, png_w, png_h = rewrite_png_indexed(blob, palette_rgb, trns)
        if png_w != fw:
            raise RuntimeError(
                f"{png}: manifest width {fw} != png width {png_w}")
        if png_h != fh * frames:
            raise RuntimeError(
                f"{png}: manifest implies height {fh * frames}, png has {png_h}")
        png.write_bytes(new_blob)
        if frames > 1:
            write_aseprite_sidecar(png, frames, fw, fh)
            sidecar_count += 1

    # Gladiator archive (only one in scope).
    repack_gladiator_archive(palette_rgb, trns)

    # Consistency pass.
    for png in pix_pngs:
        verify_indexed_png(png)
    for sc in sorted(PIX_DIR.glob("*.json")):
        verify_sidecar_roundtrip(sc)
    with zipfile.ZipFile(GLADIATOR_GLAD, "r") as zf:
        for name in zf.namelist():
            if name.endswith(".png"):
                blob = zf.read(name)
                # Use the same chunk verifier on a temp file path string only.
                ihdr, chunks = read_png_chunks(blob)
                if ihdr["color_type"] != 3:
                    raise RuntimeError(
                        f"gladiator.glad:{name} not indexed after repack")
                if not any(t == b"PLTE" for t, _ in chunks):
                    raise RuntimeError(f"gladiator.glad:{name} missing PLTE")
                if not any(t == b"tRNS" for t, _ in chunks):
                    raise RuntimeError(f"gladiator.glad:{name} missing tRNS")

    MANIFEST_PATH.unlink()

    print(f"[migrate] rewrote {len(pix_pngs)} pix/*.png as indexed-color")
    print(f"[migrate] emitted {sidecar_count} JSON sidecars (multi-frame)")
    print(f"[migrate] repacked {GLADIATOR_GLAD.name}")
    print(f"[migrate] deleted {MANIFEST_PATH.relative_to(REPO_ROOT)}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--emit-gpl",
        metavar="PATH",
        type=Path,
        default=None,
        help="Regenerate the GIMP palette from our_palette.cpp and exit.",
    )
    args = parser.parse_args(argv)

    if args.emit_gpl is not None:
        emit_gpl(args.emit_gpl)
        print(f"[gpl] wrote {args.emit_gpl}")
        return 0

    return run_migration()


if __name__ == "__main__":
    sys.exit(main())
