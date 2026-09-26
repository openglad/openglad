#!/usr/bin/env bash
# Issues #293, #294, and #295 gameplay proof. The same capture-only tests run against the
# b1412cab product tree and the fixed product tree. Gameplay pixels come from
# screen::get_pixel through gameplay_rec::dump_viewport; labels are added only
# outside those pixels. Output stays in gitignored build/media/. Publish the
# final PNG/MP4 files to openglad/openglad-screenshots/pr-<PR>/.
#
# Usage (from repo root, inside nix develop):
#   scripts/media/capture_classic_find.sh --before --revision b1412cab \
#       --binary /path/to/base/build/ci-test/og_test_game_core
#   scripts/media/capture_classic_find.sh --after --revision <fixed-sha> \
#       --binary /path/to/fixed/build/ci-test/og_test_game_core
#   scripts/media/capture_classic_find.sh --compose
#
# CLASSIC_FIND_MEDIA_DIR overrides build/media/classic-find. Capture and
# compilation are deliberately separate, so a base worktree can receive only
# the capture tests without receiving the product fix.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
MEDIA="${CLASSIC_FIND_MEDIA_DIR:-$ROOT/build/media/classic-find}"
BASE_REVISION=b1412cab
FRAMES=17
SCENES=(
    classic_shield_hostile
    classic_shield_friendly
    classic_boomerang_hostile
    classic_boomerang_friendly
    classic_yell
)
TESTS=(
    GameLoop.zz_classic_guard_shield_hostile
    GameLoop.zz_classic_guard_shield_friendly
    GameLoop.zz_classic_guard_boomerang_hostile
    GameLoop.zz_classic_guard_boomerang_friendly
    GameLoop.zz_classic_yell
)

die() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
need() { command -v "$1" >/dev/null || die "$1 is missing; enter nix develop"; }

mode=""
revision=""
binary=""
while (($#)); do
    case "$1" in
        --before|--after|--compose) mode="${1#--}" ;;
        --revision) shift; (($#)) || die '--revision needs a commit'; revision="$1" ;;
        --binary) shift; (($#)) || die '--binary needs a path'; binary="$1" ;;
        -h|--help) sed -n '/^# Usage/,/^# CLASSIC/s/^# \{0,1\}//p' "$0"; exit 0 ;;
        *) die "unknown argument: $1" ;;
    esac
    shift
done
[[ -n "$mode" ]] || die 'select --before, --after, or --compose'
for tool in ffmpeg ffprobe magick python3; do need "$tool"; done
mkdir -p "$MEDIA"

frame_path() { printf '%s/raw/%s/%03d.ppm' "$1" "$2" "$3"; }

verify_frames() { # verify_frames <phase-dir> <scene>
    local phase_dir="$1" scene="$2" n path count dims
    for ((n=0; n<FRAMES; ++n)); do
        path="$(frame_path "$phase_dir" "$scene" "$n")"
        [[ -s "$path" ]] || die "missing captured frame $path"
    done
    count="$(find "$phase_dir/raw/$scene" -maxdepth 1 -type f -name '*.ppm' | wc -l)"
    [[ "$count" -eq "$FRAMES" ]] || die "$scene has $count frames, expected $FRAMES"
    dims="$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=width,height -of csv=p=0:s=x \
        "$(frame_path "$phase_dir" "$scene" 0)")"
    [[ "$dims" =~ ^[0-9]+x[0-9]+$ ]] || die "$scene has invalid PPM dimensions: $dims"
    printf '%s: %s frames at %s\n' "$scene" "$count" "$dims"
}

if [[ "$mode" != compose ]]; then
    [[ -n "$revision" ]] || die '--revision is required for a capture phase'
    [[ -n "$binary" && -x "$binary" ]] || die "test binary not executable: $binary"
    full_revision="$(git rev-parse --verify "$revision^{commit}")" \
        || die "unknown commit: $revision"
    if [[ "$mode" == before ]]; then
        base_full="$(git rev-parse --verify "$BASE_REVISION^{commit}")"
        [[ "$full_revision" == "$base_full" ]] \
            || die "before must name product base $BASE_REVISION"
        # These two source guards catch an accidental 'before' capture from
        # a partly fixed checkout. The binary checksum below records the
        # executable; the caller must build it from this product source.
        rg -Fq 'og.find_foe_weapons_in_range("ob"' \
            packs/core/lib/effect_shield.lua \
            || die 'before checkout already has the Lua weapon-list fix'
        weapon_lookup="$(sed -n '/GameWorld::find_foe_weapons_in_range/,/^}/p' \
            src/gameplay/game_world.cpp)"
        [[ -n "$weapon_lookup" &&
           "$weapon_lookup" == *'ob->is_friendly(w) &&'* ]] \
            || die 'before checkout already has the hostile-weapon fix'
    fi
    phase_dir="$MEDIA/$mode"
    mkdir -p "$phase_dir"
    rm -rf -- "$phase_dir/raw" "$phase_dir/config"
    mkdir -p "$phase_dir/raw" "$phase_dir/config" "$phase_dir/logs"
    printf '%s\n' "$full_revision" > "$phase_dir/product-revision.txt"
    git rev-parse HEAD > "$phase_dir/checkout-head.txt"
    sha256sum "$binary" > "$phase_dir/binary-sha256.txt"
    for ((i=0; i<${#SCENES[@]}; ++i)); do
        scene="${SCENES[i]}"
        test_name="${TESTS[i]}"
        log="$phase_dir/logs/$scene.log"
        status=0
        env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
            SDL_RENDER_DRIVER=software \
            OG_FX_CAPTURE_DIR="$phase_dir/raw" \
            OPENGLAD_CONFIG_DIR="$phase_dir/config" \
            "$binary" --gtest_filter="$test_name" \
            < /dev/null > "$log" 2>&1 || status=$?
        verify_frames "$phase_dir" "$scene"
        if [[ "$mode" == before &&
              ( "$scene" == classic_shield_hostile ||
                "$scene" == classic_boomerang_hostile ||
                "$scene" == classic_yell ) ]]; then
            [[ "$status" -eq 1 ]] || die "$scene: base must fail its fixed-behavior assertion (status $status)"
            rg -Fq "[  FAILED  ] $test_name" "$log" \
                || die "$scene: failure is not the named regression; inspect $log"
            # gtest prints the failed name twice, once at the case and once
            # in its final failed list; no other case should be in this log.
            if rg '^\[  FAILED  \] GameLoop\.' "$log" |
               rg -v -F "[  FAILED  ] $test_name"; then
                die "$scene: unexpected test failure; inspect $log"
            fi
            failures="$(rg -c '^.+test_game_loop\.cpp:[0-9]+: Failure$' "$log" || true)"
            if [[ "$scene" == classic_yell ]]; then
                [[ "$failures" == 2 ]] \
                    || die "$scene: expected exactly two specific assertions, got $failures; inspect $log"
                rg -Fq 'CLASSIC_YELL_SELF_RECRUITED' "$log" \
                    || die "$scene: self-recruit regression signature missing; inspect $log"
                rg -Fq 'CLASSIC_YELL_ATTACKER_BACKREF_MISSING' "$log" \
                    || die "$scene: attacker back-reference signature missing; inspect $log"
                rg -Fq 'classic yell: scene=classic_yell frames=17 self_leads_self=1 attacker_targets_yeller=0 ally_follows_yeller=1 flee_queued=1' "$log" \
                    || die "$scene: base state differs from the named regression; inspect $log"
            else
                [[ "$failures" == 1 ]] \
                    || die "$scene: expected exactly one specific assertion, got $failures; inspect $log"
                rg -Fq 'CLASSIC_GUARD_WRONG_ARROW_FATE' "$log" \
                    || die "$scene: arrow fate regression signature missing; inspect $log"
                rg -Fq "classic guard: scene=$scene frames=17 arrow_survived=1" "$log" \
                    || die "$scene: base arrow state differs from the named regression; inspect $log"
            fi
        else
            [[ "$status" -eq 0 ]] || die "$scene: test failed (status $status); inspect $log"
            rg -Fq "[       OK ] $test_name" "$log" \
                || die "$scene: missing passing test verdict in $log"
        fi
        printf '%s: test status %s, media complete\n' "$scene" "$status"
    done
    printf '%s capture complete: %s\n' "$mode" "$phase_dir"
    exit 0
fi

before="$MEDIA/before"
after="$MEDIA/after"
[[ -s "$before/product-revision.txt" && -s "$after/product-revision.txt" ]] \
    || die 'capture both phases before composing'
before_rev="$(<"$before/product-revision.txt")"
after_rev="$(<"$after/product-revision.txt")"
[[ "$before_rev" != "$after_rev" ]] || die 'before and after name the same product revision'
for scene in "${SCENES[@]}"; do
    verify_frames "$before" "$scene"
    verify_frames "$after" "$scene"
done

# The guard's visible arrow is only 3x7 native pixels. Require a real pixel
# difference in both hostile scenes and an unchanged friendly control before
# the montage can claim either result. The yell pointer assertions are in the
# test log; its frame-16 difference only says the actors moved differently.
for scene in "${SCENES[@]}"; do
    frame=001
    [[ "$scene" == classic_yell ]] && frame=016
    difference="$(magick compare -metric AE \
        "$(frame_path "$before" "$scene" "$((10#$frame))")" \
        "$(frame_path "$after" "$scene" "$((10#$frame))")" \
        null: 2>&1 || true)"
    differing_pixels="${difference%% *}"
    case "$scene" in
        classic_shield_friendly|classic_boomerang_friendly)
            [[ "$differing_pixels" -eq 0 ]] \
                || die "$scene: friendly control changed by $differing_pixels pixels" ;;
        *)
            [[ "$differing_pixels" -gt 0 ]] \
                || die "$scene: no visible before/after difference at frame $frame" ;;
    esac
    printf '%s: frame %s differs by %s native pixels\n' \
        "$scene" "$frame" "$differing_pixels"
done

font="${OG_OVERLAY_FONT:-}"
if [[ -z "$font" ]]; then
    roots="${XDG_DATA_DIRS:-}:$HOME/.nix-profile/share:/usr/local/share:/usr/share"
    IFS=':' read -r -a split <<< "$roots"
    for root in "${split[@]}"; do
        [[ -n "$root" ]] || continue
        font="$(find "$root/fonts" -name DejaVuSansMono.ttf \
            -print -quit 2>/dev/null || true)"
        [[ -n "$font" ]] && break
    done
fi
[[ -f "$font" ]] || die 'set OG_OVERLAY_FONT to a readable mono TTF'

final="$MEDIA/final"
mkdir -p "$final"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT

for scene in "${SCENES[@]}"; do
    scene_work="$work/$scene"
    mkdir -p "$scene_work"
    dims="$(ffprobe -v error -select_streams v:0 \
        -show_entries stream=width,height -of csv=p=0:s=x \
        "$(frame_path "$before" "$scene" 0)")"
    width="${dims%x*}"
    scaled_width=$((width * 2))
    for view in full close; do
        label_width="$scaled_width"
        [[ "$view" == close ]] && label_width=448
        magick -background '#202028' -fill '#f0f0f0' -font "$font" \
            -pointsize 15 -size "${label_width}x28" -gravity center \
            "label:BEFORE ${before_rev:0:8}" \
            "$scene_work/before-$view-label.png"
        magick -background '#202028' -fill '#f0f0f0' -font "$font" \
            -pointsize 15 -size "${label_width}x28" -gravity center \
            "label:AFTER ${after_rev:0:8}" \
            "$scene_work/after-$view-label.png"
    done
    for ((n=0; n<FRAMES; ++n)); do
        num="$(printf '%03d' "$n")"
        for phase in before after; do
            magick "$(frame_path "$MEDIA/$phase" "$scene" "$n")" \
                -filter point -resize 200% "$scene_work/$phase-up.png"
            magick "$scene_work/$phase-full-label.png" \
                "$scene_work/$phase-up.png" -append \
                "$scene_work/$phase-panel.png"
            # A fixed 112x88 native-pixel crop around the actors. The full
            # frame is still shipped as the context still; this 4x view makes
            # the small arrow and orbiting guard readable in the MP4.
            magick "$(frame_path "$MEDIA/$phase" "$scene" "$n")" \
                -crop 112x88+104+56 +repage -filter point -resize 400% \
                "$scene_work/$phase-close-up.png"
            magick "$scene_work/$phase-close-label.png" \
                "$scene_work/$phase-close-up.png" -append \
                "$scene_work/$phase-close-panel.png"
        done
        magick "$scene_work/before-panel.png" \
            "$scene_work/after-panel.png" +append \
            "$scene_work/$num.png"
        magick "$scene_work/before-close-panel.png" \
            "$scene_work/after-close-panel.png" +append \
            "$scene_work/close-$num.png"
    done
    # Guard frame 1 shows the shot just after interception. The yell's
    # back-reference takes several ticks to affect movement, so its still is
    # the last frame; the exact pointer result remains a test assertion.
    still_frame=001
    [[ "$scene" == classic_yell ]] && still_frame=016
    cp -- "$scene_work/$still_frame.png" "$final/$scene.png"
    cp -- "$scene_work/close-$still_frame.png" "$final/$scene-close.png"
    ffmpeg -hide_banner -loglevel error -y -framerate 8 \
        -i "$scene_work/close-%03d.png" -vf 'format=yuv420p' \
        -c:v libx264 -movflags +faststart "$final/$scene.mp4"
    video_frames="$(ffprobe -v error -count_frames -select_streams v:0 \
        -show_entries stream=nb_read_frames -of csv=p=0 \
        "$final/$scene.mp4")"
    [[ "$video_frames" -eq "$FRAMES" ]] \
        || die "$scene MP4 decoded $video_frames frames, expected $FRAMES"
    ffprobe -v error -select_streams v:0 -show_entries stream=width,height \
        -of csv=p=0:s=x "$final/$scene.png" >/dev/null \
        || die "$scene PNG does not decode"
    ffprobe -v error -select_streams v:0 -show_entries stream=width,height \
        -of csv=p=0:s=x "$final/$scene-close.png" >/dev/null \
        || die "$scene close PNG does not decode"
    case "$scene" in
        classic_shield_hostile) detail_crop=32x32+133+62 ;;
        classic_boomerang_hostile) detail_crop=32x32+148+78 ;;
        *) detail_crop="" ;;
    esac
    if [[ -n "$detail_crop" ]]; then
        for phase in before after; do
            magick "$(frame_path "$MEDIA/$phase" "$scene" 1)" \
                -crop "$detail_crop" +repage -filter point -resize 1200% \
                "$scene_work/$phase-detail-up.png"
            magick -background '#202028' -fill '#f0f0f0' -font "$font" \
                -pointsize 15 -size 384x28 -gravity center \
                "label:${phase^^} $([[ "$phase" == before ]] && \
                    printf '%s' "${before_rev:0:8}" || \
                    printf '%s' "${after_rev:0:8}")" \
                "$scene_work/$phase-detail-label.png"
            magick "$scene_work/$phase-detail-label.png" \
                "$scene_work/$phase-detail-up.png" -append \
                "$scene_work/$phase-detail-panel.png"
        done
        magick "$scene_work/before-detail-panel.png" \
            "$scene_work/after-detail-panel.png" +append \
            "$final/$scene-detail.png"
        ffprobe -v error -select_streams v:0 -show_entries stream=width,height \
            -of csv=p=0:s=x "$final/$scene-detail.png" >/dev/null \
            || die "$scene detail PNG does not decode"
    fi
    printf '%s: full PNG + close PNG + close MP4 (%s frames)\n' \
        "$scene" "$video_frames"
done
printf 'Final media: %s\n' "$final"
