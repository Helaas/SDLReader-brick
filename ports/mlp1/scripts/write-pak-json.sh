#!/usr/bin/env bash
# Emit the Leaf (MLP1) pak.json with pak_version stamped from the release version.
#
# The release tag is not available at build time: .github/workflows/release.yml
# reads .version out of the repo-root pak.json and creates the tag from it, so
# that field IS the tag and is the single source of truth for both packages.
# Stamping here keeps the Leaf pak_version from drifting away from the release
# it ships in - a mismatch is rejected by the Pak Rat catalog gate in leaf-docs,
# which requires pak_version to equal the catalogued version exactly.
#
# Usage: write-pak-json.sh <destination pak.json>
set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MLP1_PORT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$MLP1_PORT/../.." && pwd)"

DEST="${1:-}"
if [ -z "$DEST" ]; then
    echo "Usage: write-pak-json.sh <destination pak.json>" >&2
    exit 2
fi

TEMPLATE="$MLP1_PORT/pak/pak.json"
RELEASE_PAK_JSON="$PROJECT_ROOT/pak.json"

for f in "$TEMPLATE" "$RELEASE_PAK_JSON"; do
    [ -f "$f" ] || { echo "Error: missing $f" >&2; exit 1; }
done

# Release versions are tags ("v2.5.1"); pak_version is the bare triple ("2.5.1").
RAW_VERSION="$(sed -n 's/^[[:space:]]*"version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$RELEASE_PAK_JSON" | head -n 1)"
VERSION="${RAW_VERSION#v}"

if ! printf '%s' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Error: could not read a MAJOR.MINOR.PATCH version from $RELEASE_PAK_JSON (got '$RAW_VERSION')." >&2
    exit 1
fi

if ! grep -q '"pak_version"' "$TEMPLATE"; then
    echo "Error: $TEMPLATE has no pak_version field to stamp." >&2
    exit 1
fi

# export_bundle.sh assembles the bundle in the template's own directory, so DEST
# and TEMPLATE can be the same file - stage through a temp file rather than
# truncating the template out from under sed.
mkdir -p "$(dirname "$DEST")"
TMP="$(mktemp "${TMPDIR:-/tmp}/mlp1-pak-json.XXXXXX")"
trap 'rm -f "$TMP"' EXIT
sed 's/\("pak_version"[[:space:]]*:[[:space:]]*"\)[^"]*"/\1'"$VERSION"'"/' "$TEMPLATE" > "$TMP"
cat "$TMP" > "$DEST"

# The stamp is load-bearing for the catalog gate, so never ship an unstamped pak.
if ! grep -q "\"pak_version\"[[:space:]]*:[[:space:]]*\"$VERSION\"" "$DEST"; then
    echo "Error: failed to stamp pak_version $VERSION into $DEST." >&2
    exit 1
fi

echo "  pak.json stamped with pak_version $VERSION"
