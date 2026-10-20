#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "presspng/image.h"

namespace presspng {

// Where a preview's pixels come from. One subclass per origin, so a new
// origin (clipboard, network, ...) is a new class, not new preview logic.
class IPreviewSource {
public:
    virtual ~IPreviewSource() = default;

    virtual const char* describe() const = 0;

    // Full-size decode. Throws CompressionError on any failure.
    virtual Image fetch() const = 0;
};

class FilePreviewSource final : public IPreviewSource {
public:
    explicit FilePreviewSource(std::filesystem::path path);

    const char* describe() const override { return "file"; }
    Image fetch() const override;
    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class BytesPreviewSource final : public IPreviewSource {
public:
    explicit BytesPreviewSource(std::vector<std::uint8_t> bytes);

    const char* describe() const override { return "bytes"; }
    Image fetch() const override;

private:
    std::vector<std::uint8_t> bytes_;
};

struct PreviewRequest {
    int maxWidth = 0; // maximum thumbnail size in pixels
    int maxHeight = 0;
};

struct PreviewFrame {
    bool ok = false;
    Image thumbnail; // aspect-fitted RGBA, never larger than the box
    int sourceWidth = 0;
    int sourceHeight = 0;
    double scale = 1.0; // thumbnail width / source width
    std::string message;
};

// Facade: decode through a source, aspect-fit into the request box.
// Exceptions never cross this boundary; failures land in frame.message.
class PreviewService {
public:
    PreviewFrame make(const IPreviewSource& source,
                      const PreviewRequest& request) const;
};

} // namespace presspng
