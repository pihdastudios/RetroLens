package io.pihda.retrolens;

import android.os.Environment;
import android.os.StatFs;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/** Verifies the exact removable-storage tree used by Java and native photo output. */
public final class StorageController {
  public static final int INITIALIZING = 0;
  public static final int READY = 1;
  public static final int ROOT_UNAVAILABLE = 2;
  public static final int DIRECTORY_FAILED = 3;
  public static final int WRITE_PROBE_FAILED = 4;
  public static final int INSUFFICIENT_SPACE = 5;
  private static final long MINIMUM_FREE_BYTES = 64L * 1024L * 1024L;

  public static final class Result {
    public final int status;
    public final String root;
    public final long freeBytes;
    public final String detail;

    private Result(int status, String root, long freeBytes, String detail) {
      this.status = status;
      this.root = root;
      this.freeBytes = freeBytes;
      this.detail = detail;
    }

    public boolean isReady() {
      return status == READY;
    }
  }

  private StorageController() {}

  public static Result probe(String buildId) {
    File external = Environment.getExternalStorageDirectory();
    if (external == null)
      return failure(ROOT_UNAVAILABLE, "", -1L, "external root is null");
    File root = external.getAbsoluteFile();
    File base = new File(root, "RETROLENS");
    File config = new File(base, "CONFIG");
    File photos = new File(base, "PHOTOS");
    File thumbnails = new File(base, "THUMBNAILS");
    File[] directories = {base, config, photos, thumbnails};
    for (int index = 0; index < directories.length; index++) {
      if (ensureDirectory(directories[index]))
        continue;
      return failure(DIRECTORY_FAILED, root.getAbsolutePath(), availableBytes(root),
          "could not create " + directories[index].getAbsolutePath());
    }
    File temporary = new File(config, "storage_probe.tmp");
    File completed = new File(config, "storage_probe.ok");
    temporary.delete();
    completed.delete();
    try {
      FileOutputStream output = new FileOutputStream(temporary);
      try {
        output.write(
            ("RetroLens " + buildId + "\n" + root.getAbsolutePath() + "\n").getBytes("UTF-8"));
        output.flush();
      } finally {
        output.close();
      }
      if (!temporary.renameTo(completed) || !completed.isFile() || completed.length() == 0L)
        throw new IOException("atomic rename verification failed");
      FileInputStream input = new FileInputStream(completed);
      try {
        if (input.read() < 0)
          throw new IOException("renamed probe could not be read back");
      } finally {
        input.close();
      }
    } catch (IOException exception) {
      temporary.delete();
      completed.delete();
      return failure(
          WRITE_PROBE_FAILED, root.getAbsolutePath(), availableBytes(root), exception.getMessage());
    }
    temporary.delete();
    completed.delete();
    long freeBytes = availableBytes(root);
    if (freeBytes < MINIMUM_FREE_BYTES)
      return failure(
          INSUFFICIENT_SPACE, root.getAbsolutePath(), freeBytes, "less than 64 MiB available");
    return new Result(READY, root.getAbsolutePath(), freeBytes, "write and rename probe passed");
  }

  private static boolean ensureDirectory(File directory) {
    return directory.isDirectory() || directory.mkdirs();
  }

  private static long availableBytes(File root) {
    try {
      StatFs stats = new StatFs(root.getAbsolutePath());
      return (long) stats.getAvailableBlocks() * (long) stats.getBlockSize();
    } catch (RuntimeException exception) {
      return -1L;
    }
  }

  private static Result failure(int status, String root, long freeBytes, String detail) {
    return new Result(status, root, freeBytes, detail == null ? "unknown failure" : detail);
  }
}
