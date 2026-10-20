#pragma once

#include <cstdint>
#include <vector>

#include "presspng/image.h"

namespace presspng {

// Formats we are able to write.
enum class EncodedFormat { Jpeg, Png, Bmp, Tga, Qoi };

// What the user selected in the UI.
enum class UserFormat { Keep, Jpeg, Png, Qoi };

// Formats stb_image is able to read (sniffed from magic bytes).
enum class InputFormat { Jpeg, Png, Bmp, Tga, Gif, Hdr, Psd, Pnm, Qoi, Unknown };

struct EncodeOptions {
    int quality = 85;    // JPEG only, clamped to 1..100
    int pngLevel = 9;    // PNG only, clamped to 0..9
};

// Abstract writer: one concrete subclass per output format.
class IImageCodec {
public:
    virtual ~IImageCodec() = default;

    virtual EncodedFormat format() const = 0;
    virtual const char* formatName() const = 0;
    virtual const char* extension() const = 0;
    virtual bool hasQualityKnob() const = 0;

    virtual std::vector<std::uint8_t> encode(const Image& image,
                                             const EncodeOptions& options) const = 0;
};

} // namespace presspng
