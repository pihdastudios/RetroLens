#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

if [[ "${1:-}" == "--existing" ]]; then
    shift
elif [[ $# -eq 0 ]]; then
    "$SCRIPT_DIR/build.sh" --clean
else
    echo "Usage: $0 [--existing]" >&2
    exit 2
fi
[[ $# -eq 0 ]] || { echo "Usage: $0 [--existing]" >&2; exit 2; }
apk="$(latest_apk)"
release_dir="$PROJECT_DIR/releases"
mkdir -p "$release_dir"
cp "$apk" "$release_dir/RetroLens-1.0.0-startup-recovery.apk"
sha256sum "$release_dir/RetroLens-1.0.0-startup-recovery.apk" > \
    "$release_dir/RetroLens-1.0.0-startup-recovery.apk.sha256"
echo "Release package: $release_dir/RetroLens-1.0.0-startup-recovery.apk"
