package io.pihda.retrolens;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.View;

/** Lightweight status overlay that never replaces or obscures the Sony preview. */
public final class StartupStatusView extends View {
  private static final int MODE_STARTING = 0;
  private static final int MODE_READY = 1;
  private static final int MODE_ERROR = 2;
  private static final int MODE_TRANSIENT = 3;
  private static final int MODE_COMPACT = 4;

  private final Paint paint = new Paint();
  private final Runnable restoreReady = new Runnable() {
    @Override
    public void run() {
      showReady();
    }
  };
  private int mode = MODE_STARTING;
  private String title = "RETROLENS";
  private String detail = "STARTING PHOTO ENGINE";

  public StartupStatusView(Context context, AttributeSet attributes) {
    super(context, attributes);
    paint.setAntiAlias(true);
    paint.setTypeface(Typeface.DEFAULT_BOLD);
    setWillNotDraw(false);
    setClickable(false);
    setFocusable(false);
  }

  public void showStarting() {
    removeCallbacks(restoreReady);
    setVisibility(VISIBLE);
    mode = MODE_STARTING;
    title = "RETROLENS";
    detail = "STARTING PHOTO ENGINE";
    invalidate();
  }

  public void showReady() {
    removeCallbacks(restoreReady);
    mode = MODE_READY;
    setVisibility(GONE);
  }

  public void showError(String newTitle, String newDetail) {
    removeCallbacks(restoreReady);
    setVisibility(VISIBLE);
    mode = MODE_ERROR;
    title = newTitle == null ? "CAMERA UNAVAILABLE" : newTitle;
    detail = newDetail == null ? "" : newDetail;
    invalidate();
  }

  public void showTransient(String newTitle, String newDetail, long durationMs) {
    removeCallbacks(restoreReady);
    setVisibility(VISIBLE);
    mode = MODE_TRANSIENT;
    title = newTitle == null ? "RETROLENS" : newTitle;
    detail = newDetail == null ? "" : newDetail;
    invalidate();
    postDelayed(restoreReady, durationMs);
  }

  public void showCompact(String newTitle, String newDetail) {
    removeCallbacks(restoreReady);
    setVisibility(VISIBLE);
    mode = MODE_COMPACT;
    title = newTitle == null ? "EFFECTS STARTING" : newTitle;
    detail = newDetail == null ? "SONY PREVIEW ACTIVE" : newDetail;
    invalidate();
  }

  @Override
  protected void onDraw(Canvas canvas) {
    super.onDraw(canvas);
    if (mode == MODE_READY)
      return;
    int width = getWidth();
    int height = getHeight();

    if (mode == MODE_COMPACT) {
      int panelTop = height - 34;
      paint.setColor(Color.argb(205, 13, 17, 18));
      canvas.drawRect(0, panelTop, width, height, paint);
      paint.setTextSize(13.0f);
      paint.setColor(Color.rgb(66, 232, 188));
      canvas.drawText(title, 18, panelTop + 14, paint);
      paint.setTextSize(10.0f);
      paint.setColor(Color.rgb(239, 232, 211));
      canvas.drawText(detail, 18, panelTop + 28, paint);
      return;
    }

    paint.setColor(Color.argb(190, 13, 17, 18));
    canvas.drawRect(0, 0, width, 42, paint);
    paint.setTextSize(17.0f);
    paint.setColor(Color.rgb(239, 232, 211));
    canvas.drawText("RETROLENS", 20, 27, paint);
    paint.setTextSize(13.0f);
    paint.setColor(Color.rgb(66, 232, 188));
    canvas.drawText("PHOTO ENGINE", width - 150, 26, paint);

    int panelTop = height / 2 - 58;
    int panelBottom = height / 2 + 58;
    paint.setColor(Color.argb(mode == MODE_ERROR ? 235 : 215, 13, 17, 18));
    canvas.drawRect(48, panelTop, width - 48, panelBottom, paint);
    paint.setTextSize(27.0f);
    paint.setColor(mode == MODE_ERROR ? Color.rgb(255, 150, 110) : Color.rgb(239, 232, 211));
    canvas.drawText(title, 72, height / 2 - 7, paint);
    paint.setTextSize(14.0f);
    paint.setColor(Color.rgb(66, 232, 188));
    canvas.drawText(detail, 72, height / 2 + 27, paint);
    paint.setTextSize(11.0f);
    paint.setColor(Color.rgb(160, 164, 156));
    canvas.drawText(NativeBridge.BUILD_ID, 72, height / 2 + 48, paint);
  }
}
