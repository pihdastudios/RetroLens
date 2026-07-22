#!/usr/bin/env bash
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_env.sh"

require_file "$ANDROID_NDK_ROOT/ndk-build"
require_file "$PROJECT_DIR/app/src/main/jni/Android.mk"
require_file "$PROJECT_DIR/app/src/main/jni/Application.mk"

native_build="$PROJECT_DIR/app/build/native"
native_output="$native_build/libs/armeabi/libretrolens.so"
package_output="$PROJECT_DIR/app/src/main/jniLibs/armeabi/libretrolens.so"
mkdir -p "$native_build/obj" "$native_build/libs" "$(dirname "$package_output")"

echo "Building libretrolens.so for armeabi with NDK r14b"
"$ANDROID_NDK_ROOT/ndk-build" \
    NDK_PROJECT_PATH="$PROJECT_DIR/app/src/main" \
    APP_BUILD_SCRIPT="$PROJECT_DIR/app/src/main/jni/Android.mk" \
    NDK_APPLICATION_MK="$PROJECT_DIR/app/src/main/jni/Application.mk" \
    NDK_OUT="$native_build/obj" \
    NDK_LIBS_OUT="$native_build/libs" \
    APP_ABI=armeabi

require_file "$native_output"
cp "$native_output" "$package_output"
echo "Native output: $package_output"
