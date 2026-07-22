package io.pihda.retrolens;

import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import com.sony.scalar.hardware.CameraEx;
import java.nio.ByteBuffer;

/** Photo-only RetroLens runtime with bounded analytical preview and derivative output. */
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
  private StorageController.Result storageResult;
  private int storageAttempt;
  private static final int MAX_STORAGE_ATTEMPTS = 3;
  private final Runnable storageProbe = new Runnable() {
    @Override
    public void run() {
      if (!resumed || storageResult != null)
        return;
      storageAttempt++;
      StorageController.Result result = StorageController.probe(NativeBridge.BUILD_ID);
      Logger.info("Storage: attempt=" + storageAttempt + " status=" + result.status + " root="
          + result.root + " freeBytes=" + result.freeBytes + " detail=" + result.detail);
      if (result.isReady() || storageAttempt >= MAX_STORAGE_ATTEMPTS) {
        storageResult = result;
        Logger.configure(result);
        Logger.flush();
        displayProbeController.configureStorage(result);
        startRuntimeIfReady();
      } else {
        long delayMs = storageAttempt == 1 ? 250L : 750L;
        mainHandler.postDelayed(this, delayMs);
      }
    }
  };

  @Override
  protected void onCreate(Bundle state) {
    Logger.installUncaughtExceptionHandler();
    super.onCreate(state);
    setContentView(R.layout.activity_retrolens);
    statusView = (StartupStatusView) findViewById(R.id.startupStatus);
    Logger.startSession(NativeBridge.BUILD_ID);
    cameraController = new SonyCameraController(
        (android.view.SurfaceView) findViewById(R.id.sonyPreviewSurface), this);
    displayProbeController = new NativeDisplayProbeController(
        (android.view.SurfaceView) findViewById(R.id.nativeProbeSurface), this);
    mainHandler = new Handler();
    Logger.info("RetroLens: photo runtime created model=" + Build.MODEL + " sdk="
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
    storageResult = null;
    storageAttempt = 0;
    resetSequenceMetrics();
    setAutoPowerOffMode(false);
    statusView.showStarting();
    cameraController.start();
    mainHandler.post(storageProbe);
    Logger.info("RetroLens: photo runtime resume complete");
  }

  @Override
  protected void onPause() {
    Logger.info("RetroLens: photo runtime pause begin");
    resumed = false;
    mainHandler.removeCallbacksAndMessages(null);
    readyCamera = null;
    CameraSequenceFrameSource source = frameSource;
    frameSource = null;
    boolean sequenceStopped = stopSequence(source);
    displayProbeController.stop();
    if (sequenceStopped) {
      cameraController.stop();
      Logger.info("RetroLens: photo runtime pause release complete");
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
    startRuntimeIfReady();
    Logger.info("RetroLens: Sony normal preview ready");
  }

  @Override
  public void onDisplayProbeResult(int status) {
    if (!resumed)
      return;
    if (status == NativeBridge.SURFACE_OK) {
      displayProbeReady = true;
      startSequenceIfReady();
    } else {
      statusView.showError("NATIVE PROBE FAILED  E" + status, "SONY PREVIEW REMAINS ACTIVE");
    }
  }

  @Override
  public void onProcessedPreviewReady() {
    if (resumed)
      statusView.showReady();
  }

  @Override
  public void onSequenceStarted(CameraSequenceFrameSource source) {
    if (!isCurrentSource(source))
      return;
    updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_ACTIVE);
    Logger.info("RetroLens: analytical sequence active");
  }

  @Override
  public void onFirstFrame(
      CameraSequenceFrameSource source, final String format, final int length) {
    if (!isCurrentSource(source))
      return;
    Logger.info("RetroLens: first analytical frame format=" + format + " bytes=" + length);
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
    displayProbeController.submitJpeg(buffer, length, timestampMs);
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
        if (resumed) {
          displayProbeController.stop();
          displayProbeReady = false;
          statusView.showError("SEQUENCE UNAVAILABLE", "NORMAL CAPTURE REMAINS ACTIVE");
        }
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
    displayProbeController.setFocus(true);
    return true;
  }

  @Override
  protected boolean onFocusKeyUp() {
    cameraController.focus(false);
    displayProbeController.setFocus(false);
    return true;
  }

  @Override
  protected boolean onShutterKeyDown() {
    displayProbeController.key(NativeBridge.KEY_CAPTURE, true);
    if (cameraController.capture()) {
      displayProbeController.requestPhoto();
    } else {
      Logger.error("RetroLens: Sony capture unavailable");
    }
    return true;
  }

  @Override
  protected boolean onShutterKeyUp() {
    cameraController.releaseShutter();
    displayProbeController.key(NativeBridge.KEY_CAPTURE, false);
    return true;
  }

  @Override
  protected boolean onMovieKeyDown() {
    displayProbeController.key(NativeBridge.KEY_RECORD, true);
    Logger.info("RetroLens: movie key ignored in photo-only runtime");
    return true;
  }

  @Override
  protected boolean onMovieKeyUp() {
    return true;
  }

  @Override
  protected boolean onLeftKeyDown() {
    displayProbeController.key(NativeBridge.KEY_PREVIOUS, true);
    return true;
  }

  @Override
  protected boolean onLeftKeyUp() {
    return true;
  }

  @Override
  protected boolean onRightKeyDown() {
    displayProbeController.key(NativeBridge.KEY_NEXT, true);
    return true;
  }

  @Override
  protected boolean onRightKeyUp() {
    return true;
  }

  @Override
  protected boolean onUpperDialTurned(boolean clockwise, int value) {
    displayProbeController.key(clockwise ? NativeBridge.KEY_NEXT : NativeBridge.KEY_PREVIOUS, true);
    return true;
  }

  @Override
  protected boolean onLowerDialTurned(boolean clockwise, int value) {
    displayProbeController.key(clockwise ? NativeBridge.KEY_NEXT : NativeBridge.KEY_PREVIOUS, true);
    return true;
  }

  @Override
  protected boolean onUpKeyDown() {
    displayProbeController.key(NativeBridge.KEY_UP, true);
    return true;
  }

  @Override
  protected boolean onUpKeyUp() {
    return true;
  }

  @Override
  protected boolean onDownKeyDown() {
    displayProbeController.key(NativeBridge.KEY_DOWN, true);
    return true;
  }

  @Override
  protected boolean onDownKeyUp() {
    return true;
  }

  @Override
  protected boolean onEnterKeyDown() {
    displayProbeController.key(NativeBridge.KEY_CONFIRM, true);
    return true;
  }

  @Override
  protected boolean onEnterKeyUp() {
    return true;
  }

  @Override
  protected boolean onMenuKeyDown() {
    displayProbeController.key(NativeBridge.KEY_BROWSER, true);
    return true;
  }

  @Override
  protected boolean onMenuKeyUp() {
    return true;
  }

  @Override
  protected boolean onPlayKeyDown() {
    displayProbeController.key(NativeBridge.KEY_GALLERY, true);
    return true;
  }

  @Override
  protected boolean onPlayKeyUp() {
    return true;
  }

  @Override
  protected boolean onC1KeyDown() {
    displayProbeController.key(NativeBridge.KEY_COMPARE, true);
    return true;
  }

  @Override
  protected boolean onC1KeyUp() {
    displayProbeController.key(NativeBridge.KEY_COMPARE, false);
    return true;
  }

  @Override
  protected boolean onFnKeyDown() {
    displayProbeController.key(NativeBridge.KEY_DIAGNOSTICS, true);
    return true;
  }

  @Override
  protected boolean onFnKeyUp() {
    return true;
  }

  @Override
  protected boolean onDeleteKeyDown() {
    if (!displayProbeController.key(NativeBridge.KEY_BACK, true))
      onBackPressed();
    return true;
  }

  @Override
  protected boolean onDeleteKeyUp() {
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

  private void startRuntimeIfReady() {
    if (!resumed || readyCamera == null || storageResult == null || displayProbeReady)
      return;
    displayProbeController.start();
  }

  private boolean stopSequence(CameraSequenceFrameSource source) {
    if (source == null)
      return true;
    updateSequenceMetrics(NativeDisplayProbeController.SEQUENCE_STOPPING);
    source.requestSequenceStop();
    if (source.stopAndJoin(2000L))
      return true;
    Logger.error("RetroLens: frame worker initial join timed out after sequence stop request");
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
