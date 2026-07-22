#!/usr/bin/env bash

set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

label="${1:-latest}"
if [[ ! "$label" =~ ^[A-Za-z0-9._-]+$ ]]; then
    echo "Log label may contain only letters, digits, dot, underscore, and dash" >&2
    exit 1
fi

source_log="${SONY_LOG_FILE:-}"
if [[ -z "$source_log" ]]; then
    while IFS= read -r candidate; do
        source_log="$candidate"
        break
    done < <(find /media -maxdepth 5 -type f -path '*/RETROLENS/LOG.TXT' 2>/dev/null | sort)
fi
if [[ -z "$source_log" || ! -f "$source_log" ]]; then
    echo "RETROLENS/LOG.TXT not found. Reconnect the camera in Mass Storage mode." >&2
    exit 1
fi

destination_dir="$TOOLCHAIN_DIR/device-logs"
destination="$destination_dir/$label.log"
mkdir -p "$destination_dir"
cp "$source_log" "$destination"

echo "Copied $source_log to $destination"
echo "RetroLens excerpts:"
if command -v rg >/dev/null 2>&1; then
    rg -n 'RetroLens|RetroFrames|performance|Retro Clip|derivative|\[ERROR\]' "$destination" | tail -200 || true
else
    grep -nE 'RetroLens|RetroFrames|performance|Retro Clip|derivative|\[ERROR\]' "$destination" | tail -200 || true
fi
