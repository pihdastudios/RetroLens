#ifndef RETROLENS_JPEG_ENCODER_H
#define RETROLENS_JPEG_ENCODER_H

#include "retrolens_core.h"
#include <stddef.h>

namespace retrolens {

bool encodeJpeg(const Pixel* pixels, int width, int height, int quality, unsigned char* output,
                size_t capacity, size_t* outputSize);

} // namespace retrolens

#endif
