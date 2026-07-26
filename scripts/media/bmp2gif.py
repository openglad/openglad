#!/usr/bin/env python3
"""Assemble indexed BMP frames into an animated GIF -- no third-party deps.

OpenGlad screenshots are 8-bit palette BMPs (USE_BMP_SCREENSHOT=1), and GIF is
itself a palette format, so the conversion is direct: reuse the first frame's
palette as the GIF global color table and emit each frame's index bytes through
a standard GIF LZW encoder. Nothing here needs ffmpeg, PIL or numpy, none of
which are available in this environment.

Usage:
    bmp2gif.py out.gif frame0.bmp frame1.bmp ...      # explicit frame list
    bmp2gif.py out.gif --glob 'shots/frame*.bmp'      # sorted glob
Options:
    --delay N     centiseconds between frames (default 8 == 12.5 fps)
    --loop N      loop count, 0 = forever (default 0)
    --scale N     integer nearest-neighbour upscale (default 1)
    --skip N      keep every Nth frame (default 1 = keep all)
    --diff        store only what changed since the previous frame
"""

from __future__ import annotations

import argparse
import glob as globmod
import struct
import sys


class BmpFrame:
    """An 8-bit indexed BMP decoded to top-down index rows."""

    def __init__(self, width: int, height: int, palette: list[tuple[int, int, int]],
                 rows: list[bytes]):
        self.width = width
        self.height = height
        self.palette = palette
        self.rows = rows


def read_indexed_bmp(path: str) -> BmpFrame:
    with open(path, "rb") as handle:
        data = handle.read()

    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    header_size = struct.unpack_from("<I", data, 14)[0]
    if header_size < 40:
        raise ValueError(f"{path}: unsupported DIB header size {header_size}")

    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    palette_entries = struct.unpack_from("<I", data, 46)[0]

    if bpp != 8:
        raise ValueError(
            f"{path}: expected 8-bit indexed BMP, got {bpp}-bit. The game writes "
            f"8-bit palette BMPs; a truecolor capture needs a different path.")
    if compression != 0:
        raise ValueError(f"{path}: compressed BMPs (BI_RLE) are not supported")

    if palette_entries == 0:
        palette_entries = 256
    palette: list[tuple[int, int, int]] = []
    base = 14 + header_size
    for i in range(palette_entries):
        off = base + i * 4
        blue, green, red = data[off], data[off + 1], data[off + 2]
        palette.append((red, green, blue))
    while len(palette) < 256:
        palette.append((0, 0, 0))

    top_down = height < 0
    abs_height = abs(height)
    stride = ((width + 3) // 4) * 4  # BMP rows pad to 4 bytes

    rows: list[bytes] = []
    for y in range(abs_height):
        start = pixel_offset + y * stride
        row = data[start:start + width]
        if len(row) < width:
            raise ValueError(f"{path}: truncated pixel data at row {y}")
        rows.append(row)
    if not top_down:
        rows.reverse()  # BMP stores bottom-up by default

    return BmpFrame(width, abs_height, palette, rows)


def scale_rows(rows: list[bytes], factor: int) -> list[bytes]:
    if factor <= 1:
        return rows
    out: list[bytes] = []
    for row in rows:
        wide = bytes(b for value in row for _ in range(factor) for b in (value,))
        out.extend([wide] * factor)
    return out


class LzwEncoder:
    """GIF variable-width LZW, emitting sub-blocks of at most 255 bytes."""

    def __init__(self, min_code_size: int):
        self.min_code_size = min_code_size
        self.clear_code = 1 << min_code_size
        self.end_code = self.clear_code + 1
        self._bit_buffer = 0
        self._bit_count = 0
        self._bytes = bytearray()

    def _write_code(self, code: int, code_size: int) -> None:
        self._bit_buffer |= code << self._bit_count
        self._bit_count += code_size
        while self._bit_count >= 8:
            self._bytes.append(self._bit_buffer & 0xFF)
            self._bit_buffer >>= 8
            self._bit_count -= 8

    def encode(self, indices: bytes) -> bytes:
        code_size = self.min_code_size + 1
        next_code = self.end_code + 1
        table: dict[tuple[int, int], int] = {}

        self._write_code(self.clear_code, code_size)

        if not indices:
            self._write_code(self.end_code, code_size)
        else:
            prefix = indices[0]
            for value in indices[1:]:
                key = (prefix, value)
                found = table.get(key)
                if found is not None:
                    prefix = found
                    continue
                self._write_code(prefix, code_size)
                if next_code < 4096:
                    table[key] = next_code
                    next_code += 1
                    # Widen only after the code that filled the old width.
                    if next_code - 1 == (1 << code_size) and code_size < 12:
                        code_size += 1
                else:
                    # Table full: reset exactly like the reference encoder.
                    self._write_code(self.clear_code, code_size)
                    table = {}
                    next_code = self.end_code + 1
                    code_size = self.min_code_size + 1
                prefix = value
            self._write_code(prefix, code_size)
            self._write_code(self.end_code, code_size)

        if self._bit_count:
            self._bytes.append(self._bit_buffer & 0xFF)

        out = bytearray()
        stream = bytes(self._bytes)
        for start in range(0, len(stream), 255):
            chunk = stream[start:start + 255]
            out.append(len(chunk))
            out.extend(chunk)
        out.append(0)  # block terminator
        return bytes(out)


def changed_region(rows: list[bytes], previous: list[bytes]):
    """Bounding box of the pixels that differ, or None if the frames match."""
    min_x = min_y = max_x = max_y = -1
    for y, (current, old) in enumerate(zip(rows, previous)):
        if current == old:
            continue
        left = 0
        while current[left] == old[left]:
            left += 1
        right = len(current) - 1
        while current[right] == old[right]:
            right -= 1
        if min_y < 0:
            min_y = y
            min_x, max_x = left, right
        else:
            min_x = min(min_x, left)
            max_x = max(max_x, right)
        max_y = y
    if min_y < 0:
        return None
    return min_x, min_y, max_x, max_y


def _write_frame(out, x: int, y: int, width: int, height: int, pixels: bytes,
                 delay: int, transparent: int | None, dispose: int) -> None:
    packed = (dispose & 0x07) << 2
    if transparent is not None:
        packed |= 0x01
    out.write(b"\x21\xF9\x04")
    out.write(bytes((packed,)))
    out.write(struct.pack("<H", delay))
    out.write(bytes((transparent if transparent is not None else 0, 0)))
    # Image descriptor: no local color table, not interlaced.
    out.write(b"\x2C")
    out.write(struct.pack("<HHHHB", x, y, width, height, 0x00))
    out.write(bytes((8,)))  # LZW minimum code size for 8-bit indices
    out.write(LzwEncoder(8).encode(pixels))


def _write_delta_frame(out, rows: list[bytes], previous: list[bytes],
                       delay: int) -> None:
    """Emit only the changed sub-rectangle, unchanged pixels transparent.

    Gameplay frames are a static backdrop with a handful of moving sprites, so
    the redundancy between consecutive frames is the bulk of the file. Disposal
    method 1 ("do not dispose") leaves the previous frame on the canvas, and a
    transparent index inside the dirty rectangle keeps the parts of it that did
    not move. The transparent index is chosen per frame from the entries this
    frame does not paint -- the game draws a few dozen of the 256 palette
    entries per frame, so one is always free.
    """
    box = changed_region(rows, previous)
    if box is None:
        # Nothing moved: a 1x1 fully transparent patch is a pure delay.
        _write_frame(out, 0, 0, 1, 1, bytes((0,)), delay, 0, 1)
        return

    min_x, min_y, max_x, max_y = box
    painted = set()
    for y in range(min_y, max_y + 1):
        current = rows[y]
        old = previous[y]
        for x in range(min_x, max_x + 1):
            if current[x] != old[x]:
                painted.add(current[x])
    transparent = next((i for i in range(256) if i not in painted), None)

    chunks = []
    for y in range(min_y, max_y + 1):
        current = rows[y][min_x:max_x + 1]
        if transparent is None:
            chunks.append(current)
            continue
        old = previous[y][min_x:max_x + 1]
        chunks.append(bytes(c if c != o else transparent
                            for c, o in zip(current, old)))
    _write_frame(out, min_x, min_y, max_x - min_x + 1, max_y - min_y + 1,
                 b"".join(chunks), delay, transparent, 1)


def write_gif(path: str, frames: list[BmpFrame], delay: int, loop: int,
              scale: int, diff: bool = False) -> None:
    if not frames:
        raise ValueError("no frames to write")

    width = frames[0].width * scale
    height = frames[0].height * scale
    palette = frames[0].palette

    with open(path, "wb") as out:
        out.write(b"GIF89a")
        # Logical screen descriptor: global color table, 256 entries (2^8).
        out.write(struct.pack("<HHBBB", width, height, 0xF7, 0, 0))
        for red, green, blue in palette[:256]:
            out.write(bytes((red, green, blue)))

        # NETSCAPE 2.0 application extension carries the loop count.
        out.write(b"\x21\xFF\x0BNETSCAPE2.0\x03\x01")
        out.write(struct.pack("<H", loop))
        out.write(b"\x00")

        previous: list[bytes] | None = None
        for index, frame in enumerate(frames):
            if frame.width != frames[0].width or frame.height != frames[0].height:
                raise ValueError(
                    f"frame {index} is {frame.width}x{frame.height}, expected "
                    f"{frames[0].width}x{frames[0].height}")
            rows = scale_rows(frame.rows, scale)
            if diff and previous is not None:
                _write_delta_frame(out, rows, previous, delay)
            else:
                _write_frame(out, 0, 0, width, height, b"".join(rows), delay,
                             None, 1 if diff else 0)
            previous = rows

        out.write(b"\x3B")  # trailer


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("output")
    parser.add_argument("frames", nargs="*")
    parser.add_argument("--glob", dest="pattern")
    parser.add_argument("--delay", type=int, default=8)
    parser.add_argument("--loop", type=int, default=0)
    parser.add_argument("--scale", type=int, default=1)
    parser.add_argument("--skip", type=int, default=1)
    parser.add_argument("--diff", action="store_true",
                        help="store only the changed rectangle per frame")
    args = parser.parse_args(argv)

    paths = list(args.frames)
    if args.pattern:
        paths.extend(sorted(globmod.glob(args.pattern)))
    if args.skip > 1:
        paths = paths[::args.skip]
    if not paths:
        parser.error("no input frames (pass paths or --glob)")

    frames = [read_indexed_bmp(p) for p in paths]
    write_gif(args.output, frames, args.delay, args.loop, max(1, args.scale),
              diff=args.diff)
    print(f"{args.output}: {len(frames)} frames, "
          f"{frames[0].width * args.scale}x{frames[0].height * args.scale}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
