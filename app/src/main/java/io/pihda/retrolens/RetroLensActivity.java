package io.pihda.retrolens;

import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import com.sony.scalar.hardware.CameraEx;
import java.nio.ByteBuffer;

/** CameraSequence acquisition probe with no decode, storage, or video path. */
public final class RetroLensActivity extends BaseActivity
    implements SonyCameraController.Listener, NativeDisplayProbeController.Listener,
               CameraSequenceFrameSource.Listener, CameraSequenceFrameSource.FrameConsumer {
  private SonyCameraController cameraController;
  private NativeDisplayProbeController displayProbeController;
  private volatile CameraSequenceFrameSource frameSource;
  private StartupStatusView statusView;
  private Handler mainHandler;
  private CameraEx readyCamera;
  private volatile boolean resumed;
  private boolean displayProbeReady;
  private boolean cameraQuarantined;
  private int receivedFrames;
  private int releasedFrames;
  private int lastJpegBytes;
  private long firstFrameTimestampMs;
  private long lastFrameTimestampMs;

  @Override
  protected void onCreate(Bundle state) {
    Logger.installUncaughtExceptionHandler();
    super.onCreate(state);
    setContentView(R.layout.activity_retrolens);
    statusView = (StartupStatusView) findViewById(R.id.startupStatus);
    cameraController = new SonyCameraController(
        (android.view.SurfaceView) findViewById(R.id.sonyPreviewSurface), this);
    displayProbeController = new NativeDisplayProbeController(
        (android.view.SurfaceView) findViewById(R.id.nativeProbeSurface), this);
    mainHandler = new Handler();
    Logger.startSession(NativeBridge.BUILD_ID);
    Logger.info("RetroLens: sequence acquisition probe created model=" + Build.MODEL + " sdk="
        + Build.VERSION.SDK_INT + " abi=" + Build.CPU_ABI + " build=" + NativeBridge.BUILD_ID);
  }

  @Override
  protected void onResume() {
    super.onResume();
    if (cameraQuarantined) {
      statusView.showError("CAMERA WORKER QUARANTINED", "EXIT RETROLENS BEFORE USING CAMERA");
      return;
    }
    resumed = true;
    displayProbeReady = false;
    readyCamera = null;
    resetSequenceMetrics();
    setAutoPowerOffMode(false);
    statusView.showStarting();
    cameraController.start();
    Logger.info("RetroLens: sequence acquisition probe resume complete");
  }

  @Override
  protected void onPause() {
    Logger.info("RetroLens: sequence acquisition probe pause begin");
    resumed = false;
    mainHandler.removeCallbacksAndMessages(null);
    readyCamera = null;
    CameraSequenceFrameSource source = frameSource;
    frameSource = null;
    boolean sequenceStopped = stopSequence(source);
    displayProbeController.stop();
    if (sequenceStopped) {
      cameraController.stop();
      Logger.info("RetroLens: sequence probe pause release complete");
    } else {
      cameraQuarantined = true;
      Logger.error(
          "RetroLens: SEVERE frame worker did not stop; CameraEx deliberately quarantined");
    }
    setAutoPowerOffMode(true);
    Logger.flush();
    super.onPause();
  }

  @Override
  public void onCameraReady(CameraEx cameraEx) {
    if (!resumed)
      return;
    readyCamera = cameraEx;
    statusView.showReady();
    displayProbeController.start();
    startSequenceIfReady();
    Logger.info("RetroLens: Sony normal preview ready");
  }

  @Override
  public void onDisplayProbeResult(int status) {
    if (!resumed)
      return;
    if (status == NativeBridge.SURFACE_OK) {
      displayProbeReady = true;
      statusView.showTransient("NATIVE THREAD OK", "STARTING ANALYTICAL ACQUISITION", 1000L);
      startSequenceIfReady();
    } else {
      statusView.showError("NATIVE PROBE FAILED  E" + status, "SONY PREVIEW REMAINS ACTIVE");
    }
  }

  @Override
  public void onSequenceStarted(CameraSequenceFrameSource source) {
    if (!isCurrentSource(source))
      return;
    updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_ACTIVE);
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (resumed)
          statusView.showTransient("SEQUENCE ACTIVE", "WAITING FOR JPEG FRAMES", 900L);
      }
    });
  }

  @Override
  public void onFirstFrame(
      CameraSequenceFrameSource source, final String format, final int length) {
    if (!isCurrentSource(source))
      return;
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (resumed)
          statusView.showTransient("ANALYTICAL " + format, length + " BYTES", 1200L);
      }
    });
  }

  @Override
  public void onFrame(ByteBuffer buffer, int length, long timestampMs) {
    CameraSequenceFrameSource source = frameSource;
    if (!resumed || source == null)
      return;
    receivedFrames++;
    lastJpegBytes = length;
    if (firstFrameTimestampMs == 0L)
      firstFrameTimestampMs = timestampMs;
    lastFrameTimestampMs = timestampMs;
    updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_ACTIVE);
  }

  @Override
  public void onFrameReleased(CameraSequenceFrameSource source, int totalReleasedFrames) {
    if (!isCurrentSource(source))
      return;
    releasedFrames = totalReleasedFrames;
    updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_ACTIVE);
  }

  @Override
  public void onSequenceUnavailable(CameraSequenceFrameSource source, final String reason) {
    if (!isCurrentSource(source))
      return;
    updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_ERROR);
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        if (resumed)
          statusView.showError("SEQUENCE UNAVAILABLE", "NORMAL CAPTURE REMAINS ACTIVE");
        Logger.error("RetroLens: sequence unavailable " + reason);
      }
    });
  }

  @Override
  public void onCameraUnavailable(String reason) {
    if (resumed)
      statusView.showError("CAMERA UNAVAILABLE", "EXIT AND RESTART SAFELY");
    Logger.error("RetroLens: camera unavailable " + reason);
  }

  @Override
  protected boolean onFocusKeyDown() {
    cameraController.focus(true);
    statusView.showTransient("FOCUS", "HALF PRESS", 350L);
    return true;
  }

  @Override
  protected boolean onFocusKeyUp() {
    cameraController.focus(false);
    return true;
  }

  @Override
  protected boolean onShutterKeyDown() {
    if (cameraController.capture()) {
      statusView.showTransient("CAPTURE", "SONY ORIGINAL", 700L);
    } else {
      statusView.showTransient("CAPTURE UNAVAILABLE", "WAIT FOR CAMERA", 900L);
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
    statusView.showTransient("VIDEO DISABLED", "ACQUISITION PROBE - PHOTO ONLY", 1400L);
    Logger.info("RetroLens: movie key ignored in sequence probe");
    return true;
  }

  @Override
  protected boolean onDeleteKeyUp() {
    onBackPressed();
    return true;
  }

  @Override
  protected void setColorDepth(boolean highQuality) {
    super.setColorDepth(false);
  }

  private void startSequenceIfReady() {
    if (!resumed || !displayProbeReady || readyCamera == null || frameSource != null)
      return;
    resetSequenceMetrics();
    CameraSequenceFrameSource source = new CameraSequenceFrameSource(this, this);
    frameSource = source;
    updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_STARTING);
    if (!source.start(readyCamera)) {
      frameSource = null;
      updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_ERROR);
      statusView.showError("SEQUENCE START FAILED", "NORMAL CAPTURE REMAINS ACTIVE");
    }
  }

  private boolean stopSequence(CameraSequenceFrameSource source) {
    if (source == null)
      return true;
    updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_STOPPING);
    if (source.stopAndJoin(2000L))
      return true;
    Logger.error("RetroLens: frame worker initial join timed out; requesting sequence stop");
    source.requestSequenceStop();
    return source.stopAndJoin(1000L);
  }

  private boolean isCurrentSource(CameraSequenceFrameSource source) {
    return resumed && source != null && source == frameSource;
  }

  private void resetSequenceMetrics() {
    receivedFrames = 0;
    releasedFrames = 0;
    lastJpegBytes = 0;
    firstFrameTimestampMs = 0L;
    lastFrameTimestampMs = 0L;
  }

  private void updateSequenceMetrics(int state) {
    displayProbeController.updateSequenceMetrics(state, receivedFrames, releasedFrames,
        lastJpegBytes, firstFrameTimestampMs, lastFrameTimestampMs);
  }
}
