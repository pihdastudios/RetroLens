package io.pihda.retrolens;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.View;

/** Allocation-free fallback card used when the native output surface cannot be shown. */
public final class StartupStatusView extends View {
  private final Paint paint = new Paint();
  private String title = "RETROLENS";
  private String detail = "STARTING ANALOG IMAGE ENGINE";
  private String build = NativeBridge.BUILD_ID;

  public StartupStatusView(Context context, AttributeSet attributes) {
    super(context, attributes);
    paint.setAntiAlias(true);
    paint.setTypeface(Typeface.DEFAULT_BOLD);
    setWillNotDraw(false);
  }

  public void showStatus(String newTitle, String newDetail) {
    title = newTitle == null ? "RETROLENS" : newTitle;
    detail = newDetail == null ? "" : newDetail;
    setVisibility(VISIBLE);
    invalidate();
  }

  public void hideStatus() {
    setVisibility(GONE);
  }

  @Override
  protected void onDraw(Canvas canvas) {
    super.onDraw(canvas);
    int width = getWidth();
    int height = getHeight();
    paint.setColor(Color.argb(220, 17, 22, 25));
    canvas.drawRect(54, height / 2 - 92, width - 54, height / 2 + 92, paint);
    paint.setColor(Color.rgb(240, 232, 210));
    paint.setTextSize(34.0f);
    canvas.drawText(title, 82, height / 2 - 30, paint);
    paint.setColor(Color.rgb(64, 232, 190));
    paint.setTextSize(17.0f);
    canvas.drawText(detail, 82, height / 2 + 15, paint);
    paint.setColor(Color.rgb(185, 185, 175));
    paint.setTextSize(13.0f);
    canvas.drawText(build, 82, height / 2 + 58, paint);
  }
}
