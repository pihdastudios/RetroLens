package io.pihda.retrolens;

import android.graphics.PixelFormat;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;

/** Owns the synchronous, storage-free native display probe. */
public final class NativeDisplayProbeController implements SurfaceHolder.Callback {
  public interface Listener {
    void onDisplayProbeResult(int status);
  }

  private final SurfaceView surfaceView;
  private final SurfaceHolder holder;
  private final Listener listener;
  private long handle;
  private boolean active;
  private boolean surfaceReady;

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
    surfaceView.setVisibility(View.VISIBLE);
    if (!NativeBridge.load()) {
      fail(NativeBridge.SURFACE_NO_WINDOW);
      return;
    }
    handle = NativeBridge.nativeCreateDisplayProbe();
    if (handle == 0L) {
      fail(NativeBridge.SURFACE_NO_WINDOW);
      return;
    }
    holder.addCallback(this);
    if (surfaceReady && holder.getSurface().isValid())
      post();
  }

  public void stop() {
    if (!active && handle == 0L)
      return;
    active = false;
    holder.removeCallback(this);
    surfaceReady = false;
    long probe = handle;
    handle = 0L;
    if (probe != 0L) {
      NativeBridge.nativeClearDisplayProbe(probe);
      NativeBridge.nativeDestroyDisplayProbe(probe);
    }
    Logger.info("DisplayProbe: stopped");
  }

  @Override
  public void surfaceCreated(SurfaceHolder surfaceHolder) {
    surfaceReady = true;
    post();
  }

  @Override
  public void surfaceChanged(SurfaceHolder surfaceHolder, int format, int width, int height) {
    Logger.info(
        "DisplayProbe: surface changed width=" + width + " height=" + height + " format=" + format);
    surfaceReady = true;
    post();
  }

  @Override
  public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
    surfaceReady = false;
    Logger.info("DisplayProbe: surface destroyed");
  }

  private void post() {
    if (!active || !surfaceReady || handle == 0L || !holder.getSurface().isValid())
      return;
    int status =
        NativeBridge.nativePostDisplayProbe(handle, holder.getSurface(), NativeBridge.BUILD_ID);
    Logger.info("DisplayProbe: synchronous post status=" + status);
    if (status != NativeBridge.SURFACE_OK) {
      fail(status);
      return;
    }
    listener.onDisplayProbeResult(status);
  }

  private void fail(int status) {
    surfaceView.setVisibility(View.GONE);
    listener.onDisplayProbeResult(status);
  }
}
