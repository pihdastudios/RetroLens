package io.pihda.retrolens;

import android.graphics.PixelFormat;
import android.os.Handler;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;

/** Owns the synchronous, storage-free native display probe. */
public final class NativeDisplayProbeController implements SurfaceHolder.Callback {
  private static final int FRAME_INTERVAL_MS = 125;
  private static final int MAX_EXPECTED_STOP_MS = 250;

  public interface Listener {
    void onDisplayProbeResult(int status);
  }

  private final SurfaceView surfaceView;
  private final SurfaceHolder holder;
  private final Listener listener;
  private final Handler handler = new Handler();
  private final int[] finalStats = new int[3];
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

  public NativeDisplayProbeController(SurfaceView view, Listener listener) {
    this.surfaceView = view;
    this.listener = listener;
    holder = view.getHolder();
    holder.setFormat(PixelFormat.RGB_565);
    view.setZOrderMediaOverlay(true);
  }

  public void start() {
    if (active)
      return;
    active = true;
    firstPostReported = false;
    surfaceView.setVisibility(View.VISIBLE);
    if (!NativeBridge.load()) {
      fail(NativeBridge.SURFACE_NO_WINDOW);
      return;
    }
    handle = NativeBridge.nativeCreateDisplayProbe(NativeBridge.BUILD_ID, FRAME_INTERVAL_MS);
    if (handle == 0L) {
      fail(NativeBridge.SURFACE_NO_WINDOW);
      return;
    }
    holder.addCallback(this);
  }

  public void stop() {
    if (!active && handle == 0L)
      return;
    active = false;
    handler.removeCallbacks(animationTick);
    tickScheduled = false;
    holder.removeCallback(this);
    surfaceReady = false;
    destroyNativeProbe();
    Logger.info("DisplayProbe: stopped");
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
    Logger.info("DisplayProbe: synchronous post status=" + status);
    if (status != NativeBridge.SURFACE_OK) {
      fail(status);
      return false;
    }
    if (!firstPostReported) {
      firstPostReported = true;
      listener.onDisplayProbeResult(status);
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
