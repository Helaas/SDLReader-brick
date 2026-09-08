#!/usr/bin/env bash
# Verify the Leaf (MLP1) release metadata agrees with the release version.
#
# The version is carried in three places and a v2.5.1 release shipped with a
# stale 2.4.0 in two of them, which the Pak Rat catalog gate in leaf-docs
# rejects (it requires the artifact's pak_version to equal the catalogued
# version). This check turns that silent drift into a build failure.
#
#   pak.json                    .version           - source of truth, becomes the tag
#   pakrat.json                 .leaf.packages[0]  - Pak Rat metadata
#   <built pak>/pak.json        .pak_version       - stamped by write-pak-json.sh
#
# Usage: check-leaf-release.sh [built SDLReader.pak directory]
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
PAK_DIR="${1:-$PROJECT_ROOT/build/mlp1/package/SDLReader.pak}"

command -v jq >/dev/null 2>&1 || { echo "Error: check-leaf-release.sh requires jq." >&2; exit 1; }

fail() { echo "::error::$*"; echo "Error: $*" >&2; exit 1; }

RELEASE_TAG="$(jq -er .version "$PROJECT_ROOT/pak.json")"
VERSION="${RELEASE_TAG#v}"
printf '%s' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' \
    || fail "pak.json .version '$RELEASE_TAG' is not a MAJOR.MINOR.PATCH release tag."

echo "Release version: $VERSION (tag $RELEASE_TAG)"

# pakrat.json must describe this release, and must name the artifact that
# leaf-mlp1.yml actually uploads.
PAKRAT_VERSION="$(jq -er '.leaf.packages[0].version' "$PROJECT_ROOT/pakrat.json")"
PAKRAT_ARTIFACT="$(jq -er '.leaf.packages[0].artifact_name' "$PROJECT_ROOT/pakrat.json")"
[ "$PAKRAT_VERSION" = "$VERSION" ] \
    || fail "pakrat.json version '$PAKRAT_VERSION' does not match release version '$VERSION'."
[ "$PAKRAT_ARTIFACT" = "SDLReader.leaf-mlp1.pak.zip" ] \
    || fail "pakrat.json artifact_name '$PAKRAT_ARTIFACT' is not the Leaf artifact SDLReader.leaf-mlp1.pak.zip."

# The built pak carries the version that the catalog gate compares against.
if [ -d "$PAK_DIR" ]; then
    BUILT_VERSION="$(jq -er .pak_version "$PAK_DIR/pak.json")"
    [ "$BUILT_VERSION" = "$VERSION" ] \
        || fail "$PAK_DIR/pak.json pak_version '$BUILT_VERSION' does not match release version '$VERSION'."
    echo "Built pak pak_version: $BUILT_VERSION"
else
    echo "Note: $PAK_DIR not present - skipping the built-pak check."
fi

echo "Leaf release metadata is consistent at $VERSION."
