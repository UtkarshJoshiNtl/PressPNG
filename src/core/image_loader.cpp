#include "presspng/image_loader.h"

#include <cstring>
#include <fstream>
#include <memory>
#include <system_error>

#include "presspng/errors.h"

// stb_image lives in third_party; declared here because the implementation
// unit is stb_impl.cpp.
extern "C" {
unsigned char* stbi_load_from_memory(const unsigned char* buffer, int len,
                                     int* x, int* y, int* channels_in_file,
                                     int desired_channels);
const char* stbi_failure_reason(void);
void stbi_image_free(void* retval_from_stbi_load);
}

#include "presspng/qoi.h"

namespace presspng {

namespace {

std::string narrow(const std::filesystem::path& path) {
    return path.u8string();
}

bool startsWith(const std::vector<std::uint8_t>& bytes, const char* magic,
                std::size_t len) {
    if (bytes.size() < len) {
        return false;
    }
    return std::memcmp(bytes.data(), magic, len) == 0;
}

} // namespace

std::vector<std::uint8_t> ImageLoader::readFile(const std::filesystem::path& path) {
    if (path.empty()) {
        throw CompressionError(ErrorCode::IoRead, "no input file selected");
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        throw CompressionError(ErrorCode::IoRead,
                               "file not found: " + narrow(path));
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw CompressionError(ErrorCode::IoRead,
                               "cannot open file: " + narrow(path));
    }
    in.seekg(0, std::ios::end);
    const std::streampos end = in.tellg();
    if (end < 0) {
        throw CompressionError(ErrorCode::IoRead,
                               "cannot determine file size: " + narrow(path));
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        if (!in) {
            throw CompressionError(ErrorCode::IoRead,
                                   "read failed: " + narrow(path));
        }
    }
    return bytes;
}

void ImageLoader::writeFile(const std::filesystem::path& path,
                            const std::vector<std::uint8_t>& bytes) {
    if (path.empty()) {
        throw CompressionError(ErrorCode::IoWrite, "no output path given");
    }
    std::error_code ec;
    const std::filesystem::path dir = path.parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir, ec);
        // A failure here surfaces as an open failure below.
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw CompressionError(ErrorCode::IoWrite,
                               "cannot write file: " + narrow(path));
    }
    if (!bytes.empty()) {
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    out.flush();
    if (!out) {
        throw CompressionError(ErrorCode::IoWrite,
                               "write failed: " + narrow(path));
    }
}

InputFormat ImageLoader::sniff(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() >= 8 &&
        bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E &&
        bytes[3] == 0x47 && bytes[4] == 0x0D && bytes[5] == 0x0A &&
        bytes[6] == 0x1A && bytes[7] == 0x0A) {
        return InputFormat::Png;
    }
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 &&
        bytes[2] == 0xFF) {
        return InputFormat::Jpeg;
    }
    if (startsWith(bytes, "BM", 2)) {
        return InputFormat::Bmp;
    }
    if (startsWith(bytes, "GIF87a", 6) || startsWith(bytes, "GIF89a", 6)) {
        return InputFormat::Gif;
    }
    if (startsWith(bytes, "#?RADIANCE", 10) || startsWith(bytes, "#?RGBE", 6)) {
        return InputFormat::Hdr;
    }
    if (startsWith(bytes, "8BPS", 4)) {
        return InputFormat::Psd;
    }
    if (bytes.size() >= 2 && bytes[0] == 'P' && bytes[1] >= '1' &&
        bytes[1] <= '6') {
        return InputFormat::Pnm;
    }
    if (bytes.size() >= 4 && bytes[0] == 'q' && bytes[1] == 'o' &&
        bytes[2] == 'i' && bytes[3] == 'f') {
        return InputFormat::Qoi;
    }
    // TGA has no magic number; check its header fields.
    // TGA has no magic number; check its header fields.
    if (bytes.size() >= 18) {
        const std::uint8_t imageType = bytes[2];
        const std::uint8_t depth = bytes[16];
        const bool typeOk = imageType == 2 || imageType == 3 ||
                            imageType == 10 || imageType == 11;
        const bool depthOk = depth == 8 || depth == 16 || depth == 24 ||
                             depth == 32;
        const std::uint16_t width =
            static_cast<std::uint16_t>(bytes[12]) |
            (static_cast<std::uint16_t>(bytes[13]) << 8);
        const std::uint16_t height =
            static_cast<std::uint16_t>(bytes[14]) |
            (static_cast<std::uint16_t>(bytes[15]) << 8);
        if (typeOk && depthOk && width > 0 && height > 0 &&
            bytes[1] <= 2) {
            return InputFormat::Tga;
        }
    }
    return InputFormat::Unknown;
}

Image ImageLoader::decode(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        throw CompressionError(ErrorCode::DecodeFailed, "file is empty");
    }
    // Try QOI first (magic only, handled by our decoder).
    if (bytes.size() >= 4 && bytes[0] == 'q' && bytes[1] == 'o' &&
        bytes[2] == 'i' && bytes[3] == 'f') {
        Image image;
        if (qoi::decode(bytes, image)) {
            return image;
        }
        throw CompressionError(ErrorCode::DecodeFailed,
                               "cannot decode QOI image");
    }
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()),
                              &width, &height, &sourceChannels, 4),
        &stbi_image_free);
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        throw CompressionError(
            ErrorCode::DecodeFailed,
            reason ? std::string("cannot decode image: ") + reason
                   : std::string("cannot decode image data"));
    }
    if (width <= 0 || height <= 0) {
        throw CompressionError(ErrorCode::DecodeFailed,
                               "decoded image has invalid dimensions");
    }
    Image image(width, height);
    std::memcpy(image.pixels(), pixels.get(), image.byteSize());
    return image;
}

} // namespace presspng
