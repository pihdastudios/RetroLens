#ifndef RETROLENS_DISPLAY_PROBE_H
#define RETROLENS_DISPLAY_PROBE_H

#include <stdint.h>

namespace retrolens {

static const int kDisplayProbeWidth = 256;
static const int kDisplayProbeHeight = 144;

uint16_t probeRgb565(int red, int green, int blue);
bool renderDisplayProbe(uint16_t* pixels, int width, int height, const char* buildId,
                        int surfaceWidth, int surfaceHeight, int surfaceFormat, int frameNumber);

} // namespace retrolens

#endif
