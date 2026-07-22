package io.pihda.retrolens;

/**
 * Sony movie control remains intentionally disabled until MediaRecorder output
 * semantics and OnRecord confirmation are physically verified on the a5100.
 */
public final class SonyMovieController {
  public boolean isSupported() {
    return false;
  }
  public String getStatus() {
    return "Sony Movie unavailable: effect preview only; saved movie would remain clean";
  }
}
