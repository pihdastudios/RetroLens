#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

echo "Cleaning RetroLens generated build outputs"
rm -rf "$PROJECT_DIR/app/build" "$PROJECT_DIR/app/.externalNativeBuild"
rm -f "$PROJECT_DIR/app/src/main/jniLibs/armeabi/libretrolens.so"
