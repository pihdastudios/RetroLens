#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdint.h>

#include "display_probe_worker.h"

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
    DisplayProbe(const char* buildId, int intervalMs, const char* storageRoot,
                 const char* cameraModel, const char* versionName)
        : cookie(kProbeCookie), worker(buildId, intervalMs, storageRoot, cameraModel, versionName) {
    }
    uint32_t cookie;
    retrolens::DisplayProbeWorker worker;
};

static DisplayProbe* from(jlong handle) {
    DisplayProbe* probe = reinterpret_cast<DisplayProbe*>((intptr_t)handle);
    return probe && probe->cookie == kProbeCookie ? probe : 0;
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL Java_io_pihda_retrolens_NativeBridge_nativeCreateDisplayProbe(
    JNIEnv* env, jclass, jstring buildId, jint intervalMs, jstring storageRoot, jstring cameraModel,
    jstring versionName) {
    const char* build = buildId ? env->GetStringUTFChars(buildId, 0) : 0;
    const char* storage = storageRoot ? env->GetStringUTFChars(storageRoot, 0) : 0;
    const char* model = cameraModel ? env->GetStringUTFChars(cameraModel, 0) : 0;
    const char* version = versionName ? env->GetStringUTFChars(versionName, 0) : 0;
    DisplayProbe* probe = new DisplayProbe(build, intervalMs, storage, model, version);
    if (build)
        env->ReleaseStringUTFChars(buildId, build);
    if (storage)
        env->ReleaseStringUTFChars(storageRoot, storage);
    if (model)
        env->ReleaseStringUTFChars(cameraModel, model);
    if (version)
        env->ReleaseStringUTFChars(versionName, version);
    if (!probe->worker.start()) {
        delete probe;
        return 0;
    }
    return (jlong)(intptr_t)probe;
}

extern "C" JNIEXPORT jint JNICALL Java_io_pihda_retrolens_NativeBridge_nativePostDisplayProbe(
    JNIEnv* env, jclass, jlong handle, jobject surface) {
    DisplayProbe* probe = from(handle);
    if (!probe || !surface)
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

    int status = SURFACE_OK;
    int frameNumber = 0;
    if (buffer.width <= 0 || buffer.height <= 0 || !buffer.bits) {
        status = SURFACE_LOCK_FAILED;
    } else {
        probe->worker.updateSurfaceInfo(buffer.width, buffer.height, buffer.format);
        if (!probe->worker.blitLatest(buffer.bits, buffer.width, buffer.height, buffer.stride,
                                      buffer.format, &frameNumber))
            status = SURFACE_FORMAT_UNSUPPORTED;
    }
    int postResult = ANativeWindow_unlockAndPost(window);
    if (status != SURFACE_OK || frameNumber % 80 == 0)
        __android_log_print(ANDROID_LOG_INFO, "RetroLens",
                            "PhotoRuntime: status=%d geometry=%d surface=%dx%d stride=%d "
                            "format=%d frame=%d",
                            status, geometryResult, buffer.width, buffer.height, buffer.stride,
                            buffer.format, frameNumber);
    ANativeWindow_release(window);
    if (postResult != 0 && status == SURFACE_OK)
        status = SURFACE_POST_FAILED;
    return status;
}

extern "C" JNIEXPORT void JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeUpdateDisplayProbeSequence(
    JNIEnv*, jclass, jlong handle, jint state, jint receivedFrames, jint releasedFrames,
    jint lastJpegBytes, jlong firstTimestampMs, jlong lastTimestampMs) {
    DisplayProbe* probe = from(handle);
    if (!probe)
        return;
    probe->worker.updateSequenceMetrics(state, receivedFrames, releasedFrames, lastJpegBytes,
                                        firstTimestampMs, lastTimestampMs);
}

extern "C" JNIEXPORT jint JNICALL Java_io_pihda_retrolens_NativeBridge_nativeSubmitDisplayProbeJpeg(
    JNIEnv* env, jclass, jlong handle, jobject jpeg, jint length, jlong timestampMs) {
    DisplayProbe* probe = from(handle);
    if (!probe || !jpeg || length < 4)
        return retrolens::kFilterSubmitInvalid;
    void* bytes = env->GetDirectBufferAddress(jpeg);
    jlong capacity = env->GetDirectBufferCapacity(jpeg);
    if (!bytes || capacity < length)
        return retrolens::kFilterSubmitInvalid;
    return probe->worker.submitJpeg(static_cast<const unsigned char*>(bytes), length, timestampMs);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeChangeDisplayProbeStyle(JNIEnv*, jclass, jlong handle,
                                                                   jint delta) {
    DisplayProbe* probe = from(handle);
    return probe ? probe->worker.changeStyle(delta) : -1;
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_pihda_retrolens_NativeBridge_nativeDisplayProbeKey(
    JNIEnv*, jclass, jlong handle, jint key, jboolean down, jlong timestampMs) {
    DisplayProbe* probe = from(handle);
    return probe && probe->worker.key(key, down == JNI_TRUE, timestampMs) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeDisplayProbeTouch(
    JNIEnv*, jclass, jlong handle, jint action, jfloat x, jfloat y, jlong timestampMs) {
    DisplayProbe* probe = from(handle);
    if (probe)
        probe->worker.touch(action, x, y, timestampMs);
}

extern "C" JNIEXPORT jint JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeRequestDisplayProbePhoto(JNIEnv*, jclass, jlong handle,
                                                                    jlong timestampMs) {
    DisplayProbe* probe = from(handle);
    return probe ? probe->worker.requestPhoto(timestampMs) : retrolens::kPhotoRequestUnavailable;
}

extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeSetDisplayProbeFocus(
    JNIEnv*, jclass, jlong handle, jboolean active) {
    DisplayProbe* probe = from(handle);
    if (probe)
        probe->worker.setFocus(active == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeGetDisplayProbeStats(
    JNIEnv* env, jclass, jlong handle, jintArray output) {
    DisplayProbe* probe = from(handle);
    if (!probe || !output || env->GetArrayLength(output) < 8)
        return;
    retrolens::FilterProbeMetrics metrics;
    probe->worker.getFilterStats(&metrics);
    jint values[8] = {metrics.selectedPreset,    metrics.photoSavedCount, metrics.photoFailedCount,
                      metrics.photoEncodedBytes, metrics.photoStatus,     metrics.galleryPhotoCount,
                      metrics.processedFrames,   metrics.droppedFrames};
    env->SetIntArrayRegion(output, 0, 8, values);
}

extern "C" JNIEXPORT void JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeClearDisplayProbe(JNIEnv*, jclass, jlong handle) {
    (void)from(handle);
}

extern "C" JNIEXPORT jint JNICALL Java_io_pihda_retrolens_NativeBridge_nativeDestroyDisplayProbe(
    JNIEnv* env, jclass, jlong handle, jintArray stats) {
    DisplayProbe* probe = from(handle);
    if (!probe)
        return -1;
    int frameCount = 0;
    int postCount = 0;
    probe->worker.getStats(&frameCount, &postCount);
    int stopMs = probe->worker.stop();
    if (stats && env->GetArrayLength(stats) >= 3) {
        jint values[3] = {frameCount, postCount, stopMs};
        env->SetIntArrayRegion(stats, 0, 3, values);
    }
    probe->cookie = 0;
    delete probe;
    return stopMs;
}
