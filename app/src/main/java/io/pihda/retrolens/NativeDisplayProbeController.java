package io.pihda.retrolens;

import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Handler;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import java.nio.ByteBuffer;

/** Owns the synchronous full-screen native photo runtime. */
public final class NativeDisplayProbeController
    implements SurfaceHolder.Callback, View.OnTouchListener {
  public static final int SEQUENCE_OFF = 0;
  public static final int SEQUENCE_STARTING = 1;
  public static final int SEQUENCE_ACTIVE = 2;
  public static final int SEQUENCE_ERROR = 3;
  public static final int SEQUENCE_STOPPING = 4;
  private static final int FRAME_INTERVAL_MS = 125;
  private static final int MAX_EXPECTED_STOP_MS = 3000;

  public interface Listener {
    void onDisplayProbeResult(int status);
  }

  private final SurfaceView surfaceView;
  private final SurfaceHolder holder;
  private final Listener listener;
  private final Handler handler = new Handler();
  private final int[] finalStats = new int[3];
  private final int[] runtimeStats = new int[12];
  private final String storageRoot;
  private final Runnable animationTick = new Runnable() {
    @Override
    public void run() {
      tickScheduled = false;
      if (active && surfaceReady && postFrame())
        scheduleNextFrame();
    }
  };
  private long handle;
  private boolean active;
  private boolean surfaceReady;
  private boolean tickScheduled;
  private boolean firstPostReported;
  private int reportedPhotoSaves;
  private int reportedPhotoFailures;
  private int reportedStorageState = -1;

  public NativeDisplayProbeController(
      SurfaceView view, Listener listener, StorageController.Result storage) {
    this.surfaceView = view;
    this.listener = listener;
    storageRoot = storage != null && storage.isReady() ? storage.root : "";
    holder = view.getHolder();
    holder.setFormat(PixelFormat.RGB_565);
    view.setZOrderMediaOverlay(true);
  }

  public synchronized void start() {
    if (active)
      return;
    active = true;
    firstPostReported = false;
    reportedPhotoSaves = 0;
    reportedPhotoFailures = 0;
    reportedStorageState = -1;
    surfaceView.setVisibility(View.VISIBLE);
    surfaceView.setOnTouchListener(this);
    if (!NativeBridge.load()) {
      fail(NativeBridge.SURFACE_NO_WINDOW);
      return;
    }
    handle = NativeBridge.nativeCreateDisplayProbe(
        NativeBridge.BUILD_ID, FRAME_INTERVAL_MS, storageRoot, Build.MODEL, "1.0.0");
    if (handle == 0L) {
      fail(NativeBridge.SURFACE_NO_WINDOW);
      return;
    }
    holder.addCallback(this);
  }

  public synchronized void stop() {
    if (!active && handle == 0L)
      return;
    active = false;
    handler.removeCallbacks(animationTick);
    tickScheduled = false;
    holder.removeCallback(this);
    surfaceReady = false;
    surfaceView.setOnTouchListener(null);
    destroyNativeProbe();
    surfaceView.setVisibility(View.GONE);
    Logger.info("DisplayProbe: stopped");
  }

  public synchronized void updateSequenceMetrics(int state, int receivedFrames, int releasedFrames,
      int lastJpegBytes, long firstTimestampMs, long lastTimestampMs) {
    long probe = handle;
    if (probe == 0L)
      return;
    NativeBridge.nativeUpdateDisplayProbeSequence(probe, state, receivedFrames, releasedFrames,
        lastJpegBytes, firstTimestampMs, lastTimestampMs);
  }

  public synchronized int submitJpeg(ByteBuffer jpeg, int length, long timestampMs) {
    long probe = handle;
    if (probe == 0L)
      return NativeBridge.FILTER_SUBMIT_INVALID;
    return NativeBridge.nativeSubmitDisplayProbeJpeg(probe, jpeg, length, timestampMs);
  }

  public synchronized int changeStyle(int delta) {
    long probe = handle;
    return probe == 0L ? -1 : NativeBridge.nativeChangeDisplayProbeStyle(probe, delta);
  }

  public synchronized boolean key(int key, boolean down) {
    long probe = handle;
    return probe != 0L
        && NativeBridge.nativeDisplayProbeKey(probe, key, down, SystemClock.elapsedRealtime());
  }

  public synchronized int requestPhoto() {
    long probe = handle;
    return probe == 0L
        ? NativeBridge.PHOTO_UNAVAILABLE
        : NativeBridge.nativeRequestDisplayProbePhoto(probe, SystemClock.elapsedRealtime());
  }

  public synchronized void setFocus(boolean active) {
    if (handle != 0L)
      NativeBridge.nativeSetDisplayProbeFocus(handle, active);
  }

  @Override
  public synchronized boolean onTouch(View view, MotionEvent event) {
    if (handle == 0L || view.getWidth() <= 0 || view.getHeight() <= 0)
      return false;
    float x = event.getX() * 240.0f / view.getWidth();
    float y = event.getY() * 180.0f / view.getHeight();
    NativeBridge.nativeDisplayProbeTouch(
        handle, event.getAction(), x, y, SystemClock.elapsedRealtime());
    return true;
  }

  @Override
  public void surfaceCreated(SurfaceHolder surfaceHolder) {
    surfaceReady = true;
    beginPosting();
  }

  @Override
  public void surfaceChanged(SurfaceHolder surfaceHolder, int format, int width, int height) {
    Logger.info(
        "DisplayProbe: surface changed width=" + width + " height=" + height + " format=" + format);
    surfaceReady = true;
    beginPosting();
  }

  @Override
  public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
    surfaceReady = false;
    Logger.info("DisplayProbe: surface destroyed");
  }

  private void beginPosting() {
    handler.removeCallbacks(animationTick);
    tickScheduled = false;
    if (postFrame())
      scheduleNextFrame();
  }

  private boolean postFrame() {
    if (!active || !surfaceReady || handle == 0L || !holder.getSurface().isValid())
      return false;
    int status = NativeBridge.nativePostDisplayProbe(handle, holder.getSurface());
    if (status != NativeBridge.SURFACE_OK) {
      Logger.error("PhotoRuntime: synchronous post failed status=" + status);
      fail(status);
      return false;
    }
    if (!firstPostReported) {
      firstPostReported = true;
      Logger.info("PhotoRuntime: first synchronous post complete");
      listener.onDisplayProbeResult(status);
    }
    NativeBridge.nativeGetDisplayProbeStats(handle, runtimeStats);
    if (runtimeStats[6] == 0 && runtimeStats[7] >= 3) {
      Logger.error("PhotoRuntime: analytical decode failed repeatedly; showing Sony fallback");
      fail(NativeBridge.SURFACE_FORMAT_UNSUPPORTED);
      return false;
    }
    if (runtimeStats[1] != reportedPhotoSaves) {
      reportedPhotoSaves = runtimeStats[1];
      Logger.info("PhotoRuntime: processed derivative saved count=" + runtimeStats[1]
          + " bytes=" + runtimeStats[3] + " galleryCount=" + runtimeStats[5]);
      Logger.flush();
    }
    if (runtimeStats[2] != reportedPhotoFailures) {
      reportedPhotoFailures = runtimeStats[2];
      Logger.error("PhotoRuntime: processed derivative failure count=" + runtimeStats[2]);
    }
    if (runtimeStats[8] != reportedStorageState) {
      reportedStorageState = runtimeStats[8];
      if (runtimeStats[8] == 1)
        Logger.info("PhotoRuntime: storage ready freeMiB=" + runtimeStats[11]);
      else
        Logger.error("PhotoRuntime: storage unavailable state=" + runtimeStats[8] + " stage="
            + runtimeStats[9] + " errno=" + runtimeStats[10] + " freeMiB=" + runtimeStats[11]);
    }
    return true;
  }

  private void scheduleNextFrame() {
    if (!active || tickScheduled)
      return;
    tickScheduled = true;
    handler.postDelayed(animationTick, FRAME_INTERVAL_MS);
  }

  private void fail(int status) {
    active = false;
    handler.removeCallbacks(animationTick);
    tickScheduled = false;
    holder.removeCallback(this);
    surfaceReady = false;
    destroyNativeProbe();
    surfaceView.setVisibility(View.GONE);
    listener.onDisplayProbeResult(status);
  }

  private void destroyNativeProbe() {
    long probe = handle;
    handle = 0L;
    if (probe == 0L)
      return;
    NativeBridge.nativeClearDisplayProbe(probe);
    int stopMs = NativeBridge.nativeDestroyDisplayProbe(probe, finalStats);
    Logger.info("DisplayProbe: worker stopped frames=" + finalStats[0] + " posts=" + finalStats[1]
        + " joinMs=" + finalStats[2] + " returnMs=" + stopMs);
    if (stopMs < 0 || stopMs > MAX_EXPECTED_STOP_MS)
      Logger.error("DisplayProbe: worker shutdown outside bound milliseconds=" + stopMs);
  }
}
