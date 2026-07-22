#include "display_probe.h"
#include "jpeg_encoder.h"
#include "retrolens_core.h"
#include "third_party/picojpeg/picojpeg.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace retrolens;

struct BadInput {
    const unsigned char* data;
    size_t length;
    size_t offset;
};
static unsigned char readBadJpeg(unsigned char* destination, unsigned char requested,
                                 unsigned char* actual, void* context) {
    BadInput* input = (BadInput*)context;
    size_t left = input->offset < input->length ? input->length - input->offset : 0;
    size_t count = left < requested ? left : requested;
    if (count)
        memcpy(destination, input->data + input->offset, count);
    input->offset += count;
    *actual = (unsigned char)count;
    return 0;
}

static void fill(Pixel* pixels, int width, int height) {
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            Pixel& p = pixels[y * width + x];
            p.r = (unsigned char)(x * 255 / (width ? width : 1));
            p.g = (unsigned char)(y * 255 / (height ? height : 1));
            p.b = (unsigned char)((x + y) * 17);
        }
}

static unsigned long checksum(const Pixel* pixels, int count) {
    unsigned long value = 2166136261UL;
    for (int i = 0; i < count; i++) {
        value ^= pixels[i].r;
        value *= 16777619UL;
        value ^= pixels[i].g;
        value *= 16777619UL;
        value ^= pixels[i].b;
        value *= 16777619UL;
    }
    return value;
}

static void testCatalog() {
    assert(kFrameWidth == 80 && kFrameHeight == 60);
    assert(!kRetroClipEnabled);
    assert(!kProcessedDerivativeEnabled);
    assert(presetCount() == 70);
    assert(!strcmp(presetAt(findPreset("piss_filter_2007")).name, "Piss Filter 2007"));
    const Preset& soviet = presetAt(findPreset("soviet_archive_1978"));
    assert((soviet.flags & FX_TEMPORAL) != 0);
    assert((soviet.flags & FX_JITTER) != 0);
    assert((soviet.flags & FX_TEAR) != 0);
    for (int i = 0; i < presetCount(); i++) {
        const Preset& p = presetAt(i);
        assert(p.id && *p.id);
        assert(p.name && *p.name);
        assert(p.category && *p.category);
        assert(p.description && strlen(p.description) > 12);
        assert(p.tier >= 1 && p.tier <= 3);
        assert(p.controls != 0);
    }
}

static void testAllPresetsAndTinyImages() {
    const int widths[] = {1, 3, 7, 17, 63};
    const int heights[] = {1, 5, 9, 15, 47};
    for (int size = 0; size < 5; size++) {
        int w = widths[size], h = heights[size], n = w * h;
        Pixel* src = new Pixel[n];
        Pixel* out = new Pixel[n];
        Pixel* scratch = new Pixel[n];
        Pixel* previous = new Pixel[n];
        fill(src, w, h);
        fill(previous, w, h);
        for (int i = 0; i < presetCount(); i++) {
            memset(out, 0, n * sizeof(Pixel));
            processFrame(src, out, scratch, previous, w, h, presetAt(i), 100, 12345, 987654);
            assert(checksum(out, n) != 0);
        }
        delete[] src;
        delete[] out;
        delete[] scratch;
        delete[] previous;
    }
}

static void testDeterminismAndBayer() {
    Pixel src[64], a[64], b[64], scratch[64], previous[64];
    fill(src, 8, 8);
    fill(previous, 8, 8);
    const Preset& p = presetAt(findPreset("olive_pocket"));
    processFrame(src, a, scratch, previous, 8, 8, p, 100, 77, 1000);
    processFrame(src, b, scratch, previous, 8, 8, p, 100, 77, 1000);
    assert(!memcmp(a, b, sizeof(a)));
    memset(b, 0, sizeof(b));
    processFrame(src, b, 0, previous, 8, 8, p, 100, 77, 1000);
    assert(!memcmp(a, b, sizeof(a)));
    assert(checksum(a, 64) != checksum(src, 64));
    uint32_t s1 = 9, s2 = 9;
    for (int i = 0; i < 100; i++)
        assert(nextRandom(&s1) == nextRandom(&s2));
}

static void testSurfaceBlitBoundsAndFormats() {
    const uint16_t source[4] = {0xf800, 0x07e0, 0x001f, 0xffff};
    uint16_t rgb565[3 * 5 + 2];
    for (int i = 0; i < 17; i++)
        rgb565[i] = 0x55aa;
    assert(blitRgb565(source, 2, 2, rgb565, 3, 5, 3, 4));
    assert(rgb565[15] == 0x55aa && rgb565[16] == 0x55aa);
    assert(rgb565[0] == 0xf800 && rgb565[2] == 0x07e0);
    assert(rgb565[12] == 0x001f && rgb565[14] == 0xffff);

    uint32_t rgba[5 * 3 + 2];
    for (int i = 0; i < 17; i++)
        rgba[i] = 0x12345678U;
    assert(blitRgb565(source, 2, 2, rgba, 3, 5, 3, 1));
    assert(rgba[15] == 0x12345678U && rgba[16] == 0x12345678U);
    assert(rgba[0] == 0xff0000ffU);
    assert(!blitRgb565(source, 2, 2, rgba, 3, 5, 2, 1));
    assert(!blitRgb565(source, 2, 2, rgba, 3, 5, 3, 99));
}

static unsigned long checksum16(const uint16_t* pixels, int count) {
    unsigned long value = 2166136261UL;
    for (int index = 0; index < count; index++) {
        value ^= pixels[index];
        value *= 16777619UL;
    }
    return value;
}

static void testDisplayProbeRaster() {
    uint16_t guarded[2 + 13 * 9 + 2];
    for (int index = 0; index < (int)(sizeof(guarded) / sizeof(guarded[0])); index++)
        guarded[index] = 0x55aa;
    assert(renderDisplayProbe(guarded + 2, 13, 9, "probe-test", 17, 11, 4));
    assert(guarded[0] == 0x55aa && guarded[1] == 0x55aa);
    assert(guarded[2 + 13 * 9] == 0x55aa && guarded[2 + 13 * 9 + 1] == 0x55aa);
    assert(checksum16(guarded + 2, 13 * 9) != 0);

    uint16_t first[kDisplayProbeWidth * kDisplayProbeHeight];
    uint16_t second[kDisplayProbeWidth * kDisplayProbeHeight];
    assert(renderDisplayProbe(first, kDisplayProbeWidth, kDisplayProbeHeight, "native-probe-test",
                              256, 144, 4));
    assert(renderDisplayProbe(second, kDisplayProbeWidth, kDisplayProbeHeight, "native-probe-test",
                              256, 144, 4));
    assert(!memcmp(first, second, sizeof(first)));
    assert(checksum16(first, kDisplayProbeWidth * kDisplayProbeHeight) != 0);
    assert(first[0] == probeRgb565(66, 232, 188));
    assert(!renderDisplayProbe(0, 1, 1, "invalid", 1, 1, 4));
    assert(!renderDisplayProbe(first, 0, 1, "invalid", 1, 1, 4));
}

static void testPerformanceController() {
    PerformanceDecision fast = choosePerformance(20, 15, 5, 0, -1);
    assert(fast.targetFps == 10 && !fast.reducedAnimation);
    PerformanceDecision slow = choosePerformance(100, 60, 20, 20, -1);
    assert(slow.detail == 0 && slow.targetFps == 6 && slow.reducedAnimation);
    PerformanceDecision forced = choosePerformance(200, 100, 80, 30, 2);
    assert(forced.detail == 2);
}

static void testJsonEscaping() {
    FILE* f = tmpfile();
    assert(f);
    jsonEscape(f, "quote\" slash\\ line\n tab\t");
    fflush(f);
    rewind(f);
    char text[128] = {0};
    assert(fread(text, 1, sizeof(text) - 1, f) > 0);
    fclose(f);
    assert(strstr(text, "\\\"") != 0);
    assert(strstr(text, "\\\\") != 0);
    assert(strstr(text, "\\n") != 0);
    assert(strstr(text, "\\t") != 0);
}

static void testJpegAndAvi() {
    const int w = 64, h = 48;
    Pixel pixels[64 * 48];
    fill(pixels, w, h);
    unsigned char encoded[128 * 1024];
    size_t size = 0;
    assert(encodeJpeg(pixels, w, h, 82, encoded, sizeof(encoded), &size));
    assert(size > 100);
    assert(encoded[0] == 0xff && encoded[1] == 0xd8 && encoded[size - 2] == 0xff &&
           encoded[size - 1] == 0xd9);
    size_t rejectedSize = 0;
    assert(!encodeJpeg(pixels, w, h, 82, encoded, 32, &rejectedSize));
    const char* path = "/tmp/retrolens-native-test.avi";
    AviWriter avi;
    assert(avi.open(path, w, h, 10));
    assert(avi.addFrame(encoded, size));
    assert(avi.addFrame(encoded, size));
    assert(avi.frameCount() == 2);
    assert(avi.finish());
    FILE* f = fopen(path, "rb");
    assert(f);
    char header[12];
    assert(fread(header, 1, 12, f) == 12);
    assert(!memcmp(header, "RIFF", 4));
    assert(!memcmp(header + 8, "AVI ", 4));
    fseek(f, 0, SEEK_END);
    assert(ftell(f) > 1000);
    fclose(f);
}

static void testAviBoundsAndAbort() {
    AviWriter avi;
    assert(!avi.open(0, 320, 240, 10));
    assert(avi.open("/tmp/retrolens-abort.avi", 320, 240, 10));
    unsigned char invalid[8] = {0};
    assert(!avi.addFrame(invalid, 1024U * 1024U + 1));
    avi.abort();
    remove("/tmp/retrolens-abort.avi");
}

static void testMalformedJpegRejection() {
    const unsigned char malformed[] = {0xff, 0xd8, 0xff, 0x00, 0x12, 0x34, 0xff, 0xd9};
    BadInput input = {malformed, sizeof(malformed), 0};
    pjpeg_image_info_t info;
    unsigned char status = pjpeg_decode_init(&info, readBadJpeg, &input, 1);
    assert(status != 0);
}

int main() {
    testCatalog();
    testAllPresetsAndTinyImages();
    testDeterminismAndBayer();
    testSurfaceBlitBoundsAndFormats();
    testDisplayProbeRaster();
    testPerformanceController();
    testJsonEscaping();
    testJpegAndAvi();
    testAviBoundsAndAbort();
    testMalformedJpegRejection();
    printf("RetroLens native tests passed: presets=%d\n", presetCount());
    return 0;
}
