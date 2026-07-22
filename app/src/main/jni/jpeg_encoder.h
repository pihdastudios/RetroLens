#ifndef RETROLENS_JPEG_ENCODER_H
#define RETROLENS_JPEG_ENCODER_H

#include <stddef.h>
#include "retrolens_core.h"

namespace retrolens {

bool encodeJpeg(const Pixel* pixels, int width, int height, int quality,
        unsigned char* output, size_t capacity, size_t* outputSize);

} // namespace retrolens

#endif
