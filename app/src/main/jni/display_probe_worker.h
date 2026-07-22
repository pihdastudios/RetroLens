#ifndef RETROLENS_DISPLAY_PROBE_WORKER_H
#define RETROLENS_DISPLAY_PROBE_WORKER_H

#include <pthread.h>
#include <stdint.h>

#include "display_probe.h"

namespace retrolens {

class DisplayProbeWorker {
  public:
    DisplayProbeWorker(const char* buildId, int intervalMs);
    ~DisplayProbeWorker();
    bool start();
    int stop();
    void updateSurfaceInfo(int width, int height, int format);
    bool blitLatest(void* destination, int width, int height, int stride, int format,
                    int* frameNumber);
    bool waitForFrame(int minimumFrame, int timeoutMs);
    void getStats(int* frameCount, int* postCount);

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
    char buildId_[48];
    uint16_t pixels_[kDisplayProbeWidth * kDisplayProbeHeight];
};

} // namespace retrolens

#endif
