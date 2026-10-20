#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "presspng/codec.h"

namespace presspng {

struct CompressionResult {
    bool ok = false;        // operation completed without errors
    bool targetMet = false; // outputBytes <= requested target
    bool cancelled = false; // user pressed Cancel before finishing

    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::uint64_t inputBytes = 0;
    std::uint64_t outputBytes = 0;
    double reductionPercent = 0.0;

    EncodedFormat encodedFormat = EncodedFormat::Png;
    int quality = 0;        // JPEG quality used (0 for formats without one)
    double scale = 1.0;     // resolution fraction of the output
    int elapsedMs = 0;
    std::string message;
};

} // namespace presspng
