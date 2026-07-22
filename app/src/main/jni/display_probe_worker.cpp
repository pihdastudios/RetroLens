#include "display_probe_worker.h"

#include <string.h>
#include <time.h>

#include "reduced_jpeg_decoder.h"
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

static const char* kFilterStyleIds[kFilterProbeStyleCount] = {
    "olive_pocket",     "cga_shock",           "one_bit_desktop", "consumer_crt",
    "vhs_rental",       "soviet_archive_1978", "newsprint",       "comic_ink",
    "piss_filter_2007", "thermal_false_color"};

static int filterPresetIndex(int styleIndex) {
    if (styleIndex < 0)
        styleIndex = 0;
    if (styleIndex >= kFilterProbeStyleCount)
        styleIndex = kFilterProbeStyleCount - 1;
    return findPreset(kFilterStyleIds[styleIndex]);
}

} // namespace

DisplayProbeWorker::DisplayProbeWorker(const char* buildId, int intervalMs)
    : running_(false), threadStarted_(false), intervalMs_(intervalMs), frameCount_(0),
      postCount_(0), surfaceWidth_(0), surfaceHeight_(0), surfaceFormat_(0), inputPending_(false),
      inputInUse_(false), hasPrevious_(false), hasRaw_(false), styleDirty_(false),
      selectedStyle_(0), jpegLength_(0), jpegTimestampMs_(0), lastDecodedTimestampMs_(0),
      firstProcessedTimestampMs_(0), lastProcessedTimestampMs_(0) {
    if (intervalMs_ < 16)
        intervalMs_ = 16;
    if (intervalMs_ > 1000)
        intervalMs_ = 1000;
    strncpy(buildId_, buildId ? buildId : "UNKNOWN BUILD", sizeof(buildId_) - 1);
    buildId_[sizeof(buildId_) - 1] = 0;
    pthread_mutex_init(&mutex_, 0);
    pthread_cond_init(&condition_, 0);
    sequenceMetrics_ = calculateSequenceProbeMetrics(kSequenceOff, 0, 0, 0, 0, 0);
    memset(&filterMetrics_, 0, sizeof(filterMetrics_));
    filterMetrics_.selectedPreset = filterPresetIndex(selectedStyle_);
    memset(raw_, 0, sizeof(raw_));
    memset(filtered_, 0, sizeof(filtered_));
    memset(previous_, 0, sizeof(previous_));
    renderDisplayProbe(pixels_, kDisplayProbeWidth, kDisplayProbeHeight, buildId_, 0, 0, 0, 0,
                       sequenceMetrics_, 0, filterMetrics_);
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

void DisplayProbeWorker::updateSequenceMetrics(int state, int receivedFrames, int releasedFrames,
                                               int lastJpegBytes, int64_t firstTimestampMs,
                                               int64_t lastTimestampMs) {
    pthread_mutex_lock(&mutex_);
    sequenceMetrics_ = calculateSequenceProbeMetrics(
        state, receivedFrames, releasedFrames, lastJpegBytes, firstTimestampMs, lastTimestampMs);
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
}

int DisplayProbeWorker::submitJpeg(const unsigned char* jpeg, int length, int64_t timestampMs) {
    if (!jpeg || length < 4 || length > kFilterProbeInputCapacity || timestampMs <= 0)
        return kFilterSubmitInvalid;
    pthread_mutex_lock(&mutex_);
    if (!running_) {
        pthread_mutex_unlock(&mutex_);
        return kFilterSubmitInvalid;
    }
    if (inputPending_ || inputInUse_) {
        filterMetrics_.droppedFrames++;
        pthread_mutex_unlock(&mutex_);
        return kFilterSubmitBusyDropped;
    }
    memcpy(jpeg_, jpeg, (size_t)length);
    jpegLength_ = length;
    jpegTimestampMs_ = timestampMs;
    inputPending_ = true;
    filterMetrics_.acceptedFrames++;
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
    return kFilterSubmitAccepted;
}

int DisplayProbeWorker::changeStyle(int delta) {
    pthread_mutex_lock(&mutex_);
    if (delta) {
        selectedStyle_ = (selectedStyle_ + delta) % kFilterProbeStyleCount;
        if (selectedStyle_ < 0)
            selectedStyle_ += kFilterProbeStyleCount;
        filterMetrics_.selectedPreset = filterPresetIndex(selectedStyle_);
        filterMetrics_.styleChanges++;
        styleDirty_ = hasRaw_;
        pthread_cond_broadcast(&condition_);
    }
    int presetIndex = filterMetrics_.selectedPreset;
    pthread_mutex_unlock(&mutex_);
    return presetIndex;
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

bool DisplayProbeWorker::waitForProcessedFrame(int minimumFrame, int timeoutMs) {
    struct timespec deadline;
    deadlineFromNow(&deadline, timeoutMs);
    pthread_mutex_lock(&mutex_);
    while (running_ && filterMetrics_.processedFrames < minimumFrame) {
        int result = pthread_cond_timedwait(&condition_, &mutex_, &deadline);
        if (result != 0)
            break;
    }
    bool reached = filterMetrics_.processedFrames >= minimumFrame;
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

void DisplayProbeWorker::getFilterStats(FilterProbeMetrics* metrics) {
    pthread_mutex_lock(&mutex_);
    if (metrics)
        *metrics = filterMetrics_;
    pthread_mutex_unlock(&mutex_);
}

void* DisplayProbeWorker::threadEntry(void* context) {
    static_cast<DisplayProbeWorker*>(context)->run();
    return 0;
}

void DisplayProbeWorker::run() {
    pthread_mutex_lock(&mutex_);
    while (running_) {
        if (inputPending_) {
            inputPending_ = false;
            inputInUse_ = true;
            int length = jpegLength_;
            int64_t timestampMs = jpegTimestampMs_;
            int presetIndex = filterMetrics_.selectedPreset;
            bool usePrevious = hasPrevious_ && !styleDirty_;
            if (styleDirty_) {
                hasPrevious_ = false;
                styleDirty_ = false;
            }
            pthread_mutex_unlock(&mutex_);

            int64_t decodeStartMs = monotonicMs();
            bool decoded = decodeReducedJpeg(jpeg_, (size_t)length, raw_);
            int64_t filterStartMs = monotonicMs();
            if (decoded)
                processFrame(raw_, filtered_, 0, usePrevious ? previous_ : 0, kFrameWidth,
                             kFrameHeight, presetAt(presetIndex), 100, 0x52455452U, timestampMs);
            int64_t completedMs = monotonicMs();

            pthread_mutex_lock(&mutex_);
            filterMetrics_.decodeMs = (int)(filterStartMs - decodeStartMs);
            filterMetrics_.filterMs = (int)(completedMs - filterStartMs);
            if (decoded) {
                memcpy(previous_, filtered_, sizeof(previous_));
                hasPrevious_ = true;
                hasRaw_ = true;
                lastDecodedTimestampMs_ = timestampMs;
                filterMetrics_.hasFrame = true;
                filterMetrics_.decodeError = false;
                filterMetrics_.processedFrames++;
                if (!firstProcessedTimestampMs_)
                    firstProcessedTimestampMs_ = timestampMs;
                lastProcessedTimestampMs_ = timestampMs;
                if (filterMetrics_.processedFrames > 1 &&
                    lastProcessedTimestampMs_ > firstProcessedTimestampMs_) {
                    int64_t elapsedMs = lastProcessedTimestampMs_ - firstProcessedTimestampMs_;
                    int64_t fpsTenths =
                        ((int64_t)filterMetrics_.processedFrames - 1) * 10000 / elapsedMs;
                    filterMetrics_.processedFpsTenths = fpsTenths > 999 ? 999 : (int)fpsTenths;
                }
            } else {
                filterMetrics_.decodeError = true;
                filterMetrics_.decodeFailures++;
            }
            inputInUse_ = false;
        } else if (styleDirty_ && hasRaw_) {
            styleDirty_ = false;
            hasPrevious_ = false;
            int presetIndex = filterMetrics_.selectedPreset;
            int64_t timestampMs = lastDecodedTimestampMs_;
            pthread_mutex_unlock(&mutex_);
            int64_t filterStartMs = monotonicMs();
            processFrame(raw_, filtered_, 0, 0, kFrameWidth, kFrameHeight, presetAt(presetIndex),
                         100, 0x52455452U, timestampMs);
            int64_t completedMs = monotonicMs();
            pthread_mutex_lock(&mutex_);
            memcpy(previous_, filtered_, sizeof(previous_));
            hasPrevious_ = true;
            filterMetrics_.filterMs = (int)(completedMs - filterStartMs);
            filterMetrics_.hasFrame = true;
        }
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
                       surfaceHeight_, surfaceFormat_, frameCount_, sequenceMetrics_,
                       filterMetrics_.hasFrame ? filtered_ : 0, filterMetrics_);
}

} // namespace retrolens
