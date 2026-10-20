#pragma once

#include <cstdint>
#include <string>

namespace presspng {

// "1234567" -> "1.2 MB" (decimal units, 1 KB = 1000 bytes).
std::string formatBytes(std::uint64_t bytes);

} // namespace presspng
