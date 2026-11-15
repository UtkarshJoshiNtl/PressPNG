#include "preview_view.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace presspng::gui {

namespace {

constexpr double kMargin = 12.0;
constexpr double kHandleRadius = 5.0;
constexpr double kMinZoom = 0.05;
constexpr double kMaxZoom = 16.0;

double clamp01(double value) {
    return std::clamp(value, 0.0, 1.0);
}

} // namespace

PreviewView::PreviewView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void PreviewView::setOriginal(const QImage& image) {
    original_ = image;
    update();
}

void PreviewView::setBefore(const QImage& image) {
    before_ = image;
    diffCacheDirty_ = true;
    update();
}

void PreviewView::setAfter(const QImage& image) {
    after_ = image;
    diffCacheDirty_ = true;
    update();
}

void PreviewView::clearAfter() {
    after_ = QImage();
    diffCacheDirty_ = true;
    update();
}

void PreviewView::clearAll() {
    original_ = before_ = after_ = QImage();
    diffCacheDirty_ = true;
    pan_ = QPointF();
    update();
}

void PreviewView::setZoomMode(ZoomMode mode) {
    zoomMode_ = mode;
    if (mode == ZoomMode::Fit) {
        pan_ = QPointF();
    }
    update();
}

void PreviewView::setZoom(double factor) {
    zoom_ = std::clamp(factor, kMinZoom, kMaxZoom);
    zoomMode_ = ZoomMode::Manual;
    update();
    emit zoomChanged(zoom_);
}

void PreviewView::zoomToFit() {
    setZoomMode(ZoomMode::Fit);
    emit zoomChanged(currentScale());
}

void PreviewView::setDiffEnabled(bool enabled) {
    diff_ = enabled;
    update();
}

void PreviewView::setWipe(double fraction) {
    wipe_ = clamp01(fraction);
    update();
}

void PreviewView::setCropEnabled(bool enabled) {
    cropEnabled_ = enabled;
    update();
}

void PreviewView::setCropRect(const QRectF& rect) {
    crop_ = rect.normalized().intersected(QRectF(0.0, 0.0, 1.0, 1.0));
    if (crop_.isEmpty()) {
        crop_ = QRectF(0.0, 0.0, 1.0, 1.0);
    }
    update();
}

void PreviewView::resetCrop() {
    crop_ = QRectF(0.0, 0.0, 1.0, 1.0);
    update();
    emit cropChanged(crop_);
}

void PreviewView::setCropAspect(double ratio) {
    cropAspect_ = ratio;
}

const QImage& PreviewView::displayImage() const {
    static const QImage kNull;
    if (cropEnabled_) {
        return original_.isNull() ? before_ : original_;
    }
    return before_.isNull() ? original_ : before_;
}

double PreviewView::currentScale() const {
    const QImage& image = displayImage();
    if (image.isNull()) {
        return 1.0;
    }
    if (zoomMode_ == ZoomMode::Manual) {
        return zoom_;
    }
    const double fitX =
        (width() - 2.0 * kMargin) / static_cast<double>(image.width());
    const double fitY =
        (height() - 2.0 * kMargin) / static_cast<double>(image.height());
    return std::max(0.01, std::min(fitX, fitY));
}

QRectF PreviewView::imageRect() const {
    const QImage& image = displayImage();
    if (image.isNull()) {
        return QRectF();
    }
    const double scale = currentScale();
    const double w = image.width() * scale;
    const double h = image.height() * scale;
    const double x = (width() - w) / 2.0 + pan_.x();
    const double y = (height() - h) / 2.0 + pan_.y();
    return QRectF(x, y, w, h);
}

QRectF PreviewView::cropWidgetRect() const {
    const QRectF r = imageRect();
    return QRectF(r.left() + crop_.left() * r.width(),
                  r.top() + crop_.top() * r.height(),
                  crop_.width() * r.width(), crop_.height() * r.height());
}

PreviewView::Handle PreviewView::hitHandle(const QPointF& pos) const {
    const QRectF r = cropWidgetRect();
    const double t = kHandleRadius + 3.0;
    const bool nearL = std::abs(pos.x() - r.left()) <= t;
    const bool nearR = std::abs(pos.x() - r.right()) <= t;
    const bool nearT = std::abs(pos.y() - r.top()) <= t;
    const bool nearB = std::abs(pos.y() - r.bottom()) <= t;
    const bool inY = pos.y() >= r.top() - t && pos.y() <= r.bottom() + t;
    const bool inX = pos.x() >= r.left() - t && pos.x() <= r.right() + t;

    if (nearL && nearT) return Handle::TL;
    if (nearR && nearT) return Handle::TR;
    if (nearL && nearB) return Handle::BL;
    if (nearR && nearB) return Handle::BR;
    if (nearL && inY) return Handle::L;
    if (nearR && inY) return Handle::R;
    if (nearT && inX) return Handle::T;
    if (nearB && inX) return Handle::B;
    return Handle::None;
}

void PreviewView::applyCropResize(Handle handle, const QPointF& pos) {
    const QRectF r = imageRect();
    const QImage& image = displayImage();
    if (r.isEmpty() || image.isNull()) {
        return;
    }

    QPointF n((pos.x() - r.left()) / r.width(), (pos.y() - r.top()) / r.height());
    n.setX(clamp01(n.x()));
    n.setY(clamp01(n.y()));

    const double iw = image.width();
    const double ih = image.height();
    const double minN = std::max(8.0 / iw, 8.0 / ih);

    const bool left = handle == Handle::L || handle == Handle::TL || handle == Handle::BL;
    const bool right = handle == Handle::R || handle == Handle::TR || handle == Handle::BR;
    const bool top = handle == Handle::T || handle == Handle::TL || handle == Handle::TR;
    const bool bottom = handle == Handle::B || handle == Handle::BL || handle == Handle::BR;
    const bool corner = (left || right) && (top || bottom);

    double l = crop_.left();
    double t = crop_.top();
    double rr = crop_.right();
    double bb = crop_.bottom();
    if (left) l = n.x();
    if (right) rr = n.x();
    if (top) t = n.y();
    if (bottom) bb = n.y();

    if (cropAspect_ > 0.0 && corner) {
        const double ax = left ? rr : l; // anchored opposite corner
        const double ay = top ? bb : t;
        double wpx = std::abs(n.x() - ax) * iw;
        double hpx = std::abs(n.y() - ay) * ih;
        if (wpx / std::max(1.0, hpx) > cropAspect_) {
            hpx = wpx / cropAspect_;
        } else {
            wpx = hpx * cropAspect_;
        }
        const double sx = (n.x() >= ax) ? 1.0 : -1.0;
        const double sy = (n.y() >= ay) ? 1.0 : -1.0;
        l = ax;
        t = ay;
        rr = ax + sx * (wpx / iw);
        bb = ay + sy * (hpx / ih);
    }

    QRectF next(QPointF(std::min(l, rr), std::min(t, bb)),
                QPointF(std::max(l, rr), std::max(t, bb)));
    if (next.width() < minN) next.setWidth(minN);
    if (next.height() < minN) next.setHeight(minN);
    if (next.left() < 0.0) next.moveLeft(0.0);
    if (next.top() < 0.0) next.moveTop(0.0);
    if (next.right() > 1.0) next.moveRight(1.0);
    if (next.bottom() > 1.0) next.moveBottom(1.0);
    crop_ = next;
}

void PreviewView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF pos = event->position();
    dragStart_ = pos;

    if (cropEnabled_) {
        const Handle hit = hitHandle(pos);
        if (hit != Handle::None) {
            handle_ = hit;
            drag_ = Drag::CropResize;
        } else if (cropWidgetRect().contains(pos)) {
            drag_ = Drag::CropMove;
            cropStart_ = crop_;
        } else {
            handle_ = Handle::BR;
            drag_ = Drag::CropResize;
            crop_ = QRectF(clamp01((pos.x() - imageRect().left()) / imageRect().width()),
                           clamp01((pos.y() - imageRect().top()) / imageRect().height()),
                           0.0, 0.0);
        }
        update();
        return;
    }

    const QRectF r = imageRect();
    if (!after_.isNull() && !r.isEmpty()) {
        const double line = r.left() + wipe_ * r.width();
        if (std::abs(pos.x() - line) <= 6.0) {
            drag_ = Drag::Wipe;
            return;
        }
    }
    drag_ = Drag::Pan;
    setCursor(Qt::ClosedHandCursor);
}

void PreviewView::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();
    const QRectF r = imageRect();

    switch (drag_) {
        case Drag::Pan: {
            pan_ += pos - dragStart_;
            dragStart_ = pos;
            update();
            break;
        }
        case Drag::Wipe: {
            if (!r.isEmpty()) {
                wipe_ = clamp01((pos.x() - r.left()) / r.width());
                update();
                emit wipeChanged(wipe_);
            }
            break;
        }
        case Drag::CropMove: {
            if (!r.isEmpty()) {
                const QPointF delta = pos - dragStart_;
                QRectF moved = cropStart_.translated(delta.x() / r.width(),
                                                     delta.y() / r.height());
                if (moved.left() < 0.0) moved.moveLeft(0.0);
                if (moved.top() < 0.0) moved.moveTop(0.0);
                if (moved.right() > 1.0) moved.moveRight(1.0);
                if (moved.bottom() > 1.0) moved.moveBottom(1.0);
                crop_ = moved;
                update();
                emit cropChanged(crop_);
            }
            break;
        }
        case Drag::CropResize: {
            applyCropResize(handle_, pos);
            update();
            emit cropChanged(crop_);
            break;
        }
        case Drag::None:
        default: {
            if (!cropEnabled_) {
                if (!after_.isNull() && !r.isEmpty() &&
                    std::abs(pos.x() - (r.left() + wipe_ * r.width())) <= 6.0) {
                    setCursor(Qt::SplitHCursor);
                } else if (zoomMode_ == ZoomMode::Manual) {
                    setCursor(Qt::OpenHandCursor);
                } else {
                    setCursor(Qt::ArrowCursor);
                }
            } else {
                const Handle hit = hitHandle(pos);
                if (hit == Handle::L || hit == Handle::R) {
                    setCursor(Qt::SizeHorCursor);
                } else if (hit == Handle::T || hit == Handle::B) {
                    setCursor(Qt::SizeVerCursor);
                } else if (hit == Handle::TL || hit == Handle::BR) {
                    setCursor(Qt::SizeFDiagCursor);
                } else if (hit == Handle::TR || hit == Handle::BL) {
                    setCursor(Qt::SizeBDiagCursor);
                } else if (cropWidgetRect().contains(pos)) {
                    setCursor(Qt::SizeAllCursor);
                } else {
                    setCursor(Qt::CrossCursor);
                }
            }
            break;
        }
    }
}

void PreviewView::mouseReleaseEvent(QMouseEvent* event) {
    if (drag_ == Drag::Pan) {
        setCursor(zoomMode_ == ZoomMode::Manual ? Qt::OpenHandCursor
                                                : Qt::ArrowCursor);
    }
    if (drag_ == Drag::CropResize || drag_ == Drag::CropMove) {
        emit cropChanged(crop_);
    }
    drag_ = Drag::None;
    handle_ = Handle::None;
    QWidget::mouseReleaseEvent(event);
}

void PreviewView::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !cropEnabled_) {
        if (zoomMode_ == ZoomMode::Fit) {
            setZoom(1.0);
        } else {
            zoomToFit();
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PreviewView::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if (delta == 0) {
        return;
    }
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const QRectF r = imageRect();
        const QImage& image = displayImage();
        if (r.isEmpty() || image.isNull()) {
            return;
        }
        // Remember which point of the image sits under the cursor so it can
        // stay there across the zoom.
        const double fx = (event->position().x() - r.left()) / r.width();
        const double fy = (event->position().y() - r.top()) / r.height();

        const double base =
            (zoomMode_ == ZoomMode::Manual) ? zoom_ : currentScale();
        setZoom(base * std::pow(1.0015, delta));

        const double scale = currentScale();
        const double w = image.width() * scale;
        const double h = image.height() * scale;
        pan_.setX(event->position().x() - fx * w - (width() - w) / 2.0);
        pan_.setY(event->position().y() - fy * h - (height() - h) / 2.0);
        update();
    } else {
        pan_ += event->pixelDelta().isNull() ? QPointF(0.0, delta / 3.0)
                                             : event->pixelDelta();
        update();
    }
    event->accept();
}

// Difference between the source and the compressed result, amplified into a
// red-on-black heatmap. Computed at native resolution and cached: downscaling
// first would smooth the compression artifacts away, and the wipe drags
// repaint often.
const QImage& PreviewView::diffImage() const {
    if (!diffCacheDirty_) {
        return diffCache_;
    }
    diffCacheDirty_ = false;
    diffCache_ = QImage();

    if (before_.isNull() || after_.isNull()) {
        return diffCache_;
    }
    QSize target = before_.size();
    if (target.width() > 1024 || target.height() > 1024) {
        target.scale(1024, 1024, Qt::KeepAspectRatio);
    }
    const QImage a = before_.scaled(target, Qt::IgnoreAspectRatio,
                                    Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGB32);
    const QImage b = after_.scaled(target, Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation)
                         .convertToFormat(QImage::Format_RGB32);

    diffCache_ = QImage(target, QImage::Format_RGB32);
    for (int y = 0; y < target.height(); ++y) {
        const QRgb* ra = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* rb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        QRgb* ro = reinterpret_cast<QRgb*>(diffCache_.scanLine(y));
        for (int x = 0; x < target.width(); ++x) {
            const int dr = std::abs(qRed(ra[x]) - qRed(rb[x]));
            const int dg = std::abs(qGreen(ra[x]) - qGreen(rb[x]));
            const int db = std::abs(qBlue(ra[x]) - qBlue(rb[x]));
            int magnitude = std::max({dr, dg, db}) * 4;
            magnitude = std::min(255, magnitude);
            ro[x] = qRgb(magnitude, magnitude / 3, 0);
        }
    }
    return diffCache_;
}

void PreviewView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), palette().window());

    const QImage& image = displayImage();
    if (image.isNull()) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter,
                         tr("Drop an image here, or paste with Ctrl+V"));
        return;
    }

    const QRectF r = imageRect();

    // Checkerboard shows through transparent pixels.
    const int cell = 10;
    painter.save();
    painter.setClipRect(r);
    const QColor c1 = palette().base().color();
    const QColor c2 = palette().alternateBase().color();
    for (int y = 0; y < r.height(); y += cell) {
        for (int x = 0; x < r.width(); x += cell) {
            const QColor& color =
                (((x / cell) + (y / cell)) % 2 == 0) ? c1 : c2;
            painter.fillRect(QRectF(r.left() + x, r.top() + y, cell, cell),
                             color);
        }
    }
    painter.restore();

    painter.drawImage(r, image);

    if (cropEnabled_) {
        const QRectF crop = cropWidgetRect();
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        path.addRect(r);
        path.addRect(crop);
        painter.fillPath(path, QColor(0, 0, 0, 140));

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(palette().highlight().color(), 1.5));
        painter.drawRect(crop);

        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().highlight().color());
        const QPointF handles[8] = {
            crop.topLeft(), crop.topRight(), crop.bottomLeft(), crop.bottomRight(),
            QPointF(crop.center().x(), crop.top()),
            QPointF(crop.center().x(), crop.bottom()),
            QPointF(crop.left(), crop.center().y()),
            QPointF(crop.right(), crop.center().y())};
        for (const QPointF& point : handles) {
            painter.drawEllipse(point, kHandleRadius, kHandleRadius);
        }
        return;
    }

    if (!after_.isNull()) {
        const double lineX = r.left() + wipe_ * r.width();
        painter.save();
        painter.setClipRect(QRectF(lineX, r.top(), r.right() - lineX, r.height()));
        if (diff_) {
            painter.drawImage(r, diffImage());
        } else {
            painter.drawImage(r, after_);
        }
        painter.restore();

        painter.setPen(QPen(QColor(255, 255, 255, 210), 1.0));
        painter.drawLine(QPointF(lineX, r.top()), QPointF(lineX, r.bottom()));

        const QPointF center(lineX, r.center().y());
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().highlight().color());
        painter.drawEllipse(center, 10.0, 10.0);
        painter.setBrush(Qt::white);
        QPolygonF leftArrow;
        leftArrow << center + QPointF(-5, 0) << center + QPointF(-1, -4)
                  << center + QPointF(-1, 4);
        QPolygonF rightArrow;
        rightArrow << center + QPointF(5, 0) << center + QPointF(1, -4)
                   << center + QPointF(1, 4);
        painter.drawPolygon(leftArrow);
        painter.drawPolygon(rightArrow);
    }

    painter.setPen(QPen(palette().mid().color(), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(r.adjusted(-0.5, -0.5, 0.5, 0.5));
}

} // namespace presspng::gui
