#ifndef RETROLENS_DISPLAY_PROBE_WORKER_H
#define RETROLENS_DISPLAY_PROBE_WORKER_H

#include <pthread.h>
#include <stdint.h>

#include "display_probe.h"
#include "photo_store.h"

namespace retrolens {

enum FilterSubmitStatus {
    kFilterSubmitAccepted = 0,
    kFilterSubmitBusyDropped = 1,
    kFilterSubmitInvalid = 2
};

static const int kFilterProbeInputCapacity = 256 * 1024;
static const int kFilterProbeStyleCount = kMaxPresets;

enum PhotoRequestStatus {
    kPhotoRequestQueued = 0,
    kPhotoRequestBusy = 1,
    kPhotoRequestUnavailable = 2
};

enum StorageProbeStatus {
    kStorageProbeInitializing = 0,
    kStorageProbeReady = 1,
    kStorageProbeRootUnavailable = 2,
    kStorageProbeDirectoryFailed = 3,
    kStorageProbeWriteFailed = 4,
    kStorageProbeInsufficientSpace = 5
};

enum PhotoWriteState {
    kPhotoWriteIdle = 0,
    kPhotoWriteSaving = 1,
    kPhotoWriteSaved = 2,
    kPhotoWriteFailed = 3,
    kPhotoWriteBusy = 4,
    kPhotoWriteLoading = 5,
    kPhotoWriteDeleted = 6,
    kPhotoWriteVideoDisabled = 7,
    kPhotoWriteUnavailable = 8,
    kPhotoWriteNoFrame = 9
};

enum PhotoUiKey {
    kPhotoKeyPrevious = 1,
    kPhotoKeyNext = 2,
    kPhotoKeyUp = 3,
    kPhotoKeyDown = 4,
    kPhotoKeyConfirm = 5,
    kPhotoKeyBrowser = 6,
    kPhotoKeyGallery = 7,
    kPhotoKeyCompare = 8,
    kPhotoKeyBack = 9,
    kPhotoKeyRecord = 10,
    kPhotoKeyFocus = 11,
    kPhotoKeyCapture = 12
};

class DisplayProbeWorker {
  public:
    DisplayProbeWorker(const char* buildId, int intervalMs, const char* storageRoot,
                       const char* cameraModel, const char* versionName,
                       int storageStatus = kStorageProbeReady);
    ~DisplayProbeWorker();
    bool start();
    int stop();
    void updateSurfaceInfo(int width, int height, int format);
    void updateSequenceMetrics(int state, int receivedFrames, int releasedFrames, int lastJpegBytes,
                               int64_t firstTimestampMs, int64_t lastTimestampMs);
    int submitJpeg(const unsigned char* jpeg, int length, int64_t timestampMs);
    int changeStyle(int delta);
    bool key(int key, bool down, int64_t timestampMs);
    void touch(int action, float x, float y, int64_t timestampMs);
    int requestPhoto(int64_t timestampMs);
    void setFocus(bool active);
    void configureStorage(int status, int attempts);
    bool blitLatest(void* destination, int width, int height, int stride, int format,
                    int* frameNumber);
    bool waitForFrame(int minimumFrame, int timeoutMs);
    bool waitForProcessedFrame(int minimumFrame, int timeoutMs);
    bool waitForStorageInitialization(int timeoutMs);
    bool waitForPhotoResult(int minimumResults, int timeoutMs);
    void getStats(int* frameCount, int* postCount);
    void getFilterStats(FilterProbeMetrics* metrics);

  private:
    static void* threadEntry(void* context);
    static void* photoThreadEntry(void* context);
    void run();
    void photoRun();
    void renderNextLocked();
    void changeCategoryLocked(int direction);
    void adjustParameterLocked(int direction);
    Preset adjustedPresetLocked(int presetIndex) const;
    void recordRecentLocked(int presetIndex);
    void toggleFavoriteLocked();
    bool isFavoriteLocked(int presetIndex) const;
    bool queueGalleryLoadLocked();
    bool queueGalleryDeleteLocked();
    RuntimeSettings settingsLocked() const;

    pthread_mutex_t mutex_;
    pthread_cond_t condition_;
    pthread_t thread_;
    pthread_mutex_t photoMutex_;
    pthread_cond_t photoCondition_;
    pthread_t photoThread_;
    bool running_;
    bool threadStarted_;
    bool photoRunning_;
    bool photoThreadStarted_;
    bool storageConfigured_;
    bool storageInitializationComplete_;
    int initialStorageStatus_;
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
    int intensity_;
    int parameterIndex_;
    int adjustments_[kMaxPresets][kPresetAdjustmentCount];
    int scene_;
    bool controlsVisible_;
    bool compare_;
    bool focusActive_;
    uint64_t favoritesLo_;
    uint64_t favoritesHi_;
    int recent_[kRecentPresetCount];
    int recentCount_;
    int galleryIndex_;
    int galleryLoadedIndex_;
    bool galleryHasThumbnail_;
    bool settingsDirty_;
    bool diagnostics_;
    int64_t controlsVisibleUntilMs_;
    int64_t photoMessageUntilMs_;
    float touchStartX_;
    float touchStartY_;
    int64_t touchStartMs_;
    int jpegLength_;
    int64_t jpegTimestampMs_;
    int64_t lastDecodedTimestampMs_;
    int64_t firstProcessedTimestampMs_;
    int64_t lastProcessedTimestampMs_;
    PhotoStore photoStore_;
    bool storageReady_;
    bool photoPending_;
    bool photoInUse_;
    int photoJob_;
    int photoPreset_;
    int photoIntensity_;
    int photoAdjustments_[kPresetAdjustmentCount];
    bool photoFavorite_;
    int photoGalleryIndex_;
    int64_t photoTimestampMs_;
    int photoStatus_;
    int photoSavedCount_;
    int photoFailedCount_;
    int photoEncodedBytes_;
    int galleryPhotoCount_;
    int galleryPreset_;
    unsigned char jpeg_[kFilterProbeInputCapacity];
    Pixel raw_[kFrameWidth * kFrameHeight];
    Pixel filtered_[kFrameWidth * kFrameHeight];
    Pixel previous_[kFrameWidth * kFrameHeight];
    Pixel photoFrame_[kFrameWidth * kFrameHeight];
    Pixel galleryThumbnail_[kFrameWidth * kFrameHeight];
    Pixel scaledPhoto_[kPhotoWidth * kPhotoHeight];
    unsigned char encodedPhoto_[512 * 1024];
    char buildId_[48];
    uint16_t pixels_[kDisplayProbeWidth * kDisplayProbeHeight];
};

} // namespace retrolens

#endif
