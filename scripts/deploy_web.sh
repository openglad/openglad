#!/usr/bin/env bash
# Deploy dist/ to S3 for static website hosting.
#
# Defaults are intentionally non-destructive:
#   ./scripts/deploy_web.sh --bucket example.com
#
# To publish for real:
#   ./scripts/deploy_web.sh --bucket example.com --execute
#
# To also delete remote files that no longer exist locally:
#   ./scripts/deploy_web.sh --bucket example.com --execute --delete-remote

set -euo pipefail

usage() {
    cat >&2 <<'EOF'
Usage: deploy_web.sh --bucket BUCKET [--profile PROFILE] [--execute] [--delete-remote]

Options:
  --bucket BUCKET    Required unless OPENGLAD_DEPLOY_BUCKET is set.
  --profile PROFILE  Optional AWS CLI profile.
  --execute          Perform changes. Without this flag, aws runs with --dryrun.
  --delete-remote    Delete remote files missing from dist/. Requires --execute.
EOF
}

BUCKET="${OPENGLAD_DEPLOY_BUCKET:-}"
PROFILE="${AWS_PROFILE:-}"
EXECUTE=0
DELETE_REMOTE=0
DIST_DIR="$(cd "$(dirname "$0")/.." && pwd)/dist"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --bucket)
            if [ "$#" -lt 2 ]; then
                usage
                exit 2
            fi
            BUCKET="$2"
            shift 2
            ;;
        --profile)
            if [ "$#" -lt 2 ]; then
                usage
                exit 2
            fi
            PROFILE="$2"
            shift 2
            ;;
        --execute)
            EXECUTE=1
            shift
            ;;
        --delete-remote)
            DELETE_REMOTE=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [ -z "$BUCKET" ]; then
    echo "Error: --bucket or OPENGLAD_DEPLOY_BUCKET is required." >&2
    usage
    exit 2
fi

if [ "$DELETE_REMOTE" -eq 1 ] && [ "$EXECUTE" -ne 1 ]; then
    echo "Error: --delete-remote requires --execute." >&2
    exit 2
fi

if [ ! -d "$DIST_DIR" ]; then
    echo "Error: dist/ directory not found. Run ./scripts/build_web.sh first." >&2
    exit 1
fi

if [ ! -f "$DIST_DIR/index.html" ] || [ ! -f "$DIST_DIR/play.wasm" ]; then
    echo "Error: dist/ is missing index.html or play.wasm." >&2
    exit 1
fi

AWS_ARGS=()
if [ -n "$PROFILE" ]; then
    AWS_ARGS+=(--profile "$PROFILE")
fi

DRYRUN_ARGS=(--dryrun)
if [ "$EXECUTE" -eq 1 ]; then
    DRYRUN_ARGS=()
fi

DELETE_ARGS=()
if [ "$DELETE_REMOTE" -eq 1 ]; then
    DELETE_ARGS=(--delete)
fi

MODE="dry run"
if [ "$EXECUTE" -eq 1 ]; then
    MODE="execute"
fi
echo "Deploying $DIST_DIR to s3://$BUCKET ($MODE)..."

aws "${AWS_ARGS[@]}" s3 sync "$DIST_DIR" "s3://$BUCKET" \
    "${DRYRUN_ARGS[@]}" \
    "${DELETE_ARGS[@]}" \
    --cache-control "max-age=86400" \
    --exclude "*.html"

aws "${AWS_ARGS[@]}" s3 sync "$DIST_DIR" "s3://$BUCKET" \
    "${DRYRUN_ARGS[@]}" \
    --cache-control "max-age=300" \
    --exclude "*" \
    --include "*.html"

aws "${AWS_ARGS[@]}" s3 cp "$DIST_DIR/play.wasm" "s3://$BUCKET/play.wasm" \
    "${DRYRUN_ARGS[@]}" \
    --content-type "application/wasm" \
    --cache-control "max-age=86400"

if [ "$EXECUTE" -eq 1 ]; then
    echo "Deploy complete: s3://$BUCKET"
else
    echo "Dry run complete. Re-run with --execute to publish."
fi
