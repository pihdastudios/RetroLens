package io.pihda.retrolens;

import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import com.sony.scalar.hardware.CameraEx;

/** Camera-safe Sony lifecycle baseline. Native effects remain compiled but are not started. */
public final class RetroLensActivity extends BaseActivity
    implements SonyCameraController.Listener, NativeDisplayProbeController.Listener {
  private SonyCameraController cameraController;
  private NativeDisplayProbeController displayProbeController;
  private StartupStatusView statusView;
  private Handler mainHandler;
  private boolean resumed;

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
    Logger.info("RetroLens: native thread probe created model=" + Build.MODEL + " sdk="
        + Build.VERSION.SDK_INT + " abi=" + Build.CPU_ABI + " build=" + NativeBridge.BUILD_ID);
  }

  @Override
  protected void onResume() {
    super.onResume();
    resumed = true;
    setAutoPowerOffMode(false);
    statusView.showStarting();
    cameraController.start();
    Logger.info("RetroLens: native thread probe resume complete");
  }

  @Override
  protected void onPause() {
    Logger.info("RetroLens: native thread probe pause begin");
    resumed = false;
    mainHandler.removeCallbacksAndMessages(null);
    displayProbeController.stop();
    cameraController.stop();
    setAutoPowerOffMode(true);
    Logger.info("RetroLens: native thread probe pause release complete");
    Logger.flush();
    super.onPause();
  }

  @Override
  public void onCameraReady(CameraEx cameraEx) {
    if (!resumed)
      return;
    statusView.showReady();
    displayProbeController.start();
    Logger.info("RetroLens: Sony normal preview ready");
  }

  @Override
  public void onDisplayProbeResult(int status) {
    if (!resumed)
      return;
    if (status == NativeBridge.SURFACE_OK) {
      statusView.showTransient("NATIVE THREAD OK", "8 FPS  NORMAL PREVIEW ACTIVE", 1200L);
    } else {
      statusView.showError("NATIVE PROBE FAILED  E" + status, "SONY PREVIEW REMAINS ACTIVE");
    }
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
    statusView.showTransient("VIDEO DISABLED", "SAFETY BUILD - PHOTO ONLY", 1400L);
    Logger.info("RetroLens: movie key ignored in safe baseline");
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
}
