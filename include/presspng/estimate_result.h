#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "presspng/codec.h"

namespace presspng {

// What compress() would produce, without touching the disk. The encoded
// bytes are included so a UI can decode and preview the real result.
struct EstimateResult {
    bool ok = false;
    bool targetMet = false;

    std::uint64_t inputBytes = 0;
    std::uint64_t outputBytes = 0;

    EncodedFormat encodedFormat = EncodedFormat::Png;
    int quality = 0;
    double scale = 1.0;

    std::vector<std::uint8_t> bytes;
    std::string message;
};

} // namespace presspng
