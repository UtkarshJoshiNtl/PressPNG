#pragma once

#include <QSettings>

#include "presspng/settings.h"

namespace presspng::gui {

// QSettings-backed ISettingsStore: registry on Windows, ini elsewhere.
class QtSettingsStore final : public presspng::ISettingsStore {
public:
    QtSettingsStore();

    presspng::AppSettings load() const override;
    void save(const presspng::AppSettings& settings) const override;

private:
    mutable QSettings settings_;
};

} // namespace presspng::gui
