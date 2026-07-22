package io.pihda.retrolens;

import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import com.sony.scalar.hardware.CameraEx;

/** Camera-safe Sony lifecycle baseline. Native effects remain compiled but are not started. */
public final class RetroLensActivity extends BaseActivity implements SonyCameraController.Listener {
  private SonyCameraController cameraController;
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
    mainHandler = new Handler();
    Logger.startSession(NativeBridge.BUILD_ID);
    Logger.info("RetroLens: safe baseline created model=" + Build.MODEL + " sdk="
        + Build.VERSION.SDK_INT + " abi=" + Build.CPU_ABI + " build=" + NativeBridge.BUILD_ID);
  }

  @Override
  protected void onResume() {
    super.onResume();
    resumed = true;
    setAutoPowerOffMode(false);
    statusView.showStarting();
    cameraController.start();
    Logger.info("RetroLens: safe baseline resume complete");
  }

  @Override
  protected void onPause() {
    Logger.info("RetroLens: safe baseline pause begin");
    resumed = false;
    mainHandler.removeCallbacksAndMessages(null);
    cameraController.stop();
    setAutoPowerOffMode(true);
    Logger.info("RetroLens: safe baseline pause release complete");
    Logger.flush();
    super.onPause();
  }

  @Override
  public void onCameraReady(CameraEx cameraEx) {
    if (!resumed)
      return;
    statusView.showReady();
    Logger.info("RetroLens: Sony normal preview ready");
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
