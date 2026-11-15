#pragma once

#include <QImage>

#include "presspng/image.h"

namespace presspng::gui {

// RGBA8 -> QImage; Qt's RGBA8888 byte order matches presspng::Image.
QImage toQImage(const presspng::Image& image);

} // namespace presspng::gui
