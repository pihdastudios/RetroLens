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
                        const SequenceProbeMetrics& sequence, const Pixel* filtered,
                        const FilterProbeMetrics& filter) {
    if (!pixels || width <= 0 || height <= 0)
        return false;

    const uint16_t background = probeRgb565(13, 17, 18);
    const uint16_t warm = probeRgb565(239, 232, 211);
    const uint16_t accent = probeRgb565(66, 232, 188);
    rectangle(pixels, width, height, 0, 0, width, height, background);

    int outstanding = sequence.receivedFrames - sequence.releasedFrames;
    bool imbalance = outstanding < 0 || outstanding > 1;
    if (filtered && filter.hasFrame) {
        for (int y = 0; y < height; y++) {
            int sourceY = y * kFrameHeight / height;
            for (int x = 0; x < width; x++) {
                int sourceX = x * kFrameWidth / width;
                const Pixel& source = filtered[sourceY * kFrameWidth + sourceX];
                pixels[y * width + x] = probeRgb565(source.r, source.g, source.b);
            }
        }
        char top[64];
        snprintf(top, sizeof(top), "%s P%d.%d D%d F%d", presetAt(filter.selectedPreset).name,
                 filter.processedFpsTenths / 10, filter.processedFpsTenths % 10, filter.decodeMs,
                 filter.filterMs);
        shadowedText(pixels, width, height, 4, 4, top, accent, background);
        char bottom[64];
        snprintf(bottom, sizeof(bottom), "R%d/%d O%d X%d J%dK", sequence.receivedFrames,
                 sequence.releasedFrames, outstanding, filter.droppedFrames,
                 (sequence.lastJpegBytes + 1023) / 1024);
        shadowedText(pixels, width, height, 4, height - 11, bottom,
                     imbalance || filter.decodeError ? probeRgb565(255, 150, 110) : warm,
                     background);
        if (imbalance || filter.decodeError) {
            int errorTop = height / 2 - 14;
            rectangle(pixels, width, height, 24, errorTop, width - 24, errorTop + 28, background);
            text(pixels, width, height, 36, errorTop + 10,
                 imbalance ? "BUFFER IMBALANCE" : "DECODE ERROR", probeRgb565(255, 150, 110));
        }
        return true;
    }

    const uint16_t bars[] = {probeRgb565(239, 232, 211), probeRgb565(66, 232, 188),
                             probeRgb565(229, 91, 112),  probeRgb565(244, 193, 74),
                             probeRgb565(89, 132, 212),  probeRgb565(171, 112, 205)};
    int barLeft = 9;
    int barRight = width - 9;
    int barWidth = barRight > barLeft ? (barRight - barLeft) / 6 : 0;
    for (int index = 0; index < 6; index++)
        rectangle(pixels, width, height, barLeft + index * barWidth, 7,
                  index == 5 ? barRight : barLeft + (index + 1) * barWidth, 25, bars[index]);

    char threadStatus[64];
    snprintf(threadStatus, sizeof(threadStatus), "NATIVE THREAD 8 FPS F%d", frameNumber);
    text(pixels, width, height, 10, 38, threadStatus, accent);
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
    text(pixels, width, height, 10, 62, imbalance ? "BUFFER IMBALANCE" : state,
         imbalance || sequence.state == kSequenceError || filter.decodeError
             ? probeRgb565(255, 150, 110)
             : warm);
    char balance[64];
    snprintf(balance, sizeof(balance), "RX %d REL %d OUT %d", sequence.receivedFrames,
             sequence.releasedFrames, outstanding);
    text(pixels, width, height, 10, 86, balance, warm);
    char analytical[64];
    if (filter.decodeError)
        snprintf(analytical, sizeof(analytical), "DECODE FAIL %d JPEG %dK", filter.decodeFailures,
                 (sequence.lastJpegBytes + 1023) / 1024);
    else
        snprintf(analytical, sizeof(analytical), "FPS %d.%d JPEG %dK", sequence.fpsTenths / 10,
                 sequence.fpsTenths % 10, (sequence.lastJpegBytes + 1023) / 1024);
    text(pixels, width, height, 10, 110, analytical, accent);
    text(pixels, width, height, 10, 134, buildId ? buildId : "UNKNOWN BUILD", warm);
    int sweepWidth = width > 20 ? width - 20 : 1;
    int sweep = 10 + (int)(((unsigned int)frameNumber * 7U) % (unsigned int)sweepWidth);
    rectangle(pixels, width, height, sweep, height - 20, sweep + 2, height - 4, warm);

    (void)surfaceWidth;
    (void)surfaceHeight;
    (void)surfaceFormat;

    rectangle(pixels, width, height, 0, 0, width, 2, accent);
    rectangle(pixels, width, height, 0, height - 2, width, height, accent);
    rectangle(pixels, width, height, 0, 0, 2, height, accent);
    rectangle(pixels, width, height, width - 2, 0, width, height, accent);
    return true;
}

} // namespace retrolens
