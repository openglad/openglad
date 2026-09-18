#!/usr/bin/env bash
# P13 proof media: floating damage/heal numbers on the display mirror.
#
# The 2013 rising-number overlay (OPTIONS > EFFECTS > "Damage numbers" /
# "Healing numbers") stopped painting on every client when the networking
# merge (#106) made the display render a MIRROR world: the sim pushes the
# numbers onto the AUTHORITATIVE walkers, and apply_snapshot cleared the
# mirror's copy. This script measures that directly — two identical lockstep
# demo runs, one with the toggle ON and one OFF, compared frame by frame:
#
#   before the fix: 0 of 600 frames differ (the toggle changes nothing)
#   after  the fix: N > 0 frames differ, every differing pixel inside a
#                   floating-number glyph box above a walker
#
# Scenario 20 / seed 5 is the recipe: the demo binds seat 0 to a hero that
# actually fights there (on quieter levels the bound hero can idle out of
# contact for the whole window and neither tree paints anything).
#
# Run it once per tree (base and fixed) with OPENGLAD_DEMO pointed at that
# tree's binary; the GIF from each run's ON capture is the before/after pair.
#
# The demo runs on SDL's dummy video driver and writes 8-bit indexed BMPs;
# ffmpeg/magick come from the dev shell (`nix develop`, see flake.nix).
# Output lands in build/media/p13/ (gitignored) — finished artifacts go to the
# openglad/openglad-screenshots repo, never to this one.
#
# Usage: scripts/media/capture_damage_numbers.sh [output-dir]
#        OPENGLAD_DEMO=/path/to/openglad_demo LABEL=before ... (default: after)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO_BIN="${OPENGLAD_DEMO:-$REPO_ROOT/build/ci-test/openglad_demo}"
OUT_DIR="${1:-$REPO_ROOT/build/media/p13}"
LABEL="${LABEL:-after}"
FRAMES="${OPENGLAD_DEMO_CAPTURE_LIMIT:-600}"
MAX_FRAMES="${OPENGLAD_DEMO_MAX_FRAMES:-620}"
SCEN="${OPENGLAD_DEMO_SCENARIOS:-20}"
SEED="${OPENGLAD_DEMO_SEED:-5}"

if [ ! -x "$DEMO_BIN" ]; then
    printf 'openglad_demo not found at %s\n' "$DEMO_BIN" >&2
    printf 'build it with: cmake --build --preset ci-test --target openglad_demo\n' >&2
    exit 1
fi
for tool in ffmpeg magick; do
    if ! command -v "$tool" > /dev/null; then
        printf '%s not found; enter the dev shell first: nix develop\n' "$tool" >&2
        exit 1
    fi
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT
mkdir -p "$OUT_DIR"

FFMPEG=(ffmpeg -hide_banner -loglevel error -y)

# The GIFs are cut from the BMP dumps with the concat demuxer: one list line
# per animation frame, each with an explicit on-screen duration.
emit() { # emit <list> <dir> <duration-secs> <first> <last> <step>
    local list=$1 dir=$2 dur=$3 first=$4 last=$5 step=$6 i
    for ((i = first; i <= last; i += step)); do
        printf "file '%s/frame%05d.bmp'\nduration %s\n" "$dir" "$i" "$dur" >> "$list"
    done
}

# Two passes: palettegen collects the exact colours the frames use (indexed
# game frames have at most 256, so nothing is quantized), paletteuse maps every
# frame through that palette with no dithering. The 2x nearest-neighbour
# upscale is what makes the 4x6 damage-number font legible.
encode_gif() { # encode_gif <list> <out.gif> <final-delay-cs>
    local list=$1 out=$2 final=$3 palette="$WORK_DIR/palette.png"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,palettegen' "$palette"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" -i "$palette" \
        -lavfi '[0:v]scale=iw*2:ih*2:flags=neighbor[x];[x][1:v]paletteuse=dither=none' \
        -final_delay "$final" "$out"
    rm -f -- "$palette"
}

# Stills stay indexed too: palettegen/paletteuse on the single frame hands the
# PNG encoder a pal8 picture instead of 32-bit RGB.
still2x() { # still2x <in.bmp> <out.png>
    "${FFMPEG[@]}" -i "$1" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,split[a][b];[a]palettegen=reserve_transparent=0[p];[b][p]paletteuse=dither=none' \
        -frames:v 1 -update 1 -compression_level 9 "$2"
}

# The demo reads its settings through OPENGLAD_CONFIG_DIR (platform_io.cpp;
# gparser reads cfg/openglad.yaml through it), so each run gets its own seeded
# copy with only the damage_numbers line changed.
seed_config() { # seed_config <dir> <on|off>
    mkdir -p "$1/cfg"
    sed "s/^  damage_numbers: .*/  damage_numbers: $2/" \
        "$REPO_ROOT/cfg/openglad.yaml" > "$1/cfg/openglad.yaml"
    grep -q "damage_numbers: $2" "$1/cfg/openglad.yaml" ||
        { printf 'failed to seed damage_numbers: %s\n' "$2" >&2; exit 1; }
}

capture() { # capture <on|off>
    local state=$1
    seed_config "$WORK_DIR/cfg-$state" "$state"
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_SEED="$SEED" \
        OPENGLAD_DEMO_SCENARIOS="$SCEN" \
        OPENGLAD_DEMO_LOCKSTEP=1 \
        OPENGLAD_DEMO_CAPTURE_DIR="$WORK_DIR/frames-$state" \
        OPENGLAD_DEMO_CAPTURE_FOCUS=player \
        OPENGLAD_DEMO_CAPTURE_LIMIT="$FRAMES" \
        OPENGLAD_DEMO_MAX_FRAMES="$MAX_FRAMES" \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/cfg-$state" \
        "$DEMO_BIN" > "$WORK_DIR/$state.log" 2>&1
}

capture on
capture off

# The quantified claim: how many of the captured frames the toggle changes.
differing=0
first_diff=-1
for ((i = 0; i < FRAMES; ++i)); do
    a="$(printf '%s/frames-on/frame%05d.bmp' "$WORK_DIR" "$i")"
    b="$(printf '%s/frames-off/frame%05d.bmp' "$WORK_DIR" "$i")"
    [ -f "$a" ] && [ -f "$b" ] || break
    if ! cmp -s "$a" "$b"; then
        differing=$((differing + 1))
        [ "$first_diff" -lt 0 ] && first_diff=$i
    fi
done
printf 'damage_numbers on vs off: %d of %d frames differ\n' "$differing" "$FRAMES"

# The GIF is the first 300 ticks of the ON run (every 2nd frame at 0.08 s);
# the differing-frame count above is measured over the whole window.
gif_last=$((FRAMES - 1))
[ "$gif_last" -gt 299 ] && gif_last=299
list="$WORK_DIR/p13.ffconcat"
: > "$list"
emit "$list" "$WORK_DIR/frames-on" 0.08 0 "$gif_last" 2
encode_gif "$list" "$OUT_DIR/p13-damage-numbers-$LABEL.gif" 100

# The "before" run has no differing frame at all (that IS the defect), so it
# takes the still frame index from the "after" run via OPENGLAD_P13_STILL_FRAME
# and the pair shows the same moment on both trees.
still_frame="${OPENGLAD_P13_STILL_FRAME:-$first_diff}"
if [ "$first_diff" -ge 0 ]; then
    printf 'first differing frame: %d\n' "$first_diff"
    magick compare -metric AE \
        "$(printf '%s/frames-on/frame%05d.bmp' "$WORK_DIR" "$first_diff")" \
        "$(printf '%s/frames-off/frame%05d.bmp' "$WORK_DIR" "$first_diff")" \
        "$WORK_DIR/diff.png" 2> "$WORK_DIR/ae.txt" || true
    printf 'magick compare -metric AE on frame %d: %s differing pixel(s)\n' \
        "$first_diff" "$(cat "$WORK_DIR/ae.txt")"
    # Bounding box of the changed pixels: the glyph box above the walker.
    box="$(magick "$WORK_DIR/diff.png" -fuzz 0 -fill white -opaque red \
        -colorspace Gray -threshold 50% -format '%@' info: 2>/dev/null || true)"
    printf 'changed-pixel bounding box: %s\n' "${box:-unavailable}"
else
    printf 'no differing frame: the toggle paints nothing (the P13 defect)\n'
fi

if [ "$still_frame" -ge 0 ]; then
    frame_bmp="$(printf '%s/frames-on/frame%05d.bmp' "$WORK_DIR" "$still_frame")"
    still2x "$frame_bmp" "$OUT_DIR/p13-first-hit-$LABEL.png"
    # A 10x5 glyph box is unreadable on its own: crop the walker around it and
    # blow it up with nearest-neighbour so the number is legible.
    if [ -n "${box:-}" ]; then
        read -r bw bh bx by <<< "$(printf '%s' "$box" | tr 'x+' '   ')"
        cx=$((bx + bw / 2)); cy=$((by + bh / 2))
        ox=$((cx - 28)); oy=$((cy - 20))
        [ "$ox" -lt 0 ] && ox=0
        [ "$oy" -lt 0 ] && oy=0
        magick "$frame_bmp" -crop "56x40+$ox+$oy" +repage \
            -filter point -resize 600% \
            "$OUT_DIR/p13-first-hit-crop-4x.png"
    fi
fi

printf 'wrote media to %s\n' "$OUT_DIR"
