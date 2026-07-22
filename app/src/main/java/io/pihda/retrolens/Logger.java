package io.pihda.retrolens;

import android.os.Environment;
import java.io.*;

public class Logger {
  private static final int MAX_BUFFER_CHARS = 32768;
  private static final StringBuffer pending = new StringBuffer();

  public static File getFile() {
    return new File(Environment.getExternalStorageDirectory(), "RETROLENS/LOG.TXT");
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
    appendToFile("[INFO] RetroLens session start build=" + buildId + "\n");
  }

  public static synchronized void flush() {
    if (pending.length() == 0)
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
    try {
      getFile().getParentFile().mkdirs();
      BufferedWriter writer = new BufferedWriter(new FileWriter(getFile(), true));
      writer.append(text);
      writer.close();
      return true;
    } catch (IOException e) {
      android.util.Log.e("RetroLens", "File logging failed", e);
      return false;
    }
  }
}
