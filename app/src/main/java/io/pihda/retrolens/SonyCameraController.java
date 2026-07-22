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
    void onCapturedJpeg(int byteCount, long timestampMs);
  }

  private final SurfaceHolder holder;
  private final Listener listener;
  private final Handler handler;
  private CameraEx cameraEx;
  private boolean active;
  private boolean surfaceReady;
  private boolean previewReady;
  private int captureGeneration;

  public SonyCameraController(SurfaceView surfaceView, Listener listener) {
    this.listener = listener;
    this.handler = new Handler();
    holder = surfaceView.getHolder();
    holder.setType(SurfaceHolder.SURFACE_TYPE_PUSH_BUFFERS);
    holder.addCallback(this);
  }

  public void start() {
    if (active)
      return;
    active = true;
    try {
      cameraEx = CameraEx.open(0, null);
      if (NativeBridge.PROCESSED_DERIVATIVE_ENABLED) {
        cameraEx.setJpegListener(new CameraEx.JpegListener() {
          @Override
          public void onPictureTaken(byte[] data, CameraEx camera) {
            if (!active)
              return;
            int bytes = data == null ? 0 : data.length;
            Logger.info("Camera: captured JPEG callback bytes=" + bytes);
            listener.onCapturedJpeg(bytes, android.os.SystemClock.elapsedRealtime());
          }
        });
      }
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
    CameraEx camera = cameraEx;
    cameraEx = null;
    previewReady = false;
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
        camera.setJpegListener(null);
      } catch (Throwable t) {
        report("Remove JPEG listener failed", t);
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
      Logger.info("Camera: capture requested generation=" + generation);
      handler.postDelayed(new Runnable() {
        @Override
        public void run() {
          if (!active || generation != captureGeneration || cameraEx != camera)
            return;
          releaseShutter();
        }
      }, 425L);
      return true;
    } catch (Throwable t) {
      report("Capture failed", t);
      return false;
    }
  }

  public void releaseShutter() {
    CameraEx camera = cameraEx;
    if (!active || camera == null)
      return;
    try {
      camera.cancelTakePicture();
      Logger.info("Camera: shutter released");
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
