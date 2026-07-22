package io.pihda.retrolens;

import java.io.*;

public class Logger {
  public static final boolean EXTERNAL_LOGGING_ENABLED = true;
  private static final int MAX_BUFFER_CHARS = 32768;
  private static final StringBuffer pending = new StringBuffer();
  private static File storageRoot;

  public static synchronized void configure(StorageController.Result storage) {
    storageRoot = storage != null && storage.isReady() ? new File(storage.root) : null;
    if (storage == null || !storage.isReady())
      android.util.Log.e("RetroLens",
          "External logging disabled: " + (storage == null ? "no storage result" : storage.detail));
  }

  public static File getFile() {
    return storageRoot == null ? null : new File(storageRoot, "RETROLENS/LOG.TXT");
  }

  public static void installUncaughtExceptionHandler() {
    Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
      @Override
      public void uncaughtException(Thread thread, Throwable throwable) {
        StringWriter writer = new StringWriter();
        writer.append(throwable.toString());
        writer.append("\n");
        throwable.printStackTrace(new PrintWriter(writer));
        error(writer.toString());
        System.exit(0);
      }
    });
  }

  public static synchronized void startSession(String buildId) {
    log("INFO",
        "RetroLens session start build=" + buildId
            + " externalLogging=" + EXTERNAL_LOGGING_ENABLED);
  }

  public static synchronized void flush() {
    if (!EXTERNAL_LOGGING_ENABLED) {
      pending.setLength(0);
      return;
    }
    if (pending.length() == 0)
      return;
    if (getFile() == null)
      return;
    if (appendToFile(pending.toString()))
      pending.setLength(0);
  }

  protected static synchronized void log(String msg) {
    android.util.Log.i("RetroLens", msg);
    pending.append(msg).append('\n');
    int excess = pending.length() - MAX_BUFFER_CHARS;
    if (excess > 0)
      pending.delete(0, excess);
  }
  protected static void log(String type, String msg) {
    log("[" + type + "] " + msg);
  }

  public static void info(String msg) {
    log("INFO", msg);
  }
  public static void error(String msg) {
    log("ERROR", msg);
    flush();
  }

  private static boolean appendToFile(String text) {
    if (!EXTERNAL_LOGGING_ENABLED)
      return true;
    try {
      File file = getFile();
      file.getParentFile().mkdirs();
      FileOutputStream stream = new FileOutputStream(file, true);
      OutputStreamWriter output = new OutputStreamWriter(stream, "UTF-8");
      output.write(text);
      output.flush();
      stream.getFD().sync();
      output.close();
      return true;
    } catch (IOException e) {
      android.util.Log.e("RetroLens", "File logging failed", e);
      return false;
    }
  }
}
