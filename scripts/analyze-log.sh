#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! -f "$1" ]]; then
    echo "Usage: $0 /path/to/RETROLENS-log.txt" >&2
    exit 2
fi

log_file="$1"
errors="$(awk '/\[ERROR\]|failed|unavailable/ { count++ } END { print count + 0 }' "$log_file")"
starts="$(awk '/runtime create/ { count++ } END { print count + 0 }' "$log_file")"
stops="$(awk '/runtime destroyed/ { count++ } END { print count + 0 }' "$log_file")"
frames="$(awk '/performance processedFps=/ { count++ } END { print count + 0 }' "$log_file")"
captures="$(awk '/processed derivative saved/ { count++ } END { print count + 0 }' "$log_file")"
clips="$(awk '/Retro Clip stop/ { count++ } END { print count + 0 }' "$log_file")"

printf '%-30s %s\n' \
    "runtime starts" "$starts" \
    "runtime clean destroys" "$stops" \
    "performance windows" "$frames" \
    "processed derivatives" "$captures" \
    "finalized clips" "$clips" \
    "error-like lines" "$errors"

echo "Recent RetroLens events:"
if command -v rg >/dev/null 2>&1; then
    rg 'runtime |performance |preset selected|capture|derivative|Retro Clip|failed|unavailable' "$log_file" | tail -200 || true
else
    grep -E 'runtime |performance |preset selected|capture|derivative|Retro Clip|failed|unavailable' "$log_file" | tail -200 || true
fi

if [[ "$starts" -ne "$stops" ]]; then
    echo "Runtime start/destroy count is unbalanced" >&2
    exit 1
fi
