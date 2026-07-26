#!/usr/bin/env python3
"""Round-trip test for bmp2gif.py: synthesize indexed BMPs, encode to GIF, then
decode the GIF back with an independent LZW decoder and compare every pixel.

Run: python3 scripts/media/test_bmp2gif.py
Exits non-zero on any mismatch. No third-party dependencies (there is no PIL or
ffmpeg in this environment, which is why bmp2gif exists in the first place).
"""

from __future__ import annotations

import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmp2gif  # noqa: E402


def write_indexed_bmp(path: str, width: int, height: int,
                      rows_top_down: list[bytes],
                      palette: list[tuple[int, int, int]],
                      bottom_up: bool = True) -> None:
    """Write an 8-bit indexed BMP in the same shape SDL_SaveBMP produces."""
    stride = ((width + 3) // 4) * 4
    pad = stride - width
    rows = list(rows_top_down)
    if bottom_up:
        rows = rows[::-1]
    pixel_data = b"".join(row + b"\x00" * pad for row in rows)

    palette_bytes = b"".join(
        bytes((b, g, r, 0)) for (r, g, b) in (palette + [(0, 0, 0)] * 256)[:256])

    header_size = 40
    pixel_offset = 14 + header_size + len(palette_bytes)
    file_size = pixel_offset + len(pixel_data)

    with open(path, "wb") as out:
        out.write(b"BM")
        out.write(struct.pack("<IHHI", file_size, 0, 0, pixel_offset))
        out.write(struct.pack("<IiiHHIIiiII", header_size, width,
                              height if bottom_up else -height,
                              1, 8, 0, len(pixel_data), 0, 0, 256, 0))
        out.write(palette_bytes)
        out.write(pixel_data)


def decode_gif(path: str):
    """Minimal GIF89a decoder -> (width, height, palette, [frame index rows]).

    Deliberately written from the spec rather than sharing code with the
    encoder, so an encoder bug cannot cancel itself out.
    """
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:6] not in (b"GIF89a", b"GIF87a"):
        raise AssertionError("bad GIF signature")

    width, height, packed = struct.unpack_from("<HHB", data, 6)
    pos = 13
    palette = []
    if packed & 0x80:
        count = 2 << (packed & 0x07)
        for i in range(count):
            off = pos + i * 3
            palette.append((data[off], data[off + 1], data[off + 2]))
        pos += count * 3

    frames = []
    while pos < len(data):
        block = data[pos]
        if block == 0x3B:  # trailer
            break
        if block == 0x21:  # extension
            pos += 2  # skip label
            while data[pos]:
                pos += data[pos] + 1
            pos += 1
            continue
        if block != 0x2C:
            raise AssertionError(f"unexpected block 0x{block:02X} at {pos}")

        fx, fy, fw, fh, fpacked = struct.unpack_from("<HHHHB", data, pos + 1)
        pos += 10
        if fpacked & 0x80:
            pos += 3 * (2 << (fpacked & 0x07))
        if fpacked & 0x40:
            raise AssertionError("interlaced frames not produced by bmp2gif")

        min_code_size = data[pos]
        pos += 1
        chunks = bytearray()
        while data[pos]:
            size = data[pos]
            chunks.extend(data[pos + 1:pos + 1 + size])
            pos += size + 1
        pos += 1

        indices = lzw_decode(bytes(chunks), min_code_size, fw * fh)
        rows = [bytes(indices[y * fw:(y + 1) * fw]) for y in range(fh)]
        frames.append(((fx, fy, fw, fh), rows))

    return width, height, palette, frames


def lzw_decode(stream: bytes, min_code_size: int, expected: int) -> bytearray:
    clear_code = 1 << min_code_size
    end_code = clear_code + 1

    def reset():
        table = {i: bytes((i,)) for i in range(clear_code)}
        return table, end_code + 1, min_code_size + 1

    table, next_code, code_size = reset()
    out = bytearray()
    bit_pos = 0
    total_bits = len(stream) * 8
    previous = None

    while bit_pos + code_size <= total_bits:
        byte_index = bit_pos // 8
        chunk = int.from_bytes(stream[byte_index:byte_index + 3].ljust(3, b"\x00"),
                               "little")
        code = (chunk >> (bit_pos % 8)) & ((1 << code_size) - 1)
        bit_pos += code_size

        if code == clear_code:
            table, next_code, code_size = reset()
            previous = None
            continue
        if code == end_code:
            break

        if code in table:
            entry = table[code]
        elif previous is not None:
            entry = previous + previous[:1]
        else:
            raise AssertionError(f"undefined first code {code}")

        out.extend(entry)
        if previous is not None and next_code < 4096:
            table[next_code] = previous + entry[:1]
            next_code += 1
            if next_code == (1 << code_size) and code_size < 12:
                code_size += 1
        previous = entry

    if len(out) != expected:
        raise AssertionError(f"decoded {len(out)} pixels, expected {expected}")
    return out


def make_palette():
    # A recognisable ramp plus a few saturated entries, like a game palette.
    palette = [(i, i, i) for i in range(256)]
    palette[1] = (255, 0, 0)
    palette[2] = (0, 255, 0)
    palette[3] = (0, 0, 255)
    return palette


def case_roundtrip(tmp: str, width: int, height: int, frame_count: int,
                   bottom_up: bool, scale: int, label: str) -> None:
    palette = make_palette()
    expected_frames = []
    paths = []
    for f in range(frame_count):
        rows = []
        for y in range(height):
            # Content that defeats trivial run-length shortcuts: a moving
            # diagonal over a gradient, plus a solid band (LZW-friendly) so
            # both compressible and incompressible regions are exercised.
            row = bytearray()
            for x in range(width):
                if y == (x + f * 3) % height:
                    row.append(1)
                elif y < 3:
                    row.append(7)
                else:
                    row.append((x * 5 + y * 11 + f * 17) % 256)
            rows.append(bytes(row))
        expected_frames.append(rows)
        path = os.path.join(tmp, f"{label}_frame{f:03d}.bmp")
        write_indexed_bmp(path, width, height, rows, palette, bottom_up=bottom_up)
        paths.append(path)

    gif_path = os.path.join(tmp, f"{label}.gif")
    rc = bmp2gif.main([gif_path, *paths, "--delay", "5", "--scale", str(scale)])
    assert rc == 0, "bmp2gif returned non-zero"

    gw, gh, gpal, frames = decode_gif(gif_path)
    assert gw == width * scale and gh == height * scale, \
        f"{label}: gif is {gw}x{gh}, expected {width * scale}x{height * scale}"
    assert len(frames) == frame_count, \
        f"{label}: decoded {len(frames)} frames, expected {frame_count}"
    assert gpal[1] == (255, 0, 0) and gpal[3] == (0, 0, 255), \
        f"{label}: palette not preserved: {gpal[1]} {gpal[3]}"

    for f, (_, rows) in enumerate(frames):
        for y in range(height * scale):
            want = expected_frames[f][y // scale]
            got = rows[y]
            if scale > 1:
                want = bytes(v for v in want for _ in range(scale))
            assert got == want, \
                f"{label}: frame {f} row {y} mismatch\n  want {want[:16].hex()}\n  got  {got[:16].hex()}"
    print(f"  ok {label}: {frame_count} frames {gw}x{gh}, pixels identical")


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        print("bmp2gif round-trip tests")
        # Odd width forces BMP row padding (stride != width).
        case_roundtrip(tmp, 37, 21, 3, True, 1, "odd_bottom_up")
        # Top-down BMPs (negative height) must not come out flipped.
        case_roundtrip(tmp, 32, 16, 2, False, 1, "top_down")
        # Integer upscale.
        case_roundtrip(tmp, 20, 12, 2, True, 3, "scaled")
        # Wide frame: forces the LZW code width to grow past 9 bits and the
        # 255-byte sub-block boundary to be crossed many times.
        case_roundtrip(tmp, 320, 200, 2, True, 1, "full_screen")
        print("all bmp2gif round-trip tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
