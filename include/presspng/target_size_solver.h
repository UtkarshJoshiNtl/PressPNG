#pragma once

#include "presspng/size_solver.h"

namespace presspng {

// Finds the highest JPEG quality (or PNG settings) that fits the budget,
// shrinking the image in steps when quality alone is not enough.
class TargetSizeSolver final : public ISizeSolver {
public:
    const char* name() const override;

    SolvedImage solve(const Image& source, const IImageCodec& codec,
                      std::uint64_t targetBytes,
                      const ProgressFn& progress,
                      const CancelToken* cancel = nullptr) const override;
};

} // namespace presspng
