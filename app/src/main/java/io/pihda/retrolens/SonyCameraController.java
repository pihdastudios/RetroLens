package io.pihda.retrolens;

import android.hardware.Camera;
import android.os.Handler;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import com.sony.scalar.hardware.CameraEx;
import java.io.IOException;

/** UI-thread owner for CameraEx and the normal Sony preview. */
public final class SonyCameraController implements SurfaceHolder.Callback {
  public interface Listener {
    void onCameraReady(CameraEx cameraEx);
    void onCameraUnavailable(String reason);
  }

  private final SurfaceHolder holder;
  private final Listener listener;
  private final Handler handler;
  private CameraEx cameraEx;
  private boolean active;
  private boolean surfaceReady;
  private boolean previewReady;
  private boolean captureInProgress;
  private int captureGeneration;

  public SonyCameraController(SurfaceView surfaceView, Listener listener) {
    this.listener = listener;
    this.handler = new Handler();
    holder = surfaceView.getHolder();
    holder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
  }

  public void start() {
    if (active)
      return;
    active = true;
    captureInProgress = false;
    holder.addCallback(this);
    try {
      cameraEx = CameraEx.open(0, null);
      Logger.info("Camera: CameraEx.open completed");
      startPreviewIfReady();
    } catch (Throwable throwable) {
      cameraEx = null;
      report("Camera open failed", throwable);
    }
  }

  public void stop() {
    if (!active && cameraEx == null)
      return;
    active = false;
    captureGeneration++;
    handler.removeCallbacksAndMessages(null);
    holder.removeCallback(this);
    CameraEx camera = cameraEx;
    cameraEx = null;
    surfaceReady = false;
    previewReady = false;
    captureInProgress = false;
    if (camera != null) {
      try {
        camera.getNormalCamera().cancelAutoFocus();
      } catch (Throwable t) {
        report("Cancel autofocus during release failed", t);
      }
      try {
        camera.cancelTakePicture();
      } catch (Throwable t) {
        report("Cancel shutter during release failed", t);
      }
      try {
        camera.release();
        Logger.info("Camera: CameraEx released");
      } catch (Throwable t) {
        report("Camera release failed", t);
      }
    }
  }

  public CameraEx getCameraEx() {
    return active && previewReady ? cameraEx : null;
  }

  public void focus(boolean start) {
    CameraEx camera = getCameraEx();
    if (camera == null)
      return;
    try {
      if (start)
        camera.getNormalCamera().autoFocus(null);
      else
        camera.getNormalCamera().cancelAutoFocus();
      Logger.info("Camera: autofocus " + (start ? "requested" : "cancelled"));
    } catch (Throwable t) {
      report("Autofocus failed", t);
    }
  }

  public boolean capture() {
    final CameraEx camera = getCameraEx();
    if (camera == null)
      return false;
    final int generation = ++captureGeneration;
    try {
      camera.getNormalCamera().takePicture(null, null, null);
      captureInProgress = true;
      previewReady = false;
      Logger.info("Camera: capture requested generation=" + generation);
      handler.postDelayed(new Runnable() {
        @Override
        public void run() {
          if (!active || !captureInProgress || generation != captureGeneration
              || cameraEx != camera)
            return;
          releaseShutterForGeneration(generation);
        }
      }, 425L);
      return true;
    } catch (Throwable t) {
      report("Capture failed", t);
      return false;
    }
  }

  public void releaseShutter() {
    releaseShutterForGeneration(captureGeneration);
  }

  private void releaseShutterForGeneration(int generation) {
    CameraEx camera = cameraEx;
    if (!active || camera == null || !captureInProgress || generation != captureGeneration)
      return;
    captureInProgress = false;
    captureGeneration++;
    try {
      camera.cancelTakePicture();
      Logger.info("Camera: shutter released");
      startPreviewIfReady();
    } catch (Throwable t) {
      report("Shutter release failed", t);
    }
  }

  private void startPreviewIfReady() {
    if (!active || !surfaceReady || previewReady || cameraEx == null)
      return;
    try {
      Camera camera = cameraEx.getNormalCamera();
      camera.setPreviewDisplay(holder);
      camera.startPreview();
      previewReady = true;
      Logger.info("Camera: normal preview started");
      listener.onCameraReady(cameraEx);
    } catch (IOException e) {
      report("Preview display failed", e);
    } catch (Throwable t) {
      report("Preview start failed", t);
    }
  }

  @Override
  public void surfaceCreated(SurfaceHolder surfaceHolder) {
    surfaceReady = true;
    startPreviewIfReady();
  }
  @Override
  public void surfaceChanged(SurfaceHolder h, int f, int w, int he) {
    Logger.info("Camera: normal surface " + w + "x" + he);
  }
  @Override
  public void surfaceDestroyed(SurfaceHolder surfaceHolder) {
    surfaceReady = false;
    previewReady = false;
    Logger.info("Camera: normal surface destroyed");
  }

  private void report(String operation, Throwable throwable) {
    String message = operation + ": " + throwable.toString();
    Logger.error("Camera: " + message);
    listener.onCameraUnavailable(message);
  }
}
