#ifndef RETROLENS_DISPLAY_PROBE_H
#define RETROLENS_DISPLAY_PROBE_H

#include <stdint.h>

#include "retrolens_core.h"

namespace retrolens {

static const int kDisplayProbeWidth = 240;
static const int kDisplayProbeHeight = 180;

enum SequenceProbeState {
    kSequenceOff = 0,
    kSequenceStarting = 1,
    kSequenceActive = 2,
    kSequenceError = 3,
    kSequenceStopping = 4
};

enum PhotoUiScene {
    kPhotoSceneCamera = 0,
    kPhotoSceneControls = 1,
    kPhotoSceneBrowser = 2,
    kPhotoSceneGallery = 3,
    kPhotoSceneDiagnostics = 4,
    kPhotoSceneDeleteConfirm = 5
};

struct SequenceProbeMetrics {
    int state;
    int receivedFrames;
    int releasedFrames;
    int lastJpegBytes;
    int fpsTenths;
};

struct FilterProbeMetrics {
    bool hasFrame;
    bool decodeError;
    int acceptedFrames;
    int processedFrames;
    int droppedFrames;
    int decodeFailures;
    int decodeMs;
    int filterMs;
    int processedFpsTenths;
    int selectedPreset;
    int styleChanges;
    int intensity;
    int parameterIndex;
    int parameterValue;
    int scene;
    bool controlsVisible;
    bool compare;
    bool favorite;
    bool focusActive;
    bool diagnostics;
    int photoStatus;
    int photoSavedCount;
    int photoFailedCount;
    int photoEncodedBytes;
    int galleryPhotoCount;
    int galleryIndex;
    int galleryPreset;
    bool galleryHasThumbnail;
    int photoStorageState;
    int photoStorageAttempts;
    int photoWriteStage;
    int photoWriteError;
    int photoFreeMiB;
};

SequenceProbeMetrics calculateSequenceProbeMetrics(int state, int receivedFrames,
                                                   int releasedFrames, int lastJpegBytes,
                                                   int64_t firstTimestampMs,
                                                   int64_t lastTimestampMs);

uint16_t probeRgb565(int red, int green, int blue);
bool renderDisplayProbe(uint16_t* pixels, int width, int height, const char* buildId,
                        int surfaceWidth, int surfaceHeight, int surfaceFormat, int frameNumber,
                        const SequenceProbeMetrics& sequence, const Pixel* original,
                        const Pixel* filtered, const Pixel* galleryThumbnail,
                        const FilterProbeMetrics& filter);

} // namespace retrolens

#endif
