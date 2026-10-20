#include "presspng/target_size_solver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "presspng/errors.h"
#include "presspng/format_bytes.h"

namespace presspng {

namespace {

constexpr int kMaxDownscaleSteps = 4;

struct Tracked {
    bool valid = false;
    std::vector<std::uint8_t> bytes;
    EncodeOptions options{};
    double scale = 1.0;
    std::uint64_t size = 0;
};

void trackSmallest(Tracked& smallest, std::vector<std::uint8_t> bytes,
                   const EncodeOptions& options, double scale) {
    const std::uint64_t size = bytes.size();
    if (!smallest.valid || size < smallest.size) {
        smallest.valid = true;
        smallest.size = size;
        smallest.options = options;
        smallest.scale = scale;
        smallest.bytes = std::move(bytes);
    }
}

int scaledDimension(int original, double scale) {
    const int value =
        static_cast<int>(std::lround(original * scale));
    return std::max(Image::kMinDimension, value);
}

} // namespace

SolvedImage TargetSizeSolver::solve(const Image& source,
                                    const IImageCodec& codec,
                                    std::uint64_t targetBytes,
                                    const ProgressFn& progress,
                                    const CancelToken* cancel) const {
    if (source.empty()) {
        throw CompressionError(ErrorCode::BadRequest, "nothing to compress");
    }
    if (targetBytes == 0) {
        throw CompressionError(ErrorCode::BadRequest,
                               "target size must be greater than zero");
    }

    auto report = [&](const std::string& text) {
        if (progress) {
            progress(text);
        }
    };

    Tracked smallest;
    Image current = source.resized(source.width(), source.height());
    double currentScale = 1.0;

    auto checkCancel = [&]() {
        if (cancel != nullptr && cancel->cancelled()) {
            throw CompressionError(ErrorCode::Cancelled, "cancelled by user");
        }
    };

    for (int step = 0; step <= kMaxDownscaleSteps; ++step) {
        EncodeOptions options{};
        std::vector<std::uint8_t> fitted;
        bool fit = false;

        if (codec.hasQualityKnob()) {
            int low = 1;
            int high = 100;
            int bestQuality = 0;
            std::vector<std::uint8_t> bestBytes;
            while (low <= high) {
                checkCancel();
                const int quality = low + (high - low) / 2;
                options.quality = quality;
                report(std::string(codec.formatName()) + " quality " +
                       std::to_string(quality) + " (" +
                       std::to_string(static_cast<int>(currentScale * 100.0)) +
                       "% scale)");
                auto bytes = codec.encode(current, options);
                trackSmallest(smallest, bytes, options, currentScale);
                if (bytes.size() <= targetBytes) {
                    bestQuality = quality;
                    bestBytes = std::move(bytes);
                    low = quality + 1;
                } else {
                    high = quality - 1;
                }
            }
            if (bestQuality > 0) {
                fit = true;
                options.quality = bestQuality;
                fitted = std::move(bestBytes);
            }
        } else {
            checkCancel();
            report(std::string(codec.formatName()) + " encode (" +
                   std::to_string(static_cast<int>(currentScale * 100.0)) +
                   "% scale)");
            auto bytes = codec.encode(current, options);
            trackSmallest(smallest, bytes, options, currentScale);
            if (bytes.size() <= targetBytes) {
                fit = true;
                fitted = std::move(bytes);
            }
        }

        if (fit) {
            SolvedImage result;
            result.targetMet = true;
            result.bytes = std::move(fitted);
            result.options = options;
            result.scale = currentScale;
            result.message = currentScale < 1.0
                                 ? "target met after reducing resolution"
                                 : "target met at full resolution";
            return result;
        }

        if (step == kMaxDownscaleSteps || !smallest.valid) {
            break;
        }

        // Estimate the scale that would bring the current smallest encoding
        // under budget (bytes scale roughly with pixel count), with margin.
        const double ratio =
            static_cast<double>(targetBytes) / static_cast<double>(smallest.size);
        double factor = std::sqrt(ratio) * 0.95;
        factor = std::max(0.05, std::min(factor, 0.95));

        const int newWidth =
            scaledDimension(source.width(), currentScale * factor);
        const int newHeight =
            scaledDimension(source.height(), currentScale * factor);

        if (newWidth >= current.width() && newHeight >= current.height()) {
            break; // cannot get any smaller
        }

        current = source.resized(newWidth, newHeight);
        currentScale = static_cast<double>(newWidth) / source.width();
    }

    SolvedImage result;
    result.targetMet = false;
    result.bytes = std::move(smallest.bytes);
    result.options = smallest.options;
    result.scale = smallest.scale;
    result.message = "could not reach " + formatBytes(targetBytes) +
                     "; smallest possible output here is " +
                     formatBytes(smallest.size);
    return result;
}

const char* TargetSizeSolver::name() const {
    return "binary-search + downscale ladder";
}

} // namespace presspng
