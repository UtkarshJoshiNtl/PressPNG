#pragma once

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QWidget>

namespace presspng::gui {

// Shows the source, the compressed result, or both split by a draggable
// wipe line. Doubles as the crop editor when crop mode is on.
class PreviewView : public QWidget {
    Q_OBJECT
public:
    enum class ZoomMode { Fit, Manual };

    explicit PreviewView(QWidget* parent = nullptr);

    void setOriginal(const QImage& image);
    void setBefore(const QImage& image);
    void setAfter(const QImage& image);
    void clearAfter();
    void clearAll();

    ZoomMode zoomMode() const { return zoomMode_; }
    void setZoomMode(ZoomMode mode);
    double zoom() const { return zoom_; }
    void setZoom(double factor);
    void zoomToFit();

    bool diffEnabled() const { return diff_; }
    void setDiffEnabled(bool enabled);

    double wipe() const { return wipe_; }
    void setWipe(double fraction);

    bool cropEnabled() const { return cropEnabled_; }
    void setCropEnabled(bool enabled);
    QRectF cropRect() const { return crop_; }
    void setCropRect(const QRectF& rect);
    void resetCrop();
    void setCropAspect(double ratio); // pixel w/h; 0 = free

signals:
    void wipeChanged(double fraction);
    void zoomChanged(double factor);
    void cropChanged(const QRectF& rect);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class Drag { None, Pan, Wipe, CropMove, CropResize };
    enum class Handle { None, L, R, T, B, TL, TR, BL, BR };

    const QImage& displayImage() const;
    double currentScale() const;
    QRectF imageRect() const;
    QRectF cropWidgetRect() const;
    Handle hitHandle(const QPointF& pos) const;
    void applyCropResize(Handle handle, const QPointF& pos);
    const QImage& diffImage() const;

    QImage original_;
    QImage before_;
    QImage after_;
    mutable QImage diffCache_;
    mutable bool diffCacheDirty_ = true;

    ZoomMode zoomMode_ = ZoomMode::Fit;
    double zoom_ = 1.0;
    QPointF pan_;
    double wipe_ = 0.5;
    bool diff_ = false;

    bool cropEnabled_ = false;
    QRectF crop_{0.0, 0.0, 1.0, 1.0};
    double cropAspect_ = 0.0;

    Drag drag_ = Drag::None;
    Handle handle_ = Handle::None;
    QPointF dragStart_;
    QRectF cropStart_;
};

} // namespace presspng::gui
