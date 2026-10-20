#include "presspng/codecs.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include "lodepng.h"
#include "presspng/errors.h"
#include "presspng/qoi.h"

extern "C" {
typedef void stbi_write_func(void* context, void* data, int size);
int stbi_write_jpg_to_func(stbi_write_func* func, void* context, int x, int y,
                           int comp, const void* data, int quality);
int stbi_write_bmp_to_func(stbi_write_func* func, void* context, int x, int y,
                           int comp, const void* data);
int stbi_write_tga_to_func(stbi_write_func* func, void* context, int x, int y,
                           int comp, const void* data);
}

namespace presspng {

namespace {

struct BufferWriter {
    std::vector<std::uint8_t>* out;
};

void appendToBuffer(void* context, void* data, int size) {
    auto* writer = static_cast<BufferWriter*>(context);
    const auto* begin = static_cast<const std::uint8_t*>(data);
    writer->out->insert(writer->out->end(), begin, begin + size);
}

void requirePixels(const Image& image) {
    if (image.empty()) {
        throw CompressionError(ErrorCode::EncodeFailed,
                               "cannot encode an empty image");
    }
}

int clampInt(int value, int low, int high) {
    return std::max(low, std::min(value, high));
}

} // namespace

std::vector<std::uint8_t> JpegCodec::encode(
    const Image& image, const EncodeOptions& options) const {
    requirePixels(image);
    std::vector<std::uint8_t> out;
    BufferWriter writer{&out};
    const int quality = clampInt(options.quality, 1, 100);
    if (!stbi_write_jpg_to_func(appendToBuffer, &writer, image.width(),
                                image.height(), 4, image.pixels(), quality)) {
        throw CompressionError(ErrorCode::EncodeFailed, "JPEG encoding failed");
    }
    return out;
}

std::vector<std::uint8_t> PngCodec::encode(
    const Image& image, const EncodeOptions& options) const {
    requirePixels(image);
    LodePNGState state;
    lodepng_state_init(&state);
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;
    state.encoder.auto_convert = 1;
    const int level = clampInt(options.pngLevel, 0, 9);
    LodePNGCompressSettings& z = state.encoder.zlibsettings;
    if (level == 0) {
        z.use_lz77 = 0;
        z.btype = 1;
    } else {
        z.use_lz77 = 1;
        z.btype = 2;
        z.windowsize = level >= 7 ? 32768u : (level >= 4 ? 8192u : 2048u);
        z.nicematch = level >= 7 ? 258u : 128u;
        z.lazymatching = 1;
    }
    unsigned char* encoded = nullptr;
    std::size_t encodedSize = 0;
    const unsigned err =
        lodepng_encode(&encoded, &encodedSize, image.pixels(),
                       static_cast<unsigned>(image.width()),
                       static_cast<unsigned>(image.height()), &state);
    lodepng_state_cleanup(&state);
    if (err != 0) {
        throw CompressionError(ErrorCode::EncodeFailed,
                               std::string("PNG encoding failed: ") +
                                   lodepng_error_text(err));
    }
    std::vector<std::uint8_t> out(encoded, encoded + encodedSize);
    std::free(encoded);
    return out;
}

std::vector<std::uint8_t> BmpCodec::encode(
    const Image& image, const EncodeOptions&) const {
    requirePixels(image);
    std::vector<std::uint8_t> out;
    BufferWriter writer{&out};
    if (!stbi_write_bmp_to_func(appendToBuffer, &writer, image.width(),
                                image.height(), 4, image.pixels())) {
        throw CompressionError(ErrorCode::EncodeFailed, "BMP encoding failed");
    }
    return out;
}

std::vector<std::uint8_t> TgaCodec::encode(
    const Image& image, const EncodeOptions&) const {
    requirePixels(image);
    std::vector<std::uint8_t> out;
    BufferWriter writer{&out};
    if (!stbi_write_tga_to_func(appendToBuffer, &writer, image.width(),
                                image.height(), 4, image.pixels())) {
        throw CompressionError(ErrorCode::EncodeFailed, "TGA encoding failed");
    }
    return out;
}

std::vector<std::uint8_t> QoiCodec::encode(
    const Image& image, const EncodeOptions&) const {
    requirePixels(image);
    return qoi::encode(image);
}

} // namespace presspng
