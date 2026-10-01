#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <os> <category>" >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="${QV2RAY_RELEASE_DEPENDENCIES:-$SCRIPT_DIR/../.github/release-dependencies.json}"
DEPS_OS="$1"
DEPS_CATEGORY="$2"

if [[ ! -f "$MANIFEST" ]]; then
    echo "Pinned release dependency manifest not found: $MANIFEST" >&2
    exit 1
fi

RELEASE_ID="$(jq -r '.qv2ray_deps.release_id' "$MANIFEST")"
RELEASE_TAG="$(jq -r '.qv2ray_deps.release_tag' "$MANIFEST")"
if [[ ! "$RELEASE_ID" =~ ^[0-9]+$ ]] || [[ -z "$RELEASE_TAG" || "$RELEASE_TAG" == "null" ]]; then
    echo "Invalid Qv2ray-deps release pin in $MANIFEST" >&2
    exit 1
fi

release_json="$(curl -fsSL --retry 3 "https://api.github.com/repos/Qv2ray/Qv2ray-deps/releases/$RELEASE_ID")"
actual_tag="$(jq -r '.tag_name' <<<"$release_json")"
if [[ "$actual_tag" != "$RELEASE_TAG" ]]; then
    echo "Pinned Qv2ray-deps release id $RELEASE_ID resolved to unexpected tag $actual_tag" >&2
    exit 1
fi

suffix="$DEPS_CATEGORY-$DEPS_OS.7z"
mapfile -t assets < <(jq -c --arg suffix "$suffix" '.assets[] | select(.name | endswith($suffix)) | {id, name, size, browser_download_url}' <<<"$release_json")
if (( ${#assets[@]} == 0 )); then
    echo "No dependency assets matched $suffix in pinned release $RELEASE_ID" >&2
    exit 1
fi

DOWNLOAD_DIR="$SCRIPT_DIR/deps/downloaded"
mkdir -p "$DOWNLOAD_DIR"

declare -a downloaded_names=()
for data in "${assets[@]}"; do
    NAME="$(jq -r '.name' <<<"$data")"
    ASSET_ID="$(jq -r '.id' <<<"$data")"
    API_SIZE="$(jq -r '.size' <<<"$data")"
    URL="$(jq -r '.browser_download_url' <<<"$data")"

    pinned="$(jq -c --arg name "$NAME" '.qv2ray_deps.assets[$name] // empty' "$MANIFEST")"
    if [[ "$DEPS_OS" == "windows" && "$DEPS_CATEGORY" == "x64" && -z "$pinned" ]]; then
        echo "Windows x64 dependency $NAME is not explicitly pinned in $MANIFEST" >&2
        exit 1
    fi
    if [[ -n "$pinned" ]]; then
        EXPECTED_ID="$(jq -r '.asset_id' <<<"$pinned")"
        EXPECTED_SIZE="$(jq -r '.size' <<<"$pinned")"
        if [[ "$ASSET_ID" != "$EXPECTED_ID" || "$API_SIZE" != "$EXPECTED_SIZE" ]]; then
            echo "Pinned identity mismatch for $NAME (id=$ASSET_ID size=$API_SIZE)" >&2
            exit 1
        fi
    fi

    echo "Downloading pinned Qv2ray-deps asset: $NAME (asset id $ASSET_ID)"
    curl -fL --retry 3 "$URL" -o "$DOWNLOAD_DIR/$NAME"
    ACTUAL_SIZE="$(wc -c < "$DOWNLOAD_DIR/$NAME" | tr -d '[:space:]')"
    if [[ "$ACTUAL_SIZE" != "$API_SIZE" ]]; then
        echo "Downloaded size mismatch for $NAME: expected $API_SIZE, got $ACTUAL_SIZE" >&2
        exit 1
    fi
    downloaded_names+=("$NAME")
done

cd "$SCRIPT_DIR/deps"
for NAME in "${downloaded_names[@]}"; do
    7z x -y "./downloaded/$NAME"
done

if [[ "$DEPS_CATEGORY" == "tools" ]]; then
    mkdir -p "$SCRIPT_DIR/tools"
    cp -rvf ./tools/. "$SCRIPT_DIR/tools/"
    rm -rvf ./tools
else
    echo "Cleaning up $DEPS_CATEGORY-$DEPS_OS"
    TARGET_DIR="$SCRIPT_DIR/$DEPS_CATEGORY-$DEPS_OS"
    rm -rf "$TARGET_DIR"
    mkdir -p "$TARGET_DIR"
    cp -rvf "./$DEPS_OS-$DEPS_CATEGORY/installed/$DEPS_CATEGORY-$DEPS_OS/." "$TARGET_DIR/"
    rm -rvf "./$DEPS_OS-$DEPS_CATEGORY/"
fi
