#include "reduced_jpeg_decoder.h"

#include <string.h>

#include "third_party/picojpeg/picojpeg.h"

namespace retrolens {
namespace {

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

} // namespace

bool decodeReducedJpeg(const unsigned char* jpeg, size_t length, Pixel* output) {
    if (!jpeg || length < 4 || !output)
        return false;
    JpegInput input = {jpeg, length, 0};
    pjpeg_image_info_t info;
    unsigned char status = pjpeg_decode_init(&info, readJpeg, &input, 1);
    if (status || info.m_width != 640 || info.m_height != 480)
        return false;
    for (int my = 0; my < info.m_MCUSPerCol; my++)
        for (int mx = 0; mx < info.m_MCUSPerRow; mx++) {
            status = pjpeg_decode_mcu();
            if (status)
                return false;
            int left = mx * info.m_MCUWidth;
            int top = my * info.m_MCUHeight;
            int right = left + info.m_MCUWidth;
            int bottom = top + info.m_MCUHeight;
            if (right > info.m_width)
                right = info.m_width;
            if (bottom > info.m_height)
                bottom = info.m_height;
            int outputLeft = left * kFrameWidth / info.m_width;
            int outputTop = top * kFrameHeight / info.m_height;
            int outputRight = (right * kFrameWidth + info.m_width - 1) / info.m_width;
            int outputBottom = (bottom * kFrameHeight + info.m_height - 1) / info.m_height;
            for (int y = outputTop; y < outputBottom; y++) {
                int sourceY = y * info.m_height / kFrameHeight;
                int localY = sourceY - top;
                for (int x = outputLeft; x < outputRight; x++) {
                    int sourceX = x * info.m_width / kFrameWidth;
                    int localX = sourceX - left;
                    int offset = mcuOffset(localX, localY);
                    Pixel& pixel = output[y * kFrameWidth + x];
                    pixel.r = info.m_pMCUBufR[offset];
                    pixel.g = info.m_pMCUBufG[offset];
                    pixel.b = info.m_pMCUBufB[offset];
                }
            }
        }
    return true;
}

} // namespace retrolens
