#include "jpeg_encoder.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "third_party/stb_image_write.h"

#include <string.h>

namespace retrolens {

struct EncoderTarget {
    unsigned char* output;
    size_t capacity;
    size_t size;
    bool overflow;
};

static void writeEncoded(void* context, void* data, int size) {
    EncoderTarget* target = static_cast<EncoderTarget*>(context);
    if (!target || size < 0 || target->overflow) return;
    if ((size_t)size > target->capacity - target->size) {
        target->overflow = true;
        return;
    }
    memcpy(target->output + target->size, data, (size_t)size);
    target->size += (size_t)size;
}

bool encodeJpeg(const Pixel* pixels, int width, int height, int quality,
        unsigned char* output, size_t capacity, size_t* outputSize) {
    if (outputSize) *outputSize = 0;
    if (!pixels || !output || !outputSize || width <= 0 || height <= 0) return false;
    EncoderTarget target = { output, capacity, 0, false };
    int result = stbi_write_jpg_to_func(writeEncoded, &target, width, height, 3,
            pixels, quality);
    if (!result || target.overflow || target.size < 4) return false;
    *outputSize = target.size;
    return true;
}

} // namespace retrolens
