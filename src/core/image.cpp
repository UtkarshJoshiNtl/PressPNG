#include "presspng/image.h"

#include <algorithm>
#include <cstdint>

#include "presspng/errors.h"

namespace presspng {

namespace {
constexpr std::int64_t kMaxPixels = 268435456LL; // 1 GiB of RGBA
}

Image::Image(int width, int height) : width_(width), height_(height) {
    if (width <= 0 || height <= 0) {
        throw CompressionError(ErrorCode::BadRequest,
                               "image dimensions must be positive");
    }
    const std::int64_t pixels = static_cast<std::int64_t>(width) * height;
    if (pixels > kMaxPixels) {
        throw CompressionError(ErrorCode::BadRequest,
                               "image is too large to fit in memory");
    }
    pixels_.assign(static_cast<std::size_t>(pixels) * 4u, 0u);
}

bool Image::inBounds(int x, int y) const noexcept {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

std::array<std::uint8_t, 4> Image::pixel(int x, int y) const {
    if (!inBounds(x, y)) {
        throw CompressionError(ErrorCode::BadRequest, "pixel out of bounds");
    }
    const std::size_t i = (static_cast<std::size_t>(y) * width_ + x) * 4u;
    return {pixels_[i], pixels_[i + 1], pixels_[i + 2], pixels_[i + 3]};
}

void Image::setPixel(int x, int y, std::uint8_t r, std::uint8_t g,
                     std::uint8_t b, std::uint8_t a) {
    if (!inBounds(x, y)) {
        throw CompressionError(ErrorCode::BadRequest, "pixel out of bounds");
    }
    const std::size_t i = (static_cast<std::size_t>(y) * width_ + x) * 4u;
    pixels_[i] = r;
    pixels_[i + 1] = g;
    pixels_[i + 2] = b;
    pixels_[i + 3] = a;
}

Image Image::resized(int newWidth, int newHeight) const {
    if (empty()) {
        throw CompressionError(ErrorCode::BadRequest, "cannot resize an empty image");
    }
    if (newWidth <= 0 || newHeight <= 0) {
        throw CompressionError(ErrorCode::BadRequest,
                               "resize dimensions must be positive");
    }
    if (newWidth == width_ && newHeight == height_) {
        Image copy(width_, height_);
        copy.pixels_ = pixels_;
        return copy;
    }

    Image out(newWidth, newHeight);
    for (int y = 0; y < newHeight; ++y) {
        int y0 = static_cast<int>(static_cast<std::int64_t>(y) * height_ / newHeight);
        int y1 = static_cast<int>(static_cast<std::int64_t>(y + 1) * height_ / newHeight);
        y1 = std::min(std::max(y1, y0 + 1), height_);
        for (int x = 0; x < newWidth; ++x) {
            int x0 = static_cast<int>(static_cast<std::int64_t>(x) * width_ / newWidth);
            int x1 = static_cast<int>(static_cast<std::int64_t>(x + 1) * width_ / newWidth);
            x1 = std::min(std::max(x1, x0 + 1), width_);

            std::uint64_t r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const std::size_t row = static_cast<std::size_t>(yy) * width_;
                for (int xx = x0; xx < x1; ++xx) {
                    const std::size_t i = (row + xx) * 4u;
                    r += pixels_[i];
                    g += pixels_[i + 1];
                    b += pixels_[i + 2];
                    a += pixels_[i + 3];
                    ++count;
                }
            }
            out.setPixel(x, y,
                         static_cast<std::uint8_t>(r / count),
                         static_cast<std::uint8_t>(g / count),
                         static_cast<std::uint8_t>(b / count),
                         static_cast<std::uint8_t>(a / count));
        }
    }
    return out;
}

} // namespace presspng
