#include "display_probe_worker.h"

#include <string.h>
#include <time.h>

#include "retrolens_core.h"

namespace retrolens {
namespace {

static int64_t monotonicMs() {
    struct timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return (int64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}

static void deadlineFromNow(struct timespec* deadline, int milliseconds) {
    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_sec += milliseconds / 1000;
    deadline->tv_nsec += (milliseconds % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

} // namespace

DisplayProbeWorker::DisplayProbeWorker(const char* buildId, int intervalMs)
    : running_(false), threadStarted_(false), intervalMs_(intervalMs), frameCount_(0),
      postCount_(0), surfaceWidth_(0), surfaceHeight_(0), surfaceFormat_(0) {
    if (intervalMs_ < 16)
        intervalMs_ = 16;
    if (intervalMs_ > 1000)
        intervalMs_ = 1000;
    strncpy(buildId_, buildId ? buildId : "UNKNOWN BUILD", sizeof(buildId_) - 1);
    buildId_[sizeof(buildId_) - 1] = 0;
    pthread_mutex_init(&mutex_, 0);
    pthread_cond_init(&condition_, 0);
    renderDisplayProbe(pixels_, kDisplayProbeWidth, kDisplayProbeHeight, buildId_, 0, 0, 0, 0);
}

DisplayProbeWorker::~DisplayProbeWorker() {
    stop();
    pthread_cond_destroy(&condition_);
    pthread_mutex_destroy(&mutex_);
}

bool DisplayProbeWorker::start() {
    pthread_mutex_lock(&mutex_);
    if (threadStarted_) {
        pthread_mutex_unlock(&mutex_);
        return true;
    }
    running_ = true;
    pthread_mutex_unlock(&mutex_);
    if (pthread_create(&thread_, 0, threadEntry, this) != 0) {
        pthread_mutex_lock(&mutex_);
        running_ = false;
        pthread_mutex_unlock(&mutex_);
        return false;
    }
    threadStarted_ = true;
    return true;
}

int DisplayProbeWorker::stop() {
    if (!threadStarted_)
        return 0;
    int64_t started = monotonicMs();
    pthread_mutex_lock(&mutex_);
    running_ = false;
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
    pthread_join(thread_, 0);
    threadStarted_ = false;
    return (int)(monotonicMs() - started);
}

void DisplayProbeWorker::updateSurfaceInfo(int width, int height, int format) {
    pthread_mutex_lock(&mutex_);
    if (surfaceWidth_ != width || surfaceHeight_ != height || surfaceFormat_ != format) {
        surfaceWidth_ = width;
        surfaceHeight_ = height;
        surfaceFormat_ = format;
        pthread_cond_broadcast(&condition_);
    }
    pthread_mutex_unlock(&mutex_);
}

bool DisplayProbeWorker::blitLatest(void* destination, int width, int height, int stride,
                                    int format, int* frameNumber) {
    pthread_mutex_lock(&mutex_);
    bool result = blitRgb565(pixels_, kDisplayProbeWidth, kDisplayProbeHeight, destination, width,
                             height, stride, format);
    if (result)
        postCount_++;
    if (frameNumber)
        *frameNumber = frameCount_;
    pthread_mutex_unlock(&mutex_);
    return result;
}

bool DisplayProbeWorker::waitForFrame(int minimumFrame, int timeoutMs) {
    struct timespec deadline;
    deadlineFromNow(&deadline, timeoutMs);
    pthread_mutex_lock(&mutex_);
    while (running_ && frameCount_ < minimumFrame) {
        int result = pthread_cond_timedwait(&condition_, &mutex_, &deadline);
        if (result != 0)
            break;
    }
    bool reached = frameCount_ >= minimumFrame;
    pthread_mutex_unlock(&mutex_);
    return reached;
}

void DisplayProbeWorker::getStats(int* frameCount, int* postCount) {
    pthread_mutex_lock(&mutex_);
    if (frameCount)
        *frameCount = frameCount_;
    if (postCount)
        *postCount = postCount_;
    pthread_mutex_unlock(&mutex_);
}

void* DisplayProbeWorker::threadEntry(void* context) {
    static_cast<DisplayProbeWorker*>(context)->run();
    return 0;
}

void DisplayProbeWorker::run() {
    pthread_mutex_lock(&mutex_);
    while (running_) {
        renderNextLocked();
        pthread_cond_broadcast(&condition_);
        struct timespec deadline;
        deadlineFromNow(&deadline, intervalMs_);
        pthread_cond_timedwait(&condition_, &mutex_, &deadline);
    }
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
}

void DisplayProbeWorker::renderNextLocked() {
    if (frameCount_ == 0x7fffffff)
        frameCount_ = 0;
    else
        frameCount_++;
    renderDisplayProbe(pixels_, kDisplayProbeWidth, kDisplayProbeHeight, buildId_, surfaceWidth_,
                       surfaceHeight_, surfaceFormat_, frameCount_);
}

} // namespace retrolens
