package io.pihda.retrolens;

import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import com.sony.scalar.hardware.CameraEx;
import java.nio.ByteBuffer;

/** Sony lifecycle and input adapter for the native RetroLens image engine. */
public final class RetroLensActivity extends BaseActivity
    implements CameraSequenceFrameSource.Listener, CameraSequenceFrameSource.FrameConsumer,
               SonyCameraController.Listener, SurfaceHolder.Callback, View.OnTouchListener {
  private final Object lifecycleLock = new Object();
  private SonyCameraController cameraController;
  private CameraSequenceFrameSource frameSource;
  private SurfaceView effectSurface;
  private long nativeHandle;
  private boolean resumed;
  private Handler mainHandler;
  private int captureGeneration;
  private int savedGeneration;

  @Override
  protected void onCreate(Bundle state) {
    Logger.installUncaughtExceptionHandler();
    super.onCreate(state);
    setContentView(R.layout.activity_retrolens);
    SurfaceView normal = (SurfaceView) findViewById(R.id.sonyPreviewSurface);
    effectSurface = (SurfaceView) findViewById(R.id.retroLensSurface);
    effectSurface.setZOrderMediaOverlay(true);
    effectSurface.getHolder().addCallback(this);
    effectSurface.setOnTouchListener(this);
    mainHandler = new Handler();
    cameraController = new SonyCameraController(normal, this);
    Logger.info("RetroLens: created model=" + Build.MODEL + " sdk=" + Build.VERSION.SDK_INT);
  }

  @Override
  protected void onResume() {
    super.onResume();
    setAutoPowerOffMode(false);
    synchronized (lifecycleLock) {
      resumed = true;
      if (NativeBridge.load()) {
        nativeHandle = NativeBridge.nativeCreate(
            Environment.getExternalStorageDirectory().getAbsolutePath(), Build.MODEL, "1.0.0");
      }
    }
    mainHandler.removeCallbacksAndMessages(null);
    if (nativeHandle != 0L && effectSurface.getHolder().getSurface().isValid()) {
      NativeBridge.nativeSetSurface(nativeHandle, effectSurface.getHolder().getSurface());
    }
    cameraController.start();
    Logger.info("RetroLens: resume complete nativeHandle=" + nativeHandle);
  }

  @Override
  protected void onPause() {
    mainHandler.removeCallbacksAndMessages(null);
    long handle;
    CameraSequenceFrameSource source;
    synchronized (lifecycleLock) {
      resumed = false;
      source = frameSource;
      frameSource = null;
      handle = nativeHandle;
    }
    if (handle != 0L)
      NativeBridge.nativeStopRecording(handle, true, SystemClock.elapsedRealtime());
    if (source != null) {
      if (!source.stopAndJoin(2500L)) {
        source.requestSequenceStop();
        source.stopAndJoin(1500L);
      }
      source.release();
    }
    cameraController.stop();
    synchronized (lifecycleLock) {
      nativeHandle = 0L;
    }
    if (handle != 0L) {
      NativeBridge.nativeClearSurface(handle);
      NativeBridge.nativeDestroy(handle);
    }
    setAutoPowerOffMode(true);
    Logger.info("RetroLens: pause release complete");
    super.onPause();
  }

  @Override
  public void onCameraReady(CameraEx cameraEx) {
    synchronized (lifecycleLock) {
      if (!resumed || frameSource != null)
        return;
      frameSource = new CameraSequenceFrameSource(this, this);
      frameSource.start(cameraEx);
    }
  }

  @Override
  public void onCameraUnavailable(String reason) {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeSetPreviewError(handle, reason);
  }

  @Override
  public void onCapturedJpeg(int byteCount, long timestampMs) {
    savedGeneration = captureGeneration;
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeSaveSnapshot(handle, timestampMs);
  }

  @Override
  public void onSequenceStarted(CameraSequenceFrameSource source) {
    Logger.info("RetroLens: analytical preview started");
  }
  @Override
  public void onFirstFrame(CameraSequenceFrameSource source, String format, int length) {
    Logger.info("RetroLens: first analytical frame format=" + format + " bytes=" + length);
  }
  @Override
  public void onSequenceUnavailable(CameraSequenceFrameSource source, String reason) {
    Logger.error("RetroLens: analytical preview unavailable " + reason);
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeSetPreviewError(handle, reason);
  }
  @Override
  public void onFrame(ByteBuffer buffer, int length, long timestampMs) {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeSubmitFrame(handle, buffer, length, timestampMs);
  }

  @Override
  public void surfaceCreated(SurfaceHolder holder) {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeSetSurface(handle, holder.getSurface());
  }
  @Override
  public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeSetSurface(handle, holder.getSurface());
  }
  @Override
  public void surfaceDestroyed(SurfaceHolder holder) {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeClearSurface(handle);
  }

  @Override
  public boolean onTouch(View view, MotionEvent event) {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeTouch(
          handle, event.getAction(), event.getX(), event.getY(), SystemClock.elapsedRealtime());
    return true;
  }

  private long getHandle() {
    synchronized (lifecycleLock) {
      return resumed ? nativeHandle : 0L;
    }
  }
  private void key(int key, boolean down) {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeKey(handle, key, down, SystemClock.elapsedRealtime());
  }

  @Override
  protected boolean onLeftKeyDown() {
    key(NativeBridge.KEY_PREVIOUS, true);
    return true;
  }
  @Override
  protected boolean onRightKeyDown() {
    key(NativeBridge.KEY_NEXT, true);
    return true;
  }
  @Override
  protected boolean onUpKeyDown() {
    key(NativeBridge.KEY_UP, true);
    return true;
  }
  @Override
  protected boolean onDownKeyDown() {
    key(NativeBridge.KEY_DOWN, true);
    return true;
  }
  @Override
  protected boolean onEnterKeyDown() {
    key(NativeBridge.KEY_CONFIRM, true);
    return true;
  }
  @Override
  protected boolean onMenuKeyDown() {
    key(NativeBridge.KEY_BROWSER, true);
    return true;
  }
  @Override
  protected boolean onPlayKeyDown() {
    key(NativeBridge.KEY_GALLERY, true);
    return true;
  }
  @Override
  protected boolean onC1KeyDown() {
    key(NativeBridge.KEY_COMPARE, true);
    return true;
  }
  @Override
  protected boolean onC1KeyUp() {
    key(NativeBridge.KEY_COMPARE, false);
    return true;
  }
  @Override
  protected boolean onFocusKeyDown() {
    key(NativeBridge.KEY_FOCUS, true);
    cameraController.focus(true);
    return true;
  }
  @Override
  protected boolean onFocusKeyUp() {
    key(NativeBridge.KEY_FOCUS, false);
    cameraController.focus(false);
    return true;
  }
  @Override
  protected boolean onShutterKeyDown() {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeCaptureRequested(handle, SystemClock.elapsedRealtime());
    final int generation = ++captureGeneration;
    if (cameraController.capture()) {
      mainHandler.postDelayed(new Runnable() {
        @Override
        public void run() {
          if (resumed && savedGeneration < generation) {
            long activeHandle = getHandle();
            if (activeHandle != 0L) {
              Logger.info("RetroLens: using preview derivative fallback generation=" + generation);
              NativeBridge.nativeSaveSnapshot(activeHandle, SystemClock.elapsedRealtime());
              savedGeneration = generation;
            }
          }
        }
      }, 900L);
    }
    return true;
  }
  @Override
  protected boolean onShutterKeyUp() {
    cameraController.releaseShutter();
    return true;
  }
  @Override
  protected boolean onMovieKeyDown() {
    long handle = getHandle();
    if (handle != 0L)
      NativeBridge.nativeToggleRecording(handle, SystemClock.elapsedRealtime());
    return true;
  }
  @Override
  protected boolean onUpperDialChanged(int value) {
    key(value >= 0 ? NativeBridge.KEY_NEXT : NativeBridge.KEY_PREVIOUS, true);
    return true;
  }
  @Override
  protected boolean onLowerDialChanged(int value) {
    key(value >= 0 ? NativeBridge.KEY_DOWN : NativeBridge.KEY_UP, true);
    return true;
  }
  @Override
  protected boolean onDeleteKeyUp() {
    key(NativeBridge.KEY_BACK, true);
    return super.onDeleteKeyUp();
  }

  @Override
  protected void setColorDepth(boolean highQuality) {
    super.setColorDepth(true);
  }
}
