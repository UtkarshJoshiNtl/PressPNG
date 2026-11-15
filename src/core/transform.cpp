#include "presspng/transform.h"

#include <algorithm>
#include <cmath>

#include "presspng/errors.h"

namespace presspng {

namespace {

constexpr double kCropEpsilon = 1e-4;

} // namespace

bool TransformSpec::isIdentity() const noexcept {
    return outputWidth == 0 && outputHeight == 0 && rotate == Rotate::None &&
           !flipH && !flipV && std::abs(cropX) < kCropEpsilon &&
           std::abs(cropY) < kCropEpsilon &&
           std::abs(cropW - 1.0) < kCropEpsilon &&
           std::abs(cropH - 1.0) < kCropEpsilon;
}

Image applyTransform(const Image& source, const TransformSpec& spec) {
    if (source.empty()) {
        throw CompressionError(ErrorCode::BadRequest, "nothing to transform");
    }

    Image current = source.cropped(
        static_cast<int>(std::lround(spec.cropX * source.width())),
        static_cast<int>(std::lround(spec.cropY * source.height())),
        static_cast<int>(std::lround(spec.cropW * source.width())),
        static_cast<int>(std::lround(spec.cropH * source.height())));

    if (spec.rotate != Rotate::None) {
        current = current.rotated(spec.rotate);
    }
    if (spec.flipH) {
        current = current.flipped(true);
    }
    if (spec.flipV) {
        current = current.flipped(false);
    }

    int width = current.width();
    int height = current.height();
    if (spec.outputWidth > 0 && spec.outputHeight > 0) {
        width = spec.outputWidth;
        height = spec.outputHeight;
    } else if (spec.outputWidth > 0) {
        width = spec.outputWidth;
        height = static_cast<int>(std::lround(
            static_cast<double>(current.height()) * width / current.width()));
    } else if (spec.outputHeight > 0) {
        height = spec.outputHeight;
        width = static_cast<int>(std::lround(
            static_cast<double>(current.width()) * height / current.height()));
    }

    // The requested size is a ceiling: a compressor never upscales.
    if (spec.outputWidth > 0) {
        width = std::min(width, current.width());
    }
    if (spec.outputHeight > 0) {
        height = std::min(height, current.height());
    }
    width = std::max(Image::kMinDimension, width);
    height = std::max(Image::kMinDimension, height);

    if (width != current.width() || height != current.height()) {
        current = current.resized(width, height);
    }
    return current;
}

} // namespace presspng
