#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "presspng/codec.h"

namespace presspng {

// Values the app remembers between runs.
struct AppSettings {
    std::string lastDirectory;
    std::string targetSizeText = "500";
    UserFormat format = UserFormat::Keep;
};

// One interface per storage location (registry, ini, in-memory, ...).
class ISettingsStore {
public:
    virtual ~ISettingsStore() = default;

    virtual AppSettings load() const = 0;
    virtual void save(const AppSettings& settings) const = 0;
};

// Default no-op store: keeps the core usable on any platform and in tests.
class NullSettingsStore final : public ISettingsStore {
public:
    AppSettings load() const override { return AppSettings{}; }
    void save(const AppSettings&) const override {}
};

} // namespace presspng
