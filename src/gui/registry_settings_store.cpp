#include "registry_settings_store.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>

namespace presspng {

namespace {

std::string readString(HKEY key, const wchar_t* name) {
    wchar_t buffer[1024] = L"";
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    if (RegQueryValueExW(key, name, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buffer), &size) !=
            ERROR_SUCCESS ||
        type != REG_SZ) {
        return {};
    }
    const std::wstring wide(buffer);
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                           static_cast<int>(wide.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()), &out[0], needed,
                        nullptr, nullptr);
    return out;
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                        static_cast<int>(text.size()), &out[0], needed);
    return out;
}

void writeString(HKEY key, const wchar_t* name, const std::string& value) {
    const std::wstring wide = toWide(value);
    RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(wide.c_str()),
                   static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
}

} // namespace

RegistrySettingsStore::RegistrySettingsStore(const wchar_t* keyPath)
    : keyPath_(keyPath) {}

AppSettings RegistrySettingsStore::load() const {
    AppSettings settings;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath_, 0, KEY_READ, &key) !=
        ERROR_SUCCESS) {
        return settings; // first run: defaults
    }

    settings.lastDirectory = readString(key, L"LastDirectory");
    const std::string size = readString(key, L"TargetSize");
    if (!size.empty()) {
        settings.targetSizeText = size;
    }
    const std::string format = readString(key, L"Format");
    if (format == "jpeg") {
        settings.format = UserFormat::Jpeg;
    } else if (format == "png") {
        settings.format = UserFormat::Png;
    } else {
        settings.format = UserFormat::Keep;
    }

    RegCloseKey(key);
    return settings;
}

void RegistrySettingsStore::save(const AppSettings& settings) const {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath_, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS) {
        return; // settings are a convenience; never fail the app over them
    }

    writeString(key, L"LastDirectory", settings.lastDirectory);
    writeString(key, L"TargetSize", settings.targetSizeText);

    const char* format = "keep";
    if (settings.format == UserFormat::Jpeg) {
        format = "jpeg";
    } else if (settings.format == UserFormat::Png) {
        format = "png";
    }
    writeString(key, L"Format", format);

    RegCloseKey(key);
}

} // namespace presspng
