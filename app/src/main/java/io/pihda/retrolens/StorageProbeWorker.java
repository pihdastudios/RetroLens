package io.pihda.retrolens;

import android.os.Handler;

/** Performs bounded removable-storage preparation without blocking camera startup. */
public final class StorageProbeWorker implements Runnable {
  public interface Listener {
    void onStorageProbeComplete(
        StorageProbeWorker worker, StorageController.Result result, int attempts);
  }

  private static final long[] RETRY_DELAYS_MS = {0L, 250L, 750L, 1500L, 3000L};

  private final Handler mainHandler;
  private volatile Listener listener;
  private volatile boolean stopping;
  private Thread thread;

  public StorageProbeWorker(Handler handler, Listener resultListener) {
    mainHandler = handler;
    listener = resultListener;
  }

  public synchronized boolean start() {
    if (thread != null)
      return false;
    stopping = false;
    thread = new Thread(this, "RetroLensStorage");
    thread.start();
    return true;
  }

  public boolean stopAndJoin(long timeoutMs) {
    Thread activeThread;
    stopping = true;
    listener = null;
    synchronized (this) {
      activeThread = thread;
    }
    if (activeThread == null)
      return true;
    activeThread.interrupt();
    try {
      activeThread.join(timeoutMs);
    } catch (InterruptedException exception) {
      Thread.currentThread().interrupt();
    }
    return !activeThread.isAlive();
  }

  @Override
  public void run() {
    StorageController.Result result = null;
    int attempts = 0;
    for (int index = 0; index < RETRY_DELAYS_MS.length && !stopping; index++) {
      long delayMs = RETRY_DELAYS_MS[index];
      if (delayMs > 0) {
        try {
          Thread.sleep(delayMs);
        } catch (InterruptedException exception) {
          if (stopping)
            break;
          Thread.currentThread().interrupt();
          break;
        }
      }
      if (stopping)
        break;
      attempts++;
      result = StorageController.probe(NativeBridge.BUILD_ID);
      Logger.info("Storage: worker attempt=" + attempts + " status=" + result.status + " root="
          + result.root + " freeBytes=" + result.freeBytes + " detail=" + result.detail);
      if (result.isReady() || result.status == StorageController.INSUFFICIENT_SPACE
          || index == RETRY_DELAYS_MS.length - 1)
        break;
    }

    if (stopping || result == null)
      return;
    if (result.isReady()) {
      Logger.configure(result);
      Logger.flush();
    }
    final StorageController.Result completedResult = result;
    final int completedAttempts = attempts;
    mainHandler.post(new Runnable() {
      @Override
      public void run() {
        Listener current = listener;
        if (!stopping && current != null)
          current.onStorageProbeComplete(
              StorageProbeWorker.this, completedResult, completedAttempts);
      }
    });
  }
}
