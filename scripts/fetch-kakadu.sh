#!/usr/bin/env bash
# Fetch the proprietary Kakadu archive from dsp-ci-assets release into vendor/.
# Idempotent: skips download if the file is already present.
#
# Local: requires `gh auth login` and dasch-swiss org membership.
# CI: set GH_TOKEN to a PAT with read access to dsp-ci-assets (DASCHBOT_PAT).
#
# Re-run after bumping kakaduAssetName / kakaduSha256 in flake.nix.
set -euo pipefail

ASSET="v8_5-01382N.zip"
TAG="kakadu-v8.5"
REPO="dasch-swiss/dsp-ci-assets"
DEST="vendor/$ASSET"

if [ ! -f "$DEST" ]; then
    if ! command -v gh >/dev/null 2>&1; then
        echo "error: gh CLI not found; install GitHub CLI to fetch Kakadu" >&2
        exit 1
    fi

    echo "Fetching $ASSET from $REPO ($TAG)..."
    gh release download "$TAG" \
        --repo "$REPO" \
        --pattern "$ASSET" \
        --dir vendor/

    echo "$DEST downloaded ($(wc -c < "$DEST") bytes)"
else
    echo "$DEST already present — skipping download"
fi

# Nix flake source filtering excludes gitignored files (see .gitignore:
# "vendor/v8_5-*.zip"). Use git's intent-to-add to make the file visible to
# Nix's flake source without staging it for commit. The file is still
# protected by the gitignore entry against accidental `git add`.
if [ -d .git ] || git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git add --intent-to-add --force "$DEST"
fi
