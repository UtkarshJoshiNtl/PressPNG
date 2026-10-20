#pragma once

#include <cstdint>
#include <vector>

#include "presspng/image.h"

namespace presspng {

// Tiny QOI (Quite OK Image) decoder/encoder to prove the extension point.
// https://qoiformat.org/ — very small, lossless, zero external deps.
namespace qoi {

struct Header {
    uint32_t width;
    uint32_t height;
    uint8_t channels;
    uint8_t colorspace;
};

bool decode(const std::vector<std::uint8_t>& in, Image& out);
std::vector<std::uint8_t> encode(const Image& image);

} // namespace qoi

} // namespace presspng
