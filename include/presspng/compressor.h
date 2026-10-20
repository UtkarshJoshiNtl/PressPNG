#pragma once

#include <functional>
#include <memory>
#include <string>

#include "presspng/compression_request.h"
#include "presspng/compression_result.h"
#include "presspng/size_solver.h"

namespace presspng {

// Facade: one call turns a request into a written file + result.
// Exceptions never cross this boundary; errors land in result.message.
class Compressor {
public:
    using ProgressFn = std::function<void(const std::string&)>;

    explicit Compressor(std::unique_ptr<ISizeSolver> solver = {});

    CompressionResult compress(const CompressionRequest& request,
                               ProgressFn progress = nullptr,
                               const CancelToken* cancel = nullptr) const;

private:
    std::unique_ptr<ISizeSolver> solver_;
};

} // namespace presspng
