#pragma once

#include "presspng/codec.h"

namespace presspng {

class JpegCodec final : public IImageCodec {
public:
    EncodedFormat format() const override { return EncodedFormat::Jpeg; }
    const char* formatName() const override { return "JPEG"; }
    const char* extension() const override { return ".jpg"; }
    bool hasQualityKnob() const override { return true; }
    std::vector<std::uint8_t> encode(const Image& image,
                                     const EncodeOptions& options) const override;
};

class PngCodec final : public IImageCodec {
public:
    EncodedFormat format() const override { return EncodedFormat::Png; }
    const char* formatName() const override { return "PNG"; }
    const char* extension() const override { return ".png"; }
    bool hasQualityKnob() const override { return false; }
    std::vector<std::uint8_t> encode(const Image& image,
                                     const EncodeOptions& options) const override;
};

class BmpCodec final : public IImageCodec {
public:
    EncodedFormat format() const override { return EncodedFormat::Bmp; }
    const char* formatName() const override { return "BMP"; }
    const char* extension() const override { return ".bmp"; }
    bool hasQualityKnob() const override { return false; }
    std::vector<std::uint8_t> encode(const Image& image,
                                     const EncodeOptions& options) const override;
};

class TgaCodec final : public IImageCodec {
public:
    EncodedFormat format() const override { return EncodedFormat::Tga; }
    const char* formatName() const override { return "TGA"; }
    const char* extension() const override { return ".tga"; }
    bool hasQualityKnob() const override { return false; }
    std::vector<std::uint8_t> encode(const Image& image,
                                     const EncodeOptions& options) const override;
};

class QoiCodec final : public IImageCodec {
public:
    EncodedFormat format() const override { return EncodedFormat::Qoi; }
    const char* formatName() const override { return "QOI"; }
    const char* extension() const override { return ".qoi"; }
    bool hasQualityKnob() const override { return false; }
    std::vector<std::uint8_t> encode(const Image& image,
                                     const EncodeOptions& options) const override;
};

} // namespace presspng
