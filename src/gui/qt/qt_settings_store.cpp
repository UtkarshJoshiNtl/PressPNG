#include "qt_settings_store.h"

#include <QString>

#include "presspng/codec.h"

namespace presspng::gui {

namespace {

int formatToInt(presspng::UserFormat format) {
    switch (format) {
        case presspng::UserFormat::Jpeg: return 1;
        case presspng::UserFormat::Png:  return 2;
        case presspng::UserFormat::Qoi:  return 3;
        case presspng::UserFormat::Keep: return 0;
    }
    return 0;
}

presspng::UserFormat intToFormat(int value) {
    switch (value) {
        case 1: return presspng::UserFormat::Jpeg;
        case 2: return presspng::UserFormat::Png;
        case 3: return presspng::UserFormat::Qoi;
        default: return presspng::UserFormat::Keep;
    }
}

} // namespace

QtSettingsStore::QtSettingsStore() : settings_() {}

presspng::AppSettings QtSettingsStore::load() const {
    presspng::AppSettings settings;
    settings.lastDirectory =
        settings_.value(QStringLiteral("lastDirectory")).toString().toStdString();
    settings.targetSizeText =
        settings_.value(QStringLiteral("targetSizeText"), QStringLiteral("500"))
            .toString()
            .toStdString();
    settings.format =
        intToFormat(settings_.value(QStringLiteral("format"), 0).toInt());
    return settings;
}

void QtSettingsStore::save(const presspng::AppSettings& settings) const {
    settings_.setValue(QStringLiteral("lastDirectory"),
                       QString::fromStdString(settings.lastDirectory));
    settings_.setValue(QStringLiteral("targetSizeText"),
                       QString::fromStdString(settings.targetSizeText));
    settings_.setValue(QStringLiteral("format"), formatToInt(settings.format));
}

} // namespace presspng::gui
