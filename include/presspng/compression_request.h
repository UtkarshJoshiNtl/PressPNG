#pragma once

#include <cstdint>
#include <filesystem>

#include "presspng/codec.h"

namespace presspng {

struct CompressionRequest {
    std::filesystem::path input;
    std::filesystem::path outputDirectory; // empty => alongside the input
    std::filesystem::path outputPath;      // optional exact destination chosen by the user
    std::uint64_t targetBytes = 0;
    UserFormat format = UserFormat::Keep;
};

} // namespace presspng
