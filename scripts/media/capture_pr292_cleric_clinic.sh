#!/usr/bin/env bash
# PR #292 / P5 proof media: a low-magic cleric heals — or, before the fix,
# silently refuses to.
#
# Two GIFs from the SAME seeded lockstep openglad_demo run of the dev-only
# Concept Playground level 600 ("Stairs"), staged by the campaign-var rig in
# campaigns/concept/packs/concept.showcase/scripts/clinic.lua: a level-4
# cleric pinned to 3 MP casts HEAL every 60 ticks at a soldier hurt to 20 of
# 120 hitpoints.
#
#   cleric-lowmp-before.gif  the pre-fix pack: no toast, a flat HP bar
#   cleric-lowmp-after.gif   this branch:      a toast every 60 ticks and the
#                            bar climbing 20 -> 120, then quiet
#
# The two runs differ ONLY by which packs/core/families/living-05-cleric.lua
# is staged next to the binary: the "before" pass copies the version from the
# parent of the fix commit into the build tree's staged packs/ and copies the
# repository's own back afterwards. Nothing is rebuilt between the runs, so
# the frames are identical up to the first cast on tick 30.
#
# Output lands in build/media/pr-292/ (gitignored); the finished artifacts get
# committed to the openglad/openglad-screenshots repo under pr-292/, never to
# this one (AGENTS.md, "PR screenshots and proof media").
#
# Usage: scripts/media/capture_pr292_cleric_clinic.sh [output-dir]
#   OPENGLAD_BUILD=<dir>            build tree (default build/ci-test)
#   OPENGLAD_PR292_PREFIX_REV=<rev> the tree the "before" pack comes from
#                                   (default: the fix commit's parent)
#
# ffmpeg/ffprobe come from the dev shell (`nix develop`, see flake.nix); no
# encoder is hand-rolled here.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="${OPENGLAD_BUILD:-$REPO_ROOT/build/ci-test}"
DEMO_BIN="${OPENGLAD_DEMO:-$BUILD/openglad_demo}"
OUT_DIR="${1:-$REPO_ROOT/build/media/pr-292}"

# The one file the two passes disagree about.
CLERIC_REL="packs/core/families/living-05-cleric.lua"
STAGED_CLERIC="$BUILD/$CLERIC_REL"

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

[ -x "$DEMO_BIN" ] \
    || die "$DEMO_BIN not found; build it first:
  cmake --build --preset ci-test --target openglad_demo"
for tool in ffmpeg ffprobe; do
    command -v "$tool" > /dev/null \
        || die "$tool not found; enter the dev shell first: nix develop"
done
[ -f "$STAGED_CLERIC" ] \
    || die "$STAGED_CLERIC is missing; the build stages packs/ next to the binary"

# The pre-fix cleric. Resolved from the fix commit by its message so the
# recipe keeps working after a rebase; override with OPENGLAD_PR292_PREFIX_REV.
PREFIX_REV="${OPENGLAD_PR292_PREFIX_REV:-}"
if [ -z "$PREFIX_REV" ]; then
    fix_rev="$(git -C "$REPO_ROOT" log --format=%H -n 1 \
        --grep='W-C1-P5-cleric-heal-floor' || true)"
    [ -n "$fix_rev" ] \
        || die 'cannot find the P5 fix commit; pass OPENGLAD_PR292_PREFIX_REV=<rev>'
    PREFIX_REV="$fix_rev^"
fi
git -C "$REPO_ROOT" cat-file -e "$PREFIX_REV:$CLERIC_REL" 2> /dev/null \
    || die "$PREFIX_REV does not carry $CLERIC_REL"

WORK_DIR="$(mktemp -d)"

# EVERY exit path puts the repository's own pack back next to the binary: a
# staged file left behind would silently contaminate every gate that runs a
# build-tree binary (CMakeLists.txt, "stage_runtime_assets").
restore_staged_packs() {
    cp -- "$REPO_ROOT/$CLERIC_REL" "$STAGED_CLERIC"
    if ! diff -rq "$REPO_ROOT/packs" "$BUILD/packs" > "$WORK_DIR/packs.diff" 2>&1
    then
        printf 'FAIL: the staged pack tree does not match the repository:\n' >&2
        cat "$WORK_DIR/packs.diff" >&2
        rm -rf -- "$WORK_DIR"
        exit 1
    fi
    rm -rf -- "$WORK_DIR"
}
trap restore_staged_packs EXIT

mkdir -p "$OUT_DIR"

# BMP frame dir -> looping GIF via the two-pass palette chain
# (scripts/media/capture_campaign_scripting.sh's frames_to_gif, plus the crop
# and integer upscale that make a 320x200 game frame readable on a PR page:
# the rig is composed so one window holds the notification lines, both
# fighters and the patient's HP bar).
CROP="crop=140:100:92:20,scale=iw*4:ih*4:flags=neighbor"
frames_to_gif() {
    local dir="$1" out="$2" fps="${3:-12}"
    # Every 2nd frame: 420 sim ticks at 12 fps would run 35 s.
    local glob="$dir/*[02468].bmp"
    ffmpeg -hide_banner -loglevel error -y -framerate "$fps" \
        -pattern_type glob -i "$glob" \
        -vf "$CROP,palettegen=stats_mode=diff" "$WORK_DIR/pal.png"
    ffmpeg -hide_banner -loglevel error -y -framerate "$fps" \
        -pattern_type glob -i "$glob" -i "$WORK_DIR/pal.png" \
        -lavfi "[0:v]$CROP[x];[x][1:v]paletteuse=dither=bayer:bayer_scale=4" \
        -loop 0 "$out"
}

# One seeded lockstep run of the clinic. TEAM_SIZE=1 keeps a single deployed
# soldier out of the frame; CAMPAIGN_STATE is what arms clinic.lua's fence;
# FOCUS=center parks the free camera on the middle of the map, where the rig
# stands. The config dir is private: a headless run that cannot reach one
# falls back to the working directory and rewrites the tracked cfg/.
demo_capture() { # demo_capture <phase>
    local phase="$1"
    local dir="$WORK_DIR/frames-$phase"
    rm -rf -- "$dir"
    env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_SEED=1337 \
        OPENGLAD_DEMO_LOCKSTEP=1 \
        OPENGLAD_DEMO_CAMPAIGN=concept \
        OPENGLAD_DEMO_SCENARIOS=600 \
        OPENGLAD_DEMO_TEAM_SIZE=1 \
        OPENGLAD_DEMO_CAMPAIGN_STATE=clinic=1 \
        OPENGLAD_DEMO_CAPTURE_DIR="$dir" \
        OPENGLAD_DEMO_CAPTURE_LIMIT=420 \
        OPENGLAD_DEMO_CAPTURE_FOCUS=center \
        OPENGLAD_CONFIG_DIR="$WORK_DIR/config-$phase" \
        "$DEMO_BIN" > "$WORK_DIR/$phase.log" 2>&1 \
        || { cat "$WORK_DIR/$phase.log" >&2; die "the $phase demo run failed"; }
    [ -s "$dir/frame00000.bmp" ] \
        || die "the $phase run dumped no frames; see $WORK_DIR/$phase.log"
    frames_to_gif "$dir" "$OUT_DIR/cleric-lowmp-$phase.gif"
    FRAME_DIR="$dir"
}

# The pre-fix pack, staged next to the binary. Lua packs are read from the
# staged tree at run time, so this needs no rebuild -- which is exactly what
# makes the two runs comparable.
git -C "$REPO_ROOT" show "$PREFIX_REV:$CLERIC_REL" > "$STAGED_CLERIC"
demo_capture before
BEFORE_FRAMES="$FRAME_DIR"

cp -- "$REPO_ROOT/$CLERIC_REL" "$STAGED_CLERIC"
demo_capture after
AFTER_FRAMES="$FRAME_DIR"

# --- verification ----------------------------------------------------------
# Same binary, same seed, same lockstep: everything before the first cast must
# be bit-identical. A divergence at frame 0 would mean the two runs are not
# comparable and the pair proves nothing. (Frame N is the render of sim tick
# N+1, so the cast on tick 30 first shows on frame 00029.)
for f in 00000 00010 00020 00028; do
    cmp -s "$BEFORE_FRAMES/frame$f.bmp" "$AFTER_FRAMES/frame$f.bmp" \
        || die "FAIL: frame $f already differs; the runs are not comparable"
done
# And they must diverge after it, or the "fix" is not on camera at all.
cmp -s "$BEFORE_FRAMES/frame00029.bmp" "$AFTER_FRAMES/frame00029.bmp" \
    && die 'FAIL: frame 29 is identical; no heal landed in the "after" run'

for phase in before after; do
    gif="$OUT_DIR/cleric-lowmp-$phase.gif"
    [ -s "$gif" ] || die "FAIL: $gif was never produced"
    frames="$(ffprobe -loglevel error -select_streams v:0 -count_frames \
        -show_entries stream=nb_read_frames -of csv=p=0 "$gif")"
    [ "${frames:-0}" -ge 2 ] \
        || die "FAIL: $gif holds $frames frame(s); an animation needs >= 2"
    bytes="$(stat -c %s "$gif")"
    [ "$bytes" -le 4000000 ] \
        || die "FAIL: $gif is $bytes bytes, over the 4 MB budget"
    printf 'wrote %s (%s frames, %s bytes)\n' "$gif" "$frames" "$bytes"
done
