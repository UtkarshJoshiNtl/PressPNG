#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace presspng {

enum class Rotate { None, Cw90, Cw180, Cw270 };

// Decoded 8-bit RGBA pixel buffer. Move-only, owns its memory (RAII).
class Image {
public:
    Image() = default;
    Image(int width, int height);

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&&) noexcept = default;
    Image& operator=(Image&&) noexcept = default;
    ~Image() = default;

    bool empty() const noexcept { return pixels_.empty(); }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    static constexpr int channels() noexcept { return 4; }
    std::size_t byteSize() const noexcept { return pixels_.size(); }

    const std::uint8_t* pixels() const noexcept { return pixels_.data(); }
    std::uint8_t* pixels() noexcept { return pixels_.data(); }

    std::array<std::uint8_t, 4> pixel(int x, int y) const;
    void setPixel(int x, int y, std::uint8_t r, std::uint8_t g,
                  std::uint8_t b, std::uint8_t a = 255);

    // Box-filter resize, always derived from this image.
    Image resized(int newWidth, int newHeight) const;

    // Geometry primitives. Crop uses pixel coordinates and clamps to bounds.
    Image cropped(int x, int y, int width, int height) const;
    Image rotated(Rotate rotate) const;
    Image flipped(bool horizontal) const;

    static constexpr int kMinDimension = 16;

private:
    bool inBounds(int x, int y) const noexcept;

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

} // namespace presspng
