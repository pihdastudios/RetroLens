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
grep -q 'static const int kFrameWidth = 80' "$PROJECT_DIR/app/src/main/jni/retrolens_core.h"
grep -q 'static const bool kRetroClipEnabled = false' "$PROJECT_DIR/app/src/main/jni/retrolens_core.h"
grep -q 'static const bool kProcessedDerivativeEnabled = false' \
    "$PROJECT_DIR/app/src/main/jni/retrolens_core.h"
grep -q 'RETRO_CLIP_ENABLED = false' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'PROCESSED_DERIVATIVE_ENABLED = false' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'SAFE_BASELINE_ENABLED = false' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'DISPLAY_PROBE_ENABLED = true' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'DISPLAY_PROBE_THREAD_ENABLED = true' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'ANALYTICAL_PREVIEW_ENABLED = true' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'FILTER_PANEL_ENABLED = true' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'NATIVE_OUTPUT_ENABLED = false' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'EXTERNAL_LOGGING_ENABLED = false' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/Logger.java"
grep -q 'nativeProbeSurface' "$PROJECT_DIR/app/src/main/res/layout/activity_retrolens.xml"
grep -q 'android:layout_width="240dp"' \
    "$PROJECT_DIR/app/src/main/res/layout/activity_retrolens.xml"
grep -q 'android:layout_height="180dp"' \
    "$PROJECT_DIR/app/src/main/res/layout/activity_retrolens.xml"
grep -q 'kDisplayProbeWidth = 240' "$PROJECT_DIR/app/src/main/jni/display_probe.h"
grep -q 'kDisplayProbeHeight = 180' "$PROJECT_DIR/app/src/main/jni/display_probe.h"
grep -q 'reduced_jpeg_decoder.cpp display_probe.cpp display_probe_worker.cpp display_probe_jni.cpp' \
    "$PROJECT_DIR/app/src/main/jni/Android.mk"
if grep -q 'retroLensSurface' "$PROJECT_DIR/app/src/main/res/layout/activity_retrolens.xml"; then
    echo "Display probe must not restore the full native output surface" >&2
    exit 1
fi
grep -q 'new CameraSequenceFrameSource' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/RetroLensActivity.java"
grep -q 'ByteBuffer.allocateDirect(MAX_JPEG_SIZE)' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/CameraSequenceFrameSource.java"
if [[ $(grep -c 'ByteBuffer.allocateDirect' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/CameraSequenceFrameSource.java") -ne 1 ]]; then
    echo "Sequence probe must allocate exactly one reusable direct buffer" >&2
    exit 1
fi
grep -q 'getPreviewSequenceFrames(1)' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/CameraSequenceFrameSource.java"
grep -q 'finally {' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/CameraSequenceFrameSource.java"
grep -q 'memory.release()' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/CameraSequenceFrameSource.java"
if grep -Eq 'Environment|FileOutputStream|FileChannel|getExternalStorageDirectory' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/CameraSequenceFrameSource.java"; then
    echo "Acquisition probe must not contain an external-storage frame dump path" >&2
    exit 1
fi
if grep -Eq 'nativeSubmitFrame|nativeCreate\(' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/RetroLensActivity.java"; then
    echo "Filter panel probe must not create or submit to the full native runtime" >&2
    exit 1
fi
grep -q 'nativeSubmitDisplayProbeJpeg' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'nativeChangeDisplayProbeStyle' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeBridge.java"
grep -q 'displayProbeController.submitJpeg' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/RetroLensActivity.java"
grep -q 'displayProbeController.changeStyle(-1)' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/RetroLensActivity.java"
grep -q 'displayProbeController.changeStyle(1)' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/RetroLensActivity.java"
if grep -q 'NativeBridge.load' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/RetroLensActivity.java"; then
    echo "Activity must delegate native loading to the isolated display probe" >&2
    exit 1
fi
if grep -q 'nativeCreate(' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeDisplayProbeController.java"; then
    echo "Display probe must not create the full native runtime" >&2
    exit 1
fi
if grep -Eq 'CameraSequence|Environment|getExternalStorageDirectory|new Thread' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeDisplayProbeController.java"; then
    echo "Thread probe must not add a Java worker or storage access" >&2
    exit 1
fi
grep -q 'FRAME_INTERVAL_MS = 125' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeDisplayProbeController.java"
if [[ $(grep -c 'new Runnable' \
    "$PROJECT_DIR/app/src/main/java/io/pihda/retrolens/NativeDisplayProbeController.java") -ne 1 ]]; then
    echo "Thread probe must use exactly one reusable Java cadence runnable" >&2
    exit 1
fi
if grep -Eq 'ANativeWindow|CameraSequence|fopen|mkdir' \
    "$PROJECT_DIR/app/src/main/jni/display_probe_worker.cpp"; then
    echo "Offscreen probe worker must not own surfaces, camera APIs, or files" >&2
    exit 1
fi
grep -q 'unsigned char jpeg_\[kFilterProbeInputCapacity\]' \
    "$PROJECT_DIR/app/src/main/jni/display_probe_worker.h"
if grep -Eq 'malloc|new |fopen|mkdir|encodeJpeg|AviWriter' \
    "$PROJECT_DIR/app/src/main/jni/display_probe_worker.cpp"; then
    echo "Filter panel worker must use fixed buffers and contain no storage or recorder path" >&2
    exit 1
fi
grep -q 'kFilterProbeStyleCount = 10' \
    "$PROJECT_DIR/app/src/main/jni/display_probe_worker.h"
for style in olive_pocket cga_shock one_bit_desktop consumer_crt vhs_rental \
    soviet_archive_1978 newsprint comic_ink piss_filter_2007 thermal_false_color; do
    grep -q "\"$style\"" "$PROJECT_DIR/app/src/main/jni/display_probe_worker.cpp"
done
if grep -q 'android.permission.INTERNET' "$PROJECT_DIR/app/src/main/AndroidManifest.xml"; then
    echo "RetroLens must not request Internet permission" >&2
    exit 1
fi
if grep -q 'android.permission.WRITE_EXTERNAL_STORAGE' \
    "$PROJECT_DIR/app/src/main/AndroidManifest.xml"; then
    echo "Safe baseline must not request external-storage write permission" >&2
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
