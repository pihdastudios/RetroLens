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

static int filterPresetIndex(int styleIndex) {
    if (styleIndex < 0)
        styleIndex = 0;
    if (styleIndex >= presetCount())
        styleIndex = presetCount() - 1;
    return styleIndex;
}

enum PhotoJob { kPhotoJobNone = 0, kPhotoJobSave = 1, kPhotoJobLoad = 2, kPhotoJobDelete = 3 };

static int clampValue(int value, int minimum, int maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static void applyCaptureTexture(Pixel* frame, const Preset& preset) {
    for (int y = 0; y < kFrameHeight; y++) {
        for (int x = 0; x < kFrameWidth; x++) {
            Pixel& pixel = frame[y * kFrameWidth + x];
            int red = pixel.r;
            int green = pixel.g;
            int blue = pixel.b;
            if ((preset.flags & FX_SCANLINES) && (y & 1)) {
                red = red * 78 / 100;
                green = green * 78 / 100;
                blue = blue * 78 / 100;
            }
            if (preset.flags & FX_MASK) {
                int channel = x % 3;
                red = red * (channel == 0 ? 100 : 84) / 100;
                green = green * (channel == 1 ? 100 : 84) / 100;
                blue = blue * (channel == 2 ? 100 : 84) / 100;
            }
            if (preset.flags & FX_VIGNETTE) {
                int dx = x * 2 - kFrameWidth;
                int dy = y * 2 - kFrameHeight;
                int distance = dx * dx + dy * dy;
                int shade = clampValue(
                    110 - distance * 35 / (kFrameWidth * kFrameWidth + kFrameHeight * kFrameHeight),
                    65, 100);
                red = red * shade / 100;
                green = green * shade / 100;
                blue = blue * shade / 100;
            }
            pixel.r = (uint8_t)clampValue(red, 0, 255);
            pixel.g = (uint8_t)clampValue(green, 0, 255);
            pixel.b = (uint8_t)clampValue(blue, 0, 255);
        }
    }
}

} // namespace

DisplayProbeWorker::DisplayProbeWorker(const char* buildId, int intervalMs, const char* storageRoot,
                                       const char* cameraModel, const char* versionName)
    : running_(false), threadStarted_(false), photoRunning_(false), photoThreadStarted_(false),
      intervalMs_(intervalMs), frameCount_(0), postCount_(0), surfaceWidth_(0), surfaceHeight_(0),
      surfaceFormat_(0), inputPending_(false), inputInUse_(false), hasPrevious_(false),
      hasRaw_(false), styleDirty_(false), selectedStyle_(0), intensity_(100), parameterIndex_(0),
      scene_(kPhotoSceneCamera), controlsVisible_(true), compare_(false), focusActive_(false),
      favoritesLo_(0), favoritesHi_(0), recentCount_(0), galleryIndex_(0), galleryLoadedIndex_(-1),
      galleryHasThumbnail_(false), settingsDirty_(false), diagnostics_(false), touchStartX_(0),
      touchStartY_(0), touchStartMs_(0), jpegLength_(0), jpegTimestampMs_(0),
      lastDecodedTimestampMs_(0), firstProcessedTimestampMs_(0), lastProcessedTimestampMs_(0),
      photoStore_(storageRoot, cameraModel, versionName), storageReady_(false),
      photoPending_(false), photoInUse_(false), photoJob_(kPhotoJobNone), photoPreset_(0),
      photoIntensity_(100), photoFavorite_(false), photoGalleryIndex_(0), photoTimestampMs_(0),
      photoStatus_(0), photoSavedCount_(0), photoFailedCount_(0), photoEncodedBytes_(0),
      galleryPhotoCount_(0), galleryPreset_(0) {
    if (intervalMs_ < 16)
        intervalMs_ = 16;
    if (intervalMs_ > 1000)
        intervalMs_ = 1000;
    strncpy(buildId_, buildId ? buildId : "UNKNOWN BUILD", sizeof(buildId_) - 1);
    buildId_[sizeof(buildId_) - 1] = 0;
    pthread_mutex_init(&mutex_, 0);
    pthread_cond_init(&condition_, 0);
    pthread_mutex_init(&photoMutex_, 0);
    pthread_cond_init(&photoCondition_, 0);
    memset(recent_, 0, sizeof(recent_));
    memset(adjustments_, 0, sizeof(adjustments_));
    memset(photoAdjustments_, 0, sizeof(photoAdjustments_));
    sequenceMetrics_ = calculateSequenceProbeMetrics(kSequenceOff, 0, 0, 0, 0, 0);
    memset(&filterMetrics_, 0, sizeof(filterMetrics_));
    RuntimeSettings settings;
    storageReady_ = photoStore_.initialize();
    if (storageReady_ && photoStore_.loadSettings(&settings)) {
        selectedStyle_ = settings.selectedPreset;
        intensity_ = settings.intensity;
        favoritesLo_ = settings.favoritesLo;
        favoritesHi_ = settings.favoritesHi;
        recentCount_ = settings.recentCount;
        diagnostics_ = settings.diagnostics;
        for (int i = 0; i < recentCount_; i++)
            recent_[i] = settings.recent[i];
        memcpy(adjustments_, settings.adjustments, sizeof(adjustments_));
    }
    filterMetrics_.selectedPreset = selectedStyle_;
    filterMetrics_.intensity = intensity_;
    filterMetrics_.parameterIndex = parameterIndex_;
    filterMetrics_.parameterValue = intensity_;
    filterMetrics_.scene = scene_;
    filterMetrics_.controlsVisible = controlsVisible_;
    filterMetrics_.diagnostics = diagnostics_;
    filterMetrics_.favorite = isFavoriteLocked(selectedStyle_);
    galleryPhotoCount_ = photoStore_.photoCount();
    filterMetrics_.galleryPhotoCount = galleryPhotoCount_;
    memset(raw_, 0, sizeof(raw_));
    memset(filtered_, 0, sizeof(filtered_));
    memset(previous_, 0, sizeof(previous_));
    memset(galleryThumbnail_, 0, sizeof(galleryThumbnail_));
    renderDisplayProbe(pixels_, kDisplayProbeWidth, kDisplayProbeHeight, buildId_, 0, 0, 0, 0,
                       sequenceMetrics_, 0, 0, 0, filterMetrics_);
}

DisplayProbeWorker::~DisplayProbeWorker() {
    stop();
    pthread_cond_destroy(&photoCondition_);
    pthread_mutex_destroy(&photoMutex_);
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
    if (storageReady_) {
        pthread_mutex_lock(&photoMutex_);
        photoRunning_ = true;
        pthread_mutex_unlock(&photoMutex_);
        if (pthread_create(&photoThread_, 0, photoThreadEntry, this) == 0) {
            photoThreadStarted_ = true;
        } else {
            pthread_mutex_lock(&photoMutex_);
            photoRunning_ = false;
            pthread_mutex_unlock(&photoMutex_);
            storageReady_ = false;
        }
    }
    return true;
}

int DisplayProbeWorker::stop() {
    if (!threadStarted_ && !photoThreadStarted_)
        return 0;
    int64_t started = monotonicMs();
    pthread_mutex_lock(&mutex_);
    running_ = false;
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
    if (threadStarted_) {
        pthread_join(thread_, 0);
        threadStarted_ = false;
    }
    pthread_mutex_lock(&photoMutex_);
    photoRunning_ = false;
    pthread_cond_broadcast(&photoCondition_);
    pthread_mutex_unlock(&photoMutex_);
    if (photoThreadStarted_) {
        pthread_join(photoThread_, 0);
        photoThreadStarted_ = false;
    }
    if (storageReady_ && settingsDirty_)
        photoStore_.saveSettings(settingsLocked());
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
        selectedStyle_ = (selectedStyle_ + delta) % presetCount();
        if (selectedStyle_ < 0)
            selectedStyle_ += presetCount();
        filterMetrics_.selectedPreset = filterPresetIndex(selectedStyle_);
        filterMetrics_.parameterValue =
            parameterIndex_ == 0 ? intensity_ : adjustments_[selectedStyle_][parameterIndex_ - 1];
        filterMetrics_.styleChanges++;
        styleDirty_ = hasRaw_;
        settingsDirty_ = true;
        recordRecentLocked(selectedStyle_);
        pthread_cond_broadcast(&condition_);
    }
    int presetIndex = filterMetrics_.selectedPreset;
    pthread_mutex_unlock(&mutex_);
    return presetIndex;
}

bool DisplayProbeWorker::key(int keyValue, bool down, int64_t timestampMs) {
    pthread_mutex_lock(&mutex_);
    if (keyValue == kPhotoKeyCompare) {
        compare_ = down;
        filterMetrics_.compare = compare_;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
        return true;
    }
    if (!down) {
        pthread_mutex_unlock(&mutex_);
        return true;
    }
    bool consumed = true;
    if (keyValue == kPhotoKeyPrevious || keyValue == kPhotoKeyNext) {
        int direction = keyValue == kPhotoKeyNext ? 1 : -1;
        if (scene_ == kPhotoSceneGallery) {
            if (galleryPhotoCount_ > 0) {
                galleryIndex_ =
                    (galleryIndex_ + direction + galleryPhotoCount_) % galleryPhotoCount_;
                galleryHasThumbnail_ = false;
                galleryLoadedIndex_ = -1;
                queueGalleryLoadLocked();
            }
        } else if (scene_ == kPhotoSceneControls) {
            adjustParameterLocked(direction);
        } else {
            selectedStyle_ = (selectedStyle_ + direction + presetCount()) % presetCount();
            filterMetrics_.selectedPreset = selectedStyle_;
            filterMetrics_.parameterValue = parameterIndex_ == 0
                                                ? intensity_
                                                : adjustments_[selectedStyle_][parameterIndex_ - 1];
            filterMetrics_.styleChanges++;
            filterMetrics_.favorite = isFavoriteLocked(selectedStyle_);
            styleDirty_ = hasRaw_;
            settingsDirty_ = true;
            recordRecentLocked(selectedStyle_);
        }
    } else if (keyValue == kPhotoKeyUp || keyValue == kPhotoKeyDown) {
        if (scene_ == kPhotoSceneControls) {
            parameterIndex_ = (parameterIndex_ + (keyValue == kPhotoKeyDown ? 1 : 3)) % 4;
            filterMetrics_.parameterIndex = parameterIndex_;
            filterMetrics_.parameterValue = parameterIndex_ == 0
                                                ? intensity_
                                                : adjustments_[selectedStyle_][parameterIndex_ - 1];
        } else if (scene_ != kPhotoSceneGallery) {
            changeCategoryLocked(keyValue == kPhotoKeyDown ? 1 : -1);
        }
    } else if (keyValue == kPhotoKeyConfirm) {
        if (scene_ == kPhotoSceneDeleteConfirm) {
            if (!queueGalleryDeleteLocked())
                photoStatus_ = kPhotoWriteBusy;
        } else if (scene_ == kPhotoSceneGallery) {
            toggleFavoriteLocked();
        } else {
            scene_ = scene_ == kPhotoSceneControls ? kPhotoSceneCamera : kPhotoSceneControls;
        }
    } else if (keyValue == kPhotoKeyBrowser) {
        scene_ = scene_ == kPhotoSceneBrowser ? kPhotoSceneCamera : kPhotoSceneBrowser;
    } else if (keyValue == kPhotoKeyGallery) {
        scene_ = scene_ == kPhotoSceneGallery ? kPhotoSceneCamera : kPhotoSceneGallery;
        if (scene_ == kPhotoSceneGallery) {
            galleryIndex_ = 0;
            if (!galleryHasThumbnail_ || galleryLoadedIndex_ != 0) {
                galleryHasThumbnail_ = false;
                galleryLoadedIndex_ = -1;
                queueGalleryLoadLocked();
            }
        }
    } else if (keyValue == kPhotoKeyBack) {
        if (scene_ == kPhotoSceneGallery) {
            scene_ = kPhotoSceneDeleteConfirm;
        } else if (scene_ == kPhotoSceneDeleteConfirm) {
            scene_ = kPhotoSceneGallery;
        } else if (scene_ != kPhotoSceneCamera) {
            scene_ = kPhotoSceneCamera;
        } else {
            consumed = false;
        }
    } else if (keyValue == kPhotoKeyRecord) {
        photoStatus_ = kPhotoWriteVideoDisabled;
    } else if (keyValue == kPhotoKeyFocus) {
        focusActive_ = true;
    } else if (keyValue == 13) {
        scene_ = scene_ == kPhotoSceneDiagnostics ? kPhotoSceneCamera : kPhotoSceneDiagnostics;
        diagnostics_ = scene_ == kPhotoSceneDiagnostics;
        settingsDirty_ = true;
    }
    filterMetrics_.scene = scene_;
    filterMetrics_.controlsVisible = controlsVisible_;
    filterMetrics_.favorite = isFavoriteLocked(selectedStyle_);
    filterMetrics_.focusActive = focusActive_;
    filterMetrics_.diagnostics = diagnostics_;
    filterMetrics_.photoStatus = photoStatus_;
    filterMetrics_.galleryIndex = galleryIndex_;
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
    (void)timestampMs;
    return consumed;
}

void DisplayProbeWorker::touch(int action, float x, float y, int64_t timestampMs) {
    pthread_mutex_lock(&mutex_);
    if (action == 0) {
        touchStartX_ = x;
        touchStartY_ = y;
        touchStartMs_ = timestampMs;
    } else if (action == 2 && timestampMs - touchStartMs_ >= 450) {
        compare_ = true;
    } else if (action == 1) {
        float dx = x - touchStartX_;
        compare_ = false;
        if (dx > 40 || dx < -40) {
            int direction = dx < 0 ? 1 : -1;
            selectedStyle_ = (selectedStyle_ + direction + presetCount()) % presetCount();
            filterMetrics_.selectedPreset = selectedStyle_;
            filterMetrics_.parameterValue = parameterIndex_ == 0
                                                ? intensity_
                                                : adjustments_[selectedStyle_][parameterIndex_ - 1];
            filterMetrics_.styleChanges++;
            styleDirty_ = hasRaw_;
            recordRecentLocked(selectedStyle_);
            settingsDirty_ = true;
        } else {
            controlsVisible_ = !controlsVisible_;
        }
    }
    filterMetrics_.compare = compare_;
    filterMetrics_.controlsVisible = controlsVisible_;
    filterMetrics_.favorite = isFavoriteLocked(selectedStyle_);
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
    (void)y;
}

int DisplayProbeWorker::requestPhoto(int64_t timestampMs) {
    pthread_mutex_lock(&mutex_);
    if (!storageReady_ || !hasPrevious_) {
        photoStatus_ = kPhotoWriteUnavailable;
        filterMetrics_.photoStatus = photoStatus_;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
        return kPhotoRequestUnavailable;
    }
    pthread_mutex_lock(&photoMutex_);
    if (!photoRunning_ || photoPending_ || photoInUse_) {
        pthread_mutex_unlock(&photoMutex_);
        photoStatus_ = kPhotoWriteBusy;
        filterMetrics_.photoStatus = photoStatus_;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
        return kPhotoRequestBusy;
    }
    memcpy(photoFrame_, filtered_, sizeof(photoFrame_));
    applyCaptureTexture(photoFrame_, adjustedPresetLocked(selectedStyle_));
    photoPreset_ = selectedStyle_;
    photoIntensity_ = intensity_;
    memcpy(photoAdjustments_, adjustments_[selectedStyle_], sizeof(photoAdjustments_));
    photoFavorite_ = isFavoriteLocked(selectedStyle_);
    photoTimestampMs_ = timestampMs;
    photoJob_ = kPhotoJobSave;
    photoPending_ = true;
    photoStatus_ = kPhotoWriteSaving;
    filterMetrics_.photoStatus = photoStatus_;
    pthread_cond_broadcast(&photoCondition_);
    pthread_mutex_unlock(&photoMutex_);
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
    return kPhotoRequestQueued;
}

void DisplayProbeWorker::setFocus(bool active) {
    pthread_mutex_lock(&mutex_);
    focusActive_ = active;
    filterMetrics_.focusActive = active;
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
}

bool DisplayProbeWorker::blitLatest(void* destination, int width, int height, int stride,
                                    int format, int* frameNumber) {
    pthread_mutex_lock(&mutex_);
    bool result = blitRgb565CenterCrop(pixels_, kDisplayProbeWidth, kDisplayProbeHeight,
                                       destination, width, height, stride, format);
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

bool DisplayProbeWorker::waitForPhotoResult(int minimumResults, int timeoutMs) {
    struct timespec deadline;
    deadlineFromNow(&deadline, timeoutMs);
    pthread_mutex_lock(&mutex_);
    while (running_ && photoSavedCount_ + photoFailedCount_ < minimumResults) {
        int result = pthread_cond_timedwait(&condition_, &mutex_, &deadline);
        if (result != 0)
            break;
    }
    bool reached = photoSavedCount_ + photoFailedCount_ >= minimumResults;
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

void* DisplayProbeWorker::photoThreadEntry(void* context) {
    static_cast<DisplayProbeWorker*>(context)->photoRun();
    return 0;
}

void DisplayProbeWorker::photoRun() {
    while (true) {
        pthread_mutex_lock(&photoMutex_);
        while (photoRunning_ && !photoPending_)
            pthread_cond_wait(&photoCondition_, &photoMutex_);
        if (!photoRunning_ && !photoPending_) {
            pthread_mutex_unlock(&photoMutex_);
            break;
        }
        int job = photoJob_;
        int presetIndex = photoPreset_;
        int intensity = photoIntensity_;
        int photoAdjustments[kPresetAdjustmentCount];
        memcpy(photoAdjustments, photoAdjustments_, sizeof(photoAdjustments));
        bool favorite = photoFavorite_;
        int galleryIndex = photoGalleryIndex_;
        int64_t timestampMs = photoTimestampMs_;
        photoPending_ = false;
        photoInUse_ = true;
        pthread_mutex_unlock(&photoMutex_);

        bool ok = false;
        int encodedBytes = 0;
        PhotoEntry entry;
        memset(&entry, 0, sizeof(entry));
        if (job == kPhotoJobSave && photoStore_.freeBytes() >= (int64_t)64 * 1024 * 1024) {
            ok = photoStore_.savePhoto(photoFrame_, presetIndex, intensity, photoAdjustments,
                                       favorite, timestampMs, scaledPhoto_, encodedPhoto_,
                                       sizeof(encodedPhoto_), &encodedBytes);
        } else if (job == kPhotoJobLoad) {
            ok = photoStore_.loadThumbnail(galleryIndex, photoFrame_) &&
                 photoStore_.getPhoto(galleryIndex, &entry);
        } else if (job == kPhotoJobDelete) {
            ok = photoStore_.deletePhoto(galleryIndex);
        }

        pthread_mutex_lock(&photoMutex_);
        photoInUse_ = false;
        photoJob_ = kPhotoJobNone;
        pthread_cond_broadcast(&photoCondition_);
        pthread_mutex_unlock(&photoMutex_);

        pthread_mutex_lock(&mutex_);
        if (job == kPhotoJobSave) {
            if (ok) {
                photoStatus_ = kPhotoWriteSaved;
                photoSavedCount_++;
                photoEncodedBytes_ = encodedBytes;
                galleryPhotoCount_ = photoStore_.photoCount();
                galleryIndex_ = 0;
                galleryLoadedIndex_ = 0;
                galleryHasThumbnail_ = true;
                galleryPreset_ = presetIndex;
                memcpy(galleryThumbnail_, photoFrame_, sizeof(galleryThumbnail_));
            } else {
                photoStatus_ = kPhotoWriteFailed;
                photoFailedCount_++;
            }
        } else if (job == kPhotoJobLoad) {
            galleryHasThumbnail_ = ok;
            if (ok) {
                memcpy(galleryThumbnail_, photoFrame_, sizeof(galleryThumbnail_));
                galleryPreset_ = entry.presetIndex;
                galleryLoadedIndex_ = galleryIndex;
            } else {
                galleryLoadedIndex_ = -1;
            }
            photoStatus_ = ok ? kPhotoWriteIdle : kPhotoWriteFailed;
        } else if (job == kPhotoJobDelete) {
            galleryPhotoCount_ = photoStore_.photoCount();
            if (galleryIndex_ >= galleryPhotoCount_)
                galleryIndex_ = galleryPhotoCount_ > 0 ? galleryPhotoCount_ - 1 : 0;
            galleryHasThumbnail_ = false;
            galleryLoadedIndex_ = -1;
            scene_ = kPhotoSceneGallery;
            photoStatus_ = ok ? kPhotoWriteDeleted : kPhotoWriteFailed;
            if (galleryPhotoCount_ > 0)
                queueGalleryLoadLocked();
        }
        filterMetrics_.photoStatus = photoStatus_;
        filterMetrics_.photoSavedCount = photoSavedCount_;
        filterMetrics_.photoFailedCount = photoFailedCount_;
        filterMetrics_.photoEncodedBytes = photoEncodedBytes_;
        filterMetrics_.galleryPhotoCount = galleryPhotoCount_;
        filterMetrics_.galleryIndex = galleryIndex_;
        filterMetrics_.galleryPreset = galleryPreset_;
        filterMetrics_.galleryHasThumbnail = galleryHasThumbnail_;
        filterMetrics_.scene = scene_;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
    }
}

void DisplayProbeWorker::changeCategoryLocked(int direction) {
    const char* category = presetAt(selectedStyle_).category;
    int candidate = selectedStyle_;
    do {
        candidate = (candidate + direction + presetCount()) % presetCount();
    } while (!strcmp(category, presetAt(candidate).category) && candidate != selectedStyle_);
    selectedStyle_ = candidate;
    filterMetrics_.selectedPreset = selectedStyle_;
    filterMetrics_.parameterValue =
        parameterIndex_ == 0 ? intensity_ : adjustments_[selectedStyle_][parameterIndex_ - 1];
    filterMetrics_.styleChanges++;
    filterMetrics_.favorite = isFavoriteLocked(selectedStyle_);
    styleDirty_ = hasRaw_;
    settingsDirty_ = true;
    recordRecentLocked(selectedStyle_);
}

void DisplayProbeWorker::adjustParameterLocked(int direction) {
    if (parameterIndex_ == 0) {
        intensity_ = clampValue(intensity_ + direction * 5, 0, 100);
        filterMetrics_.intensity = intensity_;
        filterMetrics_.parameterValue = intensity_;
    } else {
        static const int kMinimum[] = {-40, -20, -30};
        static const int kMaximum[] = {40, 20, 30};
        static const int kStep[] = {5, 2, 5};
        int index = parameterIndex_ - 1;
        adjustments_[selectedStyle_][index] =
            clampValue(adjustments_[selectedStyle_][index] + direction * kStep[index],
                       kMinimum[index], kMaximum[index]);
        filterMetrics_.parameterValue = adjustments_[selectedStyle_][index];
    }
    styleDirty_ = hasRaw_;
    settingsDirty_ = true;
}

Preset DisplayProbeWorker::adjustedPresetLocked(int presetIndex) const {
    Preset result = presetAt(presetIndex);
    result.contrast =
        (int16_t)clampValue(result.contrast + adjustments_[presetIndex][0], -100, 100);
    result.noise = (uint8_t)clampValue(result.noise + adjustments_[presetIndex][1], 0, 100);
    result.temporal = (uint8_t)clampValue(result.temporal + adjustments_[presetIndex][2], 0, 100);
    return result;
}

void DisplayProbeWorker::recordRecentLocked(int presetIndex) {
    int existing = -1;
    for (int i = 0; i < recentCount_; i++)
        if (recent_[i] == presetIndex)
            existing = i;
    if (existing >= 0)
        for (int i = existing; i > 0; i--)
            recent_[i] = recent_[i - 1];
    else {
        if (recentCount_ < kRecentPresetCount)
            recentCount_++;
        for (int i = recentCount_ - 1; i > 0; i--)
            recent_[i] = recent_[i - 1];
    }
    recent_[0] = presetIndex;
}

void DisplayProbeWorker::toggleFavoriteLocked() {
    if (selectedStyle_ < 64)
        favoritesLo_ ^= (uint64_t)1 << selectedStyle_;
    else
        favoritesHi_ ^= (uint64_t)1 << (selectedStyle_ - 64);
    filterMetrics_.favorite = isFavoriteLocked(selectedStyle_);
    settingsDirty_ = true;
}

bool DisplayProbeWorker::isFavoriteLocked(int presetIndex) const {
    if (presetIndex < 0 || presetIndex >= presetCount())
        return false;
    return presetIndex < 64 ? (favoritesLo_ & ((uint64_t)1 << presetIndex)) != 0
                            : (favoritesHi_ & ((uint64_t)1 << (presetIndex - 64))) != 0;
}

bool DisplayProbeWorker::queueGalleryLoadLocked() {
    if (!storageReady_ || galleryPhotoCount_ <= 0)
        return false;
    pthread_mutex_lock(&photoMutex_);
    if (!photoRunning_ || photoPending_ || photoInUse_) {
        pthread_mutex_unlock(&photoMutex_);
        return false;
    }
    photoGalleryIndex_ = galleryIndex_;
    photoJob_ = kPhotoJobLoad;
    photoPending_ = true;
    photoStatus_ = kPhotoWriteLoading;
    pthread_cond_broadcast(&photoCondition_);
    pthread_mutex_unlock(&photoMutex_);
    return true;
}

bool DisplayProbeWorker::queueGalleryDeleteLocked() {
    if (!storageReady_ || galleryPhotoCount_ <= 0)
        return false;
    pthread_mutex_lock(&photoMutex_);
    if (!photoRunning_ || photoPending_ || photoInUse_) {
        pthread_mutex_unlock(&photoMutex_);
        return false;
    }
    photoGalleryIndex_ = galleryIndex_;
    photoJob_ = kPhotoJobDelete;
    photoPending_ = true;
    photoStatus_ = kPhotoWriteSaving;
    pthread_cond_broadcast(&photoCondition_);
    pthread_mutex_unlock(&photoMutex_);
    return true;
}

RuntimeSettings DisplayProbeWorker::settingsLocked() const {
    RuntimeSettings settings;
    settings.selectedPreset = selectedStyle_;
    settings.intensity = intensity_;
    settings.favoritesLo = favoritesLo_;
    settings.favoritesHi = favoritesHi_;
    settings.recentCount = recentCount_;
    settings.motion = 2;
    settings.diagnostics = diagnostics_;
    for (int i = 0; i < kRecentPresetCount; i++)
        settings.recent[i] = i < recentCount_ ? recent_[i] : 0;
    memcpy(settings.adjustments, adjustments_, sizeof(settings.adjustments));
    return settings;
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
            int frameIntensity = intensity_;
            Preset framePreset = adjustedPresetLocked(presetIndex);
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
                             kFrameHeight, framePreset, frameIntensity, 0x52455452U, timestampMs);
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
            int frameIntensity = intensity_;
            Preset framePreset = adjustedPresetLocked(presetIndex);
            int64_t timestampMs = lastDecodedTimestampMs_;
            pthread_mutex_unlock(&mutex_);
            int64_t filterStartMs = monotonicMs();
            processFrame(raw_, filtered_, 0, 0, kFrameWidth, kFrameHeight, framePreset,
                         frameIntensity, 0x52455452U, timestampMs);
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
                       filterMetrics_.hasFrame ? raw_ : 0, filterMetrics_.hasFrame ? filtered_ : 0,
                       galleryHasThumbnail_ ? galleryThumbnail_ : 0, filterMetrics_);
}

} // namespace retrolens
