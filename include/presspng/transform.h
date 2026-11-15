#pragma once

#include "presspng/image.h"

namespace presspng {

// Geometry the user asked for, applied before compression.
//
// Order is fixed: crop, then rotate/flip, then resize. Crop is normalized
// (0..1) so it survives a later resolution change; the size fields are a
// ceiling per axis and never upscale.
struct TransformSpec {
    int outputWidth = 0;  // 0 => keep width
    int outputHeight = 0; // 0 => keep height

    double cropX = 0.0;
    double cropY = 0.0;
    double cropW = 1.0;
    double cropH = 1.0;

    Rotate rotate = Rotate::None;
    bool flipH = false;
    bool flipV = false;

    bool isIdentity() const noexcept;
};

// Applies the spec to a copy of the source. Throws CompressionError on
// impossible input (empty image, degenerate crop).
Image applyTransform(const Image& source, const TransformSpec& spec);

} // namespace presspng
