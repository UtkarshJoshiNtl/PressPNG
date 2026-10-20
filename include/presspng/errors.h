#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace presspng {

enum class ErrorCode {
    BadRequest,
    IoRead,
    IoWrite,
    Unsupported,
    DecodeFailed,
    EncodeFailed,
    Cancelled,
    Internal,
};

class CompressionError : public std::runtime_error {
public:
    CompressionError(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

} // namespace presspng
