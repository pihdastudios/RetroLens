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

bool renderDisplayProbe(uint16_t* pixels, int width, int height, const char* buildId,
                        int surfaceWidth, int surfaceHeight, int surfaceFormat, int frameNumber) {
    if (!pixels || width <= 0 || height <= 0)
        return false;

    const uint16_t background = probeRgb565(13, 17, 18);
    const uint16_t warm = probeRgb565(239, 232, 211);
    const uint16_t accent = probeRgb565(66, 232, 188);
    rectangle(pixels, width, height, 0, 0, width, height, background);

    const uint16_t bars[] = {probeRgb565(239, 232, 211), probeRgb565(66, 232, 188),
                             probeRgb565(229, 91, 112),  probeRgb565(244, 193, 74),
                             probeRgb565(89, 132, 212),  probeRgb565(171, 112, 205)};
    int barLeft = 9;
    int barRight = width - 9;
    int barWidth = barRight > barLeft ? (barRight - barLeft) / 6 : 0;
    for (int index = 0; index < 6; index++)
        rectangle(pixels, width, height, barLeft + index * barWidth, 9,
                  index == 5 ? barRight : barLeft + (index + 1) * barWidth, 36, bars[index]);

    text(pixels, width, height, 10, 49, "NATIVE DISPLAY OK", accent);
    char geometry[64];
    snprintf(geometry, sizeof(geometry), "SURFACE %dX%d FMT %d", surfaceWidth, surfaceHeight,
             surfaceFormat);
    text(pixels, width, height, 10, 70, geometry, warm);
    text(pixels, width, height, 10, 91, buildId ? buildId : "UNKNOWN BUILD", warm);
    char threadStatus[64];
    snprintf(threadStatus, sizeof(threadStatus), "THREAD 8 FPS FRAME %d", frameNumber);
    text(pixels, width, height, 10, 112, threadStatus, accent);
    int sweepWidth = width > 20 ? width - 20 : 1;
    int sweep = 10 + (int)(((unsigned int)frameNumber * 7U) % (unsigned int)sweepWidth);
    rectangle(pixels, width, height, sweep, 128, sweep + 2, height - 4, warm);

    rectangle(pixels, width, height, 0, 0, width, 2, accent);
    rectangle(pixels, width, height, 0, height - 2, width, height, accent);
    rectangle(pixels, width, height, 0, 0, 2, height, accent);
    rectangle(pixels, width, height, width - 2, 0, width, height, accent);
    return true;
}

} // namespace retrolens
