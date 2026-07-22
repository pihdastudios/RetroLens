#ifndef RETROLENS_DISPLAY_PROBE_WORKER_H
#define RETROLENS_DISPLAY_PROBE_WORKER_H

#include <pthread.h>
#include <stdint.h>

#include "display_probe.h"

namespace retrolens {

enum FilterSubmitStatus {
    kFilterSubmitAccepted = 0,
    kFilterSubmitBusyDropped = 1,
    kFilterSubmitInvalid = 2
};

static const int kFilterProbeInputCapacity = 256 * 1024;
static const int kFilterProbeStyleCount = 10;

class DisplayProbeWorker {
  public:
    DisplayProbeWorker(const char* buildId, int intervalMs);
    ~DisplayProbeWorker();
    bool start();
    int stop();
    void updateSurfaceInfo(int width, int height, int format);
    void updateSequenceMetrics(int state, int receivedFrames, int releasedFrames, int lastJpegBytes,
                               int64_t firstTimestampMs, int64_t lastTimestampMs);
    int submitJpeg(const unsigned char* jpeg, int length, int64_t timestampMs);
    int changeStyle(int delta);
    bool blitLatest(void* destination, int width, int height, int stride, int format,
                    int* frameNumber);
    bool waitForFrame(int minimumFrame, int timeoutMs);
    bool waitForProcessedFrame(int minimumFrame, int timeoutMs);
    void getStats(int* frameCount, int* postCount);
    void getFilterStats(FilterProbeMetrics* metrics);

  private:
    static void* threadEntry(void* context);
    void run();
    void renderNextLocked();

    pthread_mutex_t mutex_;
    pthread_cond_t condition_;
    pthread_t thread_;
    bool running_;
    bool threadStarted_;
    int intervalMs_;
    int frameCount_;
    int postCount_;
    int surfaceWidth_;
    int surfaceHeight_;
    int surfaceFormat_;
    SequenceProbeMetrics sequenceMetrics_;
    FilterProbeMetrics filterMetrics_;
    bool inputPending_;
    bool inputInUse_;
    bool hasPrevious_;
    bool hasRaw_;
    bool styleDirty_;
    int selectedStyle_;
    int jpegLength_;
    int64_t jpegTimestampMs_;
    int64_t lastDecodedTimestampMs_;
    int64_t firstProcessedTimestampMs_;
    int64_t lastProcessedTimestampMs_;
    unsigned char jpeg_[kFilterProbeInputCapacity];
    Pixel raw_[kFrameWidth * kFrameHeight];
    Pixel filtered_[kFrameWidth * kFrameHeight];
    Pixel previous_[kFrameWidth * kFrameHeight];
    char buildId_[48];
    uint16_t pixels_[kDisplayProbeWidth * kDisplayProbeHeight];
};

} // namespace retrolens

#endif
