#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdint.h>

#include "display_probe.h"
#include "retrolens_core.h"

namespace {

enum SurfaceStatus {
    SURFACE_OK = 0,
    SURFACE_NO_WINDOW = 1,
    SURFACE_LOCK_FAILED = 2,
    SURFACE_FORMAT_UNSUPPORTED = 3,
    SURFACE_POST_FAILED = 4
};

static const uint32_t kProbeCookie = 0x52545042U;

struct DisplayProbe {
    DisplayProbe() : cookie(kProbeCookie) {}
    uint32_t cookie;
};

static DisplayProbe* from(jlong handle) {
    DisplayProbe* probe = reinterpret_cast<DisplayProbe*>((intptr_t)handle);
    return probe && probe->cookie == kProbeCookie ? probe : 0;
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeCreateDisplayProbe(JNIEnv*, jclass) {
    DisplayProbe* probe = new DisplayProbe();
    return (jlong)(intptr_t)probe;
}

extern "C" JNIEXPORT jint JNICALL Java_io_pihda_retrolens_NativeBridge_nativePostDisplayProbe(
    JNIEnv* env, jclass, jlong handle, jobject surface, jstring buildId) {
    if (!from(handle) || !surface)
        return SURFACE_NO_WINDOW;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window)
        return SURFACE_NO_WINDOW;
    int geometryResult =
        ANativeWindow_setBuffersGeometry(window, retrolens::kDisplayProbeWidth,
                                         retrolens::kDisplayProbeHeight, WINDOW_FORMAT_RGB_565);
    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(window, &buffer, 0) != 0) {
        ANativeWindow_release(window);
        return SURFACE_LOCK_FAILED;
    }

    uint16_t pixels[retrolens::kDisplayProbeWidth * retrolens::kDisplayProbeHeight];
    const char* build = buildId ? env->GetStringUTFChars(buildId, 0) : 0;
    bool rendered = retrolens::renderDisplayProbe(pixels, retrolens::kDisplayProbeWidth,
                                                  retrolens::kDisplayProbeHeight, build,
                                                  buffer.width, buffer.height, buffer.format);
    if (build)
        env->ReleaseStringUTFChars(buildId, build);

    int status = SURFACE_OK;
    if (!rendered || buffer.width <= 0 || buffer.height <= 0 || !buffer.bits) {
        status = SURFACE_LOCK_FAILED;
    } else if (!retrolens::blitRgb565(pixels, retrolens::kDisplayProbeWidth,
                                      retrolens::kDisplayProbeHeight, buffer.bits, buffer.width,
                                      buffer.height, buffer.stride, buffer.format)) {
        status = SURFACE_FORMAT_UNSUPPORTED;
    }
    int postResult = ANativeWindow_unlockAndPost(window);
    __android_log_print(ANDROID_LOG_INFO, "RetroLens",
                        "DisplayProbe: status=%d geometry=%d surface=%dx%d stride=%d format=%d",
                        status, geometryResult, buffer.width, buffer.height, buffer.stride,
                        buffer.format);
    ANativeWindow_release(window);
    if (postResult != 0 && status == SURFACE_OK)
        status = SURFACE_POST_FAILED;
    return status;
}

extern "C" JNIEXPORT void JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeClearDisplayProbe(JNIEnv*, jclass, jlong handle) {
    (void)from(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeDestroyDisplayProbe(JNIEnv*, jclass, jlong handle) {
    DisplayProbe* probe = from(handle);
    if (!probe)
        return;
    probe->cookie = 0;
    delete probe;
}
