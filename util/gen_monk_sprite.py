#!/usr/bin/env python3
"""Generate monk.pix sprite from archer.pix for OpenGlad.

Takes the archer sprite as a base, removes the bow/arrow/quiver,
and replaces the hood with a bald head.

Palette:
  - 0x80/0x82: bald dome skin (highlight / medium)
  - 0x84: face skin (unchanged from archer)
  - 0xFF-0xFA: team-colored gi (same range as archer)
  - 0xF1/0xF3/0xF6: neutral clothing
  - Bow (0x84 outside face), quiver (0x8B on back), arrow: removed
  - Hood (0xFF on head): replaced with skin tones
"""
import sys
import os

WIDTH = 16
HEIGHT = 16
NUM_FRAMES = 24

# Skin tones for bald head
SKIN_HI = 0x80   # dome highlight
SKIN_MD = 0x82   # dome medium
SKIN_FK = 0x84   # face (same as archer)

def read_pix(filename):
    """Read a .pix file, return list of frames (each frame = list of rows, each row = list of ints)."""
    with open(filename, 'rb') as f:
        data = f.read()
    nf = data[0]; w = data[1]; h = data[2]
    frames = []
    off = 3
    for fi in range(nf):
        frame = []
        for r in range(h):
            row = []
            for c in range(w):
                row.append(data[off])
                off += 1
            frame.append(row)
        frames.append(frame)
    return frames

def write_pix(filename, frames):
    """Write a .pix file from frame data."""
    nf = len(frames)
    h = len(frames[0])
    w = len(frames[0][0])
    assert nf == NUM_FRAMES, f"Expected {NUM_FRAMES} frames, got {nf}"
    assert w == WIDTH, f"Expected width {WIDTH}, got {w}"
    assert h == HEIGHT, f"Expected height {HEIGHT}, got {h}"
    data = bytearray()
    data.append(nf)
    data.append(w)
    data.append(h)
    for frame in frames:
        for row in frame:
            for pixel in row:
                data.append(pixel & 0xFF)
    with open(filename, 'wb') as f:
        f.write(data)
    print(f"Wrote {filename}: {nf} frames, {w}x{h}, {len(data)} bytes")

def preview_frame(frame, label=""):
    """Print a text preview of a frame."""
    if label:
        print(f"=== {label} ===")
    print("     " + " ".join(f"{c:2d}" for c in range(WIDTH)))
    for r, row in enumerate(frame):
        line = f"{r:2d} | "
        for pixel in row:
            if pixel == 0:
                line += " . "
            else:
                line += f"{pixel:02x} "
        print(line)
    print()


# =====================================================================
# Per-frame bow/arrow/quiver removal masks + hood-to-bald replacement
# =====================================================================
# For each frame, we define:
#   - pixels to CLEAR (set to 0): bow, arrow, bowstring
#   - pixels to CHANGE: hood ff -> skin tones, quiver 8b -> gi colors
#
# Format: list of (row, col, new_value) tuples.
# new_value=0 means "remove" (transparent), otherwise it's the replacement.

def make_modifications():
    """Return a dict of frame_index -> list of (row, col, new_value)."""
    mods = {}

    # --- Frame 0: Down base ---
    # Bow: 84 at (2,11), (3,11), (3,12), (4,10), (9,3), (9,4), (9,5),
    #      (10,2), (11,1), (11,3), (12,1), (12,2), (13,1)
    # Bowstring: fe at (3,8)
    # Hood: ff at (4,7)(4,8)(4,9), (5,5)(5,11), (6,5)(6,11), (7,5)(7,11),
    #        (8,5)(8,11), (9,13) <- that's f3 actually
    # The ff in rows 5-8 cols 5,11 are team-color gi borders - keep those.
    # Hood top: row 4 cols 7-9 are ff = hood peak
    mods[0] = [
        # Remove bow + arrow
        (2, 11, 0), (3, 11, 0), (3, 12, 0), (3, 8, 0),  # arrow + bowstring
        (4, 10, 0),  # bow tip near hood
        (9, 3, SKIN_FK), (9, 4, SKIN_FK),  (9, 5, 0),  # left hand - keep hands as skin
        (10, 2, 0),  # bow
        (11, 1, 0), (11, 3, SKIN_FK),  # bow + hand
        (12, 1, 0), (12, 2, 0),  # bow
        (13, 1, 0),  # bow
        # Hood -> bald head
        (4, 7, SKIN_MD), (4, 8, SKIN_HI), (4, 9, SKIN_MD),
    ]

    # --- Frame 1: Up base ---
    # Bow: 84 at (3,14), (4,3), (4,13), (4,14), (5,2), (5,3), (5,12), (5,14),
    #      (6,4), (6,14), (7,5)
    # Quiver: 8b at (8,7), (9,6)(9,7)(9,8), (10,7)(10,8)(10,9), (11,8)(11,9)
    # Hood: ff at (4,7)(4,8)(4,9), (5,6)(5,7)(5,8)(5,9)(5,10)
    mods[1] = [
        # Remove bow
        (3, 14, 0),
        (4, 3, 0), (4, 13, 0), (4, 14, 0),
        (5, 2, 0), (5, 3, 0), (5, 12, 0), (5, 14, 0),
        (6, 4, 0), (6, 14, 0),
        (7, 5, 0),
        # Quiver -> gi back colors
        (8, 7, 0xFD), (9, 6, 0xFD), (9, 7, 0xFD), (9, 8, 0xFD),
        (10, 7, 0xFD), (10, 8, 0xFD), (10, 9, 0xFD),
        (11, 8, 0xFD), (11, 9, 0xFD),
        # Hood -> bald head (back of head, so darker/uniform)
        (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD), (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    # --- Frame 2: Right base ---
    # Bow: 84 at (2,3), (3,3)(3,8), (4,7), (6,11)(6,12)(6,13),
    #      (7,10)(7,13), (8,9)(8,12), (9,8)(9,11), (10,10)(10,11)(not exist),
    #      (11,8)(11,9)
    # Quiver: 8b at (6,3), (7,3), (8,3)
    # Hood: ff at (3,4)(3,5)(3,6)(3,7), (4,3)(4,4)(4,5)(4,6),
    #        (5,3)(5,4)(5,5)(5,6)
    mods[2] = [
        # Remove bow + arrow
        (2, 3, 0),
        (3, 8, 0),  # arrow tip (3,3 is part of hood area, handle below)
        (4, 7, SKIN_FK),  # face edge where arrow meets head
        (6, 11, 0), (6, 12, 0), (6, 13, 0),  # bow right
        (7, 10, 0), (7, 13, 0),  # bow
        (8, 9, 0), (8, 12, 0),  # bow
        (9, 8, SKIN_FK), (9, 11, 0),  # hand + bow
        (10, 10, 0), (10, 11, 0),  # bow (these are f1 f1 actually)
        (11, 8, 0), (11, 9, 0),  # bow bottom
        # Remove quiver
        (6, 3, 0), (7, 3, 0), (8, 3, 0),
        # Hood -> bald head (profile right)
        (3, 3, 0), (3, 4, SKIN_MD), (3, 5, SKIN_HI), (3, 6, SKIN_HI), (3, 7, SKIN_HI),
        (4, 3, SKIN_MD), (4, 4, SKIN_HI), (4, 5, SKIN_HI), (4, 6, SKIN_HI),
        (5, 3, SKIN_MD), (5, 4, SKIN_HI), (5, 5, SKIN_HI), (5, 6, SKIN_HI),
    ]

    # --- Frame 3: Left base (mirror of right) ---
    # Bow: 84 at (3,2)(3,3)(3,10), (4,1)(4,4)(4,12),
    #      (5,2)(5,11), (6,3), (7,4), (8,5)
    # Quiver: 8b at (7,11), (8,11), (9,11)
    # Hood: ff at (3,7)(3,8)(3,9), (4,7)(4,8)(4,9)(4,10),
    #        (5,8)(5,9)(5,10)
    mods[3] = [
        # Remove bow
        (3, 2, 0), (3, 3, 0), (3, 10, 0),  # (3,10 is face/hood edge)
        (4, 1, 0), (4, 4, 0), (4, 12, 0),
        (5, 2, 0), (5, 11, 0),
        (6, 3, 0),
        (7, 4, 0),
        (8, 5, SKIN_FK),  # hand
        # Remove quiver
        (7, 11, 0), (8, 11, 0), (9, 11, 0),
        # Hood -> bald (profile left - mirror of right)
        (3, 7, SKIN_HI), (3, 8, SKIN_HI), (3, 9, SKIN_MD),
        (4, 7, SKIN_HI), (4, 8, SKIN_HI), (4, 9, SKIN_HI), (4, 10, SKIN_MD),
        (5, 8, SKIN_HI), (5, 9, SKIN_HI), (5, 10, SKIN_MD),
    ]

    # --- Frame 4: Down walk1 ---
    # Same head as frame 0, bow shifts during walk
    # Bow: (2,11), (3,11)(3,12), (3,8)=fe, (4,10),
    #       (11,2)(11,3)(11,4)(11,5), (12,1)(12,6), (13,1)(13,2)(13,3)(13,5)(13,6),
    #       wait let me re-check...
    # Row 11: 84 84 84 84 at cols 2,3,4,5 -> but (11,2)=84,(11,3)=84,(11,4)=84,(11,5)=84
    # Row 12: (12,1)=84, (12,6)=84
    # Row 13: (13,1)=84,(13,2)=84,(13,3)=84,(13,5)=84,(13,6)=84
    mods[4] = [
        # Remove arrow
        (2, 11, 0), (3, 11, 0), (3, 12, 0), (3, 8, 0),
        (4, 10, 0),
        # Remove bow (held lower during walk)
        (11, 2, 0), (11, 3, 0), (11, 4, 0), (11, 5, 0),
        (12, 1, 0), (12, 6, 0),
        (13, 1, 0), (13, 2, 0), (13, 3, 0), (13, 5, 0), (13, 6, 0),
        # Hood -> bald
        (4, 7, SKIN_MD), (4, 8, SKIN_HI), (4, 9, SKIN_MD),
    ]

    # --- Frame 5: Up walk1 ---
    # Bow on right side: (8,14)=84, (9,14)=84, (10,14)=84, (11,14)=84,
    #   (12,12)=84, (12,14)=84, (13,11)=84,(13,12)=84,(13,13)=84, (14,11)=84
    # Also (4,3)=84, (5,2)=84,(5,3)=84
    mods[5] = [
        # Remove bow
        (4, 3, 0), (5, 2, 0), (5, 3, 0),
        (8, 14, 0), (9, 14, 0), (10, 14, 0), (11, 14, 0),
        (12, 12, 0), (12, 14, 0),
        (13, 11, 0), (13, 12, 0), (13, 13, 0),
        (14, 11, 0),
        # Quiver
        (8, 7, 0xFD), (9, 6, 0xFD), (9, 7, 0xFD), (9, 8, 0xFD),
        (10, 7, 0xFD), (10, 8, 0xFD), (10, 9, 0xFD),
        (11, 8, 0xFD), (11, 9, 0xFD),
        # Hood -> bald
        (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD), (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    # --- Frame 6: Right walk1 ---
    # Bow: (2,3)=84, (3,3)=84,(3,8)=84,
    #      (8,12)=84, (9,11)=84,(9,12)=84, (10,9)=84,(10,10)=84,(10,12)=84,
    #      (11,11)=84, (12,7)=84,(12,10)=84, (13,6)=84,(13,7)=84,(13,8)=84,(13,9)=84
    # Quiver: (6,3)=8b, (7,3)=8b, (8,3)=8b
    mods[6] = [
        # Remove bow
        (2, 3, 0), (3, 8, 0),
        (8, 12, 0),
        (9, 11, 0), (9, 12, 0),
        (10, 9, SKIN_FK), (10, 10, 0), (10, 12, 0),
        (11, 11, 0),
        (12, 7, SKIN_FK), (12, 10, 0),
        (13, 6, 0), (13, 7, 0), (13, 8, 0), (13, 9, 0),
        # Quiver
        (6, 3, 0), (7, 3, 0), (8, 3, 0),
        # Hood -> bald
        (3, 3, 0), (3, 4, SKIN_MD), (3, 5, SKIN_HI), (3, 6, SKIN_HI), (3, 7, SKIN_HI),
        (4, 3, SKIN_MD), (4, 4, SKIN_HI), (4, 5, SKIN_HI), (4, 6, SKIN_HI),
        (5, 3, SKIN_MD), (5, 4, SKIN_HI), (5, 5, SKIN_HI), (5, 6, SKIN_HI),
    ]

    # --- Frame 7: Left walk1 ---
    # Bow: (7,4)=84,(7,5)=84,(7,6)=84,(7,12)=84,
    #      (8,3)=84,(8,13)=84, (9,2)=84,(9,3)=84,(9,12)=84,(9,13)=84
    # Quiver: (7,11 is 8b? no it's fd)
    # Actually looking at frame 7 data more carefully...
    # Row 7: 84 84 84 at cols 4,5,6 - these look like bow
    #         84 at col 12
    # Row 8: 84 at col 3, 84 at col 13
    # Row 9: 84 84 at cols 2,3 and 84 84 at cols 12,13
    mods[7] = [
        # Remove bow
        (7, 4, 0), (7, 5, 0), (7, 6, 0), (7, 12, 0),
        (8, 3, 0), (8, 13, 0),
        (9, 2, 0), (9, 3, 0), (9, 12, 0), (9, 13, 0),
        # Quiver
        (7, 11, 0), (8, 11, 0), (9, 11, 0),
        # Hood -> bald (left profile)
        (3, 7, SKIN_HI), (3, 8, SKIN_HI), (3, 9, SKIN_MD),
        (4, 7, SKIN_HI), (4, 8, SKIN_HI), (4, 9, SKIN_HI), (4, 10, SKIN_MD),
        (5, 8, SKIN_HI), (5, 9, SKIN_HI), (5, 10, SKIN_MD),
    ]

    # --- Frame 8: Down walk2 ---
    # Same arrow as frame 0: (2,11),(3,11),(3,12),(3,8)=fe,(4,10)
    # Bow: (8,3)=84,(8,4)=84, (9,2)=84,(9,4)=84,
    #      (10,1)=84,(10,3)=84, (11,1)=84,(11,2)=84, (12,1)=84
    mods[8] = [
        # Arrow
        (2, 11, 0), (3, 11, 0), (3, 12, 0), (3, 8, 0),
        (4, 10, 0),
        # Bow
        (8, 3, SKIN_FK), (8, 4, SKIN_FK),
        (9, 2, 0), (9, 4, 0),
        (10, 1, 0), (10, 3, SKIN_FK),
        (11, 1, 0), (11, 2, 0),
        (12, 1, 0),
        # Hood -> bald
        (4, 7, SKIN_MD), (4, 8, SKIN_HI), (4, 9, SKIN_MD),
    ]

    # --- Frame 9: Up walk2 ---
    # Bow: (2,13)=84, (3,13)=84,
    #      (4,3)=84, (4,13)=84,(4,14)=84,
    #      (5,2)=84,(5,3)=84,(5,13)=84,(5,14)=84,
    #      (6,14)=84, (7,14)=84, (8,14)=84, (9,13)=84,(9,14)=84,
    #      (10,13)=84
    mods[9] = [
        # Bow
        (2, 13, 0), (3, 13, 0),
        (4, 3, 0), (4, 13, 0), (4, 14, 0),
        (5, 2, 0), (5, 3, 0), (5, 13, 0), (5, 14, 0),
        (6, 14, 0), (7, 14, 0), (8, 14, 0),
        (9, 13, 0), (9, 14, 0),
        (10, 13, 0),
        # Quiver
        (8, 7, 0xFD), (9, 6, 0xFD), (9, 7, 0xFD), (9, 8, 0xFD),
        (10, 7, 0xFD), (10, 8, 0xFD), (10, 9, 0xFD),
        (11, 8, 0xFD), (11, 9, 0xFD),
        # Hood -> bald
        (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD), (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    # --- Frame 10: Right walk2 ---
    # Bow: (2,3)=84, (3,3)=84,(3,8)=84,
    #      (5,13)=84, (6,12)=84,(6,13)=84,
    #      (7,11)=84,(7,13)=84, (8,10)=84,(8,12)=84,
    #      (9,11)=84, (10,8)=84,(10,10)=84,
    #      (11,8)=84,(11,9)=84
    # Quiver: (6,3)=8b,(7,3)=8b,(8,3)=8b
    mods[10] = [
        # Bow
        (2, 3, 0), (3, 8, 0),
        (5, 13, 0),
        (6, 12, 0), (6, 13, 0),
        (7, 11, 0), (7, 13, 0),
        (8, 10, SKIN_FK), (8, 12, 0),
        (9, 11, 0),
        (10, 8, SKIN_FK), (10, 10, 0),
        (11, 8, 0), (11, 9, 0),
        # Quiver
        (6, 3, 0), (7, 3, 0), (8, 3, 0),
        # Hood -> bald
        (3, 3, 0), (3, 4, SKIN_MD), (3, 5, SKIN_HI), (3, 6, SKIN_HI), (3, 7, SKIN_HI),
        (4, 3, SKIN_MD), (4, 4, SKIN_HI), (4, 5, SKIN_HI), (4, 6, SKIN_HI),
        (5, 3, SKIN_MD), (5, 4, SKIN_HI), (5, 5, SKIN_HI), (5, 6, SKIN_HI),
    ]

    # --- Frame 11: Left walk2 ---
    # Bow: (3,2)=84,(3,3)=84, (4,2)=84,(4,4)=84,
    #      (5,2)=84,(5,3)=84, (6,3)=84,(6,5)=84,
    #      (7,3)=84,(7,6)=84, (9,4)=84
    # Quiver: (8,11)=8b? let me check... (7,11)=8b,(8,11)=8b,(9,11)=no
    mods[11] = [
        # Bow
        (3, 2, 0), (3, 3, 0),
        (4, 2, 0), (4, 4, 0),
        (5, 2, 0), (5, 3, 0),
        (6, 3, 0), (6, 5, SKIN_FK),
        (7, 3, 0), (7, 6, 0),
        (9, 4, 0),
        # Quiver
        (7, 11, 0), (8, 11, 0),
        # Hood -> bald (left profile)
        (3, 7, SKIN_HI), (3, 8, SKIN_HI), (3, 9, SKIN_MD),
        (4, 7, SKIN_HI), (4, 8, SKIN_HI), (4, 9, SKIN_HI), (4, 10, SKIN_MD),
        (5, 8, SKIN_HI), (5, 9, SKIN_HI), (5, 10, SKIN_MD),
    ]

    # --- Frame 12: Down-left base ---
    # Bow: (3,2)=84, (4,2)=84,(4,3)=84, (5,2)=84,(5,4)=84,(5,14)=84,
    #      (6,3)=84, (7,3)=84,(7,12)=84,(7,13)=84,
    #      (8,14)=84
    # Hood: ff at (4,7)(4,8)(4,9)(4,10)(4,11), (5,7)(5,11)(5,12)
    #        etc - the ff pixels at the top
    mods[12] = [
        # Bow
        (3, 2, 0),
        (4, 2, 0), (4, 3, 0),
        (5, 2, 0), (5, 4, 0), (5, 14, 0),
        (6, 3, 0),
        (7, 3, 0), (7, 12, SKIN_FK), (7, 13, 0),
        (8, 14, 0),
        # Hood -> bald (down-left angle)
        (4, 7, SKIN_MD), (4, 8, SKIN_HI), (4, 9, SKIN_HI), (4, 10, SKIN_HI), (4, 11, SKIN_MD),
    ]

    # --- Frame 13: Up-right base ---
    # Bow: (1,4)=84, (2,5)=84, (3,5)=84,(3,6)=84,
    #      (4,4)=84,(4,5)=84, (5,3)=84,(5,4)=84
    # Quiver: (7,6)=8b,(7,7)=8b, (8,6)=8b,(8,7)=8b,(8,8)=8b,
    #         (9,6)=8c,(9,7)=8b,(9,8)=8b, (10,6)=8c,(10,7)=8c,
    #         (11,6)=08 (strap)
    # Hood: ff at (4,6)(4,7)(4,8)(4,9), (5,5)(5,6)(5,7)(5,8)(5,9)(5,10)
    mods[13] = [
        # Bow
        (1, 4, 0),
        (2, 5, 0),
        (3, 5, 0), (3, 6, 0),
        (4, 4, 0), (4, 5, 0),
        (5, 3, 0), (5, 4, 0),
        # Quiver -> gi
        (7, 6, 0xFD), (7, 7, 0xFD),
        (8, 6, 0xFD), (8, 7, 0xFD), (8, 8, 0xFD),
        (9, 7, 0xFD), (9, 8, 0xFD),
        (10, 7, 0xFD), (10, 8, 0xFD),
        # Hood -> bald (up-right, back of head)
        (4, 6, SKIN_MD), (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 5, SKIN_MD), (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD),
        (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    # --- Frame 14: Down-right base ---
    # Bow: (2,13)=84, (3,12)=84,(3,13)=84,
    #      (4,11)=84,(4,13)=84,(4,14)=84,
    #      (5,1)=84,(5,12)=84,
    #      (6,2)=84,(6,3)=84,(6,12)=84,
    #      (7,1)=84,(7,3 is not 84 its ff),
    #      (8,10)=84
    # Hood: ff at (3,4)(3,5)(3,6)(3,7)(3,8), (4,3)(4,4)(4,5)
    mods[14] = [
        # Bow
        (2, 13, 0), (3, 12, 0), (3, 13, 0),
        (4, 11, 0), (4, 13, 0), (4, 14, 0),
        (5, 1, 0), (5, 12, 0),
        (6, 2, 0), (6, 3, 0), (6, 12, 0),
        (7, 1, 0),
        (8, 10, SKIN_FK),
        # Hood -> bald (down-right angle)
        (3, 4, SKIN_MD), (3, 5, SKIN_HI), (3, 6, SKIN_HI), (3, 7, SKIN_HI), (3, 8, SKIN_HI),
    ]

    # --- Frame 15: Up-left base ---
    # Bow: (1,11)=84, (2,10)=84, (3,9)=84,(3,10)=84,
    #      (4,10)=84,(4,11)=84, (5,11)=84,(5,12)=84
    # Quiver: (7,8)=8b,(7,9)=8b, (8,7)=8b,(8,8)=8b,(8,9)=8b,
    #         (9,8)=8b,(9,9)=8c
    # Hood: ff at (4,6)(4,7)(4,8)(4,9), (5,5)(5,6)(5,7)(5,8)(5,9)(5,10)
    mods[15] = [
        # Bow
        (1, 11, 0), (2, 10, 0),
        (3, 9, 0), (3, 10, 0),
        (4, 10, 0), (4, 11, 0),
        (5, 11, 0), (5, 12, 0),
        # Quiver -> gi
        (7, 8, 0xFD), (7, 9, 0xFD),
        (8, 7, 0xFD), (8, 8, 0xFD), (8, 9, 0xFD),
        (9, 7, 0xFD), (9, 8, 0xFD),
        # Hood -> bald (up-left, back of head)
        (4, 6, SKIN_MD), (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 5, SKIN_MD), (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD),
        (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    # --- Frame 16: Down-left walk1 ---
    # Bow: (3,1)=84, (4,1)=84,(4,2)=84, (5,1)=84,(5,3)=84,(5,14)=84,
    #      (6,2)=84,(6,4)=84, (7,2)=84,(7,12)=84,(7,13)=84,
    #      (8,14)=84,
    #      (9,6)=84, (10,5)=84,(10,6)=84
    mods[16] = [
        # Bow
        (3, 1, 0),
        (4, 1, 0), (4, 2, 0),
        (5, 1, 0), (5, 3, 0), (5, 14, 0),
        (6, 2, 0), (6, 4, 0),
        (7, 2, 0), (7, 12, SKIN_FK), (7, 13, 0),
        (8, 14, 0),
        (9, 6, 0),
        (10, 5, 0), (10, 6, 0),
        # Hood -> bald
        (4, 7, SKIN_MD), (4, 8, SKIN_HI), (4, 9, SKIN_HI), (4, 10, SKIN_HI), (4, 11, SKIN_MD),
    ]

    # --- Frame 17: Up-right walk1 ---
    # Bow: (1,10)=84, (2,9)=84,(2,10)=84, (3,8)=84,(3,10)=84,
    #      (5,3)=84,(5,4)=84
    # Quiver: (7,6)=8b,(7,7)=8b, (8,6)=8b,(8,7)=8b,(8,8)=8b,
    #         (9,7)=8b,(9,8)=8b
    mods[17] = [
        # Bow
        (1, 10, 0), (2, 9, 0), (2, 10, 0),
        (3, 8, 0), (3, 10, 0),
        (5, 3, 0), (5, 4, 0),
        # Also some 84 that are hands:
        (9, 5, SKIN_FK), (10, 6, 0),
        (11, 4, 0), (11, 5, 0),
        (12, 3, 0),
        # Quiver -> gi
        (7, 6, 0xFD), (7, 7, 0xFD),
        (8, 6, 0xFD), (8, 7, 0xFD), (8, 8, 0xFD),
        (9, 7, 0xFD), (9, 8, 0xFD),
        # Hood -> bald
        (4, 6, SKIN_MD), (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 5, SKIN_MD), (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD),
        (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    # --- Frame 18: Down-right walk1 ---
    # Bow: (5,1)=84,(5,10)=84,(5,11)=84,(5,12)=84,(5,13)=84,(5,14)=84,
    #      (6,2)=84,(6,3)=84,(6,14)=84,
    #      (7,1)=84,(7,9)=84,(7,10)=84,(7,11)=84,(7,12)=84,(7,13)=84
    mods[18] = [
        # Bow
        (5, 1, 0), (5, 10, 0), (5, 11, 0), (5, 12, 0), (5, 13, 0), (5, 14, 0),
        (6, 2, 0), (6, 3, 0), (6, 14, 0),
        (7, 1, 0), (7, 9, SKIN_FK), (7, 10, 0), (7, 11, 0), (7, 12, 0), (7, 13, 0),
        # Hood -> bald
        (3, 4, SKIN_MD), (3, 5, SKIN_HI), (3, 6, SKIN_HI), (3, 7, SKIN_HI), (3, 8, SKIN_HI),
    ]

    # --- Frame 19: Up-left walk1 ---
    # Bow: (4,2)=84,(4,3)=84,(4,4)=84,(4,11)=84,
    #      (5,3)=84,(5,12)=84, (6,4)=84
    # Quiver: (7,8)=8b,(7,9)=8b, (8,7)=8b,(8,8)=8b,(8,9)=8b
    mods[19] = [
        # Bow
        (4, 2, 0), (4, 3, 0), (4, 4, 0), (4, 11, 0),
        (5, 3, 0), (5, 12, 0),
        (6, 4, 0),
        (9, 10, 0), (10, 11, 0), (10, 12, 0),
        (11, 12, 0), (11, 13, 0),
        (12, 6, 0),
        # Quiver -> gi
        (7, 8, 0xFD), (7, 9, 0xFD),
        (8, 7, 0xFD), (8, 8, 0xFD), (8, 9, 0xFD),
        (9, 7, 0xFD), (9, 8, 0xFD),
        # Hood -> bald
        (4, 6, SKIN_MD), (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 5, SKIN_MD), (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD),
        (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    # --- Frame 20: Down-left walk2 ---
    # Bow: (6,1)=84,(6,2)=84,(6,3)=84,(6,4)=84,(6,5)=84,(6,14)=84,
    #      (7,1)=84,
    #      (8,2)=84,(8,3)=84,(8,4)=84,(8,5)=84,(8,6)=84,(8,14)=84
    mods[20] = [
        # Bow
        (6, 1, 0), (6, 2, 0), (6, 3, 0), (6, 4, 0), (6, 5, 0), (6, 14, 0),
        (7, 1, 0),
        (8, 2, 0), (8, 3, 0), (8, 4, 0), (8, 5, 0), (8, 6, 0), (8, 14, 0),
        # Hood -> bald
        (4, 7, SKIN_MD), (4, 8, SKIN_HI), (4, 9, SKIN_HI), (4, 10, SKIN_HI), (4, 11, SKIN_MD),
    ]

    # --- Frame 21: Up-right walk2 ---
    # Bow: (4,4)=84,(4,11)=84,(4,12)=84,(4,13)=84,
    #      (5,3)=84,(5,4)=84,(5,12)=84
    # Quiver: (7,6)=8b,(7,7)=8b, (8,6)=8b,(8,7)=8b,(8,8)=8b
    mods[21] = [
        # Bow
        (4, 4, 0), (4, 11, 0), (4, 12, 0), (4, 13, 0),
        (5, 3, 0), (5, 4, 0), (5, 12, 0),
        (9, 5, 0), (10, 3, 0), (10, 4, 0),
        (11, 2, 0), (11, 3, 0),
        # Quiver -> gi
        (7, 6, 0xFD), (7, 7, 0xFD),
        (8, 6, 0xFD), (8, 7, 0xFD), (8, 8, 0xFD),
        (9, 6, 0xFD), (9, 7, 0xFD), (9, 8, 0xFD),
        # Hood -> bald
        (4, 6, SKIN_MD), (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 5, SKIN_MD), (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD),
        (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    # --- Frame 22: Down-right walk2 ---
    # Bow: (2,14)=84, (3,13)=84,(3,14)=84,
    #      (4,12)=84,(4,14)=84,
    #      (5,1)=84,(5,11)=84,(5,13)=84,
    #      (6,2)=84,(6,3)=84,(6,13)=84,
    #      (7,1)=84,
    #      (8,9)=84, (9,9)=84,(9,10)=84,
    #      (10,9)=84
    mods[22] = [
        # Bow
        (2, 14, 0), (3, 13, 0), (3, 14, 0),
        (4, 12, 0), (4, 14, 0),
        (5, 1, 0), (5, 11, SKIN_FK), (5, 13, 0),
        (6, 2, 0), (6, 3, 0), (6, 13, 0),
        (7, 1, 0),
        (8, 9, 0), (9, 9, 0), (9, 10, 0),
        (10, 9, 0),
        # Hood -> bald
        (3, 4, SKIN_MD), (3, 5, SKIN_HI), (3, 6, SKIN_HI), (3, 7, SKIN_HI), (3, 8, SKIN_HI),
    ]

    # --- Frame 23: Up-left walk2 ---
    # Bow: (1,5)=84, (2,5)=84,(2,6)=84, (3,5)=84,(3,7)=84,
    #      (5,12)=84,
    #      (6,11)=84, (9,10)=84, (10,9)=84,(10,10)=84,
    #      (11,12)=84,(11,13)=84, (12,12)=84
    # Quiver: (7,8)=8b,(7,9)=8b, (8,7)=8b,(8,8)=8b,(8,9)=8b
    mods[23] = [
        # Bow
        (1, 5, 0), (2, 5, 0), (2, 6, 0),
        (3, 5, 0), (3, 7, 0),
        (5, 12, 0),
        (6, 11, 0),
        (9, 10, 0), (10, 9, 0), (10, 10, 0),
        (11, 12, 0), (11, 13, 0),
        (12, 12, 0),
        # Quiver -> gi
        (7, 8, 0xFD), (7, 9, 0xFD),
        (8, 7, 0xFD), (8, 8, 0xFD), (8, 9, 0xFD),
        (9, 7, 0xFD), (9, 8, 0xFD),
        # Hood -> bald
        (4, 6, SKIN_MD), (4, 7, SKIN_MD), (4, 8, SKIN_MD), (4, 9, SKIN_MD),
        (5, 5, SKIN_MD), (5, 6, SKIN_MD), (5, 7, SKIN_MD), (5, 8, SKIN_MD),
        (5, 9, SKIN_MD), (5, 10, SKIN_MD),
    ]

    return mods


def make_hood_cleanup():
    """Remove remaining hood ff pixels that wrap around the head/neck area.

    The initial bald-head pass only replaced the top of the head (rows 3-5).
    The hood also wraps down the sides and back through rows 5-7, forming
    a visible hood shape that needs to become robe collar (fd) or skin (82).
    """
    mods = {}

    # Down-facing (0, 4, 8): widen dome + forehead + expose neck/face
    for fi in [0, 4, 8]:
        mods[fi] = [
            # Widen bald dome from 3px to 5px
            (4, 6, SKIN_MD), (4, 10, SKIN_MD),
            # Forehead (continuous skin — no line between dome and eyes)
            (5, 5, SKIN_FK), (5, 6, SKIN_MD),
            (5, 7, SKIN_HI), (5, 8, SKIN_HI), (5, 9, SKIN_HI),
            (5, 10, SKIN_MD), (5, 11, SKIN_FK),
            # Widen face row
            (6, 5, SKIN_FK), (6, 10, SKIN_FK), (6, 11, 0xFD),
            # Jaw / upper collar
            (7, 5, 0xFD), (7, 11, 0xFD),
        ]

    # Up-facing (1, 5, 9): hood wraps back of head at row 6
    for fi in [1, 5, 9]:
        mods[fi] = [
            (6, 6, SKIN_MD), (6, 7, SKIN_MD), (6, 8, SKIN_MD), (6, 9, SKIN_MD),
            (6, 11, 0xFD),
            (7, 11, 0xFD),
        ]

    # Right-facing base (2) and walk2 (10): hood at (6,4),(6,5)
    for fi in [2, 10]:
        mods[fi] = [
            (6, 4, 0xFD), (6, 5, 0xFD),
        ]
    # Right walk1 (6): only (6,5)
    mods[6] = [
        (6, 5, 0xFD),
    ]

    # Left-facing base (3) and walk1 (7): hood at (6,9),(6,10)
    for fi in [3, 7]:
        mods[fi] = [
            (6, 9, 0xFD), (6, 10, 0xFD),
        ]
    # Left walk2 (11): (6,8),(6,9)
    mods[11] = [
        (6, 8, 0xFD), (6, 9, 0xFD),
    ]

    # Down-left (12, 16, 20): widen head + expose face on right side
    for fi in [12, 16, 20]:
        mods[fi] = [
            (5, 7, SKIN_MD), (5, 10, SKIN_MD), (5, 11, SKIN_FK),
            (6, 10, SKIN_FK), (6, 11, 0xFD),
            (7, 10, 0xFD), (7, 11, 0xFD),
        ]

    # Up-right (13, 17, 21): hood at rows 6-7
    for fi in [13, 17, 21]:
        mods[fi] = [
            (6, 9, SKIN_MD), (6, 10, SKIN_MD),
            (7, 9, 0xFD), (7, 10, 0xFD),
        ]

    # Down-right (14, 18, 22): extensive hood through rows 4-7 left side
    for fi in [14, 18, 22]:
        mods[fi] = [
            (4, 3, 0xFD), (4, 4, SKIN_MD), (4, 5, SKIN_HI), (4, 8, SKIN_HI),
            (5, 3, 0xFD), (5, 4, SKIN_MD), (5, 5, SKIN_HI),
            (6, 4, 0xFD), (6, 5, 0xFD),
            (7, 4, 0xFD), (7, 5, 0xFD), (7, 6, 0xFD),
        ]

    # Up-left (15, 19, 23): hood at rows 6-7
    for fi in [15, 19, 23]:
        mods[fi] = [
            (6, 5, SKIN_MD), (6, 6, SKIN_MD),
            (7, 5, 0xFD), (7, 6, 0xFD),
        ]

    return mods


def apply_modifications(frames, mods):
    """Apply pixel modifications to frames."""
    for fi, mod_list in mods.items():
        if fi < 0 or fi >= len(frames):
            continue
        frame = frames[fi]
        for (r, c, val) in mod_list:
            if 0 <= r < HEIGHT and 0 <= c < WIDTH:
                frame[r][c] = val


# =====================================================================
# Robe templates — replace archer legs/boots with flowing robes
# =====================================================================
# Team color indices used for robes:
R0 = 0xFF  # Bright highlight
R1 = 0xFE  # Light
R2 = 0xFD  # Main fill
R3 = 0xFC  # Dark
R4 = 0xFB  # Shadow
T = 0       # Transparent
FK = 0x84   # Skin (hands)
N1 = 0xF3   # Neutral (hand/wrist)

def clear_rows(frame, start_row, end_row):
    """Set rows [start_row, end_row) to transparent."""
    for r in range(start_row, min(end_row, HEIGHT)):
        for c in range(WIDTH):
            frame[r][c] = T

def set_row(frame, row, pixels):
    """Set a row from a dict of {col: value}."""
    for c, v in pixels.items():
        frame[row][c] = v

def apply_robes(frames):
    """Apply mage-style robes to all frames, replacing archer lower body."""

    # --- Frame 0: Down base ---
    f = frames[0]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {3:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 12:FK})
    set_row(f, 10, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 11, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 12, {3:R3, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3, 9:R3})

    # --- Frame 1: Up base ---
    f = frames[1]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {4:R0, 5:R2, 6:R3, 7:R2, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 11, {3:R0, 4:R3, 5:R2, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3, 11:R0})
    set_row(f, 12, {3:R3, 4:R3, 5:R2, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3, 9:R3})

    # --- Frame 2: Right base ---
    f = frames[2]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {4:R2, 5:R2, 6:R2, 7:R2, 8:FK})
    set_row(f, 10, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2})
    set_row(f, 11, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R0})
    set_row(f, 12, {3:R3, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3})

    # --- Frame 3: Left base ---
    f = frames[3]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {7:FK, 8:R2, 9:R2, 10:R2, 11:R2})
    set_row(f, 10, {7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 11, {6:R0, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 12, {7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R3})
    set_row(f, 13, {8:R3, 9:R3, 10:R3, 11:R3})

    # --- Frame 4: Down walk1 (left leg forward = robe swings left) ---
    f = frames[4]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {3:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 12:FK})
    set_row(f, 10, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 11, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R0})
    set_row(f, 12, {3:R3, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3, 10:R3, 11:R3})

    # --- Frame 5: Up walk1 ---
    f = frames[5]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {4:R0, 5:R2, 6:R3, 7:R2, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 11, {3:R0, 4:R3, 5:R2, 6:R3, 7:R2, 8:R3, 9:R2, 10:R0})
    set_row(f, 12, {3:R3, 4:R3, 5:R2, 6:R3, 7:R2, 8:R3, 9:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3, 10:R3, 11:R3})

    # --- Frame 6: Right walk1 (stride forward) ---
    f = frames[6]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {4:R2, 5:R2, 6:R2, 7:R2, 8:FK})
    set_row(f, 10, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2})
    set_row(f, 11, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R0})
    set_row(f, 12, {3:R3, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3})

    # --- Frame 7: Left walk1 ---
    f = frames[7]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {7:FK, 8:R2, 9:R2, 10:R2, 11:R2})
    set_row(f, 10, {6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 11, {6:R0, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 12, {6:R3, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R3})
    set_row(f, 13, {7:R3, 8:R3, 9:R3, 10:R3, 11:R3})

    # --- Frame 8: Down walk2 (right leg forward = robe swings right) ---
    f = frames[8]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {3:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 12:FK})
    set_row(f, 10, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 11, {5:R0, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 12, {6:R3, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R3})
    set_row(f, 13, {5:R3, 6:R3, 7:R3, 8:R3, 9:R3, 10:R3, 11:R3})

    # --- Frame 9: Up walk2 ---
    f = frames[9]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {4:R0, 5:R2, 6:R3, 7:R2, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 11, {5:R0, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3, 11:R2, 12:R0})
    set_row(f, 12, {6:R3, 7:R2, 8:R3, 9:R2, 10:R3, 11:R2, 12:R3})
    set_row(f, 13, {5:R3, 6:R3, 7:R3, 8:R3, 9:R3, 10:R3, 11:R3})

    # --- Frame 10: Right walk2 ---
    f = frames[10]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {4:R2, 5:R2, 6:R2, 7:R2, 8:FK})
    set_row(f, 10, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2})
    set_row(f, 11, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R0})
    set_row(f, 12, {4:R3, 5:R2, 6:R2, 7:R2, 8:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3})

    # --- Frame 11: Left walk2 ---
    f = frames[11]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {7:FK, 8:R2, 9:R2, 10:R2, 11:R2})
    set_row(f, 10, {7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 11, {6:R0, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 12, {7:R3, 8:R2, 9:R2, 10:R2, 11:R3})
    set_row(f, 13, {8:R3, 9:R3, 10:R3, 11:R3})

    # --- Frame 12: Down-left base ---
    f = frames[12]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {4:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 12:FK})
    set_row(f, 10, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 11, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 12, {4:R3, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R3})
    set_row(f, 13, {5:R3, 6:R3, 7:R3, 8:R3, 9:R3})

    # --- Frame 13: Up-right base ---
    f = frames[13]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {4:R0, 5:R3, 6:R2, 7:R2, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 11, {4:R0, 5:R3, 6:R2, 7:R3, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 12, {5:R3, 6:R2, 7:R3, 8:R2, 9:R3, 10:R3})
    set_row(f, 13, {5:R3, 6:R3, 7:R3, 8:R3, 9:R3})

    # --- Frame 14: Down-right base ---
    f = frames[14]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {3:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:FK})
    set_row(f, 10, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 11, {4:R0, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R0})
    set_row(f, 12, {5:R3, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R3})
    set_row(f, 13, {6:R3, 7:R3, 8:R3, 9:R3, 10:R3})

    # --- Frame 15: Up-left base ---
    f = frames[15]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {4:R0, 5:R2, 6:R3, 7:R2, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 11, {4:R0, 5:R2, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3, 11:R0})
    set_row(f, 12, {5:R3, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3})
    set_row(f, 13, {6:R3, 7:R3, 8:R3, 9:R3, 10:R3})

    # --- Frame 16: Down-left walk1 ---
    f = frames[16]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {4:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 12:FK})
    set_row(f, 10, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R0})
    set_row(f, 11, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R0})
    set_row(f, 12, {3:R3, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3, 10:R3, 11:R3})

    # --- Frame 17: Up-right walk1 ---
    f = frames[17]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {5:R0, 6:R3, 7:R2, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 11, {5:R0, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3, 11:R2, 12:R0})
    set_row(f, 12, {6:R3, 7:R2, 8:R3, 9:R2, 10:R3, 11:R2, 12:R3})
    set_row(f, 13, {5:R3, 6:R3, 7:R3, 8:R3, 9:R3, 10:R3, 11:R3})

    # --- Frame 18: Down-right walk1 ---
    f = frames[18]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {3:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:FK})
    set_row(f, 10, {5:R0, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 11, {5:R0, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 12, {6:R3, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R3})
    set_row(f, 13, {5:R3, 6:R3, 7:R3, 8:R3, 9:R3, 10:R3, 11:R3})

    # --- Frame 19: Up-left walk1 ---
    f = frames[19]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {4:R0, 5:R2, 6:R3, 7:R2, 8:R2, 9:R3, 10:R0})
    set_row(f, 11, {3:R0, 4:R2, 5:R3, 6:R2, 7:R3, 8:R2, 9:R3, 10:R0})
    set_row(f, 12, {3:R3, 4:R2, 5:R3, 6:R2, 7:R3, 8:R2, 9:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3, 9:R3})

    # --- Frame 20: Down-left walk2 ---
    f = frames[20]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {4:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 12:FK})
    set_row(f, 10, {5:R0, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 11, {5:R0, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R0})
    set_row(f, 12, {6:R3, 7:R2, 8:R2, 9:R2, 10:R2, 11:R2, 12:R3})
    set_row(f, 13, {5:R3, 6:R3, 7:R3, 8:R3, 9:R3, 10:R3, 11:R3})

    # --- Frame 21: Up-right walk2 ---
    f = frames[21]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {4:R0, 5:R3, 6:R2, 7:R2, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 11, {3:R0, 4:R3, 5:R2, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3, 11:R0})
    set_row(f, 12, {3:R3, 4:R3, 5:R2, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3, 9:R3})

    # --- Frame 22: Down-right walk2 ---
    f = frames[22]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {3:FK, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2, 11:FK})
    set_row(f, 10, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R0})
    set_row(f, 11, {3:R0, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R0})
    set_row(f, 12, {3:R3, 4:R2, 5:R2, 6:R2, 7:R2, 8:R2, 9:R3})
    set_row(f, 13, {4:R3, 5:R3, 6:R3, 7:R3, 8:R3})

    # --- Frame 23: Up-left walk2 ---
    f = frames[23]
    clear_rows(f, 9, 15)
    set_row(f, 9,  {5:R2, 6:R2, 7:R2, 8:R2, 9:R2, 10:R2})
    set_row(f, 10, {4:R0, 5:R2, 6:R3, 7:R2, 8:R2, 9:R3, 10:R2, 11:R0})
    set_row(f, 11, {4:R0, 5:R2, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3, 11:R0})
    set_row(f, 12, {5:R3, 6:R3, 7:R2, 8:R3, 9:R2, 10:R3})
    set_row(f, 13, {6:R3, 7:R3, 8:R3, 9:R3, 10:R3})


def mirror_downleft_from_downright(frames):
    """Copy the head area (rows 3-7) from down-right frames to down-left,
    mirrored horizontally. The down-right head looks good; the down-left
    should be its mirror image."""
    pairs = [(14, 12), (18, 16), (22, 20)]  # (down-right, down-left)
    for src, dst in pairs:
        for r in range(3, 8):
            frames[dst][r] = frames[src][r][::-1]


if __name__ == "__main__":
    archer_path = os.path.join(os.path.dirname(__file__), "..", "pix", "archer.pix")
    frames = read_pix(archer_path)

    mods = make_modifications()
    apply_modifications(frames, mods)
    hood_mods = make_hood_cleanup()
    apply_modifications(frames, hood_mods)
    mirror_downleft_from_downright(frames)
    apply_robes(frames)

    if "--preview" in sys.argv:
        labels = [
            'Down base', 'Up base', 'Right base', 'Left base',
            'Down walk1', 'Up walk1', 'Right walk1', 'Left walk1',
            'Down walk2', 'Up walk2', 'Right walk2', 'Left walk2',
            'Down-left base', 'Up-right base', 'Down-right base', 'Up-left base',
            'Down-left walk1', 'Up-right walk1', 'Down-right walk1', 'Up-left walk1',
            'Down-left walk2', 'Up-right walk2', 'Down-right walk2', 'Up-left walk2',
        ]
        for i in range(24):
            preview_frame(frames[i], f"Frame {i}: {labels[i]}")

    outpath = os.path.join(os.path.dirname(__file__), "..", "pix", "monk.pix")
    write_pix(outpath, frames)
