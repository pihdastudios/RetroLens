#include "retrolens_core.h"

#include <math.h>
#include <string.h>

namespace retrolens {

#define C (CTRL_INTENSITY | CTRL_CONTRAST | CTRL_GRAIN)
#define CP (C | CTRL_PIXEL | CTRL_PALETTE)
#define CM (C | CTRL_MOTION | CTRL_CADENCE)
#define CV (C | CTRL_VIGNETTE)
#define P(ID, NAME, CAT, DESC, TIER, FLAGS, CTRLS, R, G, B, SAT, CON, POST,    \
          NOISE, PIX, TEMP)                                                    \
  {ID, NAME, CAT, DESC, TIER, FLAGS, CTRLS, R,                                 \
   G,  B,    SAT, CON,  POST, NOISE, PIX,   TEMP}

static const Preset kPresets[] = {
    P("olive_pocket", "Olive Pocket", "Handheld",
      "Four-tone olive LCD with ordered dither and ghosting", 1,
      FX_MONO | FX_DITHER | FX_PIXELATE | FX_TEMPORAL, CP, 72, 95, 48, -100, 15,
      4, 3, 3, 18),
    P("silver_pocket", "Silver Pocket", "Handheld",
      "Cool reflective four-shade pocket display", 1,
      FX_MONO | FX_DITHER | FX_TEMPORAL, CP, 90, 98, 110, -100, 5, 4, 2, 2, 13),
    P("virtual_red", "Virtual Red", "Handheld",
      "Deep red high-contrast scan grid with restrained bloom", 1,
      FX_MONO | FX_SCANLINES | FX_BLOOM, CV, 130, 20, 22, -100, 38, 3, 3, 2, 0),
    P("dot_matrix_toy", "Dot Matrix Toy", "Handheld",
      "Slow low-resolution dot-matrix toy camera", 2,
      FX_MONO | FX_PIXELATE | FX_MASK | FX_TEMPORAL, CP | CTRL_MOTION, 70, 86,
      58, -100, 20, 3, 2, 5, 28),
    P("calculator_vision", "Calculator Vision", "Handheld",
      "Hard dark segments on pale green-gray LCD", 1,
      FX_MONO | FX_PIXELATE | FX_DITHER, CP, 58, 70, 50, -100, 42, 2, 1, 5, 0),
    P("pocket_color", "Pocket Color", "Handheld",
      "Pastel limited-color handheld LCD", 1, FX_DITHER | FX_PIXELATE | FX_MASK,
      CP, 100, 102, 92, -12, 10, 5, 2, 3, 0),
    P("one_bit_desktop", "One-Bit Desktop", "Computer",
      "Pure one-bit desktop with square Bayer pixels", 1,
      FX_MONO | FX_DITHER | FX_PIXELATE, CP, 100, 100, 100, -100, 55, 2, 0, 2,
      0),
    P("cga_shock", "CGA Shock", "Computer",
      "Cyan-magenta-black-white computer palette", 1,
      FX_CGA | FX_PIXELATE | FX_DITHER, CP, 100, 100, 100, 25, 30, 4, 0, 4, 0),
    P("ega_sixteen", "EGA Sixteen", "Computer",
      "Hard sixteen-color early computer quantization", 1,
      FX_DITHER | FX_PIXELATE, CP, 105, 100, 105, 20, 22, 4, 0, 3, 0),
    P("vga_sixteen", "VGA Sixteen", "Computer",
      "Saturated VGA-inspired sixteen-color dither", 1, FX_DITHER | FX_PIXELATE,
      CP, 110, 105, 115, 42, 20, 4, 1, 2, 0),
    P("amber_terminal", "Amber Terminal", "Computer",
      "Amber phosphor terminal with character texture", 1,
      FX_MONO | FX_SCANLINES | FX_BLOOM | FX_ASCII, CP, 130, 76, 12, -100, 28,
      5, 2, 4, 0),
    P("green_terminal", "Green Terminal", "Computer",
      "Green phosphor persistence and rolling refresh", 2,
      FX_MONO | FX_SCANLINES | FX_TEMPORAL | FX_BLOOM, CM, 25, 125, 48, -100,
      25, 6, 2, 2, 20),
    P("teletext", "Teletext", "Computer",
      "Primary-color block graphics and character cells", 2,
      FX_CGA | FX_PIXELATE | FX_ASCII, CP, 105, 105, 105, 35, 28, 4, 0, 6, 0),
    P("ansi_camera", "ANSI Camera", "Computer",
      "ANSI-like colored text-cell shading", 2,
      FX_ASCII | FX_PIXELATE | FX_DITHER, CP, 108, 104, 100, 25, 25, 6, 1, 5,
      0),
    P("ascii_mono", "ASCII Mono", "Computer",
      "Dense monochrome luminance characters", 3,
      FX_ASCII | FX_MONO | FX_PIXELATE, CP, 105, 96, 76, -100, 34, 6, 0, 5, 0),
    P("ascii_color", "ASCII Color", "Computer",
      "Colored character cells with adjustable density", 3,
      FX_ASCII | FX_PIXELATE, CP, 105, 105, 105, 15, 22, 6, 0, 5, 0),
    P("braille_mosaic", "Braille Mosaic", "Computer",
      "Duotone dot-cell luminance mosaic", 2,
      FX_MONO | FX_HALFTONE | FX_PIXELATE, CP, 92, 104, 82, -100, 30, 3, 0, 4,
      0),
    P("eight_bit_summer", "Eight-Bit Summer", "Console",
      "Bright chunky palette and checkerboard dither", 1,
      FX_DITHER | FX_PIXELATE, CP, 115, 108, 92, 38, 18, 5, 1, 4, 0),
    P("sixteen_bit_arcade", "Sixteen-Bit Arcade", "Console",
      "Wide saturated palette with pixel sharpening", 1, FX_PIXELATE | FX_EDGE,
      CP, 110, 105, 110, 42, 25, 6, 0, 2, 0),
    P("arcade_cabinet", "Arcade Cabinet", "Console",
      "Curved cabinet CRT, mask, and scanlines", 2,
      FX_PIXELATE | FX_SCANLINES | FX_MASK | FX_VIGNETTE, CV | CTRL_PIXEL, 105,
      100, 104, 18, 24, 7, 1, 3, 0),
    P("composite_console", "Composite Console", "Console",
      "Soft composite bleed, fringe, and dot crawl", 2,
      FX_TEAR | FX_SCANLINES | FX_TEMPORAL, CM, 108, 96, 105, 15, 8, 8, 3, 1,
      12),
    P("rf_channel_three", "RF Channel Three", "Console",
      "Unstable RF ghosts, snow, and sync errors", 2,
      FX_TEAR | FX_JITTER | FX_TEMPORAL | FX_SCANLINES, CM, 98, 98, 98, -10, 20,
      7, 18, 1, 18),
    P("consumer_crt", "Consumer CRT", "Analog TV",
      "Soft bloom, scanlines, curvature, and convergence", 2,
      FX_SCANLINES | FX_MASK | FX_VIGNETTE | FX_BLOOM, CV, 105, 100, 103, 12,
      16, 8, 2, 1, 0),
    P("aperture_grille", "Aperture Grille", "Analog TV",
      "Crisp vertical phosphor grille with mild bloom", 2,
      FX_SCANLINES | FX_MASK | FX_BLOOM, CV, 106, 102, 105, 14, 20, 8, 1, 1, 0),
    P("shadow_mask", "Shadow Mask", "Analog TV",
      "Soft phosphor triads and darkened corners", 2,
      FX_MASK | FX_SCANLINES | FX_VIGNETTE, CV, 101, 98, 100, 5, 10, 8, 1, 1,
      0),
    P("late_night_broadcast", "Late-Night Broadcast", "Analog TV",
      "Crushed late-night broadcast color and noise", 1,
      FX_SCANLINES | FX_VIGNETTE, CV, 110, 98, 106, 35, 32, 8, 7, 1, 0),
    P("ntsc_composite", "NTSC Composite", "Analog TV",
      "Phase errors, horizontal bleed, and dot crawl", 2,
      FX_TEAR | FX_SCANLINES | FX_TEMPORAL, CM, 108, 96, 104, 18, 12, 8, 4, 1,
      10),
    P("pal_ghost", "PAL Ghost", "Analog TV",
      "Alternate-line color and restrained frame ghosting", 2,
      FX_TEMPORAL | FX_SCANLINES | FX_TEAR, CM, 98, 104, 104, 4, 8, 8, 2, 1,
      22),
    P("secam_state_tv", "SECAM State TV", "Analog TV",
      "Smeared state broadcast color in a 4:3 frame", 2,
      FX_TEAR | FX_SCANLINES | FX_VIGNETTE, CV, 102, 98, 92, -25, 18, 7, 6, 1,
      0),
    P("vhs_rental", "VHS Rental", "Tape",
      "Rental tape jitter, bleed, tracking, and head noise", 2,
      FX_TEAR | FX_JITTER | FX_TEMPORAL | FX_SCANLINES, CM, 108, 98, 90, -8, 12,
      8, 10, 1, 18),
    P("damaged_vhs", "Damaged VHS", "Tape",
      "Heavy dropouts, tears, unstable tracking, and fading", 2,
      FX_TEAR | FX_JITTER | FX_TEMPORAL | FX_SCANLINES, CM, 102, 92, 90, -28,
      25, 7, 22, 1, 25),
    P("video8_family", "Video8 Family", "Tape",
      "Warm soft consumer video with exposure breathing", 2,
      FX_TEMPORAL | FX_BLOOM | FX_VIGNETTE, CM, 112, 103, 90, 8, 5, 8, 7, 1,
      13),
    P("hi8_vacation", "Hi8 Vacation", "Tape",
      "Bright sharp analog vacation tape", 2,
      FX_JITTER | FX_SCANLINES | FX_EDGE, CM, 112, 108, 96, 20, 18, 8, 5, 1, 0),
    P("minidv_2002", "MiniDV 2002", "Tape",
      "Cool clipped digital camcorder sharpness", 1, FX_EDGE | FX_MASK, CP, 96,
      104, 114, 5, 30, 8, 3, 2, 0),
    P("early_webcam", "Early Webcam", "Tape",
      "Low-cadence block compression and smeared motion", 2,
      FX_PIXELATE | FX_TEMPORAL | FX_MASK, CM | CTRL_PIXEL, 105, 96, 110, 5, 25,
      5, 7, 6, 30),
    P("cctv_1996", "CCTV 1996", "Tape",
      "Monochrome interlaced security monitor", 2,
      FX_MONO | FX_SCANLINES | FX_TEMPORAL, CM, 100, 103, 98, -100, 28, 6, 8, 1,
      10),
    P("night_security", "Night Security", "Tape",
      "Simulated green night surveillance amplification", 2,
      FX_MONO | FX_SCANLINES | FX_VIGNETTE | FX_BLOOM, CV, 28, 124, 42, -100,
      22, 6, 20, 1, 0),
    P("super_eight", "Super Eight", "Film",
      "Warm grain, gate weave, dust, and flicker", 2,
      FX_JITTER | FX_VIGNETTE | FX_BLOOM | FX_TEMPORAL, CM | CTRL_VIGNETTE, 114,
      103, 84, 8, 12, 8, 12, 1, 9),
    P("sixteen_millimeter_news", "Sixteen-Millimeter News", "Film",
      "High-contrast grain, scratches, and frame jitter", 2,
      FX_MONO | FX_JITTER | FX_EDGE | FX_VIGNETTE, CM | CTRL_VIGNETTE, 104, 100,
      86, -80, 34, 7, 16, 1, 0),
    P("soviet_archive_1978", "Soviet Archive 1978", "Archive",
      "Cinematic cadence, weave, dust, scratches, and splice flash", 2,
      FX_MONO | FX_JITTER | FX_TEMPORAL | FX_VIGNETTE | FX_TEAR,
      CM | CTRL_VIGNETTE, 94, 105, 78, -75, 24, 6, 18, 1, 22),
    P("state_archive_color", "State Archive Color", "Archive",
      "Faded uneven archival color with film jitter", 2,
      FX_JITTER | FX_TEMPORAL | FX_VIGNETTE, CM | CTRL_VIGNETTE, 108, 101, 88,
      -35, 0, 7, 9, 1, 12),
    P("cosmonaut_tape", "Cosmonaut Tape", "Archive",
      "Blue-gray telemetry tape with periodic signal loss", 2,
      FX_MONO | FX_TEAR | FX_SCANLINES | FX_MASK, CM, 72, 92, 110, -70, 22, 6,
      14, 1, 0),
    P("expired_slide", "Expired Slide", "Film",
      "Shifted dye, lifted blacks, fading, and vignette", 1, FX_VIGNETTE, CV,
      116, 94, 112, -12, -5, 8, 5, 1, 0),
    P("bleach_bypass", "Bleach Bypass", "Film",
      "Metallic low-saturation contrast and grain", 1, FX_EDGE | FX_VIGNETTE,
      CV, 105, 102, 96, -65, 42, 8, 9, 1, 0),
    P("cross_process", "Cross Process", "Film",
      "Cyan shadows, yellow highlights, and hard curves", 1, FX_VIGNETTE, CV,
      104, 112, 88, 24, 38, 8, 4, 1, 0),
    P("orthochromatic", "Orthochromatic", "Film",
      "Old monochrome response with dark reds", 1,
      FX_MONO | FX_DITHER | FX_VIGNETTE, CV, 72, 110, 96, -100, 26, 7, 8, 1, 0),
    P("cyanotype", "Cyanotype", "Print",
      "Deep blue paper print with pale cyan highlights", 1,
      FX_MONO | FX_INVERT | FX_VIGNETTE, CV, 20, 80, 122, -100, 24, 6, 5, 1, 0),
    P("newsprint", "Newsprint", "Print", "Monochrome halftone dots on paper", 2,
      FX_MONO | FX_HALFTONE | FX_DITHER, CP, 96, 94, 88, -100, 30, 3, 3, 3, 0),
    P("cmyk_misprint", "CMYK Misprint", "Print",
      "Offset halftone channels on tinted paper", 2,
      FX_HALFTONE | FX_TEAR | FX_DITHER, CP, 106, 98, 92, 18, 18, 5, 4, 2, 0),
    P("risograph", "Risograph", "Print",
      "Limited grainy inks with imperfect registration", 2,
      FX_CGA | FX_HALFTONE | FX_TEAR, CP, 108, 92, 96, 8, 25, 4, 8, 2, 0),
    P("manga_screen", "Manga Screen", "Print",
      "Black contours and screentone paper shading", 2,
      FX_MONO | FX_EDGE | FX_HALFTONE | FX_INVERT, CP, 100, 100, 100, -100, 44,
      3, 2, 2, 0),
    P("photocopier", "Photocopier", "Print",
      "Hard dirty toner threshold and glass streaks", 1,
      FX_MONO | FX_EDGE | FX_DITHER, CP, 100, 100, 98, -100, 62, 2, 12, 1, 0),
    P("thermal_receipt", "Thermal Receipt", "Print",
      "Faded thermal marks and transport bands", 1,
      FX_MONO | FX_DITHER | FX_TEAR, CP, 96, 92, 82, -100, 38, 2, 7, 2, 0),
    P("pencil_draft", "Pencil Draft", "Print",
      "Edge-aware grayscale pencil shading", 3,
      FX_MONO | FX_EDGE | FX_INVERT | FX_DITHER, CP, 100, 100, 100, -100, 18, 6,
      4, 1, 0),
    P("comic_ink", "Comic Ink", "Print",
      "Posterized regions with strong dark contours", 3, FX_EDGE | FX_DITHER,
      CP, 108, 103, 98, 18, 34, 4, 2, 2, 0),
    P("jpeg_hell", "JPEG Hell", "Digital Decay",
      "Repeated-generation blocks, ringing, and chroma loss", 3,
      FX_PIXELATE | FX_MASK | FX_TEAR, CP, 108, 94, 106, -12, 36, 4, 10, 8, 0),
    P("databent_preview", "Databent Preview", "Digital Decay",
      "Deterministic displaced blocks and frozen planes", 3,
      FX_TEAR | FX_JITTER | FX_MASK | FX_TEMPORAL, CM, 112, 92, 108, 22, 25, 6,
      8, 4, 35),
    P("frame_echo", "Frame Echo", "Digital Decay",
      "Tinted temporal trails with controlled decay", 2, FX_TEMPORAL | FX_TEAR,
      CM, 110, 98, 108, 12, 10, 8, 2, 1, 48),
    P("signal_tear", "Signal Tear", "Digital Decay",
      "Intermittent row splits and chromatic offset", 2, FX_TEAR | FX_JITTER,
      CM, 110, 96, 108, 18, 24, 7, 8, 1, 0),
    P("pixel_sort", "Pixel Sort", "Digital Decay",
      "Thresholded row sorting in capture-quality mode", 3,
      FX_TEAR | FX_PIXELATE | FX_EDGE, CP, 105, 102, 108, 20, 30, 6, 2, 3, 0),
    P("piss_filter_2007", "Piss Filter 2007", "Game Era",
      "Seventh-Gen Amber: dusty yellow-green blockbuster grade", 1,
      FX_VIGNETTE | FX_BLOOM | FX_EDGE, CV, 112, 102, 55, -28, 34, 8, 6, 1, 0),
    P("brown_apocalypse", "Brown Apocalypse", "Game Era",
      "Harsh brown-gray contrast, bloom, and dirt", 1, FX_VIGNETTE | FX_BLOOM,
      CV, 108, 82, 58, -48, 42, 7, 9, 1, 0),
    P("teal_orange_blockbuster", "Teal-Orange Blockbuster", "Game Era",
      "Teal shadows and orange highlights with widescreen punch", 1,
      FX_VIGNETTE, CV, 112, 101, 96, 28, 38, 8, 3, 1, 0),
    P("green_code", "Green Code", "Game Era",
      "Green-biased shadows and restrained digital noise", 1, FX_VIGNETTE, CV,
      78, 112, 82, -20, 36, 7, 5, 1, 0),
    P("blue_scifi_2004", "Blue Sci-Fi 2004", "Game Era",
      "Metallic blue shadows, cyan highlights, and bloom", 1,
      FX_BLOOM | FX_VIGNETTE, CV, 86, 104, 120, -8, 32, 8, 4, 1, 0),
    P("bloom_shooter", "Bloom Shooter", "Game Era",
      "Overexposed desaturated bloom and sharpening", 2,
      FX_BLOOM | FX_EDGE | FX_VIGNETTE, CV, 112, 108, 102, -35, 18, 8, 4, 1, 0),
    P("thermal_false_color", "Thermal False Color", "Experimental",
      "Simulated luminance thermal palette; not thermal sensing", 1, FX_THERMAL,
      CP, 100, 100, 100, 0, 24, 8, 2, 1, 0),
    P("infrared_false_color", "Infrared False Color", "Experimental",
      "Simulated false infrared through channel remapping", 1,
      FX_INVERT | FX_VIGNETTE, CV, 72, 120, 105, 35, 25, 8, 3, 1, 0),
    P("edge_scanner", "Edge Scanner", "Experimental",
      "Animated targeting sweep over an edge map", 2,
      FX_EDGE | FX_MONO | FX_SCANLINES | FX_INVERT, CM, 30, 116, 104, -100, 45,
      5, 3, 1, 0),
    P("blueprint", "Blueprint", "Experimental",
      "White technical edges on a measured blue grid", 2,
      FX_EDGE | FX_MONO | FX_INVERT | FX_MASK, CP, 25, 78, 120, -100, 36, 5, 1,
      1, 0)};

#undef P
#undef C
#undef CP
#undef CM
#undef CV

int presetCount() { return (int)(sizeof(kPresets) / sizeof(kPresets[0])); }

const Preset &presetAt(int index) {
  if (index < 0)
    index = 0;
  if (index >= presetCount())
    index = presetCount() - 1;
  return kPresets[index];
}

int findPreset(const char *id) {
  if (!id)
    return 0;
  for (int i = 0; i < presetCount(); ++i)
    if (!strcmp(id, kPresets[i].id))
      return i;
  return 0;
}

uint32_t nextRandom(uint32_t *state) {
  uint32_t x = *state ? *state : 0x6d2b79f5U;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

static int clamp8(int value) {
  return value < 0 ? 0 : (value > 255 ? 255 : value);
}
static int luma(const Pixel &p) {
  return (p.r * 77 + p.g * 150 + p.b * 29) >> 8;
}

void processFrame(const Pixel *source, Pixel *output, Pixel *scratch,
                  const Pixel *previous, int width, int height,
                  const Preset &preset, int intensity, uint32_t seed,
                  int64_t timestampMs) {
  if (!source || !output || !scratch || width <= 0 || height <= 0)
    return;
  if (intensity < 0)
    intensity = 0;
  if (intensity > 100)
    intensity = 100;
  const int amount = intensity;
  uint32_t randomState = seed ^ (uint32_t)(timestampMs / 67) ^ 0x9e3779b9U;
  int jitterX = 0, jitterY = 0;
  if (preset.flags & FX_JITTER) {
    jitterX = (int)(nextRandom(&randomState) % 7) - 3;
    jitterY = (int)(nextRandom(&randomState) % 5) - 2;
  }

  const int pixel = (preset.flags & FX_PIXELATE)
                        ? (preset.pixelSize < 2 ? 2 : preset.pixelSize)
                        : 1;
  for (int y = 0; y < height; ++y) {
    int sy = y + jitterY;
    if (sy < 0)
      sy = 0;
    if (sy >= height)
      sy = height - 1;
    if (pixel > 1)
      sy -= sy % pixel;
    int tear = 0;
    if ((preset.flags & FX_TEAR) && ((y + (int)(timestampMs / 31)) % 47 < 3))
      tear = (int)(nextRandom(&randomState) % 23) - 11;
    for (int x = 0; x < width; ++x) {
      int sx = x + jitterX + tear;
      if (sx < 0)
        sx = 0;
      if (sx >= width)
        sx = width - 1;
      if (pixel > 1)
        sx -= sx % pixel;
      const Pixel &in = source[sy * width + sx];
      int r = in.r, g = in.g, b = in.b;
      int grey = (r * 77 + g * 150 + b * 29) >> 8;
      int sat = 100 + preset.saturation * amount / 100;
      r = grey + (r - grey) * sat / 100;
      g = grey + (g - grey) * sat / 100;
      b = grey + (b - grey) * sat / 100;
      r = r * (100 + (preset.red - 100) * amount / 100) / 100;
      g = g * (100 + (preset.green - 100) * amount / 100) / 100;
      b = b * (100 + (preset.blue - 100) * amount / 100) / 100;
      int contrast = 100 + preset.contrast * amount / 100;
      r = 128 + (r - 128) * contrast / 100;
      g = 128 + (g - 128) * contrast / 100;
      b = 128 + (b - 128) * contrast / 100;

      if (preset.flags & FX_MONO) {
        int m = (r * 77 + g * 150 + b * 29) >> 8;
        r = m * preset.red / 100;
        g = m * preset.green / 100;
        b = m * preset.blue / 100;
      }
      if (preset.flags & FX_THERMAL) {
        int t = grey;
        r = clamp8((t - 96) * 3);
        g = clamp8(255 - ((t - 128) < 0 ? -(t - 128) : (t - 128)) * 2);
        b = clamp8((128 - t) * 2);
      }
      if (preset.flags & FX_EDGE) {
        int xl = x > 0 ? x - 1 : x;
        int xr = x + 1 < width ? x + 1 : x;
        int yu = y > 0 ? y - 1 : y;
        int yd = y + 1 < height ? y + 1 : y;
        int edge =
            ((luma(source[y * width + xr]) - luma(source[y * width + xl])));
        if (edge < 0)
          edge = -edge;
        int ey = luma(source[yd * width + x]) - luma(source[yu * width + x]);
        if (ey < 0)
          ey = -ey;
        edge = clamp8((edge + ey) * 2);
        if (preset.flags & FX_INVERT)
          r = g = b = 255 - edge;
        else {
          r = clamp8(r - edge);
          g = clamp8(g - edge);
          b = clamp8(b - edge);
        }
      } else if (preset.flags & FX_INVERT) {
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;
      }
      if (preset.flags & FX_CGA) {
        int hi = grey > 150;
        int selector = ((r > b) ? 1 : 0) | ((g > r) ? 2 : 0);
        if (!hi && grey < 55)
          r = g = b = 0;
        else if (selector == 1) {
          r = 255;
          g = 45;
          b = 220;
        } else if (selector == 2) {
          r = 30;
          g = 235;
          b = 245;
        } else
          r = g = b = 255;
      }
      if (preset.flags & FX_DITHER) {
        static const int bayer[16] = {0, 8,  2, 10, 12, 4, 14, 6,
                                      3, 11, 1, 9,  15, 7, 13, 5};
        int d = (bayer[(y & 3) * 4 + (x & 3)] - 8) * 4;
        r += d;
        g += d;
        b += d;
      }
      if (preset.flags & FX_HALFTONE) {
        int cell = pixel < 3 ? 3 : pixel;
        int cx = x % cell, cy = y % cell;
        int dx = cx * 2 - cell, dy = cy * 2 - cell;
        int radius = (255 - grey) * cell / 255;
        if (dx * dx + dy * dy < radius * radius)
          r = g = b = 18;
        else {
          r = clamp8(r + 70);
          g = clamp8(g + 68);
          b = clamp8(b + 60);
        }
      }
      if (preset.flags & FX_ASCII) {
        int cell = pixel < 4 ? 4 : pixel;
        int mark = ((x % cell) == (grey / 32) % cell) || (y % cell == cell - 1);
        if (!mark) {
          r = r * 28 / 100;
          g = g * 28 / 100;
          b = b * 28 / 100;
        }
      }
      if (preset.posterize >= 2 && preset.posterize < 16) {
        int levels = preset.posterize;
        r = (clamp8(r) * (levels - 1) / 255) * 255 / (levels - 1);
        g = (clamp8(g) * (levels - 1) / 255) * 255 / (levels - 1);
        b = (clamp8(b) * (levels - 1) / 255) * 255 / (levels - 1);
      }
      if ((preset.flags & FX_TEMPORAL) && previous && preset.temporal) {
        const Pixel &old = previous[y * width + x];
        int mix = preset.temporal * amount / 100;
        r = (r * (100 - mix) + old.r * mix) / 100;
        g = (g * (100 - mix) + old.g * mix) / 100;
        b = (b * (100 - mix) + old.b * mix) / 100;
      }
      if ((preset.flags & FX_SCANLINES) && (y & 1)) {
        r = r * 68 / 100;
        g = g * 68 / 100;
        b = b * 68 / 100;
      }
      if ((preset.flags & FX_MASK) && ((x + y) % 3)) {
        if (x % 3 == 0)
          g = g * 82 / 100;
        else if (x % 3 == 1)
          b = b * 82 / 100;
        else
          r = r * 82 / 100;
      }
      if (preset.flags & FX_VIGNETTE) {
        int dx = (x * 2 - width), dy = (y * 2 - height);
        int distance = (dx * dx * 100 / (width * width)) +
                       (dy * dy * 100 / (height * height));
        int shade = 100 - (distance > 40 ? (distance - 40) * amount / 180 : 0);
        if (shade < 35)
          shade = 35;
        r = r * shade / 100;
        g = g * shade / 100;
        b = b * shade / 100;
      }
      if (preset.flags & FX_BLOOM) {
        int lift = grey > 185 ? (grey - 185) * amount / 250 : 0;
        r += lift;
        g += lift;
        b += lift;
      }
      if (preset.noise) {
        int n = (int)(nextRandom(&randomState) & 255) - 128;
        n = n * preset.noise * amount / 12800;
        r += n;
        g += n;
        b += n;
      }
      scratch[y * width + x].r = (uint8_t)clamp8(r);
      scratch[y * width + x].g = (uint8_t)clamp8(g);
      scratch[y * width + x].b = (uint8_t)clamp8(b);
    }
  }
  memcpy(output, scratch, (size_t)width * height * sizeof(Pixel));
}

void jsonEscape(FILE *output, const char *value) {
  fputc('"', output);
  if (value)
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
      if (*p == '"' || *p == '\\') {
        fputc('\\', output);
        fputc(*p, output);
      } else if (*p == '\n')
        fputs("\\n", output);
      else if (*p == '\r')
        fputs("\\r", output);
      else if (*p == '\t')
        fputs("\\t", output);
      else if (*p < 0x20)
        fprintf(output, "\\u%04x", (unsigned)*p);
      else
        fputc(*p, output);
    }
  fputc('"', output);
}

PerformanceDecision choosePerformance(int decodeMs, int filterMs, int renderMs,
                                      int droppedFrames, int forcedMode) {
  PerformanceDecision d;
  d.detail = forcedMode >= 0 ? forcedMode : 1;
  d.targetFps = 10;
  d.reducedAnimation = false;
  int total = decodeMs + filterMs + renderMs;
  if (forcedMode < 0 && (total > 100 || droppedFrames > 4))
    d.detail = 0;
  if (total > 120 || droppedFrames > 8) {
    d.targetFps = 8;
    d.reducedAnimation = true;
  }
  if (total > 170 || droppedFrames > 16) {
    d.targetFps = 6;
    if (forcedMode < 0)
      d.detail = 0;
  }
  return d;
}

static void writeZeros(FILE *file, int count) {
  while (count-- > 0)
    fputc(0, file);
}

AviWriter::AviWriter()
    : file_(0), width_(0), height_(0), fps_(0), frameCount_(0),
      bytesWritten_(0), riffSizeOffset_(0), moviSizeOffset_(0),
      moviDataOffset_(0), avihFramesOffset_(0), strhFramesOffset_(0) {}
AviWriter::~AviWriter() { abort(); }
void AviWriter::writeU16(uint16_t v) {
  fputc(v & 255, file_);
  fputc((v >> 8) & 255, file_);
}
void AviWriter::writeU32(uint32_t v) {
  writeU16((uint16_t)(v & 0xffff));
  writeU16((uint16_t)(v >> 16));
}
void AviWriter::writeFourCC(const char *v) { fwrite(v, 1, 4, file_); }
void AviWriter::patchU32(long offset, uint32_t value) {
  long here = ftell(file_);
  fseek(file_, offset, SEEK_SET);
  writeU32(value);
  fseek(file_, here, SEEK_SET);
}

bool AviWriter::open(const char *path, int width, int height, int fps) {
  abort();
  if (!path || width <= 0 || height <= 0 || fps <= 0)
    return false;
  file_ = fopen(path, "wb+");
  if (!file_)
    return false;
  width_ = width;
  height_ = height;
  fps_ = fps;
  frameCount_ = 0;
  writeFourCC("RIFF");
  riffSizeOffset_ = ftell(file_);
  writeU32(0);
  writeFourCC("AVI ");
  writeFourCC("LIST");
  long hdrlSize = ftell(file_);
  writeU32(0);
  long hdrlStart = ftell(file_);
  writeFourCC("hdrl");
  writeFourCC("avih");
  writeU32(56);
  writeU32(1000000U / (uint32_t)fps_);
  writeU32(0);
  writeU32(0);
  writeU32(0x10);
  avihFramesOffset_ = ftell(file_);
  writeU32(0);
  writeU32(0);
  writeU32(1);
  writeU32((uint32_t)(width_ * height_ * 3));
  writeU32((uint32_t)width_);
  writeU32((uint32_t)height_);
  writeZeros(file_, 16);
  writeFourCC("LIST");
  long strlSize = ftell(file_);
  writeU32(0);
  long strlStart = ftell(file_);
  writeFourCC("strl");
  writeFourCC("strh");
  writeU32(56);
  writeFourCC("vids");
  writeFourCC("MJPG");
  writeU32(0);
  writeU16(0);
  writeU16(0);
  writeU32(0);
  writeU32(1);
  writeU32((uint32_t)fps_);
  writeU32(0);
  strhFramesOffset_ = ftell(file_);
  writeU32(0);
  writeU32((uint32_t)(width_ * height_ * 3));
  writeU32(0xffffffffU);
  writeU32(0);
  writeU16(0);
  writeU16(0);
  writeU16((uint16_t)width_);
  writeU16((uint16_t)height_);
  writeFourCC("strf");
  writeU32(40);
  writeU32(40);
  writeU32((uint32_t)width_);
  writeU32((uint32_t)height_);
  writeU16(1);
  writeU16(24);
  writeFourCC("MJPG");
  writeU32((uint32_t)(width_ * height_ * 3));
  writeU32(0);
  writeU32(0);
  writeU32(0);
  writeU32(0);
  patchU32(strlSize, (uint32_t)(ftell(file_) - strlStart));
  patchU32(hdrlSize, (uint32_t)(ftell(file_) - hdrlStart));
  writeFourCC("LIST");
  moviSizeOffset_ = ftell(file_);
  writeU32(0);
  moviDataOffset_ = ftell(file_);
  writeFourCC("movi");
  bytesWritten_ = ftell(file_);
  return !ferror(file_);
}

bool AviWriter::addFrame(const unsigned char *jpeg, size_t size) {
  if (!file_ || !jpeg || !size || frameCount_ >= kMaxAviFrames ||
      size > 1024U * 1024U)
    return false;
  long chunk = ftell(file_);
  writeFourCC("00dc");
  writeU32((uint32_t)size);
  if (fwrite(jpeg, 1, size, file_) != size)
    return false;
  if (size & 1)
    fputc(0, file_);
  frameOffsets_[frameCount_] = (uint32_t)(chunk - moviDataOffset_);
  frameSizes_[frameCount_] = (uint32_t)size;
  frameCount_++;
  bytesWritten_ = ftell(file_);
  return !ferror(file_);
}

bool AviWriter::finish() {
  if (!file_)
    return false;
  long moviEnd = ftell(file_);
  patchU32(moviSizeOffset_, (uint32_t)(moviEnd - moviDataOffset_));
  writeFourCC("idx1");
  writeU32((uint32_t)(frameCount_ * 16));
  for (int i = 0; i < frameCount_; ++i) {
    writeFourCC("00dc");
    writeU32(0x10);
    writeU32(frameOffsets_[i]);
    writeU32(frameSizes_[i]);
  }
  patchU32(avihFramesOffset_, (uint32_t)frameCount_);
  patchU32(strhFramesOffset_, (uint32_t)frameCount_);
  patchU32(riffSizeOffset_, (uint32_t)(ftell(file_) - 8));
  bool ok = !ferror(file_) && fflush(file_) == 0;
  fclose(file_);
  file_ = 0;
  return ok;
}

void AviWriter::abort() {
  if (file_) {
    fclose(file_);
    file_ = 0;
  }
}

} // namespace retrolens
