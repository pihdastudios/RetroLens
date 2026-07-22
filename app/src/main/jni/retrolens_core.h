#ifndef RETROLENS_CORE_H
#define RETROLENS_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace retrolens {

static const int kFrameWidth = 320;
static const int kFrameHeight = 240;
static const int kMaxPresets = 70;
static const int kMaxAviFrames = 9000;

struct Pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

enum EffectFlags {
    FX_MONO       = 1 << 0,
    FX_DITHER     = 1 << 1,
    FX_SCANLINES  = 1 << 2,
    FX_PIXELATE   = 1 << 3,
    FX_TEMPORAL   = 1 << 4,
    FX_JITTER     = 1 << 5,
    FX_VIGNETTE   = 1 << 6,
    FX_EDGE       = 1 << 7,
    FX_HALFTONE   = 1 << 8,
    FX_THERMAL    = 1 << 9,
    FX_CGA        = 1 << 10,
    FX_INVERT     = 1 << 11,
    FX_ASCII      = 1 << 12,
    FX_TEAR       = 1 << 13,
    FX_BLOOM      = 1 << 14,
    FX_MASK       = 1 << 15
};

enum ControlFlags {
    CTRL_INTENSITY = 1 << 0,
    CTRL_PIXEL = 1 << 1,
    CTRL_PALETTE = 1 << 2,
    CTRL_CONTRAST = 1 << 3,
    CTRL_GRAIN = 1 << 4,
    CTRL_MOTION = 1 << 5,
    CTRL_VIGNETTE = 1 << 6,
    CTRL_CADENCE = 1 << 7
};

struct Preset {
    const char* id;
    const char* name;
    const char* category;
    const char* description;
    uint8_t tier;
    uint16_t flags;
    uint16_t controls;
    int16_t red;
    int16_t green;
    int16_t blue;
    int16_t saturation;
    int16_t contrast;
    uint8_t posterize;
    uint8_t noise;
    uint8_t pixelSize;
    uint8_t temporal;
};

int presetCount();
const Preset& presetAt(int index);
int findPreset(const char* id);
void processFrame(const Pixel* source, Pixel* output, Pixel* scratch,
        const Pixel* previous, int width, int height, const Preset& preset,
        int intensity, uint32_t seed, int64_t timestampMs);
uint32_t nextRandom(uint32_t* state);
void jsonEscape(FILE* output, const char* value);

struct PerformanceDecision {
    int detail;
    int targetFps;
    bool reducedAnimation;
};

PerformanceDecision choosePerformance(int decodeMs, int filterMs, int renderMs,
        int droppedFrames, int forcedMode);

class AviWriter {
public:
    AviWriter();
    ~AviWriter();
    bool open(const char* temporaryPath, int width, int height, int fps);
    bool addFrame(const unsigned char* jpeg, size_t size);
    bool finish();
    void abort();
    int frameCount() const { return frameCount_; }
    long bytesWritten() const { return bytesWritten_; }
private:
    FILE* file_;
    int width_;
    int height_;
    int fps_;
    int frameCount_;
    long bytesWritten_;
    long riffSizeOffset_;
    long moviSizeOffset_;
    long moviDataOffset_;
    long avihFramesOffset_;
    long strhFramesOffset_;
    uint32_t frameOffsets_[kMaxAviFrames];
    uint32_t frameSizes_[kMaxAviFrames];
    void writeU16(uint16_t value);
    void writeU32(uint32_t value);
    void writeFourCC(const char* value);
    void patchU32(long offset, uint32_t value);
};

} // namespace retrolens

#endif
