#ifndef RETROLENS_DISPLAY_PROBE_H
#define RETROLENS_DISPLAY_PROBE_H

#include <stdint.h>

namespace retrolens {

static const int kDisplayProbeWidth = 256;
static const int kDisplayProbeHeight = 144;

enum SequenceProbeState {
    kSequenceOff = 0,
    kSequenceStarting = 1,
    kSequenceActive = 2,
    kSequenceError = 3,
    kSequenceStopping = 4
};

struct SequenceProbeMetrics {
    int state;
    int receivedFrames;
    int releasedFrames;
    int lastJpegBytes;
    int fpsTenths;
};

SequenceProbeMetrics calculateSequenceProbeMetrics(int state, int receivedFrames,
                                                   int releasedFrames, int lastJpegBytes,
                                                   int64_t firstTimestampMs,
                                                   int64_t lastTimestampMs);

uint16_t probeRgb565(int red, int green, int blue);
bool renderDisplayProbe(uint16_t* pixels, int width, int height, const char* buildId,
                        int surfaceWidth, int surfaceHeight, int surfaceFormat, int frameNumber,
                        const SequenceProbeMetrics& sequence);

} // namespace retrolens

#endif
