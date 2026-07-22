package io.pihda.retrolens;

import android.os.SystemClock;
import com.sony.scalar.hardware.CameraEx;
import com.sony.scalar.hardware.CameraSequence;
import com.sony.scalar.hardware.DeviceBuffer;
import com.sony.scalar.hardware.DeviceMemory;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.ByteBuffer;

/**
 * Owns the low-rate Sony CameraSequence polling worker.
 *
 * A frame buffer is valid only for the duration of FrameConsumer.onFrame().
 * The consumer must not retain it. CameraSequence and every DeviceMemory are
 * released deterministically by stop/join/release during activity shutdown.
 */
public final class CameraSequenceFrameSource {
  public interface Listener {
    void onSequenceStarted(CameraSequenceFrameSource source);
    void onFirstFrame(CameraSequenceFrameSource source, String format, int length);
    void onFrameReleased(CameraSequenceFrameSource source, int releasedFrames);
    void onSequenceUnavailable(CameraSequenceFrameSource source, String reason);
  }

  public interface FrameConsumer {
    void onFrame(ByteBuffer buffer, int length, long timestampMs);
  }

  public static final int SOURCE_WIDTH = 640;
  public static final int SOURCE_HEIGHT = 480;
  private static final int SOURCE_FRAME_RATE = 30000;
  private static final int SOURCE_FORMAT = 256;
  private static final int MAX_QUEUED_FRAMES = 1;
  private static final int MAX_JPEG_SIZE = 256 * 1024;
  private static final long POLL_INTERVAL_MS = 90;
  private static final long FIRST_FRAME_TIMEOUT_MS = 5000;
  private static final long STATS_INTERVAL_MS = 5000;

  private final Object sequenceLock = new Object();
  private final Listener listener;
  private final FrameConsumer consumer;

  private volatile boolean stopping;
  private Thread workerThread;
  private CameraSequence sequence;
  private boolean sequenceStarted;

  public CameraSequenceFrameSource(Listener listener, FrameConsumer consumer) {
    this.listener = listener;
    this.consumer = consumer;
  }

  public synchronized boolean start(final CameraEx cameraEx) {
    if (workerThread != null) {
      return false;
    }
    stopping = false;
    workerThread = new Thread(new Runnable() {
      @Override
      public void run() {
        runWorker(cameraEx);
      }
    }, "RetroFrameWorker");
    workerThread.start();
    return true;
  }

  public synchronized boolean stopAndJoin(long timeoutMs) {
    stopping = true;
    Thread thread = workerThread;
    if (thread == null) {
      return true;
    }
    thread.interrupt();
    try {
      thread.join(timeoutMs);
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
      logFailure("join frame worker", e);
    }
    if (!thread.isAlive()) {
      workerThread = null;
      return true;
    }
    return false;
  }

  /** Recovery path used only if a bounded join did not unblock polling. */
  public void requestSequenceStop() {
    CameraSequence activeSequence;
    boolean active;
    synchronized (sequenceLock) {
      activeSequence = sequence;
      active = sequenceStarted;
    }
    if (activeSequence != null && active) {
      try {
        activeSequence.stopPreviewSequence();
        synchronized (sequenceLock) {
          if (sequence == activeSequence) {
            sequenceStarted = false;
          }
        }
        Logger.info("RetroFrames: preview sequence stop requested to unblock worker");
      } catch (Throwable throwable) {
        logFailure("stop preview sequence to unblock worker", throwable);
      }
    }
  }

  /** Call only after the worker has stopped. Safe after partial initialization. */
  public void release() {
    CameraSequence sequenceToRelease;
    boolean stopFirst;
    synchronized (sequenceLock) {
      sequenceToRelease = sequence;
      stopFirst = sequenceStarted;
      sequence = null;
      sequenceStarted = false;
    }
    if (sequenceToRelease == null) {
      return;
    }
    if (stopFirst) {
      try {
        sequenceToRelease.stopPreviewSequence();
        Logger.info("RetroFrames: preview sequence stopped");
      } catch (Throwable throwable) {
        logFailure("stop preview sequence", throwable);
      }
    }
    try {
      sequenceToRelease.release();
      Logger.info("RetroFrames: CameraSequence released");
    } catch (Throwable throwable) {
      logFailure("release CameraSequence", throwable);
    }
  }

  private void runWorker(CameraEx cameraEx) {
    ByteBuffer reusableBuffer;
    try {
      reusableBuffer = ByteBuffer.allocateDirect(MAX_JPEG_SIZE);
      Logger.info("RetroFrames: allocated direct frame buffer bytes=" + MAX_JPEG_SIZE);
    } catch (Throwable throwable) {
      unavailable("direct frame buffer allocation failed", throwable);
      return;
    }
    if (stopping) {
      Logger.info("RetroFrames: worker stopped before CameraSequence.open");
      return;
    }

    try {
      CameraSequence openedSequence = CameraSequence.open(cameraEx);
      synchronized (sequenceLock) {
        sequence = openedSequence;
      }
      Logger.info("RetroFrames: CameraSequence.open completed");
      if (stopping) {
        Logger.info("RetroFrames: worker stopped after CameraSequence.open");
        return;
      }

      CameraSequence.Options options = new CameraSequence.Options();
      options.setOption(CameraSequence.Options.PREVIEW_FRAME_WIDTH, SOURCE_WIDTH);
      options.setOption(CameraSequence.Options.PREVIEW_FRAME_HEIGHT, SOURCE_HEIGHT);
      options.setOption(CameraSequence.Options.PREVIEW_FRAME_RATE, SOURCE_FRAME_RATE);
      options.setOption(CameraSequence.Options.PREVIEW_FRAME_FORMAT, SOURCE_FORMAT);
      options.setOption(CameraSequence.Options.PREVIEW_FRAME_MAX_NUM, MAX_QUEUED_FRAMES);
      options.setOption(CameraSequence.Options.JPEG_COMPRESS_MAX_SIZE, MAX_JPEG_SIZE);
      Logger.info("RetroFrames: starting probe width=" + SOURCE_WIDTH + " height=" + SOURCE_HEIGHT
          + " rate=" + SOURCE_FRAME_RATE + " format=" + SOURCE_FORMAT
          + " maxFrames=" + MAX_QUEUED_FRAMES + " maxJpegBytes=" + MAX_JPEG_SIZE);

      if (stopping) {
        Logger.info("RetroFrames: worker stopped before preview sequence start");
        return;
      }
      openedSequence.startPreviewSequence(options);
      synchronized (sequenceLock) {
        if (sequence == openedSequence) {
          sequenceStarted = true;
        }
      }
      Logger.info("RetroFrames: preview sequence started with probe options");
      if (!stopping && listener != null) {
        listener.onSequenceStarted(this);
      }

      if (!stopping) {
        pollFrames(openedSequence, reusableBuffer);
      }
    } catch (Throwable throwable) {
      unavailable("CameraSequence initialization or polling failed", throwable);
    } finally {
      // The worker that opens the private Sony sequence also owns its final
      // release. A future activity must still quarantine CameraEx if this
      // worker cannot be joined, rather than releasing underneath polling.
      release();
    }
    Logger.info("RetroFrames: worker stopped");
  }

  private void pollFrames(CameraSequence activeSequence, ByteBuffer reusableBuffer)
      throws Exception {
    long workerStartMs = SystemClock.elapsedRealtime();
    long statsStartMs = workerStartMs;
    long nextPollMs = workerStartMs;
    long totalBytes = 0;
    int statsFrames = 0;
    int totalFrames = 0;
    boolean firstFrameReported = false;
    int releasedFrames = 0;

    while (!stopping) {
      long nowMs = SystemClock.elapsedRealtime();
      long delayMs = nextPollMs - nowMs;
      if (delayMs > 0) {
        try {
          Thread.sleep(delayMs);
        } catch (InterruptedException e) {
          if (stopping) {
            break;
          }
          Thread.currentThread().interrupt();
          throw e;
        }
      }
      nextPollMs = SystemClock.elapsedRealtime() + POLL_INTERVAL_MS;

      DeviceMemory[] frames = activeSequence.getPreviewSequenceFrames(1);
      if (frames == null || frames.length == 0) {
        if (!firstFrameReported
            && SystemClock.elapsedRealtime() - workerStartMs >= FIRST_FRAME_TIMEOUT_MS) {
          throw new IllegalStateException(
              "no analytical preview frame received within " + FIRST_FRAME_TIMEOUT_MS + " ms");
        }
        continue;
      }

      for (int i = 0; i < frames.length; i++) {
        DeviceMemory memory = frames[i];
        try {
          if (!(memory instanceof DeviceBuffer)) {
            Logger.error("RetroFrames: non-DeviceBuffer frame type="
                + (memory == null ? "null" : memory.getClass().getName()));
            continue;
          }

          DeviceBuffer frame = (DeviceBuffer) memory;
          int frameSize = frame.getSize();
          if (frameSize <= 0 || frameSize > reusableBuffer.capacity()) {
            throw new IllegalStateException(
                "invalid frame size=" + frameSize + " capacity=" + reusableBuffer.capacity());
          }

          reusableBuffer.clear();
          int readResult = frame.read(reusableBuffer, frameSize, 0);
          if (readResult < 0) {
            throw new IllegalStateException("DeviceBuffer.read result=" + readResult);
          }

          int jpegLength = findJpegLength(reusableBuffer, frameSize);
          if (jpegLength <= 0) {
            throw new IllegalStateException(
                "analytical frame is not a bounded JPEG frameSize=" + frameSize);
          }
          boolean jpeg = true;
          int payloadLength = jpegLength;
          long timestampMs = SystemClock.elapsedRealtime();

          totalFrames++;
          statsFrames++;
          totalBytes += payloadLength;
          if (totalFrames <= 5 || totalFrames % 100 == 0) {
            Logger.info("RetroFrames: frame=" + totalFrames + " bufferSize=" + frameSize
                + " readResult=" + readResult + " payloadSize=" + payloadLength
                + " jpegMarkers=" + jpeg);
          }

          if (!firstFrameReported) {
            firstFrameReported = true;
            String format = jpeg ? "JPEG" : "UNKNOWN";
            Logger.info(
                "RetroFrames: first frame format=" + format + " payloadSize=" + payloadLength);
            if (listener != null) {
              listener.onFirstFrame(this, format, payloadLength);
            }
          }

          if (consumer != null) {
            reusableBuffer.position(0);
            reusableBuffer.limit(payloadLength);
            consumer.onFrame(reusableBuffer, payloadLength, timestampMs);
          }
          reusableBuffer.clear();

          long statsNowMs = SystemClock.elapsedRealtime();
          long statsElapsedMs = statsNowMs - statsStartMs;
          if (statsElapsedMs >= STATS_INTERVAL_MS) {
            long averageBytes = statsFrames == 0 ? 0 : totalBytes / statsFrames;
            float fps = statsElapsedMs == 0 ? 0 : (statsFrames * 1000.0f) / statsElapsedMs;
            Logger.info("RetroFrames: analyticalFps=" + fps + " averagePayloadBytes=" + averageBytes
                + " intervalFrames=" + statsFrames);
            statsStartMs = statsNowMs;
            statsFrames = 0;
            totalBytes = 0;
          }
        } finally {
          if (memory != null) {
            try {
              memory.release();
              releasedFrames++;
              if (listener != null) {
                listener.onFrameReleased(this, releasedFrames);
              }
            } catch (Throwable throwable) {
              logFailure("release DeviceMemory", throwable);
            }
          }
        }
      }
    }
  }

  private int findJpegLength(ByteBuffer buffer, int frameSize) {
    if (frameSize < 4 || (buffer.get(0) & 0xff) != 0xff || (buffer.get(1) & 0xff) != 0xd8) {
      return -1;
    }
    for (int i = frameSize - 2; i >= 2; i--) {
      if ((buffer.get(i) & 0xff) == 0xff && (buffer.get(i + 1) & 0xff) == 0xd9) {
        return i + 2;
      }
    }
    return -1;
  }

  private void unavailable(String reason, Throwable throwable) {
    logFailure(reason, throwable);
    if (listener != null) {
      listener.onSequenceUnavailable(
          this, reason + ": " + throwable.getClass().getName() + ": " + throwable.getMessage());
    }
  }

  private static void logFailure(String operation, Throwable throwable) {
    StringWriter writer = new StringWriter();
    PrintWriter printer = new PrintWriter(writer);
    throwable.printStackTrace(printer);
    printer.close();
    Logger.error("RetroFrames: operation=" + operation
        + " exception=" + throwable.getClass().getName() + " message=" + throwable.getMessage()
        + "\n" + writer.toString());
  }
}
