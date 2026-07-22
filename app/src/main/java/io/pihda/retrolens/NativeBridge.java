package io.pihda.retrolens;

import android.view.Surface;
import java.nio.ByteBuffer;

/** Narrow JNI boundary. Pixels, UI, persistence, and recording remain native. */
public final class NativeBridge {
  public static final String BUILD_ID = "sequence-probe-20260722-e";
  public static final boolean SAFE_BASELINE_ENABLED = false;
  public static final boolean DISPLAY_PROBE_ENABLED = true;
  public static final boolean DISPLAY_PROBE_THREAD_ENABLED = true;
  public static final boolean ANALYTICAL_PREVIEW_ENABLED = true;
  public static final boolean NATIVE_OUTPUT_ENABLED = false;
  public static final boolean RETRO_CLIP_ENABLED = false;
  public static final boolean PROCESSED_DERIVATIVE_ENABLED = false;

  public static final int SURFACE_OK = 0;
  public static final int SURFACE_NO_WINDOW = 1;
  public static final int SURFACE_LOCK_FAILED = 2;
  public static final int SURFACE_FORMAT_UNSUPPORTED = 3;
  public static final int SURFACE_POST_FAILED = 4;
  public static final int KEY_PREVIOUS = 1;
  public static final int KEY_NEXT = 2;
  public static final int KEY_UP = 3;
  public static final int KEY_DOWN = 4;
  public static final int KEY_CONFIRM = 5;
  public static final int KEY_BROWSER = 6;
  public static final int KEY_GALLERY = 7;
  public static final int KEY_COMPARE = 8;
  public static final int KEY_BACK = 9;
  public static final int KEY_RECORD = 10;
  public static final int KEY_FOCUS = 11;
  public static final int KEY_CAPTURE = 12;

  private static boolean loaded;

  private NativeBridge() {}

  public static synchronized boolean load() {
    if (loaded)
      return true;
    try {
      System.loadLibrary("retrolens");
      loaded = true;
      Logger.info("Native: libretrolens loaded");
    } catch (Throwable throwable) {
      Logger.error("Native: load failed " + throwable.toString());
    }
    return loaded;
  }

  public static native long nativeCreate(
      String storageRoot, String cameraModel, String versionName);
  public static native void nativeDestroy(long handle);
  public static native int nativeSetSurface(long handle, Surface surface);
  public static native void nativeClearSurface(long handle);
  public static native boolean nativeSubmitFrame(
      long handle, ByteBuffer jpeg, int length, long timestampMs);
  public static native void nativeSetPreviewError(long handle, String message);
  public static native void nativeKey(long handle, int key, boolean down, long timestampMs);
  public static native void nativeTouch(
      long handle, int action, float x, float y, long timestampMs);
  public static native void nativeCaptureRequested(long handle, long timestampMs);
  public static native void nativeSaveSnapshot(long handle, long timestampMs);
  public static native void nativeToggleRecording(long handle, long timestampMs);
  public static native void nativeStopRecording(long handle, boolean interrupted, long timestampMs);
  public static native void nativeGetStats(long handle, int[] output);

  public static native long nativeCreateDisplayProbe(String buildId, int intervalMs);
  public static native int nativePostDisplayProbe(long handle, Surface surface);
  public static native void nativeUpdateDisplayProbeSequence(long handle, int state,
      int receivedFrames, int releasedFrames, int lastJpegBytes, long firstTimestampMs,
      long lastTimestampMs);
  public static native void nativeClearDisplayProbe(long handle);
  public static native int nativeDestroyDisplayProbe(long handle, int[] stats);
}
