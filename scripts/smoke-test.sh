#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

host_only=false
clean=false
for argument in "$@"; do
    case "$argument" in
        --host-only) host_only=true ;;
        --clean) clean=true ;;
        *) echo "Usage: $0 [--host-only] [--clean]" >&2; exit 2 ;;
    esac
done

echo "Running native ASan/UBSan tests"
make -C "$PROJECT_DIR/native-tests" clean test
if command -v ffprobe >/dev/null 2>&1; then
    ffprobe -v error -select_streams v:0 -show_entries stream=codec_name,width,height,nb_frames \
        -of default=noprint_wrappers=1 /tmp/retrolens-native-test.avi | \
        grep -q 'codec_name=mjpeg'
fi
rm -f /tmp/retrolens-native-test.avi

grep -q 'applicationId "io.pihda.retrolens"' "$PROJECT_DIR/app/build.gradle"
grep -q 'package="io.pihda.retrolens"' "$PROJECT_DIR/app/src/main/AndroidManifest.xml"
grep -q 'APP_ABI := armeabi' "$PROJECT_DIR/app/src/main/jni/Application.mk"
grep -q 'LOCAL_MODULE := retrolens' "$PROJECT_DIR/app/src/main/jni/Android.mk"
grep -q 'P("piss_filter_2007"' "$PROJECT_DIR/app/src/main/jni/retrolens_core.cpp"
grep -q 'P("soviet_archive_1978"' "$PROJECT_DIR/app/src/main/jni/retrolens_core.cpp"
if grep -q 'android.permission.INTERNET' "$PROJECT_DIR/app/src/main/AndroidManifest.xml"; then
    echo "RetroLens must not request Internet permission" >&2
    exit 1
fi

if "$host_only"; then
    echo "Host-only smoke suite passed"
    exit 0
fi

if "$clean"; then
    "$SCRIPT_DIR/build.sh" --clean
else
    "$SCRIPT_DIR/build.sh"
fi

apk="$(latest_apk)"
"$SCRIPT_DIR/verify-apk.sh" "$apk"
temporary_dir="$(mktemp -d)"
trap 'rm -rf "$temporary_dir"' EXIT
unzip -p "$apk" lib/armeabi/libretrolens.so > "$temporary_dir/libretrolens.so"
readelf -h "$temporary_dir/libretrolens.so" | grep -q 'Machine:.*ARM'
if command -v ffprobe >/dev/null 2>&1; then
    echo "ffprobe available for device-generated Retro Clip validation"
fi
echo "Complete RetroLens smoke suite passed"
