#ifndef RETROLENS_PHOTO_STORE_H
#define RETROLENS_PHOTO_STORE_H

#include <stdint.h>

#include "retrolens_core.h"

namespace retrolens {

static const int kPhotoWidth = 320;
static const int kPhotoHeight = 240;
static const int kMaxPhotoEntries = 48;
static const int kRecentPresetCount = 12;
static const int kPresetAdjustmentCount = 3;

enum PhotoStorageState {
    kPhotoStorageNotConfigured = 0,
    kPhotoStorageReady = 1,
    kPhotoStorageInvalidRoot = 2,
    kPhotoStorageDirectoryFailed = 3,
    kPhotoStorageInsufficientSpace = 4,
    kPhotoStorageIndexFailed = 5,
    kPhotoStorageWriteProbeFailed = 6,
    kPhotoStorageInitializing = 7
};

enum PhotoWriteStage {
    kPhotoStageNone = 0,
    kPhotoStageEncode = 1,
    kPhotoStageJpeg = 2,
    kPhotoStageSidecar = 3,
    kPhotoStageThumbnail = 4,
    kPhotoStageIndex = 5,
    kPhotoStageComplete = 6
};

struct RuntimeSettings {
    int selectedPreset;
    int intensity;
    uint64_t favoritesLo;
    uint64_t favoritesHi;
    int recent[kRecentPresetCount];
    int adjustments[kMaxPresets][kPresetAdjustmentCount];
    int recentCount;
    int motion;
    bool diagnostics;
};

struct PhotoEntry {
    int64_t timestampMs;
    int presetIndex;
    bool favorite;
    char filename[128];
};

class PhotoStore {
  public:
    PhotoStore(const char* storageRoot, const char* cameraModel, const char* versionName);
    PhotoStorageState initialize();
    bool loadSettings(RuntimeSettings* settings);
    bool saveSettings(const RuntimeSettings& settings);
    bool savePhoto(const Pixel* frame, int presetIndex, int intensity, const int* adjustments,
                   bool favorite, int64_t timestampMs, Pixel* scaled, unsigned char* encoded,
                   int encodedCapacity, int* encodedBytes);
    int photoCount() const;
    bool getPhoto(int newestIndex, PhotoEntry* entry) const;
    bool loadThumbnail(int newestIndex, Pixel* thumbnail) const;
    bool deletePhoto(int newestIndex);
    int64_t freeBytes() const;
    const char* basePath() const;
    PhotoStorageState storageState() const;
    PhotoWriteStage writeStage() const;
    int lastError() const;

  private:
    bool loadIndex(bool* needsRebuild);
    bool rebuildIndex();
    void cleanTemporaryFiles(const char* directory);
    bool validateEntry(const PhotoEntry& entry) const;
    bool saveIndex();
    void addEntry(const PhotoEntry& entry);
    int storageIndex(int newestIndex) const;
    void photoPath(char* output, int capacity, const char* filename) const;
    void sidecarPath(char* output, int capacity, const char* filename) const;
    void thumbnailPath(char* output, int capacity, const char* filename) const;

    char root_[512];
    char base_[576];
    char config_[640];
    char photos_[640];
    char thumbnails_[640];
    char settingsPath_[680];
    char indexPath_[680];
    char model_[64];
    char version_[32];
    PhotoEntry entries_[kMaxPhotoEntries];
    int entryCount_;
    PhotoStorageState storageState_;
    PhotoWriteStage writeStage_;
    int lastError_;
};

} // namespace retrolens

#endif
