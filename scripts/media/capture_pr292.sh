#!/usr/bin/env bash
# Proof media for PR #292 (the test-teeth audit's product fixes).
#
# Every user-visible fix in that PR owes a before/after pair, and both halves
# have to come from the repository's own capture seams — openglad_demo for
# gameplay, the og_test_* capture tests for scripted scenes — never from a
# hand-rolled encoder or a manual screenshot (AGENTS.md, "PR screenshots and
# proof media").  Output lands in build/media/pr-292/{before,after,final}/,
# which is gitignored; the finished files are pushed to the
# openglad/openglad-screenshots repo under pr-292/ and linked from the PR body
# by SHA-pinned raw URL.
#
# The two halves are captured at different times on purpose:
#   --before  runs on the phase-2 base tree with only the capture code added,
#             BEFORE any fix merges;
#   --after   runs on the merged tree;
#   --compose pairs the two directories into the shipped artifacts.
# Anything else (re-deriving a "before" from a half-fixed tree) produces a
# picture that proves nothing.
#
# Usage:
#   scripts/media/capture_pr292.sh --before  [--scene <name>]...
#   scripts/media/capture_pr292.sh --after   [--scene <name>]...
#   scripts/media/capture_pr292.sh --compose [--scene <name>]...
#   scripts/media/capture_pr292.sh --list
#
#   OPENGLAD_BUILD_DIR=<dir>   build tree holding the binaries
#                              (default build/ci-test)
#   OPENGLAD_PR292_MEDIA_DIR=<dir>
#                              output root (default build/media/pr-292)
#   BEFORE_CAPTION=<text>      --compose only: what each half's burnt-in strip
#   AFTER_CAPTION=<text>       says after "BEFORE  " / "AFTER  ".  The defaults
#                              ("base tree" / "fixed tree") name no tree, so the
#                              shipped PR #292 strips were composed with
#                              BEFORE_CAPTION='base 64df07ad' and
#                              AFTER_CAPTION='PR #292'; --compose with those two
#                              values reproduces the pushed files byte for byte,
#                              without them it does not.
#   OPENGLAD_Q15_DIR=<dir>     q15 only: re-encode the phase's cut from the
#                              frames already in <dir>/q15-frames instead of
#                              running the scene test.  Only the base tree can
#                              dump the "before" frames, so on a merged
#                              checkout --before needs this.
#   OPENGLAD_P13_DIR=<dir>     p13 only: take the phase's assets from <dir>
#                              instead of running the demo.  The "before" half
#                              can only be filmed by the BASE tree's
#                              openglad_demo, so on a merged checkout --before
#                              always needs this.
#
# Scenes:
#   selftest  harness self-check: two openglad_demo stills of one stock level
#             through the cell camera.  Proves the whole pipeline (demo run,
#             scratch config, BMP -> pal8 PNG, ffprobe verification) before a
#             recipe blames the game for a missing frame.  Not shipped.
#   p1        the alpha text blitter (sdl_video::walkputbuffertext_alpha).  A
#             scripted render scene draws the floating damage/heal numbers,
#             the path's only production consumer.
#   p5        a low-magic AI cleric healing a hurt ally — motion, so a GIF.
#   q2        the TEAM/FOES counter box with a pending wave.  The base tree
#             right-aligns "FOES: 12 (+34)" and "NEXT WAVE: 120s" to the
#             viewport edge, so both rows hang off the 55-px box over the
#             world; the fit rule sizes the box to its widest row instead.
#             og_test_view's GladHud.zz_capture_pending_wave_counter_box
#             asserts that geometry and dumps the frame, so it is red BY
#             DESIGN on the base tree (SCENE_ALLOW_FAIL, like p5).
#   q15       the local spectator's follow camera.  A zero-seat display presses
#             SwitchChar at frame 30 of 121; the base tree's per-tick control
#             re-sync drags the camera back onto the seat-bound walker inside
#             the same frame (and that walker stands claimed but unmanned for
#             the whole level), the fixed tree moves the camera and keeps it.
#             "Moves, then snaps back" is not a still, so this one is a GIF;
#             og_test_game_core's GameLoop.zz_capture_spectator_follow asserts
#             the fixed behaviour and is therefore red BY DESIGN on the base
#             tree (SCENE_ALLOW_FAIL, like p5 and q2).
#   p8        the curses lobby's start-denial band, as a terminal transcript.
#   p13       the floating damage/heal numbers, which stopped painting on every
#             client when the display began rendering a mirror world.  The
#             capture itself belongs to scripts/media/capture_damage_numbers.sh;
#             this harness runs it and assembles the pair.
#
# The P2 autotiler recipe that used to live here left with the item: the
# smooth.cpp gaps were deferred out of PR #292 to issue #301, and the census
# that sizes them stayed behind as a standalone tool
# (scripts/media/smooth_mask_census.py, which issue #301 runs directly).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${OPENGLAD_BUILD_DIR:-$REPO_ROOT/build/ci-test}"
MEDIA_ROOT="${OPENGLAD_PR292_MEDIA_DIR:-$REPO_ROOT/build/media/pr-292}"

SCENES=(selftest p1 p5 p8 p13 q2 q15)
DEFAULT_SCENES=(p1 p5 p8 p13 q2 q15)

# A GIF that will not load on GitHub is not proof; this is the band every
# shipped animation in the screenshots repo already sits in.
GIF_MAX_BYTES=2000000

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

usage() {
    sed -n '/^# Usage:/,/^$/s/^# \{0,1\}//p' "${BASH_SOURCE[0]}"
}

# --- arguments --------------------------------------------------------------
PHASE=""
SELECTED=()
while [ $# -gt 0 ]; do
    case "$1" in
        --before) PHASE="before" ;;
        --after) PHASE="after" ;;
        --compose) PHASE="compose" ;;
        --list)
            printf '%s\n' "${SCENES[@]}"
            exit 0
            ;;
        --scene)
            shift
            [ $# -gt 0 ] || die 'error: --scene needs a name'
            SELECTED+=("$1")
            ;;
        --scene=*) SELECTED+=("${1#--scene=}") ;;
        -h | --help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            die "error: unknown argument '$1'"
            ;;
    esac
    shift
done

[ -n "$PHASE" ] || { usage >&2; die 'error: one of --before / --after / --compose is required'; }
if [ "${#SELECTED[@]}" -eq 0 ]; then
    SELECTED=("${DEFAULT_SCENES[@]}")
fi
for want in "${SELECTED[@]}"; do
    found=0
    for known in "${SCENES[@]}"; do
        [ "$want" = "$known" ] && found=1
    done
    [ "$found" -eq 1 ] || die "error: no such scene '$want' (see --list)"
done

wanted() { # wanted <scene>
    local name
    for name in "${SELECTED[@]}"; do
        [ "$name" = "$1" ] && return 0
    done
    return 1
}

BEFORE_DIR="$MEDIA_ROOT/before"
AFTER_DIR="$MEDIA_ROOT/after"
FINAL_DIR="$MEDIA_ROOT/final"
if [ "$PHASE" = "compose" ]; then
    OUT_DIR="$FINAL_DIR"
else
    OUT_DIR="$MEDIA_ROOT/$PHASE"
fi
mkdir -p "$OUT_DIR"

# --- prerequisites ----------------------------------------------------------
need_bin() { # need_bin <path>
    [ -x "$1" ] && return 0
    printf '%s not found; build it first:\n' "$1" >&2
    printf '  cmake --build --preset ci-test --target %s\n' "$(basename "$1")" >&2
    exit 1
}

DEMO_BIN="$BUILD_DIR/openglad_demo"

for tool in ffmpeg ffprobe magick python3; do
    command -v "$tool" > /dev/null \
        || die "$tool not found; enter the dev shell first: nix develop"
done

# The transcripts are rendered with a real mono TrueType face, found the way
# scripts/media/make_lua_ownership_overlays.py finds its DejaVu files.
find_mono_font() {
    if [ -n "${OG_OVERLAY_FONT:-}" ] && [ -f "$OG_OVERLAY_FONT" ]; then
        printf '%s\n' "$OG_OVERLAY_FONT"
        return 0
    fi
    local roots root hit split
    roots="${XDG_DATA_DIRS:-}:$HOME/.nix-profile/share:/usr/local/share:/usr/share"
    IFS=':' read -r -a split <<< "$roots"
    for root in "${split[@]}"; do
        [ -n "$root" ] || continue
        hit="$(find "$root/fonts" -name 'DejaVuSansMono.ttf' 2>/dev/null | head -1)"
        [ -n "$hit" ] && { printf '%s\n' "$hit"; return 0; }
    done
    return 1
}

WORK_DIR="$(mktemp -d)"
FFMPEG=(ffmpeg -hide_banner -loglevel error -y)

# The scripted scenes build their own rosters through save/save0.gtl; a real
# save must survive the run (scripts/fx_review/generate.sh precedent).
SAVE_BACKUP=""
if [ -f save/save0.gtl ]; then
    SAVE_BACKUP="$(mktemp)"
    cp save/save0.gtl "$SAVE_BACKUP"
fi
cleanup() {
    if [ -n "$SAVE_BACKUP" ]; then
        cp "$SAVE_BACKUP" save/save0.gtl
        rm -f -- "$SAVE_BACKUP"
    fi
    rm -rf -- "$WORK_DIR"
}
trap cleanup EXIT

# --- expected-output manifest ----------------------------------------------
# Each recipe declares what it owes; the tail below is what actually checks
# it.  scripts/media/verify_media.py has a fixed EXPECTED table and cannot
# know about files that only exist for one PR.
EXPECT_PNG=()   # "<path>|<width>|<height>|<pix_fmt>"
EXPECT_GIF=()   # "<path>"

expect_png() { EXPECT_PNG+=("$1|$2|$3|${4:-}"); }
expect_gif() { EXPECT_GIF+=("$1"); }

# --- capture seams ----------------------------------------------------------
# Headless, and every run writes its config into a scratch directory: a run
# that cannot reach its config directory falls back to the working directory
# and rewrites the tracked cfg/openglad.yaml.  The guard at the bottom of this
# script catches that.
run_demo() { # run_demo <name> <campaign> <scenarios> <team> <focus> <seed> <limit>
    local name="$1" campaign="$2" scenarios="$3" team="$4" focus="$5" seed="$6" limit="$7"
    need_bin "$DEMO_BIN"
    FRAME_DIR="$WORK_DIR/frames-$name"
    rm -rf -- "$FRAME_DIR"
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_SEED="$seed" \
        OPENGLAD_DEMO_CAMPAIGN="$campaign" \
        OPENGLAD_DEMO_SCENARIOS="$scenarios" \
        OPENGLAD_DEMO_TEAM_SIZE="$team" \
        OPENGLAD_DEMO_CAPTURE_DIR="$FRAME_DIR" \
        OPENGLAD_DEMO_CAPTURE_FOCUS="$focus" \
        OPENGLAD_DEMO_CAPTURE_LIMIT="$limit" \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
        "$DEMO_BIN" > "$WORK_DIR/$name.log" 2>&1 || {
        printf 'demo run %s failed; see %s\n' "$name" "$WORK_DIR/$name.log" >&2
        exit 1
    }
}

# One test binary, one --gtest_filter, per call: the shuffle and multi-suite
# forms are what make og_test_basecamp hang on seeds 7 and 29.
#
# SCENE_ALLOW_FAIL=1 keeps a red exit from stopping the capture: a scene that
# is red BY DESIGN on the base tree (it asserts the behaviour the defect
# withholds) still dumps its frames, and that refusal cut is the "before".
# The caller gets the exit status in SCENE_STATUS either way.
run_scene_test() { # run_scene_test <name> <binary> <gtest_filter> [VAR=VALUE ...]
    local name="$1" binary="$2" filter="$3"
    shift 3
    need_bin "$BUILD_DIR/$binary"
    SCENE_DIR="$WORK_DIR/scene-$name"
    SCENE_LOG="$WORK_DIR/$name.log"
    rm -rf -- "$SCENE_DIR"
    mkdir -p "$SCENE_DIR"
    SCENE_STATUS=0
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        OG_FX_CAPTURE_DIR="$SCENE_DIR" \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$name" \
        "$@" \
        "$BUILD_DIR/$binary" --gtest_filter="$filter" \
        < /dev/null > "$SCENE_LOG" 2>&1 || SCENE_STATUS=$?
    if [ "$SCENE_STATUS" -ne 0 ] && [ "${SCENE_ALLOW_FAIL:-0}" != "1" ]; then
        printf 'scene run %s failed; see %s\n' "$name" "$SCENE_LOG" >&2
        exit 1
    fi
}

frame_path() { # frame_path <dir> <frame>
    printf '%s/frame%05d.bmp' "$1" "$2"
}

# --- conversion -------------------------------------------------------------
# BMP dumps are indexed already; palettegen/paletteuse on the single upscaled
# frame hands the PNG encoder a pal8 picture instead of 32-bit RGB.  The 2x
# nearest-neighbour upscale is what makes the 5x6 font legible on GitHub.
still2x() { # still2x <in.bmp> <out.png>
    [ -s "$1" ] || die "FAIL: expected frame $1 was never dumped"
    "${FFMPEG[@]}" -i "$1" \
        -vf 'scale=iw*2:ih*2:flags=neighbor,split[a][b];[a]palettegen=reserve_transparent=0[p];[b][p]paletteuse=dither=none' \
        -frames:v 1 -update 1 -compression_level 9 "$2"
}

# PPM captures are true colour already; a nearest-neighbour double is all they
# need.
ppm_still() { # ppm_still <in.ppm> <out.png> [scale]
    local in="$1" out="$2" scale="${3:-iw*2:ih*2}"
    [ -s "$in" ] || die "FAIL: expected capture $in was never written"
    "${FFMPEG[@]}" -i "$in" -vf "scale=$scale:flags=neighbor" \
        -frames:v 1 -update 1 -compression_level 9 "$out"
}

# A 5x6 glyph is unreadable at 1x and barely legible at 2x; the colour claims
# get their own 4x crop of the rows that hold the numbers.
ppm_crop4x() { # ppm_crop4x <in.ppm> <out.png> <crop_h> <crop_y>
    local in="$1" out="$2" ch="$3" cy="$4"
    [ -s "$in" ] || die "FAIL: expected capture $in was never written"
    "${FFMPEG[@]}" -i "$in" \
        -vf "crop=iw:$ch:0:$cy,scale=iw*4:ih*4:flags=neighbor" \
        -frames:v 1 -update 1 -compression_level 9 "$out"
}

# A 3x4 mini HP bar is invisible at 1x: a tight crop blown up with nearest
# neighbour is what makes "the bar rose" a claim a reader can check.
ppm_crop_zoom() { # ppm_crop_zoom <in.ppm> <out.png> <WxH+X+Y> <percent>
    [ -s "$1" ] || die "FAIL: expected capture $1 was never written"
    magick "$1" -crop "$3" +repage -filter point -resize "$4" \
        -alpha off -depth 8 "$2"
}

# The quantified claim for a glyph-colour change: every distinct colour inside
# a horizontal band of the capture, with its pixel count.  A flat plot shows
# one colour per number; a ramped one shows several inside the same band.
ppm_band_census() { # ppm_band_census <in.ppm> <y0> <y1>
    python3 - "$1" "$2" "$3" <<'PY'
import collections, re, sys
data = open(sys.argv[1], 'rb').read()
m = re.match(rb'P6\s+(\d+)\s+(\d+)\s+(\d+)\s', data)
w, h = int(m.group(1)), int(m.group(2))
off = m.end()
y0, y1 = int(sys.argv[2]), int(sys.argv[3])
count = collections.Counter()
for y in range(y0, y1):
    row = off + 3 * y * w
    for x in range(w):
        i = row + 3 * x
        count[(data[i], data[i + 1], data[i + 2])] += 1
total = sum(count.values())
print(f'  band y={y0}..{y1 - 1}: {total} px, {len(count)} distinct colours')
for rgb, n in sorted(count.items(), key=lambda kv: -kv[1]):
    print('    rgb%-16s %5d' % (str(rgb), n))
PY
}

# --- GIF assembly (scripts/media/capture_showcase.sh recipe) ----------------
# One concat-demuxer list line per animation frame with an explicit on-screen
# duration: a moment that should linger is a longer duration, not a repeated
# frame, because GIF stores per-frame delays natively.  <pattern> is the
# printf form of one frame file ("frame%05d.bmp" for demo dumps,
# "%03d.ppm" for the fx-capture scenes).
emit() { # emit <list> <dir> <pattern> <duration-secs> <first> <last> <step>
    local list=$1 dir=$2 pat=$3 dur=$4 first=$5 last=$6 step=$7 i
    for ((i = first; i <= last; i += step)); do
        printf "file '%s/%s'\nduration %s\n" "$dir" "$(printf "$pat" "$i")" "$dur" >> "$list"
    done
}

hold() { # hold <list> <dir> <pattern> <frame> <duration-secs>
    printf "file '%s/%s'\nduration %s\n" "$2" "$(printf "$3" "$4")" "$5" >> "$1"
}

# Two passes: palettegen collects the exact colours the frames use (indexed
# game frames have at most 256, so nothing is quantized), paletteuse maps
# every frame through that palette with no dithering.
#
# GIF_CAPTION=<png> lays that strip along the bottom of every frame (it must
# already be the encoded width, i.e. twice the source frame): a side-by-side
# animation that leaves the repository has no PR body around it to say which
# half is which.
encode_gif() { # encode_gif <list> <out.gif> <final-delay-cs>
    local list=$1 out=$2 final=$3 palette="$WORK_DIR/palette.png"
    [ -s "$list" ] || die "FAIL: $out has an empty frame list"
    local chain='[0:v]scale=iw*2:ih*2:flags=neighbor[x]'
    local -a extra=()
    local palette_input=1
    if [ -n "${GIF_CAPTION:-}" ]; then
        [ -s "$GIF_CAPTION" ] || die "FAIL: caption strip $GIF_CAPTION is missing"
        local cap_h
        cap_h="$(magick identify -format '%h' "$GIF_CAPTION")"
        extra=(-loop 1 -i "$GIF_CAPTION")
        palette_input=2
        chain="[0:v]scale=iw*2:ih*2:flags=neighbor,pad=iw:ih+$cap_h:0:0:color=0x202028[p];"
        chain+="[p][1:v]overlay=0:main_h-$cap_h:shortest=1[x]"
    fi
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" ${extra[@]+"${extra[@]}"} \
        -lavfi "$chain;[x]palettegen" "$palette"
    "${FFMPEG[@]}" -f concat -safe 0 -i "$list" ${extra[@]+"${extra[@]}"} -i "$palette" \
        -lavfi "$chain;[x][$palette_input:v]paletteuse=dither=none" \
        -final_delay "$final" "$out"
    rm -f -- "$palette"
}

# A 150-frame 640x400 gameplay capture lands near 3.6 MB, well over the band
# every shipped animation sits in.  Halving the frame rate keeps the same
# wall-clock cut inside it: a floating number lingers for dozens of ticks, so
# nothing in this scene becomes unreadable at 6.25 fps.
gif_to_budget() { # gif_to_budget <in.gif> <out.gif> <fps>
    local in=$1 out=$2 fps=$3 palette="$WORK_DIR/palette.png"
    [ -s "$in" ] || die "FAIL: $in was never produced"
    "${FFMPEG[@]}" -i "$in" -vf "fps=$fps,palettegen" "$palette"
    "${FFMPEG[@]}" -i "$in" -i "$palette" \
        -lavfi "[0:v]fps=$fps[x];[x][1:v]paletteuse=dither=none" \
        -final_delay 100 "$out"
    rm -f -- "$palette"
}

# --- composition ------------------------------------------------------------
# A before/after pair reads as one picture only when the two halves sit on one
# canvas with a visible seam between them.
# Each half carries its own caption strip, because a reader who meets the
# image outside the PR body has no other way to tell which side is which.
caption() { # caption <image> <text> <out>
    local font w
    font="$(find_mono_font)" || die 'no DejaVuSansMono.ttf found (enter `nix develop`)'
    w="$(magick identify -format '%w' "$1")"
    magick "$1" \
        \( -background '#202028' -fill '#d8dee9' -font "$font" -pointsize 18 \
           -size "${w}x28" -gravity center label:"$2" \) \
        -background '#202028' -append -alpha off -depth 8 "$3"
}

side_by_side() { # side_by_side <before> <after> <out>
    [ -s "$1" ] || die "FAIL: $1 is missing (capture the before phase first)"
    [ -s "$2" ] || die "FAIL: $2 is missing (capture the after phase first)"
    caption "$1" "BEFORE  ${BEFORE_CAPTION:-base tree}" "$WORK_DIR/cap-before.png"
    caption "$2" "AFTER  ${AFTER_CAPTION:-fixed tree}" "$WORK_DIR/cap-after.png"
    magick "$WORK_DIR/cap-before.png" "$WORK_DIR/cap-after.png" \
        -background '#202028' +smush 6 -alpha off -depth 8 "$3"
    rm -f -- "$WORK_DIR/cap-before.png" "$WORK_DIR/cap-after.png"
}

# A pair of wide, short crops reads better stacked: the glyphs keep their x
# positions, so the eye compares them column for column.
stack_vertical() { # stack_vertical <before> <after> <out>
    [ -s "$1" ] || die "FAIL: $1 is missing (capture the before phase first)"
    [ -s "$2" ] || die "FAIL: $2 is missing (capture the after phase first)"
    caption "$1" "BEFORE  ${BEFORE_CAPTION:-base tree}" "$WORK_DIR/cap-before.png"
    caption "$2" "AFTER  ${AFTER_CAPTION:-fixed tree}" "$WORK_DIR/cap-after.png"
    magick "$WORK_DIR/cap-before.png" "$WORK_DIR/cap-after.png" \
        -background '#202028' -smush 6 -alpha off -depth 8 "$3"
    rm -f -- "$WORK_DIR/cap-before.png" "$WORK_DIR/cap-after.png"
}

# The quantified claim: how many pixels the fix moved, and where.
pixel_diff() { # pixel_diff <before> <after> [diff.png]
    local a="$1" b="$2" out="${3:-null:}" ae
    ae="$(magick compare -metric AE "$a" "$b" "$out" 2>&1 >/dev/null || true)"
    printf '%s' "${ae%% *}"
}

# --- scenes -----------------------------------------------------------------

# Harness self-check.  Two one-frame captures of the same seeded level, aimed
# at two different map cells: they must differ, which proves the cell camera
# reached the game and not just the environment block.
scene_selftest() {
    local near="$OUT_DIR/k0-selftest-origin-$PHASE.png"
    local far="$OUT_DIR/k0-selftest-$PHASE.png"

    run_demo selftest-origin gladiator 5 4 'cell:0,0' 7 1
    still2x "$(frame_path "$FRAME_DIR" 0)" "$near"

    run_demo selftest-far gladiator 5 4 'cell:20,20' 7 1
    still2x "$(frame_path "$FRAME_DIR" 0)" "$far"

    if cmp -s "$near" "$far"; then
        die "FAIL: the cell camera produced identical frames at 0,0 and 20,20"
    fi
    expect_png "$near" 640 400 pal8
    expect_png "$far" 640 400 pal8
}

compose_selftest() { :; }   # a self-check ships nothing

# P1 — the alpha text blitter.  sdl_video::walkputbuffertext_alpha used to
# compute a ramped curcolor for every source byte above 247 and then plot the
# flat teamcolor anyway; it now runs every byte through the same text_ink rule
# the opaque blitter uses.  Its ONLY production consumer is the floating
# damage/heal number (draw_damage_number -> text::write_xy_center_alpha), so
# the proof is a scripted render scene: og_test_rendering's
# RenderEffects.damage_number_glyphs_paint_in_the_requested_band draws five
# numbers (heal green 56, attacker orange 235, target RED, then the same "99"
# opaque and at half alpha) on flat backing strips and dumps the 320x200
# viewport as a P6 PPM.
P1_PPM=""
scene_p1() {
    run_scene_test p1 og_test_rendering \
        'RenderEffects.damage_number_glyphs_paint_in_the_requested_band'
    P1_PPM="$SCENE_DIR/damage_numbers_alpha/000.ppm"
    ppm_still "$P1_PPM" "$OUT_DIR/p1-damage-numbers-$PHASE.png"
    expect_png "$OUT_DIR/p1-damage-numbers-$PHASE.png" 640 400
    # The numbers sit on backing strips spanning rows 10..19 of the viewport;
    # rows 6..21 give them a little air at 4x.
    ppm_crop4x "$P1_PPM" "$OUT_DIR/p1-damage-numbers-$PHASE-crop-4x.png" 16 6
    expect_png "$OUT_DIR/p1-damage-numbers-$PHASE-crop-4x.png" 1280 64
    printf 'p1-damage-numbers (%s): glyph colours\n' "$PHASE"
    ppm_band_census "$P1_PPM" 10 20 | tee "$OUT_DIR/p1-band-census-$PHASE.txt"
}

compose_p1() {
    local b="$BEFORE_DIR/p1-damage-numbers-before.png"
    local a="$AFTER_DIR/p1-damage-numbers-after.png"
    side_by_side "$b" "$a" "$FINAL_DIR/p1-damage-numbers-side-by-side.png"
    stack_vertical "$BEFORE_DIR/p1-damage-numbers-before-crop-4x.png" \
        "$AFTER_DIR/p1-damage-numbers-after-crop-4x.png" \
        "$FINAL_DIR/p1-damage-numbers-crop-4x.png"
    cp -- "$b" "$FINAL_DIR/p1-damage-numbers-before.png"
    cp -- "$a" "$FINAL_DIR/p1-damage-numbers-after.png"
    local ae total pct
    ae="$(pixel_diff "$b" "$a")"
    total="$(magick identify -format '%[fx:w*h]' "$a")"
    pct="$(python3 -c "print(f'{100.0*$ae/$total:.2f}')")"
    printf 'p1: %s of %s pixels moved (%s %%)\n' "$ae" "$total" "$pct" \
        | tee "$FINAL_DIR/p1-pixel-diff.txt"
    expect_png "$FINAL_DIR/p1-damage-numbers-side-by-side.png" 1286 428
    expect_png "$FINAL_DIR/p1-damage-numbers-crop-4x.png" 1280 190
    expect_png "$FINAL_DIR/p1-damage-numbers-before.png" 640 400
    expect_png "$FINAL_DIR/p1-damage-numbers-after.png" 640 400
}

# P5 — a low-magic AI cleric heals a hurt ally, or (before the fix) silently
# refuses to.  The tells are motion (the patient's HP bar refilling) and the
# replicated "Cleric healed 1 man!" line, so this one is a GIF, not a still.
# Every 2nd frame of the 240-frame scene at 0.08 s is the 240 sim ticks played
# back at roughly real speed; the frame the heal first lands on is held for
# 0.6 s so the line is readable.  The refusal cut has no such frame (the scene
# reports heal_frame=-1) and runs straight through.
scene_p5() {
    SCENE_ALLOW_FAIL=1 run_scene_test p5 og_test_game_core \
        'GameLoop.zz_capture_cleric_low_magic_heal'
    local frames="$SCENE_DIR/cleric_low_magic_heal"
    [ -s "$frames/000.ppm" ] \
        || die "FAIL: the P5 scene dumped no frames; see $SCENE_LOG"
    local summary heal_frame
    summary="$(grep -m1 '^p5 clinic: ' "$SCENE_LOG" || echo 'no scene summary')"
    printf 'p5 (%s): %s\n' "$PHASE" "$summary" | tee "$OUT_DIR/p5-summary-$PHASE.txt"
    heal_frame="$(sed -n 's/^p5 clinic: .*heal_frame=\(-\{0,1\}[0-9]\{1,\}\).*/\1/p' \
        "$SCENE_LOG" | head -n 1)"
    heal_frame="${heal_frame:--1}"

    local list="$WORK_DIR/p5.ffconcat"
    : > "$list"
    if [ "$heal_frame" -ge 1 ]; then
        emit "$list" "$frames" '%03d.ppm' 0.08 0 "$((heal_frame - 1))" 2
        hold "$list" "$frames" '%03d.ppm' "$heal_frame" 0.6
        emit "$list" "$frames" '%03d.ppm' 0.08 "$((heal_frame + 1))" 238 2
    else
        emit "$list" "$frames" '%03d.ppm' 0.08 0 238 2
    fi
    encode_gif "$list" "$OUT_DIR/p5-cleric-low-magic-$PHASE.gif" 8
    expect_gif "$OUT_DIR/p5-cleric-low-magic-$PHASE.gif"
    # The raw PPM directory is what the side-by-side composition consumes.
    rm -rf -- "$OUT_DIR/p5-frames"
    cp -r -- "$frames" "$OUT_DIR/p5-frames"
}

compose_p5() {
    local bf="$BEFORE_DIR/p5-frames" af="$AFTER_DIR/p5-frames"
    [ -d "$bf" ] || die "FAIL: $bf is missing (capture the before phase first)"
    [ -d "$af" ] || die "FAIL: $af is missing (capture the after phase first)"
    local pair="$WORK_DIR/p5-pair"
    rm -rf -- "$pair"
    python3 - "$bf" "$af" "$pair" <<'PY'
import sys
sys.path.insert(0, 'scripts/fx_review')
from make_site import compose_side_by_side
print('p5 side-by-side frames:', compose_side_by_side(sys.argv[1], sys.argv[2], sys.argv[3]))
PY
    local list="$WORK_DIR/p5-pair.ffconcat"
    : > "$list"
    emit "$list" "$pair" '%03d.ppm' 0.08 0 238 2
    # The strip is built at the encoded width (the frames are doubled first).
    local pair_w font cap="$WORK_DIR/p5-caption.png"
    pair_w="$(magick identify -format '%w' "$pair/000.ppm")"
    font="$(find_mono_font)" || die 'no DejaVuSansMono.ttf found (enter `nix develop`)'
    magick -background '#202028' -fill '#d8dee9' -font "$font" -pointsize 18 \
        -size "$((pair_w * 2))x28" -gravity center \
        label:"BEFORE  ${BEFORE_CAPTION:-base tree}          AFTER  ${AFTER_CAPTION:-fixed tree}" \
        -alpha off -depth 8 "$cap"
    GIF_CAPTION="$cap" \
        encode_gif "$list" "$FINAL_DIR/p5-cleric-low-magic-side-by-side.gif" 8
    # The last frame of each cut, cropped to the patient and his mini HP bar
    # at 10x: the GIF shows the moment, this shows the outcome standing still.
    ppm_crop_zoom "$bf/239.ppm" "$WORK_DIR/p5-zoom-before.png" '60x30+135+85' 1000%
    ppm_crop_zoom "$af/239.ppm" "$WORK_DIR/p5-zoom-after.png" '60x30+135+85' 1000%
    side_by_side "$WORK_DIR/p5-zoom-before.png" "$WORK_DIR/p5-zoom-after.png" \
        "$FINAL_DIR/p5-cleric-low-magic-hp-bar-crop-10x.png"
    expect_png "$FINAL_DIR/p5-cleric-low-magic-hp-bar-crop-10x.png" 1206 328

    cp -- "$BEFORE_DIR/p5-cleric-low-magic-before.gif" "$FINAL_DIR/"
    cp -- "$AFTER_DIR/p5-cleric-low-magic-after.gif" "$FINAL_DIR/"
    expect_gif "$FINAL_DIR/p5-cleric-low-magic-side-by-side.gif"
    expect_gif "$FINAL_DIR/p5-cleric-low-magic-before.gif"
    expect_gif "$FINAL_DIR/p5-cleric-low-magic-after.gif"
}

# P8 — the curses lobby is the one place the start-denial correlation is
# user-visible, and a HeadlessTerminal transcript of the real two-client
# regression test is the proof: the test writes host_term/join_term through
# tests/curses/transcript_capture.h whenever OG_FX_CAPTURE_DIR is set, so no
# capture-only code path exists.
#
# BEFORE: the joiner pressed 's' and its status band stays EMPTY while the
# host's band carries "Waiting for other machines".
# AFTER: the joiner's band reads "Only the host can start" and the host's own
# denial stays on the host's band.
scene_p8() {
    local font
    font="$(find_mono_font)" || die 'no DejaVuSansMono.ttf found (enter `nix develop`)'
    SCENE_ALLOW_FAIL=1 run_scene_test p8 og_test_curses \
        'CursesNetwork.joiner_start_request_is_denied_as_not_host' \
        OG_FX_PHASE="$PHASE"
    if [ "$SCENE_STATUS" -ne 0 ]; then
        printf 'note: the P8 test is red on this tree (expected on the base tree); '
        printf 'the transcripts were still written\n'
    fi
    local side txt png
    for side in host join; do
        txt="$SCENE_DIR/p8-curses-$side-$PHASE.txt"
        [ -s "$txt" ] || die "FAIL: no transcript at $txt; see $SCENE_LOG"
        cp -- "$txt" "$OUT_DIR/p8-curses-$side-$PHASE.txt"
        magick -background '#101418' -fill '#d8dee9' -font "$font" -pointsize 14 \
            "label:@$txt" "$OUT_DIR/p8-curses-$side-$PHASE.png"
    done
    # One canvas for both sides so a later +smush lines the two bands up row
    # for row.
    local w h
    w="$(magick identify -format '%w\n' "$OUT_DIR"/p8-curses-*-"$PHASE".png | sort -n | tail -1)"
    h="$(magick identify -format '%h\n' "$OUT_DIR"/p8-curses-*-"$PHASE".png | sort -n | tail -1)"
    for side in host join; do
        png="$OUT_DIR/p8-curses-$side-$PHASE.png"
        magick "$png" -background '#101418' -gravity northwest \
            -extent "${w}x${h}" -depth 8 "$png"
        expect_png "$png" "$w" "$h"
    done
    printf 'p8 (%s) status bands:\n' "$PHASE"
    grep -n 'Waiting for other machines\|Only the host can start' \
        "$OUT_DIR"/p8-curses-*-"$PHASE".txt || printf '  (neither band carries a verdict)\n'
}

compose_p8() {
    local side
    for side in host join; do
        side_by_side "$BEFORE_DIR/p8-curses-$side-before.png" \
            "$AFTER_DIR/p8-curses-$side-after.png" \
            "$FINAL_DIR/p8-curses-$side-side-by-side.png"
        cp -- "$BEFORE_DIR/p8-curses-$side-before.png" "$FINAL_DIR/"
        cp -- "$AFTER_DIR/p8-curses-$side-after.png" "$FINAL_DIR/"
        cp -- "$BEFORE_DIR/p8-curses-$side-before.txt" "$FINAL_DIR/"
        cp -- "$AFTER_DIR/p8-curses-$side-after.txt" "$FINAL_DIR/"
    done
    # Two pairs ship, not one: the joiner band is where the fix shows, and the
    # host band is the control that proves the host's own verdict did not move.
    local w h
    for side in host join; do
        w="$(magick identify -format '%w' "$FINAL_DIR/p8-curses-$side-side-by-side.png")"
        h="$(magick identify -format '%h' "$FINAL_DIR/p8-curses-$side-side-by-side.png")"
        expect_png "$FINAL_DIR/p8-curses-$side-side-by-side.png" "$w" "$h"
    done
}

# P13 — the floating damage/heal numbers.  The 2013 overlay stopped painting
# on every client when the networking merge made the display render a MIRROR
# world: the sim pushed the numbers onto the authoritative walkers and
# apply_snapshot cleared the mirror's copy, so the OPTIONS > EFFECTS toggle
# changed nothing at all.  The capture is two identical lockstep demo runs,
# toggle on against toggle off, and it lives in its own script — this harness
# runs that one implementation rather than restating the recipe.
#
# OPENGLAD_P13_DIR (usage header) takes the phase's assets from a directory
# instead of running the demo; the "before" half is always taken that way,
# because only the base tree's openglad_demo can film it.
scene_p13() {
    if [ -n "${OPENGLAD_P13_DIR:-}" ]; then
        local crop="$OPENGLAD_P13_DIR/p13-first-hit-crop-4x-$PHASE.png"
        [ -f "$crop" ] || crop="$OPENGLAD_P13_DIR/p13-first-hit-crop-4x.png"
        cp -- "$OPENGLAD_P13_DIR/p13-damage-numbers-$PHASE.gif" \
            "$OUT_DIR/p13-damage-numbers-$PHASE.gif"
        cp -- "$OPENGLAD_P13_DIR/p13-first-hit-$PHASE.png" \
            "$OUT_DIR/p13-first-hit-$PHASE.png"
        cp -- "$crop" "$OUT_DIR/p13-first-hit-crop-4x-$PHASE.png"
        printf 'p13 (%s): reused the assets in %s\n' "$PHASE" "$OPENGLAD_P13_DIR"
    else
        LABEL="$PHASE" scripts/media/capture_damage_numbers.sh "$OUT_DIR"
        # That script names its crop without a phase; both halves live here.
        mv -- "$OUT_DIR/p13-first-hit-crop-4x.png" \
            "$OUT_DIR/p13-first-hit-crop-4x-$PHASE.png"
    fi
    expect_png "$OUT_DIR/p13-first-hit-$PHASE.png" 640 400
    expect_png "$OUT_DIR/p13-first-hit-crop-4x-$PHASE.png" 336 240
    [ -s "$OUT_DIR/p13-damage-numbers-$PHASE.gif" ] \
        || die "FAIL: no P13 animation for the $PHASE phase"
}

compose_p13() {
    gif_to_budget "$BEFORE_DIR/p13-damage-numbers-before.gif" \
        "$FINAL_DIR/p13-damage-numbers-before.gif" 6.25
    gif_to_budget "$AFTER_DIR/p13-damage-numbers-after.gif" \
        "$FINAL_DIR/p13-damage-numbers-after.gif" 6.25
    side_by_side "$BEFORE_DIR/p13-first-hit-before.png" \
        "$AFTER_DIR/p13-first-hit-after.png" \
        "$FINAL_DIR/p13-first-hit-side-by-side.png"
    side_by_side "$BEFORE_DIR/p13-first-hit-crop-4x-before.png" \
        "$AFTER_DIR/p13-first-hit-crop-4x-after.png" \
        "$FINAL_DIR/p13-first-hit-crop-4x.png"
    expect_gif "$FINAL_DIR/p13-damage-numbers-before.gif"
    expect_gif "$FINAL_DIR/p13-damage-numbers-after.gif"
    expect_png "$FINAL_DIR/p13-first-hit-side-by-side.png" 1286 428
    expect_png "$FINAL_DIR/p13-first-hit-crop-4x.png" 678 268
}

# Q15 - the local spectator's follow camera.  The scene test drives the real
# frame path (game_frame_with_result -> run_game_tick) for 121 frames with a
# one-frame SwitchChar press on frame 30, dumping the 320x200 viewport every
# frame, and prints the summary line this recipe reads back.
#
# BEFORE: the camera never leaves the walker it started on (the install binds
# seat 0 to it, and every snapshot apply re-resolves view->control back to that
# seat before the frame renders), the walker itself stands frozen at its spawn
# tile, and the classic HUD is drawn because the mirror walker wears a user
# tag.  Summary: frames_on_new_target=0/90 hero_acted=0.
# AFTER: the press moves the camera to the next watchable hero and it stays
# there through the tick-60 keyframe, the view carries the FOLLOWING caption
# and no HUD/radar, and the walker the camera left walks off as AI.
# Summary: frames_on_new_target=90/90 hero_acted=1.
Q15_SWITCH_FRAME=30
Q15_LAST_FRAME=120
scene_q15() {
    local frames summary
    if [ -n "${OPENGLAD_Q15_DIR:-}" ]; then
        # Re-encode a cut whose frames are already on disk.  Only the base
        # tree's og_test_game_core can dump the "before" frames, so on a
        # merged checkout --before always needs this (the p13 precedent).
        frames="$OPENGLAD_Q15_DIR/q15-frames"
        [ -d "$frames" ] || die "FAIL: $frames is missing"
        summary="$(cat "$OPENGLAD_Q15_DIR/q15-summary-$PHASE.txt" 2> /dev/null \
            || echo 'no scene summary')"
        summary="${summary#q15 ($PHASE): }"
    else
        SCENE_ALLOW_FAIL=1 run_scene_test q15 og_test_game_core \
            'GameLoop.zz_capture_spectator_follow'
        frames="$SCENE_DIR/spectator_follow"
        summary="$(grep -m1 '^q15 spectator: ' "$SCENE_LOG" \
            || echo 'no scene summary')"
    fi
    [ -s "$frames/000.ppm" ] \
        || die "FAIL: the Q15 scene dumped no frames; see ${SCENE_LOG:-$frames}"
    printf 'q15 (%s): %s\n' "$PHASE" "$summary" | tee "$OUT_DIR/q15-summary-$PHASE.txt"

    local list="$WORK_DIR/q15.ffconcat"
    : > "$list"
    # Every 2nd frame at 0.08 s is the 121 sim ticks played back at roughly
    # real speed.  The AFTER cut is a moving camera, i.e. no two frames alike:
    # at every frame it lands near 5 MB, well over the band every shipped
    # animation sits in, so the cut is halved here and halved again by
    # gif_to_budget rather than being quantized or cropped.
    emit "$list" "$frames" '%03d.ppm' 0.08 0 "$((Q15_SWITCH_FRAME - 2))" 2
    # The key frame is held so a reader can see WHERE the camera was when the
    # key was pressed; every frame after it is the claim.
    hold "$list" "$frames" '%03d.ppm' "$Q15_SWITCH_FRAME" 0.6
    emit "$list" "$frames" '%03d.ppm' 0.08 "$((Q15_SWITCH_FRAME + 2))" \
        "$Q15_LAST_FRAME" 2
    encode_gif "$list" "$WORK_DIR/q15-raw-$PHASE.gif" 8
    gif_to_budget "$WORK_DIR/q15-raw-$PHASE.gif" \
        "$OUT_DIR/q15-spectator-follow-$PHASE.gif" 6.25
    expect_gif "$OUT_DIR/q15-spectator-follow-$PHASE.gif"
    # The raw PPM directory is what the side-by-side composition consumes.
    rm -rf -- "$OUT_DIR/q15-frames"
    cp -r -- "$frames" "$OUT_DIR/q15-frames"
}

compose_q15() {
    local bf="$BEFORE_DIR/q15-frames" af="$AFTER_DIR/q15-frames"
    [ -d "$bf" ] || die "FAIL: $bf is missing (capture the before phase first)"
    [ -d "$af" ] || die "FAIL: $af is missing (capture the after phase first)"
    local pair="$WORK_DIR/q15-pair"
    rm -rf -- "$pair"
    python3 - "$bf" "$af" "$pair" <<'Q15PY'
import sys
sys.path.insert(0, 'scripts/fx_review')
from make_site import compose_side_by_side
print('q15 side-by-side frames:', compose_side_by_side(sys.argv[1], sys.argv[2], sys.argv[3]))
Q15PY
    local list="$WORK_DIR/q15-pair.ffconcat"
    : > "$list"
    emit "$list" "$pair" '%03d.ppm' 0.08 0 "$((Q15_SWITCH_FRAME - 2))" 2
    hold "$list" "$pair" '%03d.ppm' "$Q15_SWITCH_FRAME" 0.6
    emit "$list" "$pair" '%03d.ppm' 0.08 "$((Q15_SWITCH_FRAME + 2))" \
        "$Q15_LAST_FRAME" 2
    # The strip is built at the encoded width (the frames are doubled first).
    local pair_w font cap="$WORK_DIR/q15-caption.png"
    pair_w="$(magick identify -format '%w' "$pair/000.ppm")"
    font="$(find_mono_font)" || die 'no DejaVuSansMono.ttf found (enter `nix develop`)'
    magick -background '#202028' -fill '#d8dee9' -font "$font" -pointsize 18 \
        -size "$((pair_w * 2))x28" -gravity center \
        label:"BEFORE  ${BEFORE_CAPTION:-base tree}          AFTER  ${AFTER_CAPTION:-fixed tree}" \
        -alpha off -depth 8 "$cap"
    GIF_CAPTION="$cap" \
        encode_gif "$list" "$WORK_DIR/q15-pair-raw.gif" 8
    gif_to_budget "$WORK_DIR/q15-pair-raw.gif" \
        "$FINAL_DIR/q15-spectator-follow-side-by-side.gif" 6.25
    # The static outcome: the bottom strip of the last frame at 10x.  The
    # BEFORE half carries the classic HUD row, the AFTER half the section 2.8
    # "FOLLOWING <name>" caption (score_panel.cpp) and nothing else.
    local last
    last="$(printf '%03d.ppm' "$Q15_LAST_FRAME")"
    ppm_crop_zoom "$bf/$last" "$WORK_DIR/q15-zoom-before.png" '160x24+80+174' 1000%
    ppm_crop_zoom "$af/$last" "$WORK_DIR/q15-zoom-after.png" '160x24+80+174' 1000%
    stack_vertical "$WORK_DIR/q15-zoom-before.png" "$WORK_DIR/q15-zoom-after.png" \
        "$FINAL_DIR/q15-follow-caption-crop-10x.png"

    cp -- "$BEFORE_DIR/q15-spectator-follow-before.gif" "$FINAL_DIR/"
    cp -- "$AFTER_DIR/q15-spectator-follow-after.gif" "$FINAL_DIR/"
    cp -- "$BEFORE_DIR/q15-summary-before.txt" "$FINAL_DIR/"
    cp -- "$AFTER_DIR/q15-summary-after.txt" "$FINAL_DIR/"
    expect_png "$FINAL_DIR/q15-follow-caption-crop-10x.png" 1600 542
    expect_gif "$FINAL_DIR/q15-spectator-follow-side-by-side.gif"
    expect_gif "$FINAL_DIR/q15-spectator-follow-before.gif"
    expect_gif "$FINAL_DIR/q15-spectator-follow-after.gif"
}

# --- contact sheet ----------------------------------------------------------
# One page of every still that ships, for the review pass.
# Q2 — the TEAM/FOES counter box with a pending wave.  The scene test renders
# one classic 320x200 frame with 12 awake foes, a 34-strong wave 120 s out,
# and dumps it; the fix moves both wave rows out of the world and onto a box
# sized to hold them.
Q2_PPM=""
scene_q2() {
    SCENE_ALLOW_FAIL=1 run_scene_test q2 og_test_view \
        'GladHud.zz_capture_pending_wave_counter_box'
    Q2_PPM="$SCENE_DIR/pending_wave_counter_box/000.ppm"
    ppm_still "$Q2_PPM" "$OUT_DIR/q2-counter-box-$PHASE.png"
    expect_png "$OUT_DIR/q2-counter-box-$PHASE.png" 640 400
    # The box occupies rows 1..24 of the pane; rows 0..33 give it some air.
    ppm_crop4x "$Q2_PPM" "$OUT_DIR/q2-counter-box-$PHASE-crop-4x.png" 34 0
    expect_png "$OUT_DIR/q2-counter-box-$PHASE-crop-4x.png" 1280 136
}

compose_q2() {
    local b="$BEFORE_DIR/q2-counter-box-before.png"
    local a="$AFTER_DIR/q2-counter-box-after.png"
    side_by_side "$b" "$a" "$FINAL_DIR/q2-counter-box-side-by-side.png"
    stack_vertical "$BEFORE_DIR/q2-counter-box-before-crop-4x.png" \
        "$AFTER_DIR/q2-counter-box-after-crop-4x.png" \
        "$FINAL_DIR/q2-counter-box-crop-4x.png"
    cp -- "$b" "$FINAL_DIR/q2-counter-box-before.png"
    cp -- "$a" "$FINAL_DIR/q2-counter-box-after.png"
    local ae total pct
    ae="$(pixel_diff "$b" "$a")"
    total="$(magick identify -format '%[fx:w*h]' "$a")"
    pct="$(python3 -c "print(f'{100.0*$ae/$total:.2f}')")"
    printf 'q2: %s of %s pixels moved (%s %%)\n' "$ae" "$total" "$pct" \
        | tee "$FINAL_DIR/q2-pixel-diff.txt"
    expect_png "$FINAL_DIR/q2-counter-box-side-by-side.png" 1286 428
    expect_png "$FINAL_DIR/q2-counter-box-crop-4x.png" 1280 334
    expect_png "$FINAL_DIR/q2-counter-box-before.png" 640 400
    expect_png "$FINAL_DIR/q2-counter-box-after.png" 640 400
}

contact_sheet() {
    local -a stills=()
    local f
    for f in "$FINAL_DIR"/*.png; do
        case "$f" in
            *contact-sheet*) continue ;;
        esac
        [ -e "$f" ] && stills+=("$f")
    done
    [ "${#stills[@]}" -gt 0 ] || return 0
    # montage's -label needs a real face: the dev shell has no fontconfig
    # default, so the DejaVu file the transcripts already use is named here too.
    local font
    font="$(find_mono_font)" || die 'no DejaVuSansMono.ttf found (enter `nix develop`)'
    magick montage "${stills[@]}" -tile 2x -geometry '600x340+8+8' \
        -background '#202028' -fill '#d8dee9' -font "$font" -pointsize 13 \
        -label '%f' "$FINAL_DIR/pr-292-contact-sheet.png"
    printf 'contact sheet: %s (%s stills)\n' \
        "$FINAL_DIR/pr-292-contact-sheet.png" "${#stills[@]}"
}

for scene in "${SELECTED[@]}"; do
    printf '=== scene %s (%s) ===\n' "$scene" "$PHASE"
    if [ "$PHASE" = "compose" ]; then
        "compose_$scene"
    else
        "scene_$scene"
    fi
done
if [ "$PHASE" = "compose" ]; then
    contact_sheet
fi

# --- verification tail ------------------------------------------------------
verify_png() { # verify_png <file> <width> <height> [pix_fmt]
    local file="$1" want_w="$2" want_h="$3" want_fmt="${4:-}" probed
    [ -s "$file" ] || die "FAIL: $file was never produced"
    probed="$(ffprobe -loglevel error -select_streams v:0 \
        -show_entries stream=codec_name,width,height,pix_fmt \
        -of csv=p=0 "$file")"
    local codec width height fmt
    IFS=, read -r codec width height fmt <<< "$probed"
    [ "$codec" = "png" ] || die "FAIL: $file decoded as $codec, expected png"
    { [ "$width" = "$want_w" ] && [ "$height" = "$want_h" ]; } \
        || die "FAIL: $file is ${width}x${height}, expected ${want_w}x${want_h}"
    if [ -n "$want_fmt" ] && [ "$fmt" != "$want_fmt" ]; then
        die "FAIL: $file is $fmt, expected $want_fmt"
    fi
    printf 'ok %s (%sx%s %s)\n' "$file" "$width" "$height" "$fmt"
}

verify_gif() { # verify_gif <file>
    local file="$1" frames bytes
    [ -s "$file" ] || die "FAIL: $file was never produced"
    frames="$(ffprobe -loglevel error -select_streams v:0 -count_frames \
        -show_entries stream=nb_read_frames -of csv=p=0 "$file")"
    [ "${frames:-0}" -ge 2 ] \
        || die "FAIL: $file holds $frames frame(s); an animation needs at least 2"
    bytes="$(stat -c %s "$file")"
    [ "$bytes" -le "$GIF_MAX_BYTES" ] \
        || die "FAIL: $file is $bytes bytes, over the $GIF_MAX_BYTES budget"
    printf 'ok %s (%s frames, %s bytes)\n' "$file" "$frames" "$bytes"
}

printf '=== verifying %s ===\n' "$OUT_DIR"
if [ "${#EXPECT_PNG[@]}" -eq 0 ] && [ "${#EXPECT_GIF[@]}" -eq 0 ]; then
    die 'FAIL: the selected scenes declared no output'
fi
for row in ${EXPECT_PNG[@]+"${EXPECT_PNG[@]}"}; do
    IFS='|' read -r path w h fmt <<< "$row"
    verify_png "$path" "$w" "$h" "$fmt"
done
for path in ${EXPECT_GIF[@]+"${EXPECT_GIF[@]}"}; do
    verify_gif "$path"
done

# A headless run that cannot reach its scratch config directory falls back to
# the working directory and rewrites the tracked cfg/openglad.yaml.  That has
# cost enough debugging time to be worth failing over.
if command -v git > /dev/null && [ -e "$REPO_ROOT/.git" ]; then
    cfg_dirty="$(git -C "$REPO_ROOT" status --porcelain cfg/)"
    if [ -n "$cfg_dirty" ]; then
        printf 'FAIL: a capture run dirtied cfg/:\n%s\n' "$cfg_dirty" >&2
        exit 1
    fi
fi

ls -l "$OUT_DIR"
