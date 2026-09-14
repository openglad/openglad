#!/usr/bin/env bash
# Proof media for PR #292. Output lands in build/media/pr-292/<phase>/
# (gitignored); the finished artifacts go to the openglad/openglad-screenshots
# repo under pr-292/, never to this one.
#
# Each recipe is a function named after its item, so the phase-2 packages can
# land theirs independently. Run one, or all of them:
#
#   scripts/media/capture_pr292.sh p8 before      # the curses lobby transcripts
#   scripts/media/capture_pr292.sh all after
#
# Everything here needs the dev shell (`nix develop`): ImageMagick and the
# DejaVu fonts come from flake.nix, and the test binaries from build/ci-test.
# Overrides: OG_MEDIA_OUT=<dir> to write somewhere else, OG_TEST_CURSES=<path>.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ITEM="${1:-all}"
PHASE="${2:-before}"
OUT_DIR="${OG_MEDIA_OUT:-$REPO_ROOT/build/media/pr-292/$PHASE}"

die() { printf 'capture_pr292: %s\n' "$*" >&2; exit 1; }

# The transcripts and stills are rendered with a real mono TrueType face, found
# the way make_lua_ownership_overlays.py finds its DejaVu files.
find_mono_font() {
    if [ -n "${OG_OVERLAY_FONT:-}" ] && [ -f "$OG_OVERLAY_FONT" ]; then
        printf '%s\n' "$OG_OVERLAY_FONT"; return 0
    fi
    local roots root hit
    roots="${XDG_DATA_DIRS:-}:$HOME/.nix-profile/share:/usr/local/share:/usr/share"
    IFS=':' read -r -a split <<< "$roots"
    for root in "${split[@]}"; do
        [ -n "$root" ] || continue
        hit="$(find "$root/fonts" -name 'DejaVuSansMono.ttf' 2>/dev/null | head -1)"
        [ -n "$hit" ] && { printf '%s\n' "$hit"; return 0; }
    done
    return 1
}

# ---------------------------------------------------------------- P8 -------
# The curses lobby is the one place P8 is user-visible, and a HeadlessTerminal
# transcript of the real two-client regression test is the proof: the test
# writes host_term/join_term through tests/curses/transcript_capture.h whenever
# OG_FX_CAPTURE_DIR is set, so no capture-only code path exists.
#
# BEFORE (base tree): the joiner pressed 's' and its status band stays EMPTY
# while the host's band carries "Waiting for other machines".
# AFTER (fixed tree): the joiner's band reads "Only the host can start" and the
# host's own denial stays on the host's band.
p8_curses_transcripts() {
    local bin="${OG_TEST_CURSES:-$REPO_ROOT/build/ci-test/og_test_curses}"
    [ -x "$bin" ] || die "og_test_curses not found at $bin (build the ci-test preset)"
    local font; font="$(find_mono_font)" || die 'no DejaVuSansMono.ttf found (enter `nix develop`)'
    command -v magick >/dev/null || die 'ImageMagick not on PATH (enter `nix develop`)'

    mkdir -p "$OUT_DIR"
    # A capture run must never touch the repo's own cfg/ (the cwd-fallback
    # clobber trap), and one test binary runs at a time, one filter, no shuffle.
    local cfg_scratch; cfg_scratch="$(mktemp -d)"
    OPENGLAD_CONFIG_DIR="$cfg_scratch" \
    OG_FX_CAPTURE_DIR="$OUT_DIR" \
    OG_FX_PHASE="$PHASE" \
        "$bin" --gtest_filter='CursesNetwork.*joiner_start_request*' </dev/null \
        || printf 'capture_pr292: the P8 test failed (expected on the base tree); transcripts are still written\n' >&2
    rm -rf "$cfg_scratch"

    local side
    for side in host join; do
        local txt="$OUT_DIR/p8-curses-$side-$PHASE.txt"
        [ -s "$txt" ] || die "no transcript at $txt"
        magick -background '#101418' -fill '#d8dee9' -font "$font" -pointsize 14 \
            "label:@$txt" "$OUT_DIR/p8-curses-$side-$PHASE.png"
    done
    # One canvas for both sides so `magick a.png b.png +append` lines the two
    # bands up row for row.
    local w h
    w="$(magick identify -format '%w\n' "$OUT_DIR"/p8-curses-*-"$PHASE".png | sort -n | tail -1)"
    h="$(magick identify -format '%h\n' "$OUT_DIR"/p8-curses-*-"$PHASE".png | sort -n | tail -1)"
    for side in host join; do
        local png="$OUT_DIR/p8-curses-$side-$PHASE.png"
        magick "$png" -background '#101418' -gravity northwest \
            -extent "${w}x${h}" -depth 8 "$png"
    done

    verify_png "$OUT_DIR/p8-curses-host-$PHASE.png"
    verify_png "$OUT_DIR/p8-curses-join-$PHASE.png"
    printf 'P8 %s: %s\n' "$PHASE" "$OUT_DIR"
    grep -n 'Waiting for other machines\|Only the host can start' \
        "$OUT_DIR"/p8-curses-*-"$PHASE".txt || true
}

# --------------------------------------------------------- verify tail -----
# verify_media.py's EXPECTED table cannot cover files it has never seen, so the
# check lives here: every artifact must exist, be a real image, and be sane.
verify_png() {
    local f="$1"
    [ -s "$f" ] || die "missing artifact $f"
    magick identify "$f" >/dev/null || die "not a readable image: $f"
    local px; px="$(magick identify -format '%[fx:w*h]' "$f")"
    [ "$px" -ge 100000 ] || die "suspiciously small image: $f ($px px)"
}

case "$ITEM" in
    p8)  p8_curses_transcripts ;;
    all) p8_curses_transcripts ;;
    *)   die "unknown item '$ITEM' (known: p8, all)" ;;
esac
