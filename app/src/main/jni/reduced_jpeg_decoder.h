#ifndef RETROLENS_REDUCED_JPEG_DECODER_H
#define RETROLENS_REDUCED_JPEG_DECODER_H

#include <stddef.h>

#include "retrolens_core.h"

namespace retrolens {

/** Decodes one 640x480 baseline JPEG into the fixed 80x60 reduced sample grid. */
bool decodeReducedJpeg(const unsigned char* jpeg, size_t length, Pixel* output);

} // namespace retrolens

#endif
