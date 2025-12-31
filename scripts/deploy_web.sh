#!/bin/bash
# Deploy dist/ to S3 for static website hosting
#
# Prerequisites:
#   1. AWS CLI installed and configured (aws configure)
#   2. S3 bucket created with static website hosting enabled
#
# Setup (one-time):
#   1. Create S3 bucket: aws s3 mb s3://huddledungeon.com
#   2. Enable static website hosting:
#      aws s3 website s3://huddledungeon.com --index-document index.html
#   3. Set bucket policy for public access (see below)
#   4. In Cloudflare DNS, add CNAME: huddledungeon.com -> huddledungeon.com.s3-website-us-east-1.amazonaws.com
#      (adjust region as needed)
#
# Bucket policy (save as policy.json and run: aws s3api put-bucket-policy --bucket huddledungeon.com --policy file://policy.json):
# {
#     "Version": "2012-10-17",
#     "Statement": [
#         {
#             "Sid": "PublicReadGetObject",
#             "Effect": "Allow",
#             "Principal": "*",
#             "Action": "s3:GetObject",
#             "Resource": "arn:aws:s3:::huddledungeon.com/*"
#         }
#     ]
# }

set -e

BUCKET="huddledungeon.com"
DIST_DIR="$(dirname "$0")/../dist"

if [ ! -d "$DIST_DIR" ]; then
    echo "Error: dist/ directory not found. Run ./scripts/build_web.sh first."
    exit 1
fi

echo "Deploying to s3://$BUCKET..."

# Sync all files with appropriate cache headers
aws s3 sync "$DIST_DIR" "s3://$BUCKET" \
    --delete \
    --cache-control "max-age=86400" \
    --exclude "*.html"

# HTML files with shorter cache (for updates)
aws s3 sync "$DIST_DIR" "s3://$BUCKET" \
    --cache-control "max-age=300" \
    --exclude "*" \
    --include "*.html"

# WASM files need correct content type
aws s3 cp "$DIST_DIR/play.wasm" "s3://$BUCKET/play.wasm" \
    --content-type "application/wasm" \
    --cache-control "max-age=86400"

echo ""
echo "Deploy complete!"
echo "Site: https://huddledungeon.com"
