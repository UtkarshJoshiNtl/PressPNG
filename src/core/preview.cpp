#include "presspng/preview.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "presspng/errors.h"
#include "presspng/image_loader.h"

namespace presspng {

FilePreviewSource::FilePreviewSource(std::filesystem::path path)
    : path_(std::move(path)) {}

Image FilePreviewSource::fetch() const {
    return ImageLoader::decode(ImageLoader::readFile(path_));
}

BytesPreviewSource::BytesPreviewSource(std::vector<std::uint8_t> bytes)
    : bytes_(std::move(bytes)) {}

Image BytesPreviewSource::fetch() const {
    return ImageLoader::decode(bytes_);
}

PreviewFrame PreviewService::make(const IPreviewSource& source,
                                  const PreviewRequest& request) const {
    PreviewFrame frame;
    if (request.maxWidth <= 0 || request.maxHeight <= 0) {
        frame.message = "preview box must be positive";
        return frame;
    }

    try {
        Image image = source.fetch();
        frame.sourceWidth = image.width();
        frame.sourceHeight = image.height();

        // Fit inside the box while keeping the aspect ratio; never upscale,
        // a preview of a small image stays at its true size.
        const double fitX =
            static_cast<double>(request.maxWidth) / image.width();
        const double fitY =
            static_cast<double>(request.maxHeight) / image.height();
        frame.scale = std::min({1.0, fitX, fitY});

        int width = std::max(
            1, static_cast<int>(std::lround(image.width() * frame.scale)));
        int height = std::max(
            1, static_cast<int>(std::lround(image.height() * frame.scale)));

        frame.thumbnail = (width == image.width() && height == image.height())
                              ? std::move(image)
                              : image.resized(width, height);
        frame.ok = true;
        frame.message = std::string("preview from ") + source.describe();
    } catch (const CompressionError& error) {
        frame.ok = false;
        frame.message = error.what();
    } catch (const std::exception& error) {
        frame.ok = false;
        frame.message = std::string("unexpected error: ") + error.what();
    }
    return frame;
}

} // namespace presspng
