#include "presspng/format_bytes.h"

#include <cstdio>

namespace presspng {

std::string formatBytes(std::uint64_t bytes) {
    char buffer[64];
    if (bytes < 1000ULL) {
        std::snprintf(buffer, sizeof(buffer), "%llu B",
                      static_cast<unsigned long long>(bytes));
    } else if (bytes < 1000000ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB",
                      static_cast<double>(bytes) / 1000.0);
    } else if (bytes < 1000000000ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.2f MB",
                      static_cast<double>(bytes) / 1000000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f GB",
                      static_cast<double>(bytes) / 1000000000.0);
    }
    return buffer;
}

} // namespace presspng
