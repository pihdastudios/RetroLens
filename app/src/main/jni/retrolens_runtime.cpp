#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "jpeg_encoder.h"
#include "retrolens_core.h"
#include "third_party/picojpeg/picojpeg.h"

using retrolens::Pixel;
using retrolens::Preset;

namespace {

static const int kInputCapacity = 256 * 1024;
static const int kEncodedCapacity = 512 * 1024;
static const int kSourceGridWidth = 80;
static const int kSourceGridHeight = 60;
static const int kSurfaceWidth = 640;
static const int kSurfaceHeight = 480;

enum Scene { SCENE_CAMERA = 0, SCENE_DRAWER = 1, SCENE_BROWSER = 2, SCENE_GALLERY = 3 };
enum RecorderJob { JOB_NONE = 0, JOB_SNAPSHOT = 1, JOB_VIDEO = 2 };

struct JpegInput {
    const unsigned char* data;
    size_t length;
    size_t offset;
};

static unsigned char readJpeg(unsigned char* destination, unsigned char requested,
                              unsigned char* actual, void* context) {
    JpegInput* input = static_cast<JpegInput*>(context);
    if (!input || !destination || !actual)
        return PJPG_STREAM_READ_ERROR;
    size_t remaining = input->offset < input->length ? input->length - input->offset : 0;
    size_t count = remaining < requested ? remaining : requested;
    if (count)
        memcpy(destination, input->data + input->offset, count);
    input->offset += count;
    *actual = (unsigned char)count;
    return 0;
}

static int mcuOffset(int localX, int localY) {
    return (localY / 8) * 128 + (localX / 8) * 64;
}

static int64_t monotonicMs() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void makeDirectory(const char* path) {
    if (mkdir(path, 0775) != 0 && errno != EEXIST) {
        __android_log_print(ANDROID_LOG_ERROR, "RetroLens", "mkdir failed path=%s errno=%d", path,
                            errno);
    }
}

static uint16_t rgb565(int r, int g, int b) {
    if (r < 0)
        r = 0;
    if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    if (b > 255)
        b = 255;
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static uint16_t blend565(uint16_t base, uint16_t over, int alpha) {
    int br = ((base >> 11) & 31) * 255 / 31, bg = ((base >> 5) & 63) * 255 / 63,
        bb = (base & 31) * 255 / 31;
    int orr = ((over >> 11) & 31) * 255 / 31, og = ((over >> 5) & 63) * 255 / 63,
        ob = (over & 31) * 255 / 31;
    return rgb565((br * (100 - alpha) + orr * alpha) / 100, (bg * (100 - alpha) + og * alpha) / 100,
                  (bb * (100 - alpha) + ob * alpha) / 100);
}

static const unsigned char kGlyphs[36][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {31, 4, 4, 4, 4, 4, 31},
    {7, 2, 2, 2, 18, 18, 12},     {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},     {14, 17, 19, 21, 25, 17, 14},
    {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 30, 1, 1, 17, 14},   {6, 8, 16, 30, 17, 17, 14},
    {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12}};

static const unsigned char* glyph(char c) {
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return kGlyphs[c - 'A'];
    if (c >= '0' && c <= '9')
        return kGlyphs[26 + c - '0'];
    return 0;
}

class Runtime {
  public:
    Runtime(const char* root, const char* model, const char* version)
        : window_(0), running_(true), valid_(true), threadStarted_(false),
          recorderThreadStarted_(false), framePending_(false), frameInUse_(false), jpegLength_(0),
          frameTimestamp_(0), hasFrame_(false), hasPrevious_(false), selected_(0), intensity_(100),
          scene_(SCENE_CAMERA), controlsVisible_(true), compare_(false), touchActive_(false),
          favoriteMaskLo_(0), favoriteMaskHi_(0), frameCount_(0), droppedFrames_(0), decodeMs_(0),
          filterMs_(0), renderMs_(0), fps_(0), fpsFrames_(0), fpsStart_(monotonicMs()),
          startMs_(monotonicMs()), lastFrameMs_(0), lastTouchMs_(0), touchDownMs_(0),
          touchStartX_(0), touchStartY_(0), error_(false), captureFlashUntil_(0),
          saveRequest_(false), saveTimestamp_(0), savePreset_(0), saveIntensity_(100),
          recThreadRunning_(true), recCommand_(0), recPending_(false), recJob_(JOB_NONE),
          recPreset_(0), recIntensity_(100), recording_(false), recordingInterrupted_(false),
          recordStartMs_(0), recordEndMs_(0), lastRecordFrameMs_(0), recordPreset_(0),
          recordIntensity_(100), recordFrames_(0), recordDropped_(0), settingsDirty_(false),
          logFile_(0) {
        pthread_mutex_init(&mutex_, 0);
        pthread_cond_init(&condition_, 0);
        pthread_mutex_init(&recMutex_, 0);
        pthread_cond_init(&recCondition_, 0);
        strncpy(root_, root ? root : "/mnt/sdcard", sizeof(root_) - 1);
        root_[sizeof(root_) - 1] = 0;
        strncpy(model_, model ? model : "UNKNOWN", sizeof(model_) - 1);
        model_[sizeof(model_) - 1] = 0;
        strncpy(version_, version ? version : "1.0.0", sizeof(version_) - 1);
        version_[sizeof(version_) - 1] = 0;
        setupPaths();
        input_ = (unsigned char*)malloc(kInputCapacity);
        raw_ = (Pixel*)malloc(retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
        output_ = (Pixel*)malloc(retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
        scratch_ = (Pixel*)malloc(retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
        previous_ =
            (Pixel*)malloc(retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
        recorderFrame_ =
            (Pixel*)malloc(retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
        encoded_ = (unsigned char*)malloc(kEncodedCapacity);
        if (!input_ || !raw_ || !output_ || !scratch_ || !previous_ || !recorderFrame_ ||
            !encoded_) {
            valid_ = false;
            running_ = false;
            error_ = true;
            strncpy(errorText_, "Native memory allocation failed", sizeof(errorText_) - 1);
            errorText_[sizeof(errorText_) - 1] = 0;
            log("runtime allocation failed");
            return;
        }
        memset(raw_, 0, retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
        memset(output_, 0, retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
        loadSettings();
        log("runtime create version=%s model=%s presets=%d allocations=%lu", version_, model_,
            retrolens::presetCount(),
            (unsigned long)(kInputCapacity + kEncodedCapacity +
                            5 * retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel)));
        if (pthread_create(&recorderThread_, 0, recorderEntry, this) == 0)
            recorderThreadStarted_ = true;
        if (pthread_create(&thread_, 0, processEntry, this) == 0)
            threadStarted_ = true;
    }

    ~Runtime() {
        pthread_mutex_lock(&mutex_);
        running_ = false;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
        if (threadStarted_)
            pthread_join(thread_, 0);
        pthread_mutex_lock(&recMutex_);
        recThreadRunning_ = false;
        pthread_cond_broadcast(&recCondition_);
        pthread_mutex_unlock(&recMutex_);
        if (recorderThreadStarted_)
            pthread_join(recorderThread_, 0);
        clearSurface();
        saveSettingsNow();
        log("runtime destroyed frames=%d dropped=%d", frameCount_, droppedFrames_);
        if (logFile_)
            fclose(logFile_);
        free(input_);
        free(raw_);
        free(output_);
        free(scratch_);
        free(previous_);
        free(recorderFrame_);
        free(encoded_);
        pthread_cond_destroy(&condition_);
        pthread_mutex_destroy(&mutex_);
        pthread_cond_destroy(&recCondition_);
        pthread_mutex_destroy(&recMutex_);
    }

    bool valid() const {
        return valid_;
    }

    void setSurface(ANativeWindow* window) {
        pthread_mutex_lock(&mutex_);
        if (window_)
            ANativeWindow_release(window_);
        window_ = window;
        if (window_)
            ANativeWindow_setBuffersGeometry(window_, kSurfaceWidth, kSurfaceHeight,
                                             WINDOW_FORMAT_RGB_565);
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
        log("surface attached");
    }
    void clearSurface() {
        pthread_mutex_lock(&mutex_);
        ANativeWindow* old = window_;
        window_ = 0;
        pthread_mutex_unlock(&mutex_);
        if (old)
            ANativeWindow_release(old);
    }
    bool submit(const unsigned char* data, int length, int64_t timestamp) {
        if (!data || length <= 0 || length > kInputCapacity)
            return false;
        pthread_mutex_lock(&mutex_);
        if (!running_ || framePending_ || frameInUse_) {
            droppedFrames_++;
            pthread_mutex_unlock(&mutex_);
            return false;
        }
        memcpy(input_, data, (size_t)length);
        jpegLength_ = length;
        frameTimestamp_ = timestamp;
        framePending_ = true;
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
        return true;
    }
    void setError(const char* value) {
        pthread_mutex_lock(&mutex_);
        error_ = true;
        strncpy(errorText_, value ? value : "Analytical live view unavailable",
                sizeof(errorText_) - 1);
        errorText_[sizeof(errorText_) - 1] = 0;
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
    }
    void key(int key, bool down, int64_t now) {
        if (key == 8) {
            compare_ = down;
            return;
        }
        if (!down)
            return;
        pthread_mutex_lock(&mutex_);
        if (key == 1)
            changePreset(-1);
        else if (key == 2)
            changePreset(1);
        else if (key == 3) {
            if (scene_ == SCENE_DRAWER) {
                intensity_ += 5;
                if (intensity_ > 100)
                    intensity_ = 100;
            } else
                changeCategory(-1);
        } else if (key == 4) {
            if (scene_ == SCENE_DRAWER) {
                intensity_ -= 5;
                if (intensity_ < 0)
                    intensity_ = 0;
            } else
                changeCategory(1);
        } else if (key == 5)
            scene_ = scene_ == SCENE_DRAWER ? SCENE_CAMERA : SCENE_DRAWER;
        else if (key == 6)
            scene_ = scene_ == SCENE_BROWSER ? SCENE_CAMERA : SCENE_BROWSER;
        else if (key == 7)
            scene_ = scene_ == SCENE_GALLERY ? SCENE_CAMERA : SCENE_GALLERY;
        else if (key == 9)
            scene_ = SCENE_CAMERA;
        settingsDirty_ = true;
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
        (void)now;
    }
    void touch(int action, float x, float y, int64_t now) {
        pthread_mutex_lock(&mutex_);
        if (action == 0) {
            touchStartX_ = x;
            touchStartY_ = y;
            touchDownMs_ = now;
            touchActive_ = true;
        } else if (action == 2 && scene_ == SCENE_DRAWER) {
            intensity_ = (int)(x * 100.0f / 640.0f);
            if (intensity_ < 0)
                intensity_ = 0;
            if (intensity_ > 100)
                intensity_ = 100;
        } else if (action == 1) {
            float dx = x - touchStartX_;
            if (now - touchDownMs_ > 450) {
                compare_ = false;
            } else if (dx > 70)
                changePreset(-1);
            else if (dx < -70)
                changePreset(1);
            else if (y > 370) {
                if (x < 220)
                    changePreset(-1);
                else if (x > 420)
                    changePreset(1);
                else
                    scene_ = scene_ == SCENE_DRAWER ? SCENE_CAMERA : SCENE_DRAWER;
            } else if (now - lastTouchMs_ < 330) {
                toggleFavorite();
                lastTouchMs_ = 0;
            } else {
                controlsVisible_ = !controlsVisible_;
                lastTouchMs_ = now;
            }
            touchActive_ = false;
        }
        settingsDirty_ = true;
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
    }
    void captureRequested(int64_t now) {
        captureFlashUntil_ = now + 180;
    }
    void saveSnapshot(int64_t timestamp) {
        pthread_mutex_lock(&mutex_);
        saveRequest_ = true;
        saveTimestamp_ = timestamp;
        savePreset_ = selected_;
        saveIntensity_ = intensity_;
        pthread_cond_signal(&condition_);
        pthread_mutex_unlock(&mutex_);
    }
    void toggleRecording(int64_t timestamp) {
        pthread_mutex_lock(&recMutex_);
        recCommand_ = recording_ ? 2 : 1;
        recordingInterrupted_ = false;
        recordStartMs_ = timestamp;
        pthread_cond_signal(&recCondition_);
        pthread_mutex_unlock(&recMutex_);
    }
    void stopRecording(bool interrupted, int64_t timestamp) {
        (void)timestamp;
        pthread_mutex_lock(&recMutex_);
        if (recording_ || recCommand_ == 1) {
            recCommand_ = 2;
            recordingInterrupted_ = interrupted;
            pthread_cond_signal(&recCondition_);
        }
        pthread_mutex_unlock(&recMutex_);
    }

  private:
    ANativeWindow* window_;
    pthread_t thread_;
    pthread_t recorderThread_;
    bool running_, valid_, threadStarted_, recorderThreadStarted_;
    pthread_mutex_t mutex_;
    pthread_cond_t condition_;
    unsigned char* input_;
    bool framePending_, frameInUse_;
    int jpegLength_;
    int64_t frameTimestamp_;
    Pixel *raw_, *output_, *scratch_, *previous_;
    bool hasFrame_, hasPrevious_;
    int selected_, intensity_, scene_;
    bool controlsVisible_, compare_, touchActive_;
    uint64_t favoriteMaskLo_, favoriteMaskHi_;
    int frameCount_, droppedFrames_, decodeMs_, filterMs_, renderMs_, fps_, fpsFrames_;
    int64_t fpsStart_, startMs_, lastFrameMs_, lastTouchMs_, touchDownMs_;
    float touchStartX_, touchStartY_;
    bool error_;
    char errorText_[192];
    int64_t captureFlashUntil_;
    bool saveRequest_;
    int64_t saveTimestamp_;
    int savePreset_, saveIntensity_;
    pthread_mutex_t recMutex_;
    pthread_cond_t recCondition_;
    bool recThreadRunning_;
    int recCommand_;
    bool recPending_;
    int recJob_;
    int recPreset_, recIntensity_;
    Pixel* recorderFrame_;
    unsigned char* encoded_;
    bool recording_, recordingInterrupted_;
    int64_t recordStartMs_, recordEndMs_, lastRecordFrameMs_;
    int recordPreset_, recordIntensity_;
    int recordFrames_, recordDropped_;
    retrolens::AviWriter avi_;
    char tempPath_[640], finalPath_[640], manifestPath_[640];
    bool settingsDirty_;
    char root_[512], base_[576], model_[64], version_[32], logPath_[640], settingsPath_[640];
    FILE* logFile_;

    static void* processEntry(void* value) {
        static_cast<Runtime*>(value)->processLoop();
        return 0;
    }
    static void* recorderEntry(void* value) {
        static_cast<Runtime*>(value)->recorderLoop();
        return 0;
    }

    void log(const char* format, ...) {
        char text[768];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        __android_log_print(ANDROID_LOG_INFO, "RetroLens", "%s", text);
        if (!logFile_)
            logFile_ = fopen(logPath_, "a");
        if (logFile_) {
            fprintf(logFile_, "[%lld] %s\n", (long long)monotonicMs(), text);
            fflush(logFile_);
        }
    }
    void setupPaths() {
        snprintf(base_, sizeof(base_), "%s/RETROLENS", root_);
        makeDirectory(base_);
        const char* names[] = {"CONFIG", "PRESETS", "PHOTOS", "CLIPS", "THUMBNAILS"};
        for (int i = 0; i < 5; i++) {
            char p[640];
            snprintf(p, sizeof(p), "%s/%s", base_, names[i]);
            makeDirectory(p);
        }
        snprintf(logPath_, sizeof(logPath_), "%s/LOG.TXT", base_);
        snprintf(settingsPath_, sizeof(settingsPath_), "%s/CONFIG/settings.cfg", base_);
    }
    void loadSettings() {
        FILE* f = fopen(settingsPath_, "r");
        if (!f)
            return;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "preset=", 7))
                selected_ = retrolens::findPreset(line + 7);
            else if (!strncmp(line, "intensity=", 10))
                intensity_ = atoi(line + 10);
            else if (!strncmp(line, "favorites_lo=", 13))
                favoriteMaskLo_ = (uint64_t)strtoull(line + 13, 0, 16);
            else if (!strncmp(line, "favorites_hi=", 13))
                favoriteMaskHi_ = (uint64_t)strtoull(line + 13, 0, 16);
            char* nl = strchr(line, '\n');
            if (nl)
                *nl = 0;
        }
        fclose(f);
        if (intensity_ < 0 || intensity_ > 100)
            intensity_ = 100;
    }
    void saveSettingsNow() {
        char tmp[680];
        snprintf(tmp, sizeof(tmp), "%s.tmp", settingsPath_);
        FILE* f = fopen(tmp, "w");
        if (!f)
            return;
        fprintf(f,
                "version=1\npreset=%s\nintensity=%d\nfavorites_lo=%llx\nfavorites_"
                "hi=%llx\nmotion=full\nquality=performance\nrecord_fps=10\n",
                retrolens::presetAt(selected_).id, intensity_, (unsigned long long)favoriteMaskLo_,
                (unsigned long long)favoriteMaskHi_);
        if (fflush(f) == 0) {
            fsync(fileno(f));
            fclose(f);
            rename(tmp, settingsPath_);
        } else
            fclose(f);
        settingsDirty_ = false;
    }
    void changePreset(int delta) {
        if (recording_) {
            log("preset change ignored while Retro Clip is recording");
            return;
        }
        selected_ = (selected_ + delta + retrolens::presetCount()) % retrolens::presetCount();
        hasPrevious_ = false;
        log("preset selected id=%s tier=%d", retrolens::presetAt(selected_).id,
            retrolens::presetAt(selected_).tier);
    }
    void changeCategory(int direction) {
        const char* cat = retrolens::presetAt(selected_).category;
        int i = selected_;
        do {
            i = (i + direction + retrolens::presetCount()) % retrolens::presetCount();
        } while (!strcmp(cat, retrolens::presetAt(i).category) && i != selected_);
        selected_ = i;
        hasPrevious_ = false;
    }
    void toggleFavorite() {
        if (selected_ < 64)
            favoriteMaskLo_ ^= ((uint64_t)1 << selected_);
        else
            favoriteMaskHi_ ^= ((uint64_t)1 << (selected_ - 64));
    }
    bool isFavorite() const {
        return selected_ < 64 ? (favoriteMaskLo_ & ((uint64_t)1 << selected_)) != 0
                              : (favoriteMaskHi_ & ((uint64_t)1 << (selected_ - 64))) != 0;
    }

    bool decodeReduced(const unsigned char* jpeg, int length) {
        JpegInput input = {jpeg, (size_t)length, 0};
        pjpeg_image_info_t info;
        unsigned char status = pjpeg_decode_init(&info, readJpeg, &input, 1);
        if (status || info.m_width != 640 || info.m_height != 480) {
            log("JPEG decode init failed status=%u dimensions=%dx%d", (unsigned)status,
                info.m_width, info.m_height);
            return false;
        }
        for (int my = 0; my < info.m_MCUSPerCol; my++)
            for (int mx = 0; mx < info.m_MCUSPerRow; mx++) {
                status = pjpeg_decode_mcu();
                if (status) {
                    log("JPEG MCU failed status=%u", (unsigned)status);
                    return false;
                }
                int left = mx * info.m_MCUWidth, top = my * info.m_MCUHeight,
                    right = left + info.m_MCUWidth, bottom = top + info.m_MCUHeight;
                if (right > info.m_width)
                    right = info.m_width;
                if (bottom > info.m_height)
                    bottom = info.m_height;
                int ol = left * retrolens::kFrameWidth / info.m_width,
                    ot = top * retrolens::kFrameHeight / info.m_height;
                int orr = (right * retrolens::kFrameWidth + info.m_width - 1) / info.m_width,
                    ob = (bottom * retrolens::kFrameHeight + info.m_height - 1) / info.m_height;
                for (int y = ot; y < ob; y++) {
                    int sy = y * info.m_height / retrolens::kFrameHeight;
                    int ly = sy - top;
                    for (int x = ol; x < orr; x++) {
                        int sx = x * info.m_width / retrolens::kFrameWidth;
                        int lx = sx - left;
                        int off = mcuOffset(lx, ly);
                        Pixel& p = raw_[y * retrolens::kFrameWidth + x];
                        p.r = info.m_pMCUBufR[off];
                        p.g = info.m_pMCUBufG[off];
                        p.b = info.m_pMCUBufB[off];
                    }
                }
            }
        return true;
    }
    void processLoop() {
        while (true) {
            pthread_mutex_lock(&mutex_);
            if (running_ && !framePending_) {
                struct timespec t;
                clock_gettime(CLOCK_REALTIME, &t);
                t.tv_nsec += 33000000;
                if (t.tv_nsec >= 1000000000) {
                    t.tv_sec++;
                    t.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(&condition_, &mutex_, &t);
            }
            if (!running_) {
                pthread_mutex_unlock(&mutex_);
                break;
            }
            bool frame = framePending_;
            int length = jpegLength_;
            int64_t timestamp = frameTimestamp_;
            if (frame) {
                framePending_ = false;
                frameInUse_ = true;
            }
            pthread_mutex_unlock(&mutex_);
            if (frame) {
                int64_t begin = monotonicMs();
                bool decoded = decodeReduced(input_, length);
                int64_t decodedAt = monotonicMs();
                if (decoded) {
                    const Preset& p = retrolens::presetAt(selected_);
                    retrolens::processFrame(raw_, output_, scratch_, hasPrevious_ ? previous_ : 0,
                                            retrolens::kFrameWidth, retrolens::kFrameHeight, p,
                                            intensity_, 0x52455452U, timestamp);
                    memcpy(previous_, output_,
                           retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
                    hasPrevious_ = true;
                    hasFrame_ = true;
                    error_ = false;
                    lastFrameMs_ = timestamp;
                    frameCount_++;
                    fpsFrames_++;
                    int64_t now = monotonicMs();
                    decodeMs_ = (int)(decodedAt - begin);
                    filterMs_ = (int)(now - decodedAt);
                    if (now - fpsStart_ >= 3000) {
                        fps_ = (int)(fpsFrames_ * 1000 / (now - fpsStart_));
                        log("performance processedFps=%d decodeMs=%d filterMs=%d "
                            "renderMs=%d jpegBytes=%d dropped=%d",
                            fps_, decodeMs_, filterMs_, renderMs_, length, droppedFrames_);
                        fpsFrames_ = 0;
                        fpsStart_ = now;
                    }
                }
                pthread_mutex_lock(&mutex_);
                frameInUse_ = false;
                pthread_mutex_unlock(&mutex_);
            }
            queueOutputJobs(timestamp);
            int64_t rb = monotonicMs();
            render();
            renderMs_ = (int)(monotonicMs() - rb);
        }
    }
    void queueOutputJobs(int64_t timestamp) {
        bool snapshot = false;
        pthread_mutex_lock(&mutex_);
        if (saveRequest_ && hasFrame_) {
            snapshot = true;
            saveRequest_ = false;
            timestamp = saveTimestamp_;
        }
        pthread_mutex_unlock(&mutex_);
        pthread_mutex_lock(&recMutex_);
        int job = JOB_NONE;
        if (snapshot)
            job = JOB_SNAPSHOT;
        else if (recording_ && hasFrame_ && timestamp - lastRecordFrameMs_ >= 100)
            job = JOB_VIDEO;
        if (job != JOB_NONE) {
            if (!recPending_) {
                memcpy(recorderFrame_, output_,
                       retrolens::kFrameWidth * retrolens::kFrameHeight * sizeof(Pixel));
                recPending_ = true;
                recJob_ = job;
                recPreset_ = snapshot ? savePreset_ : recordPreset_;
                recIntensity_ = snapshot ? saveIntensity_ : recordIntensity_;
                saveTimestamp_ = timestamp;
                if (job == JOB_VIDEO)
                    lastRecordFrameMs_ = timestamp;
                pthread_cond_signal(&recCondition_);
            } else if (job == JOB_VIDEO)
                recordDropped_++;
        }
        pthread_mutex_unlock(&recMutex_);
    }

    void recorderLoop() {
        while (true) {
            pthread_mutex_lock(&recMutex_);
            while (recThreadRunning_ && !recCommand_ && !recPending_ && !settingsDirty_)
                pthread_cond_wait(&recCondition_, &recMutex_);
            if (!recThreadRunning_) {
                bool finish = recording_;
                pthread_mutex_unlock(&recMutex_);
                if (finish)
                    finishRecording(true);
                break;
            }
            int command = recCommand_;
            recCommand_ = 0;
            int job = recPending_ ? recJob_ : JOB_NONE;
            pthread_mutex_unlock(&recMutex_);
            if (command == 1)
                startRecording();
            else if (command == 2)
                finishRecording(recordingInterrupted_);
            if (job != JOB_NONE)
                writeJob(job);
            if (settingsDirty_)
                saveSettingsNow();
        }
    }
    void timestampName(char* out, size_t size, int64_t stamp) {
        time_t value = time(0);
        struct tm local;
        localtime_r(&value, &local);
        snprintf(out, size, "%04d%02d%02d_%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1,
                 local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
        (void)stamp;
    }
    void startRecording() {
        char name[64];
        timestampName(name, sizeof(name), recordStartMs_);
        recordPreset_ = selected_;
        recordIntensity_ = intensity_;
        const Preset& p = retrolens::presetAt(recordPreset_);
        snprintf(tempPath_, sizeof(tempPath_), "%s/CLIPS/%s_%s.avi.tmp", base_, name, p.id);
        snprintf(finalPath_, sizeof(finalPath_), "%s/CLIPS/%s_%s.avi", base_, name, p.id);
        snprintf(manifestPath_, sizeof(manifestPath_), "%s/CLIPS/%s_%s.json", base_, name, p.id);
        recording_ = avi_.open(tempPath_, retrolens::kFrameWidth, retrolens::kFrameHeight, 10);
        recordFrames_ = recordDropped_ = 0;
        lastRecordFrameMs_ = 0;
        log("Retro Clip start path=%s result=%d resolution=320x240 fps=10 "
            "audio=none",
            tempPath_, recording_ ? 1 : 0);
    }
    void finishRecording(bool interrupted) {
        if (!recording_)
            return;
        recordEndMs_ = monotonicMs();
        bool ok = avi_.finish();
        recordFrames_ = avi_.frameCount();
        char target[680];
        if (interrupted)
            snprintf(target, sizeof(target), "%s.incomplete", finalPath_);
        else
            snprintf(target, sizeof(target), "%s", finalPath_);
        if (ok)
            rename(tempPath_, target);
        FILE* f = fopen(manifestPath_, "w");
        if (f) {
            fputs("{\n  \"version\":1,\n  \"preset\":", f);
            retrolens::jsonEscape(f, retrolens::presetAt(recordPreset_).id);
            fprintf(f,
                    ",\n  \"width\":320,\n  \"height\":240,\n  \"nominalFps\":10,\n  "
                    "\"frames\":%d,\n  \"droppedFrames\":%d,\n  \"audio\":false,\n  "
                    "\"interrupted\":%s,\n  \"intensity\":%d,\n  \"startMonotonicMs\":%lld,\n  "
                    "\"endMonotonicMs\":%lld,\n  \"cameraModel\":",
                    recordFrames_, recordDropped_, interrupted ? "true" : "false", recordIntensity_,
                    (long long)recordStartMs_, (long long)recordEndMs_);
            retrolens::jsonEscape(f, model_);
            fputs(",\n  \"applicationVersion\":", f);
            retrolens::jsonEscape(f, version_);
            fputs("\n}\n", f);
            fclose(f);
        }
        recording_ = false;
        log("Retro Clip stop path=%s frames=%d dropped=%d finalized=%d "
            "interrupted=%d",
            target, recordFrames_, recordDropped_, ok ? 1 : 0, interrupted ? 1 : 0);
    }
    void writeJob(int job) {
        size_t size = 0;
        bool stopAfterJob = false;
        bool ok =
            retrolens::encodeJpeg(recorderFrame_, retrolens::kFrameWidth, retrolens::kFrameHeight,
                                  82, encoded_, kEncodedCapacity, &size);
        if (ok && job == JOB_VIDEO) {
            ok = avi_.addFrame(encoded_, size);
            if (ok)
                recordFrames_++;
            else {
                recordDropped_++;
                stopAfterJob = true;
            }
        } else if (ok && job == JOB_SNAPSHOT) {
            char name[64];
            timestampName(name, sizeof(name), saveTimestamp_);
            const Preset& p = retrolens::presetAt(recPreset_);
            char tmp[680], path[680], side[680];
            snprintf(tmp, sizeof(tmp), "%s/PHOTOS/%s_%s_preview.jpg.tmp", base_, name, p.id);
            snprintf(path, sizeof(path), "%s/PHOTOS/%s_%s_preview.jpg", base_, name, p.id);
            snprintf(side, sizeof(side), "%s/PHOTOS/%s_%s_preview.json", base_, name, p.id);
            FILE* f = fopen(tmp, "wb");
            if (f && fwrite(encoded_, 1, size, f) == size && fflush(f) == 0) {
                fsync(fileno(f));
                fclose(f);
                rename(tmp, path);
                FILE* j = fopen(side, "w");
                if (j) {
                    fputs("{\n  \"type\":\"preview-resolution derivative\",\n  \"preset\":", j);
                    retrolens::jsonEscape(j, p.id);
                    fprintf(j,
                            ",\n  \"width\":320,\n  \"height\":240,\n  \"intensity\":%d,\n  "
                            "\"sourceTimestampMs\":%lld,\n  "
                            "\"originalPreserved\":true,\n  \"cameraModel\":",
                            recIntensity_, (long long)saveTimestamp_);
                    retrolens::jsonEscape(j, model_);
                    fputs("\n}\n", j);
                    fclose(j);
                }
                log("processed derivative saved path=%s bytes=%lu", path, (unsigned long)size);
            } else {
                if (f)
                    fclose(f);
                log("processed derivative write failed");
            }
        }
        pthread_mutex_lock(&recMutex_);
        recPending_ = false;
        recJob_ = JOB_NONE;
        pthread_cond_signal(&recCondition_);
        pthread_mutex_unlock(&recMutex_);
        if (stopAfterJob) {
            log("Retro Clip stopped after storage/index write failure");
            finishRecording(true);
        }
    }

    void rect(uint16_t* bits, int stride, int x, int y, int w, int h, uint16_t color, int alpha) {
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > kSurfaceWidth)
            w = kSurfaceWidth - x;
        if (y + h > kSurfaceHeight)
            h = kSurfaceHeight - y;
        if (w <= 0 || h <= 0)
            return;
        for (int yy = y; yy < y + h; yy++)
            for (int xx = x; xx < x + w; xx++)
                bits[yy * stride + xx] =
                    alpha >= 100 ? color : blend565(bits[yy * stride + xx], color, alpha);
    }
    void text(uint16_t* bits, int stride, int x, int y, const char* value, uint16_t color,
              int scale) {
        if (!value)
            return;
        for (const char* p = value; *p && x < kSurfaceWidth - 6 * scale; p++) {
            const unsigned char* g = glyph(*p);
            if (g)
                for (int row = 0; row < 7; row++)
                    for (int col = 0; col < 5; col++)
                        if (g[row] & (1 << (4 - col)))
                            rect(bits, stride, x + col * scale, y + row * scale, scale, scale,
                                 color, 100);
            x += 6 * scale;
        }
    }
    void render() {
        pthread_mutex_lock(&mutex_);
        ANativeWindow* win = window_;
        if (win)
            ANativeWindow_acquire(win);
        pthread_mutex_unlock(&mutex_);
        if (!win)
            return;
        ANativeWindow_Buffer b;
        if (ANativeWindow_lock(win, &b, 0) != 0) {
            ANativeWindow_release(win);
            return;
        }
        uint16_t* bits = (uint16_t*)b.bits;
        int stride = b.stride;
        int64_t now = monotonicMs();
        if (touchActive_ && now - touchDownMs_ > 450)
            compare_ = true;
        rect(bits, stride, 0, 0, kSurfaceWidth, kSurfaceHeight, rgb565(15, 18, 20), 100);
        if (hasFrame_) {
            const Pixel* src = compare_ ? raw_ : output_;
            for (int y = 0; y < kSurfaceHeight; y++) {
                int sy = y * retrolens::kFrameHeight / kSurfaceHeight;
                for (int x = 0; x < kSurfaceWidth; x++) {
                    int sx = x * retrolens::kFrameWidth / kSurfaceWidth;
                    const Pixel& p = src[sy * retrolens::kFrameWidth + sx];
                    bits[y * stride + x] = rgb565(p.r, p.g, p.b);
                }
            }
        }
        uint16_t warm = rgb565(240, 232, 210), accent = rgb565(64, 232, 190),
                 panel = rgb565(17, 22, 25);
        if (!hasFrame_) {
            text(bits, stride, 205, 190, "RETROLENS", warm, 4);
            text(bits, stride, 230, 235, "ANALOG IMAGE ENGINE", accent, 2);
            int sweep = (int)((now - startMs_) / 3 % kSurfaceWidth);
            rect(bits, stride, sweep, 270, 54, 2, accent, 80);
            if (now - startMs_ > 800)
                text(bits, stride, 220, 305, "INITIALIZING CAMERA", warm, 1);
        }
        if (error_) {
            rect(bits, stride, 90, 120, 460, 240, panel, 92);
            text(bits, stride, 145, 150, "PREVIEW UNAVAILABLE", warm, 3);
            text(bits, stride, 130, 205, "ANALYTICAL LIVE VIEW COULD NOT START", warm, 1);
            text(bits, stride, 150, 235, "NORMAL CAPTURE REMAINS AVAILABLE", accent, 1);
            text(bits, stride, 150, 300, "RETRY   DIAGNOSTICS   EXIT", warm, 1);
        }
        if (hasFrame_ && controlsVisible_) {
            const Preset& p = retrolens::presetAt(selected_);
            rect(bits, stride, 0, 0, 640, 38, panel, 78);
            char top[160];
            snprintf(top, sizeof(top), "%s   T%d   %d FPS   PHOTO", p.name, p.tier, fps_);
            text(bits, stride, 12, 12, top, warm, 1);
            if (isFavorite())
                text(bits, stride, 600, 12, "FAV", accent, 1);
            rect(bits, stride, 0, 372, 640, 108, panel, 82);
            for (int card = -1; card <= 1; card++) {
                int index =
                    (selected_ + card + retrolens::presetCount()) % retrolens::presetCount();
                int x = card == 0 ? 210 : (card < 0 ? 14 : 444);
                uint16_t border = card == 0 ? accent : warm;
                rect(bits, stride, x, 385, 182, 78, border, 100);
                rect(bits, stride, x + 2, 387, 178, 74, panel, 100);
                text(bits, stride, x + 10, 399, retrolens::presetAt(index).category, accent, 1);
                text(bits, stride, x + 10, 425, retrolens::presetAt(index).name, warm, 1);
                char tier[16];
                snprintf(tier, sizeof(tier), "TIER %d", retrolens::presetAt(index).tier);
                text(bits, stride, x + 10, 445, tier, warm, 1);
            }
        }
        if (scene_ == SCENE_DRAWER) {
            const Preset& p = retrolens::presetAt(selected_);
            rect(bits, stride, 70, 70, 500, 315, panel, 94);
            text(bits, stride, 100, 92, "QUICK CONTROLS", accent, 2);
            text(bits, stride, 100, 130, p.name, warm, 2);
            char value[64];
            snprintf(value, sizeof(value), "INTENSITY  %d", intensity_);
            text(bits, stride, 100, 182, value, warm, 2);
            rect(bits, stride, 100, 220, 400, 12, rgb565(45, 55, 58), 100);
            rect(bits, stride, 100, 220, intensity_ * 4, 12, accent, 100);
            text(bits, stride, 100, 255, "GRAIN  TUNED", warm, 1);
            text(bits, stride, 100, 280, "MOTION  DETERMINISTIC", warm, 1);
            text(bits, stride, 100, 305, "CADENCE  SOURCE", warm, 1);
            text(bits, stride, 100, 340, p.description, warm, 1);
        } else if (scene_ == SCENE_BROWSER) {
            rect(bits, stride, 34, 46, 572, 360, panel, 95);
            text(bits, stride, 60, 68, "STYLE BROWSER", accent, 2);
            int first = selected_ - 4;
            if (first < 0)
                first = 0;
            for (int i = 0; i < 9 && first + i < retrolens::presetCount(); i++) {
                const Preset& p = retrolens::presetAt(first + i);
                uint16_t c = first + i == selected_ ? accent : warm;
                text(bits, stride, 70, 112 + i * 29, p.category, c, 1);
                text(bits, stride, 230, 112 + i * 29, p.name, c, 1);
            }
        } else if (scene_ == SCENE_GALLERY) {
            rect(bits, stride, 34, 46, 572, 360, panel, 95);
            text(bits, stride, 60, 68, "RETROLENS GALLERY", accent, 2);
            text(bits, stride, 60, 120, "PROCESSED PHOTOS", warm, 2);
            text(bits, stride, 60, 165, "RETRO CLIPS  MJPEG / SILENT", warm, 2);
            text(bits, stride, 60, 230, "FILES ARE STORED UNDER RETROLENS", accent, 1);
            text(bits, stride, 60, 275, "PLAYBACK INDEX READY", warm, 1);
        }
        if (compare_) {
            rect(bits, stride, 0, 42, 100, 24, panel, 80);
            text(bits, stride, 10, 50, "ORIGINAL", warm, 1);
        }
        if (recording_) {
            rect(bits, stride, 500, 48, 128, 30, panel, 85);
            rect(bits, stride, 510, 57, 12, 12, rgb565(235, 55, 48), 100);
            text(bits, stride, 532, 57, "REC 10 FPS", warm, 1);
        }
        if (captureFlashUntil_ > now)
            rect(bits, stride, 0, 0, 640, 480, warm, 25);
        ANativeWindow_unlockAndPost(win);
        ANativeWindow_release(win);
    }
};

static Runtime* from(jlong handle) {
    return reinterpret_cast<Runtime*>((intptr_t)handle);
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL Java_io_pihda_retrolens_NativeBridge_nativeCreate(
    JNIEnv* env, jclass, jstring root, jstring model, jstring version) {
    const char* r = env->GetStringUTFChars(root, 0);
    const char* m = env->GetStringUTFChars(model, 0);
    const char* v = env->GetStringUTFChars(version, 0);
    Runtime* runtime = new Runtime(r, m, v);
    env->ReleaseStringUTFChars(root, r);
    env->ReleaseStringUTFChars(model, m);
    env->ReleaseStringUTFChars(version, v);
    if (!runtime->valid()) {
        delete runtime;
        return 0;
    }
    return (jlong)(intptr_t)runtime;
}
extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeDestroy(JNIEnv*,
                                                                                     jclass,
                                                                                     jlong h) {
    delete from(h);
}
extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeSetSurface(
    JNIEnv* env, jclass, jlong h, jobject surface) {
    Runtime* r = from(h);
    if (r && surface)
        r->setSurface(ANativeWindow_fromSurface(env, surface));
}
extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeClearSurface(JNIEnv*,
                                                                                          jclass,
                                                                                          jlong h) {
    Runtime* r = from(h);
    if (r)
        r->clearSurface();
}
extern "C" JNIEXPORT jboolean JNICALL Java_io_pihda_retrolens_NativeBridge_nativeSubmitFrame(
    JNIEnv* env, jclass, jlong h, jobject buffer, jint length, jlong timestamp) {
    Runtime* r = from(h);
    unsigned char* p = (unsigned char*)env->GetDirectBufferAddress(buffer);
    return r && p && r->submit(p, length, timestamp) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeSetPreviewError(
    JNIEnv* env, jclass, jlong h, jstring message) {
    Runtime* r = from(h);
    if (!r)
        return;
    const char* m = env->GetStringUTFChars(message, 0);
    r->setError(m);
    env->ReleaseStringUTFChars(message, m);
}
extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeKey(JNIEnv*, jclass,
                                                                                 jlong h, jint key,
                                                                                 jboolean down,
                                                                                 jlong ts) {
    Runtime* r = from(h);
    if (r)
        r->key(key, down == JNI_TRUE, ts);
}
extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeTouch(
    JNIEnv*, jclass, jlong h, jint action, jfloat x, jfloat y, jlong ts) {
    Runtime* r = from(h);
    if (r)
        r->touch(action, x, y, ts);
}
extern "C" JNIEXPORT void JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeCaptureRequested(JNIEnv*, jclass, jlong h, jlong ts) {
    Runtime* r = from(h);
    if (r)
        r->captureRequested(ts);
}
extern "C" JNIEXPORT void JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeSaveSnapshot(JNIEnv*, jclass, jlong h, jlong ts) {
    Runtime* r = from(h);
    if (r)
        r->saveSnapshot(ts);
}
extern "C" JNIEXPORT void JNICALL
Java_io_pihda_retrolens_NativeBridge_nativeToggleRecording(JNIEnv*, jclass, jlong h, jlong ts) {
    Runtime* r = from(h);
    if (r)
        r->toggleRecording(ts);
}
extern "C" JNIEXPORT void JNICALL Java_io_pihda_retrolens_NativeBridge_nativeStopRecording(
    JNIEnv*, jclass, jlong h, jboolean interrupted, jlong ts) {
    Runtime* r = from(h);
    if (r)
        r->stopRecording(interrupted == JNI_TRUE, ts);
}
