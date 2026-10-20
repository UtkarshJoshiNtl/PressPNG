#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "presspng/codec.h"
#include "presspng/image.h"

namespace presspng {

using ProgressFn = std::function<void(const std::string&)>;

// Shared between the UI thread and the worker: the worker checks it
// between encode steps, the UI sets it when the user presses Cancel.
class CancelToken {
public:
    void cancel() noexcept { flag_.store(true, std::memory_order_relaxed); }
    void reset() noexcept { flag_.store(false, std::memory_order_relaxed); }
    bool cancelled() const noexcept {
        return flag_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<bool> flag_{false};
};

struct SolvedImage {
    bool targetMet = false;
    std::vector<std::uint8_t> bytes;
    EncodeOptions options{};
    double scale = 1.0;
    std::string message;
};

// Strategy interface: find encoded bytes that fit a byte budget.
class ISizeSolver {
public:
    virtual ~ISizeSolver() = default;

    virtual const char* name() const = 0;

    // Never throws for "target unreachable"; that is targetMet == false
    // with the smallest encoding produced. Throws only on encoder failure
    // or when the cancel token is set.
    virtual SolvedImage solve(const Image& source, const IImageCodec& codec,
                              std::uint64_t targetBytes,
                              const ProgressFn& progress,
                              const CancelToken* cancel = nullptr) const = 0;
};

} // namespace presspng
