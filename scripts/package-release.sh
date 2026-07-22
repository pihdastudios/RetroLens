#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

"$SCRIPT_DIR/build.sh" --clean
apk="$(latest_apk)"
release_dir="$PROJECT_DIR/releases"
mkdir -p "$release_dir"
cp "$apk" "$release_dir/RetroLens-1.0.0-debug.apk"
sha256sum "$release_dir/RetroLens-1.0.0-debug.apk" > "$release_dir/RetroLens-1.0.0-debug.apk.sha256"
echo "Release package: $release_dir/RetroLens-1.0.0-debug.apk"
