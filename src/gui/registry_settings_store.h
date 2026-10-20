// Windows registry implementation of ISettingsStore. Kept out of the core
// library so the core stays platform-neutral; the GUI links it in.
#pragma once

#include "presspng/settings.h"

namespace presspng {

class RegistrySettingsStore final : public ISettingsStore {
public:
    // keyPath defaults to HKEY_CURRENT_USER\Software\PressPNG.
    explicit RegistrySettingsStore(
        const wchar_t* keyPath = L"Software\\PressPNG");

    AppSettings load() const override;
    void save(const AppSettings& settings) const override;

private:
    const wchar_t* keyPath_;
};

} // namespace presspng
