#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace presspng::gui {

// Line icons drawn at runtime, so the app needs neither an icon theme nor a
// .qrc. Names: open, rotateLeft, rotateRight, flipH, flipV, crop, lock,
// folder, copy, diff, compress.
QIcon makeIcon(const QString& name, const QColor& color);

} // namespace presspng::gui
