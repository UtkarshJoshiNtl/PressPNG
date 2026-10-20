#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "presspng/codec.h"
#include "presspng/image.h"

namespace presspng {

// Reads raw bytes from disk and decodes them to an Image (any format
// stb_image understands). All failures surface as CompressionError.
class ImageLoader {
public:
    static std::vector<std::uint8_t> readFile(const std::filesystem::path& path);
    static void writeFile(const std::filesystem::path& path,
                          const std::vector<std::uint8_t>& bytes);

    static InputFormat sniff(const std::vector<std::uint8_t>& bytes);
    static Image decode(const std::vector<std::uint8_t>& bytes);
};

} // namespace presspng
