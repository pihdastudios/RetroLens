#include "display_probe.h"

#include <stdio.h>
#include <string.h>

namespace retrolens {
namespace {

static const unsigned char kGlyphs[36][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {31, 4, 4, 4, 4, 4, 31},
    {7, 2, 2, 2, 18, 18, 12},     {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 30, 1, 1, 17, 14},   {6, 8, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12}};

static const unsigned char* glyph(char value) {
    if (value >= 'a' && value <= 'z')
        value = (char)(value - 'a' + 'A');
    if (value >= 'A' && value <= 'Z')
        return kGlyphs[value - 'A'];
    if (value >= '0' && value <= '9')
        return kGlyphs[26 + value - '0'];
    return 0;
}

static void pixel(uint16_t* pixels, int width, int height, int x, int y, uint16_t color) {
    if (x >= 0 && x < width && y >= 0 && y < height)
        pixels[y * width + x] = color;
}

static void rectangle(uint16_t* pixels, int width, int height, int left, int top, int right,
                      int bottom, uint16_t color) {
    int64_t boundedLeft = left;
    int64_t boundedTop = top;
    int64_t boundedRight = right;
    int64_t boundedBottom = bottom;
    if (boundedLeft < 0)
        boundedLeft = 0;
    if (boundedTop < 0)
        boundedTop = 0;
    if (boundedRight > width)
        boundedRight = width;
    if (boundedBottom > height)
        boundedBottom = height;
    for (int y = (int)boundedTop; y < boundedBottom; y++)
        for (int x = (int)boundedLeft; x < boundedRight; x++)
            pixels[y * width + x] = color;
}

static void text(uint16_t* pixels, int width, int height, int x, int y, const char* value,
                 uint16_t color) {
    if (!value)
        return;
    for (const char* character = value; *character; character++) {
        const unsigned char* rows = glyph(*character);
        if (rows)
            for (int row = 0; row < 7; row++)
                for (int column = 0; column < 5; column++)
                    if (rows[row] & (1 << (4 - column)))
                        pixel(pixels, width, height, x + column, y + row, color);
        x += 6;
        if (x + 5 >= width)
            break;
    }
}

static void shadowedText(uint16_t* pixels, int width, int height, int x, int y, const char* value,
                         uint16_t color, uint16_t shadow) {
    text(pixels, width, height, x + 1, y + 1, value, shadow);
    text(pixels, width, height, x, y, value, color);
}

} // namespace

uint16_t probeRgb565(int red, int green, int blue) {
    if (red < 0)
        red = 0;
    if (red > 255)
        red = 255;
    if (green < 0)
        green = 0;
    if (green > 255)
        green = 255;
    if (blue < 0)
        blue = 0;
    if (blue > 255)
        blue = 255;
    return (uint16_t)(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
}

SequenceProbeMetrics calculateSequenceProbeMetrics(int state, int receivedFrames,
                                                   int releasedFrames, int lastJpegBytes,
                                                   int64_t firstTimestampMs,
                                                   int64_t lastTimestampMs) {
    SequenceProbeMetrics result;
    result.state = state >= kSequenceOff && state <= kSequenceStopping ? state : kSequenceError;
    result.receivedFrames = receivedFrames < 0 ? 0 : receivedFrames;
    result.releasedFrames = releasedFrames < 0 ? 0 : releasedFrames;
    result.lastJpegBytes = lastJpegBytes < 0 ? 0 : lastJpegBytes;
    if (result.lastJpegBytes > 256 * 1024)
        result.lastJpegBytes = 256 * 1024;
    result.fpsTenths = 0;
    if (result.receivedFrames > 1 && firstTimestampMs > 0 && lastTimestampMs > firstTimestampMs) {
        int64_t elapsedMs = lastTimestampMs - firstTimestampMs;
        int64_t fpsTenths = ((int64_t)result.receivedFrames - 1) * 10000 / elapsedMs;
        result.fpsTenths = fpsTenths > 999 ? 999 : (int)fpsTenths;
    }
    return result;
}

bool renderDisplayProbe(uint16_t* pixels, int width, int height, const char* buildId,
                        int surfaceWidth, int surfaceHeight, int surfaceFormat, int frameNumber,
                        const SequenceProbeMetrics& sequence, const Pixel* original,
                        const Pixel* filtered, const Pixel* galleryThumbnail,
                        const FilterProbeMetrics& filter) {
    if (!pixels || width <= 0 || height <= 0)
        return false;

    const uint16_t background = probeRgb565(13, 17, 18);
    const uint16_t warm = probeRgb565(239, 232, 211);
    const uint16_t accent = probeRgb565(66, 232, 188);
    rectangle(pixels, width, height, 0, 0, width, height, background);

    int outstanding = sequence.receivedFrames - sequence.releasedFrames;
    bool imbalance = outstanding < 0 || outstanding > 1;
    bool galleryScene =
        filter.scene == kPhotoSceneGallery || filter.scene == kPhotoSceneDeleteConfirm;
    const Pixel* fullFrame = galleryScene ? (filter.galleryHasThumbnail ? galleryThumbnail : 0)
                                          : (filter.hasFrame ? filtered : 0);
    if (filter.hasFrame || galleryScene) {
        if (fullFrame) {
            for (int y = 0; y < height; y++) {
                int sourceY = y * kFrameHeight / height;
                for (int x = 0; x < width; x++) {
                    int sourceX = x * kFrameWidth / width;
                    bool showOriginal =
                        !galleryScene && original && filter.compare && x < width / 2;
                    const Pixel& source = showOriginal ? original[sourceY * kFrameWidth + sourceX]
                                                       : fullFrame[sourceY * kFrameWidth + sourceX];
                    pixels[y * width + x] = probeRgb565(source.r, source.g, source.b);
                }
            }
        }
        const int drawerWidth = 150;
        const Preset& selected = presetAt(filter.selectedPreset);
        char label[20];
        strncpy(label, selected.name, sizeof(label) - 1);
        label[sizeof(label) - 1] = 0;
        if (galleryScene) {
            rectangle(pixels, width, height, 0, 0, width, 14, background);
            shadowedText(pixels, width, height, 3, 3, "PHOTO GALLERY", accent, background);
            if (filter.galleryPhotoCount <= 0) {
                text(pixels, width, height, 72, 82, "NO RETRO PHOTOS", warm);
            } else if (galleryThumbnail && filter.galleryHasThumbnail) {
                char count[24];
                snprintf(count, sizeof(count), "%d/%d", filter.galleryIndex + 1,
                         filter.galleryPhotoCount);
                text(pixels, width, height, 204, 3, count, warm);
            } else {
                text(pixels, width, height, 99, 82, "LOADING", accent);
            }
            rectangle(pixels, width, height, 0, 151, width, height, background);
            if (filter.galleryPhotoCount > 0) {
                char galleryPreset[24];
                strncpy(galleryPreset, presetAt(filter.galleryPreset).name,
                        sizeof(galleryPreset) - 1);
                galleryPreset[sizeof(galleryPreset) - 1] = 0;
                text(pixels, width, height, 3, 155, galleryPreset, accent);
                text(pixels, width, height, 132, 155, "PROCESSED 320X240", warm);
            }
            text(pixels, width, height, 3, 170,
                 filter.scene == kPhotoSceneDeleteConfirm ? "CENTER DELETE    BACK CANCEL"
                                                          : "LEFT RIGHT       BACK DELETE",
                 filter.scene == kPhotoSceneDeleteConfirm ? probeRgb565(255, 150, 110) : warm);
        } else if (filter.scene == kPhotoSceneControls) {
            rectangle(pixels, width, height, 0, 0, drawerWidth, height, background);
            text(pixels, width, height, 3, 3, "QUICK CONTROL", accent);
            text(pixels, width, height, 3, 17, label, warm);
            static const char* kParameters[] = {"INTENSITY", "CONTRAST", "GRAIN", "MOTION"};
            char parameter[24];
            snprintf(parameter, sizeof(parameter), "%s %d", kParameters[filter.parameterIndex],
                     filter.parameterValue);
            text(pixels, width, height, 3, 34, parameter, warm);
            rectangle(pixels, width, height, 3, 47, 107, 54, probeRgb565(48, 55, 57));
            int bar =
                filter.parameterIndex == 0 ? filter.parameterValue : 50 + filter.parameterValue;
            if (bar < 0)
                bar = 0;
            if (bar > 100)
                bar = 100;
            rectangle(pixels, width, height, 3, 47, 3 + bar, 54, accent);
            text(pixels, width, height, 3, 62, "UP DOWN SELECT", warm);
            text(pixels, width, height, 3, 166, "CENTER CLOSE", accent);
        } else if (filter.scene == kPhotoSceneBrowser) {
            rectangle(pixels, width, height, 0, 0, drawerWidth, height, background);
            text(pixels, width, height, 3, 3, "STYLE BROWSER", accent);
            for (int row = -2; row <= 2; row++) {
                int index = (filter.selectedPreset + row + presetCount()) % presetCount();
                char name[18];
                strncpy(name, presetAt(index).name, sizeof(name) - 1);
                name[sizeof(name) - 1] = 0;
                text(pixels, width, height, 3, 18 + (row + 2) * 14, name, row == 0 ? accent : warm);
            }
        } else if (filter.scene == kPhotoSceneDiagnostics) {
            rectangle(pixels, width, height, 0, 0, drawerWidth, height, background);
            text(pixels, width, height, 3, 3, "DIAGNOSTICS", accent);
            char line[32];
            snprintf(line, sizeof(line), "FPS %d.%d D%d F%d", filter.processedFpsTenths / 10,
                     filter.processedFpsTenths % 10, filter.decodeMs, filter.filterMs);
            text(pixels, width, height, 3, 18, line, warm);
            snprintf(line, sizeof(line), "RX %d REL %d OUT %d", sequence.receivedFrames,
                     sequence.releasedFrames, outstanding);
            text(pixels, width, height, 3, 33, line, warm);
            snprintf(line, sizeof(line), "DROP %d JPEG %dK", filter.droppedFrames,
                     (sequence.lastJpegBytes + 1023) / 1024);
            text(pixels, width, height, 3, 48, line, warm);
            snprintf(line, sizeof(line), "SAVE %d FAIL %d", filter.photoSavedCount,
                     filter.photoFailedCount);
            text(pixels, width, height, 3, 63, line, warm);
            snprintf(line, sizeof(line), "CARD S%d W%d E%d", filter.photoStorageState,
                     filter.photoWriteStage, filter.photoWriteError);
            text(pixels, width, height, 3, 78, line,
                 filter.photoStorageState == 1 || filter.photoStorageState == 7
                     ? accent
                     : probeRgb565(255, 150, 110));
            text(pixels, width, height, 3, 166, "FN CLOSE", accent);
        } else if (filter.controlsVisible) {
            rectangle(pixels, width, height, 0, 0, width, 12, background);
            text(pixels, width, height, 2, 2, label, accent);
            if (filter.favorite)
                text(pixels, width, height, 226, 2, "F", warm);
            rectangle(pixels, width, height, 0, 158, width, height, background);
            text(pixels, width, height, 2, 163, selected.category, warm);
            text(pixels, width, height, 132, 163, "< STYLE > CENTER", accent);
            if (filter.focusActive)
                text(pixels, width, height, 204, 17, "FOCUS", accent);
        }
        if (!galleryScene && filter.compare) {
            text(pixels, width, height, 2, 14, "ORIGINAL", warm);
            text(pixels, width, height, width / 2 + 3, 14, "RETROLENS", accent);
        }
        const char* photoMessage = 0;
        if (filter.photoStatus == 1)
            photoMessage = "SAVING PHOTO";
        else if (filter.photoStatus == 2)
            photoMessage = "RETRO PHOTO SAVED";
        else if (filter.photoStatus == 3)
            photoMessage = "PHOTO SAVE FAILED";
        else if (filter.photoStatus == 4)
            photoMessage = "PHOTO WRITER BUSY";
        else if (filter.photoStatus == 6)
            photoMessage = "PHOTO DELETED";
        else if (filter.photoStatus == 7)
            photoMessage = "PHOTO ONLY - VIDEO OFF";
        else if (filter.photoStatus == 8) {
            if (filter.photoStorageState == 3)
                photoMessage = "RETRO FOLDER FAILED";
            else if (filter.photoStorageState == 4)
                photoMessage = "RETRO CARD LOW SPACE";
            else if (filter.photoStorageState == 5)
                photoMessage = "RETRO INDEX FAILED";
            else if (filter.photoStorageState == 6)
                photoMessage = "RETRO WRITE TEST FAILED";
            else if (filter.photoStorageState == 7)
                photoMessage = "RETRO STORAGE STARTING";
            else
                photoMessage = "RETRO CARD NOT READY";
        } else if (filter.photoStatus == 9)
            photoMessage = "RETRO FRAME NOT READY";
        if (photoMessage && !galleryScene) {
            rectangle(pixels, width, height, 30, 78, 210, 94, background);
            text(pixels, width, height, 35, 82, photoMessage,
                 filter.photoStatus == 3 ||
                         (filter.photoStatus == 8 && filter.photoStorageState != 7)
                     ? probeRgb565(255, 150, 110)
                     : accent);
        }
        if (imbalance || filter.decodeError) {
            rectangle(pixels, width, height, 30, 62, 210, 96, background);
            text(pixels, width, height, 55, 75, imbalance ? "BUFFER IMBALANCE" : "DECODE ERROR",
                 probeRgb565(255, 150, 110));
        }
        if (!galleryScene && filter.focusActive) {
            int centerX = width / 2;
            int centerY = height / 2;
            rectangle(pixels, width, height, centerX - 18, centerY - 12, centerX - 8, centerY - 11,
                      accent);
            rectangle(pixels, width, height, centerX + 8, centerY - 12, centerX + 18, centerY - 11,
                      accent);
            rectangle(pixels, width, height, centerX - 18, centerY + 11, centerX - 8, centerY + 12,
                      accent);
            rectangle(pixels, width, height, centerX + 8, centerY + 11, centerX + 18, centerY + 12,
                      accent);
        }
        return true;
    }

    const uint16_t bars[] = {probeRgb565(239, 232, 211), probeRgb565(66, 232, 188),
                             probeRgb565(229, 91, 112),  probeRgb565(244, 193, 74),
                             probeRgb565(89, 132, 212),  probeRgb565(171, 112, 205)};
    int barLeft = 9;
    int barRight = width > 120 ? 111 : width - 9;
    int barWidth = barRight > barLeft ? (barRight - barLeft) / 6 : 0;
    for (int index = 0; index < 6; index++)
        rectangle(pixels, width, height, barLeft + index * barWidth, 7,
                  index == 5 ? barRight : barLeft + (index + 1) * barWidth, 25, bars[index]);

    char threadStatus[64];
    snprintf(threadStatus, sizeof(threadStatus), "PHOTO ENGINE F%d", frameNumber);
    text(pixels, width, height, 4, 30, threadStatus, accent);
    const char* state = "SEQUENCE OFF";
    if (sequence.state == kSequenceStarting)
        state = "SEQUENCE STARTING";
    else if (sequence.state == kSequenceActive)
        state = "SEQUENCE ACTIVE";
    else if (sequence.state == kSequenceError)
        state = "SEQUENCE ERROR";
    else if (sequence.state == kSequenceStopping)
        state = "SEQUENCE STOPPING";
    if (filter.decodeError)
        state = "DECODE ERROR";
    text(pixels, width, height, 4, 45, imbalance ? "BUFFER IMBALANCE" : state,
         imbalance || sequence.state == kSequenceError || filter.decodeError
             ? probeRgb565(255, 150, 110)
             : warm);
    char balance[64];
    snprintf(balance, sizeof(balance), "RX %d REL %d OUT %d", sequence.receivedFrames,
             sequence.releasedFrames, outstanding);
    text(pixels, width, height, 4, 60, balance, warm);
    char analytical[64];
    if (filter.decodeError)
        snprintf(analytical, sizeof(analytical), "DECODE FAIL %d JPEG %dK", filter.decodeFailures,
                 (sequence.lastJpegBytes + 1023) / 1024);
    else
        snprintf(analytical, sizeof(analytical), "FPS %d.%d JPEG %dK", sequence.fpsTenths / 10,
                 sequence.fpsTenths % 10, (sequence.lastJpegBytes + 1023) / 1024);
    text(pixels, width, height, 4, 75, analytical, accent);
    int sweepWidth = width > 120 ? 116 : width - 4;
    int sweep = 2 + (int)(((unsigned int)frameNumber * 7U) % (unsigned int)sweepWidth);
    rectangle(pixels, width, height, sweep, 86, sweep + 2, 90, warm);

    (void)surfaceWidth;
    (void)surfaceHeight;
    (void)surfaceFormat;
    (void)buildId;

    rectangle(pixels, width, height, 0, 0, width, 2, accent);
    rectangle(pixels, width, height, 0, 88, width > 120 ? 120 : width, 90, accent);
    rectangle(pixels, width, height, 0, 0, 2, height > 90 ? 90 : height, accent);
    rectangle(pixels, width, height, width > 120 ? 118 : width - 2, 0, width > 120 ? 120 : width,
              height > 90 ? 90 : height, accent);
    return true;
}

} // namespace retrolens
