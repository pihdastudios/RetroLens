#include "photo_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>

#include "jpeg_encoder.h"

namespace retrolens {
namespace {

static bool makeDirectory(const char* path) {
    return mkdir(path, 0775) == 0 || errno == EEXIST;
}

static bool flushAndClose(FILE* file) {
    if (!file)
        return false;
    bool ok = fflush(file) == 0 && fsync(fileno(file)) == 0;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

static void trimLine(char* value) {
    if (!value)
        return;
    size_t length = strlen(value);
    while (length && (value[length - 1] == '\n' || value[length - 1] == '\r'))
        value[--length] = 0;
}

static bool atomicReplace(const char* temporaryPath, const char* finalPath) {
    if (rename(temporaryPath, finalPath) == 0)
        return true;
    unlink(temporaryPath);
    return false;
}

} // namespace

PhotoStore::PhotoStore(const char* storageRoot, const char* cameraModel, const char* versionName)
    : entryCount_(0) {
    strncpy(root_, storageRoot ? storageRoot : "/mnt/sdcard", sizeof(root_) - 1);
    root_[sizeof(root_) - 1] = 0;
    strncpy(model_, cameraModel ? cameraModel : "UNKNOWN", sizeof(model_) - 1);
    model_[sizeof(model_) - 1] = 0;
    strncpy(version_, versionName ? versionName : "1.0.0", sizeof(version_) - 1);
    version_[sizeof(version_) - 1] = 0;
    snprintf(base_, sizeof(base_), "%s/RETROLENS", root_);
    snprintf(config_, sizeof(config_), "%s/CONFIG", base_);
    snprintf(photos_, sizeof(photos_), "%s/PHOTOS", base_);
    snprintf(thumbnails_, sizeof(thumbnails_), "%s/THUMBNAILS", base_);
    snprintf(settingsPath_, sizeof(settingsPath_), "%s/settings.cfg", config_);
    snprintf(indexPath_, sizeof(indexPath_), "%s/index.txt", thumbnails_);
    memset(entries_, 0, sizeof(entries_));
}

bool PhotoStore::initialize() {
    if (!makeDirectory(base_) || !makeDirectory(config_) || !makeDirectory(photos_) ||
        !makeDirectory(thumbnails_))
        return false;
    return loadIndex();
}

bool PhotoStore::loadSettings(RuntimeSettings* settings) {
    if (!settings)
        return false;
    settings->selectedPreset = 0;
    settings->intensity = 100;
    settings->favoritesLo = 0;
    settings->favoritesHi = 0;
    settings->recentCount = 0;
    settings->motion = 2;
    settings->diagnostics = false;
    memset(settings->adjustments, 0, sizeof(settings->adjustments));
    for (int i = 0; i < kRecentPresetCount; i++)
        settings->recent[i] = 0;
    FILE* file = fopen(settingsPath_, "r");
    if (!file)
        return false;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        trimLine(line);
        if (!strncmp(line, "preset=", 7))
            settings->selectedPreset = findPreset(line + 7);
        else if (!strncmp(line, "intensity=", 10))
            settings->intensity = atoi(line + 10);
        else if (!strncmp(line, "favorites_lo=", 13))
            settings->favoritesLo = (uint64_t)strtoull(line + 13, 0, 16);
        else if (!strncmp(line, "favorites_hi=", 13))
            settings->favoritesHi = (uint64_t)strtoull(line + 13, 0, 16);
        else if (!strncmp(line, "motion=", 7))
            settings->motion = atoi(line + 7);
        else if (!strncmp(line, "diagnostics=", 12))
            settings->diagnostics = atoi(line + 12) != 0;
        else if (!strncmp(line, "recent=", 7)) {
            char* cursor = line + 7;
            while (*cursor && settings->recentCount < kRecentPresetCount) {
                char* separator = strchr(cursor, ',');
                if (separator)
                    *separator = 0;
                settings->recent[settings->recentCount++] = findPreset(cursor);
                if (!separator)
                    break;
                cursor = separator + 1;
            }
        } else if (!strncmp(line, "adjust=", 7)) {
            char preset[64];
            int contrast = 0, grain = 0, motion = 0;
            if (sscanf(line + 7, "%63[^,],%d,%d,%d", preset, &contrast, &grain, &motion) == 4) {
                int index = findPreset(preset);
                settings->adjustments[index][0] =
                    contrast < -40 ? -40 : (contrast > 40 ? 40 : contrast);
                settings->adjustments[index][1] = grain < -20 ? -20 : (grain > 20 ? 20 : grain);
                settings->adjustments[index][2] = motion < -30 ? -30 : (motion > 30 ? 30 : motion);
            }
        }
    }
    fclose(file);
    if (settings->intensity < 0 || settings->intensity > 100)
        settings->intensity = 100;
    if (settings->motion < 0 || settings->motion > 2)
        settings->motion = 2;
    return true;
}

bool PhotoStore::saveSettings(const RuntimeSettings& settings) {
    char temporary[704];
    snprintf(temporary, sizeof(temporary), "%s.tmp", settingsPath_);
    FILE* file = fopen(temporary, "w");
    if (!file)
        return false;
    fprintf(file,
            "version=2\npreset=%s\nintensity=%d\nfavorites_lo=%llx\nfavorites_hi=%llx\n"
            "motion=%d\ndiagnostics=%d\nrecent=",
            presetAt(settings.selectedPreset).id, settings.intensity,
            (unsigned long long)settings.favoritesLo, (unsigned long long)settings.favoritesHi,
            settings.motion, settings.diagnostics ? 1 : 0);
    for (int i = 0; i < settings.recentCount && i < kRecentPresetCount; i++)
        fprintf(file, "%s%s", i ? "," : "", presetAt(settings.recent[i]).id);
    fputc('\n', file);
    for (int i = 0; i < presetCount(); i++) {
        const int* values = settings.adjustments[i];
        if (values[0] || values[1] || values[2])
            fprintf(file, "adjust=%s,%d,%d,%d\n", presetAt(i).id, values[0], values[1], values[2]);
    }
    if (!flushAndClose(file)) {
        unlink(temporary);
        return false;
    }
    return atomicReplace(temporary, settingsPath_);
}

bool PhotoStore::savePhoto(const Pixel* frame, int presetIndex, int intensity,
                           const int* adjustments, bool favorite, int64_t timestampMs,
                           Pixel* scaled, unsigned char* encoded, int encodedCapacity,
                           int* encodedBytes) {
    if (!frame || !scaled || !encoded || encodedCapacity <= 0)
        return false;
    for (int y = 0; y < kPhotoHeight; y++)
        for (int x = 0; x < kPhotoWidth; x++)
            scaled[y * kPhotoWidth + x] = frame[(y * kFrameHeight / kPhotoHeight) * kFrameWidth +
                                                x * kFrameWidth / kPhotoWidth];
    size_t jpegSize = 0;
    if (!encodeJpeg(scaled, kPhotoWidth, kPhotoHeight, 88, encoded, (size_t)encodedCapacity,
                    &jpegSize))
        return false;

    time_t wall = time(0);
    struct tm local;
    localtime_r(&wall, &local);
    char stem[112];
    snprintf(stem, sizeof(stem), "%04d%02d%02d_%02d%02d%02d_%03d_%s_preview", local.tm_year + 1900,
             local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec,
             (int)(timestampMs % 1000), presetAt(presetIndex).id);
    PhotoEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.timestampMs = timestampMs;
    entry.presetIndex = presetIndex;
    entry.favorite = favorite;
    snprintf(entry.filename, sizeof(entry.filename), "%s.jpg", stem);

    char finalPath[800], temporaryPath[824];
    photoPath(finalPath, sizeof(finalPath), entry.filename);
    snprintf(temporaryPath, sizeof(temporaryPath), "%s.tmp", finalPath);
    FILE* photo = fopen(temporaryPath, "wb");
    if (!photo)
        return false;
    bool photoOk = fwrite(encoded, 1, jpegSize, photo) == jpegSize && flushAndClose(photo) &&
                   atomicReplace(temporaryPath, finalPath);
    if (!photoOk) {
        unlink(temporaryPath);
        return false;
    }

    char sidecar[800], sidecarTemporary[824];
    sidecarPath(sidecar, sizeof(sidecar), entry.filename);
    snprintf(sidecarTemporary, sizeof(sidecarTemporary), "%s.tmp", sidecar);
    FILE* manifest = fopen(sidecarTemporary, "w");
    if (!manifest) {
        unlink(finalPath);
        return false;
    }
    fputs("{\n  \"type\":\"preview-resolution derivative\",\n  \"preset\":", manifest);
    jsonEscape(manifest, presetAt(presetIndex).id);
    fprintf(manifest,
            ",\n  \"width\":%d,\n  \"height\":%d,\n  \"intensity\":%d,\n"
            "  \"contrastAdjustment\":%d,\n  \"grainAdjustment\":%d,\n"
            "  \"motionAdjustment\":%d,\n"
            "  \"sourceTimestampMs\":%lld,\n  \"favorite\":%s,\n"
            "  \"originalPreserved\":true,\n  \"originalPathKnown\":false,\n"
            "  \"cameraModel\":",
            kPhotoWidth, kPhotoHeight, intensity, adjustments ? adjustments[0] : 0,
            adjustments ? adjustments[1] : 0, adjustments ? adjustments[2] : 0,
            (long long)timestampMs, favorite ? "true" : "false");
    jsonEscape(manifest, model_);
    fputs(",\n  \"applicationVersion\":", manifest);
    jsonEscape(manifest, version_);
    fputs("\n}\n", manifest);
    if (!flushAndClose(manifest) || !atomicReplace(sidecarTemporary, sidecar)) {
        unlink(sidecarTemporary);
        unlink(finalPath);
        return false;
    }

    char thumbnail[800], thumbnailTemporary[824];
    thumbnailPath(thumbnail, sizeof(thumbnail), entry.filename);
    snprintf(thumbnailTemporary, sizeof(thumbnailTemporary), "%s.tmp", thumbnail);
    FILE* thumb = fopen(thumbnailTemporary, "wb");
    if (!thumb) {
        unlink(sidecar);
        unlink(finalPath);
        return false;
    }
    bool thumbnailOk = fwrite(frame, sizeof(Pixel), kFrameWidth * kFrameHeight, thumb) ==
                           (size_t)(kFrameWidth * kFrameHeight) &&
                       flushAndClose(thumb) && atomicReplace(thumbnailTemporary, thumbnail);
    if (!thumbnailOk) {
        unlink(thumbnailTemporary);
        unlink(sidecar);
        unlink(finalPath);
        return false;
    }

    PhotoEntry previousEntries[kMaxPhotoEntries];
    int previousCount = entryCount_;
    memcpy(previousEntries, entries_, sizeof(previousEntries));
    PhotoEntry evicted;
    bool hasEvicted = entryCount_ == kMaxPhotoEntries;
    if (hasEvicted)
        evicted = entries_[0];
    addEntry(entry);
    if (!saveIndex()) {
        memcpy(entries_, previousEntries, sizeof(entries_));
        entryCount_ = previousCount;
        unlink(thumbnail);
        unlink(sidecar);
        unlink(finalPath);
        return false;
    }
    if (hasEvicted) {
        char oldPhoto[800], oldSidecar[800], oldThumbnail[800];
        photoPath(oldPhoto, sizeof(oldPhoto), evicted.filename);
        sidecarPath(oldSidecar, sizeof(oldSidecar), evicted.filename);
        thumbnailPath(oldThumbnail, sizeof(oldThumbnail), evicted.filename);
        unlink(oldPhoto);
        unlink(oldSidecar);
        unlink(oldThumbnail);
    }
    if (encodedBytes)
        *encodedBytes = (int)jpegSize;
    return true;
}

int PhotoStore::photoCount() const {
    return entryCount_;
}

bool PhotoStore::getPhoto(int newestIndex, PhotoEntry* entry) const {
    int index = storageIndex(newestIndex);
    if (index < 0 || !entry)
        return false;
    *entry = entries_[index];
    return true;
}

bool PhotoStore::loadThumbnail(int newestIndex, Pixel* thumbnail) const {
    PhotoEntry entry;
    if (!thumbnail || !getPhoto(newestIndex, &entry))
        return false;
    char path[800];
    thumbnailPath(path, sizeof(path), entry.filename);
    FILE* file = fopen(path, "rb");
    if (!file)
        return false;
    bool ok = fread(thumbnail, sizeof(Pixel), kFrameWidth * kFrameHeight, file) ==
              (size_t)(kFrameWidth * kFrameHeight);
    fclose(file);
    return ok;
}

bool PhotoStore::deletePhoto(int newestIndex) {
    int index = storageIndex(newestIndex);
    if (index < 0)
        return false;
    char photo[800], sidecar[800], thumbnail[800];
    photoPath(photo, sizeof(photo), entries_[index].filename);
    sidecarPath(sidecar, sizeof(sidecar), entries_[index].filename);
    thumbnailPath(thumbnail, sizeof(thumbnail), entries_[index].filename);
    PhotoEntry previousEntries[kMaxPhotoEntries];
    int previousCount = entryCount_;
    memcpy(previousEntries, entries_, sizeof(previousEntries));
    for (int i = index; i + 1 < entryCount_; i++)
        entries_[i] = entries_[i + 1];
    entryCount_--;
    if (!saveIndex()) {
        memcpy(entries_, previousEntries, sizeof(entries_));
        entryCount_ = previousCount;
        return false;
    }
    bool removed = unlink(photo) == 0 || errno == ENOENT;
    unlink(sidecar);
    unlink(thumbnail);
    return removed;
}

int64_t PhotoStore::freeBytes() const {
    struct statfs values;
    if (statfs(base_, &values) != 0)
        return -1;
    return (int64_t)values.f_bavail * values.f_bsize;
}

const char* PhotoStore::basePath() const {
    return base_;
}

bool PhotoStore::loadIndex() {
    entryCount_ = 0;
    FILE* file = fopen(indexPath_, "r");
    if (!file)
        return true;
    char line[320];
    while (entryCount_ < kMaxPhotoEntries && fgets(line, sizeof(line), file)) {
        trimLine(line);
        char* first = strchr(line, '|');
        if (!first)
            continue;
        *first++ = 0;
        char* second = strchr(first, '|');
        if (!second)
            continue;
        *second++ = 0;
        char* third = strchr(second, '|');
        if (!third)
            continue;
        *third++ = 0;
        PhotoEntry& entry = entries_[entryCount_];
        memset(&entry, 0, sizeof(entry));
        entry.timestampMs = (int64_t)strtoll(line, 0, 10);
        strncpy(entry.filename, first, sizeof(entry.filename) - 1);
        entry.presetIndex = findPreset(second);
        entry.favorite = atoi(third) != 0;
        entryCount_++;
    }
    fclose(file);
    return true;
}

bool PhotoStore::saveIndex() {
    char temporary[704];
    snprintf(temporary, sizeof(temporary), "%s.tmp", indexPath_);
    FILE* file = fopen(temporary, "w");
    if (!file)
        return false;
    for (int i = 0; i < entryCount_; i++)
        fprintf(file, "%lld|%s|%s|%d\n", (long long)entries_[i].timestampMs, entries_[i].filename,
                presetAt(entries_[i].presetIndex).id, entries_[i].favorite ? 1 : 0);
    if (!flushAndClose(file)) {
        unlink(temporary);
        return false;
    }
    return atomicReplace(temporary, indexPath_);
}

void PhotoStore::addEntry(const PhotoEntry& entry) {
    if (entryCount_ == kMaxPhotoEntries) {
        for (int i = 0; i + 1 < entryCount_; i++)
            entries_[i] = entries_[i + 1];
        entryCount_--;
    }
    entries_[entryCount_++] = entry;
}

int PhotoStore::storageIndex(int newestIndex) const {
    if (newestIndex < 0 || newestIndex >= entryCount_)
        return -1;
    return entryCount_ - 1 - newestIndex;
}

void PhotoStore::photoPath(char* output, int capacity, const char* filename) const {
    snprintf(output, (size_t)capacity, "%s/%s", photos_, filename);
}

void PhotoStore::sidecarPath(char* output, int capacity, const char* filename) const {
    char stem[128];
    strncpy(stem, filename, sizeof(stem) - 1);
    stem[sizeof(stem) - 1] = 0;
    char* extension = strrchr(stem, '.');
    if (extension)
        *extension = 0;
    snprintf(output, (size_t)capacity, "%s/%s.json", photos_, stem);
}

void PhotoStore::thumbnailPath(char* output, int capacity, const char* filename) const {
    char stem[128];
    strncpy(stem, filename, sizeof(stem) - 1);
    stem[sizeof(stem) - 1] = 0;
    char* extension = strrchr(stem, '.');
    if (extension)
        *extension = 0;
    snprintf(output, (size_t)capacity, "%s/%s.rgb", thumbnails_, stem);
}

} // namespace retrolens
