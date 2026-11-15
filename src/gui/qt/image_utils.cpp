#include "image_utils.h"

namespace presspng::gui {

QImage toQImage(const presspng::Image& image) {
    if (image.empty()) {
        return QImage();
    }
    return QImage(image.pixels(), image.width(), image.height(),
                  image.width() * 4, QImage::Format_RGBA8888)
        .copy();
}

} // namespace presspng::gui
