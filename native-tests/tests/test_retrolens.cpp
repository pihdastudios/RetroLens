#include "display_probe.h"
#include "display_probe_worker.h"
#include "jpeg_encoder.h"
#include "reduced_jpeg_decoder.h"
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
    SequenceProbeMetrics starting = calculateSequenceProbeMetrics(kSequenceStarting, 0, 0, 0, 0, 0);
    FilterProbeMetrics filter;
    memset(&filter, 0, sizeof(filter));
    uint16_t guarded[2 + 13 * 9 + 2];
    for (int index = 0; index < (int)(sizeof(guarded) / sizeof(guarded[0])); index++)
        guarded[index] = 0x55aa;
    assert(renderDisplayProbe(guarded + 2, 13, 9, "probe-test", 17, 11, 4, 7, starting, 0, filter));
    assert(guarded[0] == 0x55aa && guarded[1] == 0x55aa);
    assert(guarded[2 + 13 * 9] == 0x55aa && guarded[2 + 13 * 9 + 1] == 0x55aa);
    assert(checksum16(guarded + 2, 13 * 9) != 0);

    uint16_t first[kDisplayProbeWidth * kDisplayProbeHeight];
    uint16_t second[kDisplayProbeWidth * kDisplayProbeHeight];
    assert(renderDisplayProbe(first, kDisplayProbeWidth, kDisplayProbeHeight, "native-probe-test",
                              240, 180, 4, 11, starting, 0, filter));
    assert(renderDisplayProbe(second, kDisplayProbeWidth, kDisplayProbeHeight, "native-probe-test",
                              240, 180, 4, 11, starting, 0, filter));
    assert(!memcmp(first, second, sizeof(first)));
    assert(checksum16(first, kDisplayProbeWidth * kDisplayProbeHeight) != 0);
    assert(first[0] == probeRgb565(66, 232, 188));
    assert(!renderDisplayProbe(0, 1, 1, "invalid", 1, 1, 4, 0, starting, 0, filter));
    assert(!renderDisplayProbe(first, 0, 1, "invalid", 1, 1, 4, 0, starting, 0, filter));

    assert(renderDisplayProbe(second, kDisplayProbeWidth, kDisplayProbeHeight, "native-probe-test",
                              240, 180, 4, 12, starting, 0, filter));
    assert(memcmp(first, second, sizeof(first)) != 0);

    SequenceProbeMetrics active =
        calculateSequenceProbeMetrics(kSequenceActive, 10, 9, 65536, 1000, 2000);
    assert(active.state == kSequenceActive);
    assert(active.receivedFrames == 10 && active.releasedFrames == 9);
    assert(active.lastJpegBytes == 65536 && active.fpsTenths == 90);
    assert(renderDisplayProbe(second, kDisplayProbeWidth, kDisplayProbeHeight, "sequence-probe",
                              240, 180, 4, 11, active, 0, filter));
    assert(memcmp(first, second, sizeof(first)) != 0);

    SequenceProbeMetrics malformed = calculateSequenceProbeMetrics(99, -1, -2, 999999, 2000, 1000);
    assert(malformed.state == kSequenceError);
    assert(malformed.receivedFrames == 0 && malformed.releasedFrames == 0);
    assert(malformed.lastJpegBytes == 256 * 1024 && malformed.fpsTenths == 0);

    Pixel filtered[kFrameWidth * kFrameHeight];
    for (int index = 0; index < kFrameWidth * kFrameHeight; index++) {
        filtered[index].r = (unsigned char)(index % 256);
        filtered[index].g = (unsigned char)((index * 3) % 256);
        filtered[index].b = (unsigned char)((index * 7) % 256);
    }
    filter.hasFrame = true;
    filter.processedFrames = 3;
    filter.processedFpsTenths = 84;
    filter.decodeMs = 6;
    filter.filterMs = 1;
    filter.selectedPreset = findPreset("olive_pocket");
    assert(renderDisplayProbe(second, kDisplayProbeWidth, kDisplayProbeHeight, "filter-probe", 240,
                              180, 4, 11, active, filtered, filter));
    assert(checksum16(second, kDisplayProbeWidth * kDisplayProbeHeight) != 0);

    const int sourceX = 40;
    const int sourceY = 30;
    const uint16_t expected = probeRgb565(filtered[sourceY * kFrameWidth + sourceX].r,
                                          filtered[sourceY * kFrameWidth + sourceX].g,
                                          filtered[sourceY * kFrameWidth + sourceX].b);
    for (int y = sourceY * 3; y < sourceY * 3 + 3; y++)
        for (int x = sourceX * 3; x < sourceX * 3 + 3; x++)
            assert(second[y * kDisplayProbeWidth + x] == expected);
    assert(second[90 * kDisplayProbeWidth] == probeRgb565(filtered[30 * kFrameWidth].r,
                                                          filtered[30 * kFrameWidth].g,
                                                          filtered[30 * kFrameWidth].b));
    assert(second[90 * kDisplayProbeWidth + kDisplayProbeWidth - 1] ==
           probeRgb565(filtered[30 * kFrameWidth + kFrameWidth - 1].r,
                       filtered[30 * kFrameWidth + kFrameWidth - 1].g,
                       filtered[30 * kFrameWidth + kFrameWidth - 1].b));
}

static void testDisplayProbeWorkerLifecycle() {
    for (int cycle = 0; cycle < 20; cycle++) {
        DisplayProbeWorker worker("host-thread-probe", 2);
        assert(worker.start());
        assert(worker.start());
        assert(worker.waitForFrame(3, 250));
        worker.updateSurfaceInfo(17, 11, 4);
        worker.updateSequenceMetrics(kSequenceActive, cycle + 1, cycle + 1, 60000, 1000,
                                     1000 + cycle * 100);

        uint16_t destination[17 * 11 + 2];
        for (int index = 0; index < 17 * 11 + 2; index++)
            destination[index] = 0x55aa;
        int postedFrame = 0;
        assert(worker.blitLatest(destination, 17, 11, 17, 4, &postedFrame));
        assert(postedFrame >= 3);
        assert(destination[17 * 11] == 0x55aa && destination[17 * 11 + 1] == 0x55aa);

        int framesBeforeStop = 0;
        int postsBeforeStop = 0;
        worker.getStats(&framesBeforeStop, &postsBeforeStop);
        assert(framesBeforeStop >= 3 && postsBeforeStop == 1);
        int stopMs = worker.stop();
        assert(stopMs >= 0 && stopMs <= 250);
        assert(worker.stop() == 0);
        int framesAfterStop = 0;
        int postsAfterStop = 0;
        worker.getStats(&framesAfterStop, &postsAfterStop);
        assert(framesAfterStop >= framesBeforeStop);
        assert(postsAfterStop == postsBeforeStop);
        int stableFrames = 0;
        int stablePosts = 0;
        worker.getStats(&stableFrames, &stablePosts);
        assert(stableFrames == framesAfterStop && stablePosts == postsAfterStop);
    }
}

static size_t makeAnalyticalJpeg(unsigned char* output, size_t capacity) {
    Pixel* source = new Pixel[640 * 480];
    fill(source, 640, 480);
    size_t size = 0;
    bool encoded = encodeJpeg(source, 640, 480, 82, output, capacity, &size);
    delete[] source;
    return encoded ? size : 0;
}

static void testReducedDecodeAndBoundedWorker() {
    unsigned char* encoded = new unsigned char[kFilterProbeInputCapacity];
    size_t size = makeAnalyticalJpeg(encoded, kFilterProbeInputCapacity);
    assert(size > 1000 && size <= (size_t)kFilterProbeInputCapacity);

    Pixel decoded[kFrameWidth * kFrameHeight];
    memset(decoded, 0, sizeof(decoded));
    assert(decodeReducedJpeg(encoded, size, decoded));
    assert(checksum(decoded, kFrameWidth * kFrameHeight) != 0);
    assert(!decodeReducedJpeg(encoded, 3, decoded));
    assert(!decodeReducedJpeg(0, size, decoded));
    assert(!decodeReducedJpeg(encoded, size, 0));

    Pixel small[64 * 48];
    fill(small, 64, 48);
    unsigned char wrongSize[128 * 1024];
    size_t wrongSizeLength = 0;
    assert(encodeJpeg(small, 64, 48, 82, wrongSize, sizeof(wrongSize), &wrongSizeLength));
    assert(!decodeReducedJpeg(wrongSize, wrongSizeLength, decoded));

    DisplayProbeWorker worker("host-filter-probe", 8);
    assert(worker.start());
    int submitted = 0;
    int dropped = 0;
    for (int index = 0; index < 20; index++) {
        int result = worker.submitJpeg(encoded, (int)size, 1000 + index * 100);
        assert(result != kFilterSubmitInvalid);
        if (result == kFilterSubmitAccepted)
            submitted++;
        else
            dropped++;
    }
    assert(submitted >= 1 && submitted + dropped == 20);
    assert(worker.waitForProcessedFrame(1, 1000));
    FilterProbeMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    worker.getFilterStats(&metrics);
    assert(metrics.hasFrame && !metrics.decodeError);
    assert(metrics.acceptedFrames == submitted);
    assert(metrics.droppedFrames == dropped);
    assert(metrics.processedFrames >= 1 && metrics.processedFrames <= metrics.acceptedFrames);
    assert(metrics.decodeFailures == 0);
    const char* expectedStyles[kFilterProbeStyleCount] = {
        "olive_pocket",     "cga_shock",           "one_bit_desktop", "consumer_crt",
        "vhs_rental",       "soviet_archive_1978", "newsprint",       "comic_ink",
        "piss_filter_2007", "thermal_false_color"};
    assert(metrics.selectedPreset == findPreset(expectedStyles[0]));
    for (int index = 1; index < kFilterProbeStyleCount; index++)
        assert(worker.changeStyle(1) == findPreset(expectedStyles[index]));
    assert(worker.changeStyle(1) == findPreset(expectedStyles[0]));
    assert(worker.changeStyle(-1) == findPreset(expectedStyles[kFilterProbeStyleCount - 1]));
    worker.getFilterStats(&metrics);
    assert(metrics.styleChanges == kFilterProbeStyleCount + 1);
    assert(worker.stop() <= 250);
    assert(worker.submitJpeg(encoded, (int)size, 5000) == kFilterSubmitInvalid);
    delete[] encoded;
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
    Pixel output[kFrameWidth * kFrameHeight];
    assert(!decodeReducedJpeg(malformed, sizeof(malformed), output));
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
    testDisplayProbeWorkerLifecycle();
    testReducedDecodeAndBoundedWorker();
    testPerformanceController();
    testJsonEscaping();
    testJpegAndAvi();
    testAviBoundsAndAbort();
    testMalformedJpegRejection();
    printf("RetroLens native tests passed: presets=%d\n", presetCount());
    return 0;
}
